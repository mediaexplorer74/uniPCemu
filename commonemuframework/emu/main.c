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

#include "headers/types.h" //For global stuff etc!
#include "exception/exception.h" //For the exception handler!
#include "headers/emu/threads.h" //Threads!
#include "headers/emu/gpu/gpu_sdl.h" //SDL support!
#include "headers/support/zalloc.h" //For final freezall functionality!
#include "headers/emu/sound.h" //Sound support!

//Various stuff we use!
#include "headers/emu/emu_main.h" //Emulation core is required to run the CPU!
#include "headers/emu/emu_misc.h" //Misc. stuff!

#include "headers/support/zalloc.h" //Zero allocation support!
#include "headers/support/log.h" //Logging support!

#include "headers/emu/emucore.h" //Emulator core support for checking for memory leaks!
#include "headers/emu/timers.h" //Timer support!

#include "headers/fopen64.h" //emufopen64 support!

#include "headers/support/locks.h" //Lock support!

#include "headers/support/highrestimer.h" //High resolution timer support!

#include "headers/support/tcphelper.h" //TCP support!
#ifdef UNIPCEMU
#include "headers/mmu/mmuhandler.h" //hasmemory support!
#include "headers/hardware/modem.h" //Packet support
#endif

#include "gitcommitversion.h" //Version support!

#if defined(IS_PSP) || defined(IS_VITA)
#ifdef IS_PSP
//Module info from SDL_main
PSP_MODULE_INFO("SDL App", 0, 1, 1);
//Our own defines!
#include <psppower.h> //PSP power support for clock speed!
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER); //Make sure we're user mode!
PSP_HEAP_SIZE_KB(-1024); //Free maximum for us: need this for the memory allocation (m/zalloc)!
#else
int _newlib_heap_size_user = 192 * 1024 * 1024; 
#endif
#endif

#ifdef NDK_PROFILE
extern void monstartup(char const *);
extern void moncleanup();
#endif

//Debug zalloc allocations?
//#define DEBUG_ZALLOC

//Delete all logs on boot?
#define DELETE_LOGS_ONBOOT 1
//Delete all bitmaps captured on boot?
#define DELETE_BMP_ONBOOT 0

//Find emulator memory leaks during allocation/deallocation when defined?
//#define FIND_EMU_MEMORY_LEAKS

extern byte active_screen; //Active screen: 0=bottom, 1=Top, 2=Left/Right, 3=Right/Left!

//To debug VRAM writes?
#define DEBUG_VRAM_WRITES 0

//Automatically sleep on main thread close?
#define SLEEP_ON_MAIN_CLOSE 0

extern DOUBLE last_timing, timeemulated; //Our timing variables!
extern TicksHolder CPU_timing; //Our timing holder!
extern byte haswindowactive; //For detecting paused operation!
extern byte backgroundpolicy; //Background task policy. 0=Full halt of the application, 1=Keep running without video and audio muted, 2=Keep running with audio playback, recording muted, 3=Keep running fully without video.


byte EMU_IsShuttingDown = 0; //Shut down (default: NO)?

void EMU_Shutdown(byte execshutdown)
{
	lock(LOCK_SHUTDOWN); //Lock the CPU: we're running!
	EMU_IsShuttingDown = execshutdown; //Call shutdown or not!
	unlock(LOCK_SHUTDOWN); //Finished with the CPU: we're running!
}

byte shuttingdown() //Shutting down?
{
	byte result;
	lock(LOCK_SHUTDOWN); //Lock the CPU: we're running!
	if (EMU_IsShuttingDown)
	{
		result = EMU_IsShuttingDown; //Result!
		unlock(LOCK_SHUTDOWN); //Finished with the CPU: we're running!
		return result; //Shutting down!
	}
	unlock(LOCK_SHUTDOWN); //Finished with the CPU: we're running!
	return 0; //Not shutting down (anymore)!
}

/*

BASIC Exit Callbacks

*/

