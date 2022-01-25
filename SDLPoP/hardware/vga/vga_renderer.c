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

#define VGA_RENDERER

#include "headers/types.h" //Basic types!
#include "headers/hardware/vga/vga.h" //VGA!
#include "headers/hardware/vga/vga_renderer.h" //Ourselves!
#include "headers/hardware/vga/vga_sequencer_graphicsmode.h" //Text mode!
#include "headers/hardware/vga/vga_sequencer_textmode.h" //Text mode!
#include "headers/hardware/vga/vga_attributecontroller.h" //Attribute controller!
#include "headers/hardware/vga/vga_crtcontroller.h" //CRT Controller for finishing up!
#include "headers/hardware/vga/vga_dacrenderer.h" //DAC support!
#include "headers/hardware/vga/vga_vram.h" //VGA VRAM support!
#include "headers/hardware/vga/vga_cga_mda.h" //CGA/MDA support!
#include "headers/hardware/vga/vga_cga_ntsc.h" //CGA NTSC support!
#include "headers/support/log.h" //Logging support!
#include "headers/hardware/vga/svga/tseng.h" //ET3/4K DWord mode support! Also ET4K accelerator support!
#include "headers/support/zalloc.h" //Memory protection support for vertical refresh rate!

//Basic timings support(originally emu_VGA.c)
#include "headers/emu/gpu/gpu.h" //Basic GPU!
#include "headers/emu/gpu/gpu_renderer.h" //Renderer support!
#include "headers/emu/gpu/gpu_text.h" //Emulator support!
#include "headers/emu/gpu/gpu_framerate.h" //Framerate support!
#include "headers/hardware/pic.h" //IRQ support!
#include "headers/support/highrestimer.h" //Automatic timer support!
#include "headers/cpu/cpu.h" //Currently emulated CPU for wait states!

//Are we disabled?
#define HW_DISABLED 0

//Limit the VGA to run slower on too slow PCs? Check at least this many pixels if defined before locking on the speed!
#define LIMITVGA 1000

#define CURRENTBLINK(VGA) VGA->blink32

//Do color mode or B/W mode DAC according to our settings!
#define VGA_DAC(VGA,DACValue) (VGA->precalcs.effectiveDAC[(DACValue)])

extern GPU_type GPU; //GPU!

DOUBLE VGA_clocks[4] = {
			0.0, //25MHz: VGA standard clock
			0.0, //28MHz: VGA standard clock
			0.0, //external clock: not connected!
			0.0 //Unused
			}; //Our clocks!

uint_32 CGALineSize = 0; //How long is our line!
byte CGALineBuffer[2048]; //Full CGA scanline buffer!
uint_32 CGAOutputBuffer[2048]; //Full CGA NTSC buffer!

VGA_clockrateextensionhandler VGA_calcclockrateextensionhandler; //The clock rate extension handler!

/*

Renderer mini-optimizations.

*/

DOUBLE oldrate = 0.0f; //The old rate we're using!

DOUBLE VGA_timing = 0.0; //No timing yet!
DOUBLE VGA_debugtiming = 0.0; //Debug countdown if applyable!
byte VGA_debugtiming_enabled = 0; //Are we applying right now?
float VGA_rendertiming = 0.0f; //Time for the renderer to tick!

TicksHolder VGA_test;
float VGA_limit = 0.0f; //Our speed factor!

#ifdef LIMITVGA
uint_32 passedcounter = LIMITVGA; //Times to check for speed with LIMITVGA
#endif

byte VGA_vtotal = 0; //Are we detecting VTotal?

byte currentVGASpeed = 0; //Default: run at 100%!
byte SynchronizationMode = 0; //Synchronization mode when used: 0=Old style, 1=New style

/*

Basic renderer functionality

*/

void initVGAclocks(byte extension)
{
	if (extension!=3) //VGA clock?
	{
		VGA_clocks[0] = VGA25MHZ; //25MHZ clock!
		VGA_clocks[1] = VGA28MHZ; //28MHZ clock!
	}
	else //EGA clock?
	{
		VGA_clocks[0] = MHZ14; //14MHz clock!
		VGA_clocks[1] = 16257000.0f; //16MHz clock
	}
	VGA_clocks[2] = 0.0; //Unused!
	VGA_clocks[3] = 0.0; //Unused!
}

DOUBLE VGA_VerticalRefreshRate(VGA_Type *VGA) //Scanline speed for one line in Hz!
{
	DOUBLE result=0.0;
	byte clock;
	//Horizontal Refresh Rate=Clock Frequency (in Hz)/horizontal pixels
	//Vertical Refresh rate=Horizontal Refresh Rate/total scan lines!
	if (unlikely(memprotect(VGA,sizeof(*VGA),NULL)==0)) //No VGA?
	{
		return 0.0; //Remove VGA Scanline counter: nothing to render!
	}
	if (unlikely(VGA_calcclockrateextensionhandler))
	{
		if (unlikely(VGA_calcclockrateextensionhandler(VGA)!=0.0)) return VGA_calcclockrateextensionhandler(VGA); //Give the extended clock if needed!
		else result = VGA_clocks[(GETBITS(VGA->registers->ExternalRegisters.MISCOUTPUTREGISTER,2,3) & 3)]; //VGA clock!
	}
	else
	{
		clock = (GETBITS(VGA->registers->ExternalRegisters.MISCOUTPUTREGISTER,2,3)&3); //What clock is specified?
		result = VGA_clocks[clock]; //VGA clock, if used!
		VGA->precalcs.use14MHzclock = (((VGA->enable_SVGA==3) && (clock==0)) || (VGA->enable_SVGA==4)); //Use 14MHz motherboard clock when supplied!
	}
	return result; //Give the result!
}

//Main rendering routine: renders pixels to the emulated screen.

//Information gotten from the GPU!
extern int_32 lightpen_x, lightpen_y; //Current lightpen location, if any!
extern byte lightpen_pressed; //Lightpen pressed?

//Our internal information for determining the lightpen for the currently emulated video card!
word lightpen_currentvramlocation; //Current VRAM location for light pen detection!

void EGA_checklightpen(word currentlocation, byte is_lightpenlocation, byte is_lightpenpressed) //Check the lightpen on the current location!
{
	INLINEREGISTER word lightpenlocation;
	if (getActiveVGA()->enable_SVGA==3) //EGA is emulated?
	{
		if (((getActiveVGA()->registers->EGA_lightpenstrobeswitch&3)==1) || (is_lightpenlocation && ((getActiveVGA()->registers->EGA_lightpenstrobeswitch&2)==0))) //Light pen preset and strobing? Are we the light pen location?
		{
			getActiveVGA()->registers->EGA_lightpenstrobeswitch &= ~1; //Clear the preset: we're not set anymore! Also, clear the clearing of the strobe to cause the setting to trigger this immediately without clearing.
			getActiveVGA()->registers->EGA_lightpenstrobeswitch |= 2; //The light pen register is now set!
			lightpenlocation = currentlocation; //Load the current location for converting to CGA location!
			//Now set our lightpen location!
			getActiveVGA()->registers->lightpen_high = ((lightpenlocation>>8)&0xFF); //Our high bits!
			getActiveVGA()->registers->lightpen_low = (lightpenlocation&0xFF); //Our low bits!
		}
		//Always update the CGA lightpen button(live state)!
		getActiveVGA()->registers->EGA_lightpenstrobeswitch &= ~4; //Default: clear the pressed switch indicator: we're depressed!
		getActiveVGA()->registers->EGA_lightpenstrobeswitch |= ((is_lightpenpressed&1)<<2); //Set if we're switched or not!
	}
}

OPTINLINE void drawPixel_real(uint_32 pixel, uint_32 x, uint_32 y) //Manual version for CGA conversion!
{
	INLINEREGISTER uint_32 *screenpixel = &EMU_BUFFER(x,y); //Pointer to our pixel!
	if ((screenpixel>=EMU_SCREENBUFFEREND) || (x>=EMU_MAX_X)) return; //Out of bounds?
	//Apply light pen, directly connected to us!
	if (unlikely((*screenpixel)!=pixel)) //Are we to update the changed pixel?
	{
		*screenpixel = pixel; //Update whether it's needed or not!
		GPU.emu_buffer_dirty = 1; //Update, set changed bits when changed!
	}
}

OPTINLINE void drawPixel(VGA_Type *VGA, uint_32 pixel) //Normal VGA version!
{
	drawPixel_real(pixel,VGA->CRTC.x,VGA->CRTC.y); //Draw our pixel on the display!
}

byte MDAcolors[4] = {0x00,0x81,0xC0,0xFF}; //All 4 MDA colours according to http://www.seasip.info/VintagePC/mda.html, as MDA greyscale indexes!

extern byte CGA_RGB; //Are we a RGB monitor(1) or Composite monitor(0)?

OPTINLINE void drawCGALine(VGA_Type *VGA) //Draw the current CGA line to display!
{
	INLINEREGISTER uint_32 drawx;
	INLINEREGISTER uint_32 color; //The full color to draw!
	if (CGALineSize>2048) CGALineSize = 2048; //Limit to what we have available!
	if (VGA->registers->specialMDAflags&1) //MDA rendering mode?
	{
		INLINEREGISTER byte data; //The current entry to draw!
		INLINEREGISTER byte *bufferpos, *finalpos; //The current and end position to draw!
		if (unlikely(CGALineSize==0)) return; //Abort if nothing to render!
		finalpos = &CGALineBuffer[CGALineSize]; //End of the output buffer to process!
		bufferpos = &CGALineBuffer[0]; //First pixel to render!
		drawx = 0; //Start index to draw at!
		for (;;) //Process all pixels!
		{
			data = *bufferpos; //Load the current pixel!
			data &= 3; //Only 2 bits are used for the MDA!
			data = MDAcolors[data]; //Translate the pixel to proper DAC indexes!
			color = VGA->precalcs.effectiveMDADAC[data]; //Look up the MDA DAC color to use(translate to RGB)!
			color = RGB(VGA->DACbrightness[GETR(color)], VGA->DACbrightness[GETG(color)], VGA->DACbrightness[GETB(color)]); //Make sure we're active display levels of brightness!
			drawPixel_real(color,drawx,VGA->CRTC.y); //Render the pixel as MDA colors through the B/W DAC!
			++bufferpos; //Next pixel!
			if (unlikely(bufferpos == finalpos)) break; //Stop processing when finished!
			++drawx; //Next line index!
		}
	}
	else //CGA mode?
	{
		INLINEREGISTER uint_32 *bufferpos, *finalpos;
		if (unlikely(CGALineSize==0)) return; //Abort if nothing to render!
		finalpos = &CGAOutputBuffer[CGALineSize]; //End of the output buffer to process!
		bufferpos = &CGAOutputBuffer[0]; //First pixel to render!
		RENDER_convertCGAOutput(&CGALineBuffer[0], &CGAOutputBuffer[0], CGALineSize); //Convert the CGA line to RGB output!
		drawx = 0; //Start index to draw at!
		for (;;) //Render all pixels!
		{
			color = GA_color2bw(*bufferpos, 0); //The color to render!
			color = RGB(VGA->DACbrightness[GETR(color)], VGA->DACbrightness[GETG(color)], VGA->DACbrightness[GETB(color)]); //Make sure we're active display levels of brightness!
			drawPixel_real(color,drawx,VGA->CRTC.y); //Render the converted CGA output signal!
			if (unlikely(++bufferpos==finalpos)) break; //Stop processing when finished!
			++drawx; //Next line index!
		}
	}
}

void VGA_Sequencer_updateScanlineData(VGA_Type* VGA) //Update the VGA scanline data by software!
{
	//First, all our variables!
	byte pixelshiftcount; //Effective value!
	SEQ_DATA* Sequencer;
	Sequencer = GETSEQUENCER(VGA); //Our sequencer!

	//Determine panning
	pixelshiftcount = Sequencer->frame_pixelshiftcount; //Allowable pixel shift count!

	//Determine shifts and reset the start map if needed!
	if (Sequencer->is_topwindow) //Top window reached?
	{
		//Enforce start of map to beginning in VRAM for the top window!
		if (VGA->precalcs.AttributeModeControlRegister_PixelPanningMode) //Pixel panning mode enabled?
		{
			Sequencer->pixelshiftcount_cleared = 1; //Cleared from now on!
		}
	}

	if (Sequencer->pixelshiftcount_cleared) //Cleared PEL panning from now on in the top window?
	{
		pixelshiftcount = 0; //Reset to 0 for the remainder of the display!
	}

	//Apply the byte panning and pixel shift count!
	Sequencer->pixelshiftcount = pixelshiftcount; //Effective pixel shift count!
}

void VGA_Sequencer_calcScanlineData(VGA_Type *VGA) //Recalcs all scanline data for the sequencer!
{
	//First, all our variables!
	byte bytepanning; //Effective value!
	byte pixelshiftcount; //Effective value!
	byte presetrowscan; //Effective value!
	SEQ_DATA *Sequencer;
	Sequencer = GETSEQUENCER(VGA); //Our sequencer!

	//Determine panning
	presetrowscan = Sequencer->frame_presetrowscan; //Preset row scan!
	pixelshiftcount = Sequencer->frame_pixelshiftcount; //Allowable pixel shift count!
	bytepanning = Sequencer->frame_bytepanning; //Byte panning to apply!

	//Determine shifts and reset the start map if needed!
	if (Sequencer->is_topwindow) //Top window reached?
	{
		Sequencer->startmap = 0; //The current scanline address is reset!
		presetrowscan = 0; //Preset row scan is presumed to be 0!
		//Enforce start of map to beginning in VRAM for the top window!
		if (VGA->precalcs.AttributeModeControlRegister_PixelPanningMode) //Pixel panning mode enabled?
		{
			Sequencer->pixelshiftcount_cleared = 1; //Cleared from now on!
			bytepanning = 0; //Reset to 0 for the remainder of the display!
		}
	}

	if (Sequencer->pixelshiftcount_cleared) //Cleared PEL panning from now on in the top window?
	{
		pixelshiftcount = 0; //Reset to 0 for the remainder of the display!
	}

	if (presetrowscan > Sequencer->frame_characterheight) //More than the character height?
	{
		presetrowscan = 0; //No row scan preset to apply past the character height!
	}

	//Apply the byte panning and pixel shift count!
	Sequencer->scanline_bytepanning = bytepanning; //Effective byte panning!
	Sequencer->presetrowscan = presetrowscan; //Effective preset row scan!
	Sequencer->pixelshiftcount = pixelshiftcount; //Effective pixel shift count!
}
	

LOADEDPLANESCONTAINER loadedplanes; //All four loaded planes!
LOADEDPLANESCONTAINER loadedplaneshigh; //All four loaded planes!

typedef void (*VGA_Sequencer_planedecoder)(VGA_Type *VGA, word loadedlocation);

OPTINLINE uint_32 patch_map1314(VGA_Type *VGA, uint_32 addresscounter) //Patch full VRAM address!
{ //Check this!
	INLINEREGISTER uint_32 bit; //Load row scan counter!
	if (GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.CRTCMODECONTROLREGISTER,0,1)==0) //a13=Bit 0 of the row scan counter!
	{
		//Row scan counter bit 1 is placed on the memory bus bit 14 during active display time.
		//Bit 1, placed on memory address bit 14 has the effect of quartering the memory.
		bit = ((SEQ_DATA *)VGA->Sequencer)->rowscancounter; //Current row scan counter!
		bit &= 1; //Bit0 only!
		bit <<= 13; //Shift to our position (bit 13)!
		addresscounter &= ~0x2000; //Clear bit13!
		addresscounter |= bit; //Set bit13 if needed!
	}

	if (GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.CRTCMODECONTROLREGISTER,1,1)==0) //a14<=Bit 1 of the row scan counter!
	{
		bit = ((SEQ_DATA *)VGA->Sequencer)->rowscancounter; //Current row scan counter!
		bit &= 2; //Bit1 only!
		bit <<= 13; //Shift to our position (bit 14)!
		addresscounter &= ~0x4000; //Clear bit14;
		addresscounter |= bit; //Set bit14 if needed!
	}

	return addresscounter; //Give the linear address!
}

VGA_addresswrapextensionhandler VGA_calcaddresswrapextensionhandler = NULL; //The DWord shift extension handler!

