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

#define VGA_SEQUENCER_GRAPHICSMODE

#include "headers/types.h" //Basic types!
#include "headers/hardware/vga/vga.h" //VGA!
#include "headers/hardware/vga/vga_sequencer_graphicsmode.h" //Graphics mode!

PIXELBUFFERCONTAINERTYPE pixelbuffercontainer;

extern LOADEDPLANESCONTAINER loadedplanes; //All read planes for the current processing!

/*

256 COLOR MODE

*/

void load256colorshiftmode() //256-color shift mode!
{
	INLINEREGISTER uint_32 originalplanes; //The loaded planes!
	INLINEREGISTER LOADEDPLANESCONTAINER currentplanes; //For splitting the planes!
	originalplanes = loadedplanes.loadedplanes; //Load the planes for retrieving!
	currentplanes.loadedplanes = (originalplanes&0x0F0F0F0F); //Low planes!
	pixelbuffercontainer.pixelbuffer[1] = currentplanes.splitplanes[0];
	pixelbuffercontainer.pixelbuffer[3] = currentplanes.splitplanes[1];
	pixelbuffercontainer.pixelbuffer[5] = currentplanes.splitplanes[2];
	pixelbuffercontainer.pixelbuffer[7] = currentplanes.splitplanes[3];
	currentplanes.loadedplanes = ((originalplanes&0xF0F0F0F0)>>4); //Load the planes for retrieving!
	pixelbuffercontainer.pixelbuffer[0] = currentplanes.splitplanes[0];
	pixelbuffercontainer.pixelbuffer[2] = currentplanes.splitplanes[1];
	pixelbuffercontainer.pixelbuffer[4] = currentplanes.splitplanes[2];
	pixelbuffercontainer.pixelbuffer[6] = currentplanes.splitplanes[3];
}

/*

SHIFT REGISTER INTERLEAVE MODE

*/

void loadpackedshiftmode() //Packed shift mode!
{
	INLINEREGISTER byte temp; //A buffer for our current pixel!
	INLINEREGISTER PIXELBUFFERCONTAINERTYPE lpixelbuffercontainer;
	INLINEREGISTER LOADEDPLANESCONTAINER currentplanes; //For splitting the planes!
	INLINEREGISTER uint_64 pixelbufferqbackup;
	currentplanes.loadedplanes = loadedplanes.loadedplanes; //All loaded planes!
	temp = currentplanes.splitplanes[2]; //Load high plane!
	lpixelbuffercontainer.pixelbuffer[3] = temp;
	lpixelbuffercontainer.pixelbuffer[2] = (temp>>=2);
	lpixelbuffercontainer.pixelbuffer[1] = (temp>>=2);
	lpixelbuffercontainer.pixelbuffer[0] = (temp>>=2); //Shift out the high bits!
	temp = currentplanes.splitplanes[3]; //Load high plane!
	lpixelbuffercontainer.pixelbuffer[7] = temp;
	lpixelbuffercontainer.pixelbuffer[6] = (temp>>=2);
	lpixelbuffercontainer.pixelbuffer[5] = (temp>>=2);
	lpixelbuffercontainer.pixelbuffer[4] = (temp>>=2); //Shift out the high bits!
	pixelbufferqbackup = ((lpixelbuffercontainer.pixelbufferq<<2)&0x0C0C0C0C0C0C0C0CULL); //Shift to the high part and store!

	temp = currentplanes.splitplanes[0]; //Load low plane!
	lpixelbuffercontainer.pixelbuffer[3] = temp;
	lpixelbuffercontainer.pixelbuffer[2] = (temp>>=2);
	lpixelbuffercontainer.pixelbuffer[1] = (temp>>=2);
	lpixelbuffercontainer.pixelbuffer[0] = (temp>>=2); //Shift out the low bits!
	temp = currentplanes.splitplanes[1]; //Load low plane!
	lpixelbuffercontainer.pixelbuffer[7] = temp;
	lpixelbuffercontainer.pixelbuffer[6] = (temp>>=2);
	lpixelbuffercontainer.pixelbuffer[5] = (temp>>=2);
	lpixelbuffercontainer.pixelbuffer[4] = (temp>>=2); //Shift out the low bits!
	lpixelbuffercontainer.pixelbufferq &= 0x0303030303030303ULL; //Shift to the low part!

	//Combine the high and low values for their full 4-bit value!
	lpixelbuffercontainer.pixelbufferq |= pixelbufferqbackup; //Combine both parts for the full value!
	pixelbuffercontainer.pixelbufferq = lpixelbuffercontainer.pixelbufferq; //Save the resulting pixel buffer!
}

/*

SINGLE SHIFT MODE

*/

