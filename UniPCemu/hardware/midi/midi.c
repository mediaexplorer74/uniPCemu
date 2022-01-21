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
#include "headers/hardware/ports.h" //Basic port compatibility!
#include "headers/hardware/midi/mididevice.h" //MIDI Device compatibility!
#include "headers/support/fifobuffer.h" //FIFOBUFFER support!
#include "headers/hardware/midi/midi.h" //Our own stuff!
#include "headers/hardware/pic.h" //Interrupt support!
#include "headers/header_dosboxmpu.h" //Dosbox MPU cleanup support!

//http://www.oktopus.hu/imgs/MANAGED/Hangtechnikai_tudastar/The_MIDI_Specification.pdf

//HW: MPU-401: http://www.piclist.com/techref/io/serial/midi/mpu.html
//Protocol: http://www.gweep.net/~prefect/eng/reference/protocol/midispec.html

//MIDI ports: 330-331 or 300-301(exception rather than rule)

//Log MIDI output?
//#define __MIDI_LOG

//ACK/NACK!
#define MPU_ACK 0xFE
#define MPU_NACK 0xFF

struct
{
	MIDICOMMAND current;
	byte bufferpos; //Position in the buffer for midi commands.
	int command; //What command are we processing? -1 for none.
	//Internal MIDI support!
	byte has_result; //Do we have a result?
	FIFOBUFFER *inbuffer;
	int MPU_command; //What command of a result!
} MIDIDEV; //Midi device!

void resetMPU() //Fully resets the MPU!
{
	fifobuffer_clear(MIDIDEV.inbuffer); //Clear the FIFO buffer!
}

/*

Basic input/ouput functionality!

*/

OPTINLINE void MIDI_writeStatus(byte data) //Write a status byte to the MIDI device!
{
	memset(&MIDIDEV.current,0,sizeof(MIDIDEV.current)); //Clear info on the current command!
	switch ((data>>4)&0xF) //What command?
	{
		case 0x8: case 0x9: case 0xA: case 0xB: case 0xC: case 0xD: case 0xE: //Normal commands?
			MIDIDEV.command = data; //Load the command!
			MIDIDEV.bufferpos = 0; //Init buffer position!
			break;
		case 0xF: //System realtime command?
			switch (data&0xF) //What command?
			{
				case 0x0: //SysEx?
				case 0x1: //MTC Quarter Frame Message?
				case 0x2: //Song Position Pointer?
				case 0x3: //Song Select?
					MIDIDEV.command = data; //Load the command to use!
					MIDIDEV.bufferpos = 0; //Initialise buffer pos for the command!
					break;
				case 0x6: //Tune Request?
					//Execute Tune Request!
					break;
				case 0x8: //MIDI Clock?
					//Execute MIDI Clock!
					MIDIDEVICE_addbuffer(0xF8,&MIDIDEV.current); //Add MIDI clock!
					break;
				case 0xA: //MIDI Start?
					//Execute MIDI Start!
					MIDIDEVICE_addbuffer(0xFA,&MIDIDEV.current); //Add MIDI Start!
					break;
				case 0xB: //MIDI Continue?
					//Execute MIDI Continue!
					MIDIDEVICE_addbuffer(0xFB,&MIDIDEV.current); //Add MIDI Continue!
					break;
				case 0xC: //MIDI Stop?
					//Execute MIDI Stop!
					MIDIDEVICE_addbuffer(0xFC,&MIDIDEV.current); //Add MIDI Stop!
					break;
				case 0xE: //Active Sense?
					//Execute Active Sense!
					MIDIDEVICE_addbuffer(0xFE,&MIDIDEV.current); //Add MIDI Active Sense!
					break;
				case 0xF: //Reset?
					//Execute Reset!
					MIDIDEV.command = -1;
					MIDIDEV.bufferpos = 0;
					memset(&MIDIDEV.current.buffer,0,sizeof(MIDIDEV.current.buffer));
					//We're reset!
					MIDIDEVICE_addbuffer(0xFF,&MIDIDEV.current); //Add MIDI reset!
					break;
				default: //Unknown?
					//Ignore the data: we're not supported yet!
					break;
			}
			break;
		default:
			break;
	}
}