OPTINLINE uint_32 addresswrap(VGA_Type *VGA, uint_32 memoryaddress) //Wraps memory arround 64k!
{
	INLINEREGISTER uint_32 result, address2;
	if (VGA_calcaddresswrapextensionhandler) return VGA_calcaddresswrapextensionhandler(VGA,memoryaddress); //Apply extension shift method when specified!
	switch (VGA->precalcs.BWDModeShift) //What mode?
	{
		case 1: //Word mode?
			result = 0xD; //Load default location (13)
			result |= (GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.CRTCMODECONTROLREGISTER,5,1) << 1); //MA15 instead of MA13 when set!
			address2 = memoryaddress; //Load the address for calculating!
			address2 >>= result; //Apply MA15/MA13 to bit 0!
			address2 &= 1; //Only load bit 0!
			result = memoryaddress; //Default: don't change!
			result <<= 1; //Shift up to create the word mode!
			result |= address2; //Add bit MA15/MA13 at bit 0!
			return result; //Give the result!
		case 2: //DWord mode?
			//Doubleword mode executed normally according to documentation!
			if (getActiveVGA()->enable_SVGA == 0) //VGA?
			{
				return (memoryaddress<<2)|((memoryaddress>>14)&3); //VGA-compatible DWORD addressing!
			}
			break;
		default:
		case 0: //Byte mode?
			//Don't do anything?
			break; //Unchanged!

	}
	return memoryaddress; //Original address in byte mode!
}

VGA_AttributeInfo currentattributeinfo; //Our current collected attribute info!
VGA_AttributeInfo overrideattributeinfo; //Our current collected attribute info!
uint_32 currentvramlocation;

OPTINLINE void VGA_loadcharacterplanes(VGA_Type *VGA, SEQ_DATA *Sequencer) //Load the planes!
{
	INLINEREGISTER uint_32 vramlocation; //The location we load at!
	uint_32 vramlocationbackup; //Backup for 16-bit modes!
	//Horizontal logic
	VGA_Sequencer_planedecoder planesdecoder[2] = { VGA_TextDecoder, VGA_GraphicsDecoder }; //Use the correct decoder!

	//Column logic
	vramlocation = Sequencer->memoryaddress; //Load the address to be loaded!
	lightpen_currentvramlocation = vramlocation; //Save the new current location for light pen detection!

	if (VGA->precalcs.charactercode_16bit && VGA->precalcs.textmode) //16-bit character code?
	{
		vramlocationbackup = vramlocation; //Backup for the first byte to properly fetch!
		++vramlocation; //Fetch the high byte first!
		//Column/Row logic
		currentvramlocation = vramlocation = patch_map1314(VGA, addresswrap(VGA, vramlocation)); //Apply address wrap and MAP13/14?

		//Now calculate and give the planes to be used!
		loadedplaneshigh.loadedplanes = VGA_VRAMDIRECTPLANAR(VGA, vramlocation, 0); //Load the 4 planes from VRAM, as an entire DWORD!
		vramlocation = vramlocationbackup; //Now fetching the low byte!
		//Column/Row logic
		currentvramlocation = vramlocation = patch_map1314(VGA, addresswrap(VGA, vramlocation)); //Apply address wrap and MAP13/14?

		//Now calculate and give the planes to be used!
		loadedplanes.loadedplanes = VGA_VRAMDIRECTPLANAR(VGA, vramlocation, 0); //Load the 4 planes from VRAM, as an entire DWORD!
	}
	else //VGA-compatible latching?
	{
		//Column/Row logic
		currentvramlocation = vramlocation = patch_map1314(VGA, addresswrap(VGA, vramlocation)); //Apply address wrap and MAP13/14?

		//Now calculate and give the planes to be used!
		loadedplanes.loadedplanes = VGA_VRAMDIRECTPLANAR(VGA, vramlocation, 0); //Load the 4 planes from VRAM, as an entire DWORD!
		loadedplaneshigh.loadedplanes = 0; //No high planes!
	}

	//Now the buffer is ready to be processed into pixels!
	planesdecoder[VGA->precalcs.graphicsmode](VGA,vramlocation); //Use the decoder to get the pixels or characters!

	INLINEREGISTER byte lookupprecalcs;
	lookupprecalcs = ((SEQ_DATA *)Sequencer)->charinner_y;
	lookupprecalcs <<= 1; //Make room!
	lookupprecalcs |= CURRENTBLINK(VGA); //Blink!
	lookupprecalcs <<= 1; //Make room for the pixelon!
	currentattributeinfo.lookupprecalcs = lookupprecalcs; //Save the looked up precalcs, this never changes during a processed block of pixels (both text and graphics modes)!
}

OPTINLINE byte VGA_ActiveDisplay_timing(SEQ_DATA *Sequencer, VGA_Type *VGA)
{
	word extrastatus = *Sequencer->extrastatus; //Next status!
	if (unlikely((GETBITS(getActiveVGA()->registers->SequencerRegisters.REGISTERS.RESETREGISTER,1,1) && GETBITS(getActiveVGA()->registers->SequencerRegisters.REGISTERS.RESETREGISTER,0,1))==0)) //Reset sequencer?
	{
		return 0; //Abort: we're disabled!
	}

	if (extrastatus & 2) //Half character clock is to be executed?
	{
		if ((++Sequencer->linearcounterdivider&VGA->precalcs.characterclockshift) == 0) //Increase memory address counter?
		{
			Sequencer->linearcounterdivider = 0; //Reset!
			if (likely(!(VGA->precalcs.charactercode_16bit && VGA->precalcs.textmode))) //8-bit memory location?
			{
				++Sequencer->memoryaddress; //Increase the memory address counter!
			}
			else //16-bit memory location?
			{
				Sequencer->memoryaddress += 2; //Increase the memory address counter!
			}
		}

		if (unlikely((++Sequencer->memoryaddressclock&VGA->precalcs.VideoLoadRateMask) == 0)) //Reload data this clock?
		{
			Sequencer->memoryaddressclock = 0; //Reset!
			VGA_loadcharacterplanes(VGA, Sequencer); //Load data from the graphics planes!
		}
	}

	Sequencer->extrastatus += ((extrastatus>>2)&1); //Increase the extra status, when allowed!

	return extrastatus & 1; //Read next pixel?
}

//0=VGA-compatible, 1=8-bit, 2=reserved, 3=16-bit
static VGA_AttributeController_Mode attributecontroller_modes[4] = { VGA_AttributeController_4bit, VGA_AttributeController_8bit, VGA_AttributeController_4bit, VGA_AttributeController_16bit }; //Both modes we use!
//0=4-bit, 1=8-bit
static VGA_AttributeController_Mode attributecontroller_VGAmodes[2] = { VGA_AttributeController_4bit, VGA_AttributeController_8bit }; //Both modes we use!

void updateSequencerPixelDivider(VGA_Type* VGA, SEQ_DATA* Sequencer)
{
	byte val;
	val = 1; //Default: don't divide!
	if ((VGA->precalcs.effectiveDACmode&0x18)==0) //Adjusted?
	{
	}
	else //24BPP/32BPP mode?
	{
		val = 3+(((VGA->precalcs.effectiveDACmode & 0x10)>>4)); //3 clocks per pixel!
	}
	Sequencer->pixelclockdivider = val; //Latch this many clocks before processing it!
}

VGA_AttributeController_Mode attrmode = VGA_AttributeController_4bit; //Default mode!

void updateVGAAttributeController_Mode(VGA_Type *VGA)
{
	if (VGA->precalcs.AttributeController_16bitDAC) //16-bit DAC override active?
	{
		attrmode = attributecontroller_modes[VGA->precalcs.AttributeController_16bitDAC]; //Apply the current mode!
		VGA->precalcs.planerenderer_16bitDAC = VGA->precalcs.AttributeController_16bitDAC; //Use directly!
	}
	else //VGA compatibility mode?
	{
		VGA->precalcs.planerenderer_16bitDAC = 0; //Don't apply the special plane rendering!
		attrmode = attributecontroller_VGAmodes[VGA->precalcs.AttributeModeControlRegister_ColorEnable8Bit&1]; //Apply the current mode according to VGA registers!
	}
	VGA_AttributeController_calcAttributes(VGA); //Recalculate the attributes!
	updateSequencerPixelDivider(VGA, (SEQ_DATA*)(VGA->Sequencer)); //Update the pixel divider!
}

OPTINLINE byte VGA_AttributeController(VGA_AttributeInfo *Sequencer_attributeinfo, VGA_Type *VGA) //Process attribute to DAC index!
{
	//Originally: VGA_Type *VGA, word Scanline, word x, VGA_AttributeInfo *info

	//Our changing variables that are required!
	return attrmode(Sequencer_attributeinfo, VGA); //Passthrough!
}

OPTINLINE void VGA_Sequencer_updateRow(VGA_Type *VGA, SEQ_DATA *Sequencer, byte isinit)
{
	word shiftcount;
	byte x; //For horizontal shifting/temp storage!
	INLINEREGISTER word row;
	INLINEREGISTER uint_32 charystart;
	row = Sequencer->Scanline; //Default: our normal scanline!
	if (row>=Sequencer->frame_topwindowstart) //Splitscreen operations?
	{
		row -= Sequencer->frame_topwindowstart; //This starts after the row specified, at row #0!
		Sequencer->is_topwindow = 1; //We're starting the top window rendering!
		isinit |= (row == 0); //Init when the first row of the top window!
	}
	else
	{
		Sequencer->is_topwindow = 0; //We're not the top window!
	}

	VGA_Sequencer_calcScanlineData(VGA); //Apply live data for the current scanline, as needed!

	if (isinit) //Are we the initialization for the top/bottom window?
	{
		Sequencer->activepresetrowscan = Sequencer->presetrowscan; //Activate!
		Sequencer->chary = 0; //Init!
		Sequencer->rowscancounter = Sequencer->charinner_y = Sequencer->activepresetrowscan; //Init scanline within the character!
		Sequencer->baselineaddr = Sequencer->startmap; //Calculate the start of the map while we're at it: it's faster this way! Load the first base line address!
		Sequencer->scandoublingcounter = 0; //First of the scan doubling counter! Don't tick on the next one when double scanning!
	}
	else
	{
		//row is the vertical timing counter
		//Apply scan doubling to the row scan counter(inner character row and thus, by extension, the row itself)!
		Sequencer->scandoublingcounter ^= 1; //Check for scan doubling!
		if ((Sequencer->scandoublingcounter ^ 1) | (VGA->precalcs.scandoubling ^ 1)) //Scan doubling overflow when set or no scan doubling? We're ticking!
		{
			//We're ticking an undoubled or doubling scanline that needs reloading!
			if (VGA->precalcs.scandoubling == 0) //Not scan doubling?
			{
				Sequencer->scandoublingcounter = 1; //First the counter on the next when not double scanning to start any pending double scanning operation!
			}

			//Now that the scanline doubling is applied, apply the remaining counters!
			//Apply scanline division to the current row timing?
			//Increase the rowscancounter, overflowing the character height causes adding to the 
			++Sequencer->rowscancounter; //Increase the row scan counter!
			if (Sequencer->rowscancounter >= VGA->precalcs.characterheight) //Height reached?
			{
				Sequencer->rowscancounter = 0; //Reset the row scan counter!
				++Sequencer->chary; //Next row is used to render!
				Sequencer->baselineaddr += VGA->precalcs.rowsize; //The offset calculation is added to the memory base address, as is documented!
			}
		}
	}

	Sequencer->charinner_y = Sequencer->rowscancounter; //Inner y is the row scan counter!

	charystart = Sequencer->baselineaddr; //What row to start with our pixels! Apply the line and start map to retrieve(start at the new start of the scanline to draw)!
	Sequencer->memoryaddress = Sequencer->charystart = charystart; //Apply scanline starting memory address!

	//Some attribute controller special 8-bit mode support!
	Sequencer->extrastatus = &VGA->CRTC.extrahorizontalstatus[0]; //Start our extra status at the beginning of the row!

	Sequencer->memoryaddressclock = Sequencer->linearcounterdivider = 0; //Address counters are reset!
	currentattributeinfo.latchstatus = 0; //Reset the latches used for rendering!
	VGA_loadcharacterplanes(VGA, Sequencer); //Load data from the first planes!

	shiftcount = Sequencer->pixelshiftcount; //How much pixels to shift!
	shiftcount += (Sequencer->scanline_bytepanning<<3); //How much to shift in 8-pixel modes!
	if (VGA->precalcs.characterwidth==9) //9-pixel width instead of 8-pixel width?
	{
		shiftcount += Sequencer->scanline_bytepanning; //How much to shift in 9-pixel modes!
	}

	//Process any horizontal pixel shift count!
	if (VGA->precalcs.textmode) //Text mode?
	{
		for (x = 0;x < shiftcount;++x) //Process pixel shift count!
		{
			if (VGA_ActiveDisplay_timing(Sequencer, VGA)) //Render the next pixel?
			{
				VGA_Sequencer_TextMode(VGA, Sequencer, &currentattributeinfo); //Get the color to render!
			}
		}
	}
	else //Graphics mode?
	{
		for (x = 0;x < shiftcount;++x) //Process pixel shift count!
		{
			if (VGA_ActiveDisplay_timing(Sequencer, VGA)) //Render the next pixel?
			{
				VGA_Sequencer_GraphicsMode(VGA, Sequencer, &currentattributeinfo); //Get the color to render!
			}
		}
	}
}

byte Sequencer_run; //Sequencer breaked (loop exit)?

//Special states!
byte blanking = 0; //Are we blanking!
byte retracing = 0; //Allow rendering by retrace!
byte totalling = 0; //Allow rendering by total!

byte hblank = 0, hretrace = 0; //Horizontal blanking/retrace?
byte vblank = 0, vretrace = 0; //Vertical blanking/retrace?

byte VGA_LOGPRECALCS = 0; //Log precalcs?

//displayrenderhandler[total_retrace][signal]
DisplayRenderHandler displayrenderhandler[4][VGA_DISPLAYRENDERSIZE]; //Our handlers for all pixels!

void VGA_handleSpriteCRTCwindowNonActiveDisplay(VGA_Type* VGA, SEQ_DATA* Sequencer, VGA_AttributeInfo* attributeinfo, VGA_AttributeInfo* overrideattributeinfo); //Prototype!

void VGA_NOP(SEQ_DATA *Sequencer, VGA_Type *VGA) //NOP for pixels!
{
	VGA_handleSpriteCRTCwindowNonActiveDisplay(VGA, Sequencer, &currentattributeinfo, &overrideattributeinfo); //Handle without display!
	VGA->CRTC.CRTCBwindowmaxstatus = MAX(VGA->CRTC.CRTCBwindowmaxstatus, VGA->CRTC.CRTCBwindowEnabled); //Maximum status detected!
}

OPTINLINE void VGA_RenderOutput(SEQ_DATA *Sequencer, VGA_Type *VGA) //Render the current rendered frame to the display!
{
	//First, render ourselves to the screen!
	GPU.xres = Sequencer->xres; //Apply x resolution!
	GPU.yres = Sequencer->yres; //Apply y resolution!
	//Just render the bottom and top windows normally!
	VGA_VBlankHandler(VGA); //Handle all VBlank stuff!
	Sequencer->yres = 0; //Reset Y resolution next frame if not specified (like a real screen)!
	Sequencer->xres = 0; //Reset X resolution next frame if not specified (like a real screen)!
}

//Total handlers!
void VGA_VTotal(SEQ_DATA *Sequencer, VGA_Type *VGA)
{
	VGA->CRTC.CRTCBwindowmaxstatus = MAX(VGA->CRTC.CRTCBwindowmaxstatus, VGA->CRTC.CRTCBwindowEnabled); //Maximum status detected!
	Sequencer->Scanline = 0; //Reset for the next frame!
	//VGA_RenderOutput(Sequencer,VGA); //Render the output to the screen!
	if (VGA->enable_SVGA == 4) //CGA/MDA?
	{
		//The end of vertical total has been reached, reload start address!
		Sequencer->frame_startmap = VGA->precalcs.startaddress; //What start address to use for the next frame?
	}
	Sequencer->frame_presetrowscan = VGA->precalcs.presetrowscan; //Preset row scan!
	Sequencer->frame_characterheight = VGA->precalcs.characterheight; //The character height to compare to when checking for validity of the preset row scan!
	Sequencer->startmap = Sequencer->frame_startmap; //Restart the frame with the selected start map!
	VGA_Sequencer_updateRow(VGA, Sequencer,1); //Scanline has been changed!

	//Clear horizontal Sprite/CRTC status!
	Sequencer->SpriteCRTCylatched = Sequencer->SpriteCRTCxlatched = 1; //Waiting for X&Y to latch!
}

