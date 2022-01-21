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

#ifndef CPU_H
#define CPU_H

#include "headers/types.h"
#include "headers/cpu/mmu.h"
#include "headers/bios/bios.h" //Basic BIOS!
#include "headers/support/fifobuffer.h" //Prefetch Input Queue support!
#include "headers/cpu/paging.h" //Paging support!

//CPU?
extern BIOS_Settings_TYPE BIOS_Settings; //BIOS Settings (required for determining emulating CPU)

//How many CPU instances are used?
#define MAXCPUS 2

//Number of currently supported CPUs & opcode 0F extensions.
#define NUMCPUS 8
#define NUM0FEXTS 6
//What CPU is emulated?
#define CPU_8086 0
#define CPU_NECV30 1
#define CPU_80286 2
#define CPU_80386 3
#define CPU_80486 4
#define CPU_PENTIUMPRO 6
#define CPU_PENTIUM2 7
#define CPU_PENTIUM 5
#define CPU_MIN 0
#define CPU_MAX 7

//How many modes are there in the CPU? Currently 2: 16-bit and 32-bit modes!
#define CPU_MODES 2

//Currently emulating CPU (values see above, formula later)?
#define EMULATED_CPU emulated_CPUtype
//Since we're comparing to Bochs, emulate a Pentium PC!
//#define EMULATED_CPU CPU_PENTIUM

//How many MSRs are mapped at address 0 in the MSR space?
#define MAPPEDMSRS 0x500

//For easygoing solid state segments (not changeable) in CPU[activeCPU].registers.SEGMENT_REGISTERS[]
#define CPU_SEGMENT_CS 0
#define CPU_SEGMENT_SS 1
#define CPU_SEGMENT_DS 2
#define CPU_SEGMENT_ES 3
#define CPU_SEGMENT_FS 4
#define CPU_SEGMENT_GS 5
#define CPU_SEGMENT_TR 6
#define CPU_SEGMENT_LDTR 7
//Default specified segment!
#define CPU_SEGMENT_DEFAULT 0xFF

//The types of parameters used in the instruction for the instruction text debugger!
#define PARAM_NONE 0
#define PARAM_MODRM1 1
#define PARAM_MODRM2 2
#define PARAM_MODRM12 3
#define PARAM_MODRM21 4
#define PARAM_IMM8 5
#define PARAM_IMM16 6
#define PARAM_IMM32 7
#define PARAM_MODRM12_IMM8 8
#define PARAM_MODRM21_IMM8 9
#define PARAM_MODRM12_CL 10
#define PARAM_MODRM21_CL 11
#define PARAM_IMM8_PARAM 20
#define PARAM_IMM16_PARAM 21
#define PARAM_IMM32_PARAM 22

//Specifics based on the information table:
#define PARAM_MODRM_0_ACCUM_1 23
#define PARAM_MODRM_01 14
#define PARAM_MODRM_10 15
#define PARAM_MODRM_01_IMM8 16
#define PARAM_MODRM_10_IMM8 17
#define PARAM_MODRM_01_CL 18
#define PARAM_MODRM_10_CL 19
#define PARAM_MODRM_0 12
#define PARAM_MODRM_1 13
//Descriptors used by the CPU

//Data segment descriptor
//based on: http://www.c-jump.com/CIS77/ASM/Protection/W77_0050_segment_descriptor_structure.htm


//New struct based on: http://lkml.indiana.edu/hypermail/linux/kernel/0804.0/1447.html

#define u16 word
#define u8 byte
#define u32 uint_32

#include "headers/packed.h" //Packed type!
typedef union PACKED
{
	union
	{
		struct
		{
			union
			{
				u16 limit_low;
				u16 callgate_base_low; //Call Gate base low!
				word offsetlow; //Lower part of the interrupt function's offset address (a.k.a. pointer)
			};
			union
			{
				u16 base_low; //Low base for non-Gate Descriptors!
				u16 selector; //Selector field of a Gate Descriptor! Also the selector of the interrupt function, it's RPL field has be be 0.
			};
			union
			{
				u8 base_mid; //Mid base for non-Gate Descriptors!
				u8 ParamCnt; //Number of (uint_32) stack arguments to copy on stack switch(Call Gate Descriptor). Bits 4-7 have to be 0 for gate descriptors.
				byte zero; //Interrupt gate: Must be zero!
			};
			byte AccessRights; //Access rights!
			union
			{
				struct
				{
					union
					{
						u8 noncallgate_info; //Non-callgate access!
						u8 callgate_base_mid;
					};
					union
					{
						u8 base_high;
						u8 callgate_base_high;
					};
				};
				word offsethigh; //Higer part of the interrupt offset
			};
		};
		byte bytes[8]; //The data as bytes!
		byte descdata[8]; //The full entry data!
	};
	uint_32 dwords[2]; //2 32-bit values for easy access!
	uint_64 DATA64; //Full data for simple set!
} RAWSEGMENTDESCRIPTOR;
#include "headers/endpacked.h" //End of packed type!

typedef struct
{
	RAWSEGMENTDESCRIPTOR desc;
	struct
	{
		uint_64 base; //The effective base!
		uint_64 limit; //The effective limit!
		uint_64 roof; //What is the upper limit we can address using a top-down segment?
		byte topdown; //Are we a top-down segment?
		byte notpresent; //Are we a not-present segment?
		byte rwe_errorout[0x100]; //All possible conditions that error out. >0=Errors out for said read/write/execute!
	} PRECALCS; //The precalculated values!
} SEGMENT_DESCRIPTOR;

//Code/data descriptor information(General/Data/Exec segment)
#define GENERALSEGMENT_TYPE(descriptor) (descriptor.desc.AccessRights&0xF)
#define GENERALSEGMENT_S(descriptor) ((descriptor.desc.AccessRights>>4)&1)
#define GENERALSEGMENT_DPL(descriptor) ((descriptor.desc.AccessRights>>5)&3)
#define GENERALSEGMENT_P(descriptor) ((descriptor.desc.AccessRights>>7)&1)
#define CODEDATASEGMENT_A(descriptor) (descriptor.desc.AccessRights&1)
#define DATASEGMENT_W(descriptor) ((descriptor.desc.AccessRights>>1)&1)
#define DATASEGMENT_E(descriptorc) ((descriptor.desc.AccessRights>>2)&1)
#define DATASEGMENT_OTHERSTRUCT(descriptor) ((descriptor.desc.AccessRights>>3)&1)
#define EXECSEGMENT_R(descriptor) ((descriptor.desc.AccessRights>>1)&1)
#define EXECSEGMENT_C(descriptor) ((descriptor.desc.AccessRights>>2)&1)
#define EXECSEGMENT_ISEXEC(descriptor) ((descriptor.desc.AccessRights>>3)&1)

#define GENERALSEGMENTPTR_TYPE(descriptor) (descriptor->desc.AccessRights&0xF)
#define GENERALSEGMENTPTR_S(descriptor) ((descriptor->desc.AccessRights>>4)&1)
#define GENERALSEGMENTPTR_DPL(descriptor) ((descriptor->desc.AccessRights>>5)&3)
#define GENERALSEGMENTPTR_P(descriptor) ((descriptor->desc.AccessRights>>7)&1)
#define CODEDATASEGMENTPTR_A(descriptor) (descriptor->desc.AccessRights&1)
#define DATASEGMENTPTR_W(descriptor) ((descriptor->desc.AccessRights>>1)&1)
#define DATASEGMENTPTR_E(descriptor) ((descriptor->desc.AccessRights>>2)&1)
#define DATASEGMENTPTR_OTHERSTRUCT(descriptor) ((descriptor->desc.AccessRights>>3)&1)
#define EXECSEGMENTPTR_R(descriptor) ((descriptor->desc.AccessRights>>1)&1)
#define EXECSEGMENTPTR_C(descriptor) ((descriptor->desc.AccessRights>>2)&1)
#define EXECSEGMENTPTR_ISEXEC(descriptor) ((descriptor->desc.AccessRights>>3)&1)


//Pointer versions!

//Rest information in descriptors!
#define SEGDESC_NONCALLGATE_LIMIT_HIGH(descriptor) (descriptor.desc.noncallgate_info&0xF)
#define SEGDESC_NONCALLGATE_AVL(descriptor) ((descriptor.desc.noncallgate_info>>4)&1)
#define SEGDESC_NONCALLGATE_D_B(descriptor) ((descriptor.desc.noncallgate_info>>6)&1)
#define SEGDESC_NONCALLGATE_G(descriptor) ((descriptor.desc.noncallgate_info>>7)&1)
#define SEGDESC_GRANULARITY(descriptor) (SEGDESC_NONCALLGATE_G(descriptor)&CPU[activeCPU].G_Mask)

