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

#include "headers/cpu/mmu.h" //Ourselves!
#include "headers/cpu/cpu.h"
#include "headers/cpu/paging.h" //Paging functions!
#include "headers/support/log.h" //Logging support!
#include "headers/cpu/protection.h" //Protection support!
#include "headers/cpu/paging.h" //Protection support!
#include "headers/emu/debugger/debugger.h" //Debugger support for logging MMU accesses!
#include "headers/hardware/xtexpansionunit.h" //XT expansion unit support!
#include "headers/mmu/mmu_internals.h" //Internal MMU call support!
#include "headers/mmu/mmuhandler.h" //MMU direct handler support!
#include "headers/cpu/protecteddebugging.h" //Protected mode debugging support!
#include "headers/cpu/biu.h" //BIU support!
#include "headers/cpu/easyregs.h" //Easy register support!

extern MMU_type MMU; //MMU itself!

//Are we disabled?
#define __HW_DISABLED 0

#define CPU286_WAITSTATE_DELAY 1

byte memory_BIUdirectrb(uint_64 realaddress) //Direct read from real memory (with real data direct)!
{
	return BIU_directrb_external(realaddress, 0x100);
}
OPTINLINE byte memory_BIUdirectrbIndex(uint_64 realaddress, word index) //Direct read from real memory (with real data direct)!
{
	return BIU_directrb_external(realaddress, index);
}
word memory_BIUdirectrw(uint_64 realaddress) //Direct read from real memory (with real data direct)!
{
	return BIU_directrw(realaddress, 0x100);
}
uint_32 memory_BIUdirectrdw(uint_64 realaddress) //Direct read from real memory (with real data direct)!
{
	return BIU_directrdw(realaddress, 0x100);
}
void memory_BIUdirectwb(uint_64 realaddress, byte value) //Direct write to real memory (with real data direct)!
{
	BIU_directwb_external(realaddress, value, 0x100);
}
OPTINLINE void memory_BIUdirectwbIndex(uint_64 realaddress, byte value, word index) //Direct write to real memory (with real data direct)!
{
	BIU_directwb_external(realaddress, value, index);
}
void memory_BIUdirectww(uint_64 realaddress, word value) //Direct write to real memory (with real data direct)!
{
	BIU_directww(realaddress, value, 0x100);
}
void memory_BIUdirectwdw(uint_64 realaddress, uint_32 value) //Direct write to real memory (with real data direct)!
{
	BIU_directwdw(realaddress, value, 0x100);
}


//Pointer support (real mode only)!
void *MMU_directptr(uint_32 address, uint_32 size) //Full ptr to real MMU memory!
{
	if ((address < MMU.size) && ((address + size) <= MMU.size)) //Within our limits of flat memory and not paged?
	{
		return &MMU.memory[address]; //Give the memory's start!
	}

	//Don't signal invalid memory address: we're internal emulator call!
	return NULL; //Not found!	
}

//MMU_ptr: 80(1)86 only!
void *MMU_ptr(sword segdesc, word segment, uint_32 offset, byte forreading, uint_32 size) //Gives direct memory pointer!
{
	uint_32 realaddr;
	if (MMU.memory == NULL) //None?
	{
		return NULL; //NULL: no memory alligned!
	}

	if (EMULATED_CPU <= CPU_NECV30) //-NEC V20/V30 wraps offset arround?
	{
		offset &= 0xFFFF; //Wrap arround!
	}
	else
	{
		return NULL; //80286+ isn't supported here!
	}
	realaddr = (segment << 4) + offset; //Our real address!
	realaddr = BITOFF(realaddr, 0x100000); //Wrap arround, disable A20!	
	return MMU_directptr(realaddr, size); //Direct pointer!
}

uint_32 addresswrapping[16] = { //-NEC V20/V30 wraps offset arround 64kB? NEC V20/V30 allows 1 byte more in word operations! index: Bit0=Address 0x10000, Bit1+=Emulated CPU
							0xFFFF, //8086
							0xFFFF, //8086 0x10000
							0xFFFF, //80186
							0x1FFFF, //80186 0x10000
							0xFFFFFFFF, //80286+
							0xFFFFFFFF, //80286+
							0xFFFFFFFF, //80386+
							0xFFFFFFFF, //80386+
							0xFFFFFFFF, //80486+
							0xFFFFFFFF, //80486+
							0xFFFFFFFF, //80586+
							0xFFFFFFFF, //80586+
							0xFFFFFFFF, //80686+
							0xFFFFFFFF, //80686+
							0xFFFFFFFF, //80786+
							0xFFFFFFFF //80786+
}; //Address wrapping lookup table!

