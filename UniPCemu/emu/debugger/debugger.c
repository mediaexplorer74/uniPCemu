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

#include "headers/types.h"
#include "headers/cpu/cpu.h" //We're debugging the CPU?
#include "headers/cpu/mmu.h" //MMU support for opcode!
#include "headers/emu/input.h" //Input support!
#include "headers/cpu/interrupts.h" //Interrupt support!
#include "headers/emu/debugger/debugger.h" //Constant support!
#include "headers/bios/bios.h" //BIOS support!
#include "headers/emu/gpu/gpu.h" //GPU support!
#include "headers/support/log.h" //Log support!
#include "headers/emu/gpu/gpu.h" //GPU resolution support!
#include "headers/emu/gpu/gpu_renderer.h" //GPU renderer support!
#include "headers/emu/gpu/gpu_text.h" //Text support!
#include "headers/emu/emucore.h" //for pause/resumeEMU support!
#include "headers/fopen64.h" //64-bit fopen support!
#include "headers/support/locks.h" //Locking support!
#include "headers/emu/threads.h" //Thread support!
#include "headers/hardware/pic.h" //Interrupt support!
#include "headers/hardware/vga/vga_renderer.h" //Renderer support!
#include "headers/bios/biosmenu.h" //Support for running the BIOS from the debugger!
#include "headers/mmu/mmuhandler.h" //MMU_invaddr and MMU_dumpmemory support!
#include "headers/cpu/biu.h" //BIU support!
#include "headers/cpu/easyregs.h" //Easy register support!
#include "headers/mmu/mmuhandler.h" //Memory direct read support!
#include "headers/emu/gpu/gpu_emu.h" //GPU printing support for the BIOS screen printing functions.
#include "headers/emu/emu_misc.h" //converthex2int support!
#include "headers/cpu/paging.h" //Virtual memory support for the virtual memory viewer!

//Log flags only?
//#define LOGFLAGSONLY

//Debug logging for protected mode?
//#define DEBUG_PROTECTEDMODE

byte debugger_loggingtimestamp = 1; //Are we to log timestamps?

byte log_timestampbackup; //Backup of the original timestamp value!

//Debugger skipping functionality
uint_32 skipopcodes = 0; //Skip none!
byte skipstep = 0; //Skip while stepping? 1=repeating, 2=EIP destination, 3=Stop asap.
word skipopcodes_destCS = 0; //Wait for CS to become this value?
uint_32 skipopcodes_destEIP = 0; //Wait for EIP to become this value?

//Repeat log?
byte forcerepeat = 0; //Force repeat log?

byte allow_debuggerstep = 0; //Disabled by default: needs to be enabled by our BIOS!

char debugger_prefix[256] = ""; //The prefix!
char debugger_command_text[256] = ""; //Current command!
byte debugger_set = 0; //Debugger set?
byte debugger_instructionexecuting = 0; //Instruction not yet executing?
uint_32 debugger_index = 0; //Current debugger index!

byte debugger_logtimings = 1; //Are we to log the full timings of hardware and CPU as well?

extern byte dosoftreset; //To soft-reset?
extern BIOS_Settings_TYPE BIOS_Settings; //The BIOS for CPU info!

byte singlestep = 0; //Enforce single step by CPU/hardware special debugging effects? 0=Don't step, 1=Step this instruction(invalid state when activated during the execution of the instruction), 2+ step next instruction etc.
byte BPsinglestep = 0; //Breakpoint-enforced single-step triggered?

byte lastHLTstatus = 0; //Last halt status for debugger! 1=Was halting, 0=Not halting!

CPU_registers debuggerregisters; //Backup of the CPU's register states before the CPU starts changing them!
SEGMENT_DESCRIPTOR debuggersegmentregistercache[8]; //All segment descriptors
byte debuggerHLT = 0;
byte debuggerCPL = 0; //CPL of the executing process that's debugged!
byte debuggerReset = 0; //Are we a reset CPU?

extern uint_32 MMU_lastwaddr; //What address is last addresses in actual memory?
extern byte MMU_lastwdata;

extern PIC i8259;

extern GPU_type GPU; //GPU itself!

byte debugger_simplifiedlog = 0; //Are we to produce a simplified log?

byte verifyfile = 0; //Check for verification file?

byte debugger_is_logging = 0; //Are we logging?

byte advancedlog = 0; //Advanced log setting!

#include "headers/packed.h" //Packed!
typedef struct PACKED
{
	word CS, SS, DS, ES; //16-bit segment registers!
	word AX, BX, CX, DX; //16-bit GP registers!
	word SI, DI, SP, BP;
	word IP;
	word FLAGS;
	word type; //Special type indication!
} VERIFICATIONDATA;
#include "headers/endpacked.h" //End packed!

OPTINLINE byte readverification(uint_32 index, VERIFICATIONDATA *entry)
{
	const word size = sizeof(*entry);
	BIGFILE *f;
	f = emufopen64("debuggerverify16.dat", "rb"); //Open verify data!
	if (emufseek64(f, index*size, SEEK_SET) == 0) //OK?
	{
		if (emufread64(entry, 1, size, f) == size) //OK?
		{
			emufclose64(f); //Close the file!
			return 1; //Read!
		}
	}
	emufclose64(f); //Close the file!
	return 0; //Error reading the entry!
}

extern byte HWINT_nr, HWINT_saved; //HW interrupt saved?

byte startreached = 0;
byte harddebugging = 0; //Hard-coded debugger set?

OPTINLINE byte debugging() //Debugging?
{
	byte result=0;
	if ((singlestep==1) || (BPsinglestep)) //EMU enforced single step?
	{
		return 1; //We're enabled now!
	}
	if (!(DEBUGGER_ENABLED && allow_debuggerstep))
	{
		return 0; //No debugger enabled!
	}
	else if (DEBUGGER_ALWAYS_DEBUG)
	{
		return 1; //Always debug!
	}
	else
	{
		if (likely(BIOS_Settings.debugmode==DEBUGMODE_NOSHOW_RUN)) return 0; //Disabled when executing the noshow run!
		lock(LOCK_INPUT);
		if ((psp_keypressed(BUTTON_RTRIGGER) || (DEBUGGER_ALWAYS_STEP > 0))) //Forced step?
		{
			unlock(LOCK_INPUT);
			return 1; //Always step!
		}
		goto skiprelock; //Don't lock again!
	}
	lock(LOCK_INPUT);
	skiprelock:
	result = psp_keypressed(BUTTON_LTRIGGER); //Debugging according to LTRIGGER!!!
	unlock(LOCK_INPUT);
	return result; //Give the result!
}

byte debuggerINT = 0; //Interrupt special trigger?

byte debugger_forceEIP()
{
	return ((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT)); //Force EIP to be used?
}

byte debugger_logging()
{
	byte enablelog=0; //Default: disabled!
	switch (DEBUGGER_LOG) //What log method?
	{
	case DEBUGGERLOG_ALWAYS:
	case DEBUGGERLOG_ALWAYS_NOREGISTERS: //Same, but no register state logging?
	case DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP: //Always, but also when skipping?
	case DEBUGGERLOG_ALWAYS_SINGLELINE: //Always log, even during skipping, single line format
	case DEBUGGERLOG_ALWAYS_SINGLELINE_SIMPLIFIED: //Always log, even during skipping, single line format, simplfied
	case DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT: //Always log, common log format
	case DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT: //Always log, even during skip step, common log format
		enablelog = 1; //Always enabled!
		break;
	case DEBUGGERLOG_DEBUGGING:
	case DEBUGGERLOG_DEBUGGING_SINGLELINE:
	case DEBUGGERLOG_DEBUGGING_SINGLELINE_SIMPLIFIED:
	case DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT:
		enablelog = debugging(); //Enable log when debugging!
		break;
	case DEBUGGERLOG_INT: //Interrupts only?
		enablelog = debuggerINT; //Debug this(interrupt)!
		break;
	case DEBUGGERLOG_DIAGNOSTICCODES: //Diagnostic codes only
		enablelog = debugging(); //Enable log when debugging only!
		break; //Don't enable the log by debugging only!
	default:
		break;
	}
	enablelog |= startreached; //Start logging from this point(emulator internal debugger)!
	enablelog |= harddebugging; //Same as startreached, but special operations only!
	enablelog |= CPU[activeCPU].waitingforiret; //Waiting for IRET?
	enablelog &= allow_debuggerstep; //Are we allowed to debug?
	enablelog &= ((skipstep && ((DEBUGGER_LOG != DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP) && (DEBUGGER_LOG != DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT))) ^ 1)&1; //Disable when skipping?
	return enablelog; //Logging?
}

byte isDebuggingPOSTCodes()
{
	return (DEBUGGER_LOG==DEBUGGERLOG_DIAGNOSTICCODES); //Log Diagnostic codes only?
}

byte needdebugger() //Do we need to generate debugging information? Only called once each instruction!
{
	byte result;
	debugger_is_logging = result = debugger_logging(); //Are we logging?
	result |= (DEBUGGER_LOG == DEBUGGERLOG_INT); //Interrupts are needed, but logging is another story!
	result |= debugging(); //Are we debugging?
	return result; //Do we need debugger information?
}

OPTINLINE char stringsafeDebugger(byte x)
{
	return (x && (x!=0xD) && (x!=0xA))?x:(char)0x20;
}

char debugger_memoryaccess_text[0x40000]; //Memory access text!
char debugger_memoryaccess_line[256];

void safestrcat_text(char *line)
{
	safestrcat(debugger_memoryaccess_text,sizeof(debugger_memoryaccess_text),line); //Add the line!
}

byte debugger_forceimmediatelogging = 0; //Force immediate logging?

