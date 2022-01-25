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
#include "headers/support/sf2.h" //Our typedefs!
#include "headers/support/zalloc.h" //ZAlloc support!
#include "headers/support/log.h" //Logging support!
#include "headers/support/signedness.h" //For converting between signed/unsigned samples!
#include "headers/fopen64.h" //64-bit fopen support!

//Makes sure the format is 16-bit little endian what's read!
#define LE16(x) SDL_SwapLE16(x)
#define LE32(x) SDL_SwapLE32(x)

/*

First, all RIFF support!

*/

/*

getRIFFChunkSize: Retrieves the chunk size from an entry!

*/

OPTINLINE uint_32 RIFF_entryheadersize(RIFF_ENTRY container) //Checked & correct!
{
	uint_32 result = 0; //Default: not found!
	if (!container.voidentry) return 0; //Invalid container!
	RIFF_DATAENTRY temp;
	memcpy(&temp,container.voidentry,sizeof(temp));
	temp.ckID = LE32(temp.ckID);
	if ((temp.ckID==CKID_LIST) || (temp.ckID==CKID_RIFF)) //Valid RIFF/LIST type?
	{
		result = sizeof(*container.listentry); //Take as list entry!
	}
	else //Valid data entry?
	{
    	result = sizeof(*container.dataentry); //Take as data entry!
	}
	return result; //Invalid entry!
}

OPTINLINE uint_32 getRIFFChunkSize(RIFF_ENTRY entry) //Checked & correct!
{
	uint_32 chunksize;
	RIFF_DATAENTRY data;
	if (!entry.voidentry) return 0; //No size: we're an invalid entry!
	memcpy(&data,entry.voidentry,sizeof(data)); //Copy for usage!
	data.ckID = LE32(data.ckID);
	chunksize = LE32(data.ckSize); //The chunk size!
	if ((data.ckID==CKID_RIFF) || (data.ckID==CKID_LIST)) //We're a RIFF/LIST list?
	{
		chunksize += sizeof(*entry.dataentry); //The index is too long: it counts from the end of an data entry, including the extra ID data.
		chunksize -= sizeof(*entry.listentry); //Take off the size too large for the final result!
	}
	return chunksize; //Give the size!
}

OPTINLINE void *RIFF_start_data(RIFF_ENTRY container, uint_32 headersize)
{
	byte *result;
	uint_32 size;
	if (!headersize) return NULL; //Invalid data!
	result = container.byteentry;
	result += headersize; //Take as data entry!
	size = getRIFFChunkSize(container); //Get the chunk size!
	if (size)
	{
		return result; //Give the result!
	}
	return NULL; //Invalid data!
}

OPTINLINE RIFF_ENTRY NULLRIFFENTRY()
{
	RIFF_ENTRY result;
	result.voidentry = NULL;
	return result; //Give the result!
}

/*

checkRIFFChunkLimits: Verify if a chunk within another chunk is valid to read (within range of the parent chunk)!

*/

OPTINLINE byte checkRIFFChunkLimits(RIFF_ENTRY container, void *entry, uint_32 entrysize) //Check an entry against it's limits!
{
	uint_32 containersize;
	ptrnum containerstart, containerend, entrystart, entryend;
	void *startData; //Start of the data!

	containersize = RIFF_entryheadersize(container); //What header size?
	if (!containersize) return 0; //Invalid container!

	startData = RIFF_start_data(container,containersize); //Get the start of the container data!
	containersize = getRIFFChunkSize(container); //Get the size of the content of the container!

	containerend = containerstart = (ptrnum)startData;
	containerend += containersize; //What size!

	entryend = entrystart = (ptrnum)entry; //Start of the data!
	entryend += entrysize;

	if (entrystart<containerstart) //Out of container bounds (low)?
	{
		return 0; //Out of bounds (low)!
	}
	if (entryend>containerend) //Out of container bounds (high)?
	{
		return 0; //Out of bounds (high)!
	}
	return 1; //OK!
}

/*

getRIFFEntry: Retrieves an RIFF Entry from a RIFF Chunk!

*/

OPTINLINE RIFF_ENTRY getRIFFEntry(RIFF_ENTRY RIFFHeader, FOURCC RIFFID) //Read a RIFF Subchunk from a RIFF chunk.
{
	RIFF_LISTENTRY listentry;
	RIFF_DATAENTRY dataentry;
	
	RIFF_ENTRY CurrentEntry;
	uint_32 foundid;
	RIFF_ENTRY temp_entry;
	uint_32 headersize; //Header size, precalculated!
	if (!RIFFHeader.dataentry) return NULLRIFFENTRY(); //Invalid RIFF Entry specified!
	
	//Our entries for our usage!
	CurrentEntry.voidentry = RIFF_start_data(RIFFHeader,RIFF_entryheadersize(RIFFHeader)); //Start of the data!

	if (!CurrentEntry.voidentry) return NULLRIFFENTRY(); //Invalid RIFF Header data!

	memcpy((void *)&dataentry,RIFFHeader.voidentry,sizeof(dataentry));
	dataentry.ckID = LE32(dataentry.ckID);
	dataentry.ckSize = LE32(dataentry.ckSize);
	
	foundid = dataentry.ckID; //Default: the standard ID specified!
	if ((foundid==CKID_LIST) || (foundid==CKID_RIFF)) //List type?
	{
		memcpy((void *)&listentry,RIFFHeader.voidentry,sizeof(listentry));
		foundid = LE32(listentry.fccType); //Take what the list type is!
	}
	
	for (;;) //Start on contents!
	{
		headersize = RIFF_entryheadersize(CurrentEntry); //The size of the current header!
		if (!checkRIFFChunkLimits(RIFFHeader,CurrentEntry.voidentry,headersize)) //Entry of bounds?
		{
			return NULLRIFFENTRY(); //Not found!
		}
		if (!checkRIFFChunkLimits(RIFFHeader,RIFF_start_data(CurrentEntry,headersize),getRIFFChunkSize(CurrentEntry))) //Data out of bounds?
		{
			return NULLRIFFENTRY(); //Not found!
		}
		memcpy(&dataentry,CurrentEntry.voidentry,sizeof(dataentry)); //Copy a data entry!
		foundid = LE32(dataentry.ckID); //Default: the standard ID specified!
		if ((foundid==CKID_LIST) || (foundid==CKID_RIFF)) //List type?
		{
			memcpy(&listentry,CurrentEntry.voidentry,sizeof(listentry)); //Copy a list entry!
			foundid = LE32(listentry.fccType); //Take what the list type is!
		}
		if (foundid==RIFFID) //Found the entry?
		{
			return CurrentEntry; //Give the entry!
		}
		//Entry not found?
		temp_entry.voidentry = RIFF_start_data(CurrentEntry,headersize); //Goto our start!
		temp_entry.byteentry += getRIFFChunkSize(CurrentEntry); //Add to the position of the start of the data!
		CurrentEntry.voidentry = temp_entry.voidentry; //Get the next entry!
	}
}

/*

getRIFFData: Gives data from a RIFF Chunk.
parameters:
	RIFFHeader: The Chunk to retrieve from.
	index: The entry number within the chunk.
	size: The size of a data entry.
result:
	NULL: Invalid entry, else the entry.

*/

OPTINLINE byte getRIFFData(RIFF_ENTRY RIFFHeader, uint_32 index, uint_32 size, void *result)
{
	RIFF_DATAENTRY temp;
	byte *entrystart;
	if (!RIFFHeader.voidentry) return 0; //Invalid entry!
	memcpy(&temp,RIFFHeader.voidentry,sizeof(temp)); //Get an entry!
	temp.ckID = LE32(temp.ckID);
	temp.ckSize = LE32(temp.ckSize);
	if ((temp.ckID==CKID_LIST) || (temp.ckID==CKID_RIFF)) //Has subchunks, no data?
	{
		return 0; //Invalid entry: we're a list, not data!
	}
	entrystart = (byte *)RIFF_start_data(RIFFHeader,RIFF_entryheadersize(RIFFHeader)); //Start of the data!
	if (!entrystart) //Error?
	{
		return 0; //Invalid entry: couldn't get the start of the data!
	}
	entrystart += (index*size); //Get the start index in memory!
	if (!checkRIFFChunkLimits(RIFFHeader,entrystart,size)) //Invalid data?
	{
		return 0; //Invalid entry!
	}
	memcpy(result,entrystart,size); //Copy the data to the result, aligned if needed!
	return 1; //Give the entry that's valid!
}

/*

Next, Basic SF open/close support!

*/

