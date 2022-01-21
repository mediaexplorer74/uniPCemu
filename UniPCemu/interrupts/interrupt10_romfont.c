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

#include "headers/types.h"
#include "headers/cpu/mmu.h" //MMU support!
#include "headers/interrupts/textmodedata.h" //Text mode data itself!
#include "headers/header_dosbox.h" //Dosbox support!
#include "headers/hardware/ports.h" //Ports support!
#include "headers/cpu/cb_manager.h" //Callback support!
#include "headers/cpu/protection.h" //Protection support!
#include "headers/support/log.h" //Debugging only!
#include "headers/bios/biosrom.h" //BIOS ROM support!
#include "headers/hardware/vga/vga.h" //VGA information!
#include "headers/interrupts/interrupt10.h" //Basic function support!

uint_32 interrupt10_romfont_i;
OPTINLINE void MEM_BlockCopy(word segment, word offset, word fontseg, word fontoffs, Bitu height)
{
	uint_32 counter;
	byte data;
	for (counter=0;counter<height;counter++)
	{
		data = MMU_rb(-1,fontseg,fontoffs+counter,0,1); //Load the data!
		MMU_wb(-1,segment,offset+counter,data,1); //Write to VRAM!
	}
}

void phys_writeb(PhysPt ptr, byte val)
{
	if (!ptr) return;
	uint_32 RealPt = Phys2Real(ptr); //Convert to real pointer!
	mem_writeb(RealPt,val); //Write to memory normally!
}

void phys_writew(PhysPt ptr, word val)
{
	if (!ptr) return;
	uint_32 RealPt = Phys2Real(ptr); //Convert to real pointer!
	mem_writew(RealPt,val); //Write to memory normally!
}

void phys_writed(PhysPt ptr, uint_32 val)
{
	if (!ptr) return;
	uint_32 RealPt = Phys2Real(ptr); //Convert to real pointer!
	mem_writed(RealPt,val); //Write to memory normally!
}

byte phys_readb(PhysPt ptr)
{
	if (!ptr) return 0; //Invalid!
	uint_32 RealPt = Phys2Real(ptr); //Convert to real pointer!
	return mem_readb(RealPt); //Write to memory normally!
}

word phys_readw(PhysPt ptr)
{
	if (!ptr) return 0; //Invalid!
	uint_32 RealPt = Phys2Real(ptr); //Convert to real pointer!
	return mem_readw(RealPt); //Write to memory normally!
}

uint_32 phys_readd(PhysPt ptr)
{
	if (!ptr) return 0; //Invalid!
	uint_32 RealPt = Phys2Real(ptr); //Convert to real pointer!
	return mem_readd(RealPt); //Write to memory normally!
}

//Original function:
/*void RealSetVec(byte interrupt, RealPt ptr)
{
	CPU_setint(interrupt,(ptr>>16)&0xFFFF,ptr&0xFFFF); //Set the interrupt!
}*/

//Adjusted by superfury1
void RealSetVec(byte interrupt, word segment, word offset)
{
	CPU_setint(interrupt,segment,offset); //Set the interrupt!
}

static Bit8u static_functionality[0x10]=
{
 /* 0 */ 0xff,  // All modes supported #1
 /* 1 */ 0xff,  // All modes supported #2
 /* 2 */ 0x0f,  // All modes supported #3
 /* 3 */ 0x00, 0x00, 0x00, 0x00,  // reserved
 /* 7 */ 0x07,  // 200, 350, 400 scan lines
 /* 8 */ 0x04,  // total number of character blocks available in text modes
 /* 9 */ 0x02,  // maximum number of active character blocks in text modes
 /* a */ 0xff,  // Misc Flags Everthing supported 
 /* b */ 0x0e,  // Support for Display combination, intensity/blinking and video state saving/restoring
 /* c */ 0x00,  // reserved
 /* d */ 0x00,  // reserved
 /* e */ 0x00,  // Change to add new functions
 /* f */ 0x00   // reserved
};

static Bit16u map_offset[8]={
	0x0000,0x4000,0x8000,0xc000,
	0x2000,0x6000,0xa000,0xe000
};

