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
#include "headers/support/sf2.h" //Soundfont support!
#include "headers/hardware/midi/mididevice.h" //Our own typedefs!
#include "headers/support/zalloc.h" //Zero allocation support!
#include "headers/emu/sound.h" //Sound support!
#include "headers/support/log.h" //Logging support!
#include "headers/support/highrestimer.h" //High resolution timer support!
#include "headers/hardware/midi/adsr.h" //ADSR support!
#include "headers/emu/timers.h" //Use timers for Active Sensing!
#include "headers/support/locks.h" //Locking support!
#include "headers/support/signedness.h"

//Use direct windows MIDI processor if available?

//Our volume to use!
#define MIDI_VOLUME 100.0f

//Effective volume vs samples!
#define VOLUME 1.0f

#ifdef IS_WINDOWS
#include <mmsystem.h>  /* multimedia functions (such as MIDI) for Windows */
#endif

#ifdef IS_PSP
//The PSP doesn't have enough memory to handle reverb(around 1.9MB on reverb buffers)
//#define DISABLE_REVERB
#endif

//Are we disabled?
//#define __HW_DISABLED
RIFFHEADER *soundfont; //Our loaded soundfont!

//To log MIDI commands?
//#define MIDI_LOG

byte direct_midi = 0; //Enable direct MIDI synthesis?

//On/off controller bit values!
#define MIDI_CONTROLLER_ON 0x40

//Poly and Omni flags in the Mode Selection.
//Poly: Enable multiple voices per channel. When set to Mono, All Notes Off on the channel when a Note On is received.
#define MIDIDEVICE_POLY 0x1
//Omni: Ignore channel number of the message during note On/Off commands.
#define MIDIDEVICE_OMNI 0x2

//Default mode is Omni Off, Poly
#define MIDIDEVICE_DEFAULTMODE MIDIDEVICE_POLY

//Reverb delay in seconds (originally 250ms(19MB), now 50ms(960384 bytes of buffers))
#define REVERB_DELAY 0.00265f

//Chorus delay in seconds (5ms)
#define CHORUS_DELAY 0.005f

//Chorus LFO Frequency (5Hz)
#define CHORUS_LFO_FREQUENCY 5.0f

//Chorus LFO Strength (cents) sharp
#define CHORUS_LFO_CENTS 10.0f

//16/32 bit quantities from the SoundFont loaded in memory!
#define LE16(x) SDL_SwapLE16(x)
#define LE32(x) SDL_SwapLE32(x)
#define LE16S(x) = unsigned2signed16(LE16(signed2unsigned16(x)))
#define LE32S(x) = unsigned2signed32(LE32(signed2unsigned32(x)))

float reverb_delay[0x100];
float chorus_delay[0x100];
float choruscents[2];

MIDIDEVICE_CHANNEL MIDI_channels[0x10]; //Stuff for all channels!

MIDIDEVICE_VOICE activevoices[MIDI_TOTALVOICES]; //All active voices!

/* MIDI direct output support*/

#ifdef IS_WINDOWS
int flag;           // monitor the status of returning functions
HMIDIOUT device;    // MIDI device interface for sending MIDI output
#endif

OPTINLINE void lockMPURenderer()
{
}

OPTINLINE void unlockMPURenderer()
{
}

/* Reset support */

OPTINLINE void reset_MIDIDEVICE() //Reset the MIDI device for usage!
{
	//First, our variables!
	byte channel,chorusreverbdepth;
	word notes;
	byte purposebackup;
	byte allocatedbackup;
	FIFOBUFFER *temp, *temp2, *temp3, *temp4, *chorus_backtrace[CHORUSSIZE];

	lockMPURenderer();
	memset(&MIDI_channels,0,sizeof(MIDI_channels)); //Clear our data!

	for (channel=0;channel<NUMITEMS(activevoices);channel++) //Process all voices!
	{
		temp = activevoices[channel].effect_backtrace_samplespeedup_modenv_pitchfactor; //Back-up the effect backtrace!
		temp2 = activevoices[channel].effect_backtrace_LFO1; //Back-up the effect backtrace!
		temp3 = activevoices[channel].effect_backtrace_LFO2; //Back-up the effect backtrace!
		temp4 = activevoices[channel].effect_backtrace_lowpassfilter_modenvfactor; //Backup the effect backtrace!
		for (chorusreverbdepth=0;chorusreverbdepth<CHORUSSIZE;++chorusreverbdepth)
		{
			chorus_backtrace[chorusreverbdepth] = activevoices[channel].effect_backtrace_chorus[chorusreverbdepth]; //Back-up!
		}
		purposebackup = activevoices[channel].purpose;
		allocatedbackup = activevoices[channel].allocated;
		memset(&activevoices[channel],0,sizeof(activevoices[channel])); //Clear the entire channel!
		activevoices[channel].allocated = allocatedbackup;
		activevoices[channel].purpose = purposebackup;
		for (chorusreverbdepth=0;chorusreverbdepth<CHORUSSIZE;++chorusreverbdepth)
		{
			activevoices[channel].effect_backtrace_chorus[chorusreverbdepth] = chorus_backtrace[chorusreverbdepth]; //Restore!
		}
		activevoices[channel].effect_backtrace_lowpassfilter_modenvfactor = temp4; //Restore our buffer!
		activevoices[channel].effect_backtrace_LFO2 = temp3; //Restore our buffer!
		activevoices[channel].effect_backtrace_LFO1 = temp2; //Restore our buffer!
		activevoices[channel].effect_backtrace_samplespeedup_modenv_pitchfactor = temp; //Restore our buffer!
	}

	for (channel=0;channel<0x10;)
	{
		for (notes=0;notes<0x100;)
		{
			MIDI_channels[channel].notes[notes].channel = channel;
			MIDI_channels[channel].notes[notes].note = (byte)notes;

			//Also apply delays while we're at it(also 256 values)!
			reverb_delay[notes] = REVERB_DELAY*(float)notes; //The reverb delay to use for this stream!
			chorus_delay[notes] = CHORUS_DELAY*(float)notes; //The chorus delay to use for this stream!

			++notes; //Next note!
		}
		memset(&MIDI_channels[channel].ContinuousControllers,0,sizeof(MIDI_channels[0].ContinuousControllers)); //Reset unused!
		MIDI_channels[channel].bank = MIDI_channels[channel].activebank = 0; //Reset!
		MIDI_channels[channel].control = 0; //First instrument!
		MIDI_channels[channel].pitch = 0x2000; //Centered pitch = Default pitch!
		MIDI_channels[channel].pressure = 0x40; //Centered pressure!
		MIDI_channels[channel].program = 0; //First program!
		MIDI_channels[channel].sustain = 0; //Disable sustain!
		MIDI_channels[channel].ContinuousControllers[0x07] = 0x64; //Same as below!
		MIDI_channels[channel].volumeMSB = 0x64; //Default volume as the default volume(100)!
		MIDI_channels[channel].ContinuousControllers[0x27] = 0x7F; //Same as below!
		MIDI_channels[channel].volumeLSB = 0x7F; //Default volume as the default volume(127?)!
		MIDI_channels[channel].ContinuousControllers[0x0B] = 0x7F; //Same as below!
		MIDI_channels[channel].expression = 0x7F; //Default volume as the default max expression(127)!
		MIDI_channels[channel].panposition = (0x20<<7); //Centered pan position as the default pan!
		MIDI_channels[channel].lvolume = MIDI_channels[channel].rvolume = 0.5; //Accompanying the pan position: centered volume!
		MIDI_channels[channel].RPNmode = 0; //No (N)RPN selected!
		MIDI_channels[channel].pitchbendsensitivitysemitones = 2; //2 semitones of ...
		MIDI_channels[channel].pitchbendsensitivitycents = 0; //... default pitch bend?
		MIDI_channels[channel].sostenuto = 0; //No sostenuto yet!
		MIDI_channels[channel++].mode = MIDIDEVICE_DEFAULTMODE; //Use the default mode!
	}
	MIDI_channels[MIDI_DRUMCHANNEL].bank = MIDI_channels[MIDI_DRUMCHANNEL].activebank = 0x80; //We're locked to a drum set!
	unlockMPURenderer();
}

/*

Cents and DB conversion!

*/

//Low pass filters!

OPTINLINE float modulateLowpass(MIDIDEVICE_VOICE *voice, float Modulation, float LFOmodulation, float lowpassfilter_modenvfactor, byte filterindex)
{
	INLINEREGISTER float modulationratio;

	modulationratio = Modulation*lowpassfilter_modenvfactor; //The modulation ratio to use!

	//Now, translate the modulation ratio to samples, optimized!
	modulationratio = floorf(modulationratio); //Round it down to get integer values to optimize!
	modulationratio += LFOmodulation; //Apply the LFO modulation as well!
	if (modulationratio!=voice->lowpass_modulationratio[filterindex]) //Different ratio?
	{
		voice->lowpass_modulationratio[filterindex] = modulationratio; //Update the last ratio!
		modulationratio = voice->lowpass_modulationratiosamples[filterindex] = cents2samplesfactorf(modulationratio)*voice->lowpassfilter_freq; //Calculate the pitch bend and modulation ratio to apply!
		voice->lowpass_dirty[filterindex] = 3; //We're a dirty low-pass filter!
	}
	else
	{
		modulationratio = voice->lowpass_modulationratiosamples[filterindex]; //We're the same as last time!
	}
	return modulationratio; //Give the frequency to use for the low pass filter!
}

OPTINLINE void applyMIDILowpassFilter(MIDIDEVICE_VOICE *voice, byte rchannel, float *currentsample, float Modulation, float LFOmodulation, float lowpassfilter_modenvfactor, byte filterindex)
{
	float lowpassfilterfreq;
	if (voice->lowpassfilter_freq==0) return; //No filter?
	lowpassfilterfreq = modulateLowpass(voice,Modulation,LFOmodulation,lowpassfilter_modenvfactor,filterindex); //Load the frequency to use for low-pass filtering!
	if (voice->lowpass_dirty[filterindex]&(1<<rchannel)) //Are we dirty? We need to update the low-pass filter, if so!
	{		
		updateSoundFilter(&voice->lowpassfilter[filterindex][rchannel],0,lowpassfilterfreq,(float)LE32(voice->sample[1].dwSampleRate)); //Update the low-pass filter, when needed!
		voice->lowpass_dirty[filterindex] &= ~(1<<rchannel); //We're not dirty anymore!
	}
	applySoundFilter(&voice->lowpassfilter[filterindex][rchannel], currentsample); //Apply a low pass filter!
}

OPTINLINE void applyMIDIReverbFilter(MIDIDEVICE_VOICE *voice, float *currentsample, byte filterindex)
{
	applySoundFilter(&voice->reverbfilter[filterindex], currentsample); //Apply a low pass filter!
}

/*

Voice support

*/

//How many steps to keep!
#define SINUSTABLE_PERCISION 3600
#define SINUSTABLE_PERCISION_FLT 3600.0f
#define SINUSTABLE_PERCISION_REVERSE ((1.0f/(2.0f*PI))*3600.0f)

int_32 chorussinustable[SINUSTABLE_PERCISION][2][2]; //10x percision steps of sinus! With 1.0 added always!
float genericsinustable[SINUSTABLE_PERCISION][2]; //10x percision steps of sinus! With 1.0 added always!
float sinustable_percision_reverse = 1.0f; //Reverse lookup!

void MIDIDEVICE_generateSinusTable()
{
	word x;
	float y,z;
	byte choruschannel;
	for (x=0;x<NUMITEMS(chorussinustable);++x)
	{
		for (choruschannel=0;choruschannel<2;++choruschannel) //All channels!
		{
			genericsinustable[x][0] = sinf((float)(((float)x / SINUSTABLE_PERCISION_FLT)) * 2.0f * PI); //Raw sinus!

			z = (float)(x/SINUSTABLE_PERCISION_FLT); //Start point!
			if (x >= (SINUSTABLE_PERCISION_FLT * 0.5)) //Negative?
			{
				y = -1.0f; //Negative!
				z -= 0.5f; //Make us the first half!
			}
			else //Positive?
			{
				y = 1.0f; //Positive!
			}

			if (z >= 0.25) //Lowering?
			{
				z -= 0.25f; //Quarter!
				genericsinustable[x][1] = (1.0f-(z*4.0f))*y;
			}
			else //Raising?
			{
				genericsinustable[x][1] = (z * 4.0f) * y;
				if ((z == 0.0f) && (y == -1.0f)) //Negative zero?
				{
					genericsinustable[x][1] = 0.0f; //Just zero!
				}
			}

			chorussinustable[x][choruschannel][0] = (int_32)((sinf((float)(((float)x/SINUSTABLE_PERCISION_FLT))*2.0f*PI)+1.0f)*choruscents[choruschannel]); //Generate sinus lookup table, negative!
			chorussinustable[x][choruschannel][1] = (int_32)(chorussinustable[x][choruschannel][0]+1200.0f); //Generate sinus lookup table, with cents base added, negative!
		}
	}
	sinustable_percision_reverse = SINUSTABLE_PERCISION_REVERSE; //Our percise value, reverse lookup!
}

//Absolute to get the amount of degrees, converted to a -1.0 to 1.0 scale!
#define MIDIDEVICE_chorussinf(value, choruschannel, add1200centsbase) chorussinustable[(uint_32)(value)][choruschannel][add1200centsbase]
#define MIDIDEVICE_genericsinf(value,type) genericsinustable[(uint_32)(value)][type]

//MIDIvolume: converts a value of the range of maxvalue to a linear volume factor using maxdB dB.
OPTINLINE float MIDIattenuate(float value)
{
	return (float)powf(10.0f, value / -200.0f); //Generate default attenuation!
}

float attenuationprecalcs[1441]; //Precalcs for attenuation!
void calcAttenuationPrecalcs()
{
	word n;
	for (n = 0; n < NUMITEMS(attenuationprecalcs); ++n)
	{
		attenuationprecalcs[n] = MIDIattenuate((float)n); //Precakc!
	}
}

/*

combineAttenuation:

Combine attenuation values with each other to a new single scale.
	
*/
OPTINLINE float combineAttenuation(MIDIDEVICE_VOICE* voice, float initialAttenuation, float volumeEnvelope)
{
	float attenuation;
	//First, clip!

	if ((voice->last_initialattenuation == initialAttenuation) && (voice->last_volumeenvelope == volumeEnvelope)) //Unchanged?
	{
		return voice->last_attenuation; //Give the last attenuation!
	}
	voice->last_initialattenuation = initialAttenuation; //Last!
	voice->last_volumeenvelope = volumeEnvelope; //Last!

	attenuation = initialAttenuation + (volumeEnvelope * 960.0f); //What is the attenuation!

	if (attenuation > 1440.0f) attenuation = 1440.0f; //Limit to max!
	if (attenuation < 0.0f) attenuation = 0.0f; //Limit to min!
	//Now, combine! Normalize, convert to gain(in relative Bels), combine, convert to attenuation and apply the new scale for the attenuate function.
	return (voice->last_attenuation = attenuationprecalcs[(word)(attenuation)]); //Volume needs to be converted to a 960cB range!
}

