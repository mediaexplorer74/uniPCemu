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
#include "headers/hardware/vga/vga.h" //VGA VBlank support!
#include "headers/emu/gpu/gpu_text.h" //Text support!
#include "headers/cpu/cpu.h" //CPU!
#include "headers/interrupts/interrupt10.h" //INT10 support!
#include "headers/emu/emu_vga_bios.h" //VGA misc functionality for INT10!
#include "headers/fopen64.h" //64-bit fopen support!
#include "headers/cpu/easyregs.h" //Easy register support!

//Debugger functions!
#include "headers/emu/timers.h" //Timer support!
#include "headers/hardware/vga/vga_precalcs.h" //For the CRT precalcs dump!
#include "headers/hardware/vga/vga_dac.h" //DAC dump support!
#include "headers/hardware/vga/vga_vram.h" //DAC VRAM support!
#include "headers/support/log.h" //We're logging data!


//To make a screen capture of all of the debug screens active?
#define LOG_VGA_SCREEN_CAPTURE 0
//For text-mode debugging! 40 and 80 character modes!
#define VIDEOMODE_TEXTMODE_40 0x00
#define VIDEOMODE_TEXTMODE_80 0x02
//To log the first rendered line after putting pixels?
#define LOG_VGA_FIRST_LINE 0
//To debug text modes too in below or BIOS setting?
#define TEXTMODE_DEBUGGING 1
//Always sleep after debugging?
#define ALWAYS_SLEEP 1
//Debug 256-color and SVGA modes only?
//#define DEBUG256

extern byte LOG_MMU_WRITES; //Log MMU writes?

extern byte ENABLE_VRAM_LOG; //Enable VRAM logging?
extern byte SCREEN_CAPTURE; //Log a screen capture?

extern VGA_Type *MainVGA; //Main VGA!

extern GPU_type GPU; //For x&y initialisation!

byte *loadfile(char *filename, int_32 *size)
{
	byte *buffer;
	BIGFILE *f;
	uint_64 thesize;
	f = emufopen64(filename,"rb");
	if (!f) //failed to open?
	{
		return NULL; //Not existant!
	}
	emufseek64(f,0,SEEK_END); //EOF!
	thesize = emuftell64(f); //Size!
	emufseek64(f,0,SEEK_SET); //BOF!
	if ((!thesize) || (thesize&~0xFFFFFFFFULL)) //No size?
	{
		emufclose64(f); //Close!
		return NULL; //No file!
	}
	buffer = (byte *)zalloc((uint_32)thesize,"LOADEDFILE",NULL);
	if (!buffer) //No buffer?
	{
		emufclose64(f); //Close
		return NULL; //No file!
	}
	if (emufread64(buffer,1,thesize,f)!=thesize) //Error reading?
	{
		freez((void **)&buffer,(uint_32)thesize,"LOADEDFILE"); //Release!
		emufclose64(f); //Close!
		return NULL; //No file!
	}
	emufclose64(f); //Close!
	*size = (int_32)thesize; //Set the size!
	return buffer; //Give the buffer read!
}

void releasefile(byte **stream, int_32 *size)
{
	if (*stream) //Actually loaded something?
	{
		freez((void **)stream,*size,"LOADEDFILE"); //Release the file to release!
	}
}

