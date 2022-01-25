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

#ifndef PAGING_H
#define PAGING_H

#include "headers/types.h"

#define is_paging() CPU[activeCPU].is_paging
#define mappage effectivemappageHandler
byte CPU_Paging_checkPage(uint_32 address, word readflags, byte CPL); //Do we have paging without error? userlevel=CPL usually.

typedef struct
{
	uint_64 data; //80386 4-way associative TLB results!
	uint_32 TAG; //All TAGs used with the respective TLB!
	uint_32 addrmask; //The mask for the address(reverse of the page size mask(4KB(12 1-bits) or 4MB(22 1-bits)))
	uint_32 addrmaskset; //Same as addrmask, but with the lower 12 bits set.
	uint_32 passthroughmask; //The mask for the address to be passed through(4KB(12 1-bits), 4MB(22 1-bits) or 2MB(21 1-bits))
	void *TLB_listnode; //The list node we're associated to!
	byte isglobal; //Allocated as a global entry?
} TLBEntry;

typedef struct
{
	TLBEntry *entry; //What entry are we?
	byte index; //What index is said entry?
	void *prev, *next; //Previous and next pointers!
	byte allocated; //Are we allocated?
	byte entrynr; //Literal entry number to use!
	uint_32 memoryindex; //The memory index used!
} TLB_ptr;

typedef struct
{
	TLBEntry TLB[64]; //All TLB entries to use!
	TLB_ptr TLB_listnodes[64]; //All nodes for all TLB entries!
	TLB_ptr *TLB_freelist_head[16], *TLB_freelist_tail[16]; //Head and tail of the free list!
	TLB_ptr *TLB_usedlist_head[16], *TLB_usedlist_tail[16]; //Head and tail of the used list!
	byte TLB_usedlist_index[(1024*1024)+(1024*2)];
	TLB_ptr *TLB_usedlist_indexes[256]; //Simple lookup table for the values in the TLB_usedlist_index table.
	byte PAEenabled; //PAE pages are enabled?
} CPU_TLB; //A TLB to use for the CPU!

void Paging_clearTLB(); //Clears the TLB for further fetching!
void Paging_initTLB(); //Initialize the Paging TLB!
void Paging_Invalidate(uint_32 logicaladdress); //Invalidate a single address!
void Paging_TestRegisterWritten(byte TR); //A Test Register has been written to?

//Special: address translation for CPL program debugger.
byte CPU_paging_translateaddr(uint_32 address, byte CPL, uint_64* physaddr); //Do we have paging without error? userlevel=CPL usually.

typedef uint_64(*mappageHandler)(uint_32 address, byte iswrite, byte CPL); //Maps a page to real memory when needed!

#ifndef IS_PAGING
extern mappageHandler effectivemappageHandler; //Paging handler!
#endif

#endif