OPTINLINE byte validateSF(RIFFHEADER *RIFF) //Validate a soundfont file!
{
	uint_32 filesize;
	uint_32 finalentry; //For determining the final entry number!
	uint_64 detectedsize;
	RIFF_ENTRY sfbkentry, infoentry, version, soundentries, hydra, phdr, pbag, pmod, pgen, inst, ibag, imod, igen, shdr;
	sfVersionTag versiontag;
	sfPresetHeader finalpreset;
	sfPresetBag finalpbag;
	sfInst finalInst;
	sfInstBag finalibag;
	if (memprotect(RIFF,sizeof(RIFFHEADER),NULL)!=RIFF) //Error?
	{
		dolog("SF2","validateSF: Archive pointer is invalid!");
		return 0; //Not validated: invalid structure!
	}
	//First, validate the RIFF file structure!
	filesize = LE32(RIFF->filesize); //Get the file size from memory!
	if (memprotect(RIFF->rootentry.dataentry,filesize,NULL)!=RIFF->rootentry.dataentry) //Out of bounds in the memory module?
	{
		dolog("SF2","validateSF: Root entry pointer is invalid:!");
		return 0; //Not validated: invalid memory!
	}
	if (LE32(RIFF->rootentry.listentry->ckID)!=CKID_RIFF) //Not a soundfont block?
	{
		dolog("SF2","validateSF: RIFF Header is invalid!");
		return 0; //Not validated: not a RIFF file!
	}
	detectedsize = (getRIFFChunkSize(RIFF->rootentry)+RIFF_entryheadersize(RIFF->rootentry));
	if (detectedsize!=filesize) //Not the entire file? (we have multiple entries?)
	{
		dolog("SF2","validateSF: File has multiple entries: size detected: %u bytes, total: %u bytes; header: %u bytes!",detectedsize,filesize,RIFF_entryheadersize(RIFF->rootentry));
		return 0; //Not validated: invalid soundfont header (multiple headers not allowed)!
	}
	//Global OK!

	//Now check for precision content!
	if (LE32(RIFF->rootentry.listentry->fccType)!=CKID_SFBK) //Not a soundfont?
	{
		return 0; //Not validated: invalid soundfont header!
	}

	sfbkentry = RIFF->rootentry; //SFBK!
	if (!sfbkentry.listentry)
	{
		dolog("SF2","validateSF: SFBK Entry not found!");
		return 0; //Invalid SFBK!
	}
	
	//The root of our structure is intact, look further!
	
	infoentry = getRIFFEntry(RIFF->rootentry,CKID_INFO); //Get the info block!
	version = getRIFFEntry(infoentry,CKID_IFIL); //Get the version!
	if (!version.dataentry) //Not found?
	{
		dolog("SF2","validateSF: Version Entry not found!");
		return 0; //Not found!
	}
	if (getRIFFChunkSize(version)!=4) //Invalid size?
	{
		dolog("SF2","validateSF: Version has invalid size!");
		return 0; //Invalid size!
	}
	
	if (!getRIFFData(version,0,sizeof(sfVersionTag),&versiontag)) //Not gotten the data?
	{
		dolog("SF2","validateSF: Version Entry not retrieved!");
		return 0; //Couldn't get RIFF data!
	}
	if (versiontag.wMajor>2) //Too high major version?
	{
		dolog("SF2","validateSF: Invalid major version: %u!",versiontag.wMajor);
		return 0; //Invalid version!
	}
	if (versiontag.wMajor==2 && versiontag.wMinor>4) //Too high?
	{
		dolog("SF2","validateSF: Invalid minor version: %u!",versiontag.wMinor);
		return 0; //Invalid version!
	}
	//We're build arround the 2.04 specification!
	
	soundentries = getRIFFEntry(RIFF->rootentry,CKID_SDTA); //Check for the sound entries!
	RIFF->pcmdata = getRIFFEntry(soundentries,CKID_SMPL); //Found samples?
	if (!RIFF->pcmdata.dataentry) //Sample data not found?
	{
		dolog("SF2","validateSF: PCM samples not found!");
		return 0; //No sample data found!
	}
	
	RIFF->pcm24data = getRIFFEntry(soundentries,CKID_SM24); //Found 24-bit samples? If acquired, use it!

	//Check PHDR structure!
	RIFF->hydra = hydra = getRIFFEntry(RIFF->rootentry,CKID_PDTA); //Get the HYDRA structure!
	if (!hydra.dataentry)
	{
		dolog("SF2","validateSF: HYDRA block not found!");
		return 0; //No PDTA found!
	}
	
	//First, check the presets!
	RIFF->phdr = phdr = getRIFFEntry(hydra,CKID_PHDR); //Get the PHDR data!
	if (SAFEMOD(getRIFFChunkSize(phdr),38)) //Not an exact multiple of 38 bytes?
	{
		dolog("SF2","validateSF: PHDR block not found!");
		return 0; //Corrupt file!
	}
	
	finalentry = SAFEDIV(getRIFFChunkSize(phdr),sizeof(sfPresetHeader));
	--finalentry; //One less than the size in entries is our entry!

	if (!getRIFFData(phdr,finalentry,sizeof(sfPresetHeader),&finalpreset)) //Failed to get the PHDR data?
	{
		dolog("SF2","validateSF: Invalid final preset entry!");
		return 0; //Corrupt file!
	}

	if (!finalentry) //Nothing there?
	{
		dolog("SF2","validateSF: Missing final preset entry!");
		return 0; //No instruments found!
	}
	
	//Check PBAG size!
	RIFF->pbag = pbag = getRIFFEntry(hydra,CKID_PBAG); //Get the PBAG structure!
	if (SAFEMOD(getRIFFChunkSize(pbag),4)) //Not a multiple of 4?
	{
		dolog("SF2","validateSF: PBAG chunk size isn't a multiple of 4 bytes!");
		return 0; //Corrupt file!
	}
	if (!getRIFFData(pbag,LE16(finalpreset.wPresetBagNdx),sizeof(sfPresetBag),&finalpbag)) //Final not found?
	{
		dolog("SF2","validateSF: Final PBAG couldn't be retrieved!");
		return 0; //Corrupt file!
	}
	
	RIFF->pmod = pmod = getRIFFEntry(hydra,CKID_PMOD);
	if (!pmod.dataentry)
	{
		dolog("SF2","validateSF: PMOD chunk is missing!");
		return 0; //Corrupt file!
	}
	if (getRIFFChunkSize(pmod)!=(uint_32)((10*LE16(finalpbag.wModNdx))+10)) //Invalid PMOD size?
	{
		dolog("SF2","validateSF: Invalid PMOD chunk size!");
		return 0;
	}

	RIFF->pgen = pgen = getRIFFEntry(hydra,CKID_PGEN);
	if (!pgen.dataentry)
	{
		dolog("SF2","validateSF: PGEN chunk is missing!");
		return 0; //Corrupt file!
	}
	if (getRIFFChunkSize(pgen)!=(uint_32)((LE16(finalpbag.wGenNdx)<<2)+4)) //Invalid PGEN size?
	{
		dolog("SF2","validateSF: Invalid PGEN chunk size: %u; Expected %u!",getRIFFChunkSize(pgen),((LE16(finalpbag.wGenNdx)<<2)+4));
		return 0;
	}
	if (SAFEMOD(getRIFFChunkSize(pgen),4)) //Not a multiple of 4?
	{
		dolog("SF2","validateSF: PGEN chunk size isn't a multiple of 4 bytes!");
		return 0; //Corrupt file!
	}
	
	RIFF->inst = inst = getRIFFEntry(hydra,CKID_INST);
	if (!inst.dataentry)
	{
		dolog("SF2","validateSF: INST chunk is missing!");
		return 0; //Corrupt file!
	}
	if (SAFEMOD(getRIFFChunkSize(inst),22)) //Not a multiple of 22?
	{
		dolog("SF2","validateSF: INST chunk size isn't a multiple of 22 bytes!");
		return 0; //Corrupt file!
	}
	if (getRIFFChunkSize(inst)<(sizeof(sfInst)<<1)) //Too few records?
	{
		dolog("SF2","validateSF: The INST chunk has too few records!");
		return 0; //Corrupt file!
	}
	finalentry = SAFEDIV(getRIFFChunkSize(inst),sizeof(sfInst));
	--finalentry; //One less!

	if (!getRIFFData(inst,finalentry,sizeof(sfInst),&finalInst)) //Failed to get the record?
	{
		dolog("SF2","validateSF: INST chunk final entry couldn't be retrieved!");
		return 0; //Corrupt file!
	}

	RIFF->ibag = ibag = getRIFFEntry(hydra,CKID_IBAG);
	if (!ibag.dataentry)
	{
		dolog("SF2","validateSF: IBAG chunk is missing!");
		return 0; //Corrupt file!
	}
	if (getRIFFChunkSize(ibag)!=(uint_32)((4*LE16(finalInst.wInstBagNdx))+4))
	{
		dolog("SF2","validateSF: Invalid IBAG chunk size!");
		return 0; //Corrupt file!
	}
	if (SAFEMOD(getRIFFChunkSize(ibag),4)) //Not a multiple of 4?
	{
		dolog("SF2","validateSF: IBAG chunk size isn't a multiple of 4 bytes!");
		return 0; //Corrupt file!
	}
	if (getRIFFChunkSize(ibag)<(sizeof(sfInstBag)<<1)) //Too few records?
	{
		dolog("SF2","validateSF: IBAG chunk has too few records!");
		return 0; //Corrupt file!
	}
	if (!getRIFFData(ibag,LE16(finalInst.wInstBagNdx),sizeof(sfInstBag),&finalibag)) //Failed to get the records?
	{
		dolog("SF2","validateSF: IBAG chunk final entry couldn't be retrieved!");
		return 0; //Corrupt file!
	}

	//imod size=10xfinal instrument.wModNdx+10
	
	RIFF->imod = imod = getRIFFEntry(hydra,CKID_IMOD);
	if (!imod.dataentry)
	{
		dolog("SF2","validateSF: IMOD chunk is missing!");
		return 0; //Corrupt file!
	}
	if (getRIFFChunkSize(imod)!=(uint_32)((10*LE16(finalibag.wInstModNdx))+10))
	{
		dolog("SF2","validateSF: Invalid INST chunk size!");
		return 0; //Corrupt file!
	}
	if (SAFEMOD(getRIFFChunkSize(imod),10)) //Not a multiple of 10?
	{
		dolog("SF2","validateSF: INST chunk isn't a multiple of 10 bytes!");
		return 0; //Corrupt file?
	}
	
	//igen size=4xterminal instrument.wGenNdx+4
	RIFF->igen = igen = getRIFFEntry(hydra,CKID_IGEN);
	if (!igen.dataentry)
	{
		dolog("SF2","validateSF: IGEN chunk is missing!");
		return 0; //Corrupt file!
	}
	if (getRIFFChunkSize(igen)!=(uint_32)((4*LE16(finalibag.wInstGenNdx))+4))
	{
		dolog("SF2","validateSF: Invalid IGEN chunk size!");
		return 0; //Corrupt file!
	}
	if (SAFEMOD(getRIFFChunkSize(igen),4)) //Not a multiple of 4?
	{
		dolog("SF2","validateSF: IGEN chunk size isn't a multiple of 4 bytes!");
		return 0; //Corrupt file?
	}
	
	RIFF->shdr = shdr = getRIFFEntry(hydra,CKID_SHDR);
	if (!shdr.dataentry)
	{
		dolog("SF2","validateSF: SHDR chunk is missing!");
		return 0; //Corrupt file!
	}
	if (SAFEMOD(getRIFFChunkSize(shdr),46))
	{
		dolog("SF2","validateSF: SHDR chunk isn't a multiple of 46 bytes!");
		return 0; //Corrupt file!
	}
	
	//Create our quick lookup entries for samples!
	RIFF->pcmdata_data = (word *)RIFF_start_data(RIFF->pcmdata, RIFF_entryheadersize(RIFF->pcmdata)); //Start of the data!
	RIFF->pcmdata_size = (getRIFFChunkSize(RIFF->pcmdata) >> 1); //The ammount of entries we have as PCM samples!

	RIFF->pcm24data_data = (byte *)RIFF_start_data(RIFF->pcm24data, RIFF_entryheadersize(RIFF->pcm24data)); //Start of the data!
	RIFF->pcm24data_size = (getRIFFChunkSize(RIFF->pcm24data) >> 1); //The ammount of entries we have as PCM samples!

	//The RIFF file has been validated!
	return 1; //Validated!
}

