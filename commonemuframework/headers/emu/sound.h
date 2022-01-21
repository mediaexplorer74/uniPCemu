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

#ifndef SOUND_H
#define SOUND_H

typedef byte (*SOUNDHANDLER)(void* buf, uint_32 length, byte stereo, void *userdata);    /* A pointer to a handler function */

typedef short sample_t, *sample_p; //One sample!

typedef struct {
        sample_t l, r; //Stereo sample!
} sample_stereo_t, *sample_stereo_p;

#define SMPL16 0
#define SMPL8 1
#define SMPL16S 2
#define SMPL8S 3
#define SMPLFLT 4
//Same as SMPL16, but linear from -32768 to 32767.
#define SMPL16U 5
//Same as SMPL8, but linear from -256 to 255.
#define SMPL8U 6

//Is the buffer filled or skipped (unused)?
#define SOUNDHANDLER_RESULT_NOTFILLED 0
#define SOUNDHANDLER_RESULT_FILLED 1

//Maximum samplerate in Hertz (200KHz)
#define MAX_SAMPLERATE 50000.0f

void initAudio(); //Initialises audio subsystem!
void doneAudio(); //Finishes audio subsystem!
void audiodevice_connected(uint_32 which, byte iscapture); //An audio device has been connected!
void audiodevice_disconnected(uint_32 which, byte iscapture); //An audio device ahas been disconnected!

void updateAudio(DOUBLE timepassed); //Renders pending audio to the SDL audio renderer!

byte addchannel(SOUNDHANDLER handler, void *extradata, char *name, float samplerate, uint_32 samples, byte stereo, byte method, byte highpassfilterenabled); //Adds and gives a 1 on added or 0 on error!
//is_hw: bit 1 set: do not pause, bit 2 set: do not resume playing.
void removechannel(SOUNDHANDLER handler, void *extradata, byte is_hw); //Removes a sound handler from mixing, use is_hw=0 always, except for init/done of sound.c!
void resetchannels(); //Stop all channels&reset!
byte setVolume(SOUNDHANDLER handler, void *extradata, float p_volume); //Channel&Volume(100.0f=100%)
byte setSampleRate(SOUNDHANDLER handler, void *extradata, float rate); //Set sample rate!

byte sound_isRecording(); //Are we recording?
void sound_startRecording(); //Start sound recording?
void sound_stopRecording(); //Stop sound recording!
char *get_soundrecording_filename(); //Filename for a screen capture!

//Audio locking!
void lockaudio();
void unlockaudio();

//Basic dB and factor convertions!
#define dB2factorf(dB, fMaxLevelDB) (powf(10, (((dB) - (fMaxLevelDB)) / 20)))
#define factor2dBf(factor, fMaxLevelDB) ((fMaxLevelDB) + (20 * logf(factor)))
#define dB2factor(dB, fMaxLevelDB) (pow(10, (((dB) - (fMaxLevelDB)) / 20)))
#define factor2dB(factor, fMaxLevelDB) ((fMaxLevelDB) + (20 * log(factor)))
//Convert a volume in the range of 0=0, 100=1 to decibel factor to use with volume multiplication of signals!
#define convertVolume(vol) (factor2dB(((vol)*0.01f+1.0f),0.0f)/factor2dB(1.0f+1.0f,0.0f))

//Get the current recorded sample at hardware rate. This is timed according to the core clock timing.
sbyte getRecordedSampleL8s();
byte getRecordedSampleL8u();
sword getRecordedSampleL16s();
word getRecordedSampleL16u();
sbyte getRecordedSampleR8s();
byte getRecordedSampleR8u();
sword getRecordedSampleR16s();
word getRecordedSampleR16u();
#endif