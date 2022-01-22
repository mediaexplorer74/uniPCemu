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

#ifndef PS2_KEYBOARD_H
#define PS2_KEYBOARD_H

#include "headers/types.h" //Basic types!
#include "headers/support/fifobuffer.h" //FIFOBUFFER support!

//Timeout between commands or parameters and results being buffered! Use 5ms timings!
#ifdef IS_LONGDOUBLE
#define KEYBOARD_DEFAULTTIMEOUT 100000.0L
//BAT completion code must be 500~750ms after powerup
#define KEYBOARD_BATTIMEOUT 600000000.0L
#define KEYBOARD_i430fx_RESETCOMMAND_BATTIMEOUT 100000.0L
#else
#define KEYBOARD_DEFAULTTIMEOUT 100000.0
//BAT completion code must be 500~750ms after powerup
#define KEYBOARD_BATTIMEOUT 600000000.0
#define KEYBOARD_i430fx_RESETCOMMAND_BATTIMEOUT 100000.0
#endif

typedef struct
{
byte keypress[8];
byte keypress_size; //1-8
byte keyrelease[8];
byte keyrelease_size; //0-8: 0 for none!
} KEYBOARDENTRY; //Entry containing keyboard character data (press/hold and release data)!

typedef struct
{
	KEYBOARDENTRY entry;
	byte used; //Are we used instead of a normal one?
} KEYBOARDENTRY_EXTENDED;

//RnD
#ifndef UNIPCEMU//#ifdef UNIPCEMU
typedef struct
{
	byte keyboard_enabled; //Keyboard enabled?
	byte typematic_rate_delay; //Typematic rate/delay setting (see lookup table) bits 0-4=rate(chars/second);5-6=delay(ms)!
	byte scancodeset; //Scan code set!

	byte LEDS; //The LEDS active!

	byte has_command; //Has command?
	byte command; //Current command given!
	byte command_step; //Step within command (if any)?

	byte last_send_byte; //Last send byte from keyboard controller!

	FIFOBUFFER *buffer; //Buffer for output!
	byte repeatMake; //Repeat make codes?
	byte allowBreak; //Allow break codes?
	byte enable_translation; //Enable translation by 8042?
	DOUBLE timeout; //Timeout for reset commands!
	byte cmdOK; //Are we OK?
} PS2_KEYBOARD; //Active keyboard settings!

void give_keyboard_output(byte data); //For the 8042!
void input_lastwrite_keyboard(); //Force data to user!
#endif


byte EMU_keyboard_handler(byte key, byte pressed, byte ctrlispressed, byte altispressed, byte shiftispressed); //A key has been pressed (with interval) or released CALLED BY HARDWARE KEYBOARD (Virtual Keyboard?)? 0 indicates failure sending it!
//Name/ID conversion functionality!
int EMU_keyboard_handler_nametoid(char *name); //Same as above, but with unique names from the keys_names table!
int EMU_keyboard_handler_idtoname(int id, char *name); //Same as above, but with unique names from the keys_names table!

float HWkeyboard_getrepeatrate(); //Which repeat rate to use after the repeat delay! (chars/second)
word HWkeyboard_getrepeatdelay(); //Delay after which to start using the repeat rate till release! (in ms)

#ifndef UNIPCEMU//#ifdef UNIPCEMU
void BIOS_initKeyboard(); //Initialise the PS/2 keyboard (AFTER the 8042!)
void BIOS_doneKeyboard(); //Finish the PS/2 keyboard.

void resetKeyboard_8042(byte flags); //8042 reset for XT compatibility!

void keyboardControllerInit_extern(); //Keyboard initialization for the BIOS!

void updatePS2Keyboard(DOUBLE timepassed); //For stuff requiring timing!

//Special use for the BIOS only!
void keyboardControllerInit(byte is_extern); //Part before the BIOS at computer bootup (self test)!
#endif

#ifdef GBEMU
void initKeyboardLookup();
#endif

#endif