void MIDIDEVICE_getsample(int_64 play_counter, uint_32 totaldelay, float samplerate, int_32 samplespeedup, MIDIDEVICE_VOICE *voice, float Volume, float Modulation, byte chorus, float chorusvol, byte filterindex, float LFOpitch, float LFOvolume, float LFOfiltercutoff, int_32 *lchannelres, int_32 *rchannelres) //Get a sample from an MIDI note!
{
	//Our current rendering routine:
	INLINEREGISTER uint_32 temp;
	sword mutesample[2] = { ~0, 0 }; //Mute this sample?
	int_64 samplepos[2];
	float lchannel, rchannel; //Both channels to use!
	byte loopflags[2]; //Flags used during looping!
	byte totalloopflags;
	static sword readsample[2]= {0,0}; //The sample retrieved!
	int_32 modulationratiocents;
	uint_32 tempbuffer,tempbuffer2;
	float tempbufferf, tempbufferf2;
	int_32 modenv_pitchfactor;
	float currentattenuation;
	int_64 samplesskipped;
	float lowpassfilter_modenvfactor;

	modenv_pitchfactor = voice->modenv_pitchfactor; //The current pitch factor!
	lowpassfilter_modenvfactor = voice->lowpassfilter_modenvfactor; //Mod env factor!

	if (filterindex==0) //Main channel? Log the current sample speedup!
	{
		writefifobuffer32_2u(voice->effect_backtrace_samplespeedup_modenv_pitchfactor,signed2unsigned32(samplespeedup),signed2unsigned32(modenv_pitchfactor)); //Log a history of this!
		writefifobufferflt_2(voice->effect_backtrace_LFO1, LFOpitch, LFOvolume); //Log a history of this!
		writefifobufferflt(voice->effect_backtrace_LFO2, LFOfiltercutoff); //Log a history of this!
		writefifobufferflt(voice->effect_backtrace_lowpassfilter_modenvfactor, lowpassfilter_modenvfactor); //Log a history of this!
	}
	else if (likely(play_counter >= 0)) //Are we a running channel that needs reading back?
	{
		if (likely(readfifobuffer32_backtrace_2u(voice->effect_backtrace_samplespeedup_modenv_pitchfactor, &tempbuffer, &tempbuffer2, totaldelay, voice->isfinalchannel_chorus[filterindex]))) //Try to read from history! Only apply the value when not the originating channel!
		{
			samplespeedup = unsigned2signed32(tempbuffer); //Apply the sample speedup from that point in time! Not for the originating channel!
			modenv_pitchfactor = unsigned2signed32(tempbuffer2); //Apply the pitch factor from that point in time! Not for the originating channel!
		}
		if (likely(readfifobufferflt_backtrace_2(voice->effect_backtrace_LFO1, &tempbufferf, &tempbufferf2, totaldelay, voice->isfinalchannel_chorus[filterindex]))) //Try to read from history! Only apply the value when not the originating channel!
		{
			LFOpitch = tempbufferf; //Apply the same from that point in time! Not for the originating channel!
			LFOvolume = tempbufferf2; //Apply the same from that point in time! Not for the originating channel!
		}
		if (likely(readfifobufferflt_backtrace(voice->effect_backtrace_LFO2, &tempbufferf, totaldelay, voice->isfinalchannel_chorus[filterindex]))) //Try to read from history! Only apply the value when not the originating channel!
		{
			LFOfiltercutoff = tempbufferf; //Apply the same from that point in time! Not for the originating channel!
		}
		if (likely(readfifobufferflt_backtrace(voice->effect_backtrace_lowpassfilter_modenvfactor, &tempbufferf, totaldelay, voice->isfinalchannel_chorus[filterindex]))) //Try to read from history! Only apply the value when not the originating channel!
		{
			lowpassfilter_modenvfactor = tempbufferf; //Apply the same from that point in time! Not for the originating channel!
		}
	}
	if (unlikely(play_counter < 0)) //Invalid to lookup the position?
	{
	#ifndef DISABLE_REVERB
		goto finishedsample;
	#else
		return; //Abort: nothing to do!
	#endif
	}

	//Valid to play?
	modulationratiocents = 0; //Default: none!
	if (chorus) //Chorus extension channel?
	{
		modulationratiocents = MIDIDEVICE_chorussinf(voice->chorussinpos[filterindex], chorus, 0); //Pitch bend default!
		voice->chorussinpos[filterindex] += voice->chorussinposstep; //Step by one sample rendered!
		if (unlikely(voice->chorussinpos[filterindex] >= SINUSTABLE_PERCISION_FLT)) voice->chorussinpos[filterindex] = fmodf(voice->chorussinpos[filterindex],SINUSTABLE_PERCISION_FLT); //Wrap around when needed(once per second)!
	}

	modulationratiocents += LFOpitch; //Apply the LFO inputs for affecting pitch!

	modulationratiocents += (Modulation * voice->modenv_pitchfactor); //Apply pitch bend as well!
	//Apply pitch bend to the current factor too!
	modulationratiocents += samplespeedup; //Speedup according to pitch bend!

	//Apply the new modulation ratio, if needed!
	if (modulationratiocents != voice->modulationratiocents[filterindex]) //Different ratio?
	{
		voice->modulationratiocents[filterindex] = modulationratiocents; //Update the last ratio!
		voice->modulationratiosamples[filterindex] = cents2samplesfactord((DOUBLE)modulationratiocents); //Calculate the pitch bend and modulation ratio to apply!
	}

	samplepos[0] = samplepos[1] = voice->monotonecounter[filterindex]; //Monotone counter!
	voice->monotonecounter_diff[filterindex] += (voice->modulationratiosamples[filterindex]); //Apply the pitch bend and other modulation data to the sample to retrieve!
	samplesskipped = (int_64)voice->monotonecounter_diff[filterindex]; //Load the samples skipped!
	voice->monotonecounter_diff[filterindex] -= (float)samplesskipped; //Remainder!
	voice->monotonecounter[filterindex] += samplesskipped; //Skipped this amount of samples ahead!

	//Now, calculate the start offset to start looping!
	samplepos[0] += voice->startaddressoffset[0]; //The start of the sample!
	samplepos[1] += voice->startaddressoffset[1]; //The start of the sample!

	//First: apply looping! Left!
	loopflags[0] = voice->currentloopflags[0];

	if (voice->has_finallooppos[0] && (play_counter >= voice->finallooppos[0])) //Executing final loop?
	{
		samplepos[0] -= voice->finallooppos[0]; //Take the relative offset to the start of the final loop!
		samplepos[0] += voice->finallooppos_playcounter[0]; //Add the relative offset to the start of our data of the final loop!
	}
	else if (loopflags[0] & 1) //Currently looping and active?
	{
		if (samplepos[0] >= voice->endloopaddressoffset[0]) //Past/at the end of the loop!
		{
			if ((loopflags[0] & 0xD2) == 0x82) //We're depressed, depress action is allowed (not holding) and looping until depressed?
			{
				if (!voice->has_finallooppos[0]) //No final loop position set yet?
				{
					voice->currentloopflags[0] &= ~0x80; //Clear depress bit!
					//Loop for the last time!
					voice->finallooppos[0] = samplepos[0]; //Our new position for our final execution till the end!
					voice->has_finallooppos[0] = 1; //We have a final loop position set!
					loopflags[0] |= 0x20; //We're to update our final loop start!
				}
			}

			//Loop according to loop data!
			temp = voice->startloopaddressoffset[0]; //The actual start of the loop!
			//Loop the data!
			samplepos[0] -= temp; //Take the ammount past the start of the loop!
			samplepos[0] %= voice->loopsize[0]; //Loop past startloop by endloop!
			samplepos[0] += temp; //The destination position within the loop!
			//Check for depress special actions!
			if (loopflags[0]&0x20) //Extra information needed for the final loop?
			{
				voice->finallooppos_playcounter[0] = samplepos[0]; //The start position within the loop to use at this point in time!
			}
		}
	}

	//First: apply looping! Right!
	loopflags[1] = voice->currentloopflags[1];

	if (voice->has_finallooppos[1] && (play_counter >= voice->finallooppos[1])) //Executing final loop?
	{
		samplepos[1] -= voice->finallooppos[1]; //Take the relative offset to the start of the final loop!
		samplepos[1] += voice->finallooppos_playcounter[1]; //Add the relative offset to the start of our data of the final loop!
	}
	else if (loopflags[1] & 1) //Currently looping and active?
	{
		if (samplepos[1] >= voice->endloopaddressoffset[1]) //Past/at the end of the loop!
		{
			if ((loopflags[1] & 0xD2) == 0x82) //We're depressed, depress action is allowed (not holding) and looping until depressed?
			{
				if (!voice->has_finallooppos[1]) //No final loop position set yet?
				{
					voice->currentloopflags[1] &= ~0x80; //Clear depress bit!
					//Loop for the last time!
					voice->finallooppos[1] = samplepos[1]; //Our new position for our final execution till the end!
					voice->has_finallooppos[1] = 1; //We have a final loop position set!
					loopflags[1] |= 0x20; //We're to update our final loop start!
				}
			}

			//Loop according to loop data!
			temp = voice->startloopaddressoffset[1]; //The actual start of the loop!
			//Loop the data!
			samplepos[1] -= temp; //Take the ammount past the start of the loop!
			samplepos[1] %= voice->loopsize[1]; //Loop past startloop by endloop!
			samplepos[1] += temp; //The destination position within the loop!
			//Check for depress special actions!
			if (loopflags[1]&0x20) //Extra information needed for the final loop?
			{
				voice->finallooppos_playcounter[1] = samplepos[1]; //The start position within the loop to use at this point in time!
			}
		}
	}

	//Next, apply finish!
	totalloopflags = (samplepos[0] >= voice->endaddressoffset[0]) | ((samplepos[1] >= voice->endaddressoffset[1])<<1); //Expired or not started yet?
	#ifndef DISABLE_REVERB
	if (totalloopflags==3) goto finishedsample;
	#else
	if (totalloopflags==3) goto return;
	#endif

	if (likely(
			((getSFSample16(soundfont, (uint_32)samplepos[0], &readsample[0]))|(totalloopflags&1)) || //Left sample?
			((getSFSample16(soundfont, (uint_32)samplepos[1], &readsample[1]))|(totalloopflags&2)) //Right sample?
			)) //Sample found?
	{
		readsample[0] &= mutesample[totalloopflags & 1]; //Mute left sample, if needed!
		readsample[1] &= mutesample[(totalloopflags >> 1) & 1]; //Mute left sample, if needed!
		lchannel = (float)readsample[0]; //Convert to floating point for our calculations!
		rchannel = (float)readsample[1]; //Convert to floating point for our calculations!

		//First, apply filters and current envelope!
		applyMIDILowpassFilter(voice, 0, &lchannel, Modulation, LFOfiltercutoff, lowpassfilter_modenvfactor, filterindex); //Low pass filter!
		applyMIDILowpassFilter(voice, 1, &rchannel, Modulation, LFOfiltercutoff, lowpassfilter_modenvfactor, filterindex); //Low pass filter!
		currentattenuation = combineAttenuation(voice,voice->effectiveAttenuation+LFOvolume,Volume); //The volume of the samples including ADSR!
		currentattenuation *= chorusvol; //Apply chorus&reverb volume for this stream!
		currentattenuation *= VOLUME; //Apply general volume!
		lchannel *= currentattenuation; //Apply the current attenuation!
		rchannel *= currentattenuation; //Apply the current attenuation!
		//Now the sample is ready for output into the actual final volume!

		//Now, apply panning!
		lchannel *= voice->lvolume; //Apply left panning, also according to the CC!
		rchannel *= voice->rvolume; //Apply right panning, also according to the CC!

		#ifndef DISABLE_REVERB
		writefifobufferflt_2(voice->effect_backtrace_chorus[filterindex],lchannel,rchannel); //Left/right channel output!
		#endif

		*lchannelres += (int_32)lchannel; //Apply the immediate left channel!
		*rchannelres += (int_32)rchannel; //Apply the immedaite right channel!
	}
	#ifndef DISABLE_REVERB
	else
	{
	finishedsample: //loopflags set?
		writefifobufferflt_2(voice->effect_backtrace_chorus[filterindex],0.0f,0.0f); //Left/right channel output!
	}
	#endif
}

void MIDIDEVICE_calcLFOoutput(MIDIDEVICE_LFO* LFO, byte type)
{
	float rawoutput;
	if (LFO->delay) //Delay left?
	{
		--LFO->delay;
		LFO->outputfiltercutoff = 0.0f; //No output!
		LFO->outputpitch = 0.0f; //No output!
		LFO->outputvolume = 0.0f; //No output!
		return; //Don't update the LFO yet!
	}
	rawoutput = MIDIDEVICE_genericsinf(LFO->sinpos,type); //Raw output of the LFO!
	LFO->outputfiltercutoff = (float)LFO->tofiltercutoff*rawoutput; //Unbent sinus output for filter cutoff!
	LFO->outputpitch = (float)LFO->topitch * rawoutput; //Unbent sinus output for pitch!
	LFO->outputvolume = (float)LFO->tovolume * rawoutput; //Unbent sinus output for volume!
}

OPTINLINE void MIDIDEVICE_tickLFO(MIDIDEVICE_LFO* LFO)
{
	LFO->sinpos += LFO->sinposstep; //Step by one sample rendered!
	if (unlikely(LFO->sinpos >= SINUSTABLE_PERCISION_FLT))
	{
		LFO->sinpos = fmodf(LFO->sinpos, SINUSTABLE_PERCISION_FLT); //Wrap around when needed(once per second)!
	}
}

byte MIDIDEVICE_renderer(void* buf, uint_32 length, byte stereo, void *userdata) //Sound output renderer!
{
#ifdef __HW_DISABLED
	return 0; //We're disabled!
#endif
	if (!stereo) return 0; //Can't handle non-stereo output!
	//Initialisation info
	float lvolume, rvolume, panningtemp;
	float VolumeEnvelope=0; //Current volume envelope data!
	float ModulationEnvelope=0; //Current modulation envelope data!
	//Initialised values!
	MIDIDEVICE_VOICE *voice = (MIDIDEVICE_VOICE *)userdata;
	sample_stereo_t* ubuf = (sample_stereo_t *)buf; //Our sample buffer!
	ADSR *VolumeADSR = &voice->VolumeEnvelope; //Our used volume envelope ADSR!
	ADSR *ModulationADSR = &voice->ModulationEnvelope; //Our used modulation envelope ADSR!
	MIDIDEVICE_CHANNEL *channel = voice->channel; //Get the channel to use!
	uint_32 numsamples = length; //How many samples to buffer!
	byte currentchorusreverb; //Current chorus and reverb levels we're processing!
	int_64 chorusreverbsamplepos;

	#ifdef MIDI_LOCKSTART
	//lock(voice->locknumber); //Lock us!
	#endif

	if (voice->active==0) //Simple check!
	{
		#ifdef MIDI_LOCKSTART
		//unlock(voice->locknumber); //Lock us!
		#endif
		return SOUNDHANDLER_RESULT_NOTFILLED; //Empty buffer: we're unused!
	}
	if (memprotect(soundfont,sizeof(*soundfont),"RIFF_FILE")!=soundfont)
	{
		#ifdef MIDI_LOCKSTART
		//unlock(voice->locknumber); //Lock us!
		#endif
		return SOUNDHANDLER_RESULT_NOTFILLED; //Empty buffer: we're unable to render anything!
	}
	if (!soundfont)
	{
		#ifdef MIDI_LOCKSTART
		//unlock(voice->locknumber); //Lock us!
		#endif
		return SOUNDHANDLER_RESULT_NOTFILLED; //The same!
	}
	if (!channel) //Unknown channel?
	{
		#ifdef MIDI_LOCKSTART
		//unlock(voice->locknumber); //Lock us!
		#endif
		return SOUNDHANDLER_RESULT_NOTFILLED; //The same!
	}


	#ifdef MIDI_LOCKSTART
	lock(voice->locknumber); //Actually check!
	#endif

	//Determine panning!
	lvolume = rvolume = 0.5f; //Default to 50% each (center)!
	panningtemp = voice->initpanning; //Get the panning specified!
	panningtemp += voice->panningmod; //Apply panning CC!
	lvolume -= panningtemp; //Left percentage!
	rvolume += panningtemp; //Right percentage!
	lvolume = LIMITRANGE(lvolume, 0.0f, 1.0f); //Limit!
	rvolume = LIMITRANGE(rvolume, 0.0f, 1.0f); //Limit!
	voice->lvolume = lvolume; //Left panning!
	voice->rvolume = rvolume; //Right panning!

	if (voice->request_off) //Requested turn off?
	{
		voice->currentloopflags[0] |= 0x80; //Request quit looping if needed: finish sound!
		voice->currentloopflags[1] |= 0x80; //Request quit looping if needed: finish sound!
	} //Requested off?

	//Apply sustain
	voice->currentloopflags[0] &= ~0x40; //Sustain disabled by default!
	voice->currentloopflags[0] |= (channel->sustain << 6); //Sustaining?
	voice->currentloopflags[1] &= ~0x40; //Sustain disabled by default!
	voice->currentloopflags[1] |= (channel->sustain << 6); //Sustaining?

	VolumeEnvelope = voice->CurrentVolumeEnvelope; //Make sure we don't clear!
	ModulationEnvelope = voice->CurrentModulationEnvelope; //Make sure we don't clear!

	int_32 lchannel, rchannel; //Left&right samples, big enough for all chorus and reverb to be applied!
	float channelsamplel, channelsampler; //A channel sample!

	float samplerate = (float)LE32(voice->sample[1].dwSampleRate); //The samplerate we use!

	byte chorus,reverb;
	uint_32 totaldelay;
	float tempstorage;
	byte activechannel, currentactivefinalchannel; //Are we an active channel?

	//Now produce the sound itself!
	do //Produce the samples!
	{
		lchannel = 0; //Reset left channel!
		rchannel = 0; //Reset right channel!
		currentchorusreverb=0; //Init to first chorus channel!
		MIDIDEVICE_calcLFOoutput(&voice->LFO[0],0); //Calculate the first LFO!
		MIDIDEVICE_calcLFOoutput(&voice->LFO[1],1); //Calculate the second LFO!
		do //Process all chorus used(2 chorus channels)!
		{
			chorusreverbsamplepos = voice->play_counter; //Load the current play counter!
			totaldelay = voice->chorusdelay[currentchorusreverb]; //Load the total delay!
			chorusreverbsamplepos -= (int_64)totaldelay; //Apply specified chorus&reverb delay!
			VolumeEnvelope = 1.0f-(ADSR_tick(VolumeADSR,chorusreverbsamplepos,((voice->currentloopflags[1] & 0xD0) != 0x80),voice->note->noteon_velocity, voice->note->noteoff_velocity)); //Apply Volume Envelope, converted to attenuation!
			ModulationEnvelope = (ADSR_tick(ModulationADSR,chorusreverbsamplepos,((voice->currentloopflags[1] & 0xD0) != 0x80),voice->note->noteon_velocity, voice->note->noteoff_velocity)); //Apply Modulation Envelope, converted to attenuation!
			if (!currentchorusreverb) //The first?
			{
				voice->CurrentVolumeEnvelope = VolumeEnvelope; //Current volume!
				voice->CurrentModulationEnvelope = ModulationEnvelope; //Current modulation!
			}
			MIDIDEVICE_getsample(chorusreverbsamplepos, totaldelay, samplerate, voice->effectivesamplespeedup, voice, VolumeEnvelope, ModulationEnvelope, currentchorusreverb, voice->chorusvol[currentchorusreverb], currentchorusreverb, voice->LFO[0].outputpitch+voice->LFO[1].outputpitch, voice->LFO[0].outputvolume+voice->LFO[1].outputvolume, voice->LFO[0].outputfiltercutoff+voice->LFO[1].outputfiltercutoff, &lchannel, &rchannel); //Get the sample from the MIDI device, with only the chorus effect!
		} while (++currentchorusreverb<CHORUSSIZE); //Chorus loop.

		if (unlikely((VolumeADSR->active==ADSR_IDLE) && (voice->noteplaybackfinished==0))) //To finish note with chorus?
		{
			voice->noteplaybackfinished = 1; //Finish note!
			voice->finishnoteleft = voice->reverbdelay[REVERBSIZE-1]; //How long for any delay to be left?
		}
		else if (voice->noteplaybackfinished) //Counting down finish timer?
		{
			if (voice->finishnoteleft) //Anything left?
			{
				if ((--voice->finishnoteleft) == 0) //Finished reverb?
				{
					voice->active = 0; //Finish the voice: nothing is left to be rendered!
				}
			}
		}

		//Apply reverb based on chorus history now!
		#ifndef DISABLE_REVERB
		#if CHORUSSIZE==1
		//Only 1 chorus channel?
		chorus = 0; //Init chorus number!
		reverb = 1; //First reverberation to apply!
		#else
		chorus = 0; //Init chorus number!
		reverb = 1; //First reverberation to apply!
		#endif
		tempstorage = VolumeEnvelope; //Store for temporary storage!
		activechannel = (chorusreverbsamplepos>=0); //Are we an active channel?
		do //Process all reverb used(2 reverb channels)!
		{
			totaldelay = voice->reverbdelay[reverb]; //Load the total delay!
			currentactivefinalchannel = (voice->isfinalchannel_reverb[reverb]) && activechannel; //Active&final channel?

			if (readfifobufferflt_backtrace_2(voice->effect_backtrace_chorus[chorus],&channelsamplel,&channelsampler,totaldelay,currentactivefinalchannel)) //Are we successfully read back?
			{
				VolumeEnvelope = voice->reverbvol[reverb]; //Load the envelope to apply!
				applyMIDIReverbFilter(voice, &channelsamplel, (currentchorusreverb<<1)); //Low pass filter!
				applyMIDIReverbFilter(voice, &channelsampler, ((currentchorusreverb<<1)|1)); //Low pass filter!
				lchannel += (int_32)(channelsamplel*VolumeEnvelope); //Sound the left channel at reverb level!
				rchannel += (int_32)(channelsampler*VolumeEnvelope); //Sound the right channel at reverb level!
			}
			++chorus; //Next chorus channel to apply!
			chorus &= 1; //Only 2 choruses to apply, so loop around them!
			reverb += (chorus^1); //Next reverb channel when needed!
		} while (++currentchorusreverb<CHORUSREVERBSIZE); //Remaining channel loop.
		VolumeEnvelope = tempstorage; //Restore the volume envelope!
		#else
		if (!VolumeADSR->active) //Finish?
		{
			voice->active = 0; //Inactive!
		}
		#endif

		//Clip the samples to prevent overflow!
		if (lchannel>SHRT_MAX) lchannel = SHRT_MAX;
		if (lchannel<SHRT_MIN) lchannel = SHRT_MIN;
		if (rchannel>SHRT_MAX) rchannel = SHRT_MAX;
		if (rchannel<SHRT_MIN) rchannel = SHRT_MIN;
		ubuf->l = lchannel; //Left sample!
		ubuf->r = rchannel; //Right sample!
		++voice->play_counter; //Next sample!
		++ubuf; //Prepare for the next sample!
		MIDIDEVICE_tickLFO(&voice->LFO[0]); //Tick first LFO!
		MIDIDEVICE_tickLFO(&voice->LFO[1]); //Tick the second LFO!
	} while (--numsamples); //Repeat while samples are left!

	#ifdef MIDI_LOCKSTART
	unlock(voice->locknumber); //Lock us!
	#endif
	return SOUNDHANDLER_RESULT_FILLED; //We're filled!
}

