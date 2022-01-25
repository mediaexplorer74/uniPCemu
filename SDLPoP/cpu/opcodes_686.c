/*

Copyright (C) 2020 - 2021 Superfury

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

#include "headers/cpu/cpu.h" //Basic CPU!
#include "headers/cpu/cpu_OP80686.h" //i686 support!
#include "headers/cpu/cpu_OP8086.h" //16-bit support!
#include "headers/cpu/cpu_OP80386.h" //32-bit support!
#include "headers/cpu/cpu_pmtimings.h" //Timing support!
#include "headers/cpu/easyregs.h" //Easy register support!
#include "headers/cpu/protection.h" //CPL support!

//Modr/m support, used when reg=NULL and custommem==0

void CPU686_OP0F0D_16()
{
	if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
	{
		modrm_generateInstructionTEXT("NOP", 16, 0, PARAM_MODRM_0);
	}
	/* NOP */
	if (CPU_apply286cycles() == 0) //No 80286+ cycles instead?
	{
		CPU[activeCPU].cycles_OP += 1; //Single cycle!
	}
}

void CPU686_OP0F0D_32()
{
	if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
	{
		modrm_generateInstructionTEXT("NOP", 32, 0, PARAM_MODRM_0);
	}
	if (CPU_apply286cycles() == 0) //No 80286+ cycles instead?
	{
		CPU[activeCPU].cycles_OP += 1; //Single cycle!
	}
}

void CPU686_OP0F18_16()
{
	if (unlikely(CPU[activeCPU].cpudebugger))
	{
		modrm_debugger16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, CPU[activeCPU].MODRM_src1); //Get src!
		switch (CPU[activeCPU].thereg) //What function?
		{
		case 4: //HINT_NOP
		case 5: //HINT_NOP
		case 6: //HINT_NOP
		case 7: //HINT_NOP
			modrm_generateInstructionTEXT("HINT_NOP", 16, 0, PARAM_MODRM_0);
			break;
		default:
			CPU_unkOP(); //Undefined opcode!
			break;
		}
	}
}

void CPU686_OP0F18_32()
{
	if (unlikely(CPU[activeCPU].cpudebugger))
	{
		modrm_debugger32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, CPU[activeCPU].MODRM_src1); //Get src!
		switch (CPU[activeCPU].thereg) //What function?
		{
		case 4: //HINT_NOP
		case 5: //HINT_NOP
		case 6: //HINT_NOP
		case 7: //HINT_NOP
			modrm_generateInstructionTEXT("HINT_NOP", 32, 0, PARAM_MODRM_0);
			break;
		default:
			CPU_unkOP(); //Undefined opcode!
			break;
		}
	}
}

void CPU686_OP0FNOP_16()
{
	modrm_debugger16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, CPU[activeCPU].MODRM_src1); //Get src!
	modrm_generateInstructionTEXT("HINT_NOP", 16, 0, PARAM_MODRM_0);
}

void CPU686_OP0FNOP_32()
{
	modrm_debugger32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, CPU[activeCPU].MODRM_src1); //Get src!
	modrm_generateInstructionTEXT("HINT_NOP", 32, 0, PARAM_MODRM_0);
}

void CPU686_OP0F1F_16()
{
	if (unlikely(CPU[activeCPU].cpudebugger))
	{
		modrm_debugger16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, CPU[activeCPU].MODRM_src1); //Get src!
		switch (CPU[activeCPU].thereg) //What function?
		{
		case 1: //HINT_NOP
		case 2: //HINT_NOP
		case 3: //HINT_NOP
		case 4: //HINT_NOP
		case 5: //HINT_NOP
		case 6: //HINT_NOP
		case 7: //HINT_NOP
			modrm_generateInstructionTEXT("HINT_NOP", 16, 0, PARAM_MODRM_0);
			break;
		default:
			CPU_unkOP(); //Undefined opcode!
			break;
		}
	}
}