//Some prototypes for definition!
uint_32 MMU_realaddr186(sword segdesc, word segment, uint_32 offset, byte wordop, byte is_offset16); //Real adress?
uint_32 MMU_realaddr186CS(sword segdesc, word segment, uint_32 offset, byte wordop, byte is_offset16); //Real adress?
uint_32 MMU_realaddrGeneric(sword segdesc, word segment, uint_32 offset, byte wordop, byte is_offset16); //Real adress?
uint_32 MMU_realaddrGenericCS(sword segdesc, word segment, uint_32 offset, byte wordop, byte is_offset16); //Real adress?

byte applyspecialaddress;
uint_32* addresswrappingbase = &addresswrapping[0];
MMU_realaddrHandler realaddrHandlers[2] = {MMU_realaddrGeneric,MMU_realaddr186};
MMU_realaddrHandler realaddrCSHandlers[2] = { MMU_realaddrGenericCS,MMU_realaddr186CS };
MMU_realaddrHandler realaddrHandler = &MMU_realaddrGeneric;
MMU_realaddrHandler realaddrHandlerCS = &MMU_realaddrGenericCS;
uint_32 addresswrappingbase_simple=0xFFFFFFFF;

void MMU_determineAddressWrapping()
{
	addresswrappingbase = &addresswrapping[(EMULATED_CPU << 1)]; //The base for address wrapping!
	applyspecialaddress = (EMULATED_CPU == CPU_NECV30); //Apply a special base with 2 entries?
	realaddrHandler = realaddrHandlers[applyspecialaddress & 1]; //To apply the special addressing mode? For generic segmentation!
	realaddrHandlerCS = realaddrCSHandlers[applyspecialaddress & 1]; //To apply the special addressing mode? For CS only!
	addresswrappingbase_simple = *addresswrappingbase; //Simple version!
}

//Address translation routine.
//segdesc: >=0=use it's descriptor for segment #x, -1=No descriptor, use paging, -2=Direct physical memory access, -3=Use special ES, calculated in a 8086 way
uint_32 MMU_realaddr186(sword segdesc, word segment, uint_32 offset, byte wordop, byte is_offset16) //Real adress?
{
	INLINEREGISTER uint_32 realaddress;

	CPU[activeCPU].writeword = 0; //Reset word-write flag for checking next bytes!
	realaddress = offset; //Load the address!
	realaddress &= addresswrappingbase[(((realaddress == 0x10000) && wordop) & 1)]; //Apply the correct wrapping!

	if (likely(segdesc>=0)) //Not using the actual literal value?
	{
		return realaddress + (uint_32)CPU_MMU_start(segdesc, segment);
	}
	else if (segdesc==-3) //Special?
	{
		return realaddress + (REG_ES<<4); //Apply literal address!
	}

	return realaddress; //Give original adress!
}

uint_32 MMU_realaddr186CS(sword segdesc, word segment, uint_32 offset, byte wordop, byte is_offset16) //Real adress?
{
	INLINEREGISTER uint_32 realaddress;

	CPU[activeCPU].writeword = 0; //Reset word-write flag for checking next bytes!
	realaddress = offset; //Load the address!
	realaddress &= addresswrappingbase[(((realaddress == 0x10000) && wordop) & 1)]; //Apply the correct wrapping!

	return realaddress + (uint_32)CPU_MMU_startCS(CPU_SEGMENT_CS);
}

uint_32 MMU_realaddrGeneric(sword segdesc, word segment, uint_32 offset, byte wordop, byte is_offset16) //Real adress?
{
	INLINEREGISTER uint_32 realaddress;

	CPU[activeCPU].writeword = 0; //Reset word-write flag for checking next bytes!
	realaddress = offset; //Load the address!
	realaddress &= addresswrappingbase_simple; //Apply address wrapping for the CPU offset, when needed!

	if (likely(segdesc >= 0)) //Not using the actual literal value?
	{
		return realaddress + (uint_32)CPU_MMU_start(segdesc, segment);
	}
	else if (segdesc == -3) //Special?
	{
		return realaddress + (REG_ES << 4); //Apply literal address!
	}

	return realaddress; //Give original adress!
}

uint_32 MMU_realaddrGenericCS(sword segdesc, word segment, uint_32 offset, byte wordop, byte is_offset16) //Real adress?
{
	INLINEREGISTER uint_32 realaddress;

	CPU[activeCPU].writeword = 0; //Reset word-write flag for checking next bytes!
	realaddress = offset; //Load the address!
	realaddress &= addresswrappingbase_simple; //Apply address wrapping for the CPU offset, when needed!

	return realaddress + (uint_32)CPU_MMU_startCS(CPU_SEGMENT_CS);
}

