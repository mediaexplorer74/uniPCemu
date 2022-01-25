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

#include "headers/types.h" //Only global stuff!
#include "headers/fopen64.h" //64-bit fopen support!
#include "headers/emu/directorylist.h" //Directory list support.
#include "headers/emu/gpu/gpu_text.h" //For locking the text surface!
#include "headers/hardware/ide.h" //Geometry support!
#include "headers/emu/gpu/gpu_emu.h" //Text locking support!

//A dynamic image .DAT data:
byte SIG[7] = {'S','F','D','I','M','G','\0'}; //Signature!
byte EXTSIG[7] = {'S','F','D','I','M','E','\0'}; //Signature of an extended disk image!

#include "headers/packed.h"
typedef struct PACKED
{
byte SIG[7]; //SFDIMG\0
uint_32 headersize; //The size of this header!
int_64 filesize; //The size of the dynamic image, in sectors.
word sectorsize; //The size of a sector (512)
int_64 firstlevellocation; //The location of the first level, in bytes!
int_64 currentsize; //The current file size, in bytes!
} DYNAMICIMAGE_HEADER; //Dynamic image .DAT header.
#include "headers/endpacked.h"

#include "headers/packed.h"
typedef struct PACKED
{
byte SIG[7]; //SFDIMG\0
uint_32 headersize; //The size of this header!
int_64 filesize; //The size of the dynamic image, in sectors.
word sectorsize; //The size of a sector (512)
int_64 firstlevellocation; //The location of the first level, in bytes!
int_64 currentsize; //The current file size, in bytes!
int_64 extendedinformationblocklocation; //The location of an Extended Information Block
} EXTENDEDDYNAMICIMAGE_HEADER; //Extended Dynamic image .DAT header.
#include "headers/endpacked.h"

#include "headers/packed.h"
typedef struct PACKED
{
uint_32 type; //0=format
word format; //Format! 0=UniPCemu compatible mode. 1=UniPCemu minimal mode. 2=Bochs mode, .
int_64 nextrecord; //Next record location. 0=None.
byte padding[512-14]; //Padding to 512 bytes!
} EXTENDEDDYNAMICIMAGE_FORMATBLOCK; //Extended Dynamic image format block.
#include "headers/endpacked.h"


typedef struct
{
	byte SIG[7]; //SFDIMG\0
	uint_32 headersize; //The size of this header!
	int_64 filesize; //The size of the dynamic image, in sectors.
	word sectorsize; //The size of a sector (512)
	int_64 firstlevellocation; //The location of the first level, in bytes!
	int_64 currentsize; //The current file size, in bytes!
} PADDEDDYNAMICIMAGE_HEADER; 

byte emptylookuptable_ready = 0;
int_64 emptylookuptable[4096]; //A full sector lookup table (4096 entries for either block (1024) or sector (4096) lookup)!

OPTINLINE byte writedynamicheader(BIGFILE *f, DYNAMICIMAGE_HEADER *header)
{
	if (!f) return 0; //Failed!
	if (emufseek64(f, 0, SEEK_SET) != 0)
	{
		return 0; //Failed to seek to position 0!
	}
	if (emufwrite64(header, 1, sizeof(*header), f) != sizeof(*header)) //Failed to write?
	{
		return 0; //Failed!
	}
	if (emufflush64(f)) //Error when flushing?
	{
		return 0; //We haven't been updated!
	}
	return 1; //We've been updated!
}

OPTINLINE int_64 readdynamicheader_extensionlocation(BIGFILE *f, EXTENDEDDYNAMICIMAGE_HEADER *header) //Read the extension location!
{
	if (!emptylookuptable_ready) //Not allocated yet?
	{
		memset(&emptylookuptable, 0, sizeof(emptylookuptable)); //Initialise the lookup table!
		emptylookuptable_ready = 1; //Ready!
	}
	if (f)
	{
		if (emufseek64(f, 0, SEEK_SET) != 0)
		{
			return 0; //Failed to seek to position 0!
		}
		if (emufread64(header, 1, sizeof(*header), f) == sizeof(*header)) //Read the new header?
		{
			char *sig = (char *)&header->SIG; //The signature!
			if ((!memcmp(sig, &EXTSIG, sizeof(header->SIG))) && (header->headersize == sizeof(*header)) //Extended header?
				) //(New) dynamic image header?
			{
				return header->extendedinformationblocklocation; //Is dynamic extended image! Give the location, if used!
			}
		}
		return 0; //Valid file, not a dynamic image!
	}
	return 0; //Not found!
}

