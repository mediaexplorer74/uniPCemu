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

#ifndef __ADSR_H
#define __ADSR_H

#include "..\commonemuframework\headers\types.h" //"headers/types.h" //Basic types!
#include "headers/support/sf2.h" //SF2 support!

//All statuses for an MIDI ADSR!
#define ADSR_IDLE 0x00
#define ADSR_DELAY 0x01
#define ADSR_ATTACK 0x02
#define ADSR_HOLD 0x03
#define ADSR_DECAY 0x04
#define ADSR_SUSTAIN 0x05
#define ADSR_RELEASE 0x06

//Convert cents to samples to increase (instead of 1 sample/sample). Floating point number (between 0.0+ usually?) Use this as a counter for the current samples (1.1+1.1..., so keep the rest value (1,1,1,...,0,1,1,1,...))
//The same applies to absolute and relative timecents (with absolute referring to 1 second intervals (framerate samples) and relative to the absolute value)
#define cents2samplesfactord(cents) pow(2, ((cents) / 1200))
#define cents2samplesfactorf(cents) powf(2, ((cents) / 1200))
//Convert to samples (not entire numbers, so keep them counted)!

typedef struct
{
	//ADSR
	int_64 attackstart, holdstart, decaystart, sustainstart, releasestart, releasedstart;
	uint_32 delay, attack, hold, decay, sustain, release; //All lengths and start positions when applied!
	uint_32 attackend, holdend, decayend; //End position of each of the phases, precalculated!
	float attackfactor, decayfactor, sustainfactor, releasefactor, releaselevel;

	byte active; //Are we an active ADSR envelope?
	byte attackstarted, holdstarted, decaystarted, sustainstarted, releasestarted, released; //Are these phases started/finished?
	byte attackisconvex; //Attack is convex?
} ADSR; //An ADSR's data!

void ADSR_init(void *voice, float sampleRate, byte velocity, ADSR *adsr, RIFFHEADER *soundfont, word instrumentptrAmount, word ibag, uint_32 preset, word pbag, word delayLookup, word attackLookup, byte attackisconvex, word holdLookup, word decayLookup, word sustainLookup, word releaseLookup, byte keynum, word keynumToEnvHoldLookup, word keynumToEnvDecayLookup); //Initialise an ADSR!
float ADSR_tick(ADSR *adsr, int_64 play_counter, byte sustain, float noteon_velocity, byte releasevelocity); //Tick an ADSR!
#endif
