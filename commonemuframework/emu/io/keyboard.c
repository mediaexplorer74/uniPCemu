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

#include "headers/hardware/ps2_keyboard.h" //Keyboard support!
#include "headers/support/log.h"

//Basic keyboard support for emulator, mapping key presses and releases to the PS/2 keyboard!

//Log input and output to compare?
//#define __DEBUG_INPUT

//How many ns to wait to check for input (1000000=1ms)!
#define KEYBOARD_CHECKTIME 1000000
//How many us to take, in multiples of KEYBOARD_CHECKTIME, to reach one second!
#define KEYBOARD_MAXSTEP 1000
//Multiplier on keyboard delay (in ms) to get equal to check time. Multiply this by KEYBOARD_CHECKTIME to get 1ms.
#define KEYBOARD_DELAYSTEP 1


extern byte SCREEN_CAPTURE; //Screen capture requested?

char releasename[256]; //Special key for detection!

//All keys that are modifier keys, not to be released together with the normal key immediately, and pressed before the normal key is pressed!
int lctrlindex, laltindex, lshiftindex, rctrlindex, raltindex, rshiftindex, lwinindex, rwinindex;

int_64 key_pressed_counter = 0; //Counter for pressed keys!
uint_32 keys_pressed = 0; //Currently ammount of keys pressed!

word keys_arepressed; //How many keys of keys_ispressed are actually pressed?
byte key_status[0x100]; //Status of all keys!
byte keys_ispressed[0x100]; //All possible keys to be pressed!
int_64 key_pressed_time[0x100]; //What time was the key pressed?
int_64 last_key_pressed_time_saved = -1; //Last key pressed time!
byte capture_status; //Capture key status?

//We're running each ms, so 1000 steps/second!
uint_64 pressedkeytimer = 0; //Pressed key timer!
uint_64 keyboard_step = 0; //How fast do we update the keys (repeated keys only)!
DOUBLE keyboard_time = 0; //Total time currently counted!

void onKeyPress(char *key) //On key press/hold!
{
	if (!strcmp(key,"CAPTURE")) //Screen capture requested?
	{
		if ((!SCREEN_CAPTURE) && (!capture_status)) //Not capturing yet and now instructed to start capturing?
		{
			SCREEN_CAPTURE = 1; //Do a screen capture next frame!
		}
		capture_status = 1; //We're pressed!
		return; //Finished!
	}
	int keyid;
	keyid = EMU_keyboard_handler_nametoid(key); //Try to find the name!
	if (keyid!=-1) //Key found?
	{
		#ifdef __DEBUG_INPUT
		dolog("input","Physical Keypress:%s",key);
		#endif
		if (!(key_status[keyid]&5)) //New key pressed and allowed to press?
		{
			key_pressed_time[keyid] = key_pressed_counter++; //Increasing time of the key being pressed!
			key_status[keyid] = 1; //We've pressed a new key, not released!
			++keys_pressed; //A key has been pressed!
		}
	}
}

byte onKeyRelease(char *key) //On key release!
{
	if (!strcmp(key, "CAPTURE")) //Screen capture requested?
	{
		capture_status = 0; //We're released!
		return 1; //Finished!
	}
	int keyid;
	keyid = EMU_keyboard_handler_nametoid(key); //Try to find the name!
	if (keyid!=-1) //Key found and pressed atm?
	{
		#ifdef __DEBUG_INPUT
		dolog("input","Physical key release:%s",key);
		#endif
		if (key_status[keyid]&1) //Pressed?
		{
			key_status[keyid] |= 2; //We're released now!
			key_status[keyid] &= ~4; //Clear our pending flag if it's there: we're allowed to be released!
		}
		return 1; //We're released!
	}
	return 1; //Unhandled input, so ignore release!
}

void calculateKeyboardStep()
{
	if (keyboard_step) //Delay executed?
	{
		keyboard_step = (uint_64)(KEYBOARD_MAXSTEP / HWkeyboard_getrepeatrate()); //Apply this count per keypress!
	}
	else
	{
		keyboard_step = HWkeyboard_getrepeatdelay()*KEYBOARD_DELAYSTEP; //Delay before giving the repeat rate!
	}
}

