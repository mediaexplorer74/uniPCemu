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
#include "headers/support/zalloc.h" //Zalloc support!
#include "headers/hardware/ports.h" //MPU support!
#include "headers/emu/threads.h" //Thread support!
#include "headers/support/mid.h" //Our own typedefs!
#include "headers/emu/gpu/gpu_text.h" //Text surface support!
#include "headers/hardware/midi/mididevice.h" //For the MIDI voices!
#include "headers/support/log.h" //Logging support!
#include "headers/emu/timers.h" //Tempo timer support!
#include "headers/support/locks.h" //Locking support!
#include "headers/cpu/cpu.h" //CPU support!
#include "headers/emu/emucore.h" //Emulator start/stop support!
#include "headers/fopen64.h" //64-bit fopen support!

//Enable this define to log all midi commands executed here!
//#define MID_LOG

#define MIDIHEADER_ID 0x6468544d
#define MIDIHEADER_TRACK_ID 0x6b72544d

#include "headers/packed.h"
typedef struct PACKED
{
	uint_32 Header; //MThd
	uint_32 header_length;
	word format; //0=Single track, 1=Multiple track, 2=Multiple song file format (multiple type 0 files)
	word n; //Number of tracks that follow us
	sword division; //Positive: units per beat, negative:  SMPTE-compatible units.
} HEADER_CHNK;
#include "headers/endpacked.h"

#include "headers/packed.h"
typedef struct PACKED
{
	uint_32 Header; //MTrk
	uint_32 length; //Number of bytes in the chunk.
} TRACK_CHNK;
#include "headers/endpacked.h"

byte MID_TERM = 0; //MIDI termination flag!

//The protective semaphore for the hardware!

uint_64 timing_pos = 0; //Current timing position!

uint_32 activetempo = 500000; //Current tempo!

word MID_RUNNING = 0; //How many channels are still running/current channel running!

byte MID_INIT = 0; //Initialization to execute?

word numMIDchannels = 0; //How many channels are loaded?

byte MID_last_command = 0x00; //Last executed command!

byte MID_last_channel_command[0x10000]; //Last command sent by this channel!
byte MID_newstream[0x10000]; //New stream status!

uint_64 MID_playpos[0x10000]; //Play position in the stream for every channel!
byte MID_playing[0x10000]; //Is this channel playing?


//A loaded MIDI file!
HEADER_CHNK MID_header;
byte *MID_data[100]; //Tempo and music track!
TRACK_CHNK MID_tracks[100];
OPTINLINE word byteswap16(word value)
{
	return ((value & 0xFF) << 8) | ((value & 0xFF00) >> 8); //Byteswap!
}

OPTINLINE uint_32 byteswap32(uint_32 value)
{
	return (byteswap16(value & 0xFFFF) << 8) | byteswap16((value & 0xFFFF0000) >> 16); //
}

OPTINLINE float calcfreq(uint_32 tempo, HEADER_CHNK *header)
{
	float speed;
	byte frames;
	byte subframes; //Pulses per quarter note!
	word division;
	division = byteswap16(header->division); //Byte swap!

	if (division & 0x8000) //SMTPE?
	{
		frames = (byte)((division >> 8) & 0x7F); //Frames!
		subframes = (byte)(division & 0xFF); //Subframes!
		speed = frames; //Default: we're the frames!
		if (subframes)
		{
			//We don't use the tempo: our rate is fixed!
			frames *= subframes; //The result in subframes/second!
		}
		speed = frames; //Use (sub)frames!
	}
	else
	{
		//tempo=us/quarter note
		speed = (float)tempo; //Length of a quarter note in us!
		//speede is now the amount of quarter notes per second!
		speed /= (float)division; //Divide us by the PPQN(Pulses Per Quarter Note) to get the ammount of us/pulse!
		speed = 1000000.0f / speed; //Convert us/pulse to pulse/second!
		//Speed is now the ammount of pulses per second!
	}

	//We're counting in ticks!
	return speed; //ticks per second!
}

