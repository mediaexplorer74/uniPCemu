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

#include "headers/basicio/dskimage.h" //Our header!
#include "headers/emu/directorylist.h" //isext() support!
#include "headers/fopen64.h" //64-bit fopen support!

//First, functions to actually read&write the DSK file&sectors.

byte readDSKInformation(BIGFILE *f, DISKINFORMATIONBLOCK *result)
{
	byte ID[8] = { 'M', 'V', ' ', '-', ' ', 'C', 'P', 'C' }; //Identifier!
	emufseek64(f, 0, SEEK_SET); //Goto BOF!
	if (emuftell64(f) != 0) return 0; //Failed!
	if (emufread64(result, 1, sizeof(*result), f) != sizeof(*result)) return 0; //Failed!
	if (memcmp(&result->ID, &ID, sizeof(ID)) != 0) return 0; //Invalid file type!
	return 1; //OK!
}

byte readDSKTrackInformation(BIGFILE *f, byte side, byte track, DISKINFORMATIONBLOCK *info, TRACKINFORMATIONBLOCK *result)
{
	uint_32 position;
	word actualtracknr;
	actualtracknr = track;
	actualtracknr *= info->NumberOfSides; //Multiply by the number of sides!
	actualtracknr += side; //Add the side to the item to retrieve! This creates interleaved sides!
	position = sizeof(*info) + (info->TrackSize*actualtracknr);
	emufseek64(f, position, SEEK_SET); //Goto position of the track!
	if (emuftell64(f) != position) return 0; //Invalid track number!
	if (emufread64(result, 1, sizeof(*result), f) != sizeof(*result)) return 0; //Failed!
	if ((result->sidenumber != side) || (result->tracknumber != track)) return 0; //Failed: invalid side/track retrieved!
	return 1; //OK!
}

byte readDSKSectorInformation(BIGFILE *f, byte side, word track, byte sector, DISKINFORMATIONBLOCK *info, TRACKINFORMATIONBLOCK *trackinfo, SECTORINFORMATIONBLOCK *result)
{
	uint_32 position;
	word actualtracknr;
	actualtracknr = track;
	actualtracknr *= info->NumberOfSides; //Multiply by the number of sides!
	actualtracknr += side; //Add the side to the item to retrieve! This creates interleaved sides!
	position = sizeof(*info) + (info->TrackSize*actualtracknr);
	position += sizeof(*trackinfo); //We follow directly after the track information!
	position += sizeof(*result)*sector; //Add to apply the sector number!
	emufseek64(f, position, SEEK_SET); //Goto position of the track!
	if (emuftell64(f) != position) return 0; //Invalid track number!
	if (emufread64(result, 1, sizeof(*result), f) != sizeof(*result)) return 0; //Failed!
	if ((result->side != side) || (result->track != track)) return 0; //Failed: invalid side/track retrieved!
	return 1; //OK!
}

word getDSKSectorBlockSize(TRACKINFORMATIONBLOCK *trackinfo)
{
	return (word)powf((long)2, (float)trackinfo->sectorsize); //Apply sector size!
}

word getDSKSectorSize(SECTORINFORMATIONBLOCK *sectorinfo)
{
	return (word)powf((long)2, (float)sectorinfo->SectorSize); //Apply sector size!
}

byte readDSKSector(BIGFILE *f, byte side, word track, byte sector, DISKINFORMATIONBLOCK *info, TRACKINFORMATIONBLOCK *trackinfo, SECTORINFORMATIONBLOCK *sectorinfo, byte sectorsize, void *result)
{
	if (sectorinfo->SectorSize != sectorsize) return 0; //Wrong sector size!
	uint_32 position;
	word actualtracknr;
	actualtracknr = track;
	actualtracknr *= info->NumberOfSides; //Multiply by the number of sides!
	actualtracknr += side; //Add the side to the item to retrieve! This creates interleaved sides!
	position = sizeof(*info) + (info->TrackSize*actualtracknr);
	position += 100; //We always start 100 bytes after the track information block start!
	position += getDSKSectorBlockSize(trackinfo)*sector; //The start of the sector!
	emufseek64(f, position, SEEK_SET); //Goto position of the track!
	if (emuftell64(f) != position) return 0; //Invalid track number!
	if (emufread64(result, 1, getDSKSectorSize(sectorinfo), f) != getDSKSectorSize(sectorinfo)) return 0; //Failed!
	return 1; //Read!
}

byte writeDSKSector(BIGFILE *f, byte side, word track, byte sector, DISKINFORMATIONBLOCK *info, TRACKINFORMATIONBLOCK *trackinfo, SECTORINFORMATIONBLOCK *sectorinfo, byte sectorsize, void *sectordata)
{
	if (sectorinfo->SectorSize != sectorsize) return 0; //Wrong sector size!
	uint_32 position;
	word actualtracknr;
	actualtracknr = track;
	actualtracknr *= info->NumberOfSides; //Multiply by the number of sides!
	actualtracknr += side; //Add the side to the item to retrieve! This creates interleaved sides!
	position = sizeof(*info) + (info->TrackSize*actualtracknr);
	position += 100; //We always start 100 bytes after the track information block start!
	position += getDSKSectorBlockSize(trackinfo)*sector; //The start of the sector!
	emufseek64(f, position, SEEK_SET); //Goto position of the track!
	if (emuftell64(f) != position) return 0; //Invalid track number!
	if (emufwrite64(sectordata, 1, getDSKSectorSize(sectorinfo), f) != getDSKSectorSize(sectorinfo)) return 0; //Failed!
	return 1; //Read!
}

//Our interfaced functions!

