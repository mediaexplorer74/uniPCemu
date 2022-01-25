
#include "..\commonemuframework\headers\types.h"//"headers/types.h" //Basic types!
#include "headers/hardware/midi/adsr.h" //Our own typedefs!
#include "headers/support/sf2.h" //Soundfont support!
#include "..\commonemuframework\headers\emu\sound.h" //"headers/emu/sound.h" //dB support!
#include "..\commonemuframework\headers\support\signedness.h" //"headers/support/signedness.h" //Sign conversion support!
#include "headers/hardware/midi/mididevice.h" //MIDI attenuation support!

//16/32 bit quantities from the SoundFont loaded in memory!
#define LE16(x) SDL_SwapLE16(x)
#define LE32(x) SDL_SwapLE32(x)
#define LE16S(x) unsigned2signed16(LE16(signed2unsigned16(x)))
#define LE32S(x) unsigned2signed32(LE32(signed2unsigned32(x)))

//ADSR itself:

OPTINLINE float ADSR_release(ADSR *adsr, int_64 play_counter, byte sustaining, byte release_velocity)
{
	adsr->active = ADSR_RELEASE; //We're releasing!
	if (adsr->release && adsr->releasefactor && adsr->releaselevel) //Gotten release and a factor to apply?
	{
		float result;
		result = adsr->releaselevel - (adsr->releasefactor*(play_counter - adsr->releasestart)); //Apply factor!
		if (result>0.0f) return result; //Not quiet yet?
	}
	if (!adsr->released) //Not noted yet?
	{
		adsr->releasedstart = play_counter;
		adsr->released = 1; //We've released!
	}
	adsr->active = ADSR_IDLE; //We're idle!
	return 0.0f; //Nothing to sound!
}

OPTINLINE float enterRelease(ADSR *adsr, int_64 play_counter, byte release_velocity, float releaselevel)
{
	//Calculate the release information
	if (!adsr->releasestarted)
	{
		if (adsr->release) //Gotten a release phase?
		{
			float releasefactor = releaselevel; //From full volume currently at!
			releasefactor /= adsr->release; //Equal steps from full to 0.0f!
			adsr->releasefactor = releasefactor; //Apply the release factor for the current volume!
		}

		adsr->releasestart = play_counter; //When we start to release!
		adsr->releaselevel = releaselevel; //The level at this point!
		adsr->releasestarted = 1; //We've started!
	}
	return ADSR_release(adsr,play_counter, 0, release_velocity); //Passthrough!
}

OPTINLINE float ADSR_sustain(ADSR *adsr, int_64 play_counter, byte sustaining, byte release_velocity)
{
	adsr->active = ADSR_SUSTAIN; //We're sustaining!
	if ((!(sustaining | adsr->releasestarted)) || (adsr->releasestarted && (adsr->releasestart <= play_counter))) //Finished playing at this point?
		return enterRelease(adsr, play_counter, release_velocity, adsr->sustainfactor); //Enter the release phase!

	return adsr->sustainfactor; //Disable our voice when not sustaining anymore or sustain is unsupported!
}

OPTINLINE float ADSR_decay(ADSR *adsr, int_64 play_counter, byte sustaining, byte release_velocity)
{
	float result;
	adsr->active = ADSR_DECAY; //We're decaying!
	if ((!(sustaining | adsr->releasestarted)) || (adsr->releasestarted && (adsr->releasestart <= play_counter))) //Finished playing at this point?
	{
		return enterRelease(adsr,play_counter, release_velocity, 1.0f - (adsr->decayfactor*(play_counter - adsr->decaystart))); //Enter the release phase!
	}
	if (adsr->decay) //Gotten decay?
	{
		if (adsr->decayend > play_counter) //Decay busy?
		{
			result = 1.0f - (adsr->decayfactor*(play_counter - adsr->decaystart)); //Apply factor!
			if (result>adsr->sustainfactor) return result; //Decay busy!
		}
	}
	//Decay expired?
	if (!adsr->sustainstarted)
	{
		adsr->sustainstart = play_counter; //Start of the attack phase!
		adsr->sustainstarted = 1; //We've started!
	}
	return ADSR_sustain(adsr,play_counter, sustaining, release_velocity); //Passthrough!
}

