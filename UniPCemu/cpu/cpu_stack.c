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

#include "headers/cpu/cpu_stack.h"
#include "headers/cpu/cpu.h" //CPU register and descriptor support!
#include "headers/cpu/protection.h" //Protection support!
#include "headers/cpu/easyregs.h" //Easy register support!

uint_32 getstackaddrsizelimiter()
{
	return STACK_SEGMENT_DESCRIPTOR_B_BIT() ? 0xFFFFFFFF : 0xFFFF; //Stack address size!
}

//Changes in stack during PUSH and POP operations!
sbyte stack_pushchange(byte dword)
{
	return -(2 << dword); //Decrease!
}

sbyte stack_popchange(byte dword)
{
	return (2 << dword); //Decrease!
}

void stack_push(byte dword) //Push 16/32-bits to stack!
{
	if (STACK_SEGMENT_DESCRIPTOR_B_BIT()) //32-bits?
	{
		REG_ESP -= (2 << dword); //Decrease!
	}
	else //16-bits?
	{
		REG_SP -= (2 << dword); //Decrease!
	}
}

void stack_pop(byte dword) //Push 16/32-bits to stack!
{
	if (STACK_SEGMENT_DESCRIPTOR_B_BIT()) //32-bits?
	{
		REG_ESP += (2 << dword); //Increase!
	}
	else //16-bits?
	{
		REG_SP += (2 << dword); //Increase!
	}
}

byte checkStackAccess(uint_32 poptimes, word isPUSH, byte isdword) //How much do we need to POP from the stack?
{
	byte stackresult;
	uint_32 poptimesleft = poptimes; //Load the amount to check!
	uint_32 ESP = REG_ESP; //Load the stack pointer to verify!
	for (; poptimesleft;) //Anything left?
	{
		if (isPUSH)
		{
			ESP += stack_pushchange((isdword & 1)); //Apply the change in virtual (E)SP to check the next value!
		}

		//We're at least a word access!
		if ((isdword & 1) & (((~isdword) >> 1) & 1)) //When bit0=1 and bit 2=0(not forcing 16-bit operand size), use 32-bit accesses! This is required for segment PUSH/POP!
		{
			if ((stackresult = checkMMUaccess32(CPU_SEGMENT_SS, REG_SS, ESP & getstackaddrsizelimiter(), ((isPUSH ? 0 : 1) | 0x40) | (isPUSH & 0x300), getCPL(), !STACK_SEGMENT_DESCRIPTOR_B_BIT(), 0 | (8 << isdword)))!=0) //Error accessing memory?
			{
				return stackresult; //Abort on fault!
			}
		}
		else //Word?
		{
			if ((stackresult = checkMMUaccess16(CPU_SEGMENT_SS, REG_SS, (ESP & getstackaddrsizelimiter()), ((isPUSH ? 0 : 1) | 0x40) | (isPUSH & 0x300), getCPL(), !STACK_SEGMENT_DESCRIPTOR_B_BIT(), 0 | (8 << isdword)))!=0) //Error accessing memory?
			{
				return stackresult; //Abort on fault!
			}
		}
		if (isPUSH == 0)
		{
			ESP += stack_popchange((isdword & 1)); //Apply the change in virtual (E)SP to check the next value!
		}
		--poptimesleft; //One POP processed!
	}
	poptimesleft = poptimes; //Load the amount to check!
	ESP = REG_ESP; //Load the stack pointer to verify!
	for (; poptimesleft;) //Anything left?
	{
		if (isPUSH)
		{
			ESP += stack_pushchange((isdword & 1)); //Apply the change in virtual (E)SP to check the next value!
		}

		//We're at least a word access!
		if ((isdword & 1) & (((~isdword) >> 1) & 1)) //When bit0=1 and bit 2=0(not forcing 16-bit operand size), use 32-bit accesses! This is required for segment PUSH/POP!
		{
			if ((stackresult = checkMMUaccess32(CPU_SEGMENT_SS, REG_SS, ESP & getstackaddrsizelimiter(), (isPUSH ? 0 : 1) | 0xA0, getCPL(), !STACK_SEGMENT_DESCRIPTOR_B_BIT(), 0 | (8 << isdword)))!=0) //Error accessing memory?
			{
				return stackresult; //Abort on fault!
			}
		}
		else //Word
		{
			if ((stackresult = checkMMUaccess16(CPU_SEGMENT_SS, REG_SS, ESP & getstackaddrsizelimiter(), (isPUSH ? 0 : 1) | 0xA0, getCPL(), !STACK_SEGMENT_DESCRIPTOR_B_BIT(), 0 | (8 << isdword)))!=0) //Error accessing memory?
			{
				return stackresult; //Abort on fault!
			}
		}
		if (isPUSH == 0)
		{
			ESP += stack_popchange((isdword & 1)); //Apply the change in virtual (E)SP to check the next value!
		}
		--poptimesleft; //One POP processed!
	}
	return 0; //OK!
}

