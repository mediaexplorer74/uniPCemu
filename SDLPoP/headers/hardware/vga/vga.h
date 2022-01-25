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

#ifndef EMU_VGA_H
#define EMU_VGA_H

#include "headers/types.h"
#include "headers/hardware/vga/vga_precalcs.h" //Precalculation support!
#include "headers/emu/gpu/gpu.h" //For max X!
#include "headers/support/locks.h" //Locking support!
#include "headers/hardware/ports.h" //For registering extensions with us!

//Emulate VGA?
#define EMU_VGA 1

//4 planes of 64k totaling 256k (0x40000) of VRAM is a Minimum! Currently: 0x300000=1024x768x32;0x3C0000=1280x1024x24
#define VGA_VRAM_VGA 0x40000
#define VGA_VRAM_SVGA_1024_32 0x300000
#define VGA_VRAM_SVGA_1280_24 0x3C0000
//What size to use for a minimum: Should always be set to standard VGA.
#define VRAM_SIZE VGA_VRAM_VGA

//Our IRQ to use when enabled (EGA compatibility)!
#define VGA_IRQ_XT 0x12
#define VGA_IRQ_AT 11

//25MHz: VGA standard clock
//#define VGA25MHZ (25.2 / 1.001)*1000000.0
#ifdef IS_LONGDOUBLE
#define VGA25MHZ 25175000.0L
//28MHz: VGA standard clock
//#define VGA28MHZ (28.35 / 1.001)*1000000.0
#define VGA28MHZ 28322000.0L
#else
#define VGA25MHZ 25175000.0
//28MHz: VGA standard clock
//#define VGA28MHZ (28.35 / 1.001)*1000000.0
#define VGA28MHZ 28322000.0
#endif

//Enable VGA I/O dump when recording? Also specifies the length to dump(in ns)!
//#define VGAIODUMP 40000000.0f

//SRC for registers: http://www.osdever.net/FreeVGA/vga/vga.htm
//- Input/Output Register Information, before Indices.

//First, the registers:

//We're low endian, so low end first (DATA[0] is first item and First bit is lowest bit (so in 8-bits bit with value 1))

//Graphics Registers: OK

#include "headers/packed.h" //We're packed!
typedef union PACKED
{
	byte DATA[9]; //9 registers present!
	struct //Contains the registers itself!
	{
		//Set/Reset Register (index 00h)
		byte SETRESETREGISTER;

		//Enable Set/Reset register (index 01h)
		byte ENABLESETRESETREGISTER;

		//Color Compare register (index 02h)
		byte COLORCOMPAREREGISTER;

		//Data Rotate Register register (index 03h)
		byte DATAROTATEREGISTER;

		//Read Map Select Register (index 04h)

		byte READMAPSELECTREGISTER;

		//Graphics Mode Register (Index 05h)
		byte GRAPHICSMODEREGISTER;

		byte MISCGRAPHICSREGISTER;

		byte COLORDONTCAREREGISTER;

		byte BITMASKREGISTER; //Bit Mask Register (index 08h)
	} REGISTERS; //The registers itself!
} GRAPHREGS; //Graphics registers!
#include "headers/endpacked.h" //We're packed!





//Sequencer Registers: OK

#include "headers/packed.h" //We're packed!
typedef union PACKED
{
	byte DATA[0x8]; //5 registers present, 1 undocumented!

	struct //Contains the registers itself!
	{
		byte RESETREGISTER; //Reset Register (index 00h)

		byte CLOCKINGMODEREGISTER; //Clocking Mode Register (index 01h)

		byte MAPMASKREGISTER; //Map Mask Register (index 02h)

		byte CHARACTERMAPSELECTREGISTER; //Character Map Select Register (index 03h)

		byte SEQUENCERMEMORYMODEREGISTER; //Sequencer Memory Mode Register (index 04h)

		byte unused[2];
		
		byte DISABLERENDERING; //Just here for compatibility (actual disable check is external). The CPU can read/write to this freely. Only writes to this register are counted.
	} REGISTERS;
} SEQUENCERREGS;
#include "headers/endpacked.h" //We're packed!


//Attribute Controller Registers: OK

#include "headers/packed.h" //We're packed!
typedef union PACKED
{
	byte DATA[0x15]; //10h color registers + 5 registers present!

	struct //Contains the registers itself!
	{
		byte PALETTEREGISTERS[0x10];

		byte ATTRIBUTEMODECONTROLREGISTER;

		byte OVERSCANCOLORREGISTER;

		byte COLORPLANEENABLEREGISTER;

		byte HORIZONTALPIXELPANNINGREGISTER;

		byte COLORSELECTREGISTER;
	} REGISTERS;
} ATTRIBUTECONTROLLERREGS;
#include "headers/endpacked.h" //We're packed!


