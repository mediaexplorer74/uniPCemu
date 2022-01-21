/*

Copyright (C) 2020 - 2021 Superfury

This file is part of UniPCemu.

UniPCemu is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

UniPCemu is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with UniPCemu.  If not, see <https://www.gnu.org/licenses/>.
*/

#define IS_I430FX
#include "headers/hardware/i430fx.h" //Our own types!
#include "headers/hardware/pci.h" //PCI support!
#include "headers/cpu/cpu.h" //CPU reset support!
#include "headers/cpu/biu.h" //CPU reset support!
#include "headers/hardware/ports.h" //Port support!
#include "headers/mmu/mmuhandler.h" //RAM layout updating support!
#include "headers/hardware/ide.h" //IDE PCI support!
#include "headers/hardware/pic.h" //APIC support!
#include "headers/emu/emucore.h" //RESET line support!
#include "headers/bios/biosrom.h" //BIOS flash ROM support!

extern byte is_i430fx; //Are we an i430fx motherboard?
byte i430fx_memorymappings_read[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}; //All read memory/PCI! Set=DRAM, clear=PCI!
byte i430fx_memorymappings_write[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}; //All write memory/PCI! Set=DRAM, clear=PCI!
byte SMRAM_enabled[MAXCPUS] = { 0,0 }; //SMRAM enabled?
byte SMRAM_data = 1; //SMRAM responds to data accesses?
byte SMRAM_locked = 0; //Are we locked?
byte SMRAM_SMIACT[MAXCPUS] = { 0,0 }; //SMI activated
extern byte MMU_memoryholespec; //memory hole specification? 0=Normal, 1=512K, 2=15M.
byte i430fx_previousDRAM[8]; //Previous DRAM values
byte i430fx_DRAMsettings[8]; //Previous DRAM values
typedef struct
{
	byte DRAMsettings[8]; //All 5 DRAM settings to load!
	byte maxmemorysize; //Maximum memory size to use, in MB!
} DRAMInfo;
DRAMInfo i430fx_DRAMsettingslookup[8] = {
	{{0x02,0x02,0x02,0x02,0x02,0x00,0x00,0x00},8}, //up to 8MB
	{{0x02,0x04,0x04,0x04,0x04,0x00,0x00,0x00},16}, //up to 16MB
	{{0x02,0x04,0x06,0x06,0x06,0x00,0x00,0x00},24}, //up to 24MB
	{{0x04,0x08,0x08,0x08,0x08,0x00,0x00,0x00},32}, //up to 32MB
	{{0x04,0x08,0x0C,0x00,0x00,0x00,0x00,0x00},48}, //up to 48MB
	{{0x08,0x10,0x10,0x10,0x10,0x00,0x00,0x00},64}, //up to 64MB
	{{0x04,0x08,0x10,0x18,0x18,0x00,0x00,0x00},96}, //up to 96MB
	{{0x10,0x20,0x20,0x20,0x20,0x00,0x00,0x00},255} //up to 128MB. Since it's capped at 128 MB, take it for larger values as well!
};
byte effectiveDRAMsettings = 0; //Effective DRAM settings!

byte i430fx_configuration[256]; //Full configuration space!
byte i430fx_piix_configuration[256]; //Full configuration space!
byte i430fx_ide_configuration[256]; //IDE configuration!
extern PCI_GENERALCONFIG* activePCI_IDE; //For hooking the PCI IDE into a i430fx handler!

byte APMcontrol = 0;
byte APMstatus = 0;

byte ELCRhigh = 0;
byte ELCRlow = 0;

void i430fx_updateSMRAM()
{
	if ((i430fx_configuration[0x72] & 0x10) || SMRAM_locked) //Locked?
	{
		SMRAM_locked = 1; //Permanent lock!
		i430fx_configuration[0x72] &= ~0x40; //Bit is permanently cleared!
	}
	if (i430fx_configuration[0x72] & 0x40) //SMRAM enabled always?
	{
		SMRAM_enabled[0] = (i430fx_configuration[0x72] & 0x08); //Enabled!
		SMRAM_enabled[1] = (i430fx_configuration[0x72] & 0x08); //Enabled!
	}
	else
	{
		SMRAM_enabled[0] = SMRAM_SMIACT[0] && (i430fx_configuration[0x72] & 0x08); //Enabled for SMIACT!
		SMRAM_enabled[1] = SMRAM_SMIACT[1] && (i430fx_configuration[0x72] & 0x08); //Enabled for SMIACT!
	}
	SMRAM_data = (i430fx_configuration[0x72]&0x20)?0:1; //SMRAM responds to data accesses?
	MMU_RAMlayoutupdated(); //Update the RAM layout!
}

void i430fx__SMIACT(byte active)
{
	SMRAM_SMIACT[activeCPU] = active; //SMIACT#?
	i430fx_updateSMRAM(); //Update the SMRAM mapping!
}

void i430fx_resetPCIConfiguration()
{
	i430fx_configuration[0x00] = 0x86;
	i430fx_configuration[0x01] = 0x80; //Intel
	if (is_i430fx == 1) //i430fx
	{
		i430fx_configuration[0x02] = 0x2D;
		i430fx_configuration[0x03] = 0x12; //SB82437FX-66
	}
	else //i440fx?
	{
		i430fx_configuration[0x02] = 0x37;
		i430fx_configuration[0x03] = 0x12; //???
	}
	i430fx_configuration[0x04] = 0x06;
	i430fx_configuration[0x05] = 0x00;
	if (is_i430fx == 1) //i430fx?
	{
		i430fx_configuration[0x06] = 0x00;
	}
	else //i440fx?
	{
		i430fx_configuration[0x06] = 0x80;
	}
	i430fx_configuration[0x07] = 0x02; //ROM set is a 430FX?
	i430fx_configuration[0x08] = 0x00; //A0 stepping
	i430fx_configuration[0x09] = 0x00;
	i430fx_configuration[0x0A] = 0x00;
	i430fx_configuration[0x0B] = 0x06;
}

