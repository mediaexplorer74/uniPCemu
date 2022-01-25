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

/*

VGA ROM and handling functions.

*/

#define _IS_VGA

//Rendering priority: same as input!
#define VGARENDER_PRIORITY 0x20

#include "headers/types.h" //Basic type support!
#include "headers/hardware/ports.h" //Basic PORT compatibility!
#include "headers/bios/bios.h" //For VRAM memory size!
#include "headers/hardware/vga/vga.h" //VGA data!
#include "headers/hardware/vga/vga_renderer.h" //For precalcs!
#include "headers/hardware/vga/vga_crtcontroller.h" //For getyres for display rate!
#include "headers/hardware/vga/vga_vram.h" //VRAM read!
#include "headers/hardware/vga/vga_vramtext.h" //VRAM text read!
#include "headers/hardware/vga/vga_precalcs.h" //Precalculations!
#include "headers/hardware/vga/vga_sequencer_graphicsmode.h" //Graphics mode!
#include "headers/support/zalloc.h" //Memory allocation!
#include "headers/support/log.h" //Logging support!
#include "headers/emu/gpu/gpu_renderer.h" //GPU rendering support!
#include "headers/support/locks.h" //Lock support!
#include "headers/hardware/vga/vga_dacrenderer.h" //DAC support for initialisation!
#include "headers/hardware/pci.h" //PCI support!
#include "headers/fopen64.h" //64-bit fopen support!

//Are we disabled?
#define __HW_DISABLED 0
#define __RENDERER_DISABLED 0

extern GPU_type GPU; //GPU!
VGA_Type *ActiveVGA; //Currently active VGA chipset!

extern BIOS_Settings_TYPE BIOS_Settings; //The BIOS Settings!

//Enable disabling VGA?
#define DISABLE_VGA 0

byte is_loadchartable = 0; //Loading character table?

extern PIXELBUFFERCONTAINERTYPE pixelbuffercontainer; //All 8 pixels decoded from the planesbuffer!

extern byte allcleared;

/*

Info about basic modes:

Other source: http://webpages.charter.net/danrollins/techhelp/0114.HTM


Our base:

http://webpages.charter.net/danrollins/techhelp/0114.HTM

 AL  Type     Format   Cell  Colors        Adapter  Addr  Monitor
                                                                           
      0  text     40x25     8x8* 16/8 (shades) CGA,EGA  b800  Composite
      1  text     40x25     8x8* 16/8          CGA,EGA  b800  Comp,RGB,Enh
      2  text     80x25     8x8* 16/8 (shades) CGA,EGA  b800  Composite
      3  text     80x25     8x8* 16/8          CGA,EGA  b800  Comp,RGB,Enh
      4  graphic  320x200   8x8  4             CGA,EGA  b800  Comp,RGB,Enh
      5  graphic  320x200   8x8  4 (shades)    CGA,EGA  b800  Composite
      6  graphic  640x200   8x8  2             CGA,EGA  b800  Comp,RGB,Enh
      7  text     80x25    9x14* 3 (b/w/bold)  MDA,EGA  b000  TTL Mono
 8,9,0aH  PCjr modes
 0bH,0cH  (reserved; internal to EGA BIOS)
     0dH graphic  320x200   8x8  16            EGA,VGA  a000  Enh,Anlg
     0eH graphic  640x200   8x8  16            EGA,VGA  a000  Enh,Anlg
     0fH graphic  640x350  8x14  3 (b/w/bold)  EGA,VGA  a000  Enh,Anlg,Mono
     10H graphic  640x350  8x14  4 or 16       EGA,VGA  a000  Enh,Anlg
     11H graphic  640x480  8x16  2             VGA      a000  Anlg
     12H graphic  640x480  8x16  16            VGA      a000  Anlg
     13H graphic  640x480  8x16  256           VGA      a000  Anlg

    Notes: With EGA, VGA, and PCjr you can add 80H to AL to initialize a
          video mode without clearing the screen.

        * The character cell size for modes 0-3 and 7 varies, depending on
          the hardware.  On modes 0-3: CGA=8x8, EGA=8x14, and VGA=9x16.
          For mode 7, MDPA and EGA=9x14, VGA=9x16, LCD=8x8.


*/

