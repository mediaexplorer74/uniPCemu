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
#include "headers/hardware/ports.h" //Port I/O support!
#include "headers/support/fifobuffer.h" //FIFO buffer support for detecting Digital Mode sequence!
#include "headers/hardware/joystick.h" //Our own type definitions!

//Time until we time out!
//R=(t-24.2)/0.011, where t is in microseconds(us). Our timing is in nanoseconds(1000us). r=0-100. R(max)=2200 ohm.
//Thus, t(microseconds)=(R(ohms)*0.011)+24.2 microseconds.
#ifdef IS_LONGDOUBLE
#define OHMS (120000.0L/2.0L)
#define POS2OHM(position) ((((DOUBLE)(position+1))/65535.0L)*OHMS)
#define CALCTIMEOUT(position) (((24.2L+(POS2OHM(position)*0.011L)))*1000.0L)
#else
#define OHMS (120000.0/2.0)
#define POS2OHM(position) ((((DOUBLE)(position+1))/65535.0)*OHMS)
#define CALCTIMEOUT(position) (((24.2+(POS2OHM(position)*0.011)))*1000.0)
#endif

//Delay after initialization(linux says 200ms)
#define PACKETMODE_TIMEOUT 200000000.0

//The size of the sequence to be supported!
#define MAXSEQUENCESIZE 10

struct
{
	byte model; //What joystick type are we emulating? 0=Normal PC-compatible input, >0=Digital joystick(number specified which one) in digital mode!
	byte enabled[2]; //Is this joystick enabled for emulation?
	byte buttons[2]; //Two button status for two joysticks!
	sword Joystick_X[2]; //X location for two joysticks!
	sword Joystick_Y[2]; //Y location for two joysticks!
	DOUBLE timeoutx[2]; //Keep line high while set(based on Joystick_X[0-1] when triggered)
	DOUBLE timeouty[2]; //Keep line high while set(based on Joystick_Y[0-1] when triggered)
	byte timeout; //Timeout status of the four data lines(logic 0/1)! 1 for timed out(default state), 0 when timing!

	//Digital mode timing support!
	FIFOBUFFER *digitalmodesequence; //Digital sequence to check when pulsing when model=0.
	DOUBLE lasttiming; //Last write digital timing counter!
	DOUBLE digitaltiming; //Digital timing accumulated!
	DOUBLE digitaltiming_step; //The frequency of the digital timing(in ns, based on e.g. 100Hz signal).
	byte buttons2[6]; //Extended 6 buttons(digital joysticks)!
	byte hats[4]; //Extended 4 hats(digital joysticks)!
	sword axis[6]; //Extended 3 x/y axis(digital joysticks)!
	uint_64 packet; //A packet to be read for digital joysticks, based on previous data! The Wingman Extreme Digital's Buttons field is reversed!
	uint_64 bitmaskhigh; //High data bit mask!
	uint_64 bitmasklow; //Low data bit mask!
	byte extensionModel; //What model are we?
	word portmask;
	DOUBLE resettimer; //Timer that when times out resets the digital mode back to analog mode!
} JOYSTICK;

//WingMan Digital sequence from Linux: https://github.com/torvalds/linux/blob/master/drivers/input/joystick/adi.c
//More information about it's data packets: http://atrey.karlin.mff.cuni.cz/~vojtech/joystick/specs.txt
int DigitalModeSequence[9] = { 4, 2, 3, 10, 6, 11, 7, 9, 11 }; //WingMan digital mode sequence in milliseconds(milliseconds since last pulse)! Make negative values positive for correct handling!

void setJoystickModel(byte model)
{
	JOYSTICK.extensionModel = (model==MODEL_LOGITECH_WINGMAN_EXTREME_DIGITAL)?MODEL_LOGITECH_WINGMAN_EXTREME_DIGITAL:0; //Set the model to use!
}

void enableJoystick(byte joystick, byte enabled)
{
	if (joystick&0xFE) return; //Only two joysticks are supported!
	JOYSTICK.enabled[joystick] = enabled?((enabled<4)?enabled:1):0; //Enable this joystick(with special status disabling the analog part being status 2, only x analog being status 3)?
}