//Pointer versions!
#define SEGDESCPTR_NONCALLGATE_LIMIT_HIGH(descriptor) (descriptor->desc.noncallgate_info&0xF)
#define SEGDESCPTR_NONCALLGATE_AVL(descriptor) ((descriptor->desc.noncallgate_info>>4)&1)
#define SEGDESCPTR_NONCALLGATE_D_B(descriptor) ((descriptor->desc.noncallgate_info>>6)&1)
#define SEGDESCPTR_NONCALLGATE_G(descriptor) ((descriptor->desc.noncallgate_info>>7)&1)
#define SEGDESCPTR_GRANULARITY(descriptor) (SEGDESCPTR_NONCALLGATE_G(descriptor)&CPU[activeCPU].G_Mask)

#include "headers/packed.h" //Packed type!
typedef struct
{
	struct
	{
		word BackLink; //Back Link to Previous TSS
		union
		{
			struct
			{
				uint_32 ESP0;
				uint_32 ESP1;
				uint_32 ESP2;
			};
			uint_32 ESPs[3];
		};
		union
		{
			struct
			{
				word SS0;
				word SS1;
				word SS2;
			};
			word SSs[3];
		};
		union
		{
			struct
			{
				uint_32 CR3; //CR3 (PDPR)
				uint_32 EIP;
				uint_32 EFLAGS;
				uint_32 EAX;
				uint_32 ECX;
				uint_32 EDX;
				uint_32 EBX;
				uint_32 ESP;
				uint_32 EBP;
				uint_32 ESI;
				uint_32 EDI;
			};
			uint_32 generalpurposeregisters[11];
		};
		union
		{
			struct
			{
				word ES;
				word CS;
				word SS;
				word DS;
				word FS;
				word GS;
				word LDT;
			};
			word segmentregisters[7];
		};
		word T; //1-bit, upper 15 bits unused!
		word IOMapBase;
	};
} TSS386; //80386 32-Bit Task State Segment
#include "headers/endpacked.h" //End of packed type!

#include "headers/packed.h" //Packed type!
typedef union PACKED
{
	struct
	{
		word BackLink; //Back Link to Previous TSS
		word SP0;
		word SS0;
		word SP1;
		word SS1;
		word SP2;
		word SS2;
		word IP;
		word FLAGS;
		word AX;
		word CX;
		word DX;
		word BX;
		word SP;
		word BP;
		word SI;
		word DI;
		word ES;
		word CS;
		word SS;
		word DS;
		word LDT;
	};
	word dataw[22]; //All word-sized fields!
} TSS286; //80286 32-Bit Task State Segment
#include "headers/endpacked.h" //End of packed type!

#define IDTENTRY_TYPE(ENTRY) (ENTRY.AccessRights&0xF)
#define IDTENTRY_S(ENTRY) ((ENTRY.AccessRights>>4)&1)
#define IDTENTRY_DPL(ENTRY) ((ENTRY.AccessRights>>5)&3)
#define IDTENTRY_P(ENTRY) ((ENTRY.AccessRights>>7)&1)

//A segment descriptor

//Full gate value
#define IDTENTRY_TASKGATE 0x5

//Partial value for interrupt/trap gates!
#define IDTENTRY_INTERRUPTGATE 0x6
#define IDTENTRY_TRAPGATE 0x7
//32-bit variants (gate extensino set with interrupt&trap gates)
#define IDTENTRY_32BIT_GATEEXTENSIONFLAG 0x8
//How much to shift the gate extension flag to obtain 0/1 instead!
#define IDTENTRY_32BIT_GATEEXTENSIONFLAG_SHIFT 3

/*

Information:

A - ACCESSED
AVL - AVAILABLE FOR PROGRAMMERS USE
B - BIG
C - CONFORMING
D - DEFAULT
DPL - DESCRIPTOR PRIVILEGE LEVEL
E - EXPAND-DOWN
G - GRANUARITY
P - SEGMENT PRESENT (available for use?)
R - READABLE
W - WRITABLE


G: Determines Segment Limit & Segment size:
	G:0 = Segment Limit 1byte-1MB; 1B segment size; limit is ~1MB)
	G:1 = Segment Limit 4KB-4GB  ; 4KB (limit<<12 for limit of 4GB)

D_B: depends on type of access:
	code segment (see AVL):
		0=Operand size 16-bit
		1=Operand size 32-bit

	data segment (see AVL):
		0=SP is used with an upper bound of 0FFFFh (cleared for real mode)
		1=ESP is used with and upper bound of 0FFFFFFFh (set for protected mode)

S determines AVL type!

AVL: available to the programmer:
	see below for values.
*/

//AVL Also contains the type of segment (CODE DATA or SYSTEM)

//DATA descriptors:

#define AVL_DATA_READONLY 0
#define AVL_DATA_READONLY_ACCESSED 1
#define AVL_DATA_READWRITE 2
#define AVL_DATA_READWRITE_ACCESSED 3
#define AVL_DATA_READONLY_EXPANDDOWN 4
#define AVL_DATA_READONLY_EXPANDDOWN_ACCESSED 5
#define AVL_DATA_READWRITE_EXPANDDOWN 6
#define AVL_DATA_READWRITE_EXPANDDOWN_ACCESSED 7

//CODE descriptors:

#define AVL_CODE_EXECUTEONLY 8
#define AVL_CODE_EXECUTEONLY_ACCESSED 9
#define AVL_CODE_EXECUTE_READ 0xA
#define AVL_CODE_EXECUTE_READ_ACCESSED 0xB
#define AVL_CODE_EXECUTEONLY_CONFORMING 0xC
#define AVL_CODE_EXECUTEONLY_CONFORMING_ACCESSED 0xD
#define AVL_CODE_EXECUTE_READONLY_CONFORMING 0xE
#define AVL_CODE_EXECUTE_READONLY_CONFORMING_ACCESSED 0xF


//Type3 values (alternative for above):
#define AVL_TYPE3_READONLY 0
#define AVL_TYPE3_READWRITE 1
#define AVL_TYPE3_READONLY_EXPANDDOWN 2
#define AVL_TYPE3_READWRITE_EXPANDDOWN 3

//Or the bits:

#define AVL_TYPE3_ALLOWWRITEBIT 1
#define AVL_TYPE3_EXPANDDOWNBIT 2







//Extra data for above (bits on):

//Executable segment and values:
#define TYPE_EXEC_SEGBIT 4
#define EXEC_SEGBIT_DATASEG 0
#define EXEC_SEGBIT_CODESEG 1

//Expansion direction.
#define TYPE_EXPANDBIT 2
#define EXPAND_SEGBIT_UP 0
#define EXPAND_SEGBIT_DOWN 1

//Read/write
#define TYPE_READWRITEBIT 1

//System segment descriptor types:

#define AVL_SYSTEM_RESERVED_0 0
#define AVL_SYSTEM_TSS16BIT 1
#define AVL_SYSTEM_LDT 2
#define AVL_SYSTEM_BUSY_TSS16BIT 3
#define AVL_SYSTEM_CALLGATE16BIT 4
#define AVL_SYSTEM_TASKGATE 5
#define AVL_SYSTEM_INTERRUPTGATE16BIT 6
#define AVL_SYSTEM_TRAPGATE16BIT 7
#define AVL_SYSTEM_RESERVED_1 8
#define AVL_SYSTEM_TSS32BIT 9
#define AVL_SYSTEM_RESERVED_2 0xA
#define AVL_SYSTEM_BUSY_TSS32BIT 0xB
#define AVL_SYSTEM_CALLGATE32BIT 0xC
#define AVL_SYSTEM_RESERVED_3 0xD
#define AVL_SYSTEM_INTERRUPTGATE32BIT 0xE
#define AVL_SYSTEM_TRAPGATE32BIT 0xF

//General Purpose register support!

typedef union
{
	uint_32 reg32;
	word reg16[2];
	byte reg8[4];
} registersplitter;

#ifdef IS_BIG_ENDIAN
#define GPREG16_LO 1
#define GPREG8_LO 3
#define GPREG8_HI 2
#else
#define GPREG16_LO 0
#define GPREG8_LO 0
#define GPREG8_HI 1
#endif