VGA_Type *VGAalloc(uint_32 custom_vram_size, int update_bios, byte extension, byte enableColorPedestal) //Initialises VGA and gives the current set!
{
	if (__HW_DISABLED) return NULL; //Abort!

	debugrow("VGA: Initializing clocks if needed...");

	initVGAclocks(extension); //Initialize all clocks!

	VGA_Type *VGA; //The VGA to be allocated!
	debugrow("VGA: Allocating VGA...");
	VGA = (VGA_Type *)zalloc(sizeof(*VGA),"VGA_Struct",getLock(LOCK_CPU)); //Allocate new VGA base to work with!
	if (!VGA)
	{
		raiseError("VGAalloc","Ran out of memory allocating VGA base!");
		return NULL;
	}

	uint_64 size;
	if (update_bios) //From BIOS init?
	{
		size = BIOS_Settings.VRAM_size; //Get VRAM size from BIOS!
	}
	else if (custom_vram_size) //Custom VRAM size?
	{
		size = custom_vram_size; //VRAM size from user!
	}
	else //No VRAM size?
	{
		size = 0; //Default VRAM size!
	}
	
	if (size==0) //Default?
	{
		size = (uint_32)VRAM_SIZE; //Default!
	}
	VGA->VRAM_size = (uint_32)size; //Use the selected size!

	//VRAM size must be a power of two!
	for (size = 1; size <= (uint_64)VGA->VRAM_size; size <<= 1) //Find largest bit!
	{
	}

	if (size > (uint_64)VGA->VRAM_size) size >>= 1; //Take next bit if overflown!
	update_bios |= (VGA->VRAM_size != (uint_32)size); //Size changed? Force BIOS update if so!
	VGA->VRAM_size = (uint_32)size; //Truncate size to largest bit set(power of 2)!
	
	debugrow("VGA: Allocating VGA VRAM...");
	VGA->VRAM = (byte *)zalloc(VGA->VRAM_size,"VGA_VRAM",getLock(LOCK_CPU)); //The VRAM allocated to 0!
	if (!VGA->VRAM)
	{
		VGA->VRAM_size = VRAM_SIZE; //Try Default VRAM size!
		update_bios |= (VGA->VRAM_size != size); //Size changed? Force BIOS update if so!
		VGA->VRAM = (byte *)zalloc(VGA->VRAM_size,"VGA_VRAM",getLock(LOCK_CPU)); //The VRAM allocated to 0!
		if (!VGA->VRAM) //Still not OK?
		{
			freez((void **)&VGA,sizeof(*VGA),"VGA@VGAAlloc_VRAM"); //Release the VGA!
			raiseError("VGAalloc","Ran out of memory allocating VGA VRAM!");
			return NULL;
		}
	}

	if (update_bios) //Auto (for BIOS)
	{
		BIOS_Settings.VRAM_size = VGA->VRAM_size; //Update VRAM size in BIOS!
	}

	debugrow("VGA: Allocating VGA registers...");
	VGA->registers = (VGA_REGISTERS *)zalloc(sizeof(*VGA->registers),"VGA_Registers",getLock(LOCK_CPU)); //Allocate registers!
	if (!VGA->registers) //Couldn't allocate the registers?
	{
		freez((void **)&VGA->VRAM, VGA->VRAM_size,"VGA_VRAM@VGAAlloc_Registers"); //Release VRAM!
		freez((void **)&VGA,sizeof(*VGA),"VGA@VGAAlloc_Registers"); //Release VGA itself!
		raiseError("VGAalloc","Ran out of memory allocating VGA registers!");
	}

	debugrow("VGA: Initialising settings...");

	//Stuff from dosbox for comp.
	int i; //Counter!
	for (i=0; i<256; i++) //Init ExpandTable!
	{
		VGA->ExpandTable[i]=i | (i << 8)| (i <<16) | (i << 24); //For Graphics Unit, full 32-bits value of index!
	}

	for (i=0;i<16;i++)
	{
		VGA->FillTable[i] = (((i&1)?0x000000ff:0)
					|((i&2)?0x0000ff00:0)
					|((i&4)?0x00ff0000:0)
					|((i&8)?0xff000000:0)); //Fill the filltable for Graphics Unit!
	}
	
	VGA->Request_Termination = 0; //We're not running a request for termination!
	VGA->Terminated = 1; //We're not running yet, so run nothing yet, if enabled!
	
	VGA->Sequencer = (SEQ_DATA *)zalloc(sizeof(SEQ_DATA),"SEQ_DATA",getLock(LOCK_CPU)); //Sequencer data!
	if (!VGA->Sequencer) //Failed to allocate?
	{
		freez((void **)&VGA->VRAM, VGA->VRAM_size,"VGA_VRAM@VGAAlloc_Registers"); //Release VRAM!
		freez((void **)&VGA,sizeof(*VGA),"VGA@VGAAlloc_Registers"); //Release VGA itself!
		raiseError("VGAalloc","Ran out of memory allocating VGA precalcs!");
		return NULL; //Failed to allocate!
	}

	SETBITS(VGA->registers->ExternalRegisters.INPUTSTATUS1REGISTER,0,1,1); //Display disabled by default!
	SETBITS(VGA->registers->ExternalRegisters.INPUTSTATUS1REGISTER,3,1,1); //Vertical Retrace by default!
	
	debugrow("VGA: Initialising CGA compatibility font support...");
	fillCGAfont(); //Initialise the CGA font map if needed to use it!
	fillMDAfont(); //Initialise the MDA font map if needed to use it!

	((SEQ_DATA *)VGA->Sequencer)->graphicsx = &pixelbuffercontainer.pixelbuffer[0]; //Reset the graphics pointer!

	VGA->enable_SVGA = extension; //Enable the extension when set!

	switch (extension)
	{
		case 1: //ET4000 compatible?
		case 2: //ET3000?
			//SVGA chips?
			VGA->registers->SequencerRegisters.REGISTERS.SEQUENCERMEMORYMODEREGISTER |= 0x02; //Set by default: extended memory!
		case 3: //EGA or SVGA? We're starting in color mode!
			if (VGA->VRAM_size>=0x10004) //More than 64K installed?
			{
				VGA->registers->SequencerRegisters.REGISTERS.SEQUENCERMEMORYMODEREGISTER |= 0x02; //Set by default: extended memory!
			}
			VGA->registers->ExternalRegisters.MISCOUTPUTREGISTER |= 0x22; //Default misc output set bits!
		case 0: //VGA?
			VGA->registers->ExternalRegisters.MISCOUTPUTREGISTER |= 0x02; //Default misc output set bits: VRAM enabled!
			VGA->registers->ExternalRegisters.MISCOUTPUTREGISTER |= 1; //We're starting in color mode always!
			if (extension != 1) //EGA-compatible?
			{
				VGA->registers->VGA_enabled = 2; //Always enabled: 3C3 mode assumed!
			}
			else
			{
				VGA->registers->VGA_enabled = 1; //Always disabled: 46E8 mode assumed!
			}
			break;
		default: //Non VGA?
			//Leave it to the hardware defaults!
			VGA->registers->VGA_enabled = 3; //Always enabled!
			break;
	}

	VGA->registers->ColorRegisters.DAC_STATE_REGISTER = 1; //This starts out as being 01h, which is an unset DAC setting!

	debugrow("VGA: Executing initial precalculations...");
	VGA_calcprecalcs(VGA,WHEREUPDATED_ALL); //Init all values to be working with!

	if (extension==3) //EGA?
	{
		VGA_setupEGAPalette(VGA); //Setup the EGA palette!
	}

	//Finally, make sure that the VGA DAC output levels are in the correct range!
	VGA_initColorLevels(VGA, enableColorPedestal); //Initialize the color levels to use for output!

	debugrow("VGA: Allocation ready.");
	return VGA; //Give the new allocated VGA!
}

