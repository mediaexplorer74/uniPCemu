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
#include "headers/cpu/cpu.h" //CPU!
#include "headers/cpu/easyregs.h" //EASY Regs!

byte parity[0x100] = { //All parity values!
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1
};

//Sign and parity logic

void flag_p8(uint8_t value)
{
	FLAGW_PF(parity[value]);	
}

void flag_p16(uint16_t value)
{
	FLAGW_PF(parity[value&0xFF]);	
}

void flag_p32(uint32_t value)
{
	FLAGW_PF(parity[value&0xFF]);	
}

void flag_s8(uint8_t value)
{
	if (value & 0x80) FLAGW_SF(1);
	else FLAGW_SF(0);
}

void flag_s16(uint16_t value)
{
	if (value & 0x8000) FLAGW_SF(1);
	else FLAGW_SF(0);
}

void flag_s32(uint32_t value)
{
	if (value & 0x80000000) FLAGW_SF(1);
	else FLAGW_SF(0);
}

//Sign, Zero and Parity logic

void flag_szp8(uint8_t value)
{
	if (!value) FLAGW_ZF(1);
	else FLAGW_ZF(0);
	if (value & 0x80) FLAGW_SF(1);
	else FLAGW_SF(0);
	FLAGW_PF(parity[value]);
}

void flag_szp16(uint16_t value)
{
	if (!value) FLAGW_ZF(1);
	else FLAGW_ZF(0);
	if (value & 0x8000) FLAGW_SF(1);
	else FLAGW_SF(0);
	FLAGW_PF(parity[value & 255]);
}

void flag_szp32(uint32_t value)
{
	if (!value) FLAGW_ZF(1);
	else FLAGW_ZF(0);
	if (value & 0x80000000) FLAGW_SF(1);
	else FLAGW_SF(0);
	FLAGW_PF(parity[value & 255]);
}

//Logarithmic logic

void flag_log8(uint8_t value)
{
	flag_szp8(value);
	FLAGW_CF(0);
	FLAGW_OF(0);
	FLAGW_AF(0); //Undocumented!
}

void flag_log16(uint16_t value)
{
	flag_szp16(value);
	FLAGW_CF(0);
	FLAGW_OF(0);
	FLAGW_AF(0); //Undocumented!
}

void flag_log32(uint32_t value)
{
	flag_szp32(value);
	FLAGW_CF(0);
	FLAGW_OF(0);
	FLAGW_AF(0); //Undocumented!
}

//Addition Carry, Overflow, Adjust logic
//Tables based on http://teaching.idallen.com/dat2343/10f/notes/040_overflow.txt
//Index Bit0=sumsign, Bit1=num2sign(add or sub negated value), Bit2=num1sign(v1)
byte addoverflow[8] = {0,1,0,0,0,0,1,0};
byte suboverflow[8] = {0,0,0,1,1,0,0,0};

#define NEG(x) ((~x)+1)

#define OVERFLOW_DSTMASK 1
#define OVERFLOW_NUM2MASK 2
#define OVERFLOW_NUM1MASK 4
#define OVERFLOW8_DST 7
#define OVERFLOW8_NUM2 6
#define OVERFLOW8_NUM1 5
#define OVERFLOW16_DST 15
#define OVERFLOW16_NUM2 14
#define OVERFLOW16_NUM1 13
#define OVERFLOW32_DST 31
#define OVERFLOW32_NUM2 30
#define OVERFLOW32_NUM1 29

#define obitsa(v1,v2) ((v1^dst)&(~(v1^v2)))
#define obitss(v1,v2) ((v1^dst)&(v1^v2))