OPTINLINE byte readdynamicheader(BIGFILE *f, DYNAMICIMAGE_HEADER *header)
{
	PADDEDDYNAMICIMAGE_HEADER oldheader; //The older header data!
	EXTENDEDDYNAMICIMAGE_HEADER extendedheader; //The newest extended header data!
	if (!emptylookuptable_ready) //Not allocated yet?
	{
		memset(&emptylookuptable,0,sizeof(emptylookuptable)); //Initialise the lookup table!
		emptylookuptable_ready = 1; //Ready!
	}
	if (f)
	{
		if (emufseek64(f, 0, SEEK_SET) != 0)
		{
			return 0; //Failed to seek to position 0!
		}
		if (emufread64(&oldheader,1,sizeof(oldheader),f)==sizeof(oldheader)) //Read the unpadded header?
		{
			char *sig = (char *)&oldheader.SIG; //The signature!
			if ((!memcmp(sig,&SIG,sizeof(oldheader.SIG))) && (oldheader.headersize==sizeof(oldheader))) //Dynamic image?
			{
				//Copy valid data to the new structured header!
				memset(header,0,sizeof(*header)); //Initialise the new header for filling!
				memcpy(&header->SIG,&SIG,sizeof(header->SIG)); //Create the new signature in the new header!
				header->currentsize = oldheader.currentsize;
				header->filesize = oldheader.filesize;
				header->firstlevellocation = oldheader.firstlevellocation;
				header->headersize = sizeof(*header); //Patch the size to the new size!
				header->sectorsize = oldheader.sectorsize;
				return 1; //Is dynamic (compatibility mode)!
			}
		}
		if (emufseek64(f, 0, SEEK_SET) != 0)
		{
			return 0; //Failed to seek to position 0!
		}
		if (emufread64(header, 1, sizeof(*header), f) == sizeof(*header)) //Read the new header?
		{
			char *sig = (char *)&header->SIG; //The signature!
			if (((!memcmp(sig, &SIG, sizeof(header->SIG))) && (header->headersize == sizeof(*header))) //Normal header?
				|| ((!memcmp(sig, &EXTSIG, sizeof(header->SIG))) && (header->headersize == sizeof(extendedheader))) //Extended header?
				)//(New) dynamic image header?
			{
				int_64 extensionlocation;
				if ((extensionlocation = readdynamicheader_extensionlocation(f,&extendedheader))!=0) //Extended image data is supported!
				{
					if (emufseek64(f, extensionlocation, SEEK_SET) != 0)
					{
						return 0; //Failed to seek to position 0!
					}
					EXTENDEDDYNAMICIMAGE_FORMATBLOCK formatblock;
					if (emufread64(&formatblock,1,sizeof(formatblock),f)==sizeof(formatblock)) //Read the formatblock?
					{
						if (formatblock.type) //Unsupported type?
						{
							return 0; //Isn't supported yet!
						}
						if (formatblock.nextrecord) //More records?
						{
							return 0; //Isn't supported yet!
						}
						if (formatblock.format>2) //Unsupported format?
						{
							return 0; //Isn't supported yet!
						}
						return 1+formatblock.format; //Give format+1. 0=Compatible UniPCemu, 1=Bochs compatible disk(63x16), 2=Largest CHS disk(autodetect).
					}
				}
				return 1; //Is dynamic!
			}
		}
		return 0; //Valid file, not a dynamic image!
	}
	return 0; //Not found!
}

int is_dynamicimage(char *filename)
{
	int result;
	DYNAMICIMAGE_HEADER header; //Header to read!
	if (strcmp(filename, "") == 0) return 0; //Unexisting: don't even look at it!
	if (!isext(filename, "sfdimg")) //Not our dynamic image file?
	{
		return 0; //Not a dynamic image!
	}
	BIGFILE *f = emufopen64(filename, "rb"); //Open!
	result = readdynamicheader(f,&header); //Is dynamic?
	emufclose64(f);
	return result; //Give the result!
}

FILEPOS dynamicimage_getsize(char *filename)
{
	DYNAMICIMAGE_HEADER header; //Header to read!
	BIGFILE *f = emufopen64(filename, "rb"); //Open!
	FILEPOS result;
	if (readdynamicheader(f,&header)) //Is dynamic?
	{
		result = header.filesize*header.sectorsize; //Give the size!
	}
	else
	{
		result = 0; //No size!
	}
	emufclose64(f);
	return result; //Give the result!
}

