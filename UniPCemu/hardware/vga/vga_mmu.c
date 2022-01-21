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

#include "headers/hardware/vga/vga.h" //Basic defs!
#include "headers/support/log.h" //Loggin support!
#include "headers/hardware/vga/vga_vram.h" //VRAM support!
#include "headers/mmu/mmuhandler.h" //Handling support!
#include "headers/hardware/vga/vga_cga_mda.h" //CGA/MDA support!
#include "headers/cpu/cpu.h" //Emulator cpu support for waitstates!
#include "headers/hardware/vga/svga/tseng.h" //Tseng support!

#define LE32(x) SDL_SwapLE32(x)

//#define ENABLE_SPECIALDEBUGGER

byte VGA_linearmemoryaddressed = 0; //Is the linear memory window addressed? 0=Low VRAM, 1=Linear VRAM, 2=MMU 0-2, 3=External mapped registers, 4=Memory mapped registers
uint_32 VGA_MMU012_blocksize = 0x2000; //Size of a MMU area inside the block!
uint_32 effectiveVRAMstart = 0xA0000; //Effective VRAM start to use!
uint_32 VGA_VRAM_START = 0xA0000; //VRAM start address default!
uint_32 VGA_VRAM_END = 0xC0000; //VRAM end address default!
uint_32 VGA_MMU012_START = 0x0;
uint_32 VGA_MMU012_START_linear = 0x0;
uint_32 VGA_MMU012_END = 0x0;
uint_32 VGA_MMU012_END_linear = 0x0;
uint_32 VGA_MMUregs_START = 0x0;
uint_32 VGA_MMUregs_START_linear = 0x0;
byte VGA_MMU012_START_enabled = 0; //Enabled VGA_MMU012_START?
byte VGA_MMU012_START_linear_enabled = 0; //Enabled VGA_MMU012_START for linear addresses?
byte VGA_MMUregs_START_enabled = 0; //Enabled VGA_MMUregs_START?
byte VGA_MMUregs_START_linear_enabled = 0; //Enabled VGA_MMUregs_START for linear addresses?
byte VGA_MMUregs_enabled = 0; //MMU regs enabled at all?
byte VGA_MMU012_enabled = 0; //MMU012 enabled at all?

byte VGA_RAMEnable = 1; //Is our RAM enabled?
byte VGA_MemoryMapSelect = 0; //What memory map is active?

uint_32 VGA_MemoryMapBankRead = 0, VGA_MemoryMapBankWrite = 0; //The memory map bank to use!

OPTINLINE void VGA_updateLatches()
{
	//Update the latch the software can read.
	getActiveVGA()->registers->CRTControllerRegisters.REGISTERS.GraphicsControllerDataLatches = getActiveVGA()->registers->ExternalRegisters.DATALATCH.latchplane[GETBITS(getActiveVGA()->registers->GraphicsRegisters.REGISTERS.READMAPSELECTREGISTER,0,3)]; //Update the latch the software reads (R/O)
}

void VGA_updateVRAMmaps(VGA_Type *VGA)
{
	VGA_RAMEnable = GETBITS(VGA->registers->ExternalRegisters.MISCOUTPUTREGISTER,1,1); //RAM enabled?
	VGA_MemoryMapSelect = GETBITS(VGA->registers->GraphicsRegisters.REGISTERS.MISCGRAPHICSREGISTER,2,3); //Update the selected memory map!
	switch (VGA_MemoryMapSelect) //What memory map?
	{
	case 0: //A0000-BFFFF (128K region)?
		VGA_VRAM_START = 0xA0000; //Start!
		VGA_VRAM_END = 0xC0000; //End!
		VGA_MMU012_START_enabled = 0; //Disabled!
		VGA_MMU012_START_linear_enabled = 1; //Enabled!
		VGA_MMUregs_START_enabled = 0; //Disabled!
		VGA_MMUregs_START_linear_enabled = 1; //Enabled!
		VGA_MMU012_START = 0; //Unused!
		VGA_MMUregs_START = 0; //Unused!
		VGA_MMU012_END = 0; //Unused!
		break;
	case 1: //A0000-AFFFF (64K region)?
		VGA_VRAM_START = 0xA0000; //Start!
		VGA_VRAM_END = 0xB0000; //End!
		VGA_MMU012_START_enabled = 1; //Enabled!
		VGA_MMU012_START_linear_enabled = 1; //Enabled!
		VGA_MMUregs_START_enabled = 1; //Enabled!
		VGA_MMUregs_START_linear_enabled = 1; //Enabled!
		VGA_MMU012_START = 0xB8000; //Used!
		VGA_MMU012_END = 0xBF000; //Used!
		VGA_MMUregs_START = 0xBFF00; //Might be used!
		break;
	case 2: //B0000-B7FFF (32K region)?
		VGA_VRAM_START = 0xB0000; //Start!
		VGA_VRAM_END = 0xB8000; //End!
		VGA_MMU012_START_enabled = 1; //Enabled!
		VGA_MMU012_START_linear_enabled = 1; //Enabled!
		VGA_MMUregs_START_enabled = 1; //Enabled!
		VGA_MMUregs_START_linear_enabled = 1; //Enabled!
		VGA_MMU012_START = 0xA8000; //Used!
		VGA_MMU012_END = 0xAF000; //Used!
		VGA_MMUregs_START = 0xAFF00; //Might be used!
		break;
	case 3: //B8000-BFFFF (32K region)?
		VGA_VRAM_START = 0xB8000; //Start!
		VGA_VRAM_END = 0xC0000; //End!
		VGA_MMU012_START_enabled = 1; //Enabled!
		VGA_MMU012_START_linear_enabled = 1; //Enabled!
		VGA_MMUregs_START_enabled = 1; //Enabled!
		VGA_MMUregs_START_linear_enabled = 1; //Enabled!
		VGA_MMU012_START = 0xA8000; //Used!
		VGA_MMU012_END = 0xAF000; //Used!
		VGA_MMUregs_START = 0xAFF00; //Might be used!
		break;
	default:
		break;
	}
}

/*

VRAM base offset!

*/

