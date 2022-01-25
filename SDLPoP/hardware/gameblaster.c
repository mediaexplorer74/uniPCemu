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
#include "headers/support/sounddoublebuffer.h" //Double buffered sound support!
#include "headers/emu/sound.h" //Sound support!
#include "headers/support/signedness.h" //Sign conversion support!
#include "headers/support/log.h" //Logging support!
#include "headers/hardware/ports.h" //I/O support!
#include "headers/support/filters.h" //Filter support!
#include "headers/support/wave.h" //WAV logging test support!

//Are we disabled?
#define __HW_DISABLED 0

/* Generic enable/disable flags: */

//Define to log a test wave of 440Hz!
//#define TESTWAVE

//To filter the output signal before resampling(Only required here when not using PWM, always required to be used with PWM)?
#define FILTER_SIGNAL

//Log the rendered Game Blaster raw output stream?
//#define LOG_GAMEBLASTER

//Enable generation of PWM signal instead of direct signal to generate samples?
#define PWM_OUTPUT

//Enable PDM-style output like a a real Game Blaster instead of PWM
#define PDM_OUTPUT

//Set up a test wave, with special signal, when enabled?
//#define DEBUG_OUTPUT 440.0f

/* Required defines to function correctly and compile: */

//Game Blaster sample rate and other audio defines!
//Game blaster runs at 14MHz divided by 2 divided by 256 clocks to get our sample rate to play at! Or divided by 4 to get 3.57MHz!
//Divided by 4 when rendering 16-level output using PWM equals 4 times lower frequency when using levels instead of PWM(16-level PCM). So divide 4 further by 16 for the used rate!
//Reduce it by 16 times to provide 16-PWM states both positive and negative(using positive and negative signals, e.g. +5V, 0V and -5V)!
#define MHZ14_BASETICK 4
//#define MHZ14_BASETICK 256
//We render at ~44.1kHz!
#define MHZ14_RENDERTICK 324

//Sample rate settings and output volume!
//Base rate of the Game Blaster to run at!
#define __GAMEBLASTER_BASERATE (MHZ14/MHZ14_BASETICK) 

//Renderer defines to use!
#define __GAMEBLASTER_SAMPLERATE (MHZ14/MHZ14_RENDERTICK)
#define __GAMEBLASTER_SAMPLEBUFFERSIZE 4096
#define __GAMEBLASTER_VOLUME 100.0f

//We're two times 6 channels mixed on left and right, so not 6 channels but 12 channels each!
#ifdef IS_LONGDOUBLE
#define __GAMEBLASTER_AMPLIFIER (1.0L/12.0L)
#else
#define __GAMEBLASTER_AMPLIFIER (1.0/12.0)
#endif

typedef struct
{
	byte Amplitude; //Amplitude: 0-16, to wrap around!
	byte PWMCounter; //Counter 0-16 that's counting!
	byte flipflopoutput; //Output signal of the PWM!
	int_32 *waveform; //Current waveform step to process!
	int_32 waveforminput; //Flipflop result on the input to toggle between and 0V!
} PWMOUTPUT; //Channel PWM output signal for left or right channel!

typedef struct
{
	byte frequency;
	byte frequency_enable;
	byte noise_enable;
	byte octave; //0-7
	byte amplitude[2]; //0-F?
	byte envelope[2]; //0-F, 10=off.

	//Data required for timing the square wave
	float time; //Time
	float freq; //Frequency!
	byte level; //The level!
	byte ampenv[16]; //All envelope outputs! Index Bit0=Right channel, Bit1=Channel output index, Bit 2=Noise output! Output: 0=Negative, 1=Positive. bit3=PWM period.
	byte activeampenv; //Currently loaded ampenv!
	byte toneonnoiseonflipflop; //Flipflop used for mode 3 rendering!
	byte noisechannel; //Linked noise channel!
	byte PWMAmplitude[2]; //PWM amplitude for left and right channel to use!
	PWMOUTPUT PWMOutput[2]; //Left/right channel PWM output signal
} SAA1099_CHANNEL;

typedef struct
{
	//Data required for simulating noise generators!
	float freq; //Frequency!
	byte laststatus; //The last outputted status for detecting cycles!
	uint_32 lfsr; //The shift register!
	byte level; //1-bit current level!
} SAA1099_NOISE;

typedef struct
{
	float freq; //Currently used frequency!
	uint_32 timepoint; //Point that overflows in time!
	uint_32 timeout; //Half-wave timeout!
	byte output; //Flipflop output!
} SAA1099_SQUAREWAVE;

typedef struct
{
	//Basic storage!
	byte regsel; //The selected register!
	byte registers[0x20]; //All selectable registers!

	//Global Data!
	word noise_params[2];
	word env_enable[2];
	word env_reverse_right[2];
	byte env_mode[2];
	word env_bits[2];
	word env_clock[2];
	byte env_step[2];
	byte all_ch_enable;
	byte sync_state;
	
	//Information taken from the registers!
	SAA1099_CHANNEL channels[8]; //Our channels!
	SAA1099_NOISE noise[2]; //Noise generators!
	SAA1099_SQUAREWAVE squarewave[10]; //Everything needed to generate a square wave!
} SAA1099; //All data for one SAA-1099 chip!

struct
{
	word baseaddr; //Base address of the Game Blaster!
	byte soundblastercompatible; //Do we use sound blaster compatible I/O
	SOUNDDOUBLEBUFFER soundbuffer; //Our two sound buffers for our two chips!
	byte storelatch[2]; //Two store/latch buffers!
	SAA1099 chips[2]; //The two chips for generating output!
	HIGHLOWPASSFILTER filter[2]; //Filter for left and right channels, low-pass type!
	uint_32 baseclock; //Base clock to render at(up to bus rate of 14.31818MHz)!
	FIFOBUFFER *rawsignal; //Raw output signal we're generating!
	DOUBLE samplesleft; //Samples left to process!
} GAMEBLASTER; //Our game blaster information!

//Safety check of above defines to make sure we don't generate wrong samples(PWM without filter creates an invalid output when resampled)!
#ifdef PWM_OUTPUT
//We're outputting PWM?
#ifndef FILTER_SIGNAL
//Not filtering when outputting PWM? Enforce filtering on!
#define FILTER_SIGNAL
#endif
#endif

//AmpEnv Inputs!
//bit0=Right channel
#define AMPENV_INPUT_RIGHTCHANNEL 0x01
//bit1=Channel output index
#define AMPENV_INPUT_SQUAREWAVEOUTPUT 0x02
//bit2=Noise output!
#define AMPENV_INPUT_NOISEOUTPUT 0x04
//bit3=PWM period.
#define AMPENV_INPUT_PWMPERIOD 0x08
//Lookup blocks when changing frequency/noise/volume:
#define AMPENV_INPUT_NOISEENABLE 0x10
#define AMPENV_INPUT_NOISEDISABLE 0x00
#define AMPENV_INPUT_FREQUENCYENABLE 0x20
#define AMPENV_INPUT_FREQUENCYDISABLE 0x00

//AmpEnv Outputs!
//bit0=Right channel
#define AMPENV_RESULT_RIGHTCHANNEL 0x01
#define AMPENV_RESULT_LEFTCHANNEL 0x00
//bit1=Sign. 0=Negative, 1=Positive
#define AMPENV_RESULT_POSITIVE 0x02
#define AMPENV_RESULT_NEGATIVE 0x00
//bit2=Ignore sign. We're silence(0V)!
#define AMPENV_RESULT_SILENCE 0x04

float AMPLIFIER = 0.0; //The amplifier, amplifying samples to the full range!

WAVEFILE *GAMEBLASTER_LOG = NULL; //Logging the Game Blaster output?

byte precalcSAAenvelopes[8*0x40]; //All possible volume envelope waveforms, precalculated!

