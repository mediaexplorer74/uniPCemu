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

/*

src:http://wiki.osdev.org/Programmable_Interval_Timer#Channel_2
82C54 PIT (Timer) (82C54(8253/8254 in older systems))

*/

#include "headers/types.h"
#include "headers/hardware/ports.h" //IO Port support!
#include "headers/hardware/pic.h" //IRQ support!
#include "headers/emu/sound.h" //PC speaker support for the current emu!
#include "headers/hardware/8253.h" //Our own typedefs etc.
#include "headers/support/locks.h" //Locking support!

//PC speaker support functionality:
#include "headers/support/sounddoublebuffer.h" //Sound double buffer support!
#include "headers/support/wave.h" //Wave support!
#include "headers/support/log.h" //Logging support!
#include "headers/support/filters.h" //Filter support!
#include "headers/hardware/ppi.h" //Failsafe timer support!
#include "headers/hardware/i430fx.h" //i430fx support!

//Are we disabled?
#define __HW_DISABLED 0

//Define below to log all PIT accesses!
//#define LOG_PIT
#define PIT_LOGFILE "PIT"

//Enable logs if defined!
#ifdef LOG_PIT
#define PIT_LOG(...) { dolog(PIT_LOGFILE,__VA_ARGS__); }
#else
#define PIT_LOG(...)
#endif

/*

PC SPEAKER

*/

//What volume, in percent!
#define SPEAKER_VOLUME 100.0f

//Speaker playback rate!
#define SPEAKER_RATE 44100
//Use actual response as speaker rate! 60us responses!
//#define SPEAKER_RATE (1000000.0f/60.0f)
//Speaker buffer size!
#define SPEAKER_BUFFER 4096
//Speaker low pass filter values (if defined, it's used)! We're 96dB dynamic range, with 6dB per octave, so 16 times filtering is needed to filter it fully at the nyquist frequency.
#define SPEAKER_LOWPASS ((((float)SPEAKER_RATE)/2.0f)/16.0f)
//Speaker volume during filtering! Take half to prevent overflow!
#define SPEAKER_LOWPASSVOLUME 0.5f

//Precise timing rate!
//The clock speed of the PIT (14.31818MHz divided by 12)!
#define MHZ14_RATE 12
#ifdef IS_LONGDOUBLE
#define TIME_RATE (MHZ14/12.0L)
#else
#define TIME_RATE (MHZ14/12.0)
#endif

//Run the low pass at the 72 raw samples rate instead (16571Hz)!
//#undef SPEAKER_LOWPASS
//Formula for 4.7uF and 8 Ohm results in: 1/(2*PI*RC)=1/(2*PI*8*0.0000047) Hz(~4.4kHz) instead of ~16kHz using 72 samples.
//#define SPEAKER_LOWPASS (1/(2.0f*PI*8*0.0000047))
//#define SPEAKER_LOWPASS (TIME_RATE/72.0)

//Log the speaker to this .wav file when defined (raw and duty cycles log)!
//#define SPEAKER_LOGRAW "captures/speakerraw.wav"
//#define SPEAKER_LOGDUTY "captures/speakerduty.wav"

//End of defines!

byte enablespeaker = 0; //Are we sounding the PC speaker?

#ifdef SPEAKER_LOGRAW
	WAVEFILE *speakerlograw = NULL; //The log file for the speaker output!
#endif
#ifdef SPEAKER_LOGDUTY
	WAVEFILE *speakerlogduty = NULL; //The log file for the speaker output!
#endif

DOUBLE speaker_ticktiming; //Both current clocks!
DOUBLE speaker_tick = 0.0; //Time of a tick in the PC speaker sample!
DOUBLE time_tick = 0.0; //Time of a tick in the PIT!
DOUBLE time_tickreverse = 0.0; //Reversed of time_tick(1/ticktime)!

byte oldPCSpeakerPort = 0x00; //Backup for tracking channel 2 gate changes!
byte PCSpeakerPort; //Port 0x61 for the PC Speaker! Bit0=Gate, Bit1=Data enable, bit 4=PIT1 output!

extern byte EMU_RUNNING; //Current emulator status!

uint_32 time_ticktiming; //Current timing, in 14MHz ticks!

PITTick PIT1Ticker = NULL; //The PIT1 ticker, if connected!
SOUNDDOUBLEBUFFER pcspeaker_soundbuffer; //Output buffers for rendering!

typedef struct
{
	byte mode; //PC speaker mode!
	word frequency; //Frequency divider that has been set!
	byte status; //Status of the counter!
	word ticker; //16-bit ticks!
	byte reload; //Reload requested?
	byte channel_status; //Current output status!
	byte gatewenthigh; //Gate went high?
	byte gatelistening; //Listening to gate going high?
	byte reloadlistening; //Listening to reloading?
	byte nullcount; //Are we not loaded into the timer yet?

	//Output generating timer!
	float samples; //Output samples to process for the current audio tick!
	DOUBLE samplesleft; //Samples left to process!
	byte lastchannel_status; //Last recorded channel status
	byte risetoggle; //Toggled bit 0 when we rise.
	FIFOBUFFER *rawsignal; //The raw signal buffer for the oneshot mode!
} PITCHANNEL; // speaker!

