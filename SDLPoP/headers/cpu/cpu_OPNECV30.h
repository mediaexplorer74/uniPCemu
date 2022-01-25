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

#ifndef CPU_OPNECV30
#define CPU_OPNECV30
//Newer 8086+ opcodes!
void CPU186_OP37();
void CPU186_OP3F();

//New 80186+ opcodes!
void CPU186_OP60();//PUSHA
void CPU186_OP61(); //POPA
void CPU186_OP62(); //BOUND Gv,Ma
void CPU186_OP68(); //PUSH Iz
void CPU186_OP69(); //IMUL Gv,Ev,Iz
void CPU186_OP6A(); //PUSH Ib
void CPU186_OP6B(); //IMUL Gv,Ev,Ib
void CPU186_OP6C(); //INS Yb,DX
void CPU186_OP6D(); //INS Yz,DX
void CPU186_OP6E(); //OUTS DX,Xb
void CPU186_OP6F(); //OUTS DX,Xz
void CPU186_OP8E(); //MOV segreg,reg
void CPU186_OPC0(); //GRP2 Eb,Ib
void CPU186_OPC1(); //GRP2 Ev,Ib
void CPU186_OPC8(); //ENTER Iw,Ib
void CPU186_OPC9(); //LEAVE

void unkOP_186(); //Unknown opcode on 186+?

#endif