byte loadVGADump(byte mode)
{
	word port; //For 3B0-3DF!
	byte controller;
	byte loaded; //Are we loaded?
	char filename[256], controllerfilename[10][256];
	char controllerparts[10][10] = {"AC","DAC","GC","SEQ","RED","CRT","PL0","PL1","PL2","PL3"};
	byte *buffers[10]; //All buffers allocated for loading!
	int_32 buffersizes[10]; //All buffer sizes!
	uint_32 planaroffset;
	byte plane;
	cleardata(&filename[0],sizeof(filename)); //Init base filename!
	memset(&controllerfilename,0,sizeof(controllerfilename)); //Init current filename!
	snprintf(filename,sizeof(filename),"VGAdump/VGADMP%02X.",mode); //Base VGA dump filename!
	//Now load all files!
	memset(&buffers,0,sizeof(buffers)); //Clear all buffers!
	loaded = 1; //Default: we're fully loaded and existant!
	for (controller=0;controller<NUMITEMS(controllerparts);++controller) //Load all files!
	{
		safestrcpy(controllerfilename[controller],sizeof(controllerfilename[0]),filename); //Load base filename!
		safestrcat(controllerfilename[controller],sizeof(controllerfilename[0]),controllerparts[controller]); //Add the part for the full filename!
		buffers[controller] = loadfile(controllerfilename[controller],&buffersizes[controller]); //Try and load the file!
		if (!buffers[controller]) //Failed loading?
		{
			loaded = 0;
			goto errorloading; //Error loading!
		}
	}
	//All loaded? Move to VRAM&registers directly, then apply precalcs!
	
	//Now, add all ET3000/ET4000 registers too!
	//Don't activate the extensions, as this is already done when starting the debugging phase!
	//Ignore the memory mapping, as this is done directly from planar data!

	//RED:
	for (port = 0x3B0;port < 0x3DF;port++)
	{
		if ((port-0x3B0)>=buffersizes[4]) break; //Stop when over the limit!
		PORT_OUT_B(port,buffers[4][port-0x3B0]); //Write the port directly, doesn't matter what it's used for (VGA ports either overridden below or unused).
	}

	//Load all extended SVGA registers!

	//Then, all VGA registers!
	//Attribute
	for (port=0;port<buffersizes[0];++port) //Attribute registers!
	{
		PORT_OUT_B(0x3C0,(byte)port); //The number!
		PORT_OUT_B(0x3C0,buffers[0][port]); //The data!
	}

	//DAC
	PORT_OUT_B(0x3C8,0); //Reset DAC to known state!
	for (port = 0;port<buffersizes[1];++port) //DAC registers!
	{
		PORT_OUT_B(0x3C9, buffers[1][port]); //The data, 3 bytes per entry!
	}

	//Graphics
	for (port = 0;port<buffersizes[2];++port) //Graphics registers!
	{
		PORT_OUT_B(0x3CE, (byte)port); //The number!
		PORT_OUT_B(0x3CF, buffers[2][port]); //The data!
	}

	//Sequencer
	for (port = 0;port<buffersizes[3];++port) //Sequencer registers!
	{
		PORT_OUT_B(0x3C4, (byte)port); //The number!
		if (!port) //First port needs to be running?
		{
			PORT_OUT_B(0x3C5, buffers[3][port]|3); //The data, which sequencer always enabled!
		}
		else //Normal output?
		{
			PORT_OUT_B(0x3C5, buffers[3][port]); //The data!
		}
	}

	//Assume color mode!
	for (port = 0;port<buffersizes[5];++port) //CRT registers!
	{
		PORT_OUT_B(0x3D4, (byte)port); //The number!
		PORT_OUT_B(0x3D5, buffers[5][port]); //The data!
	}

	//Load all VRAM data for testing!
	for (plane=0;plane<4;++plane) //All four planes!
	{
		for (planaroffset=0;(int_64)planaroffset<(int_64)buffersizes[plane+6];++planaroffset) //All planar data!
		{
			writeVRAMplane(getActiveVGA(),plane,planaroffset,0,buffers[plane+6][planaroffset],1); //Write the data to VRAM directly!
		}
	}

	//Finish up and return our result!
	errorloading: //Error occurred when loading? Or we're cleaning up!
	for (controller=0;controller<NUMITEMS(controllerparts);++controller) //Load all files!
	{
		releasefile(&buffers[controller],&buffersizes[controller]); //Release the loaded file, if it's loaded!
	}
	return loaded; //Are we fully loaded?
}

void debugTextModeScreenCapture()
{
	SCREEN_CAPTURE = 1; //Screen capture next frame?
	unlock(LOCK_MAINTHREAD);
	VGA_waitforVBlank(); //Log one screen!
	lock(LOCK_MAINTHREAD);
	for (; SCREEN_CAPTURE;) //Busy?
	{
		unlock(LOCK_MAINTHREAD);
		VGA_waitforVBlank(); //Wait for VBlank!
		lock(LOCK_MAINTHREAD);
	}
}

extern GPU_TEXTSURFACE *frameratesurface; //The framerate surface!

void GPUswitchvideomode(word mode); //Prototype!


