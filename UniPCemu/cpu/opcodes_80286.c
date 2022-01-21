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
#include "headers/cpu/easyregs.h" //Easy register addressing!
#include "headers/cpu/protection.h" //Protection support!
#include "headers/cpu/cpu_OP80286.h" //80286 instruction support!
#include "headers/cpu/cpu_OPNECV30.h" //186+ #UD support!
#include "headers/cpu/cpu_execution.h" //Execution support!
#include "headers/emu/debugger/debugger.h" //Debugger support!
#include "headers/cpu/biu.h" //PIQ flushing support!
#include "headers/support/log.h" //Logging support!
#include "headers/cpu/cpu_pmtimings.h" //Timing support!

//Reading of the 16-bit entries within descriptors!
#define DESC_16BITS(x) SDL_SwapLE16(x)

/*

Interrupts:
0x00: //Divide error (0x00)
0x01: //Debug Exceptions (0x01)
0x02: //NMI Interrupt
0x03: //Breakpoint: software only!
0x04: //INTO Detected Overflow
0x05: //BOUND Range Exceeded
0x06: //Invalid Opcode
0x07: //Coprocessor Not Available
0x08: //Double Fault
0x09: //Coprocessor Segment Overrun
0x0A: //Invalid Task State Segment
0x0B: //Segment not present
0x0C: //Stack fault
0x0D: //General Protection Fault
0x0E: //Page Fault
0x0F: //Reserved
//0x10:
0x10: //Coprocessor Error
0x11: //Allignment Check

*/

//Modr/m support, used when reg=NULL and custommem==0

OPTINLINE byte CPU80286_instructionstepPOPtimeout(word base)
{
	return CPU8086_instructionstepdelayBIU(base,2);//Delay 2 cycles for POPs to start!
}

void CPU286_OP63() //ARPL r/m16,r16
{
	modrm_generateInstructionTEXT("ARPL",16,0,PARAM_MODRM_01); //Our instruction text!
	if (getcpumode() != CPU_MODE_PROTECTED) //Real/V86 mode? #UD!
	{
		unkOP_186(); //Execute our unk opcode handler!
		return; //Abort!
	}
	if (unlikely(CPU[activeCPU].modrmstep==0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,1|0x40)) return; //Abort on fault!
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1,1|0x40)) return; //Abort on fault!
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,1|0xA0)) return; //Abort on fault!
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1,1|0xA0)) return; //Abort on fault!
	}
	if (CPU8086_instructionstepreadmodrmw(0,&CPU[activeCPU].ARPL_destRPL, CPU[activeCPU].MODRM_src0)) return; //Read destination RPL!
	CPUPROT1
	if (CPU8086_instructionstepreadmodrmw(2,&CPU[activeCPU].ARPL_srcRPL, CPU[activeCPU].MODRM_src1)) return; //Read source RPL!
	CPUPROT1
		if (unlikely(CPU[activeCPU].instructionstep==0))
		{
			if (getRPL(CPU[activeCPU].ARPL_destRPL) < getRPL(CPU[activeCPU].ARPL_srcRPL))
			{
				FLAGW_ZF(1); //Set ZF!
				setRPL(CPU[activeCPU].ARPL_destRPL,getRPL(CPU[activeCPU].ARPL_srcRPL)); //Set the new RPL!
				if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return; //Abort on fault!
				if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0xA0)) return; //Abort on fault!
			}
			else
			{
				FLAGW_ZF(0); //Clear ZF!
			}
			++CPU[activeCPU].instructionstep;
			CPU_apply286cycles(); //Apply the 80286+ cycles!
			CPU[activeCPU].executed = 0; //Still running!
			return; //Apply the cycles!
		}
		if (FLAG_ZF) if (CPU8086_instructionstepwritemodrmw(4, CPU[activeCPU].ARPL_destRPL, CPU[activeCPU].MODRM_src0,0)) return; //Set the result!
	CPUPROT2
	CPUPROT2
}

//See opcodes_8086.c:
#define EU_CYCLES_SUBSTRACT_ACCESSREAD 4

void CPU286_OP9D()
{
	modrm_generateInstructionTEXT("POPF", 0, 0, PARAM_NONE);/*POPF*/
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,0,0)) return;
		++CPU[activeCPU].stackchecked;
	}
	if (CPU80286_instructionstepPOPtimeout(0)) return; /*POP timeout*/
	if (CPU8086_POPw(2,&CPU[activeCPU].POPF_tempflags,0)) return;
	if (disallowPOPFI())
	{
		CPU[activeCPU].POPF_tempflags &= ~0x200;
		CPU[activeCPU].POPF_tempflags |= REG_FLAGS&0x200; /* Ignore any changes to the Interrupt flag! */
	}
	if (getCPL())
	{
		CPU[activeCPU].POPF_tempflags &= ~0x3000;
		CPU[activeCPU].POPF_tempflags |= REG_FLAGS&0x3000; /* Ignore any changes to the IOPL when not at CPL 0! */
	}
	REG_FLAGS = CPU[activeCPU].POPF_tempflags;
	updateCPUmode(); /*POPF*/
	if (CPU_apply286cycles()==0) /* No 80286+ cycles instead? */
	{
		CPU[activeCPU].cycles_OP += 8-EU_CYCLES_SUBSTRACT_ACCESSREAD; /*POPF timing!*/
	}
	CPU[activeCPU].allowTF = 0; /*Disallow TF to be triggered after the instruction!*/
	/*CPU[activeCPU].unaffectedRF = 1;*/ //Default: affected!
}

void CPU286_OPD6() //286+ SALC
{
	debugger_setcommand("SALC");
	REG_AL = FLAG_CF?0xFF:0x00; //Set AL if Carry flag!
	CPU_apply286cycles(); //Apply the 80286+ cycles!
}