byte dynamicimage_getgeometry(char *filename, word *cylinders, word *heads, word *SPT)
{
	uint_64 disk_size = (dynamicimage_getsize(filename)>>9); //Sector count!
	switch (is_dynamicimage(filename)) //What type?
	{
		case 1: //UniPCemu format?
			HDD_classicGeometry(disk_size,cylinders,heads,SPT); //Apply classic geometry!
			return 1; //OK!
			break;
		case 2: //Auto minimal type?
			HDD_detectOptimalGeometry(disk_size,cylinders,heads,SPT); //Apply optimal geometry!
			return 1; //OK!
			break;
		case 3: //Bochs CHS?
			*heads = 16;
			*SPT = 63;
			*cylinders = (word)(MAX(MIN((disk_size/(63*16)),0xFFFF),1));
			return 1; //OK!
			break;
		default:
			break;
		}
		return 0; //Not retrieved!
}

byte dynamictostatic_imagetype(char *filename)
{
	switch (is_dynamicimage(filename)) //What type?
	{
	case 1: //UniPCemu format?
		return 2; //OK!
		break;
	case 2: //Auto minimal type?
		return 3; //OK!
		break;
	case 3: //Bochs CHS?
		return 1; //OK!
		break;
	default:
		break;
	}
	return 0; //Not retrieved!
}

OPTINLINE byte dynamicimage_updatesize(BIGFILE *f, int_64 size)
{
	DYNAMICIMAGE_HEADER header;
	if (!readdynamicheader(f, &header)) //Header failed to read?
	{
		return 0; //Failed to update the size!
	}
	header.currentsize = size; //Update the size!
	return writedynamicheader(f,&header); //Try to update the header!
}

OPTINLINE byte dynamicimage_allocatelookuptable(BIGFILE *f, int_64 *location, int_64 numentries) //Allocate a table with numentries entries, give location of allocation!
{
	DYNAMICIMAGE_HEADER header;
	int_64 newsize, entrysize;
	if (readdynamicheader(f, &header))
	{
		if (emufseek64(f, header.currentsize, SEEK_SET) != 0) //Error seeking to EOF?
		{
			return 0; //Error!
		}
		//We're at EOF!
		*location = header.currentsize; //The location we've found to use!
		entrysize = sizeof(emptylookuptable[0]) * numentries; //Size of the entry!
		if (emufwrite64(&emptylookuptable, 1, entrysize, f) == entrysize) //Block table allocated?
		{
			if (emufflush64(f)) //Error when flushing?
			{
				return 0; //We haven't been updated!
			}
			newsize = emuftell64(f); //New file size!
			return dynamicimage_updatesize(f, newsize); //Size successfully updated?
		}
	}
	return 0; //Error!
}

OPTINLINE int_64 dynamicimage_readlookuptable(BIGFILE *f, int_64 location, int_64 numentries, int_64 entry) //Read a table with numentries entries, give location of an entry!
{
	int_64 result;
	if (entry >= numentries) return 0; //Invalid entry: out of bounds!
	if (emufseek64(f, location+(entry*sizeof(int_64)), SEEK_SET) != 0) //Error seeking to entry?
	{
		return 0; //Error!
	}
	//We're at EOF!
	if (emufread64(&result, 1, sizeof(result), f) == sizeof(result)) //Block table read?
	{
		return result; //Give the entry!
	}
	return 0; //Error: not readable!
}

OPTINLINE byte dynamicimage_updatelookuptable(BIGFILE *f, int_64 location, int_64 numentries, int_64 entry, int_64 value) //Update a table with numentries entries, set location of an entry!
{
	DYNAMICIMAGE_HEADER header;
	if (readdynamicheader(f, &header)) //Check the image first!
	{
		if (entry >= numentries) return 0; //Invalid entry: out of bounds!
		if (emufseek64(f, location+(entry*sizeof(int_64)), SEEK_SET) != 0) //Error seeking to entry?
		{
			return 0; //Error!
		}
		//We're at the entry!
		if (emufwrite64(&value, 1, sizeof(int_64), f) == sizeof(int_64)) //Updated?
		{
			if (emufflush64(f)) //Error when flushing?
			{
				return 0; //We haven't been updated!
			}
			return 1; //Updated!
		}
	}
	return 0; //Error: not found!
}