void DoDebugVGAGraphics(word mode, word xsize, word ysize, uint_32 maxcolor, int allequal, uint_32 centercolor, byte usecenter, byte screencapture)
{
	stopTimers(0); //Stop all timers!

	GPUswitchvideomode(mode); //Switch to said mode!


	REG_AH = 0xB;
	REG_BH = 0x0; //Set overscan color!
	REG_BL = 0x1; //Blue overscan!
	BIOS_int10();
	unlock(LOCK_MAINTHREAD);

	int x,y; //X&Y coordinate!
	uint_32 color; //The color for the coordinate!

	GPU_text_locksurface(frameratesurface);
	GPU_textgotoxy(frameratesurface,0,2); //Goto third row!
	GPU_textprintf(frameratesurface,RGB(0xFF,0xFF,0xFF),RGB(0x00,0x00,0x00),"Surface for mode %02X(Colors %03i): Rendering...",mode,maxcolor);
	GPU_text_releasesurface(frameratesurface);

	lock(LOCK_MAINTHREAD);
	y = 0; //Init Y!
	nexty:
	{
		if (y>=ysize) goto finishy;
		x = 0; //Init x!
		nextx:
		{
			if (x>=xsize) goto finishx;
			color = convertrel(x,xsize,maxcolor); //Convert relative to get all colors from left to right on the screen!
			if (color>(maxcolor-1)) color = maxcolor-1; //MAX limit!
			if (allequal) //All equal filling?
			{
				color = maxcolor-1;
			}

			if (y>=(int)((ysize/2)-(usecenter/2)) && 
				(y<=(int)((ysize/2)+(usecenter/2))) && usecenter) //Half line horizontally?
			{
				GPU_putpixel(x,y,0,centercolor); //Plot color!
			}
			else
			{
				if (x>=(int)((xsize/2)-(usecenter/2)) && 
					(x<=(int)((xsize/2)+(usecenter/2))) && usecenter) //Half line vertically?
				{
					color = (byte)SAFEMOD(((int)convertrel(y,ysize,maxcolor)),maxcolor); //Flow Y!
				}
				GPU_putpixel(x,y,0,color); //Plot color!
			}
			++x; //Next X!
			goto nextx;
		}
		finishx: //Finish our line!
		++y; //Next Y!
		goto nexty;
	}
	
	finishy: //Finish our operations!
	if (loadVGADump((byte)mode)) //VGA dump loaded instead?
	{
		dolog("debugger","VGA dump loaded: %02X",mode); //Loaded this VGA dump instead logged!
	}

	GPU_text_locksurface(frameratesurface);
	GPU_textgotoxy(frameratesurface,33,2); //Goto Rendering... text!
	GPU_textprintf(frameratesurface,RGB(0xFF,0xFF,0xFF),RGB(0x00,0x00,0x00),"Rendered.   ",mode);
	GPU_text_releasesurface(frameratesurface);
	
	startTimers(0); //Start the timers!

	if (screencapture) //To create a screen capture?
	{
		debugTextModeScreenCapture(); //Debug a screen capture!
	}
	unlock(LOCK_MAINTHREAD);
	delay(5000000); //Wait a bit!
}

/*

VGA Full Debug routine!

*/

extern byte VGA_LOGPRECALCS; //Log precalcs after this ammount of scanlines!

