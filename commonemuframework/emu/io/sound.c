
//Our audio includes!
#include "headers/types.h" //Basic types!
#include "headers/emu/sound.h" //Sound comp.
#include "headers/support/zalloc.h" //Zero allocation!
#include "headers/support/log.h" //Logging support!
#include "headers/support/highrestimer.h" //High resolution clock for timing checks.
#include "headers/support/signedness.h" //Signedness support!
#include "headers/support/wave.h" //WAV file log support!
#include "headers/support/sounddoublebuffer.h" //Double buffered sound support!
#include "headers/support/locks.h" //Locking support!
#include "headers/support/filters.h" //Filter support!

//Hardware set sample rate
#define HW_SAMPLERATE 44100

//Our global volume setting to use!
#define SOUND_VOLUME 100.0f

//Our global low-pass filter before resampling volume!
#ifdef IS_LONGDOUBLE
#define SOUND_FILTER_VOLUME 1.0L
#else
#define SOUND_FILTER_VOLUME 1.0f
#endif

//Are we disabled?
#define __HW_DISABLED 0
//How many samples to process at once? Originally 2048; 64=Optimum
#ifdef IS_PSP
//PSP?
#define SAMPLESIZE 64
#else
#ifdef ANDROID
//We use lower latency for Android devices, since it seems it needs that.
#define SAMPLESIZE 2048
#else
//Windows/Linux?
#define SAMPLESIZE 512
#endif
#endif
//Enable below if debugging speed is to be enabled.
//#define DEBUG_SOUNDSPEED
//Enable below if debugging buffering is to be enabled.
//#define DEBUG_SOUNDBUFFER
//Same as speed, but for allocations themselves.
//#define DEBUG_SOUNDALLOC
//Enable below to use direct audio output(SDL_QueueAudio) on SDL2 instead of an audio callback!
//#define SDL_ENABLEQUEUEAUDIO
//Use external timing synchronization?
//#define EXTERNAL_TIMING
//Use the equalizer functionality?
//#define __USE_EQUALIZER
//Define RECORD_TESTWAVE to make it record a test sine wave instead.
//#define RECORD_TESTWAVE 1.0

//What frequency to filter our sound for (higher than 0Hz!) Currently the high pass filter disturbs sound too much, so it's disabled. Low pass is set to half the rendering frequency!
//#define SOUND_HIGHPASS 18.2f
#define SOUND_CHANNELHIGHPASS 18.2f

#ifndef SDL2//#ifdef SDL2
#ifdef SDL_ENABLEQUEUEAUDIO
//QueueAudio is supported, use it!
#define SDL_QUEUEAUDIO
#endif
#endif

#if defined(SDL_QUEUEAUDIO) || SDL_VERSION_ATLEAST(2,0,4)
#define PAUSEAUDIO(x) SDL_PauseAudioDevice(audiodevice,x)
#else
#define PAUSEAUDIO(x) SDL_PauseAudio(x)
#endif

typedef struct
{
	void *samples; //All samples!
	int_32 *filteredsamples;
	HIGHLOWPASSFILTER samplefilter[2]; //Filter to be applied to get the filtered samples.
	HIGHLOWPASSFILTER highpasssamplefilter[2]; //Filter to be applied to get the filtered samples.
	uint_32 length;		/* size of sound data in bytes */
	uint_32 filteredlength;		/* size of sound data in bytes */
	uint_32 position;		/* the position in the sound data in bytes */
	uint_32 numsamples; //Ammount of samples in samples.
} sound_t, *sound_p;

/* Structure for a currently playing sound. */
typedef struct
{
	//All our sound data
	sound_t sound;              /* sound data to play from the channel, loaded by the sound handler */
	SOUNDHANDLER soundhandler; //For filling the buffer!

	//Currently executing position
	word position;            /* current position in the sound buffer */
	
	//Rest channel data
	float volume; //The volume!
	float volume_percent; //The volume, in percent (1/volume).
	float samplerate; //The sample rate!
	float convert_samplerate; //Conversion from hardware samplerate to used samplerate!
	uint_32 bufferinc; //The increase in the buffer (how much position decreases during a buffering)
	byte stereo; //Mono stream or stereo stream? 1=Stereo stream, else mono.
	void *extradata; //Extra data for the handler!
	char name[256]; //A short name!
	byte samplemethod; //The method used to decode samples!
	byte highpassfilter; //High pass filter the signal?
	byte bufferflags; //Special flags about the buffer from the soundhandler function!
	//Fill and processbuffer to use!
	void *fillbuffer; //Fillbuffer function to use!
	void *processbuffer; //Channelbuffer function to use (determined by fillbuffer function)!
} playing_t, *playing_p;

#if defined(SDL_QUEUEAUDIO) || SDL_VERSION_ATLEAST(2,0,4)
SDL_AudioDeviceID audiodevice; //Our used audio device!
#endif

#ifndef SDL2//#ifdef SDL2
SDL_AudioDeviceID recorddevice; //Our used audio device!
#endif

byte audioticksready = 0; //Default: not ready yet!
TicksHolder audioticks, recordticks;

//Our calls for data buffering and processing.
typedef uint_32 (*fillbuffer_call)(playing_p currentchannel, uint_32 *relsample, uint_32 currentpos);
typedef void (*processbuffer_call)(playing_p currentchannel, int_32 *result_l, int_32 *result_r, uint_32 relsample);

uint_32 fillbuffer_new(playing_p currentchannel, uint_32 *relsample, uint_32 currentpos); //New fillbuffer call (for new channels)!

uint_32 soundchannels_used = 0; //Ammount of used sound channels to check, increases as the ammount of entries increase!
playing_t soundchannels[1000]; //All soundchannels!

SDL_AudioSpec audiospecs; //Our requested and obtained audio specifications.
#ifndef SDL2//#ifdef SDL2
SDL_AudioSpec recordspecs; //Our requested and obtained audio specifications.
#endif

//The sample rate to render at:
#define SW_SAMPLERATE (float)audiospecs.freq
#define SW_RECORDRATE (float)recordspecs.freq

//Sample position precalcs!
uint_32 *samplepos[2]; //Sample positions for mono and stereo channels!
uint_32 samplepos_size; //Size of the sample position precalcs (both of them)

SOUNDDOUBLEBUFFER mixeroutput; //The output of the mixer, to be read by the renderer!
SOUNDDOUBLEBUFFER mixerinput; //The input of the mixer, to be read by the recorder!

sword inputleft, inputright; //Left&right input for recording!
uint_32 currentrecordedsample; //The current recorded sample, as used!

//Default samplerate is HW_SAMPLERATE, Real samplerate is SW_SAMPLERATE

word audiolocklvl = 0; //Audio lock level!

void lockaudio()
{
	if (__HW_DISABLED) return; //Nothing to lock!
	if (audiolocklvl==0) //Root level?
	{
		lock(LOCK_SOUND); //Lock the audio!
	}
	++audiolocklvl; //Increase the lock level!
}

void unlockaudio()
{
	if (__HW_DISABLED) return; //Nothing to lock!
	--audiolocklvl; //Decrease the lock level!
	if (audiolocklvl==0) //Root level?
	{
		//We're unlocking!
		unlock(LOCK_SOUND); //Unlock the audio!
	}
}

#define C_CALCSAMPLEPOS(rchannel,stereo,time) ((time*(1<<stereo)) + (stereo*rchannel))
OPTINLINE void calc_samplePos() //Calculate sample position precalcs!
{
	if (samplepos[0] && samplepos[1]) return; //Don't reallocate!
	uint_32 precalcs_size = ((uint_32)MAX_SAMPLERATE*sizeof(*samplepos[0])<<1); //Size of samplepos precalcs!
	//Now allocate the precalcs!
	samplepos[0] = (uint_32 *)zalloc(precalcs_size,"SamplePosPrecalcs",NULL);
	samplepos[1] = (uint_32 *)zalloc(precalcs_size,"SamplePosPrecalcs",NULL);
	byte abort;
	abort = 0; //Default: no abort!
	if (!samplepos[1])
	{
		abort = 1; //1 abort!
	}
	if (!samplepos[0])
	{
		abort |= 2; //2 abort!
	}
	if (abort) //Aborted?
	{
		if (abort&1)
		{
			freez((void **)&samplepos[1],precalcs_size,"SamplePosPrecalcs");
		}
		if (abort&2)
		{
			freez((void **)&samplepos[0],precalcs_size,"SamplePosPrecalcs");
		}
		//Aborted: ran out of memory!
		return; //Abort!
	}
	
	//Now calculate the samplepos precalcs!
	
	uint_32 time = 0;
	uint_32 time_limit = (uint_32)MAX_SAMPLERATE; //The limit!
	for (;;)
	{
		samplepos[0][(time<<1)|0] = C_CALCSAMPLEPOS(0,0,time); //Precalculate left channel indexes!
		samplepos[0][(time<<1)|1] = C_CALCSAMPLEPOS(1,0,time); //Precalculate right channel indexes!
		samplepos[1][(time<<1)|0] = C_CALCSAMPLEPOS(0,1,time); //Precalculate left channel indexes!
		samplepos[1][(time<<1)|1] = C_CALCSAMPLEPOS(1,1,time); //Precalculate right channel indexes!
		if (++time>=time_limit) break; //Next time position!
	}
	
	samplepos_size = precalcs_size; //The size of the samplepos precalcs!
}

