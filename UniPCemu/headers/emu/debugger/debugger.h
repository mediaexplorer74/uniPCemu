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

#ifndef DEBUGGER_H
#define DEBUGGER_H

#include "headers/bios/bios.h" //BIOS support!
#include "headers/cpu/modrm.h" //Modrm support!
#include "headers/cpu/cpu.h" //CPU support!

//BIOS Settings
#ifndef BIOS_Settings
extern BIOS_Settings_TYPE BIOS_Settings; //BIOS Settings!
#endif

//Debugger enabled?
#define DEBUGGER_ENABLED BIOS_Settings.debugmode
//Always stepwise check, also ignoring RTRIGGER for STEP?
#define DEBUGGER_ALWAYS_STEP (BIOS_Settings.debugmode==DEBUGMODE_STEP)
//Always keep running, ignoring X for continuing step?
#define DEBUGGER_KEEP_NOSHOW_RUNNING (BIOS_Settings.debugmode==DEBUGMODE_NOSHOW_RUN)
//Always keep running, ignoring X for continuing step?
#define DEBUGGER_KEEP_RUNNING ((BIOS_Settings.debugmode==DEBUGMODE_SHOW_RUN) || DEBUGGER_KEEP_NOSHOW_RUNNING)
//Always debugger on (ignore LTRIGGER?) Shows the debugger on the screen! Not used when using the noshow keep running setting!
#define DEBUGGER_ALWAYS_DEBUG ((BIOS_Settings.debugmode>1) && (!DEBUGGER_KEEP_NOSHOW_RUNNING))

//Base row of register dump on-screen!
#define DEBUGGER_REGISTERS_BASEROW 1

//Log with debugger?
#define DEBUGGER_LOG BIOS_Settings.debugger_log

//Are we logging states?
#define DEBUGGER_LOGSTATES (BIOS_Settings.debugger_logstates>0)

//Are we logging states?
#define DEBUGGER_LOGREGISTERS (BIOS_Settings.debugger_logregisters>0)

void debugger_step(); //Debugging, if debugging (see below), after the CPU changes it''s registers!
//byte debugging(); //Debugging?
byte debugger_logging(); //Are we logging?
byte needdebugger(); //Do we need to generate debugging information?

//For CPU:
void debugger_beforeCPU(); //Action before the CPU changes it's registers!
void debugger_notifyRunning(); //Notify the debugger we've started running!

void debugger_setcommand(char *text, ...); //Set current command (Opcode only!)
void debugger_setprefix(char *text); //Set prefix (CPU only!)

void modrm_debugger8(MODRM_PARAMS *params, byte whichregister1, byte whichregister2); //8-bit handler!
void modrm_debugger16(MODRM_PARAMS *params, byte whichregister1, byte whichregister2); //16-bit handler!

void debugger_screen(); //On-screen dump of registers etc.

void debugger_logregisters(char *filename, CPU_registers *registers, byte halted, byte isreset);
byte isDebuggingPOSTCodes(); //Debug POST codes?

void initDebugger(); //Initialize the debugger if needed!
void debugger_logmemoryaccess(byte iswrite, uint_64 address, byte value, byte type);

byte debugger_forceEIP(); //Force EIP to be used for debugging?

byte debugger_isrunning(); //Is the debugger running?

//Segmented memory address
#define LOGMEMORYACCESS_NORMAL 0
//Logical memory address
#define LOGMEMORYACCESS_PAGED 1
//Physical memory address
#define LOGMEMORYACCESS_DIRECT 2
//RAM memory address
#define LOGMEMORYACCESS_RAM 3
//Full RAM writeback
#define LOGMEMORYACCESS_RAM_LOGMMUALL 4
//Or'ed with the above values!
#define LOGMEMORYACCESS_PREFETCHBITSHIFT 3
#define LOGMEMORYACCESS_PREFETCH 8


#endif
