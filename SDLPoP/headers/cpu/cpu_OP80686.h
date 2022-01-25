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

#ifndef CPU_OP80686_H
#define CPU_OP80686_H

#include "headers/cpu/cpu.h" //CPU support!

void CPU686_OP0F0D_16(); //NOP r/m16
void CPU686_OP0F0D_32(); //NOP r/m32

void CPU686_OP0F18_16(); //HINT_NOP /4-7 r/m16
void CPU686_OP0F18_32(); //HINT_NOP /4-7 r/m32

void CPU686_OP0FNOP_16(); //HINT_NOP r/m16
void CPU686_OP0FNOP_32(); //HINT_NOP r/m32

void CPU686_OP0F1F_16(); //HINT_NOP /1-7 r/m16
void CPU686_OP0F1F_32(); //HINT_NOP /1-7 r/m32

void CPU686_OP0F33(); //RDPMC

//CMOVcc instructions

//16-bit

void CPU686_OP0F40_16(); //CMOVO r/m16
void CPU686_OP0F41_16();//CMOVNO r/m16
void CPU686_OP0F42_16(); //CMOVC r/m16
void CPU686_OP0F43_16(); //CMOVAE r/m16
void CPU686_OP0F44_16(); //CMOVE r/m16
void CPU686_OP0F45_16(); //CMOVNE r/m16
void CPU686_OP0F46_16(); //CMOVNA r/m16
void CPU686_OP0F47_16(); //CMOVA r/m16
void CPU686_OP0F48_16(); //CMOVS r/m16
void CPU686_OP0F49_16(); //CMOVNS r/m16
void CPU686_OP0F4A_16(); //CMOVP r/m16
void CPU686_OP0F4B_16(); //CMOVNP r/m16
void CPU686_OP0F4C_16(); //CMOVL r/m16
void CPU686_OP0F4D_16(); //CMOVGE r/m16
void CPU686_OP0F4E_16(); //CMOVLE r/m16
void CPU686_OP0F4F_16(); //CMOVG r/m16

//32-bit

void CPU686_OP0F40_32(); //CMOVO r/m32
void CPU686_OP0F41_32(); //CMOVNO r/m32
void CPU686_OP0F42_32(); //CMOVC r/m32
void CPU686_OP0F43_32(); //CMOVAE r/m32
void CPU686_OP0F44_32(); //CMOVE r/m32
void CPU686_OP0F45_32(); //CMOVNE r/m32
void CPU686_OP0F46_32(); //CMOVNA r/m32
void CPU686_OP0F47_32(); //CMOVA r/m32
void CPU686_OP0F48_32(); //CMOVS r/m32
void CPU686_OP0F49_32(); //CMOVNS r/m32
void CPU686_OP0F4A_32(); //CMOVP r/m32
void CPU686_OP0F4B_32(); //CMOVNP r/m32
void CPU686_OP0F4C_32(); //CMOVL r/m32
void CPU686_OP0F4D_32(); //CMOVGE r/m32
void CPU686_OP0F4E_32(); //CMOVLE r/m32
void CPU686_OP0F4F_32(); //CMOVG r/m32

#endif
