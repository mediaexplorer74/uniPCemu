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
#include "headers/cpu/mmu.h" //MMU needed!
#include "headers/cpu/easyregs.h" //Easy register compatibility!
#include "headers/cpu/modrm.h" //MODR/M compatibility!
#include "headers/support/signedness.h" //CPU support functions!
#include "headers/hardware/ports.h" //Ports compatibility!
#include "headers/cpu/cpu_OP8086.h" //Our own opcode presets!
#include "headers/cpu/fpu_OP8087.h" //Our own opcode presets!
#include "headers/cpu/flags.h" //Flag support!
#include "headers/cpu/8086_grpOPs.h" //GRP Opcode extensions!
#include "headers/cpu/interrupts.h" //Basic interrupt support!
#include "headers/emu/debugger/debugger.h" //CPU debugger support!
#include "headers/bios/bios.h" //BIOS support!
#include "headers/cpu/protection.h"
#include "headers/cpu/cpu_OPNECV30.h" //80186+ support!
#include "headers/cpu/cpu_OP80286.h" //80286+ support!
#include "headers/cpu/biu.h" //BIU support!
#include "headers/cpu/cpu_execution.h" //Execution phase support!
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

/*

First, 8086 32-bit extensions!

*/

//Prototypes for GRP code extensions!
void op_grp3_32(); //Prototype!
uint_32 op_grp2_32(byte cnt, byte varshift); //Prototype!
void op_grp5_32(); //Prototype

void INTdebugger80386() //Special INTerrupt debugger!
{
	if (DEBUGGER_LOG==DEBUGGERLOG_INT) //Interrupts only?
	{
		debuggerINT = 1; //Debug this instruction always!
	}
}

/*

Start of help for debugging

*/

OPTINLINE char *getLEAtext32(MODRM_PARAMS *theparams)
{
	modrm_lea32_text(theparams,1,&CPU[activeCPU].LEAtext[0]);    //Help function for LEA instruction!
	return &CPU[activeCPU].LEAtext[0];
}

/*

Start of help for opcode processing

*/

extern byte CPU_databussize; //0=16/32-bit bus! 1=8-bit bus when possible (8088/80188)!

OPTINLINE void CPU80386_IRET()
{
	CPUPROT1
	CPU_IRET(); //IRET!
	CPUPROT2
	if (CPU[activeCPU].executed) //Executed?
	{
		if (CPU_apply286cycles()) return; //80286+ cycles instead?
		CPU[activeCPU].cycles_OP = 24; /*Timings!*/
	}
}

/*

List of hardware interrupts:
0: Division by 0: Attempting to execute DIV/IDIV with divisor==0: IMPLEMENTED
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

OPTINLINE void op_adc32()
{
	CPU[activeCPU].res32 = CPU[activeCPU].oper1d + CPU[activeCPU].oper2d + FLAG_CF;
	flag_adc32 (CPU[activeCPU].oper1d, CPU[activeCPU].oper2d, FLAG_CF);
}

void op_add32()
{
	CPU[activeCPU].res32 = CPU[activeCPU].oper1d + CPU[activeCPU].oper2d;
	flag_add32 (CPU[activeCPU].oper1d, CPU[activeCPU].oper2d);
}

OPTINLINE void op_and32()
{
	CPU[activeCPU].res32 = CPU[activeCPU].oper1d & CPU[activeCPU].oper2d;
	flag_log32 (CPU[activeCPU].res32);
}

OPTINLINE void op_or32()
{
	CPU[activeCPU].res32 = CPU[activeCPU].oper1d | CPU[activeCPU].oper2d;
	flag_log32 (CPU[activeCPU].res32);
}

OPTINLINE void op_xor32()
{
	CPU[activeCPU].res32 = CPU[activeCPU].oper1d ^ CPU[activeCPU].oper2d;
	flag_log32 (CPU[activeCPU].res32);
}

OPTINLINE void op_sub32()
{
	CPU[activeCPU].res32 = CPU[activeCPU].oper1d - CPU[activeCPU].oper2d;
	flag_sub32 (CPU[activeCPU].oper1d, CPU[activeCPU].oper2d);
}

OPTINLINE void op_sbb32()
{
	CPU[activeCPU].res32 = CPU[activeCPU].oper1d - (CPU[activeCPU].oper2d + FLAG_CF);
	flag_sbb32 (CPU[activeCPU].oper1d, CPU[activeCPU].oper2d, FLAG_CF);
}

/*

32-bit versions of BIU operations!

*/

//Stack operation support through the BIU!
byte CPU80386_PUSHdw(word base, uint_32 *data)
{
	uint_32 temp;
	if (CPU[activeCPU].instructionstep==base) //First step? Request!
	{
		if (CPU_PUSH32_BIU(data)==0) //Not ready?
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
		if (BIU_readResultdw(&temp)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].instructionstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU80386_internal_PUSHdw(word base, uint_32 *data)
{
	uint_32 temp;
	if (CPU[activeCPU].internalinstructionstep==base) //First step? Request!
	{
		if (CPU_PUSH32_BIU(data)==0) //Not ready?
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
		if (BIU_readResultdw(&temp)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU80386_internal_interruptPUSHdw(word base, uint_32 *data)
{
	uint_32 temp;
	if (CPU[activeCPU].internalinterruptstep==base) //First step? Request!
	{
		if (CPU_PUSH32_BIU(data)==0) //Not ready?
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
		if (BIU_readResultdw(&temp)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalinterruptstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU80386_POPdw(word base, uint_32 *result)
{
	if (CPU[activeCPU].instructionstep==base) //First step? Request!
	{
		if (CPU_POP32_BIU()==0) //Not ready?
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
		if (BIU_readResultdw(result)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].instructionstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU80386_internal_POPdw(word base, uint_32 *result)
{
	if (CPU[activeCPU].internalinstructionstep==base) //First step? Request!
	{
		if (CPU_POP32_BIU()==0) //Not ready?
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
		if (BIU_readResultdw(result)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU80386_POPESP(word base)
{
	uint_32 result;
	if (CPU[activeCPU].instructionstep==base) //First step? Request!
	{
		if (CPU_request_MMUrdw(CPU_SEGMENT_SS,STACK_SEGMENT_DESCRIPTOR_B_BIT()?REG_ESP:REG_SP,!STACK_SEGMENT_DESCRIPTOR_B_BIT())==0) //Not ready?
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
		if (BIU_readResultdw(&REG_ESP)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		result = REG_ESP; //Save the popped value!
		if (STACK_SEGMENT_DESCRIPTOR_B_BIT()) //ESP?
		{
			REG_ESP += 4; //Add four for the correct result!
		}
		else
		{
			REG_SP += 4; //Add four for the correct result!
		}
		REG_ESP = result; //Give the correct result, according to http://www.felixcloutier.com/x86/POP.html!
		++CPU[activeCPU].instructionstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

//Instruction variants of ModR/M!

byte CPU80386_instructionstepreadmodrmdw(word base, uint_32 *result, byte paramnr)
{
	byte BIUtype;
	if (CPU[activeCPU].modrmstep==base) //First step? Request!
	{
		if ((BIUtype = modrm_read32_BIU(&CPU[activeCPU].params,paramnr,result))==0) //Not ready?
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
		if (BIU_readResultdw(result)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].modrmstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU80386_instructionstepwritemodrmdw(word base, uint_32 value, byte paramnr)
{
	uint_32 dummy;
	byte BIUtype;
	if (CPU[activeCPU].modrmstep==base) //First step? Request!
	{
		if ((BIUtype = modrm_write32_BIU(&CPU[activeCPU].params,paramnr,value))==0) //Not ready?
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
		if (BIU_readResultdw(&dummy)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].modrmstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU80386_instructionstepwritedirectdw(word base, sword segment, word segval, uint_32 offset, uint_32 val, byte is_offset16)
{
	uint_32 dummy;
	if (CPU[activeCPU].modrmstep == base) //First step? Request!
	{
		if (CPU_request_MMUwdw(segment, offset, val, is_offset16) == 0) //Not ready?
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
		if (BIU_readResultdw(&dummy) == 0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].modrmstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU80386_instructionstepreaddirectdw(word base, sword segment, word segval, uint_32 offset, uint_32 *result, byte is_offset16)
{
	if (CPU[activeCPU].modrmstep == base) //First step? Request!
	{
		if (CPU_request_MMUrdw(segment, offset, is_offset16) == 0) //Not ready?
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
		if (BIU_readResultdw(result) == 0) //Not ready?
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

byte CPU80386_internal_stepreadmodrmdw(word base, uint_32 *result, byte paramnr)
{
	byte BIUtype;
	if (CPU[activeCPU].internalmodrmstep==base) //First step? Request!
	{
		if ((BIUtype = modrm_read32_BIU(&CPU[activeCPU].params,paramnr,result))==0) //Not ready?
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
		if (BIU_readResultdw(result)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalmodrmstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU80386_internal_stepwritedirectdw(word base, sword segment, word segval, uint_32 offset, uint_32 val, byte is_offset16)
{
	uint_32 dummy;
	if (CPU[activeCPU].internalmodrmstep==base) //First step? Request!
	{
		if (CPU_request_MMUwdw(segment,offset,val,is_offset16)==0) //Not ready?
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
		if (BIU_readResultdw(&dummy)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalmodrmstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU80386_internal_stepreaddirectdw(word base, sword segment, word segval, uint_32 offset, uint_32 *result, byte is_offset16)
{
	if (CPU[activeCPU].internalmodrmstep==base) //First step? Request!
	{
		if (CPU_request_MMUrdw(segment,offset,is_offset16)==0) //Not ready?
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
		if (BIU_readResultdw(result)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalmodrmstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU80386_internal_stepreadinterruptdw(word base, sword segment, word segval, uint_32 offset, uint_32 *result, byte is_offset16)
{
	if (CPU[activeCPU].internalinterruptstep==base) //First step? Request!
	{
		if (CPU_request_MMUrdw(segment,offset,is_offset16)==0) //Not ready?
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
		if (BIU_readResultdw(result)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalinterruptstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU80386_internal_stepwritemodrmdw(word base, uint_32 value, byte paramnr)
{
	uint_32 dummy;
	byte BIUtype;
	if (CPU[activeCPU].internalmodrmstep==base) //First step? Request!
	{
		if ((BIUtype = modrm_write32_BIU(&CPU[activeCPU].params,paramnr,value))==0) //Not ready?
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
		if (BIU_readResultdw(&dummy)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalmodrmstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

/*

Start of general 80386+ CMP handlers!

*/

OPTINLINE void CMP_dw(uint_32 a, uint_32 b, byte flags) //Compare instruction!
{
	CPUPROT1
	flag_sub32(a,b); //Flags only!
	if (flags != 4) if (CPU_apply286cycles()) return; //80286+ cycles instead?
	switch (flags & 7)
	{
	case 0: //Default?
		break; //Unused!
	case 1: //Accumulator?
		CPU[activeCPU].cycles_OP += 4; //Imm-Reg
		break;
	case 2: //Determined by ModR/M?
		if (CPU[activeCPU].params.EA_cycles) //Memory is used?
		{
			CPU[activeCPU].cycles_OP += 9-EU_CYCLES_SUBSTRACT_ACCESSREAD; //Mem->Reg!
		}
		else //Reg->Reg?
		{
			CPU[activeCPU].cycles_OP += 3; //Reg->Reg!
		}
		break;
	case 3: //ModR/M+imm?
		if (CPU[activeCPU].params.EA_cycles) //Memory is used?
		{
			CPU[activeCPU].cycles_OP += 10-EU_CYCLES_SUBSTRACT_ACCESSREAD; //Mem->Reg!
		}
		else //Imm->Reg?
		{
			CPU[activeCPU].cycles_OP += 4; //Reg->Reg!
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

//Custom memory support!

/*

Start of general 80386+ instruction handlers!

*/

//Help functions:
OPTINLINE byte CPU80386_internal_INC32(uint_32 *reg)
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
				if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
				if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0xA0)) return 1; //Abort on fault!
			}
		}
		if (reg==NULL) //Needs a read from memory?
		{
			if (CPU80386_internal_stepreadmodrmdw(0,&CPU[activeCPU].oper1d, CPU[activeCPU].MODRM_src0)) return 1;
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		CPU[activeCPU].oper1d = reg?*reg: CPU[activeCPU].oper1d;
		CPU[activeCPU].oper2d = 1;
		op_add32();
		FLAGW_CF(tempCF);
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		if (reg==NULL) //Destination to write?
		{
			if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
			{
				CPU[activeCPU].cycles_OP += 15-(EU_CYCLES_SUBSTRACT_ACCESSRW); //Mem
			}
			CPU[activeCPU].executed = 0;
			return 1; //Wait for execution phase to finish!
		}
	}
	if (reg) //Register?
	{
		*reg = CPU[activeCPU].res32;
		if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
		{
			CPU[activeCPU].cycles_OP += 2; //16-bit reg!
		}
	}
	else //Memory?
	{
		if (reg==NULL) //Needs a read from memory?
		{
			if (CPU80386_internal_stepwritemodrmdw(2, CPU[activeCPU].res32, CPU[activeCPU].MODRM_src0)) return 1;
		}
	}
	CPUPROT2
	return 0;
}
OPTINLINE byte CPU80386_internal_DEC32(uint_32 *reg)
{
	CPUPROT1
	INLINEREGISTER byte tempCF = FLAG_CF; //CF isn't changed!
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].internalmodrmstep==0))
		{
			if (reg==NULL)
			{
				if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
				if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0xA0)) return 1; //Abort on fault!
			}
		}
		if (reg==NULL) //Needs a read from memory?
		{
			if (CPU80386_internal_stepreadmodrmdw(0,&CPU[activeCPU].oper1d, CPU[activeCPU].MODRM_src0)) return 1;
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		CPU[activeCPU].oper1d = reg?*reg: CPU[activeCPU].oper1d;
		CPU[activeCPU].oper2d = 1;
		op_sub32();
		FLAGW_CF(tempCF);
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		if (reg==NULL) //Destination to write?
		{
			if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
			{
				CPU[activeCPU].cycles_OP += 15-(EU_CYCLES_SUBSTRACT_ACCESSRW); //Mem
			}
			CPU[activeCPU].executed = 0;
			return 1; //Wait for execution phase to finish!
		}
	}
	if (reg) //Register?
	{
		*reg = CPU[activeCPU].res32;
		if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
		{
			CPU[activeCPU].cycles_OP += 2; //16-bit reg!
		}
	}
	else //Memory?
	{
		if (reg==NULL) //Needs a read from memory?
		{
			if (CPU80386_internal_stepwritemodrmdw(2, CPU[activeCPU].res32, CPU[activeCPU].MODRM_src0)) return 1;
		}
	}
	CPUPROT2
	return 0;
}

OPTINLINE void timing_AND_OR_XOR_ADD_SUB32(uint_32 *dest, byte flags)
{
	if (CPU_apply286cycles()) return; //No 80286+ cycles instead?
	switch (flags) //What type of operation?
	{
	case 0: //Reg+Reg?
		CPU[activeCPU].cycles_OP += 3; //Reg->Reg!
		break;
	case 1: //Reg+imm?
		CPU[activeCPU].cycles_OP += 4; //Accumulator!
		break;
	case 2: //Determined by ModR/M?
		if (CPU[activeCPU].params.EA_cycles) //Memory is used?
		{
			if (dest) //Mem->Reg?
			{
				CPU[activeCPU].cycles_OP += 9-EU_CYCLES_SUBSTRACT_ACCESSREAD; //Mem->Reg!
			}
			else //Reg->Mem?
			{
				CPU[activeCPU].cycles_OP += 16-(EU_CYCLES_SUBSTRACT_ACCESSRW); //Mem->Reg!
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
				CPU[activeCPU].cycles_OP += 4; //Imm->Reg!
			}
			else //Imm->Mem?
			{
				CPU[activeCPU].cycles_OP += 17-(EU_CYCLES_SUBSTRACT_ACCESSRW); //Mem->Reg!
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

//For ADD
OPTINLINE byte CPU80386_internal_ADD32(uint_32 *dest, uint_32 addition, byte flags)
{
	CPUPROT1
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].internalmodrmstep==0))
		{
			if (dest==NULL)
			{
				if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
				if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0xA0)) return 1; //Abort on fault!
			}
		}
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU80386_internal_stepreadmodrmdw(0,&CPU[activeCPU].oper1d, CPU[activeCPU].MODRM_src0)) return 1;
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		CPU[activeCPU].oper1d = dest?*dest: CPU[activeCPU].oper1d;
		CPU[activeCPU].oper2d = addition;
		op_add32();
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		timing_AND_OR_XOR_ADD_SUB32(dest, flags);
		if (dest==NULL)
		{
			CPU[activeCPU].executed = 0;
			return 1;
		} //Wait for execution phase to finish!
	}
	if (dest) //Register?
	{
		*dest = CPU[activeCPU].res32;
	}
	else //Memory?
	{
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU80386_internal_stepwritemodrmdw(2, CPU[activeCPU].res32, CPU[activeCPU].MODRM_src0)) return 1;
		}
	}
	CPUPROT2
	return 0;
}

//For ADC
OPTINLINE byte CPU80386_internal_ADC32(uint_32 *dest, uint_32 addition, byte flags)
{
	CPUPROT1
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].internalmodrmstep==0))
		{
			if (dest==NULL)
			{
				if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
				if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0xA0)) return 1; //Abort on fault!
			}
		}
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU80386_internal_stepreadmodrmdw(0,&CPU[activeCPU].oper1d, CPU[activeCPU].MODRM_src0)) return 1;
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		CPU[activeCPU].oper1d = dest?*dest: CPU[activeCPU].oper1d;
		CPU[activeCPU].oper2d = addition;
		op_adc32();
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		timing_AND_OR_XOR_ADD_SUB32(dest, flags);
		if (dest==NULL)
		{
			CPU[activeCPU].executed = 0;
			return 1;
		} //Wait for execution phase to finish!
	}
	if (dest) //Register?
	{
		*dest = CPU[activeCPU].res32;
	}
	else //Memory?
	{
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU80386_internal_stepwritemodrmdw(2, CPU[activeCPU].res32, CPU[activeCPU].MODRM_src0)) return 1;
		}
	}
	CPUPROT2
	return 0;
}


//For OR
OPTINLINE byte CPU80386_internal_OR32(uint_32 *dest, uint_32 src, byte flags)
{
	CPUPROT1
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].internalmodrmstep==0))
		{
			if (dest==NULL)
			{
				if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
				if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0xA0)) return 1; //Abort on fault!
			}
		}
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU80386_internal_stepreadmodrmdw(0,&CPU[activeCPU].oper1d, CPU[activeCPU].MODRM_src0)) return 1;
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		CPU[activeCPU].oper1d = dest?*dest: CPU[activeCPU].oper1d;
		CPU[activeCPU].oper2d = src;
		op_or32();
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		timing_AND_OR_XOR_ADD_SUB32(dest, flags);
		if (dest==NULL)
		{
			CPU[activeCPU].executed = 0;
			return 1;
		} //Wait for execution phase to finish!
	}
	if (dest) //Register?
	{
		*dest = CPU[activeCPU].res32;
	}
	else //Memory?
	{
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU80386_internal_stepwritemodrmdw(2, CPU[activeCPU].res32, CPU[activeCPU].MODRM_src0)) return 1;
		}
	}
	CPUPROT2
	return 0;
}
//For AND
OPTINLINE byte CPU80386_internal_AND32(uint_32 *dest, uint_32 src, byte flags)
{
	CPUPROT1
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].internalmodrmstep == 0))
		{
			if (dest == NULL)
			{
				if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0x40)) return 1; //Abort on fault on write only!
				if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0xA0)) return 1; //Abort on fault on write only!
			}
		}
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU80386_internal_stepreadmodrmdw(0,&CPU[activeCPU].oper1d, CPU[activeCPU].MODRM_src0)) return 1;
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		CPU[activeCPU].oper1d = dest?*dest: CPU[activeCPU].oper1d;
		CPU[activeCPU].oper2d = src;
		op_and32();
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		timing_AND_OR_XOR_ADD_SUB32(dest, flags);
		if (dest==NULL)
		{
			CPU[activeCPU].executed = 0;
			return 1;
		} //Wait for execution phase to finish!
	}
	if (dest) //Register?
	{
		*dest = CPU[activeCPU].res32;
	}
	else //Memory?
	{
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU80386_internal_stepwritemodrmdw(2, CPU[activeCPU].res32, CPU[activeCPU].MODRM_src0)) return 1;
		}
	}
	CPUPROT2
	return 0;
}


//For SUB
OPTINLINE byte CPU80386_internal_SUB32(uint_32 *dest, uint_32 addition, byte flags)
{
	CPUPROT1
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].internalmodrmstep==0))
		{
			if (dest == NULL)
			{
				if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0x40)) return 1; //Abort on fault on write only!
				if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0xA0)) return 1; //Abort on fault on write only!
			}
		}
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU80386_internal_stepreadmodrmdw(0,&CPU[activeCPU].oper1d, CPU[activeCPU].MODRM_src0)) return 1;
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		CPU[activeCPU].oper1d = dest?*dest: CPU[activeCPU].oper1d;
		CPU[activeCPU].oper2d = addition;
		op_sub32();
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		timing_AND_OR_XOR_ADD_SUB32(dest, flags);
		if (dest==NULL)
		{
			CPU[activeCPU].executed = 0;
			return 1;
		} //Wait for execution phase to finish!
	}
	if (dest) //Register?
	{
		*dest = CPU[activeCPU].res32;
	}
	else //Memory?
	{
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU80386_internal_stepwritemodrmdw(2, CPU[activeCPU].res32, CPU[activeCPU].MODRM_src0)) return 1;
		}
	}
	CPUPROT2
	return 0;
}

