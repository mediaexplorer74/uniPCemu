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

#ifndef VARBUFFER_H
#define VARBUFFER_H

#include "..\commonemuframework\headers\types.h"  //"headers/types.h" //Basic type support!

typedef struct
{
	byte *buffer; //The buffer itself!
	uint_32 size; //The size of the buffer!
	uint_32 readpos; //The position to read!
	uint_32 writepos; //The position to write!
	uint_32 laststatus; //Last operation was a read?
	struct
	{
		uint_32 readpos; //The position to read!
		uint_32 writepos; //The position to write!
		uint_32 laststatus; //Last operation was a read?	
	} savedpos;
	SDL_sem *lock; //Our lock for single access!
} FIFOBUFFER;

/*

allocfifobuffer: generates a new buffer to work with.
parameters:
	buffersize: the size of the buffer!
	lockable: 1 to lock during accesses, 0 to use external locking when needed!
result:
	Buffer when allocated, NULL for error when allocating!

*/


FIFOBUFFER* allocfifobuffer(uint_32 buffersize, byte lockable); //Creates a new FIFO buffer!

/*

free_fifobuffer: Release a fifo buffer!

*/

void free_fifobuffer(FIFOBUFFER **buffer);

/*

fifobuffer_freesize: Determines how much data we have free (in units of buffer items)
paremeters:
	buffer: The buffer.
result:
	The ammount of items free.

*/

uint_32 fifobuffer_freesize(FIFOBUFFER *buffer);

/*

fifobuffer_size: Determines how much data we have available in total (free+filled) (in units of buffer items)
paremeters:
	buffer: The buffer.
result:
	The ammount of items available in total.

*/

uint_32 fifobuffer_size(FIFOBUFFER* buffer);

/*

waitforfreefifobuffer: Waits till some space is free. NOT TO BE USED BY THE HANDLER THAT READS IT!
parameters:
	buffer: The buffer.
	size: The size to wait for.

*/

void waitforfreefifobuffer(FIFOBUFFER *buffer, uint_32 size);


/*

peekfifobuffer: Is there data to be read?
returns:
	1 when available, 0 when not available.
	result is set to the value when available.

*/

byte peekfifobuffer(FIFOBUFFER *buffer, byte *result); //Is there data to be read?

/*

readfifobuffer: Tries to read data from a buffer (from the start)
parameters:
	buffer: pointer to the allocated buffer itself.
	result: pointer to variabele for read data
result:
	TRUE for read, FALSE for no data to read.

*/

byte readfifobuffer(FIFOBUFFER *buffer, byte *result);

/*

writefifobuffer: Writes data to the buffer (at the end)
parameters:
	buffer: pointer to the buffer itself.
	data: the data to be written to the buffer!
result:
	TRUE for written, FALSE for buffer full.
*/


byte writefifobuffer(FIFOBUFFER *buffer, byte data);

/*

fifobuffer_gotolast: Set last write to current read, if any!
parameters:
	buffer: pointer to the buffer itself.

*/

void fifobuffer_gotolast(FIFOBUFFER *buffer);

/*

fifobuffer_save: Saves the current position for looking ahead.
parameters:
	buffer: pointer to the buffer itself.

*/

void fifobuffer_save(FIFOBUFFER *buffer);

/*

fifobuffer_restore: Restores the current position after having been saved.
parameters:
	buffer: pointer to the buffer itself.

*/

void fifobuffer_restore(FIFOBUFFER *buffer);


/*

fifobuffer_clear: Remove all FIFO items from the buffer!
parameters:
	buffer: pointer to the buffer itself.

*/


void fifobuffer_clear(FIFOBUFFER *buffer);

/*

movefifobuffer8: Moved threshold items from the source to the destination buffer once threshold bytes are used.
parameters:
	src: pointer to the source buffer.
	dest: pointer to the destination buffer.
	threshold: The threshold, in FIFO buffer items.

*/

void movefifobuffer8(FIFOBUFFER *src, FIFOBUFFER *dest, uint_32 threshold);

/* 16-bit adjustments */

byte peekfifobuffer16(FIFOBUFFER *buffer, word *result); //Is there data to be read?
byte readfifobuffer16(FIFOBUFFER *buffer, word *result);
byte readfifobuffer16_backtrace(FIFOBUFFER *buffer, word *result, uint_32 backtrace, byte finalbacktrace);
byte writefifobuffer16(FIFOBUFFER *buffer, word data);
void movefifobuffer16(FIFOBUFFER *src, FIFOBUFFER *dest, uint_32 threshold);

/* 32-bit adjustments */

byte peekfifobuffer32(FIFOBUFFER *buffer, uint_32 *result); //Is there data to be read?
byte peekfifobuffer32_2(FIFOBUFFER *buffer, int_32 *result, int_32 *result2); //Is there data to be read?
byte peekfifobuffer32_2u(FIFOBUFFER* buffer, uint_32* result, uint_32* result2); //Is there data to be read?
byte readfifobuffer32(FIFOBUFFER *buffer, uint_32 *result);
byte readfifobuffer32_2(FIFOBUFFER *buffer, int_32 *result, int_32 *result2);
byte readfifobuffer32_2u(FIFOBUFFER *buffer, uint_32 *result, uint_32 *result2);
byte readfifobuffer32_backtrace(FIFOBUFFER *buffer, uint_32 *result, uint_32 backtrace, byte finalbacktrace);
byte readfifobuffer32_backtrace_2(FIFOBUFFER *buffer, int_32 *result, int_32 *result2, uint_32 backtrace, byte finalbacktrace);
byte readfifobuffer32_backtrace_2u(FIFOBUFFER *buffer, uint_32 *result, uint_32 *result2, uint_32 backtrace, byte finalbacktrace);
byte writefifobuffer32(FIFOBUFFER *buffer, uint_32 data);
byte writefifobuffer32_2(FIFOBUFFER *buffer, int_32 data, int_32 data2);
byte writefifobuffer32_2u(FIFOBUFFER *buffer, uint_32 data, uint_32 data2);
void movefifobuffer32(FIFOBUFFER *src, FIFOBUFFER *dest, uint_32 threshold);

//Floating-point simple storage support!
byte writefifobufferflt(FIFOBUFFER *buffer, float data);
byte writefifobufferflt_2(FIFOBUFFER *buffer, float data, float data2);
byte readfifobufferflt(FIFOBUFFER *buffer, float *result);
byte readfifobufferflt_2(FIFOBUFFER *buffer, float *result, float *result2);
byte readfifobufferflt_backtrace(FIFOBUFFER *buffer, float *result, uint_32 backtrace, byte finalbacktrace);
byte readfifobufferflt_backtrace_2(FIFOBUFFER *buffer, float *result, float *result2, uint_32 backtrace, byte finalbacktrace);
#endif