void i430fx_update_piixstatus()
{
	i430fx_piix_configuration[0x0E] = (i430fx_piix_configuration[0x0E] & ~0x7F) | ((i430fx_piix_configuration[0x6A] & 0x04) << 5); //Set the bit from the settings!
	i430fx_ide_configuration[0x0E] = (i430fx_ide_configuration[0x0E] & ~0x7F) | ((i430fx_piix_configuration[0x6A] & 0x04) << 5); //Set the bit from the settings!
}

void i430fx_piix_resetPCIConfiguration()
{
	i430fx_piix_configuration[0x00] = 0x86;
	i430fx_piix_configuration[0x01] = 0x80; //Intel
	if (is_i430fx == 1) //i430fx?
	{
		i430fx_piix_configuration[0x02] = 0x2E;
		i430fx_piix_configuration[0x03] = 0x12; //PIIX
	}
	else //i440fx?
	{
		i430fx_piix_configuration[0x02] = 0x00;
		i430fx_piix_configuration[0x03] = 0x70; //PIIX3
	}
	i430fx_piix_configuration[0x04] = 0x07|(i430fx_piix_configuration[0x04]&0x08);
	i430fx_piix_configuration[0x05] = 0x00;
	i430fx_piix_configuration[0x08] = 0x02; //A-1 stepping
	i430fx_piix_configuration[0x09] = 0x00;
	i430fx_piix_configuration[0x0A] = 0x01;
	i430fx_piix_configuration[0x0B] = 0x06;
	i430fx_update_piixstatus(); //Update the status register bit!
}

void i430fx_ide_resetPCIConfiguration()
{
	i430fx_ide_configuration[0x00] = 0x86;
	i430fx_ide_configuration[0x01] = 0x80; //Intel
	if (is_i430fx == 1) //i430fx?
	{
		i430fx_ide_configuration[0x02] = 0x30;
		i430fx_ide_configuration[0x03] = 0x12; //PIIX IDE
	}
	else //i440fx?
	{
		i430fx_ide_configuration[0x02] = 0x10;
		i430fx_ide_configuration[0x03] = 0x70; //PIIX3 IDE
	}
	i430fx_ide_configuration[0x04] = 0x05&1; //Limited use(bit 2=Bus master function, which is masked off to be disabled)
	i430fx_ide_configuration[0x05] = 0x00;
	i430fx_ide_configuration[0x08] = 0x02; //A-1 stepping
	i430fx_ide_configuration[0x09] = (i430fx_ide_configuration[0x09]&0x8F); //Not capable of IDE-bus master yet, so mask it as the IDE sets it! Keep the configuation intact!
	i430fx_ide_configuration[0x0A] = 0x01; //Sub-class
	i430fx_ide_configuration[0x0B] = 0x01; //Base-class
	i430fx_update_piixstatus(); //Update the status register bit!
}

void i430fx_map_read_memoryrange(byte start, byte size, byte maptoRAM)
{
	byte c, e;
	e = start + size; //How many entries?
	for (c = start; c < e; ++c) //Map all entries!
	{
		i430fx_memorymappings_read[c] = maptoRAM; //Set it to the RAM mapping(1) or PCI mapping(0)!
	}
	MMU_RAMlayoutupdated(); //Update the RAM layout!
}

void i430fx_map_write_memoryrange(byte start, byte size, byte maptoRAM)
{
	byte c,e;
	e = start + size; //How many entries?
	for (c = start; c < e; ++c) //Map all entries!
	{
		i430fx_memorymappings_write[c] = maptoRAM; //Set it to the RAM mapping(1) or PCI mapping(0)!
	}
	MMU_RAMlayoutupdated(); //Update the RAM layout!
}

void i430fx_mapRAMROM(byte start, byte size, byte setting)
{
	switch (setting&3) //What kind of mapping?
	{
	case 0: //Read=PCI, Write=PCI!
		i430fx_map_read_memoryrange(start, size, 0); //Map to PCI for reads!
		i430fx_map_write_memoryrange(start, size, 0); //Map to PCI for writes!
		break;
	case 1: //Read=RAM, write=PCI
		i430fx_map_read_memoryrange(start, size, 1); //Map to RAM for reads!
		i430fx_map_write_memoryrange(start, size, 0); //Map to PCI for writes!
		break;
	case 2: //Read=PCI, write=RAM
		i430fx_map_read_memoryrange(start, size, 0); //Map to PCI for reads!
		i430fx_map_write_memoryrange(start, size, 1); //Map to RAM for writes!
		break;
	case 3: //Read=RAM, Write=RAM
		i430fx_map_read_memoryrange(start, size, 1); //Map to RAM for reads!
		i430fx_map_write_memoryrange(start, size, 1); //Map to RAM for writes!
		break;
	default:
		break;
	}
}

extern byte PCI_transferring;
extern byte BIOS_writeprotect; //BIOS write protected?
void i430fx_hardreset(); //Prototype for register 93h on the i440fx.

