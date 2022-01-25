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

#ifndef MMUHANDLER_H
#define MMUHANDLER_H

#include "headers/types.h" //Basic types!

typedef struct
{
	uint_32 size; //The total size of memory allocated!
	int_64 maxsize; //Limit when set(-1=no limit)!
	int_64 effectivemaxsize; //Effective maximum size!
	byte *memory; //The memory itself!
	int invaddr; //Invalid adress in memory with MMU_ptr?
	uint_64 wraparround; //To wrap arround memory mask?
	byte enableA20[2];
	byte A20LineEnabled; //Is the line enabled?
	byte A20LineDisabled; //Is the line disabled?
} MMU_type;

/*

w/rhandler:
	offset: The offset to be read (full memory).
	value: The value to read into/from.
result:
	TRUE when successfull. FALSE when failed (continue searching for a viable solution).

*/

typedef byte (*MMU_WHANDLER)(uint_32 offset, byte value);    /* A pointer to a handler function */
typedef byte (*MMU_RHANDLER)(uint_32 offset, byte *value);    /* A pointer to a handler function */

void resetMMU(); //Initialises memory!
void doneMMU(); //Releases memory for closing emulator etc.
uint_32 MEMsize(); //Total size of memory in use?
byte MMU_invaddr(); //Last MMU call has invalid adressing?
void MMU_resetaddr(); //Resets the invaddr for new operations!
byte hasmemory(); //Have memory?

//Memory dumping support
void MMU_dumpmemory(char *filename); //Dump the memory to a file!

//Memory write buffering!
void flushMMU(); //Flush MMU writes!
void bufferMMU(); //Start buffering MMU writes!

//Memory access for fake86 compatibility and testing our CPU.
void MMU_directwb_realaddr(uint_64 realaddress, byte val); //Write without segment/offset translation&protection (from system/interrupt)!
byte MMU_directrb_realaddr(uint_64 realaddress); //Read without segment/offset translation&protection (from system/interrupt)!

//Memory handler support!
void MMU_resetHandlers(char *module); //Initialise/reset handlers, no module (""/NULL) for all.
byte MMU_registerWriteHandler(MMU_WHANDLER handler, char *module); //Register a write handler!
byte MMU_registerReadHandler(MMU_RHANDLER handler, char *module); //Register a read handler!

//DMA memory support!
byte memory_directrb(uint_64 realadress); //Direct read from memory (with real data direct)!
void memory_directwb(uint_64 realadress, byte value); //Direct write to memory (with real data direct)!

//Emulator debugger support!
byte MMU_directrb_hwdebugger(uint_64 realaddress, word index, uint_64* result); //Direct read from real memory (with real data direct)!

//For DMA controller/paging/system: direct word access!
word memory_directrw(uint_64 realadress); //Direct read from real memory (with real data direct)!
void memory_directww(uint_64 realadress, word value); //Direct write to real memory (with real data direct)!

//For paging/system only!
uint_32 memory_directrdw(uint_64 realaddress);
void memory_directwdw(uint_64 realaddress, uint_32 value);
void MMU_updatemaxsize(); //updated the maximum size!
void updateBUShandler(); //Update the bus handler!
void MMU_calcIndexPrecalcs(); //Calculate the index precalcs!
void MMU_RAMlayoutupdated(); //Layout updated!

void MMU_mappingupdated(); //A mapping for a MMU device has been updated?
void MMU_updatedebugger(); //Update the debugger being used or not!
//Define below to enable all memory caching in all MMU units and registered handlers.
#define USE_MEMORY_CACHING

#endif