enum
{
	GPREG_EAX = 0,
	GPREG_EBX = 1,
	GPREG_ECX = 2,
	GPREG_EDX = 3,
	GPREG_ESP = 4,
	GPREG_EBP = 5,
	GPREG_ESI = 6,
	GPREG_EDI = 7,
	GPREG_EIP = 8,
	GPREG_EFLAGS = 9,
	SREG_CS = 10,
	SREG_SS = 11,
	SREG_DS = 12,
	SREG_ES = 13,
	SREG_FS = 14,
	SREG_GS = 15,
	SREG_TR = 16,
	SREG_LDTR = 17
}; //All general purpose registers locations in the list!

//Short versions of the registers allocated with the above values as input!
#define REG8_LO(reg) CPU[activeCPU].registers->gpregisters[reg].reg8[GPREG8_LO]
#define REG8_HI(reg) CPU[activeCPU].registers->gpregisters[reg].reg8[GPREG8_HI]
#define REG16_LO(reg) CPU[activeCPU].registers->gpregisters[reg].reg16[GPREG16_LO]
#define REG32(reg) CPU[activeCPU].registers->gpregisters[reg].reg32
//Registers version:
#define REG8R_LO(list,reg) list->gpregisters[reg].reg8[GPREG8_LO]
#define REG8R_HI(list,reg) list->gpregisters[reg].reg8[GPREG8_HI]
#define REG16R_LO(list,reg) list->gpregisters[reg].reg16[GPREG16_LO]
#define REG32R(list,reg) list->gpregisters[reg].reg32
//Direct version:
#define REG8D_LO(list,reg) list.gpregisters[reg].reg8[GPREG8_LO]
#define REG8D_HI(list,reg) list.gpregisters[reg].reg8[GPREG8_HI]
#define REG16D_LO(list,reg) list.gpregisters[reg].reg16[GPREG16_LO]
#define REG32D(list,reg) list.gpregisters[reg].reg32

#define REGR_AL(list) REG8R_LO(list,GPREG_EAX)
#define REGR_AH(list) REG8R_HI(list,GPREG_EAX)
#define REGR_AX(list) REG16R_LO(list,GPREG_EAX)
#define REGR_EAX(list) REG32R(list,GPREG_EAX)
#define REGR_BL(list) REG8R_LO(list,GPREG_EBX)
#define REGR_BH(list) REG8R_HI(list,GPREG_EBX)
#define REGR_BX(list) REG16R_LO(list,GPREG_EBX)
#define REGR_EBX(list) REG32R(list,GPREG_EBX)
#define REGR_CL(list) REG8R_LO(list,GPREG_ECX)
#define REGR_CH(list) REG8R_HI(list,GPREG_ECX)
#define REGR_CX(list) REG16R_LO(list,GPREG_ECX)
#define REGR_ECX(list) REG32R(list,GPREG_ECX)
#define REGR_DL(list) REG8R_LO(list,GPREG_EDX)
#define REGR_DH(list) REG8R_HI(list,GPREG_EDX)
#define REGR_DX(list) REG16R_LO(list,GPREG_EDX)
#define REGR_EDX(list) REG32R(list,GPREG_EDX)
#define REGR_SP(list) REG16R_LO(list,GPREG_ESP)
#define REGR_ESP(list) REG32R(list,GPREG_ESP)
#define REGR_BP(list) REG16R_LO(list,GPREG_EBP)
#define REGR_EBP(list) REG32R(list,GPREG_EBP)
#define REGR_SI(list) REG16R_LO(list,GPREG_ESI)
#define REGR_ESI(list) REG32R(list,GPREG_ESI)
#define REGR_DI(list) REG16R_LO(list,GPREG_EDI)
#define REGR_EDI(list) REG32R(list,GPREG_EDI)
#define REGR_IP(list) REG16R_LO(list,GPREG_EIP)
#define REGR_EIP(list) REG32R(list,GPREG_EIP)
#define REGR_FLAGS(list) REG16R_LO(list,GPREG_EFLAGS)
#define REGR_EFLAGS(list) REG32R(list,GPREG_EFLAGS)
#define REGR_CS(list) REG16R_LO(list,SREG_CS)
#define REGR_DS(list) REG16R_LO(list,SREG_DS)
#define REGR_ES(list) REG16R_LO(list,SREG_ES)
#define REGR_FS(list) REG16R_LO(list,SREG_FS)
#define REGR_GS(list) REG16R_LO(list,SREG_GS)
#define REGR_SS(list) REG16R_LO(list,SREG_SS)
#define REGR_TR(list) REG16R_LO(list,SREG_TR)
#define REGR_LDTR(list) REG16R_LO(list,SREG_LDTR)

//Direct version
#define REGD_AL(list) REG8D_LO(list,GPREG_EAX)
#define REGD_AH(list) REG8D_HI(list,GPREG_EAX)
#define REGD_AX(list) REG16D_LO(list,GPREG_EAX)
#define REGD_EAX(list) REG32D(list,GPREG_EAX)
#define REGD_BL(list) REG8D_LO(list,GPREG_EBX)
#define REGD_BH(list) REG8D_HI(list,GPREG_EBX)
#define REGD_BX(list) REG16D_LO(list,GPREG_EBX)
#define REGD_EBX(list) REG32D(list,GPREG_EBX)
#define REGD_CL(list) REG8D_LO(list,GPREG_ECX)
#define REGD_CH(list) REG8D_HI(list,GPREG_ECX)
#define REGD_CX(list) REG16D_LO(list,GPREG_ECX)
#define REGD_ECX(list) REG32D(list,GPREG_ECX)
#define REGD_DL(list) REG8D_LO(list,GPREG_EDX)
#define REGD_DH(list) REG8D_HI(list,GPREG_EDX)
#define REGD_DX(list) REG16D_LO(list,GPREG_EDX)
#define REGD_EDX(list) REG32D(list,GPREG_EDX)
#define REGD_SP(list) REG16D_LO(list,GPREG_ESP)
#define REGD_ESP(list) REG32D(list,GPREG_ESP)
#define REGD_BP(list) REG16D_LO(list,GPREG_EBP)
#define REGD_EBP(list) REG32D(list,GPREG_EBP)
#define REGD_SI(list) REG16D_LO(list,GPREG_ESI)
#define REGD_ESI(list) REG32D(list,GPREG_ESI)
#define REGD_DI(list) REG16D_LO(list,GPREG_EDI)
#define REGD_EDI(list) REG32D(list,GPREG_EDI)
#define REGD_IP(list) REG16D_LO(list,GPREG_EIP)
#define REGD_EIP(list) REG32D(list,GPREG_EIP)
#define REGD_FLAGS(list) REG16D_LO(list,GPREG_EFLAGS)
#define REGD_EFLAGS(list) REG32D(list,GPREG_EFLAGS)
#define REGD_CS(list) REG16D_LO(list,SREG_CS)
#define REGD_DS(list) REG16D_LO(list,SREG_DS)
#define REGD_ES(list) REG16D_LO(list,SREG_ES)
#define REGD_FS(list) REG16D_LO(list,SREG_FS)
#define REGD_GS(list) REG16D_LO(list,SREG_GS)
#define REGD_SS(list) REG16D_LO(list,SREG_SS)
#define REGD_TR(list) REG16D_LO(list,SREG_TR)
#define REGD_LDTR(list) REG16D_LO(list,SREG_LDTR)

//All flags, seperated!

#define F_CARRY 0x01
//Carry flag (CF): Math instr: high-order bit carried or borrowed, else cleared.
//0x02 unmapped
#define F_PARITY 0x04
//Parity flag (PF): Lower 8-bits of a result contains an even number of bits set to 1 (set), else not set (cleared).
//0x08 unmapped.
#define F_AUXILIARY_CARRY 0x10
//Auxiliary Carry flag (AF): (=Adjust flag?): Math instr: low order 4-bits of AL were carried or borrowed, else not set (cleared).
//0x20 unmapped.
#define F_ZERO 0x40
//Zero flag (ZF): Math instr: Result=0: Set; Else Cleared.
#define F_SIGN 0x80
//Sign flag (SF): Set equal to high-order bit of results of math instr. (Result<0)=>Set; (Result>=0)=>Cleared.
#define F_TRAP 0x100
//Trap flag (TF)
#define F_INTERRUPT 0x200
//Interrupt flag (IF)
#define F_DIRECTION 0x400
//Direction flag (DF): Used by string instr. to determine to process strings from end (DECR) or start (INCR).
#define F_OVERFLOW 0x800
//Overflow flag (OF): Indicates if the number placed in the detination operand overflowed, either too large or small. No overflow=Cleared.
#define F_IOPL 0x3000
//I/O Privilege Level (two bits long) (PL)
#define F_NESTEDTASK 0x4000
//Nested Task Flag (NT)
//0x8000 unmapped.