DOUBLE timing_pos_step = 0.0; //Step of a timing position, in nanoseconds!
OPTINLINE void updateMIDTimer(HEADER_CHNK *header) //Request an update of our timer!
{
	if (calcfreq(activetempo, header)) //Valid frequency?
	{
		#ifdef IS_LONGDOUBLE
		timing_pos_step = 1000000000.0L/(DOUBLE)calcfreq(activetempo, header); //Set the counter timer!
		#else
		timing_pos_step = 1000000000.0/(DOUBLE)calcfreq(activetempo, header); //Set the counter timer!
		#endif
	}
	else
	{
		timing_pos_step = 0.0; //No step to use?
	}
}

extern MIDIDEVICE_VOICE activevoices[MIDI_TOTALVOICES]; //All active voices!
extern GPU_TEXTSURFACE *frameratesurface; //Our framerate surface!

OPTINLINE void printMIDIChannelStatus()
{
	int i,j /*,voice*/;
	uint_32 color; //The color to use!
	GPU_text_locksurface(frameratesurface); //Lock the surface!
	for (i = 0; i < MIDI_TOTALVOICES; i++) //Process all voices!
	{
		//voice = (i / MIDI_NOTEVOICES); //The main voice handler!
		GPU_textgotoxy(frameratesurface, 0, i + 5); //Row 5+!
		if (activevoices[i].VolumeEnvelope.active && activevoices[i].active) //Fully active voice?
		{
			color = RGB(0x00, 0xFF, 0x00); //The color to use!
			GPU_textprintf(frameratesurface, color, RGB(0xDD, 0xDD, 0xDD), "%02i", activevoices[i].active);
		}
		else //Inactive voice?
		{
			if (activevoices[i].play_counter) //We have been playing?
			{
				color = RGB(0xFF, 0xAA, 0x00);
				GPU_textprintf(frameratesurface, color, RGB(0xDD, 0xDD, 0xDD), "%02i", i);
			}
			else //Completely unused voice?
			{
				color = RGB(0xFF, 0x00, 0x00);
				GPU_textprintf(frameratesurface, color, RGB(0xDD, 0xDD, 0xDD), "%02i", i);
			}
		}
		if (activevoices[i].channel && activevoices[i].note) //Gotten assigned?
		{
			GPU_textprintf(frameratesurface, color, RGB(0xDD, 0xDD, 0xDD), " %04X %02X %02X", activevoices[i].channel->activebank, activevoices[i].channel->program, activevoices[i].note->note); //Dump information about the voice we're playing!
		}
		else //Not assigned?
		{
			GPU_textprintf(frameratesurface, color, RGB(0xDD, 0xDD, 0xDD), "           "); //Dump information about the voice we're playing!
		}
		if (activevoices[i].loadedinformation) //information loaded?
		{
			for (j = 0; j < NUMITEMS(activevoices[i].currentpreset.achPresetName); ++j) //preset name!
			{
				if (!activevoices[i].currentpreset.achPresetName[j]) //End of string?
				{
					break;
				}
				GPU_textprintf(frameratesurface, color, RGB(0xDD, 0xDD, 0xDD), "%c", activevoices[i].currentpreset.achPresetName[j]); //Dump information about the voice we're playing!
			}
			for (; j < NUMITEMS(activevoices[i].currentpreset.achPresetName); ++j) //preset name remainder!
			{
				GPU_textprintf(frameratesurface, color, RGB(0xDD, 0xDD, 0xDD), " "); //Clear first!
			}
		}
	}
	GPU_text_releasesurface(frameratesurface); //Unlock the surface!
}

void resetMID() //Reset our settings for playback of a new file!
{
	activetempo = 500000; //Default = 120BPM = 500000 microseconds/quarter note!

	timing_pos = 0; //Reset the timing for the current song!

	MID_TERM = 0; //Reset termination flag!

	updateMIDTimer(&MID_header); //Update the timer!
}

