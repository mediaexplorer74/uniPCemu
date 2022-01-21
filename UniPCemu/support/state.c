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

#include "headers/emu/gpu/gpu.h" //GPU support!
#include "headers/emu/state.h" //Our own data and support etc.
#include "headers/support/crc32.h" //CRC32 support!

extern GPU_type GPU; //GPU!
extern Handler CBHandlers[CB_MAX]; //Handlers!

SAVED_CPU_STATE_HEADER SaveStatus_Header; //SaveStatus structure!


//Version of save state!
#define SAVESTATE_MAIN_VER 1
#define SAVESTATE_SUB_VER 0

void EMU_SaveStatus(char *filename) //Save the status to file or memory
{
	return; //Not working ATM!
}

int EMU_LoadStatus(char *filename) //Load the status from file or memory (TRUE for success, FALSE for error)
{
	return FALSE; //Cannot load: not compatible yet!
}