RIFFHEADER *readSF(char *filename)
{
	
	BIGFILE *f;
	FILEPOS filesize;
	byte *buffer;
	static RIFFHEADER *riffheader;
	f = emufopen64(filename,"rb"); //Read the file!
	if (!f)
	{
		return NULL; //Error!
	}
	emufseek64(f,0,SEEK_END); //Goto EOF!
	filesize = emuftell64(f); //Look for the size!
	emufseek64(f,0,SEEK_SET); //Goto BOF!
	if ((!filesize) || (filesize&~0xFFFFFFFFU)) //No size?
	{
		dolog("SF2","Error: Soundfont %s is empty!",filename);
		emufclose64(f); //Close!
		return NULL; //File has no size!
	}
	buffer = (byte *)zalloc((uint_32)(filesize+sizeof(RIFFHEADER)),"RIFF_FILE",NULL); //A RIFF file entry in memory!
	if (!buffer) //Not enough memory?
	{
		dolog("SF2","Error: Ran out of memory to allocate the soundfont!");
		emufclose64(f); //Close the file!
		return NULL; //Error allocating the file!
	}
	riffheader = (RIFFHEADER *)buffer; //Convert to integer!
	riffheader->filesize = (uint_32)filesize; //Save the file size for checking!
	riffheader->rootentry.byteentry = (byte *)buffer+sizeof(RIFFHEADER); //Start of the data!
	if (emufread64(riffheader->rootentry.voidentry,1,filesize,f)!=filesize) //Error reading to memory?
	{
		dolog("SF2","Error: %s could not be read!",filename);
		emufclose64(f); //Close the file!
		freez((void **)&buffer,(uint_32)filesize,"RIFF_FILE"); //Free the file!
		return NULL; //Error!
	}
	emufclose64(f); //Close the file!
	if (validateSF(riffheader)) //Give the allocated buffer with the file!
	{
		return riffheader; //Give the result!
	}
	dolog("SF2","Error: The soundfont %s is corrupt!",filename);
	freez((void **)buffer,(uint_32)(filesize+sizeof(RIFFHEADER)),"RIFF_FILE"); //Release the buffer!
	return NULL; //Invalid soundfont!
}

void closeSF(RIFFHEADER **sf)
{
	RIFFHEADER *thesoundfont = *sf;
	uint_32 filesize;
	if (!memprotect(thesoundfont,sizeof(RIFFHEADER),"RIFF_FILE")) //Invalid header?
	{
		*sf = NULL; //Invalidate!
		return; //Abort!
	}
	filesize = thesoundfont->filesize;
	if (!filesize) //Invalid size?
	{
		*sf = NULL; //Invalidate!
		return; //Abort!
	}
	freez((void **)sf,filesize+sizeof(RIFFHEADER),"RIFF_FILE"); //Free the data!
}

/*

Basic reading functions for presets, instruments and samples.

*/

//Preset!
byte getSFPreset(RIFFHEADER *sf, uint_32 preset, sfPresetHeader *result)
{
	return getRIFFData(sf->phdr,preset,sizeof(sfPresetHeader),result); //Give the preset, if any is found!
}

byte isValidPreset(sfPresetHeader *preset) //Valid for playback?
{
	if (preset->wBank>128) return 0; //Invalid bank!
	if (preset->wPreset>127) return 0; //Invalid preset!
	return 1; //Valid preset!
}

//PBAG
byte getSFPresetBag(RIFFHEADER *sf,word wPresetBagNdx, sfPresetBag *result)
{
	return getRIFFData(sf->pbag,wPresetBagNdx,sizeof(sfPresetBag),result); //Give the preset Bag, if any is found!
}

//Next preset bag is just one index up.

byte isPresetBagNdx(RIFFHEADER *sf, uint_32 preset, word wPresetBagNdx)
{
	sfPresetHeader nextpreset, currentpreset;
	if (getSFPreset(sf,preset+1,&nextpreset) && getSFPreset(sf,preset,&currentpreset)) //Next&current preset found?
	{	
		return ((LE16(nextpreset.wPresetBagNdx>wPresetBagNdx)) && (wPresetBagNdx>=LE16(currentpreset.wPresetBagNdx))); //Are we owned by the preset!
	}
	return 0; //Not our pbag!
}

//PMOD

byte getSFPresetMod(RIFFHEADER *sf, word wPresetModNdx, sfModList *result)
{
	return getRIFFData(sf->pmod,wPresetModNdx,sizeof(sfModList),result); //Give the preset Mod, if any is found!
}

byte isPresetModNdx(RIFFHEADER *sf, word preset, word wPresetBagNdx, word wModNdx)
{
	sfPresetBag currentpbag; //current!
	sfPresetBag nextpbag; //next!
	if (getSFPresetBag(sf,wPresetBagNdx,&currentpbag) && getSFPresetBag(sf,wPresetBagNdx+1,&nextpbag))
	{
		return ((LE16(nextpbag.wModNdx)>wModNdx) && (wModNdx>=LE16(currentpbag.wModNdx))); //Are we owned by the preset bag!
	}
	return 0; //Not our pmod!
}

//PGEN

