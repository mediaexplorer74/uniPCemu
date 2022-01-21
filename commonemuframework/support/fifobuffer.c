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
#include "headers/support/zalloc.h" //Zero allocation support!
#include "headers/support/fifobuffer.h" //Our types etc.
#include "headers/support/signedness.h" //Signedness conversion

//Are we disabled?
#define __HW_DISABLED 0

//Mask for the last read/write to give the size, depending on the last status!
#define LASTSTATUS_READ buffer->size
#define LASTSTATUS_WRITE 0

#ifdef IS_BIG_ENDIAN
//Big endian needs swapping?
#define LE16(x) SDL_SwapLE16(x)
#define LE32(x) SDL_SwapLE32(x)
#else
#define LE16(x) (x)
#define LE32(x) (x)
#endif

extern byte allcleared; //Are all pointers cleared?

/*

newbuffer: generates a new buffer to work with.
parameters:
	buffersize: the size of the buffer!
	filledcontainer: pointer to filled variabele for this buffer
	sizecontainer: pointer to size variabele for this buffer
result:
	Buffer for allocated, NULL for error when allocating!

*/

FIFOBUFFER* allocfifobuffer(uint_32 buffersize, byte lockable)
{
	FIFOBUFFER *buffer;
	if (__HW_DISABLED) return NULL; //Abort!
	buffer = (FIFOBUFFER *)zalloc(sizeof(FIFOBUFFER),"FIFOBuffer",NULL); //Allocate an container!
	if (buffer) //Allocated container?
	{
		buffer->buffer = (byte *)zalloc(buffersize,"FIFOBuffer_Buffer",NULL); //Try to allocate the buffer!
		if (!buffer->buffer) //No buffer?
		{
			freez((void **)&buffer,sizeof(FIFOBUFFER),"Failed FIFOBuffer"); //Release the container!
			return NULL; //Not allocated!
		}
		buffer->size = buffersize; //Set the buffer size!
		if (lockable) //Lockable FIFO buffer?
		{
			buffer->lock = SDL_CreateSemaphore(1); //Create our lock!
			if (!buffer->lock) //Failed to lock?
			{
				freez((void **)&buffer, sizeof(FIFOBUFFER), "Failed FIFOBuffer"); //Release the container!
				freez((void **)&buffer->buffer, buffersize, "FIFOBuffer_Buffer"); //Release the buffer!
				return NULL; //Not allocated: can't lock!
			}
		}
		//The rest is ready to work with: all 0!
		buffer->laststatus = LASTSTATUS_READ; //Initialize read position!
		buffer->savedpos.laststatus = LASTSTATUS_READ; //Initialize read position!
	}
	return buffer; //Give the allocated FIFO container!
}

void free_fifobuffer(FIFOBUFFER **buffer)
{
	FIFOBUFFER *container;
	if (__HW_DISABLED) return; //Abort!
	if (buffer) //Valid?
	{
		if (memprotect(*buffer,sizeof(FIFOBUFFER),NULL)) //Valid point?
		{
			container = *buffer; //Get the buffer itself!
			if (memprotect(container->buffer,container->size,NULL)) //Valid?
			{
				freez((void **)&container->buffer,container->size,"Free FIFOBuffer_buffer"); //Release the buffer!
			}
			SDL_DestroySemaphore(container->lock); //Release the lock!
		}
		freez((void **)buffer,sizeof(FIFOBUFFER),"Free FIFOBuffer"); //Free the buffer container!
	}
}

OPTINLINE uint_32 fifobuffer_INTERNAL_freesize(FIFOBUFFER *buffer)
{
	if (__HW_DISABLED) return 0; //Abort!
	INLINEREGISTER uint_32 readpos, writepos;
	if ((readpos = buffer->readpos)!=(writepos = buffer->writepos)) //Not at the same position to read&write?
	{
		if (readpos>writepos) //Read after write index? We're a simple difference!
		{
			return readpos - writepos;
		}
		else //Read before write index? We're a complicated difference!
		{
			//The read position is before or at the write position? We wrap arround!
			return (buffer->size -writepos) + readpos;
		}
	}
	else //Readpos = Writepos? Either full or empty?
	{
		return buffer->laststatus; //Empty when last was read, else full!
	}
}

OPTINLINE uint_32 fifobuffer_INTERNAL_size(FIFOBUFFER* buffer)
{
	if (__HW_DISABLED) return 0; //Abort!
	return buffer->size; //The size of the buffer!
}

//More simple full/empty checks!
OPTINLINE uint_32 fifobuffer_INTERNAL_isfull(FIFOBUFFER *buffer)
{
	if (__HW_DISABLED) return 0; //Abort!
	if (likely(buffer->readpos!=buffer->writepos)) //Not at the same position to read&write?
	{
		return 0; //Not full!
	}
	else //Readpos = Writepos? Either full or empty?
	{
		return (buffer->laststatus==0); //Empty when last was read, else full!
	}
}

OPTINLINE uint_32 fifobuffer_INTERNAL_isempty(FIFOBUFFER* buffer)
{
	if (__HW_DISABLED) return 0; //Abort!
	if (likely(buffer->readpos!=buffer->writepos)) //Not at the same position to read&write?
	{
		return 0; //Not empty!
	}
	else //Readpos = Writepos? Either full or empty?
	{
		return (buffer->laststatus!=0); //Empty when last was read, else full!
	}
}

uint_32 fifobuffer_freesize(FIFOBUFFER *buffer)
{
	INLINEREGISTER uint_32 result;
	if (unlikely(buffer==0)) return 0; //Error: invalid buffer!
	if (unlikely(buffer->buffer==0)) return 0; //Error invalid: buffer!
	if (unlikely(allcleared)) return 0; //Abort: invalid buffer!
	if (buffer->lock) //Locked buffer?
	{
		WaitSem(buffer->lock)
		result = fifobuffer_INTERNAL_freesize(buffer); //Original wrapper!
		PostSem(buffer->lock)
	}
	else //Lockless?
	{
		return fifobuffer_INTERNAL_freesize(buffer); //Original wrapper!
	}
	return result; //Give the result!
}

uint_32 fifobuffer_size(FIFOBUFFER* buffer)
{
	INLINEREGISTER uint_32 result;
	if (unlikely(buffer == 0)) return 0; //Error: invalid buffer!
	if (unlikely(buffer->buffer == 0)) return 0; //Error invalid: buffer!
	if (unlikely(allcleared)) return 0; //Abort: invalid buffer!
	if (buffer->lock) //Locked buffer?
	{
		WaitSem(buffer->lock)
			result = fifobuffer_INTERNAL_size(buffer); //Original wrapper!
		PostSem(buffer->lock)
	}
	else //Lockless?
	{
		return fifobuffer_INTERNAL_size(buffer); //Original wrapper!
	}
	return result; //Give the result!
}

void waitforfreefifobuffer(FIFOBUFFER *buffer, uint_32 size)
{
	if (__HW_DISABLED) return; //Abort!
	for (;unlikely(fifobuffer_freesize(buffer)<size);) delay(0); //Wait for the buffer to have enough free size!
}

byte peekfifobuffer(FIFOBUFFER *buffer, byte *result) //Is there data to be read?
{
	if (__HW_DISABLED) return 0; //Abort!
	if (unlikely(buffer==0)) return 0; //Error: invalid buffer!
	if (unlikely(buffer->buffer==0)) return 0; //Error invalid: buffer!
	if (unlikely(allcleared)) return 0; //Abort: invalid buffer!

	if (buffer->lock)
	{
		WaitSem(buffer->lock)
		if (fifobuffer_INTERNAL_freesize(buffer)<buffer->size) //Filled?
		{
			*result = buffer->buffer[buffer->readpos]; //Give the data!
			PostSem(buffer->lock)
			return 1; //Something to peek at!
		}
		PostSem(buffer->lock)
	}
	else
	{
		if (fifobuffer_INTERNAL_freesize(buffer)<buffer->size) //Filled?
		{
			*result = buffer->buffer[buffer->readpos]; //Give the data!
			return 1; //Something to peek at!
		}
	}
	return 0; //Nothing to peek at!
}

