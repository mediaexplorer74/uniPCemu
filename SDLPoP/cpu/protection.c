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

#include "headers/cpu/cpu.h" //Basic CPU info!
#include "headers/cpu/protection.h" //Protection support!
#include "headers/cpu/multitasking.h" //Multitasking support!
#include "headers/support/zalloc.h" //Memory/register protection support!
#include "headers/mmu/mmuhandler.h" //Direct memory access support!
#include "headers/emu/debugger/debugger.h" //For logging check!
#include "headers/support/locks.h" //We need to unlock ourselves during triple faults, to reset ourselves!
#include "headers/cpu/cpu_pmtimings.h" //286+ timing support!
#include "headers/cpu/easyregs.h" //Easy register support!
#include "headers/support/log.h" //Logging support!
#include "headers/cpu/biu.h" //BIU support!
#include "headers/cpu/cpu_execution.h" //Execution flow support!
#include "headers/cpu/cpu_OP8086.h" //8086+ push/pop support!
#include "headers/cpu/cpu_OP80386.h" //80386+ push/pop support!
#include "headers/cpu/protecteddebugging.h" //Protected mode debugger support!
#include "headers/cpu/flags.h" //Flag support!
#include "headers/cpu/cpu_stack.h" //Stack support!
#include "headers/emu/emucore.h" //RESET line support!

//Log Virtual 8086 mode calls basic information?
//#define LOG_VIRTUALMODECALLS

/*

Basic CPU active segment value retrieval.

*/

//Exceptions, 286+ only!

//Reading of the 16-bit entries within descriptors!
#define DESC_16BITS(x) SDL_SwapLE16(x)

extern byte advancedlog; //Advanced log setting
extern byte MMU_logging; //Are we logging from the MMU?
byte motherboard_responds_to_shutdown = 1; //Motherboard responds to shutdown?
void CPU_triplefault()
{
	CPU[activeCPU].faultraised_lasttype = 0xFF; //Full on reset has been raised!
	emu_raise_resetline(motherboard_responds_to_shutdown ? 1 : 2); //Start pending a reset! Respond to the shutdown cycle if allowed by the motherboard!
	CPU[activeCPU].faultraised = 1; //We're continuing being a fault!
	CPU[activeCPU].executed = 1; //We're finishing to execute!
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger", "#Triple fault!");
	}
}

void CPU_doublefault()
{
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger", "#DF fault(%08X)!", 0);
	}

	CPU[activeCPU].faultraised_lasttype = EXCEPTION_DOUBLEFAULT;
	CPU[activeCPU].faultraised = 1; //Raising a fault!
	uint_64 zerovalue=0; //Zero value pushed!
	++CPU[activeCPU].faultlevel; //Raise the fault level to cause triple faults!
	CPU_resetOP();
	CPU_onResettingFault(0);
	CPU_executionphase_startinterrupt(EXCEPTION_DOUBLEFAULT,2|8,zerovalue); //Execute the double fault handler!
}

byte CPU_faultraised(byte type)
{
	if (EMULATED_CPU<CPU_80286) return 1; //Always allow on older processors without protection!
	if ((CPU[activeCPU].hascallinterrupttaken_type!=0xFF) || (CPU[activeCPU].CPU_interruptraised)) //Were we caused while raising an interrupt or other pending timing?
	{
		CPU_apply286cycles(); //Apply any cycles that need to be applied for the current interrupt to happen!
	}
	CPU[activeCPU].faultraised_external = 1; //Set the external status of any future raised faults until reset again by a valid cause!
	if (CPU[activeCPU].faultlevel) //Double/triple fault might have been raised?
	{
		if (CPU[activeCPU].faultlevel == 2) //Triple fault?
		{
			CPU_triplefault(); //Triple faulting!
			return 0; //Shutdown!
		}
		else
		{
			//Based on the table at http://os.phil-opp.com/double-faults.html whether or not to cause a double fault!
			CPU[activeCPU].faultlevel = 1; //We have a fault raised, so don't raise any more!
			switch (CPU[activeCPU].faultraised_lasttype) //What type was first raised?
			{
				//Contributory causing...
				case EXCEPTION_DIVIDEERROR:
				case EXCEPTION_COPROCESSOROVERRUN:
				case EXCEPTION_INVALIDTSSSEGMENT:
				case EXCEPTION_SEGMENTNOTPRESENT:
				case EXCEPTION_STACKFAULT:
				case EXCEPTION_GENERALPROTECTIONFAULT: //First cases?
					switch (type) //What second cause?
					{
						//... Contributory?
						case EXCEPTION_DIVIDEERROR:
						case EXCEPTION_COPROCESSOROVERRUN:
						case EXCEPTION_INVALIDTSSSEGMENT:
						case EXCEPTION_SEGMENTNOTPRESENT:
						case EXCEPTION_STACKFAULT:
						case EXCEPTION_GENERALPROTECTIONFAULT:
							CPU_doublefault(); //Double faulting!
							return 0; //Don't do anything anymore(partial shutdown)!
							break;
						default: //Normal handling!
							break;
					}
					break;
				//Page fault causing...
				case EXCEPTION_PAGEFAULT: //Page fault? Second case!
					switch (type) //What second cause?
					{
						//... Page fault or ...
						case EXCEPTION_PAGEFAULT:
						//... Contributory?
						case EXCEPTION_DIVIDEERROR:
						case EXCEPTION_COPROCESSOROVERRUN:
						case EXCEPTION_INVALIDTSSSEGMENT:
						case EXCEPTION_SEGMENTNOTPRESENT:
						case EXCEPTION_STACKFAULT:
						case EXCEPTION_GENERALPROTECTIONFAULT:
							CPU_doublefault(); //Double faulting!
							return 0; //Don't do anything anymore(partial shutdown)!
							break;
						default: //Normal handling!
							break;
					}
					break;
				case EXCEPTION_DOUBLEFAULT: //Special case to prevent breakdown?
					if (type==EXCEPTION_DOUBLEFAULT) //We're a double fault raising a double fault?
					{
						CPU_doublefault(); //Double fault!
						return 0; //Don't do anything anymore(partial shutdown)!
					}
				default: //No double fault!
					break;
			}
		}
	}
	else
	{
		CPU[activeCPU].faultlevel = 1; //We have a fault raised, so don't raise any more!
	}
	CPU[activeCPU].faultraised_lasttype = type; //Last type raised!
	CPU[activeCPU].faultraised = 1; //We've raised a fault! Ignore more errors for now!
	return 1; //Handle the fault normally!
}

void CPU_onResettingFault(byte is_paginglock)
{
	byte segRegLeft,segRegIndex,segRegShift;
	if (CPU[activeCPU].have_oldCPL) //Returning the CPL to it's old value?
	{
		CPU[activeCPU].CPL = CPU[activeCPU].oldCPL; //Restore CPL to it's original value!
	}
	if (CPU[activeCPU].have_oldESP) //Returning the (E)SP to it's old value?
	{
		REG_ESP = CPU[activeCPU].oldESP; //Restore ESP to it's original value!
	}
	if (CPU[activeCPU].have_oldEBP) //Returning the (E)BP to it's old value?
	{
		REG_EBP = CPU[activeCPU].oldEBP; //Restore EBP to it's original value!
	}
	if (CPU[activeCPU].have_oldESPinstr && CPU[activeCPU].have_oldEBPinstr && is_paginglock) //Use instruction ESP instead to return to during paging locks?
	{
		REG_ESP = CPU[activeCPU].oldESPinstr; //Restore ESP to it's original value!
		REG_EBP = CPU[activeCPU].oldEBPinstr; //Restore EBP to it's original value!
	}
	else //Normal fault handling?
	{
		CPU[activeCPU].have_oldESPinstr = CPU[activeCPU].have_oldEBPinstr = 0; //Don't do this again, unless retriggered (paging lock again)!
	}
	if (CPU[activeCPU].have_oldEFLAGS) //Returning the (E)SP to it's old value?
	{
		REG_EFLAGS = CPU[activeCPU].oldEFLAGS; //Restore EFLAGS to it's original value!
		updateCPUmode(); //Restore the CPU mode!
	}

	//Restore any segment register caches that are changed!
	segRegIndex = 0; //Init!
	segRegLeft = CPU[activeCPU].have_oldSegReg; //What segment registers are loaded to restore!
	segRegShift = 1; //Current bit to test!
	if (unlikely(segRegLeft)) //Anything to restore?
	{
		for (;(segRegLeft);) //Any segment register and cache left to restore?
		{
			if (segRegLeft&segRegShift) //Something to restore at the current segment register?
			{
				*CPU[activeCPU].SEGMENT_REGISTERS[segRegIndex] = CPU[activeCPU].oldSegReg[segRegIndex]; //Restore the segment register selector/value to it's original value!
				//Restore backing descriptor!
				memcpy(&CPU[activeCPU].SEG_DESCRIPTOR[segRegIndex], &CPU[activeCPU].SEG_DESCRIPTORbackup[segRegIndex], sizeof(CPU[activeCPU].SEG_DESCRIPTOR[0])); //Restore the descriptor!
				segRegLeft &= ~segRegShift; //Not set anymore!
			}
			segRegShift <<= 1; //Next segment register!
			++segRegIndex; //Next index to process!
		}
		CPU[activeCPU].have_oldSegReg = 0; //All segment registers and their caches have been restored!
	}
}

void CPU_commitState() //Prepare for a fault by saving all required data!
{
	//SS descriptor is linked to the CPL in some cases, so backup that as well!
	CPU[activeCPU].oldSS = REG_SS; //Save the most frequently used SS state!
	//Backup the descriptor itself!
	CPU[activeCPU].oldESP = REG_ESP; //Restore ESP to it's original value!
	CPU[activeCPU].have_oldESP = 1; //Restorable!
	CPU[activeCPU].oldEBP = REG_EBP; //Restore EBP to it's original value!
	CPU[activeCPU].have_oldESPinstr = 0; //Don't reload instruction ESP!
	CPU[activeCPU].have_oldEBPinstr = 0; //Don't reload instruction ESP!
	CPU[activeCPU].have_oldEBP = 1; //Restorable!
	CPU_filterflags(); //Filter the flags!
	CPU[activeCPU].oldEFLAGS = REG_EFLAGS; //Restore EFLAGS to it's original value!
	CPU[activeCPU].have_oldEFLAGS = 1; //Restorable!
	updateCPUmode(); //Restore the CPU mode!
	CPU[activeCPU].have_oldCPL = 1; //Restorable!
	CPU[activeCPU].oldCPL = CPU[activeCPU].CPL; //Restore CPL to it's original value!
	CPU[activeCPU].oldCPUmode = getcpumode(); //Save the CPU mode!
	//Backup the descriptors themselves!
	//TR is only to be restored during a section of the task switching process, so we don't save it right here(as it's unmodified, except during task switches)!
	CPU[activeCPU].have_oldSegReg = 0; //Commit the segment registers!
	CPU[activeCPU].faultraised_external = 0; //Reset the external status of any future raised faults!
}

void CPU_commitStateESP()
{
	CPU[activeCPU].oldESPinstr = REG_ESP; //Restore ESP to it's original value!
	CPU[activeCPU].have_oldESPinstr = 1; //Restorable!
	CPU[activeCPU].oldEBPinstr = REG_EBP; //Restore ESP to it's original value!
	CPU[activeCPU].have_oldEBPinstr = 1; //Restorable!
}

//More info: http://wiki.osdev.org/Paging
//General Protection fault.
void CPU_GP(int_64 errorcode)
{
	byte EXT;
	EXT = CPU[activeCPU].faultraised_external; //External type to use!
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		if (errorcode>=0)
		{
			dolog("debugger","#GP fault(%08X)!",errorcode|EXT);
		}
		else
		{
			dolog("debugger","#GP fault(-1)!");
		}
	}
	if (CPU_faultraised(EXCEPTION_GENERALPROTECTIONFAULT)) //Fault raising exception!
	{
		CPU_resetOP(); //Point to the faulting instruction!
		CPU_onResettingFault(0); //Apply reset to fault!
		CPU_executionphase_startinterrupt(EXCEPTION_GENERALPROTECTIONFAULT,2|8,errorcode|EXT); //Call IVT entry #13 decimal!
		//Execute the interrupt!
	}
}

void CPU_AC(int_64 errorcode)
{
	byte EXT;
	EXT = CPU[activeCPU].faultraised_external; //External type to use!
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		if (errorcode>=0)
		{
			dolog("debugger","#AC fault(%08X)!",errorcode|EXT);
		}
		else
		{
			dolog("debugger","#AC fault(-1)!");
		}
	}
	if (CPU_faultraised(EXCEPTION_ALIGNMENTCHECK)) //Fault raising exception!
	{
		CPU_resetOP(); //Point to the faulting instruction!
		CPU_onResettingFault(0); //Apply reset to fault!
		CPU_executionphase_startinterrupt(EXCEPTION_ALIGNMENTCHECK,2|8,errorcode|EXT); //Call IVT entry #13 decimal!
		//Execute the interrupt!
	}
}

void CPU_SegNotPresent(int_64 errorcode)
{
	byte EXT;
	EXT = CPU[activeCPU].faultraised_external; //External type to use!
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		if (errorcode>=0)
		{
			dolog("debugger","#NP fault(%08X)!",errorcode|EXT);
		}
		else
		{
			dolog("debugger","#NP fault(-1)!");
		}
	}
	if (CPU_faultraised(EXCEPTION_SEGMENTNOTPRESENT)) //Fault raising exception!
	{
		CPU_resetOP(); //Point to the faulting instruction!
		CPU_onResettingFault(0); //Apply reset to fault!
		CPU_executionphase_startinterrupt(EXCEPTION_SEGMENTNOTPRESENT,2|8,errorcode|EXT); //Call IVT entry #11 decimal!
		//Execute the interrupt!
	}
}

void CPU_StackFault(int_64 errorcode)
{
	byte EXT;
	EXT = CPU[activeCPU].faultraised_external; //External type to use!
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		if (errorcode>=0)
		{
			dolog("debugger","#SS fault(%08X)!",errorcode|EXT);
		}
		else
		{
			dolog("debugger","#SS fault(-1)!");
		}
	}

	if (CPU_faultraised(EXCEPTION_STACKFAULT)) //Fault raising exception!
	{
		CPU_resetOP(); //Point to the faulting instruction!
		CPU_onResettingFault(0); //Apply reset to fault!
		CPU_executionphase_startinterrupt(EXCEPTION_STACKFAULT,2|8,errorcode|EXT); //Call IVT entry #12 decimal!
		//Execute the interrupt!
	}
}

void protection_nextOP() //We're executing the next OPcode?
{
	CPU[activeCPU].faultraised = 0; //We don't have a fault raised anymore, so we can raise again!
	CPU[activeCPU].faultlevel = 0; //Reset the current fault level!
	CPU[activeCPU].protection_PortRightsLookedup = 0; //Are the port rights looked up, to be reset?
}

byte STACK_SEGMENT_DESCRIPTOR_B_BIT() //80286+: Gives the B-Bit of the DATA DESCRIPTOR TABLE FOR SS-register!
{
	if (EMULATED_CPU <= CPU_80286) //8086-NEC V20/V30?
	{
		return 0; //Always 16-bit descriptor!
	}

	return SEGDESC_NONCALLGATE_D_B(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_SS]) & CPU[activeCPU].D_B_Mask; //Give the B-BIT of the SS-register!
}

byte CODE_SEGMENT_DESCRIPTOR_D_BIT() //80286+: Gives the B-Bit of the DATA DESCRIPTOR TABLE FOR SS-register!
{
	if (EMULATED_CPU <= CPU_80286) //8086-NEC V20/V30?
	{
		return 0; //Always 16-bit descriptor!
	}

	return SEGDESC_NONCALLGATE_D_B(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS]) & CPU[activeCPU].D_B_Mask; //Give the D-BIT of the CS-register!
}