OPTINLINE byte SAAEnvelope(byte waveform, byte position)
{
	switch (waveform&7) //What waveform?
	{
		case 0: //Zero amplitude?
			return 0; //Always 0!
		case 1: //Maximum amplitude?
			return 0xF; //Always max!
		case 2: //Single decay?
			if (unlikely(position<0x10))
				return 0xF-position; //Decay!
			else
				return 0; //Silence!
		case 3: //Repetitive decay?
			return 0xF-(position&0xF); //Repetitive decay!
		case 4: //Single triangular?
			if (unlikely(position>0x20)) //Zero past?
				return 0; //Zero!
			else if (unlikely(position>0x10)) //Decay?
				return 0xF-(position&0xF); //Decay!
			else //Attack?
				return (position&0xF); //Attack!
		case 5: //Repetitive triangular?
			if (likely(position&0x10)) //Decay?
				return 0xF-(position&0xF); //Decay!
			else //Attack?
				return (position&0xF); //Attack!
		case 6: //Single attack?
			if (unlikely(position<0x10))
				return position; //Attack!
			else
				return 0;
		case 7: //Repetitive attack?
			return (position&0xF); //Attack!
		default:
			break;
	}
	return 0; //Unknown envelope?
}

OPTINLINE word calcAmplitude(byte amplitude)
{
	return (((amplitude<<15)-amplitude)>>4); //Simple calculation for our range!
}

int_32 amplitudes[0x10]; //All possible amplitudes!

byte AmpEnvPrecalcs[0x40]; //AmpEnv precalcs of all possible states!
OPTINLINE void updateAmpEnv(SAA1099 *chip, byte channel)
{
	INLINEREGISTER byte newampenv; //What active amplitude envelope precalcs?
	chip->channels[channel].PWMAmplitude[0] = (((int_32)(chip->channels[channel].amplitude[0])*(int_32)chip->channels[channel].envelope[0]) >> 4)&0xF; //Left envelope PWM time!
	chip->channels[channel].PWMAmplitude[1] = (((int_32)(chip->channels[channel].amplitude[1])*(int_32)chip->channels[channel].envelope[1]) >> 4)&0xF; //Right envelope PWM time!
	//bit0=right channel
	//bit1=square wave output
	//bit2=noise output
	//bit3=PWM period
	//Generate all 16 state precalcs!
	newampenv = (chip->channels[channel].frequency_enable<<1); //Load and preshift the frequency enable bit into the precalcs base index!
	newampenv |= chip->channels[channel].noise_enable; //Add the noise enable bit into the precalcs base index!
	newampenv <<= 4; //What amplitude envelope to use(index into the precalcs)? We're specifying the upper 2 bits!
	if (unlikely(newampenv!=chip->channels[channel].activeampenv)) //Changed amplitude type to render?
	{
		chip->channels[channel].activeampenv = newampenv; //Modifying the amplitude envelope we're loading now!
		memcpy(&chip->channels[channel].ampenv[0],&AmpEnvPrecalcs[newampenv],sizeof(chip->channels[channel].ampenv)); //Copy the output information over!
	}
}

OPTINLINE void calcAmpEnvPrecalcs()
{
	word i;
	byte input;
	for (i=0;i<NUMITEMS(AmpEnvPrecalcs);++i) //Process all precalcs!
	{
		input = (i&0xF); //Input signal we're precalculating!
		//Output(AmpEnvPrecalcs):
		//bit0=Right channel
		//bit1=Sign. 0=Negative, 1=Positive
		//bit2=Ignore sign. We're silence!
		//Lookup table input(i):
		//bits0-3=Index into the lookup table to generate:
		//bit0=Right channel
		//bit1=Channel output index
		//bit2=Noise output!
		//bit3=PWM period.
		//Index Loaded to select synthesis method:
		//bit4=Noise enable(block index)
		//bit5=Frequency enable(block index)
		switch ((i)&(AMPENV_INPUT_FREQUENCYENABLE|AMPENV_INPUT_NOISEENABLE)) //Noise/frequency mode?
		{
			default: //Safety check
			case 0: //Both disabled?
				AmpEnvPrecalcs[i] = AMPENV_RESULT_SILENCE; //No output, channel and positive/negative doesn't matter!
				break;
			case AMPENV_INPUT_NOISEENABLE: //Noise only?
				AmpEnvPrecalcs[i] = ((input&AMPENV_INPUT_NOISEOUTPUT)?AMPENV_RESULT_POSITIVE:AMPENV_RESULT_NEGATIVE)|((input&AMPENV_INPUT_RIGHTCHANNEL)?AMPENV_RESULT_RIGHTCHANNEL:AMPENV_RESULT_LEFTCHANNEL); //Noise at max volume!
				break;
			case AMPENV_INPUT_FREQUENCYENABLE: //Frequency only?
				AmpEnvPrecalcs[i] = ((input&AMPENV_INPUT_SQUAREWAVEOUTPUT)?AMPENV_RESULT_POSITIVE:AMPENV_RESULT_NEGATIVE)|((input&AMPENV_INPUT_RIGHTCHANNEL)?AMPENV_RESULT_RIGHTCHANNEL:AMPENV_RESULT_LEFTCHANNEL); //Noise at max volume!
				break;
			case (AMPENV_INPUT_FREQUENCYENABLE|AMPENV_INPUT_NOISEENABLE): //Noise+Frequency?
				if (likely(input&AMPENV_INPUT_SQUAREWAVEOUTPUT)) //Tone high state?
				{
					AmpEnvPrecalcs[i] = ((input&AMPENV_INPUT_RIGHTCHANNEL)?AMPENV_RESULT_RIGHTCHANNEL:AMPENV_RESULT_LEFTCHANNEL)|AMPENV_RESULT_POSITIVE; //Noise at max volume, positive!
				}
				else if ((input&AMPENV_INPUT_NOISEOUTPUT)==0) //Tone low and noise is low? Low at full amplitude!
				{
					AmpEnvPrecalcs[i] = ((input&AMPENV_INPUT_RIGHTCHANNEL)?AMPENV_RESULT_RIGHTCHANNEL:AMPENV_RESULT_LEFTCHANNEL)|AMPENV_RESULT_NEGATIVE; //Noise at max volume, negative!
				}
				else //Tone low? Then noise(output) is high only every other PWM period!
				{
					AmpEnvPrecalcs[i] = ((input&AMPENV_INPUT_RIGHTCHANNEL)?AMPENV_RESULT_RIGHTCHANNEL:AMPENV_RESULT_LEFTCHANNEL)|(((input&AMPENV_INPUT_PWMPERIOD)?(AMPENV_RESULT_POSITIVE):(AMPENV_RESULT_NEGATIVE))); //Noise at half volume!
				}
				break;
		}
	}
}

byte SAA1099_basechannels[2] = {0,3}; //The base channels!

OPTINLINE void tickSAAEnvelope(SAA1099 *chip, byte channel)
{
	byte basechannel;
	channel &= 1; //Only two channels available!
	basechannel = SAA1099_basechannels[channel]; //Base channel!
	if (likely(chip->env_enable[channel])) //Envelope enabled and running?
	{
		byte step,mode,mask; //Temp data!
		mode = chip->env_mode[channel]; //The mode to use!
		//Step from 0..63 and then loop 32..63
		step = ++chip->env_step[channel];
		step &= 0x3F; //Wrap around!
		step |= (chip->env_step[channel]&0x20); //OR in the current high block to loop the high part!
		chip->env_step[channel] = step; //Save the new step now used!
		mask = 0xF; //Full resolution!
		mask &= ~chip->env_bits[channel]; //Apply the bit resolution we use to mask bits off when needed!
		
		//Now, apply the current envelope!
		chip->channels[basechannel].envelope[0] = chip->channels[basechannel+1].envelope[0] = chip->channels[basechannel+2].envelope[0] = (precalcSAAenvelopes[(mode<<6)|step]&mask); //Apply the normal envelope!
		if (chip->env_reverse_right[channel]) //Reverse right envelope?
		{
			chip->channels[basechannel].envelope[1] = chip->channels[basechannel+1].envelope[1] = chip->channels[basechannel+2].envelope[1] = ((0xF-precalcSAAenvelopes[(mode<<6)|step])&mask); //Apply the reversed envelope!
		}
		else //Normal right envelope?
		{
			chip->channels[basechannel].envelope[1] = chip->channels[basechannel+1].envelope[1] = chip->channels[basechannel+2].envelope[1] = (precalcSAAenvelopes[(mode<<6)|step]&mask); //Apply the normal envelope!
		}
	}
	else //Envelope mode off, set all envelope factors to 16!
	{
		chip->channels[basechannel].envelope[0] = chip->channels[basechannel].envelope[1] = 
			chip->channels[basechannel+1].envelope[0] = chip->channels[basechannel+1].envelope[1] =
			chip->channels[basechannel+2].envelope[0] = chip->channels[basechannel+2].envelope[1] = 0x10; //We're off!
	}
	updateAmpEnv(chip,basechannel++); //Update the amplitude/envelope!
	updateAmpEnv(chip,basechannel++); //Update the amplitude/envelope!
	updateAmpEnv(chip,basechannel); //Update the amplitude/envelope!
}