#ifdef IS_PSP
/* Exit callback */
int exit_callback(int arg1, int arg2, void *common)
{
	EMU_Shutdown(1); //Call for a shut down!
	uint_64 counter = 0; //Counter for timeout!
	for(;;) //Loop!
	{
		if ((counter>=SHUTDOWN_TIMEOUT) && (SHUTDOWN_TIMEOUT)) //Waited more than timeout's seconds (emulator might not be responding?)?
		{
			break; //Timeout! Force shut down!
		}
		if (shuttingdown()==2) //Actually shut down?
		{
			return 0; //We've shut down!
		}
		else
		{
			delay(100000); //Wait a bit!
			counter += 100000; //For timeout!
		}
	}

	lock(LOCK_MAINTHREAD);
	termThreads(); //Terminate all threads now!	
	freezall(); //Release all still allocated data when possible!

	quitemu(0); //The emu has shut down!
	unlock(LOCK_MAINTHREAD);
	return 0; //Never arriving here!
}

/* Callback thread */
int CallbackThread(SceSize args, void *argp)
{
	int cbid;

	cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
	sceKernelRegisterExitCallback(cbid);

	sceKernelSleepThreadCB();

	return 0;
}
#endif

/* Sets up the callback thread and returns its thread id */
int SetupCallbacks()
{
#ifdef IS_PSP
	//Do the same SDL_main does on the PSP!
	/* Register sceKernelExitGame() to be called when we exit */
	atexit(sceKernelExitGame); 
#endif

	atexit(&SDL_Quit); //Basic SDL safety!
#ifdef IS_PSP
	//We're using our own handler for terminating on the PSP, so we override the SDL_main functionality manually
	int thid = 0;

	pspDebugScreenInit(); //Make sure this is initialized, like SDL 1.2.15 does!

#ifdef UNIPCEMU
	thid = sceKernelCreateThread("UniPCemu_ExitThread", CallbackThread, EXIT_PRIORITY, 0xFA0, 0, 0); //Create thread at highest priority!
#else
	thid = sceKernelCreateThread("GBemu_ExitThread", CallbackThread, EXIT_PRIORITY, 0xFA0, 0, 0); //Create thread at highest priority!
#endif
	if(thid >= 0)
	{
		sceKernelStartThread(thid, 0, 0);
	}

	return thid;
#endif
	return 0;
}

/*

Main emulation routine

*/

extern byte use_profiler; //To determine if the profiler is used!

DOUBLE clockspeed; //Current clock speed, for affecting timers!

OPTINLINE DOUBLE getCurrentClockSpeed()
{
	#ifdef IS_PSP
		return scePowerGetCpuClockFrequencyFloat(); //Current clock speed!
	#else
		return 222.0f; //Not used yet, just assume 222Hz!
	#endif
}

extern byte EMU_RUNNING; //Are we running?

TicksHolder CPUUpdate;

uint_64 CPU_time = 0; //Total CPU time before delay!

byte allowInput = 1; //Do we allow input logging?

void updateInputMain() //Frequency 1000Hz!
{
	SDL_Event event;
	lock(LOCK_ALLOWINPUT);
	if (unlikely(allowInput == 0)) //Keep running?
	{
		unlock(LOCK_ALLOWINPUT);
		return; //Block the event check!
	}
	unlock(LOCK_ALLOWINPUT);

	if (SDL_PollEvent(&event)) //Gotten an event to process?
	{
		preUpdateInput(); //Prepare for parsing input!
		do //Gotten events to handle?
		{
			//Handle an event!
			updateInput(&event); //Update input status when needed!
		}
		while (SDL_PollEvent(&event)); //Keep polling while available!
		postUpdateInput(); //Finish parsing input!
	}
}

#ifdef IS_WINDOWS
//Based on https://msdn.microsoft.com/en-us/library/aa380798(v=vs.85).aspx
#define TERMINAL_SERVER_KEY "SYSTEM\\CurrentControlSet\\Control\\Terminal Server\\"
#define GLASS_SESSION_ID    "GlassSessionId"

