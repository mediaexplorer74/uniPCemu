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

#include "headers/types.h" //Basic types
#include "headers/cpu/cpu.h" //CPU needed!
#include "headers/cpu/easyregs.h" //Easy register compatibility!
#include "headers/cpu/modrm.h" //MODR/M compatibility!
#include "headers/support/signedness.h" //CPU support functions!
#include "headers/cpu/cpu_OP8086.h" //Our own opcode presets!
#include "headers/cpu/fpu_OP8087.h" //Our own opcode presets!
#include "headers/cpu/flags.h" //Flag support!
#include "headers/cpu/8086_grpOPs.h" //GRP Opcode extensions!
#include "headers/cpu/interrupts.h" //Basic interrupt support!
#include "headers/emu/debugger/debugger.h" //CPU debugger support for INTdebugger8086!
#include "headers/cpu/protection.h"
#include "headers/cpu/biu.h" //BIU support!
#include "headers/cpu/cpu_execution.h" //Execution phase support for interupts etc.!
#include "headers/cpu/cpu_OP80386.h" //For 32-bit ADD for (I)MUL!
#include "headers/support/log.h" //Logging support!
#include "headers/cpu/cpu_pmtimings.h" //Timing support!
#include "headers/cpu/cpu_stack.h" //Stack support!


//How many cycles to substract from the documented instruction timings for the raw EU cycles for each BIU access?
#define EU_CYCLES_SUBSTRACT_ACCESSREAD 4
#define EU_CYCLES_SUBSTRACT_ACCESSWRITE 4
#define EU_CYCLES_SUBSTRACT_ACCESSRW 8

//When using http://www.mlsite.net/8086/: G=Modr/m mod&r/m adress, E=Reg field in modr/m

//INFO: http://www.mlsite.net/8086/
//Extra info about above: Extension opcodes (GRP1 etc) are contained in the modr/m
//Ammount of instructions in the completed core: 123

//Aftercount: 60-6F,C0-C1, C8-C9, D6, D8-DF, F1, 0F(has been implemented anyways)
//Total count: 30 opcodes undefined.

//Info: Ap = 32-bit segment:offset pointer (data: param 1:word segment, param 2:word offset)

//Simplifier!

extern byte debuggerINT; //Interrupt special trigger?

OPTINLINE void INTdebugger8086() //Special INTerrupt debugger!
{
	if (DEBUGGER_LOG==DEBUGGERLOG_INT) //Interrupts only?
	{
		debuggerINT = 1; //Debug this instruction always!
	}
}

/*

Start of help for debugging

*/

OPTINLINE char *getLEAtext(MODRM_PARAMS *theparams)
{
	modrm_lea16_text(theparams,1,&CPU[activeCPU].LEAtext[0]);    //Help function for LEA instruction!
	return &CPU[activeCPU].LEAtext[0];
}

/*

Start of help for opcode processing

*/

extern byte CPU_databussize; //0=16/32-bit bus! 1=8-bit bus when possible (8088/80188)!

OPTINLINE void CPU8086_IRET()
{
	CPUPROT1
	CPU_IRET(); //IRET!
	CPUPROT2
	if (CPU[activeCPU].executed) //Executed?
	{
		if (CPU_apply286cycles()) return; //80286+ cycles instead?
		CPU[activeCPU].cycles_OP += 24; /*Timings!*/
	}
}

/*

List of hardware interrupts:
0: Division by 0: Attempting to execute AAM/DIV/IDIV with divisor==0: IMPLEMENTED
1: Debug/Single step: Breakpoint hit, also after instruction when TRAP flag is set.
3: Breakpoint: INT 3 call: IMPLEMENTED
4: Overflow: When performing arithmetic instructions with signed operands. Called with INTO.
5: Bounds Check: BOUND instruction exceeds limit.
6: Invalid OPCode: Invalid LOCK prefix or invalid OPCode: IMPLEMENTED
7: Device not available: Attempt to use floating point instruction (8087) with no COProcessor.
8: Double fault: Interrupt occurs with no entry in IVT or exception within exception handler.
12: Stack exception: Stack operation exceeds offset FFFFh or a selector pointing to a non-present segment is loaded into SS.
13: CS,DS,ES,FS,GS Segment Overrun: Word memory access at offset FFFFh or an attempt to execute past the end of the code segment.
16: Floating point error: An error with the numeric coprocessor (Divide-by-Zero, Underflow, Overflow...)

*/


//5 Override prefixes! (LOCK, CS, SS, DS, ES)

/*

WE START WITH ALL HELP FUNCTIONS

*/

//First CMP instruction (for debugging) and directly related.

//CMP: Substract and set flags according (Z,S,O,C); Help functions

OPTINLINE void op_adc8()
{
	CPU[activeCPU].res8 = CPU[activeCPU].oper1b + CPU[activeCPU].oper2b + FLAG_CF;
	flag_adc8 (CPU[activeCPU].oper1b, CPU[activeCPU].oper2b, FLAG_CF);
}

OPTINLINE void op_adc16()
{
	CPU[activeCPU].res16 = CPU[activeCPU].oper1 + CPU[activeCPU].oper2 + FLAG_CF;
	flag_adc16 (CPU[activeCPU].oper1, CPU[activeCPU].oper2, FLAG_CF);
}

OPTINLINE void op_add8()
{
	CPU[activeCPU].res8 = CPU[activeCPU].oper1b + CPU[activeCPU].oper2b;
	flag_add8 (CPU[activeCPU].oper1b, CPU[activeCPU].oper2b);
}

OPTINLINE void op_add16()
{
	CPU[activeCPU].res16 = CPU[activeCPU].oper1 + CPU[activeCPU].oper2;
	flag_add16 (CPU[activeCPU].oper1, CPU[activeCPU].oper2);
}

OPTINLINE void op_and8()
{
	CPU[activeCPU].res8 = CPU[activeCPU].oper1b & CPU[activeCPU].oper2b;
	flag_log8 (CPU[activeCPU].res8);
}

OPTINLINE void op_and16()
{
	CPU[activeCPU].res16 = CPU[activeCPU].oper1 & CPU[activeCPU].oper2;
	flag_log16 (CPU[activeCPU].res16);
}

OPTINLINE void op_or8()
{
	CPU[activeCPU].res8 = CPU[activeCPU].oper1b | CPU[activeCPU].oper2b;
	flag_log8 (CPU[activeCPU].res8);
}

OPTINLINE void op_or16()
{
	CPU[activeCPU].res16 = CPU[activeCPU].oper1 | CPU[activeCPU].oper2;
	flag_log16 (CPU[activeCPU].res16);
}

OPTINLINE void op_xor8()
{
	CPU[activeCPU].res8 = CPU[activeCPU].oper1b ^ CPU[activeCPU].oper2b;
	flag_log8 (CPU[activeCPU].res8);
}

OPTINLINE void op_xor16()
{
	CPU[activeCPU].res16 = CPU[activeCPU].oper1 ^ CPU[activeCPU].oper2;
	flag_log16 (CPU[activeCPU].res16);
}

OPTINLINE void op_sub8()
{
	CPU[activeCPU].res8 = CPU[activeCPU].oper1b - CPU[activeCPU].oper2b;
	flag_sub8 (CPU[activeCPU].oper1b, CPU[activeCPU].oper2b);
}

OPTINLINE void op_sub16()
{
	CPU[activeCPU].res16 = CPU[activeCPU].oper1 - CPU[activeCPU].oper2;
	flag_sub16 (CPU[activeCPU].oper1, CPU[activeCPU].oper2);
}

OPTINLINE void op_sbb8()
{
	CPU[activeCPU].res8 = CPU[activeCPU].oper1b - (CPU[activeCPU].oper2b + FLAG_CF);
	flag_sbb8 (CPU[activeCPU].oper1b, CPU[activeCPU].oper2b, FLAG_CF);
}

OPTINLINE void op_sbb16()
{
	CPU[activeCPU].res16 = CPU[activeCPU].oper1 - (CPU[activeCPU].oper2 + FLAG_CF);
	flag_sbb16 (CPU[activeCPU].oper1, CPU[activeCPU].oper2, FLAG_CF);
}

//Stack operation support through the BIU!
byte CPU8086_PUSHw(word base, word *data, byte is32instruction)
{
	word temp;
	if (CPU[activeCPU].instructionstep==base) //First step? Request!
	{
		if (CPU_PUSH16_BIU(data,is32instruction)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		BIU_handleRequests(); //Handle all pending requests at once when to be processed!
		++CPU[activeCPU].instructionstep; //Next step!
	}
	if (CPU[activeCPU].instructionstep==(base+1))
	{
		BIU_handleRequestsPending(); //Handle all pending requests at once when to be processed!
		if (BIU_readResultw(&temp)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].instructionstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU8086_internal_PUSHw(word base, word *data, byte is32instruction)
{
	word temp;
	if (CPU[activeCPU].internalinstructionstep==base) //First step? Request!
	{
		if (CPU_PUSH16_BIU(data,is32instruction)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		BIU_handleRequests(); //Handle all pending requests at once when to be processed!
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	if (CPU[activeCPU].internalinstructionstep==(base+1))
	{
		BIU_handleRequestsPending(); //Handle all pending requests at once when to be processed!
		if (BIU_readResultw(&temp)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU8086_internal_interruptPUSHw(word base, word *data, byte is32instruction)
{
	word temp;
	if (CPU[activeCPU].internalinterruptstep==base) //First step? Request!
	{
		if (CPU_PUSH16_BIU(data,is32instruction)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		BIU_handleRequests(); //Handle all pending requests at once when to be processed!
		++CPU[activeCPU].internalinterruptstep; //Next step!
	}
	if (CPU[activeCPU].internalinterruptstep==(base+1))
	{
		BIU_handleRequestsPending(); //Handle all pending requests at once when to be processed!
		if (BIU_readResultw(&temp)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalinterruptstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU8086_PUSHb(word base, byte *data, byte is32instruction)
{
	byte temp;
	if (CPU[activeCPU].instructionstep==base) //First step? Request!
	{
		if (CPU_PUSH8_BIU(*data,is32instruction)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		BIU_handleRequests(); //Handle all pending requests at once when to be processed!
		++CPU[activeCPU].instructionstep; //Next step!
	}
	if (CPU[activeCPU].instructionstep==(base+1))
	{
		BIU_handleRequestsPending(); //Handle all pending requests at once when to be processed!
		if (BIU_readResultb(&temp)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].instructionstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU8086_internal_PUSHb(word base, byte *data, byte is32instruction)
{
	byte temp;
	if (CPU[activeCPU].internalinstructionstep==base) //First step? Request!
	{
		if (CPU_PUSH8_BIU(*data,is32instruction)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		BIU_handleRequests(); //Handle all pending requests at once when to be processed!
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	if (CPU[activeCPU].internalinstructionstep==(base+1))
	{
		BIU_handleRequestsPending(); //Handle all pending requests at once when to be processed!
		if (BIU_readResultb(&temp)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU8086_POPw(word base, word *result, byte is32instruction)
{
	if (CPU[activeCPU].instructionstep==base) //First step? Request!
	{
		if (CPU_POP16_BIU(is32instruction)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		BIU_handleRequests(); //Handle all pending requests at once when to be processed!
		++CPU[activeCPU].instructionstep; //Next step!
	}
	if (CPU[activeCPU].instructionstep==(base+1))
	{
		BIU_handleRequestsPending(); //Handle all pending requests at once when to be processed!
		if (BIU_readResultw(result)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].instructionstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU8086_internal_POPw(word base, word *result, byte is32instruction)
{
	if (CPU[activeCPU].internalinstructionstep==base) //First step? Request!
	{
		if (CPU_POP16_BIU(is32instruction)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		BIU_handleRequests(); //Handle all pending requests at once when to be processed!
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	if (CPU[activeCPU].internalinstructionstep==(base+1))
	{
		BIU_handleRequestsPending(); //Handle all pending requests at once when to be processed!
		if (BIU_readResultw(result)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU8086_POPSP(word base)
{
	word result;
	if (CPU[activeCPU].instructionstep==base) //First step? Request!
	{
		if (CPU_request_MMUrw(CPU_SEGMENT_SS,STACK_SEGMENT_DESCRIPTOR_B_BIT()?REG_ESP:REG_SP,!STACK_SEGMENT_DESCRIPTOR_B_BIT())==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		BIU_handleRequests(); //Handle all pending requests at once when to be processed!
		++CPU[activeCPU].instructionstep; //Next step!
	}
	if (CPU[activeCPU].instructionstep==(base+1))
	{
		BIU_handleRequestsPending(); //Handle all pending requests at once when to be processed!
		if (BIU_readResultw(&REG_SP)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		result = REG_SP; //Save the popped value!
		if (STACK_SEGMENT_DESCRIPTOR_B_BIT()) //ESP?
		{
			REG_ESP += 2; //Add two for the correct result!
		}
		else
		{
			REG_SP += 2; //Add two for the correct result!
		}
		REG_SP = result; //Give the correct result, according to http://www.felixcloutier.com/x86/POP.html!
		++CPU[activeCPU].instructionstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU8086_POPb(word base, byte *result, byte is32instruction)
{
	if (CPU[activeCPU].instructionstep==base) //First step? Request!
	{
		if (CPU_POP8_BIU(is32instruction)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		BIU_handleRequests(); //Handle all pending requests at once when to be processed!
		++CPU[activeCPU].instructionstep; //Next step!
	}
	if (CPU[activeCPU].instructionstep==(base+1))
	{
		BIU_handleRequestsPending(); //Handle all pending requests at once when to be processed!
		if (BIU_readResultb(result)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].instructionstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

//BIU delay(keeping BIU active)
byte CPU8086_instructionstepdelayBIU(word base, byte cycles)
{
	if (CPU[activeCPU].instructionstep==base) //First step? Request!
	{
		CPU[activeCPU].cycles_OP += cycles; //Take X cycles only!
		CPU[activeCPU].executed = 0; //Not executed!
		CPU[activeCPU].instructionstep += 2; //Next step, by 2 for compatibility!
		return 1; //Keep running!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU8086_internal_delayBIU(word base, byte cycles)
{
	if (CPU[activeCPU].internalinstructionstep==base) //First step? Request!
	{
		CPU[activeCPU].cycles_OP += cycles; //Take X cycles only!
		CPU[activeCPU].executed = 0; //Not executed!
		CPU[activeCPU].internalinstructionstep += 2; //Next step, by 2 for compatibility!
		return 1; //Keep running!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU8086_instructionstepwaitBIUready(word base)
{
	if (CPU[activeCPU].instructionstep == base) //First step? Request!
	{
		if (BIU_Busy()) //Still busy?
		{
			++CPU[activeCPU].cycles_OP; //1 cycle delay!
			CPU[activeCPU].executed = 0; //Wait to become ready!
			return 1; //Still busy!
		}
		CPU[activeCPU].instructionstep += 2; //Next step!
	}
	return 0; //Ready to process!
}

byte CPU8086_internal_waitBIUready(word base)
{
	if (CPU[activeCPU].internalinstructionstep == base) //First step? Request!
	{
		if (BIU_Busy()) //Still busy?
		{
			++CPU[activeCPU].cycles_OP; //1 cycle delay!
			CPU[activeCPU].executed = 0; //Wait to become ready!
			return 1; //Still busy!
		}
		CPU[activeCPU].internalinstructionstep += 2; //Next step!
	}
	return 0; //Ready to process!
}

byte CPU8086_instructionstepwaitBIUfinished(word base)
{
	if (CPU[activeCPU].instructionstep == base) //First step? Request!
	{
		if (CPU[activeCPU].cycles_OP) //Any pending?
		{
			CPU[activeCPU].instructionstep += 2; //Next step!
			++CPU[activeCPU].cycles_OP; //1 cycle delay!
			CPU[activeCPU].executed = 0; //Wait to become ready!
			return 1; //Still busy!
		}
	}
	return 0; //Ready to process!
}

byte CPU8086_internal_waitBIUfinished(word base)
{
	if (CPU[activeCPU].internalinstructionstep == base) //First step? Request!
	{
		CPU[activeCPU].internalinstructionstep += 2; //Next step!
		if (CPU[activeCPU].cycles_OP) //Any pending?
		{
			++CPU[activeCPU].cycles_OP; //1 cycle delay!
			CPU[activeCPU].executed = 0; //Wait to become ready!
			return 1; //Still busy!
		}
	}
	return 0; //Ready to process!
}

//BUS --- state delay!
byte CPU8086_instructionstepdelayBIUidle(word base, byte cycles)
{
	if (CPU[activeCPU].instructionstep==base) //First step? Request!
	{
		CPU[activeCPU].cycles_stallBUS += cycles; //Take X cycles only!
		CPU[activeCPU].executed = 0; //Not executed!
		CPU[activeCPU].instructionstep += 2; //Next step, by 2 for compatibility!
		return 1; //Keep running!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU8086_internal_delayBIUidle(word base, byte cycles)
{
	if (CPU[activeCPU].internalinstructionstep==base) //First step? Request!
	{
		CPU[activeCPU].cycles_stallBUS += cycles; //Take X cycles only!
		CPU[activeCPU].executed = 0; //Not executed!
		CPU[activeCPU].internalinstructionstep += 2; //Next step, by 2 for compatibility!
		return 1; //Keep running!
	}
	return 0; //Ready to process further! We're loaded!
}



//Instruction variants of ModR/M!

byte CPU8086_instructionstepreadmodrmb(word base, byte *result, byte paramnr) //Base=Start instruction step, result=Pointer to the result container!
{
	byte BIUtype;
	if (CPU[activeCPU].modrmstep==base) //First step? Request!
	{
		if ((BIUtype = modrm_read8_BIU(&CPU[activeCPU].params,paramnr,result))==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].modrmstep; //Next step!
		if (BIUtype==2) //Register?
		{
			++CPU[activeCPU].modrmstep; //Skip next step!
		}
		else //Memory?
		{
			BIU_handleRequests(); //Handle all pending requests at once when to be processed!
		}
	}
	if (CPU[activeCPU].modrmstep==(base+1))
	{
		BIU_handleRequestsPending(); //Handle all pending requests at once when to be processed!
		if (BIU_readResultb(result)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].modrmstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU8086_instructionstepreadmodrmw(word base, word *result, byte paramnr)
{
	byte BIUtype;
	if (CPU[activeCPU].modrmstep==base) //First step? Request!
	{
		if ((BIUtype = modrm_read16_BIU(&CPU[activeCPU].params,paramnr,result))==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].modrmstep; //Next step!
		if (BIUtype==2) //Register?
		{
			++CPU[activeCPU].modrmstep; //Skip next step!
		}
		else //Memory?
		{
			BIU_handleRequests(); //Handle all pending requests at once when to be processed!
		}
	}
	if (CPU[activeCPU].modrmstep==(base+1))
	{
		BIU_handleRequestsPending(); //Handle all pending requests at once when to be processed!
		if (BIU_readResultw(result)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].modrmstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU8086_instructionstepwritemodrmb(word base, byte value, byte paramnr) //Base=Start instruction step, result=Pointer to the result container!
{
	byte dummy;
	byte BIUtype;
	if (CPU[activeCPU].modrmstep==base) //First step? Request!
	{
		if ((BIUtype = modrm_write8_BIU(&CPU[activeCPU].params,paramnr,value))==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].modrmstep; //Next step!
		if (BIUtype==2) //Register?
		{
			++CPU[activeCPU].modrmstep; //Skip next step!
		}
		else //Memory?
		{
			BIU_handleRequests(); //Handle all pending requests at once when to be processed!
		}
	}
	if (CPU[activeCPU].modrmstep==(base+1))
	{
		BIU_handleRequestsPending(); //Handle all pending requests at once when to be processed!
		if (BIU_readResultb(&dummy)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].modrmstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU8086_instructionstepwritemodrmw(word base, word value, byte paramnr, byte isJMPorCALL)
{
	word dummy;
	byte BIUtype;
	if (CPU[activeCPU].modrmstep == base) //First step? Request!
	{
		if ((BIUtype = modrm_write16_BIU(&CPU[activeCPU].params, paramnr, value, isJMPorCALL)) == 0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].modrmstep; //Next step!
		if (BIUtype==2) //Register?
		{
			++CPU[activeCPU].modrmstep; //Skip next step!
		}
		else //Memory?
		{
			BIU_handleRequests(); //Handle all pending requests at once when to be processed!
		}
	}
	if (CPU[activeCPU].modrmstep==(base+1))
	{
		BIU_handleRequestsPending(); //Handle all pending requests at once when to be processed!
		if (BIU_readResultw(&dummy)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].modrmstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU8086_instructionstepwritedirectb(word base, sword segment, word segval, uint_32 offset, byte val, byte is_offset16)
{
	byte dummy;
	if (CPU[activeCPU].modrmstep == base) //First step? Request!
	{
		if (CPU_request_MMUwb(segment, offset, val, is_offset16) == 0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		BIU_handleRequests(); //Handle all pending requests at once when to be processed!
		++CPU[activeCPU].modrmstep; //Next step!
	}
	if (CPU[activeCPU].modrmstep == (base + 1))
	{
		BIU_handleRequestsPending(); //Handle all pending requests at once when to be processed!
		if (BIU_readResultb(&dummy) == 0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].modrmstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU8086_instructionstepwritedirectw(word base, sword segment, word segval, uint_32 offset, word val, byte is_offset16)
{
	word dummy;
	if (CPU[activeCPU].modrmstep == base) //First step? Request!
	{
		if (CPU_request_MMUww(segment, offset, val, is_offset16) == 0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		BIU_handleRequests(); //Handle all pending requests at once when to be processed!
		++CPU[activeCPU].modrmstep; //Next step!
	}
	if (CPU[activeCPU].modrmstep == (base + 1))
	{
		BIU_handleRequestsPending(); //Handle all pending requests at once when to be processed!
		if (BIU_readResultw(&dummy) == 0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].modrmstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU8086_instructionstepreaddirectb(word base, sword segment, word segval, uint_32 offset, byte *result, byte is_offset16)
{
	if (CPU[activeCPU].modrmstep == base) //First step? Request!
	{
		if (CPU_request_MMUrb(segment, offset, is_offset16) == 0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		BIU_handleRequests(); //Handle all pending requests at once when to be processed!
		++CPU[activeCPU].modrmstep; //Next step!
	}
	if (CPU[activeCPU].modrmstep == (base + 1))
	{
		BIU_handleRequestsPending(); //Handle all pending requests at once when to be processed!
		if (BIU_readResultb(result) == 0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].modrmstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU8086_instructionstepreaddirectw(word base, sword segment, word segval, uint_32 offset, word *result, byte is_offset16)
{
	if (CPU[activeCPU].modrmstep == base) //First step? Request!
	{
		if (CPU_request_MMUrw(segment, offset, is_offset16) == 0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		BIU_handleRequests(); //Handle all pending requests at once when to be processed!
		++CPU[activeCPU].modrmstep; //Next step!
	}
	if (CPU[activeCPU].modrmstep == (base + 1))
	{
		BIU_handleRequestsPending(); //Handle all pending requests at once when to be processed!
		if (BIU_readResultw(result) == 0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].modrmstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

//Now, the internal variants of the functions above!
byte CPU8086_internal_stepreadmodrmb(word base, byte *result, byte paramnr) //Base=Start instruction step, result=Pointer to the result container!
{
	byte BIUtype;
	if (CPU[activeCPU].internalmodrmstep==base) //First step? Request!
	{
		if ((BIUtype = modrm_read8_BIU(&CPU[activeCPU].params,paramnr,result))==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalmodrmstep; //Next step!
		if (BIUtype==2) //Register?
		{
			++CPU[activeCPU].internalmodrmstep; //Skip next step!
		}
		else //Memory?
		{
			BIU_handleRequests(); //Handle all pending requests at once when to be processed!
		}
	}
	if (CPU[activeCPU].internalmodrmstep==(base+1))
	{
		BIU_handleRequestsPending(); //Handle all pending requests at once when to be processed!
		if (BIU_readResultb(result)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalmodrmstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU8086_internal_stepreadmodrmw(word base, word *result, byte paramnr)
{
	byte BIUtype;
	if (CPU[activeCPU].internalmodrmstep==base) //First step? Request!
	{
		if ((BIUtype = modrm_read16_BIU(&CPU[activeCPU].params,paramnr,result))==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalmodrmstep; //Next step!
		if (BIUtype==2) //Register?
		{
			++CPU[activeCPU].internalmodrmstep; //Skip next step!
		}
		else //Memory?
		{
			BIU_handleRequests(); //Handle all pending requests at once when to be processed!
		}
	}
	if (CPU[activeCPU].internalmodrmstep==(base+1))
	{
		BIU_handleRequestsPending(); //Handle all pending requests at once when to be processed!
		if (BIU_readResultw(result)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalmodrmstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU8086_internal_stepwritemodrmb(word base, byte value, byte paramnr) //Base=Start instruction step, result=Pointer to the result container!
{
	byte dummy;
	byte BIUtype;
	if (CPU[activeCPU].internalmodrmstep==base) //First step? Request!
	{
		if ((BIUtype = modrm_write8_BIU(&CPU[activeCPU].params,paramnr,value))==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalmodrmstep; //Next step!
		if (BIUtype==2) //Register?
		{
			++CPU[activeCPU].internalmodrmstep; //Skip next step!
		}
		else //Memory?
		{
			BIU_handleRequests(); //Handle all pending requests at once when to be processed!
		}
	}
	if (CPU[activeCPU].internalmodrmstep==(base+1))
	{
		BIU_handleRequestsPending(); //Handle all pending requests at once when to be processed!
		if (BIU_readResultb(&dummy)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalmodrmstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU8086_internal_stepwritedirectb(word base, sword segment, word segval, uint_32 offset, byte val, byte is_offset16)
{
	byte dummy;
	if (CPU[activeCPU].internalmodrmstep==base) //First step? Request!
	{
		if (CPU_request_MMUwb(segment,offset,val,is_offset16)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		BIU_handleRequests(); //Handle all pending requests at once when to be processed!
		++CPU[activeCPU].internalmodrmstep; //Next step!
	}
	if (CPU[activeCPU].internalmodrmstep==(base+1))
	{
		BIU_handleRequestsPending(); //Handle all pending requests at once when to be processed!
		if (BIU_readResultb(&dummy)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalmodrmstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU8086_internal_stepwritedirectw(word base, sword segment, word segval, uint_32 offset, word val, byte is_offset16)
{
	word dummy;
	if (CPU[activeCPU].internalmodrmstep==base) //First step? Request!
	{
		if (CPU_request_MMUww(segment,offset,val,is_offset16)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		BIU_handleRequests(); //Handle all pending requests at once when to be processed!
		++CPU[activeCPU].internalmodrmstep; //Next step!
	}
	if (CPU[activeCPU].internalmodrmstep==(base+1))
	{
		BIU_handleRequestsPending(); //Handle all pending requests at once when to be processed!
		if (BIU_readResultw(&dummy)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalmodrmstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU8086_internal_stepreaddirectb(word base, sword segment, word segval, uint_32 offset, byte *result, byte is_offset16)
{
	if (CPU[activeCPU].internalmodrmstep==base) //First step? Request!
	{
		if (CPU_request_MMUrb(segment,offset,is_offset16)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		BIU_handleRequests(); //Handle all pending requests at once when to be processed!
		++CPU[activeCPU].internalmodrmstep; //Next step!
	}
	if (CPU[activeCPU].internalmodrmstep==(base+1))
	{
		BIU_handleRequestsPending(); //Handle all pending requests at once when to be processed!
		if (BIU_readResultb(result)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalmodrmstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU8086_internal_stepreaddirectw(word base, sword segment, word segval, uint_32 offset, word *result, byte is_offset16)
{
	if (CPU[activeCPU].internalmodrmstep==base) //First step? Request!
	{
		if (CPU_request_MMUrw(segment,offset,is_offset16)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		BIU_handleRequests(); //Handle all pending requests at once when to be processed!
		++CPU[activeCPU].internalmodrmstep; //Next step!
	}
	if (CPU[activeCPU].internalmodrmstep==(base+1))
	{
		BIU_handleRequestsPending(); //Handle all pending requests at once when to be processed!
		if (BIU_readResultw(result)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalmodrmstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU8086_internal_stepreadinterruptw(word base, sword segment, word segval, uint_32 offset, word *result, byte is_offset16)
{
	if (CPU[activeCPU].internalinterruptstep==base) //First step? Request!
	{
		if (CPU_request_MMUrw(segment,offset,is_offset16)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		BIU_handleRequests(); //Handle all pending requests at once when to be processed!
		++CPU[activeCPU].internalinterruptstep; //Next step!
	}
	if (CPU[activeCPU].internalinterruptstep==(base+1))
	{
		BIU_handleRequestsPending(); //Handle all pending requests at once when to be processed!
		if (BIU_readResultw(result)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalinterruptstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU8086_internal_stepwritemodrmw(word base, word value, byte paramnr, byte isJMPorCALL)
{
	word dummy;
	byte BIUtype;
	if (CPU[activeCPU].internalmodrmstep==base) //First step? Request!
	{
		if ((BIUtype = modrm_write16_BIU(&CPU[activeCPU].params,paramnr,value,isJMPorCALL))==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalmodrmstep; //Next step!
		if (BIUtype==2) //Register?
		{
			++CPU[activeCPU].internalmodrmstep; //Skip next step!
		}
		else //Memory?
		{
			BIU_handleRequests(); //Handle all pending requests at once when to be processed!
		}
	}
	if (CPU[activeCPU].internalmodrmstep==(base+1))
	{
		BIU_handleRequestsPending(); //Handle all pending requests at once when to be processed!
		if (BIU_readResultw(&dummy)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalmodrmstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

//Normal support for basic operations
OPTINLINE void CMP_w(word a, word b, byte flags) //Compare instruction!
{
	CPUPROT1
	flag_sub16(a,b); //Flags only!
	if (flags!=4) if (CPU_apply286cycles()) return; //80286+ cycles instead?
	switch (flags & 7)
	{
	case 0: //Default?
		break; //Unused!
	case 1: //Accumulator?
		CPU[activeCPU].cycles_OP += 2; //Imm-Reg
		break;
	case 2: //Determined by ModR/M?
		if (CPU[activeCPU].params.EA_cycles) //Memory is used?
		{
			CPU[activeCPU].cycles_OP += 1; //Mem->Reg!
		}
		else //Reg->Reg?
		{
			CPU[activeCPU].cycles_OP += 1; //Reg->Reg!
		}
		break;
	case 3: //ModR/M+imm?
		if (CPU[activeCPU].params.EA_cycles) //Memory is used?
		{
			CPU[activeCPU].cycles_OP += 5; //Mem->Reg!
		}
		else //Imm->Reg?
		{
			CPU[activeCPU].cycles_OP += 2; //Reg->Reg!
		}
		break;
	case 4: //Mem-Mem instruction?
		CPU[activeCPU].cycles_OP += 0; //Assume two times Reg->Mem
		break;
	default:
		break;
	}
	CPUPROT2
}

OPTINLINE void CMP_b(byte a, byte b, byte flags)
{
	CPUPROT1
	flag_sub8(a,b); //Flags only!
	if (flags!=4) if (CPU_apply286cycles()) return; //80286+ cycles instead?
	switch (flags&7)
	{
	case 0: //Default?
		break; //Unused!
	case 1: //Accumulator?
		CPU[activeCPU].cycles_OP += 2; //Imm-Reg
		break;
	case 2: //Determined by ModR/M?
		if (CPU[activeCPU].params.EA_cycles) //Memory is used?
		{
			CPU[activeCPU].cycles_OP += 1; //Mem->Reg!
		}
		else //Reg->Reg?
		{
			CPU[activeCPU].cycles_OP += 1; //Reg->Reg!
		}
		break;
	case 3: //ModR/M+imm?
		if (CPU[activeCPU].params.EA_cycles) //Memory is used?
		{
			CPU[activeCPU].cycles_OP += 5; //Mem->Reg!
		}
		else //Imm->Reg?
		{
			CPU[activeCPU].cycles_OP += 2; //Reg->Reg!
		}
		break;
	case 4: //Mem-Mem instruction?
		CPU[activeCPU].cycles_OP += 0; //Assume two times Reg->Mem
		break;
	default:
		break;
	}
	CPUPROT2
}

//Modr/m support, used when reg=NULL and custommem==0

//Help functions:
OPTINLINE byte CPU8086_internal_INC16(word *reg)
{
	//Check for exceptions first!
	CPUPROT1
	INLINEREGISTER byte tempCF = FLAG_CF; //CF isn't changed!
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].internalmodrmstep==0))
		{
			if (reg==NULL) 
			{
				if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
				if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0xA0)) return 1; //Abort on fault!
			}
		}
		if (reg==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepreadmodrmw(0,&CPU[activeCPU].oper1, CPU[activeCPU].MODRM_src0)) return 1;
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		CPU[activeCPU].oper1 = reg?*reg: CPU[activeCPU].oper1;
		CPU[activeCPU].oper2 = 1;
		op_add16();
		FLAGW_CF(tempCF);
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		if (reg==NULL) //Destination to write?
		{
			if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
			{
				CPU[activeCPU].cycles_OP += 2; //Mem
			}
			CPU[activeCPU].executed = 0;
			return 1; //Wait for execution phase to finish!
		}
	}
	if (reg) //Register?
	{
		*reg = CPU[activeCPU].res16;
		if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
		{
			CPU[activeCPU].cycles_OP += 1 + (CPU[activeCPU].params.notdecoded ? 0 : 1); //16-bit reg!
		}
	}
	else //Memory?
	{
		if (reg==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepwritemodrmw(2, CPU[activeCPU].res16, CPU[activeCPU].MODRM_src0,0)) return 1;
		}
	}
	CPUPROT2
	return 0;
}
OPTINLINE byte CPU8086_internal_DEC16(word *reg)
{
	CPUPROT1
	INLINEREGISTER byte tempCF = FLAG_CF; //CF isn't changed!
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].internalmodrmstep==0))
		{
			if (reg==NULL)
			{
				if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
				if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0xA0)) return 1; //Abort on fault!
			}
		}
		if (reg==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepreadmodrmw(0,&CPU[activeCPU].oper1, CPU[activeCPU].MODRM_src0)) return 1;
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		CPU[activeCPU].oper1 = reg?*reg: CPU[activeCPU].oper1;
		CPU[activeCPU].oper2 = 1;
		op_sub16();
		FLAGW_CF(tempCF);
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		if (reg==NULL) //Destination to write?
		{
			if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
			{
				CPU[activeCPU].cycles_OP += 2; //Mem
			}
			CPU[activeCPU].executed = 0;
			return 1; //Wait for execution phase to finish!
		}
	}
	if (reg) //Register?
	{
		*reg = CPU[activeCPU].res16;
		if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
		{
			CPU[activeCPU].cycles_OP += 1 + (CPU[activeCPU].params.notdecoded ? 0 : 1); //16-bit reg!
		}
	}
	else //Memory?
	{
		if (reg==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepwritemodrmw(2, CPU[activeCPU].res16, CPU[activeCPU].MODRM_src0,0)) return 1;
		}
	}
	CPUPROT2
	return 0;
}

OPTINLINE byte CPU8086_internal_INC8(byte *reg)
{
	CPUPROT1
	INLINEREGISTER byte tempCF = FLAG_CF; //CF isn't changed!
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].internalmodrmstep==0))
		{
			if (reg==NULL)
			{
				if (modrm_check8(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
				if (modrm_check8(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0xA0)) return 1; //Abort on fault!
			}
		}
		if (reg==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepreadmodrmb(0,&CPU[activeCPU].oper1b, CPU[activeCPU].MODRM_src0)) return 1;
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		CPU[activeCPU].oper1b = reg?*reg: CPU[activeCPU].oper1b;
		CPU[activeCPU].oper2b = 1;
		op_add8();
		FLAGW_CF(tempCF);
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		if (reg==NULL) //Destination to write?
		{
			if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
			{
				CPU[activeCPU].cycles_OP += 2; //Mem
			}
			CPU[activeCPU].executed = 0;
			return 1; //Wait for execution phase to finish!
		}
	}
	if (reg) //Register?
	{
		*reg = CPU[activeCPU].res8;
		if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
		{
			CPU[activeCPU].cycles_OP += 2; //8-bit reg!
		}
	}
	else //Memory?
	{
		if (reg==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepwritemodrmb(2, CPU[activeCPU].res8, CPU[activeCPU].MODRM_src0)) return 1;
		}
	}
	CPUPROT2
	return 0;
}
OPTINLINE byte CPU8086_internal_DEC8(byte *reg)
{
	CPUPROT1
	INLINEREGISTER byte tempCF = FLAG_CF;
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].internalmodrmstep==0))
		{
			if (reg==NULL)
			{
				if (modrm_check8(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
				if (modrm_check8(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0xA0)) return 1; //Abort on fault!
			}
		}
		if (reg==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepreadmodrmb(0,&CPU[activeCPU].oper1b, CPU[activeCPU].MODRM_src0)) return 1;
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		CPU[activeCPU].oper1b = reg?*reg: CPU[activeCPU].oper1b;
		CPU[activeCPU].oper2b = 1;
		op_sub8();
		FLAGW_CF(tempCF);
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		if (reg==NULL) //Destination to write?
		{
			if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
			{
				CPU[activeCPU].cycles_OP += 2; //Mem
			}
			CPU[activeCPU].executed = 0;
			return 1; //Wait for execution phase to finish!
		}
	}
	if (reg) //Register?
	{
		*reg = CPU[activeCPU].res8;
		if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
		{
			CPU[activeCPU].cycles_OP += 2; //8-bit reg!
		}
	}
	else //Memory?
	{
		if (reg==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepwritemodrmb(2, CPU[activeCPU].res8, CPU[activeCPU].MODRM_src0)) return 1;
		}
	}
	CPUPROT2
	return 0;
}

OPTINLINE void timing_AND_OR_XOR_ADD_SUB8(byte *dest, byte flags)
{
	if (CPU_apply286cycles()) return; //No 80286+ cycles instead?
	switch (flags) //What type of operation?
	{
	case 0: //Reg+Reg?
		CPU[activeCPU].cycles_OP += 3; //Reg->Reg!
		break;
	case 1: //Reg+imm?
		CPU[activeCPU].cycles_OP += 2; //Accumulator!
		break;
	case 2: //Determined by ModR/M?
		if (CPU[activeCPU].params.EA_cycles) //Memory is used?
		{
			if (dest) //Mem->Reg?
			{
				CPU[activeCPU].cycles_OP += 3; //Mem->Reg!
			}
			else //Reg->Mem?
			{
				CPU[activeCPU].cycles_OP += 3; //Mem->Reg!
			}
		}
		else //Reg->Reg?
		{
			CPU[activeCPU].cycles_OP += 1; //Reg->Reg!
		}
		break;
	case 3: //ModR/M+imm?
		if (CPU[activeCPU].params.EA_cycles) //Memory is used?
		{
			if (dest) //Imm->Reg?
			{
				CPU[activeCPU].cycles_OP += 2; //Imm->Reg!
			}
			else //Imm->Mem?
			{
				CPU[activeCPU].cycles_OP += 5; //Mem->Reg!
			}
		}
		else //Reg->Reg?
		{
			CPU[activeCPU].cycles_OP += 2; //Reg->Reg!
		}
		break;
	default:
		break;
	}
}

OPTINLINE void timing_AND_OR_XOR_ADD_SUB16(word *dest, byte flags)
{
	if (CPU_apply286cycles()) return; //No 80286+ cycles instead?
	switch (flags) //What type of operation?
	{
	case 0: //Reg+Reg?
		CPU[activeCPU].cycles_OP += 3; //Reg->Reg!
		break;
	case 1: //Reg+imm?
		CPU[activeCPU].cycles_OP += 2; //Accumulator!
		break;
	case 2: //Determined by ModR/M?
		if (CPU[activeCPU].params.EA_cycles) //Memory is used?
		{
			if (dest) //Mem->Reg?
			{
				CPU[activeCPU].cycles_OP += 4; //Mem->Reg!
			}
			else //Reg->Mem?
			{
				CPU[activeCPU].cycles_OP += 3; //Mem->Reg!
			}
		}
		else //Reg->Reg?
		{
			CPU[activeCPU].cycles_OP += 3; //Reg->Reg!
		}
		break;
	case 3: //ModR/M+imm?
		if (CPU[activeCPU].params.EA_cycles) //Memory is used?
		{
			if (dest) //Imm->Reg?
			{
				CPU[activeCPU].cycles_OP += 2; //Imm->Reg!
			}
			else //Imm->Mem?
			{
				CPU[activeCPU].cycles_OP += 5; //Mem->Reg!
			}
		}
		else //Reg->Reg?
		{
			CPU[activeCPU].cycles_OP += 2; //Reg->Reg!
		}
		break;
	default:
		break;
	}
}

//For ADD
OPTINLINE byte CPU8086_internal_ADD8(byte *dest, byte addition, byte flags)
{
	CPUPROT1
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].internalmodrmstep==0))
		{
			if (dest==NULL)
			{
				if (modrm_check8(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
				if (modrm_check8(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0xA0)) return 1; //Abort on fault!
			}
		}
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepreadmodrmb(0,&CPU[activeCPU].oper1b, CPU[activeCPU].MODRM_src0)) return 1;
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		CPU[activeCPU].oper1b = dest?*dest: CPU[activeCPU].oper1b;
		CPU[activeCPU].oper2b = addition;
		op_add8();
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		timing_AND_OR_XOR_ADD_SUB8(dest, flags);
		if (dest==NULL)
		{
			CPU[activeCPU].executed = 0;
			return 1;
		} //Wait for execution phase to finish!
	}
	if (dest) //Register?
	{
		*dest = CPU[activeCPU].res8;
	}
	else //Memory?
	{
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepwritemodrmb(2, CPU[activeCPU].res8, CPU[activeCPU].MODRM_src0)) return 1;
		}
	}
	CPUPROT2
	return 0;
}
OPTINLINE byte CPU8086_internal_ADD16(word *dest, word addition, byte flags)
{
	CPUPROT1
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].internalmodrmstep==0))
		{
			if (dest==NULL)
			{
				if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
				if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0xA0)) return 1; //Abort on fault!
			}
		}
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepreadmodrmw(0,&CPU[activeCPU].oper1, CPU[activeCPU].MODRM_src0)) return 1;
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		CPU[activeCPU].oper1 = dest?*dest: CPU[activeCPU].oper1;
		CPU[activeCPU].oper2 = addition;
		op_add16();
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		timing_AND_OR_XOR_ADD_SUB16(dest, flags);
		if (dest==NULL)
		{
			CPU[activeCPU].executed = 0;
			return 1;
		} //Wait for execution phase to finish!
	}
	if (dest) //Register?
	{
		*dest = CPU[activeCPU].res16;
	}
	else //Memory?
	{
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepwritemodrmw(2, CPU[activeCPU].res16, CPU[activeCPU].MODRM_src0,0)) return 1;
		}
	}
	CPUPROT2
	return 0;
}

//For ADC
OPTINLINE byte CPU8086_internal_ADC8(byte *dest, byte addition, byte flags)
{
	CPUPROT1
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].internalmodrmstep==0))
		{
			if (dest==NULL)
			{
				if (modrm_check8(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
				if (modrm_check8(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0xA0)) return 1; //Abort on fault!
			}
		}
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepreadmodrmb(0,&CPU[activeCPU].oper1b, CPU[activeCPU].MODRM_src0)) return 1;
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		CPU[activeCPU].oper1b = dest?*dest: CPU[activeCPU].oper1b;
		CPU[activeCPU].oper2b = addition;
		op_adc8();
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		timing_AND_OR_XOR_ADD_SUB8(dest, flags);
		if (dest==NULL)
		{
			CPU[activeCPU].executed = 0;
			return 1;
		} //Wait for execution phase to finish!
	}
	if (dest) //Register?
	{
		*dest = CPU[activeCPU].res8;
	}
	else //Memory?
	{
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepwritemodrmb(2, CPU[activeCPU].res8, CPU[activeCPU].MODRM_src0)) return 1;
		}
	}
	CPUPROT2
	return 0;
}
OPTINLINE byte CPU8086_internal_ADC16(word *dest, word addition, byte flags)
{
	CPUPROT1
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].internalmodrmstep==0))
		{
			if (dest==NULL)
			{
				if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
				if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0xA0)) return 1; //Abort on fault!
			}
		}
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepreadmodrmw(0,&CPU[activeCPU].oper1, CPU[activeCPU].MODRM_src0)) return 1;
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		CPU[activeCPU].oper1 = dest?*dest: CPU[activeCPU].oper1;
		CPU[activeCPU].oper2 = addition;
		op_adc16();
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		timing_AND_OR_XOR_ADD_SUB16(dest, flags);
		if (dest==NULL)
		{
			CPU[activeCPU].executed = 0;
			return 1;
		} //Wait for execution phase to finish!
	}
	if (dest) //Register?
	{
		*dest = CPU[activeCPU].res16;
	}
	else //Memory?
	{
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepwritemodrmw(2, CPU[activeCPU].res16, CPU[activeCPU].MODRM_src0,0)) return 1;
		}
	}
	CPUPROT2
	return 0;
}


//For OR
OPTINLINE byte CPU8086_internal_OR8(byte *dest, byte src, byte flags)
{
	CPUPROT1
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].internalmodrmstep==0))
		{
			if (dest==NULL)
			{
				if (modrm_check8(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
				if (modrm_check8(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0xA0)) return 1; //Abort on fault!
			}
		}
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepreadmodrmb(0,&CPU[activeCPU].oper1b, CPU[activeCPU].MODRM_src0)) return 1;
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		CPU[activeCPU].oper1b = dest?*dest: CPU[activeCPU].oper1b;
		CPU[activeCPU].oper2b = src;
		op_or8();
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		timing_AND_OR_XOR_ADD_SUB8(dest, flags);
		if (dest==NULL)
		{
			CPU[activeCPU].executed = 0;
			return 1;
		} //Wait for execution phase to finish!
	}
	if (dest) //Register?
	{
		*dest = CPU[activeCPU].res8;
	}
	else //Memory?
	{
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepwritemodrmb(2, CPU[activeCPU].res8, CPU[activeCPU].MODRM_src0)) return 1;
		}
	}
	CPUPROT2
	return 0;
}
OPTINLINE byte CPU8086_internal_OR16(word *dest, word src, byte flags)
{
	CPUPROT1
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].internalmodrmstep==0))
		{
			if (dest==NULL)
			{
				if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
				if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0xA0)) return 1; //Abort on fault!
			}
		}
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepreadmodrmw(0,&CPU[activeCPU].oper1,CPU[activeCPU].MODRM_src0)) return 1;
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		CPU[activeCPU].oper1 = dest?*dest: CPU[activeCPU].oper1;
		CPU[activeCPU].oper2 = src;
		op_or16();
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		timing_AND_OR_XOR_ADD_SUB16(dest, flags);
		if (dest==NULL)
		{
			CPU[activeCPU].executed = 0;
			return 1;
		} //Wait for execution phase to finish!
	}
	if (dest) //Register?
	{
		*dest = CPU[activeCPU].res16;
	}
	else //Memory?
	{
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepwritemodrmw(2, CPU[activeCPU].res16,CPU[activeCPU].MODRM_src0,0)) return 1;
		}
	}
	CPUPROT2
	return 0;
}

//For AND
OPTINLINE byte CPU8086_internal_AND8(byte *dest, byte src, byte flags)
{
	CPUPROT1
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].internalmodrmstep==0))
		{
			if (dest==NULL)
			{
				if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
				if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0xA0)) return 1; //Abort on fault!
			}
		}
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepreadmodrmb(0,&CPU[activeCPU].oper1b,CPU[activeCPU].MODRM_src0)) return 1;
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		CPU[activeCPU].oper1b = dest?*dest: CPU[activeCPU].oper1b;
		CPU[activeCPU].oper2b = src;
		op_and8();
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		timing_AND_OR_XOR_ADD_SUB8(dest, flags);
		if (dest==NULL)
		{
			CPU[activeCPU].executed = 0;
			return 1;
		} //Wait for execution phase to finish!
	}
	if (dest) //Register?
	{
		*dest = CPU[activeCPU].res8;
	}
	else //Memory?
	{
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepwritemodrmb(2, CPU[activeCPU].res8,CPU[activeCPU].MODRM_src0)) return 1;
		}
	}
	CPUPROT2
	return 0;
}
OPTINLINE byte CPU8086_internal_AND16(word *dest, word src, byte flags)
{
	CPUPROT1
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].internalmodrmstep==0))
		{
			if (dest == NULL)
			{
				if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0x40)) return 1; //Abort on fault on write only!
				if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0xA0)) return 1; //Abort on fault on write only!
			}
		}
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepreadmodrmw(0,&CPU[activeCPU].oper1,CPU[activeCPU].MODRM_src0)) return 1;
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		CPU[activeCPU].oper1 = dest?*dest: CPU[activeCPU].oper1;
		CPU[activeCPU].oper2 = src;
		op_and16();
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		timing_AND_OR_XOR_ADD_SUB16(dest, flags);
		if (dest==NULL)
		{
			CPU[activeCPU].executed = 0;
			return 1;
		} //Wait for execution phase to finish!
	}
	if (dest) //Register?
	{
		*dest = CPU[activeCPU].res16;
	}
	else //Memory?
	{
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepwritemodrmw(2, CPU[activeCPU].res16,CPU[activeCPU].MODRM_src0,0)) return 1;
		}
	}
	CPUPROT2
	return 0;
}


//For SUB
OPTINLINE byte CPU8086_internal_SUB8(byte *dest, byte addition, byte flags)
{
	CPUPROT1
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].internalmodrmstep==0))
		{
			if (dest == NULL)
			{
				if (modrm_check8(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0x40)) return 1; //Abort on fault on write only!
				if (modrm_check8(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0xA0)) return 1; //Abort on fault on write only!
			}
		}
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepreadmodrmb(0,&CPU[activeCPU].oper1b,CPU[activeCPU].MODRM_src0)) return 1;
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		CPU[activeCPU].oper1b = dest?*dest: CPU[activeCPU].oper1b;
		CPU[activeCPU].oper2b = addition;
		op_sub8();
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		timing_AND_OR_XOR_ADD_SUB8(dest, flags);
		if (dest==NULL)
		{
			CPU[activeCPU].executed = 0;
			return 1;
		} //Wait for execution phase to finish!
	}
	if (dest) //Register?
	{
		*dest = CPU[activeCPU].res8;
	}
	else //Memory?
	{
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepwritemodrmb(2, CPU[activeCPU].res8,CPU[activeCPU].MODRM_src0)) return 1;
		}
	}
	CPUPROT2
	return 0;
}
OPTINLINE byte CPU8086_internal_SUB16(word *dest, word addition, byte flags)
{
	CPUPROT1
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].internalmodrmstep==0))
		{
			if (dest == NULL)
			{
				if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0x40)) return 1; //Abort on fault on write only!
				if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0xA0)) return 1; //Abort on fault on write only!
			}
		}
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepreadmodrmw(0,&CPU[activeCPU].oper1,CPU[activeCPU].MODRM_src0)) return 1;
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		CPU[activeCPU].oper1 = dest?*dest: CPU[activeCPU].oper1;
		CPU[activeCPU].oper2 = addition;
		op_sub16();
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		timing_AND_OR_XOR_ADD_SUB16(dest, flags);
		if (dest==NULL)
		{
			CPU[activeCPU].executed = 0;
			return 1;
		} //Wait for execution phase to finish!
	}
	if (dest) //Register?
	{
		*dest = CPU[activeCPU].res16;
	}
	else //Memory?
	{
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepwritemodrmw(2, CPU[activeCPU].res16,CPU[activeCPU].MODRM_src0,0)) return 1;
		}
	}
	CPUPROT2
	return 0;
}