//val needs to be a normalized input! Performs a concave from 1 to 0!
float MIDIconcave(float val)
{
	float result;
	if (val <= 0.0f) //Invalid?
	{
		return 0.0f; //Nothing!
	}
	if (val >= 1.0f) //Invalid?
	{
		return 1.0f; //Full!
	}
	result = 1.0f-val; //Linear!
	result = (result * result); //Squared!
	result = (-20.0f / 96.0f) * log10f(result); //Convert to the 0.0-0.9 range!
	return result; //Give the result!
}

//val needs to be a normalized input! Performs a convex from 0 to 1!
float MIDIconvex(float val)
{
	return 1.0f - (MIDIconcave(1.0f - val)); //Convex is concave mirrored horizontally on the input, while also mirrored on the output!
}

float getSFmodulator(byte isInstrumentMod, MIDIDEVICE_VOICE* voice, word destination, byte applySrcAmt, float min, float max); //Prototype for linking!

float calcSFModSourceRaw(byte isInstrumentMod, byte isAmtSource, MIDIDEVICE_VOICE* voice, sfModList* mod, SFModulator oper, float linkedval, byte islinked)
{
	float inputrange;
	float i;
	byte type, polarity, direction;

	if (oper & 0x80) //CC is the source when C is set?
	{
		i = (float)voice->channel->ContinuousControllers[oper & 0x7F]; //The CC!
		inputrange = (float)0x7F; //The range!
	}
	else //The normal MIDI information is the source!
	{
		switch (oper & 0x7F) //What MIDI information is selected?
		{
		case 0: //No controller?
			i = 127.0f; //Output is considered 1!
			inputrange = (float)0x7F; //The range!
			break;
		case 2: //Note-on velocity?
			i = (float)(voice->effectivevelocity); //Effective velocity!
			inputrange = (float)0x7F; //The range!
			break;
		case 3: //Note-on key number?
			i = ((float)voice->effectivenote); //Effective velocity!
			inputrange = (float)0x7F; //The range!
			break;
		case 10: //Poly pressure?
			i = ((float)voice->note->pressure); //Poly pressure!
			inputrange = (float)0x7F; //The range!
			break;
		case 13: //Channel pressure?
			i = ((float)voice->channel->pressure); //Channel pressure!
			inputrange = (float)0x7F; //The range!
			break;
		case 14: //Pitch wheel?
			i = ((float)voice->channel->pitch); //Pitch wheel value!
			inputrange = (float)0x4000; //The range!
			break;
		case 16: //Pitch wheel sensitivity (RPN 0)?
			i = MIN(((float)voice->channel->pitchbendsensitivitysemitones)+((float)voice->channel->pitchbendsensitivitycents*0.01f),127.0f); //The semitones and cents part of the pitch wheel sensitivity! Limit to be within range!
			inputrange = (float)0x7F; //The range!
			break;
		case 127: //Link?
			if (isAmtSource) //Not supported?
			{
				if (islinked==0) //Not linked? Ignore it!
				{
					return 1.0f; //Ignore it!
				}
				//Give the result of another modulator?
				i = 127.0f;
				inputrange = (float)0x7F; //The range!
				//Act as x1.0!
			}
			else //Primary source?
			{
				if (islinked==0) //Not linked? Ignore it!
				{
					return 1.0f; //Ignore it!
				}
				//Fill the result of another modulator?
				i = 0.0f; //Fill with parameter or none!
				inputrange = (float)0x7F; //The range!
			}
			break;
		default: //Unknown source?
			i = 0.0f; //Unknown!
			inputrange = (float)0x7F; //The range(non-zero)!
			break;
		}
	}

	i += linkedval; //Add the linked value, which is not normalized! This is an additive generator!
	//Now, i is the value from the input, while inputrange is the range of the input!
	i /= inputrange; //Normalized to 0.0 through 1.0!

	//Clip the combined values to become a safe range!
	if (i > 1.0f) //Output is outside of range? Clip!
	{
		i = 1.0f; //Positive clip!
	}
	else if (i <= 0.0f) //Negative clip?
	{
		i = 0.0f; //Negative clip!
	}

	//Now, apply type, polarity and direction!
	type = ((oper >> 10) & 0x3F); //Type!
	polarity = ((oper >> 9) & 1); //Polarity!
	direction = ((oper >> 8) & 1); //Direction!
	
	if (direction) //Direction is reversed?
	{
		i = 1.0f - i; //Reverse the direction!
	}

	switch (type)
	{
	default: //Not supported?
	case 0: //Linear?
		if (polarity) //Bipolar?
		{
			i = (i * 2.0) - 1.0f; //Convert to a range of -1 to 1 for the proper input value!
		}
		//Unipolar is left alone(already done)!
		break;
	case 1: //Concave?
		if (polarity) //Bipolar?
		{
			if (i>=0.5f) //Past half? Positive half!
			{
				i = MIDIconcave((i-0.5)*2.0f); //Positive half!
			}
			else //First half? Negative half?
			{
				i = -MIDIconcave((0.5-i)*2.0f); //Negative half!
			}
		}
		else //Unipolar?
		{
			i = MIDIconcave(i); //Concave normally!
		}
		break;
	case 2: //Convex?
		if (polarity) //Bipolar?
		{
			if (i>=0.5f) //Past half? Positive half!
			{
				i = MIDIconvex((i-0.5)*2.0f); //Positive half!
			}
			else //First half? Negative half?
			{
				i = -MIDIconvex((0.5-i)*2.0f); //Negative half!
			}
		}
		else //Unipolar?
		{
			i = MIDIconvex(i); //Concave normally!
		}
		break;
	case 3: //Switch?
		if (i >= 0.5f) //Past half?
		{
			i = 1.0f; //Full!
		}
		else //Less than half?
		{
			i = 0.0f; //Empty!
		}
		if (polarity) //Bipolar?
		{
			i = (i * 2.0) - 1.0f; //Convert to a range of -1 to 1 for the proper input value!
		}
		//Unipolar is left alone(already done)!
		break;
	}
	return i; //Give the result!
}

//Get one of the sources!
float getSFModSource(byte isInstrumentMod, MIDIDEVICE_VOICE* voice, sfModList* mod, float linkedval, byte islinked)
{
	return calcSFModSourceRaw(isInstrumentMod, 0, voice, mod, mod->sfModSrcOper, linkedval, islinked);
}

float getSFModAmtSource(byte isInstrumentMod, MIDIDEVICE_VOICE* voice, sfModList* mod, float linkedval, byte islinked)
{
	return calcSFModSourceRaw(isInstrumentMod, 1, voice, mod, mod->sfModAmtSrcOper, linkedval, islinked);
}

float sfModulatorTransform(sfModList* mod, float input)
{
	return input; //Don't apply yet(linear only)!
}

float getSFmodulator(byte isInstrumentMod, MIDIDEVICE_VOICE *voice, word destination, byte applySrcAmt, float min, float max)
{
	static byte modulatorSkip[0x20000]; //Skipping of modulators! Going both ways!
	float result;
	float tempresult;
	int_32 index; //The index to check!
	int_32 originMod;
	byte isGlobal;
	//byte originGlobal;
	byte lookupResult;
	int_32 foundindex;
	word linkedentry;
	float linkedentryval;
	sfModList mod;
	originMod = -1; //Default: no origin mod yet!
	result = 0.0f; //Initialize the result!
	if (applySrcAmt==1) //Destination of a generator?
	{
		memset(&modulatorSkip, 0, sizeof(modulatorSkip)); //Default: nothing skipped yet!
	}
	for (;;) //Keep searching for new modulators!
	{
	processNextIndex:
		index = 0; //Initialize index!
	processNewOriginMod:
		//originGlobal = 2; //Originating global: none set yet!
		originMod = INT_MIN; //No originating modulator yet!

		//Start of the search for the latest modulator!
	processPriorityMod:
		foundindex = INT_MIN; //Default: no found index!
		if (isInstrumentMod) //Instrument modulator?
		{
			lookupResult = lookupSFInstrumentModGlobal(soundfont, voice->instrumentptr, voice->ibag, destination, index, &isGlobal, &mod, &originMod, &foundindex,&linkedentry);
		}
		else //Preset modulator?
		{
			lookupResult = lookupSFPresetModGlobal(soundfont, voice->instrumentptr, voice->ibag, destination, index, &isGlobal, &mod, &originMod, &foundindex,&linkedentry);
		}
		if (foundindex!=INT_MIN) //Any valid Index found?
		{
			if (modulatorSkip[(0x10000 + foundindex) & 0x1FFFF]) //Already skipping this?
			{
				if (index > 0xFFFF) //Finished?
				{
					goto finishUp; //Finish up!
				}
				++index; //Next index to try!
				goto processNewOriginMod; //Try the next modulator!
			}
			modulatorSkip[(0x10000 + foundindex) & 0x1FFFF] = 1; //Skip this modulator in the future!
		}
		switch (lookupResult) //What result?
		{
		case 0: //Not found?
			if ((index==0) && (foundindex==INT_MIN) && (originMod==INT_MIN)) //Finished?
			{
				goto finishUp;
			}
			if (originMod == INT_MIN) //Nothing found?
			{
				if (foundindex == INT_MIN) //We're fully finished?
				{
					goto finishUp; //Finish up the result!
				}
				index = 0; //Try the next index!
				goto processNextIndex;
			}
			//Next index to process?
			break;
		case 1: //Found a valid modulator?
			//Finish up this modulator!
			modulatorSkip[(0x10000 + foundindex) & 0x1FFFF] = 1; //Skip this modulator in the future!
			//Handle the modulator!
			if (linkedentry & 0x8000) //Valid to link to another entry that might exist?
			{
				linkedentryval = getSFmodulator(isInstrumentMod, voice, linkedentry, 2, 0.0f, 0.0f); //Retrieve a linked entry to sum, if any!
				tempresult = sfModulatorTransform(&mod, (getSFModSource(isInstrumentMod, voice, &mod, linkedentryval, 1) * getSFModAmtSource(isInstrumentMod, voice, &mod, 0.0f, 0))); //Source times Dest is added to the result!
			}
			else //Not linkable?
			{
				tempresult = sfModulatorTransform(&mod, (getSFModSource(isInstrumentMod, voice, &mod, 0.0f, 0) * getSFModAmtSource(isInstrumentMod, voice, &mod, 0.0f, 0))); //Source times Dest is added to the result!
			}

			tempresult *= (float)mod.modAmount; //Affect the result by the modulator amount value!
			if ((min != 0.0f) || (max != 0.0f)) //Limits specified?
			{
				if (tempresult > max) tempresult = max; //Limit!
				if (tempresult < min) tempresult = min; //Limit!
			}
			if (!applySrcAmt) //Apply source amount to a modulator? Normalize again
			{
				if (mod.modAmount) //Valid to use?
				{
					result += tempresult*(1.0f/(float)mod.modAmount); //Normalized factor to apply!
				}
			}
			else //Normal addition?
			{
				//Add to the result! Not normalized!
				result += tempresult;
			}
			break;
		case 2: //Needs next?
			modulatorSkip[(0x10000 + foundindex) & 0x1FFFF] = 1; //Skip this modulator in the future!
			++index; //Try the next index!
			goto processPriorityMod;
			break;
		}
	}
finishUp:
	return result; //Placeholder!
}

float getSFInstrumentmodulator(MIDIDEVICE_VOICE* voice, word destination, byte applySrcAmt, float min, float max)
{
	return getSFmodulator(1, voice, destination, applySrcAmt, min, max); //Give the result!
}

float getSFPresetmodulator(MIDIDEVICE_VOICE *voice, word destination, byte applySrcAmt, float min, float max)
{
	return getSFmodulator(0,voice,destination,applySrcAmt, min, max); //Give the result!
}

void calcAttenuationModulators(MIDIDEVICE_VOICE *voice)
{
	int_32 attenuation;
	
	//Apply all settable volume settings!
	attenuation = voice->initialAttenuationGen; //Initial atfenuation generator!

	attenuation += getSFInstrumentmodulator(voice, initialAttenuation, 1, 0.0f, 960.0f); //Get the initial attenuation modulators!
	attenuation += getSFPresetmodulator(voice, initialAttenuation, 1, 0.0f, 960.0f); //Get the initial attenuation modulators!

	voice->effectiveAttenuation = attenuation; //Effective attenuation!
}

void updateModulatorPanningMod(MIDIDEVICE_VOICE* voice)
{
	float panningtemp;
	panningtemp = 0.0f; //Init!
	panningtemp += getSFInstrumentmodulator(voice, pan, 1, 0.0f, 1000.0f); //Get the initial attenuation modulators!
	panningtemp += getSFPresetmodulator(voice, pan, 1, 0.0f, 1000.0f); //Get the initial attenuation modulators!
	panningtemp *= 0.001f; //Make into a percentage, it's in 0.1% units!
	voice->panningmod = panningtemp; //Apply the modulator!
}

void updateMIDILowpassFilter(MIDIDEVICE_VOICE* voice)
{
	float cents;
	sfGenList applygen;
	sfInstGenList applyigen;

	//Frequency
	cents = 13500; //Default!
	if (lookupSFInstrumentGenGlobal(soundfont, voice->instrumentptr, voice->ibag, initialFilterFc, &applyigen)) //Filter enabled?
	{
		cents = (float)LE16(applyigen.genAmount.shAmount);
		if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, initialFilterFc, &applygen))
		{
			cents += (float)LE16(applygen.genAmount.shAmount); //How many semitones! Apply to the cents: 1 semitone = 100 cents!
		}
	}
	else if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, initialFilterFc, &applygen))
	{
		cents += (float)LE16(applygen.genAmount.shAmount); //How many semitones! Apply to the cents: 1 semitone = 100 cents!
	}

	cents += getSFInstrumentmodulator(voice, initialFilterFc, 1, 0.0f, 1000.0f); //Get the initial attenuation modulators!
	cents += getSFPresetmodulator(voice, initialFilterFc, 1, 0.0f, 1000.0f); //Get the initial attenuation modulators!

	voice->lowpassfilter_freq = (8.176f * cents2samplesfactorf((float)cents)); //Set a low pass filter to it's initial value!
	if (voice->lowpassfilter_freq > 20000.0f) voice->lowpassfilter_freq = 20000.0f; //Apply maximum!

	//To filter Fc
	cents = 0; //Default!
	if (lookupSFInstrumentGenGlobal(soundfont, voice->instrumentptr, voice->ibag, modEnvToFilterFc, &applyigen)) //Filter enabled?
	{
		cents = (float)LE16(applyigen.genAmount.shAmount);
		if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, modEnvToFilterFc, &applygen))
		{
			cents += (float)LE16(applygen.genAmount.shAmount); //How many semitones! Apply to the cents: 1 semitone = 100 cents!
		}
	}
	else if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, modEnvToFilterFc, &applygen))
	{
		cents += (float)LE16(applygen.genAmount.shAmount); //How many semitones! Apply to the cents: 1 semitone = 100 cents!
	}

	cents += getSFInstrumentmodulator(voice, modEnvToFilterFc, 1, 0.0f, 1000.0f); //Get the initial attenuation modulators!
	cents += getSFPresetmodulator(voice, modEnvToFilterFc, 1, 0.0f, 1000.0f); //Get the initial attenuation modulators!

	voice->lowpassfilter_modenvfactor = cents; //Apply!

	//To pitch
	cents = 0; //Default!
	if (lookupSFInstrumentGenGlobal(soundfont, voice->instrumentptr, voice->ibag, modEnvToPitch, &applyigen)) //Filter enabled?
	{
		cents = (float)LE16(applyigen.genAmount.shAmount);
		if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, modEnvToPitch, &applygen))
		{
			cents += (float)LE16(applygen.genAmount.shAmount); //How many semitones! Apply to the cents: 1 semitone = 100 cents!
		}
	}
	else if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, modEnvToPitch, &applygen))
	{
		cents += (float)LE16(applygen.genAmount.shAmount); //How many semitones! Apply to the cents: 1 semitone = 100 cents!
	}

	cents += getSFInstrumentmodulator(voice, modEnvToPitch, 1, 0.0f, 1000.0f); //Get the initial attenuation modulators!
	cents += getSFPresetmodulator(voice, modEnvToPitch, 1, 0.0f, 1000.0f); //Get the initial attenuation modulators!

	voice->modenv_pitchfactor = (int_32)cents; //Apply!
}

void updateChorusMod(MIDIDEVICE_VOICE* voice)
{
	word chorusreverbdepth, chorusreverbchannel;
	float basechorusreverb;
	float panningtemp;
	sfGenList applygen;
	sfInstGenList applyigen;
	//Chorus percentage
	panningtemp = getSFInstrumentmodulator(voice, chorusEffectsSend, 1, 0.0f, 1000.0f);
	panningtemp += getSFPresetmodulator(voice, chorusEffectsSend, 1, 0.0f, 1000.0f);

	//The generator for it to apply to!
	basechorusreverb = 0.0f; //Init!
	if (lookupSFInstrumentGenGlobal(soundfont, voice->instrumentptr, voice->ibag, chorusEffectsSend, &applyigen))
	{
		basechorusreverb = (float)LE16(applyigen.genAmount.shAmount); //How many semitones! Apply to the cents: 1 semitone = 100 cents!
		if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, chorusEffectsSend, &applygen))
		{
			basechorusreverb += (float)LE16(applygen.genAmount.shAmount); //How many semitones! Apply to the cents: 1 semitone = 100 cents!
		}
	}
	else if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, chorusEffectsSend, &applygen))
	{
		basechorusreverb = (float)LE16(applygen.genAmount.shAmount); //How many semitones! Apply to the cents: 1 semitone = 100 cents!
	}

	basechorusreverb += panningtemp; //How much is changed by modulators!
	basechorusreverb *= 0.001f; //Make into a percentage, it's in 0.1% units!

	for (chorusreverbdepth = 1; chorusreverbdepth < NUMITEMS(voice->chorusdepth); chorusreverbdepth++) //Process all possible chorus depths!
	{
		voice->chorusdepth[chorusreverbdepth] = powf(basechorusreverb,(float)chorusreverbdepth); //Apply the volume!
	}
	voice->chorusdepth[0] = 1.0f; //Always none at the original level!

	for (chorusreverbchannel = 0; chorusreverbchannel < CHORUSSIZE; ++chorusreverbchannel) //Process all reverb&chorus channels, precalculating every used value!
	{
		voice->activechorusdepth[chorusreverbchannel] = voice->chorusdepth[chorusreverbchannel]; //The chorus feedback strength for that channel!
		voice->chorusvol[chorusreverbchannel] = voice->activechorusdepth[chorusreverbchannel]; //The (chorus) volume on this channel!
	}
}

