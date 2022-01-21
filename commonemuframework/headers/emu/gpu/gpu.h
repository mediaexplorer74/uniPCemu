//This file is part of The Common Emulator Framework.


#ifndef GPU_H
#define GPU_H

#include "..\commonemuframework\headers\types.h" // "headers/types.h" //Global types
#include "headers/bios/bios.h" //BIOS Settings support (for VIDEO_DIRECT)

//Resolution of PSP Screen!
#define PSP_SCREEN_ROWS 272
#define PSP_SCREEN_COLUMNS 480

//Maximum ammount of display pages used by the GPU
#define PC_MAX_DISPLAYPAGES 8
#ifndef __psp__
//Use full 4MP resolution!
//Maximum resolution X (row size)
#define EMU_MAX_X 4096
//Maximum resolution Y (number of rows max). Not so many rows are used usually(up to 1024, depending on the mode), so keep it small this way.
#ifdef IS_VITA
//Reduced rows on Vita!
#define EMU_MAX_Y 1280
#else
#define EMU_MAX_Y 4096
#endif
//Buffer info:
#define EMU_BUFFER(x,y) GPU.emu_screenbuffer[(y<<12)|x]
//The pitch of the pixel buffer rows!
#define EMU_BUFFERPITCH 4096
#define EMU_SCREENBUFFERSIZE (EMU_MAX_Y<<12) //Video buffer (of max 1024x1024 pixels!)
#else
//PSP has a more limited resolution, to save memory(1MP).
//Maximum resolution X (row size)
#define EMU_MAX_X 1024
//Maximum resolution Y (number of rows max)
#define EMU_MAX_Y 1024
#define EMU_BUFFER(x,y) GPU.emu_screenbuffer[(y<<10)|x]
//The pitch of the pixel buffer rows!
#define EMU_BUFFERPITCH 1024
#define EMU_SCREENBUFFERSIZE (EMU_MAX_Y<<10) //Video buffer (of max 2048x2048 pixels!)
#endif


//We're emulating a VGA screen adapter?
#define EMU_VGA 1

//Enable graphics?
#define ALLOW_GPU_GRAPHICS 1

//Enable GPU textmode & graphics(if ALLOW_GPU_GRAPHICS==1)?
#define ALLOW_VIDEO 1

//Allow direct plotting (1:1 plotting)?
//Direct plot forced?
#define VIDEO_DFORCED (BIOS_Settings.GPU_AllowDirectPlot==2)
//Normal dynamic direct plot according to resolution?
#if defined(STATICSCREEN) || defined(IS_PSP)
#ifndef IS_GPU
extern word window_xres, window_yres;
#endif
#define VIDEO_DIRECT (((GPU.xres<=window_xres) && (GPU.yres<=window_yres) && (BIOS_Settings.GPU_AllowDirectPlot==1))||VIDEO_DFORCED)
#else
#define VIDEO_DIRECT (((GPU.xres<=PSP_SCREEN_COLUMNS) && (GPU.yres<=PSP_SCREEN_ROWS) && (BIOS_Settings.GPU_AllowDirectPlot==1))||VIDEO_DFORCED)
#endif

//Start address of real device (PSP) VRAM!
#define VRAM_START 0x44000000

//Give the pixel from our real screen (after filled a scanline at least)!
#define PSP_SCREEN(x,y) GPU.vram[(y<<9)|x]
//Give the pixel from our psp screen we're rendering!
#define PSP_BUFFER(x,y) PSP_SCREEN(x,y)
//Give the pixel from our emulator we're buffering!

#define EMU_SCREENBUFFEREND (GPU.emu_screenbufferend)
#define PSP_SCREENBUFFERSIZE (PSP_SCREEN_ROWS<<9) //The PSP's screen buffer we're rendering!

//Show the framerate?
#define SHOW_FRAMERATE (GPU.show_framerate>0)

//Multithread divider for scanlines (on the real screen) (Higher means faster drawing)
//90=1sec.
#define SCANLINE_MULTITHREAD_DIVIDER 90
//Allow rendering (renderer enabled?) (Full VGA speed!)
#define ALLOW_RENDERING 1
//Allow maximum VGA speed (but minimum rendering speed!)
//#define SCANLINE_MULTITHREAD_DIVIDER 1

