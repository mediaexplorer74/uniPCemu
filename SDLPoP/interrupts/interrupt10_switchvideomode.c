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

#include "headers/types.h" //Basic types!
#include "headers/cpu/interrupts.h" //Interrupt support for GRAPHIC modes!
#include "headers/hardware/vga/vga.h" //Basic typedefs!
#include "headers/header_dosbox.h" //Screen modes from DOSBox!
#include "headers/hardware/ports.h" //Port support!
#include "headers/cpu/mmu.h" //MMU access!
#include "headers/support/log.h" //Logging support for debugging!
#include "headers/interrupts/interrupt10.h" //Our typedefs!
#include "headers/cpu/cb_manager.h" //Callback support!
#include "headers/cpu/protection.h" //Protection support!
#include "headers/hardware/vga/svga/tseng.h" //ET3000/ET4000 support!

//Log options!
#define LOG_SWITCHMODE 0
#define LOG_FILE "int10"

//Are we disabled?
#define __HW_DISABLED 0

//Helper functions:
extern VideoModeBlock ModeList_VGA_Tseng[40]; //ET3000/ET4000 mode list!
extern VideoModeBlock ModeList_VGA[0x15]; //VGA Modelist!
VideoModeBlock *CurMode = &ModeList_VGA[0]; //Current video mode information block!

//Patches for dosbox!

//EGA/VGA?

extern SVGAmode svgaCard; //SVGA card that's emulated?

OPTINLINE void INT10_SetSingleDACRegister(Bit8u index,Bit8u red,Bit8u green,Bit8u blue) {
	IO_Write(VGAREG_DAC_WRITE_ADDRESS,(Bit8u)index);
	if ((real_readb(BIOSMEM_SEG,BIOSMEM_MODESET_CTL)&0x06)==0) {
		IO_Write(VGAREG_DAC_DATA,red);
		IO_Write(VGAREG_DAC_DATA,green);
		IO_Write(VGAREG_DAC_DATA,blue);
	} else {
		/* calculate clamped intensity, taken from VGABIOS */
		Bit32u i=(( 77*red + 151*green + 28*blue ) + 0x80) >> 8;
		Bit8u ic=(i>0x3f) ? 0x3f : ((Bit8u)(i & 0xff));
		IO_Write(VGAREG_DAC_DATA,ic);
		IO_Write(VGAREG_DAC_DATA,ic);
		IO_Write(VGAREG_DAC_DATA,ic);
	}
}

void INT10_PerformGrayScaleSumming(Bit16u start_reg,Bit16u count) { //Creates a grayscale palette!
    Bitu ct;
	Bit8u red, green, blue, ic;
	Bit32u i;
	if (count>0x100) count=0x100;
	for (ct=0; ct<count; ct++) {
		IO_Write(VGAREG_DAC_READ_ADDRESS,start_reg+ct);
		red=IO_Read(VGAREG_DAC_DATA);
		green=IO_Read(VGAREG_DAC_DATA);
		blue=IO_Read(VGAREG_DAC_DATA);

		/* calculate clamped intensity, taken from VGABIOS */
		i=(( 77*red + 151*green + 28*blue ) + 0x80) >> 8;
		ic=(i>0x3f) ? 0x3f : ((Bit8u)(i & 0xff));
		INT10_SetSingleDACRegister(start_reg+ct,ic,ic,ic);
	}
}

//ET4K support
extern float ET4K_clockFreq[0x10]; //Clock frequencies used!
extern float ET3K_clockFreq[0x10]; //Clock frequencies used!

extern float VGA_clocks[4]; //VGA clock frequencies!

Bitu VideoModeMemSize(Bitu mode) {
	if (!IS_VGA_ARCH)
		return 0;

	VideoModeBlock* modelist = NULL;

	switch (svgaCard) {
	case SVGA_TsengET4K:
	case SVGA_TsengET3K:
		modelist = ModeList_VGA_Tseng;
		break;
	default:
		modelist = ModeList_VGA;
		break;
	}

	VideoModeBlock* vmodeBlock = NULL;
	Bitu i = 0;
	while (modelist[i].mode != 0xffff) {
		if (modelist[i].mode == mode) {
			vmodeBlock = &modelist[i];
			break;
		}
		i++;
	}
	if (!vmodeBlock)
		return 0;

	switch (vmodeBlock->type) {
	case M_LIN4:
		return vmodeBlock->swidth*vmodeBlock->sheight / 2;
	case M_LIN8:
		return vmodeBlock->swidth*vmodeBlock->sheight;
	case M_LIN15: case M_LIN16:
		return vmodeBlock->swidth*vmodeBlock->sheight * 2;
	case M_LIN32:
		return vmodeBlock->swidth*vmodeBlock->sheight * 4;
	case M_TEXT:
		return vmodeBlock->twidth*vmodeBlock->theight * 2;
	default:
		break;
	}
	// Return 0 for all other types, those always fit in memory
	return 0;
}

bool AcceptsMode_ET4K(Bitu mode) {
	return VideoModeMemSize(mode) < getActiveVGA()->VRAM_size;
}