PCI_GENERALCONFIG PCI_VGA; //Our PCI configuration space!

void dumpVRAM() //Diagnostic dump of VRAM!
{
	if (__HW_DISABLED) return; //Abort!
	if (getActiveVGA()) //Got active VGA?
	{
		BIGFILE *f = emufopen64("VRAM.dat","wb");
		if (f) //Opened?
		{
			byte plane,c;
			plane = 0; //Start at plane 0!
			for (plane=0;plane<4;plane++)
			{
				for (c=0;c<(getActiveVGA()->VRAM_size>>2);c++) //Process all data in VRAM!
				{
					byte data = readVRAMplane(getActiveVGA(),plane,0,c,0); //Read a direct byte from memory!
					emufwrite64(&data,1,1,f); //Write the VRAM byte!
				}
			}
			emufclose64(f); //Close the dump!
		}
	}
}

//Read port, write port and both!
#define VGAREGISTER_PORTR(port) register_PORTIN(&PORT_readVGA)
#define VGAREGISTER_PORTW(port) register_PORTOUT(&PORT_writeVGA)
#define VGAREGISTER_PORTRW(port) VGAREGISTER_PORTR(port);VGAREGISTER_PORTW(port)

extern byte is_XT; //XT emulation?

void resetPCISpaceVGA()
{
	//Info from: http://wiki.osdev.org/PCI
	PCI_VGA.VendorID = ((getActiveVGA()->enable_SVGA>=1) && (getActiveVGA()->enable_SVGA<=2))?0x100C:0xFFFF; //Tseng labs or plain VGA!
	PCI_VGA.DeviceID = 0x0000; //We're a VGA card!
	PCI_VGA.ClassCode = 3; //We...
	PCI_VGA.Subclass = 0; //Are...
	PCI_VGA.ProgIF = 0; //A VGA controller!
	PCI_VGA.HeaderType = 0x00; //Normal header!
	PCI_VGA.CacheLineSize = 0x00; //No cache supported!
	PCI_VGA.InterruptLine = is_XT?VGA_IRQ_XT:VGA_IRQ_AT; //What IRQ are we using?
	memset(&PCI_VGA.BAR,0,sizeof(PCI_VGA.BAR)); //Don't allow changing the BARs!
}