//CRT Controller Registers

#include "headers/packed.h" //We're packed!
typedef union PACKED
{
	byte DATA[0x25]; //25h registers present, up to 0x40 are undocumented!

	struct //Contains the registers itself!
	{
		/*

		Officially documented registers:

		*/
		byte HORIZONTALTOTALREGISTER; //00: # of char. clocks per scan line index #0

		byte ENDHORIZONTALDISPLAYREGISTER; //01:When to stop outputting pixels from display memory. index #1

		byte STARTHORIZONTALBLANKINGREGISTER; //02:At which character clock does the horizontal blanking period begin? index #2

		byte ENDHORIZONTALBLANKINGREGISTER; //index #3

		byte STARTHORIZONTALRETRACEREGISTER; //When to move to the left side of the screen. index #4

		byte ENDHORIZONTALRETRACEREGISTER; //index #5

		byte VERTICALTOTALREGISTER; //Lower 8 bits of the Vertical Total field. Bits 9-8 are located in the Overflow Register. index #6

		byte OVERFLOWREGISTER; //index #7

		byte PRESETROWSCANREGISTER; //index #8

		byte MAXIMUMSCANLINEREGISTER; //index #9

		byte CURSORSTARTREGISTER; //index #a

		byte CURSORENDREGISTER; //index #b

		byte STARTADDRESSHIGHREGISTER; //Bits 15-8 of the Start Address field. index #e
		byte STARTADDRESSLOWREGISTER; //Bits 7-0 of the Start Address field. Specifies 0,0 location of the display memory. index #f

		byte CURSORLOCATIONHIGHREGISTER; //Bits 15-8 of the Cursor Location field. index #c
		byte CURSORLOCATIONLOWREGISTER; //Bits 7-0 of the Cursor Location field. When displaying text-mode and the cursor is enabled, compare current display address with the sum of this field and the Cursor Skew field. If it equals then display cursor depending on scan lines. index #d

		byte VERTICALRETRACESTARTREGISTER; //Index #10:Bits 7-0 of Vertical Retrace Start field. Bits 9-8 are in the Overflow Register. When to move up to the beginning of display memory. Contains the value of the vertical scanline counter at the beginning of the 1st scanline where the signal is asserted.

		byte VERTICALRETRACEENDREGISTER; //Index #11

		byte VERTICALDISPLAYENDREGISTER; //Index #12: Bits 7-0 of the Vertical Display End field. Bits 9-8 are located in the Overflow Register.

		byte OFFSETREGISTER; //Index #13: Specifies the address difference between two scan lines (graphics) or character (text).

		/*
		Text modes: Width/(MemoryAddressSize*2).
				MemoryAddressSize =1:byte,2:word,4:dword.
		Graphics modes: Width/(PixelsPerAddress*MemoryAddressSize*2)
				Width=Width in pixels of the screen
				PixelsPerAddress=Number of pixels stored in one display memory address.
				MemoryAddressSize is the current memory addressing size.
		*/

		byte UNDERLINELOCATIONREGISTER; //Index #14

		byte STARTVERTICALBLANKINGREGISTER; //Index #15: Bits 7-0 of the Start Vertical Blanking field. Bit 8 is located in the Overflow Register, and bit 9 is located in the Maximum Scan Line register. Determines when the vblanking period begins, and contains the value of the vertical scanline counter at the beginning of the first vertical scanline of blanking.

		byte ENDVERTICALBLANKINGREGISTER; //Index #16


		byte CRTCMODECONTROLREGISTER; //Index #17


		byte LINECOMPAREREGISTER; //Index 18h: Bits 7-0 of the Line Compare field. Bit 9 is located in the Maximum Scan Line Register and bit 8 is located in the Overflow Register. Specifies the scan line at which a horizontal division can occur, providing for split-screen operation. If no horizontal division is required, this field sohuld be set to 3FFh. When the scan line counter reaches the value of the Line Compare field, the current scan line address is reset to 0 and the Preset Row Scan is presumed to be 0. If the Pixel Panning Mode field is set to 1 then the Pixel Shift Count and the Byte Panning fields are reset to 0 for the remainder of the display cycle.

		//Some undocumented registers (index 22,24,3X)

		byte notused[10]; //Unused registers
		
		byte GraphicsControllerDataLatches; //Index 22 data latch 'n', controlled by Graphics Register 4 bits 0-1; range 0-3; READ ONLY!

		byte unused2; //Unused: index 23!

		byte ATTRIBUTECONTROLLERTOGGLEREGISTER; //Index 24 Attribute Controller Toggle Register (CR24). READ ONLY at 3B5/3D5 index 24h!
	} REGISTERS;
} CRTCONTROLLERREGS;
#include "headers/endpacked.h" //We're packed!