byte getSFPresetGen(RIFFHEADER *sf, word wPresetGenNdx, sfGenList *result)
{
	return getRIFFData(sf->pgen,wPresetGenNdx,sizeof(sfGenList),result); //Give the preset Mod, if any is found!
}

byte isPresetGenNdx(RIFFHEADER *sf, word preset, word wPresetBagNdx, word wGenNdx)
{
	sfPresetBag currentpbag; //current!
	sfPresetBag nextpbag; //next!
	if (getSFPresetBag(sf,wPresetBagNdx,&currentpbag) && getSFPresetBag(sf,wPresetBagNdx+1,&nextpbag))
	{
		return ((LE16(nextpbag.wGenNdx)>wGenNdx) && (wGenNdx>=LE16(currentpbag.wGenNdx))); //Are we owned by the preset bag!
	}
	return 0; //Not our pgen!
}

//Next, we have the instrument layer!

//INST

byte getSFInstrument(RIFFHEADER *sf, word Instrument, sfInst *result)
{
	return getRIFFData(sf->inst,Instrument,sizeof(sfInst),result); //Give the Instrument, if any is found!
}

//IBAG

byte getSFInstrumentBag(RIFFHEADER *sf, word wInstBagNdx, sfInstBag *result)
{
	return getRIFFData(sf->ibag,wInstBagNdx,sizeof(sfInstBag),result); //Give the Instrument Bag, if any is found!	
}

byte isInstrumentBagNdx(RIFFHEADER *sf, word Instrument, word wInstBagNdx)
{
	sfInst currentinstrument; //current!
	sfInst nextinstrument; //next!
	if (getSFInstrument(sf,Instrument,&currentinstrument) && getSFInstrument(sf,Instrument+1,&nextinstrument))
	{
		return ((LE16(nextinstrument.wInstBagNdx)>wInstBagNdx) && (wInstBagNdx>=LE16(currentinstrument.wInstBagNdx))); //Are we owned by the instrument!
	}
	return 0; //Not our pmod!
	
}

//IMOD

byte getSFInstrumentMod(RIFFHEADER *sf, word wInstrumentModNdx, sfModList *result)
{
	return getRIFFData(sf->imod,wInstrumentModNdx,sizeof(sfModList),result); //Give the preset Mod, if any is found!
}

byte isInstrumentModNdx(RIFFHEADER *sf, word Instrument, word wInstrumentBagNdx, word wInstrumentModNdx)
{
	sfInstBag currentibag; //current!
	sfInstBag nextibag; //next!
	if (getSFInstrumentBag(sf,wInstrumentBagNdx,&currentibag) && getSFInstrumentBag(sf,wInstrumentBagNdx+1,&nextibag))
	{
		return ((LE16(nextibag.wInstModNdx)>wInstrumentModNdx) && (wInstrumentModNdx>=LE16(currentibag.wInstModNdx))); //Are we owned by the instrument bag!
	}
	return 0; //Not our pmod!
}

//IGEN

byte getSFInstrumentGen(RIFFHEADER *sf, word wInstGenNdx, sfInstGenList *result)
{
	return getRIFFData(sf->igen,wInstGenNdx,sizeof(sfInstGenList),result); //Give the instrument Gen, if any is found!
}

byte isInstrumentGenNdx(RIFFHEADER *sf, word Instrument, word wInstrumentBagNdx, word wInstrumentGenNdx)
{
	sfInstBag currentibag; //current!
	sfInstBag nextibag; //next!
	if (getSFInstrumentBag(sf,wInstrumentBagNdx,&currentibag) && getSFInstrumentBag(sf,wInstrumentBagNdx+1,&nextibag))
	{
		return ((LE16(nextibag.wInstGenNdx)>wInstrumentGenNdx) && (wInstrumentGenNdx>=LE16(currentibag.wInstGenNdx))); //Are we owned by the instrument bag!
	}
	return 0; //Not our pmod!
}

//Sample information about the samples.

byte getSFSampleInformation(RIFFHEADER *sf, word Sample, sfSample *result)
{
	byte temp;
	RIFF_ENTRY shdr = sf->shdr; //Get the SHDR data!
	if (!shdr.voidentry)
	{
		return 0; //Error!
	}
	temp = getRIFFData(shdr,Sample,sizeof(sfSample),result); //Give the sample information, if any is found!	
	if (temp) //Found?
	{
		if (result->byOriginalPitch&0x80) //128+?
		{
			result->byOriginalPitch = 60; //Assume 60!
		}
	}
	//Patch!
	result->dwEnd = LE32(result->dwEnd);
	result->dwEndloop = LE32(result->dwEndloop);
	result->dwSampleRate = LE32(result->dwSampleRate);
	result->dwStart = LE32(result->dwStart);
	result->sfSampleType = LE16(result->sfSampleType);
	return temp; //Give the result!
}

//Samples themselves!

short getsample16(word sample)
{
    return unsigned2signed16(sample); //Give the 16-bit sample!
}

short getsample24_16(uint_32 sample) //Get 24 bits sample and convert it to a 16-bit sample!
{
	union
	{
		uint_32 sample32;
		int_32 samplesigned;
	} u;
	u.sample32 = sample; //Load basic sample!
	if (u.sample32&0x800000) //Sign bit set?
	{
		u.sample32 |= 0xFF000000; //Sign extend!
	}

	//Now we have a 24-bit sample loaded!
	u.samplesigned >>= 8; //Convert to 16-bit sample range!
	return (short)u.samplesigned; //Convert to short for the result!
}

//Sample!
byte getSFsample(RIFFHEADER *sf, uint_32 sample, short *result) //Get a 16/24-bit(downsampled) sample!
{
	static word sample16;
	byte gotsample16 = getRIFFData(sf->pcmdata,sample,sizeof(word),&sample16); //Get the sample!

	//24-bit sample high 8 bits
	static byte sample24;
	byte gotsample24 = getRIFFData(sf->pcm24data,sample,sizeof(byte),&sample24); //Get the sample!
	
	//Take the correct sample (16/24 bit samples with conversion to 16-bit samples)
	if (gotsample24 && gotsample16) //24-bit sample found?
	{
		uint_32 tempsample;
		tempsample = sample24;
		tempsample <<=16;
		tempsample |= LE16(sample16); //Create the full sample!
		*result = getsample24_16(tempsample); //Get 24-bit sample!
		return 1; //OK!
	}
	else if (gotsample16) //16-bit sample found?
	{
		*result = getsample16(LE16(sample16)); //Get the sample!
		return 1; //OK!
	}

	//Invalid sample?
	*result = 0; //Clear!
	return 0; //Invalid sample!
}

//Specific, optimized versions of sample retrieval: 16 or 24-bits samples are retrieved and given more quickly, with only basic checks (limits only)!

byte getSFSample16(RIFFHEADER *sf, uint_32 sample, short *result)
{
	union
	{
		word result_tmp;
		short result_sh;
	} converter; //Conversion of the result!
	if (sf->pcmdata_size<sample) return 0; //Invalid sample!
	converter.result_tmp = sf->pcmdata_data[sample]; //Retrieve the sample!
	*result = converter.result_sh; //Set the result!
	return 1; //We have a sample!
}

byte getSFSample24(RIFFHEADER *sf, uint_32 sample, int_32 *result)
{
	static union
	{
		uint_32 result_tmp;
		int_32 result_i;
	} converter; //Conversion of the result!
	if (!sf) return 0; //Error: no soundfont!
	if (sf->pcmdata_size < sample) return 0; //Invalid sample!
	if (sf->pcm24data_size < sample) return 0; //Invalid 24-bit sample!
	converter.result_tmp = sf->pcm24data_data[sample]; //Retrieve the high 8-bits!
	converter.result_tmp <<= 16; //Generate some space for the rest of the sample!
	converter.result_tmp |= sf->pcmdata_data[sample]; //Add the low 16-bits!
	if (converter.result_tmp & 0x800000) //Sign bit set?
	{
		converter.result_tmp |= 0xFF000000; //Sign extend to 32-bits!
	}
	*result = converter.result_i; //Set the result!
	return 1; //We have a sample!
}

/*

Global zone detection

*/