void i430fx_PCIConfigurationChangeHandler(uint_32 address, byte device, byte function, byte size)
{
	PCI_GENERALCONFIG* config = (PCI_GENERALCONFIG*)&i430fx_configuration; //Configuration generic handling!
	i430fx_resetPCIConfiguration(); //Reset the ROM values!
	switch (address) //What configuration is changed?
	{
	case 0x10:
	case 0x11:
	case 0x12:
	case 0x13:
	case 0x14:
	case 0x15:
	case 0x16:
	case 0x17:
	case 0x18:
	case 0x19:
	case 0x1A:
	case 0x1B:
	case 0x1C:
	case 0x1D:
	case 0x1E:
	case 0x1F:
	case 0x20:
	case 0x21:
	case 0x22:
	case 0x23:
	case 0x24:
	case 0x25:
	case 0x26:
	case 0x27: //BAR?
	case 0x30:
	case 0x31:
	case 0x32:
	case 0x33: //Expansion ROM address?
		if (PCI_transferring == 0) //Finished transferring data for an entry?
		{
			PCI_unusedBAR(config, 0); //Unused
			PCI_unusedBAR(config, 1); //Unused
			PCI_unusedBAR(config, 2); //Unused
			PCI_unusedBAR(config, 3); //Unused
			PCI_unusedBAR(config, 4); //Unused
			PCI_unusedBAR(config, 5); //Unused
			PCI_unusedBAR(config, 6); //Unused
		}
		break;
	case 0x57: //DRAMC - DRAM control register
		switch (((i430fx_configuration[0x57] >> 6) & 3)) //What memory hole to emulate?
		{
		case 0: //None?
			MMU_memoryholespec = 1; //Disabled!
			break;
		case 1: //512K-640K?
			if (is_i430fx == 1) //i430fx?
			{
				MMU_memoryholespec = 2; //512K memory hole!
			}
			else //i440fx? Won't run properly with it enabled?
			{
				MMU_memoryholespec = 1; //Disabled!
			}
			break;
		case 2: //15-16MB?
			MMU_memoryholespec = 3; //15M memory hole!
			break;
		case 3: //Reserved?
			MMU_memoryholespec = 1; //Disabled!
			break;
		}
		break;
	case 0x59: //BIOS ROM at 0xF0000? PAM0
		i430fx_mapRAMROM(0xC, 4, (i430fx_configuration[0x59] >> 4)); //Set it up!
		//bit 4 sets some shadow BIOS setting? It's shadowing the BIOS in that case(Read=RAM setting)!
		break;
	case 0x5A: //PAM1
	case 0x5B: //PAM2
	case 0x5C: //PAM3
	case 0x5D: //PAM4
	case 0x5E: //PAM5
	case 0x5F: //RAM/PCI switches at 0xC0000-0xF0000? PAM6
		address -= 0x5A; //What PAM register number(0-based)?
		i430fx_mapRAMROM((address<<1), 1, (i430fx_configuration[address+0x5A] & 0xF)); //Set it up!
		i430fx_mapRAMROM(((address<<1)|1), 1, (i430fx_configuration[address+0x5A] >> 4)); //Set it up!
		break;
	case 0x65:
	case 0x66:
	case 0x67: //3 more on i440fx?
		if (!(is_i430fx == 1)) break; //Not i440fx?
	case 0x60:
	case 0x61:
	case 0x62:
	case 0x63:
	case 0x64:
		//DRAM module detection?
		if (is_i430fx==1) //i430fx?
		{
			i430fx_configuration[address] &= 0x3F; //Only 6 bits/row!
		}
		//DRAM auto detection!
		if (is_i430fx == 2) //i440fx?
		{
			memcpy(&i430fx_configuration[0x60], &i430fx_DRAMsettings, 8); //Set all DRAM setting registers to the to be detected value!
		}
		else //i430fx?
		{
			memcpy(&i430fx_configuration[0x60], &i430fx_DRAMsettings, 5); //Set all DRAM setting registers to the to be detected value!
		}
		break;
	case 0x72: //SMRAM?
		i430fx_updateSMRAM();
		break;
	case 0x93: //Turbo Reset Control Register (i440fx)
		if (is_i430fx == 2) //i440fx?
		{
			//Same behaviour for bits 2 and 1 as with the CF9 register.
			if (i430fx_configuration[0x93] & 4) //Set while not set yet during a direct access?
			{
				//Should reset all PCI devices?
				if (i430fx_configuration[0x93] & 2) //Hard reset?
				{
					i430fx_hardreset(); //Perform a full hard reset of the hardware!
					//CPU bist mode can be enabled as well(bit 3 of this register) with a hard reset!
				}
				emu_raise_resetline(1 | 4); //Start pending reset!
				i430fx_configuration[0x93] &= ~4; //Cannot be read as a 1, according to documentation!
			}
		}
		break;
	default: //Not emulated?
		break; //Ignore!
	}
}

extern byte motherboard_responds_to_shutdown; //Motherboard responds to shutdown?
uint_32 i440fx_ioapic_base_mask = 0;
uint_32 i440fx_ioapic_base_match = 0;