OPTINLINE void readfifobufferunlocked(FIFOBUFFER *buffer, byte *result)
{
	INLINEREGISTER uint_32 readpos;
	readpos = buffer->readpos; //Load the old read position!
	*result = buffer->buffer[readpos++]; //Read and update!
	if (unlikely(readpos >= buffer->size)) readpos = 0; //Wrap arround when needed!
	buffer->readpos = readpos; //Update the read position!
	buffer->laststatus = LASTSTATUS_READ; //Last operation was a read operation!
}

byte readfifobuffer(FIFOBUFFER *buffer, byte *result)
{
	if (__HW_DISABLED) return 0; //Abort!
	if (unlikely(buffer==0)) return 0; //Error: invalid buffer!
	if (unlikely(allcleared)) return 0; //Abort: invalid buffer!
	if (likely(buffer->buffer)) //Valid buffer?
	{
		if (buffer->lock)
		{
			WaitSem(buffer->lock)
			if (fifobuffer_INTERNAL_isempty(buffer)==0) //Filled?
			{
				readfifobufferunlocked(buffer,result); //Read the FIFO buffer without lock!
				PostSem(buffer->lock)
				return 1; //Read!
			}
			PostSem(buffer->lock)
		}
		else
		{
			if (fifobuffer_INTERNAL_isempty(buffer)==0) //Filled?
			{
				readfifobufferunlocked(buffer, result); //Read the FIFO buffer without lock!
				return 1; //Read!
			}
		}
	}
	return 0; //Nothing to read or invalid buffer!
}

OPTINLINE void writefifobufferunlocked(FIFOBUFFER *buffer, byte data)
{
	INLINEREGISTER uint_32 writepos;
	writepos = buffer->writepos; //Load the old write position!
	buffer->buffer[writepos++] = data; //Write and update!
	if (unlikely(writepos >= buffer->size)) writepos = 0; //Wrap arround when needed!
	buffer->writepos = writepos; //Update the write position!
	buffer->laststatus = LASTSTATUS_WRITE; //Last operation was a write operation!
}

byte writefifobuffer(FIFOBUFFER *buffer, byte data)
{
	if (__HW_DISABLED) return 0; //Abort!
	if (unlikely(buffer==0)) return 0; //Error: invalid buffer!
	if (unlikely(buffer->buffer==0)) return 0; //Error invalid: buffer!
	if (unlikely(allcleared)) return 0; //Abort: invalid buffer!

	if (buffer->lock)
	{
		WaitSem(buffer->lock)
		if (unlikely(fifobuffer_INTERNAL_isfull(buffer))) //Buffer full?
		{
			PostSem(buffer->lock)
			return 0; //Error: buffer full!
		}
		writefifobufferunlocked(buffer,data); //Write the FIFO buffer without lock!
		PostSem(buffer->lock)
	}
	else
	{
		if (unlikely(fifobuffer_INTERNAL_isfull(buffer))) //Buffer full?
		{
			return 0; //Error: buffer full!
		}
		writefifobufferunlocked(buffer, data); //Write the FIFO buffer without lock!
	}
	return 1; //Written!
}

OPTINLINE void readfifobuffer16unlocked(FIFOBUFFER *buffer, word *result, byte updateposition)
{
	#include "headers/packed.h"
	union PACKED
	{
		uint_32 resultw;
		struct
		{
			byte byte1; //Low byte
			byte byte2; //High byte
		};
	} temp;
	#include "headers/endpacked.h"
	INLINEREGISTER uint_32 readpos,size;
	size = buffer->size; //Size of the buffer to wrap around!
	readpos = buffer->readpos; //Load the old read position!
	if (likely(((size|readpos)&1)==0)) //Aligned access in aligned buffer?
	{
		temp.resultw = *((word *)&buffer->buffer[readpos]); //Read 16-bit aligned!
		readpos += 2;
	}
	else //Unaligned read?
	{
		temp.byte1 = (buffer->buffer[readpos++]); //Read and update high!
		if (unlikely(readpos >= size)) readpos = 0; //Wrap arround when needed!
		temp.byte2 = buffer->buffer[readpos++]; //Read and update low!
	}
	temp.resultw = LE16(temp.resultw); //Convert to native ordering, if needed!
	*result = temp.resultw; //Save the result retrieved, from LE format!
	if (updateposition)
	{
		if (unlikely(readpos >= buffer->size)) readpos = 0; //Wrap arround when needed!
		buffer->readpos = readpos; //Update our the position!
		buffer->laststatus = LASTSTATUS_READ; //Last operation was a read operation!
	}
}

OPTINLINE void readfifobuffer32unlocked(FIFOBUFFER *buffer, uint_32 *result, byte updateposition)
{
	#include "headers/packed.h"
	union PACKED
	{
		uint_32 resultd;
		struct
		{
			byte byte1; //Low byte
			byte byte2; //High byte
			byte byte3; //Low byte - High
			byte byte4; //High byte - High
		};
	} temp;
	#include "headers/endpacked.h"
	INLINEREGISTER uint_32 readpos,size;
	size = buffer->size; //Size of the buffer to wrap around!
	readpos = buffer->readpos; //Load the old read position!
	if (likely(((size|readpos)&3)==0)) //Aligned access in aligned buffer?
	{
		temp.resultd = *((uint_32 *)&buffer->buffer[readpos]); //Read 32-bit aligned!
		readpos += 4;
	}
	else //Unaligned read?
	{
		temp.byte1 = (buffer->buffer[readpos++]); //Read and update high!
		if (unlikely(readpos >= size)) readpos = 0; //Wrap arround when needed!
		temp.byte2 = buffer->buffer[readpos++]; //Read and update low!
		if (unlikely(readpos >= size)) readpos = 0; //Wrap arround when needed!
		temp.byte3 = buffer->buffer[readpos++]; //Read and update low!
		if (unlikely(readpos >= size)) readpos = 0; //Wrap arround when needed!
		temp.byte4 = buffer->buffer[readpos++]; //Read and update low!
	}
	temp.resultd = LE32(temp.resultd); //Convert to native ordering if needed!
	*result = temp.resultd; //Save the result retrieved, in LE format!
	if (updateposition)
	{
		if (unlikely(readpos >= buffer->size)) readpos = 0; //Wrap arround when needed!
		buffer->readpos = readpos; //Update our the position!
		buffer->laststatus = LASTSTATUS_READ; //Last operation was a read operation!
	}
}

