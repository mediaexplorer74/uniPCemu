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
#include "headers/support/zalloc.h" //Memory support!
#include "headers/hardware/ports.h" //I/O support!
#include "headers/support/locks.h" //Lock support!
#include "headers/emu/timers.h" //Timer support!
#include "headers/emu/input.h" //Input support!
#include "headers/cpu/cpu.h" //CPU support!
#include "headers/emu/gpu/gpu_text.h" //GPU text surface support!
#include "headers/support/log.h" //Logging support!
#include "headers/support/highrestimer.h" //Time support!
#include "headers/emu/emucore.h" //Emulator start/stop support!
#include "headers/fopen64.h" //64-bit fopen support!

//Player time update interval in ns!
#define PLAYER_USINTERVAL 1000.0f
//DRO MS in CPU timestamps! The counter runs at ns rate, so use us to ms convertion!
#define DRO_MS (PLAYER_USINTERVAL*1000.0f)
#define PLAYER_TIMEINTERVAL (DRO_MS*100.0f)

#include "headers/packed.h" //Packed!
typedef struct PACKED
{
	byte cSignature[8]; //BWRAWOPL
	word iVersionMajor; //Version number (high)
	word iVersionMinor; //Version number (low)
} DR0HEADER;
#include "headers/endpacked.h" //End packed!

enum IHARDWARETYPE01 {
	IHARDWARETYPE01_OPL2 = 0,
	IHARDWARETYPE01_OPL3 = 1,
	IHARDWARETYPE01_DUALOPL2 = 2
};

enum IHARDWARETYPE20 {
	IHARDWARETYPE20_OPL2 = 0,
	IHARDWARETYPE20_OPL3 = 1,
	IHARDWARETYPE20_DUALOPL2 = 2
};

enum COMMANDFORMAT {
	COMMANDFORMAT_INTERLEAVED = 0, //Interleaved commands/data
	COMMANDFORMAT_COMMANDS_DATA = 1 //First all commands, then all data
};

enum {
	DR0REGISTER_DELAY8 = 0, //Data byte following + 1 is the amount in milliseconds
	DR0REGISTER_DELAY16 = 1, //Same as above, but 16-bits
	DR0REGISTER_LOWOPLCHIP = 2, //Switch to Low OPL chip (#0)
	DR0REGISTER_HIGHOPLCHIP = 3, //Switch to High OPL chip (#1)
	DR0REGISTER_ESC = 4 //Escape: The next two bytes are normal register/value data.
} DR0REGISTER;

#include "headers/packed.h" //Packed!
typedef struct PACKED
{
	uint_32 iLengthMS; //Length of the song in ms
	uint_32 iLengthBytes; //Length of the song data in bytes
	byte iHardwareType; //Flag listing of the hardware used in the song
	byte iHardwareExtra[3]; //Rest of hardware type or song data. Must be 0. If nonzero, this is an early header.
} DR01HEADER;
#include "headers/endpacked.h" //End packed!


#include "headers/packed.h" //Packed!
typedef struct PACKED
{
	uint_32 iLengthMS; //Length of the song in ms
	uint_32 iLengthBytes; //Length of the song data in bytes
	byte iHardwareType; //Flag listing of the hardware used in the song
} DR01HEADEREARLY;
#include "headers/endpacked.h" //End packed!


#include "headers/packed.h" //Packed!
typedef struct PACKED
{
	uint_32 iLengthPairs; //Length of the song in register/value pairs
	uint_32 iLengthMS; //Length of the song data in milliseconds
	byte iHardwareType; //Flag listing of the hardware used in the song
	byte iFormat; //Data arrangement
	byte iCompression; //Compression type. 0=No compression. Currently only is used.
	byte iShortDelayCode; //Command code for short delay(1-256ms).
	byte iLongDelayCode; //Command for long delay(256ms)
	byte iCodemapLength; //Length of the codemap table, in bytes
} DR20HEADER;
#include "headers/endpacked.h" //End packed!