//For SBB
OPTINLINE byte CPU80386_internal_SBB32(uint_32 *dest, uint_32 addition, byte flags)
{
	CPUPROT1
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].internalmodrmstep==0))
		{
			if (dest==NULL)
			{
				if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
				if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0xA0)) return 1; //Abort on fault!
			}
		}
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU80386_internal_stepreadmodrmdw(0,&CPU[activeCPU].oper1d, CPU[activeCPU].MODRM_src0)) return 1;
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		CPU[activeCPU].oper1d = dest?*dest: CPU[activeCPU].oper1d;
		CPU[activeCPU].oper2d = addition;
		op_sbb32();
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		timing_AND_OR_XOR_ADD_SUB32(dest, flags);
		if (dest==NULL)
		{
			CPU[activeCPU].executed = 0;
			return 1;
		} //Wait for execution phase to finish!
	}
	if (dest) //Register?
	{
		*dest = CPU[activeCPU].res32;
	}
	else //Memory?
	{
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU80386_internal_stepwritemodrmdw(2, CPU[activeCPU].res32, CPU[activeCPU].MODRM_src0)) return 1;
		}
	}
	CPUPROT2
	return 0;
}

//For XOR
//See AND, but XOR
OPTINLINE byte CPU80386_internal_XOR32(uint_32 *dest, uint_32 src, byte flags)
{
	CPUPROT1
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].internalmodrmstep==0))
		{
			if (dest==NULL)
			{
				if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
				if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0xA0)) return 1; //Abort on fault!
			}
		}
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU80386_internal_stepreadmodrmdw(0,&CPU[activeCPU].oper1d, CPU[activeCPU].MODRM_src0)) return 1;
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //Execution step?
	{
		CPU[activeCPU].oper1d = dest?*dest: CPU[activeCPU].oper1d;
		CPU[activeCPU].oper2d = src;
		op_xor32();
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		timing_AND_OR_XOR_ADD_SUB32(dest, flags);
		if (dest==NULL)
		{
			CPU[activeCPU].executed = 0;
			return 1;
		} //Wait for execution phase to finish!
	}
	if (dest) //Register?
	{
		*dest = CPU[activeCPU].res32;
	}
	else //Memory?
	{
		if (dest==NULL) //Needs a read from memory?
		{
			if (CPU80386_internal_stepwritemodrmdw(2, CPU[activeCPU].res32, CPU[activeCPU].MODRM_src0)) return 1;
		}
	}
	CPUPROT2
	return 0;
}

//TEST : same as AND, but discarding the result!
OPTINLINE byte CPU80386_internal_TEST32(uint_32 dest, uint_32 src, byte flags)
{
	CPUPROT1
	CPU[activeCPU].oper1d = dest;
	CPU[activeCPU].oper2d = src;
	op_and32();
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
			CPU[activeCPU].cycles_OP += 4; //Accumulator!
			break;
		case 2: //Determined by ModR/M?
			if (CPU[activeCPU].params.EA_cycles) //Memory is used?
			{
				//Mem->Reg/Reg->Mem?
				CPU[activeCPU].cycles_OP += 9-EU_CYCLES_SUBSTRACT_ACCESSREAD; //Mem->Reg!
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
					CPU[activeCPU].cycles_OP += 5; //Imm->Reg!
				}
				else //Imm->Mem?
				{
					CPU[activeCPU].cycles_OP += 11-EU_CYCLES_SUBSTRACT_ACCESSREAD; //Mem->Reg!
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
	SHLcycle: The amount of cycles for each SHL.
	ADDSUBcycle: The amount of cycles for ADD&SUB instruction to execute.
	issigned: Signed division?
	quotientnegative: Quotient is signed negative result?
	remaindernegative: Remainder is signed negative result?

*/
void CPU80386_internal_DIV(uint_64 val, uint_32 divisor, uint_32 *quotient, uint_32 *remainder, byte *error, byte resultbits, byte SHLcycle, byte ADDSUBcycle, byte *applycycles, byte issigned, byte quotientnegative, byte remaindernegative)
{
	uint_64 temp, temp2, currentquotient; //Remaining value and current divisor!
	uint_64 resultquotient;
	byte shift; //The shift to apply! No match on 0 shift is done!
	temp = val; //Load the value to divide!
	*applycycles = 1; //Default: apply the cycles normally!
	if (divisor==0) //Not able to divide?
	{
		*quotient = 0;
		*remainder = (uint_32)temp; //Unable to comply!
		*error = 1; //Divide by 0 error!
		return; //Abort: division by 0!
	}

	if (CPU_apply286cycles()) /* No 80286+ cycles instead? */
	{
		SHLcycle = ADDSUBcycle = 0; //Don't apply the cycle counts for this instruction!
		*applycycles = 0; //Don't apply the cycles anymore!
	}

	temp = val; //Load the remainder to use!
	resultquotient = 0; //Default: we have nothing after division! 
	nextstep:
	//First step: calculate shift so that (divisor<<shift)<=remainder and ((divisor<<(shift+1))>remainder)
	temp2 = divisor; //Load the default divisor for x1!
	if (temp2>temp) //Not enough to divide? We're done!
	{
		goto gotresult; //We've gotten a result!
	}
	currentquotient = 1; //We're starting with x1 factor!
	for (shift=0;shift<(resultbits+1);++shift) //Check for the biggest factor to apply(we're going from bit 0 to maxbit)!
	{
		if ((temp2<=temp) && ((temp2<<1)>temp)) //Found our value to divide?
		{
			CPU[activeCPU].cycles_OP += SHLcycle; //We're taking 1 more SHL cycle for this!
			break; //We've found our shift!
		}
		temp2 <<= 1; //Shift to the next position!
		currentquotient <<= 1; //Shift to the next result!
		CPU[activeCPU].cycles_OP += SHLcycle; //We're taking 1 SHL cycle for this! Assuming parallel shifting!
	}
	if (shift==(resultbits+1)) //We've overflown? We're too large to divide!
	{
		*error = 1; //Raise divide by 0 error due to overflow!
		return; //Abort!
	}
	//Second step: substract divisor<<n from remainder and increase result with 1<<n.
	temp -= temp2; //Substract divisor<<n from remainder!
	resultquotient += currentquotient; //Increase result(divided value) with the found power of 2 (1<<n).
	CPU[activeCPU].cycles_OP += ADDSUBcycle; //We're taking 1 substract and 1 addition cycle for this(ADD/SUB register take 3 cycles)!
	goto nextstep; //Start the next step!
	//Finished when remainder<divisor or remainder==0.
	gotresult: //We've gotten a result!
	if (temp>((1ULL<<resultbits)-1)) //Modulo overflow?
	{
		*error = 1; //Raise divide by 0 error due to overflow!
		return; //Abort!		
	}
	if (resultquotient>((1ULL<<resultbits)-1ULL)) //Quotient overflow?
	{
		*error = 1; //Raise divide by 0 error due to overflow!
		return; //Abort!		
	}
	if (issigned) //Check for signed overflow as well?
	{
		if (checkSignedOverflow(resultquotient,64,resultbits,quotientnegative))
		{
			*error = 1; //Raise divide by 0 error due to overflow!
			return; //Abort!
		}
	}
	*quotient = (uint_32)resultquotient; //Quotient calculated!
	*remainder = (uint_32)temp; //Give the modulo! The result is already calculated!
	*error = 0; //We're having a valid result!
}

void CPU80386_internal_IDIV(uint_64 val, uint_32 divisor, uint_32 *quotient, uint_32 *remainder, byte *error, byte resultbits, byte SHLcycle, byte ADDSUBcycle, byte *applycycles)
{
	byte quotientnegative, remaindernegative; //To toggle the result and apply sign after and before?
	quotientnegative = remaindernegative = 0; //Default: don't toggle the result not remainder!
	if (((val>>63)!=(divisor>>31))) //Are we to change signs on the result? The result is negative instead! (We're a +/- or -/+ division)
	{
		quotientnegative = 1; //We're to toggle the result sign if not zero!
	}
	if (val&0x8000000000000000ULL) //Negative value to divide?
	{
		val = ((~val)+1); //Convert the negative value to be positive!
		remaindernegative = 1; //We're to toggle the remainder is any, because the value to divide is negative!
	}
	if (divisor&0x80000000) //Negative divisor? Convert to a positive divisor!
	{
		divisor = ((~divisor)+1); //Convert the divisor to be positive!
	}
	CPU80386_internal_DIV(val,divisor,quotient,remainder,error,resultbits,SHLcycle,ADDSUBcycle,applycycles,1,quotientnegative,remaindernegative); //Execute the division as an unsigned division!
	if (*error==0) //No error has occurred? Do post-processing of the results!
	{
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

//MOV
OPTINLINE byte CPU80386_internal_MOV8(byte *dest, byte val, byte flags)
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
					CPU[activeCPU].cycles_OP += 10-EU_CYCLES_SUBSTRACT_ACCESSWRITE; //[imm16]->Accumulator!
					break;
				case 2: //ModR/M Memory->Reg?
					if (MODRM_EA(CPU[activeCPU].params)) //Memory?
					{
						CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSWRITE; //Mem->Reg!
					}
					else //Reg->Reg?
					{
						CPU[activeCPU].cycles_OP += 2; //Reg->Reg!
					}
					break;
				case 3: //ModR/M Memory immediate->Reg?
					if (MODRM_EA(CPU[activeCPU].params)) //Memory?
					{
						CPU[activeCPU].cycles_OP += 10-EU_CYCLES_SUBSTRACT_ACCESSWRITE; //Mem->Reg!
					}
					else //Reg->Reg?
					{
						CPU[activeCPU].cycles_OP += 2; //Reg->Reg!
					}
					break;
				case 4: //Register immediate->Reg?
					CPU[activeCPU].cycles_OP += 4; //Reg->Reg!
					break;
				case 8: //SegReg->Reg?
					if ((!CPU[activeCPU].MODRM_src1) || (MODRM_EA(CPU[activeCPU].params)==0)) //From register?
					{
						CPU[activeCPU].cycles_OP += 2; //Reg->SegReg!
					}
					else //From memory?
					{
						CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSWRITE; //Mem->SegReg!
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
					CPU[activeCPU].cycles_OP += 10-EU_CYCLES_SUBSTRACT_ACCESSWRITE; //Accumulator->[imm16]!
				}
			}
			else //ModR/M?
			{
				if (modrm_check8(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0)) return 1; //Abort on fault!
				if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
				{
					switch (flags) //What type are we?
					{
					case 0: //Reg+Reg?
						break; //Unused!
					case 1: //Accumulator from immediate memory address?
						CPU[activeCPU].cycles_OP += 10-EU_CYCLES_SUBSTRACT_ACCESSREAD; //Accumulator->[imm16]!
						break;
					case 2: //ModR/M Memory->Reg?
						if (MODRM_EA(CPU[activeCPU].params)) //Memory?
						{
							CPU[activeCPU].cycles_OP += 9-EU_CYCLES_SUBSTRACT_ACCESSREAD; //Mem->Reg!
						}
						else //Reg->Reg?
						{
							CPU[activeCPU].cycles_OP += 2; //Reg->Reg!
						}
						break;
					case 3: //ModR/M Memory immediate->Reg?
						if (MODRM_EA(CPU[activeCPU].params)) //Memory?
						{
							CPU[activeCPU].cycles_OP += 10-EU_CYCLES_SUBSTRACT_ACCESSREAD; //Mem->Reg!
						}
						else //Reg->Reg?
						{
							CPU[activeCPU].cycles_OP += 4; //Reg->Reg!
						}
						break;
					case 4: //Register immediate->Reg (Non-existant!!!)?
						CPU[activeCPU].cycles_OP += 4; //Reg->Reg!
						break;
					case 8: //Reg->SegReg?
						if (CPU[activeCPU].MODRM_src0 || (MODRM_EA(CPU[activeCPU].params) == 0)) //From register?
						{
							CPU[activeCPU].cycles_OP += 2; //SegReg->Reg!
						}
						else //From memory?
						{
							CPU[activeCPU].cycles_OP += 9-EU_CYCLES_SUBSTRACT_ACCESSREAD; //SegReg->Mem!
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
			if (CPU8086_internal_stepwritemodrmb(0,val, CPU[activeCPU].MODRM_src0)) return 1; //Write the result to memory!
		}
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	CPUPROT2
	return 0;
}

OPTINLINE byte CPU80386_internal_MOV16(word *dest, word val, byte flags)
{
	CPUPROT1
	if (CPU[activeCPU].internalinstructionstep==0) //First step? Execution only!
	{
		if (dest) //Register?
		{
			CPU[activeCPU].destEIP = REG_EIP; //Store (E)IP for safety!
			modrm_updatedsegment(dest,val,0); //Check for an updated segment!
			CPUPROT1
			if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
			{
				switch (flags) //What type are we?
				{
				case 0: //Reg+Reg?
					break; //Unused!
				case 1: //Accumulator from immediate memory address?
					CPU[activeCPU].cycles_OP += 10-EU_CYCLES_SUBSTRACT_ACCESSREAD; //[imm16]->Accumulator!
					break;
				case 2: //ModR/M Memory->Reg?
					if (MODRM_EA(CPU[activeCPU].params)) //Memory?
					{
						CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSWRITE; //Mem->Reg!
					}
					else //Reg->Reg?
					{
						CPU[activeCPU].cycles_OP += 2; //Reg->Reg!
					}
					break;
				case 3: //ModR/M Memory immediate->Reg?
					if (MODRM_EA(CPU[activeCPU].params)) //Memory?
					{
						CPU[activeCPU].cycles_OP += 10-EU_CYCLES_SUBSTRACT_ACCESSREAD; //Mem->Reg!
					}
					else //Reg->Reg?
					{
						CPU[activeCPU].cycles_OP += 2; //Reg->Reg!
					}
					break;
				case 4: //Register immediate->Reg?
					CPU[activeCPU].cycles_OP += 4; //Reg->Reg!
					break;
				case 8: //SegReg->Reg?
					if (CPU[activeCPU].MODRM_src0 || (MODRM_EA(CPU[activeCPU].params) == 0)) //From register?
					{
						CPU[activeCPU].cycles_OP += 2; //Reg->SegReg!
					}
					else //From memory?
					{
						CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSREAD; //Mem->SegReg!
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
					CPU[activeCPU].cycles_OP += 10-EU_CYCLES_SUBSTRACT_ACCESSWRITE; //Accumulator->[imm16]!
				}
			}
			else //ModR/M?
			{
				if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
				if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0xA0)) return 1; //Abort on fault!
				if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
				{
					switch (flags) //What type are we?
					{
					case 0: //Reg+Reg?
						break; //Unused!
					case 1: //Accumulator from immediate memory address?
						CPU[activeCPU].cycles_OP += 10-EU_CYCLES_SUBSTRACT_ACCESSWRITE; //Accumulator->[imm16]!
						break;
					case 2: //ModR/M Memory->Reg?
						if (MODRM_EA(CPU[activeCPU].params)) //Memory?
						{
							CPU[activeCPU].cycles_OP += 9-EU_CYCLES_SUBSTRACT_ACCESSWRITE; //Mem->Reg!
						}
						else //Reg->Reg?
						{
							CPU[activeCPU].cycles_OP += 2; //Reg->Reg!
						}
						break;
					case 3: //ModR/M Memory immediate->Reg?
						if (MODRM_EA(CPU[activeCPU].params)) //Memory?
						{
							CPU[activeCPU].cycles_OP += 10-EU_CYCLES_SUBSTRACT_ACCESSWRITE; //Mem->Reg!
						}
						else //Reg->Reg?
						{
							CPU[activeCPU].cycles_OP += 4; //Reg->Reg!
						}
						break;
					case 4: //Register immediate->Reg (Non-existant!!!)?
						CPU[activeCPU].cycles_OP += 4; //Reg->Reg!
						break;
					case 8: //Reg->SegReg?
						if (CPU[activeCPU].MODRM_src0 || (MODRM_EA(CPU[activeCPU].params) == 0)) //From register?
						{
							CPU[activeCPU].cycles_OP += 2; //SegReg->Reg!
						}
						else //From memory?
						{
							CPU[activeCPU].cycles_OP += 9-EU_CYCLES_SUBSTRACT_ACCESSWRITE; //SegReg->Mem!
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
			if (CPU8086_internal_stepwritemodrmw(0,val, CPU[activeCPU].MODRM_src0,0)) return 1; //Write the result to memory!
		}
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	CPUPROT2
	return 0;
}

/*

32-bit move for 80386+

*/

OPTINLINE byte CPU80386_internal_MOV32(uint_32 *dest, uint_32 val, byte flags)
{
	CPUPROT1
	if (CPU[activeCPU].internalinstructionstep==0) //First step? Execution only!
	{
		if (dest) //Register?
		{
			CPU[activeCPU].destEIP = REG_EIP; //Store (E)IP for safety!
			modrm_updatedsegment((word *)dest,(word)val,0); //Check for an updated segment!
			CPUPROT1
			if (get_segment_index((word *)dest)==-1) //We're not a segment?
			{
				*dest = val;
			}
			if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
			{
				switch (flags) //What type are we?
				{
				case 0: //Reg+Reg?
					break; //Unused!
				case 1: //Accumulator from immediate memory address?
					CPU[activeCPU].cycles_OP += 10-EU_CYCLES_SUBSTRACT_ACCESSREAD; //[imm16]->Accumulator!
					break;
				case 2: //ModR/M Memory->Reg?
					if (MODRM_EA(CPU[activeCPU].params)) //Memory?
					{
						CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSWRITE; //Mem->Reg!
					}
					else //Reg->Reg?
					{
						CPU[activeCPU].cycles_OP += 2; //Reg->Reg!
					}
					break;
				case 3: //ModR/M Memory immediate->Reg?
					if (MODRM_EA(CPU[activeCPU].params)) //Memory?
					{
						CPU[activeCPU].cycles_OP += 10-EU_CYCLES_SUBSTRACT_ACCESSREAD; //Mem->Reg!
					}
					else //Reg->Reg?
					{
						CPU[activeCPU].cycles_OP += 2; //Reg->Reg!
					}
					break;
				case 4: //Register immediate->Reg?
					CPU[activeCPU].cycles_OP += 4; //Reg->Reg!
					break;
				case 8: //SegReg->Reg?
					if (CPU[activeCPU].MODRM_src0 || (MODRM_EA(CPU[activeCPU].params) == 0)) //From register?
					{
						CPU[activeCPU].cycles_OP += 2; //Reg->SegReg!
					}
					else //From memory?
					{
						CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSREAD; //Mem->SegReg!
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
				if (checkMMUaccess32(CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),(CPU[activeCPU].customoffset&CPU[activeCPU].address_size),0|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0|0x10)) //Error accessing memory?
				{
					return 1; //Abort on fault!
				}
				if (checkMMUaccess32(CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].customoffset&CPU[activeCPU].address_size), 0|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0 | 0x10)) //Error accessing memory?
				{
					return 1; //Abort on fault!
				}
				if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
				{
					CPU[activeCPU].cycles_OP += 10-EU_CYCLES_SUBSTRACT_ACCESSWRITE; //Accumulator->[imm16]!
				}
			}
			else //ModR/M?
			{
				if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
				if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0xA0)) return 1; //Abort on fault!
				if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
				{
					switch (flags) //What type are we?
					{
					case 0: //Reg+Reg?
						break; //Unused!
					case 1: //Accumulator from immediate memory address?
						CPU[activeCPU].cycles_OP += 10-EU_CYCLES_SUBSTRACT_ACCESSWRITE; //Accumulator->[imm16]!
						break;
					case 2: //ModR/M Memory->Reg?
						if (MODRM_EA(CPU[activeCPU].params)) //Memory?
						{
							CPU[activeCPU].cycles_OP += 9-EU_CYCLES_SUBSTRACT_ACCESSWRITE; //Mem->Reg!
						}
						else //Reg->Reg?
						{
							CPU[activeCPU].cycles_OP += 2; //Reg->Reg!
						}
						break;
					case 3: //ModR/M Memory immediate->Reg?
						if (MODRM_EA(CPU[activeCPU].params)) //Memory?
						{
							CPU[activeCPU].cycles_OP += 10-EU_CYCLES_SUBSTRACT_ACCESSWRITE; //Mem->Reg!
						}
						else //Reg->Reg?
						{
							CPU[activeCPU].cycles_OP += 4; //Reg->Reg!
						}
						break;
					case 4: //Register immediate->Reg (Non-existant!!!)?
						CPU[activeCPU].cycles_OP += 4; //Reg->Reg!
						break;
					case 8: //Reg->SegReg?
						if (CPU[activeCPU].MODRM_src0 || (MODRM_EA(CPU[activeCPU].params) == 0)) //From register?
						{
							CPU[activeCPU].cycles_OP += 2; //SegReg->Reg!
						}
						else //From memory?
						{
							CPU[activeCPU].cycles_OP += 9-EU_CYCLES_SUBSTRACT_ACCESSWRITE; //SegReg->Mem!
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
			if (CPU80386_internal_stepwritedirectdw(0,CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),(CPU[activeCPU].customoffset&CPU[activeCPU].address_size),val,!CPU[activeCPU].CPU_Address_size)) return 1; //Write to memory directly!
		}
		else //ModR/M?
		{
			if (CPU80386_internal_stepwritemodrmdw(0,val, CPU[activeCPU].MODRM_src0)) return 1; //Write the result to memory!
		}
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	CPUPROT2
	return 0;
}


/*

80386 special

*/
//LEA for LDS, LES
OPTINLINE uint_32 getLEA32(MODRM_PARAMS *theparams)
{
	return modrm_lea32(theparams,1);
}


/*

Non-logarithmic opcodes for 80386+!

*/

OPTINLINE void CPU80386_internal_CWDE()
{
	CPUPROT1
	if ((REG_AX&0x8000)==0x8000)
	{
		REG_EAX |= 0xFFFF0000;
	}
	else
	{
		REG_EAX &= 0xFFFF;
	}
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP = 2; //Clock cycles!
	}
	CPUPROT2
}
OPTINLINE void CPU80386_internal_CDQ()
{
	CPUPROT1
	if ((REG_EAX&0x80000000)==0x80000000)
	{
		REG_EDX = 0xFFFFFFFF;
	}
	else
	{
		REG_EDX = 0;
	}
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP = 5; //Clock cycles!
	}
	CPUPROT2
}

//Now the repeatable instructions!

/*

80386 versions of the 8086+ 16-bit instructions!

*/

OPTINLINE byte CPU80386_internal_MOVSD()
{
	if (CPU[activeCPU].blockREP) return 1; //Disabled REP!
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (checkMMUaccess32(CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),(CPU[activeCPU].CPU_Address_size?REG_ESI:REG_SI),1|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0|0x10)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		if (checkMMUaccess32(CPU_SEGMENT_ES,REG_ES,(CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI),0|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0|0x10)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		if (checkMMUaccess32(CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].CPU_Address_size ? REG_ESI : REG_SI), 1|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0 | 0x10)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		if (checkMMUaccess32(CPU_SEGMENT_ES, REG_ES, (CPU[activeCPU].CPU_Address_size ? REG_EDI : REG_DI), 0|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0 | 0x10)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //First Execution step?
	{
		//Needs a read from memory?
		if (CPU80386_internal_stepreaddirectdw(0,CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].CPU_Address_size?REG_ESI:REG_SI), &CPU[activeCPU].MOVSD_data,!CPU[activeCPU].CPU_Address_size)) return 1; //Try to read the data!
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==2) //Execution step?
	{
		if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
		{
			if (CPU[activeCPU].repeating) //Are we a repeating instruction?
			{
				if (CPU[activeCPU].newREP) //Include the REP?
				{
					CPU[activeCPU].cycles_OP += 9 + 17 - (EU_CYCLES_SUBSTRACT_ACCESSRW); //Clock cycles including REP!
				}
				else //Repeating instruction itself?
				{
					CPU[activeCPU].cycles_OP += 17 - (EU_CYCLES_SUBSTRACT_ACCESSRW); //Clock cycles excluding REP!
				}
			}
			else //Plain non-repeating instruction?
			{
				CPU[activeCPU].cycles_OP += 18 - (EU_CYCLES_SUBSTRACT_ACCESSRW); //Clock cycles!
			}
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		CPU[activeCPU].executed = 0; return 1; //Wait for execution phase to finish!
	}
	//Writeback phase!
	if (CPU80386_internal_stepwritedirectdw(2,CPU_SEGMENT_ES,REG_ES,(CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI), CPU[activeCPU].MOVSD_data,!CPU[activeCPU].CPU_Address_size)) return 1;
	CPUPROT1
	if (FLAG_DF)
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			REG_ESI -= 4;
			REG_EDI -= 4;
		}
		else
		{
			REG_SI -= 4;
			REG_DI -= 4;
		}
	}
	else
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			REG_ESI += 4;
			REG_EDI += 4;
		}
		else
		{
			REG_SI += 4;
			REG_DI += 4;
		}
	}
	CPUPROT2
	return 0;
}

OPTINLINE byte CPU80386_internal_CMPSD()
{
	if (CPU[activeCPU].blockREP) return 1; //Disabled REP!
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (checkMMUaccess32(CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS),(CPU[activeCPU].CPU_Address_size?REG_ESI:REG_SI),1|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0|0x10)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		if (checkMMUaccess32(CPU_SEGMENT_ES, REG_ES, (CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI),1|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0|0x10)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		if (checkMMUaccess32(CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].CPU_Address_size ? REG_ESI : REG_SI), 1|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0 | 0x10)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		if (checkMMUaccess32(CPU_SEGMENT_ES, REG_ES, (CPU[activeCPU].CPU_Address_size ? REG_EDI : REG_DI), 1|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0 | 0x10)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //First Execution step?
	{
		//Needs a read from memory?
		if (CPU80386_internal_stepreaddirectdw(0,CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].CPU_Address_size?REG_ESI:REG_SI),&CPU[activeCPU].CMPSD_data1,!CPU[activeCPU].CPU_Address_size)) return 1; //Try to read the data!
		if (CPU80386_internal_stepreaddirectdw(2,CPU_SEGMENT_ES, REG_ES, (CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI), &CPU[activeCPU].CMPSD_data2,!CPU[activeCPU].CPU_Address_size)) return 1; //Try to read the data!
		
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	CMP_dw(CPU[activeCPU].CMPSD_data1, CPU[activeCPU].CMPSD_data2,4);
	if (FLAG_DF)
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			REG_ESI -= 4;
			REG_EDI -= 4;
		}
		else
		{
			REG_SI -= 4;
			REG_DI -= 4;
		}
	}
	else
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			REG_ESI += 4;
			REG_EDI += 4;
		}
		else
		{
			REG_SI += 4;
			REG_DI += 4;
		}
	}

	if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
	{
		if (CPU[activeCPU].repeating) //Are we a repeating instruction?
		{
			if (CPU[activeCPU].newREP) //Include the REP?
			{
				CPU[activeCPU].cycles_OP += 9 + 22 - (EU_CYCLES_SUBSTRACT_ACCESSREAD*2); //Clock cycles including REP!
			}
			else //Repeating instruction itself?
			{
				CPU[activeCPU].cycles_OP += 22 - (EU_CYCLES_SUBSTRACT_ACCESSREAD*2); //Clock cycles excluding REP!
			}
		}
		else //Plain non-repeating instruction?
		{
			CPU[activeCPU].cycles_OP += 22 - (EU_CYCLES_SUBSTRACT_ACCESSREAD*2); //Clock cycles!
		}
	}
	return 0;
}