OPTINLINE void readfifobuffer64unlocked(FIFOBUFFER* buffer, uint_32* result, uint_32 *result2, byte updateposition)
{
#include "headers/packed.h"
	union PACKED
	{
		uint_32 resultd;
		struct
		{
			byte byte1; //Low byte
			byte byte2; //High byte
			byte byte3; //Low byte - High
			byte byte4; //High byte - High
		};
	} temp;
#include "headers/endpacked.h"
#include "headers/packed.h"
	union PACKED
	{
		uint_32 resultd;
		struct
		{
			byte byte1; //Low byte
			byte byte2; //High byte
			byte byte3; //Low byte - High
			byte byte4; //High byte - High
		};
	} temp2;
#include "headers/endpacked.h"
	INLINEREGISTER uint_32 readpos, size;
	size = buffer->size; //Size of the buffer to wrap around!
	readpos = buffer->readpos; //Load the old read position!
	if (likely(((size | readpos) & 7) == 0)) //Aligned access in aligned buffer?
	{
		temp.resultd = *((uint_32*)&buffer->buffer[readpos]); //Read 32-bit aligned!
		readpos += 4;
		temp2.resultd = *((uint_32*)&buffer->buffer[readpos]); //Read 32-bit aligned!
		readpos += 4;
	}
	else //Unaligned read?
	{
		temp.byte1 = (buffer->buffer[readpos++]); //Read and update high!
		if (unlikely(readpos >= size)) readpos = 0; //Wrap arround when needed!
		temp.byte2 = buffer->buffer[readpos++]; //Read and update low!
		if (unlikely(readpos >= size)) readpos = 0; //Wrap arround when needed!
		temp.byte3 = buffer->buffer[readpos++]; //Read and update low!
		if (unlikely(readpos >= size)) readpos = 0; //Wrap arround when needed!
		temp.byte4 = buffer->buffer[readpos++]; //Read and update low!
		if (unlikely(readpos >= size)) readpos = 0; //Wrap arround when needed!
		temp2.byte1 = (buffer->buffer[readpos++]); //Read and update high!
		if (unlikely(readpos >= size)) readpos = 0; //Wrap arround when needed!
		temp2.byte2 = buffer->buffer[readpos++]; //Read and update low!
		if (unlikely(readpos >= size)) readpos = 0; //Wrap arround when needed!
		temp2.byte3 = buffer->buffer[readpos++]; //Read and update low!
		if (unlikely(readpos >= size)) readpos = 0; //Wrap arround when needed!
		temp2.byte4 = buffer->buffer[readpos++]; //Read and update low!
	}
	temp.resultd = LE32(temp.resultd); //Convert to native ordering if needed!
	temp2.resultd = LE32(temp2.resultd); //Convert to native ordering if needed!
	*result = temp.resultd; //Save the result retrieved, in LE format!
	*result2 = temp2.resultd; //Save the result retrieved, in LE format!
	if (updateposition)
	{
		if (unlikely(readpos >= buffer->size)) readpos = 0; //Wrap arround when needed!
		buffer->readpos = readpos; //Update our the position!
		buffer->laststatus = LASTSTATUS_READ; //Last operation was a read operation!
	}
}

byte peekfifobuffer16(FIFOBUFFER* buffer, word* result) //Is there data to be read?
{
	if (__HW_DISABLED) return 0; //Abort!
	if (unlikely(buffer == 0)) return 0; //Error: invalid buffer!
	if (unlikely(buffer->buffer == 0)) return 0; //Error invalid: buffer!
	if (unlikely(allcleared)) return 0; //Abort: invalid buffer!

	if (buffer->lock)
	{
		WaitSem(buffer->lock)
		if (fifobuffer_INTERNAL_freesize(buffer) < (buffer->size - 1)) //Filled?
		{
			readfifobuffer16unlocked(buffer, result, 0); //Read directly!
			PostSem(buffer->lock)
			return 1; //Something to peek at!
		}
		PostSem(buffer->lock)
	}
	else
	{
		if (fifobuffer_INTERNAL_freesize(buffer) < (buffer->size - 1)) //Filled?
		{
			readfifobuffer16unlocked(buffer, result, 0); //Read directly!
			return 1; //Something to peek at!
		}
	}
	return 0; //Nothing to peek at!
}

byte peekfifobuffer32(FIFOBUFFER* buffer, uint_32* result) //Is there data to be read?
{
	if (__HW_DISABLED) return 0; //Abort!
	if (unlikely(buffer == 0)) return 0; //Error: invalid buffer!
	if (unlikely(buffer->buffer == 0)) return 0; //Error invalid: buffer!
	if (unlikely(allcleared)) return 0; //Abort: invalid buffer!

	if (buffer->lock)
	{
		WaitSem(buffer->lock)
		if (fifobuffer_INTERNAL_freesize(buffer) < (buffer->size - 3)) //Filled?
		{
			readfifobuffer32unlocked(buffer, result, 0); //Read directly!
			PostSem(buffer->lock)
			return 1; //Something to peek at!
		}
		PostSem(buffer->lock)
	}
	else
	{
		if (fifobuffer_INTERNAL_freesize(buffer) < (buffer->size - 3)) //Filled?
		{
			readfifobuffer32unlocked(buffer, result, 0); //Read directly!
			return 1; //Something to peek at!
		}
	}
	return 0; //Nothing to peek at!
}

byte peekfifobuffer32_2(FIFOBUFFER* buffer, int_32* result, int_32* result2) //Is there data to be read?
{
	uint_32 t, t2;
	if (__HW_DISABLED) return 0; //Abort!
	if (unlikely(buffer == 0)) return 0; //Error: invalid buffer!
	if (unlikely(buffer->buffer == 0)) return 0; //Error invalid: buffer!
	if (unlikely(allcleared)) return 0; //Abort: invalid buffer!

	if (buffer->lock)
	{
		WaitSem(buffer->lock)
		if (fifobuffer_INTERNAL_freesize(buffer) < (buffer->size - 7)) //Filled?
		{
			readfifobuffer64unlocked(buffer, &t, &t2, 0); //Read directly!
			*result = unsigned2signed32(t); //Convert to signed!
			*result2 = unsigned2signed32(t2); //Convert to signed!
			PostSem(buffer->lock)
			return 1; //Something to peek at!
		}
		PostSem(buffer->lock)
	}
	else
	{
		if (fifobuffer_INTERNAL_freesize(buffer) < (buffer->size - 7)) //Filled?
		{
			readfifobuffer64unlocked(buffer, &t, &t2, 0); //Read directly!
			*result = unsigned2signed32(t); //Convert to signed!
			*result2 = unsigned2signed32(t2); //Convert to signed!
			return 1; //Something to peek at!
		}
	}
	return 0; //Nothing to peek at!
}

byte peekfifobuffer32_2u(FIFOBUFFER* buffer, uint_32* result, uint_32* result2) //Is there data to be read?
{
	if (__HW_DISABLED) return 0; //Abort!
	if (unlikely(buffer == 0)) return 0; //Error: invalid buffer!
	if (unlikely(buffer->buffer == 0)) return 0; //Error invalid: buffer!
	if (unlikely(allcleared)) return 0; //Abort: invalid buffer!

	if (buffer->lock)
	{
		WaitSem(buffer->lock)
		if (fifobuffer_INTERNAL_freesize(buffer) < (buffer->size - 7)) //Filled?
		{
			readfifobuffer64unlocked(buffer, result, result2, 0); //Read directly!
			PostSem(buffer->lock)
			return 1; //Something to peek at!
		}
		PostSem(buffer->lock)
	}
	else
	{
		if (fifobuffer_INTERNAL_freesize(buffer) < (buffer->size - 7)) //Filled?
		{
			readfifobuffer64unlocked(buffer, result, result2, 0); //Read directly!
			return 1; //Something to peek at!
		}
	}
	return 0; //Nothing to peek at!
}

byte readfifobuffer16(FIFOBUFFER *buffer, word *result)
{
	if (__HW_DISABLED) return 0; //Abort!
	if (unlikely(buffer==0)) return 0; //Error: invalid buffer!
	if (unlikely(buffer->buffer==0)) return 0; //Error invalid: buffer!
	if (unlikely(allcleared)) return 0; //Abort: invalid buffer!

	if (buffer->lock)
	{
		WaitSem(buffer->lock)
		if (fifobuffer_INTERNAL_freesize(buffer)<(buffer->size-1)) //Filled?
		{
			readfifobuffer16unlocked(buffer,result,1); //Read the FIFO buffer without lock!
			PostSem(buffer->lock)
			return 1; //Read!
		}
		PostSem(buffer->lock)
	}
	else
	{
		if (fifobuffer_INTERNAL_freesize(buffer)<(buffer->size - 1)) //Filled?
		{
			readfifobuffer16unlocked(buffer, result,1); //Read the FIFO buffer without lock!
			return 1; //Read!
		}
	}
	return 0; //Nothing to read!
}