byte IsCurrentSessionRemoteable()
{
    byte fIsRemoteable = FALSE;
                                       
    if (GetSystemMetrics(SM_REMOTESESSION)) 
    {
        fIsRemoteable = TRUE;
    }
    else
    {
        HKEY hRegKey = NULL;
        LONG lResult;

        lResult = RegOpenKeyEx(
            HKEY_LOCAL_MACHINE,
            TERMINAL_SERVER_KEY,
            0, // ulOptions
            KEY_READ,
            &hRegKey
            );

        if (lResult == ERROR_SUCCESS)
        {
            DWORD dwGlassSessionId;
            DWORD cbGlassSessionId = sizeof(dwGlassSessionId);
            DWORD dwType;

            lResult = RegQueryValueEx(
                hRegKey,
                GLASS_SESSION_ID,
                NULL, // lpReserved
                &dwType,
                (byte*) &dwGlassSessionId,
                &cbGlassSessionId
                );

            if (lResult == ERROR_SUCCESS)
            {
                DWORD dwCurrentSessionId;

                if (ProcessIdToSessionId(GetCurrentProcessId(), &dwCurrentSessionId))
                {
                    fIsRemoteable = (dwCurrentSessionId != dwGlassSessionId);
                }
            }
        }

        if (hRegKey)
        {
            RegCloseKey(hRegKey);
        }
    }

    return (fIsRemoteable?1:0);
}
#endif

extern byte allcleared;
extern char logpath[256]; //Log path!
extern char capturepath[256]; //Capture path!
extern byte RDP;
extern byte RDPDelta;

#ifdef NDK_PROFILE
byte is_monpendingcleanup = 0; //Are we still pending cleanup?
void monpendingcleanup()
{
	lock(LOCK_PERFMON); //Lock us!
	if (is_monpendingcleanup) //Pending cleanup?
	{
		is_monpendingcleanup = 0; //Not pending anymore!
		moncleanup();
	}
	unlock(LOCK_PERFMON); //Release us! We're finished!
}
extern char UniPCEmu_root_dir[256]; //Our root directory to use!
#endif

byte dumpBIOS = 0;
extern byte logdebuggertoprintf;
extern byte verifydebuggerfrominput;
extern byte textureUpdateRequired; //Texture update required?
extern byte usefullscreenwindow; //Fullscreen Window?

extern byte alwaysdetectjoysticks; //Always detect joysticks?

#ifdef GBEMU
extern int ROMLOADED; //No rom loaded by default!
#endif

#ifdef UNIPCEMU
extern byte emu_log_qemu; //Logging qemu style enabled?
#endif

