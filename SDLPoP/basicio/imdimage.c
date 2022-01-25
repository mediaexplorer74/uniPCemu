/*

Copyright (C) 2020 - 2021 Superfury

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

#include "headers/basicio/imdimage.h" //Our own header!
#include "headers/fopen64.h" //64-bit fopen support!
#include "headers/support/zalloc.h" //Memory allocation support!
#include "headers/emu/directorylist.h"
#include "headers/support/highrestimer.h" //Time support!
#include "headers/emu/gpu/gpu_emu.h" //Text locking and output support!

//Always apply memory conservation, at the cost of speed?
//#define MEMORYCONSERVATION

#ifdef IS_PSP
//We don't have much memory to spare, use memory conservation!
#ifndef MEMORYCONSERVATION
#define MEMORYCONSERVATION
#endif
#endif

#ifdef MEMORYCONSERVATION
byte IMDimage_tmpbuf[0x10000]; //Temporary buffer for movement operations!
#endif

typedef struct
{
	BIGFILE* f; //The file!
	char fn[256]; //Filename!
	FILEPOS movecounter; //Movement counter!
	FILEPOS bytesleft; //How much data is left to read?
} MEMORYCONSERVATION_BUFFER;

//Move data between two files easily with error checking using disk-based buffering. Requires two files, a buffer(byte), a counter(FILEPOS) and success/fail jump labels
#define GOTOLABEL(label) label
#define PERFORMGOTO(label) goto GOTOLABEL(label);
#define COMMON_MEMORYCONSERVATION_MOVEBUFFER(src,dst,name,size,failjump,successjump) { { for (name.movecounter=size,name.bytesleft=0;name.movecounter;name.movecounter-=name.bytesleft) { name.bytesleft=name.movecounter; name.bytesleft=MIN(name.bytesleft,sizeof(IMDimage_tmpbuf)); if (emufread64(&IMDimage_tmpbuf,1,name.bytesleft,src)!=name.bytesleft){PERFORMGOTO(failjump)}if (emufwrite64(&IMDimage_tmpbuf,1,name.bytesleft,dst)!=name.bytesleft){PERFORMGOTO(failjump)}}} PERFORMGOTO(successjump) }
#define COMMON_MEMORYCONSERVATION_STARTBUFFER(name,failjump,successjump) { if (emufseek64(name.f, 0, SEEK_SET)<0){PERFORMGOTO(failjump)} PERFORMGOTO(successjump)}
#define COMMON_MEMORYCONSERVATION_READBUFFER(src,name,size,failjump,successjump) { COMMON_MEMORYCONSERVATION_STARTBUFFER(name,failjump,successjump_pre_##successjump##name) successjump_pre_##successjump##name: COMMON_MEMORYCONSERVATION_MOVEBUFFER(src,name.f,name,size,failjump,successjump) }
#define COMMON_MEMORYCONSERVATION_WRITEBUFFER(dst,name,size,failjump,successjump) { COMMON_MEMORYCONSERVATION_STARTBUFFER(name,failjump,successjump_pre_##successjump##name) successjump_pre_##successjump##name: COMMON_MEMORYCONSERVATION_MOVEBUFFER(name.f,dst,name,size,failjump,successjump) }
#define COMMON_MEMORYCONSERVATION_BEGINBUFFER(name,failjump,successjump) { memset(&name.fn, 0, sizeof(name.fn));safestrcpy(name.fn, sizeof(name.fn), tmpnam(NULL)); name.f = emufopen64(name.fn, "wb+"); { if (name.f==NULL) PERFORMGOTO(failjump) } PERFORMGOTO(successjump) }
#define COMMON_MEMORYCONSERVATION_ENDBUFFER(name) {emufclose64(name.f); name.f = NULL; delete_file(NULL, name.fn);}
#define COMMON_MEMORYCONSERVATION_CLEARBUFFER(name,size,failjump,successjump) { COMMON_MEMORYCONSERVATION_ENDBUFFER(name) COMMON_MEMORYCONSERVATION_BEGINBUFFER(f,name,size,##failjump,##successjump) }
#define COMMON_MEMORYCONSERVATION_buffer(name) MEMORYCONSERVATION_BUFFER name; memset(&name,0,sizeof(name));

//Simple cross-compatible check for conservative dynamic storage
#ifdef MEMORYCONSERVATION
#define COMMON_MEMORYCONSERVATION_ALLOCATED(name) name.f
#else
#define COMMON_MEMORYCONSERVATION_ALLOCATED(name) name
#endif

//One sector information block per sector.
#include "headers/packed.h" //PACKED support!
typedef struct PACKED
{
	byte mode; //0-5
	byte cylinder; //0-n
	byte head_extrabits; //Bit0=Head. Bit 6=Sector Head map present. Bit 7=Sector cylinder map present.
	byte sectorspertrack; //1+
	byte SectorSize; //128*(2^SectorSize)=Size. 0-6=Normal sector size. 0xFF=Table of 16-bit sector sizes with the sizes before the data records.
} TRACKINFORMATIONBLOCK;
#include "headers/endpacked.h" //End packed!

//Sector size to bytes translation!
#define SECTORSIZE_BYTES(SectorSize) (128<<(SectorSize))

//Head bits
#define IMD_HEAD_HEADNUMBER 0x01
#define IMD_HEAD_HEADMAPPRESENT 0x40
#define IMD_HEAD_CYLINDERMAPPRESENT 0x80

//Sector size reserved value
#define IMD_SECTORSIZE_SECTORSIZEMAPPRESENT 0xFF


//File structure:
/*

ASCII header, 0x1A terminated.
For each track:
1. TRACKINFORMATIONBLOCK
2. Sector numbering map
3. Sector cylinder map(optional)
4. Sector head map(optional)
5. Sector size map(optional)
6. Sector data records

Each data record starts with a ID, followed by one byte for compressed records(even number), nothing when zero(unavailable) or otherwise the full sector size. Valid when less than 9.

*/

byte is_IMDimage(char* filename) //Are we a IMD image?
{
	byte identifier[3];
	BIGFILE* f;
	if (strcmp(filename, "") == 0) return 0; //Unexisting: don't even look at it!
	if (!isext(filename, "imd")) //Not our IMD image file?
	{
		return 0; //Not a IMD image!
	}
	f = emufopen64(filename, "rb"); //Open the image!
	if (!f) return 0; //Not opened!
	if (emufread64(&identifier, 1, sizeof(identifier), f) != sizeof(identifier)) //Try to read the header?
	{
		emufclose64(f); //Close it!
		return 0; //Not a IMD image!
	}
	if ((identifier[0] != 'I') || (identifier[1] != 'M') || (identifier[2] != 'D')) //Invalid header?
	{
		emufclose64(f); //Close it!
		return 0; //Not a IMD image!
	}
	for (; !emufeof64(f);) //Not EOF yet?
	{
		if (emufread64(&identifier[0], 1, sizeof(identifier[0]), f) != sizeof(identifier[0])) //Failed to read an header comment byte?
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if (identifier[0] == 0x1A) //Finished comment identifier?
		{
			emufclose64(f); //Close the image!
			return 1; //Valid IMD file with a valid comment!
		}
	}
	//Reached EOF without comment finishing?

	emufclose64(f); //Close the image!
	return 0; //Invalid IMD file!
}