uint_32 MMU_realaddr(sword segdesc, word segment, uint_32 offset, byte wordop, byte is_offset16) //Real adress?
{
	return realaddrHandler(segdesc, segment, offset, wordop, is_offset16); //Passthrough!
}

uint_32 MMU_realaddrCS(sword segdesc, word segment, uint_32 offset, byte wordop, byte is_offset16) //Real adress?
{
	return realaddrHandlerCS(segdesc, segment, offset, wordop, is_offset16); //Passthrough!
}

uint_32 MMU_realaddrPhys(SEGMENT_DESCRIPTOR *segdesc, word segment, uint_32 offset, byte wordop, byte is_offset16) //Real adress?
{
	INLINEREGISTER uint_32 realaddress;

	CPU[activeCPU].writeword = 0; //Reset word-write flag for checking next bytes!
	realaddress = offset; //Load the address!
	if (likely(applyspecialaddress))
	{
		realaddress &= addresswrappingbase[(((realaddress == 0x10000) && wordop) & 1)]; //Apply the correct wrapping!
	}
	else
	{
		realaddress &= addresswrappingbase_simple; //Apply address wrapping for the CPU offset, when needed!
	}

	realaddress += (uint_32)CPU_MMU_startPhys(segdesc, segment);

	//We work!
	return realaddress; //Give real adress!
}

uint_32 BUSdatalatch=0;

void processBUS(uint_32 address, byte index, byte data)
{
	uint_32 masks[4] = {0xFF,0xFF00,0xFF0000,0xFF000000};
	BUSdatalatch &= ~masks[index&3]; //Clear the bits!
	BUSdatalatch |= (data<<((index&3)<<3)); //Set the bits on the BUS!
	latchBUS(address,BUSdatalatch); //This address is to be latched!
}

//OPcodes for the debugger!
void MMU_addOP(byte data)
{
	if (CPU[activeCPU].OPlength < sizeof(CPU[activeCPU].OPbuffer)) //Not finished yet?
	{
		CPU[activeCPU].OPbuffer[CPU[activeCPU].OPlength++] = data; //Save a part of the opcode!
	}
}

void MMU_clearOP()
{
	CPU[activeCPU].OPlength = 0; //Clear the buffer!
}

//CPU/EMU simple memory access routine.

byte checkDirectMMUaccess(uint_32 realaddress, word readflags, byte CPL)
{
	byte result;
	//Check for Page Faults!
	if (likely(is_paging()==0)) //Are we not paging?
	{
		return 0; //OK
	}
	result = CPU_Paging_checkPage(realaddress, readflags, CPL); //Map it using the paging mechanism! Errored out or waiting to page in?
	if (unlikely(result==1)) //Map it using the paging mechanism! Errored out?
	{
		return 1; //Error out!
	}
	return result; //OK or waiting to page in!
}

uint_32 checkMMUaccess_linearaddr; //Saved linear address for the BIU to use!
//readflags = 1|(opcode<<1) for reads! 0 for writes! Bit 4: Disable segmentation check, Bit 5: Disable debugger check, Bit 6: Disable paging check, Bit 8=Disable paging faults.
byte checkMMUaccess(sword segdesc, word segment, uint_64 offset, word readflags, byte CPL, byte is_offset16, byte subbyte) //Check if a byte address is invalid to read/write for a purpose! Used in all CPU modes! Subbyte is used for alignment checking!
{
	byte result;
	static byte debuggertype[4] = {PROTECTEDMODEDEBUGGER_TYPE_DATAWRITE,PROTECTEDMODEDEBUGGER_TYPE_DATAREAD,0xFF,PROTECTEDMODEDEBUGGER_TYPE_EXECUTION};
	static byte alignmentrequirement[8] = {0,1,3,0,7,0,0,0}; //What address bits can't be set for byte 0! Index=subbyte bit 3,4,5! Bits 0-2 must be 0! Value 0 means any alignment!
	INLINEREGISTER byte dt;
	INLINEREGISTER uint_32 realaddress;
	if (EMULATED_CPU<=CPU_NECV30) return 0; //No checks are done in the old processors!

	//Create a linear address first!
	realaddress = MMU_realaddr(segdesc, segment, (uint_32)offset, 0,is_offset16); //Real adress!

	if ((readflags & 0x80) == 0) //Allow basic segmentation checks?
	{
		if (CPU[activeCPU].is_aligning && (segdesc >= 0) && (CPL == 3)) //Apply #AC?
		{
			if (unlikely(((subbyte&7)==0) && (realaddress & alignmentrequirement[(subbyte>>3)&7]))) //Aligment enforced and wrong? Don't apply on internal accesses!
			{
				CPU_AC(0); //Alignment WORD/DWORD/QWORD check fault!
				return 1; //Error out!
			}
		}

		if (unlikely(CPU_MMU_checklimit(segdesc, segment, offset, readflags, is_offset16))) //Disallowed?
		{
			MMU.invaddr = 2; //Invalid address signaling!
			return 1; //Not found.
		}
	}

	//Check for paging and debugging next!

	if ((readflags & 0x20) == 0) //Allow debugger checks?
	{
		dt = debuggertype[readflags&3]; //Load debugger type!
		if (unlikely(dt==0xFF)) goto skipdebugger; //No debugger supported for this type?
		if (unlikely(checkProtectedModeDebugger(realaddress,dt))) return 1; //Error out!
	}
	skipdebugger:

	if (((readflags&0x40)==0) && (segdesc!=-128)) //Checking against paging?
	{
		result = checkDirectMMUaccess(realaddress, (readflags & (~0xE0)), CPL); //Get the result!
		if (unlikely(result==1)) //Failure in the Paging Unit? Don't give it the special flags we use!
		{
			if ((readflags&0x100)==0) //Not disabling paging faults?
			{
				MMU.invaddr = 3; //Invalid address signaling!
			}
			return 1; //Error out!
		}
		else if (result == 2) //Waiting to page in?
		{
			return 2; //Paging in!
		}
	}
	checkMMUaccess_linearaddr = realaddress; //Save the last valid access for the BIU to use(we're not erroring out after all)!
	//We're valid?
	return 0; //We're a valid access for both MMU and Paging! Allow this instruction to execute!
}