void VGA_HTotal(SEQ_DATA *Sequencer, VGA_Type *VGA)
{
	VGA->CRTC.CRTCBwindowmaxstatus = MAX(VGA->CRTC.CRTCBwindowmaxstatus, VGA->CRTC.CRTCBwindowEnabled); //Maximum status detected!
	//Process HBlank: reload display data for the next scanline!
	//Sequencer itself
	Sequencer->x = 0; //Reset for the next scanline!
	Sequencer->activedisplayx = 0; //Reset CRTCB/Sprite horizontal counter!
	
	//Sequencer rendering data
	Sequencer->DACcounter = 0; //Reset the DAC counter!
	Sequencer->lastDACcolor = 0; //Reset the last DAC color!
	Sequencer->currentpixelclock = 0; //Reset the pixel clock we're dividing!
	++Sequencer->Scanline; //Next scanline to process!
	VGA_Sequencer_updateRow(VGA, Sequencer,0); //Scanline has been changed!

	//Clear horizontal Sprite/CRTC status!
	Sequencer->SpriteCRTCxlatched = 1; //Waiting for X to latch!
}

//Retrace handlers!
void VGA_VRetrace(SEQ_DATA *Sequencer, VGA_Type *VGA)
{
	VGA->CRTC.DACOutput = 0x00; //The output is cleared during retracing!
	VGA->CRTC.CRTCBwindowmaxstatus = MAX(VGA->CRTC.CRTCBwindowmaxstatus, VGA->CRTC.CRTCBwindowEnabled); //Maximum status detected!
	if (VGA->CRTC.y>Sequencer->yres)
	{
		Sequencer->yres = VGA->CRTC.y; //Current y resolution!
	}
	VGA->CRTC.y = 0; //Reset destination row!
	VGA_RenderOutput(Sequencer,VGA); //Render the output to the screen!
}

void VGA_VRetracePending(SEQ_DATA *Sequencer, VGA_Type *VGA)
{
	VGA->CRTC.CRTCBwindowmaxstatus = MAX(VGA->CRTC.CRTCBwindowmaxstatus, VGA->CRTC.CRTCBwindowEnabled); //Maximum status detected!
	VGA->CRTC.y = 0; //Reset destination row!
}

byte CGAMDARenderer = 0; //Render CGA style?

void VGA_HRetrace(SEQ_DATA *Sequencer, VGA_Type *VGA)
{
	VGA->CRTC.DACOutput = 0x00; //The output is cleared during retracing!
	VGA->CRTC.CRTCBwindowmaxstatus = MAX(VGA->CRTC.CRTCBwindowmaxstatus, VGA->CRTC.CRTCBwindowEnabled); //Maximum status detected!
	CGALineSize = VGA->CRTC.x; //Update X resolution!
	if (VGA->CRTC.x>Sequencer->xres) Sequencer->xres = VGA->CRTC.x; //Current x resolution!
	VGA->CRTC.x = 0; //Reset destination column!
	if (likely(vretrace==0)) //Not retracing vertically?
	{
		if (CGAMDARenderer) //CGA/MDA rendering mode?
		{
			drawCGALine(VGA); //Draw the current CGA line using NTSC colours!	
		}
		++VGA->CRTC.y; //Not retracing vertically? Next row on-screen!
		if (likely(Sequencer->is_topwindow == 0)) //Not top window?
		{
			Sequencer->topwindowCRTbase = VGA->CRTC.y; //Save bottom resolution!
		}
	}
}

void VGA_HRetracePending(SEQ_DATA *Sequencer, VGA_Type *VGA)
{
	VGA->CRTC.CRTCBwindowmaxstatus = MAX(VGA->CRTC.CRTCBwindowmaxstatus, VGA->CRTC.CRTCBwindowEnabled); //Maximum status detected!
	VGA->CRTC.x = 0; //Reset destination column!
}

//All renderers for active display parts:

typedef void (*VGA_Sequencer_Mode)(VGA_Type *VGA, SEQ_DATA *Sequencer, VGA_AttributeInfo *attributeinfo); //Render an active display pixel!
typedef void (*VGA_LightPen_Mode)(word currentlocation, byte is_lightpenlocation, byte is_lightpenpressed); //Light pen handler for the specified hardware!

uint_32 CLUT16bit[0x10000]; //16-bit color lookup table!
uint_32 CLUT15bit[0x10000]; //15-bit color lookup table!

void VGA_LightPenHandler(word currentlocation, byte is_lightpenlocation, byte is_lightpenpressed)
{
	//Do nothing: VGA has no light pen support!
}

VGA_LightPen_Mode lightpenhandler = &VGA_LightPenHandler; //Light Pen Handler!

void updateLightPenMode(VGA_Type *VGA)
{
	if (unlikely(VGA->enable_SVGA==3)) //EGA?
	{
		lightpenhandler = &EGA_checklightpen; //Check for anything requiring the lightpen on the EGA!
	}
	else if (unlikely(VGA->enable_SVGA==4)) //CGA/MDA?
	{
		lightpenhandler = &CGA_checklightpen; //Check for anything requiring the lightpen on the CGA!
	}
	else //VGA?
	{
		lightpenhandler = &VGA_LightPenHandler; //Use VGA light pen handler!
	}
}

//drawnto: 0=GPU, 1=CGALineBuffer
OPTINLINE void video_updateLightPen(VGA_Type *VGA, byte drawnto)
{
	byte lightpen_triggered;
	lightpen_triggered = (((lightpen_x==VGA->CRTC.x) && (lightpen_y==VGA->CRTC.y)) && drawnto); //Are we at the location specified by the lightpen on the CRT?
	lightpenhandler(lightpen_currentvramlocation,lightpen_triggered,lightpen_pressed); //Check for anything requiring the lightpen on the device!
}

//Used for Sprite/CRTC fetching from VRAM!
byte VGA_renderer_readlinearVRAM(uint_32 addr)
{
	return readVRAMplane(getActiveVGA(), (addr & 3), (addr >> 2), 0, 0); //Read VRAM!
}

byte VGA_SpriteCRTCGetPixel(VGA_Type* VGA, SEQ_DATA* Sequencer, VGA_AttributeInfo* attributeinfo, VGA_AttributeInfo *overrideattributeinfo)
{
	byte tickingpixel; //Ticking a pixel to provide pixel duplication?
	word pixel; //The pixel that's retrieved!
	if (!Sequencer->SpriteCRTChorizontalpixelsleft) //No pixels left to render horizontally?
	{
		return 0; //Nothing to render left on this scanline!
	}
	++Sequencer->SpriteCRTCwidthstep; //Tick one pixel in the width!
	if (Sequencer->SpriteCRTCwidthstep >= VGA->precalcs.SpriteCRTCpixelwidth) //Pixel width reached?
	{
		Sequencer->SpriteCRTCwidthstep = 0; //Reset step to count again!
		--Sequencer->SpriteCRTChorizontalpixelsleft; //One pixel is rendering now!
		tickingpixel = 1; //We're ticking the pixel!
	}
	else
	{
		tickingpixel = 0; //Not ticking the pixel!
	}
	memcpy(overrideattributeinfo, attributeinfo, sizeof(*attributeinfo)); //Make the output setting the same as the original input settings!
	if ((VGA->precalcs.SpriteCRTCEnabled & 3) == 1) //Sprite mode?
	{
		//Retrieve the pixel from VRAM!
		pixel = VGA_renderer_readlinearVRAM(Sequencer->SpriteCRTC_pixel_address); //The pixels in the map!
		pixel >>= (Sequencer->SpriteCRTCstep << 1); //2 bits for each pixel!
		pixel &= 3; //Only bits used!

		//Prepare to handle the next pixel!
		if (tickingpixel) //Ticking the pixel?
		{
			++Sequencer->SpriteCRTCstep; //Next action?
			if (Sequencer->SpriteCRTCstep == 4) //1 byte processed?
			{
				++Sequencer->SpriteCRTC_pixel_address; //Next pixel address!
				if ((VGA->precalcs.SpriteCRTCrowoffset<<1)==VGA->precalcs.SpriteCRTChorizontalwindowwidth) //Double the offset of what's documented that should be programmed?
				{
					//Weird behaviour on some OSes against documentation (Windows 9x): bytes are located at every even address only?
					++Sequencer->SpriteCRTC_pixel_address; //Next pixel address!
				}
				Sequencer->SpriteCRTCstep = 0; //Reset for the next pixels to be retrieved!
			}
		}

		if (pixel & 2) //Inverted or transparent?
		{
			if (pixel & 1) //Inverted?
			{
				if (VGA->precalcs.SpriteCRTCEnabled & 4) //Output to SP 0:1?
				{
					return 0; //Unsupported!
				}
				return 2; //Inverted specially!
			}
			else //Transparent?
			{
				return 0; //Transparent!
			}
		}
		else if (pixel & 1) //Sprite color FFh?
		{
			if (VGA->precalcs.SpriteCRTCEnabled & 4) //Output to SP 0:1?
			{
				return 0; //Unsupported!
			}
			return 3; //Overridden with a font pixel, in the specified size!
		}
		else //Sprite color 00h?
		{
			if (VGA->precalcs.SpriteCRTCEnabled & 4) //Output to SP 0:1?
			{
				return 0; //Unsupported!
			}
			return 1; //Overridden with a background pixel, in the specified size!
		}
	}
	else //CRTC mode?
	{
		//Retrieve the pixel from VRAM!
		pixel = VGA_renderer_readlinearVRAM(Sequencer->SpriteCRTC_pixel_address); //The pixels in the map!

		switch (VGA->precalcs.SpriteCRTCpixeldepth) //What pixel depth?
		{
		default:
		case 0: //1BPP?
			pixel >>= 7-Sequencer->SpriteCRTCstep; //1 bit for each pixel!
			pixel &= 1; //Only bits used!

			overrideattributeinfo->attributesize = MIN(attributeinfo->attributesize, 1); //256 colors!
			overrideattributeinfo->attribute = pixel; //pixel attribute!

			//Prepare to handle the next pixel!
			if (tickingpixel) //Ticking the pixel?
			{
				++Sequencer->SpriteCRTCstep; //Next action?
				if (Sequencer->SpriteCRTCstep == 8) //1 byte processed?
				{
					++Sequencer->SpriteCRTC_pixel_address; //Next pixel address!
					Sequencer->SpriteCRTCstep = 0; //Reset for the next pixels to be retrieved!
				}
			}
			break;
		case 1: //2BPP?
			pixel >>= 6-(Sequencer->SpriteCRTCstep << 1); //2 bits for each pixel!
			pixel &= 3; //Only bits used!

			overrideattributeinfo->attributesize = MIN(attributeinfo->attributesize, 1); //256 colors!
			overrideattributeinfo->attribute = pixel; //pixel attribute!

			//Prepare to handle the next pixel!
			if (tickingpixel) //Ticking the pixel?
			{
				++Sequencer->SpriteCRTCstep; //Next action?
				if (Sequencer->SpriteCRTCstep == 4) //1 byte processed?
				{
					++Sequencer->SpriteCRTC_pixel_address; //Next pixel address!
					Sequencer->SpriteCRTCstep = 0; //Reset for the next pixels to be retrieved!
				}
			}
			break;
		case 2: //4BPP?
			pixel >>= 4-(Sequencer->SpriteCRTCstep << 2); //4 bits for each pixel!
			pixel &= 0xF; //Only bits used!

			overrideattributeinfo->attributesize = MIN(attributeinfo->attributesize, 1); //256 colors!
			overrideattributeinfo->attribute = pixel; //pixel attribute!

			//Prepare to handle the next pixel!
			if (tickingpixel) //Ticking the pixel?
			{
				++Sequencer->SpriteCRTCstep; //Next action?
				if (Sequencer->SpriteCRTCstep == 2) //1 byte processed?
				{
					++Sequencer->SpriteCRTC_pixel_address; //Next pixel address!
					Sequencer->SpriteCRTCstep = 0; //Reset for the next pixels to be retrieved!
				}
			}
			break;
		case 3: //8BPP?
			//8 bits for each pixel!
			//Prepare to handle the next pixel!
			overrideattributeinfo->attributesize = MIN(attributeinfo->attributesize, 1); //256 colors!
			overrideattributeinfo->attribute = pixel; //pixel attribute!

			if (tickingpixel) //Ticking the pixel?
			{
				++Sequencer->SpriteCRTC_pixel_address; //Next pixel address!
				Sequencer->SpriteCRTCstep = 0; //Reset for the next pixels to be retrieved!
			}
			break;
		case 4: //16BPP?
			pixel |= (VGA_renderer_readlinearVRAM(Sequencer->SpriteCRTC_pixel_address+1) << 8); //The high pixels in the map!
			overrideattributeinfo->attribute = pixel; //pixel attribute!
			attributeinfo->attributesize = 2; //Force 64K colors!
			if (tickingpixel) //Ticking the pixel?
			{
				++Sequencer->SpriteCRTC_pixel_address; //Next pixel address!
				++Sequencer->SpriteCRTC_pixel_address; //Next pixel address!
				Sequencer->SpriteCRTCstep = 0; //Reset for the next pixels to be retrieved!
			}
			break;
		}
		if (VGA->precalcs.SpriteCRTCEnabled & 4) //Output to SP 0:1?
		{
			return 0; //Unsupported!
		}
		return 4; //CRTC fully rendered!
	}
	return 0; //Don't render any pixel yet!
}

//Handle a new scanline for the Sprite/CRTC unit!
byte resultgottendummy;
void VGA_handleSpriteCRTCnewScanline(VGA_Type* VGA, SEQ_DATA* Sequencer, VGA_AttributeInfo* attributeinfo)
{
	VGA_AttributeInfo dummyattribute; //Dummy attribute!
	word n;
	uint_32 pixelstoskip; //How many pixels to skip?
	Sequencer->SpriteCRTC_pixel_address = Sequencer->SpriteCRTC_row_address; //Pixel address starts at the row address!
	Sequencer->SpriteCRTChorizontalpixelsleft = VGA->precalcs.SpriteCRTChorizontalwindowwidth; //How many pixels are left to render?
	Sequencer->SpriteCRTCwidthstep = 0; //Reset the width step to use!
	//Handle the horizontal preset now!
	if (VGA->precalcs.SpriteCRTChorizontaldisplaypreset) //Horizontal preset?
	{
		pixelstoskip = (VGA->precalcs.SpriteCRTChorizontaldisplaypreset * VGA->precalcs.SpriteCRTCpixelwidth); //How many pixels to skip?
		for (n = 0; n < pixelstoskip; ++n) //Handle horizontal preset!
		{
			resultgottendummy = VGA_SpriteCRTCGetPixel(VGA, Sequencer, &dummyattribute, &dummyattribute); //Dummy renderings!
		}
	}
}

