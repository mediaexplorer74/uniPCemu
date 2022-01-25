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

#ifndef VGA_PRECALCS_H
#define VGA_PRECALCS_H

#include "headers/hardware/vga/vga.h" //VGA basics!

//Where were we updated (what values to update?)
//ALL update? (for init)
#define WHEREUPDATED_ALL 0x0000
//Section update (from section below), flag. This updates all register within the section!
#define WHEREUPDATED_ALL_SECTION 0x20000

//Section (used when not all). This is OR-ed with the data index!
#define WHEREUPDATED_GRAPHICSCONTROLLER 0x1000
#define WHEREUPDATED_SEQUENCER 0x2000
#define WHEREUPDATED_CRTCONTROLLER 0x3000
#define WHEREUPDATED_ATTRIBUTECONTROLLER 0x4000
#define WHEREUPDATED_DAC 0x5000

//Rest registers (used when not all)
#define WHEREUPDATED_MISCOUTPUTREGISTER 0x6000
#define WHEREUPDATED_FEATURECONTROLREGISTER 0x7000
#define WHEREUPDATED_INPUTSTATUS0REGISTER 0x8000
#define WHEREUPDATED_INPUTSTATUS1REGISTER 0x9000
#define WHEREUPDATED_DACMASKREGISTER 0xA000
#define WHEREUPDATED_INDEX 0xB000

//CGA horizontal/vertical timing
#define WHEREUPDATED_CGACRTCONTROLLER_HORIZONTAL 0xC000
#define WHEREUPDATED_CGACRTCONTROLLER_VERTICAL 0xD000
//CGA misc CRT registers!
#define WHEREUPDATED_CGACRTCONTROLLER 0xE000
#define WHEREUPDATED_CRTCSPRITE 0xF000
#define WHEREUPDATED_MEMORYMAPPED 0x10000

//All index registers that can be updated!
#define INDEX_GRAPHICSCONTROLLER 0x1
#define INDEX_SEQUENCER 0x2
#define INDEX_CRTCONTROLLER 0x3
#define INDEX_ATTRIBUTECONTROLLER 0x4
#define INDEX_DACWRITE 0x5
#define INDEX_DACREAD 0x6
#define INDEX_CRTCSPRITE 0x7
//Not really an indexed register, but a single register:
#define INDEX_BANKREGISTERS 0x8

//Filter to get all above!
//The area where it was updated:
//The area/section to update?
#define WHEREUPDATED_AREA 0x1F000
//The updated register:
#define WHEREUPDATED_REGISTER 0x0FFF

//Register/section/all has been updated?
#define SECTIONISUPDATED(whereupdated,section) ((whereupdated&WHEREUPDATED_AREA)==section)
#define SECTIONISUPDATEDFULL(whereupdated,section,fullupdated) (SECTIONISUPDATED(whereupdated,section)||fullupdated)

//Simple register updated?
#define REGISTERUPDATED(whereupdated,section,reg,fullupdated) ((whereupdated==(section|reg))||fullupdated)

//Section update entirely, this section only?
#define UPDATE_SECTION(val,section) (((val&WHEREUPDATED_ALL_SECTION)==WHEREUPDATED_ALL_SECTION) && (val==section))
#define UPDATE_SECTIONFULL(val,section,fullupdate) (UPDATE_SECTION(val,section)||fullupdate)