void INT10_LoadFont(word fontseg, word fontoffs,bool reload,Bitu count,Bitu offset,Bitu map,Bitu height) {
	word ftwhere=0xa000;
	word ftoffs = map_offset[map & 0x7]+(Bit16u)(offset*32); //offset!
	
	//map mask register for writes
	IO_Write(0x3c4,0x2); //map mask register
	byte backup_3c4_2=IO_Read(0x3c5); //Save!
	IO_Write(0x3c4, 0x2); //map mask register
	IO_Write(0x3c5,0x4);	//Enable plane 2

	//sequencer memory mode register
	IO_Write(0x3c4,0x4); //Memory mode register!
	byte backup_3c4_4=IO_Read(0x3c5);
	IO_Write(0x3c4, 0x4); //Memory mode register!
	IO_Write(0x3c5,0x6); //Disable odd/even addressing
	
	//read map select register for reads
	IO_Write(0x3ce, 0x4);
	byte backup_3ce_4 = IO_Read(0x3cf);
	IO_Write(0x3ce, 0x4);
	IO_Write(0x3cf, 0x2); //Read map 2
						 
	//graphics mode register
	IO_Write(0x3ce,0x5);
	byte backup_3ce_5 = IO_Read(0x3cf);
	IO_Write(0x3ce,0x5);
	IO_Write(0x3cf,backup_3ce_5&~0x1B); //Clear bits 0-1, 3 and 5 to set the linear mode and read/write mode!

	//misc graphics register
	IO_Write(0x3ce, 0x6);
	byte backup_3ce_6 = IO_Read(0x3cf);
	IO_Write(0x3ce, 0x6);
	IO_Write(0x3cf, backup_3ce_6&~0xE); //Clear bits 1-3 to set the linear mode and read/write mode!

	for (interrupt10_romfont_i=0;interrupt10_romfont_i<count;interrupt10_romfont_i++) {
		MEM_BlockCopy(ftwhere,ftoffs,fontseg,fontoffs,height);
		ftoffs+=32; //Increment VRAM!
		fontoffs+=height; //Increment buffer!
	}

	//misc graphics register
	IO_Write(0x3ce, 0x6);
	IO_Write(0x3cf, backup_3ce_6); //Restore!

	//graphics mode register
	IO_Write(0x3ce, 0x5);
	IO_Write(0x3cf, backup_3ce_5); //Restore!

	//read map select register
	IO_Write(0x3ce, 0x4);
	IO_Write(0x3cf, (Bit8u)backup_3ce_4);	//odd/even and b8000 adressing

	//sequencer memory mode register
	IO_Write(0x3c4, 0x4); //Memory mode register!
	IO_Write(0x3c5,backup_3c4_4); //Restore!

	//map mask register
	IO_Write(0x3c4,0x2); //IO_Write(0x3c5,0x3);	//Enable textmode planes (0,1)
	IO_Write(0x3c5,backup_3c4_2); //Restore map mask register!

	/* Reload tables and registers with new values based on this height */
	if (reload) {
		//Max scanline 
		Bit16u base=real_readw(BIOSMEM_SEG,BIOSMEM_CRTC_ADDRESS);
		IO_Write(base,0x9);
		IO_Write(base+1,(IO_Read(base+1) & 0xe0)|(height-1));
		//Vertical display end bios says, but should stay the same?
		//Rows setting in bios segment
		real_writeb(BIOSMEM_SEG,BIOSMEM_NB_ROWS,(CurMode->sheight/height)-1);
		real_writeb(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT,(Bit8u)height);
		//TODO Reprogram cursor size?
	}
}

void INT10_ReloadFont() {
	switch(CurMode->cheight) {
	case 8:
		INT10_LoadFont(0xC000,int10.rom.font_8_first,true,256,0,0,8);
		break;
	case 14:
		INT10_LoadFont(0xC000,int10.rom.font_14,true,256,0,0,14);
		break;
	case 16:
	default:
		INT10_LoadFont(0xC000,int10.rom.font_16,true,256,0,0,16);
		break;
	}
} //Use the original font loader?

Int10Data int10; //Our interrupt data!

extern byte EMU_VGAROM[0x10000]; //Our VGA ROM!
extern byte EMU_BIOS[0x10000]; //Our BIOS!