OPTINLINE byte CPU80386_internal_STOSD()
{
	if (CPU[activeCPU].blockREP) return 1; //Disabled REP!
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (checkMMUaccess32(CPU_SEGMENT_ES, REG_ES, (CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI),0|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0|0x10)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		if (checkMMUaccess32(CPU_SEGMENT_ES, REG_ES, (CPU[activeCPU].CPU_Address_size ? REG_EDI : REG_DI), 0|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0 | 0x10)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //First Execution step?
	{
		//Needs a read from memory?
		if (CPU80386_internal_stepwritedirectdw(0,CPU_SEGMENT_ES,REG_ES,(CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI),REG_EAX,!CPU[activeCPU].CPU_Address_size)) return 1; //Try to read the data!
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	CPUPROT1
	if (FLAG_DF)
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			REG_EDI -= 4;
		}
		else
		{
			REG_DI -= 4;
		}
	}
	else
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			REG_EDI += 4;
		}
		else
		{
			REG_DI += 4;
		}
	}
	CPUPROT2
	if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
	{
		if (CPU[activeCPU].repeating) //Are we a repeating instruction?
		{
			if (CPU[activeCPU].newREP) //Include the REP?
			{
				CPU[activeCPU].cycles_OP += 9 + 10 - EU_CYCLES_SUBSTRACT_ACCESSWRITE; //Clock cycles including REP!
			}
			else //Repeating instruction itself?
			{
				CPU[activeCPU].cycles_OP += 10 - EU_CYCLES_SUBSTRACT_ACCESSWRITE; //Clock cycles excluding REP!
			}
		}
		else //Plain non-repeating instruction?
		{
			CPU[activeCPU].cycles_OP += 11 - EU_CYCLES_SUBSTRACT_ACCESSWRITE; //Clock cycles!
		}
	}
	return 0;
}
//OK so far!

OPTINLINE byte CPU80386_internal_LODSD()
{
	if (CPU[activeCPU].blockREP) return 1; //Disabled REP!
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (checkMMUaccess32(CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].CPU_Address_size?REG_ESI:REG_SI),1|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0|0x10)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		if (checkMMUaccess32(CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].CPU_Address_size ? REG_ESI : REG_SI), 1|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0 | 0x10)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		++CPU[activeCPU].internalinstructionstep;
	}
	if (CPU[activeCPU].internalinstructionstep==1) //First Execution step?
	{
		//Needs a read from memory?
		if (CPU80386_internal_stepreaddirectdw(0,CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].CPU_Address_size?REG_ESI:REG_SI), &CPU[activeCPU].LODSD_value,!CPU[activeCPU].CPU_Address_size)) return 1; //Try to read the data!
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	CPUPROT1
	REG_EAX = CPU[activeCPU].LODSD_value;
	if (FLAG_DF)
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			REG_ESI -= 4;
		}
		else
		{
			REG_SI -= 4;
		}
	}
	else
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			REG_ESI += 4;
		}
		else
		{
			REG_SI += 4;
		}
	}
	CPUPROT2
	if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
	{
		if (CPU[activeCPU].repeating) //Are we a repeating instruction?
		{
			if (CPU[activeCPU].newREP) //Include the REP?
			{
				CPU[activeCPU].cycles_OP += 9 + 13 - EU_CYCLES_SUBSTRACT_ACCESSREAD; //Clock cycles including REP!
			}
			else //Repeating instruction itself?
			{
				CPU[activeCPU].cycles_OP += 13 - EU_CYCLES_SUBSTRACT_ACCESSREAD; //Clock cycles excluding REP!
			}
		}
		else //Plain non-repeating instruction?
		{
			CPU[activeCPU].cycles_OP += 12 - EU_CYCLES_SUBSTRACT_ACCESSREAD; //Clock cycles!
		}
	}
	return 0;
}

OPTINLINE byte CPU80386_internal_SCASD()
{
	if (CPU[activeCPU].blockREP) return 1; //Disabled REP!
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (checkMMUaccess32(CPU_SEGMENT_ES, REG_ES, (CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI),1|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0|0x10)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		if (checkMMUaccess32(CPU_SEGMENT_ES, REG_ES, (CPU[activeCPU].CPU_Address_size ? REG_EDI : REG_DI), 1|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0 | 0x10)) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		++CPU[activeCPU].internalinstructionstep;
	}
	if (CPU[activeCPU].internalinstructionstep==1) //First Execution step?
	{
		//Needs a read from memory?
		if (CPU80386_internal_stepreaddirectdw(0,CPU_SEGMENT_ES, REG_ES, (CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI), &CPU[activeCPU].SCASD_cmp1,!CPU[activeCPU].CPU_Address_size)) return 1; //Try to read the data!
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}

	CPUPROT1
	CMP_dw(REG_EAX, CPU[activeCPU].SCASD_cmp1,4);
	if (FLAG_DF)
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			REG_EDI -= 4;
		}
		else
		{
			REG_DI -= 4;
		}
	}
	else
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			REG_EDI += 4;
		}
		else
		{
			REG_DI += 4;
		}
	}
	CPUPROT2
	if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
	{
		if (CPU[activeCPU].repeating) //Are we a repeating instruction?
		{
			if (CPU[activeCPU].newREP) //Include the REP?
			{
				CPU[activeCPU].cycles_OP += 9 + 15 - EU_CYCLES_SUBSTRACT_ACCESSREAD; //Clock cycles including REP!
			}
			else //Repeating instruction itself?
			{
				CPU[activeCPU].cycles_OP += 15 - EU_CYCLES_SUBSTRACT_ACCESSREAD; //Clock cycles excluding REP!
			}
		}
		else //Plain non-repeating instruction?
		{
			CPU[activeCPU].cycles_OP += 15 - EU_CYCLES_SUBSTRACT_ACCESSREAD; //Clock cycles!
		}
	}
	return 0;
}

OPTINLINE byte CPU80386_instructionstepPOPtimeout(word base)
{
	return CPU8086_instructionstepdelayBIU(base,2);//Delay 2 cycles for POPs to start!
}

OPTINLINE byte CPU80386_internal_POPtimeout(word base)
{
	return CPU8086_internal_delayBIU(base,2);//Delay 2 cycles for POPs to start!
}

OPTINLINE byte CPU80386_internal_RET(word popbytes, byte isimm)
{
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,0,1)) return 1;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU80386_internal_POPtimeout(0)) return 1; //POP timeout!
	if (CPU80386_internal_POPdw(2,&CPU[activeCPU].RETD_val)) return 1;
    //Near return
	CPUPROT1
	CPU_JMPabs(CPU[activeCPU].RETD_val,0);
	if (CPU_condflushPIQ(-1)) //We're jumping to another address!
	{
		return 1; //Abort!
	}
	CPUPROT1
	if (STACK_SEGMENT_DESCRIPTOR_B_BIT())
	{
		REG_ESP += popbytes;
	}
	else
	{
		REG_SP += popbytes;
	}
	CPUPROT2
	CPUPROT2
	if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
	{
		if (isimm)
			CPU[activeCPU].cycles_OP += 12 - EU_CYCLES_SUBSTRACT_ACCESSREAD; /* Intrasegment with constant */
		else
			CPU[activeCPU].cycles_OP += 8 - EU_CYCLES_SUBSTRACT_ACCESSREAD; /* Intrasegment */
		CPU[activeCPU].cycles_stallBIU += CPU[activeCPU].cycles_OP; //Stall the BIU completely now!
	}
	return 0;
}

OPTINLINE byte CPU80386_internal_RETF(word popbytes, byte isimm)
{
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(2,0,1)) return 1;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU80386_internal_POPtimeout(0)) return 1; //POP timeout!
	if (CPU80386_internal_POPdw(2,&CPU[activeCPU].RETFD_val)) return 1;
	if (CPU8086_internal_POPw(4,&CPU[activeCPU].RETF_destCS,1)) return 1;
	CPUPROT1
	CPUPROT1
	CPU[activeCPU].destEIP = CPU[activeCPU].RETFD_val; //Load IP!
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
			CPU[activeCPU].cycles_OP += 17 - (EU_CYCLES_SUBSTRACT_ACCESSREAD*2); /* Intersegment with constant */
		else
			CPU[activeCPU].cycles_OP += 18 - (EU_CYCLES_SUBSTRACT_ACCESSREAD*2); /* Intersegment */
		CPU[activeCPU].cycles_stallBIU += CPU[activeCPU].cycles_OP; //Stall the BIU completely now!
	}
	CPUPROT2
	CPUPROT2
	CPUPROT2
	return 0;
}
void external80386RETF(word popbytes)
{
	CPU80386_internal_RETF(popbytes,1); //Return immediate variant!
}

extern byte advancedlog; //Advanced log setting

extern byte MMU_logging; //Are we logging from the MMU?

OPTINLINE byte CPU80386_internal_INTO()
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
	CPU_executionphase_startinterrupt(EXCEPTION_OVERFLOW,0,-2); //Return to opcode!
	return 0; //Finished: OK!
	finishINTO:
	{
		if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
		{
			CPU[activeCPU].cycles_OP += 4; //Timings!
		}
	}
	return 0; //Finished: OK!
}

OPTINLINE byte CPU80386_internal_XLAT()
{
	if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
	{
		debugger_setcommand("XLAT");    //XLAT
	}
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		if (checkMMUaccess(CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),(((CPU[activeCPU].CPU_Address_size?REG_EBX:REG_BX)+REG_AL)&CPU[activeCPU].address_size),1,getCPL(),!CPU[activeCPU].CPU_Address_size,0)) return 1; //Abort on fault!
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	if (CPU[activeCPU].internalinstructionstep==1) //First Execution step?
	{
		//Needs a read from memory?
		if (CPU8086_internal_stepreaddirectb(0,CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),(((CPU[activeCPU].CPU_Address_size?REG_EBX:REG_BX)+REG_AL)&CPU[activeCPU].address_size),&CPU[activeCPU].XLAT_value,!CPU[activeCPU].CPU_Address_size)) return 1; //Try to read the data!
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	CPUPROT1
	REG_AL = CPU[activeCPU].XLAT_value;
	CPUPROT2
	if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
	{
		CPU[activeCPU].cycles_OP += 11 - EU_CYCLES_SUBSTRACT_ACCESSREAD; //XLAT timing!
	}
	return 0;
}

OPTINLINE byte CPU80386_internal_XCHG32(uint_32 *data1, uint_32 *data2, byte flags)
{
	if (unlikely(CPU[activeCPU].internalinstructionstep==0))
	{
		if (data1==NULL)
		{
			CPU_setprefix(0xF0); //Locked!
			if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault!
		}
		CPU[activeCPU].secondparambase = (data1||data2)?0:2; //Second param base
		CPU[activeCPU].writebackbase = ((data2==NULL) && (data1==NULL))?4:2; //Write back param base
		if (data2==NULL)
		{
			CPU_setprefix(0xF0); //Locked!
			if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1,0|0x40)) return 1; //Abort on fault!
		}
		if (data1==NULL)
		{
			if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0xA0)) return 1; //Abort on fault!
		}
		CPU[activeCPU].secondparambase = (data1||data2)?0:2; //Second param base
		CPU[activeCPU].writebackbase = ((data2==NULL) && (data1==NULL))?4:2; //Write back param base
		if (data2==NULL)
		{
			if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 0|0xA0)) return 1; //Abort on fault!
		}
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	CPUPROT1
	if (CPU[activeCPU].internalinstructionstep==1) //First step?
	{
		if (data1==NULL) if (CPU80386_internal_stepreadmodrmdw(0,&CPU[activeCPU].oper1d, CPU[activeCPU].MODRM_src0)) return 1;
		if (data2==NULL) if (CPU80386_internal_stepreadmodrmdw(CPU[activeCPU].secondparambase,&CPU[activeCPU].oper2d, CPU[activeCPU].MODRM_src1)) return 1;
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	if (CPU[activeCPU].internalinstructionstep==2) //Execution step?
	{
		CPU[activeCPU].oper1d = data1?*data1: CPU[activeCPU].oper1d;
		CPU[activeCPU].oper2d = data2?*data2: CPU[activeCPU].oper2d;
		INLINEREGISTER uint_32 temp = CPU[activeCPU].oper1d; //Copy!
		CPU[activeCPU].oper1d = CPU[activeCPU].oper2d; //We're ...
		CPU[activeCPU].oper2d = temp; //Swapping this!
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
		if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
		{
			switch (flags)
			{
			case 0: //Unknown?
				break;
			case 1: //Acc<->Reg?
				CPU[activeCPU].cycles_OP += 3; //Acc<->Reg!
				break;
			case 2: //Mem<->Reg?
				if (MODRM_EA(CPU[activeCPU].params)) //Reg<->Mem?
				{
					CPU[activeCPU].cycles_OP += 17 - (EU_CYCLES_SUBSTRACT_ACCESSRW*2); //SegReg->Mem!
				}
				else //Reg<->Reg?
				{
					CPU[activeCPU].cycles_OP += 4; //SegReg->Mem!
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
		*data1 = CPU[activeCPU].oper1d;
	}
	else //Memory?
	{
		if (CPU80386_internal_stepwritemodrmdw(CPU[activeCPU].writebackbase, CPU[activeCPU].oper1d, CPU[activeCPU].MODRM_src0)) return 1;
	}
	
	if (data2)
	{
		*data2 = CPU[activeCPU].oper2d;
	}
	else
	{
		if (CPU80386_internal_stepwritemodrmdw(CPU[activeCPU].writebackbase+ CPU[activeCPU].secondparambase, CPU[activeCPU].oper2d, CPU[activeCPU].MODRM_src1)) return 1;
	}
	CPUPROT2
	return 0;
}

byte CPU80386_internal_LXS(int segmentregister) //LDS, LES etc.
{

	if (unlikely(CPU[activeCPU].internalinstructionstep==0))
	{
		if (modrm_isregister(CPU[activeCPU].params)) //Invalid?
		{
			CPU_unkOP(); //Invalid: registers aren't allowed!
			return 1;
		}
		CPU[activeCPU].modrm_addoffset = 0; //Add this to the offset to use!
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1,1|0x40)) return 1; //Abort on fault!
		CPU[activeCPU].modrm_addoffset = 4; //Add this to the offset to use!
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1,1|0x40)) return 1; //Abort on fault!
		CPU[activeCPU].modrm_addoffset = 0;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return 1; //Abort on fault for the used segment itself!
		CPU[activeCPU].modrm_addoffset = 0; //Add this to the offset to use!
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0xA0)) return 1; //Abort on fault!
		CPU[activeCPU].modrm_addoffset = 4; //Add this to the offset to use!
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0xA0)) return 1; //Abort on fault!
		CPU[activeCPU].modrm_addoffset = 0;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0xA0)) return 1; //Abort on fault for the used segment itself!
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	CPUPROT1
	if (CPU[activeCPU].internalinstructionstep==1) //First step?
	{
		CPU[activeCPU].modrm_addoffset = 0; //Add this to the offset to use!
		if (CPU80386_internal_stepreadmodrmdw(0,&CPU[activeCPU].LXS_offsetd, CPU[activeCPU].MODRM_src1)) return 1;
		CPU[activeCPU].modrm_addoffset = 4; //Add this to the offset to use!
		if (CPU8086_internal_stepreadmodrmw(2,&CPU[activeCPU].LXS_segment, CPU[activeCPU].MODRM_src1)) return 1;
		CPU[activeCPU].modrm_addoffset = 0; //Reset again!
		++CPU[activeCPU].internalinstructionstep; //Next internal instruction step!
	}
	//Execution phase!
	CPUPROT1
	CPU[activeCPU].destEIP = REG_EIP; //Save EIP for transfers!
	if (segmentWritten(segmentregister, CPU[activeCPU].LXS_segment,0)) return 1; //Load the new segment!
	CPUPROT1
	modrm_write32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, CPU[activeCPU].LXS_offsetd); //Try to load the new register with the offset!
	CPUPROT2
	CPUPROT2
	CPUPROT2
	if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
	{
		if (MODRM_EA(CPU[activeCPU].params)) //Memory?
		{
			CPU[activeCPU].cycles_OP += 16 - (EU_CYCLES_SUBSTRACT_ACCESSREAD*2); /* LXS based on MOV Mem->SS, DS, ES */
		}
		else //Register? Should be illegal?
		{
			CPU[activeCPU].cycles_OP += 2; /* LXS based on MOV Mem->SS, DS, ES */
		}
	}
	return 0;
}