PITCHANNEL PITchannels[6]; //All possible PIT channels, whether used or not!

byte numPITchannels = 3; //Amount of PIT channels to emulate!

byte pitdecimal[8] = {0,0,0,0,0,0,0,0}; //Are we addressed as decimal data?

byte speakerCallback(void* buf, uint_32 length, byte stereo, void *userdata) {
	static sword s = 0; //Last sample!
	uint_32 i;
	if (__HW_DISABLED) return 0; //Abort!	

	//First, our information sources!
	i = 0; //Init counter!
	if (stereo) //Stereo samples?
	{
		INLINEREGISTER sample_stereo_p ubuf_stereo = (sample_stereo_p)buf; //Active buffer!
		for (;;) //Process all samples!
		{ //Process full length!
			readDoubleBufferedSound16(&pcspeaker_soundbuffer, (word *)&s); //Not readable from the buffer? Duplicate last sample!

			ubuf_stereo->l = ubuf_stereo->r = s; //Single channel!
			++ubuf_stereo; //Next item!
			if (++i == length) break; //Next item!
		}
	}
	else //Mono samples?
	{
		INLINEREGISTER sample_p ubuf_mono = (sample_p)buf; //Active buffer!
		for (;;)
		{ //Process full length!
			readDoubleBufferedSound16(&pcspeaker_soundbuffer, (word *)&s); //Not readable from the buffer? Duplicate last sample!
			*ubuf_mono = s; //Mono channel!
			++ubuf_mono; //Next item!
			if (++i == length) break; //Next item!
		}
	}

	return SOUNDHANDLER_RESULT_FILLED; //We're filled!
}

void registerPIT1Ticker(PITTick ticker) //Register a PIT1 ticker for usage?
{
	PIT1Ticker = ticker; //Register this PIT1 ticker!
}

OPTINLINE void reloadticker(byte channel)
{
	PITchannels[channel].ticker = PITchannels[channel].frequency; //Reload the start value!
	PITchannels[channel].nullcount = 0; //We're loaded into the timer!
}

OPTINLINE void wrapPITticker(byte channel)
{
	if (pitdecimal[channel] && (PITchannels[channel].ticker>9999)) //We're in decimal mode?
	{
		PITchannels[channel].ticker = 9999-MAX((0xFFFF-PITchannels[channel].ticker),9999); //Wrap, safe decimal to binary style!
	}
}

byte channel_reload[8] = {0,0,0,0,0,0,0,0}; //To reload the channel next cycle?

DOUBLE ticklength = 0.0; //Length of PIT samples to process every output sample!

HIGHLOWPASSFILTER PCSpeakerFilter; //Our filter to use!
float speaker_currentsample;

