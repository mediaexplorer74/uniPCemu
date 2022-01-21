/*

Copyright (C) 2019 - 2021 Superfury

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

#include "headers/types.h" //Basic types!
#include "headers/hardware/vga/vga.h" //Basic VGA!
#include "headers/hardware/vga/svga/tseng.h" //Our own typedefs!
#include "headers/support/zalloc.h" //Memory allocation for our override support!
#include "headers/hardware/vga/vga_precalcs.h" //Precalculation typedefs etc.
#include "headers/hardware/vga/vga_attributecontroller.h" //Attribute controller support!
#include "headers/hardware/vga/vga_sequencer_graphicsmode.h" //Graphics mode support!
#include "headers/hardware/vga/vga_dacrenderer.h" //DAC rendering support!
#include "headers/hardware/vga/vga_vram.h" //Mapping support for different addressing modes!
#include "headers/cpu/cpu.h" //NMI support!
#include "headers/hardware/vga/vga_vramtext.h" //Extended text mode support!
#include "headers/hardware/pic.h" //IRQ support!
#include "headers/mmu/mmuhandler.h" //Memory mapping support!

//Log unhandled (S)VGA accesses on the ET34k emulation?
//#define LOG_UNHANDLED_SVGA_ACCESSES

#ifdef LOG_UNHANDLED_SVGA_ACCESSES
#include "headers/support/log.h" //Logging support!
#endif

// From the depths of X86Config, probably inexact
DOUBLE ET4K_clockFreq[16] = {
	50000000.0, //25MHz: VGA standard clock: 50MHz instead?
	66000000.0, //28MHz: VGA standard clock: 66MHz instead?
	32400000.0, //ET3/4000 clock!
	35900000.0, //ET3/4000 clock!
	39900000.0, //ET3/4000 clock!
	44700000.0, //ET3/4000 clock!
	31400000.0, //ET3/4000 clock!
	37500000.0, //ET3/4000 clock!
	50000000.0, //ET4000 clock!
	56500000.0, //ET4000 clock!
	64900000.0, //ET4000 clock!
	71900000.0, //ET4000 clock!
	79900000.0, //ET4000 clock!
	89600000.0, //ET4000 clock!
	62800000.0, //ET4000 clock!
	74800000.0 //ET4000 clock!
};

DOUBLE ET3K_clockFreq[16] = {
	50000000.0, //25MHz: VGA standard clock: 50MHz instead?
	66000000.0, //28MHz: VGA standard clock: 66MHz instead?
	32400000.0, //ET3/4000 clock!
	35900000.0, //ET3/4000 clock!
	39900000.0, //ET3/4000 clock!
	44700000.0, //ET3/4000 clock!
	31400000.0, //ET3/4000 clock!
	37500000.0, //ET3/4000 clock!
	0.0, //ET3000 clock!
	0.0, //ET3000 clock!
	0.0, //ET3000 clock!
	0.0, //ET3000 clock!
	0.0, //ET3000 clock!
	0.0, //ET3000 clock!
	0.0, //ET3000 clock!
	0.0 //ET3000 clock!
};

OPTINLINE uint_32 getcol256_Tseng(VGA_Type* VGA, byte color) //Convert color to RGB!
{
	byte DACbits;
	DACEntry colorEntry; //For getcol256!
	DACbits = (0x3F | VGA->precalcs.emulatedDACextrabits); //How many DAC bits to use?
	readDAC(VGA, (color & VGA->registers->DACMaskRegister), &colorEntry); //Read the DAC entry, masked on/off by the DAC Mask Register!
	return RGB(convertrel((colorEntry.r & DACbits), DACbits, 0xFF), convertrel((colorEntry.g & DACbits), DACbits, 0xFF), convertrel((colorEntry.b & DACbits), DACbits, 0xFF)); //Convert using DAC (Scale of DAC is RGB64, we use RGB256)!
}

//Easy retrieval and storage of bits from an aperture containing a single number in little endian format!
uint_32 getTsengLE32(byte* list)
{
	return (((((list[0x03] << 8) | list[0x02]) << 8) | list[0x01]) << 8) | list[0x00];
}

uint_32 getTsengLE24(byte* list)
{
	return ((((list[0x02]) << 8) | list[0x01]) << 8) | list[0x00];
}

uint_32 getTsengLE16(byte* list)
{
	return (list[0x01] << 8) | list[0x00];
}

void setTsengLE32(byte* list, uint_32 val)
{
	list[0] = (val & 0xFF);
	val >>= 8;
	list[1] = (val & 0xFF);
	val >>= 8;
	list[2] = (val & 0xFF);
	val >>= 8;
	list[3] = (val & 0xFF);
}

void setTsengLE24(byte* list, uint_32 val)
{
	list[0] = (val & 0xFF);
	val >>= 8;
	list[1] = (val & 0xFF);
	val >>= 8;
	list[2] = (val & 0xFF);
}

void setTsengLE16(byte* list, word val)
{
	list[0] = (val & 0xFF);
	val >>= 8;
	list[1] = (val & 0xFF);
}

extern uint_32 VGA_MemoryMapBankRead, VGA_MemoryMapBankWrite; //The memory map bank to use!

void updateET34Ksegmentselectregister(byte val)
{
	SVGA_ET34K_DATA* et34kdata = et34k_data; //The et4k data!
	if (getActiveVGA()->enable_SVGA == 2) //ET3000?
	{
		et34kdata->bank_write = val & 7;
		et34kdata->bank_read = (val >> 3) & 7;
		et34kdata->bank_size = (val >> 6) & 3; //Bank size to use!
	}
	else //ET4000?
	{
		et34kdata->bank_write = val & 0xF;
		et34kdata->bank_read = (val >> 4) & 0xF;
		et34kdata->bank_size = 1; //Bank size to use is always the same(64K)!
		if (et34kdata->tsengExtensions) //W32 variant? Extensions apply!
		{
			et34kdata->bank_write |= ((et34kdata->extendedbankregister&0x03)<<4);
			et34kdata->bank_read |= (et34kdata->extendedbankregister&0x30);
		}
	}
}

void et34k_updateDAC(SVGA_ET34K_DATA* et34kdata, byte val)
{
	if (et34kdata->emulatedDAC==1) //UMC UM70C178?
	{
		val &= 0xE0; //Mask limited!
	}
	et34kdata->hicolorDACcommand = val; //Apply the command!
	//bits 3-4 redirect to the DAC mask register.
	//bit 0 is set if bits 5-7 is 1 or 3, cleared otherwise(R/O)
	//bits 1-2 are stored, but unused.
	//All generic handling of the ET3K/ET4K Hi-color DAC!
	if (et34kdata->emulatedDAC == 0) //SC11487?
	{
		//It appears that bit 0 is a flag that sets when 16-bit mode isn't selected and 2 pixel clocks per pel are selected, which is an invalid setting.
		et34kdata->hicolorDACcommand = (et34kdata->hicolorDACcommand&~1)|((((val>>2)^val)&(val&0x20))>>5); //Top 3 bits has bit 7 not set while bit 5 set, moved to bit 0?
		//All other bits are fully writable!
		//... They are read-only(proven by the WhatVGA not supposed to be able to bleed bit 4 to the DAC mask register at least!
	}
	else if (et34kdata->emulatedDAC == 2) //AT&T 20C490?
	{
		if ((val&0xE0)==0x60) //Detection logic?
		{
			et34kdata->hicolorDACcommand &= ~0xE0; //Clears the mode bits to VGA mode!
		}
		//All other settings are valid!
	}
	//SC15025 has all bits writable/readable!
	//et34kdata->hicolorDACcommand |= 6; //Always set bits 1-2?
}

byte Tseng34K_writeIO(word port, byte val)
{
	uint_32 memsize;
	byte result=0;
	SVGA_ET34K_DATA *et34kdata = et34k_data; //The et4k data!
// Tseng ET4K implementation
	if (((getActiveVGA()->registers->VGA_enabled) == 0) && (port!=0x46E8) && (port!=0x3C3)) return 0; //Disabled I/O?
	switch (port) //What port?
	{
	case 0x46E8: //Video subsystem enable register?
		if (((et4k_reg(et34kdata, 3d4, 34) & 8) == 0) && (getActiveVGA()->enable_SVGA == 1)) return 0; //Undefined on ET4000!
		SETBITS(getActiveVGA()->registers->VGA_enabled, 0, 1,(val & 8) ? 1 : 0); //RAM enabled?
		MMU_mappingupdated(); //A memory mapping has been updated?
		VGA_calcprecalcs(getActiveVGA(), WHEREUPDATED_MISCOUTPUTREGISTER); //Updated index!
		return 1; //OK
		break;
	case 0x3C3: //Video subsystem enable register in VGA mode?
		if ((et4k_reg(et34kdata, 3d4, 34) & 8) && (getActiveVGA()->enable_SVGA == 1)) return 2; //Undefined on ET4000!
		SETBITS(getActiveVGA()->registers->VGA_enabled,1,1,(val & 1)); //RAM enabled?
		MMU_mappingupdated(); //A memory mapping has been updated?
		VGA_calcprecalcs(getActiveVGA(), WHEREUPDATED_MISCOUTPUTREGISTER); //Updated index!
		return 1; //OK
		break;
	case 0x3BF: //Hercules Compatibility Mode?
		if (getActiveVGA()->enable_SVGA==1) //Extensions check?
		{
			if (val == 3) //First part of the sequence to activate the extensions?
			{
				et34kdata->extensionstep = 1; //Enable the first step to activation!
			}
			else if (val == 1) //Step one of the disable?
			{
				et34kdata->extensionstep = 2; //Enable the first step to deactivation!
			}
			else
			{
				et34kdata->extensionstep = 0; //Restart the check!
			}
		}
		et34kdata->herculescompatibilitymode = val; //Save the value!
		et34kdata->herculescompatibilitymode_secondpage = ((val & 2) >> 1); //Save the bit!
		return 1; //OK!
		break;
	case 0x3D8: //CGA mode control?
		if (!GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER,0,1)) goto checkEnableDisable; //Block: we're a mono mode addressing as color!
		result = 0; //Default result!
		et34kdata->CGAModeRegister = val; //Save the register to be read!
		if (((et4k_reg(et34kdata, 3d4, 34) & 0xA0) == 0x80) && (((getActiveVGA()->enable_SVGA < 3) && (getActiveVGA()->enable_SVGA > 0)) || (getActiveVGA()->enable_SVGA == 2))) //Enable emulation and translation disabled?
		{
			if (et34kdata->ExtendedFeatureControlRegister & 0x80) //Enable NMI?
			{
				result = !execNMI(0); //Execute an NMI from Bus!
				goto checkEnableDisable;
			}
		}
		result = 1; //Handled!
		goto checkEnableDisable;
	case 0x3B8: //MDA mode control?
		if (GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER,0,1)) goto checkEnableDisable; //Block: we're a color mode addressing as mono!
		result = 0; //Default result!
		et34kdata->MDAModeRegister = val; //Save the register to be read!
		if (((et4k_reg(et34kdata, 3d4, 34) & 0xA0) == 0x80) && (((getActiveVGA()->enable_SVGA < 3) && (getActiveVGA()->enable_SVGA > 0)) || (getActiveVGA()->enable_SVGA==2))) //Enable emulation and translation disabled?
		{
			if (et34kdata->ExtendedFeatureControlRegister & 0x80) //Enable NMI?
			{
				result = !execNMI(0); //Execute an NMI from Bus!
				goto checkEnableDisable; //Check for enable/disable!
			}
		}
		result = 1; //Handled!
		checkEnableDisable: //Check enable/disable(port 3D8 too)
		if (getActiveVGA()->enable_SVGA==1) //Extensions used?
		{
			if ((et34kdata->extensionstep==2) && (val == 0x29)) //Step two of disable extensions?
			{
				et34kdata->extensionstep = 0; //Disable steps!
				et34kdata->extensionsEnabled = 0; //Extensions are now disabled!
				VGA_calcprecalcs(getActiveVGA(), WHEREUPDATED_ALL); //Update all precalcs!
				result = 1; //Handled!
			}
			else if ((et34kdata->tsengExtensions) && et34kdata->extensionsEnabled && ((val&0xA0)!=0xA0)) //Clearing extensions on W32 and up?
			{
				et34kdata->extensionstep = 0; //Stop checking!
				et34kdata->extensionsEnabled = 0; //Extensions are now disabled!
				VGA_calcprecalcs(getActiveVGA(), WHEREUPDATED_ALL); //Update all precalcs!
				result = 1; //Handled!
			}
			else if ((et34kdata->extensionstep==1) && ((val==0xA0) || ((et34kdata->tsengExtensions) && ((val&0xA0)==0xA0)))) //Step two of enable extensions? W32 doesn't require 0xA0, but just requires bits 7&5 set.
			{
				et34kdata->extensionstep = 0; //Disable steps!
				et34kdata->extensionsEnabled = 1; //Enable the extensions!
				et34kdata->et4k_segmentselectregisterenabled = 1; //Enable the segment select register from now on!
				updateET34Ksegmentselectregister(et34kdata->segmentselectregister); //Make the segment select register active!
				VGA_calcprecalcs(getActiveVGA(), WHEREUPDATED_ALL); //Update all precalcs!
				result = 1; //Handled!
			}
			else //Not an extensions trigger?
			{
				et34kdata->extensionstep = 0; //Stop checking!
			}
		}
		return result; //Not handled!
	case 0x3D9: //CGA color control?
		if (((et4k_reg(et34kdata,3d4,34) & 0xA0) == 0x80) || (getActiveVGA()->enable_SVGA==2)) //Enable emulation and translation disabled?
		{
			et34kdata->CGAColorSelectRegister = val; //Save the register to be read!
			//Doesn't have an NMI?
			return 1; //Handled!
		}
		return 0; //Not handled!
		break;

	//16-bit DAC support(Sierra SC11487)!
	case 0x3C6: //DAC Mask Register? Pixel Mask/Command Register in the manual.
		if (et34kdata->hicolorDACcmdmode<=3)
		{
			et34kdata->hicolorDACcmdmode = 0; //Stop looking?
			return 0; //Execute normally!
		}
		//16-bit DAC operations!
		et34k_updateDAC(et34kdata,val); //Update the DAC values to be compatible!
		if (et34kdata->emulatedDAC == 2) //AT&T 20C490 or Sierra SC15025? This reset of the IPF flag on the SC15025 happens on any write to any address or a read not from the DAC mask address.
		{
			//WhatVGA says this about the UMC70C178 as well, but the identification routine of the AT&T 20C490 would identify it as a AT&T 20C491/20C492 instead, so it should actually be like a Sierra SC11487 instead.
			et34kdata->hicolorDACcmdmode = 0; //Disable command mode!
		}
		VGA_calcprecalcs(getActiveVGA(),WHEREUPDATED_DACMASKREGISTER); //We've been updated!
		return 1; //We're overridden!
		break;
	case 0x3C7: //Write: DAC Address Read Mode Register	ADDRESS? Pallette RAM read address register in the manual.
		if (et34kdata->SC15025_enableExtendedRegisters) //Extended registers?
		{
			//Extended index register!
			et34kdata->SC15025_extendedaddress = val; //The selected address!
			return 1; //We're overridden!
		}
		et34kdata->hicolorDACcmdmode = 0; //Disable command mode!
		return 0; //Normal execution!
		break;
	case 0x3C8: //DAC Address Write Mode Register		ADDRESS? Pallette RAM write address register in the manual.
		if (et34kdata->SC15025_enableExtendedRegisters) //Extended registers?
		{
			switch (et34kdata->SC15025_extendedaddress) //Extended data register?
			{
			case 0x08: //Auxiliary Control Register?
				et34kdata->SC15025_auxiliarycontrolregister = val; //Auxiliary control register. Bit 0=8-bit DAC when set. 6-bit otherwise.
				VGA_calcprecalcs(getActiveVGA(), WHEREUPDATED_DACMASKREGISTER); //We've been updated!
				break;
			case 0x09: //ID #1!
			case 0x0A: //ID #2!
			case 0x0B: //ID #3!
			case 0x0C: //Version!
				//ID registers are ROM!
				break;
			case 0x0D: //Secondary pixel mask, low byte!
			case 0x0E: //Secondary pixel mask, mid byte!
			case 0x0F: //Secondary pixel mask, high byte!
				et34kdata->SC15025_secondarypixelmaskregisters[et34kdata->SC15025_extendedaddress-0x0D] = val; //Secondary pixel mask registers!
				VGA_calcprecalcs(getActiveVGA(), WHEREUPDATED_DACMASKREGISTER); //We've been updated!
				break;
			case 0x10: //Pixel repack register!
				et34kdata->SC15025_pixelrepackregister = val; //bit 0=Enable 4-byte fetching in modes 2 and 3!
				VGA_calcprecalcs(getActiveVGA(), WHEREUPDATED_DACMASKREGISTER); //We've been updated!
				break;
			default:
				//Undefined!
				break;
			}
			return 1; //We're overridden!
		}
		et34kdata->hicolorDACcmdmode = 0; //Disable command mode!
		return 0; //Normal execution!
		break;
	case 0x3C9: //DAC Data Register				DATA? Pallette RAM in the manual.
		et34kdata->hicolorDACcmdmode = 0; //Disable command mode!
		return 0; //Normal execution!
		break;
	//RS2 is always zero on x86.

	//Normal video card support!
	case 0x3B5: //CRTC Controller Data Register		DATA
		if (GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER,0,1)) goto finishoutput; //Block: we're a color mode addressing as mono!
		goto accesscrtvalue;
	case 0x3D5: //CRTC Controller Data Register		DATA
		if (!GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER,0,1)) goto finishoutput; //Block: we're a mono mode addressing as color!
	accesscrtvalue:
		if (
			((!et34kdata->extensionsEnabled) && (getActiveVGA()->enable_SVGA == 1)) && //ET4000 blocks this without the KEY?
			(!((getActiveVGA()->registers->CRTControllerRegisters_Index == 0x33) || (getActiveVGA()->registers->CRTControllerRegisters_Index == 0x35))) //Unprotected registers for reads that can be read without the key?
			&& (getActiveVGA()->registers->CRTControllerRegisters_Index > 0x18) //For the ET4000 range of registers?
			)
			return 2; //Float the bus!

		switch(getActiveVGA()->registers->CRTControllerRegisters_Index)
		{
		/*
		3d4 index 30h (R/W): W32 only: Linear Frame Buffer address in units of 4MB!
		*/
		STORE_ET4K_W32(3d4, 30, WHEREUPDATED_CRTCONTROLLER);
		/*
		3d4h index 31h (R/W):  General Purpose
		bit  0-3  Scratch pad
			 6-7  Clock Select bits 3-4. Bits 0-1 are in 3C2h/3CCh bits 2-3.
		*/
		STORE_ET4K(3d4, 31,WHEREUPDATED_CRTCONTROLLER);

		// 3d4h index 32h - RAS/CAS Configuration (R/W)
		// No effect on emulation. Should not be written by software.
		STORE_ET4K(3d4, 32,WHEREUPDATED_CRTCONTROLLER);

		case 0x33:
			if (getActiveVGA()->enable_SVGA != 1) return 0; //Not implemented on others than ET4000!
			// 3d4 index 33h (R/W): Extended start Address
			// 0-1 Display Start Address bits 16-17
			// 2-3 Cursor start address bits 16-17
			/*
			ET4000/W32:
			0-3 Display Start Address bits 16-19
			4-7 Cursor start address bits 16-19
			*/
			// Used by standard Tseng ID scheme
			if (et34kdata->tsengExtensions) //W32 chip?
			{
				et34kdata->store_et4k_3d4_33 = val; //All bits are stored!
				et34kdata->display_start_high = ((val & 0x0F) << 16);
				et34kdata->cursor_start_high = ((val & 0xF0) << 12);
			}
			else //ET4000 chip?
			{
				et34kdata->store_et4k_3d4_33 = (val & 0xF); //According to Windows NT 4, this only stores the low 4 bits!
				et34kdata->display_start_high = ((val & 0x03) << 16);
				et34kdata->cursor_start_high = ((val & 0x0c) << 14);
			}
			VGA_calcprecalcs(getActiveVGA(),WHEREUPDATED_CRTCONTROLLER|0x33); //Update all precalcs!
			break;

		/*
		3d4h index 34h (R/W): 6845 Compatibility Control Register
		bit    0  Enable CS0 (alternate clock timing)
			   1  Clock Select bit 2.  Bits 0-1 in 3C2h bits 2-3, bits 3-4 are in 3d4h
				  index 31h bits 6-7
			   2  Tristate ET4000 bus and color outputs if set
			   3  Video Subsystem Enable Register at 46E8h if set, at 3C3h if clear.
			   4  Enable Translation ROM for reading CRTC and MISCOUT if set
			   5  Enable Translation ROM for writing CRTC and MISCOUT if set
			   6  Enable double scan in AT&T compatibility mode if set
			   7  Enable 6845 compatibility if set
		*/
		// TODO: Bit 6 may have effect on emulation
		STORE_ET4K(3d4, 34,WHEREUPDATED_CRTCONTROLLER);

		case 0x35: 
		/*
		3d4h index 35h (R/W): Overflow High
		bit    0  Vertical Blank Start Bit 10 (3d4h index 15h).
			   1  Vertical Total Bit 10 (3d4h index 6).
			   2  Vertical Display End Bit 10 (3d4h index 12h).
			   3  Vertical Sync Start Bit 10 (3d4h index 10h).
			   4  Line Compare Bit 10 (3d4h index 18h).
			   5  Gen-Lock Enabled if set (External sync)
			   6  (4000) Read/Modify/Write Enabled if set. Currently not implemented.
			   7  Vertical interlace if set. The Vertical timing registers are
				programmed as if the mode was non-interlaced!!
			   6  (W32) Source of the Vertical Retrace interrupts. 0=VGA-compatible, 1=CRTCB/Sprite registers
		*/
			if (getActiveVGA()->enable_SVGA != 1) return 0; //Not implemented on others than ET4000!
			if (GETBITS(getActiveVGA()->registers->CRTControllerRegisters.REGISTERS.VERTICALRETRACEENDREGISTER,7,1)) //Are we protected?
			{
				val = (val&0x90)|(et34k_data->store_et4k_3d4_35&~0x90); //Ignore all bits except bits 4&7(Line compare&vertical interlace)?
			}
			et34kdata->store_et4k_3d4_35 = val;
			et34kdata->line_compare_high = ((val&0x10)<<6);
			VGA_calcprecalcs(getActiveVGA(),WHEREUPDATED_CRTCONTROLLER|0x35); //Update all precalcs!
			break;

		// 3d4h index 36h - Video System Configuration 1 (R/W)
		// VGADOC provides a lot of info on this register, Ferraro has significantly less detail.
		// This is unlikely to be used by any games. Bit 4 switches chipset into linear mode -
		// that may be useful in some cases if there is any software actually using it.
		// TODO (not near future): support linear addressing
		STORE_ET4K(3d4, 36,WHEREUPDATED_CRTCONTROLLER);

		// 3d4h index 37 - Video System Configuration 2 (R/W)
		// Bits 0,1, and 3 provides information about memory size:
		// 0-1 Bus width ET4000AX: (1: 8 bit, 2: 16 bit, 3: 32 bit)
		// 0 Bus width ET4000/W32: (0: 16 bit, 1: 32 bit)
		// 3   Size of RAM chips ET4000AX: (0: 64Kx, 1: 256Kx) ET4000/W32*: (0: 1MB, 1: 256k)
		// Other bits have no effect on emulation.
		case 0x37:
			if (getActiveVGA()->enable_SVGA != 1) return 0; //Not implemented on others than ET4000!
			if ((getActiveVGA()->enable_SVGA == 1) && et34k(getActiveVGA())->tsengExtensions) //ET4000/W32 variant?
			{
				memsize = ((256 * 1024) << (((val^8) & 8) >> 2)); //Init size to detect! 256k or 1M times(bit 3) 16 or 32 bit bus width(bit 0)!
				memsize <<= 1+(val & 1); //setting bit 1 doubles it and setting bits 1 and 0 together doubles it again(value 2=x2, value 3=x3).
				if ((val & 0x42)&0x42) //Writing these bits set? Internal test mode activated! Only bit 1 seems to be set during boot which is required to trigger this!
				{
					val = (val & ~0x9) | (et34k(getActiveVGA())->et4k_reg37_init & 0x9); //Replace the VRAM detect bits with the detected VRAM chips!
				}
			}
			else //ET4000AX?
			{
				memsize = ((64 * 1024) << ((val & 8) >> 2)); //The memory size for this item!
				memsize <<= ((val & 2) >> 1) + (((val & 2) >> 1) & (val & 1)); //setting bit 1 doubles it and setting bits 1 and 0 together doubles it again(value 2=x2, value 3=x3).
				if ((val & 0x40) & 0x40) //Writing these bits set? Internal test mode activated!
				{
					val = (val & ~0xB) | (et34k(getActiveVGA())->et4k_reg37_init & 0xB); //Replace the VRAM detect bits with the detected VRAM chips!
				}
			}
			//Now, apply the bus width
			--memsize; //Get wrapping mask!
			
			et34kdata->store_et4k_3d4_37 = val;
			//et34k(getActiveVGA())->memwrap = memsize; //What to wrap against!
			VGA_calcprecalcs(getActiveVGA(),WHEREUPDATED_CRTCONTROLLER|0x37); //Update all precalcs!
			return 1;
			break;

		case 0x3f:
		/*
		3d4h index 3Fh (R/W):
		bit    0  Bit 8 of the Horizontal Total (3d4h index 0)
			   2  Bit 8 of the Horizontal Blank Start (3d4h index 3)
			   4  Bit 8 of the Horizontal Retrace Start (3d4h index 4)
			   7  Bit 8 of the CRTC offset register (3d4h index 13h).
		*/
		// The only unimplemented one is bit 7
			if (getActiveVGA()->enable_SVGA != 1) return 0; //Not implemented on others than ET4000!
			et34kdata->store_et4k_3d4_3f = val;
			if ((val & 2) && (et34kdata->tsengExtensions)) et34kdata->store_et4k_3d4_3f &= ~2; //Bit 1 is always cleared on the W32!
		// Abusing s3 ex_hor_overflow field which very similar. This is
		// to be cleaned up later
			VGA_calcprecalcs(getActiveVGA(),WHEREUPDATED_CRTCONTROLLER|0x3F); //Update all precalcs!
			return 1;
			break;

		//ET3K registers
		STORE_ET3K(3d4, 1b,WHEREUPDATED_CRTCONTROLLER);
		STORE_ET3K(3d4, 1c, WHEREUPDATED_CRTCONTROLLER);
		STORE_ET3K(3d4, 1d, WHEREUPDATED_CRTCONTROLLER);
		STORE_ET3K(3d4, 1e, WHEREUPDATED_CRTCONTROLLER);
		STORE_ET3K(3d4, 1f, WHEREUPDATED_CRTCONTROLLER);
		STORE_ET3K(3d4, 20, WHEREUPDATED_CRTCONTROLLER);
		STORE_ET3K(3d4, 21, WHEREUPDATED_CRTCONTROLLER);
		case 0x23:
			/*
			3d4h index 23h (R/W): Extended start ET3000
			bit   0  Cursor start address bit 16
			1  Display start address bit 16
			2  Zoom start address bit 16
			7  If set memory address 8 is output on the MBSL pin (allowing access to
			1MB), if clear the blanking signal is output.
			*/
			// Only bits 1 and 2 are supported. Bit 2 is related to hardware zoom, bit 7 is too obscure to be useful
			if (getActiveVGA()->enable_SVGA != 2) return 0; //Not implemented on others than ET3000!
			et34k_data->store_et3k_3d4_23 = val;
			et34k_data->display_start_high = ((val & 0x02) << 15);
			et34k_data->cursor_start_high = ((val & 0x01) << 16);
			VGA_calcprecalcs(getActiveVGA(), WHEREUPDATED_CRTCONTROLLER | 0x23); //Update all precalcs!
			break;

			/*
			3d4h index 24h (R/W): Compatibility Control
			bit   0  Enable Clock Translate if set
			1  Clock Select bit 2. Bits 0-1 are in 3C2h/3CCh.
			2  Enable tri-state for all output pins if set
			3  Enable input A8 of 1MB DRAMs from the INTL output if set
			4  Reserved
			5  Enable external ROM CRTC translation if set
			6  Enable Double Scan and Underline Attribute if set
			7  Enable 6845 compatibility if set.
			*/
			// TODO: Some of these may be worth implementing.
		STORE_ET3K(3d4, 24,WHEREUPDATED_CRTCONTROLLER);
		case 0x25:
			/*
			3d4h index 25h (R/W): Overflow High
			bit   0  Vertical Blank Start bit 10
			1  Vertical Total Start bit 10
			2  Vertical Display End bit 10
			3  Vertical Sync Start bit 10
			4  Line Compare bit 10
			5-6  Reserved
			7  Vertical Interlace if set
			*/
			if (getActiveVGA()->enable_SVGA != 2) return 0; //Not implemented on others than ET3000!
			if (GETBITS(getActiveVGA()->registers->CRTControllerRegisters.REGISTERS.VERTICALRETRACEENDREGISTER,7,1)) //Are we protected?
			{
				val = (val&0x90)|(et34k_data->store_et3k_3d4_25&~0x90); //Ignore all bits except bits 4&7(Line compare&vertical interlace)?
			}
			et34k_data->store_et3k_3d4_25 = val;
			VGA_calcprecalcs(getActiveVGA(), WHEREUPDATED_CRTCONTROLLER | 0x25); //Update all precalcs!
			break;
		default:
			return 0; //Unhandled!
			break;
		}
		break;
	case 0x3C5: //Sequencer data register?
		switch(getActiveVGA()->registers->SequencerRegisters_Index) {
		//ET4K
		/*
		3C4h index  6  (R/W): TS State Control
		bit 1-2  Font Width Select in dots/character
				If 3C4h index 4 bit 0 clear:
					0: 9 dots, 1: 10 dots, 2: 12 dots, 3: 6 dots
				If 3C4h index 5 bit 0 set:
					0: 8 dots, 1: 11 dots, 2: 7 dots, 3: 16 dots
				Only valid if 3d4h index 34h bit 3 set.
		*/
		// TODO: Figure out if this has any practical use
		STORE_ET34K(3c4, 06,WHEREUPDATED_SEQUENCER);
		// 3C4h index  7  (R/W): TS Auxiliary Mode
		// Unlikely to be used by games (things like ROM enable/disable and emulation of VGA vs EGA)
		STORE_ET34K(3c4, 07,WHEREUPDATED_SEQUENCER);
		case 0: //TS register special stuff?
			break; //Don't handle the Segment Select disabling!
			if ((val & 2) == 0) //We're stopping to repond to the Segment Select Register when a synchronous reset is started or set!
			{
				et34kdata->et4k_segmentselectregisterenabled = 0; //We're stopping to respond to the Segment Select Register until the KEY is set again!
				updateET34Ksegmentselectregister(0); //Make the segment select register inactive!
				VGA_calcprecalcs(getActiveVGA(), WHEREUPDATED_INDEX|INDEX_BANKREGISTERS); //Update from the CRTC controller registers!
			}
		default:
			break;
		}
		break;
	case 0x217A: //W32 index
		if ((getActiveVGA()->enable_SVGA != 1) || (et34kdata->tsengExtensions == 0)) return 0; //Not available on the ET4000 until having set the KEY at least once after a power-on reset or synchronous reset(TS indexed register 0h bit 1). Also disabled by the non-W32 variants!
		et34kdata->W32_21xA_index = val; //Set the index register!
		VGA_calcprecalcs(getActiveVGA(), WHEREUPDATED_INDEX|INDEX_CRTCSPRITE); //Update from the CRTC index registers!
		return 1; //Handled!
		break;
	case 0x217B:
		if ((getActiveVGA()->enable_SVGA != 1) || (et34kdata->tsengExtensions == 0)) return 0; //Not available on the ET4000 until having set the KEY at least once after a power-on reset or synchronous reset(TS indexed register 0h bit 1). Also disabled by the non-W32 variants!
		if (et34kdata->W32_21xA_index == 0xEF) //CRTC/sprite control?
		{
			et34kdata->W32_21xA_CRTCBSpriteControl = val; //Set the register!
		}
		else if (et34kdata->W32_21xA_index == 0xF7) //Image Port control?
		{
			et34kdata->W32_21xA_ImagePortControl = val; //Set the register!
		}
		else //Shared addresses?
		{
			if ((et34kdata->W32_21xA_CRTCBSpriteControl & 1) == 0) //CRTC?
			{
				switch (et34kdata->W32_21xA_index) //What index in the CRTC?
				{
				case 0xE0: //CRTCB Horizontal Pixel Position (word)
				case 0xE1:
				case 0xE2: //CRTCB Width (word)
				case 0xE3:
				case 0xE4: //CRTCB Vertical Pixel Position (word)
				case 0xE5:
				case 0xE6: //CRTCB Height (word)
				case 0xE7:
				case 0xE8: //CRTCB Starting Address (24-bit)
				case 0xE9:
				case 0xEA:
				case 0xEB: //CRTCB Row Offset (word)
				case 0xEC:
					et34kdata->W32_21xA_shadowRegisters[(et34kdata->W32_21xA_index - 0xE0)&0x1F] = val; //Set the value in the CRTCB registers!
					if (et34kdata->W32_21xA_index == 0xEC)
					{
						SETBITS(et34kdata->W32_21xA_shadowRegisters[(et34kdata->W32_21xA_index - 0xE0) & 0x1F], 4, 0xF, et34kdata->W32_version); //Set the high 4 bits to indicate ET4000/W32!
					}
					break;
				case 0xED: //CRTCB Pixel Panning
				case 0xEE: //CRTCB Color Depth
				case 0xF0: //Image Starting Address (24-bit)
				case 0xF1:
				case 0xF2:
				case 0xF3: //Image Transfer Length (word)
				case 0xF4:
				case 0xF5: //Image Row Offset
				case 0xF6:
					//All shared among both CRTCB and Sprite registers!
					et34kdata->W32_21xA_shadowRegisters[(et34kdata->W32_21xA_index - 0xE0)&0x1F] = val; //Set the value in the CRTCB registers!
					break;
				}
			}
			else //Sprite?
			{
				switch (et34kdata->W32_21xA_index) //What index in the Sprite?
				{
				case 0xE0: //Sprite Horizontal Pixel Position (word)
				case 0xE1:
				case 0xE2: //Sprite Horizontal Preset (word)
				case 0xE3:
				case 0xE4: //Sprite Horizontal Pixel Position (word)
				case 0xE5:
				case 0xE6: //Sprite Vertical Preset (word)
				case 0xE7:
				case 0xE8: //Sprite Starting Address (24-bit)
				case 0xE9:
				case 0xEA:
				case 0xEB: //Sprite Row OFfset (word)
				case 0xEC:
					et34kdata->W32_21xA_shadowRegisters[(et34kdata->W32_21xA_index - 0xE0)&0x1F] = val; //Set the value in the CRTCB registers!
					if (et34kdata->W32_21xA_index == 0xEC)
					{
						SETBITS(et34kdata->W32_21xA_shadowRegisters[(et34kdata->W32_21xA_index - 0xE0) & 0x1F], 4, 0xF, et34kdata->W32_version); //Set the high 4 bits to indicate ET4000/W32!
					}
					break;
				case 0xED: //CRTCB Pixel Panning
				case 0xEE: //CRTCB Color Depth
				case 0xF0: //Image Starting Address (24-bit)
				case 0xF1:
				case 0xF2:
				case 0xF3: //Image Transfer Length (word)
				case 0xF4:
				case 0xF5: //Image Row Offset
				case 0xF6:
					//All shared among both CRTCB and Sprite registers!
					et34kdata->W32_21xA_shadowRegisters[(et34kdata->W32_21xA_index - 0xE0)&0x1F] = val; //Set the value in the CRTCB registers!
					break;
				}
			}
		}
		VGA_calcprecalcs(getActiveVGA(), WHEREUPDATED_CRTCSPRITE|((et34kdata->W32_21xA_index)&0xFF)); //Update from the CRTC index registers!
		return 1; //Handled!
		break;
		/*
	3CDh (R/W): Segment Select
	bit 0-3  64k Write bank number (0..15)
	4-7  64k Read bank number (0..15)
	*/
	case 0x3CD: //Segment select?
		if ((getActiveVGA()->enable_SVGA == 1) && (!et34kdata->et4k_segmentselectregisterenabled)) return 0; //Not available on the ET4000 until having set the KEY at least once after a power-on reset or synchronous reset(TS indexed register 0h bit 1).
		et34kdata->segmentselectregister = val; //Save the entire segment select register!

		//Apply correct memory banks!
		updateET34Ksegmentselectregister(et34kdata->segmentselectregister); //Make the segment select register active!
		VGA_calcprecalcs(getActiveVGA(),WHEREUPDATED_INDEX | INDEX_BANKREGISTERS); //Update from the bank registers!
		return 1;
		break;
	case 0x3CB: //Extended bank register (W32)?
		if ((((getActiveVGA()->enable_SVGA == 1) && (!et34kdata->et4k_segmentselectregisterenabled)))||(et34kdata->tsengExtensions==0)) return 0; //Not available on the ET4000 until having set the KEY at least once after a power-on reset or synchronous reset(TS indexed register 0h bit 1). Also disabled by the non-W32 variants!
		et34kdata->extendedbankregister = val; //Save the entire extended bank register!

		//Apply correct memory banks!
		updateET34Ksegmentselectregister(et34kdata->segmentselectregister); //Make the segment select register active!
		VGA_calcprecalcs(getActiveVGA(), WHEREUPDATED_INDEX | INDEX_BANKREGISTERS); //Update from the bank registers!
		return 1;
		break;
	case 0x3C0: //Attribute controller?
		if (!VGA_3C0_FLIPFLOPR) return 0; //Index gets ignored!
		if (et34kdata->protect3C0_PaletteRAM && (VGA_3C0_INDEXR<0x10)) //Palette RAM? Handle protection!
		{
			VGA_3C0_FLIPFLOPW(!VGA_3C0_FLIPFLOPR); //Flipflop!
			return 1; //Ignore the write: we're protected!
		}
		switch (VGA_3C0_INDEXR) {
			// 3c0 index 16h: ATC Miscellaneous
			// VGADOC provides a lot of information, Ferarro documents only two bits
			// and even those incompletely. The register is used as part of identification
			// scheme.
			// Unlikely to be used by any games but double timing may be useful.
			// TODO: Figure out if this has any practical use
			STORE_ET34K_3C0(3c0, 16,WHEREUPDATED_ATTRIBUTECONTROLLER);
			/*
			3C0h index 17h (R/W):  Miscellaneous 1
			bit   7  If set protects the internal palette ram and redefines the attribute
			bits as follows:
			Monochrome:
			bit 0-2  Select font 0-7
			3  If set selects blinking
			4  If set selects underline
			5  If set prevents the character from being displayed
			6  If set displays the character at half intensity
			7  If set selects reverse video
			Color:
			bit 0-1  Selects font 0-3
			2  Foreground Blue
			3  Foreground Green
			4  Foreground Red
			5  Background Blue
			6  Background Green
			7  Background Red
			*/
			// TODO: Figure out if this has any practical use
			STORE_ET34K_3C0(3c0, 17,WHEREUPDATED_ATTRIBUTECONTROLLER);
		case 0x11: //Overscan? Handle protection!
			if (et34kdata->protect3C0_Overscan) //Palette RAM? Handle protection!
			{
				//Overscan low 4 bits are protected, handle this way!
				val = (val&0xF0)|(getActiveVGA()->registers->AttributeControllerRegisters.DATA[0x11]&0xF); //Leave the low 4 bits unmodified!
				getActiveVGA()->registers->AttributeControllerRegisters.DATA[0x11] = val; //Set the bits allowed to be set!
				VGA_calcprecalcs(getActiveVGA(),WHEREUPDATED_ATTRIBUTECONTROLLER|0x11); //We have been updated!
				VGA_calcprecalcs(getActiveVGA(),WHEREUPDATED_CRTCONTROLLER|VGA_CRTC_ATTRIBUTECONTROLLERTOGGLEREGISTER); //Our actual location!
				VGA_3C0_FLIPFLOPW(!VGA_3C0_FLIPFLOPR); //Flipflop!
				return 1; //We're overridden!
			}
			return 0; //Handle normally!
			break;
		default:
			break;
		}
		break;
	case 0x3BA: //Write: Feature Control Register (mono)		DATA
		if (GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER,0,1)) goto finishoutput; //Block: we're a color mode addressing as mono!
		goto accessfc;
	case 0x3CA: //Same as above!
	case 0x3DA: //Same!
		if (!GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER,0,1)) goto finishoutput; //Block: we're a mono mode addressing as color!
	accessfc: //Allow!
		getActiveVGA()->registers->ExternalRegisters.FEATURECONTROLREGISTER = val; //Set!
		if (et34kdata->extensionsEnabled || (getActiveVGA()->enable_SVGA!=1)) //Enabled extensions?
		{
			et34kdata->ExtendedFeatureControlRegister = (val&0x80); //Our extended bit is saved!
		}
		VGA_calcprecalcs(getActiveVGA(), WHEREUPDATED_FEATURECONTROLREGISTER); //We have been updated!
		return 1;
		break;
	default: //Unknown port?
		return 0;
		break;
	}
	finishoutput:
	return 0; //Unsupported port!
}

