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
#include "headers/emu/threads.h" //Basic threads!
#include "headers/support/log.h" //Logging!
#include "headers/emu/gpu/gpu_text.h" //Text support!
#include "headers/support/locks.h" //Locking support!

#define MAX_THREAD 50
//Maximum ammount of threads:

#define THREADSTATUS_UNUSED 0
#define THREADSTATUS_ALLOCATED 1
#define THREADSTATUS_CREATEN 2
#define THREADSTATUS_RUNNING 4
//Terminated=Unused.
#define THREADSTATUS_TERMINATED 8


//To debug threads?
#define DEBUG_THREADS 0
//Allow running threads/callbacks?
#define CALLBACK_ENABLED 1

//Stuff for easy thread functions.

//Threads don't need stacks, but give it some to be sure!
#define THREAD_STACKSIZE 0x100000

ThreadParams threadpool[MAX_THREAD]; //Thread pool!

//Thread allocation/deallocation!

int getthreadpoolindex(uint_32 thid) //Get index of thread in thread pool!
{
	int i;
	lock(LOCK_THREADS);
	for (i=0;i<(int)NUMITEMS(threadpool);i++) //Process all known indexes!
	{
		if (((threadpool[i].status&(THREADSTATUS_ALLOCATED|THREADSTATUS_CREATEN))==(THREADSTATUS_ALLOCATED|THREADSTATUS_CREATEN)) && (threadpool[i].threadID==thid)) //Used and found?
		{
			//Leave locked for further processing!
			return i; //Give the index!
		}
	}
	unlock(LOCK_THREADS);
	return -1; //Not found!
}

//assumes threads are locked by us
void thread_internal_waitthreadend(ThreadParams_p thread)
{
#if !defined(SDL_VERSION_ATLEAST)
	int dummy;
	SDL_Thread* finishingthread;
	finishingthread = thread->thread; //What to possibly finish?
	//Below code is for versions below 2.0.2!
	unlock(LOCK_THREADS);
	SDL_WaitThread(finishingthread, &dummy); //Wait for the thread to end, ignore the result from the thread!
	lock(LOCK_THREADS);
#else
#if SDL_VERSION_ATLEAST(2,0,2)
	for (; !(thread->status & THREADSTATUS_TERMINATED);) //Wait for termination!
	{
		unlock(LOCK_THREADS);
		//Wait for it to end!
		delay(0);
		lock(LOCK_THREADS);
	}
#else
	int dummy;
	SDL_Thread* finishingthread;
	finishingthread = thread->thread; //What to possibly finish?
	//Below code is for versions below 2.0.2!
	unlock(LOCK_THREADS);
	SDL_WaitThread(finishingthread, &dummy); //Wait for the thread to end, ignore the result from the thread!
	lock(LOCK_THREADS);
#endif
#endif
}

OPTINLINE void thread_cleanup(ThreadParams_p thread)
{
	if (thread) //Valid thread?
	{
		if ((thread->status&(THREADSTATUS_ALLOCATED|THREADSTATUS_CREATEN|THREADSTATUS_RUNNING|THREADSTATUS_TERMINATED))==(THREADSTATUS_ALLOCATED|THREADSTATUS_CREATEN|THREADSTATUS_RUNNING|THREADSTATUS_TERMINATED)) //Terminated?
		{
			if (thread->thread) //Valid thread to deallocate?
			{
				thread_internal_waitthreadend(thread); //Wait for the thread to end, ignore the result from the thread!
			}
			//We're ready to be cleaned up!
			memset(thread, 0, sizeof(*thread)); //Clean up the thread data to become ready for a new thread!
		}
		//Not terminated? Do nothing! Otherwise, it's cleaned up!
	}
}

ThreadParams_p allocateThread(Handler thefunc, char *name, void *params) //Allocate a new thread to run (waits if none to allocate)!
{
	uint_32 curindex;
	lock(LOCK_THREADS);
	for (curindex=0;curindex<NUMITEMS(threadpool);curindex++) //Find an unused entry!
	{
		thread_cleanup(&threadpool[curindex]); //Automatically cleanup the thread that's used!
		if (threadpool[curindex].status) //Used thread?
		{
			if ((threadpool[curindex].callback==thefunc) && (strcmp(threadpool[curindex].name,name)==0) && (threadpool[curindex].params==params)) //This is the same function we're trying to allocate?
			{
				unlock(LOCK_THREADS);
				return NULL; //Abort: double starting identical threads isn't allowed!
			}
		}
		else //Not used?
		{
			threadpool[curindex].status |= THREADSTATUS_ALLOCATED; //We're allocated now!
			//Leave locked for further processing!
			return &threadpool[curindex]; //Give the newly allocated thread params!
			//Failed to allocate, passthrough!
		}
	}
	unlock(LOCK_THREADS);
	return NULL; //Nothing to allocate: ran out of entries!
}

