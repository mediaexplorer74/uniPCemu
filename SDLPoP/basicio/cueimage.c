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

#include "headers/basicio/io.h"
#include "headers/fopen64.h"
#include "headers/emu/directorylist.h"
#include "headers/basicio/cueimage.h" //Our own header!

byte is_cueimage(char *filename)
{
	if (!isext(filename, "cue")) return 0; //Not a cue sheet!
	BIGFILE *f;
	f = emufopen64(filename, "rb"); //Open file!
	if (!f)
	{
		return 0; //Invalid file: file not found!
	}

	emufclose64(f); //Close the file!
	return 1; //CUE image!
}

FILEPOS cueimage_getsize(char *filename)
{
	if (strcmp(filename, "") == 0) return 0; //Not mountable!
	if (!isext(filename, "cue")) return 0; //Not a cue sheet!
	BIGFILE *f;
	f = emufopen64(filename, "rb"); //Open!
	if (!f) //Not found?
	{
		return 0; //No size!
	}
	emufclose64(f); //Close the file!
	return 0; //Give the result! We can't be used by normal read accesses!
}

char cuesheet_line[256]; //A line of the cue sheet!
char cuesheet_line_lc[256]; //A line of the cue sheet in lower case!

byte cuesheet_readline(BIGFILE *f)
{
	byte startedparsingline = 0; //Default: not a valid line with non-whitespace found!
	char c;
	if (emufeof64(f)) return 0; //EOF?
	memset(&cuesheet_line, 0, sizeof(cuesheet_line)); //Init!
	memset(&cuesheet_line_lc, 0, sizeof(cuesheet_line_lc)); //Init!
	for (; !emufeof64(f);) //Not EOF?
	{
		if (emufread64(&c, 1, 1, f) != 1) return 0; //Read a character from the file, otherwise fail!
		switch (c) //What character?
		{
		case 0: //Invalid?
			break; //Ignore!
		case '\n':
		case '\r': //Possible newline?
			if (startedparsingline) //Started parsing the line?
			{
				return 1; //Finished!
			}
		case ' ': //Space character to ignore?
		case '\t': //Tab character to ignore?
			if (startedparsingline) //Started parsing the line?
			{
				safe_scatnprintf(cuesheet_line, sizeof(cuesheet_line), "%c", c); //Add to the line!
				c = (char)tolower((int)c);
				safe_scatnprintf(cuesheet_line_lc, sizeof(cuesheet_line_lc), "%c", c); //Add to the line!
			}
			break; //Counted in!
		default: //Unknown character? Parse in the line!
			startedparsingline = 1; //We've started parsing the line if we didn't yet!
			safe_scatnprintf(cuesheet_line, sizeof(cuesheet_line), "%c", c); //Add to the line!
			safe_scatnprintf(cuesheet_line_lc, sizeof(cuesheet_line_lc), "%c", (char)tolower((int)c)); //Add to the line!
			break; //Counted as a normal character!
		}
	}
	if (startedparsingline) //Started parsing the line?
	{
		return 1; //OK: Give the final line!
	}
	return 0; //Nothing found!
}

typedef struct
{
	char identifier[256]; //Identifier
	word sectorsize;
	byte mode;
} CDROM_TRACK_MODE;

typedef struct
{
	FILEPOS datafilepos; //The current data file position(cumulatively added for non-changing files, by adding the indexes)!
	char MCN[13]; //The MCN for the cue sheet(if any)!
	byte got_MCN;
	char filename[256]; //Filename status!
	char file_type[256]; //Filename type!
	byte got_file; //Gotten?
	byte track_number; //Track number!
	CDROM_TRACK_MODE *track_mode; //Track mode!
	byte got_track; //Gotten?
	char ISRC[12]; //ISRC
	byte got_ISRC; //Gotten ISRC?
	byte index; //Current index!
	byte M; //Current M!
	byte S; //Current S!
	byte F; //Current F!
	byte got_index; //Gotten an index? 0=none, 1=Current, 2=Previous and current
	byte pregap_pending; //Pregap is pending?
	uint_32 pregap_pending_duration; //Pending pregap duration in frames!
	byte postgap_pending; //Postgap is pending?
	uint_32 postgap_pending_duration; //Pending postgap duration in frames!
	uint_32 MSFPosition; //Current MSF position on the disc in LBA format using frames!
} CUESHEET_STATUS;

typedef struct
{
	byte is_present; //Are we a filled in!
	CUESHEET_STATUS status; //The status at the moment the index entry was detected!
	byte endM; //Ending M of the index!
	byte endS; //Ending S of the index!
	byte endF; //Ending F of the index!
	//Ending MSF-1 = last index we're able to use!
} CUESHEET_ENTRYINFO;

extern IODISK disks[0x100]; //All disks available, up go 256 (drive 0-255) disks!

char identifier_CATALOG[8] = "catalog";
char identifier_FILE[5] = "file";
char identifier_TRACK[6] = "track";
char identifier_ISRC[5] = "isrc";
char identifier_PREGAP[7] = "pregap";
char identifier_INDEX[6] = "index";
char identifier_POSTGAP[8] = "postgap";

CDROM_TRACK_MODE cdrom_track_modes[10] = {
	{"AUDIO",2352,MODE_AUDIO},	//Audio / Music(2352 - 588 samples)
	{"CDG",2448,MODE_KARAOKE}, //Karaoke CD+G (2448)
	{"MODE1/2048",2048,MODE_MODE1DATA}, //CD - ROM Mode 1 Data(cooked)
	{"MODE1/2352",2352,MODE_MODE1DATA},	//CD - ROM Mode 1 Data(raw)
	{"MODE2/2048",2048,MODE_MODEXA},	//CD - ROM XA Mode 2 Data(form 1) *
	{"MODE2/2324",2324,MODE_MODEXA},	//CD - ROM XA Mode 2 Data(form 2) *
	{"MODE2/2336",2336,MODE_MODEXA},	//CD - ROM XA Mode 2 Data(form mix)
	{"MODE2/2352",2352,MODE_MODEXA},	//CD - ROM XA Mode 2 Data(raw)
	{"CDI/2336",2336,MODE_MODECDI},	//CDI Mode 2 Data
	{"CDI/2352",2352,MODE_MODECDI}	//CDI Mode 2 Data
};

uint_32 CUE_MSF2LBA(byte M, byte S, byte F)
{
	return (((M * 60) + S) * 75) + F; //75 frames per second, 60 seconds in a minute!
}

void CUE_LBA2MSF(uint_32 LBA, byte *M, byte *S, byte *F)
{
	uint_32 rest;
	rest = LBA; //Load LBA!
	*M = rest / (60 * 75); //Minute!
	rest -= *M*(60 * 75); //Rest!
	*S = rest / 75; //Second!
	rest -= *S * 75;
	*F = rest % 75; //Frame, if any!
}