byte Tseng34K_readIO(word port, byte *result)
{
	byte switchval;
	SVGA_ET34K_DATA *et34kdata = et34k_data; //The et4k data!
	if (((getActiveVGA()->registers->VGA_enabled) == 0) && (port != 0x46E8) && (port != 0x3C3)) return 0; //Disabled I/O?
	switch (port)
	{
	case 0x46E8: //Video subsystem enable register?
		if (((et4k_reg(et34kdata,3d4,34)&8)==0) && (getActiveVGA()->enable_SVGA == 1)) return 0; //Undefined!
		*result = (GETBITS(getActiveVGA()->registers->VGA_enabled, 0, 1)<<3); //Get from the register!
		return 1; //OK!
		break;
	case 0x3C3: //Video subsystem enable register in VGA mode?
		if ((et4k_reg(et34kdata,3d4,34)&8) && (getActiveVGA()->enable_SVGA == 1)) return 2; //Undefined!
		*result = GETBITS(getActiveVGA()->registers->VGA_enabled, 1, 1); //Get from the register!
		return 1; //OK!
		break;
	case 0x3BF: //Hercules Compatibility Mode?
		*result = et34kdata->herculescompatibilitymode; //The entire saved register!
		return 1; //OK!
		break;
	case 0x3B8: //MDA mode control?
		//if (GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER,0,1)) goto finishinput; //Block: we're a color mode addressing as mono!
		*result = et34kdata->MDAModeRegister; //Save the register to be read!
		SETBITS(*result, 6, 1, GETBITS(et34kdata->herculescompatibilitymode, 1, 1));
		//Doesn't do NMIs?
		return 1; //Handled!
	case 0x3D8: //CGA mode control?
		//if (!GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER,0,1)) goto finishinput; //Block: we're a mono mode addressing as color!
		*result = et34kdata->CGAModeRegister; //Save the register to be read!
		SETBITS(*result, 6, 1, GETBITS(et34kdata->herculescompatibilitymode, 1, 1));
		//Doesn't do NMIs?
		return 1; //Handled!
	case 0x3D9: //CGA color control?
		if ((et4k_reg(et34kdata, 3d4, 34) & 0xA0) == 0x80) //Enable emulation and translation disabled?
		{
			*result = et34kdata->CGAColorSelectRegister; //Save the register to be read!
			//Doesn't do NMIs?
			return 1; //Handled!
		}
		return 0; //Not handled!
		break;

		//16-bit DAC support(Sierra SC11487)!
	case 0x3C6: //DAC Mask Register?
		if (et34kdata->hicolorDACcmdmode<=3)
		{
			++et34kdata->hicolorDACcmdmode;
			return 0; //Execute normally!
		}
		else
		{
			*result = et34kdata->hicolorDACcommand;
			if (et34kdata->emulatedDAC == 0) //SC11487?
			{
				*result = (*result&~0x18)|(getActiveVGA()->registers->DACMaskRegister&0x18); //Mask in the shared bits only!
			}
			if (et34kdata->emulatedDAC==2) //AT&T 20C490?
			{
				et34kdata->hicolorDACcmdmode = 0; //Return to normal mode!
			}
			return 1; //Handled!
		}
		break;
	case 0x3C7: //Write: DAC Address Read Mode Register	ADDRESS? Pallette RAM read address register in the manual.
		et34kdata->hicolorDACcmdmode = 0; //Disable command mode!
		return 0; //Execute normally!
		break;
	case 0x3C8: //DAC Address Write Mode Register		ADDRESS? Pallette RAM write address register in the manual.
		if (et34kdata->SC15025_enableExtendedRegisters) //Extended registers?
		{
			//Extended data register!
			switch (et34kdata->SC15025_extendedaddress) //Extended data register?
			{
			case 0x08: //Auxiliary Control Register?
				*result = et34kdata->SC15025_auxiliarycontrolregister; //Auxiliary control register. Bit 0=8-bit DAC when set. 6-bit otherwise.
				break;
			case 0x09: //ID #1!
				*result = 0x53; //ID registers are ROM!
				break;
			case 0x0A: //ID #2!
				*result = 0x3A; //ID registers are ROM!
				break;
			case 0x0B: //ID #3!
				*result = 0xB1; //ID registers are ROM!
				break;
			case 0x0C: //Version!
				*result = 0x41; //Version register is ROM!
				break;
			case 0x0D: //Secondary pixel mask, low byte!
			case 0x0E: //Secondary pixel mask, mid byte!
			case 0x0F: //Secondary pixel mask, high byte!
				*result = et34kdata->SC15025_secondarypixelmaskregisters[et34kdata->SC15025_extendedaddress - 0x0D]; //Secondary pixel mask registers!
				break;
			case 0x10: //Pixel repack register!
				*result = et34kdata->SC15025_pixelrepackregister; //bit 0=Enable 4-byte fetching in modes 2 and 3!
				break;
			default: //Unknown register!
				*result = ~0; //Undefined!
				break;
			}
			return 1; //We're overridden!
		}
		et34kdata->hicolorDACcmdmode = 0; //Disable command mode!
		return 0; //Execute normally!
		break;
	case 0x3C9: //DAC Data Register				DATA? Pallette RAM in the manual.
		if (et34kdata->SC15025_enableExtendedRegisters) //Extended registers?
		{
			//Extended index register!
			*result = et34kdata->SC15025_extendedaddress; //Extended index!
			return 1; //We're overridden!
		}
		et34kdata->hicolorDACcmdmode = 0; //Disable command mode!
		return 0; //Execute normally!
		break;
	//Normal video card support!
	case 0x3B5: //CRTC Controller Data Register		5DATA
		if (GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER,0,1)) goto finishinput; //Block: we're a color mode addressing as mono!
		goto readcrtvalue;
	case 0x3D5: //CRTC Controller Data Register		DATA
		if (!GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER,0,1)) goto finishinput; //Block: we're a mono mode addressing as color!
		readcrtvalue:
		//Don't block when the KEY isn't set?
		switch(getActiveVGA()->registers->CRTControllerRegisters_Index)
		{
		//ET4K
		RESTORE_ET4K_W32_UNPROTECTED(3d4, 30);
		RESTORE_ET4K_UNPROTECTED(3d4, 31);
		RESTORE_ET4K_UNPROTECTED(3d4, 32);
		RESTORE_ET4K_UNPROTECTED(3d4, 33);
		RESTORE_ET4K_UNPROTECTED(3d4, 34);
		RESTORE_ET4K_UNPROTECTED(3d4, 35);
		RESTORE_ET4K_UNPROTECTED(3d4, 36);
		RESTORE_ET4K_UNPROTECTED(3d4, 37);
		RESTORE_ET4K_UNPROTECTED(3d4, 3f);
		//ET3K
		RESTORE_ET3K(3d4, 1b);
		RESTORE_ET3K(3d4, 1c);
		RESTORE_ET3K(3d4, 1d);
		RESTORE_ET3K(3d4, 1e);
		RESTORE_ET3K(3d4, 1f);
		RESTORE_ET3K(3d4, 20);
		RESTORE_ET3K(3d4, 21);
		RESTORE_ET3K(3d4, 23);
		RESTORE_ET3K(3d4, 24);
		RESTORE_ET3K(3d4, 25);
		default:
			return 0;
			break;
		}
	case 0x3C5: //Sequencer data register?
		switch(getActiveVGA()->registers->SequencerRegisters_Index) {
		RESTORE_ET34K_UNPROTECTED(3c4, 06);
		RESTORE_ET34K_UNPROTECTED(3c4, 07);
		default:
			break;
		}
		break;
	case 0x217A: //W32 index
		if ((getActiveVGA()->enable_SVGA != 1) || (et34kdata->tsengExtensions == 0)) return 0; //Not available on the ET4000 until having set the KEY at least once after a power-on reset or synchronous reset(TS indexed register 0h bit 1). Also disabled by the non-W32 variants!
		*result = et34kdata->W32_21xA_index; //Give the index register!
		return 1; //Handled!
		break;
	case 0x217B:
		if ((getActiveVGA()->enable_SVGA != 1) || (et34kdata->tsengExtensions == 0)) return 0; //Not available on the ET4000 until having set the KEY at least once after a power-on reset or synchronous reset(TS indexed register 0h bit 1). Also disabled by the non-W32 variants!
		if (et34kdata->W32_21xA_index == 0xEF) //CRTC/sprite control?
		{
			*result = et34kdata->W32_21xA_CRTCBSpriteControl; //Give the register!
		}
		else if (et34kdata->W32_21xA_index == 0xF7) //Image port control?
		{
			*result = et34kdata->W32_21xA_ImagePortControl; //Give the register!
		}
		else //Shared addresses?
		{
			if ((et34kdata->W32_21xA_CRTCBSpriteControl & 1) == 0) //CRTC?
			{
				switch (et34kdata->W32_21xA_index) //What index in the CRTC?
				{
				case 0xE0: //CRTCB Horizontal Pixel Position (word)
				case 0xE1:
				case 0xE2: //CRTCB Width (word)
				case 0xE3:
				case 0xE4: //CRTCB Vertical Pixel Position (word)
				case 0xE5:
				case 0xE6: //CRTCB Height (word)
				case 0xE7:
				case 0xE8: //CRTCB Starting Address (24-bit)
				case 0xE9:
				case 0xEA:
				case 0xEB: //CRTCB Row Offset (word)
				case 0xEC:
					*result = (et34kdata->W32_21xA_shadowRegisters[(et34kdata->W32_21xA_index)&0x1F]); //Give the register!
					break;
				case 0xED: //CRTCB Pixel Panning
				case 0xEE: //CRTCB Color Depth
				case 0xF0: //Image Starting Address (24-bit)
				case 0xF1:
				case 0xF2:
				case 0xF3: //Image Transfer Length (word)
				case 0xF4:
				case 0xF5: //Image Row Offset
				case 0xF6:
					//All shared among both CRTCB and Sprite registers!
					*result = (et34kdata->W32_21xA_shadowRegisters[(et34kdata->W32_21xA_index)&0x1F]); //Give the register!
					break;
				default: //Nothing connected?
					*result = 0xFF; //Float the bus!
					break;
				}
			}
			else //Sprite?
			{
				switch (et34kdata->W32_21xA_index) //What index in the Sprite?
				{
				case 0xE0: //Sprite Horizontal Pixel Position (word)
				case 0xE1:
				case 0xE2: //Sprite Horizontal Preset (word)
				case 0xE3:
				case 0xE4: //Sprite Horizontal Pixel Position (word)
				case 0xE5:
				case 0xE6: //Sprite Vertical Preset (word)
				case 0xE7:
				case 0xE8: //Sprite Starting Address (24-bit)
				case 0xE9:
				case 0xEA:
				case 0xEB: //Sprite Row OFfset (word)
				case 0xEC:
					*result = (et34kdata->W32_21xA_shadowRegisters[(et34kdata->W32_21xA_index)&0x1F]); //Give the register!
					break;
				case 0xED: //CRTCB Pixel Panning
				case 0xEE: //CRTCB Color Depth
				case 0xF0: //Image Starting Address (24-bit)
				case 0xF1:
				case 0xF2:
				case 0xF3: //Image Transfer Length (word)
				case 0xF4:
				case 0xF5: //Image Row Offset
				case 0xF6:
					//All shared among both CRTCB and Sprite registers!
					*result = (et34kdata->W32_21xA_shadowRegisters[(et34kdata->W32_21xA_index)&0x1F]); //Give the register!
					break;
				default: //Nothing connected?
					*result = 0xFF; //Float the bus!
					break;
				}
			}
		}
		return 1; //Handled!
		break;
	case 0x3CD: //Segment select?
		if ((getActiveVGA()->enable_SVGA == 1) && (!et34kdata->et4k_segmentselectregisterenabled)) return 0; //Not available on the ET4000 until having set the KEY at least once after a power-on reset or synchronous reset(TS indexed register 0h bit 1).
		*result = et34kdata->segmentselectregister; //Give the saved segment select register!
		return 1; //Supported!
		break;
	case 0x3CB: //Extended bank register (W32)?
		if (((getActiveVGA()->enable_SVGA == 1) && (!et34kdata->et4k_segmentselectregisterenabled)) || (et34kdata->tsengExtensions == 0)) return 0; //Not available on the ET4000 until having set the KEY at least once after a power-on reset or synchronous reset(TS indexed register 0h bit 1).
		*result = et34kdata->extendedbankregister; //Give the saved segment select register!
		return 1; //Supported!
		break;
	case 0x3C1: //Attribute controller read?
		switch (VGA_3C0_INDEXR) {
			RESTORE_ET34K_UNPROTECTED(3c0, 16);
			RESTORE_ET34K_UNPROTECTED(3c0, 17);
		default:
			break;
		}
		break;
	case 0x3C2: //Read: Input Status #0 Register		DATA
		//Switch sense: 0=Switch closed(value of the switch being 1)
		switchval = ((getActiveVGA()->registers->switches) >> GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER, 2, 3)); //Switch value to set!
		switchval = ~switchval; //Reverse the switch for EGA+!
		SETBITS(getActiveVGA()->registers->ExternalRegisters.INPUTSTATUS0REGISTER, 4, 1, (switchval & 1)); //Depends on the switches. This is the reverse of the actual switches used! Originally stuck to 1s, but reported as 0110!
		*result = getActiveVGA()->registers->ExternalRegisters.INPUTSTATUS0REGISTER; //Give the register!
		if ((!et34kdata->extensionsEnabled) && (getActiveVGA()->enable_SVGA == 1)) //Disabled on ET4000?
		{
			*result &= ~0x60; //Disable reading of the extended register!
		}
		else //Feature code!
		{
			SETBITS(*result, 5, 3, (getActiveVGA()->registers->ExternalRegisters.FEATURECONTROLREGISTER & 3)); //Feature bits 0&1!
		}
		SETBITS(*result, 7, 1, GETBITS(getActiveVGA()->registers->CRTControllerRegisters.REGISTERS.VERTICALRETRACEENDREGISTER, 4, 1)); //Vertical retrace interrupt pending?
		return 1;
		break;
	case 0x3CA: //Read: Feature Control Register		DATA
		*result = getActiveVGA()->registers->ExternalRegisters.FEATURECONTROLREGISTER; //Give!
		if (((et34kdata->extensionsEnabled) && (getActiveVGA()->enable_SVGA==1)) || (getActiveVGA()->enable_SVGA==2)) //Enabled extensions?
		{
			*result &= 0x7F; //Clear our extension bit!
			*result |= et34kdata->ExtendedFeatureControlRegister; //Add the extended feature control!
		}
		return 1;
		break;
	case 0x3BA:	//Read: Input Status #1 Register (mono)	DATA
		if (GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER, 0, 1)) goto finishinput; //Block: we're a color mode addressing as mono!
		goto readInputStatus1Tseng;
	case 0x3DA: //Input Status #1 Register (color)	DATA
		if (GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER, 0, 1)) //Block: we're a mono mode addressing as color!
		{
		readInputStatus1Tseng:
			SETBITS(getActiveVGA()->registers->CRTControllerRegisters.REGISTERS.ATTRIBUTECONTROLLERTOGGLEREGISTER, 7, 1, 0); //Reset flipflop for 3C0!

			*result = getActiveVGA()->registers->ExternalRegisters.INPUTSTATUS1REGISTER; //Give!
			const static byte bittablelow[2][4] = { {0,4,1,6},{0,4,1,8} }; //Bit 6 is undefined on EGA!
			const static byte bittablehigh[2][4] = { {2,5,3,7},{2,5,3,8} }; //Bit 7 is undefined on EGA!
			byte DACOutput = getActiveVGA()->CRTC.DACOutput; //Current DAC output to give!
			SETBITS(*result, 4, 1, GETBITS(DACOutput, bittablelow[(getActiveVGA()->enable_SVGA == 3) ? 1 : 0][GETBITS(getActiveVGA()->registers->AttributeControllerRegisters.REGISTERS.COLORPLANEENABLEREGISTER, 4, 3)], 1));
			SETBITS(*result, 5, 1, GETBITS(DACOutput, bittablehigh[(getActiveVGA()->enable_SVGA == 3) ? 1 : 0][GETBITS(getActiveVGA()->registers->AttributeControllerRegisters.REGISTERS.COLORPLANEENABLEREGISTER, 4, 3)], 1));
			if (getActiveVGA()->enable_SVGA == 3) //EGA has lightpen support here?
			{
				SETBITS(*result, 1, 1, GETBITS(getActiveVGA()->registers->EGA_lightpenstrobeswitch, 1, 1)); //Light pen has been triggered and stopped pending? Set light pen trigger!
				SETBITS(*result, 2, 1, GETBITS(~getActiveVGA()->registers->EGA_lightpenstrobeswitch, 2, 1)); //Light pen switch is open(not pressed)?
			}

			//New bits: bit 2: CRTCB window is active, bit 6: CRTCB window is active within the current scanline!
			SETBITS(*result, 2, 1, GETBITS(getActiveVGA()->CRTC.CRTCBwindowEnabled, 0, 1)); //CRTCB active?
			SETBITS(*result, 6, 1, GETBITS(getActiveVGA()->CRTC.CRTCBwindowEnabled, 1, 1)); //CRTCB active within the current scanline?
			return 1; //Overridden!
		}
		break;
	default: //Unknown port?
		break;
	}
	finishinput:
	return 0; //Unsupported port!
}

