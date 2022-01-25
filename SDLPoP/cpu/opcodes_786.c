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
#include "headers/cpu/cpu_OP80786.h" //i786 support!
#include "headers/cpu/cpu_OP8086.h" //16-bit support!
#include "headers/cpu/cpu_OP80386.h" //32-bit support!
#include "headers/cpu/cpu_OP80586.h" //Basic MSR support!
#include "headers/cpu/cpu_pmtimings.h" //Timing support!
#include "headers/cpu/easyregs.h" //Easy register support!
#include "headers/cpu/protection.h" //Protection support!
#include "headers/cpu/biu.h" //condflushPIQ support!

//Modr/m support, used when reg=NULL and custommem==0

void CPU786_OP0F30() //WRMSR
{
	CPUMSR* MSR;
	uint_32 validbitslo;
	uint_32 validbitshi;
	if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
	{
		modrm_generateInstructionTEXT("WRMSR", 0, 0, PARAM_NONE);
	}
	//MSR #ECX = EDX::EAX
	if (getCPL() && (getcpumode() != CPU_MODE_REAL)) //Invalid privilege?
	{
		THROWDESCGP(0, 0, 0); //#GP(0)!
		return;
	}
	if ((REG_ECX<0x174) || (REG_ECX>0x176)) //Invalid register in ECX to handle by us?
	{
		CPU586_OP0F30(); //Let the parent handle it!
		return;
	}
	validbitshi = 0; //No valid bits!
	switch (REG_ECX)
	{
	case 0x174:
		validbitslo = 0xFFFF; //Only the lower 16-bits are valid!
		break;
	case 0x175:
	case 0x176:
		validbitslo = ~0; //All bits are valid!
		break;
	default:
		validbitslo = 0; //Not supported?
		break;
	}
	
	//Inverse the valid bits to get a mask of invalid bits!
	validbitslo = ~validbitslo; //invalid bits calculation!
	validbitshi = ~validbitshi; //invalid bits calculation!
	if ((REG_EDX&validbitshi) || (REG_EAX&validbitslo)) //Invalid register bits in EDX::EAX?
	{
		THROWDESCGP(0, 0, 0); //#GP(0)!
		return;
	}

	switch (REG_ECX) //What register is to be written?
	{
	case 0x174:
		MSR = &CPU[activeCPU].registers->IA32_SYSENTER_CS; //SYSENTER CS MSR!
		break;
	case 0x175:
		MSR = &CPU[activeCPU].registers->IA32_SYSENTER_ESP; //SYSENTER CS MSR!
		break;
	case 0x176:
		MSR = &CPU[activeCPU].registers->IA32_SYSENTER_EIP; //SYSENTER CS MSR!
		break;
	default: //Unknown?
		return; //Abort: invalid to handle!
		break;
	}

	MSR->hi = REG_EDX; //Set high!
	MSR->lo = REG_EAX; //Set low!
}

void CPU786_OP0F32() //RDMSR
{
	CPUMSR* MSR;
	if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
	{
		modrm_generateInstructionTEXT("RDMSR", 0, 0, PARAM_NONE);
	}

	if (getCPL() && (getcpumode() != CPU_MODE_REAL)) //Invalid privilege?
	{
		THROWDESCGP(0, 0, 0); //#GP(0)!
		return;
	}
	if ((REG_ECX < 0x174) || (REG_ECX > 0x176)) //Invalid register in ECX?
	{
		CPU586_OP0F32(); //Let the parent handle it!
		return;
	}

	switch (REG_ECX) //What register is to be written?
	{
	case 0x174:
		MSR = &CPU[activeCPU].registers->IA32_SYSENTER_CS; //SYSENTER CS MSR!
		break;
	case 0x175:
		MSR = &CPU[activeCPU].registers->IA32_SYSENTER_ESP; //SYSENTER CS MSR!
		break;
	case 0x176:
		MSR = &CPU[activeCPU].registers->IA32_SYSENTER_EIP; //SYSENTER CS MSR!
		break;
	default: //Unknown?
		return; //Abort: invalid to handle!
		break;
	}

	REG_EDX = MSR->hi; //High dword of MSR #ECX
	REG_EAX = MSR->lo; //Low dword of MSR #ECX
}

