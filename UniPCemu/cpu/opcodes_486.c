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

#include "headers/cpu/cpu.h"
#include "headers/cpu/easyregs.h"
#include "headers/cpu/cpu_OP8086.h" //16-bit memory reading!
#include "headers/cpu/cpu_OP80286.h" //80286 opcodes!
#include "headers/cpu/cpu_OP80386.h" //80386 opcodes!
#include "headers/cpu/modrm.h" //ModR/M support!
#include "headers/cpu/protection.h" //Protection fault support!
#include "headers/cpu/paging.h" //Paging support for clearing TLB!
#include "headers/cpu/flags.h" //Flags support for adding!
#include "headers/emu/debugger/debugger.h" //Debugger support!
#include "headers/cpu/cpu_pmtimings.h" //Timing support!

//When using http://www.mlsite.net/8086/: G=Modr/m mod&r/m adress, E=Reg field in modr/m

//INFO: http://www.mlsite.net/8086/
//Extra info about above: Extension opcodes (GRP1 etc) are contained in the modr/m
//Ammount of instructions in the completed core: 123

//Aftercount: 60-6F,C0-C1, C8-C9, D6, D8-DF, F1, 0F(has been implemented anyways)
//Total count: 30 opcodes undefined.

//Info: Ap = 32-bit segment:offset pointer (data: param 1:word segment, param 2:word offset)

//Simplifier!

extern byte debuggerINT; //Interrupt special trigger?

//Modr/m support, used when reg=NULL and custommem==0

void CPU486_CPUID()
{
	CPU_unkOP(); //We don't know how to handle this yet, so handle like a #UD for now(also EFLAGS.CPUID)!
	return;
	CPU_CPUID(); //Common CPUID instruction!
	if (CPU_apply286cycles() == 0) //No 80286+ cycles instead?
	{
		CPU[activeCPU].cycles_OP += 1; //Single cycle!
	}
}

void CPU486_OP0F01_32()
{
	uint_32 linearaddr;
	if (CPU[activeCPU].thereg==7) //INVLPG?
	{
		modrm_generateInstructionTEXT("INVLPG",16,0,PARAM_MODRM_1);
		if (getcpumode()!=CPU_MODE_REAL) //Protected mode?
		{
			if (getCPL())
			{
				THROWDESCGP(0,0,0);
				return;
			}
		}
		if ((modrm_isregister(CPU[activeCPU].params)) /*&& (getcpumode() != CPU_MODE_8086)*/) //Register (not for V86 mode)? #UD
		{
			unkOP0F_286(); //We're not recognized in real mode!
			return;
		}
		linearaddr = MMU_realaddr(CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0].segmentregister_index,*CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0].segmentregister, CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0].mem_offset,0, CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0].is16bit); //Linear address!
		Paging_Invalidate(linearaddr); //Invalidate the address that's used!
	}
	else
	{
		CPU386_OP0F01(); //Fallback to 80386 instructions!
	}
}

void CPU486_OP0F01_16()
{
	uint_32 linearaddr;
	if (CPU[activeCPU].thereg==7) //INVLPG?
	{
		modrm_generateInstructionTEXT("INVLPG",32,0,PARAM_MODRM2);
		if (getcpumode() != CPU_MODE_REAL) //Protected mode?
		{
			if (getCPL())
			{
				THROWDESCGP(0, 0, 0);
				return;
			}
		}
		if ((modrm_isregister(CPU[activeCPU].params)) /*&& (getcpumode()!=CPU_MODE_8086)*/) //Register (not for V86 mode)? #UD
		{
			unkOP0F_286(); //We're not recognized in real mode!
			return;
		}
		linearaddr = MMU_realaddr(CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0].segmentregister_index,*CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0].segmentregister, CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0].mem_offset,0, CPU[activeCPU].params.info[CPU[activeCPU].MODRM_src0].is16bit); //Linear address!
		Paging_Invalidate(linearaddr); //Invalidate the address that's used!
	}
	else
	{
		CPU286_OP0F01(); //Fallback to 80386 instructions!
	}
}

void CPU486_OP0F08() //INVD?
{
	modrm_generateInstructionTEXT("INVD",0,0,PARAM_NONE);
	if (getcpumode()!=CPU_MODE_REAL) //Protected mode?
	{
		if (getCPL())
		{
			THROWDESCGP(0,0,0);
			return;
		}
	}
}

