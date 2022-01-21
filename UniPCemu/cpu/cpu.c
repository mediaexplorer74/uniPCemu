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

#define IS_CPU
#include "headers/cpu/cpu.h"
#include "headers/cpu/interrupts.h"
#include "headers/cpu/mmu.h"
#include "headers/support/signedness.h" //CPU support!
#include "headers/cpu/cpu_OP8086.h" //8086 comp.
#include "headers/cpu/cpu_OPNECV30.h" //unkOP comp.
#include "headers/emu/debugger/debugger.h" //Debugger support!
#include "headers/emu/gpu/gpu.h" //Start&StopVideo!
#include "headers/cpu/cb_manager.h" //CB support!
#include "headers/cpu/protection.h"
#include "headers/cpu/cpu_OP80286.h" //0F opcode support!
#include "headers/support/zalloc.h" //For allocating registers etc.
#include "headers/support/locks.h" //Locking support!
#include "headers/cpu/modrm.h" //MODR/M support!
#include "headers/emu/emucore.h" //Needed for CPU reset handler!
#include "headers/mmu/mmuhandler.h" //bufferMMU, MMU_resetaddr and flushMMU support!
#include "headers/cpu/cpu_pmtimings.h" //80286+ timings lookup table support!
#include "headers/cpu/cpu_opcodeinformation.h" //Opcode information support!
#include "headers/cpu/easyregs.h" //Easy register support!
#include "headers/cpu/protecteddebugging.h" //Protected debugging support!
#include "headers/cpu/biu.h" //BIU support!
#include "headers/cpu/cpu_execution.h" //Execution support!
#include "headers/support/log.h" //Logging support!
#include "headers/cpu/flags.h" //Flag support for IMUL!
#include "headers/support/log.h" //Logging support!
#include "headers/cpu/easyregs.h" //Easy register support!
#include "headers/hardware/pic.h" //APIC support on Pentium and up!

//Waitstate delay on 80286.
#define CPU286_WAITSTATE_DELAY 1

//Enable this define to use cycle-accurate emulation for supported CPUs!
#define CPU_USECYCLES

//Enable this to enable logging of unfinished BIU instructions leaving data in the BIU FIFO response buffer!
//#define DEBUG_BIUFIFO

//Save the last instruction address and opcode in a backup?
#define CPU_SAVELAST

byte activeCPU = 0; //What CPU is currently active?
byte emulated_CPUtype = 0; //The emulated CPU!

CPU_type CPU[MAXCPUS]; //The CPU data itself!

uint_32 MSRstorage; //How much storage is used?
uint_32 MSRnumbers[MAPPEDMSRS*2]; //All possible MSR numbers!
uint_32 MSRmasklow[MAPPEDMSRS*2]; //Low mask!
uint_32 MSRmaskhigh[MAPPEDMSRS*2]; //High mask!
uint_32 MSRmaskwritelow_readonly[MAPPEDMSRS*2]; //Low mask for writes changing data erroring out!
uint_32 MSRmaskwritehigh_readonly[MAPPEDMSRS*2]; //High mask for writes changing data erroring out!
uint_32 MSRmaskreadlow_writeonly[MAPPEDMSRS*2]; //Low mask for reads changing data!
uint_32 MSRmaskreadhigh_writeonly[MAPPEDMSRS*2]; //High mask for reads changing data!

