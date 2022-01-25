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

#ifndef CPU_OP8086_H
#define CPU_OP8086_H

//8086 processor used opcodes:

//Conditional JMPs opcodes:

//00+
void CPU8086_execute_ADD_modrmmodrm8();
void CPU8086_execute_ADD_modrmmodrm16();
void CPU8086_OP04();
void CPU8086_OP05();
void CPU8086_OP06();
void CPU8086_OP07();
void CPU8086_execute_OR_modrmmodrm8();
void CPU8086_execute_OR_modrmmodrm16();
void CPU8086_OP0C();
void CPU8086_OP0D();
void CPU8086_OP0E();
void CPU8086_OP0F();
//No 0F!
//10+
void CPU8086_execute_ADC_modrmmodrm8();
void CPU8086_execute_ADC_modrmmodrm16();
void CPU8086_OP14();
void CPU8086_OP15();
void CPU8086_OP16();
void CPU8086_OP17();
void CPU8086_execute_SBB_modrmmodrm8();
void CPU8086_execute_SBB_modrmmodrm16();
void CPU8086_OP1C();
void CPU8086_OP1D();
void CPU8086_OP1E();
void CPU8086_OP1F();
//20+
void CPU8086_execute_AND_modrmmodrm8();
void CPU8086_execute_AND_modrmmodrm16();
void CPU8086_OP24();
void CPU8086_OP25();
void CPU8086_OP27();
void CPU8086_execute_SUB_modrmmodrm8();
void CPU8086_execute_SUB_modrmmodrm16();
void CPU8086_OP2C();
void CPU8086_OP2D();
void CPU8086_OP2F();
//30+
void CPU8086_execute_XOR_modrmmodrm8();
void CPU8086_execute_XOR_modrmmodrm16();
void CPU8086_OP34();
void CPU8086_OP35();
void CPU8086_OP37();
void CPU8086_execute_CMP_modrmmodrm8();
void CPU8086_execute_CMP_modrmmodrm16();
void CPU8086_OP3C();
void CPU8086_OP3D();
void CPU8086_OP3F();
//40+
void CPU8086_OP40();
void CPU8086_OP41();
void CPU8086_OP42();
void CPU8086_OP43();
void CPU8086_OP44();
void CPU8086_OP45();
void CPU8086_OP46();
void CPU8086_OP47();
void CPU8086_OP48();
void CPU8086_OP49();
void CPU8086_OP4A();
void CPU8086_OP4B();
void CPU8086_OP4C();
void CPU8086_OP4D();
void CPU8086_OP4E();
void CPU8086_OP4F();
//50+
void CPU8086_OP50();
void CPU8086_OP51();
void CPU8086_OP52();
void CPU8086_OP53();
void CPU8086_OP54();
void CPU8086_OP55();
void CPU8086_OP56();
void CPU8086_OP57();
void CPU8086_OP58();
void CPU8086_OP59();
void CPU8086_OP5A();
void CPU8086_OP5B();
void CPU8086_OP5C();
void CPU8086_OP5D();
void CPU8086_OP5E();
void CPU8086_OP5F();
//No 60+
//void CPU8086_OP60(); //PUSHA
//void CPU8086_OP61(); //POPA
//70+ : Comparisions etc.
void CPU8086_OP70(); //JO rel8  : (FLAG_OF=1)
void CPU8086_OP71(); //JNO rel8 : (FLAG_OF=0)
void CPU8086_OP72(); //JB rel8  : (FLAG_CF=1)
void CPU8086_OP73(); //JNB rel8 : (FLAG_CF=0)
void CPU8086_OP74(); //JZ rel8  : (FLAG_ZF=1)
void CPU8086_OP75(); //JNZ rel8 : (FLAG_ZF=0)
void CPU8086_OP76(); //JBE rel8 : (FLAG_CF=1|FLAG_ZF=1)
void CPU8086_OP77(); //JA rel8  : (FLAG_CF=0&FLAG_ZF=0)
void CPU8086_OP78(); //JS rel8  : (FLAG_SF=1)
void CPU8086_OP79(); //JNS rel8 : (FLAG_SF=0)
void CPU8086_OP7A(); //JPE rel8 : (FLAG_PF=1)
void CPU8086_OP7B(); //JPO rel8 : (FLAG_PF=0)
void CPU8086_OP7C(); //JL rel8  : (FLAG_SF!=FLAG_OF)
void CPU8086_OP7D(); //JGE rel8 : (FLAG_SF=FLAG_OF)
void CPU8086_OP7E(); //JLE rel8 : (FLAG_ZF|(FLAG_SF!=FLAG_OF))
void CPU8086_OP7F(); //JG rel8  : ((FLAG_ZF=0)|FLAG_SF=FLAG_OF)
//80+
void CPU8086_OP80();
void CPU8086_OP81();
void CPU8086_OP82();
void CPU8086_OP83();
void CPU8086_OP84();
void CPU8086_OP85();
void CPU8086_OP86();
void CPU8086_OP87();
void CPU8086_execute_MOV_modrmmodrm8();
void CPU8086_execute_MOV_modrmmodrm16();
void CPU8086_execute_MOVSegRegMemory();
void CPU8086_OP8D();
void CPU8086_OP8F();
//90+
void CPU8086_OP90();
void CPU8086_OP91();
void CPU8086_OP92();
void CPU8086_OP93();
void CPU8086_OP94();
void CPU8086_OP95();
void CPU8086_OP96();
void CPU8086_OP97();
void CPU8086_OP98();
void CPU8086_OP99();
void CPU8086_OP9A();
void CPU8086_OP9B();
void CPU8086_OP9C();
void CPU8086_OP9D();
void CPU8086_OP9E();
void CPU8086_OP9F();
//A0+
void CPU8086_OPA0();
void CPU8086_OPA1();
void CPU8086_OPA2();
void CPU8086_OPA3();
void CPU8086_OPA4();
void CPU8086_OPA5();
void CPU8086_OPA6();
void CPU8086_OPA7();
void CPU8086_OPA8();
void CPU8086_OPA9();
void CPU8086_OPAA();
void CPU8086_OPAB();
void CPU8086_OPAC();
void CPU8086_OPAD();
void CPU8086_OPAE();
void CPU8086_OPAF();
//B0+
void CPU8086_OPB0();
void CPU8086_OPB1();
void CPU8086_OPB2();
void CPU8086_OPB3();
void CPU8086_OPB4();
void CPU8086_OPB5();
void CPU8086_OPB6();
void CPU8086_OPB7();
void CPU8086_OPB8();
void CPU8086_OPB9();
void CPU8086_OPBA();
void CPU8086_OPBB();
void CPU8086_OPBC();
void CPU8086_OPBD();
void CPU8086_OPBE();
void CPU8086_OPBF();
//C0+
void CPU8086_OPC2();
void CPU8086_OPC3();
void CPU8086_OPC4();
void CPU8086_OPC5();
void CPU8086_OPC6();
void CPU8086_OPC7();
void CPU8086_OPCA();
void CPU8086_OPCB();
void CPU8086_OPCC();
void CPU8086_OPCD();
void CPU8086_OPCE();
void CPU8086_OPCF();
//D0+
void CPU8086_OPD0();
void CPU8086_OPD1();
void CPU8086_OPD2();
void CPU8086_OPD3();
void CPU8086_OPD4();
void CPU8086_OPD5();
void CPU8086_OPD6();
void CPU8086_OPD7();
//E0+
void CPU8086_OPE0();
void CPU8086_OPE1();
void CPU8086_OPE2();
void CPU8086_OPE3();
void CPU8086_OPE4();
void CPU8086_OPE5();
void CPU8086_OPE6();
void CPU8086_OPE7();
void CPU8086_OPE8();
void CPU8086_OPE9();
void CPU8086_OPEA();
void CPU8086_OPEB();
void CPU8086_OPEC();
void CPU8086_OPED();
void CPU8086_OPEE();
void CPU8086_OPEF();
//F0+
void CPU8086_OPF1();
void CPU8086_OPF4();
void CPU8086_OPF5();
void CPU8086_OPF6();
void CPU8086_OPF7();
void CPU8086_OPF8();
void CPU8086_OPF9();
void CPU8086_OPFA();
void CPU8086_OPFB();
void CPU8086_OPFC();
void CPU8086_OPFD();
void CPU8086_OPFE();
void CPU8086_OPFF();

