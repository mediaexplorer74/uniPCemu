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

#ifndef HEADER_DOSBOX_H
#define HEADER_DOSBOX_H
//Dosbox Takeover stuff!

#include "headers/types.h" //Basic types!
#include "headers/hardware/vga/vga_cga_mda.h" //CGA/MDA detection support!

#ifndef bool
//Booleans are ints!
#define bool byte
#endif

/*

Constants for memory adressing:

*/

#define S3_LFB_BASE		0xC0000000

#define BIOSMEM_SEG		0x40

#define BIOSMEM_INITIAL_MODE  0x10
#define BIOSMEM_CURRENT_MODE  0x49
#define BIOSMEM_NB_COLS       0x4A
#define BIOSMEM_PAGE_SIZE     0x4C
#define BIOSMEM_CURRENT_START 0x4E
#define BIOSMEM_CURSOR_POS    0x50
#define BIOSMEM_CURSOR_TYPE   0x60
#define BIOSMEM_CURRENT_PAGE  0x62
#define BIOSMEM_CRTC_ADDRESS  0x63
#define BIOSMEM_CURRENT_MSR   0x65
#define BIOSMEM_CURRENT_PAL   0x66
#define BIOSMEM_NB_ROWS       0x84
#define BIOSMEM_CHAR_HEIGHT   0x85
#define BIOSMEM_VIDEO_CTL     0x87
#define BIOSMEM_SWITCHES      0x88
#define BIOSMEM_MODESET_CTL   0x89
#define BIOSMEM_DCC_INDEX     0x8A
#define BIOSMEM_CRTCPU_PAGE   0x8A
#define BIOSMEM_VS_POINTER    0xA8


/*
 *
 * VGA registers
 *
 */
#define VGAREG_ACTL_ADDRESS            0x3c0
#define VGAREG_ACTL_WRITE_DATA         0x3c0
#define VGAREG_ACTL_READ_DATA          0x3c1

#define VGAREG_INPUT_STATUS            0x3c2
#define VGAREG_WRITE_MISC_OUTPUT       0x3c2
#define VGAREG_VIDEO_ENABLE            0x3c3
#define VGAREG_SEQU_ADDRESS            0x3c4
#define VGAREG_SEQU_DATA               0x3c5

#define VGAREG_PEL_MASK                0x3c6
#define VGAREG_DAC_STATE               0x3c7
#define VGAREG_DAC_READ_ADDRESS        0x3c7
#define VGAREG_DAC_WRITE_ADDRESS       0x3c8
#define VGAREG_DAC_DATA                0x3c9

#define VGAREG_READ_FEATURE_CTL        0x3ca
#define VGAREG_READ_MISC_OUTPUT        0x3cc

#define VGAREG_GRDC_ADDRESS            0x3ce
#define VGAREG_GRDC_DATA               0x3cf

#define VGAREG_MDA_CRTC_ADDRESS        0x3b4
#define VGAREG_MDA_CRTC_DATA           0x3b5
#define VGAREG_VGA_CRTC_ADDRESS        0x3d4
#define VGAREG_VGA_CRTC_DATA           0x3d5

#define VGAREG_MDA_WRITE_FEATURE_CTL   0x3ba
#define VGAREG_VGA_WRITE_FEATURE_CTL   0x3da
#define VGAREG_ACTL_RESET              0x3da
#define VGAREG_TDY_RESET               0x3da
#define VGAREG_TDY_ADDRESS             0x3da
#define VGAREG_TDY_DATA                0x3de
#define VGAREG_PCJR_DATA               0x3da

#define VGAREG_MDA_MODECTL             0x3b8
#define VGAREG_CGA_MODECTL             0x3d8
#define VGAREG_CGA_PALETTE             0x3d9

/* Video memory */
#define VGAMEM_GRAPH 0xA000
#define VGAMEM_CTEXT 0xB800
#define VGAMEM_MTEXT 0xB000

#define BIOS_NCOLS word ncols=real_readw(BIOSMEM_SEG,BIOSMEM_NB_COLS);
#define BIOS_NROWS word nrows=(word)real_readb(BIOSMEM_SEG,BIOSMEM_NB_ROWS)+1;

extern byte int10_font_08[256 * 8];
extern byte int10_font_14[256 * 14];
extern byte int10_font_16[256 * 16];

//Set to byte for now!


