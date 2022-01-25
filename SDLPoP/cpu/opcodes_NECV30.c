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

#include "headers/types.h" //Basic types!
#include "headers/cpu/cpu.h" //Basic CPU!
#include "headers/cpu/easyregs.h" //Easy registers!
#include "headers/cpu/modrm.h" //MODR/M compatibility!
#include "headers/support/signedness.h" //CPU support functions!
#include "headers/hardware/ports.h" //Ports compatibility!
#include "headers/emu/debugger/debugger.h" //Debug compatibility!
#include "headers/cpu/cpu_OP8086.h" //8086 function specific compatibility!
#include "headers/cpu/8086_grpOPs.h" //GRP Opcode support (C0&C1 Opcodes!)
#include "headers/cpu/cpu_OPNECV30.h" //NECV30 function specific compatibility!
#include "headers/cpu/protection.h" //Protection support!
#include "headers/cpu/flags.h" //Flags support!
#include "headers/cpu/cpu_pmtimings.h" //Timing support!
#include "headers/cpu/cpu_stack.h" //Stack support!


/*

New instructions:

ENTER
LEAVE
PUSHA
POPA
BOUND
IMUL
INS
OUTS

*/

//We're still 16-bits!

//Info: Gv,Ev=See 8086 opcode map; Ib/w=Immediate, Iz=Iw/Idw.

//Help functions for debugging:
OPTINLINE byte CPU186_internal_MOV16(word *dest, word val) //Copy of 8086 version!
{
	CPUPROT1
		if (dest) //Register?
		{
			CPU[activeCPU].destEIP = REG_EIP; //Store (E)IP for safety!
			modrm_updatedsegment(dest, val, 0); //Check for an updated segment!
			CPUPROT1
			if (get_segment_index(dest)==-1) //Valid to write directly?
			{
				*dest = val; //Write directly, if not errored out!
			}
			CPU_apply286cycles(); //Apply the 80286+ cycles!
			CPUPROT2
		}
		else //Memory?
		{
			if (CPU[activeCPU].custommem)
			{
				if (checkMMUaccess16(CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].customoffset&CPU[activeCPU].address_size),0|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0|0x8)) return 1; //Abort on fault!
				if (checkMMUaccess16(CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].customoffset&CPU[activeCPU].address_size), 0|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0 | 0x8)) return 1; //Abort on fault!
				if (CPU8086_internal_stepwritedirectw(0,CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), CPU[activeCPU].customoffset, val,!CPU[activeCPU].CPU_Address_size)) return 1; //Write to memory directly!
				CPU_apply286cycles(); //Apply the 80286+ cycles!
			}
			else //ModR/M?
			{
				if (CPU8086_internal_stepwritemodrmw(0,val, CPU[activeCPU].MODRM_src0,0)) return 1; //Write the result to memory!
				CPU_apply286cycles(); //Apply the 80286+ cycles!
			}
		}
	CPUPROT2
		return 0; //OK!
}