byte checkPhysMMUaccess(void *segdesc, word segment, uint_64 offset, word readflags, byte CPL, byte is_offset16, byte subbyte) //Check if a byte address is invalid to read/write for a purpose! Used in all CPU modes! Subbyte is used for alignment checking!
{
	byte result;
	static byte debuggertype[4] = { PROTECTEDMODEDEBUGGER_TYPE_DATAWRITE,PROTECTEDMODEDEBUGGER_TYPE_DATAREAD,0xFF,PROTECTEDMODEDEBUGGER_TYPE_EXECUTION };
	INLINEREGISTER byte dt;
	INLINEREGISTER uint_32 realaddress;
	if (EMULATED_CPU <= CPU_NECV30) return 0; //No checks are done in the old processors!

	//Check for paging and debugging next!
	realaddress = MMU_realaddrPhys((SEGMENT_DESCRIPTOR *)segdesc, segment, (uint_32)offset, 0, is_offset16); //Real adress!

	if ((readflags & 0x20) == 0) //Allow debugger checks?
	{
		dt = debuggertype[readflags & 3]; //Load debugger type!
		if (unlikely(dt == 0xFF)) goto skipdebugger; //No debugger supported for this type?
		if (unlikely(checkProtectedModeDebugger(realaddress, dt))) return 1; //Error out!
	}
skipdebugger:

	if ((readflags & 0x40) == 0) //Checking against paging?
	{
		result = checkDirectMMUaccess(realaddress, (readflags & (~0xE0)), CPL);
		if (unlikely(result==1)) //Failure in the Paging Unit? Don't give it the special flags we use!
		{
			if ((readflags&0x100)==0) //Not disabling paging faults?
			{
				MMU.invaddr = 3; //Invalid address signaling!
			}
			return 1; //Error out!
		}
		else if (result == 2) //Waiting to page in?
		{
			return 2; //Waiting to page in!
		}
	}
	checkMMUaccess_linearaddr = realaddress; //Save the last valid access for the BIU to use(we're not erroring out after all)!
	//We're valid?
	return 0; //We're a valid access for both MMU and Paging! Allow this instruction to execute!
}

byte checkMMUaccess16(sword segdesc, word segment, uint_64 offset, word readflags, byte CPL, byte is_offset16, byte subbyte) //Check if a byte address is invalid to read/write for a purpose! Used in all CPU modes! Subbyte is used for alignment checking!
{
	byte result;
	if (((MMU_realaddr(segdesc, segment, (uint_32)offset, 0,is_offset16)&~0xFFF)==(MMU_realaddr(segdesc, segment, (uint_32)(offset+1), 0,is_offset16)&~0xFFF)) && ((readflags & 0x40) == 0)) //Same page being checked?
	{
		if ((result = checkMMUaccess(segdesc, segment, offset, readflags, CPL, is_offset16, subbyte)) != 0) //Lower bound!
		{
			return result; //Give the result!
		}
	}
	else if ((result = checkMMUaccess(segdesc, segment, offset, readflags, CPL, is_offset16, subbyte)) != 0) //Lower bound!
	{
		return result; //Give the result!
	}
	if ((result = checkMMUaccess(segdesc, segment, offset+1, readflags, CPL, is_offset16, subbyte|1)) != 0) //Upper bound!
	{
		return result; //Give the result!
	}
	return 0; //OK!
}