//This one is used for normal joystick(joystick A only), Gravis Gamepad and Gravis Analog Pro(joystick A+B combined).
void setJoystick(byte joystick, byte button1, byte button2, sword analog_x, sword analog_y) //Input from theuser!
{
	if (joystick&0xFE) return; //Only two joysticks are supported!
	//Set the buttons of the joystick!
	JOYSTICK.buttons[joystick] = (button2?0x0:0x2)|(button1?0x0:0x1); //Button 1&2, not pressed!
	JOYSTICK.Joystick_X[joystick] = analog_x; //Joystick x axis!
	JOYSTICK.Joystick_Y[joystick] = analog_y; //Joystick y axis!
}

//Below is used with extended digital joysticks!
void setJoystick_other(byte button1, byte button2, byte button3, byte button4, byte button5, byte button6, byte hatleft, byte hatright, byte hatup, byte hatdown, sword analog_x, sword analog_y, sword analog2_x, sword analog2_y) //Input from theuser!
{
	//Set the buttons of the joystick in their own buffers(seperated)!
	JOYSTICK.buttons2[0] = button1?1:0;
	JOYSTICK.buttons2[1] = button2?1:0;
	JOYSTICK.buttons2[2] = button3?1:0;
	JOYSTICK.buttons2[3] = button4?1:0;
	JOYSTICK.buttons2[4] = button5?1:0; //Unsupported!
	JOYSTICK.buttons2[5] = button6?1:0; //Unsupported!
	JOYSTICK.hats[0] = hatleft?1:0;
	JOYSTICK.hats[1] = hatright?1:0;
	JOYSTICK.hats[2] = hatup?1:0;
	JOYSTICK.hats[3] = hatdown?1:0;
	//Buttons are data streamed instead, so store them seperately!
	JOYSTICK.Joystick_X[0] = analog_x; //Joystick x axis for compatibility!
	JOYSTICK.Joystick_Y[0] = analog_y; //Joystick y axis for compatibility!
	JOYSTICK.Joystick_X[1] = analog2_x; //Joystick x axis!
	JOYSTICK.Joystick_Y[1] = analog2_y; //Joystick y axis!
}

void updateJoystick(DOUBLE timepassed)
{
	//Joystick timer!
	//Add to the digital timing sequence for detecting the activation sequence!
	JOYSTICK.digitaltiming += timepassed; //Apply timing directly to the digital timing for activation sequence detection!

	//Always update the analog outputs!
	if (JOYSTICK.timeout) //Timing?
	{
		if (JOYSTICK.enabled[0]) //Joystick A enabled?
		{
			if ((JOYSTICK.timeout & 1) && (JOYSTICK.enabled[0] & 1)) //AX timing?
			{
				JOYSTICK.timeoutx[0] -= timepassed; //Add the time to what's left!
				if (JOYSTICK.timeoutx[0] <= 0.0) //Finished timing?
				{
					JOYSTICK.timeout &= ~1; //Finished timing, go logic 1(digital 0)!
				}
			}
			if ((JOYSTICK.timeout & 2) && (JOYSTICK.enabled[0] == 1)) //AY timing?
			{
				JOYSTICK.timeouty[0] -= timepassed; //Add the time to what's left!
				if (JOYSTICK.timeouty[0] <= 0.0) //Finished timing?
				{
					JOYSTICK.timeout &= ~2; //Finished timing, go logic 1(digital 0)!
				}
			}
		}
		if (JOYSTICK.enabled[1]) //Joystick B enabled?
		{
			if ((JOYSTICK.timeout & 4) && (JOYSTICK.enabled[1] & 1)) //BX timing?
			{
				JOYSTICK.timeoutx[1] -= timepassed; //Add the time to what's left!
				if (JOYSTICK.timeoutx[1] <= 0.0) //Finished timing?
				{
					JOYSTICK.timeout &= ~4; //Finished timing, go logic 1(digital 0)!
				}
			}
			if ((JOYSTICK.timeout & 8) && (JOYSTICK.enabled[1] == 1)) //BY timing?
			{
				JOYSTICK.timeouty[1] -= timepassed; //Add the time to what's left!
				if (JOYSTICK.timeouty[1] <= 0.0) //Finished timing?
				{
					JOYSTICK.timeout &= ~8; //Finished timing, go logic 1(digital 0)!
				}
			}
		}
	}

	if (JOYSTICK.model==0) //Analog model? Use compatibiliy analog emulation!
	{
	updateAnalogMode:
		//Not in digital mode, so behave!
		JOYSTICK.resettimer = (DOUBLE)0; //Stop the timer!
		JOYSTICK.model = 0; //Return to analog mode!
		fifobuffer_clear(JOYSTICK.digitalmodesequence); //No digital mode sequence is known anymore!
	}
	else //Digital mode? Use packets according to the emulated device!
	{
		if (likely(JOYSTICK.resettimer)) //Gotten a reset timer?
		{
			JOYSTICK.resettimer -= timepassed; //Tick the reset timer!
			if (unlikely(JOYSTICK.resettimer <= (DOUBLE)0.0)) //Timeout?
			{
				goto updateAnalogMode; //Handle analog mode!
			}
		}

		//Set the current packet, based on the data and timing!
		switch (JOYSTICK.model) //What model?
		{
		default: //Unknown?
			goto updateAnalogMode; //Unknowns are reset!
		case MODEL_LOGITECH_WINGMAN_EXTREME_DIGITAL: //Logitech WingMan Extreme Digital?
			if (JOYSTICK.digitaltiming>=JOYSTICK.digitaltiming_step && JOYSTICK.bitmasklow) //To step the counter?
			{
				for (;JOYSTICK.digitaltiming>=JOYSTICK.digitaltiming_step;) //Stepping?
				{
					JOYSTICK.digitaltiming -= JOYSTICK.digitaltiming_step; //We're stepping!
					//Give the 1 and 0 bits on the two channels! Buttons 2&3 carry the lower half, Buttons 1&2 carry the upper half!
					//Lower half!
					if (JOYSTICK.packet&JOYSTICK.bitmasklow) //Bit is 1?
					{
						JOYSTICK.buttons[1] ^= 2; //The upper bit changes state!
					}
					else //Bit is 0?
					{
						JOYSTICK.buttons[1] ^= 1; //The lower bit changes state!
					}
					//Upper half!
					if (JOYSTICK.packet&JOYSTICK.bitmaskhigh) //Bit is 1?
					{
						JOYSTICK.buttons[0] ^= 2; //The upper bit changes state!
					}
					else //Bit is 0?
					{
						JOYSTICK.buttons[0] ^= 1; //The lower bit changes state!
					}
					JOYSTICK.bitmaskhigh >>= 1; //Check the next bit, if any!
					JOYSTICK.bitmasklow >>= 1; //Check the next bit and update status, if any!
				}
			}
			break;
		}
	}
}

