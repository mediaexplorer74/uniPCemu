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

#ifndef MIDI_H
#define MIDI_H

#include "headers/types.h" //Basic types!

void resetMPU(); //Fully resets the MPU!
byte MIDI_has_data(); //Do we have data to be read?
void MIDI_OUT(byte data);
byte MIDI_IN();
byte initMPU(char *filename, byte use_direct_MIDI); //Our own initialise function!
void doneMPU(); //Finish function!

void MPU401_Init(/*Section* sec*/); //From DOSBox (mpu.c)!

void MPU401_Done(); //Finish our MPU! Custom by superfury1!

void updateMPUTimer(DOUBLE timepassed);

void setMPUTimer(DOUBLE timeout, Handler handler);
void removeMPUTimer();

#endif