OPTINLINE float ADSR_hold(ADSR *adsr, int_64 play_counter, byte sustaining, byte release_velocity)
{
	adsr->active = ADSR_HOLD; //We're holding!
	if ((!(sustaining | adsr->releasestarted)) || (adsr->releasestarted && (adsr->releasestart <= play_counter))) //Finished playing at this point?
	{
		return enterRelease(adsr,play_counter, release_velocity,1.0f); //Enter the release phase!
	}
	if (adsr->hold) //Gotten hold?
	{
		if (adsr->holdend > play_counter) return 1.0f; //Hold busy?
	}
	//Hold expired?
	if (!adsr->decaystarted)
	{
		adsr->decaystart = play_counter; //Start of the attack phase!
		adsr->decaystarted = 1; //We've started!
	}
	return ADSR_decay(adsr, play_counter, sustaining, release_velocity); //Passthrough!
}

#define ATTACKFORMULA (adsr->attackfactor*(play_counter - adsr->attackstart))

//Attack curve is convex!
OPTINLINE float ADSR_attack(ADSR *adsr, int_64 play_counter, byte sustaining, byte release_velocity)
{
	float result;
	adsr->active = ADSR_ATTACK; //We're attacking!
	if ((!(sustaining | adsr->releasestarted)) || (adsr->releasestarted && (adsr->releasestart <= play_counter))) //Finished playing at this point?
	{
		return enterRelease(adsr, play_counter, release_velocity, adsr->attackisconvex?MIDIconvex((ATTACKFORMULA)):(ATTACKFORMULA)); //Enter the release phase!
	}
	if (adsr->attack) //Gotten attack?
	{
		if (adsr->attackend > play_counter) //Attack busy?
		{
			result = adsr->attackisconvex?MIDIconvex(ATTACKFORMULA):(ATTACKFORMULA); //Apply factor! Is convex can be applied too!
			if (result < 1.0f) return result; //Not full yet?
		}
	}
	//Attack expired?
	if (!adsr->holdstarted)
	{
		adsr->holdstart = play_counter; //Start of the attack phase!
		adsr->holdstarted = 1; //We've started!
	}
	return ADSR_hold(adsr, play_counter, sustaining, release_velocity); //Passthrough!
}

OPTINLINE float ADSR_delay(ADSR *adsr, int_64 play_counter, byte sustaining, byte release_velocity)
{
	adsr->active = ADSR_DELAY; //We're starting with a delay!
	if ((!(sustaining | adsr->releasestarted)) || (adsr->releasestarted && (adsr->releasestart <= play_counter))) //Finished playing at this point?
	{
		return enterRelease(adsr, play_counter, release_velocity, (adsr->attackfactor * (play_counter - adsr->attackstart))); //Enter the release phase!
	}
	if (adsr->delay) //Gotten delay?
	{
		if (adsr->delay > play_counter) return 0.0f; //Delay busy?
	}
	if (!adsr->attackstarted)
	{
		adsr->attackstart = play_counter; //Start of the attack phase!
		adsr->attackstarted = 1; //We've started!
	}
	return ADSR_attack(adsr,play_counter,sustaining,release_velocity); //Passthrough!
}

