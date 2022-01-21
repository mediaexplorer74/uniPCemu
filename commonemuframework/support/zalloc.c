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

#include "headers/types.h" //Basic types!
#include "headers/support/zalloc.h" //Our own definitions!
#include "headers/support/log.h" //Logging support!
#include "headers/support/locks.h" //Locking support!
#include "headers/emu/sound.h" //For locking the audio thread!

#include <malloc.h> //Specific to us only: the actual memory allocation!

//Simple canary!
#define USECANARIES 0
#define PTRCANARY 0xAABBCCDD

byte allcleared = 0; //Are all pointers cleared?
word maxptrnamelen = 0; //Maximum pointer name length detected?
char maxptrnamelenname[18] = ""; //Longest name detected when allocating!

typedef struct
{
void *pointer; //Pointer to the start of the allocated data!
uint_32 size; //Size of the data!
DEALLOCFUNC dealloc; //Deallocation function!
//Extra optimizations:
ptrnum ptrstart, ptrend; //Start&end address of the pointer!
SDL_sem *lock; //The lock applied to this pointer: if it's to be freed and this is set, wait for this lock to be free!
//Extra information
char name[18]; //The name of the allocated entry!
byte hascanary; //Do we have a canary?
} POINTERENTRY;

//4096 + 2048 entries for a small as a memory footprint as possible!
#define NUMPOINTERS 6144

POINTERENTRY registeredpointers[NUMPOINTERS]; //All registered pointers!
POINTERENTRY registeredpointerscopy[NUMPOINTERS]; //All registered pointers for logging!
byte pointersinitialised = 0; //Are the pointers already initialised?
word registeredpointerscount = 0; //How many pointers are registered at all?

//Limit each block allocated to this number when defined! Limit us to 4G for memory!
#define MEM_BLOCK_LIMIT 0xFFFFFFFF

//Debug undefined deallocations?
#define DEBUG_WRONGDEALLOCATION
//Debug allocation and deallocation?
//#define DEBUG_ALLOCDEALLOC

//Pointer registration/unregistration

byte allow_zallocfaillog = 1; //Allow zalloc fail log?

//Initialisation.
OPTINLINE void initZalloc() //Initialises the zalloc subsystem!
{
	if (pointersinitialised) return; //Don't do anything when we're ready already!
	memset(&registeredpointers,0,sizeof(registeredpointers)); //Initialise all registered pointers!
	atexit(&freezall); //Our cleanup function registered!
	pointersinitialised = 1; //We're ready to run!
}

//Log all pointers to zalloc.
void logpointers(char *cause) //Logs any changes in memory usage!
{
	int current;
	uint_32 total_memory = 0; //For checking total memory count!
	initZalloc(); //Make sure we're started!
	memcpy(&registeredpointerscopy, &registeredpointers, sizeof(registeredpointers)); //Create a copy to log!
	dolog("zalloc","Starting dump of allocated pointers (cause: %s)...",cause);
	for (current=0;current<(int)NUMITEMS(registeredpointerscopy);current++)
	{
		if (registeredpointerscopy[current].pointer && registeredpointerscopy[current].size) //Registered?
		{
			if (safestrlen(registeredpointerscopy[current].name,sizeof(registeredpointerscopy[0].name))>0) //Valid name?
			{
				dolog("zalloc","- %s with %u bytes@%p",registeredpointerscopy[current].name,registeredpointerscopy[current].size,registeredpointerscopy[current].pointer); //Add the name!
				total_memory += registeredpointerscopy[current].size; //Add to total memory!
			}
		}
	}
	dolog("zalloc","End dump of allocated pointers.");
	dolog("zalloc","Total memory allocated: %u bytes",total_memory); //We're a full log!
}

//(un)Registration and lookup of pointers.

/*
Matchpointer: matches an pointer to an entry?
parameters:
	ptr: The pointer!
	index: The start index (in bytes)
	size: The size of the data we're going to dereference!
Result:
	-2 when not matched, -1 when matched within another pointer, 0+: the index in the registeredpointers table.
	
*/

OPTINLINE sword matchptr(void *ptr, uint_32 index, uint_32 size, char *name) //Are we already in our list? Give the position!
{
	INLINEREGISTER ptrnum address_start, address_end, currentstart, currentend;
	INLINEREGISTER int left=0;
	initZalloc(); //Make sure we're started!
	if (!ptr) return -2; //Not matched when NULL!
	if (!size) return -2; //Not matched when no size (should be impossible)!
	address_start = (ptrnum)ptr;
	address_start += index; //Start of data!
	address_end = address_start; //Start of data!
	address_end += size; //Add the size!
	--address_end; //End of data!


	for (;left<(int)NUMITEMS(registeredpointers);++left) //Process matchable options!
	{
		currentstart = registeredpointers[left].ptrstart; //Start of addressing!
		if (currentstart==0) continue; //Gotten anything at all?
		currentend = registeredpointers[left].ptrend; //End of addressing!
		if (name)
		{
			if (strcmp(registeredpointers[left].name, name)!=0) continue; //Invalid name? Skip us if so!
		}
		if (currentstart > address_start) continue; //Skip: not our pointer!
		if (currentend < address_end) continue; //Skip: not our pointer!
		//Within range? Give the correct result!
		if (currentstart != address_start) return -1; //Partly match!
		if (currentend != address_end) return -1; //Partly match!
		//We're a full match!
		return (sword)left; //Full match at this index!
	}

	//Compatiblity only!
	return -2; //Not found!
}

