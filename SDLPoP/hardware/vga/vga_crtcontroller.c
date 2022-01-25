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

#define VGA_CRTCONTROLLER

#include "headers/hardware/vga/vga.h"
#include "headers/hardware/vga/vga_precalcs.h" //Precalculation typedefs etc.
#include "headers/hardware/vga/vga_sequencer_textmode.h" //VGA Attribute controller!
#include "headers/hardware/vga/vga_crtcontroller.h"
#include "headers/hardware/vga/vga_cga_mda.h" //CGA/MDA timing support!
#include "headers/hardware/vga/svga/tseng.h" //Double width character clock support!

//Horizontal information!

OPTINLINE word getHorizontalDisplayStart(VGA_Type *VGA) //How many pixels to take off the active display x to get the start x!
{
	return VGA->precalcs.horizontaldisplaystart; //Horizontal start
}

OPTINLINE word getHorizontalDisplayEnd(VGA_Type *VGA) //What is the last character start x of the current line? (max character-1)
{
	return VGA->precalcs.horizontaldisplayend; //Horizontal End of display area!
}

OPTINLINE word getHorizontalBlankingStart(VGA_Type *VGA)
{
	return VGA->precalcs.horizontalblankingstart; //When to start blanking horizontally!
}

OPTINLINE word getHorizontalBlankingStartFinish(VGA_Type* VGA)
{
	return VGA->precalcs.horizontalblankingstartfinish; //When to start blanking horizontally!
}

OPTINLINE word getHorizontalBlankingEnd(VGA_Type *VGA)
{
	return VGA->precalcs.horizontalblankingend; //When to stop blanking horizontally after starting!
}

OPTINLINE word getHorizontalRetraceStart(VGA_Type *VGA) //When to start retracing (vblank)
{
	return VGA->precalcs.horizontalretracestart; //When to start vertical retrace!
}

OPTINLINE word getHorizontalRetraceStartFinish(VGA_Type* VGA) //When to start retracing (vblank)
{
	return VGA->precalcs.horizontalretracestartfinish; //When to start vertical retrace!
}

OPTINLINE word getHorizontalRetraceEnd(VGA_Type *VGA)
{
	return VGA->precalcs.horizontalretraceend; //When to stop vertical retrace.
}

OPTINLINE word getHorizontalTotal(VGA_Type *VGA)
{
	return VGA->precalcs.horizontaltotal; //Horizontal total (full resolution plus horizontal retrace)!
}

//Vertical information

OPTINLINE word getVerticalDisplayEnd(VGA_Type *VGA)
{
	return VGA->precalcs.verticaldisplayend; //Vertical Display End Register value!
}

OPTINLINE word getVerticalBlankingStart(VGA_Type *VGA)
{
	return VGA->precalcs.verticalblankingstart; //Vertical Blanking Start value!
}

OPTINLINE word getVerticalBlankingEnd(VGA_Type *VGA)
{
	return VGA->precalcs.verticalblankingend; //Vertical Blanking End value!
}

OPTINLINE word getVerticalRetraceStart(VGA_Type *VGA) //When to start retracing (vblank)
{
	return VGA->precalcs.verticalretracestart; //When to start vertical retrace!
}

OPTINLINE word getVerticalRetraceEnd(VGA_Type *VGA)
{
	return VGA->precalcs.verticalretraceend; //When to stop vertical retrace.
}

OPTINLINE word getVerticalTotal(VGA_Type *VGA)
{
	return VGA->precalcs.verticaltotal; //Full resolution plus vertical retrace!
}

//Full screen resolution = HTotal x VTotal.

word get_display_y(VGA_Type *VGA, word scanline) //Vertical check!
{
	if (CGAMDAEMULATION_ENABLED_CRTC(VGA)) return get_display_CGAMDA_y(VGA,scanline); //Give CGA timing when enabled!
	word signal;
	signal = VGA_OVERSCAN; //Init to overscan!
	if (scanline>=getVerticalTotal(VGA)) //VTotal?
	{
		signal |= VGA_SIGNAL_VTOTAL|VGA_SIGNAL_VSYNCRESET; //VTotal&Sync reset notify!
	}
	
	if (scanline==getVerticalRetraceStart(VGA)) //Retracing to line 0?
	{
		signal |= VGA_SIGNAL_VRETRACESTART; //Vertical retracing: do nothing!
	}
	
	if ((scanline&0xF)==getVerticalRetraceEnd(VGA))
	{
		signal |= VGA_SIGNAL_VRETRACEEND;
	}
	
	if (scanline==getVerticalBlankingStart(VGA))
	{
		signal |= VGA_SIGNAL_VBLANKSTART; //Start blanking!
	}
	
	if ((scanline&0x7F)==getVerticalBlankingEnd(VGA)) //Probably 7 bits used wide? Maybe 8?
	{
		signal |= VGA_SIGNAL_VBLANKEND; //End blanking!
	}

	//We're overscan or display!
	if (scanline<getVerticalDisplayEnd(VGA)) //Vertical overscan?
	{
		signal |= VGA_VACTIVEDISPLAY; //Vertical active display!
	}
	
	return signal; //What signal!
}

word get_display_x(VGA_Type *VGA, word x) //Horizontal check!
{
	if (CGAMDAEMULATION_ENABLED_CRTC(VGA)) return get_display_CGAMDA_x(VGA,x); //Give CGA timing when enabled!
	word signal;
	signal = VGA_OVERSCAN; //Init to overscan!
	word hchar = VGA->CRTC.charcolstatus[x<<1]; //What character?
	hchar >>= Tseng34k_doublecharacterclocks(VGA); //Double the width of the character clocks, if required!
	if (x>=getHorizontalTotal(VGA)) //HTotal?
	{
		signal |= VGA_SIGNAL_HTOTAL|VGA_SIGNAL_HSYNCRESET; //HTotal&Sync reset notify!
	}
	//First, check vertical/horizontal retrace, blanking, overline!
	if ((x>=getHorizontalRetraceStart(VGA)) && (x<getHorizontalRetraceStartFinish(VGA))) //Horizontal retrace start character clock?
	{
		signal |= VGA_SIGNAL_HRETRACESTART; //Retracing: do nothing!
	}
	else if ((hchar&0x1F)==getHorizontalRetraceEnd(VGA)) //End of horizontal retrace?
	{
		signal |= VGA_SIGNAL_HRETRACEEND; //End of horizontal retrace!
	}
	
	//Not special: we're processing display! Priority: blanking, display, overscan!
	
	if ((x==getHorizontalBlankingStart(VGA)) && (x<getHorizontalBlankingStartFinish(VGA))) //Horizontal blanking start character clock?
	{
		signal |= VGA_SIGNAL_HBLANKSTART; //Blanking!
	}
	else if ((hchar&(0x1F|((VGA->enable_SVGA!=3)?0x20:0)))==getHorizontalBlankingEnd(VGA)) //We end blanking AFTER this character! EGA uses 5 bits matching instead of 6-bit matching for horizontal blanking end!
	{
		signal |= VGA_SIGNAL_HBLANKEND; //End blanking!
	}
	
	//We're overscan or display!
	if ((x>=getHorizontalDisplayStart(VGA)) && (x<getHorizontalDisplayEnd(VGA))) //Display area?
	{
		signal |= VGA_HACTIVEDISPLAY; //Horizontal active display!
	}
	
	return signal; //What signal!
}