byte checkPhysMMUaccess16(void *segdesc, word segment, uint_64 offset, word readflags, byte CPL, byte is_offset16, byte subbyte) //Check if a byte address is invalid to read/write for a purpose! Used in all CPU modes! Subbyte is used for alignment checking!
{
	byte result;
	if (((MMU_realaddrPhys(segdesc, segment, (uint_32)offset, 0,is_offset16)&~0xFFF)==(MMU_realaddrPhys(segdesc, segment, (uint_32)(offset+1), 0,is_offset16)&~0xFFF)) && ((readflags & 0x40) == 0)) //Same page being checked?
	{
		if ((result = checkPhysMMUaccess(segdesc, segment, offset, readflags, CPL, is_offset16, subbyte)) != 0) //Lower bound!
		{
			return result; //Give the result!
		}
	}
	else if ((result = checkPhysMMUaccess(segdesc, segment, offset, readflags, CPL, is_offset16, subbyte)) != 0) //Lower bound!
	{
		return result; //Give the result!
	}
	if ((result = checkPhysMMUaccess(segdesc, segment, offset + 1, readflags, CPL, is_offset16, subbyte | 1)) != 0) //Upper bound!
	{
		return result; //Give the result!
	}
	return 0; //OK!
}

byte checkMMUaccess32(sword segdesc, word segment, uint_64 offset, word readflags, byte CPL, byte is_offset16, byte subbyte) //Check if a byte address is invalid to read/write for a purpose! Used in all CPU modes! Subbyte is used for alignment checking!
{
	byte result;
	if (((MMU_realaddr(segdesc, segment, (uint_32)offset, 0,is_offset16)&~0xFFF)==(MMU_realaddr(segdesc, segment, (uint_32)(offset+3), 0,is_offset16)&~0xFFF)) && ((readflags & 0x40) == 0)) //Same page being checked?
	{
		if ((result = checkMMUaccess(segdesc, segment, offset, readflags, CPL, is_offset16, subbyte)) != 0) //Lower bound!
		{
			return result; //Give the result!
		}
	}
	else if ((result = checkMMUaccess(segdesc, segment, offset, readflags, CPL, is_offset16, subbyte)) != 0) //Lower bound!
	{
		return result; //Give the result!
	}
	else if ((result = checkMMUaccess(segdesc, segment, offset+1, readflags|0xC0, CPL, is_offset16, subbyte|1)) != 0) //Lower bound!
	{
		return result; //Give the result!
	}
	else if ((result = checkMMUaccess(segdesc, segment, offset+2, readflags|0xC0, CPL, is_offset16, subbyte|2)) != 0) //Lower bound!
	{
		return result; //Give the result!
	}
	//Dword boundary check for paging!
	//Non Page Faults first!
	if (readflags & 0x40) //Not using paging?
	{
		//This has priority with segmentation! Don't handle paging just yet!
		if ((result = checkMMUaccess(segdesc, segment, offset + 3, (readflags | 0x40), CPL, is_offset16, subbyte | 3)) != 0) //Upper bound!
		{
			return result; //Give the result!
		}
	}
	else //Using paging?
	{
		//Paging, byte granularity faults!
		if ((result = checkMMUaccess(segdesc, segment, offset + 3, (readflags | 0x100), CPL, is_offset16, subbyte | 3)) != 0) //Upper bound!
		{
			//Byte boundary check for paging!
			if ((result = checkMMUaccess(segdesc, segment, offset + 1, readflags, CPL, is_offset16, subbyte | 1)) != 0) //Upper bound!
			{
				return result; //Give the result!
			}
			if ((result = checkMMUaccess(segdesc, segment, offset + 2, readflags, CPL, is_offset16, subbyte | 2)) != 0) //Upper bound!
			{
				return result; //Give the result!
			}
			if ((result = checkMMUaccess(segdesc, segment, offset + 3, readflags, CPL, is_offset16, subbyte | 3)) != 0) //Upper bound!
			{
				return result; //Give the result!
			}
		}
	}
	return 0; //OK!
}