byte checkENTERStackAccess(uint_32 poptimes, byte isdword) //How much do we need to POP from the stack(using (E)BP)?
{
	byte stackresult;
	uint_32 poptimesleft = poptimes; //Load the amount to check!
	uint_32 EBP = REG_EBP; //Load the stack pointer to verify!
	for (; poptimesleft;) //Anything left?
	{
		EBP -= stack_popchange(isdword); //Apply the change in virtual (E)BP to check the next value(decrease in EBP)!

		//We're at least a word access!
		if (isdword) //DWord?
		{
			if ((stackresult = checkMMUaccess32(CPU_SEGMENT_SS, REG_SS, (EBP & getstackaddrsizelimiter()), 1 | 0x40, getCPL(), !STACK_SEGMENT_DESCRIPTOR_B_BIT(), 0 | (8 << isdword)))!=0) //Error accessing memory?
			{
				return stackresult; //Abort on fault!
			}
		}
		else //Word?
		{
			if ((stackresult = checkMMUaccess16(CPU_SEGMENT_SS, REG_SS, EBP & getstackaddrsizelimiter(), 1 | 0x40, getCPL(), !STACK_SEGMENT_DESCRIPTOR_B_BIT(), 0 | (8 << isdword)))!=0) //Error accessing memory?
			{
				return stackresult; //Abort on fault!
			}
		}
		--poptimesleft; //One POP processed!
	}
	poptimesleft = poptimes; //Load the amount to check!
	EBP = REG_EBP; //Load the stack pointer to verify!
	for (; poptimesleft;) //Anything left?
	{
		EBP -= stack_popchange(isdword); //Apply the change in virtual (E)BP to check the next value(decrease in EBP)!

		//We're at least a word access!
		if (isdword) //DWord?
		{
			if ((stackresult = checkMMUaccess32(CPU_SEGMENT_SS, REG_SS, (EBP & getstackaddrsizelimiter()), 1 | 0xA0, getCPL(), !STACK_SEGMENT_DESCRIPTOR_B_BIT(), 0 | (8 << isdword)))!=0) //Error accessing memory?
			{
				return stackresult; //Abort on fault!
			}
		}
		else //Word?
		{
			if ((stackresult = checkMMUaccess16(CPU_SEGMENT_SS, REG_SS, EBP & getstackaddrsizelimiter(), 1 | 0xA0, getCPL(), !STACK_SEGMENT_DESCRIPTOR_B_BIT(), 0 | (8 << isdword)))!=0) //Error accessing memory?
			{
				return stackresult; //Abort on fault!
			}
		}
		--poptimesleft; //One POP processed!
	}
	return 0; //OK!
}

//PUSH and POP values!

//Memory is the same as PSP: 1234h is 34h 12h, in stack terms reversed, because of top-down stack!

//Use below functions for the STACK!

void CPU_PUSH8(byte val, byte is32instruction) //Push Byte!
{
	word v = val; //Convert!
	CPU_PUSH16(&v, 0); //Push 16!
}

byte CPU_PUSH8_BIU(byte val, byte is32instruction) //Push Byte!
{
	word v = val; //Convert!
	return CPU_PUSH16_BIU(&v, is32instruction); //Push 16!
}

byte CPU_POP8(byte is32instruction)
{
	return (CPU_POP16(is32instruction) & 0xFF); //Give the result!
}

byte CPU_POP8_BIU(byte is32instruction) //Request an 8-bit POP from the BIU!
{
	return (CPU_POP16_BIU(is32instruction)); //Give the result: we're requesting from the BIU to POP one entry!
}