void ADSR_init(void *voice, float sampleRate, byte velocity, ADSR *adsr, RIFFHEADER *soundfont, word instrumentptrAmount, word ibag, uint_32 preset, word pbag, word delayLookup, word attackLookup, byte attackisconvex, word holdLookup, word decayLookup, word sustainLookup, word releaseLookup, byte keynum, word keynumToEnvHoldLookup, word keynumToEnvDecayLookup) //Initialise an ADSR!
{
	sfGenList applypgen;
	sfInstGenList applyigen;
	MIDIDEVICE_VOICE* thevoice;
	thevoice = (MIDIDEVICE_VOICE*)voice; //The voice to use!

//Volume envelope information!
	int_32 delaysetting, attack, hold, decay, sustain, release; //All lengths!
	uint_32 delaylength, attacklength, holdlength, decaylength, releaselength; //All lengths!
	float attackfactor, decayfactor, sustainfactor, holdenvfactor, decayenvfactor;

	sword relKeynum;
	relKeynum = 60-(sword)keynum; //How far are we below 60?
	
//Delay
	delaysetting = -12000; //Default!
	if (lookupSFInstrumentGenGlobal(soundfont, instrumentptrAmount, ibag, delayLookup, &applyigen))
	{
		delaysetting = LE16S(applyigen.genAmount.shAmount); //Apply!
		if (lookupSFPresetGenGlobal(soundfont, preset, pbag, delayLookup, &applypgen)) //Preset set?
		{
			delaysetting += LE16S(applypgen.genAmount.shAmount); //Apply!
		}
	}
	else
	{
		if (lookupSFPresetGenGlobal(soundfont, preset, pbag, delayLookup, &applypgen)) //Preset set?
		{
			delaysetting = LE16S(applypgen.genAmount.shAmount); //Apply!
		}
	}

	delaysetting += getSFInstrumentmodulator(thevoice, delayLookup, 1, 0.0f, 0.0f); //Delay modulation!
	delaysetting += getSFPresetmodulator(thevoice, delayLookup, 1, 0.0f, 0.0f); //Delay modulation!

	//Attack
	attack = -12000; //Default!
	if (lookupSFInstrumentGenGlobal(soundfont, instrumentptrAmount, ibag, attackLookup, &applyigen))
	{
		attack = LE16S(applyigen.genAmount.shAmount); //Apply!
		if (lookupSFPresetGenGlobal(soundfont, preset, pbag, attackLookup, &applypgen)) //Preset set?
		{
			attack += LE16S(applypgen.genAmount.shAmount); //Apply!
		}
	}
	else
	{
		if (lookupSFPresetGenGlobal(soundfont, preset, pbag, attackLookup, &applypgen)) //Preset set?
		{
			attack = LE16S(applypgen.genAmount.shAmount); //Apply!
		}
	}

	attack += getSFInstrumentmodulator(thevoice, attackLookup, 1, 0.0f, 0.0f); //Attack modulation!
	attack += getSFPresetmodulator(thevoice, attackLookup, 1, 0.0f, 0.0f); //Attack modulation!

	//Hold
	hold = -12000; //Default!
	if (lookupSFInstrumentGenGlobal(soundfont, instrumentptrAmount, ibag, holdLookup, &applyigen))
	{
		hold = LE16S(applyigen.genAmount.shAmount); //Apply!
		if (lookupSFPresetGenGlobal(soundfont, preset, pbag, holdLookup, &applypgen)) //Preset set?
		{
			hold += LE16S(applypgen.genAmount.shAmount); //Apply!
		}
	}
	else
	{
		if (lookupSFPresetGenGlobal(soundfont, preset, pbag, holdLookup, &applypgen)) //Preset set?
		{
			hold = LE16S(applypgen.genAmount.shAmount); //Apply!
		}
	}

	hold += getSFInstrumentmodulator(thevoice, holdLookup, 1, 0.0f, 0.0f); //Hold modulation!
	hold += getSFPresetmodulator(thevoice, holdLookup, 1, 0.0f, 0.0f); //Hold modulation!

	//Hold factor
	holdenvfactor = 0; //Default!
	if (lookupSFInstrumentGenGlobal(soundfont, instrumentptrAmount, ibag, keynumToEnvHoldLookup, &applyigen))
	{
		holdenvfactor = LE16S(applyigen.genAmount.shAmount); //Apply!
		if (lookupSFPresetGenGlobal(soundfont, preset, pbag, keynumToEnvHoldLookup, &applypgen)) //Preset set?
		{
			holdenvfactor += LE16S(applypgen.genAmount.shAmount); //Apply!
		}
	}
	else
	{
		if (lookupSFPresetGenGlobal(soundfont, preset, pbag, keynumToEnvHoldLookup, &applypgen)) //Preset set?
		{
			holdenvfactor = LE16S(applypgen.genAmount.shAmount); //Apply!
		}
	}

	holdenvfactor += getSFInstrumentmodulator(thevoice, keynumToEnvHoldLookup, 1, 0.0f, 0.0f); //Hold modulation!
	holdenvfactor += getSFPresetmodulator(thevoice, keynumToEnvHoldLookup, 1, 0.0f, 0.0f); //Hold modulation!

	//Decay
	decay = -12000; //Default!
	if (lookupSFInstrumentGenGlobal(soundfont, instrumentptrAmount, ibag, decayLookup, &applyigen))
	{
		decay = LE16S(applyigen.genAmount.shAmount); //Apply!
		if (lookupSFPresetGenGlobal(soundfont, preset, pbag, decayLookup, &applypgen)) //Preset set?
		{
			decay += LE16S(applypgen.genAmount.shAmount); //Apply!
		}
	}
	else
	{
		if (lookupSFPresetGenGlobal(soundfont, preset, pbag, decayLookup, &applypgen)) //Preset set?
		{
			decay = LE16S(applypgen.genAmount.shAmount); //Apply!
		}
	}

	decay += getSFInstrumentmodulator(thevoice, decayLookup, 1, 0.0f, 0.0f); //Hold modulation!
	decay += getSFPresetmodulator(thevoice, decayLookup, 1, 0.0f, 0.0f); //Hold modulation!

	//Decay factor
	decayenvfactor = 0; //Default!
	if (lookupSFInstrumentGenGlobal(soundfont, instrumentptrAmount, ibag, keynumToEnvDecayLookup, &applyigen))
	{
		decayenvfactor = LE16S(applyigen.genAmount.shAmount); //Apply!
		if (lookupSFPresetGenGlobal(soundfont, preset, pbag, keynumToEnvDecayLookup, &applypgen)) //Preset set?
		{
			decayenvfactor += LE16S(applypgen.genAmount.shAmount); //Apply!
		}
	}
	else
	{
		if (lookupSFPresetGenGlobal(soundfont, preset, pbag, keynumToEnvDecayLookup, &applypgen)) //Preset set?
		{
			decayenvfactor = LE16S(applypgen.genAmount.shAmount); //Apply!
		}
	}

	decayenvfactor += getSFInstrumentmodulator(thevoice, keynumToEnvDecayLookup, 1, 0.0f, 0.0f); //Hold modulation!
	decayenvfactor += getSFPresetmodulator(thevoice, keynumToEnvDecayLookup, 1, 0.0f, 0.0f); //Hold modulation!

	//Sustain (dB)
	sustain = 0; //Default!
	if (lookupSFInstrumentGenGlobal(soundfont, instrumentptrAmount, ibag, sustainLookup, &applyigen))
	{
		sustain = LE16S(applyigen.genAmount.shAmount); //Apply!
		if (lookupSFPresetGenGlobal(soundfont, preset, pbag, sustainLookup, &applypgen)) //Preset set?
		{
			sustain += LE16S(applypgen.genAmount.shAmount); //Apply!
		}
	}
	else
	{
		if (lookupSFPresetGenGlobal(soundfont, preset, pbag, sustainLookup, &applypgen)) //Preset set?
		{
			sustain = LE16S(applypgen.genAmount.shAmount); //Apply!
		}
	}

	sustain += getSFInstrumentmodulator(thevoice, sustainLookup, 1, 0.0f, 0.0f); //Hold modulation!
	sustain += getSFPresetmodulator(thevoice, sustainLookup, 1, 0.0f, 0.0f); //Hold modulation!

	//Release
	release = -12000; //Default!
	if (lookupSFInstrumentGenGlobal(soundfont, instrumentptrAmount, ibag, releaseLookup, &applyigen))
	{
		release = LE16S(applyigen.genAmount.shAmount); //Apply!
		if (lookupSFPresetGenGlobal(soundfont, preset, pbag, releaseLookup, &applypgen)) //Preset set?
		{
			release += LE16S(applypgen.genAmount.shAmount); //Apply!
		}
	}
	else
	{
		if (lookupSFPresetGenGlobal(soundfont, preset, pbag, releaseLookup, &applypgen)) //Preset set?
		{
			release = LE16S(applypgen.genAmount.shAmount); //Apply!
		}
	}

	release += getSFInstrumentmodulator(thevoice, releaseLookup, 1, 0.0f, 0.0f); //Hold modulation!
	release += getSFPresetmodulator(thevoice, releaseLookup, 1, 0.0f, 0.0f); //Hold modulation!

	//Now, calculate the length of each interval, in samples.
	if (cents2samplesfactord((DOUBLE)delaysetting) < 0.001f) //0.0001 sec?
	{
		delaylength = 0; //No delay!
	}
	else
	{
		delaylength = (uint_32)(sampleRate*cents2samplesfactord((DOUBLE)delaysetting)); //Calculate the ammount of samples!
	}
	if (cents2samplesfactord((DOUBLE)attack) < 0.001f) //0.0001 sec?
	{
		attacklength = 0; //No attack!
	}
	else
	{
		attacklength = (uint_32)(sampleRate*cents2samplesfactord((DOUBLE)attack)); //Calculate the ammount of samples!
	}
	if (cents2samplesfactord((DOUBLE)hold) < 0.001f) //0.0001 sec?
	{
		holdlength = 0; //No hold!
	}
	else
	{
		holdlength = (uint_32)(sampleRate*cents2samplesfactord((DOUBLE)hold)); //Calculate the ammount of samples!
	}
	holdlength = (uint_32)(holdlength*cents2samplesfactord((DOUBLE)(holdenvfactor*relKeynum))); //Apply key number!

	if (cents2samplesfactord((DOUBLE)decay) < 0.001f) //0.0001 sec?
	{
		decaylength = 0; //No decay!
	}
	else
	{
		decaylength = (uint_32)(sampleRate*cents2samplesfactord((DOUBLE)decay)); //Calculate the ammount of samples!
	}
	decaylength = (uint_32)(decaylength*cents2samplesfactord((DOUBLE)(decayenvfactor*relKeynum))); //Apply key number!

	if (sustain > 1000) sustain = 1000; //Limit of 1000cB!
	sustainfactor = ((float)(1000-sustain)); //We're on a rate of 1000cB attenuation, normalized!
	sustainfactor *= 0.001f; //Normalized!

	if (cents2samplesfactord((DOUBLE)release) < 0.001f) //0.0001 sec?
	{
		releaselength = 0; //No release!
	}
	else
	{
		releaselength = (uint_32)(sampleRate*cents2samplesfactord((DOUBLE)release)); //Calculate the ammount of samples!
	}
	
	//Now calculate the steps for the envelope!
	//Delay does nothing!
	//Attack!
	if (attacklength) //Gotten attack?
	{
		attackfactor = 1.0f;
		attackfactor /= attacklength; //Equal steps from 0 to 1.0f!
		if (!attackfactor)
		{
			attacklength = 0; //No attack!
		}
	}
	else
	{
		attackfactor = 0.0f; //No attack factor!
	}
	//Hold does nothing!
	//Decay
	if (decaylength) //Gotten decay?
	{
		decayfactor = 1.0f; //From full!
		decayfactor /= decaylength; //Equal steps from 1.0f to 0.0f!
		if (!decayfactor) //No decay?
		{
			decaylength = 0; //No decay!
		}
		else
		{
			float temp;
			temp = 1.0f; //Full volume!
			temp -= sustainfactor; //Change to sustain factor difference!
			temp /= decaylength; //Calculate the new decay time needed to change to the sustain factor!
			decayfactor = temp; //Load the calculated decay time!
		}
	}
	else
	{
		decayfactor = 0.0f; //No decay!
	}
	//Sustain does nothing!

	//Apply ADSR to the voice!
	adsr->delay = delaylength; //Delay
	adsr->attack = attacklength; //Attack
	adsr->attackfactor = attackfactor;
	adsr->attackisconvex = attackisconvex; //Attack has convex curve?
	adsr->hold = holdlength; //Hold
	adsr->decay = decaylength; //Decay
	adsr->decayfactor = decayfactor;
	adsr->sustain = sustain; //Sustain
	adsr->sustainfactor = sustainfactor; //Sustain %
	adsr->release = releaselength; //Release

	//Finally calculate the actual values needed!
	adsr->attackend = adsr->attack + adsr->delay;
	adsr->holdend = adsr->hold + adsr->attackend;
	adsr->decayend = adsr->decay + adsr->holdend;
	adsr->active = ADSR_DELAY; //We're starting with a delay!

	adsr->attackstarted = adsr->holdstarted = adsr->decaystarted = adsr->sustainstarted = adsr->releasestarted = adsr->released = 0; //Nothing is started yet!
}

