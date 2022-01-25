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

#ifndef TSENG_H
#define TSENG_H

#include "headers/types.h" //Basic types!
#include "headers/hardware/vga/vga.h" //Basic VGA!
#include "headers/support/fifobuffer.h" //Fifo support!

typedef struct
{
	uint_32 patternmapaddress; //Pattern map address
	uint_32 sourcemapaddress; //Source map address
	uint_32 patternYoffset; //Pattern Y offset
	uint_32 sourceYoffset; //Source Y offset
	uint_32 destinationYoffset; //Destination Y offset
	byte virtualbussize; //Virtual bus size, powers of 2! 0=1, 1=2, 2=4, 3=Reserved
	byte virtualbussizecount; //Virtual bus size, powers of 2! 1, 2, 4!
	byte XYdirection; //0=+1,+1; 1=-1,+1; 2=+1,-1; 3=-1,-1. Essentially bit 0=X direction, bit 1=Y direction. Set=Decreasing, Cleared=Increasing
	byte Xpatternwrap; //Power of 2. more than 64 or less than 4 is none.
	byte Ypatternwrap; //Power of 2. more than 8 is none.
	byte patternwrap_bit6; //Bit 6 as used?
	byte Xsourcewrap; //See pattern wrap
	byte Ysourcewrap; //See pattern wrap
	byte sourcewrap_bit6; //Bit 6 as used?
	uint_32 Xposition;
	uint_32 Yposition;
	uint_32 Xcount;
	uint_32 Ycount;
	byte reloadPatternAddress;
	byte reloadSourceAddress;
	byte BGFG_RasterOperation[2]; //Index 0=BG, 1=FG
	uint_32 destinationaddress; //Destination address
	//Intermediate variables
	uint_32 internalpatternaddress;
	uint_32 internalsourceaddress;
	byte latchedmixmap; //The mixmap input that's latched!
	uint_32 patternmap_x;
	uint_32 sourcemap_x;
	uint_32 patternmap_y;
	uint_32 sourcemap_y;
	uint_32 patternmap_x_backup;
	uint_32 sourcemap_x_backup;
	uint_32 patternmapaddress_backup; //Used for wrapping
	uint_32 sourcemapaddress_backup; //Used for wrapping
	uint_32 destinationaddress_backup; //Used for wrapping and newlines
	uint_32 patternwrap_x; //Horizontal pattern wrap
	uint_32 patternwrap_y; //Vertical pattern wrap
	uint_32 sourcewrap_x; //Horizontal source wrap
	uint_32 sourcewrap_y; //Vertical source wrap
	byte W32_newXYblock; //Starting a new X/Y block?
	byte ACL_active; //ACL is actually active and running?
	byte XCountYCountModeOriginal; //Original byte in the X Count or Y Count register when starting the accelerated mode.
	byte XYSTtriggersstart; //XYST triggers a start of a transfer!
} ET4000_W32_ACL_PRECALCS;