void cueimage_fillMSF(int device, byte *got_startMSF, CUESHEET_ENTRYINFO *cue_current, CUESHEET_ENTRYINFO *cue_next, byte tracknumber, byte index, byte *startM, byte *startS, byte *startF, byte *endM, byte *endS, byte *endF) //Current to check and next entries(if any)!
{
	if (((disks[device].selectedtrack == tracknumber) || (disks[device].selectedtrack == 0)) && //Current track number to lookup?
		((disks[device].selectedsubtrack == index) || (disks[device].selectedsubtrack == 0))) //Current subtrack number to lookup?
	{
		if (cue_current) //Specified?
		{
			if (*got_startMSF == 0)
			{
				CUE_LBA2MSF(cue_current->status.MSFPosition, startM, startS, startF); //Specify the start position!
				*got_startMSF = 1; //Got!
			}
		}
		if (cue_next) //Specified?
		{
			CUE_LBA2MSF(cue_next->status.MSFPosition - 1, endM, endS, endF); //Specify the start position!
		}
	}
}

extern char diskpath[256]; //Disk path!

//Result: -1: Out of range, 0: Failed to read, 1: Read successfully
int_64 cueimage_REAL_readsector(int device, byte *M, byte *S, byte *F, byte *startM, byte *startS, byte *startF, byte *endM, byte *endS, byte *endF, void *buffer, word size, byte specialfeatures) //Read a n-byte sector! Result=Type on success, 0 on error, -1 on not found!
{
	byte orig_M, orig_S, orig_F;
	int_64 result=-1; //The result! Default: out of range!
	FILEPOS fsize=0;
	CUESHEET_STATUS cue_status;
	CUESHEET_ENTRYINFO cue_current, cue_next; //Current to check and next entries(if any)!
	char *c;
	byte file_wasescaped;
	char *file_string;
	char *file_stringstart;
	char *file_stringend;
	byte track_number_low;
	byte track_number_high;
	byte index_number;
	byte index_M;
	byte index_S;
	byte index_F;
	char *track_mode;
	CDROM_TRACK_MODE *curtrackmode;
	uint_32 LBA,prev_LBA,gap_startAddr,gap_endAddr;
	byte got_startMSF = 0;
	char fullfilename[256];

	if ((device != CDROM0) && (device != CDROM1)) return 0; //Abort: invalid disk!
	if (!isext(disks[device].filename, "cue")) return 0; //Not a cue sheet!
	BIGFILE *f;
	f = emufopen64(disks[device].filename, "rb"); //Open the sheet!
	if (!f) //Not found?
	{
		return 0; //No size!
	}

	//Save the original values!
	orig_M = *M;
	orig_S = *S;
	orig_F = *F;

	memset(&cue_status,0,sizeof(cue_status)); //Init the status!
	memset(&cue_current, 0, sizeof(cue_current)); //Init the current entry to parse!
	memset(&cue_next, 0, sizeof(cue_next)); //Init the next entry to parse!

	for (; cuesheet_readline(f);) //Read a line?
	{
		if (memcmp(&cuesheet_line_lc[0], &identifier_INDEX, safe_strlen(identifier_INDEX, sizeof(identifier_INDEX))) == 0) //File command?
		{
			if (!cue_status.got_track) continue; //If no track is specified, abort this command!
			
			//First, read the index!
			switch (cuesheet_line[safe_strlen(identifier_INDEX, sizeof(identifier_INDEX)) + 1]) //Track number, high number!
			{
			case '0':
				track_number_high = 0;
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				track_number_high = (cuesheet_line[safe_strlen(identifier_INDEX, sizeof(identifier_INDEX)) + 1] - (byte)('1')) + 1; //Number!
				break;
			default:
				continue; //Ignore the command if incorrect!
				break;
			}
			track_mode = &cuesheet_line[safe_strlen(identifier_INDEX, sizeof(identifier_INDEX)) + 3]; //Default: track mode space difference!
			switch (cuesheet_line[safe_strlen(identifier_INDEX, sizeof(identifier_INDEX)) + 2]) //Track number, high number!
			{
			case '0':
				track_number_low = 0;
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				track_number_low = (cuesheet_line[safe_strlen(identifier_INDEX, sizeof(identifier_INDEX)) + 2] - (byte)('1')) + 1; //Number!
				break;
			case ' ': //Nothing? Single digit?
				track_number_low = track_number_high;
				track_number_high = 0;
				track_mode = &cuesheet_line[safe_strlen(identifier_INDEX, sizeof(identifier_INDEX)) + 2]; //We're the start of the mode processing instead!
				break;
			default:
				continue; //Ignore the command if incorrect!
				break;
			}
			if (*track_mode != ' ') continue; //Ignore the command if incorrect!
			++track_mode; //Start of the MSF tracking!

			index_number = (track_number_high * 10) + track_number_low; //Save the index number!
											
			//Now, handle the MSF formatted text!
			//First, read the index!
			switch (*track_mode) //Track number, high number!
			{
			case '0':
				track_number_high = 0;
				++track_mode; //Handle!
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				track_number_high = (*track_mode++ - (byte)('1')) + 1; //Number!
				break;
			default:
				continue; //Ignore the command if incorrect!
				break;
			}
			switch (*track_mode) //Track number, high number!
			{
			case '0':
				track_number_low = 0;
				++track_mode;
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				track_number_low = (*track_mode++ - (byte)('1')) + 1; //Number!
				break;
			case ':': //Nothing? Single digit?
				track_number_low = track_number_high;
				track_number_high = 0;
				break;
			default:
				continue; //Ignore the command if incorrect!
				break;
			}
			if (*track_mode != ':') continue; //Ignore the command if incorrect!
			++track_mode; //Start of the MSF tracking!

			index_M = (track_number_high * 10) + track_number_low; //M!

			//First, read the index!
			switch (*track_mode) //Track number, high number!
			{
			case '0':
				track_number_high = 0;
				++track_mode;
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				track_number_high = (*track_mode++ - (byte)('1')) + 1; //Number!
				break;
			default:
				continue; //Ignore the command if incorrect!
				break;
			}
			switch (*track_mode) //Track number, high number!
			{
			case '0':
				track_number_low = 0;
				++track_mode;
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				track_number_low = (*track_mode++ - (byte)('1')) + 1; //Number!
				break;
			case ':': //Nothing? Single digit?
				track_number_low = track_number_high;
				track_number_high = 0;
				break;
			default:
				continue; //Ignore the command if incorrect!
				break;
			}
			if (*track_mode != ':') continue; //Ignore the command if incorrect!
			++track_mode; //Start of the MSF tracking!

			index_S = (track_number_high * 10) + track_number_low; //S!

			//First, read the index!
			switch (*track_mode) //Track number, high number!
			{
			case '0':
				track_number_high = 0;
				++track_mode;
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				track_number_high = (*track_mode++ - (byte)('1')) + 1; //Number!
				break;
			case ':': //Nothing? Single digit?
				track_number_low = track_number_high;
				track_number_high = 0;
				break;
			default:
				continue; //Ignore the command if incorrect!
				break;
			}
			switch (*track_mode) //Track number, high number!
			{
			case '0':
				track_number_low = 0;
				++track_mode;
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				track_number_low = (*track_mode++ - (byte)('1')) + 1; //Number!
				break;
			case '\0': //Nothing? Single digit?
				track_number_low = track_number_high;
				track_number_high = 0;
				break;
			default:
				continue; //Ignore the command if incorrect!
				break;
			}
			if (*track_mode) continue; //Ignore the command if incorrect!
			++track_mode; //Start of the MSF tracking!

			index_F = (track_number_high * 10) + track_number_low; //F!

			if (index_F > 74) continue; //Incorrect Frame!

			//Now, the full entry is ready to be parsed!
			cue_status.index = index_number; //The index!
			cue_status.M = index_M; //The Minute!
			cue_status.S = index_S; //The Second!
			cue_status.F = index_F; //The Frame!
			cue_status.got_index = cue_status.got_index?2:1; //The index field is filled! Become 2 after the first one!

			memcpy(&cue_next.status,&cue_status,MIN(sizeof(cue_next.status),sizeof(cue_status))); //Fill the index entry of cue_next with the currently loaded cue status!
			cue_next.is_present = cue_status.got_index; //Present?
			if (cue_current.is_present && cue_next.is_present) //Got current to advance?
			{
				if ((specialfeatures & 4) == 0)
				{
					cueimage_fillMSF(device, &got_startMSF, &cue_current, &cue_next, cue_current.status.track_number, cue_current.status.index, startM, startS, startF, endM, endS, endF); //Fill info!
				}
				prev_LBA = CUE_MSF2LBA(cue_current.status.M, cue_current.status.S, cue_current.status.F); //Get the current LBA position we're advancing?
				LBA = CUE_MSF2LBA(cue_next.status.M, cue_next.status.S, cue_next.status.F); //Get the current LBA position we're advancing?
				cue_status.datafilepos += ((LBA - prev_LBA) * cue_next.status.track_mode->sectorsize); //The physical start of the data in the file with the specified mode!
				if (cue_next.status.postgap_pending) //Postgap was pending for us?
				{
					//Handle postgap using prev_LBA-LBA, postgap info and MSF position!
					gap_startAddr = (cue_next.status.MSFPosition) + (LBA - prev_LBA); //The start of the gap!
					gap_endAddr = gap_startAddr + (cue_next.status.postgap_pending_duration-1); //The end of the gap!
					cue_status.MSFPosition += (LBA - prev_LBA); //Add the previous track size of the final entry!
					//Handle the postgap for the previous track now!
					if (CUE_MSF2LBA(orig_M, orig_S, orig_F) < gap_startAddr) goto notthispostgap1; //Before start? Not us!
					if (CUE_MSF2LBA(orig_M, orig_S, orig_F) > gap_endAddr) goto notthispostgap1; //After end? not us!
					//We're this postgap!
					result = (-2LL - (int_64)((gap_endAddr-CUE_MSF2LBA(orig_M, orig_S, orig_F))+1)); //Give the result as the difference until the next track!
				notthispostgap1:
					cue_status.MSFPosition += cue_next.status.postgap_pending_duration; //Apply the gap to the physical position!
					cue_next.status.postgap_pending = 0; //Not pending anymore!
					cue_status.postgap_pending = 0; //Not pending anymore!
					cue_next.status.MSFPosition = cue_status.MSFPosition; //Update the current MSF position too!
					if (specialfeatures & 4) //Special reporting?
					{
						cueimage_fillMSF(device, &got_startMSF, &cue_current, &cue_next, cue_status.track_number, cue_status.index, startM, startS, startF, endM, endS, endF); //Special reporting!
						specialfeatures &= ~4; //Clear the flag for properly reporting the end of us!
					}
				}
				else
				{
					cue_status.MSFPosition += (LBA - prev_LBA); //Add the previous track size of the final entry!
				}

				if (cue_next.status.pregap_pending) //Pregap was pending for us?
				{
					//Handle postgap using MSF position and pregap info!
					gap_startAddr = cue_status.MSFPosition; //The pregap starts after the previous track!
					gap_endAddr = gap_startAddr + (cue_next.status.pregap_pending_duration-1); //The end of the gap!
					//Handle the pregap now!
					if (CUE_MSF2LBA(orig_M, orig_S, orig_F) < gap_startAddr) goto notthispregap; //Before start? Not us!
					if (CUE_MSF2LBA(orig_M, orig_S, orig_F) > gap_endAddr) goto notthispregap; //After end? not us!
					//We're this pregap!
					result = (-2LL - (int_64)((gap_endAddr-CUE_MSF2LBA(orig_M, orig_S, orig_F))+1)); //Give the result as the difference until the next track!
				notthispregap:
					cue_status.MSFPosition += cue_next.status.pregap_pending_duration;
					cue_next.status.pregap_pending = 0; //Not pending anymore!
					cue_status.pregap_pending = 0; //Not pending anymore!
					cue_next.status.MSFPosition = cue_status.MSFPosition; //Update the current MSF position too!
					if ((specialfeatures & 2) && ((specialfeatures & 4) == 0)) //Special reporting?
					{
						cueimage_fillMSF(device, &got_startMSF, &cue_current, &cue_next, cue_current.status.track_number, cue_current.status.index, startM, startS, startF, endM, endS, endF); //Special reporting!
					}
				}
			}
			else if (cue_next.is_present) //Only next? Initial entry of a file!
			{
				LBA = CUE_MSF2LBA(cue_next.status.M, cue_next.status.S, cue_next.status.F); //Get the current LBA position we're advancing?
				cue_status.datafilepos += (LBA * cue_next.status.track_mode->sectorsize); //The physical start of the data in the file with the specified mode!
			}
			//Update the data file position in the next entry!
			memcpy(&cue_next.status, &cue_status, MIN(sizeof(cue_next.status), sizeof(cue_status))); //Fill the index entry of cue_next with the currently loaded cue status!

			if (cue_current.is_present && cue_next.is_present) //Handle the cue_current if it and cue_next are both present!
			{
				if (!(((specialfeatures & 2) && ((specialfeatures & 4) == 0)) || (specialfeatures&4))) //No special handling of pregap/postgap?
				{
					cueimage_fillMSF(device, &got_startMSF, &cue_current, &cue_next, cue_current.status.track_number, cue_current.status.index, startM, startS, startF, endM, endS, endF); //Report the entire track with the properly updated MSF position!
				}
				//Fill the end locations into the current entry, based on the next entry!
				LBA = CUE_MSF2LBA(cue_next.status.M, cue_next.status.S, cue_next.status.F); //Convert to LBA!
				if (CUE_MSF2LBA(cue_current.status.M, cue_current.status.S, cue_current.status.F) >= LBA) goto finishMSFscan; //Invalid to read(non-zero length)?
				--LBA; //Take the end position of us!
				CUE_LBA2MSF(LBA, &cue_current.endM, &cue_current.endS, &cue_current.endF); //Save the calculated end position of the selected index!
				LBA = CUE_MSF2LBA(orig_M, orig_S, orig_F); //What LBA are we going to try to read!
				if (((disks[device].selectedtrack == cue_current.status.track_number) || (disks[device].selectedtrack==0)) &&
					((disks[device].selectedsubtrack == cue_current.status.index) || (disks[device].selectedsubtrack==0))) //Current track number and subtrack number to lookup?
				{
					if (cue_current.status.got_file && cue_current.status.got_index) //Got file and index to lookup? Otherwise, not found!
					{
						LBA = CUE_MSF2LBA(orig_M, orig_S, orig_F); //What LBA are we going to try to read!

						if (cue_current.status.MSFPosition > LBA) goto finishMSFscan; //Invalid? Current LBA isn't in our range(we're requesting before it)?
						if ((cue_current.status.MSFPosition + (CUE_MSF2LBA(cue_current.endM, cue_current.endS, cue_current.endF) - CUE_MSF2LBA(cue_current.status.M, cue_current.status.S, cue_current.status.F))) < LBA) goto finishMSFscan; //Invalid? Current LBA isn't in our range(we're requesting after it)!
						goto foundMSF; //We've found the location of our data!
					}
				}
			}
			finishMSFscan:
			memcpy(&cue_current, &cue_next, sizeof(cue_current)); //Set cue_current to cue_next! The next becomes the new current!
			cue_next.is_present = 0; //Set cue_next to not present!
		}
		else if (memcmp(&cuesheet_line_lc[0], &identifier_PREGAP, safe_strlen(identifier_PREGAP, sizeof(identifier_PREGAP))) == 0) //PREGAP command?
		{
			//Handle as an special entry for MSF index only! Don't count towards the file size! Only if a track is specified!
			if (!cue_status.got_track) continue; //If no track is specified, abort this command!
			track_mode = &cuesheet_line[safe_strlen(identifier_PREGAP, sizeof(identifier_PREGAP))];
			if (*track_mode != ' ') continue; //Ignore the command if incorrect!
			++track_mode; //Start of the MSF tracking!

			//Now, handle the MSF formatted text!
			//First, read the index!
			switch (*track_mode) //Track number, high number!
			{
			case '0':
				track_number_high = 0;
				++track_mode; //Handle!
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				track_number_high = (*track_mode++ - (byte)('1')) + 1; //Number!
				break;
			default:
				continue; //Ignore the command if incorrect!
				break;
			}
			switch (*track_mode) //Track number, high number!
			{
			case '0':
				track_number_low = 0;
				++track_mode;
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				track_number_low = (*track_mode++ - (byte)('1')) + 1; //Number!
				break;
			case ':': //Nothing? Single digit?
				track_number_low = track_number_high;
				track_number_high = 0;
				break;
			default:
				continue; //Ignore the command if incorrect!
				break;
			}
			if (*track_mode != ':') continue; //Ignore the command if incorrect!
			++track_mode; //Start of the MSF tracking!

			index_M = (track_number_high * 10) + track_number_low; //M!

			//First, read the index!
			switch (*track_mode) //Track number, high number!
			{
			case '0':
				track_number_high = 0;
				++track_mode;
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				track_number_high = (*track_mode++ - (byte)('1')) + 1; //Number!
				break;
			default:
				continue; //Ignore the command if incorrect!
				break;
			}
			switch (*track_mode) //Track number, high number!
			{
			case '0':
				track_number_low = 0;
				++track_mode;
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				track_number_low = (*track_mode++ - (byte)('1')) + 1; //Number!
				break;
			case ':': //Nothing? Single digit?
				track_number_low = track_number_high;
				track_number_high = 0;
				break;
			default:
				continue; //Ignore the command if incorrect!
				break;
			}
			if (*track_mode != ':') continue; //Ignore the command if incorrect!
			++track_mode; //Start of the MSF tracking!

			index_S = (track_number_high * 10) + track_number_low; //S!

			//First, read the index!
			switch (*track_mode) //Track number, high number!
			{
			case '0':
				track_number_high = 0;
				++track_mode;
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				track_number_high = (*track_mode++ - (byte)('1')) + 1; //Number!
				break;
			case ':': //Nothing? Single digit?
				track_number_low = track_number_high;
				track_number_high = 0;
				break;
			default:
				continue; //Ignore the command if incorrect!
				break;
			}
			switch (*track_mode) //Track number, high number!
			{
			case '0':
				track_number_low = 0;
				++track_mode;
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				track_number_low = (*track_mode++ - (byte)('1')) + 1; //Number!
				break;
			case '\0': //Nothing? Single digit?
				track_number_low = track_number_high;
				track_number_high = 0;
				break;
			default:
				continue; //Ignore the command if incorrect!
				break;
			}
			if (*track_mode) continue; //Ignore the command if incorrect!
			++track_mode; //Start of the MSF tracking!

			index_F = (track_number_high * 10) + track_number_low; //F!

			if (index_F > 74) continue; //Incorrect Frame!
			LBA = CUE_MSF2LBA(index_M, index_S, index_F); //Calculate the amount to be this type of special case!
			if (cue_status.postgap_pending || (cue_status.got_index)) //Index already specified or postgap still pending for the previous track?
			{
				cue_status.pregap_pending = 1; //Pregap is pending!
				cue_status.pregap_pending_duration = LBA; //Duration of the pregap!
			}
			else if (LBA) //Handle pregap immediately if valid!
			{
				//Handle postgap using MSF position and pregap info!
				gap_startAddr = cue_status.MSFPosition; //The pregap starts after the previous track!
				gap_endAddr = gap_startAddr + (LBA - 1); //The end of the gap!
				//Handle the pregap now!
				if (CUE_MSF2LBA(orig_M, orig_S, orig_F) < gap_startAddr) goto notthispregap2; //Before start? Not us!
				if (CUE_MSF2LBA(orig_M, orig_S, orig_F) > gap_endAddr) goto notthispregap2; //After end? not us!
				//We're this pregap!
				result = (-2LL - (int_64)((gap_endAddr-CUE_MSF2LBA(orig_M, orig_S, orig_F))+1)); //Give the result as the difference until the next track!
			notthispregap2:
				if ((specialfeatures & 2) && ((specialfeatures&4)==0)) //Special reporting?
				{
					cueimage_fillMSF(device, &got_startMSF,  &cue_current, NULL,cue_status.track_number, cue_status.index, startM, startS, startF, endM, endS, endF); //Special reporting!
				}
				cue_status.MSFPosition += LBA;
				cue_current.status.pregap_pending = 0; //Not pending anymore!
				cue_status.pregap_pending = 0; //Not pending anymore!
				cue_current.status.MSFPosition = cue_status.MSFPosition; //Update the current MSF position too!
			}
		}
		else if (memcmp(&cuesheet_line_lc[0], &identifier_POSTGAP, safe_strlen(identifier_POSTGAP, sizeof(identifier_POSTGAP))) == 0) //POSTGAP command?
		{
			//Handle as an special entry for MSF index only! Don't count towards the file size! Only if a track is specified!
			if (!cue_status.got_track) continue; //If no track is specified, abort this command!
			track_mode = &cuesheet_line[safe_strlen(identifier_POSTGAP, sizeof(identifier_POSTGAP))];
			if (*track_mode != ' ') continue; //Ignore the command if incorrect!
			++track_mode; //Start of the MSF tracking!
											
			//Now, handle the MSF formatted text!
			//First, read the index!
			switch (*track_mode) //Track number, high number!
			{
			case '0':
				track_number_high = 0;
				++track_mode; //Handle!
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				track_number_high = (*track_mode++ - (byte)('1')) + 1; //Number!
				break;
			default:
				continue; //Ignore the command if incorrect!
				break;
			}
			switch (*track_mode) //Track number, high number!
			{
			case '0':
				track_number_low = 0;
				++track_mode;
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				track_number_low = (*track_mode++ - (byte)('1')) + 1; //Number!
				break;
			case ':': //Nothing? Single digit?
				track_number_low = track_number_high;
				track_number_high = 0;
				break;
			default:
				continue; //Ignore the command if incorrect!
				break;
			}
			if (*track_mode != ':') continue; //Ignore the command if incorrect!
			++track_mode; //Start of the MSF tracking!

			index_M = (track_number_high * 10) + track_number_low; //M!

			//First, read the index!
			switch (*track_mode) //Track number, high number!
			{
			case '0':
				track_number_high = 0;
				++track_mode;
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				track_number_high = (*track_mode++ - (byte)('1')) + 1; //Number!
				break;
			default:
				continue; //Ignore the command if incorrect!
				break;
			}
			switch (*track_mode) //Track number, high number!
			{
			case '0':
				track_number_low = 0;
				++track_mode;
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				track_number_low = (*track_mode++ - (byte)('1')) + 1; //Number!
				break;
			case ':': //Nothing? Single digit?
				track_number_low = track_number_high;
				track_number_high = 0;
				break;
			default:
				continue; //Ignore the command if incorrect!
				break;
			}
			if (*track_mode != ':') continue; //Ignore the command if incorrect!
			++track_mode; //Start of the MSF tracking!

			index_S = (track_number_high * 10) + track_number_low; //S!

			//First, read the index!
			switch (*track_mode) //Track number, high number!
			{
			case '0':
				track_number_high = 0;
				++track_mode;
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				track_number_high = (*track_mode++ - (byte)('1')) + 1; //Number!
				break;
			case ':': //Nothing? Single digit?
				track_number_low = track_number_high;
				track_number_high = 0;
				break;
			default:
				continue; //Ignore the command if incorrect!
				break;
			}
			switch (*track_mode) //Track number, high number!
			{
			case '0':
				track_number_low = 0;
				++track_mode;
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				track_number_low = (*track_mode++ - (byte)('1')) + 1; //Number!
				break;
			case '\0': //Nothing? Single digit?
				track_number_low = track_number_high;
				track_number_high = 0;
				break;
			default:
				continue; //Ignore the command if incorrect!
				break;
			}
			if (*track_mode) continue; //Ignore the command if incorrect!
			++track_mode; //Start of the MSF tracking!

			index_F = (track_number_high * 10) + track_number_low; //F!

			if (index_F > 74) continue; //Incorrect Frame!
			LBA = CUE_MSF2LBA(index_M, index_S, index_F); //Calculate the amount to be this type of special case!
			cue_status.postgap_pending = 1; //Postgap is pending!
			cue_status.postgap_pending_duration = LBA; //Duration of the postgap!
		}
		else if (memcmp(&cuesheet_line_lc[0], &identifier_TRACK, safe_strlen(identifier_TRACK, sizeof(identifier_TRACK))) == 0) //Track command?
		{
			//Specify a new track and track mode to use if a file is specified!
			if (!cue_status.got_file) continue; //Ingore if no file is specified!
			if (cuesheet_line[safe_strlen(identifier_TRACK, sizeof(identifier_TRACK))] != ' ') continue; //Ignore if the command is incorrect!
			switch (cuesheet_line[safe_strlen(identifier_TRACK, sizeof(identifier_TRACK))+1]) //Track number, high number!
			{
			case '0':
				track_number_high = 0;
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				track_number_high = (cuesheet_line[safe_strlen(identifier_TRACK, sizeof(identifier_TRACK)) + 1] - (byte)('1')) + 1; //Number!
				break;
			default:
				continue; //Ignore the command if incorrect!
				break;
			}
			track_mode = &cuesheet_line[safe_strlen(identifier_TRACK, sizeof(identifier_TRACK)) + 3]; //Default: track mode space difference!
			switch (cuesheet_line[safe_strlen(identifier_TRACK, sizeof(identifier_TRACK)) + 2]) //Track number, high number!
			{
			case '0':
				track_number_low = 0;
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				track_number_low = (cuesheet_line[safe_strlen(identifier_TRACK, sizeof(identifier_TRACK)) + 2] - (byte)('1')) + 1; //Number!
				break;
			case ' ': //Nothing? Single digit?
				track_number_low = track_number_high;
				track_number_high = 0;
				track_mode = &cuesheet_line[safe_strlen(identifier_TRACK, sizeof(identifier_TRACK)) + 2]; //We're the start of the mode processing instead!
				break;
			default:
				continue; //Ignore the command if incorrect!
				break;
			}
			if (*track_mode != ' ') continue; //Ignore the command if incorrect!
			++track_mode; //Start of the mode tracking!
			for (curtrackmode = &cdrom_track_modes[0]; curtrackmode != &cdrom_track_modes[NUMITEMS(cdrom_track_modes)];++curtrackmode) //Check all CD-ROM track modes!
			{
				if (memcmp(&curtrackmode->identifier, track_mode, safe_strlen(curtrackmode->identifier, sizeof(curtrackmode->identifier)))==0) //Track mode found?
				{
					goto trackmodefound; //We're found!
				}
			}
			trackmodefound:
			if (curtrackmode == &cdrom_track_modes[NUMITEMS(cdrom_track_modes)]) continue; //Track mode not found?
			//Fill the information into the current track information!
			cue_status.track_mode = curtrackmode; //Set the current track mode!
			cue_status.track_number = (track_number_high*10)+track_number_low; //Set the current track number!
			cue_status.got_track = 1; //Track has been parsed!
			if (((cue_status.track_number == disks[device].selectedtrack) || (disks[device].selectedtrack == 0)) && (specialfeatures&1)) //Track has been found?
			{
				if (result > -2LL) //Not the special result?
				{
					result = 1 + cue_status.track_mode->mode; //Result becomes 1 instead of -1(track not found) because the track is found!
				}
			}
		}
		else if (memcmp(&cuesheet_line_lc[0], &identifier_FILE, safe_strlen(identifier_FILE, sizeof(identifier_FILE))) == 0) //File command?
		{
			if (cue_status.got_file && cue_status.got_track & cue_status.got_index) //We have a previous index until the end of the file?
			{
				//We're handling the EOF case because the running file is ending!
				if (cue_current.is_present && (!cue_next.is_present) && cue_current.status.got_file && cue_current.status.got_track && cue_current.status.got_index) //Handle the cue_current if it and cue_next isn't present!
				{
					if (safe_strlen(cue_current.status.file_type, sizeof(cue_current.status.file_type)) != strlen("binary")) goto finishup; //Invalid file type!
					if (!(strcmp(cue_current.status.file_type, "binary") == 0)) //Not supported file backend type!
					{
						goto finishup; //Finish up!
					}
					//Fill the end locations into the current entry, based on the next entry!
					BIGFILE *source;
					memset(&fullfilename, 0, sizeof(fullfilename)); //Init!
					safestrcpy(fullfilename, sizeof(fullfilename), diskpath); //Disk path!
					safestrcat(fullfilename, sizeof(fullfilename), "/");
					safestrcat(fullfilename, sizeof(fullfilename), cue_current.status.filename); //The full filename!
					source = emufopen64(fullfilename, "rb"); //Open the backend data file!
					if (!source) goto finishup; //Couldn't open the source!
					if (emufseek64(source, 0, SEEK_END) == 0) //Went to EOF?
					{
						fsize = emuftell64(source); //What is the size of the file!

						memcpy(&cue_next, &cue_current, sizeof(cue_current)); //Copy the current as the next!
						++cue_next.status.index; //Take the index one up!
						LBA = CUE_MSF2LBA(cue_current.status.M, cue_current.status.S, cue_current.status.F); //Get the LBA of the last block the entry as the start of the final block!
						fsize -= cue_current.status.datafilepos; //Get the address of the last block the entry starts as!
						LBA += (uint_32)(fsize / cue_current.status.track_mode->sectorsize); //What LBA are we going to try to read at most!
						CUE_LBA2MSF(LBA, &cue_next.status.M, &cue_next.status.S, &cue_next.status.F); //Convert the LBA back to MSF for the fake next record based on the file size(for purposes on the final index entry going until EOF of the source file)!
					}
					else //Couldn't goto EOF?
					{
						return 0; //Couldn't go EOF, so unknown size!
					}
					emufclose64(source); //Close the source!

					//Autodetect final MSF address and give it as a result!
					--LBA; //Take the end position of us!
					CUE_LBA2MSF(LBA, &cue_current.endM, &cue_current.endS, &cue_current.endF); //Save the calculated end position of the selected index!
					prev_LBA = CUE_MSF2LBA(cue_current.status.M, cue_current.status.S, cue_current.status.F); //Previous LBA for more calculations!
					if (cue_next.status.postgap_pending) //Postgap was pending for us?
					{
						//Handle postgap using prev_LBA-LBA, postgap info and MSF position!
						gap_startAddr = (cue_next.status.MSFPosition) + (LBA - prev_LBA) + 1; //The start of the gap!
						gap_endAddr = gap_startAddr + (cue_next.status.postgap_pending_duration - 1); //The end of the gap!
						cue_next.status.MSFPosition += (LBA - prev_LBA) + 1; //Add the previous track size of the final entry!
						//Handle the postgap for the previous track now!
						if (CUE_MSF2LBA(orig_M, orig_S, orig_F) < gap_startAddr) goto notthispostgap3; //Before start? Not us!
						if (CUE_MSF2LBA(orig_M, orig_S, orig_F) > gap_endAddr) goto notthispostgap3; //After end? not us!
						//We're this postgap!
						result = (-2LL - (int_64)((gap_endAddr - CUE_MSF2LBA(orig_M, orig_S, orig_F)) + 1)); //Give the result as the difference until the next track!
					notthispostgap3:
						cue_next.status.MSFPosition += cue_next.status.postgap_pending_duration; //Add the postgap to the size!
						cue_next.status.postgap_pending = 0; //Not pending anymore!
						cue_status.postgap_pending = 0; //Not pending anymore!
					}
					else //Calculate the next record for us!
					{
						cue_next.status.MSFPosition += (LBA - prev_LBA) + 1; //Add the previous track size of the final entry!
					}

					//Pregap can't be pending at EOF, since there's alway an index after it! Cue files always end with an index and maybe a postgap after it! Otherwise, ignore it!

					if ((specialfeatures & 4) == 0)
					{
						cueimage_fillMSF(device, &got_startMSF, &cue_current, &cue_next, cue_current.status.track_number, cue_current.status.index, startM, startS, startF, endM, endS, endF); //Fill info!
					}
					//Duplicate MSF into the result!
					*M = *endM;
					*S = *endS;
					*F = *endF;

					LBA = CUE_MSF2LBA(orig_M, orig_S, orig_F); //What LBA are we going to try to read!
					if (cue_current.status.MSFPosition >= (LBA + 1)) goto finishMSFscan2; //Invalid to read(non-zero length)?
					if (((disks[device].selectedtrack == cue_current.status.track_number) || (disks[device].selectedtrack == 0)) &&
						((disks[device].selectedsubtrack == cue_current.status.index) || (disks[device].selectedsubtrack == 0))) //Current track number and subtrack number to lookup?
					{
						if (cue_current.status.got_file && cue_current.status.got_index) //Got file and index to lookup? Otherwise, not found!
						{
							if (cue_current.status.MSFPosition > LBA) goto finishMSFscan2; //Invalid? Current LBA isn't in our range(we're requesting before it)?
							if ((cue_current.status.MSFPosition + (CUE_MSF2LBA(cue_current.endM, cue_current.endS, cue_current.endF) - CUE_MSF2LBA(cue_current.status.M, cue_current.status.S, cue_current.status.F))) < LBA) goto finishMSFscan2; //Invalid? Current LBA isn't in our range(we're requesting after it)!
							if (!cue_current.status.index) goto finishMSFscan2; //Not a valid index(index 0 is a pregap)!
							goto foundMSF; //Do the normal MSF being found!
						}
					}
				}
			finishMSFscan2:
				memcpy(&cue_current, &cue_next, sizeof(cue_current)); //Set cue_current to cue_next! The next becomes the new current!
				cue_next.is_present = 0; //Set cue_next to not present!
			}
			//Specify a new file and mode to use! Also, reset the virtual position in the file!
			if (cuesheet_line[safe_strlen(identifier_FILE, sizeof(identifier_FILE))] != ' ') continue; //Ignore if the command is incorrect!
			c = &cuesheet_line[safe_strlen(cuesheet_line, sizeof(cuesheet_line)) - 1]; //Start at the end of the line!
			file_string = &cuesheet_line[safe_strlen(identifier_FILE, sizeof(identifier_FILE)) + 1]; //Start of the file string!
			for (;(c>=file_string);--c) //Parse backwards from End of String(EOS)!
			{
				if (*c == ' ') //Space found? Maybe found the final identifier?
				{
					if (c == &cuesheet_line[safe_strlen(identifier_FILE, sizeof(identifier_FILE))]) //First space found instead? Incorrect parameters!
					{
						goto finalspacefound_FILE; //Ignore the invalid command!
					}
					else goto finalspacefound_FILE; //Handle!
				}
			}
		finalspacefound_FILE: //Final space has been found?
			if (c <= &cuesheet_line[0]) //Parse error?
			{
				continue; //Ignore the invalid command!
			}
			if ((c == &cuesheet_line[safe_strlen(identifier_FILE, sizeof(identifier_FILE))]) && (*c==' ')) //First space found instead? Incorrect parameters!
			{
				continue; //Ignore the invalid command!
			}
			if (c < &cuesheet_line[0]) c = &cuesheet_line[0]; //Safety!
			file_wasescaped = 0; //Default: not escaped!
			memset(&cue_status.filename, 0, sizeof(cue_status.filename)); //Init!
			memset(&cue_status.file_type, 0, sizeof(cue_status.file_type)); //Init!
			file_stringstart = file_string; //Safe a backup copy of detecting the start of the process!
			file_stringend = c - 1; //End of the file string for detecting!
			for (; (file_string != c); ++file_string) //Process the entire file string!
			{
				if (*file_string == '"') //Possible escape?
				{
					if ((file_string == file_stringstart) || (file_string == file_stringend)) //Start or end?
					{
						if (file_string == file_stringstart)
						{
							file_wasescaped = 1; //We were escaped!
						}
						if ((!file_wasescaped) || ((file_string != file_stringend) && (file_string != file_stringstart))) //Not escaped or the end?
						{
							safe_scatnprintf(cue_status.filename, sizeof(cue_status.filename), "%c", (char)(*file_string)); //Add to the result!
						}
					}
				}
				else //Not an escape?
				{
					safe_scatnprintf(cue_status.filename, sizeof(cue_status.filename), "%c", *file_string); //Add to the result!
				}
			}

			++c; //Skip the final space character!
			safe_scatnprintf(cue_status.file_type, sizeof(cue_status.file_type), "%s", c); //Set the file type to use!
			for (c = &cue_status.file_type[0]; c != &cue_status.file_type[safe_strlen(cue_status.file_type, sizeof(cue_status).file_type)]; ++c) //Convert to lower case!
			{
				if (*c) //Valid?
				{
					*c = (char)tolower((int)*c); //Convert to lower case!
				}
			}
			cue_status.got_file = 1; //File has been parsed!
			if (cue_status.got_index) //Index was loaded? Remove the memory of the next and current indexes, as a new file has been specified!
			{
				cue_status.got_index = 3; //Default: no index anymore! We're a continuing index!
				cue_next.is_present = 0; //No next!
				cue_current.is_present = 0; //No current!
			}
			cue_status.got_track = 0; //No track has been specified for this file!
			cue_status.datafilepos = 0; //Initialize the data file position to 0!
		}
		else if (memcmp(&cuesheet_line_lc[0], &identifier_ISRC, safe_strlen(identifier_ISRC, sizeof(identifier_ISRC))) == 0) //ISRC command?
		{
			//Set the IRSC to the value, if a track is specified!
			if (!((cue_status.got_ISRC) || (cue_status.got_track == 0))) //Not gotten yet or not on a track?
			{
				if (safe_strlen(cuesheet_line, sizeof(cuesheet_line)) == (safe_strlen(identifier_ISRC, sizeof(identifier_ISRC)) + sizeof(cue_status.ISRC) + 1)) //Valid length?
				{
					memcpy(&cue_status.ISRC, &cuesheet_line[safe_strlen(identifier_ISRC, sizeof(identifier_ISRC)) + 1], sizeof(cue_status.ISRC)); //Set the value of the MCN!
					cue_status.got_ISRC = 1; //Gotten a MCN now!
				}
			}
		}
		else if (memcmp(&cuesheet_line_lc[0], &identifier_CATALOG, safe_strlen(identifier_CATALOG, sizeof(identifier_CATALOG))) == 0) //Catalog command?
		{
			//Store the MCN if nothing else if set yet!
			if (cue_status.got_MCN == 0) //Valid to use?
			{
				if (safe_strlen(cuesheet_line, sizeof(cuesheet_line)) == (safe_strlen(identifier_CATALOG, sizeof(identifier_CATALOG)) + sizeof(cue_status.MCN) + 1)) //Valid length?
				{
					memcpy(&cue_status.MCN, &cuesheet_line[safe_strlen(identifier_CATALOG, sizeof(identifier_CATALOG)) + 1], sizeof(cue_status.MCN)); //Set the value of the MCN!
					cue_status.got_MCN = 1; //Gotten a MCN now!
				}
			}
		}
		//Otherwise, ignore the unknown command!
	}

	//Handle the EOF case now!

	if (cue_current.is_present && (!cue_next.is_present) && cue_current.status.got_file && cue_current.status.got_track && cue_current.status.got_index) //Handle the cue_current if it and cue_next isn't present!
	{
		if (safe_strlen(cue_current.status.file_type,sizeof(cue_current.status.file_type)) != strlen("binary")) goto finishup; //Invalid file type!
		if (!(strcmp(cue_current.status.file_type, "binary") == 0)) //Not supported file backend type!
		{
			goto finishup; //Finish up!
		}
		//Fill the end locations into the current entry, based on the next entry!
		BIGFILE *source;
		memset(&fullfilename, 0, sizeof(fullfilename)); //Init!
		safestrcpy(fullfilename, sizeof(fullfilename), diskpath); //Disk path!
		safestrcat(fullfilename, sizeof(fullfilename), "/");
		safestrcat(fullfilename, sizeof(fullfilename), cue_current.status.filename); //The full filename!
		source = emufopen64(fullfilename,"rb"); //Open the backend data file!
		if (!source) goto finishup; //Couldn't open the source!
		if (emufseek64(source, 0, SEEK_END) == 0) //Went to EOF?
		{
			fsize = emuftell64(source); //What is the size of the file!
			
			memcpy(&cue_next, &cue_current, sizeof(cue_current)); //Copy the current as the next!
			++cue_next.status.index; //Take the index one up!
			LBA = CUE_MSF2LBA(cue_current.status.M, cue_current.status.S, cue_current.status.F); //Get the LBA of the last block the entry as the start of the final block!
			fsize -= cue_current.status.datafilepos; //Get the address of the last block the entry starts as!
			LBA += (uint_32)(fsize/cue_current.status.track_mode->sectorsize); //What LBA are we going to try to read at most!
			CUE_LBA2MSF(LBA, &cue_next.status.M, &cue_next.status.S, &cue_next.status.F); //Convert the LBA back to MSF for the fake next record based on the file size(for purposes on the final index entry going until EOF of the source file)!
		}
		else //Couldn't goto EOF?
		{
			return 0; //Couldn't go EOF, so unknown size!
		}
		emufclose64(source); //Close the source!

		//Autodetect final MSF address and give it as a result!
		--LBA; //Take the end position of us!
		CUE_LBA2MSF(LBA, &cue_current.endM, &cue_current.endS, &cue_current.endF); //Save the calculated end position of the selected index!
		prev_LBA = CUE_MSF2LBA(cue_current.status.M, cue_current.status.S, cue_current.status.F); //Previous LBA for more calculations!
		if (cue_next.status.postgap_pending) //Postgap was pending for us?
		{
			//Handle postgap using prev_LBA-LBA, postgap info and MSF position!
			gap_startAddr = (cue_next.status.MSFPosition) + (LBA - prev_LBA) + 1; //The start of the gap!
			gap_endAddr = gap_startAddr + (cue_next.status.postgap_pending_duration - 1); //The end of the gap!
			cue_next.status.MSFPosition += (LBA - prev_LBA) + 1; //Add the previous track size of the final entry!
			//Handle the postgap for the previous track now!
			if (CUE_MSF2LBA(orig_M,orig_S,orig_F)<gap_startAddr) goto notthispostgap2; //Before start? Not us!
			if (CUE_MSF2LBA(orig_M,orig_S,orig_F)>gap_endAddr) goto notthispostgap2; //After end? not us!
			//We're this postgap!
			result = (-2LL - (int_64)((gap_endAddr-CUE_MSF2LBA(orig_M,orig_S,orig_F))+1)); //Give the result as the difference until the next track!
		notthispostgap2:
			cue_next.status.MSFPosition += cue_next.status.postgap_pending_duration; //Add the postgap to the size!
			cue_next.status.postgap_pending = 0; //Not pending anymore!
			cue_status.postgap_pending = 0; //Not pending anymore!
		}
		else //Calculate the next record for us!
		{
			cue_next.status.MSFPosition += (LBA - prev_LBA) + 1; //Add the previous track size of the final entry!
		}

		//Pregap can't be pending at EOF, since there's alway an index after it! Cue files always end with an index and maybe a postgap after it! Otherwise, ignore it!

		if ((specialfeatures & 4) == 0)
		{
			cueimage_fillMSF(device, &got_startMSF, &cue_current, &cue_next, cue_current.status.track_number, cue_current.status.index, startM, startS, startF, endM, endS, endF); //Fill info!
		}
		//Duplicate MSF into the result!
		*M = *endM;
		*S = *endS;
		*F = *endF;

		LBA = CUE_MSF2LBA(orig_M, orig_S, orig_F); //What LBA are we going to try to read!
		if (cue_current.status.MSFPosition >= (LBA+1)) goto finishup; //Invalid to read(non-zero length)?
		if (((disks[device].selectedtrack == cue_current.status.track_number) || (disks[device].selectedtrack==0)) &&
			((disks[device].selectedsubtrack == cue_current.status.index) || (disks[device].selectedsubtrack==0))) //Current track number and subtrack number to lookup?
		{
			if (cue_current.status.got_file && cue_current.status.got_index) //Got file and index to lookup? Otherwise, not found!
			{
				if (cue_current.status.MSFPosition > LBA) goto finishup; //Invalid? Current LBA isn't in our range(we're requesting before it)?
				if ((cue_current.status.MSFPosition + (CUE_MSF2LBA(cue_current.endM, cue_current.endS, cue_current.endF) - CUE_MSF2LBA(cue_current.status.M, cue_current.status.S, cue_current.status.F))) < LBA) goto finishup; //Invalid? Current LBA isn't in our range(we're requesting after it)!
				if (!cue_current.status.index) goto finishup; //Not a valid index(index 0 is a pregap)!
			foundMSF: //Found the location of our data?
				if ((specialfeatures & 4) == 0)
				{
					cueimage_fillMSF(device, &got_startMSF, &cue_current, &cue_next, cue_current.status.track_number, cue_current.status.index, startM, startS, startF, endM, endS, endF); //Fill info!
				}
				if (!(strcmp(cue_current.status.file_type, "binary") == 0)) //Not supported file backend type!
				{
					goto finishup; //Finish up!
				}
				emufclose64(f); //Close the sheet, we're done with it! All data we need is loaded into cue_current!
				//Check our parameters to be valid!
				if ((size != cue_current.status.track_mode->sectorsize) && buffer && size) return 0; //Invalid sector size not matching specified! Only apply when specifying a sector size(non-zero size)!
				if (buffer == NULL) return 1; //Finished reading without buffer and size! Size 0 with a buffer is allowed!
				char fullfilename[256];
				memset(&fullfilename, 0, sizeof(fullfilename)); //Init!
				safestrcpy(fullfilename, sizeof(fullfilename), diskpath); //Disk path!
				safestrcat(fullfilename, sizeof(fullfilename), "/");
				safestrcat(fullfilename, sizeof(fullfilename), cue_current.status.filename); //The full filename!
				source = emufopen64(fullfilename, "rb"); //Open the backend data file!
				if (!source) goto finishupno_f; //Couldn't open the source!
				if (emufseek64(source, 0, SEEK_END) == 0) //Went to EOF?
				{
					fsize = emuftell64(source); //What is the size of the file!
				}
				else
				{
					return 0; //Can't seek to the end!
				}
				if (!buffer) //No buffer?
				{
					emufclose64(source);
					return 1 + cue_current.status.track_mode->mode; //No buffer to read to, silently abort, with an extra result code for being a NOP!
				}
				if (cue_current.status.datafilepos >= fsize) //Past EOF?
				{
					emufclose64(source);
					return 0; //Past EOF!
				}
				if ((cue_current.status.datafilepos + (((CUE_MSF2LBA(orig_M, orig_S, orig_F) - cue_current.status.MSFPosition))*cue_current.status.track_mode->sectorsize))>=fsize) //Past EOF?
				{
					emufclose64(source);
					return 0; //Past EOF!
				}
				if ((cue_current.status.datafilepos + ((((CUE_MSF2LBA(orig_M, orig_S, orig_F) - cue_current.status.MSFPosition))+1)*cue_current.status.track_mode->sectorsize)>fsize)) //Past EOF?
				{
					emufclose64(source);
					return 0; //Past EOF!
				}
				if (emufseek64(source, (cue_current.status.datafilepos + (((CUE_MSF2LBA(orig_M, orig_S, orig_F) - cue_current.status.MSFPosition))*cue_current.status.track_mode->sectorsize)), SEEK_SET) != 0) //Past EOF?
				{
					emufclose64(source);
					return 0; //Couldn't seek to sector!
				}
				if (size) //Something to read at all?
				{
					if (emufread64(buffer, 1, size, source) != size) //Failed reading the data?
					{
						emufclose64(source);
						return 0; //Couldn't read the data!
					}
				}
				//Data has been read from the backend file(or nothing)!
				emufclose64(source);
				return 1+cue_current.status.track_mode->mode; //We've found the location of our data! Give 2+mode for the read sector type!
			}
		}
	}

	finishup:
	emufclose64(f); //Close the sheet!

	finishupno_f: //Realy finish up!
	return result; //Failed!
}

int_64 cueimage_readsector(int device, byte M, byte S, byte F, void *buffer, word size) //Read a n-byte sector! Result=Type on success, 0 on error, -1 on not found!
{
	byte startM, startS, startF, endM, endS, endF;
	byte M2, S2, F2; //Duplicates for handling!
	M2 = M; //Requested minute!
	S2 = S; //Requested second!
	F2 = F; //Requested frame!
	return cueimage_REAL_readsector(device, &M2, &S2, &F2,&startM,&startS,&startF,&endM,&endS,&endF, buffer, size,0); //Direct call!
}

int_64 cueimage_getgeometry(int device, byte *M, byte *S, byte *F, byte *startM, byte *startS, byte *startF, byte *endM, byte *endS, byte *endF, byte specialfeatures) //Read a n-byte sector! 1 on read success, 0 on error, -1 on not found!
{
	//Apply maximum numbers!
	*M = 0xFF;
	*S = 59;
	*F = 74;
	return cueimage_REAL_readsector(device, M, S, F, startM, startS, startF, endM, endS, endF, NULL, 2048,1+(specialfeatures<<1)); //Just apply a read from the disk without result buffer(nothing to read after all)!
}