void updateReverbMod(MIDIDEVICE_VOICE* voice)
{
	word chorusreverbdepth, chorusreverbchannel;
	float basechorusreverb;
	float panningtemp;
	sfGenList applygen;
	sfInstGenList applyigen;
	//Reverb percentage
	panningtemp = getSFInstrumentmodulator(voice, reverbEffectsSend, 1, 0.0f, 1000.0f);
	panningtemp += getSFPresetmodulator(voice, reverbEffectsSend, 1, 0.0f, 1000.0f);

	basechorusreverb = 0.0f; //Init!
	if (lookupSFInstrumentGenGlobal(soundfont, voice->instrumentptr, voice->ibag, reverbEffectsSend, &applyigen))
	{
		basechorusreverb = (float)LE16(applyigen.genAmount.shAmount); //How many semitones! Apply to the cents: 1 semitone = 100 cents!
		if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, reverbEffectsSend, &applygen))
		{
			basechorusreverb = (float)LE16(applygen.genAmount.shAmount); //How many semitones! Apply to the cents: 1 semitone = 100 cents!
		}
	}
	else if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, reverbEffectsSend, &applygen))
	{
		basechorusreverb = (float)LE16(applygen.genAmount.shAmount); //How many semitones! Apply to the cents: 1 semitone = 100 cents!
	}

	basechorusreverb += panningtemp; //How much is changed by modulators!
	basechorusreverb *= 0.001f; //Make into a percentage, it's in 0.1% units!

	for (chorusreverbdepth = 0; chorusreverbdepth < NUMITEMS(voice->reverbdepth); chorusreverbdepth++) //Process all possible chorus depths!
	{
		if (chorusreverbdepth == 0)
		{
			voice->reverbdepth[chorusreverbdepth] = 0.0; //Nothing at the main channel!
		}
		else //Valid depth?
		{
			voice->reverbdepth[chorusreverbdepth] = powf(basechorusreverb,(float)chorusreverbdepth); //Apply the volume!
		}
	}

	for (chorusreverbchannel = 0; chorusreverbchannel < REVERBSIZE; ++chorusreverbchannel) //Process all reverb&chorus channels, precalculating every used value!
	{
		voice->activereverbdepth[chorusreverbchannel] = voice->reverbdepth[chorusreverbchannel]; //The selected value!
	}

	for (chorusreverbdepth = 0; chorusreverbdepth < REVERBSIZE; ++chorusreverbdepth)
	{
		voice->reverbvol[chorusreverbdepth] = voice->activereverbdepth[chorusreverbdepth]; //Chorus reverb volume!
	}
}

void updateSampleSpeed(MIDIDEVICE_VOICE* voice)
{
	sfGenList applygen;
	sfInstGenList applyigen;
	int_32 cents, tonecents; //Relative root MIDI tone, different cents calculations!
	cents = 0; //Default: none!

	//Coarse tune...
	if (lookupSFInstrumentGenGlobal(soundfont, voice->instrumentptr, voice->ibag, coarseTune, &applyigen))
	{
		cents = (int_32)LE16(applyigen.genAmount.shAmount) * 100; //How many semitones! Apply to the cents: 1 semitone = 100 cents!
		if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, coarseTune, &applygen))
		{
			cents += (int_32)LE16(applygen.genAmount.shAmount) * 100; //How many semitones! Apply to the cents: 1 semitone = 100 cents!
		}
	}
	else if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, coarseTune, &applygen))
	{
		cents = (int_32)LE16(applygen.genAmount.shAmount) * 100; //How many semitones! Apply to the cents: 1 semitone = 100 cents!
	}

	cents += getSFInstrumentmodulator(voice, coarseTune, 1, 0.0f, 0.0f);
	cents += getSFPresetmodulator(voice, coarseTune, 1, 0.0f, 0.0f);

	//Fine tune...
	if (lookupSFInstrumentGenGlobal(soundfont, voice->instrumentptr, voice->ibag, fineTune, &applyigen))
	{
		cents += (int_32)LE16(applyigen.genAmount.shAmount); //Add the ammount of cents!
		if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, fineTune, &applygen))
		{
			cents += (int_32)LE16(applygen.genAmount.shAmount); //Add the ammount of cents!
		}
	}
	else if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, fineTune, &applygen))
	{
		cents += (int_32)LE16(applygen.genAmount.shAmount); //Add the ammount of cents!
	}

	cents += getSFInstrumentmodulator(voice, fineTune, 1, 0.0f, 0.0f);
	cents += getSFPresetmodulator(voice, fineTune, 1, 0.0f, 0.0f);

	//Scale tuning: how the MIDI number affects semitone (percentage of semitones)
	tonecents = 100; //Default: 100 cents(%) scale tuning!
	if (lookupSFInstrumentGenGlobal(soundfont, voice->instrumentptr, voice->ibag, scaleTuning, &applyigen))
	{
		tonecents = (int_32)LE16(applyigen.genAmount.shAmount); //Apply semitone factor in percent for each tone!
		if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, scaleTuning, &applygen))
		{
			tonecents += (int_32)LE16(applygen.genAmount.shAmount); //Apply semitone factor in percent for each tone!
		}
	}
	else if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, scaleTuning, &applygen))
	{
		tonecents = (int_32)LE16(applygen.genAmount.shAmount); //Apply semitone factor in percent for each tone!
	}

	tonecents += getSFInstrumentmodulator(voice, scaleTuning, 1, 0.0f, 0.0f);
	tonecents += getSFPresetmodulator(voice, scaleTuning, 1, 0.0f, 0.0f);

	cents += voice->sample[1].chPitchCorrection; //Apply pitch correction for the used sample!

	cents *= (tonecents * 0.01f); //Apply scale tuning to the coarse/fine tuning as well!

	tonecents *= voice->rootMIDITone; //Difference in tones we use is applied to the ammount of cents!

	cents += tonecents; //Apply the MIDI tone cents for the MIDI tone!

	//Now the cents variable contains the diviation in cents.
	voice->effectivesamplespeedup = cents; //Load the default speedup we need for our tone!
}

void MIDI_muteExclusiveClass(uint_32 exclusiveclass, MIDIDEVICE_VOICE *newvoice)
{
	word voicenr;
	MIDIDEVICE_VOICE* voice;
	word samenr;
	voicenr = 0; //First voice!
	for (; voicenr < MIDI_TOTALVOICES; ++voicenr) //Find a used voice!
	{
		voice = &activevoices[voicenr]; //The voice!
		if (voice->active) //Active?
		{
			if (voice->exclusiveclass==exclusiveclass) //Matched exclusive class?
			{
				#if MIDI_NOTEVOICES!=1
				//Ignore same voice that's starting!
				samenr = ((ptrnum)newvoice-(ptrnum)&activevoices[0])/sizeof(activevoices[0]);
				if ( ((samenr/MIDI_NOTEVOICES)!=(voicenr/MIDI_NOTEVOICES)) //Different voice?
							|| ((voicenr>samenr) && ((samenr/MIDI_NOTEVOICES)==(voicenr/MIDI_NOTEVOICES))) //Same voice that's unhandled?
					)
				#endif
				{
					if (voice->preset==newvoice->preset) //Soundfont 2.04 8.1.2 says for same preset only!
					{
						//Terminate it ASAP!
						voice->active = 0; //Immediately deallocate!
					}
				}
			}
		}
	}
}

//Initialize a LFO to use! Supply endOper when not using a certain output of the LFO!
void MIDIDEVICE_updateLFO(MIDIDEVICE_VOICE * voice, MIDIDEVICE_LFO * LFO)
{
	sfGenList applygen;
	sfInstGenList applyigen;

	float SINUS_BASE;
	float effectivefrequency;

	int_32 cents;

	cents = 0; //Default: none!

	//Frequency
	if (lookupSFInstrumentGenGlobal(soundfont, voice->instrumentptr, voice->ibag, LFO->sources.frequency, &applyigen))
	{
		cents = (int_32)LE16(applyigen.genAmount.shAmount); //How many! Apply to the cents!
		if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, LFO->sources.frequency, &applygen))
		{
			cents += (int_32)LE16(applygen.genAmount.shAmount); //How many! Apply to the cents!
		}
	}
	else if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, LFO->sources.frequency, &applygen))
	{
		cents = (int_32)LE16(applygen.genAmount.shAmount); //How many! Apply to the cents!
	}

	cents += getSFInstrumentmodulator(voice, LFO->sources.frequency, 1, 0.0f, 0.0f);
	cents += getSFPresetmodulator(voice, LFO->sources.frequency, 1, 0.0f, 0.0f);

	cents = LIMITRANGE(cents, -16000, 4500);

	effectivefrequency = 8.176f * cents2samplesfactorf(cents); //Effective frequency to use!

	SINUS_BASE = 2.0f * (float)PI * effectivefrequency; //MIDI Sinus Base for LFO effects!

	//Don't update the delay, it's handled only once!

	LFO->sinposstep = SINUS_BASE * (1.0f / (float)LE32(voice->sample[1].dwSampleRate)) * sinustable_percision_reverse; //How much time to add to the chorus sinus after each sample
	//Continue the sinus from the current position, at the newly specified frequency!

	//Now, the basic sinus is setup!

	//Lookup the affecting values for the modulators!

	//To pitch!
	cents = 0; //Default: none!
	if (lookupSFInstrumentGenGlobal(soundfont, voice->instrumentptr, voice->ibag, LFO->sources.topitch, &applyigen))
	{
		cents = (int_32)LE16(applyigen.genAmount.shAmount); //How many! Apply to the cents!
		if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, LFO->sources.topitch, &applygen))
		{
			cents += (int_32)LE16(applygen.genAmount.shAmount); //How many! Apply to the cents!
		}
	}
	else if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, LFO->sources.topitch, &applygen))
	{
		cents = (int_32)LE16(applygen.genAmount.shAmount); //How many! Apply to the cents!
	}
	cents += getSFInstrumentmodulator(voice, LFO->sources.topitch, 1, 0.0f, 0.0f);
	cents += getSFPresetmodulator(voice, LFO->sources.topitch, 1, 0.0f, 0.0f);
	LFO->topitch = cents; //Cents

	//To filter cutoff!
	cents = 0; //Default: none!
	if (lookupSFInstrumentGenGlobal(soundfont, voice->instrumentptr, voice->ibag, LFO->sources.tofiltercutoff, &applyigen))
	{
		cents = (int_32)LE16(applyigen.genAmount.shAmount); //How many! Apply to the cents!
		if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, LFO->sources.tofiltercutoff, &applygen))
		{
			cents += (int_32)LE16(applygen.genAmount.shAmount); //How many! Apply to the cents!
		}
	}
	else if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, LFO->sources.tofiltercutoff, &applygen))
	{
		cents = (int_32)LE16(applygen.genAmount.shAmount); //How many! Apply to the cents!
	}
	cents += getSFInstrumentmodulator(voice, LFO->sources.tofiltercutoff, 1, 0.0f, 0.0f);
	cents += getSFPresetmodulator(voice, LFO->sources.tofiltercutoff, 1, 0.0f, 0.0f);
	LFO->tofiltercutoff = cents; //Cents

	//To volume!
	cents = 0; //Default: none!
	if (lookupSFInstrumentGenGlobal(soundfont, voice->instrumentptr, voice->ibag, LFO->sources.tovolume, &applyigen))
	{
		cents = (int_32)LE16(applyigen.genAmount.shAmount); //How many! Apply to the cents!
		if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, LFO->sources.tovolume, &applygen))
		{
			cents += (int_32)LE16(applygen.genAmount.shAmount); //How many! Apply to the cents!
		}
	}
	else if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, LFO->sources.tovolume, &applygen))
	{
		cents = (int_32)LE16(applygen.genAmount.shAmount); //How many! Apply to the cents!
	}
	cents += getSFInstrumentmodulator(voice, LFO->sources.tovolume, 1, 0.0f, 0.0f);
	cents += getSFPresetmodulator(voice, LFO->sources.tovolume, 1, 0.0f, 0.0f);
	LFO->tovolume = cents; //cB!
}

//Initialize a LFO to use! Supply endOper when not using a certain output of the LFO!
void MIDIDEVICE_initLFO(MIDIDEVICE_VOICE* voice, MIDIDEVICE_LFO* LFO, word thedelay, word frequency, word topitch, word tofiltercutoff, word tovolume)
{
	sfGenList applygen;
	sfInstGenList applyigen;

	float SINUS_BASE;
	float effectivefrequency;
	float effectivedelay;

	int_32 cents;

	//Setup the sources of the LFO to update during runtime!
	LFO->sources.thedelay = thedelay;
	LFO->sources.frequency = frequency;
	LFO->sources.topitch = topitch;
	LFO->sources.tofiltercutoff = tofiltercutoff;
	LFO->sources.tovolume = tovolume;

	cents = 0; //Default: none!

	//Frequency
	if (lookupSFInstrumentGenGlobal(soundfont, voice->instrumentptr, voice->ibag, frequency, &applyigen))
	{
		cents = (int_32)LE16(applyigen.genAmount.shAmount); //How many! Apply to the cents!
		if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, frequency, &applygen))
		{
			cents += (int_32)LE16(applygen.genAmount.shAmount); //How many! Apply to the cents!
		}
	}
	else if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, frequency, &applygen))
	{
		cents = (int_32)LE16(applygen.genAmount.shAmount); //How many! Apply to the cents!
	}

	cents += getSFInstrumentmodulator(voice, frequency, 1, 0.0f, 0.0f);
	cents += getSFPresetmodulator(voice, frequency, 1, 0.0f, 0.0f);

	cents = LIMITRANGE(cents,-16000,4500);

	effectivefrequency = 8.176f*cents2samplesfactorf(cents); //Effective frequency to use!

	SINUS_BASE = 2.0f * (float)PI * effectivefrequency; //MIDI Sinus Base for LFO effects!

	//Delay
	cents = -12000;
	if (lookupSFInstrumentGenGlobal(soundfont, voice->instrumentptr, voice->ibag, thedelay, &applyigen))
	{
		cents = (int_32)LE16(applyigen.genAmount.shAmount); //How many! Apply to the cents!
		if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, thedelay, &applygen))
		{
			cents += (int_32)LE16(applygen.genAmount.shAmount); //How many! Apply to the cents!
		}
	}
	else if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, thedelay, &applygen))
	{
		cents = (int_32)LE16(applygen.genAmount.shAmount); //How many! Apply to the cents!
	}

	cents += getSFInstrumentmodulator(voice, thedelay, 1, 0.0f, 0.0f);
	cents += getSFPresetmodulator(voice, thedelay, 1, 0.0f, 0.0f);

	effectivedelay = cents2samplesfactorf(cents); //Effective delay to use!

	LFO->delay = (uint_32)((effectivedelay) * (float)LE16(voice->sample[1].dwSampleRate)); //Total delay to apply for this channel!
	LFO->sinposstep = SINUS_BASE * (1.0f / (float)LE32(voice->sample[1].dwSampleRate)) * sinustable_percision_reverse; //How much time to add to the chorus sinus after each sample
	LFO->sinpos = fmodf(fmodf((float)(-effectivedelay) * LFO->sinposstep, (2 * (float)PI))+(2 * (float)PI), (2 * (float)PI)) * sinustable_percision_reverse; //Initialize the starting chorus sin position for the first sample!

	//Now, the basic sinus is setup!

	//Lookup the affecting values!

	//To pitch!
	cents = 0; //Default: none!
	if (lookupSFInstrumentGenGlobal(soundfont, voice->instrumentptr, voice->ibag, topitch, &applyigen))
	{
		cents = (int_32)LE16(applyigen.genAmount.shAmount); //How many! Apply to the cents!
		if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, topitch, &applygen))
		{
			cents += (int_32)LE16(applygen.genAmount.shAmount); //How many! Apply to the cents!
		}
	}
	else if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, topitch, &applygen))
	{
		cents = (int_32)LE16(applygen.genAmount.shAmount); //How many! Apply to the cents!
	}
	cents += getSFInstrumentmodulator(voice, topitch, 1, 0.0f, 0.0f);
	cents += getSFPresetmodulator(voice, topitch, 1, 0.0f, 0.0f);
	LFO->topitch = cents; //Cents

	//To filter cutoff!
	cents = 0; //Default: none!
	if (lookupSFInstrumentGenGlobal(soundfont, voice->instrumentptr, voice->ibag, tofiltercutoff, &applyigen))
	{
		cents = (int_32)LE16(applyigen.genAmount.shAmount); //How many! Apply to the cents!
		if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, tofiltercutoff, &applygen))
		{
			cents += (int_32)LE16(applygen.genAmount.shAmount); //How many! Apply to the cents!
		}
	}
	else if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, tofiltercutoff, &applygen))
	{
		cents = (int_32)LE16(applygen.genAmount.shAmount); //How many! Apply to the cents!
	}
	cents += getSFInstrumentmodulator(voice, tofiltercutoff, 1, 0.0f, 0.0f);
	cents += getSFPresetmodulator(voice, tofiltercutoff, 1, 0.0f, 0.0f);
	LFO->tofiltercutoff = cents; //Cents

	//To volume!
	cents = 0; //Default: none!
	if (lookupSFInstrumentGenGlobal(soundfont, voice->instrumentptr, voice->ibag, tovolume, &applyigen))
	{
		cents = (int_32)LE16(applyigen.genAmount.shAmount); //How many! Apply to the cents!
		if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, tovolume, &applygen))
		{
			cents += (int_32)LE16(applygen.genAmount.shAmount); //How many! Apply to the cents!
		}
	}
	else if (lookupSFPresetGenGlobal(soundfont, voice->preset, voice->pbag, tovolume, &applygen))
	{
		cents = (int_32)LE16(applygen.genAmount.shAmount); //How many! Apply to the cents!
	}
	cents += getSFInstrumentmodulator(voice, tovolume, 1, 0.0f, 0.0f);
	cents += getSFPresetmodulator(voice, tovolume, 1, 0.0f, 0.0f);
	LFO->tovolume = cents; //cB!
}