OPTINLINE void free_samplePos()
{
	byte stereo;
	for (stereo=0;stereo<2;stereo++) //All sample positions!
	{
		if (samplepos[stereo]) //Loaded?
		{
			freez((void **)&samplepos[stereo],samplepos_size,"SamplePosPrecalcs");
		}
	}
	if (!samplepos[0] && !samplepos[1]) //Both freed?
	{
		samplepos_size = 0; //No size anymore: we're freed!
	}
}

byte setStereo(SOUNDHANDLER handler, void *extradata, byte stereo) //Channel&Volume(100.0f=100%)
{
	if (__HW_DISABLED) return 0; //Disabled?
	uint_32 n;
	for (n=0;(n<soundchannels_used);n++) //Check all!
	{
		if ((soundchannels[n].soundhandler==handler) && (soundchannels[n].extradata==extradata)) //Found?
		{
			lockaudio();
			soundchannels[n].stereo = stereo; //Are we a stereo channel?
			unlockaudio(); //Unlock the audio!
			return 1; //Done: check no more!
		}
	}
	return 0; //Not found!
}

byte setSampleRate(SOUNDHANDLER handler, void *extradata, float rate)
{
	if (__HW_DISABLED) return 0; //Disabled?
	uint_32 n;
	lockaudio(); //Lock the audio!
	for (n=0;n<soundchannels_used;n++) //Check all!
	{
		if (soundchannels[n].soundhandler && (soundchannels[n].soundhandler==handler) && (soundchannels[n].extradata==extradata)) //Found?
		{
			if (rate>(float)MAX_SAMPLERATE) //Too much?
			{
				dolog("soundservice","Maximum samplerate passed: %f",rate); //Maximum samplerate passed!
				unlockaudio();
				return 0; //Invalid samplerate!
			}
			
			//Determine the samplerate used first!
			float samplerate = rate;
			if (samplerate==0.0f) //Rate not specified?
			{
				 samplerate = SW_SAMPLERATE; //Default: hardware samplerate!
			}
			
			soundchannels[n].samplerate = samplerate; //Save the actual samplerate used!
			soundchannels[n].convert_samplerate = (1/SW_SAMPLERATE)*samplerate; //The factor for each SW samplerate in destination samplerates!
			soundchannels[n].bufferinc = (uint_32)((float)soundchannels[n].sound.numsamples/(float)soundchannels[n].convert_samplerate); //How much the buffer position decreases during a buffering, in samples!
			
			#ifdef DEBUG_SOUNDALLOC
			dolog("soundservice","Convert samplerate: x%10.5f=SW:%10.5f,Channel:%10.5f; REQ:%10.5f",soundchannels[n].convert_samplerate,SW_SAMPLERATE,samplerate,rate);
			#endif
			
			unlockaudio(); //Unlock the audio!
			return 1; //OK: we're ready to run!
		}
	}
	unlockaudio(); //Unlock the audio!
	return 0; //Not found!
}

byte setVolume(SOUNDHANDLER handler, void *extradata, float p_volume) //Channel&Volume(100.0f=100%)
{
	if (__HW_DISABLED) return 0; //Disabled?
	uint_32 n;
	lockaudio(); //Lock the audio!
	for (n=0;n<soundchannels_used;n++) //Check all!
	{
		if (soundchannels[n].soundhandler && (soundchannels[n].soundhandler==handler) && (soundchannels[n].extradata==extradata)) //Found?
		{
			soundchannels[n].volume = p_volume; //Set the volume of the channel!
			soundchannels[n].volume_percent = (float)(p_volume?convertVolume(p_volume)*convertVolume(SOUND_VOLUME):0.0f); //The volume in linear percent, with 0dB=silence!
			unlockaudio(); //Unlock the audio!
			return 1; //Done: check no more!
		}
	}
	unlockaudio(); //Unlock the audio!
	return 0; //Not found!
}

//add&removal of channels!

OPTINLINE uint_32 samplesize(uint_32 samples, byte method)
{
	switch (method)
	{
		case SMPL16: //16 bit unsigned?
		case SMPL16S: //16 bit signed?
		case SMPL16U: //16 bit unsigned linear?
			return (((uint_32)samples)<<1)*(uint_32)sizeof(word);
			break;
		case SMPL8: //8 bit unsigned?
		case SMPL8S: //8 bit signed?
		case SMPL8U: //8 bit unsigned linear?
			return (((uint_32)samples)<<1)*(uint_32)sizeof(byte);
			break;
		case SMPLFLT: //Floating point numbers?
			return (((uint_32)samples)<<1)*(uint_32)sizeof(float);
			break;
		default:
			break;
	}
	return 0; //No size available: invalid size!
}

byte addchannel(SOUNDHANDLER handler, void *extradata, char *name, float samplerate, uint_32 samples, byte stereo, byte method, byte highpassfilterenabled) //Adds and gives a 1 on added or 0 on error!
{
	if (__HW_DISABLED) return 0; //Disabled?
	if (!handler) return 0; //Invalid handler!
	if (method>6) //Invalid method?
	{
		return 0; //Nothing: unsupported method!
	}

	#ifdef DEBUG_SOUNDALLOC
	dolog("soundservice","Request: Adding channel at %fHz, buffer every %u samples, Stereo: %u",samplerate,samples,stereo);
	#endif

	if (!samplerate) //Autodetect?
	{
		samplerate = SW_SAMPLERATE; //Automatic samplerate!
	}
	if (!samples) //Autodetect?
	{
		samples = (uint_32)((float)samplerate*((float)(SAMPLESIZE)/(float)SW_SAMPLERATE)); //Calculate samples based on samplesize samples out of hardware samplerate!
	}

	//Check for existant update!
	if (setSampleRate(handler,extradata,samplerate)) //Set?
	{
		if (setStereo(handler,extradata,stereo)) //Set?
		{
			#ifdef DEBUG_SOUNDALLOC
			dolog("soundservice","Channel changed and ready to run: handler: %p, extra data: %p, samplerate: %u, stereo: %u",handler,extradata,samplerate,stereo);
			dolog("soundservice",""); //Empty row!
			#endif
			return 1; //Already added and updated!
		}
	}

	uint_32 n; //For free allocation finding!
	lockaudio(); //Lock the audio!
	for (n=0;n<NUMITEMS(soundchannels);n++) //Try to find an available one!
	{
		if (!soundchannels[n].soundhandler) //Unused entry?
		{
			#ifdef DEBUG_SOUNDALLOC
			dolog("soundservice","Adding channel %s at %f samples/s, buffer every %u samples, Stereo: %u",name,samplerate,samples,stereo);
			#endif
			soundchannels[n].soundhandler = handler; //Set handler!
			soundchannels[n].fillbuffer = &fillbuffer_new; //Our fillbuffer call to start with!
			soundchannels[n].extradata = extradata; //Extra data to be sent!
			soundchannels[n].sound.numsamples = samples; //Ammount of samples to buffer at a time!
			memset(&soundchannels[n].name,0,sizeof(soundchannels[n].name)); //Init name!
			safestrcpy(soundchannels[n].name,sizeof(soundchannels[0].name),name); //Set a name to use for easy viewing/debugging!
			
			if (n>=soundchannels_used) //Past the ammount of channels used?
			{
				soundchannels_used = n;
				++soundchannels_used; //Update the ammount of sound channels used!
			}
			
			//Init volume, sample rate and stereo!
			setVolume(handler,extradata,100.0f); //Default volume to 100%
			if (!setSampleRate(handler,extradata,samplerate)) //The sample rate to use!
			{
				removechannel(handler,extradata,0);
				unlockaudio(); //Unlock audio and start playing!
				return 0; //Abort!
			}
			if (!setStereo(handler,extradata,stereo)) //Stereo output?
			{
				removechannel(handler,extradata,0);
				unlockaudio(); //Unlock audio and start playing!
				return 0; //Abort!
			}

			soundchannels[n].samplemethod = method; //The sampling method to use!

			//Finally, the samples themselves!
			soundchannels[n].sound.position = 0; //Make us refresh immediately!
			
			soundchannels[n].sound.length = samplesize(soundchannels[n].sound.numsamples,method); //Ammount of samples in the buffer, stereo quality (even if mono used)!
			soundchannels[n].sound.samples = zalloc(soundchannels[n].sound.length,"SW_Samples",NULL);
			soundchannels[n].sound.filteredlength = (soundchannels[n].sound.numsamples<<1)*(uint_32)sizeof(int_32); //Ammount of samples in the buffer, stereo quality (even if mono used)!
			soundchannels[n].sound.filteredsamples = zalloc(soundchannels[n].sound.filteredlength,"SW_Samples",NULL);
			initSoundFilter(&soundchannels[n].sound.samplefilter[0],0,(float)(SW_SAMPLERATE/2.0),samplerate); //Left filter!
			initSoundFilter(&soundchannels[n].sound.samplefilter[1],0,(float)(SW_SAMPLERATE/2.0),samplerate); //Right filter!
			initSoundFilter(&soundchannels[n].sound.highpasssamplefilter[0], 1, SOUND_CHANNELHIGHPASS, samplerate); //Left filter!
			initSoundFilter(&soundchannels[n].sound.highpasssamplefilter[1], 1, SOUND_CHANNELHIGHPASS, samplerate); //Right filter!
			soundchannels[n].highpassfilter = highpassfilterenabled; //High-pass filter enabled on this channel?

			#ifdef DEBUG_SOUNDALLOC
			dolog("soundservice","Channel allocated and ready to run: handler: %p, extra data: %p, samplerate: %f, sample buffer size: %u, stereo: %u",soundchannels[n].soundhandler,soundchannels[n].extradata,soundchannels[n].samplerate,soundchannels[n].sound.numsamples,soundchannels[n].stereo);
			dolog("soundservice",""); //Empty row!
			#endif
			unlockaudio(); //Unlock audio and start playing!
			return 1; //Add a channel and give the pointer to the current one!
		}
	}
	
	#ifdef DEBUG_SOUNDALLOC
	dolog("soundservice","Ran out of free channels!");
	#endif
	
	unlockaudio(); //Unlock audio and start playing!
	return 0; //No channel available!
}