word readMID(char *filename, HEADER_CHNK *header, TRACK_CHNK *tracks, byte **channels, word maxchannels)
{
	BIGFILE *f;
	TRACK_CHNK currenttrack;
	word currenttrackn = 0; //Ammount of tracks loaded!
	uint_32 tracklength;

	byte *data;
	f = emufopen64(filename, "rb"); //Try to open!
	if (!f) return 0; //Error: file not found!
	if (emufread64(header, 1, sizeof(*header), f) != sizeof(*header))
	{
		emufclose64(f);
		return 0; //Error reading header!
	}
	if (header->Header != MIDIHEADER_ID)
	{
		emufclose64(f);
		return 0; //Nothing!
	}
	if (byteswap32(header->header_length) != 6)
	{
		emufclose64(f);
		return 0; //Nothing!
	}
	header->format = byteswap16(header->format); //Preswap!
	if (header->format>2) //Not single/multiple tracks played single or simultaneously?
	{
		emufclose64(f);
		return 0; //Not single/valid multi track!
	}
	nexttrack: //Read the next track!
	if (emufread64(&currenttrack, 1, sizeof(currenttrack), f) != sizeof(currenttrack)) //Error in track?
	{
		emufclose64(f);
		return 0; //Invalid track!
	}
	if (currenttrack.Header != MIDIHEADER_TRACK_ID) //Not a track ID?
	{
		emufclose64(f);
		return 0; //Invalid track header!
	}
	if (!currenttrack.length) //No length?
	{
		emufclose64(f);
		return 0; //Invalid track length!
	}
	tracklength = byteswap32(currenttrack.length); //Calculate the length of the track!
	data = zalloc(tracklength+sizeof(uint_32),"MIDI_DATA",NULL); //Allocate data and cursor!
	if (!data) //Ran out of memory?
	{
		emufclose64(f);
		return 0; //Ran out of memory!
	}
	if (emufread64(data+sizeof(uint_32), 1, tracklength, f) != tracklength) //Error reading data?
	{
		emufclose64(f);
		freez((void **)&data, tracklength+sizeof(uint_32), "MIDI_DATA");
		return 0; //Error reading data!
	}

	++currenttrackn; //Increase the number of tracks loaded!
	if (currenttrackn > maxchannels) //Limit broken?
	{
		freez((void **)&data, tracklength+sizeof(uint_32), "MIDI_DATA");
		return 0; //Limit broken: we can't store the file!
	}

	channels[currenttrackn - 1] = data; //Safe the pointer to the data!
	memcpy(tracks, &currenttrack, sizeof(currenttrack)); //Copy track information!
	++tracks; //Next track!
	if ((currenttrackn<byteswap16(header->n))) //Format 1? Take all tracks!
	{
		goto nexttrack; //Next track to check!
	}

	emufclose64(f);
	return currenttrackn; //Give the result: the ammount of tracks loaded!
}

void freeMID(TRACK_CHNK *tracks, byte **channels, word numchannels)
{
	uint_32 channelnr;
	for (channelnr = 0; channelnr < numchannels; channelnr++)
	{
		freez((void **)&channels[channelnr], byteswap32(tracks[channelnr].length)+sizeof(uint_32), "MIDI_DATA"); //Try to free!
	}
}

OPTINLINE byte consumeStream(byte *stream, TRACK_CHNK *track, byte *result)
{
	byte *streamdata = stream + sizeof(uint_32); //Start of the data!
	uint_32 *streampos = (uint_32 *)stream; //Position!
	if (!memprotect(streampos, 4, "MIDI_DATA")) return 0; //Error: Invalid stream!
	if (*streampos >= byteswap32(track->length)) return 0; //End of stream reached!
	if (!memprotect(&streamdata[*streampos], 1, "MIDI_DATA")) return 0; //Error: Invalid data!
	*result = streamdata[*streampos]; //Read the data!
	++(*streampos); //Increase pointer in the stream!
	return 1; //Consumed!
}

