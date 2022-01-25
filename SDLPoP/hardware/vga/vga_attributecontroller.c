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

#define VGA_ATTRIBUTECONTROLLER

#include "headers/hardware/vga/vga.h"
#include "headers/hardware/vga/vga_attributecontroller.h" //Our own typedefs!
#include "headers/hardware/vga/vga_sequencer_graphicsmode.h" //For masking planes!
#include "headers/hardware/vga/vga_precalcs.h" //Precalculation typedefs etc.
#include "headers/hardware/vga/vga_cga_mda.h" //CGA/MDA attribute support!
#include "headers/support/log.h" //Debugger!
extern byte LOG_RENDER_BYTES; //vga_screen/vga_sequencer_graphicsmode.c

OPTINLINE byte getattributeback(byte textmode, byte attr,byte filter)
{
	//Only during text mode: shift!
	attr >>= textmode; //Shift high nibble to low nibble with text mode!
	attr &= filter; //Apply filter!
	return attr; //Need attribute registers below used!
}

//Dependant on mode control register and underline location register
void VGA_AttributeController_calcAttributes(VGA_Type *VGA)
{
	byte pixelon, charinnery, currentblink;
	word Attribute; //This changes!
	
	byte color256;
	color256 = GETBITS(VGA->registers->AttributeControllerRegisters.REGISTERS.ATTRIBUTEMODECONTROLREGISTER,6,1); //8-bit colors?

	byte enableblink;
	enableblink = GETBITS(VGA->registers->AttributeControllerRegisters.REGISTERS.ATTRIBUTEMODECONTROLREGISTER,3,1); //Enable blink?	
	
	byte underlinelocation;
	underlinelocation = GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.UNDERLINELOCATIONREGISTER,0,0x1F); //Underline location is the value desired minus 1!
	
	byte textmode;
	textmode = (GETBITS(VGA->registers->AttributeControllerRegisters.REGISTERS.ATTRIBUTEMODECONTROLREGISTER,0,1)==0)?4:4; //Text mode? Also the number of bits to shift to get the high nibble, if used!

	byte monomode;
	monomode = GETBITS(VGA->registers->AttributeControllerRegisters.REGISTERS.ATTRIBUTEMODECONTROLREGISTER,1,1); //Monochrome emulation mode(attribute) with b/w emulation(misc output)?

	byte *attributeprecalcs;
	attributeprecalcs = &VGA->precalcs.attributeprecalcs[0]; //The attribute precalcs!	

	byte fontstatus;
	byte paletteenable=0;
	byte palette54=0; //palette 5-4 used?
	byte colorselect54=0; //Color select bits 5-4!
	byte colorselect76=0; //Color select bits 7-6!
	byte backgroundfilter=0;
	byte VGADisabled = 0;
	byte palettecopy[0x10];
	if (CGAMDAEMULATION_RENDER(VGA)) VGADisabled = 1; //Disable the VGA color processing with CGA/MDA!

	paletteenable = GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.ATTRIBUTECONTROLLERTOGGLEREGISTER,5,1); //Internal palette enabled?
	INLINEREGISTER byte CurrentDAC; //Current DAC to use!
	if (paletteenable) //Precalcs for palette?
	{
		for (CurrentDAC = 0;CurrentDAC < 0x10;++CurrentDAC) //Copy the palette internally for fast reference!
		{
			palettecopy[CurrentDAC] = GETBITS(VGA->registers->AttributeControllerRegisters.REGISTERS.PALETTEREGISTERS[CurrentDAC],0,0x3F); //Translate base index into DAC Base index!
		}
		palette54 = GETBITS(VGA->registers->AttributeControllerRegisters.REGISTERS.ATTRIBUTEMODECONTROLREGISTER,7,1); //Use palette bits 5-4?
		if (palette54) //Use palette bits 5-4?
		{
			colorselect54 = VGA->precalcs.colorselect54; //Retrieve pallete bits 5-4!
		}
		colorselect76 = VGA->precalcs.colorselect76; //Retrieve pallete bits 7-6!
	}
	paletteenable &= ((VGA->precalcs.BypassPalette)?0:1); //Bypass the palette if needed!

	backgroundfilter = ((~(enableblink<<3))&0xF); //Background filter depends on blink & full background when not in monochrome mode!

	INLINEREGISTER byte colorplanes;
	colorplanes = VGA->registers->AttributeControllerRegisters.REGISTERS.COLORPLANEENABLEREGISTER; //Read colorplane 256-color!
	colorplanes &= 0xF; //Only 4 bits can be used!

	INLINEREGISTER word pos;
	INLINEREGISTER word pos2;

	if (VGADisabled) //CGA/MDA mode?
	{
		for (pixelon = 0;pixelon<2;++pixelon) //All values of pixelon!
		{
			for (currentblink = 0;currentblink<2;++currentblink) //All values of currentblink!
			{
				for (charinnery = 0;charinnery<0x20;++charinnery)
				{
					//Take the 
					pos2 = charinnery; //Set!
					pos2 <<= 1; //Create room!
					pos2 |= currentblink; //Add!
					pos2 <<= 1; //Create room!
					pos2 |= pixelon; //Add!

					for (Attribute = 0;Attribute<0x100;++Attribute)
					{
						fontstatus = pixelon; //What font status? By default this is the font/back status!

											  //Underline(text mode)&Off capability!
						if (unlikely(monomode)) //Only in mono mode do we have underline capability and other stuff!
						{
							if (likely(textmode)) //This is active in text mode only!
							{
								if (unlikely(charinnery == underlinelocation)) //Underline (Non-graphics monochrome mode only)? Ignore textmode?
								{
									if (unlikely((Attribute & 7) == 1)) //Underline used for this character? Bits 6-4=0(not according to seasip.info/VintagePC/mda.html, so ignore that fact!) and 2-0=1 only according to freeVGA!
									{
										fontstatus = 1; //Force font color for underline WHEN FONT ON (either <blink enabled and blink ON> or <blink disabled>)!
									}
								}
							}
							if (unlikely((Attribute & 0x88) == Attribute)) //Are we always displayed as background (values 0, 8, 0x80 and 0x88)?
							{
								fontstatus = 0; //Force background!
							}
						}

						//Blinking capability! Applies to the font/background only!
						if (enableblink) //Blink affects font?
						{
							if (unlikely(getattributeback(textmode, (byte)Attribute, 0x8))) //Blink enabled?
							{
								fontstatus &= currentblink; //Need blink on to show foreground!
							}
						}

						//Determine pixel font or back color to PAL index!
						if (unlikely(fontstatus))
						{
							CurrentDAC = (byte)Attribute; //Load attribute!
						}
						else
						{
							CurrentDAC = getattributeback(textmode, (byte)Attribute, backgroundfilter); //Back!
						}

						CurrentDAC &= colorplanes; //Apply color planes(4-bits)!

						if (unlikely(paletteenable == 0)) goto skippalette; //Internal palette enable?
						//Use original 16 color palette!
						CurrentDAC = palettecopy[CurrentDAC]; //Translate base index into DAC Base index!

						//CGA doesn't have full pallette access, so skip it!
						pos = Attribute;
						pos <<= 7; //Create room for the global data!
						pos |= pos2; //Apply charinner_y, currentblink and pixelon!
						//attribute,charinnery,currentblink,pixelon: 8,5,1,1: Less shifting at a time=More speed!
						attributeprecalcs[pos] = CurrentDAC; //Our result for this combination!
					}
				}
			}
		}
	}
	else //Normal VGA palette?
	{
		for (pixelon=0;pixelon<2;++pixelon) //All values of pixelon!
		{
			for (currentblink=0;currentblink<2;++currentblink) //All values of currentblink!
			{
				for (charinnery=0;charinnery<0x20;++charinnery)
				{
					//Take the 
					pos2 = charinnery; //Set!
					pos2 <<= 1; //Create room!
					pos2 |= currentblink; //Add!
					pos2 <<= 1; //Create room!
					pos2 |= pixelon; //Add!

					for (Attribute=0;Attribute<0x100;++Attribute)
					{
						fontstatus = pixelon; //What font status? By default this is the font/back status!

						//Underline(text mode)&Off capability!
						if (unlikely(monomode)) //Only in mono mode do we have underline capability and other stuff!
						{
							if (likely(textmode)) //This is active in text mode only!
							{
								if (unlikely(charinnery==underlinelocation)) //Underline (Non-graphics monochrome mode only)? Ignore textmode?
								{
									if (unlikely((Attribute&7)==1)) //Underline used for this character? Bits 6-4=0(not according to seasip.info/VintagePC/mda.html, so ignore that fact!) and 2-0=1 only according to freeVGA!
									{
										fontstatus = 1; //Force font color for underline WHEN FONT ON (either <blink enabled and blink ON> or <blink disabled>)!
									}
								}
							}
							if (unlikely((Attribute&0x88)==Attribute)) //Are we always displayed as background (values 0, 8, 0x80 and 0x88)?
							{
								fontstatus = 0; //Force background!
							}
						}

						//Blinking capability! Applies to the font/background only!
						if (likely(enableblink)) //Blink affects font?
						{
							if (unlikely(getattributeback(textmode,(byte)Attribute,0x8))) //Blink enabled?
							{
								fontstatus &= currentblink; //Need blink on to show foreground!
							}
						}

						//Determine pixel font or back color to PAL index!
						if (unlikely(fontstatus))
						{
							CurrentDAC = (byte)Attribute; //Load attribute!
						}
						else
						{
							CurrentDAC = getattributeback(textmode,(byte)Attribute,backgroundfilter); //Back!
						}

						CurrentDAC &= colorplanes; //Apply color planes(4-bits)!

						if (unlikely(paletteenable==0)) goto skippalette; //Internal palette enable?
						//Use original 16 color palette!
						CurrentDAC = palettecopy[CurrentDAC]; //Translate base index into DAC Base index!

						if (unlikely(VGADisabled)) goto skippalette; //Are we not on a CGA? Skip the further lookup!
						if (unlikely(color256)) //8-bit colors and not monochrome mode?
						{
							CurrentDAC &= 0xF; //Take 4 bits only!
						}
						else //Process fully to a DAC index!
						{
							//First, bit 4&5 processing if needed!
							if (palette54) //Bit 4&5 map to the C45 field of the Color Select Register, determined by bit 7?
							{
								CurrentDAC &= ~0x30; //Clear bits 5-4!
								CurrentDAC |= colorselect54; //Use them as 4th&5th bit!
							}
							//Else: already 6 bits wide fully!
							CurrentDAC &= 0x3F; //Clear the high 2 bits for inserting the selected base!
							//Finally, bit 6&7 always processing!
							CurrentDAC |= colorselect76; //Apply bits 6&7!
						}

						skippalette:
						pos = Attribute;
						pos <<= 7; //Create room for the global data!
						pos |= pos2; //Apply charinner_y, currentblink and pixelon!
						//attribute,charinnery,currentblink,pixelon: 8,5,1,1: Less shifting at a time=More speed!
						attributeprecalcs[pos] = CurrentDAC; //Our result for this combination!
					}
				}
			}
		}
	}
}

