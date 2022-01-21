// SF2
#ifndef SF2_H
#define SF2_H

#include "..\commonemuframework\headers\types.h" //"headers/types.h" //Basic types!

//RIFF IDs!

#define CKID_RIFF 0x46464952
#define CKID_LIST 0x5453494c

#define CKID_SFBK 0x6b626673
#define CKID_INFO 0x4f464e49
#define CKID_PDTA 0x61746470
#define CKID_IFIL 0x6c696669
#define CKID_INAM 0x4d414e49
#define CKID_PHDR 0x72646870

#define CKID_DLS  0x20534C44
#define CKID_COLH 0x686c6f63
#define CKID_VERS 0x73726576
#define CKID_LINS 0x736e696c
#define CKID_ICOP 0x504f4349
#define CKID_INS  0x20736e69
#define CKID_INSH 0x68736e69

//Our added RIFF IDs!


//Converter: http://www.dolcevie.com/js/converter.html
//Enter letters reversed! Hex normal! (tsil=list)

//Below all lower case!
#define CKID_SDTA 0x61746473
#define CKID_SMPL 0x6c706d73
#define CKID_SM24 0x34326d73
#define CKID_PBAG 0x67616270
#define CKID_PMOD 0x646f6d70
#define CKID_PGEN 0x6e656770
#define CKID_INST 0x74736e69
#define CKID_IBAG 0x67616269
#define CKID_IMOD 0x646f6d69
#define CKID_IGEN 0x6e656769
#define CKID_SHDR 0x72646873

/*
//Extra info (lower case)!
#define CKID_ISNG
#define CKID_IROM
#define CKID_IVER
//Rest info (upper case)!
#define CKID_ICRD
#define CKID_IENG
#define CKID_IPRD
#define CKID_ICOP
#define CKID_ICMT
#define CKID_ISFT
*/

/*

Tree of a soundfont RIFF (minimum required):

RIFF
	sfbk
		LIST
			INFO
				ifil
				isng
				INAM
			sdta
				smpl
				<sm24>
			pdta
				phdr
				pbag
				pmod
				pgen
				inst
				ibag
				imod
				igen
				shdr
*/



//Our patches to names!
#define DWORD uint_32
#define BYTE byte
#define CHAR sbyte
#define SHORT sword
#define WORD word

// Four-character code 
#define FOURCC uint_32

#include "..\commonemuframework\headers\packed.h"//"headers/packed.h" //We're packed!
typedef struct  PACKED { 
 FOURCC ckID; // A chunk ID identifies the type of data within the chunk. 
 DWORD ckSize; // The size of the chunk data in bytes, excluding any pad byte. 
 // DATA THAT FOLLOWS = The actual data plus a pad byte if req�d to word align. 
} RIFF_DATAENTRY;
#include "..\commonemuframework\headers\endpacked.h" //"headers/endpacked.h" //We're packed!

#include "..\commonemuframework\headers\packed.h"//"headers/packed.h" //We're packed!
typedef struct  PACKED { 
     FOURCC ckID; 
     DWORD ckSize; 
     FOURCC fccType;          // RIFF form type 
} RIFF_LISTENTRY; //LIST/RIFF
#include "..\commonemuframework\headers\endpacked.h" //"headers/endpacked.h" //We're packed!

typedef union
{
	RIFF_DATAENTRY *dataentry; //Data type!
	RIFF_LISTENTRY *listentry; //List type!
	void *voidentry; //Void type!
	byte *byteentry; //Byte type (same as void, but for visual c++)
} RIFF_ENTRY; //Data/List entry!

#include "..\commonemuframework\headers\packed.h"//"headers/packed.h" //We're packed!
typedef struct PACKED
 {
 #ifdef IS_BIG_ENDIAN
 BYTE byHi;
 BYTE byLo;
 #else
 BYTE byLo;
 BYTE byHi;
 #endif
 } rangesType;
#include "..\commonemuframework\headers\endpacked.h" //"headers/endpacked.h" //We're packed!

#include "..\commonemuframework\headers\packed.h"//"headers/packed.h" //We're packed!
typedef union PACKED
 { 
 rangesType ranges; 
 SHORT shAmount; 
 WORD wAmount;
 } genAmountType;
#include "..\commonemuframework\headers\endpacked.h" //"headers/endpacked.h" //We're packed!