OPTINLINE byte is_A000VRAM(uint_32 linearoffset) //In VRAM (for CPU), offset=real memory address (linear memory)?
{
	INLINEREGISTER uint_32 addr=linearoffset; //The offset to check!
	if ((getActiveVGA()->registers->VGA_enabled) == 0) return 0; //Disabled MMU?
	effectiveVRAMstart = VGA_VRAM_START; //Effective start of VRAM!
	VGA_linearmemoryaddressed = 0; //Not addressed by default!
	if (((linearoffset & getActiveVGA()->precalcs.linearmemorymask) == getActiveVGA()->precalcs.linearmemorybase) && getActiveVGA()->precalcs.linearmemorymask) //Linear memory addressed?
	{
		VGA_linearmemoryaddressed = 1; //Addressed VRAM linearly!
		effectiveVRAMstart = getActiveVGA()->precalcs.linearmemorybase; //Where does the window start!
		if ((((linearoffset - effectiveVRAMstart) >= VGA_MMU012_START_linear) && ((linearoffset - effectiveVRAMstart) < (VGA_MMU012_START_linear + ((getActiveVGA()->precalcs.linearmemorysize & 0x300000) ? 0x1800000 : 0x60000)))) && VGA_MMU012_START_linear_enabled && VGA_MMU012_enabled) //MMU0-2 register blocks in 128KB/512KB chunks?
		{
			effectiveVRAMstart = VGA_MMU012_START_linear; //Where does the window start!
			VGA_linearmemoryaddressed = 2; //Addressed!
			VGA_MMU012_blocksize = ((getActiveVGA()->precalcs.linearmemorysize & 0x300000) ? 0x800000 : 0x20000); //Size of a block in the aperture!
			return 1; //Special!
		}
		if (((linearoffset - effectiveVRAMstart) >= (VGA_MMU012_START_linear + ((getActiveVGA()->precalcs.linearmemorysize & 0x300000) ? 0x1800000 : 0x60000))) && ((linearoffset - effectiveVRAMstart) < VGA_MMU012_END_linear) && VGA_MMU012_START_linear_enabled && VGA_MMU012_enabled) //External Mapped Registers?
		{
			effectiveVRAMstart = (VGA_MMU012_START_linear + ((getActiveVGA()->precalcs.linearmemorysize & 0x300000) ? 0x1800000 : 0x60000)); //Where does the window start!
			VGA_linearmemoryaddressed = 3; //Addressed!
			return 1; //Special!
		}
		if (((linearoffset - effectiveVRAMstart) >= VGA_MMUregs_START_linear) && ((linearoffset - effectiveVRAMstart) < (VGA_MMUregs_START_linear + 0x100)) && VGA_MMUregs_START_linear_enabled && VGA_MMUregs_enabled) //Memory mapped registers chunks?
		{
			effectiveVRAMstart = VGA_MMUregs_START_linear; //Where does the window start!
			VGA_linearmemoryaddressed = 4; //Addressed!
			return 1; //Special!
		}
		if (((linearoffset - effectiveVRAMstart) & 0x300000) && ((getActiveVGA()->precalcs.linearmemorysize & 0x300000) == 0)) //Image port?
		{
			effectiveVRAMstart += 0x100000; //Start of the VRAM window!
			VGA_linearmemoryaddressed = 5; //Addressed!
			return 1; //Special!
		}
		//We're either the VRAM linear window or unmapped addresses!
		if ((linearoffset-effectiveVRAMstart) >= getActiveVGA()->precalcs.linearmemorysize) //Past effective VRAM window? Either MMU0-2, memory mapped regs or external regs?
		{
			return 0; //Out of range for the VRAM window!
		}
		return VGA_RAMEnable; //Enabled if matched and RAM is enabled!
	}
	if (unlikely((linearoffset >= 0xA0000) && (linearoffset < 0xC0000))) //Memory mapped area is possibly addressed?
	{
		if ((linearoffset >= VGA_MMU012_START) && (linearoffset < (VGA_MMU012_START + 0x6000)) && VGA_MMU012_START_enabled && VGA_MMU012_enabled) //MMU0-2 register blocks in 8KB chunks?
		{
			effectiveVRAMstart = VGA_MMU012_START; //Where does the window start!
			VGA_linearmemoryaddressed = 2; //Addressed!
			VGA_MMU012_blocksize = 0x2000; //Size of a block in the aperture!
			return 1; //Special!
		}
		if ((linearoffset >= (VGA_MMU012_START + 0x6000)) && (linearoffset < VGA_MMU012_END) && VGA_MMU012_START_enabled && VGA_MMU012_enabled) //External mapped memory?
		{
			effectiveVRAMstart = (VGA_MMU012_START + 0x6000); //Where does the window start!
			VGA_linearmemoryaddressed = 3; //Addressed!
			return 1; //Special!
		}
		if ((linearoffset >= VGA_MMUregs_START) && (linearoffset < (VGA_MMUregs_START + 0x100)) && VGA_MMUregs_START_enabled && VGA_MMUregs_enabled) //Memory mapped registers?
		{
			effectiveVRAMstart = VGA_MMUregs_START; //Where does the window start!
			VGA_linearmemoryaddressed = 4; //Addressed!
			return 1; //Special!
		}
		return VGA_RAMEnable && (addr >= VGA_VRAM_START) && (addr < VGA_VRAM_END) && (!getActiveVGA()->precalcs.disableVGAlegacymemoryaperture); //Used when VRAM is enabled and VRAM is addressed!
	}
	return 0; //Not mapped!
}

//And now the input/output functions for segment 0xA000 (starting at offset 0)

/*

Special operations for write!

*/

OPTINLINE uint_32 ALUMaskLatchOperation(uint_32 input, uint_32 bitmask)
{
	switch (GETBITS(getActiveVGA()->registers->GraphicsRegisters.REGISTERS.DATAROTATEREGISTER, 3, 3))
	{
	case 0x00:	/* None */
		return ((input&bitmask) | (LE32(getActiveVGA()->registers->ExternalRegisters.DATALATCH.latch) &(~bitmask))); //Only apply the bitmask!
	case 0x01:	/* AND */
		return ((input|(~bitmask)) & (LE32(getActiveVGA()->registers->ExternalRegisters.DATALATCH.latch)));
	case 0x02:	/* OR */
		return ((input&bitmask) | LE32(getActiveVGA()->registers->ExternalRegisters.DATALATCH.latch));
	case 0x03:	/* XOR */
		return ((input&bitmask) ^ LE32(getActiveVGA()->registers->ExternalRegisters.DATALATCH.latch));
	default:
		//Shouldn't happen!
		break;
	};
	return input; //Shouldn't happen!
}