word CPU_segment(byte defaultsegment) //Plain segment to use!
{
	return (CPU[activeCPU].segment_register==CPU_SEGMENT_DEFAULT) ? *CPU[activeCPU].SEGMENT_REGISTERS[defaultsegment] : *CPU[activeCPU].SEGMENT_REGISTERS[CPU[activeCPU].segment_register]; //Use Data Segment (or different) for data!
}

word *CPU_segment_ptr(byte defaultsegment) //Plain segment to use, direct access!
{
	return (CPU[activeCPU].segment_register==CPU_SEGMENT_DEFAULT) ? CPU[activeCPU].SEGMENT_REGISTERS[defaultsegment] : CPU[activeCPU].SEGMENT_REGISTERS[CPU[activeCPU].segment_register]; //Use Data Segment (or different) for data!
}

int CPU_segment_index(byte defaultsegment) //Plain segment to use, direct access!
{
	return (CPU[activeCPU].segment_register==CPU_SEGMENT_DEFAULT) ? defaultsegment : CPU[activeCPU].segment_register; //Use Data Segment (or different in case) for data!
}

int get_segment_index(word *location)
{
	if (location==CPU[activeCPU].SEGMENT_REGISTERS[CPU_SEGMENT_CS])
	{
		return CPU_SEGMENT_CS;
	}
	else if (location==CPU[activeCPU].SEGMENT_REGISTERS[CPU_SEGMENT_DS])
	{
		return CPU_SEGMENT_DS;
	}
	else if (location==CPU[activeCPU].SEGMENT_REGISTERS[CPU_SEGMENT_ES])
	{
		return CPU_SEGMENT_ES;
	}
	else if (location==CPU[activeCPU].SEGMENT_REGISTERS[CPU_SEGMENT_SS])
	{
		return CPU_SEGMENT_SS;
	}
	else if (location==CPU[activeCPU].SEGMENT_REGISTERS[CPU_SEGMENT_FS])
	{
		return CPU_SEGMENT_FS;
	}
	else if (location==CPU[activeCPU].SEGMENT_REGISTERS[CPU_SEGMENT_GS])
	{
		return CPU_SEGMENT_GS;
	}
	else if (location == CPU[activeCPU].SEGMENT_REGISTERS[CPU_SEGMENT_TR])
	{
		return CPU_SEGMENT_TR;
	}
	else if (location == CPU[activeCPU].SEGMENT_REGISTERS[CPU_SEGMENT_LDTR])
	{
		return CPU_SEGMENT_LDTR;
	}
	return -1; //Unknown segment!
}

//getTYPE: gets the loaded descriptor type: 0=Code, 1=Exec, 2=System.
int getLoadedTYPE(SEGMENT_DESCRIPTOR *loadeddescriptor)
{
	return GENERALSEGMENTPTR_S(loadeddescriptor)?EXECSEGMENTPTR_ISEXEC(loadeddescriptor):2; //Executable or data, else System?
}

int isGateDescriptor(SEGMENT_DESCRIPTOR *loadeddescriptor) //0=Fault, 1=Gate, -1=System Segment descriptor, 2=Normal segment descriptor.
{
	if (getLoadedTYPE(loadeddescriptor)==2) //System?
	{
		switch (GENERALSEGMENTPTR_TYPE(loadeddescriptor))
		{
		case AVL_SYSTEM_RESERVED_0: //NULL descriptor?
		case AVL_SYSTEM_RESERVED_1: //Unknown?
		case AVL_SYSTEM_RESERVED_2: //Unknown?
		case AVL_SYSTEM_RESERVED_3: //Unknown?
		default: //Unknown type?
			return 0; //NULL descriptor!
		//32-bit only stuff?
		case AVL_SYSTEM_BUSY_TSS32BIT: //TSS?
		case AVL_SYSTEM_TSS32BIT: //TSS?
			if (EMULATED_CPU<=CPU_80286) return 0; //Invalid descriptor on 286-!
		//16-bit stuff? Always supported
		case AVL_SYSTEM_BUSY_TSS16BIT: //TSS?
		case AVL_SYSTEM_TSS16BIT: //TSS?
		case AVL_SYSTEM_LDT: //LDT?
			return -1; //System segment descriptor!
		case AVL_SYSTEM_CALLGATE32BIT:
		case AVL_SYSTEM_INTERRUPTGATE32BIT:
		case AVL_SYSTEM_TRAPGATE32BIT: //Any type of gate?
			if (EMULATED_CPU<=CPU_80286) return 0; //Invalid descriptor on 286-!
		case AVL_SYSTEM_TASKGATE: //Task gate?
		case AVL_SYSTEM_CALLGATE16BIT:
		case AVL_SYSTEM_INTERRUPTGATE16BIT:
		case AVL_SYSTEM_TRAPGATE16BIT:
			return 1; //We're a gate!
		}
	}
	return 2; //Not a gate descriptor, always valid!
}

void THROWDESCGP(word segmentval, byte external, byte tbl)
{
	CPU_GP((int_64)((external&1)|(segmentval&(0xFFF8))|((tbl&0x3)<<1))); //#GP with an error in the LDT/GDT (index@bits 3-15)!
}

void THROWDESCSS(word segmentval, byte external, byte tbl)
{
	CPU_StackFault((int_64)((external&1)|(segmentval&(0xFFF8))|((tbl&0x3)<<1))); //#StackFault with an error in the LDT/GDT (index@bits 3-15)!
}

void THROWDESCNP(word segmentval, byte external, byte tbl)
{
	CPU_SegNotPresent((int_64)((external&1)|(segmentval&(0xFFF8))|((tbl&0x3)<<1))); //#SegFault with an error in the LDT/GDT (index@bits 3-15)!
}

void THROWDESCTS(word segmentval, byte external, byte tbl)
{
	CPU_TSSFault((segmentval&(0xFFF8)),(external&1),(tbl&0x3)); //#SegFault with an error in the LDT/GDT (index@bits 3-15)!
}

//Another source: http://en.wikipedia.org/wiki/General_protection_fault

extern byte debugger_forceimmediatelogging; //Force immediate logging?

//Virtual memory support wrapper for memory accesses!
byte memory_readlinear(uint_32 address, byte *result)
{
	debugger_forceimmediatelogging = 1; //Log!
	*result = Paging_directrb(-1,address,0,0,0,0); //Read the address!
	debugger_forceimmediatelogging = 0; //Don't log anymore!
	return 0; //No error!
}

byte memory_writelinear(uint_32 address, byte value)
{
	debugger_forceimmediatelogging = 1; //Log!
	Paging_directwb(-1,address,value,0,0,0,0); //Write the address!
	debugger_forceimmediatelogging = 0; //Don't log!
	return 0; //No error!
}

typedef struct
{
	byte mask;
	byte nonequals;
	byte comparision;
} checkrights_cond;

checkrights_cond checkrights_conditions[0x10] = {
	{ 0x13&~0x10,0,0 }, //0 Data, read-only
	{ 0x13&~0x10,0,0 }, //1 unused
	{ 0x13&0,1,0 }, //2 Data, read/write! Allow always!
	{ 0x13&0,1,0 }, //3 unused
	{ 0x13&~0x10,0,0 }, //4 Data(expand down), read-only
	{ 0x13&~0x10,0,0 }, //5 unused
	{ 0x13&0,1,0 }, //6 Data(expand down), read/write! Allow always!
	{ 0x13&0,1,0 }, //7 unused
	{ 0x13&~0x10,1,3 }, //8 Code, non-conforming, execute-only
	{ 0x13&~0x10,1,3 }, //9 unused
	{ 0x13&~0x10,0,0 }, //10 Code, non-conforming, execute/read
	{ 0x13&~0x10,0,0 }, //11 unused
	{ 0x13&~0x10,1,3 }, //12 Code, conforming, execute-only
	{ 0x13&~0x10,1,3 }, //13 unused
	{ 0x13&~0x10,0,0 }, //14 Code, conforming, execute/read
	{ 0x13&~0x10,0,0 } //15 unused
};

byte checkrights_conditions_rwe_errorout[0x10][0x100]; //All precalculated conditions that are possible!

void CPU_calcSegmentPrecalcsPrecalcs()
{
	byte x;
	word n;
	checkrights_cond *rights;
	for (x = 0; x < 0x10; ++x) //All possible conditions!
	{
		rights = &checkrights_conditions[x]; //What type do we check for(take it all, except the dirty bit)!
		for (n = 0; n < 0x100; ++n) //Calculate all conditions that error out or not!
		{
			checkrights_conditions_rwe_errorout[x][n] = (((((n&rights->mask) == rights->comparision) == (rights->nonequals == 0))) & 1); //Are we to error out on this condition?
		}
	}
}

void CPU_calcSegmentPrecalcs(byte is_CS, SEGMENT_DESCRIPTOR *descriptor)
{
	//Calculate the precalculations for execution for this descriptor!
	uint_32 limits[2]; //What limit to apply?
	limits[0] = ((SEGDESCPTR_NONCALLGATE_LIMIT_HIGH(descriptor) << 16) | descriptor->desc.limit_low); //Base limit!
	limits[1] = ((limits[0] << 12) | 0xFFFU); //4KB for a limit of 4GB, fill lower 12 bits with 1!
	descriptor->PRECALCS.limit = (uint_64)limits[SEGDESCPTR_GRANULARITY(descriptor)]; //Use the appropriate granularity to produce the limit!
	if ((EMULATED_CPU>=CPU_PENTIUM) && is_CS && (getcpumode()==CPU_MODE_REAL)) //Special CS real mode behaviour?
	{
		//Normal base address behaviour. But ignore the limit, access rights and D/B fields!
		descriptor->PRECALCS.topdown = 0; //Topdown segment?
		descriptor->PRECALCS.notpresent = 0; //Not present descriptor?
		//Apply read/write/execute permissions to the descriptor!
		memcpy(&descriptor->PRECALCS.rwe_errorout[0], &checkrights_conditions_rwe_errorout[0x93 & 0xE][0],sizeof(descriptor->PRECALCS.rwe_errorout));
	}
	else //Normal segment descriptor behaviour?
	{
		descriptor->PRECALCS.topdown = ((descriptor->desc.AccessRights & 0x1C) == 0x14); //Topdown segment?
		descriptor->PRECALCS.notpresent = (GENERALSEGMENTPTR_P(descriptor)==0); //Not present descriptor?
		//Apply read/write/execute permissions to the descriptor!
		memcpy(&descriptor->PRECALCS.rwe_errorout[0], &checkrights_conditions_rwe_errorout[descriptor->desc.AccessRights & 0xE][0],sizeof(descriptor->PRECALCS.rwe_errorout));
	}
	//Roof: Expand-up: G=0: 1MB, G=1: 4GB. Expand-down: B=0:64K, B=1:4GB.
	descriptor->PRECALCS.roof = ((uint_64)0xFFFFU | ((uint_64)0xFFFFU << ((descriptor->PRECALCS.topdown?SEGDESCPTR_NONCALLGATE_D_B(descriptor):SEGDESCPTR_GRANULARITY(descriptor)) << 4))); //The roof of the descriptor!
	if ((descriptor->PRECALCS.topdown==0) && (SEGDESCPTR_GRANULARITY(descriptor)==0)) //Bottom-up segment that's having a 20-bit limit?
	{
		descriptor->PRECALCS.roof |= 0xF0000; //Actually a 1MB limit instead of 64K!
	}
	descriptor->PRECALCS.base = (((descriptor->desc.base_high << 24) | (descriptor->desc.base_mid << 16) | descriptor->desc.base_low)&0xFFFFFFFFU); //Update the base address!
	if (EMULATED_CPU <= CPU_80286) //286 and below?
	{
		descriptor->PRECALCS.base &= 0xFFFFFF; //Highest byte isn't used!
		descriptor->PRECALCS.roof = 0xFFFF; //Only 16-bit roof!
	}
}

sbyte LOADDESCRIPTOR(int segment, word segmentval, SEGMENT_DESCRIPTOR *container, word isJMPorCALL) //Result: 0=#GP, 1=container=descriptor.
{
	sbyte result;
	CPU[activeCPU].LOADDESCRIPTOR_segmentval = segmentval;
	uint_32 descriptor_address = 0;
	descriptor_address = (uint_32)((segmentval & 4) ? CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_LDTR].PRECALCS.base : CPU[activeCPU].registers->GDTR.base); //LDT/GDT selector!

	uint_32 descriptor_index=segmentval; //The full index within the descriptor table!
	descriptor_index &= ~0x7; //Clear bits 0-2 for our base index into the table!

	byte isNULLdescriptor = 0;
	isNULLdescriptor = 0; //Default: not a NULL descriptor!
	if ((segmentval&~3)==0) //NULL descriptor?
	{
		isNULLdescriptor = 1; //NULL descriptor!
		//Otherwise, don't load the descriptor from memory, just clear valid bit!
	}

	if ((segmentval&4) && (GENERALSEGMENT_P(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_LDTR])==0)) //Invalid LDT segment?
	{
		return 0; //Abort: invalid LDTR to use!
	}
	if (isNULLdescriptor == 0) //Not NULL descriptor?
	{
		if ((word)(descriptor_index | 0x7) > ((segmentval & 4) ? CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_LDTR].PRECALCS.limit : CPU[activeCPU].registers->GDTR.limit)) //LDT/GDT limit exceeded?
		{
			return 0; //Not present: limit exceeded!
		}
	}
	
	descriptor_address += descriptor_index; //Add the index multiplied with the width(8 bytes) to get the descriptor!

	if (isNULLdescriptor==0) //Not special NULL descriptor handling?
	{
		int i;
		if ((result = (sbyte)checkDirectMMUaccess(descriptor_address, 1,/*getCPL()*/ 0))!=0) //Error in the paging unit?
		{
			return (result==2)?-2:-1; //Error out!
		}
		if ((result = (sbyte)checkDirectMMUaccess(descriptor_address+(uint_32)sizeof(container->desc.bytes)-1, 1,/*getCPL()*/ 0))!=0) //Error in the paging unit?
		{
			return (result==2)?-2:-1; //Error out!
		}
		for (i=0;i<(int)sizeof(container->desc.bytes);) //Process the descriptor data!
		{
			if (memory_readlinear(descriptor_address++,&container->desc.bytes[i++])) //Read a descriptor byte directly from flat memory!
			{
				return 0; //Failed to load the descriptor!
			}
		}

		container->desc.limit_low = DESC_16BITS(container->desc.limit_low);
		container->desc.base_low = DESC_16BITS(container->desc.base_low);

		if (EMULATED_CPU == CPU_80286) //80286 has less options?
		{
			container->desc.base_high = 0; //No high byte is present!
			container->desc.noncallgate_info &= ~0xF; //No high limit is present!
		}
	}
	else //NULL descriptor to DS/ES/FS/GS segment registers? Don't load the descriptor from memory(instead clear it's present bit)! Any other register, just clear it's descriptor for a full NULL descriptor!
	{
		if ((segment == CPU_SEGMENT_DS) || (segment == CPU_SEGMENT_ES) || (segment == CPU_SEGMENT_FS) || (segment == CPU_SEGMENT_GS) || (segment==CPU_SEGMENT_TR))
		{
			memcpy(container,&CPU[activeCPU].SEG_DESCRIPTOR[segment],sizeof(*container)); //Copy the old value!
			container->desc.AccessRights &= 0x7F; //Clear the present flag in the descriptor itself!
		}
		else
		{
			memset(container, 0, sizeof(*container)); //Load an invalid register, which is marked invalid!
		}
	}

	CPU_calcSegmentPrecalcs((segment==CPU_SEGMENT_CS)?1:0,container); //Precalculate anything needed!
	if (((segment == CPU_SEGMENT_TR) && ((container->desc.AccessRights & 0x9D) == 0x89)) //Present TSS either 16-bit or 32-bit to set/clear the B-bit when loaded?
		|| (((container->desc.AccessRights & 0x90) == 0x90) && (((container->desc.AccessRights & 1) == 0) || (EMULATED_CPU <= CPU_80386)))) //80386 and below always update the accessed bit and lock. 80486 and up don't when already set!
	{
		//Lock the bus in these cases!
		if (BIU_obtainbuslock()) //Obtaining the bus lock?
		{
			CPU[activeCPU].executed = 0; //Didn't finish executing yet!
			if (EUphasehandlerrequiresreset()) //Requires reset to execute properly?
			{
				CPU_onResettingFault(1); //Set the fault data to restart any instruction-related things!
				if ((MMU_logging == 1) && advancedlog) //Are we logging?
				{
					dolog("debugger", "Descriptor load pending (full instruction state reset)!");
				}
			}
			else
			{
				if ((MMU_logging == 1) && advancedlog) //Are we logging?
				{
					dolog("debugger", "Descriptor load pending!");
				}
			}
			return -2; //Stop and wait to obtain the bus lock first!
		}
		else
		{
			if ((MMU_logging == 1) && advancedlog) //Are we logging?
			{
				dolog("debugger", "Descriptor load pending: bus locked!");
			}
		}
	}
	return 1; //OK!
}

