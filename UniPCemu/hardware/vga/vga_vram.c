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

#include "headers/types.h" //Basic type support!
#include "headers/hardware/vga/vga.h" //VGA data!
#include "headers/hardware/vga/vga_vram.h" //VRAM support!

//We handle all input for writing to VRAM and reading from VRAM directly here!

//Bit from left to right starts with 0(value 128) ends with 7(value 1)

//Planar access to VRAM
byte readVRAMplane(VGA_Type *VGA, byte plane, uint_32 offset, uint_32 bank, byte is_CPU) //Read from a VRAM plane!
{
	if (unlikely(VGA==0)) return 0; //Invalid VGA!
	plane &= 3; //Only 4 planes are available! Wrap arround the planes if needed!

	INLINEREGISTER uint_32 fulloffset2;
	fulloffset2 = offset; //Default offset to use!
	fulloffset2 <<= 2; //We cycle through the offsets!
	fulloffset2 |= plane; //The plane goes from low to high, through all indexes!
	fulloffset2 += bank; //Add the bank directly!

	fulloffset2 &= VGA->precalcs.VMemMask; //Only 64K memory available, so wrap arround it when needed!
	if (unlikely((fulloffset2>=VGA->VRAM_size) || ((fulloffset2 > VGA->precalcs.VRAM_limit) && VGA->precalcs.VRAM_limit && is_CPU))) return 0xFF; //VRAM valid, simple check?
	if (fulloffset2 > VGA->VRAM_used) VGA->VRAM_used = fulloffset2; //How much VRAM is actually used by software?
	return VGA->VRAM[fulloffset2]; //Read the data from VRAM!
}

void writeVRAMplane(VGA_Type *VGA, byte plane, uint_32 offset, uint_32 bank, byte value, byte is_CPU) //Write to a VRAM plane!
{
	if (unlikely(VGA==0)) return; //Invalid VGA!
	plane &= 3; //Only 4 planes are available!

	INLINEREGISTER uint_32 fulloffset2;
	fulloffset2 = offset; //Load the offset!
	fulloffset2 <<= 2; //We cycle through the offsets!
	fulloffset2 |= plane; //The plane goes from low to high, through all indexes!
	fulloffset2 += bank; //Add the bank directly!

	fulloffset2 &= VGA->precalcs.VMemMask; //Only 64K memory available, so wrap arround it when needed!
	if (unlikely((fulloffset2>=VGA->VRAM_size) || ((fulloffset2 > VGA->precalcs.VRAM_limit) && VGA->precalcs.VRAM_limit && is_CPU))) return; //VRAM valid, simple check?
	VGA->VRAM[fulloffset2++] = value; //Set the data in VRAM! Also increase the address afterwards to detect how much is used.
	if (fulloffset2 > VGA->VRAM_used) VGA->VRAM_used = fulloffset2; //How much VRAM is actually used by software?
	if (unlikely(plane&2)) //Character RAM updated(both plane 2/3)?
	{
		VGA_plane23updated(VGA,(offset+(bank&(~3)))); //Plane 2 has been updated!	
	}
}