OPTINLINE void writeSAA1099Address(SAA1099 *chip, byte address)
{
	chip->regsel = (address&0x1F); //Select the register!
	switch (chip->regsel) //What register has been selected?
	{
		case 0x18:
		case 0x19:
			if (unlikely(chip->env_clock[0])) tickSAAEnvelope(chip,0); //Tick channel 0?
			if (unlikely(chip->env_clock[1])) tickSAAEnvelope(chip,1); //Tick channel 1?
			break;
		default: //Unknown?
			break;
	}
}

OPTINLINE void updateSAA1099RNGfrequency(SAA1099 *chip, byte channel)
{
	byte channel2=channel|8;
	if (unlikely(chip->noise[channel].freq!=chip->squarewave[channel2].freq)) //Frequency changed?
	{
		#ifdef IS_LONGDOUBLE
		chip->squarewave[channel2].timeout = (uint_32)(__GAMEBLASTER_BASERATE/(DOUBLE)(2.0L*chip->noise[channel].freq)); //New timeout!
		#else
		chip->squarewave[channel2].timeout = (uint_32)(__GAMEBLASTER_BASERATE/(DOUBLE)(2.0*chip->noise[channel].freq)); //New timeout!
		#endif
		chip->squarewave[channel2].timepoint = 0; //Reset the timepoint!
		chip->squarewave[channel2].freq = chip->noise[channel].freq; //We're updated!
	}
}

float noise_frequencies[3] = {31250.0f,15625.0f,7812.0f}; //Normal frequencies!

OPTINLINE void updateSAA1099noisesettings(SAA1099 *chip, byte channel)
{
	switch (chip->noise_params[channel]) //What frequency to use?
	{
	default:
	case 0:
	case 1:
	case 2: //Normal frequencies!
		chip->noise[channel].freq = noise_frequencies[chip->noise_params[channel]]; //Normal lookup!
		break;
	case 3:
		chip->noise[channel].freq = chip->channels[(channel*3)].freq; //Channel 3 frequency instead!
		break;
	}
	updateSAA1099RNGfrequency(chip,channel);
}

OPTINLINE void updateSAA1099frequency(SAA1099 *chip, byte channel) //on octave/frequency change!
{
	byte noisechannels[8] = {0,2,2,1,2,2,2,2}; //Noise channels to use!
	channel &= 7; //Safety on channel!
	#ifdef IS_LONGDOUBLE
	chip->channels[channel].freq = (float)((DOUBLE)((uint_64)(GAMEBLASTER.baseclock/512)<<chip->channels[channel].octave)/(DOUBLE)(511.0L-chip->channels[channel].frequency)); //Calculate the current frequency to use!
	#else
	chip->channels[channel].freq = (float)((DOUBLE)((uint_64)(GAMEBLASTER.baseclock/512)<<chip->channels[channel].octave)/(DOUBLE)(511.0-chip->channels[channel].frequency)); //Calculate the current frequency to use!
	#endif
	if (unlikely(chip->channels[channel].freq!=chip->squarewave[channel].freq)) //Frequency changed?
	{
		#ifdef IS_LONGDOUBLE
		chip->squarewave[channel].timeout = (uint_32)(__GAMEBLASTER_BASERATE/(DOUBLE)(2.0L*(DOUBLE)chip->channels[channel].freq)); //New timeout!
		#else
		chip->squarewave[channel].timeout = (uint_32)(__GAMEBLASTER_BASERATE/(DOUBLE)(2.0*(DOUBLE)chip->channels[channel].freq)); //New timeout!
		#endif
		chip->squarewave[channel].timepoint = 0; //Reset!
		chip->squarewave[channel].freq = chip->channels[channel].freq; //We're updated!
	}
	if (unlikely(noisechannels[channel]!=2)) //Noise channel might be affected too?
	{
		updateSAA1099noisesettings(chip,noisechannels[channel]); //Update the noise channel frequency if needed as well!
	}
}