byte lookuptabledepth = 0; //Lookup table depth found?

OPTINLINE int_64 dynamicimage_getindex(BIGFILE *f, uint_32 sector) //Get index!
{
	DYNAMICIMAGE_HEADER header;
	int_64 index;
	lookuptabledepth = 0; //Default: nothing found!
	if (!readdynamicheader(f, &header)) //Not dynamic?
	{
		return -1; //Error: not dynamic!
	}
	if (!header.firstlevellocation) return 0; //Not present: no first level lookup table!
	lookuptabledepth = 1; //First level present, but unused!
	if (!(index = dynamicimage_readlookuptable(f, header.firstlevellocation, 1024, ((sector >> 22) & 0x3FF)))) //First level lookup!
	{
		return 0; //Not present!
	}
	lookuptabledepth = 2; //Second level present, but unused!
	if (!(index = dynamicimage_readlookuptable(f, index, 1024, ((sector >> 12) & 0x3FF)))) //Second level lookup!
	{
		return 0; //Not present!
	}
	lookuptabledepth = 3; //Third level present and checked!
	if (!(index = dynamicimage_readlookuptable(f, index, 4096, (sector & 0xFFF)))) //Sector level lookup!
	{
		return 0; //Not present!
	}
	return index; //We're present at this index, if at all!
}

OPTINLINE sbyte dynamicimage_datapresent(BIGFILE *f, uint_32 sector) //Get present?
{
	int_64 index;
	index = dynamicimage_getindex(f, sector); //Try to get the index!
	if (index == -1) //Not a dynamic image?
	{
		return -1; //Invalid file!
	}
	return (index!=0); //We're present?
}

OPTINLINE byte dynamicimage_setindex(BIGFILE *f, uint_32 sector, int_64 index)
{
	DYNAMICIMAGE_HEADER header;
	int_64 firstlevellocation,secondlevellocation,sectorlevellocation;
	int_64 firstlevelentry, secondlevelentry, sectorlevelentry;
	firstlevelentry = ((sector >> 22) & 0x3FF); //First level entry!
	secondlevelentry = ((sector >> 12) & 0x3FF); //Second level entry!
	sectorlevelentry = (sector & 0xFFF); //Sector level entry!

	if (!readdynamicheader(f, &header)) //Not dynamic?
	{
		return -1; //Error: not dynamic!
	}
	firstlevellocation = header.firstlevellocation; //First level location!
	//First, check the first level lookup table is present!
	if (!firstlevellocation) //No first level present yet?
	{
		if (!dynamicimage_allocatelookuptable(f, &firstlevellocation, 1024)) //Lookup table failed to allocate?
		{
			dynamicimage_updatesize(f, header.currentsize); //Revert!
			return 0; //Failed!
		}
		if (!readdynamicheader(f, &header)) //Update header?
		{
			return 0; //Failed!
		}
		header.firstlevellocation = firstlevellocation; //Update the first level location!
		if (!writedynamicheader(f, &header)) //Header failed to update?
		{
			return 0; //Failed: we can't process the dynamic image header!
		}
	}
	//We're present: process the first level lookup table!
	if (!(secondlevellocation = dynamicimage_readlookuptable(f, firstlevellocation, 1024, firstlevelentry))) //First level lookup failed?
	{
		if (!dynamicimage_allocatelookuptable(f, &secondlevellocation, 1024)) //Lookup table failed to allocate?
		{
			dynamicimage_updatesize(f, header.currentsize); //Revert!
			return 0; //Failed!
		}
		if (!dynamicimage_updatelookuptable(f, firstlevellocation, 1024,firstlevelentry,secondlevellocation)) //Lookup table failed to assign?
		{
			dynamicimage_updatesize(f, header.currentsize); //Revert!
			return 0; //Failed!
		}
		if (!readdynamicheader(f, &header)) //Update header?
		{
			return 0; //Failed!
		}
		//Now, allow the next level to be updated: we're ready to process!
	}
	if (!(sectorlevellocation = dynamicimage_readlookuptable(f, secondlevellocation, 1024,secondlevelentry))) //Second level lookup failed?
	{
		if (!dynamicimage_allocatelookuptable(f, &sectorlevellocation, 4096)) //Lookup table failed to allocate?
		{
			dynamicimage_updatesize(f, header.currentsize); //Revert!
			return 0; //Failed!
		}
		if (!dynamicimage_updatelookuptable(f, secondlevellocation, 4096, secondlevelentry, sectorlevellocation)) //Lookup table failed to assign?
		{
			dynamicimage_updatesize(f, header.currentsize); //Revert!
			return 0; //Failed!
		}
		if (!readdynamicheader(f, &header)) //Update header?
		{
			return 0; //Failed!
		}
		//Now, allow the next level to be updated: we're ready to process!
	}
	if (!dynamicimage_updatelookuptable(f, sectorlevellocation, 4096, sectorlevelentry, index)) //Update the lookup table, if possible!
	{
		dynamicimage_updatesize(f, header.currentsize); //Revert!
		return 0; //Failed!
	}
	return 1; //We've succeeded: the sector has been allocated and set!
}

