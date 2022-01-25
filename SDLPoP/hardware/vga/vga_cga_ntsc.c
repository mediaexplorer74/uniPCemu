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
#include "headers/hardware/vga/vga.h" //VGA/CGA support!
#include "headers/header_dosbox.h" //Dosbox support!
#include "headers/hardware/vga/vga_cga_ntsc.h" //Our own definitions!
#include "headers/fopen64.h"

//Main functions for rendering NTSC and RGBI by superfury:

//Little fix for the code!
#ifdef OUT
#undef OUT
#endif

byte CGA_RGB = 1; //Are we a RGB monitor(1) or Composite monitor(0)?

byte cga_color_burst = 1; //Color burst!

//Simple patch for New-style CGA rendering of PCEm-X!
#define new_cga New_CGA

uint_32 templine[2048]; //Our temporary line!

//Dosbox conversion function itself, Converted from PCEm-X(Parameters added with information from the emulated CGA): https://github.com/OBattler/PCem-X/blob/master/PCem/vid_cga_comp.c
//Some defines to make us easier to work with for patching the code:
#define CGA_MODECONTROL getActiveVGA()->registers->Compatibility_CGAModeControl

//Finally, the code and rest support!

int CGA_Composite_Table[1024];

DOUBLE brightness = 0;
DOUBLE contrast = 100;
DOUBLE saturation = 100;
DOUBLE sharpness = 0;
DOUBLE hue_offset = 0;

// New algorithm by reenigne
// Works in all CGA modes/color settings and can simulate older and newer CGA revisions

const DOUBLE tau = 6.28318531; // == 2*pi

unsigned char chroma_multiplexer[256] = {
	  2,  2,  2,  2, 114,174,  4,  3,   2,  1,133,135,   2,113,150,  4,
	133,  2,  1, 99, 151,152,  2,  1,   3,  2, 96,136, 151,152,151,152,
	  2, 56, 62,  4, 111,250,118,  4,   0, 51,207,137,   1,171,209,  5,
	140, 50, 54,100, 133,202, 57,  4,   2, 50,153,149, 128,198,198,135,
	 32,  1, 36, 81, 147,158,  1, 42,  33,  1,210,254,  34,109,169, 77,
	177,  2,  0,165, 189,154,  3, 44,  33,  0, 91,197, 178,142,144,192,
	  4,  2, 61, 67, 117,151,112, 83,   4,  0,249,255,   3,107,249,117,
	147,  1, 50,162, 143,141, 52, 54,   3,  0,145,206, 124,123,192,193,
	 72, 78,  2,  0, 159,208,  4,  0,  53, 58,164,159,  37,159,171,  1,
	248,117,  4, 98, 212,218,  5,  2,  54, 59, 93,121, 176,181,134,130,
	  1, 61, 31,  0, 160,255, 34,  1,   1, 58,197,166,   0,177,194,  2,
	162,111, 34, 96, 205,253, 32,  1,   1, 57,123,125, 119,188,150,112,
	 78,  4,  0, 75, 166,180, 20, 38,  78,  1,143,246,  42,113,156, 37,
	252,  4,  1,188, 175,129,  1, 37, 118,  4, 88,249, 202,150,145,200,
	 61, 59, 60, 60, 228,252,117, 77,  60, 58,248,251,  81,212,254,107,
	198, 59, 58,169, 250,251, 81, 80, 100, 58,154,250, 251,252,252,252};

#ifdef IS_LONGDOUBLE
DOUBLE intensity[4] = {
	77.175381L, 88.654656L, 166.564623L, 174.228438L};

#define NEW_CGA(c,i,r,g,b) (((c)/0.72L)*0.29L + ((i)/0.28L)*0.32L + ((r)/0.28L)*0.1L + ((g)/0.28L)*0.22L + ((b)/0.28L)*0.07L)
#else
DOUBLE intensity[4] = {
	77.175381, 88.654656, 166.564623, 174.228438};

#define NEW_CGA(c,i,r,g,b) (((c)/0.72)*0.29 + ((i)/0.28)*0.32 + ((r)/0.28)*0.1 + ((g)/0.28)*0.22 + ((b)/0.28)*0.07)
#endif

DOUBLE mode_brightness;
DOUBLE mode_contrast;
DOUBLE mode_hue;
DOUBLE min_v;
DOUBLE max_v;

DOUBLE video_ri, video_rq, video_gi, video_gq, video_bi, video_bq;
int video_sharpness;
//int tandy_mode_control = 0; //Uses VGA directly by superfury