void VGA_ConfigurationSpaceChanged(uint_32 address, byte device, byte function, byte size)
{
	//Ignore device,function: we only have one!
	if (address == 0x3C) //IRQ changed?
	{
		PCI_VGA.InterruptLine = 0xFF; //We're unused, so let the software detect it, if required!
	}
	else //Unknown address? Ignore the write!
	{
		memset((byte *)&PCI_VGA+address,0,1); //Clear the set data!
	}
	resetPCISpaceVGA(); //Reset our address space always: we're read-only!
}

void setupVGA() //Sets the VGA up for PC usage (CPU access etc.)!
{
	if (__HW_DISABLED) return; //Abort!
	VGAmemIO_reset(); //Initialise/reset memory mapped I/O!
	VGA_initIO(); //Initialise I/O suppport!
	memset(&PCI_VGA, 0, sizeof(PCI_VGA)); //Initialise to 0!
	//PNP VGA isn't supported for the current hardware(based on http://www.lanpol.pl/sprzet/-5.PCI.vendors%20and%20Devices.txt)!
	//register_PCI(&PCI_VGA,2,0,(sizeof(PCI_VGA)>>2),&VGA_ConfigurationSpaceChanged); //Register the PCI data area!
	resetPCISpaceVGA(); //Make sure our space is initialized to detect!
}

/*

Internal terminate and start functions!

*/

void terminateVGA() //Terminate running VGA and disable it! Only to be used by root processes (non-VGA processes!)
{
	if (__HW_DISABLED) return; //Abort!
	if (!memprotect(getActiveVGA(), sizeof(*getActiveVGA()), "VGA_Struct"))
	{
		lockVGA(); //Lock the VGA!
		ActiveVGA = NULL; //Unused!
		unlockVGA(); //Finished!
		return; //We can't terminate without a VGA to terminate!
	}
	lockVGA(); //Lock the VGA!
	if (getActiveVGA()->Terminated)
	{
		unlockVGA(); //Finished!
		return; //Already terminated?
	}
	//No need to request for termination: we either are rendering in hardware (already not here), or here and not rendering at all!
	getActiveVGA()->Terminated = 1; //Terminate VGA!
	unlockVGA(); //VGA can run again!
}

void startVGA() //Starts the current VGA! (See terminateVGA!)
{
	if (__HW_DISABLED) return; //Abort!
	if (memprotect(getActiveVGA(),sizeof(*getActiveVGA()),"VGA_Struct")) //Valid VGA?
	{
		getActiveVGA()->Terminated = DISABLE_VGA; //Reset termination flag, effectively starting the rendering!
		VGA_calcprecalcs(getActiveVGA(),0); //Update full VGA to make sure we're running!
		VGA_initBWConversion(); //Initialise B/W conversion data!
		VGA_initRGBAconversion(); //Initialise B/W conversion data!
		updateLightPenMode(getActiveVGA()); //Update the light pen mode for the selected hardware!
	}
}

/*

For the emulator: setActiveVGA sets and starts a selected VGA (old one is terminated!)

*/

void setActiveVGA(VGA_Type *VGA) //Sets the active VGA chipset!
{
	if (__HW_DISABLED) return; //Abort!
	terminateVGA(); //Terminate currently running VGA!
	lockVGA();
	ActiveVGA = VGA; //Set the active VGA to this!
	unlockVGA();
	if (VGA) //Valid?
	{
		startVGA(); //Start the new VGA system!
	}
}