OPTINLINE byte peekStream(byte *stream, TRACK_CHNK *track, byte *result)
{
	byte *streamdata = stream + sizeof(uint_32); //Start of the data!
	uint_32 *streampos = (uint_32 *)stream; //Position!
	if (!memprotect(streampos, 4, "MIDI_DATA")) return 0; //Error: Invalid stream!
	if (*streampos >= byteswap32(track->length)) return 0; //End of stream reached!
	if (!memprotect(&streamdata[*streampos], 1, "MIDI_DATA")) return 0; //Error: Invalid data!
	*result = streamdata[*streampos]; //Read the data!
	return 1; //Consumed!
}

OPTINLINE byte read_VLV(byte *midi_stream, TRACK_CHNK *track, uint_32 *result)
{
	uint_32 temp = 0;
	byte curdata=0;
	if (!consumeStream(midi_stream, track, &curdata)) return 0; //Read first VLV failed?
	for (;;) //Process/read the VLV!
	{
		temp |= (curdata & 0x7F); //Add to length!
		if (!(curdata & 0x80)) break; //No byte to follow?
		temp <<= 7; //Make some room for the next byte!
		if (!consumeStream(midi_stream, track, &curdata)) return 0; //Read VLV failed?
	}
	*result = temp; //Give the result!
	return 1; //OK!
}

#define MIDI_ERROR(position) {error = position; goto abortMIDI;}

/*

updateMIDIStream: Updates a new or currently playing MIDI stream!
parameters:
	channel: What channel are we?
	midi_stream: The stream to play
	header: The used header for the played file
	track: The used track we're playing
	last_command: The last executed command by this function. Initialized to 0.
	newstream: New stream status. Initialized to 1.
	play_pos: The current playing position. Initialized to 0.
result:
	1: Playing
	0: Aborted/finished

*/