//Result: 1=OK, 0=Error!
sbyte SAVEDESCRIPTOR(int segment, word segmentval, SEGMENT_DESCRIPTOR *container, word isJMPorCALL)
{
	sbyte result;
	uint_32 descriptor_address = 0;
	descriptor_address = (uint_32)((segmentval & 4) ? CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_LDTR].PRECALCS.base : CPU[activeCPU].registers->GDTR.base); //LDT/GDT selector!
	uint_32 descriptor_index = segmentval; //The full index within the descriptor table!
	descriptor_index &= ~0x7; //Clear bits 0-2 for our base index into the table!

	if ((segmentval&~3) == 0)
	{
		return 0; //Don't write the reserved NULL GDT entry, which isn't to be used!
	}

	if ((segmentval&4) && (GENERALSEGMENT_P(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_LDTR])==0)) //Invalid LDT segment?
	{
		return 0; //Abort!
	}

	if ((word)(descriptor_index | 0x7) > ((segmentval & 4) ? CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_LDTR].PRECALCS.limit : CPU[activeCPU].registers->GDTR.limit)) //LDT/GDT limit exceeded?
	{
		return 0; //Not present: limit exceeded!
	}

	if ((!getDescriptorIndex(descriptor_index)) && ((segment == CPU_SEGMENT_CS) || ((segment == CPU_SEGMENT_SS))) && ((segmentval&4)==0)) //NULL GDT segment loaded into CS or SS?
	{
		return 0; //Not present: limit exceeded!	
	}

	descriptor_address += descriptor_index; //Add the index multiplied with the width(8 bytes) to get the descriptor!

	int i;
	descriptor_address += 5; //Only the access rights byte!
		if ((result = (sbyte)checkDirectMMUaccess(descriptor_address++,0,/*getCPL()*/ 0))!=0) //Error in the paging unit?
		{
			return (result==2)?-2:-1; //Error out!
		}
	descriptor_address -= 6; //Only the access rights byte!

	i = 5; //Only the access rights byte!
	descriptor_address += 5; //Only the access rights byte!
	if (memory_writelinear(descriptor_address++,container->desc.bytes[i++])) //Read a descriptor byte directly from flat memory!
	{
		return 0; //Failed to load the descriptor!
	}
	return 1; //OK!
}


byte CPU_handleInterruptGate(byte EXT, byte table, uint_32 descriptorbase, RAWSEGMENTDESCRIPTOR *theidtentry, word returnsegment, uint_32 returnoffset, int_64 errorcode, byte is_interrupt); //Execute a protected mode interrupt!

/*

getsegment_seg: Gets a segment, if allowed, for protected mode.
parameters:
	whatsegment: For what segment is to be fetched?
	segment: The segment selector to get.
	isJMPorCALL: 0 for normal segment setting. 1 for JMP, 2 for CALL, 3 for IRET, 4 for RETF. bit7=Disable privilege level checking, bit8=Disable SAVEDESCRIPTOR writeback, bit9=task switch, bit10=Set EXT bit on faulting, bit 11=TSS Busy requirement(1=Busy, 0=Non-busy), bit 12=bit 13-14 are the CPL instead for privilege checks. bit13-14: used CPL, bit 15: don't throw #SS when set and not a present cause of the fault.
result:
	The segment when available, NULL on error or disallow.

*/

#define effectiveCPL() ((isJMPorCALL&0x1000)?((isJMPorCALL>>13)&3):getCPL())

sbyte touchSegment(int segment, word segmentval, SEGMENT_DESCRIPTOR *container, word isJMPorCALL)
{
	sbyte saveresult;
	if(GENERALSEGMENTPTR_P(container) && (getLoadedTYPE(container) != 2) && (CODEDATASEGMENTPTR_A(container) == 0) && ((isJMPorCALL&0x100)==0)) //Non-accessed loaded and needs to be set? Our reserved bit 8 in isJMPorCALL tells us not to cause writeback for accessed!
	{
		container->desc.AccessRights |= 1; //Set the accessed bit!
		if ((saveresult = SAVEDESCRIPTOR(segment, segmentval, container, isJMPorCALL))<=0) //Trigger writeback and errored out?
		{
			return saveresult;
		}
	}
	return 1; //Success!
}