//BCD opcodes!
OPTINLINE void CPU186_internal_AAA()
{
	CPUPROT1
	FLAGW_SF((REG_AL>=0x7A)&&(REG_AL<=0xF9)); //According to IBMulator
	if ((REG_AL&0xF)>9)
	{
		FLAGW_OF(((REG_AL&0xF0)==0x70)?1:0); //According to IBMulator
		REG_AX += 0x0106;
		FLAGW_AF(1);
		FLAGW_CF(1);
		FLAGW_ZF((REG_AL==0)?1:0);
	}
	else if (FLAG_AF) //Special case according to IBMulator?
	{
		REG_AX += 0x0106;
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
	}
	flag_p8(REG_AL); //Parity is affected!
	REG_AL &= 0xF;
	//z=s=p=o=?
	CPUPROT2
	if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
	{
		CPU[activeCPU].cycles_OP += 4; //Timings!
	}
}
OPTINLINE void CPU186_internal_AAS()
{
	CPUPROT1
	if (((REG_AL&0xF)>9))
	{
		FLAGW_SF((REG_AL>0x85)?1:0); //According to IBMulator!
		REG_AX -= 0x0106;
		FLAGW_AF(1);
		FLAGW_CF(1);
		FLAGW_OF(0); //According to IBMulator!
	}
	else if (FLAG_AF)
	{
		FLAGW_OF(((REG_AL>=0x80) && (REG_AL<=0x85))?1:0); //According to IBMulator!
		FLAGW_SF(((REG_AL < 0x06) || (REG_AL > 0x85))?1:0); //According to IBMulator!
		REG_AX -= 0x0106;
		FLAGW_AF(1);
		FLAGW_CF(1);
	}
	else
	{
		FLAGW_SF((REG_AL>=0x80)?1:0); //According to IBMulator!
		FLAGW_AF(0);
		FLAGW_CF(0);
		FLAGW_OF(0); //According to IBMulator!
	}
	flag_p8(REG_AL); //Parity is affected!
	FLAGW_ZF((REG_AL==0)?1:0); //Zero is affected!
	REG_AL &= 0xF;
	//z=s=o=p=?
	CPUPROT2
	if (CPU_apply286cycles()==0) //No 80286+ cycles instead?
	{
		CPU[activeCPU].cycles_OP += 4; //Timings!
	}
}

//Newer versions of the BCD instructions!
void CPU186_OP37()
{
	modrm_generateInstructionTEXT("AAA",0,0,PARAM_NONE);/*AAA?*/
	CPU186_internal_AAA();/*AAA?*/
}
void CPU186_OP3F()
{
	modrm_generateInstructionTEXT("AAS",0,0,PARAM_NONE);/*AAS?*/
	CPU186_internal_AAS();/*AAS?*/
}

/*

New opcodes for 80186+!

*/

void CPU186_OP60()
{
	debugger_setcommand("PUSHA");
	if (unlikely(CPU[activeCPU].stackchecked == 0))
	{
		if (checkStackAccess(8, 1, 0)) return; /*Abort on fault!*/
		++CPU[activeCPU].stackchecked;
	}
	CPU[activeCPU].PUSHA_oldSP = (word)CPU[activeCPU].oldESP;    //PUSHA
	if (CPU8086_PUSHw(0,&REG_AX,0)) return;
	CPUPROT1
	if (CPU8086_PUSHw(2,&REG_CX,0)) return;
	CPUPROT1
	if (CPU8086_PUSHw(4,&REG_DX,0)) return;
	CPUPROT1
	if (CPU8086_PUSHw(6,&REG_BX,0)) return;
	CPUPROT1
	if (CPU8086_PUSHw(8,&CPU[activeCPU].PUSHA_oldSP,0)) return;
	CPUPROT1
	if (CPU8086_PUSHw(10,&REG_BP,0)) return;
	CPUPROT1
	if (CPU8086_PUSHw(12,&REG_SI,0)) return;
	CPUPROT1
	if (CPU8086_PUSHw(14,&REG_DI,0)) return;
	CPUPROT2
	CPUPROT2
	CPUPROT2
	CPUPROT2
	CPUPROT2
	CPUPROT2
	CPUPROT2
	CPU_apply286cycles(); //Apply the 80286+ cycles!
}

