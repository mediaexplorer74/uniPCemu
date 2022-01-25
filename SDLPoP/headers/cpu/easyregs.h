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

#ifndef CPU_EASYREGS_H
#define CPU_EASYREGS_H

#ifndef parity
extern byte parity[0x100]; //Our parity table!
#endif

//First for parity calculations:
#define PARITY8(b) parity[b]
#define PARITY16(w) parity[w&0xFF]
#define PARITY32(dw) parity[dw&0xFF]

//Accumulator register:
#define REG_AL REG8_LO(GPREG_EAX)
#define REG_AH REG8_HI(GPREG_EAX)
#define REG_EAX REG32(GPREG_EAX)
#define REG_AX REG16_LO(GPREG_EAX)

//Base register:
#define REG_BL REG8_LO(GPREG_EBX)
#define REG_BH REG8_HI(GPREG_EBX)
#define REG_EBX REG32(GPREG_EBX)
#define REG_BX REG16_LO(GPREG_EBX)

//Counter register:
#define REG_CL REG8_LO(GPREG_ECX)
#define REG_CH REG8_HI(GPREG_ECX)
#define REG_ECX REG32(GPREG_ECX)
#define REG_CX REG16_LO(GPREG_ECX)

//Data register:
#define REG_DL REG8_LO(GPREG_EDX)
#define REG_DH REG8_HI(GPREG_EDX)
#define REG_EDX REG32(GPREG_EDX)
#define REG_DX REG16_LO(GPREG_EDX)

//Segment registers
#define REG_CS REG16_LO(SREG_CS)
#define REG_DS REG16_LO(SREG_DS)
#define REG_ES REG16_LO(SREG_ES)
#define REG_FS REG16_LO(SREG_FS)
#define REG_GS REG16_LO(SREG_GS)
#define REG_SS REG16_LO(SREG_SS)
#define REG_TR REG16_LO(SREG_TR)
#define REG_LDTR REG16_LO(SREG_LDTR)

//Indexes and pointers
#define REG_EDI REG32(GPREG_EDI)
#define REG_DI REG16_LO(GPREG_EDI)
#define REG_ESI REG32(GPREG_ESI)
#define REG_SI REG16_LO(GPREG_ESI)
#define REG_EBP REG32(GPREG_EBP)
#define REG_BP REG16_LO(GPREG_EBP)
#define REG_ESP REG32(GPREG_ESP)
#define REG_SP REG16_LO(GPREG_ESP)
#define REG_EIP REG32(GPREG_EIP)
#define REG_IP REG16_LO(GPREG_EIP)
#define REG_EFLAGS REG32(GPREG_EFLAGS)
#define REG_FLAGS REG16_LO(GPREG_EFLAGS)

//Flags(read version default)
#define FLAG_AC FLAGREGR_AC(CPU[activeCPU].registers)
#define FLAG_V8 FLAGREGR_V8(CPU[activeCPU].registers)
#define FLAG_RF FLAGREGR_RF(CPU[activeCPU].registers)
#define FLAG_NT FLAGREGR_NT(CPU[activeCPU].registers)
#define FLAG_PL FLAGREGR_IOPL(CPU[activeCPU].registers)
#define FLAG_OF FLAGREGR_OF(CPU[activeCPU].registers)
#define FLAG_DF FLAGREGR_DF(CPU[activeCPU].registers)
#define FLAG_IF FLAGREGR_IF(CPU[activeCPU].registers)
#define FLAG_TF FLAGREGR_TF(CPU[activeCPU].registers)
#define FLAG_SF FLAGREGR_SF(CPU[activeCPU].registers)
#define FLAG_ZF FLAGREGR_ZF(CPU[activeCPU].registers)
#define FLAG_AF FLAGREGR_AF(CPU[activeCPU].registers)
#define FLAG_PF FLAGREGR_PF(CPU[activeCPU].registers)
#define FLAG_CF FLAGREGR_CF(CPU[activeCPU].registers)
#define FLAG_VIF FLAGREGR_VIF(CPU[activeCPU].registers)
#define FLAG_VIP FLAGREGR_VIP(CPU[activeCPU].registers)

//Flags(write version default)
#define FLAGW_AC(val) FLAGREGW_AC(CPU[activeCPU].registers,val)
#define FLAGW_V8(val) FLAGREGW_V8(CPU[activeCPU].registers,val)
#define FLAGW_RF(val) FLAGREGW_RF(CPU[activeCPU].registers,val)
#define FLAGW_NT(val) FLAGREGW_NT(CPU[activeCPU].registers,val)
#define FLAGW_PL(val) FLAGREGW_IOPL(CPU[activeCPU].registers,val)
#define FLAGW_OF(val) FLAGREGW_OF(CPU[activeCPU].registers,val)
#define FLAGW_DF(val) FLAGREGW_DF(CPU[activeCPU].registers,val)
#define FLAGW_IF(val) FLAGREGW_IF(CPU[activeCPU].registers,val)
#define FLAGW_TF(val) FLAGREGW_TF(CPU[activeCPU].registers,val)
#define FLAGW_SF(val) FLAGREGW_SF(CPU[activeCPU].registers,val)
#define FLAGW_ZF(val) FLAGREGW_ZF(CPU[activeCPU].registers,val)
#define FLAGW_AF(val) FLAGREGW_AF(CPU[activeCPU].registers,val)
#define FLAGW_PF(val) FLAGREGW_PF(CPU[activeCPU].registers,val)
#define FLAGW_CF(val) FLAGREGW_CF(CPU[activeCPU].registers,val)
#define FLAGW_VIF(val) FLAGREGW_VIF(CPU[activeCPU].registers,val)
#define FLAGW_VIP(val) FLAGREGW_VIP(CPU[activeCPU].registers,val)

#endif