byte isGlobalPresetZone(RIFFHEADER *sf, uint_32 preset, word PBag)
{
	sfPresetHeader currentpreset;
	sfPresetBag pbag;
	sfPresetBag pbag2;
	word firstPBag;
	sfGenList finalgen;
	if (getSFPreset(sf,preset,&currentpreset)) //Retrieve the header!
	{
		if (isValidPreset(&currentpreset)) //Valid preset?
		{
			firstPBag = LE16(currentpreset.wPresetBagNdx); //Load the first PBag!
			if (PBag==firstPBag) //Must be the first PBag!
			{
				if (isPresetBagNdx(sf,preset,firstPBag) && isPresetBagNdx(sf,preset,firstPBag+1)) //Multiple zones?
				{
					//Now lookup the final entry of the first PBag!
					if (getSFPresetBag(sf,firstPBag+1,&pbag)) //Load the second zone!
					{
						if (isPresetGenNdx(sf,preset,firstPBag,LE16(pbag.wGenNdx)-1)) //Final is valid?
						{
							if (getSFPresetGen(sf,LE16(pbag.wGenNdx)-1,&finalgen)) //Retrieve the final generator of the first zone! //Loaded!
							{
								if (finalgen.sfGenOper!=instrument) //Final isn't an instrument?
								{
									return 1; //We're a global zone!
								}
								else if (finalgen.sfGenOper==endOper) //Ending operator? We're to look one back further!
								{
									if (isPresetGenNdx(sf,preset,firstPBag,LE16(pbag.wGenNdx)-2)) //Final is valid?
									{
										if (getSFPresetGen(sf,LE16(pbag.wGenNdx)-2,&finalgen)) //Retrieve the final generator of the first zone! //Loaded!
										{
											if (finalgen.sfGenOper!=instrument) //Final isn't an instrument?
											{
												return 1; //We're a global zone!
											}
										}
									}
								}
							}
						}
						
						if (getSFPresetBag(sf,firstPBag,&pbag2)) //First is valid?
						{
							if (!isPresetGenNdx(sf,preset,firstPBag,LE16(pbag2.wGenNdx)) && isPresetModNdx(sf,preset,firstPBag,LE16(pbag2.wModNdx))) //No generators but do have modulators?
							{
								return 1; //We're a global zone after all!
							}
						}
					}
				}
			}
		}
	}
	return 0; //No global zone!
}

byte isGlobalInstrumentZone(RIFFHEADER *sf, word instrument, word IBag)
{
	sfInst currentinstrument;
	word firstIBag;
	sfInstBag ibag;
	sfInstGenList finalgen;
	sfInstBag ibag2;
	if (getSFInstrument(sf,instrument,&currentinstrument)) //Valid instrument?
	{
		firstIBag = LE16(currentinstrument.wInstBagNdx); //Load the first PBag!
		if (IBag==firstIBag) //Must be the first PBag!
		{
			if (isInstrumentBagNdx(sf,instrument,firstIBag) && isInstrumentBagNdx(sf,instrument,firstIBag+1)) //Multiple zones?
			{
				//Now lookup the final entry of the first PBag!
				if (getSFInstrumentBag(sf,firstIBag+1,&ibag)) //Load the second zone!
				{
					if (isInstrumentGenNdx(sf,instrument,firstIBag,LE16(ibag.wInstGenNdx)-1)) //Final is valid?
					{
						if (getSFInstrumentGen(sf,LE16(ibag.wInstGenNdx)-1,&finalgen)) //Retrieve the final generator of the first zone! //Loaded!
						{
							if (finalgen.sfGenOper!=sampleID) //Final isn't an sampleId?
							{
								return 1; //We're a global zone!
							}
							else if (finalgen.sfGenOper==endOper) //Ending operator? We're to look one back further!
							{
								if (isInstrumentGenNdx(sf,instrument,firstIBag,LE16(ibag.wInstGenNdx)-2)) //Final is valid?
								{
									if (getSFInstrumentGen(sf,LE16(ibag.wInstGenNdx)-2,&finalgen)) //Retrieve the final generator of the first zone! //Loaded!
									{
										if (finalgen.sfGenOper!=sampleID) //Final isn't an sampleId?
										{
											return 1; //We're a global zone!
										}
									}
								}
							}
						}
					}
				}
				
				if (getSFInstrumentBag(sf,firstIBag,&ibag2)) //First is valid?
				{
					if (!isInstrumentGenNdx(sf,instrument,firstIBag,LE16(ibag2.wInstGenNdx)) && isPresetModNdx(sf,instrument,firstIBag,LE16(ibag2.wInstModNdx))) //No generators but do have modulators?
					{
						return 1; //We're a global zone after all!
					}
				}
			}
		}
	}
	return 0; //No global zone!
}

byte isValidPresetZone(RIFFHEADER *sf, uint_32 preset, word PBag)
{
	sfPresetBag pbag;
	sfGenList finalgen;
	if (isGlobalPresetZone(sf,preset,PBag)) //Valid global zone?
	{
		return 1; //Global zone: no instrument is allowed!
	}
	
	//We're a local zone!
	
	//Now lookup the final entry of the first PBag!
	if (getSFPresetBag(sf,PBag+1,&pbag)) //Load the second zone!
	{
		if (isPresetGenNdx(sf,preset,PBag,LE16(pbag.wGenNdx)-1)) //Final is valid?
		{
			if (getSFPresetGen(sf,LE16(pbag.wGenNdx)-1,&finalgen)) //Retrieve the final generator of the first zone! //Loaded!
			{
				if (finalgen.sfGenOper!=instrument) //Final isn't an instrument?
				{
					return 0; //We're a local zone without an instrument!
				}
				return 1; //Valid: we have an instrument!
			}
		}
	}
	
	return 0; //Invalid zone!
}

byte isValidInstrumentZone(RIFFHEADER *sf, word instrument, word IBag)
{
	sfInstBag ibag;
	sfInstGenList finalgen;
	if (isGlobalInstrumentZone(sf,instrument,IBag)) //Valid global zone?
	{
		return 1; //Global zone: no sampleid is allowed!
	}
	
	//We're a local zone!
	
	//Now lookup the final entry of the first PBag!
	if (getSFInstrumentBag(sf,IBag+1,&ibag)) //Load the second zone!
	{
		if (isInstrumentGenNdx(sf,instrument,IBag,LE16(ibag.wInstGenNdx)-1)) //Final is valid?
		{
			if (getSFInstrumentGen(sf,LE16(ibag.wInstGenNdx)-1,&finalgen)) //Retrieve the final generator of the first zone! //Loaded!
			{
				if (finalgen.sfGenOper!=sampleID) //Final isn't a sampleid?
				{
					return 0; //We're a local zone without an instrument!
				}
				return 1; //Valid: we have an instrument!
			}
		}
	}
	
	return 0; //Invalid zone!
}

/*

Finally: some lookup functions for contents within the bags!

*/

/*

lookupSFPresetMod: Retrieves a preset from the list
parameters:
	sfModDestOper: What destination to filter against.
	index: The index to retrieve
	originMod: Used to keep track of the original mod that's detected! Start with -INT_MAX!
result:
	0: No modulators left
	1: Found
	2: Found, but not applicable (skip this entry)!

*/
byte lookupSFPresetMod(RIFFHEADER *sf, uint_32 preset, word PBag, SFModulator sfModDestOper, word index, sfModList *result, int_32 *originMod, int_32* foundindex, word* resultindex)
{
	sfPresetHeader currentpreset;
	word CurrentMod;
	sfPresetBag pbag;
	sfModList mod,emptymod,originatingmod;
	byte found;
	word currentindex;
	byte gotoriginatingmod;
	byte originatingfilter;
	memset(&originatingmod, 0, sizeof(originatingmod)); //Init!
	currentindex = 0; //First index to find!
	found = 0; //Default: not found!
	gotoriginatingmod = 0; //Default: not gotten yet!
	*resultindex = 0; //Not gotten yet!
	if (getSFPreset(sf,preset,&currentpreset)) //Retrieve the header!
	{
		if (isValidPreset(&currentpreset)) //Valid preset?
		{
			if (isPresetBagNdx(sf,preset,PBag)) //Process the PBag for our preset&pbag!
			{
				if (getSFPresetBag(sf,PBag,&pbag)) //Load the current PBag! //Valid?
				{
					if (isValidPresetZone(sf,preset,PBag)) //Valid?
					{
						CurrentMod = LE16(pbag.wModNdx); //Load the first PMod!
						memset(&emptymod,0,sizeof(emptymod)); //Final mod!
						for (;isPresetModNdx(sf,preset,PBag,CurrentMod);) //Process all PMods for our bag!
						{
							if (getSFPresetMod(sf,CurrentMod,&mod)) //Valid?
							{
								if (memcmp(&mod,&emptymod,sizeof(mod))==0) break; //Stop searching on final item!
								if (LE16(mod.sfModDestOper)==sfModDestOper) //Found?
								{
									originatingfilter = 1; //Default: no originating filter!
									if (*originMod == CurrentMod) //Current mod is originating?
									{
										memcpy(&originatingmod, &mod, sizeof(mod)); //Originating mod itself to keep track of!
										gotoriginatingmod = 1; //Got originating mod!
									}
									if (*originMod != INT_MIN) //Originating mod specified?
									{
										if (gotoriginatingmod) //Got originating mod?
										{
											originatingfilter = ((originatingmod.sfModSrcOper == mod.sfModSrcOper) && (originatingmod.sfModDestOper == mod.sfModDestOper) && (originatingmod.sfModAmtSrcOper == mod.sfModAmtSrcOper));
										}
										else //Not gotten originating mod yet?
										{
											originatingfilter = 0; //Invalid to use!
										}
									}
									if (originatingfilter) //Valid to use?
									{
										if (currentindex == index) //Requested index?
										{
											if (found == 0) //Not found yet?
											{
												found = 1; //Found!
												*foundindex = CurrentMod; //What index has been found!
												memcpy(result, &mod, sizeof(*result)); //Set to last found!
												if (CurrentMod < 0x8000) //Valid to use?
												{
													*resultindex = CurrentMod + 0x8000; //Found index!
												}
												if (*originMod == INT_MIN) //Not set yet?
												{
													*originMod = -CurrentMod; //Copy of the original modulator!
													memcpy(&originatingmod, &mod, sizeof(originatingmod)); //Copy of the originating mod!
												}
											}
											else if (found == 1) //Already found something?
											{
												found = 2; //Found a second one, skip the first one!
											}
										}
										else if (currentindex > index) //Later index than what's requested?
										{
											if (found) //Already found?
											{
												found = 2; //Found a second one, skip the first one!
											}
										}
										++currentindex; //Next index to check against!
									}
								}
							}
							++CurrentMod;
						}
					}
				}
			}
		}
	}
	return found; //Not found or last found!
}