//is_hw: bit 1 set: do not pause, bit 2 set: do not resume playing.
void removechannel(SOUNDHANDLER handler, void *extradata, byte is_hw) //Removes a sound handler from mixing, use is_hw=0 always, except for init/done of sound.c!
{
	if (__HW_DISABLED) return; //Disabled?
	if (!handler) return; //Can't remove no handler!
	uint_32 n; //For free allocation finding!
	lockaudio(); //Lock the audio!
	for (n=0;n<soundchannels_used;n++) //Try to find an available one!
	{
		if (soundchannels[n].soundhandler && (soundchannels[n].soundhandler==handler) && (soundchannels[n].extradata==extradata)) //Found?
		{
			#ifdef DEBUG_SOUNDALLOC
			dolog("soundservice","Releasing channel %s...",soundchannels[n].name);
			dolog("soundservice",""); //Empty row!
			#endif
			if (soundchannels[n].sound.samples && soundchannels[n].sound.length) //Samples allocated?
			{
				freez((void **)&soundchannels[n].sound.samples,soundchannels[n].sound.length,"SW_Samples"); //Free samples!
				if (soundchannels[n].sound.filteredsamples)
				{
					freez((void **)&soundchannels[n].sound.filteredsamples,soundchannels[n].sound.filteredlength,"SW_Samples"); //Free samples!
				}
				if (!soundchannels[n].sound.samples) //Freed?
				{
					soundchannels[n].sound.length = 0; //No length anymore!
				}
				if (!soundchannels[n].sound.filteredsamples) //Freed?
				{
					soundchannels[n].sound.filteredlength = 0; //No length anymore!
				}
			}
			
			//Next remove our handler and the channel itself!
			soundchannels[n].soundhandler = NULL; //Stop the handler from availability!
			soundchannels[n].extradata = NULL; //No extra data anymore!

			#ifdef DEBUG_SOUNDALLOC
			dolog("soundservice","Channel %p:%p:%s released and ready to continue.",handler,extradata,soundchannels[n].name);
			dolog("soundservice",""); //Empty row!
			#endif
			
			if (n==(soundchannels_used-1)) //Final sound channel?
			{
				n = soundchannels_used;
				--n; //Final channel in use?
				while (1)
				{
					if (n==0 && !soundchannels[n].soundhandler) //Nothing found?
					{
						soundchannels_used = 0; //Nothing used!
						break; //Stop searching!
					}
					if (soundchannels[n].soundhandler) //Found a channel in use?
					{
						soundchannels_used = n;
						++soundchannels_used; //Found this channel!
						break; //Stop searching!
					}
					--n; //Decrease to the next channel!
				}
				//Now soundchannels_used contains the ammount of actually used sound channels!
			}
			memset(&soundchannels[n].name,0,sizeof(soundchannels[n].name)); //Clear the name!
			
			unlockaudio(); //Unlock the audio and start playing again?
			return; //Done!
		}
	}
	unlockaudio(); //Unlock the audio and start playing again?
}

void resetchannels()
{
	if (__HW_DISABLED) return; //Disabled?
	uint_32 n; //For free allocation finding!
	for (n=0;n<soundchannels_used;n++) //Try to find an available one!
	{
		if (soundchannels[n].soundhandler) //Allocated?
		{
			removechannel(soundchannels[n].soundhandler,soundchannels[n].extradata,3); //Remove the channel and stop all playback!
		}
	}
}

//OUR MIXING!


typedef int_32 (*SAMPLEHANDLER)(playing_p channel, uint_32 position); //Sample handler!

OPTINLINE int_32 getsample_filtered(playing_p channel, uint_32 position) //Get 16-bit sample from sample buffer, convert as needed!
{
	return channel->sound.filteredsamples[position]; //Execute handler if available!
}

//Simple macros for checking samples!
//Precalcs handling!
#define C_SAMPLEPOS(channel) (channel->sound.position)
#define C_BUFFERSIZE(channel) (channel->sound.numsamples)
#define C_BUFFERINC(channel) (channel->bufferinc)
//Use precalculated sample positions!
#define C_SAMPLERATE(channel,position) (uint_32)(channel->convert_samplerate*((float)(position)))
#define C_STEREO(channel) (channel->stereo)
#define C_GETSAMPLEPOS(channel,rchannel,time) (samplepos[C_STEREO(channel)][(((word)time)<<1)|(rchannel)])
#define C_VOLUMEPERCENT(channel) (channel->volume_percent)
#define C_SAMPLE(channel,samplepos) getsample_filtered(channel,samplepos)

//Processing functions prototypes!
void emptychannelbuffer(playing_p currentchannel, int_32 *result_l, int_32 *result_r, uint_32 relsample); //Empty buffer channel handler!
void filledchannelbuffer(playing_p currentchannel, int_32 *result_l, int_32 *result_r, uint_32 relsample); //Full buffer channel handler!

//Sample retrieval
int_32 getsample_16(playing_p channel, uint_32 position)
{
	word *y = (word *)channel->sound.samples;
	return (int_32)unsigned2signed16(y[position]);
}

int_32 getsample_16s(playing_p channel, uint_32 position)
{
	sword *ys = (sword *)channel->sound.samples;
	return (int_32)ys[position];
}

int_32 getsample_16u(playing_p channel, uint_32 position)
{
	word *y = (word *)channel->sound.samples;
	return (int_32)unsigned2signed16(y[position]^0x8000);
}

int_32 getsample_8(playing_p channel, uint_32 position)
{
	byte *x = (byte *)channel->sound.samples;	
	word result=(word)x[position]; //Load the result!
	result <<= 8; //Multiply into range!
	result |= (result&0xFF00)?0xFF:0x00; //Bit fill
	return (int_32)unsigned2signed16(result);
}

int_32 getsample_8s(playing_p channel, uint_32 position)
{
	byte *xs = (byte *)channel->sound.samples;
	word result = (word)xs[position]; //Load the result!
	result <<= 8; //Multiply into range!
	result |= (result & 0xFF00) ? 0xFF : 0x00; //Bit fill!
	return (int_32)unsigned2signed16(result); //Give the data!
}

int_32 getsample_8u(playing_p channel, uint_32 position)
{
	byte *x = (byte *)channel->sound.samples;
	word result = (word)x[position]; //Load the result!
	result ^= 0x80; //Flip the sign bit!
	result <<= 8; //Multiply into range!
	result |= (result & 0xFF00) ? 0xFF : 0x00; //Bit fill!
	return (int_32)unsigned2signed16(result); //Give the data!
}

DOUBLE FLTMULT = (1.0f/FLT_MAX)*SHRT_MAX;

int_32 getsample_flt(playing_p channel, uint_32 position)
{
	float *z = (float *)channel->sound.samples;
	return (int_32)(((DOUBLE)z[position]*FLTMULT)); //Convert to integer value!	
}

SAMPLEHANDLER samplehandlers[7] = {getsample_16,getsample_8,getsample_16s,getsample_8s,getsample_flt,getsample_16u,getsample_8u};
OPTINLINE int_32 getsample_raw(playing_p channel, uint_32 position) //Get 16-bit sample from sample buffer, convert as needed!
{
	return samplehandlers[channel->samplemethod](channel,position); //Execute handler if available!
}

#ifdef SOUND_FILTER_VOLUME
float sound_filter_volume = (float)(SOUND_FILTER_VOLUME); //Low pass filter volume!
#endif

float soundbuffer_maxval = -1.0f;
float soundbuffer_minval = 1.0f;