//0F opcodes for 286+ processors!

//Based on http://ref.x86asm.net/coder32.html

void CPU286_OP0F00() //Various extended 286+ instructions GRP opcode.
{
	memcpy(&CPU[activeCPU].info,&CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0],sizeof(CPU[activeCPU].info)); //Store the address for debugging!
	switch (CPU[activeCPU].thereg) //What function?
	{
	case 0: //SLDT
		if (getcpumode() != CPU_MODE_PROTECTED)
		{
			unkOP0F_286(); //We're not recognized in real mode!
			return;
		}
		debugger_setcommand("SLDT %s", CPU[activeCPU].info.text);
		if (unlikely(CPU[activeCPU].modrmstep == 0))
		{
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0x40)) return;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0xA0)) return;
		} //Abort on fault!
		if (CPU8086_instructionstepwritemodrmw(0,REG_LDTR, CPU[activeCPU].MODRM_src0,0)) return; //Try and write it to the address specified!
		CPU_apply286cycles(); //Apply the 80286+ cycles!
		break;
	case 1: //STR
		if (getcpumode() != CPU_MODE_PROTECTED)
		{
			unkOP0F_286(); //We're not recognized in real mode!
			return;
		}
		debugger_setcommand("STR %s", CPU[activeCPU].info.text);
		if (unlikely(CPU[activeCPU].modrmstep == 0))
		{
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0x40)) return;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0xA0)) return;
		} //Abort on fault!
		if (CPU8086_instructionstepwritemodrmw(0,REG_TR, CPU[activeCPU].MODRM_src0,0)) return; //Try and write it to the address specified!
		CPU_apply286cycles(); //Apply the 80286+ cycles!
		break;
	case 2: //LLDT
		if (getcpumode() != CPU_MODE_PROTECTED)
		{
			unkOP0F_286(); //We're not recognized in real mode!
			return;
		}
		debugger_setcommand("LLDT %s", CPU[activeCPU].info.text);
		if (getCPL()) //Privilege level isn't 0?
		{
			THROWDESCGP(0,0,0); //Throw #GP!
			return; //Abort!
		}
		if (unlikely(CPU[activeCPU].modrmstep == 0))
		{
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 1|0x40)) return;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 1|0xA0)) return;
		} //Abort on fault!
		if (CPU8086_instructionstepreadmodrmw(0,&CPU[activeCPU].oper1, CPU[activeCPU].MODRM_src0)) return; //Read the descriptor!
		CPUPROT1
			if (segmentWritten(CPU_SEGMENT_LDTR, CPU[activeCPU].oper1,0)) return; //Write the segment!
		CPUPROT2
		CPU_apply286cycles(); //Apply the 80286+ cycles!
		break;
	case 3: //LTR
		if (getcpumode() != CPU_MODE_PROTECTED)
		{
			unkOP0F_286(); //We're not recognized in real mode!
			return;
		}
		debugger_setcommand("LTR %s", CPU[activeCPU].info.text);
		if (getCPL()) //Privilege level isn't 0?
		{
			THROWDESCGP(0,0,0); //Throw #GP!
			return; //Abort!
		}
		if (unlikely(CPU[activeCPU].modrmstep == 0))
		{
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 1|0x40)) return;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 1|0xA0)) return;
		} //Abort on fault!
		if (CPU8086_instructionstepreadmodrmw(0,&CPU[activeCPU].oper1, CPU[activeCPU].MODRM_src0)) return; //Read the descriptor!
		CPUPROT1
			if (segmentWritten(CPU_SEGMENT_TR, CPU[activeCPU].oper1, 0)) return; //Write the segment!
			CPU_apply286cycles(); //Apply the 80286+ cycles!
		CPUPROT2
		break;
	case 4: //VERR
		if (getcpumode() != CPU_MODE_PROTECTED)
		{
			unkOP0F_286(); //We're not recognized in real mode!
			return;
		}
		debugger_setcommand("VERR %s", CPU[activeCPU].info.text);
		if (unlikely(CPU[activeCPU].modrmstep == 0))
		{
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 1|0x40)) return;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 1|0xA0)) return;
		} //Abort on fault!
		if (CPU8086_instructionstepreadmodrmw(0,&CPU[activeCPU].oper1, CPU[activeCPU].MODRM_src0)) return; //Read the descriptor!
		CPUPROT1
			SEGMENT_DESCRIPTOR verdescriptor;
			sbyte loadresult;
			if ((loadresult = LOADDESCRIPTOR(-1, CPU[activeCPU].oper1, &verdescriptor,0))==1) //Load the descriptor!
			{
				if ((CPU[activeCPU].oper1 & 0xFFFC) == 0) //NULL segment selector?
				{
					goto invalidresultVERR286;
				}
				if (
					((MAX(getCPL(),getRPL(CPU[activeCPU].oper1))>GENERALSEGMENT_DPL(verdescriptor)) && (!((getLoadedTYPE(&verdescriptor)==1) && EXECSEGMENT_C(verdescriptor))))  //We are a lower privilege level with either a data/system/non-conforming segment descriptor? Conforming code segments don't check!
					)
				{
					FLAGW_ZF(0); //We're invalid!
				}
				else if (GENERALSEGMENT_S(verdescriptor)==0) //Not code/data?
				{
					FLAGW_ZF(0); //We're invalid!
				}
				else if (!(EXECSEGMENT_ISEXEC(verdescriptor) && (EXECSEGMENT_R(verdescriptor)==0))) //Not an unreadable code segment? We're either a data segment(always readable) or readable code segment!
				{
					FLAGW_ZF(1); //We're valid!
				}
				else
				{
					FLAGW_ZF(0); //We're invalid!
				}
			}
			else
			{
				if (loadresult == 0)
				{
					invalidresultVERR286:
					FLAGW_ZF(0); //We're invalid!
				}
			}
			CPU_apply286cycles(); //Apply the 80286+ cycles!
		CPUPROT2
		break;
	case 5: //VERW
		if (getcpumode() != CPU_MODE_PROTECTED)
		{
			unkOP0F_286(); //We're not recognized in real mode!
			return;
		}
		debugger_setcommand("VERW %s", CPU[activeCPU].info.text);
		if (unlikely(CPU[activeCPU].modrmstep == 0))
		{
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 1|0x40)) return;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 1|0xA0)) return;
		} //Abort on fault!
		if (CPU8086_instructionstepreadmodrmw(0,&CPU[activeCPU].oper1, CPU[activeCPU].MODRM_src0)) return; //Read the descriptor!
		CPUPROT1
			SEGMENT_DESCRIPTOR verdescriptor;
			sbyte loadresult;
			if ((loadresult = LOADDESCRIPTOR(-1, CPU[activeCPU].oper1, &verdescriptor,0))==1) //Load the descriptor!
			{
				if ((CPU[activeCPU].oper1 & 0xFFFC) == 0) //NULL segment selector?
				{
					goto invalidresultVERW286;
				}
				if (
					((MAX(getCPL(),getRPL(CPU[activeCPU].oper1))>GENERALSEGMENT_DPL(verdescriptor)) && (!((getLoadedTYPE(&verdescriptor)==1) && EXECSEGMENT_C(verdescriptor)))) //We are a lower privilege level with either a data/system/non-conforming segment descriptor? Conforming code segments don't check!
					)
				{
					FLAGW_ZF(0); //We're invalid!
				}
				else if (GENERALSEGMENT_S(verdescriptor)==0) //Not code/data?
				{
					FLAGW_ZF(0); //We're invalid!
				}
				else if ((EXECSEGMENT_ISEXEC(verdescriptor)==0) && DATASEGMENT_W(verdescriptor)) //Are we a writeable data segment? All others(any code segment and unwritable data segment) are unwritable!
				{
					FLAGW_ZF(1); //We're valid!
				}
				else //Either code segment or non-writable data segment?
				{
					FLAGW_ZF(0); //We're invalid!
				}
			}
			else
			{
				if (loadresult == 0)
				{
				invalidresultVERW286:
					FLAGW_ZF(0); //We're invalid!
				}
			}
			CPU_apply286cycles(); //Apply the 80286+ cycles!
		CPUPROT2
		break;
	case 6: //--- Unknown Opcode! ---
	case 7: //--- Unknown Opcode! ---
		unkOP0F_286(); //Unknown opcode!
		break;
	default:
		break;
	}
}