void releaseKeyReleased(uint_64 keytime)
{
	byte releaseall=0;
	INLINEREGISTER word i;
releaseallkeys:
	i = 0;
	do //Process all keys needed!
	{
		if (keys_ispressed[i] && ((key_pressed_time[i]==keytime) || ((key_status[i]&1) && releaseall))) //Are we pressed and allowed to release?
		{
			if (((key_status[i]&3)==3) || ((key_status[i]&1) && releaseall)) //Are we pressed&released or releasing this key(together with all keys)?
			{
				if ((!(key_status[i]&2)) && releaseall) //Releasing a key by the standard instead of the user?
				{
					if ((i==lctrlindex) || (i==laltindex) || (i==lshiftindex) || (i==lwinindex) || (i==rctrlindex) || (i==raltindex) || (i==rshiftindex) || (i==rwinindex)) continue; //Ignore ctrl/alt/shift!
				}
				if (releaseall && (!((key_status[i]&3)==3))) //Part of release all and not releasing ourself?
				{
					key_status[i] |= 4; //Ignore any more input for now until we're released!
					continue; //Don't release us: we're only flagging to be blocked, not released!
				}
				#ifdef __DEBUG_INPUT
				if (EMU_keyboard_handler_idtoname(i,releasename)) //Name found?
				{
					dolog("input","Emulated key release: %s",releasename); //Log our release input!
				}
				#endif
				if (EMU_keyboard_handler((byte)i,0,((key_status[lctrlindex]|key_status[rctrlindex])?1:0),((key_status[laltindex]|key_status[raltindex])?1:0),((key_status[lshiftindex]?1:0)|(key_status[rshiftindex]?2:0)))) //Fired the handler for releasing!
				{
					--keys_pressed; //A key has been released!
					--keys_arepressed; //A key has been released, so decrease the counter!
					keys_ispressed[i] = 0; //We're not pressed anymore!
					key_status[i] = 0; //We're released, so clear our status!
					keyboard_step = 0; //Typematic repeat stops when any key is released!
					last_key_pressed_time_saved = -1; //No key pressed last time, so start anew!
					if (!releaseall) //One key release blocks all keys until repressed!
					{
						releaseall = 1; //Release all other keys!
						goto releaseallkeys; //Release all keys!
					}
				}
			}
		}
	} while (++i < (int)NUMITEMS(key_status)); //Process all keys available!
}

void releaseKeysReleased()
{
	int_64 i;
	if (keys_arepressed) //Are there actually keys pressed to be released?
	{
		for (i=0;i<key_pressed_counter;i++)
			releaseKeyReleased(i); //Release this time's key if applicable!
	}
}

byte keys_active = 0;
byte repeatflag = 0; //Repeat flag, set when the timeout has expired!

void tickPressedKey(uint_64 keytime)
{
	int i;
	if (keyboard_step) //Typematic key hold or new key during typematic? We're a delay in progress(repeat flag means that the delay has expired)!
	{
		int last_key_pressed = -1; //Last key pressed!
		uint_64 last_key_pressed_time = 0; //Last key pressed time!

		//Now, take the last pressed key, any press it!
		for (i = 0;i < (int)NUMITEMS(key_status);i++) //Process all keys pressed!
		{
			if ((key_status[i]&5)==1) //Pressed and not withheld from input?
			{
				if (((int_64)key_pressed_time[i] > (int_64)last_key_pressed_time) || (last_key_pressed == -1)) //Pressed later or first one to check?
				{
					last_key_pressed = i; //This is the last key pressed!
					last_key_pressed_time = key_pressed_time[i]; //The last key pressed time!
					if ((i == lctrlindex) || (i == laltindex) || (i == lshiftindex) || (i == lwinindex) || (i == rctrlindex) || (i == raltindex) || (i == rshiftindex) || (i == rwinindex)) //Ctrl/alt/shift have priority!
					{
						#ifdef __DEBUG_INPUT
						if (EMU_keyboard_handler_idtoname(last_key_pressed, releasename)) //Name found?
						{
							dolog("input", "Emulated key press (typematic ignored): %s", releasename); //Log our release input!
						}
						#endif
						goto handlectrlaltfirst; //Ignore ctrl/alt/shift!
					}
				}
			}
		}

		handlectrlaltfirst:
		if ((last_key_pressed != -1) && (last_key_pressed_time==keytime)) //Still gotten a last key pressed and acted upon?
		{
			if (last_key_pressed_time_saved!=last_key_pressed_time) //Different key pressed now?
			{
				last_key_pressed_time_saved = last_key_pressed_time; //Save the new value!
				keyboard_step = 0; //Start the delay again!
				repeatflag = 1; //Enable repeat: we're the first new key to check(restarting the delay)!
			}
			if ((i==lctrlindex) || (i==laltindex) || (i==lshiftindex) || (i==lwinindex) || (i==rctrlindex) || (i==raltindex) || (i==rshiftindex) || (i==rwinindex)) key_status[last_key_pressed] |= 4; //Ignore ctrl/alt/shift after it's pressed until it's released! We're not typematic!
			keys_active = 1; //We're active!
			#ifdef __DEBUG_INPUT
			if (EMU_keyboard_handler_idtoname(last_key_pressed,releasename)) //Name found?
			{
				dolog("input","Emulated key press (typematic): %s",releasename); //Log our release input!
			}
			#endif
			if (repeatflag) //Are we to press a repeated key, or pressing a new key?
			{
				if (EMU_keyboard_handler(last_key_pressed, 1|(keyboard_step?0:2),((key_status[lctrlindex]|key_status[rctrlindex])?1:0),((key_status[laltindex]|key_status[raltindex])?1:0),((key_status[lshiftindex]?1:0)|(key_status[rshiftindex]?2:0)))) //Fired the handler for pressing(repeating when the same as last time)!
				{
					if (!keys_ispressed[last_key_pressed]) ++keys_arepressed; //A new key has been pressed!
					keys_ispressed[last_key_pressed] = 1; //We're pressed!
					calculateKeyboardStep(); //Calculate the step for pressed keys, at the active step rate!
				}
			}
		}
	}
	else //Not repeating the last key (non-typematic)?
	{
		for (i = 0;i < (int)NUMITEMS(key_status);i++) //Process all keys needed!
		{
			if ((key_status[i]&1) && (key_pressed_time[i]==keytime)) //Pressed and acted upon?
			{
				keys_active = 1; //We're active!
				if (key_status[i]&4) continue; //Blocked from repeating until repressed?
				#ifdef __DEBUG_INPUT
				if (EMU_keyboard_handler_idtoname(i,releasename)) //Name found?
				{
					dolog("input","Emulated key press (non-typematic): %s",releasename); //Log our release input!
				}
				#endif
				if (EMU_keyboard_handler(i, 1,((key_status[lctrlindex]|key_status[rctrlindex])?1:0),((key_status[laltindex]|key_status[raltindex])?1:0),((key_status[lshiftindex]?1:0)|(key_status[rshiftindex]?2:0)))) //Fired the handler for pressing!
				{
					if (!keys_ispressed[i]) ++keys_arepressed; //A new key has been pressed!
					keys_ispressed[i] = 1; //We're pressed!
					if ((i==lctrlindex) || (i==laltindex) || (i==lshiftindex) || (i==lwinindex) || (i==rctrlindex) || (i==raltindex) || (i==rshiftindex) || (i==rwinindex)) key_status[i] |= 4; //Ignore ctrl/alt/shift after it's pressed until it's released! We're not typematic!
					last_key_pressed_time_saved = key_pressed_time[i]; //Save the last time we're pressed as the current time!
					calculateKeyboardStep(); //Calculate the step for pressed keys!
				}
			}
		}
	}
}