void CPU486_OP0F09() //WBINVD?
{
	modrm_generateInstructionTEXT("WBINVD",0,0,PARAM_NONE);
	if (getcpumode()!=CPU_MODE_REAL) //Protected mode?
	{
		if (getCPL())
		{
			THROWDESCGP(0,0,0);
			return;
		}
	}
}

void CPU486_OP0FB0()
{
	modrm_generateInstructionTEXT("CMPXCHG", 8, 0, PARAM_MODRM_0_ACCUM_1);
	if (CPU[activeCPU].modrmstep == 0) //Starting up?
	{
		if (modrm_check8(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0)) return;
		if (modrm_check8(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1)) return;
	}
	if (CPU8086_instructionstepreadmodrmb(0, &CPU[activeCPU].instructionbufferb, CPU[activeCPU].MODRM_src0)) return; //Read the destination!
	if (CPU8086_instructionstepreadmodrmb(2, &CPU[activeCPU].instructionbufferb2, CPU[activeCPU].MODRM_src1)) return; //Read the source!
	if (CPU[activeCPU].instructionstep == 0) //Execute phase?
	{
		flag_sub8(REG_AL, CPU[activeCPU].instructionbufferb); //All arithmetic flags are affected!
		++CPU[activeCPU].instructionstep;
	}
	if (FLAG_ZF) //Equal?
	{
		/* r/m8=r8 */
		if (CPU8086_instructionstepwritemodrmb(4, CPU[activeCPU].instructionbufferb2, CPU[activeCPU].MODRM_src0)) return;
	}
	else
	{
		/* r/m8(write back it's own value) and AL are both to be set to r/m8 */
		if (CPU8086_instructionstepwritemodrmb(4, CPU[activeCPU].instructionbufferb, CPU[activeCPU].MODRM_src0)) return;
		REG_AL = CPU[activeCPU].instructionbufferb; /* AL=r/m8 */
	}
} //CMPXCHG r/m8,AL,r8
void CPU486_OP0FB1_16()
{
	modrm_generateInstructionTEXT("CMPXCHG", 16, 0, PARAM_MODRM_0_ACCUM_1);
	if (CPU[activeCPU].modrmstep == 0) //Starting up?
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src0)) return; //Read the destination!
	if (CPU8086_instructionstepreadmodrmw(2, &CPU[activeCPU].instructionbufferw2, CPU[activeCPU].MODRM_src1)) return; //Read the source!
	if (CPU[activeCPU].instructionstep == 0) //Execute phase?
	{
		flag_sub16(REG_AX, CPU[activeCPU].instructionbufferw); //All arithmetic flags are affected!
		++CPU[activeCPU].instructionstep;
	}
	if (FLAG_ZF) //Equal?
	{
		/* r/m16=r16 */
		if (CPU8086_instructionstepwritemodrmw(4, CPU[activeCPU].instructionbufferw2, CPU[activeCPU].MODRM_src0, 0)) return;
	}
	else
	{
		/* r/m16(write back it's own value) and AX are both to be set to r/m8 */
		if (CPU8086_instructionstepwritemodrmw(4, CPU[activeCPU].instructionbufferw, CPU[activeCPU].MODRM_src0, 0)) return;
		REG_AX = CPU[activeCPU].instructionbufferw; /* AX=r/m16 */
	}
} //CMPXCHG r/m16,AX,r16
void CPU486_OP0FB1_32()
{
	modrm_generateInstructionTEXT("CMPXCHG", 32, 0, PARAM_MODRM_0_ACCUM_1);
	if (CPU[activeCPU].modrmstep == 0) //Starting up?
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 1 | 0xA0)) return;
	}
	if (CPU80386_instructionstepreadmodrmdw(0, &CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src0)) return; //Read the destination!
	if (CPU80386_instructionstepreadmodrmdw(2, &CPU[activeCPU].instructionbufferd2, CPU[activeCPU].MODRM_src1)) return; //Read the source!
	if (CPU[activeCPU].instructionstep == 0) //Execute phase?
	{
		flag_sub32(REG_EAX, CPU[activeCPU].instructionbufferd); //All arithmetic flags are affected!
		++CPU[activeCPU].instructionstep;
	}
	if (FLAG_ZF) //Equal?
	{
		/* r/m16=r16 */
		if (CPU80386_instructionstepwritemodrmdw(4, CPU[activeCPU].instructionbufferd2, CPU[activeCPU].MODRM_src0)) return;
	}
	else
	{
		/* r/m32(write back it's own value) and AX are both to be set to r/m8 */
		if (CPU80386_instructionstepwritemodrmdw(4, CPU[activeCPU].instructionbufferd, CPU[activeCPU].MODRM_src0)) return;
		REG_EAX = CPU[activeCPU].instructionbufferd; /* EAX=r/m32 */
	}
} //CMPXCHG r/m16,AX,r16

