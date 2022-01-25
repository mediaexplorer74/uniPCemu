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

#ifndef MIDIDEVICE_H
#define MIDIDEVICE_H

#include "..\commonemuframework\headers\types.h" //"headers/types.h"
#include "headers/hardware/midi/adsr.h" //ADSR support!
#include "..\commonemuframework\headers\support\fifobuffer.h" //"headers/support/fifobuffer.h" //Effect backtrace support for chorus/reverb effects!
#include "..\commonemuframework\headers\support\filters.h" //"headers/support/filters.h" //Filter support!

//MIDI Drum channel number
#define MIDI_DRUMCHANNEL 9

//All MIDI voices that are available! Originally 64! Minimum of 24 according to General MIDI 1!
#define __MIDI_NUMVOICES 24
//Amount of drum voices to reserve!
#define MIDI_DRUMVOICES 8
//Amount of voices allocated to each note
#define MIDI_NOTEVOICES 2
//Total amount of voices
#define MIDI_TOTALVOICES 48
//How many samples to buffer at once! 42 according to MIDI specs! Set to 84 to work!
#define __MIDI_SAMPLES 42

//Chorus amount(4 chorus channels) and reverberations including origin(7 reverberations and 1 origin, for a total of 8 copies)
#define CHORUSSIZE 2
#define REVERBSIZE 2
#define CHORUSREVERBSIZE 4

typedef struct
{
byte command; //What command?
byte buffer[2]; //The parameter buffer!
void *next; //Next command, if any!
} MIDICOMMAND, *MIDIPTR;

typedef struct
{
	//First, infomation for looking us up!
	byte channel; //What channel!
	byte note; //What note!
	byte noteon_velocity; //What velocity/AD(SR)!
	float noteon_velocity_factor; //Note on velocity converted to a factor!
	byte noteoff_velocity; //What velocity/(ADS)R!
	byte pressure; //Pressure/volume/aftertouch!
} MIDIDEVICE_NOTE; //Current playing note to process information!

typedef struct
{
	MIDIDEVICE_NOTE notes[0x100]; //All possible MIDI note statuses!
	//Channel information!
	byte control; //Control/current instrument!
	byte program; //Program/instrument!
	byte pressure; //Channel pressure/volume!
	byte volumeLSB; //Continuous controller volume LSB(CC39)!
	byte volumeMSB; //Continuous controller volume MSB(CC7)!
	byte expression; //Continuous controller expression(CC11)!
	word panposition; //Continuous controller pan position!
	float lvolume; //Left volume for panning!
	float rvolume; //Right volume for panning!
	word bank; //The bank from a bank select message!
	word activebank; //What bank are we?
	sword pitch; //Current pitch (14-bit value)
	byte sustain; //Enable sustain? Don't process KEY OFF while set!
	byte mode; //Channel mode: 0=Omni off, Mono; 1=Omni off, Poly; 2=Omni on, Mono; 3=Omni on, Poly;
	//Bit 0=1:Poly/0:Mono; Bit1=1:Omni on/0:Omni off
	/* Omni: respond to all channels (ignore channel part); Poly: Use multiple voices; Mono: Use one voice at the time (end other voices on Note On) */
	sbyte respondstart; //Start channel to respond to! -1=Don't respond!
	sbyte respondend; //End channel to respond to, if not only this channel! -1=Just this channel!
	sbyte controlchannel; //What channel to respond to CC messages? -1=Don't respond in this way!
	sbyte globalcontrolchannel; //Global control channel to respond to CC messages? -1=Don't respond in this way!
	byte singlevoice; //Single voice only to be played?
	byte choruslevel; //Current chorus depth set!
	byte reverblevel; //Current reverb depth set!
	byte monophonicchannelcount; //Monophonic channel count(in mono mode only)!
	byte ContinuousControllers[0x80]; //All possible continuous controllers!
	byte RPNhi;
	byte RPNlo;
	byte NRPNhi;
	byte NRPNlo;
	byte RPNmode; //0=None, 1=RPN, 2=NRPN(normal)
	byte NRPNpendingmode; //Pending special input mode for NRPN(See Soundfont 2.04 documentation)?
	uint_32 NRPNnumber; //NRPN number in Soundfont 2.04 mode!
	uint_32 NRPNnumbercounter; //NRPN number counter!
	byte pitchbendsensitivitysemitones;
	byte pitchbendsensitivitycents;
	byte sostenuto; //Sostenuto is activated?
} MIDIDEVICE_CHANNEL;

