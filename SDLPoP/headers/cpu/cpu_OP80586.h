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

#ifndef CPU_OP80586_H
#define CPU_OP80586_H

//Not emulated yet. Bare minimum instruction to run!

void CPU586_CPUID();
void unkOP0F_586();

void CPU586_OP0F30(); //WRMSR
void CPU586_OP0F31(); //RSTDC
void CPU586_OP0F32(); //RDMSR
void CPU586_OP0FC7(); //CMPXCHG8B r/m32

//VME instructions
void CPU80586_OPCD();
void CPU80586_OPFA();
void CPU80586_OPFB();
void CPU80586_OP9C_16();
void CPU80586_OP9D_16();
void CPU80586_OP9D_32();
#endif