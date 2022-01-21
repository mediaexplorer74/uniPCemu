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

#ifndef VGA_VRAM_H
#define VGA_VRAM_H

#include "headers/types.h" //Basic types!
#include "headers/hardware/vga/vga.h" //VGA basics!

byte readVRAMplane(VGA_Type *VGA, byte plane, uint_32 offset, uint_32 bank, byte is_CPU); //Read from a VRAM plane!
void writeVRAMplane(VGA_Type *VGA, byte plane, uint_32 offset, uint_32 bank, byte value, byte is_CPU); //Write to a VRAM plane!

//Direct access to 32-bit VRAM planes!
#define VGA_VRAMDIRECTPLANAR(VGA,vramlocation,bank) *((uint_32 *)((byte *)&VGA->VRAM[((vramlocation<<2)+bank)&VGA->precalcs.VMemMask]))
#define VGA_VRAMDIRECT(VGA,vramlocation,bank) VGA->VRAM[(vramlocation+bank)&VGA->precalcs.VMemMask]

void updateVGAMMUAddressMode(VGA_Type* VGA); //Update the currently assigned memory mode for mapping memory by address!

#endif