byte registerptr(void *ptr,uint_32 size, char *name,DEALLOCFUNC dealloc, SDL_sem *lock, byte hascanary) //Register a pointer!
{
	uint_32 current; //Current!
	ptrnum ptrend;
	initZalloc(); //Make sure we're started!
	if (!ptr)
	{
		#ifdef DEBUG_ALLOCDEALLOC
		if (allow_zallocfaillog) dolog("zalloc","WARNING: RegisterPointer %s with size %u has invalid pointer!",name,size);
		#endif
		return 0; //Not a pointer?
	}
	if (!size)
	{
		#ifdef DEBUG_ALLOCDEALLOC
		if (allow_zallocfaillog) dolog("zalloc","WARNING: RegisterPointer %s with no size!",name,size);
		#endif
		return 0; //Not a size, so can't be a pointer!
	}
	if (matchptr(ptr,0,size,NULL)>-2) return 1; //Already gotten (prevent subs to register after parents)?
	
	for (current=0;current<NUMITEMS(registeredpointers);current++) //Process valid!
	{
		if (!registeredpointers[current].pointer || !registeredpointers[current].size) //Unused?
		{
			if (registeredpointers[current].pointer == ptr) //Same?
			{
				registeredpointers[current].hascanary = (hascanary==2)?registeredpointers[current].hascanary:hascanary; //The deallocation function to call, has a canary, if any to use!
			}
			else
			{
				registeredpointers[current].hascanary = hascanary; //The deallocation function to call, has a canary, if any to use!
			}
			registeredpointers[current].pointer = ptr; //The pointer!
			registeredpointers[current].size = size; //The size!
			registeredpointers[current].dealloc = dealloc; //The deallocation function to call, if any to use!
			cleardata(&registeredpointers[current].name[0],sizeof(registeredpointers[current].name)); //Initialise the name!
			safestrcpy(registeredpointers[current].name,sizeof(registeredpointers[0].name),name); //Set the name!
			if (unlikely(safestrlen(name, 256) > maxptrnamelen)) //Longer name registered than used?
			{
				if (safestrlen(name, 256) > safestrlen(registeredpointers[current].name, sizeof(registeredpointers[current].name))) //Overflow?
				{
					dolog("zalloc", "Warning: Pointer name overflow: %s", name); //Log the name as being too long!
				}
				else //No overflow?
				{
					maxptrnamelen = safestrlen(registeredpointers[current].name, sizeof(registeredpointers[current].name)); //Longest length registered!
					safestrcpy(maxptrnamelenname, sizeof(maxptrnamelenname), name); //Set the name!
				}
			}
			registeredpointers[current].ptrstart = (ptrnum)ptr; //Start of the pointer!
			ptrend = registeredpointers[current].ptrstart;
			ptrend += size; //Add the size!
			--ptrend; //The end of the pointer is before the size!
			registeredpointers[current].ptrend = ptrend; //End address of the pointer for fast checking!
			registeredpointers[current].lock = lock; //Register the lock too!
			#ifdef DEBUG_ALLOCDEALLOC
			if (allow_zallocfaillog) dolog("zalloc","Memory has been allocated. Size: %u. name: %s, location: %p",size,name,ptr); //Log our allocated memory!
			#endif
			++current; //One more for the item count!
			if (unlikely(current > registeredpointerscount))
			{
				registeredpointerscount = current; //How many pointers are actually used!
			}
			return 1; //Registered!
		}
	}
#ifndef _DEBUG
	//Only do this when debugging!
	raiseNonFatalError("zalloc","Registration buffer full@%s@%p!",name,ptr);
#endif
	return 0; //Give error!
}