void i430fx_piix_PCIConfigurationChangeHandler(uint_32 address, byte device, byte function, byte size)
{
	PCI_GENERALCONFIG* config = (PCI_GENERALCONFIG*)&i430fx_piix_configuration; //Configuration generic handling!
	i430fx_piix_resetPCIConfiguration(); //Reset the ROM fields!
	switch (address) //What address has been updated?
	{
	case 0x04: //Command?
		i430fx_piix_configuration[0x04] &= 0x08; //Limited response! All not set bits are cleared but this one! This affects is we're responding to shutdown?
		i430fx_piix_configuration[0x04] |= 0x07; //Always set!
		motherboard_responds_to_shutdown = ((i430fx_piix_configuration[0x04] & 8) >> 3); //Do we respond to a shutdown cycle?
		break;
	case 0x05: //Command
		i430fx_piix_configuration[0x05] = 0x00; //All read-only!
		break;
	case 0x06: //PCI status?
		i430fx_piix_configuration[0x06] = 0x00; //Unchangable!
		break;
	case 0x07: //PCI status low?
		i430fx_piix_configuration[0x07] &= ~0xC1; //Always cleared!
		i430fx_piix_configuration[0x07] &= (~0x38) | ((~i430fx_piix_configuration[0x07]) & 0x38); //Bits 5-3(13-11 of the word register) are cleared by writing a 1 to their respective bits!
		break;
	case 0x10:
	case 0x11:
	case 0x12:
	case 0x13:
	case 0x14:
	case 0x15:
	case 0x16:
	case 0x17:
	case 0x18:
	case 0x19:
	case 0x1A:
	case 0x1B:
	case 0x1C:
	case 0x1D:
	case 0x1E:
	case 0x1F:
	case 0x20:
	case 0x21:
	case 0x22:
	case 0x23:
	case 0x24:
	case 0x25:
	case 0x26:
	case 0x27: //BAR?
	case 0x30:
	case 0x31:
	case 0x32:
	case 0x33: //Expansion ROM address?
		if (PCI_transferring == 0) //Finished transferring data for an entry?
		{
			PCI_unusedBAR(config, 0); //Unused
			PCI_unusedBAR(config, 1); //Unused
			PCI_unusedBAR(config, 2); //Unused
			PCI_unusedBAR(config, 3); //Unused
			PCI_unusedBAR(config, 4); //Unused
			PCI_unusedBAR(config, 5); //Unused
			PCI_unusedBAR(config, 6); //Unused
		}
		break;
	case 0x4C: //ISA Recovery I/O timer register
		//Bit 7 set: alias ports 80h, 84-86h, 88h, 8c-8eh to 90-9fh.
		break;
	case 0x4F:
	case 0x4E: //X-bus chip select register
		//bit 8 set: Enable IO APIC space (i440fx).
		//bit 7 set: alias PCI FFF80000-FFFDFFFF at F80000-FDFFFF (extended bios).
		//bit 6 set: alias PCI FFFE0000-FFFFFFFF at FE0000-FFFFFF (lower bios).
		//bit 5 set: FERR# to IRQ13, otherwise disabled (i440fx).
		//bit 4 set: IRQ12/Mouse function enable. 1=Mouse function, 0=Standard IRQ12 interrupt function (i440fx)!
		//bit 2 set: BIOS write protect enable(0=Protected, 1=Not protected).
		//bit 1 set: Enable keyboard Chip-Select for address 60h and 64h.
		//bit 0 set: Enable RTC for addresses 70-77h.
		if ((i430fx_piix_configuration[0x4F] & 1) && (is_i430fx == 2)) //Enabled the IO APIC?
		{
			APIC_enableIOAPIC(1); //Enable the IO APIC!
		}
		else //Not supported or disabled?
		{
			APIC_enableIOAPIC(0); //Disable the IO APIC!
		}
		if (i430fx_piix_configuration[0x4E] & 4) //Write protect is disabled?
		{
			BIOS_writeprotect = 0; //Write protect is disabled!
		}
		else
		{
			BIOS_writeprotect = 1; //Write protect is enabled!
		}
		break;
	case 0x60:
	case 0x61:
	case 0x62:
	case 0x63: //IRQA-IRQD PCI interrupt routing control
		//bit 7 set: disable
		//bits 3-0: IRQ number, except 0-2, 8 and 13.
		break;
	case 0x69: //Top of memory register
		//bits 7-4: Top of memory, in MB-1.
		//bit 3: forward lower bios to PCI(register 4E isn't set for the lower BIOS)? 0=Contain to ISA.
		//bit 1: forward 512-640K region to PCI instead of ISA. 0=Contain to ISA.
		break;
	case 0x6A: //Miscellaneous Status Register
		i430fx_update_piixstatus(); //Update the Misc Status!
		break;
	case 0x70: //MBIRQ0
	case 0x71:  //MBIRQ1
		//bit 7: Interrupt routing enable
		//bit 6: MIRQx/IRQx sharing enable. When 0 and Interrupt routine Enable is cleared, the IRQ is masked.
		//bits 3-0: IRQ line to connect to: 0-3. 8 and 13 are invalid.
		break;
	case 0x78:
	case 0x79: //Programmable Chip-Select control register
		//bit 15-2: 16-bit I/O address (dword accessed) that causes PCS# to be asserted.
		//bit 1-0: Address mask? 0=4 bytes, 1=8 bytes, 2=Disabled, 3=16 bytes.
		break;
	case 0x80: //PIIX-3: APIC base address register
		//bit6=Mask A12 off(aliasing)
		//bit 5-2: x, Compared against bit 15-12
		//bit 1-0: y, Compared against bit 11-10. Values: 00b=0, 01b=4, 10b=8, 11b=C
		if (is_i430fx==2) //i440fx?
		{
			//Determine address mask!
			i440fx_ioapic_base_mask = 0xFC0; //Mask against bits 10-15!
			if (i430fx_piix_configuration[0x80]&0x40) //Mask A12 off?
			{
				i440fx_ioapic_base_mask &= ~(1<<12); //Mask A12 off!
			}
			//Determine masked match!
			i440fx_ioapic_base_match = ((i430fx_piix_configuration[0x80]&0x3F)<<10); //What to match against!
			i440fx_ioapic_base_match &= i440fx_ioapic_base_mask; //Match properly masked!
		}
		else //Match default address only on i430fx?
		{
			i440fx_ioapic_base_mask = 0xFE0; //Mask against bits 9-15!
			i440fx_ioapic_base_match = 0; //At the start only! So 000 and 010 only is valid!
		}
		break;
	case 0xA0: //SMI control register
		//bit 4-3: Fast off timer count granularity. 1=Disabled.
		//it's 1(at 33MHz PCICLK or 1.1 at 30MHz or 1.32 at 25MHz) minute(when 0), disabled(when 1), 1 PCICLK(when 2), 1(or 1.1 at 33MHz PCICLK, 1.32 at 25MHz
		//bit 2: STPCLK# controlled by high and low timer registers
		//bit 1: APMC read causes assertion of STPCLK#.
		//bit 0: SMI Gate. 1=Enabled, 0=Disable.
		break;
	case 0xA2: //SMI Enable register
		//What triggers an SMI:
		//bit 7: APMC
		//bit 6: EXTSMI#
		//bit 5: Fast Off Timer
		//bit 4: IRQ12(PS/2 mouse)
		//bit 3: IRQ8(RTC Alarm)
		//bit 2: IRQ4(COM2/COM4)
		//bit 1: IRQ3(COM1/COM3)
		//bit 0: IRQ1(PS/2 keyboard)
		break;
	case 0xA4:
	case 0xA5:
	case 0xA6:
	case 0xA7: //System Event Enable Register
		//bit 31: Fast off SMI enable
		//bit 29: fast off NMI enable
		//bit 15-3: Fast off IRQ #<bit> enable
		//bit 1-0: Fast off IRQ #<bit> enable.
		break;
	case 0xA8: //Fast off timer register
		//Reload value of N+1, a read gives the last value written. Countdown to 0 reloads with N+1 and triggers an SMI.
		break;
	case 0xAA:
	case 0xAB: //SMI Request Register
		//What caused an SMI:
		//bit 7: write to APM control register
		//bit 6: extSM#
		//bit 5: Fast off timer
		//bit 4: IRQ12
		//bit 3: IRQ8
		//bit 2: IRQ4
		//bit 1: IRQ3
		//bit 0: IRQ1
		break;
	case 0xAC: //STPCLK# low timer
	case 0xAE: //STPCLK high timer
		//Number of clocks for each STPCLK# transition to/from high,low. PCI clocks=1+(1056*(n+1))
		break;
	}
}

