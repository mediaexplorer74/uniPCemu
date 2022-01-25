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
#include "headers/hardware/8237A.h" //DMA0 support!
#include "headers/hardware/8253.h" //PIT1 support!
#include "headers/hardware/dram.h" //Our own typedefs!

byte DRAM_DREQ = 0; //Our DREQ signal!
byte DRAM_Pending = 0; //DRAM tick is pending?
extern byte SystemControlPortB; //System control port B!

void DRAM_DMADREQ() //For checking any new DREQ signals of DRAM!
{
	DMA_SetDREQ(0,DRAM_Pending); //Set the current DREQ0: DRAM Refresh!
}

void DRAM_DACK()
{
	//We're to acnowledge the DACK!
	DRAM_Pending = 0; //Lower the DREQ signal now (-DACK0BRD goes low, which lowers DRQ0)!
}

void DRAM_setDREQ(byte output)
{
	if ((output!=DRAM_DREQ) && output) //DREQ raised?
	{
		DRAM_Pending = 1; //Start pending (DRQ0 goes high when output goes high only)!
	}
	DRAM_DREQ = output; //PIT1 is connected to the DREQ signal!
}

void DRAM_access(uint_32 address) //Accessing DRAM?
{
	//Tick the part of RAM affected! Clear RAM on timeout(lower bits are specified)!
}

DRAM_accessHandler doDRAM_access = NULL; //DRAM access?

void initDRAM()
{
	registerPIT1Ticker(&DRAM_setDREQ); //Register our ticker for timing DRAM ticks!
	registerDMATick(0, &DRAM_DMADREQ, &DRAM_DACK, NULL, NULL); //Our handlers for DREQ, DACK and TC of the DRAM refresh! Don't handle DACK and TC!
	DRAM_DREQ = 0; //Init us!
	//doDRAM_access = &DRAM_access; //Access DRAM handler?
}