/*

Core read/write operations!

*/

typedef uint_32 (*VGA_WriteMode)(uint_32 data);

uint_32 VGA_WriteModeLinear(uint_32 data) //Passthrough operation!
{
	data = getActiveVGA()->ExpandTable[data]; //Make sure the data is on the all planes!
	return data; //Give the resulting data!
}

uint_32 VGA_WriteMode0(uint_32 data) //Read-Modify-Write operation!
{
	INLINEREGISTER byte curplane;
	data = (byte)ror((byte)data, GETBITS(getActiveVGA()->registers->GraphicsRegisters.REGISTERS.DATAROTATEREGISTER,0,7)); //Rotate it! Keep 8-bit data!
	data &= 0xFF; //Prevent overflow!
	data = getActiveVGA()->ExpandTable[data]; //Make sure the data is on the all planes!

	curplane = 1; //Process all 4 plane bits!
	do
	{
		if (GETBITS(getActiveVGA()->registers->GraphicsRegisters.REGISTERS.ENABLESETRESETREGISTER,0,0xF)&curplane) //Enable set/reset? (Mode 3 ignores this flag)
		{
			data = (data&(~getActiveVGA()->FillTable[curplane])) | getActiveVGA()->FillTable[GETBITS(getActiveVGA()->registers->GraphicsRegisters.REGISTERS.SETRESETREGISTER,0,0xF)&curplane]; //Turn all those bits off, and the set/reset plane ON=0xFF for the plane and OFF=0x00!
		}
		curplane <<= 1; //Next plane!
	} while (curplane!=0x10); //Only the 4 planes are used!
	data = ALUMaskLatchOperation(data, getActiveVGA()->ExpandTable[getActiveVGA()->registers->GraphicsRegisters.REGISTERS.BITMASKREGISTER]); //Execute the bitmask operation!
	return data; //Give the resulting data!
}

uint_32 VGA_WriteMode1(uint_32 data) //Video-to-video transfer
{
	return LE32(getActiveVGA()->registers->ExternalRegisters.DATALATCH.latch); //Use the latch!
}

uint_32 VGA_WriteMode2(uint_32 data) //Write color to all pixels in the source address byte of VRAM. Use Bit Mask Register.
{
	data = getActiveVGA()->FillTable[data&0xF]; //Replicate across all 4 planes to 8 bits set or cleared of their respective planes. The upper 4 bits of the CPU input are unused.
	data = ALUMaskLatchOperation(data, getActiveVGA()->ExpandTable[getActiveVGA()->registers->GraphicsRegisters.REGISTERS.BITMASKREGISTER]); //Execute the bitmask operation fully!
	return data;
}

uint_32 VGA_WriteMode3(uint_32 data) //Ignore enable set reset register!
{
	data = (byte)ror((byte)data, GETBITS(getActiveVGA()->registers->GraphicsRegisters.REGISTERS.DATAROTATEREGISTER,0,7)); //Rotate it! Keep 8-bit data!
	data &= 0xFF; //Prevent overflow!
	data = ALUMaskLatchOperation(getActiveVGA()->FillTable[GETBITS(getActiveVGA()->registers->GraphicsRegisters.REGISTERS.SETRESETREGISTER,0,0xF)], getActiveVGA()->ExpandTable[data & getActiveVGA()->registers->GraphicsRegisters.REGISTERS.BITMASKREGISTER]); //Use the generated data on the Set/Reset register
	return data;
}

uint_32 readbank = 0, writebank = 0; //Banked VRAM support!
byte VGA_forcelinearmode = 0; //Forcing linear mode?

OPTINLINE void VGA_WriteModeOperation(byte planes, uint_32 offset, byte val)
{
	static const VGA_WriteMode VGA_WRITE[4] = {VGA_WriteMode0,VGA_WriteMode1,VGA_WriteMode2,VGA_WriteMode3}; //All write modes!
	INLINEREGISTER byte curplane; //For plane loops!
	INLINEREGISTER uint_32 data; //Default to the value given!
	if (VGA_forcelinearmode) //Forced linear mode?
	{		
		data = VGA_WriteModeLinear((uint_32)val); //What write mode?
	}
	else //Normal mode?
	{
		data = VGA_WRITE[GETBITS(getActiveVGA()->registers->GraphicsRegisters.REGISTERS.GRAPHICSMODEREGISTER, 0, 3)]((uint_32)val); //What write mode?
	}

	byte planeenable = GETBITS(getActiveVGA()->registers->SequencerRegisters.REGISTERS.MAPMASKREGISTER,0,0xF); //What planes to try to write to!
	if (((getActiveVGA()->precalcs.linearmode & 5) == 5) || (getActiveVGA()->precalcs.linearmode&8) || VGA_forcelinearmode) planeenable = 0xF; //Linear memory ignores this? Or are we to ignore the Write Plane Mask(linear byte mode)?
	planeenable &= planes; //The actual planes to write to!
	byte curplanemask=1;
	curplane = 0;
	do //Process all planes!
	{
		if (planeenable&curplanemask) //Modification of the plane?
		{
			writeVRAMplane(getActiveVGA(),curplane,offset,writebank,data&0xFF,1); //Write the plane from the data!
		}
		data >>= 8; //Shift to the next plane!
		curplanemask <<= 1; //Next plane!
	} while (++curplane!=4);
}