/*
These ports are used but have little if any effect on emulation:
	3BFh (R/W): Hercules Compatibility Mode
	3CBh (R/W): PEL Address/Data Wd
	3CEh index 0Dh (R/W): Microsequencer Mode
	3CEh index 0Eh (R/W): Microsequencer Reset
	3d8h (R/W): Display Mode Control
	3DEh (W);  AT&T Mode Control Register
*/

OPTINLINE byte get_clock_index_et4k(VGA_Type *VGA) {
	// Ignoring bit 4, using "only" 16 frequencies. Looks like most implementations had only that
	return ((VGA->registers->ExternalRegisters.MISCOUTPUTREGISTER>>2)&3) | ((et34k(VGA)->store_et4k_3d4_34<<1)&4) | ((et34k(VGA)->store_et4k_3d4_31>>3)&8);
}

OPTINLINE byte get_clock_index_et3k(VGA_Type *VGA) {
	// Ignoring bit 4, using "only" 16 frequencies. Looks like most implementations had only that
	return ((VGA->registers->ExternalRegisters.MISCOUTPUTREGISTER >> 2) & 3) | ((et34k(VGA)->store_et4k_3d4_34 << 1) & 4);
}

void set_clock_index_et4k(VGA_Type *VGA, byte index) { //Used by the interrupt 10h handler to set the clock index directly!
	// Shortwiring register reads/writes for simplicity
	et34k_data->store_et4k_3d4_34 = (et34k(VGA)->store_et4k_3d4_34&~0x02)|((index&4)>>1);
	et34k_data->store_et4k_3d4_31 = (et34k(VGA)->store_et4k_3d4_31&~0xc0)|((index&8)<<3); // (index&0x18) if 32 clock frequencies are to be supported
	PORT_write_MISC_3C2((VGA->registers->ExternalRegisters.MISCOUTPUTREGISTER&~0x0c)|((index&3)<<2));
}

void set_clock_index_et3k(VGA_Type *VGA, byte index) {
	// Shortwiring register reads/writes for simplicity
	et34k_data->store_et3k_3d4_24 = (et34k_data->store_et3k_3d4_24&~0x02) | ((index & 4) >> 1);
	PORT_write_MISC_3C2((VGA->registers->ExternalRegisters.MISCOUTPUTREGISTER&~0x0c)|((index&3)<<2));
}

extern byte EMU_VGAROM[0x10000];

uint_32 Tseng4k_VRAMSize = 0; //Setup VRAM size?

extern BIOS_Settings_TYPE BIOS_Settings; //Current BIOS settings to be updated!

void Tseng4k_checkAcceleratorActivity(); //Prototype! Check for activity when something needs to be done!

void Tseng34k_init()
{
	byte *Tseng_VRAM = NULL; //The new VRAM to use with our card!
	if (getActiveVGA()) //Gotten active VGA? Initialise the full hardware if needed!
	{
		if ((getActiveVGA()->enable_SVGA==1) || (getActiveVGA()->enable_SVGA==2)) //Are we enabled as SVGA?
		{
			//Handle all that needs to be initialized for the Tseng 4K!
			// Default to 1M of VRAM
			if (getActiveVGA()->enable_SVGA == 2) //No extensions supported?
			{
				et34k(getActiveVGA())->tsengExtensions = 0; //Default: no Tseng extensions!
			}
			if (getActiveVGA()->enable_SVGA==1) //ET4000?
			{
				byte n, isvalid;
				uint_32 maxsize = 0, cursize;
				if (et34k(getActiveVGA())->tsengExtensions==0) //Normal ET4000AX?
				{
					isvalid = 0; //Default: invalid!
					for (n = 0; n < 0x10; ++n) //Try all VRAM sizes!
					{
						cursize = ((64 * 1024) << ((n & 8) >> 2)); //The memory size for this item!
						//Now, apply the bus width
						cursize <<= ((n&2)>>1) + (((n&2)>>1)&(n&1)); //setting bit 1 doubles it and setting bits 1 and 0 together doubles it again(value 2=x2, value 3=x3).
						if (Tseng4k_VRAMSize == cursize) isvalid = 1; //The memory size for this item!
						if ((cursize > maxsize) && (cursize <= Tseng4k_VRAMSize)) maxsize = cursize; //Newer within range!
					}
					if (!isvalid) //Invalid VRAM size?
					{
						Tseng4k_VRAMSize = maxsize ? maxsize : 1024 * 1024; //Always 1M by default or next smaller if possible!
						BIOS_Settings.VRAM_size = Tseng4k_VRAMSize; //Update VRAM size in BIOS!
						forceBIOSSave(); //Save the BIOS setting!
					}
				}
				else //W32 variant of ET4000?
				{
					byte n, isvalid;
					isvalid = 0; //Default: invalid!
					uint_32 maxsize = 0, cursize;
					for (n = 0; n < 0x10; ++n) //Try all VRAM sizes!
					{
						cursize = ((256 * 1024) << (((n^8) & 8) >> 2)); //Init size to detect! 256k or 1M times(bit 3) 16 or 32 bit bus width(bit 0)!
						//Now, apply the bus width
						cursize <<= 1+(n&1); //setting bit 1 doubles it and setting bits 1 and 0 together doubles it again(value 2=x2, value 3=x3).
						if (Tseng4k_VRAMSize == cursize) isvalid = 1; //The memory size for this item!
						if ((cursize > maxsize) && (cursize <= Tseng4k_VRAMSize)) maxsize = cursize; //Newer within range!
					}
					if (!isvalid) //Invalid VRAM size?
					{
						Tseng4k_VRAMSize = maxsize ? maxsize : 4096 * 1024; //Always 4M by default or next smaller if possible!
						BIOS_Settings.VRAM_size = Tseng4k_VRAMSize; //Update VRAM size in BIOS!
						forceBIOSSave(); //Save the BIOS setting!
					}
				}
				//1M+=OK!
			}
			else //ET3000?
			{
				if (Tseng4k_VRAMSize != (512 * 1024)) //Different than supported?
				{
					Tseng4k_VRAMSize = 512 * 1024; //Always 512K! (Dosbox says: "Cannot figure how this was supposed to work on the real card")
					BIOS_Settings.VRAM_size = Tseng4k_VRAMSize; //Update VRAM size in BIOS!
					forceBIOSSave(); //Save the BIOS setting!
				}
			}

			debugrow("VGA: Allocating SVGA VRAM...");
			Tseng_VRAM = (byte *)zalloc(Tseng4k_VRAMSize, "VGA_VRAM", getLock(LOCK_CPU)); //The VRAM allocated to 0!
			if (Tseng_VRAM) //VRAM allocated?
			{
				freez((void **)&getActiveVGA()->VRAM,getActiveVGA()->VRAM_size,"VGA_VRAM"); //Release the original VGA VRAM!
				getActiveVGA()->VRAM = Tseng_VRAM; //Assign the new Tseng VRAM instead!
				getActiveVGA()->VRAM_size = Tseng4k_VRAMSize; //Assign the Tseng VRAM size!
			}

			byte VRAMsize = 0;
			byte regval=0; //Highest memory size that fits!
			uint_32 memsize; //Current memory size!
			uint_32 lastmemsize = 0; //Last memory size!
			//et34k(getActiveVGA())->memwrap = et34k(getActiveVGA())->memwrap_init = (lastmemsize-1); //The memory size used!
			et34k(getActiveVGA())->memwrap = et34k(getActiveVGA())->memwrap_init = ~0; //Don't wrap any further!

			for (VRAMsize = 0;VRAMsize < 0x10;++VRAMsize) //Try all VRAM sizes!
			{
				if ((getActiveVGA()->enable_SVGA == 1) && et34k(getActiveVGA())->tsengExtensions) //ET4000/W32 variant?
				{
					memsize = ((256 * 1024) << (((VRAMsize^8) & 8) >> 2)); //Init size to detect! 256k or 1M times(bit 3 set for 256K) 16 or 32 bit bus width(bit 0)!
					memsize <<= 1+(VRAMsize&1); //setting bit 1 doubles it and setting bits 1 and 0 together doubles it again(value 2=x2, value 3=x3).
				}
				else //ET4000AX?
				{
					memsize = ((64 * 1024) << ((VRAMsize & 8) >> 2)); //The memory size for this item!
					memsize <<= ((VRAMsize&2)>>1) + (((VRAMsize&2)>>1)&(VRAMsize&1)); //setting bit 1 doubles it and setting bits 1 and 0 together doubles it again(value 2=x2, value 3=x4).
				}
				//Now, apply the bus width
				if ((memsize > lastmemsize) && (memsize <= Tseng4k_VRAMSize)) //New best match found?
				{
					if ((getActiveVGA()->enable_SVGA == 1) && et34k(getActiveVGA())->tsengExtensions) //ET4000/W32 variant?
					{
						regval = (VRAMsize & 0x9); //Use this as the new best!
					}
					else //ET4000AX?
					{
						regval = (VRAMsize & 0xB); //Use this as the new best!
					}
					lastmemsize = memsize; //Use this as the last value found!
				}
			}

			if (getActiveVGA()->enable_SVGA == 1) //ET4000 chip?
			{
				if (et34k(getActiveVGA())->tsengExtensions) //W32 chip?
				{
					//We somehow got the choice between 512KB, 1MB, 2MB and 4MB.
					if (lastmemsize >= (4 * 1024 * 1024)) //4MB
					{
						regval = 0x01; //4MB indicator!
					}
					else if (lastmemsize >= (2 * 1024 * 1024)) //2MB?
					{
						regval = 0x00; //2MB indicator!
					}
					else if (lastmemsize >= (1 * 1024 * 1024)) //1MB?
					{
						regval = 0x09; //1MB indicator! Tseng and WhatVGA says 512K, but somehow this is reported as 1MB due to hardware checking errors!
						et34k(getActiveVGA())->memwrap = et34k(getActiveVGA())->memwrap_init = 0xFFFFF; //Wrapping around 1MB!
					}
					else //512KB?
					{
						regval = 0x08; //512KB indicator!
						et34k(getActiveVGA())->memwrap = et34k(getActiveVGA())->memwrap_init = 0x7FFFF; //Wrapping around 512KB!
					}
					//Although the BIOS seems to support 256K and 512K, it cannot seem to display this?
				}
			}

			et4k_reg(et34k(getActiveVGA()),3d4,37) = et34k(getActiveVGA())->et4k_reg37_init = regval; //Apply the best register value describing our memory!

			// Tseng ROM signature
			EMU_VGAROM[0x0075] = ' ';
			EMU_VGAROM[0x0076] = 'T';
			EMU_VGAROM[0x0077] = 's';
			EMU_VGAROM[0x0078] = 'e';
			EMU_VGAROM[0x0079] = 'n';
			EMU_VGAROM[0x007a] = 'g';
			EMU_VGAROM[0x007b] = ' ';

			if ((getActiveVGA()->VRAM_size == 0x200000) && (getActiveVGA()->enable_SVGA == 1)) //ET4000 with 2MB VRAM?
			{
				if (et34k(getActiveVGA())->tsengExtensions) //W32 variant?
				{
					et34k(getActiveVGA())->store_et4k_3d4_32 |= 0x80; //VRAM is interleaved!
				}
			}

			if (getActiveVGA()->enable_SVGA == 1) //ET4000?
			{
				et34k(getActiveVGA())->store_et4k_3d4_34 |= 0x8; //We're an external card, so port 46E8 is used by default!
			}

			et34k(getActiveVGA())->extensionsEnabled = 0; //Disable the extensions by default!
			et34k(getActiveVGA())->oldextensionsEnabled = 1; //Make sure the extensions are updated in status!
			et34k(getActiveVGA())->et4k_segmentselectregisterenabled = 0; //Segment select register isn't enabled yet!
			et34k(getActiveVGA())->emulatedDAC = BIOS_Settings.SVGA_DACmode; //The emulated DAC mode!
			et34k(getActiveVGA())->SC15025_secondarypixelmaskregisters[0] = 0xFF; //Default value!
			et34k(getActiveVGA())->SC15025_secondarypixelmaskregisters[1] = 0xFF; //Default value!
			et34k(getActiveVGA())->SC15025_secondarypixelmaskregisters[2] = 0xFF; //Default value!
			et34k_updateDAC(et34k(getActiveVGA()), et34k(getActiveVGA())->hicolorDACcommand); //Initialize the DAC command register to compatible values!

			VGA_calcprecalcs(getActiveVGA(),WHEREUPDATED_ALL); //Update all precalcs!
			et34k(getActiveVGA())->W32_version = 0; //What version is reported to the user!
			SETBITS(et34k(getActiveVGA())->W32_21xA_shadowRegisters[(0xEC - 0xE0) & 0x1F], 4, 0xF, et34k(getActiveVGA())->W32_version); //Set the high 4 bits to indicate ET4000/W32!

			//Initialize what's needed for the ACL registers to be initialized!
			et34k(getActiveVGA())->W32_MMUregisters[0][0x32] = 0; //Init register by the reset! Sync disable?
			Tseng4k_checkAcceleratorActivity(); //Check for any accelerator activity!
		}
	}
}

extern byte VGAROM_mapping; //Default: all mapped in!

byte Tseng34k_doublecharacterclocks(VGA_Type *VGA)
{
	if (!(((VGA->enable_SVGA == 2) || (VGA->enable_SVGA == 1)))) return 0; //Not ET3000/ET4000!
	if (!et34k(VGA)) return 0; //Not registered?
	return et34k(VGA)->doublehorizontaltimings; //Double the horizontal timings?
}

byte Tseng4k_readMMUregister(byte address, byte *result)
{
	*result = 0xFF; //Unhandled: float the bus by default!
	switch (address) //What register to read?
	{
	case 0x00:
	case 0x01:
	case 0x02:
	case 0x03: //MMU Memory Base Pointer Register 0
	case 0x04:
	case 0x05:
	case 0x06:
	case 0x07: //MMU Memory Base Pointer Register 1
	case 0x08:
	case 0x09:
	case 0x0A:
	case 0x0B: //MMU Memory Base Pointer Register 2
	case 0x13: //MMU Control Register
	case 0x30: //ACL Suspend/Terminate Register
		*result = et34k(getActiveVGA())->W32_MMUregisters[0][address & 0xFF]; //Get the register!
		break;
	case 0x31: //ACL Operation State Register (W/O?)
		//Floating the bus?
		break;
	case 0x32: //ACL Sync Enable Register
	case 0x34: //ACL Interrupt Mask Register
	case 0x35: //ACL Interrupt Status Register
	case 0x36: //ACL Accelerator Status Register
		*result = et34k(getActiveVGA())->W32_MMUregisters[0][address & 0xFF]; //Get the register, from the currently processing register!
		break;
	case 0x80:
	case 0x81:
	case 0x82:
	case 0x83: //ACL Pattern Address Register
	case 0x84:
	case 0x85:
	case 0x86:
	case 0x87: //ACL Source Address Register
	case 0x88:
	case 0x89: //ACL Pattern Y Offset Register
	case 0x8A:
	case 0x8B: //ACL Source Y Offset Register
	case 0x8C:
	case 0x8D: //ACL Destination Y Offset Register
	case 0x8E: //ACL Virtual Bus Size Register
	case 0x8F: //ACL X/Y Direction Register
	case 0x90: //ACL Pattern Wrap Register
	case 0x92: //ACL Source Wrap Register
	case 0x94:
	case 0x95: //ACL X Position Register
	case 0x96:
	case 0x97: //ACL Y Position Register
	case 0x98:
	case 0x99: //ACL X Count Register
	case 0x9A:
	case 0x9B: //ACL Y Count Register
	case 0x9C: //ACL Routing Control Register
	case 0x9D: //ACL Reload Control Register
	case 0x9E: //ACL Background Raster Operation Register
	case 0x9F: //ACL Foreground Operation Register
	case 0xA0:
	case 0xA1:
	case 0xA2:
	case 0xA3: //ACL Destination Address Register
		*result = et34k(getActiveVGA())->W32_MMUregisters[1][address & 0xFF]; //Get the register! Unqueued registers!
		break;
	case 0xA4:
	case 0xA5:
	case 0xA6:
	case 0xA7: //ACL Internal Pattern Address Register (R/O)
	case 0xA8:
	case 0xA9:
	case 0xAA:
	case 0xAB: //ACL Internal Source Address Register (R/O)
		*result = et34k(getActiveVGA())->W32_MMUregisters[0][address & 0xFF]; //Get the register! Unqueued registers!
		break;
	default:
		//Unmapped! Floating the bus!
		break;
	}
	return 1; //Handled!
}

void Tseng4k_queuedRegistersUnmodified(); //Prototype!

void et4k_transferQueuedMMURegisters()
{
	//Note that the source and pattern address register is 3 stage instead of two-stage: it shifts from the queue into a initial register and from the initial register into the internal state! (ET4000/W32i 2.11.3 The Accelerator's queue)
	//The latter 2 stages happen internally during processing finishing up, while only the first 2 happen in this function for these registers.
	//So this only loads from the queue to said Initial registers for those registers. The latter transfer is performed by doing this twice after suspension.
	memcpy(&et34k(getActiveVGA())->W32_MMUregisters[1][0x80], &et34k(getActiveVGA())->W32_MMUregisters[0][0x80], 0x80); //Load all queued registers into the internal state!
	Tseng4k_queuedRegistersUnmodified(); //Not modified anymore!
}

extern byte is_XT; //Are we emulating an XT architecture?
void Tseng4k_raiseMMUinterrupt(byte cause) //Cause is 0-2!
{
	//Interrupt causes: Bit 0=Queue not full, 1=Queue empty and accelerator goes idle (is finished), 2=Write to a full queue
	byte oldintstatus, interrupttrigger;
	oldintstatus = et34k(getActiveVGA())->W32_MMUregisters[0][0x35]; //What was active before?
	interrupttrigger = ((1 << cause) & 7); //What cause is triggered?
	interrupttrigger &= et34k(getActiveVGA())->W32_MMUregisters[0][0x34]; //Mask the interrupt if needed!
	et34k(getActiveVGA())->W32_MMUregisters[0][0x35] |= interrupttrigger; //Set all acnowledged and unmasked interrupts!
	if ((~oldintstatus & (et34k(getActiveVGA())->W32_MMUregisters[0][0x35] & 7))) //Raised a interrupt cause?
	{
		//Raise the interrupt line!
		raiseirq(is_XT ? VGA_IRQ_XT : VGA_IRQ_AT); //Execute the CRT interrupt when possible!
	}
}

//Set when not using the CPU each clock
byte Tseng4k_accelerator_calcSSO()
{
	return (((et34k(getActiveVGA())->W32_MMUregisters[1][0x9C]&0x07)==0) || ((et34k(getActiveVGA())->W32_MMUregisters[1][0x9C] & 0x07) == 4) || ((et34k(getActiveVGA())->W32_MMUregisters[1][0x9C] & 0x07) == 5))?1:0;
}

//Basic X/Y block start/termination conditions
void Tseng4k_status_startXYblock(byte is_screentoscreen, byte doResume) //Starting an X/Y block!
{
	if (doResume) //Perform resume and become an active accelerator by this?
	{
		if ((doResume == 2) && (et34k(getActiveVGA())->W32_ACLregs.ACL_active==0)) //Starting a new block?
		{
			et34k(getActiveVGA())->W32_ACLregs.W32_newXYblock = 1; //Starting a new transfer now!
		}
		et34k(getActiveVGA())->W32_ACLregs.ACL_active = 1; //Make the accelerator active!
		et34k(getActiveVGA())->W32_ACLregs.XYSTtriggersstart = 0; //XYST doesn't trigger a start!
	}
	et34k(getActiveVGA())->W32_MMUregisters[0][0x36] |= 0x04; //Raise X/Y status!
	et34k(getActiveVGA())->W32_MMUregisters[0][0x36] &= ~0x08; //To set!
	et34k(getActiveVGA())->W32_MMUregisters[0][0x36] |= is_screentoscreen ? 0x08 : 0x00; //Screen-to-screen operation?
	Tseng4k_checkAcceleratorActivity(); //Check for accelerator activity!
}

void Tseng4k_status_XYblockTerminalCount() //Finished an X/Y block and Terminal Count reached?
{
	et34k(getActiveVGA())->W32_MMUregisters[0][0x36] &= ~0x0C; //Finished X/Y block!
	Tseng4k_checkAcceleratorActivity(); //Check for accelerator activity!
}

void Tseng4k_queuedRegisterModified()
{
	et34k(getActiveVGA())->W32_MMUregisters[0][0x36] |= 0x10; //Queue modified!
}

void Tseng4k_queuedRegistersUnmodified()
{
	et34k(getActiveVGA())->W32_MMUregisters[0][0x36] &= ~0x10; //Unmodified!
}

//Helper functions for below
void Tseng4k_status_becomebusy_queuefilled() //Became busy or Queue filled?
{
	et34k(getActiveVGA())->W32_MMUregisters[0][0x36] |= 0x02; //Raise status!
}

void Tseng4k_status_becameIdleAndQueueisempty() //Became idle and queue is empty. Called after queueEmptied always!
{
	if ((et34k(getActiveVGA())->W32_MMUregisters[0][0x36] & 0x01) == 0) //Queue was emptied already?
	{
		Tseng4k_raiseMMUinterrupt((1<<1)); //Queue was emptied and became idle interrupt!
		et34k(getActiveVGA())->W32_MMUregisters[0][0x36] &= ~0x02; //Lower status!
	}
}

void Tseng4k_processRegisters_finished(); //Prototype for the special suspending behaviour of the W32 chips!

//Basic support for accelerator operations
void Tseng4k_status_acceleratorsuspended() //Accelerator is suspended?
{
	//et34k(getActiveVGA())->W32_MMUregisters[0][0x36] &= ~0x02; //Lower status!
	//Don't lower the status. This is being done by the accelerator becoming idle only!
}

void Tseng4k_status_queueFilled(byte is_suspendterminate) //Queue has been filled?
{
	if (is_suspendterminate) //Suspend/Terminate?
	{
		et34k(getActiveVGA())->W32_MMUsuspendterminatefilled |= is_suspendterminate; //Fill the suspend/terminate flag!
	}
	et34k(getActiveVGA())->W32_MMUregisters[0][0x36] |= 0x01; //Raise status!
	Tseng4k_status_becomebusy_queuefilled(); //Became busy or queue was filled!
	Tseng4k_checkAcceleratorActivity(); //Check for activity to be done!
}

byte Tseng4k_status_multiqueueNotFull() //Is the multi queue not full?
{
	return (fifobuffer_freesize(et34k(getActiveVGA())->W32_MMUqueue) != 0); //Is the multi queue not full?
}

byte Tseng4k_status_multiqueueFilled()
{
	uint_32 data1,data2;
	if (peekfifobuffer32_2u(et34k(getActiveVGA())->W32_MMUqueue,&data1,&data2)) //Filled?
	{
		return ((data1>>24)&0xFF); //The queue filled status!
	}
	return 0; //Not filled!
}

byte Tseng4k_status_virtualbusmultiqueueFilled()
{
	uint_32 data1, data2;
	if (peekfifobuffer32_2u(et34k(getActiveVGA())->W32_virtualbusqueue, &data1, &data2)) //Filled?
	{
		return ((data1 >> 24) & 0xFF); //The queue filled status!
	}
	return 0; //Not filled!
}

byte Tseng4k_status_readMultiQueue()
{
	uint_32 data1,data2;
	if (readfifobuffer32_2u(et34k(getActiveVGA())->W32_MMUqueue,&data1,&data2)) //Filled?
	{
		et34k(getActiveVGA())->W32_MMUqueueval_bankaddress = (data2&0xFFFFFF); //Bank address!
		et34k(getActiveVGA())->W32_MMUqueueval_address = (data1&0xFFFFFF); //Address!
		et34k(getActiveVGA())->W32_MMUqueueval = (data2>>24)&0xFF; //The value!
		Tseng4k_checkAcceleratorActivity(); //Check for accelerator activity!
		return ((data1>>24)&0xFF); //The queue filled status!
	}
	return 0; //Not filled!
}

byte Tseng4k_status_readVirtualBusMultiQueue()
{
	uint_32 data1, data2;
	if (readfifobuffer32_2u(et34k(getActiveVGA())->W32_virtualbusqueue, &data1, &data2)) //Filled?
	{
		et34k(getActiveVGA())->W32_virtualbusqueueval_bankaddress = (data2 & 0xFFFFFF); //Bank address!
		et34k(getActiveVGA())->W32_virtualbusqueueval_address = (data1 & 0xFFFFFF); //Address!
		et34k(getActiveVGA())->W32_virtualbusqueueval = (data2 >> 24) & 0xFF; //The value!
		Tseng4k_checkAcceleratorActivity(); //Check for accelerator activity!
		return ((data1 >> 24) & 0xFF); //The queue filled status!
	}
	return 0; //Not filled!
}

void Tseng4k_status_clearvirtualbus()
{
	fifobuffer_clear(et34k(getActiveVGA())->W32_virtualbusqueue); //Clear the virtual bus queue!
	et34k(getActiveVGA())->W32_VirtualBusCountLeft = 0; //No count left on the virtual bus to process!
}

byte Tseng4k_status_virtualbusfull()
{
	if (et34k(getActiveVGA())->W32_VirtualBusCountLeft) //Processing the virtual bus? Count as full?
	{
		return 1; //Virtual bus forced as full!
	}
	return ((fifobuffer_freesize(et34k(getActiveVGA())->W32_virtualbusqueue) >> 3)==0); //Is the queue full?
}

byte Tseng4k_status_virtualbusentries()
{
	return ((fifobuffer_size(et34k(getActiveVGA())->W32_virtualbusqueue) - fifobuffer_freesize(et34k(getActiveVGA())->W32_virtualbusqueue)) >> 3); //How many entries are filled in the buffer?
}

byte Tseng4k_status_peekMultiQueue_apply()
{
	uint_32 data1, data2;
	if (peekfifobuffer32_2u(et34k(getActiveVGA())->W32_MMUqueue, &data1, &data2)) //Filled?
	{
		et34k(getActiveVGA())->W32_MMUqueueval_bankaddress = (data2 & 0xFFFFFF); //Bank address!
		et34k(getActiveVGA())->W32_MMUqueueval_address = (data1 & 0xFFFFFF); //Address!
		et34k(getActiveVGA())->W32_MMUqueueval = (data2 >> 24) & 0xFF; //The value!
		return ((data1 >> 24) & 0xFF); //The queue filled status!
	}
	return 0; //Not filled!
}

byte Tseng4k_status_writeMultiQueue(byte type, byte value, uint_32 address, uint_32 bank)
{
	byte result; //The result!
	uint_32 data1, data2;
	if (!type) return 0; //Invalid type?
	data1 = (address&0xFFFFFF)|(type<<24); //First data: address and type!
	data2 = (bank&0xFFFFFF)|(value<<24); //Second data: bank and value!
	result = writefifobuffer32_2u(et34k(getActiveVGA())->W32_MMUqueue,data1,data2); //Write to the queue!
	Tseng4k_checkAcceleratorActivity(); //Check for activity to be done!
	return result; //Give the result!
}

byte Tseng4k_status_writeVirtualBusMultiQueue(byte type, byte value, uint_32 address, uint_32 bank)
{
	byte result; //The result!
	uint_32 data1, data2;
	if (!type) return 0; //Invalid type?
	data1 = (address & 0xFFFFFF) | (type << 24); //First data: address and type!
	data2 = (bank & 0xFFFFFF) | (value << 24); //Second data: bank and value!
	result = writefifobuffer32_2u(et34k(getActiveVGA())->W32_virtualbusqueue, data1, data2); //Write to the queue!
	Tseng4k_checkAcceleratorActivity(); //Check for activity to be done!
	return result; //Give the result!
}

void Tseng4k_status_queueEmptied() //Queue has been emptied by processing?
{
	if ((et34k(getActiveVGA())->W32_MMUsuspendterminatefilled | Tseng4k_status_multiqueueFilled()) == 0) //The suspend and normal queue are emptied?
	{
		if (et34k(getActiveVGA())->W32_MMUregisters[0][0x36] & 0x01) //Was filled?
		{
			Tseng4k_raiseMMUinterrupt(1); //Queue has been emptied interrupt!
		}
		et34k(getActiveVGA())->W32_MMUregisters[0][0x36] &= ~0x01; //Lower status!
		Tseng4k_checkAcceleratorActivity(); //Check for accelerator activity!
	}
}

void Tseng4k_doBecomeIdle(); //Accelerator becomes idle! PROTOTYPE!

void Tseng4k_status_suspendterminatecleared(byte is_suspendterminate, byte oldsuspendterminate)
{
	byte wasfilled;
	wasfilled = oldsuspendterminate; //Was the queue filled with us?
	et34k(getActiveVGA())->W32_MMUsuspendterminatefilled &= ~is_suspendterminate; //Clear the requested flags!
	if ((et34k(getActiveVGA())->W32_MMUsuspendterminatefilled == 0) && wasfilled) //Fully cleared now while we were filled?
	{
		Tseng4k_status_queueEmptied(); //Check for emptying of the queue!
		if ((et34k(getActiveVGA())->W32_acceleratorbusy & 2) == 0) //Fully suspended?
		{
			Tseng4k_doBecomeIdle(); //Accelerator became idle!
		}
		Tseng4k_checkAcceleratorActivity(); //Check for accelerator activity!
	}
}