byte dynamicimage_writesector(char *filename,uint_32 sector, void *buffer) //Write a 512-byte sector! Result=1 on success, 0 on error!
{
	DYNAMICIMAGE_HEADER header;
	static byte emptyblock[512]; //An empty block!
	static byte emptyready = 0;
	int_64 newsize;
	BIGFILE *f;
	f = emufopen64(filename, "rb+"); //Open for writing!
	if (!readdynamicheader(f, &header)) //Failed to read the header?
	{
		emufclose64(f); //Close the device!
		return FALSE; //Error: invalid file!
	}
	if (sector >= header.filesize) return FALSE; //We're over the limit of the image!
	int present = dynamicimage_datapresent(f,sector); //Data present?
	if (present!=-1) //Valid sector?
	{
		if (present) //Data present?
		{
			int_64 location;
			location = dynamicimage_getindex(f, sector); //Load the location!
			emufseek64(f,location, SEEK_SET); //Goto location!
			if (emufwrite64(buffer, 1, 512, f) != 512) //Write sector always!
			{
				emufclose64(f);
				return FALSE; //We haven't been updated!
			}
			if (emufflush64(f)) //Error when flushing?
			{
				emufclose64(f); //Close!
				return FALSE; //We haven't been updated!
			}
			//Written correctly, passthrough!
		}
		else //Not written yet?
		{
			if (!emptyready)
			{
				memset(&emptyblock,0,sizeof(emptyblock)); //To detect an empty block!
				emptyready = 1; //We're ready to be used!
			}
			if (!memcmp(&emptyblock,buffer,sizeof(emptyblock))) //Empty?
			{
				emufclose64(f); //Close the device!
				return TRUE; //We don't need to allocate/write an empty block, as it's already empty by default!
			}
			if (dynamicimage_setindex(f, sector, 0)) //Assign to not allocated!
			{
				if (readdynamicheader(f, &header)) //Header updated?
				{
					if (emufseek64(f, header.currentsize, SEEK_SET)) //Goto EOF!
					{
						emufclose64(f);
						return FALSE; //Error: couldn't goto EOF!
					}
					if (emuftell64(f) != header.currentsize) //Failed going to EOF?
					{
						emufclose64(f);
						return FALSE; //Error: couldn't goto EOF!
					}
					if (emufwrite64(buffer, 1, 512, f) == 512) //Write the buffer to the file!
					{
						if (emufflush64(f)) //Error when flushing?
						{
							emufclose64(f);
							return FALSE; //Error: couldn't flush!
						}
						newsize = emuftell64(f); //New file size!
						if (dynamicimage_updatesize(f, newsize)) //Updated the size?
						{
							if (dynamicimage_setindex(f, sector, header.currentsize)) //Assign our newly allocated block!
							{
								emufclose64(f); //Close the device!
								return TRUE; //OK: we're written!
							}
							else //Failed to assign?
							{
								dynamicimage_updatesize(f, header.currentsize); //Reverse sector allocation!
							}
							emufclose64(f); //Close the device!
							return FALSE; //An error has occurred: couldn't finish allocating the block!
						}
						emufclose64(f); //Close it!
						return FALSE; //ERROR!
					}
				}
				emufclose64(f); //Close the device!
				return FALSE; //Error!
			}
			emufclose64(f); //Close the device!
			return FALSE; //Error!
		}
	}
	else //Terminate loop: invalid sector!
	{
		emufclose64(f); //Close the device!
		return FALSE; //Error!
	}
	emufclose64(f); //Close the device!
	return TRUE; //Written!
}