OPTINLINE void loadlatch(uint_32 offset)
{
	getActiveVGA()->registers->ExternalRegisters.DATALATCH.latchplane[3] = readVRAMplane(getActiveVGA(), 3, offset, readbank, 1); //Plane 3!
	getActiveVGA()->registers->ExternalRegisters.DATALATCH.latchplane[2] = readVRAMplane(getActiveVGA(), 2, offset, readbank, 1); //Plane 2!
	getActiveVGA()->registers->ExternalRegisters.DATALATCH.latchplane[1] = readVRAMplane(getActiveVGA(), 1, offset, readbank, 1); //Plane 1!
	getActiveVGA()->registers->ExternalRegisters.DATALATCH.latchplane[0] = readVRAMplane(getActiveVGA(), 0, offset, readbank, 1); //Plane 0!
	VGA_updateLatches(); //Update the latch data mirroring!
}

typedef byte (*VGA_ReadMode)(byte planes, uint_32 offset);

byte VGA_ReadMode0(byte planes, uint_32 offset) //Read mode 0: Just read the normal way!
{
	INLINEREGISTER byte curplane;
	curplane = 0;
	do
	{
		if (planes&1) //Read from this plane?
		{
			return readVRAMplane(getActiveVGA(), curplane, offset,readbank,1); //Read directly from vram using the selected plane!
		}
		planes >>= 1; //Next plane!
	} while (++curplane!=4);
	return 0; //Unknown plane! Give 0!
}

byte VGA_ReadMode1(byte planes, uint_32 offset) //Read mode 1: Compare display memory with color defined by the Color Compare field. Colors Don't care field are not considered.
{
	byte dontcare;
	uint_32 result;
	dontcare = GETBITS(getActiveVGA()->registers->GraphicsRegisters.REGISTERS.COLORDONTCAREREGISTER,0,0xF); //Don't care bits!
	result = (LE32(getActiveVGA()->registers->ExternalRegisters.DATALATCH.latch)&getActiveVGA()->FillTable[dontcare])^(getActiveVGA()->FillTable[GETBITS(getActiveVGA()->registers->GraphicsRegisters.REGISTERS.COLORCOMPAREREGISTER,0,0xF)&dontcare]);
	return (byte)(~(result|(result>>8)|(result>>16)|(result>>24))); //Give the value!
}

OPTINLINE byte VGA_ReadModeOperation(byte planes, uint_32 offset)
{
	static const VGA_ReadMode READ[2] = {VGA_ReadMode0,VGA_ReadMode1}; //Read modes!
	loadlatch(offset); //Load the latches!
	if (VGA_forcelinearmode) //Forcing linear mode?
	{
		return READ[0](planes, offset); //What read mode?
	}
	return READ[GETBITS(getActiveVGA()->registers->GraphicsRegisters.REGISTERS.GRAPHICSMODEREGISTER,3,1)](planes,offset); //What read mode?
}

/*

The r/w operations from the CPU!

*/

extern byte specialdebugger; //Debugging special toggle?

char towritetext[2][256] = {"Reading","Writing"};

byte verboseVGA; //Verbose VGA dumping?

byte VGA_WriteMemoryMode=0, VGA_ReadMemoryMode=0;

byte decodingbankunfiltered = 0; //Read bank is unfiltered

void VGA_Chain4_decode(byte towrite, uint_32 offset, byte *planes, uint_32 *realoffset)
{
	INLINEREGISTER uint_32 realoffsettmp;
	INLINEREGISTER byte calcplanes;
	calcplanes = realoffsettmp = offset; //Original offset to start with!
	calcplanes &= 0x3; //Lower 2 bits determine the plane!
	*planes = (1 << calcplanes); //Give the planes to write to!
	if ((getActiveVGA()->enable_SVGA>=1) && (getActiveVGA()->enable_SVGA<=2)) //ET3000/ET4000?
	{
		realoffsettmp >>= 2; //Make sure we're linear in memory when requested! ET3000/ET4000 is different in this point! This always writes to a quarter of VRAM(since it's linear in VRAM, combined with the plane), according to the FreeVGA documentation!
	}
	else
	{
		realoffsettmp &= ~3; //Multiples of 4 won't get written on true VGA!
	}
	//We're the LG1 case of the Table 4.3.4 of the ET4000 manual!
	*realoffset = realoffsettmp; //Give the offset!
	#ifdef ENABLE_SPECIALDEBUGGER
		if (specialdebugger||verboseVGA) //Debugging special?
	#else
		if (verboseVGA) //Debugging special?
	#endif
		{
			dolog("VGA", "%s using Chain 4: Memory aperture offset %08X=Planes: %04X, Offset: %08X, VRAM offset: %08X, Bank: %08X", towritetext[towrite ? 1 : 0], offset, *planes, *realoffset, (*realoffset<<2), towrite?writebank:readbank);
		}
}

void VGA_OddEven_decode(byte towrite, uint_32 offset, byte *planes, uint_32 *realoffset)
{
	INLINEREGISTER uint_32 realoffsettmp;
	INLINEREGISTER byte calcplanes;
	calcplanes = realoffsettmp = offset; //Take the default offset!
	calcplanes &= 1; //Take 1 bit to determine the odd/even plane (odd/even)!
	if (GETBITS(getActiveVGA()->registers->GraphicsRegisters.REGISTERS.MISCGRAPHICSREGISTER,1,1)) //Replace A0 with high order bit?
	{
		realoffsettmp &= ~1; //Clear bit 0 for our result!
		if (VGA_MemoryMapSelect == 0) //128K window?
		{
			if (GETBITS(getActiveVGA()->registers->SequencerRegisters.REGISTERS.SEQUENCERMEMORYMODEREGISTER, 1, 1)) //More than 64K RAM?
			{
				realoffsettmp |= ((offset >> 16) & 1); //Bit 16 becomes bit A0 for VRAM!
			}
			else //64K RAM mode?
			{
				realoffsettmp |= ((offset >> 14) & 1); //Bit 14 becomes bit A0 for VRAM!
			}
		}
		else //Misc Output Register High/Low page becomes A0 for VRAM!
		{
			realoffsettmp |= GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER, 5, 1); //Apply high page if needed!
			realoffsettmp ^= 1; //Somehow this bit is swapped?
		}
	}
	//Otherwise, CPU A0 becomes VRAM A0?
	if (decodingbankunfiltered == 0) //Not unfiltered?
	{
		writebank <<= 1; //Shift to it's position!
		writebank &= (0xE0000|getActiveVGA()->precalcs.extraSegmentSelectLines); //3/5 bits only!
		readbank <<= 1; //Shift to it's postion!
		readbank &= (0xE0000|getActiveVGA()->precalcs.extraSegmentSelectLines); //3/5 bits only!
	}
	*realoffset = realoffsettmp; //Give the calculated offset!
	*planes = (0x5 << calcplanes); //Convert to used plane (0&2 or 1&3)!
	if (towrite == 0) //Read?
	{
		if (!GETBITS(getActiveVGA()->registers->GraphicsRegisters.REGISTERS.READMAPSELECTREGISTER, 1, 1)) //Lower plane?
		{
			*planes &= 0x3; //Lower planes only!
		}
		else //Upper plane?
		{
			*planes &= 0xC; //Upper planes only!
		}
	}
	#ifdef ENABLE_SPECIALDEBUGGER
		if (specialdebugger||verboseVGA) //Debugging special?
	#else
		if (verboseVGA) //Debugging special?
	#endif
		{
			dolog("VGA", "%s using Odd/Even: Memory aperture offset %08X=Planes: %04X, Offset: %08X, VRAM offset: %08X, Bank: %08X", towritetext[towrite ? 1 : 0], offset, *planes, *realoffset, (*realoffset<<2), towrite?writebank:readbank);
		}
}