OPTINLINE void processbufferflags(playing_p currentchannel)
{
	uint_32 samplepos;
	uint_32 numsamples;
	float minval,maxval;
	maxval = soundbuffer_maxval; //Fast load!
	minval = soundbuffer_minval; //Fast load!
	float sample;
	byte stereobit;
	stereobit = (C_STEREO(currentchannel)&1); //Stereo bit/toggle!
	currentchannel->processbuffer = (currentchannel->bufferflags&1)?&filledchannelbuffer:&emptychannelbuffer; //Either the filled or empty channel buffer to use!
	numsamples = (C_BUFFERSIZE(currentchannel)<<C_STEREO(currentchannel)); //How many to process?
	if (unlikely(currentchannel->bufferflags & 1)) //Got anything to process at all? Don't do anything with the samples if not needed!
	{
		for (samplepos = 0; samplepos < numsamples; ++samplepos) //Parse output!
		{
			sample = (float)(getsample_raw(currentchannel, samplepos)); //Store raw samples for now, unfiltered!
#ifdef SOUND_FILTER_VOLUME
			sample *= sound_filter_volume; //Protect against overflows!
			applySoundLowPassFilter(&currentchannel->sound.samplefilter[samplepos & stereobit], &sample); //Apply the left/right filter!
			if (currentchannel->highpassfilter) //High-pass filter enabled?
			{
				applySoundHighPassFilter(&currentchannel->sound.highpasssamplefilter[samplepos & stereobit], &sample); //Apply the left/right filter!
			}
#endif
			sample = LIMITRANGE(sample, minval, maxval); //Limit to valid range!
			currentchannel->sound.filteredsamples[samplepos] = (int_32)sample; //Save the filtered sample!
		}
	}
	//If not filled, it isn't used, so don't handle anything!
}

uint_32 fillbuffer_existing(playing_p currentchannel, uint_32 *relsample, uint_32 currentpos)
{
	uint_32 bufferinc; //Increase in the buffer!
	#ifdef DEBUG_SOUNDBUFFER
	byte buffering = 0; //We're buffered?
	#endif
	bufferinc = C_BUFFERINC(currentchannel); //Load buffer increase rate!
	*relsample = C_SAMPLERATE(currentchannel,currentpos); //Get the sample position of the destination samplerate!
	rebuffer: //Rebuffer check!
	if (*relsample>=C_BUFFERSIZE(currentchannel)) //Expired or empty, we've reached the end of the buffer (sample overflow)?
	{
		#ifdef DEBUG_SOUNDBUFFER
		buffering = 1; //We're buffering!
		dolog("soundservice","Buffering @ %u/%u samples; extra data: %p; name: %s",*relsample,C_BUFFERSIZE(currentchannel),currentchannel->extradata,currentchannel->name);
		#endif
		//Buffer and update buffer position!
		currentchannel->bufferflags = currentchannel->soundhandler(currentchannel->sound.samples,C_BUFFERSIZE(currentchannel),C_STEREO(currentchannel),currentchannel->extradata); // Request next sample for this channel, also give our channel extra information!
		if (likely((currentchannel->bufferflags & 1) != 0)) //Used channel?
		{
			processbufferflags(currentchannel); //Process the buffer flags!
			currentpos -= bufferinc; //Reset position in the next frame!
			*relsample = C_SAMPLERATE(currentchannel, currentpos); //Get the sample rate for the new buffer!
		}
		else //Became unused?
		{
			processbufferflags(currentchannel); //Process the final buffer flags!
			currentpos = 0; //Init!
			*relsample = 0; //Init!
			currentchannel->fillbuffer = &fillbuffer_new; //We're a new buffer when checking again, as we're resetting now!
			return 0; //Unused channel now!
		}
		goto rebuffer; //Rebuffer if needed!
	} //Don't buffer!
	#ifdef DEBUG_SOUNDBUFFER
	if (buffering) //We were buffering?
	{
		buffering = 0;
		dolog("soundservice","Buffer ready. Mixing...");
	}
	#endif
	return currentpos; //Give the new position!
}

uint_32 fillbuffer_new(playing_p currentchannel, uint_32 *relsample, uint_32 currentpos)
{
	*relsample = 0; //Reset relative sample!
#ifdef DEBUG_SOUNDBUFFER
	dolog("soundservice", "Initialising sound buffer...");
	dolog("soundservice", "Buffering @ 0/%u samples; extra data: %p; name: %s", C_BUFFERSIZE(currentchannel), currentchannel->extradata, currentchannel->name);
#endif
	//Buffer and update buffer position!
	currentchannel->bufferflags = currentchannel->soundhandler(currentchannel->sound.samples,C_BUFFERSIZE(currentchannel),C_STEREO(currentchannel),currentchannel->extradata); // Request next sample for this channel, also give our channel extra information!
	if (currentchannel->bufferflags&1) //Filled?
	{
		//Make sure that the filters are reset for receiving the new channels!
		initSoundFilter(&currentchannel->sound.samplefilter[0], 0, (float)(SW_SAMPLERATE / 2.0), currentchannel->samplerate); //Left filter!
		initSoundFilter(&currentchannel->sound.samplefilter[1], 0, (float)(SW_SAMPLERATE / 2.0), currentchannel->samplerate); //Right filter!
		initSoundFilter(&currentchannel->sound.highpasssamplefilter[0], 1, SOUND_CHANNELHIGHPASS, currentchannel->samplerate); //Left filter!
		initSoundFilter(&currentchannel->sound.highpasssamplefilter[1], 1, SOUND_CHANNELHIGHPASS, currentchannel->samplerate); //Right filter!
		currentchannel->fillbuffer = &fillbuffer_existing; //We're initialised, so call existing buffers from now on!
	}

	processbufferflags(currentchannel); //Process the buffer flags!
	#ifdef DEBUG_SOUNDBUFFER
	dolog("soundservice","Buffer ready. Mixing...");
	#endif
	return 0; //We start at the beginning!
}

void filledchannelbuffer(playing_p currentchannel, int_32 *result_l, int_32 *result_r, uint_32 relsample)
{
	float volume = C_VOLUMEPERCENT(currentchannel); //Retrieve the current volume!
	int_32 sample_l = C_SAMPLE(currentchannel,C_GETSAMPLEPOS(currentchannel,0,relsample)); //The composed sample, based on the relative position!
	int_32 sample_r = C_SAMPLE(currentchannel,C_GETSAMPLEPOS(currentchannel,1,relsample)); //The composed sample, based on the relative position!
	
	//Apply the channel volume!
	sample_l = (int_32)(sample_l*volume);
	sample_r = (int_32)(sample_r*volume);
	
	//Now we have the correct left and right channel data on our native samplerate.
	
	//Next, add the data to the mixer!
	*result_l += sample_l; //Mix the channels equally together based on volume!
	*result_r += sample_r; //See above!
}

void emptychannelbuffer(playing_p currentchannel, int_32 *result_l, int_32 *result_r, uint_32 relsample)
{
	//Do nothing!
}

OPTINLINE void mixchannel(playing_p currentchannel, int_32 *result_l, int_32 *result_r) //Mixes the channels with each other at a specific time!
{
	//Process multichannel!
	uint_32 relsample; //Current channel and relative sample!
	//Channel specific data
	INLINEREGISTER uint_32 currentpos; //Current sample pos!
	
	//First, initialise our variables!
	
	//First step: buffering if needed and keep our buffer!
	currentpos = ((fillbuffer_call)currentchannel->fillbuffer)(currentchannel,&relsample,C_SAMPLEPOS(currentchannel)); //Load the current position!

	((processbuffer_call)currentchannel->processbuffer)(currentchannel,result_l,result_r,relsample);

	//Finish up: update the values to be updated!
	++currentpos; //Next position on each channel!
	C_SAMPLEPOS(currentchannel) = currentpos; //Store the current position for next usage!
}

#ifdef SOUND_HIGHPASS
HIGHLOWPASSFILTER soundhighpassfilter[2], soundrecordfilter[2];
#endif

//Combined filters!
OPTINLINE void applySoundFilters(sword *leftsample, sword *rightsample)
{
	float sample_l, sample_r;

	//Load the samples to process!
	sample_l = (float)*leftsample; //Load the left sample to process!
	sample_r = (float)*rightsample; //Load the right sample to process!

	//Use the high pass to filter anything too low frequency!
	#ifdef SOUND_HIGHPASS
	applySoundFilter(&soundhighpassfilter[0],&sample_l);
	applySoundFilter(&soundhighpassfilter[1],&sample_r);
	#endif

	//Write back the samples we've processed!
	*leftsample = (sword)sample_l;
	*rightsample = (sword)sample_r;
}

OPTINLINE void applyRecordFilters(sword *leftsample, sword *rightsample)
{
	float sample_l, sample_r;

	//Our information for filtering!
	sample_l = (float)*leftsample; //Load the left sample to process!
	sample_r = (float)*rightsample; //Load the right sample to process!

	//Use the high pass to filter anything too low frequency!
#ifdef SOUND_HIGHPASS
	applySoundFilter(&soundrecordfilter[0],&sample_l);
	applySoundFilter(&soundrecordfilter[1],&sample_r);
#endif

	//Write back the samples we've processed!
	*leftsample = (sword)sample_l;
	*rightsample = (sword)sample_r;
}

word getRecordedSampleL16() //Give signed 16-bit!
{
	return signed2unsigned16(inputleft);
}

sword getRecordedSampleL16s() //Give signed 16-bit!
{
	return inputleft; //Left channel only!
}

word getRecordedSampleL16u() //Give unsigned 16-bit!
{
	return (signed2unsigned16(inputleft)^0x8000); //Left channel only!
}

byte getRecordedSampleL8()
{
	return (signed2unsigned8((sbyte)(inputleft>>8))); //Left channel only!
}