byte unregisterptr(void *ptr, uint_32 size) //Remove pointer from registration (only if original pointer)?
{
	int index;
	initZalloc(); //Make sure we're started!
	if ((index = matchptr(ptr,0,size,NULL))>-1) //We've been found fully?
	{
		if (registeredpointers[index].pointer==ptr && registeredpointers[index].size==size) //Fully matched (parents only)?
		{
			if (registeredpointers[index].hascanary == 1) //Do we have a canary to check?
			{
				if (unlikely((*((uint_32*)((byte *)registeredpointers[index].pointer + registeredpointers[index].size))) != PTRCANARY)) //Canary failed?
				{
					dolog("zalloc", "Zalloc canary of %s has triggered! Heap overflow detected!",registeredpointers[index].name);
				}
			}
			#ifdef DEBUG_ALLOCDEALLOC
			if (allow_zallocfaillog) dolog("zalloc","Freeing pointer %s with size %u bytes...",registeredpointers[index].name,size); //Show we're freeing this!
			#endif
			memset(&registeredpointers[index],0,sizeof(registeredpointers[index])); //Clear the pointer entry to it's defaults!
			return 1; //Safely unregistered!
		}
	}
	return 0; //We could't find the pointer to unregister!
}

byte changedealloc(void *ptr, uint_32 size, DEALLOCFUNC dealloc) //Change the default dealloc func for an entry (used for external overrides)!
{
	int index;
	initZalloc(); //Make sure we're started!
	if ((index = matchptr(ptr, 0, size, NULL)) > -1) //We've been found fully?
	{
		registeredpointers[index].dealloc = dealloc; //Set the new deallocation function!
		return 1; //Set!
	}
	return 0; //Not found!
}

//Core allocation/deallocation functions.
void zalloc_free(void **ptr, uint_32 size, SDL_sem *lock) //Free a pointer (used internally only) allocated with nzalloc/zalloc!
{
	if (lock)
	{
		WaitSem(lock) //Wait for the lock!
	}
	void *ptrdata = NULL;
	initZalloc(); //Make sure we're started!
	if (ptr) //Valid pointer to our pointer?
	{
		ptrdata = *ptr; //Read the current pointer!
		if (unregisterptr(ptrdata,size)) //Safe unregister, keeping parents alive, use the copy: the original pointer is destroyed by free in Visual C++?!
		{
			free(ptrdata); //Release the valid allocated pointer!
		}
		*ptr = NULL; //Release the pointer given!
	}
	if (lock)
	{
		PostSem(lock) //Release the lock!
	}
}

DEALLOCFUNC getdefaultdealloc()
{
	return &zalloc_free; //Default handler used by us!
}

void *nzalloc(uint_32 size, char *name, SDL_sem *lock) //Allocates memory, NULL on failure (ran out of memory), protected malloc!
{
	void *ptr;
	initZalloc(); //Make sure we're started!
	if (!size) return NULL; //Can't allocate nothing!
	if ((((size_t)size + sizeof(uint_32))) < (size_t)size) return NULL; //Can't allocate past memory boundaries!
	ptr = malloc((size_t)size+sizeof(uint_32)); //Try to allocate once!

	if (ptr!=NULL) //Allocated and a valid size?
	{
		#if USECANARIES==1
		*((uint_32*)(((byte *)ptr)+size)) = PTRCANARY; //Setup the canary!
		#endif
		if (registerptr(ptr,size,name,getdefaultdealloc(),lock,USECANARIES)) //Register the pointer with the detection system, using the default dealloc functionality!
		{
			return ptr; //Give the original pointer, cleared to 0!
		}
		#ifdef DEBUG_ALLOCDEALLOC
		if (allow_zallocfaillog) dolog("zalloc","Ran out of registrations while allocating %u bytes of data for block %s.",size,name);
		#endif
		free(ptr); //Free it, can't generate any more!
	}
	#ifdef DEBUG_ALLOCDEALLOC
	else if (allow_zallocfaillog)
	{
		if (freemem()>=size) //Enough memory after all?
		{
			dolog("zalloc","Error while allocating %u bytes of data for block \"%s\" with enough free memory(%i bytes).",size,name,freemem());
		}
		else
		{
			dolog("zalloc","Ran out of memory while allocating %u bytes of data for block \"%s\".",size,name);
		}
	}
	#endif
	return NULL; //Not allocated!
}

//Deallocation core function.
void freez(void **ptr, uint_32 size, char *name)
{
	int ptrn=-1;
	initZalloc(); //Make sure we're started!
	if (!ptr) return; //Invalid pointer to deref!
	if ((ptrn = matchptr(*ptr,0,size,NULL))>-1) //Found fully existant?
	{
		if (!registeredpointers[ptrn].dealloc) //Deallocation not registered?
		{
			return; //We can't be freed using this function! We're still allocated!
		}
		registeredpointers[ptrn].dealloc(ptr,size,registeredpointers[ptrn].lock); //Release the memory tied to it using the registered deallocation function, if any!
	}
	#ifdef DEBUG_ALLOCDEALLOC
	else if (allow_zallocfaillog && ptr!=NULL) //An pointer pointing to nothing?
	{
		dolog("zalloc","Warning: freeing pointer which isn't an allocated reference: %s=%p",name,*ptr); //Log it!
	}
	#endif
	//Still allocated, we might be a pointer which is a subset, so we can't deallocate!
}