OPTINLINE void writeSAA1099Value(SAA1099 *chip, byte value)
{
	INLINEREGISTER byte reg;
	reg = chip->regsel; //The selected register to write to!
	chip->registers[reg] = value; //Save the register data itself!
	byte oldval,updated; //For detecting updates!
	word oldvalw;
	switch (reg) //What register is written?
	{
		case 0x00:
		case 0x01:
		case 0x02:
		case 0x03:
		case 0x04:
		case 0x05: //Channel n amplitude?
			reg &= 7;
			oldvalw = chip->channels[reg].amplitude[0];
			chip->channels[reg].amplitude[0] = value&0xF;
			updated = (chip->channels[reg].amplitude[0]!=oldvalw); //Changed?

			oldvalw = chip->channels[reg].amplitude[1];
			chip->channels[reg].amplitude[1] = (value>>4);
			updated |= (chip->channels[reg].amplitude[1]!=oldvalw); //Changed?

			if (unlikely(updated)) updateAmpEnv(chip,reg); //Update amplitude/envelope!
			break;
		case 0x08:
		case 0x09:
		case 0x0A:
		case 0x0B:
		case 0x0C:
		case 0x0D: //Channel n frequency?
			reg &= 7;
			oldval = chip->channels[reg].frequency;
			chip->channels[reg].frequency = value; //Set the frequency!
			if (unlikely(oldval!=chip->channels[reg].frequency)) updateSAA1099frequency(chip,reg); //Update frequency!
			break;
		case 0x10:
		case 0x11:
		case 0x12: //Channel n octave?
			reg &= 3;
			oldval = chip->channels[reg<<1].octave;
			chip->channels[reg<<1].octave = (value&7);
			if (unlikely(oldval!=chip->channels[reg<<1].octave)) updateSAA1099frequency(chip,(reg<<1)); //Update frequency!
			
			oldval = chip->channels[(reg<<1)|1].octave;
			chip->channels[(reg<<1)|1].octave = ((value>>4)&7);
			if (unlikely(oldval!=chip->channels[(reg<<1)|1].octave)) updateSAA1099frequency(chip,((reg<<1)|1)); //Update frequency!
			break;
		case 0x14: //Channel n frequency enable?
			oldval = chip->channels[0].frequency_enable;
			chip->channels[0].frequency_enable = (value&1);
			if (unlikely(oldval!=chip->channels[0].frequency_enable)) updateAmpEnv(chip,0); //Update AmpEnv!

			value >>= 1;
			oldval = chip->channels[1].frequency_enable;
			chip->channels[1].frequency_enable = (value&1);
			if (unlikely(oldval!=chip->channels[1].frequency_enable)) updateAmpEnv(chip,1); //Update AmpEnv!

			value >>= 1;
			oldval = chip->channels[2].frequency_enable;
			chip->channels[2].frequency_enable = (value&1);
			if (unlikely(oldval!=chip->channels[2].frequency_enable)) updateAmpEnv(chip,2); //Update AmpEnv!

			value >>= 1;
			oldval = chip->channels[3].frequency_enable;
			chip->channels[3].frequency_enable = (value&1);
			if (unlikely(oldval!=chip->channels[3].frequency_enable)) updateAmpEnv(chip,3); //Update AmpEnv!

			value >>= 1;
			oldval = chip->channels[4].frequency_enable;
			chip->channels[4].frequency_enable = (value&1);
			if (unlikely(oldval!=chip->channels[4].frequency_enable)) updateAmpEnv(chip,4); //Update AmpEnv!

			value >>= 1;
			oldval = chip->channels[5].frequency_enable;
			chip->channels[5].frequency_enable = (value&1);
			if (unlikely(oldval!=chip->channels[5].frequency_enable)) updateAmpEnv(chip,5); //Update AmpEnv!
			break;
		case 0x15: //Channel n noise enable?
			reg = value; //Load for processing!
			oldval = chip->channels[0].noise_enable;
			chip->channels[0].noise_enable = (reg&1);
			if (unlikely(oldval!=chip->channels[0].noise_enable)) updateAmpEnv(chip,0); //Update AmpEnv!

			reg >>= 1;
			oldval = chip->channels[1].noise_enable;
			chip->channels[1].noise_enable = (reg&1);
			if (unlikely(oldval!=chip->channels[1].noise_enable)) updateAmpEnv(chip,1); //Update AmpEnv!

			reg >>= 1;
			oldval = chip->channels[2].noise_enable;
			chip->channels[2].noise_enable = (reg&1);
			if (unlikely(oldval!=chip->channels[2].noise_enable)) updateAmpEnv(chip,2); //Update AmpEnv!

			reg >>= 1;
			oldval = chip->channels[3].noise_enable;
			chip->channels[3].noise_enable = (reg&1);
			if (unlikely(oldval!=chip->channels[3].noise_enable)) updateAmpEnv(chip,3); //Update AmpEnv!

			reg >>= 1;
			oldval = chip->channels[4].noise_enable;
			chip->channels[4].noise_enable = (reg&1);
			if (unlikely(oldval!=chip->channels[4].noise_enable)) updateAmpEnv(chip,4); //Update AmpEnv!

			reg >>= 1;
			oldval = chip->channels[5].noise_enable;
			chip->channels[5].noise_enable = (reg&1);
			if (unlikely(oldval!=chip->channels[5].noise_enable)) updateAmpEnv(chip,5); //Update AmpEnv!
			break;
		case 0x16: //Noise generators parameters?
			chip->noise_params[0] = (value&3);
			updateSAA1099noisesettings(chip,0);

			chip->noise_params[1] = ((value>>4)&3);
			updateSAA1099noisesettings(chip,0);
			break;
		case 0x18:
		case 0x19: //Envelope generators parameters?
			reg &= 1; //What channel?
			chip->env_reverse_right[reg] = (value&1);
			chip->env_mode[reg] = ((value>>1)&7); //What mode?
			chip->env_bits[reg] = ((value&0x10)>>4);
			chip->env_clock[reg] = ((value&0x20)>>5);
			chip->env_enable[reg] = ((value&0x80)>>7);
			//Reset the envelope!
			chip->env_step[reg] = 0; //Reset the envelope!
			break;
		case 0x1C: //Channels enable and reset generators!
			chip->all_ch_enable = (value&1);
			if (unlikely((chip->sync_state = ((value&2)>>1)))) //Sync & Reset generators?
			{
				for (reg=0;reg<6;++reg)
				{
					chip->channels[reg].level = 0;
					chip->squarewave[reg].timepoint = 0;
					chip->squarewave[reg].output = 0; //Reset wave output signal voltage?
				}
			}
			break;
		default: //Unknown register?
			break; //Silently ignore invalid and unimplemented writes!
	}
}

OPTINLINE byte getSAA1099SquareWave(SAA1099 *chip, byte channel)
{
	INLINEREGISTER byte result;
	INLINEREGISTER uint_32 timepoint;
	result = chip->squarewave[channel].output; //Save the current output to give!
	timepoint = chip->squarewave[channel].timepoint; //Next timepoint!
	++timepoint; //Next timepoint!
	if (unlikely(timepoint>=chip->squarewave[channel].timeout)) //Timeout? Flip-flop!
	{
		result ^= AMPENV_INPUT_SQUAREWAVEOUTPUT; //Flip-flop to produce a square wave! We're bit 1 of the output!
		chip->squarewave[channel].output = result; //Save the new state that's changed!
		timepoint = 0; //Reset the timepoint!
	}
	chip->squarewave[channel].timepoint = timepoint; //Save the resulting timepoint to advance the wave!
	return result; //Give the resulting square wave!
}

#ifdef PWM_OUTPUT
int_32 PWM_outputs[4] = {-SHRT_MAX,SHRT_MAX,0,0}; //Output, if any! Four positive/negative channel input entries plus 4 0V entries!
#else
int_32 PWM_outputs[4] = {-1,1,0,0}; //Output, if any!
#endif

//PDM waveforms on the SAA1099P are reversed of the normal output(0=+/-5V, 1=0V)
//Fast(shorthand) versions of the waveform outputs to prevent needing to shift! WV_1 makes it mute output!
//Use a bitmask instead: 0xFFFF: -MAX or +MAX(positive), 0: 0V.
#define WV_1 0
#define WV_0 ~0
int_32 WAVEFORM_OUTPUT[16][16] = { //PDM Waveforms for a selected output!
	#ifndef PDM_OUTPUT
	//PWM output?
	{WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1}, //Volume 0
	{WV_0,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1}, //Volume 1
	{WV_0,WV_0,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1}, //Volume 2
	{WV_0,WV_0,WV_0,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1}, //Volume 3
	{WV_0,WV_0,WV_0,WV_0,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1}, //Volume 4
	{WV_0,WV_0,WV_0,WV_0,WV_0,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1}, //Volume 5
	{WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1}, //Volume 6
	{WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1}, //Volume 7
	{WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1}, //Volume 8
	{WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1}, //Volume 9
	{WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1}, //Volume A
	{WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_1,WV_1,WV_1,WV_1,WV_1}, //Volume B
	{WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_1,WV_1,WV_1,WV_1}, //Volume C
	{WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_1,WV_1,WV_1}, //Volume D
	{WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_1,WV_1}, //Volume E
	{WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_1}, //Volume F
	#else
	//PDM output?
	{WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1}, //Volume 0
	{WV_1,WV_0,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1}, //Volume 1
	{WV_1,WV_1,WV_0,WV_0,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1}, //Volume 2
	{WV_1,WV_0,WV_0,WV_0,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1}, //Volume 3
	{WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_0,WV_0,WV_0,WV_0,WV_1,WV_1,WV_1,WV_1}, //Volume 4
	{WV_1,WV_0,WV_1,WV_1,WV_1,WV_1,WV_1,WV_1,WV_0,WV_0,WV_0,WV_0,WV_1,WV_1,WV_1,WV_1}, //Volume 5
	{WV_1,WV_1,WV_0,WV_0,WV_1,WV_1,WV_1,WV_1,WV_0,WV_0,WV_0,WV_0,WV_1,WV_1,WV_1,WV_1}, //Volume 6
	{WV_1,WV_0,WV_0,WV_0,WV_1,WV_1,WV_1,WV_1,WV_0,WV_0,WV_0,WV_0,WV_1,WV_1,WV_1,WV_1}, //Volume 7
	{WV_1,WV_1,WV_1,WV_1,WV_0,WV_0,WV_0,WV_0,WV_1,WV_1,WV_1,WV_1,WV_0,WV_0,WV_0,WV_0}, //Volume 8
	{WV_1,WV_0,WV_1,WV_1,WV_0,WV_0,WV_0,WV_0,WV_1,WV_1,WV_1,WV_1,WV_0,WV_0,WV_0,WV_0}, //Volume 9
	{WV_1,WV_1,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_1,WV_1,WV_1,WV_1,WV_0,WV_0,WV_0,WV_0}, //Volume A
	{WV_1,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_1,WV_1,WV_1,WV_1,WV_0,WV_0,WV_0,WV_0}, //Volume B
	{WV_1,WV_1,WV_1,WV_1,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0}, //Volume C
	{WV_1,WV_0,WV_1,WV_1,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0}, //Volume D
	{WV_1,WV_1,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0}, //Volume E
	{WV_1,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0,WV_0} //Volume F
	#endif
};