byte CPU80386_CALLF(word segment, uint_32 offset)
{
	CPU[activeCPU].destEIP = offset;
	return segmentWritten(CPU_SEGMENT_CS, segment, 2); /*CS changed, call version!*/
}

/*

NOW THE REAL OPCODES!

*/

void CPU80386_execute_ADD_modrmmodrm32()
{
	modrm_generateInstructionTEXT("ADD", 32, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1,1|0xA0)) return;
	}
	if (CPU80386_instructionstepreadmodrmdw(0, &CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src1)) return;
	CPU80386_internal_ADD32(modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0), CPU[activeCPU].instructionbufferd, 2);
}
void CPU80386_OP05()
{
	INLINEREGISTER uint_32 theimm = CPU[activeCPU].imm32;
	modrm_generateInstructionTEXT("ADD EAX,",0,theimm,PARAM_IMM32_PARAM);
	CPU80386_internal_ADD32(&REG_EAX,theimm,1);
}
void CPU80386_execute_OR_modrmmodrm32()
{
	modrm_generateInstructionTEXT("OR", 32, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1,1|0xA0)) return;
	}
	if (CPU80386_instructionstepreadmodrmdw(0, &CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src1)) return;
	CPU80386_internal_OR32(modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0), CPU[activeCPU].instructionbufferd, 2);
}
void CPU80386_OP0D()
{
	INLINEREGISTER uint_32 theimm = CPU[activeCPU].imm32;
	modrm_generateInstructionTEXT("OR EAX,",0,theimm,PARAM_IMM32_PARAM);
	CPU80386_internal_OR32(&REG_EAX,theimm,1);
}
void CPU80386_execute_ADC_modrmmodrm32()
{
	modrm_generateInstructionTEXT("ADC", 32, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0xA0)) return;
	}
	if (CPU80386_instructionstepreadmodrmdw(0, &CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src1)) return;
	CPU80386_internal_ADC32(modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0), CPU[activeCPU].instructionbufferd, 2);
}
void CPU80386_OP15()
{
	INLINEREGISTER uint_32 theimm = CPU[activeCPU].imm32;
	modrm_generateInstructionTEXT("ADC EAX,",0,theimm,PARAM_IMM32_PARAM);
	CPU80386_internal_ADC32(&REG_EAX,theimm,1);
}
void CPU80386_execute_SBB_modrmmodrm32()
{
	modrm_generateInstructionTEXT("SBB", 32, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1,1|0xA0)) return;
	}
	if (CPU80386_instructionstepreadmodrmdw(0, &CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src1)) return;
	CPU80386_internal_SBB32(modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0), CPU[activeCPU].instructionbufferd, 2);
}
void CPU80386_OP1D()
{
	INLINEREGISTER uint_32 theimm = CPU[activeCPU].imm32;
	modrm_generateInstructionTEXT("SBB EAX,",0,theimm,PARAM_IMM32_PARAM);
	CPU80386_internal_SBB32(&REG_EAX,theimm,1);
}
void CPU80386_execute_AND_modrmmodrm32()
{
	modrm_generateInstructionTEXT("AND", 32, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0xA0)) return;
	}
	if (CPU80386_instructionstepreadmodrmdw(0, &CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src1)) return;
	CPU80386_internal_AND32(modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0), CPU[activeCPU].instructionbufferd, 2);
}
void CPU80386_OP25()
{
	INLINEREGISTER uint_32 theimm = CPU[activeCPU].imm32;
	modrm_generateInstructionTEXT("AND EAX,",0,theimm,PARAM_IMM32_PARAM);
	CPU80386_internal_AND32(&REG_EAX,theimm,1);
}
void CPU80386_execute_SUB_modrmmodrm32()
{
	modrm_generateInstructionTEXT("SUB", 32, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0xA0)) return;
	}
	if (CPU80386_instructionstepreadmodrmdw(0, &CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src1)) return;
	CPU80386_internal_SUB32(modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0), CPU[activeCPU].instructionbufferd, 2);
}
void CPU80386_OP2D()
{
	INLINEREGISTER uint_32 theimm = CPU[activeCPU].imm32;
	modrm_generateInstructionTEXT("SUB EAX,",0,theimm,PARAM_IMM32_PARAM);/*5=AX,imm32*/
	CPU80386_internal_SUB32(&REG_EAX,theimm,1);/*5=AX,imm32*/
}
void CPU80386_execute_XOR_modrmmodrm32()
{
	modrm_generateInstructionTEXT("XOR", 32, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0xA0)) return;
	}
	if (CPU80386_instructionstepreadmodrmdw(0, &CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src1)) return;
	CPU80386_internal_XOR32(modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0), CPU[activeCPU].instructionbufferd, 2);
}
void CPU80386_OP35()
{
	INLINEREGISTER uint_32 theimm = CPU[activeCPU].imm32;
	modrm_generateInstructionTEXT("XOR EAX,",0,theimm,PARAM_IMM32_PARAM);
	CPU80386_internal_XOR32(&REG_EAX,theimm,1);
}
void CPU80386_execute_CMP_modrmmodrm32()
{
	modrm_generateInstructionTEXT("CMP",32,0,PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep==0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,1|0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1,1|0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,1|0xA0)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1,1|0xA0)) return;
	}
	if (CPU80386_instructionstepreadmodrmdw(0,&CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src0)) return;
	if (CPU80386_instructionstepreadmodrmdw(2,&CPU[activeCPU].instructionbufferd2, CPU[activeCPU].MODRM_src1)) return;
	CMP_dw(CPU[activeCPU].instructionbufferd, CPU[activeCPU].instructionbufferd2,2);
}
void CPU80386_OP3D()
{
	INLINEREGISTER uint_32 theimm = CPU[activeCPU].imm32;
	modrm_generateInstructionTEXT("CMP EAX,",0,theimm,PARAM_IMM32_PARAM);/*CMP AX, imm32*/
	CMP_dw(REG_EAX,theimm,1);/*CMP EAX, imm32*/
}
void CPU80386_OP40()
{
	modrm_generateInstructionTEXT("INC EAX",0,0,PARAM_NONE);/*INC EAX*/
	CPU80386_internal_INC32(&REG_EAX);/*INC EAX*/
}
void CPU80386_OP41()
{
	modrm_generateInstructionTEXT("INC ECX",0,0,PARAM_NONE);/*INC ECX*/
	CPU80386_internal_INC32(&REG_ECX);/*INC ECX*/
}
void CPU80386_OP42()
{
	modrm_generateInstructionTEXT("INC EDX",0,0,PARAM_NONE);/*INC EDX*/
	CPU80386_internal_INC32(&REG_EDX);/*INC EDX*/
}
void CPU80386_OP43()
{
	modrm_generateInstructionTEXT("INC EBX",0,0,PARAM_NONE);/*INC EBX*/
	CPU80386_internal_INC32(&REG_EBX);/*INC EBX*/
}
void CPU80386_OP44()
{
	modrm_generateInstructionTEXT("INC ESP",0,0,PARAM_NONE);/*INC ESP*/
	CPU80386_internal_INC32(&REG_ESP);/*INC ESP*/
}
void CPU80386_OP45()
{
	modrm_generateInstructionTEXT("INC EBP",0,0,PARAM_NONE);/*INC EBP*/
	CPU80386_internal_INC32(&REG_EBP);/*INC EBP*/
}
void CPU80386_OP46()
{
	modrm_generateInstructionTEXT("INC ESI",0,0,PARAM_NONE);/*INC ESI*/
	CPU80386_internal_INC32(&REG_ESI);/*INC ESI*/
}
void CPU80386_OP47()
{
	modrm_generateInstructionTEXT("INC EDI",0,0,PARAM_NONE);/*INC EDI*/
	CPU80386_internal_INC32(&REG_EDI);/*INC EDI*/
}
void CPU80386_OP48()
{
	modrm_generateInstructionTEXT("DEC EAX",0,0,PARAM_NONE);/*DEC EAX*/
	CPU80386_internal_DEC32(&REG_EAX);/*DEC EAX*/
}
void CPU80386_OP49()
{
	modrm_generateInstructionTEXT("DEC ECX",0,0,PARAM_NONE);/*DEC ECX*/
	CPU80386_internal_DEC32(&REG_ECX);/*DEC ECX*/
}
void CPU80386_OP4A()
{
	modrm_generateInstructionTEXT("DEC EDX",0,0,PARAM_NONE);/*DEC EDX*/
	CPU80386_internal_DEC32(&REG_EDX);/*DEC EDX*/
}
void CPU80386_OP4B()
{
	modrm_generateInstructionTEXT("DEC EBX",0,0,PARAM_NONE);/*DEC EBX*/
	CPU80386_internal_DEC32(&REG_EBX);/*DEC EBX*/
}
void CPU80386_OP4C()
{
	modrm_generateInstructionTEXT("DEC ESP",0,0,PARAM_NONE);/*DEC ESP*/
	CPU80386_internal_DEC32(&REG_ESP);/*DEC ESP*/
}
void CPU80386_OP4D()
{
	modrm_generateInstructionTEXT("DEC EBP",0,0,PARAM_NONE);/*DEC EBP*/
	CPU80386_internal_DEC32(&REG_EBP);/*DEC EBP*/
}
void CPU80386_OP4E()
{
	modrm_generateInstructionTEXT("DEC ESI",0,0,PARAM_NONE);/*DEC ESI*/
	CPU80386_internal_DEC32(&REG_ESI);/*DEC ESI*/
}
void CPU80386_OP4F()
{
	modrm_generateInstructionTEXT("DEC EDI",0,0,PARAM_NONE);/*DEC EDI*/
	CPU80386_internal_DEC32(&REG_EDI);/*DEC EDI*/
}
void CPU80386_OP50()
{
	modrm_generateInstructionTEXT("PUSH EAX",0,0,PARAM_NONE);/*PUSH EAX*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,1,1)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU80386_PUSHdw(0,&REG_EAX)) return; /*PUSH AX*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 11-EU_CYCLES_SUBSTRACT_ACCESSWRITE; /*Push Reg!*/
	}
}
void CPU80386_OP51()
{
	modrm_generateInstructionTEXT("PUSH ECX",0,0,PARAM_NONE);/*PUSH ECX*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,1,1)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU80386_PUSHdw(0,&REG_ECX)) return; /*PUSH CX*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 11-EU_CYCLES_SUBSTRACT_ACCESSWRITE; /*Push Reg!*/
	}
}
void CPU80386_OP52()
{
	modrm_generateInstructionTEXT("PUSH EDX",0,0,PARAM_NONE);/*PUSH EDX*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,1,1)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU80386_PUSHdw(0,&REG_EDX)) return; /*PUSH DX*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 11-EU_CYCLES_SUBSTRACT_ACCESSWRITE; /*Push Reg!*/
	}
}
void CPU80386_OP53()
{
	modrm_generateInstructionTEXT("PUSH EBX",0,0,PARAM_NONE);/*PUSH EBX*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,1,1)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU80386_PUSHdw(0,&REG_EBX)) return; /*PUSH BX*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 11-EU_CYCLES_SUBSTRACT_ACCESSWRITE; /*Push Reg!*/
	}
}
void CPU80386_OP54()
{
	modrm_generateInstructionTEXT("PUSH ESP",0,0,PARAM_NONE);/*PUSH ESP*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,1,1)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU80386_PUSHdw(0,&REG_ESP)) return; /*PUSH SP*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 11-EU_CYCLES_SUBSTRACT_ACCESSWRITE; /*Push Reg!*/
	}
}
void CPU80386_OP55()
{
	modrm_generateInstructionTEXT("PUSH EBP",0,0,PARAM_NONE);/*PUSH EBP*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,1,1)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU80386_PUSHdw(0,&REG_EBP)) return; /*PUSH BP*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 11-EU_CYCLES_SUBSTRACT_ACCESSWRITE; /*Push Reg!*/
	}
}
void CPU80386_OP56()
{
	modrm_generateInstructionTEXT("PUSH ESI",0,0,PARAM_NONE);/*PUSH ESI*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,1,1)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU80386_PUSHdw(0,&REG_ESI)) return; /*PUSH SI*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 11-EU_CYCLES_SUBSTRACT_ACCESSWRITE; /*Push Reg!*/
	}
}
void CPU80386_OP57()
{
	modrm_generateInstructionTEXT("PUSH EDI",0,0,PARAM_NONE);/*PUSH EDI*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,1,1)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU80386_PUSHdw(0,&REG_EDI)) return; /*PUSH DI*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 11-EU_CYCLES_SUBSTRACT_ACCESSWRITE; /*Push Reg!*/
	}
}
void CPU80386_OP58()
{
	modrm_generateInstructionTEXT("POP EAX",0,0,PARAM_NONE);/*POP EAX*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,0,1)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU80386_instructionstepPOPtimeout(0)) return; /*POP timeout*/
	if (CPU80386_POPdw(2,&REG_EAX)) return; /*POP AX*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSREAD; /*Pop Reg!*/
	}
}
void CPU80386_OP59()
{
	modrm_generateInstructionTEXT("POP ECX",0,0,PARAM_NONE);/*POP ECX*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,0,1)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU80386_instructionstepPOPtimeout(0)) return; /*POP timeout*/
	if (CPU80386_POPdw(2,&REG_ECX)) return; /*POP CX*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSREAD; /*Pop Reg!*/
	}
}
void CPU80386_OP5A()
{
	modrm_generateInstructionTEXT("POP EDX",0,0,PARAM_NONE);/*POP EDX*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,0,1)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU80386_instructionstepPOPtimeout(0)) return; /*POP timeout*/
	if (CPU80386_POPdw(2,&REG_EDX)) return; /*POP DX*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSREAD; /*Pop Reg!*/
	}
}
void CPU80386_OP5B()
{
	modrm_generateInstructionTEXT("POP EBX",0,0,PARAM_NONE);/*POP EBX*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,0,1)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU80386_instructionstepPOPtimeout(0)) return; /*POP timeout*/
	if (CPU80386_POPdw(2,&REG_EBX)) return; /*POP BX*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSREAD; /*Pop Reg!*/
	}
}
void CPU80386_OP5C()
{
	modrm_generateInstructionTEXT("POP ESP",0,0,PARAM_NONE);/*POP ESP*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,0,1)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU80386_instructionstepPOPtimeout(0)) return; /*POP timeout*/
	if (CPU80386_POPESP(2)) return; /*POP SP*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSREAD; /*Pop Reg!*/
	}
}
void CPU80386_OP5D()
{
	modrm_generateInstructionTEXT("POP EBP",0,0,PARAM_NONE);/*POP EBP*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,0,1)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU80386_instructionstepPOPtimeout(0)) return; /*POP timeout*/
	if (CPU80386_POPdw(2,&REG_EBP)) return; /*POP BP*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSREAD; /*Pop Reg!*/
	}
}
void CPU80386_OP5E()
{
	modrm_generateInstructionTEXT("POP ESI",0,0,PARAM_NONE);/*POP ESI*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,0,1)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU80386_instructionstepPOPtimeout(0)) return; /*POP timeout*/
	if (CPU80386_POPdw(2,&REG_ESI)) return;/*POP SI*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSREAD; /*Pop Reg!*/
	}
}
void CPU80386_OP5F()
{
	modrm_generateInstructionTEXT("POP EDI",0,0,PARAM_NONE);/*POP EDI*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,0,1)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU80386_instructionstepPOPtimeout(0)) return; /*POP timeout*/
	if (CPU80386_POPdw(2,&REG_EDI)) return;/*POP DI*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSREAD; /*Pop Reg!*/
	}
}
void CPU80386_OP85()
{
	modrm_generateInstructionTEXT("TEST",32,0,PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep==0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,1|0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1,1|0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,1|0xA0)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1,1|0xA0)) return;
	}
	if (CPU80386_instructionstepreadmodrmdw(0,&CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src0)) return;
	if (CPU80386_instructionstepreadmodrmdw(2,&CPU[activeCPU].instructionbufferd2, CPU[activeCPU].MODRM_src1)) return;
	CPU80386_internal_TEST32(CPU[activeCPU].instructionbufferd, CPU[activeCPU].instructionbufferd2,2);
}
void CPU80386_OP87()
{
	modrm_generateInstructionTEXT("XCHG",32,0,PARAM_MODRM_01);
	CPU80386_internal_XCHG32(modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0),modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1,0),2); /*XCHG reg32,r/m32*/
}
void CPU80386_execute_MOV_modrmmodrm32()
{
	modrm_generateInstructionTEXT("MOV", 32, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0xA0)) return;
	}
	if (CPU80386_instructionstepreadmodrmdw(0, &CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src1)) return;
	CPU80386_internal_MOV32(modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0), CPU[activeCPU].instructionbufferd, 2);
}
void CPU80386_OP8C()
{
	modrm_generateInstructionTEXT("MOV", 32, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0xA0)) return;
	}
	if (CPU80386_instructionstepreadmodrmdw(0, &CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src1)) return;
	if (modrm_isregister(CPU[activeCPU].params))
	{
		if (CPU80386_internal_MOV32(modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0), CPU[activeCPU].instructionbufferd, 8)) return;
	}
	else
	{
		if (CPU80386_internal_MOV16(modrm_addr16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0), CPU[activeCPU].instructionbufferd, 8)) return;
	}
	if ((CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0].reg16 == &REG_SS) && (CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0].isreg == 1) && (CPU[activeCPU].previousAllowInterrupts))
	{
		CPU[activeCPU].allowInterrupts = 0; /* Inhabit all interrupts up to the next instruction */
	}
}
void CPU80386_OP8D()
{
	modrm_debugger32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, CPU[activeCPU].MODRM_src1);
	debugger_setcommand("LEA %s,%s", CPU[activeCPU].modrm_param1,getLEAtext32(&CPU[activeCPU].params));
	if ((EMULATED_CPU >= CPU_NECV30) && !modrm_ismemory(CPU[activeCPU].params))
	{
		CPU_unkOP();
		return;
	}
	if (CPU80386_internal_MOV32(modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0),getLEA32(&CPU[activeCPU].params),0)) return;
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 2; /* Load effective address */
	}
}
void CPU80386_OP90() /*NOP*/
{
	modrm_generateInstructionTEXT("NOP",0,0,PARAM_NONE);/*NOP (XCHG EAX,EAX)*/
	if (CPU80386_internal_XCHG32(&REG_EAX,&REG_EAX,1)) return; /* NOP */
}
void CPU80386_OP91()
{
	modrm_generateInstructionTEXT("XCHG ECX,EAX",0,0,PARAM_NONE);/*XCHG ECX,EAX*/
	CPU80386_internal_XCHG32(&REG_ECX,&REG_EAX,1); /*XCHG CX,AX*/
}
void CPU80386_OP92()
{
	modrm_generateInstructionTEXT("XCHG EDX,EAX",0,0,PARAM_NONE);/*XCHG EDX,EAX*/
	CPU80386_internal_XCHG32(&REG_EDX,&REG_EAX,1); /*XCHG DX,AX*/
}
void CPU80386_OP93()
{
	modrm_generateInstructionTEXT("XCHG EBX,EAX",0,0,PARAM_NONE);/*XCHG EBX,EAX*/
	CPU80386_internal_XCHG32(&REG_EBX,&REG_EAX,1); /*XCHG BX,AX*/
}
void CPU80386_OP94()
{
	modrm_generateInstructionTEXT("XCHG ESP,EAX",0,0,PARAM_NONE);/*XCHG ESP,EAX*/
	CPU80386_internal_XCHG32(&REG_ESP,&REG_EAX,1); /*XCHG SP,AX*/
}
void CPU80386_OP95()
{
	modrm_generateInstructionTEXT("XCHG EBP,EAX",0,0,PARAM_NONE);/*XCHG EBP,EAX*/
	CPU80386_internal_XCHG32(&REG_EBP,&REG_EAX,1); /*XCHG BP,AX*/
}
void CPU80386_OP96()
{
	modrm_generateInstructionTEXT("XCHG ESI,EAX",0,0,PARAM_NONE);/*XCHG ESI,EAX*/
	CPU80386_internal_XCHG32(&REG_ESI,&REG_EAX,1); /*XCHG SI,AX*/
}
void CPU80386_OP97()
{
	modrm_generateInstructionTEXT("XCHG EDI,EAX",0,0,PARAM_NONE);/*XCHG EDI,EAX*/
	CPU80386_internal_XCHG32(&REG_EDI,&REG_EAX,1); /*XCHG DI,AX*/
}
void CPU80386_OP98()
{
	modrm_generateInstructionTEXT("CWDE",0,0,PARAM_NONE);/*CWDE : sign extend AX to EAX*/
	CPU80386_internal_CWDE();/*CWDE : sign extend AX to EAX (80386+)*/
}
void CPU80386_OP99()
{
	modrm_generateInstructionTEXT("CDQ",0,0,PARAM_NONE);/*CDQ : sign extend EAX to EDX::EAX*/
	CPU80386_internal_CDQ();/*CWQ : sign extend EAX to EDX::EAX (80386+)*/
}
void CPU80386_OP9A()
{
	/*CALL Ap*/
	INLINEREGISTER uint_64 segmentoffset = CPU[activeCPU].imm64;
	debugger_setcommand("CALLD %04x:%08x", (segmentoffset>>32), (segmentoffset&CPU_EIPmask(0)));
	if (CPU80386_CALLF((segmentoffset>>32)&0xFFFF,segmentoffset&CPU_EIPmask(0))) return;
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 28; /* Intersegment direct */
	}
}

