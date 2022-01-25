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

Interrupt 10h: Video interrupt

*/

#include "headers/types.h" //Basic types!
#include "headers/emu/gpu/gpu.h" //Real ouput module!
#include "headers/cpu/cpu.h" //CPU module!
#include "headers/cpu/easyregs.h" //Easy register access!
#include "headers/cpu/interrupts.h" //Interrupt support for GRAPHIC modes!
#include "headers/hardware/vga/vga.h" //Basic typedefs!
#include "headers/header_dosbox.h" //Screen modes from DOSBox!
#include "headers/hardware/ports.h" //Port support!
#include "headers/hardware/vga/vga_dacrenderer.h" //For color/mono detection!
#include "headers/support/log.h" //Logging support!
#include "headers/interrupts/interrupt10.h" //Our typedefs etc.
#include "headers/cpu/cb_manager.h" //Callback detection!
#include "headers/cpu/protection.h" //For reading RAM!
#include "headers/fopen64.h" //64-bit fopen support!

//Are we disabled for checking?
#define __HW_DISABLED 0

//Text screen height is always 25!

extern GPU_type GPU; //GPU info&adjusting!

int int10loaded = 0; //Default: not loaded yet!

//Screencontents: 0xB800:(row*0x0040:004A)+column*2)
//Screencolorsfont: 0xB800:((row*0x0040:004A)+column*2)+1)
//Screencolorsback: Same as font, high nibble!

extern VideoModeBlock ModeList_VGA[0x15]; //VGA Modelist!
extern VideoModeBlock *CurMode; //VGA Active Video Mode!

//All palettes:
extern byte text_palette[64][3];
extern byte mtext_palette[64][3];
extern byte mtext_s3_palette[64][3];
extern byte ega_palette[64][3];
extern byte cga_palette[16][3];
extern byte cga_palette_2[64][3];
extern byte vga_palette[256][3];

//Masks for CGA!
extern Bit8u cga_masks[4];
extern Bit8u cga_masks2[8];

SVGAmode svgaCard = SVGA_None; //Default to no SVGA card!

word getscreenwidth() //Get the screen width (in characters), based on the video mode!
{
	if (__HW_DISABLED) return 0; //Abort!
	return MMU_rw(-1,BIOSMEM_SEG,BIOSMEM_NB_COLS,0,1);
	byte result;
	switch (MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURRENT_MODE,0,1)) //What video mode?
	{
	case 0x00:
	case 0x01: //40x25?
		result = 40; //40 columns a row!
		break;
	default:
	case 0x02:
	case 0x03:
	case 0x07: //80x25?
		result = 80; //80 columns a row!
		break;
	}
	//GPU.showpixels = ((result!=0) && ALLOW_GPU_GRAPHICS); //Show pixels?
	GPU.showpixels = ALLOW_GPU_GRAPHICS; //Always allow!
	return result; //Give the result!
}

OPTINLINE byte GPUgetvideomode()
{
	if (__HW_DISABLED) return 0; //Abort!
	return MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURRENT_MODE,0,1); //Give mode!
}

void GPUswitchvideomode(word mode)
{
	GPU.showpixels = ALLOW_GPU_GRAPHICS; //Show pixels!

	//Now all BIOS data!
	INT10_Internal_SetVideoMode(mode); //Switch video modes!
}


OPTINLINE int GPU_getpixel(int x, int y, byte page, word *color) //Get a pixel from the real emulated screen buffer!
{
	if (__HW_DISABLED) return 0; //Abort!
	byte curbank, repeating, reps;
	uint_32 rowoffs, rowoffsbackup;
		if ((svgaCard==SVGA_TsengET3K) || (svgaCard==SVGA_TsengET4K))
		{
			IO_Write(0x3CD, 0x40); //Set the new bank to it's default value for safety!
		}        switch (CurMode->type) {
        case M_CGA4:
                {
                        Bit16u off=(y>>1)*80+(x>>2);
                        if (y&1) off+=8*1024;
                        Bit8u val=MMU_rb(-1,0xb800,off,0,1);
                        *color=(val>>(((3-(x&3)))*2)) & 3 ;
                }
                break;
        case M_CGA2:
                {
                        Bit16u off=(y>>1)*80+(x>>3);
                        if (y&1) off+=8*1024;
                        Bit8u val=MMU_rb(-1,0xb800,off,0,1);
                        *color=(val>>(((7-(x&7))))) & 1 ;
                }
                break;
        case M_EGA:
                {
                        /* Calculate where the pixel is in video memory */
                        //if (CurMode->plength!=(Bitu)MMU_rw(-1,BIOSMEM_SEG,BIOSMEM_PAGE_SIZE,0))
                                //LOG(LOG_INT10,LOG_ERROR)("GetPixel_EGA_p: %x!=%x",CurMode->plength,MMU_rw(-1,BIOSMEM_SEG,BIOSMEM_PAGE_SIZE,0));
                        //if (CurMode->swidth!=(Bitu)MMU_rw(-1,BIOSMEM_SEG,BIOSMEM_NB_COLS,0)*8)
                                //LOG(LOG_INT10,LOG_ERROR)("GetPixel_EGA_w: %x!=%x",CurMode->swidth,MMU_rw(-1,BIOSMEM_SEG,BIOSMEM_NB_COLS,0)*8);
                        RealPt off=RealMake(0xa000,MMU_rw(-1,BIOSMEM_SEG,BIOSMEM_PAGE_SIZE,0,1)*page+
                                ((y*MMU_rw(-1,BIOSMEM_SEG,BIOSMEM_NB_COLS,0,1)*8+x)>>3));
                        Bitu shift=7-(x & 7);
                        /* Set the read map */
                        *color=0;
                        IO_Write(0x3ce,0x4);IO_Write(0x3cf,1);
                        *color|=((mem_readb(off)>>shift) & 1) << 0;
                        IO_Write(0x3ce,0x4);IO_Write(0x3cf,2);
                        *color|=((mem_readb(off)>>shift) & 1) << 1;
                        IO_Write(0x3ce,0x4);IO_Write(0x3cf,4);
                        *color|=((mem_readb(off)>>shift) & 1) << 2;
                        IO_Write(0x3ce,0x4);IO_Write(0x3cf,8);
                        *color|=((mem_readb(off)>>shift) & 1) << 3;
                        break;
                }
        case M_VGA:
                *color=mem_readb(RealMake(0xa000,320*y+x));
                break;
        case M_LIN8:
		case M_LIN16:
				rowoffs = (y*real_readw(BIOSMEM_SEG, BIOSMEM_NB_COLS) * 8); //The offset to retrieve!
				if (CurMode->type == M_LIN16)
				{
					rowoffs += (x << 1); //Add X words!
					repeating = 0; //Don't repeat!
				}
				else
				{
					rowoffs += x; //Add X bytes!
					repeating = 1; //Repeat once!
				}
				rowoffsbackup = rowoffs; //Save backup!
				*color = 0; //Init!
				reps = 0; //Init!

			dorepeat:
				//Translate the offset to a bank number&offset if needed
				if (svgaCard==SVGA_TsengET4K) //ET4K?
				{
					curbank = IO_Read(0x3CD); //Read the current bank!
					curbank &= 0xF; //Clear read bank!
					curbank |= (rowoffs >> 12) & 0xF0; //The bank to use!
					rowoffs &= 0xFFFF; //Pixel in the bank!
					IO_Write(0x3CD, curbank); //Set the new bank!
				}
				else if (svgaCard==SVGA_TsengET3K) //ET3K?
				{
					curbank = IO_Read(0x3CD); //Read the current bank!
					curbank &= 0x7; //Clear read bank&bank size!
					curbank |= (rowoffs >> 13) & 0x38; //The bank to use!
					curbank |= 0x40; //64k bank size!
					rowoffs &= 0xFFFF; //Pixel in the bank!
					IO_Write(0x3CD, curbank); //Set the new bank!
				}
                RealPt off=RealMake(0xA000,rowoffs); //Pointer to memory!
				*color |= (mem_readb(off) << (8*reps)); //Move to the correct byte!
				if (repeating) //Are we to repeat for higher data?
				{
					--repeating; //Processing once!
					rowoffs = ++rowoffsbackup; //Restore backup!
					++reps; //Increase repeats!
					goto dorepeat; //Repeat!
				}
				break;
        default:
                //LOG(LOG_INT10,LOG_ERROR)("GetPixel unhandled mode type %d",CurMode->type);
        	return 0; //Error: unknown mode!
                break;
        }
        return 1; //OK!
}

