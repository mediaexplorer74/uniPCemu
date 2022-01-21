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

#include "headers/cpu/cpu.h" //CPU reqs!
#include "headers/cpu/multitasking.h" //Our typedefs!
#include "headers/mmu/mmuhandler.h" //Direct MMU support!
#include "headers/cpu/cpu_pmtimings.h" //286+ timing support!
#include "headers/cpu/easyregs.h" //Easy register support!
#include "headers/support/log.h" //Logging support!
#include "headers/emu/debugger/debugger.h" //Debugging support!
#include "headers/cpu/protecteddebugging.h" //Protected mode debugging support!
#include "headers/cpu/biu.h" //BIU support!
#include "headers/cpu/cpu_execution.h" //Execution flow support!
#include "headers/cpu/cpu_stack.h" //Stack support!
#include "headers/cpu/easyregs.h" //Easy register support!

//Force 16-bit TSS on 80286?
//#define FORCE_16BITTSS

//Reading of the 16-bit entries within descriptors!
#define DESC_16BITS(x) SDL_SwapLE16(x)
#define DESC_32BITS(x) SDL_SwapLE32(x)

//Everything concerning TSS.

extern byte debugger_forceimmediatelogging; //Force immediate logging?

void loadTSS16(TSS286 *TSS)
{
	word n;
	byte i;
	i = 0;
	n = 0;
	for (i = 0;i < NUMITEMS(TSS->dataw);) //Load our TSS!
	{
		debugger_forceimmediatelogging = 1; //Log!
		TSS->dataw[i++] = MMU_rw(CPU_SEGMENT_TR, REG_TR, n, 0,0); //Read the TSS! Don't be afraid of errors, since we're always accessable!
		n += 2; //Next word!
	}
	debugger_forceimmediatelogging = 0; //Don't log!
}

byte checkloadTSS16(void *segdesc, word value)
{
	word n;
	for (n = 0;n < 0x2C;n+=2) //Load our TSS!
	{
		debugger_forceimmediatelogging = 1; //Log!
		if (checkPhysMMUaccess16(segdesc, value, n, 1|0x40, 0, 0, 0)) {debugger_forceimmediatelogging = 0; return 1;} //Error out!
	}
	for (n = 0; n < 0x2C; n += 2) //Load our TSS!
	{
		debugger_forceimmediatelogging = 1; //Log!
		if (checkPhysMMUaccess16(segdesc, value, n, 1|0xA0, 0, 0, 0)) { debugger_forceimmediatelogging = 0; return 1; } //Error out!
	}

	debugger_forceimmediatelogging = 0; //Don't log!
	return 0; //OK!
}

void loadTSS32(TSS386 *TSS)
{
	byte ssspreg;
	word n;
	byte i;
	debugger_forceimmediatelogging = 1; //Log!
	TSS->BackLink = MMU_rw(CPU_SEGMENT_TR, REG_TR, 0, 0,0); //Read the TSS! Don't be afraid of errors, since we're always accessable!
	//SP0/ESP0 initializing!
	n = 4; //Start of our block!
	for (ssspreg=0;ssspreg<3;++ssspreg) //Read all required stack registers!
	{
		debugger_forceimmediatelogging = 1; //Log!
		TSS->ESPs[ssspreg] = MMU_rdw(CPU_SEGMENT_TR, REG_TR, n, 0,0); //Read the TSS! Don't be afraid of errors, since we're always accessable!
		n += 4; //Next item!
		debugger_forceimmediatelogging = 1; //Log!
		TSS->SSs[ssspreg++] = MMU_rw(CPU_SEGMENT_TR, REG_TR, n, 0,0); //Read the TSS! Don't be afraid of errors, since we're always accessable!

		n += 4; //Next item!
	}

	i = 0;
	for (n=(7*4);n<((7+11)*4);n+=4) //Write our TSS 32-bit data!
	{
		debugger_forceimmediatelogging = 1; //Log!
		TSS->generalpurposeregisters[i++] = MMU_rdw(CPU_SEGMENT_TR, REG_TR, n, 0,0); //Read the TSS! Don't be afraid of errors, since we're always accessable!
	}

	i = 0;
	for (n=(((7+11)*4));n<((7+11+7)*4);n+=4) //Write our TSS 16-bit data!
	{
		debugger_forceimmediatelogging = 1; //Log!
		TSS->segmentregisters[i++] = MMU_rw(CPU_SEGMENT_TR, REG_TR, n, 0,0); //Read the TSS! Don't be afraid of errors, since we're always accessable!
	}

	debugger_forceimmediatelogging = 1; //Log!
	TSS->T = MMU_rw(CPU_SEGMENT_TR, REG_TR, (25 * 4), 0, 0); //Read the TSS! Don't be afraid of errors, since we're always accessable!
	debugger_forceimmediatelogging = 1; //Log!
	TSS->IOMapBase = MMU_rw(CPU_SEGMENT_TR, REG_TR, (25 * 4) + 2, 0, 0); //Read the TSS! Don't be afraid of errors, since we're always accessable!

	debugger_forceimmediatelogging = 0; //Don't log!
}