void FinishSetMode_ET4K(Bitu crtc_base, VGA_ModeExtraData* modeData) {
	//Setup the Hi-color DAC!
	IO_Read(0x3C6);
	IO_Read(0x3C6);
	IO_Read(0x3C6);
	IO_Read(0x3C6);
	if (modeData->modeNo & 0x200)
	{
		IO_Write(0x3C6, 0x80); //Hi-color mode!
	}
	else
	{
		IO_Write(0x3C6, 0x00); //VGA-compatible mode!
	}
	IO_Read(0x3C7); //Return to the old register!

	IO_Write(0x3cd, 0x00); // both banks to 0

	// Reinterpret hor_overflow. Curiously, three bits out of four are
	// in the same places. Input has hdispend (not supported), output
	// has CRTC offset (also not supported)
	Bit8u et4k_hor_overflow = 
		(modeData->hor_overflow & 0x01) |
		(modeData->hor_overflow & 0x04) |
		(modeData->hor_overflow & 0x10);
	IO_Write(crtc_base,0x3f);IO_Write(crtc_base+1,et4k_hor_overflow);

	// Reinterpret ver_overflow
	Bit8u et4k_ver_overflow =
		((modeData->ver_overflow & 0x01) << 1) | // vtotal10
		((modeData->ver_overflow & 0x02) << 1) | // vdispend10
		((modeData->ver_overflow & 0x04) >> 2) | // vbstart10
		((modeData->ver_overflow & 0x10) >> 1) | // vretrace10 (tseng has vsync start?)
		((modeData->ver_overflow & 0x40) >> 2);  // line_compare
	IO_Write(crtc_base,0x35);IO_Write(crtc_base+1,et4k_ver_overflow);

	// Clear remaining ext CRTC registers
	IO_Write(crtc_base,0x31);IO_Write(crtc_base+1,0);
	IO_Write(crtc_base,0x32);IO_Write(crtc_base+1,0);
	IO_Write(crtc_base,0x33);IO_Write(crtc_base+1,0);
	IO_Write(crtc_base,0x34);IO_Write(crtc_base+1,0);
	IO_Write(crtc_base,0x36);IO_Write(crtc_base+1,0);
	IO_Write(crtc_base,0x37);IO_Write(crtc_base+1,0x0c|(getActiveVGA()->VRAM_size==1024*1024?3:getActiveVGA()->VRAM_size==512*1024?2:1));
	// Clear ext SEQ
	IO_Write(0x3c4,0x06);IO_Write(0x3c5,0);
	IO_Write(0x3c4,0x07);IO_Write(0x3c5,0);
	// Clear ext ATTR
	IO_Write(0x3c0,0x16);IO_Write(0x3c0,0);
	IO_Write(0x3c0,0x17);IO_Write(0x3c0,0);

	// Select SVGA clock to get close to 60Hz (not particularly clean implementation)
	if (modeData->modeNo > 0x13) {
		Bitu target = modeData->vtotal*8*modeData->htotal*60;
		Bitu best = 1;
		Bits dist = 100000000;
		float freq;
		Bitu i;
		for (i=0; i<16; i++) {
			freq = ET4K_clockFreq[i]; //ET4K clock frequency!
			if (freq < 0.0f) freq = VGA_clocks[i&3]; //Use VGA clock!
			Bits cdiff = abs((Bits)(target - freq));
			if (cdiff < dist) {
				best = i;
				dist = cdiff;
			}
		}
		set_clock_index_et4k(getActiveVGA(),best);
	}

	// Verified (on real hardware and in a few games): Tseng ET4000 used chain4 implementation
	// different from standard VGA. It was also not limited to 64K in regular mode 13h.
}

//ET3K support!
bool AcceptsMode_ET3K(Bitu mode) {
	return (((mode <= 0x37) && (mode != 0x2f)) || ((mode>=0x200) && (mode<=0x237) && (mode!=0x22f))) && VideoModeMemSize(mode) < getActiveVGA()->VRAM_size;
}

void FinishSetMode_ET3K(Bitu crtc_base, VGA_ModeExtraData* modeData) {
	Bitu i;

	//Setup the Hi-color DAC!
	IO_Read(0x3C6);
	IO_Read(0x3C6);
	IO_Read(0x3C6);
	IO_Read(0x3C6);
	if (modeData->modeNo & 0x200)
	{
		IO_Write(0x3C6, 0x80); //Hi-color mode!
	}
	else
	{
		IO_Write(0x3C6, 0x00); //VGA-compatible mode!
	}
	IO_Read(0x3C7); //Return to the old register!

	IO_Write(0x3cd, 0x40); // both banks to 0, 64K bank size

	// Tseng ET3K does not have horizontal overflow bits
	// Reinterpret ver_overflow
	Bit8u et4k_ver_overflow =
		((modeData->ver_overflow & 0x01) << 1) | // vtotal10
		((modeData->ver_overflow & 0x02) << 1) | // vdispend10
		((modeData->ver_overflow & 0x04) >> 2) | // vbstart10
		((modeData->ver_overflow & 0x10) >> 1) | // vretrace10 (tseng has vsync start?)
		((modeData->ver_overflow & 0x40) >> 2);  // line_compare
	IO_Write(crtc_base, 0x25);IO_Write(crtc_base + 1, et4k_ver_overflow);

	// Clear remaining ext CRTC registers
	for (i = 0x16; i <= 0x21; i++)
	{
		IO_Write(crtc_base, i); IO_Write(crtc_base + 1, 0);
	}
	IO_Write(crtc_base, 0x23);IO_Write(crtc_base + 1, 0);
	IO_Write(crtc_base, 0x24);IO_Write(crtc_base + 1, 0);
	// Clear ext SEQ
	IO_Write(0x3c4, 0x06);IO_Write(0x3c5, 0);
	IO_Write(0x3c4, 0x07);IO_Write(0x3c5, 0x40); // 0 in this register breaks WHATVGA
												 // Clear ext ATTR
	IO_Write(0x3c0, 0x16);IO_Write(0x3c0, 0);
	IO_Write(0x3c0, 0x17);IO_Write(0x3c0, 0);

	// Select SVGA clock to get close to 60Hz (not particularly clean implementation)
	if (modeData->modeNo > 0x13) {
		Bitu target = modeData->vtotal * 8 * modeData->htotal * 60;
		Bitu best = 1;
		Bits dist = 100000000;
		float freq;
		for (i = 0; i<8; i++) {
			freq = ET3K_clockFreq[i];
			if (freq < 0.0f) freq = VGA_clocks[i&3]; //Use VGA clock!
			Bits cdiff = abs((Bits)(target - freq));
			if (cdiff < dist) {
				best = i;
				dist = cdiff;
			}
		}
		set_clock_index_et3k(getActiveVGA(),best);
	}

	// Verified on functioning (at last!) hardware: Tseng ET3000 is the same as ET4000 when
	// it comes to chain4 architecture
}