void VGA_handleSpriteCRTCwindowNonActiveDisplay(VGA_Type* VGA, SEQ_DATA* Sequencer, VGA_AttributeInfo* attributeinfo, VGA_AttributeInfo* overrideattributeinfo)
{
	VGA->CRTC.CRTCBwindowEnabled &= ~1; //The window is not yet active this scanline!
	if (VGA->precalcs.SpriteCRTCEnabled) //Sprite/CRTC window enabled?
	{
		if (Sequencer->currentScanline >= VGA->precalcs.SpriteCRTCverticaldisplaydelay) //Vertically within range?
		{
			if (Sequencer->activedisplayx >= VGA->precalcs.SpriteCRTChorizontaldisplaydelay) //Horizontally within range?
			{
				//We're perhaps a part of the sprite or CRTC display.
				VGA->CRTC.CRTCBwindowEnabled |= 2; //The window is active this scanline!
				if (Sequencer->SpriteCRTCxlatched) //Starting horizontal display?
				{
					if (Sequencer->currentScanline == VGA->precalcs.SpriteCRTCverticaldisplaydelay) //Starting vertical display?
					{
						//Latching the first scanline, so within range!
					}
					else //New scanline or double scanning?
					{
						if (Sequencer->SpriteCRTCylatched) //Y not latched yet?
						{
							return; //Not ready to render yet!
						}
						if (Sequencer->SpriteCRTC_virtualscanline >= VGA->precalcs.SpriteCRTCverticalwindowheight) //Already finished?
						{
							return; //Don't handle any new scanlines anymore: we're finished!
						}
					}
				}
				if ((Sequencer->SpriteCRTCylatched | Sequencer->SpriteCRTCxlatched) != 0) //Not latched the start yet?
				{
					return; //Not ready to handle yet this frame!
				}
				if (Sequencer->SpriteCRTC_virtualscanline >= VGA->precalcs.SpriteCRTCverticalwindowheight) //Already finished?
				{
					return; //Don't handle any new scanlines anymore: we're finished!
				}
				VGA->CRTC.CRTCBwindowEnabled |= 2; //The window is now active this scanline!
			}
			else //Might be on the current scanline needing handling?
			{
				if (Sequencer->SpriteCRTCxlatched) //Starting horizontal display this scanline perhaps?
				{
					if (Sequencer->currentScanline == VGA->precalcs.SpriteCRTCverticaldisplaydelay) //Starting vertical display?
					{
						VGA->CRTC.CRTCBwindowEnabled |= 2; //The window is active this scanline!
					}
					else //New scanline or double scanning?
					{
						VGA->CRTC.CRTCBwindowEnabled |= 2; //The window is active this scanline!
						if (Sequencer->SpriteCRTCylatched) //Y not latched yet?
						{
							VGA->CRTC.CRTCBwindowEnabled &= ~2; //The window is inactive this scanline!
							return; //Not ready to render yet!
						}
						if (Sequencer->SpriteCRTC_virtualscanline >= VGA->precalcs.SpriteCRTCverticalwindowheight) //Already finished?
						{
							VGA->CRTC.CRTCBwindowEnabled &= ~2; //The window is inactive this scanline!
							return; //Don't handle any new scanlines anymore: we're finished!
						}
					}
				}
				if ((Sequencer->SpriteCRTCylatched) != 0) //Not latched the start yet?
				{
					VGA->CRTC.CRTCBwindowEnabled &= ~2; //The window is inactive this scanline!
					return; //Not ready to handle yet this frame!
				}
				if (Sequencer->SpriteCRTC_virtualscanline >= VGA->precalcs.SpriteCRTCverticalwindowheight) //Already finished?
				{
					VGA->CRTC.CRTCBwindowEnabled &= ~2; //The window is inactive this scanline!
					return; //Don't handle any new scanlines anymore: we're finished!
				}
				VGA->CRTC.CRTCBwindowEnabled |= 2; //The window is now active this scanline!
			}
		}
		else //Vertically out of range?
		{
			VGA->CRTC.CRTCBwindowEnabled &= ~3; //The window is now inactive and not on the current scanline!
		}
	}
	else //Fully disabled?
	{
		VGA->CRTC.CRTCBwindowEnabled &= ~3; //The window is now inactive and not on the current scanline!
	}
}

byte VGA_handleSpriteCRTCwindow(VGA_Type* VGA, SEQ_DATA* Sequencer, VGA_AttributeInfo* attributeinfo, VGA_AttributeInfo *overrideattributeinfo)
{
	if (VGA->precalcs.SpriteCRTCEnabled) //Sprite/CRTC window enabled?
	{
		if (Sequencer->currentScanline >= VGA->precalcs.SpriteCRTCverticaldisplaydelay) //Vertically within range?
		{
			if (Sequencer->activedisplayx >= VGA->precalcs.SpriteCRTChorizontaldisplaydelay) //Horizontally within range?
			{
				//We're perhaps a part of the sprite or CRTC display.
				VGA->CRTC.CRTCBwindowEnabled |= 2; //The window is active this scanline!
				if (Sequencer->SpriteCRTCxlatched) //Starting horizontal display?
				{
					if (Sequencer->currentScanline == VGA->precalcs.SpriteCRTCverticaldisplaydelay) //Starting vertical display?
					{
						if (Sequencer->SpriteCRTCylatched) //To latch first row?
						{
							Sequencer->SpriteCRTC_row_address = VGA->precalcs.SpriteCRTCstartaddress; //Load the start address!
							Sequencer->SpriteCRTC_row_address += VGA->precalcs.SpriteCRTCrowoffset * VGA->precalcs.SpriteCRTCverticaldisplaypreset; //Vertical preset to apply!
							Sequencer->SpriteCRTC_virtualscanline = VGA->precalcs.SpriteCRTCverticaldisplaypreset; //What is our idea of the current scanline?
							Sequencer->SpriteCRTCrowstep = 0; //Initialize the row step!
							Sequencer->SpriteCRTCstep = 0; //Initialize the horizontal step!
							VGA_handleSpriteCRTCnewScanline(VGA, Sequencer, attributeinfo); //New scanline handling!
							Sequencer->SpriteCRTCylatched = 0; //We're latched now!
							Sequencer->SpriteCRTCxlatched = 0; //We're latched now!
						}
					}
					else //New scanline or double scanning?
					{
						if (Sequencer->SpriteCRTCylatched) //Y not latched yet?
						{
							VGA->CRTC.CRTCBwindowEnabled &= ~1; //The window is now inactive!
							return 0; //Not ready to render yet!
						}
						if (Sequencer->SpriteCRTC_virtualscanline >= VGA->precalcs.SpriteCRTCverticalwindowheight) //Already finished?
						{
							VGA->CRTC.CRTCBwindowEnabled &= ~1; //The window is now inactive!
							return 0; //Don't handle any new scanlines anymore: we're finished!
						}
						if (Sequencer->SpriteCRTCxlatched) //To latch first column?
						{
							++Sequencer->SpriteCRTCrowstep; //Next step stepping the row!
							if (Sequencer->SpriteCRTCrowstep >= VGA->precalcs.SpriteCRTCrowheight) //To reload a new row?
							{
								Sequencer->SpriteCRTC_row_address += VGA->precalcs.SpriteCRTCrowoffset; //One row ahead!
								Sequencer->SpriteCRTCrowstep = 0; //Restart the scanline counter!
								++Sequencer->SpriteCRTC_virtualscanline; //Next virtual scanline!
							}
							//Common new scanline handling!
							Sequencer->SpriteCRTCstep = 0; //Initialize the horizontal step!
							VGA_handleSpriteCRTCnewScanline(VGA, Sequencer, attributeinfo); //New scanline handling!
							Sequencer->SpriteCRTCxlatched = 0; //We're latched now!
						}
						if (Sequencer->SpriteCRTC_virtualscanline >= VGA->precalcs.SpriteCRTCverticalwindowheight) //Already finished?
						{
							VGA->CRTC.CRTCBwindowEnabled &= ~1; //The window is now inactive!
							return 0; //Don't handle any new scanlines anymore: we're finished!
						}
					}
				}
				if ((Sequencer->SpriteCRTCylatched | Sequencer->SpriteCRTCxlatched) != 0) //Not latched the start yet?
				{
					VGA->CRTC.CRTCBwindowEnabled &= ~1; //The window is now inactive!
					return 0; //Not ready to handle yet this frame!
				}
				if (Sequencer->activedisplayx >= (VGA->precalcs.SpriteCRTChorizontaldisplaydelay + (VGA->precalcs.SpriteCRTChorizontalwindowwidth*VGA->precalcs.SpriteCRTCpixelwidth))) //Out of horizontal range?
				{
					VGA->CRTC.CRTCBwindowEnabled &= ~1; //The window is now inactive!
					return 0; //Not handled!
				}
				if (Sequencer->SpriteCRTC_virtualscanline >= VGA->precalcs.SpriteCRTCverticalwindowheight) //Already finished?
				{
					VGA->CRTC.CRTCBwindowEnabled &= ~1; //The window is now inactive!
					return 0; //Don't handle any new scanlines anymore: we're finished!
				}
				VGA->CRTC.CRTCBwindowEnabled |= 1; //The window is now active!
				return VGA_SpriteCRTCGetPixel(VGA, Sequencer, attributeinfo, overrideattributeinfo); //Try and retrieve an attribute!!
			}
			else //Might be on the current scanline needing handling?
			{
				VGA->CRTC.CRTCBwindowEnabled &= ~1; //The window is now inactive!
				if (Sequencer->SpriteCRTCxlatched) //Starting horizontal display this scanline perhaps?
				{
					if (Sequencer->currentScanline == VGA->precalcs.SpriteCRTCverticaldisplaydelay) //Starting vertical display?
					{
						VGA->CRTC.CRTCBwindowEnabled |= 2; //The window is active this scanline!
					}
					else //New scanline or double scanning?
					{
						VGA->CRTC.CRTCBwindowEnabled |= 2; //The window is active this scanline!
						if (Sequencer->SpriteCRTCylatched) //Y not latched yet?
						{
							VGA->CRTC.CRTCBwindowEnabled &= ~2; //The window is inactive this scanline!
							return 0; //Not ready to render yet!
						}
						if (Sequencer->SpriteCRTC_virtualscanline >= VGA->precalcs.SpriteCRTCverticalwindowheight) //Already finished?
						{
							VGA->CRTC.CRTCBwindowEnabled &= ~2; //The window is inactive this scanline!
							return 0; //Don't handle any new scanlines anymore: we're finished!
						}
					}
				}
				if ((Sequencer->SpriteCRTCylatched | Sequencer->SpriteCRTCxlatched) != 0) //Not latched the start yet?
				{
					VGA->CRTC.CRTCBwindowEnabled &= ~2; //The window is inactive this scvanline!
					return 0; //Not ready to handle yet this frame!
				}
				if (Sequencer->SpriteCRTC_virtualscanline >= VGA->precalcs.SpriteCRTCverticalwindowheight) //Already finished?
				{
					VGA->CRTC.CRTCBwindowEnabled &= ~2; //The window is inactive this scanline!
					return 0; //Don't handle any new scanlines anymore: we're finished!
				}
				VGA->CRTC.CRTCBwindowEnabled |= 2; //The window is now active this scanline!
			}
		}
		else //Vertically out of range?
		{
			VGA->CRTC.CRTCBwindowEnabled &= ~3; //The window is now inactive and not on the current scanline!
		}
	}
	else //Fully disabled?
	{
		VGA->CRTC.CRTCBwindowEnabled &= ~3; //The window is now inactive and not on the current scanline!
	}
	return 0; //Not the CRTC window!
}

byte VGA_overrideoutputs; //Ignoring the inputs from the VGA?

//Blank handler!
void VGA_Blank_Activedisplay_VGA(VGA_Type *VGA, SEQ_DATA *Sequencer, VGA_AttributeInfo *attributeinfo)
{
	VGA_overrideoutputs = VGA_handleSpriteCRTCwindow(VGA, Sequencer, attributeinfo, attributeinfo); //Handle the Sprite/CRTC window overlay!
	++Sequencer->activedisplayx; //Next active display pixel!
	VGA->CRTC.CRTCBwindowmaxstatus = MAX(VGA->CRTC.CRTCBwindowmaxstatus, VGA->CRTC.CRTCBwindowEnabled); //Maximum status detected!
	if (hretrace) return; //Don't handle during horizontal retraces or top screen rendering!

	if ((VGA->precalcs.effectiveDACmode & 4) == 4) //Not latching in 1 raising&lowering(by the attribute controller) clock(Not mode 2, but mode 1)?
	{
		//Latch a 8-bit pixel?

		if ((++Sequencer->DACcounter) & ((4 >> attributeinfo->attributesize) - 1)) //To latch and not process yet? This is the least significant byte/bits of the counter!
		{
			return; //Skip this data: we only latch every two pixels!
		}
	}

	drawPixel(VGA, RGB(0x00, 0x00, 0x00)); //Draw blank!
	video_updateLightPen(VGA,0); //Update the light pen!
	if (++Sequencer->currentpixelclock >= Sequencer->pixelclockdivider) //Are we to tick the CRTC pixel clock?
	{
		Sequencer->currentpixelclock = 0; //Reset clock!
		++VGA->CRTC.x; //Next x!
	}
}

void VGA_handleSpriteCRTCwindowNonActiveDisplay(VGA_Type* VGA, SEQ_DATA* Sequencer, VGA_AttributeInfo* attributeinfo, VGA_AttributeInfo* overrideattributeinfo); //Prototype!

void VGA_Blank_Overscan_VGA(VGA_Type* VGA, SEQ_DATA* Sequencer, VGA_AttributeInfo* attributeinfo)
{
	VGA_handleSpriteCRTCwindowNonActiveDisplay(VGA, Sequencer, attributeinfo, &overrideattributeinfo); //Handle without display!
	VGA->CRTC.CRTCBwindowmaxstatus = MAX(VGA->CRTC.CRTCBwindowmaxstatus, VGA->CRTC.CRTCBwindowEnabled); //Maximum status detected!
	if (hretrace) return; //Don't handle during horizontal retraces or top screen rendering!

	if ((VGA->precalcs.effectiveDACmode & 8) == 0) //Normal mode?
	{
		if ((VGA->precalcs.effectiveDACmode & 4) == 4) //Not latching in 1 raising&lowering(by the attribute controller) clock(Not mode 2, but mode 1)?
		{
			//Latch a 8-bit pixel?
			if ((++Sequencer->DACcounter) & ((4 >> attributeinfo->attributesize) - 1)) //To latch and not process yet? This is the least significant byte/bits of the counter!
			{
				return; //Skip this data: we only latch every two pixels!
			}
		}
	}

	drawPixel(VGA, RGB(0x00, 0x00, 0x00)); //Draw blank!
	video_updateLightPen(VGA, 0); //Update the light pen!
	if (++Sequencer->currentpixelclock >= Sequencer->pixelclockdivider) //Are we to tick the CRTC pixel clock?
	{
		Sequencer->currentpixelclock = 0; //Reset clock!
		++VGA->CRTC.x; //Next x!
	}
}

void VGA_Blank_CGA(VGA_Type *VGA, SEQ_DATA *Sequencer, VGA_AttributeInfo *attributeinfo)
{
	if (hretrace) return; //Don't handle during horizontal retraces!
	//Normally, we convert the pixel given using the VGA attribute, but in this case we need to apply NTSC conversion from reenigne.
	if (likely(VGA->CRTC.x<NUMITEMS(CGALineBuffer))) //Valid pixel horizontally?
	{
		CGALineBuffer[VGA->CRTC.x] = 0; //Take the literal pixel color of the CGA for later NTSC conversion!
	}
	video_updateLightPen(VGA,0); //Update the light pen!
	++VGA->CRTC.x; //Next x!
}

uint_32 getrawVGADACentry(VGA_Type *VGA, byte index)
{
	return VGA_DAC(VGA,index); //Give the entry!
}

byte EGA_SyncPolarityConversion[2][0x40] = {
		{ //Positive VSync (cleared)
		0x00,0x01,0x02,0x03,0x04,0x05,0x14,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F, //Direct map 00-0F
		0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F, //10-17h to 38-3F
		0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2A,0x2B,0x2C,0x2D,0x2E,0x2F, //Direct map 20-2F
		0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F //Direct map 30-3F
		}
		,{ //Negative VSync (set)
		0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F, //Direct map 00-0F
		0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F, //10-17h to 38-3F
		0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2A,0x2B,0x2C,0x2D,0x2E,0x2F, //Direct map 20-2F
		0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F //Direct map 30-3F
		}
	};