#define _EGA_HALF_CLOCK		0x0001
#define _DOUBLESCAN	0x0002
#define _VGA_PIXEL_DOUBLE	0x0004
#define _REPEAT1 0x0008

#define SEQ_REGS 0x05
#define GFX_REGS 0x09
#define ATT_REGS 0x15

typedef enum
{
    M_CGA2, M_CGA4,
    M_EGA, M_VGA,
    M_LIN4, M_LIN8, M_LIN15, M_LIN16, M_LIN32,
    M_TEXT,
    M_HERC_GFX, M_HERC_TEXT,
    M_CGA16, M_TANDY2, M_TANDY4, M_TANDY16, M_TANDY_TEXT,
    M_ERROR
} VGAModes;



enum SVGACards {
	SVGA_None,
	SVGA_S3Trio,
	SVGA_TsengET4K,
	SVGA_TsengET3K,
	SVGA_ParadisePVGA1A
};
typedef byte SVGAmode; //SVGA mode!

typedef struct
{
	word	mode;
	VGAModes	type;
	uint_32	swidth, sheight;
	uint_32	twidth, theight;
	uint_32	cwidth, cheight;
	uint_32	ptotal,pstart,plength;

	uint_32	htotal,vtotal;
	uint_32	hdispend,vdispend;
	uint_32	special;

} VideoModeBlock;

extern VideoModeBlock ModeList_VGA[];
extern VideoModeBlock * CurMode;

typedef uint_32 RealPt; //16-bit segment:offset value! (segment high, offset low)

typedef struct
{
	struct
	{
		RealPt font_8_first;
		RealPt font_8_second;
		RealPt font_14;
		RealPt font_16;
		RealPt font_14_alternate;
		RealPt font_16_alternate;
		RealPt static_state;
		RealPt video_save_pointers;
		RealPt video_parameter_table;
		RealPt video_save_pointer_table;
		RealPt video_dcc_table;
		RealPt oemstring;
		RealPt vesa_modes;
		RealPt pmode_interface;
		word pmode_interface_size;
		word pmode_interface_start;
		word pmode_interface_window;
		word pmode_interface_palette;
		word used;
	} rom;
	word vesa_setmode;
	bool vesa_nolfb;
	bool vesa_oldvbe;
} Int10Data;

extern Int10Data int10;

#define CLK_25 25175
#define CLK_28 28322

#define MIN_VCO	180000
#define MAX_VCO 360000

#define S3_CLOCK_REF	14318	/* KHz */
#define S3_CLOCK(_M,_N,_R)	((S3_CLOCK_REF * ((_M) + 2)) / (((_N) + 2) * (1 << (_R))))
#define S3_MAX_CLOCK	150000	/* KHz */

#define S3_XGA_1024		0x00
#define S3_XGA_1152		0x01
#define S3_XGA_640		0x40
#define S3_XGA_800		0x80
#define S3_XGA_1280		0xc0
#define S3_XGA_WMASK	(S3_XGA_640|S3_XGA_800|S3_XGA_1024|S3_XGA_1152|S3_XGA_1280)

#define S3_XGA_8BPP  0x00
#define S3_XGA_16BPP 0x10
#define S3_XGA_32BPP 0x30
#define S3_XGA_CMASK (S3_XGA_8BPP|S3_XGA_16BPP|S3_XGA_32BPP)

/*

end of dosbox data!

*/

/*

Patches for dosbox!

*/

enum MachineType
{
    MCH_HERC=0,
    MCH_CGA=1,
    MCH_TANDY=2,
    MCH_PCJR=3,
    MCH_EGA=4,
    MCH_VGA=5
};

//UniPCemu's Video machine autodetection!
#define machine ((CGAMDAEMULATION_ENABLED(getActiveVGA()))?MCH_CGA:MCH_VGA)

//Type patches!
#define Bit16u word
#define Bit8u byte
#define Bit32u uint_32
#define Bitu uint_32
#define Bits int_32

typedef byte *PhysPt; //Physical pointer!

//Real pointer is a 32-bit segment:offset pointer.

#define color pixel

//Real compatiblity!
#define RealMake(seg,offs) ((((seg)&0xFFFF)<<16)|((offs)&0xFFFF))
#define RealSeg(real) (((real)>>16)&0xFFFF)
#define RealOff(real) ((real)&0xFFFF)