void i430fx_ide_PCIConfigurationChangeHandler(uint_32 address, byte device, byte function, byte size)
{
	switch (address) //Special handling of some layouts specific to us!
	{
	case 0x04: //Command?
		i430fx_ide_configuration[0x04] &= 0x5; //Limited!
		i430fx_ide_configuration[0x04] |= 2; //Always set!
		break;
	case 0x06: //PCI status?
		i430fx_ide_configuration[0x06] = 0x80; //Unchangable!
		break;
	case 0x07: //PCI status low?
		i430fx_ide_configuration[0x07] &= ~0xC1; //Always cleared!
		i430fx_ide_configuration[0x07] &= (~0x38) | ((~i430fx_ide_configuration[0x07]) & 0x38); //Bits 5-3(13-11 of the word register) are cleared by writing a 1 to their respective bits!
		break;
	case 0x0D: //Master Latency timer register?
		i430fx_ide_configuration[0x0D] &= 0xF0; //Lower half always 0!
		break;
	default: //Unspecified in the documentation?
		//Let the IDE handler handle this!
		break;
	}
	ATA_ConfigurationSpaceChanged(address, device, function, size); //Normal ATA/ATAPI handler passthrough!
	i430fx_ide_resetPCIConfiguration(); //Reset the ROM fields!
}