void tickPIT(DOUBLE timepassed, uint_32 MHZ14passed) //Ticks all PIT timers available!
{
	if (__HW_DISABLED) return;
	INLINEREGISTER uint_32 length; //Amount of samples to generate!
	INLINEREGISTER uint_32 i;
	uint_32 dutycyclei; //Input samples to process!
	INLINEREGISTER uint_32 tickcounter;
	word oldvalue; //Old value before decrement!
	DOUBLE tempf;
	uint_32 render_ticks; //A one shot tick!
	byte currentsample; //Saved sample in the 1.19MHz samples!
	byte channel; //Current channel?
	byte mode; //The mode of the currently processing channel!

	i = time_ticktiming; //Load the current timing!
	i += MHZ14passed; //Add the amount of time passed to the PIT timing!

	//Render 1.19MHz samples for the time that has passed!
	length = i/MHZ14_RATE; //How many ticks to tick?
	i -= length*MHZ14_RATE; //Rest the amount of ticks!
	time_ticktiming = i; //Save the new count!

	if (length) //Anything to tick at all?
	{
		for (channel=0;channel<numPITchannels;channel++)
		{
			mode = PITchannels[channel].mode; //Current mode!
			for (tickcounter = length;tickcounter;--tickcounter) //Tick all needed!
			{
				switch (mode) //What mode are we rendering?
				{
				case 0: //Interrupt on Terminal Count? Is One-Shot without Gate Input?
				case 1: //One-shot mode?
					switch (PITchannels[channel].status) //What status?
					{
					case 0: //Output goes low/high?
						PITchannels[channel].channel_status = mode; //We're high when mode 1, else low with mode 0!
						PITchannels[channel].reloadlistening |= 1; //We're listening to reloads!
						if (PITchannels[channel].reload && ((PITchannels[channel].reloadlistening&2)==0)) //Ready to reload?
						{
							PITchannels[channel].gatelistening = mode; //We're listening to gate with mode 1!
							PITchannels[channel].status = 1; //Skip to 1: we're ready to run already!
							goto mode0_1; //Skip to step 1!
						}
						break;
					case 1: //Wait for next rising edge of gate input?
						mode0_1:
						if (!mode) //No wait on mode 0?
						{
							PITchannels[channel].status = 2;
							goto mode0_2;
						}
						else if (PITchannels[channel].gatewenthigh) //Mode 1 waits for gate to become high!
						{
							PITchannels[channel].gatewenthigh = 0; //Not went high anymore!
							PITchannels[channel].gatelistening = 0; //We're not listening to gate with mode 1 anymore!
							PITchannels[channel].status = 2;
							goto mode0_2;
						}
						break;
					case 2: //Output goes low and we start counting to rise! After timeout we become 4(inactive) with mode 1!
						mode0_2:
						if (PITchannels[channel].reload)
						{
							PITchannels[channel].reload = 0; //Not reloading anymore!
							PITchannels[channel].channel_status = 0; //Lower output!
							reloadticker(channel); //Reload the counter!
						}

						oldvalue = PITchannels[channel].ticker; //Save old ticker for checking for overflow!
						if (mode) --PITchannels[channel].ticker; //Mode 1 always ticks?
						else if ((PCSpeakerPort&1) || (channel!=2)) --PITchannels[channel].ticker; //Mode 0 ticks when gate is high! The other channels are tied 1!
						wrapPITticker(channel); //Wrap us correctly!
						if ((!PITchannels[channel].ticker) && oldvalue) //Timeout when ticking? We're done!
						{
							PITchannels[channel].channel_status = 1; //We're high again!
						}
						break;
					default: //Unsupported! Ignore any input!
						break;
					}
					break;
				case 2: //Also Rate Generator mode?
				case 6: //Rate Generator mode?
					switch (PITchannels[channel].status) //What status?
					{
					case 0: //Output going high! See below! Wait for reload register to be written!
						PITchannels[channel].channel_status = 1; //We're high!
						PITchannels[channel].status = 1; //Skip to 1: we're ready to run already!
						PITchannels[channel].reloadlistening = 1; //We're listening to reloads!
						goto mode2_1; //Skip to step 1!
						break;
					case 1: //We're starting the count?
						mode2_1:
						if (PITchannels[channel].reload)
						{
							reload2:
							PITchannels[channel].reload = 0; //Not reloading!
							reloadticker(channel); //Reload the counter!
							PITchannels[channel].channel_status = 1; //We're high!
							PITchannels[channel].status = 2; //Start counting!
							PITchannels[channel].reloadlistening = 0; //We're not listening to reloads anymore!
							PITchannels[channel].gatelistening = 1; //We're listening to the gate!
						}
						break;
					case 2: //We start counting to rise!!
						if (PITchannels[channel].gatewenthigh) //Gate went high?
						{
							PITchannels[channel].gatewenthigh = 0; //Not anymore!
							goto reload2; //Reload and execute!
						}
						if (((PCSpeakerPort & 1) && (channel==2)) || (channel!=2)) //We're high or undefined?
						{
							--PITchannels[channel].ticker; //Decrement?
							switch (PITchannels[channel].ticker) //Two to one? Go low!
							{
							case 1:
								PITchannels[channel].channel_status = 0; //We're going low during this phase!
								break;
							case 0:
								PITchannels[channel].channel_status = 1; //We're going high again during this phase!
								reloadticker(channel); //Reload the counter!
								break;
							default: //No action taken!
								break;
							}
						}
						else //We're low? Output=High and wait for reload!
						{
							PITchannels[channel].channel_status = 1; //We're going high again during this phase!
						}
						break;
					default: //Unsupported! Ignore any input!
						break;
					}
				//mode 2==6 and mode 3==7.
				case 3: //Square Wave mode?
				case 7: //Also Square Wave mode?
					switch (PITchannels[channel].status) //What status?
					{
					case 0: //Output going high! See below! Wait for reload register to be written!
						PITchannels[channel].channel_status = 1; //We're high!
						PITchannels[channel].reloadlistening = 1; //We're listening to reloads!
						if (PITchannels[channel].reload)
						{
							PITchannels[channel].reload = 0; //Not reloading!
							reloadticker(channel); //Reload the counter!
							PITchannels[channel].status = 1; //Next status: we're loaded and ready to run!
							PITchannels[channel].reloadlistening = 0; //We're not listening to reloads anymore!
							PITchannels[channel].gatelistening = 1; //We're listening to the gate!
							goto mode3_1; //Skip to step 1!
						}
						break;
					case 1: //We start counting to rise!!
						mode3_1:
						if (PITchannels[channel].gatewenthigh)
						{
							PITchannels[channel].gatewenthigh = 0; //Not anymore!
							PITchannels[channel].reload = 0; //Reloaded!
							reloadticker(channel); //Gate going high reloads the ticker immediately!
						}
						if ((PCSpeakerPort&1) || (channel!=2)) //To tick at all? The other channels are tied 1!
						{
							PITchannels[channel].ticker -= 2; //Decrement by 2 instead?
							switch (PITchannels[channel].ticker)
							{
							case 0: //Even counts decreased to 0!
							case 0xFFFF: //Odd counts decreased to -1/0xFFFF.
								PITchannels[channel].channel_status ^= 1; //We're toggling during this phase!
								PITchannels[channel].reload = 0; //Reloaded!
								reloadticker(channel); //Reload the next value to tick!
								break;
							default: //No action taken!
								break;
							}
						}
						break;
					default: //Unsupported! Ignore any input!
						break;
					}
					break;
				case 4: //Software Triggered Strobe?
				case 5: //Hardware Triggered Strobe?
					switch (PITchannels[channel].status) //What status?
					{
					case 0: //Output going high! See below! Wait for reload register to be written!
						PITchannels[channel].channel_status = 1; //We're high!
						PITchannels[channel].status = 1; //Skip to 1: we're ready to run already!
						PITchannels[channel].reloadlistening = 1; //We're listening to reloads!
						PITchannels[channel].gatelistening = 1; //We're listening to the gate!
						goto mode4_1; //Skip to step 1!
						break;
					case 1: //We're starting the count or waiting for rising gate(mode 5)?
						mode4_1:
						if (PITchannels[channel].reload)
						{
						pit45_reload: //Reload PIT modes 4&5!
							if ((mode == 4) || ((PITchannels[channel].gatewenthigh) && (mode == 5))) //Reload when allowed!
							{
								PITchannels[channel].gatewenthigh = 0; //Reset gate high flag!
								PITchannels[channel].reload = 0; //Not reloading!
								reloadticker(channel); //Reload the counter!
								PITchannels[channel].status = 2; //Start counting!
							}
						}
						break;
					case 2: //We start counting to rise!!
					case 3: //We're counting, but ignored overflow?
						if (PITchannels[channel].reload || (((mode==5) && PITchannels[channel].gatewenthigh))) //We're reloaded?
						{
							goto pit45_reload; //Reload when allowed!
						}
						if (((PCSpeakerPort & 1) && (channel == 2)) || (channel!=2)) //We're high or undefined?
						{
							--PITchannels[channel].ticker; //Decrement?
							wrapPITticker(channel); //Wrap us correctly!
							if (!PITchannels[channel].ticker && (PITchannels[channel].status!=3)) //One to zero? Go low when not overflown already!
							{
								PITchannels[channel].channel_status = 0; //We're going low during this phase!
								PITchannels[channel].status = 3; //We're ignoring any further overflows from now on!
							}
							else
							{
								PITchannels[channel].channel_status = 1; //We're going high again any other phase!
							}
						}
						else //We're low? Output=High and wait for reload!
						{
							PITchannels[channel].channel_status = 1; //We're going high again during this phase!
						}
						break;
					default:
						break;
					}
					break;
				default: //Unsupported mode! Ignore any input!
					break;
				}
				currentsample = PITchannels[channel].channel_status; //The current sample we're processing, prefetched!
				if (channel) //Handle channel 1&2 and 3+ seperately too!
				{
					//Process the rise toggle!
					if (((PITchannels[channel].lastchannel_status^currentsample)&1) && currentsample) //Raised?
					{
						PITchannels[channel].risetoggle ^= 1; //Toggle the bit in our output port!
					}

					//Now, write the (changed) output to the channel to use!
					if (channel==2) //PIT2 needs a sound buffer?
					{
						//We're ready for the current result!
						writefifobuffer(PITchannels[channel].rawsignal, currentsample&((PCSpeakerPort & 2) >> 1)); //Add the data to the raw signal! Apply the output mask too!
					}
					else if (channel==1) //PIT1 is connected to an external ticker!
					{
						if ((PITchannels[channel].lastchannel_status^currentsample) & 1) //Changed?
						{
							if (PIT1Ticker) //Gotten a handler for it?
							{
								PIT1Ticker(currentsample); //Handle this PIT1 tick!
							}
						}
					}
					else if (channel == 3) //2nd PIT, channel 0 is the Failsafe timer?
					{
						if (((PITchannels[channel].lastchannel_status ^ currentsample) & 1)) //Changed failsafe timer state?
						{
							//Perform the failsafe timer NMI if it's enabled!
							PPI_failsafetimer(PITchannels->lastchannel_status); //Update the failsafe timer state!
						}
					}
				}
				else //PIT0?
				{
					if ((PITchannels[channel].lastchannel_status^currentsample) & 1) //Changed?
					{
						if (currentsample) //Raised?
						{
							raiseirq(0); //Raise IRQ0!
						}
						else //Lowered?
						{
							lowerirq(0); //Lower IRQ0!
						}
					}
				}
				PITchannels[channel].lastchannel_status = currentsample; //Save the new status!
			}
		}
	}

	//PC speaker output!
	speaker_ticktiming += timepassed; //Get the amount of time passed for the PC speaker (current emulated time passed according to set speed)!
	if ((speaker_ticktiming >= speaker_tick) && enablespeaker) //Enough time passed to render the physical PC speaker and enabled?
	{
		length = (uint_32)floor(SAFEDIV(speaker_ticktiming, speaker_tick)); //How many ticks to tick?
		speaker_ticktiming -= (length*speaker_tick); //Rest the amount of ticks!

		//Ticks the speaker when needed!
		i = 0; //Init counter!
		//Generate the samples from the output signal!
		for (;;) //Generate samples!
		{
			//Average our input ticks!
			PITchannels[2].samplesleft += ticklength; //Add our time to the sample time processed!
			tempf = floor(PITchannels[2].samplesleft); //Take the rounded number of samples to process!
			PITchannels[2].samplesleft -= tempf; //Take off the samples we've processed!
			render_ticks = (uint_32)tempf; //The ticks to render!

			//render_ticks contains the output samples to process! Calculate the duty cycle by low pass filter and use it to generate a sample!
			for (dutycyclei = render_ticks;dutycyclei;--dutycyclei)
			{
				if (!readfifobuffer(PITchannels[2].rawsignal, &currentsample)) break; //Failed to read the sample? Stop counting!
				speaker_currentsample = currentsample?(SHRT_MAX*SPEAKER_LOWPASSVOLUME):(SHRT_MIN*SPEAKER_LOWPASSVOLUME); //Convert the current result to the 16-bit data, signed instead of unsigned!
				#ifdef SPEAKER_LOGRAW
					writeWAVMonoSample(speakerlograw,(short)speaker_currentsample); //Log the mono sample to the WAV file, converted as needed!
				#endif
				#ifdef SPEAKER_LOWPASS
					//We're applying the low pass filter for the speaker!
					applySoundFilter(&PCSpeakerFilter, &speaker_currentsample);
				#endif
				#ifdef SPEAKER_LOGDUTY
					writeWAVMonoSample(speakerlogduty,(short)speaker_currentsample); //Log the mono sample to the WAV file, converted as needed!
				#endif
			}

			//Add the result to our buffer!
			writeDoubleBufferedSound16(&pcspeaker_soundbuffer, (short)speaker_currentsample); //Write the sample to the buffer (mono buffer)!
			++i; //Add time!
			if (i == length) //Fully rendered?
			{
				return; //Next item!
			}
		}
	}
}

