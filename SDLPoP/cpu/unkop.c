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

#include "headers/types.h" //Basic type support etc.
#include "headers/cpu/cpu.h" //CPU support!
#include "headers/emu/gpu/gpu.h" //Need GPU comp!
#include "headers/cpu/cpu_OP8086.h" //8086 interrupt instruction support!
#include "headers/emu/debugger/debugger.h" //Debugger support!
#include "headers/cpu/easyregs.h" //Easy register addressing!

#include "headers/emu/timers.h" //Timer support!
#include "headers/support/log.h" //Logging support!
#include "headers/cpu/protection.h" //Fault raising support!
#include "headers/cpu/cpu_execution.h" //Execution interrupt support!

//Shutdown the application when an unknown instruction is executed?
//#define UNKOP_SHUTDOWN

void halt_modrm(char *message, ...) //Unknown modr/m?
{
	stopVideo(); //Need no video!
	stopTimers(0); //Stop all normal timers!
	char buffer[256]; //Going to contain our output data!
	va_list args; //Going to contain the list!
	va_start (args, message); //Start list!
	vsnprintf (&buffer[0],sizeof(buffer), message, args); //Compile list!
	va_end (args); //Destroy list!
	raiseError("modrm","Modr/m error: %s",buffer); //Shut the adress and opcode!
	debugger_screen(); //Show debugger info!
//EMU_Shutdown(1); //Shut down the emulator!
	dosleep(); //Wait forever!
}

extern char debugger_command_text[256]; //Current command!
extern byte debugger_set; //Debugger set?

extern byte advancedlog; //Advanced log setting

extern byte MMU_logging; //Are we logging from the MMU?

//Normal instruction #UD handlers for 80(1)8X+!
void unkOP_8086() //Unknown opcode on 8086?
{
	CPU_unkOP(); //Execute the unknown opcode exception handler, if any!
	memset(&CPU[activeCPU].tempbuf,0,sizeof(CPU[activeCPU].tempbuf)); //Clear buffer!
	if (debugger_set) safestrcpy(CPU[activeCPU].tempbuf,sizeof(CPU[activeCPU].tempbuf),debugger_command_text); //Save our string that's stored!
	#ifdef UNKOP_SHUTDOWN
	dolog("unkOP","Unknown 8086 opcode detected: %02X@%04X:%04X, Previous opcode: %02X(0F:%u)@%04X(Physical %08X):%04X",CPU[activeCPU].currentopcode,CPU[activeCPU].exec_CS,CPU[activeCPU].exec_EIP,CPU[activeCPU].previousopcode,CPU[activeCPU].previousopcode0F,CPU[activeCPU].exec_lastCS,CPU[activeCPU].previousCSstart,CPU[activeCPU].exec_lastEIP); //Log our info!
	dolog("unkOP","Possible cause: %s",debugger_command_text[0]?debugger_command_text:"unknown reason"); //Log the possible reason!
	EMU_Shutdown(1); //Request to shut down!
	#endif
}

void unkOP_186() //Unknown opcode on 186+?
{
	memset(&CPU[activeCPU].tempbuf,0,sizeof(CPU[activeCPU].tempbuf)); //Clear buffer!
	if (debugger_set) safestrcpy(CPU[activeCPU].tempbuf,sizeof(CPU[activeCPU].tempbuf),debugger_command_text); //Save our string that's stored!
	debugger_set = 0; //unset!
	debugger_setcommand("<NECV20/V30+ #UD(Possible cause:%s)>", CPU[activeCPU].tempbuf); //Command is unknown opcode!
	CPU_resetOP(); //Go back to the opcode itself!
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger","#UD fault(-1)!");
	}

	if (CPU_faultraised(EXCEPTION_INVALIDOPCODE))
	{
		CPU_executionphase_startinterrupt(EXCEPTION_INVALIDOPCODE,0,-1); //Call interrupt with return addres of the OPcode!
	}
	CPU[activeCPU].faultraised = 1; //We've raised a fault!
	#ifdef UNKOP_SHUTDOWN
	dolog("unkOP","Unknown opcode detected: %02X@%04X:%08X, Previous opcode: %02X(0F:%u)@%04X(Physical %08X):%08X",CPU[activeCPU].currentopcode,CPU[activeCPU].exec_CS,CPU[activeCPU].exec_EIP,CPU[activeCPU].previousopcode,CPU[activeCPU].previousopcode0F,CPU[activeCPU].exec_lastCS,CPU[activeCPU].previousCSstart,CPU[activeCPU].exec_lastEIP); //Log our info!
	dolog("unkOP","Possible cause: %s",debugger_command_text[0]?debugger_command_text:"unknown reason"); //Log the possible reason!
	EMU_Shutdown(1); //Request to shut down!
	#endif
}

