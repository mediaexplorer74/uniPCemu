/*

Copyright (C) 2019 - 2021 Superfury

This file is part of The Common Emulator Framework.

The Common Emulator Framework is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

The Common Emulator Framework is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with The Common Emulator Framework.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "headers/emu/gpu/gpu.h" //GPU typedefs etc.
#include "headers/emu/gpu/gpu_text.h" //Text support!

extern GPU_type GPU; //GPU!
extern GPU_TEXTSURFACE *BIOS_Surface; //Our very own BIOS Surface!

extern byte allcleared; //All allocations cleared?

/*

16 color palette for keyboard etc.

*/

uint_32 getemucol16(byte color) //Special for the emulator, like the keyboard presets etc.!
{
	switch (color&0xF)
	{
		case 1: return RGB(0x00,0x00,0xAA);
		case 2: return RGB(0x00,0xAA,0x00);
		case 3: return RGB(0x00,0xAA,0xAA);
		case 4: return RGB(0xAA,0x00,0x00);
		case 5: return RGB(0xAA,0x00,0xAA);
		case 6: return RGB(0xAA,0x55,0x00);
		case 7: return RGB(0xAA,0xAA,0xAA);
		case 8: return RGB(0x55,0x55,0x55);
		case 9: return RGB(0x55,0x55,0xFF);
		case 0xA: return RGB(0x55,0xFF,0x55);
		case 0xB: return RGB(0x55,0xFF,0xFF);
		case 0xC: return RGB(0xFF,0x55,0x55);
		case 0xD: return RGB(0xFF,0x55,0xFF);
		case 0xE: return RGB(0xFF,0xFF,0x55);
		case 0xF: return RGB(0xFF,0xFF,0xFF);
		case 0:
		default:
			 return RGB(0x00,0x00,0x00);
	}
	return RGB(0x00,0x00,0x00); //Shouldn't be here, but just in case!
}

/*

Now special stuff for the emulator! Used by the emulator VGA output!

*/

word emu_x, emu_y; //EMU coordinates!

//Locking support for block actions!
void EMU_locktext()
{
	if (allcleared) return; //Abort when all is cleared!
	GPU_text_locksurface(BIOS_Surface);
}

void EMU_unlocktext()
{
	if (allcleared) return; //Abort when all is cleared!
	GPU_text_releasesurface(BIOS_Surface);
}

void EMU_clearscreen()
{
	if (BIOS_Surface)
	{
		if (allcleared) return; //Abort when all is cleared!
		GPU_text_locksurface(BIOS_Surface);
		GPU_textclearscreen(BIOS_Surface); //Clear the screen!
		GPU_text_releasesurface(BIOS_Surface);
	}
}

void EMU_textcolor(byte color)
{
	GPU.GPU_EMU_color = color; //Text mode font color!
}

void EMU_gotoxy(word x, word y)
{
	if (BIOS_Surface)
	{
		if (allcleared) return; //Abort when all is cleared!
		GPU_textgotoxy(BIOS_Surface, x, y); //Goto xy!
		emu_x = x;
		emu_y = y; //Update our coordinates!
	}
}

void EMU_getxy(word *x, word *y)
{
	*x = emu_x;
	*y = emu_y;
}

void GPU_EMU_printscreen(sword x, sword y, char *text, ...) //Direct text output (from emu)!
{
	if (allcleared)
	{
		return; //Abort when all is cleared!
	}
	char buffer[256]; //Going to contain our output data!
	va_list args; //Going to contain the list!
	va_start (args, text); //Start list!
	vsnprintf (buffer,sizeof(buffer), text, args); //Compile list!

	//First, check for returning cursor!
	if ((x == -1) && (y == -1)) //Dynamic coordinates?
	{
		EMU_gotoxy(emu_x, emu_y); //Continue at emu coordinates!
	}
	else
	{
		EMU_gotoxy(x, y); //Goto coordinates!
	}

	GPU_textprintf(BIOS_Surface,getemucol16(GPU.GPU_EMU_color&0xF),getemucol16((GPU.GPU_EMU_color>>4)&0xF),"%s",buffer); //Show our output using the full font color!

	if (BIOS_Surface) //Allowed to update?
	{
		emu_x = BIOS_Surface->x;
		emu_y = BIOS_Surface->y; //Update coordinates for our continuing!
	}
	va_end(args); //Destroy list!
}