//Allocation support: add initialization to zero.
void *zalloc(uint_32 size, char *name, SDL_sem *lock) //Same as nzalloc, but clears the allocated memory!
{
	void* ptr;
	initZalloc(); //Make sure we're started!
	if (!size) return NULL; //Can't allocate nothing!
	if ((((size_t)size + sizeof(uint_32))) < (size_t)size) return NULL; //Can't allocate past memory boundaries!
	ptr = calloc(1,(size_t)size + sizeof(uint_32)); //Try to allocate once!

	if (ptr != NULL) //Allocated and a valid size?
	{
		#if USECANARIES==1
		* ((uint_32*)(((byte*)ptr) + size)) = PTRCANARY; //Setup the canary!
		#endif
		if (registerptr(ptr, size, name, getdefaultdealloc(), lock, USECANARIES)) //Register the pointer with the detection system, using the default dealloc functionality!
		{
			return ptr; //Give the original pointer, cleared to 0!
		}
		#ifdef DEBUG_ALLOCDEALLOC
		if (allow_zallocfaillog) dolog("zalloc", "Ran out of registrations while allocating %u bytes of data for block %s.", size, name);
		#endif
		free(ptr); //Free it, can't generate any more!
	}
	#ifdef DEBUG_ALLOCDEALLOC
	else if (allow_zallocfaillog)
	{
		if (freemem() >= size) //Enough memory after all?
		{
			dolog("zalloc", "Error while allocating %u bytes of data for block \"%s\" with enough free memory(%i bytes).", size, name, freemem());
		}
		else
		{
			dolog("zalloc", "Ran out of memory while allocating %u bytes of data for block \"%s\".", size, name);
		}
	}
	#endif
	return NULL; //Not allocated!
}

//Deallocation support: release all registered pointers! This used to be unregisterptrall.
void freezall(void) //Free all allocated memory still allocated (on shutdown only, garbage collector)!
{
	int i;
	initZalloc(); //Make sure we're started!
	if (SDL_WasInit(SDL_INIT_AUDIO)) SDL_LockAudio(); //Do make sure the SDL audio callback isn't running!
	lock(LOCK_MAINTHREAD); //Make sure we're not running!
	allcleared = 1; //All is cleared!
	for (i=0;i<(int)NUMITEMS(registeredpointers);i++)
	{
		freez(&registeredpointers[i].pointer,registeredpointers[i].size,"Unregisterptrall"); //Unregister a pointer when allowed!
	}
	if (SDL_WasInit(SDL_INIT_AUDIO)) SDL_UnlockAudio(); //We're done with audio processing!
	unlock(LOCK_MAINTHREAD); //Finished!
}

//Memory protection/verification function. Returns the pointer when valid, NULL on invalid.
void *memprotect(void *ptr, uint_32 size, char *name) //Checks address of pointer!
{
	if ((!ptr) || (ptr==NULL)) //Invalid?
	{
		return NULL; //Invalid!
	}
	if (matchptr(ptr,0,size,name)>-2) //Pointer matched (partly or fully)?
	{
		return ptr; //Give the pointer!
	}
	return NULL; //Invalid!
}

//Detect free memory.
uint_32 freemem() //Largest Free memory block left to allocate!
{
	uint_32 curalloc; //Current allocated memory! This is the result to give!
	char *buffer=NULL;
	uint_64 multiplier; //The multiplier!
	uint_64 lastzalloc = 0;

	curalloc = 0; //Reset at 1 bytes!
	multiplier = (1ULL<<((sizeof(curalloc)*8)-1)); //Start at max multiplier bit!

	for (;;) //While not done...
	{
		lastzalloc = (curalloc|multiplier); //Last zalloc!
		if (((uint_64)(lastzalloc+sizeof(uint_32))<=0xFFFFFFFF) && (((size_t)(lastzalloc+sizeof(uint_32)))>=lastzalloc)) //Within memory range and not wrapping?
		{
			buffer = (char *)malloc((size_t)(lastzalloc+sizeof(uint_32))); //Try allocating, don't have to be cleared!
			if (buffer) //Allocated?
			{
				free(buffer); //Release memory for next try!
				curalloc = (uint_32)lastzalloc; //Set detected memory!
			}
		}
		multiplier >>= 1; //Shift to the next bit position to check!
		if (multiplier==0) //Gotten an allocation and/or done?
		{
			break; //Stop searching: we're done!
		}
		//We're to continue!
	} //We have success!
	
	#ifdef MEM_BLOCK_LIMIT
		if (curalloc > MEM_BLOCK_LIMIT) //More than the limit?
		{
			curalloc = MEM_BLOCK_LIMIT; //Limit to this much!
		}
	#endif
	return curalloc; //Give free allocatable memory size!
}