typedef void (*MIDI_STATE)(ADSR *adsr, int_64 play_counter, byte sustaining, byte release_velocity); //ADSR event handlers!

float ADSR_tick(ADSR *adsr, int_64 samplecounter, byte sustaining, float noteon_velocity, byte release_velocity) //Tick an ADSR!
{
	float result = 0.0f; //The result to apply!
	if (samplecounter < 0) return 0.0f; //Do not use invalid positions!
	if (adsr->released && (samplecounter >= adsr->releasedstart)) //Finished releasing?
	{
		result = 0.0f; //Finished phase!
	}
	else if (adsr->releasestarted && (samplecounter >= adsr->releasestart))
	{
		result = ADSR_release(adsr, samplecounter, sustaining, release_velocity); //Release phase!
	}
	else if (adsr->sustainstarted && (samplecounter >= adsr->sustainstart))
	{
		result = ADSR_sustain(adsr, samplecounter, sustaining, release_velocity); //Sustain phase!
	}
	else if (adsr->decaystarted && (samplecounter >= adsr->decaystart))
	{
		result = ADSR_decay(adsr, samplecounter, sustaining, release_velocity); //Decay phase!
	}
	else if (adsr->holdstarted && (samplecounter >= adsr->holdstart))
	{
		result = ADSR_hold(adsr, samplecounter, sustaining, release_velocity); //Hold phase!
	}
	else if (adsr->attackstarted && (samplecounter >= adsr->attackstart))
	{
		result = ADSR_attack(adsr, samplecounter, sustaining, release_velocity); //Attack phase!
	}
	else //Delay phase?
	{
		result = ADSR_delay(adsr, samplecounter, sustaining, release_velocity); //Delay phase!
	}
	return result; 
}