#define OVERFLOWA8(v1,add,dst) /*addoverflow[((dst>>OVERFLOW8_DST)&OVERFLOW_DSTMASK)|(((add>>OVERFLOW8_NUM2)&OVERFLOW_NUM2MASK))|((v1>>OVERFLOW8_NUM1)&OVERFLOW_NUM1MASK)]*/ /*((((dst^v1)&(dst^add))>>7)&1)*/ ((obitsa(v1,add)>>7)&1)
#define OVERFLOWA16(v1,add,dst) /*addoverflow[((dst>>OVERFLOW16_DST)&OVERFLOW_DSTMASK)|(((add>>OVERFLOW16_NUM2)&OVERFLOW_NUM2MASK))|((v1>>OVERFLOW16_NUM1)&OVERFLOW_NUM1MASK)]*/ /*((((dst^v1)&(dst^add))>>15)&1)*/ ((obitsa(v1,add)>>15)&1)
#define OVERFLOWA32(v1,add,dst) /*addoverflow[((dst>>OVERFLOW32_DST)&OVERFLOW_DSTMASK)|(((add>>OVERFLOW32_NUM2)&OVERFLOW_NUM2MASK))|((v1>>OVERFLOW32_NUM1)&OVERFLOW_NUM1MASK)]*/ /*((((dst^v1)&(dst^add))>>31)&1)*/ ((obitsa(v1,add)>>31)&1)
#define OVERFLOWS8(v1,sub,dst) /*suboverflow[((dst>>OVERFLOW8_DST)&OVERFLOW_DSTMASK)|((sub>>OVERFLOW8_NUM2)&OVERFLOW_NUM2MASK)|((v1>>OVERFLOW8_NUM1)&OVERFLOW_NUM1MASK)]*/ /*((((dst^v1)&(v1^sub))>>7)&1)*/ ((obitss(v1,sub)>>7)&1)
#define OVERFLOWS16(v1,sub,dst) /*suboverflow[((dst>>OVERFLOW16_DST)&OVERFLOW_DSTMASK)|((sub>>OVERFLOW16_NUM2)&OVERFLOW_NUM2MASK)|((v1>>OVERFLOW16_NUM1)&OVERFLOW_NUM1MASK)]*/ /*((((dst^v1)&(v1^sub))>>15)&1)*/ ((obitss(v1,sub)>>15)&1)
#define OVERFLOWS32(v1,sub,dst) /*suboverflow[((dst>>OVERFLOW32_DST)&OVERFLOW_DSTMASK)|((sub>>OVERFLOW32_NUM2)&OVERFLOW_NUM2MASK)|((v1>>OVERFLOW32_NUM1)&OVERFLOW_NUM1MASK)]*/ /*((((dst^v1)&(v1^sub))>>31)&1)*/ ((obitss(v1,sub)>>31)&1)


#define bcbitsa(v1,v2) (((v1^v2)^dst)^((v1^dst)&(~(v1^v2))))
#define bcbitss(v1,v2) (((v1^v2)^dst)^((v1^dst)&(v1^v2)))

//General macros defining add/sub carry!
#define CARRYA8(v1,add,dst) ((bcbitsa(v1,add)>>7)&1)
#define CARRYA16(v1,add,dst) ((bcbitsa(v1,add)>>15)&1)
#define CARRYA32(v1,add,dst) ((bcbitsa(v1,add)>>31)&1)
#define CARRYS8(v1,sub,dst) ((bcbitss(v1,sub)>>7)&1)
#define CARRYS16(v1,sub,dst) ((bcbitss(v1,sub)>>15)&1)
#define CARRYS32(v1,sub,dst) ((bcbitss(v1,sub)>>31)&1)
//Aux variants:
#define AUXA8(v1,add,dst) ((bcbitsa(v1,add)>>3)&1)
#define AUXA16(v1,add,dst) ((bcbitsa(v1,add)>>3)&1)
#define AUXA32(v1,add,dst) ((bcbitsa(v1,add)>>3)&1)
#define AUXS8(v1,sub,dst) ((bcbitss(v1,sub)>>3)&1)
#define AUXS16(v1,sub,dst) ((bcbitss(v1,sub)>>3)&1)
#define AUXS32(v1,sub,dst) ((bcbitss(v1,sub)>>3)&1)

void flag_adcoa8(uint8_t v1, uint16_t add, uint16_t dst)
{
	FLAGW_CF(CARRYA8(v1,add,dst)); //Carry?
	FLAGW_OF(OVERFLOWA8(v1,add,dst)); //Overflow?
	FLAGW_AF(AUXA8(v1,add,dst)); //Adjust?
}

void flag_adcoa16(uint16_t v1, uint32_t add, uint32_t dst)
{
	FLAGW_CF(CARRYA16(v1,add,dst)); //Carry?
	FLAGW_OF(OVERFLOWA16(v1,add,dst)); //Overflow?
	FLAGW_AF(AUXA16(v1,add,dst)); //Adjust?
}

void flag_adcoa32(uint32_t v1, uint64_t add, uint64_t dst)
{
	FLAGW_CF(CARRYA32(v1,add,dst)); //Carry?
	FLAGW_OF(OVERFLOWA32(v1,add,dst)); //Overflow?
	FLAGW_AF(AUXA32(v1,add,dst)); //Adjust?
}