void releasePool(uint_32 threadid) //Release a pooled thread if it exists!
{
	int index;
	if ((index = getthreadpoolindex(threadid))!=-1) //Gotten index?
	{
		threadpool[index].status |= THREADSTATUS_TERMINATED; //We're terminated!
		unlock(LOCK_THREADS);
	}
}

static void releasePoolLocked(ThreadParams_p thread) //Release a pooled thread if it exists!
{
	thread->status |= THREADSTATUS_TERMINATED; //We're terminated!
}

static void activeThread(uint_32 threadid, ThreadParams_p thread)
{
	thread->status |= THREADSTATUS_RUNNING; //Running!
	thread->threadID = threadid; //Our thread ID!
}

void terminateThread(uint_32 thid) //Terminate the thread!
{
	SDL_Thread *thread; //The thread to test!
	thread = NULL;

	int thnr;
	ThreadParams_p p;
	if ((thnr = getthreadpoolindex(thid))!=-1) //Found the thread?
	{
		p = &threadpool[thnr];
		thread = p->thread; //Get the thread!
	}
	else return; //Invalid thread!
	if (thread == NULL) //Can't release no thread!
	{
		releasePoolLocked(p); //Release from pool if available!
		unlock(LOCK_THREADS);
		return; //Abort!
	}
	//Just release!
	releasePoolLocked(p); //Release from pool if available!
	if (thnr!=-1 && thread) //Valid thread to kill?
	{
		thread_internal_waitthreadend(&threadpool[thnr]); //Kill this thread when supported!
		memset(p, 0, sizeof(*p)); //We're already killed!
	}
	unlock(LOCK_THREADS);
}

void deleteThread(uint_32 thid)
{
	terminateThread(thid); //Passthrough!
}

void runcallback(ThreadParams_p thread)
{
	//Now run the requested thread!
	if (CALLBACK_ENABLED) //Gotten params?
	{
		if (thread->callback) //Found the callback?
		{
			unlock(LOCK_THREADS);
			thread->callback(); //Execute the real callback!
			lock(LOCK_THREADS);
		}
	}
}

//Running/stop etc. function for the thread!


int ThreadsRunning() //Are there any threads running or ready to run?
{
	int i;
	int numthreads = 0; //Number of running threads?
	lock(LOCK_THREADS);
	for (i=0;i<(int)NUMITEMS(threadpool);i++) //Check all threads!
	{
		if (threadpool[i].status&THREADSTATUS_ALLOCATED) //Allocated?
		{
			if (threadpool[i].status&THREADSTATUS_CREATEN) //Createn or running?
			{
				++numthreads; //We have some createn or running threads!
			}
		}
	}
	unlock(LOCK_THREADS);
	return numthreads; //How many threads are running?
}

int minthreadsrunning() //Minimum ammount of threads running when nothing's there!
{
	return DEBUG_THREADS; //1 or 0 minimal!
}

/*

threadhandler: The actual thread running over all other threads.

*/

int threadhandler(void *data)
{
	uint_32 thid = SDL_ThreadID(); //The thread ID!
	lock(LOCK_THREADS);
	activeThread(thid,(ThreadParams_p)data); //Mark us as running!
	runcallback((ThreadParams_p)data); //Run the callback!
	releasePoolLocked((ThreadParams_p)data); //Terminate ourselves!
	unlock(LOCK_THREADS);
	return 0; //This cleans up the allocated thread in SDL, finishing when SDL_waitThreadEnd is triggered!
}