void DoDebugTextMode(byte waitforever) //Do the text-mode debugging!
{
	if (shuttingdown()) goto doshutdown;
	enableKeyboard(0); //Allow to test the keyboard!
	#ifdef DEBUG256
	goto specialdebugging;
	#endif
	lock(LOCK_MAINTHREAD); //Make sure we're the only one using it!
	if (TEXTMODE_DEBUGGING) //Debug text mode too?
	{
		stopTimers(0); //Make sure we've stopped!
		int i; //For further loops!

		REG_AX = VIDEOMODE_TEXTMODE_40;
		BIOS_int10(); //Text mode operations!
		REG_AH = 0xB;
		REG_BH = 0x0; //Set overscan color!
		REG_BL = 0x4; //Blue overscan!
		BIOS_int10(); //Set overscan!

		VGA_LOGCRTCSTATUS(); //Log our full status!
		VGA_LOGPRECALCS = 5; //Log after 5 scanlines!
		if (LOG_VGA_SCREEN_CAPTURE) debugTextModeScreenCapture(); //Make a screen capture!

		MMU_wb(-1,0xB800,0,'a',0);
		MMU_wb(-1,0xB800,1,0x1,0);
		MMU_wb(-1,0xB800,2,'b',0);
		MMU_wb(-1,0xB800,3,0x2,0);
		MMU_wb(-1,0xB800,4,'c',0);
		MMU_wb(-1,0xB800,5,0x3,0);
		MMU_wb(-1,0xB800,6,'d',0);
		MMU_wb(-1,0xB800,7,0x4,0);
		MMU_wb(-1,0xB800,8,'e',0);
		MMU_wb(-1,0xB800,9,0x5,0);
		MMU_wb(-1,0xB800,10,'f',0);
		MMU_wb(-1,0xB800,11,0x6,0);
		MMU_wb(-1,0xB800,12,'g',0);
		MMU_wb(-1,0xB800,13,0x7,0);
		MMU_wb(-1,0xB800,14,'h',0);
		MMU_wb(-1,0xB800,15,0x8,0);
		MMU_wb(-1,0xB800,16,'i',0);
		MMU_wb(-1,0xB800,17,0x9,0);
		MMU_wb(-1,0xB800,18,'j',0);
		MMU_wb(-1,0xB800,19,0xA,0);
		MMU_wb(-1,0xB800,20,'k',0);
		MMU_wb(-1,0xB800,21,0xB,0);
		MMU_wb(-1,0xB800,22,'l',0);
		MMU_wb(-1,0xB800,23,0xC,0);
		MMU_wb(-1,0xB800,24,'m',0);
		MMU_wb(-1,0xB800,25,0xD,0);
		MMU_wb(-1,0xB800,26,'n',0);
		MMU_wb(-1,0xB800,27,0xE,0);
		MMU_wb(-1,0xB800,28,'o',0);
		MMU_wb(-1,0xB800,29,0xF,0);
		
		GPU_text_locksurface(frameratesurface);
		GPU_textgotoxy(frameratesurface,0,2); //Goto third debug row!
		GPU_textprintf(frameratesurface,RGB(0xFF,0xFF,0xFF),RGB(0x00,0x00,0x00),"Direct VRAM access 40x25-0...");

		GPU_textprintf(frameratesurface,RGB(0xFF,0xFF,0xFF),RGB(0x00,0x00,0x00),"Ready.");
		GPU_text_releasesurface(frameratesurface);
		if (LOG_VGA_SCREEN_CAPTURE) debugTextModeScreenCapture(); //Debug a screen capture!

		startTimers(0); //Start timers up!
		unlock(LOCK_MAINTHREAD);
		delay(5000000); //Wait a bit!
		lock(LOCK_MAINTHREAD);
		if (shuttingdown()) goto doshutdown;
	
		REG_AH = 0x0B; //Advanced:!
		REG_BH = 0x00; //Set background/border color!
		REG_BL = 0x0E; //yellow!
		BIOS_int10(); //Show the border like this!
	
		if (LOG_VGA_SCREEN_CAPTURE) debugTextModeScreenCapture(); //Debug a screen capture!
		unlock(LOCK_MAINTHREAD);
		delay(5000000); //Wait 5 seconds!
		lock(LOCK_MAINTHREAD);
		if (shuttingdown()) goto doshutdown;
	
		REG_AX = 0x01; //40x25 TEXT mode!
		BIOS_int10(); //Switch modes!
	
		printmsg(0xF,"This is 40x25 TEXT MODE!");
		printCRLF();
		printmsg(0xF,"S"); //Start!
		for (i=0; i<38; i++) //38 columns!
		{
			printmsg(0x2,"X");
		}
		printmsg(0xF,"E"); //End!
		printmsg(0xF,"Third row!");
		GPU_text_locksurface(frameratesurface);
		GPU_textgotoxy(frameratesurface,0,2); //Goto third debug row!
		GPU_textprintf(frameratesurface,RGB(0xFF,0xFF,0xFF),RGB(0x00,0x00,0x00),"40x25-0 Alltextcolors...");
		GPU_text_releasesurface(frameratesurface);
		if (LOG_VGA_SCREEN_CAPTURE) debugTextModeScreenCapture(); //Debug a screen capture!
		unlock(LOCK_MAINTHREAD);
		delay(10000000); //Wait 10 seconds!
		lock(LOCK_MAINTHREAD);
		if (shuttingdown()) goto doshutdown;

		REG_AX = 0x81; //40x25, same, but with grayscale!
		BIOS_int10();
		GPU_text_locksurface(frameratesurface);
		GPU_textgotoxy(frameratesurface,0,2); //Goto third debug row!
		GPU_textprintf(frameratesurface,RGB(0xFF,0xFF,0xFF),RGB(0x00,0x00,0x00),"80x25-1 Alltextcolors...");
		GPU_text_releasesurface(frameratesurface);
		if (LOG_VGA_SCREEN_CAPTURE) debugTextModeScreenCapture(); //Debug a screen capture!
		unlock(LOCK_MAINTHREAD);
		delay(10000000); //Wait 10 seconds!
		lock(LOCK_MAINTHREAD);
		if (shuttingdown()) goto doshutdown;
	
		REG_AX = VIDEOMODE_TEXTMODE_80; //80x25 TEXT mode!
		BIOS_int10(); //Switch modes!
		printmsg(0xF,"This is 80x25 TEXT MODE!");
		printCRLF();
		printmsg(0xF,"S"); //Start!
		for (i=0; i<78; i++) //78 columns!
		{
			printmsg(0x2,"X");
		}
		printmsg(0xF,"E"); //End!
		printmsg(0xF,"Third row!");
		GPU_text_locksurface(frameratesurface);
		GPU_textgotoxy(frameratesurface,0,2); //Goto third debug row!
		GPU_textprintf(frameratesurface,RGB(0xFF,0xFF,0xFF),RGB(0x00,0x00,0x00),"80x25-2 WidthRows...");
		GPU_text_releasesurface(frameratesurface);
		if (LOG_VGA_SCREEN_CAPTURE) debugTextModeScreenCapture(); //Debug a screen capture!
		unlock(LOCK_MAINTHREAD);
		delay(10000000); //Wait 1 seconds!
		lock(LOCK_MAINTHREAD);
		if (shuttingdown()) goto doshutdown;
	
		REG_AX = VIDEOMODE_TEXTMODE_80; //Reset to 80x25 text mode!
		BIOS_int10(); //Reset!
	
		for (i=0; i<0x100; i++) //Verify all colors!
		{
			REG_AX = 0x0E41+(i%26); //Character A-Z!
			REG_BX = (word)(i%0x100); //Attribute at page 0!
			BIOS_int10(); //Show the color!
		}
	
		GPU_text_locksurface(frameratesurface);
		GPU_textgotoxy(frameratesurface,0,2); //Goto third debug row!
		GPU_textprintf(frameratesurface,RGB(0xFF,0xFF,0xFF),RGB(0x00,0x00,0x00),"80x25-2 Alltextcolors...");
		GPU_text_releasesurface(frameratesurface);
		if (LOG_VGA_SCREEN_CAPTURE) debugTextModeScreenCapture(); //Debug a screen capture!
		unlock(LOCK_MAINTHREAD);
		delay(10000000); //Wait 1 seconds!
		lock(LOCK_MAINTHREAD);
		if (shuttingdown()) goto doshutdown;
	
		REG_AX = 0x02; //80x25 b/w!
		BIOS_int10(); //Switch video modes!
	
		REG_AL = 0; //Reset character!
	
		for (i=0; i<0x100; i++) //Verify all characters!
		{
			if (i==12) //Special blink?
			{
				int10_internal_outputchar(0,(i&0xFF),0x8F); //Output&update with blink!
			}
			else //Normal character?
			{
				int10_internal_outputchar(0,(i&0xFF),0xF); //Output&update!
			}
		}
	
		REG_AH = 2; //Set cursor x,y
		REG_BH = 0; //Display page #0!
		REG_DL = 0; //X
		REG_DH = 0; //Y
		BIOS_int10(); //Show!
		if (LOG_VGA_SCREEN_CAPTURE) debugTextModeScreenCapture(); //Debug a screen capture!
		unlock(LOCK_MAINTHREAD);
		delay(5000000); //Wait 5 seconds!
		lock(LOCK_MAINTHREAD);
	}

	//Text modes work!
	//Graphics should be OK!
	//4-color modes!
	DoDebugVGAGraphics(0x04,320,200,0x04,0,0x3,1,0); //Debug 320x200x4!
	if (shuttingdown()) goto doshutdown;
	DoDebugVGAGraphics(0x05,320,200,0x04,0,0x0,1,0); //Debug 320x200x4(B/W)! 
	if (shuttingdown()) goto doshutdown;
	//B/W mode!
	
	DoDebugVGAGraphics(0x06,640,200,0x02,0,0x1,1,0); //Debug 640x200x2(B/W)!
	if (shuttingdown()) goto doshutdown;
	
	DoDebugVGAGraphics(0x0F,640,350,0x02,0,0x1,1,0); //Debug 640x350x2(Monochrome)!
	if (shuttingdown()) goto doshutdown;
	//16 color mode!
	DoDebugVGAGraphics(0x0D,320,200,0x10,0,0xF,0,0); //Debug 320x200x16!
	if (shuttingdown()) goto doshutdown;

	DoDebugVGAGraphics(0x0E,640,200,0x10,0,0xF,1,0); //Debug 640x200x16!
	if (shuttingdown()) goto doshutdown;
	DoDebugVGAGraphics(0x10,640,350,0x10,0,0xF,1,0); //Debug 640x350x16!
	if (shuttingdown()) goto doshutdown;
	//16 color b/w mode!
	DoDebugVGAGraphics(0x11,640,480,0x10,0,0x1,1,0); //Debug 640x480x16(B/W)! 
	if (shuttingdown()) goto doshutdown;
	//16 color maxres mode!
	DoDebugVGAGraphics(0x12,640,480,0x10,0,0xF,1,0); //Debug 640x480x16! VGA+!
	if (shuttingdown()) goto doshutdown;
	#ifdef DEBUG256
	specialdebugging:
	#endif
	//256 color mode!
	DoDebugVGAGraphics(0x13,320,200,0x100,0,0xF,1,1); //Debug 320x200x256! MCGA,VGA! works, but 1/8th screen width?
	if (shuttingdown()) goto doshutdown;

	if (getActiveVGA()->enable_SVGA) //SVGA debugging too?
	{
		DoDebugVGAGraphics(0x2E,640,480,0x100,0,0xF,1,1); //Debug 640x480x256! ET3000/ET4000!
		DoDebugVGAGraphics(0x213, 320, 200, 0x10000, 0, 0xFFFF, 1, 1); //Debug 320x200x32K! ET3000/ET4000!
		DoDebugVGAGraphics(0x22E, 640, 480, 0x10000, 0, 0xFFFF, 1, 1); //Debug 640x480x32K! ET3000/ET4000!
		if (shuttingdown()) goto doshutdown;
	}

	unlock(LOCK_MAINTHREAD); //We're finished! Unlock!
	if (waitforever) //Waiting forever?
	{
		for (;;)
		{
			if (shuttingdown()) goto doshutdown;
			delay(0); //Wait forever till user Quits the application!
		}
	}
	doshutdown:
	return; //We're finished!
}

