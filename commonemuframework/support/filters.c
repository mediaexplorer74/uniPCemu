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

#include "headers/support/filters.h" //Our filter definitions!

void updateSoundFilter(HIGHLOWPASSFILTER *filter, byte ishighpass, float cutoff_freq, float samplerate)
{
	if (filter->isInit || (filter->cutoff_freq!=cutoff_freq) || (filter->samplerate!=samplerate) || (ishighpass!=filter->isHighPass)) //We're to update?
	{
		if ((ishighpass!=filter->isHighPass) && (filter->isInit==0)) return; //Don't allow changing filter types of running channels!
		if (ishighpass) //High-pass filter?
		{
			float RC = (1.0f / (cutoff_freq * (2.0f * (float)PI))); //RC is used multiple times, calculate once!
			filter->alpha = (RC / (RC + (1.0f / samplerate))); //Alpha value to use!
		}
		else //Low-pass filter?
		{
			float dt = (1.0f / samplerate); //DT is used multiple times, calculate once!
			filter->alpha = (dt / ((1.0f / (cutoff_freq * (2.0f * (float)PI))) + dt)); //Alpha value to use!
		}
	}
	filter->isHighPass = ishighpass; //Hi-pass filter?
	filter->cutoff_freq = cutoff_freq; //New cutoff frequency!
	filter->samplerate = samplerate; //New samplerate!
}

void initSoundFilter(HIGHLOWPASSFILTER *filter, byte ishighpass, float cutoff_freq, float samplerate)
{
	filter->isInit = 1; //We're an Init!
	filter->sound_last_result = filter->sound_last_sample = 0; //Save the first&last sample!
	updateSoundFilter(filter,ishighpass,cutoff_freq,samplerate); //Init our filter!
}

void applySoundFilter(HIGHLOWPASSFILTER *filter, float *currentsample)
{
	INLINEREGISTER float last_result;
	last_result = filter->sound_last_result; //Load the last result to process!
	if (unlikely(filter->isHighPass)) //High-pass filter? Low-pass filters are more commonly used!
	{
		last_result = filter->alpha * (last_result + *currentsample - filter->sound_last_sample);
		filter->sound_last_sample = *currentsample; //The last sample that was processed!
	}
	else //Low-pass filter?
	{
		last_result += (filter->alpha*(*currentsample-last_result));
	}
	*currentsample = filter->sound_last_result = last_result; //Give the new result!
}

void applySoundHighPassFilter(HIGHLOWPASSFILTER *filter, float *currentsample)
{
	INLINEREGISTER float last_result;
	last_result = filter->sound_last_result; //Load the last result to process!
	last_result = filter->alpha * (last_result + *currentsample - filter->sound_last_sample);
	filter->sound_last_sample = *currentsample; //The last sample that was processed!
	*currentsample = filter->sound_last_result = last_result; //Give the new result!
}

#define calcSoundLowPassFilter(filter,currentsample) *currentsample = filter->sound_last_result = filter->sound_last_result+(filter->alpha*((*currentsample)-filter->sound_last_result))

void applySoundLowPassFilter(HIGHLOWPASSFILTER *filter, float *currentsample)
{
	calcSoundLowPassFilter(filter,currentsample); //Filter manually!
}
