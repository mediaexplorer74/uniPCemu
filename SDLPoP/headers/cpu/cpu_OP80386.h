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

#ifndef CPU_OP80386_H
#define CPU_OP80386_H

void unkOP0F_386(); //0F unknown opcode handler on 386+?

void CPU80386_execute_ADD_modrmmodrm32();
void CPU80386_OP05();
void CPU80386_execute_OR_modrmmodrm32();
void CPU80386_OP0D();
void CPU80386_execute_ADC_modrmmodrm32();
void CPU80386_OP15();
void CPU80386_execute_SBB_modrmmodrm32();
void CPU80386_OP1D();
void CPU80386_execute_AND_modrmmodrm32();
void CPU80386_OP25();
void CPU80386_execute_SUB_modrmmodrm32();
void CPU80386_OP2D();
void CPU80386_execute_XOR_modrmmodrm32();
void CPU80386_OP35();
void CPU80386_execute_CMP_modrmmodrm32();
void CPU80386_OP3D();
void CPU80386_OP40();
void CPU80386_OP41();
void CPU80386_OP42();
void CPU80386_OP43();
void CPU80386_OP44();
void CPU80386_OP45();
void CPU80386_OP46();
void CPU80386_OP47();
void CPU80386_OP48();
void CPU80386_OP49();
void CPU80386_OP4A();
void CPU80386_OP4B();
void CPU80386_OP4C();
void CPU80386_OP4D();
void CPU80386_OP4E();
void CPU80386_OP4F();
void CPU80386_OP50();
void CPU80386_OP51();
void CPU80386_OP52();
void CPU80386_OP53();
void CPU80386_OP54();
void CPU80386_OP55();
void CPU80386_OP56();
void CPU80386_OP57();
void CPU80386_OP58();
void CPU80386_OP59();
void CPU80386_OP5A();
void CPU80386_OP5B();
void CPU80386_OP5C();
void CPU80386_OP5D();
void CPU80386_OP5E();
void CPU80386_OP5F();
void CPU80386_OP85();
void CPU80386_OP87();
void CPU80386_execute_MOV_modrmmodrm32();
void CPU80386_OP8C();
void CPU80386_OP8D();
void CPU80386_OP90();
void CPU80386_OP91();
void CPU80386_OP92();
void CPU80386_OP93();
void CPU80386_OP94();
void CPU80386_OP95();
void CPU80386_OP96();
void CPU80386_OP97();
void CPU80386_OP98();
void CPU80386_OP99();
void CPU80386_OP9A();
void CPU80386_OP9C();
void CPU80386_OP9D_16();
void CPU80386_OP9D_32();

//Our two calling methods for handling the jumptable!
//16-bits versions having a new 32-bit address size override!
void CPU80386_OPA0_16();
void CPU80386_OPA1_16();
void CPU80386_OPA2_16();
void CPU80386_OPA3_16();
//32-bits versions having a new 32-bit address size override and operand size override, except 8-bit instructions!
void CPU80386_OPA1_32();
void CPU80386_OPA3_32();

//Normal instruction again!
void CPU80386_OPA5();
void CPU80386_OPA7();
void CPU80386_OPA9();
void CPU80386_OPAB();
void CPU80386_OPAD();
void CPU80386_OPAF();
void CPU80386_OPB8();
void CPU80386_OPB9();
void CPU80386_OPBA();
void CPU80386_OPBB();
void CPU80386_OPBC();
void CPU80386_OPBD();
void CPU80386_OPBE();
void CPU80386_OPBF();
void CPU80386_OPC2();
void CPU80386_OPC3();
void CPU80386_OPC4();
void CPU80386_OPC5();
void CPU80386_OPC7();
void CPU80386_OPCA();
void CPU80386_OPCB();
void CPU80386_OPCC();
void CPU80386_OPCD();
void CPU80386_OPCE();
void CPU80386_OPCF();
void CPU80386_OPD6();
void CPU80386_OPD7();
void CPU80386_OPE0();
void CPU80386_OPE1();
void CPU80386_OPE2();
void CPU80386_OPE3();
void CPU80386_OPE5();
void CPU80386_OPE7();
void CPU80386_OPE8();
void CPU80386_OPE9();
void CPU80386_OPEA();
void CPU80386_OPED();
void CPU80386_OPEF();
//void CPU80386_OPF1();

/*

NOW COME THE GRP1-5 OPCODES:

*/

void CPU80386_OP81(); //GRP1 Ev,Iv
void CPU80386_OP83(); //GRP1 Ev,Ib
void CPU80386_OP8F(); //Undocumented GRP opcode 8F r/m32
void CPU80386_OPD1(); //GRP2 Ev,1
void CPU80386_OPD3(); //GRP2 Ev,CL
void CPU80386_OPF7(); //GRP3b Ev
void CPU80386_OPFF(); //GRP5 Ev