byte lookupSFPresetGen(RIFFHEADER *sf, uint_32 preset, word PBag, SFGenerator sfGenOper, sfGenList *result)
{
	sfPresetHeader currentpreset;
	uint_32 CurrentGen;
	sfPresetBag pbag;
	sfGenList gen;
	byte found;
	found = 0; //Default: not found!
	uint_32 firstgen, keyrange, temp; //Other generators and temporary calculation!
	keyrange = 0; //Not specified yet!
	if (getSFPreset(sf,preset,&currentpreset)) //Retrieve the header!
	{
		if (isValidPreset(&currentpreset)) //Valid preset?
		{
			if (isPresetBagNdx(sf,preset,PBag)) //Process all PBags for our preset!
			{
				if (getSFPresetBag(sf,PBag,&pbag)) //Load the current PBag! //Valid?
				{
					if (isValidPresetZone(sf,preset,PBag)) //Valid?
					{
						CurrentGen = LE16(pbag.wGenNdx); //Load the first PGen!
						firstgen = CurrentGen; //First generator!
						for (;isPresetGenNdx(sf,preset,PBag,CurrentGen);) //Process all PGens for our bag!
						{
							if (getSFPresetGen(sf,CurrentGen,&gen)) //Valid?
							{
								if (LE16(gen.sfGenOper)==endOper) break; //Stop when finding the last entry!
								byte valid;
								valid = 1; //Default: still valid!
								if (LE16(gen.sfGenOper) == keyRange) //KEY RANGE?
								{
									if (firstgen != CurrentGen) //Not the first?
									{
										valid = 0; //Ignore this generator!
									}
									if (valid) //Valid?
									{
										keyrange = (CurrentGen&0xFFFF); //Save the position of the last key range generator!
										keyrange |= 0x10000; //Set flag: we're used!
									}
								}
								else if (LE16(gen.sfGenOper) == velRange) //VELOCITY RANGE?
								{
									temp = CurrentGen; //Load!
									--temp; //Decrease!
									temp &= 0xFFFF; //16-bit range!
									temp |= 0x10000; //Set bit for lookup!
									if ((keyrange != temp) || (keyrange == 0)) //Last wasn't a key range or we're not the first otherwise?
									{
										valid = 0; //Ignore this generator!
									}
								}
								if (valid) //Still valid?
								{
									if (LE16(gen.sfGenOper) == sfGenOper) //Found?
									{
										found = 1; //Found!
										memcpy(result, &gen, sizeof(*result)); //Set to last found!
									}
								}
							}
							++CurrentGen;
						}
					}
				}
			}
		}
	}
	return found; //Not found or last found!
}

sfModList defaultInstrumentModulators[10] = { //Default instrument modulators!
	{.sfModSrcOper = 0x0502, .sfModDestOper = initialAttenuation, .modAmount = 960, .sfModAmtSrcOper = 0x0, .sfModTransOper = 0}, //Note-On velocity to Initial Attenuation
	{.sfModSrcOper = 0x0102, .sfModDestOper = initialFilterFc, .modAmount = -2400, .sfModAmtSrcOper = 0x0, .sfModTransOper = 0}, //Note-On velocity to Filter Cutoff
	{.sfModSrcOper = 0x000D, .sfModDestOper = vibLfoToPitch, .modAmount = 50, .sfModAmtSrcOper = 0x0, .sfModTransOper = 0}, //Channel pressure to Vibrato LFO Pitch Depth
	{.sfModSrcOper = 0x0081, .sfModDestOper = vibLfoToPitch, .modAmount = 50, .sfModAmtSrcOper = 0x0, .sfModTransOper = 0}, //CC1 to Vibrato LFO Pitch Depth
	{.sfModSrcOper = 0x0587, .sfModDestOper = initialAttenuation, .modAmount = 960, .sfModAmtSrcOper = 0x0, .sfModTransOper = 0}, //CC7 to Initial Attenuation. The soundfont 2.04 spec says 0582, but it's supposed to be 0587!
	{.sfModSrcOper = 0x028A, .sfModDestOper = pan, .modAmount = 1000, .sfModAmtSrcOper = 0x0, .sfModTransOper = 0}, //CC10 to Pan Position
	{.sfModSrcOper = 0x058B, .sfModDestOper = initialAttenuation, .modAmount = 960, .sfModAmtSrcOper = 0x0, .sfModTransOper = 0}, //CC11 to Initial Attenuation
	{.sfModSrcOper = 0x00DB, .sfModDestOper = reverbEffectsSend, .modAmount = 200, .sfModAmtSrcOper = 0x0, .sfModTransOper = 0}, //CC91 to Reverb Effects Send
	{.sfModSrcOper = 0x00DD, .sfModDestOper = chorusEffectsSend, .modAmount = 200, .sfModAmtSrcOper = 0x0, .sfModTransOper = 0}, //CC93 to Chorus Effects Send
	{.sfModSrcOper = 0x020E, .sfModDestOper = fineTune, .modAmount = 12700, .sfModAmtSrcOper = 0x0010, .sfModTransOper = 0} //Pitch wheel to Initial Pitch Controlled by MIDI Pitch Wheel Sensitivity
};