sbyte getRecordedSampleL8s()
{
	return (sbyte)(inputleft>>8); //Left channel only!
}

byte getRecordedSampleL8u()
{
	return (byte)((signed2unsigned16(inputleft)>>8)^0x80); //Left channel only!
}

//Right channel support!
word getRecordedSampleR16() //Give signed 16-bit!
{
	return signed2unsigned16(inputright);
}

sword getRecordedSampleR16s() //Give signed 16-bit!
{
	return inputright; //Left channel only!
}

word getRecordedSampleR16u() //Give unsigned 16-bit!
{
	return (signed2unsigned16(inputright)^0x8000); //Left channel only!
}

byte getRecordedSampleR8()
{
	return (signed2unsigned8((sbyte)(inputright>>8))); //Left channel only!
}

sbyte getRecordedSampleR8s()
{
	return (sbyte)(inputright>>8); //Left channel only!
}

byte getRecordedSampleR8u()
{
	return (byte)((signed2unsigned16(inputright)>>8)^0x80); //Left channel only!
}

int_32 mixedsamples[SAMPLESIZE*2]; //All mixed samples buffer!

WAVEFILE *recording = NULL; //We are recording when set.

byte mixerready = 0; //Are we ready to give output to the buffer?
byte inputready = 0; //Are we ready to give output to the buffer?

extern byte haswindowactive; //Window status information about minimized, iconified and Android background(bit 2)
extern byte backgroundpolicy; //Background task policy. 0=Full halt of the application, 1=Keep running without video and audio muted, 2=Keep running with audio playback, recording muted, 3=Keep running fully without video.

OPTINLINE void mixaudio(uint_32 length) //Mix audio channels to buffer!
{
	//Variables first
	//Current data numbers
	uint_32 currentsample, channelsleft; //The ammount of channels to mix!
#ifdef SDL_QUEUEAUDIO
	sword outputbuffer[2]; //16-bit stereo output buffer, used in direct rendering(SDL2)!
#endif
	INLINEREGISTER int_32 result_l, result_r; //Sample buffer!
	sword temp_l, temp_r; //Filtered values!
	//Active data
	playing_p activechannel; //Current channel!
	int_32 *firstactivesample;
	int_32 *activesample;
	
	//Stuff for Master gain
#ifndef IS_PSP
#ifdef __USE_EQUALIZER
	DOUBLE RMS_l = 0, RMS_r = 0, gainMaster_l, gainMaster_r; //Everything needed to apply the Master gain (equalizing using Mean value)
#endif
#endif
	
	channelsleft = soundchannels_used; //Load the channels to process!
	if (!length) return; //Abort without length!
	memset(&mixedsamples,0,sizeof(mixedsamples)); //Init mixed samples, stereo!
	if (length>SAMPLESIZE) length = SAMPLESIZE; //Limit us to what we CAN render!
	if (channelsleft)
	{
		activechannel = &soundchannels[0]; //Lookup the first channel!
		for (;;) //Mix the next channel!
		{
			if (activechannel->soundhandler) //Active?
			{
				if (activechannel->samplerate && activechannel->sound.samples && activechannel->sound.filteredsamples /*&&
					memprotect(activechannel->sound.samples,activechannel->sound.length,"SW_Samples")*/) //Allocated all neccesary channel data?
				{
					currentsample = length; //The ammount of sample to still buffer!
					activesample = &mixedsamples[0]; //Init active sample to the first sample!
					if (!(activechannel->bufferflags & 1)) //Empty channel buffer?
					{
						activechannel->fillbuffer = &fillbuffer_new; //We're not yet initialised, so call check for initialisation from now on!
					}
					for (;;) //Process all samples!
					{
						firstactivesample = activesample++; //First channel sample!
						mixchannel(activechannel,firstactivesample,activesample++); //L&R channel!
						if (activechannel->fillbuffer==&fillbuffer_new) break; //Stop procesing the channel if there's nothing left to process!
						if (!--currentsample) break; //Next sample when still not done!
					}
				}
			}
			if (!--channelsleft) break; //Stop when no channels left!
			++activechannel; //Next channel!
		}
	} //Got channels?


#ifndef IS_PSP
#ifdef __USE_EQUALIZER
	//Equalize all sound volume!

	//Process all generated samples to output!
	currentsample = length; //Init samples to give!
	activesample = &mixedsamples[0]; //Initialise the mixed samples position!

	//Second step: Apply equalizer using automatic Master gain.
	for (;;)
	{
		RMS_l += (*activesample) * (*activesample);
		++activesample; //Next channel!
		RMS_r += (*activesample) * (*activesample);
		++activesample; //Next sample!
		if (!--currentsample) break;
	}
	
	RMS_l /= length;
	RMS_r /= length;
	RMS_l = sqrt(RMS_l);
	RMS_r = sqrt(RMS_r);
	
	gainMaster_l = SHRT_MAX / (sqrt(2)*RMS_l);
	gainMaster_r = SHRT_MAX / (sqrt(2)*RMS_r);
#endif
#endif
	
	//Final step: apply Master gain and clip to output!
	currentsample = length; //Init samples to give!
	activesample = &mixedsamples[0]; //Initialise the mixed samples position!
	for (;;)
	{
		result_l = *activesample++; //L channel!
		result_r = *activesample++; //R channel!
#ifndef IS_PSP
#ifdef __USE_EQUALIZER
		result_l *= gainMaster_l; //Apply master gain!
		result_r *= gainMaster_r; //Apply master gain!
#endif
#endif
		if (result_l>SHRT_MAX) result_l = SHRT_MAX;
		if (result_l<SHRT_MIN) result_l = SHRT_MIN;
		if (result_r>SHRT_MAX) result_r = SHRT_MAX;
		if (result_r<SHRT_MIN) result_r = SHRT_MIN;

		//Apply our filters!
		temp_l = result_l;
		temp_r = result_r; //Load the temp values!
		applySoundFilters(&temp_l,&temp_r); //Apply our sound filters!
		result_l = temp_l; //Write back!
		result_r = temp_r; //Write back!

		//Apply recording of sound!
		if (recording) writeWAVStereoSample(recording,result_l,result_r); //Write the recording to the file if needed!

		if (((haswindowactive&4)==0) && (backgroundpolicy<2)) //Not to sound audio?
		{
			result_l = result_r = 0; //Mute audio!
		}

		//Give the output!
		if (mixerready)
		{
#ifdef SDL_QUEUEAUDIO
			outputbuffer[0] = (sword)result_l; //Left sample!
			outputbuffer[1] = (sword)result_r; //Right sample!
			SDL_QueueAudio(audiodevice,&outputbuffer,sizeof(outputbuffer)); //Render the stereo sample!
#else
			writeDoubleBufferedSound32(&mixeroutput,(signed2unsigned16((sword)result_r)<<16)|signed2unsigned16((sword)result_l)); //Give the stereo output to the mixer!
#endif
		}
		if (!--currentsample) return; //Finished!
	}
}

OPTINLINE void recordaudio(uint_32 length) //Record audio channels to state!
{
	//Variables first
	//Current data numbers
	uint_32 currentsample, inputsample; //How many is left to record!
	INLINEREGISTER int_32 result_l, result_r; //Sample buffer!
	sword temp_l, temp_r; //Filtered values!
	uint_32 limit;
#ifndef SDL2//#ifdef SDL2
	limit = ((uint_32)SW_RECORDRATE) << 1; //How many samples to tender max?
	if (length > limit) //More than we can handle?
		length = limit; //Limit samples!
	else
		limit = 0; //Not limited!
#else
	//Can't record on SDL?
	return; //Don't record at all!
#endif
	//Final step: apply Master gain and clip to output!
	currentsample = length; //Init samples to give!
	for (;;)
	{
		//Give the input!
		if (inputready)
		{
			readDoubleBufferedSound32(&mixerinput,&currentrecordedsample); //Give the stereo output to the mixer, if there's any!
		}

		inputsample = currentrecordedsample; //Load the currently recorded sample!
		result_l = unsigned2signed16((word)inputsample); //Left sample!
		inputsample >>= 16; //Shift low!
		result_r = unsigned2signed16((word)inputsample); //Right sample!

		if (unlikely(((haswindowactive & 4) == 0) && (backgroundpolicy < 3))) //Not allowed to record right now?
		{
			result_l = result_r = 0; //Mute audio recording when not allowed!
		}

		//Apply our filters!
		temp_l = result_l;
		temp_r = result_r; //Load the temp values!
		applyRecordFilters(&temp_l, &temp_r); //Apply our sound filters!
		result_l = temp_l; //Write back!
		result_r = temp_r; //Write back!

		inputleft = result_l; //Left recorded sample!
		inputright = result_r; //Right recorded sample!

		if (!--currentsample)
		{
			if (limit) //Limited input?
			{
				fifobuffer_clear(mixerinput.inputbuffer); //Discard the remaining input!
			}
			return; //Finished!
		}
	}
}