//All loading PWM states!
void SAA1099PWM_NewCounter(SAA1099 *chip, byte channel, byte output, PWMOUTPUT *PWM) //State 0, 0x10!
{
	//Timeout? Load new information and start the next PWM sample!
	//Load the new PWM timeout and PWM settings from the channel!
	output |= chip->channels[channel].toneonnoiseonflipflop; //Save the original output with flipflop information for reference!
	output = chip->channels[channel].ampenv[output]; //Output with flip-flop!
	PWM->flipflopoutput = ((output&(AMPENV_RESULT_POSITIVE|AMPENV_RESULT_SILENCE))>>1); //Start output, if any! We're starting high! Shift positive to bit 0 and silence to bit 1!
	PWM->Amplitude = chip->channels[channel].PWMAmplitude[(output&AMPENV_RESULT_RIGHTCHANNEL)]; //Update the amplitude to use!
	PWM->waveform = &WAVEFORM_OUTPUT[PWM->Amplitude][0]; //Start the new waveform!
	#ifdef PWM_OUTPUT
	PWM->waveforminput = PWM_outputs[PWM->flipflopoutput]; //Waveform input(positive/negative/0V)!
	#else
	PWM->waveforminput = PWM_outputs[PWM->flipflopoutput]*(sword)amplitudes[PWM->Amplitude]; //Give the proper output as a simple pre-defined 16-bit sample instead!
	#endif
}

void SAA1099PWM_FinalCounterRight(SAA1099 *chip, byte channel, byte output, PWMOUTPUT *PWM) //State 0x1F
{
	//Passthrough to apply PWM resetting the counter always!
	PWM->PWMCounter = 0; //Reset the counter to count the active time!
	chip->channels[channel].toneonnoiseonflipflop ^= AMPENV_INPUT_PWMPERIOD; //Trigger the flipflop at PWM samplerate only once(entire channel)!
}

void SAA1099PWM_FinalCounterLeft(SAA1099 *chip, byte channel, byte output, PWMOUTPUT *PWM) //State 0x0F
{
	//Passthrough to apply PWM resetting the counter always!
	PWM->PWMCounter = 0; //Reset the counter to count the active time!
}

void SAA1099PWM_DummyCounter(SAA1099 *chip, byte channel, byte output, PWMOUTPUT *PWM) //State 0x0F
{
	//Do nothing!
}

typedef void (*SAA1099PWM_Counter)(SAA1099 *chip, byte channel, byte output, PWMOUTPUT *PWM); //State 0x0F

SAA1099PWM_Counter SAA1099PWMCounters[0x20] = {
	//Left counter
	SAA1099PWM_NewCounter, //00
	SAA1099PWM_NewCounter, //01
	SAA1099PWM_DummyCounter, //02
	SAA1099PWM_DummyCounter, //03
	SAA1099PWM_DummyCounter, //04
	SAA1099PWM_DummyCounter, //05
	SAA1099PWM_DummyCounter, //06
	SAA1099PWM_DummyCounter, //07
	SAA1099PWM_DummyCounter, //08
	SAA1099PWM_DummyCounter, //09
	SAA1099PWM_DummyCounter, //0A
	SAA1099PWM_DummyCounter, //0B
	SAA1099PWM_DummyCounter, //0C
	SAA1099PWM_DummyCounter, //0D
	SAA1099PWM_DummyCounter, //0E
	SAA1099PWM_DummyCounter, //0F
	SAA1099PWM_DummyCounter, //10
	SAA1099PWM_DummyCounter, //11
	SAA1099PWM_DummyCounter, //12
	SAA1099PWM_DummyCounter, //13
	SAA1099PWM_DummyCounter, //14
	SAA1099PWM_DummyCounter, //15
	SAA1099PWM_DummyCounter, //16
	SAA1099PWM_DummyCounter, //17
	SAA1099PWM_DummyCounter, //18
	SAA1099PWM_DummyCounter, //19
	SAA1099PWM_DummyCounter, //1A
	SAA1099PWM_DummyCounter, //1B
	SAA1099PWM_DummyCounter, //1C
	SAA1099PWM_DummyCounter, //1D
	SAA1099PWM_FinalCounterLeft, //1E
	SAA1099PWM_FinalCounterRight //1F
};

OPTINLINE int_32 getSAA1099PWM(SAA1099 *chip, byte channel, byte output, PWMOUTPUT *PWM)
{
	INLINEREGISTER byte counter,check;
	check = counter = (PWM->PWMCounter++&0xF); //Apply the current counter! Reset every 16 pulses to generate a 16-level PWM!
	++check; //Increase check!
	check &= 0xF; //Only 15 positions to check!
	if (unlikely(check<=1)) //Are we to take action?
	{
		SAA1099PWMCounters[(counter<<1)|(output&AMPENV_RESULT_RIGHTCHANNEL)](chip,channel,output,PWM); //Handle special pulse states for the counter!
	}
	#ifdef PWM_OUTPUT
	return PWM->waveforminput&*PWM->waveform++; //Give the proper output as a 16-bit sample! Toggle positive/negative and 0V on the waveform! Toggling opposite will result in partially inverted waveforms(volumes 0-~7), so toggle to 0V (collector) instead.
	#else
	return PWM->waveforminput; //Give the proper output as a simple pre-defined 16-bit sample! No actual waveform is used for non-PWM output!
	#endif
}

OPTINLINE void generateSAA1099channelsample(SAA1099 *chip, byte channel, int_32 *output_l, int_32 *output_r)
{
	byte output;
	channel &= 7;
	chip->channels[channel].level = getSAA1099SquareWave(chip,channel); //Current flipflop output of the square wave generator!

	//Tick the envelopes when needed(after taking the last sample from the set of channels that use it)!
	if (unlikely((channel==1) && (chip->env_clock[0]==0)))
		tickSAAEnvelope(chip,0);
	if (unlikely((channel==4) && (chip->env_clock[1]==0)))
		tickSAAEnvelope(chip,1);

	output = chip->noise[chip->channels[channel].noisechannel].level; //Noise output?
	output |= chip->channels[channel].level; //Level is always 1-bit!
	//Retrieve the PWM sample to render!
	*output_l += getSAA1099PWM(chip,channel,output,&chip->channels[channel].PWMOutput[0]); //Output left!
	*output_r += getSAA1099PWM(chip,channel,output|AMPENV_INPUT_RIGHTCHANNEL,&chip->channels[channel].PWMOutput[1]); //Output right!
}

OPTINLINE void tickSAA1099noise(SAA1099 *chip, byte channel)
{
	byte noise_flipflop;

	channel &= 1; //Only two channels!

	//Check the current noise generators and update them!
	//Noise channel output!
	noise_flipflop = getSAA1099SquareWave(chip,channel|8); //Current flipflop output of the noise timer!
	if (unlikely((noise_flipflop ^ chip->noise[channel].laststatus) && (noise_flipflop==0))) //Half-wave switched state? We're to update the noise output!
	{
		/*
			SAA1099P noise generator as documented by Jepael
			18-bit Galois LFSR
			Feedback polynomial = x^18 + x^11 + x^1
			Period = 2^18-1 = 262143 bits
			Verified to match recorded noise from my SAA1099P
		*/

		if (unlikely(chip->noise[channel].lfsr&1))
		{
			chip->noise[channel].lfsr = (chip->noise[channel].lfsr>>1)^0x20400;
			chip->noise[channel].level = AMPENV_INPUT_NOISEOUTPUT; //Level 1! Current level bit has been updated, preshifted to bit 2 of the output!
		}
		else
		{
			chip->noise[channel].lfsr = (chip->noise[channel].lfsr>>1);
			chip->noise[channel].level = 0; //Level 0!
		}
	}
	chip->noise[channel].laststatus = noise_flipflop; //Save the last status!
}