void VGA_ActiveDisplay_noblanking_VGA(VGA_Type *VGA, SEQ_DATA *Sequencer, VGA_AttributeInfo *attributeinfo)
{
	uint_32 DACcolor; //The latched color!
	byte splittingpixels=0;
	INLINEREGISTER byte doublepixels=0;
	if (hretrace) return; //Don't handle during horizontal retraces!
	//Active display!
	nextsplitpixel: //Process the next splitting pixel if available(during the falling clock)!
	if ((VGA->precalcs.effectiveDACmode&4)==4) //Not latching in 1 raising&lowering(by the attribute controller) clock(Not mode 2, but mode 1)?
	{
		//Latch a 8-bit pixel?
		if ((VGA->precalcs.effectiveDACmode & 0x18) == 0) //Normal mode?
		{
			Sequencer->lastDACcolor >>= (4<<attributeinfo->attributesize); //Latching 4/8/16 bits, whether used or not!
			Sequencer->lastDACcolor |= ((attributeinfo->attribute)<< ((8 << (VGA->precalcs.effectiveDACmode & 2) >> 1) - (4 << attributeinfo->attributesize))); //Latching this attribute! Low byte is latched first!

			if ((++Sequencer->DACcounter)&((4>>attributeinfo->attributesize)-1)) //To latch and not process yet? This is the least significant byte/bits of the counter!
			{
				return; //Skip this data: we only latch every two pixels!
			}
		}
		else //24BPP/32BPP mode?
		{
			if ((VGA->precalcs.effectiveDACmode & 0x400) == 0) //Latching 8-bits at a time?
			{
				Sequencer->lastDACcolor >>= 8; //Latching 8 bits, whether used or not!
				Sequencer->lastDACcolor |= ((attributeinfo->attribute) << (((VGA->precalcs.effectiveDACmode & 0x8) << 1) | ((VGA->precalcs.effectiveDACmode & 0x10) >> 1))); //Latching this attribute to 24 or 32 bits! Low byte is latched first!
			}
			else //Latching 16 bits at a time instead?
			{
				Sequencer->lastDACcolor >>= 16; //Latching 16 bits, whether used or not!
				Sequencer->lastDACcolor |= ((attributeinfo->attribute) << (((VGA->precalcs.effectiveDACmode & 0x8) << 1) | ((VGA->precalcs.effectiveDACmode & 0x10) >> 1))); //Latching this attribute to 24 or 32 bits! Low byte is latched first!
			}

			if (VGA->precalcs.effectiveDACmode & 0x400) //Latching 16-bits at a time?
			{
				if ((++Sequencer->DACcounter) < 2) //To latch and not process yet? This is the least significant byte/bits of the counter!
				{
					return; //Skip this data: we only latch every two pixels!
				}
			}
			else //Latching 8 bits at a time?
			{
				if ((++Sequencer->DACcounter) < (3 + ((VGA->precalcs.effectiveDACmode & 0x10) >> 4))) //To latch and not process yet? This is the least significant byte/bits of the counter!
				{
					return; //Skip this data: we only latch every two pixels!
				}
			}
			Sequencer->DACcounter = 0; //Simply clear after every 3 pixels!
		}
	}
	else //Pseudo-color mode or Mode 2 15/16-bit DAC?
	{
		Sequencer->lastDACcolor = attributeinfo->attribute; //Latching this attribute!
		if ((attributeinfo->attributesize == 2) && ((VGA->precalcs.effectiveDACmode & 2)==0)) //Using a rising 8-bit, falling 8-bit combination to produce 2 pixels from a 16-bit rise/fall input?
		{
			if (splittingpixels==0) //Falling byte of the 16-bit clock(the rising clock has been shifted high, the falling clock isn't shifted)?
			{
				Sequencer->lastDACcolor >>= 8; //Latching this attribute!
			}
			Sequencer->lastDACcolor &= 0xFF; //Latching this attribute as 8-bit pixels!
			splittingpixels ^= 1; //Start or stop a split pixel!
		}
	}

	doublepixels = (1 << (VGA->precalcs.ClockingModeRegister_DCR & 1)); /*<<attributeinfo->attributesize)*/ //Double the pixels(half horizontal clock) and multiply for each extra pixel clock taken?

	//Only send one pixel to the display unless the dot clock is divided by 2! The dot clock input isn't the direct input of the MCLK but instead should use the output rate of the attribute controller instead(to double rendered pixels instead)!
	if ((VGA->precalcs.effectiveDACmode & 0x18) == 0) //Normal mode?
	{
		if ((VGA->precalcs.AttributeModeControlRegister_ColorEnable8Bit == 1) && ((VGA->enable_SVGA >= 1) && (VGA->enable_SVGA <= 2))) //ET4000 divide by 2 fix?
		{
			doublepixels <<= VGA->precalcs.MemoryClockDivide; //MCLK/[1/2/4] causes the output given to increase due to lower speed of the memory clock!
		}
		if ((VGA->precalcs.effectiveDACmode&4)==4) //Multiple inputs are taken?
		{
			if (((VGA->precalcs.ClockingModeRegister_DCR == 2) && (VGA->precalcs.AttributeModeControlRegister_ColorEnable8Bit == 3)) && ((VGA->enable_SVGA >= 1) && (VGA->enable_SVGA <= 2))) //ET4000 divide by 2 fix?
			{
				doublepixels <<= VGA->precalcs.MemoryClockDivide; //Double the pixels being plotted during active display!
			}
		}
	}
	if (VGA->precalcs.effectiveDACmode & 0x10) //Actually 4 times bigger?
	{
		doublepixels <<= 2; //Times 4!
	}
	else if (VGA->precalcs.effectiveDACmode & 8) //Actually 3 times bigger?
	{
		doublepixels += (doublepixels << 1); //Times 3!
	}

	//Convert the pixel to a RGB value before drawing any blocks of pixels!
	//bit 5 of the DAC mode: 0=RGB(A) mode(blue first), 1=BGR(A) mode(red first)
	if (VGA->precalcs.effectiveDACmode&2) //16-bit/24-bit color?
	{
		//Now draw in the selected color depth!
		if (VGA->precalcs.effectiveDACmode&0x10) //32-bit color?
		{
			if (VGA->precalcs.effectiveDACmode & 0x800) //True RGBA?
			{
				Sequencer->lastDACcolor &= (getActiveVGA()->precalcs.SC15025_pixelmaskregister|0xFF000000U); //Apply the pixel mask, but with the 3rd byte enabled!
				if ((VGA->precalcs.effectiveDACmode & 0x20)==0) //Not RGBA mode instead of BGRA mode for output?
				{
					Sequencer->lastDACcolor = (((Sequencer->lastDACcolor >> 16) & 0xFF) | (Sequencer->lastDACcolor & 0xFF00FF00)) | ((Sequencer->lastDACcolor & 0xFF) << 16); //Convert RGBA to BGRA!
				}
				DACcolor = RGBA(((Sequencer->lastDACcolor) & 0xFF), ((Sequencer->lastDACcolor >> 8) & 0xFF), ((Sequencer->lastDACcolor >> 16) & 0xFF), ((Sequencer->lastDACcolor >> 24) & 0xFF)); //Draw the 32BPP color pixel!
			}
			else //Plain RGBA?
			{
				Sequencer->lastDACcolor &= getActiveVGA()->precalcs.SC15025_pixelmaskregister; //Apply the pixel mask!
				if ((VGA->precalcs.effectiveDACmode & 0x20)==0) //RGB mode instead of BGR mode?
				{
					Sequencer->lastDACcolor = (((Sequencer->lastDACcolor >> 16) & 0xFF) | (Sequencer->lastDACcolor & 0xFF00)) | ((Sequencer->lastDACcolor & 0xFF) << 16); //Convert RGBA to BGRA!
				}
				DACcolor = RGBA((Sequencer->lastDACcolor & 0xFF), ((Sequencer->lastDACcolor >> 8) & 0xFF), ((Sequencer->lastDACcolor >> 16) & 0xFF), 0xFF); //Draw the 32BPP color pixel!
			}
		}
		else if (VGA->precalcs.effectiveDACmode&8) //24-bit color?
		{
			Sequencer->lastDACcolor &= getActiveVGA()->precalcs.SC15025_pixelmaskregister; //Apply the pixel mask!
			if (VGA->precalcs.effectiveDACmode & 0x20) //RGB mode instead of BGR mode?
			{
				Sequencer->lastDACcolor = (((Sequencer->lastDACcolor >> 16) & 0xFF) | (Sequencer->lastDACcolor & 0xFF00)) | ((Sequencer->lastDACcolor & 0xFF) << 16); //Convert RGB to BGR!
			}
			DACcolor = RGB(((Sequencer->lastDACcolor>>16)&0xFF),((Sequencer->lastDACcolor>>8)&0xFF),(Sequencer->lastDACcolor&0xFF)); //Draw the 24BPP color pixel!
		}
		else if (VGA->precalcs.effectiveDACmode&1) //16-bit color?
		{
			Sequencer->lastDACcolor &= getActiveVGA()->precalcs.SC15025_pixelmaskregister; //Apply the pixel mask!
			DACcolor = CLUT16bit[(Sequencer->lastDACcolor&0xFFFF)]; //Draw the 16-bit color pixel!
			if (VGA->precalcs.DACmode & 0x40) //LUT enabled?
			{
				DACcolor = RGB(
					((GETR(DACcolor) >> 2) | ((VGA->precalcs.effectiveDACmode >> 1) & 0xC0)), //Red channel!
					((GETG(DACcolor) >> 2) | ((VGA->precalcs.effectiveDACmode >> 1) & 0xC0)), //Green channel!
					((GETB(DACcolor) >> 2) | ((VGA->precalcs.effectiveDACmode >> 1) & 0xC0)) //Blue channel!
					);
			}
		}
		else //15-bit color?
		{
			Sequencer->lastDACcolor &= getActiveVGA()->precalcs.SC15025_pixelmaskregister; //Apply the pixel mask!
			DACcolor = CLUT15bit[(Sequencer->lastDACcolor&0xFFFF)]; //Draw the 15-bit color pixel!
			if (VGA->precalcs.DACmode & 0x40) //LUT enabled?
			{
				DACcolor = RGB(
					((GETR(DACcolor) >> 2) | ((VGA->precalcs.effectiveDACmode >> 1) & 0xC0)), //Red channel!
					((GETG(DACcolor) >> 2) | ((VGA->precalcs.effectiveDACmode >> 1) & 0xC0)), //Green channel!
					((GETB(DACcolor) >> 2) | ((VGA->precalcs.effectiveDACmode >> 1) & 0xC0)) //Blue channel!
				);
				if (VGA->precalcs.effectiveDACmode & 0x20) //Extended mode instead of RGB mode?
				{
					DACcolor = RGB(
						(GETR(DACcolor) | ((Sequencer->lastDACcolor >> 15) & 0x01)), //Red channel!
						(GETG(DACcolor) | ((Sequencer->lastDACcolor >> 15) & 0x01)), //Green channel!
						(GETB(DACcolor) | ((Sequencer->lastDACcolor >> 15) & 0x01)) //Blue channel!
					);
				}
			}
			else if (VGA->precalcs.effectiveDACmode & 0x20) //Extended mode instead of RGB mode?
			{
				//LUT is disabled!
				DACcolor = RGB(
					(GETR(DACcolor) | ((Sequencer->lastDACcolor >> 13) & 0x04)), //Red channel!
					(GETG(DACcolor) | ((Sequencer->lastDACcolor >> 13) & 0x04)), //Green channel!
					(GETB(DACcolor) | ((Sequencer->lastDACcolor >> 13) & 0x04)) //Blue channel!
					);
			}
		}
		//Final step in the translation to the on-screen color: LUT itself and final color conversion if required!
		if (VGA->precalcs.DACmode & 0x40) //LUT enabled?
		{
			DACcolor = RGB(
				GETR(getrawVGADACentry(VGA,GETR(DACcolor))), //Red channel!
				GETG(getrawVGADACentry(VGA,GETG(DACcolor))), //Green channel!
				GETB(getrawVGADACentry(VGA,GETB(DACcolor))) //Blue channel!
			); //Translate through DAC!
		}
		DACcolor = GA_color2bw(DACcolor, ((VGA->precalcs.DACmode & 0x1000)>>12)); //Apply the finished color! Use RGBA instead of RGB when specified!
	}
	else //VGA compatibility mode? 8-bit color!
	{
		if (VGA->precalcs.EGA_DisableInternalVideoDrivers) //Special case: internal video drivers disabled?
		{
			DACcolor = VGA_DAC(VGA,VGA->CRTC.DACOutput = (VGA->registers->ExternalRegisters.FEATURECONTROLREGISTER&3)); //The FEAT0 and FEAT1 outputs become the new output!
		}
		else
		{
			VGA->CRTC.DACOutput = (Sequencer->lastDACcolor&0xFF); //DAC index!
			DACcolor = (Sequencer->lastDACcolor & 0xFF); //The color to parse!
			if (getActiveVGA()->enable_SVGA == 3) //EGA needs 4-bit to 6-bit mapping depending on polarity?
			{
				DACcolor = EGA_SyncPolarityConversion[GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER, 7, 1)][(DACcolor & 0x3F)]; //Process EGA VSync polarity required for correct colors to be createn by the display!
			}
			DACcolor = VGA_DAC(VGA,(byte)DACcolor); //Render through the 8-bit DAC!
		}
	}

	DACcolor = RGBA(VGA->DACbrightness[GETR(DACcolor)], VGA->DACbrightness[GETG(DACcolor)], VGA->DACbrightness[GETB(DACcolor)],GETA(DACcolor)); //Make sure we're active display levels of brightness!

	if (VGA->precalcs.turnDACoff) //Turning the DAC off?
	{
		DACcolor = RGB(0x00, 0x00, 0x00); //No output on the DAC!
	}
	//Draw the pixel(s) that is/are latched!
	do //We always render at least 1 pixel from the DAC!
	{
		drawPixel(VGA, DACcolor); //Draw the color pixel(s)!
		video_updateLightPen(VGA,1); //Update the light pen!
		if (++Sequencer->currentpixelclock >= Sequencer->pixelclockdivider) //Are we to tick the CRTC pixel clock?
		{
			Sequencer->currentpixelclock = 0; //Reset clock!
			++VGA->CRTC.x; //Next x!
		}
	} while (--doublepixels); //Any pixels left to render?
	if (unlikely(splittingpixels)) goto nextsplitpixel; //Handle the falling clock if we're using a 8-bit rising/falling method!
}

void VGA_ActiveDisplay_noblanking_CGA(VGA_Type *VGA, SEQ_DATA *Sequencer, VGA_AttributeInfo *attributeinfo)
{
	if (hretrace) return; //Don't handle during horizontal retraces!
	//Active display!
	//Normally, we convert the pixel given using the VGA attribute, but in this case we need to apply NTSC conversion from reenigne.
	if (likely(VGA->CRTC.x<NUMITEMS(CGALineBuffer))) //Valid pixel horizontally?
	{
		CGALineBuffer[VGA->CRTC.x] = (byte)attributeinfo->attribute; //Take the literal pixel color of the CGA for later NTSC conversion!
	}
	video_updateLightPen(VGA,1); //Update the light pen!
	++VGA->CRTC.x; //Next x!
}