byte Tseng4k_doEmptyQueue() //Try and perform an emptying of the queue! Result: 1=Was filled, 0=Was already empty
{
	byte result;
	if ((result = Tseng4k_status_readMultiQueue())!=0) //The queue was filled?
	{
		Tseng4k_status_queueEmptied(); //The queue has been emptied now!
		return result; //Was filled with this value!
	}
	return 0; //Was empty already!
}

void Tseng4k_status_suspendterminatefinished()
{
	et34k(getActiveVGA())->W32_MMUsuspendterminatefilled = 0; //Not anymore!
	Tseng4k_status_queueEmptied(); //The queue has been emptied now!
	Tseng4k_checkAcceleratorActivity(); //Check for accelerator activity!
}

void Tseng4k_encodeAcceleratorRegisters(); //Prototype for becoming idle!

byte Tseng4k_idlequeueresult = 0;
void Tseng4k_doBecomeIdle() //Accelerator becomes idle!
{
	if (et34k(getActiveVGA())->W32_transferstartedbyMMU) //Started transfer by the MMU?
	{
		Tseng4k_idlequeueresult = Tseng4k_doEmptyQueue(); //Acnowledge and empty the queue: it's a start trigger instead!
		et34k(getActiveVGA())->W32_transferstartedbyMMU = 0; //Not started by the MMU anymore!
	}
	if ((Tseng4k_status_multiqueueFilled()|et34k(getActiveVGA())->W32_MMUsuspendterminatefilled) == 0) //Queue isn't filled anymore while becoming idle?
	{
		Tseng4k_status_becameIdleAndQueueisempty(); //Became idle and queue is empty!
	}
	et34k(getActiveVGA())->W32_acceleratorleft = 0; //Starting a new operation, so start with new byte data inputs!
	Tseng4k_encodeAcceleratorRegisters(); //Encode the accelerator registers: documentation says they're readable with valid values now!
}

byte ET4k_effectivevirtualbussize[4] = {1,2,4,4}; //The effective virtual bus size specified!

void Tseng4k_decodeAcceleratorRegisters()
{
	//TODO: Load and decode all accelerator registers into easy to use variables in the accelerator
	et34k(getActiveVGA())->W32_ACLregs.patternmapaddress = (getTsengLE24(&et34k(getActiveVGA())->W32_MMUregisters[1][0x80]) & 0x3FFFFF); //Pattern map address
	et34k(getActiveVGA())->W32_ACLregs.sourcemapaddress = (getTsengLE24(&et34k(getActiveVGA())->W32_MMUregisters[1][0x84]) & 0x3FFFFF); //Source map address
	et34k(getActiveVGA())->W32_ACLregs.patternYoffset = (getTsengLE16(&et34k(getActiveVGA())->W32_MMUregisters[1][0x88]) & 0xFFF); //Pattern Y offset
	et34k(getActiveVGA())->W32_ACLregs.sourceYoffset = (getTsengLE16(&et34k(getActiveVGA())->W32_MMUregisters[1][0x8A]) & 0xFFF); //Source Y offset
	et34k(getActiveVGA())->W32_ACLregs.destinationYoffset = (getTsengLE16(&et34k(getActiveVGA())->W32_MMUregisters[1][0x8C]) & 0xFFF); //Destination Y offset
	et34k(getActiveVGA())->W32_ACLregs.virtualbussize = (et34k(getActiveVGA())->W32_MMUregisters[1][0x8E] & 3); //Virtual bus size, powers of 2! 0=1, 1=2, 2=4, 3=Reserved
	et34k(getActiveVGA())->W32_ACLregs.XYdirection = (et34k(getActiveVGA())->W32_MMUregisters[1][0x8F] & 3); //0=+1,+1; 1=-1,+1; 2=+1,-1; 3=-1,-1. Essentially bit 0=X direction, bit 1=Y direction. Set=Decreasing, Cleared=Increasing
	et34k(getActiveVGA())->W32_ACLregs.Xpatternwrap = (et34k(getActiveVGA())->W32_MMUregisters[1][0x90] & 7); //Power of 2. more than 64 or less than 4 is none.
	et34k(getActiveVGA())->W32_ACLregs.Ypatternwrap = ((et34k(getActiveVGA())->W32_MMUregisters[1][0x90] >> 4) & 7); //Power of 2. more than 8 is none.
	et34k(getActiveVGA())->W32_ACLregs.patternwrap_bit6 = ((et34k(getActiveVGA())->W32_MMUregisters[1][0x90] >> 6) & 1); //Bit 6 only
	et34k(getActiveVGA())->W32_ACLregs.Xsourcewrap = (et34k(getActiveVGA())->W32_MMUregisters[1][0x92] & 7); //See pattern wrap
	et34k(getActiveVGA())->W32_ACLregs.Ysourcewrap = ((et34k(getActiveVGA())->W32_MMUregisters[1][0x92] >> 4) & 7); //See pattern wrap
	et34k(getActiveVGA())->W32_ACLregs.sourcewrap_bit6 = ((et34k(getActiveVGA())->W32_MMUregisters[1][0x92] >> 6) & 1); //See pattern wrap
	et34k(getActiveVGA())->W32_ACLregs.Xposition = (getTsengLE16(&et34k(getActiveVGA())->W32_MMUregisters[1][0x94]) & 0xFFF); //X position
	et34k(getActiveVGA())->W32_ACLregs.Yposition = (getTsengLE16(&et34k(getActiveVGA())->W32_MMUregisters[1][0x96]) & 0xFFF); //Y position
	et34k(getActiveVGA())->W32_ACLregs.Xcount = (getTsengLE16(&et34k(getActiveVGA())->W32_MMUregisters[1][0x98]) & 0xFFF); //X count
	et34k(getActiveVGA())->W32_ACLregs.Ycount = (getTsengLE16(&et34k(getActiveVGA())->W32_MMUregisters[1][0x9A]) & 0xFFF); //Y count
	et34k(getActiveVGA())->W32_ACLregs.reloadPatternAddress = GETBITS(et34k(getActiveVGA())->W32_MMUregisters[1][0x9D], 1, 1); //Reload of pattern address. When 1, use the internal pattern address register.
	et34k(getActiveVGA())->W32_ACLregs.reloadSourceAddress = GETBITS(et34k(getActiveVGA())->W32_MMUregisters[1][0x9D], 0, 1); //Reload of source address. When 1, use the internal source address register.
	et34k(getActiveVGA())->W32_ACLregs.BGFG_RasterOperation[0] = et34k(getActiveVGA())->W32_MMUregisters[1][0x9E]; //Index 0=BG, 1=FG
	et34k(getActiveVGA())->W32_ACLregs.BGFG_RasterOperation[1] = et34k(getActiveVGA())->W32_MMUregisters[1][0x9F]; //Index 0=BG, 1=FG
	et34k(getActiveVGA())->W32_ACLregs.destinationaddress = (getTsengLE24(&et34k(getActiveVGA())->W32_MMUregisters[1][0xA0]) & 0x3FFFFF); //Destination address
	//We're now in a waiting state always! Make us waiting!
	et34k(getActiveVGA())->W32_ACLregs.ACL_active = 0; //Starting a new operation, so not started by default yet}
	//Specify the effective virtual bus size to use!
	et34k(getActiveVGA())->W32_ACLregs.virtualbussizecount = (((et34k(getActiveVGA())->W32_MMUregisters[1][0x9C] & 7)==1) || ((et34k(getActiveVGA())->W32_MMUregisters[1][0x9C] & 7) == 2))?ET4k_effectivevirtualbussize[et34k(getActiveVGA())->W32_ACLregs.virtualbussize]:1; //Virtual bus size, powers of 2! 0=1, 1=2, 2=4, 3=Reserved
	Tseng4k_doBecomeIdle(); //Accelerator itself becomes idle, waiting for inputs!
	Tseng4k_status_clearvirtualbus(); //Clear the virtual bus to be ready for the operation that can now be started!
}

void Tseng4k_encodeAcceleratorRegisters()
{
	//TODO: Save and encode all changable accelerator registers that can be modified into the accelerator registers for the CPU to read.
	setTsengLE24(&et34k(getActiveVGA())->W32_MMUregisters[1][0x80], (et34k(getActiveVGA())->W32_ACLregs.patternmapaddress & 0x3FFFFF)); //Pattern map address
	setTsengLE24(&et34k(getActiveVGA())->W32_MMUregisters[1][0x84], (et34k(getActiveVGA())->W32_ACLregs.sourcemapaddress & 0x3FFFFF)); //Source map address
	setTsengLE32(&et34k(getActiveVGA())->W32_MMUregisters[1][0xA4], et34k(getActiveVGA())->W32_ACLregs.internalpatternaddress); //Internal Pattern address
	setTsengLE32(&et34k(getActiveVGA())->W32_MMUregisters[1][0xA8], et34k(getActiveVGA())->W32_ACLregs.internalsourceaddress); //Internal Source address
	setTsengLE16(&et34k(getActiveVGA())->W32_MMUregisters[1][0x94], et34k(getActiveVGA())->W32_ACLregs.Xposition); //X position
	setTsengLE16(&et34k(getActiveVGA())->W32_MMUregisters[1][0x96], et34k(getActiveVGA())->W32_ACLregs.Yposition); //Y position
}

uint_32 Tseng4k_wrap_x[8] = { 0,0,3,7,0xF,0x1F,0x3F,~0 }; //X wrapping masks
uint_32 Tseng4k_wrap_y[8] = { 1,2,4,8,~0,~0,~0,~0 }; //Y wrapping masks

void Tseng4k_calcPatternSourceXY(uint_32* patternsourcex, uint_32* patternsourcex_backup, uint_32* patternsourcey, uint_32 *patternsourceaddress, uint_32 *patternsourceaddress_backup, uint_32 patternsourcewrapx, uint_32 patternsourcewrapy,  byte patternsourcewrapbit6)
{
	*patternsourcex = *patternsourcey = 0; //Init!
	//Handle horizontal first!
	if (patternsourcewrapx) //Wrapping horizontally?
	{
		*patternsourcex = *patternsourceaddress & patternsourcewrapx;
		*patternsourceaddress &= ~patternsourcewrapx; //Wrap!
	}
	*patternsourceaddress_backup = *patternsourceaddress;
	//Now, handle vertical wrap!
	if (!patternsourcewrapbit6) //Bit 6 not set?
	{
		*patternsourcey = (*patternsourceaddress / (patternsourcewrapx + 1)) & (patternsourcewrapy - 1);
		*patternsourceaddress_backup &= ~(((patternsourcewrapx + 1) * patternsourcewrapy) - 1);
	}
	*patternsourcex_backup = *patternsourcex;
}

//triggerfromMMU: 0=Resume operation, 1=MMU triggered.
void Tseng4k_startAccelerator(byte triggerfromMMU)
{
	//Start the accelerator's function.
	//Load all internal precalcs required and initialize all local required CPU-readonly variables.
	Tseng4k_decodeAcceleratorRegisters(); //Load all registers into the accelerator's precalcs!
	//TODO: Load the read-only variables for the accelerator to start using. Also depends on the reloadPatternAddress and reloadSourceAddress ACL settings.
	if (triggerfromMMU) //Only reload Source/Pattern address when not a resume operation!
	{
		if (et34k(getActiveVGA())->W32_ACLregs.reloadPatternAddress == 0) //To reload the pattern address?
		{
			et34k(getActiveVGA())->W32_ACLregs.internalpatternaddress = et34k(getActiveVGA())->W32_ACLregs.patternmapaddress; //Pattern address reload!
		}
		if (et34k(getActiveVGA())->W32_ACLregs.reloadSourceAddress == 0) //To reload the pattern address?
		{
			et34k(getActiveVGA())->W32_ACLregs.internalsourceaddress = et34k(getActiveVGA())->W32_ACLregs.sourcemapaddress; //Pattern address reload!
		}
	}
	//Perform what wrapping?
	et34k(getActiveVGA())->W32_ACLregs.patternwrap_x = Tseng4k_wrap_x[et34k(getActiveVGA())->W32_ACLregs.Xpatternwrap]; //What horizontal wrapping to use!
	et34k(getActiveVGA())->W32_ACLregs.patternwrap_y = Tseng4k_wrap_y[et34k(getActiveVGA())->W32_ACLregs.Ypatternwrap]; //What horizontal wrapping to use!
	et34k(getActiveVGA())->W32_ACLregs.sourcewrap_x = Tseng4k_wrap_x[et34k(getActiveVGA())->W32_ACLregs.Xsourcewrap]; //What horizontal wrapping to use!
	et34k(getActiveVGA())->W32_ACLregs.sourcewrap_y = Tseng4k_wrap_y[et34k(getActiveVGA())->W32_ACLregs.Ysourcewrap]; //What horizontal wrapping to use!
	//Perform wrapping of the inputs!
	//First, wrap pattern!
	//Next, wrap source!
	Tseng4k_calcPatternSourceXY(&et34k(getActiveVGA())->W32_ACLregs.patternmap_x,
		&et34k(getActiveVGA())->W32_ACLregs.patternmap_x_backup,
		&et34k(getActiveVGA())->W32_ACLregs.patternmap_y,
		&et34k(getActiveVGA())->W32_ACLregs.internalpatternaddress,
		&et34k(getActiveVGA())->W32_ACLregs.patternmapaddress_backup,
		et34k(getActiveVGA())->W32_ACLregs.patternwrap_x,
		et34k(getActiveVGA())->W32_ACLregs.patternwrap_y,
		et34k(getActiveVGA())->W32_ACLregs.patternwrap_bit6 //Unsupported paramter on the ET4000/W32?
		);
	Tseng4k_calcPatternSourceXY(&et34k(getActiveVGA())->W32_ACLregs.sourcemap_x,
		&et34k(getActiveVGA())->W32_ACLregs.sourcemap_x_backup,
		&et34k(getActiveVGA())->W32_ACLregs.sourcemap_y,
		&et34k(getActiveVGA())->W32_ACLregs.internalsourceaddress,
		&et34k(getActiveVGA())->W32_ACLregs.sourcemapaddress_backup,
		et34k(getActiveVGA())->W32_ACLregs.sourcewrap_x,
		et34k(getActiveVGA())->W32_ACLregs.sourcewrap_y,
		et34k(getActiveVGA())->W32_ACLregs.sourcewrap_bit6 //Unsupported paramter on the ET4000/W32?
	);
	//Backup the original values before starting!
	et34k(getActiveVGA())->W32_ACLregs.destinationaddress_backup = et34k(getActiveVGA())->W32_ACLregs.destinationaddress; //Backup!
	if ((et34k(getActiveVGA())->W32_MMUregisters[1][0x9C] & 7) == 0) //No CPU version?
	{
		et34k(getActiveVGA())->W32_acceleratorleft = 1; //Always more left until finishing! This keeps us running!
	}
	else //New inputs required for the accelerated operation?
	{
		et34k(getActiveVGA())->W32_acceleratorleft = 0; //Starting a new operation!
	}
	et34k(getActiveVGA())->W32_performMMUoperationstart = triggerfromMMU; //Trigger start from MMU type write?
	if ((Tseng4k_status_multiqueueFilled() == 0) && (!et34k(getActiveVGA())->W32_performMMUoperationstart)) //Queue not filled yet and not starting from the accelerator window?
	{
		//Make sure that the queue addresses are properly set!
		et34k(getActiveVGA())->W32_virtualbusqueueval_bankaddress = 0; //Default: no bank address loaded yet!
		et34k(getActiveVGA())->W32_virtualbusqueueval_address = et34k(getActiveVGA())->W32_ACLregs.destinationaddress; //Default: no address loaded yet, so use the specified address!
	}
}

byte et4k_emptyqueuedummy = 0;

byte Tseng4k_writeMMUregisterUnqueued(byte address, byte value)
{
	byte oldsuspendterminate;
	byte oldintstatus;
	//Unhandled!
	switch (address) //What register to read?
	{
	case 0x00:
	case 0x01:
	case 0x02:
	case 0x03: //MMU Memory Base Pointer Register 0
	case 0x04:
	case 0x05:
	case 0x06:
	case 0x07: //MMU Memory Base Pointer Register 1
	case 0x08:
	case 0x09:
	case 0x0A:
	case 0x0B: //MMU Memory Base Pointer Register 2
	case 0x13: //MMU Control Register
		et34k(getActiveVGA())->W32_MMUregisters[0][address & 0xFF] = value; //Set the register!
		if (address < 0x13) //Not the control register?
		{
			VGA_calcprecalcs(getActiveVGA(), WHEREUPDATED_MEMORYMAPPED | (address & ~3)); //This memory mapped register has been updated!
		}
		else
		{
			VGA_calcprecalcs(getActiveVGA(), WHEREUPDATED_MEMORYMAPPED | 0x13); //The memory mapped register has been updated!
		}
		//We might be having to update some internal variables when they are updated(memory mapping of the 3 base pointer registers and their handling in the MMU).
		break;
	case 0x30: //ACL Suspend/Terminate Register
	case 0x31: //ACL Operation State Register (W/O?)
	case 0x32: //ACL Sync Enable Register
	case 0x34: //ACL Interrupt Mask Register
		et34k(getActiveVGA())->W32_MMUregisters[0][address & 0xFF] = value; //Set the register with all it's bits writable!
		if (address == 0x31) //Restore/Resume operation?
		{
			if (value & 0x01) //Restore operation?
			{
				//This also discards the reported encoded internal pattern/source address registers!
				//Report the previous initial pattern/source map address after this!
				et34k(getActiveVGA())->W32_ACLregs.internalpatternaddress = et34k(getActiveVGA())->W32_ACLregs.patternmapaddress; //Pattern address always reload!
				et34k(getActiveVGA())->W32_ACLregs.internalsourceaddress = et34k(getActiveVGA())->W32_ACLregs.sourcemapaddress; //Pattern address always reload!
				//Now, perform the shift!
				et4k_transferQueuedMMURegisters(); //Load the queued MMU registers!
				Tseng4k_decodeAcceleratorRegisters(); //Decode the registers now loaded!
				//This always loads the internal pattern/source registers with their initial values (performing a 3-stage shift twice in effect)!
				Tseng4k_encodeAcceleratorRegisters(); //Encode the registers now changed!
				et34k(getActiveVGA())->W32_ACLregs.XYSTtriggersstart = 1; //XYST triggers a start after this!
			}
			if (value & 0x08) //Resume operation? Can be combined with above restore operation!
			{
				et4k_emptyqueuedummy = Tseng4k_doEmptyQueue(); //Empty the queue if possible for the new operation to start!
				if (value & 0x01) //Not Resume without restore (so not the backup/restore handler)? This is the part past the handler as documented in the documentation!
				{
					//Modify into a 3-stage shift! Filling the internal map registers!
					//Report the previous initial pattern/source map address after this!
					et34k(getActiveVGA())->W32_ACLregs.internalpatternaddress = et34k(getActiveVGA())->W32_ACLregs.patternmapaddress; //Pattern address always reload!
					et34k(getActiveVGA())->W32_ACLregs.internalsourceaddress = et34k(getActiveVGA())->W32_ACLregs.sourcemapaddress; //Pattern address always reload!
				}
				Tseng4k_startAccelerator(0); //Starting the accelerator by Resume trigger!
				Tseng4k_status_startXYblock(Tseng4k_accelerator_calcSSO(), 2); //Starting a transfer! Make the accelerator active again!
				et34k(getActiveVGA())->W32_mixmapposition = 0; //Initialize the mix map position to the first bit!
				et34k(getActiveVGA())->W32_transferstartedbyMMU = 0; //Not started by the MMU!
				et34k(getActiveVGA())->W32_ACLregs.XYSTtriggersstart = 0; //XYST doesn't trigger a start!
			}
		}
		if (address == 0x30) //Suspend operation requested?
		{
			//Check for raising first!
			if (value & 0x01) //Raised suspend?
			{
				//The accelerator should now be suspending operation and become idle!
				Tseng4k_status_queueFilled(0x01); //The queue has been filled for suspend/termination!
			}

			if (value & 0x10) //Raised terminate?
			{
				//The accelerator should now be terminating operation and become idle!
				Tseng4k_status_queueFilled(0x10); //The queue has been filled for suspend/termination!
			}

			oldsuspendterminate = et34k(getActiveVGA())->W32_MMUsuspendterminatefilled; //Old suspend/terminate status!
			//Now, check for lowering!
			if ((value & 0x01) == 0) //Cleared suspend?
			{
				Tseng4k_status_suspendterminatecleared(0x01, oldsuspendterminate); //The suspend queue has been cleared!
			}
			if ((value & 0x10) == 0) //Cleared terminate?
			{
				Tseng4k_status_suspendterminatecleared(0x10, oldsuspendterminate); //The terminate queue has been cleared!
			}
		}
		break;
	case 0x35: //ACL Interrupt Status Register
		//Writing bits 0-2 with a set bit clears said interrupt condition!
		//Clearing all set bits clears the interrupt status!
		SETBITS(et34k(getActiveVGA())->W32_MMUregisters[0][0x35],3,0x1F,GETBITS(value,3,0x1F)); //Set the high bits!
		oldintstatus = et34k(getActiveVGA())->W32_MMUregisters[0][0x35]; //What is causing the interrupt!
		et34k(getActiveVGA())->W32_MMUregisters[0][0x35] &= ~(value & 7); //Clear all acnowledged interrupts!
		if ((oldintstatus & ~(value & 7)) == 0) //Cleared all interrupt causes?
		{
			//Lower the interrupt line!
			lowerirq(is_XT ? VGA_IRQ_XT : VGA_IRQ_AT); //Lower the CRT interrupt when possible!
		}
		//Interrupt causes: Bit 0=Queue not full, 1=Queue empty and accelerator goes idle, 2=Write to a full queue
		break;
	case 0x36: //ACL Accelerator Status Register
		SETBITS(et34k(getActiveVGA())->W32_MMUregisters[0][0x36], 5, 0x7, GETBITS(value, 5, 0x7)); //Bits 5-7 are set directly to whatever is written (marked as Reserved)!
		if ((value & 4) && (!et34k(getActiveVGA())->W32_ACLregs.XYSTtriggersstart)) //Raised/SET XYST while not allowing XYST to trigger a start?
		{
			Tseng4k_startAccelerator(0); //Starting the accelerator by Resume trigger!
			Tseng4k_status_startXYblock(Tseng4k_accelerator_calcSSO(), 0); //Starting a transfer, special active case for triggering by resume! Don't trigger it to start yet (waiting for a resume first)!
			et34k(getActiveVGA())->W32_mixmapposition = 0; //Initialize the mix map position to the first bit!
			et34k(getActiveVGA())->W32_transferstartedbyMMU = 0; //Not started by the MMU!
		}
		else if ((value & 4) && (et34k(getActiveVGA())->W32_ACLregs.XYSTtriggersstart)) //Raised/SET XYST while allowing XYST to trigger a start?
		{
			Tseng4k_startAccelerator(0); //Starting the accelerator by Resume trigger!
			Tseng4k_status_startXYblock(Tseng4k_accelerator_calcSSO(), 1); //Starting a transfer, special active case for triggering by resume!
			et34k(getActiveVGA())->W32_mixmapposition = 0; //Initialize the mix map position to the first bit!
			et34k(getActiveVGA())->W32_transferstartedbyMMU = 0; //Not started by the MMU!
		}
		else if (((value & 4) == 0) && (et34k(getActiveVGA())->W32_MMUregisters[0][0x36] & 4)) //Lowered XYST?
		{
			Tseng4k_status_XYblockTerminalCount(); //Perform like a terminal count!
			if ((et34k(getActiveVGA())->W32_MMUregisters[1][0x9C] & 7) == 0) //CPU data isn't used?
			{
				et4k_emptyqueuedummy = Tseng4k_doEmptyQueue(); //Acnowledge and empty the queue: it's a start trigger instead!
				et34k(getActiveVGA())->W32_transferstartedbyMMU = 0; //Not started by the MMU anymore!
			}
		}
		break;
	case 0x80:
	case 0x81:
	case 0x82:
	case 0x83: //ACL Pattern Address Register
	case 0x84:
	case 0x85:
	case 0x86:
	case 0x87: //ACL Source Address Register
	case 0x88:
	case 0x89: //ACL Pattern Y Offset Register
	case 0x8A:
	case 0x8B: //ACL Source Y Offset Register
	case 0x8C:
	case 0x8D: //ACL Destination Y Offset Register
	case 0x8E: //ACL Virtual Bus Size Register
	case 0x8F: //ACL X/Y Direction Register
	case 0x90: //ACL Pattern Wrap Register
	case 0x92: //ACL Source Wrap Register
	case 0x94:
	case 0x95: //ACL X Position Register
	case 0x96:
	case 0x97: //ACL Y Position Register
	case 0x98:
	case 0x99: //ACL X Count Register
	case 0x9A:
	case 0x9B: //ACL Y Count Register
	case 0x9C: //ACL Routing Control Register
	case 0x9D: //ACL Reload Control Register
	case 0x9E: //ACL Background Raster Operation Register
	case 0x9F: //ACL Foreground Operation Register
	case 0xA0:
	case 0xA1:
	case 0xA2:
	case 0xA3: //ACL Destination Address Register
		Tseng4k_queuedRegisterModified(); //Modified queue!
		et34k(getActiveVGA())->W32_MMUregisters[0][address & 0xFF] = value; //Set the register, queued!
		//ET4000/W32i: 2.11.2 Starting an Accelerator Operation says this is caused by writes to the MMU aperture and Accelerator Operation State Register only!
		break;
	case 0xA4:
	case 0xA5:
	case 0xA6:
	case 0xA7: //ACL Internal Pattern Address Register (R/O)
	case 0xA8:
	case 0xA9:
	case 0xAA:
	case 0xAB: //ACL Internal Source Address Register (R/O)
		//Not writable, have no effect!
		break;
	}
	return 1; //Handled!
}

extern byte MMU_waitstateactive; //Waitstate active?

byte Tseng4k_readMMUaccelerator(byte area, uint_32 address, byte* result)
{
	//Apply area first, if used!
	//Not implemented on the emulated chip?
	MMU_waitstateactive = 0; //No wait state!
	*result = 0xFF; //Not implemented yet!
	return 1; //Handled!
}

//Handling of termination of a CPU access!
void Tseng4k_handleTermination()
{
	if (getActiveVGA()->enable_SVGA==1) //ET4000 chip?
	{
		if (et34k(getActiveVGA())) //Tseng chips valid?
		{
			et34k(getActiveVGA())->W32_waitstateremainderofqueue = 0; //Not waitstating on the remainder of the transfer anymore!
		}
	}
}

byte Tseng4k_writeMMUaccelerator(byte area, uint_32 address, byte value)
{
	byte queuetype;
	uint_32 queuebank,queueaddress;
	MMU_waitstateactive = 0; //No wait state!
	//Index 32 bit 0 being set causes this to set MMU_waitstateactive to 1 when not ready yet and abort (waitstate).
	//Otherwise, when not ready yet, ignore!
	if (Tseng4k_status_multiqueueFilled()||et34k(getActiveVGA())->W32_MMUsuspendterminatefilled) //Queue already filled?
	{
		if ((et34k(getActiveVGA())->W32_MMUregisters[0][0x32] & 1) || et34k(getActiveVGA())->W32_waitstateremainderofqueue) //Waitstate to apply for writes?
		{
			if (et34k(getActiveVGA())->W32_waitstateremainderofqueue && (Tseng4k_status_multiqueueNotFull())) //Waitstate the remainder and the multi queue isn't filled up yet?
			{
				goto fillmultiqueuefurther; //Fill up the multi queue further, as there is room to fill!
			}
			MMU_waitstateactive = 1; //Start waitstate to become ready!
			return 1; //Handled!
		}
		//Filled queue with write to it?
		Tseng4k_raiseMMUinterrupt(1 << 2); //Report write to a filled queue to the machine!
		return 1; //Otherwise, the write is ignored!
	}
	fillmultiqueuefurther: //Filling the multi queue further?
	//Handle any storage needed!
	if (area != 4) //Not to the MMU queue?
	{
		queuetype = 1; //The queue is now filled!
		queuebank = getActiveVGA()->precalcs.MMU012_aperture[area & 3]; //Apply the area we're selecting to get the actual VRAM address!
	}
	else //To the MMU registers?
	{
		queuetype = 4; //The MMU register queue is now filled!
		queuebank = 0; //No bank address used!
	}

	queueaddress = address; //What offset is filled!
	Tseng4k_status_writeMultiQueue(queuetype,value,queueaddress,queuebank); //Fill the specific offset that's filled!
	Tseng4k_status_queueFilled(0); //The queue has been filled!
	et34k(getActiveVGA())->W32_waitstateremainderofqueue = 1; //Waitstate the remainder of the transfer if needed!
	return 1; //Handled!
}