//Substract Carry, Overflow, Adjust logic
void flag_subcoa8(uint8_t v1, uint16_t sub, uint16_t dst)
{
	FLAGW_CF(CARRYS8(v1,sub,dst)); //Carry?
	FLAGW_OF(OVERFLOWS8(v1,sub,dst)); //Overflow?
	FLAGW_AF(AUXS8(v1,sub,dst)); //Adjust?
}

void flag_subcoa16(uint16_t v1, uint32_t sub, uint32_t dst)
{
	FLAGW_CF(CARRYS16(v1,sub,dst)); //Carry?
	FLAGW_OF(OVERFLOWS16(v1,sub,dst)); //Overflow?
	FLAGW_AF(AUXS16(v1,sub,dst)); //Adjust?
}

void flag_subcoa32(uint32_t v1, uint64_t sub, uint64_t dst)
{
	FLAGW_CF(CARRYS32(v1,sub,dst)); //Carry?
	FLAGW_OF(OVERFLOWS32(v1,sub,dst)); //Overflow?
	FLAGW_AF(AUXS32(v1,sub,dst)); //Adjust?
}

//Start of the externally used calls to calculate flags!

void flag_adc8(uint8_t v1, uint8_t v2, uint8_t v3)
{
	CPU[activeCPU].add=(uint16_t)v2;
	CPU[activeCPU].dst = (uint16_t)v1 + (CPU[activeCPU].add + (uint16_t)v3);
	flag_szp8((uint8_t)(CPU[activeCPU].dst&0xFF));
	flag_adcoa8(v1,(uint16_t)CPU[activeCPU].add,(uint16_t)CPU[activeCPU].dst);
}

void flag_adc16(uint16_t v1, uint16_t v2, uint16_t v3)
{
	CPU[activeCPU].add = (uint32_t)v2;
	CPU[activeCPU].dst = (uint32_t)v1 + (CPU[activeCPU].add + (uint32_t)v3);
	flag_szp16((uint16_t)CPU[activeCPU].dst);
	flag_adcoa16(v1,(uint32_t)CPU[activeCPU].add,(uint32_t)CPU[activeCPU].dst);
}

void flag_adc32(uint32_t v1, uint32_t v2, uint32_t v3)
{
	CPU[activeCPU].add = (uint64_t)v2;
	CPU[activeCPU].dst = (uint64_t)v1 + (CPU[activeCPU].add + (uint64_t)v3);
	flag_szp32((uint32_t)CPU[activeCPU].dst);
	flag_adcoa32(v1, CPU[activeCPU].add, CPU[activeCPU].dst);
}

void flag_add8(uint8_t v1, uint8_t v2)
{
	CPU[activeCPU].add = (uint16_t)v2;
	CPU[activeCPU].dst = (uint16_t)v1 + CPU[activeCPU].add;
	flag_szp8((uint8_t)(CPU[activeCPU].dst&0xFF));
	flag_adcoa8(v1,(uint16_t)CPU[activeCPU].add,(uint16_t)CPU[activeCPU].dst);
}

void flag_add16(uint16_t v1, uint16_t v2)
{
	CPU[activeCPU].add = (uint32_t)v2;
	CPU[activeCPU].dst = (uint32_t)v1 + CPU[activeCPU].add;
	flag_szp16((uint16_t)CPU[activeCPU].dst);
	flag_adcoa16(v1,(uint32_t)CPU[activeCPU].add,(uint32_t)CPU[activeCPU].dst);
}

void flag_add32(uint32_t v1, uint32_t v2)
{
	CPU[activeCPU].add = (uint64_t)v2;
	CPU[activeCPU].dst = (uint64_t)v1 + CPU[activeCPU].add;
	flag_szp32((uint32_t)CPU[activeCPU].dst);
	flag_adcoa32(v1, CPU[activeCPU].add, CPU[activeCPU].dst);
}

void flag_sbb8(uint8_t v1, uint8_t v2, uint8_t v3)
{
	CPU[activeCPU].sub = (uint16_t)v2;
	CPU[activeCPU].dst = (uint16_t)v1 - (CPU[activeCPU].sub + (uint16_t)v3);
	flag_szp8(CPU[activeCPU].dst & 0xFF);
	flag_subcoa8(v1,(uint16_t)CPU[activeCPU].sub,(uint16_t)CPU[activeCPU].dst);
}