void CPU_initMSRnumbers()
{
	uint_32 MSRcounter;
	memset(&MSRnumbers, 0, sizeof(MSRnumbers)); //Default to unmapped!
	MSRstorage = 0; //Default: first entry!
	if (EMULATED_CPU < CPU_PENTIUM) //No MSRs available?
	{
		return; //No MSR numbers available!
	}
	MSRnumbers[0] = ++MSRstorage; //MSR xxh!
	MSRnumbers[1] = ++MSRstorage; //MSR xxh!
	if (EMULATED_CPU==CPU_PENTIUM) //Pentium-only?
	{
		MSRnumbers[2] = ++MSRstorage; //MSR xxh!
		for (MSRcounter = 4; MSRcounter <= 0x9; ++MSRcounter)
		{
			MSRnumbers[MSRcounter] = ++MSRstorage; //MSR xxh!
		}
		for (MSRcounter = 0xB; MSRcounter <= 0xE; ++MSRcounter)
		{
			MSRnumbers[MSRcounter] = ++MSRstorage; //MSR xxh!
		}
	}
	MSRnumbers[0x10] = ++MSRstorage; //MSR xxh!
	if (EMULATED_CPU==CPU_PENTIUM) //Pentium-only?
	{
		for (MSRcounter = 0x11; MSRcounter <= 0x14; ++MSRcounter)
		{
			MSRnumbers[MSRcounter] = ++MSRstorage; //MSR xxh!
		}
		//High MSRs!
		for (MSRcounter = 0; MSRcounter < 0x20; ++MSRcounter)
		{
			MSRnumbers[MSRcounter+MAPPEDMSRS] = ++MSRstorage; //MSR xxh!
		}
	}
	if (EMULATED_CPU>=CPU_PENTIUMPRO) //Pentium Pro and up MSRs!
	{
		MSRnumbers[0x17] = ++MSRstorage; //MSR 17h?
		MSRnumbers[0x1B] = ++MSRstorage; //MSR xxh!
		MSRnumbers[0x2A] = ++MSRstorage; //MSR xxh!
		MSRnumbers[0x33] = ++MSRstorage; //MSR 33h?
		MSRnumbers[0x79] = ++MSRstorage; //MSR xxh!
	}
	if (EMULATED_CPU >= CPU_PENTIUM2) //Pentium II and up MSRs!
	{
		MSRnumbers[0x88] = ++MSRstorage; //MSR xxh!
		MSRnumbers[0x89] = ++MSRstorage; //MSR xxh!
		MSRnumbers[0x8A] = ++MSRstorage; //MSR xxh!
	}
	if (EMULATED_CPU>=CPU_PENTIUMPRO) //Pentium Pro and up MSRs!
	{
		MSRnumbers[0x8B] = ++MSRstorage; //MSR xxh!
		MSRnumbers[0xC1] = ++MSRstorage; //MSR xxh!
		MSRnumbers[0xC2] = ++MSRstorage; //MSR xxh!
		MSRnumbers[0xFE] = ++MSRstorage; //MSR xxh!
	}

	if (EMULATED_CPU >= CPU_PENTIUM2) //Pentium II MSRs!
	{
		MSRnumbers[0x88] = ++MSRstorage; //MSR xxh!
		MSRnumbers[0x89] = ++MSRstorage; //MSR xxh!
		MSRnumbers[0x8A] = ++MSRstorage; //MSR xxh!
		MSRnumbers[0x116] = ++MSRstorage; //MSR xxh!
	}

	if (EMULATED_CPU >= CPU_PENTIUM2) //Pentium II MSRs!
	{
		MSRnumbers[0x118] = ++MSRstorage; //MSR xxh!
	}

	if (EMULATED_CPU >= CPU_PENTIUMPRO) //Pentium Pro MSRs?
	{
		MSRnumbers[0x119] = ++MSRstorage; //MSR xxh!
	}
	if (EMULATED_CPU >= CPU_PENTIUM2) //Pentium II MSRs!
	{
		MSRnumbers[0x11A] = ++MSRstorage; //MSR xxh!
		MSRnumbers[0x11B] = ++MSRstorage; //MSR xxh!
		MSRnumbers[0x11E] = ++MSRstorage; //MSR xxh!
	}
	//Remainder of the MSRs!
	if (EMULATED_CPU>=CPU_PENTIUMPRO) //Pentium Pro and up MSRs!
	{
		MSRnumbers[0x179] = ++MSRstorage; //MSR xxh!
		MSRnumbers[0x17A] = ++MSRstorage; //MSR xxh!
		MSRnumbers[0x17B] = ++MSRstorage; //MSR xxh!
		MSRnumbers[0x186] = ++MSRstorage; //MSR xxh!
		MSRnumbers[0x187] = ++MSRstorage; //MSR xxh!
		MSRnumbers[0x1D9] = ++MSRstorage; //MSR xxh!
		MSRnumbers[0x1DB] = ++MSRstorage; //MSR xxh!
		MSRnumbers[0x1DC] = ++MSRstorage; //MSR xxh!
		MSRnumbers[0x1DD] = ++MSRstorage; //MSR xxh!
		MSRnumbers[0x1DE] = ++MSRstorage; //MSR xxh!
		MSRnumbers[0x1E0] = ++MSRstorage; //MSR xxh!
		for (MSRcounter = 0; MSRcounter < 0x10; ++MSRcounter)
		{
			MSRnumbers[0x200+MSRcounter] = ++MSRstorage; //MSR xxh!
		}
		MSRnumbers[0x250] = ++MSRstorage; //MSR xxh!
		MSRnumbers[0x258] = ++MSRstorage; //MSR xxh!
		MSRnumbers[0x259] = ++MSRstorage; //MSR xxh!
		for (MSRcounter = 0; MSRcounter < 0x8; ++MSRcounter)
		{
			MSRnumbers[0x268+MSRcounter] = ++MSRstorage; //MSR xxh!
		}
		MSRnumbers[0x2FF] = ++MSRstorage; //MSR xxh!
		for (MSRcounter = 0; MSRcounter < 0x8; ++MSRcounter)
		{
			MSRnumbers[0x268] = ++MSRstorage; //MSR xxh!
		}
		for (MSRcounter = 0; MSRcounter < 0x14; ++MSRcounter)
		{
			MSRnumbers[0x400+MSRcounter] = ++MSRstorage; //MSR xxh!
		}
	}
}
void CPU_initMSRs()
{
	CPU_initMSRnumbers(); //Initialize the number mapping!
	if (MSRstorage > NUMITEMS(CPU[activeCPU].registers->genericMSR)) //Too many items?
	{
		raiseError("cpu","Too many MSRs allocated! Amount: %i, limit: %i",MSRstorage, NUMITEMS(CPU[activeCPU].registers->genericMSR)); //Log it!
		return; //Abort!
	}
	//MSRmasklow/high is what is able to be written by the CPU! Otherwise erroring out!
	memset(&MSRmasklow, ~0, sizeof(MSRmasklow)); //Allow all bits!
	memset(&MSRmaskhigh, ~0, sizeof(MSRmaskhigh)); //Allow all bits!
	memset(&MSRmaskwritelow_readonly, 0, sizeof(MSRmaskwritelow_readonly)); //No read-only bits!
	memset(&MSRmaskwritehigh_readonly, 0, sizeof(MSRmaskwritehigh_readonly)); //No read-only bits!
	memset(&MSRmaskreadlow_writeonly, 0, sizeof(MSRmaskreadlow_writeonly)); //No write-only bits!
	memset(&MSRmaskreadhigh_writeonly, 0, sizeof(MSRmaskreadhigh_writeonly)); //No write-only bits!
	if (EMULATED_CPU >= CPU_PENTIUMPRO) //Pentium Pro and up?
	{
		MSRmasklow[MSRnumbers[0x1B] - 1] = ~0x6FF; //APICBASE mask. Invalid bits are bits 0-7, 9, 10!
		MSRmaskwritelow_readonly[MSRnumbers[0x1B] - 1] = 0x100; //ROM bit!
		if (EMULATED_CPU <= CPU_PENTIUMPRO) //Pentium Pro and below?
		{
			MSRmaskhigh[MSRnumbers[0x1B] - 1] = 0x0; //APICBASE mask. Invalid bits are bits MAXPHYSADDR-63 are invalid. MAXPHYSADDR is 32 in this case(although 35-bit physical addresses are supported!)
		}
		else //Pentium II?
		{
			MSRmaskhigh[MSRnumbers[0x1B] - 1] = 0xF; //APICBASE mask. Invalid bits are bits MAXPHYSADDR-63 are invalid. MAXPHYSADDR is 35 in this case(35-bit physical addresses are supported!)
		}
	}
	if (EMULATED_CPU==CPU_PENTIUM) //Pentium-only?
	{
		MSRmasklow[MSRnumbers[0x14] - 1] = 0; //ROM 0
		MSRmaskhigh[MSRnumbers[0x14] - 1] = 0; //ROM 0
		MSRmaskwritelow_readonly[MSRnumbers[0x00+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskwritehigh_readonly[MSRnumbers[0x00+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskwritelow_readonly[MSRnumbers[0x01+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskwritehigh_readonly[MSRnumbers[0x01+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskwritelow_readonly[MSRnumbers[0x14+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskwritehigh_readonly[MSRnumbers[0x14+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskwritelow_readonly[MSRnumbers[0x18+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskwritehigh_readonly[MSRnumbers[0x18+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskwritelow_readonly[MSRnumbers[0x19+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskwritehigh_readonly[MSRnumbers[0x19+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskwritelow_readonly[MSRnumbers[0x1A+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskwritehigh_readonly[MSRnumbers[0x1A+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskwritelow_readonly[MSRnumbers[0x1C+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskwritehigh_readonly[MSRnumbers[0x1C+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		//Write-only low ones!
		MSRmaskreadlow_writeonly[MSRnumbers[0x02] - 1] = ~0; //They're all readonly!
		MSRmaskreadhigh_writeonly[MSRnumbers[0x02] - 1] = ~0; //They're all readonly!
		MSRmaskreadlow_writeonly[MSRnumbers[0x07] - 1] = ~0; //They're all readonly!
		MSRmaskreadhigh_writeonly[MSRnumbers[0x07] - 1] = ~0; //They're all readonly!
		MSRmaskreadlow_writeonly[MSRnumbers[0x0D] - 1] = ~0; //They're all readonly!
		MSRmaskreadhigh_writeonly[MSRnumbers[0x0D] - 1] = ~0; //They're all readonly!
		MSRmaskreadlow_writeonly[MSRnumbers[0x0E] - 1] = ~0; //They're all readonly!
		MSRmaskreadhigh_writeonly[MSRnumbers[0x0E] - 1] = ~0; //They're all readonly!
		//High write-only!
		MSRmaskreadlow_writeonly[MSRnumbers[0x02+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskreadhigh_writeonly[MSRnumbers[0x02+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskreadlow_writeonly[MSRnumbers[0x07+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskreadhigh_writeonly[MSRnumbers[0x07+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskreadlow_writeonly[MSRnumbers[0x0D+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskreadhigh_writeonly[MSRnumbers[0x0D+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskreadlow_writeonly[MSRnumbers[0x0E + MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskreadhigh_writeonly[MSRnumbers[0x0E + MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskreadlow_writeonly[MSRnumbers[0x0F+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskreadhigh_writeonly[MSRnumbers[0x0F+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		//Weird always 0 ones or unimplemented?
		MSRmaskwritelow_readonly[MSRnumbers[0x03+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskwritehigh_readonly[MSRnumbers[0x03+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskwritelow_readonly[MSRnumbers[0x0A+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskwritehigh_readonly[MSRnumbers[0x0A+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskwritelow_readonly[MSRnumbers[0x15+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskwritehigh_readonly[MSRnumbers[0x15+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskwritelow_readonly[MSRnumbers[0x16+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskwritehigh_readonly[MSRnumbers[0x16+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskwritelow_readonly[MSRnumbers[0x17+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskwritehigh_readonly[MSRnumbers[0x17+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskwritelow_readonly[MSRnumbers[0x1C+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
		MSRmaskwritehigh_readonly[MSRnumbers[0x1C+MAPPEDMSRS] - 1] = ~0; //They're all readonly!
	}
	if (EMULATED_CPU>=CPU_PENTIUMPRO) //Pro and up?
	{
		MSRmasklow[MSRnumbers[0x2A] - 1] = 0x1F | (0x1F << 6) | (0xF << 10) | (3<<16) | (3<<20) | (7<<22); //EBL_CR_POWERON
		if (EMULATED_CPU >= CPU_PENTIUM2) //Pentium 2 and up?
		{
			MSRmasklow[MSRnumbers[0x2A] - 1] |= (0x7 << 22); //Writeable, but ROM!
			MSRmasklow[MSRnumbers[0x2A] - 1] |= (1 << 26); //Writeable, but ROM!
			MSRmasklow[MSRnumbers[0x2A] - 1] &= ~(1 << 25); //Reserved!
		}
		MSRmaskhigh[MSRnumbers[0x2A] - 1] = 0; //EBL_CR_POWERON
		MSRmaskwritelow_readonly[MSRnumbers[0x2A] - 1] = MSRmasklow[MSRnumbers[0x2A] - 1]&~(0x1F|(3<<6)); //They're all readonly, except the first 6 bits that are defined!
		MSRmasklow[MSRnumbers[0x186] - 1] = ~0x200000; //EVNTSEL0 mask
		MSRmaskhigh[MSRnumbers[0x186] - 1] = 0; //EVNTSEL0 mask
		if (EMULATED_CPU == CPU_PENTIUMPRO)
		{
			MSRmasklow[MSRnumbers[0x186] - 1] = (~0x600000)|(1<<21); //EVNTSEL1 mask
		}
		else
		{
			MSRmasklow[MSRnumbers[0x186] - 1] = ~0; //EVNTSEL1 mask: bit 21 only!
		}
		MSRmaskhigh[MSRnumbers[0x186] - 1] = 0; //EVNTSEL1 mask
		MSRmasklow[MSRnumbers[0x1D9] - 1] = (0x3<<8)|0x7F; //DEBUGCTLMSR mask
		MSRmaskhigh[MSRnumbers[0x1D9] - 1] = 0; //DEBUGCTLMSR mask
		if (EMULATED_CPU >= CPU_PENTIUM2)
		{
			MSRmasklow[MSRnumbers[0x1D9] - 1] |= 0xFFFF & (0x7F << 7); //DEBUGCTLMSR mask 13:7 are reserved!
		}
		MSRmasklow[MSRnumbers[0x1E0] - 1] = 2; //ROB_CR_BKUPTMPDR6 mask
		if (EMULATED_CPU >= CPU_PENTIUM2)
		{
			MSRmasklow[MSRnumbers[0x1E0] - 1] |= 4; //ROB_CR_BKUPTMPDR6 mask
		}
		MSRmaskhigh[MSRnumbers[0x1E0] - 1] = 0; //ROB_CR_BKUPTMPDR mask
		MSRmasklow[MSRnumbers[0x2FF] - 1] = 0x3|(3<<10); //MTRRdefType mask
		MSRmaskhigh[MSRnumbers[0x2FF] - 1] = 0; //MTRRdefType mask
		MSRmasklow[MSRnumbers[0x401] - 1] = ~0; //MC0_STATUS mask
		MSRmaskhigh[MSRnumbers[0x401] - 1] = 0; //MC0_STATUS mask
		MSRmasklow[MSRnumbers[0x405] - 1] = MSRmasklow[MSRnumbers[0x401] - 1]; //MC1_STATUS mask
		MSRmaskhigh[MSRnumbers[0x405] - 1] = MSRmaskhigh[MSRnumbers[0x401] - 1]; //MC1_STATUS mask
		MSRmasklow[MSRnumbers[0x409] - 1] = MSRmasklow[MSRnumbers[0x401] - 1]; //MC1_STATUS mask
		MSRmaskhigh[MSRnumbers[0x409] - 1] = MSRmaskhigh[MSRnumbers[0x401] - 1]; //MC1_STATUS mask
		MSRmasklow[MSRnumbers[0x40D] - 1] = MSRmasklow[MSRnumbers[0x401] - 1]; //MC1_STATUS mask
		MSRmaskhigh[MSRnumbers[0x40D] - 1] = MSRmaskhigh[MSRnumbers[0x401] - 1]; //MC1_STATUS mask
		MSRmasklow[MSRnumbers[0x411] - 1] = MSRmasklow[MSRnumbers[0x401] - 1]; //MC1_STATUS mask
		MSRmaskhigh[MSRnumbers[0x411] - 1] = MSRmaskhigh[MSRnumbers[0x401] - 1]; //MC1_STATUS mask
	}
	if (EMULATED_CPU >= CPU_PENTIUM2) //Pentium 2 has additional MSRs?
	{
		MSRmasklow[MSRnumbers[0x116] - 1] = ~0x7; //BBL_CR_ADDR mask
		MSRmaskhigh[MSRnumbers[0x116] - 1] = 0; //BBL_CR_ADDR mask
		MSRmasklow[MSRnumbers[0x119] - 1] = 0x1F|(0x3<<5)|(0x3<<8)|(0x3<<10)|(0x3<<12)|(1<<16)|(1<<18); //BBL_CR_CTL mask
		MSRmaskhigh[MSRnumbers[0x119] - 1] = 0; //BBL_CR_CTL mask
		MSRmasklow[MSRnumbers[0x11E] - 1] = 0x7FFFF|(0xF<<20)|(1<<25); //BBL_CR_CTL3 mask
		MSRmaskhigh[MSRnumbers[0x11E] - 1] = 0; //BBL_CR_CTL3 mask
		MSRmaskwritelow_readonly[MSRnumbers[0x11E] - 1] = (0xF<<9)|(1<<23)|(1<<25); //Readonly bits!
	}
}

//CPU timings information
extern CPU_OpcodeInformation CPUOpcodeInformationPrecalcs[CPU_MODES][0x200]; //All normal and 0F CPU timings, which are used, for all modes available!

//More info about interrupts: http://www.bioscentral.com/misc/interrupts.htm#
//More info about interrupts: http://www.bioscentral.com/misc/interrupts.htm#

#ifdef CPU_USECYCLES
byte CPU_useCycles = 0; //Enable normal cycles for supported CPUs when uncommented?
#endif

/*

checkSignedOverflow: Checks if a signed overflow occurs trying to store the data.
unsignedval: The unsigned, positive value
calculatedbits: The amount of bits that's stored in unsignedval.
bits: The amount of bits to store in.
convertedtopositive: The unsignedval is a positive conversion from a negative result, so needs to be converted back.

*/

//Based on http://www.ragestorm.net/blogs/?p=34

byte checkSignedOverflow(uint_64 unsignedval, byte calculatedbits, byte bits, byte convertedtopositive)
{
	uint_64 maxpositive,maxnegative;
	maxpositive = ((1ULL<<(bits-1))-1); //Maximum positive value we can have!
	maxnegative = (1ULL<<(bits-1)); //The highest value we cannot set and get past when negative!
	if (unlikely(((unsignedval>maxpositive) && (convertedtopositive==0)) || ((unsignedval>maxnegative) && (convertedtopositive)))) //Signed underflow/overflow on unsinged conversion?
	{
		return 1; //Underflow/overflow detected!
	}
	return 0; //OK!
}

uint_64 signextend64(uint_64 val, byte bits)
{
	INLINEREGISTER uint_64 highestbit,bitmask;
	bitmask = highestbit = (1ULL<<(bits-1)); //Sign bit to use!
	bitmask <<= 1; //Shift to bits!
	--bitmask; //Mask for the used bits!
	if (likely(val&highestbit)) //Sign to extend?
	{
		val |= (~bitmask); //Sign extend!
		return val; //Give the result!
	}
	val &= bitmask; //Mask high bits off!
	return val; //Give the result!
}

byte CPUID_mode = 0; //CPUID mode! 0=Modern mode, 1=Limited to leaf 1, 2=Set to DX on start

void CPU_CPUID()
{
	if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
	{
		modrm_generateInstructionTEXT("CPUID", 0, 0, PARAM_NONE);
	}
	byte leaf;
	leaf = REG_EAX; //What leaf?
	if (CPUID_mode == 1) //Limited to leaf 1 or 2?
	{
		switch (EMULATED_CPU)
		{
		default: //Lowest decominator!
		case CPU_80486: //80486?
		case CPU_PENTIUM: //Pentium?
			if (leaf > 1) leaf = 1; //Limit to leaf 1!
			break;
		case CPU_PENTIUMPRO: //Pentium Pro?
		case CPU_PENTIUM2: //Pentium 2?
			if (leaf > 2) leaf = 2; //Limit to leaf 2!
			break;
		}
	}
	//Otherwise, DX mode or unlimited!
	switch (leaf)
	{
	case 0x00: //Highest function parameter!
		if (CPUID_mode == 2) //DX?
		{
			goto handleCPUIDDX; //DX on start!
		}
		switch (EMULATED_CPU)
		{
		default: //Lowest decominator!
		case CPU_80486: //80486?
		case CPU_PENTIUM: //Pentium?
			REG_EAX = 1; //One function parameters supported!
			break;
		case CPU_PENTIUMPRO: //Pentium Pro?
		case CPU_PENTIUM2: //Pentium 2?
			REG_EAX = 2; //Maximum 2 parameters supported!
			break;
		}
		//GenuineIntel!
		REG_EBX = 0x756e6547;
		REG_EDX = 0x49656e69;
		REG_ECX = 0x6c65746e;
		break;
	case 0x01: //Standard level 1: Processor type/family/model/stepping and Feature flags
		if (CPUID_mode == 2) //DX?
		{
			goto handleCPUIDDX; //DX on start!
		}
		switch (EMULATED_CPU)
		{
		default: //Lowest decominator!
		case CPU_80486: //80486?
			//Information based on http://www.hugi.scene.org/online/coding/hugi%2016%20-%20corawhd4.htm
			REG_EAX = (0 << 0xC); //Type: 00b=Primary processor
			REG_EAX |= (4 << 8); //Family: 80486/AMD 5x86/Cyrix 5x86
			REG_EAX |= (2 << 4); //Model: i80486SX
			REG_EAX |= (0 << 0); //Processor stepping: unknown with 80486SX!
			REG_EBX = 0; //Unknown, leave zeroed!
			break;
		case CPU_PENTIUM: //Pentium?
			REG_EAX = (0 << 0xC); //Type: 00b=Primary processor
			REG_EAX |= (5 << 8); //Family: Pentium(what we're identifying as), Nx586(what we're effectively emulating), Cx6x86, K5/K6, C6, mP6
			REG_EAX |= (1 << 4); //Model: P5(what we're approximating, without FPU). Maybe should be 0(Nx586) instead of 1(P5), since we're not emulating a FPU.
			REG_EAX |= (0 << 0); //Processor stepping: unknown with 80486SX!
			REG_EBX = 0; //Unknown, leave zeroed!
			break;
		case CPU_PENTIUMPRO: //Pentium Pro?
			REG_EAX = (0 << 0xC); //Type: 00b=Primary processor
			REG_EAX |= (6 << 8); //Family: Pentium Pro(what we're identifying as), Nx586(what we're effectively emulating), Cx6x86, K5/K6, C6, mP6
			REG_EAX |= (1 << 4); //Model: P5(what we're approximating, without FPU). Maybe should be 0(Nx586) instead of 1(P5), since we're not emulating a FPU.
			REG_EAX |= (0 << 0); //Processor stepping: Pentium pro(0)!
			REG_EBX = 0; //Unknown, leave zeroed!
			break;
		case CPU_PENTIUM2: //Pentium 2?
			REG_EAX = (0 << 0xC); //Type: 00b=Primary processor
			REG_EAX |= (6 << 8); //Family: Pentium Pro(what we're identifying as), Nx586(what we're effectively emulating), Cx6x86, K5/K6, C6, mP6
			REG_EAX |= (3 << 4); //Model: Pentium II(3, what we're approximating, without FPU). Maybe should be 0(Nx586) instead of 1(P5), since we're not emulating a FPU.
			REG_EAX |= (3 << 0); //Processor stepping: Pentium II(3)!
			REG_EBX = 0; //Unknown, leave zeroed!
			break;
		}
		//Calculate features!
		//Load default extensions!
		REG_EDX = 0; //No extensions!
		REG_ECX = 0; //No features!

		//Now, load what the CPU can do! This is incremental by CPU generation!
		switch (EMULATED_CPU)
		{
		case CPU_PENTIUM2: //Pentium 2?
			REG_EDX |= 0x0800; //Just SYSENTER/SYSEXIT have been added!
		case CPU_PENTIUMPRO: //Pentium Pro?
			REG_EDX |= 0xA240; //Just CMOV(but not FCMOV, since the NPU feature bit(bit 0) isn't set), PAE and Page Global Enable and APIC have been implemented!
		case CPU_PENTIUM: //Pentium?
			REG_EDX |= 0x13E; //Just VME, Debugging Extensions, Page Size Extensions, TSC, MSR, CMPXCHG8 have been implemented!
		default: //Lowest decominator!
		case CPU_80486: //80486?
			//Information based on http://www.hugi.scene.org/online/coding/hugi%2016%20-%20corawhd4.htm
			//Nothing added!
			break;
		}
		break;
	case 0x02: //Cache and TLB information
		if (CPUID_mode == 2) //DX?
		{
			goto handleCPUIDDX; //DX on start!
		}
		switch (EMULATED_CPU)
		{
		case CPU_PENTIUMPRO: //Pentium Pro?
			REG_EAX = 0x01; //Only report 4KB pages!
			REG_EBX = 0; //Not reporting!
			REG_ECX = 0; //Not reporting!
			REG_EDX = 0; //Not reporting!
			break;
		case CPU_PENTIUM2: //Pentium 2?
			REG_EAX = 0x01; //Only report 4KB pages!
			REG_EBX = 0; //Not reporting!
			REG_ECX = 0; //Not reporting!
			REG_EDX = 0; //Not reporting!
			break;
		default: //Lowest decominator!
			goto handleCPUIDdefault; //Unknown request!
			break;
		}
		break;
	default: //Unknown? Return CPU reset DX in AX!
	handleCPUIDdefault:
		if ((CPUID_mode == 0) || (CPUID_mode==1)) //Modern type result?
		{
			REG_EAX = REG_EBX = REG_ECX = REG_EDX = 0; //Nothing!
		}
		else //Compatibility mode?
		{
		handleCPUIDDX:
			switch (EMULATED_CPU)
			{
			case CPU_PENTIUM: //Pentium?
				REG_EAX = 0x0521; //Reset DX!
				break;
			case CPU_PENTIUMPRO: //Pentium Pro?
				REG_EAX = 0x0621 | ((CPU[activeCPU].registers->genericMSR[MSRnumbers[0x1B] - 1].lo & 0x800) >> 2); //Reset DX! Set bit 9(APIC emulated and enabled is reported on bit 9)!
				break;
			case CPU_PENTIUM2: //Pentium 2?
				REG_EAX = 0x0721; //Reset DX!
				break;
			default: //Lowest decominator!
			case CPU_80486: //80486?
				REG_EAX = 0x0421; //Reset DX!
				break;
			}
		}
		break;
	}
}

//x86 IMUL for opcodes 69h/6Bh.
void CPU_CIMUL(uint_32 base, byte basesize, uint_32 multiplicant, byte multiplicantsize, uint_32 *result, byte resultsize)
{
	CPU[activeCPU].temp1l.val64 = signextend64(base,basesize); //Read reg instead! Word register = Word register * imm16!
	CPU[activeCPU].temp2l.val64 = signextend64(multiplicant,multiplicantsize); //Immediate word is second/third parameter!
	CPU[activeCPU].temp3l.val64s = CPU[activeCPU].temp1l.val64s; //Load and...
	CPU[activeCPU].temp3l.val64s *= CPU[activeCPU].temp2l.val64s; //Signed multiplication!
	CPU[activeCPU].temp2l.val64 = signextend64(CPU[activeCPU].temp3l.val64,resultsize); //For checking for overflow and giving the correct result!
	switch (resultsize) //What result size?
	{
	default:
	case 8: flag_log8((byte)CPU[activeCPU].temp2l.val64); break;
	case 16: flag_log16((word)CPU[activeCPU].temp2l.val64); break;
	case 32: flag_log32((uint_32)CPU[activeCPU].temp2l.val64); break;
	}
	if (CPU[activeCPU].temp3l.val64s== CPU[activeCPU].temp2l.val64s) FLAGW_OF(0); //Overflow flag is cleared when high word is a sign extension of the low word(values are equal)!
	else FLAGW_OF(1);
	FLAGW_CF(FLAG_OF); //OF=CF!
	if ((EMULATED_CPU == CPU_8086) && CPU_getprefix(0xF3)) //REP used on 8086/8088?
	{
		CPU[activeCPU].temp2l.val64s = -CPU[activeCPU].temp2l.val64s; //Flip like acting as a fused NEG to the result!
	}
	*result = (uint_32)CPU[activeCPU].temp2l.val64; //Save the result, truncated to used size as 64-bit sign extension!
}

//Now the code!

void CPU_JMPrel(int_32 reladdr, byte useAddressSize)
{
	REG_EIP += reladdr; //Apply to EIP!
	REG_EIP &= CPU_EIPmask(useAddressSize); //Only 16-bits when required!
	if (CPU_MMU_checkrights_jump(CPU_SEGMENT_CS,REG_CS,REG_EIP,0x40|3,&CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS],2, CPU[activeCPU].CPU_Operand_size)) //Limit broken or protection fault?
	{
		THROWDESCGP(0,0,0); //#GP(0) when out of limit range!
	}
}

void CPU_JMPabs(uint_32 addr, byte useAddressSize)
{
	REG_EIP = addr; //Apply to EIP!
	REG_EIP &= CPU_EIPmask(useAddressSize); //Only 16-bits when required!
	if (CPU_MMU_checkrights_jump(CPU_SEGMENT_CS,REG_CS,REG_EIP,0x40|3,&CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS],2, CPU[activeCPU].CPU_Operand_size)) //Limit broken or protection fault?
	{
		THROWDESCGP(0,0,0); //#GP(0) when out of limit range!
	}
}

uint_32 CPU_EIPmask(byte useAddressSize)
{
	if (((CPU[activeCPU].CPU_Operand_size==0) && (useAddressSize==0)) || ((CPU[activeCPU].CPU_Address_size==0) && useAddressSize)) //16-bit movement?
	{
		return 0xFFFF; //16-bit mask!
	}
	return 0xFFFFFFFF; //Full mask!
}

byte CPU_EIPSize(byte useAddressSize)
{
	return ((CPU_EIPmask(useAddressSize)==0xFFFF) && (debugger_forceEIP()==0))?PARAM_IMM16:PARAM_IMM32; //Full mask or when forcing EIP to be used!
}


void modrm_debugger8(MODRM_PARAMS *theparams, byte whichregister1, byte whichregister2) //8-bit handler!
{
	if (CPU[activeCPU].cpudebugger)
	{
		cleardata(&CPU[activeCPU].modrm_param1[0],sizeof(CPU[activeCPU].modrm_param1));
		cleardata(&CPU[activeCPU].modrm_param2[0],sizeof(CPU[activeCPU].modrm_param2));
		modrm_text8(theparams,whichregister1,&CPU[activeCPU].modrm_param1[0]);
		modrm_text8(theparams,whichregister2,&CPU[activeCPU].modrm_param2[0]);
	}
}

void modrm_debugger16(MODRM_PARAMS *theparams, byte whichregister1, byte whichregister2) //16-bit handler!
{
	if (CPU[activeCPU].cpudebugger)
	{
		cleardata(&CPU[activeCPU].modrm_param1[0],sizeof(CPU[activeCPU].modrm_param1));
		cleardata(&CPU[activeCPU].modrm_param2[0],sizeof(CPU[activeCPU].modrm_param2));
		modrm_text16(theparams,whichregister1,&CPU[activeCPU].modrm_param1[0]);
		modrm_text16(theparams,whichregister2,&CPU[activeCPU].modrm_param2[0]);
	}
}

void modrm_debugger32(MODRM_PARAMS *theparams, byte whichregister1, byte whichregister2) //16-bit handler!
{
	if (CPU[activeCPU].cpudebugger)
	{
		cleardata(&CPU[activeCPU].modrm_param1[0],sizeof(CPU[activeCPU].modrm_param1));
		cleardata(&CPU[activeCPU].modrm_param2[0],sizeof(CPU[activeCPU].modrm_param2));
		modrm_text32(theparams,whichregister1,&CPU[activeCPU].modrm_param1[0]);
		modrm_text32(theparams,whichregister2,&CPU[activeCPU].modrm_param2[0]);
	}
}

byte NumberOfSetBits(uint_32 i)
{
	// Java: use >>> instead of >>
	// C or C++: use uint32_t
	i = i - ((i >> 1) & 0x55555555);
	i = (i & 0x33333333) + ((i >> 2) & 0x33333333);
	return (((i + (i >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;
}

/*

modrm_generateInstructionTEXT: Generates text for an instruction into the debugger.
parameters:
	instruction: The instruction ("ADD","INT",etc.)
	debuggersize: Size of the debugger, if any (8/16/32/0 for none).
	paramdata: The params to use when debuggersize set and using modr/m with correct type.
	type: See above.

*/

extern byte debugger_set; //Debugger set?

void modrm_generateInstructionTEXT(char *instruction, byte debuggersize, uint_32 paramdata, byte type)
{
	if (CPU[activeCPU].cpudebugger && (debugger_set==0)) //Gotten no debugger to process?
	{
		//Process debugger!
		char result[256];
		cleardata(&result[0],sizeof(result));
		safestrcpy(result,sizeof(result),instruction); //Set the instruction!
		switch (type)
		{
			case PARAM_MODRM1: //Param1 only?
			case PARAM_MODRM2: //Param2 only?
			case PARAM_MODRM12: //param1,param2
			case PARAM_MODRM12_IMM8: //param1,param2,imm8
			case PARAM_MODRM12_CL: //param1,param2,CL
			case PARAM_MODRM21: //param2,param1
			case PARAM_MODRM21_IMM8: //param2,param1,imm8
			case PARAM_MODRM21_CL: //param2,param1,CL
				//We use modr/m decoding!
				switch (debuggersize)
				{
					case 8:
						modrm_debugger8(&CPU[activeCPU].params,0,1);
						break;
					case 16:
						modrm_debugger16(&CPU[activeCPU].params,0,1);
						break;
					case 32:
						modrm_debugger32(&CPU[activeCPU].params,0,1);
						break;
					default: //None?
						//Don't use modr/m!
						break;
				}
				break;
			//Standards based on the modr/m in the information table.
			case PARAM_MODRM_0: //Param1 only?
			case PARAM_MODRM_1: //Param2 only?
			case PARAM_MODRM_01: //param1,param2
			case PARAM_MODRM_0_ACCUM_1: //0,accumulator,1?
			case PARAM_MODRM_01_IMM8: //param1,param2,imm8
			case PARAM_MODRM_01_CL: //param1,param2,CL
			case PARAM_MODRM_10: //param2,param1
			case PARAM_MODRM_10_IMM8: //param2,param1,imm8
			case PARAM_MODRM_10_CL: //param2,param1,CL
				switch (debuggersize)
				{
					case 8:
						modrm_debugger8(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, CPU[activeCPU].MODRM_src1);
						break;
					case 16:
						modrm_debugger16(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, CPU[activeCPU].MODRM_src1);
						break;
					case 32:
						modrm_debugger32(&CPU[activeCPU].params, CPU[activeCPU].MODRM_src0, CPU[activeCPU].MODRM_src1);
						break;
					default: //None?
						//Don't use modr/m!
						break;
				}
				break;
			default:
				break;
		}
		switch (type)
		{
			case PARAM_NONE: //No params?
				debugger_setcommand(result); //Nothing!
				break;
			case PARAM_MODRM_0:
			case PARAM_MODRM1: //Param1 only?
				safestrcat(result,sizeof(result)," %s"); //1 param!
				debugger_setcommand(result, CPU[activeCPU].modrm_param1);
				break;
			case PARAM_MODRM_1:
			case PARAM_MODRM2: //Param2 only?
				safestrcat(result,sizeof(result)," %s"); //1 param!
				debugger_setcommand(result, CPU[activeCPU].modrm_param2);
				break;
			case PARAM_MODRM_0_ACCUM_1: //0,accumulator,1?
				switch (debuggersize)
				{
				case 8:
					safestrcat(result, sizeof(result), " %s,AL,%s"); //2 params!
					break;
				case 16:
					safestrcat(result, sizeof(result), " %s,AX,%s"); //2 params!
					break;
				case 32:
					safestrcat(result, sizeof(result), " %s,EAX,%s"); //2 params!
					break;
				default: //None?
					//Don't use modr/m!
					break;
				}
				debugger_setcommand(result, CPU[activeCPU].modrm_param1, CPU[activeCPU].modrm_param2);
				break;
			case PARAM_MODRM_01:
			case PARAM_MODRM12: //param1,param2
				safestrcat(result,sizeof(result)," %s,%s"); //2 params!
				debugger_setcommand(result, CPU[activeCPU].modrm_param1, CPU[activeCPU].modrm_param2);
				break;
			case PARAM_MODRM_01_IMM8:
			case PARAM_MODRM12_IMM8: //param1,param2,imm8
				safestrcat(result,sizeof(result)," %s,%s,%02X"); //2 params!
				debugger_setcommand(result, CPU[activeCPU].modrm_param1, CPU[activeCPU].modrm_param2,paramdata);
				break;
			case PARAM_MODRM_01_CL:
			case PARAM_MODRM12_CL: //param1,param2,CL
				safestrcat(result,sizeof(result)," %s,%s,CL"); //2 params!
				debugger_setcommand(result, CPU[activeCPU].modrm_param1, CPU[activeCPU].modrm_param2);
				break;
			case PARAM_MODRM_10:
			case PARAM_MODRM21: //param2,param1
				safestrcat(result,sizeof(result)," %s,%s"); //2 params!
				debugger_setcommand(result, CPU[activeCPU].modrm_param2, CPU[activeCPU].modrm_param1);
				break;
			case PARAM_MODRM_10_IMM8:
			case PARAM_MODRM21_IMM8: //param2,param1,imm8
				safestrcat(result,sizeof(result)," %s,%s,%02X"); //2 params!
				debugger_setcommand(result, CPU[activeCPU].modrm_param2, CPU[activeCPU].modrm_param1,paramdata);
				break;
			case PARAM_MODRM_10_CL:
			case PARAM_MODRM21_CL: //param2,param1,CL
				safestrcat(result,sizeof(result)," %s,%s,CL"); //2 params!
				debugger_setcommand(result, CPU[activeCPU].modrm_param2, CPU[activeCPU].modrm_param1);
				break;
			case PARAM_IMM8: //imm8
				safestrcat(result,sizeof(result)," %02X"); //1 param!
				debugger_setcommand(result,paramdata);
				break;
			case PARAM_IMM8_PARAM: //imm8
				safestrcat(result,sizeof(result),"%02X"); //1 param!
				debugger_setcommand(result,paramdata);
				break;
			case PARAM_IMM16: //imm16
				safestrcat(result,sizeof(result)," %04X"); //1 param!
				debugger_setcommand(result,paramdata);
				break;
			case PARAM_IMM16_PARAM: //imm16
				safestrcat(result,sizeof(result),"%04X"); //1 param!
				debugger_setcommand(result,paramdata);
				break;
			case PARAM_IMM32: //imm32
				safestrcat(result,sizeof(result)," %08X"); //1 param!
				debugger_setcommand(result,paramdata);
				break;
			case PARAM_IMM32_PARAM: //imm32
				safestrcat(result,sizeof(result),"%08X"); //1 param!
				debugger_setcommand(result,paramdata);
				break;
			default: //Unknown?
				break;
		}
	}
}

//PORT IN/OUT instructions!
byte CPU_PORT_OUT_B(word base, word port, byte data)
{
	//Check rights!
	if (getcpumode() != CPU_MODE_REAL) //Protected mode?
	{
		if ((CPU[activeCPU].portrights_error = checkPortRights(port))) //Not allowed?
		{
			if (CPU[activeCPU].portrights_error==1) THROWDESCGP(0,0,0); //#GP!
			return 1; //Abort!
		}
	}
	//Execute it!
	byte dummy;
	if (CPU[activeCPU].internalinstructionstep==base) //First step? Request!
	{
		if (BIU_request_BUSwb(port,data)==0) //Not ready?
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
		if (BIU_readResultb(&dummy)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU_PORT_OUT_W(word base, word port, word data)
{
	if (getcpumode() != CPU_MODE_REAL) //Protected mode?
	{
		if ((CPU[activeCPU].portrights_error = checkPortRights(port))) //Not allowed?
		{
			if (CPU[activeCPU].portrights_error==1) THROWDESCGP(0,0,0); //#GP!
			return 1; //Abort!
		}
		if ((CPU[activeCPU].portrights_error = checkPortRights(port+1))) //Not allowed?
		{
			if (CPU[activeCPU].portrights_error==1) THROWDESCGP(0,0,0); //#GP!
			return 1; //Abort!
		}
	}
	//Execute it!
	word dummy;
	if (CPU[activeCPU].internalinstructionstep==base) //First step? Request!
	{
		if (BIU_request_BUSww(port,data)==0) //Not ready?
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
		if (BIU_readResultw(&dummy)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU_PORT_OUT_D(word base, word port, uint_32 data)
{
	if (getcpumode() != CPU_MODE_REAL) //Protected mode?
	{
		if ((CPU[activeCPU].portrights_error = checkPortRights(port))) //Not allowed?
		{
			if (CPU[activeCPU].portrights_error==1) THROWDESCGP(0,0,0); //#GP!
			return 1; //Abort!
		}
		if ((CPU[activeCPU].portrights_error = checkPortRights(port + 1))) //Not allowed?
		{
			if (CPU[activeCPU].portrights_error==1) THROWDESCGP(0,0,0); //#GP!
			return 1; //Abort!
		}
		if ((CPU[activeCPU].portrights_error = checkPortRights(port + 2))) //Not allowed?
		{
			if (CPU[activeCPU].portrights_error==1) THROWDESCGP(0,0,0); //#GP!
			return 1; //Abort!
		}
		if ((CPU[activeCPU].portrights_error = checkPortRights(port + 3))) //Not allowed?
		{
			if (CPU[activeCPU].portrights_error==1) THROWDESCGP(0,0,0); //#GP!
			return 1; //Abort!
		}
	}
	//Execute it!
	uint_32 dummy;
	if (CPU[activeCPU].internalinstructionstep==base) //First step? Request!
	{
		if (BIU_request_BUSwdw(port,data)==0) //Not ready?
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
		if (BIU_readResultdw(&dummy)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU_PORT_IN_B(word base, word port, byte *result)
{
	if (getcpumode() != CPU_MODE_REAL) //Protected mode?
	{
		if ((CPU[activeCPU].portrights_error = checkPortRights(port))) //Not allowed?
		{
			if (CPU[activeCPU].portrights_error==1) THROWDESCGP(0,0,0); //#GP!
			return 1; //Abort!
		}
	}
	//Execute it!
	if (CPU[activeCPU].internalinstructionstep==base) //First step? Request!
	{
		if (BIU_request_BUSrb(port)==0) //Not ready?
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
		if (BIU_readResultb(result)==0) //Not ready?
		{
			CPU[activeCPU].cycles_OP += 1; //Take 1 cycle only!
			CPU[activeCPU].executed = 0; //Not executed!
			return 1; //Keep running!
		}
		++CPU[activeCPU].internalinstructionstep; //Next step!
	}
	return 0; //Ready to process further! We're loaded!
}

byte CPU_PORT_IN_W(word base, word port, word *result)
{
	if (getcpumode() != CPU_MODE_REAL) //Protected mode?
	{
		if ((CPU[activeCPU].portrights_error = checkPortRights(port))) //Not allowed?
		{
			if (CPU[activeCPU].portrights_error==1) THROWDESCGP(0,0,0); //#GP!
			return 1; //Abort!
		}
		if ((CPU[activeCPU].portrights_error = checkPortRights(port + 1))) //Not allowed?
		{
			if (CPU[activeCPU].portrights_error==1) THROWDESCGP(0,0,0); //#GP!
			return 1; //Abort!
		}
	}
	//Execute it!
	if (CPU[activeCPU].internalinstructionstep==base) //First step? Request!
	{
		if (BIU_request_BUSrw(port)==0) //Not ready?
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

byte CPU_PORT_IN_D(word base, word port, uint_32 *result)
{
	if (getcpumode() != CPU_MODE_REAL) //Protected mode?
	{
		if ((CPU[activeCPU].portrights_error = checkPortRights(port))) //Not allowed?
		{
			if (CPU[activeCPU].portrights_error==1) THROWDESCGP(0,0,0); //#GP!
			return 1; //Abort!
		}
		if ((CPU[activeCPU].portrights_error = checkPortRights(port + 1))) //Not allowed?
		{
			if (CPU[activeCPU].portrights_error==1) THROWDESCGP(0,0,0); //#GP!
			return 1; //Abort!
		}
		if ((CPU[activeCPU].portrights_error = checkPortRights(port + 2))) //Not allowed?
		{
			if (CPU[activeCPU].portrights_error==1) THROWDESCGP(0,0,0); //#GP!
			return 1; //Abort!
		}
		if ((CPU[activeCPU].portrights_error = checkPortRights(port + 3))) //Not allowed?
		{
			if (CPU[activeCPU].portrights_error==1) THROWDESCGP(0,0,0); //#GP!
			return 1; //Abort!
		}
	}
	//Execute it!
	if (CPU[activeCPU].internalinstructionstep==base) //First step? Request!
	{
		if (BIU_request_BUSrdw(port)==0) //Not ready?
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

byte call_soft_inthandler(byte intnr, int_64 errorcode, byte is_interrupt)
{
	//Now call handler!
	//CPU[activeCPU].cycles_HWOP += 61; /* Normal interrupt as hardware interrupt */
	CPU[activeCPU].calledinterruptnumber = intnr; //Save called interrupt number!
	return CPU_INT(intnr,errorcode,is_interrupt); //Call interrupt!
}

void call_hard_inthandler(byte intnr) //Hardware interrupt handler (FROM hardware only, or int>=0x20 for software call)!
{
//Now call handler!
	//CPU[activeCPU].cycles_HWOP += 61; /* Normal interrupt as hardware interrupt */
	CPU[activeCPU].calledinterruptnumber = intnr; //Save called interrupt number!
	CPU_executionphase_startinterrupt(intnr, 2|8, -1); //Start the interrupt handler! EXT is set for faults!
}

void CPU_8086_RETI() //Not from CPU!
{
	CPU_IRET(); //RETURN FROM INTERRUPT!
}

extern byte reset; //Reset?

void CPU_ErrorCallback_RESET() //Error callback with error code!
{
	debugrow("Resetting emulator: Error callback called!");
	reset = 1; //Reset the emulator!
}

void copyint(byte src, byte dest) //Copy interrupt handler pointer to different interrupt!
{
	MMU_ww(-1,0x0000,(dest<<2),MMU_rw(-1,0x0000,(src<<2),0,0),0); //Copy segment!
	MMU_ww(-1,0x0000,(dest<<2)|2,MMU_rw(-1,0x0000,((src<<2)|2),0,0),0); //Copy offset!
}

extern byte CPU_databussize; //0=16/32-bit bus! 1=8-bit bus when possible (8088/80188) or 16-bit when possible(286+)!

OPTINLINE void CPU_resetPrefixes() //Resets all prefixes we use!
{
	memset(&CPU[activeCPU].CPU_prefixes, 0, sizeof(CPU[activeCPU].CPU_prefixes)); //Reset prefixes!
}

OPTINLINE void CPU_initPrefixes()
{
	CPU_resetPrefixes(); //This is the same: just reset all prefixes to zero!
}

OPTINLINE void alloc_CPUregisters()
{
	CPU[activeCPU].registers = (CPU_registers *)zalloc(sizeof(*CPU[activeCPU].registers), "CPU_REGISTERS", getLock(LOCK_CPU)); //Allocate the registers!
	if (!CPU[activeCPU].registers)
	{
		raiseError("CPU", "Failed to allocate the required registers!");
	}
}

OPTINLINE void free_CPUregisters()
{
	if (CPU[activeCPU].registers) //Still allocated?
	{
		CPU[activeCPU].oldCR0 = CPU[activeCPU].registers->CR0; //Save the old value for INIT purposes!
		freez((void **)&CPU[activeCPU].registers, sizeof(*CPU[activeCPU].registers), "CPU_REGISTERS"); //Release the registers if needed!
	}
}

//isInit: bit 8 means that the INIT pin is raised without the RESET pin!
OPTINLINE void CPU_initRegisters(word isInit) //Init the registers!
{
	uint_32 MSRbackup[CPU_NUMMSRS];
	uint_32 CSBase; //Base of CS!
	byte CSAccessRights; //Default CS access rights, overwritten during first software reset!
	if (CPU[activeCPU].registers) //Already allocated?
	{
		CSAccessRights = CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS].desc.AccessRights; //Save old CS acccess rights to use now (after first reset)!
		if ((isInit&0x80)==0x80) //INIT?
		{
			memcpy(&MSRbackup, &CPU[activeCPU].registers->genericMSR, sizeof(MSRbackup)); //Backup the MSRs!
			//Leave TSC alone!
		}
		else
		{
			memset(&MSRbackup, 0, sizeof(MSRbackup)); //Cleared MSRs!
			CPU[activeCPU].TSC = 0; //Clear the TSC (Reset without BIST)!
		}
		if (((isInit&0x80)==0) && ((isInit&0x100)==0)) //Not local reset?
		{
			free_CPUregisters(); //Free the CPU registers!
		}
		else //Soft reset or hard reset with same registers allocated?
		{
			CPU[activeCPU].oldCR0 = CPU[activeCPU].registers->CR0&0x60000000; //Save the old value for INIT purposes!
			memset(CPU[activeCPU].registers, 0, sizeof(*CPU[activeCPU].registers)); //Simply clear!
			if ((isInit & 0x80) == 0x80) //INIT?
			{
				memcpy(&CPU[activeCPU].registers->genericMSR, &MSRbackup, sizeof(MSRbackup)); //Restore the MSRs to stay unaffected!
			}
		}
	}
	else
	{
		CSAccessRights = 0x93; //Initialise the CS access rights!
	}
	if ((((isInit&0x80)==0) && ((isInit&0x100)==0)) || (!CPU[activeCPU].registers)) //Needs allocation of registers?
	{
		alloc_CPUregisters(); //Allocate the CPU registers!
		CPU[activeCPU].TSC = 0; //Clear the TSC (Reset without BIST)!
	}

	if (!CPU[activeCPU].registers) return; //We can't work!
	
	//General purpose registers
	REG_EAX = 0;
	REG_EBX = 0;
	REG_ECX = 0;
	REG_EDX = 0;

	if (EMULATED_CPU>=CPU_80386) //Need revision info in DX?
	{
		switch (EMULATED_CPU)
		{
		default:
		case CPU_80386:
			REG_DX = CPU_databussize ? 0x2303 : 0x0303;
			break;
		case CPU_80486:
			REG_DX = 0x0421; //80486SX! DX not supported yet!
			break;
		case CPU_PENTIUM:
			REG_DX = 0x0521; //Pentium! DX not supported yet!
			break;
		case CPU_PENTIUMPRO:
			REG_DX = 0x0621; //Pentium! DX not supported yet!
			break;
		case CPU_PENTIUM2:
			REG_DX = 0x0721; //Pentium! DX not supported yet!
			break;
		}
	}

	//Index registers
	REG_EBP = 0; //Init offset of BP?
	REG_ESI = 0; //Source index!
	REG_EDI = 0; //Destination index!

	//Stack registers
	REG_ESP = 0; //Init offset of stack (top-1)
	REG_SS = 0; //Stack segment!


	//Code location
	if (EMULATED_CPU >= CPU_NECV30) //186+?
	{
		REG_CS = 0xF000; //We're this selector!
		REG_EIP = 0xFFF0; //We're starting at this offset!
	}
	else //8086?
	{
		REG_CS = 0xFFFF; //Code segment: default to segment 0xFFFF to start at 0xFFFF0 (bios boot jump)!
		REG_EIP = 0; //Start of executable code!
	}
	
	//Data registers!
	REG_DS = 0; //Data segment!
	REG_ES = 0; //Extra segment!
	REG_FS = 0; //Far segment (extra segment)
	REG_GS = 0; //??? segment (extra segment like FS)
	REG_EFLAGS = 0x2; //Flags!

	//Now the handling of solid state segments (might change, use index for that!)
	CPU[activeCPU].SEGMENT_REGISTERS[CPU_SEGMENT_CS] = &REG_CS; //Link!
	CPU[activeCPU].SEGMENT_REGISTERS[CPU_SEGMENT_SS] = &REG_SS; //Link!
	CPU[activeCPU].SEGMENT_REGISTERS[CPU_SEGMENT_DS] = &REG_DS; //Link!
	CPU[activeCPU].SEGMENT_REGISTERS[CPU_SEGMENT_ES] = &REG_ES; //Link!
	CPU[activeCPU].SEGMENT_REGISTERS[CPU_SEGMENT_FS] = &REG_FS; //Link!
	CPU[activeCPU].SEGMENT_REGISTERS[CPU_SEGMENT_GS] = &REG_GS; //Link!
	CPU[activeCPU].SEGMENT_REGISTERS[CPU_SEGMENT_TR] = &REG_TR; //Link!
	CPU[activeCPU].SEGMENT_REGISTERS[CPU_SEGMENT_LDTR] = &REG_LDTR; //Link!

	memset(&CPU[activeCPU].SEG_DESCRIPTOR, 0, sizeof(CPU[activeCPU].SEG_DESCRIPTOR)); //Clear the descriptor cache!
	CPU[activeCPU].have_oldSegReg = 0; //No backups for any segment registers are loaded!
	 //Now, load the default descriptors!

	//IDTR
	CPU[activeCPU].registers->IDTR.base = 0;
	CPU[activeCPU].registers->IDTR.limit = 0x3FF;

	//GDTR
	CPU[activeCPU].registers->GDTR.base = 0;
	CPU[activeCPU].registers->GDTR.limit = 0xFFFF; //From bochs!

	//LDTR (invalid)
	REG_LDTR = 0; //No LDTR (also invalid)!

	//TR (invalid)
	REG_TR = 0; //No TR (also invalid)!

	if (EMULATED_CPU == CPU_80286) //80286 CPU?
	{
		CPU[activeCPU].registers->CR0 = 0; //Clear bit 32 and 4-0, also the MSW!
		CPU[activeCPU].registers->CR0 |= 0xFFF0; //The MSW is initialized to FFF0!
	}
	else //Default or 80386?
	{
		if ((isInit&0x80)==0x80) //Were we an INIT?
		{
			CPU[activeCPU].registers->CR0 = CPU[activeCPU].oldCR0; //Restore before resetting, if possible! Keep the cache bits(bits 30-29), clear all other bits, set bit 4(done below)!
		}
		else
		{
			CPU[activeCPU].registers->CR0 = 0x60000010; //Restore before resetting, if possible! Apply init defaults!
		}
		CPU[activeCPU].registers->CR0 &= 0x60000000; //The MSW is initialized to 0000! High parts are reset as well!
		if (EMULATED_CPU >= CPU_80486) //80486+?
		{
			CPU[activeCPU].registers->CR0 |= 0x0010; //Only set the defined bits! Bits 30/29 remain unmodified, according to http://www.sandpile.org/x86/initial.htm
		}
		else //80386?
		{
			CPU[activeCPU].registers->CR0 = 0; //We don't have the 80486+ register bits, so reset them!
		}
	}

	byte reg = 0;
	for (reg = 0; reg<NUMITEMS(CPU[activeCPU].SEG_DESCRIPTOR); reg++) //Process all segment registers!
	{
		//Load Real mode compatible values for all registers!
		CPU[activeCPU].SEG_DESCRIPTOR[reg].desc.base_high = 0;
		CPU[activeCPU].SEG_DESCRIPTOR[reg].desc.base_mid = 0;
		CPU[activeCPU].SEG_DESCRIPTOR[reg].desc.base_low = 0;
		CPU[activeCPU].SEG_DESCRIPTOR[reg].desc.limit_low = 0xFFFF; //64k limit!
		CPU[activeCPU].SEG_DESCRIPTOR[reg].desc.noncallgate_info = 0; //No high limit etc.!
		//According to http://www.sandpile.org/x86/initial.htm the following access rights are used:
		if ((reg == CPU_SEGMENT_LDTR) || (reg == CPU_SEGMENT_TR)) //LDTR&TR=Special case! Apply special access rights!
		{
			CPU[activeCPU].SEG_DESCRIPTOR[reg].desc.AccessRights = (reg == CPU_SEGMENT_TR)?0x83:0x82; //Invalid segment or 16/32-bit TSS!
			if ((reg == CPU_SEGMENT_TR) && (EMULATED_CPU>=CPU_80386))
			{
				CPU[activeCPU].SEG_DESCRIPTOR[reg].desc.AccessRights |= 0x8; //32-bit TSS?
			}
		}
		else //Normal Code/Data segment?
		{
			CPU[activeCPU].SEG_DESCRIPTOR[reg].desc.AccessRights = 0x93; //Code/data segment, writable!
		}
	}

	//CS specific!
	CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS].desc.AccessRights = CSAccessRights; //Load CS default access rights!
	if (EMULATED_CPU>CPU_NECV30) //286+?
	{
		//Pulled low on first load, pulled high on reset:
		if (EMULATED_CPU>CPU_80286) //32-bit CPU?
		{
			CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS].desc.base_high = 0xFF; //More than 24 bits are pulled high as well!
		}
		CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS].desc.base_mid = 0xFF; //We're starting at the end of our address space, final block! (segment F000=>high 8 bits set)
	}
	else //186-?
	{
		CSBase = REG_CS<<4; //CS base itself!
		CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS].desc.base_mid = (CSBase>>16); //Mid range!
		CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS].desc.base_low = (CSBase&0xFFFF); //Low range!
	}

	for (reg = 0; reg<NUMITEMS(CPU[activeCPU].SEG_DESCRIPTOR); reg++) //Process all segment registers!
	{
		CPU_calcSegmentPrecalcs((reg==CPU_SEGMENT_CS)?1:0,&CPU[activeCPU].SEG_DESCRIPTOR[reg]); //Calculate the precalcs for the segment descriptor!
	}

	CPU_flushPIQ(-1); //We're jumping to another address!
}

void CPU_initLookupTables(); //Initialize the CPU timing lookup tables! Prototype!
extern byte is_XT; //Are we an XT?

uint_64 effectivecpuaddresspins = 0xFFFFFFFF;
uint_64 cpuaddresspins[16] = { //Bit0=XT, Bit1+=CPU
							0xFFFFF, //8086 AT+
							0xFFFFF, //8086 XT
							0xFFFFF, //80186 AT+
							0xFFFFF, //80186 XT
							0xFFFFFF, //80286 AT+
							0xFFFFFF, //80286 XT
							0xFFFFFFFF, //80386 AT+
							0xFFFFFFFF, //80386 XT
							0xFFFFFFFF, //80486 AT+
							0xFFFFFFFF, //80486 XT
							0xFFFFFFFF, //80586 AT+
							0xFFFFFFFF, //80586 XT
							0xFFFFFFFFFFFFULL, //80686 AT+
							0xFFFFFFFFFFFFULL, //80686 XT
							0xFFFFFFFFFFFFULL, //80786 AT+
							0xFFFFFFFFFFFFULL //80786 XT
}; //CPU address wrapping lookup table!

//isInit: bit 7 means that the INIT pin is raised without the RESET pin! bit 8 means RESET pin with INIT pin!
void resetCPU(word isInit) //Initialises the currently selected CPU!
{
	CPU[activeCPU].allowInterrupts = CPU[activeCPU].previousAllowInterrupts = 1; //Default to allowing all interrupts to run!
	CPU_initRegisters(isInit); //Initialise the registers!
	CPU_initPrefixes(); //Initialise all prefixes!
	CPU_resetMode(); //Reset the mode to the default mode!
	//Default: not waiting for interrupt to occur on startup!
	//Not waiting for TEST pin to occur!
	//Default: not blocked!
	//Continue interrupt call (hardware)?
	CPU[activeCPU].running = 1; //We're running!
	CPU[activeCPU].halt = 0; //Not halting anymore!

	CPU[activeCPU].currentopcode = CPU[activeCPU].currentopcode0F = CPU[activeCPU].currentmodrm = CPU[activeCPU].previousopcode = CPU[activeCPU].previousopcode0F = CPU[activeCPU].previousmodrm = 0; //Last opcode, default to 0 and unknown?
	generate_opcode_jmptbl(); //Generate the opcode jmptbl for the current CPU!
	generate_opcode0F_jmptbl(); //Generate the opcode 0F jmptbl for the current CPU!
	generate_opcodeInformation_tbl(); //Generate the timings tables for all CPU's!
	CPU_initLookupTables(); //Initialize our timing lookup tables!
	#ifdef CPU_USECYCLES
	CPU_useCycles = 1; //Are we using cycle-accurate emulation?
	#endif
	if ((isInit & 0x80) == 0) //Not just init?
	{
		EMU_onCPUReset(isInit); //Make sure all hardware, like CPU A20 is updated for the reset!
	}
	CPU[activeCPU].D_B_Mask = (EMULATED_CPU>=CPU_80386)?1:0; //D_B mask when applyable!
	CPU[activeCPU].G_Mask = (EMULATED_CPU >= CPU_80386) ? 1 : 0; //G mask when applyable!
	CPU[activeCPU].is_reset = 1; //We're reset!
	CPU[activeCPU].CPL = 0; //We're real mode, so CPL=0!
	memset(&CPU[activeCPU].instructionfetch,0,sizeof(CPU[activeCPU].instructionfetch)); //Reset the instruction fetching system!
	CPU[activeCPU].instructionfetch.CPU_isFetching = CPU[activeCPU].instructionfetch.CPU_fetchphase =  1; //We're starting to fetch!
	CPU_initBIU(); //Initialize the BIU for use!
	Paging_initTLB(); //Init and clear the TLB when resetting!
	effectivecpuaddresspins = cpuaddresspins[((EMULATED_CPU<<1)|is_XT)]; //What pins are supported for the current CPU/architecture?
	protectedModeDebugger_updateBreakpoints(); //Update the breakpoints to use!
	CPU_executionphase_init(); //Initialize the execution phase to it's initial state!
	if (EMULATED_CPU >= CPU_PENTIUMPRO) //Has APIC support?
	{
		if ((isInit & 0x80) == 0) //Not INIT?
		{
			CPU[activeCPU].registers->genericMSR[MSRnumbers[0x1B] - 1].lo = 0xFEE00800 | (activeCPU ? 0 : 0x100); //Initial value! We're the bootstrap processor! APIC enabled!
			CPU[activeCPU].registers->genericMSR[MSRnumbers[0x1B] - 1].hi = 0; //Initial value!
		}
		APIC_updateWindowMSR(activeCPU,CPU[activeCPU].registers->genericMSR[MSRnumbers[0x1B] - 1].lo, CPU[activeCPU].registers->genericMSR[MSRnumbers[0x1B] - 1].hi); //Update the MSR for the hardware!
	}
	else
	{
		APIC_updateWindowMSR(activeCPU, 0, 0); //Update the MSR for the hardware! Disable the APIC!
	}
	if ((isInit&0x80) && activeCPU) //INIT? Waiting for SIPI on non-BSP!
	{
		resetLAPIC(activeCPU, 2); //INIT reset of the APIC!
		CPU[activeCPU].waitingforSIPI = 1; //Waiting!
	}
	else //Normal reset?
	{
		resetLAPIC(activeCPU, (isInit&0x80)?2:1); //Hard reset of the APIC? Depends on INIT vs RESET!
		//Make sure the local APIC is using the current values!
		if (EMULATED_CPU >= CPU_PENTIUMPRO) //Has APIC support?
		{
			APIC_updateWindowMSR(activeCPU,CPU[activeCPU].registers->genericMSR[MSRnumbers[0x1B] - 1].lo, CPU[activeCPU].registers->genericMSR[MSRnumbers[0x1B] - 1].hi); //Update the MSR for the hardware!
		}
		else
		{
			APIC_updateWindowMSR(activeCPU,0, 0); //Update the MSR for the hardware! Disable the APIC!
		}
		if (activeCPU) //Waiting for SIPI after reset?
		{
			CPU[activeCPU].waitingforSIPI = 1; //Waiting!
		}
		else //BSP reset?
		{
			CPU[activeCPU].waitingforSIPI = 0; //Active!
		}
	}
	CPU[activeCPU].SIPIreceived = 0; //No SIPI received yet!
}

void initCPU() //Initialize CPU for full system reset into known state!
{
	MMU_determineAddressWrapping(); //Determine the address wrapping to use!
	CPU_calcSegmentPrecalcsPrecalcs(); //Calculate the segmentation precalcs that are used!
	memset(&CPU[activeCPU], 0, sizeof(CPU[activeCPU])); //Reset the CPU fully!
	//Initialize all local variables!
	CPU[activeCPU].newREP = 1; //Default value!
	CPU[activeCPU].CPU_executionphaseinterrupt_errorcode = -1; //Default value!
	CPU[activeCPU].hascallinterrupttaken_type = 0xFF; //Default value!
	CPU[activeCPU].currentOP_handler = &CPU_unkOP; //No opcode mapped yet!
	CPU[activeCPU].newREP = 1; //Default value!
	CPU[activeCPU].INTreturn_CS = 0xCCCC; //Default value!
	CPU[activeCPU].INTreturn_EIP = 0xCCCCCCCC; //Default value!
	CPU[activeCPU].portExceptionResult = 0xFF; //Default value!
	CPU_initMSRs(); //Initialize the MSRs and their mappings!
	resetCPU(1); //Reset normally!
	Paging_initTLB(); //Initialize the TLB for usage!
}

void CPU_tickPendingReset()
{
	byte resetPendingFlag;
	if (unlikely(CPU[activeCPU].resetPending)) //Are we pending?
	{
		if (BIU_resetRequested() && (CPU[activeCPU].instructionfetch.CPU_isFetching==1) && (CPU[activeCPU].resetPending != 2)) //Starting a new instruction or halted with pending Reset?
		{
			resetPendingFlag = CPU[activeCPU].resetPending; //The flag!
			resetCPU(0x100|(resetPendingFlag<<4)); //Simply fully reset the CPU on triple fault(e.g. reset pin result)!
			CPU[activeCPU].resetPending = 0; //Not pending reset anymore!
		}
	}
}

//data order is low-high, e.g. word 1234h is stored as 34h, 12h

/*
0xF3 Used with string REP, REPE/REPZ(Group 1)
0xF2 REPNE/REPNZ prefix(Group 1)
0xF0 LOCK prefix(Group 1)
0x2E CS segment override prefix(Group 2)
0x36 SS segment override prefix(Group 2)
0x3E DS segment override prefix(Group 2)
0x26 ES segment override prefix(Group 2)
0x64 FS segment override prefix(Group 2)
0x65 GS segment override prefix(Group 2)
0x66 Operand-size override(Group 3)
0x67 Address-size override(Group 4)

For prefix groups 1&2: last one in the group has effect(ignores anything from the same group before it).
For prefix groups 3&4: one specified in said group in total has effect once(multiple are redundant and ignored(basically OR'ed with each other)).
*/


byte CPU_getprefix(byte prefix) //Prefix set?
{
	return ((CPU[activeCPU].CPU_prefixes[prefix >> 3] >> (prefix & 7)) & 1); //Get prefix set or reset!
}

void CPU_clearprefix(byte prefix) //Sets a prefix on!
{
	CPU[activeCPU].CPU_prefixes[(prefix >> 3)] &= ~(1 << (prefix & 7)); //Don't have prefix!
}

void CPU_setprefix(byte prefix) //Sets a prefix on!
{
	switch (prefix)
	{
	case 0xF0: //LOCK
		//Last has effect (Group 1)!
		CPU_clearprefix(0xF2); //Disable multiple prefixes from being active! The last one is active!
		CPU_clearprefix(0xF3); //Disable multiple prefixes from being active! The last one is active!
		break;
	case 0xF3: //REP, REPE, REPZ?
		//Last has effect (Group 1)!
		CPU_clearprefix(0xF0); //Disable multiple prefixes from being active! The last one is active!
		CPU_clearprefix(0xF2); //Disable multiple prefixes from being active! The last one is active!
		break;
	case 0xF2: //REPNE/REPNZ?
		//Last has effect (Group 1)!
		CPU_clearprefix(0xF0); //Disable multiple prefixes from being active! The last one is active!
		CPU_clearprefix(0xF3); //Disable multiple prefixes from being active! The last one is active!
		break;
	case 0x2E: //CS segment override prefix
		//Last has effect (Group 2)!
		CPU[activeCPU].segment_register = CPU_SEGMENT_CS; //Override to CS!
		break;
	case 0x36: //SS segment override prefix
		//Last has effect (Group 2)!
		CPU[activeCPU].segment_register = CPU_SEGMENT_SS; //Override to SS!
		break;
	case 0x3E: //DS segment override prefix
		//Last has effect (Group 2)!
		CPU[activeCPU].segment_register = CPU_SEGMENT_DS; //Override to DS!
		break;
	case 0x26: //ES segment override prefix
		//Last has effect (Group 2)!
		CPU[activeCPU].segment_register = CPU_SEGMENT_ES; //Override to ES!
		break;
	case 0x64: //FS segment override prefix
		//Last has effect (Group 2)!
		CPU[activeCPU].segment_register = CPU_SEGMENT_FS; //Override to FS!
		break;
	case 0x65: //GS segment override prefix
		//Last has effect (Group 2)!
		CPU[activeCPU].segment_register = CPU_SEGMENT_GS; //Override to GS!
		break;
	case 0x66: //GS segment override prefix
		//Last has effect (Group 3)!
		break;
	case 0x67: //GS segment override prefix
		//Last has effect (Group 4)!
		break;
	default: //Unknown special prefix action?
		break; //Do nothing!
	}
	CPU[activeCPU].CPU_prefixes[(prefix >> 3)] |= (1 << (prefix & 7)); //Have prefix!
}

OPTINLINE byte CPU_isPrefix(byte prefix)
{
	switch (prefix) //What prefix/opcode?
	{
	//First, normal instruction prefix codes:
		case 0xF2: //REPNE/REPNZ prefix
		case 0xF3: //REPZ
		case 0xF0: //LOCK prefix
		case 0x2E: //CS segment override prefix
		case 0x36: //SS segment override prefix
		case 0x3E: //DS segment override prefix
		case 0x26: //ES segment override prefix
			return 1; //Always a prefix!
		case 0x64: //FS segment override prefix
		case 0x65: //GS segment override prefix
		case 0x66: //Operand-size override
		case 0x67: //Address-size override
			return (EMULATED_CPU>=CPU_80386); //We're a prefix when 386+!
		default: //It's a normal OPcode?
			return 0; //No prefix!
			break; //Not use others!
	}

	return 0; //No prefix!
}

extern Handler CurrentCPU_opcode_jmptbl[1024]; //Our standard internal standard opcode jmptbl!

OPTINLINE void CPU_resetInstructionSteps()
{
	//Prepare for a (repeated) instruction to execute!
	CPU[activeCPU].instructionstep = CPU[activeCPU].internalinstructionstep = CPU[activeCPU].modrmstep = CPU[activeCPU].internalmodrmstep = CPU[activeCPU].internalinterruptstep = CPU[activeCPU].stackchecked = 0; //Start the instruction-specific stage!
	CPU[activeCPU].timingpath = 0; //Reset timing oath!
	CPU[activeCPU].pushbusy = 0;
	CPU[activeCPU].custommem = 0; //Not using custom memory addresses for MOV!
	CPU[activeCPU].customoffset = 0; //See custommem!
	CPU[activeCPU].blockREP = 0; //Not blocking REP!
}

void CPU_interruptcomplete()
{
	CPU_resetInstructionSteps(); //Reset the instruction steps to properly handle it!
	//Prepare in the case of hardware interrupts!
	CPU[activeCPU].exec_CS = REG_CS; //CS to execute!
	CPU[activeCPU].exec_EIP = REG_EIP; //EIP to execute!
	CPU_commitState(); //Commit the state of the CPU for any future faults or interrupts!
	CPU[activeCPU].InterruptReturnEIP = REG_EIP; //Make sure that interrupts behave with a correct EIP after faulting on REP prefixed instructions!
}

OPTINLINE byte CPU_readOP_prefix(byte *OP) //Reads OPCode with prefix(es)!
{
	CPU[activeCPU].cycles_Prefix = 0; //No cycles for the prefix by default!

	if (CPU[activeCPU].instructionfetch.CPU_fetchphase) //Reading opcodes?
	{
		if (CPU[activeCPU].instructionfetch.CPU_fetchphase==1) //Reading new opcode?
		{
			CPU_resetPrefixes(); //Reset all prefixes for this opcode!
			reset_modrm(); //Reset modr/m for the current opcode, for detecting it!
			CPU[activeCPU].InterruptReturnEIP = CPU[activeCPU].last_eip = REG_EIP; //Interrupt return point by default!
			CPU[activeCPU].instructionfetch.CPU_fetchphase = 2; //Reading prefixes or opcode!
			CPU[activeCPU].ismultiprefix = 0; //Default to not being multi prefix!
		}
		if (CPU[activeCPU].instructionfetch.CPU_fetchphase==2) //Reading prefixes or opcode?
		{
			nextprefix: //Try next prefix/opcode?
			if (CPU_readOP(OP,1)) return 1; //Read opcode or prefix?
			if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
			if (CPU_isPrefix(*OP)) //We're a prefix?
			{
				CPU[activeCPU].cycles_Prefix += 2; //Add timing for the prefix!
				if (CPU[activeCPU].ismultiprefix && (EMULATED_CPU <= CPU_80286)) //This CPU has the bug and multiple prefixes are added?
				{
					CPU[activeCPU].InterruptReturnEIP = CPU[activeCPU].last_eip; //Return to the last prefix only!
				}
				CPU_setprefix(*OP); //Set the prefix ON!
				CPU[activeCPU].last_eip = REG_EIP; //Save the current EIP of the last prefix possibility!
				CPU[activeCPU].ismultiprefix = 1; //We're multi-prefix now when triggered again!
				goto nextprefix; //Try the next prefix!
			}
			else //No prefix? We've read the actual opcode!
			{
				CPU[activeCPU].instructionfetch.CPU_fetchphase = 3; //Advance to stage 3: Fetching 0F instruction!
			}
		}
		//Now we have the opcode and prefixes set or reset!
		if (CPU[activeCPU].instructionfetch.CPU_fetchphase==3) //Check and fetch 0F opcode?
		{
			if ((*OP == 0x0F) && (EMULATED_CPU >= CPU_80286)) //0F instruction extensions used?
			{
				if (CPU_readOP(OP,1)) return 1; //Read the actual opcode to use!
				if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
				CPU[activeCPU].is0Fopcode = 1; //We're a 0F opcode!
				CPU[activeCPU].instructionfetch.CPU_fetchphase = 0; //We're fetched completely! Ready for first decode!
				CPU[activeCPU].instructionfetch.CPU_fetchingRM = 1; //Fetching R/M, if any!
				CPU[activeCPU].instructionfetch.CPU_fetchparameterPos = 0; //Init parameter position!
				memset(&CPU[activeCPU].params.instructionfetch,0,sizeof(CPU[activeCPU].params.instructionfetch)); //Init instruction fetch status!
			}
			else //Normal instruction?
			{
				CPU[activeCPU].is0Fopcode = 0; //We're a normal opcode!
				CPU[activeCPU].instructionfetch.CPU_fetchphase = 0; //We're fetched completely! Ready for first decode!
				CPU[activeCPU].instructionfetch.CPU_fetchingRM = 1; //Fetching R/M, if any!
				CPU[activeCPU].instructionfetch.CPU_fetchparameterPos = 0; //Init parameter position!
				memset(&CPU[activeCPU].params.instructionfetch,0,sizeof(CPU[activeCPU].params.instructionfetch)); //Init instruction fetch status!
			}

			//Determine the stack&attribute sizes(286+)!
			//Stack address size is automatically retrieved!

			//32-bits operand&address defaulted? We're a 32-bit Operand&Address size to default to instead!
			CPU[activeCPU].CPU_Operand_size = CPU[activeCPU].CPU_Address_size = (CODE_SEGMENT_DESCRIPTOR_D_BIT() & 1);

			//Apply operand size/address size prefixes!
			CPU[activeCPU].CPU_Operand_size ^= CPU_getprefix(0x66); //Invert operand size?
			CPU[activeCPU].CPU_Address_size ^= CPU_getprefix(0x67); //Invert address size?

			CPU[activeCPU].address_size = ((0xFFFFU | (0xFFFFU << (CPU[activeCPU].CPU_Address_size << 4))) & 0xFFFFFFFFULL); //Effective address size for this instruction!
		}
	}

	//Now, check for the ModR/M byte, if present, and read the parameters if needed!
	CPU[activeCPU].currentOpcodeInformation = &CPUOpcodeInformationPrecalcs[CPU[activeCPU].CPU_Operand_size][(*OP<<1)|CPU[activeCPU].is0Fopcode]; //Only 2 modes implemented so far, 32-bit or 16-bit mode, with 0F opcode every odd entry!

	if (((CPU[activeCPU].currentOpcodeInformation->readwritebackinformation&0x80)==0) && CPU_getprefix(0xF0) && (EMULATED_CPU>=CPU_NECV30)) //LOCK when not allowed, while the exception is supported?
	{
		goto invalidlockprefix;
	}

	if (CPU[activeCPU].currentOpcodeInformation->used==0) goto skipcurrentOpcodeInformations; //Are we not used?
	if (CPU[activeCPU].currentOpcodeInformation->has_modrm && CPU[activeCPU].instructionfetch.CPU_fetchingRM) //Do we have ModR/M data?
	{
		if (modrm_readparams(&CPU[activeCPU].params, CPU[activeCPU].currentOpcodeInformation->modrm_size, CPU[activeCPU].currentOpcodeInformation->modrm_specialflags,*OP)) return 1; //Read the params!
		CPU[activeCPU].instructionfetch.CPU_fetchparameterPos = 0; //Reset the parameter position again for new parameters!
		if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
		if (MODRM_ERROR(CPU[activeCPU].params)) //An error occurred in the read params?
		{
			invalidlockprefix: //Lock prefix when not allowed? Count as #UD!
			CPU[activeCPU].currentOP_handler = &CPU_unkOP; //Unknown opcode/parameter!
			if (unlikely(EMULATED_CPU < CPU_NECV30)) //Not supporting #UD directly? Simply abort fetching and run a NOP 'instruction'!
			{
				return 0; //Just run the NOP instruction! Don't fetch anything more!
			}
			CPU_unkOP(); //Execute the unknown opcode handler!
			return 1; //Abort!
		}
		CPU[activeCPU].MODRM_src0 = CPU[activeCPU].currentOpcodeInformation->modrm_src0; //First source!
		CPU[activeCPU].MODRM_src1 = CPU[activeCPU].currentOpcodeInformation->modrm_src1; //Second source!
		CPU[activeCPU].instructionfetch.CPU_fetchingRM = 0; //We're done fetching the R/M parameters!
	}

	if (CPU[activeCPU].currentOpcodeInformation->parameters) //Gotten parameters?
	{
		switch (CPU[activeCPU].currentOpcodeInformation->parameters&~4) //What parameters?
		{
			case 1: //imm8?
				if (CPU[activeCPU].currentOpcodeInformation->parameters&4) //Only when ModR/M REG<2?
				{
					if (MODRM_REG(CPU[activeCPU].params.modrm)<2) //8-bit immediate?
					{
						if (CPU_readOP(&CPU[activeCPU].immb,1)) return 1; //Read 8-bit immediate!
						if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
					}
				}
				else //Normal imm8?
				{
					if (CPU_readOP(&CPU[activeCPU].immb,1)) return 1; //Read 8-bit immediate!
					if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
				}
				break;
			case 2: //imm16?
				if (CPU[activeCPU].currentOpcodeInformation->parameters&4) //Only when ModR/M REG<2?
				{
					if (MODRM_REG(CPU[activeCPU].params.modrm)<2) //16-bit immediate?
					{
						if (CPU_readOPw(&CPU[activeCPU].immw,1)) return 1; //Read 16-bit immediate!
						if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
					}
				}
				else //Normal imm16?
				{
					if (CPU_readOPw(&CPU[activeCPU].immw,1)) return 1; //Read 16-bit immediate!
					if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
				}
				break;
			case 3: //imm32?
				if (CPU[activeCPU].currentOpcodeInformation->parameters&4) //Only when ModR/M REG<2?
				{
					if (MODRM_REG(CPU[activeCPU].params.modrm)<2) //32-bit immediate?
					{
						if (CPU_readOPdw(&CPU[activeCPU].imm32,1)) return 1; //Read 32-bit immediate!
						if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
					}
				}
				else //Normal imm32?
				{
					if (CPU_readOPdw(&CPU[activeCPU].imm32,1)) return 1; //Read 32-bit immediate!
					if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
				}
				break;
			case 8: //imm16 + imm8
				if (CPU[activeCPU].instructionfetch.CPU_fetchparameters==0) //First parameter?
				{
					if (CPU_readOPw(&CPU[activeCPU].immw,1)) return 1; //Read 16-bit immediate!
					if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
					CPU[activeCPU].instructionfetch.CPU_fetchparameters = 1; //Start fetching the second parameter!
					CPU[activeCPU].instructionfetch.CPU_fetchparameterPos = 0; //Init parameter position!
				}
				if (CPU[activeCPU].instructionfetch.CPU_fetchparameters==1) //Second parameter?
				{
					if (CPU_readOP(&CPU[activeCPU].immb,1)) return 1; //Read 8-bit immediate!
					if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
					CPU[activeCPU].instructionfetch.CPU_fetchparameters = 2; //We're fetching the second(finished) parameter! This way, we're done fetching!
				}
				break;
			case 9: //imm64(ptr16:32)?
				if (CPU[activeCPU].currentOpcodeInformation->parameters & 4) //Only when ModR/M REG<2?
				{
					if (MODRM_REG(CPU[activeCPU].params.modrm)<2) //32-bit immediate?
					{
						if (CPU[activeCPU].instructionfetch.CPU_fetchparameters==0) //First parameter?
						{
							if (CPU_readOPdw(&CPU[activeCPU].imm32,1)) return 1; //Read 32-bit immediate offset!
							if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
							CPU[activeCPU].imm64 = (uint_64)CPU[activeCPU].imm32; //Convert to 64-bit!
							CPU[activeCPU].instructionfetch.CPU_fetchparameters = 1; //Second parameter!
							CPU[activeCPU].instructionfetch.CPU_fetchparameterPos = 0; //Init parameter position!
						}
						if (CPU[activeCPU].instructionfetch.CPU_fetchparameters==1) //Second parameter?
						{
							if (CPU_readOPw(&CPU[activeCPU].immw,1)) return 1; //Read another 16-bit immediate!
							if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
							CPU[activeCPU].imm64 |= ((uint_64)CPU[activeCPU].immw << 32);
							CPU[activeCPU].instructionfetch.CPU_fetchparameters = 2; //We're finished!
						}
					}
				}
				else //Normal imm32?
				{
					if (CPU[activeCPU].instructionfetch.CPU_fetchparameters==0) //First parameter?
					{
						if (CPU_readOPdw(&CPU[activeCPU].imm32,1)) return 1; //Read 32-bit immediate offset!
						if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
						CPU[activeCPU].imm64 = (uint_64)CPU[activeCPU].imm32; //Convert to 64-bit!
						CPU[activeCPU].instructionfetch.CPU_fetchparameters = 1; //Second parameter!
						CPU[activeCPU].instructionfetch.CPU_fetchparameterPos = 0; //Init parameter position!
					}
					if (CPU[activeCPU].instructionfetch.CPU_fetchparameters==1) //Second parameter?
					{
						if (CPU_readOPw(&CPU[activeCPU].immw,1)) return 1; //Read another 16-bit immediate!
						if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
						CPU[activeCPU].imm64 |= ((uint_64)CPU[activeCPU].immw << 32);
						if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
						CPU[activeCPU].instructionfetch.CPU_fetchparameters = 2; //We're finished!
					}
				}
				break;
			case 0xA: //imm16/32, depending on the address size?
				if (CPU[activeCPU].CPU_Address_size) //32-bit address?
				{
					if (CPU_readOPdw(&CPU[activeCPU].immaddr32,1)) return 1; //Read 32-bit immediate offset!
					if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
				}
				else //16-bit address?
				{
					if (CPU_readOPw(&CPU[activeCPU].immw,1)) return 1; //Read 32-bit immediate offset!
					CPU[activeCPU].immaddr32 = (uint_32)CPU[activeCPU].immw; //Convert to 32-bit immediate!
					if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
				}
			default: //Unknown?
				//Ignore the parameters!
				break;
		}
	}

skipcurrentOpcodeInformations: //Skip all timings and parameters(invalid instruction)!
	CPU_resetInstructionSteps(); //Reset the current instruction steps!
	CPU[activeCPU].currentopcode = *OP; //Last OPcode for reference!
	CPU[activeCPU].currentopcode0F = CPU[activeCPU].is0Fopcode; //Last OPcode for reference!
	CPU[activeCPU].currentmodrm = (likely(CPU[activeCPU].currentOpcodeInformation)? CPU[activeCPU].currentOpcodeInformation->has_modrm:0)? CPU[activeCPU].params.modrm:0; //Modr/m if used!
	CPU[activeCPU].nextCS = REG_CS;
	CPU[activeCPU].nextEIP = REG_EIP;
	CPU[activeCPU].currentOP_handler = CurrentCPU_opcode_jmptbl[((word)*OP << 2) | (CPU[activeCPU].is0Fopcode<<1) | CPU[activeCPU].CPU_Operand_size];
	CPU_executionphase_newopcode(); //We're starting a new opcode, notify the execution phase handlers!
	return 0; //We're done fetching the instruction!
}

void doneCPU() //Finish the CPU!
{
	free_CPUregisters(); //Finish the allocated registers!
	CPU_doneBIU(); //Finish the BIU!
	memset(&CPU[activeCPU],0,sizeof(CPU[activeCPU])); //Initilialize the CPU to known state!
}

CPU_registers dummyregisters; //Dummy registers!

//Specs for 80386 says we start in REAL mode!
//STDMODE: 0=protected; 1=real; 2=Virtual 8086.

void CPU_resetMode() //Resets the mode!
{
	if (!CPU[activeCPU].registers) CPU_initRegisters(0); //Make sure we have registers!
	//Always start in REAL mode!
	if (!CPU[activeCPU].registers) return; //We can't work now!
	FLAGW_V8(0); //Disable Virtual 8086 mode!
	CPU[activeCPU].registers->CR0 &= ~CR0_PE; //Real mode!
	updateCPUmode(); //Update the CPU mode!
}

const byte modes[4] = { CPU_MODE_REAL, CPU_MODE_PROTECTED, CPU_MODE_REAL, CPU_MODE_8086 }; //All possible modes (VM86 mode can't exist without Protected Mode!)

void updateCPL() //Update the CPL to be the currently loaded CPL!
{
	byte mode = 0; //Buffer new mode to start using for comparison!
	mode = FLAG_V8; //VM86 mode?
	mode <<= 1;
	mode |= (CPU[activeCPU].registers->CR0&CR0_PE); //Protected mode?
	mode = modes[mode]; //What is the new set mode, if changed?
	//Determine CPL based on the mode!
	if (mode == CPU_MODE_PROTECTED) //Switching from real mode to protected mode?
	{
		CPU[activeCPU].CPL = GENERALSEGMENT_DPL(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_SS]); //DPL of SS determines CPL from now on!
	}
	else if (mode == CPU_MODE_8086) //Switching to Virtual 8086 mode?
	{
		CPU[activeCPU].CPL = 3; //Make sure we're CPL 3 in Virtual 8086 mode!
	}
	else //Switching back to real mode?
	{
		CPU[activeCPU].CPL = 0; //Make sure we're CPL 0 in Real mode!
	}
}

void updateCPUmode() //Update the CPU mode!
{
	byte mode = 0; //Buffer new mode to start using for comparison!
	if (unlikely(!CPU[activeCPU].registers)) //Unusable registers?
	{
		CPU_initRegisters(0); //Make sure we have registers!
		if (!CPU[activeCPU].registers) CPU[activeCPU].registers = &dummyregisters; //Dummy registers!
	}
	CPU[activeCPU].is_aligning = ((EMULATED_CPU >= CPU_80486) && FLAGREGR_AC(CPU[activeCPU].registers) && (CPU[activeCPU].registers->CR0 & 0x40000)); //Alignment check in effect for CPL 3?
	mode = FLAG_V8; //VM86 mode?
	mode <<= 1;
	mode |= (CPU[activeCPU].registers->CR0&CR0_PE); //Protected mode?
	mode = modes[mode]; //What is the new set mode, if changed?
	CPU[activeCPU].is_paging = ((mode != CPU_MODE_REAL) & ((CPU[activeCPU].registers->CR0 & CR0_PG) >> 31)); //Are we paging in protected mode!
	if (unlikely(mode!=CPU[activeCPU].CPUmode)) //Mode changed?
	{
		//Always set CPUmode before calculating precalcs, to make sure that switches are handled correctly since it relies on those.
		if ((CPU[activeCPU].CPUmode == CPU_MODE_REAL) && (mode == CPU_MODE_PROTECTED)) //Switching from real mode to protected mode?
		{
			CPU[activeCPU].CPL = GENERALSEGMENT_DPL(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_SS]); //DPL of SS determines CPL from now on!
			CPU[activeCPU].CPUmode = mode; //Mode levels: Real mode > Protected Mode > VM86 Mode!
			CPU_calcSegmentPrecalcs(1,&CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS]); //Calculate the precalcs for the segment descriptor!
		}
		else if ((CPU[activeCPU].CPUmode != CPU_MODE_REAL) && (mode == CPU_MODE_REAL)) //Switching back to real mode?
		{
			CPU[activeCPU].CPL = 0; //Make sure we're CPL 0 in Real mode!
			CPU[activeCPU].CPUmode = mode; //Mode levels: Real mode > Protected Mode > VM86 Mode!
			CPU_calcSegmentPrecalcs(1,&CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS]); //Calculate the precalcs for the segment descriptor!
		}
		else if ((CPU[activeCPU].CPUmode != CPU_MODE_8086) && (mode == CPU_MODE_8086)) //Switching to Virtual 8086 mode?
		{
			CPU[activeCPU].CPUmode = mode; //Mode levels: Real mode > Protected Mode > VM86 Mode!
			CPU[activeCPU].CPL = 3; //Make sure we're CPL 3 in Virtual 8086 mode!
		}
		else //All other modes switches or no mode switch?
		{
			CPU[activeCPU].CPUmode = mode; //Mode levels: Real mode > Protected Mode > VM86 Mode!
		}
	}
}

byte getcpumode() //Retrieves the current mode!
{
	return CPU[activeCPU].CPUmode; //Give the current CPU mode!
}

byte isPM()
{
	return (CPU[activeCPU].CPUmode!=CPU_MODE_REAL)?1:0; //Are we in protected mode?
}

byte isV86()
{
	return (CPU[activeCPU].CPUmode==CPU_MODE_8086)?1:0; //Are we in virtual 8086 mode?
}

//Final stuff:

char textsegments[8][5] =   //Comply to CPU_REGISTER_XX order!
{
	"CS",
	"SS",
	"DS",
	"ES",
	"FS",
	"GS",
	"TR",
	"LDTR"
};

char *CPU_textsegment(byte defaultsegment) //Plain segment to use!
{
	if (CPU[activeCPU].segment_register==CPU_SEGMENT_DEFAULT) //Default segment?
	{
		return &textsegments[defaultsegment][0]; //Default segment!
	}
	return &textsegments[CPU[activeCPU].segment_register][0]; //Use Data Segment (or different in case) for data!
}

char* CPU_segmentname(byte segment) //Plain segment to use!
{
	if (unlikely(segment >= NUMITEMS(textsegments))) //Invalid segment?
	{
		return NULL; //Invalid segment!
	}
	return &textsegments[segment][0]; //Give the name of the segment!
}

void CPU_afterexec(); //Prototype for below!

void CPU_beforeexec()
{
	CPU_filterflags();
	if (CPU[activeCPU].instructionfetch.CPU_isFetching && (CPU[activeCPU].instructionfetch.CPU_fetchphase==1)) //Starting a new instruction?
	{
		CPU[activeCPU].trapped = FLAG_TF; //Are we to be trapped this instruction?
	}
}

void CPU_RealResetOP(byte doReset); //Rerun current Opcode? (From interrupt calls this recalls the interrupts, handling external calls in between)

//specialReset: 1 for exhibiting bug and flushing PIQ, 0 otherwise
void CPU_8086REPPending(byte doReset) //Execute this before CPU_exec!
{
	if (CPU[activeCPU].REPPending) //Pending REP?
	{
		CPU[activeCPU].REPPending = 0; //Disable pending REP!
		CPU_RealResetOP(doReset); //Rerun the last instruction!
	}
}

byte CPU_segmentOverridden(byte TheActiveCPU)
{
	return (CPU[TheActiveCPU].segment_register != CPU_SEGMENT_DEFAULT); //Is the segment register overridden?
}

void CPU_resetTimings()
{
	CPU[activeCPU].cycles_HWOP = 0; //No hardware interrupt to use anymore!
	CPU[activeCPU].cycles_Prefetch_BIU = 0; //Reset cycles spent on BIU!
	CPU[activeCPU].cycles_Prefix = 0; //No cycles prefix to use anymore!
	CPU[activeCPU].cycles_Exception = 0; //No cycles Exception to use anymore!
	CPU[activeCPU].cycles_Prefetch = 0; //No cycles prefetch to use anymore!
	CPU[activeCPU].cycles_OP = 0; //Reset cycles (used by CPU to check for presets (see below))!
	CPU[activeCPU].cycles_stallBIU = 0; //Reset cycles to stall (used by BIU to check for stalling during any jump (see below))!
	CPU[activeCPU].cycles_stallBUS = 0; //Reset cycles to stall the BUS!
	CPU[activeCPU].cycles_Prefetch_DMA = 0; //Reset cycles spent on DMA by the BIU!
	CPU[activeCPU].cycles_EA = 0; //Reset EA cycles!
}

extern BIU_type BIU[MAXCPUS]; //All possible BIUs!

//Stuff for CPU 286+ timing processing!
void CPU_prepareHWint() //Prepares the CPU for hardware interrupts!
{
	MMU_resetaddr(); //Reset invalid address for our usage!
	protection_nextOP(); //Prepare protection for the next instruction!
	CPU[activeCPU].CPU_interruptraised = 0; //Default: no interrupt raised!
	CPU[activeCPU].faultraised = 0; //Default fault raised!
	CPU[activeCPU].faultlevel = 0; //Default to no fault level!
}

extern byte BIU_buslocked; //BUS locked?

void CPU_exec() //Processes the opcode at CS:EIP (386) or CS:IP (8086).
{
	uint_32 REPcondition; //What kind of condition?
	//byte cycles_counted = 0; //Cycles have been counted?
	if (likely((BIU_Ready()&&(CPU[activeCPU].halt==0))==0)) //BIU not ready to continue? We're handling seperate cycles still!
	{
		CPU[activeCPU].executed = 0; //Not executing anymore!
		goto BIUWaiting; //Are we ready to step the Execution Unit?
	}
	if (CPU[activeCPU].resetPending == 2) //Hanging reset pending?
	{
		CPU[activeCPU].executed = 1; //Executed with nothing to do!
		goto BIUWaiting;
	}
	if (CPU[activeCPU].preinstructiontimingnotready&1) //Timing not ready yet?
	{
		goto CPUtimingready; //We might be ready for execution now?
	}
	if (CPU_executionphase_busy()) //Busy during execution?
	{
		goto executionphase_running; //Continue an running instruction!
	}
	if (CPU[activeCPU].instructionfetch.CPU_isFetching && (CPU[activeCPU].instructionfetch.CPU_fetchphase==1)) //Starting a new instruction(or repeating one)?
	{
		CPU[activeCPU].allowTF = 1; //Default: allow TF to be triggered after the instruction!
		CPU[activeCPU].debuggerFaultRaised = 0; //Default: no debugger fault raised!
		CPU[activeCPU].unaffectedRF = 0; //Default: affected!
		//bufferMMU(); //Buffer the MMU writes for us!
		debugger_beforeCPU(); //Everything that needs to be done before the CPU executes!
		MMU_resetaddr(); //Reset invalid address for our usage!
		CPU_8086REPPending(0); //Process pending REP!
		protection_nextOP(); //Prepare protection for the next instruction!
		reset_modrmall(); //Reset all modr/m related settings that are supposed to be reset each instruction, both REP and non-REP!
		CPU[activeCPU].blockREP = 0; //Default: nothing to do with REP!
		if (!CPU[activeCPU].repeating)
		{
			MMU_clearOP(); //Clear the OPcode buffer in the MMU (equal to our instruction cache) when not repeating!
			BIU_instructionStart(); //Handle all when instructions are starting!
		}

		CPU[activeCPU].newpreviousCSstart = (uint_32)CPU_MMU_start(CPU_SEGMENT_CS,REG_CS); //Save the used CS start address!

		if (CPU[activeCPU].permanentreset) //We've entered a permanent reset?
		{
			CPU[activeCPU].cycles = 4; //Small cycle dummy! Must be greater than zero!
			return; //Don't run the CPU: we're in a permanent reset state!
		}
#ifdef DEBUG_BIUFIFO
		if (fifobuffer_freesize(BIU[activeCPU].responses)!=BIU[activeCPU].responses->size) //Starting an instruction with a response remaining?
		{
			dolog("CPU","Warning: starting instruction with BIU still having a result buffered! Previous instruction: %02X(0F:%i,ModRM:%02X)@%04X:%08x",CPU[activeCPU].previousopcode,CPU[activeCPU].previousopcode0F,CPU[activeCPU].previousmodrm,CPU[activeCPU].exec_CS,CPU[activeCPU].exec_EIP);
			BIU_readResultb(&BIUresponsedummy); //Discard the result: we're logging but continuing on simply!
		}
#endif

		CPU[activeCPU].have_oldCPL = 0; //Default: no CPL to return to during exceptions!
		CPU[activeCPU].have_oldESP = 0; //Default: no ESP to return to during exceptions!
		CPU[activeCPU].have_oldESPinstr = 0; //Default: no ESP to return to during page loads!
		CPU[activeCPU].have_oldEBP = 0; //Default: no EBP to return to during exceptions!
		CPU[activeCPU].have_oldEFLAGS = 0; //Default: no EFLAGS to return during exceptions!

		//Initialize stuff needed for local CPU timing!
		CPU[activeCPU].didJump = 0; //Default: we didn't jump!
		CPU[activeCPU].ENTER_L = 0; //Default to no L depth!
		CPU[activeCPU].hascallinterrupttaken_type = 0xFF; //Default to no call/interrupt taken type!
		CPU[activeCPU].CPU_interruptraised = 0; //Default: no interrupt raised!

		//Now, starting the instruction preprocessing!
		CPU[activeCPU].is_reset = 0; //We're not reset anymore from now on!
		if (!CPU[activeCPU].repeating) //Not repeating instructions?
		{
			CPU[activeCPU].segment_register = CPU_SEGMENT_DEFAULT; //Default data segment register (default: auto)!
			//Save the last coordinates!
			CPU[activeCPU].exec_lastCS = CPU[activeCPU].exec_CS;
			CPU[activeCPU].exec_lastEIP = CPU[activeCPU].exec_EIP;
			//Save the current coordinates!
			CPU[activeCPU].exec_CS = REG_CS; //CS of command!
			CPU[activeCPU].exec_EIP = (REG_EIP&CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS].PRECALCS.roof); //EIP of command!
		}
	
		//Save the starting point when debugging!
		CPU[activeCPU].CPU_debugger_CS = CPU[activeCPU].exec_CS;
		CPU[activeCPU].CPU_debugger_EIP = CPU[activeCPU].exec_EIP;
		CPU_commitState(); //Save any fault data!

		if (getcpumode()!=CPU_MODE_REAL) //Protected mode?
		{
			if (CPU[activeCPU].allowInterrupts) //Do we allow interrupts(and traps) to be fired?
			{
				if (checkProtectedModeDebugger(CPU[activeCPU].newpreviousCSstart+CPU[activeCPU].exec_EIP,PROTECTEDMODEDEBUGGER_TYPE_EXECUTION)) //Breakpoint at the current address(linear address space)?
				{
					return; //Protected mode debugger activated! Don't fetch or execute!
				}
			}
		}

		CPU[activeCPU].faultraised = 0; //Default fault raised!
		CPU[activeCPU].faultlevel = 0; //Default to no fault level!

		if (CPU[activeCPU].cpudebugger) //Debugging?
		{
			cleardata(&CPU[activeCPU].debugtext[0], sizeof(CPU[activeCPU].debugtext)); //Init debugger!
		}

		if (FLAG_VIP && FLAG_VIF && CPU[activeCPU].allowInterrupts) //VIP and VIF both set on the new code?
		{
			CPU_commitState(); //Commit to the new instruction!
			THROWDESCGP(0, 0, 0); //#GP(0)!
			return; //Abort! Don't fetch or execute!
		}
		CPU[activeCPU].previousAllowInterrupts = CPU[activeCPU].allowInterrupts; //Were interrupts inhibited for this instruction?
		CPU[activeCPU].allowInterrupts = 1; //Allow interrupts again after this instruction!
	}

	if (CPU[activeCPU].repeating) //REPeating instruction?
	{
		CPU[activeCPU].OP = CPU[activeCPU].currentopcode; //Execute the last opcode again!
		CPU[activeCPU].newREP = 0; //Not a new repeating instruction!
		if (CPU[activeCPU].instructionfetch.CPU_isFetching && (CPU[activeCPU].instructionfetch.CPU_fetchphase==1)) //New instruction to start?
		{
			CPU_resetInstructionSteps(); //Reset all timing that's still running!
			CPU_executionphase_newopcode(); //We're starting a new opcode, notify the execution phase handlers!
		}
		memset(&CPU[activeCPU].instructionfetch,0,sizeof(CPU[activeCPU].instructionfetch)); //Not fetching anything anymore, we're ready to use!
	}
	else //Not a repeating instruction?
	{
		if (CPU[activeCPU].instructionfetch.CPU_isFetching) //Are we fetching?
		{
			CPU[activeCPU].executed = 0; //Not executed yet!
			if (CPU_readOP_prefix(&CPU[activeCPU].OP)) //Finished 
			{
				if (!CPU[activeCPU].cycles_OP) CPU[activeCPU].cycles_OP = 1; //Take 1 cycle by default!
				if (CPU[activeCPU].faultraised) //Fault has been raised while fetching&decoding the instruction?
				{
					memset(&CPU[activeCPU].instructionfetch,0,sizeof(CPU[activeCPU].instructionfetch)); //Finished fetching!
					CPU[activeCPU].instructionfetch.CPU_isFetching = CPU[activeCPU].instructionfetch.CPU_fetchphase = 1; //Start fetching the next instruction when available(not repeating etc.)!
					CPU[activeCPU].executed = 1; //We're counting as an finished instruction to handle the fault!
				}
				goto fetchinginstruction; //Process prefix(es) and read OPCode!
			}
			if (CPU[activeCPU].cycles_EA==0)
			{
				memset(&CPU[activeCPU].instructionfetch,0,sizeof(CPU[activeCPU].instructionfetch)); //Finished fetching!
			}
			else //EA cycles still set? We're pending EA!
			{
				goto fetchinginstruction; //EA cycles are timing!
			}
		}
		if (CPU[activeCPU].faultraised) goto skipexecutionOPfault; //Abort on fault!
		CPU[activeCPU].newREP = 1; //We're a new repeating instruction!
	}

	//Handle all prefixes!
	if (CPU[activeCPU].cpudebugger) debugger_setprefix(""); //Reset prefix for the debugger!
	CPU[activeCPU].gotREP = 0; //Default: no REP-prefix used!
	CPU[activeCPU].REPZ = 0; //Init REP to REPNZ/Unused zero flag(during REPNE)!
	CPU[activeCPU].REPfinishtiming = 0; //Default: no finish timing!
	if (CPU_getprefix(0xF2)) //REPNE Opcode set?
	{
		CPU[activeCPU].gotREP = 1; //We've gotten a repeat!
		CPU[activeCPU].REPZ = 0; //Allow and we're not REPZ!
		switch (CPU[activeCPU].OP) //Which special adjustment cycles Opcode?
		{
		//80186+ REP opcodes!
		case 0x6C: //A4: REP INSB
		case 0x6D: //A4: REP INSW
		case 0x6E: //A4: REP OUTSB
		case 0x6F: //A4: REP OUTSW
			//REPNZ INSB/INSW and REPNZ OUTSB/OUTSW doesn't exist! But handle us as a plain REP!
			if (CPU[activeCPU].is0Fopcode) goto noREPNE0Fand8086; //0F opcode?
			if (EMULATED_CPU < CPU_NECV30) goto noREPNE0Fand8086; //Not existant on 8086!
			break;

		//8086 REPable opcodes!	
		//New:
		case 0xA4: //A4: REPNZ MOVSB
			if (CPU[activeCPU].is0Fopcode) goto noREPNE0Fand8086; //0F opcode?
			break;
		case 0xA5: //A5: REPNZ MOVSW
			if (CPU[activeCPU].is0Fopcode) goto noREPNE0Fand8086; //0F opcode?
			break;

		//Old:
		case 0xA6: //A6: REPNZ CMPSB
			if (CPU[activeCPU].is0Fopcode) goto noREPNE0Fand8086; //0F opcode?
			CPU[activeCPU].REPZ = 1; //Check the zero flag!
			if (EMULATED_CPU<=CPU_NECV30) CPU[activeCPU].REPfinishtiming = 3; //Finish timing 3 in instruction!
			break;
		case 0xA7: //A7: REPNZ CMPSW
			if (CPU[activeCPU].is0Fopcode) goto noREPNE0Fand8086; //0F opcode?
			CPU[activeCPU].REPZ = 1; //Check the zero flag!
			if (EMULATED_CPU<=CPU_NECV30) CPU[activeCPU].REPfinishtiming = 3; //Finish timing 3+4 in instruction!
			break;

		//New:
		case 0xAA: //AA: REPNZ STOSB
			if (CPU[activeCPU].is0Fopcode) goto noREPNE0Fand8086; //0F opcode?
			break;
		case 0xAB: //AB: REPNZ STOSW
			if (CPU[activeCPU].is0Fopcode) goto noREPNE0Fand8086; //0F opcode?
			break;
		case 0xAC: //AC: REPNZ LODSB
			if (CPU[activeCPU].is0Fopcode) goto noREPNE0Fand8086; //0F opcode?
			break;
		case 0xAD: //AD: REPNZ LODSW
			if (CPU[activeCPU].is0Fopcode) goto noREPNE0Fand8086; //0F opcode?
			break;

		//Old:
		case 0xAE: //AE: REPNZ SCASB
			if (CPU[activeCPU].is0Fopcode) goto noREPNE0Fand8086; //0F opcode?
			CPU[activeCPU].REPZ = 1; //Check the zero flag!
			if (EMULATED_CPU<=CPU_NECV30) CPU[activeCPU].REPfinishtiming += 3; //Finish timing 3+4 in instruction!
			break;
		case 0xAF: //AF: REPNZ SCASW
			if (CPU[activeCPU].is0Fopcode) goto noREPNE0Fand8086; //0F opcode?
			CPU[activeCPU].REPZ = 1; //Check the zero flag!
			if (EMULATED_CPU<=CPU_NECV30) CPU[activeCPU].REPfinishtiming += 3; //Finish timing 3+4 in instruction!
			break;
		default: //Unknown yet?
		noREPNE0Fand8086: //0F/8086 #UD exception!
			CPU[activeCPU].gotREP = 0; //Dont allow after all!
			CPU[activeCPU].cycles_OP = 0; //Unknown!
			break; //Not supported yet!
		}
	}
	else if (CPU_getprefix(0xF3)) //REP/REPE Opcode set?
	{
		CPU[activeCPU].gotREP = 1; //Allow!
		CPU[activeCPU].REPZ = 0; //Don't check the zero flag: it maybe so in assembly, but not in execution!
		switch (CPU[activeCPU].OP) //Which special adjustment cycles Opcode?
		{
		//80186+ REP opcodes!
		case 0x6C: //A4: REP INSB
		case 0x6D: //A4: REP INSW
		case 0x6E: //A4: REP OUTSB
		case 0x6F: //A4: REP OUTSW
			if (CPU[activeCPU].is0Fopcode) goto noREPNE0Fand8086; //0F opcode?
			if (EMULATED_CPU < CPU_NECV30) goto noREPNE0Fand8086; //Not existant on 8086!
			break;
			//8086 REP opcodes!
		case 0xA4: //A4: REP MOVSB
			if (CPU[activeCPU].is0Fopcode) goto noREPE0Fand8086; //0F opcode?
			break;
		case 0xA5: //A5: REP MOVSW
			if (CPU[activeCPU].is0Fopcode) goto noREPE0Fand8086; //0F opcode?
			break;
		case 0xA6: //A6: REPE CMPSB
			if (CPU[activeCPU].is0Fopcode) goto noREPE0Fand8086; //0F opcode?
			CPU[activeCPU].REPZ = 1; //Check the zero flag!
			if (EMULATED_CPU<=CPU_NECV30) CPU[activeCPU].REPfinishtiming = 3; //Finish timing 3+4 in instruction!
			break;
		case 0xA7: //A7: REPE CMPSW
			if (CPU[activeCPU].is0Fopcode) goto noREPE0Fand8086; //0F opcode?
			CPU[activeCPU].REPZ = 1; //Check the zero flag!
			if (EMULATED_CPU<=CPU_NECV30) CPU[activeCPU].REPfinishtiming = 3; //Finish timing 3+4 in instruction!
			break;
		case 0xAA: //AA: REP STOSB
			if (CPU[activeCPU].is0Fopcode) goto noREPE0Fand8086; //0F opcode?
			break;
		case 0xAB: //AB: REP STOSW
			if (CPU[activeCPU].is0Fopcode) goto noREPE0Fand8086; //0F opcode?
			break;
		case 0xAC: //AC: REP LODSB
			if (CPU[activeCPU].is0Fopcode) goto noREPE0Fand8086; //0F opcode?
			break;
		case 0xAD: //AD: REP LODSW
			if (CPU[activeCPU].is0Fopcode) goto noREPE0Fand8086; //0F opcode?
			break;
		case 0xAE: //AE: REPE SCASB
			if (CPU[activeCPU].is0Fopcode) goto noREPE0Fand8086; //0F opcode?
			CPU[activeCPU].REPZ = 1; //Check the zero flag!
			if (EMULATED_CPU<=CPU_NECV30) CPU[activeCPU].REPfinishtiming = 3; //Finish timing 3+4 in instruction!
			break;
		case 0xAF: //AF: REPE SCASW
			if (CPU[activeCPU].is0Fopcode) goto noREPE0Fand8086; //0F opcode?
			CPU[activeCPU].REPZ = 1; //Check the zero flag!
			if (EMULATED_CPU<=CPU_NECV30) CPU[activeCPU].REPfinishtiming = 3; //Finish timing 3+4 in instruction!
			break;
		default: //Unknown yet?
			noREPE0Fand8086: //0F exception!
			CPU[activeCPU].gotREP = 0; //Don't allow after all!
			break; //Not supported yet!
		}
	}

	if (CPU[activeCPU].gotREP) //Gotten REP?
	{
		if (!(CPU[activeCPU].CPU_Address_size?REG_ECX:REG_CX)) //REP and finished?
		{
			CPU[activeCPU].blockREP = 1; //Block the CPU instruction from executing!
		}
	}

	if (unlikely(CPU[activeCPU].cpudebugger)) //Need to set any debugger info?
	{
		if (CPU_getprefix(0xF0)) //LOCK?
		{
			debugger_setprefix("LOCK"); //LOCK!
		}
		if (CPU[activeCPU].gotREP) //REPeating something?
		{
			if (CPU_getprefix(0xF2)) //REPNZ?
			{
				debugger_setprefix("REPNZ"); //Set prefix!
			}
			else if (CPU_getprefix(0xF3)) //REP/REPZ?
			{
				if (CPU[activeCPU].REPZ) //REPZ?
				{
					debugger_setprefix("REPZ"); //Set prefix!
				}
				else //REP?
				{
					debugger_setprefix("REP"); //Set prefix!
				}
			}
		}
	}

	CPU[activeCPU].preinstructiontimingnotready = 0; //Timing ready for the instruction to execute?
	CPUtimingready: //Timing in preparation?
	if ((CPU[activeCPU].is0Fopcode == 0) && CPU[activeCPU].newREP) //REP instruction affected?
	{
		switch (CPU[activeCPU].OP) //Check for string instructions!
		{
		case 0xA4:
		case 0xA5: //MOVS
		case 0xAC:
		case 0xAD: //LODS
			if ((CPU[activeCPU].preinstructiontimingnotready == 0) && (CPU[activeCPU].repeating == 0)) //To start ready timing?
			{
				CPU[activeCPU].cycles_OP += 1; //1 cycle for starting REP MOVS!
			}
			if ((CPU[activeCPU].preinstructiontimingnotready==0) && (CPU[activeCPU].repeating==0)) //To start ready timing?
			{
				CPU[activeCPU].preinstructiontimingnotready = 1; //Timing not ready for the instruction to execute?
				CPU[activeCPU].executed = 0; //Not executed yet!
				goto BIUWaiting; //Wait for the BIU to finish!
			}
			CPU[activeCPU].preinstructiontimingnotready = 2; //Finished and ready to check for cycles now!
			if (BIU_getcycle() == 0) CPU[activeCPU].cycles_OP += 1; //1 cycle for idle bus?
			if (((CPU[activeCPU].OP == 0xA4) || (CPU[activeCPU].OP == 0xA5)) && CPU[activeCPU].gotREP && (CPU[activeCPU].repeating==0)) //REP MOVS starting?
			{
				CPU[activeCPU].cycles_OP += 1; //1 cycle for starting REP MOVS!
			}
			break;
		case 0xA6:
		case 0xA7: //CMPS
		case 0xAE:
		case 0xAF: //SCAS
			if (CPU[activeCPU].repeating == 0) CPU[activeCPU].cycles_OP += 1; //1 cycle for non-REP!
			break;
		case 0xAA:
		case 0xAB: //STOS
			if ((CPU[activeCPU].preinstructiontimingnotready == 0) && (CPU[activeCPU].repeating == 0)) //To start ready timing?
			{
				CPU[activeCPU].cycles_OP += 1; //1 cycle for non-REP!
			}
			if ((CPU[activeCPU].preinstructiontimingnotready == 0) && (CPU[activeCPU].repeating==0)) //To start ready timing?
			{
				CPU[activeCPU].preinstructiontimingnotready = 1; //Timing not ready for the instruction to execute?
				CPU[activeCPU].executed = 0; //Not executed yet!
				goto BIUWaiting; //Wait for the BIU to finish!
			}
			CPU[activeCPU].preinstructiontimingnotready = 2; //Finished and ready to check for cycles now!
			if (BIU_getcycle() == 0) CPU[activeCPU].cycles_OP += 1; //1 cycle for idle bus?
			if (CPU[activeCPU].gotREP && (CPU[activeCPU].repeating==0)) CPU[activeCPU].cycles_OP += 1; //1 cycle for REP prefix used!
			break;
		default: //Not a string instruction?
			break;
		}
	}

	//Perform REPaction timing before instructions!
	if (CPU[activeCPU].gotREP) //Starting/continuing a REP instruction? Not finishing?
	{
		//Don't apply finishing timing, this is done automatically!
		CPU[activeCPU].cycles_OP += 2; //rep active!
		//Check for pending interrupts could be done here?
		if (unlikely(CPU[activeCPU].blockREP)) ++CPU[activeCPU].cycles_OP; //1 cycle for CX=0 or interrupt!
		else //Normally repeating?
		{
			CPU[activeCPU].cycles_OP += 2; //CX used!
			if (CPU[activeCPU].newREP) CPU[activeCPU].cycles_OP += 2; //New REP takes two cycles!
			switch (CPU[activeCPU].OP)
			{
			case 0xAC:
			case 0xAD: //LODS
				CPU[activeCPU].cycles_OP += 2; //2 cycles!
			case 0xA4:
			case 0xA5: //MOVS
			case 0xAA:
			case 0xAB: //STOS
				CPU[activeCPU].cycles_OP += 1; //1 cycle!
				break;
			case 0xA6:
			case 0xA7: //CMPS
			case 0xAE:
			case 0xAF: //SCAS
				CPU[activeCPU].cycles_OP += 2; //2 cycles!
				break;
			default: //Unknown timings?
				break;
			}
		}
	}
	
	CPU[activeCPU].didRepeating = CPU[activeCPU].repeating; //Were we doing REP?
	CPU[activeCPU].didNewREP = CPU[activeCPU].newREP; //Were we doing a REP for the first time?
	CPU[activeCPU].gotREP = CPU[activeCPU].gotREP; //Did we got REP?
	CPU[activeCPU].executed = 0; //Not executing it yet, wait for the BIU to catch up if required!
	CPU[activeCPU].preinstructiontimingnotready = 0; //We're ready now!
	goto fetchinginstruction; //Just update the BIU until it's ready to start executing the instruction!
	executionphase_running:
	CPU[activeCPU].executed = 1; //Executed by default!
	CPU_OP(); //Now go execute the OPcode once!
	debugger_notifyRunning(); //Notify the debugger we've started running!
	skipexecutionOPfault: //Instruction fetch fault?
	if (CPU[activeCPU].executed) //Are we finished executing?
	{
		if (BIU[activeCPU]._lock && (BIU[activeCPU].BUSlockowned)) //Locked the bus and we own the lock?
		{
			BIU_buslocked = 0; //Not anymore!
			BIU[activeCPU].BUSlockowned = 0; //Not owning it anymore!
			BIU[activeCPU].BUSlockrequested = 0; //Don't request the lock from the bus!								
		}
		BIU[activeCPU]._lock = 0; //Unlock!
		//Prepare for the next (fetched or repeated) instruction to start executing!
		CPU[activeCPU].instructionfetch.CPU_isFetching = CPU[activeCPU].instructionfetch.CPU_fetchphase = 1; //Start fetching the next instruction when available(not repeating etc.)!
		//Handle REP instructions post-instruction next!
		if (CPU[activeCPU].gotREP && !CPU[activeCPU].faultraised && !CPU[activeCPU].blockREP) //Gotten REP, no fault/interrupt has been raised and we're executing?
		{
			if (unlikely(CPU[activeCPU].REPZ && (CPU_getprefix(0xF2) || CPU_getprefix(0xF3)))) //REP(N)Z used?
			{
				CPU[activeCPU].gotREP &= (FLAG_ZF^CPU_getprefix(0xF2)); //Reset the opcode when ZF doesn't match(needs to be 0 to keep looping).
				if (CPU[activeCPU].gotREP == 0) //Finished?
				{
					CPU[activeCPU].cycles_OP += 4; //4 cycles for finishing!
				}
			}
			if (CPU[activeCPU].CPU_Address_size) //32-bit REP?
			{
				REPcondition = REG_ECX--; //ECX set and decremented?
			}
			else
			{
				REPcondition = REG_CX--; //CX set and decremented?
			}
			if (REPcondition && CPU[activeCPU].gotREP) //Still looping and allowed? Decrease (E)CX after checking for the final item!
			{
				CPU[activeCPU].REPPending = CPU[activeCPU].repeating = 1; //Run the current instruction again and flag repeat!
			}
			else //Finished looping?
			{
				CPU[activeCPU].cycles_OP += CPU[activeCPU].REPfinishtiming; //Apply finishing REP timing!
				CPU[activeCPU].repeating = 0; //Not repeating anymore!
			}
		}
		else
		{
			if (unlikely(CPU[activeCPU].gotREP && !CPU[activeCPU].faultraised)) //Finished REP?
			{
				CPU[activeCPU].cycles_OP += CPU[activeCPU].REPfinishtiming; //Apply finishing REP timing!
			}
			CPU[activeCPU].REPPending = CPU[activeCPU].repeating = 0; //Not repeating anymore!
		}
		CPU[activeCPU].blockREP = 0; //Don't block REP anymore!
	}
	fetchinginstruction: //We're still fetching the instruction in some way?
	//Apply the ticks to our real-time timer and BIU!
	//Fall back to the default handler on 80(1)86 systems!
	#ifdef CPU_USECYCLES
	if ((CPU[activeCPU].cycles_OP|CPU[activeCPU].cycles_stallBIU|CPU[activeCPU].cycles_stallBUS|CPU[activeCPU].cycles_EA|CPU[activeCPU].cycles_HWOP|CPU[activeCPU].cycles_Exception) && CPU_useCycles) //cycles entered by the instruction?
	{
		CPU[activeCPU].cycles = CPU[activeCPU].cycles_OP+CPU[activeCPU].cycles_EA+CPU[activeCPU].cycles_HWOP+CPU[activeCPU].cycles_Prefix + CPU[activeCPU].cycles_Exception + CPU[activeCPU].cycles_Prefetch; //Use the cycles as specified by the instruction!
	}
	else //Automatic cycles placeholder?
	{
	#endif
		CPU[activeCPU].cycles = 0; //Default to only 0 cycle at least(no cycles aren't allowed).
	#ifdef CPU_USECYCLES
	}
	//cycles_counted = 1; //Cycles have been counted!
	#endif

	if (CPU[activeCPU].executed) //Are we finished executing?
	{
		CPU_afterexec(); //After executing OPCode stuff!
		CPU[activeCPU].previousopcode = CPU[activeCPU].currentopcode; //Last executed OPcode for reference purposes!
		CPU[activeCPU].previousopcode0F = CPU[activeCPU].is0Fopcode; //Last executed OPcode for reference purposes!
		CPU[activeCPU].previousmodrm = CPU[activeCPU].currentmodrm; //Last executed OPcode for reference purposes!
		CPU[activeCPU].previousCSstart = CPU[activeCPU].newpreviousCSstart; //Save the start address of CS for the last instruction!
	}
	if ((CPU[activeCPU].cycles|CPU[activeCPU].cycles_stallBUS)==0) //Nothing ticking?
	{
		CPU[activeCPU].BIUnotticking = 1; //We're not ticking!
		goto dontTickBIU; //Don't tick the BIU!
	}
BIUWaiting: //The BIU is waiting!
	CPU[activeCPU].BIUnotticking = 0; //We're ticking normally!
	CPU_tickBIU(); //Tick the prefetch as required!
dontTickBIU:
	flushMMU(); //Flush MMU writes!
}

void CPU_afterexec() //Stuff to do after execution of the OPCode (cycular tasks etc.)
{
	if (FLAG_TF) //Trapped and to be trapped this instruction?
	{
		if (CPU[activeCPU].trapped && CPU[activeCPU].allowInterrupts && (CPU[activeCPU].allowTF) && ((FLAG_RF==0)||(EMULATED_CPU<CPU_80386)) && (CPU[activeCPU].faultraised==0)) //Are we trapped and allowed to trap?
		{
			CPU_exSingleStep(); //Type-1 interrupt: Single step interrupt!
			if (CPU[activeCPU].trapped) return; //Continue on while handling us!
			CPU_afterexec(); //All after execution fixing!
			return; //Abort: we're finished!
		}
	}

	checkProtectedModeDebuggerAfter(); //Check after executing the current instruction!
}

void CPU_RealResetOP(byte doReset) //Rerun current Opcode? (From interrupt calls this recalls the interrupts, handling external calls in between)
{
	if (likely(doReset)) //Not a repeating reset?
	{
		//Actually reset the currrent instruction!
		REG_CS = CPU[activeCPU].exec_CS; //CS is reset!
		REG_EIP = CPU[activeCPU].exec_EIP; //Destination address is reset!
		CPU_flushPIQ(CPU[activeCPU].exec_EIP); //Flush the PIQ, restoring the destination address to the start of the instruction!
	}
}

void CPU_resetOP() //Rerun current Opcode? (From interrupt calls this recalls the interrupts, handling external calls in between)
{
	CPU_RealResetOP(1); //Non-repeating reset!
}

//Exceptions!

//8086+ exceptions (real mode)

extern byte advancedlog; //Advanced log setting

extern byte MMU_logging; //Are we logging from the MMU?

void CPU_exDIV0() //Division by 0!
{
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger","#DE fault(-1)!");
	}

	if (CPU_faultraised(EXCEPTION_DIVIDEERROR)==0)
	{
		return; //Abort handling when needed!
	}
	if (EMULATED_CPU > CPU_8086) //We don't point to the instruction following the division?
	{
		CPU_resetOP(); //Return to the instruction instead!
	}
	//Else: Points to next opcode!

	CPU_executionphase_startinterrupt(EXCEPTION_DIVIDEERROR,0,-1); //Execute INT0 normally using current CS:(E)IP!
}

extern byte HWINT_nr, HWINT_saved; //HW interrupt saved?

void CPU_exSingleStep() //Single step (after the opcode only)
{
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger","#DB fault(-1)!");
	}

	if (CPU_faultraised(EXCEPTION_DEBUG)==0)
	{
		return; //Abort handling when needed!
	}
	HWINT_nr = 1; //Trapped INT NR!
	HWINT_saved = 1; //We're trapped!
	//Points to next opcode!
	CPU[activeCPU].tempcycles = CPU[activeCPU].cycles_OP; //Save old cycles!
	//if (EMULATED_CPU >= CPU_80386) FLAGW_RF(1); //Automatically set the resume flag on a debugger fault!
	SETBITS(CPU[activeCPU].registers->DR6, 14, 1, 1); //Set bit 14, the single-step trap indicator!
	CPU_commitState(); //Save the current state for any future faults to return to!
	CPU_executionphase_startinterrupt(EXCEPTION_DEBUG,2,-1); //Execute INT1 normally using current CS:(E)IP!
	CPU[activeCPU].trapped = 0; //We're not trapped anymore: we're handling the single-step!
}

void CPU_BoundException() //Bound exception!
{
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger","#BR fault(-1)!");
	}

	//Point to opcode origins!
	if (CPU_faultraised(EXCEPTION_BOUNDSCHECK)==0)
	{
		return; //Abort handling when needed!
	}
	CPU_resetOP(); //Reset instruction to start of instruction!
	CPU[activeCPU].tempcycles = CPU[activeCPU].cycles_OP; //Save old cycles!
	CPU_executionphase_startinterrupt(EXCEPTION_BOUNDSCHECK,0,-1); //Execute INT1 normally using current CS:(E)IP!
}

void THROWDESCNM() //#NM exception handler!
{
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger","#NM fault(-1)!");
	}

	//Point to opcode origins!
	if (CPU_faultraised(EXCEPTION_COPROCESSORNOTAVAILABLE)==0) //Throw #NM exception!
	{
		return; //Abort handling when needed!
	}
	CPU_resetOP(); //Reset instruction to start of instruction!
	CPU[activeCPU].tempcycles = CPU[activeCPU].cycles_OP; //Save old cycles!
	CPU_executionphase_startinterrupt(EXCEPTION_COPROCESSORNOTAVAILABLE,2,-1); //Execute INT1 normally using current CS:(E)IP! No error code is pushed!
}

void CPU_COOP_notavailable() //COProcessor not available!
{
	THROWDESCNM(); //Same!
}

void THROWDESCMF() //#MF(Coprocessor Error) exception handler!
{
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger","#MF fault(-1)!");
	}

	//Point to opcode origins!
	if (CPU_faultraised(EXCEPTION_COPROCESSORERROR)==0) //Throw #NM exception!
	{
		return; //Abort handling when needed!
	}
	CPU_resetOP(); //Reset instruction to start of instruction!
	CPU[activeCPU].tempcycles = CPU[activeCPU].cycles_OP; //Save old cycles!
	CPU_executionphase_startinterrupt(EXCEPTION_COPROCESSORERROR,2,-1); //Execute INT1 normally using current CS:(E)IP! No error code is pushed!
}

void CPU_unkOP() //General unknown OPcode handler!
{
	if (EMULATED_CPU>=CPU_NECV30) //Invalid opcode exception? 8086 just ignores the instruction and continues running!
	{
		unkOP_186(); //Execute the unknown opcode exception handler for the 186+!
	}
}