typedef struct //Contains the precalculated values!
{
	word divideby9[0x10000]; //Divide by 9 precalcs!
	uint_32 DAC[0x100]; //Full DAC saved lookup table!
	uint_32 effectiveDAC[0x100]; //The same DAC as above, but with color conversions applied for rendering!
	uint_32 effectiveMDADAC[0x100]; //The same DAC as above, but with b/w conversions applied for rendering, also it's index is changed to the R/G/B 256-color greyscale index!
	//Attribute controller precalcs!
	byte attributeprecalcs[0x8000]; //All attribute precalcs!

	byte graphicsmode; //Are we a graphics mode?
	byte textmode; //Are we a text mode?
	
	word scanline; //Current scanline rendering after all extra effects!
	
	byte overscancolor; //Default overscan color!
	
	byte characterwidth; //Character width for the CRTC!
	byte textcharacterwidth; //Text mode character width(can be extended past 8/9 pixels/char).
	byte characterheight; //Character height!

	uint_32 startaddress; //Combination of start address high&low register for normal and top screen (reset) operations!
	uint_32 VGAstartaddress; //The start address as specified for the VGA!
	
	//CRT Controller registers:
	//Horizontal timing information
	uint_32 horizontaldisplaystart;
	uint_32 horizontaldisplayend;
	uint_32 horizontalblankingstart;
	uint_32 horizontalblankingstartfinish;
	uint_32 horizontalblankingend;
	uint_32 horizontalretracestart;
	uint_32 horizontalretracestartfinish;
	uint_32 horizontalretraceend;
	uint_32 horizontaltotal;
	
	//Vertical timing information
	word verticaldisplayend;
	word verticalblankingstart;
	word verticalblankingend;
	word verticalretracestart;
	word verticalretraceend;
	word verticaltotal;
	
	byte characterclockshift; //Division 0,1 or 2 for the horizontal character clock!
	byte BWDModeShift; //Memory mode shift for the horizontal character clock in B/W/DW modes!

	//Extra information
	uint_32 rowsize;
	uint_32 VGArowsize;
	word topwindowstart;
	byte scandoubling;
	//Sequencer_textmode_cursor (CRTC):
	uint_32 cursorlocation; //Cursor location!
	byte pixelshiftcount; //Save our precalculated value!
	byte presetrowscan; //Row scanning boost!
	byte colorselect54; //Precalculate!
	byte colorselect76; //Precalculate!
	byte lastDACMask; //To determine if the DAC Mask is updated or not!
	
	byte renderedlines; //Actual ammount of lines rendered, graphics mode included!
	
	//Extra info for debugging!
	uint_32 mainupdate; //Main update counter for debugging updates to VRAMMode!

	//Register data used during rendering and barely updated at all:
	byte AttributeModeControlRegister_ColorEnable8Bit;
	byte CursorStartRegister_CursorScanLineStart;
	byte CursorEndRegister_CursorScanLineEnd;
	byte CursorStartRegister_CursorDisable;
	byte GraphicsModeRegister_ShiftRegister;
	byte PresetRowScanRegister_BytePanning;
	byte AttributeModeControlRegister_PixelPanningMode;
	byte CRTCModeControlRegister_SLDIV; //Scanline divisor!
	byte ClockingModeRegister_DCR; //Dot Clock Rate!
	byte LastMiscOutputRegister; //Last value written to the Misc Output Register!
	byte LastCGAFlags; //Last used CGA flags!
	byte LastMDAFlags; //Last used MDA flags!
	byte graphicsmode_nibbled; //Allow nibbled reversal mask this must allow values 1&2 to be decreased, else 0 with text modes!
	uint_32 VRAMmask; //The mask used for accessing VRAM!
	uint_32 extrasignal; //Graphics mode display bit!
	byte AttributeController_16bitDAC; //Enable the 16-bit/8-bit DAC color formation in the Attribute Controller?
	byte planerenderer_16bitDAC; //Plane render variant of the above!
	byte VideoLoadRateMask; //When to load the new pixels (bitmask to result in zero to apply)!
	byte BypassPalette; //Bypass the palette?
	byte linearmode; //Linear mode enabled (linear memory window)? Bit 1=1: Use high 4 bits for bank, else bank select. Bit0=1: Use contiguous memory, else VGA mapping.
	word DACmode; //The current DAC mode: Bits 0-1: 3=16-bit, 2=15-bit, 1/0: 8-bit(normal VGA DAC). Bit 4: 1=Latch every two pixel clocks, else every pixel clock.
	word effectiveDACmode; //The current DAC mode: Bits 0-1: 3=16-bit, 2=15-bit, 1/0: 8-bit(normal VGA DAC). Bit 4: 1=Latch every two pixel clocks, else every pixel clock.
	byte MemoryClockDivide; //Memory address clock divide by 0, 1 or 2(Stacked on top of the normal memory address clock).
	uint_32 VMemMask; //Extended VRAMMask.
	byte charwidthupdated; //Is the charwidth updated(for SVGA)?
	byte recalcScanline; //Recalculate the scanline data?
	byte WriteMemoryMode; //Write memory mode!
	byte ReadMemoryMode; //Read memory mode!
	byte EGA_DisableInternalVideoDrivers; //Disable internal video drivers(EGA)?
	byte use14MHzclock; //14MHz clocking?
	byte enableInterlacing; //Enable interlacing?
	byte doublewidthfont; //Enable double width font to be used(index 36h bit 3 of the Tseng chips being set)?
	byte extendedfont; //Enable extended font dots/char(bits 1-2 of the Sequencer register 6 of the Tseng chips)? 
	byte charactercode_16bit; //Render the character code as 16-bit character codes?
	byte emulatedDACextrabits; //6 or 8-bit mask!
	byte turnDACoff; //Turn the DAC output off?
	uint_32 SC15025_pixelmaskregister; //Pixel mask register!
	uint_32 linearmemorybase; //W32 Linear memory base address!
	uint_32 linearmemorymask; //W32 Linear memory base address mask on the physical memory address!
	uint_32 linearmemorysize; //W32 Linear memory size!
	byte MMUregs_enabled; //MMU registers enabled?
	byte MMU012_enabled; //MMU0-2 enabled?
	uint_32 MMU012_aperture[4]; //MMU 0-2 aperture base addresses
	byte MMU0_aperture_linear; //MMU 0 aperture is in linear mode
	byte MMU1_aperture_linear; //MMU 1 aperture is in linear mode
	byte MMU2_aperture_linear; //MMU 2 aperture is in linear mode
	uint_32 extraSegmentSelectLines; //Extra bits to appear on the memory address bus for VRAM when performing the Segment Select Register inputs.
	//Image port precalcs
	uint_32 imageport_startingaddress;
	uint_32 imageport_transferlength; //Transfer length for a scanline, in bytes!
	uint_32 imageport_rowoffset; //Length of a transferred scanline in VRAM, in bytes!
	byte imageport_interlace; //Enable interlacing of data in the image port!
	byte disableVGAlegacymemoryaperture; //Disable the VGA legacy memory aperture?
	uint_32 VRAM_limit; //Limited if non-zero!
	//CRTC/Sprite precalcs
	byte SpriteCRTCEnabled; //Is the sprite/crtc window enabled? 0=Disabled, 1=Sprite, 2=CRTC
	word SpriteCRTChorizontaldisplaydelay; //Horizontal delay in clocks until the horizontal window starts!
	word SpriteCRTChorizontaldisplaypreset; //How many pixels to skip once display starts
	word SpriteCRTChorizontalwindowwidth; //The size of the horizontal window
	word SpriteCRTCverticaldisplaydelay; //Horizontal delay in clocks until the horizontal window starts!
	word SpriteCRTCverticaldisplaypreset; //How many pixels to skip once display starts
	word SpriteCRTCverticalwindowheight; //The size of the horizontal window
	uint_32 SpriteCRTCstartaddress; //Start address of the image in video memory!
	uint_32 SpriteCRTCrowoffset; //Row offset for each row in the video memory!
	byte SpriteCRTCrowheight; //Row height in scanlines for each row to display!
	byte SpriteCRTCpixelwidth; //Pixel width in pixels for each pixel to display!
	byte SpriteCRTCpixeldepth; //The pixel depth for CRTC mode
	byte SpriteSize; //64 or 128 pixels!
	byte SpriteCRTCpixelpannning; //Horizontal pixel panning for CRTCB/Sprite!
	byte VerticalRetraceInterruptSource; //0=Normal EGA/VGA-compatible, 1=CRTC window retrace point reached.
	Handler Tseng4k_accelerator_tickhandler; //Accelerator tick handler!
} VGA_PRECALCS; //VGA pre-calculations!

typedef void (*VGA_calcprecalcsextensionhandler)(void *VGA, uint_32 whereupdated); //Calculate them!

void VGA_calcprecalcs(void *VGA, uint_32 whereupdated); //Calculate them!
void VGA_LOGCRTCSTATUS(); //Log the current CRTC precalcs status!
void dump_CRTCTiming(); //Dump the full CRTC timing calculated from the precalcs!

void VGA_calcprecalcs_CRTC(void *VGA); //Precalculate CRTC precalcs!

#endif