OPTINLINE byte updateMIDIStream(word channel, byte *midi_stream, HEADER_CHNK *header, TRACK_CHNK *track, byte *last_channel_command, byte *newstream, uint_64 *play_pos) //Single-thread version of updating a MIDI stream!
{
	byte curdata;

	//Metadata event!
	byte meta_type;
	uint_32 length=0, length_counter; //Our metadata variable length!

	uint_32 delta_time; //Delta time!

	int_32 error = 0; //Default: no error!
	for (;;) //Playing?
	{
		if (MID_TERM) //Termination requested?
		{
			//Unlock
			return 0; //Stop playback: we're requested to stop playing!
		}

		rechecktime: //Recheck the time to be!
		if (timing_pos>=*play_pos) //We've arrived?
		{
			if (*newstream) //Nothing done yet or initializing?
			{
				if (!read_VLV(midi_stream, track, &delta_time)) return 0; //Read VLV time index!
				*play_pos += delta_time; //Add the delta time to the playing position!
				*newstream = 0; //We're not a new stream anymore!
				goto rechecktime; //Recheck the time to be!
			}
		}
		else //We're still waiting?
		{
			return 1; //Playing, but aborted due to waiting!
		}

		if (!peekStream(midi_stream,track, &curdata))
		{
			return 0; //Failed to peek!
		}
		if (curdata == 0xFF) //System?
		{
			if (!consumeStream(midi_stream, track, &curdata)) return 0; //EOS!
			if (!consumeStream(midi_stream, track, &meta_type)) return 0; //Meta type failed? Give error!
			if (!read_VLV(midi_stream, track, &length)) return 0; //Error: unexpected EOS!
			switch (meta_type) //What event?
			{
				case 0x2F: //EOT?
					#ifdef MID_LOG
					dolog("MID", "channel %u: EOT!", channel); //EOT reached!
					#endif
					return 0; //End of track reached: done!
				case 0x51: //Set tempo?
					if (!consumeStream(midi_stream, track, &curdata)) return 0; //Tempo 1/3 failed?
					activetempo = curdata; //Final byte!
					activetempo <<= 8;
					if (!consumeStream(midi_stream, track, &curdata)) return 0; //Tempo 2/3 failed?
					activetempo |= curdata; //Final byte!
					activetempo <<= 8;
					if (!consumeStream(midi_stream, track, &curdata)) return 0; //Tempo 3/3 failed?
					activetempo |= curdata; //Final byte!
					//Tempo = us per quarter note!

					updateMIDTimer(header);

					#ifdef MID_LOG
					dolog("MID", "channel %u: Set Tempo:%06X!", channel, activetempo);
					#endif
					break;
				default: //Unrecognised meta event? Skip it!
					#ifdef MID_LOG
					dolog("MID", "Unrecognised meta type: %02X@Channel %u; Data length: %u", meta_type, channel, length); //Log the unrecognised metadata type!
					#endif
					for (; length--;) //Process length bytes!
					{
						if (!consumeStream(midi_stream, track, &curdata)) return 0; //Skip failed?
					}
					break;
			}
		}
		else //Hardware?
		{
			if (curdata & 0x80) //Starting a new command?
			{
				#ifdef MID_LOG
				dolog("MID", "Status@Channel %u=%02X", channel, curdata);
				#endif
				if (!consumeStream(midi_stream, track, &curdata)) MIDI_ERROR(1) //EOS!
				*last_channel_command = curdata; //Save the last command!
				if (*last_channel_command != 0xF7) //Escaped continue isn't sent!
				{
					PORT_OUT_B(0x330, *last_channel_command); //Send the command!
				}
			}
			else
			{
				#ifdef MID_LOG
				dolog("MID", "Continued status@Channel %u: %02X=>%02X",channel, last_channel_command, curdata);
				#endif
				if (*last_channel_command != 0xF7) //Escaped continue isn't used last?
				{
					PORT_OUT_B(0x330, *last_channel_command); //Repeat the status bytes: we don't know what the other channels do!
				}
			}

			MID_last_command = *last_channel_command; //We're starting this command, so record it being executed!

			//Process the data for the command!
			switch ((*last_channel_command >> 4) & 0xF) //What command to send data for?
			{
			case 0xF: //Special?
				switch (*last_channel_command & 0xF) //What subcommand are we sending?
				{
				case 0x0: //System exclusive?
				case 0x7: //Escaped continue?
					if (!read_VLV(midi_stream, track, &length)) MIDI_ERROR(2) //Error: unexpected EOS!
					length_counter = 3; //Initialise our position!
					for (; length--;) //Transmit the packet!
					{
						if (!consumeStream(midi_stream, track, &curdata)) MIDI_ERROR(length_counter++) //EOS!
						PORT_OUT_B(0x330, curdata); //Send the byte!
					}
					break;
				case 0x1:
				case 0x3:
					//1 byte follows!
					if (!consumeStream(midi_stream, track, &curdata)) MIDI_ERROR(2) //EOS!
					PORT_OUT_B(0x330, curdata); //Passthrough to MIDI!
					break;
				case 0x2:
					//2 bytes follow!
					if (!consumeStream(midi_stream, track, &curdata)) MIDI_ERROR(2) //EOS!
					PORT_OUT_B(0x330, curdata); //Passthrough to MIDI!
					if (!consumeStream(midi_stream, track, &curdata)) MIDI_ERROR(3) //EOS!
					PORT_OUT_B(0x330, curdata); //Passthrough to MIDI!
					break;
				default: //Unknown special instruction?
					break; //Single byte instruction?
				}
				break;
			case 0x8: //Note off?
			case 0x9: //Note on?
			case 0xA: //Aftertouch?
			case 0xB: //Control change?
			case 0xE: //Pitch bend?
				//2 bytes follow!
				if (!consumeStream(midi_stream, track, &curdata)) MIDI_ERROR(2) //EOS!
				PORT_OUT_B(0x330, curdata); //Passthrough to MIDI!
				if (!consumeStream(midi_stream, track, &curdata)) MIDI_ERROR(3) //EOS!
				PORT_OUT_B(0x330, curdata); //Passthrough to MIDI!
				break;
			case 0xC: //Program change?
			case 0xD: //Channel pressure/aftertouch?
				//1 byte follows
				if (!consumeStream(midi_stream, track, &curdata)) MIDI_ERROR(2) //EOS!
				PORT_OUT_B(0x330, curdata); //Passthrough to MIDI!
				break;
			default: //Unknown data? We're sending directly to the hardware! We shouldn't be here!
				if (!consumeStream(midi_stream, track, &curdata)) MIDI_ERROR(2) //EOS!
				dolog("MID", "Warning: Unknown data detected@channel %u: passthrough to MIDI device: %02X!", channel, curdata);
				//Can't process: ignore the data, since it's invalid!
				break;
			}
		abortMIDI:
			//Unlock
			if (error)
			{
				PORT_OUT_B(0x330, 0xFF); //Reset the synthesizer!
				dolog("MID", "channel %u: Error @position %u during MID processing! Unexpected EOS? Last command: %02X, Current data: %02X", channel, error, *last_channel_command, curdata);
				return 0; //Abort on error!
			}
			//Finish: prepare for next command!
		}

		//Read the next delta time, if any!
		if (!read_VLV(midi_stream, track, &delta_time)) return 0; //Read VLV time index!
		*play_pos += delta_time; //Add the delta time to the playing position!
	}
	return 0; //Stream finished by default!
}