/*

lookupSFInstrumentMod: Retrieves a instrument modulator from the list
parameters:
	sfModDestOper: What destination to filter against.
	index: The index to retrieve
	originMod: Used to keep track of the original mod that's detected! Start with -INT_MAX!
result:
	0: No modulators left
	1: Found
	2: Found, but not applicable (skip this entry)!

*/
byte lookupSFInstrumentMod(RIFFHEADER *sf, word instrument, word IBag, SFModulator sfModDestOper, word index, sfModList *result, int_32* originMod, int_32* foundindex, word* resultindex)
{
	sfInst currentinstrument;
	word CurrentMod;
	sfInstBag ibag;
	sfModList mod,emptymod,originatingmod;
	byte found;
	word currentindex;
	currentindex = 0; //First index to find!
	found = 0; //Default: not found!
	byte originatingfilter;
	byte gotoriginatingmod;
	gotoriginatingmod = 0; //Not gotten the originating mod yet!
	memset(&originatingmod, 0, sizeof(originatingmod)); //Init originating mod!

	//First, apply the default modulators!
	for (CurrentMod = 0; CurrentMod < NUMITEMS(defaultInstrumentModulators); ++CurrentMod) //Process the defaults first!
	{
		memcpy(&mod, &defaultInstrumentModulators[CurrentMod], sizeof(mod)); //Take the default modulator found!
		//Handle it just like another instrument modulator!
		if (LE16(mod.sfModDestOper) == sfModDestOper) //Found destination?
		{
			if (currentindex == index) //Requested index?
			{
				if (found == 0) //Not found yet?
				{
					found = 1; //Found!
					memcpy(result, &mod, sizeof(*result)); //Set to last found!
					*foundindex = -CurrentMod; //What index has been found!
					if (*originMod == INT_MIN) //Not set yet?
					{
						*originMod = -CurrentMod; //Copy of the original modulator!
						memcpy(&originatingmod, &mod, sizeof(originatingmod)); //Copy of the originating mod!
						gotoriginatingmod = 1; //Got originating mod!
					}
				}
				else if (*originMod == -CurrentMod) //Originating mod?
				{
					memcpy(&originatingmod, &mod, sizeof(originatingmod)); //Copy of the originating mod!
					gotoriginatingmod = 1; //Got originating mod!
				}
			}
			else if (*originMod == -CurrentMod) //Originating mod?
			{
				memcpy(&originatingmod, &mod, sizeof(originatingmod)); //Copy of the originating mod!
				gotoriginatingmod = 1; //Got originating mod!
			}
			++currentindex; //Next index to check against!
		}
	}

	*resultindex = 0; //No found index yet!

	if (getSFInstrument(sf,instrument,&currentinstrument)) //Valid instrument?
	{
		if (isInstrumentBagNdx(sf,instrument,IBag)) //Process all PBags for our preset!
		{
			if (getSFInstrumentBag(sf,IBag,&ibag)) //Valid?
			{
				if (isValidInstrumentZone(sf,instrument,IBag)) //Valid?
				{
					CurrentMod = LE16(ibag.wInstModNdx); //Load the first PMod!
					memset(&emptymod,0,sizeof(emptymod)); //Final mod!
					for (;isInstrumentModNdx(sf,instrument,IBag,CurrentMod);) //Process all PMods for our bag!
					{
						if (getSFInstrumentMod(sf,CurrentMod,&mod)) //Valid?
						{
							if (memcmp(&mod, &emptymod, sizeof(mod)) == 0) break; //Stop searching on final item!
							if (LE16(mod.sfModDestOper) == sfModDestOper) //Found?
							{
								originatingfilter = 1; //Default: no originating filter!
								if (*originMod == CurrentMod) //Current mod is originating?
								{
									memcpy(&originatingmod, &mod, sizeof(mod)); //Originating mod itself to keep track of!
									gotoriginatingmod = 1; //Got originating mod!
								}
								if (*originMod != INT_MIN) //Originating mod specified?
								{
									if (gotoriginatingmod) //Got originating mod?
									{
										originatingfilter = ((originatingmod.sfModSrcOper==mod.sfModSrcOper) && (originatingmod.sfModDestOper==mod.sfModDestOper) && (originatingmod.sfModAmtSrcOper==mod.sfModAmtSrcOper));
									}
									else //Not gotten originating mod yet?
									{
										originatingfilter = 0; //Invalid to use!
									}
								}
								if (originatingfilter) //Valid to use?
								{
									if (currentindex == index) //Requested index?
									{
										if (found == 0) //Not found yet?
										{
											found = 1; //Found!
											memcpy(result, &mod, sizeof(*result)); //Set to last found!
											*foundindex = CurrentMod; //What index has been found!
											if (CurrentMod < 0x8000) //Valid to use?
											{
												*resultindex = CurrentMod + 0x8000; //Found index!
											}
											if (*originMod == INT_MIN) //Not set yet?
											{
												*originMod = -CurrentMod; //Copy of the original modulator!
												memcpy(&originatingmod, &mod, sizeof(originatingmod)); //Copy of the originating mod!
											}
										}
										else if (found == 1) //Already found something?
										{
											found = 2; //Found a second one, skip the first one!
										}
									}
									else if (currentindex > index) //Later index than what's requested?
									{
										if (found) //Already found?
										{
											found = 2; //Found a second one, skip the first one!
										}
									}
									++currentindex; //Next index to check against!
								}
							}
						}
						++CurrentMod;
					}
				}
			}
		}
	}
	return found; //Not found or last found!
}

byte lookupSFInstrumentGen(RIFFHEADER *sf, word instrument, word IBag, SFGenerator sfGenOper, sfInstGenList *result)
{
	sfInst currentinstrument;
	uint_32 CurrentGen;
	sfInstBag ibag;
	sfInstGenList gen;
	uint_32 firstgen, keyrange, temp; //Other generators and temporary calculation!
	byte found;
	byte dontignoregenerators;
	byte valid;
	valid = 1; //Default: still valid!
	found = 0;
	if (getSFInstrument(sf,instrument,&currentinstrument)) //Valid instrument?
	{
		if (isInstrumentBagNdx(sf,instrument,IBag)) //Process all PBags for our preset!
		{
			if (getSFInstrumentBag(sf,IBag,&ibag)) //Valid?
			{
				if (isValidInstrumentZone(sf,instrument,IBag)) //Valid?
				{
					CurrentGen = LE16(ibag.wInstGenNdx); //Load the first PMod!
					firstgen = CurrentGen; //Save first generator position!
					keyrange = 0; //We're resetting the first generator and key range to unspecified!
					dontignoregenerators = 1; //Default: don't ignore generators for this zone!
					for (;isInstrumentGenNdx(sf,instrument,IBag,CurrentGen);) //Process all PMods for our bag!
					{
						if (getSFInstrumentGen(sf,CurrentGen,&gen)) //Valid?
						{
							if (LE16(gen.sfGenOper)==keyRange) //KEY RANGE?
							{
								if (firstgen!=CurrentGen) //Not the first?
								{
									valid = 0; //Ignore this generator!
								}
								if (valid) //Valid?
								{
									keyrange = (CurrentGen&0xFFFF); //Save the position of the last key range generator!
									keyrange |= 0x10000; //Set flag: we're used!
								}
							}
							else if (LE16(gen.sfGenOper)==velRange) //VELOCITY RANGE?
							{
								temp = CurrentGen; //Load!
								--temp; //Decrease!
								temp &= 0xFFFF; //16-bit range!
								temp |= 0x10000; //Set bit for lookup!
								if ((keyrange!=temp) || (keyrange==0)) //Last wasn't a key range or we're not the first otherwise?
								{
									valid = 0; //Ignore this generator!
								}
							}
							if (valid && dontignoregenerators) //Still valid?
							{
								if (LE16(gen.sfGenOper)==endOper) break; //Stop when finding the last entry!
								if (LE16(gen.sfGenOper)==sfGenOper && (found==0) && (dontignoregenerators || LE16(gen.sfGenOper)==sampleID)) //Found and not ignoring (or sampleid generator)?
								{
									//Log the retrieval!
									found = 1; //Found!
									memcpy(result,&gen,sizeof(*result)); //Set to last found!
								}
								if (gen.sfGenOper==sampleID) //SAMPLEID?
								{
									dontignoregenerators = 0; //Ignore all generators after the SAMPLEID generator!
								}
							}
						}
						++CurrentGen;
					}
				}
			}
		}
	}
	return (found&&valid); //Not found or last found!
}

byte lookupPresetByInstrument(RIFFHEADER *sf, word preset, word bank, uint_32 *result)
{
	uint_32 currentpreset;
	sfPresetHeader activepreset; //Current preset data!
	byte foundpreset; //Found a preset?
trypreset0:
	foundpreset = 0; //Didn't find any preset!
	memset(&activepreset,0,sizeof(activepreset)); //init to something at least!
	for (currentpreset=0;currentpreset<0xFFFFFFFF;) //Check for the correct preset!
	{
		if (getSFPreset(sf,currentpreset,&activepreset)) //Get the preset!
		{
			if (isValidPreset(&activepreset)) //Valid?
			{
				if (LE16(activepreset.wBank)==bank) //Bank found?
				{
					if (LE16(activepreset.wPreset)==preset) //Program/preset found?
					{
						foundpreset = 1; //Found a preset!
						break; //Stop searching!
					}
				}
			}
		}
		else //Not found?
		{
			break; //Stop searching: bank/preset not found!
		}
		++currentpreset; //Do next preset!
	}
	
	if ((!isValidPreset(&activepreset)) || (foundpreset==0)) //Not found or invalid?
	{
		if (preset) //Gotten a preset? Default to preset 0 as by GM specification!
		{
			preset = 0; //Try the first preset!
			goto trypreset0; //Try the first preset before giving up!
		}
		return 0; //Invalid preset: disabled?
	}

	if ((LE16(activepreset.wBank)!=bank) || (LE16(activepreset.wPreset)!=preset))
	{
		return 0; //Unfound preset: disabled!
	}
	
	*result = currentpreset; //Set the preset found!
	return 1; //We've found our preset!
}