typedef struct
{
	//Delay to not do anything yet!
	int_64 delay; //We're delaying for n samples left?
	//LFO generator itself
	float sinpos; //All current chorus sin positions, wrapping around the table limit!
	float sinposstep; //The step of one sample in chorussinpos, wrapping around 
	//Output levels
	int_32 topitch; //To pitch!
	int_32 tofiltercutoff; //To filter cutoff!
	int_32 tovolume; //To volume!
	//Current outputs of the levels!
	float outputpitch; //The output value of the pitch!
	float outputfiltercutoff; //The output value of the pitch!
	float outputvolume; //The output value of the pitch!
	struct
	{
		word thedelay;
		word frequency;
		word topitch;
		word tofiltercutoff;
		word tovolume;
	} sources;
} MIDIDEVICE_LFO;

typedef struct
{
	int_64 play_counter; //Current play position within the soundfont!
	int_64 monotonecounter[CHORUSSIZE]; //Monotonic counter for positive only for each chorus channel!
	float monotonecounter_diff[CHORUSSIZE]; //Diff counter for each chorus channel!
	uint_32 loopsize[2]; //The size of a loop!
	int_64 finallooppos[2]; //Final loop position!
	int_64 finallooppos_playcounter[2]; //Play counter at the final loop position we've calculated!
	//Patches to the sample offsets, calculated before generating sound!
	uint_32 startaddressoffset[2];
	uint_32 startloopaddressoffset[2];
	uint_32 endaddressoffset[2];
	uint_32 endloopaddressoffset[2];
	uint_32 finishnoteleft; //Time left since finish of note itself!

	//Stuff for voice stealing
	uint_64 starttime; //When have we started our voice?

	//Our assigned notes/channels for lookup!
	MIDIDEVICE_CHANNEL *channel; //The active channel!
	MIDIDEVICE_NOTE *note; //The active note!
	float initpanning, panningmod; //Precalculated speedup of the samples, to be processed into effective speedup when starting the rendering!
	int_32 effectivesamplespeedup; //The speedup of the samples, in cents!
	float lvolume, rvolume; //Left and right panning!
	float lowpassfilter_freq; //What frequency to filter? 0.0f=No filter!
	float lowpassfilter_modenvfactor; //How many cents to apply to the frequency of the low pass filter?

	float CurrentVolumeEnvelope; //Current volume envelope!
	float CurrentModulationEnvelope; //Current modulation envelope!

	int_32 modenv_pitchfactor; //How many cents to apply to the frequency of the sound?
	byte allocated; //Allocated sound channel?
	byte loadedinformation; //Information is loaded?
	sfPresetHeader currentpreset;
	sfInst currentinstrument;
	sfSample sample[2]; //The sample to be played back! Stereo!
	ADSR VolumeEnvelope; //The volume envelope!
	ADSR ModulationEnvelope; //The modulation envelope!

	byte currentloopflags[2]; //What loopflags are active? Stereo!
	byte request_off; //Are we to be turned off? Start the release phase when enabled!
	byte has_finallooppos[2]; //Do we have a final loop position? Stereo!

	byte purpose; //0=Normal voice, 1=Drum channel!
	word bank; //What bank are we playing from?
	byte instrument; //What instrument are we playing?
	byte locknumber; //What lock number do we have? Only valid when actually used(lock defined)!
	float effectiveAttenuation; //Effective attenuation generator with modulators!
	float initialAttenuationGen; //The generator initial value!
	sword rootMIDITone;

	//Chorus and reverb calculations!
	float chorusdepth[CHORUSSIZE]; //All chorus depths, index 0 is dry sound!
	float reverbdepth[REVERBSIZE]; //All reverb depths, index 0 is dry sound!
	float activechorusdepth[CHORUSSIZE]; //The chorus depth used for all channels!
	float activereverbdepth[REVERBSIZE]; //The reverb depth used for all channels!
	int_32 modulationratiocents[CHORUSSIZE];
	DOUBLE modulationratiosamples[CHORUSSIZE]; //Modulation ratio and it's samples rate for faster lookup on boundaries!
	float lowpass_modulationratio[CHORUSSIZE], lowpass_modulationratiosamples[CHORUSSIZE]; //See modulation ratio, but for the low pass filter only!
	FIFOBUFFER *effect_backtrace_samplespeedup_modenv_pitchfactor; //A backtrace of the sample speedup and pitch factor through time for each sample played in the main stream!
	FIFOBUFFER* effect_backtrace_LFO1; //A backtrace of the sample speedup and pitch factor through time for each sample played in the main stream!
	FIFOBUFFER* effect_backtrace_LFO2; //A backtrace of the sample speedup and pitch factor through time for each sample played in the main stream!
	FIFOBUFFER* effect_backtrace_lowpassfilter_modenvfactor; //low pass backtrace for reverb purpose, stereo!
	FIFOBUFFER* effect_backtrace_chorus[CHORUSSIZE]; //Chorus backtrace for reverb purpose, stereo!

	MIDIDEVICE_LFO LFO[2];

	uint_32 chorusdelay[CHORUSSIZE]; //Total delay for the chorus/reverb channel!
	uint_32 reverbdelay[REVERBSIZE]; //Total delay for the chorus/reverb channel!
	float chorusvol[CHORUSSIZE]; //Chorus/reverb volume!
	float reverbvol[REVERBSIZE]; //Reverb volume!
	float chorussinpos[CHORUSSIZE]; //All current chorus sin positions, wrapping around the table limit!
	float chorussinposstep; //The step of one sample in chorussinpos, wrapping around 
	byte isfinalchannel_chorus[CHORUSSIZE]; //Are we the final channel to process for the current sample?
	byte isfinalchannel_reverb[REVERBSIZE]; //Are we the final channel to process for the current sample?
	HIGHLOWPASSFILTER reverbfilter[CHORUSREVERBSIZE*2]; //Reverb filters, stereo!
	HIGHLOWPASSFILTER lowpassfilter[CHORUSSIZE][2]; //Each channel has it's own low-pass filter! Stereo!
	float last_lowpass[CHORUSSIZE]; //Last lowpass frequency used! Stereo!
	byte lowpass_dirty[CHORUSSIZE]; //Are we to update the low-pass filter? Stereo bits 0=left, bit2=right!
	byte effectivenote; //Effective note!
	byte effectivevelocity; //Effective velocity!
	//Pointers to used lists!
	uint_32 preset;
	word pbag;
	word instrumentptr;
	word ibag;
	byte active; //Are we still playing something?
	byte noteplaybackfinished; //Finallooptime valid to add?
	float last_initialattenuation; //Last initial attenuation!
	float last_volumeenvelope; //Last volume envelope!
	float last_attenuation; //Last attenuation!
	uint_32 exclusiveclass; //The exclusive class if non-zero!
} MIDIDEVICE_VOICE;

void MIDIDEVICE_tickActiveSense(); //Tick the Active Sense (MIDI) line with any command/data!
void MIDIDEVICE_addbuffer(byte command, MIDIPTR data); //Add a command to the buffer!
//MIDICOMMAND *MIDIDEVICE_peekbuffer(); //Peek at the buffer!
//int MIDIDEVICE_readbuffer(MIDICOMMAND *result); //Read from the buffer!

byte init_MIDIDEVICE(char *filename, byte use_direct_MIDI); //Initialise MIDI device for usage!
void done_MIDIDEVICE(); //Finish our midi device!

byte directMIDISupported(); //Direct MIDI supported on the compiled platform?

float getSFInstrumentmodulator(MIDIDEVICE_VOICE* voice, word destination, byte applySrcAmt, float min, float max);
float getSFPresetmodulator(MIDIDEVICE_VOICE* voice, word destination, byte applySrcAmt, float min, float max);

//val needs to be a normalized input! Performs a convex from 0 to 1!
float MIDIconvex(float val);

#endif