byte readfifobuffer16_backtrace(FIFOBUFFER *buffer, word *result, uint_32 backtrace, byte finalbacktrace)
{
	int_64 readposhistory;
	uint_32 oldreadpos;
	if (__HW_DISABLED) return 0; //Abort!
	if (unlikely(buffer==0)) return 0; //Error: invalid buffer!
	if (unlikely(buffer->buffer==0)) return 0; //Error invalid: buffer!
	if (unlikely(allcleared)) return 0; //Abort: invalid buffer!

	if (buffer->lock)
	{
		WaitSem(buffer->lock)
		if (fifobuffer_INTERNAL_freesize(buffer)<((buffer->size-1)-(backtrace<<1))) //Filled enough?
		{
			if (unlikely(finalbacktrace)) //No history? Just pop final input!
			{
				readfifobuffer16unlocked(buffer,result,1); //Read the FIFO buffer without lock!
				PostSem(buffer->lock)
				return 1; //Read!
			}
			//Normal tap?
			readposhistory = (int_64)buffer->readpos; //Save the read position!
			readposhistory -= ((int_64)backtrace<<1); //Trace this far back!
			if (unlikely(readposhistory<0)) //To make valid?
			{
				do //Invalid?
				{
					readposhistory += buffer->size; //Convert into valid range!
				} while (readposhistory<0);
			}
			readposhistory = SAFEMOD(readposhistory,buffer->size); //Make sure we don't get past the end of the buffer!
			oldreadpos = buffer->readpos; //Save the read position!
			buffer->readpos = (uint_32)readposhistory; //Patch the read position to the required state!
			readfifobuffer16unlocked(buffer,result,0); //Read the FIFO buffer without lock!
			buffer->readpos = oldreadpos; //Restore the old read position to it's original state!
			PostSem(buffer->lock)
			return 1; //Read!
		}
		PostSem(buffer->lock)
	}
	else
	{
		if (fifobuffer_INTERNAL_freesize(buffer)>=((buffer->size-1)-(backtrace<<1))) return 0; //Filled enough?
		{
			if (unlikely(finalbacktrace)) //No history? Just pop final input!
			{
				readfifobuffer16unlocked(buffer,result,1); //Read the FIFO buffer without lock!
				return 1; //Read!
			}
			//Normal tap?
			readposhistory = (int_64)buffer->readpos; //Save the read position!
			readposhistory -= ((int_64)backtrace<<1); //Trace this far back!
			if (unlikely(readposhistory<0)) //To make valid?
			{
				do //Invalid?
				{
					readposhistory += buffer->size; //Convert into valid range!
				} while (readposhistory<0);
			}
			readposhistory = SAFEMOD(readposhistory,buffer->size); //Make sure we don't get past the end of the buffer!
			oldreadpos = buffer->readpos; //Save the read position!
			buffer->readpos = (uint_32)readposhistory; //Patch the read position to the required state!
			readfifobuffer16unlocked(buffer,result,0); //Read the FIFO buffer without lock!
			buffer->readpos = oldreadpos; //Restore the old read position to it's original state!
			return 1; //Read!
		}
	}
	return 0; //Nothing to read!
}

byte readfifobuffer32(FIFOBUFFER *buffer, uint_32 *result)
{
	if (__HW_DISABLED) return 0; //Abort!
	if (unlikely(buffer==0)) return 0; //Error: invalid buffer!
	if (unlikely(buffer->buffer==0)) return 0; //Error invalid: buffer!
	if (unlikely(allcleared)) return 0; //Abort: invalid buffer!

	if (buffer->lock)
	{
		WaitSem(buffer->lock)
		if (fifobuffer_INTERNAL_freesize(buffer)<(buffer->size-3)) //Filled?
		{
			readfifobuffer32unlocked(buffer,result,1); //Read the FIFO buffer without lock!
			PostSem(buffer->lock)
			return 1; //Read!
		}
		PostSem(buffer->lock)
	}
	else
	{
		if (fifobuffer_INTERNAL_freesize(buffer)<(buffer->size-3)) //Filled?
		{
			readfifobuffer32unlocked(buffer, result,1); //Read the FIFO buffer without lock!
			return 1; //Read!
		}
	}
	return 0; //Nothing to read!
}

byte readfifobuffer32_2(FIFOBUFFER *buffer, int_32 *result, int_32 *result2)
{
	uint_32 t, t2;
	if (__HW_DISABLED) return 0; //Abort!
	if (unlikely(buffer==0)) return 0; //Error: invalid buffer!
	if (unlikely(buffer->buffer==0)) return 0; //Error invalid: buffer!
	if (unlikely(allcleared)) return 0; //Abort: invalid buffer!

	if (buffer->lock)
	{
		WaitSem(buffer->lock)
		if (fifobuffer_INTERNAL_freesize(buffer)<(buffer->size-7)) //Filled?
		{
			readfifobuffer64unlocked(buffer,&t,&t2,1); //Read the FIFO buffer without lock!
			*result = unsigned2signed32(t); //Convert to signed!
			*result2 = unsigned2signed32(t2); //Convert to signed!
			PostSem(buffer->lock)
			return 1; //Read!
		}
		PostSem(buffer->lock)
	}
	else
	{
		if (fifobuffer_INTERNAL_freesize(buffer)<(buffer->size-7)) //Filled?
		{
			readfifobuffer64unlocked(buffer, &t,&t2,1); //Read the FIFO buffer without lock!
			*result = unsigned2signed32(t); //Convert to signed!
			*result2 = unsigned2signed32(t2); //Convert to signed!
			return 1; //Read!
		}
	}
	return 0; //Nothing to read!
}

byte readfifobuffer32_2u(FIFOBUFFER *buffer, uint_32 *result, uint_32 *result2)
{
	if (__HW_DISABLED) return 0; //Abort!
	if (unlikely(buffer==0)) return 0; //Error: invalid buffer!
	if (unlikely(buffer->buffer==0)) return 0; //Error invalid: buffer!
	if (unlikely(allcleared)) return 0; //Abort: invalid buffer!

	if (buffer->lock)
	{
		WaitSem(buffer->lock)
		if (fifobuffer_INTERNAL_freesize(buffer)<(buffer->size-7)) //Filled?
		{
			readfifobuffer64unlocked(buffer,result,result2,1); //Read the FIFO buffer without lock!
			PostSem(buffer->lock)
			return 1; //Read!
		}
		PostSem(buffer->lock)
	}
	else
	{
		if (fifobuffer_INTERNAL_freesize(buffer)<(buffer->size-7)) //Filled?
		{
			readfifobuffer64unlocked(buffer, result,result2,1); //Read the FIFO buffer without lock!
			return 1; //Read!
		}
	}
	return 0; //Nothing to read!
}