//Attribute controller toggle register location for precalcs!
#define VGA_CRTC_ATTRIBUTECONTROLLERTOGGLEREGISTER 24

typedef struct
{
	byte DAC_ADDRESS_WRITE_MODE_REGISTER; //Port 3C8
	byte DAC_ADDRESS_READ_MODE_REGISTER; //Port 3C7 Write only
	byte DAC_ADDRESS_DATA_REGISTER; //Port 3C9 immediately processed if state is OK (on R/W).
	byte DAC_STATE_REGISTER; //Port 3C7 Read only
} COLORREGS;

typedef struct
{
	byte MISCOUTPUTREGISTER; //Read at 3CCh, Write at 3C2h

	byte FEATURECONTROLREGISTER; //Read at 3CAh, write at 3BAh(mono)/3DAh(color) Bit 1 is lightpen triggered when on!

	byte INPUTSTATUS0REGISTER; //Input status #0 register (Read-only at 3C2h)

	byte INPUTSTATUS1REGISTER; //Read at 3BAh(mono), Read at 3DAh(color)

	union
	{
		uint_32 latch; //The full data latch! This is always stored as little endian!
		byte latchplane[4]; //All 4 plane latches!
	} DATALATCH;
} EXTERNALREGS;

#include "headers/packed.h" //We're packed!
typedef struct PACKED
{
	byte r;
	byte g;
	byte b;
} DACEntry; //A DAC Entry!
#include "headers/endpacked.h" //We're packed!

#include "headers/packed.h" //We're packed!
typedef struct PACKED
{
	byte VideoMode; //Active video mode!

//Next, our registers:

//First, the indexed registers:
	GRAPHREGS GraphicsRegisters; //Graphics registers!
	byte GraphicsRegisters_Index; //Current index!
	byte GraphicsRegisters_IndexRegister; //Index register!

	SEQUENCERREGS SequencerRegisters; //Sequencer registers!
	byte SequencerRegisters_Index; //Current index!
	byte SequencerRegisters_IndexRegister; //Index register!

	ATTRIBUTECONTROLLERREGS AttributeControllerRegisters; //Attribute controller registers!

	CRTCONTROLLERREGS CRTControllerRegisters; //CRT Controller registers!
	byte CRTControllerDontRender; //No rendering?
	byte CRTControllerRegisters_Index; //Current index!
	byte CRTControllerRegisters_IndexRegister; //Index register!

//Now the normal registers, by group:

	COLORREGS ColorRegisters; //Color registers!
	EXTERNALREGS ExternalRegisters; //External registers!

	byte DACMaskRegister; //DAC Mask Register (port 0x3C6 for read/write; default is 0xFF)
	byte DAC[1024]; //All DAC values (4 bytes an entry)!

//For colors:
	byte colorIndex; //Written to port 0x3C8; 3 bytes to 3C9 in order R, G, B
	byte current_3C9; //Current by out of 3 from writing to 3C9.

//Index registers:
//Set by writing to 0x3C0, set address or data depending on 3C0_Current. See CRTController Register 24!

	byte ModeStep; //Current mode step (used for mode 0&1)!
	word Scanline; //Current scan line updating!

	byte VerticalDisplayTotalReached; //Set when Scanline>max. Setting this causes Scanline to reset!
	byte lightpen_high; //Lightpen high register!
	byte lightpen_low; //Lightpen low register!
	byte switches; //The switches for the EGA/VGA!
	//CGA/MDA compatibility registers, when enabled!
	byte Compatibility_MDAModeControl;
	byte Compatibility_CGAModeControl;
	byte Compatibility_CGAPaletteRegister;
	byte specialCGAflags; //Special flags concerning CGA emulation!
	byte specialMDAflags; //Special flags concerning MDA emulation!
	byte specialCGAMDAflags; //Combined(OR-ed) CGA&MDA flags.
	byte CGARegisters[18]; //18 CGA registers!
	byte CGARegistersMasked[18]; //18 Masked CGA registers!
	byte EGA_lightpenstrobeswitch; //Strobe/switch status of the light pen! Bit0=1: Light pen trigger has been set and is pending. Bit1=Light pen has been triggered and stopped pending(light pen register contents are valid). Bit2=1: Light pen switch is open(button on the light pen has been pressed).
	byte verticalinterruptflipflop; //Current vertical interrupt flipflop!
	byte VGA_enabled; //Responding to MMU and IO ports other than the enable bits?
	byte EGA_graphics1position; //EGA Graphics 1 position
	byte EGA_graphics2position; //EGA Graphics 2 position
} VGA_REGISTERS;
#include "headers/endpacked.h" //We're packed!