void CPU686_OP0F1F_32()
{
	if (unlikely(CPU[activeCPU].cpudebugger))
	{
		modrm_debugger32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, CPU[activeCPU].MODRM_src1); //Get src!
		switch (CPU[activeCPU].thereg) //What function?
		{
		case 1: //HINT_NOP
		case 2: //HINT_NOP
		case 3: //HINT_NOP
		case 4: //HINT_NOP
		case 5: //HINT_NOP
		case 6: //HINT_NOP
		case 7: //HINT_NOP
			modrm_generateInstructionTEXT("HINT_NOP", 32, 0, PARAM_MODRM_0);
			break;
		default:
			CPU_unkOP(); //Undefined opcode!
			break;
		}
	}
}

void CPU686_OP0F33()
{
	if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
	{
		modrm_generateInstructionTEXT("RDPMC", 32, 0, PARAM_NONE);
	}
	if (((CPU[activeCPU].registers->CR4&0x100)==0) && getCPL() && (getcpumode()!=CPU_MODE_REAL))
	{
		THROWDESCGP(0, 0, 0); //#GP(0)!
		return;
	}
	//Check ECX for validity?
	if (1) //Always invalid?
	{
		THROWDESCGP(0, 0, 0); //#GP(0)!
		return;
	}
}

//CMOVcc instructions

//SETCC instructions

/*

Info for different conditions:
O: FLAG_OF
NO: !FLAG_OF
C: FLAG_CF
NC: !FLAG_CF
Z: FLAG_ZF
NZ: !FLAG_ZF
BE: (FLAG_CF||FLAG_ZF)
A: (!FLAG_CF && !FLAG_ZF)
S: FLAG_SF
NS: !FLAG_SF
P: FLAG_PF
NP: !FLAG_PF
L: (FLAG_SF!=FLAG_OF)
GE: (FLAG_SF==FLAG_OF)
LE: ((FLAG_SF!=FLAG_OF) || FLAG_ZF)
G: (!FLAG_ZF && (FLAG_SF==FLAG_OF))

*/

//16-bit

