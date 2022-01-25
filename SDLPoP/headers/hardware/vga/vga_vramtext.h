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

#ifndef __VGA_VRAMTEXT_H
#define __VGA_VRAMTEXT_H

#include "headers/types.h" //Basic types!
#include "headers/hardware/vga/vga.h" //VGA basic types!

void VGA_charsetupdated(VGA_Type *VGA); //The character set settings have been updated?
byte getcharxy(VGA_Type *VGA, byte attribute, byte character, byte x, byte y);
//byte getcharrow(VGA_Type *VGA, byte attribute3, byte character, byte y); //Retrieve a characters y row on/off from table!
void dumpVGATextFonts(); //Dump the active fontsets!
void fillCGAfont();
void fillMDAfont();
byte getcharxy_CGA(byte character, byte x, byte y); //Retrieve a characters x,y pixel on/off from the unmodified 8x8 table!
byte getcharxy_MDA(byte character, byte x, byte y); //Retrieve a characters x,y pixel on/off from the unmodified 8x14 table!

#endif