typedef struct
{
	uint_32 rowstatus[0x1000]; //Row status!
	word charrowstatus[0x2000]; //Character row status (double the row status, for character and inner)
	uint_32 colstatus[0x8000]; //Column status!
	word charcolstatus[0x10000]; //Character column status (double the row status, for character and inner)
	word textcharcolstatus[0x10000]; //Character column status (double the row status, for character and inner)
	word extrahorizontalstatus[0x10000]; //Extra status information for rendering active display!
	//Current processing coordinates on-screen!
	word x; //X coordinate on the screen!
	word y; //Y coordinate on the screen!
	byte DisplayEnabled; //Is the display signal enabled?
	byte DACOutput; //The current outputted DAC index!
	byte CRTCBwindowEnabled; //bit 0: CRTCB active, bit 1: CRTCB active within the current scanline
	byte CRTCBwindowmaxstatus; //Maximum status detected on the CRTCBwindowenabled flag!
} VGA_CRTC; //CRTC information!

typedef struct
{
//First, VRAM and registers:
	byte *VRAM; //The VRAM: 64K of 32-bit values, byte align!
	uint_32 VRAM_size; //The size of the VRAM!
	uint_32 VRAM_used; //How much VRAM is used?
	byte CGAMDAShadowRAM[0x4000]; //ShadowRAM for static adapter reads!
	byte CGAMDAMemoryMode; //What memory mode(for restoring RAM during mode changes).
//Active video mode:

	VGA_REGISTERS *registers; //The registers itself!

	uint_32 ExpandTable[256]; //Expand Table (originally 32-bit) for VRAM read and write by CPU!
	uint_32 FillTable[16]; //Fill table for memory writes!
	word getcharxy_values[0x4000]; //All getcharxy values!
	byte blink8; //Blink rate 8 frames?
	byte blink16; //Blink rate 16 frames?
	byte blink32; //Blink rate 32 frames?

	int initialised; //Are we initialised (row timer active? After VGAalloc is called, this is 1, row timer sets this to 0!)

//For rendering:
	int frameDone; //We have a frame ready for rendering? (for GPU refresh)
	byte CharacterRAMUpdated; //Character RAM updated?	
	
	//VGA Termination request and active state!
	int Request_Termination; //Request VGA termination: we're going to stop etc.
	int Terminated; //We're terminated?
	
//VBlank request and occurred.
	int wait_for_vblank; //Waiting for VBlank?
	int VGA_vblank; //VBlank occurred?
	VGA_PRECALCS precalcs; //Precalcs, for speedup!

	byte frameskip; //Frameskip!
	uint_32 framenr; //Current frame number (for Frameskip, kept 0 elsewise.)
	
	void *Sequencer; //The active sequencer to use!
	VGA_CRTC CRTC; //The CRTC information!

	byte PixelCounter; //A simple counter to count up to 16 hdots with!
	byte WaitState; //Active waitstate on CGA/MDA? Wait the CPU when enabled!
	byte WaitStateCounter; //What are we counting to?
	void *SVGAExtension; //The SVGA extension data, if any!
	uint_32 SVGAExtension_size; //The size of the SVGA extension data, if any!
	byte enable_SVGA; //Enable SVGA? If >0, a SVGA extension is enabled. Then initialize it as needed!
	byte DACbrightness[0x100]; //All 256 levels of brightness for active display DAC!
} VGA_Type; //VGA dataset!

typedef DOUBLE (*VGA_clockrateextensionhandler)(VGA_Type *VGA); //The clock rate extension handler!
typedef uint_32 (*VGA_addresswrapextensionhandler)(VGA_Type *VGA, uint_32 memoryaddress); //The DWord shift memory address extension handler!