//U can find info here: http://www.ift.ulaval.ca/~marchand/ift17583/dosints.pdf

#define GPU_GETPIXEL(x,y) EMU_BUFFER(x,y)

//Divide data for fuse_pixelarea!
#define DIVIDED(v,n) (byte)SAFEDIV((DOUBLE)v,(DOUBLE)n)
#define CONVERTREL(src,srcfac,dest) SAFEDIV((DOUBLE)src,(DOUBLE)srcfac)*(DOUBLE)dest

typedef struct
{
//Now normal stuff:

	int video_on; //Video on?

	int showpixels; //To show the pixels?
	uint_32* vram; //Direct pointer to REAL vram of the PSP!
//Visual screen to render after VGA etc.!
	uint_32 *emu_screenbuffer; //Dynamic pointer to the emulator screen buffer!
	uint_32 *emu_screenbufferend; //Pointer to the emulator screen buffer end (1 byte after) where we overflow into invalid memory!

	//Display resolution:
	word xres; //X size of screen
	word yres; //Y size of screen
	byte aspectratio; //Enable GPU letterbox (keeping aspect ratio) while rendering?

	//Extra tricks:
	byte doublewidth; //Double x resolution by duplicating every pixel horizontally!
	byte doubleheight; //Double y resolution by duplicating every pixel vertically!

	//Emulator support!
//Coordinates of the emulator output!
	byte GPU_EMU_color; //Font color for emulator output!

	//Framerate!
	byte show_framerate; //Show the framerate?
	byte frameskip; //Frameskip!
	uint_32 framenr; //Current frame number (for Frameskip, kept 0 elsewise.)

	uint_32 emu_buffer_dirty; //Emu screenbuffer dirty: needs re-rendering?

	//Text surface support!
	Handler textrenderers[10]; //Every surface can have a handler to draw!
	void *textsurfaces[10]; //Up to 10 text surfaces available!

	byte fullscreen; //Are we in fullscreen view?
	byte forceRedraw; //We need a redraw explicitly?
} GPU_type; //GPU data

void initVideoLayer(); //We're for allocating the main video layer, only deallocated using SDL_Quit (when quitting the application)!
void resetVideo(); //Resets the screen (clears); used at start of emulator/reset!
void initVideo(int show_framerate); //Resets the screen (clears); used at start of emulation only!
void doneVideo(); //We're done with video operations?
void startVideo(); //Turn video on!
void stopVideo(); //Turn video off!
void GPU_AspectRatio(byte aspectratio); //Set aspect ratio!

void initVideoMain(); //Resets the screen (clears); used at start of emulator only!
void doneVideoMain(); //Resets the screen (clears); used at end of emulator only!

void GPU_addTextSurface(void *surface, Handler handler); //Register a text surface for usage with the GPU!
void GPU_removeTextSurface(void *surface); //Unregister a text surface (removes above added surface)!

void GPU_updateDPI(); //Update DPI values!

void updateVideo(); //Update the screen resolution on change!
void CPU_updateVideo(); //Actual video update from the CPU side of things!

//Clicking support!
byte GPU_mousebuttondown(word x, word y, byte finger); //We've been clicked at these coordinates!
void GPU_mousebuttonup(word x, word y, byte finger); //We've been released at these coordinates!
void GPU_mousemove(word x, word y, byte finger); //We've been moved to these coordinates!

void GPU_tickVideo(); //Tick the video display!

enum GPUMESSAGEBOXTYPES
{
	MESSAGEBOX_ERROR = 0, //Normal error!
	MESSAGEBOX_FATAL = 1, //Fatal error!
	MESSAGEBOX_WARNING = 2, //Warning
	MESSAGEBOX_INFORMATION = 3 //Information
};

void GPU_messagebox(char* title, byte type, char* text, ...); //Show a message box!

#define lockGPU() lock(LOCK_GPU)
#define unlockGPU() unlock(LOCK_GPU)
#endif
