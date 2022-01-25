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

#include "headers/hardware/vga/vga.h" //Basic VGA stuff!
#include "headers/hardware/vga/vga_vram.h" //VRAM for building text fonts!
#include "headers/hardware/vga/vga_crtcontroller.h" //For character sizes!
#include "headers/hardware/vga/vga_vramtext.h" //Our VRAM text support!
#include "headers/support/log.h" //Logging support!
#include "headers/support/bmp.h" //BMP dumping support!

OPTINLINE byte reverse8_VGA(byte b) { //Reverses byte value bits!
	INLINEREGISTER byte temp=b, temp2=b; //Load our initial values!
	temp = ((temp & 0xF0) >> 4);
	temp2 = ((temp2 & 0x0F) << 4); //Swap 4 high and low bits!
	temp |= temp2; //Combine into 1!
	temp2 = temp; //Make both equal!
	temp = ((temp & 0xCC) >> 2);
	temp2 = ((temp2 & 0x33) << 2); //Swap 2 high and low bits of both nibbles!
	temp |= temp2; //Combine into 1!
	temp2 = temp; //Make both equal!
	temp = ((temp & 0xAA) >> 1);
	temp2 = ((temp2 & 0x55) << 1); //Swap odd and even bits!
	temp |= temp2; //Combine into 1!
	return temp; //Give the result!
}

OPTINLINE void fillgetcharxy_values(VGA_Type *VGA, int_32 address)
{
	byte attribute = 0; //0 or 1 (bit value 0x4 of the attribute, 1 bit)!
	byte doublewidthfont=0;
	word *getcharxy_values;
	getcharxy_values = &VGA->getcharxy_values[0]; //The values!
	word character = 0; //From 0-255!
	sbyte singlerow = -1; //Single row only?
	byte y=0; //From 0-32 (5 bits)!
	word precalcposition; //The position in the precalcs to use!
	if (likely(address!=-1)) //Single character row only?
	{
		if (((VGA->enable_SVGA == 2) || (VGA->enable_SVGA == 1)) && VGA->SVGAExtension) //ET3000/ET4000 enabled?
		{
			doublewidthfont = VGA->precalcs.doublewidthfont; //Enable the double width font to be used?
			//The fonts are in plane 2&3. Plane 2 is like the VGA plane, but plane 3 is only used with double width fonts(combined at the same address as plane 2)!
		}
		character = (word)((address >> 5) & 0xFF); //Only single character to edit!
		singlerow = (sbyte)(address&0x1F); //The single row to edit!
		y = singlerow; //Only process this row!
	}
	for (;character<0x100;) //256 characters (8 bits)!
	{
		attribute = 0; //0 or 1 (bit value 0x4 of the attribute, 1 bit)!
		for (;attribute<2;) //2 attributes!
		{
			if (unlikely(singlerow==-1)) y = 0; //Ignore the selected row if single isn't set!
			for (;y<0x20;) //33 rows!
			{
				uint_32 characterset_offset, add2000; //First, the character set, later translated to the real charset offset!
				if (GETBITS(VGA->registers->SequencerRegisters.REGISTERS.SEQUENCERMEMORYMODEREGISTER,1,1)) //Memory maps are enabled?
				{
					if (unlikely(attribute)) //Charset A? (bit 2 (value 0x4) set?)
					{
						characterset_offset = GETBITS(VGA->registers->SequencerRegisters.REGISTERS.CHARACTERMAPSELECTREGISTER,2,3);
						add2000 = GETBITS(VGA->registers->SequencerRegisters.REGISTERS.CHARACTERMAPSELECTREGISTER,5,1); //Charset A!
					}
					else //Charset B?
					{
						characterset_offset = GETBITS(VGA->registers->SequencerRegisters.REGISTERS.CHARACTERMAPSELECTREGISTER,0,3);
						add2000 = GETBITS(VGA->registers->SequencerRegisters.REGISTERS.CHARACTERMAPSELECTREGISTER,4,1); //Charset B!
					}
	
					characterset_offset <<= 1; //Calculated 0,4,8,c! Add room for 0x2000!
					characterset_offset |= add2000; //Add the 2000 mark!
					characterset_offset <<= 13; //Shift to the start position: 0,4,8,c,2,6,a,e!
				}
				else //Force character set #0?
				{
					characterset_offset = 0; //We're at the start of VRAM plane 2 always!
				}

				word character2;
				character2 = character; //Load!
				character2 <<= 5; //Multiply by 32!
				characterset_offset += character2; //Start of the character!
				characterset_offset += y; //Add the row!
				precalcposition = ((character << 6) | (y << 1) | attribute); //Where in the precalcs is our font row located?
				getcharxy_values[precalcposition] = reverse8_VGA(readVRAMplane(VGA,2,characterset_offset,0,0)); //Read the row from the character generator! Don't do anything special, just because we're from the renderer! Also reverse the data in the byte for a little speedup! Store the row for the character generator!
				if (doublewidthfont) //Double width font? Plane 3 adds 8 more foreground/background values for the other set!
				{
					getcharxy_values[precalcposition] |= ((reverse8_VGA(readVRAMplane(VGA, 3, characterset_offset, 0,0)))<<8); //Read the row from the character generator! Don't do anything special, just because we're from the renderer! Also reverse the data in the byte for a little speedup! Store the row for the character generator!
				}
				if (likely(singlerow!=-1)) goto nextattr; //Don't change the row if a single line is updated!
				++y; //Next row!
			}
			nextattr:
			++attribute; //Next attribute!
		}
		++character; //Next character!
		if (likely(singlerow!=-1)) return; //Stop on single character update!
	}
}

