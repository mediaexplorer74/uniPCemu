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

#ifndef VGA_CGA_MDA_H
#define VGA_CGA_MDA_H

#include "headers/types.h" //Basic types!
#include "headers/hardware/vga/vga.h" //Basic VGA support!

void setVGA_CGA(byte enabled); //0=Disabled, 1=Enable with disabled VGA, 2=Enabled with enabled VGA!
void setCGA_NewCGA(byte enabled); //Use New-style CGA emulation?
void setCGA_NTSC(byte enabled); //Use NTSC CGA signal output?
void setVGA_MDA(byte enabled); //0=Disabled, 1=Enable with disabled VGA, 2=Enabled with enabled VGA!

//Initialization call for registering us on the VGA!
void initCGA_MDA();

void CGA_checklightpen(word currentlocation, byte is_lightpenlocation, byte is_lightpenpressed); //Check the lightpen on the current location!

//CGA/MDA emulation enabled on the CRTC registers&timing?
#define CGAEMULATION_ENABLED_CRTC(VGA) (((VGA->registers->specialCGAflags&0x81)==1) || (GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.VERTICALRETRACEENDREGISTER,7,1) && ((VGA->registers->specialCGAflags&0xC1)==0xC1)))
#define MDAEMULATION_ENABLED_CRTC(VGA) (((VGA->registers->specialMDAflags&0x81)==1) || (GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.VERTICALRETRACEENDREGISTER,7,1) && ((VGA->registers->specialMDAflags&0xC1)==0xC1)))
#define CGAMDAEMULATION_ENABLED_CRTC(VGA) (((VGA->registers->specialCGAflags&0x81)==1) || ((VGA->registers->specialMDAflags&0x81)==1) || (GETBITS(VGA->registers->CRTControllerRegisters.REGISTERS.VERTICALRETRACEENDREGISTER,7,1) && (((VGA->registers->specialCGAflags&0xC1)==0xC1) || ((VGA->registers->specialMDAflags&0xC1)==0xC1))))
//CGA/MDA emulation enabled?
#define CGAEMULATION_ENABLED(VGA) (((VGA->registers->specialCGAflags&0x81)==1) || ((VGA->registers->specialCGAflags&0xC1)==0xC1))
#define MDAEMULATION_ENABLED(VGA) (((VGA->registers->specialMDAflags&0x81)==1) || ((VGA->registers->specialMDAflags&0xC1)==0xC1))
#define CGAMDAEMULATION_ENABLED(VGA) (((VGA->registers->specialCGAflags&0x81)==1) || ((VGA->registers->specialMDAflags&0x81)==1) || ((VGA->registers->specialCGAflags&0xC1)==0xC1) || ((VGA->registers->specialMDAflags&0xC1)==0xC1))

//To perform CGA/MDA to display conversion when either one is emulated and active?
#define CGAEMULATION_RENDER(VGA) (((VGA->registers->specialCGAflags&1) && ((VGA->registers->specialCGAflags&0xC0)!=0x80)))
#define MDAEMULATION_RENDER(VGA) (((VGA->registers->specialMDAflags&1) && ((VGA->registers->specialMDAflags&0xC0)!=0x80)))
#define CGAMDAEMULATION_RENDER(VGA) (((VGA->registers->specialCGAflags&1) && ((VGA->registers->specialCGAflags&0xC0)!=0x80)) || ((VGA->registers->specialMDAflags&1) && ((VGA->registers->specialMDAflags&0xC0)!=0x80)))

#define CGA_DOUBLEWIDTH(VGA) (CGAEMULATION_RENDER(VGA) && (((VGA->registers->Compatibility_CGAModeControl&0x12)==0x2) || (!(VGA->registers->Compatibility_CGAModeControl&0x3))))

#endif