//result: 0=Finished not renderable, -1=Requires empty channel(voice stealing?), 1=Allocated, -2=Can't render, request next voice.
OPTINLINE sbyte MIDIDEVICE_newvoice(MIDIDEVICE_VOICE *voice, byte request_channel, byte request_note, byte voicenumber)
{
	const float MIDI_CHORUS_SINUS_BASE = 2.0f*(float)PI*CHORUS_LFO_FREQUENCY; //MIDI Sinus Base for chorus effects!
	word pbag, ibag, chorusreverbdepth;
	float panningtemp, attenuation, tempattenuation, lvolume, rvolume;
	sword rootMIDITone;
	uint_32 preset, startaddressoffset[2], endaddressoffset[2], startloopaddressoffset[2], endloopaddressoffset[2], loopsize[2];
	byte effectivenote; //Effective note we're playing!
	byte effectivevelocity; //Effective velocity we're playing!
	byte effectivenotevelocitytemp;
	word voicecounter;
	byte purposebackup;
	byte allocatedbackup;
	byte activeloopflags[2];
	uint_32 exclusiveclass;

	MIDIDEVICE_CHANNEL *channel;
	MIDIDEVICE_NOTE *note;
	sfPresetHeader currentpreset;
	sfGenList instrumentptr, applygen;
	sfInst currentinstrument;
	sfInstGenList sampleptr, applyigen;
	sfSample sampleInfo[2];
	FIFOBUFFER *temp, *temp2, *temp3, *temp4, *chorus_backtrace[CHORUSSIZE];
	int_32 previousPBag, previousIBag;
	static uint_64 starttime = 0; //Increasing start time counter (1 each note on)!

	if (memprotect(soundfont,sizeof(*soundfont),"RIFF_FILE")!=soundfont) return 0; //We're unable to render anything!
	if (!soundfont) return 0; //We're unable to render anything!
	lockMPURenderer(); //Lock the audio: we're starting to modify!
	#ifdef MIDI_LOCKSTART
	lock(voice->locknumber); //Lock us!
	#endif

	//First, our precalcs!
	channel = &MIDI_channels[request_channel]; //What channel!
	note = &channel->notes[request_note]; //What note!

	//Now retrieve our note by specification!

	if (!lookupPresetByInstrument(soundfont, channel->program, channel->activebank, &preset)) //Preset not found?
	{
		#ifdef MIDI_LOCKSTART
		unlock(voice->locknumber); //Lock us!
		#endif
		unlockMPURenderer(); //We're finished!
		return 0; //Not renderable!
	}

	if (!getSFPreset(soundfont, preset, &currentpreset))
	{
		#ifdef MIDI_LOCKSTART
		unlock(voice->locknumber); //Lock us!
		#endif
		unlockMPURenderer(); //We're finished!
		return 0; //Not renderable!
	}

	previousPBag = -1; //Default to the first zone to check!
	previousIBag = -1; //Default to the first zone to check!

	voicecounter = 0; //Find the first or next voice to allocate, if any!
	handleNextPBag:
	if (!lookupPBagByMIDIKey(soundfont, preset, note->note, note->noteon_velocity, &pbag, previousPBag)) //Preset bag not found?
	{
		if (previousPBag == -1) //Invalid preset zone to play?
		{
			#ifdef MIDI_LOCKSTART
			unlock(voice->locknumber); //Lock us!
			#endif
			unlockMPURenderer(); //We're finished!
			return 0; //Not renderable!
		}
		else //Final zone processed?
		{
			#ifdef MIDI_LOCKSTART
			unlock(voice->locknumber); //Lock us!
			#endif
			unlockMPURenderer(); //We're finished!
			return 0; //Not renderable!
		}
	}

	if (!lookupSFPresetGen(soundfont, preset, pbag, instrument, &instrumentptr))
	{
		previousPBag = (int_32)pbag; //Search for the next PBag!
		goto handleNextPBag; //Handle the next PBag, if any!
	}

	if (!getSFInstrument(soundfont, LE16(instrumentptr.genAmount.wAmount), &currentinstrument))
	{
		previousPBag = (int_32)pbag; //Search for the next PBag!
		goto handleNextPBag; //Handle the next PBag, if any!
	}

	handleNextIBag:
	if (!lookupIBagByMIDIKey(soundfont, LE16(instrumentptr.genAmount.wAmount), note->note, note->noteon_velocity, &ibag, 1, previousIBag))
	{
		previousPBag = (int_32)pbag; //Search for the next PBag!
		previousIBag = -1; //Start looking for the next IBags to apply!
		goto handleNextPBag; //Handle the next PBag, if any!
	}
	else //A valid zone has been found! Register it to be used for the next check!
	{
		previousIBag = (int_32)ibag; //Check from this IBag onwards!
	}

	if (voicecounter++ != voicenumber) //Not the voice number we're searching?
	{
		//Find the next voice instead!
		goto handleNextIBag; //Find the next IBag to use!
	}

	if (!lookupSFInstrumentGen(soundfont, LE16(instrumentptr.genAmount.wAmount), ibag, sampleID, &sampleptr))
	{
		#ifdef MIDI_LOCKSTART
		unlock(voice->locknumber); //Lock us!
		#endif
		unlockMPURenderer(); //We're finished!
		return -2; //No samples for this split! We can't render!
	}

	if (!getSFSampleInformation(soundfont, LE16(sampleptr.genAmount.wAmount), &sampleInfo[0])) //Load the used sample information!
	{
		invalidsampleinfo:
#ifdef MIDI_LOCKSTART
		unlock(voice->locknumber); //Lock us!
#endif
		unlockMPURenderer(); //We're finished!
		return -2; //No samples for this split! We can't render!
	}

	switch (sampleInfo[0].sfSampleType) //What sample type?
	{
	case monoSample:
		memcpy(&sampleInfo[1],&sampleInfo[0],sizeof(sampleInfo[0])); //Duplicate left/right channel from 1 source!
		break;
	case leftSample:
		if (!getSFSampleInformation(soundfont, LE16(sampleptr.genAmount.wAmount), &sampleInfo[1])) //Load the used sample information!
		{
			goto invalidsampleinfo;
		}
		break;
	case rightSample:
		memcpy(&sampleInfo[1],&sampleInfo[0],sizeof(sampleInfo[0])); //Duplicate left/right channel from 1 source!
		if (!getSFSampleInformation(soundfont, LE16(sampleptr.genAmount.wAmount), &sampleInfo[0])) //Load the used sample information!
		{
			goto invalidsampleinfo;
		}
		break;
	default:
		goto invalidsampleinfo;
		break;
	}
	
	//For now, assume mono samples!

	//Determine the adjusting offsets!

	//Fist, init to defaults!
	startaddressoffset[0] = LE32(sampleInfo[0].dwStart);
	startaddressoffset[1] = LE32(sampleInfo[1].dwStart);
	endaddressoffset[0] = LE32(sampleInfo[0].dwEnd);
	endaddressoffset[1] = LE32(sampleInfo[1].dwEnd);
	startloopaddressoffset[0] = LE32(sampleInfo[0].dwStartloop);
	startloopaddressoffset[1] = LE32(sampleInfo[1].dwStartloop);
	endloopaddressoffset[0] = LE32(sampleInfo[0].dwEndloop);
	endloopaddressoffset[1] = LE32(sampleInfo[1].dwEndloop);

	//Next, apply generators!
	if (lookupSFInstrumentGenGlobal(soundfont, LE16(instrumentptr.genAmount.wAmount), ibag, startAddrsOffset, &applyigen))
	{
		startaddressoffset[0] += LE16(applyigen.genAmount.shAmount); //Apply!
		startaddressoffset[1] += LE16(applyigen.genAmount.shAmount); //Apply!
	}
	if (lookupSFInstrumentGenGlobal(soundfont, LE16(instrumentptr.genAmount.wAmount), ibag, startAddrsCoarseOffset, &applyigen))
	{
		startaddressoffset[0] += (LE16(applyigen.genAmount.shAmount) << 15); //Apply!
		startaddressoffset[1] += (LE16(applyigen.genAmount.shAmount) << 15); //Apply!
	}

	if (lookupSFInstrumentGenGlobal(soundfont, LE16(instrumentptr.genAmount.wAmount), ibag, endAddrsOffset, &applyigen))
	{
		endaddressoffset[0] += LE16(applyigen.genAmount.shAmount); //Apply!
		endaddressoffset[1] += LE16(applyigen.genAmount.shAmount); //Apply!
	}
	if (lookupSFInstrumentGenGlobal(soundfont, LE16(instrumentptr.genAmount.wAmount), ibag, endAddrsCoarseOffset, &applyigen))
	{
		endaddressoffset[0] += (LE16(applyigen.genAmount.shAmount) << 15); //Apply!
		endaddressoffset[1] += (LE16(applyigen.genAmount.shAmount) << 15); //Apply!
	}

	if (lookupSFInstrumentGenGlobal(soundfont, LE16(instrumentptr.genAmount.wAmount), ibag, startloopAddrsOffset, &applyigen))
	{
		startloopaddressoffset[0] += LE16(applyigen.genAmount.shAmount); //Apply!
		startloopaddressoffset[1] += LE16(applyigen.genAmount.shAmount); //Apply!
	}
	if (lookupSFInstrumentGenGlobal(soundfont, LE16(instrumentptr.genAmount.wAmount), ibag, startloopAddrsCoarseOffset, &applyigen))
	{
		startloopaddressoffset[0] += (LE16(applyigen.genAmount.shAmount) << 15); //Apply!
		startloopaddressoffset[1] += (LE16(applyigen.genAmount.shAmount) << 15); //Apply!
	}

	if (lookupSFInstrumentGenGlobal(soundfont, LE16(instrumentptr.genAmount.wAmount), ibag, endloopAddrsOffset, &applyigen))
	{
		endloopaddressoffset[0] += LE16(applyigen.genAmount.shAmount); //Apply!
		endloopaddressoffset[1] += LE16(applyigen.genAmount.shAmount); //Apply!
	}
	if (lookupSFInstrumentGenGlobal(soundfont, LE16(instrumentptr.genAmount.wAmount), ibag, endloopAddrsCoarseOffset, &applyigen))
	{
		endloopaddressoffset[0] += (LE16(applyigen.genAmount.shAmount) << 15); //Apply!
		endloopaddressoffset[1] += (LE16(applyigen.genAmount.shAmount) << 15); //Apply!
	}

	//Apply loop flags!
	activeloopflags[0] = activeloopflags[1] = 0; //Default: no looping!
	if (lookupSFInstrumentGenGlobal(soundfont, LE16(instrumentptr.genAmount.wAmount), ibag, sampleModes, &applyigen)) //Gotten looping?
	{
		switch (LE16(applyigen.genAmount.wAmount)) //What loop?
		{
		case GEN_SAMPLEMODES_LOOP: //Always loop?
			activeloopflags[0] = activeloopflags[1] = 1; //Always loop!
			break;
		case GEN_SAMPLEMODES_LOOPUNTILDEPRESSDONE: //Loop until depressed!
			activeloopflags[0] = activeloopflags[1] = 3; //Loop until depressed!
			break;
		case GEN_SAMPLEMODES_NOLOOP: //No loop?
		case GEN_SAMPLEMODES_NOLOOP2: //No loop?
		default:
			//Do nothing!
			break;
		}
	}

	//Check the offsets against the available samples first, before starting to allocate a voice?
	//Return -2 if so(can't render voice)!

	loopsize[0] = endloopaddressoffset[0]; //End of the loop!
	loopsize[0] -= startloopaddressoffset[0]; //Size of the loop!
	loopsize[1] = endloopaddressoffset[1]; //End of the loop!
	loopsize[1] -= startloopaddressoffset[1]; //Size of the loop!

	if (((loopsize[0]==0) && activeloopflags[0])||((loopsize[1]==0) && activeloopflags[1])) //Invalid loop to render?
	{
#ifdef MIDI_LOCKSTART
		unlock(voice->locknumber); //Lock us!
#endif
		unlockMPURenderer(); //We're finished!
		return -2; //No samples for this split! We can't render!
	}

	//A requested voice counter has been found!

	//If we reach here, the voice is valid and needs to be properly allocated!

	if ((voice->active) || (voice->allocated==0)) //Already active or unusable? Needs voice stealing to work!
	{
		#ifdef MIDI_LOCKSTART
		unlock(voice->locknumber); //Lock us!
		#endif
		unlockMPURenderer(); //We're finished!
		return -1; //Active voices can't be allocated! Request voice stealing or an available channel!
	}

	if (!setSampleRate(&MIDIDEVICE_renderer, voice, (float)LE16(sampleInfo[1].dwSampleRate))) //Use this new samplerate!
	{
		//Unusable samplerate! Try next available voice!
		#ifdef MIDI_LOCKSTART
		unlock(voice->locknumber); //Lock us!
		#endif
		unlockMPURenderer(); //We're finished!
		return -2; //No samples for this split! We can't render!		
	}
	note->pressure = 0; //Initialize the pressure for this note to none yet!

	//Initialize the requested voice!
	//First, all our voice-specific variables and precalcs!
	temp = voice->effect_backtrace_samplespeedup_modenv_pitchfactor; //Back-up the effect backtrace!
	temp2 = voice->effect_backtrace_LFO1; //Back-up the effect backtrace!
	temp3 = voice->effect_backtrace_LFO2; //Back-up the effect backtrace!
	temp4 = voice->effect_backtrace_lowpassfilter_modenvfactor; //Back-up the effect backtrace!
	for (chorusreverbdepth = 0; chorusreverbdepth < CHORUSSIZE; ++chorusreverbdepth)
	{
		chorus_backtrace[chorusreverbdepth] = voice->effect_backtrace_chorus[chorusreverbdepth]; //Back-up!
	}
	purposebackup = voice->purpose;
	allocatedbackup = voice->allocated;
	memset(voice, 0, sizeof(*voice)); //Clear the entire channel!
	voice->allocated = allocatedbackup;
	voice->purpose = purposebackup;
	voice->effect_backtrace_lowpassfilter_modenvfactor = temp4; //Restore our buffer!
	voice->effect_backtrace_LFO2 = temp3; //Restore our buffer!
	voice->effect_backtrace_LFO1 = temp2; //Restore our buffer!
	voice->effect_backtrace_samplespeedup_modenv_pitchfactor = temp; //Restore our buffer!
	for (chorusreverbdepth = 0; chorusreverbdepth < CHORUSSIZE; ++chorusreverbdepth)
	{
		voice->effect_backtrace_chorus[chorusreverbdepth] = chorus_backtrace[chorusreverbdepth]; //Restore!
	}
	fifobuffer_clear(voice->effect_backtrace_samplespeedup_modenv_pitchfactor); //Clear our history buffer!
	fifobuffer_clear(voice->effect_backtrace_LFO1); //Clear our history buffer!
	fifobuffer_clear(voice->effect_backtrace_LFO2); //Clear our history buffer!
	fifobuffer_clear(voice->effect_backtrace_lowpassfilter_modenvfactor); //Clear our history buffer!
#ifndef DISABLE_REVERB
	for (chorusreverbdepth = 0; chorusreverbdepth < CHORUSSIZE; ++chorusreverbdepth) //Initialize all chorus histories!
	{
		fifobuffer_clear(voice->effect_backtrace_chorus[chorusreverbdepth]); //Clear our history buffer!
	}
	#endif

	memcpy(&voice->sample[0], &sampleInfo[0], sizeof(sampleInfo[0])); //Load the active sample info to become active for the allocated voice!
	memcpy(&voice->sample[1], &sampleInfo[1], sizeof(sampleInfo[0])); //Load the active sample info to become active for the allocated voice!
	memcpy(&voice->currentpreset, &currentpreset, sizeof(currentpreset)); //Load the active sample info to become active for the allocated voice!
	memcpy(&voice->currentinstrument, &currentinstrument, sizeof(currentinstrument)); //Load the active sample info to become active for the allocated voice!

	voice->loopsize[0] = loopsize[0]; //Save the loop size!
	voice->loopsize[1] = loopsize[1]; //Save the loop size!

	//Now, determine the actual note to be turned on!
	voice->channel = channel; //What channel!
	voice->note = note; //What note!

	//Identify to any player we're displayable!
	voice->loadedinformation = 1; //We've loaded information for this voice!

	//Preset and instrument lookups!
	voice->preset = preset; //Preset!
	voice->pbag = pbag; //PBag!
	voice->instrumentptr = LE16(instrumentptr.genAmount.wAmount); //Instrument!
	voice->ibag = ibag; //IBag!

	voice->play_counter = 0; //Reset play counter!
	
	for (chorusreverbdepth = 0; chorusreverbdepth < CHORUSSIZE; ++chorusreverbdepth) //Init chorus channels!
	{
		//Initialize all monotone counters!
		voice->monotonecounter[chorusreverbdepth] = 0;
		voice->monotonecounter_diff[chorusreverbdepth] = 0.0f;
	}

	//Check for exclusive class now!
	exclusiveclass = voice->exclusiveclass = 0; //Default: no exclusive class!
	if (lookupSFInstrumentGenGlobal(soundfont, LE16(instrumentptr.genAmount.wAmount), ibag, exclusiveClass, &applyigen))
	{
		exclusiveclass = LE16(applyigen.genAmount.shAmount); //Apply!
		if (exclusiveclass) //Non-zero? Mute other instruments!
		{
			//Mute first!
			MIDI_muteExclusiveClass(exclusiveclass,voice); //Mute other voices!
			//Now, set the exclusive class!
			voice->exclusiveclass = exclusiveclass; //To mute us by other voices, if needed!
		}
	}

	effectivevelocity = note->noteon_velocity; //What velocity to use?
	effectivenote = note->note; //What is the effective note we're playing?

	if (lookupSFInstrumentGenGlobal(soundfont, LE16(instrumentptr.genAmount.wAmount), ibag, overridingKeynum, &applyigen))
	{
		effectivenotevelocitytemp = LE16(applyigen.genAmount.shAmount); //Apply!
		if ((effectivenotevelocitytemp>=0) && (effectivenotevelocitytemp<=0x7F)) //In range?
		{
			effectivenote = effectivenotevelocitytemp; //Override!
		}
	}

	if (lookupSFInstrumentGenGlobal(soundfont, LE16(instrumentptr.genAmount.wAmount), ibag, overridingVelocity, &applyigen))
	{
		effectivenotevelocitytemp = LE16(applyigen.genAmount.shAmount); //Apply!
		if ((effectivenotevelocitytemp>=0) && (effectivenotevelocitytemp<=0x7F)) //In range?
		{
			effectivevelocity = effectivenotevelocitytemp; //Override!
		}
	}

	//Save the effective values!
	voice->effectivenote = effectivenote;
	voice->effectivevelocity = effectivevelocity;

	//Save our info calculated!
	voice->startaddressoffset[0] = startaddressoffset[0];
	voice->startaddressoffset[1] = startaddressoffset[1];
	voice->endaddressoffset[0] = endaddressoffset[0];
	voice->endaddressoffset[1] = endaddressoffset[1];
	voice->startloopaddressoffset[0] = startloopaddressoffset[0];
	voice->startloopaddressoffset[1] = startloopaddressoffset[1];
	voice->endloopaddressoffset[0] = endloopaddressoffset[0];
	voice->endloopaddressoffset[1] = endloopaddressoffset[1];

	//Determine the loop size!
	voice->loopsize[0] = loopsize[0]; //Save the loop size!
	voice->loopsize[1] = loopsize[1]; //Save the loop size!

	//Now, calculate the speedup according to the note applied!

	//Calculate MIDI difference in notes!
	if (lookupSFInstrumentGenGlobal(soundfont, LE16(instrumentptr.genAmount.wAmount), ibag, overridingRootKey, &applyigen))
	{
		rootMIDITone = (sword)LE16(applyigen.genAmount.wAmount); //The MIDI tone to apply is different!
		if ((rootMIDITone<0) || (rootMIDITone>127)) //Invalid?
		{
			rootMIDITone = (sword)voice->sample[1].byOriginalPitch; //Original MIDI tone!
		}
	}
	else
	{
		rootMIDITone = (sword)voice->sample[1].byOriginalPitch; //Original MIDI tone!
	}

	rootMIDITone = (((sword)effectivenote)-rootMIDITone); //>positive difference, <negative difference.
	//Ammount of MIDI notes too high is in rootMIDITone.

	voice->rootMIDITone = rootMIDITone; //Save the relative tone!

	updateSampleSpeed(voice); //Update the sample speed!
	
	//Determine the attenuation generator to use!
	attenuation = 0; //Default attenuation!
	if (lookupSFInstrumentGenGlobal(soundfont, LE16(instrumentptr.genAmount.wAmount), ibag, initialAttenuation, &applyigen))
	{
		attenuation = (float)LE16(applyigen.genAmount.shAmount); //Apply semitone factor in percent for each tone!
		if (attenuation>1440.0f) attenuation = 1440.0f; //Limit to max!
		if (attenuation<0.0f) attenuation = 0.0f; //Limit to min!
	}
	if (lookupSFPresetGenGlobal(soundfont, preset, pbag, initialAttenuation, &applygen))
	{
		tempattenuation = (float)LE16(applygen.genAmount.shAmount); //Apply semitone factor in percent for each tone!
		if (tempattenuation > 1440.0f) tempattenuation = 1440.0f; //Limit to max!
		if (tempattenuation < 0.0f) tempattenuation = 0.0f; //Limit to min!
		attenuation += tempattenuation; //Additive!
	}

	#ifdef IS_LONGDOUBLE
	voice->initialAttenuationGen = attenuation; //We're converted to a rate of 960 cb!
	#else
	voice->initialAttenuationGen = attenuation; //We're converted to a rate of 960 cb!
	#endif

	calcAttenuationModulators(voice); //Calc the modulators!

	//Determine panning!
	panningtemp = 0.0f; //Default: no panning at all: centered!
	if (lookupSFInstrumentGenGlobal(soundfont, LE16(instrumentptr.genAmount.wAmount), ibag, pan, &applyigen)) //Gotten panning?
	{
		panningtemp = (float)LE16(applyigen.genAmount.shAmount); //Get the panning specified!
	}
	panningtemp *= 0.001f; //Make into a percentage, it's in 0.1% units!
	voice->initpanning = panningtemp; //Set the initial panning, as a factor!

	updateModulatorPanningMod(voice); //Update the panning mod!

	//Determine panning!
	lvolume = rvolume = 0.5f; //Default to 50% each (center)!
	panningtemp = voice->initpanning; //Get the panning specified!
	panningtemp += voice->panningmod; //Apply panning CC!
	lvolume -= panningtemp; //Left percentage!
	rvolume += panningtemp; //Right percentage!
	lvolume = LIMITRANGE(lvolume, 0.0f, 1.0f); //Limit!
	rvolume = LIMITRANGE(rvolume, 0.0f, 1.0f); //Limit!
	voice->lvolume = lvolume; //Left panning!
	voice->rvolume = rvolume; //Right panning!

	updateChorusMod(voice);
	updateReverbMod(voice);

	//Apply low pass filter!

	updateMIDILowpassFilter(voice); //Update the low-pass filter!

	MIDIDEVICE_initLFO(voice, &voice->LFO[0], delayModLFO, freqModLFO, modLfoToPitch, modLfoToFilterFc, modLfoToVolume); //Initialize the Modulation LFO!
	MIDIDEVICE_initLFO(voice, &voice->LFO[1], delayVibLFO, freqVibLFO, vibLfoToPitch, endOper, endOper); //Initialize the Vibration LFO!

	//First, set all chorus data and delays!
	for (chorusreverbdepth=0;chorusreverbdepth<CHORUSSIZE;++chorusreverbdepth)
	{
		voice->modulationratiocents[chorusreverbdepth] = 1200; //Default ratio: no modulation!
		voice->modulationratiosamples[chorusreverbdepth] = 1.0f; //Default ratio: no modulation!
		voice->lowpass_modulationratio[chorusreverbdepth] = 1200.0f; //Default ratio: no modulation!
		voice->lowpass_modulationratiosamples[chorusreverbdepth] = voice->lowpassfilter_freq; //Default ratio: no modulation!
		voice->chorusdelay[chorusreverbdepth] = (uint_32)((chorus_delay[chorusreverbdepth])*(float)LE16(voice->sample[1].dwSampleRate)); //Total delay to apply for this channel!
		voice->chorussinposstep = MIDI_CHORUS_SINUS_BASE * (1.0f / (float)LE32(voice->sample[1].dwSampleRate)) * sinustable_percision_reverse; //How much time to add to the chorus sinus after each sample
		voice->chorussinpos[chorusreverbdepth] = fmodf(fmodf((float)(-(chorus_delay[chorusreverbdepth]*(float)LE16(voice->sample[1].dwSampleRate))) * voice->chorussinposstep, (2 * (float)PI)) + (2 * (float)PI), (2 * (float)PI)) * sinustable_percision_reverse; //Initialize the starting chorus sin position for the first sample!
		voice->isfinalchannel_chorus[chorusreverbdepth] = (chorusreverbdepth==(CHORUSSIZE-1)); //Are we the final channel?
		voice->lowpass_dirty[chorusreverbdepth] = 0; //We're not dirty anymore by default: we're loaded!
		voice->last_lowpass[chorusreverbdepth] = modulateLowpass(voice,1.0f,0.0f,voice->lowpassfilter_modenvfactor,(byte)chorusreverbdepth); //The current low-pass filter to use!
		initSoundFilter(&voice->lowpassfilter[chorusreverbdepth][0],0,voice->last_lowpass[chorusreverbdepth],(float)LE32(voice->sample[1].dwSampleRate)); //Apply a default low pass filter to use!
		initSoundFilter(&voice->lowpassfilter[chorusreverbdepth][1],0,voice->last_lowpass[chorusreverbdepth],(float)LE32(voice->sample[1].dwSampleRate)); //Apply a default low pass filter to use!
		voice->lowpass_dirty[chorusreverbdepth] = 0; //We're not dirty anymore by default: we're loaded!
	}

	//Now, set all reverb channel information!
	for (chorusreverbdepth=0;chorusreverbdepth<REVERBSIZE;++chorusreverbdepth)
	{
		voice->reverbvol[chorusreverbdepth] = voice->activereverbdepth[chorusreverbdepth]; //Chorus reverb volume!
		voice->reverbdelay[chorusreverbdepth] = (uint_32)((reverb_delay[chorusreverbdepth])*(float)LE16(voice->sample[1].dwSampleRate)); //Total delay to apply for this channel!
		voice->isfinalchannel_reverb[chorusreverbdepth] = (chorusreverbdepth==(REVERBSIZE-1)); //Are we the final channel?
	}

	for (chorusreverbdepth=0;chorusreverbdepth<CHORUSREVERBSIZE;++chorusreverbdepth)
	{
		initSoundFilter(&voice->reverbfilter[(chorusreverbdepth<<1)],0,voice->lowpassfilter_freq*((chorusreverbdepth<CHORUSSIZE)?1.0f:(0.7f*powf(0.9f,(float)((chorusreverbdepth/CHORUSSIZE)-1)))),(float)LE32(voice->sample[1].dwSampleRate)); //Apply a default low pass filter to use!
		initSoundFilter(&voice->reverbfilter[((chorusreverbdepth<<1)|1)],0,voice->lowpassfilter_freq*((chorusreverbdepth<CHORUSSIZE)?1.0f:(0.7f*powf(0.9f,(float)((chorusreverbdepth/CHORUSSIZE)-1)))),(float)LE32(voice->sample[1].dwSampleRate)); //Apply a default low pass filter to use!
	}

	//Now determine the volume envelope!
	voice->CurrentVolumeEnvelope = 1000.0f; //Default: nothing yet, so no volume, full attenuation!
	voice->CurrentModulationEnvelope = 0.0f; //Default: nothing yet, so no modulation!

	//Apply loop flags!
	voice->currentloopflags[0] = activeloopflags[0]; //Looping setting!
	voice->currentloopflags[1] = activeloopflags[1]; //Looping setting!

	//Save our instrument we're playing!
	voice->instrument = channel->program;
	voice->bank = channel->activebank;

	//Final adjustments and set active!
	ADSR_init(voice, (float)voice->sample[1].dwSampleRate, effectivevelocity, &voice->VolumeEnvelope, soundfont, LE16(instrumentptr.genAmount.wAmount), ibag, preset, pbag, delayVolEnv, attackVolEnv, 1, holdVolEnv, decayVolEnv, sustainVolEnv, releaseVolEnv, effectivenote, keynumToVolEnvHold, keynumToVolEnvDecay); //Initialise our Volume Envelope for use!
	ADSR_init(voice, (float)voice->sample[1].dwSampleRate, effectivevelocity, &voice->ModulationEnvelope, soundfont, LE16(instrumentptr.genAmount.wAmount), ibag, preset, pbag, delayModEnv, attackModEnv, 1, holdModEnv, decayModEnv, sustainModEnv, releaseModEnv, effectivenote, keynumToModEnvHold, keynumToModEnvDecay); //Initialise our Modulation Envelope for use!

	voice->starttime = starttime++; //Take a new start time!
	voice->active = 1; //Active!

	#ifdef MIDI_LOCKSTART
	unlock(voice->locknumber); //Unlock us!
	#endif
	unlockMPURenderer(); //We're finished!
	return 1; //Run: we're active!
}

