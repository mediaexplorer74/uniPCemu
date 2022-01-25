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

#ifndef MODRM_H
#define MODRM_H

#include "headers/types.h" //Needs type support!

//REG (depends on size specified by stack B-bit&opcode 66h&done with 'register' MOD (see below)):

//8-bit registers:
#define MODRM_REG_AL 0
#define MODRM_REG_CL 1
#define MODRM_REG_DL 2
#define MODRM_REG_BL 3
#define MODRM_REG_AH 4
#define MODRM_REG_CH 5
#define MODRM_REG_DH 6
#define MODRM_REG_BH 7

//16-bit registers (both /r and mod3 operands):
#define MODRM_REG_AX 0
#define MODRM_REG_CX 1
#define MODRM_REG_DX 2
#define MODRM_REG_BX 3
#define MODRM_REG_SP 4
#define MODRM_REG_BP 5
#define MODRM_REG_SI 6
#define MODRM_REG_DI 7

//16-bit segment registers (SReg operands)
#define MODRM_SEG_ES 0
#define MODRM_SEG_CS 1
#define MODRM_SEG_SS 2
#define MODRM_SEG_DS 3
#define MODRM_SEG_FS 4
#define MODRM_SEG_GS 5

//32-bit registers (32-bit variant of 16-bit registers):
#define MODRM_REG_EAX 0
#define MODRM_REG_ECX 1
#define MODRM_REG_EDX 2
#define MODRM_REG_EBX 3

//SP in mod3 operand
#define MODRM_REG_ESP 4
//SIB in all others.
#define MODRM_REG_SIB 4

#define MODRM_REG_EBP 5
#define MODRM_REG_ESI 6
#define MODRM_REG_EDI 7

//Control registers(always 32-bit)
#define MODRM_REG_CR0 0
#define MODRM_REG_CR1 1
#define MODRM_REG_CR2 2
#define MODRM_REG_CR3 3
#define MODRM_REG_CR4 4
#define MODRM_REG_CR5 5
#define MODRM_REG_CR6 6
#define MODRM_REG_CR7 7

//Debugger registers(always 32-bit)
#define MODRM_REG_DR0 0
#define MODRM_REG_DR1 1
#define MODRM_REG_DR2 2
#define MODRM_REG_DR3 3
#define MODRM_REG_DR4 4
#define MODRM_REG_DR5 5
#define MODRM_REG_DR6 6
#define MODRM_REG_DR7 7

//Test registers(always 32-bit)
#define MODRM_REG_TR0 0
#define MODRM_REG_TR1 1
#define MODRM_REG_TR2 2
#define MODRM_REG_TR3 3
#define MODRM_REG_TR4 4
#define MODRM_REG_TR5 5
#define MODRM_REG_TR6 6
#define MODRM_REG_TR7 7

//Finally: the data for r/m operands:

#define MODRM_MEM_BXSI 0
#define MODRM_MEM_BXDI 1
#define MODRM_MEM_BPSI 2
#define MODRM_MEM_BPDI 3
#define MODRM_MEM_SI 4
#define MODRM_MEM_DI 5

//Only in MOD0 instances, 16-bit address, instead of [BP]:
#define MODRM_MEM_DISP16 6
//All other instances:
#define MODRM_MEM_BP 6

#define MODRM_MEM_BX 7

//Same as above, but for 32-bits memory addresses (mod 0-2)!

#define MODRM_MEM_EAX 0
#define MODRM_MEM_ECX 1
#define MODRM_MEM_EDX 2
#define MODRM_MEM_EBX 3
#define MODRM_MEM_SIB 4
//Mod>0 below:
#define MODRM_MEM_EBP 5
#define MODRM_MEM_ESI 6
#define MODRM_MEM_EDI 7
//Only in MOD0 instances, 32-bit address, instead of [EBP]:
#define MODRM_MEM_DISP32 5

//SIB Base EBP becomes 32-bit displacement.
#define MODRM_SIB_DISP32 MODRM_REG_EBP

//MOD:

//e.g. [XXX] (location in memory)
#define MOD_MEM 0

//e.g. [XXX+disp8] (location in memory)
#define MOD_MEM_DISP8 1