byte Tseng4k_writeMMUregister(byte address, byte value)
{
	if (address >= 0x80) //Queued register?
	{
		//Write to the queue instead, only to write to the MMU registers later!
		return Tseng4k_writeMMUaccelerator(4, address, value); //Write to the queue for the ACL registers instead!
	}
	else //Unqueued?
	{
		return Tseng4k_writeMMUregisterUnqueued(address, value); //Write directly!
	}
}

//result: 1: blocking the queue from being processed because we're not ready to process yet.
byte Tseng4k_blockQueueAccelerator()
{
	if (et34k(getActiveVGA())->W32_acceleratorleft && et34k(getActiveVGA())->W32_ACLregs.ACL_active) //Anything left to process? This is cleared by the noqueue accelerator stepping!
	{
		return 1; //Blocking the queue, because we're not ready to process yet!
	}
	if (Tseng4k_status_multiqueueFilled() == 4) //Queue filled for the ACL registers?
	{
		return 0; //Never blocking on the ACL register queue: always writable!
	}
	//It's normal accelerator window data while it's idling!
	if (et34k(getActiveVGA())->W32_ACLregs.ACL_active == 0) //Not ready to handle the queue?
	{
		return 2; //Blocking the queue, but not termination!
	}
	if ((et34k(getActiveVGA())->W32_MMUregisters[1][0x9C] & 7) == 0) //CPU data isn't used?
	{
		if (et34k(getActiveVGA())->W32_MMUsuspendterminatefilled) //Suspend/terminate pending?
		{
			return 2; //Blocking the queue, but not termination!
		}
		if (et34k(getActiveVGA())->W32_acceleratorleft == 0) //Nothing is processing? We're idling!
		{
			et34k(getActiveVGA())->W32_ACLregs.ACL_active = 0; //ACL is inactive because we're not processing! Count any inputs to the queue as a start of a new operation!
			et34k(getActiveVGA())->W32_ACLregs.XYSTtriggersstart = 1; //XYST triggers a start!
		}
		return 1; //Always blocking the queue!
	}
	if (((et34k(getActiveVGA())->W32_MMUregisters[1][0x9C] & 7) == 4) || ((et34k(getActiveVGA())->W32_MMUregisters[1][0x9C] & 7) == 5)) //X/Y Count?
	{
		return 1; //Always blocking the queue!
	}
	return 0; //Not blocking the queue from being processed (not ready yet)?
}

byte et4k_readlinearVRAM(uint_32 addr)
{
	return readVRAMplane(getActiveVGA(),(addr&3),(addr>>2),0,0); //Read VRAM!
}

void et4k_writelinearVRAM(uint_32 addr, byte value)
{
	writeVRAMplane(getActiveVGA(),(addr&3),(addr>>2),0,value,0); //Write VRAM!
}

void et4k_dowrappatternsourceyinc(uint_32 *patternsourcey, uint_32 *patternsourceaddress, byte patternsourcewrapbit6, uint_32 patternsourceaddress_backup, uint_32 patternsourcewrapy, uint_32 patternsourcewrapx, uint_32 yoffset)
{
	++*patternsourcey;
	if (*patternsourcey == patternsourcewrapy)
	{
		*patternsourcey = 0;
		*patternsourceaddress = patternsourceaddress_backup;
	}
}

void et4k_dowrappatternsourceydec(uint_32* patternsourcey, uint_32* patternsourceaddress, byte patternsourcewrapbit6, uint_32 patternsourceaddress_backup, uint_32 patternsourcewrapy, uint_32 patternsourcewrapx, uint_32 yoffset)
{
	--*patternsourcey;
	if ((*patternsourcey == (uint_32)(~0)) && (!patternsourcewrapbit6))
	{
		*patternsourcey = patternsourcewrapy - 1;
		*patternsourceaddress = patternsourceaddress_backup + (yoffset * (patternsourcewrapy - 1));
	}
}

byte et4k_stepy()
{
	++et34k(getActiveVGA())->W32_ACLregs.Yposition;
	et34k(getActiveVGA())->W32_ACLregs.destinationaddress = et34k(getActiveVGA())->W32_ACLregs.destinationaddress_backup; //Make sure that we're jumping from the original!
	if (et34k(getActiveVGA())->W32_ACLregs.XYdirection&2) //Negative Y?
	{
		et34k(getActiveVGA())->W32_ACLregs.destinationaddress -= et34k(getActiveVGA())->W32_ACLregs.destinationYoffset + 1; //Next address!
		et34k(getActiveVGA())->W32_ACLregs.internalpatternaddress -= et34k(getActiveVGA())->W32_ACLregs.patternYoffset + 1; //Next address!
		et34k(getActiveVGA())->W32_ACLregs.internalsourceaddress -= et34k(getActiveVGA())->W32_ACLregs.sourceYoffset + 1; //Next address!
		et4k_dowrappatternsourceydec(
			&et34k(getActiveVGA())->W32_ACLregs.patternmap_y,
			&et34k(getActiveVGA())->W32_ACLregs.internalpatternaddress,
			et34k(getActiveVGA())->W32_ACLregs.patternwrap_bit6,
			et34k(getActiveVGA())->W32_ACLregs.patternmapaddress_backup,
			et34k(getActiveVGA())->W32_ACLregs.patternwrap_y,
			et34k(getActiveVGA())->W32_ACLregs.patternwrap_x,
			(et34k(getActiveVGA())->W32_ACLregs.patternYoffset + 1) //Y offset!
		);
		et4k_dowrappatternsourceydec(
			&et34k(getActiveVGA())->W32_ACLregs.sourcemap_y,
			&et34k(getActiveVGA())->W32_ACLregs.internalsourceaddress,
			et34k(getActiveVGA())->W32_ACLregs.sourcewrap_bit6,
			et34k(getActiveVGA())->W32_ACLregs.sourcemapaddress_backup,
			et34k(getActiveVGA())->W32_ACLregs.sourcewrap_y,
			et34k(getActiveVGA())->W32_ACLregs.sourcewrap_x,
			(et34k(getActiveVGA())->W32_ACLregs.sourceYoffset + 1) //Y offset!
		);
	}
	else //Positive Y?
	{
		et34k(getActiveVGA())->W32_ACLregs.destinationaddress += et34k(getActiveVGA())->W32_ACLregs.destinationYoffset + 1; //Next address!
		et34k(getActiveVGA())->W32_ACLregs.internalpatternaddress += et34k(getActiveVGA())->W32_ACLregs.patternYoffset + 1; //Next address!
		et34k(getActiveVGA())->W32_ACLregs.internalsourceaddress += et34k(getActiveVGA())->W32_ACLregs.sourceYoffset + 1; //Next address!
		et4k_dowrappatternsourceyinc(
			&et34k(getActiveVGA())->W32_ACLregs.patternmap_y,
			&et34k(getActiveVGA())->W32_ACLregs.internalpatternaddress,
			et34k(getActiveVGA())->W32_ACLregs.patternwrap_bit6,
			et34k(getActiveVGA())->W32_ACLregs.patternmapaddress_backup,
			et34k(getActiveVGA())->W32_ACLregs.patternwrap_y,
			et34k(getActiveVGA())->W32_ACLregs.patternwrap_x,
			(et34k(getActiveVGA())->W32_ACLregs.patternYoffset + 1) //Y offset!
		);
		et4k_dowrappatternsourceyinc(
			&et34k(getActiveVGA())->W32_ACLregs.sourcemap_y,
			&et34k(getActiveVGA())->W32_ACLregs.internalsourceaddress,
			et34k(getActiveVGA())->W32_ACLregs.sourcewrap_bit6,
			et34k(getActiveVGA())->W32_ACLregs.sourcemapaddress_backup,
			et34k(getActiveVGA())->W32_ACLregs.sourcewrap_y,
			et34k(getActiveVGA())->W32_ACLregs.sourcewrap_x,
			(et34k(getActiveVGA())->W32_ACLregs.sourceYoffset + 1) //Y offset!
		);
	}
	et34k(getActiveVGA())->W32_ACLregs.destinationaddress_backup = et34k(getActiveVGA())->W32_ACLregs.destinationaddress; //Save the new line on the destination address to jump back to!
	if (et34k(getActiveVGA())->W32_ACLregs.Yposition>et34k(getActiveVGA())->W32_ACLregs.Ycount)
	{
		Tseng4k_status_clearvirtualbus(); //This clears the virtual bus!
		//Leave Y position and addresses alone!
		return 2; //Y count reached!
	}
	return 0; //No overflow!
}

byte et4k_stepx()
{
	byte result;
	++et34k(getActiveVGA())->W32_ACLregs.Xposition;
	if (et34k(getActiveVGA())->W32_ACLregs.XYdirection&1) //Negative X?
	{
		--et34k(getActiveVGA())->W32_ACLregs.destinationaddress; //Next address!
		--et34k(getActiveVGA())->W32_ACLregs.patternmap_x; //Next address!
		--et34k(getActiveVGA())->W32_ACLregs.sourcemap_x; //Next address!
		if ((uint_64)et34k(getActiveVGA())->W32_ACLregs.patternmap_x == ((uint_32)~0)) //X overflow according to wrapping?
			et34k(getActiveVGA())->W32_ACLregs.patternmap_x += (uint_32)(et34k(getActiveVGA())->W32_ACLregs.patternwrap_x + 1); //Wrap it!
		if ((uint_64)et34k(getActiveVGA())->W32_ACLregs.sourcemap_x == ((uint_32)~0)) //X overflow according to wrapping?
			et34k(getActiveVGA())->W32_ACLregs.sourcemap_x += (uint_32)(et34k(getActiveVGA())->W32_ACLregs.sourcewrap_x + 1); //Wrap it!
	}
	else //Positive X?
	{
		++et34k(getActiveVGA())->W32_ACLregs.destinationaddress; //Next address!
		++et34k(getActiveVGA())->W32_ACLregs.patternmap_x; //Next address!
		++et34k(getActiveVGA())->W32_ACLregs.sourcemap_x; //Next address!
		if ((uint_64)et34k(getActiveVGA())->W32_ACLregs.patternmap_x > (uint_64)et34k(getActiveVGA())->W32_ACLregs.patternwrap_x) //X overflow according to wrapping?
			et34k(getActiveVGA())->W32_ACLregs.patternmap_x -= (et34k(getActiveVGA())->W32_ACLregs.patternwrap_x+1); //Wrap it!
		if ((uint_64)et34k(getActiveVGA())->W32_ACLregs.sourcemap_x > (uint_64)et34k(getActiveVGA())->W32_ACLregs.sourcewrap_x) //X overflow according to wrapping?
			et34k(getActiveVGA())->W32_ACLregs.sourcemap_x -= (et34k(getActiveVGA())->W32_ACLregs.sourcewrap_x+1); //Wrap it!
	}
	if (et34k(getActiveVGA())->W32_ACLregs.Xposition>et34k(getActiveVGA())->W32_ACLregs.Xcount)
	{
		//X overflow? Step vertically!
		et34k(getActiveVGA())->W32_ACLregs.Xposition = 0; //Reset
		et34k(getActiveVGA())->W32_mixmapposition = 0; //Reset mix map position!
		et34k(getActiveVGA())->W32_ACLregs.patternmap_x = et34k(getActiveVGA())->W32_ACLregs.patternmap_x_backup; //Restore the backup!
		et34k(getActiveVGA())->W32_ACLregs.sourcemap_x = et34k(getActiveVGA())->W32_ACLregs.sourcemap_x_backup; //Restore the backup!
		et34k(getActiveVGA())->W32_acceleratorleft = 0; //Starting a new operation, so start with new byte data inputs!
		result = 1 | (et4k_stepy()); //Step Y! X count reached!
		if ((result & 2) == 0) //X overflow without Y overflow? Clear virtual bus!
		{
			Tseng4k_status_clearvirtualbus(); //This clears the virtual bus!
		}
		return result; //Give the result!
	}
	return 0; //No overflow!
}

//result: bit0=Set to have handled tick and mark accelerator as busy, bit1=Set to immediately check for termination on the same clock.
byte Tseng4k_tickAccelerator_step(byte autotransfer)
{
	byte operationstart; //Starting a new operation through the MMU window?
	uint_32 queueaddress;
	byte destination,source,pattern,mixmap,ROP,result,operationx;
	word ROPbits;
	byte ROPmaskdestination[2] = {0x55,0xAA};
	byte ROPmasksource[2] = {0x33,0xCC};
	byte ROPmaskpattern[2] = {0x0F,0xF0};
	//noqueue: handle without queue only. Otherwise, ticking an input on the currently loaded queue or no queue processing.
	//acceleratorleft is used to process an queued 8-pixel block from the CPU! In 1:1 ration instead of 1:8 ratio, it's simply set to 1!
	if (likely(et34k(getActiveVGA())->W32_ACLregs.ACL_active == 0)) return 0; //Transfer isn't active? Don't do anything!
	switch (et34k(getActiveVGA())->W32_MMUregisters[1][0x9C] & 7) //What kind of operation is used?
	{
	case 0: //CPU data isn't used!
		//Handling without CPU data now!
		if ((autotransfer==0) || (et34k(getActiveVGA())->W32_acceleratorleft == 0)) return 0; //NOP when a queue version or not processing!		
		break;
	case 1: //CPU data is source data!
		if (autotransfer) //Autotransferring?
		{
			if ((et34k(getActiveVGA())->W32_MMUsuspendterminatefilled & 0x11)) //Suspend or Terminate requested?
			{
				et34k(getActiveVGA())->W32_acceleratorbusy &= ~3; //Finish operation!
				return 2; //Finish up: we're suspending/terminating right now!
			}
			return 0; //NOP when not a queue version and not processing!
		}
		break;
	case 2: //CPU data is mix data!
		if (autotransfer && (et34k(getActiveVGA())->W32_acceleratorleft == 0)) //Autotransfer without data left to process?
		{
			if ((et34k(getActiveVGA())->W32_MMUsuspendterminatefilled & 0x11)) //Suspend or Terminate requested?
			{
				et34k(getActiveVGA())->W32_acceleratorbusy &= ~3; //Finish operation!
				return 2; //Finish up: we're suspending/terminating right now!
			}
			return 0; //NOP when not a queue version and not processing!
		}
		break;
	case 4: //CPU data is X count
		if (autotransfer && (et34k(getActiveVGA())->W32_acceleratorleft == 0)) //Autotransferring?
		{
			if ((et34k(getActiveVGA())->W32_MMUsuspendterminatefilled & 0x11)) //Suspend or Terminate requested?
			{
				et34k(getActiveVGA())->W32_acceleratorbusy &= ~3; //Finish operation!
				return 2; //Finish up: we're suspending/terminating right now!
			}
			return 0; //NOP when not a queue version and not processing!
		}
		break;
	case 5: //CPU data is Y count
		if (autotransfer && (et34k(getActiveVGA())->W32_acceleratorleft == 0)) //Autotransferring?
		{
			if ((et34k(getActiveVGA())->W32_MMUsuspendterminatefilled & 0x11)) //Suspend or Terminate requested?
			{
				et34k(getActiveVGA())->W32_acceleratorbusy &= ~3; //Finish operation!
				return 2; //Finish up: we're suspending/terminating right now!
			}
			return 0; //NOP when not a queue version and not processing!
		}
		break;
	default: //Reserved
		if ((et34k(getActiveVGA())->W32_MMUsuspendterminatefilled & 0x11)) //Suspend or Terminate requested?
		{
			et34k(getActiveVGA())->W32_acceleratorbusy &= ~3; //Finish operation!
			return 2; //Finish up: we're suspending/terminating right now!
		}
		return 2; //Not handled yet! Terminate immediately on the same clock!
		break;
	}

	et34k(getActiveVGA())->W32_acceleratorbusy |= 2; //Busy accelerator!

	if (unlikely(et34k(getActiveVGA())->W32_acceleratorleft == 0)) //Need to start a new block?
	{
		//Triggering a new MMU operation block only when not having data left to process. Don't look at it until we're starting a new block!
		operationstart = et34k(getActiveVGA())->W32_performMMUoperationstart; //Are we triggered through a new operation?
		et34k(getActiveVGA())->W32_performMMUoperationstart = 1; //We're triggering from the MMU always now!
		if ((et34k(getActiveVGA())->W32_MMUsuspendterminatefilled & 0x11)) //Suspend or Terminate requested?
		{
			et34k(getActiveVGA())->W32_acceleratorbusy &= ~3; //Finish operation!
			return 2; //Finish up: we're suspending/terminating right now!
		}
		queueaddress = et34k(getActiveVGA())->W32_virtualbusqueueval_address; //What address was written to?
		switch (et34k(getActiveVGA())->W32_MMUregisters[1][0x9C] & 7) //What kind of operation is used?
		{
		case 0: //CPU data isn't used!
			//Handling without CPU data now!
			et34k(getActiveVGA())->W32_acceleratorleft = 1; //Default: only processing 1!
			break;
		case 1: //CPU data is source data!
			//Only 1 pixel is processed!
			et34k(getActiveVGA())->W32_acceleratorleft = 1; //Default: only processing 1!
			break;
		case 2: //CPU data is mix data!
			et34k(getActiveVGA())->W32_acceleratorleft = 8; //Processing 8 pixels!
			et34k(getActiveVGA())->W32_ACLregs.latchedmixmap = et34k(getActiveVGA())->W32_virtualbusqueueval; //Latch the written value!
			et34k(getActiveVGA())->W32_mixmapposition = 0; //Initialize the mix map position to the first bit!
			queueaddress <<= 3; //Multiply the address by 8!
			break;
		case 4: //CPU data is X count
		case 5: //CPU data is Y count
			//Only 1 pixel is processed!
			et34k(getActiveVGA())->W32_acceleratorleft = 1; //Default: only processing 1!
			//Manually update the X and Y count precalcs to have their proper values!
			break;
		default: //Reserved
			return 2; //Not handled yet! Terminate immediately on the same clock!
			break;
		}
		queueaddress += et34k(getActiveVGA())->W32_virtualbusqueueval_bankaddress; //Apply the bank address for the MMU window accordingly!
		//Since we're starting a new block processing, check for address updates!
		if (
			(
				//((et34k(getActiveVGA())->W32_ACLregs.W32_newXYblock) && ((et34k(getActiveVGA())->W32_MMUregisters[1][0x9C] & 0x30) == 0x00)) || //Load destination address during first write?
				((et34k(getActiveVGA())->W32_MMUregisters[1][0x9C] & 0x30) != 0x00) //Always reload destination address?
			)
			&& (operationstart) //Triggered through the MMU address?
			) //To load the destination address?
		{
			et34k(getActiveVGA())->W32_ACLregs.destinationaddress = queueaddress; //Load the destination address!
		}
		et34k(getActiveVGA())->W32_ACLregs.W32_newXYblock = 0; //Not a new block anymore!
	}

	//We're ready to start handling a pixel. Now, handle the pixel!
	destination = et4k_readlinearVRAM(et34k(getActiveVGA())->W32_ACLregs.destinationaddress); //Read destination!
	source = et4k_readlinearVRAM(et34k(getActiveVGA())->W32_ACLregs.internalsourceaddress + et34k(getActiveVGA())->W32_ACLregs.sourcemap_x);
	pattern = et4k_readlinearVRAM(et34k(getActiveVGA())->W32_ACLregs.internalpatternaddress + et34k(getActiveVGA())->W32_ACLregs.patternmap_x);
	mixmap = 0xFF; //Assumed 1 if not provided by CPU!

	//Apply CPU custom inputs!
	if ((et34k(getActiveVGA())->W32_MMUregisters[1][0x9C] & 7)==2) //Mixmap from CPU?
	{
		mixmap = et34k(getActiveVGA())->W32_ACLregs.latchedmixmap; //The used mixmap instead!
	}
	else if ((et34k(getActiveVGA())->W32_MMUregisters[1][0x9C] & 7)==1) //Source is from CPU?
	{
		source = et34k(getActiveVGA())->W32_virtualbusqueueval; //Latch the written value!
	}

	//Now, determine and apply the Raster Operation!
	operationx = et34k(getActiveVGA())->W32_mixmapposition++; //X position to work on!
	operationx &= 7; //Wrap!

	if (et34k(getActiveVGA())->W32_ACLregs.XYdirection & 1) //ET4000/W32i 2.11.5.3 Data alignment: When the X direction is decreasing(1), the most-significant bit of the mix data is processed first.
	{
		operationx = 7 - operationx; //Start from the MSb instead of the LSb!
	}

	ROP = et34k(getActiveVGA())->W32_ACLregs.BGFG_RasterOperation[((mixmap>>operationx)&1)];
	result = 0; //Initialize the result!
	ROPbits = 0x01; //What bit to process!
	for (;ROPbits<0x100;) //Check all bits!
	{
		if ((ROP&ROPmaskdestination[(destination&1)]&ROPmasksource[(source&1)]&ROPmaskpattern[(pattern&1)])!=0)
		{
			result |= ROPbits; //Set in the result!
		}
		ROPbits <<= 1; //Next bit to check!
		//Shift in the next bit to check!
		destination >>= 1;
		source >>= 1;
		pattern >>= 1;
	}

	//Finally, writeback the result to destination in VRAM!
	et4k_writelinearVRAM(et34k(getActiveVGA())->W32_ACLregs.destinationaddress,result); //Write back!

	//Increase X/Y positions accordingly!
	//Clear et34k(getActiveVGA())->W32_acceleratorbusy on terminal count reached!
	if (et4k_stepx()==3) //X and Y overflow?
	{
		et34k(getActiveVGA())->W32_acceleratorbusy &= ~3; //Finish operation!
		et34k(getActiveVGA())->W32_acceleratorleft = 0; //Nothing left!
		return 2; //Terminated immediately on the same clock!
	}

	//Apply timing remainder calculation
	if (et34k(getActiveVGA())->W32_acceleratorleft) //Anything left ticking?
	{
		--et34k(getActiveVGA())->W32_acceleratorleft; //Ticked one pixel of the current block!
	}
	if (
		(
			((et34k(getActiveVGA())->W32_MMUregisters[1][0x9C] & 7) == 0) //No CPU version?
			|| ((et34k(getActiveVGA())->W32_MMUregisters[1][0x9C] & 7) == 4) //No CPU version?
			|| ((et34k(getActiveVGA())->W32_MMUregisters[1][0x9C] & 7) == 5) //No CPU version?
		)
		&& (et34k(getActiveVGA())->W32_acceleratorleft==0) //Finished current batch?
		)
	{
		if ((et34k(getActiveVGA())->W32_MMUsuspendterminatefilled & 0x11)) //Suspend or Terminate requested?
		{
			et34k(getActiveVGA())->W32_acceleratorbusy &= ~3; //Finish operation!
			return 2; //Finish up: we're suspending/terminating right now!
		}
		et34k(getActiveVGA())->W32_acceleratorleft = 1; //Always more left until finishing! This keeps us running until terminal count!
	}
	return 1|2; //Ticking a transfer! Terminated immediately on the same clock!
}

void Tseng4k_processRegisters_finished()
{
	//What register processing to do for the client when a blit has finished?
	//Move the internal registers back to the readable ones!
	et34k(getActiveVGA())->W32_ACLregs.patternmapaddress = (et34k(getActiveVGA())->W32_ACLregs.internalpatternaddress & 0x3FFFFF); //Internal Pattern address
	et34k(getActiveVGA())->W32_ACLregs.sourcemapaddress = (et34k(getActiveVGA())->W32_ACLregs.internalsourceaddress & 0x3FFFFF); //Internal Pattern address
}