int GPU_putpixel(int x, int y, byte page, word color) //Writes a video buffer pixel to the real emulated screen buffer
{
	byte curbank,repeating;
	uint_32 rowoffs,rowoffsbackup;
	if (__HW_DISABLED) return 0; //Abort!
        //static bool putpixelwarned = false;
		if ((svgaCard==SVGA_TsengET3K) || (svgaCard==SVGA_TsengET4K))
		{
			IO_Write(0x3CD, 0x40); //Set the new bank to it's default value for safety!
		}
        switch (CurMode->type) {
        case M_CGA4:
                {
                                if (real_readb(BIOSMEM_SEG,BIOSMEM_CURRENT_MODE)<=5) {
                                        Bit16u off=(y>>1)*80+(x>>2);
                                        if (y&1) off+=8*1024;

                                        Bit8u old=real_readb(0xb800,off);
                                        if (color & 0x80) {
                                                color&=3;
                                                old^=color << (2*(3-(x&3)));
                                        } else {
                                                old=(old&cga_masks[x&3])|((color&3) << (2*(3-(x&3))));
                                        }
                                        real_writeb(0xb800,off,old);
                                } else {
										Bit16u off = (y >> 2) * 160 + ((x >> 2)&(~1));
                                        off+=(8*1024) * (y & 3);

                                        Bit16u old=real_readw(0xb800,off);
                                        if (color & 0x80) {
                                                old^=(color&1) << (7-(x&7));
                                                old^=((color&2)>>1) << ((7-(x&7))+8);
                                        } else {
											old = (old&(~(0x101 << (7 - (x & 7))))) | ((color & 1) << (7 - (x & 7))) | (((color & 2) >> 1) << ((7 - (x & 7)) + 8));
                                        }
                                        real_writew(0xb800,off,old);
                                }
                }
                break;
        case M_CGA2:
                {
                                Bit16u off=(y>>1)*80+(x>>3);
                                if (y&1) off+=8*1024;
                                Bit8u old=real_readb(0xb800,off);
                                if (color & 0x80) {
                                        color&=1;
                                        old^=color << ((7-(x&7)));
                                } else {
                                        old=(old&cga_masks2[x&7])|((color&1) << ((7-(x&7))));
                                }
                                real_writeb(0xb800,off,old);
                }
                break;
        case M_TANDY16:
                {
                        IO_Write(0x3d4,0x09);
                        Bit8u scanlines_m1=IO_Read(0x3d5);
                        Bit16u off=(y>>((scanlines_m1==1)?1:2))*(CurMode->swidth>>1)+(x>>1);
                        off+=(8*1024) * (y & scanlines_m1);
                        Bit8u old=real_readb(0xb800,off);
                        Bit8u p[2];
                        p[1] = (old >> 4) & 0xf;
                        p[0] = old & 0xf;
                        Bitu ind = 1-(x & 0x1);

                        if (color & 0x80) {
                                p[ind]^=(color & 0x7f);
                        } else {
                                p[ind]=(byte)color;
                        }
                        old = (p[1] << 4) | p[0];
                        real_writeb(0xb800,off,old);
                }
                break;
        case M_LIN4:
                //if ((machine!=MCH_VGA) || (svgaCard!=SVGA_TsengET4K) ||
                //                (CurMode->swidth>800)) {
                        // the ET4000 BIOS supports text output in 800x600 SVGA (Gateway 2)
                        // putpixel warning?
                        break;
                //}
        case M_EGA:
                {
                        /* Set the correct bitmask for the pixel position */
                        IO_Write(0x3ce,0x8);Bit8u mask=128>>(x&7);IO_Write(0x3cf,mask);
                        /* Set the color to set/reset register */
                        IO_Write(0x3ce,0x0);IO_Write(0x3cf,(byte)color);
                        /* Enable all the set/resets */
                        IO_Write(0x3ce,0x1);IO_Write(0x3cf,0xf);
                        /* test for xorring */
                        if (color & 0x80) { IO_Write(0x3ce,0x3);IO_Write(0x3cf,0x18); }
                        //Perhaps also set mode 1 
                        /* Calculate where the pixel is in video memory */
                        //if (CurMode->plength!=(Bitu)real_readw(BIOSMEM_SEG,BIOSMEM_PAGE_SIZE))
                        //        LOG(LOG_INT10,LOG_ERROR)("PutPixel_EGA_p: %x!=%x",CurMode->plength,real_readw(BIOSMEM_SEG,BIOSMEM_PAGE_SIZE));
                        //if (CurMode->swidth!=(Bitu)real_readw(BIOSMEM_SEG,BIOSMEM_NB_COLS)*8)
                        //        LOG(LOG_INT10,LOG_ERROR)("PutPixel_EGA_w: %x!=%x",CurMode->swidth,real_readw(BIOSMEM_SEG,BIOSMEM_NB_COLS)*8);
                        RealPt off=RealMake(0xa000,real_readw(BIOSMEM_SEG,BIOSMEM_PAGE_SIZE)*page+
                                ((y*real_readw(BIOSMEM_SEG,BIOSMEM_NB_COLS)*8+x)>>3));
                        /* Bitmask and set/reset should do the rest */
                        mem_readb(off);
                        mem_writeb(off,0xff);
                        /* Restore bitmask */   
                        IO_Write(0x3ce,0x8);IO_Write(0x3cf,0xff);
                        IO_Write(0x3ce,0x1);IO_Write(0x3cf,0);
                        /* Restore write operating if changed */
                        if (color & 0x80) { IO_Write(0x3ce,0x3);IO_Write(0x3cf,0x0); }
                        break;
                }

        case M_VGA:
                mem_writeb(RealMake(0xa000,y*320+x),(byte)color);
                break;
        case M_LIN8:
		case M_LIN16:
                //if (CurMode->swidth!=(Bitu)real_readw(BIOSMEM_SEG,BIOSMEM_NB_COLS)*8)
                //        LOG(LOG_INT10,LOG_ERROR)("PutPixel_VGA_w: %x!=%x",CurMode->swidth,real_readw(BIOSMEM_SEG,BIOSMEM_NB_COLS)*8);
				rowoffs = (y*real_readw(BIOSMEM_SEG,BIOSMEM_NB_COLS)*8); //The offset to retrieve!
				if (CurMode->type == M_LIN16)
				{
					rowoffs += (x << 1); //Add X words!
					repeating = 0; //Don't repeat!
				}
				else
				{
					rowoffs += x; //Add X bytes!
					repeating = 1; //Repeat once!
				}
				rowoffsbackup = rowoffs; //Save backup!

				dorepeat:
				if (svgaCard==SVGA_TsengET4K) //ET4K?
				{
					curbank = IO_Read(0x3CD); //Read the current bank!
					curbank &= 0xF0; //Clear write bank!
					curbank |= (rowoffs>>16)&0x7; //The bank to use!
					rowoffs &= 0xFFFF; //Pixel in the bank!
					IO_Write(0x3CD, curbank); //Set the new bank!
				}
				else //ET3K?
				{
					curbank = IO_Read(0x3CD); //Read the current bank!
					curbank &= 0x38; //Clear write bank!
					curbank |= (rowoffs >> 16) & 0x7; //The bank to use!
					curbank |= 0x40; //64k bank!
					rowoffs &= 0xFFFF; //Pixel in the bank!
					IO_Write(0x3CD, curbank); //Set the new bank!
				}
                RealPt off=RealMake(0xA000,rowoffs); //Pointer to memory!
                mem_writeb(off,(byte)color);
				if (repeating) //Are we to repeat for higher data?
				{
					--repeating; //Processing once!
					rowoffs = ++rowoffsbackup; //Restore backup!
					color >>= 8; //Next higher data!
					goto dorepeat; //Repeat!
				}
                break;
        default:
                //if(GCC_UNLIKELY(!putpixelwarned)) {
                        //putpixelwarned = true;          
                        //LOG(LOG_INT10,LOG_ERROR)("PutPixel unhandled mode type %d",CurMode->type);
                //}
                return 0; //Error!
                break;
        }
        return 1; //OK!
}

OPTINLINE void ResetACTL() {
	if (__HW_DISABLED) return; //Abort!
	IO_Read(real_readw(BIOSMEM_SEG,BIOSMEM_CRTC_ADDRESS) + 6);
}

OPTINLINE void INT10_SetSinglePaletteRegister(Bit8u reg,Bit8u val) {
	if (__HW_DISABLED) return; //Abort!
		if (!IS_VGA_ARCH) reg&=0x1f;
		if(reg<=ACTL_MAX_REG) {
			ResetACTL();
			IO_Write(VGAREG_ACTL_ADDRESS,reg);
			IO_Write(VGAREG_ACTL_WRITE_DATA,val);
		}
		IO_Write(VGAREG_ACTL_ADDRESS,32);		//Enable output and protect palette
}

OPTINLINE void INT10_SetOverscanBorderColor(Bit8u val) {
		ResetACTL();
		IO_Write(VGAREG_ACTL_ADDRESS,0x11);
		IO_Write(VGAREG_ACTL_WRITE_DATA,val);
		IO_Write(VGAREG_ACTL_ADDRESS,32);		//Enable output and protect palette
}

OPTINLINE void INT10_SetAllPaletteRegisters(PhysPt data) {
		ResetACTL();
		// First the colors
		Bit8u i;
		for(i=0;i<0x10;i++) {
			IO_Write(VGAREG_ACTL_ADDRESS,i);
			IO_Write(VGAREG_ACTL_WRITE_DATA,phys_readb(data));
			data++;
		}
		// Then the border
		IO_Write(VGAREG_ACTL_ADDRESS,0x11);
		IO_Write(VGAREG_ACTL_WRITE_DATA,phys_readb(data));
		IO_Write(VGAREG_ACTL_ADDRESS,32);		//Enable output and protect palette
}

OPTINLINE void INT10_SetColorSelect(Bit8u val) {
	if (__HW_DISABLED) return; //Abort!
	Bit8u temp=real_readb(BIOSMEM_SEG,BIOSMEM_CURRENT_PAL);
	temp=(temp & 0xdf) | ((val & 1) ? 0x20 : 0x0);
	real_writeb(BIOSMEM_SEG,BIOSMEM_CURRENT_PAL,temp);
		if (CurMode->mode <= 3) //Maybe even skip the total function!
			return;
		val = (temp & 0x10) | 2 | val;
		INT10_SetSinglePaletteRegister( 1, val );
		val+=2;
		INT10_SetSinglePaletteRegister( 2, val );
		val+=2;
		INT10_SetSinglePaletteRegister( 3, val );
//	}
}

OPTINLINE void INT10_ToggleBlinkingBit(Bit8u state) {
	Bit8u value;
//	state&=0x01;
	//if ((state>1) && (svgaCard==SVGA_S3Trio)) return;
	ResetACTL();
	
	IO_Write(VGAREG_ACTL_ADDRESS,0x10);
	value=IO_Read(VGAREG_ACTL_READ_DATA);
	if (state<=1) {
		value&=0xf7;
		value|=state<<3;
	}

	ResetACTL();
	IO_Write(VGAREG_ACTL_ADDRESS,0x10);
	IO_Write(VGAREG_ACTL_WRITE_DATA,value);
	IO_Write(VGAREG_ACTL_ADDRESS,32);		//Enable output and protect palette

	if (state<=1) {
		Bit8u msrval=real_readb(BIOSMEM_SEG,BIOSMEM_CURRENT_MSR)&0xdf;
		if (state) msrval|=0x20;
		real_writeb(BIOSMEM_SEG,BIOSMEM_CURRENT_MSR,msrval);
	}
}

OPTINLINE void INT10_GetSinglePaletteRegister(Bit8u reg,Bit8u * val) {
	if(reg<=ACTL_MAX_REG) {
		ResetACTL();
		IO_Write(VGAREG_ACTL_ADDRESS,reg+32);
		*val=IO_Read(VGAREG_ACTL_READ_DATA);
		IO_Write(VGAREG_ACTL_WRITE_DATA,*val);
	}
}

OPTINLINE void INT10_GetOverscanBorderColor(Bit8u * val) {
	ResetACTL();
	IO_Write(VGAREG_ACTL_ADDRESS,0x11+32);
	*val=IO_Read(VGAREG_ACTL_READ_DATA);
	IO_Write(VGAREG_ACTL_WRITE_DATA,*val);
}

OPTINLINE void INT10_GetAllPaletteRegisters(PhysPt data) {
	ResetACTL();
	// First the colors
	Bit8u i;
	for(i=0;i<0x10;i++) {
		IO_Write(VGAREG_ACTL_ADDRESS,i);
		phys_writeb(data,IO_Read(VGAREG_ACTL_READ_DATA));
		ResetACTL();
		data++;
	}
	// Then the border
	IO_Write(VGAREG_ACTL_ADDRESS,0x11+32);
	phys_writeb(data,IO_Read(VGAREG_ACTL_READ_DATA));
	ResetACTL();
}

OPTINLINE void updateCursorLocation()
{
	if (__HW_DISABLED) return; //Abort!
	word xy;
	int x;
	int y;
	xy = MMU_rw(-1,BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURRENT_PAGE,0,1)*2),0,1); //X,Y!
	x = (xy&0xFF); //X!
	y = (xy>>8); //Y
	word address; //Address of the cursor location!
	address = MMU_rw(-1,BIOSMEM_SEG,BIOSMEM_CURRENT_START,0,1)+
			(y*MMU_rw(-1,BIOSMEM_SEG,BIOSMEM_NB_COLS,0,1))+x;

	byte oldcrtc = PORT_IN_B(0x3D4); //Save old address!
	PORT_OUT_B(0x3D4,0xF); //Select cursor location low register!
	PORT_OUT_B(0x3D5,address&0xFF); //Low location!
	PORT_OUT_B(0x3D4,0xE); //Select cursor location high register!
	PORT_OUT_B(0x3D5,((address>>8)&0xFF)); //High location!
	PORT_OUT_B(0x3D4,oldcrtc); //Restore old CRTC register!
}