byte readfifobuffer32_backtrace(FIFOBUFFER *buffer, uint_32 *result, uint_32 backtrace, byte finalbacktrace)
{
	int_64 readposhistory;
	uint_32 oldreadpos;
	if (__HW_DISABLED) return 0; //Abort!
	if (unlikely(buffer==0)) return 0; //Error: invalid buffer!
	if (unlikely(buffer->buffer==0)) return 0; //Error invalid: buffer!
	if (unlikely(allcleared)) return 0; //Abort: invalid buffer!

	if (buffer->lock)
	{
		WaitSem(buffer->lock)
		if (fifobuffer_INTERNAL_freesize(buffer)<((buffer->size-3)-(backtrace<<2))) //Filled?
		{
			if (unlikely(finalbacktrace)) //No history? Just pop final input!
			{
				readfifobuffer32unlocked(buffer,result,1); //Read the FIFO buffer without lock!
				PostSem(buffer->lock)
				return 1; //Read!
			}
			//Normal tap?
			readposhistory = (int_64)buffer->readpos; //Save the read position!
			readposhistory -= ((int_64)backtrace<<2); //Trace this far back!
			if (unlikely(readposhistory<0)) //To make valid?
			{
				do //Invalid?
				{
					readposhistory += buffer->size; //Convert into valid range!
				} while (readposhistory<0); //Convert to valid range!
			}
			readposhistory = SAFEMOD(readposhistory,buffer->size); //Make sure we don't get past the end of the buffer!
			oldreadpos = buffer->readpos; //Save the read position!
			buffer->readpos = (uint_32)readposhistory; //Patch the read position to the required state!
			readfifobuffer32unlocked(buffer,result,0); //Read the FIFO buffer without lock!
			buffer->readpos = oldreadpos; //Restore the old read position to it's original state!
			PostSem(buffer->lock)
			return 1; //Read!
		}
		PostSem(buffer->lock)
	}
	else
	{
		if (fifobuffer_INTERNAL_freesize(buffer)>=((buffer->size-3)-(backtrace<<2))) return 0; //Filled?
		{
			if (unlikely(finalbacktrace)) //No history? Just pop final input!
			{
				readfifobuffer32unlocked(buffer,result,1); //Read the FIFO buffer without lock!
				return 1; //Read!
			}
			//Normal tap?
			readposhistory = (int_64)buffer->readpos; //Save the read position!
			readposhistory -= ((int_64)backtrace<<2); //Trace this far back!
			if (unlikely(readposhistory<0)) //To make valid?
			{
				do //Invalid?
				{
					readposhistory += buffer->size; //Convert into valid range!
				} while (readposhistory<0);
			}
			readposhistory = SAFEMOD(readposhistory,buffer->size); //Make sure we don't get past the end of the buffer!
			oldreadpos = buffer->readpos; //Save the read position!
			buffer->readpos = (uint_32)readposhistory; //Patch the read position to the required state!
			readfifobuffer32unlocked(buffer,result,0); //Read the FIFO buffer without lock!
			buffer->readpos = oldreadpos; //Restore the old read position to it's original state!
			return 1; //Read!
		}
	}
	return 0; //Nothing to read!
}

byte readfifobuffer32_backtrace_2(FIFOBUFFER *buffer, int_32 *result, int_32 *result2, uint_32 backtrace, byte finalbacktrace)
{
	uint_32 t, t2;
	int_64 readposhistory;
	uint_32 oldreadpos;
	if (__HW_DISABLED) return 0; //Abort!
	if (unlikely(buffer==0)) return 0; //Error: invalid buffer!
	if (unlikely(buffer->buffer==0)) return 0; //Error invalid: buffer!
	if (unlikely(allcleared)) return 0; //Abort: invalid buffer!

	if (buffer->lock)
	{
		WaitSem(buffer->lock)
		if (fifobuffer_INTERNAL_freesize(buffer)<((buffer->size-7)-(backtrace<<3))) //Filled?
		{
			if (unlikely(finalbacktrace)) //No history? Just pop final input!
			{
				readfifobuffer64unlocked(buffer,(uint_32 *)result,(uint_32 *)result2,1); //Read the FIFO buffer without lock!
				PostSem(buffer->lock)
				return 1; //Read!
			}
			//Normal tap?
			readposhistory = (int_64)buffer->readpos; //Save the read position!
			readposhistory -= ((int_64)backtrace<<3); //Trace this far back!
			if (unlikely(readposhistory<0)) //To make valid?
			{
				do //Invalid?
				{
					readposhistory += buffer->size; //Convert into valid range!
				} while (readposhistory<0); //Convert to valid range!
			}
			readposhistory = SAFEMOD(readposhistory,buffer->size); //Make sure we don't get past the end of the buffer!
			oldreadpos = buffer->readpos; //Save the read position!
			buffer->readpos = (uint_32)readposhistory; //Patch the read position to the required state!
			readfifobuffer64unlocked(buffer,&t,&t2,0); //Read the FIFO buffer without lock!
			*result = unsigned2signed32(t); //Convert to signed!
			*result2 = unsigned2signed32(t2); //Convert to signed!
			buffer->readpos = oldreadpos; //Restore the old read position to it's original state!
			PostSem(buffer->lock)
			return 1; //Read!
		}
		PostSem(buffer->lock)
	}
	else
	{
		if (fifobuffer_INTERNAL_freesize(buffer)>=((buffer->size-7)-(backtrace<<3))) return 0; //Filled?
		{
			if (unlikely(finalbacktrace)) //No history? Just pop final input!
			{
				readfifobuffer64unlocked(buffer,(uint_32 *)result,(uint_32 *)result2,1); //Read the FIFO buffer without lock!
				return 1; //Read!
			}
			//Normal tap?
			readposhistory = (int_64)buffer->readpos; //Save the read position!
			readposhistory -= ((int_64)backtrace<<3); //Trace this far back!
			if (unlikely(readposhistory<0)) //To make valid?
			{
				do //Invalid?
				{
					readposhistory += buffer->size; //Convert into valid range!
				} while (readposhistory<0);
			}
			readposhistory = SAFEMOD(readposhistory,buffer->size); //Make sure we don't get past the end of the buffer!
			oldreadpos = buffer->readpos; //Save the read position!
			buffer->readpos = (uint_32)readposhistory; //Patch the read position to the required state!
			readfifobuffer64unlocked(buffer,&t,&t2,0); //Read the FIFO buffer without lock!
			*result = unsigned2signed32(t); //Convert to signed!
			*result2 = unsigned2signed32(t2); //Convert to signed!
			buffer->readpos = oldreadpos; //Restore the old read position to it's original state!
			return 1; //Read!
		}
	}
	return 0; //Nothing to read!
}

byte readfifobuffer32_backtrace_2u(FIFOBUFFER *buffer, uint_32 *result, uint_32 *result2, uint_32 backtrace, byte finalbacktrace)
{
	int_64 readposhistory;
	uint_32 oldreadpos;
	if (__HW_DISABLED) return 0; //Abort!
	if (unlikely(buffer==0)) return 0; //Error: invalid buffer!
	if (unlikely(buffer->buffer==0)) return 0; //Error invalid: buffer!
	if (unlikely(allcleared)) return 0; //Abort: invalid buffer!

	if (buffer->lock)
	{
		WaitSem(buffer->lock)
		if (fifobuffer_INTERNAL_freesize(buffer)<((buffer->size-7)-(backtrace<<3))) //Filled?
		{
			if (unlikely(finalbacktrace)) //No history? Just pop final input!
			{
				readfifobuffer64unlocked(buffer,result,result2,1); //Read the FIFO buffer without lock!
				PostSem(buffer->lock)
				return 1; //Read!
			}
			//Normal tap?
			readposhistory = (int_64)buffer->readpos; //Save the read position!
			readposhistory -= ((int_64)backtrace<<3); //Trace this far back!
			if (unlikely(readposhistory<0)) //To make valid?
			{
				do //Invalid?
				{
					readposhistory += buffer->size; //Convert into valid range!
				} while (readposhistory<0); //Convert to valid range!
			}
			readposhistory = SAFEMOD(readposhistory,buffer->size); //Make sure we don't get past the end of the buffer!
			oldreadpos = buffer->readpos; //Save the read position!
			buffer->readpos = (uint_32)readposhistory; //Patch the read position to the required state!
			readfifobuffer64unlocked(buffer,result,result2,0); //Read the FIFO buffer without lock!
			buffer->readpos = oldreadpos; //Restore the old read position to it's original state!
			PostSem(buffer->lock)
			return 1; //Read!
		}
		PostSem(buffer->lock)
	}
	else
	{
		if (fifobuffer_INTERNAL_freesize(buffer)>=((buffer->size-7)-(backtrace<<3))) return 0; //Filled?
		{
			if (unlikely(finalbacktrace)) //No history? Just pop final input!
			{
				readfifobuffer32unlocked(buffer,(uint_32 *)result,1); //Read the FIFO buffer without lock!
				readfifobuffer32unlocked(buffer,(uint_32 *)result2,1); //Read the FIFO buffer without lock!
				return 1; //Read!
			}
			//Normal tap?
			readposhistory = (int_64)buffer->readpos; //Save the read position!
			readposhistory -= ((int_64)backtrace<<3); //Trace this far back!
			if (unlikely(readposhistory<0)) //To make valid?
			{
				do //Invalid?
				{
					readposhistory += buffer->size; //Convert into valid range!
				} while (readposhistory<0);
			}
			readposhistory = SAFEMOD(readposhistory,buffer->size); //Make sure we don't get past the end of the buffer!
			oldreadpos = buffer->readpos; //Save the read position!
			buffer->readpos = (uint_32)readposhistory; //Patch the read position to the required state!
			readfifobuffer32unlocked(buffer,(uint_32 *)result,0); //Read the FIFO buffer without lock!
			readfifobuffer32unlocked(buffer,(uint_32 *)result2,0); //Read the FIFO buffer without lock!
			buffer->readpos = oldreadpos; //Restore the old read position to it's original state!
			return 1; //Read!
		}
	}
	return 0; //Nothing to read!
}

