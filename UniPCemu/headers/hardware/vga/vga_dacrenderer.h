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

#ifndef VGA_DACRENDERER_H
#define VGA_DACRENDERER_H
#include "headers/types.h" //Basic types!
#include "headers/hardware/vga/vga.h" //VGA support!
#include "headers/hardware/vga/vga_renderer.h" //Sequencer!

void DAC_updateEntry(VGA_Type *VGA, byte entry); //Update a DAC entry for rendering!
void DAC_updateEntries(VGA_Type *VGA); //Update all DAC entries for rendering!
uint_32 GA_color2bw(uint_32 color, byte is32bit); //Convert color values to b/w values!

void VGA_DUMPColors(); //Dumps the full DAC and Attribute colors!

void VGA_initBWConversion(); //Init B/W conversion data!
void VGA_initRGBAconversion(); //Init RGBA conversion!
void VGA_initColorLevels(VGA_Type* VGA, byte enablePedestal); //Initialize the color levels to use for VGA active display colors!
byte DAC_Use_BWMonitor(byte use); //Use B/W monitor?
byte DAC_BWColor(byte use); //What B/W color to use?
byte DAC_Use_BWluminance(byte use); //What luminance method to use?
#endif
