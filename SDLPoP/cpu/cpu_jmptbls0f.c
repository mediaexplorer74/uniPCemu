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

#include "headers/cpu/cpu.h" //Need basic CPU support!
#include "headers/cpu/cpu_OP8086.h" //Unknown opcodes under 8086!
#include "headers/cpu/cpu_OPNECV30.h" //Unknown opcodes under NECV30+ and more!
#include "headers/cpu/cpu_OP80286.h" //Unknown opcodes under 80286+ and more!
#include "headers/cpu/cpu_OP80386.h" //Unknown opcodes under 80386+ and more!
#include "headers/cpu/cpu_OP80486.h" //Unknown opcodes under 80486+ and more!
#include "headers/cpu/cpu_OP80586.h" //Unknown opcodes under 80586+ and more!
#include "headers/cpu/cpu_OP80686.h" //Unknown opcodes under 80686+ and more!
#include "headers/cpu/cpu_OP80786.h" //Unknown opcodes under 80786+ and more!

//See normal opcode table, but for 0F opcodes!
Handler opcode0F_jmptbl[NUM0FEXTS][256][2] =   //Our standard internal standard interrupt jmptbl!
{
	//80286+
	{
//0x00:
		{CPU286_OP0F00,NULL}, //00h:
		{CPU286_OP0F01,NULL}, //01h
		{CPU286_OP0F02,NULL}, //02h:
		{CPU286_OP0F03,NULL}, //03h:
		{unkOP0F_286,NULL}, //04h:
		{CPU286_OP0F05,NULL}, //05h:
		{CPU286_OP0F06,NULL}, //06h:
		{unkOP0F_286,NULL}, //07h:
		{unkOP0F_286,NULL}, //08h:
		{unkOP0F_286,NULL}, //09h:
		{unkOP0F_286,NULL}, //0Ah:
		{CPU286_OP0F0B,NULL}, //0Bh:
		{unkOP0F_286,NULL}, //0Ch:
		{unkOP0F_286,NULL}, //0Dh:
		{unkOP0F_286,NULL}, //0Eh:
		{unkOP0F_286,NULL}, //0Fh:
//0x10:
		{unkOP0F_286,NULL}, //10h: video interrupt
		{unkOP0F_286,NULL}, //11h:
		{unkOP0F_286,NULL}, //12h:
		{unkOP0F_286,NULL}, //13h: I/O for HDD/Floppy disks
		{unkOP0F_286,NULL}, //14h:
		{unkOP0F_286,NULL}, //15h:
		{unkOP0F_286,NULL}, //16h:
		{unkOP0F_286,NULL}, //17h:
		{unkOP0F_286,NULL}, //18h:
		{unkOP0F_286,NULL}, //19h:
		{unkOP0F_286,NULL}, //1Ah:
		{unkOP0F_286,NULL}, //1Bh:
		{unkOP0F_286,NULL}, //1Ch:
		{unkOP0F_286,NULL}, //1Dh:
		{unkOP0F_286,NULL}, //1Eh:
		{unkOP0F_286,NULL}, //1Fh:
//0x20:
		{unkOP0F_286,NULL}, //20h:
		{unkOP0F_286,NULL}, //21h: DOS interrupt
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
//0x30:
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
//0x40:
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
//0x50:
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
//0x60:
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
//0x70:
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
//0x80:
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
//0x90:
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
//0xA0:
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
//0xB0:
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{CPU286_OP0FB9,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
//0xC0:
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
//0xD0:
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
//0xE0:
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
//0xF0:
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL},
		{unkOP0F_286,NULL}
	},

	//80386
	{
	//0x00:
	{ CPU386_OP0F00, CPU386_OP0F00 }, //00h:
	{ CPU386_OP0F01, CPU386_OP0F01 }, //01h
	{ NULL, CPU386_OP0F02 }, //02h:
	{ NULL, CPU386_OP0F03 }, //03h:
	{ NULL, NULL }, //04h:
	{ unkOP0F_386, NULL }, //05h: 286-only LOADALL doesn't exist anymore on a 386!
	{ NULL, NULL }, //06h:
	{ CPU386_OP0F07, NULL }, //07h: Undocumented 80386-only LOADALL instruction!
	{ NULL, NULL }, //08h:
	{ NULL, NULL }, //09h:
	{ NULL, NULL }, //0Ah:
	{ NULL, NULL }, //0Bh:
	{ NULL, NULL }, //0Ch:
	{ NULL, NULL }, //0Dh:
	{ NULL, NULL }, //0Eh:
	{ NULL, NULL }, //0Fh:
	//0x10:
	{ NULL, NULL }, //10h:
	{ NULL, NULL }, //11h:
	{ NULL, NULL }, //12h:
	{ NULL, NULL }, //13h:
	{ NULL, NULL }, //14h:
	{ NULL, NULL }, //15h:
	{ NULL, NULL }, //16h:
	{ NULL, NULL }, //17h:
	{ NULL, NULL }, //18h:
	{ NULL, NULL }, //19h:
	{ NULL, NULL }, //1Ah:
	{ NULL, NULL }, //1Bh:
	{ NULL, NULL }, //1Ch:
	{ NULL, NULL }, //1Dh:
	{ NULL, NULL }, //1Eh:
	{ NULL, NULL }, //1Fh:
	//0x20:
	{ CPU80386_OP0F_MOVCRn_modrmmodrm, NULL },
	{ CPU80386_OP0F_MOVDRn_modrmmodrm, NULL },
	{ CPU80386_OP0F_MOVCRn_modrmmodrm, NULL },
	{ CPU80386_OP0F_MOVDRn_modrmmodrm, NULL },
	{ CPU80386_OP0F_MOVTRn_modrmmodrm, NULL },
	{ NULL, NULL },
	{ CPU80386_OP0F_MOVTRn_modrmmodrm, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	//0x30:
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	//0x40:
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	//0x50:
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	//0x60:
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	//0x70:
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	//0x80:
	{ CPU80386_OP0F80_16, CPU80386_OP0F80_32 },
	{ CPU80386_OP0F81_16, CPU80386_OP0F81_32 },
	{ CPU80386_OP0F82_16, CPU80386_OP0F82_32 },
	{ CPU80386_OP0F83_16, CPU80386_OP0F83_32 },
	{ CPU80386_OP0F84_16, CPU80386_OP0F84_32 },
	{ CPU80386_OP0F85_16, CPU80386_OP0F85_32 },
	{ CPU80386_OP0F86_16, CPU80386_OP0F86_32 },
	{ CPU80386_OP0F87_16, CPU80386_OP0F87_32 },
	{ CPU80386_OP0F88_16, CPU80386_OP0F88_32 },
	{ CPU80386_OP0F89_16, CPU80386_OP0F89_32 },
	{ CPU80386_OP0F8A_16, CPU80386_OP0F8A_32 },
	{ CPU80386_OP0F8B_16, CPU80386_OP0F8B_32 },
	{ CPU80386_OP0F8C_16, CPU80386_OP0F8C_32 },
	{ CPU80386_OP0F8D_16, CPU80386_OP0F8D_32 },
	{ CPU80386_OP0F8E_16, CPU80386_OP0F8E_32 },
	{ CPU80386_OP0F8F_16, CPU80386_OP0F8F_32 },
	//0x90:
	{ CPU80386_OP0F90, NULL },
	{ CPU80386_OP0F91, NULL },
	{ CPU80386_OP0F92, NULL },
	{ CPU80386_OP0F93, NULL },
	{ CPU80386_OP0F94, NULL },
	{ CPU80386_OP0F95, NULL },
	{ CPU80386_OP0F96, NULL },
	{ CPU80386_OP0F97, NULL },
	{ CPU80386_OP0F98, NULL },
	{ CPU80386_OP0F99, NULL },
	{ CPU80386_OP0F9A, NULL },
	{ CPU80386_OP0F9B, NULL },
	{ CPU80386_OP0F9C, NULL },
	{ CPU80386_OP0F9D, NULL },
	{ CPU80386_OP0F9E, NULL },
	{ CPU80386_OP0F9F, NULL },
	//0xA0:
	{ CPU80386_OP0FA0, NULL },
	{ CPU80386_OP0FA1, NULL },
	{ NULL, NULL },
	{ CPU80386_OP0FA3_16, CPU80386_OP0FA3_32 },
	{ CPU80386_OP0FA4_16, CPU80386_OP0FA4_32 },
	{ CPU80386_OP0FA5_16, CPU80386_OP0FA5_32 },
	{ NULL, NULL },
	{ NULL, NULL },
	{ CPU80386_OP0FA8, NULL },
	{ CPU80386_OP0FA9, NULL },
	{ NULL, NULL },
	{ CPU80386_OP0FAB_16, CPU80386_OP0FAB_32 },
	{ CPU80386_OP0FAC_16, CPU80386_OP0FAC_32 },
	{ CPU80386_OP0FAD_16, CPU80386_OP0FAD_32 },
	{ NULL, NULL },
	{ CPU80386_OP0FAF_16, CPU80386_OP0FAF_32 },
	//0xB0:
	{ NULL, NULL },
	{ NULL, NULL },
	{ CPU80386_OP0FB2_16, CPU80386_OP0FB2_32 },
	{ CPU80386_OP0FB3_16, CPU80386_OP0FB3_32 },
	{ CPU80386_OP0FB4_16, CPU80386_OP0FB4_32 },
	{ CPU80386_OP0FB5_16, CPU80386_OP0FB5_32 },
	{ CPU80386_OP0FB6_16, CPU80386_OP0FB6_32 },
	{ CPU80386_OP0FB7_16, CPU80386_OP0FB7_32 },
	{ NULL, NULL },
	{ NULL, NULL },
	{ CPU80386_OP0FBA_16, CPU80386_OP0FBA_32 },
	{ CPU80386_OP0FBB_16, CPU80386_OP0FBB_32 },
	{ CPU80386_OP0FBC_16, CPU80386_OP0FBC_32 },
	{ CPU80386_OP0FBD_16, CPU80386_OP0FBD_32 },
	{ CPU80386_OP0FBE_16, CPU80386_OP0FBE_32 },
	{ CPU80386_OP0FBF_16, CPU80386_OP0FBF_32 },
	//0xC0:
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	//0xD0:
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	//0xE0:
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	//0xF0:
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL },
	{ NULL, NULL }
	},

	//80486
	{
		//0x00:
		{ NULL, NULL }, //00h:
		{ CPU486_OP0F01_16, CPU486_OP0F01_32 }, //01h
		{ NULL, NULL }, //02h:
		{ NULL, NULL }, //03h:
		{ NULL, NULL }, //04h:
		{ NULL, NULL }, //05h:
		{ NULL, NULL }, //06h:
		{ unkOP0F_486, NULL }, //07h: LOADALL doesn't exist on 80486+ anymore!
		{ CPU486_OP0F08, NULL }, //08h:
		{ CPU486_OP0F09, NULL }, //09h:
		{ NULL, NULL }, //0Ah:
		{ NULL, NULL }, //0Bh:
		{ NULL, NULL }, //0Ch:
		{ NULL, NULL }, //0Dh:
		{ NULL, NULL }, //0Eh:
		{ NULL, NULL }, //0Fh:
		//0x10:
		{ NULL, NULL }, //10h:
		{ NULL, NULL }, //11h:
		{ NULL, NULL }, //12h:
		{ NULL, NULL }, //13h:
		{ NULL, NULL }, //14h:
		{ NULL, NULL }, //15h:
		{ NULL, NULL }, //16h:
		{ NULL, NULL }, //17h:
		{ NULL, NULL }, //18h:
		{ NULL, NULL }, //19h:
		{ NULL, NULL }, //1Ah:
		{ NULL, NULL }, //1Bh:
		{ NULL, NULL }, //1Ch:
		{ NULL, NULL }, //1Dh:
		{ NULL, NULL }, //1Eh:
		{ NULL, NULL }, //1Fh:
		//0x20:
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		//0x30:
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		//0x40:
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		//0x50:
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		//0x60:
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		//0x70:
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		//0x80:
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		//0x90:
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		//0xA0:
		{ NULL, NULL },
		{ NULL, NULL },
		{ CPU486_CPUID, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		//0xB0:
		{ CPU486_OP0FB0, NULL },
		{ CPU486_OP0FB1_16, CPU486_OP0FB1_32 },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		//0xC0:
		{ CPU486_OP0FC0, NULL },
		{ CPU486_OP0FC1_16, CPU486_OP0FC1_32 },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ CPU486_OP0FC8_16, CPU486_OP0FC8_32 },
		{ CPU486_OP0FC9_16, CPU486_OP0FC9_32 },
		{ CPU486_OP0FCA_16, CPU486_OP0FCA_32 },
		{ CPU486_OP0FCB_16, CPU486_OP0FCB_32 },
		{ CPU486_OP0FCC_16, CPU486_OP0FCC_32 },
		{ CPU486_OP0FCD_16, CPU486_OP0FCD_32 },
		{ CPU486_OP0FCE_16, CPU486_OP0FCE_32 },
		{ CPU486_OP0FCF_16, CPU486_OP0FCF_32 },
		//0xD0:
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		//0xE0:
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		//0xF0:
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL }
	},

	//PENTIUM (80586)
	{
		//0x00:
		{ NULL, NULL }, //00h:
		{ NULL, NULL }, //01h
		{ NULL, NULL }, //02h:
		{ NULL, NULL }, //03h:
		{ NULL, NULL }, //04h:
		{ NULL, NULL }, //05h:
		{ NULL, NULL }, //06h:
		{ NULL, NULL }, //07h:
		{ NULL, NULL }, //08h:
		{ NULL, NULL }, //09h:
		{ NULL, NULL }, //0Ah:
		{ NULL, NULL }, //0Bh:
		{ NULL, NULL }, //0Ch:
		{ NULL, NULL }, //0Dh:
		{ NULL, NULL }, //0Eh:
		{ NULL, NULL }, //0Fh:
		//0x10:
		{ NULL, NULL }, //10h:
		{ NULL, NULL }, //11h:
		{ NULL, NULL }, //12h:
		{ NULL, NULL }, //13h:
		{ NULL, NULL }, //14h:
		{ NULL, NULL }, //15h:
		{ NULL, NULL }, //16h:
		{ NULL, NULL }, //17h:
		{ NULL, NULL }, //18h:
		{ NULL, NULL }, //19h:
		{ NULL, NULL }, //1Ah:
		{ NULL, NULL }, //1Bh:
		{ NULL, NULL }, //1Ch:
		{ NULL, NULL }, //1Dh:
		{ NULL, NULL }, //1Eh:
		{ NULL, NULL }, //1Fh:
		//0x20:
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ unkOP0F_586, NULL }, //24h: Test register instructions become invalid!
		{ NULL, NULL },
		{ unkOP0F_586, NULL }, //26h: Test register instructions become invalid!
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		//0x30:
		{ CPU586_OP0F30, NULL },
		{ CPU586_OP0F31, NULL },
		{ CPU586_OP0F32, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		//0x40:
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		//0x50:
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		//0x60:
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		//0x70:
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		//0x80:
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		//0x90:
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		//0xA0:
		{ NULL, NULL },
		{ NULL, NULL },
		{ CPU586_CPUID, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		//0xB0:
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		//0xC0:
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ CPU586_OP0FC7, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		//0xD0:
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		//0xE0:
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		//0xF0:
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL }
	},

	//80686(Pentium Pro)
	{
		//0x00:
		{ NULL, NULL }, //00h:
		{ NULL, NULL }, //01h:
		{ NULL, NULL }, //02h:
		{ NULL, NULL }, //03h:
		{ NULL, NULL }, //04h:
		{ NULL, NULL }, //05h:
		{ NULL, NULL }, //06h:
		{ NULL, NULL }, //07h:
		{ NULL, NULL }, //08h:
		{ NULL, NULL }, //09h:
		{ NULL, NULL }, //0Ah:
		{ NULL, NULL }, //0Bh:
		{ NULL, NULL }, //0Ch:
		{ CPU686_OP0F0D_16, CPU686_OP0F0D_32 }, //0Dh: NOP r/m16/32
		{ NULL, NULL }, //0Eh:
		{ NULL, NULL }, //0Fh: NECV30 specific OPcode!
		//0x10:
		{ NULL, NULL }, //10h:
		{ NULL, NULL }, //11h:
		{ NULL, NULL }, //12h:
		{ NULL, NULL }, //13h:
		{ NULL, NULL }, //14h:
		{ NULL, NULL }, //15h:
		{ NULL, NULL }, //16h:
		{ NULL, NULL }, //17h:
		{ CPU686_OP0F18_16, CPU686_OP0F18_32 }, //18h: HINT_NOP /4-7 r/m16/32
		{ CPU686_OP0FNOP_16, CPU686_OP0FNOP_32 }, //19h: HINT_NOP r/m16/32
		{ CPU686_OP0FNOP_16, CPU686_OP0FNOP_32 }, //1Ah: HINT_NOP r/m16/32
		{ CPU686_OP0FNOP_16, CPU686_OP0FNOP_32 }, //1Bh: HINT_NOP r/m16/32
		{ CPU686_OP0FNOP_16, CPU686_OP0FNOP_32 }, //1Ch: HINT_NOP r/m16/32
		{ CPU686_OP0FNOP_16, CPU686_OP0FNOP_32 }, //1Dh: HINT_NOP r/m16/32
		{ CPU686_OP0FNOP_16, CPU686_OP0FNOP_32 }, //1Eh: HINT_NOP r/m16/32
		{ CPU686_OP0F1F_16, CPU686_OP0F1F_32 }, //1Fh: HINT_NOP /1-7 r/m16
		//0x20:
		{ NULL, NULL }, //20h:
		{ NULL, NULL }, //21h:
		{ NULL, NULL }, //22h:
		{ NULL, NULL }, //23h:
		{ NULL, NULL }, //24h:
		{ NULL, NULL }, //25h:
		{ NULL, NULL }, //26h: Special
		{ NULL, NULL }, //27h:
		{ NULL, NULL }, //28h:
		{ NULL, NULL }, //29h:
		{ NULL, NULL }, //2Ah:
		{ NULL, NULL }, //2Bh:
		{ NULL, NULL }, //2Ch:
		{ NULL, NULL }, //2Dh:
		{ NULL, NULL }, //2Eh: Special
		{ NULL, NULL }, //2Fh:
		//0x30:
		{ NULL, NULL }, //30h:
		{ NULL, NULL }, //31h:
		{ NULL, NULL }, //32h:
		{ CPU686_OP0F33, NULL }, //33h:
		{ NULL, NULL }, //34h:
		{ NULL, NULL }, //35h:
		{ NULL, NULL }, //36h: Special
		{ NULL, NULL }, //37h:
		{ NULL, NULL }, //38h:
		{ NULL, NULL }, //39h:
		{ NULL, NULL }, //3Ah:
		{ NULL, NULL }, //3Bh:
		{ NULL, NULL }, //3Ch:
		{ NULL, NULL }, //3Dh:
		{ NULL, NULL }, //3Eh: Special
		{ NULL, NULL }, //3Fh:
		//0x40:
		{ CPU686_OP0F40_16, CPU686_OP0F40_32 }, //40h: 
		{ CPU686_OP0F41_16, CPU686_OP0F41_32 }, //41h: 
		{ CPU686_OP0F42_16, CPU686_OP0F42_32 }, //42h: 
		{ CPU686_OP0F43_16, CPU686_OP0F43_32 }, //43h: 
		{ CPU686_OP0F44_16, CPU686_OP0F44_32 }, //44h: 
		{ CPU686_OP0F45_16, CPU686_OP0F45_32 }, //45h: 
		{ CPU686_OP0F46_16, CPU686_OP0F46_32 }, //46h: 
		{ CPU686_OP0F47_16, CPU686_OP0F47_32 }, //47h: 
		{ CPU686_OP0F48_16, CPU686_OP0F48_32 }, //48h: 
		{ CPU686_OP0F49_16, CPU686_OP0F49_32 }, //49h: 
		{ CPU686_OP0F4A_16, CPU686_OP0F4A_32 }, //4Ah: 
		{ CPU686_OP0F4B_16, CPU686_OP0F4B_32 }, //4Bh: 
		{ CPU686_OP0F4C_16, CPU686_OP0F4C_32 }, //4Ch: 
		{ CPU686_OP0F4D_16, CPU686_OP0F4D_32 }, //4Dh: 
		{ CPU686_OP0F4E_16, CPU686_OP0F4E_32 }, //4Eh: 
		{ CPU686_OP0F4F_16, CPU686_OP0F4F_32 }, //4Fh: 
		//0x50:
		{ NULL, NULL }, //50h:
		{ NULL, NULL }, //51h:
		{ NULL, NULL }, //52h:
		{ NULL, NULL }, //53h:
		{ NULL, NULL }, //54h:
		{ NULL, NULL }, //55h:
		{ NULL, NULL }, //56h:
		{ NULL, NULL }, //57h:
		{ NULL, NULL }, //58h:
		{ NULL, NULL }, //59h:
		{ NULL, NULL }, //5Ah:
		{ NULL, NULL }, //5Bh:
		{ NULL, NULL }, //5Ch:
		{ NULL, NULL }, //5Dh:
		{ NULL, NULL }, //5Eh:
		{ NULL, NULL }, //5Fh:
		//0x60:
		{ NULL, NULL }, //60h: PUSHA(removed here)
		{ NULL, NULL }, //61h: POPA(removed here)
		{ NULL, NULL }, //UNK
		{ NULL, NULL }, //UNK
		{ NULL, NULL }, //UNK
		{ NULL, NULL }, //UNK
		{ NULL, NULL }, //UNK
		{ NULL, NULL }, //UNK
		{ NULL, NULL }, //68h:
		{ NULL, NULL }, //69h:
		{ NULL, NULL }, //6Ah:
		{ NULL, NULL }, //6Bh:
		{ NULL, NULL }, //6Ch:
		{ NULL, NULL }, //6Dh:
		{ NULL, NULL }, //6Eh:
		{ NULL, NULL }, //6Fh:
		//0x70: Conditional JMP OPcodes:
		{ NULL, NULL }, //70h:
		{ NULL, NULL }, //71h:
		{ NULL, NULL }, //72h:
		{ NULL, NULL }, //73h:
		{ NULL, NULL }, //74h:
		{ NULL, NULL }, //75h:
		{ NULL, NULL }, //76h:
		{ NULL, NULL }, //77h:
		{ NULL, NULL }, //78h:
		{ NULL, NULL }, //79h:
		{ NULL, NULL }, //7Ah:
		{ NULL, NULL }, //7Bh:
		{ NULL, NULL }, //7Ch:
		{ NULL, NULL }, //7Dh:
		{ NULL, NULL }, //7Eh:
		{ NULL, NULL }, //7Fh:
		//0x80:
		{ NULL, NULL }, //80h:
		{ NULL, NULL }, //81h:
		{ NULL, NULL }, //82h:
		{ NULL, NULL }, //83h:
		{ NULL, NULL }, //84h:
		{ NULL, NULL }, //85h:
		{ NULL, NULL }, //86h:
		{ NULL, NULL }, //87h:
		{ NULL, NULL }, //88h:
		{ NULL, NULL }, //89h:
		{ NULL, NULL }, //8Ah:
		{ NULL, NULL }, //8Bh:
		{ NULL, NULL }, //8Ch:
		{ NULL, NULL }, //8Dh:
		{ NULL, NULL }, //8Eh:
		{ NULL, NULL }, //8Fh:
		//0x90:
		{ NULL, NULL }, //90h:
		{ NULL, NULL }, //91h:
		{ NULL, NULL }, //92h:
		{ NULL, NULL }, //93h:
		{ NULL, NULL }, //94h:
		{ NULL, NULL }, //95h:
		{ NULL, NULL }, //96h:
		{ NULL, NULL }, //97h:
		{ NULL, NULL }, //98h:
		{ NULL, NULL }, //99h:
		{ NULL, NULL }, //9Ah:
		{ NULL, NULL }, //9Bh:
		{ NULL, NULL }, //9Ch:
		{ NULL, NULL }, //9Dh:
		{ NULL, NULL }, //9Eh:
		{ NULL, NULL }, //9Fh:
		//0xA0:
		{ NULL, NULL }, //A0h:
		{ NULL, NULL }, //A1h:
		{ NULL, NULL }, //A2h:
		{ NULL, NULL }, //A3h:
		{ NULL, NULL }, //A4h:
		{ NULL, NULL }, //A5h:
		{ NULL, NULL }, //A6h:
		{ NULL, NULL }, //A7h:
		{ NULL, NULL }, //A8h:
		{ NULL, NULL }, //A9h:
		{ NULL, NULL }, //AAh:
		{ NULL, NULL }, //ABh:
		{ NULL, NULL }, //ACh:
		{ NULL, NULL }, //ADh:
		{ NULL, NULL }, //AEh:
		{ NULL, NULL }, //AFh:
		//0xB0:
		{ NULL, NULL }, //B0h:
		{ NULL, NULL }, //B1h:
		{ NULL, NULL }, //B2h:
		{ NULL, NULL }, //B3h:
		{ NULL, NULL }, //B4h:
		{ NULL, NULL }, //B5h:
		{ NULL, NULL }, //B6h:
		{ NULL, NULL }, //B7h:
		{ NULL, NULL }, //B8h:
		{ NULL, NULL }, //B9h:
		{ NULL, NULL }, //BAh:
		{ NULL, NULL }, //BBh:
		{ NULL, NULL }, //BCh:
		{ NULL, NULL }, //BDh:
		{ NULL, NULL }, //BEh:
		{ NULL, NULL }, //BFh:
		//0xC0:
		{ NULL, NULL }, //C0h:
		{ NULL, NULL }, //C1h:
		{ NULL, NULL }, //C2h:
		{ NULL, NULL }, //C3h:
		{ NULL, NULL }, //C4h:
		{ NULL, NULL }, //C5h:
		{ NULL, NULL }, //C6h:
		{ NULL, NULL }, //C7h:
		{ NULL, NULL }, //C8h:
		{ NULL, NULL }, //C9h:
		{ NULL, NULL }, //CAh:
		{ NULL, NULL }, //CBh:
		{ NULL, NULL }, //CCh:
		{ NULL, NULL }, //CDh:
		{ NULL, NULL }, //CEh:
		{ NULL, NULL }, //CFh:
		//0xD0:
		{ NULL, NULL }, //D0h:
		{ NULL, NULL }, //D1h:
		{ NULL, NULL }, //D2h:
		{ NULL, NULL }, //D3h:
		{ NULL, NULL }, //D4h:
		{ NULL, NULL }, //D5h:
		{ NULL, NULL }, //D6h: UNK
		{ NULL, NULL }, //D7h:
		{ NULL, NULL }, //D8h: UNK
		{ NULL, NULL }, //D9h: CoProcessor Minimum
		{ NULL, NULL }, //DAh: UNK
		{ NULL, NULL }, //DBh: CoProcessor Minimum
		{ NULL, NULL }, //DCh: UNK
		{ NULL, NULL }, //DDh: CoProcessor Minimum
		{ NULL, NULL }, //DEh: UNK
		{ NULL, NULL }, //DFh: COProcessor minimum
		//0xE0:
		{ NULL, NULL }, //E0h:
		{ NULL, NULL }, //E1h:
		{ NULL, NULL }, //E2h:
		{ NULL, NULL }, //E3h:
		{ NULL, NULL }, //E4h:
		{ NULL, NULL }, //E5h:
		{ NULL, NULL }, //E6h:
		{ NULL, NULL }, //E7h:
		{ NULL, NULL }, //E8h:
		{ NULL, NULL }, //E9h:
		{ NULL, NULL }, //EAh:
		{ NULL, NULL }, //EBh:
		{ NULL, NULL }, //ECh:
		{ NULL, NULL }, //EDh:
		{ NULL, NULL }, //EEh:
		{ NULL, NULL }, //EFh:
		//0xF0:
		{ NULL, NULL }, //F0h: Special
		{ NULL, NULL }, //F1h: UNK
		{ NULL, NULL }, //F2h: Special
		{ NULL, NULL }, //F3h: Special
		{ NULL, NULL }, //F4h:
		{ NULL, NULL }, //F5h:
		{ NULL, NULL }, //F6h:
		{ NULL, NULL }, //F7h:
		{ NULL, NULL }, //F8h:
		{ NULL, NULL }, //F9h:
		{ NULL, NULL }, //FAh:
		{ NULL, NULL }, //FBh:
		{ NULL, NULL }, //FCh:
		{ NULL, NULL }, //FDh:
		{ NULL, NULL }, //FEh:
		{ NULL, NULL }  //FFh:
	},

	//80786(Pentium II)
	{
		//0x00:
		{ NULL, NULL }, //00h:
		{ NULL, NULL }, //01h:
		{ NULL, NULL }, //02h:
		{ NULL, NULL }, //03h:
		{ NULL, NULL }, //04h:
		{ NULL, NULL }, //05h:
		{ NULL, NULL }, //06h:
		{ NULL, NULL }, //07h:
		{ NULL, NULL }, //08h:
		{ NULL, NULL }, //09h:
		{ NULL, NULL }, //0Ah:
		{ NULL, NULL }, //0Bh:
		{ NULL, NULL }, //0Ch:
		{ NULL, NULL }, //0Dh: NOP r/m16/32
		{ NULL, NULL }, //0Eh:
		{ NULL, NULL }, //0Fh: NECV30 specific OPcode!
		//0x10:
		{ NULL, NULL }, //10h:
		{ NULL, NULL }, //11h:
		{ NULL, NULL }, //12h:
		{ NULL, NULL }, //13h:
		{ NULL, NULL }, //14h:
		{ NULL, NULL }, //15h:
		{ NULL, NULL }, //16h:
		{ NULL, NULL }, //17h:
		{ NULL, NULL }, //18h: HINT_NOP /4-7 r/m16/32
		{ NULL, NULL }, //19h: HINT_NOP r/m16/32
		{ NULL, NULL }, //1Ah: HINT_NOP r/m16/32
		{ NULL, NULL }, //1Bh: HINT_NOP r/m16/32
		{ NULL, NULL }, //1Ch: HINT_NOP r/m16/32
		{ NULL, NULL }, //1Dh: HINT_NOP r/m16/32
		{ NULL, NULL }, //1Eh: HINT_NOP r/m16/32
		{ NULL, NULL }, //1Fh: HINT_NOP /1-7 r/m16
		//0x20:
		{ NULL, NULL }, //20h:
		{ NULL, NULL }, //21h:
		{ NULL, NULL }, //22h:
		{ NULL, NULL }, //23h:
		{ NULL, NULL }, //24h:
		{ NULL, NULL }, //25h:
		{ NULL, NULL }, //26h: Special
		{ NULL, NULL }, //27h:
		{ NULL, NULL }, //28h:
		{ NULL, NULL }, //29h:
		{ NULL, NULL }, //2Ah:
		{ NULL, NULL }, //2Bh:
		{ NULL, NULL }, //2Ch:
		{ NULL, NULL }, //2Dh:
		{ NULL, NULL }, //2Eh: Special
		{ NULL, NULL }, //2Fh:
		//0x30:
		{ CPU786_OP0F30, NULL }, //30h: RDMSR
		{ NULL, NULL }, //31h:
		{ CPU786_OP0F32, NULL }, //32h: WRMSR
		{ NULL, NULL }, //33h:
		{ CPU786_OP0F34, NULL }, //34h: SYSENTER
		{ CPU786_OP0F35, NULL }, //35h: SYSEXIT
		{ NULL, NULL }, //36h: Special
		{ NULL, NULL }, //37h:
		{ NULL, NULL }, //38h:
		{ NULL, NULL }, //39h:
		{ NULL, NULL }, //3Ah:
		{ NULL, NULL }, //3Bh:
		{ NULL, NULL }, //3Ch:
		{ NULL, NULL }, //3Dh:
		{ NULL, NULL }, //3Eh: Special
		{ NULL, NULL }, //3Fh:
		//0x40:
		{ NULL, NULL }, //40h: 
		{ NULL, NULL }, //41h: 
		{ NULL, NULL }, //42h: 
		{ NULL, NULL }, //43h: 
		{ NULL, NULL }, //44h: 
		{ NULL, NULL }, //45h: 
		{ NULL, NULL }, //46h: 
		{ NULL, NULL }, //47h: 
		{ NULL, NULL }, //48h: 
		{ NULL, NULL }, //49h: 
		{ NULL, NULL }, //4Ah: 
		{ NULL, NULL }, //4Bh: 
		{ NULL, NULL }, //4Ch: 
		{ NULL, NULL }, //4Dh: 
		{ NULL, NULL }, //4Eh: 
		{ NULL, NULL }, //4Fh: 
		//0x50:
		{ NULL, NULL }, //50h:
		{ NULL, NULL }, //51h:
		{ NULL, NULL }, //52h:
		{ NULL, NULL }, //53h:
		{ NULL, NULL }, //54h:
		{ NULL, NULL }, //55h:
		{ NULL, NULL }, //56h:
		{ NULL, NULL }, //57h:
		{ NULL, NULL }, //58h:
		{ NULL, NULL }, //59h:
		{ NULL, NULL }, //5Ah:
		{ NULL, NULL }, //5Bh:
		{ NULL, NULL }, //5Ch:
		{ NULL, NULL }, //5Dh:
		{ NULL, NULL }, //5Eh:
		{ NULL, NULL }, //5Fh:
		//0x60:
		{ NULL, NULL }, //60h: PUSHA(removed here)
		{ NULL, NULL }, //61h: POPA(removed here)
		{ NULL, NULL }, //UNK
		{ NULL, NULL }, //UNK
		{ NULL, NULL }, //UNK
		{ NULL, NULL }, //UNK
		{ NULL, NULL }, //UNK
		{ NULL, NULL }, //UNK
		{ NULL, NULL }, //68h:
		{ NULL, NULL }, //69h:
		{ NULL, NULL }, //6Ah:
		{ NULL, NULL }, //6Bh:
		{ NULL, NULL }, //6Ch:
		{ NULL, NULL }, //6Dh:
		{ NULL, NULL }, //6Eh:
		{ NULL, NULL }, //6Fh:
		//0x70: Conditional JMP OPcodes:
		{ NULL, NULL }, //70h:
		{ NULL, NULL }, //71h:
		{ NULL, NULL }, //72h:
		{ NULL, NULL }, //73h:
		{ NULL, NULL }, //74h:
		{ NULL, NULL }, //75h:
		{ NULL, NULL }, //76h:
		{ NULL, NULL }, //77h:
		{ NULL, NULL }, //78h:
		{ NULL, NULL }, //79h:
		{ NULL, NULL }, //7Ah:
		{ NULL, NULL }, //7Bh:
		{ NULL, NULL }, //7Ch:
		{ NULL, NULL }, //7Dh:
		{ NULL, NULL }, //7Eh:
		{ NULL, NULL }, //7Fh:
		//0x80:
		{ NULL, NULL }, //80h:
		{ NULL, NULL }, //81h:
		{ NULL, NULL }, //82h:
		{ NULL, NULL }, //83h:
		{ NULL, NULL }, //84h:
		{ NULL, NULL }, //85h:
		{ NULL, NULL }, //86h:
		{ NULL, NULL }, //87h:
		{ NULL, NULL }, //88h:
		{ NULL, NULL }, //89h:
		{ NULL, NULL }, //8Ah:
		{ NULL, NULL }, //8Bh:
		{ NULL, NULL }, //8Ch:
		{ NULL, NULL }, //8Dh:
		{ NULL, NULL }, //8Eh:
		{ NULL, NULL }, //8Fh:
		//0x90:
		{ NULL, NULL }, //90h:
		{ NULL, NULL }, //91h:
		{ NULL, NULL }, //92h:
		{ NULL, NULL }, //93h:
		{ NULL, NULL }, //94h:
		{ NULL, NULL }, //95h:
		{ NULL, NULL }, //96h:
		{ NULL, NULL }, //97h:
		{ NULL, NULL }, //98h:
		{ NULL, NULL }, //99h:
		{ NULL, NULL }, //9Ah:
		{ NULL, NULL }, //9Bh:
		{ NULL, NULL }, //9Ch:
		{ NULL, NULL }, //9Dh:
		{ NULL, NULL }, //9Eh:
		{ NULL, NULL }, //9Fh:
		//0xA0:
		{ NULL, NULL }, //A0h:
		{ NULL, NULL }, //A1h:
		{ NULL, NULL }, //A2h:
		{ NULL, NULL }, //A3h:
		{ NULL, NULL }, //A4h:
		{ NULL, NULL }, //A5h:
		{ NULL, NULL }, //A6h:
		{ NULL, NULL }, //A7h:
		{ NULL, NULL }, //A8h:
		{ NULL, NULL }, //A9h:
		{ NULL, NULL }, //AAh:
		{ NULL, NULL }, //ABh:
		{ NULL, NULL }, //ACh:
		{ NULL, NULL }, //ADh:
		{ NULL, NULL }, //AEh:
		{ NULL, NULL }, //AFh:
		//0xB0:
		{ NULL, NULL }, //B0h:
		{ NULL, NULL }, //B1h:
		{ NULL, NULL }, //B2h:
		{ NULL, NULL }, //B3h:
		{ NULL, NULL }, //B4h:
		{ NULL, NULL }, //B5h:
		{ NULL, NULL }, //B6h:
		{ NULL, NULL }, //B7h:
		{ NULL, NULL }, //B8h:
		{ NULL, NULL }, //B9h:
		{ NULL, NULL }, //BAh:
		{ NULL, NULL }, //BBh:
		{ NULL, NULL }, //BCh:
		{ NULL, NULL }, //BDh:
		{ NULL, NULL }, //BEh:
		{ NULL, NULL }, //BFh:
		//0xC0:
		{ NULL, NULL }, //C0h:
		{ NULL, NULL }, //C1h:
		{ NULL, NULL }, //C2h:
		{ NULL, NULL }, //C3h:
		{ NULL, NULL }, //C4h:
		{ NULL, NULL }, //C5h:
		{ NULL, NULL }, //C6h:
		{ NULL, NULL }, //C7h:
		{ NULL, NULL }, //C8h:
		{ NULL, NULL }, //C9h:
		{ NULL, NULL }, //CAh:
		{ NULL, NULL }, //CBh:
		{ NULL, NULL }, //CCh:
		{ NULL, NULL }, //CDh:
		{ NULL, NULL }, //CEh:
		{ NULL, NULL }, //CFh:
		//0xD0:
		{ NULL, NULL }, //D0h:
		{ NULL, NULL }, //D1h:
		{ NULL, NULL }, //D2h:
		{ NULL, NULL }, //D3h:
		{ NULL, NULL }, //D4h:
		{ NULL, NULL }, //D5h:
		{ NULL, NULL }, //D6h: UNK
		{ NULL, NULL }, //D7h:
		{ NULL, NULL }, //D8h: UNK
		{ NULL, NULL }, //D9h: CoProcessor Minimum
		{ NULL, NULL }, //DAh: UNK
		{ NULL, NULL }, //DBh: CoProcessor Minimum
		{ NULL, NULL }, //DCh: UNK
		{ NULL, NULL }, //DDh: CoProcessor Minimum
		{ NULL, NULL }, //DEh: UNK
		{ NULL, NULL }, //DFh: COProcessor minimum
		//0xE0:
		{ NULL, NULL }, //E0h:
		{ NULL, NULL }, //E1h:
		{ NULL, NULL }, //E2h:
		{ NULL, NULL }, //E3h:
		{ NULL, NULL }, //E4h:
		{ NULL, NULL }, //E5h:
		{ NULL, NULL }, //E6h:
		{ NULL, NULL }, //E7h:
		{ NULL, NULL }, //E8h:
		{ NULL, NULL }, //E9h:
		{ NULL, NULL }, //EAh:
		{ NULL, NULL }, //EBh:
		{ NULL, NULL }, //ECh:
		{ NULL, NULL }, //EDh:
		{ NULL, NULL }, //EEh:
		{ NULL, NULL }, //EFh:
		//0xF0:
		{ NULL, NULL }, //F0h: Special
		{ NULL, NULL }, //F1h: UNK
		{ NULL, NULL }, //F2h: Special
		{ NULL, NULL }, //F3h: Special
		{ NULL, NULL }, //F4h:
		{ NULL, NULL }, //F5h:
		{ NULL, NULL }, //F6h:
		{ NULL, NULL }, //F7h:
		{ NULL, NULL }, //F8h:
		{ NULL, NULL }, //F9h:
		{ NULL, NULL }, //FAh:
		{ NULL, NULL }, //FBh:
		{ NULL, NULL }, //FCh:
		{ NULL, NULL }, //FDh:
		{ NULL, NULL }, //FEh:
		{ NULL, NULL }  //FFh:
	}
};

