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

#include "headers/hardware/ports.h" //Port support!
#include "headers/hardware/8253.h" //PC speaker support!
#include "headers/emu/threads.h" //Thread support!
#include "headers/emu/sound.h" //Sound support for our callback!
#include "headers/emu/timers.h" //Timer support!
#include "headers/support/mid.h" //MIDI file support!
#include "headers/cpu/cpu.h" //CPU support!
#include "headers/bios/biosmenu.h" //BIOS menu option support!
#include "headers/interrupts/interrupt10.h" //Interrupt 10h support!
#include "headers/emu/emu_vga_bios.h" //Cursor enable support!
#include "headers/support/dro.h" //DRO file support!
#include "headers/cpu/easyregs.h" //Easy register support!


//Test the speaker?
#define __DEBUG_SPEAKER
//Test the Adlib?
#define __DEBUG_ADLIB

//Test MIDI?
#define __DEBUG_MIDI

void adlibsetreg(byte reg,byte val)
{
	PORT_OUT_B(0x388,reg);
	PORT_OUT_B(0x389,val);
}

//Cannot read adlib registers!

byte adlibgetstatus()
{
	return PORT_IN_B(0x388); //Read status byte!
}

int detectadlib()
{
	return 1; //Detected: we cannot be detected because the CPU isn't running!
}

