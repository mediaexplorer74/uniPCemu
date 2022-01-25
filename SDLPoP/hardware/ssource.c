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
#include "headers/emu/sound.h" //Sound output support!
#include "headers/hardware/parallel.h" //Parallel port support!
#include "headers/support/log.h" //Logging support!
#include "headers/support/sounddoublebuffer.h" //Double buffered sound support!

//Are we disabled?
#define __HW_DISABLED 0

//Sound source sample rate and buffer size!
#define __SSOURCE_RATE 7000.0f
#define __COVOX_RATE 44100.0f
//The Sound Source buffer is always 16 bytes large (needed for full detection of the Sound Source) on the Sound Source(required for full buffer detection)!
//The Convox buffer is undefined(theoretically has no buffer since it's a simple resistor ladder, but it does in this emulation), the threshold size is used instead in this case(CPU speed handles the playback rate).
#define __SSOURCE_BUFFER 16
//Rendering buffer needs to be large enough for a good sound!
#define __SSOURCE_DBLBUFFER (651*4)
#define __SSOURCE_HWBUFFER (651)
#define __COVOX_DBLBUFFER (4096*4)
#define __COVOX_HWBUFFER (4096)

DOUBLE ssourcetiming = 0.0f, covoxtiming = 0.0f, ssourcetick=0.0, covoxtick=0.0;
byte ssource_ready = 0; //Are we running?
FIFOBUFFER *ssourcestream = NULL; //Sound and covox source data stream and secondary buffer!
SOUNDDOUBLEBUFFER ssource_soundbuffer, covox_soundbuffer; //Sound source and covox output buffers!

byte covox_left=0x80, covox_right=0x80; //Current status for the covox speech thing output (any rate updated)!

//Current buffers for the Parallel port!
byte outbuffer = 0x00; //Our outgoing data buffer!
byte lastcontrol = 0x00; //Our current control data!
DOUBLE ssourcepowerdown = (DOUBLE)0; //Power down timer!
byte ssourcepoweredup = 0; //Powered up?

byte covox_mono = 0; //Current covox mode!
byte covox_ticking = 0; //When overflows past 5 it's mono!

byte ssource_output(void* buf, uint_32 length, byte stereo, void *userdata)
{
	if (__HW_DISABLED) return SOUNDHANDLER_RESULT_NOTFILLED; //We're disabled!
	if (stereo) return SOUNDHANDLER_RESULT_NOTFILLED; //Stereo not supported!
	byte *sample = (byte *)buf; //Sample buffer!
	uint_32 lengthleft = length; //Load the length!
	byte lastssourcesample=0x80;
	for (;lengthleft--;) //While length left!
	{
		if (!readDoubleBufferedSound8(&ssource_soundbuffer, &lastssourcesample)) lastssourcesample = 0x80; //No result, so 0 converted from signed to unsigned (0-255=>-128-127)!
		*sample++ = lastssourcesample; //Fill the output buffer!
	}
	return SOUNDHANDLER_RESULT_FILLED; //We're filled!
}

byte covox_output(void* buf, uint_32 length, byte stereo, void *userdata)
{
	if (__HW_DISABLED) return SOUNDHANDLER_RESULT_NOTFILLED; //We're disabled!
	if (!stereo) return SOUNDHANDLER_RESULT_NOTFILLED; //Stereo needs to be supported!
	byte *sample = (byte *)buf; //Sample buffer!
	uint_32 lengthleft = length; //Our stereo samples!
	word covoxsample=0x8080; //Dual channel sample!
	for (;lengthleft--;)
	{
		if (!readDoubleBufferedSound16(&covox_soundbuffer,&covoxsample)) covoxsample = 0x8080; //Try to read the samples if it's there, else zero out!
		*sample++ = (covoxsample&0xFF); //Left channel!
		*sample++ = (covoxsample>>8); //Right channel!
	}
	return SOUNDHANDLER_RESULT_FILLED; //We're filled!
}

void soundsource_covox_output(byte data)
{
	outbuffer = data; //Last data set on our lines!
	if (covox_mono) //Covox mono output?
	{
		covox_left = covox_right = outbuffer; //Write the data as mono sound!
	}
	else if (++covox_ticking==5) //Covox mono detected when 5 samples are written without control change?
	{
		covox_mono = 1;
		covox_ticking = 4; //We're not ticking anymore! Reset the ticker to prepare for invalid mono state!
	}
}

byte ssource_empty = 0; //Sound source full status! bit 6: 1=Was last full, 0=Was last empty!