OPTINLINE void generateSAA1099sample(SAA1099 *chip, int_32 *leftsample, int_32 *rightsample) //Generate a sample on the requested chip!
{
	int_32 output_l, output_r;

	output_l = output_r = 0; //Reset the output!
	generateSAA1099channelsample(chip,0,&output_l,&output_r); //Channel 0 sample!
	generateSAA1099channelsample(chip,1,&output_l,&output_r); //Channel 1 sample!
	generateSAA1099channelsample(chip,2,&output_l,&output_r); //Channel 2 sample!
	generateSAA1099channelsample(chip,3,&output_l,&output_r); //Channel 3 sample!
	generateSAA1099channelsample(chip,4,&output_l,&output_r); //Channel 4 sample!
	generateSAA1099channelsample(chip,5,&output_l,&output_r); //Channel 5 sample!

	//Give the result, before doing other things!
	*leftsample = output_l; //Left sample result!
	*rightsample = output_r; //Right sample result!

	//Finally, write the resultant samples to the result!
	tickSAA1099noise(chip,0); //Tick first noise channel!
	tickSAA1099noise(chip,1); //Tick second noise channel!
}

uint_32 gameblaster_soundtiming=0;
uint_32 gameblaster_rendertiming=0;

DOUBLE gameblaster_output_ticktiming; //Both current clocks!
DOUBLE gameblaster_output_tick = 0.0; //Time of a tick in the PC speaker sample!

DOUBLE gameblaster_ticklength = 0.0; //Length of PIT samples to process every output sample!

int_32 gb_leftsample[2], gb_rightsample[2]; //Two stereo samples!

void updateGameBlaster(DOUBLE timepassed, uint_32 MHZ14passed)
{
	//Output rendering information:
	INLINEREGISTER uint_32 length; //Amount of samples to generate!
	INLINEREGISTER uint_32 i;
	uint_32 dutycyclei; //Input samples to process!
	DOUBLE tempf;
	uint_32 render_ticks; //A one shot tick!
	int_32 currentsamplel,currentsampler; //Saved sample in the 1.19MHz samples!
	float filtersamplel, filtersampler;

	if (GAMEBLASTER.baseaddr==0) return; //No game blaster?
	//Game Blaster sound output
	gameblaster_soundtiming += MHZ14passed; //Get the amount of time passed!
	if (unlikely(gameblaster_soundtiming>=MHZ14_BASETICK))
	{
		for (;gameblaster_soundtiming>=MHZ14_BASETICK;)
		{
			//Generate the sample!

			gb_leftsample[0] = gb_rightsample[0] = 0; //No sample by default!
			if (likely(GAMEBLASTER.chips[0].all_ch_enable)) //Sound generation of first chip?
			{
				generateSAA1099sample(&GAMEBLASTER.chips[0],&gb_leftsample[0],&gb_rightsample[0]); //Generate a stereo sample on this chip!
			}

			gb_leftsample[1] = gb_rightsample[1] = 0; //No sample by default!
			if (likely(GAMEBLASTER.chips[1].all_ch_enable)) //Sound generation of first chip?
			{
				generateSAA1099sample(&GAMEBLASTER.chips[1], &gb_leftsample[1], &gb_rightsample[1]); //Generate a stereo sample on this chip!
			}

			gameblaster_soundtiming -= MHZ14_BASETICK; //Decrease timer to get time left!

			#ifdef LOG_GAMEBLASTER
			if (GAMEBLASTER_LOG) //Logging output?
			{
				writeWAVStereoSample(GAMEBLASTER_LOG,signed2unsigned16((sword)(gb_leftsample[0]*AMPLIFIER)),signed2unsigned16((sword)(gb_rightsample[0]*AMPLIFIER)));
				writeWAVStereoSample(GAMEBLASTER_LOG,signed2unsigned16((sword)(gb_leftsample[1]*AMPLIFIER)),signed2unsigned16((sword)(gb_rightsample[1]*AMPLIFIER)));
			}
			#endif

			//Load and mix the sample to render!
			i = gb_leftsample[0]; //Load left sample!
			i += gb_leftsample[1]; //Mix left sample!
			length = gb_rightsample[0]; //Load right sample!
			length += gb_rightsample[1]; //Mix right sample!

			writefifobuffer32_2(GAMEBLASTER.rawsignal,i,length); //Save the raw signal for post-processing!
		}
	}

	//PC speaker output!
	gameblaster_output_ticktiming += timepassed; //Get the amount of time passed for the PC speaker (current emulated time passed according to set speed)!
	if (unlikely(gameblaster_output_ticktiming >= gameblaster_output_tick)) //Enough time passed to render the physical PC speaker and enabled?
	{
		length = (uint_32)floor(SAFEDIV(gameblaster_output_ticktiming, gameblaster_output_tick)); //How many ticks to tick?
		gameblaster_output_ticktiming -= (length*gameblaster_output_tick); //Rest the amount of ticks!

		//Ticks the speaker when needed!
		i = 0; //Init counter!
		//Generate the samples from the output signal!
		for (;;) //Generate samples!
		{
			//Average our input ticks!
			GAMEBLASTER.samplesleft += gameblaster_ticklength; //Add our time to the sample time processed!
			tempf = floor(GAMEBLASTER.samplesleft); //Take the rounded number of samples to process!
			GAMEBLASTER.samplesleft -= tempf; //Take off the samples we've processed!
			render_ticks = (uint_32)tempf; //The ticks to render!

			filtersamplel = filtersampler = 0.0f; //Initialize the filter to be sure!
			//render_ticks contains the output samples to process! Calculate the duty cycle by low pass filter and use it to generate a sample!
			for (dutycyclei = render_ticks;dutycyclei;--dutycyclei)
			{
				if (unlikely(readfifobuffer32_2(GAMEBLASTER.rawsignal, &currentsamplel, &currentsampler)==0)) break; //Failed to read the sample? Stop counting!
				//We're applying the low pass filter for the output!
				filtersamplel = (float)currentsamplel; //Convert to filter format!
				filtersampler = (float)currentsampler; //Convert to filter format!
				#ifdef FILTER_SIGNAL
				//Enable filtering when defined!
				applySoundLowPassFilter(&GAMEBLASTER.filter[0], &filtersamplel);
				applySoundLowPassFilter(&GAMEBLASTER.filter[1], &filtersampler);
				#endif
			}

			filtersamplel *= AMPLIFIER; //Amplify!
			filtersampler *= AMPLIFIER; //Amplify!
			//Add the result to our buffer!
			writeDoubleBufferedSound32(&GAMEBLASTER.soundbuffer,(signed2unsigned16((sword)LIMITRANGE(filtersampler, SHRT_MIN, SHRT_MAX))<<16)|signed2unsigned16((sword)LIMITRANGE(filtersamplel, SHRT_MIN, SHRT_MAX))); //Output the sample to the renderer!
			++i; //Add time!
			if (unlikely(i == length)) //Fully rendered?
			{
				return; //Next item!
			}
		}
	}
}

