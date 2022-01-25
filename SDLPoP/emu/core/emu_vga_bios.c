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
#include "headers/cpu/cpu.h" //CPU support!
#include "headers/interrupts/interrupt10.h" //Interrupt 10h support!
#include "headers/cpu/easyregs.h" //Easy register support!


/*

Basic Screen I/O!

*/

void bios_gotoxy(int x, int y)
{
REG_AH = 2; //Set cursor position!
REG_BH = 0; //Our page!
REG_DH = y; //Our row!
REG_DL = x; //Our column!
BIOS_int10(); //Goto row!
}

void bios_displaypage() //Select the display page!
{
REG_AH = 5; //Set display page!
REG_AL = 0; //Our page!
BIOS_int10(); //Switch page!
}

void updatechar(byte attribute, byte character)
{
	REG_AH = 0x9; //Update cursor location character/attribute pair!
	REG_AL = character; //Character!
	REG_BH = 0; //Page: always 0!
	REG_BL = attribute; //The attribute to use!
	REG_CX = 1; //One time!
	BIOS_int10(); //Output the character at the current location!
}

void printmsg(byte attribute, char *text, ...) //Print a message at page #0!
{
	char msg[256];
	cleardata(&msg[0],sizeof(msg)); //Init!

	va_list args; //Going to contain the list!
	va_start (args, text); //Start list!
	vsnprintf (&msg[0],sizeof(msg), text, args); //Compile list!
	va_end (args); //Destroy list, we're done with it!

	byte length = safe_strlen(msg,sizeof(msg)); //Check the length!
	int i;
	if (CPU[activeCPU].registers) //Gotten a CPU to work with?
	{
		for (i=0; i<length; i++) //Process text!
		{
			switch (msg[i])
			{
			case 0x7:
			case 0x8:
			case 0x9:
			case 0xA:
			case 0xB:
			case 0xD:
				//We're a control character: process as a control character (don't update video output)!
				REG_AH = 0xE; //Teletype ouput!
				REG_AL = msg[i]; //Character, we don't want to change this!
				REG_BH = 0; //Page: always 0!
				BIOS_int10(); //Output!
				break;
			default: //Default character to output?
				updatechar(attribute, msg[i]); //Update the current character attribute: we're output!
				REG_AH = 0xE; //Teletype ouput!
				REG_AL = msg[i]; //Character, we don't want to change this!
				REG_BH = 0;//Page: always 0!
				BIOS_int10(); //Output!
				break;
			}
		}
	}
}

void printCRLF()
{
	REG_AH = 0xE; //Teletype ouput!
	REG_AL = 0xD;//Character!
	REG_BH = 0;//Page
	BIOS_int10(); //Output!

	REG_AH = 0xE; //Teletype ouput!
	REG_AL = 0xA;//Character!
	REG_BH = 0;//Page
	BIOS_int10(); //Output!
}

void BIOS_enableCursor(byte enabled)
{
	return; //Ignore!
	REG_AH = 0x03; //Get old cursor position and size!
	BIOS_int10(); //Get data!
	REG_AH = 0x01; //Set cursor shape!
	if (enabled) //Enabled?
	{
		REG_CH &= ~0x20; //Enable cursor!
	}
	else //Disabled?
	{
		REG_CH |= 0x20; //Disable cursor!
	}
	BIOS_int10(); //Set data!
}