/* Execution flow support */

void MIDIDEVICE_setupchannelfilters()
{
	sbyte channel, subloop , endrange;
	//MIDI_channels[channel].monophonicchannelcount To take into account?
	for (channel = 0; channel < 0x10; ++channel) //Initialize all channels to not respond to anything!
	{
		MIDI_channels[channel].respondstart = -1; //Respond to nothing!
		MIDI_channels[channel].respondend = -1; //Respond to one channel only!
		MIDI_channels[channel].controlchannel = -1; //Default control channel: none!
		MIDI_channels[channel].globalcontrolchannel = -1; //Default control channel: none!
		MIDI_channels[channel].singlevoice = 0; //Not a single voice only(full poly mode)!
	}
	for (channel = 0; channel < 0x10; ++channel) //Process channels!
	{
		if (MIDI_channels[channel].mode & MIDIDEVICE_OMNI) //Respond to all channels?
		{
			for (subloop = 0; subloop < 0x10; ++subloop) //Initialize all channels to not respond to anything!
			{
				MIDI_channels[subloop].respondstart = -1; //Respond to nothing!
				MIDI_channels[subloop].respondend = -1; //Respond to one channel only!
				MIDI_channels[subloop].controlchannel = subloop; //Default control channel: as specified!
				MIDI_channels[subloop].globalcontrolchannel = -1; //Default control channel: none!
				MIDI_channels[channel].singlevoice = 0; //Not a single voice only(full poly mode)!
			}

			MIDI_channels[channel].respondstart = 0; //Respond to this channel...
			MIDI_channels[channel].respondend = 0xF; //... Only for all channels!
			MIDI_channels[channel].singlevoice = ((MIDI_channels[channel].mode&MIDIDEVICE_POLY)==0); //Not a single voice only(full poly mode when not selecting poly mode)!
			return; //Stop searching!
		}
		else //Respond to selected channel only?
		{
			MIDI_channels[channel].respondstart = channel; //Respond to the ...
			MIDI_channels[channel].respondend = channel; //... Selected channel only!
			MIDI_channels[channel].controlchannel = channel; //Respond on this channel to CC messages!
			if ((MIDI_channels[channel].mode & MIDIDEVICE_POLY) == 0) //Mono with omni off? Affect channel through channel+x-1
			{
				if (MIDI_channels[channel].monophonicchannelcount) //Non-zero: ending channel!
				{
					endrange = MIN(channel + MIDI_channels[channel].monophonicchannelcount - 1, 0xF); //The end of the response range!
				}
				else //Channel 16 is the ending channel!
				{
					endrange = 0xF; //Respond till the final channel!
				}

				for (subloop = MIDI_channels[channel].respondstart; subloop <= endrange; ++subloop) //Setup all effected channels!
				{
					//MIDI_channels[channel].controlchannel = -1; //Don't respond to normal control messages anymore?
					MIDI_channels[channel].respondstart = subloop; //Respond to the ...
					MIDI_channels[channel].respondend = subloop; //... Selected channel only!
					MIDI_channels[channel].globalcontrolchannel = ((channel-1)&0xF); //The global control channel to use instead of the normal channel!
					MIDI_channels[channel].singlevoice = 1; //Single voice only!
				}
				MIDI_channels[channel].controlchannel = -1; //Don't respond to this control channel!
				MIDI_channels[(channel-1)&0xF].controlchannel = -1; //Don't respond to this control channel!
			}
			else //Multiple voices!
			{
				MIDI_channels[channel].respondstart = channel; //Respond to the ...
				MIDI_channels[channel].respondend = channel; //... Selected channel only!
				MIDI_channels[channel].singlevoice = 0; //Not a single voice only(full poly mode)!
			}
		}
		channel = MIDI_channels[channel].respondend; //Continue at the next channel!
	}
}

//channel=Channel to check response for, selectedchannel=One of all channels, in order!
OPTINLINE byte MIDIDEVICE_FilterChannelVoice(byte selectedchannel, byte channel, byte filterchannel)
{
	if (filterchannel == 0) //Disabled on other channels?
	{
		return (selectedchannel == channel); //Filter the channel only!
	}
	//Follow the normal rules for channel responding!
	if (MIDI_channels[channel].respondstart != -1) //Responding setup?
	{
		if (MIDI_channels[channel].respondend != -1) //Range specified?
		{
			if (!((selectedchannel >= MIDI_channels[channel].respondstart) && (selectedchannel <= MIDI_channels[channel].respondend))) //Out of range?
			{
				return 0; //Not responding!
			}
		}
		else //Single channel specified?
		{
			if (selectedchannel != MIDI_channels[channel].respondstart) //Wrong channel?
			{
				return 0; //Not responding!
			}
		}
	}
	else //Not responding at all?
	{
		return 0; //Not responding!
	}
	//Poly mode and Omni mode: Respond to all on any channel = Ignore the channel with Poly Mode!
	return 1;
}

OPTINLINE void MIDIDEVICE_noteOff(byte selectedchannel, byte channel, byte note, byte velocity, byte filterchannel)
{
	if (MIDIDEVICE_FilterChannelVoice(selectedchannel,channel,filterchannel)) //To be applied?
	{
		int i;
		for (i = 0; i < MIDI_TOTALVOICES; i++) //Process all voices!
		{
			#ifdef MIDI_LOCKSTART
			lock(activevoices[i].locknumber); //Lock us!
			#endif
			if (activevoices[i].VolumeEnvelope.active && activevoices[i].allocated) //Active note?
			{
				if ((activevoices[i].note->channel == channel) && (activevoices[i].note->note == note)) //Note found?
				{
					activevoices[i].request_off = 1; //We're requesting to be turned off!
					activevoices[i].note->noteoff_velocity = velocity; //Note off velocity!
				}
			}
			#ifdef MIDI_LOCKSTART
			unlock(activevoices[i].locknumber); //Unlock us!
			#endif
		}
	}
}

OPTINLINE void MIDIDEVICE_AllNotesOff(byte selectedchannel, byte channel, byte channelspecific) //Used with command, mode change and Mono Mode.
{
	word noteoff; //Current note to turn off!
	//Note values
	MIDIDEVICE_setupchannelfilters(); //Setup the channel filters!
	for (noteoff=0;noteoff<0x100;) //Process all notes!
	{
		MIDIDEVICE_noteOff(selectedchannel,channel,(byte)noteoff++,64,channelspecific); //Execute Note Off!
	}
	#ifdef MIDI_LOG
	dolog("MPU","MIDIDEVICE: ALL NOTES OFF: %u",selectedchannel); //Log it!
	#endif
}

SDL_sem *activeSenseLock = NULL; //Active Sense lock!

byte MIDIDEVICE_ActiveSensing = 0; //Active Sensing?
word MIDIDEVICE_ActiveSenseCounter = 0; //Counter for Active Sense!

void MIDIDEVICE_activeSense_Timer() //Timeout while Active Sensing!
{
	if (MIDIDEVICE_ActiveSensing) //Are we Active Sensing?
	{
		PostSem(activeSenseLock) //Unlock!
		if (shuttingdown()) //Shutting down?
		{
			WaitSem(activeSenseLock) //Relock!
			MIDIDEVICE_ActiveSensing = 0; //Not sensing anymore!
			return; //Abort!
		}
		WaitSem(activeSenseLock) //Relock!
		if (++MIDIDEVICE_ActiveSenseCounter > 300) //300ms passed?
		{
			byte channel, currentchannel;
			MIDIDEVICE_ActiveSensing = 0; //Not sensing anymore!
			PostSem(activeSenseLock) //Unlock!
			lock(LOCK_MAINTHREAD); //Make sure we're the only ones!
			if (shuttingdown()) //Shutting down?
			{
				unlock(LOCK_MAINTHREAD);
				WaitSem(activeSenseLock) //Relock!
				return; //Abort!
			}
			for (currentchannel = 0; currentchannel < 0x10;) //Process all active channels!
			{
				for (channel = 0; channel < 0x10;)
				{
					MIDIDEVICE_AllNotesOff(currentchannel, channel++, 0); //Turn all notes off!
				}
				++currentchannel; //Next channel!
			}
			unlock(LOCK_MAINTHREAD);
			WaitSem(activeSenseLock) //Relock!
		}
	}
}

void MIDIDEVICE_tickActiveSense() //Tick the Active Sense (MIDI) line with any command/data!
{
	WaitSem(activeSenseLock)
	MIDIDEVICE_ActiveSenseCounter = 0; //Reset the counter to count again!
	PostSem(activeSenseLock)
}


