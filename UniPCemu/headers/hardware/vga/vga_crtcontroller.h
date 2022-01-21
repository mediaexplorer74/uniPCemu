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

#ifndef VGA_DISPLAYGENERATION_CRTCONTROLLER_H
#define VGA_DISPLAYGENERATION_CRTCONTROLLER_H

#include "headers/types.h"

//Different signals!

#define VGA_SIGNAL_VTOTAL 0x1
#define VGA_SIGNAL_HTOTAL 0x2
#define VGA_SIGNAL_VRETRACESTART 0x4
#define VGA_SIGNAL_HRETRACESTART 0x8
#define VGA_SIGNAL_VRETRACEEND 0x10
#define VGA_SIGNAL_HRETRACEEND 0x20
#define VGA_SIGNAL_VBLANKSTART 0x40
#define VGA_SIGNAL_HBLANKSTART 0x80
#define VGA_SIGNAL_VBLANKEND 0x100
#define VGA_SIGNAL_HBLANKEND 0x200
#define VGA_SIGNAL_VSYNCRESET 0x400
#define VGA_SIGNAL_HSYNCRESET 0x800
#define VGA_VACTIVEDISPLAY 0x1000
#define VGA_HACTIVEDISPLAY 0x2000
#define VGA_OVERSCAN 0x4000
//Extra information to apply globally and periodically!
#define VGA_DISPLAYGRAPHICSMODE 0x8000
#define VGA_SIGNAL_BLANKING 0x10000
#define VGA_SIGNAL_BLANKINGSHIFT 16

#define VGA_DISPLAYRENDERSIZE 0x20000
//Display check
//Bits to check:
#define VGA_DISPLAYMASK (VGA_VACTIVEDISPLAY|VGA_HACTIVEDISPLAY)
//Bits set within above bits:
#define VGA_DISPLAYACTIVE (VGA_VACTIVEDISPLAY|VGA_HACTIVEDISPLAY)

//Simple masks for checking for H/VRetrace!
#define VGA_HRETRACEMASK (VGA_SIGNAL_HRETRACESTART|VGA_SIGNAL_HRETRACEEND)
#define VGA_VRETRACEMASK (VGA_SIGNAL_VRETRACESTART|VGA_SIGNAL_VRETRACEEND)
//Simple masks for checking for H/VBlank!
#define VGA_HBLANKMASK (VGA_SIGNAL_HBLANKSTART|VGA_SIGNAL_HBLANKEND)
#define VGA_VBLANKMASK (VGA_SIGNAL_VBLANKSTART|VGA_SIGNAL_VBLANKEND)
//Same
#define VGA_HBLANKRETRACEMASK (VGA_HBLANKMASK|VGA_HRETRACEMASK)
#define VGA_VBLANKRETRACEMASK (VGA_VBLANKMASK|VGA_VRETRACEMASK)

//Do we have a signal with these bits on!
#define VGA_SIGNAL_HASSIGNAL 0xFFF

//Pixel manipulation!
//OPTINLINE byte getVRAMScanlineMultiplier(VGA_Type *VGA); //VRAM scanline multiplier!
//OPTINLINE word getHorizontalStart(VGA_Type *VGA); //How many pixels to take off the active display x to get the start x!
//OPTINLINE word getHorizontalEnd(VGA_Type *VGA); //How many pixels to take off the display x to get the start of the right border?
//OPTINLINE word getVerticalDisplayEnd(VGA_Type *VGA);
//OPTINLINE word getVerticalBlankingStart(VGA_Type *VGA);
//OPTINLINE word getHorizontalBlankingStart(VGA_Type *VGA);
//OPTINLINE byte is_activedisplay(VGA_Type *VGA,word ScanLine, word x);
//OPTINLINE byte is_overscan(VGA_Type *VGA,word ScanLine, word x);
//OPTINLINE word getxres(VGA_Type *VGA);
//OPTINLINE word getyres(VGA_Type *VGA);
//OPTINLINE word getxresfull(VGA_Type *VGA); //Full resolution (border+active display area) width
//OPTINLINE word getyresfull(VGA_Type *VGA); //Full resolution (border+active display area) height
//OPTINLINE word getrowsize(VGA_Type *VGA); //Give the size of a row in VRAM!
//OPTINLINE word getTopWindowStart(VGA_Type *VGA); //Get Top Window Start scanline!

//word getHorizontalTotal(VGA_Type *VGA); //Get horizontal total (for calculating refresh speed timer)

//For precalcs only!
word get_display_y(VGA_Type *VGA, word scanline); //Vertical check!
word get_display_x(VGA_Type *VGA, word x); //Horizontal check!

//Character sizes in pixels!
//Character sizes in pixels!
#define getcharacterwidth(VGA) VGA->precalcs.characterwidth
#define gettextcharacterwidth(VGA) VGA->precalcs.textcharacterwidth
//8 or 9 dots per line?

#define getcharacterheight(VGA) VGA->precalcs.characterheight
//The character height!

//CGA/MDA compatibility support!
word get_display_CGAMDA_x(VGA_Type *VGA, word x);
word get_display_CGAMDA_y(VGA_Type *VGA, word y);
#endif