typedef struct
{
	word w; //Temporary 16-bit number.
	int streambuffer; //Stream data buffer for read data!
	byte value,channel; //OPL Register/value container!
	byte whatchip; //Low/high chip selection!
	float playtime; //Play time, in ms!
	float oldplaytime; //Old play time!
	//All file data itself:
	DR0HEADER header;
	DR01HEADEREARLY earlyheader;
	DR01HEADER oldheader;
	DR20HEADER newheader;
	byte CodemapTable[0x100]; //256-byte Codemap table!
	byte *data;
	byte *stream; //The stream to use.
	byte *eos; //The end of the stream is reached!
	uint_32 datasize;
	byte stoprunning;
	byte droversion;
	DOUBLE currenttime; //Current time recorded!
	byte timedirty; //Time needs to be updated?
} DROPLAYER; //All data needed to play a DRO file!

//The codemap table has a length of iCodemapLength entries of 1 byte.

//The DR0 file reader.
byte readDRO(char *filename, DR0HEADER *header, DR01HEADEREARLY *earlyheader, DR01HEADER *oldheader, DR20HEADER *newheader, byte *CodemapTable, byte **data, uint_32 *datasize)
{
	byte correctSignature[8] = {'D','B','R','A','W','O','P','L'};
	byte version = 0; //The version to return!
	word temp;
	FILEPOS filesize;
	BIGFILE *f; //The file!
	FILEPOS oldpos;
	f = emufopen64(filename,"rb"); //Open the filename!
	if (!f) return 0; //File not found!
	byte empty[3] = {0,0,0}; //Empty data!
	if (emufread64(header,1,sizeof(*header),f)!=sizeof(*header))
	{
		emufclose64(f);
		return 0; //Error reading the file!
	}
	if (memcmp(&header->cSignature,&correctSignature,8)!=0) //Signature error?
	{
		emufclose64(f);
		return 0; //Error: Invalid signature!
	}
	if (((header->iVersionMajor==0) && (header->iVersionMinor==1)) || ((header->iVersionMajor == 1) && (header->iVersionMinor == 0))) //Version 1.0(old) or 0.1(new)?
	{
		oldpos = emuftell64(f); //Save the old position to jump back to!
		if (emufread64(oldheader,1,sizeof(*oldheader),f)!=sizeof(*oldheader)) //New header invalid size/read error?
		{
			emufclose64(f);
			return 0; //Error reading the file!
		}
		if (memcmp(&oldheader->iHardwareExtra,&empty,sizeof(oldheader->iHardwareExtra))!=0) //Maybe earlier version?
		{
			emufseek64(f,oldpos,SEEK_SET); //Return!
			if (emufread64(earlyheader,1,sizeof(*earlyheader),f)!=sizeof(*earlyheader)) //New header invalid size/read error?
			{
				emufclose64(f);
				return 0; //Error reading the file!
			}
			version = 1; //Early old-style header!
		}
		else //Old-style header?
		{
			version = 2; //Old-style header!
			memcpy(earlyheader,oldheader,sizeof(*earlyheader)); //Copy to the early header for easier reading, since it's only padded!
		}

		//Since we're old-style anyway, patch the codemap and 2.0 table values based on the old header now!
		for (temp=0;temp<0x100;++temp)
		{
			CodemapTable[temp] = (temp&0x7F); //All ascending, repeat, as per Dual-OPL2!
		}
		newheader->iCodemapLength = 0x00; //Maximum codemap length(No translation)!
		newheader->iLengthPairs = (oldheader->iLengthBytes>>1); //Length in pairs(2 bytes)!
		newheader->iLengthMS = oldheader->iLengthMS; //Length in pairs(2 bytes)!
		switch (oldheader->iHardwareType)
		{
		case IHARDWARETYPE01_OPL2:
			newheader->iHardwareType = IHARDWARETYPE20_OPL2; //Translate!
			break;
		case IHARDWARETYPE01_OPL3:
			newheader->iHardwareType = IHARDWARETYPE20_OPL3; //Translate!
			break;
		case IHARDWARETYPE01_DUALOPL2:
			newheader->iHardwareType = IHARDWARETYPE20_DUALOPL2; //Translate!
			break;
		default:
			break;
		}
		newheader->iFormat = COMMANDFORMAT_INTERLEAVED; //Default to interleaved format!
		newheader->iCompression = 0; //No compression!
		//Delay codes aren't used!
	}
	else if ((header->iVersionMajor==2) && (header->iVersionMinor==0)) //Version 2.0?
	{
		if (emufread64(newheader,1,sizeof(*newheader),f)!=sizeof(*newheader)) //New header invalid size/read error?
		{
			emufclose64(f);
			return 0; //Error reading the file!
		}
		if (!newheader->iCodemapLength) //Invalid code map length?
		{
			emufclose64(f);
			return 0; //Error reading the file: invalid code map length!
		}
		memset(CodemapTable,0,256); //Clear the entire table for the new file!
		if (emufread64(CodemapTable,1,newheader->iCodemapLength,f)!=newheader->iCodemapLength) //New header invalid size/read error?
		{
			emufclose64(f);
			return 0; //Error reading the file!
		}
		version = 3; //2.0 version!
	}
	else //Invalid version?
	{
		emufclose64(f);
		return 0; //Error: Invalid signature!
	}

	oldpos = emuftell64(f); //Save the old position to jump back to!
	emufseek64(f,0,SEEK_END);
	filesize = emuftell64(f); //File size!
	emufseek64(f,oldpos,SEEK_SET); //Return to the start of the data!
	filesize -= oldpos; //Difference is the file size!
	if ((!filesize) || (filesize&~0xFFFFFFFFU))
	{
		emufclose64(f);
		return 0; //Error: invalid file size!
	}

	*data = zalloc((uint_32)filesize,"DROFILE",NULL); //Allocate a DR0 file's contents!
	if (!*data) //Failed to allocate?
	{
		emufclose64(f);
		return 0; //Error: ran out of memory!
	}

	if (emufread64(*data,1,filesize,f)!=filesize) //Error reading contents?
	{
		freez((void **)data,(uint_32)filesize,"DROFILE"); //Release the file!
		emufclose64(f);
		return 0; //Error: file couldn't be read!
	}
	emufclose64(f); //Finished, close the file!
	*datasize = (uint_32)filesize; //Save the filesize for reference!
	return version; //Successfully read the file!
}