#include "..\commonemuframework\headers\packed.h" //"headers/packed.h" //We're packed!
// RnD TODO
/*typedef enum PACKED
{
 monoSample = 1, 
 rightSample = 2, 
 leftSample = 4, 
 linkedSample = 8, 
 RomMonoSample = 0x8001, 
 RomRightSample = 0x8002, 
 RomLeftSample = 0x8004, 
 RomLinkedSample = 0x8008 
} SFSampleLink;
*/
#include "..\commonemuframework\headers\endpacked.h" //"headers/endpacked.h" //We're packed!

#include "..\commonemuframework\headers\packed.h" //#include "headers/packed.h" //We're packed!
typedef struct PACKED
{ 
 WORD wMajor; 
 WORD wMinor; 
} sfVersionTag;
#include "..\commonemuframework\headers\endpacked.h" //"headers/endpacked.h" //We're packed!

#include "..\commonemuframework\headers\packed.h" //#include "headers/packed.h" //We're packed!
typedef struct PACKED
{ 
 CHAR achPresetName[20]; 
 WORD wPreset; 
 WORD wBank; 
 WORD wPresetBagNdx; 
 DWORD dwLibrary; 
 DWORD dwGenre; 
 DWORD dwMorphology; 
} sfPresetHeader;
#include "..\commonemuframework\headers\endpacked.h" //"headers/endpacked.h" //We're packed!

#include "..\commonemuframework\headers\packed.h" //#include "headers/packed.h" //We're packed!
typedef struct PACKED
{ 
 WORD wGenNdx; 
 WORD wModNdx; 
} sfPresetBag;
#include "..\commonemuframework\headers\endpacked.h" //"headers/endpacked.h" //We're packed!

typedef word SFGenerator;
typedef word SFModulator;
typedef word SFTransform;

#include "..\commonemuframework\headers\packed.h" //#include "headers/packed.h" //We're packed!
typedef struct PACKED
{ 
 SFModulator sfModSrcOper; 
 SFGenerator sfModDestOper; 
 SHORT modAmount; 
 SFModulator sfModAmtSrcOper; 
 SFTransform sfModTransOper; 
} sfModList;
#include "..\commonemuframework\headers\endpacked.h" //"headers/endpacked.h" //We're packed!

#include "..\commonemuframework\headers\packed.h" //#include "headers/packed.h" //We're packed!
typedef struct PACKED
{ 
 SFGenerator sfGenOper; 
 genAmountType genAmount; 
} sfGenList;
#include "..\commonemuframework\headers\endpacked.h" //"headers/endpacked.h" //We're packed!

#include "..\commonemuframework\headers\packed.h" //#include "headers/packed.h" //We're packed!
typedef struct PACKED
{ 
 CHAR achInstName[20]; 
 WORD wInstBagNdx; 
} sfInst;
#include "..\commonemuframework\headers\endpacked.h" //"headers/endpacked.h" //We're packed!

#include "..\commonemuframework\headers\packed.h" //#include "headers/packed.h" //We're packed!
typedef struct PACKED
{ 
 WORD wInstGenNdx; 
 WORD wInstModNdx; 
} sfInstBag;
#include "..\commonemuframework\headers\endpacked.h" //"headers/endpacked.h" //We're packed!

#include "..\commonemuframework\headers\packed.h" //#include "headers/packed.h" //We're packed!
typedef struct PACKED
{ 
 SFGenerator sfGenOper; 
 genAmountType genAmount; 
} sfInstGenList;
#include "..\commonemuframework\headers\endpacked.h" //"headers/endpacked.h" //We're packed!

#include "..\commonemuframework\headers\packed.h" //#include "headers/packed.h" //We're packed!
typedef struct PACKED
{ 
 CHAR achSampleName[20]; 
 DWORD dwStart; 
 DWORD dwEnd; 
 DWORD dwStartloop; 
 DWORD dwEndloop; 
 DWORD dwSampleRate; 
 BYTE byOriginalPitch; 
 CHAR chPitchCorrection; 
 WORD wSampleLink; 
 WORD sfSampleType; 
} sfSample;
#include "..\commonemuframework\headers\endpacked.h" //"headers/endpacked.h" //We're packed!