SEGMENT_DESCRIPTOR *getsegment_seg(int segment, SEGMENT_DESCRIPTOR *dest, word *segmentval, word isJMPorCALL, byte *isdifferentCPL, byte *errorret) //Get this corresponding segment descriptor (or LDT. For LDT, specify LDT register as segment) for loading into the segment descriptor cache!
{
	SEGMENT_DESCRIPTOR LOADEDDESCRIPTOR, GATEDESCRIPTOR; //The descriptor holder/converter!
	memset(&LOADEDDESCRIPTOR, 0, sizeof(LOADEDDESCRIPTOR)); //Init!
	memset(&GATEDESCRIPTOR, 0, sizeof(GATEDESCRIPTOR)); //Init!
	word originalval=*segmentval; //Back-up of the original segment value!
	byte allowNP; //Allow #NP to be used?
	sbyte loadresult;
	byte stackresult;
	byte privilegedone = 0; //Privilege already calculated?
	byte is_gated = 0; //Are we gated?
	byte is_TSS = 0; //Are we a TSS?
	byte callgatetype = 0; //Default: no call gate!

	if ((*segmentval&4) && (((GENERALSEGMENT_P(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_LDTR])==0) && (segment!=CPU_SEGMENT_LDTR)) || (segment==CPU_SEGMENT_LDTR) || (segment==CPU_SEGMENT_TR))) //Invalid LDT segment and LDT is addressed or LDTR/TR in LDT?
	{
	throwdescsegmentval:
		if (isJMPorCALL&0x200) //TSS is the cause?
		{
			THROWDESCTS(*segmentval,1,(*segmentval&4)?EXCEPTION_TABLE_LDT:EXCEPTION_TABLE_GDT); //Throw error!			
		}
		else //Plain #GP?
		{
			THROWDESCGP(*segmentval,((isJMPorCALL&0x400)>>10),(*segmentval&4)?EXCEPTION_TABLE_LDT:EXCEPTION_TABLE_GDT); //Throw error!
		}
		return NULL; //We're an invalid TSS to execute!
	throwSSsegmentval:
		THROWDESCSS(*segmentval,((isJMPorCALL&0x400)>>10),(*segmentval&4)?EXCEPTION_TABLE_LDT:EXCEPTION_TABLE_GDT); //Throw error!
		return NULL; //We're an invalid value to execute!
	}

	if ((loadresult = LOADDESCRIPTOR(segment,*segmentval,&LOADEDDESCRIPTOR,isJMPorCALL))<=0) //Error loading current descriptor?
	{
		if (loadresult == 0) //Not already faulted?
		{
			goto throwdescsegmentval;
		}
		if (loadresult==-2) *errorret = 2; //Page fault?
		return NULL; //Error, by specified reason!
	}
	allowNP = ((segment==CPU_SEGMENT_DS) || (segment==CPU_SEGMENT_ES) || (segment==CPU_SEGMENT_FS) || (segment==CPU_SEGMENT_GS)); //Allow segment to be marked non-present(exception: values 0-3 with data segments)?

	if (((*segmentval&~3)==0)) //NULL GDT segment when not allowed?
	{
		if (segment==CPU_SEGMENT_LDTR) //in LDTR? We're valid!
		{
			goto validLDTR; //Skip all checks, and check out as valid! We're allowed on the LDTR only!
		}
		else //Skip checks: we're invalid to check any further!
		{
			if ((segment==CPU_SEGMENT_CS) || (segment==CPU_SEGMENT_TR) || (segment==CPU_SEGMENT_SS)) //Not allowed?
			{
				goto throwdescsegmentval; //Throw #GP error!
				return NULL; //Error, by specified reason!
			}
			else if (allowNP)
			{
				goto validLDTR; //Load NULL descriptor!
			}
		}
	}

	if ((isGateDescriptor(&LOADEDDESCRIPTOR)==1) && (segment == CPU_SEGMENT_CS) && (((isJMPorCALL&0x1FF)==1) || ((isJMPorCALL&0x1FF)==2)) && ((isJMPorCALL&0x200)==0)) //Handling of gate descriptors? Disable on task code/data segment loading!
	{
		is_gated = 1; //We're gated!
		memcpy(&GATEDESCRIPTOR, &LOADEDDESCRIPTOR, sizeof(GATEDESCRIPTOR)); //Copy the loaded descriptor to the GATE!
		//Check for invalid loads!
		switch (GENERALSEGMENT_TYPE(GATEDESCRIPTOR))
		{
		default: //Unknown type?
		case AVL_SYSTEM_INTERRUPTGATE16BIT:
		case AVL_SYSTEM_TRAPGATE16BIT:
		case AVL_SYSTEM_INTERRUPTGATE32BIT:
		case AVL_SYSTEM_TRAPGATE32BIT:
			//80386 user manual CALL instruction reference says that interrupt and other gates being loaded end up with a General Protection fault.
			//JMP isn't valid for interrupt gates?
			//We're an invalid gate!
			goto throwdescsegmentval; //Throw #GP error!		
			return NULL; //Not present: invalid descriptor type loaded!
			break;
		case AVL_SYSTEM_TASKGATE: //Task gate?
		case AVL_SYSTEM_CALLGATE16BIT:
		case AVL_SYSTEM_CALLGATE32BIT:
			//Valid gate! Allow!
			break;
		}
		if ((MAX(effectiveCPL(), getRPL(*segmentval)) > GENERALSEGMENT_DPL(GATEDESCRIPTOR)) && ((isJMPorCALL&0x1FF)!=3)) //Gate has too high a privilege level? Only when not an IRET(always allowed)!
		{
			goto throwdescsegmentval; //Throw error!
			return NULL; //We are a lower privilege level, so don't load!				
		}
		if (GENERALSEGMENT_P(GATEDESCRIPTOR)==0) //Not present loaded into non-data segment register?
		{
			if (segment==CPU_SEGMENT_SS) //Stack fault?
			{
				THROWDESCSS(*segmentval,(isJMPorCALL&0x200)?1:(((isJMPorCALL&0x400)>>10)),(*segmentval&4)?EXCEPTION_TABLE_LDT:EXCEPTION_TABLE_GDT); //Stack fault!
			}
			else
			{
				THROWDESCNP(*segmentval, (isJMPorCALL&0x200)?1:(((isJMPorCALL&0x400)>>10)),(*segmentval&4)?EXCEPTION_TABLE_LDT:EXCEPTION_TABLE_GDT); //Throw error!
			}
			return NULL; //We're an invalid TSS to execute!
		}

		*segmentval = GATEDESCRIPTOR.desc.selector; //We're loading this segment now, with requesting privilege!

		if (((*segmentval&~3)==0)) //NULL GDT segment when not allowed?
		{
			goto throwdescsegmentval; //Throw #GP(0) error!
			return NULL; //Abort!
		}

		if ((loadresult = LOADDESCRIPTOR(segment, *segmentval, &LOADEDDESCRIPTOR,isJMPorCALL))<=0) //Error loading current descriptor?
		{
			if (loadresult == 0) //Not faulted already?
			{
				goto throwdescsegmentval; //Throw error!
			}
			if (loadresult==-2) *errorret = 2; //Page fault?
			return NULL; //Error, by specified reason!
		}
		privilegedone = 1; //Privilege has been precalculated!
		if (GENERALSEGMENT_TYPE(GATEDESCRIPTOR) == AVL_SYSTEM_TASKGATE) //Task gate?
		{
			if (segment != CPU_SEGMENT_CS) //Not code? We're not a task switch! We're trying to load the task segment into a data register. This is illegal! TR doesn't support Task Gates directly(hardware only)!
			{
				goto throwdescsegmentval; //Throw error!
				return NULL; //Don't load!
			}
		}
		else //Normal descriptor?
		{
			if (GENERALSEGMENT_S(LOADEDDESCRIPTOR)==0) goto throwdescsegmentval;
			if (((isJMPorCALL&0x1FF) == 1) && (!EXECSEGMENT_C(LOADEDDESCRIPTOR))) //JMP to a nonconforming segment?
			{
				if (GENERALSEGMENT_DPL(LOADEDDESCRIPTOR) != effectiveCPL()) //Different CPL?
				{
					goto throwdescsegmentval; //Throw error!
					return NULL; //We are a different privilege level, so don't load!						
				}
			}
			else if (isJMPorCALL&0x1FF) //Call instruction (or JMP instruction to a conforming segment)
			{
				if (GENERALSEGMENT_DPL(LOADEDDESCRIPTOR) > effectiveCPL()) //We have a lower CPL?
				{
					goto throwdescsegmentval; //Throw error!
					return NULL; //We are a different privilege level, so don't load!
				}
			}
		}
	}

	switch (GENERALSEGMENT_TYPE(LOADEDDESCRIPTOR)) //We're a TSS? We're to perform a task switch!
	{
	case AVL_SYSTEM_BUSY_TSS16BIT:
	case AVL_SYSTEM_TSS16BIT: //TSS?
	case AVL_SYSTEM_BUSY_TSS32BIT:
	case AVL_SYSTEM_TSS32BIT: //TSS?
		is_TSS = (isGateDescriptor(&LOADEDDESCRIPTOR)==-1); //We're a TSS when a system segment!
		break;
	default:
		is_TSS = 0; //We're no TSS!
		break;
	}

	byte isnonconformingcode;
	isnonconformingcode = EXECSEGMENT_ISEXEC(LOADEDDESCRIPTOR) && (!EXECSEGMENT_C(LOADEDDESCRIPTOR)) && (getLoadedTYPE(&LOADEDDESCRIPTOR) == 1); //non-conforming code?
	//Now check for CPL,DPL&RPL! (chapter 6.3.2)
	if (
		(
		(!privilegedone && (getRPL(*segmentval)<effectiveCPL()) && (((isJMPorCALL&0x1FF)==4)||(isJMPorCALL&0x1FF)==3)) || //IRET/RETF to higher privilege level?
		((GENERALSEGMENT_DPL(LOADEDDESCRIPTOR)>effectiveCPL()) && (EXECSEGMENT_ISEXEC(LOADEDDESCRIPTOR) && (getLoadedTYPE(&LOADEDDESCRIPTOR) == 1)) && (((isJMPorCALL&0x1FF)==2) || ((isJMPorCALL&0x1FF)==1))) || //CALL/JMP to a lower privilege?
		(!privilegedone && (MAX(effectiveCPL(),getRPL(*segmentval))>GENERALSEGMENT_DPL(LOADEDDESCRIPTOR)) && (((getLoadedTYPE(&LOADEDDESCRIPTOR)!=1) && (segment!=CPU_SEGMENT_SS)) || (isnonconformingcode && (segment!=CPU_SEGMENT_CS)) /*|| ((getLoadedTYPE(&LOADEDDESCRIPTOR) == 1) && (EXECSEGMENT_C(LOADEDDESCRIPTOR)) && (segment != CPU_SEGMENT_CS))*/) && ((isJMPorCALL&0x1FF)!=4) && ((isJMPorCALL&0x1FF)!=3)) || //We are a lower privilege level with either a data/system segment descriptor(also for both conforming and non-conforming code segments with data registers)? Conforming code into a data register ignores CPL/DPL/RPL. Non-conforming code segments have different check for code segments only, but the same rule for data segments:
		(!privilegedone && (((((isJMPorCALL&0x1FF)==4)||((isJMPorCALL&0x1FF)==3))?getRPL(*segmentval):effectiveCPL())<GENERALSEGMENT_DPL(LOADEDDESCRIPTOR)) && (EXECSEGMENT_ISEXEC(LOADEDDESCRIPTOR) && (EXECSEGMENT_C(LOADEDDESCRIPTOR) && (segment==CPU_SEGMENT_CS)) && (getLoadedTYPE(&LOADEDDESCRIPTOR) == 1))) || //We must be at the same privilege level or higher compared to MAX(CPL,RPL) (or just RPL for RETF) for conforming code segment descriptors? Doesn't apply to to data segments(conforming allowed always)!
		(!privilegedone && ((((((isJMPorCALL&0x1FF)==4)||((isJMPorCALL&0x1FF)==3))?getRPL(*segmentval):effectiveCPL())!=GENERALSEGMENT_DPL(LOADEDDESCRIPTOR))) && isnonconformingcode && (segment==CPU_SEGMENT_CS)) || //We must be at the same privilege level compared to CPL (or RPL for RETF) for non-conforming code segment descriptors? Not for data segment selectors(the same as data segment descriptors).
		(!privilegedone && ((effectiveCPL()!=getRPL(*segmentval)) || (effectiveCPL()!=GENERALSEGMENT_DPL(LOADEDDESCRIPTOR))) && (segment==CPU_SEGMENT_SS)) //SS DPL must match CPL and RPL!
		)
		&& (!(((isJMPorCALL&0x1FF)==3) && is_TSS)) //No privilege checking is done on IRET through TSS!
		&& (!((isJMPorCALL&0x80)==0x80)) //Don't ignore privilege?
		&& (segment!=CPU_SEGMENT_TR) //TR loading (LTR, task switching) ignores RPL and DPL!
		&& (segment!=CPU_SEGMENT_LDTR) //LDTR loading (LLDT, task switching) ignores RPL and DPL!
		)
	{
	throwdescoriginalval:
		if (isJMPorCALL & 0x200) //TSS is the cause?
		{
			THROWDESCTS(originalval, 1, (originalval & 4) ? EXCEPTION_TABLE_LDT : EXCEPTION_TABLE_GDT); //Throw error!
		}
		else //Plain #GP?
		{
			THROWDESCGP(originalval, ((isJMPorCALL & 0x400) >> 10), (originalval & 4) ? EXCEPTION_TABLE_LDT : EXCEPTION_TABLE_GDT); //Throw error!
		}
		return NULL; //Not present: limit exceeded!
	}

	if (is_TSS && (*segmentval & 4)) //TSS in LDT detected? That's not allowed!
	{
		goto throwdescoriginalval; //Throw error!
		return NULL; //Error out!
	}

	if ((segment==CPU_SEGMENT_CS) && is_TSS && ((isJMPorCALL&0x200)==0)) //Special stuff on CS, CPL, Task switch.
	{
		//Present is handled by the task switch mechanism, so don't check it here!

		//Execute a normal task switch!
		if (CPU_executionphase_starttaskswitch(segment,&LOADEDDESCRIPTOR,segmentval,*segmentval,(byte)isJMPorCALL,is_gated,-1)) //Switching to a certain task?
		{
			return NULL; //Error changing priviledges or anything else!
		}

		//We've properly switched to the destination task! Continue execution normally!
		return NULL; //Don't actually load CS with the descriptor: we've caused a task switch after all!
	}

	if ((segment == CPU_SEGMENT_CS) && (is_gated == 0) && (getLoadedTYPE(&LOADEDDESCRIPTOR)==1) && (((isJMPorCALL & 0x1FF) == 2)||((isJMPorCALL & 0x1FF) == 1))) //CALL/JMP to lower or different privilege?
	{
		if ((GENERALSEGMENT_DPL(LOADEDDESCRIPTOR) > effectiveCPL()) && EXECSEGMENT_C(LOADEDDESCRIPTOR)) //Conforming and lower privilege?
		{
			goto throwdescoriginalval; //Throw #GP error!		
		}
		if (((getRPL(*segmentval) > effectiveCPL()) || (GENERALSEGMENT_DPL(LOADEDDESCRIPTOR) != effectiveCPL())) && !EXECSEGMENT_C(LOADEDDESCRIPTOR)) //Non-conforming and different privilege or lowering privilege?
		{
			goto throwdescoriginalval; //Throw #GP error!		
		}
		//Non-conforming always must match CPL, so we don't handle it here(it's in the generic check)!
	}

	//Handle invalid types to load now!
	switch (getLoadedTYPE(&LOADEDDESCRIPTOR))
	{
	case 0: //Data descriptor?
		if ((segment==CPU_SEGMENT_CS) || (segment==CPU_SEGMENT_LDTR) || (segment==CPU_SEGMENT_TR)) //Data descriptor in invalid type?
		{
			goto throwdescsegmentval; //Throw #GP error!
			return NULL; //Not present: invalid descriptor type loaded!
		}
		if ((DATASEGMENT_W(LOADEDDESCRIPTOR) == 0) && (segment == CPU_SEGMENT_SS)) //Non-writable SS segment?
		{
			goto throwdescsegmentval; //Throw #GP error!
			return NULL; //Not present: invalid descriptor type loaded!
		}
		break;
	case 1: //Executable descriptor?
		if ((segment != CPU_SEGMENT_CS) && (EXECSEGMENT_R(LOADEDDESCRIPTOR) == 0)) //Executable non-readable in non-executable segment?
		{
			goto throwdescsegmentval; //Throw #GP error!
			return NULL; //Not present: invalid descriptor type loaded!
		}
		if (((segment == CPU_SEGMENT_LDTR) || (segment == CPU_SEGMENT_TR) || (segment == CPU_SEGMENT_SS))) //Executable segment in invalid register?
		{
			goto throwdescsegmentval; //Throw #GP error!
			return NULL; //Not present: invalid descriptor type loaded!
		}
		break;
	case 2: //System descriptor?
		if ((segment!=CPU_SEGMENT_LDTR) && (segment!=CPU_SEGMENT_TR)) //System descriptor in invalid register?
		{
			goto throwdescsegmentval; //Throw #GP error!
			return NULL; //Not present: invalid descriptor type loaded!
		}
		if ((segment == CPU_SEGMENT_LDTR) && (GENERALSEGMENT_TYPE(LOADEDDESCRIPTOR) != AVL_SYSTEM_LDT)) //Invalid LDT load?
		{
			goto throwdescsegmentval; //Throw #GP error!
			return NULL; //Not present: invalid descriptor type loaded!
		}
		if ((segment == CPU_SEGMENT_TR) && (is_TSS == 0)) //Non-TSS into task register?
		{
			goto throwdescsegmentval; //Throw #GP error!
			return NULL; //Not present: invalid descriptor type loaded!
		}
		break;
	default: //Unknown descriptor type? Count as invalid!
		goto throwdescsegmentval; //Throw #GP error!
		return NULL; //Not present: invalid descriptor type loaded!
		break;
	}

	//Make sure we're present last!
	if (GENERALSEGMENT_P(LOADEDDESCRIPTOR)==0) //Not present loaded into non-data NULL register?
	{
		if (segment==CPU_SEGMENT_SS) //Stack fault?
		{
			goto throwSSsegmentval;
		}
		else
		{
			THROWDESCNP(*segmentval,(isJMPorCALL&0x200)?1:((isJMPorCALL&0x400)>>10),(*segmentval&4)?EXCEPTION_TABLE_LDT:EXCEPTION_TABLE_GDT); //Throw error!
		}
		return NULL; //We're an invalid TSS to execute!
	}


	if ((segment == CPU_SEGMENT_CS) && (isGateDescriptor(&GATEDESCRIPTOR) == 1) && (is_gated)) //Gated CS?
	{
		switch (GENERALSEGMENT_TYPE(GATEDESCRIPTOR)) //What type of gate are we using?
		{
		case AVL_SYSTEM_CALLGATE16BIT: //16-bit call gate?
			callgatetype = 1; //16-bit call gate!
			break;
		case AVL_SYSTEM_CALLGATE32BIT: //32-bit call gate?
			callgatetype = 2; //32-bit call gate!
			break;
		default:
			callgatetype = 0; //No call gate!
			break;
		}
		if (callgatetype) //To process a call gate's parameters and offsets?
		{
			CPU[activeCPU].destEIP = (uint_32)GATEDESCRIPTOR.desc.callgate_base_low; //16-bit EIP!
			if (callgatetype == 2) //32-bit destination?
			{
				CPU[activeCPU].destEIP |= (((uint_32)GATEDESCRIPTOR.desc.callgate_base_mid)<<16); //Mid EIP!
				CPU[activeCPU].destEIP |= (((uint_32)GATEDESCRIPTOR.desc.callgate_base_high)<<24); //High EIP!
			}
			uint_32 argument; //Current argument to copy to the destination stack!
			word arguments;
			CPU[activeCPU].CallGateParamCount = 0; //Clear our stack to transfer!
			CPU[activeCPU].CallGateSize = (callgatetype==2)?1:0; //32-bit vs 16-bit call gate!

			if ((GENERALSEGMENT_DPL(LOADEDDESCRIPTOR)<effectiveCPL()) && (EXECSEGMENT_C(LOADEDDESCRIPTOR)==0) && ((isJMPorCALL&0x1FF)==2)) //Stack switch required (with CALL only)?
			{
				//Now, copy the stack arguments!

				*isdifferentCPL = 1; //We're a different level!
				arguments = CPU[activeCPU].CALLGATE_NUMARGUMENTS =  (GATEDESCRIPTOR.desc.ParamCnt&0x1F); //Amount of parameters!
				CPU[activeCPU].CallGateParamCount = 0; //Initialize the amount of arguments that we're storing!
				if ((stackresult = checkStackAccess(arguments,0,(callgatetype==2)?1:0))!=0)
				{
						if (stackresult==2) *errorret = 2; //Page fault?
						return NULL; //Abort on stack fault! Use #SS(0) because we're on the unchanged stack still!
				}
				for (;arguments--;) //Copy as many arguments as needed!
				{
					if (callgatetype==2) //32-bit source?
					{
						argument = MMU_rdw(CPU_SEGMENT_SS, REG_SS, REG_ESP&getstackaddrsizelimiter(), 0,!STACK_SEGMENT_DESCRIPTOR_B_BIT()); //POP 32-bit argument!
						if (STACK_SEGMENT_DESCRIPTOR_B_BIT()) //32-bits?
						{
							REG_ESP += 4; //Increase!
						}
						else //16-bits?
						{
							REG_SP += 4; //Increase!
						}
					}
					else //16-bit source?
					{
						argument = MMU_rw(CPU_SEGMENT_SS, REG_SS, (REG_ESP&getstackaddrsizelimiter()), 0,!STACK_SEGMENT_DESCRIPTOR_B_BIT()); //POP 16-bit argument!
						if (STACK_SEGMENT_DESCRIPTOR_B_BIT()) //32-bits?
						{
							REG_ESP += 2; //Increase!
						}
						else //16-bits?
						{
							REG_SP += 2; //Increase!
						}
					}
					CPU[activeCPU].CallGateStack[CPU[activeCPU].CallGateParamCount++] = argument; //Add the argument to the call gate buffer to transfer to the new stack! Implement us as a LIFO for transfers!
				}
			}
			else
			{
				*isdifferentCPL = 2; //Indicate call gate determines operand size!
			}
		}
	}

	validLDTR:
	memcpy(dest,&LOADEDDESCRIPTOR,sizeof(LOADEDDESCRIPTOR)); //Give the loaded descriptor!

	return dest; //Give the segment descriptor read from memory!
}

/*

MMU: Memory limit!

*/

OPTINLINE byte invalidLimit(SEGMENT_DESCRIPTOR* descriptor, uint_64 offset)
{
	return ((((offset > descriptor->PRECALCS.limit) ^ descriptor->PRECALCS.topdown) | (offset > descriptor->PRECALCS.roof)) & 1);
	//Execute address test?
	//Invalid address range?
	//Apply expand-down data segment, if required, which reverses valid/invalid!
	//Limit to 16-bit/32-bit address space using both top-down(required) and bottom-up(resulting in just the limit, which is lower or equal to the roof) descriptors!
	//Only 1-bit testing!
}

//Used by the CPU(VERR/VERW)&MMU I/O! forreading=0: Write, 1=Read normal, 3=Read opcode

OPTINLINE byte CPU_MMU_checkrights(int segment, word segmentval, uint_64 offset, byte forreading, SEGMENT_DESCRIPTOR* descriptor, byte addrtest, byte is_offset16)
{
	INLINEREGISTER byte result;
	//First: type checking!

	if (unlikely(descriptor->PRECALCS.notpresent)) //Not present(invalid in the cache)? This also applies to NULL descriptors!
	{
		CPU[activeCPU].CPU_MMU_checkrights_cause = 1; //What cause?
		return 1; //#GP fault: not present in descriptor cache mean invalid, thus #GP!
	}

	//Basic access rights are always checked!
	if (likely(GENERALSEGMENTPTR_S(descriptor))) //System segment? Check for additional type information!
	{
		//Entries 0,4,10,14: On writing, Entries 2,6: Never match, Entries 8,12: Writing or reading normally(!=3).
		//To ignore an entry for errors, specify mask 0, non-equals nonzero, comparison 0(a.k.a. ((forreading&0)!=0)
		if (unlikely(descriptor->PRECALCS.rwe_errorout[forreading])) //Are we to error out on this read/write/execute operation?
		{
			CPU[activeCPU].CPU_MMU_checkrights_cause = 3; //What cause?
			return 1; //Error!
		}
	}

	//Next: limit checking!
	if (unlikely(addrtest == 0)) //Address test is to be performed?
	{
		return 0; //Not performing the address test!
	}

	result = invalidLimit(descriptor, offset); //Error out?
	if (likely(result == 0)) return 0; //Not erroring out?
	result |= (result << 1); //3-bit mask! Set both bits for determining the result! Thus it's 3 or 0 now!
	CPU[activeCPU].CPU_MMU_checkrights_cause = (result<<1); //What cause? A limit-determined fault! 6 for errors, 0 otherwise!
	result >>= ((segment!=CPU_SEGMENT_SS)&1); //1 instead of 3 if erroring out on a non-stack fault!
	return result; //Give the result!

	//Don't perform rights checks: This is done when loading the segment register only!
}

byte CPU_MMU_checkrights_jump(int segment, word segmentval, uint_64 offset, byte forreading, SEGMENT_DESCRIPTOR* descriptor, byte addrtest, byte is_offset16) //Check rights for VERR/VERW!
{
	return CPU_MMU_checkrights(segment, segmentval, offset, forreading, descriptor, addrtest, is_offset16); //External call!
}

byte RETF_checkSegmentRegisters[4] = {CPU_SEGMENT_ES,CPU_SEGMENT_FS,CPU_SEGMENT_GS,CPU_SEGMENT_DS}; //All segment registers to check for when returning to a lower privilege level!