typedef struct {
	byte tsengExtensions; //0=Normal ET4000, 1=ET4000/W32
	byte extensionsEnabled;
	byte oldextensionsEnabled; //Old extensions status for detecting changes!

// Stored exact values of some registers. Documentation only specifies some bits but hardware checks may
// expect other bits to be preserved.
	//ET4K registers
	byte store_et4k_3d4_31;
	byte store_et4k_3d4_32;
	byte store_et4k_3d4_33;
	byte store_et4k_3d4_34;
	byte store_et4k_3d4_35;
	byte store_et4k_3d4_36;
	byte store_et4k_3d4_37;
	byte store_et4k_3d4_3f;
	//ET4K/W32 registers
	byte store_et4k_W32_3d4_30;

	//ET3K registers
	byte store_et3k_3d4_1b;
	byte store_et3k_3d4_1c;
	byte store_et3k_3d4_1d;
	byte store_et3k_3d4_1e;
	byte store_et3k_3d4_1f;
	byte store_et3k_3d4_20;
	byte store_et3k_3d4_21;
	byte store_et3k_3d4_23; // note that 22 is missing
	byte store_et3k_3d4_24;
	byte store_et3k_3d4_25;

	//ET3K/ET4K registers
	byte store_3c0_16;
	byte store_3c0_17;

	byte store_3c4_06;
	byte store_3c4_07;

	byte herculescompatibilitymode;
	byte herculescompatibilitymode_secondpage; //Second page of hercules compatibility mode enabled?

	byte extensionstep; //The steps to activate the extensions!
	//Extra data added by superfury(Device specific precalculation storage)

	//Banking support
	byte et4k_segmentselectregisterenabled; //Segment select register on the ET4000 has been enabled?
	byte segmentselectregister; //Containing the below values.
	byte extendedbankregister; //Containing an extensions of the below values (Extended bank register)! W32 only!
	byte bank_read; //Read bank number!
	byte bank_write; //Write bank number!
	byte bank_size; //The bank size to use(2 bits)!

	//Extended bits
	uint_32 display_start_high;
	uint_32 cursor_start_high;
	uint_32 line_compare_high;
	byte doublehorizontaltimings; //Doubling the horizontal timings!

	//Memory wrapping
	uint_32 memwrap; //The memory wrap to be AND-ed into the address given!
	uint_32 memwrap_init; //Poweron value!
	byte et4k_reg37_init; //Poweron value!

	//Attribute protection?
	byte protect3C0_Overscan; //Disable writes to bits 0-3 of the Overscan?
	byte protect3C0_PaletteRAM; //Disable writes to Internal/External Palette RAM?

	//High color DAC information
	byte hicolorDACcmdmode;
	byte hicolorDACcommand;

	byte CGAModeRegister;
	byte MDAModeRegister;
	byte CGAColorSelectRegister;
	byte ExtendedFeatureControlRegister; //Feature control extension(extra bits)!
	byte useInterlacing; //Interlacing enabled?
	byte emulatedDAC; //What kind of emulated DAC? 0=SC11487, 1=UMC UM70C178

	//SC15025 DAC registers!
	byte SC15025_extendedaddress; //Extended address register!
	byte SC15025_auxiliarycontrolregister; //Auxiliary control register. Bit 0=8-bit DAC when set. 6-bit otherwise.
	//ID registers are ROM!
	byte SC15025_secondarypixelmaskregisters[3]; //Secondary pixel mask registers!
	byte SC15025_pixelrepackregister; //bit 0=Enable 4-byte fetching in modes 2 and 3!
	byte SC15025_enableExtendedRegisters; //Enable the extended registers at the color registers?

	//W32 registers
	byte W32_21xA_index; //Index selected
	byte W32_21xA_shadowRegisters[0x20]; //The registers from E0 through FF shadowed for both CRTC and Sprite Registers
	byte W32_21xA_CRTCBSpriteControl; //Index EF in both sprite and CRTCB modes. Bit 0=1: Sprite window, 0=CRTC window
	byte W32_21xA_ImagePortControl; //Index F7 in both sprite and CRTCB modes. Bit 0=Enable the Image Port. Bit 1=Odd/even interlace transfers, Bit 7=Enable CRTCB window or hardware cursor(Sprite).
	byte W32_MMUregisters[2][0x100]; //All W32 memory mapped registers! First is the queued registers(or non-queued). Second is the processing registers.
	byte W32_performMMUoperationstart; //Perform a MMU-type operation start?
	byte W32_MMUsuspendterminatefilled; //Is the suspend/terminate flag filled (alternative to the queue)?
	FIFOBUFFER* W32_MMUqueue, *W32_virtualbusqueue; //The actual queue that's maintained in the background with multiple entries!
	//Direct MMU queue being emptied below:
	byte W32_MMUqueueval; //What value is stored inside the queue?
	uint_32 W32_MMUqueueval_address; //What offset inside the queue is filled!
	uint_32 W32_MMUqueueval_bankaddress; //Address for any bank, if supplied by an MMU aperture!
	//Virtual bus queue being emptied below:
	byte W32_VirtualBusCountLeft; //How much is left on the virtual bus size?
	byte W32_virtualbusqueueval; //What value is stored inside the queue?
	uint_32 W32_virtualbusqueueval_address; //What offset inside the queue is filled!
	uint_32 W32_virtualbusqueueval_bankaddress; //Address for any bank, if supplied by an MMU aperture!
	//Normal accelerator status:
	byte W32_acceleratorbusy; //Is the accelerator started up in a processing? bit 0=ticking this clock,  bit 1=operation still in progress
	byte W32_acceleratorleft; //How many ticks are left to process!
	ET4000_W32_ACL_PRECALCS W32_ACLregs; //ACL registers used during rendering
	byte W32_version; //What version of the W32 is emulated?
	byte W32_mixmapposition; //Position in the mix map data currently to be processed!
	byte W32_waitstateremainderofqueue; //Waitstate the remainder of the queue instead of ignoring or faulting?
	byte W32_transferstartedbyMMU; //Type 0 transfer started by the MMU?
} SVGA_ET34K_DATA; //Dosbox ET4000 saved data!