void Tseng4k_tickAccelerator_active()
{
	byte allowreadingqueue;
	byte terminationrequested;
	uint_32 effectiveoffset;
	byte result;
	//For now, just empty the queue, if filled and become idle!
	if (!(et34k(getActiveVGA()) && (getActiveVGA()->enable_SVGA == 1))) return; //Not ET4000/W32? Do nothing!
	if (unlikely(et34k(getActiveVGA())->W32_MMUsuspendterminatefilled)) goto forcehandlesuspendterminateMMU; //Force handle suspend/terminate?
	if (unlikely(Tseng4k_status_multiqueueFilled() == 4)) goto forcehandlesuspendterminateMMU; //Force handle queued MMU memory mapped registers?
	if (et34k(getActiveVGA())->W32_ACLregs.ACL_active == 0) //ACL inactive?
	{
		if (unlikely(Tseng4k_status_multiqueueFilled()==1)) //Queue is filled while inactive?
		{
			if (likely(Tseng4k_status_peekMultiQueue_apply()==1)) //Peeked and read into pre-processing?
			{
				effectiveoffset = et34k(getActiveVGA())->W32_MMUqueueval_address; //The offset!
				if ((et34k(getActiveVGA())->W32_MMUregisters[0][0x9C] & 7) == 2) //Mix data is going to be used?
				{
					effectiveoffset <<= 3; //Shift left by 3 bits!
				}
				effectiveoffset += et34k(getActiveVGA())->W32_MMUqueueval_bankaddress; //The effective base address for the operation!
				//Fully update the destination address in both queued, unqueued and active locations!
				//if ((et34k(getActiveVGA())->W32_MMUregisters[0][0x9C] & 0x30) != 0x00) //Load destination address during first write?
				{
					setTsengLE32(&et34k(getActiveVGA())->W32_MMUregisters[0][0xA0], effectiveoffset); //Load the banked address into the queued destination address?
				}
				et4k_transferQueuedMMURegisters(); //Load the queued MMU registers!
				Tseng4k_decodeAcceleratorRegisters(); //Make sure our internal state is up-to-date!
				Tseng4k_startAccelerator(1); //Starting the accelerator by MMU trigger!
				Tseng4k_status_startXYblock(Tseng4k_accelerator_calcSSO(), 2); //Starting a transfer! Make the accelerator active as a new transfer!
				if ((et34k(getActiveVGA())->W32_MMUregisters[1][0x9C] & 7) == 0) //CPU data isn't used?
				{
					et34k(getActiveVGA())->W32_transferstartedbyMMU = 1; //Started by the MMU!
				}
				else
				{
					et34k(getActiveVGA())->W32_transferstartedbyMMU = 0; //Not started by the MMU!
				}
				if (
					((et34k(getActiveVGA())->W32_MMUregisters[1][0x9C] & 7) == 4) || //X count?
					((et34k(getActiveVGA())->W32_MMUregisters[1][0x9C] & 7) == 5) //Y count?
					) //X/Y count?
				{
					if ((et34k(getActiveVGA())->W32_MMUregisters[1][0x9C] & 7) == 4) //X count?
					{
						et34k(getActiveVGA())->W32_ACLregs.XCountYCountModeOriginal = et34k(getActiveVGA())->W32_MMUregisters[1][0x98]; //Original value in the register when starting the mode!
						et34k(getActiveVGA())->W32_MMUregisters[1][0x98] = et34k(getActiveVGA())->W32_MMUqueueval; //X count low byte (loaded internal as documented)!
						et34k(getActiveVGA())->W32_ACLregs.Xcount = (getTsengLE16(&et34k(getActiveVGA())->W32_MMUregisters[1][0x98]) & 0xFFF); //X count
					}
					else //Y count?
					{
						et34k(getActiveVGA())->W32_ACLregs.XCountYCountModeOriginal = et34k(getActiveVGA())->W32_MMUregisters[1][0x9A]; //Original value in the register when starting the mode!
						et34k(getActiveVGA())->W32_MMUregisters[1][0x9A] = et34k(getActiveVGA())->W32_MMUqueueval; //Y count low byte (loaded internal as documented)!
						et34k(getActiveVGA())->W32_ACLregs.Ycount = (getTsengLE16(&et34k(getActiveVGA())->W32_MMUregisters[1][0x9A]) & 0xFFF); //Y count
					}
					et34k(getActiveVGA())->W32_acceleratorleft = 1; //Always more left until finishing! This keeps us running!
					result = Tseng4k_doEmptyQueue(); //Acnowledge and empty the queue: it's a start trigger instead!
				}
				et34k(getActiveVGA())->W32_mixmapposition = 0; //Initialize the mix map position to the first bit!
				//Initialize the X and Y position to start rendering!
				et34k(getActiveVGA())->W32_ACLregs.Xposition = et34k(getActiveVGA())->W32_ACLregs.Yposition = 0; //Initialize the position!
				if (
					!( //Not triggered by:
					((et34k(getActiveVGA())->W32_MMUregisters[1][0x9C] & 7) == 4) || //X count?
					((et34k(getActiveVGA())->W32_MMUregisters[1][0x9C] & 7) == 5) //Y count?
					)
					) //X/Y count?
				{
					goto forcehandlesuspendterminateMMU; //Force handle the starting of a transfer!
				}
			}
		}
		return; //Transfer isn't active? Don't do anything!
	}
	forcehandlesuspendterminateMMU: //Force handling of suspend/terminate!
	if (unlikely((et34k(getActiveVGA())->W32_MMUsuspendterminatefilled & 0x10) == 0x10)) //Terminate requested?
	{
		et34k(getActiveVGA())->W32_acceleratorbusy = 0; //Not busy anymore!
	}
	if (likely((result = Tseng4k_tickAccelerator_step(1))!=0)) //No queue version of ticking the accelerator?
	{
		Tseng4k_encodeAcceleratorRegisters(); //Encode the registers, updating them for the CPU!
		et34k(getActiveVGA())->W32_acceleratorbusy |= (result&1); //Become a busy accelerator!
		if ((result & 2) == 0) //Keep ticking?
		{
			return; //Abort!
		}
	}
	else if ((result = Tseng4k_blockQueueAccelerator())!=0) //Not ready to process the queue yet?
	{
		if (result == 1) //Fully block?
		{
			return; //Not ready to process yet!
		}
		//Otherwise, it's 2, requesting suspend/terminate!
	}
	else //Processing queues?
	{
		allowreadingqueue = 1; //Default: allow reading the queue!
		if (Tseng4k_status_multiqueueFilled() == 1) //Filled with data to process?
		{
			if (Tseng4k_status_virtualbusfull()) //Is the virtual bus queue full?
			{
				allowreadingqueue = 0; //Don't allow reading the queue: the virtual bus is full (prevent data loss)!
			}
		}
		if (allowreadingqueue) //Do we allow reading the queue?
		{
			if (likely((result = Tseng4k_doEmptyQueue()) != 0)) //Try and perform an emptying of the queue, if it's filled (act like it's processed into the accelerator)!
			{
				if (result == 4) //Transfer to the ACL register queue instead?
				{
					Tseng4k_writeMMUregisterUnqueued(et34k(getActiveVGA())->W32_MMUqueueval_address, et34k(getActiveVGA())->W32_MMUqueueval); //Writing from the queue to the ACL register!
					if (!et34k(getActiveVGA())->W32_acceleratorbusy) //Accelerator not busy on anything?
					{
						Tseng4k_doBecomeIdle(); //The accelerator becomes idle now (no input anymore)! We're waiting for input!
					}
				}
				else //Handling an input on the accelerator?
				{
					result = Tseng4k_status_writeVirtualBusMultiQueue(result, et34k(getActiveVGA())->W32_MMUqueueval, et34k(getActiveVGA())->W32_MMUqueueval_address, et34k(getActiveVGA())->W32_MMUqueueval_bankaddress); //Added to the virtual queue?
				}
			}
		}

		//First, check starting a new fill of the virtual bus count to become active!
		if (et34k(getActiveVGA())->W32_VirtualBusCountLeft == 0) //Nothing processing?
		{
			if (Tseng4k_status_virtualbusentries() >= et34k(getActiveVGA())->W32_ACLregs.virtualbussizecount) //Enough entried filled to clear the virtual bus out?
			{
				et34k(getActiveVGA())->W32_VirtualBusCountLeft = et34k(getActiveVGA())->W32_ACLregs.virtualbussizecount; //Start processing the virtual queue into the accelerator!
			}
		}

		//Finally, process the virtual bus queue into the active accelerator!
		if (et34k(getActiveVGA())->W32_VirtualBusCountLeft) //Something left to process on the virtual bus?
		{
			--et34k(getActiveVGA())->W32_VirtualBusCountLeft; //One processed!
			if (Tseng4k_status_readVirtualBusMultiQueue()) //Read the virtual bus multi queue! This should always succeed!
			{
				if ((result = Tseng4k_tickAccelerator_step(0)) != 0) //Queue version of ticking the accelerator?
				{
					Tseng4k_encodeAcceleratorRegisters(); //Encode the registers, updating them for the CPU!
					et34k(getActiveVGA())->W32_acceleratorbusy |= (result & 1); //Become a busy accelerator!
					//Tick the accelerator with the specified address and value loaded!
					//Latch the value written if a valid address that's requested!
					if ((result & 2) == 0) //Keep ticking?
					{
						return; //Wait for the next tick to finish the accelerator! Abort!
					}
				}
			}
		}
		else //Idle and queue is empty?
		{
			Tseng4k_doBecomeIdle(); //The accelerator becomes idle now! We're waiting for input!
		}
	}

	if (unlikely((((et34k(getActiveVGA())->W32_acceleratorbusy&2)==0) && (Tseng4k_status_virtualbusmultiqueueFilled()==0)) || (et34k(getActiveVGA())->W32_MMUsuspendterminatefilled && ((et34k(getActiveVGA())->W32_acceleratorbusy&3)==0)))) //Accelerator was busy or suspending/terminating while allowed to?
	{
		if (unlikely(et34k(getActiveVGA())->W32_MMUsuspendterminatefilled & 0x11)) //Suspend or Terminate requested?
		{
			if (likely((et34k(getActiveVGA())->W32_acceleratorbusy & 2) == 0)) //Terminated?
			{
				et34k(getActiveVGA())->W32_acceleratorbusy = 0; //Not busy anymore!
				et34k(getActiveVGA())->W32_ACLregs.ACL_active = 0; //ACL is inactive!
				if ((et34k(getActiveVGA())->W32_MMUsuspendterminatefilled & 0x10) == 0x00) //Suspend requested without terminate?
				{
					Tseng4k_status_clearvirtualbus(); //Clear the virtual bus!
					Tseng4k_status_acceleratorsuspended(); //Accelerator has been suspended!
					//XYST isn't cleared when already set during a transfer, to properly resume when resumed?
					//Tseng4k_status_XYblockTerminalCount(); //Terminal count reached during the tranfer!
					et4k_emptyqueuedummy = Tseng4k_doEmptyQueue(); //Empty the queue if possible for the new operation to start! Since interrupts are disabled, doesn't trigger an IRQ!
					Tseng4k_encodeAcceleratorRegisters(); //Encode the registers, updating them for the CPU!
				}
				terminationrequested = et34k(getActiveVGA())->W32_MMUsuspendterminatefilled; //Was termination requested?
				Tseng4k_status_suspendterminatefinished(); //Suspend/terminate finished.
				et34k(getActiveVGA())->W32_ACLregs.XYSTtriggersstart = 0; //XYST doesn't trigger a start!
				if ((terminationrequested & 0x10) == 0x10) //Terminate requested?
				{
					Tseng4k_status_clearvirtualbus(); //Clear the virtual bus!
					//Documentation says that all ACL registers are returned to a state they were during the reset of the chip.
					//All known registers are cleared by this command, returning to power-up state!
					memset(&et34k(getActiveVGA())->W32_MMUregisters[1][0x80], 0, (sizeof(et34k(getActiveVGA())->W32_MMUregisters[1][0]) * 0x80)); //Clear the internal registers!
					memset(&et34k(getActiveVGA())->W32_MMUregisters[0][0x80], 0, (sizeof(et34k(getActiveVGA())->W32_MMUregisters[0][0]) * 0x80)); //Clear the queue itself!
					//memset(&et34k(getActiveVGA())->W32_MMUregisters[0][0], 0, 0x14); //Clear the MMU base registers!
					SETBITS(et34k(getActiveVGA())->W32_MMUregisters[0][0x36], 5, 0x7, 0); //Clear the reserved bits of the status register!
					Tseng4k_status_XYblockTerminalCount(); //Terminal count reached during the tranfer!
					Tseng4k_queuedRegistersUnmodified(); //The queue is unmodified now!
					et34k(getActiveVGA())->W32_ACLregs.internalpatternaddress = et34k(getActiveVGA())->W32_ACLregs.internalsourceaddress = 0; //Reset the internal adresses (officially: undefined on power-up state)!
					Tseng4k_decodeAcceleratorRegisters(); //Make sure that we're up-to-date with our internal registers!
					//Leave register 30h bit 4 untouched, this is to be done by software itself?
					et34k(getActiveVGA())->W32_MMUregisters[0][0x30] &= 0x10; //Cleared register by the reset! Only leave the terminate bit left for the software to clear?
					et34k(getActiveVGA())->W32_MMUregisters[0][0x31] = 0; //Cleared register by the reset!
					et34k(getActiveVGA())->W32_MMUregisters[0][0x32] = 0; //Init register by the reset! Sync disable?
					et34k(getActiveVGA())->W32_MMUregisters[0][0x34] = 0; //Cleared register by the reset! No interrupts enabled!
					VGA_calcprecalcs(getActiveVGA(), WHEREUPDATED_MEMORYMAPPED | 0x13); //The memory mapped registers has been updated!
					et4k_emptyqueuedummy = Tseng4k_doEmptyQueue(); //Empty the queue if possible for the new operation to start! Since interrupts are disabled, doesn't trigger an IRQ!
					Tseng4k_doBecomeIdle(); //Accelerator becomes idle now!
					Tseng4k_writeMMUregisterUnqueued(0x35, 0x7); //Clear interrupts from the cause of a write error only? Others are interpreted by the CPU itself for a new operation to start! Since documentation says 'reset of the chip', I assume it clears the other interrupts as well.
					Tseng4k_encodeAcceleratorRegisters(); //Make sure that we're up-to-date with our internal registers!
				}
				else //Become idle only!
				{
					Tseng4k_doBecomeIdle(); //The accelerator becomes idle now! We're waiting for input!
				}
			}
		}
		//Tick the accelerator with the specified address and value loaded!
		//Abort if still processing! Otherwise, finish up below:
		if (unlikely(((et34k(getActiveVGA())->W32_acceleratorbusy & 2) == 0) && et34k(getActiveVGA())->W32_ACLregs.ACL_active)) //Terminated a running tranafer?
		{
			Tseng4k_status_clearvirtualbus(); //Clear the virtual bus!
			et34k(getActiveVGA())->W32_ACLregs.ACL_active = 0; //ACL is inactive!
			et34k(getActiveVGA())->W32_ACLregs.XYSTtriggersstart = 1; //XYST triggers a start!
			Tseng4k_status_XYblockTerminalCount(); //Terminal count reached during the tranfer!
			Tseng4k_doBecomeIdle(); //Accelerator becomes idle now!
			Tseng4k_processRegisters_finished();
			Tseng4k_encodeAcceleratorRegisters(); //Encode the registers, updating them for the CPU!
		}
	}
	Tseng4k_checkAcceleratorActivity(); //Check for new activity status!
}

void Tseng4k_tickAccelerator()
{
	if (likely(!getActiveVGA()->precalcs.Tseng4k_accelerator_tickhandler)) return; //Not ticking something? Abort!
	getActiveVGA()->precalcs.Tseng4k_accelerator_tickhandler(); //Perform the current action of the accelerator!
}

void Tseng4k_checkAcceleratorActivity()
{
	getActiveVGA()->precalcs.Tseng4k_accelerator_tickhandler = (Handler)0; //Default: inactive handler!
	if (!(et34k(getActiveVGA()) && (getActiveVGA()->enable_SVGA == 1))) return; //Not ET4000/W32? Do nothing!
	if (unlikely(et34k(getActiveVGA())->W32_MMUsuspendterminatefilled)) goto startTicking; //Force handle suspend/terminate?
	if (unlikely(Tseng4k_status_multiqueueFilled() == 4)) goto startTicking; //Force handle queued MMU memory mapped registers?
	if (et34k(getActiveVGA())->W32_ACLregs.ACL_active == 0) //ACL inactive?
	{
		if (unlikely(Tseng4k_status_multiqueueFilled() == 1)) //Queue is filled while inactive?
		{
		startTicking:
			getActiveVGA()->precalcs.Tseng4k_accelerator_tickhandler = &Tseng4k_tickAccelerator_active; //Become active!
		}
	}
	else goto startTicking; //Active accelerator to tick!
}