byte GameBlaster_soundGenerator(void* buf, uint_32 length, byte stereo, void *userdata) //Generate a sample!
{
	uint_32 c;
	c = length; //Init c!
	
	static uint_32 last=0;
	INLINEREGISTER uint_32 buffer;

	SOUNDDOUBLEBUFFER *doublebuffer = (SOUNDDOUBLEBUFFER *)userdata; //Our double buffered sound input to use!
	int_32 mono_converter;
	sample_stereo_p data_stereo;
	sword *data_mono;
	if (stereo) //Stereo processing?
	{
		data_stereo = (sample_stereo_p)buf; //The data in correct samples!
		for (;;) //Fill it!
		{
			//Left and right samples are the same: we're a mono signal!
			readDoubleBufferedSound32(doublebuffer,&last); //Generate a stereo sample if it's available!
			buffer = last; //Load the last sample for processing!
			data_stereo->l = unsigned2signed16((word)buffer); //Load the last generated sample(left)!
			buffer >>= 16; //Shift low!
			data_stereo->r = unsigned2signed16((word)buffer); //Load the last generated sample(right)!
			++data_stereo; //Next stereo sample!
			if (!--c) return SOUNDHANDLER_RESULT_FILLED; //Next item!
		}
	}
	else //Mono processing?
	{
		data_mono = (sword *)buf; //The data in correct samples!
		for (;;) //Fill it!
		{
			//Left and right samples are the same: we're a mono signal!
			readDoubleBufferedSound32(doublebuffer,&last); //Generate a stereo sample if it's available!
			buffer = last; //Load the last sample for processing!
			mono_converter = unsigned2signed16((word)buffer); //Load the last generated sample(left)!
			buffer >>= 16; //Shift low!
			mono_converter += unsigned2signed16((word)buffer); //Load the last generated sample(right)!
			mono_converter = LIMITRANGE(mono_converter, SHRT_MIN, SHRT_MAX); //Clip our data to prevent overflow!
			*data_mono++ = mono_converter; //Save the sample and point to the next mono sample!
			if (!--c) return SOUNDHANDLER_RESULT_FILLED; //Next item!
		}
	}
}

byte outGameBlaster(word port, byte value)
{
	if (__HW_DISABLED) return 0; //We're disabled!
	if ((port&~0xF)!=GAMEBLASTER.baseaddr) return 0; //Not Game Blaster port!
	switch (port&0xF)
	{
		case 0: //Left SAA-1099?
			#ifdef LOG_GAMEBLASTER
			if (!GAMEBLASTER_LOG) GAMEBLASTER_LOG = createWAV("captures/gameblaster.wav",4,(uint_32)__GAMEBLASTER_BASERATE); //Create a wave file at our rate!
			#endif
			writeSAA1099Value(&GAMEBLASTER.chips[0],value); //Write value!
			return 1; //Handled!
		case 1: //Left SAA-1099?
			#ifdef LOG_GAMEBLASTER
			if (!GAMEBLASTER_LOG) GAMEBLASTER_LOG = createWAV("captures/gameblaster.wav",4,(uint_32)__GAMEBLASTER_BASERATE); //Create a wave file at our rate!
			#endif
			writeSAA1099Address(&GAMEBLASTER.chips[0],value); //Write address!
			return 1; //Handled!
		case 2: //Right SAA-1099?
			#ifdef LOG_GAMEBLASTER
			if (!GAMEBLASTER_LOG) GAMEBLASTER_LOG = createWAV("captures/gameblaster.wav",4,(uint_32)__GAMEBLASTER_BASERATE); //Create a wave file at our rate!
			#endif
			writeSAA1099Value(&GAMEBLASTER.chips[1],value); //Write value!
			return 1; //Handled!
		case 3: //Right SAA-1099?
			#ifdef LOG_GAMEBLASTER
			if (!GAMEBLASTER_LOG) GAMEBLASTER_LOG = createWAV("captures/gameblaster.wav",4,(uint_32)__GAMEBLASTER_BASERATE); //Create a wave file at our rate!
			#endif
			writeSAA1099Address(&GAMEBLASTER.chips[1],value); //Write address!
			return 1; //Handled!
		default: //Other addresses(16 addresses)? CT-1302!
			if (GAMEBLASTER.soundblastercompatible>1) return 0; //Ignore all other addresses!
			switch (port&0xF) //What port?
			{
				case 6: //Store 1!
				case 7: //Store 2!
					GAMEBLASTER.storelatch[port&1] = value; //Store/latch!
					return 1; //Handled!
				default:
					break;
			}
			return 0; //Not handled yet!
			break;
	}
	return 0; //Not handled!
}

byte inGameBlaster(word port, byte *result)
{
	if (__HW_DISABLED) return 0; //We're disabled!
	if ((port&~0xF)!=GAMEBLASTER.baseaddr) return 0; //Not Game Blaster port!
	switch (port&0xF)
	{
		case 0: //Left SAA-1099?
		case 1: //Left SAA-1099?
		case 2: //Right SAA-1099?
		case 3: //Right SAA-1099?
			return 0; //Not Handled! The chips cannot be read, only written!
		default: //Other addresses(16 addresses)? CT-1302!
			if (GAMEBLASTER.soundblastercompatible>1) return 0; //Ignore all other addresses!
			switch (port&0xF) //What port?
			{
				case 0x4: //Detection!
					*result = 0x7F; //Give the detection value!
					return 1; //Handled!					
				case 0xA: //Store 1!
					if (GAMEBLASTER.soundblastercompatible) return 0; //Sound blaster compatibility?
				case 0xB: //Store 2!
					*result = GAMEBLASTER.storelatch[port&1]; //Give the store/latch!
					return 1; //Handled!
				default:
					break;
			}
			return 0; //Not handled yet!
			break;
	}
}

void setGameBlaster_SoundBlaster(byte useSoundBlasterIO)
{
	if (__HW_DISABLED) return; //We're disabled!
	GAMEBLASTER.soundblastercompatible = useSoundBlasterIO?1:0; //Sound Blaster compatible I/O? Use 2 chips only with sound blaster, else full 16 ports for detection!
}

void GameBlaster_setVolume(float volume)
{
	if (__HW_DISABLED) return; //We're disabled!
	setVolume(&GameBlaster_soundGenerator,&GAMEBLASTER.soundbuffer, volume); //Set the volume!
}