int main(int argc, char * argv[])
{
	int argn;
	char *argch;
	char *testparam;
	char nosoundparam[] = "nosound";
	char RDPparam[] = "rdp";
	char dumpBIOSparam[] = "dumpbios";
	char noSleepparam[] = "nosleep";
	char detectjoysticksrequirefocus[] = "multiplayerjoysticks";
	#ifdef SDL2
	char fullscreenwindow[] = "fullscreenwindow";
	#endif
	#ifdef UNIPCEMU
	char debuggertoprintfparam[] = "debuggerout";
	char verifydebuggerfrominputparam[] = "debuggerin";
	char logqemuparam[] = "debuggerqemu";
	#endif
	#if defined(IS_LINUX) && !defined(ANDROID)
	char versionparam[] = "--version"; //Linux only!
	#endif
	byte usesoundmode = 1;
	uint_32 SDLsubsystemflags = 0; //Our default SDL subsystem flags of used functionality!
	int emu_status;
	#ifdef IS_WINDOWS
	byte nosleep = 0; //Disable sleeping?
	byte useRDP = 0, oldRDP = 0;
	#endif
	usefullscreenwindow = 0; //Default: normal window!
	logdebuggertoprintf = 0; //Default: don't debug to printf!

	#ifdef NDK_PROFILE
	setenv( "CPUPROFILE_FREQUENCY", "500", 1 ); // interrupts per second, default 100
	monstartup("libmain.so");
	is_monpendingcleanup = 1; //We're running to allow pending cleanup!
	#endif

	//Basic PSP stuff and base I/O callback(lowest priority) for terminating the application using SDL!
	SetupCallbacks();

	#ifdef NDK_PROFILE
	atexit(&monpendingcleanup); //Cleanup function! We have lower priority than the callbacks(which includes SDL_Quit to terminate the application, which would prevent us from cleaning up properly.
	#endif


	#ifdef IS_PSP
		scePowerSetClockFrequency(333, 333, 166); //Start high-speed CPU!
	#endif
	clockspeed = getCurrentClockSpeed(); //Save the current clock frequency for reference!

	#ifdef IS_PSP
	if (FILE_EXISTS("exception.prx")) //Enable exceptions?
	{
		initExceptionHandler(); //Start the exception handler!
	}
	#endif

	if (SDL_Init(0) < 0) //Error initialising SDL defaults?
	{
		exit(1); //Just to be sure
	}

	initTCP(); //Initialize TCP support!

	#ifdef IS_WINDOWS
	useRDP = RDP = oldRDP = 0; //Default: normal!
	#endif

	if (argc) //Gotten parameters?
	{
		for (argn=0;argn<argc;++argn) //Process all arguments!
		{
			//First param option!
			argch = &argv[argn][0]; //First character of the parameter!
			if (*argch) //Specified?
			{
				argch = &argv[argn][0]; //First character of the parameter!
				testparam = &nosoundparam[0]; //Our parameter to check for!
				for (;*argch!='\0';) //Parse the string!
				{
					if ((char)tolower((int)*argch)!=*testparam) //Not matched?
					{
						goto nomatch;
					}
					if (*testparam=='\0') //No match? We're too long!
					{
						goto nomatch;
					}
					++argch;
					++testparam;
				}
				nomatch:
				if ((*argch==*testparam) && (*argch=='\0')) //End of string? Full match!
				{
					usesoundmode = 0; //Disable audio: we're disabled by the parameter!
				}

				//Second param option!
				argch = &argv[argn][0]; //First character of the parameter!
				testparam = &RDPparam[0]; //Our parameter to check for!
				for (;*argch!='\0';) //Parse the string!
				{
					if ((char)tolower((int)*argch)!=*testparam) //Not matched?
					{
						goto nomatch2;
					}
					if (*testparam=='\0') //No match? We're too long!
					{
						goto nomatch2;
					}
					++argch;
					++testparam;
				}
				nomatch2:
				if ((*argch==*testparam) && (*argch=='\0')) //End of string? Full match!
				{
					#ifdef IS_WINDOWS
					useRDP = oldRDP = 1; //Enable RDP: we're enabled by the parameter!
					#endif
				}

				//Third param option!
				argch = &argv[argn][0]; //First character of the parameter!
				testparam = &dumpBIOSparam[0]; //Our parameter to check for!
				for (;*argch!='\0';) //Parse the string!
				{
					if ((char)tolower((int)*argch)!=*testparam) //Not matched?
					{
						goto nomatch3;
					}
					if (*testparam=='\0') //No match? We're too long!
					{
						goto nomatch3;
					}
					++argch;
					++testparam;
				}
				nomatch3:
				if ((*argch==*testparam) && (*argch=='\0')) //End of string? Full match!
				{
					dumpBIOS = 1; //Enable RDP: we're enabled by the parameter!
				}

				//Fourth param option!
				alwaysdetectjoysticks = 1; //Default: always detect joysticks(compatiblity)!
				argch = &argv[argn][0]; //First character of the parameter!
				testparam = &detectjoysticksrequirefocus[0]; //Our parameter to check for!
				for (; *argch != '\0';) //Parse the string!
				{
					if ((char)tolower((int)*argch) != *testparam) //Not matched?
					{
						goto nomatch4;
					}
					if (*testparam == '\0') //No match? We're too long!
					{
						goto nomatch4;
					}
					++argch;
					++testparam;
				}
				nomatch4:
				if ((*argch == *testparam) && (*argch == '\0')) //End of string? Full match!
				{
					alwaysdetectjoysticks = 0; //Only detect joysticks when having focus!
				}

				//Fourth param option
				argch = &argv[argn][0]; //First character of the parameter!
				testparam = &noSleepparam[0]; //Our parameter to check for!
				for (; *argch != '\0';) //Parse the string!
				{
					if ((char)tolower((int)*argch) != *testparam) //Not matched?
					{
						goto nomatch5;
					}
					if (*testparam == '\0') //No match? We're too long!
					{
						goto nomatch5;
					}
					++argch;
					++testparam;
				}
				nomatch5:
				if ((*argch == *testparam) && (*argch == '\0')) //End of string? Full match!
				{
					#ifdef IS_WINDOWS
					nosleep = 1; //Enable noSleep: we're enabled by the parameter!
					#endif
				}

				#ifdef SDL2
				//SDL2 param option
				argch = &argv[argn][0]; //First character of the parameter!
				testparam = &fullscreenwindow[0]; //Our parameter to check for!
				for (; *argch != '\0';) //Parse the string!
				{
					if ((char)tolower((int)*argch) != *testparam) //Not matched?
					{
						goto nomatchfsw;
					}
					if (*testparam == '\0') //No match? We're too long!
					{
						goto nomatchfsw;
					}
					++argch;
					++testparam;
				}
				nomatchfsw:
				if ((*argch == *testparam) && (*argch == '\0')) //End of string? Full match!
				{
					usefullscreenwindow = 1; //Enable noSleep: we're enabled by the parameter!
				}
				#endif


				#if defined(IS_LINUX) && !defined(ANDROID)
				argch = &argv[argn][0]; //First character of the parameter!
				testparam = &versionparam[0]; //Our parameter to check for!
				for (;*argch!='\0';) //Parse the string!
				{
					if ((char)tolower((int)*argch)!=*testparam) //Not matched?
					{
						goto nomatch6;
					}
					if (*testparam=='\0') //No match? We're too long!
					{
						goto nomatch6;
					}
					++argch;
					++testparam;
				}
				nomatch6:
				if ((*argch==*testparam) && (*argch=='\0')) //End of string? Full match!
				{
					#ifdef GITVERSION
					#ifdef UNIPCEMU
					printf("UniPCemu build %s\n",GITVERSION); //Show the version information!
					#else
					#ifdef GBEMU
					printf("GBemu build %s\n",GITVERSION); //Show the version information!
					#endif
					#endif
					#else
					//No version information available?
					#ifdef UNIPCEMU
					printf("UniPCemu built without version information!\n");
					#else
					#ifdef GBEMU
					printf("GBemu built without version information!\n");
					#endif
					#endif
					#endif
					exit(0); //Terminate us!
					return 0; //Quit!
				}
				#endif

				#ifdef UNIPCEMU
				logdebuggertoprintf = 0;
				argch = &argv[argn][0]; //First character of the parameter!
				testparam = &debuggertoprintfparam[0]; //Our parameter to check for!
				for (;*argch!='\0';) //Parse the string!
				{
					if ((char)tolower((int)*argch)!=*testparam) //Not matched?
					{
						goto nomatch7;
					}
					if (*testparam=='\0') //No match? We're too long!
					{
						goto nomatch7;
					}
					++argch;
					++testparam;
				}
				nomatch7:
				if ((*argch == *testparam) && (*argch == '\0')) //End of string? Full match!
				{
					logdebuggertoprintf = 1; //debugger to printf as well!
				}

				verifydebuggerfrominput = 0;
				argch = &argv[argn][0]; //First character of the parameter!
				testparam = &verifydebuggerfrominputparam[0]; //Our parameter to check for!
				for (; *argch != '\0';) //Parse the string!
				{
					if ((char)tolower((int)*argch) != *testparam) //Not matched?
					{
						goto nomatch8;
					}
					if (*testparam == '\0') //No match? We're too long!
					{
						goto nomatch8;
					}
					++argch;
					++testparam;
				}
				nomatch8:
				if ((*argch == *testparam) && (*argch == '\0')) //End of string? Full match!
				{
					verifydebuggerfrominput = 1; //debugger to printf as well!
				}

				emu_log_qemu = 0;
				argch = &argv[argn][0]; //First character of the parameter!
				testparam = &logqemuparam[0]; //Our parameter to check for!
				for (; *argch != '\0';) //Parse the string!
				{
					if ((char)tolower((int)*argch) != *testparam) //Not matched?
					{
						goto nomatch9;
					}
					if (*testparam == '\0') //No match? We're too long!
					{
						goto nomatch9;
					}
					++argch;
					++testparam;
				}
				nomatch9:
				if ((*argch == *testparam) && (*argch == '\0')) //End of string? Full match!
				{
					emu_log_qemu = 1; //debugger to printf as well!
				}
				#endif
			}
		}
	}

	SDLsubsystemflags = SDL_INIT_VIDEO | SDL_INIT_JOYSTICK; //Our default subsystem flags!
	if (usesoundmode) //Using sound output?
	{
		SDLsubsystemflags |= SDL_INIT_AUDIO; //Use audio output!
	}

	initLocks(); //Initialise all locks before anything: we have the highest priority!

	//First, allocate all locks needed!
	getLock(LOCK_ALLOWINPUT);
	getLock(LOCK_GPU);
	getLock(LOCK_CPU);
	getLock(LOCK_VIDEO);
	getLock(LOCK_TIMERS);
	getLock(LOCK_INPUT);
	getLock(LOCK_SHUTDOWN);
	getLock(LOCK_FRAMERATE);
	getLock(LOCK_MAINTHREAD);
	getLock(LOCK_PERFMON);
	getLock(LOCK_THREADS);
	getLock(LOCK_DISKINDICATOR);
	getLock(LOCK_PCAP); //For Pcap packets
	getLock(LOCK_PCAPFLAG); //For the Pcap thread itself

	initHighresTimer(); //Global init of the high resoltion timer!
	initTicksHolder(&CPUUpdate); //Initialise the Video Update timer!

	initlog(); //Initialise the logging system!

#ifdef UNIPCEMU
	BIOS_DetectStorage(); //Detect all storage devices and BIOS Settings file needed to run!
#endif

	//Normal operations!
	resetTimers(); //Make sure all timers are ready!
	
	#ifdef DEBUG_ZALLOC
	//Verify zalloc functionality?
	{
		//First ensure freemem is valid!
		uint_32 f1,f2;
		f1 = freemem(); //Get free memory!
		f2 = freemem(); //Second check!
		if (f1!=f2) //Memory changed on the second check?
		{
			dolog("zalloc_debug","Multiple freemem fail!");
			quitemu(0); //Quit
		}

		uint_32 f;
		f = freemem(); //Detect free memory final!
		
		int *p; //Pointer to int #1!
		int *p2; //Pointer to int #2!
		
		p = (int *)zalloc(sizeof(*p),"zalloc_debug_int",NULL);
		freez((void **)&p,sizeof(*p),"zalloc_debug_int"); //Release int #1!
		
		if (freemem()!=f) //Different free memory?
		{
			dolog("zalloc_debug","Allocation-deallocation failed.");
		}
		p = (int *)zalloc(sizeof(*p),"debug_int",NULL);
		p2 = (int *)zalloc(sizeof(*p),"debug_int_2",NULL);
		freez((void **)&p2,sizeof(*p),"debug_int_2"); //Release int #2!
		freez((void **)&p,sizeof(*p),"debug_int"); //Release int #1!
		
		if (freemem()!=f) //Different free memory?
		{
			dolog("zalloc_debug","Multiple deallocation failed.");
		}
		
		p = (int *)zalloc(sizeof(*p),"debug_int",NULL);
		p2 = (int *)zalloc(sizeof(*p),"debug_int_2",NULL);
		freez((void **)&p,sizeof(*p),"debug_int"); //Release int #1!
		freez((void **)&p2,sizeof(*p),"debug_int_2"); //Release int #2!
		
		if (freemem()!=f) //Different free memory?
		{
			dolog("zalloc_debug","Multiple deallocation (shuffled) failed.");
		}
		
		dolog("zalloc_debug","All checks passed. Free memory: %u bytes Total memory: %u bytes",freemem(),f1);
		quitemu(0); //Quit!
	}
	#endif
	
	if (DELETE_LOGS_ONBOOT)
	{
		delete_file(logpath,"*.log"); //Delete any logs still there!
		delete_file(logpath,"*.txt"); //Delete any logs still there!
	}
	if (DELETE_BMP_ONBOOT) delete_file(capturepath,"*.bmp"); //Delete any bitmaps still there!
	
	#ifdef IS_PSP
		if (FILE_EXISTS("logs/profiler.txt")) //Enable profiler: doesn't work in UniPCemu?
		{
			// Clear the existing profile regs
			pspDebugProfilerClear();
			// Enable profiling
			pspDebugProfilerEnable();
			use_profiler = 1; //Use the profiler!	
		}
	#endif

	#ifdef UNIPCEMU
		BIOS_LoadIO(1); //Load basic BIOS I/O, don't show checksum errors! This is required for our settings!
		initPcap(); //PCAP initialization, when supported!
	#endif

	#ifdef SDL2
	#ifdef SDL_HINT_ANDROID_BLOCK_ON_PAUSE
		SDL_SetHintWithPriority(SDL_HINT_ANDROID_BLOCK_ON_PAUSE,"0",SDL_HINT_OVERRIDE); //We're forcing us to not pause when minimized(on Android)!
	#endif
	#endif

	#ifdef SDL2
	#ifdef SDL_HINT_ANDROID_BLOCK_ON_PAUSE_PAUSEAUDIO
		SDL_SetHintWithPriority(SDL_HINT_ANDROID_BLOCK_ON_PAUSE_PAUSEAUDIO, "0", SDL_HINT_OVERRIDE); //We're forcing us to not pause when minimized(on Android)!
	#endif
	#endif


	if (SDL_InitSubSystem(SDLsubsystemflags)<0) //Error initialising video,audio&joystick?
	{
		raiseError("init","SDL Init error: %s",SDL_GetError()); //Raise an error!
#ifdef UNIPCEMU
		dosleep(); //Wait forever!
#else
		return 0; //Wait forever!
#endif
	}

	initThreads(); //Initialise&reset thread subsystem!
	initVideoLayer(); //We're for allocating the main video layer, only deallocated using SDL_Quit (when quitting the application)!

	debugrow("Initialising main video service...");
	initVideoMain(); //All main video!
	debugrow("Initialising main audio service...");
	initAudio(); //Initialise main audio!

	resetmain: //Main reset!

#ifdef GBEMU
	DEBUG_INIT(); //Initialise debugger!
#endif

	startTimers(1); //Start core timing!
	startTimers(0); //Disable normal timing!

//First, support for I/O on the PSP!

	#ifdef FIND_EMU_MEMORY_LEAKS
	//Find memory leaks?
		uint_32 freememstart;
		freememstart = freemem(); //Freemem at the start!
		logpointers("initEMU(simple test)..."); //Log pointers at the start!
		dolog("zalloc","");
		initEMU(0); //Start the EMU partially, not running video!
		logpointers("doneEMU..."); //Log pointers at work!
		doneEMU(); //Finish the EMU!
		
		logpointers("find_emu_memory_leaks"); //Log pointers after finishing!
		dolog("zalloc","Memory overflow at the end: %i bytes too much deallocated.",freemem()-freememstart); //Should be the ammount of data still allocated!
		termThreads(); //Terminate all running threads still running!

		//Extended test
		freememstart = freemem(); //Freemem at the start!
		logpointers("initEMU (extended test)..."); //Log pointers at the start!
		initEMU(1); //Start the EMU fully, running without CPU!
		delay(10000000); //Wait 10 seconds to allow the full emulator to run some (without CPU)!
		logpointers("doneEMU..."); //Log pointers at work!
		doneEMU(); //Finish the EMU!
		
		logpointers("find_emu_memory_leaks2"); //Log pointers after finishing!
		dolog("zalloc","Memory overflow at the end: %i bytes too much deallocated.",freemem()-freememstart); //Should be the ammount of data still allocated!
		termThreads(); //Terminate all running threads still running!

		debugrow("Terminating main audio service...");		
		doneAudio(); //Finish audio processing!
		debugrow("Terminating main video service...");		
		doneVideoMain(); //Finish video!
		quitemu(0); //Exit software!
		dosleep(); //Wait forever if needed!
	#endif

	
	//Start of the visible part!

	initEMUreset(); //Reset initialisation!
#ifdef GBEMU
	//machineorder(); //Discover machine order!

	if (!ROMLOADED) //No ROM loaded?
	{
		ROMERROR("A ROM is required to run!");
		goto skipcpu; //Skip CPU running!
	}
#endif

	#ifdef NDK_PROFILE
	char gmonoutpath[256]; //GMON.OUT path!
	memset(&gmonoutpath,0,sizeof(gmonoutpath)); //Init!
	safestrcpy(gmonoutpath,sizeof(gmonoutpath),UniPCEmu_root_dir); //Init to root dir!
	safestrcat(gmonoutpath,sizeof(gmonoutpath),"/gmon.out"); //Our output filename in our application directory!
	setenv("CPUPROFILE",gmonoutpath,1); //Set the correct filename to output!
	#endif

	//New SDL way!
	/* Check for events */
	getnspassed(&CPUUpdate);
	lock(LOCK_CPU); //Lock the CPU: we're running!
	lock(LOCK_MAINTHREAD); //Lock the main thread(us)!
	getnspassed(&CPU_timing); //Make sure we start at zero time!
	last_timing = 0.0; //Nothing spent yet!
	timeemulated = 0.0; //Nothing has been emulated yet!
	for (;;) //Still running?
	{
#ifdef IS_WINDOWS
		//Windows Remote Desktop autodetection!
		lock(LOCK_INPUT);
		RDP = (useRDP|IsCurrentSessionRemoteable()); //Are we a remote desktop session?
		RDPDelta |= ((oldRDP ^ RDP)?~0:0); //Many deltas are being kept in parallel! Set all if changed!
		oldRDP = RDP;
		if (RDPDelta & 8) //To update the GPU textures?
		{
			lockGPU();
			RDPDelta &= ~8; //Clear the flag that's pending!
			textureUpdateRequired = 1; //Update required!
			unlockGPU();
		}
		unlock(LOCK_INPUT);
#endif
		updateInputMain(); //Update input!
		CPU_time += (uint_64)getuspassed(&CPUUpdate); //Update the CPU time passed!
		if (CPU_time>=10000) //Allow other threads to lock the CPU requirements once in a while!
		{
			CPU_time %= 10000; //Rest!
			unlock(LOCK_CPU); //Unlock the CPU: we're not running anymore!
			unlock(LOCK_MAINTHREAD); //Lock the main thread(us)!
			if (unlikely(((haswindowactive&0xA)==8) && (backgroundpolicy==0))) //Discarding time and backgrounded?
			{
				delay(1000000); //Wait maximum amount of time, large delays when inactive!
			}
			else //Normal operation?
			{
				delay(0); //Wait minimum amount of time!
			}
			lock(LOCK_MAINTHREAD); //Lock the main thread(us)!
			lock(LOCK_CPU); //Lock the CPU: we're running!
		}
		CPU_updateVideo(); //Update the video if needed from the CPU!
		GPU_tickVideo(); //Tick the video display to keep it up-to-date!
		//Now, run the CPU!
		emu_status = DoEmulator(); //Run the emulator!
		if (unlikely((haswindowactive&0x38)==0x38)) {haswindowactive &= ~0x38;} //Fully active again?
		switch (emu_status) //What to do next?
		{
		case -1: //Continue running?
			emu_status = 0; //Continue running!
			break;
		case 0: //Shutdown
			debugrow("Shutdown...");
			EMU_Shutdown(1); //Execute shutdown!
		case 1: //Full reset emu
			debugrow("Reset..."); //Not supported yet!
		default: //Unknown status?
			debugrow("Invalid EMU return code OR full reset requested!");
			emu_status = 1; //Shut down our thread, returning to the main processor!
		}

		#ifdef IS_WINDOWS
		if (nosleep)
		{
			// Prevent Idle-to-Sleep (monitor not affected) (see note above)
			SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED); //Enable noSleep: we're enabled by the parameter!
		}
		#endif

		if (emu_status) break; //Stop running the CPU?
	}
	debugrow("core: finishing emulator state...");
	unlock(LOCK_CPU); //Unlock the CPU: we're not running anymore!
	doneEMU(); //Finish up the emulator, if still required!
	unlock(LOCK_MAINTHREAD); //Unlock us!