/*

VGA Graphics debugging routine!

*/

void dumpVGA()
{
	GPU_text_locksurface(frameratesurface);
	GPU_textgotoxy(frameratesurface,0,0);
	GPU_textprintf(frameratesurface,RGB(0xFF,0xFF,0xFF),RGB(0x00,0x00,0x00),"Dumping VGA data...");
	GPU_text_releasesurface(frameratesurface);
	BIGFILE *f;
	f = emufopen64("VGA.DAT","wb"); //Open it!
	byte *b = (byte *)MainVGA->VRAM;
	uint_32 i=0;
	for (;i<MainVGA->VRAM_size;)
	{
		emufwrite64(&b[i],1,1,f); //Write VRAM!
		++i; //Next byte!
	}
	emufclose64(f); //Close it!
	f = emufopen64("DISPLAY.DAT","wb"); //Display!
	emufwrite64(&GPU.xres,1,sizeof(GPU.xres),f); //X size!
	emufwrite64(&GPU.yres,1,sizeof(GPU.yres),f); //Y size!
	emufwrite64(&GPU.emu_screenbuffer,1,1024*sizeof(GPU.emu_screenbuffer[0])*GPU.yres,f); //Video data!
	emufclose64(f); //Close it!
	raiseError("Debugging","Main VGA&Display dumped!");
}