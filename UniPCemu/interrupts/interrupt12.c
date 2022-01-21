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
#include "headers/cpu/mmu.h" //Basic MMU support!
#include "headers/cpu/cb_manager.h" //Callback support!
#include "headers/cpu/protection.h" //Protection support!

void BIOS_int12() //Get BIOS memory size
{
	REG_AX = MMU_rw(CB_ISCallback() ? (sword)CPU_segment_index(CPU_SEGMENT_DS) : -1, 0x0040, 0x0013, 0,1); //Give BIOS equipment word!
}