//For SBB
OPTINLINE byte CPU8086_internal_SBB8(byte *dest, byte addition, byte flags)
{
	CPUPROT1
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].internalmodrmstep==0))
		{
			if (dest==NULL)
			{
				if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
				if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0xA0)) return 1; //Abort on fault!
			}
		}
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepreadmodrmb(0,&CPU[activeCPU].oper1b,CPU[activeCPU].MODRM_src0)) return 1;
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		CPU[activeCPU].oper1b = dest?*dest: CPU[activeCPU].oper1b;
		CPU[activeCPU].oper2b = addition;
		op_sbb8();
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		timing_AND_OR_XOR_ADD_SUB8(dest, flags);
		if (dest==NULL)
		{
			CPU[activeCPU].executed = 0;
			return 1;
		} //Wait for execution phase to finish!
	}
	if (dest) //Register?
	{
		*dest = CPU[activeCPU].res8;
	}
	else //Memory?
	{
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepwritemodrmb(2, CPU[activeCPU].res8,CPU[activeCPU].MODRM_src0)) return 1;
		}
	}
	CPUPROT2
	return 0;
}
OPTINLINE byte CPU8086_internal_SBB16(word *dest, word addition, byte flags)
{
	CPUPROT1
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].internalmodrmstep==0))
		{
			if (dest==NULL)
			{
				if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
				if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0xA0)) return 1; //Abort on fault!
			}
		}
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepreadmodrmw(0,&CPU[activeCPU].oper1,CPU[activeCPU].MODRM_src0)) return 1;
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		CPU[activeCPU].oper1 = dest?*dest: CPU[activeCPU].oper1;
		CPU[activeCPU].oper2 = addition;
		op_sbb16();
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		timing_AND_OR_XOR_ADD_SUB16(dest, flags);
		if (dest==NULL)
		{
			CPU[activeCPU].executed = 0;
			return 1;
		} //Wait for execution phase to finish!
	}
	if (dest) //Register?
	{
		*dest = CPU[activeCPU].res16;
	}
	else //Memory?
	{
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepwritemodrmw(2, CPU[activeCPU].res16,CPU[activeCPU].MODRM_src0,0)) return 1;
		}
	}
	CPUPROT2
	return 0;
}

//For XOR
//See AND, but XOR
OPTINLINE byte CPU8086_internal_XOR8(byte *dest, byte src, byte flags)
{
	CPUPROT1
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].internalmodrmstep==0))
		{
			if (dest==NULL)
			{
				if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
				if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0xA0)) return 1; //Abort on fault!
			}
		}
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepreadmodrmb(0,&CPU[activeCPU].oper1b,CPU[activeCPU].MODRM_src0)) return 1;
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		CPU[activeCPU].oper1b = dest?*dest: CPU[activeCPU].oper1b;
		CPU[activeCPU].oper2b = src;
		op_xor8();
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		timing_AND_OR_XOR_ADD_SUB8(dest, flags);
		if (dest==NULL)
		{
			CPU[activeCPU].executed = 0;
			return 1;
		} //Wait for execution phase to finish!
	}
	if (dest) //Register?
	{
		*dest = CPU[activeCPU].res8;
	}
	else //Memory?
	{
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepwritemodrmb(2, CPU[activeCPU].res8,CPU[activeCPU].MODRM_src0)) return 1;
		}
	}
	CPUPROT2
	return 0;
}
OPTINLINE byte CPU8086_internal_XOR16(word *dest, word src, byte flags)
{
	CPUPROT1
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].internalmodrmstep==0))
		{
			if (dest==NULL)
			{
				if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
				if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0xA0)) return 1; //Abort on fault!
			}
		}
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepreadmodrmw(0,&CPU[activeCPU].oper1,CPU[activeCPU].MODRM_src0)) return 1;
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		CPU[activeCPU].oper1 = dest?*dest: CPU[activeCPU].oper1;
		CPU[activeCPU].oper2 = src;
		op_xor16();
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		timing_AND_OR_XOR_ADD_SUB16(dest, flags);
		if (dest==NULL)
		{
			CPU[activeCPU].executed = 0;
			return 1;
		} //Wait for execution phase to finish!
	}
	if (dest) //Register?
	{
		*dest = CPU[activeCPU].res16;
	}
	else //Memory?
	{
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU8086_internal_stepwritemodrmw(2, CPU[activeCPU].res16,CPU[activeCPU].MODRM_src0,0)) return 1;
		}
	}
	CPUPROT2
	return 0;
}

//TEST : same as AND, but discarding the result!
OPTINLINE byte CPU8086_internal_TEST8(byte dest, byte src, byte flags)
{
	CPUPROT1
	CPU[activeCPU].oper1b = dest;
	CPU[activeCPU].oper2b = src;
	op_and8();
	//We don't write anything back for TEST, so only execution step is used!
	//Adjust timing for TEST!
	if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
	{
		switch (flags) //What type of operation?
		{
		case 0: //Reg+Reg?
			CPU[activeCPU].cycles_OP += 3; //Reg->Reg!
			break;
		case 1: //Reg+imm?
			CPU[activeCPU].cycles_OP += 2; //Accumulator!
			break;
		case 2: //Determined by ModR/M?
			if (CPU[activeCPU].params.EA_cycles) //Memory is used?
			{
				//Mem->Reg/Reg->Mem?
				CPU[activeCPU].cycles_OP += 4; //Mem->Reg!
			}
			else //Reg->Reg?
			{
				CPU[activeCPU].cycles_OP += 2; //Reg->Reg!
			}
			break;
		case 3: //ModR/M+imm?
			if (CPU[activeCPU].params.EA_cycles) //Memory is used?
			{
				if (dest) //Imm->Reg?
				{
					CPU[activeCPU].cycles_OP += 3; //Imm->Reg!
				}
				else //Imm->Mem?
				{
					CPU[activeCPU].cycles_OP += 5; //Mem->Reg!
				}
			}
			else //Reg->Reg?
			{
				CPU[activeCPU].cycles_OP += 3; //Reg->Reg!
			}
			break;
		default:
			break;
		}
	}
	CPUPROT2
	return 0;
}

OPTINLINE byte CPU8086_internal_TEST16(word dest, word src, byte flags)
{
	CPUPROT1
	CPU[activeCPU].oper1 = dest;
	CPU[activeCPU].oper2 = src;
	op_and16();
	//We don't write anything back for TEST, so only execution step is used!
	//Adjust timing for TEST!
	if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
	{
		switch (flags) //What type of operation?
		{
		case 0: //Reg+Reg?
			CPU[activeCPU].cycles_OP += 3; //Reg->Reg!
			break;
		case 1: //Reg+imm?
			CPU[activeCPU].cycles_OP += 2; //Accumulator!
			break;
		case 2: //Determined by ModR/M?
			if (CPU[activeCPU].params.EA_cycles) //Memory is used?
			{
				//Mem->Reg/Reg->Mem?
				CPU[activeCPU].cycles_OP += 4; //Mem->Reg!
			}
			else //Reg->Reg?
			{
				CPU[activeCPU].cycles_OP += 2; //Reg->Reg!
			}
			break;
		case 3: //ModR/M+imm?
			if (CPU[activeCPU].params.EA_cycles) //Memory is used?
			{
				if (dest) //Imm->Reg?
				{
					CPU[activeCPU].cycles_OP += 3; //Imm->Reg!
				}
				else //Imm->Mem?
				{
					CPU[activeCPU].cycles_OP += 5; //Mem->Reg!
				}
			}
			else //Reg->Reg?
			{
				CPU[activeCPU].cycles_OP += 3; //Reg->Reg!
			}
			break;
		default:
			break;
		}
	}
	CPUPROT2
	return 0;
}

//Universal DIV instruction for x86 DIV instructions!
/*

Parameters:
	val: The value to divide
	divisor: The value to divide by
	quotient: Quotient result container
	remainder: Remainder result container
	error: 1 on error(DIV0), 0 when valid.
	resultbits: The amount of bits the result contains(16 or 8 on 8086) of quotient and remainder.
	applycycles: Apply the normal cycles? Start out as 1!
	issigned: Signed division?
	quotientnegative: Quotient is signed negative result?
	remaindernegative: Remainder is signed negative result?
	isAdjust: AAM/AAD=1, otherwise 0.
	isRegister: non-zero for register parameter.

*/
void CPU8086_internal_DIV(uint_32 val, word divisor, word *quotient, word *remainder, byte *error, byte resultbits, byte *applycycles, byte issigned, byte quotientnegative, byte remaindernegative, byte isAdjust, byte isRegister)
{
	*error = 0; //Default: no error!
	if (*applycycles)
	{
		if (CPU_apply286cycles()) /* No 80286+ cycles instead? */
		{
			*applycycles = 0; //Don't apply the cycles anymore!
		}
	}

	if ((isAdjust == 0) && *applycycles) CPU[activeCPU].cycles_OP += 3; //3 cycles for non-AA*!

	word l, h; //h=remainder, l=quotient!
	word r;
	byte carry;
	byte c;
	if (resultbits == 16)
	{
		h = ((val >> 16) & 0xFFFF); //High word or byte!
		l = (val & 0xFFFF); //Low word or byte!
	}
	else //8-bit?
	{
		h = ((val >> 8) & 0xFF); //High word or byte!
		l = (val & 0xFF); //Low word or byte!
	}
	if (*applycycles) CPU[activeCPU].cycles_OP += 8; //8 cycles!
	divisor &= (1ULL << resultbits) - 1; //Weap the divisor to be valid!
	if (h >= divisor) //Overflow?
	{
		if ((isAdjust == 0) && *applycycles) ++CPU[activeCPU].cycles_OP; //1 cycle!
		*error = 1; //Divide by 0 error!
		return; //Abort: overflow!
	}
	if (isAdjust == 0) ++CPU[activeCPU].cycles_OP; //1 cycle!

	CPU[activeCPU].cycles_OP += 2; //2 cycles to prepare CLC!
	carry = 1; //Carry!
	for (c = 0; c < resultbits; ++c)
	{
		//Perform 32-bit or 16-bit RCL on both quotient and remainder!
		r = (l << 1) | carry; //RCL!
		carry = (l>>(resultbits - 1))&1; //Carry-out!
		l = r;
		r = (h << 1) | carry; //RCL!
		carry = (h >> (resultbits - 1)) & 1; //Carry-out!
		h = r;

		if (*applycycles) CPU[activeCPU].cycles_OP = 8; //Takes 8 cycles!

		if (carry)
		{
			carry = 0; //No carry anymore!
			h -= divisor; //Substract!
			if ((c == (resultbits - 1)) && *applycycles) CPU[activeCPU].cycles_OP += 2; //Takes 2 cycles on the final turn!
		}
		else //No carry?
		{
			carry = (divisor > h)?1:0; //Overflow?
			if (carry == 0)
			{
				h -= divisor; //Substract!
				if (*applycycles) ++CPU[activeCPU].cycles_OP; //Takes 1 cycle!
				if ((c == (resultbits - 1)) && *applycycles) CPU[activeCPU].cycles_OP += 2; //Takes 2 cycles on the final bit!
			}
		}
	}
	l = ~((l << 1) | carry); //Final RCL and negate for the result!

	h &= ((1ULL << resultbits) - 1); //Wrap to become valid!
	l &= ((1ULL << resultbits) - 1); //Wrap to become valid!

	if ((isAdjust == 0) && issigned) //IDIV?
	{
		if (*applycycles) CPU[activeCPU].cycles_OP += 4; //Takes 4 cycles!
		if ((l&(1ULL << (resultbits-1))) && (((l>(1ULL << (resultbits - 1))) && quotientnegative) || (quotientnegative==0))) //Overflow?
		{
			if (isRegister) ++CPU[activeCPU].cycles_OP; //Register takes 1 cycle?
			*error = 1; //Throw division by 0!
			return;
		}
	}
	*remainder = h; //Remainder!
	*quotient = l; //Quotient!
}

void CPU8086_internal_IDIV(uint_32 val, word divisor, word *quotient, word *remainder, byte *error, byte resultbits, byte *applycycles, byte isAdjust, byte isRegister)
{
	byte quotientnegative, remaindernegative; //To toggle the result and apply sign after and before?

	*applycycles = 1; //Default: apply cycles!
	if (CPU_apply286cycles()) /* No 80286+ cycles instead? */
	{
		*applycycles = 0; //Don't apply the cycles anymore!
	}

	remaindernegative = 0; //Default: don't toggle the result not remainder!
	quotientnegative = (((val >> ((resultbits << 1) - 1)) ^ (divisor >> (resultbits - 1))) & 1); //Are we to change signs on the result? The result is negative instead! (We're a +/- or -/+ division)
	if (val&0x80000000) //Negative value to divide?
	{
		val = ((~val)+1); //Convert the negative value to be positive!
		remaindernegative = 1; //We're to toggle the remainder is any, because the value to divide is negative!
		if (*applycycles) CPU[activeCPU].cycles_OP += 4; //Takes 4 cycles!
	}
	if (divisor&0x8000) //Negative divisor? Convert to a positive divisor!
	{
		divisor = ((~divisor)+1); //Convert the divisor to be positive!
	}
	else
	{
		if (*applycycles) CPU[activeCPU].cycles_OP += 1; //Takes 1 cycles!
	}
	if (*applycycles) CPU[activeCPU].cycles_OP += 9; //Takes 9 cycles!
	CPU8086_internal_DIV(val,divisor,quotient,remainder,error,resultbits,applycycles,1,quotientnegative,remaindernegative,isAdjust,isRegister); //Execute the division as an unsigned division!
	if (*error==0) //No error has occurred? Do post-processing of the results!
	{
		if (*applycycles) CPU[activeCPU].cycles_OP += 7; //Takes 7 cycles!
		//Undocumented IDIV behaviour on 808X! Documented by Reenigne!
		if ((EMULATED_CPU == CPU_8086) && (CPU_getprefix(0xF3)||CPU_getprefix(0xF2))) //REP/REPNZ used on 8086/8088?
		{
			quotientnegative ^= 1; //Flip like acting as a fused NEG to the result!
		}
		if (quotientnegative) //The result is negative?
		{
			*quotient = (~*quotient)+1; //Apply the new sign to the result!
		}
		if (remaindernegative) //The remainder is negative?
		{
			*remainder = (~*remainder)+1; //Apply the new sign to the remainder!
		}
	}
}

//resultbits is either 8(16-bit) or 16(32-bit).
//val=AX, multiplier=multiplier
//low and high are either bytes or words for the result!
void CPU8086_internal_MUL(word val, word multiplier, word *low, word *high, byte resultbits, byte *applycycles, byte issigned, byte resultnegative, byte isAdjust, byte isRegister)
{
	if (*applycycles)
	{
		if (CPU_apply286cycles()) /* No 80286+ cycles instead? */
		{
			*applycycles = 0; //Don't apply the cycles anymore!
		}
	}

	if ((isAdjust == 0) && *applycycles)
	{
		if (resultbits != 16) CPU[activeCPU].cycles_OP += 8; //Byte size takes 8 cycles!

		CPU[activeCPU].cycles_OP += 3; //3 cycles for non-AA* multiply!
	}

	//Main multiply operation!
	byte carry, i;
	word c, a, b, r;

	c = 0;
	a = val;
	b = multiplier;

	a &= ((1 << resultbits) - 1); //Wrap to what's valid!
	b &= ((1 << resultbits) - 1); //Wrap to what's valid!

	carry = (a & 1);
	a >>= 1;
	for (i = 0; i < resultbits; ++i)
	{
		if (*applycycles) CPU[activeCPU].cycles_OP += 7; //Take 7 cycles!
		if (carry)
		{
			CPU[activeCPU].oper1 = c; //Source!
			CPU[activeCPU].oper2 = b; //What to add!
			op_add16(); //8-bit MUL!
			c = CPU[activeCPU].res16; //The result!
			if (*applycycles) ++CPU[activeCPU].cycles_OP; //Take 1 cycle!
			carry = FLAG_CF;
		}

		//Normal shifting!
		r = (c >> 1) | (carry << (resultbits - 1)); //ROR...
		carry = (c & 1); //... carry ...
		c = r; //... Store result!
		r = (a >> 1) | (carry << (resultbits - 1)); //ROR...
		carry = (a & 1); //...carry ...
		a = r; //... Store result!
	}

	//Undocumented MUL/IMUL behaviour on 808X! Documented by Reenigne!
	if ((EMULATED_CPU == CPU_8086) && (CPU_getprefix(0xF2) || CPU_getprefix(0xF3)) && (isAdjust == 0)) //REP/REPNZ used on MUL/IMUL?
	{
		if (*applycycles) CPU[activeCPU].cycles_OP += 9; //Post-negate takes 9 cycles!
		c = ~c; //Negate high and low...!
		a = (~a) + 1; //... and apply the new sign to the result!
		if (a == 0) ++c; //Overflow to high byte/word?
	}

	//Store the result!
	*low = a;
	*high = c;
}

