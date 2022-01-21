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

#include "headers/hardware/vga/vga.h" //VGA support (plus precalculation!)
#include "headers/hardware/vga/vga_precalcs.h" //Precalculation typedefs etc.
#include "headers/hardware/vga/vga_crtcontroller.h"
#include "headers/hardware/vga/vga_sequencer_graphicsmode.h" //Graphics mode updating support!
#include "headers/hardware/vga/vga_attributecontroller.h" //Attribute controller support!
#include "headers/hardware/vga/vga_renderer.h" //Sequencer render counter support!
#include "headers/hardware/vga/vga_vramtext.h" //VRAM text support!
#include "headers/hardware/vga/vga_dacrenderer.h" //B/W detection support!
#include "headers/hardware/vga/vga_cga_mda.h" //CGA/MDA support!
#include "headers/hardware/vga/vga_vram.h" //Mapping support for different addressing modes!
#include "headers/hardware/vga/svga/tseng.h" //Our own typedefs for ET3000/ET4000!
#include "headers/support/log.h" //Logging support!

void VGA_updateVRAMmaps(VGA_Type *VGA); //VRAM map updater prototype!

//Works!
OPTINLINE uint_32 getcol256(VGA_Type *VGA, byte color) //Convert color to RGB!
{
	byte DACbits;
	DACEntry colorEntry; //For getcol256!
	DACbits = (0x3F | VGA->precalcs.emulatedDACextrabits); //How many DAC bits to use?
	readDAC(VGA, (color & VGA->registers->DACMaskRegister), &colorEntry); //Read the DAC entry, masked on/off by the DAC Mask Register!
	return RGB(convertrel((colorEntry.r & DACbits), DACbits, 0xFF), convertrel((colorEntry.g & DACbits), DACbits, 0xFF), convertrel((colorEntry.b & DACbits), DACbits, 0xFF)); //Convert using DAC (Scale of DAC is RGB64, we use RGB256)!
}

extern byte VGA_LOGPRECALCS; //Are we manually updated to log?

void VGA_calcprecalcs_CRTC(void *useVGA) //Precalculate CRTC precalcs!
{
	VGA_Type *VGA = (VGA_Type *)useVGA; //The VGA to use!
	uint_32 current;
	byte charsize,textcharsize;
	//Column and row status for each pixel on-screen!
	charsize = getcharacterheight(VGA); //First, based on height!
	current = 0; //Init!
	for (;current<NUMITEMS(VGA->CRTC.rowstatus);) //All available resolutions!
	{
		VGA->CRTC.charrowstatus[current<<1] = current/charsize;
		VGA->CRTC.charrowstatus[(current<<1)|1] = current%charsize;
		VGA->CRTC.rowstatus[current] = get_display_y(VGA,current); //Translate!
		++current; //Next!
	}

	//Horizontal coordinates!
	charsize = getcharacterwidth(VGA); //Now, based on width!
	textcharsize = gettextcharacterwidth(VGA); //Text character width instead!
	current = 0; //Init!
	word extrastatus;
	byte pixelrate=1;
	byte innerpixel;
	byte fetchrate=0; //Half clock fetch!
	byte pixelticked=0; //Pixel has been ticked?
	byte clockrate;
	byte firstfetch=1; //First fetch is ignored!
	byte graphicshalfclockrate = 0; //Graphics half clock rate!
	byte usegraphicsrate;
	usegraphicsrate = VGA->precalcs.graphicsmode; //Are we in graphics mode?
	clockrate = (((VGA->precalcs.ClockingModeRegister_DCR&1) | (CGA_DOUBLEWIDTH(VGA) ? 1 : 0))); //The clock rate to run the VGA clock at!
	byte theshift = 0;
	switch (VGA->precalcs.ClockingModeRegister_DCR)
	{
	case 0:
		theshift = 0; //Handle normally? VGA-compatible!
		break;
	case 1:
		theshift = 1; //Handle normally? VGA-compatible!
		break;
	case 3:
		theshift = 1; //Handle normally? VGA-incompatible!
		break;
	case 2: //Special mode?
		theshift = 0; //Handle normally? VGA-comaptible!
		break;
	}
	for (;current<NUMITEMS(VGA->CRTC.colstatus);)
	{
		VGA->CRTC.charcolstatus[current<<1] = current/charsize;
		VGA->CRTC.charcolstatus[(current<<1)|1] = current%charsize; //Doesn't affect the rendering process itself!
		VGA->CRTC.textcharcolstatus[current << 1] = current / textcharsize;
		VGA->CRTC.textcharcolstatus[(current << 1) | 1] = innerpixel = current % textcharsize;
		if (usegraphicsrate) //Graphics mode is used? Don't use the extended text-mode sizes!
		{
			VGA->CRTC.textcharcolstatus[current << 1] = VGA->CRTC.charcolstatus[current << 1];
			innerpixel = (byte)(VGA->CRTC.textcharcolstatus[(current << 1) | 1] = VGA->CRTC.charcolstatus[(current << 1) | 1]);
		}
		VGA->CRTC.colstatus[current] = get_display_x(VGA,((current>>theshift))); //Translate to display rate!

		//Determine some extra information!
		extrastatus = 0; //Initialise extra horizontal status!
		
		if (((VGA->registers->specialCGAflags|VGA->registers->specialMDAflags)&1) && !CGA_DOUBLEWIDTH(VGA)) //Affect by 620x200/320x200 mode?
		{
			extrastatus |= 1; //Always render like we are asked, at full resolution single pixels!
			pixelticked = 1; //A pixel has been ticked!
		}
		else //Normal VGA?
		{
			if (++pixelrate>clockrate) //To read the pixel every or every other pixel(forced every clock in CGA normal mode)?
			{
				extrastatus |= 1; //Reset for the new block/next pixel!
				pixelrate = 0; //Reset!
				pixelticked = 1; //A pixel has been ticked!
			}
			else
			{
				pixelticked = 0; //Not ticked!
			}
		}

		if (pixelticked)
		{
			if (innerpixel == 0) //First pixel of a character(loading)?
			{
				fetchrate = 0; //Reset fetching for the new character!
			}

			//Tick fetch rate!
			++fetchrate; //Fetch ticking!
			if (usegraphicsrate) //Use 4 pixel clocking?
			{
				if ((++graphicshalfclockrate & 3) == 1) goto tickdiv4; //Tick 1&5, use 4 clock division for pixels 1&5, ignoring character width completely!
			}
			else if (((fetchrate == 1) || (fetchrate == 5))) //Half clock rate? Tick clocks 1&5 out of 8 or 9+!
			{
				tickdiv4: //Graphics DIV4 clock!
				if (!firstfetch) //Not the first fetch?
				{
					extrastatus |= 2; //Half pixel clock for division in graphics rates!
				}
				else --firstfetch; //Not the first fetch anymore!
			}
			pixelticked = 0; //Not ticked anymore!
		}

		if (current < NUMITEMS(VGA->CRTC.extrahorizontalstatus)) //Valid to increase?
		{
			extrastatus |= 4; //Allow increasing to prevent overflow if not allowed!
		}
		VGA->CRTC.extrahorizontalstatus[current] = extrastatus; //Extra status to apply!

		//Finished horizontal timing!
		++current; //Next!
	}
}