OPTINLINE void HW_mixaudio(sample_stereo_p buffer, uint_32 length) //Mix audio channels to buffer!
{
	//Variables first
	INLINEREGISTER sword result_l, result_r; //Sample buffer!
	INLINEREGISTER uint_32 currentsample; //The current sample number!

	static uint_32 mixersample;

	//Stuff for Master gain

	if (length == 0) return; //Abort without length!

	//Final step: apply Master gain and clip to output!
	currentsample = length; //Init samples to give!
	for (;;)
	{
		if (readDoubleBufferedSound32(&mixeroutput, &mixersample) == 0) //Read a sample, if present! Else duplicate the previous sample!
		{
			mixersample = 0; //Clear output when no sample is available!
		}
		result_l = unsigned2signed16(mixersample); //Left output!
		mixersample >>= 16; //Shift low!
		result_r = unsigned2signed16(mixersample); //Right output!
		//Give the output!
		buffer->l = (sample_t)result_l; //Left channel!
		buffer->r = (sample_t)result_r; //Right channel!
		if (!--currentsample) return; //Finished!
		++buffer; //Next sample in the result!
	}
}

OPTINLINE void HW_recordaudio(sample_stereo_p buffer, uint_32 length) //Mix audio channels to buffer!
{
	//Variables first
	INLINEREGISTER uint_32 currentsample; //The current sample number!

	static uint_32 mixersample;
	#ifdef RECORD_TESTWAVE
	static float recordtime=0.0f;
	#endif

	//Stuff for Master gain

	if (length == 0) return; //Abort without length!

	//Final step: apply Master gain and clip to output!
	currentsample = length; //Init samples to give!
	for (;;)
	{
        #ifndef SDL2//#ifdef SDL2
		#ifdef RECORD_TESTWAVE
		buffer->r = buffer->l = (sword)(sinf(2.0f*PI*RECORD_TESTWAVE*recordtime)*(SHRT_MAX/2.0)); //Use a test wave instead!
		recordtime += (1.0f/SW_RECORDRATE); //Tick time!
		recordtime = fmodf(recordtime,1.0f); //Wrap around a second!
		#endif
		#endif
		mixersample = signed2unsigned16(buffer->r); //Right output!
		mixersample <<= 16; //Shift high!
		mixersample |= signed2unsigned16(buffer->l); //Left output!
		//Give the output!

		writeDoubleBufferedSound32(&mixerinput, mixersample); //Write a sample, if present!
		if (!--currentsample) return; //Finished!
		++buffer; //Next sample in the result!
	}
}

//Audio callbacks!

DOUBLE sound_soundtiming = 0.0, sound_soundtick = 0.0;
DOUBLE sound_recordtiming = 0.0, sound_recordtick = 0.0;
void updateAudio(DOUBLE timepassed)
{
	uint_32 samples; //How many samples to render?
	byte ticksound, tickrecording, locked; //To tick us?
	//Sound output
	sound_soundtiming += timepassed; //Get the amount of time passed!
	sound_recordtiming += timepassed; //Time recording at the same rate as the playback, in emulated speed!
	ticksound = ((sound_soundtiming >= sound_soundtick) && sound_soundtick); //To tick sound?
	tickrecording = ((sound_recordtiming >= sound_recordtick) && sound_recordtick); //To tick recording?
	if (ticksound || tickrecording) //Anything to lock?
	{
		lockaudio(); //Make sure we're the only ones rendering!
		locked = 1; //We're locked!
	}
	else
	{
		locked = 0; //We're not locked!
	}

	if (ticksound) //To tick sound?
	{
		samples = (uint_32)(sound_soundtiming/sound_soundtick); //How many samples to render?
		sound_soundtiming -= (DOUBLE)samples*sound_soundtick; //Tick as many samples as we're rendering!
		mixaudio(samples); //Mix the samples required!
	}

	if (tickrecording) //Anything to record?
	{
		samples = (uint_32)(sound_recordtiming / sound_recordtick); //How many samples to render?
		sound_recordtiming -= (DOUBLE)samples*sound_recordtick; //Tick as many samples as we're rendering!
		recordaudio(samples); //Mix the samples required!
	}

	if (locked) //Anything locked?
	{
		unlockaudio(); //We're finished rendering!
	}
}

//Recording support!
byte sound_isRecording() //Are we recording?
{
	return recording?1:0; //Are we recording?
}

void sound_stopRecording() //Stop sound recording!
{
	lockaudio();
	closeWAV(&recording); //Stop recording!
	unlockaudio();
}

char recordingfilename[256];
extern char capturepath[256];
char *get_soundrecording_filename() //Filename for a screen capture!
{
	domkdir(capturepath); //Captures directory!
	uint_32 i = 0; //For the number!
	char filename2[256];
	memset(&filename2, 0, sizeof(filename2)); //Init filename!
	memset(&recordingfilename, 0, sizeof(recordingfilename)); //Init filename!
	do
	{
		snprintf(filename2,sizeof(filename2), "%s/recording_%" SPRINTF_u_UINT32 ".wav",capturepath,++i); //Next bitmap file!
	} while (file_exists(filename2)); //Still exists?
	snprintf(recordingfilename,sizeof(recordingfilename), "%s/recording_%" SPRINTF_u_UINT32 ".wav",capturepath,i); //The capture filename!
	return &recordingfilename[0]; //Give the filename for quick reference!
}

void sound_startRecording() //Start sound recording?
{
	if (recording) //Already recording?
	{
		sound_stopRecording(); //Stop recording first!
	}
	lockaudio();
	recording = createWAV(get_soundrecording_filename(),2,(uint_32)SW_SAMPLERATE); //Start recording to this file!
	unlockaudio();
}

//SDL audio callback:

/* This function is called by SDL whenever the sound card
   needs more samples to play. It might be called from a
   separate thread, so we should be careful what we touch. */
uint_64 totaltime_audio = 0; //Total time!
uint_64 totaltimes_audio = 0; //Total times!
uint_32 totaltime_audio_avg = 1; //Total time of an average audio thread. Use this for synchronization with other time-taking hardware threads.
void Sound_AudioCallback(void *user_data, Uint8 *audio, int length)
{
	if (__HW_DISABLED) return; //Disabled?
	/* Clear the audio buffer so we can mix samples into it. */

	//Now, mix all channels!
	sample_stereo_p ubuf = (sample_stereo_p) audio; //Buffer!
	#ifdef EXTERNAL_TIMING
	getuspassed(&audioticks); //Init!
	#endif
	uint_32 reallength = length/sizeof(*ubuf); //Total length!
	HW_mixaudio(ubuf,reallength); //Mix the audio!
	#ifdef EXTERNAL_TIMING
	uint_64 mspassed = getuspassed(&audioticks); //Load the time passed!
	totaltime_audio += mspassed; //Total time!
	++totaltimes_audio; //Total times increase!
	totaltime_audio_avg = (uint_32)SAFEDIV(totaltime_audio,totaltimes_audio); //Recalculate AVG audio time!
	#endif
	#ifdef DEBUG_SOUNDSPEED
	char time1[20];
	char time2[20];
	convertTime(mspassed,&time1[0]); //Ms passed!
	convertTime(totaltime_audio_avg,&time2[0]); //Total time passed!
	if (soundchannels_used) //Any channels out there?
	{
		dolog("soundservice","Mixing %u samples took: %s, average: %s",length/sizeof(ubuf[0]),time1,time2); //Log it!
	}
	#endif
}

void Sound_RecordCallback(void *user_data, Uint8 *audio, int length)
{
	if (__HW_DISABLED) return; //Disabled?
							   /* Clear the audio buffer so we can mix samples into it. */

							   //Now, mix all channels!
	sample_stereo_p ubuf = (sample_stereo_p)audio; //Buffer!
#ifdef EXTERNAL_TIMING
	getuspassed(&audioticks); //Init!
#endif
	uint_32 reallength = length / sizeof(*ubuf); //Total length!
	HW_recordaudio(ubuf, reallength); //Mix the audio!
#ifdef EXTERNAL_TIMING
	uint_64 mspassed = getuspassed(&audioticks); //Load the time passed!
	totaltime_audio += mspassed; //Total time!
	++totaltimes_audio; //Total times increase!
	totaltime_audio_avg = (uint_32)SAFEDIV(totaltime_audio, totaltimes_audio); //Recalculate AVG audio time!
#endif
#ifdef DEBUG_SOUNDSPEED
	char time1[20];
	char time2[20];
	convertTime(mspassed, &time1[0]); //Ms passed!
	convertTime(totaltime_audio_avg, &time2[0]); //Total time passed!
	if (soundchannels_used) //Any channels out there?
	{
		dolog("soundservice", "Recording %u samples took: %s, average: %s", length / sizeof(ubuf[0]), time1, time2); //Log it!
	}
#endif
}

byte SDLAudio_Loaded = 0; //Are we loaded (kept forever till quitting)
byte SDLRecord_Loaded = 0; //Are we loaded (kept forever till quitting)