void soundsource_covox_controlout(byte control)
{
	byte bitson,bitsoff;
	bitson = (control ^ lastcontrol) & control; //What is turned on?
	bitsoff = (control ^ lastcontrol) & lastcontrol; //What is turned off?
	//bit0=Covox left channel tick, bit1=Covox right channel tick, bit 3=Sound source mono channel tick. Bit 2=Sound source ON.
	//bit0/1/3 transition from high to low ticks sound!
	if ((control&4)==0) //Is the Sound Source powered up? The INIT line is inversed, so it's active low!
	{
		if (bitsoff & 8) //Toggling this bit on sends the data to the DAC! Documentation says rising edge on the port itself, thus falling edge since it's active low!
		{
			writefifobuffer(ssourcestream, outbuffer); //Add to the primary buffer when possible!
			covox_ticking = covox_mono = 0; //Not ticking nor covox mono!
		}
		ssourcepowerdown = (DOUBLE)0; //Not powering down!
		ssourcepoweredup = 1; //Has power!
	}
	else if (bitson&4) //Is the Sound Source powered off?
	{
		ssourcepowerdown = (DOUBLE)15000; //More than 10 usec to power down!
	}
	if (bitson&1) //Covox speech thing left channel pulse? Falling edge according to Dosbox!
	{
		covox_left = outbuffer; //Set left channel value!
		covox_ticking = covox_mono = 0; //Not ticking nor covox mono!
	}
	if (bitson&2) //Covox speech thing right channel pulse? Falling edge according to Dosbox!
	{
		covox_right = outbuffer; //Set right channel value!
		covox_ticking = covox_mono = 0; //Not ticking nor covox mono!
	}
	lastcontrol = control; //Save the last status for checking the bits!
}

byte soundsource_covox_controlin()
{
	return lastcontrol; //Give our last control byte!
}

byte soundsource_covox_status()
{
	byte freebuffer;
	byte result; //The result to use!
	result = (3|(outbuffer&0x80)); //Default for detection! Give the BUSY signal using the highest bit of the data lines. Any inversion on the bit isn't done here but at the chip itself!
	//Bits 0-3 is set to detect. Bit 2 is cleared with a full buffer. Bit 6 is set with a full buffer. Output buffer bit 7(pin 9) is wired to status bit 0(pin 11). According to Dosbox.
	freebuffer = fifobuffer_freesize(ssourcestream); //How empty is the buffer, in bytes!

	/*
	
	So, from the output pins(raw output signals on SELECT/INIT lines) until the input pins:
	- Off(SELECT(pin 17)=0), pin 16(INIT not grounded)=>10(ACK), ACK is reversed so: ACK=!INIT
	- On(SELECT(pin 17)=1) with full buffer(INIT not grounded): same as the off state, pin 16(INIT)=>10(ACK), ACK is reversed so: ACK=!INIT. So when INIT=1, ACK=0 and when INIT=0, ACK=1.
	- On(SELECT(pin 17)=1) with not full buffer: INIT is grounded(becomes 0 always), then reversed on read, so ACK=1 always.

	*/

	if (ssourcepoweredup) //Powered up FIFO&DAC?
	{
		if (!freebuffer) //We're full when there's nothing to add to the buffer!
		{
			ssource_empty = 0; //We have a full buffer! This is the inverted signal we're giving(ACK=low when set, so we're reporting it's set).
		}
		else //Buffer is not full instead?
		{
			ssource_empty = 0x40; //Buffer is not full! Ground the signal!
		}
	}
	else //Powered off?
	{
		//When the state is off, ACK=!INIT, just like full buffer.
		ssource_empty = 0; //Act like full!
	}
	//The and operation between the empty and the -INIT line(which needs setting when it's ground for us to work with to become a positive value) is the grounding of the transistor from the INIT line using the empty input from the chip(it's _FULL_ signal, our ssource_emtpy variable).
	//The inversion of the ACK line is done at the parallel port controller itself!
	result |= ((((~lastcontrol) << 4)&ssource_empty)&0x40);
	return result; //We have an empty buffer!
}

