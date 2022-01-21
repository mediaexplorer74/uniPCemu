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

#include "headers/types.h" //Basic types
#include "headers/cpu/cpu.h" //CPU support!
#include "headers/cpu/easyregs.h" //Easy registers!
#include "headers/cpu/cb_manager.h" //Callback support!

void BIOS_int15()
{
	switch (REG_AH) //What function?
	{
	case 0xC0: //Get configuration?
		if (EMULATED_CPU>=CPU_80286) //286+ function?
		{
			REG_AH = 0x00; //Supported!
			CALLBACK_SCF(0); //Set carry flag to indicate an error!
			MMU_ww(CPU_SEGMENT_ES, REG_ES, REG_BX, 8,1); //8 bytes following!
			switch (EMULATED_CPU) //What CPU are we emulating?
			{
			case 0: //8086?
				MMU_wb(CPU_SEGMENT_ES, REG_ES, REG_BX + 0x02, 0xFB,1); //PC/XT!
				break;
			case 1: //NECV30?
				MMU_wb(CPU_SEGMENT_ES, REG_ES, REG_BX + 0x02, 0xFB,1); //PC/XT!
				break;
			case 2: //80286?
			case 3: //80386?
			case 4: //80486?
			case 5: //Pentium?
				MMU_wb(CPU_SEGMENT_ES, REG_ES, REG_BX + 0x02, 0xF8,1); //386 or higher CPU!
				break;
			default:
				break;
			}
			MMU_wb(CPU_SEGMENT_ES, REG_ES, REG_BX + 0x03, 0x00,1); //Unknown submodel!
			MMU_wb(CPU_SEGMENT_ES, REG_ES, REG_BX + 0x04, 0x01,1); //First BIOS revision!
			MMU_wb(CPU_SEGMENT_ES, REG_ES, REG_BX + 0x05, 0x40|0x20,1); //We have 2nd PIC and RTC!
			MMU_wb(CPU_SEGMENT_ES, REG_ES, REG_BX + 0x06, 0x00,1); //No extra hardware!
			MMU_wb(CPU_SEGMENT_ES, REG_ES, REG_BX + 0x07, 0x00,1); //No extra support!
			MMU_wb(CPU_SEGMENT_ES, REG_ES, REG_BX + 0x08, 0x00,1); //No extra support!
			MMU_wb(CPU_SEGMENT_ES, REG_ES, REG_BX + 0x09, 0x00,1); //No enhanced mouse mode or Flash BIOS!
		}
		else
		{
			goto invalidfunction; //Count as invalid function!
		}
		break;
	case 0x4F: //Translate scan code?
		CALLBACK_SCF(1); //Set carry flag to indicate translating the scan code normally(let the BIOS process the scan code)!
		goto invalidfunction; //Count as invalid function!
		break;
	default: //Unknown function?
		invalidfunction:
		REG_AH = 0x86; //Not supported!
		CALLBACK_SCF(1); //Set carry flag to indicate an error!
		break;
	}
}