void initSpeakers(byte soundspeaker)
{
	if (__HW_DISABLED) return; //Abort!
	//First speaker defaults!
	memset(&PITchannels, 0, sizeof(PITchannels)); //Initialise our data!
	enablespeaker = soundspeaker; //Are we to sound the speaker?
	byte i;
	for (i=0;i<numPITchannels;i++)
	{
		PITchannels[i].rawsignal = allocfifobuffer(((uint_64)((2048.0f / SPEAKER_RATE)*TIME_RATE)) + 1, 0); //Nonlockable FIFO with 2048 word-sized samples with lock (TICK_RATE)!
		if (i==2 && enablespeaker) //Speaker?
		{
			allocDoubleBufferedSound16(SPEAKER_BUFFER,&pcspeaker_soundbuffer,0,SPEAKER_RATE); //(non-)Lockable FIFO with X word-sized samples without lock!
		}
	}
	speaker_ticktiming = time_ticktiming = 0; //Initialise our timing!
	if (enablespeaker)
	{
		addchannel(&speakerCallback, &PITchannels[2], "PC Speaker", SPEAKER_RATE, SPEAKER_BUFFER, 0, SMPL16S,1); //Add the speaker at the hardware rate, mono! Make sure our buffer responds every 2ms at least!
		setVolume(&speakerCallback, &PITchannels[2], SPEAKER_VOLUME); //What volume?

#ifdef SPEAKER_LOGRAW
		domkdir("captures"); //Captures directory!
		speakerlograw = createWAV(SPEAKER_LOGRAW,1,(uint_32)TIME_RATE); //Start raw wave file logging!
#endif
#ifdef SPEAKER_LOGDUTY
		domkdir("captures"); //Captures directory!
		speakerlogduty = createWAV(SPEAKER_LOGDUTY,1,(uint_32)TIME_RATE); //Start duty wave file logging!
#endif
	}
	initSoundFilter(&PCSpeakerFilter,0,(float)SPEAKER_LOWPASS, (float)TIME_RATE); //Initialize our low-pass filter to use!
}