//All palettes:
extern byte text_palette[64][3];
extern byte mtext_palette[64][3];
extern byte mtext_s3_palette[64][3];
extern byte ega_palette[64][3];
extern byte cga_palette[16][3];
extern byte cga_palette_2[64][3];
extern byte vga_palette[256][3];

extern VideoModeBlock ModeList_VGA_Text_200lines[5];
extern VideoModeBlock ModeList_VGA_Text_350lines[5];

//Now the function itself (a big one)!
/* Setup the BIOS */

OPTINLINE bool SetCurMode(VideoModeBlock modeblock[],word mode)
{
	byte i=0;
	while (modeblock[i].mode!=0xffff)
	{
		if (modeblock[i].mode!=mode) i++;
		else
		{
			if ((modeblock[i].mode<0x120))
			{
				CurMode=&modeblock[i];
				return true;
			}
			return false;
		}
	}
	return false;
}

OPTINLINE void RealSetVec2(byte interrupt, word segment, word offset)
{
	CPU_setint(interrupt, segment, offset); //Set the interrupt!
}

OPTINLINE void FinishSetMode(int clearmem)
{
	VGA_Type *currentVGA;
	byte ct;
	uint_32 ct2;
	word seg;
	word CRTCAddr;
	/* Clear video memory if needs be */
	if (clearmem)
	{
		switch (CurMode->type)
		{
		case M_CGA4:
			goto cgacorruption; //Corruption bugfix: the high planes need to be cleared too, not only the low planes when coming !
		case M_CGA2:
			for (ct2=0;ct2<16*1024;ct2++) {
				memreal_writew( 0xb800,ct2<<1,0x0000);
			}
			break;
		case M_TEXT:
			seg = (CurMode->mode==7)?0xb000:0xb800;
			for (ct2=0;ct2<16*1024;ct2++) memreal_writew(seg,ct2<<1,0x0720);
			break;
		case M_EGA:
		case M_VGA:
		case M_LIN4:
		case M_LIN8:
		case M_LIN15:
		case M_LIN16:
			cgacorruption:
			/* Hack we just acess the memory directly */
			if ((currentVGA = getActiveVGA())) //Gotten active VGA?
			{
				memset(currentVGA->VRAM,0,currentVGA->VRAM_size);
			}
			break;
		default: //Unknown?
			break;
		}
	}


	if (CurMode->mode<128) MMU_wb(-1,BIOSMEM_SEG,BIOSMEM_CURRENT_MODE,(byte)CurMode->mode,1);
	else MMU_wb(-1,BIOSMEM_SEG,BIOSMEM_CURRENT_MODE,(byte)(CurMode->mode-0x98),1); //Looks like the s3 bios
	MMU_wb(-1,BIOSMEM_SEG,BIOSMEM_NB_COLS,CurMode->twidth&0xFF,1); //Low!
	MMU_wb(-1,BIOSMEM_SEG,BIOSMEM_NB_COLS+1,((CurMode->twidth&0xFF00)>>8),1); //High!
	MMU_wb(-1,BIOSMEM_SEG,BIOSMEM_PAGE_SIZE,(CurMode->plength&0xFF),1); //Low!
	MMU_wb(-1,BIOSMEM_SEG,BIOSMEM_PAGE_SIZE+1,((CurMode->plength&0xFF00)>>8),1); //High!
	CRTCAddr = ((CurMode->mode==7 )|| (CurMode->mode==0x0f)) ? 0x3b4 : 0x3d4; //Address!
	MMU_wb(-1,BIOSMEM_SEG,BIOSMEM_CRTC_ADDRESS,(CRTCAddr&0xFF),1); //Low!
	MMU_wb(-1,BIOSMEM_SEG,BIOSMEM_CRTC_ADDRESS+1,((CRTCAddr&0xFF00)>>8),1); //High!
	MMU_wb(-1,BIOSMEM_SEG,BIOSMEM_NB_ROWS,(byte)(CurMode->theight-1),1); //Height!
	MMU_wb(-1,BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT,(CurMode->cheight&0xFF),1); //Low!
	MMU_wb(-1,BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT+1,((CurMode->cheight&0xFF00)>>8),1); //High!
	MMU_wb(-1,BIOSMEM_SEG,BIOSMEM_VIDEO_CTL,(0x60|(((MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURRENT_MODE,0,1)&0x80) && EMU_VGA)?0:0x80)),1);
	MMU_wb(-1,BIOSMEM_SEG,BIOSMEM_SWITCHES,0x09,1);


	// this is an index into the dcc table:
	if (IS_VGA_ARCH) MMU_wb(-1,BIOSMEM_SEG,BIOSMEM_DCC_INDEX,0x0b,1);
	//MMU_wd(BIOSMEM_SEG,BIOSMEM_VS_POINTER,int10.rom.video_save_pointers); //Unknown/unimplemented yet!

	// Set cursor shape
	if (CurMode->type==M_TEXT)
	{
		EMU_CPU_setCursorScanlines(0x06,0x07);
	}

	// Set cursor pos for page 0..7
	for (ct=0; ct<8; ct++) cursorXY(ct,0,0);
	// Set active page 0
	emu_setactivedisplaypage(0);

	/* Set some interrupt vectors */
	switch (CurMode->cheight) {
	case 8:RealSetVec2(0x43,0xC000, int10.rom.font_8_first);break;
	case 14:RealSetVec2(0x43,0xC000, int10.rom.font_14);break;
	case 16:RealSetVec2(0x43,0xC000, int10.rom.font_16);break;
	default:
		break;
	}
}

