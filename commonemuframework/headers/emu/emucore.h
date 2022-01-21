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

#ifndef EMUCORE_H
#define EMUCORE_H

void initEMU(int full); //Init EMU!
void doneEMU(); //Finish EMU!

//Pause/resume full emulation
void resumeEMU(byte startinput);
void pauseEMU();

void BIOSMenuResumeEMU(); //BIOS menu specific variant of resuming!

void initEMUreset(); //Simple reset emulator!

//Timers start/stop!
void stopEMUTimers();
void startEMUTimers();

//Input control
void EMU_stopInput();
void EMU_startInput();


//DoEmulator results:
//-1: Keep running: execute next instruction!
//0:Shutdown
//1:Reset emu
int DoEmulator(); //Run the emulator execution itself!

void EMU_drawBusy(byte disk); //Draw busy on-screen!
void EMU_drawRecording(byte location); //Draw recording identifier on the screen!

void updateSpeedLimit(); //Prototype!

void updateEMUSingleStep(byte index); //Update our single-step address!

#ifdef GBEMU
void DEBUG_INIT();
void ROMERROR(char *errormsg); //Called on error loading the ROM.
#endif

#ifdef UNIPCEMU
void EMU_onCPUReset(word isInit); //Emu handling for hardware on CPU reset!
void UniPCemu_onRenderingFrame();
void UniPCemu_afterRenderingFrameFPS(); //When finished rendering an update 10FPS frame!
void emu_raise_resetline(byte resetPendingFlags); //Raise the RESET line on the CPUs!
#endif

#endif