void loadplanarshiftmode() //Planar shift mode!
{
	INLINEREGISTER uint_32 originalplanes; //The loaded planes!
	INLINEREGISTER LOADEDPLANESCONTAINER currentplanes; //For splitting the planes!
	originalplanes = loadedplanes.loadedplanes; //Load the planes for retrieving!

	currentplanes.loadedplanes = originalplanes&0x01010101; //Load the value to split!
	pixelbuffercontainer.pixelbuffer[7] = (currentplanes.plane0)|(currentplanes.plane1<<1)|(currentplanes.plane2<<2)|(currentplanes.plane3<<3); //Combine the four planes into a pixel!
	originalplanes >>= 1; //Shift to the next pixel!

	currentplanes.loadedplanes = originalplanes&0x01010101; //Load the value to split!
	pixelbuffercontainer.pixelbuffer[6] = (currentplanes.plane0) | (currentplanes.plane1 << 1) | (currentplanes.plane2 << 2) | (currentplanes.plane3 << 3); //Combine the four planes into a pixel!
	originalplanes >>= 1; //Shift to the next pixel!

	currentplanes.loadedplanes = originalplanes&0x01010101; //Load the value to split!
	pixelbuffercontainer.pixelbuffer[5] = (currentplanes.plane0) | (currentplanes.plane1 << 1) | (currentplanes.plane2 << 2) | (currentplanes.plane3 << 3); //Combine the four planes into a pixel!
	originalplanes >>= 1; //Shift to the next pixel!

	currentplanes.loadedplanes = originalplanes&0x01010101; //Load the value to split!
	pixelbuffercontainer.pixelbuffer[4] = (currentplanes.plane0) | (currentplanes.plane1 << 1) | (currentplanes.plane2 << 2) | (currentplanes.plane3 << 3); //Combine the four planes into a pixel!
	originalplanes >>= 1; //Shift to the next pixel!

	currentplanes.loadedplanes = originalplanes&0x01010101; //Load the value to split!
	pixelbuffercontainer.pixelbuffer[3] = (currentplanes.plane0) | (currentplanes.plane1 << 1) | (currentplanes.plane2 << 2) | (currentplanes.plane3 << 3); //Combine the four planes into a pixel!
	originalplanes >>= 1; //Shift to the next pixel!

	currentplanes.loadedplanes = originalplanes&0x01010101; //Load the value to split!
	pixelbuffercontainer.pixelbuffer[2] = (currentplanes.plane0) | (currentplanes.plane1 << 1) | (currentplanes.plane2 << 2) | (currentplanes.plane3 << 3); //Combine the four planes into a pixel!
	originalplanes >>= 1; //Shift to the next pixel!

	currentplanes.loadedplanes = originalplanes&0x01010101; //Load the value to split!
	pixelbuffercontainer.pixelbuffer[1] = (currentplanes.plane0) | (currentplanes.plane1 << 1) | (currentplanes.plane2 << 2) | (currentplanes.plane3 << 3); //Combine the four planes into a pixel!
	originalplanes >>= 1; //Shift to the next pixel!

	currentplanes.loadedplanes = originalplanes&0x01010101; //Load the value to split!
	pixelbuffercontainer.pixelbuffer[0] = (currentplanes.plane0) | (currentplanes.plane1 << 1) | (currentplanes.plane2 << 2) | (currentplanes.plane3 << 3); //Combine the four planes into a pixel!
}

//Shiftregister: 2=ShiftRegisterInterleave, 1=Color256ShiftMode. Priority list: 1, 2, 0; So 1&3=256colorshiftmode, 2=ShiftRegisterInterleave, 0=SingleShift.
//When index0(VGA->registers->GraphicsRegisters.REGISTERS.MISCGRAPHICSREGISTER.AlphaNumericModeDisable)=1, getColorPlanesAlphaNumeric
//When index1(IGNOREATTRPLANES)=1, getColorPlanesIgnoreAttrPlanes

//http://www.openwatcom.org/index.php/VGA_Fundamentals:
//Packed Pixel: Color 256 Shift Mode.
//Parallel Planes: Else case!
//Interleaved: Shift Register Interleave!

/*

Core functions!

*/

static Handler loadpixel_jmptbl[4] = {
	loadplanarshiftmode,
	loadpackedshiftmode,
	load256colorshiftmode, //Normal VGA 256-color shift mode. Also with 8-bit DAC used(SVGA mode 2h)!
	loadplanarshiftmode //Normal 256-color shift mode. Also with 16-bit DAC used(SVGA mode 3h)!
}; //All the getpixel functionality!

Handler decodegraphicspixels = loadplanarshiftmode; //Active graphics mode!

void updateVGAGraphics_Mode(VGA_Type *VGA)
{
	decodegraphicspixels = loadpixel_jmptbl[VGA->precalcs.GraphicsModeRegister_ShiftRegister|VGA->precalcs.planerenderer_16bitDAC]; //Apply the current mode(with 8/16-bit support)!
}

void VGA_GraphicsDecoder(VGA_Type *VGA, word loadedlocation) //Graphics decoder!
{
	decodegraphicspixels(); //Split the pixels from the buffer!
	((SEQ_DATA *)VGA->Sequencer)->graphicsx = &pixelbuffercontainer.pixelbuffer[0]; //Start rendering from the graphics buffer pixels at the current location!
}

void VGA_Sequencer_GraphicsMode(VGA_Type *VGA, SEQ_DATA *Sequencer, VGA_AttributeInfo *attributeinfo)
{
	attributeinfo->attribute = ((*Sequencer->graphicsx++)<<VGA_SEQUENCER_ATTRIBUTESHIFT); //Give the current pixel, loaded with our block!
	attributeinfo->fontpixel = 1; //Graphics attribute is always foreground by default!
}