byte checkloadTSS32(void* segdesc, word value)
{
	byte ssspreg;
	word n;
	debugger_forceimmediatelogging = 1; //Log!
	if (checkPhysMMUaccess16(segdesc, value,0,1|0x40,0,0,0)) {debugger_forceimmediatelogging = 0; return 1;} //Error out!
	//SP0/ESP0 initializing!
	n = 4; //Start of our block!

	for (ssspreg=0;ssspreg<3;++ssspreg) //Read all required stack registers!
	{
		debugger_forceimmediatelogging = 1; //Log!
		if (checkPhysMMUaccess32(segdesc, value,n+0,1|0x40,0,0,0)) {debugger_forceimmediatelogging = 0; return 1;} //Error out!

		n += 4; //Next item!
		debugger_forceimmediatelogging = 1; //Log!
		if (checkPhysMMUaccess16(segdesc, value,n+0,1|0x40,0,0,0)) {debugger_forceimmediatelogging = 0; return 1;} //Error out!
		n += 4; //Next item!
	}

	for (n=(7*4);n<((7+11)*4);n+=4) //Write our TSS 32-bit data!
	{
		debugger_forceimmediatelogging = 1; //Log!
		if (checkPhysMMUaccess32(segdesc, value,n+0,1|0x40,0,0,0)) {debugger_forceimmediatelogging = 0; return 1;} //Error out!
	}

	for (n=(((7+11)*4));n<((7+11+7)*4);n+=4) //Write our TSS 16-bit data!
	{
		debugger_forceimmediatelogging = 1; //Log!
		if (checkPhysMMUaccess32(segdesc, value,n+0,1|0x40,0,0,0)) {debugger_forceimmediatelogging = 0; return 1;} //Error out!
	}

	debugger_forceimmediatelogging = 1; //Log!
	if (checkPhysMMUaccess16(segdesc, value,(25*4)+0,1|0x40,0,0,0)) {debugger_forceimmediatelogging = 0; return 1;} //Error out!
	debugger_forceimmediatelogging = 1; //Log!
	if (checkPhysMMUaccess16(segdesc, value,(25*4)+2,1|0x40,0,0,0)) {debugger_forceimmediatelogging = 0; return 1;} //Error out!
	debugger_forceimmediatelogging = 0; //Don't log!

	debugger_forceimmediatelogging = 1; //Log!
	if (checkPhysMMUaccess16(segdesc, value, 0, 1|0xA0, 0, 0, 0)) { debugger_forceimmediatelogging = 0; return 1; } //Error out!
	//SP0/ESP0 initializing!
	n = 4; //Start of our block!

	for (ssspreg = 0; ssspreg < 3; ++ssspreg) //Read all required stack registers!
	{
		debugger_forceimmediatelogging = 1; //Log!
		if (checkPhysMMUaccess32(segdesc, value, n + 0, 1|0xA0, 0, 0, 0)) { debugger_forceimmediatelogging = 0; return 1; } //Error out!

		n += 4; //Next item!
		debugger_forceimmediatelogging = 1; //Log!
		if (checkPhysMMUaccess16(segdesc, value, n + 0, 1|0xA0, 0, 0, 0)) { debugger_forceimmediatelogging = 0; return 1; } //Error out!
		n += 4; //Next item!
	}

	for (n = (7 * 4); n < ((7 + 11) * 4); n += 4) //Write our TSS 32-bit data!
	{
		debugger_forceimmediatelogging = 1; //Log!
		if (checkPhysMMUaccess32(segdesc, value, n + 0, 1|0xA0, 0, 0, 0)) { debugger_forceimmediatelogging = 0; return 1; } //Error out!
	}

	for (n = (((7 + 11) * 4)); n < ((7 + 11 + 7) * 4); n += 4) //Write our TSS 16-bit data!
	{
		debugger_forceimmediatelogging = 1; //Log!
		if (checkPhysMMUaccess32(segdesc, value, n + 0, 1|0xA0, 0, 0, 0)) { debugger_forceimmediatelogging = 0; return 1; } //Error out!
	}

	debugger_forceimmediatelogging = 1; //Log!
	if (checkPhysMMUaccess16(segdesc, value, (25 * 4) + 0, 1|0xA0, 0, 0, 0)) { debugger_forceimmediatelogging = 0; return 1; } //Error out!
	debugger_forceimmediatelogging = 1; //Log!
	if (checkPhysMMUaccess16(segdesc, value, (25 * 4) + 2, 1|0xA0, 0, 0, 0)) { debugger_forceimmediatelogging = 0; return 1; } //Error out!
	debugger_forceimmediatelogging = 0; //Don't log!

	return 0; //OK!
}

void saveTSS16(TSS286 *TSS)
{
	word n;
	byte i;
	i = 7;
	for (n=((7*2));n<(sizeof(*TSS)-2);n+=2) //Write our TSS 16-bit data! Don't store the LDT and Stacks for different privilege levels!
	{
		debugger_forceimmediatelogging = 1; //Log!
		MMU_ww(CPU_SEGMENT_TR, REG_TR, n, TSS->dataw[i++],0); //Write the TSS! Don't be afraid of errors, since we're always accessable!
	}
	debugger_forceimmediatelogging = 0; //Don't log!
}

byte checksaveTSS16(void* segdesc, word value, byte isBacklinked)
{
	word n;
	if (isBacklinked)
	{
		if (checkPhysMMUaccess16(segdesc, value,0,0|0x40,0,0,0)) {debugger_forceimmediatelogging = 0; return 1;} //Error out!
	}
	for (n=((7*2));n<(0x2C-2);n+=2) //Write our TSS 16-bit data! Don't store the LDT and Stacks for different privilege levels!
	{
		debugger_forceimmediatelogging = 1; //Log!
		if (checkPhysMMUaccess16(segdesc, value,n+0,0|0x40,0,0,0)) {debugger_forceimmediatelogging = 0; return 1;} //Error out!
	}
	if (isBacklinked)
	{
		if (checkPhysMMUaccess16(segdesc, value,0,0|0xA0,0,0,0)) {debugger_forceimmediatelogging = 0; return 1;} //Error out!
	}
	for (n = ((7 * 2)); n < (0x2C - 2); n += 2) //Write our TSS 16-bit data! Don't store the LDT and Stacks for different privilege levels!
	{
		debugger_forceimmediatelogging = 1; //Log!
		if (checkPhysMMUaccess16(segdesc, value, n + 0, 0|0xA0, 0, 0, 0)) { debugger_forceimmediatelogging = 0; return 1; } //Error out!
	}
	debugger_forceimmediatelogging = 0; //Don't log!
	return 0; //OK!
}

void saveTSS32(TSS386 *TSS)
{
	word n;
	byte i;
	i = 1;
	for (n =(8*4);n<((8+10)*4);n+=4) //Write our TSS 32-bit data! Ignore the Stack data for different privilege levels and CR3(PDBR)!
	{
		debugger_forceimmediatelogging = 1; //Log!
		MMU_wdw(CPU_SEGMENT_TR, REG_TR, n, TSS->generalpurposeregisters[i++],0); //Write the TSS! Don't be afraid of errors, since we're always accessable!
	}
	i = 0;
	for (n=(((8+10)*4));n<((8+10+6)*4);n+=4) //Write our TSS 16-bit data! Ignore the LDT and I/O map/T-bit, as it's read-only!
	{
		debugger_forceimmediatelogging = 1; //Log!
		MMU_wdw(CPU_SEGMENT_TR, REG_TR, n, TSS->segmentregisters[i++],0); //Write the TSS! Don't be afraid of errors, since we're always accessable!
	}
	debugger_forceimmediatelogging = 0; //Don't log!
}

byte checksaveTSS32(void* segdesc, word value, byte isBacklinked)
{
	word n;
	if (isBacklinked)
	{
		if (EMULATED_CPU>=CPU_PENTIUM) //32-bit write?
		{
			if (checkPhysMMUaccess32(segdesc, value,0,0|0x40,0,0,0)) {debugger_forceimmediatelogging = 0; return 1;} //Error out!
		}
		else //16-bit write?
		{
			if (checkPhysMMUaccess16(segdesc, value,0,0|0x40,0,0,0)) {debugger_forceimmediatelogging = 0; return 1;} //Error out!
		}
	}
	for (n =(8*4);n<((8+10)*4);n+=4) //Write our TSS 32-bit data! Ignore the Stack data for different privilege levels and CR3(PDBR)!
	{
		debugger_forceimmediatelogging = 1; //Log!
		if (checkPhysMMUaccess32(segdesc, value,n+0,0|0x40,0,0,0)) {debugger_forceimmediatelogging = 0; return 1;} //Error out!
	}

	for (n=(((8+10)*4));n<((8+10+6)*4);n+=4) //Write our TSS 16-bit data! Ignore the LDT and I/O map/T-bit, as it's read-only!
	{
		debugger_forceimmediatelogging = 1; //Log!
		if (checkPhysMMUaccess32(segdesc, value,n+0,0|0x40,0,0,0)) {debugger_forceimmediatelogging = 0; return 1;} //Error out!
	}
	debugger_forceimmediatelogging = 0; //Don't log!

	if (isBacklinked)
	{
		if (EMULATED_CPU>=CPU_PENTIUM) //32-bit write?
		{
			if (checkPhysMMUaccess32(segdesc, value,0,0|0xA0,0,0,0)) {debugger_forceimmediatelogging = 0; return 1;} //Error out!
		}
		else //16-bit write?
		{
			if (checkPhysMMUaccess16(segdesc, value,0,0|0xA0,0,0,0)) {debugger_forceimmediatelogging = 0; return 1;} //Error out!
		}
	}
	for (n = (8 * 4); n < ((8 + 10) * 4); n += 4) //Write our TSS 32-bit data! Ignore the Stack data for different privilege levels and CR3(PDBR)!
	{
		debugger_forceimmediatelogging = 1; //Log!
		if (checkPhysMMUaccess32(segdesc, value, n + 0, 0|0xA0, 0, 0, 0)) { debugger_forceimmediatelogging = 0; return 1; } //Error out!
	}

	for (n = (((8 + 10) * 4)); n < ((8 + 10 + 6) * 4); n += 4) //Write our TSS 16-bit data! Ignore the LDT and I/O map/T-bit, as it's read-only!
	{
		debugger_forceimmediatelogging = 1; //Log!
		if (checkPhysMMUaccess32(segdesc, value, n + 0, 0|0xA0, 0, 0, 0)) { debugger_forceimmediatelogging = 0; return 1; } //Error out!
	}
	debugger_forceimmediatelogging = 0; //Don't log!
	return 0; //OK!
}


