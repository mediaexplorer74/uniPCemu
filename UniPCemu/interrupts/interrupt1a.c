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

#include "headers/types.h"
#include "headers/cpu/cpu.h" //CPU module!
#include "headers/cpu/easyregs.h" //Easy register access!
#include "headers/hardware/ports.h" //CMOS support!
#include "headers/cpu/cb_manager.h" //Callback support!

//Our IRQ0 handler!
void BIOS_IRQ0()
{
	uint_32 result;
	result = MMU_rdw(CPU_SEGMENT_DS, 0x0040, 0x6C, 0,1); //Read the result!
	++result; //Increase the number!
	if (result == 0x1800B0) //Midnight count reached?
	{
		MMU_wb(CPU_SEGMENT_DS, 0x0040, 0x0070, 0x01,1); //Set Midnight flag!
		result = 0; //Clear counter!
	}
	MMU_wdw(CPU_SEGMENT_DS, 0x0040, 0x6c, result,1); //Write data!
}

void BIOS_int1A() //Handler!
{
	uint_32 result;
	switch (REG_AH) //What function
	{
	case 0x00: //Get system clock?
		CALLBACK_SCF(0); //Clear carry flag to indicate no error!
		result = MMU_rdw(CPU_SEGMENT_DS, 0x0040, 0x6C, 0,1); //Read the result!
		REG_DX = (result >> 16); //High value!
		REG_CX = (result & 0xFFFF); //Low value!
		REG_AL = MMU_rb(CPU_SEGMENT_DS, 0x0040, 0x0070,0,1); //Midnight flag!
		MMU_wb(CPU_SEGMENT_DS, 0x0040, 0x0070, 0x00,1); //Clear Midnight flag!
		break;
	case 0x001: //Set system clock!
		CALLBACK_SCF(0); //Clear carry flag to indicate no error!; / Clear error flag!
		result = ((REG_DX << 16) | REG_CX); //Calculate result!
		MMU_wdw(CPU_SEGMENT_DS, 0x0040, 0x6c, result,1); //Write data!
		MMU_wb(CPU_SEGMENT_DS, 0x0040, 0x0070, 0x00,1); //Clear Midnight flag!
		break;
	default: //Unknown function?
		CALLBACK_SCF(1); //Set carry flag to indicate an error!
		break;
	}
}