byte loadSYSENTERLEAVEdescriptor(int segment, word value, byte PL)
{
	SEGMENT_DESCRIPTOR* s;
	value &= 0xFFFC; //Valid bits!
	PL &= 3; //Valid bits!
	//Now, load the new descriptor and address for CS if needed(with secondary effects)!
	if ((CPU[activeCPU].have_oldSegReg & (1 << segment)) == 0) //Backup not loaded yet?
	{
		memcpy(&CPU[activeCPU].SEG_DESCRIPTORbackup[segment], &CPU[activeCPU].SEG_DESCRIPTOR[segment], sizeof(CPU[activeCPU].SEG_DESCRIPTORbackup[0])); //Restore the descriptor!
		CPU[activeCPU].oldSegReg[segment] = *CPU[activeCPU].SEGMENT_REGISTERS[segment]; //Backup the register too!
		CPU[activeCPU].have_oldSegReg |= (1 << segment); //Loaded!
	}
	s = &CPU[activeCPU].SEG_DESCRIPTOR[segment]; //The descriptor to load!
	//Load the entire descriptor!
	s->desc.limit_low = 0xFFFF; //We're 4GB!
	s->desc.base_low = 0; //flat!
	s->desc.base_mid = 0; //flat!
	if (segment == CPU_SEGMENT_CS) //Code?
	{
		s->desc.AccessRights = 0x9B; //Code, readable, non-conforming, accessed!
	}
	else //Data?
	{
		s->desc.AccessRights = 0x93; //Data, writable, expand-up, accessed!
	}
	s->desc.AccessRights |= (PL << 5); //Privilege level also!
	s->desc.noncallgate_info = 0xCF; //4GB flat! 32-bit(D/B) and Granularity set!
	s->desc.base_high = 0; //flat!
	*CPU[activeCPU].SEGMENT_REGISTERS[segment] = (value | PL); //Set the segment value!
	if (segment == CPU_SEGMENT_CS) //Code?
	{
		REG_EIP = CPU[activeCPU].destEIP; //Destination EIP!
		CPU_calcSegmentPrecalcs(1, &CPU[activeCPU].SEG_DESCRIPTOR[segment]); //Calculate any precalcs for the segment descriptor(do it here since we don't load descriptors externally)!
		if (CPU_condflushPIQ(-1)) return 1; //We're jumping to another address!
		//Don't check limits, as this can't go wrong!
	}
	else //Stack?
	{
		updateCPL(); //Update the CPL according to the mode!
		CPU_calcSegmentPrecalcs(0, &CPU[activeCPU].SEG_DESCRIPTOR[segment]); //Calculate any precalcs for the segment descriptor(do it here since we don't load descriptors externally)!
	}
	return 0; //OK!
}

void CPU786_OP0F34() //SYSENTER
{
	if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
	{
		modrm_generateInstructionTEXT("SYSENTER", 0, 0, PARAM_NONE);
	}
	if ((getcpumode() == CPU_MODE_REAL) || ((CPU[activeCPU].registers->IA32_SYSENTER_CS.lo & 0xFFFC) == 0)) //Invalid to use?
	{
		THROWDESCGP(0, 0, 0); //#GP(0)!
		return;
	}

	//Jump to protected mode in kernel mode!
	FLAGW_V8(0); //Clear V86 mode!
	FLAGW_IF(0); //Clear Interrupt flag!
	updateCPUmode(); //Update the CPU mode!
	REG_ESP = CPU[activeCPU].registers->IA32_SYSENTER_ESP.lo; //ESP!
	CPU[activeCPU].destEIP = CPU[activeCPU].registers->IA32_SYSENTER_EIP.lo; //EIP!
	if (loadSYSENTERLEAVEdescriptor(CPU_SEGMENT_SS, ((CPU[activeCPU].registers->IA32_SYSENTER_CS.lo & 0xFFFC) + 8), 0)) //SS failed?
	{
		return; //Abort!
	}
	if (loadSYSENTERLEAVEdescriptor(CPU_SEGMENT_CS, (CPU[activeCPU].registers->IA32_SYSENTER_CS.lo & 0xFFFC), 0)) //CS failed?
	{
		return; //Abort!
	}
	//Now properly switched to the kernel!
}

void CPU786_OP0F35() //SYSEXIT
{
	if (unlikely(CPU[activeCPU].cpudebugger)) //Debugger on?
	{
		modrm_generateInstructionTEXT("SYSEXIT", 0, 0, PARAM_NONE);
	}

	if ((getcpumode() == CPU_MODE_REAL) || ((CPU[activeCPU].registers->IA32_SYSENTER_CS.lo & 0xFFFC) == 0) || (getCPL())) //Invalid to use?
	{
		THROWDESCGP(0, 0, 0); //#GP(0)!
		return;
	}

	//Jump to protected mode in kernel mode!
	FLAGW_V8(0); //Clear V86 mode!
	FLAGW_IF(0); //Clear Interrupt flag!
	updateCPUmode(); //Update the CPU mode!
	REG_ESP = REG_ECX; //ESP from ECX!
	CPU[activeCPU].destEIP = REG_EDX; //EIP from EDX!
	//Perform SS first to set a proper privilege level(although it's documented first CS then SS)!
	if (loadSYSENTERLEAVEdescriptor(CPU_SEGMENT_SS, ((CPU[activeCPU].registers->IA32_SYSENTER_CS.lo & 0xFFFC) + 24), 3)) //SS failed?
	{
		return; //Abort!
	}
	if (loadSYSENTERLEAVEdescriptor(CPU_SEGMENT_CS, (CPU[activeCPU].registers->IA32_SYSENTER_CS.lo & 0xFFFC) + 16, 3)) //CS failed?
	{
		return; //Abort!
	}
	//Now properly switched to the user mode!
}
