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

#ifndef CPU_OPCODEINFORMATION_H
#define CPU_OPCODEINFORMATION_H

#include "headers/types.h"

typedef struct
{
	byte used; //Valid instruction? If zero, passthrough to earlier CPU timings.
	byte has_modrm; //Do we have ModR/M parameters?
	byte modrm_size; //First parameter of ModR/M setting
	byte modrm_specialflags; //Second parameter of ModR/M setting
	byte modrm_src0; //ModR/M first parameter! ModR/M sources: 1=R/M(E in documentation), 0=Reg(G/S(eg) in documentation)
	byte modrm_src1; //ModR/M second parameter! ModR/M sources: 1=R/M(E in documentation), 0=Reg(G/S(eg) in documentation)
	byte parameters; //The type of parameters to follow the ModR/M! 0=No parameters, 1=imm8, 2=imm16, 3=imm32, bit 2=Immediate is enabled on the REG of the RM byte(only when <2).
	word readwritebackinformation; //The type of writing back/reading data to memory if needed! Bits 0-1: 0=None, 1=Read, Write back operation, 2=Write operation only, 3=Read operation only, Bit 4: Operates on AL/AX/EAX when set. Bit 5: Push operation. Bit 6: Pop operation. Bit 7: Allows LOCK operation(with ModR/M when memory only). Bits 8-9: Allow LOCK operation only on: 0=Memory only(default behaviour), 1=(Reg!=7 with memory only), 2=(Reg=2 or 3 with memory only), 3=(Reg=0 or 1), Bit 10: Allow LOCK operation for reg=5-7 memory only.
} CPU_OpcodeInformation; //The CPU timing information!

void generate_opcodeInformation_tbl(); //Generate the timings table!
#endif
