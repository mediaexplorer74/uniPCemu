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

#ifndef INTERRUPT10_H
#define INTERRUPT10_H

#include "headers/header_dosbox.h" //Dosbox support!
#include "headers/hardware/vga/vga.h" //VGA support!

void int10_BIOSInit(); //Initisation of the BIOS routine!
void BIOS_int10(); //Interrupt #10h: (Video Services)! Overridable!

//Stuff for VGA screen/INT10!

void GPU_setresolution(word mode); //Apply current video mode and clear screen if needed!
void int10_refreshscreen(); //Interrupt 10 screen refresh!
void int10_dumpscreen(); //Dump screen to file!
void int10_vram_readcharacter(byte x, byte y, byte page, byte *character, byte *fontcolor); //Read character+attribute!

//Putpixel functions for interrupt 10h!
//Putpixel variants!
void MEMGRAPHICS_put2colors(uint_32 startaddr, int x, int y, byte color);
void MEMGRAPHICS_put4colors(uint_32 startaddr, int x, int y, byte color);
void MEMGRAPHICS_put4colorsSHADE(uint_32 startaddr, int x, int y, byte color);
void MEMGRAPHICS_put16colors(uint_32 startaddr, int x, int y, byte color);
void MEMGRAPHICS_put256colors(uint_32 startaddr, int x, int y, byte color);

//Getpixel variants!
byte MEMGRAPHICS_get2colors(uint_32 startaddr, int x, int y);
byte MEMGRAPHICS_get4colors(uint_32 startaddr, int x, int y);
byte MEMGRAPHICS_get4colorsSHADE(uint_32 startaddr, int x, int y);
byte MEMGRAPHICS_get16colors(uint_32 startaddr, int x, int y);
byte MEMGRAPHICS_get256colors(uint_32 startaddr, int x, int y);

void cursorXY(byte displaypage, byte x, byte y); //Set cursor x,y!
void emu_setactivedisplaypage(byte page); //Set active display page!
int INT10_Internal_SetVideoMode(word mode); //For internal int10 video mode switching!
void int10_internal_outputchar(byte videopage, byte character, byte attribute); //Ourput character (see int10,function 0xE)
void EMU_CPU_setCursorScanlines(byte start, byte end);
void GPU_clearscreen(); //Clears the screen!

//Finally, for switching VGA sets...
void int10_useVGA(VGA_Type *VGA);
void initint10(); //Fully initialise interrupt 10h!

word getscreenwidth(); //Get the screen width (in characters), based on the video mode!

//Character fonts!
void INT10_ReloadFont(); //Reload active font at address 0 in the VGA Plane 2!
void INT10_LoadFont(word fontseg, word fontoffs,bool reload,Bitu count,Bitu offset,Bitu map,Bitu height);
void INT10_ActivateFont(VGA_Type *VGA, byte height, word offset);

int GPU_putpixel(int x, int y, byte page, word color); //Writes a video buffer pixel to the real emulated screen buffer

//ROM support!
void INT10_SetupRomMemory(byte setinterrupts); //ROM memory installation!
void INT10_SetupRomMemoryChecksum(); //ROM memory checksum installation!
void INT10_PerformGrayScaleSumming(Bit16u start_reg,Bit16u count);

//ROM Video Parameter tables support!
void INT10_SetupBasicVideoParameterTable(void);
word INT10_SetupVideoParameterTable(word basepos);
void INT10_StartBasicVideoParameterTable();

#endif