byte joystick_readIO(word port, byte *result)
{
	INLINEREGISTER byte temp;
	if ((port&JOYSTICK.portmask) != 0x200) return 0; //Not our port?
	//Read joystick position and status?
	//bits 8-7 are joystick B buttons 2/1, Bits 6-5 are joystick A buttons 2/1, bit 4-3 are joystick B Y-X timeout timing, bits 1-0 are joystick A Y-X timeout timing.
	temp = 0xFF; //Init the result!
	if (JOYSTICK.enabled[1]) //Joystick B enabled?
	{
		if (JOYSTICK.extensionModel && (!JOYSTICK.model)) //Digital joystick not in digital mode? Compatibility mode for unsupporting software!
		{
			JOYSTICK.buttons[1] = (JOYSTICK.buttons2[3] ? 0x0 : 0x2) | (JOYSTICK.buttons2[2] ? 0x0 : 0x1); //Button 3&4, as pressed!
		}
		temp &= 0x33|(((JOYSTICK.buttons[1]<<6)|JOYSTICK.timeout)&0xCC); //Clear joystick B bits when applied!
	}
	if (JOYSTICK.enabled[0]) //Joystick A enabled?
	{
		if (JOYSTICK.extensionModel && (!JOYSTICK.model)) //Digital joystick not in digital mode? Compatibility mode for unsupporting software!
		{
			JOYSTICK.buttons[0] = (JOYSTICK.buttons2[1] ? 0x0 : 0x2) | (JOYSTICK.buttons2[0] ? 0x0 : 0x1); //Button 1&2, as pressed!
		}
		temp &= 0xCC|(((JOYSTICK.buttons[0]<<4)|JOYSTICK.timeout)&0x33); //Set joystick A bits when applied!
	}
	*result = temp; //Give the result!
	return 1; //OK!
}

