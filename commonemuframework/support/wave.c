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

#include "headers/types.h"
#include "headers/support/wave.h" //Wave file structures etc.
#include "headers/support/zalloc.h" //Allocation support!
#include "headers/fopen64.h" //64-bit fopen support!

#define RIFF_RIFF 0x46464952
#define RIFF_WAVE 0x45564157
#define RIFF_FMT 0x20746d66
#define RIFF_DATA 0x61746164

byte writeWord(BIGFILE *f, word w) //INTERNAL: Channels are interleaved, channel 0 left, channel 0 right, channel 1 left, channel 1 right etc.
{
	return (emufwrite64(&w, 1, sizeof(w), f) == sizeof(w)); //Written?
}

byte writeDWord(BIGFILE *f, uint_32 d) //INTERNAL
{
	return (emufwrite64(&d, 1, sizeof(d), f) == sizeof(d)); //Written?
}

byte writeWAVMonoSample(WAVEFILE *f, word sample)
{
	if (memprotect(f,sizeof(*f),NULL))
	{
		return writeWord(f->f, sample); //Write the sample!
	}
	return 0; //Error!
}

byte writeWAVStereoSample(WAVEFILE *f, word lsample, word rsample)
{
	byte result;
	if (memprotect(f, sizeof(*f), NULL))
	{
		result = writeWord(f->f, lsample); //Write the left sample!
		if (!result) return 0; //Error!
		return writeWord(f->f, rsample); //Write the right sample!
	}
	return 0; //Error!
}

void WAVdealloc(void **ptr, uint_32 size, SDL_sem *lock)
{
	if (lock) WaitSem(lock)
	WAVEFILE **f;
	WAVEFILE *f2;
	FILEPOS finalposition; //Final data position!
	f = (WAVEFILE **)ptr; //The wave file pointer
	if (f) //valid?
	{
		f2 = *f; //Get the pointer value!
		if (f2) //Valid pointer?
		{
			if (f2->f) //Valid file?
			{
				//Update WAVE file data!
				finalposition = emuftell64(f2->f); //Final position!
				if (finalposition == sizeof(f2->header)) //Empty file?
				{
					if (f2->header.NumChannels == 2) //Stereo?
					{
						writeWAVStereoSample(f2,0,0); //Stereo empty sample!
					}
					else
					{
						writeWAVMonoSample(f2, 0); //Mono empty sample!
					}
					finalposition = emuftell64(f2->f); //Update final position!
				}
				f2->header.Subchunk2Size = (uint_32)(finalposition - sizeof(f2->header)); //Update data size!
				f2->header.ChunkSize = (uint_32)(finalposition - 8U); //Update WaveFmt chunk size
				emufseek64(f2->f,0,SEEK_SET); //Goto BOF to update the header!
				emufwrite64(&f2->header,1,sizeof(f2->header),f2->f); //Overwrite the file's header!
				//Finally, close the file!
				emufclose64(f2->f); //Close the file!
				if (finalposition == sizeof(f2->header)) //Empty file?
				{
					remove(f2->filename); //Remove the file: it's invalid!
				}
				f2->f = NULL; //Not allocated anymore!
			}
		}
	}
	DEALLOCFUNC defaultdealloc = getdefaultdealloc(); //Default deallocation function!
	defaultdealloc(ptr,size,NULL); //Release the pointer normally by direct deallocation!
	if (lock) PostSem(lock)
}

WAVEFILE *createWAV(char *filename, byte channels, uint_32 samplerate)
{
	WAVEFILE *f;
	if (!samplerate) return NULL; //Invalid: without samples we don't work!
	if (!channels) return NULL; //Invalid: without channels we don't work!
	f = zalloc(sizeof(WAVEFILE),"WAVEFILE",NULL); //Allocate the structure for processing!
	if (!f) return NULL; //Cannot allocate structure!
	if (!changedealloc(f, sizeof(WAVEFILE), &WAVdealloc)) //Failed to register our deallocation function?
	{
		freez((void **)&f, sizeof(WAVEFILE), "WAVEFILE"); //Free the file!
		return NULL; //Failed to unregister!
	}
	//Create basic header!
	f->header.ChunkID = RIFF_RIFF; //RIFF chunk start!
	f->header.ChunkSize = sizeof(f->header)-12; //Still empty!
	f->header.Format = RIFF_WAVE; //We're a WAVE file!

	f->header.Subchunk1ID = RIFF_FMT; //Format chunk start!
	f->header.Subchunk1Size = 16; //We're a basic format chunk with 16 bytes of data!
	f->header.AudioFormat = 1; //We're PCM format, uncompressed!
	f->header.NumChannels = channels; //Our number of channels!
	f->header.SampleRate = samplerate; //Our sample rate!
	f->header.ByteRate = (samplerate * channels * 16) / 8; //Byte rate per second
	f->header.BlockAlign = (channels*16)/8; //Size of one sample!
	f->header.BitsPerSample = 16; //Bits per sample!

	f->header.Subchunk2ID = RIFF_DATA; //DATA chunk start!
	f->header.Subchunk2Size = 0; //We don't have any data recorded yet, so 0 bytes atm!
	safestrcpy(f->filename,sizeof(f->filename),filename); //Set the filename to be removed if empty!
	f->f = emufopen64(filename, "wb+"); //Open the WAV file!
	if (emufwrite64(&f->header, 1, sizeof(f->header), f->f) != sizeof(f->header)) //Failed to write the header?
	{
		freez((void **)&f, sizeof(WAVEFILE), "WAVEFILE"); //Free the file!
		return NULL; //Failed to unregister!
	}

	return f; //Give the started file!
}

void closeWAV(WAVEFILE **f)
{
	freez((void **)f,sizeof(WAVEFILE),"closeWAV"); //Save and close the file, taking advantage of the auto-cleanup feature of zalloc!
}