//New flags in 80386:
#define F_RESUME 0x10000
//Resume flag
#define F_V8 0x20000
//Virtual 8086 MODE flag

//New flags in 80486:
#define F_AC 0x40000

//New flags in 80486:
//Virtual interrupt flag
#define F_VIF 0x80000
//Virtual interrupt pending
#define F_VIP 0x100000
//Able to use CPUID instruction
#define F_ID 0x200000

//Same, but shorter:

#define F_CF F_CARRY
#define F_PF F_PARITY
#define F_AF F_AUXILIARY_CARRY
#define F_ZF F_ZERO
#define F_SF F_SIGN
#define F_TF F_TRAP
#define F_IF F_INTERRUPT
#define F_DF F_DIRECTION
#define F_PL F_IOPL
#define F_NT F_NESTEDTASK
#define F_RF F_RESUME

//Read versions
#define FLAGREGR_CF(registers) (REG16R_LO(registers,GPREG_EFLAGS)&1)
//Unused. Value 1
#define FLAGREGR_UNMAPPED2(registers) ((REG16R_LO(registers,GPREG_EFLAGS)>>1)&1)
#define FLAGREGR_PF(registers) ((REG16R_LO(registers,GPREG_EFLAGS)>>2)&1)
//Unused. Value 0
#define FLAGREGR_UNMAPPED8(registers) ((REG16R_LO(registers,GPREG_EFLAGS)>>3)&1)
#define FLAGREGR_AF(registers) ((REG16R_LO(registers,GPREG_EFLAGS)>>4)&1)
//Unused. Value 1 on 186-, 0 on later models.
#define FLAGREGR_UNMAPPED32768(registers) ((REG16R_LO(registers,GPREG_EFLAGS)>>15)&1)
//Unused. Value 0
#define FLAGREGR_UNMAPPED32(registers) ((REG16R_LO(registers,GPREG_EFLAGS)>>5)&1)
#define FLAGREGR_ZF(registers) ((REG16R_LO(registers,GPREG_EFLAGS)>>6)&1)
#define FLAGREGR_SF(registers) ((REG16R_LO(registers,GPREG_EFLAGS)>>7)&1)
#define FLAGREGR_TF(registers) ((REG16R_LO(registers,GPREG_EFLAGS)>>8)&1)
#define FLAGREGR_IF(registers) ((REG16R_LO(registers,GPREG_EFLAGS)>>9)&1)
#define FLAGREGR_DF(registers) ((REG16R_LO(registers,GPREG_EFLAGS)>>10)&1)
#define FLAGREGR_OF(registers) ((REG16R_LO(registers,GPREG_EFLAGS)>>11)&1)
//Always 1 on 186-
#define FLAGREGR_IOPL(registers) ((REG16R_LO(registers,GPREG_EFLAGS)>>12)&3)
//Always 1 on 186-
#define FLAGREGR_NT(registers) ((REG16R_LO(registers,GPREG_EFLAGS)>>14)&1)
//High nibble
//Resume flag. 386+
#define FLAGREGR_RF(registers) ((REG32R(registers,GPREG_EFLAGS)>>16)&1)
//Virtual 8086 mode flag (386+ only)
#define FLAGREGR_V8(registers) ((REG32R(registers,GPREG_EFLAGS)>>17)&1)
//Alignment check (486SX+ only)
#define FLAGREGR_AC(registers) ((REG32R(registers,GPREG_EFLAGS)>>18)&1)
//Virtual interrupt flag (Pentium+)
#define FLAGREGR_VIF(registers) ((REG32R(registers,GPREG_EFLAGS)>>19)&1)
//Virtual interrupt pending (Pentium+)
#define FLAGREGR_VIP(registers) ((REG32R(registers,GPREG_EFLAGS)>>20)&1)
//Able to use CPUID function (Pentium+)
#define FLAGREGR_ID(registers) ((REG32R(registers,GPREG_EFLAGS)>>21)&1)
#define FLAGREGR_UNMAPPEDHI(registers) (REG32R(registers,GPREG_EFLAGS)>>22)&0x3FF

//Write versions
#define FLAGREGW_CF(registers,val) REG16R_LO(registers,GPREG_EFLAGS)=((REG16R_LO(registers,GPREG_EFLAGS)&~1)|((val)&1))
//Unused. Value 1
#define FLAGREGW_UNMAPPED2(registers,val) REG16R_LO(registers,GPREG_EFLAGS)=(((REG16R_LO(registers,GPREG_EFLAGS)&~2)|(((val)&1)<<1)))
#define FLAGREGW_PF(registers,val) REG16R_LO(registers,GPREG_EFLAGS)=(((REG16R_LO(registers,GPREG_EFLAGS)&~4)|(((val)&1)<<2)))
//Unused. Value 0
#define FLAGREGW_UNMAPPED8(registers,val) REG16R_LO(registers,GPREG_EFLAGS)=(((REG16R_LO(registers,GPREG_EFLAGS)&~8)|(((val)&1)<<3)))
#define FLAGREGW_AF(registers,val) REG16R_LO(registers,GPREG_EFLAGS)=(((REG16R_LO(registers,GPREG_EFLAGS)&~0x10)|(((val)&1)<<4)))
//Unused. Value 1 on 186-, 0 on later models.
#define FLAGREGW_UNMAPPED32768(registers,val) REG16R_LO(registers,GPREG_EFLAGS)=(((REG16R_LO(registers,GPREG_EFLAGS)&~0x8000)|(((val)&1)<<15)))
//Unused. Value 0
#define FLAGREGW_UNMAPPED32(registers,val) REG16R_LO(registers,GPREG_EFLAGS)=(((REG16R_LO(registers,GPREG_EFLAGS)&~0x20)|(((val)&1)<<5)))
#define FLAGREGW_ZF(registers,val) REG16R_LO(registers,GPREG_EFLAGS)=(((REG16R_LO(registers,GPREG_EFLAGS)&~0x40)|(((val)&1)<<6)))
#define FLAGREGW_SF(registers,val) REG16R_LO(registers,GPREG_EFLAGS)=(((REG16R_LO(registers,GPREG_EFLAGS)&~0x80)|(((val)&1)<<7)))
#define FLAGREGW_TF(registers,val) REG16R_LO(registers,GPREG_EFLAGS)=(((REG16R_LO(registers,GPREG_EFLAGS)&~0x100)|(((val)&1)<<8)))
#define FLAGREGW_IF(registers,val) REG16R_LO(registers,GPREG_EFLAGS)=(((REG16R_LO(registers,GPREG_EFLAGS)&~0x200)|(((val)&1)<<9)))
#define FLAGREGW_DF(registers,val) REG16R_LO(registers,GPREG_EFLAGS)=(((REG16R_LO(registers,GPREG_EFLAGS)&~0x400)|(((val)&1)<<10)))
#define FLAGREGW_OF(registers,val) REG16R_LO(registers,GPREG_EFLAGS)=(((REG16R_LO(registers,GPREG_EFLAGS)&~0x800)|(((val)&1)<<11)))
//Always 1 on 186-
#define FLAGREGW_IOPL(registers,val) REG16R_LO(registers,GPREG_EFLAGS)=(((REG16R_LO(registers,GPREG_EFLAGS)&~0x3000)|(((val)&3)<<12)))
//Always 1 on 186-
#define FLAGREGW_NT(registers,val) REG16R_LO(registers,GPREG_EFLAGS)=(((REG16R_LO(registers,GPREG_EFLAGS)&~0x4000)|(((val)&1)<<14)))
//High nibble
//Resume flag. 386+
#define FLAGREGW_RF(registers,val) REG32R(registers,GPREG_EFLAGS)=(((REG32R(registers,GPREG_EFLAGS)&~0x10000)|(((val)&1)<<16)))
//Virtual 8086 mode flag (386+ only)
#define FLAGREGW_V8(registers,val) REG32R(registers,GPREG_EFLAGS)=(((REG32R(registers,GPREG_EFLAGS)&~0x20000)|(((val)&1)<<17)))
//Alignment check (486SX+ only)
#define FLAGREGW_AC(registers,val) REG32R(registers,GPREG_EFLAGS)=(((REG32R(registers,GPREG_EFLAGS)&~0x40000)|(((val)&1)<<18))); updateCPUmode()
//Virtual interrupt flag (Pentium+)
#define FLAGREGW_VIF(registers,val) REG32R(registers,GPREG_EFLAGS)=(((REG32R(registers,GPREG_EFLAGS)&~0x80000)|(((val)&1)<<19)))
//Virtual interrupt pending (Pentium+)
#define FLAGREGW_VIP(registers,val) REG32R(registers,GPREG_EFLAGS)=(((REG32R(registers,GPREG_EFLAGS)&~0x100000)|(((val)&1)<<20)))
//Able to use CPUID function (Pentium+)
#define FLAGREGW_ID(registers,val) REG32R(registers,GPREG_EFLAGS)=(((REG32R(registers,GPREG_EFLAGS)&~0x200000)|(((val)&1)<<21)))
#define FLAGREGW_UNMAPPEDHI(registers,val) REG32R(registers,GPREG_EFLAGS)=((REG32R(registers,GPREG_EFLAGS)&0x3FFFFF)|(((val)&0x3FF)<<22))