void unkOP_80386(); //Unknown opcode on 8086?

/*

80186 32-bit extensions

*/

void CPU386_OP60();
void CPU386_OP61();
void CPU386_OP62();
void CPU386_OP68();
void CPU386_OP69();
void CPU386_OP6A();
void CPU386_OP6B();
void CPU386_OP6D();

void CPU386_OP6F();

void CPU386_OPC1(); //GRP2 Ev,Ib

void CPU386_OPC8_16(); //ENTER Iw,Ib
void CPU386_OPC8_32(); //ENTER Iw,Ib
void CPU386_OPC9_16(); //LEAVE
void CPU386_OPC9_32(); //LEAVE

/*

No 80286 normal extensions exist.

*/

/*

0F opcodes of the 80286, extended.

*/

void CPU386_OP0F00(); //Various extended 286+ instructions GRP opcode.
void CPU386_OP0F01(); //Various extended 286+ instruction GRP opcode.
void CPU386_OP0F02(); //LAR /r
void CPU386_OP0F03(); //LSL /r
void CPU386_OP0F07(); //Undocumented LOADALL instruction

//New: 16-bit and 32-bit variants of OP70-7F as a 0F opcode!
//16-bits variant
void CPU80386_OP0F80_16();
void CPU80386_OP0F81_16();
void CPU80386_OP0F82_16();
void CPU80386_OP0F83_16();
void CPU80386_OP0F84_16();
void CPU80386_OP0F85_16();
void CPU80386_OP0F86_16();
void CPU80386_OP0F87_16();
void CPU80386_OP0F88_16();
void CPU80386_OP0F89_16();
void CPU80386_OP0F8A_16();
void CPU80386_OP0F8B_16();
void CPU80386_OP0F8C_16();
void CPU80386_OP0F8D_16();
void CPU80386_OP0F8E_16();
void CPU80386_OP0F8F_16();
//32-bits variant
void CPU80386_OP0F80_32();
void CPU80386_OP0F81_32();
void CPU80386_OP0F82_32();
void CPU80386_OP0F83_32();
void CPU80386_OP0F84_32();
void CPU80386_OP0F85_32();
void CPU80386_OP0F86_32();
void CPU80386_OP0F87_32();
void CPU80386_OP0F88_32();
void CPU80386_OP0F89_32();
void CPU80386_OP0F8A_32();
void CPU80386_OP0F8B_32();
void CPU80386_OP0F8C_32();
void CPU80386_OP0F8D_32();
void CPU80386_OP0F8E_32();
void CPU80386_OP0F8F_32();

//All remaining 0F opcodes

//MOV [D/C]Rn instructions
void CPU80386_OP0F_MOVCRn_modrmmodrm(); //MOV /r CRn/r32,r32/CRn
void CPU80386_OP0F_MOVDRn_modrmmodrm(); //MOV /r DRn/r32,r32/DRn
void CPU80386_OP0F_MOVTRn_modrmmodrm(); //MOV /r TRn/r32,r32/TRn

//SETCC instructions
void CPU80386_OP0F90(); //SETO r/m8
void CPU80386_OP0F91(); //SETNO r/m8
void CPU80386_OP0F92(); //SETC r/m8
void CPU80386_OP0F93(); //SETAE r/m8
void CPU80386_OP0F94(); //SETE r/m8
void CPU80386_OP0F95(); //SETNE r/m8
void CPU80386_OP0F96(); //SETNA r/m8
void CPU80386_OP0F97(); //SETA r/m8
void CPU80386_OP0F98(); //SETS r/m8
void CPU80386_OP0F99(); //SETNS r/m8
void CPU80386_OP0F9A(); //SETP r/m8
void CPU80386_OP0F9B(); //SETNP r/m8
void CPU80386_OP0F9C(); //SETL r/m8
void CPU80386_OP0F9D(); //SETGE r/m8
void CPU80386_OP0F9E(); //SETLE r/m8
void CPU80386_OP0F9F(); //SETG r/m8

//Push/pop FS
void CPU80386_OP0FA0(); //PUSH FS
void CPU80386_OP0FA1(); //POP FS

void CPU80386_OP0FA3_16(); //BT /r r/m16,r16
void CPU80386_OP0FA3_32(); //BT /r r/m32,r32

void CPU80386_OP0FA4_16(); //SHLD /r r/m16,r16,imm8
void CPU80386_OP0FA4_32(); //SHLD /r r/m32,r32,imm8

void CPU80386_OP0FA5_16(); //SHLD /r r/m16,r16,CL
void CPU80386_OP0FA5_32(); //SHLD /r r/m32,r32,CL