//resultbits: 16 for 32-bit, 8 for 16-bit.
//low and high are the halves of the result.
void CPU8086_internal_IMUL(word val, word multiplier, word *low, word *high, byte resultbits, byte *applycycles, byte isAdjust, byte isRegister)
{
	byte resultnegative; //To toggle the result and apply sign after and before?
	byte valwasnegative,multiplierwasnegative,multiplierwasminimum;
	*applycycles = 1; //Default: apply cycles!
	if (CPU_apply286cycles()) /* No 80286+ cycles instead? */
	{
		*applycycles = 0; //Don't apply the cycles anymore!
	}

	resultnegative = (((val^multiplier) >> (resultbits - 1)) & 1); //Are we to change signs on the result? The result is negative instead! (We're a +/- or -/+ division)
	valwasnegative = multiplierwasnegative = 0;
	if (val&(1<<(resultbits-1))) //Negative value to divide?
	{
		val = ((~val)+1); //Convert the negative value to be positive!
		if (*applycycles) ++CPU[activeCPU].cycles_OP; //Takes 1 cycle!
		valwasnegative = 1; //We were signed!
	}

	multiplierwasminimum = 0;
	if (multiplier&(1<<(resultbits-1))) //Negative divisor? Convert to a positive divisor!
	{
		multiplierwasminimum = (multiplier == (1 << (resultbits - 1))); //Are we the minimum?
		multiplier = ((~multiplier)+1); //Convert the divisor to be positive!
		multiplierwasnegative = 1;
	}
	else
	{
		if (*applycycles && (valwasnegative)) CPU[activeCPU].cycles_OP += 4; //Takes 1 cycle!
	}
	if ((valwasnegative == 0) && *applycycles)
	{
		if (multiplierwasnegative)
		{
			if (!multiplierwasminimum) ++CPU[activeCPU].cycles_OP; //Multiplier wasn't minimum takes 1 cycle?
			++CPU[activeCPU].cycles_OP; //Takes 1 cycle!
		}
	}
	if (*applycycles) CPU[activeCPU].cycles_OP += 10; //Takes 10 cycles!
	CPU8086_internal_MUL(val, multiplier, low, high, resultbits, applycycles, 1, resultnegative, isAdjust, isRegister); //Execute the division as an unsigned division!
	//if (*applycycles) CPU[activeCPU].cycles_OP += 7; //Takes 7 cycles!
	if (resultnegative)
	{
		if (*applycycles) CPU[activeCPU].cycles_OP += 9; //Post-negate takes 9 cycles!
		*high = ~*high; //Negate high and low...!
		*low = (~*low) + 1; //... and apply the new sign to the result!
		if (*low == 0) ++*high; //Overflow to high byte/word?
	}
}

//MOV
OPTINLINE byte CPU8086_internal_MOV8(byte *dest, byte val, byte flags)
{
	CPUPROT1
	if (CPU[activeCPU].internalinstructionstep==0) //First step? Execution only!
	{
		if (dest) //Register?
		{
			*dest = val;
			if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
			{
				switch (flags) //What type are we?
				{
				case 0: //Reg+Reg?
					break; //Unused!
				case 1: //Accumulator from immediate memory address?
					CPU[activeCPU].cycles_OP += 1; //[imm16]->Accumulator!
					break;
				case 2: //ModR/M Memory->Reg?
					if (MODRM_EA(CPU[activeCPU].params)) //Memory?
					{
						CPU[activeCPU].cycles_OP += 3; //Mem->Reg!
					}
					else //Reg->Reg?
					{
						CPU[activeCPU].cycles_OP += 1; //Reg->Reg!
					}
					break;
				case 3: //ModR/M Memory immediate->Reg?
					if (MODRM_EA(CPU[activeCPU].params)) //Memory?
					{
						CPU[activeCPU].cycles_OP += 3; //Mem->Reg!
					}
					else //Reg->Reg?
					{
						CPU[activeCPU].cycles_OP += 1; //Reg->Reg!
					}
					break;
				case 4: //Register immediate->Reg?
					CPU[activeCPU].cycles_OP += 2; //Reg->Reg!
					break;
				case 8: //SegReg->Reg?
					if ((!CPU[activeCPU].MODRM_src1) || (MODRM_EA(CPU[activeCPU].params)==0)) //From register?
					{
						CPU[activeCPU].cycles_OP += 1; //Reg->SegReg!
					}
					else //From memory?
					{
						CPU[activeCPU].cycles_OP += 3; //Mem->SegReg!
					}
					break;
				default:
					break;
				}
			}
			++CPU[activeCPU].internalinstructionstep; //Skip the writeback step!
		}
		else //Memory destination?
		{
			if (CPU[activeCPU].custommem)
			{
				if (checkMMUaccess(CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),(CPU[activeCPU].customoffset&CPU[activeCPU].address_size),0,getCPL(),!CPU[activeCPU].CPU_Address_size,0)) //Error accessing memory?
				{
					return 1; //Abort on fault!
				}
				if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
				{
					CPU[activeCPU].cycles_OP += 1; //Accumulator->[imm16]!
				}
			}
			else //ModR/M?
			{
				if (dest==NULL) if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0)) return 1; //Abort on fault!
				if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
				{
					switch (flags) //What type are we?
					{
					case 0: //Reg+Reg?
						break; //Unused!
					case 1: //Accumulator from immediate memory address?
						CPU[activeCPU].cycles_OP += 1; //Accumulator->[imm16]!
						break;
					case 2: //ModR/M Memory->Reg?
						if (MODRM_EA(CPU[activeCPU].params)) //Memory?
						{
							CPU[activeCPU].cycles_OP += 9-EU_CYCLES_SUBSTRACT_ACCESSREAD; //Mem->Reg!
						}
						else //Reg->Reg?
						{
							CPU[activeCPU].cycles_OP += 1; //Reg->Reg!
						}
						break;
					case 3: //ModR/M Memory immediate->Reg?
						if (MODRM_EA(CPU[activeCPU].params)) //Memory?
						{
							CPU[activeCPU].cycles_OP += 2; //Mem->Reg!
						}
						else //Reg->Reg?
						{
							CPU[activeCPU].cycles_OP += 2; //Reg->Reg!
						}
						break;
					case 4: //Register immediate->Reg (Non-existant!!!)?
						CPU[activeCPU].cycles_OP += 2; //Reg->Reg!
						break;
					case 8: //Reg->SegReg?
						if (CPU[activeCPU].MODRM_src0 || (MODRM_EA(CPU[activeCPU].params) == 0)) //From register?
						{
							CPU[activeCPU].cycles_OP += 1; //SegReg->Reg!
						}
						else //From memory?
						{
							CPU[activeCPU].cycles_OP += 3; //SegReg->Mem!
						}
						break;
					default:
						break;
					}
				}
			}
			++CPU[activeCPU].internalinstructionstep; //Next internal instruction step: memory access!
			CPU[activeCPU].executed = 0; return 1; //Wait for execution phase to finish!
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step: memory access!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		if (CPU[activeCPU].custommem)
		{
			if (CPU8086_internal_stepwritedirectb(0,CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),(CPU[activeCPU].customoffset&CPU[activeCPU].address_size),val,!CPU[activeCPU].CPU_Address_size)) return 1; //Write to memory directly!
		}
		else //ModR/M?
		{
			if (CPU8086_internal_stepwritemodrmb(0,val,CPU[activeCPU].MODRM_src0)) return 1; //Write the result to memory!
		}
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	CPUPROT2
	return 0;
}

OPTINLINE byte CPU8086_internal_MOV16(word *dest, word val, byte flags)
{
	CPUPROT1
	if (CPU[activeCPU].internalinstructionstep==0) //First step? Execution only!
	{
		if (dest) //Register?
		{
			CPU[activeCPU].destEIP = REG_EIP; //Store (E)IP for safety!
			modrm_updatedsegment(dest,val,0); //Check for an updated segment!
			CPUPROT1
			if (CPU_apply286cycles() == 0) //No 80286+ cycles instead?
			{
				switch (flags) //What type are we?
				{
				case 0: //Reg+Reg?
					break; //Unused!
				case 1: //Accumulator from immediate memory address?
					CPU[activeCPU].cycles_OP += 1; //[imm16]->Accumulator!
					break;
				case 2: //ModR/M Memory->Reg?
					if (MODRM_EA(CPU[activeCPU].params)) //Memory?
					{
						CPU[activeCPU].cycles_OP += 3; //Mem->Reg!
					}
					else //Reg->Reg?
					{
						CPU[activeCPU].cycles_OP += 1; //Reg->Reg!
					}
					break;
				case 3: //ModR/M Memory immediate->Reg?
					if (MODRM_EA(CPU[activeCPU].params)) //Memory?
					{
						CPU[activeCPU].cycles_OP += 3; //Mem->Reg!
					}
					else //Reg->Reg?
					{
						CPU[activeCPU].cycles_OP += 1; //Reg->Reg!
					}
					break;
				case 4: //Register immediate->Reg?
					CPU[activeCPU].cycles_OP += 2; //Reg->Reg!
					break;
				case 8: //SegReg->Reg?
					if ((!CPU[activeCPU].MODRM_src1) || (MODRM_EA(CPU[activeCPU].params) == 0)) //From register?
					{
						CPU[activeCPU].cycles_OP += 1; //Reg->SegReg!
					}
					else //From memory?
					{
						CPU[activeCPU].cycles_OP += 3; //Mem->SegReg!
					}
					break;
				default:
					break;
				}
			}
			CPUPROT2
			++CPU[activeCPU].internalinstructionstep; //Skip the memory step!
		}
		else //Memory?
		{
			if (CPU[activeCPU].custommem)
			{
				if (checkMMUaccess16(CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),(CPU[activeCPU].customoffset&CPU[activeCPU].address_size),0|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0|0x8)) //Error accessing memory?
				{
					return 1; //Abort on fault!
				}
				if (checkMMUaccess16(CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].customoffset&CPU[activeCPU].address_size), 0|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0 | 0x8)) //Error accessing memory?
				{
					return 1; //Abort on fault!
				}
				if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
				{
					CPU[activeCPU].cycles_OP += 1; //Accumulator->[imm16]!
				}
			}
			else //ModR/M?
			{
				if (dest==NULL)
				{
					if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
					if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0xA0)) return 1; //Abort on fault!
				}
				if (CPU_apply286cycles() == 0) //No 80286+ cycles instead?
				{
					switch (flags) //What type are we?
					{
					case 0: //Reg+Reg?
						break; //Unused!
					case 1: //Accumulator from immediate memory address?
						CPU[activeCPU].cycles_OP += 1; //Accumulator->[imm16]!
						break;
					case 2: //ModR/M Memory->Reg?
						if (MODRM_EA(CPU[activeCPU].params)) //Memory?
						{
							CPU[activeCPU].cycles_OP += 9 - EU_CYCLES_SUBSTRACT_ACCESSREAD; //Mem->Reg!
						}
						else //Reg->Reg?
						{
							CPU[activeCPU].cycles_OP += 1; //Reg->Reg!
						}
						break;
					case 3: //ModR/M Memory immediate->Reg?
						if (MODRM_EA(CPU[activeCPU].params)) //Memory?
						{
							CPU[activeCPU].cycles_OP += 2; //Mem->Reg!
						}
						else //Reg->Reg?
						{
							CPU[activeCPU].cycles_OP += 2; //Reg->Reg!
						}
						break;
					case 4: //Register immediate->Reg (Non-existant!!!)?
						CPU[activeCPU].cycles_OP += 2; //Reg->Reg!
						break;
					case 8: //Reg->SegReg?
						if (CPU[activeCPU].MODRM_src0 || (MODRM_EA(CPU[activeCPU].params) == 0)) //From register?
						{
							CPU[activeCPU].cycles_OP += 1; //SegReg->Reg!
						}
						else //From memory?
						{
							CPU[activeCPU].cycles_OP += 3; //SegReg->Mem!
						}
						break;
					default:
						break;
					}
				}
			}
			++CPU[activeCPU].internalinstructionstep; //Next internal instruction step: memory access!
			CPU[activeCPU].executed = 0; return 1; //Wait for execution phase to finish!
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step: memory access!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		if (CPU[activeCPU].custommem)
		{
			if (CPU8086_internal_stepwritedirectw(0,CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),(CPU[activeCPU].customoffset&CPU[activeCPU].address_size),val,!CPU[activeCPU].CPU_Address_size)) return 1; //Write to memory directly!
		}
		else //ModR/M?
		{
			if (CPU8086_internal_stepwritemodrmw(0,val,CPU[activeCPU].MODRM_src0,0)) return 1; //Write the result to memory!
		}
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	CPUPROT2
	return 0;
}

//LEA for LDS, LES
OPTINLINE word getLEA(MODRM_PARAMS *theparams)
{
	return modrm_lea16(theparams,1);
}

/*

Non-logarithmic opcodes!

*/

//BCD opcodes!
byte CPU8086_internal_DAA()
{
	word ALVAL, oldCF, oldAL;
	CPUPROT1
	oldAL = (word)REG_AL; //Save original!
	oldCF = FLAG_CF; //Save old Carry!
	ALVAL = (word)REG_AL;
	if (((ALVAL&0xF)>9) || FLAG_AF)
	{
		CPU[activeCPU].oper1 = ALVAL+6;
		ALVAL = (CPU[activeCPU].oper1&0xFF);
		FLAGW_AF(1);
	}
	else FLAGW_AF(0);
	if (((REG_AL)>0x99) || oldCF)
	{
		ALVAL += 0x60;
		FLAGW_CF(1);
	}
	else
	{
		FLAGW_CF(0);
	}
	REG_AL = (byte)(ALVAL&0xFF); //Write the value back to AL!
	flag_szp8(REG_AL);
	FLAGW_OF(((((oldAL&0x80)==0) && (REG_AL&0x80)))?1:0); //Overflow flag, according to IBMulator!
	CPUPROT2
	if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
	{
		CPU[activeCPU].cycles_OP += 3; //Timings!
	}
	return 0;
}
byte CPU8086_internal_DAS()
{
	INLINEREGISTER byte old_CF, old_AL;
	INLINEREGISTER word carryAL;
	old_AL = (word)(REG_AL);
	old_CF = FLAG_CF; //Save old values!
	FLAGW_CF(0);
	CPUPROT1
	if (((old_AL&0xF)>9) || FLAG_AF)
	{
		carryAL = REG_AL-6;
		REG_AL = (carryAL&0xFF); //Store the result!
		FLAGW_CF(old_CF|((carryAL&0xFF00)>0)); //Old CF or borrow that occurs when substracting!
		FLAGW_AF(1);
	}
	else FLAGW_AF(0);

	if ((old_AL>0x99) || old_CF)
	{
		REG_AL -= 0x60;
		FLAGW_CF(1);
	}
	flag_szp8(REG_AL);
	FLAGW_OF(((old_AL&0x80)) && ((REG_AL&0x80)==0)); //According to IBMulator!
	CPUPROT2
	if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
	{
		CPU[activeCPU].cycles_OP += 3; //Timings!
	}
	return 0;
}
byte CPU8086_internal_AAA()
{
	byte applycycles;
	CPUPROT1
		applycycles = 0;
		if (CPU_apply286cycles() == 0) //No 80286+ cycles instead?
		{
			CPU[activeCPU].cycles_OP += 7; //Timings!
			applycycles = 1;
		}

	if (((REG_AL&0xF)>9))
	{
		FLAGW_OF(((REG_AL&0xF0)==0x70)?1:0); //According to IBMulator
		REG_AL += 6;
		REG_AL &= 0xF;
		++REG_AH;
		FLAGW_AF(1);
		FLAGW_CF(1);
		FLAGW_ZF((REG_AL==0)?1:0);
	}
	else if (FLAG_AF)
	{
		REG_AL += 6;
		REG_AL &= 0xF;
		++REG_AH;
		FLAGW_AF(1);
		FLAGW_CF(1);
		FLAGW_ZF(0); //According to IBMulator!
		FLAGW_OF(0); //According to IBMulator!
	}
	else
	{
		FLAGW_AF(0);
		FLAGW_CF(0);
		FLAGW_OF(0); //According to IBMulator!
		FLAGW_ZF((REG_AL==0)?1:0); //According to IBMulator!
		CPU[activeCPU].cycles_OP += applycycles; //Timings!
	}
	flag_p8(REG_AL); //Parity is affected!
	//z=s=p=o=?
	CPUPROT2
	return 0;
}
byte CPU8086_internal_AAS()
{
	byte applycycles;
	CPUPROT1
		applycycles = 0;
		if (CPU_apply286cycles() == 0) //No 80286+ cycles instead?
		{
			CPU[activeCPU].cycles_OP += 7; //Timings!
			applycycles = 1;
		}

	if (((REG_AL&0xF)>9) || FLAG_AF)
	{
		FLAGW_SF((REG_AL>0x85)?1:0); //According to IBMulator!
		REG_AL -= 6;
		--REG_AH;
		FLAGW_AF(1);
		FLAGW_CF(1);
		FLAGW_OF(0); //According to IBMulator!
	}
	else if (FLAG_AF)
	{
		FLAGW_OF(((REG_AL>=0x80) && (REG_AL<=0x85))?1:0); //According to IBMulator!
		FLAGW_SF(((REG_AL < 0x06) || (REG_AL > 0x85))?1:0); //According to IBMulator!
		REG_AL -= 6;
		--REG_AH;
		FLAGW_AF(1);
		FLAGW_CF(1);
	}
	else
	{
		FLAGW_SF((REG_AL>=0x80)?1:0); //According to IBMulator!
		FLAGW_AF(0);
		FLAGW_CF(0);
		FLAGW_OF(0); //According to IBMulator!
		CPU[activeCPU].cycles_OP += applycycles; //Timings!
	}
	flag_p8(REG_AL); //Parity is affected!
	FLAGW_ZF((REG_AL==0)?1:0); //Zero is affected!
	REG_AL &= 0xF;
	//z=s=o=p=?
	CPUPROT2
	return 0;
}

OPTINLINE byte CPU8086_internal_AAM(byte data)
{
	CPUPROT1
	word quotient, remainder;
	byte error, applycycles;
	applycycles = 1; //Default: apply cycles!
	CPU8086_internal_DIV(REG_AL,data,&quotient,&remainder,&error,8,&applycycles,0,0,0,1,0);
	if (error) //Error occurred?
	{
		CPU_exDIV0(); //Raise error that's requested!
		return 1;
	}
	else //Valid result?
	{
		REG_AH = (byte)(quotient&0xFF);
		REG_AL = (byte)(remainder&0xFF);
		//Flags are set on newer CPUs according to the MOD operation: Sign, Zero and Parity are set according to the mod operation(AL) and Overflow, Carry and Auxiliary carry are cleared.
		flag_szp8(REG_AL); //Result of MOD instead!
		FLAGW_OF(0); FLAGW_CF(0); FLAGW_AF(0); //Clear these!
		//C=O=A=?
	}
	CPUPROT2
	return 0;
}
OPTINLINE byte CPU8086_internal_AAD(byte data)
{
	byte applycycles;
	CPUPROT1
	applycycles = 1;
	CPU8086_internal_MUL((word)data, REG_AH, &CPU[activeCPU].oper1, &CPU[activeCPU].oper2, 8, &applycycles, 0, 0, 1, 0); //Execute MUL!
	CPU[activeCPU].oper2b = (word)REG_AL; //What to add!
	CPU[activeCPU].oper1b = (byte)((word)CPU[activeCPU].oper1); //AAD base to work on, we're adding to this!
	op_add8(); //Add, 8-bit, including flags!
	REG_AX = (CPU[activeCPU].res8&0xFF); //The result to load!
	CPUPROT2
	return 0;
}

OPTINLINE byte CPU8086_internal_CBW()
{
	CPUPROT1
	if ((REG_AL&0x80)==0x80)
	{
		REG_AH = 0xFF;
	}
	else
	{
		REG_AH = 0;
	}
	if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
	{
		CPU[activeCPU].cycles_OP += 1; //Clock cycles!
	}
	CPUPROT2
	return 0;
}
OPTINLINE byte CPU8086_internal_CWD()
{
	CPUPROT1
	if ((REG_AH&0x80)==0x80)
	{
		if (CPU_apply286cycles() == 0) //No 80286+ cycles instead?
		{
			CPU[activeCPU].cycles_OP += 5; //Clock cycles!
		}
		REG_DX = 0xFFFF;
	}
	else
	{
		if (CPU_apply286cycles() == 0) //No 80286+ cycles instead?
		{
			CPU[activeCPU].cycles_OP += 4; //Clock cycles!
		}
		REG_DX = 0;
	}
	CPUPROT2
	return 0;
}

//Now the repeatable instructions!

OPTINLINE byte CPU8086_internal_MOVSB()
{
	if (CPU[activeCPU].blockREP) return 1; //Disabled REP!
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (checkMMUaccess(CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),(CPU[activeCPU].CPU_Address_size?REG_ESI:REG_SI),1|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		if (checkMMUaccess(CPU_SEGMENT_ES,REG_ES,(CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI),0|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		if (checkMMUaccess(CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].CPU_Address_size ? REG_ESI : REG_SI), 1|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		if (checkMMUaccess(CPU_SEGMENT_ES, REG_ES, (CPU[activeCPU].CPU_Address_size ? REG_EDI : REG_DI), 0|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	CPU[activeCPU].counter = 0; //Init counter!
	if (CPU[activeCPU].internalinstructionstep==1) //First Execution step?
	{
		//Needs a read from memory?
		if (CPU8086_internal_stepreaddirectb(0,CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].CPU_Address_size?REG_ESI:REG_SI), &CPU[activeCPU].MOVSB_data,!CPU[activeCPU].CPU_Address_size)) return 1; //Try to read the data!
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==2) //Execution step?
	{
		if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
		{
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	//Writeback phase!
	if (CPU8086_internal_stepwritedirectb(2,CPU_SEGMENT_ES,REG_ES,(CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI), CPU[activeCPU].MOVSB_data,!CPU[activeCPU].CPU_Address_size)) return 1;
	if (!CPU[activeCPU].gotREP) //Non-blocked non-REP?
	{
		if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 3)) return 1; //3 cycles for non-REP!
		CPU[activeCPU].counter += 2;
	}
	CPUPROT1
	if (FLAG_DF)
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			--REG_ESI;
			--REG_EDI;
		}
		else
		{
			--REG_SI;
			--REG_DI;
		}
	}
	else
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			++REG_ESI;
			++REG_EDI;
		}
		else
		{
			++REG_SI;
			++REG_DI;
		}
	}
	CPUPROT2
	return 0;
}

OPTINLINE byte CPU8086_internal_MOVSW()
{
	if (CPU[activeCPU].blockREP) return 1; //Disabled REP!
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (checkMMUaccess16(CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),(CPU[activeCPU].CPU_Address_size?REG_ESI:REG_SI),1|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0|0x8)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		if (checkMMUaccess16(CPU_SEGMENT_ES,REG_ES,(CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI),0|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0|0x8)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		if (checkMMUaccess16(CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].CPU_Address_size ? REG_ESI : REG_SI), 1|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0 | 0x8)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		if (checkMMUaccess16(CPU_SEGMENT_ES, REG_ES, (CPU[activeCPU].CPU_Address_size ? REG_EDI : REG_DI), 0|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0 | 0x8)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	CPU[activeCPU].counter = 0; //Init counter!
	if (CPU[activeCPU].internalinstructionstep==1) //First Execution step?
	{
		//Needs a read from memory?
		if (CPU8086_internal_stepreaddirectw(0,CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].CPU_Address_size?REG_ESI:REG_SI), &CPU[activeCPU].MOVSW_data,!CPU[activeCPU].CPU_Address_size)) return 1; //Try to read the data!
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==2) //Execution step?
	{
		if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
		{
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	//Writeback phase!
	if (CPU8086_internal_stepwritedirectw(2,CPU_SEGMENT_ES,REG_ES,(CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI), CPU[activeCPU].MOVSW_data,!CPU[activeCPU].CPU_Address_size)) return 1;
	if (!CPU[activeCPU].gotREP) //Non-blocked non-REP?
	{
		if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 3)) return 1; //3 cycles for non-REP!
		CPU[activeCPU].counter += 2;
	}
	CPUPROT1
	if (FLAG_DF)
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			REG_ESI -= 2;
			REG_EDI -= 2;
		}
		else
		{
			REG_SI -= 2;
			REG_DI -= 2;
		}
	}
	else
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			REG_ESI += 2;
			REG_EDI += 2;
		}
		else
		{
			REG_SI += 2;
			REG_DI += 2;
		}
	}
	CPUPROT2
	return 0;
}

OPTINLINE byte CPU8086_internal_CMPSB()
{
	if (CPU[activeCPU].blockREP) return 1; //Disabled REP!
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (checkMMUaccess(CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS),(CPU[activeCPU].CPU_Address_size?REG_ESI:REG_SI),1|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		if (checkMMUaccess(CPU_SEGMENT_ES, REG_ES, (CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI),1|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		if (checkMMUaccess(CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].CPU_Address_size ? REG_ESI : REG_SI), 1|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		if (checkMMUaccess(CPU_SEGMENT_ES, REG_ES, (CPU[activeCPU].CPU_Address_size ? REG_EDI : REG_DI), 1|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	CPU[activeCPU].counter = 0; //Init counter!
	if (CPU[activeCPU].gotREP) //Non-blocked non-REP?
	{
		if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 1)) return 1; //1 cycles for REP!
		CPU[activeCPU].counter += 2;
	}
	if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 1)) return 1; //1 cycles always!
	CPU[activeCPU].counter += 2;
	if (CPU[activeCPU].internalinstructionstep==1) //First Execution step?
	{
		//Needs a read from memory?
		if (CPU8086_internal_stepreaddirectb(0,CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].CPU_Address_size?REG_ESI:REG_SI),&CPU[activeCPU].CMPSB_data1,!CPU[activeCPU].CPU_Address_size)) return 1; //Try to read the data!
		if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 2 + ((BIU_getcycle() == 0) ? 1 : 0))) return 1; //2 cycles + 1 for idle bus!
		CPU[activeCPU].counter += 2;
		if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 1)) return 1; //1 cycles after!
		CPU[activeCPU].counter += 2;
		if (CPU8086_internal_stepreaddirectb(2,CPU_SEGMENT_ES, REG_ES, (CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI), &CPU[activeCPU].CMPSB_data2,!CPU[activeCPU].CPU_Address_size)) return 1; //Try to read the data!
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	else
	{
		CPU[activeCPU].counter += 4;
	}
	if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 2+((BIU_getcycle()==0)?1:0))) return 1; //2 cycles + 1 for idle bus!
	CPU[activeCPU].counter += 2;
	if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 2)) return 1; //2 cycles always!
	CPU[activeCPU].counter += 2;
	if (!CPU[activeCPU].gotREP) //Non-blocked non-REP?
	{
		if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 3)) return 1; //3 cycles for REP!
		CPU[activeCPU].counter += 2;
	}
	CMP_b(CPU[activeCPU].CMPSB_data1, CPU[activeCPU].CMPSB_data2,4);
	if (FLAG_DF)
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			--REG_ESI;
			--REG_EDI;
		}
		else
		{
			--REG_SI;
			--REG_DI;
		}
	}
	else
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			++REG_ESI;
			++REG_EDI;
		}
		else
		{
			++REG_SI;
			++REG_DI;
		}
	}
	if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
	{
		//Huh? No timing on the CMPSB instruction? But it does for the CMPSW instruction?
	}
	return 0;
}

OPTINLINE byte CPU8086_internal_CMPSW()
{
	if (CPU[activeCPU].blockREP) return 1; //Disabled REP!
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (checkMMUaccess16(CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS),(CPU[activeCPU].CPU_Address_size?REG_ESI:REG_SI),1|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0|0x8)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		if (checkMMUaccess16(CPU_SEGMENT_ES, REG_ES, (CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI),1|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0|0x8)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		if (checkMMUaccess16(CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].CPU_Address_size ? REG_ESI : REG_SI), 1|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0 | 0x8)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		if (checkMMUaccess16(CPU_SEGMENT_ES, REG_ES, (CPU[activeCPU].CPU_Address_size ? REG_EDI : REG_DI), 1|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0 | 0x8)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	CPU[activeCPU].counter = 0; //Init counter!
	if (CPU[activeCPU].gotREP) //Non-blocked non-REP?
	{
		if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 1)) return 1; //1 cycles for REP!
		CPU[activeCPU].counter += 2;
	}
	if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 1)) return 1; //1 cycles always!
	CPU[activeCPU].counter += 2;
	if (CPU[activeCPU].internalinstructionstep==1) //First Execution step?
	{
		//Needs a read from memory?
		if (CPU8086_internal_stepreaddirectw(0,CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].CPU_Address_size?REG_ESI:REG_SI),&CPU[activeCPU].CMPSW_data1,!CPU[activeCPU].CPU_Address_size)) return 1; //Try to read the data!
		if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 2 + ((BIU_getcycle() == 0) ? 1 : 0))) return 1; //2 cycles + 1 for idle bus!
		CPU[activeCPU].counter += 2;
		if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 1)) return 1; //1 cycles after!
		CPU[activeCPU].counter += 2;
		if (CPU8086_internal_stepreaddirectw(2,CPU_SEGMENT_ES, REG_ES, (CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI), &CPU[activeCPU].CMPSW_data2,!CPU[activeCPU].CPU_Address_size)) return 1; //Try to read the data!
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	else
	{
		CPU[activeCPU].counter += 4;
	}
	if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 2 + ((BIU_getcycle() == 0) ? 1 : 0))) return 1; //2 cycles + 1 for idle bus!
	CPU[activeCPU].counter += 2;
	if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 2)) return 1; //2 cycles always!
	CPU[activeCPU].counter += 2;
	if (!CPU[activeCPU].gotREP) //Non-blocked non-REP?
	{
		if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 3)) return 1; //3 cycles for REP!
		CPU[activeCPU].counter += 2;
	}
	CMP_w(CPU[activeCPU].CMPSW_data1, CPU[activeCPU].CMPSW_data2,4);
	if (FLAG_DF)
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			REG_ESI -= 2;
			REG_EDI -= 2;
		}
		else
		{
			REG_SI -= 2;
			REG_DI -= 2;
		}
	}
	else
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			REG_ESI += 2;
			REG_EDI += 2;
		}
		else
		{
			REG_SI += 2;
			REG_DI += 2;
		}
	}

	if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
	{
		if (CPU[activeCPU].repeating) //Are we a repeating instruction?
		{
			if (CPU[activeCPU].newREP) //Include the REP?
			{
				CPU[activeCPU].cycles_OP += 8; //Clock cycles including REP!
			}
			else //Repeating instruction itself?
			{
				CPU[activeCPU].cycles_OP += 7; //Clock cycles excluding REP!
			}
		}
		else //Plain non-repeating instruction?
		{
			CPU[activeCPU].cycles_OP += 8; //Clock cycles!
		}
	}
	return 0;
}
OPTINLINE byte CPU8086_internal_STOSB()
{
	if (CPU[activeCPU].blockREP) return 1; //Disabled REP!
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (checkMMUaccess(CPU_SEGMENT_ES, REG_ES, (CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI),0,getCPL(),!CPU[activeCPU].CPU_Address_size,0)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	CPU[activeCPU].counter = 0; //Init counter!
	if (CPU[activeCPU].internalinstructionstep==1) //First Execution step?
	{
		//Needs a read from memory?
		if (CPU8086_internal_stepwritedirectb(0,CPU_SEGMENT_ES,REG_ES,(CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI),REG_AL,!CPU[activeCPU].CPU_Address_size)) return 1; //Try to read the data!
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 1)) return 1; //1 cycle after!
	CPU[activeCPU].counter += 2;
	if (!CPU[activeCPU].gotREP) //Non-blocked REP?
	{
		if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 3)) return 1; //3 cycles for REP!
		CPU[activeCPU].counter += 2;
	}

	CPUPROT1
	if (FLAG_DF)
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			--REG_EDI;
		}
		else
		{
			--REG_DI;
		}
	}
	else
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			++REG_EDI;
		}
		else
		{
			++REG_DI;
		}
	}
	CPUPROT2
	if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
	{
	}
	return 0;
}
OPTINLINE byte CPU8086_internal_STOSW()
{
	if (CPU[activeCPU].blockREP) return 1; //Disabled REP!
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (checkMMUaccess16(CPU_SEGMENT_ES, REG_ES, (CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI),0|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0|0x8)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		if (checkMMUaccess16(CPU_SEGMENT_ES, REG_ES, (CPU[activeCPU].CPU_Address_size ? REG_EDI : REG_DI), 0|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0 | 0x8)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	CPU[activeCPU].counter = 0; //Init counter!
	if (CPU[activeCPU].internalinstructionstep==1) //First Execution step?
	{
		//Needs a read from memory?
		if (CPU8086_internal_stepwritedirectw(0,CPU_SEGMENT_ES,REG_ES,(CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI),REG_AX,!CPU[activeCPU].CPU_Address_size)) return 1; //Try to read the data!
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 1)) return 1; //1 cycle after!
	CPU[activeCPU].counter += 2;
	if (!CPU[activeCPU].gotREP) //Non-blocked REP?
	{
		if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 3)) return 1; //3 cycles for REP!
		CPU[activeCPU].counter += 2;
	}
	CPUPROT1
	if (FLAG_DF)
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			REG_EDI -= 2;
		}
		else
		{
			REG_DI -= 2;
		}
	}
	else
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			REG_EDI += 2;
		}
		else
		{
			REG_DI += 2;
		}
	}
	CPUPROT2
	if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
	{
	}
	return 0;
}
//OK so far!
OPTINLINE byte CPU8086_internal_LODSB()
{
	if (CPU[activeCPU].blockREP) return 1; //Disabled REP!
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (checkMMUaccess(CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].CPU_Address_size?REG_ESI:REG_SI),1,getCPL(),!CPU[activeCPU].CPU_Address_size,0)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		++CPU[activeCPU].internalinstructionstep;
	}
	CPU[activeCPU].counter = 0; //Init counter!
	if (CPU[activeCPU].gotREP) //Non-blocked REP?
	{
		if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 1)) return 1; //1 cycle for REP LODS!
		CPU[activeCPU].counter += 2;
	}
	if (CPU[activeCPU].internalinstructionstep==1) //First Execution step?
	{
		//Needs a read from memory?
		if (CPU8086_internal_stepreaddirectb(0,CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].CPU_Address_size?REG_ESI:REG_SI), &CPU[activeCPU].LODSB_value,!CPU[activeCPU].CPU_Address_size)) return 1; //Try to read the data!
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].gotREP) //Non-blocked REP?
	{
		if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 2)) return 1; //2 cycles for REP LODS!
		CPU[activeCPU].counter += 2;
	}
	else
	{
		if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 3)) return 1; //3 cycles for non-REP!
		CPU[activeCPU].counter += 2;
	}
	if (!CPU[activeCPU].gotREP) //Non-blocked REP?
	{
		if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 1)) return 1; //1 cycles for non-REP LODS!
		CPU[activeCPU].counter += 2;
	}
	CPUPROT1
	REG_AL = CPU[activeCPU].LODSB_value;
	if (FLAG_DF)
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			--REG_ESI;
		}
		else
		{
			--REG_SI;
		}
	}
	else
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			++REG_ESI;
		}
		else
		{
			++REG_SI;
		}
	}
	CPUPROT2

	if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
	{
	}
	return 0;
}