void CPU80386_OP9C()
{
	modrm_generateInstructionTEXT("PUSHFD", 0, 0, PARAM_NONE);/*PUSHF*/
	if (unlikely((getcpumode() == CPU_MODE_8086) && (FLAG_PL != 3)))
	{
		THROWDESCGP(0, 0, 0);
		return;
	} /*#GP fault!*/
	if (unlikely(CPU[activeCPU].stackchecked == 0))
	{
		if (checkStackAccess(1, 1, 1)) return;
		++CPU[activeCPU].stackchecked;
	}
	uint_32 flags = REG_EFLAGS;
	if (FLAG_V8) flags &= ~0x20000; /* VM is never pushed during Virtual 8086 mode! */
	if (FLAG_RF) flags &= ~0x10000; /* Resume flag is never pushed! */
	if (CPU80386_PUSHdw(0, &flags)) return;
	if (CPU_apply286cycles() == 0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 10 - EU_CYCLES_SUBSTRACT_ACCESSWRITE; /*PUSHF timing!*/
	}
}

void CPU80386_OP9D_16()
{
	modrm_generateInstructionTEXT("POPF", 0, 0, PARAM_NONE);/*POPF*/
	if (unlikely((getcpumode()==CPU_MODE_8086) && (FLAG_PL!=3)))
	{
		THROWDESCGP(0,0,0);
		return;
	} //#GP fault!
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,0,0)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU80386_instructionstepPOPtimeout(0)) return; /*POP timeout*/
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
		CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSREAD; /*POPF timing!*/
	}
	CPU[activeCPU].allowTF = 0; /*Disallow TF to be triggered after the instruction!*/
	/*CPU[activeCPU].unaffectedRF = 1;*/ //Default: affected!
}

void CPU80386_OP9D_32()
{
	modrm_generateInstructionTEXT("POPFD", 0, 0, PARAM_NONE);/*POPF*/
	if (unlikely((getcpumode()==CPU_MODE_8086) && (FLAG_PL!=3)))
	{
		THROWDESCGP(0,0,0);
		return;
	}//#GP fault!
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,0,1)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU80386_instructionstepPOPtimeout(0)) return; /*POP timeout*/
	if (CPU80386_POPdw(2,&CPU[activeCPU].tempflagsd)) return;
	if (disallowPOPFI())
	{
		CPU[activeCPU].tempflagsd &= ~0x200;
		CPU[activeCPU].tempflagsd |= REG_FLAGS&0x200; /* Ignore any changes to the Interrupt flag! */
	}
	if (getCPL())
	{
		CPU[activeCPU].tempflagsd &= ~0x3000;
		CPU[activeCPU].tempflagsd |= REG_FLAGS&0x3000; /* Ignore any changes to the IOPL when not at CPL 0! */
	}
	if (getcpumode()==CPU_MODE_8086) //Virtual 8086 mode?
	{
		if (FLAG_PL==3) //IOPL 3?
		{
			CPU[activeCPU].tempflagsd = ((CPU[activeCPU].tempflagsd&~0x1B0000)|(REG_EFLAGS&0x1B0000)); /* Ignore any changes to the VM, RF, IOPL, VIP and VIF ! */
		} //Otherwise, fault is raised!
	}
	else //Protected/real mode?
	{
		if (getCPL())
		{
			CPU[activeCPU].tempflagsd = ((CPU[activeCPU].tempflagsd&~0x1A0000)|(REG_EFLAGS&0x20000)); /* Ignore any changes to the IOPL, VM ! VIP/VIF are cleared. */			
		}
		else
		{
			CPU[activeCPU].tempflagsd = ((CPU[activeCPU].tempflagsd&~0x1A0000)|(REG_EFLAGS&0x20000)); /* VIP/VIF are cleared. Ignore any changes to VM! */			
		}
	}
	REG_EFLAGS = CPU[activeCPU].tempflagsd;
	updateCPUmode(); /*POPF*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSREAD; /*POPF timing!*/
	}
	CPU[activeCPU].allowTF = 0; /*Disallow TF to be triggered after the instruction!*/
	/*CPU[activeCPU].unaffectedRF = 1;*/ //Default: affected!
}

//Different addressing modes affect us! Combine operand size and address size into new versions of the instructions, where needed!
//16/32 depending on address size!
//A0 32-bits address version with 8-bit reg
OPTINLINE void CPU80386_OPA0_8exec_addr32()
{
	debugger_setcommand("MOV AL,byte %s:[%08X]",CPU_textsegment(CPU_SEGMENT_DS), CPU[activeCPU].immaddr32);/*MOV AL,[imm32]*/
	if (unlikely(CPU[activeCPU].modrmstep==0))
	{
		if (checkMMUaccess(CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),(CPU[activeCPU].immaddr32&CPU[activeCPU].address_size),1,getCPL(),!CPU[activeCPU].CPU_Address_size,0)) return;
	}
	if (CPU8086_instructionstepreaddirectb(0,CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),(CPU[activeCPU].immaddr32&CPU[activeCPU].address_size),&CPU[activeCPU].instructionbufferb,0)) return;
	CPU80386_internal_MOV8(&REG_AL, CPU[activeCPU].instructionbufferb,1);/*MOV AL,[imm32]*/
}

//A1 16/32-bits address version with 16/32-bit reg
OPTINLINE void CPU80386_OPA1_16exec_addr32()
{
	debugger_setcommand("MOV AX,word %s:[%08X]",CPU_textsegment(CPU_SEGMENT_DS), CPU[activeCPU].immaddr32);/*MOV AX,[imm32]*/
	if (unlikely(CPU[activeCPU].modrmstep==0))
	{
		if (checkMMUaccess16(CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),(CPU[activeCPU].immaddr32&CPU[activeCPU].address_size),1|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0|0x8)) return;
		if (checkMMUaccess16(CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].immaddr32&CPU[activeCPU].address_size), 1|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0 | 0x8)) return;
	}
	if (CPU8086_instructionstepreaddirectw(0,CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),(CPU[activeCPU].immaddr32&CPU[activeCPU].address_size),&CPU[activeCPU].instructionbufferw,0)) return;
	CPU80386_internal_MOV16(&REG_AX, CPU[activeCPU].instructionbufferw,1);/*MOV AX,[imm32]*/
}
OPTINLINE void CPU80386_OPA1_32exec_addr16()
{
	debugger_setcommand("MOV EAX,dword %s:[%04X]",CPU_textsegment(CPU_SEGMENT_DS), CPU[activeCPU].immaddr32);/*MOV AX,[imm32]*/
	if (unlikely(CPU[activeCPU].modrmstep==0))
	{
		if (checkMMUaccess32(CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),(CPU[activeCPU].immaddr32&CPU[activeCPU].address_size),1|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0|0x10)) return;
		if (checkMMUaccess32(CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].immaddr32&CPU[activeCPU].address_size), 1|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0 | 0x10)) return;
	}
	if (CPU80386_instructionstepreaddirectdw(0,CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),(CPU[activeCPU].immaddr32&CPU[activeCPU].address_size),&CPU[activeCPU].instructionbufferd,1)) return;
	CPU80386_internal_MOV32(&REG_EAX, CPU[activeCPU].instructionbufferd,1);/*MOV EAX,[imm16]*/
}
OPTINLINE void CPU80386_OPA1_32exec_addr32()
{
	debugger_setcommand("MOV EAX,dword %s:[%08X]",CPU_textsegment(CPU_SEGMENT_DS), CPU[activeCPU].immaddr32);/*MOV AX,[imm32]*/
	if (unlikely(CPU[activeCPU].modrmstep==0))
	{
		if (checkMMUaccess32(CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),(CPU[activeCPU].immaddr32&CPU[activeCPU].address_size),1|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0|0x10)) return;
		if (checkMMUaccess32(CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].immaddr32&CPU[activeCPU].address_size), 1|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0 | 0x10)) return;
	}
	if (CPU80386_instructionstepreaddirectdw(0,CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),(CPU[activeCPU].immaddr32&CPU[activeCPU].address_size),&CPU[activeCPU].instructionbufferd,0)) return;
	CPU80386_internal_MOV32(&REG_EAX, CPU[activeCPU].instructionbufferd,1);/*MOV EAX,[imm32]*/ }

//A2 32-bits address version with 8-bit reg
OPTINLINE void CPU80386_OPA2_8exec_addr32()
{
	debugger_setcommand("MOV byte %s:[%08X],AL",CPU_textsegment(CPU_SEGMENT_DS),(CPU[activeCPU].immaddr32&CPU[activeCPU].address_size));/*MOV [imm32],AL*/
	CPU[activeCPU].custommem = 1;
	CPU[activeCPU].customoffset = (CPU[activeCPU].immaddr32&CPU[activeCPU].address_size);
	CPU80386_internal_MOV8(NULL,REG_AL,1);/*MOV [imm32],AL*/
	CPU[activeCPU].custommem = 0;
}

//A3 16/32-bits address version with 16/32-bit reg
OPTINLINE void CPU80386_OPA3_16exec_addr32()
{
	debugger_setcommand("MOV word %s:[%08X],AX",CPU_textsegment(CPU_SEGMENT_DS),(CPU[activeCPU].immaddr32&CPU[activeCPU].address_size));/*MOV [imm32], AX*/
	CPU[activeCPU].custommem = 1;
	CPU[activeCPU].customoffset = (CPU[activeCPU].immaddr32&CPU[activeCPU].address_size);
	CPU80386_internal_MOV16(NULL,REG_AX,1);/*MOV [imm32], AX*/
	CPU[activeCPU].custommem = 0;
}
OPTINLINE void CPU80386_OPA3_32exec_addr16()
{
	debugger_setcommand("MOV dword %s:[%04X],EAX",CPU_textsegment(CPU_SEGMENT_DS),(CPU[activeCPU].immaddr32&CPU[activeCPU].address_size));/*MOV [imm32], AX*/
	CPU[activeCPU].custommem = 1;
	CPU[activeCPU].customoffset = (CPU[activeCPU].immaddr32&CPU[activeCPU].address_size);
	CPU80386_internal_MOV32(NULL,REG_EAX,1);/*MOV [imm16], EAX*/
	CPU[activeCPU].custommem = 0;
}
OPTINLINE void CPU80386_OPA3_32exec_addr32()
{
	debugger_setcommand("MOV dword %s:[%08X],EAX",CPU_textsegment(CPU_SEGMENT_DS),(CPU[activeCPU].immaddr32&CPU[activeCPU].address_size));/*MOV [imm32], AX*/
	CPU[activeCPU].custommem = 1;
	CPU[activeCPU].customoffset = (CPU[activeCPU].immaddr32&CPU[activeCPU].address_size);
	CPU80386_internal_MOV32(NULL,REG_EAX,1);/*MOV [imm32], EAX*/
	CPU[activeCPU].custommem = 0;
}

//Our two calling methods for handling the jumptable!
//16-bits versions having a new 32-bit address size override!
void CPU80386_OPA0_16()
{
	if (CPU[activeCPU].CPU_Address_size)
		CPU80386_OPA0_8exec_addr32();
	else
		CPU8086_OPA0();
}
void CPU80386_OPA1_16()
{
	if (CPU[activeCPU].CPU_Address_size)
		CPU80386_OPA1_16exec_addr32();
	else
		CPU8086_OPA1();
}
void CPU80386_OPA2_16()
{
	if (CPU[activeCPU].CPU_Address_size)
		CPU80386_OPA2_8exec_addr32();
	else
		CPU8086_OPA2();
}
void CPU80386_OPA3_16()
{
	if (CPU[activeCPU].CPU_Address_size)
		CPU80386_OPA3_16exec_addr32();
	else
		CPU8086_OPA3();
}
//32-bits versions having a new 32-bit address size override and operand size override, except 8-bit instructions!
void CPU80386_OPA1_32()
{
	if (CPU[activeCPU].CPU_Address_size)
		CPU80386_OPA1_32exec_addr32();
	else
		CPU80386_OPA1_32exec_addr16();
}
void CPU80386_OPA3_32()
{
	if (CPU[activeCPU].CPU_Address_size)
		CPU80386_OPA3_32exec_addr32();
	else
		CPU80386_OPA3_32exec_addr16();
}

//Normal instruction again!
void CPU80386_OPA5()
{
	modrm_generateInstructionTEXT("MOVSD",0,0,PARAM_NONE);/*MOVSD*/
	CPU80386_internal_MOVSD();/*MOVSD*/
}
void CPU80386_OPA7()
{
	debugger_setcommand(CPU[activeCPU].CPU_Address_size?"CMPSD %s:[ESI],ES:[EDI]":"CMPSD %s:[SI],ES:[DI]",CPU_textsegment(CPU_SEGMENT_DS));/*CMPSD*/
	CPU80386_internal_CMPSD();/*CMPSD*/
}
void CPU80386_OPA9()
{
	INLINEREGISTER uint_32 theimm = CPU[activeCPU].imm32;
	modrm_generateInstructionTEXT("TEST EAX,",0,theimm,PARAM_IMM32_PARAM);/*TEST EAX,imm32*/
	CPU80386_internal_TEST32(REG_EAX,theimm,1);/*TEST EAX,imm32*/
}
void CPU80386_OPAB()
{
	modrm_generateInstructionTEXT("STOSD",0,0,PARAM_NONE);/*STOSW*/
	CPU80386_internal_STOSD();/*STOSW*/
}
void CPU80386_OPAD()
{
	modrm_generateInstructionTEXT("LODSD",0,0,PARAM_NONE);/*LODSW*/
	CPU80386_internal_LODSD();/*LODSW*/
}
void CPU80386_OPAF()
{
	modrm_generateInstructionTEXT("SCASD",0,0,PARAM_NONE);/*SCASW*/
	CPU80386_internal_SCASD();/*SCASW*/
}
void CPU80386_OPB8()
{
	INLINEREGISTER uint_32 theimm = CPU[activeCPU].imm32;
	modrm_generateInstructionTEXT("MOV EAX,",0,theimm,PARAM_IMM32_PARAM);/*MOV AX,imm32*/
	CPU80386_internal_MOV32(&REG_EAX,theimm,4);/*MOV AX,imm32*/
}
void CPU80386_OPB9()
{
	INLINEREGISTER uint_32 theimm = CPU[activeCPU].imm32;
	modrm_generateInstructionTEXT("MOV ECX,",0,theimm,PARAM_IMM32_PARAM);/*MOV CX,imm32*/
	CPU80386_internal_MOV32(&REG_ECX,theimm,4);/*MOV CX,imm32*/
}
void CPU80386_OPBA()
{
	INLINEREGISTER uint_32 theimm = CPU[activeCPU].imm32;
	modrm_generateInstructionTEXT("MOV EDX,",0,theimm,PARAM_IMM32_PARAM);/*MOV DX,imm32*/
	CPU80386_internal_MOV32(&REG_EDX,theimm,4);/*MOV DX,imm32*/
}
void CPU80386_OPBB()
{
	INLINEREGISTER uint_32 theimm = CPU[activeCPU].imm32;
	modrm_generateInstructionTEXT("MOV EBX,",0,theimm,PARAM_IMM32_PARAM);/*MOV BX,imm32*/
	CPU80386_internal_MOV32(&REG_EBX,theimm,4);/*MOV BX,imm32*/
}
void CPU80386_OPBC()
{
	INLINEREGISTER uint_32 theimm = CPU[activeCPU].imm32;
	modrm_generateInstructionTEXT("MOV ESP,",0,theimm,PARAM_IMM32_PARAM);/*MOV SP,imm32*/
	CPU80386_internal_MOV32(&REG_ESP,theimm,4);/*MOV SP,imm32*/
}
void CPU80386_OPBD()
{
	INLINEREGISTER uint_32 theimm = CPU[activeCPU].imm32;
	modrm_generateInstructionTEXT("MOV EBP,",0,theimm,PARAM_IMM32_PARAM);/*MOV BP,imm32*/
	CPU80386_internal_MOV32(&REG_EBP,theimm,4);/*MOV BP,imm32*/
}
void CPU80386_OPBE()
{
	INLINEREGISTER uint_32 theimm = CPU[activeCPU].imm32;
	modrm_generateInstructionTEXT("MOV ESI,",0,theimm,PARAM_IMM32_PARAM);/*MOV SI,imm32*/
	CPU80386_internal_MOV32(&REG_ESI,theimm,4);/*MOV SI,imm32*/
}
void CPU80386_OPBF()
{
	INLINEREGISTER uint_32 theimm = CPU[activeCPU].imm32;
	modrm_generateInstructionTEXT("MOV EDI,",0,theimm,PARAM_IMM32_PARAM);/*MOV DI,imm32*/
	CPU80386_internal_MOV32(&REG_EDI,theimm,4);/*MOV DI,imm32*/
}
void CPU80386_OPC2()
{
	INLINEREGISTER word popbytes = CPU[activeCPU].immw;/*RET imm32 (Near return to calling proc and POP imm32 bytes)*/
	modrm_generateInstructionTEXT("RETD",0,popbytes,PARAM_IMM8); /*RET imm16 (Near return to calling proc and POP imm32 bytes)*/
	CPU80386_internal_RET(popbytes,1);
}
void CPU80386_OPC3()
{
	modrm_generateInstructionTEXT("RETD",0,0,PARAM_NONE);/*RET (Near return to calling proc)*/ /*RET (Near return to calling proc)*/
	CPU80386_internal_RET(0,0);
}
void CPU80386_OPC4() /*LES modr/m*/
{
	modrm_generateInstructionTEXT("LES",32,0,PARAM_MODRM_01);
	CPU80386_internal_LXS(CPU_SEGMENT_ES); /*Load new ES!*/
}
void CPU80386_OPC5() /*LDS modr/m*/
{
	modrm_generateInstructionTEXT("LDS",32,0,PARAM_MODRM_01);
	CPU80386_internal_LXS(CPU_SEGMENT_DS); /*Load new DS!*/
}
void CPU80386_OPC7()
{
	if (MODRM_REG(CPU[activeCPU].params.modrm))
	{
		CPU_unkOP();
		return;
	}
	uint_32 val = CPU[activeCPU].imm32;
	modrm_debugger32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, CPU[activeCPU].MODRM_src1);
	debugger_setcommand("MOV %s,%08x", CPU[activeCPU].modrm_param1, val);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0xA0)) return;
	}
	if (CPU80386_instructionstepwritemodrmdw(0, val, CPU[activeCPU].MODRM_src0)) return;
	if (CPU_apply286cycles() == 0) /* No 80286+ cycles instead? */
	{
		if (MODRM_EA(CPU[activeCPU].params))
		{
			CPU[activeCPU].cycles_OP += 10 - EU_CYCLES_SUBSTRACT_ACCESSWRITE; /* Imm->Mem */
		}
		else
			CPU[activeCPU].cycles_OP += 4; /* Imm->Reg */
	}
}
void CPU80386_OPCA()
{
	INLINEREGISTER word popbytes = CPU[activeCPU].immw;/*RETF imm32 (Far return to calling proc and pop imm32 bytes)*/
	modrm_generateInstructionTEXT("RETFD",0,popbytes,PARAM_IMM32); /*RETF imm32 (Far return to calling proc and pop imm16 bytes)*/
	CPU80386_internal_RETF(popbytes,1);
}
void CPU80386_OPCB()
{
	modrm_generateInstructionTEXT("RETFD",0,0,PARAM_NONE); /*RETF (Far return to calling proc)*/
	CPU80386_internal_RETF(0,0);
}
void CPU80386_OPCC()
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
void CPU80386_OPCD()
{
	INLINEREGISTER byte theimm = CPU[activeCPU].immb;
	INTdebugger80386();
	modrm_generateInstructionTEXT("INT",0,theimm,PARAM_IMM8);/*INT imm8*/
	if (isV86() && (FLAG_PL!=3))
	{
		THROWDESCGP(0,0,0);
		return;
	}
	CPU_executionphase_startinterrupt(theimm,0,-2);/*INT imm8*/
}
void CPU80386_OPCE()
{
	modrm_generateInstructionTEXT("INTO",0,0,PARAM_NONE);/*INTO*/
	CPU80386_internal_INTO();/*INTO*/
}
void CPU80386_OPCF()
{
	modrm_generateInstructionTEXT("IRETD",0,0,PARAM_NONE);/*IRET*/
	CPU80386_IRET();/*IRET : also restore interrupt flag!*/
}
void CPU80386_OPD6()
{
	debugger_setcommand("SALC");
	REG_AL=FLAG_CF?0xFF:0x00;
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 2;
	}
} //Special case on the 80386: SALC!
void CPU80386_OPD7()
{
	CPU80386_internal_XLAT();
} //We depend on the address size instead!
void CPU80386_OPE0()
{
	if (!CPU[activeCPU].CPU_Address_size)
	{
		CPU8086_OPE0();
		return; /* Use CX instead! */
	}
	INLINEREGISTER sbyte rel8;
	rel8 = imm8();
	modrm_generateInstructionTEXT("LOOPNZ",0, ((REG_EIP+rel8)&CPU_EIPmask(0)),CPU_EIPSize(0));
	if ((--REG_ECX) && (!FLAG_ZF))
	{
		CPU_JMPrel((int_32)rel8,0);
		CPU_flushPIQ(-1); /*We're jumping to another address*/
		CPU[activeCPU].didJump = 1;
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 19;
		}
		/* Branch taken */
	}
	else
	{
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 5;
		}
		/* Branch not taken */
	}
}
void CPU80386_OPE1()
{
	if (!CPU[activeCPU].CPU_Address_size)
	{
		CPU8086_OPE1();
		return; /* Use CX instead! */
	}
	INLINEREGISTER sbyte rel8;
	rel8 = imm8();
	modrm_generateInstructionTEXT("LOOPZ",0, ((REG_EIP+rel8)&CPU_EIPmask(0)),CPU_EIPSize(0));
	if ((--REG_ECX) && (FLAG_ZF))
	{
		CPU_JMPrel((int_32)rel8,0);
		CPU_flushPIQ(-1); /*We're jumping to another address*/
		CPU[activeCPU].didJump = 1;
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 18;
		}
		/* Branch taken */
	}
	else
	{
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 6;
		}
		/* Branch not taken */
	}
}
void CPU80386_OPE2()
{
	if (!CPU[activeCPU].CPU_Address_size)
	{
		CPU8086_OPE2();
		return; /* Use CX instead! */
	}
	INLINEREGISTER sbyte rel8;
	rel8 = imm8();
	modrm_generateInstructionTEXT("LOOP", 0,((REG_EIP+rel8)&CPU_EIPmask(0)),CPU_EIPSize(0));
	if (--REG_ECX)
	{
		CPU_JMPrel((int_32)rel8,0);
		CPU_flushPIQ(-1); /*We're jumping to another address*/
		CPU[activeCPU].didJump = 1;
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 17;
		}
		/* Branch taken */
	}
	else
	{
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 5;
		}
		/* Branch not taken */
	}
}
void CPU80386_OPE3()
{
	if (!CPU[activeCPU].CPU_Address_size)
	{
		CPU8086_OPE3();
		return; /* Use CX instead! */
	}
	INLINEREGISTER sbyte rel8;
	rel8 = imm8();
	modrm_generateInstructionTEXT("JECXZ",0,((REG_EIP+rel8)&CPU_EIPmask(0)),CPU_EIPSize(0));
	if (REG_ECX==0)
	{
		CPU_JMPrel((int_32)rel8,0);
		CPU_flushPIQ(-1); /*We're jumping to another address*/
		CPU[activeCPU].didJump = 1;
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 18;
		}
		/* Branch taken */
	}
	else
	{
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			CPU[activeCPU].cycles_OP += 6;
		}
		/* Branch not taken */
	}
}
void CPU80386_OPE5()
{
	INLINEREGISTER byte theimm = CPU[activeCPU].immb;
	modrm_generateInstructionTEXT("IN EAX,",0,theimm,PARAM_IMM8_PARAM);
	if (CPU_PORT_IN_D(0,theimm,&REG_EAX)) return;
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 10-EU_CYCLES_SUBSTRACT_ACCESSREAD; /*Timings!*/
	}
}
void CPU80386_OPE7()
{
	INLINEREGISTER byte theimm = CPU[activeCPU].immb;
	debugger_setcommand("OUT %02X,EAX",theimm);
	if (CPU_PORT_OUT_D(0,theimm,REG_EAX)) return;
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 10-EU_CYCLES_SUBSTRACT_ACCESSWRITE; /*Timings!*/
	}
}
void CPU80386_OPE8()
{
	INLINEREGISTER int_32 reloffset = imm32s();
	modrm_generateInstructionTEXT("CALLD",0,((REG_EIP + reloffset)&CPU_EIPmask(0)),CPU_EIPSize(0));
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,1,1)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU80386_PUSHdw(0,&REG_EIP)) return;
	CPU_JMPrel((int_32)reloffset,0);
	CPU_flushPIQ(-1); /*We're jumping to another address*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 19-EU_CYCLES_SUBSTRACT_ACCESSREAD;
		CPU[activeCPU].cycles_stallBIU += CPU[activeCPU].cycles_OP; /*Stall the BIU completely now!*/
	}
	/* Intrasegment direct */
}
void CPU80386_OPE9()
{
	INLINEREGISTER int_32 reloffset = imm32s();
	modrm_generateInstructionTEXT("JMPD",0,((REG_EIP + reloffset)&CPU_EIPmask(0)),CPU_EIPSize(0));
	CPU_JMPrel((int_32)reloffset,0);
	CPU_flushPIQ(-1); /*We're jumping to another address*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 15;
		CPU[activeCPU].cycles_stallBIU += CPU[activeCPU].cycles_OP; /*Stall the BIU completely now!*/
	}
	/* Intrasegment direct */
}
void CPU80386_OPEA()
{
	INLINEREGISTER uint_64 segmentoffset = CPU[activeCPU].imm64;
	debugger_setcommand("JMPD %04X:%08X", (segmentoffset>>32), (segmentoffset&CPU_EIPmask(0)));
	CPU[activeCPU].destEIP = (segmentoffset&CPU_EIPmask(0));
	if (segmentWritten(CPU_SEGMENT_CS, (word)(segmentoffset>>32), 1)) return;
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 15;
	}
	/* Intersegment direct */
}
void CPU80386_OPED()
{
	modrm_generateInstructionTEXT("IN EAX,DX",0,0,PARAM_NONE);
	if (CPU_PORT_IN_D(0,REG_DX,&REG_EAX)) return;
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSREAD; /*Timings!*/
	}
}
void CPU80386_OPEF()
{
	modrm_generateInstructionTEXT("OUT DX,EAX",0,0,PARAM_NONE);
	if (CPU_PORT_OUT_D(0,REG_DX,REG_EAX)) return;
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSWRITE; /*Timings!*/
	}
	/*To memory?*/
}