OPTINLINE void writefifobuffer16unlocked(FIFOBUFFER *buffer, word data)
{
	INLINEREGISTER uint_32 writepos, size;
	#include "headers/packed.h"
	union PACKED
	{
		word resultw;
		struct
		{
			byte byte1; //Low byte
			byte byte2; //High byte
		};
	} temp;
	#include "headers/endpacked.h"
	size = buffer->size; //Load the size!
	writepos = buffer->writepos; //Load the write position!
	temp.resultw = LE16(data); //Load the data to store, in LE format!
	if (likely(((size|writepos)&1)==0)) //Aligned access in aligned buffer?
	{
		*((word *)&buffer->buffer[writepos]) = temp.resultw; //Write 16-bit aligned!
		writepos += 2;
	}
	else //Unaligned read?
	{
		buffer->buffer[writepos++] = temp.byte1; //Write high and update!
		if (unlikely(writepos >= size)) writepos = 0; //Wrap arround when needed!
		buffer->buffer[writepos++] = temp.byte2; //Write high and update!
	}
	if (unlikely(writepos >= size)) writepos = 0; //Wrap arround when needed!
	buffer->writepos = writepos; //Update the write position!
	buffer->laststatus = LASTSTATUS_WRITE; //Last operation was a write operation!
}

OPTINLINE void writefifobuffer32unlocked(FIFOBUFFER *buffer, uint_32 data)
{
	INLINEREGISTER uint_32 writepos, size;
	#include "headers/packed.h"
	union PACKED
	{
		uint_32 resultd;
		struct
		{
			byte byte1; //Low byte
			byte byte2; //High byte
			byte byte3; //Low byte - High
			byte byte4; //High byte - High
		};
	} temp;
	#include "headers/endpacked.h"
	size = buffer->size; //Load the size!
	writepos = buffer->writepos; //Load the write position!
	temp.resultd = LE32(data); //Convert us to LE format!
	if (likely(((size|writepos)&3)==0)) //Aligned access in aligned buffer?
	{
		*((uint_32 *)&buffer->buffer[writepos]) = temp.resultd; //Write 32-bit aligned!
		writepos += 4;
	}
	else //Unaligned read?
	{
		buffer->buffer[writepos++] = temp.byte1; //Write high and update!
		if (unlikely(writepos >= size)) writepos = 0; //Wrap arround when needed!
		buffer->buffer[writepos++] = temp.byte2; //Write high and update!
		if (unlikely(writepos >= size)) writepos = 0; //Wrap arround when needed!
		buffer->buffer[writepos++] = temp.byte3; //Write high and update!
		if (unlikely(writepos >= size)) writepos = 0; //Wrap arround when needed!
		buffer->buffer[writepos++] = temp.byte4; //Write low and update!
	}
	if (unlikely(writepos >= size)) writepos = 0; //Wrap arround when needed!
	buffer->writepos = writepos; //Update the write position!
	buffer->laststatus = LASTSTATUS_WRITE; //Last operation was a write operation!
}

OPTINLINE void writefifobuffer64unlocked(FIFOBUFFER* buffer, uint_32 data, uint_32 data2)
{
	INLINEREGISTER uint_32 writepos, size;
#include "headers/packed.h"
	union PACKED
	{
		uint_32 resultd;
		struct
		{
			byte byte1; //Low byte
			byte byte2; //High byte
			byte byte3; //Low byte - High
			byte byte4; //High byte - High
		};
	} temp;
#include "headers/endpacked.h"
#include "headers/packed.h"
	union PACKED
	{
		uint_32 resultd;
		struct
		{
			byte byte1; //Low byte
			byte byte2; //High byte
			byte byte3; //Low byte - High
			byte byte4; //High byte - High
		};
	} temp2;
#include "headers/endpacked.h"
	size = buffer->size; //Load the size!
	writepos = buffer->writepos; //Load the write position!
	temp.resultd = LE32(data); //Convert us to LE format!
	temp2.resultd = LE32(data2); //Convert us to LE format!
	if (likely(((size | writepos) & 7) == 0)) //Aligned access in aligned buffer?
	{
		*((uint_32*)&buffer->buffer[writepos]) = temp.resultd; //Write 32-bit aligned!
		writepos += 4;
		*((uint_32*)&buffer->buffer[writepos]) = temp2.resultd; //Write 32-bit aligned!
		writepos += 4;
	}
	else //Unaligned read?
	{
		buffer->buffer[writepos++] = temp.byte1; //Write high and update!
		if (unlikely(writepos >= size)) writepos = 0; //Wrap arround when needed!
		buffer->buffer[writepos++] = temp.byte2; //Write high and update!
		if (unlikely(writepos >= size)) writepos = 0; //Wrap arround when needed!
		buffer->buffer[writepos++] = temp.byte3; //Write high and update!
		if (unlikely(writepos >= size)) writepos = 0; //Wrap arround when needed!
		buffer->buffer[writepos++] = temp.byte4; //Write low and update!
		if (unlikely(writepos >= size)) writepos = 0; //Wrap arround when needed!
		buffer->buffer[writepos++] = temp2.byte1; //Write high and update!
		if (unlikely(writepos >= size)) writepos = 0; //Wrap arround when needed!
		buffer->buffer[writepos++] = temp2.byte2; //Write high and update!
		if (unlikely(writepos >= size)) writepos = 0; //Wrap arround when needed!
		buffer->buffer[writepos++] = temp2.byte3; //Write high and update!
		if (unlikely(writepos >= size)) writepos = 0; //Wrap arround when needed!
		buffer->buffer[writepos++] = temp2.byte4; //Write low and update!
	}
	if (unlikely(writepos >= size)) writepos = 0; //Wrap arround when needed!
	buffer->writepos = writepos; //Update the write position!
	buffer->laststatus = LASTSTATUS_WRITE; //Last operation was a write operation!
}