//Below depends on MOD.
//e.g. [XXX+disp16] (location in memory) as /r param.
#define MOD_MEM_DISP16 2
//e.g. [XXX+disp32] (location in memory)
#define MOD_MEM_DISP32 2

//register (Xl/Xx/eXx; e.g. al/ax/eax) source/dest. (see above for further specification!)
#define MOD_REG 3

#define SIB_BASE(SIB) (SIB&7)
#define SIB_INDEX(SIB) ((SIB>>3)&7)
#define SIB_SCALE(SIB) ((SIB>>6)&3)
typedef byte SIBType; //SIB byte!

//Struct containing the MODRM info:

typedef struct
{
	byte isreg; //1 for register, 2 for memory, other is unknown!
	byte regsize; //1 for byte, 2 for word, 3 for dword
	struct
	{
		uint_32 *reg32;
		word *reg16;
		byte *reg8;
	}; //Register direct access!

//When not register:

	char text[30]; //String representation of reg or memory address!
	word mem_segment; //Segment of memory address!
	word *segmentregister; //The segment register (LEA related functions)!
	int segmentregister_index; //Segment register index!
	uint_32 mem_offset; //Offset of memory address!
	uint_32 memorymask; //Memory mask to use!
	byte is16bit; //16-bit address based?
	byte is_segmentregister; //Are we a segment register?
} MODRM_PTR; //ModRM decoded pointer!

typedef struct
{
	byte MODRM_instructionfetch; //What state are we in to fetch? 0=ModR/M byte, 1=SIB byte, 2=Immediate data
} MODRM_instructionfetch;

typedef struct
{
	byte notdecoded; //Are we not loaded with ModR/M information and it hasn't been decoded properly?
	byte modrm; //MODR/M!
	SIBType SIB; //SIB Byte if applied.
	dwordsplitterb displacement; //byte/word/dword!
	byte size; //The used operand size of the memory operand for logging!
	byte sizeparam; //Operand size of the opcode!
	byte specialflags; //Is this a /r MODR/M (=RM is reg2)?
	byte reg_is_segmentregister; //REG is segment register?
	MODRM_PTR info[3]; //All versions of info!
	byte EA_cycles; //The effective address cycles we're using!
	byte havethreevariables; //3-variable memory addition?
	byte error; //An error was detected decoding the modr/m?
	MODRM_instructionfetch instructionfetch;
} MODRM_PARAMS;

#define MODRM_MOD(modrm) ((modrm & 0xC0) >> 6)
#define MODRM_REG(modrm) ((modrm & 0x38) >> 3)
#define MODRM_RM(modrm) (modrm & 0x07)
#define modrm_isregister(params) (MODRM_MOD(params.modrm) == MOD_REG)
#define modrm_ismemory(params) (MODRM_MOD(params.modrm)!=MOD_REG)
#define MODRM_EA(params) params.EA_cycles
#define MODRM_threevariables(params) (params.havethreevariables)
//Error in the ModR/M?
#define MODRM_ERROR(params) params.error

/*
Warning: SIB=Scaled index byte modes
*/

//whichregister: 1=R/M, other=register!

//Direct addressing of MODR/M bytes:

byte *modrm_addr8(MODRM_PARAMS *params, int whichregister, int forreading);
word *modrm_addr16(MODRM_PARAMS *params, int whichregister, int forreading);
uint_32 *modrm_addr32(MODRM_PARAMS *params, int whichregister, int forreading);

//Read/write things on MODR/M:

byte modrm_read8(MODRM_PARAMS *params, int whichregister);
byte modrm_read8_BIU(MODRM_PARAMS *params, int whichregister, byte *result); //Returns: 0: Busy, 1=Finished request(memory to be read back from BIU), 2=Register written, no BIU to read a response from.
word modrm_read16(MODRM_PARAMS *params, int whichregister);
byte modrm_read16_BIU(MODRM_PARAMS *params, int whichregister, word *result); //Returns: 0: Busy, 1=Finished request(memory to be read back from BIU), 2=Register written, no BIU to read a response from.
uint_32 modrm_read32(MODRM_PARAMS *params, int whichregister);
byte modrm_read32_BIU(MODRM_PARAMS *params, int whichregister, uint_32 *result); //Returns: 0: Busy, 1=Finished request(memory to be read back from BIU), 2=Register written, no BIU to read a response from.