void VGA_Overscan_noblanking_VGA(VGA_Type *VGA, SEQ_DATA *Sequencer, VGA_AttributeInfo *attributeinfo)
{
	uint_32 DACcolor;
	VGA->CRTC.CRTCBwindowEnabled &= ~1; //Not active right now!
	VGA_handleSpriteCRTCwindowNonActiveDisplay(VGA, Sequencer, attributeinfo, &overrideattributeinfo); //Handle without display!
	VGA->CRTC.CRTCBwindowmaxstatus = MAX(VGA->CRTC.CRTCBwindowmaxstatus, VGA->CRTC.CRTCBwindowEnabled); //Maximum status detected!
	if (hretrace) return; //Don't handle during horizontal retraces!

	if ((VGA->precalcs.effectiveDACmode & 4) == 4) //Not latching in 1 raising&lowering(by the attribute controller) clock(Not mode 2, but mode 1)?
	{
		//Latch a 8-bit pixel?
		if ((++Sequencer->DACcounter) & ((4 >> attributeinfo->attributesize) - 1)) //To latch and not process yet? This is the least significant byte/bits of the counter!
		{
			return; //Skip this data: we only latch every two pixels!
		}
	}
	//Overscan!
	if (VGA->precalcs.EGA_DisableInternalVideoDrivers) //Special case: internal video drivers disabled?
	{
		VGA->CRTC.DACOutput = (VGA->registers->ExternalRegisters.FEATURECONTROLREGISTER&3); //The FEAT0 and FEAT1 outputs become the new output!
		if (VGA->precalcs.turnDACoff) //Turning the DAC off?
		{
			drawPixel(VGA, RGB(0x00, 0x00, 0x00)); //Draw blanked!
		}
		else
		{
			DACcolor = VGA->CRTC.DACOutput; //The color to convert!
			if (getActiveVGA()->enable_SVGA == 3) //EGA needs 4-bit to 6-bit mapping depending on polarity?
			{
				DACcolor = EGA_SyncPolarityConversion[GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER, 7, 1)][(DACcolor & 0x3F)]; //Process EGA VSync polarity required for correct colors to be createn by the display!
			}
			DACcolor = VGA_DAC(VGA, (byte)DACcolor); //What color to render?
			DACcolor = RGB(VGA->DACbrightness[GETR(DACcolor)], VGA->DACbrightness[GETG(DACcolor)], VGA->DACbrightness[GETB(DACcolor)]); //Make sure we're active display levels of brightness!
			drawPixel(VGA, DACcolor); //Draw overscan in the specified color instead!
		}
	}
	else //Normal VGA behaviour?
	{
		VGA->CRTC.DACOutput = VGA->precalcs.overscancolor; //Overscan index!
		if (VGA->precalcs.turnDACoff) //Turning the DAC off?
		{
			drawPixel(VGA, RGB(0x00, 0x00, 0x00)); //Draw blanked!
		}
		else
		{
			DACcolor = VGA->CRTC.DACOutput; //The color to convert!
			if (getActiveVGA()->enable_SVGA == 3) //EGA needs 4-bit to 6-bit mapping depending on polarity?
			{
				DACcolor = EGA_SyncPolarityConversion[GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER, 7, 1)][(DACcolor & 0x3F)]; //Process EGA VSync polarity required for correct colors to be createn by the display!
			}
			DACcolor = VGA_DAC(VGA, (byte)DACcolor); //Draw overscan!
			DACcolor = RGB(VGA->DACbrightness[GETR(DACcolor)], VGA->DACbrightness[GETG(DACcolor)], VGA->DACbrightness[GETB(DACcolor)]); //Make sure we're active display levels of brightness!
			drawPixel(VGA, DACcolor); //Draw overscan in the specified color instead!
		}
	}
	video_updateLightPen(VGA,0); //Update the light pen!
	if (++Sequencer->currentpixelclock >= Sequencer->pixelclockdivider) //Are we to tick the CRTC pixel clock?
	{
		Sequencer->currentpixelclock = 0; //Reset clock!
		++VGA->CRTC.x; //Next x!
	}
}

void VGA_Overscan_noblanking_CGA(VGA_Type *VGA, SEQ_DATA *Sequencer, VGA_AttributeInfo *attributeinfo)
{
	if (hretrace) return; //Don't handle during horizontal retraces!
	//Overscan!
	//Normally, we convert the pixel given using the VGA attribute, but in this case we need to apply NTSC conversion from reenigne.
	if (likely(VGA->CRTC.x<NUMITEMS(CGALineBuffer))) //Valid pixel horizontally?
	{
		CGALineBuffer[VGA->CRTC.x] = VGA->precalcs.overscancolor; //Take the literal pixel color of the CGA for later NTSC conversion!
	}
	video_updateLightPen(VGA,0); //Update the light pen!
	++VGA->CRTC.x; //Next x!
}

void updateVGASequencer_Mode(VGA_Type *VGA)
{
	VGA->precalcs.extrasignal = VGA->precalcs.graphicsmode?VGA_DISPLAYGRAPHICSMODE:0x0000; //Apply the current mode (graphics vs text mode)!
	updateVGADAC_Mode(VGA); //Update the effective DAC mode!
	updateSequencerPixelDivider(VGA, (SEQ_DATA*)VGA->Sequencer); //Update the sequencer as well!
	updateVGAAttributeController_Mode(VGA); //Update the attribute mode!
}

void updateVGADAC_Mode(VGA_Type* VGA)
{
	VGA->precalcs.effectiveDACmode = VGA->precalcs.DACmode; //Use the selected DAC mode!
	if (unlikely((VGA->precalcs.graphicsmode == 0) && (VGA->precalcs.effectiveDACmode & 0x1F))) //Not in graphics mode? Force 8-bit DAC compatiblity mode!
	{
		VGA->precalcs.effectiveDACmode = 0; //Force VGA-compatible text mode!
	}
}

VGA_Sequencer_Mode activedisplay_noblanking_handler = NULL;
VGA_Sequencer_Mode activedisplay_blank_handler = NULL;
VGA_Sequencer_Mode overscan_noblanking_handler = NULL;
VGA_Sequencer_Mode overscan_blank_handler = NULL;

//Active display handler!
void VGA_ActiveDisplay_Text(SEQ_DATA *Sequencer, VGA_Type *VGA)
{
	VGA_overrideoutputs = 0; //Default: not overriding anything!
	//Render our active display here!
	retryTimingText: //For linear mode!
	if (VGA_ActiveDisplay_timing(Sequencer, VGA)) //Execute our timings!
	{
		VGA_Sequencer_TextMode(VGA,Sequencer,&currentattributeinfo); //Get the color to render!
		if (VGA_AttributeController(&currentattributeinfo,VGA))
		{
			if ((VGA->precalcs.AttributeModeControlRegister_ColorEnable8Bit & 2) && (VGA->precalcs.ClockingModeRegister_DCR & 2) == 2) //Special mode active?
			{
				goto retryTimingText; //Retry text timing!
			}
			return; //Nibbled!
		}
		VGA_overrideoutputs = VGA_handleSpriteCRTCwindow(VGA, Sequencer, &currentattributeinfo, &overrideattributeinfo); //Handle the Sprite/CRTC window overlay!
		VGA->CRTC.CRTCBwindowmaxstatus = MAX(VGA->CRTC.CRTCBwindowmaxstatus, VGA->CRTC.CRTCBwindowEnabled); //Maximum status detected!
	}
	else if (!CGAMDARenderer) return; //Don't render when not ticking!

	++Sequencer->activedisplayx; //Next active display pixel!
	if (VGA_overrideoutputs == 1) //Size selected by inputs?
	{
		memcpy(&overrideattributeinfo, &currentattributeinfo, sizeof(overrideattributeinfo)); //Copy to use!
		if (overrideattributeinfo.attributesize == 2) //2 bytes?
		{
			overrideattributeinfo.attribute = 0; //00h attribute!
		}
		else
		{
			overrideattributeinfo.attribute = 0; //00h attribute!
		}
	}
	else if (VGA_overrideoutputs == 2) //Inverted instead?
	{
		memcpy(&overrideattributeinfo, &currentattributeinfo, sizeof(overrideattributeinfo)); //Copy to use!
		if (overrideattributeinfo.attributesize == 2) //2-byte?
		{
			overrideattributeinfo.attribute ^= 0xFFFF; //Flip 16-bit!
		}
		else //1-byte?
		{
			overrideattributeinfo.attribute ^= 0xFF; //Flip 8-bit!
		}
	}
	else if (VGA_overrideoutputs == 3) //Font instead?
	{
		memcpy(&overrideattributeinfo, &currentattributeinfo, sizeof(overrideattributeinfo)); //Copy to use!
		if (overrideattributeinfo.attributesize == 2) //2 bytes?
		{
			overrideattributeinfo.attribute = 0xFFFF; //FFh attribute!
		}
		else
		{
			overrideattributeinfo.attribute = 0xFF; //FFh attribute!
		}
	}
	activedisplay_noblanking_handler(VGA, Sequencer, VGA_overrideoutputs ? &overrideattributeinfo : &currentattributeinfo); //Blank or active display!
}

void VGA_ActiveDisplay_Text_blanking(SEQ_DATA *Sequencer, VGA_Type *VGA)
{
	VGA_overrideoutputs = 0; //Default: not overriding anything!
	Sequencer->DACcounter = 0; //Reset the DAC counter: the DAC starts scanning again after blanking ends!
	//Render our active display here!
	retryTimingTextBlanking: //For linear mode!
	if (VGA_ActiveDisplay_timing(Sequencer, VGA)) //Execute our timings!
	{
		VGA_Sequencer_TextMode(VGA, Sequencer, &currentattributeinfo); //Get the color to render!
		if (VGA_AttributeController(&currentattributeinfo, VGA))
		{
			Sequencer->DACcounter = 0; //Reset the DAC counter: the DAC starts scanning again after blanking ends!
			if ((VGA->precalcs.AttributeModeControlRegister_ColorEnable8Bit & 2) && (VGA->precalcs.ClockingModeRegister_DCR & 2) == 2) //Special mode active?
			{
				goto retryTimingTextBlanking; //Retry text timing!
			}
			return; //Nibbled!
		}
		VGA_overrideoutputs = VGA_handleSpriteCRTCwindow(VGA, Sequencer, &currentattributeinfo, &overrideattributeinfo); //Handle the Sprite/CRTC window overlay!
		VGA->CRTC.CRTCBwindowmaxstatus = MAX(VGA->CRTC.CRTCBwindowmaxstatus, VGA->CRTC.CRTCBwindowEnabled); //Maximum status detected!
		Sequencer->DACcounter = 0; //Reset the DAC counter: the DAC starts scanning again after blanking ends!
	}
	else if (!CGAMDARenderer) return; //Don't render when not ticking!

	++Sequencer->activedisplayx; //Next active display pixel!
	if (VGA_overrideoutputs == 1) //Size selected by inputs?
	{
		memcpy(&overrideattributeinfo, &currentattributeinfo, sizeof(overrideattributeinfo)); //Copy to use!
		if (overrideattributeinfo.attributesize == 2) //2 bytes?
		{
			overrideattributeinfo.attribute = 0; //00h attribute!
		}
		else
		{
			overrideattributeinfo.attribute = 0; //00h attribute!
		}
	}
	else if (VGA_overrideoutputs == 2) //Inverted instead?
	{
		memcpy(&overrideattributeinfo, &currentattributeinfo, sizeof(overrideattributeinfo)); //Copy to use!
		if (overrideattributeinfo.attributesize == 2) //2-byte?
		{
			overrideattributeinfo.attribute ^= 0xFFFF; //Flip 16-bit!
		}
		else //1-byte?
		{
			overrideattributeinfo.attribute ^= 0xFF; //Flip 8-bit!
		}
	}
	else if (VGA_overrideoutputs == 3) //Font instead?
	{
		memcpy(&overrideattributeinfo, &currentattributeinfo, sizeof(overrideattributeinfo)); //Copy to use!
		if (overrideattributeinfo.attributesize == 2) //2 bytes?
		{
			overrideattributeinfo.attribute = 0xFFFF; //FFh attribute!
		}
		else
		{
			overrideattributeinfo.attribute = 0xFF; //FFh attribute!
		}
	}
	Sequencer->DACcounter = 0; //Reset the DAC counter: the DAC starts scanning again after blanking ends!
	activedisplay_blank_handler(VGA, Sequencer, VGA_overrideoutputs ? &overrideattributeinfo : &currentattributeinfo); //Blank or active display!
	Sequencer->DACcounter = 0; //Reset the DAC counter: the DAC starts scanning again after blanking ends!
}

void VGA_ActiveDisplay_Graphics(SEQ_DATA *Sequencer, VGA_Type *VGA)
{
	VGA_overrideoutputs = 0; //Default: not overriding anything!
	//Render our active display here!
	retryTimingGraphics: //For linear mode!
	if (VGA_ActiveDisplay_timing(Sequencer, VGA)) //Execute our timings!
	{
		VGA_Sequencer_GraphicsMode(VGA, Sequencer, &currentattributeinfo); //Get the color to render!
		if (VGA_AttributeController(&currentattributeinfo, VGA))
		{
			if ((VGA->precalcs.AttributeModeControlRegister_ColorEnable8Bit & 2) && (VGA->precalcs.ClockingModeRegister_DCR & 2) == 2) //Special mode active?
			{
				goto retryTimingGraphics; //Retry text timing!
			}
			return; //Nibbled!
		}
		VGA_overrideoutputs = VGA_handleSpriteCRTCwindow(VGA, Sequencer, &currentattributeinfo, &overrideattributeinfo); //Handle the Sprite/CRTC window overlay!
		VGA->CRTC.CRTCBwindowmaxstatus = MAX(VGA->CRTC.CRTCBwindowmaxstatus, VGA->CRTC.CRTCBwindowEnabled); //Maximum status detected!
	}
	else if (!CGAMDARenderer) return; //Don't render when not ticking!

	++Sequencer->activedisplayx; //Next active display pixel!
	if (VGA_overrideoutputs == 1) //Size selected by inputs?
	{
		memcpy(&overrideattributeinfo, &currentattributeinfo, sizeof(overrideattributeinfo)); //Copy to use!
		if (overrideattributeinfo.attributesize == 2) //2 bytes?
		{
			overrideattributeinfo.attribute = 0; //00h attribute!
		}
		else
		{
			overrideattributeinfo.attribute = 0; //00h attribute!
		}
	}
	else if (VGA_overrideoutputs == 2) //Inverted instead?
	{
		memcpy(&overrideattributeinfo, &currentattributeinfo, sizeof(overrideattributeinfo)); //Copy to use!
		if (overrideattributeinfo.attributesize == 2) //2-byte?
		{
			overrideattributeinfo.attribute ^= 0xFFFF; //Flip 16-bit!
		}
		else //1-byte?
		{
			overrideattributeinfo.attribute ^= 0xFF; //Flip 8-bit!
		}
	}
	else if (VGA_overrideoutputs == 3) //Font instead?
	{
		memcpy(&overrideattributeinfo, &currentattributeinfo, sizeof(overrideattributeinfo)); //Copy to use!
		if (overrideattributeinfo.attributesize == 2) //2 bytes?
		{
			overrideattributeinfo.attribute = 0xFFFF; //FFh attribute!
		}
		else
		{
			overrideattributeinfo.attribute = 0xFF; //FFh attribute!
		}
	}
	activedisplay_noblanking_handler(VGA, Sequencer, VGA_overrideoutputs ? &overrideattributeinfo : &currentattributeinfo); //Blank or active display!
}

void VGA_ActiveDisplay_Graphics_blanking(SEQ_DATA *Sequencer, VGA_Type *VGA)
{
	VGA_overrideoutputs = 0; //Default: not overriding anything!
	//Render our active display here! Start with text mode!		
	retryTimingGraphicsBlanking: //For linear mode!
	if (VGA_ActiveDisplay_timing(Sequencer,VGA)) //Execute our timings!
	{
		VGA_Sequencer_GraphicsMode(VGA, Sequencer, &currentattributeinfo); //Get the color to render!
		if (VGA_AttributeController(&currentattributeinfo, VGA))
		{
			Sequencer->DACcounter = 0; //Reset the DAC counter: the DAC starts scanning again after blanking ends!
			if ((VGA->precalcs.AttributeModeControlRegister_ColorEnable8Bit & 2) && (VGA->precalcs.ClockingModeRegister_DCR & 2) == 2) //Special mode active?
			{
				goto retryTimingGraphicsBlanking; //Retry text timing!
			}
			return; //Nibbled!
		}
		VGA_overrideoutputs = VGA_handleSpriteCRTCwindow(VGA, Sequencer, &currentattributeinfo, &overrideattributeinfo); //Handle the Sprite/CRTC window overlay!
		VGA->CRTC.CRTCBwindowmaxstatus = MAX(VGA->CRTC.CRTCBwindowmaxstatus, VGA->CRTC.CRTCBwindowEnabled); //Maximum status detected!
	}
	else if (!CGAMDARenderer) return; //Don't render when not ticking!

	++Sequencer->activedisplayx; //Next active display pixel!
	if (VGA_overrideoutputs == 1) //Size selected by inputs?
	{
		memcpy(&overrideattributeinfo, &currentattributeinfo, sizeof(overrideattributeinfo)); //Copy to use!
		if (overrideattributeinfo.attributesize == 2) //2 bytes?
		{
			overrideattributeinfo.attribute = 0; //00h attribute!
		}
		else
		{
			overrideattributeinfo.attribute = 0; //00h attribute!
		}
	}
	else if (VGA_overrideoutputs == 2) //Inverted instead?
	{
		memcpy(&overrideattributeinfo, &currentattributeinfo, sizeof(overrideattributeinfo)); //Copy to use!
		if (overrideattributeinfo.attributesize == 2) //2-byte?
		{
			overrideattributeinfo.attribute ^= 0xFFFF; //Flip 16-bit!
		}
		else //1-byte?
		{
			overrideattributeinfo.attribute ^= 0xFF; //Flip 8-bit!
		}
	}
	else if (VGA_overrideoutputs == 3) //Font instead?
	{
		memcpy(&overrideattributeinfo, &currentattributeinfo, sizeof(overrideattributeinfo)); //Copy to use!
		if (overrideattributeinfo.attributesize == 2) //2 bytes?
		{
			overrideattributeinfo.attribute = 0xFFFF; //FFh attribute!
		}
		else
		{
			overrideattributeinfo.attribute = 0xFF; //FFh attribute!
		}
	}
	Sequencer->DACcounter = 0; //Reset the DAC counter: the DAC starts scanning again after blanking ends!
	activedisplay_blank_handler(VGA, Sequencer, VGA_overrideoutputs ? &overrideattributeinfo : &currentattributeinfo); //Blank or active display!
	Sequencer->DACcounter = 0; //Reset the DAC counter: the DAC starts scanning again after blanking ends!
}