//0F opcode extensions #UD handler
void unkOP0F_286() //0F unknown opcode handler on 286+?
{
	memset(&CPU[activeCPU].tempbuf,0,sizeof(CPU[activeCPU].tempbuf)); //Clear buffer!
	if (debugger_set) safestrcpy(CPU[activeCPU].tempbuf,sizeof(CPU[activeCPU].tempbuf),debugger_command_text); //Save our string that's stored!
	debugger_set = 0; //unset!
	debugger_setcommand("<80286+ 0F #UD(Possible cause:%s)>", CPU[activeCPU].tempbuf); //Command is unknown opcode!
	CPU_resetOP(); //Go back to the opcode itself!
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger","#UD fault(-1)!");
	}

	if (CPU_faultraised(EXCEPTION_INVALIDOPCODE))
	{
		CPU_executionphase_startinterrupt(EXCEPTION_INVALIDOPCODE,0,-1); //Call interrupt!
	}
	CPU[activeCPU].faultraised = 1; //We've raised a fault!
	#ifdef UNKOP_SHUTDOWN
	dolog("unkOP","Unknown 0F opcode detected: %02X@%04X:%08X, Previous opcode: %02X(0F:%u)@%04X(Physical %08X):%08X",CPU[activeCPU].currentopcode,CPU[activeCPU].exec_CS,CPU[activeCPU].exec_EIP,CPU[activeCPU].previousopcode,CPU[activeCPU].previousopcode0F,CPU[activeCPU].exec_lastCS,CPU[activeCPU].previousCSstart,CPU[activeCPU].exec_lastEIP); //Log our info!
	dolog("unkOP","Possible cause: %s",debugger_command_text[0]?debugger_command_text:"unknown reason"); //Log the possible reason!
	EMU_Shutdown(1); //Request to shut down!
	#endif
}

//0F opcode extensions #UD handler
void unkOP0F_386() //0F unknown opcode handler on 386+?
{
	memset(&CPU[activeCPU].tempbuf,0,sizeof(CPU[activeCPU].tempbuf)); //Clear buffer!
	if (debugger_set) safestrcpy(CPU[activeCPU].tempbuf,sizeof(CPU[activeCPU].tempbuf),debugger_command_text); //Save our string that's stored!
	debugger_set = 0; //unset!
	debugger_setcommand("<80386+ 0F #UD(Possible cause:%s)>", CPU[activeCPU].tempbuf); //Command is unknown opcode!
	CPU_resetOP(); //Go back to the opcode itself!
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger","#UD fault(-1)!");
	}

	if (CPU_faultraised(EXCEPTION_INVALIDOPCODE))
	{
		CPU_executionphase_startinterrupt(EXCEPTION_INVALIDOPCODE,0,-1); //Call interrupt!
	}
	CPU[activeCPU].faultraised = 1; //We've raised a fault!
	#ifdef UNKOP_SHUTDOWN
	dolog("unkOP","Unknown 386+ 0F opcode detected: %02X@%04X:%08X, Previous opcode: %02X(0F:%u)@%04X(Physical %08X):%08X",CPU[activeCPU].currentopcode,CPU[activeCPU].exec_CS,CPU[activeCPU].exec_EIP,CPU[activeCPU].previousopcode,CPU[activeCPU].previousopcode0F,CPU[activeCPU].exec_lastCS,CPU[activeCPU].previousCSstart,CPU[activeCPU].exec_lastEIP); //Log our info!
	dolog("unkOP","Possible cause: %s",debugger_command_text[0]?debugger_command_text:"unknown reason"); //Log the possible reason!
	EMU_Shutdown(1); //Request to shut down!
	#endif
}

