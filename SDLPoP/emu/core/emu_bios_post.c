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

#include "headers/types.h" //Basic types!

#include "headers/cpu/mmu.h" //For MMU
#include "headers/basicio/io.h" //For mounting!
#include "headers/bios/bios.h" //For BIOS!
#include "headers/bios/biosrom.h" //BIOS ROM support!
#include "headers/bios/biosmenu.h" //For checking the BIOS menu!
#include "headers/bios/initmem.h" //BIOS Memory support!
#include "headers/hardware/pic.h" //Interrupt controller support!
#include "headers/emu/timers.h" //Timers!
#include "headers/support/log.h" //Log support!
#include "headers/interrupts/interrupt18.h" //Interrupt 18h support!
#include "headers/interrupts/interrupt13.h" //Interrupt 13h initialising support!
#include "headers/support/zalloc.h" //For final freezall functionality!
#include "headers/hardware/ps2_mouse.h" //PS/2 mouse support!
#include "headers/hardware/ps2_keyboard.h" //PS/2 keyboard support!
#include "headers/interrupts/interrupt10.h" //Interrupt 10h support!
#include "headers/emu/emucore.h" //Emulation core!
#include "headers/emu/emu_bios_sound.h" //BIOS sound option!
//Debugging:
#include "headers/emu/graphics_debug.h" //Graphics debug!
#include "headers/emu/file_debug.h" //File debug!

#include "headers/emu/soundtest.h" //Sound test support!

#include "headers/cpu/protection.h" //Basic protection support!

#include "headers/emu/emu_main.h" //Main stuff!
#include "headers/emu/emu_vga_bios.h" //VGA BIOS support for output!

//Bootstrap stuff
#include "headers/cpu/easyregs.h" //Easy register stuff!
#include "headers/cpu/cb_manager.h"
#include "headers/interrupts/interrupt05.h"
#include "headers/interrupts/interrupt11.h"
#include "headers/interrupts/interrupt12.h"
#include "headers/interrupts/interrupt15.h"
#include "headers/interrupts/interrupt16.h"
#include "headers/interrupts/interrupt19.h"
#include "headers/interrupts/interrupt1a.h" //Timer and IRQ0!

#include "headers/hardware/ports.h" //I/O support!
#include "headers/cpu/cpu.h" //CPU support!
#include "headers/bios/bios.h" //Keyboard setup support!
#include "headers/emu/emu_vga_bios.h" //VGA BIOS support!
#include "headers/cpu/biu.h" //BIU support!
#include "headers/cpu/cpu_execution.h" //Execution phase support!
#include "headers/fopen64.h" //64-bit fopen support!


extern byte reset; //To fully reset emu?
extern byte dosoftreset; //To fully softreset emu (start from booting)

extern uint_32 romsize; //For checking if we're running a ROM!

extern BIOS_Settings_TYPE BIOS_Settings;
extern byte EMU_RUNNING; //Emulator running? 0=Not running, 1=Running, Active CPU, 2=Running, Inactive CPU (BIOS etc.)

//Special flags for the BIOS POST loader!
//To only debug text/graphic mode operations for now (and sleep after)?
#define DEBUG_VGA_ONLY 0
//Don't run the emulator?
#define NOEMU 0
//Allow the BIOS to be run?
#define ALLOW_BIOS

extern byte allow_debuggerstep; //Do we allow the debugger to step through?

extern byte EMU_BIOS[0x10000]; //Full custom BIOS from 0xF0000-0xFFFFF for the emulator itself to use!
extern word CB_datasegment; //Reserved segment when adding callback!
extern word CB_dataoffset; //Reserved offset when adding callback!

void BIOS_int10entry() //Interrupt 10 BIOS ROM entry point!
{
	//Set up the Video BIOS interrupt and return!
	int10_BIOSInit(); //Call the initialisation routine of interrupt 10h!
}

void BIOS_initStart() //Memory defaults for the CPU with our internal BIOS!
{
	//Initialise VGA ROM memory!
	INT10_SetupRomMemory(0); //Setup ROM memory without interrupts!
	if (EMULATED_CPU<CPU_80286) //Good CPU?
	{
		addCBHandler(CB_VIDEOINTERRUPT,&BIOS_int10,0x10); //Unassigned interrupt #10: Video BIOS. This is reserved in the emulator!
	}
	addCBHandler(CB_VIDEOENTRY,&BIOS_int10entry,0x00); //Interrupt 10h entry point!
	INT10_SetupRomMemoryChecksum(); //Set the checksum for detection!

	debugrow("Setting up the initial emulator JMP to internal BIOS ROM executable...");
	//Our core handlers!
	addCBHandler(CB_UNASSIGNEDINTERRUPT, &BIOS_int19, 0x19); //Second is used by the Bootstrap/BIOS loader! Don't assign to an interrupt!
	//Jump to our BIOS!
}

