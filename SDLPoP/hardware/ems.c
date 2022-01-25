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

#include "headers/types.h" //Basic types!
#include "headers/mmu/mmuhandler.h" //MMU support!
#include "headers/hardware/ports.h" //I/O port support!
#include "headers/hardware/ems.h" //EMS support prototypes!
#include "headers/support/zalloc.h" //Memory allocation support!
#include "headers/support/locks.h" //Locking support!
#include "headers/cpu/cpu.h" //CPU count support!

word EMS_baseport = 0x260; //Base I/O port!
uint_32 EMS_baseaddr = 0xE0000; //Base address!

byte *EMS = NULL; //EMS memory itself!
uint_32 EMS_size = 0; //Size of EMS memory!

byte EMS_pages[4] = { 0,0,0,0 }; //What pages are mapped?

byte readEMSMem(uint_32 address, byte *value)
{
	byte block;
	uint_32 memoryaddress;
	if (address < EMS_baseaddr) return 0; //No EMS!
	address -= EMS_baseaddr; //Get the EMS address!
	if (address >= 0x10000) return 0; //No EMS!
	block = (address >> 14); //What block are we?
	address &= 0x3FFF; //What address witin the page?
	memoryaddress = (EMS_pages[block] << 14); //Block in memory!
	memoryaddress |= address; //The address of the byte in memory!
	if (memoryaddress >= EMS_size) return 0; //Out of range?
	*value = EMS[memoryaddress]; //Give the byte from memory!
	return 1; //We're mapped!
}

extern uint_64 BIU_cachedmemoryaddr[MAXCPUS][2];
extern uint_64 BIU_cachedmemoryread[MAXCPUS];
extern byte BIU_cachedmemorysize[MAXCPUS][2]; //To invalidate the BIU cache!
extern byte memory_datasize[2]; //The size of the data that has been read!
byte writeEMSMem(uint_32 address, byte value)
{
	byte block;
	uint_32 memoryaddress,originaladdress;
	originaladdress = address; //Backup for comparing with the BIU cache!
	if (address < EMS_baseaddr) return 0; //No EMS!
	address -= EMS_baseaddr; //Get the EMS address!
	if (address >= 0x10000) return 0; //No EMS!
	block = (address >> 14); //What block are we?
	address &= 0x3FFF; //What address witin the page?
	memoryaddress = (EMS_pages[block] << 14); //Block in memory!
	memoryaddress |= address; //The address of the byte in memory!
	if (memoryaddress >= EMS_size) return 0; //Out of range?
	EMS[memoryaddress] = value; //Set the byte in memory!
	if (unlikely(BIU_cachedmemorysize[0][0] && (BIU_cachedmemoryaddr[0][0] <= originaladdress) && ((BIU_cachedmemoryaddr[0][0] + BIU_cachedmemorysize[0][0]) > originaladdress))) //Matched an active read cache(allowing self-modifying code)?
	{
		memory_datasize[0] = 0; //Invalidate the read cache to re-read memory!
		BIU_cachedmemorysize[0][0] = 0; //Invalidate the BIU cache as well!
	}
	if (unlikely(BIU_cachedmemorysize[1][0] && (BIU_cachedmemoryaddr[1][0] <= originaladdress) && ((BIU_cachedmemoryaddr[1][0] + BIU_cachedmemorysize[1][0]) > originaladdress))) //Matched an active read cache(allowing self-modifying code)?
	{
		memory_datasize[0] = 0; //Invalidate the read cache to re-read memory!
		BIU_cachedmemorysize[1][0] = 0; //Invalidate the BIU cache as well!
	}
	if (unlikely(BIU_cachedmemorysize[0][1] && (BIU_cachedmemoryaddr[0][1] <= originaladdress) && ((BIU_cachedmemoryaddr[0][1] + BIU_cachedmemorysize[0][1]) > originaladdress))) //Matched an active read cache(allowing self-modifying code)?
	{
		memory_datasize[1] = 0; //Invalidate the read cache to re-read memory!
		BIU_cachedmemorysize[0][1] = 0; //Invalidate the BIU cache as well!
	}
	if (unlikely(BIU_cachedmemorysize[1][1] && (BIU_cachedmemoryaddr[1][1] <= originaladdress) && ((BIU_cachedmemoryaddr[1][1] + BIU_cachedmemorysize[1][1]) > originaladdress))) //Matched an active read cache(allowing self-modifying code)?
	{
		memory_datasize[1] = 0; //Invalidate the read cache to re-read memory!
		BIU_cachedmemorysize[1][1] = 0; //Invalidate the BIU cache as well!
	}
	return 1; //We're mapped!
}

byte readEMSIO(word port, byte *value)
{
	if (port<EMS_baseport) return 0; //No EMS!
	port -= EMS_baseport; //Get the EMS port!
	if (port>=NUMITEMS(EMS_pages)) return 0; //No EMS!
	*value = EMS_pages[port]; //Get the page!
	return 1; //Give the value!
}

byte writeEMSIO(word port, byte value)
{
	if (port<EMS_baseport) return 0; //No EMS!
	port -= EMS_baseport; //Get the EMS port!
	if (port >= NUMITEMS(EMS_pages)) return 0; //No EMS!
	EMS_pages[port] = value; //Set the page!
	return 1; //Give the value!
}

void initEMS(uint_32 memorysize, byte allocmemory_initIO)
{
	if (allocmemory_initIO==0) //Don't allocate memory? We're a normal startup!
	{
		doneEMS(); //Make sure we're cleaned up first!
		EMS = (byte*)zalloc(memorysize, "EMS", getLock(LOCK_CPU));
		EMS_size = memorysize; //We're allocated for this much!
	}
	else if (EMS && EMS_size) //Initialize I/O now?
	{
		register_PORTIN(&readEMSIO);
		register_PORTOUT(&writeEMSIO);
		MMU_registerWriteHandler(&writeEMSMem, "EMS");
		MMU_registerReadHandler(&readEMSMem, "EMS");
		memset(&EMS_pages, 0, sizeof(EMS_pages)); //Initialise EMS pages to first page!
	}
}

void doneEMS()
{
	freez((void **)&EMS, EMS_size, "EMS"); //Free our memory!
	EMS_size = 0; //No size anymore!
}