//Overscan handler!
void VGA_Overscan(SEQ_DATA *Sequencer, VGA_Type *VGA)
{
	overscan_noblanking_handler(VGA,Sequencer, &currentattributeinfo);
}

void VGA_Overscan_blanking(SEQ_DATA *Sequencer, VGA_Type *VGA)
{
	overscan_blank_handler(VGA, Sequencer, &currentattributeinfo);
}

void updateCGAMDARenderer() //Update the renderer to use!
{
	if (unlikely(CGAMDARenderer)) //CGA/MDA rendering mode?
	{
		activedisplay_noblanking_handler = &VGA_ActiveDisplay_noblanking_CGA; //Blank or active display!
		activedisplay_blank_handler = &VGA_Blank_CGA; //Blank or active display!
		overscan_noblanking_handler = &VGA_Overscan_noblanking_CGA; //Attribute info isn't used!
		overscan_blank_handler = &VGA_Blank_CGA; //Attribute info isn't used!
	}
	else //VGA+ rendering mode?
	{
		activedisplay_noblanking_handler = &VGA_ActiveDisplay_noblanking_VGA; //Blank or active display!
		activedisplay_blank_handler = &VGA_Blank_Activedisplay_VGA; //Blank or active display!
		overscan_noblanking_handler = &VGA_Overscan_noblanking_VGA; //Attribute info isn't used!
		overscan_blank_handler = &VGA_Blank_Overscan_VGA; //Attribute info isn't used!
	}
}

//Combination functions of the above:

//Horizontal before vertical, retrace before total.

extern byte charxbuffer[256]; //Full character inner x location!

//Initialise all handlers!
void initStateHandlers()
{
	uint_32 i;
	//Default uninitialised entries!
	displayrenderhandler[0][0] = &VGA_NOP; //Default: no action!
	displayrenderhandler[1][0] = &VGA_NOP; //Default: no action!
	displayrenderhandler[2][0] = &VGA_NOP; //Default: no action!
	displayrenderhandler[3][0] = &VGA_NOP; //Default: no action!

	updateCGAMDARenderer(); //Initialise the renderer for usage!

	for (i=1;i<VGA_DISPLAYRENDERSIZE;i++) //Fill the normal entries!
	{
		//Total handler for total handlers!
		displayrenderhandler[1][i] = &VGA_NOP; //Do nothing when disabled: retrace does no output!
		displayrenderhandler[2][i] = &VGA_NOP; //Do nothing when disabled: total handler!
		displayrenderhandler[3][i] = &VGA_NOP; //Do nothing when disabled: total&retrace handler!
		
		//Rendering handler without retrace AND total!
		displayrenderhandler[0][i] = ((i&VGA_DISPLAYMASK)==VGA_DISPLAYACTIVE)?((i&VGA_DISPLAYGRAPHICSMODE)?((i&VGA_SIGNAL_BLANKING)?&VGA_ActiveDisplay_Graphics_blanking: &VGA_ActiveDisplay_Graphics): ((i&VGA_SIGNAL_BLANKING) ? &VGA_ActiveDisplay_Text_blanking : &VGA_ActiveDisplay_Text)):((i&VGA_SIGNAL_BLANKING) ? &VGA_Overscan : &VGA_Overscan_blanking); //Not retracing or any total handler = display/overscan!
	}

	for (i = 0;i < 0x10000;++i) //Create the 16&15-bit CLUT! The format is Red on MSB, Green in the middle and Blue in the LSB.
	{
		CLUT16bit[i] = RGB((byte)(((i >> 11) & 0x1F)<<3), (byte)(((i >> 5) & 0x3F)<<2), (byte)(((i >> 0) & 0x1F)<<3)); //16-bit color lookup table (5:6:5 format)! Lower 2 bits or 2 bits of each color channel are cleared!
		CLUT15bit[i] = RGB((byte)(((i >> 10) & 0x1F)<<3), (byte)(((i >> 5) & 0x1F)<<3), (byte)(((i >> 0) & 0x1F)<<3)); //15-bit color lookup table (5:5:5 format)! Lower 3 bits of each color channel are cleared!
	}
	memset(&charxbuffer,0xFF,sizeof(charxbuffer)); //Character x buffer!
	for (i=0;i<9;++i)
	{
		charxbuffer[i] = i; //We're this inner pixel!
	}
	VGA_GraphicsDecoder(getActiveVGA(),0); //Load initial data!
	VGA_TextDecoder(getActiveVGA(),0); //Load initial data!
}

/*

Now, the renderer basic, timings etc.

*/

//0=Automatic synchronization, 1=Tightly synchronized with the CPU emulation.
void setVGASpeed(byte setting)
{
	if (setting) //New style setting?
	{
		if (setting==1) //Modern Automatic synchronization and request to tightly synchronize?
		{
			if (currentVGASpeed) //Set?
			{
				currentVGASpeed = 0; //Start tight synchronization!
			}
		}
		else if ((!currentVGASpeed) && (setting!=2)) //Tightly synchronized and request to use automatic synchronization?
		{
			passedcounter = LIMITVGA; //Start speed detection with this many items!
			currentVGASpeed = 1; //Start automatic synchronization!
			SynchronizationMode = 1; //New style synchronization!
		}
		else if (setting == 2) //Tightly synchonized with the CPU?
		{
			passedcounter = 0; //No sync!
			currentVGASpeed = 0;
			SynchronizationMode = 0; //Unknown!
			VGA_limit = 0; //Unlimited!
		}
		//When there's no change, do nothing!
	}
	else //Old style synchronization method?
	{
		currentVGASpeed = 1; //Start automatic synchronization!
		passedcounter = 1; //Don't apply passed counter! As long as we're >0 to apply synchronization!
		SynchronizationMode = 0; //Old style synchronization!		
	}
}

void adjustVGASpeed()
{
	#ifdef LIMITVGA
	passedcounter = LIMITVGA; //Start counting this many times before locking to the speed!
	#endif
}

void changeRowTimer(VGA_Type *VGA) //Change the VGA row processing timer the ammount of lines on display!
{
	#ifdef __HW_DISABLED
	return; //Disabled?
	#endif
	DOUBLE rate;
	rate = VGA_VerticalRefreshRate(VGA); //Get our rate first!
	if (unlikely(rate!=oldrate)) //New rate has been specified?
	{
		oldrate = rate; //We've updated to this rate!
		#ifdef IS_LONGDOUBLE
		VGA_rendertiming = (float)(1000000000.0L/rate); //Handle this rate from now on! Keep us locked though to prevent screen updates messing with this!
		#else
		VGA_rendertiming = (float)(1000000000.0/rate); //Handle this rate from now on! Keep us locked though to prevent screen updates messing with this!
		#endif
		adjustVGASpeed(); //Auto-adjust our speed!
	}
}

void VGA_initTimer()
{
	VGA_timing = 0.0f; //We're starting to run now!
	oldrate = VGA_VerticalRefreshRate(getActiveVGA()); //Initialise the default rate!
	#ifdef IS_LONGDOUBLE
	VGA_rendertiming = (float)(1000000000.0L/oldrate); //Handle this rate from now on!
	#else
	VGA_rendertiming = (float)(1000000000.0/oldrate); //Handle this rate from now on!
	#endif
	initTicksHolder(&VGA_test);
	adjustVGASpeed(); //Auto-adjust our speed!
}

extern GPU_type GPU;

extern byte allcleared; //Are all pointers cleared?

OPTINLINE byte doVGA_Sequencer() //Do we even execute?
{
	if (unlikely((getActiveVGA()==NULL) || allcleared)) //Invalid VGA? Don't do anything!
	{
		return 0; //Abort: we're disabled without a invalid VGA!
	}
	if (unlikely(GPU.emu_screenbuffer==NULL)) //Invalid screen buffer?
	{
		return 0; //Abort: we're disabled!
	}
	return 1; //We can render something!
}

byte isoutputdisabled = 0; //Output disabled?

//Special states!
extern byte blanking; //Are we blanking!
extern byte retracing; //Allow rendering by retrace!
extern byte totalling; //Allow rendering by total!
byte totalretracing; //Combined flags of retracing/totalling!

extern byte hblank, hretrace; //Horizontal blanking/retrace?
extern byte vblank, vretrace; //Vertical blanking/retrace?
byte hblankendpending = 0; //Ending blank/retrace pending? bits set for any of them!
byte vblankendpending = 0; //Ending blank/retrace pending? bits set for any of them!

byte vtotal = 0, htotal = 0; //V/HTotal busy?

byte VGA_hblankstart = 0; //HBlank started?
extern byte CGAMDARenderer; //CGA/MDA renderer?

OPTINLINE uint_32 get_display(VGA_Type *VGA, SEQ_DATA *Sequencer, word Scanline, word x) //Get/adjust the current display part for the next pixel (going from 0-total on both x and y)!
{
	INLINEREGISTER uint_32 stat; //The status of the pixel!
	//We are a maximum of 4096x1024 size!
	Sequencer->currentScanline = Scanline; //What scanline!
	Scanline >>= VGA->precalcs.CRTCModeControlRegister_SLDIV; //Apply Scan Doubling on the row scan counter: we take effect on content (double scanning)!
	Sequencer->currentx = x; //What coordinate on the scanline!
	if (unlikely(Scanline > 0x7FFF)) Scanline = 0x7FFF; //Clip to the allowed range!
	if (unlikely(x >= 0x7FFF)) x = 0x7FFF; //Clip to the allowed range!
	Scanline &= 0x7FFF; //Range safety: 4095 scanlines!
	x &= 0x7FFF; //Range safety: 4095 columns!
	stat = VGA->CRTC.rowstatus[Scanline]; //Get row status!
	stat |= VGA->CRTC.colstatus[x]; //Get column status!
	stat |= VGA->precalcs.extrasignal; //Graphics mode etc. display status affects the signal too!
	stat |= (blanking<<VGA_SIGNAL_BLANKINGSHIFT); //Apply the current blanking signal as well!
	VGA_hblankstart = stat; //Save directly! Ignore the overflow!
	VGA_hblankstart >>= 7; //Shift into bit 1 to get the hblank status(small hack)!
	return stat; //Give the combined (OR'ed) status!
}

word displaystate; //Last display state!

//All possible wait-states for the video adapter!
typedef void (*WaitStateHandler)(VGA_Type *VGA);

void updateVGAWaitState(); //Prototype!

void WaitState_None(VGA_Type *VGA) {} //No waitstate: NOP!
void WaitState_WaitDots(VGA_Type *VGA)
{
	//Wait 8 hdots!
	if (unlikely(--VGA->WaitStateCounter == 0)) //First wait state done?
	{
		VGA->WaitState = 2; //Enter the next phase: Wait for the next lchar(16 dots period)!
		updateVGAWaitState(); //Update the waitstate!
	}
}
void WaitState_NextlChar(VGA_Type *VGA)
{
	//Wait for the next lchar?
	if (unlikely((VGA->PixelCounter & 0xF) == 0)) //Second wait state done?
	{
		VGA->WaitState = 0; //Enter the next phase: Wait for the next ccycle(3 hdots)
		CPU[activeCPU].halt |= 8; //Start again when the next CPU clock arrives!
		CPU[activeCPU].halt &= ~4; //We're done waiting!
		updateVGAWaitState(); //Update the waitstate!
	}
}

WaitStateHandler CurrentWaitState = NULL; //Current waitstate!

WaitStateHandler WaitStates[8] = { NULL,
WaitState_WaitDots, //Wait 8 hdots!
WaitState_NextlChar, //Wait for the next lchar?
WaitState_None, //Wait for the next ccycle(3 hdots)?
WaitState_None,WaitState_None,WaitState_None,WaitState_None }; //All possible waitstates!

void updateVGAWaitState()
{
	CurrentWaitState = WaitStates[getActiveVGA()->WaitState]; //Load the new waitstate!
}

//HBlank/Retrace handling!

typedef void (*hblankretraceHandler)(SEQ_DATA *Sequencer, VGA_Type *VGA, word signal);

void exechblankretrace(SEQ_DATA *Sequencer, VGA_Type *VGA, word signal)
{
	if (unlikely(VGA_hblankstart)) //HBlank start?
	{
		hblank = 1; //We're blanking!
	}
	else if (unlikely(hblank))
	{
		if (unlikely(signal&VGA_SIGNAL_HBLANKEND)) //HBlank end?
		{
			if ((VGA->registers->specialCGAMDAflags) & 1)
			{
				//End pending HBlank!
				hblank = 0; //We're not blanking anymore!
				hblankendpending = 0; //Remove from flags pending!
			}
			else
			{
				hblankendpending = 1;
			}
		}
	}

	if (unlikely(signal&VGA_SIGNAL_HRETRACESTART)) //HRetrace start?
	{
		if (unlikely(hretrace==0)) //Not running yet?
		{
			VGA_HRetrace(Sequencer, VGA); //Execute the handler!
		}
		hretrace = 1; //We're retracing!
	}
	else if (unlikely(hretrace))
	{
		if (unlikely(signal&VGA_SIGNAL_HRETRACEEND)) //HRetrace end?
		{
			hretrace = 0; //We're not retracing anymore!
		}
		else //HRetrace pending?
		{
			VGA_HRetracePending(Sequencer, VGA); //Execute the handler!
		}
	}
}

void nohblankretrace(SEQ_DATA *Sequencer, VGA_Type *VGA, word signal)
{
	if (likely(hblankendpending==0)) return; //End pending HBlank!
	{
		hblank = 0; //We're not blanking anymore!
		hblankendpending = 0; //Remove from flags pending!
	}
}

extern byte is_XT; //Are we emulating an XT architecture?

void VGA_triggerVerticalRetraceInterrupt(VGA_Type *VGA, byte isCRTC)
{
	if (isCRTC != VGA->precalcs.VerticalRetraceInterruptSource) return; //Not the selected vertical retrace interrupt source?
	//EGA/VGA vertical retrace interrupt support!
	if (GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.VERTICALRETRACEENDREGISTER, 4, 1)) //Enabled vertical retrace interrupt?
	{
		if (!GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.VERTICALRETRACEENDREGISTER, 5, 1)) //Generate vertical retrace interrupts? When set, disables the interrupt from happening!
		{
			if (VGA->registers->verticalinterruptflipflop == 0) //Not set yet?
			{
				raiseirq(is_XT ? VGA_IRQ_XT : VGA_IRQ_AT); //Execute the CRT interrupt when possible!
				VGA->registers->verticalinterruptflipflop = 1; //We're pending an CRT interrupt!
			}
		}
	}
}