OPTINLINE void MIDI_writeData(byte data) //Write a data byte to the MIDI device!
{
	switch ((MIDIDEV.command>>4)&0xF) //What command?
	{
		case 0x8: //Note Off?
			MIDIDEV.current.buffer[MIDIDEV.bufferpos++] = data; //Add to the buffer!
			if (MIDIDEV.bufferpos==2) //Done when not giving input anymore!
			{
				//Process Note Off!
				MIDIDEVICE_addbuffer(MIDIDEV.command,&MIDIDEV.current); //Add MIDI command!
				MIDIDEV.bufferpos = 0; //Reset buffer position for the next command!
			}
			break;
		case 0x9: //Note On?
			MIDIDEV.current.buffer[MIDIDEV.bufferpos++] = data; //Add to the buffer!
			if (MIDIDEV.bufferpos==2) //Done when not giving input anymore!
			{
				//Process Note On!
				MIDIDEVICE_addbuffer(MIDIDEV.command,&MIDIDEV.current); //Add MIDI command!
				MIDIDEV.bufferpos = 0; //Reset buffer position for the next command!
			}
			break;			
		case 0xA: //AfterTouch?
			MIDIDEV.current.buffer[MIDIDEV.bufferpos++] = data; //Add to the buffer!
			if (MIDIDEV.bufferpos==2) //Done when not giving input anymore!
			{
				//Process Aftertouch!
				MIDIDEVICE_addbuffer(MIDIDEV.command,&MIDIDEV.current); //Add MIDI command!
				MIDIDEV.bufferpos = 0; //Reset buffer position for the next command!
			}
			break;
		case 0xB: //Control change?
			MIDIDEV.current.buffer[MIDIDEV.bufferpos++] = data; //Add to the buffer!
			if (MIDIDEV.bufferpos==2) //Done when not giving input anymore!
			{
				//Process Control change!
				MIDIDEVICE_addbuffer(MIDIDEV.command,&MIDIDEV.current); //Add MIDI command!
				MIDIDEV.bufferpos = 0; //Reset buffer position for the next command!
			}
			break;
		case 0xC: //Program (patch) change?
			//Process Program change!
			MIDIDEV.current.buffer[0] = data; //Load data!
			MIDIDEVICE_addbuffer(MIDIDEV.command,&MIDIDEV.current); //Add MIDI command!
			break;
		case 0xD: //Channel pressure?
			//Process channel pressure!
			MIDIDEV.current.buffer[0] = data; //Load data!
			MIDIDEVICE_addbuffer(MIDIDEV.command,&MIDIDEV.current); //Add MIDI command!
			break;
		case 0xE: //Pitch Wheel?
			MIDIDEV.current.buffer[MIDIDEV.bufferpos++] = data; //Add to the buffer!
			if (MIDIDEV.bufferpos==2) //Done when not giving input anymore!
			{
				//Process Pitch Wheel!
				//Pitch = ((MIDIDEV.current.buffer[1]<<7)|MIDIDEV.current.buffer[0])
				MIDIDEVICE_addbuffer(MIDIDEV.command,&MIDIDEV.current); //Add MIDI command!
				MIDIDEV.bufferpos = 0; //Reset buffer position for the next command!
			}
			break;
		case 0xF: //System message?
			switch (MIDIDEV.command&0xF) //What kind of message?
			{
				case 0: //SysEx?
					if (data==0xF7) //End of SysEx message?
					{
						//Handle the given SysEx message?
						MIDIDEV.command = -1; //Done!
						return; //Abort processing!
					}
					//Don't do anything with the data yet!
					break;
				case 0x1: //MTC Quarter Frame Message?
					//Process the parameter!
					break;
				case 0x2: //Song Position Pointer?
					MIDIDEV.current.buffer[MIDIDEV.bufferpos++] = data; //Add to the buffer!
					if (MIDIDEV.bufferpos==2) //Done when not giving input anymore!
					{
						//Process Song Position Pointer!
						MIDIDEV.bufferpos = 0; //Reset buffer position for the next command!
					}
					break;
				case 0x3: //Song Select?
					//Execute song select with the data!
					break;
				default: //Unknown?
					break;
			}
			//Unknown, don't parse!
			break;
		default:
			break;
	}
}