byte CPU_segmentWritten_protectedmode_JMPCALL(word *value, word isJMPorCALL, SEGMENT_DESCRIPTOR* descriptor, byte isDifferentCPL)
{
	byte stackresult;
	uint_32 stackval;
	word stackval16; //16-bit stack value truncated!
	if ((isDifferentCPL == 1) && ((isJMPorCALL & 0x1FF) == 2)) //Stack switch is required with CALL only?
	{
		switch (GENERALSEGMENT_TYPE(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_TR])) //What kind of TSS?
		{
		case AVL_SYSTEM_BUSY_TSS32BIT:
		case AVL_SYSTEM_TSS32BIT:
		case AVL_SYSTEM_BUSY_TSS16BIT:
		case AVL_SYSTEM_TSS16BIT:
			if ((stackresult = switchStacks(GENERALSEGMENTPTR_DPL(descriptor) | ((isJMPorCALL & 0x400) >> 8)))!=0) return (stackresult==2)?2:1; //Abort failing switching stacks!

			if ((stackresult = checkStackAccess(2, 1 | (0x100 | 0x200 | (isJMPorCALL & 0x400)), CPU[activeCPU].CallGateSize))!=0) return (stackresult==2)?2:1; //Abort on error! Call Gates throws #SS(SS) instead of #SS(0)!

			CPU_PUSH16(&CPU[activeCPU].oldSS, CPU[activeCPU].CallGateSize); //SS to return!

			if (CPU[activeCPU].CallGateSize)
			{
				CPU_PUSH32(&CPU[activeCPU].oldESP);
			}
			else
			{
				word temp = (word)(CPU[activeCPU].oldESP & 0xFFFF);
				CPU_PUSH16(&temp, 0);
			}

			//Now, we've switched to the destination stack! Load all parameters onto the new stack!
			if ((stackresult = checkStackAccess(CPU[activeCPU].CallGateParamCount, 1 | (0x100 | 0x200 | (isJMPorCALL & 0x400)), CPU[activeCPU].CallGateSize))!=0) return (stackresult==2)?2:1; //Abort on error! Call Gates throws #SS(SS) instead of #SS(0)!
			for (; CPU[activeCPU].CallGateParamCount;) //Process the CALL Gate Stack!
			{
				stackval = CPU[activeCPU].CallGateStack[--CPU[activeCPU].CallGateParamCount]; //Read the next value to store!
				if (CPU[activeCPU].CallGateSize) //32-bit stack to push to?
				{
					CPU_PUSH32(&stackval); //Push the 32-bit stack value to the new stack!
				}
				else //16-bit?
				{
					stackval16 = (word)(stackval & 0xFFFF); //Reduced data if needed!
					CPU_PUSH16(&stackval16, 0); //Push the 16-bit stack value to the new stack!
				}
			}
			break;
		default:
			break;
		}
	}
	else if (isDifferentCPL == 0) //Unchanging CPL? Take call size from operand size!
	{
		CPU[activeCPU].CallGateSize = CPU[activeCPU].CPU_Operand_size; //Use the call instruction size!
	}
	//Else, call by call gate size!

	if ((isJMPorCALL & 0x1FF) == 2) //CALL pushes return address!
	{
		if ((stackresult = checkStackAccess(2, 1, CPU[activeCPU].CallGateSize | ((isDifferentCPL == 1) ? (0x100 | 0x200 | (isJMPorCALL & 0x400)) : 0)))!=0) return (stackresult==2)?2:1; //Abort on error! Call Gates throws #SS(SS) instead of #SS(0)!

		//Push the old address to the new stack!
		if (CPU[activeCPU].CallGateSize) //32-bit?
		{
			CPU_PUSH16(&REG_CS, 1);
			CPU_PUSH32(&REG_EIP);
		}
		else //16-bit?
		{
			CPU_PUSH16(&REG_CS, 0);
			CPU_PUSH16(&REG_IP, 0);
		}
	}

	setRPL(*value, getCPL()); //RPL of CS always becomes CPL!

	if (isDifferentCPL == 1) //Different CPL?
	{
		CPU[activeCPU].hascallinterrupttaken_type = CPU[activeCPU].CALLGATE_NUMARGUMENTS ? CALLGATE_DIFFERENTLEVEL_XPARAMETERS : CALLGATE_DIFFERENTLEVEL_NOPARAMETERS; //INT gate type taken. Low 4 bits are the type. High 2 bits are privilege level/task gate flag. Left at 0xFF when nothing is used(unknown case?)
	}
	else //Same CPL call gate?
	{
		CPU[activeCPU].hascallinterrupttaken_type = CALLGATE_SAMELEVEL; //Same level call gate!
	}
	return 0; //OK!
}

byte CPU_segmentWritten_protectedmode_RETF(byte oldCPL, word value, word isJMPorCALL, byte *RETF_segmentregister)
{
	byte stackresult;
	if (CPU[activeCPU].is_stackswitching == 0) //We're ready to process?
	{
		if (STACK_SEGMENT_DESCRIPTOR_B_BIT())
		{
			REG_ESP += CPU[activeCPU].RETF_popbytes; //Process ESP!
		}
		else
		{
			REG_SP += CPU[activeCPU].RETF_popbytes; //Process SP!
		}
		if (oldCPL < getRPL(value)) //Lowering privilege?
		{
			if ((stackresult = checkStackAccess(2, 0, CPU[activeCPU].CPU_Operand_size))!=0) return (stackresult==2)?2:1; //Stack fault?
		}
	}

	if (oldCPL < getRPL(value)) //CPL changed or still busy for this stage?
	{
		//Now, return to the old prvilege level!
		CPU[activeCPU].hascallinterrupttaken_type = RET_DIFFERENTLEVEL; //INT gate type taken. Low 4 bits are the type. High 2 bits are privilege level/task
		if (CPU[activeCPU].CPU_Operand_size)
		{
			if (CPU80386_internal_POPdw(6, &CPU[activeCPU].segmentWritten_tempESP))
			{
				CPU[activeCPU].is_stackswitching = 1; //We're stack switching!
				return 1; //POP ESP!
			}
		}
		else
		{
			if (CPU8086_internal_POPw(6, &CPU[activeCPU].segmentWritten_tempSP, 0))
			{
				CPU[activeCPU].is_stackswitching = 1; //We're stack switching!
				return 1; //POP SP!
			}
		}
		if (CPU8086_internal_POPw(8, &CPU[activeCPU].segmentWritten_tempSS, CPU[activeCPU].CPU_Operand_size))
		{
			CPU[activeCPU].is_stackswitching = 1; //We're stack switching!
			return 1; //POPped?
		}
		CPU[activeCPU].is_stackswitching = 0; //We've finished stack switching!
		//Privilege change!

		if ((stackresult = segmentWritten(CPU_SEGMENT_SS, CPU[activeCPU].segmentWritten_tempSS, (getRPL(value) << 13) | 0x1000))!=0) return (stackresult==2)?2:1; //Back to our calling stack!
		if (CPU[activeCPU].CPU_Operand_size)
		{
			REG_ESP = CPU[activeCPU].segmentWritten_tempESP; //POP ESP!
		}
		else
		{
			REG_ESP = (uint_32)CPU[activeCPU].segmentWritten_tempSP; //POP SP!
		}
		*RETF_segmentregister = 1; //We're checking the segments for privilege changes to be invalidated!
	}
	else if (oldCPL > getRPL(value)) //CPL raised during RETF?
	{
		THROWDESCGP(value, (isJMPorCALL & 0x200) ? 1 : (((isJMPorCALL & 0x400) >> 10)), (value & 4) ? EXCEPTION_TABLE_LDT : EXCEPTION_TABLE_GDT); //Raising CPL using RETF isn't allowed!
		return 1; //Abort on fault!
	}
	else //Same privilege? (E)SP on the destination stack is already processed, don't process again!
	{
		CPU[activeCPU].RETF_popbytes = 0; //Nothing to pop anymore!
	}
	return 0; //OK!
}

byte CPU_segmentWritten_protectedmode_IRET(byte oldCPL, word value, word isJMPorCALL, byte *RETF_segmentregister)
{
	byte stackresult;
	uint_32 tempesp;
	if (getRPL(value) > oldCPL) //Stack needs to be restored when returning to outer privilege level!
	{
		if ((stackresult = checkStackAccess(2, 0, CPU[activeCPU].CPU_Operand_size))!=0) return (stackresult==2)?2:1; //First level IRET data?
		if (CPU[activeCPU].CPU_Operand_size)
		{
			tempesp = CPU_POP32();
		}
		else
		{
			tempesp = CPU_POP16(CPU[activeCPU].CPU_Operand_size);
		}

		CPU[activeCPU].segmentWritten_tempSS = CPU_POP16(CPU[activeCPU].CPU_Operand_size);

		if ((stackresult = segmentWritten(CPU_SEGMENT_SS, CPU[activeCPU].segmentWritten_tempSS, (getRPL(value) << 13) | 0x1000))!=0) return (stackresult==2)?2:1; //Back to our calling stack!
		if (STACK_SEGMENT_DESCRIPTOR_B_BIT()) //32-bit stack write (undocumented)?
		{
			REG_ESP = tempesp; //32-bits written!
		}
		else
		{
			REG_SP = tempesp; //Only write SP, leave upper bits alone!
		}

		*RETF_segmentregister = 1; //We're checking the segments for privilege changes to be invalidated!
	}
	else if (oldCPL > getRPL(value)) //CPL raised during IRET?
	{
		THROWDESCGP(value, (isJMPorCALL & 0x200) ? 1 : ((isJMPorCALL & 0x400) >> 10), (value & 4) ? EXCEPTION_TABLE_LDT : EXCEPTION_TABLE_GDT); //Raising CPL using RETF isn't allowed!
		return 1; //Abort!
	}
	return 0; //OK!
}

byte CPU_segmentWritten_protectedmode_TR(int segment, word value, word isJMPorCALL, SEGMENT_DESCRIPTOR *tempdescriptor)
{
	sbyte errorret;
	if ((isJMPorCALL & 0x1FF) == 0) //Not a JMP or CALL itself, or a task switch, so just a plain load using LTR?
	{
		SEGMENT_DESCRIPTOR savedescriptor;
		switch (GENERALSEGMENTPTR_TYPE(tempdescriptor)) //What kind of TSS?
		{
		case AVL_SYSTEM_BUSY_TSS32BIT:
		case AVL_SYSTEM_BUSY_TSS16BIT:
			if ((isJMPorCALL & 0x800) == 0) //Needs to be non-busy?
			{
				THROWDESCGP(value, (isJMPorCALL & 0x200) ? 1 : (((isJMPorCALL & 0x400) >> 10)), (value & 4) ? EXCEPTION_TABLE_LDT : EXCEPTION_TABLE_GDT); //We cannot load a busy TSS!
				return 1; //Abort on fault!
			}
			break;
		case AVL_SYSTEM_TSS32BIT:
		case AVL_SYSTEM_TSS16BIT:
			if ((isJMPorCALL & 0x800)) //Needs to be busy?
			{
				THROWDESCGP(value, (isJMPorCALL & 0x200) ? 1 : (((isJMPorCALL & 0x400) >> 10)), (value & 4) ? EXCEPTION_TABLE_LDT : EXCEPTION_TABLE_GDT); //We cannot load a busy TSS!
				return 1; //Abort on fault!
			}

			tempdescriptor->desc.AccessRights |= 2; //Mark not idle in the RAM descriptor!
			savedescriptor.desc.DATA64 = tempdescriptor->desc.DATA64; //Copy the resulting descriptor contents to our buffer for writing to RAM!
			if ((errorret = SAVEDESCRIPTOR(segment, value, &savedescriptor, isJMPorCALL) <= 0)) //Save it back to RAM failed?
			{
				return (errorret==-2)?2:1; //Abort on fault!
			}
			break;
		default: //Invalid segment descriptor to load into the TR register?
			THROWDESCGP(value, (isJMPorCALL & 0x200) ? 1 : ((isJMPorCALL & 0x400) >> 10), (value & 4) ? EXCEPTION_TABLE_LDT : EXCEPTION_TABLE_GDT); //We cannot load a busy TSS!
			return 1; //Abort on fault!
			break; //Ignore!
		}
	}
	return 0; //OK!
}

byte CPU_segmentWritten_protectedmode_CS(word isJMPorCALL)
{
	REG_EIP = CPU[activeCPU].destEIP; //The current OPCode: just jump to the address specified by the descriptor OR command!
	if (((isJMPorCALL & 0x1FF) == 4) || ((isJMPorCALL & 0x1FF) == 3)) //IRET/RETF required limit check!
	{
		if (CPU_MMU_checkrights(CPU_SEGMENT_CS, REG_CS, REG_EIP, 0x40|3, &CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS], 2, CPU[activeCPU].CPU_Operand_size)) //Limit broken or protection fault?
		{
			THROWDESCGP(0, 0, 0); //#GP(0) when out of limit range!
			return 1; //Abort on fault!
		}
	}
	return 0; //OK!
}

void CPU_segmentWritten_protectedmode_RETFIRET(word isJMPorCALL)
{
	byte RETF_segmentregister, RETF_whatsegment; //The segment registers we're handling!
	byte isnonconformingcodeordata;
	for (RETF_segmentregister = 0; RETF_segmentregister < NUMITEMS(RETF_checkSegmentRegisters); ++RETF_segmentregister) //Process all we need to check!
	{
		RETF_whatsegment = RETF_checkSegmentRegisters[RETF_segmentregister]; //What register to check?
		word descriptor_index;
		descriptor_index = getDescriptorIndex(*CPU[activeCPU].SEGMENT_REGISTERS[RETF_whatsegment]); //What descriptor index?
		if (descriptor_index) //Valid index(Non-NULL)?
		{
			if ((word)(descriptor_index | 0x7) > ((*CPU[activeCPU].SEGMENT_REGISTERS[RETF_whatsegment] & 4) ? CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_LDTR].PRECALCS.limit : CPU[activeCPU].registers->GDTR.limit)) //LDT/GDT limit exceeded?
			{
			invalidRETFsegment:
				if ((CPU[activeCPU].have_oldSegReg & (1 << RETF_whatsegment)) == 0) //Backup not loaded yet?
				{
					memcpy(&CPU[activeCPU].SEG_DESCRIPTORbackup[RETF_whatsegment], &CPU[activeCPU].SEG_DESCRIPTOR[RETF_whatsegment], sizeof(CPU[activeCPU].SEG_DESCRIPTORbackup[0])); //Restore the descriptor!
					CPU[activeCPU].oldSegReg[RETF_whatsegment] = *CPU[activeCPU].SEGMENT_REGISTERS[RETF_whatsegment]; //Backup the register too!
					CPU[activeCPU].have_oldSegReg |= (1 << RETF_whatsegment); //Loaded!
				}
				//Selector and Access rights are zeroed!
				*CPU[activeCPU].SEGMENT_REGISTERS[RETF_whatsegment] = 0; //Zero the register!
				if ((isJMPorCALL & 0x1FF) == 3) //IRET?
				{
					CPU[activeCPU].SEG_DESCRIPTOR[RETF_whatsegment].desc.AccessRights &= 0x7F; //Clear the valid flag only with IRET!
				}
				else //RETF?
				{
					CPU[activeCPU].SEG_DESCRIPTOR[RETF_whatsegment].desc.AccessRights = 0; //Invalid!
				}
				CPU_calcSegmentPrecalcs(0, &CPU[activeCPU].SEG_DESCRIPTOR[RETF_whatsegment]); //Update the precalcs for said access rights!
				continue; //Next register!
			}
		}
		if (GENERALSEGMENT_P(CPU[activeCPU].SEG_DESCRIPTOR[RETF_whatsegment]) == 0) //Not present?
		{
			goto invalidRETFsegment; //Next register!
		}
		if (GENERALSEGMENT_S(CPU[activeCPU].SEG_DESCRIPTOR[RETF_whatsegment]) == 0) //Not data/readable code segment?
		{
			goto invalidRETFsegment; //Next register!
		}
		//We're either data or code!
		isnonconformingcodeordata = 0; //Default: neither!
		if (EXECSEGMENT_ISEXEC(CPU[activeCPU].SEG_DESCRIPTOR[RETF_whatsegment])) //Code?
		{
			if (!EXECSEGMENT_C(CPU[activeCPU].SEG_DESCRIPTOR[RETF_whatsegment])) //Nonconforming? Invalid!
			{
				isnonconformingcodeordata = 1; //Next register!
			}
			if (!EXECSEGMENT_R(CPU[activeCPU].SEG_DESCRIPTOR[RETF_whatsegment])) //Not readable? Invalid!
			{
				goto invalidRETFsegment; //Next register!
			}
		}
		else isnonconformingcodeordata = 1; //Data!
		//We're either data or readable code!
		if (isnonconformingcodeordata && (GENERALSEGMENT_DPL(CPU[activeCPU].SEG_DESCRIPTOR[RETF_whatsegment]) < MAX(getCPL(), getRPL(*CPU[activeCPU].SEGMENT_REGISTERS[RETF_whatsegment])))) //Not privileged enough to handle said segment descriptor?
		{
			goto invalidRETFsegment; //Next register!
		}
	}
}