void CPU8086_noCOOP(); //Coprosessor opcodes handler!

//Extra support:
//word getLEA(MODRM_PARAMS *params);

byte CPU086_int(byte interrupt); //Executes an hardware interrupt (from tbl). Returns 1 when finished, 0 when still busy.

//For GRP Opcodes!
//byte CPU8086_internal_INC16(word *reg);
//byte CPU8086_internal_DEC16(word *reg);

void unkOP_8086(); //Unknown opcode on 8086?

void external8086RETF(word popbytes); //Support for special interrupt handlers!

void CPU8086_external_XLAT(); //XLAT for extensions!

byte checkStackAccess(uint_32 poptimes, word isPUSH, byte isdword); //How much do we need to POP from the stack?

byte CPU8086_internal_LXS(int segmentregister); //LDS, LES etc. 16-bit variant!

//I/O and memory support!
byte CPU8086_PUSHw(word base, word *data, byte is32instruction);
byte CPU8086_internal_PUSHw(word base, word *data, byte is32instruction);
byte CPU8086_PUSHb(word base, byte *data, byte is32instruction);
byte CPU8086_internal_interruptPUSHw(word base, word *data, byte is32instruction);
byte CPU8086_internal_PUSHb(word base, byte *data, byte is32instruction);
byte CPU8086_POPw(word base, word *result, byte is32instruction);
byte CPU8086_internal_POPw(word base, word *result, byte is32instruction);
byte CPU8086_POPSP(word base);
byte CPU8086_POPb(word base, byte *result, byte is32instruction);
byte CPU8086_instructionstepreadmodrmb(word base, byte *result, byte paramnr); //Base=Start instruction step, result=Pointer to the result container!
byte CPU8086_instructionstepreadmodrmw(word base, word *result, byte paramnr);
byte CPU8086_instructionstepwritemodrmb(word base, byte value, byte paramnr); //Base=Start instruction step, result=Pointer to the result container!
byte CPU8086_instructionstepwritemodrmw(word base, word value, byte paramnr, byte isJMPorCALL);
byte CPU8086_instructionstepwritedirectb(word base, sword segment, word segval, uint_32 offset, byte val, byte is_offset16);
byte CPU8086_instructionstepwritedirectw(word base, sword segment, word segval, uint_32 offset, word val, byte is_offset16);
byte CPU8086_instructionstepreaddirectb(word base, sword segment, word segval, uint_32 offset, byte *result, byte is_offset16);
byte CPU8086_instructionstepreaddirectw(word base, sword segment, word segval, uint_32 offset, word *result, byte is_offset16);
byte CPU8086_internal_stepreadmodrmb(word base, byte *result, byte paramnr); //Base=Start instruction step, result=Pointer to the result container!
byte CPU8086_internal_stepreadmodrmw(word base, word *result, byte paramnr);
byte CPU8086_internal_stepwritemodrmb(word base, byte value, byte paramnr); //Base=Start instruction step, result=Pointer to the result container!
byte CPU8086_internal_stepwritedirectb(word base, sword segment, word segval, uint_32 offset, byte val, byte is_offset16);
byte CPU8086_internal_stepwritedirectw(word base, sword segment, word segval, uint_32 offset, word val, byte is_offset16);
byte CPU8086_internal_stepreaddirectb(word base, sword segment, word segval, uint_32 offset, byte *result, byte is_offset16);
byte CPU8086_internal_stepreaddirectw(word base, sword segment, word segval, uint_32 offset, word *result, byte is_offset16);
byte CPU8086_internal_stepreadinterruptw(word base, sword segment, word segval, uint_32 offset, word *result, byte is_offset16);
byte CPU8086_internal_stepwritemodrmw(word base, word value, byte paramnr, byte isJMPorCALL);