/* reinitialize the PIC controllers, giving them specified vector offsets
rather than 8h and 70h, as configured by default */

#define PIC1_CMD                    0x20
#define PIC1_DATA                   0x21
#define PIC2_CMD                    0xA0
#define PIC2_DATA                   0xA1
#define PIC_READ_IRR                0x0a    /* OCW3 irq ready next CMD read */
#define PIC_READ_ISR                0x0b    /* OCW3 irq service next CMD read */

#define ICW1_ICW4	0x01		/* ICW4 (not) needed */
#define ICW1_SINGLE	0x02		/* Single (cascade) mode */
#define ICW1_INTERVAL4	0x04		/* Call address interval 4 (8) */
#define ICW1_LEVEL	0x08		/* Level triggered (edge) mode */
#define ICW1_INIT	0x10		/* Initialization - required! */

#define ICW4_8086	0x01		/* 8086/88 (MCS-80/85) mode */
#define ICW4_AUTO	0x02		/* Auto (normal) EOI */
#define ICW4_BUF_SLAVE	0x08		/* Buffered mode/slave */
#define ICW4_BUF_MASTER	0x0C		/* Buffered mode/master */
#define ICW4_SFNM	0x10		/* Special fully nested (not) */

/*
arguments:
offset1 - vector offset for master PIC
vectors on the master become offset1..offset1+7
offset2 - same for slave PIC: offset2..offset2+7
*/
void PIC_remap(int offset1, int offset2)
{
	unsigned char a1, a2;

	a1 = PORT_IN_B(PIC1_DATA);                        // save masks
	a2 = PORT_IN_B(PIC2_DATA);

	PORT_OUT_B(PIC1_CMD, ICW1_INIT + ICW1_ICW4);  // starts the initialization sequence (in cascade mode)
	PORT_OUT_B(PIC2_CMD, ICW1_INIT + ICW1_ICW4);
	PORT_OUT_B(PIC1_DATA, offset1);                 // ICW2: Master PIC vector offset
	PORT_OUT_B(PIC2_DATA, offset2);                 // ICW2: Slave PIC vector offset
	PORT_OUT_B(PIC1_DATA, 4);                       // ICW3: tell Master PIC that there is a slave PIC at IRQ2 (0000 0100)
	PORT_OUT_B(PIC2_DATA, 2);                       // ICW3: tell Slave PIC its cascade identity (0000 0010)

	PORT_OUT_B(PIC1_DATA, ICW4_8086);
	PORT_OUT_B(PIC2_DATA, ICW4_8086);

	PORT_OUT_B(PIC1_DATA, a1);   // restore saved masks.
	PORT_OUT_B(PIC2_DATA, a2);
}