byte checkPhysMMUaccess32(void *segdesc, word segment, uint_64 offset, word readflags, byte CPL, byte is_offset16, byte subbyte) //Check if a byte address is invalid to read/write for a purpose! Used in all CPU modes! Subbyte is used for alignment checking!
{
	byte result;
	if (((MMU_realaddrPhys(segdesc, segment, (uint_32)offset, 0,is_offset16)&~0xFFF)==(MMU_realaddrPhys(segdesc, segment, (uint_32)(offset+3), 0,is_offset16)&~0xFFF)) && ((readflags & 0x40) == 0)) //Same page being checked?
	{
		if ((result = checkPhysMMUaccess(segdesc, segment, offset, readflags, CPL, is_offset16, subbyte)) != 0) //Lower bound!
		{
			return result; //Give the result!
		}
	}
	else if ((result = checkPhysMMUaccess(segdesc, segment, offset, readflags, CPL, is_offset16, subbyte)) != 0) //Lower bound!
	{
		return result; //Give the result!
	}
	//Ignore segmentation, as there is none special upper-bound case here!
	if ((result = checkPhysMMUaccess(segdesc, segment, offset + 3, (readflags|0x100), CPL, is_offset16, subbyte | 3)) != 0) //Upper bound!
	{
		if ((result = checkPhysMMUaccess(segdesc, segment, offset + 1, readflags, CPL, is_offset16, subbyte | 1)) != 0) //Upper bound!
		{
			return result; //Give the result!
		}
		if ((result = checkPhysMMUaccess(segdesc, segment, offset + 2, readflags, CPL, is_offset16, subbyte | 2)) != 0) //Upper bound!
		{
			return result; //Give the result!
		}
		if ((result = checkPhysMMUaccess(segdesc, segment, offset + 3, readflags, CPL, is_offset16, subbyte | 3)) != 0) //Upper bound!
		{
			return result; //Give the result!
		}
	}
	return 0; //OK!
}

extern byte MMU_logging; //Are we logging?
extern uint_64 effectivecpuaddresspins; //What address pins are supported?
extern byte CompaqWrapping[0x1000]; //Compaq Wrapping precalcs!
byte Paging_directrb(sword segdesc, uint_32 realaddress, byte writewordbackup, byte opcode, byte index, byte CPL)
{
	byte result;
	uint_32 originaladdr;
	uint_64 translatedaddr;
	originaladdr = (uint_64)realaddress; //The linear address!
	translatedaddr = originaladdr; //Same by default!
	if (is_paging() && (segdesc!=-128)) //Are we paging?
	{
		translatedaddr = mappage(realaddress,0,CPL); //Map it using the paging mechanism!
	}

	if (segdesc!=-1) //Normal memory access by the CPU itself?
	{
		if (writewordbackup==0) //First data of the (d)word access?
		{
			CPU[activeCPU].wordaddress = realaddress; //Word address used during memory access!
		}
	}

	result = memory_BIUdirectrbIndex(translatedaddr,index|0x100); //Use the BIU to read the data from memory in a cached way!

	if (unlikely(MMU_logging==1)) //To log?
	{
		debugger_logmemoryaccess(0,originaladdr,result,LOGMEMORYACCESS_PAGED); //Log it!
	}

	return result; //Give the result!
}

void Paging_directwb(sword segdesc, uint_32 realaddress, byte val, byte index, byte is_offset16, byte writewordbackup, byte CPL)
{
	uint_32 originaladdr;
	uint_64 translatedaddr;
	originaladdr = (uint_64)realaddress; //The linear address!
	translatedaddr = originaladdr; //Default!
	if (is_paging() && (segdesc!=-128)) //Are we paging?
	{
		translatedaddr = mappage(realaddress,1,CPL); //Map it using the paging mechanism!
	}

	if (segdesc!=-1) //Normal memory access?
	{
		if (writewordbackup==0) //First data of the word access?
		{
			CPU[activeCPU].wordaddress = realaddress; //Word address used during memory access!
		}
	}

	if (unlikely(MMU_logging==1)) //To log?
	{
		debugger_logmemoryaccess(1,originaladdr,val,LOGMEMORYACCESS_PAGED); //Log it!
	}

	memory_BIUdirectwbIndex(translatedaddr, val,(index|0x100)); //Use the BIU to write the data to memory!
}

OPTINLINE byte MMU_INTERNAL_rb(sword segdesc, word segment, uint_32 offset, byte opcode, byte index, byte is_offset16) //Get adress, opcode=1 when opcode reading, else 0!
{
	INLINEREGISTER byte result; //The result!
	INLINEREGISTER uint_32 realaddress;
	byte writewordbackup = CPU[activeCPU].writeword; //Save the old value first!
	if (MMU.memory==NULL) //No mem?
	{
		MMU.invaddr = 1; //Invalid adress!
		return 0xFF; //Out of bounds!
	}

	realaddress = MMU_realaddr(segdesc, segment, offset, CPU[activeCPU].writeword, is_offset16); //Real adress!

	result = Paging_directrb(segdesc,realaddress,writewordbackup,opcode,index,getCPL()); //Read through the paging unit and hardware layer!

	if (unlikely(MMU_logging==1)) //To log?
	{
		debugger_logmemoryaccess(0,offset,result,LOGMEMORYACCESS_NORMAL); //Log it!
	}

	return result; //Give the result!
}