byte CPU8086_instructionstepdelayBIU(word base, byte cycles);
byte CPU8086_internal_delayBIU(word base, byte cycles);
byte CPU8086_instructionstepdelayBIUidle(word base, byte cycles);
byte CPU8086_internal_delayBIUidle(word base, byte cycles);

//Wait for the BIU to become ready!
byte CPU8086_instructionstepwaitBIUready(word base);
byte CPU8086_internal_waitBIUready(word base);
//Wait for the BIU to become finished for the current instruction timing!
byte CPU8086_instructionstepwaitBIUfinished(word base);
byte CPU8086_internal_waitBIUfinished(word base);


/*

Parameters:
	val: The value to divide
	divisor: The value to divide by
	quotient: Quotient result container
	remainder: Remainder result container
	error: 1 on error(DIV0), 0 when valid.
	resultbits: The amount of bits the result contains(16 or 8 on 8086) of quotient and remainder.
	SHLcycle: The amount of cycles for each SHL.
	ADDSUBcycle: The amount of cycles for ADD&SUB instruction to execute.
	applycycles: Apply the default cycles externally?
	issigned: Signed division?
	quotientnegative: Quotient is signed negative result?
	remaindernegative: Remainder is signed negative result?

*/
void CPU8086_internal_DIV(uint_32 val, word divisor, word *quotient, word *remainder, byte *error, byte resultbits, byte *applycycles, byte issigned, byte quotientnegative, byte remaindernegative, byte isAdjust, byte isRegister);
void CPU8086_internal_IDIV(uint_32 val, word divisor, word *quotient, word *remainder, byte *error, byte resultbits, byte *applycycles, byte isAdjust, byte isRegister);

#endif