int INT10_Internal_SetVideoMode(word mode)
{
	byte ct;
	bool clearmem=true;
	uint_32 i;
	byte modeset_ctl;
	word crtc_base;
	bool mono_mode=0;
	byte misc_output;
	byte seq_data[SEQ_REGS];
	byte overflow=0;
	byte max_scanline=0;
	byte ver_overflow=0;
	byte hor_overflow=0;
	byte ret_start;
	byte ret_end;
	word vretrace;
	byte vblank_trim;
	byte underline=0;
	byte offset;
	byte mode_control=0;
	byte gfx_data[GFX_REGS];
	byte att_data[ATT_REGS];
	byte feature;

	mono_mode = ((mode == 7) || (mode==0xf)); //Are we in mono mode?

	if (__HW_DISABLED) return true; //Abort!

	if ((mode<0x100) && (mode & 0x80))
	{
		clearmem=false;
		mode-=0x80;
	}
	else if ((mode >= 0x300) || ((mode>=0x100)&&(mode<0x200))) //High mode = not supported!
	{
		return false; //Error: unsupported mode!
	}

	modeset_ctl=real_readb(BIOSMEM_SEG,BIOSMEM_MODESET_CTL);
	if (IS_VGA_ARCH) {

		switch(svgaCard) {
		case SVGA_TsengET4K: //ET4000?
			if (!AcceptsMode_ET4K(mode)) return false; //Not accepted!
			goto setTsengMode;
		case SVGA_TsengET3K: //ET3000?
			if (!AcceptsMode_ET3K(mode)) return false; //Not accepted!
			setTsengMode: //Added by superfury for easy support!
			if (!SetCurMode(ModeList_VGA_Tseng,mode)){
				return false;
			}
			break;
		default:
			if (!SetCurMode(ModeList_VGA,mode)){
				return false;
			}
		}
		// check for scanline backwards compatibility (VESA text modes??)
		if (CurMode->type==M_TEXT)
		{
			if ((modeset_ctl&0x90)==0x80)   // 200 lines emulation
			{
				if (CurMode->mode <= 3)
				{
					CurMode = &ModeList_VGA_Text_200lines[CurMode->mode];
				}
			}
			else if ((modeset_ctl&0x90)==0x00)     // 350 lines emulation
			{
				if (CurMode->mode <= 3)
				{
					CurMode = &ModeList_VGA_Text_350lines[CurMode->mode];
				}
			}
		}
	} else {
		return false; //Superfury: EGA isn't supported here!
	}

	/* Setup the VGA to the correct mode */
	// turn off video
	IO_Write(0x3c4, 0); IO_Write(0x3c5, 1); // reset
	IO_Write(0x3c4, 1); IO_Write(0x3c5, 0x20); // screen off
	if (mono_mode) crtc_base=0x3b4;
	else crtc_base=0x3d4;

	/* Setup MISC Output Register */
	misc_output=0x2 | (mono_mode ? 0x0 : 0x1);

	if ((CurMode->type==M_TEXT) && (CurMode->cwidth==9))
	{
		// 28MHz (16MHz EGA) clock for 9-pixel wide chars
		misc_output|=0x4;
	}

	switch (CurMode->vdispend)
	{
	case 400:
		misc_output|=0x60;
		break;
	case 480:
		misc_output|=0xe0;
		break;
	case 350:
		misc_output|=0xa0;
		break;
	default:
		misc_output|=0x20; //According to Dosbox-X
	}

	IO_Write(0x3c2,misc_output);		//Setup for 3b4 or 3d4

	/* Program Sequencer */
	memset(&seq_data,0,SEQ_REGS);

	seq_data[0] = 0x3;	// not reset
	seq_data[1] = 0x21;	// screen still disabled, will be enabled at end of setmode
	seq_data[4] = 0x04;	// odd/even disable

	if (CurMode->special & _EGA_HALF_CLOCK) seq_data[1] |= 0x08; //Check for half clock
	if ((machine == MCH_EGA) && (CurMode->special & _EGA_HALF_CLOCK)) seq_data[1] |= 0x02;
	seq_data[4] |= 0x02;	//More than 64kb
	switch (CurMode->type) {
	case M_TEXT:
		if (CurMode->cwidth == 9) seq_data[1] &= ~1;
		seq_data[2] |= 0x3;				//Enable plane 0 and 1
		seq_data[4] |= 0x01;				//Alpanumeric
		seq_data[4] &= ~0x04;				//odd/even enable
		break;
	case M_CGA2:
		if (IS_EGAVGA_ARCH) {
			seq_data[2] |= 0x1;			//Enable plane 0. Most VGA cards treat it as a 640x200 variant of the MCGA 2-color mode, with bit 13 remapped for interlace
		}
		break;
	case M_CGA4:
		if (IS_EGAVGA_ARCH) {
			seq_data[2] |= 0x3;			//Enable plane 0 and 1
			seq_data[4] &= ~0x04;			//odd/even enable
		}
		break;
	case M_LIN4:
	case M_EGA:
		seq_data[2] |= 0xf;				//Enable all planes for writing
		break;
	case M_LIN8:						//Seems to have the same reg layout from testing
	case M_LIN15:
	case M_LIN16:
	case M_LIN32:
	case M_VGA:
		seq_data[2] |= 0xf;				//Enable all planes for writing
		seq_data[4] |= 0x8;				//Graphics - Chained
		break;
	default:
		break;
	}
	for (ct=0; ct<SEQ_REGS; ct++)
	{
		IO_Write(0x3c4,ct);
		IO_Write(0x3c5,seq_data[ct]);
	}

	/* Program CRTC */
	/* First disable write protection */
	IO_Write(crtc_base,0x11);
	IO_Write(crtc_base+1,IO_Read(crtc_base+1)&0x7f);
	/* Clear all the regs */
	for (ct=0x0; ct<=0x18; ct++)
	{
		IO_Write(crtc_base,ct);
		IO_Write(crtc_base+1,0);
	}
	/* Horizontal Total */
	IO_Write(crtc_base,0x00);
	IO_Write(crtc_base+1,(byte)(CurMode->htotal-5));
	hor_overflow|=((CurMode->htotal-5) & 0x100) >> 8;
	/* Horizontal Display End */
	IO_Write(crtc_base,0x01);
	IO_Write(crtc_base+1,(byte)(CurMode->hdispend-1));
	hor_overflow|=((CurMode->hdispend-1) & 0x100) >> 7;
	/* Start horizontal Blanking */
	IO_Write(crtc_base,0x02);
	IO_Write(crtc_base+1,(byte)CurMode->hdispend);
	hor_overflow|=((CurMode->hdispend) & 0x100) >> 6;
	/* End horizontal Blanking */
	byte blank_end=(CurMode->htotal-2) & 0x7f;
	IO_Write(crtc_base,0x03);
	IO_Write(crtc_base+1,0x80|(blank_end & 0x1f));

	/* Start Horizontal Retrace */
	if ((CurMode->special & _EGA_HALF_CLOCK) && (CurMode->type!=M_CGA2)) ret_start = (CurMode->hdispend+3);
	else if (CurMode->type==M_TEXT) ret_start = (CurMode->hdispend+5);
	else ret_start = (CurMode->hdispend+4);
	IO_Write(crtc_base,0x04);
	IO_Write(crtc_base+1,(byte)ret_start);
	hor_overflow|=(ret_start & 0x100) >> 4;

	/* End Horizontal Retrace */
	if (CurMode->special & _EGA_HALF_CLOCK)
	{
		if (CurMode->type==M_CGA2) ret_end=0;	// mode 6
		else if (CurMode->special & _DOUBLESCAN) ret_end = (CurMode->htotal-18) & 0x1f;
		else ret_end = ((CurMode->htotal-18) & 0x1f) | 0x20; // mode 0&1 have 1 char sync delay
	}
	else if (CurMode->type==M_TEXT) ret_end = (CurMode->htotal-3) & 0x1f;
	else ret_end = (CurMode->htotal-4) & 0x1f;

	IO_Write(crtc_base,0x05);
	IO_Write(crtc_base+1,(byte)(ret_end | (blank_end & 0x20) << 2));

	/* Vertical Total */
	IO_Write(crtc_base,0x06);
	IO_Write(crtc_base+1,(byte)(CurMode->vtotal-2));
	overflow|=((CurMode->vtotal-2) & 0x100) >> 8;
	overflow|=((CurMode->vtotal-2) & 0x200) >> 4;
	ver_overflow|=((CurMode->vtotal-2) & 0x400) >> 10;

	switch (CurMode->vdispend)
	{
	case 400:
		vretrace=CurMode->vdispend+12;
		break;
	case 480:
		vretrace=CurMode->vdispend+10;
		break;
	case 350:
		vretrace=CurMode->vdispend+37;
		break;
	default:
		vretrace=CurMode->vdispend+12;
	}

	/* Vertical Retrace Start */
	IO_Write(crtc_base,0x10);
	IO_Write(crtc_base+1,(byte)vretrace);
	overflow|=(vretrace & 0x100) >> 6;
	overflow|=(vretrace & 0x200) >> 2;
	ver_overflow|=(vretrace & 0x400) >> 6;

	/* Vertical Retrace End */
	IO_Write(crtc_base,0x11);
	IO_Write(crtc_base+1,(vretrace+2) & 0xF);

	/* Vertical Display End */
	IO_Write(crtc_base,0x12);
	IO_Write(crtc_base+1,(byte)(CurMode->vdispend-1));
	overflow|=((CurMode->vdispend-1) & 0x100) >> 7;
	overflow|=((CurMode->vdispend-1) & 0x200) >> 3;
	ver_overflow|=((CurMode->vdispend-1) & 0x400) >> 9;

	if (IS_VGA_ARCH)
	{
		switch (CurMode->vdispend)
		{
		case 400:
			vblank_trim=6;
			break;
		case 480:
			vblank_trim=7;
			break;
		case 350:
			vblank_trim=5;
			break;
		default:
			vblank_trim=8;
		}
	}
	else
	{
		switch (CurMode->vdispend)
		{
		case 350:
			vblank_trim=0;
			break;
		default:
			vblank_trim=23;
		}
	}

	/* Vertical Blank Start */
	IO_Write(crtc_base,0x15);
	IO_Write(crtc_base+1,(byte)(CurMode->vdispend+vblank_trim));
	overflow|=((CurMode->vdispend+vblank_trim) & 0x100) >> 5;
	max_scanline|=((CurMode->vdispend+vblank_trim) & 0x200) >> 4;
	ver_overflow|=((CurMode->vdispend+vblank_trim) & 0x400) >> 8;

	/* Vertical Blank End */
	IO_Write(crtc_base,0x16);
	IO_Write(crtc_base+1,(byte)(CurMode->vtotal-vblank_trim-2));

	/* Line Compare */
	word line_compare=(CurMode->vtotal < 1024) ? 1023 : 2047;
	IO_Write(crtc_base,0x18);
	IO_Write(crtc_base+1,line_compare&0xff);
	overflow|=(line_compare & 0x100) >> 4;
	max_scanline|=(line_compare & 0x200) >> 3;
	ver_overflow|=(line_compare & 0x400) >> 4;
	/* Maximum scanline / Underline Location */
	if (CurMode->special & _DOUBLESCAN) max_scanline|=0x80;
	if (CurMode->special & _REPEAT1) max_scanline |= 0x01;
	switch (CurMode->type)
	{
	case M_TEXT:
		switch (modeset_ctl & 0x90) {
		case 0x0: // 350-lines mode: 8x14 font
			max_scanline |= (14 - 1);
			break;
		default: // reserved
		case 0x10: // 400 lines 8x16 font
			max_scanline |= CurMode->cheight - 1;
			break;
		case 0x80: // 200 lines: 8x8 font and doublescan
			max_scanline |= (8 - 1);
			max_scanline |= 0x80;
			break;
		}
		underline = mono_mode ? 0x0f : 0x1f; // mode 7 uses a diff underline position
		break;
	case M_VGA:
		underline=0x40;
		break;
	case M_CGA2:
	case M_CGA4:
		max_scanline |= 1;
		break;
	case M_LIN8:
	case M_LIN15:
	case M_LIN16:
	case M_LIN32:
		underline = 0x40; //Seems to enable every 4th clock on S3? Superfury: This shouldn't be the case on the ET4000!
		break;
	default:
		break;
	}
	if (CurMode->vdispend==350) underline=0x0f;

	IO_Write(crtc_base,0x09);
	IO_Write(crtc_base+1,max_scanline);
	IO_Write(crtc_base,0x14);
	IO_Write(crtc_base+1,underline);

	/* OverFlow */
	IO_Write(crtc_base,0x07);
	IO_Write(crtc_base+1,overflow);

	/* Offset Register */
	switch (CurMode->type) {
		case M_LIN8:
			offset = CurMode->swidth/8;
			break;
		case M_LIN15:
		case M_LIN16:
			offset = 2 * CurMode->swidth/8;
			break;
		case M_LIN32:
			offset = 4 * CurMode->swidth/8;
			break;
		default: //VGA compatibility?
			offset = CurMode->hdispend/2;
			break;
	}
	IO_Write(crtc_base,0x13);
	IO_Write(crtc_base + 1,offset & 0xff);

	/* Mode Control */

	switch (CurMode->type)
	{
	case M_CGA2:
		mode_control=0xc2; // 0x06 sets address wrap.
		break;
	case M_CGA4:
		mode_control=0xa2;
		break;
	case M_EGA:
		if (CurMode->mode==0x11) // 0x11 also sets address wrap.  thought maybe all 2 color modes did but 0x0f doesn't.
			mode_control=0xc3; // so.. 0x11 or 0x0f a one off?
		else
		{
			if (machine==MCH_EGA)
			{
				mode_control = 0xe3;
			}
			else
			{
				mode_control=0xe3;
			}
		}
		break;
	case M_TEXT:
	case M_VGA:
	case M_LIN8:
	case M_LIN15:
	case M_LIN16:
	case M_LIN32:
		mode_control=0xa3;
		if (CurMode->special & _VGA_PIXEL_DOUBLE)
			mode_control |= 0x08;
		break;
	default:
		break;
	}

	IO_Write(crtc_base,0x17);
	IO_Write(crtc_base+1,mode_control);
	/* Renable write protection */
	IO_Write(crtc_base,0x11);
	IO_Write(crtc_base+1,IO_Read(crtc_base+1)|0x80);

	/* Write Misc Output */
	IO_Write(0x3c2,misc_output);

	/* Program Graphics controller */
	memset(&gfx_data,0,GFX_REGS);
	gfx_data[0x7]=0xf;				/* Color don't care */
	gfx_data[0x8]=0xff;				/* BitMask */
	switch (CurMode->type)
	{
	case M_TEXT:
		gfx_data[0x5]|=0x10;		//Odd-Even Mode
		gfx_data[0x6]|=mono_mode ? 0x0a : 0x0e;		//Either b800 or b000
		break;
	case M_LIN8:
	case M_LIN15:
	case M_LIN16:
	case M_LIN32:
	case M_VGA:
		gfx_data[0x5]|=0x40;		//256 color mode
		gfx_data[0x6]|=0x05;		//graphics mode at 0xa000-affff
		break;
	case M_EGA:
		if (CurMode->mode == 0x0f)
			gfx_data[0x7] = 0x05;		// only planes 0 and 2 are used
		gfx_data[0x6]|=0x05;		//graphics mode at 0xa000-affff
		break;
	case M_CGA4:
		gfx_data[0x5]|=0x20;		//CGA mode
		gfx_data[0x6]|=0x0f;		//graphics mode at at 0xb800=0xbfff
		if (IS_EGAVGA_ARCH) gfx_data[0x5] |= 0x10;
		break;
	case M_CGA2:
		gfx_data[0x6] |= 0x0d;		//graphics mode at at 0xb800=0xbfff, chain odd/even disabled
		break;
	default:
		break;
	}
	for (ct=0; ct<GFX_REGS; ct++)
	{
		IO_Write(0x3ce,ct);
		IO_Write(0x3cf,gfx_data[ct]);
	}
	memset(&att_data,0,ATT_REGS);
	att_data[0x12]=0xf;				//Always have all color planes enabled

	/* Program Attribute Controller */
	switch (CurMode->type)
	{
	case M_EGA:
	case M_LIN4:
		att_data[0x10]=0x01;		//Color Graphics
		switch (CurMode->mode)
		{
		case 0x0f:
			att_data[0x12] = 0x05;	// planes 0 and 2 enabled
			att_data[0x10] |= 0x0a;	// monochrome and blinking

			att_data[0x01] = 0x08; // low-intensity
			att_data[0x04] = 0x18; // blink-on case
			att_data[0x05] = 0x18; // high-intensity
			att_data[0x09] = 0x08; // low-intensity in blink-off case
			att_data[0x0d] = 0x18; // high-intensity in blink-off
			break;
		case 0x11:
			for (i=1; i<16; i++) att_data[i]=0x3f;
			break;
		case 0x10:
		case 0x12:
			goto att_text16;
		default:
			for (ct=0; ct<8; ct++)
			{
				att_data[ct]=ct;
				att_data[ct+8]=ct+0x10;
			}
			break;
		}
		break;
	case M_TEXT:
		if (CurMode->cwidth==9)
		{
			att_data[0x13]=0x08;	//Pel panning on 8, although we don't have 9 dot text mode
			att_data[0x10]=0x0C;	//Color Text with blinking, 9 Bit characters
		}
		else
		{
			att_data[0x13]=0x00;
			att_data[0x10]=0x08;	//Color Text with blinking, 8 Bit characters
		}
		real_writeb(BIOSMEM_SEG,BIOSMEM_CURRENT_PAL,0x30);
att_text16:
		if (CurMode->mode==7)   //Monochrome mode?
		{
			att_data[0]=0x00; //Black!
			att_data[8]=0x10; //Green!
			for (i=1; i<8; i++)   //Everything in between?
			{
				att_data[i]=0x08;
				att_data[i+8]=0x18;
			}
			att_data[0x10] |= 2; //Enable monochrome emulation mode! Added by superfury for full VGA compatibility!
		}
		else
		{
			for (ct=0; ct<8; ct++) //Used to be up to 8!
			{
				att_data[ct]=ct; //Color all, dark!
				att_data[ct+8]=ct+0x38; //Color all, lighter!
			}
			if (IS_VGA_ARCH) att_data[0x06]=0x14; //Odd Color 6 yellow/brown.
		}
		break;
	case M_CGA2:
		att_data[0x10]=0x01;		//Color Graphics
		att_data[0]=0x0;
		for (i=1; i<0x10; i++) att_data[i]=0x17;
		att_data[0x12]=0x1;			//Only enable 1 plane
		real_writeb(BIOSMEM_SEG,BIOSMEM_CURRENT_PAL,0x3f);
		break;
	case M_CGA4:
		att_data[0x10]=0x01;		//Color Graphics
		att_data[0]=0x0;
		att_data[1]=0x13;
		att_data[2]=0x15;
		att_data[3]=0x17;
		att_data[4]=0x02;
		att_data[5]=0x04;
		att_data[6]=0x06;
		att_data[7]=0x07;
		for (ct=0x8; ct<0x10; ct++)
			att_data[ct] = ct + 0x8;
		real_writeb(BIOSMEM_SEG,BIOSMEM_CURRENT_PAL,0x30);
		break;
	case M_VGA:
	case M_LIN8:
	case M_LIN15:
	case M_LIN16:
	case M_LIN32:
		for (ct=0; ct<16; ct++) att_data[ct]=ct;
		att_data[0x10]=0x41; //Color Graphics 8-bit
		break;
	default:
		break;
	}
	IO_Read(mono_mode ? 0x3ba : 0x3da);
	if ((modeset_ctl & 8)==0)
	{
		for (ct=0; ct<ATT_REGS; ct++)
		{
			IO_Write(0x3c0,ct);
			IO_Write(0x3c0,att_data[ct]);
		}

		IO_Write(0x3c0,0x20);
		IO_Write(0x3c0,0x00); //Enable palette access for HW!
		IO_Write(0x3c6,0xff); //Reset Pelmask
		/* Setup the DAC */
		IO_Write(0x3c8,0);
		switch (CurMode->type)
		{
		case M_EGA:
			if (CurMode->mode>0xf)
			{
				goto dac_text16;
			}
			else if (CurMode->mode==0xf)
			{
				for (i=0; i<64; i++)
				{
					IO_Write(0x3c9,mtext_s3_palette[i][0]);
					IO_Write(0x3c9,mtext_s3_palette[i][1]);
					IO_Write(0x3c9,mtext_s3_palette[i][2]);
				}
			}
			else
			{
				for (i=0; i<64; i++)
				{
					IO_Write(0x3c9,ega_palette[i][0]);
					IO_Write(0x3c9,ega_palette[i][1]);
					IO_Write(0x3c9,ega_palette[i][2]);
				}
			}
			break;
		case M_CGA2:
		case M_CGA4:
			for (i=0; i<64; i++)
			{
				IO_Write(0x3c9,cga_palette_2[i][0]);
				IO_Write(0x3c9,cga_palette_2[i][1]);
				IO_Write(0x3c9,cga_palette_2[i][2]);
			}
			break;
		case M_TEXT:
			if (CurMode->mode==7)
			{
				for (i=0; i<64; i++)
				{
					IO_Write(0x3c9,mtext_palette[i][0]);
					IO_Write(0x3c9,mtext_palette[i][1]);
					IO_Write(0x3c9,mtext_palette[i][2]);
				}
				break;
			} //FALLTHROUGH!!!!
dac_text16:
			for (i=0; i<64; i++)
			{
				IO_Write(0x3c9,text_palette[i][0]);
				IO_Write(0x3c9,text_palette[i][1]);
				IO_Write(0x3c9,text_palette[i][2]);
			}
			break;
		case M_VGA:
		case M_LIN8:
		case M_LIN15:
		case M_LIN16:
		case M_LIN32:
			// IBM and clones use 248 default colors in the palette for 256-color mode.
			// The last 8 colors of the palette are only initialized to 0 at BIOS init.
			// Palette index is left at 0xf8 as on most clones, IBM leaves it at 0x10.
			for (i = 0; i<248; i++)
			{
				IO_Write(0x3c9,vga_palette[i][0]);
				IO_Write(0x3c9,vga_palette[i][1]);
				IO_Write(0x3c9,vga_palette[i][2]);
			}
			break;
		default:
			break;
		}
		/* check if gray scale summing is enabled */
		if (real_readb(BIOSMEM_SEG,BIOSMEM_MODESET_CTL) & 2)
		{
			INT10_PerformGrayScaleSumming(0,256);
		}
	}
	else
	{
		for (ct=0x10; ct<ATT_REGS; ct++)
		{
			if (ct==0x11) continue;	// skip overscan register
			IO_Write(0x3c0,ct);
			IO_Write(0x3c0,att_data[ct]);
		}
	}

	/* Setup some special stuff for different modes */
	feature=real_readb(BIOSMEM_SEG,BIOSMEM_INITIAL_MODE);
	switch (CurMode->type)
	{
	case M_CGA2:
		feature=(feature&~0x30)|0x20;
		real_writeb(BIOSMEM_SEG,BIOSMEM_CURRENT_MSR,0x1e);
		break;
	case M_CGA4:
		feature=(feature&~0x30)|0x20;
		if (CurMode->mode==4) real_writeb(BIOSMEM_SEG,BIOSMEM_CURRENT_MSR,0x2a);
		else if (CurMode->mode==5) real_writeb(BIOSMEM_SEG,BIOSMEM_CURRENT_MSR,0x2e);
		else real_writeb(BIOSMEM_SEG,BIOSMEM_CURRENT_MSR,0x2);
		break;
	case M_TEXT:
		feature=(feature&~0x30)|0x20;
		switch (CurMode->mode)
		{
		case 0:
			real_writeb(BIOSMEM_SEG,BIOSMEM_CURRENT_MSR,0x2c);
			break;
		case 1:
			real_writeb(BIOSMEM_SEG,BIOSMEM_CURRENT_MSR,0x28);
			break;
		case 2:
			real_writeb(BIOSMEM_SEG,BIOSMEM_CURRENT_MSR,0x2d);
			break;
		case 3:
		case 7:
			real_writeb(BIOSMEM_SEG,BIOSMEM_CURRENT_MSR,0x29);
			break;
		default:
			break;
		}
		break;
	case M_LIN4:
	case M_EGA:
	case M_VGA:
		feature=(feature&~0x30);
		break;
	default:
		break;
	}
	// disabled, has to be set in bios.cpp exclusively
//	real_writeb(BIOSMEM_SEG,BIOSMEM_INITIAL_MODE,feature);

	VGA_ModeExtraData modeData;
	switch (svgaCard) { //What SVGA card?
	case SVGA_TsengET4K: //ET4K?
	case SVGA_TsengET3K: //ET3K?
		modeData.ver_overflow = ver_overflow;
		modeData.hor_overflow = hor_overflow;
		modeData.offset = offset;
		modeData.modeNo = CurMode->mode;
		modeData.htotal = CurMode->htotal;
		modeData.vtotal = CurMode->vtotal;
		if (svgaCard==SVGA_TsengET4K) //ET4K?
		{
			FinishSetMode_ET4K(crtc_base, &modeData); //Finish the ET4K mode!
		}
		else //ET3K?
		{
			FinishSetMode_ET3K(crtc_base, &modeData); //Finish the ET3K mode!
		}
		break;
	case SVGA_None: //No SVGA?
	default: //Not SVGA or valid SVGA?
		break; //Not SVGA: Ignore this setting!
	}


	FinishSetMode(clearmem);

	/* Set vga attrib register into defined state */
	IO_Read(mono_mode ? 0x3ba : 0x3da);
	IO_Write(0x3c0,0x20);

	/* Load text mode font */
	if (CurMode->type==M_TEXT)
	{
		INT10_ReloadFont(); //Reload the font!
	}

	// Enable screen memory access
	IO_Write(0x3c4, 1); IO_Write(0x3c5, seq_data[1] & ~0x20);
	return true;
}