OPTINLINE void EMU_CPU_setCursorXY(byte displaypage, byte x, byte y)
{
	if (__HW_DISABLED) return; //Abort!
//First: BDA entry update!
	MMU_ww(-1,BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(displaypage*2),x|(y<<8),1); //X,Y!

	if (MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURRENT_PAGE,0,1)==displaypage) //Current page?
	{
		//Apply the cursor position to the VGA!
		updateCursorLocation(); //Update the cursor's location!
	}
}

OPTINLINE void EMU_CPU_getCursorScanlines(byte *start, byte *end)
{
	if (__HW_DISABLED) return; //Abort!
	*start = MMU_rb(-1, BIOSMEM_SEG, BIOSMEM_CURSOR_TYPE, 0,1); //Get start line!
	*end = MMU_rb(-1, BIOSMEM_SEG, BIOSMEM_CURSOR_TYPE + 1, 0,1); //Get end line!
}

void EMU_CPU_setCursorScanlines(byte start, byte end)
{
	if (__HW_DISABLED) return; //Abort!
	byte start2, end2;

	float cheight = (float)(CurMode->cheight-1); //Character height value!
	start2 = (byte)(((float)(start&0x7)/7.0f)*cheight); //Scale to character height!
	end2 = (byte)(((float)end / 7.0f)*cheight); //Scale to character height!

	if (start & 0x20) //Disable the cursor?
	{
		start2 |= 0x20; //We're disabling the cursor too!
	}

	//Translate start2 and end2 to size!
	byte oldcrtc = PORT_IN_B(0x3D4); //Save old address!

	//Process cursor start!
	PORT_OUT_B(0x3D4,0xA); //Select start register!
	byte cursorStart = PORT_IN_B(0x3D5); //Read current cursor start!

	cursorStart &= ~0x3F; //Clear our data location!
	cursorStart |= (start2&0x3F); //Add the usable data!
	PORT_OUT_B(0x3D5,cursorStart); //Start!

	//Process cursor end!
	PORT_OUT_B(0x3D4,0xB); //Select end register!
	byte cursorEnd = PORT_IN_B(0x3D5); //Read old end!

	cursorEnd &= ~0x1F; //Clear our data location!
	cursorEnd |= (end2&0x1F); //Create the cursor end data!

	PORT_OUT_B(0x3D5,cursorEnd); //Write new cursor end!
	
	PORT_OUT_B(0x3D4,oldcrtc); //Restore old CRTC register address!

	//Update our values!
	MMU_wb(-1, BIOSMEM_SEG, BIOSMEM_CURSOR_TYPE, start,1); //Set start line!
	MMU_wb(-1, BIOSMEM_SEG, BIOSMEM_CURSOR_TYPE + 1, end,1); //Set end line!
}

void GPU_clearscreen() //Clears the screen!
{
	if (__HW_DISABLED) return; //Abort!
	byte oldmode;
	oldmode = MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURRENT_MODE,0,1); //Active video mode!
	MMU_wb(-1,BIOSMEM_SEG,BIOSMEM_CURRENT_MODE,oldmode&0x7F,1); //Clear!
	GPUswitchvideomode(MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURRENT_MODE,0,1)); //Reset the resolution!
	MMU_wb(-1,BIOSMEM_SEG,BIOSMEM_CURRENT_MODE,oldmode,1); //Restore old mode!
}

OPTINLINE void int10_nextcol(byte thepage)
{
	if (__HW_DISABLED) return; //Abort!
	byte x = MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(thepage*2),0,1);
	byte y = MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(thepage*2)+1,0,1);
	++x; //Next X!
	if ((word)x>=getscreenwidth()) //Overflow?
	{
		x = 0; //Reset!
		++y; //Next Y!
		if (y>=MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_NB_ROWS,0,1)) //Overflow?
		{
			y = 0; //Reset!
		}
	}
	EMU_CPU_setCursorXY(thepage,x,y); //Give x,y of cursor!
}

void cursorXY(byte displaypage, byte x, byte y)
{
	if (__HW_DISABLED) return; //Abort!
	EMU_CPU_setCursorXY(displaypage,x,y); //Give x,y of cursor!
}

























//Below read/writecharacter based upon: https://code.google.com/p/dosbox-wii/source/browse/trunk/src/ints/int10_char.cpp

OPTINLINE void int10_vram_writecharacter(byte x, byte y, byte page, byte character, byte attribute) //Write character+attribute!
{
	if (__HW_DISABLED) return; //Abort!
	switch (CurMode->type)
	{
	case M_TEXT: //Text mode?
		{
		//+ _4KB * vdupage + 160 * y + 2 * x
			uint_32 where = (CurMode->pstart>>4); //Position of the character, all above the fourth bit (pstart=segment<<4)!
			word address = (CurMode->pstart&0xF); //Rest address!
			address += (page*MMU_rw(-1,BIOSMEM_SEG,BIOSMEM_PAGE_SIZE,0,1)); //Start of page!
			address += (((y*MMU_rw(-1,BIOSMEM_SEG,BIOSMEM_NB_COLS,0,1))+x)<<1); //Character offset within page!
			MMU_wb(-1,where,address,character,1); //The character! Write plane 0!
			MMU_wb(-1,where,address+1,attribute,1); //The attribute! Write plane 1!
			return; //Done!
		}
		break;
	default:
		break; //Do nothing: unsupported yet!
	}
}

void int10_vram_readcharacter(byte x, byte y, byte page, byte *character, byte *attribute) //Read character+attribute!
{
	if (__HW_DISABLED) return; //Abort!
	switch (CurMode->type)
	{
	case M_TEXT: //Text mode?
		{
		//+ _4KB * vdupage + 160 * y + 2 * x
			uint_32 where = (CurMode->pstart>>4); //Position of the character, all above the fourth bit (pstart=segment<<4)!
			word address = (CurMode->pstart&0xF); //Rest address!
			address += page*MMU_rw(-1,BIOSMEM_SEG,BIOSMEM_PAGE_SIZE,0,1); //Start of page!
			address += (((y*MMU_rw(-1,BIOSMEM_SEG,BIOSMEM_NB_COLS,0,1))+x)<<1); //Character offset within page!
			*character = MMU_rb(-1,where,address,0,1); //The character!
			*attribute = MMU_rb(-1,where,address+1,0,1); //The attribute!
		}
		break;
	default:
		break; //Do nothing: unsupported yet!
	}
}

void emu_setactivedisplaypage(byte page) //Set active display page!
{
	if (__HW_DISABLED) return; //Abort!
	MMU_wb(-1,BIOSMEM_SEG,BIOSMEM_CURRENT_PAGE,page,1); //Active video page!
	MMU_ww(-1,BIOSMEM_SEG,BIOSMEM_CURRENT_START,page*MMU_rw(-1,BIOSMEM_SEG,BIOSMEM_PAGE_SIZE,0,1),1); //Display page offset!
//Now for the VGA!

	byte oldcrtc = PORT_IN_B(0x3D4); //Save old address!
	PORT_OUT_B(0x3D4,0xE); //Select high register!
	PORT_OUT_B(0x3D5,((MMU_rw(-1,BIOSMEM_SEG,BIOSMEM_CURRENT_START,0,1)>>8)&0xFF));  //High!
	PORT_OUT_B(0x3D4,0xF); //Select low register!
	PORT_OUT_B(0x3D5,(MMU_rw(-1,BIOSMEM_SEG,BIOSMEM_CURRENT_START,0,1)&0xFF)); //Low!
	PORT_OUT_B(0x3D4,oldcrtc); //Restore old CRTC register!
}

OPTINLINE byte emu_getdisplaypage()
{
	if (__HW_DISABLED) return 0; //Abort!
	return MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURRENT_PAGE,0,1); //Active page!
}

OPTINLINE void INT10_SetSingleDacRegister(Bit8u index,Bit8u red,Bit8u green,Bit8u blue) {
	IO_Write(VGAREG_DAC_WRITE_ADDRESS,(Bit8u)index);
	IO_Write(VGAREG_DAC_DATA,red);
	IO_Write(VGAREG_DAC_DATA,green);
	IO_Write(VGAREG_DAC_DATA,blue);
}

OPTINLINE void INT10_GetSingleDacRegister(Bit8u index,Bit8u * red,Bit8u * green,Bit8u * blue) {
	IO_Write(VGAREG_DAC_READ_ADDRESS,index);
	*red=IO_Read(VGAREG_DAC_DATA);
	*green=IO_Read(VGAREG_DAC_DATA);
	*blue=IO_Read(VGAREG_DAC_DATA);
}

OPTINLINE void INT10_SetDACBlock(Bit16u index,Bit16u count,PhysPt data) {
 	IO_Write(VGAREG_DAC_WRITE_ADDRESS,(Bit8u)index);
	for (;count>0;count--) {
		IO_Write(VGAREG_DAC_DATA,phys_readb(data++));
		IO_Write(VGAREG_DAC_DATA,phys_readb(data++));
		IO_Write(VGAREG_DAC_DATA,phys_readb(data++));
	}
}

OPTINLINE void INT10_GetDACBlock(Bit16u index,Bit16u count,PhysPt data) {
 	IO_Write(VGAREG_DAC_READ_ADDRESS,(Bit8u)index);
	for (;count>0;count--) {
		phys_writeb(data++,IO_Read(VGAREG_DAC_DATA));
		phys_writeb(data++,IO_Read(VGAREG_DAC_DATA));
		phys_writeb(data++,IO_Read(VGAREG_DAC_DATA));
	}
}

OPTINLINE void INT10_SelectDACPage(Bit8u function,Bit8u mode) {
	ResetACTL();
	IO_Write(VGAREG_ACTL_ADDRESS,0x10);
	Bit8u old10=IO_Read(VGAREG_ACTL_READ_DATA);
	if (!function) {		//Select paging mode
		if (mode) old10|=0x80;
		else old10&=0x7f;
		IO_Write(VGAREG_ACTL_WRITE_DATA,old10);
	} else {				//Select page
		IO_Write(VGAREG_ACTL_WRITE_DATA,old10);
		if (!(old10 & 0x80)) mode<<=2;
		mode&=0xf;
		IO_Write(VGAREG_ACTL_ADDRESS,0x14);
		IO_Write(VGAREG_ACTL_WRITE_DATA,mode);
	}
	IO_Write(VGAREG_ACTL_ADDRESS,32);		//Enable output and protect palette
}

OPTINLINE void INT10_GetDACPage(Bit8u* mode,Bit8u* page) {
	ResetACTL();
	IO_Write(VGAREG_ACTL_ADDRESS,0x10);
	Bit8u reg10=IO_Read(VGAREG_ACTL_READ_DATA);
	IO_Write(VGAREG_ACTL_WRITE_DATA,reg10);
	*mode=(reg10&0x80)?0x01:0x00;
	IO_Write(VGAREG_ACTL_ADDRESS,0x14);
	*page=IO_Read(VGAREG_ACTL_READ_DATA);
	IO_Write(VGAREG_ACTL_WRITE_DATA,*page);
	if(*mode) {
		*page&=0xf;
	} else {
		*page&=0xc;
		*page>>=2;
	}
}