void MIDIDEVICE_ActiveSenseFinished()
{
	removetimer("MIDI Active Sense Timeout"); //Remove the current timeout, if any!
	if (activeSenseLock) //Is Active Sensing used?
	{
		SDL_DestroySemaphore(activeSenseLock); //Destroy our lock!
		activeSenseLock = NULL; //Nothing anymore!
	}
}

void MIDIDEVICE_ActiveSenseInit()
{
	MIDIDEVICE_ActiveSenseFinished(); //Finish old one!
	activeSenseLock = SDL_CreateSemaphore(1); //Create our lock!
	addtimer(300.0f / 1000.0f, &MIDIDEVICE_activeSense_Timer, "MIDI Active Sense Timeout", 1, 1, activeSenseLock); //Add the Active Sense timer!
}

OPTINLINE void MIDIDEVICE_noteOn(byte selectedchannel, byte channel, byte note, byte velocity)
{
	byte purpose;
	word requestedvoice;
	sbyte newvoiceresult;
	word voicelimit;
	int voice, foundvoice, voicetosteal, voiceactive;
	int_32 stolenvoiceranking, currentranking; //Stolen voice ranking starts lowest always!
	voicelimit = MIDI_NOTEVOICES; //Amount of voices that can be allocated for each note on!

	if (MIDIDEVICE_FilterChannelVoice(selectedchannel,channel,1)) //To be applied?
	{
		if (MIDI_channels[channel].singlevoice) //Single voice only?
		{
			MIDIDEVICE_AllNotesOff(selectedchannel,channel,1); //Turn all notes off on the selected channel first!
		}
		MIDI_channels[channel].notes[note].noteon_velocity = velocity; //Add velocity to our lookup!
		requestedvoice = 0; //Try to allocate the first voice, if any!
		voice = 0; //Start at the first voice for the first search only!
		nextRequestedVoice: //Perform the next requested voice!
		purpose = (channel==MIDI_DRUMCHANNEL)?1:0; //Are we a drum channel?
		foundvoice = -1;
		voicetosteal = -1;
		stolenvoiceranking = 0; //Stolen voice ranking starts lowest always!
		for (; voice < MIDI_TOTALVOICES; voice += voicelimit) //Find a voice!
		{
			if (activevoices[voice].purpose==purpose) //Our type of channel (drums vs melodic channels)?
			{
				if ((newvoiceresult = MIDIDEVICE_newvoice(&activevoices[voice], channel, note, requestedvoice))!=0) //Needs voice stealing or made active?
				{
					if ((newvoiceresult == 1) || (newvoiceresult==-2)) //Allocated and made active(1)? Or can't render(-2)? We don't need to steal any voices!
					{
						if (newvoiceresult == 1) ++voice; //Next voice to allocate next!
						foundvoice = voice; //What voice has been found!
						goto nextallocation; //Perform the next allocation!
					}
				}
				else //Not allocated?
				{
					return; //Nothing to allocate! We're finished adding all available voices!
				}

				//Unable to allocate? Perform ranking if it's active!
				for (voiceactive = voice; voiceactive < (voice + voicelimit); ++voiceactive) //Check all subvoices!
				{
					if (activevoices[voiceactive].active && activevoices[voiceactive].allocated) //Are we active and the first voice?
					{
						//Create ranking by scoring the voice!
						currentranking = 0; //Start with no ranking!
						if (activevoices[voiceactive].VolumeEnvelope.active == ADSR_IDLE) currentranking -= 4000; //Idle gets priority to be stolen!
						else if (activevoices[voiceactive].VolumeEnvelope.active == ADSR_RELEASE) currentranking -= 2000; //Release gets priority to be stolen!
						if (activevoices[voiceactive].channel->sustain | ((activevoices[voiceactive].currentloopflags[1]|activevoices[voiceactive].currentloopflags[0])&0x10)) currentranking -= 1000; //Lower when sustained or sostenuto!
						float volume;
						volume = combineAttenuation(&activevoices[voiceactive],activevoices[voiceactive].effectiveAttenuation, activevoices[voiceactive].CurrentVolumeEnvelope); //Load the ADSR volume!
						if (activevoices[voiceactive].lvolume > activevoices[voice].rvolume) //More left volume?
						{
							volume *= activevoices[voiceactive].lvolume; //Left volume!
						}
						else
						{
							volume *= activevoices[voiceactive].rvolume; //Right volume!
						}
						currentranking += (int_32)(volume * 1000.0f); //Factor in volume, on a scale of 1000!
						if ((stolenvoiceranking > currentranking) || (voicetosteal == -1)) //We're a lower rank or the first ranking?
						{
							stolenvoiceranking = currentranking; //New voice to steal!
							voicetosteal = voice; //Steal this voice, if needed!
						}
						else if ((currentranking == stolenvoiceranking) && (voicetosteal != -1)) //Same ranking as the last one found?
						{
							if (activevoices[voiceactive].starttime < activevoices[voicetosteal].starttime) //Earlier start time with same ranking?
							{
								voicetosteal = voice; //Steal this voice, if needed!
							}
						}
					}
				}
			}
		}
		if (foundvoice == -1) //No channels available? We need voice stealing!
		{
			//Perform voice stealing using voicetosteal, if available!
			if (voicetosteal != -1) //Something to steal?
			{
				lockMPURenderer();
				for (voice = voicetosteal; voice < (voicetosteal + voicelimit); ++voice)
				{
					#ifdef MIDI_LOCKSTART
					lock(activevoices[voice].locknumber); //Lock us!
					#endif
					activevoices[voice].active = 0; //Make inactive!
					#ifdef MIDI_LOCKSTART
					unlock(activevoices[voice].locknumber); //unlock us!
					#endif
				}
				unlockMPURenderer();
				newvoiceresult = MIDIDEVICE_newvoice(&activevoices[voicetosteal], channel,note,requestedvoice); //Steal the selected voice!
				voice = voicetosteal + 1; //Next voice to use!
			}
		}
		nextallocation: //Check for any next voice to allocate!
		//Else: allocated!
		++requestedvoice; //The next voice to check!
		if (requestedvoice >= voicelimit) //More than the maximum amount of voices allocated?
		{
			return; //Finish up!
		}
		activevoices[voice].active = 0; //Make sure we allocate the next voice without issues!
		goto nextRequestedVoice; //Handle the next requested voice!
	}
}

void updateMIDImodulators(byte channel)
{
	word voicenr;
	MIDIDEVICE_VOICE* voice;
	voicenr = 0; //First voice!
	for (; voicenr < MIDI_TOTALVOICES; ++voicenr) //Find a used voice!
	{
		voice = &activevoices[voicenr]; //The voice!
		if (voice->VolumeEnvelope.active && voice->allocated) //Active?
		{
			if (voice->channel == &MIDI_channels[channel]) //The requested channel?
			{
				calcAttenuationModulators(voice); //Calc the modulators!
				updateSampleSpeed(voice); //Calc the pitch wheel!
				updateModulatorPanningMod(voice); //Calc the panning modulators!
				updateMIDILowpassFilter(voice); //Update the low-pass filter!
				MIDIDEVICE_updateLFO(voice, &voice->LFO[0]); //Update the first LFO!
				MIDIDEVICE_updateLFO(voice, &voice->LFO[1]); //Update the second LFO!
			}
		}
	}
}

void startMIDIsostenuto(byte channel)
{
	word voicenr;
	MIDIDEVICE_VOICE* voice;
	voicenr = 0; //First voice!
	for (; voicenr < MIDI_TOTALVOICES; ++voicenr) //Find a used voice!
	{
		voice = &activevoices[voicenr]; //The voice!
		if (voice->VolumeEnvelope.active && voice->allocated) //Active?
		{
			if (voice->channel == &MIDI_channels[channel]) //The requested channel?
			{
				if (((voice->currentloopflags[1]&0xD0)!=0x80) && (voice->VolumeEnvelope.active!=ADSR_RELEASE)) //Still pressed and not releasing?
				{
					voice->currentloopflags[0] |= 0x10; //Set sostenuto!
					voice->currentloopflags[1] |= 0x10; //Set sostenuto!
				}
			}
		}
	}
}

void stopMIDIsostenuto(byte channel)
{
	word voicenr;
	MIDIDEVICE_VOICE* voice;
	voicenr = 0; //First voice!
	for (; voicenr < MIDI_TOTALVOICES; ++voicenr) //Find a used voice!
	{
		voice = &activevoices[voicenr]; //The voice!
		if (voice->VolumeEnvelope.active && voice->allocated) //Active?
		{
			if (voice->channel == &MIDI_channels[channel]) //The requested channel?
			{
				voice->currentloopflags[0] &= ~0x10; //Clear sostenuto!
				voice->currentloopflags[1] &= ~0x10; //Clear sostenuto!
			}
		}
	}
}