void dump_CRTCTiming()
{
	uint_32 i;
	char information[0x1000];
	memset(&information,0,sizeof(information)); //Init!
	lockVGA(); //We don't want to corrupt the renderer's data!
	for (i=0;i<NUMITEMS(getActiveVGA()->CRTC.rowstatus);i++)
	{
		snprintf(information,sizeof(information),"Row #%" SPRINTF_u_UINT32 "=",i); //Current row!
		word status;
		status = getActiveVGA()->CRTC.rowstatus[i]; //Read the status for the row!
		if (status&VGA_SIGNAL_VTOTAL)
		{
			safescatnprintf(information,sizeof(information),"+VTOTAL"); //Add!
		}
		if (status&VGA_SIGNAL_VRETRACESTART)
		{
			safescatnprintf(information,sizeof(information),"+VRETRACESTART"); //Add!
		}
		if (status&VGA_SIGNAL_VRETRACEEND)
		{
			safescatnprintf(information,sizeof(information),"+VRETRACEEND"); //Add!
		}
		if (status&VGA_SIGNAL_VBLANKSTART)
		{
			safescatnprintf(information,sizeof(information),"+VBLANKSTART"); //Add!
		}
		if (status&VGA_SIGNAL_VBLANKEND)
		{
			safescatnprintf(information,sizeof(information),"+VBLANKEND"); //Add!
		}
		if (status&VGA_VACTIVEDISPLAY)
		{
			safescatnprintf(information,sizeof(information),"+VACTIVEDISPLAY"); //Add!
		}
		if (status&VGA_OVERSCAN)
		{
			safescatnprintf(information,sizeof(information),"+OVERSCAN"); //Add!
		}
		if (status&VGA_SIGNAL_VSYNCRESET)
		{
			safescatnprintf(information,sizeof(information),"+VSYNCRESET"); //Add!
		}
		dolog("VGA","%s",information);
		if (status&VGA_SIGNAL_VTOTAL) break; //Total reached? Don't look any further!
	}

	for (i=0;i<NUMITEMS(getActiveVGA()->CRTC.colstatus);i++)
	{
		snprintf(information,sizeof(information),"Col #%" SPRINTF_u_UINT32 "=",i); //Current row!
		word status, extrahorizontalstatus;
		status = getActiveVGA()->CRTC.colstatus[i]; //Read the status for the column!
		extrahorizontalstatus = getActiveVGA()->CRTC.extrahorizontalstatus[i]; //Read the extra status for the column!
		if (status&VGA_SIGNAL_HTOTAL)
		{
			safescatnprintf(information,sizeof(information),"+HTOTAL"); //Add!
		}
		if (status&VGA_SIGNAL_HRETRACESTART)
		{
			safescatnprintf(information,sizeof(information),"+HRETRACESTART"); //Add!
		}
		if (status&VGA_SIGNAL_HRETRACEEND)
		{
			safescatnprintf(information,sizeof(information),"+HRETRACEEND"); //Add!
		}
		if (status&VGA_SIGNAL_HBLANKSTART)
		{
			safescatnprintf(information,sizeof(information),"+HBLANKSTART"); //Add!
		}
		if (status&VGA_SIGNAL_HBLANKEND)
		{
			safescatnprintf(information,sizeof(information),"+HBLANKEND"); //Add!
		}
		if (status&VGA_HACTIVEDISPLAY)
		{
			safescatnprintf(information,sizeof(information),"+HACTIVEDISPLAY"); //Add!
		}
		if (status&VGA_OVERSCAN)
		{
			safescatnprintf(information,sizeof(information),"+OVERSCAN"); //Add!
		}
		if (status&VGA_SIGNAL_HSYNCRESET)
		{
			safescatnprintf(information,sizeof(information),"+HSYNCRESET"); //Add!
		}
		if (extrahorizontalstatus & 1)
		{
			safescatnprintf(information,sizeof(information),"+WRITEBACK"); //Add!
		}
		if (extrahorizontalstatus & 2)
		{
			safescatnprintf(information,sizeof(information), "+HALFCLOCK"); //Add!
		}
		if (extrahorizontalstatus & 4)
		{
			safescatnprintf(information,sizeof(information), "+NEXTCLOCK"); //Add!
		}
		dolog("VGA","%s",information);
		if (status&VGA_SIGNAL_HTOTAL)
		{
			unlockVGA(); //We're finished with the VGA!
			return; //Total reached? Don't look any further!
		}
	}
	unlockVGA(); //We're finished with the VGA!
}

void VGA_LOGCRTCSTATUS()
{
	lockVGA(); //We don't want to corrupt the renderer's data!
	if (!getActiveVGA())
	{
		unlockVGA(); //We're finished with the VGA!
		return; //No VGA available!
	}
	//Log all register info:
	dolog("VGA","CRTC Info:");
	dolog("VGA","HDispStart:%u",getActiveVGA()->precalcs.horizontaldisplaystart); //Horizontal start
	dolog("VGA","HDispEnd:%u",getActiveVGA()->precalcs.horizontaldisplayend); //Horizontal End of display area!
	dolog("VGA","HBlankStart:%u",getActiveVGA()->precalcs.horizontalblankingstart); //When to start blanking horizontally!
	dolog("VGA", "HBlankStartFinish:%u", getActiveVGA()->precalcs.horizontalblankingstartfinish); //When to start blanking horizontally!
	dolog("VGA","HBlankEnd:~%u",getActiveVGA()->precalcs.horizontalblankingend); //When to stop blanking horizontally after starting!
	dolog("VGA","HRetraceStart:%u",getActiveVGA()->precalcs.horizontalretracestart); //When to start vertical retrace!
	dolog("VGA", "HRetraceStartFinish:%u", getActiveVGA()->precalcs.horizontalretracestartfinish); //When to start vertical retrace!
	dolog("VGA","HRetraceEnd:~%u",getActiveVGA()->precalcs.horizontalretraceend); //When to stop vertical retrace.
	dolog("VGA","HTotal:%u",getActiveVGA()->precalcs.horizontaltotal); //Horizontal total (full resolution plus horizontal retrace)!
	dolog("VGA","VDispEnd:%u",getActiveVGA()->precalcs.verticaldisplayend); //Vertical Display End Register value!
	dolog("VGA","VBlankStart:%u",getActiveVGA()->precalcs.verticalblankingstart); //Vertical Blanking Start value!
	dolog("VGA","VBlankEnd:~%u",getActiveVGA()->precalcs.verticalblankingend); //Vertical Blanking End value!
	dolog("VGA","VRetraceStart:%u",getActiveVGA()->precalcs.verticalretracestart); //When to start vertical retrace!
	dolog("VGA","VRetraceEnd:~%u",getActiveVGA()->precalcs.verticalretraceend); //When to stop vertical retrace.
	dolog("VGA","VTotal:%u",getActiveVGA()->precalcs.verticaltotal); //Full resolution plus vertical retrace!
	unlockVGA(); //We're finished with the VGA!
}