byte is_DSKimage(char *filename)
{
	if (strcmp(filename, "") == 0) return 0; //Unexisting: don't even look at it!
	if (!isext(filename, "dsk")) //Not our DSK image file?
	{
		return 0; //Not a dynamic image!
	}
	BIGFILE *f;
	f = emufopen64(filename, "rb"); //Open the image!
	if (!f) return 0; //Not opened!
	DISKINFORMATIONBLOCK DSKInformation; //The read information!
	if (!readDSKInformation(f, &DSKInformation)) //Invalid header?
	{
		emufclose64(f); //Close the image!
		return 0; //Not a valid DSK file!
	}
	emufclose64(f); //Close the image!
	return 1; //Valid DSK file!
}

byte readDSKSectorInfo(char *filename, byte side, byte track, byte sector, SECTORINFORMATIONBLOCK *result)
{
	BIGFILE *f;
	f = emufopen64(filename, "rb"); //Open the image!
	if (!f) return 0; //Not opened!
	DISKINFORMATIONBLOCK DSKInformation;
	TRACKINFORMATIONBLOCK TrackInformation;
	if (!readDSKInformation(f, &DSKInformation)) //Invalid header?
	{
		emufclose64(f); //Close the image!
		return 0; //Not a valid DSK file!
	}
	if (!readDSKTrackInformation(f, side, track, &DSKInformation, &TrackInformation)) //Invalid track?
	{
		emufclose64(f); //Close the image!
		return 0; //Not a valid DSK Track!
	}
	if (!readDSKSectorInformation(f, side, track, sector, &DSKInformation, &TrackInformation, result)) //Invalid sector?
	{
		emufclose64(f); //Close the image!
		return 0; //Not a valid DSK Sector!
	}
	emufclose64(f);
	return 1; //We have retrieved the sector information!
}

byte readDSKSectorData(char *filename, byte side, byte track, byte sector, byte sectorsize, void *result)
{
	BIGFILE *f;
	f = emufopen64(filename, "rb"); //Open the image!
	if (!f) return 0; //Not opened!
	DISKINFORMATIONBLOCK DSKInformation;
	TRACKINFORMATIONBLOCK TrackInformation;
	SECTORINFORMATIONBLOCK SectorInformation;
	if (!readDSKInformation(f, &DSKInformation)) //Invalid header?
	{
		emufclose64(f); //Close the image!
		return 0; //Not a valid DSK file!
	}
	if (!readDSKTrackInformation(f, side, track, &DSKInformation, &TrackInformation)) //Invalid track?
	{
		emufclose64(f); //Close the image!
		return 0; //Not a valid DSK Track!
	}
	if (!readDSKSectorInformation(f, side, track, sector, &DSKInformation, &TrackInformation, &SectorInformation)) //Invalid sector?
	{
		emufclose64(f); //Close the image!
		return 0; //Not a valid DSK Sector!
	}
	if (!readDSKSector(f, side, track, sector, &DSKInformation, &TrackInformation, &SectorInformation,sectorsize, result)) //Failed reading the sector?
	{
		emufclose64(f); //Close the image!
		return 0; //Invalid DSK data!
	}
	//Sector has been read, give the valid result!
	emufclose64(f);
	return 1; //We have retrieved the sector information!
}

byte writeDSKSectorData(char *filename, byte side, byte track, byte sector, byte sectorsize, void *sectordata)
{
	BIGFILE *f;
	f = emufopen64(filename, "rb+"); //Open the image!
	if (!f) return 0; //Not opened!
	DISKINFORMATIONBLOCK DSKInformation;
	TRACKINFORMATIONBLOCK TrackInformation;
	SECTORINFORMATIONBLOCK SectorInformation;
	if (!readDSKInformation(f, &DSKInformation)) //Invalid header?
	{
		emufclose64(f); //Close the image!
		return 0; //Not a valid DSK file!
	}
	if (!readDSKTrackInformation(f, side, track, &DSKInformation, &TrackInformation)) //Invalid track?
	{
		emufclose64(f); //Close the image!
		return 0; //Not a valid DSK Track!
	}
	if (!readDSKSectorInformation(f, side, track, sector, &DSKInformation, &TrackInformation, &SectorInformation)) //Invalid sector?
	{
		emufclose64(f); //Close the image!
		return 0; //Not a valid DSK Sector!
	}
	if (!writeDSKSector(f, side, track, sector, &DSKInformation, &TrackInformation, &SectorInformation, sectorsize, sectordata)) //Failed writing the sector?
	{
		emufclose64(f); //Close the image!
		return 0; //Invalid DSK data!
	}
	//Sector has been read, give the valid result!
	emufclose64(f);
	return 1; //We have retrieved the sector information!
}

byte readDSKInfo(char *filename, DISKINFORMATIONBLOCK *result)
{
	BIGFILE *f;
	f = emufopen64(filename, "rb"); //Open the image!
	if (!f) return 0; //Not opened!
	if (!readDSKInformation(f, result)) //Invalid header?
	{
		emufclose64(f); //Close the image!
		return 0; //Not a valid DSK file!
	}
	emufclose64(f); //Close the image!
	return 1; //Valid DSK file!
}

byte readDSKTrackInfo(char *filename, byte side, byte track, TRACKINFORMATIONBLOCK *result)
{
	BIGFILE *f;
	f = emufopen64(filename, "rb+"); //Open the image!
	if (!f) return 0; //Not opened!
	DISKINFORMATIONBLOCK DSKInformation;
	if (!readDSKInformation(f, &DSKInformation)) //Invalid header?
	{
		emufclose64(f); //Close the image!
		return 0; //Not a valid DSK file!
	}
	if (!readDSKTrackInformation(f, side, track, &DSKInformation, result)) //Invalid track?
	{
		emufclose64(f); //Close the image!
		return 0; //Not a valid DSK Track!
	}
	emufclose64(f); //Close the image!
	return 1; //Valid DSK Track!
}