void doneSpeakers()
{
	if (__HW_DISABLED) return;
	removechannel(&speakerCallback, &PITchannels[2], 0); //Remove the speaker!
	byte i;
	for (i=0;i<numPITchannels;i++)
	{
		free_fifobuffer(&PITchannels[i].rawsignal); //Release the FIFO buffer we use!
		if (i==2 && enablespeaker) //Speaker?
		{
			freeDoubleBufferedSound(&pcspeaker_soundbuffer); //Release the FIFO buffer we use!
		}
	}
	#ifdef SPEAKER_LOGRAW
		if (enablespeaker)
		{
			closeWAV(&speakerlograw); //Stop wave file logging!
		}
	#endif
	#ifdef SPEAKER_LOGDUTY
		if (enablespeaker)
		{
			closeWAV(&speakerlogduty); //Stop wave file logging!
		}
	#endif
}

void speakerGateUpdated()
{
	if (((oldPCSpeakerPort ^ PCSpeakerPort) & 0x1) && (PCSpeakerPort & 0x1)) //Risen?
	{
		PITchannels[2].gatewenthigh |= PITchannels[2].gatelistening; //We went high if listening on PIT2!
	}
	oldPCSpeakerPort = PCSpeakerPort; //Save new value!
}

void setPITFrequency(byte channel, word frequency) //Set the new frequency!
{
	if (__HW_DISABLED) return; //Abort!
	PITchannels[channel].frequency = frequency;
	PITchannels[channel].reload |= PITchannels[channel].reloadlistening; //We've been reloaded!
	if ((PITchannels[channel].reloadlistening & 2) && (PITchannels[channel].mode==0)) //Immediately load it into the counter?
	{
		if (PITchannels[channel].reload) //Reload requested?
		{
			PITchannels[channel].gatelistening = PITchannels[channel].mode; //We're listening to gate with mode 1!
			PITchannels[channel].status = 1; //Skip to 1: we're ready to run already!
			PITchannels[channel].reload = 0; //Not reloading anymore!
			PITchannels[channel].channel_status = 0; //Lower output!
			reloadticker(channel); //Reload the counter!
		}
	}
	PITchannels[channel].reloadlistening &= ~2; //We're reloaded now!
}