void write_VGAd(uint_32 offset, uint_32 value)
{
	#include "headers/packed.h" //Packed!
	union PACKED
	{
		uint_32 packed32;
		struct
		{
			byte byte1;
			byte byte2;
			byte byte3;
			byte byte4;
		};
	} unpack32;
	#include "headers/endpacked.h" //End packed in little-endian!
	unpack32.packed32 = SDL_SwapLE32(value); //Load to unpack in little-endian format!
	EMU_VGAROM[offset++] = unpack32.byte1; //Low byte!
	EMU_VGAROM[offset++] = unpack32.byte2; //Higher byte!
	EMU_VGAROM[offset++] = unpack32.byte3; //Higher byte!
	EMU_VGAROM[offset] = unpack32.byte4; //Higher byte!
}

void INT10_SetupRomMemory(byte setinterrupts)
{
	//Addition by superfury: load as a ROM, instead of RAM!
	BIOS_load_VGAROM(); //Load our custom VGA ROM!

/* This should fill up certain structures inside the Video Bios Rom Area */
	word rom_base=0xC000;
	if (IS_EGAVGA_ARCH) {
		// set up the start of the ROM
		EMU_VGAROM[0] = 0x55;
		EMU_VGAROM[1] = 0xaa;
		EMU_VGAROM[2] = 0x80;		// Size of ROM: 128 512-blocks = 64KB
		if (IS_VGA_ARCH && (!getActiveVGA()->enable_SVGA)) { //Plain VGA?
			EMU_VGAROM[0x1e] = 0x49;	// IBM string
			EMU_VGAROM[0x1f] = 0x42;
			EMU_VGAROM[0x20] = 0x4d;
			EMU_VGAROM[0x21] = 0x00;
		}
		int10.rom.used=0x100; //Start of our data!
	}
	int10.rom.font_8_first=int10.rom.used;
	for (interrupt10_romfont_i=0;interrupt10_romfont_i<128*8;interrupt10_romfont_i++) {
		EMU_VGAROM[int10.rom.used++] = int10_font_08[interrupt10_romfont_i];
	}
	int10.rom.font_8_second=int10.rom.used;
	for (interrupt10_romfont_i=0;interrupt10_romfont_i<128*8;interrupt10_romfont_i++) {
		EMU_VGAROM[int10.rom.used++] = int10_font_08[interrupt10_romfont_i+128*8];
	}
	int10.rom.font_14=int10.rom.used;
	for (interrupt10_romfont_i=0;interrupt10_romfont_i<256*14;interrupt10_romfont_i++) {
		EMU_VGAROM[int10.rom.used++] = int10_font_14[interrupt10_romfont_i];
	}
	int10.rom.font_16=int10.rom.used;
	for (interrupt10_romfont_i=0;interrupt10_romfont_i<256*16;interrupt10_romfont_i++) {
		EMU_VGAROM[int10.rom.used++] = int10_font_16[interrupt10_romfont_i];
	}
	int10.rom.static_state=int10.rom.used;
	for (interrupt10_romfont_i=0;interrupt10_romfont_i<0x10;interrupt10_romfont_i++) {
		EMU_VGAROM[int10.rom.used++] = static_functionality[interrupt10_romfont_i];
	}
	for (interrupt10_romfont_i=0;interrupt10_romfont_i<128*8;interrupt10_romfont_i++) {
		EMU_BIOS[interrupt10_romfont_i+0xfa6e] = int10_font_08[interrupt10_romfont_i]; //Small ROM!
	}
	if (setinterrupts)
	{
		RealSetVec(0x1F,0xC000,int10.rom.font_8_second);
	}
	int10.rom.font_14_alternate=RealMake(0xC000,int10.rom.used);
	int10.rom.font_16_alternate=RealMake(0xC000,int10.rom.used);
	EMU_BIOS[int10.rom.used++] = 0x00;	// end of table (empty)

	if (IS_EGAVGA_ARCH) {
		int10.rom.video_parameter_table=RealMake(0xC000,int10.rom.used);
		int10.rom.used+=INT10_SetupVideoParameterTable(rom_base+int10.rom.used);

		if (IS_VGA_ARCH) {
			int10.rom.video_dcc_table=RealMake(0xC000,int10.rom.used);
			EMU_VGAROM[int10.rom.used++] = 0x10;	// number of entries
			EMU_VGAROM[int10.rom.used++] = 1;		// version number
			EMU_VGAROM[int10.rom.used++] = 8;		// maximal display code
			EMU_VGAROM[int10.rom.used++] = 0;		// reserved
			// display combination codes
			write_VGAw(int10.rom.used,0x0000);	int10.rom.used+=2;
			write_VGAw(int10.rom.used,0x0100);	int10.rom.used+=2;
			write_VGAw(int10.rom.used,0x0200);	int10.rom.used+=2;
			write_VGAw(int10.rom.used,0x0102);	int10.rom.used+=2;
			write_VGAw(int10.rom.used,0x0400);	int10.rom.used+=2;
			write_VGAw(int10.rom.used,0x0104);	int10.rom.used+=2;
			write_VGAw(int10.rom.used,0x0500);	int10.rom.used+=2;
			write_VGAw(int10.rom.used,0x0502);	int10.rom.used+=2;
			write_VGAw(int10.rom.used,0x0600);	int10.rom.used+=2;
			write_VGAw(int10.rom.used,0x0601);	int10.rom.used+=2;
			write_VGAw(int10.rom.used,0x0605);	int10.rom.used+=2;
			write_VGAw(int10.rom.used,0x0800);	int10.rom.used+=2;
			write_VGAw(int10.rom.used,0x0801);	int10.rom.used+=2;
			write_VGAw(int10.rom.used,0x0700);	int10.rom.used+=2;
			write_VGAw(int10.rom.used,0x0702);	int10.rom.used+=2;
			write_VGAw(rom_base+int10.rom.used,0x0706);	int10.rom.used+=2;

			int10.rom.video_save_pointer_table=RealMake(0xC000,int10.rom.used);
			write_VGAw(int10.rom.used,0x1a);	// length of table
			int10.rom.used+=2;
			write_VGAd(int10.rom.used,int10.rom.video_dcc_table);
			int10.rom.used+=4;
			write_VGAd(int10.rom.used,0);		// alphanumeric charset override
			int10.rom.used+=4;
			write_VGAd(int10.rom.used,0);		// user palette table
			int10.rom.used+=4;
			write_VGAd(int10.rom.used,0);		int10.rom.used+=4;
			write_VGAd(int10.rom.used,0);		int10.rom.used+=4;
			write_VGAd(int10.rom.used,0);		int10.rom.used+=4;
		}

		int10.rom.video_save_pointers=RealMake(0xC000,int10.rom.used);
		write_VGAd(int10.rom.used,int10.rom.video_parameter_table);
		int10.rom.used+=4;
		write_VGAd(int10.rom.used,0);		// dynamic save area pointer
		int10.rom.used+=4;
		write_VGAd(int10.rom.used,0);		// alphanumeric character set override
		int10.rom.used+=4;
		write_VGAd(int10.rom.used,0);		// graphics character set override
		int10.rom.used+=4;
		if (IS_VGA_ARCH) {
			write_VGAd(int10.rom.used,int10.rom.video_save_pointer_table);
		} else {
			write_VGAd(int10.rom.used,0);		// secondary save pointer table
		}
		int10.rom.used+=4;
		write_VGAd(int10.rom.used,0);		int10.rom.used+=4;
		write_VGAd(int10.rom.used,0);		int10.rom.used+=4;
	}

	INT10_SetupBasicVideoParameterTable();
}

void INT10_ReloadRomFonts() {
	//Superfury: It's an actual ROM in UniPCemu, don't reload, as this cannot be changed by software!
	//This isn't needed: it's stored in a ROM!
}

void INT10_SetupRomMemoryChecksum() {
	if (IS_EGAVGA_ARCH) { //EGA/VGA. Just to be safe
		/* Sum of all bytes in rom module 256 should be 0 */
		Bit8u sum = 0;
		Bitu last_rombyte = 64*1024 - 1;		//64 KB romsize(32KB isn't enough!)
		for (interrupt10_romfont_i = 0;interrupt10_romfont_i < last_rombyte;interrupt10_romfont_i++)
			sum += (Bit8u)EMU_VGAROM[interrupt10_romfont_i];	//OVERFLOW IS OKAY
		sum = (Bit8u)((256 - (Bitu)sum)&0xff);
		EMU_VGAROM[last_rombyte] = sum;
	}
}