OPTINLINE void MIDIDEVICE_execMIDI(MIDIPTR current) //Execute the current MIDI command!
{
	//First, our variables!
	byte command, currentchannel, channel, firstparam;

	//Process the current command!
	command = current->command; //What command!
	currentchannel = command; //What channel!
	currentchannel &= 0xF; //Make sure we're OK!
	firstparam = current->buffer[0]; //Read the first param: always needed!
	switch (command&0xF0) //What command?
	{
		noteoff: //Note off!
			#ifdef MIDI_LOG
				if ((command & 0xF0) == 0x90) dolog("MPU", "MIDIDEVICE: NOTE ON: Redirected to NOTE OFF.");
			#endif

		case 0x80: //Note off?
			MIDIDEVICE_setupchannelfilters(); //Setup the channel filters!
			for (channel=0;channel<0x10;) //Process all channels!
			{
				MIDIDEVICE_noteOff(currentchannel,channel++,firstparam,current->buffer[1],1); //Execute Note Off!
			}
			#ifdef MIDI_LOG
				dolog("MPU","MIDIDEVICE: NOTE OFF: Channel %u Note %u Velocity %u",currentchannel,firstparam,current->buffer[1]); //Log it!
			#endif
			break;
		case 0x90: //Note on?
			if (!current->buffer[1])
			{
				current->buffer[1] = 0x40; //The specified note off velocity!
				goto noteoff; //Actually a note off?
			}
			MIDIDEVICE_setupchannelfilters(); //Setup the channel filters to use!
			for (channel=0;channel<0x10;) //Process all channels!
			{
				MIDIDEVICE_noteOn(currentchannel, channel++, firstparam, current->buffer[1]); //Execute Note On!
			}
			#ifdef MIDI_LOG
				dolog("MPU","MIDIDEVICE: NOTE ON: Channel %u Note %u Velocity %u",currentchannel,firstparam,current->buffer[1]); //Log it!
			#endif
			break;
		case 0xA0: //Aftertouch?
			lockMPURenderer(); //Lock the audio!
			MIDI_channels[currentchannel].notes[firstparam].pressure = current->buffer[1];
			updateMIDImodulators(currentchannel); //Update!
			unlockMPURenderer(); //Unlock the audio!
			#ifdef MIDI_LOG
				dolog("MPU","MIDIDEVICE: Aftertouch: %u-%u",currentchannel,MIDI_channels[currentchannel].notes[firstparam].pressure); //Log it!
			#endif
			break;
		case 0xB0: //Control change?
			switch (firstparam) //What control?
			{
				case 0x00: //Bank Select (MSB)
					#ifdef MIDI_LOG
						dolog("MPU","MIDIDEVICE: Bank select MSB on channel %u: %02X",currentchannel,current->buffer[1]); //Log it!
					#endif
						if (currentchannel != MIDI_DRUMCHANNEL) //Don't receive on channel 9: it's locked!
						{
							lockMPURenderer(); //Lock the audio!
							MIDI_channels[currentchannel].bank &= 0x3F80; //Only keep MSB!
							MIDI_channels[currentchannel].bank |= current->buffer[1]; //Set LSB!
							unlockMPURenderer(); //Unlock the audio!
						}
					break;
				case 0x20: //Bank Select (LSB) (see cc0)
#ifdef MIDI_LOG
					dolog("MPU", "MIDIDEVICE: Bank select LSB on channel %u: %02X", currentchannel, current->buffer[1]); //Log it!
#endif
					if (currentchannel != MIDI_DRUMCHANNEL) //Don't receive on channel 9: it's locked!
					{
						lockMPURenderer(); //Lock the audio!
						MIDI_channels[currentchannel].bank &= 0x7F; //Only keep LSB!
						MIDI_channels[currentchannel].bank |= (current->buffer[1] << 7); //Set MSB!
						unlockMPURenderer(); //Unlock the audio!
					}
					break;

				case 0x07: //Volume (MSB) CC 07
					#ifdef MIDI_LOG
						dolog("MPU", "MIDIDEVICE: Volume MSB on channel %u: %02X",currentchannel, current->buffer[1]); //Log it!
					#endif
					lockMPURenderer(); //Lock the audio!
					MIDI_channels[currentchannel].volumeMSB = current->buffer[1]; //Set MSB!
					MIDI_channels[currentchannel].ContinuousControllers[firstparam] = (current->buffer[1] & 0x7F); //Specify the CC itself!
					updateMIDImodulators(currentchannel); //Update!
					unlockMPURenderer(); //Unlock the audio!
					break;
				case 0x0B: //Expression (MSB) CC 11
					#ifdef MIDI_LOG
						dolog("MPU", "MIDIDEVICE: Volume MSB on channel %u: %02X",currentchannel, current->buffer[1]); //Log it!
					#endif
					lockMPURenderer(); //Lock the audio!
					MIDI_channels[currentchannel].expression = current->buffer[1]; //Set Expression!
					MIDI_channels[currentchannel].ContinuousControllers[firstparam] = (current->buffer[1] & 0x7F); //Specify the CC itself!
					updateMIDImodulators(currentchannel); //Update!
					unlockMPURenderer(); //Unlock the audio!
					break;
				case 0x27: //Volume (LSB) CC 39
#ifdef MIDI_LOG
					dolog("MPU", "MIDIDEVICE: Volume LSB on channel %u: %02X", currentchannel, current->buffer[1]); //Log it!
#endif
					lockMPURenderer(); //Lock the audio!
					MIDI_channels[currentchannel].volumeLSB = current->buffer[1]; //Set LSB!
					MIDI_channels[currentchannel].ContinuousControllers[firstparam] = (current->buffer[1] & 0x7F); //Specify the CC itself!
					updateMIDImodulators(currentchannel); //Update!
					unlockMPURenderer(); //Unlock the audio!
					break;

				case 0x0A: //Pan position (MSB)
					#ifdef MIDI_LOG
						dolog("MPU", "MIDIDEVICE: Pan position MSB on channel %u: %02X",currentchannel, current->buffer[1]); //Log it!
					#endif
					lockMPURenderer(); //Lock the audio!
					MIDI_channels[currentchannel].panposition &= 0x3F80; //Only keep MSB!
					MIDI_channels[currentchannel].panposition |= current->buffer[1]; //Set LSB!
					MIDI_channels[currentchannel].ContinuousControllers[firstparam] = (current->buffer[1] & 0x7F); //Specify the CC itself!
					updateMIDImodulators(currentchannel); //Update!
					unlockMPURenderer(); //Unlock the audio!
					break;
				case 0x2A: //Pan position (LSB)
					#ifdef MIDI_LOG
						dolog("MPU", "MIDIDEVICE: Pan position LSB on channel %u: %02X",currentchannel, current->buffer[1]); //Log it!
					#endif
					lockMPURenderer(); //Lock the audio!
					MIDI_channels[currentchannel].panposition &= 0x7F; //Only keep LSB!
					MIDI_channels[currentchannel].panposition |= (current->buffer[1] << 7); //Set MSB!
					MIDI_channels[currentchannel].ContinuousControllers[firstparam] = (current->buffer[1] & 0x7F); //Specify the CC itself!
					updateMIDImodulators(currentchannel); //Update!
					unlockMPURenderer(); //Unlock the audio!
					break;

				//case 0x01: //Modulation wheel (MSB)
					//break;
				//case 0x04: //Foot Pedal (MSB)
					//break;
				//case 0x06: //Data Entry, followed by cc100&101 for the address.
					//break;
				//case 0x21: //Modulation wheel (LSB)
					//break;
				//case 0x24: //Foot Pedal (LSB)
					//break;
				//case 0x26: //Data Entry, followed by cc100&101 for the address.
					//break;
				case 0x40: //Hold Pedal (On/Off) = Sustain Pedal
					#ifdef MIDI_LOG
						dolog("MPU", "MIDIDEVICE:  Channel %u; Hold pedal: %02X=%u", currentchannel, current->buffer[1],(current->buffer[1]&MIDI_CONTROLLER_ON)?1:0); //Log it!
					#endif
					lockMPURenderer(); //Lock the audio!
					MIDI_channels[currentchannel].sustain = (current->buffer[1]&MIDI_CONTROLLER_ON)?1:0; //Sustain?
					MIDI_channels[currentchannel].ContinuousControllers[firstparam] = (current->buffer[1] & 0x7F); //Specify the CC itself!
					unlockMPURenderer(); //Unlock the audio!
					break;
				case 0x42: //Sostenuto (on/off)
					#ifdef MIDI_LOG
						dolog("MPU", "MIDIDEVICE:  Channel %u; Sostenuto: %02X=%u", currentchannel, current->buffer[1],(current->buffer[1]&MIDI_CONTROLLER_ON)?1:0); //Log it!
					#endif
					lockMPURenderer(); //Lock the audio!
					if ((MIDI_channels[currentchannel].sostenuto==0) && ((current->buffer[1]&MIDI_CONTROLLER_ON))) //Turned on?
					{
						MIDI_channels[currentchannel].sostenuto = 1; //Turned on now!
						startMIDIsostenuto(currentchannel); //Start sostenuto for all running voices!
					}
					else if (MIDI_channels[currentchannel].sostenuto && ((current->buffer[1]&MIDI_CONTROLLER_ON)==0)) //Turned off?
					{
						MIDI_channels[currentchannel].sostenuto = 0; //Turned off now!
						stopMIDIsostenuto(currentchannel); //Stop sostenuto for all running voices!
					}
					MIDI_channels[currentchannel].ContinuousControllers[firstparam] = (current->buffer[1] & 0x7F); //Specify the CC itself!
					unlockMPURenderer(); //Unlock the audio!
					break;
				case 0x06: //(N)RPN data entry high
					switch (MIDI_channels[currentchannel].RPNmode) //What RPN mode?
					{
					case 0: //Nothing: ignore!
						break;
					case 1: //RPN mode!
						if ((MIDI_channels[currentchannel].RPNhi == 0) && (MIDI_channels[currentchannel].RPNlo == 0)) //Pitch wheel Sensitivity?
						{
							MIDI_channels[currentchannel].pitchbendsensitivitysemitones = (current->buffer[1]&0x7F); //Semitones!
							updateMIDImodulators(currentchannel); //Update!
						}
						//127,127=NULL!
						//Other RPNs aren't supported!
						break;
					case 2: //NRPN mode!
						//Use RPNhi and RPNlo for the selection?
						//None supported!
						break;
					case 3: //Soundfont 2.04 NRPN mode!
						break;
					}
					break;
				case 0x26: //(N)RPN data entry low
					switch (MIDI_channels[currentchannel].RPNmode) //What RPN mode?
					{
					case 0: //Nothing: ignore!
						break;
					case 1: //RPN mode!
						if ((MIDI_channels[currentchannel].RPNhi == 0) && (MIDI_channels[currentchannel].RPNlo == 0)) //Pitch wheel Sensitivity?
						{
							MIDI_channels[currentchannel].pitchbendsensitivitycents = (current->buffer[1] & 0x7F); //Cents!
							updateMIDImodulators(currentchannel); //Update!
						}
						//127,127=NULL!
						//Other RPNs aren't supported!
						break;
					case 2: //NRPN mode!
						//Use RPNhi and RPNlo for the selection?
						//None supported!
						break;
					case 3: //Soundfont 2.04 NRPN mode!
						//Use NRPNnumber for the selection!
						//Not supported by this implementation!
						break;
					}
					break;
				case 0x65: //RPN high
					MIDI_channels[currentchannel].RPNhi = (current->buffer[1] & 0x7F); //RPN high!
					MIDI_channels[currentchannel].RPNmode = 1; //RPN mode!
					break;
				case 0x64: //RPN low
					MIDI_channels[currentchannel].RPNlo = (current->buffer[1] & 0x7F); //RPN low!
					MIDI_channels[currentchannel].RPNmode = 1; //RPN mode!
					break;
				case 0x63: //NRPN high
					MIDI_channels[currentchannel].NRPNhi = (current->buffer[1] & 0x7F); //RPN high!
					if (MIDI_channels[currentchannel].NRPNhi != 120) //Normal mode!
					{
						MIDI_channels[currentchannel].RPNmode = 2; //Normal NRPN mode!
						MIDI_channels[currentchannel].NRPNpendingmode = 0; //Normal mode!
					}
					else
					{
						MIDI_channels[currentchannel].NRPNpendingmode = 1; //Soundfont 2.01 mode style index!
						MIDI_channels[currentchannel].NRPNnumber = 0; //Initialize the number!
						MIDI_channels[currentchannel].NRPNnumbercounter = 0; //Initialize the number!
						MIDI_channels[currentchannel].RPNmode = 3; //Soundfont mode!
					}
					break;
				case 0x62: //NPRN low
					if (MIDI_channels[currentchannel].NRPNpendingmode == 0) //Normal mode?
					{
						MIDI_channels[currentchannel].NRPNlo = (current->buffer[1] & 0x7F); //RPN low!
						MIDI_channels[currentchannel].RPNmode = 2; //NRPN mode!
					}
					else //Soundfont 2.01 mode?
					{
						MIDI_channels[currentchannel].RPNmode = 3; //Soundfont mode!
						if ((current->buffer[1] & 0x7F) < 100) //Finish count?
						{
							MIDI_channels[currentchannel].NRPNnumbercounter += (current->buffer[1] & 0x7F); //Finish the counting!
							MIDI_channels[currentchannel].NRPNnumber = MIDI_channels[currentchannel].NRPNnumbercounter; //Latch!
							MIDI_channels[currentchannel].NRPNnumbercounter = 0; //Clear the count for a new entry to be written!
						}
						else if ((current->buffer[1] & 0x7F) == 101) //Count 100?
						{
							MIDI_channels[currentchannel].NRPNnumbercounter += 100; //Continue the counting!
							MIDI_channels[currentchannel].NRPNnumber = MIDI_channels[currentchannel].NRPNnumbercounter; //Latch!
						}
						else if ((current->buffer[1] & 0x7F) == 102) //Count 1000?
						{
							MIDI_channels[currentchannel].NRPNnumbercounter += 1000; //Continue the counting!
							MIDI_channels[currentchannel].NRPNnumber = MIDI_channels[currentchannel].NRPNnumbercounter; //Latch!
						}
						else if ((current->buffer[1] & 0x7F) == 103) //Count 10000?
						{
							MIDI_channels[currentchannel].NRPNnumbercounter += 10000; //Continue the counting!
							MIDI_channels[currentchannel].NRPNnumber = MIDI_channels[currentchannel].NRPNnumbercounter; //Latch!
						}
						//Other values are ignored, according to Soundfont 2.04!
					}
					break;
				//case 0x41: //Portamento (On/Off)
					//break;
				//case 0x47: //Resonance a.k.a. Timbre
					//break;
				//case 0x4A: //Frequency Cutoff (a.k.a. Brightness)
					//break;
				case 0x5B: //Reverb Level
					lockMPURenderer(); //Lock the audio!
					MIDI_channels[currentchannel].reverblevel = current->buffer[1]; //Reverb level!
					MIDI_channels[currentchannel].ContinuousControllers[firstparam] = (current->buffer[1] & 0x7F); //Specify the CC itself!
					updateMIDImodulators(currentchannel); //Update!
					unlockMPURenderer(); //Unlock the audio!
					break;
				case 0x5D: //Chorus Level
					lockMPURenderer(); //Lock the audio!
					MIDI_channels[currentchannel].choruslevel = current->buffer[1]; //Chorus level!
					MIDI_channels[currentchannel].ContinuousControllers[firstparam] = (current->buffer[1] & 0x7F); //Specify the CC itself!
					updateMIDImodulators(currentchannel); //Update!
					unlockMPURenderer(); //Unlock the audio!
					break;
					//Sound function On/Off:
				//case 0x78: //All Sound Off
					//break;
				//case 0x79: //All Controllers Off
					//break;
				//case 0x7A: //Local Keyboard On/Off
					//break;
				case 0x7B: //All Notes Off
				case 0x7C: //Omni Mode Off
				case 0x7D: //Omni Mode On
				case 0x7E: //Mono operation
				case 0x7F: //Poly Operation
					for (channel=0;channel<0x10;)
					{
						MIDIDEVICE_AllNotesOff(currentchannel,channel++,0); //Turn all notes off!
					}
					if ((firstparam&0x7C)==0x7C) //Mode change command?
					{
						lockMPURenderer(); //Lock the audio!
						switch (firstparam&3) //What mode change?
						{
						case 0: //Omni Mode Off
							#ifdef MIDI_LOG
								dolog("MPU", "MIDIDEVICE: Channel %u, OMNI OFF", currentchannel); //Log it!
							#endif
							MIDI_channels[currentchannel].mode &= ~MIDIDEVICE_OMNI; //Disable Omni mode!
							break;
						case 1: //Omni Mode On
							#ifdef MIDI_LOG
								dolog("MPU", "MIDIDEVICE: Channel %u, OMNI ON", currentchannel); //Log it!
							#endif
							MIDI_channels[currentchannel].mode |= MIDIDEVICE_OMNI; //Enable Omni mode!
							break;
						case 2: //Mono operation
							MIDI_channels[currentchannel].mode &= ~MIDIDEVICE_POLY; //Disable Poly mode and enter mono mode!
							MIDI_channels[currentchannel].monophonicchannelcount = current->buffer[1]; //Channel count, if non-zero!
							break;
						case 3: //Poly Operation
							#ifdef MIDI_LOG
								dolog("MPU", "MIDIDEVICE: Channel %u, POLY", currentchannel); //Log it!
							#endif
							MIDI_channels[currentchannel].mode |= MIDIDEVICE_POLY; //Enable Poly mode!
							break;
						default:
							break;
						}
						unlockMPURenderer(); //Unlock the audio!
					}
					break;
				default: //Unknown controller?
					#ifdef MIDI_LOG
						dolog("MPU", "MIDIDEVICE: Unknown Continuous Controller change: %u=%u", currentchannel, firstparam); //Log it!
					#endif
					MIDI_channels[currentchannel].ContinuousControllers[firstparam] = (current->buffer[1] & 0x7F); //Specify the CC itself!
					updateMIDImodulators(currentchannel); //Update!
					break;
			}
			break;
		case 0xC0: //Program change?
			lockMPURenderer(); //Lock the audio!
			MIDI_channels[currentchannel].program = firstparam; //What program?
			MIDI_channels[currentchannel].activebank = MIDI_channels[currentchannel].bank; //Apply bank from Bank Select Messages!
			unlockMPURenderer(); //Unlock the audio!
			#ifdef MIDI_LOG
				dolog("MPU","MIDIDEVICE: Program change: %u=%u",currentchannel,MIDI_channels[currentchannel].program); //Log it!
			#endif
			break;
		case 0xD0: //Channel pressure?
			lockMPURenderer(); //Lock the audio!
			MIDI_channels[currentchannel].pressure = firstparam;
			updateMIDImodulators(currentchannel); //Update!
			unlockMPURenderer(); //Unlock the audio!
			#ifdef MIDI_LOG
				dolog("MPU","MIDIDEVICE: Channel pressure: %u=%u",currentchannel,MIDI_channels[currentchannel].pressure); //Log it!
			#endif
			break;
		case 0xE0: //Pitch wheel?
			lockMPURenderer(); //Lock the audio!
			MIDI_channels[currentchannel].pitch = (sword)((current->buffer[1]<<7)|firstparam); //Actual pitch, converted to signed value!
			updateMIDImodulators(currentchannel); //Update!
			unlockMPURenderer(); //Unlock the audio!
			#ifdef MIDI_LOG
				dolog("MPU","MIDIDEVICE: Pitch wheel: %u=%u",currentchannel,MIDI_channels[currentchannel].pitch); //Log it!
			#endif
			break;
		case 0xF0: //System message?
			//We don't handle system messages!
			switch (command)
			{
			case 0xFE: //Active Sense?
				MIDIDEVICE_ActiveSensing = 1; //We're Active Sensing!
				break;
			case 0xFF: //Reset?
				reset_MIDIDEVICE(); //Reset ourselves!
				break;
			default:
				#ifdef MIDI_LOG
					dolog("MPU", "MIDIDEVICE: System messages are unsupported!"); //Log it!
				#endif
				break;
			}
			break;
		default: //Invalid command?
			#ifdef MIDI_LOG
				dolog("MPU","MIDIDEVICE: Unknown command: %02X",command);
			#endif
			break; //Do nothing!
	}
}

/* Buffer support */

void MIDIDEVICE_addbuffer(byte command, MIDIPTR data) //Add a command to the buffer!
{
	#ifdef __HW_DISABLED
	return; //We're disabled!
	#endif

	#ifdef IS_WINDOWS
	if (direct_midi)
	{
		//We're directly sending MIDI to the output!
		union { unsigned long word; unsigned char data[4]; } message;
		message.data[0] = command; //The command!
		message.data[1] = data->buffer[0];
		message.data[2] = data->buffer[1];
		message.data[3] = 0; //Unused!
		switch (command&0xF0) //What command?
		{
		case 0x80:
		case 0x90:
		case 0xA0:
		case 0xB0:
		case 0xC0:
		case 0xD0:
		case 0xE0:
		case 0xF0:
			if (command != 0xFF) //Not resetting?
			{
				flag = midiOutShortMsg(device, message.word);
				if (flag != MMSYSERR_NOERROR) {
					printf("Warning: MIDI Output is not open.\n");
				}
			}
			else
			{
				// turn any MIDI notes currently playing:
				midiOutReset(device);
			}
			break;
		}
		return; //Stop: ready!
	}
	#endif

	data->command = command; //Set the command to use!
	MIDIDEVICE_execMIDI(data); //Execute directly!
}

/* Init/destroy support */
extern byte RDPDelta; //RDP toggled?
void done_MIDIDEVICE() //Finish our midi device!
{
	#ifdef __HW_DISABLED
		return; //We're disabled!
	#endif
	#ifdef IS_WINDOWS
	if (direct_midi)
	{
		// turn any MIDI notes currently playing:
		midiOutReset(device);
		lock(LOCK_INPUT);
		if (RDPDelta&1)
		{
			unlock(LOCK_INPUT);
			return;
		}
		unlock(LOCK_INPUT);
		// Remove any data in MIDI device and close the MIDI Output port
		midiOutClose(device);
		//We're directly sending MIDI to the output!
		return; //Stop: ready!
	}
	#endif
	
	lockaudio();
	//Close the soundfont?
	closeSF(&soundfont);
	int i,j;
	for (i=0;i<MIDI_TOTALVOICES;i++) //Assign all voices available!
	{
		removechannel(&MIDIDEVICE_renderer,&activevoices[i],0); //Remove the channel! Delay at 0.96ms for response speed!
		if (activevoices[i].effect_backtrace_samplespeedup_modenv_pitchfactor) //Used?
		{
			free_fifobuffer(&activevoices[i].effect_backtrace_samplespeedup_modenv_pitchfactor); //Release the FIFO buffer containing the entire history!
		}
		if (activevoices[i].effect_backtrace_LFO1) //Used?
		{
			free_fifobuffer(&activevoices[i].effect_backtrace_LFO1); //Release the FIFO buffer containing the entire history!
		}
		if (activevoices[i].effect_backtrace_LFO2) //Used?
		{
			free_fifobuffer(&activevoices[i].effect_backtrace_LFO2); //Release the FIFO buffer containing the entire history!
		}
		if (activevoices[i].effect_backtrace_lowpassfilter_modenvfactor) //Used?
		{
			free_fifobuffer(&activevoices[i].effect_backtrace_lowpassfilter_modenvfactor); //Release the FIFO buffer containing the entire history!
		}
#ifndef DISABLE_REVERB
		for (j=0;j<CHORUSSIZE;++j)
		{
			free_fifobuffer(&activevoices[i].effect_backtrace_chorus[j]); //Release the FIFO buffer containing the entire history!
		}
		#endif
	}
	MIDIDEVICE_ActiveSenseFinished(); //Finish our Active Sense: we're not needed anymore!
	unlockaudio();
}

byte init_MIDIDEVICE(char *filename, byte use_direct_MIDI) //Initialise MIDI device for usage!
{
	float MIDI_CHORUS_SINUS_CENTS;
	MIDI_CHORUS_SINUS_CENTS = (0.5f*CHORUS_LFO_CENTS); //Cents modulation for the outgoing sinus!
	byte result;
	#ifdef __HW_DISABLED
		return 0; //We're disabled!
	#endif
	#ifdef IS_WINDOWS
	direct_midi = use_direct_MIDI; //Use direct MIDI synthesis by the OS, if any?
	if (direct_midi)
	{
		lock(LOCK_INPUT);
		RDPDelta &= ~1; //Clear our RDP delta flag!
		unlock(LOCK_INPUT);
		// Open the MIDI output port
		flag = midiOutOpen(&device, 0, 0, 0, CALLBACK_NULL);
		if (flag != MMSYSERR_NOERROR) {
			printf("Error opening MIDI Output.\n");
			return 0;
		}
		//We're directly sending MIDI to the output!
		return 1; //Stop: ready!
	}
	#endif
	#ifdef MIDI_LOCKSTART
	for (result=0;result<MIDI_TOTALVOICES;result++) //Process all voices!
	{
		if (getLock(result + MIDI_LOCKSTART)) //Our MIDI lock!
		{
			activevoices[result].locknumber = result+MIDI_LOCKSTART; //Our locking number!
		}
		else
		{
			return 0; //We're disabled!
		}
	}
	#endif
	done_MIDIDEVICE(); //Start finished!
	lockaudio();
	memset(&activevoices,0,sizeof(activevoices)); //Clear all voice data!

	reset_MIDIDEVICE(); //Reset our MIDI device!

	int i,j;
	for (i=0;i<2;++i)
	{
		choruscents[i] = (MIDI_CHORUS_SINUS_CENTS*(float)i); //Cents used for this chorus!
	}

	MIDIDEVICE_generateSinusTable(); //Make sure we can generate sinuses required!
	calcAttenuationPrecalcs(); //Calculate attenuation!

	//Load the soundfont?
	soundfont = readSF(filename); //Read the soundfont, if available!
	if (!soundfont) //Unable to load?
	{
		if (filename[0]) //Valid filename?
		{
			dolog("MPU", "No soundfont found or could be loaded!");
		}
		result = 0; //Error!
	}
	else
	{
		result = 1; //OK!
		for (i=0;i<MIDI_TOTALVOICES;i++) //Assign all voices available!
		{
			activevoices[i].purpose = ((((__MIDI_NUMVOICES)-(i/MIDI_NOTEVOICES))-1) < MIDI_DRUMVOICES) ? 1 : 0; //Drum or melodic voice? Put the drum voices at the far end!
			activevoices[i].effect_backtrace_samplespeedup_modenv_pitchfactor = allocfifobuffer(((uint_32)((chorus_delay[CHORUSSIZE])*MAX_SAMPLERATE)+1)<<3,0); //Not locked FIFO buffer containing the entire history!
			activevoices[i].effect_backtrace_LFO1 = allocfifobuffer(((uint_32)((chorus_delay[CHORUSSIZE]) * MAX_SAMPLERATE) + 1) << 3, 0); //Not locked FIFO buffer containing the entire history!
			activevoices[i].effect_backtrace_LFO2 = allocfifobuffer(((uint_32)((chorus_delay[CHORUSSIZE]) * MAX_SAMPLERATE) + 1) << 2, 0); //Not locked FIFO buffer containing the entire history!
			activevoices[i].effect_backtrace_lowpassfilter_modenvfactor = allocfifobuffer(((uint_32)((chorus_delay[CHORUSSIZE]) * MAX_SAMPLERATE) + 1) << 2, 0); //Not locked FIFO buffer containing the entire history!
			#ifndef DISABLE_REVERB
			for (j=0;j<CHORUSSIZE;++j) //All chorus backtrace channels!
			{
				activevoices[i].effect_backtrace_chorus[j] = allocfifobuffer(((uint_32)((reverb_delay[CHORUSSIZE])*MAX_SAMPLERATE)+1)<<3,0); //Not locked FIFO buffer containing the entire history!				
			}
			#endif
			activevoices[i].allocated = addchannel(&MIDIDEVICE_renderer,&activevoices[i],"MIDI Voice",44100.0f,__MIDI_SAMPLES,1,SMPL16S,0); //Add the channel! Delay at 0.96ms for response speed! 44100/(1000000/960)=42.336 samples/response!
			setVolume(&MIDIDEVICE_renderer,&activevoices[i],MIDI_VOLUME); //We're at 40% volume!
		}
	}
	MIDIDEVICE_ActiveSenseInit(); //Initialise Active Sense!
	unlockaudio();
	return result;
}

byte directMIDISupported()
{
	#ifdef IS_WINDOWS
		return 1; //Supported!
	#endif
	return 0; //Default: Unsupported platform!
}
