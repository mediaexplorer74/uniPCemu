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

#ifndef FILTER_H
#define FILTER_H

#include "..\commonemuframework\headers\types.h" //"headers/types.h" //Basic types!

typedef struct
{
	byte isInit; //New uninitialized filter?
	float sound_last_result; //Last result!
	float sound_last_sample; //Last sample!

	float alpha; //Solid value that doesn't change for the filter, until the filter is updated!

	//General filter information and settings set for the filter!
	byte isHighPass;
	float cutoff_freq;
	float samplerate;
} HIGHLOWPASSFILTER; //High or low pass filter!


#define applySoundLowPassFilterObj(filter,currentsample) currentsample = filter.sound_last_result = filter.sound_last_result+(filter.alpha*(currentsample-filter.sound_last_result))

#define applySoundHighPassFilterObj(filter,currentsample,last_resulttmp) last_resulttmp = filter.sound_last_result; last_resulttmp = filter.alpha * (last_resulttmp + currentsample - filter.sound_last_sample); filter.sound_last_sample = currentsample; currentsample = filter.sound_last_result = last_resulttmp

//Global high and low pass filters support!
void initSoundFilter(HIGHLOWPASSFILTER *filter, byte ishighpass, float cutoff_freq, float samplerate); //Initialize the filter!
void updateSoundFilter(HIGHLOWPASSFILTER *filter, byte ishighpass, float cutoff_freq, float samplerate); //Update the filter information/type!
void applySoundHighPassFilter(HIGHLOWPASSFILTER *filter, float *currentsample); //Apply the filter to a sample stream!
void applySoundLowPassFilter(HIGHLOWPASSFILTER *filter, float *currentsample); //Apply the filter to a sample stream!
void applySoundFilter(HIGHLOWPASSFILTER *filter, float *currentsample); //Apply the filter to a sample stream!

#endif