enum sfGenerator
{
	startAddrsOffset = 0,		// instrument only
	endAddrsOffset = 1,			// instrument only
	startloopAddrsOffset = 2,	// instrument only
	endloopAddrsOffset = 3,		// instrument only
	startAddrsCoarseOffset = 4,	// instrument only
	modLfoToPitch = 5,
	vibLfoToPitch = 6,
	modEnvToPitch = 7,
	initialFilterFc = 8,
	initialFilterQ = 9,
	modLfoToFilterFc = 10,
	modEnvToFilterFc = 11,
	endAddrsCoarseOffset = 12,	// instrument only
	modLfoToVolume = 13,
	chorusEffectsSend = 15,
	reverbEffectsSend = 16,
	pan = 17,
	delayModLFO = 21,
	freqModLFO = 22,
	delayVibLFO = 23,
	freqVibLFO = 24,
	delayModEnv = 25,
	attackModEnv = 26,
	holdModEnv = 27,
	decayModEnv = 28,
	sustainModEnv = 29,
	releaseModEnv = 30,
	keynumToModEnvHold = 31,
	keynumToModEnvDecay = 32,
	delayVolEnv = 33,
	attackVolEnv = 34,
	holdVolEnv = 35,
	decayVolEnv = 36,
	sustainVolEnv = 37,
	releaseVolEnv = 38,
	keynumToVolEnvHold = 39,
	keynumToVolEnvDecay = 40,
	instrument = 41,			// preset only
	keyRange = 43,
	velRange = 44,
	startloopAddrsCoarseOffset = 45,	// instrument only
	overridingKeynum = 46,				// instrument only
	overridingVelocity = 47,				// instrument only
	initialAttenuation = 48,
	endloopAddrsCoarseOffset = 50,		// instrument only
	coarseTune = 51,
	fineTune = 52,
	sampleID = 53,
	sampleModes = 54,			// instrument only
	scaleTuning = 56,
	exclusiveClass = 57,		// instrument only
	overridingRootKey = 58,		// instrument only
	endOper = 60
};

//Our own defines!

/*
Different sample modes:
*/
#define GEN_SAMPLEMODES_NOLOOP 0
#define GEN_SAMPLEMODES_LOOP 1
#define GEN_SAMPLEMODES_NOLOOP2 2
#define GEN_SAMPLEMODES_LOOPUNTILDEPRESSDONE 3

//Don't pack this structure: we will need to speed up this part as much as possible for rendering speedup!
typedef struct
{
	uint_32 filesize; //The total filesize!

	//PCM lookup speedup!
	uint_32 pcmdata_size; //PCM data size, in entries!
	word *pcmdata_data; //PCM data itself!
	uint_32 pcm24data_size; //PCM 24-bit size!
	byte *pcm24data_data; //PCM 24-bit data itself!

	//Now, all required chunks in the file!
	RIFF_ENTRY rootentry; //Root (SFBK) entry!
	RIFF_ENTRY hydra; //Hydra block!
	RIFF_ENTRY phdr; //PHDR block!
	RIFF_ENTRY pbag; //PBAG block!
	RIFF_ENTRY pgen; //PGEN block!
	RIFF_ENTRY pmod; //PMOD block!
	RIFF_ENTRY inst; //INST block!
	RIFF_ENTRY ibag; //IBAG block!
	RIFF_ENTRY igen; //IGEN block!
	RIFF_ENTRY imod; //IMOD block!
	RIFF_ENTRY shdr; //SHDR block!
	RIFF_ENTRY pcmdata; //16-bit (SMPL) audio entry!
	RIFF_ENTRY pcm24data; //24-bit (SMPL) audio extension of 16-bit audio entry!
} RIFFHEADER; //RIFF data header!

//Basic open/close functions for soundfonts!
RIFFHEADER *readSF(char *filename); //Open, read and validate a soundfont!
void closeSF(RIFFHEADER **sf); //Close the soundfont!

//All different soundfont blocks to read for MIDI!

/* Presets */
//PHDR
byte getSFPreset(RIFFHEADER *sf, uint_32 preset, sfPresetHeader *result); //Retrieves a preset from a soundfont!
byte isValidPreset(sfPresetHeader *preset); //Valid for playback?

//PBAG
byte getSFPresetBag(RIFFHEADER *sf,word wPresetBagNdx, sfPresetBag *result);
byte isPresetBagNdx(RIFFHEADER *sf, uint_32 preset, word wPresetBagNdx);

//PMOD
byte getSFPresetMod(RIFFHEADER *sf, word wPresetModNdx, sfModList *result);
byte isPresetModNdx(RIFFHEADER *sf, word preset, word wPresetBagNdx, word wPresetModNdx);

//PGEN
byte getSFPresetGen(RIFFHEADER *sf, word wPresetGenNdx, sfGenList *result);
byte isPresetGenNdx(RIFFHEADER *sf, word preset, word wPresetBagNdx, word wPresetGenNdx);

/* Instruments */

//INST
byte getSFInstrument(RIFFHEADER *sf, word Instrument, sfInst *result);