OPTINLINE void op_add8_486()
{
	CPU[activeCPU].res8 = CPU[activeCPU].oper1b + CPU[activeCPU].oper2b;
	flag_add8 (CPU[activeCPU].oper1b, CPU[activeCPU].oper2b);
}

OPTINLINE void op_add16_486()
{
	CPU[activeCPU].res16 = CPU[activeCPU].oper1 + CPU[activeCPU].oper2;
	flag_add16 (CPU[activeCPU].oper1, CPU[activeCPU].oper2);
}

OPTINLINE void op_add32_486()
{
	CPU[activeCPU].res32 = CPU[activeCPU].oper1d + CPU[activeCPU].oper2d;
	flag_add32 (CPU[activeCPU].oper1d, CPU[activeCPU].oper2d);
}

void CPU486_OP0FC0()
{
	modrm_generateInstructionTEXT("XADD",8,0,PARAM_MODRM21);
	if (CPU[activeCPU].modrmstep == 0) //Starting up?
	{
		if (modrm_check8(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check8(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 0 | 0x40)) return;
		if (modrm_check8(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check8(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 0 | 0xA0)) return;
	}
	if (CPU8086_instructionstepreadmodrmb(0, &CPU[activeCPU].oper1b, CPU[activeCPU].MODRM_src1)) return; //Read the source!
	if (CPU8086_instructionstepreadmodrmb(2, &CPU[activeCPU].oper2b, CPU[activeCPU].MODRM_src0)) return; //Read the destination!
	if (CPU[activeCPU].instructionstep == 0) //Execute phase?
	{
		op_add8_486();
		++CPU[activeCPU].instructionstep;
	}
	if (CPU8086_instructionstepwritemodrmb(4, CPU[activeCPU].oper2b, CPU[activeCPU].MODRM_src1)) return; //Write the source!
	if (CPU8086_instructionstepwritemodrmb(6, CPU[activeCPU].res8, CPU[activeCPU].MODRM_src0)) return; //Write the destination!
} //XADD r/m8,r8
void CPU486_OP0FC1_16()
{
	modrm_generateInstructionTEXT("XADD",16,0,PARAM_MODRM21);
	if (CPU[activeCPU].modrmstep == 0) //Starting up?
	{
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 0 | 0x40)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 0 | 0xA0)) return;
	}
	if (CPU8086_instructionstepreadmodrmw(0, &CPU[activeCPU].oper1, CPU[activeCPU].MODRM_src1)) return; //Read the source!
	if (CPU8086_instructionstepreadmodrmw(2, &CPU[activeCPU].oper2, CPU[activeCPU].MODRM_src0)) return; //Read the destination!
	if (CPU[activeCPU].instructionstep == 0) //Execute phase?
	{
		op_add16_486();
		++CPU[activeCPU].instructionstep;
	}
	if (CPU8086_instructionstepwritemodrmw(4, CPU[activeCPU].oper2, CPU[activeCPU].MODRM_src1, 0)) return; //Write the source!
	if (CPU8086_instructionstepwritemodrmw(6, CPU[activeCPU].res16, CPU[activeCPU].MODRM_src0, 0)) return; //Write the destination!
} //XADD r/m16,r16
void CPU486_OP0FC1_32()
{
	modrm_generateInstructionTEXT("XADD",32,0,PARAM_MODRM21);
	if (CPU[activeCPU].modrmstep == 0) //Starting up?
	{
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 0 | 0x40)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, 0 | 0xA0)) return;
		if (modrm_check32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src1, 0 | 0xA0)) return;
	}
	if (CPU80386_instructionstepreadmodrmdw(0, &CPU[activeCPU].oper1d, CPU[activeCPU].MODRM_src1)) return; //Read the source!
	if (CPU80386_instructionstepreadmodrmdw(2, &CPU[activeCPU].oper2d, CPU[activeCPU].MODRM_src0)) return; //Read the destination!
	if (CPU[activeCPU].instructionstep == 0) //Execute phase?
	{
		op_add32_486();
		++CPU[activeCPU].instructionstep;
	}
	if (CPU80386_instructionstepwritemodrmdw(4, CPU[activeCPU].oper2d, CPU[activeCPU].MODRM_src1)) return; //Write the source!
	if (CPU80386_instructionstepwritemodrmdw(6, CPU[activeCPU].res32, CPU[activeCPU].MODRM_src0)) return; //Write the destination!
} //XADD r/m32,r32