extern byte VGA_WriteMemoryMode, VGA_ReadMemoryMode; //Write/read memory modes used for accessing VRAM!
//ET4K precalcs updating functionality.
void Tseng34k_calcPrecalcs(void *useVGA, uint_32 whereupdated)
{
	VGA_Type *VGA = (VGA_Type *)useVGA; //The VGA to work on!
	SVGA_ET34K_DATA *et34kdata = et34k(VGA); //The et4k data!
	byte updateCRTC = 0; //CRTC updated?
	byte horizontaltimingsupdated = 0; //Horizontal timings are updated?
	byte verticaltimingsupdated = 0; //Vertical timings are updated?
	byte et34k_tempreg;
	word DACmode; //Current/new DAC mode!
	byte newcharwidth, newtextwidth; //Change detection!
	byte newfontwidth; //Change detection!
	byte pixelboost; //Actual pixel boost!
	byte possibleboost; //Possible value!
	uint_32 tempdata; //Saved data!
	byte tempval;
	int colorval;
	if (!et34k(VGA)) return; //No extension registered?

	byte FullUpdate = (whereupdated == 0); //Fully updated?
	byte charwidthupdated = ((whereupdated == (WHEREUPDATED_SEQUENCER | 0x01)) || FullUpdate || VGA->precalcs.charwidthupdated); //Sequencer register updated?
	byte CRTUpdated = UPDATE_SECTIONFULL(whereupdated, WHEREUPDATED_CRTCONTROLLER, FullUpdate); //Fully updated?
	byte CRTUpdatedCharwidth = CRTUpdated || charwidthupdated; //Character width has been updated, for following registers using those?
	byte AttrUpdated = UPDATE_SECTIONFULL(whereupdated,WHEREUPDATED_ATTRIBUTECONTROLLER,FullUpdate); //Fully updated?
	byte SequencerUpdated = UPDATE_SECTIONFULL(whereupdated, WHEREUPDATED_SEQUENCER, FullUpdate); //Fully updated?
	byte linearmodeupdated = 0; //Linear mode has been updated?
	byte SpriteCRTCBenabledupdated = 0; //Sprite/CRTCB enable updated?

	#ifdef LOG_UNHANDLED_SVGA_ACCESSES
	byte handled = 0;
	#endif

	if ((whereupdated==WHEREUPDATED_ALL) || (whereupdated==(WHEREUPDATED_SEQUENCER|0x7))) //TS Auxiliary Mode updated?
	{
		#ifdef LOG_UNHANDLED_SVGA_ACCESSES
		handled = 1;
		#endif
		et34k_reg(et34kdata,3c4,07) |= 0x04; //Always set!
		if (VGA->enable_SVGA==1) //ET4000?
		{
			et34k_reg(et34kdata,3c4,07) |= 0x10; //ET4000 rev E always sets this bit!
		}
		et34k_tempreg = et34k_reg(et34kdata,3c4,07); //The TS Auxiliary mode to apply!
		if (et34k_tempreg&0x1) //MCLK/4?
		{
			VGA->precalcs.MemoryClockDivide = 2; //Divide by 4!
		}
		else if (et34k_tempreg&0x40) //MCLK/2?
		{
			VGA->precalcs.MemoryClockDivide = 1; //Divide by 2!
		}
		else //Normal 1:1 MCLK!
		{
			VGA->precalcs.MemoryClockDivide = 0; //Do not divide!
		}
		VGAROM_mapping = ((et34k_tempreg&8)>>2)|((et34k_tempreg&0x20)>>5); //Bit 3 is the high bit, Bit 5 is the low bit!
	}

	//Bits 4-5 of the Attribute Controller register 0x16(Miscellaneous) determine the mode to be used when decoding pixels:
	/*
	00=Normal power-up/default(VGA mode)
	01=Reserved
	10=High-resolution mode (up to 256 colors)
	11=High-color 16-bits/pixel
	*/

	if (AttrUpdated || (whereupdated == WHEREUPDATED_ALL) || (whereupdated == (WHEREUPDATED_ATTRIBUTECONTROLLER|0x16)) || (whereupdated == (WHEREUPDATED_ATTRIBUTECONTROLLER | 0x10)) || (whereupdated == (WHEREUPDATED_SEQUENCER | 0x7))) //Attribute misc. register?
	{
		#ifdef LOG_UNHANDLED_SVGA_ACCESSES
		handled = 1;
		#endif
		et34k_tempreg = et34k_reg(et34kdata,3c0,16); //The mode to use when decoding!

		VGA->precalcs.BypassPalette = (et34k_tempreg&0x80)?1:0; //Bypass the palette if specified!
		et34kdata->protect3C0_Overscan = (et34k_tempreg&0x01)?1:0; //Protect overscan if specified!
		et34kdata->protect3C0_PaletteRAM = (et34k_tempreg&0x02)?1:0; //Protect Internal/External Palette RAM if specified!
		horizontaltimingsupdated = (et34kdata->doublehorizontaltimings != (((et34k_tempreg&0x10) && (VGA->enable_SVGA==2))?1:0)); //Horizontal timings double has been changed?
		et34kdata->doublehorizontaltimings = (((et34k_tempreg & 0x10) && (VGA->enable_SVGA == 2))?1:0); //Double the horizontal timings?
		VGA->precalcs.charactercode_16bit = ((et34k_tempreg & 0x40) >> 6); //The new character code size!

		if (et34k(VGA)->tsengExtensions) //W32 chip?
		{
			et34k_tempreg >>= 5; //Only 0 and 2 are defined!
			et34k_tempreg &= 1; //The high resolution bit only is the 16-bit setting instead!
			et34k_tempreg |= (et34k_tempreg << 1); //Duplicate bit 1 to bit 2 to obtain either 8-bit or 16-bit modes only!
		}
		else //Tseng chip?
		{
			et34k_tempreg >>= 4; //Shift to our position!
			et34k_tempreg &= 3; //Only 2 bits are used for detection!
		}
		if (VGA->enable_SVGA==2) et34k_tempreg = 0; //Unused on the ET3000! Force default mode!
		//Manual says: 00b=Normal power-up default, 01b=High-resolution mode(up to 256 colors), 10b=Reserved, 11b=High-color 16-bit/pixel
		if (et34k(VGA)->tsengExtensions == 0) //No W32 chip?
		{
			if (et34k_tempreg == 2) //The third value is illegal(reserved in the manual)!
			{
				et34k_tempreg = 0; //Ignore the reserved value, forcing VGA mode in that case!
			}
			if ((et34k_reg(et34kdata, 3c4, 07) & 2) == 0) //SCLK not divided? Then we're in normal mode!
			{
				et34k_tempreg = 0; //Ignore the reserved value, forcing VGA mode in that case!
			}
		}
		//W32: Sequencer index 07 replicates pixels horizontally. bits 2,4=0:x8,1=x4,2=x2,3=x8. Is this correct (according to WhatVGA)?
		VGA->precalcs.AttributeController_16bitDAC = et34k_tempreg; //Set the new mode to use (mode 2/3 or 0)!
		//Modes 2&3 set forced 8-bit and 16-bit Attribute modes!
		updateVGAAttributeController_Mode(VGA); //Update the attribute controller mode, which might have changed!
		updateVGAGraphics_Mode(VGA);
	}

	if (AttrUpdated || (whereupdated==(WHEREUPDATED_ATTRIBUTECONTROLLER|0x13)) //Updated horizontal panning?
			|| (whereupdated == (WHEREUPDATED_GRAPHICSCONTROLLER | 0x06)) //Updated text mode?
			|| charwidthupdated //Char width updated?
			) //Horizontal pixel panning is to be updated?
	{
		#ifdef LOG_UNHANDLED_SVGA_ACCESSES
		handled = 1;
		#endif
		//Precalculate horizontal pixel panning:
		pixelboost = 0; //Actual pixel boost!
		possibleboost = GETBITS(VGA->registers->AttributeControllerRegisters.REGISTERS.HORIZONTALPIXELPANNINGREGISTER,0,0xF); //Possible value, to be determined!
		if ((GETBITS(VGA->registers->SequencerRegisters.REGISTERS.CLOCKINGMODEREGISTER, 0, 1) == 0) && (VGA->precalcs.graphicsmode)) //Different behaviour with 9 pixel modes?
		{
			if (possibleboost >= 8) //No shift?
			{
				possibleboost = 0; //No shift!
			}
			else //Less than 8?
			{
				++possibleboost; //1 more!
			}
		}
		else //Only 3 bits?
		{
			possibleboost &= 0x7; //Repeat the low values!
		}
		pixelboost = possibleboost; //Enable normally!
		VGA->precalcs.recalcScanline |= (VGA->precalcs.pixelshiftcount!=pixelboost); //Recalc scanline data when needed!
		VGA->precalcs.pixelshiftcount = pixelboost; //Save our precalculated value!
	}

	//ET3000/ET4000 Start address register
	if (CRTUpdated || horizontaltimingsupdated || (whereupdated == WHEREUPDATED_ALL) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER|0x33)) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x23)) || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0xC)) || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0xD))) //Extended start address?
	{
		#ifdef LOG_UNHANDLED_SVGA_ACCESSES
		handled = 1;
		#endif
		VGA->precalcs.startaddress = (((VGA->precalcs.VGAstartaddress+et34k(VGA)->display_start_high))<<et34kdata->doublehorizontaltimings); //Double the horizontal timings if needed!
	}

	//ET3000/ET4000 Cursor Location register
	if (CRTUpdated || (whereupdated == WHEREUPDATED_ALL) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x33)) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x23)) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0xE)) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0xF))) //Extended cursor location?
	{
		#ifdef LOG_UNHANDLED_SVGA_ACCESSES
		handled = 1;
		#endif
		VGA->precalcs.cursorlocation = (VGA->precalcs.cursorlocation & 0xFFFF) | et34k(VGA)->cursor_start_high;
	}

	//ET3000/ET4000 Vertical Overflow register!
	if (VGA->enable_SVGA == 1) //ET4000?
	{
		et34k_tempreg = et4k_reg(et34kdata,3d4,35); //The overflow register!
	}
	else //ET3000?
	{
		et34k_tempreg = et3k_reg(et34kdata,3d4,25); //The overflow register!
	}

	verticaltimingsupdated = 0; //Default: not updated!
	if (CRTUpdated || (whereupdated == WHEREUPDATED_ALL) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x35)) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x25))) //Interlacing?
	{
		#ifdef LOG_UNHANDLED_SVGA_ACCESSES
		handled = 1;
		#endif
		verticaltimingsupdated |= (et34kdata->useInterlacing != ((et34k_tempreg & 0x80) ? 1 : 0)); //Interlace has changed?
		et34kdata->useInterlacing = VGA->precalcs.enableInterlacing = (VGA->enable_SVGA==2)?((et34k_tempreg & 0x80) ? 1 : 0):0; //Enable/disable interlacing! Apply with ET3000 only!

		//W32 chips: bit 6=Source of Vertical Retrace interrupt. 0=VGA-compatible, 1=CRTC/Sprite registers based.
		if ((et34k_tempreg & 0x40) && (et34kdata->tsengExtensions)) //W32-chip and CRTCB/Sprite register interrupt enabled?
		{
			VGA->precalcs.VerticalRetraceInterruptSource = 1; //CRTCB/Sprite is the interrupt source!
		}
		else //Normal VGA-compatible interrupts!
		{
			VGA->precalcs.VerticalRetraceInterruptSource = 0; //Vertical retrace is the interrupt source!
		}
	}

	if (CRTUpdated || verticaltimingsupdated || (whereupdated == WHEREUPDATED_ALL) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x35)) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x25)) //Extended bits of the overflow register!
		|| (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0x7)) || //Overflow register itself
		//Finally, bits needed by the overflow register itself(of which we are an extension)!
		(whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x12)) //Vertical display end
		) //Extended bits of the overflow register!
	{
		#ifdef LOG_UNHANDLED_SVGA_ACCESSES
		handled = 1;
		#endif
		//bit2=Vertical display end bit 10
		tempdata = GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.OVERFLOWREGISTER,6,1);
		tempdata <<= 1;
		tempdata |= GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.OVERFLOWREGISTER,1,1);
		tempdata <<= 8;
		tempdata |= VGA->registers->CRTControllerRegisters.REGISTERS.VERTICALDISPLAYENDREGISTER;
		tempdata = ((et34k_tempreg & 4) << 9) | (tempdata & 0x3FF); //Add/replace the new/changed bits!
		tempdata <<= et34kdata->useInterlacing; //Interlacing doubles vertical resolution!
		++tempdata; //One later!
		updateCRTC |= (VGA->precalcs.verticaldisplayend!=tempdata); //To be updated?
		VGA->precalcs.verticaldisplayend = tempdata; //Save the new data!
	}

	if (CRTUpdated || verticaltimingsupdated || (whereupdated == WHEREUPDATED_ALL) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x35)) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x25)) //Extended bits of the overflow register!
		|| (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x7)) || //Overflow register itself
		//Finally, bits needed by the overflow register itself(of which we are an extension)!
		(whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x15)) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x9)) //Vertical blanking start
		)
	{
		#ifdef LOG_UNHANDLED_SVGA_ACCESSES
		handled = 1;
		#endif
		//bit0=Vertical blank bit 10
		tempdata = GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.MAXIMUMSCANLINEREGISTER,5,1);
		tempdata <<= 1;
		tempdata |= GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.OVERFLOWREGISTER,3,1);
		tempdata <<= 8;
		tempdata |= VGA->registers->CRTControllerRegisters.REGISTERS.STARTVERTICALBLANKINGREGISTER;
		tempdata = ((et34k_tempreg & 1) << 10) | (tempdata & 0x3FF); //Add/replace the new/changed bits!
		tempdata <<= et34kdata->useInterlacing; //Interlacing doubles vertical resolution!
		updateCRTC |= (VGA->precalcs.verticalblankingstart!=tempdata); //To be updated?
		VGA->precalcs.verticalblankingstart = tempdata; //Save the new data!
	}

	if (CRTUpdated || verticaltimingsupdated || (whereupdated == WHEREUPDATED_ALL) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x35)) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x25)) //Extended bits of the overflow register!
		|| (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x7)) || //Overflow register itself
		//Finally, bits needed by the overflow register itself(of which we are an extension)!
		(whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x10)) //Vertical retrace start
		)
	{
		#ifdef LOG_UNHANDLED_SVGA_ACCESSES
		handled = 1;
		#endif
		//bit3=Vertical sync start bit 10
		tempdata = GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.OVERFLOWREGISTER,7,1);
		tempdata <<= 1;
		tempdata |= GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.OVERFLOWREGISTER,2,1);
		tempdata <<= 8;
		tempdata |= VGA->registers->CRTControllerRegisters.REGISTERS.VERTICALRETRACESTARTREGISTER;
		tempdata = ((et34k_tempreg & 8) << 7) | (tempdata & 0x3FF); //Add/replace the new/changed bits!
		tempdata <<= et34kdata->useInterlacing; //Interlacing doubles vertical resolution!
		updateCRTC |= (VGA->precalcs.verticalretracestart!=tempdata); //To be updated?
		VGA->precalcs.verticalretracestart = tempdata; //Save the new data!
	}

	if (CRTUpdated || verticaltimingsupdated || (whereupdated == WHEREUPDATED_ALL) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x35)) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x25)) //Extended bits of the overflow register!
		|| (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x7)) || //Overflow register itself
		//Finally, bits needed by the overflow register itself(of which we are an extension)!
		(whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x6)) //Vertical total
		)
	{
		#ifdef LOG_UNHANDLED_SVGA_ACCESSES
		handled = 1;
		#endif
		//bit1=Vertical total bit 10
		tempdata = GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.OVERFLOWREGISTER,5,1);
		tempdata <<= 1;
		tempdata |= GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.OVERFLOWREGISTER,0,1);
		tempdata <<= 8;
		tempdata |= VGA->registers->CRTControllerRegisters.REGISTERS.VERTICALTOTALREGISTER;
		tempdata = ((et34k_tempreg & 2) << 9) | (tempdata & 0x3FF); //Add/replace the new/changed bits!
		tempdata <<= et34kdata->useInterlacing; //Interlacing doubles vertical resolution!
		++tempdata; //One later!
		updateCRTC |= (VGA->precalcs.verticaltotal!=tempdata); //To be updated?
		VGA->precalcs.verticaltotal = tempdata; //Save the new data!
	}

	if (CRTUpdated || verticaltimingsupdated || (whereupdated == WHEREUPDATED_ALL) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x35)) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x25)) //Extended bits of the overflow register!
		|| (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x7)) || //Overflow register itself
		//Finally, bits needed by the overflow register itself(of which we are an extension)!
		(whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x18)) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x9)) //Line compare
		)
	{
		#ifdef LOG_UNHANDLED_SVGA_ACCESSES
		handled = 1;
		#endif
		//bit4=Line compare bit 10
		tempdata = GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.MAXIMUMSCANLINEREGISTER,6,1);
		tempdata <<= 1;
		tempdata |= GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.OVERFLOWREGISTER,4,1);
		tempdata <<= 8;
		tempdata |= VGA->registers->CRTControllerRegisters.REGISTERS.LINECOMPAREREGISTER;
		tempdata = ((et34k_tempreg & 0x10) << 6) | (tempdata & 0x3FF); //Add/replace the new/changed bits!
		tempdata <<= et34kdata->useInterlacing; //Interlacing doubles vertical resolution!
		++tempdata; //One later!
		updateCRTC |= (VGA->precalcs.topwindowstart!=tempdata); //To be updated?
		VGA->precalcs.topwindowstart = tempdata; //Save the new data!
	}

	//ET4000 horizontal overflow timings!
	et34k_tempreg = et4k_reg(et34kdata, 3d4, 3f); //The overflow register!
	if (VGA->enable_SVGA!=1) et34k_tempreg = 0; //Disable the register with ET3000(always zeroed)!

	if (CRTUpdated || horizontaltimingsupdated || CRTUpdatedCharwidth || (whereupdated == WHEREUPDATED_ALL) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x3F)) //Extended bits of the overflow register!
		//Finally, bits needed by the overflow register itself(of which we are an extension)!
		|| ((whereupdated == WHEREUPDATED_ALL) || (whereupdated == (WHEREUPDATED_SEQUENCER | 0x07))) //VGA/EGA mode updated?
		|| (whereupdated == WHEREUPDATED_CRTCONTROLLER) //Horizontal total
		)
	{
		#ifdef LOG_UNHANDLED_SVGA_ACCESSES
		handled = 1;
		#endif
		//bit0=Horizontal total bit 8
		tempdata = VGA->registers->CRTControllerRegisters.REGISTERS.HORIZONTALTOTALREGISTER;
		tempdata |= ((et34k_tempreg & 1) << 8); //To be updated?
		tempdata += (et34k_reg(et34kdata, 3c4, 07)&0x80)?5:2; //Actually five clocks more on VGA mode! Only two clocks on EGA mode!
		tempdata *= VGA->precalcs.characterwidth; //We're character units!
		tempdata <<= et34kdata->doublehorizontaltimings; //Double the horizontal timings if needed!
		updateCRTC |= (VGA->precalcs.horizontaltotal != tempdata); //To be updated?
		VGA->precalcs.horizontaltotal = tempdata; //Save the new data!
	}
	
	if (CRTUpdated || horizontaltimingsupdated || CRTUpdatedCharwidth || (whereupdated==WHEREUPDATED_ALL) || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0x01)) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x3F))) //End horizontal display updated?
	{
		#ifdef LOG_UNHANDLED_SVGA_ACCESSES
		handled = 1;
		#endif
		tempdata = VGA->registers->CRTControllerRegisters.REGISTERS.ENDHORIZONTALDISPLAYREGISTER;
		++tempdata; //Stop after this character!
		tempdata *= VGA->precalcs.characterwidth; //Original!
		tempdata <<= et34kdata->doublehorizontaltimings; //Double the horizontal timings if needed!
		if (VGA->precalcs.horizontaldisplayend != tempdata) adjustVGASpeed(); //Update our speed!
		updateCRTC |= (VGA->precalcs.horizontaldisplayend != tempdata); //Update!
		VGA->precalcs.horizontaldisplayend = tempdata; //Load!
	}

	if (CRTUpdated || horizontaltimingsupdated || CRTUpdatedCharwidth || (whereupdated == WHEREUPDATED_ALL) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x3F)) //Extended bits of the overflow register!
		//Finally, bits needed by the overflow register itself(of which we are an extension)!
		|| (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x2)) //Horizontal blank start
		)
	{
		#ifdef LOG_UNHANDLED_SVGA_ACCESSES
		handled = 1;
		#endif

		word hblankstart;
		//bit2=Horizontal blanking bit 8
		hblankstart = VGA->registers->CRTControllerRegisters.REGISTERS.STARTHORIZONTALBLANKINGREGISTER;
		hblankstart |= ((et34k_tempreg & 4) << 6); //Add/replace the new/changed bits!
		++hblankstart; //Start after this character!
		VGA->precalcs.horizontalblankingstartfinish = hblankstart;
		hblankstart *= VGA->precalcs.characterwidth;
		hblankstart <<= et34kdata->doublehorizontaltimings; //Double the horizontal timings if needed!
		if (VGA->precalcs.horizontalblankingstart != hblankstart) adjustVGASpeed(); //Update our speed!
		updateCRTC |= (VGA->precalcs.horizontalblankingstart != hblankstart); //Update!
		VGA->precalcs.horizontalblankingstart = hblankstart; //Load!
		hblankstart = VGA->precalcs.horizontalblankingstartfinish;
		++hblankstart; //End after this character!
		hblankstart *= VGA->precalcs.characterwidth;
		hblankstart <<= et34kdata->doublehorizontaltimings; //Double the horizontal timings if needed!
		VGA->precalcs.horizontalblankingstartfinish = hblankstart; //Load!
	}

	if (CRTUpdated || horizontaltimingsupdated || CRTUpdatedCharwidth || (whereupdated == WHEREUPDATED_ALL) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x3F)) //Extended bits of the overflow register!
		//Finally, bits needed by the overflow register itself(of which we are an extension)!
		|| (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x4)) //Horizontal retrace start
		)
	{
		#ifdef LOG_UNHANDLED_SVGA_ACCESSES
		handled = 1;
		#endif
		//bit4=Horizontal retrace bit 8
		tempdata = VGA->registers->CRTControllerRegisters.REGISTERS.STARTHORIZONTALRETRACEREGISTER;
		tempdata |= ((et34k_tempreg & 0x10) << 4); //Add the new/changed bits!
		tempdata += GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.ENDHORIZONTALRETRACEREGISTER,5,0x3); //Add skew!
		//++tempdata; //One later!
		VGA->precalcs.horizontalretracestartfinish = tempdata; //Finish on the next clock?
		tempdata *= VGA->precalcs.characterwidth; //We're character units!
		tempdata <<= et34kdata->doublehorizontaltimings; //Double the horizontal timings if needed!
		updateCRTC |= VGA->precalcs.horizontalretracestart != tempdata; //To be updated?
		VGA->precalcs.horizontalretracestart = tempdata; //Save the new data!
		tempdata = VGA->precalcs.horizontalretracestartfinish; //When to finish?
		++tempdata; //The next clock is when we finish!
		tempdata *= VGA->precalcs.characterwidth; //We're character units!
		tempdata <<= et34kdata->doublehorizontaltimings; //Double the horizontal timings if needed!
		updateCRTC |= VGA->precalcs.horizontalretracestartfinish != tempdata; //To be updated?
		VGA->precalcs.horizontalretracestartfinish = tempdata; //Save the new data!
	}
	if (CRTUpdated || horizontaltimingsupdated || (whereupdated == WHEREUPDATED_ALL) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x3F)) //Extended bits of the overflow register!
		//Finally, bits needed by the overflow register itself(of which we are an extension)!
		|| (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x13)) //Offset register
		)
	{
		#ifdef LOG_UNHANDLED_SVGA_ACCESSES
		handled = 1;
		#endif
		//bit7=Offset bit 8
		tempdata = VGA->registers->CRTControllerRegisters.REGISTERS.OFFSETREGISTER; //The offset to use!
		updateCRTC |= (((et34k_tempreg & 0x80) << 1) | (tempdata & 0xFF)) != tempdata; //To be updated?
		tempdata |= ((et34k_tempreg & 0x80) << 1); //Add/replace the new/changed bits!
		tempdata <<= et34kdata->doublehorizontaltimings; //Double the horizontal timings if needed!
		tempdata <<= 1; //Reapply the x2 multiplier that's required!
		VGA->precalcs.rowsize = tempdata; //Save the new data!
	}
	if (CRTUpdated || (whereupdated == WHEREUPDATED_ALL) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x34)) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x31)) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x24)) || (whereupdated==(WHEREUPDATED_SEQUENCER|0x07))) //Clock frequency might have been updated?
	{
		#ifdef LOG_UNHANDLED_SVGA_ACCESSES
		handled = 1;
		#endif
		if (VGA==getActiveVGA()) //Active VGA?
		{
			changeRowTimer(VGA); //Make sure the display scanline refresh rate is OK!
		}		
	}

	//Misc settings
	if (CRTUpdated || (whereupdated == WHEREUPDATED_ALL) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x36))
		|| (whereupdated == (WHEREUPDATED_INDEX | INDEX_BANKREGISTERS)) //Bank registers?
		|| (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x30))
		|| (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x32)) //Interleaved check?
		|| (whereupdated == (WHEREUPDATED_MEMORYMAPPED | 0x13)) //MMU control register
		|| (whereupdated == (WHEREUPDATED_MEMORYMAPPED | 0x00)) //MMU aperture 0 register
		|| (whereupdated == (WHEREUPDATED_MEMORYMAPPED | 0x04)) //MMU aperture 1 register
		|| (whereupdated == (WHEREUPDATED_MEMORYMAPPED | 0x08)) //MMU aperture 2 register

		|| (whereupdated == (WHEREUPDATED_CRTCSPRITE | 0xEF)) //Image port updated?
		|| (whereupdated==(WHEREUPDATED_SEQUENCER|0x4)) || (whereupdated==(WHEREUPDATED_GRAPHICSCONTROLLER|0x5)) //Memory address
		 ) //Video system configuration #1!
	{
		#ifdef LOG_UNHANDLED_SVGA_ACCESSES
		handled = 1;
		#endif
		et34k_tempreg = et4k_reg(et34kdata, 3d4, 36); //The overflow register!
		tempval = VGA->precalcs.linearmode; //Old val!
		VGA->precalcs.MMU012_enabled = 0; //Default: disabled!
		VGA->precalcs.MMUregs_enabled = 0; //Default: disabled!
		VGA->precalcs.disableVGAlegacymemoryaperture = 0; //Enabling VGA memory access through the low aperture!

		VGA->precalcs.MMU012_aperture[0] = (getTsengLE24(&et34kdata->W32_MMUregisters[0][0x00]) & 0x3FFFFF); //Full 22-bit address
		VGA->precalcs.MMU012_aperture[1]= (getTsengLE24(&et34kdata->W32_MMUregisters[0][0x04]) & 0x3FFFFF); //Full 22-bit address
		VGA->precalcs.MMU012_aperture[2] = (getTsengLE24(&et34kdata->W32_MMUregisters[0][0x08]) & 0x3FFFFF); //Full 22-bit address
		VGA->precalcs.MMU012_aperture[3] = 0; //Full 22-bit address: not supported!
		VGA->precalcs.MMU0_aperture_linear = ((et34kdata->W32_MMUregisters[0][0x13] >> 4) & 1) | ((et34kdata->W32_MMUregisters[0][0x13] & 1) << 1); //Linear/accelerator mode?
		VGA->precalcs.MMU1_aperture_linear = ((et34kdata->W32_MMUregisters[0][0x13] >> 5) & 1) | (et34kdata->W32_MMUregisters[0][0x13] & 2); //Linear/accelerator mode?
		VGA->precalcs.MMU2_aperture_linear = ((et34kdata->W32_MMUregisters[0][0x13] >> 6) & 1) | ((et34kdata->W32_MMUregisters[0][0x13] & 4) >> 1); //Linear/accelerator mode?
		MMU_mappingupdated(); //A memory mapping has been updated?

		if (VGA->enable_SVGA==2) //Special ET3000 mapping?
		{
			VGA->precalcs.linearmode &= ~3; //Use normal Bank Select Register with VGA method of access!
			switch (et34k(VGA)->bank_size&3) //What Bank setting are we using?
			{
				case 0: //128k segments?
					VGA_MemoryMapBankRead = et34kdata->bank_read<<17; //Read bank!
					VGA_MemoryMapBankWrite = et34kdata->bank_write<<17; //Write bank!
					break;
				case 2: //1M linear memory?
				case 3: //1M linear memory? Unverified!
					//256K memory banking is used!
					VGA_MemoryMapBankRead = et34kdata->bank_read << 18; //Read bank!
					VGA_MemoryMapBankWrite = et34kdata->bank_write << 18; //Write bank!
					VGA->precalcs.linearmode |= 1; //Use contiguous memory accessing!
					break;
				case 1: //64k segments?
					VGA_MemoryMapBankRead = et34kdata->bank_read<<16; //Read bank!
					VGA_MemoryMapBankWrite = et34kdata->bank_write<<16; //Write bank!
					break;
				default:
					break;
			}
			VGA->precalcs.linearmode |= 4; //Enable the new linear and contiguous modes to affect memory!
			VGA->precalcs.extraSegmentSelectLines = 0; //We have no extra bits for the segment select lines!
		}
		else //ET4000 mapping?
		{
			if (et34k(VGA)->tsengExtensions) //W32 chip?
			{
				VGA->precalcs.linearmemorybase = ((uint_32)(et4k_W32_reg(et34kdata, 3d4, 30)&3)<<22); //Base to apply, in 4MB chunks! Only bits 1:0 are decoded for A23 and A22 in ISA mode!
				VGA->precalcs.linearmemorymask = ~((((uint_32)1)<<22)-1); //Disabled!
				VGA->precalcs.linearmemorysize = (1ULL<<22); //The default size of the aperture!
				if (et34k(VGA)->W32_21xA_ImagePortControl & 1) //IMA port enabled?
				{
					VGA->precalcs.linearmemorysize >>= 2; //The IMA port takes off 2 bits for quartering the window and moving all other structures following it upwards.
				}
				VGA->precalcs.MMU012_enabled = (et34k_tempreg & 8) ? 1 : 0; //MMU 0-2 enabled when bit 3 is set (the remainder is handled by the MAP setting itself for the low memory area)?
				VGA->precalcs.MMUregs_enabled = ((et34k_tempreg & 0x20) == 0x20) ? 1 : 0; //Memory mapped registers are enabled when bits 3 and 5 are set, according to documentation. PCem says bit 5 is enough? Windows 95 seems to agree.
				VGA->precalcs.extraSegmentSelectLines = 0x300000; //We have a bit 20 and 21 for the segment select lines!
			}
			else //Plain ET4000?
			{
				VGA->precalcs.linearmemorybase = 0; //No base to apply!
				VGA->precalcs.linearmemorymask = 0; //Disabled!
				VGA->precalcs.extraSegmentSelectLines = 0; //We have no extra bits for the segment select lines!
				VGA->precalcs.MMU012_enabled = 0; //No MMU 0-2 enabled when bit 3 is set (the remainder is handled by the MAP setting itself for the low memory area)?
				VGA->precalcs.MMUregs_enabled = 0; //No memory mapped registers are enabled when bits 3 and 5 are set.
			}
			if ((et34k_tempreg & 0x10)==0x00) //Segment configuration?
			{
				enforceSegmentMode:
				VGA->precalcs.linearmemorymask = 0; //Disable the memory window on W32 chips!
				VGA_MemoryMapBankRead = et34kdata->bank_read<<16; //Read bank!
				VGA_MemoryMapBankWrite = et34kdata->bank_write<<16; //Write bank!
				VGA->precalcs.linearmode &= ~2; //Use normal data addresses!
			}
			else //Linear system configuration? Disable the segment and enable linear mode (high 4 bits of the address select the bank)!
			{
				if ((VGA->enable_SVGA == 1) && et34k(VGA)->tsengExtensions) //W32 chip?
				{
					if (GETBITS(VGA->registers->GraphicsRegisters.REGISTERS.MISCGRAPHICSREGISTER, 2, 3) != 0) //Not a valid map layout to use? Enforce the low mapping instead!
					{
						goto enforceSegmentMode; //Enforce segment mode!
					}
					VGA->precalcs.disableVGAlegacymemoryaperture = 1; //Enforce disabling VGA memory access through the low aperture!
					VGA_MemoryMapBankRead = 0; //No read bank!
					VGA_MemoryMapBankWrite = 0; //No write bank!
					VGA->precalcs.linearmode |= 2; //Linear mode, use high bits!
				}
				else
				{
					VGA->precalcs.linearmemorymask = 0; //Disabled the memory window on ET4000 chips!
					VGA_MemoryMapBankRead = 0; //No read bank!
					VGA_MemoryMapBankWrite = 0; //No write bank!
					VGA->precalcs.linearmode |= 2; //Linear mode, use high 4-bits!
				}
			}
			if ((et34k_tempreg & 0x20) && (et34kdata->tsengExtensions==0)) //Continuous memory? Not on W32 variants! Only on the ET4000AX variant!
			{
				VGA->precalcs.linearmode |= 1; //Enable contiguous memory!
			}
			else //Normal memory addressing?
			{
				VGA->precalcs.linearmode &= ~1; //Use VGA-mapping of memory!
			}
			VGA->precalcs.linearmode |= 4; //Enable the new linear and contiguous modes to affect memory!
		}

		linearmodeupdated = (tempval!=VGA->precalcs.linearmode); //Linear mode has been updated!

		if (((VGA->precalcs.linearmode&5)==5) && (et34kdata->tsengExtensions==0)) //Special ET3K/ET4K linear graphics memory mode?
		{
			VGA_ReadMemoryMode = VGA_WriteMemoryMode = 3; //Special ET3000/ET4000 linear graphics memory mode!
		}
		else //Normal VGA memory access?
		{
			VGA_ReadMemoryMode = VGA->precalcs.ReadMemoryMode; //VGA compatibility mode!
			VGA_WriteMemoryMode = VGA->precalcs.WriteMemoryMode; //VGA compatiblity mode!
		}
		et34k_tempreg = et4k_reg(et34kdata, 3d4, 32); //The RAS/CAS configuration register!
		if ((et34k_tempreg&0x80) && (VGA->VRAM_size!=0x200000)) //Interleaved set without 2MB installed?
		{
			VGA->precalcs.disableVGAlegacymemoryaperture = 1; //Enforce disabling VGA memory access through the low aperture!
		}
		updateVGAMMUAddressMode(VGA); //Update the currently assigned memory mode for mapping memory by address!

		newfontwidth = ((et34k_tempreg & 4) >> 2); //Are we to use 16-bit wide fonts?
		if (unlikely(VGA->precalcs.doublewidthfont != newfontwidth)) //Font width is changed?
		{
			VGA->precalcs.doublewidthfont = newfontwidth; //Apply double font width or not!
			VGA_charsetupdated(VGA); //Update the character set, as a new width is to be applied!
		}
	}

	if (CRTUpdated || (whereupdated == WHEREUPDATED_ALL) || (whereupdated == (WHEREUPDATED_SEQUENCER | 0x04)) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x37))
		) //Video system configuration #2?
	{
		#ifdef LOG_UNHANDLED_SVGA_ACCESSES
		handled = 1;
		#endif
		if (CRTUpdated || (whereupdated == (WHEREUPDATED_SEQUENCER | 0x04))) //Need to update the VRAM limits itself instead of the wrapping?
		{
			if (GETBITS(VGA->registers->SequencerRegisters.REGISTERS.SEQUENCERMEMORYMODEREGISTER,1,1)==0) //Extended memory bit not set?
			{
				VGA->precalcs.VRAM_limit = 0xFFFF; //Limit to 64K memory!
			}
			else
			{
				VGA->precalcs.VRAM_limit = 0; //Unlimited!
			}
		}
		VGA->precalcs.VRAMmask = (VGA->VRAM_size - 1); //Don't limit VGA memory, wrap normally! Undocumented, but only affects multiple fonts in the font select register on the Tseng chipsets!
		VGA->precalcs.VMemMask = et34kdata->memwrap; //Apply the SVGA memory wrap on top of the normal memory wrapping!
	}

	if ((whereupdated==WHEREUPDATED_ALL) || (whereupdated==WHEREUPDATED_DACMASKREGISTER) || //DAC Mask register has been updated?
		(AttrUpdated || (whereupdated == WHEREUPDATED_ALL) || (whereupdated == (WHEREUPDATED_ATTRIBUTECONTROLLER | 0x16)) || (whereupdated == (WHEREUPDATED_ATTRIBUTECONTROLLER | 0x10))) //Attribute misc. register?
		|| (
			(et34k(VGA)->emulatedDAC == 2) //AT&T 20C490?
			&& UPDATE_SECTIONFULL(whereupdated, WHEREUPDATED_DAC, FullUpdate) //Single register updated?
			&& (et34k(VGA)->hicolorDACcommand&1) //Supposed to be masked off?
			)
		) 
	{
		#ifdef LOG_UNHANDLED_SVGA_ACCESSES
		handled = 1;
		#endif
		et34k_tempreg = et34k(VGA)->hicolorDACcommand; //Load the command to process! (Process like a SC11487)
		VGA->precalcs.SC15025_pixelmaskregister = ~0; //Default: no filter!
		DACmode = VGA->precalcs.DACmode; //Load the current DAC mode!
		DACmode &= ~8; //Legacy DAC modes?
		if ((et34k(VGA)->emulatedDAC!=2) && (et34k(VGA)->emulatedDAC<3)) //UMC UM70C178 or SC11487?
		{
			if (VGA->precalcs.AttributeController_16bitDAC == 3) //In 16-bit mode? Raise the DAC's HICOL input, thus making it 16-bit too!
			{
				//DACmode |= 3; //Set bit 0: we're full range, Set bit 1: we're a 16-bit mode!
				goto legacyDACmode; //Let the DAC determine what mode it's in normally!
			}
			else //Legacy DAC mode? Use the DAC itself for determining the mode it's rendering in!
			{
				legacyDACmode:
				if ((et34k_tempreg & 0xC0) == 0x80) //15-bit hicolor mode?
				{
					DACmode &= ~1; //Clear bit 0: we're one bit less!
					DACmode |= 2; //Set bit 1: we're a 16-bit mode!
				}
				else if ((et34k_tempreg & 0xC0) == 0xC0) //16-bit hicolor mode?
				{
					DACmode |= 3; //Set bit 0: we're full range, Set bit 1: we're a 16-bit mode!
				}
				else //Normal 8-bit DAC?
				{
					DACmode &= ~3; //Set bit 0: we're full range, Set bit 1: we're a 16-bit mode!
				}
			}
			if (et34k_tempreg & 0x20) //Two pixel clocks are used to latch the two bytes?
			{
				DACmode |= 4; //Use two pixel clocks to latch the two bytes?
			}
			else
			{
				DACmode &= ~4; //Use one pixel clock to latch the two bytes?
			}
		}
		else if (et34k(VGA)->emulatedDAC==2) //AT&T 20C490?
		{
			DACmode = 0; //Legacy VGA RAMDAC!
			VGA->precalcs.emulatedDACextrabits = 0xC0; //Become 8-bits DAC entries by default!
			switch ((et34k_tempreg>>5)&7) //What rendering mode?
			{
				case 0:
				case 1:
				case 2:
				case 3: //VGA mode?
					if ((et34k_tempreg & 2)==0) //6-bit DAC?
					{
						VGA->precalcs.emulatedDACextrabits = 0x00; //Become 6-bits DAC only!
					}
					break;
				case 4: //15-bit HICOLOR1 one clock?
					DACmode &= ~1; //Clear bit 0: we're one bit less!
					DACmode |= 2; //Set bit 1: we're a 16-bit mode!
					DACmode &= ~4; //Use one pixel clock to latch the two bytes?
					break;
				case 5: //15-bit HICOLOR2 two clocks?
					DACmode &= ~1; //Clear bit 0: we're one bit less!
					DACmode |= 2; //Set bit 1: we're a 16-bit mode!
					DACmode |= 4; //Use two pixel clocks to latch the two bytes?
					break;
				case 6: //16-bit two clocks?
					DACmode |= 3; //Set bit 0: we're full range, Set bit 1: we're a 16-bit mode!
					DACmode |= 4; //Use two pixel clocks to latch the two bytes?
					break;
				case 7: //24-bit three clocks?
					DACmode |= 3; //Set bit 0: we're full range, Set bit 1: we're a 16-bit+ mode!
					DACmode |= 4; //Use multiple pixel clocks to latch the two bytes?
					DACmode |= 8; //Use three pixel clocks to latch the three bytes?
					break;
			}

			//Update the DAC colors as required!
			if (et34k_tempreg & 1) //Sleep mode?
			{
				VGA->precalcs.turnDACoff = 1; //Turn the DAC off!
			}
			else
			{
				VGA->precalcs.turnDACoff = 0; //Turn the DAC on!
			}

			colorval = 0; //Init!
			for (;;) //Precalculate colors for DAC!
			{
				if (VGA->enable_SVGA != 3) //EGA can't change the DAC!
				{
					VGA->precalcs.DAC[colorval] = getcol256_Tseng(VGA, colorval); //Translate directly through DAC for output!
				}
				DAC_updateEntry(VGA, colorval); //Update a DAC entry for rendering!
				if (++colorval & 0xFF00) break; //Overflow?
			}
		}
		else if (et34k(VGA)->emulatedDAC == 3) //SC15025?
		{
			DACmode = 0; //Legacy VGA RAMDAC! Bit 5 is 32-bit color, otherwise 24-bit color! Bit 6 is translation mode enabled!
			if (et34k(VGA)->SC15025_auxiliarycontrolregister&4) //Sleep mode? Undocumented!
			{
				VGA->precalcs.turnDACoff = 1; //Turn the DAC off!
			}
			else
			{
				VGA->precalcs.turnDACoff = 0; //Turn the DAC on!
			}
			//Bit 1 of the Auxiliary Control Register is PED 75 IRE? Unknown what this is?
			VGA->precalcs.emulatedDACextrabits = 0xC0; //Become 8-bits DAC entries by default!
			if ((et34k(VGA)->SC15025_auxiliarycontrolregister & 1) == 0) //6-bit DAC?
			{
				VGA->precalcs.emulatedDACextrabits = 0x00; //Become 6-bits DAC only!
			}
			VGA->precalcs.SC15025_pixelmaskregister = ((((et34k(VGA)->SC15025_secondarypixelmaskregisters[2]<<8)|et34k(VGA)->SC15025_secondarypixelmaskregisters[1])<<8)|et34k(VGA)->SC15025_secondarypixelmaskregisters[0]); //Pixel mask to use!
			et34k(VGA)->SC15025_enableExtendedRegisters = ((et34k_tempreg & 0x10) >> 4); //Enable the extended registers at the color registers?
			switch ((et34k_tempreg >> 5) & 7) //What rendering mode?
			{
			case 0:
			case 1: //VGA mode? Mode 0!
				break;
			case 2:  //Mode 3a always!
				//Documentation: Hicolor 24 8-8-8. Mode 3a always. Color mode 4/5(Depending on command bit 0).
				DACmode |= 3; //Set bit 0: we're full range, Set bit 1: we're a 16-bit+ mode!
				DACmode |= 4; //Use multiple pixel clocks to latch the two bytes?
				DACmode |= 8; //Use three pixel clocks to latch the three bytes?
				DACmode |= 0x10; //Use four pixel clocks to latch the three bytes?
				if (et34k(VGA)->SC15025_pixelrepackregister & 1) //Both edges?
				{
					DACmode |= 0x400; //Use two pixel clocks to latch the four bytes?
				}
				//Otherwise, Rising edge only (undocumented)?
				else //Rising edge only (undocumented)
				{
					DACmode |= 0x800; //Special: true color RGBA!
				}
				if ((et34k_tempreg & 1)==1) //BGR mode?
				{
					DACmode |= 0x20; //BGR mode is enabled!
				}
				if (et34k_tempreg & 0x8) //D3 set? Enable LUT mode!
				{
					DACmode |= 0x40; //Enable LUT!
					DACmode |= ((et34k_tempreg & 0x6) << 6); //Bits 6&7 are to shift in 
				}
				break;
			case 3: //Mode 2 or 3b?
				if (et34k(VGA)->SC15025_pixelrepackregister & 1) //Mode 3b? 4-byte mode!
				{
					DACmode |= 3; //Set bit 0: we're full range, Set bit 1: we're a 16-bit+ mode!
					DACmode |= 4; //Use multiple pixel clocks to latch the two bytes?
					DACmode |= 8; //Use three pixel clocks to latch the three bytes?
					DACmode |= 0x10; //Use four pixel clocks to latch the three bytes instead!
					if ((et34k_tempreg & 1)==1) //BGR mode?
					{
						DACmode |= 0x20; //BGR mode is enabled!
					}
					if (et34k_tempreg & 0x8) //D3 set? Enable LUT mode!
					{
						DACmode |= 0x40; //Enable LUT!
						DACmode |= ((et34k_tempreg & 0x6) << 6); //Bits 6&7 are to shift in 
					}
				}
				else //Mode 2? 3-byte mode!
				{
					DACmode |= 3; //Set bit 0: we're full range, Set bit 1: we're a 16-bit+ mode!
					DACmode |= 4; //Use multiple pixel clocks to latch the two bytes?
					DACmode |= 8; //Use three pixel clocks to latch the three bytes?
					if ((et34k_tempreg & 1) == 1) //BGR mode?
					{
						DACmode |= 0x20; //BGR mode is enabled!
					}
					if (et34k_tempreg & 0x8) //D3 set? Enable LUT mode!
					{
						DACmode |= 0x40; //Enable LUT!
						DACmode |= ((et34k_tempreg & 0x6) << 6); //Bits 6&7 are to shift in 
					}
				}
				break;
			case 4: //15-bit HICOLOR1 one clock? Mode 1&2! 2 when D0 is set, color mode 1 otherwise! Repack mode 1a!
				DACmode |= 0x200; //Bit 15 is sent as well!
				DACmode &= ~1; //Clear bit 0: we're one bit less!
				DACmode |= 2; //Set bit 1: we're a 16-bit mode!
				DACmode &= ~4; //Use one pixel clock to latch the two bytes?
				if (et34k_tempreg & 1) //Extended mode? Color Mode 2!
				{
					DACmode |= 0x20; //Extended mode is enabled!
				}
				if (et34k_tempreg & 0x8) //D3 set? Enable LUT mode!
				{
					DACmode |= 0x40; //Enable LUT!
					DACmode |= ((et34k_tempreg & 0x6) << 6); //Bits 6&7 are to shift in 
				}
				//Otherwise, Color Mode 1?
				break;
			case 5: //15-bit HICOLOR2 two clocks? Color Mode 1&2! 2 when D0 is set! Repack mode 1b!
				DACmode |= 0x200; //Bit 15 is sent as well!
				DACmode &= ~1; //Clear bit 0: we're one bit less!
				DACmode |= 2; //Set bit 1: we're a 16-bit mode!
				DACmode |= 4; //Use two pixel clocks to latch the two bytes?
				if (et34k_tempreg & 1) //Extended mode? Mode 2!
				{
					DACmode |= 0x20; //Extended mode is enabled!
				}
				if (et34k_tempreg & 0x8) //D3 set? Enable LUT mode!
				{
					DACmode |= 0x40; //Enable LUT!
					DACmode |= ((et34k_tempreg & 0x6) << 6); //Bits 6&7 are to shift in 
				}
				//Othereise, color Mode 1?
				break;
			case 6: //16-bit one clock? Color Mode 3! Repack mode 1a!
				DACmode |= 3; //Set bit 0: we're full range, Set bit 1: we're a 16-bit mode!
				DACmode &= ~4; //Use one pixel clock to latch the two bytes?
				if (et34k_tempreg & 0x8) //D3 set? Enable LUT mode!
				{
					DACmode |= 0x40; //Enable LUT!
					DACmode |= ((et34k_tempreg & 0x6) << 6); //Bits 6&7 are to shift in 
				}
				break;
			case 7: //16-bit two clocks? Color Mode 3! Repack mode 1b!
				DACmode |= 3; //Set bit 0: we're full range, Set bit 1: we're a 16-bit+ mode!
				DACmode |= 4; //Use multiple pixel clocks to latch the two bytes?
				if (et34k_tempreg & 0x8) //D3 set? Enable LUT mode!
				{
					DACmode |= 0x40; //Enable LUT!
					DACmode |= ((et34k_tempreg & 0x6) << 6); //Bits 6&7 are to shift in 
				}
				break;
			}
		}
		else //Unknown DAC?
		{
			DACmode = 0; //Legacy VGA RAMDAC!
		}
		VGA->precalcs.DACmode = DACmode; //Apply the new DAC mode!
		updateVGADAC_Mode(VGA); //Update the effective DAC mode!
		updateSequencerPixelDivider(VGA, (SEQ_DATA*)VGA->Sequencer); //Update the sequencer as well!
		updateVGAAttributeController_Mode(VGA); //Update the attribute mode!

		colorval = 0; //Init!
		for (;;) //Precalculate colors for DAC!
		{
			if (VGA->enable_SVGA != 3) //EGA can't change the DAC!
			{
				VGA->precalcs.DAC[colorval] = getcol256_Tseng(VGA, colorval); //Translate directly through DAC for output!
			}
			DAC_updateEntry(VGA, colorval); //Update a DAC entry for rendering!
			if (++colorval & 0xFF00) break; //Overflow?
		}
	}

	if (SequencerUpdated || AttrUpdated || (whereupdated==(WHEREUPDATED_ATTRIBUTECONTROLLER|0x10)) || (whereupdated == WHEREUPDATED_ALL) || (whereupdated == (WHEREUPDATED_GRAPHICSCONTROLLER | 0x05)) || (whereupdated == (WHEREUPDATED_SEQUENCER | 0x04)) || linearmodeupdated
		) //Attribute misc. register?
	{
		et34k_tempreg = VGA->precalcs.linearmode; //Save the old mode for reference!
		VGA->precalcs.linearmode = ((VGA->precalcs.linearmode&~8) | (VGA->registers->SequencerRegisters.REGISTERS.SEQUENCERMEMORYMODEREGISTER&8)); //Linear graphics mode special actions enabled? Ignore Read Plane Select and Write Plane mask if set!
		VGA->precalcs.linearmode = ((VGA->precalcs.linearmode&~0x10) | ((VGA->registers->GraphicsRegisters.REGISTERS.GRAPHICSMODEREGISTER&0x40)>>2)); //Linear graphics mode for the renderer enabled?
		if (VGA->registers->AttributeControllerRegisters.REGISTERS.ATTRIBUTEMODECONTROLREGISTER & 0x40) //8-bit mode is setup?
		{
			VGA->precalcs.linearmode &= ~0x18; //Disable the linear mode override and use compatibility with the VGA!
		}

		linearmodeupdated = (VGA->precalcs.linearmode != et34k_tempreg); //Are we updating the mode?
		updateCRTC |= (VGA->precalcs.linearmode != et34k_tempreg); //Are we to update modes?

		if ((VGA->precalcs.linearmode & 0x10) || GETBITS(VGA->registers->AttributeControllerRegisters.REGISTERS.ATTRIBUTEMODECONTROLREGISTER, 6, 1)) //8-bit rendering has been enabled either through the Attribute Controller or mode set?
		{
			VGA->precalcs.AttributeModeControlRegister_ColorEnable8Bit = (VGA->precalcs.linearmode & 0x10)?3:1; //Enable 8-bit graphics!
			updateVGAAttributeController_Mode(VGA); //Update the attribute controller!
		}
		else
		{
			VGA->precalcs.AttributeModeControlRegister_ColorEnable8Bit = 0; //Disable 8-bit graphics!
			updateVGAAttributeController_Mode(VGA); //Update the attribute controller!
		}
	}

	if (CRTUpdated || charwidthupdated || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0x14))
		|| (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0x17))
		|| SequencerUpdated || AttrUpdated || (whereupdated==(WHEREUPDATED_ATTRIBUTECONTROLLER|0x10)) || (whereupdated == WHEREUPDATED_ALL) || (whereupdated == (WHEREUPDATED_GRAPHICSCONTROLLER | 0x05)) || linearmodeupdated
		) //Updated?
	{
		//This applies to the Frame buffer:
		byte BWDModeShift = 1; //Default: word mode!
		if (GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.UNDERLINELOCATIONREGISTER,6,1))
		{
			BWDModeShift = 2; //Shift by 2!
		}
		else if (GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.CRTCMODECONTROLREGISTER,6,1))
		{
			BWDModeShift = 0; //Shift by 0! We're byte mode!
		}

		byte characterclockshift = 1; //Default: reload every whole clock!
		//This applies to the address counter (renderer), causing it to increase and load more/less(factors of 2). This is used as a mask to apply to the 
		if (GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.UNDERLINELOCATIONREGISTER,5,1))
		{
			if (GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.CRTCMODECONTROLREGISTER,3,1)) //Both set? We reload twice per clock!
			{
				characterclockshift = 0; //Reload every half clock(4 pixels)!
			}
			else //Reload every 4 clocks!
			{
				characterclockshift = 7; //Reload every 4 clocks(32 pixels)!
			}
		}
		else if (GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.CRTCMODECONTROLREGISTER,3,1))
		{
			characterclockshift = 3; //Reload every other clock(16 pixels)!
		}
		else //Reload every clock!
		{
			characterclockshift = 1; //Reload every whole clock(8 pixels)!
		}

		if (VGA->precalcs.linearmode&0x10) //Linear mode is different on Tseng chipsets? This activates byte mode!
		{
			BWDModeShift = 0; //Byte mode always! We're linear memory, so act that way!
			characterclockshift = ((characterclockshift << 1) | 1); //Double the programmed character clock: two times the normal data is processed!
		}

		updateCRTC |= (VGA->precalcs.BWDModeShift != BWDModeShift); //Update the CRTC!
		VGA->precalcs.BWDModeShift = BWDModeShift;

		updateCRTC |= (VGA->precalcs.characterclockshift != characterclockshift); //Update the CRTC!
		VGA->precalcs.characterclockshift = characterclockshift; //Apply character clock shift!
	}

	if (((whereupdated==(WHEREUPDATED_SEQUENCER|0x01)) || FullUpdate || !VGA->precalcs.characterwidth) || (VGA->precalcs.charwidthupdated) //Sequencer register updated?
		|| (SequencerUpdated || AttrUpdated || (whereupdated==(WHEREUPDATED_ATTRIBUTECONTROLLER|0x10)) || (whereupdated == WHEREUPDATED_ALL) || (whereupdated == (WHEREUPDATED_SEQUENCER | 0x04)))
		|| ((whereupdated==(WHEREUPDATED_SEQUENCER|0x06)))
		//Double width font updated is checked below?
		|| (CRTUpdated || (whereupdated == WHEREUPDATED_ALL) || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x36))
			|| (whereupdated == (WHEREUPDATED_GRAPHICSCONTROLLER | 0x5)) //Memory address
			)
		|| linearmodeupdated
		)
	{
		if (VGA->precalcs.ClockingModeRegister_DCR != et34k_tempreg) adjustVGASpeed(); //Auto-adjust our VGA speed!
		et34k_tempreg = (GETBITS(VGA->registers->SequencerRegisters.REGISTERS.CLOCKINGMODEREGISTER,3,1))|((VGA->precalcs.linearmode&0x10)>>3); //Dot Clock Rate!
		if (VGA->enable_SVGA == 2) //ET3000 seems to oddly provide the DCR in bit 2 sometimes?
		{
			et34k_tempreg |= GETBITS(VGA->registers->SequencerRegisters.REGISTERS.CLOCKINGMODEREGISTER, 1, 1); //Use bit 1 as well!
		}
		updateCRTC |= (VGA->precalcs.ClockingModeRegister_DCR != et34k_tempreg); //Update the CRTC!
		VGA->precalcs.ClockingModeRegister_DCR = et34k_tempreg;

		et34k_tempreg = et34k_reg(et34kdata, 3c4, 06); //TS State Control
		et34k_tempreg &= 0x06; //Only bits 1-2 are used!
		if (VGA->precalcs.doublewidthfont == 0) //Double width not enabled? Then we're invalid(VGA-compatible)!
		{
			et34k_tempreg = 0; //VGA-compatible!
		}
		et34k_tempreg |= GETBITS(VGA->registers->SequencerRegisters.REGISTERS.CLOCKINGMODEREGISTER, 0, 1); //Bit 0 of the Clocking Mode Register(Tseng calls it the TS Mode register) is also included!
		switch (et34k_tempreg) //What extended clocking mode?
		{
		default:
		case 0: //VGA-compatible modes?
		case 1: //VGA-compatible modes?
			newcharwidth = GETBITS(VGA->registers->SequencerRegisters.REGISTERS.CLOCKINGMODEREGISTER, 0, 1) ? 8 : 9; //Character width!
			newtextwidth = VGA->precalcs.characterwidth; //Text character width(same as normal characterwidth by default)!
			break;
		case 2: //10 dots/char?
		case 3: //11 dots/char?
		case 4: //12 dots/char?
			newcharwidth = 8; //Character width!
			newtextwidth = (8|et34k_tempreg); //Text character width!
			break;
		case 5: //WhatVGA says 7 dots/char!
		case 6: //WhatVGA says 6 dots/char!
			newcharwidth = 8; //Character width!
			newtextwidth = (6 | (et34k_tempreg&1)); //Text character width!
			break;
		case 7: //16 dots/char?
			newcharwidth = 8; //Character width!
			newtextwidth = 16; //Text character width!
			break;
		}
		updateCRTC |= (VGA->precalcs.characterwidth != newcharwidth); //Char width updated?
		updateCRTC |= (VGA->precalcs.textcharacterwidth != newtextwidth); //Char width updated?
		VGA->precalcs.characterwidth = newcharwidth; //Char clock width!
		VGA->precalcs.textcharacterwidth = newtextwidth; //Text character width!
	}

	//Image port
	if ((whereupdated == WHEREUPDATED_ALL)
		|| (whereupdated == (WHEREUPDATED_CRTCSPRITE | 0xF0)) //Image Starting Address (24-bit) updated?
		|| (whereupdated == (WHEREUPDATED_CRTCSPRITE | 0xF1)) //Image Starting Address (24-bit) updated?
		|| (whereupdated == (WHEREUPDATED_CRTCSPRITE | 0xF2)) //Image Starting Address (24-bit) updated?
		) //Image port
	{
		VGA->precalcs.imageport_startingaddress = (getTsengLE24(&et34k(getActiveVGA())->W32_21xA_shadowRegisters[0xF0-0xE0])<<2); //Starting address!
	}

	if ((whereupdated == WHEREUPDATED_ALL)
		|| (whereupdated == (WHEREUPDATED_CRTCSPRITE | 0xF3)) //Image Transfer Length (word) updated?
		|| (whereupdated == (WHEREUPDATED_CRTCSPRITE | 0xF4)) //Image Transfer Length (word) updated?
		) //Image port
	{
		VGA->precalcs.imageport_transferlength = (getTsengLE16(&et34k(getActiveVGA())->W32_21xA_shadowRegisters[0xF3 - 0xE0])<<2); //Transfer length for a scanline, in bytes!
	}

	if ((whereupdated == WHEREUPDATED_ALL)
		|| (whereupdated == (WHEREUPDATED_CRTCSPRITE | 0xF5))  //Length of a transferred scanline in VRAM, in bytes!
		|| (whereupdated == (WHEREUPDATED_CRTCSPRITE | 0xF6))  //Length of a transferred scanline in VRAM, in bytes!
		) //Image port
	{
		VGA->precalcs.imageport_rowoffset = (getTsengLE16(&et34k(getActiveVGA())->W32_21xA_shadowRegisters[0xF5 - 0xE0])<<2); //Transfer length for a scanline, in bytes!
	}

	if ((whereupdated == WHEREUPDATED_ALL)
		|| (whereupdated == (WHEREUPDATED_CRTCSPRITE | 0xF7))  //Enable interlacing of data in the image port!
		) //Image port
	{
		VGA->precalcs.imageport_interlace = ((et34k(getActiveVGA())->W32_21xA_ImagePortControl&2)>>1); //Transfer length for a scanline, in bytes!
	}

	//CRTCB/Sprite registers!
	if ((whereupdated == WHEREUPDATED_ALL)
		|| (whereupdated == (WHEREUPDATED_CRTCSPRITE | 0xEF)) //CRTC/Sprite select updated?
		|| (whereupdated == (WHEREUPDATED_CRTCSPRITE | 0xF7))  //Enable CRTCB/Sprite!
		)
	{
		//Sprite function enable/disable?
		SpriteCRTCBenabledupdated = 1; //We're updating the enable flag!
		if (et34kdata->W32_21xA_ImagePortControl & 0x80) //CRTCB/Sprite enable?
		{
			if (et34kdata->W32_21xA_CRTCBSpriteControl & 1) //CRTCB function?
			{
				VGA->precalcs.SpriteCRTCEnabled = 2; //CRTCB function!
			}
			else //Sprite function?
			{
				VGA->precalcs.SpriteCRTCEnabled = 1; //Sprite function!
			}
			if (et34kdata->W32_21xA_CRTCBSpriteControl & 2) //Overlay the CRTC?
			{
				//Nothing special: normally display over the CRTC data!
			}
			else //Output to SP 0:1?
			{
				VGA->precalcs.SpriteCRTCEnabled |= 4; //Output to SP0:1 instead?
			}
		}
		else //Disabled?
		{
			VGA->precalcs.SpriteCRTCEnabled = 0; //Disabled!
		}
		if (et34kdata->W32_21xA_CRTCBSpriteControl & 4) //128 pixels instead of 64?
		{
			VGA->precalcs.SpriteSize = 128; //128 pixels wide/height!
		}
		else
		{
			VGA->precalcs.SpriteSize = 64; //64 pixels wide/height!
		}
	}

	if ((whereupdated == WHEREUPDATED_ALL)
		|| SpriteCRTCBenabledupdated //CRTC/Sprite select updated?
		|| (whereupdated == (WHEREUPDATED_CRTCSPRITE | 0xE0)) //Horizontal pixel position low?
		|| (whereupdated == (WHEREUPDATED_CRTCSPRITE | 0xE1)) //Horizontal pixel position high?
		)
	{
		if (VGA->precalcs.SpriteCRTCEnabled) //CRTCB/Sprite function?
		{
			VGA->precalcs.SpriteCRTChorizontaldisplaydelay = (((et34kdata->W32_21xA_shadowRegisters[0xE1 - 0xE0] & 7) << 8) | et34kdata->W32_21xA_shadowRegisters[0xE0 - 0xE0]); //Horizontal pixel delay until we start the window!
		}
		else //No function?
		{
			VGA->precalcs.SpriteCRTChorizontaldisplaydelay = ~0; //Don't display!
		}
	}

	if ((whereupdated == WHEREUPDATED_ALL)
		|| SpriteCRTCBenabledupdated //CRTC/Sprite select updated?
		|| (whereupdated == (WHEREUPDATED_CRTCSPRITE | 0xE4)) //Vertical pixel position low?
		|| (whereupdated == (WHEREUPDATED_CRTCSPRITE | 0xE5)) //Vertical pixel position high?
		)
	{
		if (VGA->precalcs.SpriteCRTCEnabled) //CRTCB/Sprite function?
		{
			VGA->precalcs.SpriteCRTCverticaldisplaydelay = (((et34kdata->W32_21xA_shadowRegisters[0xE5 - 0xE0] & 7) << 8) | et34kdata->W32_21xA_shadowRegisters[0xE4 - 0xE0]); //Horizontal pixel delay until we start the window!
		}
		else //No function?
		{
			VGA->precalcs.SpriteCRTChorizontaldisplaydelay = ~0; //Don't display!
		}
	}

	if ((whereupdated == WHEREUPDATED_ALL)
		|| SpriteCRTCBenabledupdated //CRTC/Sprite select updated?
		|| (whereupdated == (WHEREUPDATED_CRTCSPRITE | 0xE2)) //CRTC Horizontal width low or Sprite horizontal preset?
		|| (whereupdated == (WHEREUPDATED_CRTCSPRITE | 0xE3)) //CRTC Horizontal width high?
		)
	{
		if (VGA->precalcs.SpriteCRTCEnabled) //CRTCB/Sprite function?
		{
			if (VGA->precalcs.SpriteCRTCEnabled == 2) //CRTC?
			{
				VGA->precalcs.SpriteCRTChorizontalwindowwidth = (((et34kdata->W32_21xA_shadowRegisters[0xE3 - 0xE0] & 7) << 8) | et34kdata->W32_21xA_shadowRegisters[0xE2 - 0xE0]) + 1; //Horizontal width of the end of the window!
				VGA->precalcs.SpriteCRTChorizontaldisplaypreset = 0; //No preset is used!
			}
			else //Sprite?
			{
				VGA->precalcs.SpriteCRTChorizontalwindowwidth = VGA->precalcs.SpriteSize; //The width is the sprite size!
				VGA->precalcs.SpriteCRTChorizontaldisplaypreset = et34kdata->W32_21xA_shadowRegisters[0xE2 - 0xE0]; //Horizontal preset of the sprite!
			}
		}
		else //No function?
		{
			VGA->precalcs.SpriteCRTChorizontaldisplaydelay = 0; //Don't display!
		}
	}

	if ((whereupdated == WHEREUPDATED_ALL)
		|| SpriteCRTCBenabledupdated //CRTC/Sprite select updated?
		|| (whereupdated == (WHEREUPDATED_CRTCSPRITE | 0xE6)) //CRTC Vertical height low or Sprite vertical preset?
		|| (whereupdated == (WHEREUPDATED_CRTCSPRITE | 0xE7)) //CRTC Vertical height high?
		)
	{
		if (VGA->precalcs.SpriteCRTCEnabled) //CRTCB/Sprite function?
		{
			if (VGA->precalcs.SpriteCRTCEnabled == 2) //CRTC?
			{
				VGA->precalcs.SpriteCRTCverticalwindowheight = (((et34kdata->W32_21xA_shadowRegisters[0xE7 - 0xE0] & 7) << 8) | et34kdata->W32_21xA_shadowRegisters[0xE6 - 0xE0]) + 1; //Horizontal width of the end of the window!
				VGA->precalcs.SpriteCRTCverticaldisplaypreset = 0; //No preset is used!
			}
			else //Sprite?
			{
				VGA->precalcs.SpriteCRTCverticalwindowheight = VGA->precalcs.SpriteSize; //The width is the sprite size!
				VGA->precalcs.SpriteCRTCverticaldisplaypreset = et34kdata->W32_21xA_shadowRegisters[0xE6 - 0xE0]; //Horizontal preset of the sprite!
			}
		}
		else //No function?
		{
			VGA->precalcs.SpriteCRTChorizontaldisplaydelay = 0; //Don't display!
		}
	}

	if ((whereupdated == WHEREUPDATED_ALL)
		|| SpriteCRTCBenabledupdated //CRTC/Sprite select updated?
		|| (whereupdated == (WHEREUPDATED_CRTCSPRITE | 0xE8)) //CRTC/Sprite start address low?
		|| (whereupdated == (WHEREUPDATED_CRTCSPRITE | 0xE9)) //CRTC/Sprite start address mid?
		|| (whereupdated == (WHEREUPDATED_CRTCSPRITE | 0xEA)) //CRTC/Sprite start address high?
		)
	{
		VGA->precalcs.SpriteCRTCstartaddress = ((((((et34kdata->W32_21xA_shadowRegisters[0xEA - 0xE0] & 0xF) << 8) | et34kdata->W32_21xA_shadowRegisters[0xE9 - 0xE0]) << 8) | et34kdata->W32_21xA_shadowRegisters[0xE8 - 0xE0]) << 2); //Start offset in doublewords!
	}

	if ((whereupdated == WHEREUPDATED_ALL)
		|| SpriteCRTCBenabledupdated //CRTC/Sprite select updated?
		|| (whereupdated == (WHEREUPDATED_CRTCSPRITE | 0xEB)) //CRTC/Sprite row offset low?
		|| (whereupdated == (WHEREUPDATED_CRTCSPRITE | 0xEC)) //CRTC/Sprite start address mid?
		)
	{
		VGA->precalcs.SpriteCRTCrowoffset = ((((et34kdata->W32_21xA_shadowRegisters[0xEC - 0xE0] & 1) << 8) | et34kdata->W32_21xA_shadowRegisters[0xEB - 0xE0]) << 3); //Number of quadwords between rows in VRAM!
	}

	if ((whereupdated == WHEREUPDATED_ALL)
		|| SpriteCRTCBenabledupdated //CRTC/Sprite select updated?
		|| (whereupdated == (WHEREUPDATED_CRTCSPRITE | 0xEE)) //CRTC color depth?
		|| (charwidthupdated) //Character width updated for different pixel panning?
		)
	{
		et34k_tempreg = et34kdata->W32_21xA_shadowRegisters[0xEE - 0xE0]; //The register!
		if (VGA->precalcs.SpriteCRTCEnabled == 2) //CRTCB function?
		{
			if ((et34k_tempreg & 0xF) > 4) //More than 16BPP?
			{
				et34k_tempreg &= ~0xF; //Default to 0!
			}
			VGA->precalcs.SpriteCRTCpixeldepth = (et34k_tempreg & 0xF); //Bit depth in power of 2!
			VGA->precalcs.SpriteCRTCrowheight = ((et34k_tempreg >> 6)+1); //The height of each row!
			VGA->precalcs.SpriteCRTCpixelwidth = (((et34k_tempreg >> 4)&3) + 1); //The width of each pixel!
		}
		else //Sprite function?
		{
			VGA->precalcs.SpriteCRTCpixeldepth = 1; //Bit depth in power of 2! Always 2BPP!
			VGA->precalcs.SpriteCRTCrowheight = 1; //The height of each row!
			VGA->precalcs.SpriteCRTCpixelwidth = 1; //The width of each pixel!
		}

		if (VGA->precalcs.SpriteCRTCEnabled == 2) //CRTCB enabled?
		{
			pixelboost = 0; //Actual pixel boost!
			possibleboost = GETBITS(et34k_tempreg, 0, 0xF); //Possible value, to be determined!
			//Only 8 pixel width? Only 3 bits are available for storage anyways!
			possibleboost &= 0x7; //Repeat the low values!
			pixelboost = possibleboost; //Enable normally!
			VGA->precalcs.SpriteCRTCpixelpannning = pixelboost; //No pixel panning possible!
		}
		else //Sprite enabled?
		{
			VGA->precalcs.SpriteCRTCpixelpannning = 0; //No pixel panning possible!
		}
	}

	if (updateCRTC) //Update CRTC?
	{
		VGA_calcprecalcs_CRTC(VGA); //Update the CRTC timing data!
		adjustVGASpeed(); //Auto-adjust our VGA speed!
	}

	VGA->precalcs.charwidthupdated = 0; //Not updated anymore!
	et34k(VGA)->oldextensionsEnabled = et34k(VGA)->extensionsEnabled; //Save the new extension status to detect changes!
	#ifdef LOG_UNHANDLED_SVGA_ACCESSES
	if (!handled) //Are we not handled?
	{
		dolog("ET34k","Unandled precalcs on SVGA: %08X",whereupdated); //We're ignored!
	}
	#endif
}