void CPU_PUSH16(word* val, byte is32instruction) //Push Word!
{
	if (EMULATED_CPU <= CPU_NECV30) //186- we push the decremented value of SP to the stack instead of the original value?
	{
		stack_push(0); //We're pushing a 16-bit value!
		MMU_ww(CPU_SEGMENT_SS, REG_SS, (REG_ESP & getstackaddrsizelimiter()), *val, !STACK_SEGMENT_DESCRIPTOR_B_BIT()); //Put value!
	}
	else //286+?
	{
		word oldval = *val; //Original value, saved before decrementing (E)SP!
		stack_push(is32instruction&1); //We're pushing a 16-bit or 32-bit value!
		if ((is32instruction & 1) & (((~is32instruction) >> 1) & 1)) //32-bit?
		{
			MMU_wdw(CPU_SEGMENT_SS, REG_SS, (REG_ESP & getstackaddrsizelimiter()), (uint_32)oldval, !STACK_SEGMENT_DESCRIPTOR_B_BIT()); //Put value!
		}
		else
		{
			MMU_ww(CPU_SEGMENT_SS, REG_SS, (REG_ESP & getstackaddrsizelimiter()), oldval, !STACK_SEGMENT_DESCRIPTOR_B_BIT()); //Put value!
		}
	}
}

byte CPU_PUSH16_BIU(word* val, byte is32instruction) //Push Word!
{
	if (EMULATED_CPU <= CPU_NECV30) //186- we push the decremented value of SP to the stack instead of the original value?
	{
		if (CPU[activeCPU].pushbusy == 0)
		{
			stack_push(0); //We're pushing a 16-bit value!
			CPU_commitStateESP(); //ESP has been changed within an instruction to be kept when not faulting!
			CPU[activeCPU].pushbusy = 1; //We're pending!
		}
		if (CPU_request_MMUww(CPU_SEGMENT_SS, (REG_ESP & getstackaddrsizelimiter()), *val, !STACK_SEGMENT_DESCRIPTOR_B_BIT())) //Request Put value!
		{
			CPU[activeCPU].pushbusy = 0; //We're not pending anymore!
			return 1;
		}
	}
	else //286+?
	{
		if (CPU[activeCPU].pushbusy == 0)
		{
			CPU[activeCPU].oldvalw = *val; //Original value, saved before decrementing (E)SP!
			stack_push(is32instruction&1); //We're pushing a 16-bit or 32-bit value!
			CPU_commitStateESP(); //ESP has been changed within an instruction to be kept when not faulting!
			CPU[activeCPU].pushbusy = 1; //We're pending!
		}
		if ((is32instruction & 1) & (((~is32instruction) >> 1) & 1)) //32-bit?
		{
			if (CPU_request_MMUwdw(CPU_SEGMENT_SS, (REG_ESP & getstackaddrsizelimiter()), CPU[activeCPU].oldvalw, !STACK_SEGMENT_DESCRIPTOR_B_BIT())) //Request Put value!
			{
				CPU[activeCPU].pushbusy = 0; //We're not pending anymore!
				return 1;
			}
		}
		else
		{
			if (CPU_request_MMUww(CPU_SEGMENT_SS, (REG_ESP & getstackaddrsizelimiter()), CPU[activeCPU].oldvalw, !STACK_SEGMENT_DESCRIPTOR_B_BIT())) //Request Put value!
			{
				CPU[activeCPU].pushbusy = 0; //We're not pending anymore!
				return 1;
			}
		}
	}
	return 0; //Not ready yet!
}

word CPU_POP16(byte is32instruction) //Pop Word!
{
	word result;
	result = MMU_rw(CPU_SEGMENT_SS, REG_SS, (REG_ESP & getstackaddrsizelimiter()), 0, !STACK_SEGMENT_DESCRIPTOR_B_BIT()); //Get value!
	stack_pop(/*CODE_SEGMENT_DESCRIPTOR_D_BIT()*/ is32instruction&1); //We're popping a 16-bit value!
	return result; //Give the result!
}