byte dynamicimage_readsector(char *filename,uint_32 sector, void *buffer) //Read a 512-byte sector! Result=1 on success, 0 on error!
{
	DYNAMICIMAGE_HEADER header;
	BIGFILE *f;
	f = emufopen64(filename, "rb"); //Open!
	if (!readdynamicheader(f, &header)) //Failed to read the header?
	{
		emufclose64(f); //Close the device!
		return FALSE; //Error: invalid file!
	}
	if (sector >= header.filesize)
	{
		emufclose64(f); //Close the device!
		return FALSE; //We're over the limit of the image!
	}

	int present = dynamicimage_datapresent(f,sector); //Data present?
	if (present!=-1) //Valid sector?
	{
		if (present) //Data present?
		{
			int_64 index;
			index = dynamicimage_getindex(f,sector);
			if (emufseek64(f,index,SEEK_SET)) //Seek failed?
			{
				emufclose64(f);
				return FALSE; //Error: file is corrupt?
			}
			if (emufread64(buffer,1,512,f)!=512) //Error reading sector?
			{
				emufclose64(f);
				return FALSE; //Error: file is corrupt?
			}
		}
		else //Present, but not written yet?
		{
			memset(buffer,0,512); //Empty sector!
		}
	}
	else //Terminate loop: invalid sector!
	{
		emufclose64(f);
		return FALSE; //Error!
	}
	emufclose64(f); //Close it!
	return TRUE; //Read!
}

byte dynamicimage_readexistingsector(char *filename,uint_32 sector, void *buffer) //Has a 512-byte sector! Result=1 on present&filled(buffer filled), 0 on not present or error! Used for simply copying the sector to a different image!
{
	DYNAMICIMAGE_HEADER header;
	BIGFILE *f;
	f = emufopen64(filename, "rb"); //Open!
	if (!readdynamicheader(f, &header)) //Failed to read the header?
	{
		emufclose64(f); //Close the device!
		return FALSE; //Error: invalid file!
	}
	if (sector >= header.filesize)
	{
		emufclose64(f); //Close the device!
		return FALSE; //We're over the limit of the image!
	}
	
	int present = dynamicimage_datapresent(f,sector); //Data present?
	if (present!=-1) //Valid sector?
	{
		if (present) //Data present?
		{
			int_64 index;
			index = dynamicimage_getindex(f,sector);
			if (emufseek64(f,index+512,SEEK_SET)) //Seek failed?
			{
				emufclose64(f);
				return FALSE; //Error: file is corrupt?
			}
			if (emufseek64(f,index,SEEK_SET)) //Seek failed?
			{
				emufclose64(f);
				return FALSE; //Error: file is corrupt?				
			}
			if (emufread64(buffer,1,512,f)!=512) //Read failed?
			{
				emufclose64(f);
				return FALSE; //Error: file is corrupt?								
			}
			emufclose64(f);
			//We exist! Buffer contains the data!
			return (!memcmp(&emptylookuptable,buffer,512))?FALSE:TRUE; //Do we exist and are filled is the result!
		}
		else //Present, but not written yet?
		{
			emufclose64(f);
			return FALSE; //Empty sector: not present!
		}
	}
	else //Terminate loop: invalid sector!
	{
		emufclose64(f);
		return FALSE; //Error!
	}
	emufclose64(f); //Close it!
	return FALSE; //Not present by default!
}

byte dummydata[512]; //Loaded sector!