//IBAG
byte getSFInstrumentBag(RIFFHEADER *sf, word wInstBagNdx, sfInstBag *result);
byte isInstrumentBagNdx(RIFFHEADER *sf, word Instrument, word wInstBagNdx);

//IMOD
byte getSFInstrumentMod(RIFFHEADER *sf, word wInstrumentModNdx, sfModList *result);
byte isInstrumentModNdx(RIFFHEADER *sf, word Instrument, word wInstrumentBagNdx, word wInstrumentModNdx);

//IGEN
byte getSFInstrumentGen(RIFFHEADER *sf, word wInstGenNdx, sfInstGenList *result);
byte isInstrumentGenNdx(RIFFHEADER *sf, word Instrument, word wInstrumentBagNdx, word wInstrumentGenNdx);

/* Samples */

//Sample information and samples themselves!
byte getSFSampleInformation(RIFFHEADER *sf, word Sample, sfSample *result);
byte getSFsample(RIFFHEADER *sf, uint_32 sample, short *result); //Get a 16/24-bit sample!

//Optimized versions of sample retrieval:
byte getSFSample16(RIFFHEADER *sf, uint_32 sample, short *result);
byte getSFSample24(RIFFHEADER *sf, uint_32 sample, int_32 *result);


/* Global and validation of zones */

byte isGlobalPresetZone(RIFFHEADER *sf, uint_32 preset, word PBag);
byte isGlobalInstrumentZone(RIFFHEADER *sf, word instrument, word IBag);
byte isValidPresetZone(RIFFHEADER *sf, uint_32 preset, word PBag);
byte isValidInstrumentZone(RIFFHEADER *sf, word instrument, word IBag);

/* Finally: some lookup functions for contents within the bags! */

/*

lookupSFPresetMod/lookupSFInstrumentMod: Retrieves a preset/instrument modulator from the list
parameters:
	sfModDestOper: What destination to filter against.
	index: The index to retrieve
	foundindex: Pointer to a variable containing the found index to not traverse again!
result:
	0: No modulators left
	1: Found
	2: Found, but not applicable (skip this entry)!

*/

byte lookupSFPresetMod(RIFFHEADER *sf, uint_32 preset, word PBag, SFModulator sfModDestOper, word index, sfModList *result, int_32* originMod, int_32* foundindex, word *resultindex);
byte lookupSFInstrumentMod(RIFFHEADER *sf, word instrument, word IBag, SFModulator sfModDestOper, word index, sfModList *result, int_32* originMod, int_32* foundindex, word *resultindex);

//Lookup a generator from preset
byte lookupSFPresetGen(RIFFHEADER* sf, uint_32 preset, word PBag, SFGenerator sfGenOper, sfGenList* result);

//Lookup a generator from instrument
byte lookupSFInstrumentGen(RIFFHEADER *sf, word instrument, word IBag, SFGenerator sfGenOper, sfInstGenList *result);

byte lookupPresetByInstrument(RIFFHEADER *sf, word preset, word bank, uint_32 *result);
byte lookupPBagByMIDIKey(RIFFHEADER *sf, uint_32 preset, byte MIDIKey, byte MIDIVelocity, word *result, int_32 previousPBag);
byte lookupIBagByMIDIKey(RIFFHEADER *sf, word instrument, byte MIDIKey, byte MIDIVelocity, word *result, byte RequireInstrument, int_32 previousIBag);

/* Global and normal lookup of data (global variations of the normal support) */

/*

lookupSFPresetModGlobal/lookupSFInstrumentModGlobal: See lookupSFPresetMod/lookupSFInstrumentMod
parameters:
	isGlobal: A flag indicating that the resulting modulator is global or not. Changing from non-Global to global means to abort(don't count the modulator as valid anymore).
result:
	See lookupSFPresetMod/lookupSFInstrumentMod

*/
byte lookupSFPresetModGlobal(RIFFHEADER *sf, uint_32 preset, word PBag, SFModulator sfModDestOper, word index, byte* isGlobal, sfModList *result, int_32* originMod, int_32* foundindex, word *resultindex);
byte lookupSFInstrumentModGlobal(RIFFHEADER *sf, uint_32 instrument, word IBag, SFModulator sfModDestOper, word index, byte* isGlobal, sfModList *result, int_32* originMod, int_32* foundindex, word *resultindex);

byte lookupSFPresetGenGlobal(RIFFHEADER* sf, word preset, word PBag, SFGenerator sfGenOper, sfGenList* result);
byte lookupSFInstrumentGenGlobal(RIFFHEADER* sf, word instrument, word IBag, SFGenerator sfGenOper, sfInstGenList* result);
#endif