void VGA_Planar_decode(byte towrite, uint_32 offset, byte *planes, uint_32 *realoffset)
{
	INLINEREGISTER byte calcplanes;
	if (towrite) //Writing access?
	{
		calcplanes = 0xF; //Write to all planes possible, map mask register does the rest!
	}
	else if ((getActiveVGA()->precalcs.linearmode & 5) == 5) //Linear memory?
	{
		calcplanes = 0xF; //Read map select is ignored!
	}
	else //Normal VGA read!
	{
		calcplanes = 1; //Load plane 0!
		calcplanes <<= GETBITS(getActiveVGA()->registers->GraphicsRegisters.REGISTERS.READMAPSELECTREGISTER,0,3); //Take this plane!
	}
	//Apply new bank base for this mode!
	if (decodingbankunfiltered == 0) //Not unfiltered?
	{
		writebank <<= 2; //Shift to it's position!
		writebank &= (0xC0000|getActiveVGA()->precalcs.extraSegmentSelectLines); //2 bits only and extra lines if required!
		readbank <<= 2; //Shift to it's postion!
		readbank &= (0xC0000|getActiveVGA()->precalcs.extraSegmentSelectLines); //2 bits only!
	}
	*planes = calcplanes; //The planes to apply!
	*realoffset = offset; //Load the offset directly!
	//Use planar mode!
	#ifdef ENABLE_SPECIALDEBUGGER
		if (specialdebugger||verboseVGA) //Debugging special?
	#else
		if (verboseVGA) //Debugging special?
	#endif
		{
			dolog("VGA", "%s using Planar access: Memory aperture offset %08X=Planes: %04X, Offset: %08X, VRAM offset: %08X, Bank: %08X", towritetext[towrite ? 1 : 0], offset, *planes, *realoffset, (*realoffset<<2), towrite?writebank:readbank);
		}
}

void SVGA_LinearContinuous_decode(byte towrite, uint_32 offset, byte *planes, uint_32 *realoffset)
{
	INLINEREGISTER uint_32 realoffsettmp;
	INLINEREGISTER byte calcplanes;
	calcplanes = realoffsettmp = offset; //Original offset to start with!
	calcplanes &= 0x3; //Lower 2 bits determine the plane(ascending VRAM memory blocks of 4 bytes)!
	*planes = (1 << calcplanes); //Give the planes to write to!
	realoffsettmp >>= 2; //Rest of bits determine the direct index!
	*realoffset = realoffsettmp; //Give the offset!
}

typedef void (*decodeCPUaddressMode)(byte towrite, uint_32 offset, byte *planes, uint_32 *realoffset); //Decode addressing mode typedef!

decodeCPUaddressMode decodeCPUAddressW = VGA_OddEven_decode, decodeCPUAddressR=VGA_OddEven_decode; //Our current MMU decoder for reads and writes!

//First, direct decoding of banked used mode and linear mode addresses for extended chipsets!
OPTINLINE void directdecodeCPUaddressBanked(byte towrite, uint_32 offset, byte* planes, uint_32* realoffset, uint_32 bank)
{
	VGA_forcelinearmode = 0; //Not forcing linear mode!
	readbank = writebank = bank; //Direct banks are used!

	//Calculate according to the mode in our table and write/read memory mode!
	if (towrite) //Writing?
	{
		decodeCPUAddressW(towrite, offset, planes, realoffset); //Apply the write memory mode!
	}
	else //Reading?
	{
		decodeCPUAddressR(towrite, offset, planes, realoffset); //Apply the read memory mode!
	}
}

OPTINLINE void lineardecodeCPUaddressBanked(byte towrite, uint_32 offset, byte* planes, uint_32* realoffset, uint_32 bank)
{
	VGA_forcelinearmode = 1; //Forcing linear mode!
	readbank = writebank = bank; //Direct banks are used!

	//Calculate according to the mode in our table and write/read memory mode!
	SVGA_LinearContinuous_decode(towrite, offset, planes, realoffset); //Apply the read memory mode!
}