void CPU286_OP0F01() //Various extended 286+ instruction GRP opcode.
{
	memcpy(&CPU[activeCPU].info,&CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0],sizeof(CPU[activeCPU].info)); //Store the address for debugging!
	switch (CPU[activeCPU].thereg) //What function?
	{
	case 0: //SGDT
		debugger_setcommand("SGDT %s", CPU[activeCPU].info.text);
		if (CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0].isreg==1) //We're storing to a register? Invalid!
		{
			unkOP0F_286();
			return; //Abort!
		}

		if (unlikely(CPU[activeCPU].modrmstep==0)) //Starting?
		{
			CPU[activeCPU].modrm_addoffset = 0;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return; //Abort on fault!
			CPU[activeCPU].modrm_addoffset = 2;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return; //Abort on fault!
			CPU[activeCPU].modrm_addoffset = 4;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return; //Abort on fault!
			CPU[activeCPU].modrm_addoffset = 0;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0xA0)) return; //Abort on fault!
			CPU[activeCPU].modrm_addoffset = 2;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0xA0)) return; //Abort on fault!
			CPU[activeCPU].modrm_addoffset = 4;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0xA0)) return; //Abort on fault!
		}

		CPU[activeCPU].modrm_addoffset = 0; //Add no bytes to the offset!
		if (CPU8086_instructionstepwritemodrmw(0,CPU[activeCPU].registers->GDTR.limit, CPU[activeCPU].MODRM_src0,0)) return; //Try and write it to the address specified!
		CPUPROT1
			CPU[activeCPU].modrm_addoffset = 2; //Add 2 bytes to the offset!
			if (CPU8086_instructionstepwritemodrmw(2,(CPU[activeCPU].registers->GDTR.base & 0xFFFF), CPU[activeCPU].MODRM_src0,0)) return; //Only 24-bits of limit, high byte is cleared with 386+, set with 286!
			CPUPROT1
				CPU[activeCPU].modrm_addoffset = 4; //Add 4 bytes to the offset!
				if (CPU8086_instructionstepwritemodrmw(4,((CPU[activeCPU].registers->GDTR.base >> 16) & 0xFF)|((EMULATED_CPU==CPU_80286)?0xFF00:0x0000), CPU[activeCPU].MODRM_src0,0)) return; //Write rest value!
				CPU_apply286cycles(); //Apply the 80286+ cycles!
			CPUPROT2
		CPUPROT2
		CPU[activeCPU].modrm_addoffset = 0; //Add no bytes to the offset!
		break;
	case 1: //SIDT
		debugger_setcommand("SIDT %s", CPU[activeCPU].info.text);
		if (CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0].isreg==1) //We're storing to a register? Invalid!
		{
			unkOP0F_286();
			return; //Abort!
		}

		if (unlikely(CPU[activeCPU].modrmstep==0)) //Starting?
		{
			CPU[activeCPU].modrm_addoffset = 0;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return; //Abort on fault!
			CPU[activeCPU].modrm_addoffset = 2;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return; //Abort on fault!
			CPU[activeCPU].modrm_addoffset = 4;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return; //Abort on fault!
			CPU[activeCPU].modrm_addoffset = 0;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0xA0)) return; //Abort on fault!
			CPU[activeCPU].modrm_addoffset = 2;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0xA0)) return; //Abort on fault!
			CPU[activeCPU].modrm_addoffset = 4;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0xA0)) return; //Abort on fault!
		}

		CPU[activeCPU].modrm_addoffset = 0; //Add no bytes to the offset!
		if (CPU8086_instructionstepwritemodrmw(0,CPU[activeCPU].registers->IDTR.limit, CPU[activeCPU].MODRM_src0,0)) return; //Try and write it to the address specified!
		CPUPROT1
			CPU[activeCPU].modrm_addoffset = 2; //Add 2 bytes to the offset!
			if (CPU8086_instructionstepwritemodrmw(2,(CPU[activeCPU].registers->IDTR.base & 0xFFFF), CPU[activeCPU].MODRM_src0,0)) return; //Only 24-bits of limit, high byte is cleared with 386+, set with 286!
			CPUPROT1
				CPU[activeCPU].modrm_addoffset = 4; //Add 4 bytes to the offset!
				if (CPU8086_instructionstepwritemodrmw(4,((CPU[activeCPU].registers->IDTR.base >> 16) & 0xFF)|((EMULATED_CPU==CPU_80286)?0xFF00:0x0000), CPU[activeCPU].MODRM_src0,0)) return; //Write rest value!
				CPU_apply286cycles(); //Apply the 80286+ cycles!
			CPUPROT2
		CPUPROT2
		CPU[activeCPU].modrm_addoffset = 0; //Add no bytes to the offset!
		break;
	case 2: //LGDT
		debugger_setcommand("LGDT %s", CPU[activeCPU].info.text);
		if ((getCPL() && (getcpumode() != CPU_MODE_REAL)) || (getcpumode()==CPU_MODE_8086)) //Privilege level isn't 0 or invalid in V86 mode?
		{
			THROWDESCGP(0,0,0); //Throw #GP!
			return; //Abort!
		}
		if (CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0].isreg==1) //We're storing to a register? Invalid!
		{
			unkOP0F_286();
			return; //Abort!
		}

		if (unlikely(CPU[activeCPU].modrmstep==0)) //Starting?
		{
			CPU[activeCPU].modrm_addoffset = 0;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,1|0x40)) return; //Abort on fault!
			CPU[activeCPU].modrm_addoffset = 2;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,1|0x40)) return; //Abort on fault!
			CPU[activeCPU].modrm_addoffset = 4;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,1|0x40)) return; //Abort on fault!
			CPU[activeCPU].modrm_addoffset = 0;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,1|0xA0)) return; //Abort on fault!
			CPU[activeCPU].modrm_addoffset = 2;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,1|0xA0)) return; //Abort on fault!
			CPU[activeCPU].modrm_addoffset = 4;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,1|0xA0)) return; //Abort on fault!
		}

		CPU[activeCPU].modrm_addoffset = 0; //Add no bytes to the offset!
		if (CPU8086_instructionstepreadmodrmw(0,&CPU[activeCPU].oper1, CPU[activeCPU].MODRM_src0)) return; //Read the limit first!
		CPUPROT1
			CPU[activeCPU].modrm_addoffset = 2; //Add 2 bytes to the offset!
			if (CPU8086_instructionstepreadmodrmw(2,&CPU[activeCPU].Rdata1, CPU[activeCPU].MODRM_src0)) return; //Read the limit first!
			CPU[activeCPU].oper1d = ((uint_32)CPU[activeCPU].Rdata1); //Lower part of the limit!
			CPUPROT1
				CPU[activeCPU].modrm_addoffset = 4; //Last byte!
				if (CPU8086_instructionstepreadmodrmw(4,&CPU[activeCPU].Rdata2, CPU[activeCPU].MODRM_src0)) return; //Read the limit first!
				CPU[activeCPU].oper1d |= (((uint_32)(CPU[activeCPU].Rdata2&0xFF))<<16); //Higher part of the limit!
				CPUPROT1
					CPU[activeCPU].registers->GDTR.base = CPU[activeCPU].oper1d; //Load the base!
					CPU[activeCPU].registers->GDTR.limit = CPU[activeCPU].oper1; //Load the limit!
					CPU_apply286cycles(); //Apply the 80286+ cycles!
				CPUPROT2
			CPUPROT2
		CPUPROT2
		CPU[activeCPU].modrm_addoffset = 0; //Add no bytes to the offset!
		break;
	case 3: //LIDT
		debugger_setcommand("LIDT %s", CPU[activeCPU].info.text);
		if ((getCPL() && (getcpumode() != CPU_MODE_REAL)) || (getcpumode()==CPU_MODE_8086)) //Privilege level isn't 0 or invalid in V86-mode?
		{
			THROWDESCGP(0,0,0); //Throw #GP!
			return; //Abort!
		}
		if (CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0].isreg==1) //We're storing to a register? Invalid!
		{
			unkOP0F_286();
			return; //Abort!
		}

		if (unlikely(CPU[activeCPU].modrmstep==0)) //Starting?
		{
			CPU[activeCPU].modrm_addoffset = 0;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,1|0x40)) return; //Abort on fault!
			CPU[activeCPU].modrm_addoffset = 2;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,1|0x40)) return; //Abort on fault!
			CPU[activeCPU].modrm_addoffset = 4;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,1|0x40)) return; //Abort on fault!
			CPU[activeCPU].modrm_addoffset = 0;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,1|0xA0)) return; //Abort on fault!
			CPU[activeCPU].modrm_addoffset = 2;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,1|0xA0)) return; //Abort on fault!
			CPU[activeCPU].modrm_addoffset = 4;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,1|0xA0)) return; //Abort on fault!
		}

		CPU[activeCPU].modrm_addoffset = 0; //Add no bytes to the offset!
		if (CPU8086_instructionstepreadmodrmw(0,&CPU[activeCPU].oper1, CPU[activeCPU].MODRM_src0)) return; //Read the limit first!
		CPUPROT1
			CPU[activeCPU].modrm_addoffset = 2; //Add 2 bytes to the offset!
			if (CPU8086_instructionstepreadmodrmw(2,&CPU[activeCPU].Rdata1, CPU[activeCPU].MODRM_src0)) return; //Read the limit first!
			CPU[activeCPU].oper1d = ((uint_32)CPU[activeCPU].Rdata1); //Lower part of the limit!
			CPUPROT1
				CPU[activeCPU].modrm_addoffset = 4; //Last byte!
				if (CPU8086_instructionstepreadmodrmw(4,&CPU[activeCPU].Rdata2, CPU[activeCPU].MODRM_src0)) return; //Read the limit first!
				CPU[activeCPU].oper1d |= (((uint_32)(CPU[activeCPU].Rdata2&0xFF))<<16); //Higher part of the limit!
				CPUPROT1
					CPU[activeCPU].registers->IDTR.base = CPU[activeCPU].oper1d; //Load the base!
					CPU[activeCPU].registers->IDTR.limit = CPU[activeCPU].oper1; //Load the limit!
					CPU_apply286cycles(); //Apply the 80286+ cycles!
				CPUPROT2
			CPUPROT2
		CPUPROT2
		CPU[activeCPU].modrm_addoffset = 0; //Add no bytes to the offset!
		break;
	case 4: //SMSW
		debugger_setcommand("SMSW %s", CPU[activeCPU].info.text);
		if (unlikely(CPU[activeCPU].modrmstep == 0))
		{
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0x40)) return;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0xA0)) return;
		} //Abort on fault!
		if (CPU8086_instructionstepwritemodrmw(0,(word)(CPU[activeCPU].registers->CR0&0xFFFF), CPU[activeCPU].MODRM_src0,0)) return; //Store the MSW into the specified location!
		CPU_apply286cycles(); //Apply the 80286+ cycles!
		break;
	case 6: //LMSW
		debugger_setcommand("LMSW %s", CPU[activeCPU].info.text);
		if (getCPL() && (getcpumode() != CPU_MODE_REAL)) //Privilege level isn't 0?
		{
			THROWDESCGP(0, 0, 0); //Throw #GP!
			return; //Abort!
		}
		if (unlikely(CPU[activeCPU].modrmstep == 0))
		{
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 1|0x40)) return;
			if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 1|0xA0)) return;
		} //Abort on fault!
		if (CPU8086_instructionstepreadmodrmw(0,&CPU[activeCPU].oper1, CPU[activeCPU].MODRM_src0)) return; //Read the new register!
		CPUPROT1
		CPU[activeCPU].oper1 |= (CPU[activeCPU].registers->CR0&CR0_PE); //Keep the protected mode bit on, this isn't toggable anymore once set!
		CPU_writeCR0(CPU[activeCPU].registers->CR0,(CPU[activeCPU].registers->CR0&(~0xF))|(CPU[activeCPU].oper1&0xF)); //Set the MSW only! According to Bochs it only affects the low 4 bits!
		CPU_apply286cycles(); //Apply the 80286+ cycles!
		updateCPUmode(); //Update the CPU mode to reflect the new mode set, if required!
		CPUPROT2
		break;
	case 5: //--- Unknown Opcode!
	case 7: //--- Unknown Opcode!
		unkOP0F_286(); //Unknown opcode!
		break;
	default:
		break;
	}
}