void OPLXsetreg(byte version, byte newHardwareType, byte whatchip,byte reg,byte *CodemapTable,byte codemapLength,byte value)
{
	byte chip;
	if (version < 3) //Version 1.0?
	{
		switch (newHardwareType) //What hardware type?
		{
		case IHARDWARETYPE20_OPL2: //OPL2?
			chip = 0; //Only one chip(Dual OPL-2)/register bank(OPL-3)!
			break;
		case IHARDWARETYPE20_OPL3: //OPL3?
			chip = whatchip; //Not supported yet: High register bank!
			break;
		case IHARDWARETYPE20_DUALOPL2: //Dual OPL2?
			chip = whatchip; //Not supported yet: High chip!
			break;
		default: //Unknown hardware type?
			return; //Unknown hardware: abort!
			break;
		}
	}
	else //Version 2.0?
	{
		switch (newHardwareType) //What hardware type?
		{
		case IHARDWARETYPE20_OPL2: //OPL2?
			break;
		case IHARDWARETYPE20_OPL3: //OPL3?
			break;
		case IHARDWARETYPE20_DUALOPL2: //Dual OPL2?
			break;
		default: //Unknown hardware type?
			return; //Unknown hardware: abort!
			break;
		}
		chip = (reg & 0x80)?1:0; //Not supported yet: High register bank/chip!
		reg &= 0x7F; //Only the low bits are looked up!
		if (reg<codemapLength) reg = CodemapTable[reg]; //Translate reg through the Codemap Table when within range!
	}
	if (chip)
	{
		return; //High chip/bank isn't supported yet!
	}
	//Ignore the chip!
	PORT_OUT_B(0x388,reg);
	PORT_OUT_B(0x389,value);
}

int readStream(byte **stream, byte *eos)
{
	if (*stream!=eos) //Valid item to read?
		return (int)*((*stream)++);
	return -1; //Invalid item!
}

extern GPU_TEXTSURFACE *BIOS_Surface; //Our display(BIOS) text surface!