void CPU186_OP61()
{
	word dummy;
	debugger_setcommand("POPA");
	if (unlikely(CPU[activeCPU].stackchecked == 0))
	{
		if (checkStackAccess(8, 0, 0)) return; /*Abort on fault!*/
		++CPU[activeCPU].stackchecked;
	}
	if (CPU8086_POPw(0,&REG_DI,0)) return;
	CPUPROT1
	if (CPU8086_POPw(2,&REG_SI,0)) return;
	CPUPROT1
	if (CPU8086_POPw(4,&REG_BP,0)) return;
	CPUPROT1
	if (CPU8086_POPw(6,&dummy,0)) return;
	CPUPROT1
	if (CPU8086_POPw(8,&REG_BX,0)) return;
	CPUPROT1
	if (CPU8086_POPw(10,&REG_DX,0)) return;
	CPUPROT1
	if (CPU8086_POPw(12,&REG_CX,0)) return;
	CPUPROT1
	if (CPU8086_POPw(14,&REG_AX,0)) return;
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
void CPU186_OP62()
{
	modrm_debugger16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, CPU[activeCPU].MODRM_src1); //Debug the location!
	debugger_setcommand("BOUND %s,%s", CPU[activeCPU].modrm_param1, CPU[activeCPU].modrm_param2); //Opcode!

	if (modrm_isregister(CPU[activeCPU].params)) //ModR/M may only be referencing memory?
	{
		unkOP_186(); //Raise #UD!
		return; //Abort!
	}

	if (unlikely(CPU[activeCPU].modrmstep==0))
	{
		CPU[activeCPU].modrm_addoffset = 0; //No offset!
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,1|0x40)) return; //Abort on fault!
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1,1|0x40)) return; //Abort on fault!
		CPU[activeCPU].modrm_addoffset = 2; //Max offset!
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1,1|0x40)) return; //Abort on fault!
		CPU[activeCPU].modrm_addoffset = 0; //No offset!
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,1|0xA0)) return; //Abort on fault!
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1,1|0xA0)) return; //Abort on fault!
		CPU[activeCPU].modrm_addoffset = 2; //Max offset!
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1,1|0xA0)) return; //Abort on fault!
	}

	CPU[activeCPU].modrm_addoffset = 0; //No offset!
	if (CPU8086_instructionstepreadmodrmw(0,&CPU[activeCPU].boundval16, CPU[activeCPU].MODRM_src0)) return; //Read index!
	if (CPU8086_instructionstepreadmodrmw(2,&CPU[activeCPU].bound_min16, CPU[activeCPU].MODRM_src1)) return; //Read min!
	CPU[activeCPU].modrm_addoffset = 2; //Max offset!
	if (CPU8086_instructionstepreadmodrmw(4,&CPU[activeCPU].bound_max16, CPU[activeCPU].MODRM_src1)) return; //Read max!
	CPU[activeCPU].modrm_addoffset = 0; //Reset offset!
	if ((unsigned2signed16(CPU[activeCPU].boundval16)<unsigned2signed16(CPU[activeCPU].bound_min16)) || (unsigned2signed16(CPU[activeCPU].boundval16)>unsigned2signed16(CPU[activeCPU].bound_max16)))
	{
		//BOUND Gv,Ma
		CPU_BoundException(); //Execute bound exception!
	}
	else //No exception?
	{
		CPU_apply286cycles(); //Apply the 80286+ cycles!
	}
}

void CPU186_OP68()
{
	word val = CPU[activeCPU].immw;    //PUSH Iz
	debugger_setcommand("PUSH %04X",val);
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,1,0)) return;
		++CPU[activeCPU].stackchecked;
	} //Abort on fault!
	if (CPU8086_PUSHw(0,&val,0)) return; //PUSH!
	CPU_apply286cycles(); //Apply the 80286+ cycles!
}