void CPU286_OP0F02() //LAR /r
{
	byte isconforming = 1;
	SEGMENT_DESCRIPTOR verdescriptor;
	sbyte loadresult;
	if (getcpumode() != CPU_MODE_PROTECTED)
	{
		unkOP0F_286(); //We're not recognized in real mode!
		return;
	}
	modrm_generateInstructionTEXT("LAR", 16, 0, PARAM_MODRM12); //Our instruction text!
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0xA0)) return;
	} //Abort on fault!
	if (CPU8086_instructionstepreadmodrmw(0,&CPU[activeCPU].oper1, CPU[activeCPU].MODRM_src1)) return; //Read the segment to check!
	CPUPROT1
		if ((loadresult = LOADDESCRIPTOR(-1, CPU[activeCPU].oper1, &verdescriptor,0))==1) //Load the descriptor!
		{
			if ((CPU[activeCPU].oper1 & 0xFFFC) == 0) //NULL segment selector?
			{
				goto invalidresultLAR286;
			}
			switch (GENERALSEGMENT_TYPE(verdescriptor))
			{
			default://Other system types are invalid!
				if (GENERALSEGMENT_S(verdescriptor) == 0) //System?
				{
					FLAGW_ZF(0); //Invalid descriptor type!
					break;
				}
			case AVL_SYSTEM_TSS16BIT:
			case AVL_SYSTEM_LDT:
			case AVL_SYSTEM_BUSY_TSS16BIT:
			case AVL_SYSTEM_CALLGATE16BIT:
			case AVL_SYSTEM_TASKGATE:
			case AVL_SYSTEM_TSS32BIT:
			case AVL_SYSTEM_BUSY_TSS32BIT:
			case AVL_SYSTEM_CALLGATE32BIT:
			//All non-system types are valid!
				if (GENERALSEGMENT_S(verdescriptor)) //Non-System?
				{
					switch (GENERALSEGMENT_TYPE(verdescriptor)) //What type?
					{
					case AVL_CODE_EXECUTEONLY_CONFORMING:
					case AVL_CODE_EXECUTEONLY_CONFORMING_ACCESSED:
					case AVL_CODE_EXECUTE_READONLY_CONFORMING:
					case AVL_CODE_EXECUTE_READONLY_CONFORMING_ACCESSED: //Conforming?
						isconforming = 1;
						break;
					default: //Not conforming?
						isconforming = 0;
						break;
					}
				}
				else
				{
					isconforming = 0; //Not conforming!
				}
				if ((MAX(getCPL(), getRPL(CPU[activeCPU].oper1)) <= GENERALSEGMENT_DPL(verdescriptor)) || isconforming) //Valid privilege?
				{
					if (unlikely(CPU[activeCPU].modrmstep == 2))
					{
						if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0x40)) return;
						if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0xA0)) return;
					} //Abort on fault!
					if (CPU8086_instructionstepwritemodrmw(2,(word)(verdescriptor.desc.AccessRights<<8), CPU[activeCPU].MODRM_src0,0)) return; //Write our result!
					CPUPROT1
						FLAGW_ZF(1); //We're valid!
					CPUPROT2
				}
				else
				{
					FLAGW_ZF(0); //Not valid!
				}
				break;
			}
		}
		else //Couldn't be loaded?
		{
			if (loadresult == 0)
			{
				invalidresultLAR286:
				FLAGW_ZF(0); //Default: not loaded!
			}
		}
		CPU_apply286cycles(); //Apply the 80286+ cycles!
	CPUPROT2
}