void checkCGAcursor(VGA_Type *VGA)
{
	if (GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.CURSORSTARTREGISTER,0,0x1F)>GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.CURSORENDREGISTER,0,0x1F)) //We're past: display split cursor!
		VGA->registers->specialCGAflags |= 0x8; //Set special CGA flag: split cursor!
	else
		VGA->registers->specialCGAflags &= ~0x8; //Clear special CGA flag: normal cursor!
}

extern byte CGAMDARenderer;
extern byte VGAROM_mapping; //Default: all mapped in!
extern byte VGA_WriteMemoryMode, VGA_ReadMemoryMode; //Write/read memory modes used for accessing VRAM!

VGA_calcprecalcsextensionhandler VGA_precalcsextensionhandler = NULL; //Our precalcs extension handler!

void VGA_calcprecalcs(void *useVGA, uint_32 whereupdated) //Calculate them, whereupdated: where were we updated?
{
	//All our flags for updating sections related!
	byte recalcScanline = 0, recalcAttr = 0, ClocksUpdated = 0, updateCRTC = 0, charwidthupdated = 0, underlinelocationupdated = 0; //Default: don't update!
	byte pattern; //The pattern to use!
	VGA_Type *VGA = (VGA_Type *)useVGA; //The VGA!
	byte FullUpdate = (whereupdated==0); //Fully updated?
//Calculate the precalcs!
	//Sequencer_Textmode: we update this always!
	byte CRTUpdated=0, updateCGACRTCONTROLLER=0;
	byte CRTUpdatedCharwidth=0;
	byte overflowupdated=0;
	byte graphicsmodechanges=0;

	if (whereupdated==WHEREUPDATED_ALL) //Update all? Init!
	{
		VGAROM_mapping = 0xFF; //Disable the custom VGA ROM mapping used for normal VGA, allow the full ROM mapping!
	}

	CGAMDARenderer = CGAMDAEMULATION_RENDER(VGA)?1:0; //Render CGA/MDA style?
	updateCGAMDARenderer(); //Update the renderer to behave in the correct way!

	if ((whereupdated == (WHEREUPDATED_MISCOUTPUTREGISTER)) || FullUpdate) //Misc output register updated?
	{
		VGA_updateVRAMmaps(VGA); //Update the active VRAM maps!

		recalcAttr |= (VGA->precalcs.LastMiscOutputRegister^VGA->registers->ExternalRegisters.MISCOUTPUTREGISTER)&1; //We've updated bit 1 of the misc output register? Then update monochrome vs color emulation mode!
		ClocksUpdated |= ((((VGA->registers->ExternalRegisters.MISCOUTPUTREGISTER)^(VGA->precalcs.LastMiscOutputRegister))&0xC) || FullUpdate); //Are we to update the clock?
		VGA->precalcs.LastMiscOutputRegister = VGA->registers->ExternalRegisters.MISCOUTPUTREGISTER; //Save the last version of us!

		if (VGA->enable_SVGA==3) //EGA?
		{
			if (VGA->registers->ExternalRegisters.MISCOUTPUTREGISTER&0x10) //Disable internal video drivers?
			{
				VGA->precalcs.EGA_DisableInternalVideoDrivers = 1; //Disable it!
			}
			else //Enable internal video drivers?
			{
				VGA->precalcs.EGA_DisableInternalVideoDrivers = 0; //Enable it!
			}
		}

		//Update our dipswitches according to the emulated monitor!
		//Dipswitch source: https://groups.google.com/d/msg/comp.sys.ibm.pc.classic/O-oivadTYck/kLe4xxf7wDIJ
		pattern = 0x6; //Pattern 0110: Enhanced Color - Enhanced Mode, 0110 according to Dosbox's VGA
		//Pattern 0001=CGA, Pattern 0010=MDA
		if (DAC_Use_BWMonitor(0xFF)) //Are we using a non-color monitor?
		{
			pattern = 0x2; //Bit 1=Monochrome?, originally 0010 for Monochrome!
		}
		else //Color monitor?
		{
			if (VGA->enable_SVGA == 3) //EGA?
			{
				pattern = 0x6; //Start up as a EGA 80x25 color!
			}
		}

		//Set the dipswitches themselves!
		VGA->registers->switches = pattern; //Set the pattern to use!
	}

	if ((whereupdated==(WHEREUPDATED_SEQUENCER|0x01)) || FullUpdate || !VGA->precalcs.characterwidth) //Sequencer register updated?
	{
		//CGA forces character width to 8 wide!
		if (VGA->precalcs.characterwidth != (GETBITS(VGA->registers->SequencerRegisters.REGISTERS.CLOCKINGMODEREGISTER,0,1)?8:9)) adjustVGASpeed(); //Auto-adjust our VGA speed!
		VGA->precalcs.characterwidth = GETBITS(VGA->registers->SequencerRegisters.REGISTERS.CLOCKINGMODEREGISTER,0,1)?8:9; //Character width!
		VGA->precalcs.textcharacterwidth = VGA->precalcs.characterwidth; //Text character width(same as normal characterwidth by default)!
		if (VGA->precalcs.ClockingModeRegister_DCR != GETBITS(VGA->registers->SequencerRegisters.REGISTERS.CLOCKINGMODEREGISTER,3,1)) adjustVGASpeed(); //Auto-adjust our VGA speed!
		VGA->precalcs.ClockingModeRegister_DCR = GETBITS(VGA->registers->SequencerRegisters.REGISTERS.CLOCKINGMODEREGISTER,3,1); //Dot Clock Rate!

		byte newSLR = 0x7; //New shift/load rate!
		if (GETBITS(VGA->registers->SequencerRegisters.REGISTERS.CLOCKINGMODEREGISTER,4,1)) //Quarter the video load rate?
		{
			newSLR = 0x7; //Reload every 4 clocks!
		}
		else if (GETBITS(VGA->registers->SequencerRegisters.REGISTERS.CLOCKINGMODEREGISTER,2,1)) //Half the video load rate?
		{
			newSLR = 0x3; //Reload every 2 clocks!
		}
		else //Single load rate?
		{
			newSLR = 0x1; //Always load(Single load rate) every character clock(2 half clocks)!
		}
		VGA->precalcs.VideoLoadRateMask = newSLR; //Apply the determined Shift/Load rate mask!

		updateCRTC = 1; //We need to update the CRTC!
		if (!FullUpdate) whereupdated = WHEREUPDATED_ALL_SECTION|WHEREUPDATED_CRTCONTROLLER; //We affect the CRTController fully too with above!
		charwidthupdated = VGA->precalcs.charwidthupdated = 1; //The character width has been updated, so update the corresponding registers too!
	}

	if (FullUpdate || (whereupdated==(WHEREUPDATED_SEQUENCER|0x4)) || (whereupdated==(WHEREUPDATED_GRAPHICSCONTROLLER|0x5))) //Sequencer Memory Mode Register or Graphics Mode register?
	{
		if (GETBITS(VGA->registers->SequencerRegisters.REGISTERS.SEQUENCERMEMORYMODEREGISTER,3,1)) //Chain 4 mode?
		{
			VGA_WriteMemoryMode = VGA_ReadMemoryMode = VGA->precalcs.WriteMemoryMode = VGA->precalcs.ReadMemoryMode = 1; //Chain-4 mode on both writes and reads!
		}
		else //Other memory modes, which can be mixed?
		{
			//Determine write memory mode!
			if (GETBITS(VGA->registers->SequencerRegisters.REGISTERS.SEQUENCERMEMORYMODEREGISTER,2,1)==0) //Write using odd/even addressing?
			{
				VGA_WriteMemoryMode = VGA->precalcs.WriteMemoryMode = 2; //Odd/Even mode!
			}
			else //Planar mode?
			{
				VGA_WriteMemoryMode = VGA->precalcs.WriteMemoryMode = 0; //Planar mode!
			}

			//Determine read memory mode!
			if (GETBITS(VGA->registers->GraphicsRegisters.REGISTERS.GRAPHICSMODEREGISTER,4,1)) //Read using odd/even addressing?
			{
				VGA_ReadMemoryMode = VGA->precalcs.ReadMemoryMode = 2; //Odd/Even mode!
			}
			else //Planar mode?
			{
				VGA_ReadMemoryMode = VGA->precalcs.ReadMemoryMode = 0; //Planar mode!
			}
		}
		updateVGAMMUAddressMode(VGA); //Update the currently assigned memory mode for mapping memory by address!
	}

	if ((whereupdated==(WHEREUPDATED_SEQUENCER|0x03)) || (whereupdated==(WHEREUPDATED_SEQUENCER|0x04)) || FullUpdate) //Sequencer character map register updated?
	{
		VGA_charsetupdated(VGA); //The character sets have been updated! Apply all changes to the active characters!
	}

	if (whereupdated == (WHEREUPDATED_SEQUENCER | 0x04) || FullUpdate) //Sequencer memory mode register?
	{
		if (!GETBITS(VGA->registers->SequencerRegisters.REGISTERS.SEQUENCERMEMORYMODEREGISTER,1,1)) //Enable limited memory when Extended memory is unused!
		{
			VGA->precalcs.VRAMmask = 0xFFFF; //Wrap memory according to specs!
		}
		else
		{
			VGA->precalcs.VRAMmask = (VGA->VRAM_size-1); //Don't limit VGA memory, wrap normally!
		}
		VGA->precalcs.VMemMask = VGA->precalcs.VRAMmask; //The current VGA memory mask applied the VGA way!
	}
	
	if (FullUpdate || (whereupdated == (WHEREUPDATED_GRAPHICSCONTROLLER | 0x5))) //Graphics mode register?
	{
		if (VGA->precalcs.GraphicsModeRegister_ShiftRegister!=GETBITS(VGA->registers->GraphicsRegisters.REGISTERS.GRAPHICSMODEREGISTER,5,3)) adjustVGASpeed(); //Auto-adjust our VGA speed!
		VGA->precalcs.GraphicsModeRegister_ShiftRegister = GETBITS(VGA->registers->GraphicsRegisters.REGISTERS.GRAPHICSMODEREGISTER,5,3); //Update shift mode!
		updateVGAGraphics_Mode(VGA); //Update the graphics mode!
	}

	if ((whereupdated==(WHEREUPDATED_GRAPHICSCONTROLLER|0x06)) || FullUpdate) //Misc graphics register?
	{
		if (VGA->precalcs.graphicsmode != GETBITS(VGA->registers->GraphicsRegisters.REGISTERS.MISCGRAPHICSREGISTER, 0, 1)) //Graphics mode changed?
		{
			adjustVGASpeed(); //Auto-adjust VGA speed!
		}
		graphicsmodechanges = VGA->precalcs.graphicsmode; //Save the old mode!
		VGA->precalcs.graphicsmode = GETBITS(VGA->registers->GraphicsRegisters.REGISTERS.MISCGRAPHICSREGISTER,0,1)?1:0; //Update Graphics mode!
		VGA->precalcs.graphicsmode_nibbled = VGA->precalcs.graphicsmode?3:0; //Allow nibbled to be used (1 or 2) during graphics modes only!
		VGA->precalcs.textmode = !VGA->precalcs.graphicsmode; //Text mode instead, since we must have faster graphics mode (intensive changes)!
		updateCRTC |= (graphicsmodechanges != VGA->precalcs.graphicsmode); //Changed graphics mode updates the CRTC as well?
		if ((graphicsmodechanges != VGA->precalcs.graphicsmode))
		{
			updateVGASequencer_Mode(VGA); //Update the sequencer mode!
		}
		VGA_updateVRAMmaps(VGA); //Update the active VRAM maps!
		adjustVGASpeed(); //Auto-adjust our VGA speed!
	}

	if (SECTIONISUPDATED(whereupdated,WHEREUPDATED_CGACRTCONTROLLER_HORIZONTAL)) //CGA horizontal timing updated?
	{
		updateCGACRTCONTROLLER = UPDATE_SECTION(whereupdated,WHEREUPDATED_CGACRTCONTROLLER_HORIZONTAL); //Update the entire section?
		updateCRTC = 1; //Update the CRTC!
		if (updateCGACRTCONTROLLER || (whereupdated==(WHEREUPDATED_CGACRTCONTROLLER_HORIZONTAL|0x1))) //Horizontal displayed register?
		{
			word cgarowsize;
			cgarowsize = (word)VGA->registers->CGARegistersMasked[1]; //We're the value of the displayed characters!
			cgarowsize <<= GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.CRTCMODECONTROLREGISTER,6,1); //Convert from byte to word mode when used!
			VGA->precalcs.rowsize = VGA->precalcs.VGArowsize = cgarowsize; //Apply the new row size!
			adjustVGASpeed(); //Auto-adjust our VGA speed!
			goto updateoffsetregister; //Update the offset register, then the rest!
		}
		adjustVGASpeed(); //Auto-adjust our VGA speed!
	}

	if (SECTIONISUPDATED(whereupdated,WHEREUPDATED_CGACRTCONTROLLER_VERTICAL)) //CGA vertical timing updated?
	{
		updateCGACRTCONTROLLER = UPDATE_SECTION(whereupdated,WHEREUPDATED_CGACRTCONTROLLER_VERTICAL); //Update the entire section?
		//Don't handle these registers just yet!
		if (updateCGACRTCONTROLLER || (whereupdated==(WHEREUPDATED_CGACRTCONTROLLER_VERTICAL|0x9))) //Character height updated?
		{
			SETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.MAXIMUMSCANLINEREGISTER,0,0x1F,(VGA->registers->CGARegistersMasked[9])); //Character height is set!
			adjustVGASpeed(); //Auto-adjust our VGA speed!
			updateCRTC = 1; //Update the CRTC!
			goto updatecharheight;
		}
		updateCRTC = 1; //Update the CRTC!
		adjustVGASpeed(); //Auto-adjust our VGA speed!
	}
	
	if (SECTIONISUPDATED(whereupdated,WHEREUPDATED_CGACRTCONTROLLER)) //CGA CRT misc. stuff updated?
	{
		updateCGACRTCONTROLLER = UPDATE_SECTION(whereupdated,WHEREUPDATED_CGACRTCONTROLLER); //Update the entire section?

		if (updateCGACRTCONTROLLER || (whereupdated==(WHEREUPDATED_CGACRTCONTROLLER|0xA))) //Cursor Start Register updated?
		{
			SETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.CURSORSTARTREGISTER,0,0x1F,(VGA->registers->CGARegistersMasked[0xA]&0x1F)); //Cursor scanline start!
			SETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.CURSORSTARTREGISTER,5,1,(((VGA->registers->CGARegistersMasked[0xA])&0x60)!=0x20)?0:1); //Disable the cursor? Setting these bits to any display will enable the cursor!
			checkCGAcursor(VGA); //Check the cursor!
			goto updateCursorStart; //Update us!
		}
		if (updateCGACRTCONTROLLER || (whereupdated==(WHEREUPDATED_CGACRTCONTROLLER|0xB))) //Cursor End Register updated?
		{
			SETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.CURSORENDREGISTER,0,0x1F,(VGA->registers->CGARegistersMasked[0xB]&0x1F)); //Cursor scanline end!
			checkCGAcursor(VGA); //Check the cursor!
			goto updateCursorEnd; //Update us!
		}

		if (updateCGACRTCONTROLLER || ((whereupdated==(WHEREUPDATED_CGACRTCONTROLLER|0xC)) || whereupdated==(WHEREUPDATED_CGACRTCONTROLLER|0xD))) //Start address High/Low register updated?
		{
			word startaddress;
			startaddress = (VGA->registers->CGARegistersMasked[0xC]); //Apply the start address high register!
			startaddress <<= 8; //Move high!
			startaddress |= VGA->registers->CGARegistersMasked[0xD]; //Apply the start address low register!

			//Translate to a VGA value!
			startaddress <<= GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.CRTCMODECONTROLREGISTER,6,1); //Convert from byte to word mode when used!

			//Apply to the VGA!
			VGA->registers->CRTControllerRegisters.REGISTERS.STARTADDRESSHIGHREGISTER = (startaddress>>8)&0xFF;
			VGA->registers->CRTControllerRegisters.REGISTERS.STARTADDRESSLOWREGISTER = (startaddress&0xFF);
			goto updateStartAddress;
		}

		if (updateCGACRTCONTROLLER || (whereupdated==(WHEREUPDATED_CGACRTCONTROLLER|0xE)) || (whereupdated==(WHEREUPDATED_CGACRTCONTROLLER|0xF))) //Cursor address High/Low register updated?
		{
			word cursorlocation;
			cursorlocation = (VGA->registers->CGARegistersMasked[0xE]); //Apply the start address high register!
			cursorlocation <<= 8; //Move high!
			cursorlocation |= VGA->registers->CGARegistersMasked[0xF]; //Apply the start address low register!

			//This seems to be the same on a VGA!
			cursorlocation <<= GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.CRTCMODECONTROLREGISTER,6,1); //Convert from byte to word mode when used!
			//Apply to the VGA!
			VGA->registers->CRTControllerRegisters.REGISTERS.CURSORLOCATIONHIGHREGISTER = (cursorlocation>>8)&0xFF;
			VGA->registers->CRTControllerRegisters.REGISTERS.CURSORLOCATIONLOWREGISTER = (cursorlocation&0xFF);
			goto updateCursorLocation;
		}

		adjustVGASpeed(); //Auto-adjust our VGA speed!
	}

	if (SECTIONISUPDATED(whereupdated,WHEREUPDATED_CRTCONTROLLER) || FullUpdate || charwidthupdated) //(some) CRT Controller values need to be updated?
	{
		CRTUpdated = UPDATE_SECTIONFULL(whereupdated,WHEREUPDATED_CRTCONTROLLER,FullUpdate); //Fully updated?
		if (CRTUpdated || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0x9))) //We have been updated?
		{
			updatecharheight:
			if (VGA->precalcs.characterheight != GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.MAXIMUMSCANLINEREGISTER,0,0x1F)+1)
			{
				adjustVGASpeed(); //Auto-adjust our VGA speed!
				updateCRTC = 1; //Update the CRTC!
			}
			VGA->precalcs.characterheight = GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.MAXIMUMSCANLINEREGISTER,0,0x1F)+1; //Character height!
		}

		CRTUpdatedCharwidth = CRTUpdated||charwidthupdated; //Character width has been updated, for following registers using those?
		overflowupdated = FullUpdate||(whereupdated==(WHEREUPDATED_CRTCONTROLLER|0x7)); //Overflow register has been updated?
		
		if (CRTUpdated || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x8))) //Preset row scan?
		{
			VGA->precalcs.PresetRowScanRegister_BytePanning = GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.PRESETROWSCANREGISTER,5,3); //Update byte panning!
		}

		if (CRTUpdated || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0xA))) //Cursor start register?
		{
			updateCursorStart:
			VGA->precalcs.CursorStartRegister_CursorScanLineStart = GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.CURSORSTARTREGISTER,0,0x1F); //Update!
			if (VGA->precalcs.CursorStartRegister_CursorDisable != GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.CURSORSTARTREGISTER,5,1)) adjustVGASpeed(); //Changed speed!
			VGA->precalcs.CursorStartRegister_CursorDisable = GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.CURSORSTARTREGISTER,5,1); //Update!
		}

		if (CRTUpdated || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0xB))) //Cursor end register?
		{
			updateCursorEnd:
			VGA->precalcs.CursorEndRegister_CursorScanLineEnd = GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.CURSORENDREGISTER,0,0x1F); //Update!
		}

		//CRT Controller registers:
		byte hendstartupdated = 0;
		if (CRTUpdatedCharwidth || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0x3))) //Updated?
		{
			word hstart;
			hstart = GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.ENDHORIZONTALBLANKINGREGISTER,5,3);
			hstart *= VGA->precalcs.characterwidth; //We're a character width!
			hendstartupdated = (VGA->precalcs.horizontaldisplaystart != hstart); //Update!
			if (VGA->precalcs.horizontaldisplaystart != hstart) adjustVGASpeed(); //Auto-adjust our speed!
			VGA->precalcs.horizontaldisplaystart = hstart; //Load!
			recalcScanline |= hendstartupdated; //Update!
			updateCRTC |= hendstartupdated; //Update!
		}
		
		if (CRTUpdatedCharwidth || (whereupdated==WHEREUPDATED_CRTCONTROLLER)) //Updated?
		{
			word htotal;
			htotal = VGA->registers->CRTControllerRegisters.REGISTERS.HORIZONTALTOTALREGISTER;
			htotal += (VGA->enable_SVGA != 3)?5:2; //VGA=+5, EGA=+2
			htotal *= VGA->precalcs.characterwidth; //We're character units!
			if (htotal!=VGA->precalcs.horizontaltotal) adjustVGASpeed(); //Update our speed!
			updateCRTC |= (VGA->precalcs.horizontaltotal != htotal); //Update!
			VGA->precalcs.horizontaltotal = htotal; //Load!
		}
		
		if (CRTUpdatedCharwidth || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0x1))) //Updated?
		{
			word hdispend;
			hdispend = VGA->registers->CRTControllerRegisters.REGISTERS.ENDHORIZONTALDISPLAYREGISTER;
			++hdispend; //Stop after this character!
			hdispend *= VGA->precalcs.characterwidth; //Original!
			if (VGA->precalcs.horizontaldisplayend != hdispend) adjustVGASpeed(); //Update our speed!
			hendstartupdated |= (VGA->precalcs.horizontaldisplayend != hdispend); //Update!
			updateCRTC |= (VGA->precalcs.horizontaldisplayend != hdispend); //Update!
			VGA->precalcs.horizontaldisplayend = hdispend; //Load!
		}
		
		if (CRTUpdatedCharwidth || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0x2))) //Updated?
		{
			word hblankstart;
			hblankstart = VGA->registers->CRTControllerRegisters.REGISTERS.STARTHORIZONTALBLANKINGREGISTER;
			++hblankstart; //Start after this character!
			VGA->precalcs.horizontalblankingstartfinish = hblankstart; //For calculating it's finish!
			hblankstart *= VGA->precalcs.characterwidth;
			if (VGA->precalcs.horizontalblankingstart != hblankstart) adjustVGASpeed(); //Update our speed!
			updateCRTC |= (VGA->precalcs.horizontalblankingstart != hblankstart); //Update!
			VGA->precalcs.horizontalblankingstart = hblankstart; //Load!
			hblankstart = VGA->precalcs.horizontalblankingstartfinish;
			++hblankstart; //End after this character!
			hblankstart *= VGA->precalcs.characterwidth;
			updateCRTC |= (VGA->precalcs.horizontalblankingstartfinish != hblankstart); //Update!
			VGA->precalcs.horizontalblankingstartfinish = hblankstart; //Load!
		}

		if (CRTUpdatedCharwidth || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0x3)) || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0x5))) //Updated?
		{
			word hblankend;
			if (VGA->enable_SVGA != 3) //Not EGA?
			{
				hblankend = GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.ENDHORIZONTALRETRACEREGISTER, 7, 1);
				hblankend <<= 5; //Move to bit 6!
			}
			else //EGA?
			{
				hblankend = 0; //Init!
			}
			hblankend |= GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.ENDHORIZONTALBLANKINGREGISTER,0,0x1F);
			if (VGA->precalcs.horizontalblankingend != hblankend) adjustVGASpeed(); //Update our speed!
			updateCRTC |= (VGA->precalcs.horizontalblankingend != hblankend); //Update!
			VGA->precalcs.horizontalblankingend = hblankend; //Load!
		}
		
		if (CRTUpdatedCharwidth || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0x4)))
		{
			word hretracestart;
			hretracestart = VGA->registers->CRTControllerRegisters.REGISTERS.STARTHORIZONTALRETRACEREGISTER;
			hretracestart += GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.ENDHORIZONTALRETRACEREGISTER, 5, 0x3); //Add skew!
			++hretracestart; //We start after this!
			VGA->precalcs.horizontalretracestartfinish = hretracestart; //Finish on the next clock?
			hretracestart *= VGA->precalcs.characterwidth; //We're character units!
			if (VGA->precalcs.horizontalretracestart != hretracestart) adjustVGASpeed(); //Update our speed!
			updateCRTC |= (VGA->precalcs.horizontalretracestart != hretracestart); //Update!
			VGA->precalcs.horizontalretracestart = hretracestart; //Load!
			hretracestart = VGA->precalcs.horizontalretracestartfinish; //When to finish?
			++hretracestart; //The next character clock!
			hretracestart *= VGA->precalcs.characterwidth; //We're character units!
			updateCRTC |= VGA->precalcs.horizontalretracestartfinish != hretracestart; //To be updated?
			VGA->precalcs.horizontalretracestartfinish = hretracestart; //Load!
		}
		
		if (CRTUpdatedCharwidth || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0x5))) 
		{
			if (VGA->precalcs.horizontalretraceend != GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.ENDHORIZONTALRETRACEREGISTER,0,0x1F)) adjustVGASpeed();
			updateCRTC |= (VGA->precalcs.horizontalretraceend != GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.ENDHORIZONTALRETRACEREGISTER,0,0x1F)); //Update!
			VGA->precalcs.horizontalretraceend = GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.ENDHORIZONTALRETRACEREGISTER,0,0x1F); //Load!
		}
		
		if (CRTUpdated || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0x12)) || overflowupdated) //Updated?
		{
			word vdispend;
			vdispend = GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.OVERFLOWREGISTER,6,1);
			vdispend <<= 1;
			vdispend |= GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.OVERFLOWREGISTER,1,1);
			vdispend <<= 8;
			vdispend |= VGA->registers->CRTControllerRegisters.REGISTERS.VERTICALDISPLAYENDREGISTER;
			++vdispend; //Stop one scanline later: we're the final scanline!
			if (VGA->precalcs.verticaldisplayend != vdispend) adjustVGASpeed(); //Update our speed?
			updateCRTC |= (VGA->precalcs.verticaldisplayend != vdispend); //Update!
			VGA->precalcs.verticaldisplayend = vdispend;
		}
		
		if (CRTUpdated || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0x15)) || overflowupdated || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0x9))) //Updated?
		{
			word vblankstart;
			vblankstart = GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.MAXIMUMSCANLINEREGISTER,5,1);
			vblankstart <<= 1;
			vblankstart |= GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.OVERFLOWREGISTER,3,1);
			vblankstart <<= 8;
			vblankstart |= VGA->registers->CRTControllerRegisters.REGISTERS.STARTVERTICALBLANKINGREGISTER;
			if (VGA->precalcs.verticalblankingstart != vblankstart) adjustVGASpeed(); //Update our speed?
			updateCRTC |= (VGA->precalcs.verticalblankingstart != vblankstart); //Update!
			VGA->precalcs.verticalblankingstart = vblankstart;
		}
		
		if (CRTUpdated || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0x16)))
		{
			if (VGA->precalcs.verticalblankingend != GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.ENDVERTICALBLANKINGREGISTER,0,0x7F)) adjustVGASpeed(); //Update our speed?
			updateCRTC |= (VGA->precalcs.verticalblankingend != GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.ENDVERTICALBLANKINGREGISTER,0,0x7F)); //Update!
			VGA->precalcs.verticalblankingend = GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.ENDVERTICALBLANKINGREGISTER,0,0x7F);
		}

		if (CRTUpdated || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0x10)) || overflowupdated) //Updated?
		{
			word vretracestart;
			vretracestart = GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.OVERFLOWREGISTER,7,1);
			vretracestart <<= 1;
			vretracestart |= GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.OVERFLOWREGISTER,2,1);
			vretracestart <<= 8;
			vretracestart |= VGA->registers->CRTControllerRegisters.REGISTERS.VERTICALRETRACESTARTREGISTER;
			if (VGA->precalcs.verticalretracestart != vretracestart) adjustVGASpeed(); //Update our speed?
			updateCRTC |= (VGA->precalcs.verticalretracestart != vretracestart); //Update!
			VGA->precalcs.verticalretracestart = vretracestart;
		}
		
		if (CRTUpdated || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0x6)) || overflowupdated) //Updated?
		{
			word vtotal;
			if (VGA->enable_SVGA != 3) //EGA doesn't support this?
			{
				vtotal = GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.OVERFLOWREGISTER, 5, 1);
				vtotal <<= 1;
			}
			else //EGA?
			{
				vtotal = 0; //Init!
			}
			vtotal |= GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.OVERFLOWREGISTER,0,1);
			vtotal <<= 8;
			vtotal |= VGA->registers->CRTControllerRegisters.REGISTERS.VERTICALTOTALREGISTER;
			++vtotal; //We end after the line specified, so specify the line to end at!
			if (VGA->precalcs.verticaltotal != vtotal) adjustVGASpeed(); //Update our speed?
			updateCRTC |= (VGA->precalcs.verticaltotal != vtotal); //Update!
			VGA->precalcs.verticaltotal = vtotal;
		}
		
		if (CRTUpdated || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0x11))) //Updated?
		{
			if (VGA->precalcs.verticalretraceend != GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.VERTICALRETRACEENDREGISTER,0,0xF)) adjustVGASpeed(); //Update our speed?
			updateCRTC |= (VGA->precalcs.verticalretraceend != GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.VERTICALRETRACEENDREGISTER,0,0xF)); //Update!
			VGA->precalcs.verticalretraceend = GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.VERTICALRETRACEENDREGISTER,0,0xF); //Load!
		}
		
		if (CRTUpdated || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0x13))) //Updated?
		{
			word rowsize;
			if (!CGAMDAEMULATION_ENABLED_CRTC(VGA)) //We're not using the CGA/MDA CRTC? Prevent us from updating the VGA data into the CGA emulation!
			{
				rowsize = VGA->registers->CRTControllerRegisters.REGISTERS.OFFSETREGISTER;
				rowsize <<= 1;
				VGA->precalcs.rowsize = VGA->precalcs.VGArowsize = rowsize; //=Offset*2
			}
			updateoffsetregister:
			recalcScanline = 1; //Recalculate the scanline data!
		}
		
		if (CRTUpdated || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0x18))
			       || overflowupdated
			       || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0x9))) //Updated?
		{
			word topwindowstart;
			topwindowstart = GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.MAXIMUMSCANLINEREGISTER,6,1);
			topwindowstart <<= 1;
			topwindowstart |= GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.OVERFLOWREGISTER,4,1);
			topwindowstart <<= 8;
			topwindowstart |= VGA->registers->CRTControllerRegisters.REGISTERS.LINECOMPAREREGISTER;
			++topwindowstart; //We're one further starting than specified!
			VGA->precalcs.topwindowstart = topwindowstart;
			recalcScanline = 1; //Recalc scanline data!
		}

		if (CRTUpdated || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x17))) //Mode control updated?
		{
			VGA->precalcs.CRTCModeControlRegister_SLDIV = GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.CRTCMODECONTROLREGISTER,2,1); //Update!
		}

		if (CRTUpdated || charwidthupdated || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0x14))
			       || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0x17))) //Updated?
		{
			//This applies to the Frame buffer:
			byte BWDModeShift = 1; //Default: word mode!
			if (GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.UNDERLINELOCATIONREGISTER,6,1))
			{
				BWDModeShift = 2; //Shift by 2!
			}
			else if (GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.CRTCMODECONTROLREGISTER,6,1))
			{
				BWDModeShift = 0; //Shift by 0! We're byte mode!
			}

			byte characterclockshift = 1; //Default: reload every whole clock!
			//This applies to the address counter (renderer), causing it to increase and load more/less(factors of 2). This is used as a mask to apply to the 
			if (GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.UNDERLINELOCATIONREGISTER,5,1))
			{
				if (GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.CRTCMODECONTROLREGISTER,3,1)) //Both set? We reload twice per clock!
				{
					characterclockshift = 0; //Reload every half clock(4 pixels)!
				}
				else //Reload every 4 clocks!
				{
					characterclockshift = 7; //Reload every 4 clocks(32 pixels)!
				}
			}
			else if (GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.CRTCMODECONTROLREGISTER,3,1))
			{
				characterclockshift = 3; //Reload every other clock(16 pixels)!
			}
			else //Reload every clock!
			{
				characterclockshift = 1; //Reload every whole clock(8 pixels)!
			}

			updateCRTC |= (VGA->precalcs.BWDModeShift != BWDModeShift); //Update the CRTC!
			VGA->precalcs.BWDModeShift = BWDModeShift;

			updateCRTC |= (VGA->precalcs.characterclockshift != characterclockshift); //Update the CRTC!
			VGA->precalcs.characterclockshift = characterclockshift; //Apply character clock shift!

			underlinelocationupdated = 1; //We need to update the attribute controller!
		}
		
		if (CRTUpdated || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0x9))) //Updated?
		{
			VGA->precalcs.scandoubling = GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.MAXIMUMSCANLINEREGISTER,7,1); //Scan doubling enabled? CGA disables scanline doubling for compatibility.
		}
		
		//Sequencer_textmode_cursor (CRTC):
		if (CRTUpdated || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0xE))
			       || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0xF))
			       || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0xB))
				   
				   || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x14))
				   || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x17)) //Also update on B/W/DW mode changes!
				   ) //Updated?
		{
			word cursorlocation;
			updateCursorLocation:
			cursorlocation = VGA->registers->CRTControllerRegisters.REGISTERS.CURSORLOCATIONHIGHREGISTER;
			cursorlocation <<= 8;
			cursorlocation |= VGA->registers->CRTControllerRegisters.REGISTERS.CURSORLOCATIONLOWREGISTER;
			cursorlocation += GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.CURSORENDREGISTER,5,3);
			cursorlocation <<= VGA->precalcs.BWDModeShift; //Apply byte/word/doubleword mode at the character level!

			VGA->precalcs.cursorlocation = cursorlocation; //Cursor location!
		}

		if (CRTUpdated || (whereupdated == (WHEREUPDATED_CRTCONTROLLER | 0x8))) //Preset row scan updated?
		{
			VGA->precalcs.presetrowscan = GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.PRESETROWSCANREGISTER,0,0x1F); //Apply new preset row scan!
		}
		
		if (CRTUpdated || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0xC))
						|| (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0xD))) //Updated?
		{
			word startaddress;
			updateStartAddress:
			startaddress = VGA->registers->CRTControllerRegisters.REGISTERS.STARTADDRESSHIGHREGISTER;
			startaddress <<= 8;
			startaddress |= VGA->registers->CRTControllerRegisters.REGISTERS.STARTADDRESSLOWREGISTER;
			VGA->precalcs.VGAstartaddress = VGA->precalcs.startaddress = startaddress; //Updated start address for the VGA!
			recalcScanline = 1; //Recalc scanline data!
		}
		if (CRTUpdated || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|0x14))) //Underline location updated?
		{
			recalcAttr = 1; //Recalc attribute pixels!
		}

		if (CRTUpdated || (whereupdated==(WHEREUPDATED_CRTCONTROLLER|VGA_CRTC_ATTRIBUTECONTROLLERTOGGLEREGISTER))) //Attribute controller toggle register updated?
		{
			recalcAttr = 1; //We've been updated: update the color logic!
		}
	}

	byte AttrUpdated = 0; //Fully updated?
	if (SECTIONISUPDATED(whereupdated,WHEREUPDATED_ATTRIBUTECONTROLLER) || FullUpdate || underlinelocationupdated || (whereupdated==(WHEREUPDATED_INDEX|INDEX_ATTRIBUTECONTROLLER)) || (whereupdated == (WHEREUPDATED_GRAPHICSCONTROLLER | 0x06)) || charwidthupdated) //Attribute Controller updated?
	{
		AttrUpdated = UPDATE_SECTIONFULL(whereupdated,WHEREUPDATED_ATTRIBUTECONTROLLER,FullUpdate); //Fully updated?

		if (AttrUpdated || (whereupdated==(WHEREUPDATED_ATTRIBUTECONTROLLER|0x14)))
		{
			byte csel,csel2;
			
			csel = GETBITS(VGA->registers->AttributeControllerRegisters.REGISTERS.COLORSELECTREGISTER,0,3);
			csel <<= 4;
			
			csel2 = GETBITS(VGA->registers->AttributeControllerRegisters.REGISTERS.COLORSELECTREGISTER,2,3);
			csel2 <<= 6;

			VGA->precalcs.colorselect54 = csel; //Precalculate!
			VGA->precalcs.colorselect76 = csel2; //Precalculate!

			recalcAttr = 1; //We've been updated: update the color logic!
		}

		if (AttrUpdated || (whereupdated==(WHEREUPDATED_ATTRIBUTECONTROLLER|0x11))) //Overscan?
		{
			VGA->precalcs.overscancolor = VGA->registers->AttributeControllerRegisters.REGISTERS.OVERSCANCOLORREGISTER; //Update the overscan color!
		}

		if (AttrUpdated || (whereupdated == (WHEREUPDATED_ATTRIBUTECONTROLLER | 0x10))) //Mode control updated?
		{
			VGA->precalcs.AttributeModeControlRegister_ColorEnable8Bit = GETBITS(VGA->registers->AttributeControllerRegisters.REGISTERS.ATTRIBUTEMODECONTROLREGISTER,6,1);
			VGA->precalcs.AttributeModeControlRegister_PixelPanningMode = GETBITS(VGA->registers->AttributeControllerRegisters.REGISTERS.ATTRIBUTEMODECONTROLREGISTER,5,1);
			updateVGAAttributeController_Mode(VGA); //Update the attribute mode!
		}

		if (AttrUpdated || (whereupdated==(WHEREUPDATED_ATTRIBUTECONTROLLER|0x13))
			|| (whereupdated==(WHEREUPDATED_GRAPHICSCONTROLLER|0x06))
			|| charwidthupdated) //Updated?
		{
			//Precalculate horizontal pixel panning:
			byte pixelboost; //Actual pixel boost!
			pixelboost = 0; //Default: no boost!
			byte possibleboost; //Possible value!
			possibleboost = GETBITS(VGA->registers->AttributeControllerRegisters.REGISTERS.HORIZONTALPIXELPANNINGREGISTER,0,0xF); //Possible value, to be determined!
			if ((GETBITS(VGA->registers->SequencerRegisters.REGISTERS.CLOCKINGMODEREGISTER, 0, 1) == 0) && (VGA->precalcs.graphicsmode==0)) //Different behaviour with 9 pixel text modes?
			{
				if (possibleboost >= 8) //No shift from 8 and up?
				{
					possibleboost = 0; //No shift!
				}
				else //Less than 8?
				{
					++possibleboost; //1 more!
				}
			}
			else //Only 3 bits?
			{
				possibleboost &= 0x7; //Repeat the low values!
			}
			pixelboost = possibleboost; //Enable normally!
			recalcScanline |= (VGA->precalcs.pixelshiftcount!=pixelboost); //Recalc scanline data when needed!
			VGA->precalcs.pixelshiftcount = pixelboost; //Save our precalculated value!
		}
		
		//Simple attribute controller updates?

		if (AttrUpdated || (whereupdated==(WHEREUPDATED_ATTRIBUTECONTROLLER|0x10))) //Mode control register updated?
		{
			recalcAttr = 1; //We've been updated: update the color logic and pixels!
		}
		else if (whereupdated==(WHEREUPDATED_ATTRIBUTECONTROLLER|0x12)) //Color planes enable register?
		{
			recalcAttr = 1; //We've been updated: update the color logic!
		}
		else if (SECTIONISUPDATED(whereupdated,WHEREUPDATED_ATTRIBUTECONTROLLER) && ((whereupdated&WHEREUPDATED_REGISTER)<0x10)) //Pallette updated?
		{
			recalcAttr = 1; //We've been updated: update the color logic!
		}
	}

	if ((SECTIONISUPDATED(whereupdated,WHEREUPDATED_DAC) || (whereupdated==WHEREUPDATED_DACMASKREGISTER) || FullUpdate)) //DAC Updated(not on EGA emulation)?
	{
		if (UPDATE_SECTIONFULL(whereupdated,WHEREUPDATED_DAC,FullUpdate) || (whereupdated==WHEREUPDATED_DACMASKREGISTER)) //DAC Fully needs to be updated?
		{
			if (SECTIONISUPDATEDFULL(whereupdated,WHEREUPDATED_DAC,FullUpdate) || ((whereupdated==WHEREUPDATED_DACMASKREGISTER) && VGA->precalcs.lastDACMask!=VGA->registers->DACMaskRegister)) //DAC Mask changed only?
			{
				int colorval;
				colorval = 0; //Init!
				for (;;) //Precalculate colors for DAC!
				{
					if (VGA->enable_SVGA!=3) //EGA can't change the DAC!
					{
						VGA->precalcs.DAC[colorval] = getcol256(VGA,colorval); //Translate directly through DAC for output!
					}
					DAC_updateEntry(VGA,colorval); //Update a DAC entry for rendering!
					if (++colorval&0xFF00) break; //Overflow?
				}
				VGA->precalcs.lastDACMask = VGA->registers->DACMaskRegister; //Save the DAC mask for future checking if it's changed!
			}
		}
		else //Single register updated, no mask register updated?
		{
			if (VGA->enable_SVGA!=3) //EGA can't change the DAC!
			{
				VGA->precalcs.DAC[whereupdated&0xFF] = getcol256(VGA,whereupdated&0xFF); //Translate directly through DAC for output, single color only!
			}
			DAC_updateEntry(VGA,whereupdated&0xFF); //Update a DAC entry for rendering!
		}
	}

	if (ClocksUpdated) //Ammount of vertical clocks have been updated?
	{
		if (VGA==getActiveVGA()) //Active VGA?
		{
			changeRowTimer(VGA); //Make sure the display scanline refresh rate is OK!
		}
	}

	VGA->precalcs.recalcScanline = recalcScanline; //Extension support!

	if (VGA_precalcsextensionhandler) //Extension registered?
	{
		VGA_precalcsextensionhandler(useVGA,whereupdated); //Execute the precalcs extension!
	}

	//Recalculate all our lookup tables when needed!
	if (VGA->precalcs.recalcScanline) //Update scanline information?
	{
		VGA_Sequencer_updateScanlineData(VGA); //Recalculate all scanline data!
	}
	
	if (updateCRTC) //Update CRTC?
	{
		VGA_calcprecalcs_CRTC(VGA); //Update the CRTC timing data!
		adjustVGASpeed(); //Auto-adjust our VGA speed!
	}
	
	if (recalcAttr) //Update attribute controller?
	{
		VGA_AttributeController_calcAttributes(VGA); //Recalc pixel logic!	
		adjustVGASpeed(); //Auto-adjust our VGA speed!
	}
}