OPTINLINE void INT10_SetPelMask(Bit8u mask) {
	IO_Write(VGAREG_PEL_MASK,mask);
}	

OPTINLINE void INT10_GetPelMask(Bit8u *mask) {
	*mask=IO_Read(VGAREG_PEL_MASK);
}	

void int10_SetVideoMode()
{
	/*
		AL=Video mode
		result: AL=Video mode flag/controller byte
	*/
	GPUswitchvideomode(REG_AL); //Switch the video mode!
}

void int10_SetTextModeCursorShape()
{
	/*
		CH=Scan row start
		CL=Scan row end
		If bit 5 is used on VGA: hide cursor; else determine by start>end.
	*/
	EMU_CPU_setCursorScanlines(REG_CH,REG_CL); //Set scanline start&end to off by making start higher than end!
}

void int10_SetCursorPosition()
{
	/*
		BH=Page Number
		DH=Row
		DL=Column
	*/
	cursorXY(REG_BH,REG_DL,REG_DH); //Goto x,y!
}

void int10_GetCursorPositionAndSize()
{
	/*
		BH=Page Number
		result:
		AX=0
		CH=Start scan line
		CL=End scan line
		DH=Row
		DL=Column
	*/
	REG_AX = 0;
	REG_DX = MMU_rw(-1,BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(REG_BH*2),0,1); //Cursor x,y!
	EMU_CPU_getCursorScanlines(&REG_CH,&REG_CL); //Scan lines of the cursor!
}

void int10_ReadLightPenPosition()
{
	//Not used on VGA systems!
	REG_AH = 0; //Invalid function!
}

void int10_SelectActiveDisplayPage()
{
	/*
		AL=Page Number
	*/
	emu_setactivedisplaypage(REG_AL); //Set!
}

void int10_ScrollDownWindow_real(byte linestoscroll, byte backgroundcolor, byte page, byte x1, byte y1, byte x2, byte y2)
{
	int x; //Current x!
	int y; //Current y!
	byte oldchar;
	byte oldattr;
	int rowstoclear;
	rowstoclear = linestoscroll; //Default!
	if (linestoscroll==0)
	{
		rowstoclear = (y2-y1)+1; /* Clear all! */
	}
	for (y=y2; y>=y1; --y) //Rows!
	{
		for (x=x1; x<x2; ++x) //Columns!
		{
			oldchar = 0;
			oldattr = backgroundcolor; //Init to off-screen!
			if (linestoscroll) //Get from coordinates (not clearing entire screen)?
			{
				if ((y-rowstoclear)>=y1) //Not at top of window (bottom fill empty)?
				{
					int10_vram_readcharacter(x,y-rowstoclear,page,&oldchar,&oldattr); //Use character rows above this one!
				}
			}
			int10_vram_writecharacter(x,y,page,oldchar,oldattr); //Set our character!
		}
	}
}

void int10_ScrollUpWindow_real(byte linestoscroll, byte backgroundcolor, byte page, byte x1, byte y1, byte x2, byte y2)
{
	int x; //Current x!
	int y; //Current y!
	byte oldchar;
	byte oldattr;
	int rowstoclear;
	rowstoclear = linestoscroll; //Default!
	if (linestoscroll == 0)
	{
		rowstoclear = (y2 - y1) + 1; /* Clear all! */
	}

	for (y = y1; y <= y2; ++y) //Rows top to bottom!
	{
		for (x = x1; x <= x2; ++x) //Columns!
		{
			oldchar = 0;
			oldattr = backgroundcolor; //Init to off-screen empty!
			if (linestoscroll) //Get from coordinates (not clearing entire screen)?
			{
				if ((y + rowstoclear)<=y2) //Not at bottom of window (bottom fill empty)?
				{
					int10_vram_readcharacter(x, y + rowstoclear, page, &oldchar, &oldattr); //Use character above this one!
				}
			}
			int10_vram_writecharacter(x, y, page, oldchar, oldattr); //Clear!
		}
	}
}

void int10_ScrollDownWindow() //Top off screen is lost, bottom goes up.
{
	/*
		AL=Lines to scroll (0=clear: CH,CL,DH,DL are used)
		BH=Background color

		CH=Upper row number
		DH=Lower row number
		CL=Left column number
		DL=Right column number
	*/
	int10_ScrollDownWindow_real(REG_AL,REG_BH,emu_getdisplaypage(),REG_CL,REG_CH,REG_DL,REG_DH); //Scroll down this window!
}

void int10_ScrollUpWindow() //Bottom off screen is lost, top goes down.
{
	/*
		AL=Lines to scroll (0=clear; CH,CL,DH,DL are used)
		BH=Background Color

		CH=Upper row number
		DH=Lower row number
		CL=Left column number
		DL=Right column number
	*/
	int10_ScrollUpWindow_real(REG_AL, REG_BH, emu_getdisplaypage(), REG_CL, REG_CH, REG_DL, REG_DH); //Scroll down this window!
}










void int10_ReadCharAttrAtCursor()
{
	/*
	BH=Page Number

	Result:
	AH=Color
	AL=Character!
	*/
	int10_vram_readcharacter(MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(REG_BH*2),0,1),
				 MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(REG_BH*2)+1,0,1),REG_BH,&REG_AL,&REG_AH); //Read character REG_AL font REG_AH from page!
}

void int10_WriteCharAttrAtCursor()
{
	/*
	AL=Character
	BH=Page Number
	BL=Color
	CX=Number of times to print character
	*/

	byte tempx,tempy;
	tempx = MMU_rb(-1, BIOSMEM_SEG, BIOSMEM_CURSOR_POS + (REG_BH * 2), 0,1); //Column!
	tempy = MMU_rb(-1, BIOSMEM_SEG, BIOSMEM_CURSOR_POS + (REG_BH * 2) + 1, 0,1); //Row!

	while (REG_CX--) //Times left?
	{
		int10_vram_writecharacter(
			MMU_rb(-1, BIOSMEM_SEG, BIOSMEM_CURSOR_POS + (REG_BH * 2), 0,1),
			MMU_rb(-1, BIOSMEM_SEG, BIOSMEM_CURSOR_POS + (REG_BH * 2) + 1, 0,1)
			, REG_BH, REG_AL, REG_BL); //Write character REG_AL font REG_BL at page!
		int10_nextcol(REG_BH); //Next column!
	}
	cursorXY(REG_BH, tempx, tempy); //Return the cursor to it's original position!
}

void int10_WriteCharOnlyAtCursor()
{
	/*
	AL=Character
	BH=Page Number
	CX=Number of times to print character
	*/

	word tempx, tempy;
	tempx = MMU_rb(-1, BIOSMEM_SEG, BIOSMEM_CURSOR_POS + (REG_BH * 2), 0,1); //Column!
	tempy = MMU_rb(-1, BIOSMEM_SEG, BIOSMEM_CURSOR_POS + (REG_BH * 2) + 1, 0,1); //Row!

	while (REG_CX--)
	{
		byte oldchar = 0;
		byte oldattr = 0;
		byte x = MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(REG_BH*2),0,1);
		byte y = MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(REG_BH*2)+1,0,1);
		int10_vram_readcharacter(x,y,REG_BH,&oldchar,&oldattr); //Get old info!
		int10_vram_writecharacter(x,y,REG_BH,REG_AL,oldattr); //Write character REG_AL with old font at page!
		int10_nextcol(REG_BH); //Next column!
	}

	cursorXY(REG_BH, (byte)tempx, (byte)tempy); //Return the cursor!
}

void int10_SetBackColor() //REG_AH=0B REG_BH=00h
{
	/*
	BL=Background/Border color (border only in text modes)
	*/
	
	PORT_IN_B(0x3DA); //Reset attribute controller!
	byte step7;
	step7 = PORT_IN_B(0x3C0); //Read and save original index!
	PORT_OUT_B(0x3C0,(step7&0x20)|0x11); //Goto index we need, leave toggle intact!
	PORT_OUT_B(0x3C0,REG_BL); //Write the value to use!
	PORT_OUT_B(0x3C0,step7); //Restore the index!
	byte oldindex;
	oldindex = PORT_IN_B(0x3B4); //Read current CRTC index!
	PORT_OUT_B(0x3B4,0x24); //Flipflop register!
	if (!(PORT_IN_B(0x3B5)&0x80)) //Flipflop is to be reset?
	{
		PORT_IN_B(0x3DA); //Reset the flip-flop!
	}
	PORT_OUT_B(0x3B4,oldindex); //Restore CRTC index!
}

void int10_SetPalette() //REG_AH=0B REG_BH!=00h
{
	/*
	BL=Palette ID (was only valid in CGA, but newer cards support it in many or all graphics modes)
	*/
	INT10_SetColorSelect(REG_BL); //Set the palette!
//???
}

void int10_Multi0B()
{
	if (!REG_BH)
	{
		int10_SetBackColor();
	}
	else //All other cases = function 01h!
	{
		int10_SetPalette();
	}
}

void int10_PutPixel()
{
	/*
	GRAPHICS
	AL=Color
	BH=Page Number
	CX=x
	DX=y
	*/

	GPU_putpixel(REG_CX,REG_DX,REG_BH,REG_AL); //Put the pixel, ignore result!
}

void int10_GetPixel()
{
	/*
	GRAPHICS
	BH=Page Number
	CX=x
	DX=y

	Returns:
	AL=Color
	*/

	word temp=0;
	GPU_getpixel(REG_CX,REG_DX,REG_BH,&temp); //Try to get the pixel, ignore result!
	REG_AL = (temp&0xFF); //Give the low 8-bits of the result!
}

