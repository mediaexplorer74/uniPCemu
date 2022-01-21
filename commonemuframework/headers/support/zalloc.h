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

#ifndef ZALLOC_H
#define ZALLOC_H

#include "..\commonemuframework\headers\types.h" //"headers/types.h" //Basic types!

void *memprotect(void *ptr, uint_32 size, char *name); //Checks address (with name optionally) of pointer!

typedef void (*DEALLOCFUNC)(void **ptr, uint_32 size, SDL_sem *lock); //Deallocation functionality!

//Functionality for dynamic memory!
void *nzalloc(uint_32 size, char *name, SDL_sem *lock); //Allocates memory, NULL on failure (ran out of memory), protected allocation!
void *zalloc(uint_32 size, char *name, SDL_sem *lock); //Same as nzalloc, but clears the allocated memory!
void freez(void **ptr, uint_32 size, char *name); //Release memory allocated with zalloc!
void freezall(void); //Free all allocated memory still allocated (on shutdown only, garbage collector)!

//Free memory available!
uint_32 freemem(); //Free memory left!

//Debugging functionality for finding memory leaks!
void logpointers(char *cause); //Logs any changes in memory usage!

//For stuff using external allocations. Result: 1 on success, 0 on failure.
//hascanary: 1 for canary, 2 for only canary if registered already, 0 for no canary
byte registerptr(void *ptr,uint_32 size, char *name, DEALLOCFUNC dealloc, SDL_sem *lock, byte hascanary); //Register a pointer!
byte unregisterptr(void *ptr, uint_32 size); //Remove pointer from registration (only if original pointer)?

DEALLOCFUNC getdefaultdealloc(); //The default dealloc function!
byte changedealloc(void *ptr, uint_32 size, DEALLOCFUNC dealloc); //Change the default dealloc func for an entry (used for external overrides)!
#endif