void CPU286_OP0F03() //LSL /r
{
	uint_64 limit;
	byte isconforming = 1;
	SEGMENT_DESCRIPTOR verdescriptor;
	sbyte loadresult;
	if (getcpumode() != CPU_MODE_PROTECTED)
	{
		unkOP0F_286(); //We're not recognized in real mode!
		return;
	}
	modrm_generateInstructionTEXT("LSL", 16, 0, PARAM_MODRM12); //Our instruction text!
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0xA0)) return;
	} //Abort on fault!
	if (CPU8086_instructionstepreadmodrmw(0,&CPU[activeCPU].oper1, CPU[activeCPU].MODRM_src1)) return; //Read the segment to check!
	CPUPROT1
		if ((loadresult = LOADDESCRIPTOR(-1, CPU[activeCPU].oper1, &verdescriptor,0))==1) //Load the descriptor!
		{
			if ((CPU[activeCPU].oper1 & 0xFFFC) == 0) //NULL segment selector?
			{
				goto invalidresultLSL286;
			}
			CPU[activeCPU].protection_PortRightsLookedup = (SEGDESC_NONCALLGATE_G(verdescriptor)&CPU[activeCPU].G_Mask); //What granularity are we?
			switch (GENERALSEGMENT_TYPE(verdescriptor))
			{
			default: //Other system types are invalid!
				if (GENERALSEGMENT_S(verdescriptor) == 0) //System?
				{
					FLAGW_ZF(0); //Invalid descriptor type!
					break;
				}
			//Only system valid types are TSS(16/32-bit) and LDT!
			case AVL_SYSTEM_TSS16BIT:
			case AVL_SYSTEM_LDT:
			case AVL_SYSTEM_BUSY_TSS16BIT:
			case AVL_SYSTEM_TSS32BIT:
			case AVL_SYSTEM_BUSY_TSS32BIT:
			//All non-system types are valid!
				if (GENERALSEGMENT_S(verdescriptor)) //Non-System?
				{
					switch (GENERALSEGMENT_TYPE(verdescriptor)) //What type?
					{
					case AVL_CODE_EXECUTEONLY_CONFORMING:
					case AVL_CODE_EXECUTEONLY_CONFORMING_ACCESSED:
					case AVL_CODE_EXECUTE_READONLY_CONFORMING:
					case AVL_CODE_EXECUTE_READONLY_CONFORMING_ACCESSED: //Conforming?
						isconforming = 1;
						break;
					default: //Not conforming?
						isconforming = 0;
						break;
					}
				}
				else
				{
					isconforming = 0; //Not conforming!
				}

				limit = verdescriptor.PRECALCS.limit; //The limit to apply!

				if ((MAX(getCPL(), getRPL(CPU[activeCPU].oper1)) <= GENERALSEGMENT_DPL(verdescriptor)) || isconforming) //Valid privilege?
				{
					if (unlikely(CPU[activeCPU].modrmstep == 2))
					{
						if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0x40)) return;
						if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0|0xA0)) return;
					} //Abort on fault!
					if (CPU8086_instructionstepwritemodrmw(2,(word)(limit&0xFFFF), CPU[activeCPU].MODRM_src0,0)) return; //Write our result!
					CPUPROT1
						FLAGW_ZF(1); //We're valid!
					CPUPROT2
				}
				else
				{
					FLAGW_ZF(0); //Not valid!
				}
				break;
			}
		}
		else //Couldn't be loaded?
		{
			if (loadresult == 0)
			{
				invalidresultLSL286:
				FLAGW_ZF(0); //Default: not loaded!
			}
		}
		CPU_apply286cycles(); //Apply the 80286+ cycles!
	CPUPROT2
}