void setPITMode(byte channel, byte mode)
{
	if (__HW_DISABLED) return; //Abort!
	PITchannels[channel].mode = (mode&7); //Set the current PC speaker mode! Protect against invalid modes!
	PITchannels[channel].status = 0; //Output going high! Wait for reload to be set!
	PITchannels[channel].reload = 0; //Init to be sure!
	PITchannels[channel].gatewenthigh = 0; //Reset gate status to be sure, since we're reset!
	PITchannels[channel].gatelistening = PITchannels[channel].reloadlistening = 0; //Not listening to anything right now!
	switch (PITchannels[channel].mode) //Are we to start listening?
	{
	case 0: //We're listening?
	case 2: //We're listening?
	case 3: //We're listening?
	case 4: //We're listening?
		PITchannels[channel].reloadlistening = 1; //Not listening to anything right now!
		break;
	default: //Not listening!
		break;
	}
	switch (PITchannels[channel].mode) //Are we to start listening?
	{
	case 0: //We're listening?
		PITchannels[channel].reloadlistening |= 2; //Wait to start counting until loaded!
		break;
	default: //Not listening?
		break;
	}
	PITchannels[channel].nullcount = 1; //We're not loaded into the divider yet!
}

extern byte EMU_RUNNING; //Emulator running? 0=Not running, 1=Running, Active CPU, 2=Running, Inactive CPU (BIOS etc.)

void cleanPIT()
{
	//We don't do anything: we're locked to CPU speed instead!
}

uint_32 pitcurrentlatch[8][2], pitlatch[8], pitdivisor[8]; //Latches & divisors are 32-bits large!
byte pitcommand[8]; //PIT command is only 1 byte large!

//PC Speaker functionality in PIT

void updatePITState(byte channel)
{
	//Calculate the current PIT0 state by frequency and time passed!
	pitlatch[channel] = PITchannels[channel].ticker; //Convert it to 16-bits value of the PIT and latch it!
}

//Read back command support!
byte statusbytes[8] = {0,0,0,0,0,0,0,0}; //All 3 status bytes to be read when the Read Back command executes 
byte readstatus[8] = {0,0,0,0,0,0,0,0};
byte readlatch[8] = {0,0,0,0,0,0,0,0};

byte lastpit[2] = {0,0};

word decodeBCD16(word bcd) //Converts from digits to decimal!
{
	INLINEREGISTER word temp, result = 0;
	temp = bcd; //Load the BCD value!
	result += (temp & 0xF); //Factor 1!
	temp >>= 4;
	result += (temp & 0xF) * 10; //Factor 10!
	temp >>= 4;
	result += (temp & 0xF) * 100; //Factor 100!
	temp >>= 4;
	result += (temp & 0xF) * 1000; //Factor 1000!
	return result; //Give the decoded integer value!
}

word encodeBCD16(word value) //Converts from decimal to digits!
{
	INLINEREGISTER word temp, result = 0;
	temp = value; //Load the original value!
	temp %= 10000; //Wrap around!
	result |= (0x1000 * (temp / 1000)); //Factor 1000!
	temp %= 1000;
	result |= (0x0100 * (temp / 100)); //Factor 100
	temp %= 100;
	result |= (0x0010 * (temp / 10)); //Factor 10!
	temp %= 10;
	result |= temp; //Factor 1!
	return result;
}

extern byte is_XT; //Are we emulating a XT architecture?

byte TimerBase[2] = {0,3}; //The timer base to use!
byte secondPITmapping[8] = {0,0xFF,0xFF,0xFF,0xFF,1,1,0xFF}; //Ports 44-4B mapping to second PIT, when emulation is enabled!