byte segmentWritten(int segment, word value, word isJMPorCALL) //A segment register has been written to!
{
	byte errorret=1;
	byte checkSegmentRegisters=0; //A segment register we're checking during a RETF instruction!
	byte oldCPL= getCPL();
	byte isDifferentCPL;
	sbyte loadresult;
	CPU[activeCPU].segmentWrittenVal = value; //What value is written!
	CPU[activeCPU].isJMPorCALLval = isJMPorCALL; //What type of write are we?
	if (getcpumode()==CPU_MODE_PROTECTED) //Protected mode, must not be real or V8086 mode, so update the segment descriptor cache!
	{
		CPU[activeCPU].segmentWritten_instructionrunning += 1; //We're running the segmentWritten function now!
		isDifferentCPL = 0; //Default: same CPL!
		SEGMENT_DESCRIPTOR tempdescriptor;
		SEGMENT_DESCRIPTOR *descriptor = getsegment_seg(segment,&tempdescriptor,&value,isJMPorCALL,&isDifferentCPL, &errorret); //Read the segment!
		if (descriptor) //Loaded&valid?
		{
			if (segment == CPU_SEGMENT_CS) //Code segment? We're some kind of jump or return!
			{
				switch (isJMPorCALL & 0x1FF) //Special action to take?
				{
				case 1: //JMP?
				case 2: //CALL?
					//JMP(with call gate)/CALL needs pushed data on the stack?
					if ((errorret = CPU_segmentWritten_protectedmode_JMPCALL(&value, isJMPorCALL, descriptor, isDifferentCPL))!=0) //Handle JMP/CALL for protected mode segments!
					{
						CPU[activeCPU].segmentWritten_instructionrunning -= 1; //We're running the segmentWritten function now!
						return errorret; //Abort!
					}
					break;
				case 3: //IRET?
					//IRET might need extra data popped?
					if ((errorret = CPU_segmentWritten_protectedmode_IRET(oldCPL, value, isJMPorCALL, &checkSegmentRegisters))!=0) //Handle RETF for protected mode segments!
					{
						CPU[activeCPU].segmentWritten_instructionrunning -= 1; //We're running the segmentWritten function now!
						return errorret; //Abort!
					}
					break;
				case 4: //RETF?
					//RETF needs popped data on the stack?
					if ((errorret = CPU_segmentWritten_protectedmode_RETF(oldCPL, value, isJMPorCALL, &checkSegmentRegisters))!=0) //Handle RETF for protected mode segments!
					{
						CPU[activeCPU].segmentWritten_instructionrunning -= 1; //We're running the segmentWritten function now!
						return errorret; //Abort!
					}
					break;
				default: //Unknown action?
					break;
				}
			}
			else if (segment==CPU_SEGMENT_TR) //Loading the Task Register? We're to mask us as busy!
			{
				if (CPU_segmentWritten_protectedmode_TR(segment, value, isJMPorCALL, descriptor)) //Handle TR for protected mode segments!
				{
					CPU[activeCPU].segmentWritten_instructionrunning -= 1; //We're running the segmentWritten function now!
					return 1; //Abort!
				}
			}
			//Now, load the new descriptor and address for CS if needed(with secondary effects)!
			if ((CPU[activeCPU].have_oldSegReg&(1 << segment)) == 0) //Backup not loaded yet?
			{
				memcpy(&CPU[activeCPU].SEG_DESCRIPTORbackup[segment], &CPU[activeCPU].SEG_DESCRIPTOR[segment], sizeof(CPU[activeCPU].SEG_DESCRIPTORbackup[0])); //Restore the descriptor!
				CPU[activeCPU].oldSegReg[segment] = *CPU[activeCPU].SEGMENT_REGISTERS[segment]; //Backup the register too!
				CPU[activeCPU].have_oldSegReg |= (1 << segment); //Loaded!
			}
			memcpy(&CPU[activeCPU].SEG_DESCRIPTOR[segment],descriptor,sizeof(CPU[activeCPU].SEG_DESCRIPTOR[segment])); //Load the segment descriptor into the cache!
			*CPU[activeCPU].SEGMENT_REGISTERS[segment] = value; //Set the segment register to the allowed value!

			if ((loadresult = touchSegment(segment,value,descriptor,isJMPorCALL))<=0) //Errored out during touching?
			{
				if (loadresult == 0) //Not already faulted?
				{
					if (isJMPorCALL&0x200) //TSS is the cause?
					{
						THROWDESCTS(value,1,(value&4)?EXCEPTION_TABLE_LDT:EXCEPTION_TABLE_GDT); //Throw error!			
					}
					else //Plain #GP?
					{
						THROWDESCGP(value,((isJMPorCALL&0x400)>>10),(value&4)?EXCEPTION_TABLE_LDT:EXCEPTION_TABLE_GDT); //Throw error!
					}
				}
				CPU[activeCPU].segmentWritten_instructionrunning -= 1; //We're running the segmentWritten function now!
				return (loadresult==-2)?2:1; //Abort on fault!
			}

			switch (segment)
			{
			case CPU_SEGMENT_CS: //CS register?
				if (CPU_segmentWritten_protectedmode_CS(isJMPorCALL))
				{
					CPU[activeCPU].segmentWritten_instructionrunning -= 1; //We're running the segmentWritten function now!
					return 1; //Abort!
				}
				break;
			case CPU_SEGMENT_SS: //SS? We're also updating the CPL!
				updateCPL(); //Update the CPL according to the mode!
				break;
			default: //All other segments: nothing special!
				break;
			}

			if (checkSegmentRegisters) //Are we to check the segment registers for validity during a RETF?
			{
				CPU_segmentWritten_protectedmode_RETFIRET(isJMPorCALL); //Handle it!
			}
			if (segment == CPU_SEGMENT_CS) //CS needs a update on all CPU-related stuff!
			{
				if (CPU_condflushPIQ(-1))
				{
					CPU[activeCPU].segmentWritten_instructionrunning -= 1; //We're running the segmentWritten function now!
					return 1; //We're jumping to another address!
				}
			}
			CPU[activeCPU].segmentWritten_instructionrunning -= 1; //We're running the segmentWritten function now!
		}
		else //A fault has been raised? Abort!
		{
			if (segment == CPU_SEGMENT_CS)
			{
				CPU_flushPIQ(-1); //We're jumping to another address!
			}
			CPU[activeCPU].segmentWritten_instructionrunning -= 1; //We're running the segmentWritten function now!
			return errorret; //Abort on fault!
		}
	}
	else //Real mode has no protection?
	{
		if ((isJMPorCALL&0x1FF) == 2) //CALL needs pushed data?
		{
			if ((CPU[activeCPU].CPU_Operand_size) && (EMULATED_CPU>=CPU_80386)) //32-bit?
			{
				if (CPU[activeCPU].internalinstructionstep==0) if ((errorret = checkStackAccess(2, 1, 1))!=0) return errorret; //We're trying to push on the stack!
				uint_32 pushingval;
				pushingval = REG_CS; //What to push!
				if (CPU80386_internal_PUSHdw(0,&pushingval)) return 1;
				if (CPU80386_internal_PUSHdw(2,&REG_EIP)) return 1;
			}
			else //16-bit?
			{
				if (CPU[activeCPU].internalinstructionstep==0) if((errorret = checkStackAccess(2, 1, 0))!=0) return errorret; //We're trying to push on the stack!
				if (CPU8086_internal_PUSHw(0,&REG_CS,0)) return 1;
				if (CPU8086_internal_PUSHw(2,&REG_IP,0)) return 1;
			}
		}

		if ((CPU[activeCPU].have_oldSegReg&(1 << segment)) == 0) //Backup not loaded yet?
		{
			memcpy(&CPU[activeCPU].SEG_DESCRIPTORbackup[segment], &CPU[activeCPU].SEG_DESCRIPTOR[segment], sizeof(CPU[activeCPU].SEG_DESCRIPTORbackup[0])); //Restore the descriptor!
			CPU[activeCPU].oldSegReg[segment] = *CPU[activeCPU].SEGMENT_REGISTERS[segment]; //Backup the register too!
			CPU[activeCPU].have_oldSegReg |= (1 << segment); //Loaded!
		}

		*CPU[activeCPU].SEGMENT_REGISTERS[segment] = value; //Just set the segment, don't load descriptor!
		//Load the correct base data for our loading!
		CPU[activeCPU].SEG_DESCRIPTOR[segment].desc.base_low = (word)(((uint_32)value<<4)&0xFFFF); //Low base!
		CPU[activeCPU].SEG_DESCRIPTOR[segment].desc.base_mid = ((((uint_32)value << 4) & 0xFF0000)>>16); //Mid base!
		CPU[activeCPU].SEG_DESCRIPTOR[segment].desc.base_high = ((((uint_32)value << 4) & 0xFF000000U)>>24); //High base!
		//This also maps the resulting segment in low memory (20-bit address space) in real mode, thus CS is pulled low as well!
		//Real mode affects only CS like Virtual 8086 mode(reloading all base/limit values). Other segments are unmodified.
		//Virtual 8086 mode also loads the rights etc.? This is to prevent Virtual 8086 tasks having leftover data in their descriptors, causing faults!
		//Real mode CS before Pentium too, Pentium and up ignores access rights and limit fields in real mode!
		if (((segment==CPU_SEGMENT_CS) && (EMULATED_CPU<CPU_PENTIUM)) || (getcpumode()==CPU_MODE_8086)) //Only done for the CS segment in real mode as well as all registers in 8086 mode?
		{
			CPU[activeCPU].SEG_DESCRIPTOR[segment].desc.AccessRights = 0x93; //Compatible rights!
			CPU[activeCPU].SEG_DESCRIPTOR[segment].desc.limit_low = 0xFFFF;
			CPU[activeCPU].SEG_DESCRIPTOR[segment].desc.noncallgate_info = 0x00; //Clear: D/B-bit, G-bit, Limit High!
		}
		switch (segment)
		{
		case CPU_SEGMENT_CS: //CS segment? Reload access rights in real mode on first write access!
			if ((EMULATED_CPU >= CPU_PENTIUM) && (getcpumode() == CPU_MODE_REAL)) //Real mode loading CS on Pentium+?
			{
				CPU[activeCPU].SEG_DESCRIPTOR[segment].desc.noncallgate_info &= ~0x40; //Clear the B-bit only, to enforce 16-bit code when loading CS in real mode! Leave the Granularity and Limit fields alone!
			}
			if ((EMULATED_CPU < CPU_PENTIUM) || (getcpumode() == CPU_MODE_8086)) //Only before Pentium or V86 mode?
			{
				CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS].desc.AccessRights = 0x93; //Load default access rights!
			}
			CPU_calcSegmentPrecalcs(1, &CPU[activeCPU].SEG_DESCRIPTOR[segment]); //Calculate any precalcs for the segment descriptor(do it here since we don't load descriptors externally)!
			REG_EIP = CPU[activeCPU].destEIP; //... The current OPCode: just jump to the address!
			if (CPU_MMU_checkrights(CPU_SEGMENT_CS, REG_CS, REG_EIP, 0x40|3, &CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS], 2, CPU[activeCPU].CPU_Operand_size)) //Limit broken or protection fault?
			{
				THROWDESCGP(0, 0, 0); //#GP(0) when out of limit range!
				return 1; //Abort on fault!
			}
			if (CPU_condflushPIQ(-1)) return 1; //We're jumping to another address!
			break;
		case CPU_SEGMENT_SS: //SS? We're also updating the CPL!
			updateCPL(); //Update the CPL according to the mode!
			//Fall through to normal segments for it's precalcs!
		default: //All other segments!
			CPU_calcSegmentPrecalcs(0, &CPU[activeCPU].SEG_DESCRIPTOR[segment]); //Calculate any precalcs for the segment descriptor(do it here since we don't load descriptors externally)!
			break;
		}
	}
	return 0; //No fault raised&continue!
}

int checkPrivilegedInstruction() //Allowed to run a privileged instruction?
{
	if (getCPL()) //Not allowed when CPL isn't zero?
	{
		THROWDESCGP(0,0,0); //Throw a descriptor fault!
		return 0; //Not allowed to run!
	}
	return 1; //Allowed to run!
}

//Used by the MMU! forreading: 0=Writes, 1=Read normal, 3=Read opcode fetch. bit8=Use SS instead of 0 for the error code, bit9=bit10 contains EXT bit to use!
int CPU_MMU_checklimit(int segment, word segmentval, uint_64 offset, word forreading, byte is_offset16) //Determines the limit of the segment, forreading=2 when reading an opcode!
{
	byte rights;
	//Determine the Limit!
	if (likely(EMULATED_CPU >= CPU_80286)) //Handle like a 80286+?
	{
		if (unlikely(segment==-1))
		{
			CPU[activeCPU].CPU_MMU_checkrights_cause = 0x80; //What cause?
			return 0; //Enable: we're an emulator call!
		}
		
		//Use segment descriptors, even when in real mode on 286+ processors!
		rights = CPU_MMU_checkrights(segment,segmentval, offset, (byte)forreading, &CPU[activeCPU].SEG_DESCRIPTOR[segment],1,is_offset16); //What rights resulting? Test the address itself too!
		if (unlikely(rights)) //Error?
		{
			switch (rights)
			{
			default: //Unknown status? Count #GP by default!
			case 1: //#GP(0) or pseudo protection fault(Real/V86 mode(V86 mode only during limit range exceptions, otherwise error code 0))?
				if (unlikely((forreading&0x10)==0)) CPU_GP(((getcpumode()==CPU_MODE_PROTECTED) || (!(((CPU[activeCPU].CPU_MMU_checkrights_cause==6) && (getcpumode()==CPU_MODE_8086)) || (getcpumode()==CPU_MODE_REAL))))?(0|((forreading&0x200)?((forreading&0x400)>>10):0)):-5); //Throw (pseudo) fault when not prefetching!
				return 1; //Error out!
				break;
			case 2: //#NP?
				if (unlikely((forreading&0x10)==0)) THROWDESCNP(segmentval,0,(segmentval&4)?EXCEPTION_TABLE_LDT:EXCEPTION_TABLE_GDT); //Throw error: accessing non-present segment descriptor when not prefetching!
				return 1; //Error out!
				break;
			case 3: //#SS(0) or pseudo protection fault(Real/V86 mode)?
				if (unlikely((forreading&0x10)==0)) CPU_StackFault(((getcpumode()==CPU_MODE_PROTECTED) || (!(((CPU[activeCPU].CPU_MMU_checkrights_cause==6) && (getcpumode()==CPU_MODE_8086)) || (getcpumode()==CPU_MODE_REAL))))?((((forreading&0x100))?((REG_SS&0xFFF8)|(((REG_SS&4)?EXCEPTION_TABLE_LDT:EXCEPTION_TABLE_GDT)<<1)):0)|((forreading&0x200)?((forreading&0x400)>>10):0)):-5); //Throw (pseudo) fault when not prefetching! Set EXT bit(bit7) when requested(bit6) and give SS instead of 0!
				return 1; //Error out!
				break;
			}
		}
		return 0; //OK!
	}
	return 0; //Don't give errors: handle like a 80(1)86!
}

byte checkInterruptFlagRestricted() //Check special rights, common by any interrupt flag instructions!
{
	if (getcpumode() == CPU_MODE_REAL) return 0; //Allow all for real mode!
	if (FLAG_PL < getCPL()) //We're not allowed!
	{
		return 1; //Not priviledged!
	}
	return 0; //Priviledged!
}