void debugger_logmemoryaccess(byte iswrite, uint_64 address, byte value, byte type)
{
	if (activeCPU) return; //Not CPU #0?
	if (advancedlog == 0) //Not logging advanced?
	{
		return; //Disable memory logs entirely!
	}
	if (iswrite)
	{
		switch (type&7)
		{
			case LOGMEMORYACCESS_NORMAL:
				if (((DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE_SIMPLIFIED) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE_SIMPLIFIED) && !((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT))) || debugger_forceimmediatelogging) //Not using a single line?
				{
					log_timestampbackup = log_logtimestamp(2); //Save state!
					log_logtimestamp(debugger_loggingtimestamp); //Are we to log the timestamp?
					dolog("debugger","Writing to normal memory(w): %08x=%02x (%c)",address,value,stringsafeDebugger(value));
					log_logtimestamp(log_timestampbackup); //Restore state!
				}
				else
				{
					if (strcmp(debugger_memoryaccess_text,"")==0) //Nothing logged yet?
					{
						snprintf(debugger_memoryaccess_text,sizeof(debugger_memoryaccess_text),"Normal(w):%08" SPRINTF_x_UINT32 "=%02x(%c)",(uint_32)address,value,stringsafeDebugger(value)); //Compact version!
					}
					else
					{
						snprintf(debugger_memoryaccess_line,sizeof(debugger_memoryaccess_line),"Normal(w):%08" SPRINTF_x_UINT32 "=%02x(%c)",(uint_32)address,value,stringsafeDebugger(value)); //Compact version!
						safestrcat_text("; ");
						safestrcat_text(debugger_memoryaccess_line); //Add the line!
					}
				}
				break;
			case LOGMEMORYACCESS_PAGED:
				if (((DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE_SIMPLIFIED) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE_SIMPLIFIED) && !((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT))) || debugger_forceimmediatelogging) //Not using a single line?
				{
					log_timestampbackup = log_logtimestamp(2); //Save state!
					log_logtimestamp(debugger_loggingtimestamp); //Are we to log the timestamp?
					dolog("debugger","Writing to paged memory(w): %08x=%02x (%c)",(uint_32)address,value,stringsafeDebugger(value));
					log_logtimestamp(log_timestampbackup); //Restore state!
				}
				else
				{
					if (strcmp(debugger_memoryaccess_text,"")==0) //Nothing logged yet?
					{
						snprintf(debugger_memoryaccess_text,sizeof(debugger_memoryaccess_text),"Paged(w):%08" SPRINTF_x_UINT32 "=%02x(%c)",(uint_32)address,value,stringsafeDebugger(value)); //Compact version!
					}
					else
					{
						snprintf(debugger_memoryaccess_line,sizeof(debugger_memoryaccess_line),"Paged(w):%08" SPRINTF_x_UINT32 "=%02x(%c)",(uint_32)address,value,stringsafeDebugger(value)); //Compact version!
						safestrcat_text("; ");
						safestrcat_text(debugger_memoryaccess_line); //Add the line!
					}
				}
				break;
			case LOGMEMORYACCESS_DIRECT:
				if (((DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE_SIMPLIFIED) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE_SIMPLIFIED) && !((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT))) || debugger_forceimmediatelogging) //Not using a single line?
				{
					log_timestampbackup = log_logtimestamp(2); //Save state!
					log_logtimestamp(debugger_loggingtimestamp); //Are we to log the timestamp?
					dolog("debugger","Writing to physical memory(w): %08x=%02x (%c)",(uint_32)address,value,stringsafeDebugger(value));
					log_logtimestamp(log_timestampbackup); //Restore state!
				}
				else
				{
					if (strcmp(debugger_memoryaccess_text,"")==0) //Nothing logged yet?
					{
						snprintf(debugger_memoryaccess_text,sizeof(debugger_memoryaccess_text),"Physical(w):%08" SPRINTF_x_UINT32 "=%02x(%c)",(uint_32)address,value,stringsafeDebugger(value)); //Compact version!
					}
					else
					{
						snprintf(debugger_memoryaccess_line,sizeof(debugger_memoryaccess_line),"Physical(w):%08" SPRINTF_x_UINT32 "=%02x(%c)", (uint_32)address,value,stringsafeDebugger(value)); //Compact version!
						safestrcat_text("; ");
						safestrcat_text(debugger_memoryaccess_line); //Add the line!
					}
				}
				break;
			default:
			case LOGMEMORYACCESS_RAM:
				if (((DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE_SIMPLIFIED) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE_SIMPLIFIED) && !((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT))) || debugger_forceimmediatelogging) //Not using a single line?
				{
					log_timestampbackup = log_logtimestamp(2); //Save state!
					log_logtimestamp(debugger_loggingtimestamp); //Are we to log the timestamp?
					dolog("debugger","Writing to RAM(w): %08x=%02x (%c)", (uint_32)address,value,stringsafeDebugger(value));
					log_logtimestamp(log_timestampbackup); //Restore state!
				}
				else
				{
					if (strcmp(debugger_memoryaccess_text,"")==0) //Nothing logged yet?
					{
						snprintf(debugger_memoryaccess_text,sizeof(debugger_memoryaccess_text),"RAM(w):%08" SPRINTF_x_UINT32 "=%02x(%c)", (uint_32)address,value,stringsafeDebugger(value)); //Compact version!
					}
					else
					{
						snprintf(debugger_memoryaccess_line,sizeof(debugger_memoryaccess_line),"RAM(w):%08" SPRINTF_x_UINT32 "=%02x(%c)", (uint_32)address,value,stringsafeDebugger(value)); //Compact version!
						safestrcat_text("; ");
						safestrcat_text(debugger_memoryaccess_line); //Add the line!
					}
				}
				break;
			case LOGMEMORYACCESS_RAM_LOGMMUALL:
				if (((DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE_SIMPLIFIED) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE_SIMPLIFIED) && !((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT))) || debugger_forceimmediatelogging) //Not using a single line?
				{
					log_timestampbackup = log_logtimestamp(2); //Save state!
					log_logtimestamp(debugger_loggingtimestamp); //Are we to log the timestamp?
					dolog("debugger","MMU: Writing to real(w): %08x=%02x (%c)", (uint_32)address,value,stringsafeDebugger(value));
					log_logtimestamp(log_timestampbackup); //Restore state!
				}
				else
				{
					if (strcmp(debugger_memoryaccess_text,"")==0) //Nothing logged yet?
					{
						snprintf(debugger_memoryaccess_text,sizeof(debugger_memoryaccess_text),"RealRAM(w):%08" SPRINTF_x_UINT32 "=%02x(%c)", (uint_32)address,value,stringsafeDebugger(value)); //Compact version!
					}
					else
					{
						snprintf(debugger_memoryaccess_line,sizeof(debugger_memoryaccess_line),"RealRAM(w):%08" SPRINTF_x_UINT32 "=%02x(%c)", (uint_32)address,value,stringsafeDebugger(value)); //Compact version!
						safestrcat_text("; ");
						safestrcat_text(debugger_memoryaccess_line); //Add the line!
					}
				}
				break;
		}
	}
	else
	{
		switch (type&7)
		{
			case LOGMEMORYACCESS_NORMAL:
				if (((DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE_SIMPLIFIED) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE_SIMPLIFIED) && !((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT))) || debugger_forceimmediatelogging) //Not using a single line?
				{
					log_timestampbackup = log_logtimestamp(2); //Save state!
					log_logtimestamp(debugger_loggingtimestamp); //Are we to log the timestamp?
					dolog("debugger","Reading from normal memory(%c): %08x=%02x (%c)",(type&LOGMEMORYACCESS_PREFETCH)?'p':'r', (uint_32)address,value,stringsafeDebugger(value));
					log_logtimestamp(log_timestampbackup); //Restore state!
				}
				else
				{
					if (strcmp(debugger_memoryaccess_text,"")==0) //Nothing logged yet?
					{
						snprintf(debugger_memoryaccess_text,sizeof(debugger_memoryaccess_text),"Normal(%c):%08" SPRINTF_x_UINT32 "=%02x(%c)",(type&LOGMEMORYACCESS_PREFETCH)?'p':'r', (uint_32)address,value,stringsafeDebugger(value)); //Compact version!
					}
					else
					{
						snprintf(debugger_memoryaccess_line,sizeof(debugger_memoryaccess_line),"Normal(%c):%08" SPRINTF_x_UINT32 "=%02x(%c)",(type&LOGMEMORYACCESS_PREFETCH)?'p':'r', (uint_32)address,value,stringsafeDebugger(value)); //Compact version!
						safestrcat_text("; ");
						safestrcat_text(debugger_memoryaccess_line); //Add the line!
					}
				}
				break;
			case LOGMEMORYACCESS_PAGED:
				if (((DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE_SIMPLIFIED) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE_SIMPLIFIED) && !((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT))) || debugger_forceimmediatelogging) //Not using a single line?
				{
					log_timestampbackup = log_logtimestamp(2); //Save state!
					log_logtimestamp(debugger_loggingtimestamp); //Are we to log the timestamp?
					dolog("debugger","Reading from paged memory(%c): %08x=%02x (%c)",(type&LOGMEMORYACCESS_PREFETCH)?'p':'r', (uint_32)address,value,stringsafeDebugger(value));
					log_logtimestamp(log_timestampbackup); //Restore state!
				}
				else
				{
					if (strcmp(debugger_memoryaccess_text,"")==0) //Nothing logged yet?
					{
						snprintf(debugger_memoryaccess_text,sizeof(debugger_memoryaccess_text),"Paged(%c):%08" SPRINTF_x_UINT32 "=%02x(%c)",(type&LOGMEMORYACCESS_PREFETCH)?'p':'r', (uint_32)address,value,stringsafeDebugger(value)); //Compact version!
					}
					else
					{
						snprintf(debugger_memoryaccess_line,sizeof(debugger_memoryaccess_line),"Paged(%c):%08" SPRINTF_x_UINT32 "=%02x(%c)",(type&LOGMEMORYACCESS_PREFETCH)?'p':'r', (uint_32)address,value,stringsafeDebugger(value)); //Compact version!
						safestrcat_text("; ");
						safestrcat_text(debugger_memoryaccess_line); //Add the line!
					}
				}
				break;
			case LOGMEMORYACCESS_DIRECT:
				if (((DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE_SIMPLIFIED) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE_SIMPLIFIED) && !((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT))) || debugger_forceimmediatelogging) //Not using a single line?
				{
					log_timestampbackup = log_logtimestamp(2); //Save state!
					log_logtimestamp(debugger_loggingtimestamp); //Are we to log the timestamp?
					dolog("debugger","Reading from physical memory(%c): %08" SPRINTF_x_UINT32 "=%02x (%c)",(type&LOGMEMORYACCESS_PREFETCH)?'p':'r', (uint_32)address,value,stringsafeDebugger(value));
					log_logtimestamp(log_timestampbackup); //Restore state!
				}
				else
				{
					if (strcmp(debugger_memoryaccess_text,"")==0) //Nothing logged yet?
					{
						snprintf(debugger_memoryaccess_text,sizeof(debugger_memoryaccess_text),"Physical(%c):%08" SPRINTF_x_UINT32 "=%02x(%c)",(type&LOGMEMORYACCESS_PREFETCH)?'p':'r', (uint_32)address,value,stringsafeDebugger(value)); //Compact version!
					}
					else
					{
						snprintf(debugger_memoryaccess_line,sizeof(debugger_memoryaccess_line),"Physical(%c):%08" SPRINTF_x_UINT32 "=%02x(%c)",(type&LOGMEMORYACCESS_PREFETCH)?'p':'r', (uint_32)address,value,stringsafeDebugger(value)); //Compact version!
						safestrcat_text("; ");
						safestrcat_text(debugger_memoryaccess_line); //Add the line!
					}
				}
				break;
			default:
			case LOGMEMORYACCESS_RAM:
				if (((DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE_SIMPLIFIED) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE_SIMPLIFIED) && !((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT))) || debugger_forceimmediatelogging) //Not using a single line?
				{
					log_timestampbackup = log_logtimestamp(2); //Save state!
					log_logtimestamp(debugger_loggingtimestamp); //Are we to log the timestamp?
					dolog("debugger","Reading from RAM(%c): %08" SPRINTF_x_UINT32 "=%02x (%c)",(type&LOGMEMORYACCESS_PREFETCH)?'p':'r', (uint_32)address,value,stringsafeDebugger(value));
					log_logtimestamp(log_timestampbackup); //Restore state!
				}
				else
				{
					if (strcmp(debugger_memoryaccess_text,"")==0) //Nothing logged yet?
					{
						snprintf(debugger_memoryaccess_text,sizeof(debugger_memoryaccess_text),"RAM(%c):%08" SPRINTF_x_UINT32 "=%02x(%c)",(type&LOGMEMORYACCESS_PREFETCH)?'p':'r', (uint_32)address,value,stringsafeDebugger(value)); //Compact version!
					}
					else
					{
						snprintf(debugger_memoryaccess_line,sizeof(debugger_memoryaccess_line),"RAM(%c):%08" SPRINTF_x_UINT32 "=%02x(%c)",(type&LOGMEMORYACCESS_PREFETCH)?'p':'r', (uint_32)address,value,stringsafeDebugger(value)); //Compact version!
						safestrcat_text("; ");
						safestrcat_text(debugger_memoryaccess_line); //Add the line!
					}
				}
				break;
			case LOGMEMORYACCESS_RAM_LOGMMUALL:
				if (((DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE_SIMPLIFIED) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE_SIMPLIFIED) && !((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT))) || debugger_forceimmediatelogging) //Not using a single line?
				{
					log_timestampbackup = log_logtimestamp(2); //Save state!
					log_logtimestamp(debugger_loggingtimestamp); //Are we to log the timestamp?
					dolog("debugger","MMU: Reading from real(%c): %08" SPRINTF_x_UINT32 "=%02x (%c)",(type&LOGMEMORYACCESS_PREFETCH)?'p':'r', (uint_32)address,value,stringsafeDebugger(value));
					log_logtimestamp(log_timestampbackup); //Restore state!
				}
				else
				{
					if (strcmp(debugger_memoryaccess_text,"")==0) //Nothing logged yet?
					{
						snprintf(debugger_memoryaccess_text,sizeof(debugger_memoryaccess_text),"RealRAM(%c):%08" SPRINTF_x_UINT32 "=%02x(%c)",(type&LOGMEMORYACCESS_PREFETCH)?'p':'r', (uint_32)address,value,stringsafeDebugger(value)); //Compact version!
					}
					else
					{
						snprintf(debugger_memoryaccess_line,sizeof(debugger_memoryaccess_line),"RealRAM(%c):%08" SPRINTF_x_UINT32 "=%02x(%c)",(type&LOGMEMORYACCESS_PREFETCH)?'p':'r', (uint_32)address,value,stringsafeDebugger(value)); //Compact version!
						safestrcat_text("; ");
						safestrcat_text(debugger_memoryaccess_line); //Add the line!
					}
				}
				break;
		}
	}
}

void debugger_beforeCPU() //Action before the CPU changes it's registers!
{
	if (activeCPU) return; //Only CPU #0!
	if (CPU[activeCPU].cpudebugger) //To apply the debugger generator?
	{
		static VERIFICATIONDATA verify, originalverify;
		if (!CPU[activeCPU].repeating) //Not repeating an instruction?
		{
			memcpy(&debuggerregisters, CPU[activeCPU].registers, sizeof(debuggerregisters)); //Copy the registers to our buffer for logging and debugging etc.
			memcpy(&debuggersegmentregistercache, CPU[activeCPU].SEG_DESCRIPTOR, MIN(sizeof(CPU[activeCPU].SEG_DESCRIPTOR),sizeof(debuggersegmentregistercache))); //Copy the registers to our buffer for logging and debugging etc.
		}
		//Initialise debugger texts!
		cleardata(&debugger_prefix[0],sizeof(debugger_prefix));
		cleardata(&debugger_command_text[0],sizeof(debugger_command_text)); //Init vars!
		safestrcpy(debugger_prefix,sizeof(debugger_prefix),""); //Initialise the prefix(es)!
		safestrcpy(debugger_command_text,sizeof(debugger_command_text),"<DEBUGGER UNKOP NOT IMPLEMENTED>"); //Standard: unknown opcode!
		debugger_set = 0; //Default: the debugger isn't implemented!
		debugger_instructionexecuting = 0; //Not yet executing!
		debuggerHLT = CPU[activeCPU].halt; //Are we halted?
		debuggerReset = CPU[activeCPU].is_reset|(CPU[activeCPU].permanentreset<<1); //Are we reset?
		debuggerCPL = CPU[activeCPU].CPL; //Current CPL for the debugger to use!

		if (verifyfile) //Verification file exists?
		{
			if (!file_exists("debuggerverify16.dat")) return; //Abort if it doesn't exist anymore!
			if (HWINT_saved) //Saved HW interrupt?
			{
				switch (HWINT_saved)
				{
				case 1: //Trap/SW Interrupt?
					log_timestampbackup = log_logtimestamp(2); //Save state!
					log_logtimestamp(debugger_loggingtimestamp); //Are we to log the timestamp?
					dolog("debugger", "Trapped interrupt: %04x", HWINT_nr);
					log_logtimestamp(log_timestampbackup); //Restore state!
					break;
				case 2: //PIC Interrupt toggle?
					log_timestampbackup = log_logtimestamp(2); //Save state!
					log_logtimestamp(debugger_loggingtimestamp); //Are we to log the timestamp?
					dolog("debugger", "HW interrupt: %04x", HWINT_nr);
					log_logtimestamp(log_timestampbackup); //Restore state!
					break;
				default: //Unknown?
					break;
				}
			}
			nextspecial: //Special entry loop!
			if (readverification(debugger_index, &verify)) //Read the current index?
			{
				if (verify.type) //Special type?
				{
					switch (verify.type) //What type?
					{
					case 1: //Trap/SW Interrupt?
						log_timestampbackup = log_logtimestamp(2); //Save state!
						log_logtimestamp(debugger_loggingtimestamp); //Are we to log the timestamp?
						dolog("debugger", "debuggerverify.dat: Trapped Interrupt: %04x", verify.CS); //Trap interrupt!
						log_logtimestamp(log_timestampbackup); //Restore state!
						break;
					case 2: //PIC Interrupt toggle?
						log_timestampbackup = log_logtimestamp(2); //Save state!
						log_logtimestamp(debugger_loggingtimestamp); //Are we to log the timestamp?
						dolog("debugger", "debuggerverify.dat: HW Interrupt: %04x", verify.CS); //HW interrupt!
						log_logtimestamp(log_timestampbackup); //Restore state!
					break;
					default: //Unknown?
						break; //Skip unknown special types: we don't handle them!
					}
					++debugger_index; //Skip this entry!
					goto nextspecial; //Check the next entry for special types/normal type!
				}
			}
			originalverify.CS = REGD_CS(debuggerregisters);
			originalverify.SS = REGD_SS(debuggerregisters);
			originalverify.DS = REGD_DS(debuggerregisters);
			originalverify.ES = REGD_ES(debuggerregisters);
			originalverify.SI = REGD_SI(debuggerregisters);
			originalverify.DI = REGD_DI(debuggerregisters);
			originalverify.SP = REGD_SP(debuggerregisters);
			originalverify.BP = REGD_BP(debuggerregisters);
			originalverify.AX = REGD_AX(debuggerregisters);
			originalverify.BX = REGD_BX(debuggerregisters);
			originalverify.CX = REGD_CX(debuggerregisters);
			originalverify.DX = REGD_DX(debuggerregisters);
			originalverify.IP = REGD_IP(debuggerregisters);
			originalverify.FLAGS = REGD_FLAGS(debuggerregisters);
			if (readverification(debugger_index,&verify)) //Read the verification entry!
			{
				if (EMULATED_CPU < CPU_80286) //Special case for 80(1)86 from fake86!
				{
					originalverify.FLAGS &= ~0xF000; //Clear the high 4 bits: they're not set in the dump!
				}
				if (memcmp(&verify, &originalverify, sizeof(verify)) != 0) //Not equal?
				{
					log_timestampbackup = log_logtimestamp(2); //Save state!
					log_logtimestamp(debugger_loggingtimestamp); //Are we to log the timestamp?
					dolog("debugger", "Invalid data according to debuggerverify.dat before executing the following instruction(Entry number %08x):",debugger_index); //Show where we got our error!
					debugger_logregisters("debugger",&debuggerregisters,debuggerHLT,debuggerReset); //Log the original registers!
					//Apply the debugger registers to the actual register set!
					REG_CS = verify.CS;
					REG_SS = verify.SS;
					REG_DS = verify.DS;
					REG_ES = verify.ES;
					REG_SI = verify.SI;
					REG_DI = verify.DI;
					REG_SP = verify.SP;
					REG_BP = verify.BP;
					REG_AX = verify.AX;
					REG_BX = verify.BX;
					REG_CX = verify.CX;
					REG_DX = verify.DX;
					REG_IP = verify.IP;
					REG_FLAGS = verify.FLAGS;
					updateCPUmode(); //Update the CPU mode: flags have been changed!
					log_timestampbackup = log_logtimestamp(2); //Save state!
					log_logtimestamp(debugger_loggingtimestamp); //Are we to log the timestamp?
					dolog("debugger", "Expected:");
					log_logtimestamp(log_timestampbackup); //Restore state!
					debugger_logregisters("debugger",CPU[activeCPU].registers,debuggerHLT,debuggerReset); //Log the correct registers!
					//Refresh our debugger registers!
					memcpy(&debuggerregisters,CPU[activeCPU].registers, sizeof(debuggerregisters)); //Copy the registers to our buffer for logging and debugging etc.
					forcerepeat = 1; //Force repeat log!
				}
			}
			++debugger_index; //Apply next index!
		}
	} //Are we logging or needing info?
}