void unkOP0F_486() //0F unknown opcode handler on 486+?
{
	memset(&CPU[activeCPU].tempbuf,0,sizeof(CPU[activeCPU].tempbuf)); //Clear buffer!
	if (debugger_set) safestrcpy(CPU[activeCPU].tempbuf,sizeof(CPU[activeCPU].tempbuf),debugger_command_text); //Save our string that's stored!
	debugger_set = 0; //unset!
	debugger_setcommand("<80486+ 0F #UD(Possible cause:%s)>", CPU[activeCPU].tempbuf); //Command is unknown opcode!
	CPU_resetOP(); //Go back to the opcode itself!
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger","#UD fault(-1)!");
	}

	if (CPU_faultraised(EXCEPTION_INVALIDOPCODE))
	{
		CPU_executionphase_startinterrupt(EXCEPTION_INVALIDOPCODE,0,-1); //Call interrupt!
	}
	CPU[activeCPU].faultraised = 1; //We've raised a fault!
	#ifdef UNKOP_SHUTDOWN
	dolog("unkOP","Unknown 486+ 0F opcode detected: %02X@%04X:%08X, Previous opcode: %02X(0F:%u)@%04X(Physical %08X):%08X",CPU[activeCPU].currentopcode,CPU[activeCPU].exec_CS,CPU[activeCPU].exec_EIP,CPU[activeCPU].previousopcode,CPU[activeCPU].previousopcode0F,CPU[activeCPU].exec_lastCS,CPU[activeCPU].previousCSstart,CPU[activeCPU].exec_lastEIP); //Log our info!
	dolog("unkOP","Possible cause: %s",debugger_command_text[0]?debugger_command_text:"unknown reason"); //Log the possible reason!
	EMU_Shutdown(1); //Request to shut down!
	#endif
}

void unkOP0F_586() //0F unknown opcode handler on 586+?
{
	memset(&CPU[activeCPU].tempbuf,0,sizeof(CPU[activeCPU].tempbuf)); //Clear buffer!
	if (debugger_set) safestrcpy(CPU[activeCPU].tempbuf,sizeof(CPU[activeCPU].tempbuf),debugger_command_text); //Save our string that's stored!
	debugger_set = 0; //unset!
	debugger_setcommand("<80586+ 0F #UD(Possible cause:%s)>", CPU[activeCPU].tempbuf); //Command is unknown opcode!
	CPU_resetOP(); //Go back to the opcode itself!
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger","#UD fault(-1)!");
	}

	if (CPU_faultraised(EXCEPTION_INVALIDOPCODE))
	{
		CPU_executionphase_startinterrupt(EXCEPTION_INVALIDOPCODE,0,-1); //Call interrupt!
	}
	CPU[activeCPU].faultraised = 1; //We've raised a fault!
	#ifdef UNKOP_SHUTDOWN
	dolog("unkOP","Unknown 586+ 0F opcode detected: %02X@%04X:%08X, Previous opcode: %02X(0F:%u)@%04X(Physical %08X):%08X",CPU[activeCPU].currentopcode,CPU[activeCPU].exec_CS,CPU[activeCPU].exec_EIP,CPU[activeCPU].previousopcode,CPU[activeCPU].previousopcode0F,CPU[activeCPU].exec_lastCS,CPU[activeCPU].previousCSstart,CPU[activeCPU].exec_lastEIP); //Log our info!
	dolog("unkOP","Possible cause: %s",debugger_command_text[0]?debugger_command_text:"unknown reason"); //Log the possible reason!
	EMU_Shutdown(1); //Request to shut down!
	#endif
}
