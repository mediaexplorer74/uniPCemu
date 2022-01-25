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

#ifndef STATE_H
#define STATE_H

#include "headers/types.h" //Basic types!
#include "headers/cpu/cb_manager.h" //Callbacks!

typedef struct
{
	uint_32 checksum; //Checksum of below for error checking!
	union
	{
		struct
		{
			uint_32 CPU; //Saved-state CPU!
			uint_32 GPU; //Saved-state GPU!
			uint_32 VGA; //Saved-state VGA (GPU subsystem)
			uint_32 MMU_size; //MMU size!
			Handler CBHandlers[CB_MAX]; //Handlers!
		}; //Contents!
		byte data[sizeof(uint_32)*4+(sizeof(Handler)*CB_MAX)]; //Data!
	}; //Contains data!
} SAVED_CPU_STATE_HEADER; //Saved/loaded status of CPU/MMU/etc. information!

//Finally: functions for loading and saving!

void EMU_SaveStatus(char *filename); //Save the status to file or memory
int EMU_LoadStatus(char *filename); //Load the status from file or memory (TRUE for success, FALSE for error)

#endif