//Simple memory compatiblity for real mode!
#define mem_readb(off) MMU_rb(-1,RealSeg(off),RealOff(off),0,1)
#define mem_writeb(off,val) MMU_wb(-1,RealSeg(off),RealOff(off),val,1)
#define mem_readw(off) MMU_rw(-1,RealSeg(off),RealOff(off),0,1)
#define mem_writew(off,val) MMU_ww(-1,RealSeg(off),RealOff(off),val,1)
#define mem_readd(off) MMU_rdw(-1,RealSeg(off),RealOff(off),0,1)
#define mem_writed(off,val) MMU_wdw(-1,RealSeg(off),RealOff(off),val,1)

#define PhysMake(seg,offs) (byte *)MMU_ptr(-1,seg,offs,0,1)

//Physical 2 real support
#define Phys2Real1(x) (uint_32)(((byte *)x)-((byte *)MMU_ptr(-1,0,0,0,0)))
#define Phys2Real(x) (RealMake((Phys2Real1(x)>>4),(Phys2Real1(x)&0xF)))

//Real 2 physical
#define Real2Phys(x) PhysMake((((x)>>16)&0xFFFF),((x)&0xFFFF))

#define false 0
#define S3_LFB_BASE 0xC0000000

//Palette only:
#define VGAREG_ACTL_ADDRESS            0x3c0
#define VGAREG_ACTL_WRITE_DATA         0x3c0
#define VGAREG_ACTL_READ_DATA          0x3c1
#define VGAREG_ACTL_RESET              0x3da
#define ACTL_MAX_REG   0x14


//Dosbox Patches! Redirect it all!
#define real_readb(biosseg,offs) mem_readb(RealMake(biosseg,offs))
#define real_writeb(biosseg,offs,val) mem_writeb(RealMake(biosseg,offs),val)
#define real_readw(biosseg,offs) mem_readw(RealMake(biosseg,offs))
#define real_writew(biosseg,offs,val) mem_writew(RealMake(biosseg,offs),val)
#define real_readd(biosseg,offs) mem_readd(RealMake(biosseg,offs))
#define real_writed(biosseg,offs,val) mem_writed(RealMake(biosseg,offs),val)
#define memreal_writeb real_writeb
#define memreal_writew real_writew
#define memreal_writed real_writed

//Extras
#define address2phys(address) PhysMake(address>>4,(address&0xF))

//Port I/O
#define IO_WriteB(port,value) PORT_OUT_B(port,value)
#define IO_WriteW(port,value) PORT_OUT_W(port,value)
#define IO_ReadB(port) PORT_IN_B(port)
#define IO_ReadW(port) PORT_IN_W(port)
//Synonym for IO_WriteB:
#define IO_Write(port,value) IO_WriteB(port,value)
#define IO_Read(port) IO_ReadB(port)

//Simply return to the emulator from the interrupt handler when required to go IDLE!
#define CALLBACK_Idle() return

#define true 1
#define false 0

#define IS_TANDY_ARCH ((machine==MCH_TANDY) || (machine==MCH_PCJR))
#define IS_EGAVGA_ARCH ((machine==MCH_EGA) || (machine==MCH_VGA))
#define IS_VGA_ARCH (machine==MCH_VGA)
#define TANDY_ARCH_CASE MCH_TANDY: case MCH_PCJR
#define EGAVGA_ARCH_CASE MCH_EGA: case MCH_VGA
#define VGA_ARCH_CASE MCH_VGA

#define Bitu uint_32


/*

Our own switch function!

*/

void switchvideomode(word mode); //For DOSBox way!

//Phys/Real pointer support
void phys_writed(PhysPt ptr, uint_32 val);
void phys_writew(PhysPt ptr, word val);
void phys_writeb(PhysPt ptr, byte val);
byte phys_readb(PhysPt ptr);
word phys_readw(PhysPt ptr);
uint_32 phys_readd(PhysPt ptr);
void RealSetVec(byte interrupt, word segment, word offset);


//Now, keyboard support!