char flags[256]; //Flags as a text!
static char *debugger_generateFlags(CPU_registers *registers)
{
	safestrcpy(flags,sizeof(flags),""); //Clear the flags!

	//First the high word (80386+)!
	if (EMULATED_CPU>=CPU_80386) //386+?
	{
		//First, the unmapped bits!
		//Unmapped high bits!
		int i; //For counting the current bit!
		word j; //For holding the current bit!
		j = 1; //Start with value 1!
		for (i=9;i>=0;--i) //10-bits value rest!
		{
			if (FLAGREGR_UNMAPPEDHI(registers)&j) //Bit set?
			{
				safescatnprintf(flags,sizeof(flags),"1"); //1!
			}
			else //Bit cleared?
			{
				safescatnprintf(flags,sizeof(flags),"0"); //0!
			}
			j <<= 1; //Shift to the next bit!
		}

		if (EMULATED_CPU>=CPU_80486) //ID is available?
		{
			safescatnprintf(flags,sizeof(flags),"%c",(char)(FLAGREGR_ID(registers)?'I':'i'));
		}
		else //No ID?
		{
			safescatnprintf(flags,sizeof(flags),"%u",FLAGREGR_ID(registers));
		}

		if (EMULATED_CPU>=CPU_PENTIUM) //Pentium+?
		{
			safescatnprintf(flags,sizeof(flags),"%c",(char)(FLAGREGR_VIP(registers)?'P':'p'));
			safescatnprintf(flags,sizeof(flags),"%c",(char)(FLAGREGR_VIF(registers)?'F':'f'));
		}
		else //386/486?
		{
			safescatnprintf(flags,sizeof(flags),"%u",FLAGREGR_VIP(registers));
			safescatnprintf(flags,sizeof(flags),"%u",FLAGREGR_VIF(registers));
		}

		if (EMULATED_CPU>=CPU_80486) //486+?
		{
			safescatnprintf(flags,sizeof(flags),"%c",(char)(FLAGREGR_AC(registers)?'A':'a'));
		}
		else //386?
		{
			safescatnprintf(flags,sizeof(flags),"%u",FLAGREGR_AC(registers)); //Literal bit!
		}

		safescatnprintf(flags,sizeof(flags),"%c",(char)(FLAGREGR_V8(registers)?'V':'v'));
		safescatnprintf(flags,sizeof(flags),"%c",(char)(FLAGREGR_RF(registers)?'R':'r'));
	}

	//Higest 16-bit value!
	safescatnprintf(flags,sizeof(flags),"%u",FLAGREGR_UNMAPPED32768(registers));

	if (EMULATED_CPU>=CPU_80286) //286+?
	{
		safescatnprintf(flags,sizeof(flags),"%c",(char)(FLAGREGR_NT(registers)?'N':'n'));
		safescatnprintf(flags,sizeof(flags),"%u",(word)((FLAGREGR_IOPL(registers)&2)>>1));
		safescatnprintf(flags,sizeof(flags),"%u",(word)(FLAGREGR_IOPL(registers)&1));
	}
	else //186-? Display as numbers!
	{
		safescatnprintf(flags,sizeof(flags),"%u",FLAGREGR_NT(registers));
		safescatnprintf(flags,sizeof(flags),"%u",(word)((FLAGREGR_IOPL(registers)&2)>>1));
		safescatnprintf(flags,sizeof(flags),"%u",(word)(FLAGREGR_IOPL(registers)&1));
	}

	safescatnprintf(flags,sizeof(flags),"%c",(char)(FLAGREGR_OF(registers)?'O':'o'));
	safescatnprintf(flags,sizeof(flags),"%c",(char)(FLAGREGR_DF(registers)?'D':'d'));
	safescatnprintf(flags,sizeof(flags),"%c",(char)(FLAGREGR_IF(registers)?'I':'i'));
	safescatnprintf(flags,sizeof(flags),"%c",(char)(FLAGREGR_TF(registers)?'T':'t'));
	safescatnprintf(flags,sizeof(flags),"%c",(char)(FLAGREGR_SF(registers)?'S':'s'));
	safescatnprintf(flags,sizeof(flags),"%c",(char)(FLAGREGR_ZF(registers)?'Z':'z'));
	safescatnprintf(flags,sizeof(flags),"%u",FLAGREGR_UNMAPPED32(registers));
	safescatnprintf(flags,sizeof(flags),"%c",(char)(FLAGREGR_AF(registers)?'A':'a'));
	safescatnprintf(flags,sizeof(flags),"%u",FLAGREGR_UNMAPPED8(registers));
	safescatnprintf(flags,sizeof(flags),"%c",(char)(FLAGREGR_PF(registers)?'P':'p'));
	safescatnprintf(flags,sizeof(flags),"%u",FLAGREGR_UNMAPPED2(registers));
	safescatnprintf(flags,sizeof(flags),"%c",(char)(FLAGREGR_CF(registers)?'C':'c'));

	return &flags[0]; //Give the flags for quick reference!
}

OPTINLINE char decodeHLTreset(byte halted,byte isreset)
{
	if (halted)
	{
		return 'H'; //We're halted!
	}
	else if (isreset)
	{
		if (isreset&2) //Permanently reset?
		{
			return '*'; //We're permanently reset!
		}
		//Normal reset?
		return 'R'; //We're reset!
	}
	return ' '; //Nothing to report, give empty!
}

byte descordering[8] = { CPU_SEGMENT_CS,CPU_SEGMENT_SS,CPU_SEGMENT_DS,CPU_SEGMENT_ES,CPU_SEGMENT_FS,CPU_SEGMENT_GS,CPU_SEGMENT_TR,CPU_SEGMENT_LDTR };

void debugger_logdescriptors(char* filename)
{
	byte whatdesc;
	byte descnr;
	char* textseg;
	//Descriptors themselves!
	if (EMULATED_CPU >= CPU_80286) //Having descriptors on this CPU?
	{
		for (descnr = 0; descnr < NUMITEMS(debuggersegmentregistercache); ++descnr) //Process all segment descriptors!
		{
			whatdesc = descordering[descnr]; //The processing descriptor, ordered!
			textseg = CPU_segmentname(whatdesc); //Get the text value of the segment!
			if (((whatdesc == CPU_SEGMENT_FS) || (whatdesc == CPU_SEGMENT_GS)) && (EMULATED_CPU < CPU_80386)) continue; //Skip non-present descriptors!
			if (likely(textseg && (whatdesc<NUMITEMS(debuggersegmentregistercache)))) //Valid name/entry?
			{
				dolog(filename, "%s descriptor: %08X%08X", textseg, (debuggersegmentregistercache[whatdesc].desc.DATA64 >> 32), (debuggersegmentregistercache[whatdesc].desc.DATA64 & 0xFFFFFFFF)); //Log the descriptor's cache!
			}
		}
	}
}

extern GPU_TEXTSURFACE* frameratesurface; //The framerate surface!

void debugger_printdescriptors()
{
	byte whatdesc;
	byte descnr;
	char* textseg;
	uint_32 fontcolor = RGB(0xFF, 0xFF, 0xFF);
	uint_32 backcolor = RGB(0x00, 0x00, 0x00);
	//Descriptors themselves!
	byte desccounter;
	byte descnrs;
	desccounter = 0; //Init counter!
	descnrs = NUMITEMS(debuggersegmentregistercache); //Number of items to display!
	if (EMULATED_CPU >= CPU_80286) //Having descriptors on this CPU?
	{
		if (EMULATED_CPU < CPU_80386) //No FS/GS descriptors?
		{
			descnrs -= 2; //2 descriptors less!
		}
		for (descnr = 0; descnr < NUMITEMS(debuggersegmentregistercache); ++descnr) //Process all segment descriptors!
		{
			whatdesc = descordering[descnr]; //The processing descriptor, ordered!
			textseg = CPU_segmentname(whatdesc); //Get the text value of the segment!
			if (((whatdesc == CPU_SEGMENT_FS) || (whatdesc == CPU_SEGMENT_GS)) && (EMULATED_CPU < CPU_80386)) continue; //Skip non-present 386+ descriptors!
			if (likely(textseg && (whatdesc < NUMITEMS(debuggersegmentregistercache)))) //Valid name/entry?
			{
				GPU_textgotoxy(frameratesurface, 0, (GPU_TEXTSURFACE_HEIGHT-descnrs)+desccounter++); //Nth row below bottom!
				GPU_textprintf(frameratesurface, fontcolor, backcolor,"%s BASE:%08X LIMIT:%08X AR:%02X U:%02X  ", textseg, debuggersegmentregistercache[whatdesc].PRECALCS.base, debuggersegmentregistercache[whatdesc].PRECALCS.limit, debuggersegmentregistercache[whatdesc].desc.AccessRights, debuggersegmentregistercache[whatdesc].desc.noncallgate_info); //Display the descriptor's cache!
			}
		}
	}
}