extern byte enableMMUbuffer; //To buffer the MMU writes?

extern byte advancedlog; //Advanced log setting

extern byte MMU_logging; //Are we logging from the MMU?

byte CPU_switchtask(int whatsegment, SEGMENT_DESCRIPTOR *LOADEDDESCRIPTOR, word *segment, word destinationtask, byte isJMPorCALL, byte gated, int_64 errorcode) //Switching to a certain task?
{
	//Both structures to use for the TSS!
	byte backlinking;
	sbyte affectedregisters[7] = { CPU_SEGMENT_CS,CPU_SEGMENT_SS,CPU_SEGMENT_DS,CPU_SEGMENT_ES,CPU_SEGMENT_FS,CPU_SEGMENT_GS,CPU_SEGMENT_LDTR }; //What registers are affected and to be cleared with a task switch?
	sbyte affectedregisterindex;
	byte affectedregister; //What affected register!

	if ((CPU[activeCPU].taskswitch_stepping&1)==0) //Step 1?
	{
	CPU[activeCPU].taskswitchdata.TSS_dirty = 0; //Is the new TSS dirty?
	CPU[activeCPU].taskswitchdata.TSSSizeSrc = CPU[activeCPU].taskswitchdata.TSSSize = 0; //The (source) TSS size!
	if (errorcode>=0) //Error code to be pushed on the stack(not an interrupt without error code or errorless task switch)?
	{
		CPU[activeCPU].hascallinterrupttaken_type = INTERRUPTGATETIMING_TASKGATE; //INT gate type taken. Low 4 bits are the type. High 2 bits are privilege level/task gate flag. Left at 0xFF when nothing is used(unknown case?)
	}

	if (CPU[activeCPU].hascallinterrupttaken_type==0xFF) //Not set yet?
	{
		if (gated) //Different CPL?
		{
			CPU[activeCPU].hascallinterrupttaken_type = OTHERGATE_NORMALTASKGATE; //INT gate type taken. Low 4 bits are the type. High 2 bits are privilege level/task gate flag. Left at 0xFF when nothing is used(unknown case?)
		}
		else //Same CPL call gate?
		{
			CPU[activeCPU].hascallinterrupttaken_type = OTHERGATE_NORMALTSS; //Normal TSS direct call!
		}
	}

	CPU[activeCPU].allowInterrupts = 1; //Allow interrupts again after this task switch finishes(count as an instruction executing)!

	enableMMUbuffer = 0; //Disable any MMU buffering: we need to update memory directly and properly, in order to work!

	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger", "Switching task to task %04X", destinationtask);
	}

	uint_64 limit; //The limit we use!

	switch (GENERALSEGMENTPTR_TYPE(LOADEDDESCRIPTOR)) //Check the type of descriptor we're switching to!
	{
	case AVL_SYSTEM_BUSY_TSS16BIT:
	case AVL_SYSTEM_TSS16BIT:
		CPU[activeCPU].taskswitchdata.TSSSize = 0; //16-bit TSS!
		break;
	case AVL_SYSTEM_BUSY_TSS32BIT:
	case AVL_SYSTEM_TSS32BIT: //Valid descriptor?
		CPU[activeCPU].taskswitchdata.TSSSize = 1; //32-bit TSS!
		if (EMULATED_CPU < CPU_80386) //Continue normally: we're valid on a 80386 only?
		{
			goto invaliddsttask; //Thow #GP!
		}
		break;
	default: //Invalid descriptor!
	invaliddsttask:
		if (isJMPorCALL == 3) //IRET?
		{
			CPU_TSSFault(destinationtask, ((isJMPorCALL & 0x400) >> 10), (destinationtask & 4) ? EXCEPTION_TABLE_LDT : EXCEPTION_TABLE_GDT); //Throw #TS!
			return 0; //Error out!
		}
		THROWDESCGP(destinationtask, ((isJMPorCALL & 0x400) >> 10), (destinationtask & 4) ? EXCEPTION_TABLE_LDT : EXCEPTION_TABLE_GDT); //Thow #GP!
		return 0; //Error out!
	}

	if (GENERALSEGMENTPTR_P(LOADEDDESCRIPTOR) == 0) //Not present?
	{
		THROWDESCNP(destinationtask, ((isJMPorCALL & 0x400) >> 10), (destinationtask & 4) ? EXCEPTION_TABLE_LDT : EXCEPTION_TABLE_GDT); //Throw #NP!
		return 0; //Error out!
	}

#ifdef FORCE_16BITTSS
	if (EMULATED_CPU == CPU_80286) CPU[activeCPU].taskswitchdata.TSSSize = 0; //Force 16-bit TSS on 286!
#endif

	limit = LOADEDDESCRIPTOR->PRECALCS.limit; //Limit!

	if (limit < (uint_32)(CPU[activeCPU].taskswitchdata.TSSSize ? 43 : 103)) //Limit isn't high enough(>=103 for 386+, >=43 for 80286)?
	{
		CPU_TSSFault(destinationtask, ((isJMPorCALL & 0x400) >> 10), (destinationtask & 4) ? EXCEPTION_TABLE_LDT : EXCEPTION_TABLE_GDT); //Throw #TS!
		return 0; //Error out!
	}

	//Now going to switch to the current task, save the registers etc in the current task!

	if (isJMPorCALL == 3) //IRET?
	{
		if ((LOADEDDESCRIPTOR->desc.AccessRights & 2) == 0) //Destination task is available?
		{
			CPU_TSSFault(destinationtask, ((isJMPorCALL & 0x400) >> 10), (destinationtask & 4) ? EXCEPTION_TABLE_LDT : EXCEPTION_TABLE_GDT); //Throw #GP!
			return 0; //Error out!
		}
	}

	//Check and prepare source task information!
	switch (GENERALSEGMENT_TYPE(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_TR])) //Check the type of descriptor we're switching from!
	{
	case AVL_SYSTEM_BUSY_TSS16BIT:
	case AVL_SYSTEM_TSS16BIT:
		CPU[activeCPU].taskswitchdata.TSSSizeSrc = 0; //16-bit TSS!
		break;
	case AVL_SYSTEM_BUSY_TSS32BIT:
	case AVL_SYSTEM_TSS32BIT: //Valid descriptor?
		CPU[activeCPU].taskswitchdata.TSSSizeSrc = 1; //32-bit TSS!
		if (EMULATED_CPU < CPU_80386) //Continue normally: we're valid on a 80386 only?
		{
			goto invalidsrctask; //Thow #GP!
		}
		break;
	default: //Invalid descriptor!
	invalidsrctask:
		{
			CPU_TSSFault(REG_TR, ((isJMPorCALL & 0x400) >> 10), (REG_TR & 4) ? EXCEPTION_TABLE_LDT : EXCEPTION_TABLE_GDT); //Throw #TS!
			return 0; //Error out!
		}
		THROWDESCGP(REG_TR, ((isJMPorCALL & 0x400) >> 10), (REG_TR & 4) ? EXCEPTION_TABLE_LDT : EXCEPTION_TABLE_GDT); //Thow #GP!
		return 0; //Error out!
	}

	if (CPU[activeCPU].taskswitchdata.TSSSizeSrc) //32-bit TSS?
	{
		memset(&CPU[activeCPU].taskswitchdata.TSS32, 0, sizeof(CPU[activeCPU].taskswitchdata.TSS32)); //Read the TSS! Don't be afraid of errors, since we're always accessable!
	}
	else //16-bit TSS?
	{
		memset(&CPU[activeCPU].taskswitchdata.TSS16, 0, sizeof(CPU[activeCPU].taskswitchdata.TSS16)); //Read the TSS! Don't be afraid of errors, since we're always accessable!
	}
	CPU[activeCPU].taskswitch_stepping |= 1; //Finished step!
	} //End of step 1

	backlinking = ((isJMPorCALL | 0x80) == 0x82); //CALL with backlink?

	if ((CPU[activeCPU].taskswitch_stepping&2)==0) //Step 2?
	{
	if (GENERALSEGMENT_P(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_TR])) //Valid task to switch FROM?
	{
		if ((MMU_logging == 1) && advancedlog) //Are we logging?
		{
			dolog("debugger", "Checking incoming task %04X for transfer", destinationtask);
		}

		//Check the incoming task for valid memory area before doing anything!
		if (CPU[activeCPU].taskswitchdata.TSSSize) //32-bit switching in?
		{
			if (checkloadTSS32((void *)LOADEDDESCRIPTOR,destinationtask)) return 0; //Abort on error!
			if (checksaveTSS32((void *)LOADEDDESCRIPTOR,destinationtask,backlinking)) return 0; //Abort on error!
		}
		else //16-bit switching in?
		{
			if (checkloadTSS16((void *)LOADEDDESCRIPTOR,destinationtask)) return 0; //Abort on error!
			if (checksaveTSS16((void *)LOADEDDESCRIPTOR,destinationtask,backlinking)) return 0; //Abort on error!
		}

		if ((MMU_logging == 1) && advancedlog) //Are we logging?
		{
			dolog("debugger", "Preparing outgoing task %04X for transfer", REG_TR);
		}

		if (CPU[activeCPU].taskswitchdata.TSSSizeSrc) //32-bit switching out?
		{
			if (checksaveTSS32((void *)&CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_TR],REG_TR,0)) return 0; //Abort on error!
		}
		else //16-bit switching out?
		{
			if (checksaveTSS16((void *)&CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_TR],REG_TR,0)) return 0; //Abort on error!
		}

		if (isJMPorCALL == 3) //IRET?
		{
			FLAGW_NT(0); //Clear Nested Task flag of the leaving task!
		}

		//16 or 32-bit TSS is loaded, now save the registers!
		if (CPU[activeCPU].taskswitchdata.TSSSizeSrc) //We're a 32-bit TSS?
		{
			CPU[activeCPU].taskswitchdata.TSS32.EAX = REG_EAX;
			CPU[activeCPU].taskswitchdata.TSS32.ECX = REG_ECX;
			CPU[activeCPU].taskswitchdata.TSS32.EDX = REG_EDX;
			CPU[activeCPU].taskswitchdata.TSS32.EBX = REG_EBX;
			CPU[activeCPU].taskswitchdata.TSS32.ESP = REG_ESP;
			CPU[activeCPU].taskswitchdata.TSS32.EBP = REG_EBP;
			CPU[activeCPU].taskswitchdata.TSS32.ESI = REG_ESI;
			CPU[activeCPU].taskswitchdata.TSS32.EDI = REG_EDI;
			CPU[activeCPU].taskswitchdata.TSS32.CS = REG_CS;
			CPU[activeCPU].taskswitchdata.TSS32.EIP = REG_EIP;
			CPU[activeCPU].taskswitchdata.TSS32.SS = REG_SS;
			CPU[activeCPU].taskswitchdata.TSS32.DS = REG_DS;
			CPU[activeCPU].taskswitchdata.TSS32.ES = REG_ES;
			CPU[activeCPU].taskswitchdata.TSS32.FS = REG_FS;
			CPU[activeCPU].taskswitchdata.TSS32.GS = REG_GS;
			CPU[activeCPU].taskswitchdata.TSS32.EFLAGS = REG_EFLAGS;
		}
		else //We're a 16-bit TSS?
		{
			CPU[activeCPU].taskswitchdata.TSS16.AX = REG_AX;
			CPU[activeCPU].taskswitchdata.TSS16.CX = REG_CX;
			CPU[activeCPU].taskswitchdata.TSS16.DX = REG_DX;
			CPU[activeCPU].taskswitchdata.TSS16.BX = REG_BX;
			CPU[activeCPU].taskswitchdata.TSS16.SP = REG_SP;
			CPU[activeCPU].taskswitchdata.TSS16.BP = REG_BP;
			CPU[activeCPU].taskswitchdata.TSS16.SI = REG_SI;
			CPU[activeCPU].taskswitchdata.TSS16.DI = REG_DI;
			CPU[activeCPU].taskswitchdata.TSS16.CS = REG_CS;
			CPU[activeCPU].taskswitchdata.TSS16.IP = REG_IP;
			CPU[activeCPU].taskswitchdata.TSS16.SS = REG_SS;
			CPU[activeCPU].taskswitchdata.TSS16.DS = REG_DS;
			CPU[activeCPU].taskswitchdata.TSS16.ES = REG_ES;
			CPU[activeCPU].taskswitchdata.TSS16.FLAGS = REG_FLAGS;
		}

		if ((MMU_logging == 1) && advancedlog) //Are we logging?
		{
			dolog("debugger", "Saving outgoing task %04X to memory", REG_TR);
		}

		if (CPU[activeCPU].taskswitchdata.TSSSizeSrc) //32-bit TSS?
		{
			saveTSS32(&CPU[activeCPU].taskswitchdata.TSS32); //Save us!
		}
		else //16-bit TSS?
		{
			saveTSS16(&CPU[activeCPU].taskswitchdata.TSS16); //Save us!
		}
	}
	else //Invalid task to switch FROM?
	{
		goto invalidsrctask; //Invalid source task!
	}
	CPU[activeCPU].taskswitchdata.oldtask = REG_TR; //Save the old task, for backlink purposes!
	CPU[activeCPU].taskswitch_stepping |= 2; //Finished step 2!
	}

	if ((CPU[activeCPU].taskswitch_stepping&4)==0) //Step 3?
	{
	//Now, load all the registers required as needed!
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger", "Switching active TSS to segment selector %04X", destinationtask);
	}
	//Backup of the TR register&descriptor isn't needed, as this is automatically done when loading it!

	if (segmentWritten(CPU_SEGMENT_TR, destinationtask, (isJMPorCALL & 0x400)|((isJMPorCALL == 3)?0x800:0)|(gated?0x80:0))) return 0; //Execute the task switch itself, loading our new descriptor! //Abort on fault: invalid(or busy) task we're switching to! Ignore the privilege level if we're using a gated task switch!
	CPU[activeCPU].taskswitch_stepping |= 4; //Finished step 3!
	}

	if ((CPU[activeCPU].taskswitch_stepping&8)==0) //Step 4?
	{
		if ((isJMPorCALL | 0x80) != 0x82) //Not a call? Stop being busy to switch to another task(or ourselves)!
		{
			SEGMENT_DESCRIPTOR tempdesc;
			if (LOADDESCRIPTOR(CPU_SEGMENT_TR, CPU[activeCPU].taskswitchdata.oldtask, &tempdesc, (isJMPorCALL & 0x400)) == 1) //Loaded old container?
			{
				tempdesc.desc.AccessRights &= ~2; //Mark idle!
				if (SAVEDESCRIPTOR(CPU_SEGMENT_TR, CPU[activeCPU].taskswitchdata.oldtask, &tempdesc, (isJMPorCALL & 0x400)) <= 0) //Save the new status into the old descriptor!
				{
					return 0; //Abort on fault raised!
				}
			}
			else return 0; //Abort on fault raised!
		}
		CPU[activeCPU].taskswitch_stepping |= 8; //Finished step 4!
	}

	if ((CPU[activeCPU].taskswitch_stepping&0x10)==0) //Step 5?
	{
	switch (GENERALSEGMENT_TYPE(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_TR])) //Check the type of descriptor we're executing now!
	{
	case AVL_SYSTEM_BUSY_TSS16BIT:
	case AVL_SYSTEM_TSS16BIT:
		CPU[activeCPU].taskswitchdata.TSSSize = 0; //16-bit TSS!
		break;
	case AVL_SYSTEM_BUSY_TSS32BIT:
	case AVL_SYSTEM_TSS32BIT: //Valid descriptor?
		CPU[activeCPU].taskswitchdata.TSSSize = 1; //32-bit TSS!
		if (EMULATED_CPU < CPU_80386) //Continue normally: we're valid on a 80386 only?
		{
			goto invaliddesttask; //Thow #GP!
		}
		break;
	default: //Invalid descriptor!
	invaliddesttask:
		if (isJMPorCALL == 3) //IRET?
		{
			CPU_TSSFault(destinationtask, ((isJMPorCALL & 0x400) >> 10), (destinationtask & 4) ? EXCEPTION_TABLE_LDT : EXCEPTION_TABLE_GDT); //Throw #TS!
			return 0; //Error out!
		}
		THROWDESCGP(destinationtask, ((isJMPorCALL & 0x400) >> 10), (destinationtask & 4) ? EXCEPTION_TABLE_LDT : EXCEPTION_TABLE_GDT); //Thow #GP!
		return 0; //Error out!
	}

