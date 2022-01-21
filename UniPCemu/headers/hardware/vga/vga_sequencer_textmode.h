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

#ifndef VGA_SEQUENCER_TEXTMODE_H
#define VGA_SEQUENCER_TEXTMODE_H

#include "headers/types.h"
#include "headers/hardware/vga/vga.h" //For VGA_Info!
#include "headers/hardware/vga/vga_attributecontroller.h" //For attribute info result!
#include "headers/hardware/vga/vga_renderer.h" //For attribute info result!

void VGA_Sequencer_TextMode(VGA_Type *VGA, SEQ_DATA *Sequencer, VGA_AttributeInfo *attributeinfo); //Render a text mode pixel!
void VGA_TextDecoder(VGA_Type *VGA, word loadedlocation);
#endif