void debugger_logregisters(char *filename, CPU_registers *registers, byte halted, byte isreset)
{
	if (likely(DEBUGGER_LOGREGISTERS==0)) //Disable register loggimg?
	{
		return; //No register logging, we're disabled for now!
	}
	if ((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_NOREGISTERS) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_SINGLELINE) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_SINGLELINE) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_SINGLELINE_SIMPLIFIED) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_SINGLELINE_SIMPLIFIED)) return; //Don't log the register state?
	if (!registers || !filename) //Invalid?
	{
		log_timestampbackup = log_logtimestamp(2); //Save state!
		log_logtimestamp(debugger_loggingtimestamp); //Are we to log the timestamp?
		dolog(filename,"Log registers called with invalid argument!");
		log_logtimestamp(log_timestampbackup); //Restore state!
		return; //Abort!
	}
	log_timestampbackup = log_logtimestamp(2); //Save state!
	log_logtimestamp(debugger_loggingtimestamp); //Are we to log the timestamp?
	//(DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT)
	if (EMULATED_CPU<=CPU_80286) //Emulating 80(1)86 registers or 80286?
	{
		#ifndef LOGFLAGSONLY
		dolog(filename,"Registers:"); //Start of the registers!
		dolog(filename,"AX: %04x BX: %04x CX: %04x DX: %04x",REGR_AX(registers),REGR_BX(registers),REGR_CX(registers),REGR_DX(registers)); //Basic registers!
		dolog(filename,"SP: %04x BP: %04x SI: %04x DI: %04x",REGR_SP(registers),REGR_BP(registers),REGR_SI(registers),REGR_DI(registers)); //Segment registers!
		if (EMULATED_CPU==CPU_80286) //Protected mode available?
		{
			dolog(filename,"CS: %04x DS: %04x ES: %04x SS: %04x TR: %04x LDTR: %04x",REGR_CS(registers),REGR_DS(registers),REGR_ES(registers),REGR_SS(registers),REGR_TR(registers),REGR_LDTR(registers)); //Segment registers!
		}
		else //Real mode only?
		{
			dolog(filename,"CS: %04x DS: %04x ES: %04x SS: %04x",REGR_CS(registers),REGR_DS(registers),REGR_ES(registers),REGR_SS(registers)); //Segment registers!
		}
		dolog(filename,"IP: %04x FLAGS: %04x",REGR_IP(registers),REGR_FLAGS(registers)); //Rest!
		if (EMULATED_CPU==CPU_80286) //80286 has CR0 as well?
		{
			dolog(filename, "CR0: %04x", (registers->CR0&0xFFFF)); //Rest!
			dolog(filename,"GDTR: " LONGLONGSPRINTx " IDTR: " LONGLONGSPRINTx,(LONG64SPRINTF)registers->GDTR.data,(LONG64SPRINTF)registers->IDTR.data); //GDTR/IDTR!
			if (advancedlog) //Advanced log enabled?
			{
				debugger_logdescriptors(filename); //Log descriptors too!
			}
		}
		if (advancedlog) //Advanced log enabled?
		{
			if (CPU[activeCPU].exec_lastCS!=REGR_CS(registers))
			{
				if ((DEBUGGER_LOG == DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG == DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG == DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT)) //Common log format?
					dolog(filename, "\t\t\tPrevious CS:IP: %04x:%04x", CPU[activeCPU].exec_lastCS,CPU[activeCPU].exec_lastEIP);
				else
					dolog(filename, "Previous CS:IP: %04x:%04x", CPU[activeCPU].exec_lastCS, CPU[activeCPU].exec_lastEIP);
			}
		}
		#endif
		dolog(filename,"FLAGSINFO: %s%c",debugger_generateFlags(registers),decodeHLTreset(halted,isreset)); //Log the flags!
		//More aren't implemented in the 80(1/2)86!
	}
	else //80386+? 32-bit registers!
	{
		dolog(filename,"Registers:"); //Start of the registers!
		#ifndef LOGFLAGSONLY0
		dolog(filename,"EAX: %08x EBX: %08x ECX: %08x EDX: %08x",REGR_EAX(registers),REGR_EBX(registers),REGR_ECX(registers),REGR_EDX(registers)); //Basic registers!
		dolog(filename,"ESP: %08x EBP: %08x ESI: %08x EDI: %08x",REGR_ESP(registers),REGR_EBP(registers),REGR_ESI(registers),REGR_EDI(registers)); //Segment registers!
		
		dolog(filename,"CS: %04x DS: %04x ES: %04x FS: %04x GS: %04x SS: %04x TR: %04x LDTR: %04x",REGR_CS(registers),REGR_DS(registers),REGR_ES(registers),REGR_FS(registers),REGR_GS(registers),REGR_SS(registers),REGR_TR(registers),REGR_LDTR(registers)); //Segment registers!

		dolog(filename,"EIP: %08x EFLAGS: %08x",REGR_EIP(registers),REGR_EFLAGS(registers)); //Rest!
		
		dolog(filename, "CR0: %08x CR1: %08x CR2: %08x CR3: %08x", registers->CR0, registers->CR1, registers->CR2, registers->CR3); //Rest!
		if (EMULATED_CPU>CPU_80386) //More available?
		{
			dolog(filename, "CR4: %08x", registers->CR4); //Rest!
		}

		dolog(filename, "DR0: %08x DR1: %08x DR2: %08x DR3: %08x", registers->DR0, registers->DR1, registers->DR2, registers->DR3); //Rest!
		dolog(filename, "DR6: %08x DR7: %08x", registers->DR6, registers->DR7); //Rest!

		dolog(filename,"GDTR: " LONGLONGSPRINTx " IDTR: " LONGLONGSPRINTx,(LONG64SPRINTF)registers->GDTR.data,(LONG64SPRINTF)registers->IDTR.data); //GDTR/IDTR!
		if (advancedlog) //Advanced log enabled?
		{
			debugger_logdescriptors(filename); //Log descriptors too!
			if (CPU[activeCPU].exec_lastCS!=REGR_CS(registers))
			{
				if ((DEBUGGER_LOG == DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG == DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG == DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT)) //Common log format?
					dolog(filename, "\t\t\tPrevious CS:EIP: %04x:%08x", CPU[activeCPU].exec_lastCS, CPU[activeCPU].exec_lastEIP);
				else
					dolog(filename, "Previous CS:EIP: %04x:%08x", CPU[activeCPU].exec_lastCS, CPU[activeCPU].exec_lastEIP);
			}
		}
		#endif
		//Finally, flags seperated!
		dolog(filename,"FLAGSINFO: %s%c",debugger_generateFlags(registers),(char)(halted?'H':' ')); //Log the flags!
	}
	log_logtimestamp(log_timestampbackup); //Restore state!
}

void debugger_logmisc(char *filename, CPU_registers *registers, byte halted, byte isreset, CPU_type *theCPU)
{
	if ((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT)) //Common log format?
	{
		return; //No misc logging, we're disabled for now!
	}
	if ((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_NOREGISTERS) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_SINGLELINE) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_SINGLELINE) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_SINGLELINE_SIMPLIFIED) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_SINGLELINE_SIMPLIFIED)) return; //Don't log us: don't log register state!
	log_timestampbackup = log_logtimestamp(2); //Save state!
	//Interrupt status
	int i;
	//Full interrupt status!
	char buffer[0x11] = ""; //Empty buffer to fill!
	safestrcpy(buffer,sizeof(buffer),""); //Clear the buffer!
	for (i = 0xF;i >= 0;i--) //All 16 interrupt flags!
	{
		safescatnprintf(buffer,sizeof(buffer),"%u",(i8259.irr[(i&8)>>3]>>(i&7))&1); //Show the interrupt status!
	}
	log_logtimestamp(debugger_loggingtimestamp); //Are we to log the timestamp?
	dolog(filename,"Interrupt status: %s",buffer); //Log the interrupt status!
	safestrcpy(buffer,sizeof(buffer),""); //Clear the buffer!
	for (i = 0xF;i >= 0;i--) //All 16 interrupt flags!
	{
		safescatnprintf(buffer,sizeof(buffer),"%u",(i8259.imr[(i&8)>>3]>>(i&7))&1); //Show the interrupt status!
	}
	dolog(filename,"Interrupt mask: %s",buffer); //Log the interrupt status!
	if (getActiveVGA() && debugger_logtimings) //Gotten an active VGA?
	{
		dolog(filename,"VGA@%u,%u(CRT:%u,%u)",((SEQ_DATA *)getActiveVGA()->Sequencer)->x,((SEQ_DATA *)getActiveVGA()->Sequencer)->Scanline,getActiveVGA()->CRTC.x,getActiveVGA()->CRTC.y);
		dolog(filename,"Display=%u,%u",GPU.xres,GPU.yres);
	}
	log_logtimestamp(log_timestampbackup); //Restore state!
}

extern BIU_type BIU[MAXCPUS]; //The BIU we're emulating!

extern byte DMA_S; //DMA state of transfer(clocks S0-S3), when active!

extern char DMA_States_text[6][256]; //DMA states!

char executedinstruction[256];
char statelog[256];
char executedinstructionstatelog[2048];
char fullcmd[65536];

void debugger_notifyRunning() //Notify the debugger that instruction execution has begun!
{
	debugger_instructionexecuting |= 1; //We've started executing!
}