//Normal decoding algorithm as specified for VRAM on any chipset
//decodeCPUaddress(Write from CPU=1; Read from CPU=0, offset (from VRAM start address), planes to read/write (4-bit mask), offset to read/write within the plane(s)).
OPTINLINE void decodeCPUaddress(byte towrite, uint_32 offset, byte *planes, uint_32 *realoffset)
{
	VGA_forcelinearmode = 0; //Not forcing linear mode!
	//Apply bank when used!
	if ((getActiveVGA()->precalcs.linearmode&4)==4) //Enable SVGA Normal segmented read/write bank mode support?
	{
		if (getActiveVGA()->precalcs.linearmode & 2) //Use high 4 bits as address!
		{
			if ((getActiveVGA()->enable_SVGA == 1) && getActiveVGA()->precalcs.linearmemorymask) //ET4000/W32 chip high memory address enabled?
			{
				if (VGA_linearmemoryaddressed) //Enabled and addressed?
				{
					readbank = writebank = (offset & 0x3F0000); //Apply read/write bank from the high 6 bits that's unused for a 4MB offset!
				}
				else //Not addressed and enabled? ET4000 compatibility?
				{
					readbank = writebank = (offset & 0xF0000); //Apply read/write bank from the high 4 bits that's unused!
				}
			}
			else //Compatibility mode?
			{
				readbank = writebank = (offset & 0xF0000); //Apply read/write bank from the high 4 bits that's unused!
			}
		}
		else //Use bank select?
		{
			readbank = VGA_MemoryMapBankRead; //Read bank
			writebank = VGA_MemoryMapBankWrite; //Write bank
		}
		//Apply the segmented VGA mode like any normal VGA!
	}
	else readbank = writebank = 0; //No memory banks are used!

	//Calculate according to the mode in our table and write/read memory mode!
	if (towrite) //Writing?
	{
		decodeCPUAddressW(towrite,offset,planes,realoffset); //Apply the write memory mode!
	}
	else //Reading?
	{
		decodeCPUAddressR(towrite,offset,planes,realoffset); //Apply the read memory mode!
	}
}

void updateVGAMMUAddressMode(VGA_Type *VGA)
{
	static const decodeCPUaddressMode decodeCPUaddressmode[4] = {VGA_Planar_decode,VGA_Chain4_decode,VGA_OddEven_decode,SVGA_LinearContinuous_decode}; //All decode modes supported!
	decodeCPUAddressW = decodeCPUaddressmode[VGA_WriteMemoryMode&3]; //Apply the Write memory mode!
	decodeCPUAddressR = decodeCPUaddressmode[VGA_ReadMemoryMode&3]; //Apply the Read memory mode!
	VGA_MMU012_START_linear = VGA->precalcs.linearmemorysize; //Where the 0-2 MMU 8KB blocks and the ones following it start!
	VGA_MMUregs_START_linear = (VGA->precalcs.linearmemorysize & 0x300000) ? 0x3FFF00 : 0xFFF00; //Where the memory mapped registers start!
	VGA_MMU012_END_linear = (VGA->precalcs.linearmemorysize & 0x300000) ? 0x3F0000 : 0xF0000; //Where the external mapped registers end!
	VGA_MMU012_enabled = VGA->precalcs.MMU012_enabled; //MMU0-2 Enabled?
	VGA_MMUregs_enabled = VGA->precalcs.MMUregs_enabled; //MMU regs Enabled?
}

byte planes; //What planes to affect!
uint_32 realoffset; //What offset to affect!

extern byte useIPSclock; //Are we using the IPS clock instead of cycle accurate clock?

void applyCGAMDAOffset(byte CPUtiming, uint_32 *offset)
{
	if (CGAEMULATION_ENABLED(getActiveVGA())) //CGA?
	{
		*offset &= 0x3FFF; //Wrap around 16KB!

		//Apply wait states(except when using the IPS clock)!
		if ((CPU[activeCPU].running==1) && (useIPSclock==0) && CPUtiming) //Are we running? Introduce wait states! Don't allow wait states when using the IPS clock: it will crash because the instruction is never finished, thus never allowing the video adapter emulation to finish the wait state!
		{
			getActiveVGA()->WaitState = 1; //Start our waitstate for CGA memory access!
			getActiveVGA()->WaitStateCounter = 8; //Reset our counter for the 8 hdots to wait!
			CPU[activeCPU].halt |= 4; //We're starting to wait for the CGA!
			updateVGAWaitState(); //Update the current waitstate!
		}
	}
	else if (MDAEMULATION_ENABLED(getActiveVGA())) //MDA?
	{
		*offset &= 0xFFF; //Wrap around 4KB!
	}
}

byte extVGA_isnotVRAM(uint_32 offset)
{
	return !is_A000VRAM(offset); //Isn't VRAM and within range?
}