void i430fx_hardreset()
{
	byte address;
	memset(&i430fx_memorymappings_read, 0, sizeof(i430fx_memorymappings_read)); //Default to PCI!
	memset(&i430fx_memorymappings_write, 0, sizeof(i430fx_memorymappings_write)); //Default to PCI!
	memset(&i430fx_configuration, 0, sizeof(i430fx_configuration)); //Initialize the configuration!
	memset(&i430fx_piix_configuration, 0, sizeof(i430fx_piix_configuration)); //Initialize the configuration!

	if (!is_i430fx) //Not this chipset?
	{
		return; //nothing to do!
	}

	BIOS_flash_reset(); //Reset the BIOS flash because of hard or soft reset of PCI devices!

	i430fx_resetPCIConfiguration(); //Initialize/reset the configuration!
	i430fx_piix_resetPCIConfiguration(); //Initialize/reset the configuration!
	i430fx_ide_resetPCIConfiguration(); //Initialize/reset the configuration!

	if (is_i430fx!=1) //i440fx?
	{
		i430fx_configuration[0x51] = 0x01;
		i430fx_configuration[0x58] = 0x10;
		i430fx_configuration[0xB4] = 0x00;
		i430fx_configuration[0xB9] = 0x00;
		i430fx_configuration[0xBA] = 0x00;
		i430fx_configuration[0xBB] = 0x00;
		i430fx_configuration[0x93] = 0x00; //Turbo Reset Control register
	}

	//Initialize DRAM module detection!
	if (is_i430fx==1) //i430fx?
	{
		memset(&i430fx_configuration[0x60], 2, 5); //Initialize the DRAM settings!
	}
	else //i440fx?
	{
		memset(&i430fx_configuration[0x60], 1, 8); //Initialize the DRAM settings!
	}

	MMU_memoryholespec = 1; //Default: disabled!
	i430fx_configuration[0x59] = 0xF; //Default configuration setting when reset!
	i430fx_configuration[0x57] = 0x01; //Default memory hole setting!
	i430fx_configuration[0x72] = 0x02; //Default SMRAM setting!

	//Known and unknown registers:
	i430fx_configuration[0x52] = 0x02; //0x40: 256kB PLB cache(originally 0x42)? 0x00: No cache installed? 0x02: No cache installed and force cache miss?
	i430fx_configuration[0x53] = 0x14; //ROM set is a 430FX?
	i430fx_configuration[0x56] = 0x52; //ROM set is a 430FX? DRAM control
	i430fx_configuration[0x57] = 0x01;
	i430fx_PCIConfigurationChangeHandler(0x57, 3, 0, 1); //Initialize all memory hole settings!
	i430fx_configuration[0x69] = 0x03; //ROM set is a 430FX?
	i430fx_configuration[0x70] = 0x20; //ROM set is a 430FX?
	i430fx_configuration[0x72] = 0x02;
	i430fx_configuration[0x74] = 0x0E; //ROM set is a 430FX?
	i430fx_configuration[0x78] = 0x23; //ROM set is a 430FX?

	PCI_GENERALCONFIG* config = (PCI_GENERALCONFIG*)&i430fx_configuration; //Configuration generic handling!
	PCI_GENERALCONFIG* config_piix = (PCI_GENERALCONFIG*)&i430fx_piix_configuration; //Configuration generic handling!
	PCI_GENERALCONFIG* config_ide = (PCI_GENERALCONFIG*)&i430fx_ide_configuration; //Configuration generic handling!
	PCI_unusedBAR(config, 0); //Unused!
	PCI_unusedBAR(config, 1); //Unused!
	PCI_unusedBAR(config, 2); //Unused!
	PCI_unusedBAR(config, 3); //Unused!
	PCI_unusedBAR(config, 4); //Unused!
	PCI_unusedBAR(config, 5); //Unused!
	PCI_unusedBAR(config, 6); //Unused!
	PCI_unusedBAR(config_piix, 0); //Unused!
	PCI_unusedBAR(config_piix, 1); //Unused!
	PCI_unusedBAR(config_piix, 2); //Unused!
	PCI_unusedBAR(config_piix, 3); //Unused!
	PCI_unusedBAR(config_piix, 4); //Unused!
	PCI_unusedBAR(config_piix, 5); //Unused!
	PCI_unusedBAR(config_piix, 6); //Unused!
	PCI_unusedBAR(config_ide, 4); //Unused!
	PCI_unusedBAR(config_ide, 5); //Unused!
	PCI_unusedBAR(config_ide, 6); //Unused!

	//Initalize all mappings!
	for (address = 0x59; address <= 0x5F; ++address) //Initialize us!
	{
		i430fx_PCIConfigurationChangeHandler(address, 3, 0, 1); //Initialize all required settings!
	}

	i430fx_piix_configuration[0x04] |= 8; //Default: enable special cycles!
	i430fx_piix_PCIConfigurationChangeHandler(0x04, 3, 0, 1); //Initialize all required settings!

	i430fx_piix_configuration[0x06] = 0x00;
	i430fx_piix_configuration[0x07] = 0x02; //ROM set is a 430FX?

	i430fx_piix_configuration[0x6A] = 0x04; //Default value: PCI Header type bit enable set!
	i430fx_piix_PCIConfigurationChangeHandler(0x6A, 3, 0, 1); //Initialize all required settings!

	i430fx_piix_configuration[0x4C] = 0x4D; //Default
	i430fx_piix_configuration[0x4E] = 0x03; //Default
	i430fx_piix_configuration[0x4F] = 0x00; //Default
	i430fx_piix_PCIConfigurationChangeHandler(0x4E, 3, 0, 1); //Initialize all memory hole settings!
	i430fx_piix_PCIConfigurationChangeHandler(0x4F, 3, 0, 1); //Initialize all memory hole settings!

	//IRQ mappings
	i430fx_piix_configuration[0x60] = 0x80; //Default value: No IRQ mapped!
	i430fx_piix_configuration[0x61] = 0x80; //Default value: No IRQ mapped!
	i430fx_piix_configuration[0x62] = 0x80; //Default value: No IRQ mapped!
	i430fx_piix_configuration[0x63] = 0x80; //Default value: No IRQ mapped!
	i430fx_piix_configuration[0x70] = 0x80; //Default value: No IRQ mapped!
	i430fx_piix_configuration[0x71] = 0x80; //Default value: No IRQ mapped!
	i430fx_piix_PCIConfigurationChangeHandler(0x60, 3, 0, 1); //Initialize all required settings!
	i430fx_piix_PCIConfigurationChangeHandler(0x61, 3, 0, 1); //Initialize all required settings!
	i430fx_piix_PCIConfigurationChangeHandler(0x62, 3, 0, 1); //Initialize all required settings!
	i430fx_piix_PCIConfigurationChangeHandler(0x63, 3, 0, 1); //Initialize all required settings!
	i430fx_piix_PCIConfigurationChangeHandler(0x70, 3, 0, 1); //Initialize all required settings!
	i430fx_piix_PCIConfigurationChangeHandler(0x71, 3, 0, 1); //Initialize all required settings!

	i430fx_piix_configuration[0x76] = 0x0C; //Default value: ISA compatible!
	i430fx_piix_configuration[0x77] = 0x0C; //Default value: No DMA F used!
	i430fx_piix_configuration[0x78] = 0x02; //Default value: PCSC mapping: disabled!
	i430fx_piix_configuration[0x79] = 0x00; //Default value: PCSC mapping: 0!

	i430fx_piix_PCIConfigurationChangeHandler(0x80, 3, 0, 1); //Initialize all required settings!

	i430fx_piix_configuration[0xA0] = 0x08; //Default value: SMI clock disabled!
	i430fx_piix_configuration[0xA2] = 0x00; //Default value: SMI disabled!
	i430fx_piix_configuration[0xA3] = 0x00; //Default value: SMI disabled!
	i430fx_piix_configuration[0xA4] = 0x00; //Default value: SMI disabled!
	i430fx_piix_configuration[0xA5] = 0x00; //Default value: SMI disabled!
	i430fx_piix_configuration[0xA6] = 0x00; //Default value: SMI disabled!
	i430fx_piix_configuration[0xA7] = 0x00; //Default value: SMI disabled!
	i430fx_piix_configuration[0xA8] = 0x0F; //Default value: Fast Off timer!
	i430fx_piix_configuration[0xAA] = 0x00; //Default value: SMI request (cause) register!
	i430fx_piix_configuration[0xAB] = 0x00; //Default value: SMI request (cause) register!
	i430fx_piix_configuration[0xAC] = 0x00; //Default value: STPCLK low!
	i430fx_piix_configuration[0xAE] = 0x00; //Default value: STPCLK high!
	i430fx_piix_PCIConfigurationChangeHandler(0xA0, 3, 0, 1); //Initialize all required settings!
	i430fx_piix_PCIConfigurationChangeHandler(0xA2, 3, 0, 1); //Initialize all required settings!
	i430fx_piix_PCIConfigurationChangeHandler(0xA3, 3, 0, 1); //Initialize all required settings!
	i430fx_piix_PCIConfigurationChangeHandler(0xA4, 3, 0, 1); //Initialize all required settings!
	i430fx_piix_PCIConfigurationChangeHandler(0xA5, 3, 0, 1); //Initialize all required settings!
	i430fx_piix_PCIConfigurationChangeHandler(0xA6, 3, 0, 1); //Initialize all required settings!
	i430fx_piix_PCIConfigurationChangeHandler(0xA7, 3, 0, 1); //Initialize all required settings!
	i430fx_piix_PCIConfigurationChangeHandler(0xA8, 3, 0, 1); //Initialize all required settings!

	i430fx_piix_configuration[0x69] = 0x02; //Top of memory: 1MB

	//PCI IDE registers reset
	i430fx_ide_configuration[0x06] = 0x80;
	i430fx_ide_configuration[0x07] = 0x02; //Status
	i430fx_ide_configuration[0x0D] = 0x00; //Master Latency Timer register
	i430fx_ide_configuration[0x0E] &= 0x80; //Header type: cleared
	i430fx_ide_configuration[0x40] = 0x00; //IDE timing primary
	i430fx_ide_configuration[0x41] = 0x00; //IDE timing primary
	i430fx_ide_configuration[0x42] = 0x00; //IDE timing secondary
	i430fx_ide_configuration[0x43] = 0x00; //IDE timing secondary

	i430fx_update_piixstatus(); //Update the status register bit for the IDE controller!
	i430fx_ide_PCIConfigurationChangeHandler(0x09, 3, 1, 1); //Update Programming Interface to be as specified by the emulated controller!
	i430fx_ide_PCIConfigurationChangeHandler(0x0D, 3, 1, 1); //Update Layency Timer register!
	i430fx_ide_PCIConfigurationChangeHandler(0x40, 3, 1, 1); //Update primary timing!
	i430fx_ide_PCIConfigurationChangeHandler(0x41, 3, 1, 1); //Update primary timing!
	i430fx_ide_PCIConfigurationChangeHandler(0x42, 3, 1, 1); //Update primary timing!
	i430fx_ide_PCIConfigurationChangeHandler(0x43, 3, 1, 1); //Update primary timing!

	SMRAM_locked = 0; //Unlock SMRAM always!
	SMRAM_SMIACT[0] = 0; //Default: not active!
	SMRAM_SMIACT[1] = 0; //Default: not active!
	i430fx_updateSMRAM(); //Update the SMRAM setting!

	APMcontrol = APMstatus = 0; //Initialize APM registers!
	ELCRhigh = ELCRlow = 0; //Initialize the ELCR registers!

	if (is_i430fx==2) //i440fx?
	{
		//Affect the I/O APIC as well!
		resetIOAPIC(1); //Hard reset on the I/O APIC!
	}
}

