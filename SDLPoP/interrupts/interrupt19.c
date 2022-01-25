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

#include "headers/types.h"
#include "headers/bios/bios.h" //BOOT loader!
#include "headers/emu/threads.h" //Thread support!
#include "headers/emu/emu_main.h" //EMU_BIOSPOST support!
#include "headers/cpu/cpu.h" //CPU_resetOP support!

extern byte reset; //To reset the emulator?
extern BIOS_Settings_TYPE BIOS_Settings; //Our BIOS Settings!

//Boot strap loader!

/*
note 1) Reads track 0, sector 1 into address 0000h:7C00h, then transfers
        control to that address. If no diskette drive available, looks at
        absolute address C:800 for a valid hard disk or other ROM. If none,
        transfers to ROM-BASIC via int 18h or displays loader error message.
     2) Causes reboot of disk system if invoked while running. (no memory test
        performed).
     3) If location 0000:0472h does not contain the value 1234h, a memory test
        (POST) will be performed before reading the boot sector.
     4) VDISK from DOS 3.0+ traps this vector to determine when the CPU has
        shifted from protected mode to real mode. A detailed discussion can
        be found by Ray Duncan in PC Magazine, May 30, 1989.
     5) Reportedly, some versions of DOS 2.x and all versions of DOS 3.x+
        intercept int 19h in order to restore some interrupt vectors DOS takes
        over, in order to put the machine back to a cleaner state for the
        reboot, since the POST will not be run on the int 19h. These vectors
        are reported to be: 02h, 08h, 09h, 0Ah, 0Bh, 0Ch, 0Dh, 0Eh, 70h, 72h,
        73h, 74h, 75h, 76h, and 77h. After restoring these, it restores the
        original int 19h vector and calls int 19h.
     6) The system checks for installed ROMs by searching memory from 0C000h to
        the beginning of the BIOS, in 2k chunks. ROM memory is identified if it
        starts with the word 0AA55h. It is followed a one byte field length of
        the ROM (divided by 512). If ROM is found, the BIOS will call the ROM
        at an offset of 3 from the beginning. This feature was not supported in
        the earliest PC machines. The last task turns control over to the
        bootstrap loader (assuming the floppy controller is operational).
     7) 8255 port 60h bit 0 = 1 if booting from diskette.
*/

extern ThreadParams_p BIOSMenuThread; //BIOS pause menu thread!
extern ThreadParams_p debugger_thread; //The debugger thread, if any!

void POSTThread()
{
	reset = (byte)EMU_BIOSPOST(); //Execute POST, process emulator reset if needed!
}

void BIOS_int19()
{
	if (BIOSMenuThread) //Gotten a POST thread?
	{
		if (threadRunning(BIOSMenuThread))
		{
			return; //Don't re-start the POST thread when already running: keep idle looping(waiting for it to finish) while it's running!
		}
	}
	CPU_resetOP(); //Reset the CPU to re-run this opcode by default!
	if ((BIOSMenuThread==NULL) && (debugger_thread==NULL)) //Not started yet?
	{
		BIOSMenuThread = startThread(&POSTThread,"UniPCemu_POST",NULL); //Start the POST thread at default priority!
	}
	delay(0); //Wait a bit to start up the thread!
}