byte MMUblock; //What block is addressed for MMU0-2?
byte bit8read;
extern uint_64 memory_dataread[2];
extern byte memory_datasize[2]; //The size of the data that has been read!
byte VGAmemIO_rb(uint_32 offset, byte index)
{
	if (unlikely(is_A000VRAM(offset))) //VRAM and within range?
	{
		offset -= effectiveVRAMstart; //Calculate start offset into VRAM!

		if (VGA_linearmemoryaddressed) //Special memory addressed?
		{
			if (VGA_linearmemoryaddressed == 1) //Linear address map?
			{
				lineardecodeCPUaddressBanked(0, offset, &planes, &realoffset, 0); //Our VRAM offset starting from the 32-bit offset (A0000 etc.)!
				goto readdatacurrentmode; //Apply the operation on read mode!
			}
			else if (VGA_linearmemoryaddressed == 4) //MMU registers?
			{
				if (Tseng4k_readMMUregister(offset, &bit8read))
				{
					memory_dataread[0] = bit8read; //What is read!
					memory_datasize[(index >> 5) & 1] = 1; //Only 1 byte chunks can be read!
					return 1; //Handled!
				}
			}
			else if (VGA_linearmemoryaddressed == 2) //MMU 0-2?
			{
				if (offset >= (VGA_MMU012_blocksize << 1)) //MMU 2?
				{
					offset -= (VGA_MMU012_blocksize << 1); //Convert to relative offset within the block!
					if (getActiveVGA()->precalcs.MMU2_aperture_linear & 2) //Accelerator mode?
					{
						if (Tseng4k_readMMUaccelerator(2, offset, &bit8read)) //Read?
						{
							memory_dataread[0] = bit8read; //What is read!
							memory_datasize[(index >> 5) & 1] = 1; //Only 1 byte chunks can be read!
							return 1; //G
						}
						else //Floating bus?
						{
							memory_dataread[0] = 0xFF; //Unsupported!
							memory_datasize[(index >> 5) & 1] = 1; //Only 1 byte chunks can be read!
							return 1; //Unsupported!
						}
					}
					if (getActiveVGA()->precalcs.MMU2_aperture_linear) //Linear mode?
					{
						lineardecodeCPUaddressBanked(0, offset, &planes, &realoffset, getActiveVGA()->precalcs.MMU012_aperture[2]); //Our VRAM offset starting from the 32-bit offset (A0000 etc.)!
						goto readdatacurrentmode; //Apply the operation on read mode!
					}
					else //According to current display mode?
					{
						directdecodeCPUaddressBanked(0, offset, &planes, &realoffset, getActiveVGA()->precalcs.MMU012_aperture[2]); //Our VRAM offset starting from the 32-bit offset (A0000 etc.)!
						goto readdatacurrentmode; //Apply the operation on read mode!
					}
				}
				else if (offset >= VGA_MMU012_blocksize) //MMU 1?
				{
					offset -= VGA_MMU012_blocksize; //Convert to relative offset within the block!
					if (getActiveVGA()->precalcs.MMU1_aperture_linear & 2) //Accelerator mode?
					{
						if (Tseng4k_readMMUaccelerator(1, offset, &bit8read)) //Read?
						{
							memory_dataread[0] = bit8read; //What is read!
							memory_datasize[(index >> 5) & 1] = 1; //Only 1 byte chunks can be read!
							return 1; //G
						}
						else //Floating bus?
						{
							memory_dataread[0] = 0xFF; //Unsupported!
							memory_datasize[(index >> 5) & 1] = 1; //Only 1 byte chunks can be read!
							return 1; //Unsupported!
						}
					}
					if (getActiveVGA()->precalcs.MMU1_aperture_linear) //Linear mode?
					{
						lineardecodeCPUaddressBanked(0, offset, &planes, &realoffset, getActiveVGA()->precalcs.MMU012_aperture[1]); //Our VRAM offset starting from the 32-bit offset (A0000 etc.)!
						goto readdatacurrentmode; //Apply the operation on read mode!
					}
					else //According to current display mode?
					{
						directdecodeCPUaddressBanked(0, offset, &planes, &realoffset, getActiveVGA()->precalcs.MMU012_aperture[1]); //Our VRAM offset starting from the 32-bit offset (A0000 etc.)!
						goto readdatacurrentmode; //Apply the operation on read mode!
					}
				}
				else //MMU 0?
				{
					if (getActiveVGA()->precalcs.MMU0_aperture_linear & 2) //Accelerator mode?
					{
						if (Tseng4k_readMMUaccelerator(0, offset, &bit8read)) //Read?
						{
							memory_dataread[0] = bit8read; //What is read!
							memory_datasize[(index >> 5) & 1] = 1; //Only 1 byte chunks can be read!
							return 1; //G
						}
						else //Floating bus?
						{
							memory_dataread[0] = 0xFF; //Unsupported!
							memory_datasize[(index >> 5) & 1] = 1; //Only 1 byte chunks can be read!
							return 1; //Unsupported!
						}
					}
					if (getActiveVGA()->precalcs.MMU0_aperture_linear) //Linear mode?
					{
						lineardecodeCPUaddressBanked(0, offset, &planes, &realoffset, getActiveVGA()->precalcs.MMU012_aperture[0]); //Our VRAM offset starting from the 32-bit offset (A0000 etc.)!
						goto readdatacurrentmode; //Apply the operation on read mode!
					}
					else //According to current display mode?
					{
						directdecodeCPUaddressBanked(0, offset, &planes, &realoffset, getActiveVGA()->precalcs.MMU012_aperture[0]); //Our VRAM offset starting from the 32-bit offset (A0000 etc.)!
						goto readdatacurrentmode; //Apply the operation on read mode!
					}
				}
			}
			else if (VGA_linearmemoryaddressed == 5) //Image Port?
			{
				//Calculate the linear offset using linear addresses, replace offset with the linear address to use(minus the starting address), converting between formats!
				//Unknown how to handle interlacing?
				offset = SAFEMOD(offset, getActiveVGA()->precalcs.imageport_transferlength) + ((SAFEDIV(offset, getActiveVGA()->precalcs.imageport_transferlength) * getActiveVGA()->precalcs.imageport_rowoffset)<<getActiveVGA()->precalcs.imageport_interlace); //Convert between original lengths vs destination row length!
				//IXOF input adds 1 scanline in VRAM during interlaced operation?
				lineardecodeCPUaddressBanked(0, offset, &planes, &realoffset, getActiveVGA()->precalcs.imageport_startingaddress); //Our VRAM offset starting from the 32-bit offset (A0000 etc.)!
				goto readdatacurrentmode; //Apply the operation on read mode!
			}
			//External registers?
			return 0; //Unmapped!
		}
		applyCGAMDAOffset(1,&offset); //Apply CGA/MDA offset if needed!
		decodeCPUaddress(0, offset, &planes, &realoffset); //Our VRAM offset starting from the 32-bit offset (A0000 etc.)!
		readdatacurrentmode:
		memory_dataread[0] = VGA_ReadModeOperation(planes, realoffset); //Apply the operation on read mode!
		if (CGAEMULATION_ENABLED(getActiveVGA())||MDAEMULATION_ENABLED(getActiveVGA())) //Unchanged mapping?
		{
			memory_dataread[0] = getActiveVGA()->CGAMDAShadowRAM[offset]; //Read from shadow RAM!
		}
		memory_datasize[(index >> 5) & 1] = 1; //Only 1 byte chunks can be read!
		return 1; //Read!
	}
	return 0; //Not read!
}

void CGAMDA_doWriteRAMrefresh(uint_32 offset)
{
	applyCGAMDAOffset(0,&offset); //Apply CGA/MDA offset if needed!
	decodeCPUaddress(1, offset, &planes, &realoffset); //Our VRAM offset starting from the 32-bit offset (A0000 etc.)!
	VGA_WriteModeOperation(planes, realoffset, getActiveVGA()->CGAMDAShadowRAM[offset]); //Apply the operation on write mode!
}