OPTINLINE byte CPU8086_internal_LODSW()
{
	if (CPU[activeCPU].blockREP) return 1; //Disabled REP!
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (checkMMUaccess16(CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].CPU_Address_size?REG_ESI:REG_SI),1|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0|0x8)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		if (checkMMUaccess16(CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].CPU_Address_size ? REG_ESI : REG_SI), 1|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0 | 0x8)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		++CPU[activeCPU].internalinstructionstep;
	}
	CPU[activeCPU].counter = 0; //Init counter!
	if (CPU[activeCPU].gotREP) //Non-blocked REP?
	{
		if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 1)) return 1; //1 cycle for REP LODS!
		CPU[activeCPU].counter += 2;
	}
	if (CPU[activeCPU].internalinstructionstep==1) //First Execution step?
	{
		//Needs a read from memory?
		if (CPU8086_internal_stepreaddirectw(0,CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].CPU_Address_size?REG_ESI:REG_SI), &CPU[activeCPU].LODSW_value,!CPU[activeCPU].CPU_Address_size)) return 1; //Try to read the data!
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].gotREP) //Non-blocked REP?
	{
		if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 2)) return 1; //2 cycles for REP LODS!
		CPU[activeCPU].counter += 2;
	}
	else
	{
		if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 3)) return 1; //3 cycles for non-REP!
		CPU[activeCPU].counter += 2;
	}
	if (!CPU[activeCPU].gotREP) //Non-blocked REP?
	{
		if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 1)) return 1; //1 cycles for non-REP LODS!
		CPU[activeCPU].counter += 2;
	}
	CPUPROT1
	REG_AX = CPU[activeCPU].LODSW_value;
	if (FLAG_DF)
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			REG_ESI -= 2;
		}
		else
		{
			REG_SI -= 2;
		}
	}
	else
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			REG_ESI += 2;
		}
		else
		{
			REG_SI += 2;
		}
	}
	CPUPROT2
	if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
	{
	}
	return 0;
}

OPTINLINE byte CPU8086_internal_SCASB()
{
	if (CPU[activeCPU].blockREP) return 1; //Disabled REP!
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (checkMMUaccess(CPU_SEGMENT_ES, REG_ES, (CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI),1,getCPL(),!CPU[activeCPU].CPU_Address_size,0)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		++CPU[activeCPU].internalinstructionstep;
	}
	CPU[activeCPU].counter = 0; //Init counter!
	if (CPU[activeCPU].gotREP) //Non-blocked non-REP?
	{
		if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 1)) return 1; //1 cycles for REP!
		CPU[activeCPU].counter += 2;
	}
	if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 1)) return 1; //1 cycles always!
	CPU[activeCPU].counter += 2;
	if (CPU[activeCPU].internalinstructionstep==1) //First Execution step?
	{
		//Needs a read from memory?
		if (CPU8086_internal_stepreaddirectb(0,CPU_SEGMENT_ES, REG_ES, (CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI), &CPU[activeCPU].SCASB_cmp1,!CPU[activeCPU].CPU_Address_size)) return 1; //Try to read the data!
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 2)) return 1; //2 cycles always!
	CPU[activeCPU].counter += 2;
	if (!CPU[activeCPU].gotREP) //Non-blocked non-REP?
	{
		if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 3)) return 1; //3 cycles for REP!
		CPU[activeCPU].counter += 2;
	}

	//Old function
	CPUPROT1
	CMP_b(REG_AL, CPU[activeCPU].SCASB_cmp1,4);
	if (FLAG_DF)
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			--REG_EDI;
		}
		else
		{
			--REG_DI;
		}
	}
	else
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			++REG_EDI;
		}
		else
		{
			++REG_DI;
		}
	}
	CPUPROT2
	if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
	{
	}
	return 0;
}

OPTINLINE byte CPU8086_internal_SCASW()
{
	if (CPU[activeCPU].blockREP) return 1; //Disabled REP!
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (checkMMUaccess16(CPU_SEGMENT_ES, REG_ES, (CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI),1|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0|0x8)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		if (checkMMUaccess16(CPU_SEGMENT_ES, REG_ES, (CPU[activeCPU].CPU_Address_size ? REG_EDI : REG_DI), 1|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0 | 0x8)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		++CPU[activeCPU].internalinstructionstep;
	}
	CPU[activeCPU].counter = 0; //Init counter!
	if (CPU[activeCPU].gotREP) //Non-blocked non-REP?
	{
		if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 1)) return 1; //1 cycles for REP!
		CPU[activeCPU].counter += 2;
	}
	if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 1)) return 1; //1 cycles always!
	CPU[activeCPU].counter += 2;
	if (CPU[activeCPU].internalinstructionstep==1) //First Execution step?
	{
		//Needs a read from memory?
		if (CPU8086_internal_stepreaddirectw(0,CPU_SEGMENT_ES, REG_ES, (CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI), &CPU[activeCPU].SCASW_cmp1,!CPU[activeCPU].CPU_Address_size)) return 1; //Try to read the data!
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 2)) return 1; //2 cycles always!
	CPU[activeCPU].counter += 2;
	if (!CPU[activeCPU].gotREP) //Non-blocked non-REP?
	{
		if (CPU8086_instructionstepdelayBIU(CPU[activeCPU].counter, 3)) return 1; //3 cycles for REP!
		CPU[activeCPU].counter += 2;
	}

	CPUPROT1
	CMP_w(REG_AX, CPU[activeCPU].SCASW_cmp1,4);
	if (FLAG_DF)
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			REG_EDI -= 2;
		}
		else
		{
			REG_DI -= 2;
		}
	}
	else
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			REG_EDI += 2;
		}
		else
		{
			REG_DI += 2;
		}
	}
	CPUPROT2
	if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
	{
	}
	return 0;
}

OPTINLINE byte CPU8086_instructionstepPOPtimeout(word base)
{
	return CPU8086_instructionstepdelayBIU(base,2);//Delay 2 cycles for POPs to start!
}

OPTINLINE byte CPU8086_internal_POPtimeout(word base)
{
	return CPU8086_internal_delayBIU(base,2);//Delay 2 cycles for POPs to start!
}

OPTINLINE byte CPU8086_internal_RET(word popbytes, byte isimm)
{
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,0,0)) return 1;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU8086_internal_POPtimeout(0)) return 1; //POP timeout!
	if (CPU8086_internal_POPw(2,&CPU[activeCPU].RET_val,0)) return 1;
    //Near return
	CPUPROT1
	CPU_JMPabs((uint_32)CPU[activeCPU].RET_val,0);
	if (CPU_condflushPIQ(-1)) return 1; //We're jumping to another address!
	CPUPROT1
	if (STACK_SEGMENT_DESCRIPTOR_B_BIT())
	{
		REG_ESP += popbytes; //Process ESP!
	}
	else
	{
		REG_SP += popbytes; //Process SP!
	}
	CPUPROT2
	CPUPROT2
	if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
	{
		if (isimm)
			CPU[activeCPU].cycles_OP += 5; /* Intrasegment with constant */
		else
			CPU[activeCPU].cycles_OP += 3; /* Intrasegment */
		CPU[activeCPU].cycles_stallBIU += CPU[activeCPU].cycles_OP; //Stall the BIU completely now!
	}
	return 0;
}

OPTINLINE byte CPU8086_internal_RETF(word popbytes, byte isimm)
{
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(2,0,0)) return 1;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU8086_internal_POPtimeout(0)) return 1; //POP timeout!
	if (CPU8086_internal_POPw(2,&CPU[activeCPU].RETF_val,0)) return 1;
	if (CPU8086_internal_POPw(4,&CPU[activeCPU].RETF_destCS,0)) return 1;
	CPUPROT1
	CPUPROT1
	CPU[activeCPU].destEIP = (uint_32)CPU[activeCPU].RETF_val; //Load IP!
	CPU[activeCPU].RETF_popbytes = popbytes; //Allow modification!
	if (segmentWritten(CPU_SEGMENT_CS, CPU[activeCPU].RETF_destCS,4)) return 1; //CS changed, we're a RETF instruction!
	CPUPROT1
	if (STACK_SEGMENT_DESCRIPTOR_B_BIT())
	{
		REG_ESP += CPU[activeCPU].RETF_popbytes; //Process ESP!
	}
	else
	{
		REG_SP += CPU[activeCPU].RETF_popbytes; //Process SP!
	}
	if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
	{
		if (isimm)
			CPU[activeCPU].cycles_OP += 5; /* Intersegment with constant */
		else
			CPU[activeCPU].cycles_OP += 5; /* Intersegment */
		CPU[activeCPU].cycles_stallBIU += CPU[activeCPU].cycles_OP; //Stall the BIU completely now!
	}
	CPUPROT2
	CPUPROT2
	CPUPROT2
	return 0;
}

void external8086RETF(word popbytes)
{
	CPU8086_internal_RETF(popbytes,1); //Return immediate variant!
}

extern byte advancedlog; //Advanced log setting

extern byte MMU_logging; //Are we logging from the MMU?

OPTINLINE byte CPU8086_internal_INTO()
{
	if (FLAG_OF==0) goto finishINTO; //Finish?
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger","#OF fault(-1)!");
	}

	if (CPU_faultraised(EXCEPTION_OVERFLOW)==0) //Fault raised?
	{
		return 1; //Abort handling when needed!
	}
	if (CPU_apply286cycles() == 0) //No 80286+ cycles instead?
	{
		CPU[activeCPU].cycles_OP += 5; //Timings!
	}
	CPU_executionphase_startinterrupt(EXCEPTION_OVERFLOW,0,-2); //Return to opcode!
	return 0; //Finished: OK!
	finishINTO:
	{
		if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
		{
			CPU[activeCPU].cycles_OP += 3; //Timings!
		}
	}
	return 0; //Finished: OK!
}

OPTINLINE byte CPU8086_internal_XLAT()
{
	if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
	{
		debugger_setcommand("XLAT");    //XLAT
	}
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (checkMMUaccess(CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),((REG_BX+REG_AL)&CPU[activeCPU].address_size),1,getCPL(),!CPU[activeCPU].CPU_Address_size,0)) return 1; //Abort on fault!
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //First Execution step?
	{
		//Needs a read from memory?
		if (CPU8086_internal_stepreaddirectb(0,CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),((REG_BX+REG_AL)&CPU[activeCPU].address_size),&CPU[activeCPU].XLAT_value,!CPU[activeCPU].CPU_Address_size)) return 1; //Try to read the data!
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	CPUPROT1
	REG_AL = CPU[activeCPU].XLAT_value;
	CPUPROT2
	if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
	{
		CPU[activeCPU].cycles_OP += 1; //XLAT timing!
	}
	return 0;
}

void CPU8086_external_XLAT()
{
	CPU8086_internal_XLAT();
} //External variant!

OPTINLINE byte CPU8086_internal_XCHG8(byte *data1, byte *data2, byte flags)
{
	if (unlikely(CPU[activeCPU].internalinstructionstep==0))
	{
		if (data1==NULL)
		{
			CPU_setprefix(0xF0); //Locked!
			if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
		}
		CPU[activeCPU].secondparambase = (data1||data2)?0:2; //Second param base
		CPU[activeCPU].writebackbase = ((data2==NULL) && (data1==NULL))?4:2; //Write back param base
		if (data2==NULL)
		{
			CPU_setprefix(0xF0); //Locked!
			if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,0|0x40)) return 1; //Abort on fault!
		}
		if (data1==NULL)
		{
			if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0xA0)) return 1; //Abort on fault!
		}
		CPU[activeCPU].secondparambase = (data1||data2)?0:2; //Second param base
		CPU[activeCPU].writebackbase = ((data2==NULL) && (data1==NULL))?4:2; //Write back param base
		if (data2==NULL)
		{
			if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,0|0xA0)) return 1; //Abort on fault!
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	CPUPROT1
	if (CPU[activeCPU].internalinstructionstep==1) //First step?
	{
		if (data1==NULL) if (CPU8086_internal_stepreadmodrmb(0,&CPU[activeCPU].oper1b,CPU[activeCPU].MODRM_src0)) return 1;
		if (data2==NULL) if (CPU8086_internal_stepreadmodrmb(CPU[activeCPU].secondparambase,&CPU[activeCPU].oper2b,CPU[activeCPU].MODRM_src1)) return 1;
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==2) //Execution step?
	{
		CPU[activeCPU].oper1b = data1?*data1: CPU[activeCPU].oper1b;
		CPU[activeCPU].oper2b = data2?*data2: CPU[activeCPU].oper2b;
		INLINEREGISTER byte temp = CPU[activeCPU].oper1b; //Copy!
		CPU[activeCPU].oper1b = CPU[activeCPU].oper2b; //We're ...
		CPU[activeCPU].oper2b = temp; //Swapping this!
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
		{
			switch (flags)
			{
			case 0: //Unknown?
				break;
			case 1: //Acc<->Reg?
				CPU[activeCPU].cycles_OP += 2; //Acc<->Reg!
				break;
			case 2: //Mem<->Reg?
				if (MODRM_EA(CPU[activeCPU].params)) //Reg<->Mem?
				{
					CPU[activeCPU].cycles_OP += 3; //SegReg->Mem!
				}
				else //Reg<->Reg?
				{
					CPU[activeCPU].cycles_OP += 3; //SegReg->Mem!
				}
				break;
			default:
				break;
			}
		}
		if ((data1==NULL) || (data2==NULL))
		{
			CPU[activeCPU].executed = 0;
			return 1;
		} //Wait for execution phase to finish!
	}
	if (data1) //Register?
	{
		*data1 = CPU[activeCPU].oper1b;
	}
	else //Memory?
	{
		if (CPU8086_internal_stepwritemodrmb(CPU[activeCPU].writebackbase, CPU[activeCPU].oper1b,CPU[activeCPU].MODRM_src0)) return 1;
	}
	
	if (data2)
	{
		*data2 = CPU[activeCPU].oper2b;
	}
	else
	{
		if (CPU8086_internal_stepwritemodrmb(CPU[activeCPU].writebackbase+ CPU[activeCPU].secondparambase, CPU[activeCPU].oper2b,CPU[activeCPU].MODRM_src1)) return 1;
	}
	CPUPROT2
	return 0;
}

OPTINLINE byte CPU8086_internal_XCHG16(word *data1, word *data2, byte flags)
{
	if (unlikely(CPU[activeCPU].internalinstructionstep==0))
	{
		if (data1==NULL)
		{
			CPU_setprefix(0xF0); //Locked!
			if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
		}
		CPU[activeCPU].secondparambase = (data1||data2)?0:2; //Second param base
		CPU[activeCPU].writebackbase = ((data2==NULL) && (data1==NULL))?4:2; //Write back param base
		if (data2==NULL)
		{
			CPU_setprefix(0xF0); //Locked!
			if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,0|0x40)) return 1; //Abort on fault!
		}
		if (data1==NULL)
		{
			if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0xA0)) return 1; //Abort on fault!
		}
		CPU[activeCPU].secondparambase = (data1||data2)?0:2; //Second param base
		CPU[activeCPU].writebackbase = ((data2==NULL) && (data1==NULL))?4:2; //Write back param base
		if (data2==NULL)
		{
			if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,0|0xA0)) return 1; //Abort on fault!
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	CPUPROT1
	if (CPU[activeCPU].internalinstructionstep==1) //First step?
	{
		if (data1==NULL) if (CPU8086_internal_stepreadmodrmw(0,&CPU[activeCPU].oper1,CPU[activeCPU].MODRM_src0)) return 1;
		if (data2==NULL) if (CPU8086_internal_stepreadmodrmw(CPU[activeCPU].secondparambase,&CPU[activeCPU].oper2,CPU[activeCPU].MODRM_src1)) return 1;
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==2) //Execution step?
	{
		CPU[activeCPU].oper1 = data1?*data1: CPU[activeCPU].oper1;
		CPU[activeCPU].oper2 = data2?*data2: CPU[activeCPU].oper2;
		INLINEREGISTER word temp = CPU[activeCPU].oper1; //Copy!
		CPU[activeCPU].oper1 = CPU[activeCPU].oper2; //We're ...
		CPU[activeCPU].oper2 = temp; //Swapping this!
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
		{
			switch (flags)
			{
			case 0: //Unknown?
				break;
			case 1: //Acc<->Reg?
				CPU[activeCPU].cycles_OP += 2; //Acc<->Reg!
				break;
			case 2: //Mem<->Reg?
				if (MODRM_EA(CPU[activeCPU].params)) //Reg<->Mem?
				{
					CPU[activeCPU].cycles_OP += 3; //SegReg->Mem!
				}
				else //Reg<->Reg?
				{
					CPU[activeCPU].cycles_OP += 3; //SegReg->Mem!
				}
				break;
			default:
				break;
			}
		}
		if ((data1==NULL) || (data2==NULL))
		{
			CPU[activeCPU].executed = 0;
			return 1;
		} //Wait for execution phase to finish!
	}

	if (data1) //Register?
	{
		*data1 = CPU[activeCPU].oper1;
	}
	else //Memory?
	{
		if (CPU8086_internal_stepwritemodrmw(CPU[activeCPU].writebackbase, CPU[activeCPU].oper1,CPU[activeCPU].MODRM_src0,0)) return 1;
	}
	
	if (data2)
	{
		*data2 = CPU[activeCPU].oper2;
	}
	else
	{
		if (CPU8086_internal_stepwritemodrmw(CPU[activeCPU].writebackbase+ CPU[activeCPU].secondparambase, CPU[activeCPU].oper2,CPU[activeCPU].MODRM_src1,0)) return 1;
	}
	CPUPROT2
	return 0;
}

byte CPU8086_internal_LXS(int segmentregister) //LDS, LES etc.
{
	if (unlikely(CPU[activeCPU].internalinstructionstep==0))
	{
		if (modrm_isregister(CPU[activeCPU].params)) //Invalid?
		{
			CPU_unkOP(); //Invalid: registers aren't allowed!
			return 1;
		}
		CPU[activeCPU].modrm_addoffset = 0; //Add this to the offset to use!
		if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,1|0x40)) return 1; //Abort on fault!
		CPU[activeCPU].modrm_addoffset = 2; //Add this to the offset to use!
		if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,1|0x40)) return 1; //Abort on fault!
		CPU[activeCPU].modrm_addoffset = 0;
		if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault for the used segment itself!
		CPU[activeCPU].modrm_addoffset = 0; //Add this to the offset to use!
		if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,1|0xA0)) return 1; //Abort on fault!
		CPU[activeCPU].modrm_addoffset = 2; //Add this to the offset to use!
		if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,1|0xA0)) return 1; //Abort on fault!
		CPU[activeCPU].modrm_addoffset = 0;
		if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0xA0)) return 1; //Abort on fault for the used segment itself!
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	CPUPROT1
	if (CPU[activeCPU].internalinstructionstep==1) //First step?
	{
		CPU[activeCPU].modrm_addoffset = 0; //Add this to the offset to use!
		if (CPU8086_internal_stepreadmodrmw(0,&CPU[activeCPU].LXS_offsetw,CPU[activeCPU].MODRM_src1)) return 1;
		CPU[activeCPU].modrm_addoffset = 2; //Add this to the offset to use!
		if (CPU8086_internal_stepreadmodrmw(2,&CPU[activeCPU].LXS_segment,CPU[activeCPU].MODRM_src1)) return 1;
		CPU[activeCPU].modrm_addoffset = 0; //Reset again!
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	//Execution phase!
	CPUPROT1
		CPU[activeCPU].destEIP = REG_EIP; //Save EIP for transfers!
	if (segmentWritten(segmentregister, CPU[activeCPU].LXS_segment,0)) return 1; //Load the new segment!
	CPUPROT1
	modrm_write16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, CPU[activeCPU].LXS_offsetw, 0); //Try to load the new register with the offset!
	CPUPROT2
	CPUPROT2
	CPUPROT2
	if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
	{
		if (MODRM_EA(CPU[activeCPU].params)) //Memory?
		{
			CPU[activeCPU].cycles_OP += 3; /* LXS based on MOV Mem->SS, DS, ES */
		}
		else //Register? Should be illegal?
		{
			CPU[activeCPU].cycles_OP += 1; /* LXS based on MOV Mem->SS, DS, ES */
		}
	}
	return 0;
}

byte CPU8086_CALLF(word segment, word offset)
{
	CPU[activeCPU].destEIP = (uint_32)offset;
	return segmentWritten(CPU_SEGMENT_CS, segment, 2); /*CS changed, call version!*/
}

/*

NOW THE REAL OPCODES!

*/