#ifdef GBEMU
	skipcpu: //No CPU to execute?
#endif
	lock(LOCK_MAINTHREAD); //Lock the main thread, preventing corruption!
	debugrow("core: Stopping timers...");
	stopTimers(0); //Stop all timers still running!

	debugrow("core: Finishing emulator state...");
	doneEMU(); //Finish up the emulator, if still required!

	debugrow("core: Stopping timers...");
	stopTimers(1); //Stop all timers still running!

	debugrow("core: Terminating threads...");
	termThreads(); //Terminate all still running threads!

	if (shuttingdown()) //Shutdown requested or SDL termination requested?
	{
		debugrow("core: Finishing user input...");
		psp_input_done(); //Make sure input is set up!
		debugrow("Terminating main audio service...");
		doneAudio(); //Finish audio processing!
		debugrow("Terminating main video service...");
		doneVideoMain(); //Finish video!
#ifdef UNIPCEMU
		debugrow("Terminating network services...");
		termPcap(); //Terminate us, if used!
#endif
		debugrow("Exiting app...");
		unlock(LOCK_MAINTHREAD); //Unlock the main thread!
		EMU_Shutdown(2); //Report actually shutting down!
		exit(0); //Quit using SDL, terminating the pspsurface!
		return 0; //Finish to be safe!
	}
	unlock(LOCK_MAINTHREAD); //Unlock the main thread!
	//Prepare us for a full software/emu reset
	goto resetmain; //Reset?
	return 0; //Just here for, ehm.... nothing!
}