OPTINLINE byte VGA_getAttributeDACIndex(VGA_AttributeInfo *Sequencer_attributeinfo, VGA_Type *VGA)
{
	return VGA->precalcs.attributeprecalcs[Sequencer_attributeinfo->attribute|Sequencer_attributeinfo->lookupprecalcs|Sequencer_attributeinfo->fontpixel]; //Give the data from the lookup table!
}

byte VGA_AttributeController_16bit(VGA_AttributeInfo *Sequencer_attributeinfo, VGA_Type *VGA)
{
	INLINEREGISTER byte curnibble;
	static word latchednibbles = 0; //What nibble are we currently?
	INLINEREGISTER word temp;
	//First, execute the shift and add required in this mode!
	temp = latchednibbles;
	temp <<= 4; //Shift high!
	temp |= (VGA_getAttributeDACIndex(Sequencer_attributeinfo, VGA) & 0xF); //Latch to DAC Nibble!
	Sequencer_attributeinfo->attribute = latchednibbles = temp; //Look the DAC Index up!
	Sequencer_attributeinfo->attributesize = 2; //16-bit attribute size, so 3 extra clocks!
	curnibble = Sequencer_attributeinfo->latchstatus; //Load the current latch status!
	++curnibble;
	curnibble &= 3; //4 nibbles form one color value!
	return (Sequencer_attributeinfo->latchstatus = curnibble); //Give us the next nibble, when needed, please!
}

byte VGA_AttributeController_8bit(VGA_AttributeInfo *Sequencer_attributeinfo, VGA_Type *VGA)
{
	static byte latchednibbles = 0; //What nibble are we currently?
	INLINEREGISTER byte temp;
	//First, execute the shift and add required in this mode!
	temp = latchednibbles;
	temp <<= 4; //Shift high!
	temp |= (VGA_getAttributeDACIndex(Sequencer_attributeinfo,VGA)&0xF); //Latch to DAC Nibble!
	Sequencer_attributeinfo->attribute = latchednibbles = temp; //Look the DAC Index up!
	Sequencer_attributeinfo->attributesize = 1; //8-bit attribute size, so 1 extra clock!
	return (Sequencer_attributeinfo->latchstatus ^= 1); //Give us the next nibble, when needed, please!
}

byte VGA_AttributeController_4bit(VGA_AttributeInfo *Sequencer_attributeinfo, VGA_Type *VGA)
{
	Sequencer_attributeinfo->attribute = VGA_getAttributeDACIndex(Sequencer_attributeinfo, VGA); //Look the DAC Index up!
	Sequencer_attributeinfo->attributesize = 0; //4-bit attribute size!
	return 0; //We're ready to execute: we contain a pixel to plot!
}