/*

NOW COME THE GRP1-5 OPCODES:

*/

//GRP1

void CPU80386_OP81() //GRP1 Ev,Iv
{
	INLINEREGISTER uint_32 imm = CPU[activeCPU].imm32;
	if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
	{
		modrm_debugger32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, CPU[activeCPU].MODRM_src1);
	}
	switch (CPU[activeCPU].thereg) //What function?
	{
	case 0: //ADD
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("ADD %s,%08X",&CPU[activeCPU].modrm_param1,imm); //ADD Ed, Id
		}
		CPU80386_internal_ADD32(modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0),imm,3); //ADD Eb, Id
		break;
	case 1: //OR
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("OR %s,%08X",&CPU[activeCPU].modrm_param1,imm); //OR Ed, Id
		}
		CPU80386_internal_OR32(modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0),imm,3); //OR Eb, Id
		break;
	case 2: //ADC
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("ADC %s,%08X",&CPU[activeCPU].modrm_param1,imm); //ADC Ed, Id
		}
		CPU80386_internal_ADC32(modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0),imm,3); //ADC Eb, Id
		break;
	case 3: //SBB
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("SBB %s,%08X",&CPU[activeCPU].modrm_param1,imm); //SBB Ed, Id
		}
		CPU80386_internal_SBB32(modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0),imm,3); //SBB Eb, Id
		break;
	case 4: //AND
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("AND %s,%08X",&CPU[activeCPU].modrm_param1,imm); //AND Ed, Id
		}
		CPU80386_internal_AND32(modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0),imm,3); //AND Eb, Id
		break;
	case 5: //SUB
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("SUB %s,%08X",&CPU[activeCPU].modrm_param1,imm); //SUB Ed, Id
		}
		CPU80386_internal_SUB32(modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0),imm,3); //SUB Eb, Id
		break;
	case 6: //XOR
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("XOR %s,%08X",&CPU[activeCPU].modrm_param1,imm); //XOR Ed, Id
		}
		CPU80386_internal_XOR32(modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0),imm,3); //XOR Eb, Id
		break;
	case 7: //CMP
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("CMP %s,%08X",&CPU[activeCPU].modrm_param1,imm); //CMP Ed, Id
		}
		if (unlikely(CPU[activeCPU].modrmstep == 0))
		{
			if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 1|0x40)) return;
			if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 1|0xA0)) return;
		} //Abort when needed!
		if (CPU80386_instructionstepreadmodrmdw(0,&CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src0)) return;
		CMP_dw(CPU[activeCPU].instructionbufferd,imm,3); //CMP Eb, Id
		break;
	default:
		break;
	}
}

void CPU80386_OP83() //GRP1 Ev,Ib
{
	INLINEREGISTER uint_32 imm;
	imm = (uint_32)CPU[activeCPU].immb;
	if (imm&0x80) imm |= 0xFFFFFF00; //Sign extend!
	if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
	{
		modrm_debugger32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, CPU[activeCPU].MODRM_src1);
	}
	switch (CPU[activeCPU].thereg) //What function?
	{
	case 0: //ADD
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("ADD %s,%02X",&CPU[activeCPU].modrm_param1, CPU[activeCPU].immb); //ADD Ev, Ib
		}
		CPU80386_internal_ADD32(modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0),imm,3); //ADD Eb, Ib
		break;
	case 1: //OR
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("OR %s,%02X",&CPU[activeCPU].modrm_param1, CPU[activeCPU].immb); //OR Ev, Ib
		}
		CPU80386_internal_OR32(modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0),imm,3); //OR Eb, Ib
		break;
	case 2: //ADC
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("ADC %s,%02X",&CPU[activeCPU].modrm_param1, CPU[activeCPU].immb); //ADC Ev, Ib
		}
		CPU80386_internal_ADC32(modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0),imm,3); //ADC Eb, Ib
		break;
	case 3: //SBB
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("SBB %s,%02X",&CPU[activeCPU].modrm_param1, CPU[activeCPU].immb); //SBB Ev, Ib
		}
		CPU80386_internal_SBB32(modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0),imm,3); //SBB Eb, Ib
		break;
	case 4: //AND
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("AND %s,%02X",&CPU[activeCPU].modrm_param1, CPU[activeCPU].immb); //AND Ev, Ib
		}
		CPU80386_internal_AND32(modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0),imm,3); //AND Eb, Ib
		break;
	case 5: //SUB
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("SUB %s,%02X",&CPU[activeCPU].modrm_param1, CPU[activeCPU].immb); //SUB Ev, Ib
		}
		CPU80386_internal_SUB32(modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0),imm,3); //SUB Eb, Ib
		break;
	case 6: //XOR
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("XOR %s,%02X",&CPU[activeCPU].modrm_param1, CPU[activeCPU].immb); //XOR Ev, Ib
		}
		CPU80386_internal_XOR32(modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0),imm,3); //XOR Eb, Ib
		break;
	case 7: //CMP
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			debugger_setcommand("CMP %s,%02X",&CPU[activeCPU].modrm_param1, CPU[activeCPU].immb); //CMP Ev, Ib
		}
		if (unlikely(CPU[activeCPU].modrmstep == 0))
		{
			if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 1|0x40)) return;
			if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 1|0xA0)) return;
		} //Abort when needed!
		if (CPU80386_instructionstepreadmodrmdw(0,&CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src0)) return;
		CMP_dw(CPU[activeCPU].instructionbufferd,imm,3); //CMP Eb, Id
		break;
	default:
		break;
	}
}

void CPU80386_OP8F() //Undocumented GRP opcode 8F r/m32
{
	byte stackresult;
	switch (CPU[activeCPU].thereg) //What function?
	{
	case 0: //POP
		//Cycle-accurate emulation of the instruction!
		if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
		{
			modrm_generateInstructionTEXT("POP",32,0,PARAM_MODRM_0); //POPW Ew
		}
		if (unlikely(CPU[activeCPU].stackchecked==0))
		{
			modrm_recalc(&CPU[activeCPU].params); //Recalc if using (e)sp as the destination offset!
			if (checkStackAccess(1,0,1)) return;
			stack_pop(1); //Popped a dword!
			modrm_recalc(&CPU[activeCPU].params); //Recalc if using (e)sp as the destination offset!
			if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0x40)) return;
			if ((stackresult = modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0))!=0)
			{
				if (stackresult==2)
				{
					stack_push(1); //Popped a dword!
				}
				return; //Abort when needed!
			}
			stack_push(1); //Popped a dword!
			++CPU[activeCPU].stackchecked;
		}
		//Execution step!
		if (CPU80386_instructionstepPOPtimeout(0)) return; /*POP timeout*/
		if (CPU80386_POPdw(2,&CPU[activeCPU].value8F_32)) return; //POP first!
		if (CPU80386_instructionstepwritemodrmdw(0,CPU[activeCPU].value8F_32, CPU[activeCPU].MODRM_src0)) return; //POP r/m32
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			if (MODRM_EA(CPU[activeCPU].params)) //Mem?
			{
				CPU[activeCPU].cycles_OP += 17-EU_CYCLES_SUBSTRACT_ACCESSRW; /*Pop Mem!*/
			}
			else //Reg?
			{
				CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSREAD; /*Pop Reg!*/
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

void CPU80386_OPD1() //GRP2 Ev,1
{
	if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
	{
		modrm_debugger32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, CPU[activeCPU].MODRM_src1); //Get src!
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
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return; //Abort when needed!
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0xA0)) return; //Abort when needed!
	}
	if (CPU80386_instructionstepreadmodrmdw(0,&CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src0)) return;
	if (CPU[activeCPU].instructionstep==0) //Execution step?
	{
		CPU[activeCPU].oper1d = CPU[activeCPU].instructionbufferd;
		CPU[activeCPU].res32 = op_grp2_32(1,0); //Execute!
		++CPU[activeCPU].instructionstep; //Next step: writeback!
		CPU[activeCPU].executed = 0; //Time it!
		return; //Wait for the next step!
	}
	if (CPU80386_instructionstepwritemodrmdw(2, CPU[activeCPU].res32, CPU[activeCPU].MODRM_src0)) return;
}

void CPU80386_OPD3() //GRP2 Ev,CL
{
	if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
	{
		modrm_debugger32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, CPU[activeCPU].MODRM_src1); //Get src!
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
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return; //Abort when needed!
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0xA0)) return; //Abort when needed!
	}
	if (CPU80386_instructionstepreadmodrmdw(0,&CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src0)) return;
	if (CPU[activeCPU].instructionstep==0) //Execution step?
	{
		CPU[activeCPU].oper1d = CPU[activeCPU].instructionbufferd;
		CPU[activeCPU].res32 = op_grp2_32(REG_CL,1); //Execute!
		++CPU[activeCPU].instructionstep; //Next step: writeback!
		CPU[activeCPU].executed = 0; //Time it!
		return; //Wait for the next step!
	}
	if (CPU80386_instructionstepwritemodrmdw(2, CPU[activeCPU].res32, CPU[activeCPU].MODRM_src0)) return;
}

void CPU80386_OPF7() //GRP3b Ev
{
	if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
	{
		modrm_debugger32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, CPU[activeCPU].MODRM_src1); //Get src!
		switch (CPU[activeCPU].thereg) //What function?
		{
		case 0: //TEST modrm32, imm32
		case 1: //--- Undocumented opcode, same as above!
			debugger_setcommand("TEST %s,%08x",&CPU[activeCPU].modrm_param1, CPU[activeCPU].imm32);
			break;
		case 2: //NOT
			modrm_generateInstructionTEXT("NOT",32,0,PARAM_MODRM_0);
			break;
		case 3: //NEG
			modrm_generateInstructionTEXT("NEG",32,0,PARAM_MODRM_0);
			break;
		case 4: //MUL
			modrm_generateInstructionTEXT("MUL",32,0,PARAM_MODRM_0);
			break;
		case 5: //IMUL
			modrm_generateInstructionTEXT("IMUL",32,0,PARAM_MODRM_0);
			break;
		case 6: //DIV
			modrm_generateInstructionTEXT("DIV",32,0,PARAM_MODRM_0);
			break;
		case 7: //IDIV
			modrm_generateInstructionTEXT("IDIV",32,0,PARAM_MODRM_0);
			break;
		default:
			break;
		}
	}
	if (unlikely(CPU[activeCPU].modrmstep==0)) 
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,1|0x40)) return; //Abort when needed!
		if ((CPU[activeCPU].thereg>1) && (CPU[activeCPU].thereg<4)) //NOT/NEG?
		{
			if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return; //Abort when needed!
		}
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,1|0xA0)) return; //Abort when needed!
		if ((CPU[activeCPU].thereg>1) && (CPU[activeCPU].thereg<4)) //NOT/NEG?
		{
			if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0xA0)) return; //Abort when needed!
		}
	}
	if (CPU80386_instructionstepreadmodrmdw(0,&CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src0)) return;
	if (CPU[activeCPU].instructionstep==0) //Execution step?
	{
		CPU[activeCPU].oper1d = CPU[activeCPU].instructionbufferd;
		op_grp3_32();
		if (likely(CPU[activeCPU].executed)) ++CPU[activeCPU].instructionstep; //Next step!
		else return; //Wait for completion!
	}
	if ((CPU[activeCPU].thereg>1) && (CPU[activeCPU].thereg<4)) //NOT/NEG?
	{
		if (CPU80386_instructionstepwritemodrmdw(2, CPU[activeCPU].res32, CPU[activeCPU].MODRM_src0)) return;
	}
}

void CPU80386_OPFF() //GRP5 Ev
{
	if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
	{
		modrm_debugger32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, CPU[activeCPU].MODRM_src1); //Get src!
		switch (CPU[activeCPU].thereg) //What function?
		{
		case 0: //INC modrm8
			modrm_generateInstructionTEXT("INC",32,0,PARAM_MODRM_0); //INC!
			break;
		case 1: //DEC modrm8
			modrm_generateInstructionTEXT("DEC",32,0,PARAM_MODRM_0); //DEC!
			break;
		case 2: //CALL
			modrm_generateInstructionTEXT("CALL",32,0,PARAM_MODRM_0); //CALL!
			break;
		case 3: //CALL Mp (Read address word and jump there)
			modrm_generateInstructionTEXT("CALLF",32,0,PARAM_MODRM_0); //Jump to the address pointed here!
			break;
		case 4: //JMP
			modrm_generateInstructionTEXT("JMP",32,0,PARAM_MODRM_0); //JMP to the register!
			break;
		case 5: //JMP Mp
			modrm_generateInstructionTEXT("JMPF",32,0,PARAM_MODRM_0); //Jump to the address pointed here!
			break;
		case 6: //PUSH
			modrm_generateInstructionTEXT("PUSH",32,0,PARAM_MODRM_0); //PUSH!
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
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,1|0x40)) return; //Abort when needed!
		if ((CPU[activeCPU].thereg==3) || (CPU[activeCPU].thereg==5)) //extra segment?
		{
			CPU[activeCPU].modrm_addoffset = 4;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,1|0x40)) return; //Abort when needed!		
		}
		CPU[activeCPU].modrm_addoffset = 0;
		if (CPU[activeCPU].thereg == 3) //far JMP/CALL?
		{
			if (getcpumode() != CPU_MODE_PROTECTED) //Real mode or V86 mode?
			{
				if (unlikely(CPU[activeCPU].stackchecked == 0))
				{
					if (checkStackAccess(2, 1, 1)) return; /*We're trying to push on the stack!*/
					++CPU[activeCPU].stackchecked;
				}
			}
		}
		else if ((CPU[activeCPU].thereg == 2) || (CPU[activeCPU].thereg == 6)) //pushing something on the stack normally?
		{
			if (unlikely(CPU[activeCPU].stackchecked == 0))
			{
				if (checkStackAccess(1, 1, 1)) return;
				++CPU[activeCPU].stackchecked;
			}
		}
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,1|0xA0)) return; //Abort when needed!
		if ((CPU[activeCPU].thereg==3) || (CPU[activeCPU].thereg==5)) //extra segment?
		{
			CPU[activeCPU].modrm_addoffset = 4;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,1|0xA0)) return; //Abort when needed!		
		}
		CPU[activeCPU].modrm_addoffset = 0;
	}
	if (CPU[activeCPU].thereg>1) //Data needs to be read directly? Not INC/DEC(which already reads it's data directly)?
	{
		if (CPU80386_instructionstepreadmodrmdw(0,&CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src0)) return;
	}
	CPU[activeCPU].oper1d = CPU[activeCPU].instructionbufferd;
	op_grp5_32();
}

/*

Special stuff for NO COprocessor (8087) present/available (default)!

*/

void unkOP_80386() //Unknown opcode on 8086?
{
	CPU_unkOP(); //Execute the unknown opcode exception handler, if any!
}

//Gecontroleerd: 100% OK!

//Now, the GRP opcodes!

OPTINLINE void op_grp2_cycles32(byte cnt, byte varshift)
{
	switch (varshift) //What type of shift are we using?
	{
	case 0: //Reg/Mem with 1 shift?
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			if (MODRM_EA(CPU[activeCPU].params)) //Mem?
			{
				CPU[activeCPU].cycles_OP += 15-(EU_CYCLES_SUBSTRACT_ACCESSRW); //Mem
			}
			else //Reg?
			{
				CPU[activeCPU].cycles_OP += 2; //Reg
			}
		}
		break;
	case 1: //Reg/Mem with variable shift?
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			if (MODRM_EA(CPU[activeCPU].params)) //Mem?
			{
				CPU[activeCPU].cycles_OP += 20 + (cnt << 2)- (EU_CYCLES_SUBSTRACT_ACCESSRW); //Mem
			}
			else //Reg?
			{
				CPU[activeCPU].cycles_OP += 8 + (cnt << 2); //Reg
			}
		}
		break;
	case 2: //Reg/Mem with immediate variable shift(NEC V20/V30)?
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			if (MODRM_EA(CPU[activeCPU].params)) //Mem?
			{
				CPU[activeCPU].cycles_OP += 20 + (cnt << 2) - (EU_CYCLES_SUBSTRACT_ACCESSRW); //Mem
			}
			else //Reg?
			{
				CPU[activeCPU].cycles_OP += 8 + (cnt << 2); //Reg
			}
		}
		break;
	default:
		break;
	}
}