byte CPU_POP16_BIU(byte is32instruction) //Pop Word!
{
	byte result;
	result = CPU_request_MMUrw(CPU_SEGMENT_SS, (REG_ESP & getstackaddrsizelimiter()), !STACK_SEGMENT_DESCRIPTOR_B_BIT()); //Get value!
	if (result) //Requested?
	{
		stack_pop(/*CODE_SEGMENT_DESCRIPTOR_D_BIT()*/ is32instruction&1); //We're popping a 16-bit value!
		CPU_commitStateESP(); //ESP has been changed within an instruction to be kept when not faulting!
	}
	return result; //Give the result!
}

byte CPU_PUSH32_BIU(uint_32* val) //Push DWord!
{
	if (EMULATED_CPU < CPU_80386) //286-?
	{
		if (CPU[activeCPU].pushbusy == 0)
		{
			stack_push(0); //We're pushing a 16-bit value!
			CPU_commitStateESP(); //ESP has been changed within an instruction to be kept when not faulting!
			CPU[activeCPU].pushbusy = 1; //We're pending!
		}
		if (CPU_request_MMUww(CPU_SEGMENT_SS, (REG_ESP & getstackaddrsizelimiter()), *val, !STACK_SEGMENT_DESCRIPTOR_B_BIT())) //Request Put value!
		{
			CPU[activeCPU].pushbusy = 0; //We're not pending anymore!
			return 1;
		}
	}
	else //386+?
	{
		if (CPU[activeCPU].pushbusy == 0)
		{
			CPU[activeCPU].oldvald = *val; //Original value, saved before decrementing (E)SP!
			stack_push(/*CODE_SEGMENT_DESCRIPTOR_D_BIT()*/ 1); //We're pushing a 16-bit or 32-bit value!
			CPU_commitStateESP(); //ESP has been changed within an instruction to be kept when not faulting!
			CPU[activeCPU].pushbusy = 1; //We're pending!
		}
		if (CPU_request_MMUwdw(CPU_SEGMENT_SS, (REG_ESP & getstackaddrsizelimiter()), CPU[activeCPU].oldvald, !STACK_SEGMENT_DESCRIPTOR_B_BIT())) //Request Put value!
		{
			CPU[activeCPU].pushbusy = 0; //We're not pending anymore!
			return 1;
		}
	}
	return 0; //Not ready!
}

void CPU_PUSH32(uint_32* val) //Push DWord!
{
	if (EMULATED_CPU < CPU_80386) //286-?
	{
		stack_push(0); //We're pushing a 32-bit value!
		MMU_ww(CPU_SEGMENT_SS, REG_SS, (REG_ESP & getstackaddrsizelimiter()), *val, !STACK_SEGMENT_DESCRIPTOR_B_BIT()); //Put value!
	}
	else //386+?
	{
		uint_32 oldval = *val; //Old value!
		stack_push(/*CODE_SEGMENT_DESCRIPTOR_D_BIT()*/ 1); //We're pushing a 32-bit value!
		MMU_wdw(CPU_SEGMENT_SS, REG_SS, (REG_ESP & getstackaddrsizelimiter()), oldval, !STACK_SEGMENT_DESCRIPTOR_B_BIT()); //Put value!
	}
}

uint_32 CPU_POP32() //Full stack used!
{
	uint_32 result;
	result = MMU_rdw(CPU_SEGMENT_SS, REG_SS, REG_ESP & getstackaddrsizelimiter(), 0, !STACK_SEGMENT_DESCRIPTOR_B_BIT()); //Get value!
	stack_pop(/*CODE_SEGMENT_DESCRIPTOR_D_BIT()*/ 1); //We're popping a 32-bit value!
	return result; //Give the result!
}

byte CPU_POP32_BIU() //Full stack used!
{
	byte result;
	result = CPU_request_MMUrdw(CPU_SEGMENT_SS, REG_ESP & getstackaddrsizelimiter(), !STACK_SEGMENT_DESCRIPTOR_B_BIT()); //Get value!
	if (result) //Requested?
	{
		stack_pop(/*CODE_SEGMENT_DESCRIPTOR_D_BIT()*/ 1); //We're popping a 32-bit value!
		CPU_commitStateESP(); //ESP has been changed within an instruction to be kept when not faulting!
	}
	return result; //Give the result!
}