byte showTime(float playtime, float *oldplaytime)
{
	static char playtimetext[256] = ""; //Time in text format!
	if ((playtime != *oldplaytime) && (playtime>=(*oldplaytime+PLAYER_TIMEINTERVAL))) //Playtime updated?
	{
		convertTime(playtime/PLAYER_USINTERVAL, &playtimetext[0],sizeof(playtimetext)); //Convert the time(in us)!
		playtimetext[safestrlen(playtimetext,sizeof(playtimetext))-9] = '\0'; //Cut off the timing past the second!
		GPU_text_locksurface(BIOS_Surface); //Lock!
		GPU_textgotoxy(BIOS_Surface,0, GPU_TEXTSURFACE_HEIGHT - 2); //Show playing init!
		GPU_textprintf(BIOS_Surface, RGB(0xFF, 0xFF, 0xFF), RGB(0xBB, 0x00, 0x00), "Play time: %s", playtimetext); //Current play time!
		GPU_text_releasesurface(BIOS_Surface); //Lock!			
		*oldplaytime = playtime; //We're updated with this value!
		return 1; //Time updated!
	}
	return 0; //Time not updated!
}

void clearTime()
{
	static char playtimetext[256] = "";
	GPU_text_locksurface(BIOS_Surface); //Lock!
	convertTime(0, &playtimetext[0],sizeof(playtimetext)); //Convert the time(in us)!
	playtimetext[safestrlen(playtimetext,sizeof(playtimetext)) - 9] = '\0'; //Cut off the timing past the second!
	byte b;
	for (b=0;b<safestrlen(playtimetext,sizeof(playtimetext));) playtimetext[b++] = ' '; //Clear the text!
	GPU_textgotoxy(BIOS_Surface,0, GPU_TEXTSURFACE_HEIGHT - 2); //Show playing init!
	GPU_textprintf(BIOS_Surface, RGB(0x00, 0x00, 0x00), RGB(0x00, 0x00, 0x00), "           %s     ", playtimetext); //Clear the play time!
	GPU_text_releasesurface(BIOS_Surface); //Lock!			
}

float speedup = 1.0f; //How much speed to apply? 1.0=100% speed!
DROPLAYER *droplayer = NULL; //No DRO file playing!

void finishDROPlayer()
{
	if (droplayer) //Registered?
	{
		freez((void**)&droplayer->data, droplayer->datasize, "DROFILE");
		clearTime(); //Clear our time displayed!
		droplayer = NULL; //Destroy the player: we're finished!
	}
}