void CPU8086_execute_ADD_modrmmodrm8()
{
	modrm_generateInstructionTEXT("ADD",8,0,PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep==0)) if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,1)) return;
	if (CPU8086_instructionstepreadmodrmb(0,&CPU[activeCPU].instructionbufferb,CPU[activeCPU].MODRM_src1)) return;
	if (CPU8086_internal_ADD8(modrm_addr8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0), CPU[activeCPU].instructionbufferb,2)) return;
	if (CPU8086_instructionstepdelayBIU(0, 2 + (MODRM_EA(CPU[activeCPU].params) ? 0 : 1))) return; //2 more cycles + 1 for non-memory!
}
void CPU8086_execute_ADD_modrmmodrm16()
{
	modrm_generateInstructionTEXT("ADD", 16, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src1)) return;
	if (CPU8086_internal_ADD16(modrm_addr16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0), CPU[activeCPU].instructionbufferw, 2)) return;
	if (CPU8086_instructionstepdelayBIU(0, 2 + (MODRM_EA(CPU[activeCPU].params) ? 0 : 1))) return; //2 more cycles + 1 for non-memory!
}
void CPU8086_OP04()
{
	INLINEREGISTER byte theimm = CPU[activeCPU].immb;
	modrm_generateInstructionTEXT("ADD AL,",0,theimm,PARAM_IMM8_PARAM);
	CPU8086_internal_ADD8(&REG_AL,theimm,1);
}
void CPU8086_OP05()
{
	INLINEREGISTER word theimm = CPU[activeCPU].immw;
	modrm_generateInstructionTEXT("ADD AX,",0,theimm,PARAM_IMM16_PARAM);
	CPU8086_internal_ADD16(&REG_AX,theimm,1);
}
void CPU8086_OP06()
{
	modrm_generateInstructionTEXT("PUSH ES",0,0,PARAM_NONE);
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,1,CPU[activeCPU].CPU_Operand_size|2)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU8086_instructionstepdelayBIU(0, 3)) return; //Wait 3 cycles before activating the BIU!
	if (CPU8086_PUSHw(2,&REG_ES,CPU[activeCPU].CPU_Operand_size|2)) return; /*PUSH ES*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 0;
	} /*Push Segreg!*/
}
void CPU8086_OP07()
{
	modrm_generateInstructionTEXT("POP ES",0,0,PARAM_NONE);
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,0,CPU[activeCPU].CPU_Operand_size|2)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU8086_instructionstepPOPtimeout(0)) return; /*POP timeout*/
	if (CPU8086_POPw(2,&CPU[activeCPU].instructionbufferw,CPU[activeCPU].CPU_Operand_size|2)) return;
	if (segmentWritten(CPU_SEGMENT_ES, CPU[activeCPU].instructionbufferw,0)) return; /*CS changed!*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSREAD;
	} /*Pop Segreg!*/
}
void CPU8086_execute_OR_modrmmodrm8()
{
	modrm_generateInstructionTEXT("OR",8,0,PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep==0)) if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,1)) return;
	if (CPU8086_instructionstepreadmodrmb(0,&CPU[activeCPU].instructionbufferb,CPU[activeCPU].MODRM_src1)) return;
	if (CPU8086_internal_OR8(modrm_addr8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0), CPU[activeCPU].instructionbufferb,2)) return;
	if (CPU8086_instructionstepdelayBIU(0, 2 + (MODRM_EA(CPU[activeCPU].params) ? 0 : 1))) return; //2 more cycles + 1 for non-memory!
}
void CPU8086_execute_OR_modrmmodrm16()
{
	modrm_generateInstructionTEXT("OR", 16, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0xA0)) return;
	}
	if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src1)) return;
	if (CPU8086_internal_OR16(modrm_addr16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0), CPU[activeCPU].instructionbufferw, 2)) return;
	if (CPU8086_instructionstepdelayBIU(0, 2 + (MODRM_EA(CPU[activeCPU].params) ? 0 : 1))) return; //2 more cycles + 1 for non-memory!
}
void CPU8086_OP0C()
{
	INLINEREGISTER byte theimm = CPU[activeCPU].immb;
	modrm_generateInstructionTEXT("OR AL,",0,theimm,PARAM_IMM8_PARAM);
	CPU8086_internal_OR8(&REG_AL,theimm,1);
}
void CPU8086_OP0D()
{
	INLINEREGISTER word theimm = CPU[activeCPU].immw;
	modrm_generateInstructionTEXT("OR AX,",0,theimm,PARAM_IMM16_PARAM);
	CPU8086_internal_OR16(&REG_AX,theimm,1);
}
void CPU8086_OP0E()
{
	modrm_generateInstructionTEXT("PUSH CS",0,0,PARAM_NONE);
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,1,CPU[activeCPU].CPU_Operand_size|2)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU8086_instructionstepdelayBIU(0, 3)) return; //Wait 3 cycles before activating the BIU!
	if (CPU8086_PUSHw(2,&REG_CS,CPU[activeCPU].CPU_Operand_size|2)) return; /*PUSH CS*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 0; /*Push Segreg!*/
	}
}
void CPU8086_OP0F() /*FLAG_OF: POP CS; shouldn't be used?*/
{
	modrm_generateInstructionTEXT("POP CS", 0, 0, PARAM_NONE);
	if (CPU8086_instructionstepPOPtimeout(0)) return; /*POP timeout*/
	if (CPU8086_POPw(2,&CPU[activeCPU].instructionbufferw,CPU[activeCPU].CPU_Operand_size)) return; /*Don't handle: 8086 ignores this opcode, and you won't find it there!*/
	CPU[activeCPU].destEIP = REG_EIP;
	if (segmentWritten(CPU_SEGMENT_CS, CPU[activeCPU].instructionbufferw, 0)) return; /*POP CS!*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSREAD; /*Pop Segreg!*/
	}
}
void CPU8086_execute_ADC_modrmmodrm8()
{
	modrm_generateInstructionTEXT("ADC",8,0,PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep==0)) if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,1)) return;
	if (CPU8086_instructionstepreadmodrmb(0,&CPU[activeCPU].instructionbufferb,CPU[activeCPU].MODRM_src1)) return;
	if (CPU8086_internal_ADC8(modrm_addr8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0), CPU[activeCPU].instructionbufferb,2)) return;
	if (CPU8086_instructionstepdelayBIU(0, 2 + (MODRM_EA(CPU[activeCPU].params) ? 0 : 1))) return; //2 more cycles + 1 for non-memory!
}
void CPU8086_execute_ADC_modrmmodrm16()
{
	modrm_generateInstructionTEXT("ADC", 16, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0xA0)) return;
	}
	if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src1)) return;
	if (CPU8086_internal_ADC16(modrm_addr16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0), CPU[activeCPU].instructionbufferw, 2)) return;
	if (CPU8086_instructionstepdelayBIU(0, 2 + (MODRM_EA(CPU[activeCPU].params) ? 0 : 1))) return; //2 more cycles + 1 for non-memory!
}
void CPU8086_OP14()
{
	INLINEREGISTER byte theimm = CPU[activeCPU].immb;
	modrm_generateInstructionTEXT("ADC AL,",0,theimm,PARAM_IMM8_PARAM);
	CPU8086_internal_ADC8(&REG_AL,theimm,1);
}
void CPU8086_OP15()
{
	INLINEREGISTER word theimm = CPU[activeCPU].immw;
	modrm_generateInstructionTEXT("ADC AX,",0,theimm,PARAM_IMM16_PARAM);
	CPU8086_internal_ADC16(&REG_AX,theimm,1);
}
void CPU8086_OP16()
{
	modrm_generateInstructionTEXT("PUSH SS",0,0,PARAM_NONE);/*PUSH SS*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,1,CPU[activeCPU].CPU_Operand_size|2)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU8086_instructionstepdelayBIU(0, 3)) return; //Wait 3 cycles before activating the BIU!
	if (CPU8086_PUSHw(2,&REG_SS,CPU[activeCPU].CPU_Operand_size|2)) return; /*PUSH SS*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 0; /*Push Segreg!*/
	}
}
void CPU8086_OP17()
{
	modrm_generateInstructionTEXT("POP SS",0,0,PARAM_NONE);/*POP SS*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,0,CPU[activeCPU].CPU_Operand_size|2)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU8086_instructionstepPOPtimeout(0)) return; /*POP timeout*/
	if (CPU8086_POPw(2,&CPU[activeCPU].instructionbufferw,CPU[activeCPU].CPU_Operand_size|2)) return;
	if (segmentWritten(CPU_SEGMENT_SS, CPU[activeCPU].instructionbufferw,0)) return; /*CS changed!*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSREAD; /*Pop Segreg!*/
	}
	if (CPU[activeCPU].previousAllowInterrupts) //Not already inhibiting?
	{
		CPU[activeCPU].allowInterrupts = 0; /* Inhabit all interrupts up to the next instruction */
	}
}
void CPU8086_execute_SBB_modrmmodrm8()
{
	modrm_generateInstructionTEXT("SBB",8,0,PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep==0)) if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,1)) return;
	if (CPU8086_instructionstepreadmodrmb(0,&CPU[activeCPU].instructionbufferb,CPU[activeCPU].MODRM_src1)) return;
	if (CPU8086_internal_SBB8(modrm_addr8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0), CPU[activeCPU].instructionbufferb,2)) return;
	if (CPU8086_instructionstepdelayBIU(0, 2 + (MODRM_EA(CPU[activeCPU].params) ? 0 : 1))) return; //2 more cycles + 1 for non-memory!
}
void CPU8086_execute_SBB_modrmmodrm16()
{
	modrm_generateInstructionTEXT("SBB", 16, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0xA0)) return;
	}
	if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src1)) return;
	if (CPU8086_internal_SBB16(modrm_addr16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0), CPU[activeCPU].instructionbufferw, 2)) return;
	if (CPU8086_instructionstepdelayBIU(0, 2 + (MODRM_EA(CPU[activeCPU].params) ? 0 : 1))) return; //2 more cycles + 1 for non-memory!
}
void CPU8086_OP1C()
{
	INLINEREGISTER byte theimm = CPU[activeCPU].immb;
	modrm_generateInstructionTEXT("SBB AL,",0,theimm,PARAM_IMM8_PARAM);
	CPU8086_internal_SBB8(&REG_AL,theimm,1);
}
void CPU8086_OP1D()
{
	INLINEREGISTER word theimm = CPU[activeCPU].immw;
	modrm_generateInstructionTEXT("SBB AX,",0,theimm,PARAM_IMM16_PARAM);
	CPU8086_internal_SBB16(&REG_AX,theimm,1);
}
void CPU8086_OP1E()
{
	modrm_generateInstructionTEXT("PUSH DS",0,0,PARAM_NONE);/*PUSH DS*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,1,CPU[activeCPU].CPU_Operand_size|2)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU8086_instructionstepdelayBIU(0, 3)) return; //Wait 3 cycles before activating the BIU!
	if (CPU8086_PUSHw(2,&REG_DS,CPU[activeCPU].CPU_Operand_size|2)) return; /*PUSH DS*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 0;
	} /*Push Segreg!*/
}
void CPU8086_OP1F()
{
	modrm_generateInstructionTEXT("POP DS",0,0,PARAM_NONE);/*POP DS*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,0,CPU[activeCPU].CPU_Operand_size|2)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU8086_instructionstepPOPtimeout(0)) return; /*POP timeout*/
	if (CPU8086_POPw(2,&CPU[activeCPU].instructionbufferw,CPU[activeCPU].CPU_Operand_size|2)) return;
	if (segmentWritten(CPU_SEGMENT_DS, CPU[activeCPU].instructionbufferw,0)) return; /*CS changed!*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSREAD; /*Pop Segreg!*/
	}
}
void CPU8086_execute_AND_modrmmodrm8()
{
	modrm_generateInstructionTEXT("AND",8,0,PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep==0)) if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,1)) return;
	if (CPU8086_instructionstepreadmodrmb(0,&CPU[activeCPU].instructionbufferb,CPU[activeCPU].MODRM_src1)) return;
	if (CPU8086_internal_AND8(modrm_addr8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0), CPU[activeCPU].instructionbufferb,2)) return;
	if (CPU8086_instructionstepdelayBIU(0, 2 + (MODRM_EA(CPU[activeCPU].params) ? 0 : 1))) return; //2 more cycles + 1 for non-memory!
}
void CPU8086_execute_AND_modrmmodrm16()
{
	modrm_generateInstructionTEXT("AND", 16, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0xA0)) return;
	}
	if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src1)) return;
	if (CPU8086_internal_AND16(modrm_addr16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0), CPU[activeCPU].instructionbufferw, 2)) return;
	if (CPU8086_instructionstepdelayBIU(0, 2 + (MODRM_EA(CPU[activeCPU].params) ? 0 : 1))) return; //2 more cycles + 1 for non-memory!
}
void CPU8086_OP24()
{
	INLINEREGISTER byte theimm = CPU[activeCPU].immb;
	modrm_generateInstructionTEXT("AND AL,",0,theimm,PARAM_IMM8_PARAM);
	CPU8086_internal_AND8(&REG_AL,theimm,1);
}
void CPU8086_OP25()
{
	INLINEREGISTER word theimm = CPU[activeCPU].immw;
	modrm_generateInstructionTEXT("AND AX,",0,theimm,PARAM_IMM16_PARAM);
	CPU8086_internal_AND16(&REG_AX,theimm,1);
}
void CPU8086_OP27()
{
	modrm_generateInstructionTEXT("DAA",0,0,PARAM_NONE);/*DAA?*/
	CPU8086_internal_DAA();/*DAA?*/
}
void CPU8086_execute_SUB_modrmmodrm8()
{
	modrm_generateInstructionTEXT("SUB",8,0,PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep==0)) if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,1)) return;
	if (CPU8086_instructionstepreadmodrmb(0,&CPU[activeCPU].instructionbufferb,CPU[activeCPU].MODRM_src1)) return;
	if (CPU8086_internal_SUB8(modrm_addr8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0), CPU[activeCPU].instructionbufferb,2)) return;
	if (CPU8086_instructionstepdelayBIU(0, 2 + (MODRM_EA(CPU[activeCPU].params) ? 0 : 1))) return; //2 more cycles + 1 for non-memory!
}
void CPU8086_execute_SUB_modrmmodrm16()
{
	modrm_generateInstructionTEXT("SUB", 16, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0xA0)) return;
	}
	if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src1)) return;
	if (CPU8086_internal_SUB16(modrm_addr16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0), CPU[activeCPU].instructionbufferw, 2)) return;
	if (CPU8086_instructionstepdelayBIU(0, 2 + (MODRM_EA(CPU[activeCPU].params) ? 0 : 1))) return; //2 more cycles + 1 for non-memory!
}
void CPU8086_OP2C()
{
	INLINEREGISTER byte theimm = CPU[activeCPU].immb;
	modrm_generateInstructionTEXT("SUB AL,",0,theimm,PARAM_IMM8_PARAM);/*4=AL,imm8*/
	CPU8086_internal_SUB8(&REG_AL,theimm,1);/*4=AL,imm8*/
}
void CPU8086_OP2D()
{
	INLINEREGISTER word theimm = CPU[activeCPU].immw;
	modrm_generateInstructionTEXT("SUB AX,",0,theimm,PARAM_IMM16_PARAM);/*5=AX,imm16*/
	CPU8086_internal_SUB16(&REG_AX,theimm,1);/*5=AX,imm16*/
}
void CPU8086_OP2F()
{
	modrm_generateInstructionTEXT("DAS",0,0,PARAM_NONE);/*DAS?*/
	CPU8086_internal_DAS();/*DAS?*/
}
void CPU8086_execute_XOR_modrmmodrm8()
{
	modrm_generateInstructionTEXT("XOR",8,0,PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep==0)) if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,1)) return;
	if (CPU8086_instructionstepreadmodrmb(0,&CPU[activeCPU].instructionbufferb,CPU[activeCPU].MODRM_src1)) return;
	if (CPU8086_internal_XOR8(modrm_addr8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0), CPU[activeCPU].instructionbufferb,2)) return;
	if (CPU8086_instructionstepdelayBIU(0, 2 + (MODRM_EA(CPU[activeCPU].params) ? 0 : 1))) return; //2 more cycles + 1 for non-memory!
}
void CPU8086_execute_XOR_modrmmodrm16()
{
	modrm_generateInstructionTEXT("XOR", 16, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0xA0)) return;
	}
	if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src1)) return;
	if (CPU8086_internal_XOR16(modrm_addr16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0), CPU[activeCPU].instructionbufferw, 2)) return;
	if (CPU8086_instructionstepdelayBIU(0, 2 + (MODRM_EA(CPU[activeCPU].params) ? 0 : 1))) return; //2 more cycles + 1 for non-memory!
}
void CPU8086_OP34()
{
	INLINEREGISTER byte theimm = CPU[activeCPU].immb;
	modrm_generateInstructionTEXT("XOR AL,",0,theimm,PARAM_IMM8_PARAM);
	CPU8086_internal_XOR8(&REG_AL,theimm,1);
}
void CPU8086_OP35()
{
	INLINEREGISTER word theimm = CPU[activeCPU].immw;
	modrm_generateInstructionTEXT("XOR AX,",0,theimm,PARAM_IMM16_PARAM);
	CPU8086_internal_XOR16(&REG_AX,theimm,1);
}
void CPU8086_OP37()
{
	modrm_generateInstructionTEXT("AAA",0,0,PARAM_NONE);/*AAA?*/
	CPU8086_internal_AAA();/*AAA?*/
}
void CPU8086_execute_CMP_modrmmodrm8() 
{
	modrm_generateInstructionTEXT("CMP",8,0,PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep==0))
	{
		if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,1|0x40)) return;
		if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,1|0x40)) return;
		if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,1|0xA0)) return;
		if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,1|0xA0)) return;
	}
	if (CPU8086_instructionstepreadmodrmb(0,&CPU[activeCPU].instructionbufferb,CPU[activeCPU].MODRM_src0)) return;
	if (CPU8086_instructionstepreadmodrmb(2,&CPU[activeCPU].instructionbufferb2,CPU[activeCPU].MODRM_src1)) return;
	CMP_b(CPU[activeCPU].instructionbufferb, CPU[activeCPU].instructionbufferb2,2);
	if (CPU8086_instructionstepwaitBIUfinished(4)) return; //Wait for the BIU to finish it's timing!
	if (CPU8086_instructionstepdelayBIU(0, 3)) return; //2 more cycles + 1 for CMP!
}
void CPU8086_execute_CMP_modrmmodrm16()
{
	modrm_generateInstructionTEXT("CMP",16,0,PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep==0))
	{
		if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,1|0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,1|0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,1|0xA0)) return;
		if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,1|0xA0)) return;
	}
	if (CPU8086_instructionstepreadmodrmw(0,&CPU[activeCPU].instructionbufferw,CPU[activeCPU].MODRM_src0)) return;
	if (CPU8086_instructionstepreadmodrmw(2,&CPU[activeCPU].instructionbufferw2,CPU[activeCPU].MODRM_src1)) return;
	CMP_w(CPU[activeCPU].instructionbufferw, CPU[activeCPU].instructionbufferw2,2);
	if (CPU8086_instructionstepwaitBIUfinished(4)) return; //Wait for the BIU to finish it's timing!
	if (CPU8086_instructionstepdelayBIU(0, 3)) return; //2 more cycles + 1 for CMP!
}
void CPU8086_OP3C()
{
	INLINEREGISTER byte theimm = CPU[activeCPU].immb;
	modrm_generateInstructionTEXT("CMP AL,",0,theimm,PARAM_IMM8_PARAM);/*CMP AL, imm8*/
	CMP_b(REG_AL,theimm,1);/*CMP AL, imm8*/
}
void CPU8086_OP3D()
{
	INLINEREGISTER word theimm = CPU[activeCPU].immw;
	modrm_generateInstructionTEXT("CMP AX,",0,theimm,PARAM_IMM16_PARAM);/*CMP AX, imm16*/
	CMP_w(REG_AX,theimm,1);/*CMP AX, imm16*/
}
void CPU8086_OP3F()
{
	modrm_generateInstructionTEXT("AAS",0,0,PARAM_NONE);/*AAS?*/
	CPU8086_internal_AAS();/*AAS?*/
}
void CPU8086_OP40()
{
	modrm_generateInstructionTEXT("INC AX",0,0,PARAM_NONE);/*INC AX*/
	CPU8086_internal_INC16(&REG_AX);/*INC AX*/
}
void CPU8086_OP41()
{
	modrm_generateInstructionTEXT("INC CX",0,0,PARAM_NONE);/*INC CX*/
	CPU8086_internal_INC16(&REG_CX);/*INC CX*/
}
void CPU8086_OP42()
{
	modrm_generateInstructionTEXT("INC DX",0,0,PARAM_NONE);/*INC DX*/
	CPU8086_internal_INC16(&REG_DX);/*INC DX*/
}
void CPU8086_OP43()
{
	modrm_generateInstructionTEXT("INC BX",0,0,PARAM_NONE);/*INC BX*/
	CPU8086_internal_INC16(&REG_BX);/*INC BX*/
}
void CPU8086_OP44()
{
	modrm_generateInstructionTEXT("INC SP",0,0,PARAM_NONE);/*INC SP*/
	CPU8086_internal_INC16(&REG_SP);/*INC SP*/
}
void CPU8086_OP45()
{
	modrm_generateInstructionTEXT("INC BP",0,0,PARAM_NONE);/*INC BP*/
	CPU8086_internal_INC16(&REG_BP);/*INC BP*/
}
void CPU8086_OP46()
{
	modrm_generateInstructionTEXT("INC SI",0,0,PARAM_NONE);/*INC SI*/
	CPU8086_internal_INC16(&REG_SI);/*INC SI*/
}
void CPU8086_OP47()
{
	modrm_generateInstructionTEXT("INC DI",0,0,PARAM_NONE);/*INC DI*/
	CPU8086_internal_INC16(&REG_DI);/*INC DI*/
}
void CPU8086_OP48()
{
	modrm_generateInstructionTEXT("DEC AX",0,0,PARAM_NONE);/*DEC AX*/
	CPU8086_internal_DEC16(&REG_AX);/*DEC AX*/
}
void CPU8086_OP49()
{
	modrm_generateInstructionTEXT("DEC CX",0,0,PARAM_NONE);/*DEC CX*/
	CPU8086_internal_DEC16(&REG_CX);/*DEC CX*/
}
void CPU8086_OP4A()
{
	modrm_generateInstructionTEXT("DEC DX",0,0,PARAM_NONE);/*DEC DX*/
	CPU8086_internal_DEC16(&REG_DX);/*DEC DX*/
}
void CPU8086_OP4B()
{
	modrm_generateInstructionTEXT("DEC BX",0,0,PARAM_NONE);/*DEC BX*/
	CPU8086_internal_DEC16(&REG_BX);/*DEC BX*/
}
void CPU8086_OP4C()
{
	modrm_generateInstructionTEXT("DEC SP",0,0,PARAM_NONE);/*DEC SP*/
	CPU8086_internal_DEC16(&REG_SP);/*DEC SP*/
}
void CPU8086_OP4D()
{
	modrm_generateInstructionTEXT("DEC BP",0,0,PARAM_NONE);/*DEC BP*/
	CPU8086_internal_DEC16(&REG_BP);/*DEC BP*/
}
void CPU8086_OP4E()
{
	modrm_generateInstructionTEXT("DEC SI",0,0,PARAM_NONE);/*DEC SI*/
	CPU8086_internal_DEC16(&REG_SI);/*DEC SI*/
}
void CPU8086_OP4F()
{
	modrm_generateInstructionTEXT("DEC DI",0,0,PARAM_NONE);/*DEC DI*/
	CPU8086_internal_DEC16(&REG_DI);/*DEC DI*/
}
void CPU8086_OP50()
{
	modrm_generateInstructionTEXT("PUSH AX",0,0,PARAM_NONE);/*PUSH AX*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,1,0)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU8086_instructionstepdelayBIU(0, 3)) return; //Wait 3 cycles before activating the BIU!
	if (CPU8086_PUSHw(2,&REG_AX,0)) return; /*PUSH AX*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 0; /*Push Reg!*/
	}
}
void CPU8086_OP51()
{
	modrm_generateInstructionTEXT("PUSH CX",0,0,PARAM_NONE);/*PUSH CX*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,1,0)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU8086_instructionstepdelayBIU(0, 3)) return; //Wait 3 cycles before activating the BIU!
	if (CPU8086_PUSHw(2,&REG_CX,0)) return; /*PUSH CX*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 0; /*Push Reg!*/
	}
}
void CPU8086_OP52()
{
	modrm_generateInstructionTEXT("PUSH DX",0,0,PARAM_NONE);/*PUSH DX*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,1,0)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU8086_instructionstepdelayBIU(0, 3)) return; //Wait 3 cycles before activating the BIU!
	if (CPU8086_PUSHw(2,&REG_DX,0)) return; /*PUSH DX*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 0; /*Push Reg!*/
	}
}
void CPU8086_OP53()
{
	modrm_generateInstructionTEXT("PUSH BX",0,0,PARAM_NONE);/*PUSH BX*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,1,0)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU8086_instructionstepdelayBIU(0, 3)) return; //Wait 3 cycles before activating the BIU!
	if (CPU8086_PUSHw(2,&REG_BX,0)) return; /*PUSH BX*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 0; /*Push Reg!*/
	}
}
void CPU8086_OP54()
{
	modrm_generateInstructionTEXT("PUSH SP",0,0,PARAM_NONE);/*PUSH SP*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,1,0)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU8086_instructionstepdelayBIU(0, 3)) return; //Wait 3 cycles before activating the BIU!
	if (CPU8086_PUSHw(2,&REG_SP,0)) return; /*PUSH SP*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 0; /*Push Reg!*/
	}
}
void CPU8086_OP55()
{
	modrm_generateInstructionTEXT("PUSH BP",0,0,PARAM_NONE);/*PUSH BP*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,1,0)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU8086_instructionstepdelayBIU(0, 3)) return; //Wait 3 cycles before activating the BIU!
	if (CPU8086_PUSHw(2,&REG_BP,0)) return; /*PUSH BP*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 0; /*Push Reg!*/
	}
}
void CPU8086_OP56()
{
	modrm_generateInstructionTEXT("PUSH SI",0,0,PARAM_NONE);/*PUSH SI*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,1,0)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU8086_instructionstepdelayBIU(0, 3)) return; //Wait 3 cycles before activating the BIU!
	if (CPU8086_PUSHw(2,&REG_SI,0)) return; /*PUSH SI*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 0; /*Push Reg!*/
	}
}
void CPU8086_OP57()
{
	modrm_generateInstructionTEXT("PUSH DI",0,0,PARAM_NONE);/*PUSH DI*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,1,0)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU8086_instructionstepdelayBIU(0, 3)) return; //Wait 3 cycles before activating the BIU!
	if (CPU8086_PUSHw(2,&REG_DI,0)) return; /*PUSH DI*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 0; /*Push Reg!*/
	}
}
void CPU8086_OP58()
{
	modrm_generateInstructionTEXT("POP AX",0,0,PARAM_NONE);/*POP AX*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,0,0)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU8086_instructionstepPOPtimeout(0)) return; /*POP timeout*/
	if (CPU8086_POPw(2,&REG_AX,0)) return; /*POP AX*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 3; /*Pop Reg!*/
	}
}
void CPU8086_OP59()
{
	modrm_generateInstructionTEXT("POP CX",0,0,PARAM_NONE);/*POP CX*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,0,0)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU8086_instructionstepPOPtimeout(0)) return; /*POP timeout*/
	if (CPU8086_POPw(2,&REG_CX,0)) return; /*POP CX*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 3; /*Pop Reg!*/
	}
}
void CPU8086_OP5A()
{
	modrm_generateInstructionTEXT("POP DX",0,0,PARAM_NONE);/*POP DX*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,0,0)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU8086_instructionstepPOPtimeout(0)) return; /*POP timeout*/
	if (CPU8086_POPw(2,&REG_DX,0)) return; /*POP DX*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 3; /*Pop Reg!*/
	}
}
void CPU8086_OP5B()
{
	modrm_generateInstructionTEXT("POP BX",0,0,PARAM_NONE);/*POP BX*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,0,0)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU8086_instructionstepPOPtimeout(0)) return; /*POP timeout*/
	if (CPU8086_POPw(2,&REG_BX,0)) return; /*POP BX*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 3; /*Pop Reg!*/
	}
}
void CPU8086_OP5C()
{
	modrm_generateInstructionTEXT("POP SP",0,0,PARAM_NONE);/*POP SP*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,0,0)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU8086_instructionstepPOPtimeout(0)) return; /*POP timeout*/
	if (CPU8086_POPSP(2)) return; /*POP SP*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 3; /*Pop Reg!*/
	}
}
void CPU8086_OP5D()
{
	modrm_generateInstructionTEXT("POP BP",0,0,PARAM_NONE);/*POP BP*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,0,0)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU8086_instructionstepPOPtimeout(0)) return; /*POP timeout*/
	if (CPU8086_POPw(2,&REG_BP,0)) return; /*POP BP*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 3; /*Pop Reg!*/
	}
}
void CPU8086_OP5E()
{
	modrm_generateInstructionTEXT("POP SI",0,0,PARAM_NONE);/*POP SI*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,0,0)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU8086_instructionstepPOPtimeout(0)) return; /*POP timeout*/
	if (CPU8086_POPw(2,&REG_SI,0)) return;/*POP SI*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 3; /*Pop Reg!*/
	}
}
void CPU8086_OP5F()
{
	modrm_generateInstructionTEXT("POP DI",0,0,PARAM_NONE);/*POP DI*/
	if (unlikely(CPU[activeCPU].stackchecked==0)) 
	{
		if (checkStackAccess(1,0,0)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU8086_instructionstepPOPtimeout(0)) return; /*POP timeout*/
	if (CPU8086_POPw(2,&REG_DI,0)) return;/*POP DI*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 3; /*Pop Reg!*/
	}
}
void CPU8086_OP70()
{
	INLINEREGISTER sbyte rel8;/*JO rel8: (OF=1)*/
	rel8 = imm8();
	modrm_generateInstructionTEXT("JO",0,((REG_EIP + rel8)&CPU_EIPmask(0)),CPU_EIPSize(0)); /* JUMP to destination? */
	if (FLAG_OF)
	{
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			if (CPU8086_instructionstepdelayBIU(0, 2)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepdelayBIU(2, 3)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepwaitBIUready(4)) return; //Wait to become ready!
			if (CPU8086_instructionstepdelayBIU(6, 2)) return; //2 more cycles here!
		}
		CPU_JMPrel((int_32)rel8, 0); /* JUMP to destination? */
		CPU_flushPIQ(-1); /*We're jumping to another address*/
		CPU[activeCPU].didJump = 1;
		/* Branch taken */
	}
	else
	{
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 1; /* Branch not taken */
		}
	}
}
void CPU8086_OP71()
{
	INLINEREGISTER sbyte rel8;/*JNO rel8 : (OF=0)*/
	rel8 = imm8();
	modrm_generateInstructionTEXT("JNO",0, ((REG_EIP + rel8)&CPU_EIPmask(0)),CPU_EIPSize(0)); /* JUMP to destination? */
	if (!FLAG_OF)
	{
		if (CPU_apply286cycles() == 0) /* No 80286+ cycles instead? */
		{
			if (CPU8086_instructionstepdelayBIU(0, 2)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepdelayBIU(2, 3)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepwaitBIUready(4)) return; //Wait to become ready!
			if (CPU8086_instructionstepdelayBIU(6, 2)) return; //2 more cycles here!
		}
		CPU_JMPrel((int_32)rel8,0); /* JUMP to destination? */
		CPU_flushPIQ(-1); /*We're jumping to another address*/
		CPU[activeCPU].didJump = 1;
		/* Branch taken */
	}
	else
	{
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 1; /* Branch not taken */
		}
	}
}
void CPU8086_OP72()
{
	INLINEREGISTER sbyte rel8;/*JC rel8: (CF=1)*/
	rel8 = imm8();
	modrm_generateInstructionTEXT("JC",0, ((REG_EIP + rel8)&CPU_EIPmask(0)),CPU_EIPSize(0)); /* JUMP to destination? */
	if (FLAG_CF)
	{
		if (CPU_apply286cycles() == 0) /* No 80286+ cycles instead? */
		{
			if (CPU8086_instructionstepdelayBIU(0, 2)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepdelayBIU(2, 3)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepwaitBIUready(4)) return; //Wait to become ready!
			if (CPU8086_instructionstepdelayBIU(6, 2)) return; //2 more cycles here!
		}
		CPU_JMPrel((int_32)rel8,0); /* JUMP to destination? */
		CPU_flushPIQ(-1); /*We're jumping to another address*/
		CPU[activeCPU].didJump = 1;
		/* Branch taken */
	}
	else
	{
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 1; /* Branch not taken */
		}
	}
}
void CPU8086_OP73()
{
	INLINEREGISTER sbyte rel8;/*JNC rel8 : (CF=0)*/
	rel8 = imm8();
	modrm_generateInstructionTEXT("JNC",0, ((REG_EIP + rel8)&CPU_EIPmask(0)),CPU_EIPSize(0)); /* JUMP to destination? */
	if (!FLAG_CF)
	{
		if (CPU_apply286cycles() == 0) /* No 80286+ cycles instead? */
		{
			if (CPU8086_instructionstepdelayBIU(0, 2)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepdelayBIU(2, 3)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepwaitBIUready(4)) return; //Wait to become ready!
			if (CPU8086_instructionstepdelayBIU(6, 2)) return; //2 more cycles here!
		}
		CPU_JMPrel((int_32)rel8,0); /* JUMP to destination? */
		CPU_flushPIQ(-1); /*We're jumping to another address*/
		CPU[activeCPU].didJump = 1;
		/* Branch taken */
	}
	else
	{
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 1; /* Branch not taken */
		}
	}
}
void CPU8086_OP74()
{
	INLINEREGISTER sbyte rel8;/*JZ rel8: (ZF=1)*/
	rel8 = imm8();
	modrm_generateInstructionTEXT("JZ",0, ((REG_EIP + rel8)&CPU_EIPmask(0)),CPU_EIPSize(0)); /* JUMP to destination? */
	if (FLAG_ZF)
	{
		if (CPU_apply286cycles() == 0) /* No 80286+ cycles instead? */
		{
			if (CPU8086_instructionstepdelayBIU(0, 2)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepdelayBIU(2, 3)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepwaitBIUready(4)) return; //Wait to become ready!
			if (CPU8086_instructionstepdelayBIU(6, 2)) return; //2 more cycles here!
		}
		CPU_JMPrel((int_32)rel8,0); /* JUMP to destination? */
		CPU_flushPIQ(-1); /*We're jumping to another address*/
		CPU[activeCPU].didJump = 1;
		/* Branch taken */
	}
	else
	{
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 1; /* Branch not taken */
		}
	}
}
void CPU8086_OP75()
{
	INLINEREGISTER sbyte rel8;/*JNZ rel8 : (ZF=0)*/
	rel8 = imm8();
	modrm_generateInstructionTEXT("JNZ",0, ((REG_EIP + rel8)&CPU_EIPmask(0)),CPU_EIPSize(0)); /* JUMP to destination? */
	if (!FLAG_ZF)
	{
		if (CPU_apply286cycles() == 0) /* No 80286+ cycles instead? */
		{
			if (CPU8086_instructionstepdelayBIU(0, 2)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepdelayBIU(2, 3)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepwaitBIUready(4)) return; //Wait to become ready!
			if (CPU8086_instructionstepdelayBIU(6, 2)) return; //2 more cycles here!
		}
		CPU_JMPrel((int_32)rel8,0); /* JUMP to destination? */
		CPU_flushPIQ(-1); /*We're jumping to another address*/
		CPU[activeCPU].didJump = 1;
		/* Branch taken */
	}
	else
	{
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 1; /* Branch not taken */
		}
	}
}
void CPU8086_OP76()
{
	INLINEREGISTER sbyte rel8;/*JNA rel8 : (CF=1|ZF=1)*/
	rel8 = imm8();
	modrm_generateInstructionTEXT("JBE",0, ((REG_EIP + rel8)&CPU_EIPmask(0)),CPU_EIPSize(0)); /* JUMP to destination? */
	if (FLAG_CF||FLAG_ZF)
	{
		if (CPU_apply286cycles() == 0) /* No 80286+ cycles instead? */
		{
			if (CPU8086_instructionstepdelayBIU(0, 2)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepdelayBIU(2, 3)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepwaitBIUready(4)) return; //Wait to become ready!
			if (CPU8086_instructionstepdelayBIU(6, 2)) return; //2 more cycles here!
		}
		CPU_JMPrel((int_32)rel8,0); /* JUMP to destination? */
		CPU_flushPIQ(-1); /*We're jumping to another address*/
		CPU[activeCPU].didJump = 1;
		/* Branch taken */
	}
	else
	{
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 1; /* Branch not taken */
		}
	}
}
void CPU8086_OP77()
{
	INLINEREGISTER sbyte rel8;/*JA rel8: (CF=0&ZF=0)*/
	rel8 = imm8();
	modrm_generateInstructionTEXT("JNBE",0, ((REG_EIP + rel8)&CPU_EIPmask(0)),CPU_EIPSize(0)); /* JUMP to destination? */
	if (!(FLAG_CF|FLAG_ZF))
	{
		if (CPU_apply286cycles() == 0) /* No 80286+ cycles instead? */
		{
			if (CPU8086_instructionstepdelayBIU(0, 2)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepdelayBIU(2, 3)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepwaitBIUready(4)) return; //Wait to become ready!
			if (CPU8086_instructionstepdelayBIU(6, 2)) return; //2 more cycles here!
		}
		CPU_JMPrel((int_32)rel8,0); /* JUMP to destination? */
		CPU_flushPIQ(-1); /*We're jumping to another address*/
		CPU[activeCPU].didJump = 1;
		/* Branch taken */
	}
	else
	{
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 1; /* Branch not taken */
		}
	}
}
void CPU8086_OP78()
{
	INLINEREGISTER sbyte rel8;/*JS rel8: (SF=1)*/
	rel8 = imm8();
	modrm_generateInstructionTEXT("JS",0, ((REG_EIP + rel8)&CPU_EIPmask(0)),CPU_EIPSize(0)); /* JUMP to destination? */
	if (FLAG_SF)
	{
		if (CPU_apply286cycles() == 0) /* No 80286+ cycles instead? */
		{
			if (CPU8086_instructionstepdelayBIU(0, 2)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepdelayBIU(2, 3)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepwaitBIUready(4)) return; //Wait to become ready!
			if (CPU8086_instructionstepdelayBIU(6, 2)) return; //2 more cycles here!
		}
		CPU_JMPrel((int_32)rel8,0); /* JUMP to destination? */
		CPU_flushPIQ(-1); /*We're jumping to another address*/
		CPU[activeCPU].didJump = 1;
		/* Branch taken */
	}
	else
	{
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 1; /* Branch not taken */
		}
	}
}
void CPU8086_OP79()
{
	INLINEREGISTER sbyte rel8;/*JNS rel8 : (SF=0)*/
	rel8 = imm8();
	modrm_generateInstructionTEXT("JNS",0, ((REG_EIP + rel8)&CPU_EIPmask(0)),CPU_EIPSize(0)); /* JUMP to destination? */
	if (!FLAG_SF)
	{
		if (CPU_apply286cycles() == 0) /* No 80286+ cycles instead? */
		{
			if (CPU8086_instructionstepdelayBIU(0, 2)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepdelayBIU(2, 3)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepwaitBIUready(4)) return; //Wait to become ready!
			if (CPU8086_instructionstepdelayBIU(6, 2)) return; //2 more cycles here!
		}
		CPU_JMPrel((int_32)rel8,0); /* JUMP to destination? */
		CPU_flushPIQ(-1); /*We're jumping to another address*/
		CPU[activeCPU].didJump = 1;
		/* Branch taken */
	}
	else
	{
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 1; /* Branch not taken */
		}
	}
}
void CPU8086_OP7A()
{
	INLINEREGISTER sbyte rel8;/*JP rel8 : (PF=1)*/
	rel8 = imm8();
	modrm_generateInstructionTEXT("JP",0, ((REG_EIP + rel8)&CPU_EIPmask(0)),CPU_EIPSize(0)); /* JUMP to destination? */
	if (FLAG_PF)
	{
		if (CPU_apply286cycles() == 0) /* No 80286+ cycles instead? */
		{
			if (CPU8086_instructionstepdelayBIU(0, 2)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepdelayBIU(2, 3)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepwaitBIUready(4)) return; //Wait to become ready!
			if (CPU8086_instructionstepdelayBIU(6, 2)) return; //2 more cycles here!
		}
		CPU_JMPrel((int_32)rel8,0); /* JUMP to destination? */
		CPU_flushPIQ(-1); /*We're jumping to another address*/
		CPU[activeCPU].didJump = 1;
		/* Branch taken */
	}
	else
	{
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 1; /* Branch not taken */
		}
	}
}
void CPU8086_OP7B()
{
	INLINEREGISTER sbyte rel8;/*JNP rel8 : (PF=0)*/
	rel8 = imm8();
	modrm_generateInstructionTEXT("JNP",0, ((REG_EIP + rel8)&CPU_EIPmask(0)),CPU_EIPSize(0)); /* JUMP to destination? */
	if (!FLAG_PF)
	{
		if (CPU_apply286cycles() == 0) /* No 80286+ cycles instead? */
		{
			if (CPU8086_instructionstepdelayBIU(0, 2)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepdelayBIU(2, 3)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepwaitBIUready(4)) return; //Wait to become ready!
			if (CPU8086_instructionstepdelayBIU(6, 2)) return; //2 more cycles here!
		}
		CPU_JMPrel((int_32)rel8,0); /* JUMP to destination? */
		CPU_flushPIQ(-1); /*We're jumping to another address*/
		CPU[activeCPU].didJump = 1;
		/* Branch taken */
	}
	else
	{
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 1; /* Branch not taken */
		}
	}
}
void CPU8086_OP7C()
{
	INLINEREGISTER sbyte rel8;/*JL rel8: (SF!=OF)*/
	rel8 = imm8();
	modrm_generateInstructionTEXT("JL",0, ((REG_EIP + rel8)&CPU_EIPmask(0)),CPU_EIPSize(0)); /* JUMP to destination? */
	if (FLAG_SF!=FLAG_OF)
	{
		if (CPU_apply286cycles() == 0) /* No 80286+ cycles instead? */
		{
			if (CPU8086_instructionstepdelayBIU(0, 2)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepdelayBIU(2, 3)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepwaitBIUready(4)) return; //Wait to become ready!
			if (CPU8086_instructionstepdelayBIU(6, 2)) return; //2 more cycles here!
		}
		CPU_JMPrel((int_32)rel8,0);
		/* JUMP to destination? */
		CPU_flushPIQ(-1); /*We're jumping to another address*/
		CPU[activeCPU].didJump = 1;
		/* Branch taken */
	}
	else
	{
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 1; /* Branch not taken */
		}
	}
}
void CPU8086_OP7D()
{
	INLINEREGISTER sbyte rel8;/*JGE rel8 : (SF=OF)*/
	rel8 = imm8();
	modrm_generateInstructionTEXT("JGE",0, ((REG_EIP + rel8)&CPU_EIPmask(0)),CPU_EIPSize(0)); /* JUMP to destination? */
	if (FLAG_SF==FLAG_OF)
	{
		if (CPU_apply286cycles() == 0) /* No 80286+ cycles instead? */
		{
			if (CPU8086_instructionstepdelayBIU(0, 2)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepdelayBIU(2, 3)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepwaitBIUready(4)) return; //Wait to become ready!
			if (CPU8086_instructionstepdelayBIU(6, 2)) return; //2 more cycles here!
		}
		CPU_JMPrel((int_32)rel8,0); /* JUMP to destination? */
		CPU_flushPIQ(-1); /*We're jumping to another address*/
		CPU[activeCPU].didJump = 1;
		/* Branch taken */
	}
	else
	{
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 1; /* Branch not taken */
		}
	}
}
void CPU8086_OP7E()
{
	INLINEREGISTER sbyte rel8;/*JLE rel8 : (ZF|(SF!=OF))*/
	rel8 = imm8();
	modrm_generateInstructionTEXT("JLE",0, ((REG_EIP + rel8)&CPU_EIPmask(0)),CPU_EIPSize(0)); /* JUMP to destination? */
	if ((FLAG_SF!=FLAG_OF) || FLAG_ZF)
	{
		if (CPU_apply286cycles() == 0) /* No 80286+ cycles instead? */
		{
			if (CPU8086_instructionstepdelayBIU(0, 2)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepdelayBIU(2, 3)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepwaitBIUready(4)) return; //Wait to become ready!
			if (CPU8086_instructionstepdelayBIU(6, 2)) return; //2 more cycles here!
		}
		CPU_JMPrel((int_32)rel8,0); /* JUMP to destination? */
		CPU_flushPIQ(-1); /*We're jumping to another address*/
		CPU[activeCPU].didJump = 1;
		/* Branch taken */
	}
	else
	{
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 1; /* Branch not taken */
		}
	}
}
void CPU8086_OP7F()
{
	INLINEREGISTER sbyte rel8;/*JG rel8: ((ZF=0)&&(SF=OF))*/
	rel8 = imm8();
	modrm_generateInstructionTEXT("JG",0, ((REG_EIP + rel8)&CPU_EIPmask(0)),CPU_EIPSize(0)); /* JUMP to destination? */
	if ((!FLAG_ZF) && (FLAG_SF==FLAG_OF))
	{
		if (CPU_apply286cycles() == 0) /* No 80286+ cycles instead? */
		{
			if (CPU8086_instructionstepdelayBIU(0, 2)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepdelayBIU(2, 3)) return; //First, 3 cycles NOP!
			if (CPU8086_instructionstepwaitBIUready(4)) return; //Wait to become ready!
			if (CPU8086_instructionstepdelayBIU(6, 2)) return; //2 more cycles here!
		}
		CPU_JMPrel((int_32)rel8,0); /* JUMP to destination? */
		CPU_flushPIQ(-1); /*We're jumping to another address*/
		CPU[activeCPU].didJump = 1;
		/* Branch taken */
	}
	else
	{
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 1; /* Branch not taken */
		}
	}
}
void CPU8086_OP84()
{
	modrm_generateInstructionTEXT("TEST",8,0,PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep==0))
	{
		if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,1|0x40)) return;
		if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,1|0x40)) return;
		if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,1|0xA0)) return;
		if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,1|0xA0)) return;
	}
	if (CPU8086_instructionstepreadmodrmb(0,&CPU[activeCPU].instructionbufferb,CPU[activeCPU].MODRM_src0)) return;
	if (CPU8086_instructionstepreadmodrmb(2,&CPU[activeCPU].instructionbufferb2,CPU[activeCPU].MODRM_src1)) return;
	CPU8086_internal_TEST8(CPU[activeCPU].instructionbufferb, CPU[activeCPU].instructionbufferb2,2);
}
void CPU8086_OP85()
{
	modrm_generateInstructionTEXT("TEST",16,0,PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep==0))
	{
		if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,1|0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,1|0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,1|0xA0)) return;
		if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,1|0xA0)) return;
	}
	if (CPU8086_instructionstepreadmodrmw(0,&CPU[activeCPU].instructionbufferw,CPU[activeCPU].MODRM_src0)) return;
	if (CPU8086_instructionstepreadmodrmw(2,&CPU[activeCPU].instructionbufferw2,CPU[activeCPU].MODRM_src1)) return;
	CPU8086_internal_TEST16(CPU[activeCPU].instructionbufferw, CPU[activeCPU].instructionbufferw2,2);
}
void CPU8086_OP86()
{
	modrm_generateInstructionTEXT("XCHG",8,0,PARAM_MODRM_01);
	CPU8086_internal_XCHG8(modrm_addr8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0),modrm_addr8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,1),2); /*XCHG reg8,r/m8*/
}
void CPU8086_OP87()
{
	modrm_generateInstructionTEXT("XCHG",16,0,PARAM_MODRM_01);
	CPU8086_internal_XCHG16(modrm_addr16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0),modrm_addr16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,0),2); /*XCHG reg16,r/m16*/
}
void CPU8086_execute_MOV_modrmmodrm8()
{
	modrm_generateInstructionTEXT("MOV",8,0,PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep==0)) if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,1)) return;
	if (CPU8086_instructionstepreadmodrmb(0,&CPU[activeCPU].instructionbufferb,CPU[activeCPU].MODRM_src1)) return;
	CPU8086_internal_MOV8(modrm_addr8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0), CPU[activeCPU].instructionbufferb,2);
}
void CPU8086_execute_MOV_modrmmodrm16()
{
	modrm_generateInstructionTEXT("MOV", 16, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0xA0)) return;
	}
	if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src1)) return;
	CPU8086_internal_MOV16(modrm_addr16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0), CPU[activeCPU].instructionbufferw, 2);
}
void CPU8086_execute_MOVSegRegMemory()
{
	modrm_generateInstructionTEXT("MOV", 16, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0xA0)) return;
	}
	if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src1)) return;
	if (CPU8086_internal_MOV16(modrm_addr16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0), CPU[activeCPU].instructionbufferw, 8)) return;
	if ((CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0].reg16 == &REG_SS) && (CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0].isreg == 1) && (CPU[activeCPU].previousAllowInterrupts))
	{
		CPU[activeCPU].allowInterrupts = 0; /* Inhabit all interrupts up to the next instruction */
	}
}
void CPU8086_OP8D()
{
	modrm_debugger16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, CPU[activeCPU].MODRM_src1);
	debugger_setcommand("LEA %s,%s", CPU[activeCPU].modrm_param1, getLEAtext(&CPU[activeCPU].params));
	if ((EMULATED_CPU >= CPU_NECV30) && !modrm_ismemory(CPU[activeCPU].params))
	{
		CPU_unkOP();
		return;
	}
	if (CPU8086_internal_MOV16(modrm_addr16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0), getLEA(&CPU[activeCPU].params), 0)) return;
	if (CPU_apply286cycles() == 0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 1+(modrm_ismemory(CPU[activeCPU].params)?2:0); /* Load effective address */
	}
}
void CPU8086_OP90()/*NOP*/
{
	modrm_generateInstructionTEXT("NOP",0,0,PARAM_NONE);/*NOP (XCHG AX,AX)*/
	if (CPU8086_internal_XCHG16(&REG_AX,&REG_AX,1)) return; /* NOP */
}
void CPU8086_OP91()
{
	modrm_generateInstructionTEXT("XCHG CX,AX",0,0,PARAM_NONE);/*XCHG CX,AX*/
	CPU8086_internal_XCHG16(&REG_CX,&REG_AX,1); /*XCHG CX,AX*/
}
void CPU8086_OP92()
{
	modrm_generateInstructionTEXT("XCHG DX,AX",0,0,PARAM_NONE);/*XCHG DX,AX*/
	CPU8086_internal_XCHG16(&REG_DX,&REG_AX,1); /*XCHG DX,AX*/
}
void CPU8086_OP93()
{
	modrm_generateInstructionTEXT("XCHG BX,AX",0,0,PARAM_NONE);/*XCHG BX,AX*/
	CPU8086_internal_XCHG16(&REG_BX,&REG_AX,1); /*XCHG BX,AX*/
}
void CPU8086_OP94()
{
	modrm_generateInstructionTEXT("XCHG SP,AX",0,0,PARAM_NONE);/*XCHG SP,AX*/
	CPU8086_internal_XCHG16(&REG_SP,&REG_AX,1); /*XCHG SP,AX*/
}
void CPU8086_OP95()
{
	modrm_generateInstructionTEXT("XCHG BP,AX",0,0,PARAM_NONE);/*XCHG BP,AX*/
	CPU8086_internal_XCHG16(&REG_BP,&REG_AX,1); /*XCHG BP,AX*/
}
void CPU8086_OP96()
{
	modrm_generateInstructionTEXT("XCHG SI,AX",0,0,PARAM_NONE);/*XCHG SI,AX*/
	CPU8086_internal_XCHG16(&REG_SI,&REG_AX,1); /*XCHG SI,AX*/
}
void CPU8086_OP97()
{
	modrm_generateInstructionTEXT("XCHG DI,AX",0,0,PARAM_NONE);/*XCHG DI,AX*/
	CPU8086_internal_XCHG16(&REG_DI,&REG_AX,1); /*XCHG DI,AX*/
}
void CPU8086_OP98()
{
	modrm_generateInstructionTEXT("CBW",0,0,PARAM_NONE);/*CBW : sign extend AL to AX*/
	CPU8086_internal_CBW();/*CBW : sign extend AL to AX (8088+)*/
}
void CPU8086_OP99()
{
	modrm_generateInstructionTEXT("CWD",0,0,PARAM_NONE);/*CWD : sign extend AX to DX::AX*/
	CPU8086_internal_CWD();/*CWD : sign extend AX to DX::AX (8088+)*/
}
void CPU8086_OP9A()
{
	/*CALL Ap*/ INLINEREGISTER uint_32 segmentoffset = CPU[activeCPU].imm32;
	debugger_setcommand(debugger_forceEIP()?"CALL %04X:%08X":"CALL %04x:%04x", (segmentoffset>>16), ((segmentoffset&0xFFFF)&CPU_EIPmask(0)));
	if (EMULATED_CPU <= CPU_NECV30) //8086 timings?
	{
		if (CPU8086_PUSHw(0, &REG_CS, 0)) return; //CS pushed!
		if (CPU8086_instructionstepdelayBIU(2, 5)) return; //Wait 5 cycles!
		if (CPU8086_instructionstepdelayBIU(4, 4)) return; //Wait 4 cycles!
		if (CPU8086_PUSHw(6, &REG_IP, 0)) return; //IP pushed!
		CPU[activeCPU].destEIP = (segmentoffset & 0xFFFF) & CPU_EIPmask(0); //IP destination!
		segmentWritten(CPU_SEGMENT_CS, (segmentoffset >> 16) & 0xFFFF, 0); //Set CS&IP!
	}
	else //Other CPU?
	{
		if (CPU8086_CALLF((segmentoffset >> 16) & 0xFFFF, (segmentoffset & 0xFFFF) & CPU_EIPmask(0))) return;
		if (CPU_apply286cycles() == 0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 28; /* Intersegment direct */
		}
	}
}
void CPU8086_OP9B()
{
	modrm_generateInstructionTEXT("WAIT",0,0,PARAM_NONE);/*WAIT : wait for TEST pin activity. (UNIMPLEMENTED)*/
	CPU[activeCPU].wait = 1;/*9B: WAIT : wait for TEST pin activity. (Edit: continue on interrupts or 8087+!!!)*/
}
void CPU8086_OP9C()
{
	modrm_generateInstructionTEXT("PUSHF",0,0,PARAM_NONE);/*PUSHF*/
	if (unlikely((getcpumode()==CPU_MODE_8086) && (FLAG_PL!=3)))
	{
		THROWDESCGP(0,0,0); return;
	} /*#GP fault!*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,1,0)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (EMULATED_CPU <= CPU_NECV30) //808X timings?
	{
		if (CPU8086_instructionstepdelayBIU(0, 3)) return; //3 cycles before starting the transfer!
	}
	if (CPU8086_PUSHw((EMULATED_CPU<=CPU_NECV30)?2:0,&REG_FLAGS,0)) return;
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		//CPU[activeCPU].cycles_OP += 10-EU_CYCLES_SUBSTRACT_ACCESSWRITE; /*PUSHF timing!*/
	}
}
void CPU8086_OP9D()
{
	modrm_generateInstructionTEXT("POPF", 0, 0, PARAM_NONE);/*POPF*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,0,0)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU8086_instructionstepPOPtimeout(0)) return; /*POP timeout*/
	if (CPU8086_POPw(2,&CPU[activeCPU].tempflagsw,0)) return;
	if (disallowPOPFI())
	{
		CPU[activeCPU].tempflagsw &= ~0x200;
		CPU[activeCPU].tempflagsw |= REG_FLAGS&0x200; /* Ignore any changes to the Interrupt flag! */
	}
	if (getCPL())
	{
		CPU[activeCPU].tempflagsw &= ~0x3000;
		CPU[activeCPU].tempflagsw |= REG_FLAGS&0x3000; /* Ignore any changes to the IOPL when not at CPL 0! */
	}
	REG_FLAGS = CPU[activeCPU].tempflagsw;
	updateCPUmode(); /*POPF*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 1; /*POPF timing!*/
	}
	CPU[activeCPU].allowTF = 0; /*Disallow TF to be triggered after the instruction!*/
	/*CPU[activeCPU].unaffectedRF = 1;*/
}
void CPU8086_OP9E()
{
	modrm_generateInstructionTEXT("SAHF", 0, 0, PARAM_NONE);/*SAHF : Save AH to lower half of FLAGS.*/
	REG_FLAGS = ((REG_FLAGS & 0xFF00) | REG_AH);
	updateCPUmode(); /*SAHF : Save AH to lower half of FLAGS.*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 3; /*SAHF timing!*/
	}
}
void CPU8086_OP9F()
{
	modrm_generateInstructionTEXT("LAHF",0,0,PARAM_NONE);/*LAHF : Load lower half of FLAGS into AH.*/
	REG_AH = (REG_FLAGS&0xFF);/*LAHF : Load lower half of FLAGS into AH.*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 1; /*LAHF timing!*/
	}
}
void CPU8086_OPA0()
{
	INLINEREGISTER uint_32 theimm = CPU[activeCPU].immaddr32;
	debugger_setcommand("MOV AL,byte %s:[%04X]",CPU_textsegment(CPU_SEGMENT_DS), (theimm & CPU[activeCPU].address_size));/*MOV AL,[imm16]*/
	if (unlikely(CPU[activeCPU].modrmstep==0))
	{
		if (checkMMUaccess(CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),(theimm&CPU[activeCPU].address_size),1,getCPL(),1,0)) return;
	}
	if (CPU8086_instructionstepreaddirectb(0,CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS), (theimm & CPU[activeCPU].address_size),&CPU[activeCPU].instructionbufferb,1)) return;
	CPU8086_internal_MOV8(&REG_AL, CPU[activeCPU].instructionbufferb,1);/*MOV AL,[imm16]*/
}
void CPU8086_OPA1()
{
	INLINEREGISTER uint_32 theimm = CPU[activeCPU].immaddr32;
	debugger_setcommand("MOV AX,word %s:[%04X]",CPU_textsegment(CPU_SEGMENT_DS), (theimm & CPU[activeCPU].address_size));/*MOV AX,[imm16]*/
	if (unlikely(CPU[activeCPU].modrmstep==0))
	{
		if (checkMMUaccess16(CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),(theimm&CPU[activeCPU].address_size),1|0x40,getCPL(),1,0|0x8)) return;
		if (checkMMUaccess16(CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (theimm&CPU[activeCPU].address_size), 1|0xA0, getCPL(), 1, 0 | 0x8)) return;
	}
	if (CPU8086_instructionstepreaddirectw(0,CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS), (theimm & CPU[activeCPU].address_size),&CPU[activeCPU].instructionbufferw,1)) return;
	CPU8086_internal_MOV16(&REG_AX, CPU[activeCPU].instructionbufferw,1);/*MOV AX,[imm16]*/
}
void CPU8086_OPA2()
{
	INLINEREGISTER uint_32 theimm = CPU[activeCPU].immaddr32;
	debugger_setcommand("MOV byte %s:[%04X],AL",CPU_textsegment(CPU_SEGMENT_DS),theimm);/*MOV [imm16],AL*/
	CPU[activeCPU].custommem = 1;
	CPU[activeCPU].customoffset = (theimm&CPU[activeCPU].address_size);
	if (CPU8086_internal_MOV8(NULL,REG_AL,1)) return;/*MOV [imm16],AL*/
	CPU[activeCPU].custommem = 0;
}
void CPU8086_OPA3()
{
	INLINEREGISTER uint_32 theimm = CPU[activeCPU].immaddr32;
	debugger_setcommand("MOV word %s:[%04X],AX",CPU_textsegment(CPU_SEGMENT_DS),theimm);/*MOV [imm16], AX*/
	CPU[activeCPU].custommem = 1;
	CPU[activeCPU].customoffset = (theimm&CPU[activeCPU].address_size);
	if (CPU8086_internal_MOV16(NULL,REG_AX,1)) return;/*MOV [imm16], AX*/
	CPU[activeCPU].custommem = 0;
}
void CPU8086_OPA4()
{
	modrm_generateInstructionTEXT("MOVSB",0,0,PARAM_NONE);/*MOVSB*/
	CPU8086_internal_MOVSB();/*MOVSB*/
}
void CPU8086_OPA5()
{
	modrm_generateInstructionTEXT("MOVSW",0,0,PARAM_NONE);/*MOVSW*/
	CPU8086_internal_MOVSW();/*MOVSW*/
}
void CPU8086_OPA6()
{
	debugger_setcommand(CPU[activeCPU].CPU_Address_size?"CMPSB %s:[ESI],ES:[EDI]":"CMPSB %s:[SI],ES:[DI]",CPU_textsegment(CPU_SEGMENT_DS));/*CMPSB*/
	CPU8086_internal_CMPSB();/*CMPSB*/
}
void CPU8086_OPA7()
{
	debugger_setcommand(CPU[activeCPU].CPU_Address_size?"CMPSW %s:[ESI],ES:[EDI]":"CMPSW %s:[SI],ES:[DI]",CPU_textsegment(CPU_SEGMENT_DS));/*CMPSW*/
	CPU8086_internal_CMPSW();/*CMPSW*/
}
void CPU8086_OPA8()
{
	INLINEREGISTER byte theimm = CPU[activeCPU].immb;
	modrm_generateInstructionTEXT("TEST AL,",0,theimm,PARAM_IMM8_PARAM);/*TEST AL,imm8*/
	CPU8086_internal_TEST8(REG_AL,theimm,1);/*TEST AL,imm8*/
}
void CPU8086_OPA9()
{
	INLINEREGISTER word theimm = CPU[activeCPU].immw;
	modrm_generateInstructionTEXT("TEST AX,",0,theimm,PARAM_IMM16_PARAM);/*TEST AX,imm16*/
	CPU8086_internal_TEST16(REG_AX,theimm,1);/*TEST AX,imm16*/
}
void CPU8086_OPAA()
{
	modrm_generateInstructionTEXT("STOSB",0,0,PARAM_NONE);/*STOSB*/
	CPU8086_internal_STOSB();/*STOSB*/
}
void CPU8086_OPAB()
{
	modrm_generateInstructionTEXT("STOSW",0,0,PARAM_NONE);/*STOSW*/
	CPU8086_internal_STOSW();/*STOSW*/
}
void CPU8086_OPAC()
{
	modrm_generateInstructionTEXT("LODSB",0,0,PARAM_NONE);/*LODSB*/
	CPU8086_internal_LODSB();/*LODSB*/
}
void CPU8086_OPAD()
{
	modrm_generateInstructionTEXT("LODSW",0,0,PARAM_NONE);/*LODSW*/
	CPU8086_internal_LODSW();/*LODSW*/
}
void CPU8086_OPAE()
{
	modrm_generateInstructionTEXT("SCASB",0,0,PARAM_NONE);/*SCASB*/
	CPU8086_internal_SCASB();/*SCASB*/
}
void CPU8086_OPAF()
{
	modrm_generateInstructionTEXT("SCASW",0,0,PARAM_NONE);/*SCASW*/
	CPU8086_internal_SCASW();/*SCASW*/
}
void CPU8086_OPB0()
{
	INLINEREGISTER byte theimm = CPU[activeCPU].immb;
	modrm_generateInstructionTEXT("MOV AL,",0,theimm,PARAM_IMM8_PARAM);/*MOV AL,imm8*/
	CPU8086_internal_MOV8(&REG_AL,theimm,4);/*MOV AL,imm8*/
}
void CPU8086_OPB1()
{
	INLINEREGISTER byte theimm = CPU[activeCPU].immb;
	modrm_generateInstructionTEXT("MOV CL,",0,theimm,PARAM_IMM8_PARAM);/*MOV CL,imm8*/
	CPU8086_internal_MOV8(&REG_CL,theimm,4);/*MOV CL,imm8*/
}
void CPU8086_OPB2()
{
	INLINEREGISTER byte theimm = CPU[activeCPU].immb;
	modrm_generateInstructionTEXT("MOV DL,",0,theimm,PARAM_IMM8_PARAM);/*MOV DL,imm8*/
	CPU8086_internal_MOV8(&REG_DL,theimm,4);/*MOV DL,imm8*/
}
void CPU8086_OPB3()
{
	INLINEREGISTER byte theimm = CPU[activeCPU].immb;
	modrm_generateInstructionTEXT("MOV BL,",0,theimm,PARAM_IMM8_PARAM);/*MOV BL,imm8*/
	CPU8086_internal_MOV8(&REG_BL,theimm,4);/*MOV BL,imm8*/
}
void CPU8086_OPB4()
{
	INLINEREGISTER byte theimm = CPU[activeCPU].immb;
	modrm_generateInstructionTEXT("MOV AH,",0,theimm,PARAM_IMM8_PARAM);/*MOV AH,imm8*/
	CPU8086_internal_MOV8(&REG_AH,theimm,4);/*MOV AH,imm8*/
}
void CPU8086_OPB5()
{
	INLINEREGISTER byte theimm = CPU[activeCPU].immb;
	modrm_generateInstructionTEXT("MOV CH,",0,theimm,PARAM_IMM8_PARAM);/*MOV CH,imm8*/
	CPU8086_internal_MOV8(&REG_CH,theimm,4);/*MOV CH,imm8*/
}
void CPU8086_OPB6()
{
	INLINEREGISTER byte theimm = CPU[activeCPU].immb;
	modrm_generateInstructionTEXT("MOV DH,",0,theimm,PARAM_IMM8_PARAM);/*MOV DH,imm8*/
	CPU8086_internal_MOV8(&REG_DH,theimm,4);/*MOV DH,imm8*/
}
void CPU8086_OPB7()
{
	INLINEREGISTER byte theimm = CPU[activeCPU].immb;
	modrm_generateInstructionTEXT("MOV BH,",0,theimm,PARAM_IMM8_PARAM);/*MOV BH,imm8*/
	CPU8086_internal_MOV8(&REG_BH,theimm,4);/*MOV BH,imm8*/
}
void CPU8086_OPB8()
{
	INLINEREGISTER word theimm = CPU[activeCPU].immw;
	modrm_generateInstructionTEXT("MOV AX,",0,theimm,PARAM_IMM16_PARAM);/*MOV AX,imm16*/
	CPU8086_internal_MOV16(&REG_AX,theimm,4);/*MOV AX,imm16*/
}
void CPU8086_OPB9()
{
	INLINEREGISTER word theimm = CPU[activeCPU].immw;
	modrm_generateInstructionTEXT("MOV CX,",0,theimm,PARAM_IMM16_PARAM);/*MOV CX,imm16*/
	CPU8086_internal_MOV16(&REG_CX,theimm,4);/*MOV CX,imm16*/
}
void CPU8086_OPBA()
{
	INLINEREGISTER word theimm = CPU[activeCPU].immw;
	modrm_generateInstructionTEXT("MOV DX,",0,theimm,PARAM_IMM16_PARAM);/*MOV DX,imm16*/
	CPU8086_internal_MOV16(&REG_DX,theimm,4);/*MOV DX,imm16*/
}
void CPU8086_OPBB()
{
	INLINEREGISTER word theimm = CPU[activeCPU].immw;
	modrm_generateInstructionTEXT("MOV BX,",0,theimm,PARAM_IMM16_PARAM);/*MOV BX,imm16*/
	CPU8086_internal_MOV16(&REG_BX,theimm,4);/*MOV BX,imm16*/
}
void CPU8086_OPBC()
{
	INLINEREGISTER word theimm = CPU[activeCPU].immw;
	modrm_generateInstructionTEXT("MOV SP,",0,theimm,PARAM_IMM16_PARAM);/*MOV SP,imm16*/
	CPU8086_internal_MOV16(&REG_SP,theimm,4);/*MOV SP,imm16*/
}
void CPU8086_OPBD()
{
	INLINEREGISTER word theimm = CPU[activeCPU].immw;
	modrm_generateInstructionTEXT("MOV BP,",0,theimm,PARAM_IMM16_PARAM);/*MOV BP,imm16*/
	CPU8086_internal_MOV16(&REG_BP,theimm,4);/*MOV BP,imm16*/
}
void CPU8086_OPBE()
{
	INLINEREGISTER word theimm = CPU[activeCPU].immw;
	modrm_generateInstructionTEXT("MOV SI,",0,theimm,PARAM_IMM16_PARAM);/*MOV SI,imm16*/
	CPU8086_internal_MOV16(&REG_SI,theimm,4);/*MOV SI,imm16*/
}
void CPU8086_OPBF()
{
	INLINEREGISTER word theimm = CPU[activeCPU].immw;
	modrm_generateInstructionTEXT("MOV DI,",0,theimm,PARAM_IMM16_PARAM);/*MOV DI,imm16*/
	CPU8086_internal_MOV16(&REG_DI,theimm,4);/*MOV DI,imm16*/
}
void CPU8086_OPC2()
{
	INLINEREGISTER word popbytes = CPU[activeCPU].immw;/*RET imm16 (Near return to calling proc and POP imm16 bytes)*/
	modrm_generateInstructionTEXT("RET",0,popbytes,PARAM_IMM16); /*RET imm16 (Near return to calling proc and POP imm16 bytes)*/
	CPU8086_internal_RET(popbytes,1);
}
void CPU8086_OPC3()
{
	modrm_generateInstructionTEXT("RET",0,0,PARAM_NONE);/*RET (Near return to calling proc)*/ /*RET (Near return to calling proc)*/
	CPU8086_internal_RET(0,0);
}
void CPU8086_OPC4() /*LES modr/m*/
{
	modrm_generateInstructionTEXT("LES",16,0,PARAM_MODRM_01);
	CPU8086_internal_LXS(CPU_SEGMENT_ES); /*Load new ES!*/
}
void CPU8086_OPC5() /*LDS modr/m*/
{
	modrm_generateInstructionTEXT("LDS",16,0,PARAM_MODRM_01);
	CPU8086_internal_LXS(CPU_SEGMENT_DS); /*Load new DS!*/
}
void CPU8086_OPC6()
{
	if (MODRM_REG(CPU[activeCPU].params.modrm))
	{
		CPU_unkOP();
		return;
	}
	byte val = CPU[activeCPU].immb;
	modrm_debugger8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,CPU[activeCPU].MODRM_src1);
	debugger_setcommand("MOV %s,%02x", CPU[activeCPU].modrm_param1,val);
	if (unlikely(CPU[activeCPU].modrmstep==0)) if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0)) return;
	if (CPU8086_instructionstepwritemodrmb(0,val,CPU[activeCPU].MODRM_src0)) return;
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		if (MODRM_EA(CPU[activeCPU].params))
			CPU[activeCPU].cycles_OP += 3; /* Imm->Mem */
		else
			CPU[activeCPU].cycles_OP += 2; /* Imm->Reg */
	}
}
void CPU8086_OPC7()
{
	if (MODRM_REG(CPU[activeCPU].params.modrm))
	{
		CPU_unkOP();
		return;
	}
	word val = CPU[activeCPU].immw;
	modrm_debugger16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, CPU[activeCPU].MODRM_src1);
	debugger_setcommand("MOV %s,%04x", CPU[activeCPU].modrm_param1, val);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0xA0)) return;
	}
	if (CPU8086_instructionstepwritemodrmw(0, val, CPU[activeCPU].MODRM_src0, 0)) return;
	if (CPU_apply286cycles() == 0) /* No 80286+ cycles instead? */
	{
		if (MODRM_EA(CPU[activeCPU].params))
		{
			CPU[activeCPU].cycles_OP += 3; /* Imm->Mem */
		}
		else CPU[activeCPU].cycles_OP += 2; /* Imm->Reg */
	}
}
void CPU8086_OPCA()
{
	INLINEREGISTER word popbytes = CPU[activeCPU].immw;/*RETF imm16 (Far return to calling proc and pop imm16 bytes)*/
	modrm_generateInstructionTEXT("RETF",0,popbytes,PARAM_IMM16); /*RETF imm16 (Far return to calling proc and pop imm16 bytes)*/
	CPU8086_internal_RETF(popbytes,1);
}
void CPU8086_OPCB()
{
	modrm_generateInstructionTEXT("RETF",0,0,PARAM_NONE); /*RETF (Far return to calling proc)*/
	CPU8086_internal_RETF(0,0);
}
void CPU8086_OPCC()
{
	modrm_generateInstructionTEXT("INT 3",0,0,PARAM_NONE); /*INT 3*/
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger","#BP fault(-1)!");
	}

	if (CPU_faultraised(EXCEPTION_CPUBREAKPOINT))
	{
		CPU_executionphase_startinterrupt(EXCEPTION_CPUBREAKPOINT,0,-2);
	} /*INT 3*/
}
void CPU8086_OPCD()
{
	INLINEREGISTER byte theimm = CPU[activeCPU].immb;
	INTdebugger8086();
	modrm_generateInstructionTEXT("INT",0,theimm,PARAM_IMM8);/*INT imm8*/
	CPU_executionphase_startinterrupt(theimm,0,-2); /*INT imm8*/
}
void CPU8086_OPCE()
{
	modrm_generateInstructionTEXT("INTO",0,0,PARAM_NONE);/*INTO*/
	CPU8086_internal_INTO();/*INTO*/
}
void CPU8086_OPCF()
{
	modrm_generateInstructionTEXT("IRET",0,0,PARAM_NONE);/*IRET*/
	CPU8086_IRET();/*IRET : also restore interrupt flag!*/
}
void CPU8086_OPD4()
{
	INLINEREGISTER byte theimm = CPU[activeCPU].immb;
	modrm_generateInstructionTEXT("AAM",0,theimm,PARAM_IMM8);/*AAM*/
	CPU8086_internal_AAM(theimm);/*AAM*/
}
void CPU8086_OPD5()
{
	INLINEREGISTER byte theimm = CPU[activeCPU].immb;
	modrm_generateInstructionTEXT("AAD",0,theimm,PARAM_IMM8);/*AAD*/
	CPU8086_internal_AAD(theimm);/*AAD*/
}
void CPU8086_OPD6()
{
	debugger_setcommand("SALC");
	REG_AL=FLAG_CF?0xFF:0x00;
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 2;
	}
} //Special case on the 8086: SALC!
void CPU8086_OPD7()
{
	CPU8086_internal_XLAT();
}
void CPU8086_OPE0()
{
	INLINEREGISTER sbyte rel8;
	rel8 = imm8();
	modrm_generateInstructionTEXT("LOOPNZ",0, ((REG_EIP+rel8)&CPU_EIPmask(0)),CPU_EIPSize(0));
	if ((--REG_CX) && (!FLAG_ZF))
	{
		CPU_JMPrel((int_32)rel8,0);
		CPU_flushPIQ(-1); /*We're jumping to another address*/
		CPU[activeCPU].didJump = 1;
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 9;
		}
		/* Branch taken */
	}
	else
	{
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 4;
		}
		/* Branch not taken */
	}
}
void CPU8086_OPE1()
{
	INLINEREGISTER sbyte rel8;
	rel8 = imm8();
	modrm_generateInstructionTEXT("LOOPZ",0, ((REG_EIP+rel8)&CPU_EIPmask(0)),CPU_EIPSize(0));
	if ((--REG_CX) && (FLAG_ZF))
	{
		CPU_JMPrel((int_32)rel8,0);
		CPU_flushPIQ(-1); /*We're jumping to another address*/
		CPU[activeCPU].didJump = 1;
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 9;
		}
		/* Branch taken */
	}
	else
	{
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 4;
		}
		/* Branch not taken */
	}
}
void CPU8086_OPE2()
{
	INLINEREGISTER sbyte rel8;
	rel8 = imm8();
	modrm_generateInstructionTEXT("LOOP", 0,((REG_EIP+rel8)&CPU_EIPmask(0)),CPU_EIPSize(0));
	if (--REG_CX)
	{
		CPU_JMPrel((int_32)rel8,0);
		CPU_flushPIQ(-1); /*We're jumping to another address*/
		CPU[activeCPU].didJump = 1;
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 8;
		}
		/* Branch taken */
	}
	else
	{
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 3;
		}
		/* Branch not taken */
	}
}
void CPU8086_OPE3()
{
	INLINEREGISTER sbyte rel8;
	rel8 = imm8();
	modrm_generateInstructionTEXT("JCXZ",0,((REG_EIP+rel8)&CPU_EIPmask(0)),CPU_EIPSize(0));
	if (REG_CX==0)
	{
		CPU_JMPrel((int_32)rel8,0);
		CPU_flushPIQ(-1); /*We're jumping to another address*/
		CPU[activeCPU].didJump = 1;
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 9;
		}
		/* Branch taken */
	}
	else 
	{
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 4;
		}
		/* Branch not taken */
	}
}
void CPU8086_OPE4()
{
	INLINEREGISTER byte theimm = CPU[activeCPU].immb;
	modrm_generateInstructionTEXT("IN AL,",0,theimm,PARAM_IMM8_PARAM);
	if (EMULATED_CPU <= CPU_NECV30) //Valid CPU to apply?
	{
		if (CPU8086_instructionstepdelayBIU(0, 1)) return; //1 cycles before we start, active cycles only, wait for it to finish!
		if (CPU8086_instructionstepdelayBIUidle(2, 2)) return; //2 cycles before we start, active cycles only!
	}
	if (CPU_PORT_IN_B(0,theimm,&REG_AL)) return;
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		//CPU[activeCPU].cycles_OP += 2;
	}
	/*Timings!*/
}
void CPU8086_OPE5()
{
	INLINEREGISTER byte theimm = CPU[activeCPU].immb;
	modrm_generateInstructionTEXT("IN AX,",0,theimm,PARAM_IMM8_PARAM);
	if (EMULATED_CPU <= CPU_NECV30) //Valid CPU to apply?
	{
		if (CPU8086_instructionstepdelayBIU(0, 1)) return; //1 cycles before we start, active cycles only, wait for it to finish!
		if (CPU8086_instructionstepdelayBIUidle(2, 2)) return; //2 cycles before we start, active cycles only!
	}
	if (CPU_PORT_IN_W(0,theimm,&REG_AX)) return;
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		//CPU[activeCPU].cycles_OP += 2; /*Timings!*/
	}
}
void CPU8086_OPE6()
{
	INLINEREGISTER byte theimm = CPU[activeCPU].immb;
	debugger_setcommand("OUT %02X,AL",theimm);
	if (EMULATED_CPU <= CPU_NECV30) //Valid CPU to apply?
	{
		if (CPU8086_instructionstepdelayBIU(0, 1)) return; //1 cycles before we start, active cycles only, wait for it to finish!
		if ((BIU_getcycle()==1) && (CPU[activeCPU].timingpath==0)) //T2?
		{
			CPU[activeCPU].timingpath = 0;
			if (CPU8086_instructionstepdelayBIUidle(2, 2)) return; //2 cycles before we start, active cycles only!
		}
		else
		{
			CPU[activeCPU].timingpath = 1; //Other path!
			if (CPU8086_instructionstepwaitBIUready(2)) return; //Wait for ready!
			if (CPU8086_instructionstepdelayBIU(4, 1)) return; //1 cycles before we start, active cycles only, wait for it to finish!
		}
	}
	if(CPU_PORT_OUT_B(0,theimm,REG_AL)) return;
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		//CPU[activeCPU].cycles_OP += 1;
	}
	/*Timings!*/
}
void CPU8086_OPE7()
{
	INLINEREGISTER byte theimm = CPU[activeCPU].immb;
	debugger_setcommand("OUT %02X,AX",theimm);
	if (EMULATED_CPU <= CPU_NECV30) //Valid CPU to apply?
	{
		if (CPU8086_instructionstepdelayBIU(0, 1)) return; //1 cycles before we start, active cycles only, wait for it to finish!
		if ((BIU_getcycle()==1) && (CPU[activeCPU].timingpath==0)) //T2?
		{
			CPU[activeCPU].timingpath = 0;
			if (CPU8086_instructionstepdelayBIUidle(2, 2)) return; //2 cycles before we start, active cycles only!
		}
		else
		{
			CPU[activeCPU].timingpath = 1; //Other path!
			if (CPU8086_instructionstepwaitBIUready(2)) return; //Wait for ready!
			if (CPU8086_instructionstepdelayBIU(4, 1)) return; //1 cycles before we start, active cycles only, wait for it to finish!
		}
	}
	if (CPU_PORT_OUT_W(0,theimm,REG_AX)) return;
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		//CPU[activeCPU].cycles_OP += 1; /*Timings!*/
	}
}
void CPU8086_OPE8()
{
	INLINEREGISTER sword reloffset = imm16();
	modrm_generateInstructionTEXT("CALL", 0, ((REG_EIP + reloffset)&CPU_EIPmask(0)), CPU_EIPSize(0));
	if (unlikely(CPU[activeCPU].stackchecked == 0))
	{
		if (checkStackAccess(1, 1, 0)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU_apply286cycles() == 0) /* No 80286+ cycles instead? */
	{
		if (CPU8086_instructionstepdelayBIU(0, 1)) return; //First, 3 cycles NOP!
		if (CPU8086_instructionstepdelayBIU(2, 3)) return; //First, 3 cycles NOP!
		if (CPU8086_instructionstepwaitBIUready(4)) return; //Wait to become ready!
		if (CPU8086_instructionstepdelayBIU(6, 2)) return; //2 more cycles here!
		if (CPU8086_PUSHw(8, &REG_IP, 0)) return;
	}
	else
	{
		if (CPU8086_PUSHw(0, &REG_IP, 0)) return;
	}
	CPU_JMPrel((int_32)reloffset,0);
	CPU_flushPIQ(-1); /*We're jumping to another address*/
	/* Intrasegment direct */
}
void CPU8086_OPE9()
{
	INLINEREGISTER sword reloffset = imm16();
	modrm_generateInstructionTEXT("JMP",0,((REG_EIP + reloffset)&CPU_EIPmask(0)),CPU_EIPSize(0));
	if (CPU_apply286cycles() == 0) /* No 80286+ cycles instead? */
	{
		if (CPU8086_instructionstepdelayBIU(0, 1)) return; //First, 3 cycles NOP!
		if (CPU8086_instructionstepdelayBIU(2, 3)) return; //First, 3 cycles NOP!
		if (CPU8086_instructionstepwaitBIUready(4)) return; //Wait to become ready!
		if (CPU8086_instructionstepdelayBIU(6, 2)) return; //2 more cycles here!
	}
	CPU_JMPrel((int_32)reloffset,0);
	CPU_flushPIQ(-1); /*We're jumping to another address*/
	/* Intrasegment direct */
}
void CPU8086_OPEA()
{
	INLINEREGISTER uint_32 segmentoffset = CPU[activeCPU].imm32;
	debugger_setcommand(debugger_forceEIP()?"JMP %04X:%08X":"JMP %04X:%04X", (segmentoffset>>16), ((segmentoffset&0xFFFF)&CPU_EIPmask(0)));
	CPU[activeCPU].destEIP = (uint_32)((segmentoffset&0xFFFF)&CPU_EIPmask(0));
	if (segmentWritten(CPU_SEGMENT_CS, (segmentoffset>>16), 1)) return;
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 3;
	}
	/* Intersegment direct */
}
void CPU8086_OPEB()
{
	INLINEREGISTER sbyte reloffset = imm8();
	modrm_generateInstructionTEXT("JMP",0,((REG_EIP + reloffset)&CPU_EIPmask(0)),CPU_EIPSize(0));
	CPU_JMPrel((int_32)reloffset,0);
	CPU_flushPIQ(-1); /*We're jumping to another address*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 7;
		CPU[activeCPU].cycles_stallBIU += 7; /*Stall the BIU partly now!*/
	}
	/* Intrasegment direct short */
}
void CPU8086_OPEC()
{
	modrm_generateInstructionTEXT("IN AL,DX",0,0,PARAM_NONE);
	if (EMULATED_CPU <= CPU_NECV30) //Valid CPU to apply?
	{
		if (CPU8086_instructionstepdelayBIU(0, 1)) return; //1 cycles before we start, active cycles only, wait for it to finish!
	}
	if (CPU_PORT_IN_B(0,REG_DX,&REG_AL)) return;
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		//CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSREAD;
	}
	/*Timings!*/
}
void CPU8086_OPED()
{
	modrm_generateInstructionTEXT("IN AX,DX",0,0,PARAM_NONE);
	if (EMULATED_CPU <= CPU_NECV30) //Valid CPU to apply?
	{
		if (CPU8086_instructionstepdelayBIU(0, 1)) return; //1 cycles before we start, active cycles only, wait for it to finish!
	}
	if (CPU_PORT_IN_W(0,REG_DX,&REG_AX)) return;
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		//CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSREAD; /*Timings!*/
	}
}
void CPU8086_OPEE()
{
	modrm_generateInstructionTEXT("OUT DX,AL",0,0,PARAM_NONE);
	if (EMULATED_CPU <= CPU_NECV30) //Valid CPU to apply?
	{
		if (CPU8086_instructionstepdelayBIU(0, (BIU_getcycle()==3)?2:1)) return; //1 cycles before we start, active cycles only, wait for it to finish!
		if (CPU8086_instructionstepdelayBIU(2, 4)) return; //4 cycles before we start, active cycles only, wait for it to finish!
	}
	if (CPU_PORT_OUT_B(0,REG_DX,REG_AL)) return;
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		//CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSWRITE;
	}
	/*Timings!*/
}
void CPU8086_OPEF()
{
	modrm_generateInstructionTEXT("OUT DX,AX",0,0,PARAM_NONE);
	if (EMULATED_CPU <= CPU_NECV30) //Valid CPU to apply?
	{
		if (CPU8086_instructionstepdelayBIU(0, (BIU_getcycle()==3)?2:1)) return; //1 cycles before we start, active cycles only, wait for it to finish!
		if (CPU8086_instructionstepdelayBIU(2, 4)) return; //4 cycles before we start, active cycles only, wait for it to finish!
	}
	if (CPU_PORT_OUT_W(0,REG_DX,REG_AX)) return;
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		//CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSWRITE; /*Timings!*/
	}
	/*To memory?*/
}
void CPU8086_OPF1()
{
	modrm_generateInstructionTEXT("<Undefined and reserved opcode, no error>",0,0,PARAM_NONE);
}
void CPU8086_OPF4()
{
	modrm_generateInstructionTEXT("HLT",0,0,PARAM_NONE);
	if (getCPL() && (getcpumode() != CPU_MODE_REAL)) /* Privilege level isn't 0? */
	{
		THROWDESCGP(0,0,0);
		return; /* Privileged instruction in non-real mode! */
	}
	CPU[activeCPU].halt = 1;
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 1+(CPU[activeCPU].repeating?0:1);
	}
	/*Special timing!*/
}
void CPU8086_OPF5()
{
	modrm_generateInstructionTEXT("CMC",0,0,PARAM_NONE);
	FLAGW_CF(FLAG_CF^1);
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 1;
	}
	/*Special timing!*/
}
void CPU8086_OPF8()
{
	modrm_generateInstructionTEXT("CLC",0,0,PARAM_NONE);
	FLAGW_CF(0);
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 1;
	}
	/*Special timing!*/
}
void CPU8086_OPF9()
{
	modrm_generateInstructionTEXT("STC",0,0,PARAM_NONE);
	FLAGW_CF(1);
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 1;
	}
	/*Special timing!*/
}
void CPU8086_OPFA()
{
	modrm_generateInstructionTEXT("CLI",0,0,PARAM_NONE);
	if (checkSTICLI())
	{
		FLAGW_IF(0);
	}
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 1;
	}
	/*Special timing!*/
}
void CPU8086_OPFB()
{
	modrm_generateInstructionTEXT("STI",0,0,PARAM_NONE);
	if (checkSTICLI())
	{
		FLAGW_IF(1);
		CPU[activeCPU].allowInterrupts = 0; /* Inhabit all interrupts up to the next instruction */
	}
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 1;
	}
	/*Special timing!*/
}
void CPU8086_OPFC()
{
	modrm_generateInstructionTEXT("CLD",0,0,PARAM_NONE);
	FLAGW_DF(0);
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 1;
	}
	/*Special timing!*/
}
void CPU8086_OPFD()
{
	modrm_generateInstructionTEXT("STD",0,0,PARAM_NONE);
	FLAGW_DF(1);
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 1;
	}
	/*Special timing!*/
}