OPTINLINE byte MMU_INTERNAL_rb0(sword segdesc, word segment, uint_32 offset, byte opcode, byte index, byte is_offset16) //Get adress, opcode=1 when opcode reading, else 0!
{
	INLINEREGISTER byte result; //The result!
	INLINEREGISTER uint_32 realaddress;
	byte writewordbackup = CPU[activeCPU].writeword; //Save the old value first!
	if (MMU.memory==NULL) //No mem?
	{
		MMU.invaddr = 1; //Invalid adress!
		return 0xFF; //Out of bounds!
	}

	realaddress = MMU_realaddr(segdesc, segment, offset, CPU[activeCPU].writeword, is_offset16); //Real adress!

	result = Paging_directrb(segdesc,realaddress,writewordbackup,opcode,index,0); //Read through the paging unit and hardware layer!

	if (unlikely(MMU_logging==1)) //To log?
	{
		debugger_logmemoryaccess(0,offset,result,LOGMEMORYACCESS_NORMAL); //Log it!
	}

	return result; //Give the result!
}

OPTINLINE word MMU_INTERNAL_rw(sword segdesc, word segment, uint_32 offset, byte opcode, byte index, byte is_offset16) //Get adress!
{
	INLINEREGISTER word result;
	result = MMU_INTERNAL_rb(segdesc, segment, offset, opcode,index|0x40,is_offset16); //We're accessing a word!
	result |= (MMU_INTERNAL_rb(segdesc, segment, offset + 1, opcode,index|1|0x40,is_offset16) << 8); //Get adress word!
	return result; //Give the result!
}

OPTINLINE word MMU_INTERNAL_rw0(sword segdesc, word segment, uint_32 offset, byte opcode, byte index, byte is_offset16) //Get adress!
{
	INLINEREGISTER word result;
	result = MMU_INTERNAL_rb0(segdesc, segment, offset, opcode,index|0x40,is_offset16); //We're accessing a word!
	result |= (MMU_INTERNAL_rb0(segdesc, segment, offset + 1, opcode,index|1|0x40,is_offset16) << 8); //Get adress word!
	return result; //Give the result!
}

OPTINLINE uint_32 MMU_INTERNAL_rdw(sword segdesc, word segment, uint_32 offset, byte opcode, byte index, byte is_offset16) //Get adress!
{
	INLINEREGISTER uint_32 result;
	result = MMU_INTERNAL_rw(segdesc, segment, offset, opcode,index|0x80,is_offset16);
	result |= (MMU_INTERNAL_rw(segdesc, segment, offset + 2, opcode,(index|2|0x80),is_offset16) << 16); //Get adress dword!
	return result; //Give the result!
}

OPTINLINE uint_32 MMU_INTERNAL_rdw0(sword segdesc, word segment, uint_32 offset, byte opcode, byte index, byte is_offset16) //Get adress!
{
	INLINEREGISTER uint_32 result;
	result = MMU_INTERNAL_rw0(segdesc, segment, offset, opcode, index | 0x80, is_offset16);
	result |= (MMU_INTERNAL_rw0(segdesc, segment, offset + 2, opcode, (index | 2 | 0x80), is_offset16) << 16); //Get adress dword!
	return result; //Give the result!
}

extern byte EMU_RUNNING; //Emulator is running?

OPTINLINE void MMU_INTERNAL_wb(sword segdesc, word segment, uint_32 offset, byte val, byte index, byte is_offset16) //Set adress!
{
	INLINEREGISTER uint_32 realaddress;
	byte writewordbackup = CPU[activeCPU].writeword; //Save the old value first!
	if ((MMU.memory==NULL) || !MMU.size) //No mem?
	{
		MMU.invaddr = 1; //Invalid address signaling!
		return; //Out of bounds!
	}

	realaddress = MMU_realaddr(segdesc, segment, offset, CPU[activeCPU].writeword, is_offset16); //Real adress!

	if (unlikely(MMU_logging==1)) //To log?
	{
		debugger_logmemoryaccess(1,offset,val,LOGMEMORYACCESS_NORMAL); //Log it!
	}

	Paging_directwb(segdesc,realaddress,val,index,is_offset16,writewordbackup,getCPL()); //Write through the paging unit and hardware layer!
}