OPTINLINE void debugger_autolog()
{
	byte dologinstruction = 1;
	dologinstruction = 1; //Default: log the instruction!
	if (activeCPU) return; //Only with processor #0!
	if (unlikely(CPU[activeCPU].executed)) //Are we executed?
	{
		if ((CPU[activeCPU].repeating|debuggerHLT) && (!CPU[activeCPU].faultraised) && (!forcerepeat))
		{
			dologinstruction = 0; //Are we a repeating(REP-prefixed) instruction that's still looping internally or halted CPU state and no fault has been raised? We're a repeat/<HLT> operation that we don't log instructions for!
		}
		forcerepeat = 0; //Don't force repeats anymore if forcing!
	}

	if (unlikely(debugger_is_logging)) //To log?
	{
		log_timestampbackup = log_logtimestamp(2); //Save state!
		log_logtimestamp(debugger_loggingtimestamp); //Are we to log the timestamp?
		safestrcpy(executedinstruction, sizeof(executedinstruction), ""); //Clear instruction that's to be logged by default!
		if (((debugger_instructionexecuting == 1) && dologinstruction && debugger_logtimings) || (CPU[activeCPU].executed && dologinstruction && (!debugger_logtimings))) //Instruction started to execute(timings) or finished(no timings)?
		{
			if (debugger_logtimings)
			{
				debugger_instructionexecuting |= 2; //Stop more than one cycle for the instruction!
			}
			//Now generate debugger information!
			if ((DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE_SIMPLIFIED) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE_SIMPLIFIED) && (DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) && (DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT)) //Not single-line?
			{
				if (advancedlog) //Advanced logging?
				{
					if (CPU[activeCPU].last_modrm)
					{
						if (EMULATED_CPU <= CPU_80286) //16-bits addresses?
						{
							dolog("debugger", "ModR/M address: %04x:%04x=%08x", CPU[activeCPU].modrm_lastsegment, CPU[activeCPU].modrm_lastoffset, ((CPU[activeCPU].modrm_lastsegment << 4) + CPU[activeCPU].modrm_lastoffset));
						}
						else //386+? Unknown addresses, so just take it as given!
						{
							dolog("debugger", "ModR/M address: %04x:%08x", CPU[activeCPU].modrm_lastsegment, CPU[activeCPU].modrm_lastoffset);
						}
					}
					if (MMU_invaddr()) //We've detected an invalid address?
					{
						switch (MMU_invaddr()) //What error?
						{
						case 0: //OK!
							break;
						case 1: //Memory not found!
							dolog("debugger", "MMU has detected that the addressed data isn't valid! The memory is non-existant.");
							break;
						case 2: //Paging or protection fault!
							dolog("debugger", "MMU has detected that the addressed data isn't valid! The memory is not paged or protected.");
							break;
						default:
							dolog("debugger", "MMU has detected that the addressed data isn't valid! The cause is unknown.");
							break;
						}
					}
					if (CPU[activeCPU].faultraised) //Fault has been raised?
					{
						dolog("debugger", "The CPU has raised an exception.");
					}
				}
			}
			cleardata(&fullcmd[0],sizeof(fullcmd)); //Init!
			int i; //A counter for opcode data dump!
			if (!debugger_set) //No debugger set?
			{
				if (!((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT))) safestrcpy(fullcmd,sizeof(fullcmd),"<Debugger not implemented: "); //Set to the last opcode!
				for (i = 0; i < (int)CPU[activeCPU].OPlength; i++) //List the full command!
				{
					if (!((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT))) snprintf(fullcmd,sizeof(fullcmd), "%s%02X", debugger_command_text, CPU[activeCPU].OPbuffer[i]); //Add part of the opcode!
					else snprintf(fullcmd,sizeof(fullcmd), fullcmd[0]?"%s %02X":"%s%02X", debugger_command_text, CPU[activeCPU].OPbuffer[i]); //Add part of the opcode!
				}
				if (!((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT))) safestrcat(fullcmd,sizeof(fullcmd), ">"); //End of #UNKOP!
			}
			else
			{
				if (!((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT))) safestrcpy(fullcmd,sizeof(fullcmd), "(");
				for (i = 0; i < (int)CPU[activeCPU].OPlength; i++) //List the full command!
				{
					if (!((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT))) safescatnprintf(fullcmd,sizeof(fullcmd), "%02X", CPU[activeCPU].OPbuffer[i]); //Add part of the opcode!
					else { safescatnprintf(fullcmd,sizeof(fullcmd), fullcmd[0]?" %02X":"%02X", CPU[activeCPU].OPbuffer[i]); } //Add part of the opcode!
				}
				if (!((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT))) safestrcat(fullcmd,sizeof(fullcmd), ")"); //Our opcode before disassembly!
				else safestrcat(fullcmd,sizeof(fullcmd), " "); //Our opcode before disassembly!
				safestrcat(fullcmd,sizeof(fullcmd), debugger_prefix); //The prefix(es)!
				safestrcat(fullcmd,sizeof(fullcmd), debugger_command_text); //Command itself!
			}

			if (HWINT_saved && (DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE_SIMPLIFIED) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE_SIMPLIFIED) && (DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) && (DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT)) //Saved HW interrupt?
			{
				switch (HWINT_saved)
				{
				case 1: //Trap/SW Interrupt?
					dolog("debugger", "Trapped interrupt: %04x", HWINT_nr);
					break;
				case 2: //PIC Interrupt toggle?
					dolog("debugger", "HW interrupt: %04x", HWINT_nr);
					break;
				default: //Unknown?
					break;
				}
			}

			if ((debuggerregisters.CR0&1)==0) //Emulating 80(1)86? Use IP!
			{
				if ((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT)) //Common log format?
				{
					snprintf(executedinstruction,sizeof(executedinstruction),"%04x:%08x %s",REGD_CS(debuggerregisters),REGD_IP(debuggerregisters),fullcmd); //Log command, 16-bit disassembler style!
				}
				else //8086 compatible log?
				{
					snprintf(executedinstruction,sizeof(executedinstruction),"%04x:%04x %s",REGD_CS(debuggerregisters),REGD_IP(debuggerregisters),fullcmd); //Log command, 16-bit disassembler style!
				}
			}
			else //286+? Use EIP!
			{
				if ((EMULATED_CPU>CPU_80286) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT)) //Newer? Use 32-bits addressing when newer or Common log format!
				{
					snprintf(executedinstruction,sizeof(executedinstruction),"%04x:%08" SPRINTF_x_UINT32 " %s",REGD_CS(debuggerregisters),REGD_EIP(debuggerregisters),fullcmd); //Log command, 32-bit disassembler style!
				}
				else //16-bits offset?
				{
					snprintf(executedinstruction,sizeof(executedinstruction),"%04x:%04" SPRINTF_x_UINT32 " %s",REGD_CS(debuggerregisters),REGD_EIP(debuggerregisters),fullcmd); //Log command, 32-bit disassembler style!
				}
			}
			if ((DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE_SIMPLIFIED) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE_SIMPLIFIED) && (DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) && (DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT)) //Not single line?
			{
				dolog("debugger",executedinstruction); //The executed instruction!
			}
		}

		if (debugger_logtimings) //Logging the timings?
		{
			safestrcpy(statelog,sizeof(statelog),""); //Default to empty!
			if (DEBUGGER_LOGSTATES) //Are we logging states?
			{
				if (CPU[activeCPU].BIUnotticking) goto debugger_finishExecutionstate;
				if (BIU[activeCPU].stallingBUS && ((BIU[activeCPU].stallingBUS!=3) || ((BIU[activeCPU].stallingBUS==3) && (BIU[activeCPU].BUSactive==0)))) //Stalling the BUS?
				{
					safestrcpy(statelog,sizeof(statelog),"BIU --"); //Stalling the BIU!
				}
				else if (BIU[activeCPU].TState<0xFE) //Not a special state?
				{
					if (debugger_simplifiedlog) //Simplified log?
					{
						snprintf(statelog,sizeof(statelog),"BIU T%u",
							(BIU[activeCPU].TState+1) //Current T-state!
							);
					}
					else //Normal full log?
					{
						snprintf(statelog,sizeof(statelog),"BIU T%u: EU&BIU cycles: %" SPRINTF_u_UINT32 ", Operation cycles: %u, HW interrupt cycles: %u, Prefix cycles: %u, Exception cycles: %u, Prefetching cycles: %u, BIU prefetching cycles(1 each): %u, BIU DMA cycles: %u",
							(BIU[activeCPU].TState+1), //Current T-state!
							CPU[activeCPU].cycles, //Cycles executed by the BIU!
							CPU[activeCPU].cycles_OP, //Total number of cycles for an operation!
							CPU[activeCPU].cycles_HWOP, //Total number of cycles for an hardware interrupt!
							CPU[activeCPU].cycles_Prefix, //Total number of cycles for the prefix!
							CPU[activeCPU].cycles_Exception, //Total number of cycles for an exception!
							CPU[activeCPU].cycles_Prefetch, //Total number of cycles for prefetching from memory!
							CPU[activeCPU].cycles_Prefetch_BIU, //BIU cycles actually spent on prefetching during the remaining idle BUS time!
							CPU[activeCPU].cycles_Prefetch_DMA //BIU cycles actually spent on prefetching during the remaining idle BUS time!
							);
					}
				}
				else
				{
					switch (BIU[activeCPU].TState) //What state?
					{
						default: //Unknown?
						case 0xFE: //DMA cycle?
							if (debugger_simplifiedlog) //Simplified log?
							{
								snprintf(statelog,sizeof(statelog),"DMA %s",
									DMA_States_text[DMA_S] //Current S-state!
									);
							}
							else //Normal full log?
							{
								snprintf(statelog,sizeof(statelog),"DMA %s: EU&BIU cycles: %" SPRINTF_u_UINT32 ", Operation cycles: %u, HW interrupt cycles: %u, Prefix cycles: %u, Exception cycles: %u, Prefetching cycles: %u, BIU prefetching cycles(1 each): %u, BIU DMA cycles: %u",
									DMA_States_text[DMA_S], //Current S-state!
									CPU[activeCPU].cycles, //Cycles executed by the BIU!
									CPU[activeCPU].cycles_OP, //Total number of cycles for an operation!
									CPU[activeCPU].cycles_HWOP, //Total number of cycles for an hardware interrupt!
									CPU[activeCPU].cycles_Prefix, //Total number of cycles for the prefix!
									CPU[activeCPU].cycles_Exception, //Total number of cycles for an exception!
									CPU[activeCPU].cycles_Prefetch, //Total number of cycles for prefetching from memory!
									CPU[activeCPU].cycles_Prefetch_BIU, //BIU cycles actually spent on prefetching during the remaining idle BUS time!
									CPU[activeCPU].cycles_Prefetch_DMA //BIU cycles actually spent on prefetching during the remaining idle BUS time!
									);
							}
							break;
						case 0xFF: //Waitstate RAM!
							if (debugger_simplifiedlog) //Simplified log?
							{
								snprintf(statelog,sizeof(statelog),"BIU W"
									);
							}
							else //Normal full log?
							{
								snprintf(statelog,sizeof(statelog),"BIU W: EU&BIU cycles: %" SPRINTF_u_UINT32 ", Operation cycles: %u, HW interrupt cycles: %u, Prefix cycles: %u, Exception cycles: %u, Prefetching cycles: %u, BIU prefetching cycles(1 each): %u, BIU DMA cycles: %u",
									CPU[activeCPU].cycles, //Cycles executed by the BIU!
									CPU[activeCPU].cycles_OP, //Total number of cycles for an operation!
									CPU[activeCPU].cycles_HWOP, //Total number of cycles for an hardware interrupt!
									CPU[activeCPU].cycles_Prefix, //Total number of cycles for the prefix!
									CPU[activeCPU].cycles_Exception, //Total number of cycles for an exception!
									CPU[activeCPU].cycles_Prefetch, //Total number of cycles for prefetching from memory!
									CPU[activeCPU].cycles_Prefetch_BIU, //BIU cycles actually spent on prefetching during the remaining idle BUS time!
									CPU[activeCPU].cycles_Prefetch_DMA //BIU cycles actually spent on prefetching during the remaining idle BUS time!
									);
							}
							break;
					}
				}
			}
			if ((DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE_SIMPLIFIED) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE_SIMPLIFIED) && (DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) && (DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT)) //Not logging single lines?
			{
				if (safestrlen(statelog,sizeof(statelog)))
				{
					dolog("debugger",statelog); //Log the state log only!
				}
			}
			else //Logging single line?
			{
				safestrcpy(executedinstructionstatelog,sizeof(executedinstructionstatelog),""); //Init!
				if (safestrlen(executedinstruction,sizeof(executedinstruction))) //Executed instruction?
				{
					if ((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT)) //Special case?
					{
						snprintf(executedinstructionstatelog,sizeof(executedinstructionstatelog),"%s",executedinstruction); //Universal format!
						goto finishstatelog; //Skip other data for now!
					}
					else
					{
						snprintf(executedinstructionstatelog,sizeof(executedinstructionstatelog),"%s\t%s",statelog,executedinstruction);
					}
				}
				else if (!((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT))) //State only?
				{
					snprintf(executedinstructionstatelog,sizeof(executedinstructionstatelog),"%s\t",statelog);
				}
				finishstatelog:
				if (safestrlen(debugger_memoryaccess_text,sizeof(debugger_memoryaccess_text))) //memory access?
				{
					if (((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT)) && (executedinstructionstatelog[0]=='\0')) //Special case?
					{
						dolog("debugger","\t%s",debugger_memoryaccess_text);
					}
					else //Normal case?
					{
						dolog("debugger","%s\t%s",executedinstructionstatelog,debugger_memoryaccess_text);
					}
				}
				else //(Instruction+)State only?
				{
					if (strcmp(executedinstructionstatelog,"\t")!=0) //Valid state(not containing nothing at all)?
					{
						if ((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT)) //Special case?
						{
							if (executedinstructionstatelog[0]) //Anything to log?
							{
								dolog("debugger","%s",executedinstructionstatelog); //Instruction/State only!
							}
						}
						else
						{
							dolog("debugger","%s\t",executedinstructionstatelog); //Instruction/State only!
						}
					}
				}
			}
			safestrcpy(debugger_memoryaccess_text,sizeof(debugger_memoryaccess_text),""); //Clear the text to apply: we're done!
		}
		debugger_finishExecutionstate: //Nothing to log for the current debugger cycle?
		log_logtimestamp(log_timestampbackup); //Restore state!

		if (unlikely(dologinstruction == 0)) return; //Abort when not logging the instruction(don't check below)!

		if (CPU[activeCPU].executed && (DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE) && (DEBUGGER_LOG!=DEBUGGERLOG_ALWAYS_SINGLELINE_SIMPLIFIED) && (DEBUGGER_LOG!=DEBUGGERLOG_DEBUGGING_SINGLELINE_SIMPLIFIED)) //Multiple lines and finished executing?
		{
			debugger_logregisters("debugger",&debuggerregisters,debuggerHLT,debuggerReset); //Log the previous (initial) register status!
		
			debugger_logmisc("debugger",&debuggerregisters,debuggerHLT,debuggerReset,&CPU[activeCPU]); //Log misc stuff!
			if (((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT))==0) //Allowed to log?
			{
				if (advancedlog)
				{
					log_timestampbackup = log_logtimestamp(2); //Save state!
					log_logtimestamp(debugger_loggingtimestamp); //Are we to log the timestamp?
					dolog("debugger", ""); //Empty line between comands!
					log_logtimestamp(log_timestampbackup); //Restore state!
				}
			}
			debuggerINT = 0; //Don't continue after an INT has been used!
		}
	} //Allow logging?
}

word debuggerrow; //Debugger row after the final row!
struct
{
	byte enabled; //Is the memory viewer visible?
	uint_64 address; //Address to start viewing!
	byte x; //X coordinate to select!
	byte y; //Y coordinate to select!
	byte virtualmemory; //Use virtual memory instead of physical memory?
	byte virtualmemoryCPL; //CPL to use for virtual memory!
} debugger_memoryviewer; //Memory viewer enabled on the debugger screen?