void int10_internal_outputchar(byte videopage, byte character, byte attribute)
{
	switch (character) //What character?
	{
		//Control character?
	case 0x07: //Bell?
		//TODO BEEP
		break;
	case 0x08: //Backspace?
		if (MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(videopage*2),0,1)>0)
		{
			MMU_wb(-1,BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(videopage*2),MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(videopage*2),0,1)-1,1); //Decrease only!
		}
		EMU_CPU_setCursorXY(videopage,
					MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(videopage*2),0,1),
					MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(videopage*2)+1,0,1)); //Refresh x,y of cursor!
		break;
	case 0x09: //Tab (8 horizontal, 6 vertical)?
		do
		{
			int10_nextcol(videopage); //Next column!
		}
		while (MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(videopage*2),0,1)%8);   //Loop to next 8th position!
		break;
	case 0x0A: //LF?
		EMU_CPU_setCursorXY(videopage,
				    MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(videopage*2),0,1), //Same X!
				    MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(videopage*2)+1,0,1)+1 //Next row!
				    ); //Give x,y+1 of cursor!
		break;
	case 0x0B: //Vertical tab?
		do //Move some to the bottom!
		{
			int10_internal_outputchar(videopage,0x0A,attribute); //Next row!
		}
		while (MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(videopage*2)+1,0,1)%6);   //Loop to next 6th row!
		break;
	case 0x0D: //CR?
		EMU_CPU_setCursorXY(videopage,0,MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(videopage*2)+1,0,1)); //Give 0,y of cursor!
		break;
	default: //Normal character?
		int10_vram_writecharacter(MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(videopage*2),0,1),
					  MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(videopage*2)+1,0,1),
					  videopage,character,attribute); //Write character & font at page!
		int10_nextcol(videopage); //Next column!
		break;
	}
	byte maxrows = MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_NB_ROWS,0,1); //Maximum number of rows minus 1!
	++maxrows; //Row at which to scroll is one past maximum rows!
	for (;MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(videopage*2)+1,0,1)>=maxrows;) //Past limit: scroll one down!
	{
		byte currow = MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(videopage*2)+1,0,1); //Current row!
		switch (MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURRENT_MODE,0,1)) //Active video mode?
		{
		case 0:
		case 1:
		case 2:
		case 3:
		case 7:
			int10_ScrollUpWindow_real(1,attribute,videopage,0,0,MMU_rw(-1,BIOSMEM_SEG,BIOSMEM_NB_COLS,0,1)-1,
										MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_NB_ROWS,0,1)); //XxY rows?
			MMU_wb(-1,
				BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(videopage*2)+1, //Row=...
				currow-1, //One row up!
				1
				);
			break;
		default: //Not supported: graphics mode?
			return; //Abort scrolling: unsupported!
			break;
		}
	}
}

void int10_TeleTypeOutput()
{
	/*
	AL=Character
	BH=Page Number
	BL=Color (only in graphic mode)
	*/

	byte oldchar = 0;
	byte oldattr = 0;
	byte x = MMU_rb(-1, BIOSMEM_SEG, BIOSMEM_CURSOR_POS + (REG_BH * 2), 0,1);
	byte y = MMU_rb(-1, BIOSMEM_SEG, BIOSMEM_CURSOR_POS + (REG_BH * 2) + 1, 0,1);
	int10_vram_readcharacter(x, y, REG_BH, &oldchar, &oldattr); //Get old info!
	int10_internal_outputchar(REG_BH, REG_AL, oldattr); //Write character REG_AL with old font at page!
}

void int10_GetCurrentVideoMode()
{
	/*
	Returns:
	AH=Columns
	AL=Video Mode
	BH=Video page
	*/
	if (CurMode) //Valid mode set?
	{
		REG_AH = CurMode->twidth; //Text width in AH!
	}
	REG_AL = GPUgetvideomode(); //Give video mode!
	REG_BH = emu_getdisplaypage(); //Get the current display page!
}

void int10_WriteString()
{
	/*
	AL=Write mode
	BH=Page Number
	BL=Color
	CX=String length
	DH=Row
	DL=Column
	ES:BP=Offset of string
	*/
	byte c;
	byte x;
	byte y;

	word len; //Length!
	len = REG_CX; //The length of the string!
	word cur=0; //Current value!

	x = MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(REG_BH*2),0,1); //Old x!
	y = MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(REG_BH*2)+1,0,1); //Old y!

	while (len)
	{
		c = MMU_rb(CB_ISCallback()?CPU_segment_index(CPU_SEGMENT_ES):-1,REG_ES,REG_BP+cur,0,1); //Read character from memory!
		int10_internal_outputchar(REG_BH,c,REG_BL); //Output&update!
		--len; //Next item!
	}

	if (!(REG_AL&0x01)) //No Update cursor?
	{
		MMU_wb(-1,BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(REG_BH*2),x,1); //Restore x!
		MMU_wb(-1,BIOSMEM_SEG,BIOSMEM_CURSOR_POS+(REG_BH*2)+1,y,1); //Restore y!
	}
}

//Extra: EGA/VGA functions:

void int10_Pallette() //REG_AH=10h,REG_AL=subfunc
{
	switch (REG_AL) {
		case 0x00:							/* SET SINGLE PALETTE REGISTER */
			INT10_SetSinglePaletteRegister(REG_BL,REG_BH);
			break;
		case 0x01:							/* SET BORDER (OVERSCAN) COLOR*/
			INT10_SetOverscanBorderColor(REG_BH);
			break;
		case 0x02:							/* SET ALL PALETTE REGISTERS */
			INT10_SetAllPaletteRegisters(Real2Phys(RealMake(REG_ES,REG_DX)));
			break;
		case 0x03:							/* TOGGLE INTENSITY/BLINKING BIT */
			INT10_ToggleBlinkingBit(REG_BL);
			break;
		case 0x07:							/* GET SINGLE PALETTE REGISTER */
			INT10_GetSinglePaletteRegister(REG_BL,&REG_BH);
			break;
		case 0x08:							/* READ OVERSCAN (BORDER COLOR) REGISTER */
			INT10_GetOverscanBorderColor(&REG_BH);
			break;
		case 0x09:							/* READ ALL PALETTE REGISTERS AND OVERSCAN REGISTER */
			INT10_GetAllPaletteRegisters(Real2Phys(RealMake(REG_ES,REG_DX)));
			break;
		case 0x10:							/* SET INDIVIDUAL DAC REGISTER */
			INT10_SetSingleDacRegister(REG_BL,REG_DH,REG_CH,REG_CL);
			break;
		case 0x12:							/* SET BLOCK OF DAC REGISTERS */
			INT10_SetDACBlock(REG_BX,REG_CX,Real2Phys(RealMake(REG_ES,REG_DX)));
			break;
		case 0x13:							/* SELECT VIDEO DAC COLOR PAGE */
			INT10_SelectDACPage(REG_BL,REG_BH);
			break;
		case 0x15:							/* GET INDIVIDUAL DAC REGISTER */
			INT10_GetSingleDacRegister(REG_BL,&REG_DH,&REG_CH,&REG_CL);
			break;
		case 0x17:							/* GET BLOCK OF DAC REGISTER */
			INT10_GetDACBlock(REG_BX,REG_CX,Real2Phys(RealMake(REG_ES,REG_DX)));
			break;
		case 0x18:							/* undocumented - SET PEL MASK */
			INT10_SetPelMask(REG_BL);
			break;
		case 0x19:							/* undocumented - GET PEL MASK */
			INT10_GetPelMask(&REG_BL);
			REG_BH=0;	// bx for get mask
			break;
		case 0x1A:							/* GET VIDEO DAC COLOR PAGE */
			INT10_GetDACPage(&REG_BL,&REG_BH);
			break;
		case 0x1B:							/* PERFORM GRAY-SCALE SUMMING */
			INT10_PerformGrayScaleSumming(REG_BX,REG_CX);
			break;
		case 0xF0:							/* ET4000: SET HiColor GRAPHICS MODE */
		case 0xF1:							/* ET4000: GET DAC TYPE */
		case 0xF2:							/* ET4000: CHECK/SET HiColor MODE */
			if ((svgaCard == SVGA_TsengET4K) || (svgaCard == SVGA_TsengET3K)) { //Sierra Hi-Color DAC supported?
				switch (REG_AX) {
				case 0x10F0: /* ET4000: SET HiColor GRAPHICS MODE */
					if (INT10_Internal_SetVideoMode(0x200 | (word)(REG_BL)))
					{
						REG_AX = 0x0010;
					}
					break;
				case 0x10F1: /* ET4000: GET DAC TYPE */
					REG_AX = 0x0010;
					REG_BL = 0x01;
					break;
				case 0x10F2: /* ET4000: CHECK/SET HiColor MODE */
					 //Setup the Hi-color DAC!
					IO_Read(0x3C6);
					IO_Read(0x3C6);
					IO_Read(0x3C6);
					IO_Read(0x3C6); //Make the DAC register available!
					switch (REG_BL)
					{
					case 0:
						REG_AX = 0x0010;
						break;
					case 1: case 2:
						{
							Bit8u val = (REG_BL == 1) ? 0xa0 : 0xe0;
							if (val != IO_Read(0x3C6)) {
								IO_Write(0x3C6, val);
								REG_AX = 0x0010;
							}
						}
						break;
					}
					switch (IO_Read(0x3C6) & 0xc0)
					{
					case 0x80:
						REG_BL = 1;
						break;
					case 0xc0:
						REG_BL = 2;
						break;
					default:
						REG_BL = 0;
						break;
					}
					IO_Read(0x3C7); //Change the register back to normal VGA-compatible mode!
				}
			}
		default:
			break;
	}
}

OPTINLINE uint_32 RealGetVec(byte interrupt)
{
	word segment, offset;
	CPU_getint(interrupt,&segment,&offset);
	return RealMake(segment,offset); //Give vector!
}