void dosoundtest()
{
	REG_AH = 0x00; //Init video mode!
	REG_AL = VIDEOMODE_EMU; //80x25 16-color TEXT for EMU mode!
	BIOS_int10(); //Switch!

	BIOS_enableCursor(0); //Disable the cursor!
	delay(1000000); //Wait 1 second!
	if (shuttingdown()) goto doshutdown;

	printmsg(0xF, "\r\nStarting sound test...\r\n");
	delay(1000000);
	if (shuttingdown()) goto doshutdown;
	VGA_waitforVBlank(); //Wait 1 frame!
	if (shuttingdown()) goto doshutdown;

	#ifdef __DEBUG_SPEAKER
	printmsg(0xF,"Debugging PC Speaker...\r\n");
	setPITFrequency(2,(word)(1190000.0f/100.0f)); //Low!
	PORT_OUT_B(0x61,(PORT_IN_B(0x61)|3)); //Enable the second speaker!
	delay(1000000);
	if (shuttingdown()) goto doshutdown;
	
	setPITFrequency(2,(word)(1190000.0f/1000.0f)); //Medium!
	delay(1000000);
	if (shuttingdown()) goto doshutdown;

	setPITFrequency(2,(word)(1190000.0f/2000.0f)); //High!
	delay(1000000);
	if (shuttingdown()) goto doshutdown;

	PORT_OUT_B(0x61, (PORT_IN_B(0x61) & 0xFC)); //Disable the speaker!

	delay(4000000); //Wait 1 second for the next test!
	if (shuttingdown()) goto doshutdown;
#endif

#ifdef __DEBUG_ADLIB
	startTimers(0); //Make sure we're timing (needed for adlib test).
	printmsg(0xF, "Detecting adlib...");
	if (detectadlib()) //Detected?
	{
		if (shuttingdown()) goto doshutdown;
		printmsg(0xF, "\r\nAdlib detected. Starting sound in 1 second...");
		VGA_waitforVBlank(); //Wait 1 frame!
		if (shuttingdown()) goto doshutdown;
		VGA_waitforVBlank(); //Wait 1 frame!
		if (shuttingdown()) goto doshutdown;
		printmsg(0xF, "\r\nStarting adlib sound...");
		if (playDROFile("music/ADLIB.DRO",1)) goto skipadlib;
		adlibsetreg(0x20, 0x21); //Modulator multiple to 1!
		adlibsetreg(0x40, 0x3F); //Modulator level about zero to produce a pure tone!
		adlibsetreg(0x60, 0xF7); //Modulator attack: quick; decay long!
		adlibsetreg(0x80, 0xFF); //Modulator sustain: medium; release: medium
		adlibsetreg(0xA0, 0x98); //Set voice frequency's LSB (it'll be a D#)!
		adlibsetreg(0x23, 0x21); //Set the carrier's multiple to 1!
		adlibsetreg(0x43, 0x00); //Set the carrier to maximum volume (about 47dB).
		adlibsetreg(0x63, 0xFF); //Carrier attack: quick; decay: long!
		adlibsetreg(0x83, 0x0F); //Carrier sustain: medium; release: medium!
		adlibsetreg(0xB0, 0x31); //Turn the voice on; set the octave and freq MSB!
		adlibsetreg(0xC0, 0x00); //No feedback and use FM synthesis!
		printmsg(0xF, "\r\nYou should only be hearing the Adlib tone now.");
		delay(5000000); //Basic tone!
		if (shuttingdown()) goto doshutdown;
		int i,j;
		adlibsetreg(0x40, 0x10); //Modulator level about 40dB!
		for (j = 0; j < 2; j++)
		{
			for (i = 0; i <= 7; i++)
			{
				if (j)
				{
					printmsg(0xF, "\r\nSetting feedback level %i, additive synthesis", i);
				}
				else
				{
					printmsg(0xF, "\r\nSetting feedback level %i, fm synthesis", i);
				}
				adlibsetreg(0xC0, ((i << 1) | j));
				delay(3000000); //Wait some time!
				if (shuttingdown()) goto doshutdown;
			}
		}
		printmsg(0xF, "\r\nResetting synthesis to fm synthesis without feedback...");
		adlibsetreg(0xC0, 0); //Reset synthesis mode and disable feedback!
		delay(10000000); //Adlib only!
		if (shuttingdown()) goto doshutdown;
		printmsg(0xF, "\r\nSilencing Adlib tone...");
		adlibsetreg(0xB0,0x11); //Turn voice off!
		delay(4000000); //Wait 1 second for the next test!
		if (shuttingdown()) goto doshutdown;
		printmsg(0xF, "\r\n"); //Finisher!
	}
	skipadlib: //Skip test playback?
#endif

	#ifdef __DEBUG_MIDI
	printmsg(0xF,"Debugging MIDI...\r\n");
	VGA_waitforVBlank(); //Wait for a VBlank, to allow the screen to be up to date!
	if (shuttingdown()) goto doshutdown;
	if (!playMIDIFile("music/MPU.MID",1)) //Play the default(we're showing info when playing MIDI files)?
	{
		if (shuttingdown()) goto doshutdown;
		stopTimers(1); //Stop ALL timers for testing speed!
		//Don't worry about timing!
		PORT_OUT_B(0x331, 0xFF); //Reset!
		PORT_OUT_B(0x331, 0x3F); //Kick to UART mode!
		PORT_OUT_B(0x330, 0xC0);
		PORT_OUT_B(0x330, 0x00); //Piano

		//Apply drum kit!

		//Switch to the drum kit set!
		PORT_OUT_B(0x330, 0xB0); //Controller change!
		PORT_OUT_B(0x330, 0x00); //Bank high!
		PORT_OUT_B(0x330, 0x00); //0xXX00
		PORT_OUT_B(0x330, 0x20); //Bank low!
		PORT_OUT_B(0x330, 0x00); //0x00XX

		byte notes[10] = { 60, 62, 64, 65, 67, 69, 71, 72, 74, 76 };
		byte i;
		for (i = 0; i < 10;)
		{
			PORT_OUT_B(0x330, 0x90); //First tone ON!
			PORT_OUT_B(0x330, notes[i]); //This note...
			PORT_OUT_B(0x330, 100); //Is sounded at AVG velocity!!
			delay(10000); //Wait 1 second!
			if (shuttingdown()) goto doshutdown;
			PORT_OUT_B(0x330, 0x80); //Note off!
			PORT_OUT_B(0x330, notes[i]); //Previous note!
			PORT_OUT_B(0x330, 100); //Normally off!
			delay(1000000); //Wait 1 second!
			if (shuttingdown()) goto doshutdown;
			++i; //Next note!
		}
		delay(10000000);
	}
	#endif
doshutdown:
	exit(0); //Quit the application!
	dosleep(); //Wait forever!
}