/*

NOW COME THE GRP1-5 OPCODES:

*/

//GRP1

/*

DEBUG: REALLY SUPPOSED TO HANDLE OP80-83 HERE?

*/

void CPU8086_OP80() //GRP1 Eb,Ib
{
	INLINEREGISTER byte imm = CPU[activeCPU].immb;
	if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
	{
		modrm_debugger8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,CPU[activeCPU].MODRM_src1);
	}
	switch (CPU[activeCPU].thereg) //What function?
	{
	case 0: //ADD
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("ADD %s,%02X",&CPU[activeCPU].modrm_param1,imm); //CMP Eb, Ib
		}
		CPU8086_internal_ADD8(modrm_addr8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0),imm,3); //ADD Eb, Ib
		break;
	case 1: //OR
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("OR %s,%02X",&CPU[activeCPU].modrm_param1,imm); //CMP Eb, Ib
		}
		CPU8086_internal_OR8(modrm_addr8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0),imm,3); //OR Eb, Ib
		break;
	case 2: //ADC
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("ADC %s,%02X",&CPU[activeCPU].modrm_param1,imm); //CMP Eb, Ib
		}
		CPU8086_internal_ADC8(modrm_addr8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0),imm,3); //ADC Eb, Ib
		break;
	case 3: //SBB
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("SBB %s,%02X",&CPU[activeCPU].modrm_param1,imm); //CMP Eb, Ib
		}
		CPU8086_internal_SBB8(modrm_addr8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0),imm,3); //SBB Eb, Ib
		break;
	case 4: //AND
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("AND %s,%02X",&CPU[activeCPU].modrm_param1,imm); //CMP Eb, Ib
		}
		CPU8086_internal_AND8(modrm_addr8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0),imm,3); //AND Eb, Ib
		break;
	case 5: //SUB
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("SUB %s,%02X",&CPU[activeCPU].modrm_param1,imm); //CMP Eb, Ib
		}
		CPU8086_internal_SUB8(modrm_addr8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0),imm,3); //SUB Eb, Ib
		break;
	case 6: //XOR
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("XOR %s,%02X",&CPU[activeCPU].modrm_param1,imm); //CMP Eb, Ib
		}
		CPU8086_internal_XOR8(modrm_addr8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0),imm,3); //XOR Eb, Ib
		break;
	case 7: //CMP
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("CMP %s,%02X",&CPU[activeCPU].modrm_param1,imm); //CMP Eb, Ib
		}
		if (unlikely(CPU[activeCPU].modrmstep==0)) if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,1)) return; //Abort when needed!
		if (CPU8086_instructionstepreadmodrmb(0,&CPU[activeCPU].instructionbufferb,CPU[activeCPU].MODRM_src0)) return;
		CMP_b(CPU[activeCPU].instructionbufferb,imm,3); //CMP Eb, Ib
		break;
	default:
		break;
	}
}