extern byte coreshutdown; //Core is shutting down?

void finishMIDplayer()
{
	lock(LOCK_INPUT);
	if (MID_RUNNING) //Are we playing anything?
	{
		MID_TERM = 2; //Finish up!
		MID_RUNNING = 0; //Not running anymore!
	}
	unlock(LOCK_INPUT);
}

DOUBLE MID_timing = 0.0; //Timing of a playing MID file!
void updateMIDIPlayer(DOUBLE timepassed)
{
	uint_32 channel; //The current channel!
	//Update any channels still playing!
	if (MID_RUNNING) //Are we playing anything?
	{
		MID_timing += timepassed; //Add the time we're to play!
		if (MID_INIT) //Special start-up at 0 seconds?
		{
			MID_INIT = 0; //We're finished with initialization after this!
		}
		else
		{
			if ((MID_timing>=timing_pos_step) && timing_pos_step) //Enough to step?
			{
				timing_pos += (uint_64)(MID_timing/timing_pos_step); //Step as many as we can!
				MID_timing = fmod(MID_timing,timing_pos_step); //Rest the timing!
			}
		}

		MID_TERM |= coreshutdown; //Shutting down terminates us!

		//Now, update all playing streams!
		for (channel=0;channel<numMIDchannels;channel++) //Process all channels that need processing!
		{
			if (MID_playing[channel]) //Are we a running channel?
			{
				switch (updateMIDIStream(channel, MID_data[channel], &MID_header, &MID_tracks[channel],&MID_last_channel_command[channel],&MID_newstream[channel],&MID_playpos[channel])) //Play the MIDI stream!
				{
					default: //Unknown status?
					case 0: //Terminated?
						MID_playing[channel] = 0; //We're not running anymore!
						--MID_RUNNING; //Done!
						break;
					case 1: //Playing?
						if (MID_header.format == 2) //Multiple tracks to be played after one another(Series of type-0 files)?
						{
							return; //Only a single channel can be playing at any time!
						}
						break;
				}
			}
		}

		lock(LOCK_INPUT);
		if (psp_inputkey()&(BUTTON_CANCEL|BUTTON_STOP)) //Circle/stop pressed? Request to stop playback!
		{
			MID_TERM = 2; //Pending termination by pressing the stop button!
		}
		unlock(LOCK_INPUT);
	}
}

extern byte EMU_RUNNING; //Current running status!

