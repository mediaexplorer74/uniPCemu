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

#ifndef __WAVE_H
#define __WAVE_H

#include "headers/types.h" //Basic types!
#include "headers/fopen64.h"

#include "headers/packed.h" //Packed type!
typedef struct PACKED
{
	uint_32 ChunkID;
	uint_32 ChunkSize;
	uint_32 Format;
	uint_32 Subchunk1ID;
	uint_32 Subchunk1Size;
	word AudioFormat;
	word NumChannels;
	uint_32 SampleRate;
	uint_32 ByteRate;
	word BlockAlign;
	word BitsPerSample;
	uint_32 Subchunk2ID;
	uint_32 Subchunk2Size;
} WAVEHEADER;
#include "headers/endpacked.h" //End of packed type!

typedef struct
{
	BIGFILE *f; //The file itself!
	WAVEHEADER header; //Full version of the WAVE header to be written to the file when closed!
	char filename[256]; //Full filename!
} WAVEFILE;

WAVEFILE *createWAV(char *filename, byte channels, uint_32 samplerate);
byte writeWAVMonoSample(WAVEFILE *f, word sample);
byte writeWAVStereoSample(WAVEFILE *f, word lsample, word rsample);
void closeWAV(WAVEFILE **f);

#endif