#ifndef IS_VGA
extern VGA_Type *ActiveVGA; //Currently active VGA chipset!
#endif

#define lockVGA() lock(LOCK_CPU)
#define unlockVGA() unlock(LOCK_CPU)
//Give the active VGA!
#define getActiveVGA() ActiveVGA

//Palette support!
//Port 3C0 info holder:
#define VGA_3C0_HOLDER getActiveVGA()->registers->CRTControllerRegisters.REGISTERS.ATTRIBUTECONTROLLERTOGGLEREGISTER

//The flipflop of the port 3C0 toggler and port itself!
#define VGA_3C0_FLIPFLOPR GETBITS(VGA_3C0_HOLDER,7,1)
#define VGA_3C0_PALR GETBITS(VGA_3C0_HOLDER,5,1)
#define VGA_3C0_INDEXR GETBITS(VGA_3C0_HOLDER,0,0x1F)
#define VGA_3C0_FLIPFLOPW(val) SETBITS(VGA_3C0_HOLDER,7,1,val)
#define VGA_3C0_PALW(val) SETBITS(VGA_3C0_HOLDER,5,1,val)
#define VGA_3C0_INDEXW(val) SETBITS(VGA_3C0_HOLDER,0,0x1F,val)

/*

All functions!

*/

VGA_Type *VGAalloc(uint_32 custom_vram_size, int update_bios, byte extension, byte enableColorPedestal); //Allocates VGA and gives the current set!
void setupVGA(); //Sets the VGA up for PC usage (CPU access etc.)!
void setActiveVGA(VGA_Type *VGA); //Sets the active VGA chipset!
void terminateVGA(); //Terminate running VGA and disable it! Only to be used by root processes (non-VGA processes!)
void startVGA(); //Starts the current VGA! (See terminateVGA!)
void doneVGA(VGA_Type **VGA); //Cleans up after the VGA operations are done.


//CPU specific: ***
byte VRAM_readdirect(uint_32 offset);
void VRAM_writedirect(uint_32 offset, byte value); //Direct read/write!

byte PORT_readVGA(word port, byte *result); //Read from a port/register!
byte PORT_writeVGA(word port, byte value); //Write to a port/register!
//End of CPU specific! ***

//DAC (for rendering)
void readDAC(VGA_Type *VGA,byte entrynumber,DACEntry *entry); //Read a DAC entry
void writeDAC(VGA_Type *VGA,byte entrynumber,DACEntry *entry); //Write a DAC entry

//CRTC Controller renderer&int10:
void VGA_VBlankHandler(VGA_Type *VGA); //VBlank handler for VGA!

void VGA_waitforVBlank(); //Wait for a VBlank to happen?

void changeRowTimer(VGA_Type *VGA); //Change the VGA row processing timer the ammount of lines on display: should be in the emulator itself!

void dumpVRAM(); //Diagnostic dump of VRAM!

void VGAmemIO_reset(); //Reset/initialise memory mapped I/O for VGA!

void VGA_dumpFonts(); //Dump all VGA fonts!

void VGA_plane23updated(VGA_Type *VGA, uint_32 address); //Plane 2/3 has been updated?

void setVGA_NMIonPrecursors(byte enabled); //Trigger an NMI when our precursors are called?

void adjustVGASpeed(); //Auto-adjust our VGA speed!

//Initialisation call for CPU extension support!
void VGA_initIO(); //Initialise all I/O support for the VGA/EGA/CGA/MDA!

//Extension support!
void VGA_registerExtension(PORTIN readhandler, PORTOUT writehandler, Handler initializationhandler, VGA_calcprecalcsextensionhandler precalcsextensionhandler, VGA_clockrateextensionhandler clockrateextension, VGA_addresswrapextensionhandler addresswrapextension); //Register an extension for use with the VGA!

void PORT_write_MISC_3C2(byte value); //Misc Output register updating for SVGA!

void updateVGAWaitState(); //Update the new WaitState for the VGA handler!

void VGA_setupEGAPalette(VGA_Type *VGA); //Setup the EGA palette for the EGA emulation!

//Support for adapters using unmapped memory!
byte VGAmemIO_wb(uint_32 offset, byte value);
void CGAMDA_doWriteRAMrefresh(uint_32 offset);

//Support for the MMU to call directly!
byte VGAmemIO_rb(uint_32 offset, byte index);
byte VGAmemIO_wb(uint_32 offset, byte value);
byte extVGA_isnotVRAM(uint_32 offset); //Isn't VRAM?

#endif