byte checkPortRightsRestricted() //Check special rights, common by any port rights instructions!
{
	if (getcpumode() == CPU_MODE_REAL) return 0; //Allow all for real mode!
	if ((getCPL()>FLAG_PL)||isV86()) //We're to check when not priviledged or Virtual 8086 mode!
	{
		return 1; //Not priviledged!
	}
	return 0; //Priviledged!
}

byte checkSTICLI() //Check STI/CLI rights!
{
	if (checkInterruptFlagRestricted()) //Not priviledged?
	{
		THROWDESCGP(0,0,0); //Raise exception!
		return 0; //Ignore this command!
	}
	return 1; //We're allowed to execute!
}

byte disallowPOPFI() //Allow POPF to change interrupt flag?
{
	return checkInterruptFlagRestricted(); //Simply ignore the change when not priviledged!
}

byte checkPortRights(word port) //Are we allowed to not use this port?
{
	if (checkPortRightsRestricted()) //We're to check the I/O permission bitmap! 286+ only!
	{
		CPU[activeCPU].protection_PortRightsLookedup = 1; //The port rights are looked up!
		uint_32 maplocation;
		byte mappos;
		byte mapvalue;
		word mapbase;
		maplocation = (port>>3); //8 bits per byte!
		mappos = (1<<(port&7)); //The bit within the byte specified!
		mapvalue = 1; //Default to have the value 1!
		if (((GENERALSEGMENT_TYPE(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_TR]) == AVL_SYSTEM_BUSY_TSS32BIT) || (GENERALSEGMENT_TYPE(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_TR]) == AVL_SYSTEM_TSS32BIT)) && REG_TR && GENERALSEGMENT_P(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_TR])) //Active 32-bit TSS?
		{
			uint_64 limit;
			limit = CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_TR].PRECALCS.limit; //The limit of the descriptor!
			if (limit >= 0x67) //Valid to check?
			{
				if (checkMMUaccess16(CPU_SEGMENT_TR, REG_TR, 0x66, 0x40 | 1, 0, 1, 0)) return 2; //Check if the address is valid according to segmentation!
				if (checkMMUaccess16(CPU_SEGMENT_TR, REG_TR, 0x66, 0xA0 | 1, 0, 1, 0)) return 2; //Check if the address is valid according to the remainder of checks!
				mapbase = MMU_rw0(CPU_SEGMENT_TR, REG_TR, 0x66, 0, 1); //Add the map location to the specified address!
				maplocation += mapbase; //The actual location!
				//Custom, not in documentation: 
				if ((maplocation <= limit) && (mapbase < limit) && (mapbase >= 0x68)) //Not over the limit? We're an valid entry! There is no map when the base address is greater than or equal to the TSS limit().
				{
					if (checkMMUaccess(CPU_SEGMENT_TR, REG_TR, maplocation, 0x40 | 1, 0, 1, 0)) return 2; //Check if the address is valid according to segmentation!
					if (checkMMUaccess(CPU_SEGMENT_TR, REG_TR, maplocation, 0xA0 | 1, 0, 1, 0)) return 2; //Check if the address is valid according to the remainder of checks!
					mapvalue = (MMU_rb0(CPU_SEGMENT_TR, REG_TR, maplocation, 0, 1)&mappos); //We're the bit to use!
				}
			}
		}
		if (mapvalue) //The map bit is set(or not a 32-bit task)? We're to trigger an exception!
		{
			CPU[activeCPU].portExceptionResult = checkProtectedModeDebugger(port, PROTECTEDMODEDEBUGGER_TYPE_IOREADWRITE); //Check for the debugger always!
			return (CPU[activeCPU].portExceptionResult|1); //Trigger an exception!
		}
	}
	CPU[activeCPU].portExceptionResult = checkProtectedModeDebugger(port, PROTECTEDMODEDEBUGGER_TYPE_IOREADWRITE); //Check for the debugger always!
	return 0; //Allow all for now!
}

byte getTSSIRmap(word intnr) //What are we to do with this interrupt? 0=Perform V86 real-mode interrupt. 1=Perform protected mode interrupt(legacy). 2=Faulted on the TSS; Abort INT instruction processing.
{
	uint_32 maplocation;
	byte mappos;
	byte mapvalue;
	word mapbase;
	maplocation = (intnr>>3); //8 bits per byte!
	mappos = (1<<(intnr&7)); //The bit within the byte specified!
	mapvalue = 1; //Default to have the value 1!
	if (((GENERALSEGMENT_TYPE(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_TR]) == AVL_SYSTEM_BUSY_TSS32BIT) || (GENERALSEGMENT_TYPE(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_TR]) == AVL_SYSTEM_TSS32BIT)) && REG_TR && GENERALSEGMENT_P(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_TR])) //Active 32-bit TSS?
	{
		uint_64 limit;
		limit = CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_TR].PRECALCS.limit; //The limit of the descriptor!
		if (limit >= 0x67) //Valid to check?
		{
			if (checkMMUaccess16(CPU_SEGMENT_TR, REG_TR, 0x66, 0x40 | 1, 0, 1, 0)) return 2; //Check if the address is valid according to segmentation!
			if (checkMMUaccess16(CPU_SEGMENT_TR, REG_TR, 0x66, 0xA0 | 1, 0, 1, 0)) return 2; //Check if the address is valid according to the remainder of checks!
			mapbase = MMU_rw0(CPU_SEGMENT_TR, REG_TR, 0x66, 0, 1); //Add the map location to the specified address!
			//Custom, not in documentation: 
			if (((mapbase-1U) <= limit) && (mapbase >= (0x68U+0x20U))) //Not over the limit? We're an valid entry! There is no map when the base address is greater than or equal to the TSS limit().
			{
				maplocation += mapbase; //The actual location!
				maplocation -= 0x20; //Start of the IR map!
				if (checkMMUaccess(CPU_SEGMENT_TR, REG_TR, maplocation, 0x40 | 1, 0, 1, 0)) return 2; //Check if the address is valid according to segmentation!
				if (checkMMUaccess(CPU_SEGMENT_TR, REG_TR, maplocation, 0xA0 | 1, 0, 1, 0)) return 2; //Check if the address is valid according to the remainder of checks!
				mapvalue = (MMU_rb0(CPU_SEGMENT_TR, REG_TR, maplocation, 0, 1)&mappos); //We're the bit to use!
			}
		}
	}
	if (mapvalue) //The map bit is set(or not a 32-bit task)?
	{
		return 1; //Count as set!
	}
	return 0; //Allow all for now!
}

//bit2=EXT when set.
byte switchStacks(byte newCPL)
{
	byte stackresult;
	word SSn;
	uint_32 ESPn;
	byte TSSSize;
	word TSS_StackPos;
	TSSSize = 0; //Default to 16-bit TSS!
	switch (GENERALSEGMENT_TYPE(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_TR])) //What kind of TSS?
	{
	case AVL_SYSTEM_BUSY_TSS32BIT:
	case AVL_SYSTEM_TSS32BIT:
		TSSSize = 1; //32-bit TSS!
	case AVL_SYSTEM_BUSY_TSS16BIT:
	case AVL_SYSTEM_TSS16BIT:
		TSS_StackPos = (2<<TSSSize); //Start of the stack block! 2 for 16-bit TSS, 4 for 32-bit TSS!
		TSS_StackPos += (4<<TSSSize)*(newCPL&3); //Start of the correct TSS (E)SP! 4 for 16-bit TSS, 8 for 32-bit TSS!
		//Check against memory first!
		//First two are the SP!
		if ((stackresult = checkMMUaccess16(CPU_SEGMENT_TR, REG_TR, TSS_StackPos, 0x40 | 1, 0, 1, 0))!=0) return (stackresult==2)?2:1; //Check if the address is valid according to segmentation!
		//Next two are either high ESP or SS!
		if ((stackresult = checkMMUaccess16(CPU_SEGMENT_TR, REG_TR, TSS_StackPos+2, 0x40 | 1, 0, 1, 0))!=0) return (stackresult==2)?2:1; //Check if the address is valid according to segmentation!
		if (TSSSize) //Extra checks for 32-bit?
		{
			//The 32-bit TSS SSn value!
			if ((stackresult = checkMMUaccess16(CPU_SEGMENT_TR, REG_TR, TSS_StackPos+4, 0x40 | 1, 0, 1, 0))!=0) return (stackresult==2)?2:1; //Check if the address is valid according to segmentation!
		}
		//First two are the SP!
		if ((stackresult = checkMMUaccess16(CPU_SEGMENT_TR, REG_TR, TSS_StackPos, 0xA0 | 1, 0, 1, 0))!=0) return (stackresult==2)?2:1; //Check if the address is valid according to the remainder of checks!
		//Next two are either high ESP or SS!
		if ((stackresult = checkMMUaccess16(CPU_SEGMENT_TR, REG_TR, TSS_StackPos+2, 0xA0 | 1, 0, 1, 0))!=0) return (stackresult==2)?2:1; //Check if the address is valid according to the remainder of checks!
		if (TSSSize) //Extra checks for 32-bit?
		{
			//The 32-bit TSS SSn value!
			if ((stackresult = checkMMUaccess16(CPU_SEGMENT_TR, REG_TR, TSS_StackPos+4, 0xA0 | 1, 0, 1, 0))!=0) return (stackresult==2)?2:1; //Check if the address is valid according to the remainder of checks!
		}
		//Memory is now validated! Load the values from memory!

		ESPn = TSSSize?MMU_rdw0(CPU_SEGMENT_TR,REG_TR,TSS_StackPos,0,1):MMU_rw0(CPU_SEGMENT_TR,REG_TR,TSS_StackPos,0,1); //Read (E)SP for the privilege level from the TSS!
		TSS_StackPos += (2<<TSSSize); //Convert the (E)SP location to SS location!
		SSn = MMU_rw0(CPU_SEGMENT_TR,REG_TR,TSS_StackPos,0,1); //SS!
		if ((stackresult = segmentWritten(CPU_SEGMENT_SS,SSn,0x8000|0x200|((newCPL<<8)&0x400)|0x1000|((newCPL&3)<<13)))) return (stackresult==2)?2:1; //Read SS, privilege level changes, ignore DPL vs CPL check! Fault=#TS. EXT bit when set in bit 2 of newCPL. Don't throw #SS for normal faults, throw #TS instead!
		if (TSSSize) //32-bit?
		{
			REG_ESP = ESPn; //Apply the stack position!
		}
		else
		{
			REG_SP = (word)ESPn; //Apply the stack position!
		}
	default: //Unknown TSS?
		break; //No switching for now!
	}
	return 0; //OK!
}

byte CPU_ProtectedModeInterrupt(byte intnr, word returnsegment, uint_32 returnoffset, int_64 errorcode, byte is_interrupt) //Execute a protected mode interrupt!
{
	byte result;
	byte left; //The amount of bytes left to read of the IDT entry!
	uint_32 base;
	base = (intnr<<3); //The base offset of the interrupt in the IDT!

	CPU[activeCPU].hascallinterrupttaken_type = (getCPL())?INTERRUPTGATETIMING_SAMELEVEL:INTERRUPTGATETIMING_DIFFERENTLEVEL; //Assume we're jumping to CPL0 when erroring out!

	CPU[activeCPU].executed = 0; //Default: still busy executing!
	if (CPU[activeCPU].faultraised==2) CPU[activeCPU].faultraised = 0; //Clear non-fault, if present!

	byte isEXT;
	isEXT = (is_interrupt&0x10)?1:((is_interrupt&1)?0:((is_interrupt&4)>>2))|((errorcode>=0)?(errorcode&1):0); //The EXT bit to use for direct exceptions! 0 for interrupts, 1 for exceptions!

	if ((base|0x7) > CPU[activeCPU].registers->IDTR.limit) //Limit exceeded?
	{
		THROWDESCGP(base,isEXT,EXCEPTION_TABLE_IDT); //#GP!
		return 0; //Abort!
	}

	base += CPU[activeCPU].registers->IDTR.base; //Add the base for the actual offset into the IDT!
	
	RAWSEGMENTDESCRIPTOR idtentry; //The loaded IVT entry!
	if ((result = checkDirectMMUaccess(base, 1,/*getCPL()*/ 0))!=0) //Error in the paging unit?
	{
		return (result==2)?0:1; //Error out!
	}
	if ((result = checkDirectMMUaccess(base+sizeof(idtentry.descdata)-1, 1,/*getCPL()*/ 0))!=0) //Error in the paging unit?
	{
		return (result==2)?0:1; //Error out!
	}
	for (left=0;left<sizeof(idtentry.descdata);) //Data left to read?
	{
		if (memory_readlinear(base++,&idtentry.descdata[left++])) //Read a descriptor byte directly from flat memory!
		{
			return 0; //Failed to load the descriptor!
		}
	}
	base -= sizeof(idtentry.descdata); //Restore start address!
	base -= CPU[activeCPU].registers->IDTR.base; //Substract the base for the actual offset into the IDT!
	//Now, base is the restored vector into the IDT!

	idtentry.offsethigh = DESC_16BITS(idtentry.offsethigh); //Patch when needed!
	idtentry.offsetlow = DESC_16BITS(idtentry.offsetlow); //Patch when needed!
	idtentry.selector = DESC_16BITS(idtentry.selector); //Patch when needed!
	return CPU_handleInterruptGate(isEXT,EXCEPTION_TABLE_IDT,base,&idtentry,returnsegment,returnoffset,errorcode,is_interrupt); //Handle the interrupt gate!
}