//Retrieve a point to the et4k?
#define et34k(VGA) ((SVGA_ET34K_DATA *)VGA->SVGAExtension)
//Retrieve the active et4k!
#define et34k_data et34k(getActiveVGA())

#define et4k_reg(data,port,index) data->store_et4k_##port##_##index
#define et4k_W32_reg(data,port,index) data->store_et4k_W32_##port##_##index
#define et3k_reg(data,port,index) data->store_et3k_##port##_##index
#define et34k_reg(data,port,index) data->store_##port##_##index

//ET4K register access
#define STORE_ET4K_W32(port, index, category) \
	case 0x##index: \
	if ((getActiveVGA()->enable_SVGA!=1) || ((getActiveVGA()->enable_SVGA==1) && (!et34kdata->extensionsEnabled)) || (et34kdata->tsengExtensions==0)) return 0; \
	et34k_data->store_et4k_W32_##port##_##index = val; \
	VGA_calcprecalcs(getActiveVGA(),category|0x##index); \
	return 1;

#define STORE_ET4K(port, index, category) \
	case 0x##index: \
	if ((getActiveVGA()->enable_SVGA!=1) || ((getActiveVGA()->enable_SVGA==1) && (!et34kdata->extensionsEnabled))) return 0; \
	et34k_data->store_et4k_##port##_##index = val; \
	VGA_calcprecalcs(getActiveVGA(),category|0x##index); \
	return 1;

#define STORE_ET4K_W32_UNPROTECTED(port, index, category) \
	case 0x##index: \
	if ((getActiveVGA()->enable_SVGA!=1) || (et34kdata->tsengExtensions==0)) return 0; \
	et34k_data->store_et4k_W32_##port##_##index = val; \
	VGA_calcprecalcs(getActiveVGA(),category|0x##index); \
	return 1;

#define STORE_ET4K_UNPROTECTED(port, index, category) \
	case 0x##index: \
	if (getActiveVGA()->enable_SVGA!=1) return 0; \
	et34k_data->store_et4k_##port##_##index = val; \
	VGA_calcprecalcs(getActiveVGA(),category|0x##index); \
	return 1;

#define RESTORE_ET4K_W32(port, index) \
	case 0x##index: \
		if ((getActiveVGA()->enable_SVGA!=1) || ((getActiveVGA()->enable_SVGA==1) && (!et34kdata->extensionsEnabled)) || (et34kdata->tsengExtensions==0)) return 0; \
		*result = et34k_data->store_et4k_W32_##port##_##index; \
		return 1;

#define RESTORE_ET4K(port, index) \
	case 0x##index: \
		if ((getActiveVGA()->enable_SVGA!=1) || ((getActiveVGA()->enable_SVGA==1) && (!et34kdata->extensionsEnabled))) return 0; \
		*result = et34k_data->store_et4k_##port##_##index; \
		return 1;

#define RESTORE_ET4K_W32_UNPROTECTED(port, index) \
	case 0x##index: \
		if ((getActiveVGA()->enable_SVGA!=1) || (et34kdata->tsengExtensions==0)) return 0; \
		*result = et34k_data->store_et4k_W32_##port##_##index; \
		return 1;

#define RESTORE_ET4K_UNPROTECTED(port, index) \
	case 0x##index: \
		if (getActiveVGA()->enable_SVGA!=1) return 0; \
		*result = et34k_data->store_et4k_##port##_##index; \
		return 1;

//ET3K register access
#define STORE_ET3K(port, index, category) \
	case 0x##index: \
		if (getActiveVGA()->enable_SVGA!=2) return 0; \
		et34k_data->store_et3k_##port##_##index = val; \
		VGA_calcprecalcs(getActiveVGA(),category|0x##index); \
		return 1;

#define STORE_ET3K_UNPROTECTED(port, index, category) \
	case 0x##index: \
		if (getActiveVGA()->enable_SVGA!=2) return 0; \
		et34k_data->store_et3k_##port##_##index = val; \
		VGA_calcprecalcs(getActiveVGA(),category|0x##index); \
		return 1;

#define RESTORE_ET3K(port, index) \
	case 0x##index: \
		if (getActiveVGA()->enable_SVGA!=2) return 0; \
		*result = et34k_data->store_et3k_##port##_##index; \
		return 1;

#define RESTORE_ET3K_UNPROTECTED(port, index) \
	case 0x##index: \
		if (getActiveVGA()->enable_SVGA!=2) return 0; \
		*result = et34k_data->store_et3k_##port##_##index; \
		return 1;

//ET3K/ET4K register access
#define STORE_ET34K(port, index, category) \
	case 0x##index: \
	if ((getActiveVGA()->enable_SVGA<1) || (getActiveVGA()->enable_SVGA>2) || ((getActiveVGA()->enable_SVGA==1) && (!et34kdata->extensionsEnabled))) return 0; \
		et34k_data->store_##port##_##index = val; \
		VGA_calcprecalcs(getActiveVGA(),category|0x##index); \
		return 1;

//3C0: write only if not protected, handle flipflop always!
#define STORE_ET34K_3C0(port, index, category) \
	case 0x##index: \
	if ((getActiveVGA()->enable_SVGA<1) || (getActiveVGA()->enable_SVGA>2)) return 0; \
	if (!((getActiveVGA()->enable_SVGA==1) && (!et34kdata->extensionsEnabled))) \
	{ \
		et34k_data->store_##port##_##index = val; \
	} \
		VGA_3C0_FLIPFLOPW(!VGA_3C0_FLIPFLOPR); \
	if (!((getActiveVGA()->enable_SVGA==1) && (!et34kdata->extensionsEnabled))) \
	{ \
		VGA_calcprecalcs(getActiveVGA(),category|0x##index); \
	} \
		return 1;

#define STORE_ET34K_UNPROTECTED(port, index, category) \
	case 0x##index: \
	if ((getActiveVGA()->enable_SVGA<1) || (getActiveVGA()->enable_SVGA>2)) return 0; \
		et34k_data->store_##port##_##index = val; \
		VGA_calcprecalcs(getActiveVGA(),category|0x##index); \
		return 1;

#define RESTORE_ET34K(port, index) \
	case 0x##index: \
	if ((getActiveVGA()->enable_SVGA<1) || (getActiveVGA()->enable_SVGA>2) || ((getActiveVGA()->enable_SVGA==1) && (!et34kdata->extensionsEnabled))) return 0; \
		*result = et34k_data->store_##port##_##index; \
		return 1;

#define RESTORE_ET34K_UNPROTECTED(port, index) \
	case 0x##index: \
	if ((getActiveVGA()->enable_SVGA<1) || (getActiveVGA()->enable_SVGA>2)) return 0; \
		*result = et34k_data->store_##port##_##index; \
		return 1;

void SVGA_Setup_TsengET4K(uint_32 VRAMSize, byte ET4000_extensions);
void set_clock_index_et4k(VGA_Type *VGA, byte index); //Used by the interrupt 10h handler to set the clock index directly!
void set_clock_index_et3k(VGA_Type *VGA, byte index); //Used by the interrupt 10h handler to set the clock index directly!
byte Tseng34k_doublecharacterclocks(VGA_Type * VGA); //Doubled character clocks width?

byte Tseng4k_readMMUregister(byte address, byte *result);
byte Tseng4k_writeMMUregister(byte address, byte value);
byte Tseng4k_readMMUaccelerator(byte area, uint_32 address, byte * result);
byte Tseng4k_writeMMUaccelerator(byte area, uint_32 address, byte value);
void Tseng4k_tickAccelerator(); //Tick the accelerator one clock!
void Tseng4k_handleTermination(); //Terminate a memory cycle!
#endif
