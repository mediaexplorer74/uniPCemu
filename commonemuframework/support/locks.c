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

SDL_sem *commonemuframeworklocks[100]; //All allocated locks!
SDL_sem *LockLock; //Our own lock!

void exitLocks(void)
{
	int i;
	for (i = 0; i < (int)NUMITEMS(commonemuframeworklocks); i++)
	{
		if (commonemuframeworklocks[i]) //Gotten a lock?
		{
			SDL_DestroySemaphore(commonemuframeworklocks[i]);
			commonemuframeworklocks[i] = NULL; //Destroy the createn item to make it unusable!
		}
	}
	SDL_DestroySemaphore(LockLock); //Finally: destroy our own lock: we're finished!
}

void initLocks()
{
	static byte toinitialise = 1;
	if (toinitialise) //Not initialised yet?
	{
		memset(&commonemuframeworklocks, 0, sizeof(commonemuframeworklocks)); //Initialise locks!
		atexit(&exitLocks); //Register the lock cleanup function!
		LockLock = SDL_CreateSemaphore(1); //Create our own lock!
		toinitialise = 0; //Initialised!
	}
}

SDL_sem *getLock(byte id)
{
	if (commonemuframeworklocks[id]) //Used lock?
	{
		return commonemuframeworklocks[id]; //Give the lock!
	}
	//Not found? Allocate the lock!
	WaitSem(LockLock)
	commonemuframeworklocks[id] = SDL_CreateSemaphore(1); //Create the lock!
	PostSem(LockLock)
	return commonemuframeworklocks[id]; //Give the createn lock!
}

byte lock(byte id)
{
	SDL_sem *lock;
	lock = getLock(id); //Get the lock!
	if (lock) //Gotten the lock?
	{
		WaitSem(lock); //Wait for it!
		return 1; //OK!
	}
	return 0; //Error!
}

void unlock(byte id)
{
	SDL_sem *lock;
	lock = getLock(id); //Try and get the lock!
	if (lock) //Gotten the lock?
	{
		PostSem(lock)
	}
}