/*

Copyright (C) 2019 - 2021 Superfury

This file is part of The Common Emulator Framework.

The Common Emulator Framework is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

The Common Emulator Framework is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with The Common Emulator Framework.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef GPU_TEXT_H
#define GPU_TEXT_H

#include "..\commonemuframework\headers\types.h" //"headers/types.h" //Basic types!
#include "..\commonemuframework\headers\emu\gpu\gpu.h" //"headers/emu/gpu/gpu.h" //GPU support!
#include "..\commonemuframework\headers\support\zalloc.h" //"headers/support/zalloc.h" //Memory protections for our inline function!

//\n is newline? Else \r\n is newline!
#define USESLASHN 1

//We're using a 8x8 font!
#define GPU_TEXTSURFACE_HEIGHT (int)(PSP_SCREEN_ROWS>>3)
#define GPU_TEXTSURFACE_WIDTH (int)(PSP_SCREEN_COLUMNS>>3)
#define GPU_TEXTPIXELSY (GPU_TEXTSURFACE_HEIGHT<<3)
#define GPU_TEXTPIXELSX (GPU_TEXTSURFACE_WIDTH<<3)

//Flags:
//Dirty flag!
#define TEXTSURFACE_FLAG_DIRTY 1

typedef struct
{
	int x;
	int y;
} BACKLISTITEM; //Background list coordinate!

//SetXYclicked and printfclicked result bit values!
#define SETXYCLICKED_OK 1
#define SETXYCLICKED_CLICKED 2

typedef struct
{
//First, the text data (modified during write/read)!
byte text[GPU_TEXTSURFACE_HEIGHT][GPU_TEXTSURFACE_WIDTH]; //Text surface text!
uint_32 font[GPU_TEXTSURFACE_HEIGHT][GPU_TEXTSURFACE_WIDTH]; //Text surface font!
uint_32 border[GPU_TEXTSURFACE_HEIGHT][GPU_TEXTSURFACE_WIDTH]; //Text surface border!
uint_32 clickable[GPU_TEXTSURFACE_HEIGHT][GPU_TEXTSURFACE_WIDTH]; //Text surface click status!

//Dirty flags and rendered data (internal).
uint_32 notdirty[GPU_TEXTPIXELSY*512]; //This is non-dirty, so use this then!
uint_32 notbackground[((GPU_TEXTPIXELSY*512)+32)/32]; //Used with rounding up enough to the amount of data stored! Contains data to support notdirty!
byte fontpixels[GPU_TEXTPIXELSY*512]; //Font pixels when set(1), else 0! Aligned to a power of 2 for speedup!
byte clickablefinger[GPU_TEXTSURFACE_HEIGHT][GPU_TEXTSURFACE_WIDTH]; //Text surface click status for the used finger for this!

//List for checking for borders, set by allocator!
BACKLISTITEM backlist[8]; //List of border background positions candidates!
int x,y; //Coordinates currently!
byte flags; //Extra flags for a surface!
byte xdelta, ydelta; //Enable delta coordinates during plotting?
SDL_sem *lock; //For locking the surface!

//Precalculated data for speeding up rendering!
word curXDELTA, curYDELTA; //X/Y delta values of the precalcs!
float cur_render_xfactor, cur_render_yfactor; //Render xfactor/yfactor used(implying reverse as well)!
uint_32 *horizontalprecalcs, *verticalprecalcs; //Our precalcs used when rendering!
uint_32 horizontalprecalcssize, verticalprecalcssize; //Our precalcs sizes!
uint_32 horizontalprecalcsentries, verticalprecalcsentries; //Precalcssize in entries!
byte precalcsready; //Precalcs loaded at least once?
} GPU_TEXTSURFACE;

//Allocation/deallocation!
GPU_TEXTSURFACE *alloc_GPUtext(); //Allocates a GPU text overlay!
void free_GPUtext(GPU_TEXTSURFACE **surface); //Frees an allocated GPU text overlay!
void freeTextSurfacePrecalcs(GPU_TEXTSURFACE* ptr); //Frees just the precalcs!

//Normal rendering/text functions!
uint_64 GPU_textrenderer(void *surface); //Run the text rendering on pspsurface, result is the ammount of ms taken!
int GPU_textgetxy(GPU_TEXTSURFACE *surface,int x, int y, byte *character, uint_32 *font, uint_32 *border); //Read a character+attribute!
int GPU_textsetxy(GPU_TEXTSURFACE *surface,int x, int y, byte character, uint_32 font, uint_32 border); //Write a character+attribute!
int GPU_textsetxyignoreclickable(GPU_TEXTSURFACE* surface, int x, int y, byte character, uint_32 font, uint_32 border); //Write a character+attribute!
int GPU_textsetxyfont(GPU_TEXTSURFACE *surface, int x, int y, uint_32 font, uint_32 border); //Write a attribute only!
void GPU_textprintf(GPU_TEXTSURFACE *surface, uint_32 font, uint_32 border, char *text, ...); //Write a string on the debug screen!
void GPU_textgotoxy(GPU_TEXTSURFACE *surface,int x, int y); //Goto coordinates!
void GPU_textclearrow(GPU_TEXTSURFACE *surface, int y); //Clear a row!
void GPU_textclearcurrentrownext(GPU_TEXTSURFACE *surface); //For clearing the rest of the current row!
void GPU_textclearscreen(GPU_TEXTSURFACE *surface); //Clear a text screen!
#define GPU_textdirty(surface) memprotect(surface, sizeof(GPU_TEXTSURFACE), NULL)?(((GPU_TEXTSURFACE *)surface)->flags&TEXTSURFACE_FLAG_DIRTY):0
void GPU_enableDelta(GPU_TEXTSURFACE *surface, byte xdelta, byte ydelta); //Enable delta coordinates on the x/y axis!
void GPU_text_updatedelta(SDL_Surface *surface); //Update delta!

void GPU_text_locksurface(GPU_TEXTSURFACE *surface); //Lock a surface for usage!
void GPU_text_releasesurface(GPU_TEXTSURFACE *surface); //Unlock a surface when we're done with it!

//GPU Clicking support!
byte GPU_textbuttondown(GPU_TEXTSURFACE *surface, byte finger, word x, word y); //We've been clicked at these coordinates!
void GPU_textbuttonup(GPU_TEXTSURFACE *surface, byte finger, word x, word y); //We've been released at these coordinates!
byte GPU_textpriority(GPU_TEXTSURFACE *surface, word x, word y); //We've been released at these coordinates!

//TEXT Clicking support!
byte GPU_textsetxyclickable(GPU_TEXTSURFACE *surface, int x, int y, byte character, uint_32 font, uint_32 border, byte ignoreempty); //Set x/y coordinates for clickable character! Result is bit value of SETXYCLICKED_*
byte GPU_textprintfclickable(GPU_TEXTSURFACE *surface, uint_32 font, uint_32 border, byte ignoreempty, char *text, ...); //Same as normal GPU_textprintf, but with clickable support! Result is bit value of SETXYCLICKED_*
byte GPU_ispressed(GPU_TEXTSURFACE *surface, word x, word y); //Are we pressed?
#endif