void termThreads() //Terminate all threads but our own!
{
	word i=(MAX_THREAD-1);
	uint_32 my_thid = SDL_ThreadID(); //My own thread ID!
	lock(LOCK_THREADS);
	for (;;) //Process all of our threads!
	{
		if (threadpool[i].status && (threadpool[i].threadID!=my_thid)) //Used and not ourselves?
		{
			if (threadpool[i].status&THREADSTATUS_TERMINATED) //Terminated thread?
			{
				//Do nothing!
			}
			else if (threadpool[i].status&THREADSTATUS_RUNNING) //Running?
			{
				unlock(LOCK_THREADS);
				terminateThread(threadpool[i].threadID); //Terminate it!
				lock(LOCK_THREADS);
			}
			else if (threadpool[i].status&THREADSTATUS_CREATEN) //Createn?
			{
				unlock(LOCK_THREADS);
				deleteThread(threadpool[i].threadID); //Delete it!
				lock(LOCK_THREADS);
			}
			else if (threadpool[i].status&THREADSTATUS_ALLOCATED) //Allocated?
			{
				memset(&threadpool[i], 0, sizeof(threadpool[0])); //Cleanup the unused entry!
			}
		}
		thread_cleanup(&threadpool[i]); //Cleanup the thread as required!
		if (i==0) break; //Finished?
		--i; //Next!
	}
	unlock(LOCK_THREADS);
}

extern GPU_TEXTSURFACE *frameratesurface;

void debug_threads()
{
	char thread_name[271];
	cleardata(&thread_name[0],sizeof(thread_name)); //Init!
	while (1)
	{
		int numthreads = 0; //Number of installed threads running!
		int i,i2;
		int totalthreads = ThreadsRunning(); //Ammount of threads running!
		GPU_text_locksurface(frameratesurface);
		lock(LOCK_THREADS);
		for (i=0;i<(int)NUMITEMS(threadpool);i++)
		{
			if (threadpool[i].status&THREADSTATUS_ALLOCATED) //Allocated?
			{
				if (threadpool[i].status&THREADSTATUS_CREATEN) //Created or running?
				{
					++numthreads; //Count ammount of threads!
					GPU_textgotoxy(frameratesurface,0,29-totalthreads+numthreads); //Goto the debug row start!
					snprintf(thread_name,sizeof(thread_name),"Active thread: %s",threadpool[i].name); //Get data string!
					GPU_textprintf(frameratesurface,RGB(0xFF,0x00,0x00),RGB(0x00,0xFF,0x00),thread_name);
					for (i2=(int)safestrlen(thread_name,sizeof(thread_name));i2<=50;i2++)
					{
						GPU_textprintf(frameratesurface,RGB(0xFF,0x00,0x00),RGB(0x00,0xFF,0x00)," "); //Filler to 50 chars!
					} 
				}
			}
		}
		unlock(LOCK_THREADS);
		GPU_textgotoxy(frameratesurface,0,30);
		GPU_textprintf(frameratesurface,RGB(0xFF,0x00,0x00),RGB(0x00,0xFF,0x00),"Number of threads: %u",numthreads); //Debug the ammount of threads used!
		GPU_text_releasesurface(frameratesurface);
		delay(100000); //Wait 100ms!
		lock(LOCK_MAINTHREAD);
		if (shuttingdown()) //Shutting down?
		{
			unlock(LOCK_MAINTHREAD);
			return;
		}
		unlock(LOCK_MAINTHREAD);
	}
}

void onThreadExit(void) //Exit handler for quitting the application! Used for cleanup!
{
	termThreads(); //Terminate all threads!
}

void initThreads() //Initialise&reset thread subsystem!
{
	termThreads(); //Make sure all running threads are stopped!
	atexit(&onThreadExit); //Register out cleanup function!
	memset(&threadpool,0,sizeof(threadpool)); //Clear thread pool!
#ifdef UNIPCEMU
	if (DEBUG_THREADS) startThread(&debug_threads,"UniPCemu_ThreadDebugger",NULL); //Plain debug threads!
#else
	if (DEBUG_THREADS) startThread(&debug_threads,"GBemu_ThreadDebugger",NULL); //Plain debug threads!
#endif
}

void *getthreadparams()
{
	uint_32 threadID = SDL_ThreadID(); //Get the current thread ID!
	int index;
	if ((index = getthreadpoolindex(threadID)) != -1) //Gotten?
	{
		unlock(LOCK_THREADS);
		return threadpool[index].params; //Give the params, if any!
	}
	return NULL; //No params given!
}

void threadCreaten(ThreadParams_p params, uint_32 threadID, char *name)
{
	if (params) //Gotten params?
	{
		params->threadID = threadID; //The thread ID, just in case!
		cleardata(&params->name[0],sizeof(params->name));
		safestrcpy(params->name,sizeof(params->name),name); //Save the name for usage!
	}
}