extern uint_32 PCI_address; //What address register is currently set?
extern BIU_type BIU[MAXCPUS]; //BIU definition!
void i430fx_writeaddr(byte index, byte *value) //Written an address?
{
	if (index == 1) //Written bit 2 of register CF9h?
	{
		if ((*value & 4) && ((PCI_address & 0x400)==0) && (BIU[activeCPU].newtransfer<=1) && (BIU[activeCPU].newtransfer_size==1)) //Set while not set yet during a direct access?
		{
			//Should reset all PCI devices?
			if (*value & 2) //Hard reset?
			{
				i430fx_hardreset(); //Perform a full hard reset of the hardware!
			}
			emu_raise_resetline(1|4); //Raise the RESET line!
			*value &= ~4; //Cannot be read as a 1, according to documentation!
		}
	}
}

void i430fx_postwriteaddr(byte index)
{
}

byte readAPM(word port, byte* value)
{
	if (likely((port < 0xB2) || (port > 0xB3))) return 0; //Not us!
	switch (port)
	{
	case 0xB2: //APM Control(APMC)
		*value = APMcontrol; //Give the control register!
		break;
	case 0xB3: //APM Status (APMS)
		*value = APMstatus; //Give the status!
		break;
	}
	return 1; //Give the value!
}

byte writeAPM(word port, byte value)
{
	if (likely((port < 0xB2) || (port > 0xB3))) return 0; //Not us!
	switch (port)
	{
	case 0xB2: //APM Control(APMC)
		APMcontrol = value; //Store the value for reading later!
		//Write: can generate an SMI, depending on bit 7 of SMI Enable register and bit 0 of SMI control register both being set.
		break;
	case 0xB3: //APM Status (APMS)
		APMstatus = value; //Store the value for reading by the handler!
		break;
	}
	return 1; //Give the value!
}