void CPU286_LOADALL_LoadDescriptor(DESCRIPTORCACHE286 *source, sword segment)
{
	CPU[activeCPU].SEG_DESCRIPTOR[segment].desc.limit_low = source->limit;
	CPU[activeCPU].SEG_DESCRIPTOR[segment].desc.noncallgate_info &= ~0xF; //No high limit!
	CPU[activeCPU].SEG_DESCRIPTOR[segment].desc.base_low = source->baselow;
	CPU[activeCPU].SEG_DESCRIPTOR[segment].desc.base_mid = (source->basehighaccessrights&0xFF); //Mid is High base in the descriptor(286 only)!
	CPU[activeCPU].SEG_DESCRIPTOR[segment].desc.base_high = 0; //Only 24 bits are used for the base!
	CPU[activeCPU].SEG_DESCRIPTOR[segment].desc.callgate_base_mid = 0; //Not used!
	CPU[activeCPU].SEG_DESCRIPTOR[segment].desc.AccessRights = (source->basehighaccessrights>>8); //Access rights is completely used. Present being 0 makes the register unfit to read (#GP is fired).
	CPU_calcSegmentPrecalcs((segment==CPU_SEGMENT_CS)?1:0,&CPU[activeCPU].SEG_DESCRIPTOR[segment]); //Calculate the precalcs!
}

