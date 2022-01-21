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

#ifndef VGA_ATTRIBUTECONTROLLER_H
#define VGA_ATTRIBUTECONTROLLER_H

#include "headers/types.h" //Basic types!
#include "headers/hardware/vga/vga.h" //VGA!

typedef struct
{
	int attribute_graphics; //Use graphics attribute: attribute is raw index into table? 0=Normal operation, 1=Font only, 2=Attribute controller disabled!
	word attribute; //Attribute for the character!
	byte fontpixel; //Are we a front pixel?
	word charx; //Character x!
	word charinner_x; //Inner x base of character!
	byte lookupprecalcs; //Precalculated lookup values!
	byte attributesize; //The size of the attribute read, in Sequencer clocks!
	byte latchstatus; //The current status of the Attribute Controller latch used for 8-bit pixels!
} VGA_AttributeInfo; //Attribute info!

#define VGA_SEQUENCER_ATTRIBUTESHIFT 7

//Precalcs!
byte getHorizontalPixelPanning(VGA_Type *VGA); //Active horizontal pixel panning when enabled?
void VGA_AttributeController_calcAttributes(VGA_Type *VGA); //Update attributes!
//Seperate attribute modes!
typedef byte(*VGA_AttributeController_Mode)(VGA_AttributeInfo *Sequencer_attributeinfo, VGA_Type *VGA); //An attribute controller mode!
byte VGA_AttributeController_16bit(VGA_AttributeInfo *Sequencer_attributeinfo, VGA_Type *VGA);
byte VGA_AttributeController_8bit(VGA_AttributeInfo *Sequencer_attributeinfo, VGA_Type *VGA);
byte VGA_AttributeController_4bit(VGA_AttributeInfo *Sequencer_attributeinfo, VGA_Type *VGA);
//Translate 4-bit or 8-bit color to 256 color DAC Index through palette!

void updateVGAAttributeController_Mode(VGA_Type *VGA); //Update the current mode!
#endif