void POST_memorydefaults() //Memory defaults for the CPU without custom BIOS!
{
	//Finally: interrupt callbacks!
	addCBHandler(CB_IRET, NULL, 0x00); //IRET first!
	addCBHandler(CB_INTERRUPT, &BIOS_int05, 0x05); //Interrupt 05h overrideable handler!
	addCBHandler(CB_INTERRUPT, &BIOS_IRQ0, 0x08); //IRQ0 overridable handler!
	CPU_setint(0x10,0xC000,int10.rom.used); //Interrupt 10h overridable handler at the end of the VGA ROM!
	addCBHandler(CB_INTERRUPT, &BIOS_int11, 0x11); //Interrupt 11h overrideable handler!
	addCBHandler(CB_INTERRUPT, &BIOS_int12, 0x12); //Interrupt 12h overrideable handler!
	addCBHandler(CB_INTERRUPT, &BIOS_int13, 0x13); //Interrupt 13h overrideable handler!
	addCBHandler(CB_INTERRUPT, &BIOS_int15, 0x15); //Interrupt 15h overrideable handler!
	addCBHandler(CB_INTERRUPT_BOOT, &BIOS_int18, 0x18); //Interrupt 18h overridable handler!
	addCBHandler(CB_INTERRUPT, &BIOS_int1A, 0x1A); //Interrupt 1Ah overridable handler!
	addCBHandler(CB_IRET,NULL,0x14); //Async communication services to IRET!
	addCBHandler(CB_IRET,NULL,0x17); //Printer to IRET!
	addCBHandler(CB_IRET,NULL,0x1B); //BIOS CTRL-BREAK!
	addCBHandler(CB_IRET,NULL,0x1C); //System tick!
	CPU_setint(0x19, MMU_rw(-1, 0xF000, 0xFFF3, 0,1), MMU_rw(-1, 0xF000, 0xFFF1, 0,1)); //Interrupt 19 (bootstrap)!

	//1D=Video control parameter table
	//1E=Disk base table
	//1F=High video graphics characters
	//49=Translation table for keyboard-supplement devices

	//Set up interrupt handler base table!
	copyint(0x00, 0x01); //Set int 1 to IRET!
	copyint(0x00, 0x02); //Set int 2 to IRET!
	copyint(0x00, 0x03); //Set int 3 to IRET!
	copyint(0x00, 0x04); //Set int 4 to IRET!
	copyint(0x00, 0x06); //Set int 6 to IRET!
	copyint(0x00, 0x07); //Set int 7 to IRET!
	copyint(0x00, 0x09); //Set int 9 to IRET!
	copyint(0x00, 0x0A); //Set int 10 to IRET!
	copyint(0x00, 0x0B); //Set int 11 to IRET!
	copyint(0x00, 0x0C); //Set int 12 to IRET!
	copyint(0x00, 0x0D); //Set int 13 to IRET!
	copyint(0x00, 0x0E); //Set int 14 to IRET!

	//rest is unset or unused!

	PIC_remap(0x08,0x70); //Remap the PIC for our usage!

	PORT_OUT_B(PIC1_DATA, PORT_IN_B(PIC1_DATA)|~0x00); //Disable all interrupts that aren't supported by us!
	PORT_OUT_B(PIC2_DATA, PORT_IN_B(PIC2_DATA)|~0x00); //Disable all interrupts that aren't supported by us!

	//Now, setup overriding interrupts which are installed now!
	BIOS_SetupKeyboard(); //Setup the Dosbox keyboard handler!

	MMU_ww(CPU_segment_index(CPU_SEGMENT_DS), 0x40, 0x72, 0x1234,1); //Make sure we boot the disk only, not do the BIOS again!
}

extern byte is_XT; //XT Architecture?
extern byte is_Compaq; //Compaq Architecture?
extern byte is_PS2; //Are we emulating a Compaq with PS/2 mouse(modern) device?
extern byte is_i430fx; //Are we emulating a i430fx architecture?
extern byte dumpBIOS; //dump BIOS?