sbyte dynamicimage_nextallocatedsector(char *filename, uint_32 *sector) //Finds the next allocated sector which isn't empty!
{
	DYNAMICIMAGE_HEADER header;
	BIGFILE *f;
	f = emufopen64(filename, "rb"); //Open!
	if (!readdynamicheader(f, &header)) //Failed to read the header?
	{
		emufclose64(f); //Close the device!
		return -1; //Error: invalid file!
	}
	if (*sector >= header.filesize) //Primary limit check!
	{
		emufclose64(f); //Close the device!
		return -1; //We're over the limit of the image!
	}
	
	++*sector; //The next sector to check!
	int present = dynamicimage_datapresent(f,*sector); //Data present?
	if (present==1) //Existing sector?
	{
		if (!dynamicimage_readexistingsector(filename,*sector,&dummydata)) //Empty buffer?
		{
			present = 0; //Not counted as present!
		}
	}
	for (;(present!=-1) && (!present);) //Check while not present and valid sector!
	{
		if (shuttingdown()) //Shutting down?
		{
			present = -2; //Special: Aborting!
			break; //Aborting!
		}
		if (*sector >= header.filesize) //EOF reached
		{
			present = 0; //Nothing present, because of EOF!
			break; //Stop searching!
		}
		switch (lookuptabledepth) //What depth has been detected?
		{
			case 1: //First level present, but unused!
				*sector = ((*sector+0x400000)&~0x3FFFFF); //Skip to the start of the next sector block(up to 4096*1024 sectors)!
				break;
			case 2: //Second level present, but unused!
				*sector = ((*sector+0x1000)&~0xFFF); //Skip to the start of the next sector block(up to 4096 sectors)!
				break;
			default: //Normal handling, sector by sector!
			case 3: //Third level present, but unused!
				++*sector; //Skip one sector only!
				break;
		}
		present = dynamicimage_datapresent(f,*sector); //Data present/valid for this sector?
		if (present==1) //Existing sector? Might be empty!
		{
			if (!dynamicimage_readexistingsector(filename,*sector,&dummydata)) //Empty buffer?
			{
				present = 0; //Not counted as present!
			}
		}
	}
	emufclose64(f); //Close it!
	return (sbyte)present; //Next sector that is (un)allocated! -1=Invalid file, 0=Nothing present anymore(EOF reached during search), 1=
}

extern char diskpath[256]; //Disk path!

FILEPOS generateDynamicImage(char *filename, FILEPOS size, int percentagex, int percentagey, byte format)
{
	EXTENDEDDYNAMICIMAGE_HEADER header;
	BIGFILE *f;
	EXTENDEDDYNAMICIMAGE_FORMATBLOCK formatblock;

	char fullfilename[256];
	memset(&fullfilename,0,sizeof(fullfilename)); //Init!
	safestrcpy(fullfilename,sizeof(fullfilename), diskpath); //Disk path!
	safestrcat(fullfilename,sizeof(fullfilename), "/");
	safestrcat(fullfilename,sizeof(fullfilename), filename); //The full filename!

	if ((percentagex!=-1) && (percentagey!=-1)) //To show percentage?
	{
		EMU_locktext();
		GPU_EMU_printscreen(percentagex,percentagey,"%2.1f%%",0.0f); //Show first percentage!
		EMU_unlocktext();
	}

	FILEPOS numblocks;
	numblocks = size;
	numblocks >>= 9; //Divide by 512 for the ammount of sectors!

	if (size != 0) //Has size?
	{
		f = emufopen64(fullfilename, "wb"); //Start generating dynamic info!
		memcpy(&header.SIG,EXTSIG,sizeof(header.SIG)); //Set the signature!
		header.headersize = sizeof(header); //The size of the header to validate!
		header.filesize = numblocks; //Ammount of blocks!
		header.sectorsize = 512; //512 bytes per sector!
		header.currentsize = sizeof(header)+sizeof(formatblock); //The current file size. This is updated as data is appended to the file.
		header.firstlevellocation = 0; //No first level createn yet!
		header.extendedinformationblocklocation = sizeof(header); //We have extended information!
		memset(&formatblock,0,sizeof(formatblock)); //Init format block!
		formatblock.format = ((format<=3)&&format)?(format-1):0; //CHS mode! Default to compatible mode!
		if (formatblock.format==0) //Compatible format? Fallback to not use an extended information block for simple compatiblity!
		{
			header.extendedinformationblocklocation = 0; //Disable extended information block for compatiblity with older UniPCemu versions!
			header.currentsize = sizeof(header); //The new current file size as required. Don't add the extended block size, as it isn't there!
		}
		if (emufwrite64(&header,1,sizeof(header),f)!=sizeof(header)) //Failed to write the header?
		{
			emufclose64(f); //Close the file!
			return 0; //Error: couldn't write the header!
		}
		if (header.extendedinformationblocklocation) //Use extended information block?
		{
			if (emufwrite64(&formatblock,1,sizeof(formatblock),f)!=sizeof(formatblock)) //Failed to write the format block?
			{
				emufclose64(f); //Close the file!
				return 0; //Error: couldn't write the header!
			}
		}
		emufclose64(f); //Close info file!
	}
	
	if ((percentagex!=-1) && (percentagey!=-1)) //To show percentage?
	{
		EMU_locktext();
		GPU_EMU_printscreen(percentagex,percentagey,"%2.1f%%",100.0f); //Show final percentage!
		EMU_unlocktext();
	}
	return (numblocks<<9); //Give generated size!
}