void CPU8086_OP81() //GRP1 Ev,Iv
{
	INLINEREGISTER word imm = CPU[activeCPU].immw;
	if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
	{
		modrm_debugger16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,CPU[activeCPU].MODRM_src1);
	}
	switch (CPU[activeCPU].thereg) //What function?
	{
	case 0: //ADD
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("ADD %s,%04X",&CPU[activeCPU].modrm_param1,imm); //ADD Eb, Ib
		}
		CPU8086_internal_ADD16(modrm_addr16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0),imm,3); //ADD Eb, Ib
		break;
	case 1: //OR
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("OR %s,%04X",&CPU[activeCPU].modrm_param1,imm); //OR Eb, Ib
		}
		CPU8086_internal_OR16(modrm_addr16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0),imm,3); //OR Eb, Ib
		break;
	case 2: //ADC
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("ADC %s,%04X",&CPU[activeCPU].modrm_param1,imm); //ADC Eb, Ib
		}
		CPU8086_internal_ADC16(modrm_addr16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0),imm,3); //ADC Eb, Ib
		break;
	case 3: //SBB
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("SBB %s,%04X",&CPU[activeCPU].modrm_param1,imm); //SBB Eb, Ib
		}
		CPU8086_internal_SBB16(modrm_addr16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0),imm,3); //SBB Eb, Ib
		break;
	case 4: //AND
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("AND %s,%04X",&CPU[activeCPU].modrm_param1,imm); //AND Eb, Ib
		}
		CPU8086_internal_AND16(modrm_addr16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0),imm,3); //AND Eb, Ib
		break;
	case 5: //SUB
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("SUB %s,%04X",&CPU[activeCPU].modrm_param1,imm); //SUB Eb, Ib
		}
		CPU8086_internal_SUB16(modrm_addr16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0),imm,3); //SUB Eb, Ib
		break;
	case 6: //XOR
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("XOR %s,%04X",&CPU[activeCPU].modrm_param1,imm); //XOR Eb, Ib
		}
		CPU8086_internal_XOR16(modrm_addr16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0),imm,3); //XOR Eb, Ib
		break;
	case 7: //CMP
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("CMP %s,%04X",&CPU[activeCPU].modrm_param1,imm); //CMP Eb, Ib
		}
		if (unlikely(CPU[activeCPU].modrmstep == 0))
		{
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 1|0x40)) return;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 1|0xA0)) return;
		} //Abort when needed!
		if (CPU8086_instructionstepreadmodrmw(0,&CPU[activeCPU].instructionbufferw,CPU[activeCPU].MODRM_src0)) return;
		CMP_w(CPU[activeCPU].instructionbufferw,imm,3); //CMP Eb, Ib
		break;
	default:
		break;
	}
}

void CPU8086_OP82() //GRP1 Eb,Ib (same as OP80)
{
	CPU8086_OP80(); //Same!
}

void CPU8086_OP83() //GRP1 Ev,Ib
{
	INLINEREGISTER word imm;
	imm = (word)CPU[activeCPU].immb;
	if (imm&0x80) imm |= 0xFF00; //Sign extend!
	if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
	{
		modrm_debugger16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,CPU[activeCPU].MODRM_src1);
	}
	switch (CPU[activeCPU].thereg) //What function?
	{
	case 0: //ADD
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("ADD %s,%04X",&CPU[activeCPU].modrm_param1,imm); //ADD Eb, Ib
		}
		CPU8086_internal_ADD16(modrm_addr16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0),imm,3); //ADD Eb, Ib
		break;
	case 1: //OR
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("OR %s,%04X",&CPU[activeCPU].modrm_param1,imm); //OR Eb, Ib
		}
		CPU8086_internal_OR16(modrm_addr16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0),imm,3); //OR Eb, Ib
		break;
	case 2: //ADC
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("ADC %s,%04X",&CPU[activeCPU].modrm_param1,imm); //ADC Eb, Ib
		}
		CPU8086_internal_ADC16(modrm_addr16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0),imm,3); //ADC Eb, Ib
		break;
	case 3: //SBB
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("SBB %s,%04X",&CPU[activeCPU].modrm_param1,imm); //SBB Eb, Ib
		}
		CPU8086_internal_SBB16(modrm_addr16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0),imm,3); //SBB Eb, Ib
		break;
	case 4: //AND
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("AND %s,%04X",&CPU[activeCPU].modrm_param1,imm); //AND Eb, Ib
		}
		CPU8086_internal_AND16(modrm_addr16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0),imm,3); //AND Eb, Ib
		break;
	case 5: //SUB
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("SUB %s,%04X",&CPU[activeCPU].modrm_param1,imm); //SUB Eb, Ib
		}
		CPU8086_internal_SUB16(modrm_addr16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0),imm,3); //SUB Eb, Ib
		break;
	case 6: //XOR
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("XOR %s,%04X",&CPU[activeCPU].modrm_param1,imm); //XOR Eb, Ib
		}
		CPU8086_internal_XOR16(modrm_addr16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0),imm,3); //XOR Eb, Ib
		break;
	case 7: //CMP
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("CMP %s,%04X",&CPU[activeCPU].modrm_param1,imm); //CMP Eb, Ib
		}
		if (unlikely(CPU[activeCPU].modrmstep == 0))
		{
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 1|0x40)) return;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 1|0xA0)) return;
		} //Abort when needed!
		if (CPU8086_instructionstepreadmodrmw(0,&CPU[activeCPU].instructionbufferw,CPU[activeCPU].MODRM_src0)) return;
		CMP_w(CPU[activeCPU].instructionbufferw,imm,3); //CMP Eb, Ib
		break;
	default:
		break;
	}
}

void CPU8086_OP8F() //Undocumented GRP opcode 8F r/m16
{
	byte stackresult;
	switch (CPU[activeCPU].thereg) //What function?
	{
	case 0: //POP
		//Cycle-accurate emulation of the instruction!
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			modrm_generateInstructionTEXT("POP",16,0,PARAM_MODRM_0); //POP Ew
		}
		if (unlikely(CPU[activeCPU].stackchecked==0))
		{
			modrm_recalc(&CPU[activeCPU].params); //Recalc if using (e)sp as the destination offset!
			if (checkStackAccess(1,0,0)) return;
			stack_pop(0); //Popped a word!
			modrm_recalc(&CPU[activeCPU].params); //Recalc if using (e)sp as the destination offset!
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0x40)) return;
			if ((stackresult = modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0))!=0)
			{
				if (stackresult==2)
				{
					stack_push(0); //Popped a word!
				}
				return; //Abort when needed!
			}
			stack_push(0); //Popped a word!
			++CPU[activeCPU].stackchecked;
		}
		//Execution step!
		if (CPU8086_instructionstepPOPtimeout(0)) return; /*POP timeout*/
		if (CPU8086_POPw(2,&CPU[activeCPU].value8F_16,0)) return; //POP first!
		if (CPU8086_instructionstepwritemodrmw(0,CPU[activeCPU].value8F_16,CPU[activeCPU].MODRM_src0,0)) return; //POP r/m16
		if ((CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0].reg16 == &REG_SS) && (CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0].isreg==1) && (CPU[activeCPU].previousAllowInterrupts)) //Popping into SS?
		{
			CPU[activeCPU].allowInterrupts = 0; /* Inhabit all interrupts up to the next instruction */
		}
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			if (MODRM_EA(CPU[activeCPU].params)) //Mem?
			{
				CPU[activeCPU].cycles_OP += 5; /*Pop Mem!*/
			}
			else //Reg?
			{
				CPU[activeCPU].cycles_OP += 3; /*Pop Reg!*/
			}
		}
		break;
	default: //Unknown opcode or special?
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("Unknown opcode: 8F /%u", CPU[activeCPU].thereg); //Error!
		}
		CPU_unkOP(); //Execute the unknown opcode exception handler, if any!
		break;
	}
}

void CPU8086_OPD0() //GRP2 Eb,1
{
	if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
	{
		modrm_debugger8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,CPU[activeCPU].MODRM_src1); //Get src!
		switch (CPU[activeCPU].thereg) //What function?
		{
		case 0: //ROL
			debugger_setcommand("ROL %s,1",&CPU[activeCPU].modrm_param1);
			break;
		case 1: //ROR
			debugger_setcommand("ROR %s,1",&CPU[activeCPU].modrm_param1);
			break;
		case 2: //RCL
			debugger_setcommand("RCL %s,1",&CPU[activeCPU].modrm_param1);
			break;
		case 3: //RCR
			debugger_setcommand("RCR %s,1",&CPU[activeCPU].modrm_param1);
			break;
		case 4: //SHL
		case 6: //--- Unknown Opcode! --- Undocumented opcode!
			debugger_setcommand("SHL %s,1",&CPU[activeCPU].modrm_param1);
			break;
		case 5: //SHR
			debugger_setcommand("SHR %s,1",&CPU[activeCPU].modrm_param1);
			break;
		case 7: //SAR
			debugger_setcommand("SAR %s,1",&CPU[activeCPU].modrm_param1);
			break;
		default:
			break;
		}
	}
	if (unlikely(CPU[activeCPU].modrmstep==0)) 
	{
		if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0x40)) return; //Abort when needed!
		if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0xA0)) return; //Abort when needed!
	}
	if (CPU8086_instructionstepreadmodrmb(0,&CPU[activeCPU].instructionbufferb,CPU[activeCPU].MODRM_src0)) return;
	if (CPU[activeCPU].instructionstep==0) //Execution step?
	{
		CPU[activeCPU].oper1b = CPU[activeCPU].instructionbufferb;
		CPU[activeCPU].res8 = op_grp2_8(1,0); //Execute!
		++CPU[activeCPU].instructionstep; //Next step: writeback!
		CPU[activeCPU].executed = 0; //Time it!
		return; //Wait for the next step!
	}
	if (CPU8086_instructionstepwritemodrmb(2, CPU[activeCPU].res8,CPU[activeCPU].MODRM_src0)) return;
}
void CPU8086_OPD1() //GRP2 Ev,1
{
	if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
	{
		modrm_debugger16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,CPU[activeCPU].MODRM_src1); //Get src!
		switch (CPU[activeCPU].thereg) //What function?
		{
		case 0: //ROL
			debugger_setcommand("ROL %s,1",&CPU[activeCPU].modrm_param1);
			break;
		case 1: //ROR
			debugger_setcommand("ROR %s,1",&CPU[activeCPU].modrm_param1);
			break;
		case 2: //RCL
			debugger_setcommand("RCL %s,1",&CPU[activeCPU].modrm_param1);
			break;
		case 3: //RCR
			debugger_setcommand("RCR %s,1",&CPU[activeCPU].modrm_param1);
			break;
		case 4: //SHL
		case 6: //--- Unknown Opcode! --- Undocumented opcode!
			debugger_setcommand("SHL %s,1",&CPU[activeCPU].modrm_param1);
			break;
		case 5: //SHR
			debugger_setcommand("SHR %s,1",&CPU[activeCPU].modrm_param1);
			break;
		case 7: //SAR
			debugger_setcommand("SAR %s,1",&CPU[activeCPU].modrm_param1);
			break;
		default:
			break;
		}
	}
	if (unlikely(CPU[activeCPU].modrmstep==0)) 
	{
		if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0x40)) return; //Abort when needed!
		if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0xA0)) return; //Abort when needed!
	}
	if (CPU8086_instructionstepreadmodrmw(0,&CPU[activeCPU].instructionbufferw,CPU[activeCPU].MODRM_src0)) return;
	if (CPU[activeCPU].instructionstep==0) //Execution step?
	{
		CPU[activeCPU].oper1 = CPU[activeCPU].instructionbufferw;
		CPU[activeCPU].res16 = op_grp2_16(1,0); //Execute!
		++CPU[activeCPU].instructionstep; //Next step: writeback!
		CPU[activeCPU].executed = 0; //Time it!
		return; //Wait for the next step!
	}
	if (CPU8086_instructionstepwritemodrmw(2, CPU[activeCPU].res16,CPU[activeCPU].MODRM_src0,0)) return;
}
void CPU8086_OPD2() //GRP2 Eb,CL
{
	if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
	{
		modrm_debugger8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,CPU[activeCPU].MODRM_src1); //Get src!
		switch (CPU[activeCPU].thereg) //What function?
		{
		case 0: //ROL
			debugger_setcommand("ROL %s,CL",&CPU[activeCPU].modrm_param1);
			break;
		case 1: //ROR
			debugger_setcommand("ROR %s,CL",&CPU[activeCPU].modrm_param1);
			break;
		case 2: //RCL
			debugger_setcommand("RCL %s,CL",&CPU[activeCPU].modrm_param1);
			break;
		case 3: //RCR
			debugger_setcommand("RCR %s,CL",&CPU[activeCPU].modrm_param1);
			break;
		case 4: //SHL
		case 6: //--- Unknown Opcode! --- Undocumented opcode!
			debugger_setcommand("SHL %s,CL",&CPU[activeCPU].modrm_param1);
			break;
		case 5: //SHR
			debugger_setcommand("SHR %s,CL",&CPU[activeCPU].modrm_param1);
			break;
		case 7: //SAR
			debugger_setcommand("SAR %s,CL",&CPU[activeCPU].modrm_param1);
			break;
		default:
			break;
		}
	}
	if (unlikely(CPU[activeCPU].modrmstep==0)) 
	{
		if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0x40)) return; //Abort when needed!
		if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0xA0)) return; //Abort when needed!
	}
	if (CPU8086_instructionstepreadmodrmb(0,&CPU[activeCPU].instructionbufferb,CPU[activeCPU].MODRM_src0)) return;
	if (CPU[activeCPU].instructionstep==0) //Execution step?
	{
		CPU[activeCPU].oper1b = CPU[activeCPU].instructionbufferb;
		CPU[activeCPU].res8 = op_grp2_8(REG_CL,1); //Execute!
		++CPU[activeCPU].instructionstep; //Next step: writeback!
		CPU[activeCPU].executed = 0; //Time it!
		return; //Wait for the next step!
	}
	if (CPU8086_instructionstepwritemodrmb(2, CPU[activeCPU].res8,CPU[activeCPU].MODRM_src0)) return;
}
void CPU8086_OPD3() //GRP2 Ev,CL
{
	if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
	{
		modrm_debugger16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,CPU[activeCPU].MODRM_src1); //Get src!
		switch (CPU[activeCPU].thereg) //What function?
		{
		case 0: //ROL
			debugger_setcommand("ROL %s,CL",&CPU[activeCPU].modrm_param1);
			break;
		case 1: //ROR
			debugger_setcommand("ROR %s,CL",&CPU[activeCPU].modrm_param1);
			break;
		case 2: //RCL
			debugger_setcommand("RCL %s,CL",&CPU[activeCPU].modrm_param1);
			break;
		case 3: //RCR
			debugger_setcommand("RCR %s,CL",&CPU[activeCPU].modrm_param1);
			break;
		case 4: //SHL
		case 6: //--- Unknown Opcode! --- Undocumented opcode!
			debugger_setcommand("SHL %s,CL",&CPU[activeCPU].modrm_param1);
			break;
		case 5: //SHR
			debugger_setcommand("SHR %s,CL",&CPU[activeCPU].modrm_param1);
			break;
		case 7: //SAR
			debugger_setcommand("SAR %s,CL",&CPU[activeCPU].modrm_param1);
			break;
		default:
			break;
		}
	}
	if (unlikely(CPU[activeCPU].modrmstep==0)) 
	{
		if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0x40)) return; //Abort when needed!
		if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0xA0)) return; //Abort when needed!
	}
	if (CPU8086_instructionstepreadmodrmw(0,&CPU[activeCPU].instructionbufferw,CPU[activeCPU].MODRM_src0)) return;
	if (CPU[activeCPU].instructionstep==0) //Execution step?
	{
		CPU[activeCPU].oper1 = CPU[activeCPU].instructionbufferw;
		CPU[activeCPU].res16 = op_grp2_16(REG_CL,1); //Execute!
		++CPU[activeCPU].instructionstep; //Next step: writeback!
		CPU[activeCPU].executed = 0; //Time it!
		return; //Wait for the next step!
	}
	if (CPU8086_instructionstepwritemodrmw(2, CPU[activeCPU].res16,CPU[activeCPU].MODRM_src0,0)) return;
}

void CPU8086_OPF6() //GRP3a Eb
{
	if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
	{
		modrm_debugger8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,CPU[activeCPU].MODRM_src1); //Get src!
		switch (CPU[activeCPU].thereg) //What function?
		{
		case 0: //TEST modrm8, imm8
		case 1: //--- Undocumented opcode, same as above!
			debugger_setcommand("TEST %s,%02x",&CPU[activeCPU].modrm_param1, CPU[activeCPU].immb);
			break;
		case 2: //NOT
			debugger_setcommand("NOT %s",&CPU[activeCPU].modrm_param1);
			break;
		case 3: //NEG
			debugger_setcommand("NEG %s",&CPU[activeCPU].modrm_param1);
			break;
		case 4: //MUL
			debugger_setcommand("MUL %s",&CPU[activeCPU].modrm_param1);
			break;
		case 5: //IMUL
			debugger_setcommand("IMUL %s",&CPU[activeCPU].modrm_param1);
			break;
		case 6: //DIV
			modrm_generateInstructionTEXT("DIV",8,0,PARAM_MODRM_0);
			break;
		case 7: //IDIV
			modrm_generateInstructionTEXT("IDIV",8,0,PARAM_MODRM_0);
			break;
		default:
			break;
		}
	}
	if (unlikely(CPU[activeCPU].modrmstep==0)) 
	{
		if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,1|0x40)) return; //Abort when needed!
		if ((CPU[activeCPU].thereg>1) && (CPU[activeCPU].thereg<4))
		{
			if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0x40)) return; //Abort when needed!
		}
		if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,1|0xA0)) return; //Abort when needed!
		if ((CPU[activeCPU].thereg>1) && (CPU[activeCPU].thereg<4))
		{
			if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0xA0)) return; //Abort when needed!
		}
	}
	if (CPU8086_instructionstepreadmodrmb(0,&CPU[activeCPU].instructionbufferb,CPU[activeCPU].MODRM_src0)) return;
	if (CPU[activeCPU].instructionstep==0) //Execution step?
	{
		CPU[activeCPU].oper1b = CPU[activeCPU].instructionbufferb;
		op_grp3_8();
		if (likely(CPU[activeCPU].executed)) ++CPU[activeCPU].instructionstep; //Next step!
		else return; //Wait for completion!
	}
	if ((CPU[activeCPU].thereg>1) && (CPU[activeCPU].thereg<4))
	{
		if (CPU8086_instructionstepwritemodrmb(2, CPU[activeCPU].res8,CPU[activeCPU].MODRM_src0)) return;
	}
}
void CPU8086_OPF7() //GRP3b Ev
{
	if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
	{
		modrm_debugger16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,CPU[activeCPU].MODRM_src1); //Get src!
		switch (CPU[activeCPU].thereg) //What function?
		{
		case 0: //TEST modrm16, imm16
		case 1: //--- Undocumented opcode, same as above!
			debugger_setcommand("TEST %s,%04x",&CPU[activeCPU].modrm_param1, CPU[activeCPU].immw);
			break;
		case 2: //NOT
			modrm_generateInstructionTEXT("NOT",16,0,PARAM_MODRM_0);
			break;
		case 3: //NEG
			modrm_generateInstructionTEXT("NEG",16,0,PARAM_MODRM_0);
			break;
		case 4: //MUL
			modrm_generateInstructionTEXT("MUL",16,0,PARAM_MODRM_0);
			break;
		case 5: //IMUL
			modrm_generateInstructionTEXT("IMUL",16,0,PARAM_MODRM_0);
			break;
		case 6: //DIV
			modrm_generateInstructionTEXT("DIV",16,0,PARAM_MODRM_0);
			break;
		case 7: //IDIV
			modrm_generateInstructionTEXT("IDIV",16,0,PARAM_MODRM_0);
			break;
		default:
			break;
		}
	}
	if (unlikely(CPU[activeCPU].modrmstep==0)) 
	{
		if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,1|0x40)) return; //Abort when needed!
		if ((CPU[activeCPU].thereg>1) && (CPU[activeCPU].thereg<4)) //NOT/NEG?
		{
			if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0x40)) return; //Abort when needed!
		}
		if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,1|0xA0)) return; //Abort when needed!
		if ((CPU[activeCPU].thereg>1) && (CPU[activeCPU].thereg<4)) //NOT/NEG?
		{
			if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0xA0)) return; //Abort when needed!
		}

	}
	if (CPU8086_instructionstepreadmodrmw(0,&CPU[activeCPU].instructionbufferw,CPU[activeCPU].MODRM_src0)) return;
	if (CPU[activeCPU].instructionstep==0) //Execution step?
	{
		CPU[activeCPU].oper1 = CPU[activeCPU].instructionbufferw;
		op_grp3_16();
		if (likely(CPU[activeCPU].executed)) ++CPU[activeCPU].instructionstep; //Next step!
		else return; //Wait for completion!
	}
	if ((CPU[activeCPU].thereg>1) && (CPU[activeCPU].thereg<4)) //NOT/NEG?
	{
		if (CPU8086_instructionstepwritemodrmw(2, CPU[activeCPU].res16,CPU[activeCPU].MODRM_src0,0)) return;
	}
}
//All OK up till here.

void CPU8086_OPFE() //GRP4 Eb
{
	modrm_debugger8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,CPU[activeCPU].MODRM_src1);
	switch (CPU[activeCPU].thereg) //What function?
	{
	case 0: //INC
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			modrm_generateInstructionTEXT("INC",8,0,PARAM_MODRM_0); //INC!
		}
		if (unlikely(CPU[activeCPU].internalinstructionstep==0))
		{
			if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0x40)) return; //Abort when needed!
			if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0xA0)) return; //Abort when needed!
		}
		CPU8086_internal_INC8(modrm_addr8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0));
		break;
	case 1: //DEC
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			modrm_generateInstructionTEXT("DEC",8,0,PARAM_MODRM_0); //DEC!
		}
		if (unlikely(CPU[activeCPU].internalinstructionstep==0))
		{
			if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0x40)) return; //Abort when needed!
			if (modrm_check8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0xA0)) return; //Abort when needed!
		}
		CPU8086_internal_DEC8(modrm_addr8(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0));
		break;
	default: //Unknown opcode or special?
		CPU_unkOP(); //Execute the unknown opcode exception handler, if any!
		break;
	}
}

void CPU8086_OPFF() //GRP5 Ev
{
	if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
	{
		modrm_debugger16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,CPU[activeCPU].MODRM_src1); //Get src!
		switch (CPU[activeCPU].thereg) //What function?
		{
		case 0: //INC modrm8
			modrm_generateInstructionTEXT("INC",16,0,PARAM_MODRM_0); //INC!
			break;
		case 1: //DEC modrm8
			modrm_generateInstructionTEXT("DEC",16,0,PARAM_MODRM_0); //DEC!
			break;
		case 2: //CALL
			modrm_generateInstructionTEXT("CALL",16,0,PARAM_MODRM_0); //CALL!
			break;
		case 3: //CALL Mp (Read address word and jump there)
			modrm_generateInstructionTEXT("CALLF",16,0,PARAM_MODRM_0); //Jump to the address pointed here!
			//debugger_setcommand("CALL %04X:%04X",MMU_rw(CPU_SEGMENT_CS,REG_CS,ea,0),MMU_rw(CPU_SEGMENT_CS,REG_CS,ea+2,0)); //Based on CALL Ap
			break;
		case 4: //JMP
			modrm_generateInstructionTEXT("JMP",16,0,PARAM_MODRM_0); //JMP to the register!
			break;
		case 5: //JMP Mp
			modrm_generateInstructionTEXT("JMPF",16,0,PARAM_MODRM_0); //Jump to the address pointed here!
			//debugger_setcommand("JMP %04X:%04X",MMU_rw(CPU_SEGMENT_CS,REG_CS,ea,0),MMU_rw(CPU_SEGMENT_CS,REG_CS,ea+2,0)); //JMP to destination!
			break;
		case 6: //PUSH
			modrm_generateInstructionTEXT("PUSH",16,0,PARAM_MODRM_0); //PUSH!
			break;
		case 7: //---
			debugger_setcommand("<UNKNOWN Opcode: GRP5(w) /7>");
			break;
		default:
			break;
		}
	}
	if (CPU[activeCPU].thereg == 7) //Undefined opcode has priority over all other faults!
	{
		CPU_unkOP(); //Invalid: registers aren't allowed!
		return;
	}
	if (unlikely((CPU[activeCPU].modrmstep==0) && (CPU[activeCPU].internalmodrmstep==0) && (CPU[activeCPU].instructionstep==0)))
	{
		CPU[activeCPU].modrm_addoffset = 0;
		if ((CPU[activeCPU].thereg==3) || (CPU[activeCPU].thereg==5)) //extra segment?
		{
			if (modrm_isregister(CPU[activeCPU].params)) //Invalid?
			{
				CPU_unkOP(); //Invalid: registers aren't allowed!
				return;
			}
		}
		if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,1|0x40)) return; //Abort when needed!
		if ((CPU[activeCPU].thereg==3) || (CPU[activeCPU].thereg==5)) //extra segment?
		{
			CPU[activeCPU].modrm_addoffset = 2;
			if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,1|0x40)) return; //Abort when needed!		
		}
		CPU[activeCPU].modrm_addoffset = 0;
		if (CPU[activeCPU].thereg == 3) //far CALL?
		{
			if (getcpumode() != CPU_MODE_PROTECTED) //Real mode or V86 mode?
			{
				if (unlikely(CPU[activeCPU].stackchecked == 0))
				{
					if (checkStackAccess(2, 1, 0)) return; /*We're trying to push on the stack!*/
					++CPU[activeCPU].stackchecked;
				}
			}
		}
		else if ((CPU[activeCPU].thereg == 2) || (CPU[activeCPU].thereg == 6)) //pushing something on the stack normally?
		{
			if (unlikely(CPU[activeCPU].stackchecked == 0))
			{
				if (checkStackAccess(1, 1, 0)) return;
				++CPU[activeCPU].stackchecked;
			}
		}
		if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,1|0xA0)) return; //Abort when needed!
		if ((CPU[activeCPU].thereg==3) || (CPU[activeCPU].thereg==5)) //extra segment?
		{
			CPU[activeCPU].modrm_addoffset = 2;
			if (modrm_check16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,1|0xA0)) return; //Abort when needed!		
		}
		CPU[activeCPU].modrm_addoffset = 0;
	}
	if (CPU[activeCPU].thereg>1) //Data needs to be read directly? Not INC/DEC(which already reads it's data directly)?
	{
		if (CPU8086_instructionstepreadmodrmw(0,&CPU[activeCPU].instructionbufferw,CPU[activeCPU].MODRM_src0)) return;
	}
	CPU[activeCPU].oper1 = CPU[activeCPU].instructionbufferw;
	op_grp5();
}

/*

Special stuff for NO COprocessor (8087) present/available (default)!

*/


void FPU8087_noCOOP(){
	debugger_setcommand("<No COprocessor OPcodes implemented!>");
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += MODRM_EA(CPU[activeCPU].params)?3:1; //No hardware interrupt to use anymore!
	}
}

//Gecontroleerd: 100% OK!

//Now, the GRP opcodes!

OPTINLINE void op_grp2_cycles(byte cnt, byte varshift)
{
	switch (varshift) //What type of shift are we using?
	{
	case 0: //Reg/Mem with 1 shift?
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			if (MODRM_EA(CPU[activeCPU].params)) //Mem?
			{
				CPU[activeCPU].cycles_OP += 4; //Mem
			}
			else //Reg?
			{
				CPU[activeCPU].cycles_OP += 1; //Reg
			}
		}
		break;
	case 1: //Reg/Mem with variable shift?
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			if (MODRM_EA(CPU[activeCPU].params)) //Mem?
			{
				CPU[activeCPU].cycles_OP += 9 + (cnt << 2); //Mem
			}
			else //Reg?
			{
				CPU[activeCPU].cycles_OP += 7 + (cnt << 2); //Reg
			}
		}
		break;
	case 2: //Reg/Mem with immediate variable shift(NEC V20/V30)?
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			if (MODRM_EA(CPU[activeCPU].params)) //Mem?
			{
				CPU[activeCPU].cycles_OP += 5 + (cnt << 2) - (EU_CYCLES_SUBSTRACT_ACCESSRW); //Mem
			}
			else //Reg?
			{
				CPU[activeCPU].cycles_OP += 3 + (cnt << 2); //Reg
			}
		}
		break;
	default:
		break;
	}
}