OPTINLINE void MMU_INTERNAL_ww(sword segdesc, word segment, uint_32 offset, word val, byte index, byte is_offset16) //Set adress (word)!
{
	INLINEREGISTER word w;
	w = val;
	MMU_INTERNAL_wb(segdesc,segment,offset,w&0xFF,(index|0x40),is_offset16); //Low first!
	CPU[activeCPU].writeword = 1; //We're writing a 2nd byte word, for emulating the NEC V20/V30 0x10000 overflow bug.
	w >>= 8; //Shift low!
	MMU_INTERNAL_wb(segdesc,segment,offset+1,(byte)w,(index|1|0x40),is_offset16); //High last!
}

OPTINLINE void MMU_INTERNAL_wdw(sword segdesc, word segment, uint_32 offset, uint_32 val, byte index, byte is_offset16) //Set adress (dword)!
{
	INLINEREGISTER uint_32 d;
	d = val;
	MMU_INTERNAL_ww(segdesc,segment,offset,d&0xFFFF,(index|0x80),is_offset16); //Low first!
	d >>= 16; //Shift low!
	MMU_INTERNAL_ww(segdesc,segment,offset+2,d,(index|2|0x80),is_offset16); //High last!
}

//Routines used by CPU!
byte MMU_directrb_realaddr(uint_64 realaddress) //Read without segment/offset translation&protection (from system/interrupt)!
{
	return MMU_INTERNAL_directrb_realaddr(realaddress,0);
}
void MMU_directwb_realaddr(uint_64 realaddress, byte val) //Read without segment/offset translation&protection (from system/interrupt)!
{
	MMU_INTERNAL_directwb_realaddr(realaddress,val,0);
}

extern byte CPU_databussize; //0=16/32-bit bus! 1=8-bit bus when possible (8088/80188)!
void MMU_wb(sword segdesc, word segment, uint_32 offset, byte val, byte is_offset16) //Set adress!
{
	MMU_INTERNAL_wb(segdesc,segment,offset,val,0,is_offset16);
}
void MMU_ww(sword segdesc, word segment, uint_32 offset, word val, byte is_offset16) //Set adress!
{
	MMU_INTERNAL_ww(segdesc,segment,offset,val,0,is_offset16);
}
void MMU_wdw(sword segdesc, word segment, uint_32 offset, uint_32 val, byte is_offset16) //Set adress!
{
	MMU_INTERNAL_wdw(segdesc,segment,offset,val,0,is_offset16);
}
byte MMU_rb(sword segdesc, word segment, uint_32 offset, byte opcode, byte is_offset16) //Get adress, opcode=1 when opcode reading, else 0!
{
	return MMU_INTERNAL_rb(segdesc,segment,offset,opcode,0,is_offset16);
}
word MMU_rw(sword segdesc, word segment, uint_32 offset, byte opcode, byte is_offset16) //Get adress, opcode=1 when opcode reading, else 0!
{
	return MMU_INTERNAL_rw(segdesc,segment,offset,opcode,0,is_offset16);
}
uint_32 MMU_rdw(sword segdesc, word segment, uint_32 offset, byte opcode, byte is_offset16) //Get adress, opcode=1 when opcode reading, else 0!
{
	return MMU_INTERNAL_rdw(segdesc,segment,offset,opcode,0,is_offset16);
}

byte MMU_rb0(sword segdesc, word segment, uint_32 offset, byte opcode, byte is_offset16) //Get adress, opcode=1 when opcode reading, else 0!
{
	return MMU_INTERNAL_rb0(segdesc,segment,offset,opcode,0,is_offset16);
}
word MMU_rw0(sword segdesc, word segment, uint_32 offset, byte opcode, byte is_offset16) //Get adress, opcode=1 when opcode reading, else 0!
{
	return MMU_INTERNAL_rw0(segdesc,segment,offset,opcode,0,is_offset16);
}
uint_32 MMU_rdw0(sword segdesc, word segment, uint_32 offset, byte opcode, byte is_offset16) //Get adress, opcode=1 when opcode reading, else 0!
{
	return MMU_INTERNAL_rdw0(segdesc,segment,offset,opcode,0,is_offset16);
}


//Extra routines for the emulator.

//A20 bit enable/disable (80286+).
void MMU_setA20(byte where, byte enabled) //To enable A20?
{
	byte A20lineold;
	MMU.enableA20[where] = enabled?1:0; //Enabled?
	A20lineold = MMU.A20LineEnabled; //Old A20 line setting!
	MMU.A20LineDisabled = ((MMU.A20LineEnabled = (MMU.enableA20[0]|MMU.enableA20[1]))^1); //Line enabled/disabled?
	MMU.wraparround = ((~0ULL)^(MMU.A20LineDisabled<<20)); //Clear A20 when both lines that specify it are disabled! Convert it to a simple mask to use!
	if (unlikely(MMU.A20LineEnabled != A20lineold)) //A20 line changed?
	{
		MMU_RAMlayoutupdated(); //Memory layout has been updated!
	}
}