extern char capturepath[256]; //Capture path!

uint_32 textdisplay[2 * 32 * 256 * 16]; //All possible outputs!
void dumpVGATextFonts()
{
	char fullfilename[256];
	cleardata(&fullfilename[0],sizeof(fullfilename)); //Init!
	uint_32 displayindex;
	word *getcharxy_values;

	byte currentattribute;
	byte currentcharacter;
	byte currentrow;
	byte currentpixel;

	getcharxy_values = &getActiveVGA()->getcharxy_values[0]; //The values!
	for (displayindex=0;displayindex<NUMITEMS(textdisplay);displayindex++)
	{
		currentpixel = (displayindex&0xF); //Every pixel we change the font/back pixel!
		currentcharacter = ((displayindex>>4)&0xFF); //The character changes every 8 pixels!
		currentrow = ((displayindex>>12)&0x1F); //The row changes every 256 characters!
		currentattribute = ((displayindex>>17)&1); //The attribute changes every 32 rows!
		if (getActiveVGA())
		{
			if (getActiveVGA()->registers->specialCGAflags&1) //CGA font is used instead?
			{
				textdisplay[displayindex] = getcharxy_CGA(currentcharacter,currentpixel,currentrow&7)?RGB(0xFF,0xFF,0xFF):RGB(0x00,0x00,0x00);
			}
			else //VGA mode?
			{
				textdisplay[displayindex] = ((getcharxy_values[(currentcharacter<<6)|(currentrow<<1)|currentattribute]>>currentpixel)&1)?RGB(0xFF,0xFF,0xFF):RGB(0x00,0x00,0x00);
			}
		}
		else
		{
			textdisplay[displayindex] = ((getcharxy_values[(currentcharacter<<6)|(currentrow<<1)|currentattribute]>>currentpixel)&1)?RGB(0xFF,0xFF,0xFF):RGB(0x00,0x00,0x00);
		}
	}
	domkdir(capturepath); //Make sure we can log!
	safestrcpy(fullfilename,sizeof(fullfilename), capturepath); //Capture path!
	safestrcat(fullfilename,sizeof(fullfilename), "/");
	safestrcat(fullfilename,sizeof(fullfilename), "VRAMText"); //The full filename!
	writeBMP(fullfilename,&textdisplay[0],256*16,32*2,0,0,256*16); //Dump our font to the BMP file! We're two characters high (one for every font table) and 256 characters wide(total characters in the font).
}

