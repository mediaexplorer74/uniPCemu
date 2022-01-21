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

#ifndef SOUNDDOUBLEBUFFER_H
#define SOUNDDOUBLEBUFFER_H

#include "headers/types.h" //Basic types!
#include "headers/support/fifobuffer.h" //FIFO buffer support for our buffering needs!

typedef struct
{
	FIFOBUFFER *outputbuffer; //Output buffer for rendering!
	FIFOBUFFER *sharedbuffer; //Shared buffer for transfers!
	FIFOBUFFER *inputbuffer; //Input buffer for playback!
	uint_32 samplebuffersize; //Size of the sample buffers!
} SOUNDDOUBLEBUFFER;

//Basic (de)allocation
byte allocDoubleBufferedSound32(uint_32 samplebuffersize, SOUNDDOUBLEBUFFER *buffer, byte locked, DOUBLE samplerate);
byte allocDoubleBufferedSound16(uint_32 samplebuffersize, SOUNDDOUBLEBUFFER *buffer, byte locked, DOUBLE samplerate);
byte allocDoubleBufferedSound8(uint_32 samplebuffersize, SOUNDDOUBLEBUFFER *buffer, byte locked, DOUBLE samplerate);
void freeDoubleBufferedSound(SOUNDDOUBLEBUFFER *buffer);

//Input&Output
void writeDoubleBufferedSound32(SOUNDDOUBLEBUFFER *buffer, uint_32 sample);
void writeDoubleBufferedSound16(SOUNDDOUBLEBUFFER *buffer, word sample);
void writeDoubleBufferedSound8(SOUNDDOUBLEBUFFER *buffer, byte sample);
byte readDoubleBufferedSound32(SOUNDDOUBLEBUFFER *buffer, uint_32 *sample);
byte readDoubleBufferedSound16(SOUNDDOUBLEBUFFER *buffer, word *sample);
byte readDoubleBufferedSound8(SOUNDDOUBLEBUFFER *buffer, byte *sample);
#endif