bool new_cga = 0;
//static bool is_bw = 0; //Superfury: not used!
//static bool is_bpp1 = 0; //Superfury: not used!

//static uint8_t comp_pal[256][3];

//static Bit8u byte_clamp_other(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); } //Superfury: defined but not used!

BIGFILE *df;

OPTINLINE void update_cga16_color() { //Superfury: Removed the parameter: we access the emulation directly!
	int x;

        if (!new_cga) {
                min_v = chroma_multiplexer[0] + intensity[0];
                max_v = chroma_multiplexer[255] + intensity[3];
        }
        else {
                DOUBLE i0 = intensity[0];
                DOUBLE i3 = intensity[3];
                min_v = NEW_CGA(chroma_multiplexer[0], i0, i0, i0, i0);
                max_v = NEW_CGA(chroma_multiplexer[255], i3, i3, i3, i3);
        }
        mode_contrast = 256/(max_v - min_v);
        mode_brightness = -min_v*mode_contrast;
        if ((CGA_MODECONTROL & 3) == 1)
                mode_hue = 14;
        else
                mode_hue = 4;
		#ifdef IS_LONGDOUBLE
        mode_contrast *= contrast * (new_cga ? 1.2L : 1.0L)/100.0L;             // new CGA: 120%
		#else
        mode_contrast *= contrast * (new_cga ? 1.2 : 1)/100;             // new CGA: 120%
		#endif
        mode_brightness += (new_cga ? brightness-10 : brightness)*5;     // new CGA: -10
		#ifdef IS_LONGDOUBLE
        DOUBLE mode_saturation = (new_cga ? 4.35L : 2.9L)*saturation/100.0L;  // new CGA: 150%
		#else
        DOUBLE mode_saturation = (new_cga ? 4.35 : 2.9)*saturation/100.0;  // new CGA: 150%
		#endif

        for (x = 0; x < 1024; ++x) {
                int phase = x & 3;
                int right = (x >> 2) & 15;
                int left = (x >> 6) & 15;
                int rc = right;
                int lc = left;
                if ((CGA_MODECONTROL & 4) != 0) {
                        rc = (right & 8) | ((right & 7) != 0 ? 7 : 0);
                        lc = (left & 8) | ((left & 7) != 0 ? 7 : 0);
                }
                DOUBLE c =
                        chroma_multiplexer[((lc & 7) << 5) | ((rc & 7) << 2) | phase];
                DOUBLE i = intensity[(left >> 3) | ((right >> 2) & 2)];
                DOUBLE v;
                if (!new_cga)
                        v = c + i;
                else {
                        DOUBLE r = intensity[((left >> 2) & 1) | ((right >> 1) & 2)];
                        DOUBLE g = intensity[((left >> 1) & 1) | (right & 2)];
                        DOUBLE b = intensity[(left & 1) | ((right << 1) & 2)];
                        v = NEW_CGA(c, i, r, g, b);
                }
                CGA_Composite_Table[x] = (int) (v*mode_contrast + mode_brightness);
        }

        DOUBLE i = CGA_Composite_Table[6*68] - CGA_Composite_Table[6*68 + 2];
        DOUBLE q = CGA_Composite_Table[6*68 + 1] - CGA_Composite_Table[6*68 + 3];

		#ifdef IS_LONGDOUBLE
        DOUBLE a = tau*(33 + 90 + hue_offset + mode_hue)/360.0L;
		#else
        DOUBLE a = tau*(33 + 90 + hue_offset + mode_hue)/360.0;
		#endif
        DOUBLE c = cos(a);
        DOUBLE s = sin(a);
        DOUBLE r = 256*mode_saturation/sqrt(i*i+q*q);

        DOUBLE iq_adjust_i = -(i*c + q*s)*r;
        DOUBLE iq_adjust_q = (q*c - i*s)*r;

        static const DOUBLE ri = 0.9563;
        static const DOUBLE rq = 0.6210;
        static const DOUBLE gi = -0.2721;
        static const DOUBLE gq = -0.6474;
        static const DOUBLE bi = -1.1069;
        static const DOUBLE bq = 1.7046;

        video_ri = (int) (ri*iq_adjust_i + rq*iq_adjust_q);
        video_rq = (int) (-ri*iq_adjust_q + rq*iq_adjust_i);
        video_gi = (int) (gi*iq_adjust_i + gq*iq_adjust_q);
        video_gq = (int) (-gi*iq_adjust_q + gq*iq_adjust_i);
        video_bi = (int) (bi*iq_adjust_i + bq*iq_adjust_q);
        video_bq = (int) (-bi*iq_adjust_q + bq*iq_adjust_i);
        video_sharpness = (int) (sharpness*256/100);

#if 0
	df = emufopen64("CGA_Composite_Table.dmp", "wb");
	emufwrite64(CGA_Composite_Table, 1024, sizeof(int), df);
	emufclose64(df);
#endif
}

