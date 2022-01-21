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

#ifndef VGA_SEQUENCER_GRAPHICSMODE_H
#define VGA_SEQUENCER_GRAPHICSMODE_H

#include "headers/types.h" //Basic types!
#include "headers/hardware/vga/vga.h" //VGA subset!
#include "headers/hardware/vga/vga_renderer.h" //Sequencer!

typedef union
{
	uint_64 pixelbufferq;
	byte pixelbuffer[8]; //All 8 pixels decoded from the planesbuffer!
} PIXELBUFFERCONTAINERTYPE;

void VGA_Sequencer_GraphicsMode(VGA_Type *VGA, SEQ_DATA *Sequencer, VGA_AttributeInfo *attributeinfo);
void VGA_GraphicsDecoder(VGA_Type *VGA, word loadedlocation);
void updateVGAGraphics_Mode(VGA_Type *VGA);
#endif