byte joystick_writeIO(word port, byte value)
{
	#include "headers/packed.h"
	union
	{
		sword axis;
		word data;
	} axisconversion;
	#include "headers/endpacked.h"
	byte entry;
	byte sequencepos;
	if ((port&JOYSTICK.portmask)!=0x200) return 0; //Not our port?
	//Fire joystick four one-shots and related handling?

	//Set timeoutx and timeouty based on the relative status of Joystick_X and Joystick_Y to fully left/top!
	//First joystick timeout!
	if (JOYSTICK.enabled[1]) //Joystick B enabled?
	{
		if (JOYSTICK.enabled[1]&1) //X axis used?
		{
			JOYSTICK.timeoutx[1] = CALCTIMEOUT((int_32)JOYSTICK.Joystick_X[1]-SHRT_MIN);
		}
		if (JOYSTICK.enabled[1]==1) //Y axis used?
		{
			JOYSTICK.timeouty[1] = CALCTIMEOUT((int_32)JOYSTICK.Joystick_Y[1]-SHRT_MIN);
		}
	}
	//Second joystick timeout!
	if (JOYSTICK.enabled[0]) //Joystick A enabled?
	{
		if (JOYSTICK.enabled[0]&1) //X axis used?
		{
			JOYSTICK.timeoutx[0] = CALCTIMEOUT((int_32)JOYSTICK.Joystick_X[0]-SHRT_MIN);
		}
		if (JOYSTICK.enabled[0]==1) //Y axis used?
		{
			JOYSTICK.timeouty[0] = CALCTIMEOUT((int_32)JOYSTICK.Joystick_Y[0]-SHRT_MIN);
		}
	}

	//Check for activation of the WingMan digital mode!
	if (JOYSTICK.model==0) //Analog mode?
	{
	handleAnalogMode:
		switch (JOYSTICK.extensionModel) //What extension model?
		{
		default: //Unknown?
			break; //Don't handle model extension!
		case MODEL_LOGITECH_WINGMAN_EXTREME_DIGITAL: //Logitech WingMan Extreme Digital supports digital mode!
			if (fifobuffer_freesize(JOYSTICK.digitalmodesequence)==0) //No space left?
			{
				readfifobuffer(JOYSTICK.digitalmodesequence,&entry); //Discard an entry!
			}
			if ((JOYSTICK.digitaltiming/1000000.0f)>=256.0) //Timeout?
			{
				writefifobuffer(JOYSTICK.digitalmodesequence,0xFF); //Simply time out!
			}
			else //Within range?
			{
				writefifobuffer(JOYSTICK.digitalmodesequence,(byte)(JOYSTICK.digitaltiming/1000000.0f)); //The time since last pulse!
			}
			JOYSTICK.digitaltiming = 0.0f; //Reset the timing?
			fifobuffer_save(JOYSTICK.digitalmodesequence); //Save the position for checking!
			for (;fifobuffer_freesize(JOYSTICK.digitalmodesequence)<(MAXSEQUENCESIZE-NUMITEMS(DigitalModeSequence));) //We need the buffer items from the point of the sequence most recently given!
			{
				readfifobuffer(JOYSTICK.digitalmodesequence,&entry); //Discard an entry!					
			}
			for (sequencepos=0;sequencepos<NUMITEMS(DigitalModeSequence);) //Check for the sequence!
			{
				if (!readfifobuffer(JOYSTICK.digitalmodesequence,&entry)) break; //Not enough entries yet?
				if (entry!=DigitalModeSequence[sequencepos]) break; //Only count when the sequence is matched!
				++sequencepos; //Counted!
			}
			fifobuffer_restore(JOYSTICK.digitalmodesequence); //We're finished, undo any reads!
			if (sequencepos==NUMITEMS(DigitalModeSequence)) //Sequence pattern matched?
			{
				JOYSTICK.model = JOYSTICK.extensionModel; //Enter it's digital mode!
				#ifdef IS_LONGDOUBLE
				JOYSTICK.digitaltiming_step = 1000000000.0L/(100000.0L*2.0L); //We're a signal going 1-0 or 0-1 at 100kHz!
				#else
				JOYSTICK.digitaltiming_step = 1000000000.0/(100000.0*2.0); //We're a signal going 1-0 or 0-1 at 100kHz!
				#endif
				JOYSTICK.resettimer = PACKETMODE_TIMEOUT; //How long to take until reset without pulse!

				/*
						
				Are we supposed to send some kind of special packet now?

				*/
			}
			break;
		}
	}
	else //Give a packet in the current device format!
	{
		switch (JOYSTICK.model) //What model are we?
		{		
		default: //Unknown?
			JOYSTICK.model = 0; //Return to analog mode!
			fifobuffer_clear(JOYSTICK.digitalmodesequence); //No digital mode sequence is known anymore!
			goto handleAnalogMode; //Handle as analog mode!
			break; //Don't handle packet modes for analog mode!
		case 1: //Logitech WingMan Extreme Digital?
			JOYSTICK.bitmaskhigh = (1ULL<<41); //Reset to the high 21st bit!
			JOYSTICK.bitmasklow = (1<<20); //Reset to the low 21st bit! We're starting to send a packet, because we're not 0! Once we shift below bit 0, becoming 0, we're finished!
			JOYSTICK.digitaltiming = 0.0f; //We're starting to send a packet, reset our timing!
			//Formulate a packet from the current data!
			/*
				Bits     Meaning
				0 ..  3 - Hat			(4 bits)
				4 ..  9 - Buttons		(6 bits)
			10 .. 17 - Axis 2 (Twist)	(8 bits)
			18 .. 25 - Axis 1 (Y)		(8 bits)
			26 .. 33 - Axis 0 (X)		(8 bits)
			34 .. 41 - 0x00			(8 bits)
			*/
			JOYSTICK.packet = 0; //Initialize the packet!
			//Now, fill the packet with our current information!
			//First, hats!
			JOYSTICK.packet |= JOYSTICK.hats[0]|(JOYSTICK.hats[1]<<1)|(JOYSTICK.hats[2]<<2)|(JOYSTICK.hats[3]<<3);
			//Then buttons(reversed)!
			JOYSTICK.packet |= JOYSTICK.buttons2[5]|(JOYSTICK.buttons2[4]<<1)|(JOYSTICK.buttons2[3]<<2)|(JOYSTICK.buttons2[2]<<3)|(JOYSTICK.buttons2[1]<<4)|(JOYSTICK.buttons2[0]<<5);
			//Axis2(Twist) 8-bits converted!
			axisconversion.axis = JOYSTICK.Joystick_X[1]; //Twist!
			JOYSTICK.packet |= ((((uint_64)axisconversion.data>>8)&0xFF)<<10); //Twist converted!
			//Axis 1(Y) 8-bits converted!
			axisconversion.axis = JOYSTICK.Joystick_Y[0]; //Y!
			JOYSTICK.packet |= ((((uint_64)axisconversion.data>>8)&0xFF)<<18); //Twist converted!
			//Axis 0(X) 8-bits converted!
			axisconversion.axis = JOYSTICK.Joystick_X[0]; //Y!
			JOYSTICK.packet |= ((((uint_64)axisconversion.data>>8)&0xFF)<<26); //Twist converted!
			//The upper bits are 0x00!
			//According to Linux, button state starts at zero when starting a packet!
			JOYSTICK.buttons[1] &= ~3;
			JOYSTICK.buttons[0] &= ~3;
			break;
		}
		JOYSTICK.resettimer = PACKETMODE_TIMEOUT; //How long to take until reset without pulse!
	}
	JOYSTICK.timeout = 0xF; //Start the timeout on all channels, regardless if they're enabled. Multivibrator output goes to logic 0.

	return 1;
}

