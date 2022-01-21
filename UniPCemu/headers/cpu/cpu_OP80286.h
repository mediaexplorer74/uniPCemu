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

#ifndef CPU_OP80286_H
#define CPU_OP80286_H

void unkOP0F_286(); //0F unknown opcode handler on 286+?

//The 80286 instructions themselves!
void CPU286_OP63(); //ARPL r/m16,r16
void CPU286_OPD6(); //286+ SALC
void CPU286_OP9D(); //286+ POPF

//0F opcodes!
void CPU286_OP0F00(); //Various extended 286+ instructions GRP opcode.
void CPU286_OP0F01(); //Various extended 286+ instruction GRP opcode.
void CPU286_OP0F02(); //LAR /r
void CPU286_OP0F03(); //LSL /r
void CPU286_OP0F05(); //Undocumented LOADALL instruction
void CPU286_OP0F06(); //CLTS
void CPU286_OP0F0B(); //#UD instruction
void CPU286_OP0FB9(); //#UD instruction

void CPU286_OPF1(); //Undefined opcode, Don't throw any exception!

//FPU opcodes!
void FPU80287_OPDB();
void FPU80287_OPDF();
void FPU80287_OPDD();
void FPU80287_OPD9();
void FPU80287_OP9B(); //FWAIT

void FPU80287_noCOOP(); //Rest opcodes for the Escaped instructions!

#endif