byte readELCR(word port, byte* value)
{
	if (likely((port < 0x4D0) || (port > 0x4D1))) return 0; //Not us!
	switch (port)
	{
	case 0x4D0: //ELCR1
		*value = ELCRlow; //Low!
		break;
	case 0x4D1: //ELCR2
		*value = ELCRhigh; //High!
		break;
	}
	return 1; //Give the value!
}

byte writeELCR(word port, byte value)
{
	if (likely((port < 0x4D0) || (port > 0x4D1))) return 0; //Not us!
	switch (port)
	{
	case 0x4D0: //ELCR1
		ELCRlow = value; //Low!
		break;
	case 0x4D1: //ELCR2
		ELCRhigh = value; //High!
		break;
	}
	return 1; //Give the value!
}

byte i430fx_piix_portremapper(word *port, byte size, byte isread)
{
	if (size != 1) return 1; //Passthrough with invalid size!
	return 1; //Passthrough by default!
}

void i430fx_MMUready()
{
	byte b;
	byte i440fx_types[3] = { 128, 32, 8 };
	word i440fx_row; //Currently processing row!
	byte i440fx_DRBval; //Last DRB value!
	byte i440fx_rowtype;
	byte memorydetection;
	uint_32 i440fx_ramsize;
	uint_32 i440fx_divideresult;
	//First, detect the DRAM settings to use!
	if (is_i430fx==1) //i430fx? Perform the lookup style calculation!
	{
		memset(&i430fx_DRAMsettings, 0, sizeof(i430fx_DRAMsettings)); //Initialize the variable!
		effectiveDRAMsettings = 0; //Default DRAM settings is the first entry!
		for (memorydetection = 0; memorydetection < NUMITEMS(i430fx_DRAMsettingslookup); ++memorydetection) //Check all possible memory sizes!
		{
			if (MEMsize() <= (i430fx_DRAMsettingslookup[memorydetection].maxmemorysize << 20U)) //Within the limits of the maximum memory size?
			{
				effectiveDRAMsettings = memorydetection; //Use this memory size information!
			}
		}
		//effectiveDRAMsettings now points to the DRAM information to use!
		memcpy(&i430fx_DRAMsettings, &i430fx_DRAMsettingslookup[effectiveDRAMsettings], sizeof(i430fx_DRAMsettings)); //Setup the DRAM settings to use!
	}
	else if (is_i430fx==2) //i440fx? Calculate the rows!
	{
		memset(&i430fx_DRAMsettings, 0, sizeof(i430fx_DRAMsettings)); //Initialize to 0MB detected!
		i440fx_ramsize = MEMsize(); //The size of the installed RAM!
		i440fx_ramsize >>= 20; //In MB!
		i440fx_ramsize = LIMITRANGE(i440fx_ramsize, 8, 1024); //At least 8MB, at most 1GB!
		i440fx_DRBval = 0;
		i440fx_row = 0;
		i440fx_rowtype = 0;
		for (; i440fx_ramsize && (i440fx_row < 8) && (i440fx_rowtype < 3);) //Left to process?
		{
			i440fx_divideresult = i440fx_ramsize / i440fx_types[i440fx_rowtype]; //How many times inside the type!
			i440fx_ramsize = SAFEMOD(i440fx_ramsize, i440fx_types[i440fx_rowtype]); //How much is left to process!
			for (b = 0; b < i440fx_divideresult; ++b)
			{
				i440fx_DRBval += (i440fx_types[i440fx_rowtype] >> 3); //Add multiples of 8MB!
				i430fx_DRAMsettings[i440fx_row++] = i440fx_DRBval; //Set a new row that's detected!
				if (i440fx_row == 8) goto finishi440fxBankDetection; //Finish up if needed!
			}
			++i440fx_rowtype; //Next rowtype to try!
		}
		finishi440fxBankDetection:
		for (; i440fx_row < 8;) //Not fully filled?
		{
			i430fx_DRAMsettings[i440fx_row++] = i440fx_DRBval; //Fill the remainder with more of the last used row to indicate nothing is added anymore!
		}
	}
}

void init_i430fx()
{
	effectiveDRAMsettings = 0; //Effective DRAM settings to take effect! Start at the first entry, which is the minimum!
	i440fx_ioapic_base_mask = 0xFE0; //What bits to match!
	i440fx_ioapic_base_match = 0; //The value it needs to be!

	//Register PCI configuration space?
	if (is_i430fx) //Are we enabled?
	{
		register_PCI(&i430fx_configuration, 3, 0, (sizeof(i430fx_configuration)>>2), &i430fx_PCIConfigurationChangeHandler); //Register ourselves to PCI!
		MMU_memoryholespec = 1; //Our specific specification!
		register_PCI(&i430fx_piix_configuration, 4, 0, (sizeof(i430fx_piix_configuration) >> 2), &i430fx_piix_PCIConfigurationChangeHandler); //Register ourselves to PCI!
		register_PCI(&i430fx_ide_configuration, 4, 1, (sizeof(i430fx_ide_configuration) >> 2), &i430fx_ide_PCIConfigurationChangeHandler); //Register ourselves to PCI!
		activePCI_IDE = (PCI_GENERALCONFIG *)&i430fx_ide_configuration; //Use our custom handler!
		//APM registers
		register_PORTIN(&readAPM);
		register_PORTOUT(&writeAPM);
		//ECLR registers
		register_PORTIN(&readELCR);
		register_PORTOUT(&writeELCR);
		//Port remapping itself!
		register_PORTremapping(&i430fx_piix_portremapper);
	}

	i430fx_hardreset(); //Perform a hard reset of the hardware!
}

void done_i430fx()
{
	//Nothing to be done!
}