extern Handler CurrentCPU_opcode_jmptbl[1024]; //Our standard internal opcode jmptbl!
#ifdef VISUALC
extern Handler CurrentCPU_opcode_jmptbl_2[2][256][2]; //Our standard internal opcode jmptbl!
#endif

void generate_opcode0F_jmptbl()
{
	byte cpu; //What CPU are we processing!
	byte currentoperandsize = 0;
	word OP; //The opcode to process!
	byte currentCPU; //Current CPU to start off at!
	for (currentoperandsize = 0; currentoperandsize < 2; currentoperandsize++) //Process all operand sizes!
	{
		byte operandsize = currentoperandsize; //Operand size to use!
		for (OP = 0; OP < 0x100; OP++) //Process all opcodes!
		{
			cpu = (byte)EMULATED_CPU; //Start with the emulated CPU and work up to the predesessors!
			if (cpu >= CPU_80286) //286+?
			{
				cpu -= CPU_80286; //We start existing at the 286!
				currentCPU = cpu; //Save for restoration during searching!
				operandsize = currentoperandsize; //Initialize to our current operand size to search!
				while (!opcode0F_jmptbl[cpu][OP][operandsize]) //No opcode to handle at current CPU&operand size?
				{
					if (cpu) //We have an CPU size: switch to an earlier CPU if possible!
					{
						--cpu; //Not anymore! Look up one level!
						continue; //Try again!
					}
					else //No CPU: we're a standard, so go up one operand size and retry!
					{
						cpu = currentCPU; //Reset CPU to search!
						if (operandsize) //We've got operand sizes left?
						{
							--operandsize; //Go up one operand size!
						}
						else break; //No CPUs left? Then stop searching!
					}
				}
				if (opcode0F_jmptbl[cpu][OP][operandsize])
				{
					CurrentCPU_opcode_jmptbl[(OP << 2) | 2 | currentoperandsize] = opcode0F_jmptbl[cpu][OP][operandsize]; //Execute this instruction when we're triggered!
				}
				else
				{
					CurrentCPU_opcode_jmptbl[(OP << 2) | 2 | currentoperandsize] = &unkOP0F_286; //Execute this instruction when we're triggered!
				}
			}
			else //Too old a CPU to support 0F opcodes? Install safety handlers instead!
			{
				CurrentCPU_opcode_jmptbl[(OP << 1) | 2 | currentoperandsize] = (cpu==CPU_8086)?&unkOP_8086:&unkOP_186; //Execute this instruction when we're triggered!
			}
			#ifdef VISUALC
			CurrentCPU_opcode_jmptbl_2[1][OP][currentoperandsize] = CurrentCPU_opcode_jmptbl[(OP << 2) | 2 | currentoperandsize]; //Easy to view version!
			#endif
		}
	}
}