void debugger_screen() //Show debugger info on-screen!
{
	int memoryx, memoryy;
	int tablebasex, tablebasey;
	uint_32 effectiveaddress;
	uint_64 effectivememorydata;
	uint_64 physicaladdress;
	if (frameratesurface) //We can show?
	{
		GPU_text_locksurface(frameratesurface); //Lock!
		uint_32 fontcolor = RGB(0xFF, 0xFF, 0xFF); //Font color!
		uint_32 backcolor = RGB(0x00, 0x00, 0x00); //Back color!
		uint_32 fontcoloractive_blocked = RGB(0xAA, 0x55, 0x00); //Font color unmapped!
		uint_32 fontcolor_blocked = RGB(0xAA, 0xAA, 0xAA); //Font color unmapped!
		uint_32 fontcoloractive = RGB(0x00, 0xFF, 0x00); //Font color!
		uint_32 backcoloractive_blocked = RGB(0x00, 0x00, 0x00); //Back color unmapped!
		uint_32 backcolor_blocked = RGB(0x00, 0x00, 0x00); //Back color unmapped!
		uint_32 backcoloractive = RGB(0x00, 0x00, 0x00); //Back color!
		uint_32 currentfontcoloractive; //Current selected font color!
		uint_32 currentbackcoloractive; //Current selected back color!
		uint_32 currentfontcolor; //Current unselected font color!
		uint_32 currentbackcolor; //Current unselected back color!
		if (debugger_memoryviewer.enabled) //Memory viewer instead>
		{
 			GPU_textclearscreen(frameratesurface); //Clear the screen!
			GPU_textgotoxy(frameratesurface, 0, 0); //Goto start of the screen!
			if (debugger_memoryviewer.virtualmemory) //Virtual memory?
			{
				GPU_textprintf(frameratesurface, fontcolor, backcolor, "Virtual Memory viewer");
			}
			else
			{
				GPU_textprintf(frameratesurface, fontcolor, backcolor, "Physical Memory viewer");
			}

			effectiveaddress = debugger_memoryviewer.address + (debugger_memoryviewer.y * 0x10) + debugger_memoryviewer.x; //What address!

			GPU_textgotoxy(frameratesurface, 0, 2); //Second row!
			GPU_textprintf(frameratesurface, fontcolor, backcolor, "Address: %08X", effectiveaddress); //What address!

			tablebasex = 0; //Start column of the table!
			tablebasey = 4; //Start row of the table!

			//Draw the table and headers!
			for (memoryx = 0; memoryx < 0x11; ++memoryx) //All horizontal coordinates!
			{
				for (memoryy = 0; memoryy < 0x11; ++memoryy) //All vertical coordinates!
				{
					GPU_textgotoxy(frameratesurface, tablebasex + (memoryx * 3), tablebasey + (memoryy)); //Go to the location of the display!
					if ((memoryx == 0) && (memoryy == 0)) //Top-left corner? Don't display anything!
					{
						//Nothing in the top left corner!
					}
					else if (memoryx == 0) //Vertical header?
					{
						//First, check for the correct active color!
						currentfontcoloractive = fontcoloractive; //Default: normally active!
						currentbackcoloractive = backcoloractive; //Default: normally active!
						currentfontcolor = fontcolor; //Default: normally active!
						currentbackcolor = backcolor; //Default: normally active!
						if (debugger_memoryviewer.virtualmemory) //Using virtual memory?
						{
							if (!CPU_paging_translateaddr(debugger_memoryviewer.address + (debugger_memoryviewer.y * 0x10) + debugger_memoryviewer.x, debugger_memoryviewer.virtualmemoryCPL, &physicaladdress)) //Invalid address?
							{
								currentfontcoloractive = fontcoloractive_blocked; //Blocked color!
								currentbackcoloractive = backcoloractive_blocked; //Blocked color!
							}
							else //Mapped to physical memory?
							{
								if (MMU_directrb_hwdebugger(physicaladdress, 3, &effectivememorydata)) //Floating bus at this address?
								{
									currentfontcoloractive = fontcoloractive_blocked; //Blocked color!
									currentbackcoloractive = backcoloractive_blocked; //Blocked color!
								}
							}
						}
						else //Mapped to physical memory?
						{
							if (MMU_directrb_hwdebugger(debugger_memoryviewer.address + (debugger_memoryviewer.y * 0x10) + debugger_memoryviewer.x, 3, &effectivememorydata)) //Floating bus at this address?
							{
								currentfontcoloractive = fontcoloractive_blocked; //Blocked color!
								currentbackcoloractive = backcoloractive_blocked; //Blocked color!
							}
						}

						//Now, render the vertical header!
						effectiveaddress = debugger_memoryviewer.address + ((memoryy - 1) * 0x10); //What address!
						if ((memoryy - 1) == debugger_memoryviewer.y) //Selected row?
						{
							GPU_textprintf(frameratesurface, currentfontcoloractive, currentbackcoloractive, "%02X", ((effectiveaddress>>4) & 0xF));
						}
						else //Inactive?
						{
							GPU_textprintf(frameratesurface, currentfontcolor, currentbackcolor, "%02X", ((effectiveaddress>>4) & 0xF));
						}
					}
					else if (memoryy == 0) //Horizontal header?
					{
						//First, check for the correct active color!
						currentfontcoloractive = fontcoloractive; //Default: normally active!
						currentbackcoloractive = backcoloractive; //Default: normally active!
						currentfontcolor = fontcolor; //Default: normally active!
						currentbackcolor = backcolor; //Default: normally active!
						if (debugger_memoryviewer.virtualmemory) //Using virtual memory?
						{
							if (!CPU_paging_translateaddr(debugger_memoryviewer.address + (debugger_memoryviewer.y * 0x10) + debugger_memoryviewer.x, debugger_memoryviewer.virtualmemoryCPL, &physicaladdress)) //Invalid address?
							{
								currentfontcoloractive = fontcoloractive_blocked; //Blocked color!
								currentbackcoloractive = backcoloractive_blocked; //Blocked color!
							}
							else //Mapped to physical memory?
							{
								if (MMU_directrb_hwdebugger(physicaladdress, 3, &effectivememorydata)) //Floating bus at this address?
								{
									currentfontcoloractive = fontcoloractive_blocked; //Blocked color!
									currentbackcoloractive = backcoloractive_blocked; //Blocked color!
								}
							}
						}
						else //Mapped to physical memory?
						{
							if (MMU_directrb_hwdebugger(debugger_memoryviewer.address + (debugger_memoryviewer.y * 0x10) + debugger_memoryviewer.x, 3, &effectivememorydata)) //Floating bus at this address?
							{
								currentfontcoloractive = fontcoloractive_blocked; //Blocked color!
								currentbackcoloractive = backcoloractive_blocked; //Blocked color!
							}
						}

						//Now, render the horizontal header!
						effectiveaddress = debugger_memoryviewer.address + (memoryx - 1); //What address!
						if ((memoryx - 1) == debugger_memoryviewer.x) //Selected row?
						{
							GPU_textprintf(frameratesurface, currentfontcoloractive, currentbackcoloractive, "%02X", (effectiveaddress & 0xF));
						}
						else //Inactive?
						{
							GPU_textprintf(frameratesurface, currentfontcolor, currentbackcolor, "%02X", (effectiveaddress & 0xF));
						}
					}
					else //Memory data?
					{
						effectiveaddress = debugger_memoryviewer.address + ((memoryy - 1) * 0x10) + (memoryx - 1); //What address!
						currentfontcoloractive = fontcoloractive; //Default: normally active!
						currentbackcoloractive = backcoloractive; //Default: normally active!
						currentfontcolor = fontcolor; //Default: normally active!
						currentbackcolor = backcolor; //Default: normally active!
						if (debugger_memoryviewer.virtualmemory) //Using virtual memory?
						{
							if (CPU_paging_translateaddr(effectiveaddress, debugger_memoryviewer.virtualmemoryCPL, &physicaladdress)) //Valid address?
							{
								if (MMU_directrb_hwdebugger(physicaladdress, 3, &effectivememorydata)) //Floating bus at this address?
								{
									effectivememorydata = 0xFF; //Floating bus!
									currentfontcoloractive = fontcoloractive_blocked; //Blocked color!
									currentbackcoloractive = backcoloractive_blocked; //Blocked color!
									currentfontcolor = fontcolor_blocked; //Blocked color!
									currentbackcolor = backcolor_blocked; //Blocked color!
								}
							}
							else //Invalid address?
							{
								effectivememorydata = 0xFF; //Unmapped, so display a floating bus!
								currentfontcoloractive = fontcoloractive_blocked; //Blocked color!
								currentbackcoloractive = backcoloractive_blocked; //Blocked color!
								currentfontcolor = fontcolor_blocked; //Blocked color!
								currentbackcolor = backcolor_blocked; //Blocked color!
							}
						}
						else //Physical memory?
						{
							if (MMU_directrb_hwdebugger(effectiveaddress, 3, &effectivememorydata)) //Floating bus at this address?
							{
								effectivememorydata = 0xFF; //Floating bus!
								currentfontcoloractive = fontcoloractive_blocked; //Blocked color!
								currentbackcoloractive = backcoloractive_blocked; //Blocked color!
								currentfontcolor = fontcolor_blocked; //Blocked color!
								currentbackcolor = backcolor_blocked; //Blocked color!
							}
						}
						if (
							((memoryx - 1) == debugger_memoryviewer.x) && //Selected column?
							((memoryy - 1) == debugger_memoryviewer.y) //Selected row?
							)
						{
							GPU_textprintf(frameratesurface, currentfontcoloractive, currentbackcoloractive, "%02X", (effectivememorydata&0xFF));
						}
						else
						{
							GPU_textprintf(frameratesurface, currentfontcolor, currentbackcolor, "%02X", (effectivememorydata&0xFF));
						}
					}
				}
			}
			//Finished drawing. Finish up and don't draw the debugger!
			GPU_text_releasesurface(frameratesurface); //Unlock!
			return; //Don't show the normal debugger over it!
		}
		char str[256];
		cleardata(&str[0], sizeof(str)); //For clearing!
		int i;
		GPU_textgotoxy(frameratesurface, safe_strlen(debugger_prefix, sizeof(debugger_prefix)) + safe_strlen(debugger_command_text, sizeof(debugger_command_text)), GPU_TEXT_DEBUGGERROW); //Goto start of clearing!
		for (i = (safe_strlen(debugger_prefix, sizeof(debugger_prefix)) + safe_strlen(debugger_command_text, sizeof(debugger_command_text))); i < GPU_TEXTSURFACE_WIDTH - 6; i++) //Clear unneeded!
		{
			GPU_textprintf(frameratesurface, 0, 0, " "); //Clear all unneeded!
		}

		GPU_textgotoxy(frameratesurface, 0, GPU_TEXT_DEBUGGERROW);
		GPU_textprintf(frameratesurface, fontcolor, backcolor, "Command: %s%s", debugger_prefix, debugger_command_text); //Show our command!
		debuggerrow = GPU_TEXT_DEBUGGERROW; //The debug row we're writing to!	
		GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 22, debuggerrow++); //First debug row!
		GPU_textprintf(frameratesurface, fontcolor, backcolor, "Prefix(0):%02X; ROP: %02X%u", CPU[activeCPU].OPbuffer[0], CPU[activeCPU].currentopcode, MODRM_REG(CPU[activeCPU].currentmodrm)); //Debug opcode and executed opcode!

		//First: location!
		if ((((debuggerregisters.CR0&1)==0) || (REGD_EFLAGS(debuggerregisters)&F_V8)) || (EMULATED_CPU == CPU_80286)) //Real mode, virtual 8086 mode or normal real-mode registers used in 16-bit protected mode?
		{
			GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 15, debuggerrow++); //Second debug row!
			GPU_textprintf(frameratesurface, fontcolor, backcolor, "CS:IP %04X:%04X", REGD_CS(debuggerregisters), REGD_IP(debuggerregisters)); //Debug CS:IP!
		}
		else //386+?
		{
			if (EMULATED_CPU>=CPU_80386) //32-bit CPU?
			{
				GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 20, debuggerrow++); //Second debug row!
				GPU_textprintf(frameratesurface, fontcolor, backcolor, "CS:EIP %04X:%08X", REGD_CS(debuggerregisters), REGD_EIP(debuggerregisters)); //Debug IP!
			}
			else //286-?
			{
				GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 16, debuggerrow++); //Second debug row!
				GPU_textprintf(frameratesurface, fontcolor, backcolor, "CS:IP %04X:%04X", REGD_CS(debuggerregisters), REGD_IP(debuggerregisters)); //Debug IP!
			}
		}

		if (((((debuggerregisters.CR0&1)==0) || (REGD_EFLAGS(debuggerregisters)&F_V8)) || (EMULATED_CPU == CPU_80286)) && (CPU[0].exec_lastEIP<0x10000)) //Real mode, virtual 8086 mode or normal real-mode registers used in 16-bit protected mode?
		{
			GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 18, debuggerrow++); //Second debug row!
			GPU_textprintf(frameratesurface, fontcolor, backcolor, "P: CS:IP %04X:%04X", CPU[activeCPU].exec_lastCS, CPU[activeCPU].exec_lastEIP); //Debug CS:IP!
		}
		else //386+?
		{
			if (EMULATED_CPU>=CPU_80386) //32-bit CPU?
			{
				GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 23, debuggerrow++); //Second debug row!
				GPU_textprintf(frameratesurface, fontcolor, backcolor, "P: CS:EIP %04X:%08X", CPU[activeCPU].exec_lastCS, CPU[activeCPU].exec_lastEIP); //Debug IP!
			}
			else //286-?
			{
				GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 19, debuggerrow++); //Second debug row!
				GPU_textprintf(frameratesurface, fontcolor, backcolor, "P: CS:IP %04X:%04X", CPU[activeCPU].exec_lastCS, CPU[activeCPU].exec_lastEIP); //Debug IP!
			}
		}

		//Now: Rest segments!
		GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 16, debuggerrow++); //Second debug row!
		GPU_textprintf(frameratesurface, fontcolor, backcolor, "DS:%04X; ES:%04X", REGD_DS(debuggerregisters), REGD_ES(debuggerregisters)); //Debug DS&ES!
		GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 7, debuggerrow++); //Second debug row!
		GPU_textprintf(frameratesurface, fontcolor, backcolor, "SS:%04X", REGD_SS(debuggerregisters)); //Debug SS!
		if (EMULATED_CPU >= CPU_80386) //386+ has more plain segment registers(F segment and G segment)?
		{
			GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 16, debuggerrow++); //Second debug row!
			GPU_textprintf(frameratesurface, fontcolor, backcolor, "FS:%04X; GS:%04X", REGD_FS(debuggerregisters), REGD_GS(debuggerregisters)); //Debug FS&GS!
		}
		if (EMULATED_CPU >= CPU_80286) //Protected mode-capable CPU has Task and Local Descriptor Table registers?
		{
			GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 18, debuggerrow++); //Second debug row!
			GPU_textprintf(frameratesurface, fontcolor, backcolor, "TR:%04X; LDTR:%04X", REGD_TR(debuggerregisters), REGD_LDTR(debuggerregisters)); //Debug TR&LDTR!
		}

		//General purpose registers!
		if (EMULATED_CPU<=CPU_80286) //Real mode, virtual 8086 mode or normal real-mode registers used in 16-bit protected mode?
		{
			//General purpose registers!
			GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 17, debuggerrow++); //Second debug row!
			GPU_textprintf(frameratesurface, fontcolor, backcolor, "AX:%04X; BX: %04X", REGD_AX(debuggerregisters), REGD_BX(debuggerregisters)); //Debug AX&BX!
			GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 17, debuggerrow++); //Second debug row!
			GPU_textprintf(frameratesurface, fontcolor, backcolor, "CX:%04X; DX: %04X", REGD_CX(debuggerregisters), REGD_DX(debuggerregisters)); //Debug CX&DX!

			//Pointers and indexes!

			GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 17, debuggerrow++); //Second debug row!
			GPU_textprintf(frameratesurface, fontcolor, backcolor, "SP:%04X; BP: %04X", REGD_SP(debuggerregisters), REGD_BP(debuggerregisters)); //Debug SP&BP!
			GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 17, debuggerrow++); //Second debug row!
			GPU_textprintf(frameratesurface, fontcolor, backcolor, "SI:%04X; DI: %04X", REGD_SI(debuggerregisters), REGD_DI(debuggerregisters)); //Debug SI&DI!

			if (EMULATED_CPU>=CPU_80286) //We have an extra register?
			{
				//Control registers!
				GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 8, debuggerrow++); //Second debug row!
				GPU_textprintf(frameratesurface, fontcolor, backcolor, "CR0:%04X", (debuggerregisters.CR0&0xFFFF)); //Debug CR0!
			}
		}
		else //386+?
		{
			//General purpose registers!
			GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 27, debuggerrow++); //Second debug row!
			GPU_textprintf(frameratesurface, fontcolor, backcolor, "EAX:%08X; EBX: %08X", REGD_EAX(debuggerregisters), REGD_EBX(debuggerregisters)); //Debug EAX&EBX!
			GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 27, debuggerrow++); //Second debug row!
			GPU_textprintf(frameratesurface, fontcolor, backcolor, "ECX:%08X; EDX: %08X", REGD_ECX(debuggerregisters), REGD_EDX(debuggerregisters)); //Debug ECX&EDX!

			//Pointers and indexes!

			GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 27, debuggerrow++); //Second debug row!
			GPU_textprintf(frameratesurface, fontcolor, backcolor, "ESP:%08X; EBP: %08X", REGD_ESP(debuggerregisters), REGD_EBP(debuggerregisters)); //Debug ESP&EBP!
			GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 27, debuggerrow++); //Second debug row!
			GPU_textprintf(frameratesurface, fontcolor, backcolor, "ESI:%08X; EDI: %08X", REGD_ESI(debuggerregisters), REGD_EDI(debuggerregisters)); //Debug ESI&EDI!

			//Control Registers!
			GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 27, debuggerrow++); //Second debug row!
			GPU_textprintf(frameratesurface, fontcolor, backcolor, "CR0:%08X; CR1:%08X", debuggerregisters.CR0, debuggerregisters.CR1); //Debug CR0&CR1!
			GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 27, debuggerrow++); //Second debug row!
			GPU_textprintf(frameratesurface, fontcolor, backcolor, "CR2:%08X; CR3:%08X", debuggerregisters.CR2, debuggerregisters.CR3); //Debug CR2&CR3!
			GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 27, debuggerrow++); //Second debug row!
			GPU_textprintf(frameratesurface, fontcolor, backcolor, "CR4:%08X; CR5:%08X", debuggerregisters.CR4, debuggerregisters.CR5); //Debug CR4&CR5!
			GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 27, debuggerrow++); //Second debug row!
			GPU_textprintf(frameratesurface, fontcolor, backcolor, "CR6:%08X; CR7:%08X", debuggerregisters.CR6, debuggerregisters.CR7); //Debug CR6&CR7!

			//Debugger registers!
			GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 27, debuggerrow++); //Second debug row!
			GPU_textprintf(frameratesurface, fontcolor, backcolor, "DR0:%08X; DR1:%08X", debuggerregisters.DR[0], debuggerregisters.DR[1]); //Debug DR0&DR1!
			GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 27, debuggerrow++); //Second debug row!
			GPU_textprintf(frameratesurface, fontcolor, backcolor, "DR2:%08X; DR3:%08X", debuggerregisters.DR[2], debuggerregisters.DR[3]); //Debug DR2&DR3!
			if (EMULATED_CPU<CPU_80486) //DR4=>DR6?
			{
				GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 27, debuggerrow++); //Second debug row!
				GPU_textprintf(frameratesurface, fontcolor, backcolor, "DR6:%08X; DR7:%08X", debuggerregisters.DR[4], debuggerregisters.DR[5]); //Debug DR4/6&DR5/7!
			}
			else //DR4 and DR6 are seperated and both implemented on 80486+!
			{
				GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 42, debuggerrow++); //Second debug row!
				GPU_textprintf(frameratesurface, fontcolor, backcolor, "DR4:%08X; DR6:%08X DR7:%08X", debuggerregisters.DR[4], debuggerregisters.DR[6], debuggerregisters.DR[7]); //Debug DR4/6&DR5/7!
			}
		}

		if (EMULATED_CPU>=CPU_80286) //We have extra registers in all modes?
		{
			GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 44, debuggerrow++); //Second debug row!
			GPU_textprintf(frameratesurface, fontcolor, backcolor, "GDTR:" LONGLONGSPRINTX "; IDTR:" LONGLONGSPRINTX, (LONG64SPRINTF)debuggerregisters.GDTR.data, (LONG64SPRINTF)debuggerregisters.IDTR.data); //Debug GDTR&IDTR!
		}

		//Finally, the flags!
		//First, flags fully...
		if (EMULATED_CPU <= CPU_80286) //Real mode, virtual 8086 mode or normal real-mode registers used in 16-bit protected mode? 80386 virtual 8086 mode uses 32-bit flags!
		{
			GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 7, debuggerrow++); //Second debug row!
			GPU_textprintf(frameratesurface, fontcolor, backcolor, "F :%04X", REGD_FLAGS(debuggerregisters)); //Debug FLAGS!
		}
		else //386+
		{
			GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 11, debuggerrow++); //Second debug row!
			GPU_textprintf(frameratesurface, fontcolor, backcolor, "F :%08X", REGD_EFLAGS(debuggerregisters)); //Debug FLAGS!
		}

		//Finally, flags seperated!
		char *theflags = debugger_generateFlags(&debuggerregisters); //Generate the flags as text!
		GPU_textgotoxy(frameratesurface, (GPU_TEXTSURFACE_WIDTH - safestrlen(theflags,256)) - 1, debuggerrow++); //Second flags row! Reserve one for our special HLT flag!
		GPU_textprintf(frameratesurface, fontcolor, backcolor, "%s%c", theflags, decodeHLTreset(debuggerHLT,debuggerReset)); //All flags, seperated!

		//Full interrupt status!
		GPU_textgotoxy(frameratesurface,GPU_TEXTSURFACE_WIDTH-18,debuggerrow++); //Interrupt status!
		GPU_textprintf(frameratesurface,fontcolor,backcolor,"R:"); //Show the interrupt request status!
		for (i = 0xF;i >= 0;i--) //All 16 interrupt flags!
		{
			GPU_textprintf(frameratesurface,fontcolor,backcolor,"%u",(i8259.irr[(i&8)>>3]>>(i&7))&1); //Show the interrupt status!
		}
		GPU_textgotoxy(frameratesurface,GPU_TEXTSURFACE_WIDTH-18,debuggerrow++); //Interrupt status!
		GPU_textprintf(frameratesurface,fontcolor,backcolor,"M:"); //Show the interrupt mask!
		for (i = 0xF;i >= 0;i--) //All 16 interrupt flags!
		{
			GPU_textprintf(frameratesurface,fontcolor,backcolor,"%u",(i8259.imr[(i&8)>>3]>>(i&7))&1); //Show the interrupt status!
		}

		if (memprotect(getActiveVGA(),sizeof(VGA_Type),"VGA_Struct")) //Gotten an active VGA?
		{
			GPU_textgotoxy(frameratesurface,GPU_TEXTSURFACE_WIDTH-52,debuggerrow++); //CRT status!
			GPU_textprintf(frameratesurface,fontcolor,backcolor,"VGA@%05i,%05i(CRT:%05i,%05i) Display=%05i,%05i",((SEQ_DATA *)getActiveVGA()->Sequencer)->x,((SEQ_DATA *)getActiveVGA()->Sequencer)->Scanline,getActiveVGA()->CRTC.x,getActiveVGA()->CRTC.y,GPU.xres, GPU.yres);
		}

		debugger_printdescriptors(); //Print the segment descriptors on the screen!

		GPU_text_releasesurface(frameratesurface); //Unlock!
	}
}