#include "headers/packed.h" //Packed type!
typedef struct PACKED
{
	union
	{
		struct
		{
			word limit; //Limit
			uint_32 base; //Base
			word unused1; //Unused 1!
		};
		uint_64 data; //48 bits long!
	};
} DTR_PTR;
#include "headers/endpacked.h" //End of packed type!

typedef struct
{
	uint_32 lo; //Low dword!
	uint_32 hi; //High dword!
} CPUMSR;

#define CPU_NUMMSRS 0x5C

typedef struct //The registers!
{
	//First, the General Purpose registers!
	registersplitter gpregisters[18]; //10 general purpose registers! EAX, EBX, ECX, EDX, ESP, EBP, ESI, EDI, EIP, EFLAGS, CS, DS, ES, FS, GS, SS, TR, LDTR!
//Info: with union, first low data then high data!

	struct
	{
		union
		{
			uint_32 CR[8];
			struct
			{
				uint_32 CR0;
				uint_32 CR1; //Unused!
				uint_32 CR2; //Page Fault Linear Address
				uint_32 CR3;
				uint_32 CR4; //4 unused CRs until Pentium!
				uint_32 CR5;
				uint_32 CR6;
				uint_32 CR7;
			}; //CR0-3!
		}; //CR0-3!
		union
		{
			uint_32 DR[8]; //All debugger registers! index 4=>6 and index 5=>7!
			struct
			{
				uint_32 DR0;
				uint_32 DR1;
				uint_32 DR2;
				uint_32 DR3;
				uint_32 DR4; //Existant on Pentium+ only! Redirected to DR6 on 386+, except when enabled using CR4.
				uint_32 noDRregister; //Not defined: DR5 on Pentium! Redirected to DR7 on 386+(when not disabled using CR4).
				uint_32 DR6; //DR4->6 on 386+, DR6 on Pentium+!
				uint_32 DR7; //DR5->7 on 386+, DR7 on Pentium+!
			};
		}; //DR0-7; 4=6&5=7!
		union
		{
			uint_32 TRX[8]; //All debugger registers! index 4=>6 and index 5=>7!
			struct
			{
				uint_32 TR0;
				uint_32 TR1;
				uint_32 TR2;
				uint_32 TR3;
				uint_32 TR4;
				uint_32 TR5;
				uint_32 TR6;
				uint_32 TR7;
			};
		}; //DR0-7; 4=6&5=7!
	}; //Special registers!
//Tables:
	DTR_PTR GDTR; //GDTR pointer (48-bits) Global Descriptor Table Register
	DTR_PTR IDTR; //IDTR pointer (48-bits) Interrupt Descriptor Table Register

	CPUMSR genericMSR[CPU_NUMMSRS]; //Generic, unnamed MSR containers!

	//MSR registers (Pentium II and up)
	CPUMSR IA32_SYSENTER_CS;
	CPUMSR IA32_SYSENTER_ESP;
	CPUMSR IA32_SYSENTER_EIP;
} CPU_registers; //Registers

//Protected mode enable
#define CR0_PE 0x00000001
//Math coprocessor present
#define CR0_MP 0x00000002 
//Emulation: math instructions are to be emulated?
#define CR0_EM 0x00000004
//Task Switched
#define CR0_TS 0x00000008 
//Extension Type: type of coprocessor present, 80286 or 80387
#define CR0_ET 0x00000010
//26 unknown/unspecified bits
//Bit 31
//Paging enable
 #define CR0_PG 0x80000000

typedef struct
{
	byte CPU_isFetching; //1=Fetching/decoding new instruction to execute(CPU_readOP_prefix), 0=Executing instruction(decoded)
	byte CPU_fetchphase; //Fetching phase: 1=Reading new opcode, 2=Reading prefixes or opcode, 3=Reading 0F instruction, 0=Main Opcode fetched
	byte CPU_fetchingRM; //1=Fetching modR/M parameters, 0=ModR/M loaded when used.
	byte CPU_fetchparameters; //1+=Fetching parameter #X, 0=Parameters fetched.
	byte CPU_fetchparameterPos; //Parameter position we're fetching. 0=First byte, 1=Second byte etc.
} CPU_InstructionFetchingStatus;

#include "headers/packed.h" //Packed!
typedef union PACKED
{
	struct
	{
		word baselow; //First word
		word basehighaccessrights; //Second word low bits=base high, high=access rights!
		word limit; //Third word
	};
	word data[3]; //All our descriptor cache data!
} DESCRIPTORCACHE286;
#include "headers/endpacked.h" //Finished!

#include "headers/packed.h" //Packed!
typedef union PACKED
{
	struct
	{
		word baselow; //First word
		word basehigh; //Second word low bits, high=zeroed!
		word limit; //Third word
	};
	word data[3];
} DTRdata286;
#include "headers/endpacked.h" //Finished!

#include "headers/packed.h" //Packed!
typedef union PACKED
{
	struct
	{
		uint_32 AR;
		uint_32 BASE;
		uint_32 LIMIT;
	};
	uint_32 data[3]; //All our descriptor cache data!
} DESCRIPTORCACHE386;
#include "headers/endpacked.h" //Finished!

#include "headers/packed.h" //Packed!
typedef union PACKED
{
	struct
	{
		uint_32 AR;
		uint_32 BASE;
		uint_32 LIMIT;
	};
	uint_32 data[3];
} DTRdata386;
#include "headers/endpacked.h" //Finished!

#include "headers/packed.h" //Packed
typedef union PACKED
{
	struct
	{
		word unused[3];
		word MSW;
		word unused2[7];
		word TR;
		word flags;
		word IP;
		word LDT;
		word DS;
		word SS;
		word CS;
		word ES;
		word DI;
		word SI;
		word BP;
		word SP;
		word BX;
		word DX;
		word CX;
		word AX;
		DESCRIPTORCACHE286 ESdescriptor;
		DESCRIPTORCACHE286 CSdescriptor;
		DESCRIPTORCACHE286 SSdescriptor;
		DESCRIPTORCACHE286 DSdescriptor;
		DTRdata286 GDTR;
		DESCRIPTORCACHE286 LDTdescriptor;
		DTRdata286 IDTR;
		DESCRIPTORCACHE286 TSSdescriptor;
	} fields; //Fields
	word dataw[0x33]; //Word-sized data to be loaded, if any!
} LOADALL286DATATYPE;
#include "headers/endpacked.h"