DOUBLE Tseng34k_clockMultiplier(VGA_Type *VGA)
{
	byte timingdivider = et34k_reg(et34k(VGA),3c4,07); //Get the divider info!
	if (timingdivider&0x01) //Divide Master Clock Input by 4!
	{
		#ifdef IS_LONGDOUBLE
		return 0.25L; //Divide by 4!
		#else
		return 0.25; //Divide by 4!
		#endif
	}
	else if (timingdivider&0x40) //Divide Master Clock Input by 2!
	{
		#ifdef IS_LONGDOUBLE
		return 0.5L; //Divide by 2!
		#else
		return 0.5; //Divide by 2!
		#endif
	}
	//Normal Master clock?
	#ifdef IS_LONGDOUBLE
	return 1.0L; //Normal clock!
	#else
	return 1.0; //Normal clock!
	#endif
}

extern DOUBLE VGA_clocks[4]; //Normal VGA clocks!

DOUBLE Tseng34k_getClockRate(VGA_Type *VGA)
{
	byte clock_index;
	if (!et34k(VGA)) return 0.0f; //Unregisterd ET4K!
	if (VGA->enable_SVGA == 2) //ET3000?
	{
		clock_index = get_clock_index_et3k(VGA); //Retrieve the ET4K clock index!
		return ET3K_clockFreq[clock_index & 0xF]*Tseng34k_clockMultiplier(VGA); //Give the ET4K clock index rate!
	}
	else //ET4000?
	{
		clock_index = get_clock_index_et4k(VGA); //Retrieve the ET4K clock index!
		return ET4K_clockFreq[clock_index & 0xF]*Tseng34k_clockMultiplier(VGA); //Give the ET4K clock index rate!
	}
	return 0.0; //Not an ET3K/ET4K clock rate, default to VGA rate!
}

//Basic Container/wrapper support
void freeTsengExtensions(void** ptr, uint_32 size, SDL_sem* lock) //Free a pointer (used internally only) allocated with nzalloc/zalloc and our internal functions!
{
	SVGA_ET34K_DATA* obj = (SVGA_ET34K_DATA*)*ptr; //Take the object out of the pointer!
	//Start by freeing the surfaces in the handlers!
	changedealloc((void*)obj, sizeof(*obj), getdefaultdealloc()); //Change the deallocation function back to it's default!
	if (obj->W32_MMUqueue) //Queue still allocated?
	{
		free_fifobuffer(&obj->W32_MMUqueue); //Free the queue!
	}
	if (obj->W32_virtualbusqueue) //Queue still allocated?
	{
		free_fifobuffer(&obj->W32_virtualbusqueue); //Free the queue!
	}
	//We're always allowed to release the container.
	freez(ptr,sizeof(*obj),"SVGA_ET34K_DATA"); //Free normally using the normally used functions!
}

void SVGA_Setup_TsengET4K(uint_32 VRAMSize, byte ET4000_extensions) {
	if ((getActiveVGA()->enable_SVGA == 2) || (getActiveVGA()->enable_SVGA == 1)) //ET3000/ET4000?
		VGA_registerExtension(&Tseng34K_readIO, &Tseng34K_writeIO, &Tseng34k_init,&Tseng34k_calcPrecalcs,&Tseng34k_getClockRate,NULL);
	else return; //Invalid SVGA!		
	Tseng4k_VRAMSize = VRAMSize; //Set this VRAM size to use!
	getActiveVGA()->SVGAExtension = zalloc(sizeof(SVGA_ET34K_DATA),"SVGA_ET34K_DATA",getLock(LOCK_CPU)); //Our SVGA extension data!
	if (!getActiveVGA()->SVGAExtension)
	{
		raiseError("ET4000","Couldn't allocate SVGA card ET4000 data! Ran out of memory!");
	}
	else //Valid registers?
	{
		getActiveVGA()->SVGAExtension_size = sizeof(SVGA_ET34K_DATA); //Our SVGA extension data!
		changedealloc(et34k(getActiveVGA()), sizeof(*et34k(getActiveVGA())), &freeTsengExtensions); //Deallocation support for the extensions!
		et34k_reg(et34k(getActiveVGA()),3c4,07) = 0x4|(0x8|0x20)|0x80; //Default to VGA mode(bit 7 set) with full memory map (bits 3&5 set), Other bits are set always.
		et34k(getActiveVGA())->tsengExtensions = ET4000_extensions; //What extension is enabled in the settings!
		if (et34k(getActiveVGA())->tsengExtensions) //Extensions enabled?
		{
			et34k(getActiveVGA())->W32_MMUqueue = allocfifobuffer(0x20 * sizeof(uint_64), 0); //Basic fifo to use!
			if (!et34k(getActiveVGA())->W32_MMUqueue) //Couldn't allocate?
			{
				raiseError("ET4000", "Couldn't allocate SVGA card ET4000/W32 queue data! Ran out of memory!");
			}
			et34k(getActiveVGA())->W32_virtualbusqueue = allocfifobuffer(0x4 * sizeof(uint_64), 0); //Basic fifo to use!
			if (!et34k(getActiveVGA())->W32_virtualbusqueue) //Couldn't allocate?
			{
				raiseError("ET4000", "Couldn't allocate SVGA card ET4000/W32 virtual bus queue data! Ran out of memory!");
			}
		}
	}
}
