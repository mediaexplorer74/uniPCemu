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

#include "headers/types.h" //Types!
#include "headers/hardware/vga/vga.h" //VGA!

void readDAC(VGA_Type *VGA, byte entrynumber,DACEntry *entry) //Read a DAC entry
{
	word entryi;
	entryi = entrynumber;
	entryi <<= 2; //Multiply by 4!
	entry->r = VGA->registers->DAC[entryi]; //R
	entry->g = VGA->registers->DAC[entryi|1]; //G
	entry->b = VGA->registers->DAC[entryi|2]; //B
}

void writeDAC(VGA_Type *VGA, byte entrynumber,DACEntry *entry) //Write a DAC entry
{
	word entryi;
	entryi = entrynumber;
	entryi <<= 2; //Multiply by 4!
	VGA->registers->DAC[entryi] = entry->r; //R
	VGA->registers->DAC[entryi|1] = entry->g; //G
	VGA->registers->DAC[entryi|2] = entry->b; //B
	VGA_calcprecalcs(VGA,WHEREUPDATED_DAC|entrynumber); //We've been updated!
}