byte threadRunning(ThreadParams_p thread)
{
	byte result=0;
	if (thread) //OK?
	{
		lock(LOCK_THREADS);
		result = ((thread->status&(THREADSTATUS_CREATEN | THREADSTATUS_RUNNING | THREADSTATUS_TERMINATED))==(THREADSTATUS_CREATEN | THREADSTATUS_RUNNING)); //Createn and running?
		unlock(LOCK_THREADS);
	}
	return result; //Running?
}

ThreadParams_p startThread(Handler thefunc, char *name, void *params) //Start a thread!
{
	if ((!thefunc) || (thefunc==NULL)) //No func?
	{
		raiseError("thread manager","NULL thread: %s",name); //Show the thread as being NULL!
		return NULL; //Don't start: can't start no function!
	}

	//We create our handler in dynamic memory, because we need to keep it in the threadhandler!
	
	//First, allocate a thread position!
	ThreadParams_p threadparams = allocateThread(thefunc,name,params); //Allocate a thread for us, wait for any to come free in the meanwhile!
	if (threadparams==NULL) return NULL; //Not able to allocate the thread: ran out of thread entries!
//Next, start the timer function!
	threadparams->callback = thefunc; //The function to run!
	threadparams->status |= THREADSTATUS_CREATEN; //Createn!
	threadparams->params = params; //The params to save!

	uint_32 thid; //The thread ID!
	docreatethread: //Try to start a thread!
	#ifndef SDL2
	threadparams->thread = SDL_CreateThread(threadhandler,threadparams); //Create the thread!
	#else
	threadparams->thread = SDL_CreateThread(threadhandler,name,threadparams); //Create the thread!
	#endif
	
	if (!threadparams->thread) //Failed to create?
	{
		unlock(LOCK_THREADS);
		delay(0); //Wait a bit!
		lock(LOCK_THREADS);
		goto docreatethread; //Try again!
	}
	thid = SDL_GetThreadID(threadparams->thread); //Get the thread ID!
	threadCreaten(threadparams,thid,name); //We've been createn!
	unlock(LOCK_THREADS);

	#ifdef SDL_VERSION_ATLEAST
	#if SDL_VERSION_ATLEAST(2,0,2)
	SDL_DetachThread(threadparams->thread); //Detach the thread, causing it to clean up on itself!
	#endif
	#endif

	lock(LOCK_THREADS); //Lock the threads!
	for (; (!(threadparams->status & (THREADSTATUS_RUNNING | THREADSTATUS_TERMINATED)));) //Not yet running or terminated?
	{
		if ((threadparams->status & (THREADSTATUS_ALLOCATED | THREADSTATUS_CREATEN)) != (THREADSTATUS_ALLOCATED | THREADSTATUS_CREATEN)) //Deallocated behind our backs?
		{
			break; //Stop waiting for it to start up: we're not ready anymore!
		}
		unlock(LOCK_THREADS); //Wait time!
		delay(0); //Wait a bit!
		lock(LOCK_THREADS); //Wait time finished!
	}
	unlock(LOCK_THREADS); //Ready to process!

	//Now the thread is at least started!

	return threadparams; //Give the thread createn!
}

void waitThreadEnd(ThreadParams_p thread) //Wait for this thread to end!
{
	if (thread) //Valid thread?
	{
		lock(LOCK_THREADS);
		if (thread->status&THREADSTATUS_CREATEN) //Createn or running?
		{
			while ((thread->status&(THREADSTATUS_CREATEN|THREADSTATUS_RUNNING))!=(THREADSTATUS_CREATEN|THREADSTATUS_RUNNING)) //Not running yet?
			{
				unlock(LOCK_THREADS);
				delay(100); //Wait for a bit for the thread to start!
				lock(LOCK_THREADS);
			}
			thread_internal_waitthreadend(thread); //Wait for it to end!
			memset(thread, 0, sizeof(*thread)); //We're finished!
		}
		unlock(LOCK_THREADS);
	}
	//Done with running the thread!
}

OPTINLINE void quitThread() //Quit the current thread!
{
	uint_32 thid = SDL_ThreadID(); //Get the current thread ID!
	terminateThread(thid); //Terminate ourselves!
}

void termThread(){quitThread();} //Alias!