byte writefifobuffer16(FIFOBUFFER *buffer, word data)
{
	if (__HW_DISABLED) return 0; //Abort!
	if (unlikely(buffer==0)) return 0; //Error: invalid buffer!
	if (unlikely(buffer->buffer==0)) return 0; //Error invalid: buffer!
	if (unlikely(allcleared)) return 0; //Error: invalid buffer!

	if (buffer->lock)
	{
		WaitSem(buffer->lock)
		if (fifobuffer_INTERNAL_freesize(buffer)<2) //Buffer full?
		{
			PostSem(buffer->lock)
			return 0; //Error: buffer full!
		}

		writefifobuffer16unlocked(buffer,data); //Write the FIFO buffer without lock!
		PostSem(buffer->lock)
	}
	else
	{
		if (fifobuffer_INTERNAL_freesize(buffer)<2) //Buffer full?
		{
			return 0; //Error: buffer full!
		}

		writefifobuffer16unlocked(buffer, data); //Write the FIFO buffer without lock!
	}
	return 1; //Written!
}

byte writefifobuffer32(FIFOBUFFER *buffer, uint_32 data)
{
	if (__HW_DISABLED) return 0; //Abort!
	if (unlikely(buffer==0)) return 0; //Error: invalid buffer!
	if (unlikely(buffer->buffer==0)) return 0; //Error invalid: buffer!
	if (unlikely(allcleared)) return 0; //Error: invalid buffer!

	if (buffer->lock)
	{
		WaitSem(buffer->lock)
		if (fifobuffer_INTERNAL_freesize(buffer)<4) //Buffer full?
		{
			PostSem(buffer->lock)
			return 0; //Error: buffer full!
		}

		writefifobuffer32unlocked(buffer,data); //Write the FIFO buffer without lock!
		PostSem(buffer->lock)
	}
	else
	{
		if (fifobuffer_INTERNAL_freesize(buffer)<4) //Buffer full?
		{
			return 0; //Error: buffer full!
		}

		writefifobuffer32unlocked(buffer, data); //Write the FIFO buffer without lock!
	}
	return 1; //Written!
}

byte writefifobuffer32_2(FIFOBUFFER *buffer, int_32 data, int_32 data2)
{
	if (__HW_DISABLED) return 0; //Abort!
	if (unlikely(buffer==0)) return 0; //Error: invalid buffer!
	if (unlikely(buffer->buffer==0)) return 0; //Error invalid: buffer!
	if (unlikely(allcleared)) return 0; //Error: invalid buffer!

	if (buffer->lock)
	{
		WaitSem(buffer->lock)
		if (fifobuffer_INTERNAL_freesize(buffer)<8) //Buffer full?
		{
			PostSem(buffer->lock)
			return 0; //Error: buffer full!
		}

		writefifobuffer64unlocked(buffer,signed2unsigned32(data),signed2unsigned32(data2)); //Write the FIFO buffer without lock!
		PostSem(buffer->lock)
	}
	else
	{
		if (fifobuffer_INTERNAL_freesize(buffer)<8) //Buffer full?
		{
			return 0; //Error: buffer full!
		}

		writefifobuffer64unlocked(buffer, signed2unsigned32(data), signed2unsigned32(data2)); //Write the FIFO buffer without lock!
	}
	return 1; //Written!
}

byte writefifobuffer32_2u(FIFOBUFFER *buffer, uint_32 data, uint_32 data2)
{
	if (__HW_DISABLED) return 0; //Abort!
	if (unlikely(buffer==0)) return 0; //Error: invalid buffer!
	if (unlikely(buffer->buffer==0)) return 0; //Error invalid: buffer!
	if (unlikely(allcleared)) return 0; //Error: invalid buffer!

	if (buffer->lock)
	{
		WaitSem(buffer->lock)
		if (fifobuffer_INTERNAL_freesize(buffer)<8) //Buffer full?
		{
			PostSem(buffer->lock)
			return 0; //Error: buffer full!
		}

		writefifobuffer64unlocked(buffer,data, data2); //Write the FIFO buffer without lock!
		PostSem(buffer->lock)
	}
	else
	{
		if (fifobuffer_INTERNAL_freesize(buffer)<8) //Buffer full?
		{
			return 0; //Error: buffer full!
		}

		writefifobuffer64unlocked(buffer, data, data2); //Write the FIFO buffer without lock!
	}
	return 1; //Written!
}

//Wrapper support for floating point as storage!
OPTINLINE int_32 flt2int_32(float data)
{
	#include "headers/packed.h"
	union PACKED
	{
		float fltval;
		int_32 val32;
	} converter;
	#include "headers/endpacked.h" //Finish packed!

	converter.fltval = data;
	return converter.val32; //Apply 32-bit value conversion!
}

OPTINLINE int_32 flt2uint_32(float data)
{
#include "headers/packed.h"
	union PACKED
	{
		float fltval;
		uint_32 val32;
	} converter;
#include "headers/endpacked.h" //Finish packed!

	converter.fltval = data;
	return converter.val32; //Apply 32-bit value conversion!
}

OPTINLINE float int_322flt(int_32 data)
{
	#include "headers/packed.h"
	union PACKED
	{
		float fltval;
		int_32 val32;
	} converter;
	#include "headers/endpacked.h" //Finish packed!

	converter.val32 = data;
	return converter.fltval; //Apply 32-bit value conversion!
}

OPTINLINE float uint_322flt(uint_32 data)
{
#include "headers/packed.h"
	union PACKED
	{
		float fltval;
		uint_32 val32;
	} converter;
#include "headers/endpacked.h" //Finish packed!

	converter.val32 = data;
	return converter.fltval; //Apply 32-bit value conversion!
}

byte writefifobufferflt(FIFOBUFFER *buffer, float data)
{
	return writefifobuffer32(buffer,flt2uint_32(data)); //Write as 32-bit value!
}

byte writefifobufferflt_2(FIFOBUFFER *buffer, float data, float data2)
{
	return writefifobuffer32_2(buffer,flt2int_32(data),flt2int_32(data2)); //Write as 32-bit values!
}

byte readfifobufferflt(FIFOBUFFER *buffer, float *result)
{
	byte tempresult;
	uint_32 temp;
	tempresult = readfifobuffer32(buffer,&temp); //Read into buffer!
	if (tempresult) //Valid to convert and store?
	{
		*result = uint_322flt(temp); //Convert back to floating point!
	}
	return tempresult;
}

byte readfifobufferflt_2(FIFOBUFFER *buffer, float *result, float *result2)
{
	byte tempresult;
	int_32 temp,temp2;
	tempresult = readfifobuffer32_2(buffer,&temp,&temp2); //Read into buffer!
	if (tempresult) //Valid to convert and store?
	{
		*result = int_322flt(temp); //Convert back to floating point!
		*result2 = int_322flt(temp2); //Convert back to floating point!
	}
	return tempresult;
}

byte readfifobufferflt_backtrace(FIFOBUFFER *buffer, float *result, uint_32 backtrace, byte finalbacktrace)
{
	byte tempresult;
	uint_32 temp;
	tempresult = readfifobuffer32_backtrace(buffer,(uint_32 *)&temp,backtrace,finalbacktrace); //Read into buffer!
	if (tempresult) //Valid to convert and store?
	{
		*result = uint_322flt(temp); //Convert back to floating point!
	}
	return tempresult;	
}

byte readfifobufferflt_backtrace_2(FIFOBUFFER *buffer, float *result, float *result2, uint_32 backtrace, byte finalbacktrace)
{
	byte tempresult;
	int_32 temp,temp2;
	tempresult = readfifobuffer32_backtrace_2(buffer,&temp,&temp2,backtrace,finalbacktrace); //Read into buffer!
	if (tempresult) //Valid to convert and store?
	{
		*result = int_322flt(temp); //Convert back to floating point!
		*result2 = int_322flt(temp2); //Convert back to floating point!
	}
	return tempresult;	
}