void flag_sbb16(uint16_t v1, uint16_t v2, uint16_t v3)
{
	CPU[activeCPU].sub = (uint16_t)v2;
	CPU[activeCPU].dst = (uint32_t)v1 - (CPU[activeCPU].sub + (uint16_t)v3);
	flag_szp16(CPU[activeCPU].dst & 0xFFFF);
	flag_subcoa16(v1,(uint32_t)CPU[activeCPU].sub,(uint32_t)CPU[activeCPU].dst);
}

void flag_sbb32(uint32_t v1, uint32_t v2, uint32_t v3)
{
	CPU[activeCPU].sub = (uint32_t)v2;
	CPU[activeCPU].dst = (uint64_t)v1 - (CPU[activeCPU].sub + (uint32_t)v3);
	flag_szp32(CPU[activeCPU].dst & 0xFFFFFFFF);
	flag_subcoa32(v1, CPU[activeCPU].sub, CPU[activeCPU].dst);
}

void flag_sub8(uint8_t v1, uint8_t v2)
{
	CPU[activeCPU].sub = (uint16_t)v2;
	CPU[activeCPU].dst = (uint16_t)v1 - CPU[activeCPU].sub;
	flag_szp8(CPU[activeCPU].dst&0xFF);
	flag_subcoa8(v1,(uint16_t)CPU[activeCPU].sub,(uint16_t)CPU[activeCPU].dst);
}

void flag_sub16(uint16_t v1, uint16_t v2)
{
	CPU[activeCPU].sub = (uint32_t)v2;
	CPU[activeCPU].dst = (uint32_t)v1 - CPU[activeCPU].sub;
	flag_szp16(CPU[activeCPU].dst & 0xFFFF);
	flag_subcoa16(v1,(uint32_t)CPU[activeCPU].sub,(uint32_t)CPU[activeCPU].dst);
}

void flag_sub32(uint32_t v1, uint32_t v2)
{
	CPU[activeCPU].sub = (uint64_t)v2;
	CPU[activeCPU].dst = (uint64_t)v1 - CPU[activeCPU].sub;
	flag_szp32(CPU[activeCPU].dst & 0xFFFFFFFF);
	flag_subcoa32(v1, CPU[activeCPU].sub, CPU[activeCPU].dst);
}

void CPU_filterflags()
{
	//This applies to all processors:
	INLINEREGISTER uint_32 tempflags;
	tempflags = REG_EFLAGS; //Load the flags to set/clear!
	tempflags &= ~(8 | 32); //Clear bits 3&5!

	switch (EMULATED_CPU) //What CPU flags to emulate?
	{
	case CPU_8086:
	case CPU_NECV30:
		tempflags |= 0xF000; //High bits are stuck to 1!
		break;
	case CPU_80286:
		if (getcpumode() == CPU_MODE_REAL) //Real mode?
		{
			tempflags &= 0x0FFF; //Always set the high flags in real mode only!
		}
		else //Protected mode?
		{
			tempflags &= 0x7FFF; //Bit 15 is always cleared!
		}
		break;
	case CPU_80386:
		if (getcpumode() == CPU_MODE_REAL) //Real mode?
		{
			tempflags &= 0x37FFF; //Always set the high flags in real mode only!
		}
		else //Protected mode?
		{
			tempflags &= 0x37FFF; //Bit 15 is always cleared! AC is stuck to 0! All bits above AC are always cleared!
		}
		break;
	case CPU_80486:
		if (getcpumode() == CPU_MODE_REAL) //Real mode?
		{
			tempflags &= 0x277FFF; //Always set the high flags in real mode only!
		}
		else //Protected mode?
		{
			tempflags &= 0x277FFF; //Bit 15 is always cleared! Don't allow setting of the CPUID and larger flags! Allow toggling the CPUID flag too(it's supported)!
		}
		//Don't handle CPUID yet!
		tempflags &= ~0x200000; //Clear CPUID always!
		break;
	case CPU_PENTIUM:
	case CPU_PENTIUMPRO:
	case CPU_PENTIUM2:
		//Allow all bits to be set, except the one needed from the 80386+ identification(bit 15=0)!
		if (getcpumode() == CPU_MODE_REAL) //Real mode?
		{
			tempflags &= 0x3F7FFF; //Always set the high flags in real mode only!
		}
		else //Protected mode?
		{
			tempflags &= 0x3F7FFF;
		}
		break;
	default: //Unknown CPU?
		break;
	}
	tempflags |= 2; //Clear bit values 8&32(unmapped bits 3&5) and set bit value 2!
	REG_EFLAGS = tempflags; //Update the flags!
}