void tickssourcecovox(DOUBLE timepassed)
{
	byte ssourcesample;
	if (__HW_DISABLED) return; //We're disabled!
	//HW emulation of ticking the sound source in CPU time!
	if (ssourcepowerdown) //Powered down by software?
	{
		ssourcepowerdown -= timepassed; //Time it's power down!
		if (ssourcepowerdown<=(DOUBLE)0.0) //Fully powered down?
		{
			ssourcepowerdown = (DOUBLE)0; //Stop timing!
			fifobuffer_clear(ssourcestream); //FIFO has no power anymore!
			ssource_empty = 0x40; //Clear the sticky buffer: update with a new status immediately!		
			ssourcepoweredup = 0; //We've powered down!
		}
	}
	ssourcetiming += timepassed; //Tick the sound source!
	if (unlikely(ssourcetiming>=ssourcetick && ssourcetick)) //Enough time passed to tick?
	{
		do
		{
			if (ssourcepoweredup) //Is the Sound Source powered up?
			{
				if (readfifobuffer(ssourcestream,&ssourcesample))
					writeDoubleBufferedSound8(&ssource_soundbuffer,ssourcesample); //Move data to the destination buffer one sample at a time!
				else
					writeDoubleBufferedSound8(&ssource_soundbuffer, 0x80); //Move Silence to the destination buffer one sample at a time!
			}
			else //Sound source is powered down? Flush the buffers!
			{
				writeDoubleBufferedSound8(&ssource_soundbuffer, 0x80); //Nothing, clear output!
			}
			ssourcetiming -= ssourcetick; //Ticked one sample!
		} while (ssourcetiming>=ssourcetick); //Do ... while, because we execute at least once!
	}
	
	covoxtiming += timepassed; //Tick the Covox Speech Thing!
	if (unlikely((covoxtiming>=covoxtick) && covoxtick)) //Enough time passed to tick?
	{
		//Write both left and right channels at the destination to get the sample rate converted, since we don't have a input buffer(just a state at any moment in time)!
		do
		{
			writeDoubleBufferedSound16(&covox_soundbuffer, covox_left | (covox_right << 8)); //Move data to the destination buffer one sample at a time!
			covoxtiming -= covoxtick; //Ticked one sample!
		} while (covoxtiming>=covoxtick); //Do ... while, because we execute at least once!
	}
}

void ssource_setVolume(float volume)
{
	if (__HW_DISABLED) return; //We're disabled!
	setVolume(&ssource_output, NULL, volume); //Set the volume!
	setVolume(&covox_output, NULL, volume); //Set the volume!
}

void doneSoundsource()
{
	if (__HW_DISABLED) return; //We're disabled!
	if (ssource_ready) //Are we running?
	{
		removechannel(&ssource_output, NULL, 0); //Remove the channel!
		removechannel(&covox_output, NULL, 0); //Remove the channel!
		free_fifobuffer(&ssourcestream); //Finish the stream if it's there!
		freeDoubleBufferedSound(&ssource_soundbuffer); //Finish the stream if it's there!
		freeDoubleBufferedSound(&covox_soundbuffer); //Finish the stream if it's there!
		ssource_ready = 0; //We're finished!
	}
}

void initSoundsource() {
	if (__HW_DISABLED) return; //We're disabled!
	doneSoundsource(); //Make sure we're not already running!
	
	ssource_empty = 0; //Initialise status!

	//Sound source streams!
	ssourcestream = allocfifobuffer(__SSOURCE_BUFFER,0); //Our FIFO buffer! This is the buffer the CPU writes to!
	
	//Covox streams!

	ssourcetiming = covoxtiming = 0.0f; //Initialise our timing!

	if (ssourcestream && allocDoubleBufferedSound8(__SSOURCE_DBLBUFFER,&ssource_soundbuffer,0,__SSOURCE_RATE) && allocDoubleBufferedSound16(__COVOX_DBLBUFFER,&covox_soundbuffer,0,__COVOX_RATE)) //Allocated buffers?
	{
		if (addchannel(&covox_output, NULL, "Covox Speech Thing", __COVOX_RATE, __COVOX_HWBUFFER, 1, SMPL8U,1)) //Covox channel added?
		{
			if (addchannel(&ssource_output, NULL, "Sound Source", __SSOURCE_RATE, __SSOURCE_HWBUFFER, 0, SMPL8U,1)) //Sound source channel added?
			{
				outbuffer = lastcontrol = 0; //Reset output buffer and last control!
				ssource_setVolume(100.0f); //Default volume: 100%!
				registerParallel(0,&soundsource_covox_output,&soundsource_covox_controlout,&soundsource_covox_controlin,&soundsource_covox_status); //Register out parallel port for handling!
				ssource_ready = 1; //We're running!
			}
		}
	}
	if (!ssource_ready) //Failed to allocate anything?
	{
		free_fifobuffer(&ssourcestream); //Finish the stream if it's there!
	}

	//Our tick timings!
	#ifdef IS_LONGDOUBLE
	ssourcetick = (1000000000.0L / __SSOURCE_RATE);
	covoxtick = (1000000000.0L / __COVOX_RATE);
	#else
	ssourcetick = (1000000000.0 / __SSOURCE_RATE);
	covoxtick = (1000000000.0 / __COVOX_RATE);
	#endif
}