#if 0
void configure_comp(DOUBLE h, uint8_t n, uint8_t bw, uint8_t b1)
{
	hue_offset = h;
	new_cga = n;
	is_bw = bw;
	is_bpp1 = b1;
}
#endif

OPTINLINE Bit8u byte_clamp(int v) {
        v >>= 13;
        return v < 0 ? 0 : (v > 255 ? 255 : v);
}

//Just leave the scaler size to it's original size (maximum CGA display width)!
#define SCALER_MAXWIDTH 2048

int temp[SCALER_MAXWIDTH + 10]={0};
int atemp[SCALER_MAXWIDTH + 2]={0};
int btemp[SCALER_MAXWIDTH + 2]={0};

OPTINLINE void Composite_Process(Bit8u border, Bit32u blocks/*, bool doublewidth*/, Bit8u *TempLine) //Superfury: Used to return a pointer(not used?). Replaced with void.
{
	int x;
	Bit32u x2;

        int w = blocks*4;

/* PCem-X's CGA code already accounts for that before feeding the buffer to processing. */
#if 0
        if (doublewidth) {
                Bit8u * source = TempLine + w - 1;
                Bit8u * dest = TempLine + w*2 - 2;
                for (x = 0; x < w; ++x) {
                        *dest = *source;
                        *(dest + 1) = *source;
                        --source;
                        dest -= 2;
                }
                blocks *= 2;
                w *= 2;
        }
#endif

#define COMPOSITE_CONVERT(I, Q) { \
        i[1] = (i[1]<<3) - ap[1]; \
        a = ap[0]; \
        b = bp[0]; \
        c = i[0]+i[0]; \
        d = i[-1]+i[1]; \
        y = ((c+d)<<8) + (int)(video_sharpness*(DOUBLE)(c-d)); \
        rr = y + (int)(video_ri*(I)) + (int)(video_rq*(Q)); \
        gg = y + (int)(video_gi*(I)) + (int)(video_gq*(Q)); \
        bb = y + (int)(video_bi*(I)) + (int)(video_bq*(Q)); \
        ++i; \
        ++ap; \
        ++bp; \
        *srgb = RGB(byte_clamp(rr),byte_clamp(gg),byte_clamp(bb)); \
        ++srgb; \
}

#define OUT(v) { *o = (v); ++o; }

        // Simulate CGA composite output
        int* o = temp;
        Bit8u* rgbi = TempLine;
        int* b2 = &CGA_Composite_Table[border*68];
        for (x = 0; x < 4; ++x)
                OUT(b2[(x+3)&3]);
        OUT(CGA_Composite_Table[(border<<6) | ((*rgbi)<<2) | 3]);
        for (x = 0; x < w-1; ++x) {
                OUT(CGA_Composite_Table[(rgbi[0]<<6) | (rgbi[1]<<2) | (x&3)]);
                ++rgbi;
        }
        OUT(CGA_Composite_Table[((*rgbi)<<6) | (border<<2) | 3]);
        for (x = 0; x < 5; ++x)
                OUT(b2[x&3]);

        if ((CGA_MODECONTROL & 4) != 0 || !cga_color_burst) {
                // Decode
                int* i = temp + 5;
                Bit32u* srgb = (Bit32u *)TempLine;
                for (x2 = 0; x2 < blocks*4; ++x2) {
                        INLINEREGISTER int c = (i[0]+i[0])<<3;
						INLINEREGISTER int d = (i[-1]+i[1])<<3;
						INLINEREGISTER int y = ((c+d)<<8) + video_sharpness*(c-d);
                        ++i;
                        *srgb = byte_clamp(y)*0x10101;
                        ++srgb;
                }
        }
        else {
                // Store chroma
                int* i = temp + 4;
                int* ap = atemp + 1;
                int* bp = btemp + 1;
                for (x = -1; x < w + 1; ++x) {
                        ap[x] = i[-4]-((i[-2]-i[0]+i[2])<<1)+i[4];
                        bp[x] = (i[-3]-i[-1]+i[1]-i[3])<<1;
                        ++i;
                }

                // Decode
                i = temp + 5;
                i[-1] = (i[-1]<<3) - ap[-1];
                i[0] = (i[0]<<3) - ap[0];
                Bit32u* srgb = (Bit32u *)TempLine;
                for (x2 = 0; x2 < blocks; ++x2) {
					INLINEREGISTER int y,a,b,c,d,rr,gg,bb;
                        COMPOSITE_CONVERT(a, b);
                        COMPOSITE_CONVERT(-b, a);
                        COMPOSITE_CONVERT(-a, -b);
                        COMPOSITE_CONVERT(b, -a);
                }
        }