void fifobuffer_save(FIFOBUFFER *buffer)
{
	if (unlikely(buffer==0)) return; //Error: invalid buffer!
	if (unlikely(buffer->buffer==0)) return; //Error invalid: buffer!
	if (unlikely(allcleared)) return; //Error: invalid buffer!

	if (buffer->lock)
	{
		WaitSem(buffer->lock)
		buffer->savedpos.readpos = buffer->readpos;
		buffer->savedpos.writepos = buffer->writepos;
		buffer->savedpos.laststatus = buffer->laststatus;
		PostSem(buffer->lock)
	}
	else
	{
		buffer->savedpos.readpos = buffer->readpos;
		buffer->savedpos.writepos = buffer->writepos;
		buffer->savedpos.laststatus = buffer->laststatus;
	}
}

void fifobuffer_restore(FIFOBUFFER *buffer)
{
	if (unlikely(buffer==0)) return; //Error: invalid buffer!
	if (unlikely(buffer->buffer==0)) return; //Error invalid: buffer!
	if (unlikely(allcleared)) return; //Error: invalid buffer!

	if (buffer->lock)
	{
		WaitSem(buffer->lock)
		buffer->readpos = buffer->savedpos.readpos;
		buffer->writepos = buffer->savedpos.writepos;
		buffer->laststatus = buffer->savedpos.laststatus;
		PostSem(buffer->lock)
	}
	else
	{
		buffer->readpos = buffer->savedpos.readpos;
		buffer->writepos = buffer->savedpos.writepos;
		buffer->laststatus = buffer->savedpos.laststatus;
	}
}

void fifobuffer_gotolast(FIFOBUFFER *buffer)
{
	if (__HW_DISABLED) return; //Abort!
	if (unlikely(buffer==0)) return; //Error: invalid buffer!
	if (unlikely(buffer->buffer==0)) return; //Error invalid: buffer!
	if (unlikely(allcleared)) return; //Abort: invalid buffer!

	if (buffer->lock)
	{
		WaitSem(buffer->lock)
		if (fifobuffer_INTERNAL_freesize(buffer) == buffer->size)
		{
			PostSem(buffer->lock)
			return; //Empty? We can't: there is nothing to go back to!
		}

		if ((((int_64)buffer->writepos)-1)<0) //Last pos?
		{
			buffer->readpos = buffer->size-1; //Goto end!
		}
		else
		{
			buffer->readpos = buffer->writepos-1; //Last write!
		}
		buffer->laststatus = LASTSTATUS_READ; //We're a read operation last!
		PostSem(buffer->lock)
	}
	else
	{
		if (fifobuffer_INTERNAL_freesize(buffer) == buffer->size)
		{
			return; //Empty? We can't: there is nothing to go back to!
		}

		if ((((int_64)buffer->writepos) - 1)<0) //Last pos?
		{
			buffer->readpos = buffer->size - 1; //Goto end!
		}
		else
		{
			buffer->readpos = buffer->writepos - 1; //Last write!
		}
		buffer->laststatus = LASTSTATUS_READ; //We're a read operation last!
	}
}

void fifobuffer_clear(FIFOBUFFER *buffer)
{
	byte temp; //Saved data to discard!
	if (unlikely(buffer==0)) return; //Error: invalid buffer!

	fifobuffer_gotolast(buffer); //Goto last!
	readfifobuffer(buffer,&temp); //Clean out the last byte if it's there!
}

void movefifobuffer8(FIFOBUFFER *src, FIFOBUFFER *dest, uint_32 threshold)
{
	if (allcleared) return; //Abort: invalid buffer!
	if (unlikely((src == dest) || (!threshold))) return; //Can't move to itself!
	INLINEREGISTER uint_32 current; //Current thresholded data index!
	byte buffer; //our buffer for the transfer!
	if (unlikely(src==0)) return; //Invalid source!
	if (unlikely(dest==0)) return; //Invalid destination!
	if (src->lock) WaitSem(src->lock) //Lock the source!
	if (fifobuffer_INTERNAL_freesize(src) <= (src->size - threshold)) //Buffered enough words of data?
	{
		if (dest->lock) WaitSem(dest->lock) //Lock the destination!
		if (fifobuffer_INTERNAL_freesize(dest) >= threshold) //Enough free space left?
		{
			//Now quickly move the thesholded data from the source to the destination!
			current = threshold; //Move threshold items!
			do //Process all items as fast as possible!
			{
				readfifobufferunlocked(src, &buffer); //Read 8-bit data!
				writefifobufferunlocked(dest, buffer); //Write 8-bit data!
			} while (likely(--current));
		}
		if (dest->lock) PostSem(dest->lock) //Unlock the destination!
	}
	if (src->lock) PostSem(src->lock) //Unlock the source!
}

void movefifobuffer16(FIFOBUFFER *src, FIFOBUFFER *dest, uint_32 threshold)
{
	if (unlikely(allcleared)) return; //Abort: invalid buffer!
	if (unlikely((src==dest) || (!threshold))) return; //Can't move to itself!
	INLINEREGISTER uint_32 current; //Current thresholded data index!
	word buffer; //our buffer for the transfer!
	if (unlikely(src==0)) return; //Invalid source!
	if (unlikely(dest==0)) return; //Invalid destination!
	threshold <<= 1; //Make the threshold word-sized, since we're moving word items!
	if (src->lock) WaitSem(src->lock) //Lock the source!
	if (fifobuffer_INTERNAL_freesize(src) <= (src->size - threshold)) //Buffered enough words of data?
	{
		if (dest->lock) WaitSem(dest->lock) //Lock the destination!
		if (fifobuffer_INTERNAL_freesize(dest) >= threshold) //Enough free space left?
		{
			threshold >>= 1; //Make it into actual data items!
			//Now quickly move the thesholded data from the source to the destination!
			current = threshold; //Move threshold items!
			do //Process all items as fast as possible!
			{
				readfifobuffer16unlocked(src,&buffer,1); //Read 16-bit data!
				writefifobuffer16unlocked(dest,buffer); //Write 16-bit data!
			} while (likely(--current));
		}
		if (dest->lock) PostSem(dest->lock) //Unlock the destination!
	}
	if (src->lock) PostSem(src->lock) //Unlock the source!
}

void movefifobuffer32(FIFOBUFFER *src, FIFOBUFFER *dest, uint_32 threshold)
{
	if (unlikely(allcleared)) return; //Abort: invalid buffer!
	if (unlikely((src==dest) || (!threshold))) return; //Can't move to itself!
	INLINEREGISTER uint_32 current; //Current thresholded data index!
	uint_32 buffer; //our buffer for the transfer!
	if (unlikely(src==0)) return; //Invalid source!
	if (unlikely(dest==0)) return; //Invalid destination!
	threshold <<= 2; //Make the threshold dword-sized, since we're moving dword items!
	if (src->lock) WaitSem(src->lock) //Lock the source!
	if (fifobuffer_INTERNAL_freesize(src) <= (src->size - threshold)) //Buffered enough words of data?
	{
		if (dest->lock) WaitSem(dest->lock) //Lock the destination!
		if (fifobuffer_INTERNAL_freesize(dest) >= threshold) //Enough free space left?
		{
			threshold >>= 2; //Make it into actual data items!
			//Now quickly move the thesholded data from the source to the destination!
			current = threshold; //Move threshold items!
			do //Process all items as fast as possible!
			{
				readfifobuffer32unlocked(src,&buffer,1); //Read 32-bit data!
				writefifobuffer32unlocked(dest,buffer); //Write 32-bit data!
			} while (likely(--current));
		}
		if (dest->lock) PostSem(dest->lock) //Unlock the destination!
	}
	if (src->lock) PostSem(src->lock) //Unlock the source!
}