void int10_CharacterGenerator() //REG_AH=11h,REG_AL=subfunc
{
	switch (REG_AL) {
/* Textmode calls */
	case 0x00:			/* Load user font */
	case 0x10:
		INT10_LoadFont(REG_ES,REG_BP,REG_AL==0x10,REG_CX,REG_DX,REG_BL,REG_BH);
		break;
	case 0x01:			/* Load 8x14 font */
	case 0x11:
		INT10_LoadFont(RealSeg(int10.rom.font_14),RealOff(int10.rom.font_14),REG_AL==0x11,256,0,0,14);
		break;
	case 0x02:			/* Load 8x8 font */
	case 0x12:
		INT10_LoadFont(RealSeg(int10.rom.font_8_first),RealOff(int10.rom.font_8_first),REG_AL==0x12,256,0,0,8);
		break;
	case 0x03:			/* Set Block Specifier */
		IO_Write(0x3c4,0x3);IO_Write(0x3c5,REG_BL);
		break;
	case 0x04:			/* Load 8x16 font */
	case 0x14:
		if (!IS_VGA_ARCH) break;
		INT10_LoadFont(RealSeg(int10.rom.font_16),RealOff(int10.rom.font_16),REG_AL==0x14,256,0,0,16);
		break;
/* Graphics mode calls */
	case 0x20:			/* Set User 8x8 Graphics characters */
		RealSetVec(0x1f,REG_ES,REG_BP);
		break;
	case 0x21:			/* Set user graphics characters */
		RealSetVec(0x43,REG_ES,REG_BP);
		real_writew(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT,REG_CX);
		goto graphics_chars;
	case 0x22:			/* Rom 8x14 set */
		RealSetVec(0x43,RealSeg(int10.rom.font_14),RealOff(int10.rom.font_14));
		real_writew(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT,14);
		goto graphics_chars;
	case 0x23:			/* Rom 8x8 double dot set */
		RealSetVec(0x43,RealSeg(int10.rom.font_8_first),RealOff(int10.rom.font_8_first));
		real_writew(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT,8);
		goto graphics_chars;
	case 0x24:			/* Rom 8x16 set */
		if (!IS_VGA_ARCH) break;
		RealSetVec(0x43,RealSeg(int10.rom.font_16),RealOff(int10.rom.font_16));
		real_writew(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT,16);
		goto graphics_chars;
graphics_chars:
		switch (REG_BL) {
		case 0x00:real_writeb(BIOSMEM_SEG,BIOSMEM_NB_ROWS,REG_DL-1);break;
		case 0x01:real_writeb(BIOSMEM_SEG,BIOSMEM_NB_ROWS,13);break;
		case 0x03:real_writeb(BIOSMEM_SEG,BIOSMEM_NB_ROWS,42);break;
		case 0x02:
		default:real_writeb(BIOSMEM_SEG,BIOSMEM_NB_ROWS,24);break;
		}
		break;
/* General */
	case 0x30:/* Get Font Information */
		switch (REG_BH) {
		case 0x00:	/* interupt 0x1f vector */
			{
				RealPt int_1f=RealGetVec(0x1f);
				segmentWritten(CPU_SEGMENT_ES,RealSeg(int_1f),0);
				REG_BP=RealOff(int_1f);
			}
			break;
		case 0x01:	/* interupt 0x43 vector */
			{
				RealPt int_43=RealGetVec(0x43);
				segmentWritten(CPU_SEGMENT_ES,RealSeg(int_43),0);
				REG_BP=RealOff(int_43);
			}
			break;
		case 0x02:	/* font 8x14 */
			segmentWritten(CPU_SEGMENT_ES,RealSeg(int10.rom.font_14),0);
			REG_BP=RealOff(int10.rom.font_14);
			break;
		case 0x03:	/* font 8x8 first 128 */
			segmentWritten(CPU_SEGMENT_ES,RealSeg(int10.rom.font_8_first),0);
			REG_BP=RealOff(int10.rom.font_8_first);
			break;
		case 0x04:	/* font 8x8 second 128 */
			segmentWritten(CPU_SEGMENT_ES,RealSeg(int10.rom.font_8_second),0);
			REG_BP=RealOff(int10.rom.font_8_second);
			break;
		case 0x05:	/* alpha alternate 9x14 */
			if (!IS_VGA_ARCH) break;
			segmentWritten(CPU_SEGMENT_ES,RealSeg(int10.rom.font_14_alternate),0);
			REG_BP=RealOff(int10.rom.font_14_alternate);
			break;
		case 0x06:	/* font 8x16 */
			if (!IS_VGA_ARCH) break;
			segmentWritten(CPU_SEGMENT_ES,RealSeg(int10.rom.font_16),0);
			REG_BP=RealOff(int10.rom.font_16);
			break;
		case 0x07:	/* alpha alternate 9x16 */
			if (!IS_VGA_ARCH) break;
			segmentWritten(CPU_SEGMENT_ES,RealSeg(int10.rom.font_16_alternate),0);
			REG_BP=RealOff(int10.rom.font_16_alternate);
			break;
		default:
			break;
		}
		if ((REG_BH<=7) /*|| (svgaCard==SVGA_TsengET4K)*/) {
			/*if (machine==MCH_EGA) {
				REG_CX=0x0e;
				REG_DL=0x18;
			} else {*/
				REG_CX=real_readw(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT);
				REG_DL=real_readb(BIOSMEM_SEG,BIOSMEM_NB_ROWS);
			//}
		}
		break;
	default:
		break;
	}
}

void int10_SpecialFunctions() //REG_AH=12h
{
	switch (REG_BL) {
	case 0x10:							/* Get EGA Information */
		REG_BH=(real_readw(BIOSMEM_SEG,BIOSMEM_CRTC_ADDRESS)==0x3B4)?0xFF:0x00;	
		REG_BL=3;	//256 kb
		REG_CL=real_readb(BIOSMEM_SEG,BIOSMEM_SWITCHES) & 0x0F;
		REG_CH=real_readb(BIOSMEM_SEG,BIOSMEM_SWITCHES) >> 4;
		break;
	case 0x20:							/* Set alternate printscreen */
		break;
	case 0x30:							/* Select vertical resolution */
		{   
			if (!IS_VGA_ARCH) break;
			/*if (svgaCard != SVGA_None) {
				if (REG_AL > 2) {
					REG_AL=0;		// invalid subfunction
					break;
				}
			}*/
			Bit8u modeset_ctl = real_readb(BIOSMEM_SEG,BIOSMEM_MODESET_CTL);
			Bit8u video_switches = real_readb(BIOSMEM_SEG,BIOSMEM_SWITCHES)&0xf0;
			switch(REG_AL) {
			case 0: // 200
				modeset_ctl &= 0xef;
				modeset_ctl |= 0x80;
				video_switches |= 8;	// ega normal/cga emulation
				break;
			case 1: // 350
				modeset_ctl &= 0x6f;
				video_switches |= 9;	// ega enhanced
				break;
			case 2: // 400
				modeset_ctl &= 0x6f;
				modeset_ctl |= 0x10;	// use 400-line mode at next mode set
				video_switches |= 9;	// ega enhanced
				break;
			default:
				modeset_ctl &= 0xef;
				video_switches |= 8;	// ega normal/cga emulation
				break;
			}
			real_writeb(BIOSMEM_SEG,BIOSMEM_MODESET_CTL,modeset_ctl);
			real_writeb(BIOSMEM_SEG,BIOSMEM_SWITCHES,video_switches);
			REG_AL=0x12;	// success
			break;
		}
	case 0x31:							/* Palette loading on modeset */
		{   
			if (!IS_VGA_ARCH) break;
			//if (svgaCard==SVGA_TsengET4K) REG_AL&=1;
			if (REG_AL>1) {
				REG_AL=0;		//invalid subfunction
				break;
			}
			Bit8u temp = real_readb(BIOSMEM_SEG,BIOSMEM_MODESET_CTL) & 0xf7;
			if (REG_AL&1) temp|=8;		// enable if al=0
			real_writeb(BIOSMEM_SEG,BIOSMEM_MODESET_CTL,temp);
			REG_AL=0x12;
			break;	
		}		
	case 0x32:							/* Video adressing */
		if (!IS_VGA_ARCH) break;
		//if (svgaCard==SVGA_TsengET4K) REG_AL&=1;
		if (REG_AL>1) REG_AL=0;		//invalid subfunction
		else REG_AL=0x12;			//fake a success call
		break;
	case 0x33: /* SWITCH GRAY-SCALE SUMMING */
		{   
			if (!IS_VGA_ARCH) break;
			//if (svgaCard==SVGA_TsengET4K) REG_AL&=1;
			if (REG_AL>1) {
				REG_AL=0;
				break;
			}
			Bit8u temp = real_readb(BIOSMEM_SEG,BIOSMEM_MODESET_CTL) & 0xfd;
			if (!(REG_AL&1)) temp|=2;		// enable if al=0
			real_writeb(BIOSMEM_SEG,BIOSMEM_MODESET_CTL,temp);
			REG_AL=0x12;
			break;	
		}		
	case 0x34: /* ALTERNATE FUNCTION SELECT (VGA) - CURSOR EMULATION */
		{   
			// bit 0: 0=enable, 1=disable
			if (!IS_VGA_ARCH) break;
			//if (svgaCard==SVGA_TsengET4K) REG_AL&=1;
			if (REG_AL>1) {
				REG_AL=0;
				break;
			}
			Bit8u temp = real_readb(BIOSMEM_SEG,BIOSMEM_VIDEO_CTL) & 0xfe;
			real_writeb(BIOSMEM_SEG,BIOSMEM_VIDEO_CTL,temp|REG_AL);
			REG_AL=0x12;
			break;	
		}		
	case 0x35:
		if (!IS_VGA_ARCH) break;
		REG_AL=0x12;
		break;
	case 0x36: {						/* VGA Refresh control */
		if (!IS_VGA_ARCH) break;
		IO_Write(0x3c4,0x1);
		Bit8u clocking = IO_Read(0x3c5);
		
		if (REG_AL==0) clocking &= ~0x20;
		else clocking |= 0x20;
		
		IO_Write(0x3c4,0x1);
		IO_Write(0x3c5,clocking);

		REG_AL=0x12; // success
		break;
	}
	default:
		/*if (machine!=MCH_EGA)*/ REG_AL=0;
		break;
	}
}

void int10_DCC() //REG_AH=1Ah
{
	if (REG_AL==0) {	// get dcc
		// walk the tables...
		RealPt vsavept=real_readd(BIOSMEM_SEG,BIOSMEM_VS_POINTER);
		RealPt svstable=real_readd(RealSeg(vsavept),RealOff(vsavept)+0x10);
		if (svstable) {
			RealPt dcctable=real_readd(RealSeg(svstable),RealOff(svstable)+0x02);
			Bit8u entries=real_readb(RealSeg(dcctable),RealOff(dcctable)+0x00);
			Bit8u idx=real_readb(BIOSMEM_SEG,BIOSMEM_DCC_INDEX);
			// check if index within range
			if (idx<entries) {
				Bit16u dccentry=real_readw(RealSeg(dcctable),RealOff(dcctable)+0x04+idx*2);
				if ((dccentry&0xff)==0) REG_BX=dccentry>>8;
				else REG_BX=dccentry;
			} else REG_BX=0xffff;
		} else REG_BX=0xffff;
		REG_AX=0x1A;	// high part destroyed or zeroed depending on BIOS
	} else if (REG_AL==1) {	// set dcc
		Bit8u newidx=0xff;
		// walk the tables...
		RealPt vsavept=real_readd(BIOSMEM_SEG,BIOSMEM_VS_POINTER);
		RealPt svstable=real_readd(RealSeg(vsavept),RealOff(vsavept)+0x10);
		if (svstable) {
			RealPt dcctable=real_readd(RealSeg(svstable),RealOff(svstable)+0x02);
			Bit8u entries=real_readb(RealSeg(dcctable),RealOff(dcctable)+0x00);
			if (entries) {
				Bitu ct;
				Bit16u swpidx=REG_BH|(REG_BL<<8);
				// search the ddc index in the dcc table
				for (ct=0; ct<entries; ct++) {
					Bit16u dccentry=real_readw(RealSeg(dcctable),RealOff(dcctable)+0x04+ct*2);
					if ((dccentry==REG_BX) || (dccentry==swpidx)) {
						newidx=(Bit8u)ct;
						break;
					}
				}
			}
		}

		real_writeb(BIOSMEM_SEG,BIOSMEM_DCC_INDEX,newidx);
		REG_AX=0x1A;	// high part destroyed or zeroed depending on BIOS
	}
}

OPTINLINE Bitu INT10_VideoState_GetSize(Bitu state) {
	// state: bit0=hardware, bit1=bios data, bit2=color regs/dac state
	if ((state&7)==0) return 0;

	Bitu size=0x20;
	if (state&1) size+=0x46;
	if (state&2) size+=0x3a;
	if (state&4) size+=0x303;
	if (size!=0) size=(size-1)/64+1;
	return size;
}