void CPU686_OP0F40_16()
{
	modrm_generateInstructionTEXT("CMOVO", 16, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if (FLAG_OF)
	{
		if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src1)) return;
		if (CPU8086_instructionstepwritemodrmw(2, CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src0, 0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVO r/m16
void CPU686_OP0F41_16()
{
	modrm_generateInstructionTEXT("CMOVNO", 16, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if (FLAG_OF ^ 1)
	{
		if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src1)) return;
		if (CPU8086_instructionstepwritemodrmw(2, CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src0, 0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVNO r/m16
void CPU686_OP0F42_16()
{
	modrm_generateInstructionTEXT("CMOVC", 16, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if (FLAG_CF)
	{
		if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src1)) return;
		if (CPU8086_instructionstepwritemodrmw(2, CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src0, 0)) return;
	}
	if (CPU8086_instructionstepwritemodrmb(0, FLAG_CF, CPU[activeCPU].MODRM_src0)) return;
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVC r/m16
void CPU686_OP0F43_16()
{
	modrm_generateInstructionTEXT("CMOVNC", 16, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if (FLAG_CF ^ 1)
	{
		if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src1)) return;
		if (CPU8086_instructionstepwritemodrmw(2, CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src0, 0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVAE r/m16
void CPU686_OP0F44_16()
{
	modrm_generateInstructionTEXT("CMOVZ", 16, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if (FLAG_ZF)
	{
		if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src1)) return;
		if (CPU8086_instructionstepwritemodrmw(2, CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src0, 0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVE r/m16
void CPU686_OP0F45_16()
{
	modrm_generateInstructionTEXT("CMOVNZ", 16, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if (FLAG_ZF ^ 1)
	{
		if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src1)) return;
		if (CPU8086_instructionstepwritemodrmw(2, CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src0, 0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVNE r/m16
void CPU686_OP0F46_16()
{
	modrm_generateInstructionTEXT("CMOVBE", 16, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if ((FLAG_CF | FLAG_ZF)!=0)
	{
		if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src1)) return;
		if (CPU8086_instructionstepwritemodrmw(2, CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src0, 0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVNA r/m16
void CPU686_OP0F47_16()
{
	modrm_generateInstructionTEXT("CMOVNBE", 16, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if ((FLAG_CF | FLAG_ZF) ^ 1)
	{
		if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src1)) return;
		if (CPU8086_instructionstepwritemodrmw(2, CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src0, 0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVA r/m16
void CPU686_OP0F48_16()
{
	modrm_generateInstructionTEXT("CMOVS", 16, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if (FLAG_SF)
	{
		if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src1)) return;
		if (CPU8086_instructionstepwritemodrmw(2, CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src0, 0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVS r/m16
void CPU686_OP0F49_16()
{
	modrm_generateInstructionTEXT("CMOVNS", 16, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if (FLAG_SF ^ 1)
	{
		if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src1)) return;
		if (CPU8086_instructionstepwritemodrmw(2, CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src0, 0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVNS r/m16
void CPU686_OP0F4A_16()
{
	modrm_generateInstructionTEXT("CMOVP", 16, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if (FLAG_PF)
	{
		if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src1)) return;
		if (CPU8086_instructionstepwritemodrmw(2, CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src0, 0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVP r/m16
void CPU686_OP0F4B_16()
{
	modrm_generateInstructionTEXT("CMOVNP", 16, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if (FLAG_PF ^ 1)
	{
		if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src1)) return;
		if (CPU8086_instructionstepwritemodrmw(2, CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src0, 0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVNP r/m16
void CPU686_OP0F4C_16()
{
	modrm_generateInstructionTEXT("CMOVL", 16, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if ((FLAG_SF ^ FLAG_OF)!=0)
	{
		if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src1)) return;
		if (CPU8086_instructionstepwritemodrmw(2, CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src0, 0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVL r/m16
void CPU686_OP0F4D_16()
{
	modrm_generateInstructionTEXT("CMOVGE", 16, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if ((FLAG_SF ^ FLAG_OF) ^ 1)
	{
		if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src1)) return;
		if (CPU8086_instructionstepwritemodrmw(2, CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src0, 0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVGE r/m16
void CPU686_OP0F4E_16()
{
	modrm_generateInstructionTEXT("CMOVLE", 16, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if ((FLAG_SF ^ FLAG_OF) | FLAG_ZF)
	{
		if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src1)) return;
		if (CPU8086_instructionstepwritemodrmw(2, CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src0, 0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVLE r/m16
void CPU686_OP0F4F_16()
{
	modrm_generateInstructionTEXT("CMOVG", 16, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if ((((FLAG_SF ^ FLAG_OF) ^ 1) & (FLAG_ZF ^ 1))!=0)
	{
		if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src1)) return;
		if (CPU8086_instructionstepwritemodrmw(2, CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src0, 0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVG r/m16

//32-bit

void CPU686_OP0F40_32()
{
	modrm_generateInstructionTEXT("CMOVO", 32, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if (FLAG_OF)
	{
		if (CPU80386_instructionstepreadmodrmdw(0, &CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src1)) return;
		if (CPU80386_instructionstepwritemodrmdw(2, CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVO r/m32
void CPU686_OP0F41_32()
{
	modrm_generateInstructionTEXT("CMOVNO", 32, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if (FLAG_OF ^ 1)
	{
		if (CPU80386_instructionstepreadmodrmdw(0, &CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src1)) return;
		if (CPU80386_instructionstepwritemodrmdw(2, CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVNO r/m32
void CPU686_OP0F42_32()
{
	modrm_generateInstructionTEXT("CMOVC", 32, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if (FLAG_CF)
	{
		if (CPU80386_instructionstepreadmodrmdw(0, &CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src1)) return;
		if (CPU80386_instructionstepwritemodrmdw(2, CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src0)) return;
	}
	if (CPU8086_instructionstepwritemodrmb(0, FLAG_CF, CPU[activeCPU].MODRM_src0)) return;
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVC r/m32
void CPU686_OP0F43_32()
{
	modrm_generateInstructionTEXT("CMOVNC", 32, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if (FLAG_CF ^ 1)
	{
		if (CPU80386_instructionstepreadmodrmdw(0, &CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src1)) return;
		if (CPU80386_instructionstepwritemodrmdw(2, CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVAE r/m32
void CPU686_OP0F44_32()
{
	modrm_generateInstructionTEXT("CMOVZ", 32, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if (FLAG_ZF)
	{
		if (CPU80386_instructionstepreadmodrmdw(0, &CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src1)) return;
		if (CPU80386_instructionstepwritemodrmdw(2, CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVE r/m32
void CPU686_OP0F45_32()
{
	modrm_generateInstructionTEXT("CMOVNZ", 32, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if (FLAG_ZF ^ 1)
	{
		if (CPU80386_instructionstepreadmodrmdw(0, &CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src1)) return;
		if (CPU80386_instructionstepwritemodrmdw(2, CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVNE r/m32
void CPU686_OP0F46_32()
{
	modrm_generateInstructionTEXT("CMOVBE", 32, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if ((FLAG_CF | FLAG_ZF) != 0)
	{
		if (CPU80386_instructionstepreadmodrmdw(0, &CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src1)) return;
		if (CPU80386_instructionstepwritemodrmdw(2, CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVNA r/m32
void CPU686_OP0F47_32()
{
	modrm_generateInstructionTEXT("CMOVNBE", 32, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if ((FLAG_CF | FLAG_ZF) ^ 1)
	{
		if (CPU80386_instructionstepreadmodrmdw(0, &CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src1)) return;
		if (CPU80386_instructionstepwritemodrmdw(2, CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVA r/m32
void CPU686_OP0F48_32()
{
	modrm_generateInstructionTEXT("CMOVS", 32, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if (FLAG_SF)
	{
		if (CPU80386_instructionstepreadmodrmdw(0, &CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src1)) return;
		if (CPU80386_instructionstepwritemodrmdw(2, CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVS r/m32
void CPU686_OP0F49_32()
{
	modrm_generateInstructionTEXT("CMOVNS", 32, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if (FLAG_SF ^ 1)
	{
		if (CPU80386_instructionstepreadmodrmdw(0, &CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src1)) return;
		if (CPU80386_instructionstepwritemodrmdw(2, CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVNS r/m32
void CPU686_OP0F4A_32()
{
	modrm_generateInstructionTEXT("CMOVP", 32, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if (FLAG_PF)
	{
		if (CPU80386_instructionstepreadmodrmdw(0, &CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src1)) return;
		if (CPU80386_instructionstepwritemodrmdw(2, CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVP r/m32
void CPU686_OP0F4B_32()
{
	modrm_generateInstructionTEXT("CMOVNP", 32, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if (FLAG_PF ^ 1)
	{
		if (CPU80386_instructionstepreadmodrmdw(0, &CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src1)) return;
		if (CPU80386_instructionstepwritemodrmdw(2, CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVNP r/m32
void CPU686_OP0F4C_32()
{
	modrm_generateInstructionTEXT("CMOVL", 32, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if ((FLAG_SF ^ FLAG_OF) != 0)
	{
		if (CPU80386_instructionstepreadmodrmdw(0, &CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src1)) return;
		if (CPU80386_instructionstepwritemodrmdw(2, CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVL r/m32
void CPU686_OP0F4D_32()
{
	modrm_generateInstructionTEXT("CMOVGE", 32, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if ((FLAG_SF ^ FLAG_OF) ^ 1)
	{
		if (CPU80386_instructionstepreadmodrmdw(0, &CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src1)) return;
		if (CPU80386_instructionstepwritemodrmdw(2, CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVGE r/m32
void CPU686_OP0F4E_32()
{
	modrm_generateInstructionTEXT("CMOVLE", 32, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if ((FLAG_SF ^ FLAG_OF) | FLAG_ZF)
	{
		if (CPU80386_instructionstepreadmodrmdw(0, &CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src1)) return;
		if (CPU80386_instructionstepwritemodrmdw(2, CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVLE r/m32
void CPU686_OP0F4F_32()
{
	modrm_generateInstructionTEXT("CMOVG", 32, 0, PARAM_MODRM_01);
	if (unlikely(CPU[activeCPU].modrmstep == 0))
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if ((((FLAG_SF ^ FLAG_OF) ^ 1) & (FLAG_ZF ^ 1)) != 0)
	{
		if (CPU80386_instructionstepreadmodrmdw(0, &CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src1)) return;
		if (CPU80386_instructionstepwritemodrmdw(2, CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src0)) return;
	}
	CPU_apply286cycles(); /* Apply cycles */
} //CMOVG r/m32