void CPU186_OP69()
{
	memcpy(&CPU[activeCPU].info,&CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0],sizeof(CPU[activeCPU].info)); //Reg!
	memcpy(&CPU[activeCPU].info2,&CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src1],sizeof(CPU[activeCPU].info2)); //Second parameter(R/M)!
	if ((MODRM_MOD(CPU[activeCPU].params.modrm)==3) && (CPU[activeCPU].info.reg16== CPU[activeCPU].info2.reg16)) //Two-operand version?
	{
		debugger_setcommand("IMUL %s,%04X", CPU[activeCPU].info.text, CPU[activeCPU].immw); //IMUL reg,imm16
	}
	else //Three-operand version?
	{
		debugger_setcommand("IMUL %s,%s,%04X", CPU[activeCPU].info.text, CPU[activeCPU].info2.text, CPU[activeCPU].immw); //IMUL reg,r/m16,imm16
	}
	if (unlikely(CPU[activeCPU].instructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].modrmstep==0))
		{
			if (modrm_check16(&CPU[activeCPU].params,1,1|0x40)) return; //Abort on fault!
			if (modrm_check16(&CPU[activeCPU].params,1,1|0xA0)) return; //Abort on fault!
		}
		if (CPU8086_instructionstepreadmodrmw(0,&CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src1)) return; //Read R/M!
		CPU[activeCPU].temp1.val16high = 0; //Clear high part by default!
		++CPU[activeCPU].instructionstep; //Next step!
	}
	if (CPU[activeCPU].instructionstep==1) //Second step?
	{
		CPU_CIMUL(CPU[activeCPU].instructionbufferw,16, CPU[activeCPU].immw,16,&CPU[activeCPU].IMULresult,16); //Immediate word is second/third parameter!
		CPU_apply286cycles(); //Apply the 80286+ cycles!
		//We're writing to the register always, so no normal writeback!
		++CPU[activeCPU].instructionstep; //Next step!
	}
	modrm_write16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,(CPU[activeCPU].IMULresult&0xFFFF),0); //Write to the destination(register)!
}

void CPU186_OP6A()
{
	word val = (word)CPU[activeCPU].immb; //Read the value!
	if (val&0x80) val |= 0xFF00; //Sign-extend!
	debugger_setcommand("PUSH %02X",(val&0xFF)); //PUSH this!
	if (unlikely(CPU[activeCPU].stackchecked==0))
	{
		if (checkStackAccess(1,1,0)) return;
		++CPU[activeCPU].stackchecked;
	} //Abort on fault!
	if (CPU8086_PUSHw(0,&val,0)) return;    //PUSH Ib
	CPU_apply286cycles(); //Apply the 80286+ cycles!
}

void CPU186_OP6B()
{
	memcpy(&CPU[activeCPU].info,&CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0],sizeof(CPU[activeCPU].info)); //Reg!
	memcpy(&CPU[activeCPU].info2,&CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src1],sizeof(CPU[activeCPU].info2)); //Second parameter(R/M)!
	if ((MODRM_MOD(CPU[activeCPU].params.modrm)==3) && (CPU[activeCPU].info.reg16== CPU[activeCPU].info2.reg16)) //Two-operand version?
	{
		debugger_setcommand("IMUL %s,%02X", CPU[activeCPU].info.text, CPU[activeCPU].immb); //IMUL reg,imm8
	}
	else //Three-operand version?
	{
		debugger_setcommand("IMUL %s,%s,%02X", CPU[activeCPU].info.text, CPU[activeCPU].info2.text, CPU[activeCPU].immb); //IMUL reg,r/m16,imm8
	}

	if (unlikely(CPU[activeCPU].instructionstep==0)) //First step?
	{
		if (unlikely(CPU[activeCPU].modrmstep==0))
		{
			if (modrm_check16(&CPU[activeCPU].params,1,1|0x40)) return; //Abort on fault!
			if (modrm_check16(&CPU[activeCPU].params,1,1|0xA0)) return; //Abort on fault!
		}
		if (CPU8086_instructionstepreadmodrmw(0,&CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src1)) return; //Read R/M!
		CPU[activeCPU].temp1.val16high = 0; //Clear high part by default!
		++CPU[activeCPU].instructionstep; //Next step!
	}
	if (CPU[activeCPU].instructionstep==1) //Second step?
	{
		CPU_CIMUL(CPU[activeCPU].instructionbufferw,16, CPU[activeCPU].immb,8,&CPU[activeCPU].IMULresult,16); //Immediate word is second/third parameter!
		CPU_apply286cycles(); //Apply the 80286+ cycles!
		//We're writing to the register always, so no normal writeback!
		++CPU[activeCPU].instructionstep; //Next step!
	}

	modrm_write16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,(CPU[activeCPU].IMULresult&0xFFFF),0); //Write to register!
}