#ifdef FORCE_16BITTSS
	if (EMULATED_CPU == CPU_80286) CPU[activeCPU].taskswitchdata.TSSSize = 0; //Force 16-bit TSS on 286!
#endif

	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger", "Loading incoming TSS %04X state", REG_TR);
	}


	//Load the new TSS!
	if (CPU[activeCPU].taskswitchdata.TSSSize) //32-bit TSS?
	{
		memset(&CPU[activeCPU].taskswitchdata.TSS32, 0, sizeof(CPU[activeCPU].taskswitchdata.TSS32)); //Init!
		loadTSS32(&CPU[activeCPU].taskswitchdata.TSS32); //Load the TSS!
	}
	else //16-bit TSS?
	{
		memset(&CPU[activeCPU].taskswitchdata.TSS16, 0, sizeof(CPU[activeCPU].taskswitchdata.TSS16)); //Init!
		loadTSS16(&CPU[activeCPU].taskswitchdata.TSS16); //Load the TSS!
	}
	CPU[activeCPU].taskswitchdata.TSS_dirty = 0; //Not dirty!

	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger", "Checking for backlink to TSS %04X", CPU[activeCPU].taskswitchdata.oldtask);
	}

	if (backlinking) //CALL with backlink?
	{
		if (CPU[activeCPU].taskswitchdata.TSSSize) //32-bit TSS?
		{
			CPU[activeCPU].taskswitchdata.TSS32.BackLink = CPU[activeCPU].taskswitchdata.oldtask; //Save the old task as a backlink in the new task!
			CPU[activeCPU].taskswitchdata.TSS_dirty |= 1; //We're dirty(backlink)!
		}
		else //16-bit TSS?
		{
			CPU[activeCPU].taskswitchdata.TSS16.BackLink = CPU[activeCPU].taskswitchdata.oldtask; //Save the old task as a backlink in the new task!
			CPU[activeCPU].taskswitchdata.TSS_dirty |= 1; //We're dirty(backlink)!
		}
	}
	CPU[activeCPU].taskswitch_stepping |= 0x10; //Finished step 5!
	}

	if ((CPU[activeCPU].taskswitch_stepping&0x20)==0) //Step 6?
	{
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger", "Marking incoming TSS %04X busy if needed", REG_TR);
	}

	if (isJMPorCALL != 3) //Not an IRET?
	{
		LOADEDDESCRIPTOR->desc.AccessRights |= 2; //Mark not idle!
		if (SAVEDESCRIPTOR(CPU_SEGMENT_TR, REG_TR, LOADEDDESCRIPTOR, (isJMPorCALL & 0x400)) <= 0) //Save the new status into the old descriptor!
		{
			return 0; //Abort on fault raised!
		}
	}
	CPU[activeCPU].taskswitch_stepping |= 0x20; //Finished step 6!
	}

	if ((CPU[activeCPU].taskswitch_stepping&0x40)==0) //Step 7?
	{
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger", "Loading incoming TSS %04X state into the registers.", REG_TR);
	}

	if ((CPU[activeCPU].have_oldSegReg&(1 << CPU_SEGMENT_LDTR)) == 0) //Backup not loaded yet?
	{
		memcpy(&CPU[activeCPU].SEG_DESCRIPTORbackup[CPU_SEGMENT_LDTR], &CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_LDTR], sizeof(CPU[activeCPU].SEG_DESCRIPTORbackup[0])); //Restore the descriptor!
		CPU[activeCPU].oldSegReg[CPU_SEGMENT_LDTR] = *CPU[activeCPU].SEGMENT_REGISTERS[CPU_SEGMENT_LDTR]; //Backup the register too!
		CPU[activeCPU].have_oldSegReg |= (1 << CPU_SEGMENT_LDTR); //Loaded!
	}

	//Now we're ready to load all registers!
	if (CPU[activeCPU].taskswitchdata.TSSSize) //We're a 32-bit TSS?
	{
		REG_EAX = CPU[activeCPU].taskswitchdata.TSS32.EAX;
		REG_ECX = CPU[activeCPU].taskswitchdata.TSS32.ECX;
		REG_EDX = CPU[activeCPU].taskswitchdata.TSS32.EDX;
		REG_EBX = CPU[activeCPU].taskswitchdata.TSS32.EBX;
		REG_ESP = CPU[activeCPU].taskswitchdata.TSS32.ESP;
		REG_EBP = CPU[activeCPU].taskswitchdata.TSS32.EBP;
		REG_ESI = CPU[activeCPU].taskswitchdata.TSS32.ESI;
		REG_EDI = CPU[activeCPU].taskswitchdata.TSS32.EDI;
		CPU[activeCPU].registers->CR3 = CPU[activeCPU].taskswitchdata.TSS32.CR3; //Load the new CR3 register to use the new Paging table!
		REG_EFLAGS = CPU[activeCPU].taskswitchdata.TSS32.EFLAGS;
		//Load all remaining registers manually for exceptions!
		REG_CS = CPU[activeCPU].taskswitchdata.TSS32.CS;
		REG_DS = CPU[activeCPU].taskswitchdata.TSS32.DS;
		REG_ES = CPU[activeCPU].taskswitchdata.TSS32.ES;
		REG_FS = CPU[activeCPU].taskswitchdata.TSS32.FS;
		REG_GS = CPU[activeCPU].taskswitchdata.TSS32.GS;
		REG_EIP = CPU[activeCPU].taskswitchdata.TSS32.EIP;
		REG_SS = CPU[activeCPU].taskswitchdata.TSS32.SS; //Default stack to use: the old stack!
		REG_LDTR = CPU[activeCPU].taskswitchdata.TSS32.LDT;
		CPU[activeCPU].taskswitchdata.LDTsegment = CPU[activeCPU].taskswitchdata.TSS32.LDT; //LDT used!
		Paging_clearTLB(); //Clear the TLB: CR3 has been changed!
	}
	else //We're a 16-bit TSS?
	{
		REG_EAX = CPU[activeCPU].taskswitchdata.TSS16.AX;
		REG_ECX = CPU[activeCPU].taskswitchdata.TSS16.CX;
		REG_EDX = CPU[activeCPU].taskswitchdata.TSS16.DX;
		REG_EBX = CPU[activeCPU].taskswitchdata.TSS16.BX;
		REG_ESP = CPU[activeCPU].taskswitchdata.TSS16.SP;
		REG_EBP = CPU[activeCPU].taskswitchdata.TSS16.BP;
		REG_ESI = CPU[activeCPU].taskswitchdata.TSS16.SI;
		REG_EDI = CPU[activeCPU].taskswitchdata.TSS16.DI;
		REG_EFLAGS = (uint_32)CPU[activeCPU].taskswitchdata.TSS16.FLAGS;
		//Load all remaining registers manually for exceptions!
		REG_CS = CPU[activeCPU].taskswitchdata.TSS16.CS; //This should also load the privilege level!
		REG_DS = CPU[activeCPU].taskswitchdata.TSS16.DS;
		REG_ES = CPU[activeCPU].taskswitchdata.TSS16.ES;
		REG_EIP = (uint_32)CPU[activeCPU].taskswitchdata.TSS16.IP;
		REG_SS = CPU[activeCPU].taskswitchdata.TSS16.SS; //Default stack to use: the old stack!
		REG_LDTR = CPU[activeCPU].taskswitchdata.TSS16.LDT;
		CPU[activeCPU].taskswitchdata.LDTsegment = CPU[activeCPU].taskswitchdata.TSS16.LDT; //LDT used!
	}

	if (backlinking) //CALL with backlink?
	{
		FLAGW_NT(1); //Set Nested Task flag of the new task!
		if (CPU[activeCPU].taskswitchdata.TSSSize) //32-bit TSS?
		{
			CPU[activeCPU].taskswitchdata.TSS32.EFLAGS = REG_EFLAGS; //Save the new flag!
		}
		else //16-bit TSS?
		{
			CPU[activeCPU].taskswitchdata.TSS16.FLAGS = REG_FLAGS; //Save the new flag!
		}
		CPU[activeCPU].taskswitchdata.TSS_dirty |= 2; //We're dirty((E)FLAGS)!
	}

	if (CPU[activeCPU].taskswitchdata.TSS_dirty) //Destination TSS dirty?
	{
		if ((MMU_logging == 1) && advancedlog) //Are we logging?
		{
			dolog("debugger", "Saving incoming TSS %04X state to memory, because the state has changed(Nested Task).", REG_TR);
		}

		if (CPU[activeCPU].taskswitchdata.TSS_dirty & 1)
		{
			if ((EMULATED_CPU>=CPU_PENTIUM) && CPU[activeCPU].taskswitchdata.TSSSize) //Pentium uses a 32-bit write?
			{
				MMU_wdw(CPU_SEGMENT_TR, REG_TR, 0, CPU[activeCPU].taskswitchdata.TSSSize ? CPU[activeCPU].taskswitchdata.TSS32.BackLink : CPU[activeCPU].taskswitchdata.TSS16.BackLink, 0); //Write the TSS Backlink to use! Don't be afraid of errors, since we're always accessable!
			}
			else
			{
				MMU_ww(CPU_SEGMENT_TR, REG_TR, 0, CPU[activeCPU].taskswitchdata.TSSSize ? CPU[activeCPU].taskswitchdata.TSS32.BackLink : CPU[activeCPU].taskswitchdata.TSS16.BackLink, 0); //Write the TSS Backlink to use! Don't be afraid of errors, since we're always accessable!
			}
		}

		if (CPU[activeCPU].taskswitchdata.TSS_dirty & 2) //Dirty (E)FLAGS?
		{
			if (CPU[activeCPU].taskswitchdata.TSSSize) //32-bit TSS?
			{
				saveTSS32(&CPU[activeCPU].taskswitchdata.TSS32); //Save the TSS!
			}
			else //16-bit TSS?
			{
				saveTSS16(&CPU[activeCPU].taskswitchdata.TSS16); //Save the TSS!
			}
		}
	}
	CPU[activeCPU].taskswitch_stepping |= 0x40; //Finished step 7!
	}

	if ((CPU[activeCPU].taskswitch_stepping&0x80)==0) //Step 8?
	{
	//At this point, the basic task switch is complete. All that remains is loading all segment descriptors as required!

	CPU[activeCPU].have_oldSegReg &= ~(1<<CPU_SEGMENT_TR); //Not supporting returning to the old task anymore, we've completed the task switch, committing to the new task!
	for (affectedregister = 0; affectedregister < NUMITEMS(affectedregisters); ++affectedregister) //Clear all effective register data!
	{
		affectedregisterindex = affectedregisters[affectedregister]; //What index to use?
		memset(&CPU[activeCPU].SEG_DESCRIPTOR[affectedregisterindex],0,sizeof(CPU[activeCPU].SEG_DESCRIPTOR[affectedregisterindex])); //Clear said descriptors!
		CPU[activeCPU].SEG_DESCRIPTOR[affectedregisterindex].PRECALCS.notpresent = 1; //Not present!
	}
	//Set the default CPL!
	CPU[activeCPU].CPL = (getcpumode() == CPU_MODE_8086) ? 3 : ((getcpumode() == CPU_MODE_REAL) ? 0 : getRPL(CPU[activeCPU].taskswitchdata.TSSSize ? CPU[activeCPU].taskswitchdata.TSS32.CS : CPU[activeCPU].taskswitchdata.TSS16.CS)); //Load default CPL, according to the mode!
	CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_SS].desc.AccessRights = (CPU[activeCPU].CPL << 5); //Make sure that SS has the correct DPL(and Access Rights) for the selected task when erroring out!
	CPU_commitState(); //Set the new fault as a return point when faulting!
	//Clear all the descriptor caches to invalid!
	CPU[activeCPU].exec_CS = REG_CS; //Save for error handling!
	CPU[activeCPU].exec_EIP = (REG_EIP&CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS].PRECALCS.roof); //Save for error handling!
	CPU[activeCPU].InterruptReturnEIP = REG_EIP; //Make sure that interrupts behave with a correct EIP after faulting on REP prefixed instructions!
	//No last: we're entering a task that has this information, so no return point is given!
	CPU[activeCPU].exec_lastCS = CPU[activeCPU].exec_CS;
	CPU[activeCPU].exec_lastEIP = CPU[activeCPU].exec_EIP;

	//Update the x86 debugger, if needed!
	protectedModeDebugger_taskswitching(); //Apply any action required for a task switch!

	updateCPUmode(); //Make sure the CPU mode is updated, according to the task!
	CPU[activeCPU].taskswitch_stepping |= 0x80; //Finished step 8!
	}

	if ((CPU[activeCPU].taskswitch_stepping&0x100)==0) //Step 9?
	{
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger", "Loading incoming TSS LDT %04X", CPU[activeCPU].taskswitchdata.LDTsegment);
	}

	//Check and verify the LDT descriptor!
	SEGMENT_DESCRIPTOR LDTsegdesc;
	word LDTsegment; //For ease of use below
	LDTsegment = CPU[activeCPU].taskswitchdata.LDTsegment; //Load the used segment!
	uint_32 descriptor_index = (LDTsegment&~0x7); //The full index within the descriptor table!

	if (!(descriptor_index&~3)) //NULL segment loaded into LDTR? Special case: no LDT available!
	{
		memset(&LDTsegdesc, 0, sizeof(LDTsegdesc)); //No descriptor available to use: mark as invalid!
	}
	else //Valid LDT index?
	{
		if (LDTsegment & 4) //We cannot reside in the LDT!
		{
			CPU_TSSFault(REG_TR, ((isJMPorCALL & 0x400) >> 10), (REG_TR & 4) ? EXCEPTION_TABLE_LDT : EXCEPTION_TABLE_GDT); //Throw error!
			return 0; //Not present: we cannot reside in the LDT!
		}

		if ((word)(descriptor_index | 0x7) > CPU[activeCPU].registers->GDTR.limit) //GDT limit exceeded?
		{
			CPU_TSSFault(REG_TR, ((isJMPorCALL & 0x400) >> 10), (REG_TR & 4) ? EXCEPTION_TABLE_LDT : EXCEPTION_TABLE_GDT); //Throw error!
			return 0; //Not present: limit exceeded!
		}

		CPU[activeCPU].taskswitchdata.loadresult = LOADDESCRIPTOR(CPU_SEGMENT_LDTR, LDTsegment, &LDTsegdesc, 0x200 | (isJMPorCALL & 0x400)); //Load it, ignore errors?
		if (unlikely(CPU[activeCPU].taskswitchdata.loadresult <= 0)) //Invalid LDT(due to being unpaged or other loading fault)?
		{
			if (CPU[activeCPU].taskswitchdata.loadresult==0) //Yet to throw the fault?
			{
				CPU_TSSFault(REG_TR, ((isJMPorCALL & 0x400) >> 10), (REG_TR & 4) ? EXCEPTION_TABLE_LDT : EXCEPTION_TABLE_GDT); //Throw error!
			}
			return 0; //Invalid LDT(due to being unpaged or other loading fault)?
		}

		//Now the LDT entry is loaded for testing!
		if (GENERALSEGMENT_S(LDTsegdesc)) //Not an LDT?
		{
			CPU_TSSFault(REG_TR, ((isJMPorCALL & 0x400) >> 10), (REG_TR & 4) ? EXCEPTION_TABLE_LDT : EXCEPTION_TABLE_GDT); //Throw error!
			return 0; //Not present: not an IDT!	
		}
		if (GENERALSEGMENT_TYPE(LDTsegdesc) != AVL_SYSTEM_LDT) //Not an LDT?
		{
			CPU_TSSFault(REG_TR, ((isJMPorCALL & 0x400) >> 10), (REG_TR & 4) ? EXCEPTION_TABLE_LDT : EXCEPTION_TABLE_GDT); //Throw error!
			return 0; //Not present: not an IDT!	
		}

		if (!GENERALSEGMENT_P(LDTsegdesc)) //Not present?
		{
			CPU_TSSFault(REG_TR, ((isJMPorCALL & 0x400) >> 10), (REG_TR & 4) ? EXCEPTION_TABLE_LDT : EXCEPTION_TABLE_GDT); //Throw error!
			return 0; //Not present: not an IDT!	
		}
	}

	memcpy(&CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_LDTR], &LDTsegdesc, sizeof(CPU[activeCPU].SEG_DESCRIPTOR[0])); //Make the LDTR active by loading it into the descriptor cache!
	CPU_commitState(); //Set the new fault as a return point when faulting!
	CPU[activeCPU].taskswitch_stepping |= 0x100; //Finished step 9!
	}

	if ((CPU[activeCPU].taskswitch_stepping&0x200)==0) //Step 10?
	{
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger", "Setting Task Switched flag in CR0");
	}

	CPU[activeCPU].registers->CR0 |= CR0_TS; //Set the high bit of the TS bit(bit 3)!

	//Now, load all normal registers in order, keeping aborts possible!
	CPU[activeCPU].faultraised = 0; //Clear the fault level: the new task has no faults by default!
	CPU[activeCPU].taskswitch_stepping |= 0x200; //Finished step 10!
	}

	if ((CPU[activeCPU].taskswitch_stepping&0x400)==0) //Step 11?
	{
	//First, load CS!
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger", "Loading incoming TSS CS register");
	}
	CPU[activeCPU].destEIP = REG_EIP; //Save EIP for the new address, we don't want to lose it when loading!
	if (CPU[activeCPU].taskswitchdata.TSSSize) //32-bit?
	{
		if (segmentWritten(CPU_SEGMENT_CS, CPU[activeCPU].taskswitchdata.TSS32.CS, 0x200 | (isJMPorCALL & 0x400))) return 0; //Load CS!
	}
	else
	{
		if (segmentWritten(CPU_SEGMENT_CS, CPU[activeCPU].taskswitchdata.TSS16.CS, 0x200 | (isJMPorCALL & 0x400))) return 0; //Load CS!
	}
	CPU_commitState(); //Set the new fault as a return point when faulting!
	//RPL(CS)!=DPL(CS descriptor) Doesn't make sense with conforming segments, nor with V86 segments! Also, already handled by segmentWritten.
	CPU[activeCPU].taskswitch_stepping |= 0x400; //Finished step 11!
	}

	if ((CPU[activeCPU].taskswitch_stepping&0x800)==0) //Step 12?
	{
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger", "Loading incoming TSS Stack address");
	}

	if (CPU[activeCPU].taskswitchdata.TSSSize) //32-bit?
	{
		if (segmentWritten(CPU_SEGMENT_SS, CPU[activeCPU].taskswitchdata.TSS32.SS, 0x200 | (isJMPorCALL & 0x400))) return 0; //Update the segment! Privilege must match CPL(bit 7 of isJMPorCALL==0)!
		REG_ESP = CPU[activeCPU].taskswitchdata.TSS32.ESP;
	}
	else //16-bit?
	{
		if (segmentWritten(CPU_SEGMENT_SS, CPU[activeCPU].taskswitchdata.TSS16.SS, 0x200 | (isJMPorCALL & 0x400))) return 0; //Update the segment! Privilege must match CPL(bit 7 of isJMPorCALL==0)!
		REG_SP = CPU[activeCPU].taskswitchdata.TSS16.SP;
	}

	//Set the default CPL!
	CPU[activeCPU].CPL = (getcpumode()==CPU_MODE_8086)?3:((getcpumode()==CPU_MODE_REAL)?0:GENERALSEGMENT_DPL(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_SS])); //Load default CPL to use from SS if needed!
	CPU_commitState(); //Set the new fault as a return point when faulting!
	CPU[activeCPU].taskswitch_stepping |= 0x800; //Finished step 12!
	}

	if ((CPU[activeCPU].taskswitch_stepping&0x1000)==0) //Step 13?
	{
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger","Loading DS register");
	}
	if (CPU[activeCPU].taskswitchdata.TSSSize) //32-bit?
	{
		if (segmentWritten(CPU_SEGMENT_DS, CPU[activeCPU].taskswitchdata.TSS32.DS, 0x280|(isJMPorCALL&0x400))) return 0; //Load reg!
	}
	else //16-bit?
	{
		if (segmentWritten(CPU_SEGMENT_DS, CPU[activeCPU].taskswitchdata.TSS16.DS, 0x280|(isJMPorCALL&0x400))) return 0; //Load reg!
	}
	CPU_commitState(); //Set the new fault as a return point when faulting!
	CPU[activeCPU].taskswitch_stepping |= 0x1000; //Finished step 13!
	}

	if ((CPU[activeCPU].taskswitch_stepping&0x2000)==0) //Step 14?
	{
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger","Loading ES register");
	}
	if (CPU[activeCPU].taskswitchdata.TSSSize) //32-bit?
	{
		if (segmentWritten(CPU_SEGMENT_ES, CPU[activeCPU].taskswitchdata.TSS32.ES, 0x280|(isJMPorCALL&0x400))) return 0; //Load reg!
	}
	else //16-bit?
	{
		if (segmentWritten(CPU_SEGMENT_ES, CPU[activeCPU].taskswitchdata.TSS16.ES, 0x280|(isJMPorCALL&0x400))) return 0; //Load reg!
	}
	CPU_commitState(); //Set the new fault as a return point when faulting!
	CPU[activeCPU].taskswitch_stepping |= 0x2000; //Finished step 14!
	}

	if ((CPU[activeCPU].taskswitch_stepping&0x4000)==0) //Step 15?
	{
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger","Loading FS register");
	}
	if (CPU[activeCPU].taskswitchdata.TSSSize) //32-bit?
	{
		if (segmentWritten(CPU_SEGMENT_FS, CPU[activeCPU].taskswitchdata.TSS32.FS, 0x280|(isJMPorCALL&0x400))) return 0; //Load reg!
	}
	else //16-bit?
	{
		if (segmentWritten(CPU_SEGMENT_FS, 0, 0x280|(isJMPorCALL&0x400))) return 0; //Load reg: FS is unusable!
	}
	CPU_commitState(); //Set the new fault as a return point when faulting!
	CPU[activeCPU].taskswitch_stepping |= 0x4000; //Finished step 15!
	}

	if ((CPU[activeCPU].taskswitch_stepping&0x8000)==0) //Step 16?
	{
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger","Loading GS register");
	}
	if (CPU[activeCPU].taskswitchdata.TSSSize) //32-bit?
	{
		if (segmentWritten(CPU_SEGMENT_GS, CPU[activeCPU].taskswitchdata.TSS32.GS, 0x280|(isJMPorCALL&0x400))) return 0; //Load reg!
	}
	else //16-bit?
	{
		if (segmentWritten(CPU_SEGMENT_GS, 0, 0x280|(isJMPorCALL&0x400))) return 0; //Load reg: GS is unusable!
	}
	CPU_commitState(); //Set the new fault as a return point when faulting!
	CPU[activeCPU].taskswitch_stepping |= 0x8000; //Finished step 16!
	}

	//All segments are valid and readable!

	if ((CPU[activeCPU].taskswitch_stepping&0x10000)==0) //Step 17?
	{
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger","New task ready for execution.");
	}

	uint_32 errorcode32 = (uint_32)errorcode; //Get the error code itelf!
	word errorcode16 = (word)errorcode; //16-bit variant, if needed!

	if (errorcode>=0) //Error code to be pushed on the stack(not an interrupt without error code or errorless task switch)?
	{
		if (checkStackAccess(1,1,CPU[activeCPU].taskswitchdata.TSSSize)) return 0; //Abort on fault!
		if (CPU[activeCPU].taskswitchdata.TSSSize) //32-bit task?
		{
			CPU_PUSH32(&errorcode32); //Push the error on the stack!
		}
		else
		{
			CPU_PUSH16(&errorcode16,0); //Push the error on the stack!
		}
		CPU[activeCPU].hascallinterrupttaken_type = INTERRUPTGATETIMING_TASKGATE; //INT gate type taken. Low 4 bits are the type. High 2 bits are privilege level/task gate flag. Left at 0xFF when nothing is used(unknown case?)
	}

	CPU[activeCPU].faultlevel = 0; //Clear the fault level: the new task has no faults by default!

	CPU_commitState(); //Set the new fault as a return point when faulting!
	CPU[activeCPU].taskswitch_stepping |= 0x10000; //Finished step 17!
	}

	if ((CPU[activeCPU].taskswitch_stepping&0x20000)==0) //Step 18?
	{
	if (CPU[activeCPU].taskswitchdata.TSSSize) //32-bit TSS?
	{
		if ((CPU[activeCPU].taskswitchdata.TSS32.T & 1) && (FLAG_RF == 0)) //Trace bit set? Cause a debug exception when this context is run?
		{
			if (protectedModeDebugger_taskswitched()) return 0; //Finished task switch with debugger interrupt handling!
		}
	}
	CPU[activeCPU].taskswitch_stepping |= 0x20000; //Finished step 18!
	}

	CPU[activeCPU].unaffectedRF = 3; //Default: affected, but don't trigger an exception right away for the current instruction(changed state)!
	CPU[activeCPU].executed = 1; //Task switch completed!
	CPU_flushPIQ(-1); //We're jumping to another address!
	return 0; //Abort any running instruction operation, finish up!
}

void CPU_TSSFault(word segmentval, byte is_external, byte tbl)
{
	byte EXT;
	uint_32 errorcode;
	errorcode = (segmentval&0xFFF8)|(is_external&1)|((tbl&3)<<1);
	EXT = CPU[activeCPU].faultraised_external; //External type to use!
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger","#TS fault(%08X)!",errorcode|EXT);
	}
	if (CPU_faultraised(EXCEPTION_INVALIDTSSSEGMENT)) //We're raising a fault!
	{
		CPU_resetOP(); //Point to the faulting instruction!
		CPU_onResettingFault(0); //Apply reset to fault!
		CPU_executionphase_startinterrupt(EXCEPTION_INVALIDTSSSEGMENT,2|8,errorcode|EXT); //Call IVT entry #13 decimal!
	}
}