void stepDROPlayer(DOUBLE timepassed)
{
	byte timeupdated;
	if (droplayer) //Are we playing anything?
	{
		droplayer->currenttime += timepassed; //Add the time passed to the playback time!
		//Checks time and plays the DRO file selected!
		timeupdated = showTime((float)droplayer->currenttime, &droplayer->oldplaytime); //Update time!
		if (droplayer->currenttime>=droplayer->playtime) //Enough time passed to start playback (again)?
		{
			for (;droplayer->currenttime>=droplayer->playtime;) //Execute all commands we can in this time!
			{
				//Process input!
				droplayer->streambuffer = readStream(&droplayer->stream,droplayer->eos); //Read the instruction from the stream!
				if (droplayer->streambuffer==-1) goto finishinput; //Stop if reached EOS!
				if (droplayer->droversion==3) //v2.0 commands?
				{
					droplayer->channel = (byte)droplayer->streambuffer; //We're the channel!
					droplayer->streambuffer = readStream(&droplayer->stream, droplayer->eos); //Read the value from the stream!
					if (droplayer->streambuffer == -1) goto finishinput; //Stop if reached EOS!
					droplayer->value = (byte)droplayer->streambuffer; //We're the value!
					if (droplayer->channel==droplayer->newheader.iShortDelayCode) //Short delay?
					{
						droplayer->playtime += (((float)(DRO_MS * (droplayer->value + 1)))/speedup); //Update player time!
					}
					else if (droplayer->channel==droplayer->newheader.iLongDelayCode) //Long delay?
					{
						droplayer->playtime += (((float)(DRO_MS*((droplayer->value+1)<<8)))/speedup); //Update player time!
					}
					else goto runinstruction; //Check for v2.0 commands?
					goto nextinstruction;
				}
				//v1.0 commands!
				if (droplayer->streambuffer==DR0REGISTER_DELAY8) //8-bit delay?
				{
					droplayer->streambuffer = readStream(&droplayer->stream, droplayer->eos); //Read the instruction from the stream!
					if (droplayer->streambuffer==-1) goto finishinput; //Stop if reached EOS!
					droplayer->playtime += (((float)(DRO_MS*(droplayer->streambuffer+1)))/speedup); //Update player time!
				}
				else if (droplayer->streambuffer==DR0REGISTER_DELAY16) //16-bit delay?
				{
					droplayer->streambuffer = readStream(&droplayer->stream, droplayer->eos); //Read the instruction low byte from the stream!
					if (droplayer->streambuffer==-1) goto finishinput; //Stop if reached EOS!
					droplayer->w = (droplayer->streambuffer&0xFF); //Load low byte!
					droplayer->streambuffer = readStream(&droplayer->stream, droplayer->eos); //Read the instruction high byte from the stream!
					if (droplayer->streambuffer == -1) goto finishinput; //Stop if reached EOS!
					droplayer->w |= ((droplayer->streambuffer & 0xFF)<<8); //Load high byte!
					droplayer->playtime += (((float)(DRO_MS*(droplayer->w+1)))/speedup); //Update player time!
				}
				else if ((droplayer->streambuffer==DR0REGISTER_LOWOPLCHIP) && (droplayer->newheader.iHardwareType!=IHARDWARETYPE20_OPL2)) //Low OPL chip and supported?
				{
					droplayer->whatchip = 0; //Low channel in dual-OPL!
				}
				else if ((droplayer->streambuffer==DR0REGISTER_HIGHOPLCHIP) && (droplayer->newheader.iHardwareType != IHARDWARETYPE20_OPL2)) //High OPL chip and supported?
				{
					droplayer->whatchip = 1; //High channel in dual-OPL!
				}
				else if (droplayer->streambuffer==DR0REGISTER_ESC) //Escape?
				{
					droplayer->streambuffer = readStream(&droplayer->stream, droplayer->eos); //Read the instruction from the stream!
					if (droplayer->streambuffer==-1) goto finishinput; //Stop if reached EOS!
					goto escapedinstruction; //Execute us as a normal instruction!
				}
				else //Normal instruction?
				{
					escapedinstruction: //Process a normal instruction!
					droplayer->channel = (byte)droplayer->streambuffer; //The first is the channel!
					droplayer->streambuffer = readStream(&droplayer->stream, droplayer->eos); //Read the instruction from the stream!
					if (droplayer->streambuffer==-1) goto finishinput; //Stop if reached EOS!
					droplayer->value = (byte)droplayer->streambuffer; //The second is the value!
					runinstruction: //Run a normal instruction!
					OPLXsetreg(droplayer->droversion, droplayer->newheader.iHardwareType, droplayer->whatchip, droplayer->channel,&droplayer->CodemapTable[0], droplayer->newheader.iCodemapLength, droplayer->value); //Set the register!
				}

				nextinstruction: //Execute next instruction!
				//Check for stopping the song!			
				lock(LOCK_INPUT);
				if (psp_keypressed(BUTTON_CANCEL) || psp_keypressed(BUTTON_STOP)) //Circle/stop pressed? Request to stop playback!
				{
					droplayer->stoprunning |= 2; //Set termination flag to request a termination by pressing!
				}
				else if ((droplayer->stoprunning & 2) || shuttingdown()) //Requested termination by pressing and released?
				{
					droplayer->stoprunning = 1; //We're terminating now!
				}
				unlock(LOCK_INPUT);

				if (droplayer->stoprunning&1) goto finishinput; //Requesting termination? Start quitting!
			}
			goto continueplayer; //Continue playing by default!
			
			finishinput: //Finish the input! Close the file!
			showTime((float)droplayer->currenttime, &droplayer->oldplaytime); //Update time!
	
			for (droplayer->w=0;droplayer->w<=0xFF;++droplayer->w) //Clear all registers!
			{
				OPLXsetreg(droplayer->droversion, droplayer->newheader.iHardwareType,0,(droplayer->w&0x7F),&droplayer->CodemapTable[0], droplayer->newheader.iCodemapLength,0); //Clear all registers, as per the DR0 specification!
				OPLXsetreg(droplayer->droversion, droplayer->newheader.iHardwareType,1,(droplayer->w&0x7F),&droplayer->CodemapTable[0], droplayer->newheader.iCodemapLength,0); //Clear all registers, as per the DR0 specification!
			}
			finishDROPlayer(); //Finish our player!
		}
		else if (timeupdated) //Time is updated without anything playing and waiting for something?
		{
			//Check for stopping the song!			
			lock(LOCK_INPUT);
			if (psp_keypressed(BUTTON_CANCEL) || psp_keypressed(BUTTON_STOP)) //Circle/stop pressed? Request to stop playback!
			{
				droplayer->stoprunning |= 2; //Set termination flag to request a termination by pressing!
			}
			else if ((droplayer->stoprunning & 2) || shuttingdown()) //Requested termination by pressing and released?
			{
				droplayer->stoprunning = 1; //We're terminating now!
			}
			unlock(LOCK_INPUT);
			if (droplayer->stoprunning & 1) goto finishinput; //Requesting termination? Start quitting!
		}
	}
	continueplayer: return; //Continue playing!
}