OPTINLINE bool INT10_VideoState_Save(Bitu state,RealPt buffer) {
	Bitu ct;
	if ((state&7)==0) return false;

	Bitu base_seg=RealSeg(buffer);
	Bitu base_dest=RealOff(buffer)+0x20;

	if (state&1)  {
		real_writew(base_seg,RealOff(buffer),base_dest);

		Bit16u crt_reg=real_readw(BIOSMEM_SEG,BIOSMEM_CRTC_ADDRESS);
		real_writew(base_seg,base_dest+0x40,crt_reg);

		real_writeb(base_seg,base_dest+0x00,IO_ReadB(0x3c4));
		real_writeb(base_seg,base_dest+0x01,IO_ReadB(0x3d4));
		real_writeb(base_seg,base_dest+0x02,IO_ReadB(0x3ce));
		IO_ReadB(crt_reg+6);
		real_writeb(base_seg,base_dest+0x03,IO_ReadB(0x3c0));
		real_writeb(base_seg,base_dest+0x04,IO_ReadB(0x3ca));

		// sequencer
		for (ct=1; ct<5; ct++) {
			IO_WriteB(0x3c4,ct);
			real_writeb(base_seg,base_dest+0x04+ct,IO_ReadB(0x3c5));
		}

		real_writeb(base_seg,base_dest+0x09,IO_ReadB(0x3cc));

		// crt controller
		for (ct=0; ct<0x19; ct++) {
			IO_WriteB(crt_reg,ct);
			real_writeb(base_seg,base_dest+0x0a+ct,IO_ReadB(crt_reg+1));
		}

		// attr registers
		for (ct=0; ct<4; ct++) {
			IO_ReadB(crt_reg+6);
			IO_WriteB(0x3c0,0x10+ct);
			real_writeb(base_seg,base_dest+0x33+ct,IO_ReadB(0x3c1));
		}

		// graphics registers
		for (ct=0; ct<9; ct++) {
			IO_WriteB(0x3ce,ct);
			real_writeb(base_seg,base_dest+0x37+ct,IO_ReadB(0x3cf));
		}

		// save some registers
		IO_WriteB(0x3c4,2);
		Bit8u crtc_2=IO_ReadB(0x3c5);
		IO_WriteB(0x3c4,4);
		Bit8u crtc_4=IO_ReadB(0x3c5);
		IO_WriteB(0x3ce,6);
		Bit8u gfx_6=IO_ReadB(0x3cf);
		IO_WriteB(0x3ce,5);
		Bit8u gfx_5=IO_ReadB(0x3cf);
		IO_WriteB(0x3ce,4);
		Bit8u gfx_4=IO_ReadB(0x3cf);

		// reprogram for full access to plane latches
		IO_WriteW(0x3c4,0x0f02);
		IO_WriteW(0x3c4,0x0704);
		IO_WriteW(0x3ce,0x0406);
		IO_WriteW(0x3ce,0x0105);
		mem_writeb(0xaffff,0);

		for (ct=0; ct<4; ct++) {
			IO_WriteW(0x3ce,0x0004+ct*0x100);
			real_writeb(base_seg,base_dest+0x42+ct,mem_readb(0xaffff));
		}

		// restore registers
		IO_WriteW(0x3ce,0x0004|(gfx_4<<8));
		IO_WriteW(0x3ce,0x0005|(gfx_5<<8));
		IO_WriteW(0x3ce,0x0006|(gfx_6<<8));
		IO_WriteW(0x3c4,0x0004|(crtc_4<<8));
		IO_WriteW(0x3c4,0x0002|(crtc_2<<8));

		for (ct=0; ct<0x10; ct++) {
			IO_ReadB(crt_reg+6);
			IO_WriteB(0x3c0,ct);
			real_writeb(base_seg,base_dest+0x23+ct,IO_ReadB(0x3c1));
		}
		IO_WriteB(0x3c0,0x20);

		base_dest+=0x46;
	}

	if (state&2)  {
		real_writew(base_seg,RealOff(buffer)+2,base_dest);

		real_writeb(base_seg,base_dest+0x00,mem_readb(0x410)&0x30);
		for (ct=0; ct<0x1e; ct++) {
			real_writeb(base_seg,base_dest+0x01+ct,mem_readb(0x449+ct));
		}
		for (ct=0; ct<0x07; ct++) {
			real_writeb(base_seg,base_dest+0x1f+ct,mem_readb(0x484+ct));
		}
		real_writed(base_seg,base_dest+0x26,mem_readd(0x48a));
		real_writed(base_seg,base_dest+0x2a,mem_readd(0x14));	// int 5
		real_writed(base_seg,base_dest+0x2e,mem_readd(0x74));	// int 1d
		real_writed(base_seg,base_dest+0x32,mem_readd(0x7c));	// int 1f
		real_writed(base_seg,base_dest+0x36,mem_readd(0x10c));	// int 43

		base_dest+=0x3a;
	}

	if (state&4)  {
		real_writew(base_seg,RealOff(buffer)+4,base_dest);

		Bit16u crt_reg=real_readw(BIOSMEM_SEG,BIOSMEM_CRTC_ADDRESS);

		IO_ReadB(crt_reg+6);
		IO_WriteB(0x3c0,0x14);
		real_writeb(base_seg,base_dest+0x303,IO_ReadB(0x3c1));

		Bitu dac_state=IO_ReadB(0x3c7)&1;
		Bitu dac_windex=IO_ReadB(0x3c8);
		if (dac_state!=0) dac_windex--;
		real_writeb(base_seg,base_dest+0x000,dac_state);
		real_writeb(base_seg,base_dest+0x001,dac_windex);
		real_writeb(base_seg,base_dest+0x002,IO_ReadB(0x3c6));

		for (ct=0; ct<0x100; ct++) {
			IO_WriteB(0x3c7,ct);
			real_writeb(base_seg,base_dest+0x003+ct*3+0,IO_ReadB(0x3c9));
			real_writeb(base_seg,base_dest+0x003+ct*3+1,IO_ReadB(0x3c9));
			real_writeb(base_seg,base_dest+0x003+ct*3+2,IO_ReadB(0x3c9));
		}

		IO_ReadB(crt_reg+6);
		IO_WriteB(0x3c0,0x20);

		base_dest+=0x303;
	}
	return true;
}

OPTINLINE bool INT10_VideoState_Restore(Bitu state,RealPt buffer) {
	Bitu ct;
	if ((state&7)==0) return false;

	Bit16u base_seg=RealSeg(buffer);
	Bit16u base_dest;

	if (state&1)  {
		base_dest=real_readw(base_seg,RealOff(buffer));
		Bit16u crt_reg=real_readw(base_seg,base_dest+0x40);

		// reprogram for full access to plane latches
		IO_WriteW(0x3c4,0x0704);
		IO_WriteW(0x3ce,0x0406);
		IO_WriteW(0x3ce,0x0005);

		IO_WriteW(0x3c4,0x0002);
		mem_writeb(0xaffff,real_readb(base_seg,base_dest+0x42));
		IO_WriteW(0x3c4,0x0102);
		mem_writeb(0xaffff,real_readb(base_seg,base_dest+0x43));
		IO_WriteW(0x3c4,0x0202);
		mem_writeb(0xaffff,real_readb(base_seg,base_dest+0x44));
		IO_WriteW(0x3c4,0x0402);
		mem_writeb(0xaffff,real_readb(base_seg,base_dest+0x45));
		IO_WriteW(0x3c4,0x0f02);
		mem_readb(0xaffff);

		IO_WriteW(0x3c4,0x0100);

		// sequencer
		for (ct=1; ct<5; ct++) {
			IO_WriteW(0x3c4,ct+(real_readb(base_seg,base_dest+0x04+ct)<<8));
		}

		IO_WriteB(0x3c2,real_readb(base_seg,base_dest+0x09));
		IO_WriteW(0x3c4,0x0300);
		IO_WriteW(crt_reg,0x0011);

		// crt controller
		for (ct=0; ct<0x19; ct++) {
			IO_WriteW(crt_reg,ct+(real_readb(base_seg,base_dest+0x0a+ct)<<8));
		}

		IO_ReadB(crt_reg+6);
		// attr registers
		for (ct=0; ct<4; ct++) {
			IO_WriteB(0x3c0,0x10+ct);
			IO_WriteB(0x3c0,real_readb(base_seg,base_dest+0x33+ct));
		}

		// graphics registers
		for (ct=0; ct<9; ct++) {
			IO_WriteW(0x3ce,ct+(real_readb(base_seg,base_dest+0x37+ct)<<8));
		}

		IO_WriteB(crt_reg+6,real_readb(base_seg,base_dest+0x04));
		IO_ReadB(crt_reg+6);

		// attr registers
		for (ct=0; ct<0x10; ct++) {
			IO_WriteB(0x3c0,ct);
			IO_WriteB(0x3c0,real_readb(base_seg,base_dest+0x23+ct));
		}

		IO_WriteB(0x3c4,real_readb(base_seg,base_dest+0x00));
		IO_WriteB(0x3d4,real_readb(base_seg,base_dest+0x01));
		IO_WriteB(0x3ce,real_readb(base_seg,base_dest+0x02));
		IO_ReadB(crt_reg+6);
		IO_WriteB(0x3c0,real_readb(base_seg,base_dest+0x03));
	}

	if (state&2)  {
		base_dest=real_readw(base_seg,RealOff(buffer)+2);

		mem_writeb(0x410,(mem_readb(0x410)&0xcf) | real_readb(base_seg,base_dest+0x00));
		for (ct=0; ct<0x1e; ct++) {
			mem_writeb(0x449+ct,real_readb(base_seg,base_dest+0x01+ct));
		}
		for (ct=0; ct<0x07; ct++) {
			mem_writeb(0x484+ct,real_readb(base_seg,base_dest+0x1f+ct));
		}
		mem_writed(0x48a,real_readd(base_seg,base_dest+0x26));
		mem_writed(0x14,real_readd(base_seg,base_dest+0x2a));	// int 5
		mem_writed(0x74,real_readd(base_seg,base_dest+0x2e));	// int 1d
		mem_writed(0x7c,real_readd(base_seg,base_dest+0x32));	// int 1f
		mem_writed(0x10c,real_readd(base_seg,base_dest+0x36));	// int 43
	}

	if (state&4)  {
		base_dest=real_readw(base_seg,RealOff(buffer)+4);

		Bit16u crt_reg=real_readw(BIOSMEM_SEG,BIOSMEM_CRTC_ADDRESS);

		IO_WriteB(0x3c6,real_readb(base_seg,base_dest+0x002));

		for (ct=0; ct<0x100; ct++) {
			IO_WriteB(0x3c8,ct);
			IO_WriteB(0x3c9,real_readb(base_seg,base_dest+0x003+ct*3+0));
			IO_WriteB(0x3c9,real_readb(base_seg,base_dest+0x003+ct*3+1));
			IO_WriteB(0x3c9,real_readb(base_seg,base_dest+0x003+ct*3+2));
		}

		IO_ReadB(crt_reg+6);
		IO_WriteB(0x3c0,0x14);
		IO_WriteB(0x3c0,real_readb(base_seg,base_dest+0x303));

		Bitu dac_state=real_readb(base_seg,base_dest+0x000);
		if (dac_state==0) {
			IO_WriteB(0x3c8,real_readb(base_seg,base_dest+0x001));
		} else {
			IO_WriteB(0x3c7,real_readb(base_seg,base_dest+0x001));
		}
	}

	return true;
}