byte in8254(word portnum, byte *result)
{
	byte whichtimer=0;
	byte pit;
	if (__HW_DISABLED) return 0; //Abort!
	switch (portnum)
	{
		case 0x48:
		case 0x49:
		case 0x4A: //Second timer on Compaq?
			if (numPITchannels!=6) return 0; //Disabled?
			whichtimer = 1; //Second timer to use!
			pit = (byte)(portnum&0xFF);
			pit -= 0x48; //PIT!
			pit = secondPITmapping[pit]; //Map according to the second PIT!
			if (pit==0xFF) return 0; //Unmapped?
			goto applyINpit;
		case 0x40:
		case 0x41:
		case 0x42: //First PIT?
			pit = (byte)(portnum&0xFF);
			pit &= 3; //PIT!
			applyINpit:
			pit += TimerBase[whichtimer]; //Convert to the correct timer (0-5)!
			if (readstatus[pit]) //Status is to be read now?
			{
				*result = statusbytes[pit]; //Read the current status!
				readstatus[pit] = 0; //We're read!
				PIT_LOG("Read from data port 0x%02X=%02X", portnum, *result);
				return 1; //Finished!
			}
			if (readlatch[pit]==0) //No latch mode?
			{
				updatePITState(pit); //Update the state: effect like a running timer!
			}
			switch (pitcommand[pit] & 0x30) //What input mode currently?
			{
			default:
			case 0x10: //Lo mode?
				if (pitdecimal[pit])
				{
					*result = (encodeBCD16(pitlatch[pit]) & 0xFF);
				}
				else //Binary?
				{
					*result = (pitlatch[pit] & 0xFF);
				}
				readlatch[pit] = 0; //Finished latching!
				break;
			case 0x20: //Hi mode?
				if (pitdecimal[pit])
				{
					*result = ((encodeBCD16(pitlatch[pit]) >> 8) & 0xFF);
				}
				else //Binary?
				{
					*result = ((pitlatch[pit]>>8) & 0xFF) ;
				}
				readlatch[pit] = 0; //Finished latching!
				break;
			case 0x30: //Lo/hi mode?
				if (pitcurrentlatch[pit][0] == 0)
				{
					//Give the value!
					pitcurrentlatch[pit][0] = 1;
					if (pitdecimal[pit])
					{
						*result = (encodeBCD16(pitlatch[pit]) & 0xFF);
					}
					else //Binary?
					{
						*result = (pitlatch[pit] & 0xFF);
					}
				}
				else
				{
					pitcurrentlatch[pit][0] = 0;
					if (pitdecimal[pit])
					{
						*result = ((encodeBCD16(pitlatch[pit]) >> 8) & 0xFF);
					}
					else //Binary?
					{
						*result = ((pitlatch[pit] >> 8) & 0xFF);
					}
					readlatch[pit] = 0; //Normal handling again!
				}
				break;
			}
			PIT_LOG("Read from data port 0x%02X=%02X", portnum, *result);
			return 1;
			break;
		case 0x4B: //Second timer command?
			if (numPITchannels!=6) return 0; //Disabled?
			whichtimer = 1; //Second timer to use!
		case 0x43:
			*result = pitcommand[lastpit[whichtimer]]; //Give the last command byte!
			PIT_LOG("Read from command port 0x%02X=%02X", portnum, *result);
			return 1;
		case 0x61: //PC speaker? From original timer!
			*result = (PCSpeakerPort&3); //This is always present on all PCs!
			*result |= ((PITchannels[1].risetoggle&1)<<4)|((PITchannels[2].channel_status&1)<<5); //Give the channel outputs too! PIT1 output at bit 4(toggling), PIT0 status as bit 5!
			PIT_LOG("Read from misc port 0x%02X=%02X", portnum, *result);
			return 1;
		case 0x62: //Channel 2 on bit 5 of port 62 on XT!
			if (is_XT) //Extra data specified here too (XT only)?
			{
				*result = ((PITchannels[2].channel_status & 1) << 5); //Give this bit only!
				return 1; //Given!
			}
			break;
		default: //Unknown port?
			break; //Unknown port!
	}
	return 0; //Disabled!
}

