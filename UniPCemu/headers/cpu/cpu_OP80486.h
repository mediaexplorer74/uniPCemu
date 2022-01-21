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

#ifndef CPU_OP80486_H
#define CPU_OP80486_H

//Not emulated yet. Bare minimum instruction to run!

void CPU486_CPUID();
void unkOP0F_486();

void CPU486_OP0F01_32();
void CPU486_OP0F01_16();
void CPU486_OP0F08(); //INVD?
void CPU486_OP0F09(); //WBINVD?
void CPU486_OP0FB0(); //CMPXCHG r/m8,AL,r8
void CPU486_OP0FB1_16(); //CMPXCHG r/m16,AL,r16
void CPU486_OP0FB1_32(); //CMPXCHG r/m32,AL,r32
void CPU486_OP0FC0(); //XADD r/m8,r8
void CPU486_OP0FC1_16(); //XADD r/m16,r16
void CPU486_OP0FC1_32(); //XADD r/m32,r32
void CPU486_OP0FC8_16(); //BSWAP AX
void CPU486_OP0FC8_32(); //BSWAP EAX
void CPU486_OP0FC9_16(); //BSWAP CX
void CPU486_OP0FC9_32(); //BSWAP ECX
void CPU486_OP0FCA_16(); //BSWAP DX
void CPU486_OP0FCA_32(); //BSWAP EDX
void CPU486_OP0FCB_16(); //BSWAP BX
void CPU486_OP0FCB_32(); //BSWAP EBX
void CPU486_OP0FCC_16(); //BSWAP SP
void CPU486_OP0FCC_32(); //BSWAP ESP
void CPU486_OP0FCD_16(); //BSWAP BP
void CPU486_OP0FCD_32(); //BSWAP EBP
void CPU486_OP0FCE_16(); //BSWAP SI
void CPU486_OP0FCE_32(); //BSWAP ESI
void CPU486_OP0FCF_16(); //BSWAP DI
void CPU486_OP0FCF_32(); //BSWAP EDI

#endif