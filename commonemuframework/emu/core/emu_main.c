//This file is part of The Common Emulator Framework.

#include "..\commonemuframework\headers\types.h" //"headers/types.h" //Basic types!

#include "..\commonemuframework\headers\emu\emu_main.h" //"headers/emu/emu_main.h" //Ourselves!

#include "..\commonemuframework\headers\emu\emucore.h" //"headers/emu/emucore.h"

//Disable BIOS&OS loading for testing?
#define NOEMU 0
#define NOBIOS 0

/*

BIOS Loader&Execution!

*/

byte use_profiler = 0; //To use the profiler?

byte emu_use_profiler()
{
	return use_profiler; //To use or not?
}

//Main loader stuff:

byte reset = 0; //To fully reset emu?
uint_32 romsize = 0; //For checking if we're running a ROM!

extern byte EMU_RUNNING; //Emulator running? 0=Not running, 1=Running, Active CPU, 2=Running, Inactive CPU (BIOS etc.)

void finishEMU() //Called on emulator quit.
{
	doneEMU(); //Finish up the emulator that was still running!
}