byte debugger_updatememoryviewer()
{
	int xdirection, ydirection; //Direction to move in!
	if (psp_keypressed(BUTTON_CROSS)) //Cross pressed?
	{
		while (psp_keypressed(BUTTON_CROSS)) //Wait for release!
		{
			unlock(LOCK_INPUT);
			delay(0);
			lock(LOCK_INPUT);
		}
		debugger_memoryviewer.enabled = 0; //Disable the memory viewer and return to the normal interface for the debugger!
		//Special drawing: remove our text from the debugger!
		GPU_text_locksurface(frameratesurface); //Lock!
		GPU_textclearscreen(frameratesurface); //Clear the screen!
		//Finished drawing. Finish up and don't draw the debugger!
		GPU_text_releasesurface(frameratesurface); //Unlock!
		return 1; //Terminated!
	}
	xdirection = ydirection = 0; //Default: no movement!
	if (psp_keypressed(BUTTON_LEFT)) //Left?
	{
		while (psp_keypressed(BUTTON_LEFT)) //Wait for release?
		{
			unlock(LOCK_INPUT);
			delay(0);
			lock(LOCK_INPUT);
		}
		unlock(LOCK_INPUT);
		delay(0);
		lock(LOCK_INPUT);
		xdirection = -1; //-1 x direction!
	}
	if (psp_keypressed(BUTTON_RIGHT)) //Left?
	{
		while (psp_keypressed(BUTTON_RIGHT)) //Wait for release?
		{
			unlock(LOCK_INPUT);
			delay(0);
			lock(LOCK_INPUT);
		}
		unlock(LOCK_INPUT);
		delay(0);
		lock(LOCK_INPUT);
		xdirection = 1; //+1 x direction!
	}
	if (psp_keypressed(BUTTON_UP)) //Left?
	{
		while (psp_keypressed(BUTTON_UP)) //Wait for release?
		{
			unlock(LOCK_INPUT);
			delay(0);
			lock(LOCK_INPUT);
		}
		unlock(LOCK_INPUT);
		delay(0);
		lock(LOCK_INPUT);
		ydirection = -1; //-1 y direction!
	}
	if (psp_keypressed(BUTTON_DOWN)) //Left?
	{
		while (psp_keypressed(BUTTON_DOWN)) //Wait for release?
		{
			unlock(LOCK_INPUT);
			delay(0);
			lock(LOCK_INPUT);
		}
		unlock(LOCK_INPUT);
		delay(0);
		lock(LOCK_INPUT);
		ydirection = 1; //+1 y direction!
	}
	if (xdirection || ydirection) //Movement requested?
	{
		if (xdirection < 0) //Negative X?
		{
			if ((((int)debugger_memoryviewer.x) + xdirection) >= 0) //Valid to apply?
			{
				debugger_memoryviewer.x = xdirection+(int)debugger_memoryviewer.x; //Apply!
			}
		}
		else if (xdirection > 0) //Positive X?
		{
			debugger_memoryviewer.x = LIMITRANGE(xdirection + (int)debugger_memoryviewer.x, 0, 0xF); //Limit the range to the display!
		}
		if (ydirection < 0) //Negative Y?
		{
			if ((((int)debugger_memoryviewer.y) + ydirection) >= 0) //Valid to apply?
			{
				debugger_memoryviewer.y = ydirection + (int)debugger_memoryviewer.y; //Apply!
			}
		}
		else if (ydirection > 0) //Positive X?
		{
			debugger_memoryviewer.y = LIMITRANGE(ydirection + (int)debugger_memoryviewer.y, 0, 0xF); //Limit the range to the display!
		}
		return 1; //Update the memory viewer!
	}
	return 0; //Not done yet!
}

extern byte Settings_request; //Settings requested to be executed?
extern byte reset; //To reset the emulator?

void debugger_startmemoryviewer(char* breakpointstr, byte enabled, byte virtualmemory, byte virtualmemoryCPL)
{
	uint_32 offset;
	offset = converthex2int(&breakpointstr[0]); //Convert the number to our usable format!

	//Apply the new viewer!
	debugger_memoryviewer.virtualmemory = virtualmemory; //Use virtual memory instead?
	debugger_memoryviewer.virtualmemoryCPL = virtualmemoryCPL; //The CPL to use for the virtual memory!
	debugger_memoryviewer.enabled = enabled; //Enabled?
	debugger_memoryviewer.address = (uint_64)(offset & 0xFFFFFFFFULL); //Set the new breakpoint!
	debugger_memoryviewer.x = 0; //Init!
	debugger_memoryviewer.y = 0; //Init!
}


byte debugger_memoryviewerMode(char* breakpointstr); //Prototype for debugger_memoryvieweraddress as a second input step.
byte debugger_memoryvieweraddress()
{
	byte result;
	char breakpointstr[256]; //32-bits offset, colon, 16-bits segment, mode if required(Protected/Virtual 8086), Ignore EIP/CS/Whole address(mode only) and final character(always zero)!
	cleardata(&breakpointstr[0], sizeof(breakpointstr));
	//First, convert the current breakpoint to a string format!
	BIOSClearScreen(); //Clear the screen!
	BIOS_Title("Memory Breakpoint"); //Full clear!
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto position for info!
	GPU_EMU_printscreen(0, 4, "Address: "); //Show the filename!
	EMU_unlocktext();
	word maxoffsetsize = 8;
	result = 0; //Default: not handled!
	unlock(LOCK_INPUT); //Make sure the input isn't locked!
	if (BIOS_InputAddressWithMode(9, 4, &breakpointstr[0], sizeof(breakpointstr) - 1, 0, 0, 0)) //Input text confirmed?
	{
		if (strcmp(breakpointstr, "") != 0) //Got valid input?
		{
			//Convert the string back into our valid numbers for storage!
			//This won't compile on the PSP for some unknown reason, crashing the compiler!
			if (((safe_strlen(&breakpointstr[0], sizeof(breakpointstr))) - 1) <= maxoffsetsize) //Offset OK?
			{
				result = debugger_memoryviewerMode(&breakpointstr[0]); //Take input from the second step: the privilege level choice!
			}
		}
	}
	lock(LOCK_INPUT); //Relock!
	BIOSDoneScreen(); //Clear the screen after we're done with it!
	return result; //Give the result!
}

byte debugger_memoryviewerMode(char *breakpointstr)
{
	byte result;
	//First, convert the current breakpoint to a string format!
	BIOSClearScreen(); //Clear the screen!
	BIOS_Title("Memory Breakpoint"); //Full clear!
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto position for info!
	GPU_EMU_printscreen(0, 4, "Cross=Virtual memory (instr), Square=Virtual memory (kernel)\nTriangle=Physical, Circle=Cancel"); //Show the filename!
	EMU_unlocktext();
	result = 0; //Default: not handled!
memoryviewerModeinputloop:
	if (shuttingdown()) return 0; //Stop debugging when shutting down!
	lock(LOCK_INPUT);
	if (psp_keypressed(BUTTON_SQUARE)) //Square pressed?
	{
		while (psp_keypressed(BUTTON_SQUARE)) //Wait for release!
		{
			unlock(LOCK_INPUT);
			delay(0);
			lock(LOCK_INPUT);
		}
		unlock(LOCK_INPUT); //Unlock!
		debugger_startmemoryviewer(&breakpointstr[0], 1, 1, 0); //Start the virtual memory viewer interface with kernel privilege!
		result = 1; //Started!
		goto loopfinished; //Finish the loop!
	}
	else if (psp_keypressed(BUTTON_CROSS)) //Cross pressed?
	{
		while (psp_keypressed(BUTTON_CROSS)) //Wait for release!
		{
			unlock(LOCK_INPUT);
			delay(0);
			lock(LOCK_INPUT);
		}
		unlock(LOCK_INPUT); //Unlock!
		debugger_startmemoryviewer(&breakpointstr[0], 1, 1, debuggerCPL); //Start the virtual memory viewer interface with debugged instruction privilege!
		result = 1; //Started!
		goto loopfinished; //Finish the loop!
	}
	else if (psp_keypressed(BUTTON_TRIANGLE)) //Cross pressed?
	{
		while (psp_keypressed(BUTTON_TRIANGLE)) //Wait for release!
		{
			unlock(LOCK_INPUT);
			delay(0);
			lock(LOCK_INPUT);
		}
		unlock(LOCK_INPUT); //Unlock!
		debugger_startmemoryviewer(&breakpointstr[0], 1, 0, 0); //Start the physical memory viewer interface!
		result = 1; //Started!
		goto loopfinished; //Finish the loop!
	}
	else if (psp_keypressed(BUTTON_CIRCLE)) //Cross pressed?
	{
		while (psp_keypressed(BUTTON_CIRCLE)) //Wait for release!
		{
			unlock(LOCK_INPUT);
			delay(0);
			lock(LOCK_INPUT);
		}
		unlock(LOCK_INPUT); //Unlock!
		result = 0; //Aborted!
		goto loopfinished; //Finish the loop!
	}

	unlock(LOCK_INPUT); //Unlock!
	goto memoryviewerModeinputloop; //Check again until receiving input!
	loopfinished:
	BIOSDoneScreen(); //Clear the screen after we're done with it!
	return result; //Give the result!
}