void CPU286_OP0F05() //Undocumented LOADALL instruction
{
	if (CPU[activeCPU].instructionstep==0) //First step? Start Request!
	{
		if (getCPL() && (getcpumode()!=CPU_MODE_REAL)) //We're protected by CPL!
		{
			unkOP0F_286(); //Raise an error!
			return;
		}
		memset(&CPU[activeCPU].LOADALL286DATA,0,sizeof(CPU[activeCPU].LOADALL286DATA)); //Init the structure to be used as a buffer!
		++CPU[activeCPU].instructionstep; //Finished check!
	}

	//Load the data from the used location!

	word readindex; //Our read index for all reads that are required!
	readindex = 0; //Init read index to read all data in time through the BIU!

	for (readindex=0;readindex<NUMITEMS(CPU[activeCPU].LOADALL286DATA.dataw);++readindex) //Load all registers in the correct format!
	{
		if (CPU8086_instructionstepreaddirectw((byte)(readindex<<1),-1,0,(0x800|(readindex<<1)),&CPU[activeCPU].loadall286loader[readindex],0)) return; //Access memory directly through the BIU! Read the data to load from memory! Take care of any conversion needed!
	}
	for (readindex = 0; readindex < MIN(NUMITEMS(CPU[activeCPU].LOADALL286DATA.dataw),NUMITEMS(CPU[activeCPU].loadall286loader)); ++readindex) //Load all registers in the correct format!
	{
		CPU[activeCPU].LOADALL286DATA.dataw[readindex] = CPU[activeCPU].loadall286loader[readindex]; //Access memory directly through the BIU! Read the data to load from memory! Take care of any conversion needed!
	}

	//Load all registers and caches, ignore any protection normally done(not checked during LOADALL)!
	//Plain registers!
	CPU[activeCPU].registers->CR0 = CPU[activeCPU].LOADALL286DATA.fields.MSW|(CPU[activeCPU].registers->CR0&CR0_PE); //MSW! We cannot reenter real mode by clearing bit 0(Protection Enable bit)!
	REG_TR = CPU[activeCPU].LOADALL286DATA.fields.TR; //TR
	REG_FLAGS = CPU[activeCPU].LOADALL286DATA.fields.flags; //FLAGS
	REG_EIP = (uint_32)CPU[activeCPU].LOADALL286DATA.fields.IP; //IP
	REG_LDTR = CPU[activeCPU].LOADALL286DATA.fields.LDT; //LDT
	REG_DS = CPU[activeCPU].LOADALL286DATA.fields.DS; //DS
	REG_SS = CPU[activeCPU].LOADALL286DATA.fields.SS; //SS
	REG_CS = CPU[activeCPU].LOADALL286DATA.fields.CS; //CS
	REG_ES = CPU[activeCPU].LOADALL286DATA.fields.ES; //ES
	REG_DI = CPU[activeCPU].LOADALL286DATA.fields.DI; //DI
	REG_SI = CPU[activeCPU].LOADALL286DATA.fields.SI; //SI
	REG_BP = CPU[activeCPU].LOADALL286DATA.fields.BP; //BP
	REG_SP = CPU[activeCPU].LOADALL286DATA.fields.SP; //SP
	REG_BX = CPU[activeCPU].LOADALL286DATA.fields.BX; //BX
	REG_DX = CPU[activeCPU].LOADALL286DATA.fields.DX; //DX
	REG_CX = CPU[activeCPU].LOADALL286DATA.fields.CX; //CX
	REG_AX = CPU[activeCPU].LOADALL286DATA.fields.AX; //AX
	updateCPUmode(); //We're updating the CPU mode if needed, since we're reloading CR0 and FLAGS!

	//GDTR/IDTR registers!
	CPU[activeCPU].registers->GDTR.base = ((CPU[activeCPU].LOADALL286DATA.fields.GDTR.basehigh&0xFF)<<16)| CPU[activeCPU].LOADALL286DATA.fields.GDTR.baselow; //Base!
	CPU[activeCPU].registers->GDTR.limit = CPU[activeCPU].LOADALL286DATA.fields.GDTR.limit; //Limit
	CPU[activeCPU].registers->IDTR.base = ((CPU[activeCPU].LOADALL286DATA.fields.IDTR.basehigh&0xFF)<<16)| CPU[activeCPU].LOADALL286DATA.fields.IDTR.baselow; //Base!
	CPU[activeCPU].registers->IDTR.limit = CPU[activeCPU].LOADALL286DATA.fields.IDTR.limit; //Limit

	//Load all descriptors directly without checks!
	CPU286_LOADALL_LoadDescriptor(&CPU[activeCPU].LOADALL286DATA.fields.ESdescriptor,CPU_SEGMENT_ES); //ES descriptor!
	CPU286_LOADALL_LoadDescriptor(&CPU[activeCPU].LOADALL286DATA.fields.CSdescriptor,CPU_SEGMENT_CS); //CS descriptor!
	CPU286_LOADALL_LoadDescriptor(&CPU[activeCPU].LOADALL286DATA.fields.SSdescriptor,CPU_SEGMENT_SS); //SS descriptor!
	CPU286_LOADALL_LoadDescriptor(&CPU[activeCPU].LOADALL286DATA.fields.DSdescriptor,CPU_SEGMENT_DS); //DS descriptor!
	CPU286_LOADALL_LoadDescriptor(&CPU[activeCPU].LOADALL286DATA.fields.LDTdescriptor,CPU_SEGMENT_LDTR); //LDT descriptor!
	CPU286_LOADALL_LoadDescriptor(&CPU[activeCPU].LOADALL286DATA.fields.TSSdescriptor,CPU_SEGMENT_TR); //TSS descriptor!
	CPU[activeCPU].CPL = GENERALSEGMENT_DPL(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_SS]); //DPL determines CPL!
	CPU_apply286cycles(); //Apply the 80286+ cycles!
	CPU_flushPIQ(-1); //We're jumping to another address!
}