byte playMIDIFile(char *filename, byte showinfo) //Play a MIDI file, CIRCLE to stop playback!
{
	byte EMU_RUNNING_BACKUP=0;
	lock(LOCK_CPU);
	EMU_RUNNING_BACKUP = EMU_RUNNING; //Make a backup to restore after we've finished!
	unlock(LOCK_CPU);

	memset(&MID_data, 0, sizeof(MID_data)); //Init data!
	memset(&MID_tracks, 0, sizeof(MID_tracks)); //Init tracks!

	numMIDchannels = readMID(filename, &MID_header, &MID_tracks[0], &MID_data[0], 100); //Load the number of channels!
	if (numMIDchannels)
	{
		stopTimers(0); //Stop most timers for max compatiblity and speed!
		//Initialise our device!
		PORT_OUT_B(0x331, 0xFF); //Reset!
		PORT_OUT_B(0x331, 0x3F); //Kick to UART mode!

		//Now, start up all timers!
		resetMID(); //Reset all our settings!

		lock(LOCK_CPU); //Lock the executing thread!
		MID_timing = 0.0; //Reset our main timing clock!
		word i;
		MID_RUNNING = numMIDchannels; //Init to all running!
		for (i = 0; i < numMIDchannels; i++)
		{
			MID_last_channel_command[i] = 0x00; //Reset last channel command!
			MID_newstream[i] = 1; //We're a new stream!
			MID_playpos[i] = 0; //We're starting to play at the start!
			MID_playing[i] = 1; //Default: we're playing!
		}
		MID_INIT = 1; //We're starting the initialization cycle!
		unlock(LOCK_CPU); //Finished!

		startTimers(1);
		startTimers(0); //Start our timers!

		byte running; //Are we running?
		running = 1; //We start running!

		resumeEMU(0); //Resume the emulator!
		lock(LOCK_CPU); //Lock the CPU: we're checking for finishing!
		CPU[activeCPU].halt |= 0x12; //Force us into HLT state, starting playback!
		BIOSMenuResumeEMU(); //Resume the emulator from the BIOS menu thread!
		EMU_stopInput(); //We don't want anything to be input into the emulator!

		for (;;) //Wait to end!
		{
			unlock(LOCK_CPU); //Unlock us!
			delay(1000000); //Wait little intervals to update status display!
			lock(LOCK_CPU); //Lock us!
			if (MID_RUNNING==0) //Stopped playing or shutting down?
			{
				running = 0; //Not running anymore!
			}
			if (showinfo) printMIDIChannelStatus(); //Print the MIDI channel status!

			lock(LOCK_INPUT);
			if ((psp_keypressed(BUTTON_CANCEL) || psp_keypressed(BUTTON_STOP)) || (MID_TERM==2)) //Circle/stop pressed? Request to stop playback!
			{
				unlock(LOCK_INPUT);
				unlock(LOCK_CPU); //Unlock us!
				lock(LOCK_INPUT);
				for (; (psp_keypressed(BUTTON_CANCEL) || psp_keypressed(BUTTON_STOP));)
				{
					unlock(LOCK_INPUT);
					delay(0); //Wait for release while pressed!
					lock(LOCK_INPUT);
				}
				unlock(LOCK_INPUT);
				lock(LOCK_CPU); //Lock us!
				MID_TERM = 1; //Set termination flag to request a termination!
			}
			else unlock(LOCK_INPUT);

			if (!running) break; //Not running anymore? Start quitting!
		}

		CPU[activeCPU].halt &= ~0x12; //Remove the forced execution!
		unlock(LOCK_CPU); //We're finished with the CPU!
		pauseEMU(); //Stop timers and back to the BIOS menu!
		lock(LOCK_CPU);
		EMU_RUNNING = EMU_RUNNING_BACKUP; //We're not running atm, restore the backup!

		freeMID(&MID_tracks[0], &MID_data[0], numMIDchannels); //Free all channels!

		if (shuttingdown()) //Shutdown requested?
		{
			unlock(LOCK_CPU); //We're finished with the CPU!
			return MID_TERM?0:1; //Played without termination?
		}
		//Clean up the MIDI device for any leftover sound!
		byte channel;
		for (channel = 0; channel < 0x10; channel++) //Process all channels!
		{
			PORT_OUT_B(0x330, 0xB0 | (channel & 0xF)); //We're requesting a ...
			PORT_OUT_B(0x330, 0x7B); //All Notes off!
			PORT_OUT_B(0x330, 0x00); //On the current channel!!
		}
		PORT_OUT_B(0x330, 0xFF); //Reset MIDI device!
		PORT_OUT_B(0x331, 0xFF); //Reset the MPU!
		unlock(LOCK_CPU); //We're finished with the CPU!
		return MID_TERM?0:1; //Played without termination?
	}
	return 0; //Invalid file?
}