void debuggerThread()
{
	byte openBIOS = 0;
	int done = 0;
	byte displayed = 0; //Are we displayed?
	pauseEMU(); //Pause it!
	debugger_memoryviewer.enabled = 0; //Default: not using the memory viewer interface when starting up the debugger!

	restartdebugger: //Restart the debugger during debugging!
	done = 0; //Init: not done yet!
	if (shuttingdown()) return; //Stop debugging when shutting down!
	if ((!(done || skipopcodes || (skipstep&&CPU[activeCPU].repeating)) || (BPsinglestep==1)) || (displayed==0)) //Are we to show the (new) debugger screen?
	{
		displayed = 1; //We're displayed!
		lock(LOCK_MAINTHREAD); //Lock the main thread!
		debugger_screen(); //Show debugger info on-screen!
		unlock(LOCK_MAINTHREAD); //Finished with the main thread!
	}

	lock(LOCK_INPUT);
	for (; !(done || skipopcodes || (skipstep && CPU[activeCPU].repeating));) //Still not done or skipping?
	{
		if (debugger_memoryviewer.enabled == 0) //Normal debugger?
		{
			if (DEBUGGER_ALWAYS_STEP || ((singlestep == 1) || (BPsinglestep == 1))) //Always step?
			{
				//We're going though like a normal STEP. Ignore RTRIGGER.
			}
			else if (DEBUGGER_KEEP_RUNNING) //Always keep running?
			{
				done = 1; //Keep running!
			}
			else
			{
				done = (!psp_keypressed(BUTTON_RTRIGGER)); //Continue when release hold (except when forcing stepping), singlestep prevents this!
			}

			if (psp_keypressed(BUTTON_CROSS)) //Step (wait for release and break)?
			{
				while (psp_keypressed(BUTTON_CROSS)) //Wait for release!
				{
					unlock(LOCK_INPUT);
					delay(0);
					lock(LOCK_INPUT);
				}
				singlestep = BPsinglestep = 0; //If single stepping, stop doing so!
				break;
			}
			else if (psp_keypressed(BUTTON_TRIANGLE)) //Might Dump memory?
			{
				while (psp_keypressed(BUTTON_TRIANGLE)) //Wait for release!
				{
					unlock(LOCK_INPUT);
					delay(0);
					lock(LOCK_INPUT);
				}
				if (psp_keypressed(BUTTON_CIRCLE)) //Memory dump?
				{
					MMU_dumpmemory("memory.dat"); //Dump the MMU memory!
				}
				else if (psp_keypressed(BUTTON_SQUARE)) //Memory viewer
				{
					while (psp_keypressed(BUTTON_SQUARE)) //Wait for release!
					{
						unlock(LOCK_INPUT);
						delay(0);
						lock(LOCK_INPUT);
					}
					if (debugger_memoryvieweraddress()) //Input the address for use with the memory viewer!
					{
						//Viewer has been started!
						unlock(LOCK_INPUT);
						displayed = 0; //Not displayed yet!
						goto restartdebugger; //Update us!
					}
					unlock(LOCK_INPUT);
					displayed = 0; //Not displayed yet!
					goto restartdebugger; //Update us!
				}
				else //Skip 10 commands?
				{
					skipopcodes = 9; //Skip 9 additional opcodes!
					singlestep = 0; //If single stepping, stop doing so!
					BPsinglestep = 0; //If single stepping, stop doing so!
					break;
				}
			}
			else if (psp_keypressed(BUTTON_SQUARE)) //Skip until finished command?
			{
				while (psp_keypressed(BUTTON_SQUARE)) //Wait for release!
				{
					unlock(LOCK_INPUT);
					delay(0);
					lock(LOCK_INPUT);
				}
				skipopcodes_destEIP = CPU[activeCPU].nextEIP; //Destination instruction position!
				skipopcodes_destCS = CPU[activeCPU].nextCS; //Destination instruction position!
				if (getcpumode() != CPU_MODE_PROTECTED) //Not protected mode?
				{
					skipopcodes_destEIP &= 0xFFFF; //Wrap around, like we need to!
				}
				if (psp_keypressed(BUTTON_CIRCLE) && (CPU[activeCPU].repeating == 0)) //Wait for the jump to be taken from the current address?
				{
					skipopcodes_destEIP = REGD_EIP(debuggerregisters); //We're jumping from this address!
					skipopcodes_destCS = REGD_CS(debuggerregisters); //We're jumping from this address!
					skipstep = 4;
				}
				else //Normal behaviour?
				{
					if (CPU[activeCPU].repeating) //Are we repeating?
					{
						skipstep = 1; //Skip all REP additional opcodes!
					}
					else //Use the supplied EIP!
					{
						skipstep = 2; //Simply skip until the next instruction is reached after this address!
					}
				}
				BPsinglestep = 0; //Stop breakpoint single step when this is used!
				break;
			}
			unlock(LOCK_INPUT);
			openBIOS = 0; //Init!
			lock(LOCK_MAINTHREAD); //We're checking some input!
			openBIOS = Settings_request;
			Settings_request = 0; //We're handling it if needed!
			unlock(LOCK_MAINTHREAD); //We've finished checking for input!
			lock(LOCK_INPUT);
			openBIOS |= psp_keypressed(BUTTON_SELECT); //Are we to open the BIOS menu?
			if (openBIOS && !is_gamingmode()) //Goto BIOS?
			{
				while (psp_keypressed(BUTTON_SELECT)) //Wait for release when pressed!
				{
					unlock(LOCK_INPUT);
					delay(0);
					lock(LOCK_INPUT);
				}
				unlock(LOCK_INPUT);
				//Start the BIOS
				if (runBIOS(0)) //Run the BIOS, reboot needed?
				{
					skipopcodes = 0; //Nothing to be skipped!
					BPsinglestep = 0; //Nothing to break on!
					lock(LOCK_MAINTHREAD);
					reset = 1; //We're resetting!
					allow_debuggerstep = 0;
					unlock(LOCK_MAINTHREAD);
					goto singlestepenabled; //We're rebooting, abort!
				}
				//Check the current state to continue at!
				if (debugging()) //Recheck the debugger!
				{
					goto restartdebugger; //Restart the debugger!
				}
				else //Not debugging anymore?
				{
					goto singlestepenabled; //Stop debugging!
				}
			}
		} //While not done
		else //Memory viewer interface?
		{
			if (debugger_updatememoryviewer()) //Update the screen?
			{
				unlock(LOCK_INPUT);
				goto restartdebugger; //Update us!
			}
		}
		//Make sure we give the main thread time to run as well as support terminating the app!
		unlock(LOCK_INPUT);
		if (shuttingdown()) break; //Stop debugging when shutting down!
		delay(0); //Wait a bit!
		lock(LOCK_INPUT);
	}
	unlock(LOCK_INPUT);
	singlestepenabled: //Single step has been enabled just now?
	if (displayed) //Are we to clean up?
	{
		lock(LOCK_MAINTHREAD); //Make sure we aren't cleaning up!
		debugger_is_logging = debugger_logging(); //Are we logging?
		GPU_text_locksurface(frameratesurface); //Lock!
		//for (i = GPU_TEXT_DEBUGGERROW;i < debuggerrow;i++) GPU_textclearrow(frameratesurface, i); //Clear our debugger rows!
		GPU_textclearscreen(frameratesurface); //Clear the screen!
		GPU_text_releasesurface(frameratesurface); //Unlock!
		unlock(LOCK_MAINTHREAD); //Finished!
	}
	resumeEMU(1); //Resume it!
}

ThreadParams_p debugger_thread = NULL; //The debugger thread, if any!
extern ThreadParams_p BIOSMenuThread; //BIOS pause menu thread!

byte debugger_isrunning()
{
	if (unlikely(debugger_thread)) //Debugger not running yet?
	{
		if (threadRunning(debugger_thread)) //Still running?
		{
			return 1; //We're still running, so start nothing!
		}
	}
	return 0; //Not running!
}

void debugger_step() //Processes the debugging step!
{
	if (activeCPU) return; //Only with CPU #0!
	if (unlikely(debugger_thread)) //Debugger not running yet?
	{
		if (threadRunning(debugger_thread)) //Still running?
		{
			return; //We're still running, so start nothing!
		}
	}
	debugger_simplifiedlog = ((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_SINGLELINE_SIMPLIFIED) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_SINGLELINE_SIMPLIFIED) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT)); //Simplified log?
	debugger_loggingtimestamp = ((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT))?0:1; //Are we to log time too? Not with common log format!
	debugger_thread = NULL; //Not a running thread!
	debugger_autolog(); //Log when enabled!
	if (unlikely(CPU[activeCPU].executed)) //Are we executed?
	{
		if (unlikely(debugging())) //Debugging step or single step enforced?
		{
			if (unlikely(shuttingdown())) return; //Don't when shutting down!
			if (unlikely(skipstep)) //Finished?
			{
				if (!CPU[activeCPU].repeating && (skipstep==1)) //Finished repeating?
				{
					skipstep = 0; //Disable skip step!
				}
				else if ((REGD_EIP(debuggerregisters) == skipopcodes_destEIP) && (REGD_CS(debuggerregisters) == skipopcodes_destCS)) //We've reached the destination address?
				{
					if ((skipstep==4) && CPU[activeCPU].didJump) //Jumped at our specified step?
					{
						skipstep = 0; //We're finished!
					}
					else if (skipstep!=4) //Normal finish not requiring Jump to be executed?
					{
						skipstep = 0; //We're finished!
					}
				}
				else if (skipstep==3) //Stop immediately?
				{
					skipstep = 0; //We're finished!
				}
			}
			if (unlikely(skipopcodes)) //Skipping?
			{
				--skipopcodes; //Skipped one opcode!
			}
			if (unlikely((!(skipopcodes || ((skipstep==1)&&CPU[activeCPU].repeating) || (skipstep==2))) && (skipstep!=4))) //To debug when not skipping repeating or skipping opcodes?
			{
				if (unlikely((!(DEBUGGER_KEEP_NOSHOW_RUNNING)) || (BPsinglestep==1))) //Are we to show the debugger at all(not explicitly disabled)?
				{
					if ((BIOSMenuThread==NULL) && (reset==0)) //These are mutually exclusive to run!
					{
						debugger_is_logging = 0; //Disable logging from now on!
						debugger_thread = startThread(debuggerThread, "UniPCemu_debugger", NULL); //Start the debugger!
					}
				}
				else if (unlikely(DEBUGGER_KEEP_NOSHOW_RUNNING && (singlestep==1))) //To stop anyway?
				{
					singlestep = 0; //We're finishing the single step anyway, ignoring!
				}
			}
		} //Step mode?
		if (unlikely(singlestep>1)) //Start single-stepping from the next instruction?
		{
			--singlestep; //Start single-stepping the next X instruction!
		}
	}
	#ifdef DEBUG_PROTECTEDMODE
	harddebugging = (getcpumode()!=CPU_MODE_REAL); //Protected/V86 mode forced debugging log to start/stop? Don't include the real mode this way(as it's already disabled after execution), do include the final instruction, leaving protected mode this way(as it's already handled).
	#endif
}

void debugger_setcommand(char *text, ...)
{
	char *c;
	if (activeCPU) return; //Only CPU #0!
	if (CPU[activeCPU].cpudebugger && (debugger_set==0)) //Are we debugging?
	{
		va_list args; //Going to contain the list!
		va_start (args, text); //Start list!
		vsnprintf (&debugger_command_text[0],sizeof(debugger_command_text), text, args); //Compile list!
		va_end (args); //Destroy list!
		debugger_set = 1; //We've set the debugger!
		if ((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT)) //Common log format?
		{
			c = &debugger_command_text[0]; //Process it first!
			for (;*c;)
			{
				if ((*c>='A') && (*c<='Z')) //Capital?
				{
					*c -= (int)'A'; //Decrease!
					*c += (int)'a'; //Convert to lower case!
				}
				++c; //Next character!
			}
		}
	}
}

void debugger_setprefix(char *text)
{
	char *c;
	if (activeCPU) return; //Only CPU #0!
	if ((debugger_prefix[0]=='\0') || (*text=='\0')) //No prefix yet or reset?
	{
		safestrcpy(debugger_prefix,sizeof(debugger_prefix),text); //Set prefix!
		if (*text!='\0') //Not reset?
		{
			safestrcat(debugger_prefix,sizeof(debugger_prefix), " "); //Prefix seperator!
		}
	}
	else
	{
		safestrcat(debugger_prefix,sizeof(debugger_prefix), text); //Add prefix!
		safestrcat(debugger_prefix,sizeof(debugger_prefix), " "); //Prefix seperator!
	}
	if ((DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT) || (DEBUGGER_LOG==DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT)) //Common log format?
	{
		c = &debugger_prefix[0]; //Process it first!
		for (;*c;)
		{
			if ((*c>='A') && (*c<='Z')) //Capital?
			{
				*c -= (int)'A'; //Decrease!
				*c += (int)'a'; //Convert to lower case!
			}
			++c; //Next character!
		}
	}
}

void initDebugger() //Initialize the debugger if needed!
{
	verifyfile = file_exists("debuggerverify16.dat"); //To perform verification checks at all?
	memset(&flags,0,sizeof(flags)); //Clear/init flags!
	memset(&executedinstruction,0,sizeof(executedinstruction)); //Init instruction!
	memset(&statelog,0,sizeof(statelog));
	memset(&debugger_memoryaccess_text,0,sizeof(debugger_memoryaccess_text));
	memset(&debugger_memoryaccess_line,0,sizeof(debugger_memoryaccess_line));
	memset(&executedinstructionstatelog,0,sizeof(executedinstructionstatelog));

	//Debugger skipping functionality(requires to be reset when initializing the emulator)
	skipopcodes = 0; //Skip none!
	skipstep = 0; //Skip while stepping? 1=repeating, 2=EIP destination, 3=Stop asap.
	skipopcodes_destEIP = 0; //Wait for EIP to become this value?

	//Repeat log?
	forcerepeat = 0; //Force repeat log?

	debugger_index = 0; //Current debugger index is to be initialized!

	debugger_logtimings = 1; //Are we to log the full timings of hardware and CPU as well?

	singlestep = 0; //Enforce single step by CPU/hardware special debugging effects? 0=Don't step, 1=Step this instruction(invalid state when activated during the execution of the instruction), 2+ step next instruction etc.
	BPsinglestep = 0; //Enforce single step by breakpoint once!

	debuggerHLT = 0; //We're assuming CPU reset, so not halting!
	debuggerReset = 1; //Are we a reset CPU(we assume so)?
	debugger_instructionexecuting = 0; //Not yet executing!
}