OPTINLINE void VGA_SIGNAL_HANDLER(SEQ_DATA *Sequencer, VGA_Type *VGA, byte *totalretracing, byte hblankretrace)
{
	const static byte retracemasks[4] = { 0xFF,0x00,0x00,0x00 }; //Disable display when retracing!
	const static hblankretraceHandler hblankretracehandlers[2] = { nohblankretrace,exechblankretrace }; //The handlers!
	byte totalcheckpending;

	INLINEREGISTER word tempsignalbackup, tempsignal; //Our signal backup and signal itself!
	totalcheckpending = 0; //Default: total check isn't pending!
recalcsignal: //Recalculate the signal to process!
	tempsignal = tempsignalbackup = displaystate; //The back-up of the signal!
	//Blankings

	hblankretracehandlers[hblankretrace](Sequencer,VGA,tempsignal); //Horizontal timing?

	tempsignal = tempsignalbackup; //Restore the original backup signal!
	tempsignal &= VGA_VBLANKRETRACEMASK; //Check for blanking/tretracing!
	if (unlikely(tempsignal)) //VBlank?
	{
		if (unlikely(tempsignal&VGA_SIGNAL_VBLANKSTART)) //VBlank start?
		{
			vblank = 1; //We're blanking!
		}
		else if (unlikely(vblank))
		{
			if (unlikely(tempsignal&VGA_SIGNAL_VBLANKEND)) //VBlank end?
			{
				if (VGA->registers->specialCGAMDAflags & 1) //CGA special?
				{
					vblank = 0; //We're not blanking anymore!
					vblankendpending = 0; //Remove from flags pending!
				}
				else
				{
					vblankendpending = 1; //Start pending vblank end!
				}
			}
		}

		if (unlikely(tempsignal&VGA_SIGNAL_VRETRACESTART)) //VRetrace start?
		{
			if (unlikely(vretrace==0)) //Not running yet?
			{
				VGA_VRetrace(Sequencer, VGA); //Execute the handler!

				VGA_triggerVerticalRetraceInterrupt(VGA, 0); //Vertical retrace interrupt triggered!
				if (VGA->enable_SVGA != 4) //Not CGA/MDA?
				{
					//The end of vertical retrace has been reached, reload start address!
					Sequencer->frame_startmap = VGA->precalcs.startaddress; //What start address to use for the next frame?
					Sequencer->frame_bytepanning = VGA->precalcs.PresetRowScanRegister_BytePanning; //Byte panning is latched as well!
				}
			}
			SETBITS(VGA->registers->ExternalRegisters.INPUTSTATUS1REGISTER,3,1,(vretrace = 1)); //We're retracing!
		}
		else if (unlikely(vretrace))
		{
			if (unlikely(tempsignal&VGA_SIGNAL_VRETRACEEND)) //VRetrace end?
			{
				vretrace = 0; //We're not retracing anymore!
				SETBITS(VGA->registers->ExternalRegisters.INPUTSTATUS1REGISTER,3,1,vretrace); //Vertical retrace?
				Sequencer->frame_pixelshiftcount = VGA->precalcs.pixelshiftcount; //Pixel shift count is latched during vertical retrace ending, according to Dosbox!
			}
			else
			{
				SETBITS(VGA->registers->ExternalRegisters.INPUTSTATUS1REGISTER,3,1,vretrace); //Vertical retrace?
				VGA_VRetracePending(Sequencer, VGA); //Execute the handler!
			}
		}
		else //No vretrace?
		{
			SETBITS(VGA->registers->ExternalRegisters.INPUTSTATUS1REGISTER,3,1,vretrace); //Vertical retrace?
		}
	}
	else
	{
		if (unlikely(vblankendpending)) //End pending HBlank!
		{
			vblank = 0; //We're not blanking anymore!
			vblankendpending = 0; //Remove from flags pending!
		}
		SETBITS(VGA->registers->ExternalRegisters.INPUTSTATUS1REGISTER,3,1,vretrace); //No vertical retrace?
	}

	//Both H&VBlank count!
	blanking = hblank;
	blanking |= vblank; //Process blank!
	//Screen disable applies blanking permanently!
	blanking |= GETBITS(VGA->registers->SequencerRegisters.REGISTERS.CLOCKINGMODEREGISTER,5,1); //Use disabled output when asked to!
	blanking |= (VGA_3C0_PALR^1); //Clearing the Attribute disables the display signal, blanking it!
	blanking |= VGA->registers->CRTControllerDontRender; //Disable rendering?

	//Process resetting the HSync/VSync counters!

	INLINEREGISTER byte isretrace; //Vertical or horizontal retrace?
	*totalretracing = isretrace = hretrace;
	retracing = (isretrace |= vretrace); //We're retracing?

	//Process HTotal/VTotal
	tempsignal = tempsignalbackup; //Restore the original backup signal!
	if (unlikely(tempsignal&VGA_SIGNAL_HTOTAL)) //HTotal?
	{
		VGA_HTotal(Sequencer,VGA); //Process HTotal!
		displaystate = get_display(getActiveVGA(), Sequencer, Sequencer->Scanline, Sequencer->x++); //Current display state!
		tempsignal = tempsignalbackup = displaystate; //The back-up of the signal!
		htotal = 1; //Triggered horizontal total!
	}
	else if (unlikely(htotal)) //HTotal ended?
	{
		htotal = 0; //Not horizontal total anymore!
	}
	if (unlikely(tempsignal&VGA_SIGNAL_VTOTAL)) //VTotal?
	{
		VGA_VTotal(Sequencer,VGA); //Process VTotal!
		displaystate = get_display(getActiveVGA(), Sequencer, Sequencer->Scanline, Sequencer->x++); //Current display state, keep x coordinate(retain x coordinate on the next frame)!
		tempsignal = tempsignalbackup = displaystate; //The back-up of the signal!
		vtotal = 1;
		Sequencer->frame_latchpending = 1; //Latch is pending!
	}
	else if (unlikely(vtotal)) //VTotal ended?
	{
		vtotal = 0; //Not vertical total anymore!
	}

	if (unlikely(htotal|vtotal)) //Not infinitely looping(V/HTotal pending)?
	{
		hblankretrace = (displaystate & VGA_HBLANKRETRACEMASK) ? 1 : 0; //Check for blanking/retracing!
		if (likely(totalcheckpending<3)) //Not an infinite loop?
		{
			++totalcheckpending; //We're rechecking, don't check again!
			goto recalcsignal;
		}
	}

	tempsignal &= VGA_DISPLAYMASK; //Check the display now!

	INLINEREGISTER byte currenttotalretracing;
	currenttotalretracing = (tempsignal==VGA_DISPLAYACTIVE); //We're active display when not retracing/totalling and active display area!
	currenttotalretracing &= retracemasks[isretrace]; //Apply the retrace mask: we're not using the displayenabled when retracing!
	VGA->CRTC.DisplayEnabled = currenttotalretracing; //The Display Enable signal, which depends on the active video adapter how to use it!
	if (unlikely(currenttotalretracing && Sequencer->frame_latchpending)) //Display became active again and a latch for it is pending?
	{
		Sequencer->pixelshiftcount_cleared = 0; //Not cleared from now on!
		Sequencer->frame_latchpending = 0; //Latch isn't pending anymore!
		Sequencer->frame_topwindowstart = VGA->precalcs.topwindowstart; //When does the top window start?
		VGA_Sequencer_updateRow(VGA, Sequencer,1); //Update the row information with the updated values!
	}
	++VGA->PixelCounter; //Simply blindly increase the pixel counter!

	if (unlikely(CurrentWaitState)) CurrentWaitState(VGA); //Execute the current waitstate, when used!
}

extern DisplayRenderHandler displayrenderhandler[4][VGA_DISPLAYRENDERSIZE]; //Our handlers for all pixels!

OPTINLINE void VGA_Renderer(SEQ_DATA *Sequencer)
{
	static byte totalretracing = 0;
	//Process one pixel only!
	displaystate = get_display(getActiveVGA(), Sequencer, Sequencer->Scanline, Sequencer->x++); //Current display state!
	VGA_SIGNAL_HANDLER(Sequencer, getActiveVGA(),&totalretracing,(displaystate&VGA_HBLANKRETRACEMASK)?1:0); //Handle any change in display state first!
	displayrenderhandler[totalretracing][displaystate](Sequencer, getActiveVGA()); //Execute our signal!
	if (((getActiveVGA()->CRTC.CRTCBwindowmaxstatus ^ getActiveVGA()->CRTC.CRTCBwindowEnabled) & getActiveVGA()->CRTC.CRTCBwindowmaxstatus) & 2) //CRTCB window is finished rendering (the scanline active marker has been lowered)?
	{
		getActiveVGA()->CRTC.CRTCBwindowmaxstatus = 0; //Clear the max status to prevent retriggering!
		VGA_triggerVerticalRetraceInterrupt(getActiveVGA(), 1); //CRTCB interrupt triggered!
	}
	Tseng4k_tickAccelerator(); //Tick the accelerator one clock, if it's present and operating!
}

//CPU cycle locked version of VGA rendering!
void updateVGA(DOUBLE timepassed, uint_32 MHZ14passed)
{
	#ifdef LIMITVGA
	float limitcalc=0;
	uint_32 renderingsbackup=0;
	float timeprocessed=0.0;
	#endif
	if (unlikely(VGA_debugtiming_enabled)) //Valid debug timing to apply?
	{
		VGA_debugtiming += timepassed; //Time has passed!
	}

	INLINEREGISTER uint_32 renderings; //How many clocks to render?
	renderings = MHZ14passed; //Default to 14MHz clock from the motherboard!
	if (unlikely(getActiveVGA()->precalcs.use14MHzclock==0)) //Not using 14MHz clocking?
	{
		VGA_timing += timepassed; //Time has passed!

		if (unlikely((VGA_timing >= VGA_rendertiming) && VGA_rendertiming)) //Might have passed?
		{
			renderings = (uint_32)floorf((float)(VGA_timing/VGA_rendertiming)); //Ammount of times to render!
			VGA_timing -= (renderings*VGA_rendertiming); //Rest the amount we can process!
		}
		else
		{
			renderings = 0; //Nothing to render!
		}
	}

	if (unlikely(renderings)) //Anything to render?
	{
		#ifdef LIMITVGA
		if ((renderings>VGA_limit) && VGA_limit) //Limit broken?
		{
			renderings = (uint_32)VGA_limit; //Limit the processing to the amount of time specified!
		}
		#endif
		if (unlikely(renderings==0)) return; //Nothing to render!
		#ifdef LIMITVGA
		if (passedcounter && currentVGASpeed) //Still counting?
		{
			timeprocessed = (renderings*VGA_rendertiming); //How much are we processing?
			renderingsbackup = renderings; //Save the backup for comparision!
			VGA_vtotal = 0; //Reset our flag to detect finish of a frame while measuring!
		}
		#endif

		if (unlikely(doVGA_Sequencer()==0)) return; //Don't execute the sequencer if requested to!

		SEQ_DATA *Sequencer;
		Sequencer = GETSEQUENCER(getActiveVGA()); //Our sequencer!

		//All possible states!
		if (unlikely(displayrenderhandler[0][0]==0)) initStateHandlers(); //Init our display states for usage when needed!
		if (unlikely(Sequencer->extrastatus==0)) Sequencer->extrastatus = &getActiveVGA()->CRTC.extrahorizontalstatus[0]; //Start our extra status at the beginning of the row!

		#ifdef LIMITVGA
		if (unlikely(passedcounter && currentVGASpeed)) getnspassed(&VGA_test); //Still counting? Then count our interval!
		#endif
		do
		{
			if (likely(renderings==1)) VGA_Renderer(Sequencer); //2+ optimization? Not usable? Execute only once!
			else //2+ optimization?
			{
				switch (renderings-1) //How many to render(1-5 clocks multiple optimizations)?
				{
				case 3: //4 rendering?
					VGA_Renderer(Sequencer); //Tick the VGA once!
				case 2: //3 rendering?
					VGA_Renderer(Sequencer); //Tick the VGA once!
				case 1: //2 rendering?
					VGA_Renderer(Sequencer); //Tick the VGA once!
					renderings -= (renderings-1); //We've processed 1-3 more!
				case 0: //1 rendering?
					VGA_Renderer(Sequencer); //Tick the VGA once!
					break;
				default: //5+ optimization?
					VGA_Renderer(Sequencer); //Tick the VGA once!
					VGA_Renderer(Sequencer); //Tick the VGA once!
					VGA_Renderer(Sequencer); //Tick the VGA once!
					VGA_Renderer(Sequencer); //Tick the VGA once!
					VGA_Renderer(Sequencer); //Tick the VGA once!
					renderings -= 4; //We've processed 4 more!
					break;
				}
			}
		} while (--renderings); //Ticks left to tick?

		SETBITS(getActiveVGA()->registers->ExternalRegisters.INPUTSTATUS1REGISTER,0,1,(getActiveVGA()->CRTC.DisplayEnabled^1)); //Only update the display disabled when required to: it's only needed by the CPU, not the renderer!

		#ifdef LIMITVGA
		if (unlikely(passedcounter && currentVGASpeed)) //Still counting?
		{
			limitcalc = getnspassed(&VGA_test); //How long have we taken?

			//timeprocessed=how much time to use, limitcalc=how much time we have taken, renderingsbackup=How many pixels have we processed.
			VGA_limit = floorf(((float)renderingsbackup/(float)limitcalc)*timeprocessed); //Don't process any more than we're allowed to (timepassed).
			if (limitcalc<=timeprocessed) VGA_limit = 0; //Don't limit if we're running at full speed (we're below time we are allowed to process)!
			if (SynchronizationMode) --passedcounter; //A part has been rendered! Only with
		}
		#endif
	}
}

extern BIOS_Settings_TYPE BIOS_Settings; //Our settings!

void EMU_update_VGA_Settings() //Update the VGA settings!
{
	DAC_Use_BWluminance(BIOS_Settings.bwmonitor_luminancemode); //Luminance type!
	DAC_Use_BWMonitor((BIOS_Settings.bwmonitor>0) ? 1 : 0); //Select color/bw monitor!
	if (DAC_Use_BWMonitor(0xFF)) //Using a b/w monitor?
	{
		DAC_BWColor(BIOS_Settings.bwmonitor); //Set the color to use!
	}
	switch (BIOS_Settings.VGA_Mode) //What precursor compatibility mode?
	{
		default: //Pure VGA?
		case 6: //Tseng ET4000?
		case 0: //Pure VGA?
		case 8: //EGA?
			setVGA_NMIonPrecursors(0); //No NMI on precursors!
			setVGA_CGA(0); //No CGA!
			setVGA_MDA(0); //No MDA!
			break;
		case 1: //VGA with NMI?
			setVGA_NMIonPrecursors(BIOS_Settings.VGA_Mode); //Set NMI on precursors!
			setVGA_CGA(0); //No CGA!
			setVGA_MDA(0); //No MDA!
			break;
		case 2: //VGA with CGA?
			setVGA_NMIonPrecursors(0); //Disable NMI on precursors!
			setVGA_CGA(2); //CGA enabled with VGA!
			setVGA_MDA(0); //No MDA!
			break;
		case 3: //VGA with MDA?
			setVGA_NMIonPrecursors(0); //Disable NMI on precursors!
			setVGA_CGA(0); //No CGA!
			setVGA_MDA(2); //MDA enabled with VGA!
			break;
		case 4: //Pure CGA?
			setVGA_NMIonPrecursors(0); //Disable NMI on precursors!
			setVGA_CGA(1); //Pure CGA!
			setVGA_MDA(0); //No MDA!
			break;
		case 5: //Pure MDA?
			setVGA_NMIonPrecursors(0); //Disable NMI on precursors!
			setVGA_CGA(0); //No CGA!
			setVGA_MDA(1); //Pure MDA!
			break;
	}
	setVGASpeed(BIOS_Settings.VGASynchronization); //Apply VGA synchronization setting!
	if (getActiveVGA()) //Gotten an active VGA?
	{
		DAC_updateEntries(getActiveVGA()); //Update all DAC entries according to the current/new color settings!
		byte CGAMode;
		CGAMode = BIOS_Settings.CGAModel; //What CGA is emulated?
		if ((CGAMode&3)!=CGAMode) CGAMode = 0; //Default to RGB, old-style CGA!
		setCGA_NTSC(CGAMode&1); //RGB with modes 0&2, NTSC with modes 1&3
		setCGA_NewCGA(CGAMode&2); //New-style with modes 2&3, Old-style with modes 0&1
		VGA_initColorLevels(getActiveVGA(), BIOS_Settings.video_blackpedestal); //Initialize the color levels to use for output!
	}
}