extern byte EMU_RUNNING; //Emulator running? 0=Not running, 1=Running, Active CPU, 2=Running, Inactive CPU (BIOS etc.)
DROPLAYER playedfile; //A file to play!

//The player itself!
byte playDROFile(char *filename, byte showinfo) //Play a MIDI file, CIRCLE to stop playback!
{
	byte EMU_RUNNING_BACKUP=0;
	lock(LOCK_CPU);
	EMU_RUNNING_BACKUP = EMU_RUNNING; //Make a backup to restore after we've finished!
	unlock(LOCK_CPU);
	//Start reading the file!
	playedfile.playtime = 0.0; //No time passed!
	playedfile.currenttime = 0.0; //No time passed!
	#ifdef IS_LONGDOUBLE
	playedfile.oldplaytime = -1.0L; //Uninitialized = any negative time!
	#else
	playedfile.oldplaytime = -1.0; //Uninitialized = any negative time!
	#endif
	playedfile.value = playedfile.channel = 0;
	playedfile.whatchip = 0;
	playedfile.stoprunning = 0;
	playedfile.timedirty = 1; //Time is dirty!

	if ((playedfile.droversion = readDRO(filename, &playedfile.header, &playedfile.earlyheader, &playedfile.oldheader, &playedfile.newheader, &playedfile.CodemapTable[0], &playedfile.data, &playedfile.datasize))>0) //Loaded DRO file?
	{
		stopTimers(0); //Stop most timers for max compatiblity and speed!
		//Initialise our device!
		lock(LOCK_CPU);
		for (playedfile.w=0;playedfile.w<=0xFF;++playedfile.w) //Clear all registers!
		{
			OPLXsetreg(playedfile.droversion,playedfile.newheader.iHardwareType,0,(byte)playedfile.w,&playedfile.CodemapTable[0],playedfile.newheader.iCodemapLength,0); //Clear all registers, as per the DR0 specification!
			OPLXsetreg(playedfile.droversion,playedfile.newheader.iHardwareType,1,(byte)playedfile.w,&playedfile.CodemapTable[0],playedfile.newheader.iCodemapLength,0); //Clear all registers, as per the DR0 specification!
		}
		unlock(LOCK_CPU);

		startTimers(1);
		startTimers(0); //Start our timers!

		playedfile.stream = playedfile.data; //Start processing the start of the stream!
		playedfile.eos = &playedfile.data[playedfile.datasize]; //The end of the stream!

		resumeEMU(0); //Resume the emulator!
		lock(LOCK_CPU); //Lock the CPU: we're checking for finishing!
		droplayer = &playedfile; //Start playing this file!
		CPU[activeCPU].halt |= 0x12; //Force us into HLT state, starting playback!
		BIOSMenuResumeEMU(); //Resume the emulator from the BIOS menu thread!
		EMU_stopInput(); //We don't want anything to be input into the emulator!
		for (;droplayer;) //Wait while playing!
		{
			unlock(LOCK_CPU);
			delay(1000000); //Wait a bit for the playback!
			lock(LOCK_CPU);
		}
		CPU[activeCPU].halt &= ~0x12; //Remove the forced execution!
		unlock(LOCK_CPU); //We're finished with the CPU!
		pauseEMU(); //Stop timers and back to the BIOS menu!
		lock(LOCK_CPU);
		EMU_RUNNING = EMU_RUNNING_BACKUP; //We're not running atm, restore the backup!
		unlock(LOCK_CPU); //We're finished with the CPU!
		return playedfile.stoprunning?0:1; //Played without termination?
	}
	return 0; //Invalid file?
}