void CPU486_BSWAP32(uint_32 *reg)
{
	/* Swap endianness on a register(Big->Little or Little->Big) */
	INLINEREGISTER uint_32 buf;
	buf = *reg; //Read to start!
	buf = ((buf>>16)|(buf<<16)); //Swap words!
	buf = (((buf>>8)&0xFF00FF)|((buf<<8)&0xFF00FF00)); //Swap bytes to finish!
	*reg = buf; //Save the result!
}

//BSWAP on 16-bit registers is undefined!
void CPU486_BSWAP16(word* reg)
{
	/* Swap endianness on a register(Big->Little or Little->Big) */
	uint_32 buf;
	//Undocumented behaviour on 80486+: clears the register(essentially zero-extend to 32-bits, then BSWAP val32, then truncated to 16-bits to write the result)!
	buf = *reg; //Read the register to start!
	buf &= 0xFFFF; //Zero-extend to 32-bits!
	CPU486_BSWAP32(&buf); //BSWAP val32
	*reg = (buf&0xFFFFU); //Give the undocumented behaviour!
}

void CPU486_OP0FC8_16()
{
	debugger_setcommand("BSWAP AX");
	CPU486_BSWAP16(&REG_AX);
} //BSWAP AX
void CPU486_OP0FC8_32()
{
	debugger_setcommand("BSWAP EAX");
	CPU486_BSWAP32(&REG_EAX);
} //BSWAP EAX
void CPU486_OP0FC9_16()
{
	debugger_setcommand("BSWAP CX");
	CPU486_BSWAP16(&REG_CX);
} //BSWAP CX
void CPU486_OP0FC9_32()
{
	debugger_setcommand("BSWAP ECX");
	CPU486_BSWAP32(&REG_ECX);
} //BSWAP ECX
void CPU486_OP0FCA_16()
{
	debugger_setcommand("BSWAP DX");
	CPU486_BSWAP16(&REG_DX);
} //BSWAP DX
void CPU486_OP0FCA_32()
{
	debugger_setcommand("BSWAP EDX");
	CPU486_BSWAP32(&REG_EDX);
} //BSWAP EDX
void CPU486_OP0FCB_16()
{
	debugger_setcommand("BSWAP BX");
	CPU486_BSWAP16(&REG_BX);
} //BSWAP BX
void CPU486_OP0FCB_32()
{
	debugger_setcommand("BSWAP EBX");
	CPU486_BSWAP32(&REG_EBX);
} //BSWAP EBX
void CPU486_OP0FCC_16()
{
	debugger_setcommand("BSWAP SP");
	CPU486_BSWAP16(&REG_SP);
} //BSWAP SP
void CPU486_OP0FCC_32()
{
	debugger_setcommand("BSWAP ESP");
	CPU486_BSWAP32(&REG_ESP);
} //BSWAP ESP
void CPU486_OP0FCD_16()
{
	debugger_setcommand("BSWAP BP");
	CPU486_BSWAP16(&REG_BP);
} //BSWAP BP
void CPU486_OP0FCD_32()
{
	debugger_setcommand("BSWAP EBP");
	CPU486_BSWAP32(&REG_EBP);
} //BSWAP EBP
void CPU486_OP0FCE_16()
{
	debugger_setcommand("BSWAP SI");
	CPU486_BSWAP16(&REG_SI);
} //BSWAP SI
void CPU486_OP0FCE_32()
{
	debugger_setcommand("BSWAP ESI");
	CPU486_BSWAP32(&REG_ESI);
} //BSWAP ESI
void CPU486_OP0FCF_16()
{
	debugger_setcommand("BSWAP DI");
	CPU486_BSWAP16(&REG_DI);
} //BSWAP DI
void CPU486_OP0FCF_32()
{
	debugger_setcommand("BSWAP EDI");
	CPU486_BSWAP32(&REG_EDI);
} //BSWAP EDI