uint_32 op_grp2_32(byte cnt, byte varshift)
{
	INLINEREGISTER uint_64 s, shift, tempCF, msb;
	INLINEREGISTER byte numcnt,maskcnt,overflow;
	numcnt = maskcnt = cnt; //Save count!
	s = CPU[activeCPU].oper1d;
	switch (CPU[activeCPU].thereg)
	{
	case 0: //ROL r/m32
		if (EMULATED_CPU >= CPU_NECV30) maskcnt &= 0x1F; //Clear the upper 3 bits to become a NEC V20/V30+!
		numcnt = maskcnt;
		if (EMULATED_CPU>=CPU_80386) numcnt &= 0x1F; //Operand size wrap!
		overflow = numcnt?0:FLAG_OF; //Default: no overflow!
		for (shift = 1; shift <= numcnt; shift++)
		{
			FLAGW_CF(s>>31); //Save MSB!
			s = (s << 1)|FLAG_CF;
			overflow = (((s >> 31) & 1)^FLAG_CF);
		}
		if (maskcnt && (numcnt==0)) FLAGW_CF(s); //Always sets CF, according to various sources?
		if (maskcnt) FLAGW_OF(overflow); //Overflow?
		break;

	case 1: //ROR r/m32
		if (EMULATED_CPU >= CPU_NECV30) maskcnt &= 0x1F; //Clear the upper 3 bits to become a NEC V20/V30+!
		numcnt = maskcnt;
		if (EMULATED_CPU>=CPU_80386) numcnt &= 0x1F; //Operand size wrap!
		overflow = numcnt?0:FLAG_OF; //Default: no overflow!
		for (shift = 1; shift <= numcnt; shift++)
		{
			FLAGW_CF(s); //Save LSB!
			s = ((s >> 1)&0x7FFFFFFFULL) | ((uint_64)FLAG_CF << 31);
			overflow = (byte)((s >> 31) ^ ((s >> 30) & 1U));
		}
		if (maskcnt && (numcnt==0)) FLAGW_CF(s>>31); //Always sets CF, according to various sources?
		if (maskcnt) FLAGW_OF(overflow); //Overflow?
		break;

	case 2: //RCL r/m32
		if (EMULATED_CPU >= CPU_80386) maskcnt &= 0x1F; //Clear the upper 3 bits to become a NEC V20/V30+!
		numcnt = maskcnt;
		overflow = numcnt?0:FLAG_OF; //Default: no overflow!
		for (shift = 1; shift <= numcnt; shift++)
		{
			tempCF = FLAG_CF;
			FLAGW_CF(s>>31); //Save MSB!
			s = (s << 1)|tempCF; //Shift and set CF!
			overflow = (((s >> 31) & 1)^FLAG_CF); //OF=MSB^CF
		}
		if (maskcnt && (numcnt==0)) FLAGW_CF(s); //Always sets CF, according to various sources?
		if (maskcnt) FLAGW_OF(overflow); //Overflow?
		break;

	case 3: //RCR r/m32
		if (EMULATED_CPU >= CPU_80386) maskcnt &= 0x1F; //Clear the upper 3 bits to become a NEC V20/V30+!
		numcnt = maskcnt;
		overflow = numcnt?0:FLAG_OF; //Default: no overflow!
		for (shift = 1; shift <= numcnt; shift++)
		{
			overflow = (((s >> 31)&1)^FLAG_CF);
			tempCF = FLAG_CF;
			FLAGW_CF(s); //Save LSB!
			s = ((s >> 1)&0x7FFFFFFFU) | (tempCF << 31);
		}
		if (maskcnt && (numcnt==0)) FLAGW_CF(s); //Always sets CF, according to various sources?
		if (maskcnt) FLAGW_OF(overflow); //Overflow?
		break;

	case 4: case 6: //SHL r/m32
		if (EMULATED_CPU >= CPU_NECV30) maskcnt &= 0x1F; //Clear the upper 3 bits to become a NEC V20/V30+!
		numcnt = maskcnt;
		overflow = numcnt?0:FLAG_OF;
		for (shift = 1; shift <= numcnt; shift++)
		{
			FLAGW_CF(s>>31);
			s = (s << 1) & 0xFFFFFFFFU;
			overflow = (byte)(FLAG_CF^(s>>31));
		}
		if (maskcnt && (numcnt==0)) FLAGW_CF(s>>31); //Always sets CF, according to various sources?
		if (maskcnt) FLAGW_OF(overflow);
		if (maskcnt) FLAGW_AF(1);
		if (maskcnt) flag_szp32((uint32_t)(s&0xFFFFFFFFU));
		break;

	case 5: //SHR r/m32
		if (EMULATED_CPU >= CPU_NECV30) maskcnt &= 0x1F; //Clear the upper 3 bits to become a NEC V20/V30+!
		numcnt = maskcnt;
		overflow = numcnt?0:FLAG_OF;
		for (shift = 1; shift <= numcnt; shift++)
		{
			overflow = (byte)(s>>31);
			FLAGW_CF(s);
			s = s >> 1;
		}
		if (maskcnt && (numcnt==0)) FLAGW_CF(s); //Always sets CF, according to various sources?
		if (maskcnt) FLAGW_OF(overflow);
		if (maskcnt) FLAGW_AF(1);
		if (maskcnt) flag_szp32((uint32_t)(s & 0xFFFFFFFFU));
		break;

	case 7: //SAR r/m32
		if (EMULATED_CPU >= CPU_NECV30) maskcnt &= 0x1F; //Clear the upper 3 bits to become a NEC V20/V30+!
		numcnt = maskcnt;
		msb = s & 0x80000000U;
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
			flag_szp32((uint32_t)s); //Affect sign as well!
			FLAGW_OF(0); //Cleared!
		}
		else if (numcnt) //Anything shifted at all?
		{
			flag_szp32((uint32_t)s); //Affect sign as well!
			FLAGW_OF(0); //Cleared with count as well?
		}
		break;
	default:
		break;
	}
	op_grp2_cycles32(numcnt, varshift);
	return (s & 0xFFFFFFFFU);
}

OPTINLINE void op_div32(uint_64 valdiv, uint_32 divisor)
{
	if ((!divisor) && (CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		//Timings always!
		++CPU[activeCPU].internalinstructionstep; //Next step after we're done!
		CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
		CPU[activeCPU].executed = 0; //Not executed yet!
		return;
	}
	uint_32 quotient, remainder; //Result and modulo!
	byte error, applycycles; //Error/apply cycles!
	CPU80386_internal_DIV(valdiv,divisor,&quotient,&remainder,&error,32,2,6,&applycycles,0,0,0); //Execute the unsigned division! 8-bits result and modulo!
	if (error==0) //No error?
	{
		REG_EAX = quotient; //Quotient!
		REG_EDX = remainder; //Remainder!
	}
	else //Error?
	{
		CPU_exDIV0(); //Exception!
		return; //Exception executed!
	}
	if (applycycles) /* No 80286+ cycles instead? */
	{
		if (MODRM_EA(CPU[activeCPU].params)) //Memory?
		{
			CPU[activeCPU].cycles_OP += 6 - EU_CYCLES_SUBSTRACT_ACCESSREAD; //Mem max!
		}
	}
}

OPTINLINE void op_idiv32(uint_64 valdiv, uint_32 divisor)
{
	if ((!divisor) && (CPU[activeCPU].internalinstructionstep==0)) //First step?
	{
		//Timings always!
		++CPU[activeCPU].internalinstructionstep; //Next step after we're done!
		CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
		CPU[activeCPU].executed = 0; //Not executed yet!
		return;
	}

	uint_32 quotient, remainder; //Result and modulo!
	byte error, applycycles; //Error/apply cycles!
	CPU80386_internal_IDIV(valdiv,divisor,&quotient,&remainder,&error,32,2,6,&applycycles); //Execute the unsigned division! 8-bits result and modulo!
	if (error==0) //No error?
	{
		REG_EAX = quotient; //Quotient!
		REG_EDX = remainder; //Remainder!
	}
	else //Error?
	{
		CPU_exDIV0(); //Exception!
		return; //Exception executed!
	}
	if (applycycles) /* No 80286+ cycles instead? */
	{
		if (MODRM_EA(CPU[activeCPU].params)) //Memory?
		{
			CPU[activeCPU].cycles_OP += 6 - EU_CYCLES_SUBSTRACT_ACCESSREAD; //Mem max!
		}
	}
}

void op_grp3_32()
{
	switch (CPU[activeCPU].thereg)
	{
	case 0: case 1: //TEST
		CPU80386_internal_TEST32(CPU[activeCPU].oper1d, CPU[activeCPU].imm32, 3);
		break;
	case 2: //NOT
		CPU[activeCPU].res32 = ~CPU[activeCPU].oper1d;
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			if (MODRM_EA(CPU[activeCPU].params)) //Memory?
			{
				CPU[activeCPU].cycles_OP += 16 - (EU_CYCLES_SUBSTRACT_ACCESSRW); //Mem!
			}
			else //Register?
			{
				CPU[activeCPU].cycles_OP += 3; //Reg!
			}
		}
		break;
	case 3: //NEG
		CPU[activeCPU].res32 = (~CPU[activeCPU].oper1d) + 1;
		flag_sub32(0, CPU[activeCPU].oper1d);
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			if (MODRM_EA(CPU[activeCPU].params)) //Memory?
			{
				CPU[activeCPU].cycles_OP += 16 - (EU_CYCLES_SUBSTRACT_ACCESSRW); //Mem!
			}
			else //Register?
			{
				CPU[activeCPU].cycles_OP += 3; //Reg!
			}
		}
		break;
	case 4: //MULW
		CPU[activeCPU].tempEAX = REG_EAX; //Save a backup for calculating cycles!
		CPU[activeCPU].temp1l.val64 = (uint64_t)CPU[activeCPU].oper1d * (uint64_t)REG_EAX;
		REG_EAX = CPU[activeCPU].temp1l.val32;
		REG_EDX = CPU[activeCPU].temp1l.val32high;
		flag_log32(CPU[activeCPU].temp1l.val32); //Flags!
		if (REG_EDX)
		{
			FLAGW_OF(1);
		}
		else
		{
			FLAGW_OF(0);
		}
		FLAGW_CF(FLAG_OF);

		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			if (MODRM_EA(CPU[activeCPU].params)) //Memory?
			{
				CPU[activeCPU].cycles_OP += 124 - EU_CYCLES_SUBSTRACT_ACCESSREAD; //Mem max!
			}
			else //Register?
			{
				CPU[activeCPU].cycles_OP += 118; //Reg!
			}
			if (NumberOfSetBits(CPU[activeCPU].tempEAX)>1) //More than 1 bit set?
			{
				CPU[activeCPU].cycles_OP += NumberOfSetBits(CPU[activeCPU].tempEAX) - 1; //1 cycle for all bits more than 1 bit set!
			}
		}
		break;
	case 5: //IMULW
		CPU[activeCPU].temp1l.val64 = REG_EAX;
		CPU[activeCPU].temp2l.val64 = CPU[activeCPU].oper1d;
		//Sign extend!
		if (CPU[activeCPU].temp1l.val32 & 0x80000000) CPU[activeCPU].temp1l.val64 |= 0xFFFFFFFF00000000ULL;
		if (CPU[activeCPU].temp2l .val32 & 0x80000000) CPU[activeCPU].temp2l.val64 |= 0xFFFFFFFF00000000ULL;
		CPU[activeCPU].temp3l.val64s = CPU[activeCPU].temp1l .val64s; //Load and...
		CPU[activeCPU].temp3l.val64s *= CPU[activeCPU].temp2l.val64s; //Signed multiplication!
		REG_EAX = CPU[activeCPU].temp3l.val32; //into register ax
		REG_EDX = CPU[activeCPU].temp3l.val32high; //into register dx
		flag_log32(CPU[activeCPU].temp3l.val32); //Flags!
		if (((CPU[activeCPU].temp3l.val64>>31)==0ULL) || ((CPU[activeCPU].temp3l.val64>>31)==0x1FFFFFFFFULL)) FLAGW_OF(0);
		else FLAGW_OF(1);
		FLAGW_CF(FLAG_OF); //Same!
		FLAGW_SF((REG_EDX>>31)&1); //Sign flag is affected!
		FLAGW_PF(parity[CPU[activeCPU].temp3l.val32&0xFF]); //Parity flag!
		FLAGW_ZF((CPU[activeCPU].temp3l.val64==0)?1:0); //Set the zero flag!
		if (MODRM_EA(CPU[activeCPU].params)) //Memory?
		{
			CPU[activeCPU].cycles_OP = 128 + MODRM_EA(CPU[activeCPU].params); //Mem max!
		}
		else //Register?
		{
			CPU[activeCPU].cycles_OP = 134; //Reg max!
		}
		break;
	case 6: //DIV
		op_div32(((uint_64)REG_EDX << 32) | REG_EAX, CPU[activeCPU].oper1d);
		break;
	case 7: //IDIV
		op_idiv32(((uint_64)REG_EDX << 32) | REG_EAX, CPU[activeCPU].oper1d); break;
	default:
		break;
	}
}

void op_grp5_32()
{
	MODRM_PTR info; //To contain the info!
	switch (CPU[activeCPU].thereg)
	{
	case 0: //INC Ev
		if (unlikely(CPU[activeCPU].internalinstructionstep==0)) 
		{
			if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return; //Abort when needed!
			if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0xA0)) return; //Abort when needed!
		}
		CPU80386_internal_INC32(modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0));
		break;
	case 1: //DEC Ev
		if (unlikely(CPU[activeCPU].internalinstructionstep==0)) 
		{
			if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return; //Abort when needed!
			if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0xA0)) return; //Abort when needed!
		}
		CPU80386_internal_DEC32(modrm_addr32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0));
		break;
	case 2: //CALL Ev
		if (unlikely(CPU[activeCPU].stackchecked==0))
		{
			if (checkStackAccess(1,1,1)) return;
			++CPU[activeCPU].stackchecked;
		} //Abort when needed!
		if (CPU80386_PUSHdw(0,&REG_EIP)) return;
		CPU_JMPabs(CPU[activeCPU].oper1d,0);
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			if (MODRM_EA(CPU[activeCPU].params)) //Mem?
			{
				CPU[activeCPU].cycles_OP += 21 - EU_CYCLES_SUBSTRACT_ACCESSREAD; /* Intrasegment indirect through memory */
			}
			else //Register?
			{
				CPU[activeCPU].cycles_OP += 16; /* Intrasegment indirect through register */
			}
			CPU[activeCPU].cycles_stallBIU += CPU[activeCPU].cycles_OP; /*Stall the BIU completely now!*/
		}
		CPU_flushPIQ(-1); //We're jumping to another address!
		break;
	case 3: //CALL Mp
		memcpy(&info,&CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0],sizeof(info)); //Get data!

		CPU[activeCPU].modrm_addoffset = 0;

		CPU[activeCPU].destEIP = CPU[activeCPU].oper1d; //Get destination IP!
		CPUPROT1
		CPU[activeCPU].modrm_addoffset = 4; //Then destination CS!
		if (CPU8086_instructionstepreadmodrmw(2,&CPU[activeCPU].destCS,CPU[activeCPU].MODRM_src0)) return; //Get destination CS!
		CPUPROT1
		CPU[activeCPU].modrm_addoffset = 0;
		if (CPU80386_CALLF(CPU[activeCPU].destCS, CPU[activeCPU].destEIP)) return; //Call the destination address!
		CPUPROT1
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			if (MODRM_EA(CPU[activeCPU].params)) //Mem?
			{
				CPU[activeCPU].cycles_OP += 37 - (EU_CYCLES_SUBSTRACT_ACCESSREAD*2); /* Intersegment indirect */
			}
			else //Register?
			{
				CPU[activeCPU].cycles_OP += 28; /* Intersegment direct */
			}
			CPU[activeCPU].cycles_stallBIU += CPU[activeCPU].cycles_OP; /*Stall the BIU completely now!*/
		}
		CPUPROT2
		CPUPROT2
		CPUPROT2
		break;
	case 4: //JMP Ev
		CPU_JMPabs(CPU[activeCPU].oper1d,0);
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
		memcpy(&info,&CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0],sizeof(info)); //Get data!

		CPUPROT1
		CPU[activeCPU].destEIP = CPU[activeCPU].oper1d; //Convert to EIP!
		CPU[activeCPU].modrm_addoffset = 4; //Then destination CS!
		if (CPU8086_instructionstepreadmodrmw(2,&CPU[activeCPU].destCS,CPU[activeCPU].MODRM_src0)) return; //Get destination CS!
		CPU[activeCPU].modrm_addoffset = 0;
		CPUPROT1
		if (segmentWritten(CPU_SEGMENT_CS, CPU[activeCPU].destCS, 1)) return;
		CPUPROT1
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
		{
			if (MODRM_EA(CPU[activeCPU].params)) //Memory?
			{
				CPU[activeCPU].cycles_OP += 24 - (EU_CYCLES_SUBSTRACT_ACCESSREAD*2); /* Intersegment indirect through memory */
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
		break;
	case 6: //PUSH Ev
		if (unlikely(CPU[activeCPU].stackchecked==0))
		{
			if (checkStackAccess(1,1,1)) return;
			++CPU[activeCPU].stackchecked;
		}
		if (modrm_addr32(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0)==&REG_ESP) //ESP?
		{
			if (CPU80386_PUSHdw(0,&REG_ESP)) return;
		}
		else
		{
			if (CPU80386_PUSHdw(0,&CPU[activeCPU].oper1d)) return;
		}
		CPUPROT1
		if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
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
		CPUPROT2
		break;
	default: //Unknown OPcode?
		CPU_unkOP(); //Execute the unknown opcode exception handler, if any!
		break;
	}
}

/*

80186 32-bit extensions

*/

void CPU386_OP60()
{
	debugger_setcommand("PUSHAD");
	if (unlikely(CPU[activeCPU].stackchecked == 0))
	{
		if (checkStackAccess(8, 1, 1)) return; /*Abort on fault!*/
		++CPU[activeCPU].stackchecked;
	}
	CPU[activeCPU].PUSHAD_oldESP = CPU[activeCPU].oldESP;    //PUSHA
	if (CPU80386_PUSHdw(0,&REG_EAX)) return;
	CPUPROT1
	if (CPU80386_PUSHdw(2,&REG_ECX)) return;
	CPUPROT1
	if (CPU80386_PUSHdw(4,&REG_EDX)) return;
	CPUPROT1
	if (CPU80386_PUSHdw(6,&REG_EBX)) return;
	CPUPROT1
	if (CPU80386_PUSHdw(8,&CPU[activeCPU].PUSHAD_oldESP)) return;
	CPUPROT1
	if (CPU80386_PUSHdw(10,&REG_EBP)) return;
	CPUPROT1
	if (CPU80386_PUSHdw(12,&REG_ESI)) return;
	CPUPROT1
	if (CPU80386_PUSHdw(14,&REG_EDI)) return;
	CPUPROT2
	CPUPROT2
	CPUPROT2
	CPUPROT2
	CPUPROT2
	CPUPROT2
	CPUPROT2
	CPU_apply286cycles(); //Apply the 80286+ cycles!
}

void CPU386_OP61()
{
	uint_32 dummy;
	debugger_setcommand("POPAD");
	if (unlikely(CPU[activeCPU].stackchecked == 0))
	{
		if (checkStackAccess(8, 0, 1)) return; /*Abort on fault!*/
		++CPU[activeCPU].stackchecked;
	}
	if (CPU80386_POPdw(0,&REG_EDI)) return;
	CPUPROT1
	if (CPU80386_POPdw(2,&REG_ESI)) return;
	CPUPROT1
	if (CPU80386_POPdw(4,&REG_EBP)) return;
	CPUPROT1
	if (CPU80386_POPdw(6,&dummy)) return;
	CPUPROT1
	if (CPU80386_POPdw(8,&REG_EBX)) return;
	CPUPROT1
	if (CPU80386_POPdw(10,&REG_EDX)) return;
	CPUPROT1
	if (CPU80386_POPdw(12,&REG_ECX)) return;
	CPUPROT1
	if (CPU80386_POPdw(14,&REG_EAX)) return;
	CPUPROT2
	CPUPROT2
	CPUPROT2
	CPUPROT2
	CPUPROT2
	CPUPROT2
	CPUPROT2
	CPU_apply286cycles(); //Apply the 80286+ cycles!
}

//62 not implemented in fake86? Does this not exist?
void CPU386_OP62()
{
	modrm_debugger32(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0, CPU[activeCPU].MODRM_src1); //Debug the location!
	debugger_setcommand("BOUND %s,%s", CPU[activeCPU].modrm_param1, CPU[activeCPU].modrm_param2); //Opcode!

	if (modrm_isregister(CPU[activeCPU].params)) //ModR/M may only be referencing memory?
	{
		unkOP_186(); //Raise #UD!
		return; //Abort!
	}

	if (unlikely(CPU[activeCPU].modrmstep==0)) 
	{
		CPU[activeCPU].modrm_addoffset = 0; //No offset!
		if (modrm_check32(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,1|0x40)) return; //Abort on fault!
		if (modrm_check32(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,1|0x40)) return; //Abort on fault!
		CPU[activeCPU].modrm_addoffset = 4; //Max offset!
		if (modrm_check32(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,1|0x40)) return; //Abort on fault!
		CPU[activeCPU].modrm_addoffset = 0; //No offset!
		if (modrm_check32(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,1|0xA0)) return; //Abort on fault!
		if (modrm_check32(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,1|0xA0)) return; //Abort on fault!
		CPU[activeCPU].modrm_addoffset = 4; //Max offset!
		if (modrm_check32(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src1,1|0xA0)) return; //Abort on fault!
	}

	CPU[activeCPU].modrm_addoffset = 0; //No offset!
	if (CPU80386_instructionstepreadmodrmdw(0,&CPU[activeCPU].boundval32,CPU[activeCPU].MODRM_src0)) return; //Read index!
	if (CPU80386_instructionstepreadmodrmdw(2,&CPU[activeCPU].bound_min32, CPU[activeCPU].MODRM_src1)) return; //Read min!
	CPU[activeCPU].modrm_addoffset = 4; //Max offset!
	if (CPU80386_instructionstepreadmodrmdw(4,&CPU[activeCPU].bound_max32, CPU[activeCPU].MODRM_src1)) return; //Read max!
	CPU[activeCPU].modrm_addoffset = 0; //Reset offset!
	if ((unsigned2signed32(CPU[activeCPU].boundval32)<unsigned2signed32(CPU[activeCPU].bound_min32)) || (unsigned2signed32(CPU[activeCPU].boundval32)>unsigned2signed32(CPU[activeCPU].bound_max32)))
	{
		//BOUND Gv,Ma
		CPU_BoundException(); //Execute bound exception!
	}
	else //No exception?
	{
		CPU_apply286cycles(); //Apply the 80286+ cycles!
	}
}

void CPU386_OP68()
{
	uint_32 val = CPU[activeCPU].imm32;    //PUSH Iz
	debugger_setcommand("PUSH %08X",val);
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,1,1)) return;
		++CPU[activeCPU].stackchecked;
	} //Abort on fault!
	if (CPU80386_PUSHdw(0,&val)) return; //PUSH!
	CPU_apply286cycles(); //Apply the 80286+ cycles!
}