//Result: 0=Continue;1=Reset!
int EMU_BIOSPOST() //The BIOS (INT19h) POST Loader!
{
	softreboot: //Little software reboot internal jump!
	allow_debuggerstep = 0; //Default: don't allow to step!

	pauseEMU(); //Stop the emu&input from running!

	lock(LOCK_MAINTHREAD); //Make sure we're in control!
	if (MMU_rw(CPU_segment_index(CPU_SEGMENT_DS), 0x40, 0x72, 0,1) != 0x1234) //Normal BIOS POST?
	{
		debugrow("Running BIOS POST!");
	#ifdef ALLOW_BIOS
		EMU_RUNNING = 0; //We're not running atm!
		unlock(LOCK_MAINTHREAD);

		if (CheckBIOSMenu(3000000)) //Run BIOS Menu if needed for a short time!
		{
			resumeEMU(1); //Start the emulator back up again!
			return 1; //Reset after the BIOS!
		}

		if (shuttingdown()) return 0; //Abort!
	#endif
		lock(LOCK_MAINTHREAD);

		debugrow("Running core BIOS POST...");

		if (BIOS_Settings.executionmode == EXECUTIONMODE_BIOS)
		{
			if (NOEMU)
			{
				dolog("emu", "BIOS is used, but not emulated! Resetting emulator!");
				return 1; //Reboot always: emulation isn't allowed!
			}
			byte verified;
			verified = 0; //Default: not verified!

			doneEMU(); //Terminate the entire emulator, we're starting from scratch!
			initEMU(1); //Reset the entire emulator to it's power-up defaults!
			lock(LOCK_CPU);
			verified = BIOS_load_custom(NULL,"BIOSROM.BIN"); //Try to load a custom general BIOS ROM!
			if (verified) goto loadOPTROMS; //Loaded the BIOS?

			//Load a normal BIOS ROM, according to the chips!
			if (is_XT) //5160/5162(80286) XT PC?
			{
				if ((BIOS_Settings.BIOSROMmode==BIOSROMMODE_DIAGNOSTICS) && (EMULATED_CPU!=CPU_80286)) //Diagnostics mode?
				{
					verified = BIOS_load_custom(NULL, "BIOSROM.XT.DIAGNOSTICS.BIN"); //Try to load a custom 32-bit BIOS ROM!
					if (verified) goto loadOPTROMS; //Loaded the BIOS?							
				}
				if ((EMULATED_CPU!=CPU_80286) && (BIOS_Settings.BIOSROMmode!=BIOSROMMODE_UROMS))
				{
					verified = BIOS_load_custom(NULL, "BIOSROM.XT.BIN"); //Try to load a custom XT BIOS ROM!
					if (verified) goto loadOPTROMS; //Loaded the BIOS?
				}
				if (BIOS_Settings.BIOSROMmode==BIOSROMMODE_DIAGNOSTICS) //Diagnostics mode?
				{
					verified = BIOS_load_custom(NULL, "BIOSROM.XT286.DIAGNOSTICS.BIN"); //Try to load a custom 32-bit BIOS ROM!
					if (verified) goto loadOPTROMS; //Loaded the BIOS?							
				}
				
				if ((BIOS_Settings.BIOSROMmode!=BIOSROMMODE_UROMS) && (EMULATED_CPU==CPU_80286))
				{
					verified = BIOS_load_custom(NULL, "BIOSROM.XT286.BIN"); //Try to load a custom XT BIOS ROM!
					if (verified) goto loadOPTROMS; //Loaded the BIOS?
				}

				if (EMULATED_CPU == CPU_80286) //80286 has different ROMs?
				{
					if (!BIOS_load_ROM(34)) //Failed to load u18?
					{
						dolog("emu", "Failed loading BIOS ROM u34!");
						CPU[activeCPU].halt = 1; //Start to HLT!
						FLAGW_IF(0); //CLI!
						allow_debuggerstep = 1; //Allow stepping from now on!
						resumeEMU(1); //Resume the emulator!
						unlock(LOCK_CPU);
						unlock(LOCK_MAINTHREAD);
						return 0; //No reset!
					}
					if (!BIOS_load_ROM(35)) //Failed to load u19?
					{
						dolog("emu", "Failed loading BIOS ROM u35!");
						BIOS_free_ROM(34); //Release u34!
						CPU[activeCPU].halt = 1; //Start to HLT!
						FLAGW_IF(0); //CLI!
						resumeEMU(1); //Resume the emulator!
						allow_debuggerstep = 1; //Allow stepping from now on!
						unlock(LOCK_CPU);
						unlock(LOCK_MAINTHREAD);
						return 0; //No reset!
					}
				}
				else //Normal PC BIOS ROMs?
				{
					if (!BIOS_load_ROM(18)) //Failed to load u18?
					{
						dolog("emu", "Failed loading BIOS ROM u18!");
						CPU[activeCPU].halt = 1; //Start to HLT!
						FLAGW_IF(0); //CLI!
						allow_debuggerstep = 1; //Allow stepping from now on!
						resumeEMU(1); //Resume the emulator!
						unlock(LOCK_CPU);
						unlock(LOCK_MAINTHREAD);
						return 0; //No reset!
					}
					if (!BIOS_load_ROM(19)) //Failed to load u19?
					{
						dolog("emu", "Failed loading BIOS ROM u19!");
						BIOS_free_ROM(18); //Release u18!
						CPU[activeCPU].halt |= 1; //Start to HLT!
						FLAGW_IF(0); //CLI!
						resumeEMU(1); //Resume the emulator!
						allow_debuggerstep = 1; //Allow stepping from now on!
						unlock(LOCK_CPU);
						unlock(LOCK_MAINTHREAD);
						return 0; //No reset!
					}
				}
				verified = 1; //Verified!
			}
			else //5170 AT+ PC?
			{
				if ((EMULATED_CPU>=CPU_80386) && (is_Compaq || is_i430fx)) //386+ CPU and Compaq architecture(32-bit hardware)? We're using a 32-bit BIOS instead!
				{
					if (is_i430fx) //Try PS/2 architecture first?
					{
						if (is_i430fx == 1) //i430fx?
						{
							if (BIOS_Settings.BIOSROMmode == BIOSROMMODE_DIAGNOSTICS) //Diagnostics mode?
							{
								verified = BIOS_load_custom(NULL, "BIOSROM.i430fx.DIAGNOSTICS.BIN"); //Try to load a custom 32-bit BIOS ROM!
								if (verified) goto loadOPTROMS; //Loaded the BIOS?							
							}

							if (BIOS_Settings.BIOSROMmode != BIOSROMMODE_UROMS)
							{
								verified = BIOS_load_custom(NULL, "BIOSROM.i430fx.BIN"); //Try to load a custom 32-bit BIOS ROM!
								if (verified) goto loadOPTROMS; //Loaded the BIOS?
							}
						}
						else //i440fx?
						{
							if (BIOS_Settings.BIOSROMmode == BIOSROMMODE_DIAGNOSTICS) //Diagnostics mode?
							{
								verified = BIOS_load_custom(NULL, "BIOSROM.i440fx.DIAGNOSTICS.BIN"); //Try to load a custom 32-bit BIOS ROM!
								if (verified) goto loadOPTROMS; //Loaded the BIOS?							
							}

							if (BIOS_Settings.BIOSROMmode != BIOSROMMODE_UROMS)
							{
								verified = BIOS_load_custom(NULL, "BIOSROM.i440fx.BIN"); //Try to load a custom 32-bit BIOS ROM!
								if (verified) goto loadOPTROMS; //Loaded the BIOS?
							}
						}
					}
					if (is_PS2) //Try PS/2 architecture first?
					{
						if (BIOS_Settings.BIOSROMmode == BIOSROMMODE_DIAGNOSTICS) //Diagnostics mode?
						{
							verified = BIOS_load_custom(NULL, "BIOSROM.PS2.DIAGNOSTICS.BIN"); //Try to load a custom 32-bit BIOS ROM!
							if (verified) goto loadOPTROMS; //Loaded the BIOS?							
						}

						if (BIOS_Settings.BIOSROMmode != BIOSROMMODE_UROMS)
						{
							verified = BIOS_load_custom(NULL, "BIOSROM.PS2.BIN"); //Try to load a custom 32-bit BIOS ROM!
							if (verified) goto loadOPTROMS; //Loaded the BIOS?
						}
					}
					if (BIOS_Settings.BIOSROMmode==BIOSROMMODE_DIAGNOSTICS) //Diagnostics mode?
					{
						verified = BIOS_load_custom(NULL, "BIOSROM.32.DIAGNOSTICS.BIN"); //Try to load a custom 32-bit BIOS ROM!
						if (verified) goto loadOPTROMS; //Loaded the BIOS?							
					}

					if (BIOS_Settings.BIOSROMmode!=BIOSROMMODE_UROMS)
					{
						verified = BIOS_load_custom(NULL, "BIOSROM.32.BIN"); //Try to load a custom 32-bit BIOS ROM!
						if (verified) goto loadOPTROMS; //Loaded the BIOS?
					}

					//Try Compaq Deskpro ROMs next!
					if (is_Compaq==1) //Compaq ROMs?
					{
						//u13 (even) and u15(odd)
						if (!BIOS_load_ROM(13)) //Failed to load u13?
						{
							dolog("emu", "Failed loading BIOS ROM u13, reverting to AT ROMs!");
							goto tryATROM; //Try normal AT ROM!
						}
						if (!BIOS_load_ROM(15)) //Failed to load u15?
						{
							dolog("emu", "Failed loading BIOS ROM u15, reverting to AT ROMs!");
							BIOS_free_ROM(13); //Release u13!
							goto tryATROM; //Try normal AT ROM!
						}

						verified = 1; //Verified!
						goto verifiedspecificROMs; //We've verified the specific ROMs!
					}
				}

			tryATROM:
				if (is_PS2) //Try PS/2 architecture first?
				{
					if (BIOS_Settings.BIOSROMmode == BIOSROMMODE_DIAGNOSTICS) //Diagnostics mode?
					{
						verified = BIOS_load_custom(NULL, "BIOSROM.PS2.DIAGNOSTICS.BIN"); //Try to load a custom 32-bit BIOS ROM!
						if (verified) goto loadOPTROMS; //Loaded the BIOS?							
					}

					if (BIOS_Settings.BIOSROMmode != BIOSROMMODE_UROMS)
					{
						verified = BIOS_load_custom(NULL, "BIOSROM.PS2.BIN"); //Try to load a custom AT BIOS ROM!
						if (verified) goto loadOPTROMS; //Loaded the BIOS?
					}
				}
				if (BIOS_Settings.BIOSROMmode==BIOSROMMODE_DIAGNOSTICS) //Diagnostics mode?
				{
					verified = BIOS_load_custom(NULL, "BIOSROM.AT.DIAGNOSTICS.BIN"); //Try to load a custom 32-bit BIOS ROM!
					if (verified) goto loadOPTROMS; //Loaded the BIOS?							
				}

				if (BIOS_Settings.BIOSROMmode!=BIOSROMMODE_UROMS)
				{
					verified = BIOS_load_custom(NULL, "BIOSROM.AT.BIN"); //Try to load a custom AT BIOS ROM!
					if (verified) goto loadOPTROMS; //Loaded the BIOS?
				}

				if (!BIOS_load_ROM(27)) //Failed to load u27?
				{
					dolog("emu", "Failed loading BIOS ROM u27!");
					CPU[activeCPU].halt = 1; //Start to HLT!
					FLAGW_IF(0); //CLI!
					allow_debuggerstep = 1; //Allow stepping from now on!
					resumeEMU(1); //Resume the emulator!
					unlock(LOCK_CPU);
					unlock(LOCK_MAINTHREAD);
					return 0; //No reset!
				}
				if (!BIOS_load_ROM(47)) //Failed to load u47?
				{
					dolog("emu", "Failed loading BIOS ROM u47!");
					BIOS_free_ROM(27); //Release u27!
					CPU[activeCPU].halt = 1; //Start to HLT!
					FLAGW_IF(0); //CLI!
					resumeEMU(1); //Resume the emulator!
					allow_debuggerstep = 1; //Allow stepping from now on!
					unlock(LOCK_CPU);
					unlock(LOCK_MAINTHREAD);
					return 0; //No reset!
				}
				verified = 1; //Verified!
			}

			verifiedspecificROMs:

			BIOS_free_systemROM(); //Stop our own ROM: we're using the loaded ROMs now!

		loadOPTROMS:

			if (verified) //Ready to boot, but need option ROMS?
			{
				verified = BIOS_checkOPTROMS(); //Try and load OPT roms!
				if (!verified) //Error reading ROM?
				{
					dolog("emu", "Failed loading Option ROMs!");
				}
			}

			if (!verified) //Error reading ROM?
			{
				dolog("emu", "Failed loading ROMs! Resetting emulator!");
				CPU[activeCPU].halt = 1; //Start to HLT!
				FLAGW_IF(0); //CLI!
				unlock(LOCK_CPU);
				resumeEMU(1); //Resume the emulator!
				unlock(LOCK_MAINTHREAD);
				return 0; //No reset!
			}
			else //Boot rom ready?
			{
				BIOS_registerROM(); //Register the BIOS ROMS to actually start using them!
				if (dumpBIOS)
				{
					BIOSROM_dumpBIOS(); //Dump the BIOS!
				}
				CPU_flushPIQ(-1); //Make sure the PIQ is up-to-date with out newly mapped ROMs in IPS mode!
				allow_debuggerstep = 1; //Allow stepping from now on!
				startTimers(0); //Make sure we're running fully!
				startTimers(1); //Make sure we're running fully!
				resumeEMU(1); //Resume the emulator!
				EMU_startInput(); //Start input again!
				unlock(LOCK_CPU);
				unlock(LOCK_MAINTHREAD);
				return 0; //No reset, start the BIOS!
			}
		}

		debugrow("Continuing BIOS POST...");
		EMU_stopInput(); //Stop emulator input!

		debugrow("BIOS Beep...");
		doBIOSBeep(); //Do the beep to signal we're ready to run!	

		//Now for the user visible part:

		//Start the required timers first!
		useTimer("Framerate",1); //Enable framerate display too, if used!

		//Start the CPU to execute some of our output only!
		lock(LOCK_CPU);
		CPU[activeCPU].halt |= 2; //Make sure the CPU is just halted!
		CPU[activeCPU].halt &= ~4; //Start VGA rendering!
		unlock(LOCK_CPU); //We're done with the CPU!

		if (DEBUG_VGA_ONLY)
		{
			unlock(LOCK_MAINTHREAD);
			DoDebugTextMode(1); //Text mode debugging only, finally sleep!
		}

#ifdef ALLOW_BIOS
		debugrow("BIOS POST Screen...");
		//Now we're ready to go run the POST!

		lock(LOCK_CPU);
		REG_AH = 0x00; //Init video mode!
		REG_AL = VIDEOMODE_EMU; //80x25 16-color TEXT for EMU mode!
		BIOS_int10(); //Switch!
		unlock(LOCK_CPU); //We're done with the CPU!

		BIOS_enableCursor(0); //Disable the cursor!

		unlock(LOCK_MAINTHREAD);
		delay(200000); //Wait a bit before showing on-screen!

		lock(LOCK_MAINTHREAD);
		printmsg(0xF, "UniPCemu\r\n");
		printmsg(0xF, "\r\n"); //A bit of whitespace before output!
#endif

#ifdef ALLOW_BIOS
		BIOS_LoadIO(1); //Load basic BIOS I/O (disks), show checksum errors!
#else
		BIOS_LoadIO(0); //Load basic BIOS I/O (disks), hide checksum errors!
#endif

#ifdef ALLOW_BIOS
		startTimers(0); //Start EMU timers!
		BIOS_ShowBIOS(); //Show BIOS information!
		unlock(LOCK_MAINTHREAD);
		if (CheckBIOSMenu(0)) //Run BIOS Menu if needed!
		{
			BIOS_enableCursor(1); //Re-enable the cursor!
			resumeEMU(1); //Resume the emulator!
			return 1; //Reset after the BIOS!
		}
		lock(LOCK_MAINTHREAD);
#endif

		BIOS_enableCursor(1); //Re-enable the cursor!

		if (DEBUG_VIDEOCARD) //Debugging text mode?
		{
			unlock(LOCK_MAINTHREAD);
			DoDebugTextMode(0); //Do the debugging!
			resumeEMU(0); //Resume the emulator!
			return 1; //Full reset emulator!
		}

		if (shuttingdown()) //Shut down?
		{
			unlock(LOCK_MAINTHREAD);
			EMU_Shutdown(0); //Done shutting down!
			quitemu(0); //Shut down!
		}

		//First debugger step: custom bios check!

		//Emulate anything here!
		BIGFILE *f; //The file to use for loading ROMs.
		//Try booting of different disks:

		switch (BIOS_Settings.executionmode) //What execution mode?
		{
		case EXECUTIONMODE_TEST:
			unlock(LOCK_MAINTHREAD);
			debugrow("Debugging files!");
			lock(LOCK_CPU); //Lock the main thread!
			DoDebugFiles(); //Do the debug files!
			unlock(LOCK_CPU); //Finished with the main thread!
			resumeEMU(0); //Resume the emulator!
			return 1; //Reboot to terminate us!

		case EXECUTIONMODE_SOUND:
			unlock(LOCK_MAINTHREAD);
			debugrow("Starting sound test...");
			dosoundtest(); //Run the sound test!
			goto softreboot; //Execute soft reboot!
			return 1; //Reboot!

		case EXECUTIONMODE_TESTROM:
			lock(LOCK_CPU); //Lock the main thread!
			f = emufopen64("TESTROM.DAT", "rb"); //Try TESTROM.DAT?
			int verified;
			romsize = 0; //Default: no ROM!

			if (f) //Show boot rom msg?
			{
				printmsg(0x0F, "Booting Test ROM...\r\n");
			}

			if (f) //Boot ROM?
			{
				emufseek64(f, 0, SEEK_END); //Goto EOF!
				romsize = (uint_32)emuftell64(f); //ROM size!
				emufseek64(f, 0, SEEK_SET); //Goto BOF!
				byte *ptr = (byte *)MMU_ptr(-1, 0x0000, 0x0000, 0, romsize); //Read a pointer to test ROM memory!
				if (ptr) //Valid pointer?
				{
					verified = (emufread64(ptr, 1, romsize, f)==romsize); //Read ROM to memory adress 0!
				}
				else
				{
					verified = 0; //Failed!
				}
				REG_CS = REG_DS = REG_ES = 0;
				REG_IP = 0; //Run ROM!
				CPU_flushPIQ(-1); //We're jumping to another address!
				REG_SS = 0;
				REG_SP = 0x100; //For ROM specific!
				emufclose64(f); //Close boot rom!
				if (!verified) //Error reading ROM?
				{
					CPU[activeCPU].halt |= 1; //Start to HLT!
					FLAGW_IF(0); //CLI!
					EMU_startInput(); //Start input again!
					EMU_RUNNING = 1; //We're running again!
					resumeEMU(1);
					unlock(LOCK_CPU); //We're done with the CPU!
					unlock(LOCK_MAINTHREAD);
					return 0; //No reset!
				}
				else //Boot rom ready?
				{
					EMU_startInput(); //Start input again!
					EMU_RUNNING = 1; //We're running again!
					allow_debuggerstep = 1; //Allow stepping from now on!
					resumeEMU(1);
					unlock(LOCK_CPU); //We're done with the CPU!
					unlock(LOCK_MAINTHREAD);
					return 0; //Run the boot rom!
				}
			}
			unlock(LOCK_CPU); //We're done with the CPU!
			break;
		default: //Unknown state? Ignore the setting!
			break;
		}

		if (NOEMU)
		{
			resumeEMU(1); //Resume the emulator!
			unlock(LOCK_MAINTHREAD);
			return 1; //Don't emulate: just reset!
		}

		debugrow("Starting CPU emulation...");

		//We're starting up normal emulation of our 'BIOS'?

		initMEM(); //Initialise all BIOS stuff in memory!

		POST_memorydefaults(); //Install default handlers for interrupts etc.!

		keyboardControllerInit_extern(); //Initialize the keyboard controller always!
		BIOSKeyboardInit(); //Initialise the BIOS stuff for the keyboard!
		if (is_XT) keyboardControllerInit(0); //BIOS: Swallow the 0xAA byte that's left, if required!

		REG_AX = VIDEOMODE_BOOT; //TEXT mode for booting!
		BIOS_int10(); //Switch modes!

		REG_AH = 0x06; //Shift down total rows (moving everthing down one line)?
		REG_AL = 0; //0 for clear >0 for shift!
		REG_BH = 0xF; //Attribute!
		REG_CH = 0;
		REG_CL = 0;
		REG_DH = 23;
		REG_DL = 79; //Coordinates of our window!
		BIOS_int10(); //Clear the screen!

		REG_AH = 0x02;
		REG_BH = 0; //Page #0!
		REG_DH = 0; //Y!
		REG_DL = 0; //X!
		BIOS_int10(); //Move cursor!

		REG_AX = 0;
		REG_BX = 0;
		REG_CX = 0;
		REG_DX = 0; //Reset basic registers!
	}

	int13_init(1, 1, is_mounted(HDD0), is_mounted(HDD1), 1, 1); //Initialise interrupt 13h disks! Always floppy0&1 and cdrom0&1. HDD are predefined and mounted.

	//Execute boot sequence, if possible...
	if (EMULATED_CPU >= CPU_80286) //Emulating a CPU with protected mode?
	{
		printmsg(0xF, "You can't use the 80286+ with the default BIOS. Please insert a BIOS ROM.");
		delay(1000000); //Wait 1 second before rebooting!
		MMU_ww(CPU_segment_index(CPU_SEGMENT_DS), 0x0000, 0x0472, 0,1); //Clear reboot flag!
		REG_CS = 0xF000; //Go back to our bootstrap, by using a simulated jump to ROM!
		REG_IP = 0xFFFF;
		CPU_flushPIQ(-1); //We're jumping to another address!
		lock(LOCK_CPU);
		CPU_commitState(); //Commit the current state!
		CPU[activeCPU].halt &= ~0x12; //Make sure the CPU is just halted!
		unlock(LOCK_CPU); //We're done with the CPU!
	}
	else //We can boot safely?
	{
		//Boot to disk system!
		if (!boot_system()) //System not booted?
		{
			CPU_executionphase_startinterrupt(0x18,0,-1); //Boot failure!
		}
		else //We're booted?
		{
			BIOS_DUMPSYSTEMROM(); //Dump our system ROM for debugging purposes, if enabled!
			allow_debuggerstep = 1; //Allow stepping from now on!
		}
		lock(LOCK_CPU);
		BIOS_registerROM(); //Register the BIOS ROMS!
		EMU_startInput(); //Start input again!
		allow_debuggerstep = 1; //Allow stepping from now on!
		startTimers(0); //Make sure we're running fully!
		startTimers(1); //Make sure we're running fully!
		resumeEMU(1); //Resume the emulator!
		CPU[activeCPU].halt &= ~0x12; //Make sure the CPU is just halted!
		unlock(LOCK_CPU);
		unlock(LOCK_MAINTHREAD);
		return 0; //Continue normally: we've booted, or give an error message!
	}

	resumeEMU(1); //Resume the emulator!
	lock(LOCK_CPU);
	CPU[activeCPU].halt &= ~0x12; //Make sure the CPU is just halted!
	unlock(LOCK_CPU); //We're done with the CPU!
	unlock(LOCK_MAINTHREAD);
	return 0; //Plain run!
}