byte MIDI_has_data() //Do we have data to be read?
{
	if (MIDIDEV.inbuffer) //Gotten a FIFO buffer?
	{
		byte temp;
		return peekfifobuffer(MIDIDEV.inbuffer,&temp)?1:0; //We're containing a result?
	}
	return 0; //We never have data to be read!
}

OPTINLINE byte MIDI_readData() //Read data from the MPU!
{
	if (MIDIDEV.inbuffer) //We're containing a FIFO buffer?
	{
		byte result;
		if (readfifobuffer(MIDIDEV.inbuffer,&result))
		{
			return result; //Give the read result!
		}
	}
	return 0; //Unimplemented yet: we never have anything from hardware to read!
}


//MPU MIDI support!
//MIDI ports: 330-331 or 300-301(exception rather than rule)

void MIDI_OUT(byte data)
{
	#ifdef __MIDI_LOG
	dolog("MIDI","MIDI OUT: %02X",data); //Log it!
	#endif
	MIDIDEVICE_tickActiveSense(); //Tick the Active Sense: we're sending MIDI Status or Data bytes!
	if (data&0x80)
	{
		MIDI_writeStatus(data);
	}
	else
	{
		MIDI_writeData(data);
	}
}

byte MIDI_IN()
{
	return MIDI_readData(); //Read data from the MPU!
}

byte MPU_ready = 0;

DOUBLE MPU_ticktiming = 0.0, MPU_ticktick = 0.0;
Handler MPUTickHandler = NULL;

byte initMPU(char *filename, byte use_direct_MIDI) //Initialise function!
{
	byte result;
	result = init_MIDIDEVICE(filename, use_direct_MIDI); //Initialise the MIDI device!
	MPU_ready = result; //Are we ready?
	if (result) //Valid MIDI device?
	{
		memset(&MIDIDEV, 0, sizeof(MIDIDEV)); //Clear the MIDI device!
		MIDIDEV.inbuffer = allocfifobuffer(100,1); //Alloc FIFO buffer of 100 bytes!
		MIDIDEV.command = -1; //Default: no command there!
		resetMPU(); //Reset the MPU!
		MPU401_Init(); //Init the dosbox handler for our MPU-401!
		MPUTickHandler = NULL; //Init!
		MPU_ticktiming = 0.0f; //Reset our timer!
		MPU_ticktick = 0; //Clear all timing running!
	}
	return result; //Are we loaded?
}

void doneMPU() //Finish function!
{
	if (MPU_ready) //Are we loaded?
	{
		MPU_ready = 0; //We're not loaded anymore!
		done_MIDIDEVICE(); //Finish the MIDI device!
		free_fifobuffer(&MIDIDEV.inbuffer); //Free the FIFO buffer!
		MPU401_Done(); //Finish our MPU-401 system: custom!
	}
}

extern byte is_XT; //Are we emulating a XT architecture?

void MPU401_Done() //Finish our MPU system! Custom by superfury1!
{
	PIC_RemoveEvents(NULL); //Remove all events!
	lowerirq(is_XT?MPU_IRQ_XT:MPU_IRQ_AT); //Remove the irq if it's still there!
	acnowledgeIRQrequest(is_XT?MPU_IRQ_XT:MPU_IRQ_AT); //Remove us fully!
}

void updateMPUTimer(DOUBLE timepassed)
{
	if (MPU_ticktick) //Are we timing anything?
	{
		MPU_ticktiming += timepassed; //Tick us!
		if ((MPU_ticktiming>=MPU_ticktick) && MPU_ticktick)
		{
			for (;MPU_ticktiming>=MPU_ticktick;) //Still left?
			{
				MPU_ticktiming -= MPU_ticktick; //Tick us!
				if (MPUTickHandler) MPUTickHandler(); //Execute the handler, if any!
			}
		}
	}
}

void setMPUTimer(DOUBLE timeout, Handler handler)
{
	if (MPU_ticktick==0) //New timing starting?
	{
		MPU_ticktiming = 0.0f; //Restart counting!
	}
	MPU_ticktick = timeout*1000.0f; //Simple extension!
	MPUTickHandler = handler; //Use the new tick handler!
}

void removeMPUTimer()
{
	MPU_ticktick = 0; //Disable our handler!
	MPU_ticktiming = 0.0f; //Clear the timeout!
}