void CPU286_OP0F06() //CLTS
{
	debugger_setcommand("CLTS"); //Our instruction text!
	if (getCPL() && (getcpumode() != CPU_MODE_REAL)) //Privilege level isn't 0?
	{
		THROWDESCGP(0,0,0); //Throw #GP!
		return; //Abort!
	}
	CPU[activeCPU].registers->CR0 &= ~CR0_TS; //Clear the Task Switched flag!
	CPU_apply286cycles(); //Apply the 80286+ cycles!
}

void CPU286_OP0F0B() //#UD instruction
{
	unkOP0F_286(); //Deliberately #UD!
	CPU_apply286cycles(); //Apply the 80286+ cycles!
}

void CPU286_OP0FB9() //#UD instruction
{
	unkOP0F_286(); //Deliberately #UD!
	CPU_apply286cycles(); //Apply the 80286+ cycles!
}

extern byte advancedlog; //Advanced log setting

extern byte MMU_logging; //Are we logging from the MMU?

void CPU286_OPF1() //Undefined opcode, Don't throw any exception!
{
	debugger_setcommand("INT1");
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger","#DB fault(-1)!");
	}

	if (CPU_faultraised(EXCEPTION_DEBUG))
	{
		if (EMULATED_CPU >= CPU_80386) FLAGW_RF(1); //Automatically set the resume flag on a debugger fault!
		CPU_executionphase_startinterrupt(EXCEPTION_DEBUG, 0, -6); //ICEBP! Don't ever fault on CPL (thus -6)!
	}
}

//FPU non-existant Coprocessor support!

void FPU80287_FPU_UD(byte isESC)
{ //Generic x86 FPU #UD opcode decoder!
	//MP needs to be set for TS to have effect during WAIT(throw emulation). It's always in effect with ESC instructions(ignoring MP). EM only has effect on ESC instructions(throw emulation if set).
	if (((CPU[activeCPU].registers->CR0&CR0_EM)&&(isESC)) || (((CPU[activeCPU].registers->CR0&CR0_MP)||isESC) && (CPU[activeCPU].registers->CR0&CR0_TS))) //To be emulated or task switched?
	{
		debugger_setcommand("<FPU EMULATION>");
		CPU_resetOP();
		THROWDESCNM(); //Only on 286+!
	}
	else //Normal execution?
	{
		debugger_setcommand("<No COprocessor OPcodes implemented!>");
		if (CPU_apply286cycles()==0) //No 286+? Apply the 80286+ cycles!
		{
			CPU[activeCPU].cycles_OP = MODRM_EA(CPU[activeCPU].params) ? 8 : 2; //No hardware interrupt to use anymore!
		}
	}
}

void FPU80287_OPDBE3()
{
	debugger_setcommand("<FPU #UD: FNINIT>");
}
void FPU80287_OPDFE0()
{
	debugger_setcommand("<FPU #UD: FSTSW AX>");
}
void FPU80287_OPDDslash7()
{
	debugger_setcommand("<FPU #UD: FNSTSW>");
}
void FPU80287_OPD9slash7()
{
	debugger_setcommand("<FPU #UD: FNSTCW>");
}

void FPU80287_OP9B()
{
	modrm_generateInstructionTEXT("<FPU #UD: FWAIT>",0,0,PARAM_NONE);
	FPU80287_FPU_UD(0); /* Handle emulation etc. */
	/*9B: WAIT : wait for TEST pin activity. (Edit: continue on interrupts or 8087+!!!)*/
}
void FPU80287_OPDB()
{
	FPU80287_FPU_UD(1); /* Handle emulation etc. */
	if (CPU[activeCPU].params.modrm==0xE3)
	{
		FPU80287_OPDBE3();
		/* Special naming! */
	}
}
void FPU80287_OPDF()
{
	FPU80287_FPU_UD(1); /* Handle emulation etc. */
	if (CPU[activeCPU].params.modrm==0xE0)
	{
		FPU80287_OPDFE0(); /* Special naming! */
	}
}
void FPU80287_OPDD()
{
	FPU80287_FPU_UD(1); /* Handle emulation etc. */
	if (CPU[activeCPU].thereg==7)
	{
		FPU80287_OPDDslash7(); /* Special naming! */
	}
}
void FPU80287_OPD9()
{
	FPU80287_FPU_UD(1); /* Handle emulation etc. */
	if (CPU[activeCPU].thereg==7)
	{
		FPU80287_OPD9slash7(); /* Special naming! */
	}
}

void FPU80287_noCOOP()
{
	//Generic x86 FPU opcode decoder!
	FPU80287_FPU_UD(1); //Generic #UD for FPU!
}