void audiodevice_disconnectcurrentdevice(byte iscapture)
{
	//This just removes the rendering part of the current audio device and cleans up any dangling threads for a new connection!
	if (SDL_WasInit(SDL_INIT_AUDIO) && ((SDLAudio_Loaded && !iscapture) || (SDLRecord_Loaded && iscapture))) //Audio/capture loaded?
	{
#ifdef SDL_QUEUEAUDIO
		if (!iscapture) //Playback device?
		{
			if (audiodevice)
			{
				SDL_CloseAudioDevice(audiodevice); //Close our allocated audio device!
				audiodevice = 0; //Not connected anymore!
				SDLAudio_Loaded = 0; //Not anymore!
			}
		}
		else //Capture?
		{
			if (recorddevice)
			{
				SDL_CloseAudioDevice(recorddevice); //Close our allocated audio device!
				recorddevice = 0; //No record device connected anymore!
				SDLRecord_Loaded = 0; //Not anymore!
	}
		}
#else
#ifndef SDL2//#ifdef SDL2
		if (iscapture) //Record device?
		{
			if (recorddevice)
			{
				SDL_CloseAudioDevice(recorddevice); //Close our allocated audio device!
				recorddevice = 0; //No record device connected anymore!
				SDLRecord_Loaded = 0; //Not anymore!
			}
		}
		else //Playback device?
		{
			if (SDLAudio_Loaded) //Audio loaded?
			{
				#if SDL_VERSION_ATLEAST(2,0,4)
				if (audiodevice == 0) //Not loaded through audio service?
				{
					SDL_CloseAudio(); //Close the audio system!
				}
				else //Audio device method?
				{
					SDL_CloseAudioDevice(audiodevice); //Close our allocated audio device!
					audiodevice = 0; //Not connected anymore!
				}
				#else
				SDL_CloseAudio(); //Close the audio system!
				#endif
				SDLAudio_Loaded = 0; //Not loaded anymore!
			}
		}
#endif
#endif
	}

	if (!iscapture) //Playback device?
	{
		if (mixerready)
		{
			freeDoubleBufferedSound(&mixeroutput); //Release our double buffered output!
			mixerready = 0; //Not ready anymore!
		}
	}
	else //Capture device?
	{
		if (inputready)
		{
			freeDoubleBufferedSound(&mixerinput); //Release our double buffered output!
			inputready = 0; //Not ready anymore!
		}
	}
}

byte audiodevice_connected_dummy = 0; //Dummy!
void audiodevice_connected(uint_32 which, byte iscapture)
{
	uint_32 currentchannel;
	#ifdef IS_WINDOWS
	//Don't handle this on Windows: we're the default playback device always!
	if (!iscapture) return; //Only handle capture devices!
	#endif
	if (!(SDL_WasInit(SDL_INIT_AUDIO))) //Audio not loaded?
	{
		return; //Not able to handle this!
	}
	
	//Handle dangling first!
	audiodevice_disconnectcurrentdevice(iscapture); //Disconnect the current audio device!
	//Connect the audio device and switch over to the audio device from the previous one!
	if (!iscapture) //Playback device?
	{
        #ifndef SDL2//#ifdef SDL2
		if (audiodevice)
		{
		#endif
			PAUSEAUDIO(1); //Disable the thread!
        #ifndef SDL2//#ifdef SDL2
		}
		#endif
	}
	else //Capture device?
	{
        #ifndef SDL2//#ifdef SDL2
		if (recorddevice) //Allocated?
		{
			SDL_PauseAudioDevice(recorddevice, 1); //Disable the thread!
		}
		#endif
	}

	lockaudio(); //Make sure we're the only one!
	if (!iscapture) //Playback device?
	{
		/* Open the audio device. The sound driver will try to give us
		the requested format, but it might not succeed. The 'obtained'
		structure will be filled in with the actual format data. */
		audiospecs.freq = HW_SAMPLERATE;	/* desired output sample rate */
		audiospecs.format = AUDIO_S16SYS;	/* request signed 16-bit samples */
		audiospecs.channels = 2;	/* ask for stereo */
		audiospecs.samples = SAMPLESIZE;	/* this is more or less discretionary */
		audiospecs.size = audiospecs.samples * audiospecs.channels * sizeof(sample_t);
#ifdef SDL_QUEUEAUDIO
		audiospecs.callback = NULL; //We're queueing audio!
#else
		audiospecs.callback = &Sound_AudioCallback; //We're not queueing audio! Use the callback instead!
#endif
		audiospecs.userdata = NULL;	/* we don't need this */
#if SDL_VERSION_ATLEAST(2,0,4)
		audiodevice = SDL_OpenAudioDevice(SDL_GetAudioDeviceName(which, iscapture), 0, &audiospecs, NULL, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE); //We need this for our direct rendering!
		if (audiodevice == 0)
		{
			return; //Just to be safe!
		}
#else
		if (SDL_OpenAudio(&audiospecs, NULL) < 0)
		{
			return; //Just to be safe!
		}
#endif
#ifdef IS_LONGDOUBLE
		sound_soundtick = (DOUBLE)1000000000.0L / (DOUBLE)SW_SAMPLERATE; //Set the sample rate we render at!
#else
		sound_soundtick = (DOUBLE)1000000000.0 / (DOUBLE)SW_SAMPLERATE; //Set the sample rate we render at!
#endif
	}
	else //Capture device?
	{
#ifndef SDL2//#ifdef SDL2
		//Supporting audio recording?
		recordspecs.freq = HW_SAMPLERATE;	/* desired output sample rate */
		recordspecs.format = AUDIO_S16SYS;	/* request signed 16-bit samples */
		recordspecs.channels = 2;	/* ask for stereo */
		recordspecs.samples = SAMPLESIZE;	/* this is more or less discretionary */
		recordspecs.size = recordspecs.samples * recordspecs.channels * sizeof(sample_t);
		recordspecs.callback = &Sound_RecordCallback; //We're not queueing audio! Use the callback instead!
		recordspecs.userdata = NULL;	/* we don't need this */
		recorddevice = SDL_OpenAudioDevice(SDL_GetAudioDeviceName(which, iscapture), 1, &recordspecs, NULL, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE); //We need this for our direct rendering!
		if (recorddevice == 0) //No recording device?
		{
			dolog("soundservice", "Unable to open audio record device: %s", SDL_GetError());
			SDL_ClearError(); //Clear any error that's occurred!
			sound_recordtick = (DOUBLE)0; //Set the sample rate we render at!
		}
		else //Recording device found?
		{
			SDLRecord_Loaded = 1; //We're loaded!
#ifdef IS_LONGDOUBLE
			sound_recordtick = (DOUBLE)1000000000.0L / (DOUBLE)SW_RECORDRATE; //Set the sample rate we render at!
#else
			sound_recordtick = (DOUBLE)1000000000.0 / (DOUBLE)SW_RECORDRATE; //Set the sample rate we render at!
#endif
		}
#endif
	}
	if (iscapture) //Capture device?
	{
#ifndef SDL2//#ifdef SDL2
		if (SDLRecord_Loaded)
		{
			SDL_PauseAudioDevice(recorddevice, 1); //Disable the thread
		}
#endif
	}
	if (!iscapture) //Playback device?
	{
		SDLAudio_Loaded = 1; //We're loaded!
		//Allocate the buffers!
		if (allocDoubleBufferedSound32(SAMPLESIZE, &mixeroutput, 1, SW_SAMPLERATE)) //Valid buffer?
		{
			mixerready = 1; //We have a mixer!
		}
	}
	else //Capture device
	{
		if (allocDoubleBufferedSound32(SAMPLESIZE, &mixerinput, 1, SW_SAMPLERATE)) //Valid buffer?
		{
			inputready = 1; //We have a mixer!
		}
	}

	#ifdef SOUND_HIGHPASS
	if (!iscapture) //Playback device?
	{
		initSoundFilter(&soundhighpassfilter[0], 1, SOUND_HIGHPASS, SW_SAMPLERATE); //Initialize our output filter!
		initSoundFilter(&soundhighpassfilter[1], 1, SOUND_HIGHPASS, SW_SAMPLERATE); //Initialize our output filter!
	}
	else //Capture device?
	{
		initSoundFilter(&soundrecordfilter[0], 1, SOUND_HIGHPASS, SW_SAMPLERATE); //Initialize our record filter!
		initSoundFilter(&soundrecordfilter[1], 1, SOUND_HIGHPASS, SW_SAMPLERATE); //Initialize our record filter!
	}
	#endif

#ifdef SOUND_FILTER_VOLUME
	sound_filter_volume = (float)SOUND_FILTER_VOLUME; //Set the volume to limit against!
#endif
	if (iscapture) //Capture device?
	{
		inputleft = inputright = 0; //Clear input samples!
		currentrecordedsample = (signed2unsigned16(0) << 8) | signed2unsigned16(0); //Clear the currently recorded sample to initialize it!
	}

	//Finish up to start playing!
	if (!iscapture) //Playback device?
	{
		PAUSEAUDIO(0); //Start playing!
	}
    #ifndef SDL2//#ifdef SDL2
	if (iscapture) //Capture device?
	{
		if (SDLRecord_Loaded)
		{
			SDL_PauseAudioDevice(recorddevice, 0); //Start recording!
		}
	}
	#endif
	
	//TODO: Reset all channels to use the new samplerate speed!
	if (!iscapture) //Playback devices?
	{
		for (currentchannel = 0; currentchannel < soundchannels_used; ++currentchannel) //Process all allocated sound channels!
		{
			if (soundchannels[currentchannel].soundhandler) //Allocated?
			{
				audiodevice_connected_dummy = setSampleRate(soundchannels[currentchannel].soundhandler, soundchannels[currentchannel].extradata, soundchannels[currentchannel].samplerate); //Set the samplerate information required for playback!
			}
		}
	}
	unlockaudio(); //Finished!
}