void CPU386_OP69()
{
	memcpy(&CPU[activeCPU].info,&CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0],sizeof(CPU[activeCPU].info)); //Reg!
	memcpy(&CPU[activeCPU].info2,&CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src1],sizeof(CPU[activeCPU].info2)); //Second parameter(R/M)!
	if ((MODRM_MOD(CPU[activeCPU].params.modrm)==3) && (CPU[activeCPU].info.reg32== CPU[activeCPU].info2.reg32)) //Two-operand version?
	{
		debugger_setcommand("IMUL %s,%08X", CPU[activeCPU].info.text, CPU[activeCPU].imm32); //IMUL reg,imm32
	}
	else //Three-operand version?
	{
		debugger_setcommand("IMUL %s,%s,%08X", CPU[activeCPU].info.text, CPU[activeCPU].info2.text, CPU[activeCPU].imm32); //IMUL reg,r/m32,imm32
	}
	if (unlikely(CPU[activeCPU].instructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].modrmstep==0))
		{
			if (modrm_check32(&CPU[activeCPU].params,1,1|0x40)) return; //Abort on fault!
			if (modrm_check32(&CPU[activeCPU].params,1,1|0xA0)) return; //Abort on fault!
		}
		if (CPU80386_instructionstepreadmodrmdw(0,&CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src1)) return; //Read R/M!
		CPU[activeCPU].temp1l.val32high = 0; //Clear high part by default!
		++CPU[activeCPU].instructionstep; //Next step!
	}
	if (CPU[activeCPU].instructionstep==1) //Second step?
	{
		CPU_CIMUL(CPU[activeCPU].instructionbufferd,32, CPU[activeCPU].imm32,32,&CPU[activeCPU].IMULresult,32); //Execute!
		CPU_apply286cycles(); //Apply the 80286+ cycles!
		//We're writing to the register always, so no normal writeback!
		++CPU[activeCPU].instructionstep; //Next step!
	}
	modrm_write32(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0, CPU[activeCPU].IMULresult); //Write to the destination(register)!
}

void CPU386_OP6A()
{
	uint_32 val = (uint_32)CPU[activeCPU].immb; //Read the value!
	if (CPU[activeCPU].immb&0x80) val |= 0xFFFFFF00; //Sign-extend to 32-bit!
	debugger_setcommand("PUSH %02X",(val&0xFF)); //PUSH this!
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,1,1)) return;
		++CPU[activeCPU].stackchecked;
	} //Abort on fault!
	if (CPU80386_PUSHdw(0,&val)) return;    //PUSH Ib
	CPU_apply286cycles(); //Apply the 80286+ cycles!
}

void CPU386_OP6B()
{
	memcpy(&CPU[activeCPU].info,&CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0],sizeof(CPU[activeCPU].info)); //Reg!
	memcpy(&CPU[activeCPU].info2,&CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src1],sizeof(CPU[activeCPU].info2)); //Second parameter(R/M)!
	if ((MODRM_MOD(CPU[activeCPU].params.modrm)==3) && (CPU[activeCPU].info.reg32==CPU[activeCPU].info2.reg32)) //Two-operand version?
	{
		debugger_setcommand("IMUL %s,%02X", CPU[activeCPU].info.text, CPU[activeCPU].immb); //IMUL reg,imm8
	}
	else //Three-operand version?
	{
		debugger_setcommand("IMUL %s,%s,%02X", CPU[activeCPU].info.text, CPU[activeCPU].info2.text, CPU[activeCPU].immb); //IMUL reg,r/m32,imm8
	}

	if (unlikely(CPU[activeCPU].instructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].modrmstep==0))
		{
			if (modrm_check32(&CPU[activeCPU].params,1,1|0x40)) return; //Abort on fault!
			if (modrm_check32(&CPU[activeCPU].params,1,1|0xA0)) return; //Abort on fault!
		}
		if (CPU80386_instructionstepreadmodrmdw(0,&CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src1)) return; //Read R/M!
		CPU[activeCPU].temp1l.val32high = 0; //Clear high part by default!
		++CPU[activeCPU].instructionstep; //Next step!
	}
	if (CPU[activeCPU].instructionstep==1) //Second step?
	{
		CPU_CIMUL(CPU[activeCPU].instructionbufferd,32, CPU[activeCPU].immb,8,&CPU[activeCPU].IMULresult,32); //Execute!
		CPU_apply286cycles(); //Apply the 80286+ cycles!
		//We're writing to the register always, so no normal writeback!
		++CPU[activeCPU].instructionstep; //Next step!
	}

	modrm_write32(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0, CPU[activeCPU].IMULresult); //Write to register!
}

void CPU386_OP6D()
{
	debugger_setcommand("INSD");
	if (CPU[activeCPU].blockREP) return; //Disabled REP!
	if (unlikely(CPU[activeCPU].internalinstructionstep==0))
	{
		if (checkMMUaccess32(CPU_SEGMENT_ES,REG_ES,(CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI),0|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0|0x10)) return; //Abort on fault!
		if (checkMMUaccess32(CPU_SEGMENT_ES, REG_ES, (CPU[activeCPU].CPU_Address_size ? REG_EDI : REG_DI), 0|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0 | 0x10)) return; //Abort on fault!
	}
	if (CPU_PORT_IN_D(0,REG_DX, &CPU[activeCPU].data32)) return; //Read the port!
	CPUPROT1
	if (CPU80386_instructionstepwritedirectdw(0,CPU_SEGMENT_ES,REG_ES,(CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI), CPU[activeCPU].data32,!CPU[activeCPU].CPU_Address_size)) return; //INSD
	CPUPROT1
	if (FLAG_DF)
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			REG_EDI -= 4;
		}
		else
		{
			REG_DI -= 4;
		}
	}
	else
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			REG_EDI += 4;
		}
		else
		{
			REG_DI += 4;
		}
	}
	CPU_apply286cycles(); //Apply the 80286+ cycles!
	CPUPROT2
	CPUPROT2
}

void CPU386_OP6F()
{
	debugger_setcommand("OUTSD");
	if (CPU[activeCPU].blockREP) return; //Disabled REP!
	if (unlikely(CPU[activeCPU].modrmstep==0))
	{
		if (checkMMUaccess32(CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),(CPU[activeCPU].CPU_Address_size?REG_ESI:REG_SI),1|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0|0x10)) return; //Abort on fault!
		if (checkMMUaccess32(CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].CPU_Address_size ? REG_ESI : REG_SI), 1|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0 | 0x10)) return; //Abort on fault!
	}
	if (CPU80386_instructionstepreaddirectdw(0,CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),(CPU[activeCPU].CPU_Address_size?REG_ESI:REG_SI),&CPU[activeCPU].data32,!CPU[activeCPU].CPU_Address_size)) return; //OUTSD
	CPUPROT1
	if (CPU_PORT_OUT_D(0,REG_DX, CPU[activeCPU].data32)) return;    //OUTS DX,Xz
	CPUPROT1
	if (FLAG_DF)
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			REG_ESI -= 4;
		}
		else
		{
			REG_SI -= 4;
		}
	}
	else
	{
		if (CPU[activeCPU].CPU_Address_size)
		{
			REG_ESI += 4;
		}
		else
		{
			REG_SI += 4;
		}
	}
	CPU_apply286cycles(); //Apply the 80286+ cycles!
	CPUPROT2
	CPUPROT2
}

void CPU386_OPC1()
{
	memcpy(&CPU[activeCPU].info,&CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0],sizeof(CPU[activeCPU].info)); //Store the address for debugging!
	CPU[activeCPU].oper2d = (uint_32)CPU[activeCPU].immb;
	switch (CPU[activeCPU].thereg) //What function?
	{
		case 0: //ROL
			debugger_setcommand("ROL %s,%02X", CPU[activeCPU].info.text, CPU[activeCPU].oper2d);
			break;
		case 1: //ROR
			debugger_setcommand("ROR %s,%02X", CPU[activeCPU].info.text, CPU[activeCPU].oper2d);
			break;
		case 2: //RCL
			debugger_setcommand("RCL %s,%02X", CPU[activeCPU].info.text, CPU[activeCPU].oper2d);
			break;
		case 3: //RCR
			debugger_setcommand("RCR %s,%02X", CPU[activeCPU].info.text, CPU[activeCPU].oper2d);
			break;
		case 4: //SHL
		case 6: //--- Unknown Opcode! --- Undocumented opcode!
			debugger_setcommand("SHL %s,%02X", CPU[activeCPU].info.text, CPU[activeCPU].oper2d);
			break;
		case 5: //SHR
			debugger_setcommand("SHR %s,%02X", CPU[activeCPU].info.text, CPU[activeCPU].oper2d);
			break;
		case 7: //SAR
			debugger_setcommand("SAR %s,%02X", CPU[activeCPU].info.text, CPU[activeCPU].oper2d);
			break;
		default:
			break;
	}
	
	if (unlikely(CPU[activeCPU].modrmstep==0))
	{
		if (modrm_check32(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0x40)) return; //Abort when needed!
		if (modrm_check32(&CPU[activeCPU].params,CPU[activeCPU].MODRM_src0,0|0xA0)) return; //Abort when needed!
	}
	if (CPU80386_instructionstepreadmodrmdw(0,&CPU[activeCPU].instructionbufferd,CPU[activeCPU].MODRM_src0)) return;
	if (CPU[activeCPU].instructionstep==0) //Execution step?
	{
		CPU[activeCPU].oper1d = CPU[activeCPU].instructionbufferd;
		CPU[activeCPU].res32 = op_grp2_32((byte)CPU[activeCPU].oper2d,2); //Execute!
		++CPU[activeCPU].instructionstep; //Next step: writeback!
		CPU[activeCPU].executed = 0; //Time it!
		return; //Wait for the next step!
	}
	if (CPU80386_instructionstepwritemodrmdw(2, CPU[activeCPU].res32,CPU[activeCPU].MODRM_src0)) return;
} //GRP2 Ev,Ib

void CPU386_OPC8_32()
{
	byte memoryaccessfault;
	uint_32 temp16;    //ENTER Iw,Ib
	word stacksize = CPU[activeCPU].immw;
	byte nestlev = CPU[activeCPU].immb;
	debugger_setcommand("ENTERD %04X,%02X",stacksize,nestlev);
	nestlev &= 0x1F; //MOD 32!
	if (EMULATED_CPU>CPU_80486) //We don't check it all before, but during the execution on 486- processors!
	{
		if (unlikely(CPU[activeCPU].instructionstep==0))
		{
			if (checkStackAccess(1+nestlev,1,1)) return; //Abort on error!
			if (checkENTERStackAccess((nestlev>1)?(nestlev-1):0,1)) return; //Abort on error!
		}
	}
	CPU[activeCPU].ENTER_L = nestlev; //Set the nesting level used!
	//according to http://www.felixcloutier.com/x86/ENTER.html
	if (EMULATED_CPU<=CPU_80486) //We don't check it all before, but during the execution on 486- processors!
	{
		if (unlikely(CPU[activeCPU].instructionstep==0)) if (checkStackAccess(1,1,1)) return; //Abort on error!		
	}

	if (CPU80386_PUSHdw(0,&REG_EBP)) return; //Busy pushing?

	word framestep, instructionstep;
	instructionstep = 2; //We start at step 2 for the stack operations on instruction step!
	framestep = 0; //We start at step 0 for the stack frame operations!
	if (CPU[activeCPU].instructionstep == instructionstep)
	{
		CPU[activeCPU].frametempd = REG_ESP; //Read the original value to start at(for stepping compatibility)!
		++CPU[activeCPU].instructionstep; //Instruction step is progressed!
	}
	++instructionstep; //Instruction step is progressed!
	if (nestlev)
	{
		if (nestlev>1) //More than 1?
		{
			for (temp16=1; temp16<nestlev; ++temp16)
			{
				if (EMULATED_CPU<=CPU_80486) //We don't check it all before, but during the execution on 486- processors!
				{
					if (CPU[activeCPU].modrmstep==framestep) if (checkENTERStackAccess(1,1)) return; //Abort on error!				
				}
				if (CPU80386_instructionstepreaddirectdw(framestep,CPU_SEGMENT_SS,REG_SS,(STACK_SEGMENT_DESCRIPTOR_B_BIT()?REG_EBP:REG_BP)-(temp16<<2),&CPU[activeCPU].bpdatad,(STACK_SEGMENT_DESCRIPTOR_B_BIT()^1))) return; //Read data from memory to copy the stack!
				framestep += 2; //We're adding 2 immediately!
				if (unlikely(CPU[activeCPU].instructionstep==instructionstep)) //At the write back phase?
				{
					if (EMULATED_CPU<=CPU_80486) //We don't check it all before, but during the execution on 486- processors!
					{
						if (checkStackAccess(1,1,1)) return; //Abort on error!
					}
				}
				if (CPU80386_PUSHdw(instructionstep, &CPU[activeCPU].bpdatad)) return; //Write back!
				instructionstep += 2; //Next instruction step base to process!
			}
		}
		if (EMULATED_CPU<=CPU_80486) //We don't check it all before, but during the execution on 486- processors!
		{
			if (checkStackAccess(1,1,1)) return; //Abort on error!		
		}
		if (CPU80386_PUSHdw(instructionstep,&CPU[activeCPU].frametempd)) return; //Felixcloutier.com says frametemp, fake86 says Sp(incorrect).
		instructionstep += 2; //Next instruction step base to process!
	}
	
	if (CPU[activeCPU].instructionstep == instructionstep) //Finish step?
	{
		CPU[activeCPU].enter_finalESP = REG_ESP; //Final ESP!
		CPU[activeCPU].instructionstep += 2; //Next instruction step base to process!
	}
	else
	{
		REG_ESP = CPU[activeCPU].enter_finalESP; //Restore ESP!
	}

	REG_EBP = CPU[activeCPU].frametempd;
	if (STACK_SEGMENT_DESCRIPTOR_B_BIT()) //32-bit stack?
	{
		REG_ESP -= stacksize; //Substract: the stack size is data after the buffer created, not immediately at the params.  
	}
	else
	{
		REG_SP -= stacksize; //Substract: the stack size is data after the buffer created, not immediately at the params.  
	}

	//page fault if cannot write to esp pointer!

	if ((memoryaccessfault = checkMMUaccess(CPU_SEGMENT_SS, REG_SS, REG_ESP&getstackaddrsizelimiter(), 0|0x40, getCPL(), !STACK_SEGMENT_DESCRIPTOR_B_BIT(), 0 | (0x0)))!=0) //Error accessing memory?
	{
		return; //Abort on fault!
	}

	if ((memoryaccessfault = checkMMUaccess(CPU_SEGMENT_SS, REG_SS, REG_ESP&getstackaddrsizelimiter(), 0|0xA0, getCPL(), !STACK_SEGMENT_DESCRIPTOR_B_BIT(), 0 | (0x0)))!=0) //Error accessing memory?
	{
		return; //Abort on fault!
	}

	CPU_apply286cycles(); //Apply the 80286+ cycles!
}

void CPU386_OPC8_16()
{
	byte memoryaccessfault;
	word temp16;    //ENTER Iw,Ib
	word stacksize = CPU[activeCPU].immw;
	byte nestlev = CPU[activeCPU].immb;
	debugger_setcommand("ENTER %04X,%02X",stacksize,nestlev);
	nestlev &= 0x1F; //MOD 32!
	if (EMULATED_CPU>CPU_80486) //We don't check it all before, but during the execution on 486- processors!
	{
		if (unlikely(CPU[activeCPU].instructionstep==0)) 
		{
			if (checkStackAccess(1+nestlev,1,0)) return; //Abort on error!
			if (checkENTERStackAccess((nestlev>1)?(nestlev-1):0,0)) return; //Abort on error!
		}
	}
	CPU[activeCPU].ENTER_L = nestlev; //Set the nesting level used!
	//according to http://www.felixcloutier.com/x86/ENTER.html
	if (EMULATED_CPU<=CPU_80486) //We don't check it all before, but during the execution on 486- processors!
	{
		if (unlikely(CPU[activeCPU].instructionstep==0)) if (checkStackAccess(1,1,0)) return; //Abort on error!		
	}

	if (CPU8086_PUSHw(0,&REG_BP,0)) return; //Busy pushing?
	word framestep, instructionstep;
	instructionstep = 2; //We start at step 2 for the stack operations on instruction step!
	framestep = 0; //We start at step 0 for the stack frame operations!
	if (CPU[activeCPU].instructionstep == instructionstep)
	{
		CPU[activeCPU].frametempw = (word)REG_ESP; //Read the original value to start at(for stepping compatibility)!
		++CPU[activeCPU].instructionstep; //Instruction step is progressed!
	}
	++instructionstep; //Instruction step is progressed!
	if (nestlev)
	{
		if (nestlev>1) //More than 1?
		{
			for (temp16=1; temp16<nestlev; ++temp16)
			{
				if (EMULATED_CPU<=CPU_80486) //We don't check it all before, but during the execution on 486- processors!
				{
					if (CPU[activeCPU].modrmstep==framestep) if (checkENTERStackAccess(1,0)) return; //Abort on error!				
				}
				if (CPU8086_instructionstepreaddirectw(framestep,CPU_SEGMENT_SS,REG_SS, (STACK_SEGMENT_DESCRIPTOR_B_BIT()?REG_EBP:REG_BP)-(temp16<<1),&CPU[activeCPU].bpdataw,(STACK_SEGMENT_DESCRIPTOR_B_BIT()^1))) return; //Read data from memory to copy the stack!
				framestep += 2; //We're adding 2 immediately!
				if (CPU[activeCPU].instructionstep==instructionstep) //At the write back phase?
				{
					if (EMULATED_CPU<=CPU_80486) //We don't check it all before, but during the execution on 486- processors!
					{
						if (checkStackAccess(1,1,0)) return; //Abort on error!
					}
				}
				if (CPU8086_PUSHw(instructionstep, &CPU[activeCPU].bpdataw, 0)) return; //Write back!
				instructionstep += 2; //Next instruction step base to process!
			}
		}
		if (EMULATED_CPU<=CPU_80486) //We don't check it all before, but during the execution on 486- processors!
		{
			if (checkStackAccess(1,1,0)) return; //Abort on error!		
		}
		if (CPU8086_PUSHw(instructionstep,&CPU[activeCPU].frametempw,0)) return; //Felixcloutier.com says frametemp, fake86 says Sp(incorrect).
		instructionstep += 2; //Next instruction step base to process!
	}
	
	if (CPU[activeCPU].instructionstep == instructionstep) //Finish step?
	{
		CPU[activeCPU].enter_finalESP = REG_ESP; //Final ESP!
		CPU[activeCPU].instructionstep += 2; //Next instruction step base to process!
	}
	else
	{
		REG_ESP = CPU[activeCPU].enter_finalESP; //Restore ESP!
	}

	REG_BP = CPU[activeCPU].frametempw;
	if (STACK_SEGMENT_DESCRIPTOR_B_BIT()) //32-bit stack?
	{
		REG_ESP -= stacksize; //Substract: the stack size is data after the buffer created, not immediately at the params.  
	}
	else
	{
		REG_SP -= stacksize; //Substract: the stack size is data after the buffer created, not immediately at the params.  
	}
	
	//page fault if cannot write to esp pointer!
	if ((memoryaccessfault = checkMMUaccess(CPU_SEGMENT_SS, REG_SS, REG_ESP&getstackaddrsizelimiter(), 0|0x40, getCPL(), !STACK_SEGMENT_DESCRIPTOR_B_BIT(), 0 | (0x0)))!=0) //Error accessing memory?
	{
		return; //Abort on fault!
	}

	if ((memoryaccessfault = checkMMUaccess(CPU_SEGMENT_SS, REG_SS, REG_ESP&getstackaddrsizelimiter(), 0|0xA0, getCPL(), !STACK_SEGMENT_DESCRIPTOR_B_BIT(), 0 | (0x0)))!=0) //Error accessing memory?
	{
		return; //Abort on fault!
	}

	CPU_apply286cycles(); //Apply the 80286+ cycles!
}

void CPU386_OPC9_32()
{
	debugger_setcommand("LEAVE");
	if (CPU[activeCPU].instructionstep==0) //Starting?
	{
		if (unlikely(STACK_SEGMENT_DESCRIPTOR_B_BIT())) //32-bit stack?
		{
			REG_ESP = REG_EBP; //LEAVE starting!
		}
		else
		{
			REG_SP = REG_BP; //LEAVE starting!
		}
		++CPU[activeCPU].instructionstep; //Next step!
	}
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,0,1)) return;
		++CPU[activeCPU].stackchecked;
	} //Abort on fault!
	if (CPU80386_POPdw(1,&REG_EBP)) //Not done yet?
	{
		return; //Abort!
	}
	CPU_apply286cycles(); //Apply the 80286+ cycles!
}

void CPU386_OPC9_16()
{
	debugger_setcommand("LEAVE");
	if (CPU[activeCPU].instructionstep==0) //Starting?
	{
		if (unlikely(STACK_SEGMENT_DESCRIPTOR_B_BIT())) //32-bit stack?
		{
			REG_ESP = REG_EBP; //LEAVE starting!
		}
		else
		{
			REG_SP = REG_BP; //LEAVE starting!
		}
		++CPU[activeCPU].instructionstep; //Next step!
	}
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,0,0)) return;
		++CPU[activeCPU].stackchecked;
	} //Abort on fault!
	if (CPU8086_POPw(1,&REG_BP,0)) //Not done yet?
	{
		return; //Abort!
	}
	CPU_apply286cycles(); //Apply the 80286+ cycles!
}

/*

80286 32-bit extensions aren't needed: they're 0F opcodes and 16-bit/32-bit instructions extensions only.

*/

/*

No 80386 are needed: only 0F opcodes are used(286+ 32-bit versions and 80386+ opcodes)!

*/