extern byte is_XT; //Are we emulating a XT architecture?

void joystickInit()
{
	register_PORTIN(&joystick_readIO); //Register our handler!
	register_PORTOUT(&joystick_writeIO); //Register our handler!
	JOYSTICK.buttons[0] = JOYSTICK.buttons[1] = 3; //Not pressed!
	JOYSTICK.timeoutx[0] = JOYSTICK.timeouty[0] = JOYSTICK.timeoutx[1] = JOYSTICK.timeouty[1] = 0.0; //Init timeout to nothing!
	JOYSTICK.timeout = 0x0; //Default: all lines timed out!
	JOYSTICK.model = 0; //Default: analog model(compatibility)!
	JOYSTICK.digitaltiming = 0.0f; //Reset our digital detection(during analog mode)/step(during digital mode) timing!
	JOYSTICK.digitalmodesequence = allocfifobuffer(MAXSEQUENCESIZE,0); //We use a simple buffer with 10 8-bit entries, unlocked!
	JOYSTICK.lasttiming = 0.0f; //Last write digital timing delay!
	JOYSTICK.extensionModel = 0; //Default: no extension specified!
	JOYSTICK.portmask = is_XT?0xFFF0:0xFFF8; //XT:200-20F, AT+:200-207
}

void joystickDone()
{
	free_fifobuffer(&JOYSTICK.digitalmodesequence); //Release our buffer, we're finished with it!
}
