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
#include "headers/hardware/ports.h" //Port support!

/*

Stuff for generating BIOS beeps.

*/

//Long BIOS beep
#define BIOSLONGBEEP_DIVISOR 1280
#define BIOSLONGBEEP_TIME 112
#define BIOSLONGBEEP_SEC 1.75
#define BIOSLONGSLEEP_DIVISOR 49715
#define BIOSLONGSLEEP_SEC 2/3
//Short BIOS beep
#define BIOSSHORTBEEP_DIVISOR 1208
#define BIOSSHORTBEEP_TIME 18
//Below also for after last beep
#define BIOSSHORTBEEP_SEC 9/32
#define BIOSSHORTSLEEP_DIVISOR 33144
#define BIOSSHORTSLEEP_SEC 0.5

/*

Speaker I/O!

*/


void setSpeaker(word frequency)
{
	byte tmp;
	PORT_OUT_B(0x43,0xB6); //Set up channel 2!
	PORT_OUT_B(0x42,(frequency&0xFF)); //Low!
	PORT_OUT_B(0x42,((frequency&0xFF00)>>8)); //High!
	//Play the sound using the PC speaker!
	tmp = PORT_IN_B(0x61);
	if (tmp!=(tmp|3)) //Disabled?
	{
		PORT_OUT_B(0x61,tmp|3); //Enable the speaker!
	}
}

/*

Turn speaker off!

*/

void speakerOff()
{
	byte tmp;
	tmp = (PORT_IN_B(0x61)&0xFC);
	PORT_OUT_B(0x61,tmp); //Disable the speaker!
}

/*

Emulate speaker out!

*/

void speakerOut(word frequency)
{
	if (frequency>0) //Enabled?
	{
		word div;
		div = (word)(1193180 / frequency);
		setSpeaker(div); //Set the speaker!
	}
	else
	{
		speakerOff(); //Speaker off!
	}
}

/*

Sound Test routine!

*/

word musical[10] = {1,1000,2,1000,3,1000,4,1000,5,1000}; //Size: 2*notes even indexes: notes, odd: length in ms

void domusical()
{
	int i;
	for (i=0;i<(int)NUMITEMS(musical);i+=2)
	{
		speakerOut(musical[i]); //Sound!
		delay((uint_32)(musical[i+1]*1000)); //Wait for the period!
		speakerOff(); //Turn speaker off again!
	}
}



/*

BIOS Boot Beep!

*/

void doBIOSBeep()
{
	speakerOut(BIOSSHORTBEEP_DIVISOR); //Speaker with frequency!
	delay((uint_32)(1000000*BIOSSHORTBEEP_SEC)); //Wait a bit!
	speakerOut(0); //Disable speaker!
}