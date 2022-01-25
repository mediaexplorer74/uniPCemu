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

#ifndef CPU_PMTIMINGS_H
#define CPU_PMTIMINGS_H

#include "headers/types.h" //Our types!

//Essentially, each instruction is expressed as a 0F,OPcode,modr/m set. This specifies the entries that apply(base on 0F opcode used, opcode executed and modr/m set when modrm_reg!=0.
//Next, the bits 1-5 specify different kinds of filters that specifies the variant to use, if specified. Variants have priority over the non-variants(bits 1-5==0).

typedef struct
{
	word basetiming;
	word n; //With RO*/SH*/SAR is the amount of bytes actually shifted; With String instructions, added to base count with multiplier(number of repeats after first instruction)
	byte addclock; //bit 0=Add one clock if we're using 3 memory operands! bit 1=n is count to add for string instructions (every repeat).  This variant is only used with string instructions., bit 2=We depend on the gate used. The gate type we're for is specified in the low 4 bits of n. The upper 2(bits 4-5) bits of n specify: 1=Same privilege level Call gate, 2=Different privilege level Call gate, no parameters, 3=Different privilege level, X parameters, 0=Ignore privilege level/parameters in the cycle calculation, bit 3=This rule only fires when the jump is taken. bit 4=This rule fires only when the L value of the ENTER instruction matches and fits in the lowest bit of n. 5=This rule fires only when the L value of the ENTER instruction doesn't fit in 1 bit. L is multiplied with the n value and added to the base count cycles.
	//Setting addclock bit 2, n lower bits to call gate and n higher bits to 2 adds 4 cycles for each parameter on a 80286.
	//With addclock bit 4, n is the L value to be specified. With addclock bit 5, (L - 1) is multiplied with the n value and added to the base count cycles.
	//With addclock bit 6, n is multiplied with the 80386+ BST_cnt variable and added to the base timing.
	//With addclock bit 7, n's highest bit must match the protection_PortRightsLookedup variable to match timings.
} MemoryTimingInfo; //First entry is register value(modr/m register-register), Second entry is memory value(modr/m register-memory)


typedef struct
{
	byte CPU; //For what CPU(286 relative)? 0=286, 1=386, 2=486, 3=586(Pentium) etc
	byte is32; //32-bit variant of the opcode?
	byte is0F; //Are we an extended instruction(0F instruction)?
	byte OPcode; //The opcode to be applied to!
	byte OPcodemask; //The mask to be applied to the original opcode to match this opcode in order to be applied!
	byte modrm_reg; //>0: Substract 1 for the modr/m reg requirement. Else no modr/m is looked at!
	struct
	{
		MemoryTimingInfo ismemory[2]; //First entry is register value(modr/m register-register), Second entry is memory value(modr/m register-memory)
	} CPUmode[2]; //0=Real mode, 1=Protected mode
} CPUPM_Timings;

//Lower 4 bits of the n information
#define GATECOMPARISON_CALLGATE 1
#define GATECOMPARISON_TSS 2
#define GATECOMPARISON_TASKGATE 3
#define GATECOMPARISON_INTERRUPTGATE 4
//Special RET case for returning to different privilege levels!
#define GATECOMPARISON_RET 4

//High 2 bits of the n information
#define CALLGATETIMING_SAMEPRIVILEGELEVEL 1
#define CALLGATETIMING_DIFFERENTPRIVILEGELEVEL_NOPARAMETERS 2
#define CALLGATETIMING_DIFFERENTPRIVILEGELEVEL_XPARAMETERS 3
#define GATETIMING_ANYPRIVILEGELEVEL 0

#define INTERRUPTGATE_SAMEPRIVILEGELEVEL 0
#define INTERRUPTGATE_DIFFERENTPRIVILEGELEVEL 1
#define INTERRUPTGATE_TASKGATE 2

//Simplified stuff for 286 gate descriptors(combination of the above flags used, which are used in the lookup table multiple times)!
#define INTERRUPTGATETIMING_SAMELEVEL ((GATECOMPARISON_INTERRUPTGATE)|(INTERRUPTGATE_SAMEPRIVILEGELEVEL<<4))
#define INTERRUPTGATETIMING_DIFFERENTLEVEL ((GATECOMPARISON_INTERRUPTGATE)|(INTERRUPTGATE_DIFFERENTPRIVILEGELEVEL<<4))
#define INTERRUPTGATETIMING_TASKGATE ((GATECOMPARISON_INTERRUPTGATE)|(INTERRUPTGATE_TASKGATE<<4))

//Simplified stuff for 286 gate descriptors(combination of the above flags used, which are used in the lookup table multiple times)!
#define CALLGATE_SAMELEVEL ((GATECOMPARISON_CALLGATE)|(CALLGATETIMING_SAMEPRIVILEGELEVEL<<4))
#define CALLGATE_DIFFERENTLEVEL_NOPARAMETERS ((GATECOMPARISON_CALLGATE)|(CALLGATETIMING_DIFFERENTPRIVILEGELEVEL_NOPARAMETERS<<4))
#define CALLGATE_DIFFERENTLEVEL_XPARAMETERS ((GATECOMPARISON_CALLGATE)|(CALLGATETIMING_DIFFERENTPRIVILEGELEVEL_XPARAMETERS<<4))
#define OTHERGATE_NORMALTSS ((GATECOMPARISON_TSS)|(GATETIMING_ANYPRIVILEGELEVEL<<4))
#define OTHERGATE_NORMALTASKGATE ((GATECOMPARISON_TASKGATE)|(GATETIMING_ANYPRIVILEGELEVEL<<4))
#define RET_DIFFERENTLEVEL ((GATECOMPARISON_RET)|(CALLGATETIMING_DIFFERENTPRIVILEGELEVEL_NOPARAMETERS<<4))

//The size of our PM timings table!
#define CPUPMTIMINGS_SIZE 731

byte CPU_apply286cycles(); //Apply the 80286+ cycles method. Result: 0 when to apply normal cycles. 1 when 80286+ cycles are applied!

#endif