void VGA_plane23updated(VGA_Type *VGA, uint_32 address) //Plane 2 has been updated?
{
	fillgetcharxy_values(VGA,address); //Update the character: character number is increased every 32 locations (5 bits row index), but we include the character set too(bits 13-15), so ignore that for correct character and character set handling!
}

void VGA_charsetupdated(VGA_Type *VGA)
{
	fillgetcharxy_values(VGA,-1); //Update all characters: the character sets are updated!	
}

byte getcharxy(VGA_Type *VGA, byte attribute, byte character, byte x, byte y) //Retrieve a characters x,y pixel on/off from table!
{
	static byte lastrow; //Last retrieved character row data!
	static word lastcharinfo = 0; //attribute|character|row|1, bit0=Set?
	INLINEREGISTER word lastlookup;
	INLINEREGISTER word charloc;
	INLINEREGISTER byte newx;
	newx = x; //Default: use the 9th bit if needed! Otherwise use the horizontal coordinate within the character!

	attribute >>= 3; //...
	attribute &= 1; //... Take bit 3 to get the actual attribute we need!
	if (unlikely((newx==8))) //Extra ninth bit?
	{
		if (likely(VGA->precalcs.textcharacterwidth==9)) //What width? 9 wide?
		{
			if ((GETBITS(VGA->registers->AttributeControllerRegisters.REGISTERS.ATTRIBUTEMODECONTROLREGISTER,2,1)==0) || ((character & 0xE0) != 0xC0)) return 0; //9th bit is always background or not a line graphics character?
			newx = 7; //Only 7 max!
		}
		else if (VGA->precalcs.textcharacterwidth == 8) //8 wide?
		{
			newx = 7; //Only 7 max!
		}
	}
	
	lastlookup = (((((character << 1) | attribute) << 5) | y) | 0x8000); //The last lookup!
	if (unlikely(lastcharinfo!=lastlookup)) //Row not yet loaded?
	{
		charloc = character; //Character position!
		charloc <<= 5;
		charloc |= y;
		charloc <<= 1;
		charloc |= attribute;
		lastrow = (byte)VGA->getcharxy_values[charloc]; //Lookup the new row!
		lastcharinfo = lastlookup; //Save the loaded row as the current row!
	}
	
	return (lastrow>>newx)&1; //Give bit!
}

OPTINLINE void VGA_dumpchar(VGA_Type *VGA, byte c)
{
	byte y=0;
	byte maxx=0;
	maxx = GETBITS(VGA->registers->SequencerRegisters.REGISTERS.CLOCKINGMODEREGISTER,0,1)?8:9; //8/9 dot mode!
	byte maxy=0;
	maxy = GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.MAXIMUMSCANLINEREGISTER,0,0x1F)+1; //Ammount of scanlines!
	for (;;)
	{
		char row[10]; //9-character row for the letter!
		char buf[2] = " "; //Zero terminated string!
		memset(&row,0,sizeof(row));
		byte x = 0;
		for (;;)
		{
			buf[0] = getcharxy(VGA,0xF,c,x,y)?'X':' '; //Load character pixel!
			safestrcat(row,sizeof(row),buf); //The character to use!
			if (unlikely(++x>=maxx)) goto nexty;
		}
		nexty:
		dolog("VRAM_CHARS","%s",row); //Log the row!
		if (unlikely(++y>=maxy)) break; //Done!
	}
	dolog("VRAM_CHARS",""); //Empty row!
}

void VGA_dumpstr(VGA_Type *VGA, char *s)
{
	if (!s) return;
	char *s2 = s; //Load string!
	while (likely(*s2!='\0')) //Not EOS?
	{
		VGA_dumpchar(VGA,*s2++); //Dump the next character!
	}
}

void VGA_dumpFonts()
{
	VGA_Type *VGA = getActiveVGA(); //Get the active VGA!
	if (VGA) //Gotten?
	{
		VGA_dumpstr(VGA,"Testing ABC!");
		return;
		//Dump of all available characters!
		int c=0;
		for (;;)
		{
			VGA_dumpchar(VGA,c);
			if (unlikely(++c>0xFF)) return; //Abort: done!
		}
	}
}