OPTINLINE void INT10_GetFuncStateInformation(PhysPt save) {
	/* set static state pointer */
	mem_writed(Phys2Real(save),int10.rom.static_state);
	/* Copy BIOS Segment areas */
	Bit16u i;

	/* First area in Bios Seg */
	for (i=0;i<0x1e;i++) {
		mem_writeb(Phys2Real(save+0x4+i),real_readb(BIOSMEM_SEG,BIOSMEM_CURRENT_MODE+i));
	}
	/* Second area */
	mem_writeb(Phys2Real(save+0x22),real_readb(BIOSMEM_SEG,BIOSMEM_NB_ROWS)+1);
	for (i=1;i<3;i++) {
		mem_writeb(Phys2Real(save+0x22+i),real_readb(BIOSMEM_SEG,BIOSMEM_NB_ROWS+i));
	}
	/* Zero out rest of block */
	for (i=0x25;i<0x40;i++) mem_writeb(Phys2Real(save+i),0);
	/* DCC */
//	mem_writeb(save+0x25,real_readb(BIOSMEM_SEG,BIOSMEM_DCC_INDEX));
	Bit8u dccode = 0x00;
	RealPt vsavept=real_readd(BIOSMEM_SEG,BIOSMEM_VS_POINTER);
	RealPt svstable=real_readd(RealSeg(vsavept),RealOff(vsavept)+0x10);
	if (svstable) {
		RealPt dcctable=real_readd(RealSeg(svstable),RealOff(svstable)+0x02);
		Bit8u entries=real_readb(RealSeg(dcctable),RealOff(dcctable)+0x00);
		Bit8u idx=real_readb(BIOSMEM_SEG,BIOSMEM_DCC_INDEX);
		// check if index within range
		if (idx<entries) {
			Bit16u dccentry=real_readw(RealSeg(dcctable),RealOff(dcctable)+0x04+idx*2);
			if ((dccentry&0xff)==0) dccode=(Bit8u)((dccentry>>8)&0xff);
			else dccode=(Bit8u)(dccentry&0xff);
		}
	}
	mem_writeb(Phys2Real(save+0x25),dccode);

	Bit16u col_count=0;
	switch (CurMode->type) {
	case M_TEXT:
		if (CurMode->mode==0x7) col_count=1; else col_count=16;break; 
	case M_CGA2:
		col_count=2;break;
	case M_CGA4:
		col_count=4;break;
	case M_EGA:
		if (CurMode->mode==0x11 || CurMode->mode==0x0f) 
			col_count=2; 
		else 
			col_count=16;
		break; 
	case M_VGA:
		col_count=256;
		break;
	default:
		break;
	}
	/* Colour count */
	mem_writew(Phys2Real(save+0x27),col_count);
	/* Page count */
	mem_writeb(Phys2Real(save+0x29),CurMode->ptotal);
	/* scan lines */
	switch (CurMode->sheight) {
	case 200:
		mem_writeb(Phys2Real(save+0x2a),0);break;
	case 350:
		mem_writeb(Phys2Real(save+0x2a),1);break;
	case 400:
		mem_writeb(Phys2Real(save+0x2a),2);break;
	case 480:
		mem_writeb(Phys2Real(save+0x2a),3);break;
	default:
		break;
	};
	/* misc flags */
	if (CurMode->type==M_TEXT) mem_writeb(Phys2Real(save+0x2d),0x21);
	else mem_writeb(Phys2Real(save+0x2d),0x01);
	/* Video Memory available */
	mem_writeb(Phys2Real(save+0x31),3);
}

void int10_FuncStatus() //REG_AH=1Bh
{
	switch (REG_BX) {
	case 0x0000:
		INT10_GetFuncStateInformation(Real2Phys(RealMake(REG_ES,REG_DI)));
		REG_AL=0x1B;
		break;
	default:
		REG_AL=0;
		break;
	}
}

void int10_SaveRestoreVideoStateFns() //REG_AH=1Ch
{
	switch (REG_AL) {
		case 0: {
			Bitu ret=INT10_VideoState_GetSize(REG_CX);
			if (ret) {
				REG_AL=0x1c;
				REG_BX=(Bit16u)ret;
			} else REG_AL=0;
			}
			break;
		case 1:
			if (INT10_VideoState_Save(REG_CX,RealMake(REG_ES,REG_BX))) REG_AL=0x1c;
			else REG_AL=0;
			break;
		case 2:
			if (INT10_VideoState_Restore(REG_CX,RealMake(REG_ES,REG_BX))) REG_AL=0x1c;
			else REG_AL=0;
			break;
		default:
			/*if (svgaCard==SVGA_TsengET4K) reg_ax=0;
			else*/ REG_AL=0;
			break;
	}
}

//AH=4Fh,AL=subfunc; SVGA support?













OPTINLINE byte int2hex(byte b)
{
	byte translatetable[0x10] = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'}; //The Hexdecimal notation!
	return translatetable[b&0xF];
}

OPTINLINE byte bytetonum(byte b, byte nibble)
{
	if (!nibble) return int2hex(b&0xF); //Low nibble!
	return int2hex((b>>4)&0xF); //High nibble!
}

OPTINLINE void writehex(BIGFILE *f, byte num) //Write a number (byte) to a file!
{
	byte low = bytetonum(num,0); //Low!
	byte high = bytetonum(num,1); //High!
	emufwrite64(&high,1,sizeof(high),f); //High!
	emufwrite64(&low,1,sizeof(low),f); //High!
}





void int10_dumpscreen() //Dump screen to file!
{
	if (__HW_DISABLED) return; //Abort!
	int x;
	int y;
	BIGFILE *f;
	int firstrow = 1;
	f = emufopen64("INT10.TXT","w"); //Open file!
	byte displaypage;
	displaypage = MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURRENT_PAGE,0,1); //Active video page!

	writehex(f,MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_CURRENT_MODE,0,1)); //Current display mode!
	writehex(f,displaypage); //Write the display page first!
	writehex(f,(byte)getscreenwidth()); //Screen width!

	char lb[3];
	cleardata(&lb[0],sizeof(lb));
	safestrcpy(lb,sizeof(lb),"\r\n"); //Line break!

	emufwrite64(&lb,1,safe_strlen(lb,sizeof(lb)),f); //Write a line break first!

	for (y=0; y<MMU_rb(-1,BIOSMEM_SEG,BIOSMEM_NB_ROWS,0,1); y++) //Process rows!
	{
		if (!firstrow)
		{
			emufwrite64(&lb,1,safe_strlen(lb,sizeof(lb)),f); //Line break!
		}
		else
		{
			firstrow = 0; //Reset!
		}
		for (x=0; (word)x<getscreenwidth(); x++) //Process columns!
		{
			byte c,a; //Character&attribute!
			int10_vram_readcharacter(x,y,0,&c,&a);
			emufwrite64(&c,1,sizeof(c),f); //The character at the page!
		}
	}
	emufclose64(f); //Close the file!
}























void int10_refreshscreen() //Refresh a text-screen to debug screen on PSP!
{
//No debug screen!
}

Handler int10functions[] =
{
	int10_SetVideoMode, //00
	int10_SetTextModeCursorShape, //01
	int10_SetCursorPosition, //02
	int10_GetCursorPositionAndSize, //03
	int10_ReadLightPenPosition, //04
	int10_SelectActiveDisplayPage, //05
	int10_ScrollUpWindow, //06
	int10_ScrollDownWindow, //07
	int10_ReadCharAttrAtCursor, //08
	int10_WriteCharAttrAtCursor, //09
	int10_WriteCharOnlyAtCursor, //0A
	int10_Multi0B, //0B
	int10_PutPixel, //0C
	int10_GetPixel, //0D
	int10_TeleTypeOutput, //0E
	int10_GetCurrentVideoMode, //0F
	int10_Pallette, //10
	int10_CharacterGenerator, //11
	int10_SpecialFunctions, //12
	int10_WriteString //13
	,NULL, //14
	NULL, //15
	NULL, //16
	NULL, //17
	NULL, //18
	NULL, //19
	int10_DCC, //1A
	int10_FuncStatus, //1B
	int10_SaveRestoreVideoStateFns, //1C
}; //Function list!

void int10_tseng_enableExtensions() //Write the key for ET4000 access!
{
	PORT_OUT_B(0x3BF,0x03);
	PORT_OUT_B(0x3B8,0xA0); //Write the KEY, enabling the ET4000 extensions!
}

void int10_tseng_disableExtensions() //Write the key for disabling ET4000 access!
{
	PORT_OUT_B(0x3B8,0x29);
	PORT_OUT_B(0x3BF,0x01); //Write the KEY, disabling the ET4000 extensions!
}

void init_int10() //Initialises int10&VGA for usage!
{
//Initialise variables!
	switch (getActiveVGA()->enable_SVGA) //SVGA detection?
	{
		case 1: //ET4000?
			svgaCard = SVGA_TsengET4K; //ET4000 card!
			int10_tseng_enableExtensions(); //Always enable the extensions!
			break;
		case 2: //ET3000?
			svgaCard = SVGA_TsengET3K; //ET4000 card!
			int10_tseng_enableExtensions(); //Always enable the extensions!
			break;
		default: //VGA?
			svgaCard = SVGA_None; //No SVGA!
			break;
	}
	if (DAC_Use_BWMonitor(0xFF) || (MDAEMULATION_ENABLED(getActiveVGA()))) //Are we using a B/W monitor or MDA video adapter?
	{
		GPUswitchvideomode(7); //Init video mode #7(mono)!
	}
	else //Color monitor?
	{
		GPUswitchvideomode(3); //Init video mode #3(color)!
	}
}

void initint10() //Fully initialise interrupt 10h!
{
	int10loaded = TRUE; //Interrupt presets loaded!
	init_int10(); //Initialise!
}

void BIOS_int10() //Handler!
{
	if (!int10loaded) //First call?
	{
		initint10();
	}

	if (__HW_DISABLED) return; //Disabled!
//Now, handle the interrupt!

//First, function protection!

	int dohandle = 0;
	dohandle = (REG_AH<NUMITEMS(int10functions)); //handle?

	if (!dohandle) //Not within list to execute?
	{
		REG_AH = 0xFF; //Break!
		CALLBACK_SCF(1); //Set carry flag to indicate an error!
	}
	else //To handle?
	{
		if (int10functions[REG_AH]!=NULL) //Set?
		{
			int10functions[REG_AH](); //Run the function!
		}
		else
		{
			REG_AH = 0xFF; //Error: unknown command!
			CALLBACK_SCF(1); //Set carry flag to indicate an error!
		}
	}
}

void int10_BIOSInit() //Initisation of the BIOS routine!
{
	if (EMULATED_CPU<CPU_80286) //Good CPU? Otherwise, don't run(Protected mode CPUs)
	{
		INT10_SetupRomMemory(1); //Setup ROM memory with interrupts!
		INT10_StartBasicVideoParameterTable(); //Setup the basic Video Parameter table!
		CPU_setint(0x10, 0xC000, int10.rom.used); //Interrupt 10h overridable handler at the end of the VGA ROM!
		initint10(); //Enter interrupt 10h defaults for our video card!
	}
}