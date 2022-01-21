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

#ifndef THREADS_H
#define THREADS_H

#include "headers/types.h" //Basic type support!
#ifdef VISUALC
#include <SDL_thread.h> //Multithreading support!
#else
#ifdef ANDROID
#include "SDL_thread.h" //Multithreading support!
#else
#if defined(__MINGW64__) || defined(__MINGW32__)
#include <SDL_thread.h> //Multithreading support!
#else
#ifdef IS_LINUX
#include <SDL_thread.h> //Multithreading support!
#else
#if defined(IS_VITA)
#include <SDL2/SDL_thread.h> //Multithreading support!
#else
#if defined(IS_SWITCH)
#ifdef SDL2
#include <SDL2/SDL_thread.h> //Multithreading support!
#else
#include <SDL/SDL_thread.h> //Multithreading support!
#endif
#else
#include <SDL_thread.h> //Multithreading support!
#endif
#endif
#endif
#endif
#endif
#endif

typedef struct
{
Handler callback; //The callback to use!
byte status; //Used thread entries status: 0=Allocated, 1=Created, 2=Running! All else is invalid: regard as NULL record, only allocated!
char name[256]; //Names of the threads (just for debugging)
SDL_Thread *thread; //The specified thread in SDL!
uint_32 threadID; //The specified thread ID!
void *params; //Our given param(s)!
} ThreadParams, *ThreadParams_p; //The thread's params!

void initThreads(); //Initialise&reset thread subsystem!
ThreadParams_p startThread(Handler thefunc, char *name, void *params); //Start a thread, gives the thread info if successfull!
byte threadRunning(ThreadParams_p thread); //Is this thread running?
void waitThreadEnd(ThreadParams_p thread); //Wait for this thread to end!
void termThread(); //Alias of quitThread!
void termThreads(); //Terminate all threads but our own (active thread)!
int ThreadsRunning(); //Are there any threads running or ready to run?
int minthreadsrunning(); //Minimum ammount of threads running when nothing's there!
void *getthreadparams(); //The params if set!
#endif