#include "headers/packed.h" //Packed
typedef union PACKED
{
	struct
	{
		uint_32 CR0;
		uint_32 EFLAGS;
		uint_32 EIP;
		uint_32 EDI;
		uint_32 ESI;
		uint_32 EBP;
		uint_32 ESP;
		uint_32 EBX;
		uint_32 EDX;
		uint_32 ECX;
		uint_32 EAX;
		uint_32 DR6;
		uint_32 DR7;
		uint_32 TR;
		uint_32 LDTR;
		uint_32 GS;
		uint_32 FS;
		uint_32	DS;
		uint_32 SS;
		uint_32 CS;
		uint_32 ES;
		DESCRIPTORCACHE386 TRdescriptor;
		DTRdata386 IDTR;
		DTRdata386 GDTR;
		DESCRIPTORCACHE386 LDTRdescriptor;
		DESCRIPTORCACHE386 GSdescriptor;
		DESCRIPTORCACHE386 FSdescriptor;
		DESCRIPTORCACHE386 DSdescriptor;
		DESCRIPTORCACHE386 SSdescriptor;
		DESCRIPTORCACHE386 CSdescriptor;
		DESCRIPTORCACHE386 ESdescriptor;
	} fields; //Fields
	uint_32 datad[0x33]; //Our data size!
} LOADALL386DATATYPE;
#include "headers/endpacked.h" //End packed!

#include "headers/cpu/modrm.h" //Param support!
#include "headers/cpu/cpu_opcodeinformation.h" //Opcode information support!