byte CPU_handleInterruptGate(byte EXT, byte table,uint_32 descriptorbase, RAWSEGMENTDESCRIPTOR *theidtentry, word returnsegment, uint_32 returnoffset, int_64 errorcode, byte is_interrupt) //Execute a protected mode interrupt!
{
	byte stackresult;
	uint_32 errorcode32 = (uint_32)errorcode; //Get the error code itelf!
	word errorcode16 = (word)errorcode; //16-bit variant, if needed!
	SEGMENT_DESCRIPTOR newdescriptor; //Temporary storage for task switches!
	word desttask; //Destination task for task switches!
	uint_32 base;
	sbyte loadresult;
	byte oldCPL;
	base = descriptorbase; //The base offset of the interrupt in the IDT!
	oldCPL = getCPL(); //Save the old CPL for reference!

	CPU[activeCPU].hascallinterrupttaken_type = (getRPL(theidtentry->selector)==oldCPL)?INTERRUPTGATETIMING_SAMELEVEL:INTERRUPTGATETIMING_DIFFERENTLEVEL;

	if (errorcode<0) //Invalid error code to use?
	{
		errorcode16 = 0; //Empty to log!
		errorcode32 = 0; //Empty to log!
	}

	EXT &= 1; //1-bit value!

	CPU[activeCPU].executed = 0; //Default: still busy executing!
	if (CPU[activeCPU].faultraised==2) CPU[activeCPU].faultraised = 0; //Clear non-fault, if present!

	byte is32bit;
	RAWSEGMENTDESCRIPTOR idtentry; //The loaded IVT entry!
	memcpy(&idtentry,theidtentry,sizeof(idtentry)); //Make a copy for our own use!

	if ((is_interrupt&1) && /*((is_interrupt&0x10)==0) &&*/ (IDTENTRY_DPL(idtentry) < getCPL()) && (errorcode!=-5) && (errorcode!=-6)) //Not enough rights on software interrupt? Don't fault on a pseudo-interrupt! -6 means normal hanlding, except ignore CPL vs DPL of the IDT descriptor!
	{
		THROWDESCGP(base,EXT,table); //#GP!
		return 0;
	}
	//Now, the (gate) descriptor to use is loaded!
	switch (IDTENTRY_TYPE(idtentry)) //What type are we?
	{
	case IDTENTRY_TASKGATE: //task gate?
	case IDTENTRY_INTERRUPTGATE: //16-bit interrupt gate?
	case IDTENTRY_TRAPGATE: //16-bit trap gate?
		break;
	case IDTENTRY_INTERRUPTGATE|IDTENTRY_32BIT_GATEEXTENSIONFLAG: //32-bit interrupt gate?
	case IDTENTRY_TRAPGATE|IDTENTRY_32BIT_GATEEXTENSIONFLAG: //32-bit trap gate?
		if (EMULATED_CPU>=CPU_80386) break; //OK on 80386+ only!
	default:
		THROWDESCGP(base,EXT,table); //#NP isn't triggered with IDT entries! #GP is triggered instead!
		return 0;
	}

	if (IDTENTRY_P(idtentry)==0) //Not present?
	{
		THROWDESCNP(base,EXT,table); //#NP isn't triggered with IDT entries! #GP is triggered instead?
		return 0;
	}

	//Now, the (gate) descriptor to use is loaded!
	switch (IDTENTRY_TYPE(idtentry)) //What type are we?
	{
	case IDTENTRY_TASKGATE: //task gate?
		desttask = idtentry.selector; //Read the destination task!
		if (((loadresult = LOADDESCRIPTOR(CPU_SEGMENT_TR, desttask, &newdescriptor,2|(EXT<<10)))<=0) || (desttask&4)) //Error loading new descriptor? The backlink is always at the start of the TSS! It muse also always be in the GDT!
		{
			if (loadresult >= 0) //Not faulted already?
			{
				THROWDESCGP(desttask, EXT, (desttask & 4) ? EXCEPTION_TABLE_LDT : EXCEPTION_TABLE_GDT); //Throw #GP error!
			}
			return 0; //Error, by specified reason!
		}
		CPU_executionphase_starttaskswitch(CPU_SEGMENT_TR, &newdescriptor, &REG_TR, desttask, ((2|0x80)|(EXT<<10)),1,errorcode); //Execute a task switch to the new task! We're switching tasks like a CALL instruction(https://xem.github.io/minix86/manual/intel-x86-and-64-manual-vol3/o_fe12b1e2a880e0ce-250.html)! We're a call based on an interrupt!
		break;
	default: //All other cases?
		is32bit = ((IDTENTRY_TYPE(idtentry)&IDTENTRY_32BIT_GATEEXTENSIONFLAG)>>IDTENTRY_32BIT_GATEEXTENSIONFLAG_SHIFT); //Enable 32-bit gate?
		switch (IDTENTRY_TYPE(idtentry) & 0x7) //What type are we?
		{
		case IDTENTRY_INTERRUPTGATE: //interrupt gate?
		case IDTENTRY_TRAPGATE: //trap gate?
			CPU[activeCPU].hascallinterrupttaken_type = (getRPL(idtentry.selector)==oldCPL)?INTERRUPTGATETIMING_SAMELEVEL:INTERRUPTGATETIMING_DIFFERENTLEVEL;

			//Table can be found at: http://www.read.seas.harvard.edu/~kohler/class/04f-aos/ref/i386/s15_03.htm#fig15-3

			if ((loadresult = LOADDESCRIPTOR(CPU_SEGMENT_CS, idtentry.selector, &newdescriptor,2))<=0) //Error loading new descriptor? The backlink is always at the start of the TSS!
			{
				if (loadresult==0) //Not faulted already?
				{
					THROWDESCGP(idtentry.selector,EXT,(idtentry.selector&4)?EXCEPTION_TABLE_LDT:EXCEPTION_TABLE_GDT); //Throw error!
				}
				return 0; //Error, by specified reason!
			}

			CPU[activeCPU].hascallinterrupttaken_type = (GENERALSEGMENT_DPL(newdescriptor)==oldCPL)?INTERRUPTGATETIMING_SAMELEVEL:INTERRUPTGATETIMING_DIFFERENTLEVEL; //Assume destination privilege level for faults!

			if (
				(getLoadedTYPE(&newdescriptor) != 1) //Not an executable segment?
					) //NULL descriptor loaded? Invalid too(done by the above present check too)!
			{
				THROWDESCGP(idtentry.selector,EXT,(idtentry.selector&4)?EXCEPTION_TABLE_LDT:EXCEPTION_TABLE_GDT); //Throw error!
				return 0; //Not present: limit exceeded!	
			}

			if (!GENERALSEGMENT_P(newdescriptor)) //Not present?
			{
				THROWDESCNP(idtentry.selector,EXT,(idtentry.selector&4)?EXCEPTION_TABLE_LDT:EXCEPTION_TABLE_GDT); //Throw #GP!
				return 0;
			}

			byte INTTYPE=0;

			if ((EXECSEGMENT_C(newdescriptor) == 0) && (GENERALSEGMENT_DPL(newdescriptor)<getCPL())) //Not enough rights, but conforming?
			{
				INTTYPE = 1; //Interrupt to inner privilege!
			}
			else
			{
				if (((EXECSEGMENT_C(newdescriptor)) && (GENERALSEGMENT_DPL(newdescriptor)<getCPL())) || (GENERALSEGMENT_DPL(newdescriptor)==getCPL())) //Conforming with more or same privilege or non-conforming with same privilege?
				{
					INTTYPE = 2; //Interrupt to same privilege level!
				}
				else
				{
					THROWDESCGP(idtentry.selector,EXT,(idtentry.selector&4)?EXCEPTION_TABLE_LDT:EXCEPTION_TABLE_GDT); //Throw #GP!
					return 0;
				}
			}

			uint_32 EFLAGSbackup;
			EFLAGSbackup = REG_EFLAGS; //Back-up EFLAGS!

			byte newCPL;
			newCPL = GENERALSEGMENT_DPL(newdescriptor); //New CPL to use!

			byte forcepush32;
			forcepush32 = 0; //Default: automatic 32-bit push!

			if (FLAG_V8 && (INTTYPE==1)) //Virtual 8086 mode to monitor switching to CPL 0?
			{
				CPU[activeCPU].hascallinterrupttaken_type = (newCPL==oldCPL)?INTERRUPTGATETIMING_SAMELEVEL:INTERRUPTGATETIMING_DIFFERENTLEVEL;
				#ifdef LOG_VIRTUALMODECALLS
				if ((MMU_logging == 1) && advancedlog)
				{
					dolog("debugger", "Starting V86 interrupt/fault: INT %02X(%02X(0F:%02X)),immb:%02X,AX=%04X)", intnr, CPU[activeCPU].currentopcode, CPU[activeCPU].currentopcode0F, immb, REG_AX);
				}
				#endif
				if (newCPL!=0) //Not switching to PL0?
				{
					THROWDESCGP(idtentry.selector,EXT,EXCEPTION_TABLE_GDT); //Exception!
					return 0; //Abort on fault!
				}

				//Now, switch to the new EFLAGS!
				FLAGW_V8(0); //Clear the Virtual 8086 mode flag!
				updateCPUmode(); //Update the CPU mode!

				//We're back in protected mode now!

				//Switch Stack segment first!
				if ((stackresult = switchStacks(newCPL|(EXT<<2)))!=0) return (stackresult!=2)?1:0; //Abort failing switching stacks!
				//Verify that the new stack is available!
				if ((stackresult = checkStackAccess(9+((errorcode>=0)?1:0),1|0x100|0x200|((EXT&1)<<10),is32bit?1:0))!=0) return 0; //Abort on fault! Different privileges throws #SS(SS) instead of #SS(0)!

				//Calculate and check the limit!

				if (invalidLimit(&newdescriptor,((idtentry.offsetlow | (idtentry.offsethigh << 16))&(0xFFFFFFFFU>>((is32bit^1)<<4))))) //Limit exceeded?
				{
					THROWDESCGP(0,0,0); //Throw #GP(0)!
					return 0;
				}

				//Save the Segment registers on the new stack! Always push in 32-bit quantities(pad to 32-bits) when in 32-bit mode, according to documentation?
				uint_32 val;
				if (is32bit)
				{
					val = REG_GS;
					CPU_PUSH32(&val);
					val = REG_FS;
					CPU_PUSH32(&val);
					val = REG_DS;
					CPU_PUSH32(&val);
					val = REG_ES;
					CPU_PUSH32(&val);
					val = CPU[activeCPU].oldSS;
					CPU_PUSH32(&val);
					val = CPU[activeCPU].oldESP;
					CPU_PUSH32(&val);
				}
				//Note: 16-bit pushes are supposed to be patched by the running V86 monitor before IRETD?
				else //16-bit mode?
				{
					word val16;
					CPU_PUSH16(&REG_GS,0);
					CPU_PUSH16(&REG_FS,0);
					CPU_PUSH16(&REG_DS,0);
					CPU_PUSH16(&REG_ES,0);
					val16 = CPU[activeCPU].oldSS;
					CPU_PUSH16(&val16,0);
					val16 = (CPU[activeCPU].oldESP&0xFFFF);
					CPU_PUSH16(&val16,0);
				}

				//Other registers are the normal variants!

				//Load all Segment registers with zeroes!
				if (segmentWritten(CPU_SEGMENT_DS,0,(EXT<<10))) return 0; //Clear DS! Abort on fault!
				if (segmentWritten(CPU_SEGMENT_ES,0,(EXT<<10))) return 0; //Clear ES! Abort on fault!
				if (segmentWritten(CPU_SEGMENT_FS,0,(EXT<<10))) return 0; //Clear FS! Abort on fault!
				if (segmentWritten(CPU_SEGMENT_GS,0,(EXT<<10))) return 0; //Clear GS! Abort on fault!
			}
			else if (FLAG_V8) 
			{
				THROWDESCGP(idtentry.selector,EXT,EXCEPTION_TABLE_GDT); //Exception!
				return 0; //Abort on fault!
			}
			else if ((FLAG_V8==0) && (INTTYPE==1)) //Privilege level changed in protected mode?
			{
				//Unlike the other case, we're still in protected mode!
				//We're back in protected mode now!

				//Switch Stack segment first!
				if ((stackresult = switchStacks(newCPL|(EXT<<2)))!=0) return (stackresult!=2)?1:0; //Abort failing switching stacks!

				//Verify that the new stack is available!
				if ((stackresult = checkStackAccess(5+((errorcode>=0)?1:0),1|0x100|0x200|((EXT&1)<<10),is32bit?1:0))!=0) return 0; //Abort on fault! Different privileges throws #SS(SS) instead of #SS(0)!

				//Calculate and check the limit!

				if (invalidLimit(&newdescriptor,((idtentry.offsetlow | (idtentry.offsethigh << 16))&(0xFFFFFFFFU>>((is32bit^1)<<4))))) //Limit exceeded?
				{
					THROWDESCGP(0,0,0); //Throw #GP(0)!
					return 0;
				}

				if (is32bit) //32-bit gate?
				{
					uint_32 temp;
					temp = CPU[activeCPU].oldSS;
					CPU_PUSH32(&temp);
					CPU_PUSH32(&CPU[activeCPU].oldESP);
				}
				else //16-bit gate?
				{
					word temp = (word)(CPU[activeCPU].oldESP&0xFFFF); //Backup SP!
					CPU_PUSH16(&CPU[activeCPU].oldSS,0);
					CPU_PUSH16(&temp,0); //Old SP!
				}
				//Other registers are the normal variants!
			}
			else //No privilege level change?
			{
				if ((stackresult = checkStackAccess(3+((errorcode>=0)?1:0),1|0x200|((EXT&1)<<10),is32bit?1:0))!=0) return 0; //Abort on fault!
				//Calculate and check the limit!

				if (invalidLimit(&newdescriptor,((idtentry.offsetlow | (idtentry.offsethigh << 16))&(0xFFFFFFFFU>>((is32bit^1)<<4))))) //Limit exceeded?
				{
					THROWDESCGP(0,0,0); //Throw #GP(0)!
					return 0;
				}
			}

			if (is32bit || forcepush32)
			{
				CPU_PUSH32(&EFLAGSbackup); //Push original EFLAGS!
				uint_32 val;
				val = REG_CS;
				CPU_PUSH32(&val);
				CPU_PUSH32(&REG_EIP); //Push EIP!
			}
			else
			{
				word temp2 = (word)(EFLAGSbackup&0xFFFF);
				CPU_PUSH16(&temp2,0); //Push FLAGS!
				CPU_PUSH16(&REG_CS, 0); //Push CS!
				CPU_PUSH16(&REG_IP,0); //Push IP!
			}

			if ((CPU[activeCPU].have_oldSegReg&(1 << CPU_SEGMENT_CS)) == 0) //Backup not loaded yet?
			{
				memcpy(&CPU[activeCPU].SEG_DESCRIPTORbackup[CPU_SEGMENT_CS], &CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS], sizeof(CPU[activeCPU].SEG_DESCRIPTORbackup[0])); //Restore the descriptor!
				CPU[activeCPU].oldSegReg[CPU_SEGMENT_CS] = *CPU[activeCPU].SEGMENT_REGISTERS[CPU_SEGMENT_CS]; //Backup the register too!
				CPU[activeCPU].have_oldSegReg |= (1 << CPU_SEGMENT_CS); //Loaded!
			}
			memcpy(&CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS], &newdescriptor, sizeof(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS])); //Load the segment descriptor into the cache!
			*CPU[activeCPU].SEGMENT_REGISTERS[CPU_SEGMENT_CS] = idtentry.selector; //Set the segment register to the allowed value!

			setRPL(*CPU[activeCPU].SEGMENT_REGISTERS[CPU_SEGMENT_CS],getCPL()); //CS.RPL=CPL!

			REG_EIP = ((idtentry.offsetlow | (idtentry.offsethigh << 16))&(0xFFFFFFFFU >> ((is32bit ^ 1) << 4))); //The current OPCode: just jump to the address specified by the descriptor OR command!

			FLAGW_TF(0);
			FLAGW_NT(0);
			FLAGW_RF(0); //Clear Resume flag too!

			if (EMULATED_CPU >= CPU_80486)
			{
				FLAGW_AC(0); //Clear Alignment Check flag too!
			}

			if ((IDTENTRY_TYPE(idtentry) & 0x7) == IDTENTRY_INTERRUPTGATE)
			{
				FLAGW_IF(0); //No interrupts!
			}
			updateCPUmode(); //flags have been updated!

			if ((errorcode>=0)) //Error code specified?
			{
				if (/*SEGDESC_NONCALLGATE_D_B(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_TR])&CPU[activeCPU].D_B_Mask*/ is32bit || forcepush32) //32-bit task?
				{
					CPU_PUSH32(&errorcode32); //Push the error on the stack!
				}
				else
				{
					CPU_PUSH16(&errorcode16,is32bit); //Push the error on the stack!
				}
			}

			if ((loadresult = touchSegment(CPU_SEGMENT_CS,idtentry.selector,&newdescriptor,2))<=0) //Errored out during touching?
			{
				if (loadresult == 0) //Not already faulted?
				{
					if (2&0x200) //TSS is the cause?
					{
						THROWDESCTS(idtentry.selector,1,(idtentry.selector&4)?EXCEPTION_TABLE_LDT:EXCEPTION_TABLE_GDT); //Throw error!			
					}
					else //Plain #GP?
					{
						THROWDESCGP(idtentry.selector,((idtentry.selector&0x400)>>10),(idtentry.selector&4)?EXCEPTION_TABLE_LDT:EXCEPTION_TABLE_GDT); //Throw error!
					}
				}
				if (loadresult == -1) //Errored out?
				{
					CPU_flushPIQ(-1); //We're jumping to another address!
				}
				return (loadresult==-2)?0:1; //Abort on fault!
			}

			CPU[activeCPU].hascallinterrupttaken_type = (getCPL()==oldCPL)?INTERRUPTGATETIMING_SAMELEVEL:INTERRUPTGATETIMING_DIFFERENTLEVEL;
			CPU[activeCPU].executed = 1; //We've executed, start any post-instruction stuff!
			CPU_interruptcomplete(); //Prepare us for new instructions!
			CPU_flushPIQ(-1); //We're jumping to another address!
			return 1; //OK!
			break;
		default: //Unknown descriptor type?
			THROWDESCGP(base,EXT,table); //#GP! We're always from the IDT!
			return 0; //Errored out!
			break;
		}
		break;
	}
	return 0; //Default: Errored out!
}