void modrm_write8(MODRM_PARAMS *params, int whichregister, byte value);
byte modrm_write8_BIU(MODRM_PARAMS *params, int whichregister, byte value);
void modrm_write16(MODRM_PARAMS *params, int whichregister, word value, byte isJMPorCALL);
byte modrm_write16_BIU(MODRM_PARAMS *params, int whichregister, word value, byte isJMPorCALL);
void modrm_write32(MODRM_PARAMS *params, int whichregister, uint_32 value);
byte modrm_write32_BIU(MODRM_PARAMS *params, int whichregister, uint_32 value);

//Just the adressing:
word modrm_lea16(MODRM_PARAMS *params, int whichregister); //For LEA instructions!
void modrm_lea16_text(MODRM_PARAMS *params, int whichregister, char *result); //For LEA instructions!
uint_32 modrm_lea32(MODRM_PARAMS *params, int whichregister); //For LEA instructions!
void modrm_lea32_text(MODRM_PARAMS *params, int whichregister, char *result); //For LEA instructions!
word modrm_offset16(MODRM_PARAMS *params, int whichregister); //Gives address for JMP, CALL etc.!
uint_32 modrm_offset32(MODRM_PARAMS *params, int whichregister); //Gives address for JMP, CALL etc.!
word *modrm_addr_reg16(MODRM_PARAMS *params, int whichregister); //For LEA related instructions, returning the register!

void modrm_text8(MODRM_PARAMS *params, int whichregister, char *result); //8-bit text representation!
void modrm_text16(MODRM_PARAMS *params, int whichregister, char *result); //16-bit text representation!
void modrm_text32(MODRM_PARAMS *params, int whichregister, char *result); //32-bit text represnetation!

void halt_modrm(char *message, ...); //Modr/m error?

void reset_modrm(); //Resets the modrm info for the current opcode(not REP)!
void reset_modrmall(); //Resets the modrm settings for the new opcode (both REP and not REP)!

//For CPU itself:
/*

Slashr:
0: No slashr! (Use displacement if needed!)
1: RM=> REG2 (No displacement etc.)
2: REG1=> SEGMENTREGISTER (Use displacement if needed!)
3: Reg=> CRn(/r implied), R/M is General Purpose register!
4: Reg=> DRn(/r implied), R/M is General Purpose register!
5: R/M register is 8-bits instead!
6: R/M register is 16-bits instead!
7: Reg=>TRn(/r implied), R/M is General Purpose register!

*/
byte modrm_readparams(MODRM_PARAMS *param, byte size, byte specialflags, byte OP); //Read params for modr/m processing from CS:(E)IP
//Calculate the offsets used for the modr/m byte!
void modrm_recalc(MODRM_PARAMS *param);
//For fixing segment loads through MOV instructions.
void modrm_updatedsegment(word *location, word value, byte isJMPorCALL); //Check for updated segment registers!

//Checks for real and protected mode segments and paging!
//isread=0 for writes, 1 for reads.
byte modrm_check8(MODRM_PARAMS *params, int whichregister, byte isread);
byte modrm_check16(MODRM_PARAMS *params, int whichregister, byte isread);
byte modrm_check32(MODRM_PARAMS *params, int whichregister, byte isread);

//The following in in the general CPU module:
void modrm_debugger8(MODRM_PARAMS *theparams, byte whichregister1, byte whichregister2); //8-bit handler!
void modrm_debugger16(MODRM_PARAMS *theparams, byte whichregister1, byte whichregister2); //16-bit handler!
void modrm_debugger32(MODRM_PARAMS *theparams, byte whichregister1, byte whichregister2); //32-bit handler!
void modrm_generateInstructionTEXT(char *instruction, byte debuggersize, uint_32 paramdata, byte type); //In the CPU module, generates debugger modR/M info!

void CPU_writeCR0(uint_32 backupval, uint_32 value); //Update CR0 externally!


#endif