void doneVGA(VGA_Type **VGA) //Cleans up after the VGA operations are done.
{
	if (__HW_DISABLED) return; //Abort!
	VGA_Type *realVGA = *VGA; //The real VGA!
	if (!realVGA || allcleared) return; //Invalid VGA to deallocate!
	if (realVGA->SVGAExtension) //Extensions registered?
	{
		freez((void**)&realVGA->SVGAExtension, realVGA->SVGAExtension_size, "VGAExtension@DoneVGA"); //Free the extensions!
	}
	if (realVGA->VRAM) //Got allocated?
	{
		freez((void **)&realVGA->VRAM,realVGA->VRAM_size,"VGA_VRAM@DoneVGA"); //Free the VRAM!
	}
	if (realVGA->registers) //Got allocated?
	{
		freez((void **)&realVGA->registers,sizeof(*realVGA->registers),"VGA_Registers@DoneVGA"); //Free the registers!
	}
	if (realVGA->Sequencer)
	{
		freez((void **)&realVGA->Sequencer,sizeof(SEQ_DATA),"SEQ_DATA@DoneVGA"); //Free the registers!
	}
	if (VGA) //Valid ptr?
	{
		if (*VGA) //Allocated?
		{
			freez((void **)VGA,sizeof(*realVGA),"VGA@DoneVGA"); //Cleanup the real VGA structure finally!
		}
	}
}

//Cursor blink handler!
OPTINLINE void cursorBlinkHandler() //Handled every 16 frames!
{
	if (__HW_DISABLED) return; //Abort!
	if (getActiveVGA()) //Active?
	{
		getActiveVGA()->blink8 = !getActiveVGA()->blink8; //8 frames processed: Blink!
		if (!getActiveVGA()->blink8) //Reverted? 16 frames processed!
		{
			getActiveVGA()->blink16 = !getActiveVGA()->blink16; //Blink at 16 frames!
			if (!getActiveVGA()->blink16) //32 frames processed (we start at ON=1, becomes off first 16, becomes on second 16==32)
			{
				getActiveVGA()->blink32 = !getActiveVGA()->blink32; //Blink at 32 frames!
			}
		}
	}
}

//Now, input/output functions for the emulator.

byte VRAM_readdirect(uint_32 offset)
{
	if (__HW_DISABLED) return 0; //Abort!
	return getActiveVGA()->VRAM[SAFEMOD(offset,getActiveVGA()->VRAM_size)]; //Give the offset, protected overflow!
}

void VRAM_writedirect(uint_32 offset, byte value)
{
	if (__HW_DISABLED) return; //Abort!
	getActiveVGA()->VRAM[SAFEMOD(offset,getActiveVGA()->VRAM_size)] = value; //Set the offset, protected overflow!
}

void VGA_VBlankHandler(VGA_Type *VGA)
{
	if (__HW_DISABLED) return; //Abort!
//First: cursor blink handler every 16 frames!
	static byte cursorCounter = 0; //Cursor counter!
	++cursorCounter; //Next cursor!
	cursorCounter &= 0x7; //Reset every 8 frames!
	if (!cursorCounter) //Every 8 frames?
	{
		cursorBlinkHandler(); //This is handled every 8 frames!
	}
	
	if (VGA->wait_for_vblank) //Waiting for vblank?
	{
		VGA->wait_for_vblank = 0; //Reset!
		VGA->VGA_vblank = 1; //VBlank occurred!
	}
	
	renderHWFrame(); //Render the GPU a frame!
}

void VGA_waitforVBlank() //Wait for a VBlank to happen?
{
	if (__HW_DISABLED || __RENDERER_DISABLED) return; //Abort!
	lockVGA();
	getActiveVGA()->VGA_vblank = 0; //Reset we've occurred!
	getActiveVGA()->wait_for_vblank = 1; //We're waiting for vblank to happen!
	while (!getActiveVGA()->VGA_vblank) //Not happened yet?
	{
		unlockVGA(); //Allow some running time!
		delay(0); //Wait a bit for the VBlank to occur!
		lockVGA(); //Lock it for our checking!
	}
	unlockVGA(); //Allow to run again!
}

void VGA_setupEGAPalette(VGA_Type *VGA)
{
	word index;
	byte r,g,b;
	byte strengthtable[4] = {0x00,0x55,0xAA,0xFF}; //4 intentities of each of the 2-bit color channels!
	VGA->registers->DACMaskRegister = 0x3F; //Set a DAC mask register to apply!
	for (index=0;index<0x100;++index) //
	{
		r = strengthtable[((index >> 1) & 2) | ((index >> 5) & 1)];
		g = strengthtable[(index & 2) | ((index >> 4) & 1)];
		b = strengthtable[((index << 1) & 2) | ((index >> 3) & 1)];
		VGA->precalcs.DAC[index] = RGB(r,g,b); //Calculate the color to use!
	}
	VGA_calcprecalcs(VGA,WHEREUPDATED_DACMASKREGISTER); //Update the entire DAC with our loaded DAC values!
}