void CPU80386_OP0FA8(); //PUSH GS
void CPU80386_OP0FA9(); //POP GS

//0F AA is RSM FLAGS on 386++

void CPU80386_OP0FAB_16(); //BTS /r r/m16,r16
void CPU80386_OP0FAB_32(); //BTS /r r/m32,r32
void CPU80386_OP0FAC_16(); //SHRD /r r/m16,r16,imm8
void CPU80386_OP0FAC_32(); //SHRD /r r/m32,r32,imm8
void CPU80386_OP0FAD_16(); //SHRD /r r/m16,r16,CL
void CPU80386_OP0FAD_32(); //SHRD /r r/m32,r32,CL
void CPU80386_OP0FAF_16(); //IMUL /r r16,r/m16
void CPU80386_OP0FAF_32(); //IMUL /r r32,r/m32

//LSS
void CPU80386_OP0FB2_16(); //LSS /r r16,m16:16
void CPU80386_OP0FB2_32(); //LSS /r r32,m16:32

void CPU80386_OP0FB3_16(); //BTR /r r/m16,r16
void CPU80386_OP0FB3_32(); //BTR /r r/m32,r32

void CPU80386_OP0FB4_16(); //LFS /r r16,m16:16
void CPU80386_OP0FB4_32(); //LFS /r r32,m16:32

void CPU80386_OP0FB5_16(); //LGS /r r16,m16:16
void CPU80386_OP0FB5_32(); //LGS /r r32,m16:32

void CPU80386_OP0FB6_16(); //MOVZX /r r16,r/m8
void CPU80386_OP0FB6_32(); //MOVZX /r r32,r/m8

void CPU80386_OP0FB7_16(); //MOVZX /r r16,r/m16
void CPU80386_OP0FB7_32(); //MOVZX /r r32,r/m16

void CPU80386_OP0FBA_16();
void CPU80386_OP0FBA_32();

void CPU80386_OP0FBB_16(); //BTC /r r/m16,r16
void CPU80386_OP0FBB_32(); //BTC /r r/m32,r32

void CPU80386_OP0FBC_16(); //BSF /r r16,r/m16
void CPU80386_OP0FBC_32(); //BSF /r r32,r/m32

void CPU80386_OP0FBD_16(); //BSR /r r16,r/m16
void CPU80386_OP0FBD_32(); //BSR /r r32,r/m32

void CPU80386_OP0FBE_16(); //MOVSX /r r16,r/m8
void CPU80386_OP0FBE_32(); //MOVSX /r r32,r/m8

void CPU80386_OP0FBF_16(); //MOVSX /r r16,r/m16
void CPU80386_OP0FBF_32(); //MOVSX /r r32,r/m16

byte CPU80386_internal_LXS(int segmentregister); //LDS, LES etc. 32-bit variant!

/*

32-bit versions of BIU operations!

*/

//Stack operation support through the BIU!
byte CPU80386_PUSHdw(word base, uint_32 *data);
byte CPU80386_internal_PUSHdw(word base, uint_32 *data);
byte CPU80386_internal_interruptPUSHdw(word base, uint_32 *data);
byte CPU80386_POPdw(word base, uint_32 *result);
byte CPU80386_internal_POPdw(word base, uint_32 *result);
byte CPU80386_POPESP(word base);

//Instruction variants of ModR/M!

byte CPU80386_instructionstepreadmodrmdw(word base, uint_32 *result, byte paramnr);
byte CPU80386_instructionstepwritemodrmdw(word base, uint_32 value, byte paramnr);
byte CPU80386_instructionstepwritedirectdw(word base, sword segment, word segval, uint_32 offset, uint_32 val, byte is_offset16);
byte CPU80386_instructionstepreaddirectdw(word base, sword segment, word segval, uint_32 offset, uint_32 *result, byte is_offset16);
//Now, the internal variants of the functions above!
byte CPU80386_internal_stepreadmodrmdw(word base, uint_32 *result, byte paramnr);
byte CPU80386_internal_stepwritedirectdw(word base, sword segment, word segval, uint_32 offset, uint_32 val, byte is_offset16);
byte CPU80386_internal_stepreaddirectdw(word base, sword segment, word segval, uint_32 offset, uint_32 *result, byte is_offset16);
byte CPU80386_internal_stepreadinterruptdw(word base, sword segment, word segval, uint_32 offset, uint_32 *result, byte is_offset16);
byte CPU80386_internal_stepwritemodrmdw(word base, uint_32 value, byte paramnr);

void op_add32(); //32-bit add for 8086 (I)MUL!
void INTdebugger80386(); //Special INTerrupt debugger!
#endif