byte out8254(word portnum, byte value)
{
	byte whichtimer=0;
	if (__HW_DISABLED) return 0; //Abort!
	byte pit;
	byte currentstatus; //For read back command!
	switch (portnum)
	{
		case 0x48:
		case 0x49:
		case 0x4A: //Second timer on Compaq?
			if (numPITchannels!=6) return 0; //Disabled?
			whichtimer = 1; //Second timer to use!
			pit = (byte)(portnum&0xFF);
			pit -= 0x48; //PIT!
			pit = secondPITmapping[pit]; //Map according to the second PIT!
			if (pit==0xFF) return 0; //Unmapped?
			goto applyOUTpit;
		case 0x40: //pit 0 data port
		case 0x41: //pit 1 data port
		case 0x42: //speaker data port
			pit = (byte)(portnum&0xFF);
			pit &= 3; //Low 2 bits only!
			applyOUTpit:
			pit += TimerBase[whichtimer]; //Convert to the correct timer (0-5)!
			PIT_LOG("Write to data port 0x%02X=%02X",portnum,value);
			switch (pitcommand[pit]&0x30) //What input mode currently?
			{
			default:
			case 0x10: //Lo mode?
				if (pitdecimal[pit])
				{
					pitdivisor[pit] = decodeBCD16(value & 0xFF);
				}
				else //Binary?
				{
					pitdivisor[pit] = (value & 0xFF);
				}
				PITchannels[pit].nullcount = 1; //We're not loaded into the divider yet!
				setPITFrequency(pit, pitdivisor[pit]); //Set the new divisor!
				break;
			case 0x20: //Hi mode?
				if (pitdecimal[pit])
				{
					pitdivisor[pit] = decodeBCD16((value & 0xFF)<<8);
				}
				else //Binary?
				{
					pitdivisor[pit] = ((value & 0xFF)<<8);
				}
				PITchannels[pit].nullcount = 1; //We're not loaded into the divider yet!
				setPITFrequency(pit, pitdivisor[pit]); //Set the new divisor!
				break;
			case 0x30: //Lo/hi mode?
				if (!pitcurrentlatch[pit][1])
				{
					if (pitdecimal[pit])
					{
						pitdivisor[pit] = decodeBCD16((value & 0xFF) | (encodeBCD16(pitdivisor[pit]) & 0xFF00));
					}
					else //Binary?
					{
						pitdivisor[pit] = (value & 0xFF) | (pitdivisor[pit] & 0xFF00);
					}
					PITchannels[pit].nullcount = 1; //We're not loaded into the divider yet!
					pitcurrentlatch[pit][1] = 1;
				}
				else
				{
					if (pitdecimal[pit])
					{
						pitdivisor[pit] = decodeBCD16(((value & 0xFF) << 8) | (encodeBCD16(pitdivisor[pit]) & 0xFF));
					}
					else //Binary?
					{
						pitdivisor[pit] = ((value & 0xFF) << 8) | (pitdivisor[pit] & 0xFF);
					}
					PITchannels[pit].nullcount = 1; //We're not loaded into the divider yet!
					pitcurrentlatch[pit][1] = 0;
					setPITFrequency(pit, pitdivisor[pit]); //Set the new divisor!
				}
				break;
			}
			return 1;
		case 0x4B: //Second timer command?
			if (numPITchannels!=6) return 0; //Disabled?			
			whichtimer = 1; //Second timer to use!
		case 0x43: //pit command port
			PIT_LOG("Write to command port 0x%02X=%02X", portnum, value);
			if ((value & 0xC0) == 0xC0) //Read-back command?
			{
				if ((value & 0x10)==0) //Latch status flag?
				{
					for (pit = 0;pit < 3;++pit) //Process all PITs!
					{
						if (value&(2<<pit)) //To use?
						{
							//Build status flag
							currentstatus = (pitcommand[pit+TimerBase[whichtimer]]&0x3F); //Init current status to data that's ready!
							currentstatus |= ((PITchannels[pit+TimerBase[whichtimer]].channel_status&1)<<7); //The current output!
							currentstatus |= ((PITchannels[pit+TimerBase[whichtimer]].nullcount&1)<<6); //Are we not loaded into the timer yet?
							readstatus[pit+TimerBase[whichtimer]] = 1; //Enable read of the status!
							statusbytes[pit+TimerBase[whichtimer]] = currentstatus; //Store the status byte!
						}
					}
				}
				if ((value & 0x20) == 0) //Latch count flag?
				{
					for (pit = 0;pit < 3;++pit) //Process all PITs!
					{
						if (value&(2 << pit)) //To use?
						{
							//Build status flag
							updatePITState(pit+TimerBase[whichtimer]); //Update the latch!
							readlatch[pit+TimerBase[whichtimer]] = 1; //Latch us, just once!
							pitcurrentlatch[pit+TimerBase[whichtimer]][0] = pitcurrentlatch[pit+TimerBase[whichtimer]][1] = 0; //Reset the latches always!
						}
					}
				}
			}
			else //Normal command?
			{
				byte channel;
				channel = (value >> 6);
				channel &= 3; //The channel!
				channel += TimerBase[whichtimer]; //Secondary channel support!
				if (value&0x30) //Not latching?
				{
					pitcommand[channel] = value; //Set the command for the port!
					setPITMode(channel,(value>>1)&7); //Update the PIT mode when needed!
					pitdecimal[channel] = (value&1); //Set the decimal mode if requested!
					readlatch[channel] = 0; //Not latching anymore!
				}
				else //Latch count value?
				{
					updatePITState(channel); //Update the latch!
					readlatch[channel] = 1; //Latch us, just once!
				}
				lastpit[whichtimer] = channel; //The last channel effected!
				pitcurrentlatch[channel][0] = pitcurrentlatch[channel][1] = 0; //Reset the latches always!
			}
			return 1;
		//From above original:
	case 0x61: //PC Speaker?
		PIT_LOG("Write to misc port 0x%02X=%02X", portnum, value);
		PCSpeakerPort = (value&3); //Set the new port value, only low 2 bits are changed!
		speakerGateUpdated(); //Gate has been updated!
		return 1;
	default:
		break;
	}
	return 0; //Unhandled!
}

extern byte is_XT; //Are we emulating a XT architecture?

void PIT0Acnowledge(byte IRQ)
{
	if (is_XT) //Non-AT?
	{
		acnowledgeIRQrequest(0); //Acnowledge us!
	}
}

extern byte is_Compaq; //Are we emulating an Compaq architecture?

void init8253() {
	if (__HW_DISABLED) return; //Abort!
	register_PORTOUT(&out8254);
	register_PORTIN(&in8254);

	#ifdef IS_LONGDOUBLE
	speaker_tick = (1000000000.0L / (DOUBLE)SPEAKER_RATE); //Speaker tick!
	ticklength = (1.0L / SPEAKER_RATE)*TIME_RATE; //Time to speaker sample ratio!
	#else
	speaker_tick = (1000000000.0 / (DOUBLE)SPEAKER_RATE); //Speaker tick!
	ticklength = (1.0 / SPEAKER_RATE)*TIME_RATE; //Time to speaker sample ratio!
	#endif
	registerIRQ(0,&PIT0Acnowledge,NULL); //Register our acnowledge IRQ!
	PCSpeakerPort = 0; //Init PC speaker port!
	if ((is_Compaq==1) || (is_i430fx)) //Emulating Compaq architecture?
	{
		numPITchannels = 6; //We're emulating all 6 channels instead!
	}
	else
	{
		numPITchannels = 3; //We're emulating base XT+ 3 channel PIT!
	}
}
