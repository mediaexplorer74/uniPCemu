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

#ifndef HW82C54_H
#define HW82C54_H

typedef void (*PITTick)(byte output); //A handler for PIT ticks!

typedef struct {
	uint16_t chandata[3];
	uint8_t accessmode[3];
	uint8_t bytetoggle[3];
	uint32_t effectivedata[3];
	float chanfreq[3];
	uint8_t active[3];
	uint16_t counter[3];
} i8253_s;

void init8253(); //Initialisation!
void cleanPIT(); //Timer tick Irq reset timing

//PC speaker!
void setPITFrequency(byte channel, word newfrequency); //Set the new frequency!
void initSpeakers(byte soundspeaker); //Initialises the speaker and sets it up!
void doneSpeakers(); //Finishes the speaker and removes it!
void tickPIT(DOUBLE timepassed, uint_32 MHZ14passed); //Ticks all PIT timers/speakers available!
void setPITMode(byte channel, byte mode); //Set the current rendering mode!
void speakerGateUpdated(); //Gate has been updated?
void registerPIT1Ticker(PITTick ticker); //Register a PIT1 ticker for usage?

#endif