void initGameBlaster(word baseaddr)
{
	uint_32 i;
	byte channel;
	if (__HW_DISABLED) return; //We're disabled!
	memset(&GAMEBLASTER,0,sizeof(GAMEBLASTER)); //Full init!
	GAMEBLASTER.baseaddr = baseaddr; //Base address of the Game Blaster!
	setGameBlaster_SoundBlaster(0); //Default to Game Blaster I/O!

	GAMEBLASTER.rawsignal = allocfifobuffer((((uint_64)((2048.0f / __GAMEBLASTER_SAMPLERATE)*__GAMEBLASTER_BASERATE)) + 1)<<3, 0); //Nonlockable FIFO with 2048 word-sized samples with lock (TICK_RATE)!

	if (allocDoubleBufferedSound32(__GAMEBLASTER_SAMPLEBUFFERSIZE,&GAMEBLASTER.soundbuffer,0,__GAMEBLASTER_SAMPLERATE)) //Valid buffer?
	{
		if (!addchannel(&GameBlaster_soundGenerator,&GAMEBLASTER.soundbuffer,"GameBlaster",(float)__GAMEBLASTER_SAMPLERATE,__GAMEBLASTER_SAMPLEBUFFERSIZE,1,SMPL16S,1)) //Start the sound emulation (mono) with automatic samples buffer?
		{
			dolog("GameBlaster","Error registering sound channel for output!");
		}
		else
		{
			setVolume(&GameBlaster_soundGenerator,&GAMEBLASTER.soundbuffer,__GAMEBLASTER_VOLUME);
		}
	}
	else
	{
		dolog("GameBlaster","Error registering first double buffer for output!");
	}
	//Ignore unregistered channel, we need to be used by software!
	register_PORTIN(&inGameBlaster); //Input ports!
	//All output!
	register_PORTOUT(&outGameBlaster); //Output ports!

	GAMEBLASTER.storelatch[0] = GAMEBLASTER.storelatch[1] = 0xFF; //Initialise our latches!

	#ifdef IS_LONGDOUBLE
	gameblaster_output_tick = (1000000000.0L / (DOUBLE)__GAMEBLASTER_SAMPLERATE); //Speaker tick!
	gameblaster_ticklength = (1.0L / __GAMEBLASTER_SAMPLERATE)*__GAMEBLASTER_BASERATE; //Time to speaker sample ratio!
	#else
	gameblaster_output_tick = (1000000000.0 / (DOUBLE)__GAMEBLASTER_SAMPLERATE); //Speaker tick!
	gameblaster_ticklength = (1.0 / __GAMEBLASTER_SAMPLERATE)*__GAMEBLASTER_BASERATE; //Time to speaker sample ratio!
	#endif

	AMPLIFIER = (float)__GAMEBLASTER_AMPLIFIER; //Set the amplifier to use!
	GAMEBLASTER.baseclock = (uint_32)(MHZ14/2); //We're currently clocking at the sample rate!
	#ifdef IS_LONGDOUBLE
	noise_frequencies[0] = (float)((float)GAMEBLASTER.baseclock/(256.0L)); //~13982.xxxHz
	noise_frequencies[1] = (float)((float)GAMEBLASTER.baseclock/(512.0L));
	noise_frequencies[2] = (float)((float)GAMEBLASTER.baseclock/(1024.0L));
	#else
	noise_frequencies[0] = (float)((float)GAMEBLASTER.baseclock/(256.0)); //~13982.xxxHz
	noise_frequencies[1] = (float)((float)GAMEBLASTER.baseclock/(512.0));
	noise_frequencies[2] = (float)((float)GAMEBLASTER.baseclock/(1024.0));
	#endif

	initSoundFilter(&GAMEBLASTER.filter[0],0,(float)(__GAMEBLASTER_SAMPLERATE/2.0),(float)__GAMEBLASTER_BASERATE); //Low-pass filter used left at nyquist!
	initSoundFilter(&GAMEBLASTER.filter[1],0,(float)(__GAMEBLASTER_SAMPLERATE/2.0),(float)__GAMEBLASTER_BASERATE); //Low-pass filter used right at nyquist!
	
	/*

	Test values!

	*/
#ifdef TESTWAVE
	//Load test wave information for generating samples!
	GAMEBLASTER.chips[0].squarewave[7].timeout = (uint_32)(__GAMEBLASTER_BASERATE/(DOUBLE)(440.0f*2.0f)); //New timeout!
	GAMEBLASTER.chips[0].squarewave[7].timepoint = 0; //Reset!
	GAMEBLASTER.chips[0].squarewave[7].freq = 440.0f; //We're updated!

	WAVEFILE *testoutput=NULL;

	byte signal;

	testoutput = createWAV("captures/testgameblaster440hz.wav",1,(uint_32)__GAMEBLASTER_BASERATE); //Start the log!

	for (i=0;i<__GAMEBLASTER_BASERATE;++i) //Generate one second of data!
	{
		signal = getSAA1099SquareWave(&GAMEBLASTER.chips[0],7);
		writeWAVMonoSample(testoutput,signed2unsigned16(signal?(sword)32767:(sword)-32768)); //Write a sample!
	}

	closeWAV(&testoutput); //Close the wave file!
#endif
	//End test

	for (channel=0;channel<8;++channel) //Init all channels, when needed!
	{
		updateSAA1099frequency(&GAMEBLASTER.chips[0],channel); //Init frequency!
		updateSAA1099frequency(&GAMEBLASTER.chips[1],channel); //Init frequency!
		GAMEBLASTER.chips[0].channels[channel].noisechannel = GAMEBLASTER.chips[1].channels[channel].noisechannel = (channel/3); //Our noise channel linked to this channel!
		GAMEBLASTER.chips[0].channels[channel].activeampenv = GAMEBLASTER.chips[1].channels[channel].activeampenv = 0xFF; //Default to need to be updating the amplitude envelope!
	}
	updateSAA1099RNGfrequency(&GAMEBLASTER.chips[0],0); //Init frequency!
	updateSAA1099RNGfrequency(&GAMEBLASTER.chips[1],0); //Init frequency!
	updateSAA1099RNGfrequency(&GAMEBLASTER.chips[0],1); //Init frequency!
	updateSAA1099RNGfrequency(&GAMEBLASTER.chips[1],1); //Init frequency!

	GAMEBLASTER.chips[0].noise[0].lfsr = GAMEBLASTER.chips[0].noise[1].lfsr = 1; //Make sure the LFSR is properly initialized!
	GAMEBLASTER.chips[1].noise[0].lfsr = GAMEBLASTER.chips[1].noise[1].lfsr = 1; //Make sure the LFSR is properly initialized!

	calcAmpEnvPrecalcs(); //Calculate the AmpEnv precalcs!

	gameblaster_rendertiming = gameblaster_soundtiming = 0; //Reset rendering!

	for (i=0;i<0x10;++i)
	{
		amplitudes[i] = calcAmplitude(i); //Possible amplitudes, for easy lookup!
	}

	for (channel=0;channel<8;++channel)
	{
		for (i=0;i<2;++i)
		{
			GAMEBLASTER.chips[0].channels[channel].PWMOutput[i].waveform = GAMEBLASTER.chips[1].channels[channel].PWMOutput[i].waveform = &WAVEFORM_OUTPUT[0][0]; //Load the first waveform by default!
		}
	}

	for (i=0;i<(8*0x40);++i) //8 waveforms, 64 positions, rounded to 256 positions!
	{
		precalcSAAenvelopes[i] = SAAEnvelope((i>>6),(i&0x3F)); //Precalculate the entire waveforms!
	}

	#ifdef DEBUG_OUTPUT
	//manually set a test frequency!
	GAMEBLASTER.chips[0].squarewave[0].timeout = (uint_32)(__GAMEBLASTER_BASERATE/(DOUBLE)(2.0*DEBUG_OUTPUT)); //New timeout!
	GAMEBLASTER.chips[0].squarewave[0].timepoint = 0; //Reset!
	GAMEBLASTER.chips[0].channels[0].freq = GAMEBLASTER.chips[0].squarewave[0].freq = DEBUG_OUTPUT; //We're updated!
	GAMEBLASTER.chips[0].squarewave[8].timeout = (uint_32)(__GAMEBLASTER_BASERATE/(DOUBLE)(2.0*DEBUG_OUTPUT)); //New timeout!
	GAMEBLASTER.chips[0].squarewave[8].timepoint = 0; //Reset!
	GAMEBLASTER.chips[0].squarewave[8].freq = DEBUG_OUTPUT; //We're updated!
	outGameBlaster(GAMEBLASTER.baseaddr+1,0x00); //Channel 0 amplitude!
	outGameBlaster(GAMEBLASTER.baseaddr,0xFF); //Maximum amplitude!
	outGameBlaster(GAMEBLASTER.baseaddr+1,0x18); //Channel 0-3 settings!
	outGameBlaster(GAMEBLASTER.baseaddr,0x82); //Enable frequency output at full volume!
	outGameBlaster(GAMEBLASTER.baseaddr+1,0x1C); //General settings!
	outGameBlaster(GAMEBLASTER.baseaddr,0x01); //Enable all outputs!
	outGameBlaster(GAMEBLASTER.baseaddr+1,0x14); //Channel n frequency!
	outGameBlaster(GAMEBLASTER.baseaddr,0x01); //Enable frequency output!
	outGameBlaster(GAMEBLASTER.baseaddr+1,0x15); //Channel n frequency!
	outGameBlaster(GAMEBLASTER.baseaddr,0x01); //Enable noise output!
	outGameBlaster(GAMEBLASTER.baseaddr+1,0x16); //Channel n frequency!
	outGameBlaster(GAMEBLASTER.baseaddr,0x03); //Set noise output mode!
	#endif
}

void doneGameBlaster()
{
	if (GAMEBLASTER_LOG) closeWAV(&GAMEBLASTER_LOG); //Close our log, if logging!
	removechannel(&GameBlaster_soundGenerator,&GAMEBLASTER.soundbuffer,0); //Stop the sound emulation?
	freeDoubleBufferedSound(&GAMEBLASTER.soundbuffer);
	free_fifobuffer(&GAMEBLASTER.rawsignal); //Release the FIFO buffer we use!
}