typedef struct
{
	CPU_registers *registers; //The registers of the CPU!

	//Everything containing and buffering segment registers!
	SEGMENT_DESCRIPTOR SEG_DESCRIPTOR[8]; //Segment descriptor for all segment registers, currently cached, loaded when it's used!
	SEGMENT_DESCRIPTOR SEG_DESCRIPTORbackup[8]; //Segment descriptor for all segment registers, currently cached, loaded when it's used!
	word *SEGMENT_REGISTERS[8]; //Segment registers pointers container (CS, SS, DS, ES, FS, GS, TR; in that order)!
	byte have_oldSegReg; //old segment register and cache is set to use(bit index=segment index)?
	word oldSegReg[8];
	byte CPL; //The current privilege level, registered on descriptor load!

	uint_32 cycles; //Total cycles number (adjusted after operation)
	byte cycles_OP; //Total number of cycles for an operation!
	byte cycles_HWOP; //Total number of cycles for an hardware interrupt!
	byte cycles_Prefix; //Total number of cycles for the prefix!
	byte cycles_EA; //ModR/M decode cycles!
	byte cycles_Exception; //Total number of cycles for an exception!
	byte cycles_Prefetch; //Total number of cycles for prefetching from memory!
	byte cycles_stallBIU; //How many cycles to stall the BIU this step?
	byte cycles_stallBUS; //How many cycles to stall the BUS(all CPU hardware) this step?
	byte cycles_Prefetch_BIU; //BIU cycles actually spent on prefetching during the remaining idle BUS time!
	byte cycles_Prefetch_DMA; //DMA cycles actually spent on prefetching during the remaining idle BUS time!

	//PE in .registers.CR0.PE: In real mode or V86 mode (V86 flag&PE=V86; !PE=protected; else real)?

	byte segment_register; //Current segment register of the above!
	int halt; //Halted: waiting for interrupt to occur!
	int wait; //Wait: wait for TEST pin to occur (8087)
	int blocked; //Blocked=1: int 21 function 9C00h stops CPU. Reset blocked to 0 when occurs.
	int continue_int; //Continue interrupt call=1 or (POP CS:IP)=0?
	int calllayer; //What CALL layer are we (Starts with 0 for none, 1+=CALL)
	int running; //We're running?
	byte currentopcode; //Currently/last running opcode!
	byte currentopcode0F; //Currently/last opcode 0F state!
	byte currentmodrm; //Currently/last ModR/M byte value!

	byte previousopcode; //Previous opcode for diagnostic purposes!
	byte previousopcode0F; //Previous opcode 0F state!
	byte previousmodrm; //Previous ModR/M byte value!
	uint_32 previousCSstart; //Previous CS starting address!
	byte faultraised; //Has a fault been raised by the protection module?
	byte faultraised_external; //External fault raised?
	byte faultlevel; //The level of the raised fault!
	byte faultraised_lasttype; //Last type of fault raised!
	byte trapped; //Have we been trapped? Don't execute hardware interrupts!

	//REP support (ignore re-reading instruction bytes from memory)
	byte repeating; //We're executing a REP* instruction?
	byte REPfinishtiming; //Finish timing of a REP loop!
	byte gotREP; //Did we got REP?

	//POP SS inhabits interrupts!
	byte allowInterrupts; //Do we allow interrupts to run?
	byte previousAllowInterrupts; //Do we allow interrupts to run?
	byte allowTF; //Allow trapping now?
	byte is0Fopcode; //Are we a 0F opcode to be executed?
	byte D_B_Mask; //D_B bit mask when used for 16 vs 32-bits!
	byte G_Mask; //G bit mask when used for 16 vs 32-bits!

	//For stack argument copying of call gates!
	uint_32 CallGateStack[256]; //Arguments to copy!
	word CallGateParamCount;
	byte CallGateSize; //The size of the data to copy!
	byte is_reset; //Are we a reset CPU?
	byte permanentreset; //Are we in a permanent reset lock?

	//80286 timing support for lookup tables!
	byte have_oldCPL; //oldCPL is set to use?
	byte oldCPL; //CPL backup
	byte oldCPUmode; //For debugging purposes!
	word oldSS; //SS backup for fault handling!
	byte have_oldESP; //oldESP is set to use?
	uint_32 oldESP; //Back-up of ESP during stack faults to use!
	byte have_oldESPinstr; //oldESP is set to use?
	uint_32 oldESPinstr; //Back-up of ESP during stack faults to use!
	byte have_oldEBP; //oldEBP is set to use?
	uint_32 oldEBP; //Back-up of EBP during stack faults to use!
	byte have_oldEBPinstr; //oldESP is set to use?
	uint_32 oldEBPinstr; //Back-up of ESP during stack faults to use!
	byte have_oldEFLAGS;
	uint_32 oldEFLAGS;
	byte debuggerFaultRaised; //Debugger faults raised after execution flags?
	CPU_InstructionFetchingStatus instructionfetch; //Information about fetching the current instruction. This contains the status we're in!
	byte executed; //Has the current instruction finished executing?
	word instructionstep, internalinstructionstep, modrmstep, internalmodrmstep, internalinterruptstep, stackchecked; //Step we're at, executing the instruction that's fetched and loaded to execute.
	byte timingpath; //Timing path taken?
	byte pushbusy; //Is a push operation busy?
	byte resetPending; //Is a CPU reset pending?
	CPU_TLB Paging_TLB; //Our TLB to use for paging access!
	byte is_paging; //Are we paging?
	byte is_aligning; //Is data alignment(align flag) in effect?
	uint_64 address_size; //Effective address size for the current instruction!
	byte activeBreakpoint[4]; //Are we an active breakpoint?
	uint_64 TSC; //Timestamp counter, counts raw clock cycles!
	double TSCtiming; //How much remaining time has been counted on top of the time-stamp counter?
	byte unaffectedRF; //Don't affect the resume flag this instruction!
	byte BIUnotticking;
	byte preinstructiontimingnotready; //Pre-instruction timing ready?
	word exec_CS, exec_lastCS; //(Previous) Executing CS!
	uint_32 exec_EIP, exec_lastEIP; //(Previous) Executing EIP!
	uint_32 InterruptReturnEIP; //Interrupt return EIP!
	word nextCS; //Next instruction CS
	uint_32 nextEIP; //Next instruction EIP
	word SIPIreceived; //SIPI received?
	byte waitingforSIPI; //Waiting for SIPI? Set for CPU cores, but not the BSP!
	byte CPUmode; //The current CPU mode!

	//Runtime values
	byte XLAT_value; //XLAT
	byte writeword; //Hi-end word written?
	byte secondparambase, writebackbase;
	word oldCS, oldIP, waitingforiret;
	byte tmps, tmpp; //Sign/parity backup!
	byte thereg; //For function number!
	word tempSHLRDW;
	uint_32 tempSHLRDD;
	uint_32 tempEAX;
	uint_64 tempEDXEAX;
	byte tempAL;
	word tempAX;
	uint_32 tempDXAX;
	byte tempcycles;
	byte oper1b, oper2b; //Byte variants!
	word oper1, oper2; //Word variants!
	byte res8; //Result 8-bit!
	word res16; //Result 16-bit!
	byte tempCF2;

	VAL32Splitter temp1, temp2, temp3, temp4, temp5; //All temporary values!
	VAL64Splitter temp1l, temp2l, temp3l, temp4l, temp5l; //All temporary values!
	uint_32 temp32, tempaddr32; //Defined in opcodes_8086.c
	word temp8Edata;
	uint_32 oper1d, oper2d; //DWord variants!
	uint_32 res32; //Result 32-bit!

	char tempbuf[256];
	uint64_t dst, add, sub;

	struct
	{
		int whatsegment;
		SEGMENT_DESCRIPTOR LOADEDDESCRIPTOR;
		word* segment;
		word destinationtask;
		byte isJMPorCALL;
		byte gated;
		int_64 errorcode;
		byte taskswitch_result;
	} TASKSWITCH_INFO;

	byte successfullpagemapping;

	word segmentWrittenVal, isJMPorCALLval;
	word segmentWritten_tempSS;
	uint_32 segmentWritten_tempESP;
	word segmentWritten_tempSP;
	byte is_stackswitching; //Are we busy stack switching?
	byte SCASB_cmp1;
	word SCASW_cmp1;
	uint_32 SCASD_cmp1;
	uint_32 RETFD_val; //Far return
	word RETF_destCS;
	word RETF_val; //Far return
	word RETF_popbytes; //How many to pop?
	uint_32 RETD_val;
	word RET_val;
	byte REPZ; //Default to REP!
	byte didNewREP, didRepeating; //Did we do a REP?
	byte BIUresponsedummy;
	byte REPPending; //Pending REP reset?
	word Rdata1, Rdata2; //3 words of data to access!
	byte portrights_error;
	byte portExceptionResult; //Init to 0xFF?
	word PFflags;
	MODRM_PARAMS params; //For getting all params for the CPU!
	byte MODRM_src0; //What source is our modr/m? (1/2)
	byte MODRM_src1; //What source is our modr/m? (1/2)

	//Immediate data read for execution!
	byte immb; //For CPU_readOP result!
	word immw; //For CPU_readOPw result!
	uint_32 imm32; //For CPU_readOPdw result!
	uint_64 imm64; //For CPU_readOPdw x2 result!
	uint_32 immaddr32; //Immediate address, for instructions requiring it, either 16-bits or 32-bits of immediate data, depending on the address size!

	//ModR/M information!
	//Opcode&Stack sizes: 0=16-bits, 1=32-bits!
	byte CPU_Operand_size; //Operand size for this opcode!
	byte CPU_Address_size; //Address size for this opcode!

	//Internal prefix table for below functions!
	byte CPU_prefixes[32]; //All prefixes, packed in a bitfield!
	byte OPbuffer[256]; //A large opcode buffer!
	word OPlength; //The length of the opcode buffer!
	uint_32 oldCR0;
	byte NMIMasked; //Are NMI masked?
	word IRET_IP, IRET_CS, IRET_FLAGS;
	byte newREP; //Are we a new repeating instruction (REP issued?) Default = 1!
	byte MOVSB_data;
	word MOVSW_data;
	uint_32 MOVSD_data;
	byte counter;
	char modrm_param1[256]; //Contains param/reg1
	char modrm_param2[256]; //Contains param/reg2
	word modrm_lastsegment;
	uint_32 modrm_lastoffset;
	byte last_modrm; //Is the last opcode a modr/m read?
	int_32 modrm_addoffset; //To add this to the calculated offset!
	byte LODSB_value;
	word LODSW_value;
	uint_32 LODSD_value;

	word LOADDESCRIPTOR_segmentval;
	uint_32 loadall386loader[0x33]; //Loader data!
	LOADALL386DATATYPE LOADALL386DATA;
	word loadall286loader[0x33]; //Word-sized data to be loaded, if any!
	LOADALL286DATATYPE LOADALL286DATA;

	char LEAtext[256];

	uint_32 last_eip;
	byte ismultiprefix; //Are we multi-prefix?

	word INTreturn_CS;
	uint_32 INTreturn_EIP;

	byte CPU_executionphaseinterrupt_nr; //What interrupt to execute?
	byte CPU_executionphaseinterrupt_type; //Are we a type3 interrupt(bit0) or external interrupt(bit1)?
	int_64 CPU_executionphaseinterrupt_errorcode; //What code to push afterwards? Default: -1!
	byte CPU_executionphaseinterrupt_is_interrupt; //int instruction?
	byte interrupt_result;

	byte instructionbufferb, instructionbufferb2; //For 8-bit read storage!
	word instructionbufferw, instructionbufferw2; //For 16-bit read storage!
	uint_32 instructionbufferd, instructionbufferd2; //For 16-bit read storage!

	MODRM_PTR info, info2; //For storing ModR/M Info(second for 186+ IMUL instructions)!
	
	uint_32 IMULresult; //Buffer to use, general purpose!

	//Stuff for CPU 286+ timing processing!
	byte BST_cnt; //How many of bit scan/test (forward) times are taken?
	byte protection_PortRightsLookedup; //Are the port rights looked up?
	byte didJump; //Did we jump this instruction?
	byte ENTER_L; //Level value of the ENTER instruction!
	byte hascallinterrupttaken_type; //INT gate type taken. Low 4 bits are the type. High 2 bits are privilege level/task gate flag. Left at 0xFF when nothing is used(unknown case?). Default = 0xFF
	byte CPU_interruptraised; //Interrupt raised flag?

	word destINTCS, destINTIP;
	uint_32 destEIP; //Destination address for CS JMP instruction!
	CPU_OpcodeInformation* currentOpcodeInformation; //The timing used for the current instruction!
	Handler currentOP_handler; //Default = &CPU_unkOP
	Handler currentEUphasehandler; //Current execution phase handler, start of with none loaded yet!

	byte CPU_MMU_checkrights_cause; //What cause?

	word CPU_debugger_CS; //OPCode CS
	uint_32 CPU_debugger_EIP; //OPCode EIP
	byte blockREP; //Block the instruction from executing (REP with (E)CX=0

	byte CMPSB_data1, CMPSB_data2;
	word CMPSW_data1, CMPSW_data2;
	uint_32 CMPSD_data1, CMPSD_data2;

	uint_32 CALLGATE_NUMARGUMENTS; //The amount of arguments of the call gate!
	byte calledinterruptnumber; //Called interrupt number for unkint funcs!
	byte custommem; //Custom memory address used?
	uint_32 customoffset; //What custom memory address used!
	byte taskswitch_result; //Result of a task switch!
	uint_32 wordaddress;
	byte cpudebugger; //To debug the CPU?
	byte data8; //For string instructions!
	word data16; //For string instructions!
	uint_32 data32; //For string instructions!
	//Enter instruction data!
	word frametempw;
	uint_32 frametempd;
	word bpdataw;
	uint_32 bpdatad;
	word POPF_tempflags;
	word ARPL_destRPL, ARPL_srcRPL;
	word LXS_segment;
	word LXS_offsetw;
	uint_32 LXS_offsetd;
	word destCS;
	word PUSHA_oldSP;
	uint_32 PUSHAD_oldESP;
	uint_32 newpreviousCSstart;
	char debugtext[256]; //Debug text!
	byte OP; //Currenltly executing opcode!
	word oldvalw; //For stack accesses!
	uint_32 oldvald; //For stack accesses!
	word tempflagsw;
	uint_32 tempflagsd;
	word value8F_16;
	uint_32 value8F_32;
	uint_32 bound_min32;
	uint_32 bound_max32;
	uint_32 boundval32;
	word bound_min16;
	word bound_max16;
	word boundval16;
	uint_64 PageFault_PDPT;
	uint_64 PageFault_PDE;
	uint_64 PageFault_PTE;
	uint_32 enter_finalESP;
	byte segmentWritten_instructionrunning; //segmentWritten running within an instruction?
	uint_32 taskswitch_stepping; //Currently completed steps during a task switch.
	struct
	{
		byte TSS_dirty; //Is the new TSS dirty?
		TSS286 TSS16;
		TSS386 TSS32;
		byte TSSSizeSrc, TSSSize; //The (source) TSS size!
		sbyte loadresult;
		word oldtask;
		word LDTsegment;
	} taskswitchdata;
} CPU_type;

#ifndef IS_CPU
extern byte activeCPU; //That currently active CPU!
extern byte emulated_CPUtype; //The emulated CPU processor type!
extern CPU_type CPU[MAXCPUS]; //All CPUs itself!
#endif

//Overrides:

//Lock prefix
#define CPU_PREFIX_LOCK 0xF0
//REPNE, REPNZ prefix
#define CPU_PREFIX_REPNEZ 0xF2
//REP, REPZ, REPE prefix
#define CPU_PREFIX_REPZPE 0xF3
//Segment overrides:
#define CPU_PREFIX_CS 0x2E
#define CPU_PREFIX_SS 0x36
#define CPU_PREFIX_DS 0x3E
#define CPU_PREFIX_ES 0x26
#define CPU_PREFIX_FS 0x64
#define CPU_PREFIX_GS 0x65
//Operand override:
#define CPU_PREFIX_OP 0x66
//Address size override:
#define CPU_PREFIX_ADDR 0x67

//CPU Modes:
//Real mode has no special stuff
#define CPU_MODE_REAL 0
//Protected mode has bit 1 set
#define CPU_MODE_PROTECTED 1
//Virtual 8086 mode has bit 1(Protected mode) and bit 2(Virtual override) set
#define CPU_MODE_8086 3
//Virtual override without Protected mode has no effect: it's real mode after all!
#define CPU_MODE_UNKNOWN 2

//Exception interrupt numbers!
#define EXCEPTION_DIVIDEERROR 0
#define EXCEPTION_DEBUG 1
#define EXCEPTION_NMI 2
#define EXCEPTION_CPUBREAKPOINT 3
#define EXCEPTION_OVERFLOW 4
#define EXCEPTION_BOUNDSCHECK 5
#define EXCEPTION_INVALIDOPCODE 6
#define EXCEPTION_COPROCESSORNOTAVAILABLE 7
#define EXCEPTION_DOUBLEFAULT 8
#define EXCEPTION_COPROCESSOROVERRUN 9
#define EXCEPTION_INVALIDTSSSEGMENT 0xA
#define EXCEPTION_SEGMENTNOTPRESENT 0xB
#define EXCEPTION_STACKFAULT 0xC
#define EXCEPTION_GENERALPROTECTIONFAULT 0xD
#define EXCEPTION_PAGEFAULT 0xE
#define EXCEPTION_COPROCESSORERROR 0x10
#define EXCEPTION_ALIGNMENTCHECK 0x11

#define EXCEPTION_TABLE_GDT 0x00
#define EXCEPTION_TABLE_IDT 0x01
#define EXCEPTION_TABLE_LDT 0x02
//0x03 seems to be an alias of 0x01?

void initCPU(); //Initialize CPU for full system reset into known state!
void resetCPU(word isInit); //Initialises CPU!
void doneCPU(); //Finish the CPU!
void CPU_resetMode(); //Reset the mode to the default mode! (see above)
byte CPU_getprefix(byte prefix); //Prefix set? (might be used by OPcodes!)
void CPU_setprefix(byte prefix); //Sets a prefix on!
byte getcpumode(); //Get current CPU mode (see CPU modes above!)

void CPU_resetOP(); //Rerun current Opcode? (From interrupt calls this recalls the interrupts, handling external calls in between)

//CPU executing functions:

void CPU_beforeexec(); //Everything before the execution of the current CPU OPcode!
void CPU_exec(); //Run one CPU OPCode!
//void CPU_exec_DEBUGGER(); //Processes the opcode at CS:EIP (386) or CS:IP (8086) for debugging.

//Sign extension!
#define SIGNEXTEND_16(x) (sword)x
#define SIGNEXTEND_32(x) (int_32)x

//Adress for booting: default is physical adress 0x7C00!
#define BOOT_SEGMENT 0x0000
#define BOOT_OFFSET 0x7C00


word CPU_segment(byte defaultsegment); //Plain segment to use (Plain and overrides)!
char *CPU_textsegment(byte defaultsegment); //Plain segment to use (text)!
char* CPU_segmentname(byte segment); //Plain segment to use!

byte call_soft_inthandler(byte intnr, int_64 errorcode, byte is_interrupt); //Software interrupt handler (FROM software interrupts only (int>=0x20 for software call from Handler))!
void call_hard_inthandler(byte intnr); //Software interrupt handler (FROM hardware only)!
void CPU_prepareHWint(); //Prepares the CPU for hardware interrupts!


word *CPU_segment_ptr(byte defaultsegment); //Plain segment to use, direct access!

void copyint(byte src, byte dest); //Copy interrupt handler pointer to different interrupt!

#define signext(value) ((((word)value&0x80)*0x1FE)|(word)value)
#define signext32(value) ((((uint_32)value&0x8000)*0x1FFFE)|(uint_32)value)

//Software access with protection!
#define CPUPROT1 if(likely(CPU[activeCPU].faultraised==0)){
#define CPUPROT2 }

#include "headers/cpu/interrupts.h" //Real interrupts!

//Extra:

#include "headers/cpu/modrm.h" //MODR/M comp!

//Read signed numbers from CS:(E)IP!
#define imm8() unsigned2signed8(CPU[activeCPU].immb)
#define imm16() unsigned2signed16(CPU[activeCPU].immw)
#define imm32s() unsigned2signed32(CPU[activeCPU].imm32)

//Exceptions!

//8086+ CPU triggered exceptions (real mode)

void CPU_exDIV0(); //Division by 0!
void CPU_exSingleStep(); //Single step (after the opcode only)
void CPU_BoundException(); //Bound exception!
void CPU_COOP_notavailable(); //COProcessor not available!
void THROWDESCNM(); //#NM exception handler!
void THROWDESCMF(); //#MF exception handler!

void CPU_getint(byte intnr, word *segment, word *offset); //Set real mode IVT entry!

void generate_opcode_jmptbl(); //Generate the current CPU opcode jmptbl!
void generate_opcode0F_jmptbl(); //Generate thr current CPU opcode 0F jmptbl!
void updateCPUmode(); //Update the CPU mode!

byte CPU_segmentOverridden(byte activeCPU);
//specialReset: 1 for exhibiting bug and flushing PIQ, 0 otherwise
void CPU_8086REPPending(byte doReset); //Execute this before CPU_exec!

byte CPU_handleNMI(byte isHLT); //Did we not handle an NMI?
byte CPU_checkNMIAPIC(byte isHLT); //Check for APIC NMI to fire?
byte execNMI(byte causeisMemory); //Execute an NMI!

void CPU_unkOP(); //General unknown OPcode handler!

//Port I/O by the emulated CPU itself!
byte CPU_PORT_OUT_B(word base, word port, byte data);
byte CPU_PORT_OUT_W(word base, word port, word data);
byte CPU_PORT_OUT_D(word base, word port, uint_32 data);
byte CPU_PORT_IN_B(word base, word port, byte *result);
byte CPU_PORT_IN_W(word base, word port, word *result);
byte CPU_PORT_IN_D(word base, word port, uint_32 *result);

byte isPM(); //Are we in protected mode?
byte isV86(); //Are we in Virtual 8086 mode?

//ModR/M debugger support!
byte NumberOfSetBits(uint_32 i); //Number of bits set in this variable!

void CPU_resetTimings(); //Reset timings before processing the next CPU state!
void CPU_interruptcomplete(); //What to do when an interrupt is completed!

void CPU_JMPrel(int_32 reladdr, byte useAddressSize);
void CPU_JMPabs(uint_32 addr, byte useAddressSize);
uint_32 CPU_EIPmask(byte useAddressSize);
byte CPU_EIPSize(byte useAddressSize);

void CPU_tickPendingReset(); //Tick a pending CPU reset!
byte BIU_resetRequested(); //Reset requested?

//Memory access functionality, supporting Paging, Direct access and Segmented access!
byte CPU_request_MMUrb(sword segdesc, uint_32 offset, byte is_offset16);
byte CPU_request_MMUrw(sword segdesc, uint_32 offset, byte is_offset16);
byte CPU_request_MMUrdw(sword segdesc, uint_32 offset, byte is_offset16);
byte CPU_request_MMUwb(sword segdesc, uint_32 offset, byte val, byte is_offset16);
byte CPU_request_MMUww(sword segdesc, uint_32 offset, word val, byte is_offset16);
byte CPU_request_MMUwdw(sword segdesc, uint_32 offset, uint_32 val, byte is_offset16);

byte checkSignedOverflow(uint_64 unsignedval, byte calculatedbits, byte bits, byte convertedtopositive); //Is there a signed overflow?
void CPU_CIMUL(uint_32 base, byte basesize, uint_32 multiplicant, byte multiplicantsize, uint_32 *result, byte resultsize); //IMUL instruction support for fixed size IMUL(not GRP opcodes)!
void CPU_CPUID(); //Common CPUID instruction!

#endif