byte op_grp2_8(byte cnt, byte varshift)
{
	INLINEREGISTER word s, shift, tempCF, msb;
	INLINEREGISTER byte numcnt, maskcnt, overflow;
	numcnt = maskcnt = cnt; //Save count!
	s = CPU[activeCPU].oper1b;
	switch (CPU[activeCPU].thereg)
	{
	case 0: //ROL r/m8
		if (EMULATED_CPU >= CPU_NECV30) maskcnt &= 0x1F; //Clear the upper 3 bits to become a NEC V20/V30+!
		numcnt = maskcnt;
		if (EMULATED_CPU>=CPU_80386) numcnt &= 7; //Operand size wrap!
		overflow = numcnt?0:FLAG_OF;
		for (shift = 1; shift <= numcnt; shift++)
		{
			FLAGW_CF(s>>7); //Save MSB!
			s = (s << 1)|FLAG_CF;
			overflow = (((s >> 7) & 1)^FLAG_CF); //Only when not using CL?
		}
		if (maskcnt && (numcnt==0)) FLAGW_CF(s); //Always sets CF, according to various sources?
		if (maskcnt) FLAGW_OF(overflow);
		break;

	case 1: //ROR r/m8
		if (EMULATED_CPU >= CPU_NECV30) maskcnt &= 0x1F; //Clear the upper 3 bits to become a NEC V20/V30+!
		numcnt = maskcnt;
		if (EMULATED_CPU>=CPU_80386) numcnt &= 7; //Operand size wrap!
		overflow = numcnt?0:FLAG_OF;
		for (shift = 1; shift <= numcnt; shift++)
		{
			FLAGW_CF(s); //Save LSB!
			s = ((s >> 1)&0x7FU) | (FLAG_CF << 7);
			overflow = ((s >> 7) ^ ((s >> 6) & 1)); //Only when not using CL?
		}
		if (maskcnt && (numcnt==0)) FLAGW_CF(s>>7); //Always sets CF, according to various sources?
		if (maskcnt) FLAGW_OF(overflow);
		break;

	case 2: //RCL r/m8
		if (EMULATED_CPU >= CPU_NECV30) maskcnt &= 0x1F; //Clear the upper 3 bits to become a NEC V20/V30+!
		numcnt = maskcnt;
		if ((EMULATED_CPU>=CPU_80386) && (maskcnt>9)) numcnt %= 9; //Operand size wrap!
		overflow = numcnt?0:FLAG_OF;
		for (shift = 1; shift <= numcnt; shift++)
		{
			tempCF = FLAG_CF;
			FLAGW_CF(s>>7); //Save MSB!
			s = (s << 1)|tempCF; //Shift and set CF!
			overflow = (((s >> 7) & 1)^FLAG_CF); //OF=MSB^CF, only when not using CL?
		}
		if (maskcnt && (numcnt==0)) FLAGW_CF(s); //Always sets CF, according to various sources?
		if (maskcnt) FLAGW_OF(overflow);
		break;

	case 3: //RCR r/m8
		if (EMULATED_CPU >= CPU_NECV30) maskcnt &= 0x1F; //Clear the upper 3 bits to become a NEC V20/V30+!
		numcnt = maskcnt;
		if ((EMULATED_CPU>=CPU_80386) && (maskcnt>9)) numcnt %= 9; //Operand size wrap!
		overflow = numcnt?0:FLAG_OF;
		for (shift = 1; shift <= numcnt; shift++)
		{
			overflow = (((s >> 7)&1)^FLAG_CF);
			tempCF = FLAG_CF;
			FLAGW_CF(s); //Save LSB!
			s = ((s >> 1)&0x7FU) | (tempCF << 7);
		}
		if (maskcnt && (numcnt==0)) FLAGW_CF(s); //Always sets CF, according to various sources?
		if (maskcnt) FLAGW_OF(overflow);
		break;

	case 4: case 6: //SHL r/m8
		if (EMULATED_CPU >= CPU_NECV30) maskcnt &= 0x1F; //Clear the upper 3 bits to become a NEC V20/V30+!
		numcnt = maskcnt;
		if ((EMULATED_CPU>=CPU_80386))
		{
			if (((maskcnt&0x18)==maskcnt) && maskcnt) //8/16/24 shifts?
			{
				numcnt = maskcnt = 8; //Brhave like a 8 bit shift!
			}
			else numcnt &= 7; //Limit count!
		}
		overflow = numcnt?0:FLAG_OF;
		for (shift = 1; shift <= numcnt; shift++)
		{
			FLAGW_CF(s>>7);
			s = (s << 1) & 0xFFU;
			overflow = (FLAG_CF^(s>>7));
		}
		if (maskcnt) flag_szp8((uint8_t)(s&0xFFU));
		if (maskcnt) FLAGW_OF(overflow);
		if (maskcnt) FLAGW_AF(1);
		break;

	case 5: //SHR r/m8
		if (EMULATED_CPU >= CPU_NECV30) maskcnt &= 0x1F; //Clear the upper 3 bits to become a NEC V20/V30+!
		numcnt = maskcnt;
		overflow = numcnt?0:FLAG_OF;
		if (maskcnt && ((maskcnt&7)==0))
		{
			//Adjusted according to IBMulator!
			FLAGW_CF(s>>7); //Always sets CF, according to various sources?
		}
		for (shift = 1; shift <= numcnt; shift++)
		{
			overflow = (s>>7);
			if (numcnt&7) FLAGW_CF(s);
			s = s >> 1;
		}
		if (maskcnt) flag_szp8((uint8_t)(s & 0xFFU));
		if (maskcnt) FLAGW_OF(overflow);
		if (maskcnt) FLAGW_AF(1);
		break;

	case 7: //SAR r/m8
		if (EMULATED_CPU >= CPU_NECV30) maskcnt &= 0x1F; //Clear the upper 3 bits to become a NEC V20/V30+!
		numcnt = maskcnt;
		msb = s & 0x80U;
		for (shift = 1; shift <= numcnt; shift++)
		{
			FLAGW_CF(s);
			s = (s >> 1) | msb;
		}
		if (maskcnt && (numcnt==0)) FLAGW_CF(s); //Always sets CF, according to various sources?
		if (maskcnt) FLAGW_AF(1);
		byte tempSF;
		tempSF = FLAG_SF; //Save the SF!
		//http://www.electronics.dit.ie/staff/tscarff/8086_instruction_set/8086_instruction_set.html#SAR says only C and O flags!
		if (!maskcnt) //Nothing done?
		{
			FLAGW_SF(tempSF); //We don't update when nothing's done!
		}
		else if (maskcnt==1) //Overflow is cleared on all 1-bit shifts!
		{
			flag_szp8((uint8_t)s); //Affect sign as well!
			FLAGW_OF(0); //Cleared!
		}
		else if (numcnt) //Anything shifted at all?
		{
			flag_szp8((uint8_t)s); //Affect sign as well!
			FLAGW_OF(0); //Cleared with count as well?
		}
		break;
	default:
		break;
	}
	op_grp2_cycles(numcnt, varshift);
	return (s & 0xFFU);
}

word op_grp2_16(byte cnt, byte varshift)
{
	INLINEREGISTER uint_32 s, shift, tempCF, msb;
	INLINEREGISTER byte numcnt, maskcnt, overflow;
	numcnt = maskcnt = cnt; //Save count!
	s = CPU[activeCPU].oper1;
	switch (CPU[activeCPU].thereg)
	{
	case 0: //ROL r/m16
		if (EMULATED_CPU >= CPU_NECV30) maskcnt &= 0x1F; //Clear the upper 3 bits to become a NEC V20/V30+!
		numcnt = maskcnt;
		if (EMULATED_CPU>=CPU_80386) numcnt &= 0xF; //Operand size wrap!
		overflow = numcnt?0:FLAG_OF;
		for (shift = 1; shift <= numcnt; shift++)
		{
			FLAGW_CF(s>>15); //Save MSB!
			s = (s << 1)|FLAG_CF;
			overflow = (((s >> 15) & 1)^FLAG_CF); //Only when not using CL?
		}
		if (maskcnt && (numcnt==0)) FLAGW_CF(s); //Always sets CF, according to various sources?
		if (maskcnt) FLAGW_OF(overflow);
		break;

	case 1: //ROR r/m16
		if (EMULATED_CPU >= CPU_NECV30) maskcnt &= 0x1F; //Clear the upper 3 bits to become a NEC V20/V30+!
		numcnt = maskcnt;
		if (EMULATED_CPU>=CPU_80386) numcnt &= 0xF; //Operand size wrap!
		overflow = numcnt?0:FLAG_OF;
		for (shift = 1; shift <= numcnt; shift++)
		{
			FLAGW_CF(s); //Save LSB!
			s = ((s >> 1)&0x7FFFU) | (FLAG_CF << 15);
			overflow = ((s >> 15) ^ ((s >> 14) & 1)); //Only when not using CL?
		}
		if (maskcnt && (numcnt==0)) FLAGW_CF(s>>15); //Always sets CF, according to various sources?
		if (maskcnt) FLAGW_OF(overflow);
		break;

	case 2: //RCL r/m16
		if (EMULATED_CPU >= CPU_NECV30) maskcnt &= 0x1F; //Clear the upper 3 bits to become a NEC V20/V30+!
		numcnt = maskcnt;
		if ((EMULATED_CPU>=CPU_80386) && (maskcnt>17)) numcnt %= 17; //Operand size wrap!
		overflow = numcnt?0:FLAG_OF;
		for (shift = 1; shift <= numcnt; shift++)
		{
			tempCF = FLAG_CF;
			FLAGW_CF(s>>15); //Save MSB!
			s = (s << 1)|tempCF; //Shift and set CF!
			overflow = (((s >> 15) & 1)^FLAG_CF); //OF=MSB^CF, only when not using CL?
		}
		if (maskcnt && (numcnt==0)) FLAGW_CF(s); //Always sets CF, according to various sources?
		if (maskcnt) FLAGW_OF(overflow);
		break;

	case 3: //RCR r/m16
		if (EMULATED_CPU >= CPU_NECV30) maskcnt &= 0x1F; //Clear the upper 3 bits to become a NEC V20/V30+!
		numcnt = maskcnt;
		if ((EMULATED_CPU>=CPU_80386) && (maskcnt>17)) numcnt %= 17; //Operand size wrap!
		overflow = numcnt?0:FLAG_OF; //Default: no overflow!
		for (shift = 1; shift <= numcnt; shift++)
		{
			overflow = ((s >> 15)^FLAG_CF);
			tempCF = FLAG_CF;
			FLAGW_CF(s); //Save LSB!
			s = ((s >> 1)&0x7FFFU) | (tempCF << 15);
		}
		if (maskcnt && (numcnt==0)) FLAGW_CF(s); //Always sets CF, according to various sources?
		if (maskcnt) FLAGW_OF(overflow);
		break;

	case 4: case 6: //SHL r/m16
		if (EMULATED_CPU >= CPU_NECV30) maskcnt &= 0x1F; //Clear the upper 3 bits to become a NEC V20/V30+!
		numcnt = maskcnt;
		overflow = numcnt?0:FLAG_OF; //Default: no overflow!
		for (shift = 1; shift <= numcnt; shift++)
		{
			FLAGW_CF(s>>15);
			s = (s << 1) & 0xFFFFU;
			overflow = (FLAG_CF^(s>>15));
		}
		if (maskcnt && (numcnt==0)) FLAGW_CF(s>>15); //Always sets CF, according to various sources?
		if (maskcnt) flag_szp16((uint16_t)(s&0xFFFFU));
		if (maskcnt) FLAGW_OF(overflow);
		if (maskcnt) FLAGW_AF(1);
		break;

	case 5: //SHR r/m16
		if (EMULATED_CPU >= CPU_NECV30) maskcnt &= 0x1F; //Clear the upper 3 bits to become a NEC V20/V30+!
		numcnt = maskcnt;
		overflow = numcnt?0:FLAG_OF; //Default: no overflow!
		for (shift = 1; shift <= numcnt; shift++)
		{
			overflow = (s>>15);
			FLAGW_CF(s);
			s = s >> 1;
		}
		if (maskcnt && (numcnt==0)) FLAGW_CF(s); //Always sets CF, according to various sources?
		if (maskcnt) flag_szp16((uint16_t)(s & 0xFFFFU));
		if (maskcnt) FLAGW_OF(overflow);
		if (maskcnt) FLAGW_AF(1);
		break;

	case 7: //SAR r/m16
		if (EMULATED_CPU >= CPU_NECV30) maskcnt &= 0x1F; //Clear the upper 3 bits to become a NEC V20/V30+!
		numcnt = maskcnt;
		msb = s & 0x8000U;
		for (shift = 1; shift <= numcnt; shift++)
		{
			FLAGW_CF(s);
			s = (s >> 1) | msb;
		}
		if (maskcnt && (numcnt==0)) FLAGW_CF(s); //Always sets CF, according to various sources?
		if (maskcnt) FLAGW_AF(1);
		byte tempSF;
		tempSF = FLAG_SF; //Save the SF!
		/*flag_szp8((uint8_t)(s & 0xFF));*/
		//http://www.electronics.dit.ie/staff/tscarff/8086_instruction_set/8086_instruction_set.html#SAR says only C and O flags!
		if (!maskcnt) //Nothing done?
		{
			FLAGW_SF(tempSF); //We don't update when nothing's done!
		}
		else if (maskcnt==1) //Overflow is cleared on all 1-bit shifts!
		{
			flag_szp16(s); //Affect sign as well!
			FLAGW_OF(0); //Cleared!
		}
		else if (numcnt) //Anything shifted at all?
		{
			flag_szp16(s); //Affect sign as well!
			FLAGW_OF(0); //Cleared with count as well?
		}
		break;
	default:
		break;
	}
	op_grp2_cycles(numcnt, varshift);
	return (s & 0xFFFFU);
}

OPTINLINE void op_div8(word valdiv, byte divisor)
{
	word quotient, remainder; //Result and modulo!
	byte error, applycycles; //Error/apply cycles!
	applycycles = 1; //Default: apply cycles!
	CPU8086_internal_DIV(valdiv,divisor,&quotient,&remainder,&error,8,&applycycles,0,0,0,0,(MODRM_EA(CPU[activeCPU].params)==0)); //Execute the unsigned division! 8-bits result and modulo!
	if (error==0) //No error?
	{
		REG_AL = (byte)(quotient&0xFF); //Quotient!
		REG_AH = (byte)(remainder&0xFF); //Remainder!
	}
	else //Error?
	{
		CPU_exDIV0(); //Exception!
		return; //Exception executed!
	}
}

OPTINLINE void op_idiv8(word valdiv, byte divisor)
{
	word quotient, remainder; //Result and modulo!
	byte error, applycycles; //Error/apply cycles!
	uint_32 valdivd;
	word divisorw;
	valdivd = valdiv;
	divisorw = divisor;
	if (valdiv&0x8000) valdivd |= 0xFFFF0000; //Sign extend to 32-bits!
	if (divisor&0x80) divisorw |= 0xFF00; //Sign extend to 16-bits!
	CPU8086_internal_IDIV(valdivd,divisorw,&quotient,&remainder,&error,8,&applycycles,0,(MODRM_EA(CPU[activeCPU].params)==0)); //Execute the unsigned division! 8-bits result and modulo!
	if (error==0) //No error?
	{
		REG_AL = (quotient&0xFF); //Quotient!
		REG_AH = (remainder&0xFF); //Remainder!
	}
	else //Error?
	{
		CPU_exDIV0(); //Exception!
		return; //Exception executed!
	}
}

extern byte CPU_databussize; //Current data bus size!

void op_grp3_8()
{
	byte applycycles;
	CPU[activeCPU].oper1 = signext(CPU[activeCPU].oper1b); CPU[activeCPU].oper2 = signext(CPU[activeCPU].oper2b);
	switch (CPU[activeCPU].thereg)
	{
	case 0: case 1: //TEST
		CPU8086_internal_TEST8(CPU[activeCPU].oper1b, CPU[activeCPU].immb,3);
		break;

	case 2: //NOT
		CPU[activeCPU].res8 = ~CPU[activeCPU].oper1b;
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			if (MODRM_EA(CPU[activeCPU].params)) //Memory?
			{
				CPU[activeCPU].cycles_OP += 4; //Mem!
			}
			else //Register?
			{
				CPU[activeCPU].cycles_OP += 4; //Reg!
			}
		}
		break;

	case 3: //NEG
		CPU[activeCPU].res8 = (~CPU[activeCPU].oper1b) + 1;
		flag_sub8(0, CPU[activeCPU].oper1b);
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			if (MODRM_EA(CPU[activeCPU].params)) //Memory?
			{
				CPU[activeCPU].cycles_OP += 4; //Mem!
			}
			else //Register?
			{
				CPU[activeCPU].cycles_OP += 4; //Reg!
			}
		}
		break;

	case 4: //MULB
		applycycles = 1;
		CPU8086_internal_MUL(REG_AL, (word)CPU[activeCPU].oper1b, &CPU[activeCPU].oper1, &CPU[activeCPU].oper2, 8, &applycycles, 0, 0, 0, modrm_isregister(CPU[activeCPU].params)); //Execute MUL!
		CPU[activeCPU].temp1.val16 = CPU[activeCPU].oper1;
		CPU[activeCPU].temp2.val16 = CPU[activeCPU].oper2;
		REG_AL = (CPU[activeCPU].temp1.val16&0xFF);
		REG_AH = (CPU[activeCPU].temp2.val16&0xFF);
		CPU[activeCPU].tempAL = FLAG_ZF; //Backup!
		flag_log8((byte)CPU[activeCPU].temp1.val16); //Flags!
		if ((REG_AX&0xFF00)==0)
		{
			FLAGW_OF(0); //Both zeroed!
		}
		else FLAGW_OF(1); //Set due to overflow!

		FLAGW_CF(FLAG_OF); //Same!
		if (EMULATED_CPU==CPU_8086) //8086 only?
		{
			FLAGW_ZF(CPU[activeCPU].tempAL); //Restore Zero flag!
			if (REG_AX) FLAGW_ZF(0); //8086/8088 clears the Zero flag when not zero only. Undocumented bug!
		}
		break;

	case 5: //IMULB
		CPU8086_internal_IMUL(REG_AL, (word)CPU[activeCPU].oper1b, &CPU[activeCPU].oper1, &CPU[activeCPU].oper2, 8, &applycycles, 0, modrm_isregister(CPU[activeCPU].params)); //Execute MUL!
		CPU[activeCPU].temp1.val16 = CPU[activeCPU].oper1;
		CPU[activeCPU].temp2.val16 = CPU[activeCPU].oper2;
		REG_AL = (byte)CPU[activeCPU].temp1.val16; //Load into AX!
		REG_AH = (byte)CPU[activeCPU].temp2.val16; //Load into AX!
		flag_log8((byte)CPU[activeCPU].temp1.val16); //Flags!
		if (((REG_AX&0xFF80)==0) || ((REG_AX&0xFF80)==0xFF80))
		{
			FLAGW_OF(0); //Both zeroed!
		}
		else FLAGW_OF(1); //Set due to overflow!
		FLAGW_CF(FLAG_OF); //Same!
		if (EMULATED_CPU==CPU_8086)
		{
			FLAGW_ZF(0); //Clear ZF!
		}
		break;

	case 6: //DIV
		op_div8(REG_AX, CPU[activeCPU].oper1b);
		break;

	case 7: //IDIV
		op_idiv8(REG_AX, CPU[activeCPU].oper1b);
		break;
	default:
		break;
	}
}

OPTINLINE void op_div16(uint32_t valdiv, word divisor)
{
	word quotient, remainder; //Result and modulo!
	byte error, applycycles; //Error/apply cycles!
	applycycles = 1; //Default: apply cycles!
	CPU8086_internal_DIV(valdiv,divisor,&quotient,&remainder,&error,16,&applycycles,0,0,0,0,(MODRM_EA(CPU[activeCPU].params)==0)); //Execute the unsigned division! 8-bits result and modulo!
	if (error==0) //No error?
	{
		REG_AX = quotient; //Quotient!
		REG_DX = remainder; //Remainder!
	}
	else //Error?
	{
		CPU_exDIV0(); //Exception!
		return; //Exception executed!
	}
}

OPTINLINE void op_idiv16(uint32_t valdiv, word divisor)
{
	word quotient, remainder; //Result and modulo!
	byte error, applycycles; //Error/apply cycles!
	CPU8086_internal_IDIV(valdiv,divisor,&quotient,&remainder,&error,16,&applycycles,0,(MODRM_EA(CPU[activeCPU].params)==0)); //Execute the unsigned division! 8-bits result and modulo!
	if (error==0) //No error?
	{
		REG_AX = quotient; //Quotient!
		REG_DX = remainder; //Remainder!
	}
	else //Error?
	{
		CPU_exDIV0(); //Exception!
		return; //Exception executed!
	}
}

void op_grp3_16()
{
	byte applycycles;
	switch (CPU[activeCPU].thereg)
	{
	case 0: case 1: //TEST
		CPU8086_internal_TEST16(CPU[activeCPU].oper1, CPU[activeCPU].immw, 3);
		break;
	case 2: //NOT
		CPU[activeCPU].res16 = ~CPU[activeCPU].oper1;
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			if (MODRM_EA(CPU[activeCPU].params)) //Memory?
			{
				CPU[activeCPU].cycles_OP += 4; //Mem!
			}
			else //Register?
			{
				CPU[activeCPU].cycles_OP += 4; //Reg!
			}
		}
		break;
	case 3: //NEG
		CPU[activeCPU].res16 = (~CPU[activeCPU].oper1) + 1;
		flag_sub16(0, CPU[activeCPU].oper1);
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			if (MODRM_EA(CPU[activeCPU].params)) //Memory?
			{
				CPU[activeCPU].cycles_OP += 4; //Mem!
			}
			else //Register?
			{
				CPU[activeCPU].cycles_OP += 4; //Reg!
			}
		}
		break;
	case 4: //MULW
		applycycles = 1;
		CPU8086_internal_MUL(REG_AX, (word)CPU[activeCPU].oper1, &CPU[activeCPU].oper1, &CPU[activeCPU].oper2, 16, &applycycles, 0, 0, 0, modrm_isregister(CPU[activeCPU].params)); //Execute MUL!
		CPU[activeCPU].temp1.val16 = CPU[activeCPU].oper1;
		CPU[activeCPU].temp2.val16 = CPU[activeCPU].oper2;
		REG_AX = CPU[activeCPU].temp1.val16;
		REG_DX = CPU[activeCPU].temp2.val16;
		CPU[activeCPU].tempAL = FLAG_ZF; //Backup!
		flag_log16(CPU[activeCPU].temp1.val16); //Flags!
		if (CPU[activeCPU].temp2.val16==0) FLAGW_OF(0);
		else FLAGW_OF(1);
		FLAGW_CF(FLAG_OF); //OF=CF!

		if (EMULATED_CPU==CPU_8086)
		{
			FLAGW_ZF(CPU[activeCPU].tempAL); //Restore!
			if ((EMULATED_CPU==CPU_8086) && (CPU[activeCPU].temp1.val16| CPU[activeCPU].temp2.val16)) FLAGW_ZF(0); //8086/8088 clears the Zero flag when not zero only.
		}
		break;
	case 5: //IMULW
		CPU8086_internal_IMUL(REG_AX, (word)CPU[activeCPU].oper1, &CPU[activeCPU].oper1, &CPU[activeCPU].oper2, 16, &applycycles, 0, modrm_isregister(CPU[activeCPU].params)); //Execute MUL!
		CPU[activeCPU].temp1.val16 = CPU[activeCPU].oper1;
		CPU[activeCPU].temp2.val16 = CPU[activeCPU].oper2;
		REG_AX = CPU[activeCPU].temp1.val16; //into register ax
		REG_DX = CPU[activeCPU].temp2.val16; //into register dx
		flag_log16(CPU[activeCPU].temp1.val16); //Flags!
		CPU[activeCPU].temp3.val32 = CPU[activeCPU].temp1.val16 | (CPU[activeCPU].temp2.val16 << 16); //Combine for convenience!
		if (((CPU[activeCPU].temp3.val32>>15)==0) || ((CPU[activeCPU].temp3.val32>>15)==0x1FFFF)) FLAGW_OF(0);
		else FLAGW_OF(1);
		FLAGW_CF(FLAG_OF); //OF=CF!
		if (EMULATED_CPU==CPU_8086)
		{
			FLAGW_ZF(0); //Clear ZF!
		}
		break;
	case 6: //DIV
		op_div16(((uint32_t)REG_DX << 16) | REG_AX, CPU[activeCPU].oper1);
		break;
	case 7: //IDIV
		op_idiv16(((uint32_t)REG_DX << 16) | REG_AX, CPU[activeCPU].oper1); break;
	default:
		break;
	}
}

void op_grp5()
{
	MODRM_PTR info; //To contain the info!
	switch (CPU[activeCPU].thereg)
	{
	case 0: //INC Ev
		if (unlikely(CPU[activeCPU].internalinstructionstep == 0))
		{
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return; //Abort when needed!
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return; //Abort when needed!
		}
		CPU8086_internal_INC16(modrm_addr16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0));
		break;
	case 1: //DEC Ev
		if (unlikely(CPU[activeCPU].internalinstructionstep == 0))
		{
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return; //Abort when needed!
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return; //Abort when needed!
		}
		CPU8086_internal_DEC16(modrm_addr16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0));
		break;
	case 2: //CALL Ev
		if (unlikely(CPU[activeCPU].stackchecked == 0))
		{
			if (checkStackAccess(1, 1, 0)) return;
			++CPU[activeCPU].stackchecked;
		}
		if (EMULATED_CPU <= CPU_NECV30) //8086 timings?
		{
			if (CPU8086_instructionstepdelayBIUidle(0, 1)) return; //Wait 1 cycle!
			if (CPU8086_instructionstepdelayBIUidle(2, 4 + (MODRM_EA(CPU[activeCPU].params) ? 1 : 0))) return; //Wait 4 idle cycles, wait 1 more for memory!
			if (CPU8086_instructionstepwaitBIUready(4)) return; //Wait for the BIU to become ready!
			if (CPU8086_instructionstepdelayBIUidle(6, 1)) return; //Wait 1 cycle more!
			if (CPU8086_instructionstepdelayBIUidle(8, 2)) return; //Wait 2 cycles more!
			if (CPU8086_PUSHw(10, &REG_IP, 0)) return; //PUSH IP!
		}
		else
		{
			if (CPU8086_PUSHw(0, &REG_IP, 0)) return; //PUSH IP!
		}
		CPU_JMPabs((uint_32)CPU[activeCPU].oper1, 0);
		if (EMULATED_CPU > CPU_NECV30) //Not 8086 timings?
		{
			if (CPU_apply286cycles() == 0) /* No 80286+ cycles instead? */
			{
				if (MODRM_EA(CPU[activeCPU].params)) //Mem?
				{
					CPU[activeCPU].cycles_OP += 9; /* Intrasegment indirect through memory */
				}
				else //Register?
				{
					CPU[activeCPU].cycles_OP += 8; /* Intrasegment indirect through register */
				}
				CPU[activeCPU].cycles_stallBIU += CPU[activeCPU].cycles_OP; /*Stall the BIU completely now!*/
			}
		}
		CPU_flushPIQ(-1); //We're jumping to another address!
		break;
	case 3: //CALL Mp
		memcpy(&info, &CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0], sizeof(CPU[activeCPU].info)); //Get data!

		CPU[activeCPU].modrm_addoffset = 0;

		CPU[activeCPU].destEIP = (uint_32)CPU[activeCPU].oper1; //Convert to EIP!
		if (EMULATED_CPU <= CPU_NECV30) //8086 timings?
		{
			if (CPU8086_instructionstepdelayBIU(0, 2+MODRM_EA(CPU[activeCPU].params)?1:0)) return; //2+ 1 for memory cycle first!
			CPU[activeCPU].modrm_addoffset = 2; //Then destination CS!
			if (CPU8086_instructionstepreadmodrmw(2, &CPU[activeCPU].destCS, CPU[activeCPU].MODRM_src0)) return; //Get destination CS!
			if (CPU8086_instructionstepdelayBIU(2, 2 + (MODRM_EA(CPU[activeCPU].params) ? 1 : 0))) return; //2 + 1 for memory cycle first!
			if (CPU8086_PUSHw(4, &REG_CS, 0)) return; //PUSH CS!
			if (CPU8086_instructionstepdelayBIU(6, 3)) return; //3 cycles for after the push!
			if (CPU8086_instructionstepdelayBIU(8, 4)) return; //4 cycles delay!
			//We set CS:IP now?
			if (CPU8086_instructionstepdelayBIU(10, 1)) return; //1 more cycle before starting the push of IP!
			if (CPU8086_PUSHw(12, &REG_IP, 0)) return; //PUSH IP!
			//Now, set CS:IP directly!
			segmentWritten(CPU_SEGMENT_CS, CPU[activeCPU].destCS, 0); //Set CS:IP directly!
		}
		else //Not 8086 timings?
		{
			CPUPROT1
			CPU[activeCPU].modrm_addoffset = 2; //Then destination CS!
			if (CPU8086_instructionstepreadmodrmw(2, &CPU[activeCPU].destCS, CPU[activeCPU].MODRM_src0)) return; //Get destination CS!
			CPUPROT1
			CPU[activeCPU].modrm_addoffset = 0;
			if (CPU8086_CALLF(CPU[activeCPU].destCS, CPU[activeCPU].destEIP)) return; //Call the destination address!
			CPUPROT1
			if (CPU_apply286cycles() == 0) /* No 80286+ cycles instead? */
			{
				if (MODRM_EA(CPU[activeCPU].params)) //Mem?
				{
					CPU[activeCPU].cycles_OP += 4; /* Intersegment indirect */
				}
				else //Register?
				{
					CPU[activeCPU].cycles_OP += 4; /* Intersegment direct */
				}
				CPU[activeCPU].cycles_stallBIU += CPU[activeCPU].cycles_OP; /*Stall the BIU completely now!*/
			}
			CPUPROT2
			CPUPROT2
			CPUPROT2
			}
		break;
	case 4: //JMP Ev
		CPU_JMPabs((uint_32)CPU[activeCPU].oper1,0);
		CPU_flushPIQ(-1); //We're jumping to another address!
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			if (MODRM_EA(CPU[activeCPU].params)) //Memory?
			{
				CPU[activeCPU].cycles_OP += 18 - EU_CYCLES_SUBSTRACT_ACCESSREAD; /* Intrasegment indirect through memory */
			}
			else //Register?
			{
				CPU[activeCPU].cycles_OP += 11; /* Intrasegment indirect through register */
			}
			CPU[activeCPU].cycles_stallBIU += CPU[activeCPU].cycles_OP; /*Stall the BIU completely now!*/
		}
		break;
	case 5: //JMP Mp
		memcpy(&info,&CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0],sizeof(info)); //Store the address for debugging!

		CPUPROT1
		CPU[activeCPU].destEIP = (uint_32)CPU[activeCPU].oper1; //Convert to EIP!
		CPU[activeCPU].modrm_addoffset = 2; //Then destination CS!
		if (EMULATED_CPU <= CPU_NECV30) //8086 timings?
		{
			if (CPU8086_instructionstepdelayBIU(0, 2 + MODRM_EA(CPU[activeCPU].params) ? 1 : 0)) return; //2+ 1 for memory cycle first!
			if (CPU8086_instructionstepreadmodrmw(2, &CPU[activeCPU].destCS, CPU[activeCPU].MODRM_src0)) return; //Get destination CS!
			if (CPU8086_instructionstepdelayBIU(2, 3)) return; //3 cycles for after the read!
			if (CPU8086_instructionstepdelayBIU(4, 1)) return; //1 cycles for after that!
			CPU[activeCPU].modrm_addoffset = 0;
			if (segmentWritten(CPU_SEGMENT_CS, CPU[activeCPU].destCS, 1)) return;
		}
		else
		{
			if (CPU8086_instructionstepreadmodrmw(2, &CPU[activeCPU].destCS, CPU[activeCPU].MODRM_src0)) return; //Get destination CS!
			CPU[activeCPU].modrm_addoffset = 0;
			CPUPROT1
			if (segmentWritten(CPU_SEGMENT_CS, CPU[activeCPU].destCS, 1)) return;
			CPUPROT1
				if (CPU_apply286cycles() == 0) /* No 80286+ cycles instead? */
				{
					if (MODRM_EA(CPU[activeCPU].params)) //Memory?
					{
						CPU[activeCPU].cycles_OP += 24 - (EU_CYCLES_SUBSTRACT_ACCESSREAD * 2); /* Intersegment indirect through memory */
					}
					else //Register?
					{
						CPU[activeCPU].cycles_OP += 11; /* Intersegment indirect through register */
					}
					CPU[activeCPU].cycles_stallBIU += CPU[activeCPU].cycles_OP; /*Stall the BIU completely now!*/
				}
			CPUPROT2
			CPUPROT2
			CPUPROT2
		}
		break;
	case 6: //PUSH Ev
		if (unlikely(CPU[activeCPU].stackchecked==0))
		{
			if (checkStackAccess(1,1,0)) return;
			++CPU[activeCPU].stackchecked;
		}
		if (EMULATED_CPU <= CPU_NECV30)
		{
			if (CPU8086_instructionstepdelayBIU(0, 1)) return; //1 cycle before starting!
		}
		if (modrm_addr16(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0)==&REG_SP) //SP?
		{
			if (CPU8086_PUSHw((EMULATED_CPU<=CPU_NECV30)?2:0,&REG_SP,0)) return;
		}
		else
		{
			if (CPU8086_PUSHw((EMULATED_CPU<=CPU_NECV30)?2:0,&CPU[activeCPU].oper1,0)) return;
		}
		CPUPROT1
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			if (EMULATED_CPU > CPU_NECV30)
			{
				if (MODRM_EA(CPU[activeCPU].params)) //Memory?
				{
					CPU[activeCPU].cycles_OP += 16 - (EU_CYCLES_SUBSTRACT_ACCESSRW); /*Push Mem!*/
				}
				else //Register?
				{
					CPU[activeCPU].cycles_OP += 11 - EU_CYCLES_SUBSTRACT_ACCESSWRITE; /*Push Reg!*/
				}
			}
		}
		CPUPROT2
		break;
	default: //Unknown OPcode?
		CPU_unkOP(); //Execute the unknown opcode exception handler, if any!
		break;
	}
}