byte readIMDDiskInfo(char* filename, IMDIMAGE_SECTORINFO* result)
{
	byte maxhead = 0; //Maximum head!
	byte maxtrack = 0; //Amount of tracks!
#ifndef MEMORYCONSERVATION
	byte* filedata; //Total data of the entire file!
#endif
	FILEPOS filesize; //The size of the file!
	FILEPOS filepos; //The position in the file that's loaded in memory!
	word sectornumber;
	word* sectorsizemap = NULL; //Sector size map!
	uint_32 datarecordnumber;
	TRACKINFORMATIONBLOCK trackinfo;
	byte identifier[3];
	byte data;
	BIGFILE* f;
	if (strcmp(filename, "") == 0) return 0; //Unexisting: don't even look at it!
	if (!isext(filename, "imd")) //Not our IMD image file?
	{
		return 0; //Not a IMD image!
	}
	f = emufopen64(filename, "rb"); //Open the image!
	if (!f) return 0; //Not opened!
	if (emufread64(&identifier, 1, sizeof(identifier), f) != sizeof(identifier)) //Try to read the header?
	{
		emufclose64(f); //Close it!
		return 0; //Not a IMD image!
	}
	if ((identifier[0] != 'I') || (identifier[1] != 'M') || (identifier[2] != 'D')) //Invalid header?
	{
		emufclose64(f); //Close it!
		return 0; //Not a IMD image!
	}
	for (; !emufeof64(f);) //Not EOF yet?
	{
		if (emufread64(&identifier[0], 1, sizeof(identifier[0]), f) != sizeof(identifier[0])) //Failed to read an header comment byte?
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if (identifier[0] == 0x1A) //Finished comment identifier?
		{
			goto validIMDheaderInfo; //Header is validly read!
		}
	}
	//Reached EOF without comment finishing?

	emufclose64(f); //Close the image!
	return 0; //Invalid IMD file!

validIMDheaderInfo:
	if (emuftell64(f) < 0) //Can't tell the location?
	{
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	filepos = emuftell64(f); //Save the current location within the file to use later!
	if (emufseek64(f, 0, SEEK_END) < 0) //Goto EOF?
	{
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	filesize = emuftell64(f); //The size of the file!
	#ifndef MEMORYCONSERVATION
	filedata = (byte*)zalloc(filesize, "IMDIMAGE_FILE", NULL); //Allocate a buffer for the file!
	if (filedata == NULL) //Couldn't allocate?
	{
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	if (emufseek64(f, 0, SEEK_SET) < 0) //Can't goto BOF?
	{
		#ifndef MEMORYCONSERVATION
		freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
		#endif
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	if (emufread64(filedata, 1, filesize, f) != filesize) //Can't read the file data?
	{
		#ifndef MEMORYCONSERVATION
		freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
		#endif
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	#else
	if (emufseek64(f, filepos, SEEK_SET) < 0) //Goto EOF?
	{
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	#endif
	//Now, skip tracks until we reach the selected track!
	if (filepos == filesize) //EOF reached too soon? No tracks?
	{
		#ifndef MEMORYCONSERVATION
		freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
		#endif	
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	for (;;) //Skipping left?
	{
		#ifdef MEMORYCONSERVATION
		if (emufread64(&trackinfo,1,sizeof(trackinfo),f)!=sizeof(trackinfo)) //Failed to read track info?
		#else
		if ((filepos + sizeof(trackinfo)) > filesize) //Failed to read track info?
		#endif
		{
			#ifndef MEMORYCONSERVATION
			freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
			#endif
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		#ifndef MEMORYCONSERVATION
		memcpy(&trackinfo, &filedata[filepos], sizeof(trackinfo)); //Get the track info
		filepos += sizeof(trackinfo); //Read from the buffer!
		#endif
		//Track info block has been read!
		#ifdef MEMORYCONSERVATION
		if (emufseek64(f,trackinfo.sectorspertrack,SEEK_CUR)<0) //Skip the sector number map!
		#else
		if ((filepos + trackinfo.sectorspertrack) > filesize) //Skip the sector number map!
		#endif
		{
			#ifndef MEMORYCONSERVATION
			freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
			#endif
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		#ifndef MEMORYCONSERVATION
		filepos += trackinfo.sectorspertrack; //Skip the sector number map!
		#endif
		if (trackinfo.head_extrabits & IMD_HEAD_CYLINDERMAPPRESENT) //Cylinder map following?
		{
			#ifdef MEMORYCONSERVATION
			if (emufseek64(f,trackinfo.sectorspertrack,SEEK_CUR)<0) //Skip the cylinder number map!
			#else
			if ((filepos + trackinfo.sectorspertrack) > filesize) //Skip the cylinder number map!
			#endif
			{
				#ifndef MEMORYCONSERVATION
				freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
				#endif
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			#ifndef MEMORYCONSERVATION
			filepos += trackinfo.sectorspertrack; //Skip the cylinder number map!
			#endif
		}
		if (trackinfo.head_extrabits & IMD_HEAD_HEADMAPPRESENT) //Head map following?
		{
			#ifdef MEMORYCONSERVATION
			if (emufseek64(f,trackinfo.sectorspertrack,SEEK_CUR)<0) //Skip the head number map!
			#else
			if ((filepos + trackinfo.sectorspertrack) > filesize) //Skip the head number map!
			#endif
			{
				#ifndef MEMORYCONSERVATION
				freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
				#endif
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			#ifndef MEMORYCONSERVATION
			filepos += trackinfo.sectorspertrack; //Skip the head number map!
			#endif
		}
		if (trackinfo.SectorSize == IMD_SECTORSIZE_SECTORSIZEMAPPRESENT) //Sector size map following?
		{
			sectorsizemap = zalloc((trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP", NULL); //Allocate a sector map to use!
			if (!sectorsizemap) //Failed to allocate?
			{
				#ifndef MEMORYCONSERVATION
				freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
				#endif
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			#ifdef MEMORYCONSERVATION
			if (emufread64(sectorsizemap,1,(trackinfo.sectorspertrack << 1),f)<0) //Read the sector size map!
			#else
			if ((filepos + (trackinfo.sectorspertrack << 1)) > filesize) //Read the sector size map!
			#endif
			{
				#ifndef MEMORYCONSERVATION
				freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
				#endif
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			#ifndef MEMORYCONSERVATION
			memcpy(sectorsizemap, &filedata[filepos], (trackinfo.sectorspertrack << 1));
			filepos += (trackinfo.sectorspertrack << 1);
			#endif
			sectornumber = 0;
			for (sectornumber = 0; sectornumber < trackinfo.sectorspertrack; ++sectornumber) //Patch as needed!
			{
				sectorsizemap[sectornumber] = SDL_SwapLE16(sectorsizemap[sectornumber]); //Swap all byte ordering to be readable!
			}
		}
		datarecordnumber = trackinfo.sectorspertrack; //How many records to read!
		sectornumber = 0; //Start at the first sector number!
		for (; datarecordnumber;) //Process all sectors on the track!
		{
			#ifdef MEMORYCONSERVATION
			if (emufread64(&data,1,sizeof(data),f)!=sizeof(data)) //Read the identifier!
			#else
			if (filepos >= filesize) //Read the identifier!
			#endif
			{
				#ifndef MEMORYCONSERVATION
				freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
				#endif
				freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			#ifndef MEMORYCONSERVATION
			data = filedata[filepos++];
			#endif
			//Now, we have the identifier!
			if (data) //Not one that's unavailable?
			{
				if (data > 8) //Undefined value?
				{
					#ifndef MEMORYCONSERVATION
					freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
					#endif
					freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
					emufclose64(f); //Close the image!
					return 0; //Invalid IMD file!
				}
				if (data & 1) //Normal sector with or without mark, data error or deleted?
				{
					//Skip the sector's data!
					if (sectorsizemap) //Map used?
					{
						#ifdef MEMORYCONSERVATION
						if (emufseek64(f,sectorsizemap[sectornumber],SEEK_CUR)<0) //Errored out?
						#else
						if ((filepos + sectorsizemap[sectornumber]) > filesize) //Errored out?
						#endif
						{
							#ifndef MEMORYCONSERVATION
							freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
							#endif
							freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
							emufclose64(f); //Close the image!
							return 0; //Invalid IMD file!
						}
						#ifndef MEMORYCONSERVATION
						filepos += sectorsizemap[sectornumber];
						#endif
					}
					else
					{
						#ifdef MEMORYCONSERVATION
						if (emufseek64(f,SECTORSIZE_BYTES(trackinfo.SectorSize),SEEK_CUR)<0)
						#else
						if ((filepos + SECTORSIZE_BYTES(trackinfo.SectorSize)) > filesize)
						#endif
						{
							#ifndef MEMORYCONSERVATION
							freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
							#endif
							freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
							emufclose64(f); //Close the image!
							return 0; //Invalid IMD file!
						}
						#ifndef MEMORYCONSERVATION
						filepos += SECTORSIZE_BYTES(trackinfo.SectorSize);
						#endif
					}
				}
				else //Compressed?
				{
					#ifdef MEMORYCONSERVATION
					if (emufseek64(f,sizeof(data),SEEK_CUR)<0) //Skip the compressed data!
					#else
					if (filepos >= filesize) //Skip the compressed data!
					#endif
					{
						#ifndef MEMORYCONSERVATION
						freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
						#endif
						if (sectorsizemap) //Allocated sector size map?
						{
							freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
						}
						emufclose64(f); //Close the image!
						return 0; //Invalid IMD file!
					}
					#ifndef MEMORYCONSERVATION
					++filepos; //Skip the compressed data!
					#endif
				}
			}
			++sectornumber; //Process the next sector number!
			--datarecordnumber; //Processed!
		}
		freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
		if (trackinfo.cylinder > maxtrack) maxtrack = trackinfo.cylinder; //Maximum value!
		if ((trackinfo.head_extrabits & IMD_HEAD_HEADNUMBER) > maxhead) maxhead = (trackinfo.head_extrabits & IMD_HEAD_HEADNUMBER); //Maximum head!
		#ifdef MEMORYCONSERVATION
		filepos = emuftell64(f); //New file position!
		#endif
		if (filepos == filesize) //EOF reached properly with at least one track?
		{
			result->cylinderID = maxtrack; //Last cylinder!
			result->datamark = DATAMARK_INVALID; //Nothing to report here!
			result->headnumber = maxhead; //Last possible head!
			result->MFM_speedmode = trackinfo.mode; //Unknown, take the last mode!
			result->sectorID = trackinfo.sectorspertrack; //SPT detected?
			result->sectorsize = 0; //Unknown!
			result->totalsectors = trackinfo.sectorspertrack; //Last SPT detected!
			#ifndef MEMORYCONSERVATION
			freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
			#endif
			emufclose64(f); //Close the image!
			return 1; //Valid IMD file!
		}
	}

	#ifndef MEMORYCONSERVATION
	freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
	#endif

	emufclose64(f); //Close the image!
	return 0; //Invalid IMD file!
}

byte readIMDSectorInfo(char* filename, byte track, byte head, byte sector, IMDIMAGE_SECTORINFO* result)
{
	#ifndef MEMORYCONSERVATION
	byte* filedata; //Total data of the entire file!
	FILEPOS filesize; //The size of the file!
	#endif
	FILEPOS filepos; //The position in the file that's loaded in memory!
	byte physicalsectornr;
	byte physicalheadnr;
	byte physicalcylindernr;
	word physicalsectorsize; //Effective sector size!
	word sectornumber;
	word* sectorsizemap=NULL; //Sector size map!
	uint_32 datarecordnumber;
	TRACKINFORMATIONBLOCK trackinfo;
	byte identifier[3];
	byte data;
	BIGFILE* f;
	if (strcmp(filename, "") == 0) return 0; //Unexisting: don't even look at it!
	if (!isext(filename, "imd")) //Not our IMD image file?
	{
		return 0; //Not a IMD image!
	}
	f = emufopen64(filename, "rb"); //Open the image!
	if (!f) return 0; //Not opened!
	if (emufread64(&identifier, 1, sizeof(identifier), f) != sizeof(identifier)) //Try to read the header?
	{
		emufclose64(f); //Close it!
		return 0; //Not a IMD image!
	}
	if ((identifier[0] != 'I') || (identifier[1] != 'M') || (identifier[2] != 'D')) //Invalid header?
	{
		emufclose64(f); //Close it!
		return 0; //Not a IMD image!
	}
	for (; !emufeof64(f);) //Not EOF yet?
	{
		if (emufread64(&identifier[0], 1, sizeof(identifier[0]), f) != sizeof(identifier[0])) //Failed to read an header comment byte?
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if (identifier[0] == 0x1A) //Finished comment identifier?
		{
			goto validIMDheaderInfo; //Header is validly read!
		}
	}
	//Reached EOF without comment finishing?

	emufclose64(f); //Close the image!
	return 0; //Invalid IMD file!

validIMDheaderInfo:
	if (emuftell64(f) < 0) //Can't tell the location?
	{
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	filepos = emuftell64(f); //Save the current location within the file to use later!
	#ifndef MEMORYCONSERVATION
	if (emufseek64(f, 0, SEEK_END) < 0) //Goto EOF?
	{
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	filesize = emuftell64(f); //The size of the file!
	#endif
	#ifndef MEMORYCONSERVATION
	filedata = (byte*)zalloc(filesize, "IMDIMAGE_FILE", NULL); //Allocate a buffer for the file!
	if (filedata == NULL) //Couldn't allocate?
	{
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	if (emufseek64(f, 0, SEEK_SET) < 0) //Can't goto BOF?
	{
#ifndef MEMORYCONSERVATION
		freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
#endif
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	if (emufread64(filedata, 1, filesize, f) != filesize) //Can't read the file data?
	{
#ifndef MEMORYCONSERVATION
		freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
#endif
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	if (filepos == filesize) //EOF reached too soon? No tracks?
	{
#ifndef MEMORYCONSERVATION
		freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
#endif
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
#endif
	//Now, skip tracks until we reach the selected track!
	for (;;) //Skipping left?
	{
		#ifdef MEMORYCONSERVATION
		if (emufread64(&trackinfo, 1, sizeof(trackinfo), f)!=sizeof(trackinfo)) //Failed to read track info?
		#else
		if ((filepos + sizeof(trackinfo)) > filesize) //Failed to read track info?
		#endif
		{
			#ifndef MEMORYCONSERVATION
			freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
			#endif
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		#ifndef MEMORYCONSERVATION
		memcpy(&trackinfo, &filedata[filepos], sizeof(trackinfo)); //Get the track info
		filepos += sizeof(trackinfo); //Read from the buffer!
		#endif
		//Track info block has been read!
		if ((trackinfo.cylinder == track) && ((trackinfo.head_extrabits & IMD_HEAD_HEADNUMBER) == head)) //Track&head found?
		{
			#ifdef MEMORYCONSERVATION
			if (emufseek64(f, -((int)sizeof(trackinfo)), SEEK_CUR) < 0)
			{
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			#else
			filepos -= sizeof(trackinfo); //Go back, undo the read!
			#endif
			break; //Stop searching: we're found!
		}

		#ifdef MEMORYCONSERVATION
		if (emufseek64(f, trackinfo.sectorspertrack, SEEK_CUR) < 0) //Skip the sector number map!
		#else
		if ((filepos + trackinfo.sectorspertrack) > filesize) //Skip the sector number map!
		#endif
		{
			#ifndef MEMORYCONSERVATION
			freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
			#endif
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		#ifndef MEMORYCONSERVATION
		filepos += trackinfo.sectorspertrack; //Skip the sector number map!
		#endif
		if (trackinfo.head_extrabits & IMD_HEAD_CYLINDERMAPPRESENT) //Cylinder map following?
		{
			#ifdef MEMORYCONSERVATION
			if (emufseek64(f, trackinfo.sectorspertrack, SEEK_CUR) < 0) //Skip the cylinder number map!
			#else
			if ((filepos + trackinfo.sectorspertrack) > filesize) //Skip the cylinder number map!
			#endif
			{
				#ifndef MEMORYCONSERVATION
				freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
				#endif
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			#ifndef MEMORYCONSERVATION
			filepos += trackinfo.sectorspertrack; //Skip the cylinder number map!
			#endif
		}
		if (trackinfo.head_extrabits & IMD_HEAD_HEADMAPPRESENT) //Head map following?
		{
			#ifdef MEMORYCONSERVATION
			if (emufseek64(f, trackinfo.sectorspertrack, SEEK_CUR) < 0) //Skip the head number map!
			#else
			if ((filepos + trackinfo.sectorspertrack) > filesize) //Skip the head number map!
			#endif
			{
				#ifndef MEMORYCONSERVATION
				freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
				#endif
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			#ifndef MEMORYCONSERVATION
			filepos += trackinfo.sectorspertrack; //Skip the head number map!
			#endif
		}
		if (trackinfo.SectorSize == IMD_SECTORSIZE_SECTORSIZEMAPPRESENT) //Sector size map following?
		{
			sectorsizemap = zalloc((trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP", NULL); //Allocate a sector map to use!
			if (!sectorsizemap) //Failed to allocate?
			{
				#ifndef MEMORYCONSERVATION
				freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
				#endif
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			#ifdef MEMORYCONSERVATION
			if (emufread64(sectorsizemap, 1, (trackinfo.sectorspertrack << 1), f) < 0) //Read the sector size map!
			#else
			if ((filepos + (trackinfo.sectorspertrack << 1)) > filesize) //Read the sector size map!
			#endif
			{
				#ifndef MEMORYCONSERVATION
				freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
				#endif
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			#ifndef MEMORYCONSERVATION
			memcpy(sectorsizemap, &filedata[filepos], (trackinfo.sectorspertrack << 1));
			filepos += (trackinfo.sectorspertrack << 1);
			#endif
			sectornumber = 0;
			for (sectornumber = 0; sectornumber < trackinfo.sectorspertrack; ++sectornumber) //Patch as needed!
			{
				sectorsizemap[sectornumber] = SDL_SwapLE16(sectorsizemap[sectornumber]); //Swap all byte ordering to be readable!
			}
		}
		datarecordnumber = trackinfo.sectorspertrack; //How many records to read!
		sectornumber = 0; //Start at the first sector number!
		for (; datarecordnumber;) //Process all sectors on the track!
		{
			#ifdef MEMORYCONSERVATION
			if (emufread64(&data, 1, sizeof(data), f) != sizeof(data)) //Read the identifier!
			#else
			if (filepos >= filesize) //Read the identifier!
			#endif
			{
				#ifndef MEMORYCONSERVATION
				freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
				#endif
				freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			#ifndef MEMORYCONSERVATION
			data = filedata[filepos++];
			#endif
			//Now, we have the identifier!
			if (data) //Not one that's unavailable?
			{
				if (data > 8) //Undefined value?
				{
					#ifndef MEMORYCONSERVATION
					freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
					#endif
					freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
					emufclose64(f); //Close the image!
					return 0; //Invalid IMD file!
				}
				if (data & 1) //Normal sector with or without mark, data error or deleted?
				{
					//Skip the sector's data!
					if (sectorsizemap) //Map used?
					{
						#ifdef MEMORYCONSERVATION
						if (emufseek64(f, sectorsizemap[sectornumber], SEEK_CUR) < 0) //Errored out?
						#else
						if ((filepos + sectorsizemap[sectornumber]) > filesize) //Errored out?
						#endif
						{
							#ifndef MEMORYCONSERVATION
							freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
							#endif
							freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
							emufclose64(f); //Close the image!
							return 0; //Invalid IMD file!
						}
						#ifndef MEMORYCONSERVATION
						filepos += sectorsizemap[sectornumber];
						#endif
					}
					else
					{
						#ifdef MEMORYCONSERVATION
						if (emufseek64(f, SECTORSIZE_BYTES(trackinfo.SectorSize), SEEK_CUR) < 0)
						#else
						if ((filepos + SECTORSIZE_BYTES(trackinfo.SectorSize)) > filesize)
						#endif
						{
							#ifndef MEMORYCONSERVATION
							freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
							#endif
							freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
							emufclose64(f); //Close the image!
							return 0; //Invalid IMD file!
						}
						#ifndef MEMORYCONSERVATION
						filepos += SECTORSIZE_BYTES(trackinfo.SectorSize);
						#endif
					}
				}
				else //Compressed?
				{
					#ifdef MEMORYCONSERVATION
					if (emufseek64(f, sizeof(data), SEEK_CUR) < 0) //Skip the compressed data!
					#else
					if (filepos >= filesize) //Skip the compressed data!
					#endif
					{
						#ifndef MEMORYCONSERVATION
						freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
						#endif
						if (sectorsizemap) //Allocated sector size map?
						{
							freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
						}
						emufclose64(f); //Close the image!
						return 0; //Invalid IMD file!
					}
					#ifndef MEMORYCONSERVATION
					++filepos; //Skip the compressed data!
					#endif
				}
			}
			++sectornumber; //Process the next sector number!
			--datarecordnumber; //Processed!
		}
		freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
	}

	#ifndef MEMORYCONSERVATION
	freez((void**)&filedata, filesize, "IMDIMAGE_FILE"); //Free the allocated file!
	if (emufseek64(f, filepos, SEEK_SET)) //Couldn't goto the file's real position that isn't buffered?
	{
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	#endif

	//Now, we're at the specified track!
	if (emufread64(&trackinfo, 1, sizeof(trackinfo), f) != sizeof(trackinfo)) //Failed to read track info?
	{
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	//Track info block has been read!
	if (sector >= trackinfo.sectorspertrack) //Not enough to read?
	{
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	if (emufseek64(f, sector, SEEK_CUR) < 0) //Skip the sector number map!
	{
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	if (emufread64(&physicalsectornr, 1, sizeof(physicalsectornr), f)!=sizeof(physicalsectornr)) //Read the actual sector number!
	{
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	if (emufseek64(f, trackinfo.sectorspertrack-sector-1, SEEK_CUR) < 0) //Skip the sector number map!
	{
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	physicalcylindernr = (trackinfo.cylinder); //Default cylinder number!
	if (trackinfo.head_extrabits & IMD_HEAD_CYLINDERMAPPRESENT) //Cylinder map following?
	{
		if (emufseek64(f, sector, SEEK_CUR) < 0) //Skip the cylinder number map!
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if (emufread64(&physicalcylindernr,1,sizeof(physicalcylindernr), f)!=sizeof(physicalcylindernr)) //Read the cylinder number!
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if (emufseek64(f, trackinfo.sectorspertrack - sector - 1, SEEK_CUR) < 0) //Skip the cylinder number map!
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
	}
	physicalheadnr = (trackinfo.head_extrabits & IMD_HEAD_HEADNUMBER); //Default head number!
	if (trackinfo.head_extrabits & IMD_HEAD_HEADMAPPRESENT) //Head map following?
	{
		if (emufseek64(f, sector, SEEK_CUR) < 0) //Skip the head number map!
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if (emufread64(&physicalheadnr, 1, sizeof(physicalheadnr), f) != sizeof(physicalheadnr)) //Read the cylinder number!
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if (emufseek64(f, trackinfo.sectorspertrack - sector - 1, SEEK_CUR) < 0) //Skip the head number map!
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
	}
	if (trackinfo.SectorSize == IMD_SECTORSIZE_SECTORSIZEMAPPRESENT) //Sector size map following?
	{
		sectorsizemap = zalloc((trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP", NULL); //Allocate a sector map to use!
		if (!sectorsizemap) //Failed to allocate?
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if (emufread64(sectorsizemap, 1, (trackinfo.sectorspertrack << 1), f) < 0) //Read the sector size map!
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		sectornumber = 0;
		for (sectornumber = 0; sectornumber < trackinfo.sectorspertrack; ++sectornumber) //Patch as needed!
		{
			sectorsizemap[sectornumber] = SDL_SwapLE16(sectorsizemap[sectornumber]); //Swap all byte ordering to be readable!
		}
	}

	//Skip n sectors!
	datarecordnumber = MIN(trackinfo.sectorspertrack,sector); //How many records to read!
	sectornumber = 0; //Start at the first sector number!
	for (; datarecordnumber;) //Process all sectors on the track!
	{
		if (emufread64(&data, 1, sizeof(data), f) != sizeof(data)) //Read the identifier!
		{
			if (sectorsizemap) //Allocated sector size map?
			{
				freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
			}
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		//Now, we have the identifier!
		if (data) //Not one that's unavailable?
		{
			if (data > 8) //Undefined value?
			{
				if (sectorsizemap) //Allocated sector size map?
				{
					freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
				}
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			if (data & 1) //Normal sector with or without mark, data error or deleted?
			{
				//Skip the sector's data!
				if (sectorsizemap) //Map used?
				{
					if (emufseek64(f, (sectorsizemap[sectornumber]), SEEK_CUR) < 0) //Errored out?
					{
						if (sectorsizemap) //Allocated sector size map?
						{
							freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
						}
						emufclose64(f); //Close the image!
						return 0; //Invalid IMD file!
					}
				}
				else
				{
					if (emufseek64(f, SECTORSIZE_BYTES(trackinfo.SectorSize), SEEK_CUR) < 0) //Errored out?
					{
						if (sectorsizemap) //Allocated sector size map?
						{
							freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
						}
						emufclose64(f); //Close the image!
						return 0; //Invalid IMD file!
					}
				}
			}
			else //Compressed?
			{
				if (emufseek64(f, 1, SEEK_CUR) < 0) //Skip the compressed data!
				{
					if (sectorsizemap) //Allocated sector size map?
					{
						freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
					}
					emufclose64(f); //Close the image!
					return 0; //Invalid IMD file!
				}
			}
		}
		++sectornumber; //Process the next sector number!
		--datarecordnumber; //Processed!
	}

	if (datarecordnumber==0) //Left to read? Reached the sector!
	{
		if (emufread64(&data, 1, sizeof(data), f) != sizeof(data)) //Read the identifier!
		{
			if (sectorsizemap) //Allocated sector size map?
			{
				freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
			}
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		//Now, we have the identifier!

		if (data > 8) //Undefined value?
		{
		invalidsectordataInfo:
			if (sectorsizemap) //Allocated sector size map?
			{
				freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
			}
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if (data) //Not one that's unavailable?
		{
			if (data & 1) //Normal sector with or without mark, data error or deleted?
			{
				//Skip the sector's data!
				if (sectorsizemap) //Map used?
				{
					//Check if we're valid!
					if (emufseek64(f, sectorsizemap[sectornumber], SEEK_CUR) < 0) //Failed to skip the data part of the sector?
					{
						goto invalidsectordataInfo; //Error out!
					}
					physicalsectorsize = sectorsizemap[sectornumber]; //Physical sector size!
					//The sector is loaded and valid!
				sectorreadyInfo:
					//Fill up the sector information block!
					result->cylinderID = physicalcylindernr; //Physical cylinder number!
					result->headnumber = physicalheadnr; //Physical head number!
					result->sectorID = physicalsectornr; //Physical sector number!
					result->MFM_speedmode = trackinfo.mode; //The mode!
					result->sectorsize = physicalsectorsize; //Physical sector size!
					result->totalsectors = trackinfo.sectorspertrack; //How many sectors are on this track!
					result->datamark = ((data - 1) >> 1); //What is it marked as!
					sectorready_unreadableInfo:

					//Finish up!
					if (sectorsizemap) //Allocated sector size map?
					{
						freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
					}
					emufclose64(f); //Close the image!
					return 1; //Gotten information about the record!
				}
				else
				{
					if (emufseek64(f, SECTORSIZE_BYTES(trackinfo.SectorSize), SEEK_CUR) < 0) //Errored out?
					{
						goto invalidsectordataInfo;
					}
					physicalsectorsize = SECTORSIZE_BYTES(trackinfo.SectorSize); //How large are we physically!

					goto sectorreadyInfo; //Handle ready sector information!
				}
			}
			else //Compressed?
			{
				if (emufseek64(f, 1, SEEK_CUR) < 0) //Skip the compressed data!
				{
					if (sectorsizemap) //Allocated sector size map?
					{
						freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
					}
					emufclose64(f); //Close the image!
					return 0; //Invalid IMD file!
				}
				if (sectorsizemap) //Map used?
				{
					physicalsectorsize = sectorsizemap[sectornumber]; //Errored out?
				}
				else
				{
					physicalsectorsize = SECTORSIZE_BYTES(trackinfo.SectorSize); //Errored out?
				}
				goto sectorreadyInfo;
			}
		}
		else //Unreadable sector?
		{
			result->cylinderID = physicalcylindernr; //Physical cylinder number!
			result->headnumber = physicalheadnr; //Physical head number!
			result->sectorID = physicalsectornr; //Physical sector number!
			result->MFM_speedmode = trackinfo.mode; //The mode!
			result->sectorsize = 0; //Physical sector size!
			result->totalsectors = trackinfo.sectorspertrack; //How many sectors are on this track!
			result->datamark = DATAMARK_INVALID; //What is it marked as!
			goto sectorready_unreadableInfo; //Handle the unreadable sector result!
		}
	}

	//Couldn't read it!

	if (sectorsizemap) //Allocated sector size map?
	{
		freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
	}
	emufclose64(f); //Close the image!
	return 0; //Invalid IMD file!
}


byte readIMDSector(char* filename, byte track, byte head, byte sector, word sectorsize, void* result)
{
	byte filldata;
	byte physicalsectornr;
	byte physicalheadnr;
	byte physicalcylindernr;
	word physicalsectorsize; //Effective sector size!
	word sectornumber;
	word* sectorsizemap=NULL; //Sector size map!
	uint_32 datarecordnumber;
	TRACKINFORMATIONBLOCK trackinfo;
	byte identifier[3];
	byte data;
	BIGFILE* f;
	if (strcmp(filename, "") == 0) return 0; //Unexisting: don't even look at it!
	if (!isext(filename, "imd")) //Not our IMD image file?
	{
		return 0; //Not a IMD image!
	}
	f = emufopen64(filename, "rb"); //Open the image!
	if (!f) return 0; //Not opened!
	if (emufread64(&identifier, 1, sizeof(identifier), f) != sizeof(identifier)) //Try to read the header?
	{
		emufclose64(f); //Close it!
		return 0; //Not a IMD image!
	}
	if ((identifier[0] != 'I') || (identifier[1] != 'M') || (identifier[2] != 'D')) //Invalid header?
	{
		emufclose64(f); //Close it!
		return 0; //Not a IMD image!
	}
	for (; !emufeof64(f);) //Not EOF yet?
	{
		if (emufread64(&identifier[0], 1, sizeof(identifier[0]), f) != sizeof(identifier[0])) //Failed to read an header comment byte?
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if (identifier[0] == 0x1A) //Finished comment identifier?
		{
			goto validIMDheaderRead; //Header is validly read!
		}
	}
	//Reached EOF without comment finishing?

	emufclose64(f); //Close the image!
	return 0; //Invalid IMD file!

validIMDheaderRead:
	//Now, skip tracks until we reach the selected track!
	for (;;) //Skipping left?
	{
		if (emufread64(&trackinfo, 1, sizeof(trackinfo), f) != sizeof(trackinfo)) //Failed to read track info?
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if ((trackinfo.cylinder == track) && ((trackinfo.head_extrabits & IMD_HEAD_HEADNUMBER) == head)) //Track&head found?
		{
			if (emufseek64(f, -((int)sizeof(trackinfo)), SEEK_CUR) < 0) //Found!
			{
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			break; //Stop searching: we're found!
		}
		//Track info block has been read!
		if (emufseek64(f, trackinfo.sectorspertrack, SEEK_CUR) < 0) //Skip the sector number map!
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if (trackinfo.head_extrabits & IMD_HEAD_CYLINDERMAPPRESENT) //Cylinder map following?
		{
			if (emufseek64(f, trackinfo.sectorspertrack, SEEK_CUR) < 0) //Skip the cylinder number map!
			{
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
		}
		if (trackinfo.head_extrabits & IMD_HEAD_HEADMAPPRESENT) //Head map following?
		{
			if (emufseek64(f, trackinfo.sectorspertrack, SEEK_CUR) < 0) //Skip the head number map!
			{
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
		}
		if (trackinfo.SectorSize == IMD_SECTORSIZE_SECTORSIZEMAPPRESENT) //Sector size map following?
		{
			sectorsizemap = zalloc((trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP", NULL); //Allocate a sector map to use!
			if (!sectorsizemap) //Failed to allocate?
			{
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			if (emufread64(sectorsizemap, 1, (trackinfo.sectorspertrack << 1), f) < 0) //Read the sector size map!
			{
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			sectornumber = 0;
			for (sectornumber = 0; sectornumber < trackinfo.sectorspertrack; ++sectornumber) //Patch as needed!
			{
				sectorsizemap[sectornumber] = SDL_SwapLE16(sectorsizemap[sectornumber]); //Swap all byte ordering to be readable!
			}
		}
		datarecordnumber = trackinfo.sectorspertrack; //How many records to read!
		sectornumber = 0; //Start at the first sector number!
		for (; datarecordnumber;) //Process all sectors on the track!
		{
			if (emufread64(&data, 1, sizeof(data), f) != sizeof(data)) //Read the identifier!
			{
				freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			//Now, we have the identifier!
			if (data) //Not one that's unavailable?
			{
				if (data > 8) //Undefined value?
				{
					freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
					emufclose64(f); //Close the image!
					return 0; //Invalid IMD file!
				}
				if (data & 1) //Normal sector with or without mark, data error or deleted?
				{
					//Skip the sector's data!
					if (sectorsizemap) //Map used?
					{
						if (emufseek64(f, (sectorsizemap[sectornumber]), SEEK_CUR) < 0) //Errored out?
						{
							freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
							emufclose64(f); //Close the image!
							return 0; //Invalid IMD file!
						}
					}
					else
					{
						if (emufseek64(f, SECTORSIZE_BYTES(trackinfo.SectorSize), SEEK_CUR) < 0) //Errored out?
						{
							freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
							emufclose64(f); //Close the image!
							return 0; //Invalid IMD file!
						}
					}
				}
				else //Compressed?
				{
					if (emufseek64(f, 1, SEEK_CUR) < 0) //Skip the compressed data!
					{
						if (sectorsizemap) //Allocated sector size map?
						{
							freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
						}
						emufclose64(f); //Close the image!
						return 0; //Invalid IMD file!
					}
				}
			}
			++sectornumber; //Process the next sector number!
			--datarecordnumber; //Processed!
		}
		freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
	}

	//Now, we're at the specified track!
	if (emufread64(&trackinfo, 1, sizeof(trackinfo), f) != sizeof(trackinfo)) //Failed to read track info?
	{
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	//Track info block has been read!
	if (sector >= trackinfo.sectorspertrack) //Not enough to read?
	{
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	if (emufseek64(f, sector, SEEK_CUR) < 0) //Skip the sector number map!
	{
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	if (emufread64(&physicalsectornr, 1, sizeof(physicalsectornr), f) != sizeof(physicalsectornr)) //Read the actual sector number!
	{
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	if (emufseek64(f, trackinfo.sectorspertrack - sector - 1, SEEK_CUR) < 0) //Skip the sector number map!
	{
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	physicalcylindernr = (trackinfo.cylinder); //Default cylinder number!
	if (trackinfo.head_extrabits & IMD_HEAD_CYLINDERMAPPRESENT) //Cylinder map following?
	{
		if (emufseek64(f, sector, SEEK_CUR) < 0) //Skip the cylinder number map!
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if (emufread64(&physicalcylindernr, 1, sizeof(physicalcylindernr), f) != sizeof(physicalcylindernr)) //Read the cylinder number!
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if (emufseek64(f, trackinfo.sectorspertrack - sector - 1, SEEK_CUR) < 0) //Skip the cylinder number map!
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
	}
	physicalheadnr = (trackinfo.head_extrabits & IMD_HEAD_HEADNUMBER); //Default head number!
	if (trackinfo.head_extrabits & IMD_HEAD_HEADMAPPRESENT) //Head map following?
	{
		if (emufseek64(f, sector, SEEK_CUR) < 0) //Skip the head number map!
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if (emufread64(&physicalheadnr, 1, sizeof(physicalheadnr), f) != sizeof(physicalheadnr)) //Read the cylinder number!
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if (emufseek64(f, trackinfo.sectorspertrack - sector - 1, SEEK_CUR) < 0) //Skip the head number map!
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
	}
	if (trackinfo.SectorSize == IMD_SECTORSIZE_SECTORSIZEMAPPRESENT) //Sector size map following?
	{
		sectorsizemap = zalloc((trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP", NULL); //Allocate a sector map to use!
		if (!sectorsizemap) //Failed to allocate?
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if (emufread64(sectorsizemap, 1, (trackinfo.sectorspertrack << 1), f) < 0) //Read the sector size map!
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		sectornumber = 0;
		for (sectornumber = 0; sectornumber < trackinfo.sectorspertrack; ++sectornumber) //Patch as needed!
		{
			sectorsizemap[sectornumber] = SDL_SwapLE16(sectorsizemap[sectornumber]); //Swap all byte ordering to be readable!
		}
	}

	//Skip n sectors!
	datarecordnumber = MIN(trackinfo.sectorspertrack, sector); //How many records to read!
	sectornumber = 0; //Start at the first sector number!
	for (; datarecordnumber;) //Process all sectors on the track!
	{
		if (emufread64(&data, 1, sizeof(data), f) != sizeof(data)) //Read the identifier!
		{
			if (sectorsizemap) //Allocated sector size map?
			{
				freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
			}
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		//Now, we have the identifier!
		if (data) //Not one that's unavailable?
		{
			if (data > 8) //Undefined value?
			{
				if (sectorsizemap) //Allocated sector size map?
				{
					freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
				}
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			if (data & 1) //Normal sector with or without mark, data error or deleted?
			{
				//Skip the sector's data!
				if (sectorsizemap) //Map used?
				{
					if (emufseek64(f, (sectorsizemap[sectornumber]), SEEK_CUR) < 0) //Errored out?
					{
						if (sectorsizemap) //Allocated sector size map?
						{
							freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
						}
						emufclose64(f); //Close the image!
						return 0; //Invalid IMD file!
					}
				}
				else
				{
					if (emufseek64(f, SECTORSIZE_BYTES(trackinfo.SectorSize), SEEK_CUR) < 0) //Errored out?
					{
						if (sectorsizemap) //Allocated sector size map?
						{
							freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
						}
						emufclose64(f); //Close the image!
						return 0; //Invalid IMD file!
					}
				}
			}
			else //Compressed?
			{
				if (emufseek64(f, 1, SEEK_CUR) < 0) //Skip the compressed data!
				{
					if (sectorsizemap) //Allocated sector size map?
					{
						freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
					}
					emufclose64(f); //Close the image!
					return 0; //Invalid IMD file!
				}
			}
		}
		++sectornumber; //Process the next sector number!
		--datarecordnumber; //Processed!
	}

	if (datarecordnumber==0) //Left to read? Reached the sector!
	{
		if (emufread64(&data, 1, sizeof(data), f) != sizeof(data)) //Read the identifier!
		{
			if (sectorsizemap) //Allocated sector size map?
			{
				freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
			}
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		//Now, we have the identifier!
		if (data) //Not one that's unavailable?
		{
			if (data > 8) //Undefined value?
			{
			invalidsectordataRead:
				if (sectorsizemap) //Allocated sector size map?
				{
					freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
				}
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			if (data & 1) //Normal sector with or without mark, data error or deleted?
			{
				//Skip the sector's data!
				if (sectorsizemap) //Map used?
				{
					if (sectorsizemap[sectornumber] != sectorsize) //Sector size mismatch?
					{
						goto invalidsectordataRead; //Error out!
					}
					//Check if we're valid!
					if (emufread64(result,1,sectorsizemap[sectornumber],f)!=sectorsizemap[sectornumber]) //Failed to skip the data part of the sector?
					{
						goto invalidsectordataRead; //Error out!
					}
					physicalsectorsize = sectorsizemap[sectornumber]; //Physical sector size!
					//The sector is loaded and valid!
				sectorreadyRead:
					//Finish up!
					if (sectorsizemap) //Allocated sector size map?
					{
						freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
					}
					emufclose64(f); //Close the image!
					return 1; //Gotten information about the record!
				}
				else
				{
					if (SECTORSIZE_BYTES(trackinfo.SectorSize) != sectorsize) //Sector size mismatch?
					{
						goto invalidsectordataRead; //Error out!
					}
					if (emufread64(result, 1, SECTORSIZE_BYTES(trackinfo.SectorSize), f) < 0) //Errored out?
					{
						goto invalidsectordataRead; //Error out!
					}
					physicalsectorsize = SECTORSIZE_BYTES(trackinfo.SectorSize); //How large are we physically!

					goto sectorreadyRead; //Handle ready sector information!
				}
			}
			else //Compressed?
			{
				if (emufread64(&filldata, 1, sizeof(filldata),f)!=sizeof(filldata)) //Read the compressed data!
				{
					if (sectorsizemap) //Allocated sector size map?
					{
						freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
					}
					emufclose64(f); //Close the image!
					return 0; //Invalid IMD file!
				}
				if (sectorsizemap) //Map used?
				{
					physicalsectorsize = sectorsizemap[sectornumber]; //Errored out?
				}
				else
				{
					physicalsectorsize = SECTORSIZE_BYTES(trackinfo.SectorSize); //Errored out?
				}
				if (physicalsectorsize != sectorsize) //Sector size mismatch?
				{
					goto invalidsectordataRead; //Error out!
				}
				//Fill the result up!
				memset(result, filldata, physicalsectorsize); //Give the result: a sector filled with one type of data!
				goto sectorreadyRead;
			}
		}
		else //Invalid sector to read?
		{
			goto invalidsectordataRead; //Error out!
		}
	}

	//Couldn't read it!

	if (sectorsizemap) //Allocated sector size map?
	{
		freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
	}
	emufclose64(f); //Close the image!
	return 0; //Invalid IMD file!
}

byte writeIMDSector(char* filename, byte track, byte head, byte sector, byte deleted, word sectorsize, void* sectordata)
{
	byte newdatamark = 0x01; //Non-compressed data mark!
	byte newdatamarkcompressed = 0x02; //Compressed data mark!
	uint_32 fillsector_dataleft;
	byte *fillsector=NULL;
	#ifdef MEMORYCONSERVATION
	COMMON_MEMORYCONSERVATION_buffer(tailbuffer)
	COMMON_MEMORYCONSERVATION_buffer(headbuffer)
	#else
	byte* tailbuffer = NULL; //Buffer for the compressed sector until the end!
	byte* headbuffer = NULL; //Buffer for the compressed sector until the end!
	#endif
	FILEPOS tailbuffersize; //Size of the tail buffer!
	FILEPOS compressedsectorpos=0; //Position of the compressed sector!
	FILEPOS eofpos; //EOF position!
	byte retryingheaderror; //Prevents infinite loop on file rewrite!
	byte filldata;
	byte compresseddata_byteval; //What is the compressed data, if it's compressed?
	byte physicalsectornr;
	byte physicalheadnr;
	byte physicalcylindernr;
	word physicalsectorsize; //Effective sector size!
	word sectornumber;
	word* sectorsizemap=NULL; //Sector size map!
	uint_32 datarecordnumber;
	TRACKINFORMATIONBLOCK trackinfo;
	byte identifier[3];
	byte data;
	BIGFILE* f;
	if (strcmp(filename, "") == 0) return 0; //Unexisting: don't even look at it!
	if (!isext(filename, "imd")) //Not our IMD image file?
	{
		return 0; //Not a IMD image!
	}
	fillsector = (byte*)sectordata; //What sector is supposed to be filled with this byte!
	f = emufopen64(filename, "rb+"); //Open the image!
	if (!f) return 0; //Not opened!
	if (emufread64(&identifier, 1, sizeof(identifier), f) != sizeof(identifier)) //Try to read the header?
	{
		emufclose64(f); //Close it!
		return 0; //Not a IMD image!
	}
	if ((identifier[0] != 'I') || (identifier[1] != 'M') || (identifier[2] != 'D')) //Invalid header?
	{
		emufclose64(f); //Close it!
		return 0; //Not a IMD image!
	}
	for (; !emufeof64(f);) //Not EOF yet?
	{
		if (emufread64(&identifier[0], 1, sizeof(identifier[0]), f) != sizeof(identifier[0])) //Failed to read an header comment byte?
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if (identifier[0] == 0x1A) //Finished comment identifier?
		{
			goto validIMDheaderWrite; //Header is validly read!
		}
	}
	//Reached EOF without comment finishing?

	emufclose64(f); //Close the image!
	return 0; //Invalid IMD file!

validIMDheaderWrite:
	if (deleted)
	{
		newdatamark = 0x03; //Deleted data mark!
		newdatamarkcompressed = 0x04; //Compressed data mark!
	}
	//Now, skip tracks until we reach the selected track!
	for (;;) //Skipping left?
	{
		if (emufread64(&trackinfo, 1, sizeof(trackinfo), f) != sizeof(trackinfo)) //Failed to read track info?
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if ((trackinfo.cylinder == track) && ((trackinfo.head_extrabits & IMD_HEAD_HEADNUMBER) == head)) //Track&head found?
		{
			if (emufseek64(f, -((int)sizeof(trackinfo)), SEEK_CUR) < 0) //Found!
			{
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			break; //Stop searching: we're found!
		}
		//Track info block has been read!
		if (emufseek64(f, trackinfo.sectorspertrack, SEEK_CUR) < 0) //Skip the sector number map!
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if (trackinfo.head_extrabits & IMD_HEAD_CYLINDERMAPPRESENT) //Cylinder map following?
		{
			if (emufseek64(f, trackinfo.sectorspertrack, SEEK_CUR) < 0) //Skip the cylinder number map!
			{
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
		}
		if (trackinfo.head_extrabits & IMD_HEAD_HEADMAPPRESENT) //Head map following?
		{
			if (emufseek64(f, trackinfo.sectorspertrack, SEEK_CUR) < 0) //Skip the head number map!
			{
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
		}
		if (trackinfo.SectorSize == IMD_SECTORSIZE_SECTORSIZEMAPPRESENT) //Sector size map following?
		{
			sectorsizemap = zalloc((trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP", NULL); //Allocate a sector map to use!
			if (!sectorsizemap) //Failed to allocate?
			{
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			if (emufread64(sectorsizemap, 1, (trackinfo.sectorspertrack << 1), f) < 0) //Read the sector size map!
			{
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			sectornumber = 0;
			for (sectornumber = 0; sectornumber < trackinfo.sectorspertrack; ++sectornumber) //Patch as needed!
			{
				sectorsizemap[sectornumber] = SDL_SwapLE16(sectorsizemap[sectornumber]); //Swap all byte ordering to be readable!
			}
		}
		datarecordnumber = trackinfo.sectorspertrack; //How many records to read!
		sectornumber = 0; //Start at the first sector number!
		for (; datarecordnumber;) //Process all sectors on the track!
		{
			if (emufread64(&data, 1, sizeof(data), f) != sizeof(data)) //Read the identifier!
			{
				freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			//Now, we have the identifier!
			if (data) //Not one that's unavailable?
			{
				if (data > 8) //Undefined value?
				{
					freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
					emufclose64(f); //Close the image!
					return 0; //Invalid IMD file!
				}
				if (data & 1) //Normal sector with or without mark, data error or deleted?
				{
					//Skip the sector's data!
					if (sectorsizemap) //Map used?
					{
						if (emufseek64(f, (sectorsizemap[sectornumber]), SEEK_CUR) < 0) //Errored out?
						{
							freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
							emufclose64(f); //Close the image!
							return 0; //Invalid IMD file!
						}
					}
					else
					{
						if (emufseek64(f, SECTORSIZE_BYTES(trackinfo.SectorSize), SEEK_CUR) < 0) //Errored out?
						{
							freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
							emufclose64(f); //Close the image!
							return 0; //Invalid IMD file!
						}
					}
				}
				else //Compressed?
				{
					if (emufseek64(f, 1, SEEK_CUR) < 0) //Skip the compressed data!
					{
						if (sectorsizemap) //Allocated sector size map?
						{
							freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
						}
						emufclose64(f); //Close the image!
						return 0; //Invalid IMD file!
					}
				}
			}
			++sectornumber; //Process the next sector number!
			--datarecordnumber; //Processed!
		}
		freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
	}

	//Now, we're at the specified track!
	if (emufread64(&trackinfo, 1, sizeof(trackinfo), f) != sizeof(trackinfo)) //Failed to read track info?
	{
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	//Track info block has been read!
	if (sector >= trackinfo.sectorspertrack) //Not enough to read?
	{
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	if (emufseek64(f, sector, SEEK_CUR) < 0) //Skip the sector number map!
	{
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	if (emufread64(&physicalsectornr, 1, sizeof(physicalsectornr), f) != sizeof(physicalsectornr)) //Read the actual sector number!
	{
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	if (emufseek64(f, trackinfo.sectorspertrack - sector - 1, SEEK_CUR) < 0) //Skip the sector number map!
	{
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	physicalcylindernr = (trackinfo.cylinder); //Default cylinder number!
	if (trackinfo.head_extrabits & IMD_HEAD_CYLINDERMAPPRESENT) //Cylinder map following?
	{
		if (emufseek64(f, sector, SEEK_CUR) < 0) //Skip the cylinder number map!
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if (emufread64(&physicalcylindernr, 1, sizeof(physicalcylindernr), f) != sizeof(physicalcylindernr)) //Read the cylinder number!
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if (emufseek64(f, trackinfo.sectorspertrack - sector - 1, SEEK_CUR) < 0) //Skip the cylinder number map!
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
	}
	physicalheadnr = (trackinfo.head_extrabits & IMD_HEAD_HEADNUMBER); //Default head number!
	if (trackinfo.head_extrabits & IMD_HEAD_HEADMAPPRESENT) //Head map following?
	{
		if (emufseek64(f, sector, SEEK_CUR) < 0) //Skip the head number map!
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if (emufread64(&physicalheadnr, 1, sizeof(physicalheadnr), f) != sizeof(physicalheadnr)) //Read the cylinder number!
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if (emufseek64(f, trackinfo.sectorspertrack - sector - 1, SEEK_CUR) < 0) //Skip the head number map!
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
	}
	if (trackinfo.SectorSize == IMD_SECTORSIZE_SECTORSIZEMAPPRESENT) //Sector size map following?
	{
		sectorsizemap = zalloc((trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP", NULL); //Allocate a sector map to use!
		if (!sectorsizemap) //Failed to allocate?
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if (emufread64(sectorsizemap, 1, (trackinfo.sectorspertrack << 1), f) < 0) //Read the sector size map!
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		sectornumber = 0;
		for (sectornumber = 0; sectornumber < trackinfo.sectorspertrack; ++sectornumber) //Patch as needed!
		{
			sectorsizemap[sectornumber] = SDL_SwapLE16(sectorsizemap[sectornumber]); //Swap all byte ordering to be readable!
		}
	}

	//Skip n sectors!
	datarecordnumber = MIN(trackinfo.sectorspertrack, sector); //How many records to read!
	sectornumber = 0; //Start at the first sector number!
	for (; datarecordnumber;) //Process all sectors on the track!
	{
		if (emufread64(&data, 1, sizeof(data), f) != sizeof(data)) //Read the identifier!
		{
			if (sectorsizemap) //Allocated sector size map?
			{
				freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
			}
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		//Now, we have the identifier!
		if (data) //Not one that's unavailable?
		{
			if (data > 8) //Undefined value?
			{
				if (sectorsizemap) //Allocated sector size map?
				{
					freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
				}
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			if (data & 1) //Normal sector with or without mark, data error or deleted?
			{
				//Skip the sector's data!
				if (sectorsizemap) //Map used?
				{
					if (emufseek64(f, (sectorsizemap[sectornumber]), SEEK_CUR) < 0) //Errored out?
					{
						if (sectorsizemap) //Allocated sector size map?
						{
							freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
						}
						emufclose64(f); //Close the image!
						return 0; //Invalid IMD file!
					}
				}
				else
				{
					if (emufseek64(f, SECTORSIZE_BYTES(trackinfo.SectorSize), SEEK_CUR) < 0) //Errored out?
					{
						if (sectorsizemap) //Allocated sector size map?
						{
							freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
						}
						emufclose64(f); //Close the image!
						return 0; //Invalid IMD file!
					}
				}
			}
			else //Compressed?
			{
				if (emufseek64(f, 1, SEEK_CUR) < 0) //Skip the compressed data!
				{
					if (sectorsizemap) //Allocated sector size map?
					{
						freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
					}
					emufclose64(f); //Close the image!
					return 0; //Invalid IMD file!
				}
			}
		}
		++sectornumber; //Process the next sector number!
		--datarecordnumber; //Processed!
	}

	if (datarecordnumber==0) //Left to read? Reached the sector!
	{
		compressedsectorpos = emuftell64(f); //What is the location of the compressed sector in the original file, if it's compressed!
		if (compressedsectorpos < 0) goto failedcompressedposWrite; //Failed detecting the compressed location?
		#ifdef MEMORYCONSERVATION
		COMMON_MEMORYCONSERVATION_BEGINBUFFER(headbuffer,failedcompressedposWrite,headbufferReady)
		headbufferReady:
		#else
		headbuffer = (byte*)zalloc(compressedsectorpos, "IMDIMAGE_FAILEDHEADBUFFER",NULL);
		#endif
		if (emufread64(&data, 1, sizeof(data), f) != sizeof(data)) //Read the identifier!
		{
			failedcompressedposWrite:
			if (sectorsizemap) //Allocated sector size map?
			{
				freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
			}
			if (COMMON_MEMORYCONSERVATION_ALLOCATED(headbuffer)) //Head buffer allocated?
			{
				#ifdef MEMORYCONSERVATION
				COMMON_MEMORYCONSERVATION_ENDBUFFER(headbuffer)
				#else
				freez((void**)&headbuffer, compressedsectorpos, "IMDIMAGE_FAILEDHEADBUFFER"); //Free the allocated sector size map!
				#endif
			}
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		//Now, we have the identifier!
		if (data) //Not one that's unavailable?
		{
			if (data > 8) //Undefined value?
			{
			invalidsectordataWrite:
				if (((data & 1) == 0) && (data <= 8)) //Needs restoration of the compressed type information?
				{
					if (emufseek64(f, compressedsectorpos, SEEK_SET) < 0) //Could seek?
					{
						goto failedrestoringsectortypeWrite; //Error out!
					}
					if (emufwrite64(&data, 1, sizeof(data), f) != sizeof(data)) //Failed restoring it?
					{
						goto failedrestoringsectortypeWrite; //Error out!
					}
				}
				failedrestoringsectortypeWrite:
				if (sectorsizemap) //Allocated sector size map?
				{
					freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
				}
				if (COMMON_MEMORYCONSERVATION_ALLOCATED(headbuffer)) //Head buffer allocated?
				{
					#ifdef MEMORYCONSERVATION
					COMMON_MEMORYCONSERVATION_ENDBUFFER(headbuffer)
					#else
					freez((void**)&headbuffer, compressedsectorpos, "IMDIMAGE_FAILEDHEADBUFFER"); //Free the allocated sector size map!
					#endif
				}
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			if (data & 1) //Normal sector with or without mark, data error or deleted?
			{
				if (emufseek64(f, -1, SEEK_CUR) < 0) //Could goto sector mark?
				{
					goto invalidsectordataWrite;
				}
				if (emufwrite64(&newdatamark, 1, sizeof(newdatamark), f) != sizeof(newdatamark)) //Couldn't write new data mark?
				{
					goto invalidsectordataWrite;
				}
				//Skip the sector's data!
				if (sectorsizemap) //Map used?
				{
					if (sectorsizemap[sectornumber] != sectorsize) //Sector size mismatch?
					{
						goto invalidsectordataWrite; //Error out!
					}
					//Check if we're valid!
					if (emufwrite64(sectordata, 1, sectorsizemap[sectornumber], f) != sectorsizemap[sectornumber]) //Failed to skip the data part of the sector?
					{
						goto invalidsectordataWrite; //Error out!
					}
					physicalsectorsize = sectorsizemap[sectornumber]; //Physical sector size!
					//The sector is loaded and valid!
				sectorreadyWrite:
					//Finish up!
					if (sectorsizemap) //Allocated sector size map?
					{
						freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
					}
					if (COMMON_MEMORYCONSERVATION_ALLOCATED(headbuffer)) //Head buffer allocated?
					{
						#ifdef MEMORYCONSERVATION
						COMMON_MEMORYCONSERVATION_ENDBUFFER(headbuffer)
						#else
						freez((void**)&headbuffer, compressedsectorpos, "IMDIMAGE_FAILEDHEADBUFFER"); //Free the allocated sector size map!
						#endif
					}
					emufclose64(f); //Close the image!
					return 1; //Gotten information about the record!
				}
				else
				{
					if (SECTORSIZE_BYTES(trackinfo.SectorSize) != sectorsize) //Sector size mismatch?
					{
						goto invalidsectordataWrite; //Error out!
					}
					if (emufwrite64(sectordata, 1, SECTORSIZE_BYTES(trackinfo.SectorSize), f) < 0) //Errored out?
					{
						goto invalidsectordataWrite; //Error out!
					}
					physicalsectorsize = SECTORSIZE_BYTES(trackinfo.SectorSize); //How large are we physically!

					goto sectorreadyWrite; //Handle ready sector information!
				}
			}
			else //Compressed?
			{
				if (emufread64(&filldata, 1, sizeof(filldata), f) != sizeof(filldata)) //Read the compressed data!
				{
					if (sectorsizemap) //Allocated sector size map?
					{
						freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
					}
					if (COMMON_MEMORYCONSERVATION_ALLOCATED(headbuffer)) //Head buffer allocated?
					{
						#ifdef MEMORYCONSERVATION
						COMMON_MEMORYCONSERVATION_ENDBUFFER(headbuffer);
						#else
						freez((void**)&headbuffer, compressedsectorpos, "IMDIMAGE_FAILEDHEADBUFFER"); //Free the allocated sector size map!
						#endif
					}
					emufclose64(f); //Close the image!
					return 0; //Invalid IMD file!
				}
				if (sectorsizemap) //Map used?
				{
					physicalsectorsize = sectorsizemap[sectornumber]; //Errored out?
				}
				else
				{
					physicalsectorsize = SECTORSIZE_BYTES(trackinfo.SectorSize); //Errored out?
				}
				if (physicalsectorsize != sectorsize) //Sector size mismatch?
				{
					goto invalidsectordataWrite; //Error out!
				}
				//Fill the result up!
				compresseddata_byteval = fillsector[0]; //What byte are we checking to be compressed!
				fillsector_dataleft = physicalsectorsize; //How much data is left to check!

				for (; fillsector_dataleft;) //Check all bytes for the fill byte!
				{
					if (*(fillsector++) != compresseddata_byteval) //Not compressed anymore?
					{
						break; //Stop looking: we're not compressed data anymore!
					}
					--fillsector_dataleft; //One byte has been checked!
				}
				if (fillsector_dataleft == 0) //Compressed data stays compressed?
				{
					if (emufseek64(f, -2, SEEK_CUR) < 0) //Go back to the compressed byte!
					{
						goto invalidsectordataWrite; //Error out!
					}
					if (emufwrite64(&newdatamarkcompressed, 1, sizeof(newdatamarkcompressed), f) != sizeof(newdatamark)) //Couldn't write new data mark?
					{
						goto invalidsectordataWrite;
					}
					if (emufwrite64(&compresseddata_byteval, 1, sizeof(compresseddata_byteval), f) != sizeof(compresseddata_byteval)) //Update the compressed data!
					{
						goto invalidsectordataWrite; //Error out!
					}
					//The compressed sector data has been updated!
					goto sectorreadyWrite; //Success!
				}
				else //We need to convert the compressed sector into a non-compressed sector!
				{
					//First, read all data that's following the compressed sector into memory!
					if (emufseek64(f, 0, SEEK_END) < 0) //Failed getting to EOF?
					{
						goto invalidsectordataWrite; //Error out!
					}
					if (emuftell64(f) < 0) //Invalid to update?
					{
						goto invalidsectordataWrite; //Error out!
					}
					eofpos = emuftell64(f); //What is the location of the EOF!
					if (emufseek64(f, compressedsectorpos + 2ULL, SEEK_SET) < 0) //Return to the compressed sector's following data!
					{
						goto invalidsectordataWrite;
					}
					//Now, allocate a buffer to contain it!
					tailbuffersize = (eofpos - compressedsectorpos - 2ULL); //How large should the tail buffer be?
					if (tailbuffersize)
					{
						#ifdef MEMORYCONSERVATION
						COMMON_MEMORYCONSERVATION_BEGINBUFFER(tailbuffer,invalidsectordataWrite,tailbufferallocatedWrite)
						tailbufferallocatedWrite:
						#else
						tailbuffer = (byte*)zalloc(tailbuffersize, "IMDIMAGE_FOOTERDATA", NULL); //Allocate room for the footer to be contained!
						if (tailbuffer == NULL) //Failed to allocate?
						{
							goto invalidsectordataWrite; //Error out!
						}
						#endif
						//Tail buffer is ready, now fill it up!
						#ifdef MEMORYCONSERVATION
						COMMON_MEMORYCONSERVATION_READBUFFER(f,tailbuffer,tailbuffersize,tailbufferfilledFailed,tailbufferfilledWrite)
						tailbufferfilledFailed:
						#else
						if (emufread64(tailbuffer, 1, tailbuffersize, f) != tailbuffersize) //Failed to read?
						#endif
						{
							freez((void**)&tailbuffer, tailbuffersize, "IMDIMAGE_FOOTERDATA"); //Release the tail buffer!
							goto invalidsectordataWrite; //Error out!
						}
					}
					#ifdef MEMORYCONSERVATION
					tailbufferfilledWrite:
					#endif
					//Tail buffer is filled, now return to the sector to write!
					if (emufseek64(f, compressedsectorpos, SEEK_SET) < 0) //Return to the compressed sector's following data!
					{
						goto invalidsectordataWrite;
					}
					//Now, try to write the sector ID and data!
					compresseddata_byteval = newdatamark; //A valid sector ID!
					if (emufwrite64(&compresseddata_byteval, 1, sizeof(compresseddata_byteval), f) != sizeof(compresseddata_byteval)) //Write the sector ID!
					{
						goto invalidsectordataWrite; //Error out!
					}
					if (emufwrite64(sectordata, 1, physicalsectorsize, f) != physicalsectorsize) //Failed writing the physical sector?
					{
						//Perform an undo operation!
					undoUncompressedsectorwriteWrite: //Undo it!
						if (emufseek64(f, 0, SEEK_SET) < 0) //Failed getting to BOF?
						{
							goto invalidsectordataWrite; //Error out!
						}
						#ifdef MEMORYCONSERVATION
						COMMON_MEMORYCONSERVATION_READBUFFER(f,headbuffer,compressedsectorpos,invalidsectordataWrite,successreadheadbufferWrite)
						#else
						if (emufread64(headbuffer, 1, compressedsectorpos, f) != compressedsectorpos) //Failed reading the head?
						#endif
						{
							goto invalidsectordataWrite; //Error out!
						}
						#ifdef MEMORYCONSERVATION
						successreadheadbufferWrite:
						#endif
						if (emufseek64(f, compressedsectorpos, SEEK_SET) < 0) //Failed to seek?
						{
							#ifdef MEMORYCONSERVATION
							COMMON_MEMORYCONSERVATION_ENDBUFFER(tailbuffer)
							#else
							freez((void**)&tailbuffer, tailbuffersize, "IMDIMAGE_FOOTERDATA"); //Release the tail buffer!
							#endif
							goto invalidsectordataWrite; //Error out!
						}
						retryingheaderror = 1; //Allow retrying rewrite once!
					retryclearedendWrite: //After writing the head buffer when not having reached EOF at the end of this!
						if (emufwrite64(&data, 1, sizeof(data), f) != sizeof(data)) //Failed to write the compressed ID?
						{
							#ifdef MEMORYCONSERVATION
							COMMON_MEMORYCONSERVATION_ENDBUFFER(tailbuffer)
							#else
							freez((void**)&tailbuffer, tailbuffersize, "IMDIMAGE_FOOTERDATA"); //Release the tail buffer!
							#endif
							goto invalidsectordataWrite; //Error out!
						}
						if (emufwrite64(&filldata, 1, sizeof(filldata), f) != sizeof(filldata)) //Failed writing the original data back?
						{
							#ifdef MEMORYCONSERVATION
							COMMON_MEMORYCONSERVATION_ENDBUFFER(tailbuffer)
							#else
							freez((void**)&tailbuffer, tailbuffersize, "IMDIMAGE_FOOTERDATA"); //Release the tail buffer!
							#endif
							goto invalidsectordataWrite; //Error out!
						}
						if (COMMON_MEMORYCONSERVATION_ALLOCATED(tailbuffer))
						{
							#ifdef MEMORYCONSERVATION
							COMMON_MEMORYCONSERVATION_WRITEBUFFER(f,tailbuffer,tailbuffersize,invalidsectordataWrite,tailbufferWrittenWrite)
							#else
							if (emufwrite64(tailbuffer, 1, tailbuffersize, f) != tailbuffersize) //Failed writing the original tail data back?
							#endif
							{
								#ifndef MEMORYCONSERVATION
								freez((void**)&tailbuffer, tailbuffersize, "IMDIMAGE_FOOTERDATA"); //Release the tail buffer!
								#endif
								goto invalidsectordataWrite; //Error out!
							}
						}
						#ifdef  MEMORYCONSERVATION
						tailbufferWrittenWrite:
						#endif
						if (emufseek64(f, 0, SEEK_END) < 0) //Couldn't seek to EOF?
						{
							freez((void**)&tailbuffer, tailbuffersize, "IMDIMAGE_FOOTERDATA"); //Release the tail buffer!
							goto invalidsectordataWrite; //Error out!
						}
						if ((emuftell64(f)!=eofpos) && retryingheaderror) //EOF has changed to an incorrect value?
						{
							emufclose64(f); //Close the image!
							f = emufopen64(filename, "wb"); //Open the image!
							if (!f) //Failed to reopen?
							{
								if (COMMON_MEMORYCONSERVATION_ALLOCATED(headbuffer)) //Head buffer allocated?
								{
									#ifdef MEMORYCONSERVATION
									dofailheadbufferWrite:
									COMMON_MEMORYCONSERVATION_ENDBUFFER(headbuffer)
									#else
									freez((void**)&headbuffer, compressedsectorpos, "IMDIMAGE_FAILEDHEADBUFFER"); //Free the allocated sector size map!
									#endif
								}
								#ifdef MEMORYCONSERVATION
								COMMON_MEMORYCONSERVATION_ENDBUFFER(tailbuffer)
								#else
								freez((void**)&tailbuffer, tailbuffersize, "IMDIMAGE_FOOTERDATA"); //Release the tail buffer!
								#endif
								goto invalidsectordataWrite; //Error out!
							}
							#ifdef MEMORYCONSERVATION
							COMMON_MEMORYCONSERVATION_WRITEBUFFER(f,headbuffer,compressedsectorpos,dofailheadbufferWrite,successWriteHeadBufferRetry)
							#else
							if (emufwrite64(headbuffer, 1, compressedsectorpos, f) != compressedsectorpos) //Failed writing the original head data back?
							#endif
							{
								if (COMMON_MEMORYCONSERVATION_ALLOCATED(headbuffer)) //Head buffer allocated?
								{
									freez((void**)&headbuffer, compressedsectorpos, "IMDIMAGE_FAILEDHEADBUFFER"); //Free the allocated sector size map!
								}
								freez((void**)&tailbuffer, tailbuffersize, "IMDIMAGE_FOOTERDATA"); //Release the tail buffer!
								goto invalidsectordataWrite; //Error out!
							}
							#ifdef MEMORYCONSERVATION
							successWriteHeadBufferRetry:
							#endif
							retryingheaderror = 0; //Fail if this occurs again, prevent infinite loop!
							goto retryclearedendWrite; //Retry with the end having been cleared!
						}
						#ifdef MEMORYCONSERVATION
						COMMON_MEMORYCONSERVATION_ENDBUFFER(tailbuffer)
						#else
						freez((void**)&tailbuffer, tailbuffersize, "IMDIMAGE_FOOTERDATA"); //Release the tail buffer!
						#endif
						if (COMMON_MEMORYCONSERVATION_ALLOCATED(headbuffer)) //Head buffer allocated?
						{
							#ifdef MEMORYCONSERVATION
							COMMON_MEMORYCONSERVATION_ENDBUFFER(headbuffer)
							#else
							freez((void**)&headbuffer, compressedsectorpos, "IMDIMAGE_FAILEDHEADBUFFER"); //Free the allocated sector size map!
							#endif
						}
						goto invalidsectordataWrite; //Error out always, since we couldn't update the real data!
					}
					if (COMMON_MEMORYCONSERVATION_ALLOCATED(tailbuffer))
					{
						#ifdef MEMORYCONSERVATION
						COMMON_MEMORYCONSERVATION_WRITEBUFFER(f,tailbuffer,tailbuffersize,undoUncompressedsectorwriteWrite,successWriteTailBufferWrite)
						#else
						if (emufwrite64(tailbuffer, 1, tailbuffersize, f) != tailbuffersize) //Couldn't update the remainder of the file correctly?
						#endif
						{
							goto undoUncompressedsectorwriteWrite; //Perform an undo operation, if we can!
						}
					}
					#ifdef MEMORYCONSERVATION
					successWriteTailBufferWrite:
					#endif
					//We've successfully updated the file with a new sector!
					//The compressed sector data has been updated!
					#ifdef MEMORYCONSERVATION
					COMMON_MEMORYCONSERVATION_ENDBUFFER(tailbuffer)
					#else
					freez((void**)&tailbuffer, tailbuffersize, "IMDIMAGE_FOOTERDATA"); //Release the tail buffer!
					#endif
					if (COMMON_MEMORYCONSERVATION_ALLOCATED(headbuffer)) //Head buffer allocated?
					{
						#ifdef MEMORYCONSERVATION
						COMMON_MEMORYCONSERVATION_ENDBUFFER(headbuffer)
						#else
						freez((void**)&headbuffer, compressedsectorpos, "IMDIMAGE_FAILEDHEADBUFFER"); //Free the allocated sector size map!
						#endif
					}
					goto sectorreadyWrite; //Success!
				}
				//memset(result, filldata, physicalsectorsize); //Give the result: a sector filled with one type of data!
				goto invalidsectordataWrite; //Couldn't properly update the compressed sector data!
			}
		}
		else //Invalid sector to write?
		{
			goto invalidsectordataWrite; //Error out!
		}
	}

	//Couldn't find the sector!

	if (sectorsizemap) //Allocated sector size map?
	{
		freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
	}
	if (COMMON_MEMORYCONSERVATION_ALLOCATED(headbuffer)) //Head buffer allocated?
	{
		#ifdef MEMORYCONSERVATION
		COMMON_MEMORYCONSERVATION_ENDBUFFER(headbuffer)
		#else
		freez((void**)&headbuffer, compressedsectorpos, "IMDIMAGE_FAILEDHEADBUFFER"); //Free the allocated sector size map!
		#endif
	}
	emufclose64(f); //Close the image!
	return 0; //Invalid IMD file!
}

byte formatIMDTrack(char* filename, byte track, byte head, byte MFM, byte speed, byte filldata, byte sectorsizeformat, byte numsectors, byte* sectordata)
{
	byte wasskippingtrack=0;
	word currentsector;
	byte firstsectorsize;
	byte b;
	word w;
	byte skippingtrack = 0; //Skipping this track once?
	byte* sectordataptr = NULL;
	#ifdef MEMORYCONSERVATION
	COMMON_MEMORYCONSERVATION_buffer(tailbuffer)
	COMMON_MEMORYCONSERVATION_buffer(headbuffer)
	COMMON_MEMORYCONSERVATION_buffer(oldsectordata)
	#else
	byte* tailbuffer = NULL; //Buffer for the compressed sector until the end!
	byte* headbuffer = NULL; //Buffer for the compressed sector until the end!
	byte* oldsectordata = NULL; //Old sector data!
	#endif
	byte* sectornumbermap = NULL; //Original sector number map!
	byte* cylindermap = NULL; //Original cylinder map!
	byte* headmap = NULL; //Original head map!
	FILEPOS oldsectordatasize=0;
	FILEPOS tailbuffersize = 0; //Size of the tail buffer!
	FILEPOS headbuffersize = 0; //Position of the compressed sector!
	FILEPOS tailpos; //Tail position!
	byte searchingheadsize = 1; //Were we searching the head size?
	word sectornumber;
	word* sectorsizemap = NULL; //Original sector size map!
	uint_32 datarecordnumber;
	TRACKINFORMATIONBLOCK trackinfo, newtrackinfo; //Original and new track info!
	byte identifier[3];
	byte data;
	BIGFILE* f;
	if (strcmp(filename, "") == 0) return 0; //Unexisting: don't even look at it!
	if (!isext(filename, "imd")) //Not our IMD image file?
	{
		return 0; //Not a IMD image!
	}
	f = emufopen64(filename, "rb+"); //Open the image!
	if (!f) return 0; //Not opened!
	if (emufread64(&identifier, 1, sizeof(identifier), f) != sizeof(identifier)) //Try to read the header?
	{
		emufclose64(f); //Close it!
		return 0; //Not a IMD image!
	}
	if ((identifier[0] != 'I') || (identifier[1] != 'M') || (identifier[2] != 'D')) //Invalid header?
	{
		emufclose64(f); //Close it!
		return 0; //Not a IMD image!
	}
	for (; !emufeof64(f);) //Not EOF yet?
	{
		if (emufread64(&identifier[0], 1, sizeof(identifier[0]), f) != sizeof(identifier[0])) //Failed to read an header comment byte?
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if (identifier[0] == 0x1A) //Finished comment identifier?
		{
			goto validIMDheaderFormat; //Header is validly read!
		}
	}
	//Reached EOF without comment finishing?

	emufclose64(f); //Close the image!
	return 0; //Invalid IMD file!

validIMDheaderFormat:
	//Now, skip tracks until we reach the selected track!
	for (;;) //Skipping left?
	{
		if ((wasskippingtrack) && (skippingtrack==0)) //Were we skipping a track?
		{
			break; //Finished skipping the track!
		}
	format_skipFormattedTrack: //Skip the formatted track when formatting!
		if (emufread64(&trackinfo, 1, sizeof(trackinfo), f) != sizeof(trackinfo)) //Failed to read track info?
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if ((((trackinfo.cylinder == track) && ((trackinfo.head_extrabits & IMD_HEAD_HEADNUMBER) == head)) || (wasskippingtrack)) && (skippingtrack == 0)) //Track&head found?
		{
			wasskippingtrack = 0; //Not skipping anymore!
			if (emufseek64(f, -((int)sizeof(trackinfo)), SEEK_CUR) < 0) //Found!
			{
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			break; //Stop searching: we're found!
		}
		if (skippingtrack) //Skipping a track?
		{
			--skippingtrack; //One track has been skipped!
		}
		//Track info block has been read!
		if (emufseek64(f, trackinfo.sectorspertrack, SEEK_CUR) < 0) //Skip the sector number map!
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		if (trackinfo.head_extrabits & IMD_HEAD_CYLINDERMAPPRESENT) //Cylinder map following?
		{
			if (emufseek64(f, trackinfo.sectorspertrack, SEEK_CUR) < 0) //Skip the cylinder number map!
			{
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
		}
		if (trackinfo.head_extrabits & IMD_HEAD_HEADMAPPRESENT) //Head map following?
		{
			if (emufseek64(f, trackinfo.sectorspertrack, SEEK_CUR) < 0) //Skip the head number map!
			{
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
		}
		if (trackinfo.SectorSize == IMD_SECTORSIZE_SECTORSIZEMAPPRESENT) //Sector size map following?
		{
			sectorsizemap = zalloc((trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP", NULL); //Allocate a sector map to use!
			if (!sectorsizemap) //Failed to allocate?
			{
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			if (emufread64(sectorsizemap, 1, (trackinfo.sectorspertrack << 1), f) < 0) //Read the sector size map!
			{
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			sectornumber = 0;
			for (sectornumber = 0; sectornumber < trackinfo.sectorspertrack; ++sectornumber) //Patch as needed!
			{
				sectorsizemap[sectornumber] = SDL_SwapLE16(sectorsizemap[sectornumber]); //Swap all byte ordering to be readable!
			}
		}
		datarecordnumber = trackinfo.sectorspertrack; //How many records to read!
		sectornumber = 0; //Start at the first sector number!
		for (; datarecordnumber;) //Process all sectors on the track!
		{
			if (emufread64(&data, 1, sizeof(data), f) != sizeof(data)) //Read the identifier!
			{
				freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
				emufclose64(f); //Close the image!
				return 0; //Invalid IMD file!
			}
			//Now, we have the identifier!
			if (data) //Not one that's unavailable?
			{
				if (data > 8) //Undefined value?
				{
					freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
					emufclose64(f); //Close the image!
					return 0; //Invalid IMD file!
				}
				if (data & 1) //Normal sector with or without mark, data error or deleted?
				{
					//Skip the sector's data!
					if (sectorsizemap) //Map used?
					{
						if (emufseek64(f, (sectorsizemap[sectornumber]), SEEK_CUR) < 0) //Errored out?
						{
							freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
							emufclose64(f); //Close the image!
							return 0; //Invalid IMD file!
						}
					}
					else
					{
						if (emufseek64(f, SECTORSIZE_BYTES(trackinfo.SectorSize), SEEK_CUR) < 0) //Errored out?
						{
							freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
							emufclose64(f); //Close the image!
							return 0; //Invalid IMD file!
						}
					}
				}
				else //Compressed?
				{
					if (emufseek64(f, 1, SEEK_CUR) < 0) //Skip the compressed data!
					{
						if (sectorsizemap) //Allocated sector size map?
						{
							freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
						}
						emufclose64(f); //Close the image!
						return 0; //Invalid IMD file!
					}
				}
			}
			++sectornumber; //Process the next sector number!
			--datarecordnumber; //Processed!
		}
		freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
	}

	//Save the head position!
	if (searchingheadsize) //Searching the head size?
	{
		if (emufeof64(f)) //At EOF? Invalid track!
		{
			goto errorOutFormat; //Erroring out on the formatting process!
		}
		//Now, we're at the specified track!
		if (emuftell64(f) < 0) //Can't find the head position&size?
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}
		headbuffersize = emuftell64(f); //Head buffer size of previous tracks!

		if (emufread64(&trackinfo, 1, sizeof(trackinfo), f) != sizeof(trackinfo)) //Failed to read track info?
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}

		if (emufseek64(f, -((int)sizeof(trackinfo)), SEEK_CUR) < 0) //Go back to the track information!
		{
			emufclose64(f); //Close the image!
			return 0; //Invalid IMD file!
		}

		searchingheadsize = 0; //Not searching the head size anymore!
		skippingtrack = 1; //Skipping one track!
		wasskippingtrack = 1; //We were skipping tracks! Ignore the next track information's details!
		goto format_skipFormattedTrack; //Skip the track to format!
	}

	//Now, we're at the track after the track to format!
	tailpos = emuftell64(f); //The position of the tail in the original file!
	if (emufseek64(f, 0, SEEK_END) < 0) //Couldn't seek to EOF?
	{
		goto errorOutFormat;
	}

	tailbuffersize = emuftell64(f) - tailpos; //The size of the tail buffer! 

	if (emufseek64(f, headbuffersize, SEEK_SET) < 0) //Can't seek to the track we want to write?
	{
		goto errorOutFormat; //Error out!
	}

	if (emufread64(&trackinfo, 1, sizeof(trackinfo), f) != sizeof(trackinfo)) //Failed to read old track info?
	{
		emufclose64(f); //Close the image!
		return 0; //Invalid IMD file!
	}
	//Now, we have the start of the track, end of the track and end of the following tracks! We need to load the head, current track's data and following tracks into memory!
	if (headbuffersize)
	{
		#ifdef MEMORYCONSERVATION
		COMMON_MEMORYCONSERVATION_BEGINBUFFER(headbuffer,errorOutFormat,headbufferallocated)
		#else
		headbuffer = (byte*)zalloc(headbuffersize, "IMDIMAGE_HEADBUFFER", NULL); //Allocate a head buffer!
		#endif
	}
	#ifdef MEMORYCONSERVATION
	headbufferallocated:
	#endif
	if ((COMMON_MEMORYCONSERVATION_ALLOCATED(headbuffer) == NULL) && headbuffersize) goto errorOutFormat; //Error out if we can't allocate!
	if (emufseek64(f, 0, SEEK_SET) < 0) //Failed to get to BOF?
	{
		goto errorOutFormat;
	}
#ifdef MEMORYCONSERVATION
	COMMON_MEMORYCONSERVATION_READBUFFER(f,headbuffer,headbuffersize,errorOutFormat,headbufferReadFormat)
	headbufferReadFormat:
#else
	if (emufread64(headbuffer, 1, headbuffersize, f) != headbuffersize) goto errorOutFormat; //Couldn't read the old head!
#endif
	if (tailbuffersize) //Gotten a size to use?
	{
		#ifdef MEMORYCONSERVATION
		COMMON_MEMORYCONSERVATION_BEGINBUFFER(tailbuffer,errorOutFormat,tailbufferallocated)
		#else
		tailbuffer = (byte*)zalloc(tailbuffersize, "IMDIMAGE_TAILBUFFER", NULL); //Allocate a tail buffer!
		#endif
		if ((COMMON_MEMORYCONSERVATION_ALLOCATED(tailbuffer) == NULL) && tailbuffersize) goto errorOutFormat; //Error out if we can't allocate!
	}
	#ifdef MEMORYCONSERVATION
	tailbufferallocated:
	#endif
	if (emufseek64(f, tailpos, SEEK_SET) < 0) //Failed to get to next track?
	{
		goto errorOutFormat;
	}
	if (COMMON_MEMORYCONSERVATION_ALLOCATED(tailbuffer)) //Have a tail buffer?
	{
		#ifdef MEMORYCONSERVATION
		COMMON_MEMORYCONSERVATION_READBUFFER(f,tailbuffer,tailbuffersize,errorOutFormat,tailbufferReadFormat)
		#else
		if (emufread64(tailbuffer, 1, tailbuffersize, f) != tailbuffersize) goto errorOutFormat; //Couldn't read the old tail!
		#endif
	}
	#ifdef MEMORYCONSERVATION
	tailbufferReadFormat:
	#endif
	if (emufseek64(f, headbuffersize + sizeof(trackinfo), SEEK_SET) < 0) goto errorOutFormat; //Couldn't goto sector number map!
	if (trackinfo.sectorspertrack) //Gotten sectors per track?
	{
		sectornumbermap = (byte*)zalloc(trackinfo.sectorspertrack, "IMDIMAGE_SECTORNUMBERMAP", NULL); //Allocate the sector number map!
		if (sectornumbermap == NULL) goto errorOutFormat;
		if (emufread64(sectornumbermap, 1, trackinfo.sectorspertrack, f) != trackinfo.sectorspertrack) goto errorOutFormat;
		if (trackinfo.head_extrabits & IMD_HEAD_CYLINDERMAPPRESENT) //Cylinder map following?
		{
			cylindermap = (byte*)zalloc(trackinfo.sectorspertrack, "IMDIMAGE_CYLINDERMAP", NULL); //Allocate the cylinder map!
			if (cylindermap == NULL) goto errorOutFormat; //Error out if we can't allocate!
			if (emufread64(cylindermap, 1, trackinfo.sectorspertrack, f) != trackinfo.sectorspertrack) goto errorOutFormat;
		}
		if (trackinfo.head_extrabits & IMD_HEAD_HEADMAPPRESENT) //Head map following?
		{
			headmap = (byte*)zalloc(trackinfo.sectorspertrack, "IMDIMAGE_HEADMAP", NULL); //Allocate the cylinder map!
			if (headmap == NULL) goto errorOutFormat; //Error out if we can't allocate!
			if (emufread64(headmap, 1, trackinfo.sectorspertrack, f) != trackinfo.sectorspertrack) goto errorOutFormat;
		}
		sectorsizemap = NULL; //Default: no sector size map was present!
		if (trackinfo.SectorSize == IMD_SECTORSIZE_SECTORSIZEMAPPRESENT) //Sector size map following?
		{
			sectorsizemap = zalloc((trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP", NULL); //Allocate a sector map to use!
			if (!sectorsizemap) //Failed to allocate?
			{
				goto errorOutFormat;
			}
			if (emufread64(sectorsizemap, 1, (trackinfo.sectorspertrack << 1), f) != (trackinfo.sectorspertrack << 1)) //Read the sector size map!
			{
				goto errorOutFormat;
			}
			//Leave the map alone for easier restoring in case of errors!
		}
	}

	oldsectordatasize = 0; //Initialize the old sector data size for the track!
	if (emuftell64(f) < 0) goto errorOutFormat; //Can't format if can't tell the size of the data!
	oldsectordatasize = emuftell64(f); //Old sector data size!
	oldsectordatasize = tailpos - oldsectordatasize; //The size of the old sector data!
	if (oldsectordatasize) //Gotten sector data?
	{
		#ifdef MEMORYCONSERVATION
		COMMON_MEMORYCONSERVATION_BEGINBUFFER(oldsectordata,errorOutFormat,oldsectordataRead_ReadingFormat)
		oldsectordataRead_ReadingFormat:
		COMMON_MEMORYCONSERVATION_READBUFFER(f,oldsectordata,oldsectordatasize,errorOutFormat,oldsectordataReadFormat)
		#else
		oldsectordata = (byte*)zalloc(oldsectordatasize, "IMDIMAGE_OLDSECTORDATA", NULL); //Allocate the old sector data!
		if (oldsectordata == NULL) goto errorOutFormat; //Error out if we can't allocate!
		if (emufread64(oldsectordata, 1, oldsectordatasize, f) != oldsectordatasize) goto errorOutFormat; //Read the old sector data!
		#endif
	}

	#ifdef MEMORYCONSERVATION
	oldsectordataReadFormat:
	#endif

	//Now, we have the entire track loaded in memory, along with the previous(head), the old track(both heads) and next(tail) tracks for restoration!

	//Here, we can reopen the file for formatting it, write a new track based on the information we now got form the disk and the new head data!
	emufclose64(f); //Close the old file first, we're recreating it!
	f = emufopen64(filename, "wb+"); //Open the image and clear it!
	if (!f) goto errorOutFormat; //Not opened!
	//First, the head!
	if (COMMON_MEMORYCONSERVATION_ALLOCATED(headbuffer))
	{
		#ifdef MEMORYCONSERVATION
		COMMON_MEMORYCONSERVATION_WRITEBUFFER(f,headbuffer,headbuffersize,failedwritingheadbufferFormat,wroteHeadBufferFormat)
		failedwritingheadbufferFormat:
		emufclose64(f); //Close the file!
		goto errorOutFormat_restore; //Error out and restore!
		#else
		if (emufwrite64(headbuffer, 1, headbuffersize, f) != headbuffersize)
		{
			emufclose64(f); //Close the file!
			goto errorOutFormat_restore; //Error out and restore!
		}
		#endif
	}

	#ifdef MEMORYCONSERVATION
	wroteHeadBufferFormat:
	#endif

	//Now the new track to create!
	//First, create a new track header!
	newtrackinfo.cylinder = trackinfo.cylinder; //Same cylinder!
	newtrackinfo.head_extrabits = (trackinfo.head_extrabits & IMD_HEAD_HEADNUMBER) | IMD_HEAD_HEADMAPPRESENT | IMD_HEAD_CYLINDERMAPPRESENT; //Head of the track, with head map and cylinder map present!
	newtrackinfo.mode = MFM ? ((speed < 3) ? (3 + speed) : 0) : ((speed < 3) ? (speed) : 0); //Mode: MFM or FM in 500,300,250.
	newtrackinfo.sectorspertrack = MAX(numsectors,1); //Amount of sectors on this track! Minimum value is 1, according to the specification!

	//Then, size map determination, if it's to be used or not!
	sectordataptr = &sectordata[3]; //Size number used during formatting!
	firstsectorsize = *sectordataptr; //First byte for reference!
	newtrackinfo.SectorSize = firstsectorsize; //Not a custom size map by default(to be compatible with the original specification when possible)!
	if (!numsectors) //No sectors?
	{
		newtrackinfo.SectorSize = 0; //No sector size default without sectors!
	}
	for (currentsector = 0; currentsector < numsectors; ++currentsector) //Process all size numbers!
	{
		b = *sectordataptr; //The size number!
		if ((b == 0xFF) || (sectorsizeformat==0xFF)) //Invalid sector size specified that's a reserved value in the track header or formatting is set up to be custom?
		{
			newtrackinfo.SectorSize = 0xFF; //Custom map is to be used!
			break; //Finish searching!
		}
		if (b != firstsectorsize) //Different sector size encountered?
		{
			newtrackinfo.SectorSize = 0xFF; //Custom map is to be used!
			break; //Finish searching!
		}
		sectordataptr += 4; //Next record!
	}

	//Write the track header!
	if (emufwrite64(&newtrackinfo, 1, sizeof(newtrackinfo), f) != sizeof(newtrackinfo)) //Write the new track info!
	{
		emufclose64(f); //Close the file!
		goto errorOutFormat_restore; //Error out and restore!
	}
	//Sector data bytes is in packet format: track,head,number,size!

	//First, sector number map!
	sectordataptr = &sectordata[2]; //Sector number used during formatting!
	for (currentsector = 0; currentsector < numsectors; ++currentsector) //Process all sector numbers!
	{
		b = *sectordataptr; //The sector number!
		if (emufwrite64(&b, 1, sizeof(b), f) != sizeof(b))
		{
			emufclose64(f); //Close the file!
			goto errorOutFormat_restore; //Error out and restore!
		}
		sectordataptr += 4; //Next record!
	}
	if (!numsectors) //No sectors specified?
	{
		b = 0; //The sector number!
		if (emufwrite64(&b, 1, sizeof(b), f) != sizeof(b))
		{
			emufclose64(f); //Close the file!
			goto errorOutFormat_restore; //Error out and restore!
		}
	}

	//Then, cylinder number map!
	sectordataptr = &sectordata[0]; //Cylinder number used during formatting!
	for (currentsector = 0; currentsector < numsectors; ++currentsector) //Process all cylinder numbers!
	{
		b = *sectordataptr; //The cylinder number!
		if (emufwrite64(&b, 1, sizeof(b), f) != sizeof(b))
		{
			emufclose64(f); //Close the file!
			goto errorOutFormat_restore; //Error out and restore!
		}
		sectordataptr += 4; //Next record!
	}
	if (!numsectors) //No sectors specified?
	{
		b = newtrackinfo.cylinder; //The cylinder number!
		if (emufwrite64(&b, 1, sizeof(b), f) != sizeof(b))
		{
			emufclose64(f); //Close the file!
			goto errorOutFormat_restore; //Error out and restore!
		}
	}
	//Then, head number map!
	sectordataptr = &sectordata[1]; //Head number used during formatting!
	for (currentsector = 0; currentsector < numsectors; ++currentsector) //Process all head numbers!
	{
		b = *sectordataptr; //The head number!
		if (emufwrite64(&b, 1, sizeof(b), f) != sizeof(b))
		{
			emufclose64(f); //Close the file!
			goto errorOutFormat_restore; //Error out and restore!
		}
		sectordataptr += 4; //Next record!
	}
	if (!numsectors) //No sectors specified?
	{
		b = (newtrackinfo.head_extrabits&IMD_HEAD_HEADNUMBER); //The head number!
		if (emufwrite64(&b, 1, sizeof(b), f) != sizeof(b))
		{
			emufclose64(f); //Close the file!
			goto errorOutFormat_restore; //Error out and restore!
		}
	}

	//Then, sector size map, if need to be used!
	if (newtrackinfo.SectorSize == 0xFF) //Use sector size map, which is a proposed extension to the specification?
	{
		sectordataptr = &sectordata[3]; //Size number used during formatting!
		for (currentsector = 0; currentsector < numsectors; ++currentsector) //Process all size numbers!
		{
			b = *sectordataptr; //The head number!
			if ((sectorsizeformat==0xFF) || (b==0xFF)) //Differently specified in the format command?
			{
				if ((sectorsizeformat == 0xFF) && (b==0xFF)) //Both 0xFF specified?
				{
					w = 0xFF; //Use the direct sector size specified!
				}
				else if (sectorsizeformat == 0xFF) //Use b?
				{
					w = b; //Use b directly!
				}
				else if (b==0xFF) //Use format command?
				{
					w = sectorsizeformat; //Use sector size format!
				}
				else //Default behaviour, use b!
				{
					w = (0x80 << b); //Use compatiblity!
				}
			}
			else //Normal behaviour, use b?
			{
				w = (0x80 << b); //128*2^x is the cylinder size!
			}
			if (emufwrite64(&w, 1, sizeof(w), f) != sizeof(w))
			{
				emufclose64(f); //Close the file!
				goto errorOutFormat_restore; //Error out and restore!
			}
			sectordataptr += 4; //Next record!
		}
		if (!numsectors) //No sectors specified?
		{
			b = 0; //The sector size!
			w = (0x80 << b); //128*2^x is the cylinder size!
			if (emufwrite64(&w, 1, sizeof(w), f) != sizeof(w))
			{
				emufclose64(f); //Close the file!
				goto errorOutFormat_restore; //Error out and restore!
			}
		}
	}

	//Then, the sector data(is compressed for easy formatting)!

	for (currentsector = 0; currentsector < numsectors; ++currentsector) //Process all size numbers!
	{
		b = 0x02; //What kind of sector to write: compressed to 1 byte!
		if (emufwrite64(&b, 1, sizeof(b), f) != sizeof(b))
		{
			emufclose64(f); //Close the file!
			goto errorOutFormat_restore; //Error out and restore!
		}
		b = filldata; //What kind of byte to fill!
		if (emufwrite64(&b, 1, sizeof(b), f) != sizeof(b))
		{
			emufclose64(f); //Close the file!
			goto errorOutFormat_restore; //Error out and restore!
		}
	}
	if (!numsectors) //No sectors specified?
	{
		b = 0x00; //What kind of sector to write: not a readable sector!
		if (emufwrite64(&b, 1, sizeof(b), f) != sizeof(b))
		{
			emufclose64(f); //Close the file!
			goto errorOutFormat_restore; //Error out and restore!
		}
	}

	//Finally, the tail!
	if (COMMON_MEMORYCONSERVATION_ALLOCATED(tailbuffer))
	{
		#ifdef MEMORYCONSERVATION
		COMMON_MEMORYCONSERVATION_WRITEBUFFER(f,tailbuffer,tailbuffersize,tailbufferfailedFormat,tailbufferwrittenFormat)
		tailbufferfailedFormat:
		#else
		if (emufwrite64(tailbuffer, 1, tailbuffersize, f) != tailbuffersize) //Write the tail buffer!
		#endif
		{
			emufclose64(f); //Close the file!
			goto errorOutFormat_restore; //Error out and restore!
		}
	}
	#ifdef MEMORYCONSERVATION
	tailbufferwrittenFormat:
	#endif
	//Finish up!
	if (sectorsizemap) //Allocated sector size map?
	{
		freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
	}
	if (COMMON_MEMORYCONSERVATION_ALLOCATED(headbuffer)) //Head buffer allocated?
	{
		#ifdef MEMORYCONSERVATION
		COMMON_MEMORYCONSERVATION_ENDBUFFER(headbuffer)
		#else
		freez((void**)&headbuffer, headbuffersize, "IMDIMAGE_HEADBUFFER"); //Free the allocated head!
		#endif
	}
	if (COMMON_MEMORYCONSERVATION_ALLOCATED(tailbuffer)) //Tail buffer allocated?
	{
		#ifdef MEMORYCONSERVATION
		COMMON_MEMORYCONSERVATION_ENDBUFFER(tailbuffer)
		#else
		freez((void**)&tailbuffer, tailbuffersize, "IMDIMAGE_TAILBUFFER"); //Free the allocated tail!
		#endif
	}
	if (sectornumbermap) //Map allocated?
	{
		freez((void**)&sectornumbermap, trackinfo.sectorspertrack, "IMDIMAGE_SECTORNUMBERMAP"); //Free the allocated sector number map!
	}
	if (cylindermap) //Map allocated?
	{
		freez((void**)&cylindermap, trackinfo.sectorspertrack, "IMDIMAGE_CYLINDERMAP"); //Free the allocated cylinder map!
	}
	if (headmap) //Map allocated?
	{
		freez((void**)&headmap, trackinfo.sectorspertrack, "IMDIMAGE_HEADMAP"); //Free the allocated head map!
	}
	if (COMMON_MEMORYCONSERVATION_ALLOCATED(oldsectordata)) //Sector data allocated?
	{
		#ifdef MEMORYCONSERVATION
		COMMON_MEMORYCONSERVATION_ENDBUFFER(oldsectordata)
		#else
		freez((void**)&oldsectordata, oldsectordatasize, "IMDIMAGE_OLDSECTORDATA"); //Free the allocated sector data map!
		#endif
	}
	emufclose64(f); //Close the image!
	return 1; //Success!	

errorOutFormat_restore: //Error out on formatting and restore the file!
	f = emufopen64(filename, "wb+"); //Open the image and clear it!
	if (!f) goto errorOutFormat; //Not opened!
	//First, the previous tracks!
	if (COMMON_MEMORYCONSERVATION_ALLOCATED(headbuffer) && headbuffersize) //Gotten a head buffer?
	{
		#ifdef MEMORYCONSERVATION
		COMMON_MEMORYCONSERVATION_WRITEBUFFER(f,headbuffer,headbuffersize,errorOutFormat,wroteHeadBufferRestoreFormat)
		#else
		if (emufwrite64(headbuffer, 1, headbuffersize, f) != headbuffersize) goto errorOutFormat; //Write the previous tracks back!
		#endif
	}
	#ifdef MEMORYCONSERVATION
	wroteHeadBufferRestoreFormat:
	#endif
	//Now, reached the reformatted track!
	if (emufwrite64(&trackinfo, 1, sizeof(trackinfo), f) != sizeof(trackinfo)) goto errorOutFormat; //Write the track header!
	if (sectornumbermap)
	{
		if (emufwrite64(sectornumbermap, 1, trackinfo.sectorspertrack, f) != trackinfo.sectorspertrack) goto errorOutFormat; //Write the sector number map!
	}
	if (cylindermap) //Cylinder map was present?
	{
		if (emufwrite64(cylindermap, 1, trackinfo.sectorspertrack, f) != trackinfo.sectorspertrack) goto errorOutFormat; //Write the cylinder number map!
	}
	if (headmap) //Head map was present?
	{
		if (emufwrite64(headmap, 1, trackinfo.sectorspertrack, f) != trackinfo.sectorspertrack) goto errorOutFormat; //Write the head map!
	}
	if (sectorsizemap) //Sector size map was present?
	{
		if (emufwrite64(sectorsizemap, 1, (trackinfo.sectorspertrack << 1), f) != (trackinfo.sectorspertrack << 1)) goto errorOutFormat; //Write the sector size map!
	}
	if (COMMON_MEMORYCONSERVATION_ALLOCATED(oldsectordata))
	{
		#ifdef MEMORYCONSERVATION
		COMMON_MEMORYCONSERVATION_WRITEBUFFER(f,oldsectordata,oldsectordatasize,errorOutFormat,wroteoldsectordataRestoreFormat)
		#else
		if (emufwrite64(oldsectordata, 1, oldsectordatasize, f) != oldsectordatasize) goto errorOutFormat; //Write the sector data!
		#endif
	}
	#ifdef MEMORYCONSERVATION
	wroteoldsectordataRestoreFormat:
	#endif
	if (COMMON_MEMORYCONSERVATION_ALLOCATED(tailbuffer))
	{
		#ifdef MEMORYCONSERVATION
		COMMON_MEMORYCONSERVATION_WRITEBUFFER(f, tailbuffer, tailbuffersize, errorOutFormat, wrotetailbufferRestoreFormat)
		#else
		if (emufwrite64(tailbuffer, 1, tailbuffersize, f) != tailbuffersize) goto errorOutFormat; //Write the next tracks back!
		#endif
	}
	#ifdef MEMORYCONSERVATION
	wrotetailbufferRestoreFormat:
	#endif
	//Now, the entire file has been restored to it's old state! Finish up the normal way below!

	//Couldn't find the track!
	errorOutFormat: //Erroring out on the formatting process!
	if (sectorsizemap) //Allocated sector size map?
	{
		freez((void**)&sectorsizemap, (trackinfo.sectorspertrack << 1), "IMDIMAGE_SECTORSIZEMAP"); //Free the allocated sector size map!
	}
	if (COMMON_MEMORYCONSERVATION_ALLOCATED(headbuffer)) //Head buffer allocated?
	{
		#ifdef MEMORYCONSERVATION
		COMMON_MEMORYCONSERVATION_ENDBUFFER(headbuffer)
		#else
		freez((void**)&headbuffer, headbuffersize, "IMDIMAGE_HEADBUFFER"); //Free the allocated head!
		#endif
	}
	if (COMMON_MEMORYCONSERVATION_ALLOCATED(tailbuffer)) //Tail buffer allocated?
	{
		#ifdef MEMORYCONSERVATION
		COMMON_MEMORYCONSERVATION_ENDBUFFER(tailbuffer)
		#else
		freez((void**)&tailbuffer, tailbuffersize, "IMDIMAGE_TAILBUFFER"); //Free the allocated tail!
		#endif
	}
	if (sectornumbermap) //Map allocated?
	{
		freez((void**)&sectornumbermap, trackinfo.sectorspertrack, "IMDIMAGE_SECTORNUMBERMAP"); //Free the allocated sector number map!
	}
	if (cylindermap) //Map allocated?
	{
		freez((void**)&cylindermap, trackinfo.sectorspertrack, "IMDIMAGE_CYLINDERMAP"); //Free the allocated cylinder map!
	}
	if (headmap) //Map allocated?
	{
		freez((void**)&headmap, trackinfo.sectorspertrack, "IMDIMAGE_HEADMAP"); //Free the allocated head map!
	}
	if (COMMON_MEMORYCONSERVATION_ALLOCATED(oldsectordata)) //Sector data allocated?
	{
		#ifdef MEMORYCONSERVATION
		COMMON_MEMORYCONSERVATION_ENDBUFFER(oldsectordata)
		#else
		freez((void**)&oldsectordata, oldsectordatasize, "IMDIMAGE_OLDSECTORDATA"); //Free the allocated sector data map!
		#endif
	}
	emufclose64(f); //Close the image!
	return 0; //Invalid IMD file!
}

extern char diskpath[256]; //Disk path!

byte generateIMDImage(char* filename, byte tracks, byte heads, byte MFM, byte speed, int percentagex, int percentagey)
{
	byte track;
	byte head;
	byte b;
	word w;
	TRACKINFORMATIONBLOCK newtrackinfo; //Original and new track info!
	byte identifier[256];
	char* identifierp;
	BIGFILE* f;
	word headersize;
	DOUBLE percentage;
	UniversalTimeOfDay tp;
	accuratetime currenttime;
	char fullfilename[256];
	memset(&fullfilename[0], 0, sizeof(fullfilename)); //Init!
	safestrcpy(fullfilename, sizeof(fullfilename), diskpath); //Disk path!
	safestrcat(fullfilename, sizeof(fullfilename), "/");
	safestrcat(fullfilename, sizeof(fullfilename), filename); //The full filename!
	domkdir(diskpath); //Make sure our directory we're creating an image in exists!

	if (strcmp(fullfilename, "") == 0) return 0; //Unexisting: don't even look at it!
	if (!isext(fullfilename, "imd")) //Not our IMD image file?
	{
		return 0; //Not a IMD image!
	}
	f = emufopen64(fullfilename, "wb"); //Open the image!
	if (!f) return 0; //Not opened!

	if ((percentagex != -1) && (percentagey != -1)) //To show percentage?
	{
		EMU_locktext();
		GPU_EMU_printscreen(percentagex, percentagey, "%2.1f%%", 0.0f); //Show first percentage!
		EMU_unlocktext();
	}

	//First, create the file header!

	memset(&identifier, 0, sizeof(identifier)); //Init identifier!
	identifierp = (char*)&identifier;
	safescatnprintf(identifierp, sizeof(identifier), "IMD 1.18: %02i/%02i/%04i %02i:%02i:%02i\r\n",0,0,0,0,0,0); //Default if we can't get the current date/time!
	//First, the identifier!
	if (getUniversalTimeOfDay(&tp) == 0) //Time gotten?
	{
		if (epochtoaccuratetime(&tp, &currenttime)) //Converted?
		{
			safestrcpy(identifierp, sizeof(identifier), ""); //Clear it!
			safescatnprintf(identifierp, sizeof(identifier), "IMD 1.18: %02i/%02i/%04i %02i:%02i:%02i\r\n", currenttime.month, currenttime.day, currenttime.year, currenttime.hour, currenttime.minute, currenttime.second); //Set the header accordingly!
		}
	}
	headersize = safe_strlen(identifierp, sizeof(identifier)); //Length!
	identifier[headersize] = 0x1A; //End of string marker!
	++headersize; //The size includes the finish byte!

	//Write the identifier to the file!
	if (emufwrite64(&identifier, 1, headersize, f) != headersize) //Write the new header!
	{
		emufclose64(f); //Close the file!
		delete_file(NULL, fullfilename); //Remove it, it's invalid!
		return 0; //Error out!
	}

	//Create all unformatted tracks!
	for (track = 0; track < tracks; ++track)
	{
		for (head = 0; head < heads; ++head)
		{
			//Now the new track to create!
//First, create a new track header!
			newtrackinfo.cylinder = track; //Same cylinder!
			newtrackinfo.head_extrabits = (head & IMD_HEAD_HEADNUMBER) | IMD_HEAD_HEADMAPPRESENT | IMD_HEAD_CYLINDERMAPPRESENT; //Head of the track, with head map and cylinder map present!
			newtrackinfo.mode = MFM ? ((speed < 3) ? (3 + speed) : 0) : ((speed < 3) ? (speed) : 0); //Mode: MFM or FM in 500,300,250.
			newtrackinfo.sectorspertrack = 1; //Amount of sectors on this track! Minimum value is 1, according to the specification!

			//Then, size map determination, if it's to be used or not!
			newtrackinfo.SectorSize = 0; //No sector size default without sectors!

			//Write the track header!
			if (emufwrite64(&newtrackinfo, 1, sizeof(newtrackinfo), f) != sizeof(newtrackinfo)) //Write the new track info!
			{
				emufclose64(f); //Close the file!
				delete_file(NULL, fullfilename); //Remove it, it's invalid!
				return 0; //Error out!
			}
			//Sector data bytes is in packet format: track,head,number,size!

			//First, sector number map!
			b = 0; //The sector number!
			if (emufwrite64(&b, 1, sizeof(b), f) != sizeof(b))
			{
				emufclose64(f); //Close the file!
				delete_file(NULL, fullfilename); //Remove it, it's invalid!
				return 0; //Error out!
			}

			//Then, cylinder number map!
			b = newtrackinfo.cylinder; //The cylinder number!
			if (emufwrite64(&b, 1, sizeof(b), f) != sizeof(b))
			{
				emufclose64(f); //Close the file!
				delete_file(NULL, fullfilename); //Remove it, it's invalid!
				return 0; //Error out!
			}
			//Then, head number map!
			b = (newtrackinfo.head_extrabits & IMD_HEAD_HEADNUMBER); //The head number!
			if (emufwrite64(&b, 1, sizeof(b), f) != sizeof(b))
			{
				emufclose64(f); //Close the file!
				delete_file(NULL, fullfilename); //Remove it, it's invalid!
				return 0; //Error out!
			}

			//Then, sector size map, if need to be used!
			if (newtrackinfo.SectorSize == 0xFF) //Sector size map used?
			{
				b = 0; //The sector size!
				w = (0x80 << b); //128*2^x is the cylinder size!
				if (emufwrite64(&w, 1, sizeof(w), f) != sizeof(w))
				{
					emufclose64(f); //Close the file!
					delete_file(NULL, fullfilename); //Remove it, it's invalid!
					return 0; //Error out!
				}
			}

			//Then, the sector data(is compressed for easy formatting)!

			b = 0x00; //What kind of sector to write: not a readable sector!
			if (emufwrite64(&b, 1, sizeof(b), f) != sizeof(b))
			{
				emufclose64(f); //Close the file!
				delete_file(NULL, fullfilename); //Remove it, it's invalid!
				return 0; //Error out!
			}

			if ((percentagex != -1) && (percentagey != -1)) //To show percentage?
			{
				percentage = (DOUBLE)((track*heads)+head);
				percentage /= (DOUBLE)(tracks*heads);
				percentage *= 100.0f;
				EMU_locktext();
				GPU_EMU_printscreen(percentagex, percentagey, "%2.1f%%", (float)percentage); //Show percentage!
				EMU_unlocktext();
#ifdef IS_PSP
				delay(0); //Allow update of the screen, if needed!
#endif
			}
		}
	}

	emufclose64(f); //Close the image!

	if ((percentagex != -1) && (percentagey != -1)) //To show percentage?
	{
		EMU_locktext();
		GPU_EMU_printscreen(percentagex, percentagey, "%2.1f%%", 100.0f); //Show percentage!
		EMU_unlocktext();
	}

	return 1; //Created IMD file!
}