extern byte memory_datawrittensize; //How many bytes have been written to memory during a write!
byte VGAmemIO_wb(uint_32 offset, byte value)
{
	if (unlikely(is_A000VRAM(offset))) //VRAM and within range?
	{
		offset -= effectiveVRAMstart; //Calculate start offset into VRAM!
		if (VGA_linearmemoryaddressed) //Special memory addressed?
		{
			if (VGA_linearmemoryaddressed == 1) //Linear mode?
			{
				lineardecodeCPUaddressBanked(1, offset, &planes, &realoffset, 0); //Our VRAM offset starting from the 32-bit offset (A0000 etc.)!
				goto writedatacurrentmode; //Apply the operation on read mode!
			}
			else if (VGA_linearmemoryaddressed == 4) //MMU registers?
			{
				if (Tseng4k_writeMMUregister(offset, value))
				{
					memory_datawrittensize = 1; //Only 1 byte written!
					return 1; //Handled!
				}
			}
			else if (VGA_linearmemoryaddressed == 2) //MMU 0-2?
			{
				if (offset >= (VGA_MMU012_blocksize << 1)) //MMU 2?
				{
					offset -= (VGA_MMU012_blocksize << 1); //Convert to relative offset within the block!
					if (getActiveVGA()->precalcs.MMU2_aperture_linear & 2) //Accelerator mode?
					{
						if (Tseng4k_writeMMUaccelerator(2, offset, value)) //Read?
						{
							memory_datawrittensize = 1; //Only 1 byte written!
							return 1; //Handled!
						}
						return 1; //Unmapped!
					}
					if (getActiveVGA()->precalcs.MMU2_aperture_linear) //Linear mode?
					{
						lineardecodeCPUaddressBanked(1, offset, &planes, &realoffset, getActiveVGA()->precalcs.MMU012_aperture[2]); //Our VRAM offset starting from the 32-bit offset (A0000 etc.)!
						goto writedatacurrentmode; //Apply the operation on read mode!
					}
					else //According to current display mode?
					{
						directdecodeCPUaddressBanked(1, offset, &planes, &realoffset, getActiveVGA()->precalcs.MMU012_aperture[2]); //Our VRAM offset starting from the 32-bit offset (A0000 etc.)!
						goto writedatacurrentmode; //Apply the operation on read mode!
					}
				}
				else if (offset >= VGA_MMU012_blocksize) //MMU 1?
				{
					offset -= VGA_MMU012_blocksize; //Convert to relative offset within the block!
					if (getActiveVGA()->precalcs.MMU1_aperture_linear & 2) //Accelerator mode?
					{
						if (Tseng4k_writeMMUaccelerator(1, offset, value)) //Read?
						{
							memory_datawrittensize = 1; //Only 1 byte written!
							return 1; //Handled!
						}
						return 1; //Unmapped!
					}
					if (getActiveVGA()->precalcs.MMU1_aperture_linear) //Linear mode?
					{
						lineardecodeCPUaddressBanked(1, offset, &planes, &realoffset, getActiveVGA()->precalcs.MMU012_aperture[1]); //Our VRAM offset starting from the 32-bit offset (A0000 etc.)!
						goto writedatacurrentmode; //Apply the operation on read mode!
					}
					else //According to current display mode?
					{
						directdecodeCPUaddressBanked(1, offset, &planes, &realoffset, getActiveVGA()->precalcs.MMU012_aperture[1]); //Our VRAM offset starting from the 32-bit offset (A0000 etc.)!
						goto writedatacurrentmode; //Apply the operation on read mode!
					}
				}
				else //MMU 0?
				{
					if (getActiveVGA()->precalcs.MMU0_aperture_linear & 2) //Accelerator mode?
					{
						if (Tseng4k_writeMMUaccelerator(0, offset, value)) //Read?
						{
							memory_datawrittensize = 1; //Only 1 byte written!
							return 1; //Handled!
						}
						return 1; //Unmapped!
					}
					if (getActiveVGA()->precalcs.MMU0_aperture_linear) //Linear mode?
					{
						lineardecodeCPUaddressBanked(1, offset, &planes, &realoffset, getActiveVGA()->precalcs.MMU012_aperture[0]); //Our VRAM offset starting from the 32-bit offset (A0000 etc.)!
						goto writedatacurrentmode; //Apply the operation on read mode!
					}
					else //According to current display mode?
					{
						directdecodeCPUaddressBanked(1, offset, &planes, &realoffset, getActiveVGA()->precalcs.MMU012_aperture[0]); //Our VRAM offset starting from the 32-bit offset (A0000 etc.)!
						goto writedatacurrentmode; //Apply the operation on read mode!
					}
				}
			}
			else if (VGA_linearmemoryaddressed == 5) //Image Port?
			{
				//Calculate the linear offset using linear addresses, replace offset with the linear address to use(minus the starting address), converting between formats!
				//Unknown how to handle interlacing?
				offset = SAFEMOD(offset, getActiveVGA()->precalcs.imageport_transferlength) + ((SAFEDIV(offset, getActiveVGA()->precalcs.imageport_transferlength) * getActiveVGA()->precalcs.imageport_rowoffset)<<getActiveVGA()->precalcs.imageport_interlace); //Convert between original lengths vs destination row length!
				//IXOF input adds 1 scanline in VRAM during interlaced operation?
				lineardecodeCPUaddressBanked(1, offset, &planes, &realoffset, getActiveVGA()->precalcs.imageport_startingaddress); //Our VRAM offset starting from the 32-bit offset (A0000 etc.)!
				goto writedatacurrentmode; //Apply the operation on read mode!
			}
			//External registers?
			return 0; //Unmapped!
		}
		applyCGAMDAOffset(1,&offset); //Apply CGA/MDA offset if needed!
		decodeCPUaddress(1, offset, &planes, &realoffset); //Our VRAM offset starting from the 32-bit offset (A0000 etc.)!
		writedatacurrentmode:
		VGA_WriteModeOperation(planes, realoffset, value); //Apply the operation on write mode!
		if (CGAEMULATION_ENABLED(getActiveVGA())||MDAEMULATION_ENABLED(getActiveVGA())) //Unchanged mapping?
		{
			getActiveVGA()->CGAMDAShadowRAM[offset] = value; //Write to shadow RAM!
		}
		memory_datawrittensize = 1; //Only 1 byte written!
		return 1; //Written!
	}
	return 0; //Not written!
}

void VGAmemIO_reset()
{
	//Done directly by the MMU, since we're always present!
}