void audiodevice_disconnected(uint_32 which, byte iscapture)
{
	//TODO: Disconnect the audio device and switch over to the default audio device!
	//Don't handle this at all? Just leave it dangling until the next device connects!
	if (iscapture) //Capture device disconnected?
	{
		lockaudio(); //Lock us!
		if (recording) //Already recording?
		{
			sound_stopRecording(); //Stop recording first!
		}
		unlockaudio(); //Finished!
	}
	else //Playback device?
	{
		#ifndef IS_WINDOWS
		//Don't handle this on Windows: we're the default playback device always!
		audiodevice_connected(0, 0); //Immediately resume on the default playback device instead!
		#endif
	}
}

//Audio initialisation!
void initAudio() //Initialises audio subsystem!
{
	if (__HW_DISABLED) return; //Abort?

	soundbuffer_maxval = (float)(pow(2.0, 32.0) - 1.0);
	soundbuffer_minval = (0.0f - (soundbuffer_maxval + 1.0f));

	if (SDL_WasInit(SDL_INIT_AUDIO)) //SDL rendering?
	{
		if (!SDLAudio_Loaded) //Not loaded yet?
		{
			#ifdef IS_LONGDOUBLE
			sound_soundtick = (DOUBLE)1000000000.0L / (DOUBLE)HW_SAMPLERATE; //Load the default sound rate!
			#else
			sound_soundtick = (DOUBLE)1000000000.0 / (DOUBLE)HW_SAMPLERATE; //Load the default sound rate!
			#endif

			if (!audioticksready) //Not ready yet?
			{
				initTicksHolder(&audioticks); //Init!
				initTicksHolder(&recordticks); //Init!
				audioticksready = 1; //Ready!
			}
			PAUSEAUDIO(1); //Disable the thread!

            #ifndef SDL2//#ifdef SDL2
			SDL_PauseAudioDevice(recorddevice,1); //Disable the thread!
			#endif
			
			/* Open the audio device. The sound driver will try to give us
			the requested format, but it might not succeed. The 'obtained'
			structure will be filled in with the actual format data. */
			audiospecs.freq = HW_SAMPLERATE;	/* desired output sample rate */
			audiospecs.format = AUDIO_S16SYS;	/* request signed 16-bit samples */
			audiospecs.channels = 2;	/* ask for stereo */
			audiospecs.samples = SAMPLESIZE;	/* this is more or less discretionary */
			audiospecs.size = audiospecs.samples * audiospecs.channels * sizeof(sample_t);
			#ifdef SDL_QUEUEAUDIO
			audiospecs.callback = NULL; //We're queueing audio!
			#else
			audiospecs.callback = &Sound_AudioCallback; //We're not queueing audio! Use the callback instead!
			#endif
			audiospecs.userdata = NULL;	/* we don't need this */
#if SDL_VERSION_ATLEAST(2,0,4)
			audiodevice = SDL_OpenAudioDevice(NULL, 0, &audiospecs, NULL, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE); //We need this for our direct rendering!
			if (audiodevice==0)
			{
				raiseError("sound service", "Unable to open audio device: %s", SDL_GetError());
				return; //Just to be safe!
			}
#else
			if (SDL_OpenAudio(&audiospecs, NULL) < 0)
			{
				raiseError("sound service","Unable to open audio playback device: %s", SDL_GetError());
				return; //Just to be safe!
			}
#endif
#ifndef SDL2//#ifdef SDL2
			//Supporting audio recording?
			recordspecs.freq = HW_SAMPLERATE;	/* desired output sample rate */
			recordspecs.format = AUDIO_S16SYS;	/* request signed 16-bit samples */
			recordspecs.channels = 2;	/* ask for stereo */
			recordspecs.samples = SAMPLESIZE;	/* this is more or less discretionary */
			recordspecs.size = recordspecs.samples * recordspecs.channels * sizeof(sample_t);
			recordspecs.callback = &Sound_RecordCallback; //We're not queueing audio! Use the callback instead!
			recordspecs.userdata = NULL;	/* we don't need this */
			recorddevice = SDL_OpenAudioDevice(NULL, 1, &recordspecs, NULL, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE); //We need this for our direct rendering!
			if (recorddevice == 0) //No recording device?
			{
				dolog("soundservice","Unable to open audio record device: %s",SDL_GetError());
				SDL_ClearError(); //Clear any error that's occurred!
				sound_recordtick = (DOUBLE)0; //Set the sample rate we render at!
			}
			else //Recording device found?
			{
				SDLRecord_Loaded = 1; //We're loaded!
				#ifdef IS_LONGDOUBLE
				sound_recordtick = (DOUBLE)1000000000.0L / (DOUBLE)SW_RECORDRATE; //Set the sample rate we render at!
				#else
				sound_recordtick = (DOUBLE)1000000000.0 / (DOUBLE)SW_RECORDRATE; //Set the sample rate we render at!
				#endif
			}
#endif
			#ifdef IS_LONGDOUBLE
			sound_soundtick = (DOUBLE)1000000000.0L / (DOUBLE)SW_SAMPLERATE; //Set the sample rate we render at!
			#else
			sound_soundtick = (DOUBLE)1000000000.0 / (DOUBLE)SW_SAMPLERATE; //Set the sample rate we render at!
			#endif
			memset(&soundchannels,0,sizeof(soundchannels)); //Initialise/reset all sound channels!
			SDLAudio_Loaded = 1; //We're loaded!
		}
		else //Already loaded, needs reset?
		{
			PAUSEAUDIO(1); //Disable the thread!
			resetchannels(); //Reset the channels!
		}

#ifndef SDL2//#ifdef SDL2
		if (SDLRecord_Loaded)
		{
			SDL_PauseAudioDevice(recorddevice, 1); //Disable the thread
		}
#endif

		//Allocate the buffers!
		if (allocDoubleBufferedSound32(SAMPLESIZE, &mixeroutput,1,SW_SAMPLERATE)) //Valid buffer?
		{
			mixerready = 1; //We have a mixer!
		}

		if (allocDoubleBufferedSound32(SAMPLESIZE, &mixerinput, 1, SW_SAMPLERATE)) //Valid buffer?
		{
			inputready = 1; //We have a mixer!
		}

#ifdef SOUND_HIGHPASS
		initSoundFilter(&soundhighpassfilter[0],1,SOUND_HIGHPASS,SW_SAMPLERATE); //Initialize our output filter!
		initSoundFilter(&soundhighpassfilter[1],1,SOUND_HIGHPASS,SW_SAMPLERATE); //Initialize our output filter!
		initSoundFilter(&soundrecordfilter[0],1,SOUND_HIGHPASS,SW_SAMPLERATE); //Initialize our record filter!
		initSoundFilter(&soundrecordfilter[1],1,SOUND_HIGHPASS,SW_SAMPLERATE); //Initialize our record filter!
#endif

		#ifdef SOUND_FILTER_VOLUME
		sound_filter_volume = (float)SOUND_FILTER_VOLUME; //Set the volume to limit against!
		#endif
		inputleft = inputright = 0; //Clear input samples!
		currentrecordedsample = (signed2unsigned16(0)<<8)|signed2unsigned16(0); //Clear the currently recorded sample to initialize it!

		//Finish up to start playing!
		calc_samplePos(); //Initialise sample position precalcs!
		PAUSEAUDIO(0); //Start playing!
#ifndef SDL2//#ifdef SDL2
		if (SDLRecord_Loaded)
		{
			SDL_PauseAudioDevice(recorddevice, 0); //Start recording!
		}
#endif
	}
}

void doneAudio()
{
	if (__HW_DISABLED) return; //Abort?
	resetchannels(); //Stop all channels!
	if (recording) //Already recording?
	{
		sound_stopRecording(); //Stop recording first!
	}
	if (SDL_WasInit(SDL_INIT_AUDIO) && SDLAudio_Loaded) //Audio loaded?
	{
#if defined(SDL_QUEUEAUDIO) || SDL_VERSION_ATLEAST(2,0,4)
		if (audiodevice)
		{
			SDL_CloseAudioDevice(audiodevice); //Close our allocated audio device!
		}
		if (recorddevice)
		{
			SDL_CloseAudioDevice(recorddevice); //Close our allocated audio device!
		}
#else
#ifndef SDL2//#ifdef SDL2
		if (recorddevice)
		{
			SDL_CloseAudioDevice(recorddevice); //Close our allocated audio device!
		}
#endif
		SDL_CloseAudio(); //Close the audio system!
#endif
		SDLAudio_Loaded = 0; //Not loaded anymore!
	}

	if (mixerready)
	{
		freeDoubleBufferedSound(&mixeroutput); //Release our double buffered output!
		mixerready = 0; //Not ready anymore!
	}

	if (inputready)
	{
		freeDoubleBufferedSound(&mixerinput); //Release our double buffered output!
		inputready = 0; //Not ready anymore!
	}

	free_samplePos(); //Free the sample position precalcs!
}