void tickPressedKeys() //Tick any keys needed to be pressed!
{
	uint_64 i;
	keys_active = 0; //Initialise keys active!
	for (i=0;i<(uint_64)key_pressed_counter;++i) //Tick all pressed keys in order of time!
		tickPressedKey(i); //Tick the timed key!
}

extern byte EMU_RUNNING; //Are we running?

void tickPendingKeys(DOUBLE timepassed) //Handle all pending keys from our emulation! Updating every 1/1000th second!
{
	if (EMU_RUNNING!=1) return; //Only process pending keys when running! 
	keyboard_time += timepassed; //Add the ammount of nanoseconds passed!

	//Release keys as fast as possible!
	releaseKeysReleased(); //Release any unpressed keys!

	if (keyboard_time >= KEYBOARD_CHECKTIME) //1ms passed or more?
	{
		keyboard_time -= KEYBOARD_CHECKTIME; //Rest the time passed, allow overflow!
		
		if (++pressedkeytimer > keyboard_step) //Timer expired? Tick pressed keys, when required!
		{
			pressedkeytimer = 0; //Reset the timer!
			repeatflag = 1; //Set the repeat flag: we're to handle as a repeat, when used!
		}
		if (keys_pressed) //Gotten any keys pressed to process?
		{
			tickPressedKeys(); //Tick any pressed keys!
		}
		repeatflag = 0; //Clear the repeat flag!
	}
}

extern char keys_names[104][11]; //Keys names!
void ReleaseKeys() //Force release all normal keys currently pressed!
{
	int i;
	for (i=0;i<(int)NUMITEMS(keys_names);i++) //Process all keys!
	{
		if (key_status[i]&1) //We're pressed?
		{
			onKeyRelease(keys_names[i]); //Release the key!
		}
	}
}

void onKeySetChange() //PSP input: keyset change!
{
	ReleaseKeys(); //Release all keys when changing sets, except CTRL,ALT,SHIFT
	//Now, recheck input!
}

void initEMUKeyboard() //Initialise the keyboard support for emulating!
{
	keyboard_time = 0.0f; //Initialise our time!
	memset(&key_status,0,sizeof(key_status)); //Reset our full status to unpressed!
	memset(&releasename,0,sizeof(releasename)); //Initialise our text for detecting Ctrl/Alt/Shift!
	//Save ctrl/alt/shift codes for fast lookup!
	lctrlindex = EMU_keyboard_handler_nametoid("lctrl");
	laltindex = EMU_keyboard_handler_nametoid("lalt");
	lshiftindex = EMU_keyboard_handler_nametoid("lshift");
	lwinindex = EMU_keyboard_handler_nametoid("lwin");
	rctrlindex = EMU_keyboard_handler_nametoid("rctrl");
	raltindex = EMU_keyboard_handler_nametoid("ralt");
	rshiftindex = EMU_keyboard_handler_nametoid("rshift");
	rwinindex = EMU_keyboard_handler_nametoid("rwin");
}

void cleanEMUKeyboard()
{
}