byte lookupPBagByMIDIKey(RIFFHEADER *sf, uint_32 preset, byte MIDIKey, byte MIDIVelocity, word *result, int_32 previousPBag)
{
	word PBag; //Preset(instrument) bag!
	sfGenList pgen, pgen2;
	sfPresetHeader currentpreset;
	byte gotpgen;

	if (getSFPreset(sf,preset,&currentpreset)) //Found?
	{
		PBag = LE16(currentpreset.wPresetBagNdx); //Load the first preset bag!
		for (;isValidPresetZone(sf,preset,PBag);) //Valid zone?
		{
			if ((previousPBag >= 0) && (PBag <= previousPBag)) //To skip?
			{
				goto skipPBag; //Skip this PBag for the lookup!
			}
			if (!isGlobalPresetZone(sf,preset,PBag)) //Not a global zone?
			{
				gotpgen = lookupSFPresetGen(sf,preset,PBag,velRange,&pgen2); //Velocity lookup!
				if (lookupSFPresetGen(sf,preset,PBag,keyRange,&pgen)) //Key range lookup! Found?
				{
					if ((MIDIKey>=pgen.genAmount.ranges.byLo) && (MIDIKey<=pgen.genAmount.ranges.byHi)) //Key within range?
					{
						if ((!gotpgen) || (gotpgen && (MIDIVelocity>=pgen2.genAmount.ranges.byLo) && (MIDIVelocity<=pgen2.genAmount.ranges.byHi))) //Velocity match or no velocity filter?
						{
							if (lookupSFPresetGen(sf,preset,PBag,instrument,&pgen)) //Gotten an instrument to play?
							{
								*result = PBag; //It's this PBag!
								return 1; //Found!
							}
						}
					}
					//No valid velocity/key!
				}
				else //Not found and not global(choosable)? By default it's the complete range of keys!
				{
					if ((!gotpgen) || (gotpgen && (MIDIVelocity >= pgen2.genAmount.ranges.byLo) && (MIDIVelocity <= pgen2.genAmount.ranges.byHi))) //Velocity match or no velocity filter?
					{
						if (lookupSFPresetGen(sf, preset, PBag, instrument, &pgen)) //Gotten an instrument to play?
						{
							*result = PBag; //It's this PBag!
							return 1; //Found!
						}
					}
				}
			}
			skipPBag:
			++PBag; //Next zone!
		}
	}
	return 0; //Not found!
}

byte lookupIBagByMIDIKey(RIFFHEADER *sf, word instrument, byte MIDIKey, byte MIDIVelocity, word *result, byte RequireInstrument, int_32 previousIBag)
{
	word IBag; //Instrument(note) bag!
	sfInstGenList igen, igen2;
	sfInst currentinstrument;
	byte gotigen;
	byte exists;

	if (getSFInstrument(sf,instrument,&currentinstrument)) //Found?
	{
		IBag = LE16(currentinstrument.wInstBagNdx); //Load the first preset bag!
		for (;isValidInstrumentZone(sf,instrument,IBag);) //Valid zone?
		{
			if ((previousIBag >= 0) && (IBag <= previousIBag)) //To skip?
			{
				goto skipIBag; //Skip this PBag for the lookup!
			}
			if (!isGlobalInstrumentZone(sf,instrument,IBag))
			{
				//Sample lookup/verification!
				sfSample sample;
				sfInstGenList sampleid;
				//Valid?
				exists = lookupSFInstrumentGen(sf,instrument,IBag,keyRange,&igen); //Key range lookup!
				if (exists) //Key range found?
				{
					exists = ((MIDIKey>=igen.genAmount.ranges.byLo) && (MIDIKey<=igen.genAmount.ranges.byHi));
				}
				else //No key range filter in there! Always valid key!
				{
					exists = 1; //Valid key!
				}
				if (exists)
				{
					exists = !RequireInstrument; //Default: invalid when instrument is required!
					if (lookupSFInstrumentGen(sf,instrument,IBag,sampleID,&sampleid)) //SAMPLEID found?
					{
						exists |= getSFSampleInformation(sf,LE16(sampleid.genAmount.wAmount),&sample); //Sample found=Valid! Otherwise, invalid and keep searching!
					}
					if (exists) //Valid IGEN?
					{
						gotigen = lookupSFInstrumentGen(sf,instrument,IBag,velRange,&igen2); //Velocity lookup!
						if (!gotigen) //No velocity filter? Take just the key filter!
						{
							*result = IBag; //It's this PBag!
							return 1; //Found!
						}
						else //Gotten a velocity filter?
						{
							exists = ((MIDIKey >= igen.genAmount.ranges.byLo) && (MIDIKey <= igen.genAmount.ranges.byHi));
							if (exists) //Valid velocity range?
							{
								*result = IBag; //It's this PBag!
								return 1; //Found!
							}
						}
					}
					//No valid velocity/key/sampleid!
				}
			}
			skipIBag:
			++IBag; //Next zone!
		}
	}
	return 0; //Not found!
}

//Global lookup support for supported entries!

byte lookupSFPresetModGlobal(RIFFHEADER *sf, uint_32 preset, word PBag, SFModulator sfModDestOper, word index, byte *isGlobal, sfModList *result, int_32* originMod, int_32* foundindex, word* resultindex)
{
	byte tempresult;
	sfPresetHeader currentpreset;
	word GlobalPBag;
	*isGlobal = 0; //Not global!
	if ((tempresult = lookupSFPresetMod(sf,preset,PBag,sfModDestOper,index,result,originMod,foundindex,resultindex))!=0) //Found normally?
	{
		return tempresult; //Found normally!
	}
	if (getSFPreset(sf,preset,&currentpreset)) //Found?
	{
		GlobalPBag = LE16(currentpreset.wPresetBagNdx); //Load the first preset bag!
		if (isValidPresetZone(sf,preset,GlobalPBag)) //Valid zone?
		{
			if (isGlobalPresetZone(sf,preset,GlobalPBag)) //Global zone?
			{
				if ((tempresult = lookupSFPresetMod(sf,preset,GlobalPBag,sfModDestOper,index,result,originMod,foundindex,resultindex))) //Global found?
				{
					*isGlobal = 1; //Changed to global if not before!
					return tempresult; //Global found!
				}
			}
		}
	}
	return 0; //Not found at all!
}

byte lookupSFPresetGenGlobal(RIFFHEADER *sf, word preset, word PBag, SFGenerator sfGenOper, sfGenList *result)
{
	sfPresetHeader currentpreset;
	word GlobalPBag;
	if (lookupSFPresetGen(sf,preset,PBag,sfGenOper,result)) //Found normally?
	{
		return 1; //Found normally!
	}
	if (getSFPreset(sf,preset,&currentpreset)) //Found?
	{
		GlobalPBag = LE16(currentpreset.wPresetBagNdx); //Load the first preset bag!
		if (isValidPresetZone(sf,preset,GlobalPBag)) //Valid zone?
		{
			if (isGlobalPresetZone(sf,preset,GlobalPBag)) //Global zone?
			{
				if (lookupSFPresetGen(sf,preset,GlobalPBag,sfGenOper,result)) //Global found?
				{
					return 1; //Global found!
				}
			}
		}
	}
	return 0; //Not found at all!
}

byte lookupSFInstrumentModGlobal(RIFFHEADER *sf, uint_32 instrument, word IBag, SFModulator sfModDestOper, word index, byte *isGlobal, sfModList *result, int_32* originMod, int_32 *foundindex, word* resultindex)
{
	sfInst currentinstrument;
	word GlobalIBag;
	byte tempresult;
	*isGlobal = 0; //Not global!
	if ((tempresult = lookupSFInstrumentMod(sf,instrument,IBag,sfModDestOper,index,result,originMod,foundindex,resultindex))!=0) //Found normally?
	{
		return tempresult; //Found normally!
	}
	if (getSFInstrument(sf,instrument,&currentinstrument)) //Found?
	{
		GlobalIBag = LE16(currentinstrument.wInstBagNdx); //Load the first preset bag!
		if (isValidPresetZone(sf,instrument,GlobalIBag)) //Valid zone?
		{
			if (isGlobalPresetZone(sf,instrument,GlobalIBag)) //Global zone?
			{
				if ((tempresult = lookupSFInstrumentMod(sf,instrument,GlobalIBag,sfModDestOper,index,result,originMod,foundindex,resultindex))!=0) //Global found?
				{
					*isGlobal = 1; //Global!
					return tempresult; //Global found!
				}
			}
		}
	}
	return 0; //Not found at all!
}

byte lookupSFInstrumentGenGlobal(RIFFHEADER *sf, word instrument, word IBag, SFGenerator sfGenOper, sfInstGenList *result)
{
	sfInst currentinstrument;
	word GlobalIBag;
	if (lookupSFInstrumentGen(sf,instrument,IBag,sfGenOper,result)) //Found normally?
	{
		return 1; //Found normally!
	}
	if (getSFInstrument(sf,instrument,&currentinstrument)) //Found?
	{
		GlobalIBag = LE16(currentinstrument.wInstBagNdx); //Load the first preset bag!
		if (isValidInstrumentZone(sf,instrument,GlobalIBag)) //Valid zone?
		{
			if (isGlobalInstrumentZone(sf,instrument,GlobalIBag)) //Global zone?
			{
				if (lookupSFInstrumentGen(sf,instrument,GlobalIBag,sfGenOper,result)) //Global found?
				{
					return 1; //Global found!
				}
			}
		}
	}
	return 0; //Not found at all!
}