void CPU186_OP6C()
{
	debugger_setcommand("INSB");
	if (CPU[activeCPU].blockREP) return; //Disabled REP!
	if (unlikely(CPU[activeCPU].internalinstructionstep==0)) if (checkMMUaccess(CPU_SEGMENT_ES,REG_ES,(CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI),0,getCPL(),!CPU[activeCPU].CPU_Address_size,0)) return; //Abort on fault!
	if (CPU_PORT_IN_B(0,REG_DX,&CPU[activeCPU].data8)) return; //Read the port!
	CPUPROT1
	if (CPU8086_instructionstepwritedirectb(0,CPU_SEGMENT_ES,REG_ES,(CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI), CPU[activeCPU].data8,!CPU[activeCPU].CPU_Address_size)) return; //INSB
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
	CPU_apply286cycles(); //Apply the 80286+ cycles!
	CPUPROT2
	CPUPROT2
}

void CPU186_OP6D()
{
	debugger_setcommand("INSW");
	if (CPU[activeCPU].blockREP) return; //Disabled REP!
	if (unlikely(CPU[activeCPU].internalinstructionstep==0))
	{
		if (checkMMUaccess16(CPU_SEGMENT_ES,REG_ES,(CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI),0|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0|0x8)) return; //Abort on fault!
		if (checkMMUaccess16(CPU_SEGMENT_ES,REG_ES, (CPU[activeCPU].CPU_Address_size ? REG_EDI : REG_DI), 0|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0 | 0x8)) return; //Abort on fault!
	}
	if (CPU_PORT_IN_W(0,REG_DX, &CPU[activeCPU].data16)) return; //Read the port!
	CPUPROT1
	if (CPU8086_instructionstepwritedirectw(0,CPU_SEGMENT_ES,REG_ES,(CPU[activeCPU].CPU_Address_size?REG_EDI:REG_DI), CPU[activeCPU].data16,!CPU[activeCPU].CPU_Address_size)) return; //INSW
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
	CPU_apply286cycles(); //Apply the 80286+ cycles!
	CPUPROT2
	CPUPROT2
}

void CPU186_OP6E()
{
	debugger_setcommand("OUTSB");
	if (CPU[activeCPU].blockREP) return; //Disabled REP!
	if (unlikely(CPU[activeCPU].modrmstep==0)) if (checkMMUaccess(CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),(CPU[activeCPU].CPU_Address_size?REG_ESI:REG_SI),1,getCPL(),!CPU[activeCPU].CPU_Address_size,0)) return; //Abort on fault!
	if (CPU8086_instructionstepreaddirectb(0,CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),(CPU[activeCPU].CPU_Address_size?REG_ESI:REG_SI),&CPU[activeCPU].data8,!CPU[activeCPU].CPU_Address_size)) return; //OUTSB
	CPUPROT1
	if (CPU_PORT_OUT_B(0,REG_DX, CPU[activeCPU].data8)) return; //OUTS DX,Xb
	CPUPROT1
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
	CPU_apply286cycles(); //Apply the 80286+ cycles!
	CPUPROT2
	CPUPROT2
}

void CPU186_OP6F()
{
	debugger_setcommand("OUTSW");
	if (CPU[activeCPU].blockREP) return; //Disabled REP!
	if (unlikely(CPU[activeCPU].modrmstep==0))
	{
		if (checkMMUaccess16(CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),(CPU[activeCPU].CPU_Address_size?REG_ESI:REG_SI),1|0x40,getCPL(),!CPU[activeCPU].CPU_Address_size,0|0x8)) return; //Abort on fault!
		if (checkMMUaccess16(CPU_segment_index(CPU_SEGMENT_DS), CPU_segment(CPU_SEGMENT_DS), (CPU[activeCPU].CPU_Address_size ? REG_ESI : REG_SI), 1|0xA0, getCPL(), !CPU[activeCPU].CPU_Address_size, 0 | 0x8)) return; //Abort on fault!
	}
	if (CPU8086_instructionstepreaddirectw(0,CPU_segment_index(CPU_SEGMENT_DS),CPU_segment(CPU_SEGMENT_DS),(CPU[activeCPU].CPU_Address_size?REG_ESI:REG_SI),&CPU[activeCPU].data16,!CPU[activeCPU].CPU_Address_size)) return; //OUTSW
	CPUPROT1
	if (CPU_PORT_OUT_W(0,REG_DX, CPU[activeCPU].data16)) return;    //OUTS DX,Xz
	CPUPROT1
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
	CPU_apply286cycles(); //Apply the 80286+ cycles!
	CPUPROT2
	CPUPROT2
}