enum KBD_KEYS {
	KBD_NONE,
	KBD_1,	KBD_2,	KBD_3,	KBD_4,	KBD_5,	KBD_6,	KBD_7,	KBD_8,	KBD_9,	KBD_0,		
	KBD_q,	KBD_w,	KBD_e,	KBD_r,	KBD_t,	KBD_y,	KBD_u,	KBD_i,	KBD_o,	KBD_p,	
	KBD_a,	KBD_s,	KBD_d,	KBD_f,	KBD_g,	KBD_h,	KBD_j,	KBD_k,	KBD_l,	KBD_z,
	KBD_x,	KBD_c,	KBD_v,	KBD_b,	KBD_n,	KBD_m,	
	KBD_f1,	KBD_f2,	KBD_f3,	KBD_f4,	KBD_f5,	KBD_f6,	KBD_f7,	KBD_f8,	KBD_f9,	KBD_f10,KBD_f11,KBD_f12,
	
	/*Now the weirder keys */

	KBD_esc,KBD_tab,KBD_backspace,KBD_enter,KBD_space,
	KBD_leftalt,KBD_rightalt,KBD_leftctrl,KBD_rightctrl,KBD_leftshift,KBD_rightshift,
	KBD_capslock,KBD_scrolllock,KBD_numlock,
	
	KBD_grave,KBD_minus,KBD_equals,KBD_backslash,KBD_leftbracket,KBD_rightbracket,
	KBD_semicolon,KBD_quote,KBD_period,KBD_comma,KBD_slash,KBD_extra_lt_gt,

	KBD_printscreen,KBD_pause,
	KBD_insert,KBD_home,KBD_pageup,KBD_delete,KBD_end,KBD_pagedown,
	KBD_left,KBD_up,KBD_down,KBD_right,

	KBD_kp1,KBD_kp2,KBD_kp3,KBD_kp4,KBD_kp5,KBD_kp6,KBD_kp7,KBD_kp8,KBD_kp9,KBD_kp0,
	KBD_kpdivide,KBD_kpmultiply,KBD_kpminus,KBD_kpplus,KBD_kpenter,KBD_kpperiod,

	
	KBD_LAST
};

#define BIOS_KEYBOARD_STATE             0x417
#define BIOS_KEYBOARD_FLAGS1            BIOS_KEYBOARD_STATE
#define BIOS_KEYBOARD_FLAGS2            0x418
#define BIOS_KEYBOARD_TOKEN             0x419
/* used for keyboard input with Alt-Number */
#define BIOS_KEYBOARD_BUFFER_HEAD       0x41a
#define BIOS_KEYBOARD_BUFFER_TAIL       0x41c
#define BIOS_KEYBOARD_BUFFER            0x41e
#define BIOS_KEYBOARD_FLAGS             0x471
#define BIOS_KEYBOARD_BUFFER_START      0x480
#define BIOS_KEYBOARD_BUFFER_END        0x482
#define BIOS_KEYBOARD_FLAGS3            0x496
#define BIOS_KEYBOARD_LEDS              0x497
#define BIOS_WAIT_FLAG_POINTER          0x498
#define BIOS_WAIT_FLAG_COUNT	        0x49c		
#define BIOS_WAIT_FLAG_ACTIVE			0x4a0
#define BIOS_WAIT_FLAG_TEMP				0x4a1

/* maximum of scancodes handled by keyboard bios routines */
#define MAX_SCAN_CODE 0x58

#define BIOS_DEFAULT_HANDLER_LOCATION	(RealMake(0xf000,0xff53))
#define BIOS_DEFAULT_IRQ0_LOCATION		(RealMake(0xf000,0xfea5))
#define BIOS_DEFAULT_IRQ1_LOCATION		(RealMake(0xf000,0xe987))
#define BIOS_DEFAULT_IRQ2_LOCATION		(RealMake(0xf000,0xff55))

/* Video mode extra data to be passed to FinishSetMode_SVGA().

   This structure will be in flux until all drivers (including S3)

   are properly separated. Right now it contains only three overflow

   fields in S3 format and relies on drivers re-interpreting those.

   For reference:

   ver_overflow:X|line_comp10|X|vretrace10|X|vbstart10|vdispend10|vtotal10

   hor_overflow:X|X|X|hretrace8|X|hblank8|hdispend8|htotal8

   offset is not currently used by drivers (useful only for S3 itself)

   It also contains basic int10 mode data - number, vtotal, htotal

   */

typedef struct {

	Bit8u ver_overflow;

	Bit8u hor_overflow;

	Bitu offset;

	Bitu modeNo;

	Bitu htotal;

	Bitu vtotal;

} VGA_ModeExtraData;


#endif