#undef COMPOSITE_CONVERT
#undef OUT

} //Don't return the result: it's already known!

//Remaining support by superfury for updating CGA color registers!

void RENDER_updateCGAColors() //Update CGA rendering NTSC vs RGBI conversion!
{
	if (!CGA_RGB) update_cga16_color(); //Update us if we're used!
}

void setCGA_NTSC(byte enabled) //Use NTSC CGA signal output?
{
	byte needupdate = 0;
	needupdate = (CGA_RGB^(enabled?0:1)); //Do we need to update the palette?
	CGA_RGB = enabled?0:1; //RGB or NTSC monitor!
	if (needupdate) RENDER_updateCGAColors(); //Update colors if we're changed!
}

void setCGA_NewCGA(byte enabled)
{
	byte needupdate = 0;
	needupdate = (New_CGA^(enabled?1:0)); //Do we need to update the palette?
	New_CGA = enabled?1:0; //Use New Style CGA as set with protection?
	if (needupdate) RENDER_updateCGAColors(); //Update colors if we're changed!
}

//Our main rendering functions for the RGB/NTSC modes!
//RGBI conversion

byte cga_use_brown = 1; //Halve yellow's green signal to get brown on color monitors?

OPTINLINE uint_32 getCGAcol16(byte color) //Special for the emulator, like the keyboard presets etc.!
{
	switch (color&0xF)
	{
		case 1: return RGB(0x00,0x00,0xAA);
		case 2: return RGB(0x00,0xAA,0x00);
		case 3: return RGB(0x00,0xAA,0xAA);
		case 4: return RGB(0xAA,0x00,0x00);
		case 5: return RGB(0xAA,0x00,0xAA);
		case 6: return cga_use_brown?RGB(0xAA,0x55,0x00):RGB(0xAA,0xAA,0x00); //Halved green to get brown instead of yellow on new monitors, old monitors still display dark yellow(not halved)!
		case 7: return RGB(0xAA,0xAA,0xAA);
		case 8: return RGB(0x55,0x55,0x55);
		case 9: return RGB(0x55,0x55,0xFF);
		case 0xA: return RGB(0x55,0xFF,0x55);
		case 0xB: return RGB(0x55,0xFF,0xFF);
		case 0xC: return RGB(0xFF,0x55,0x55);
		case 0xD: return RGB(0xFF,0x55,0xFF);
		case 0xE: return RGB(0xFF,0xFF,0x55);
		case 0xF: return RGB(0xFF,0xFF,0xFF);
		case 0:
		default:
			 return RGB(0x00,0x00,0x00);
	}
	return RGB(0x00,0x00,0x00); //Shouldn't be here, but just in case!
}

OPTINLINE void RENDER_convertRGBI(byte *pixels, uint_32 *renderdestination, uint_32 size) //Convert a row of data to NTSC output!
{
	uint_32 current;
	for (current=0;likely(current<size);current++) //Process all pixels!
		renderdestination[current] = getCGAcol16(pixels[current]); //Just use the CGA RGBI colors!
}

//NTSC conversion
OPTINLINE void RENDER_convertNTSC(byte *pixels, uint_32 *renderdestination, uint_32 size) //Convert a row of data to NTSC output!
{
	memcpy(renderdestination,pixels,size); //Copy the pixels to the display to convert!
	Composite_Process(0,size>>2,(uint8_t *)renderdestination); //Convert to NTSC composite!
}

//Functions to call to update our data and render it according to our settings!
void RENDER_convertCGAOutput(byte *pixels, uint_32 *renderdestination, uint_32 size) //Convert a row of data to NTSC output!
{
	if (CGA_RGB) //RGB monitor?
	{
		RENDER_convertRGBI(pixels, renderdestination, size); //Convert the pixels as RGBI!
	}
	else //NTSC monitor?
	{
		RENDER_convertNTSC(pixels, renderdestination, size); //Convert the pixels as NTSC!
	}
}