void CPU186_OP8E()
{
	modrm_debugger16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, CPU[activeCPU].MODRM_src1);
	modrm_generateInstructionTEXT("MOV", 16, 0, PARAM_MODRM_01);
	if (CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0].reg16 == CPU[activeCPU].SEGMENT_REGISTERS[CPU_SEGMENT_CS]) /* CS is forbidden from this processor onwards! */
	{
		unkOP_186();
		return;
	}
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1|0xA0)) return;
	}
	if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].temp8Edata, CPU[activeCPU].MODRM_src1)) return;
	if (CPU186_internal_MOV16(modrm_addr16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0), CPU[activeCPU].temp8Edata)) return;
	if ((CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0].reg16 == &REG_SS) && (CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0].isreg == 1) && (CPU[activeCPU].previousAllowInterrupts))
	{
		CPU[activeCPU].allowInterrupts = 0; /* Inhabit all interrupts up to the next instruction */
	}
}

void CPU186_OPC0()
{
	CPU[activeCPU].oper2b = CPU[activeCPU].immb;

	memcpy(&CPU[activeCPU].info,&CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0],sizeof(CPU[activeCPU].info)); //Store the address for debugging!
	switch (CPU[activeCPU].thereg) //What function?
	{
		case 0: //ROL
			debugger_setcommand("ROL %s,%02X", CPU[activeCPU].info.text, CPU[activeCPU].oper2b);
			break;
		case 1: //ROR
			debugger_setcommand("ROR %s,%02X", CPU[activeCPU].info.text, CPU[activeCPU].oper2b);
			break;
		case 2: //RCL
			debugger_setcommand("RCL %s,%02X", CPU[activeCPU].info.text, CPU[activeCPU].oper2b);
			break;
		case 3: //RCR
			debugger_setcommand("RCR %s,%02X", CPU[activeCPU].info.text, CPU[activeCPU].oper2b);
			break;
		case 4: //SHL
		case 6: //--- Unknown Opcode! --- Undocumented opcode!
			debugger_setcommand("SHL %s,%02X", CPU[activeCPU].info.text, CPU[activeCPU].oper2b);
			break;
		case 5: //SHR
			debugger_setcommand("SHR %s,%02X", CPU[activeCPU].info.text, CPU[activeCPU].oper2b);
			break;
		case 7: //SAR
			debugger_setcommand("SAR %s,%02X", CPU[activeCPU].info.text, CPU[activeCPU].oper2b);
			break;
		default:
			break;
	}

	if (unlikely(CPU[activeCPU].modrmstep==0))
	{
		if (modrm_check8(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return; //Abort when needed!
		if (modrm_check8(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0xA0)) return; //Abort when needed!
	}
	if (CPU8086_instructionstepreadmodrmb(0,&CPU[activeCPU].instructionbufferb, CPU[activeCPU].MODRM_src0)) return;
	if (CPU[activeCPU].instructionstep==0) //Execution step?
	{
		CPU[activeCPU].oper1b = CPU[activeCPU].instructionbufferb;
		CPU[activeCPU].res8 = op_grp2_8(CPU[activeCPU].oper2b,2); //Execute!
		++CPU[activeCPU].instructionstep; //Next step: writeback!
		CPU[activeCPU].executed = 0; //Time it!
		return; //Wait for the next step!
	}
	if (CPU8086_instructionstepwritemodrmb(2, CPU[activeCPU].res8, CPU[activeCPU].MODRM_src0)) return;
} //GRP2 Eb,Ib

void CPU186_OPC1()
{
	memcpy(&CPU[activeCPU].info,&CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0],sizeof(CPU[activeCPU].info)); //Store the address for debugging!
	CPU[activeCPU].oper2 = (word)CPU[activeCPU].immb;
	switch (CPU[activeCPU].thereg) //What function?
	{
		case 0: //ROL
			debugger_setcommand("ROL %s,%02X", CPU[activeCPU].info.text, CPU[activeCPU].oper2);
			break;
		case 1: //ROR
			debugger_setcommand("ROR %s,%02X", CPU[activeCPU].info.text, CPU[activeCPU].oper2);
			break;
		case 2: //RCL
			debugger_setcommand("RCL %s,%02X", CPU[activeCPU].info.text, CPU[activeCPU].oper2);
			break;
		case 3: //RCR
			debugger_setcommand("RCR %s,%02X", CPU[activeCPU].info.text, CPU[activeCPU].oper2);
			break;
		case 4: //SHL
			debugger_setcommand("SHL %s,%02X", CPU[activeCPU].info.text, CPU[activeCPU].oper2);
			break;
		case 5: //SHR
			debugger_setcommand("SHR %s,%02X", CPU[activeCPU].info.text, CPU[activeCPU].oper2);
			break;
		case 6: //--- Unknown Opcode! --- Undocumented opcode!
			debugger_setcommand("SHL %s,%02X", CPU[activeCPU].info.text, CPU[activeCPU].oper2);
			break;
		case 7: //SAR
			debugger_setcommand("SAR %s,%02X", CPU[activeCPU].info.text, CPU[activeCPU].oper2);
			break;
		default:
			break;
	}
	
	if (unlikely(CPU[activeCPU].modrmstep==0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0x40)) return; //Abort when needed!
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0,0|0xA0)) return; //Abort when needed!
	}
	if (CPU8086_instructionstepreadmodrmw(0,&CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src0)) return;
	if (CPU[activeCPU].instructionstep==0) //Execution step?
	{
		CPU[activeCPU].oper1 = CPU[activeCPU].instructionbufferw;
		CPU[activeCPU].res16 = op_grp2_16((byte)CPU[activeCPU].oper2,2); //Execute!
		++CPU[activeCPU].instructionstep; //Next step: writeback!
		CPU[activeCPU].executed = 0; //Time it!
		return; //Wait for the next step!
	}
	if (CPU8086_instructionstepwritemodrmw(2, CPU[activeCPU].res16, CPU[activeCPU].MODRM_src0,0)) return;
} //GRP2 Ev,Ib

void CPU186_OPC8()
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
				if (CPU[activeCPU].instructionstep == instructionstep) //At the write back phase?
				{
					if (EMULATED_CPU <= CPU_80486) //We don't check it all before, but during the execution on 486- processors!
					{
						if (checkStackAccess(1, 1, 0)) return; //Abort on error!
					}
				}
				if (CPU8086_PUSHw(instructionstep,&CPU[activeCPU].bpdataw,0)) return; //Write back!
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
	REG_SP -= stacksize; //Substract: the stack size is data after the buffer created, not immediately at the params.  

	if ((memoryaccessfault = checkMMUaccess(CPU_SEGMENT_SS, REG_SS, REG_ESP&getstackaddrsizelimiter(), 0|0x40, getCPL(), !STACK_SEGMENT_DESCRIPTOR_B_BIT(), 0 | (0x0)))!=0) //Error accessing memory?
	{
		return; //Abort on fault!
	}

	CPU_apply286cycles(); //Apply the 80286+ cycles!
}
void CPU186_OPC9()
{
	debugger_setcommand("LEAVE");
	if (CPU[activeCPU].instructionstep==0) //Starting?
	{
		REG_SP = REG_BP; //LEAVE starting!
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

//Fully checked, and the same as fake86.
