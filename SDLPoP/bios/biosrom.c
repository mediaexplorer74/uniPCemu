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

#include "headers/mmu/mmuhandler.h" //Basic MMU handler support!
#include "headers/bios/bios.h" //BIOS support!
#include "headers/support/zalloc.h" //Allocation support!
#include "headers/cpu/cpu.h" //CPU support!
#include "headers/cpu/paging.h" //Paging support for address decoding (for Turbo XT BIOS detection)!
#include "headers/support/locks.h" //Locking support!
#include "headers/fopen64.h" //64-bit fopen support!
#include "headers/emu/emu_misc.h" //For 128-bit shifting support!

//Comment this define to disable logging
//#define __ENABLE_LOGGING

#ifdef __ENABLE_LOGGING
#include "headers/support/log.h" //Logging support!
#else
//Ignore logging!
#define dolog(...)
#endif

byte EMU_BIOS[0x10000]; //Full custom BIOS from 0xF0000-0xFFFFF for the emulator itself to use!
byte EMU_VGAROM[0x10000]; //Maximum size custom BIOS VGA ROM!

byte* BIOS_combinedROM; //Combined ROM with all odd/even made linear!
uint_32 BIOS_combinedROM_size = 0; //The size of the combined ROM!
byte *BIOS_ROMS[0x100]; //All possible BIOS roms!
byte BIOS_ROMS_ext[0x100]; //Extended load used to load us?
uint_32 BIOS_ROM_size[0x100]; //All possible BIOS ROM sizes!

byte numOPT_ROMS = 0;
byte *OPT_ROMS[40]; //Up to 40 option roms!
byte *OPT_ROMS_shadow[40]; //Up to 40 option roms!
uint_32 OPTROM_size[40]; //All possible OPT ROM sizes!
uint_64 OPTROM_location[40]; //All possible OPT ROM locations(low word) and end position(high word)!
char OPTROM_filename[40][256]; //All possible filenames for the OPTROMs loaded!

byte OPTROM_writeSequence[40]; //Current write sequence command state!
byte OPTROM_pendingAA_1555[40]; //Pending write AA to 1555?
byte OPTROM_pending55_0AAA[40]; //Pending write 55 to 0AAA?
byte OPTROM_writeSequence_waitingforDisable[40]; //Waiting for disable command?
byte OPTROM_writeenabled[40]; //Write enabled ROM?
DOUBLE OPTROM_writetimeout[40]; //Timeout until SDP is activated!
byte OPTROM_timeoutused = 0;

extern BIOS_Settings_TYPE BIOS_Settings;

int BIOS_load_VGAROM(); //Prototype: Load custom ROM from emulator itself!

byte ISVGA = 0; //VGA that's loaded!

char ROMpath[256] = "ROM";

extern byte is_XT; //Are we emulating an XT architecture?

uint_32 BIOSROM_BASE_Modern = 0xFFFF0000; //AT+ BIOS ROM base!
uint_32 BIOSROM_BASE_AT = 0xFF0000; //AT BIOS ROM base!
uint_32 BIOSROM_BASE_XT = 0xF0000; //XT BIOS ROM base!

extern byte is_Compaq; //Are we emulating a Compaq device?
extern byte is_PS2; //Are we emulating a Compaq with PS/2 mouse(modern) device?
extern byte is_i430fx; //Are we emulating a i430fx architecture?

//Extra: Intel Flash ROM support(8-bit) for the MP tables to be supported(not written back to storage yet)!
enum
{
	BLOCK_MAIN1=0,
	BLOCK_MAIN2=1,
	BLOCK_DATA1=2,
	BLOCK_DATA2=3,
	BLOCK_BOOT=4,
	BLOCKS_NUM=5
};

enum
{
	CMD_READ_ARRAY = 0xff,
	CMD_IID = 0x90,
	CMD_READ_STATUS = 0x70,
	CMD_CLEAR_STATUS = 0x50,
	CMD_ERASE_SETUP = 0x20,
	CMD_ERASE_CONFIRM = 0xd0,
	CMD_ERASE_SUSPEND = 0xb0,
	CMD_PROGRAM_SETUP = 0x40,
	CMD_PROGRAM_SETUP_ALT = 0x10
};

//Basic state for managing the BIOS ROM
byte BIOS_writeprotect = 1; //BIOS write protected?
byte BIOS_flash_enabled = 0;
struct
{
	byte command;
	byte status; //bit 7: ready, bit 6: erase suspended when set, bit 5: block erase error(set) or success(cleared), bit 4: byte program error(set) or success(cleared), bit 3: cleared for Vpp OK. bit 2-0: reserved. 
	byte pad;
	byte flags;
	word flash_id;
	word pad16;
	uint_32 program_addr;
	uint_32 blocks_start[BLOCKS_NUM];
	uint_32 blocks_end[BLOCKS_NUM];
	uint_32 blocks_len[BLOCKS_NUM];
} BIOS_flash;

void BIOS_flash_reset()
{
	BIOS_flash.status = 0x80; //Default status!
	BIOS_flash.command = CMD_READ_ARRAY; //Return to giving the array result!
}

byte BIOS_flash_read8(byte* ROM, uint_32 offset, byte* result)
{
	*result = 0xFF; //Default!
	switch (BIOS_flash.command) //Active command?
	{
	default:
	case CMD_READ_ARRAY:
		*result = ROM[offset]; //Plain result!
		return 1; //Mapped as a byte!
		break;
	case CMD_IID:
		if (offset & 1)
		{
			*result = (BIOS_flash.flash_id & 0xFF);
			return 1; //mapped!
		}
		else
		{
			*result = 0x89;
			return 1; //mapped!
		}
		break;
	case CMD_READ_STATUS:
		*result = BIOS_flash.status; //Give status until receiving a new command!
		return 1; //Mapped!
	}
	return 1; //Safeguard!
}

byte blocks_backup[0x20000]; //Backup of written data when failed to flash!

void BIOS_flash_write8(byte* ROM, uint_32 offset, char *filename, byte value)
{
	BIGFILE* f;
	byte i;
	//Hard-coded BIOS mask for 128KB ROM
	#define bbmask 0x1E000
	switch (BIOS_flash.command) //What is the active command?
	{
	case CMD_ERASE_SETUP:
		if (value == CMD_ERASE_CONFIRM)
		{
			for (i = 0; i < 3; ++i) //First 3 blocks!
			{
				if ((offset >= BIOS_flash.blocks_start[i]) && (offset <= BIOS_flash.blocks_end[i])) //Within range of the block?
				{
					memcpy(&blocks_backup, &ROM[BIOS_flash.blocks_start[i]], BIOS_flash.blocks_len[i]); //Create a backup copy!
					memset(&ROM[BIOS_flash.blocks_start[i]], 0xFF, BIOS_flash.blocks_len[i]); //Clear the block!
					f = emufopen64(filename, "rb+"); //Open the file for updating!
					if (!f) //Failed to open?
					{
						memcpy(&ROM[BIOS_flash.blocks_start[i]], &blocks_backup, BIOS_flash.blocks_len[i]); //Restore the backup copy!
						emufclose64(f); //Close the file!
						BIOS_flash.status = (0x80|0x20); //Error!
						goto finishflashingblock;
					}
					else
					{
						emufseek64(f, BIOS_flash.blocks_start[i], SEEK_SET);
						if (emuftell64(f) == BIOS_flash.blocks_start[i]) //Correct position!
						{
							if (emufwrite64(&ROM[BIOS_flash.blocks_start[i]], 1, BIOS_flash.blocks_len[i], f) == BIOS_flash.blocks_len[i]) //Succeeded to overwrite?
							{
								emufclose64(f); //Close the file!
								BIOS_flash.status = 0x80; //Success!
								goto finishflashingblock;
							}
							else //Failed to properly flash?
							{
								memcpy(&ROM[BIOS_flash.blocks_start[i]], &blocks_backup, BIOS_flash.blocks_len[i]); //Restore the backup copy!
								emufclose64(f); //Close the file!
								BIOS_flash.status = (0x80|0x20); //Error!
								goto finishflashingblock;
							}
						}
						else //Failed to flash?
						{
							emufclose64(f); //Close the file!
							BIOS_flash.status = (0x80|0x20); //Error!
							goto finishflashingblock;
						}
					}
				}
			}
		}
		finishflashingblock:
		BIOS_flash.command = CMD_READ_STATUS; //Read the resulting status!
		break;
	case CMD_PROGRAM_SETUP:
	case CMD_PROGRAM_SETUP_ALT:
		if (((offset & bbmask) != (BIOS_flash.blocks_start[4] & bbmask)) && (offset == BIOS_flash.program_addr)) //Flashing a single byte?
		{
			f = emufopen64(filename, "rb+"); //Open the file for updating!
			if (!f) //Failed to open?
			{
				emufclose64(f); //Close the file!
				BIOS_flash.status = (0x80|0x10); //Error!
				goto finishflashingblock;
			}
			else
			{
				emufseek64(f, offset, SEEK_SET);
				if (emuftell64(f) == offset) //Correct position!
				{
					if (emufwrite64(&value, 1, 1, f) == 1) //Succeeded to overwrite?
					{
						emufclose64(f); //Close the file!
						ROM[offset] = value; //Write the value directly to the flash!
						BIOS_flash.status = 0x80; //Success!
						goto finishflashingblock;
					}
					else //Failed to properly flash?
					{
						emufclose64(f); //Close the file!
						BIOS_flash.status = (0x80|0x10); //Error!
						goto finishflashingblock;
					}
				}
				else //Failed to flash?
				{
					emufclose64(f); //Close the file!
					BIOS_flash.status = (0x80|0x10); //Error!
					goto finishflashingblock;
				}
			}
		}
		break;
	default:
		BIOS_flash.command = value; //The command!
		switch (value) //What command?
		{
		case CMD_ERASE_SETUP:
			BIOS_flash.status = 0x80; //Ready, no error to report!
			break;
		case CMD_CLEAR_STATUS:
			BIOS_flash.status = 0x80; //Ready, no error to report!
			break;
		case CMD_PROGRAM_SETUP:
		case CMD_PROGRAM_SETUP_ALT: //Start writing the ROM here!
			BIOS_flash.status = 0x80; //Ready, no error to report!
			BIOS_flash.program_addr = offset;
			break;
		default: //Other command?
			//NOP!
			break;
		}
		break;
	}
}


void scanROM(char *device, char *filename, uint_32 size)
{
	//Special case: 32-bit uses Compaq ROMs!
	snprintf(filename, size, "%s/%s.%s.BIN", ROMpath, device, ((is_i430fx) ? ((is_i430fx == 1) ? "i430fx" : "i440fx") : (is_PS2 ? "PS2" : (is_Compaq ? "32" : (is_XT ? "XT" : "AT"))))); //Create the filename for the ROM for the architecture!
	if (!file_exists(filename)) //This version doesn't exist? Then try the other version!
	{
		snprintf(filename, size, "%s/%s.%s.BIN", ROMpath, device, ((is_i430fx) ? "i430fx" : (is_PS2 ? "PS2" : (is_Compaq ? "32" : (is_XT ? "XT" : "AT"))))); //Create the filename for the ROM for the architecture!
		if (!file_exists(filename)) //This version doesn't exist? Then try the other version!
		{
			snprintf(filename, size, "%s/%s.%s.BIN", ROMpath, device, (is_PS2 ? "PS2" : (is_Compaq ? "32" : (is_XT ? "XT" : "AT")))); //Create the filename for the ROM for the architecture!
			if (!file_exists(filename)) //This version doesn't exist? Then try the other version!
			{
				snprintf(filename, size, "%s/%s.%s.BIN", ROMpath, device, (is_Compaq ? "32" : (is_XT ? "XT" : "AT"))); //Create the filename for the ROM for the architecture!
				if (!file_exists(filename)) //This version doesn't exist? Then try the other version!
				{
					snprintf(filename, size, "%s/%s.%s.BIN", ROMpath, device, is_XT ? "XT" : "AT"); //Create the filename for the ROM for the architecture!
					if (!file_exists(filename)) //This version doesn't exist? Then try the other version!
					{
						snprintf(filename, size, "%s/%s.BIN", ROMpath, device); //CGA ROM!
					}
				}
			}
		}
	}
}

byte BIOS_checkOPTROMS() //Check and load Option ROMs!
{
	uint_32 datapos;
	numOPT_ROMS = 0; //Initialise the number of OPTROMS!
	memset(&OPTROM_writeenabled, 0, sizeof(OPTROM_writeenabled)); //Disable all write enable flags by default!
	memset(&OPTROM_writeSequence, 0, sizeof(OPTROM_writeSequence)); //Disable all write enable flags by default!
	memset(&OPTROM_writeSequence_waitingforDisable, 0, sizeof(OPTROM_writeSequence_waitingforDisable)); //Disable all write enable flags by default!
	memset(&OPTROM_writetimeout,0,sizeof(OPTROM_writetimeout)); //Disable all timers for all ROMs!
	memset(&OPTROM_pending55_0AAA,0,sizeof(OPTROM_pending55_0AAA)); //Disable all timers for all ROMs!
	memset(&OPTROM_pendingAA_1555,0,sizeof(OPTROM_pendingAA_1555)); //Disable all timers for all ROMs!
	OPTROM_timeoutused = 0; //Not timing?
	byte i; //Current OPT ROM!
	uint_32 location; //The location within the OPT ROM area!
	location = 0; //Init location!
	ISVGA = 0; //Are we a VGA ROM?
	for (i=0;(i<NUMITEMS(OPT_ROMS)) && (location<0x20000);i++) //Process all ROMS we can process!
	{
		BIGFILE *f;
		char filename[256];
		memset(&filename,0,sizeof(filename)); //Clear/init!
		if (i) //Not Graphics Adapter ROM?
		{
			//Default!
			snprintf(filename, sizeof(filename), "%s/OPTROM.%s.%u.BIN", ROMpath, (is_i430fx ? ((is_i430fx == 1) ? "i430fx" : "i440fx") : (is_PS2 ? "PS2" : (is_Compaq ? "32" : (is_XT ? "XT" : "AT")))), i); //Create the filename for the ROM for the architecture!
			if (!file_exists(filename)) //This version doesn't exist? Then try the other version!
			{
				snprintf(filename, sizeof(filename), "%s/OPTROM.%s.%u.BIN", ROMpath, (is_i430fx ? "i430fx" : (is_PS2 ? "PS2" : (is_Compaq ? "32" : (is_XT ? "XT" : "AT")))), i); //Create the filename for the ROM for the architecture!
				if (!file_exists(filename)) //This version doesn't exist? Then try the other version!
				{
					snprintf(filename, sizeof(filename), "%s/OPTROM.%s.%u.BIN", ROMpath, (is_PS2 ? "PS2" : (is_Compaq ? "32" : (is_XT ? "XT" : "AT"))), i); //Create the filename for the ROM for the architecture!
					if (!file_exists(filename)) //This version doesn't exist? Then try the other version!
					{
						snprintf(filename, sizeof(filename), "%s/OPTROM.%s.%u.BIN", ROMpath, (is_PS2 ? "32" : (is_Compaq ? "32" : (is_XT ? "XT" : "AT"))), i); //Create the filename for the ROM for the architecture!
						if (!file_exists(filename)) //This version doesn't exist? Then try the other version!
						{
							snprintf(filename, sizeof(filename), "%s/OPTROM.%s.%u.BIN", ROMpath, ((is_Compaq ? "32" : (is_XT ? "XT" : "AT"))), i); //Create the filename for the ROM for the architecture!
							if (!file_exists(filename)) //This version doesn't exist? Then try the other version!
							{
								snprintf(filename, sizeof(filename), "%s/OPTROM.%s.%u.BIN", ROMpath, is_XT ? "XT" : "AT", i); //Create the filename for the ROM for the architecture!
								if (!file_exists(filename)) //This version doesn't exist? Then try the other version!
								{
									snprintf(filename, sizeof(filename), "%s/OPTROM.%s.%u.BIN", ROMpath, "XT", i); //Create the filename for the ROM for the architecture!
									if (!file_exists(filename)) //This version doesn't exist? Then try the other version!
									{
										snprintf(filename, sizeof(filename), "%s/OPTROM.%u.BIN", ROMpath, i); //Create the filename for the ROM!
									}
								}
							}
						}
					}
				}
			}
		}
		else
		{
			ISVGA = 0; //No VGA!
			if (BIOS_Settings.VGA_Mode==4) //Pure CGA?
			{
				scanROM("CGAROM",&filename[0],sizeof(filename)); //Scan for a CGA ROM!
			}
			else if (BIOS_Settings.VGA_Mode==5) //Pure MDA?
			{
				scanROM("MDAROM",&filename[0],sizeof(filename)); //Scan for a MDA ROM!
			}
			else
			{	
				ISVGA = 1; //We're a VGA!
				if (BIOS_Settings.VGA_Mode==6) //ET4000?
				{
					if (BIOS_Settings.ET4000_extensions) //W32 variant?
					{
						scanROM("ET4000_W32", &filename[0], sizeof(filename)); //Scan for a ET4000/W32 ROM!
					}
					else //Normal ET4000AX?
					{
						scanROM("ET4000", &filename[0], sizeof(filename)); //Scan for a ET4000 ROM!
					}
					//Provide emulator fallback support!
					if (file_exists(filename)) //Full ET4000?
					{
						ISVGA = 2; //ET4000!
						//ET4000 ROM!
					}
					else //VGA ROM?
					{
						safestrcpy(filename,sizeof(filename), ""); //VGA ROM!
					}
				}
				else if (BIOS_Settings.VGA_Mode == 7) //ET3000?
				{
					scanROM("ET3000",&filename[0],sizeof(filename)); //Scan for a ET3000 ROM!
					//Provide emulator fallback support!
					if (file_exists(filename)) //Full ET3000?
					{
						ISVGA = 3; //ET3000!
						//ET3000 ROM!
					}
					else //VGA ROM?
					{
						safestrcpy(filename,sizeof(filename), ""); //VGA ROM!
					}
				}
				else if (BIOS_Settings.VGA_Mode == 8) //EGA?
				{
					scanROM("EGAROM",&filename[0],sizeof(filename)); //Scan for a EGA ROM!
					//Provide emulator fallback support!
					if (file_exists(filename)) //Full EGA?
					{
						ISVGA = 4; //EGA!
						//EGA ROM!
					}
					else //VGA ROM?
					{
						safestrcpy(filename,sizeof(filename), ""); //VGA ROM!
					}
				}
				else //Plain VGA?
				{
					scanROM("VGAROM",&filename[0],sizeof(filename)); //Scan for a VGA ROM!
				}
			}
		}
		if (strcmp(filename,"")==0) //No ROM?
		{
			f = NULL; //No ROM!
		}
		else //Try to open!
		{
			f = emufopen64(filename,"rb");
		}
		if (!f)
		{
			if (!i) //First ROM is reserved by the VGA BIOS ROM. If not found, we're skipping it and using the internal VGA BIOS!
			{
				if (ISVGA) //Are we the VGA ROM?
				{
					location = sizeof(EMU_VGAROM); //Allocate the Emulator VGA ROM for the first entry instead!
					BIOS_load_VGAROM(); //Load the BIOS VGA ROM!
				}
			}
			continue; //Failed to load!
		}
		emufseek64(f,0,SEEK_END); //Goto EOF!
		if (emuftell64(f)) //Gotten size?
		{
			OPTROM_size[numOPT_ROMS] = (uint_32)emuftell64(f); //Save the size!
			emufseek64(f,0,SEEK_SET); //Goto BOF!
			if ((location+OPTROM_size[numOPT_ROMS])>0x20000) //Overflow?
			{
				if (!i) //First ROM is reserved by the VGA BIOS ROM. If not found, we're skipping it and using the internal VGA BIOS!
				{
					if (!((sizeof(EMU_VGAROM) + 0xC0000) > BIOSROM_BASE_XT)) //Not more than we can handle?
					{
						location = sizeof(EMU_VGAROM); //Allocate the Emulator VGA ROM for the first entry instead!
						BIOS_load_VGAROM(); //Load the BIOS VGA ROM!
					}
				}
				BIOS_ROM_size[numOPT_ROMS] = 0; //Reset!
				continue; //We're skipping this ROM: it's too big!
			}
			OPT_ROMS[numOPT_ROMS] = (byte *)nzalloc(OPTROM_size[numOPT_ROMS],"OPTROM",getLock(LOCK_CPU)); //Simple memory allocation for our ROM!
			if (!OPT_ROMS[numOPT_ROMS]) //Failed to allocate?
			{
				failedallocationOPTROMshadow: //Failed allocation of the shadow otpion ROM?
				emufclose64(f); //Close the file!
				if (!i) //First ROM is reserved by the VGA BIOS ROM. If not found, we're skipping it and using the internal VGA BIOS!
				{
					if (!((sizeof(EMU_VGAROM) + 0xC0000) > BIOSROM_BASE_XT)) //Not more than we can handle?
					{
						unlock(LOCK_CPU);
						freez((void **)&OPT_ROMS[numOPT_ROMS], OPTROM_size[numOPT_ROMS], "OPTROM"); //Release the ROM!
						lock(LOCK_CPU);
						location = sizeof(EMU_VGAROM); //Allocate the Emulator VGA ROM for the first entry instead!
						BIOS_load_VGAROM(); //Load the BIOS VGA ROM!
					}
				}
				continue; //Failed to allocate!
			}
			if ((ISVGA == 4) && (!i)) //EGA ROM?
			{
				OPT_ROMS_shadow[numOPT_ROMS] = (byte*)nzalloc(OPTROM_size[numOPT_ROMS], "OPTROM", getLock(LOCK_CPU)); //Simple memory allocation for our ROM!
				if (!OPT_ROMS_shadow[numOPT_ROMS]) //Failed to allocate a shadow ROM?
				{
					goto failedallocationOPTROMshadow;
				}
			}
			else
			{
				OPT_ROMS_shadow[numOPT_ROMS] = NULL; //No shadow ROM!
			}
			if (emufread64(OPT_ROMS[numOPT_ROMS],1,OPTROM_size[numOPT_ROMS],f)!=OPTROM_size[numOPT_ROMS]) //Not fully read?
			{
				freez((void **)&OPT_ROMS[numOPT_ROMS],OPTROM_size[numOPT_ROMS],"OPTROM"); //Failed to read!
				emufclose64(f); //Close the file!
				if (!i) //First ROM is reserved by the VGA BIOS ROM. If not found, we're skipping it and using the internal VGA BIOS!
				{
					if (!((sizeof(EMU_VGAROM) + 0xC0000) > BIOSROM_BASE_XT)) //Not more than we can handle?
					{
						unlock(LOCK_CPU);
						freez((void **)&OPT_ROMS[numOPT_ROMS], OPTROM_size[numOPT_ROMS], "OPTROM"); //Release the ROM!
						lock(LOCK_CPU);
						location = sizeof(EMU_VGAROM); //Allocate the Emulator VGA ROM for the first entry instead!
						BIOS_load_VGAROM(); //Load the BIOS VGA ROM!
					}
				}
				continue; //Failed to read!
			}
			if (OPT_ROMS_shadow[numOPT_ROMS]) //Shadow ROM to be used(reversed)?
			{
				for (datapos = 0; datapos < OPTROM_size[numOPT_ROMS]; ++datapos) //Process the entire ROM!
				{
					OPT_ROMS_shadow[numOPT_ROMS][datapos] = OPT_ROMS[numOPT_ROMS][OPTROM_size[numOPT_ROMS] - datapos - 1]; //Reversed ROM!
				}
			}
			emufclose64(f); //Close the file!
			
			OPTROM_location[numOPT_ROMS] = location; //The option ROM location we're loaded at!
			cleardata(&OPTROM_filename[numOPT_ROMS][0],sizeof(OPTROM_filename[numOPT_ROMS])); //Init filename!
			safestrcpy(OPTROM_filename[numOPT_ROMS],sizeof(OPTROM_filename[0]),filename); //Save the filename of the loaded ROM for writing to it, as well as releasing it!

			location += OPTROM_size[numOPT_ROMS]; //Next ROM position!
			OPTROM_location[numOPT_ROMS] |= ((uint_64)location<<32); //The end location of the option ROM!
			if (OPTROM_size[numOPT_ROMS]&0x7FF) //Not 2KB alligned?
			{
				location += 0x800-(OPTROM_size[numOPT_ROMS]&0x7FF); //2KB align!
			}
			++numOPT_ROMS; //We've loaded this many ROMS!
			if ((location+0xC0000) > BIOSROM_BASE_XT) //More ROMs loaded than we can handle?
			{
				--numOPT_ROMS; //Unused option ROM!
				location = (OPTROM_location[numOPT_ROMS]&0xFFFFFFFFU); //Reverse to start next ROM(s) at said location again!
				unlock(LOCK_CPU);
				freez((void **)&OPT_ROMS[numOPT_ROMS],OPTROM_size[numOPT_ROMS], "OPTROM"); //Release the ROM!
				lock(LOCK_CPU);
			}
			continue; //Loaded!
		}
		
		emufclose64(f);
	}
	return 1; //Just run always!
}

void BIOS_freeOPTROMS()
{
	byte i;
	for (i=0;i<NUMITEMS(OPT_ROMS);i++)
	{
		if (OPT_ROMS[i]) //Loaded?
		{
			char filename[256];
			memset(&filename,0,sizeof(filename)); //Clear/init!
			safestrcpy(filename,sizeof(filename),OPTROM_filename[i]); //Set the filename from the loaded ROM!
			freez((void **)&OPT_ROMS[i],OPTROM_size[i],"OPTROM"); //Release the OPT ROM!
			if (OPT_ROMS_shadow[i]) //Shadow also loaded?
			{
				freez((void**)&OPT_ROMS_shadow[i], OPTROM_size[i], "OPTROM"); //Release the OPT ROM shadow!
			}
		}
	}
}

byte BIOS_ROM_type = 0;
uint_32 BIOS_ROM_U13_15_double = 0, BIOS_ROM_U13_15_single = 0;

#define BIOSROMTYPE_INVALID 0
#define BIOSROMTYPE_U18_19 1
#define BIOSROMTYPE_U34_35 2
#define BIOSROMTYPE_U27_47 3
#define BIOSROMTYPE_U13_15 4

int BIOS_load_ROM(byte nr)
{
	uint_32 ROMdst, ROMsrc;
	byte srcROM;
	byte tryext = 0; //Try extra ROMs for different architectures?
	uint_32 ROM_size=0; //The size of both ROMs!
	BIGFILE *f;
	char filename[100];
	memset(&filename,0,sizeof(filename)); //Clear/init!
retryext:
	if ((tryext&7)<=3) //32-bit ROM available?
	{
		if (EMULATED_CPU<CPU_80386) //Unusable CPU for 32-bit code?
		{
			++tryext; //Next try!
			goto retryext; //Skip 32-bit ROMs!
		}
		if ((!tryext) && (EMULATED_CPU < CPU_PENTIUM)) //Unusable CPU for this architecture?
		{
			++tryext; //Next try!
			goto retryext; //Skip Pentium-only ROMs!
		}
	}
	switch (tryext&7)
	{
	case 0: //i430fx?
		if (is_i430fx == 0) //Not a i430fx compatible architecture?
		{
			++tryext; //Next try!
			goto retryext; //Skip PS/2 ROMs!
		}
		if (is_i430fx == 1) //i430fx?
		{
			if (BIOS_Settings.BIOSROMmode == BIOSROMMODE_DIAGNOSTICS) //Diagnostics mode?
			{
				snprintf(filename, sizeof(filename), "%s/BIOSROM.i430fx.U%u.DIAGNOSTICS.BIN", ROMpath, nr); //Create the filename for the ROM!		
			}
			else //Normal mode?
			{
				snprintf(filename, sizeof(filename), "%s/BIOSROM.i430fx.U%u.BIN", ROMpath, nr); //Create the filename for the ROM!		
			}
		}
		else //i440fx?
		{
			if (BIOS_Settings.BIOSROMmode == BIOSROMMODE_DIAGNOSTICS) //Diagnostics mode?
			{
				snprintf(filename, sizeof(filename), "%s/BIOSROM.i440fx.U%u.DIAGNOSTICS.BIN", ROMpath, nr); //Create the filename for the ROM!		
			}
			else //Normal mode?
			{
				snprintf(filename, sizeof(filename), "%s/BIOSROM.i440fx.U%u.BIN", ROMpath, nr); //Create the filename for the ROM!		
			}
		}
		break;
	case 1: //PS/2?
		if (is_PS2 == 0) //Not a PS/2 compatible architecture?
		{
			++tryext; //Next try!
			goto retryext; //Skip PS/2 ROMs!
		}
		if (BIOS_Settings.BIOSROMmode == BIOSROMMODE_DIAGNOSTICS) //Diagnostics mode?
		{
			snprintf(filename, sizeof(filename), "%s/BIOSROM.PS2.U%u.DIAGNOSTICS.BIN", ROMpath, nr); //Create the filename for the ROM!		
		}
		else //Normal mode?
		{
			snprintf(filename, sizeof(filename), "%s/BIOSROM.PS2.U%u.BIN", ROMpath, nr); //Create the filename for the ROM!		
		}
		break;
	case 2: //32-bit?
		if (!(is_Compaq || is_i430fx || is_PS2)) //Not a 32-bit compatible architecture?
		{
			++tryext; //Next try!
			goto retryext; //Skip 32-bit ROMs!
		}

		if (BIOS_Settings.BIOSROMmode == BIOSROMMODE_DIAGNOSTICS) //Diagnostics mode?
		{
			snprintf(filename, sizeof(filename), "%s/BIOSROM.32.U%u.DIAGNOSTICS.BIN", ROMpath, nr); //Create the filename for the ROM!		
		}
		else //Normal mode?
		{
			snprintf(filename, sizeof(filename), "%s/BIOSROM.32.U%u.BIN", ROMpath, nr); //Create the filename for the ROM!		
		}
		break;
	case 3: //Compaq?
		if (is_Compaq==0) //Not a Compaq compatible architecture?
		{
			++tryext; //Next try!
			goto retryext; //Skip 32-bit ROMs!
		}

		if (BIOS_Settings.BIOSROMmode == BIOSROMMODE_DIAGNOSTICS) //Diagnostics mode?
		{
			snprintf(filename, sizeof(filename), "%s/BIOSROM.COMPAQ.U%u.DIAGNOSTICS.BIN", ROMpath, nr); //Create the filename for the ROM!		
		}
		else //Normal mode?
		{
			snprintf(filename, sizeof(filename), "%s/BIOSROM.COMPAQ.U%u.BIN", ROMpath, nr); //Create the filename for the ROM!		
		}
		break;
	case 4: //AT?
		if (is_XT) //Not a AT compatible architecture?
		{
			++tryext; //Next try!
			goto retryext; //Skip PS/2 ROMs!
		}
		if (BIOS_Settings.BIOSROMmode == BIOSROMMODE_DIAGNOSTICS) //Diagnostics mode?
		{
			snprintf(filename, sizeof(filename), "%s/BIOSROM.AT.U%u.DIAGNOSTICS.BIN", ROMpath, nr); //Create the filename for the ROM!
		}
		else
		{
			snprintf(filename, sizeof(filename), "%s/BIOSROM.AT.U%u.BIN", ROMpath, nr); //Create the filename for the ROM!
		}
		break;
	case 5: //XT?
		if (BIOS_Settings.BIOSROMmode == BIOSROMMODE_DIAGNOSTICS) //Diagnostics mode?
		{
			snprintf(filename, sizeof(filename), "%s/BIOSROM.XT.U%u.DIAGNOSTICS.BIN", ROMpath, nr); //Create the filename for the ROM!
		}
		else
		{
			snprintf(filename, sizeof(filename), "%s/BIOSROM.XT.U%u.BIN", ROMpath, nr); //Create the filename for the ROM!
		}
		break;
	default:
	case 6: //Universal ROM?
		if (BIOS_Settings.BIOSROMmode == BIOSROMMODE_DIAGNOSTICS) //Diagnostics mode?
		{
			snprintf(filename, sizeof(filename), "%s/BIOSROM.U%u.DIAGNOSTICS.BIN", ROMpath, nr); //Create the filename for the ROM!
		}
		else
		{
			snprintf(filename, sizeof(filename), "%s/BIOSROM.U%u.BIN", ROMpath, nr); //Create the filename for the ROM!
		}
		break;
	}
	f = emufopen64(filename,"rb");
	if (!f)
	{
		++tryext; //Try second time and onwards!
		if (tryext<=6) //Extension try valid to be tried?
		{
			goto retryext;
		}
		return 0; //Failed to load!
	}
	emufseek64(f,0,SEEK_END); //Goto EOF!
	if (emuftell64(f)) //Gotten size?
 	{
		BIOS_ROM_size[nr] = (uint_32)emuftell64(f); //Save the size!
		emufseek64(f,0,SEEK_SET); //Goto BOF!
		BIOS_ROMS[nr] = (byte *)nzalloc(BIOS_ROM_size[nr],"BIOSROM", getLock(LOCK_CPU)); //Simple memory allocation for our ROM!
		if (!BIOS_ROMS[nr]) //Failed to allocate?
		{
			emufclose64(f); //Close the file!
			return 0; //Failed to allocate!
		}
		if (emufread64(BIOS_ROMS[nr],1,BIOS_ROM_size[nr],f)!=BIOS_ROM_size[nr]) //Not fully read?
		{
			freez((void **)&BIOS_ROMS[nr],BIOS_ROM_size[nr],"BIOSROM"); //Failed to read!
			emufclose64(f); //Close the file!
			return 0; //Failed to read!
		}
		emufclose64(f); //Close the file!

		BIOS_ROMS_ext[nr] = ((BIOS_Settings.BIOSROMmode==BIOSROMMODE_DIAGNOSTICS)?8:0)|(tryext&7); //Extension enabled?

		switch (nr) //What ROM has been loaded?
		{
			case 18:
			case 19: //u18/u19 chips?
				if (BIOS_ROMS[18] && BIOS_ROMS[19]) //Both loaded?
				{
					BIOS_ROM_type = BIOSROMTYPE_U18_19; //u18/19 combo!
					ROM_size = BIOS_ROM_size[18]+BIOS_ROM_size[19]; //ROM size!
				}
				else
				{
					BIOS_ROM_type = BIOSROMTYPE_INVALID; //Invalid!
					ROM_size = 0; //No ROM!
				}
				break;
			case 34:
			case 35: //u34/u35 chips?
				if (BIOS_ROMS[34] && BIOS_ROMS[35]) //Both loaded?
				{
					BIOS_ROM_type = BIOSROMTYPE_U34_35; //u34/35 combo!
					ROM_size = BIOS_ROM_size[34]+BIOS_ROM_size[35]; //ROM size!
					//Preprocess the ROM into a linear one instead of interleaved!
					BIOS_combinedROM = (byte*)zalloc(ROM_size, "BIOS_combinedROM", getLock(LOCK_CPU));
					if (!BIOS_combinedROM) //Failed to allocate?
					{
						freez((void**)&BIOS_ROMS[nr], BIOS_ROM_size[nr], "BIOSROM"); //Failed to read!
						return 0; //Abort!
					}
					BIOS_ROMS_ext[nr] |= 0x10; //Tell we're using an combined ROM!
					BIOS_combinedROM_size = ROM_size; //The size of the combined ROM!
					//Combined ROM allocated?
					srcROM = 34; //The first byte is this ROM!
					ROMdst = ROMsrc = 0; //Init!
					for (; ROMdst < ROM_size;) //Process the entire ROM!
					{
						BIOS_combinedROM[ROMdst++] = BIOS_ROMS[srcROM][ROMsrc]; //Take a byte from the source ROM!
						srcROM = (srcROM == 34) ? 35 : 34; //Toggle the src ROM!
						if (srcROM == 34) ++ROMsrc; //Next position when two ROMs have been processed!
					}
				}
				else
				{
					BIOS_ROM_type = BIOSROMTYPE_INVALID; //Invalid!
					ROM_size = 0; //No ROM!
				}
				break;
			case 27:
			case 47: //u27/u47 chips?
				if (BIOS_ROMS[27] && BIOS_ROMS[47]) //Both loaded?
				{
					BIOS_ROM_type = BIOSROMTYPE_U27_47; //u27/47 combo!
					ROM_size = BIOS_ROM_size[27]+BIOS_ROM_size[47]; //ROM size!
					//Preprocess the ROM into a linear one instead of interleaved!
					BIOS_combinedROM = (byte*)zalloc(ROM_size, "BIOS_combinedROM", getLock(LOCK_CPU));
					if (!BIOS_combinedROM) //Failed to allocate?
					{
						freez((void**)&BIOS_ROMS[nr], BIOS_ROM_size[nr], "BIOSROM"); //Failed to read!
						return 0; //Abort!
					}
					BIOS_ROMS_ext[nr] |= 0x10; //Tell we're using an combined ROM!
					BIOS_combinedROM_size = ROM_size; //The size of the combined ROM!
					//Combined ROM allocated?
					srcROM = 27; //The first byte is this ROM!
					ROMdst = ROMsrc = 0; //Init!
					for (; ROMdst < ROM_size;) //Process the entire ROM!
					{
						BIOS_combinedROM[ROMdst++] = BIOS_ROMS[srcROM][ROMsrc]; //Take a byte from the source ROM!
						srcROM = (srcROM == 27) ? 47 : 27; //Toggle the src ROM!
						if (srcROM == 27) ++ROMsrc; //Next position when two ROMs have been processed!
					}
				}
				else
				{
					BIOS_ROM_type = BIOSROMTYPE_INVALID; //Invalid!
					ROM_size = 0; //No ROM!
				}
				break;
			case 13:
			case 15: //u13/u15 chips?
				if (BIOS_ROMS[13] && BIOS_ROMS[15]) //Both loaded?
				{
					BIOS_ROM_type = BIOSROMTYPE_U13_15; //u13/15 combo!
					ROM_size = (BIOS_ROM_size[13]+BIOS_ROM_size[15])<<1; //ROM size! The ROM is doubled in RAM(duplicated twice)
					BIOS_ROM_U13_15_double = ROM_size; //Save the loaded ROM size for easier processing!
					BIOS_ROM_U13_15_single = ROM_size>>1; //Half the ROM for easier lookup!
					//Preprocess the ROM into a linear one instead of interleaved!
					BIOS_combinedROM = (byte*)zalloc(BIOS_ROM_U13_15_single, "BIOS_combinedROM", getLock(LOCK_CPU));
					if (!BIOS_combinedROM) //Failed to allocate?
					{
						freez((void**)&BIOS_ROMS[nr], BIOS_ROM_size[nr], "BIOSROM"); //Failed to read!
						return 0; //Abort!
					}
					BIOS_ROMS_ext[nr] |= 0x10; //Tell we're using an combined ROM!
					BIOS_combinedROM_size = BIOS_ROM_U13_15_single; //The size of the combined ROM!
					//Combined ROM allocated?
					srcROM = 13; //The first byte is this ROM!
					ROMdst = ROMsrc = 0;
					for (; ROMdst < BIOS_combinedROM_size;) //Process the entire ROM!
					{
						BIOS_combinedROM[ROMdst++] = BIOS_ROMS[srcROM][ROMsrc]; //Take a byte from the source ROM!
						srcROM = (srcROM == 13) ? 15 : 13; //Toggle the src ROM!
						if (srcROM == 13) ++ROMsrc; //Next position when two ROMs have been processed!
					}
				}
				else
				{
					BIOS_ROM_type = BIOSROMTYPE_INVALID; //Invalid!
					ROM_size = 0; //No ROM!
					BIOS_ROM_U13_15_double = 0; //Save the loaded ROM size for easier processing!
					BIOS_ROM_U13_15_single = 0; //Half the ROM for easier lookup!
				}
				break;
			default:
				break;
		}
		
		//Recalculate based on ROM size!
		BIOSROM_BASE_AT = 0xFFFFFFU-(MIN(ROM_size,0x100000U)-1U); //AT ROM size! Limit to 1MB!
		BIOSROM_BASE_XT = 0xFFFFFU-(MIN(ROM_size,(is_XT?0x10000U:(is_Compaq?0x40000U:0x20000U)))-1U); //XT ROM size! Limit to 256KB(Compaq, but not i430fx(which acts like the AT)), 128KB(AT) or 64KB(XT)!
		BIOSROM_BASE_Modern = 0xFFFFFFFFU-(ROM_size-1U); //Modern ROM size!
		return 1; //Loaded!
	}
	
	emufclose64(f);
	return 0; //Failed to load!
}

//Custom loaded BIOS ROM (single only)!
byte *BIOS_custom_ROM;
uint_32 BIOS_custom_ROM_size;
char customROMname[256]; //Custom ROM name!
byte ROM_doubling = 0; //Double the ROM?

int BIOS_load_custom(char *path, char *rom)
{
	BIGFILE *f;
	char filename[256];
	memset(&filename,0,sizeof(filename)); //Clear/init!
	if (!path)
	{
		safestrcpy(filename,sizeof(filename),ROMpath); //Where to find our ROM!
	}
	else
	{
		safestrcpy(filename,sizeof(filename), path); //Where to find our ROM!
	}
	if (strcmp(filename, "") != 0) safestrcat(filename,sizeof(filename), "/"); //Only a seperator when not empty!
	safestrcat(filename,sizeof(filename),rom); //Create the filename for the ROM!
	f = emufopen64(filename,"rb");
	if (!f)
	{
		return 0; //Failed to load!
	}
	emufseek64(f,0,SEEK_END); //Goto EOF!
	if (emuftell64(f)) //Gotten size?
 	{
		BIOS_custom_ROM_size = (uint_32)emuftell64(f); //Save the size!
		emufseek64(f,0,SEEK_SET); //Goto BOF!
		BIOS_custom_ROM = (byte *)nzalloc(BIOS_custom_ROM_size,"BIOSROM", getLock(LOCK_CPU)); //Simple memory allocation for our ROM!
		if (!BIOS_custom_ROM) //Failed to allocate?
		{
			emufclose64(f); //Close the file!
			return 0; //Failed to allocate!
		}
		if (emufread64(BIOS_custom_ROM,1,BIOS_custom_ROM_size,f)!=BIOS_custom_ROM_size) //Not fully read?
		{
			freez((void **)&BIOS_custom_ROM,BIOS_custom_ROM_size,"BIOSROM"); //Failed to read!
			emufclose64(f); //Close the file!
			return 0; //Failed to read!
		}
		emufclose64(f); //Close the file!
		safestrcpy(customROMname,sizeof(customROMname),filename); //Custom ROM name for easy dealloc!
		//Update the base address to use for this CPU!
		ROM_doubling = 0; //Default: no ROM doubling!
		if (BIOS_custom_ROM_size<=0x8000) //Safe to double?
		{
			if (EMULATED_CPU>=CPU_80386 && (is_XT==0)) //We're to emulate a Compaq Deskpro 386?
			{
				ROM_doubling = 1; //Double the ROM!
			}
		}

		//Also limit the ROM base addresses accordingly(only last block).
		BIOSROM_BASE_AT = 0xFFFFFF-(MIN(BIOS_custom_ROM_size<<ROM_doubling,0x100000)-1); //AT ROM size!
		BIOSROM_BASE_XT = 0xFFFFF-(MIN(BIOS_custom_ROM_size<<ROM_doubling,(is_XT?0x10000U:(is_Compaq?0x40000U:0x20000U)))-1U); //XT ROM size! XT has a 64K limit(0xF0000 min) because of the EMS mapped at 0xE0000(64K), while AT and up has 128K limit(0xE0000) because the memory is unused(no expansion board present, allowing all addresses to be used up to the end of the expansion ROM area(0xE0000). Compaq limits to 256KB instead(addresses from 0xC0000 and up)), while newer motherboards act like the AT(only 128KB).
		BIOSROM_BASE_Modern = 0xFFFFFFFF-(MIN(BIOS_custom_ROM_size<<ROM_doubling,0x10000000)-1); //Modern ROM size!
		return 1; //Loaded!
	}
	
	emufclose64(f);
	return 0; //Failed to load!
}


void BIOS_free_ROM(byte nr)
{
	char filename[100];
	memset(&filename,0,sizeof(filename)); //Clear/init!
	switch (BIOS_ROMS_ext[nr]&7) //ROM type?
	{
	case 0: //PS/2 ROM?
			if (BIOS_ROMS_ext[nr] & 8) //Diagnostic ROM?
			{
				snprintf(filename, sizeof(filename), "BIOSROM.PS2.U%u.DIAGNOSTICS.BIN", nr); //Create the filename for the ROM!
			}
			else //Normal ROM?
			{
				snprintf(filename, sizeof(filename), "BIOSROM.PS2.U%u.BIN", nr); //Create the filename for the ROM!
			}
			break;
	case 1: //32-bit ROM?
			if (BIOS_ROMS_ext[nr] & 8) //Diagnostic ROM?
			{
				snprintf(filename, sizeof(filename), "BIOSROM.32.U%u.DIAGNOSTICS.BIN", nr); //Create the filename for the ROM!
			}
			else //Normal ROM?
			{
				snprintf(filename, sizeof(filename), "BIOSROM.32.U%u.BIN", nr); //Create the filename for the ROM!
			}
			break;
	case 2: //Compaq ROM?
			if (BIOS_ROMS_ext[nr] & 8) //Diagnostic ROM?
			{
				snprintf(filename, sizeof(filename), "BIOSROM.COMPAQ.U%u.DIAGNOSTICS.BIN", nr); //Create the filename for the ROM!
			}
			else //Normal ROM?
			{
				snprintf(filename, sizeof(filename), "BIOSROM.COMPAQ.U%u.BIN", nr); //Create the filename for the ROM!
			}
			break;
	case 3: //AT ROM?
			if (BIOS_ROMS_ext[nr]&8) //Diagnostic ROM?
			{
				snprintf(filename,sizeof(filename),"BIOSROM.AT.U%u.DIAGNOSTICS.BIN",nr); //Create the filename for the ROM!
			}
			else //Normal ROM?
			{
				snprintf(filename,sizeof(filename),"BIOSROM.AT.U%u.BIN",nr); //Create the filename for the ROM!
			}
	case 4: //XT ROM?
			if (BIOS_ROMS_ext[nr]&8) //Diagnostic ROM?
			{
				snprintf(filename,sizeof(filename),"BIOSROM.XT.U%u.DIAGNOSTICS.BIN",nr); //Create the filename for the ROM!
			}
			else //Normal ROM?
			{
				snprintf(filename,sizeof(filename),"BIOSROM.XT.U%u.BIN",nr); //Create the filename for the ROM!
			}
			break;
	default:
	case 5: //Universal ROM?
			if (BIOS_ROMS_ext[nr]&8) //Diagnostic ROM?
			{
				snprintf(filename,sizeof(filename),"BIOSROM.U%u.DIAGNOSTICS.BIN",nr); //Create the filename for the ROM!
			}
			else //Normal ROM?
			{
				snprintf(filename,sizeof(filename),"BIOSROM.U%u.BIN",nr); //Create the filename for the ROM!
			}
			break;
	}
	if (BIOS_ROM_size[nr]) //Has size?
	{
		if (BIOS_ROMS_ext[nr] & 0x10) //Needs freeing of the combined ROM as well?
		{
			freez((void **)&BIOS_combinedROM,BIOS_combinedROM_size,"BIOS_combinedROM"); //Free the combined ROM!
		}
		freez((void **)&BIOS_ROMS[nr],BIOS_ROM_size[nr],"BIOSROM"); //Release the BIOS ROM!
	}
}

void BIOS_free_custom(char *rom)
{
	char filename[256];
	memset(&filename,0,sizeof(filename)); //Clear/init!
	if (rom==NULL) //NULL ROM (Autodetect)?
	{
		rom = &customROMname[0]; //Use custom ROM name!
	}
	safestrcpy(filename,sizeof(filename),rom); //Create the filename for the ROM!
	if (BIOS_custom_ROM_size) //Has size?
	{
		freez((void **)&BIOS_custom_ROM,BIOS_custom_ROM_size,"BIOSROM"); //Release the BIOS ROM!
	}
	BIOS_custom_ROM = NULL; //No custom ROM anymore!
}

int BIOS_load_systemROM() //Load custom ROM from emulator itself!
{
	BIOS_free_custom(NULL); //Free the custom ROM, if needed and known!
	BIOS_custom_ROM_size = sizeof(EMU_BIOS); //Save the size!
	BIOS_custom_ROM = &EMU_BIOS[0]; //Simple memory allocation for our ROM!
	BIOSROM_BASE_AT = 0xFFFFFF-(BIOS_custom_ROM_size-1); //AT ROM size!
	BIOSROM_BASE_XT = 0xFFFFF-(BIOS_custom_ROM_size-1); //XT ROM size!
	BIOSROM_BASE_Modern = 0xFFFFFFFF-(BIOS_custom_ROM_size-1); //Modern ROM size!
	return 1; //Loaded!
}

void BIOS_free_systemROM()
{
	BIOS_free_custom(NULL); //Free the custom ROM, if needed and known!
}

void BIOS_DUMPSYSTEMROM() //Dump the SYSTEM ROM currently set (debugging purposes)!
{
	char path[256];
	if (BIOS_custom_ROM == &EMU_BIOS[0]) //We're our own BIOS?
	{
		memset(&path,0,sizeof(path));
		safestrcpy(path,sizeof(path),ROMpath); //Current ROM path!
		safestrcat(path,sizeof(path),"/SYSROM.DMP.BIN"); //Dump path!
		//Dump our own BIOS ROM!
		BIGFILE *f;
		f = emufopen64(path, "wb");
		emufwrite64(&EMU_BIOS, 1, sizeof(EMU_BIOS), f); //Save our BIOS!
		emufclose64(f);
	}
}


//VGA support!

byte *BIOS_custom_VGAROM;
uint_32 BIOS_custom_VGAROM_size;
char customVGAROMname[256] = "EMU_VGAROM"; //Custom ROM name!
byte VGAROM_mapping = 0xFF; //Default: all mapped in!

void BIOS_free_VGAROM()
{
	if (BIOS_custom_VGAROM_size) //Has size?
	{
		freez((void **)&BIOS_custom_VGAROM, BIOS_custom_VGAROM_size, "EMU_VGAROM"); //Release the BIOS ROM!
	}
}

int BIOS_load_VGAROM() //Load custom ROM from emulator itself!
{
	BIOS_free_VGAROM(); //Free the custom ROM, if needed and known!
	BIOS_custom_VGAROM_size = sizeof(EMU_VGAROM); //Save the size!
	BIOS_custom_VGAROM = (byte *)&EMU_VGAROM; //Simple memory allocation for our ROM!
	return 1; //Loaded!
}

void BIOS_finishROMs()
{
	uint_32 counter;
	if (BIOS_custom_ROM != &EMU_BIOS[0]) //Custom BIOS ROM loaded?
	{
		BIOS_free_custom(NULL); //Free the custom ROM!
	}
	if (BIOS_custom_VGAROM != &EMU_VGAROM[0]) //Custom VGA ROM loaded?
	{
		BIOS_free_VGAROM(); //Free the VGA ROM!
	}
	//Now, process all normal ROMs!
	for (counter = 0; counter < 0x100; ++counter)
	{
		BIOS_free_ROM(counter); //Free said ROM, if loaded!
	}
	BIOS_freeOPTROMS(); //Free all option ROMs!
}

byte BIOSROM_DisableLowMemory = 0; //Disable low-memory mapping of the BIOS and OPTROMs! Disable mapping of low memory locations E0000-FFFFF used on the Compaq Deskpro 386.

extern uint_64 memory_dataread[2];
extern byte memory_datasize[2]; //The size of the data that has been read!
byte OPTROM_readhandler(uint_32 offset, byte index)    /* A pointer to a handler function */
{
	byte* srcROM;
	uint_32 ROMsize;
	INLINEREGISTER uint_64 basepos, currentpos, temppos, temp; //Current position!
	basepos = currentpos = offset; //Load the offset!
	if (unlikely((basepos >= 0xC0000) && (basepos<0xF0000))) basepos = 0xC0000; //Our base reference position!
	else //Out of range (16-bit)?
	{
		if (unlikely((basepos >= 0xC0000000) && (basepos < 0xF0000000))) basepos = 0xC0000000; //Our base reference position!
		else return 0; //Our of range (32-bit)?
	}
	currentpos -= basepos; //Calculate from the base position!
	if (unlikely((offset>=0xE0000) && (offset<=0xFFFFF) && (BIOSROM_DisableLowMemory))) return 0; //Disabled for Compaq RAM!
	basepos = currentpos; //Save a backup!
	INLINEREGISTER byte i=0,j=numOPT_ROMS;
	if (unlikely(!numOPT_ROMS)) goto noOPTROMSR;
	do //Check OPT ROMS!
	{
		currentpos = OPTROM_location[i]; //Load the current location for analysis and usage!
		ROMsize = (currentpos>>32); //Save ROM end location!
		if (likely(OPT_ROMS[i] && (ROMsize>basepos))) //Before the end location and valid rom?
		{
			currentpos &= 0xFFFFFFFF; //The location of the ROM itself!
			ROMsize -= (uint_32)currentpos; //Convert ROMsize to the actual ROM size to use!
			if (likely(currentpos <= basepos)) //At/after the start location? We've found the ROM!
			{
				temppos = basepos-currentpos; //Calculate the offset within the ROM!
				if ((VGAROM_mapping!=0xFF) && (i==0)) //Special mapping for the VGA-reserved ROM?
				{
					switch (VGAROM_mapping) //What special mapping?
					{
						case 0: //C000-C3FF enabled
							if (temppos>=0x4000) return 0; //Unmapped!
							break;
						case 1: //ROM disabled (ET3K/4K-AX), C000-C5FFF(ET3K/4K-AF)
							return 0; //Disable the ROM!
						case 2: //C000-C5FF, C680-C7FF Enabled
							if (((temppos>=0x6000) && (temppos<0x6800)) || (temppos>=0x8000)) return 0; //Unmapped in the mid-range!
							break;
						case 3: //C000-C7FF Enabled
							if (temppos>=0x8000) return 0; //Disabled!
							break;
						default: //Don't handle specially?
							break;
					}
				}
				srcROM = &OPT_ROMS[i][0]; //Default source ROM!
				if ((ISVGA==4) && (i==0)) //EGA ROM is reversed?
				{
					srcROM = &OPT_ROMS_shadow[i][0]; //Use the reversed option ROM!
				}
				#ifdef USE_MEMORY_CACHING
				if (likely((index & 3) == 0))
				{
					temp = temppos; //Backup address!
					temppos &= ~0xF; //Round down to the dword address!
					if (likely(((temppos | 0xF) < ROMsize))) //Enough to read a dword?
					{
						memory_dataread[0] = SDL_SwapLE64(*((uint_64*)(&srcROM[temppos]))); //Read the data from the ROM!
						memory_dataread[1] = SDL_SwapLE64(*((uint_64*)(&srcROM[temppos+8]))); //Read the data from the ROM!
						memory_datasize[(index >> 5) & 1] = temppos = 16 - (temp - temppos); //What is read from the whole dword!
						shiftr128(&memory_dataread[1],&memory_dataread[0],((16 - temppos) << 3)); //Discard the bytes that are not to be read(before the requested address)!
						return 1; //Done: we've been read!
					}
					else
					{
						temppos = temp; //Restore the original address!
						temppos &= ~7; //Round down to the dword address!
						if (likely(((temppos | 7) < ROMsize))) //Enough to read a dword?
						{
							memory_dataread[0] = SDL_SwapLE64(*((uint_64*)(&srcROM[temppos]))); //Read the data from the ROM!
							memory_datasize[(index >> 5) & 1] = temppos = 8 - (temp - temppos); //What is read from the whole dword!
							memory_dataread[0] >>= ((8 - temppos) << 3); //Discard the bytes that are not to be read(before the requested address)!
							return 1; //Done: we've been read!
						}
						else
						{
							temppos = temp; //Restore the original address!
							temppos &= ~3; //Round down to the dword address!
							if (likely(((temppos | 3) < ROMsize))) //Enough to read a dword?
							{
								memory_dataread[0] = SDL_SwapLE32(*((uint_32*)(&srcROM[temppos]))); //Read the data from the ROM!
								memory_datasize[(index >> 5) & 1] = temppos = 4 - (temp - temppos); //What is read from the whole dword!
								memory_dataread[0] >>= ((4 - temppos) << 3); //Discard the bytes that are not to be read(before the requested address)!
								return 1; //Done: we've been read!
							}
							else
							{
								temppos = temp; //Restore the original address!
								temppos &= ~1; //Round down to the word address!
								if (likely(((temppos | 1) < ROMsize))) //Enough to read a word, aligned?
								{
									memory_dataread[0] = SDL_SwapLE16(*((word*)(&srcROM[temppos]))); //Read the data from the ROM!
									memory_datasize[(index >> 5) & 1] = temppos = 2 - (temp - temppos); //What is read from the whole word!
									memory_dataread[0] >>= ((2 - temppos) << 3); //Discard the bytes that are not to be read(before the requested address)!
									return 1; //Done: we've been read!				
								}
								else //Enough to read a byte only?
								{
									memory_dataread[0] = srcROM[temp]; //Read the data from the ROM!
									memory_datasize[(index >> 5) & 1] = 1; //Only 1 byte!
									return 1; //Done: we've been read!				
								}
							}
						}
					}
				}
				else //Enough to read a byte only?
				#endif
				{
					memory_dataread[0] = OPT_ROMS[i][temppos]; //Read the data from the ROM, reversed!
					memory_datasize[(index >> 5) & 1] = 1; //Only 1 byte!
					return 1; //Done: we've been read!				
				}
			}
		}
		++i;
	} while (--j);
	noOPTROMSR:
	if (BIOS_custom_VGAROM_size) //Custom VGA ROM mounted?
	{
		if (likely(basepos < BIOS_custom_VGAROM_size)) //OK?
		{
			#ifdef USE_MEMORY_CACHING
			if (likely((index & 3) == 0))
			{
				temp = basepos; //Backup address!
				basepos &= ~0xF; //Round down to the dword address!
				if (likely(((basepos | 0xF) < BIOS_custom_VGAROM_size))) //Enough to read a dword?
				{
					memory_dataread[0] = SDL_SwapLE64(*((uint_64*)(&BIOS_custom_VGAROM[basepos]))); //Read the data from the ROM!
					memory_dataread[1] = SDL_SwapLE64(*((uint_64*)(&BIOS_custom_VGAROM[basepos+8]))); //Read the data from the ROM!
					memory_datasize[(index >> 5) & 1] = basepos = 16 - (temp - basepos); //What is read from the whole dword!
					shiftr128(&memory_dataread[1],&memory_dataread[0],((16 - basepos) << 3)); //Discard the bytes that are not to be read(before the requested address)!
					return 1; //Done: we've been read!
				}
				else
				{
					basepos = temp; //Restore the original address!
					basepos &= ~7; //Round down to the dword address!
					if (likely(((basepos | 7) < BIOS_custom_VGAROM_size))) //Enough to read a dword?
					{
						memory_dataread[0] = SDL_SwapLE64(*((uint_64*)(&BIOS_custom_VGAROM[basepos]))); //Read the data from the ROM!
						memory_datasize[(index >> 5) & 1] = basepos = 8 - (temp - basepos); //What is read from the whole dword!
						memory_dataread[0] >>= ((8 - basepos) << 3); //Discard the bytes that are not to be read(before the requested address)!
						return 1; //Done: we've been read!
					}
					else
					{
						basepos = temp; //Restore the original address!
						basepos &= ~3; //Round down to the dword address!
						if (likely(((basepos | 3) < BIOS_custom_VGAROM_size))) //Enough to read a dword?
						{
							memory_dataread[0] = SDL_SwapLE32(*((uint_32*)(&BIOS_custom_VGAROM[basepos]))); //Read the data from the ROM!
							memory_datasize[(index >> 5) & 1] = basepos = 4 - (temp - basepos); //What is read from the whole dword!
							memory_dataread[0] >>= ((4 - basepos) << 3); //Discard the bytes that are not to be read(before the requested address)!
							return 1; //Done: we've been read!
						}
						else
						{
							basepos = temp; //Restore the original address!
							basepos &= ~1; //Round down to the word address!
							if (likely(((basepos | 1) < BIOS_custom_VGAROM_size))) //Enough to read a word, aligned?
							{
								memory_dataread[0] = SDL_SwapLE16(*((word*)(&BIOS_custom_VGAROM[basepos]))); //Read the data from the ROM!
								memory_datasize[(index >> 5) & 1] = basepos = 2 - (temp - basepos); //What is read from the whole word!
								memory_dataread[0] >>= ((2 - basepos) << 3); //Discard the bytes that are not to be read(before the requested address)!
								return 1; //Done: we've been read!				
							}
							else //Enough to read a byte only?
							{
								memory_dataread[0] = BIOS_custom_VGAROM[temp]; //Read the data from the ROM!
								memory_datasize[(index >> 5) & 1] = 1; //Only 1 byte!
								return 1; //Done: we've been read!				
							}
						}
					}
				}
			}
			else //Enough to read a byte only?
			#endif
			{
				memory_dataread[0] = BIOS_custom_VGAROM[basepos]; //Read the data from the ROM, reversed!
				memory_datasize[(index >> 5) & 1] = 1; //Only 1 byte!
				return 1; //Done: we've been read!				
			}
			return 1;
		}
	}
	return 0; //No ROM here, allow read from nroaml memory!
}

void BIOSROM_updateTimers(DOUBLE timepassed)
{
	byte i, timersleft;
	if (unlikely(OPTROM_timeoutused))
	{
		timersleft = 0; //Default: finished!
		for (i=0;i<numOPT_ROMS;++i)
		{
			if (unlikely(OPTROM_writetimeout[i])) //Timing?
			{
				OPTROM_writetimeout[i] -= timepassed; //Time passed!
				if (unlikely(OPTROM_writetimeout[i]<=0.0)) //Expired?
				{
					OPTROM_writetimeout[i] = (DOUBLE)0; //Finish state!
					OPTROM_writeenabled[i] = 0; //Disable writes!
				}
				else timersleft = 1; //Still running?
			}
		}
		if (timersleft==0) OPTROM_timeoutused = 0; //Finished all timers!
	}
}


extern uint_64 BIU_cachedmemoryaddr[MAXCPUS][2];
extern uint_64 BIU_cachedmemoryread[MAXCPUS][2];
extern byte BIU_cachedmemorysize[MAXCPUS][2]; //To invalidate the BIU cache!
extern byte memory_datawrittensize; //How many bytes have been written to memory during a write!

byte OPTROM_writehandler(uint_32 offset, byte value)    /* A pointer to a handler function */
{
	INLINEREGISTER uint_32 basepos, currentpos, ROMaddress;
	basepos = currentpos = offset; //Load the offset!
	if (unlikely((basepos>=0xC0000) && (basepos<0xF0000))) basepos = 0xC0000; //Our base reference position!
	else //Out of range (16-bit)?
	{
		if (unlikely((basepos>=0xC0000000) && (basepos<0xF0000000))) basepos = 0xC0000000; //Our base reference position!
		else return 0; //Our of range (32-bit)?
	}
	currentpos -= basepos; //Calculate from the base position!
	if (unlikely((offset>=0xE0000) && (offset<=0xFFFFF) && (BIOSROM_DisableLowMemory))) return 0; //Disabled for Compaq RAM!
	basepos = currentpos; //Write back!
	INLINEREGISTER uint_64 OPTROM_address, OPTROM_loc; //The address calculated in the EEPROM!
	INLINEREGISTER byte i=0,j=numOPT_ROMS;
	if (unlikely(!numOPT_ROMS)) goto noOPTROMSW;
	do //Check OPT ROMS!
	{
		if (likely(OPT_ROMS[i])) //Enabled?
		{
			OPTROM_loc = OPTROM_location[i]; //Load the current location!
			if (likely((OPTROM_loc>>32)>basepos)) //Before the end of the ROM?
			{
				OPTROM_loc &= 0xFFFFFFFF;
				if (likely(OPTROM_loc <= basepos)) //After the start of the ROM?
				{
					OPTROM_address = basepos;
					OPTROM_address -= OPTROM_loc; //The location within the OPTROM!
					if ((VGAROM_mapping!=0xFF) && (i==0)) //Special mapping?
					{
						switch (VGAROM_mapping) //What special mapping?
						{
							case 0: //C000-C3FF enabled
								if (OPTROM_address>0x3FF0) return 0; //Unmapped!
								break;
							case 1: //ROM disabled (ET3K/4K-AX), C000-C5FFF(ET3K/4K-AF)
								return 0; //Disable the ROM!
							case 2: //C000-C5FF, C680-C7FF Enabled
								if ((OPTROM_address>=0xC600) && (OPTROM_address<0xC680)) return 0; //Unmapped in the mid-range!
								//Passthrough to the end mapping!
							case 3: //C000-C7FF Enabled
								if (OPTROM_address>0x8000) return 0; //Disabled!
								break;
							default: //Don't handle specially?
								break;
						}
					}
					byte OPTROM_inhabitwrite = 0; //Are we to inhabit the current write(pending buffered)?
					switch (OPTROM_address)
					{
					case 0x1555:
						if ((value == 0xAA) && !OPTROM_writeSequence[i]) //Start sequence!
						{
							OPTROM_writeSequence[i] = 1; //Next step!
							OPTROM_pendingAA_1555[i] = 1; //We're pending to write!
							OPTROM_inhabitwrite = 1; //We're inhabiting the write!
						}
						else if (OPTROM_writeSequence[i] == 2) //We're a command byte!
						{
							switch (value)
							{
							case 0xA0: //Enable write protect!
								OPTROM_writeSequence_waitingforDisable[i] = 0; //Not waiting anymore!
								OPTROM_writeSequence[i] = 0; //Finished write sequence!
								#ifdef IS_LONGDOUBLE
								OPTROM_writetimeout[i] = 10000000.0L; //We're disabling writes to the EEPROM 10ms after this write, the same applies to the following writes!
								#else
								OPTROM_writetimeout[i] = 10000000.0; //We're disabling writes to the EEPROM 10ms after this write, the same applies to the following writes!
								#endif
								OPTROM_timeoutused = 1; //Timing!
								OPTROM_pending55_0AAA[i] = OPTROM_pendingAA_1555[i] = 0; //Not pending anymore!
								OPTROM_inhabitwrite = 1; //We're preventing us from writing!
								break;
							case 0x80: //Wait for 0x20 to disable write protect!
								OPTROM_writeSequence_waitingforDisable[i] = 1; //Waiting for disable!
								OPTROM_writeSequence[i] = 0; //Finished write sequence!
								OPTROM_inhabitwrite = 1; //We're preventing us from writing!
								OPTROM_pendingAA_1555[i] = OPTROM_pending55_0AAA[i] = 0; //Not pending anymore!
								break;
							case 0x20: //Disable write protect!
								if (OPTROM_writeSequence_waitingforDisable[i]) //Waiting for disable?
								{
									OPTROM_writeenabled[i] = OPTROM_writeenabled[i]?1:2; //We're enabling writes to the EEPROM now/before next write!
									OPTROM_pending55_0AAA[i] = OPTROM_pendingAA_1555[i] = 0; //Not pending anymore!
									OPTROM_inhabitwrite = (OPTROM_writeenabled[i]==1)?1:0; //We're preventing us from writing!
									OPTROM_writeSequence_waitingforDisable[i] = 0; //Not waiting anymore!
									OPTROM_writeSequence[i] =  0; //Reset the sequence!
								}
								else
								{
									OPTROM_writeSequence_waitingforDisable[i] = 0; //Not waiting anymore!
									OPTROM_writeSequence[i] = 0; //Finished write sequence!
									OPTROM_pendingAA_1555[i] = 0;
								}
								break;
							default: //Not a command!
								OPTROM_writeSequence_waitingforDisable[i] = 0; //Not waiting anymore!
								OPTROM_writeSequence[i] = 0; //Finished write sequence!
								OPTROM_pendingAA_1555[i] = 0;
								break;
							}
						}
						else
						{
							OPTROM_writeSequence_waitingforDisable[i] = 0; //Not waiting anymore!
							OPTROM_writeSequence[i] = 0; //Finished write sequence!
							OPTROM_pendingAA_1555[i] = 0; //Not pending anymore!
						}
						break;
					case 0x0AAA:
						if ((value == 0x55) && (OPTROM_writeSequence[i] == 1)) //Start of valid sequence which is command-specific?
						{
							OPTROM_writeSequence[i] = 2; //Start write command sequence!
							OPTROM_pending55_0AAA[i] = 1; //We're pending to write!
							OPTROM_inhabitwrite = 1; //We're inhabiting the write!
						}
						else
						{
							OPTROM_writeSequence_waitingforDisable[i] = 0; //Not waiting anymore!
							OPTROM_writeSequence[i] = 0; //Finished write sequence!
							OPTROM_pending55_0AAA[i] = 0; //Not pending anymore!
						}
						break;
					default: //Any other address!
						OPTROM_writeSequence_waitingforDisable[i] = 0; //Not waiting anymore!
						OPTROM_writeSequence[i] = 0; //No sequence running!
						break;
					}
					uint_32 originaladdress = (uint_32)OPTROM_address; //Save the address we're writing to!
					if ((!OPTROM_writeenabled[i]) || OPTROM_inhabitwrite)
					{
						memory_datawrittensize = 1; //Only 1 byte written!
						return 1; //Handled: ignore writes to ROM or protected ROM!
					}
					else if (OPTROM_writeenabled[i]==2)
					{
						OPTROM_writeenabled[i] = 1; //Start next write!
						memory_datawrittensize = 1; //Only 1 byte written!
						return 1; //Disable this write, enable next write!
					}
					if (OPTROM_writetimeout[i]) //Timing?
					{
						#ifdef IS_LONGDOUBLE
						OPTROM_writetimeout[i] = 10000000.0L; //Reset timer!
						#else
						OPTROM_writetimeout[i] = 10000000.0; //Reset timer!
						#endif
						OPTROM_timeoutused = 1; //Timing!
					}
				processPendingWrites:
					ROMaddress = OPTROM_address; //Reversed location!
					if ((ISVGA==4) && (i==0)) //EGA ROM is reversed?
					{
						OPTROM_address = ((OPTROM_location[i]>>32)-OPTROM_address)-1; //The ROM is reversed, so reverse write too!
					}
					//We're a EEPROM with write protect disabled!
					BIGFILE *f; //For opening the ROM file!
					f = emufopen64(OPTROM_filename[i], "rb+"); //Open the ROM for writing!
					if (!f) return 1; //Couldn't open the ROM for writing!
					if (emufseek64(f, (uint_32)OPTROM_address, SEEK_SET)) //Couldn't seek?
					{
						emufclose64(f); //Close the file!
						memory_datawrittensize = 1; //Only 1 byte written!
						return 1; //Abort!
					}
					if (emuftell64(f) != OPTROM_address) //Failed seek position?
					{
						emufclose64(f); //Close the file!
						memory_datawrittensize = 1; //Only 1 byte written!
						return 1; //Abort!
					}
					if (emufwrite64(&value, 1, 1, f) != 1) //Failed to write the data to the file?
					{
						emufclose64(f); //Close thefile!
						memory_datawrittensize = 1; //Only 1 byte written!
						return 1; //Abort!
					}
					emufclose64(f); //Close the file!
					OPT_ROMS[i][OPTROM_address] = value; //Write the data to the ROM in memory!
					OPT_ROMS_shadow[i][ROMaddress] = value; //Write the data to the shadow ROM in memory!
					if (unlikely(BIU_cachedmemorysize[0][0] && (BIU_cachedmemoryaddr[0][0] <= offset) && ((BIU_cachedmemoryaddr[0][0] + BIU_cachedmemorysize[0][0]) > offset))) //Matched an active read cache(allowing self-modifying code)?
					{
						memory_datasize[0] = 0; //Only 1 byte invalidated!
						BIU_cachedmemorysize[0][0] = 0; //Make sure that the BIU has an updated copy of this in it's cache!
					}
					if (unlikely(BIU_cachedmemorysize[1][0] && (BIU_cachedmemoryaddr[1][0] <= offset) && ((BIU_cachedmemoryaddr[1][0] + BIU_cachedmemorysize[1][0]) > offset))) //Matched an active read cache(allowing self-modifying code)?
					{
						memory_datasize[0] = 0; //Only 1 byte invalidated!
						BIU_cachedmemorysize[1][0] = 0; //Make sure that the BIU has an updated copy of this in it's cache!
					}
					if (unlikely(BIU_cachedmemorysize[0][1] && (BIU_cachedmemoryaddr[0][1] <= offset) && ((BIU_cachedmemoryaddr[0][1] + BIU_cachedmemorysize[0][1]) > offset))) //Matched an active read cache(allowing self-modifying code)?
					{
						memory_datasize[1] = 0; //Only 1 byte invalidated!
						BIU_cachedmemorysize[0][1] = 0; //Make sure that the BIU has an updated copy of this in it's cache!
					}
					if (unlikely(BIU_cachedmemorysize[1][1] && (BIU_cachedmemoryaddr[1][1] <= offset) && ((BIU_cachedmemoryaddr[1][1] + BIU_cachedmemorysize[1][1]) > offset))) //Matched an active read cache(allowing self-modifying code)?
					{
						memory_datasize[1] = 0; //Only 1 byte invalidated!
						BIU_cachedmemorysize[1][1] = 0; //Make sure that the BIU has an updated copy of this in it's cache!
					}
					if (OPTROM_pending55_0AAA[i] && ((OPTROM_location[i]>>32)>0x0AAA)) //Pending write and within ROM range?
					{
						OPTROM_pending55_0AAA[i] = 0; //Not pending anymore, processing now!
						value = 0x55; //We're writing this anyway!
						OPTROM_address = 0x0AAA; //The address to write to!
						if (originaladdress!=0x0AAA) goto processPendingWrites; //Process the pending write!
					}
					if (OPTROM_pendingAA_1555[i] && ((OPTROM_location[i]>>32)>0x1555)) //Pending write and within ROM range?
					{
						OPTROM_pendingAA_1555[i] = 0; //Not pending anymore, processing now!
						value = 0xAA; //We're writing this anyway!
						OPTROM_address = 0x1555; //The address to write to!
						if (originaladdress!=0x1555) goto processPendingWrites; //Process the pending write!
					}
					memory_datawrittensize = 1; //Only 1 byte written!
					return 1; //Ignore writes to memory: we've handled it!
				}
			}
		}
		++i;
	} while (--j);
	noOPTROMSW:
	if (BIOS_custom_VGAROM_size) //Custom VGA ROM mounted?
	{
		if (likely(basepos < BIOS_custom_VGAROM_size)) //OK?
		{
			memory_datawrittensize = 1; //Only 1 byte written!
			return 1; //Ignore writes!
		}
	}
	return 0; //No ROM here, allow writes to normal memory!
}

byte BIOS_writehandler(uint_32 offset, byte value)    /* A pointer to a handler function */
{
	INLINEREGISTER uint_32 basepos, tempoffset;
	basepos = tempoffset = offset; //Load the current location!
	if (basepos >= BIOSROM_BASE_XT) //Inside 16-bit/32-bit range?
	{
		if (unlikely(basepos < 0x100000)) basepos = BIOSROM_BASE_XT; //Our base reference position(low memory)!
		else if (unlikely((basepos >= BIOSROM_BASE_Modern) && (EMULATED_CPU >= CPU_80386))) basepos = BIOSROM_BASE_Modern; //Our base reference position(high memory 386+)!
		else if (unlikely((basepos >= BIOSROM_BASE_AT) && (EMULATED_CPU == CPU_80286) && (basepos < 0x1000000))) basepos = BIOSROM_BASE_AT; //Our base reference position(high memmory 286)
		else return OPTROM_writehandler(offset, value); //OPTROM? Out of range (32-bit)?
	}
	else return OPTROM_writehandler(offset, value); //Our of range (32-bit)?

	tempoffset -= basepos; //Calculate from the base position!
	if ((offset>=0xE0000) && (offset<=0xFFFFF) && (BIOSROM_DisableLowMemory)) return 0; //Disabled for Compaq RAM!
	basepos = tempoffset; //Save for easy reference!

	if (BIOS_writeprotect) //BIOS ROM is write protected?
	{
		memory_datawrittensize = 1; //Only 1 byte written!
		return 1; //Ignore writes!
	}

	if (unlikely(BIOS_custom_ROM)) //Custom/system ROM loaded?
	{
		if (likely(BIOS_custom_ROM_size == 0x10000))
		{
			if (likely(tempoffset<0x10000)) //Within range?
			{
				tempoffset &= 0xFFFF; //16-bit ROM!
				memory_datawrittensize = 1; //Only 1 byte written!
				return 1; //Ignore writes!
			}
		}

		if ((EMULATED_CPU>=CPU_80386) && (is_XT==0)) //Compaq compatible?
		{
			if (unlikely(tempoffset>=BIOS_custom_ROM_size)) //Doubled copy?
			{
				tempoffset -= BIOS_custom_ROM_size; //Double in memory!
			}
		}
		memory_datawrittensize = 1; //Only 1 byte written!
		if (likely(tempoffset<BIOS_custom_ROM_size)) //Within range?
		{
			if (BIOS_custom_ROM_size == 0x20000) //Valid size to use a BIOS flash ROM emulation?
			{
				if (likely(BIOS_flash_enabled)) //Enabled?
				{
					//Emulate the flash ROM!
					BIOS_flash_write8(BIOS_custom_ROM, tempoffset, &customROMname[0], value); //Write access!
				}
			}
			return 1; //Ignore writes!
		}
		else //Custom ROM, but nothing to give? Special mapping!
		{
			return 1; //Abort!
		}
		tempoffset = basepos; //Restore the temporary offset!
	}

	INLINEREGISTER uint_32 originaloffset;
	INLINEREGISTER uint_32 segment; //Current segment!
		segment = basepos; //Load the offset!
		switch (BIOS_ROM_type) //What type of ROM is loaded?
		{
		case BIOSROMTYPE_U18_19: //U18&19 combo?
			originaloffset = basepos; //Save the original offset for reference!
			if (unlikely(basepos>=0x10000)) return 0; //Not us!
			basepos &= 0x7FFF; //Our offset within the ROM!
			if (originaloffset&0x8000) //u18?
			{
				if (likely(BIOS_ROM_size[18]>basepos)) //Within range?
				{
					memory_datawrittensize = 1; //Only 1 byte written!
					return 1; //Ignore writes!
				}
			}
			else //u19?
			{
				if (likely(BIOS_ROM_size[19]>basepos)) //Within range?
				{
					memory_datawrittensize = 1; //Only 1 byte written!
					return 1; //Ignore writes!
				}
			}
			break;
		case BIOSROMTYPE_U13_15: //U13&15 combo?
			if (likely(tempoffset<BIOS_ROM_U13_15_double)) //This is doubled in ROM!
			{
				if (unlikely(tempoffset>=(BIOS_ROM_U13_15_double>>1))) //Second copy?
				{
					tempoffset -= BIOS_ROM_U13_15_single; //Patch to first block to address!
				}
			}
			tempoffset >>= 1; //The offset is at every 2 bytes of memory!
			segment &= 1; //Even=u27, Odd=u47
			if (segment) //u47/u35/u15?
			{
				if (likely(BIOS_ROM_size[15]>tempoffset)) //Within range?
				{
					memory_datawrittensize = 1; //Only 1 byte written!
					return 1; //Ignore writes!
				}					
			}
			else //u13/u15 combination?
			{
				if (likely(BIOS_ROM_size[13]>tempoffset)) //Within range?
				{
					memory_datawrittensize = 1; //Only 1 byte written!
					return 1; //Ignore writes!
				}
			}
			break;
		case BIOSROMTYPE_U34_35: //U34/35 combo?
			tempoffset >>= 1; //The offset is at every 2 bytes of memory!
			segment &= 1; //Even=u27, Odd=u47
			if (segment)
			{
				if (likely(BIOS_ROM_size[35]>tempoffset)) //Within range?
				{
					memory_datawrittensize = 1; //Only 1 byte written!
					return 1; //Ignore writes!
				}
			}
			else //u34/u35 combination?
			{
				if (likely(BIOS_ROM_size[34]>tempoffset)) //Within range?
				{
					memory_datawrittensize = 1; //Only 1 byte written!
					return 1; //Ignore writes!
				}
			}
			break;
		case BIOSROMTYPE_U27_47: //U27/47 combo?
			tempoffset >>= 1; //The offset is at every 2 bytes of memory!
			segment &= 1; //Even=u27, Odd=u47
			if (segment) //Normal AT BIOS ROM?
			{
				if (likely(BIOS_ROM_size[47]>tempoffset)) //Within range?
				{
					memory_datawrittensize = 1; //Only 1 byte written!
					return 1; //Ignore writes!
				}
			}
			else //Loaded?
			{
				if (likely(BIOS_ROM_size[27]>tempoffset)) //Within range?
				{
					memory_datawrittensize = 1; //Only 1 byte written!
					return 1; //Ignore writes!
				}
			}
			break;
		default: break; //Unknown even/odd mapping!
		}

	return OPTROM_writehandler(offset, value); //Not recognised, use normal RAM or option ROM!
}

byte BIOS_readhandler(uint_32 offset, byte index) /* A pointer to a handler function */
{
	byte flashresult;
	INLINEREGISTER uint_32 basepos, tempoffset, baseposbackup, temp;
	uint_64 endpos;
	basepos = tempoffset = offset;
	if (basepos>=BIOSROM_BASE_XT) //Inside 16-bit/32-bit range?
	{
		if (unlikely(basepos < 0x100000)) { basepos = BIOSROM_BASE_XT; endpos = 0x100000; } //Our base reference position(low memory)!
		else if (unlikely((basepos >= BIOSROM_BASE_Modern) && (EMULATED_CPU >= CPU_80386))) { basepos = BIOSROM_BASE_Modern; endpos = 0x100000000ULL; } //Our base reference position(high memory 386+)!
		else if (unlikely((basepos >= BIOSROM_BASE_AT) && (EMULATED_CPU == CPU_80286) && (basepos < 0x1000000))) { basepos = BIOSROM_BASE_AT; endpos = 0x1000000; } //Our base reference position(high memmory 286)
		else return OPTROM_readhandler(offset,index); //OPTROM or nothing? Out of range (32-bit)?
	}
	else return OPTROM_readhandler(offset,index); //OPTROM or nothing? Out of range (32-bit)?
	
	baseposbackup = basepos; //Store for end location reversal!
	tempoffset -= basepos; //Calculate from the base position!
	basepos = tempoffset; //Save for easy reference!
	if (unlikely(BIOS_custom_ROM)) //Custom/system ROM loaded?
	{
		if (BIOS_custom_ROM_size == 0x10000)
		{
			if (likely(tempoffset<0x10000)) //Within range?
			{
				tempoffset &= 0xFFFF; //16-bit ROM!
				#ifdef USE_MEMORY_CACHING
				if (likely((index & 3) == 0)) //First byte?
				{
					temp = tempoffset; //Backup address!
					tempoffset &= ~0xF; //Round down to the qword address!
					if (likely(((tempoffset | 0xF) < BIOS_custom_ROM_size))) //Enough to read a dword?
					{
						memory_dataread[0] = SDL_SwapLE64(*((uint_64*)(&BIOS_custom_ROM[tempoffset]))); //Read the data from the ROM!
						memory_dataread[1] = SDL_SwapLE64(*((uint_64*)(&BIOS_custom_ROM[tempoffset+8]))); //Read the data from the ROM!
						memory_datasize[(index >> 5) & 1] = tempoffset = 16 - (temp - tempoffset); //What is read from the whole dword!
						shiftr128(&memory_dataread[1],&memory_dataread[0],((16 - tempoffset) << 3)); //Discard the bytes that are not to be read(before the requested address)!
						return 1; //Done: we've been read!
					}
					else
					{
						tempoffset = temp; //Restore the original address!
						tempoffset &= ~7; //Round down to the qword address!
						if (likely(((tempoffset | 7) < BIOS_custom_ROM_size))) //Enough to read a dword?
						{
							memory_dataread[0] = SDL_SwapLE64(*((uint_64*)(&BIOS_custom_ROM[tempoffset]))); //Read the data from the ROM!
							memory_datasize[(index >> 5) & 1] = tempoffset = 8 - (temp - tempoffset); //What is read from the whole dword!
							memory_dataread[0] >>= ((8 - tempoffset) << 3); //Discard the bytes that are not to be read(before the requested address)!
							return 1; //Done: we've been read!
						}
						else
						{
							tempoffset = temp; //Restore the original address!
							tempoffset &= ~3; //Round down to the dword address!
							if (likely(((tempoffset | 3) < BIOS_custom_ROM_size))) //Enough to read a dword?
							{
								memory_dataread[0] = SDL_SwapLE32(*((uint_32*)(&BIOS_custom_ROM[tempoffset]))); //Read the data from the ROM!
								memory_datasize[(index >> 5) & 1] = tempoffset = 4 - (temp - tempoffset); //What is read from the whole dword!
								memory_dataread[0] >>= ((4 - tempoffset) << 3); //Discard the bytes that are not to be read(before the requested address)!
								return 1; //Done: we've been read!
							}
							else
							{
								tempoffset = temp; //Restore the original address!
								tempoffset &= ~1; //Round down to the word address!
								if (likely(((tempoffset | 1) < BIOS_custom_ROM_size))) //Enough to read a word, aligned?
								{
									memory_dataread[0] = SDL_SwapLE16(*((word*)(&BIOS_custom_ROM[tempoffset]))); //Read the data from the ROM!
									memory_datasize[(index >> 5) & 1] = tempoffset = 2 - (temp - tempoffset); //What is read from the whole word!
									memory_dataread[0] >>= ((2 - tempoffset) << 3); //Discard the bytes that are not to be read(before the requested address)!
									return 1; //Done: we've been read!				
								}
								else //Enough to read a byte only?
								{
									memory_dataread[0] = BIOS_custom_ROM[temp]; //Read the data from the ROM!
									memory_datasize[(index >> 5) & 1] = 1; //Only 1 byte!
									return 1; //Done: we've been read!				
								}
							}
						}
					}
				}
				else //Enough to read a byte only?
				#endif
				{
					memory_dataread[0] = BIOS_custom_ROM[tempoffset]; //Read the data from the ROM, reversed!
					memory_datasize[(index >> 5) & 1] = 1; //Only 1 byte!
					return 1; //Done: we've been read!				
				}
			}
		}
		if ((EMULATED_CPU>=CPU_80386) && (is_XT==0)) //Compaq compatible?
		{
			if ((tempoffset<BIOS_custom_ROM_size) && ROM_doubling) //Doubled copy?
			{
				tempoffset += BIOS_custom_ROM_size; //Double in memory by patching to second block!
			}
		}
		tempoffset = (uint_32)(BIOS_custom_ROM_size-(endpos-(tempoffset+baseposbackup))); //Patch to the end block of the ROM instead of the start.
		if ((BIOS_custom_ROM_size == 0x20000) && (is_i430fx)) //Emulating flash ROM?
		{
			//Emulate Intel flash ROM!
			if (BIOS_flash_enabled)
			{
				if (BIOS_flash_read8(BIOS_custom_ROM, tempoffset, &flashresult)) //Flash override?
				{
					memory_dataread[0] = flashresult; //Read the data from the ROM, reversed!
					memory_datasize[(index >> 5) & 1] = 1; //Only 1 byte!
					return 1; //Done: we've been read!
				}
			}
		}
		if (likely(tempoffset<BIOS_custom_ROM_size)) //Within range?
		{
			#ifdef USE_MEMORY_CACHING
			if (likely((index & 3) == 0))
			{
				temp = tempoffset; //Backup address!
				tempoffset &= ~0xF; //Round down to the dword address!
				if (likely(((tempoffset | 0xF) < BIOS_custom_ROM_size))) //Enough to read a dword?
				{
					memory_dataread[0] = SDL_SwapLE64(*((uint_64*)(&BIOS_custom_ROM[tempoffset]))); //Read the data from the ROM!
					memory_dataread[1] = SDL_SwapLE64(*((uint_64*)(&BIOS_custom_ROM[tempoffset+8]))); //Read the data from the ROM!
					memory_datasize[(index >> 5) & 1] = tempoffset = 16 - (temp - tempoffset); //What is read from the whole dword!
					shiftr128(&memory_dataread[1],&memory_dataread[0],((16 - tempoffset) << 3)); //Discard the bytes that are not to be read(before the requested address)!
					return 1; //Done: we've been read!
				}
				else
				{
					tempoffset = temp; //Restore the original address!
					tempoffset &= ~7; //Round down to the dword address!
					if (likely(((tempoffset | 7) < BIOS_custom_ROM_size))) //Enough to read a dword?
					{
						memory_dataread[0] = SDL_SwapLE64(*((uint_64*)(&BIOS_custom_ROM[tempoffset]))); //Read the data from the ROM!
						memory_datasize[(index >> 5) & 1] = tempoffset = 8 - (temp - tempoffset); //What is read from the whole dword!
						memory_dataread[0] >>= ((8 - tempoffset) << 3); //Discard the bytes that are not to be read(before the requested address)!
						return 1; //Done: we've been read!
					}
					else
					{
						tempoffset = temp; //Restore the original address!
						tempoffset &= ~3; //Round down to the dword address!
						if (likely(((tempoffset | 3) < BIOS_custom_ROM_size))) //Enough to read a dword?
						{
							memory_dataread[0] = SDL_SwapLE32(*((uint_32*)(&BIOS_custom_ROM[tempoffset]))); //Read the data from the ROM!
							memory_datasize[(index >> 5) & 1] = tempoffset = 4 - (temp - tempoffset); //What is read from the whole dword!
							memory_dataread[0] >>= ((4 - tempoffset) << 3); //Discard the bytes that are not to be read(before the requested address)!
							return 1; //Done: we've been read!
						}
						else
						{
							tempoffset = temp; //Restore the original address!
							tempoffset &= ~1; //Round down to the word address!
							if (likely(((tempoffset | 1) < BIOS_custom_ROM_size))) //Enough to read a word, aligned?
							{
								memory_dataread[0] = SDL_SwapLE16(*((word*)(&BIOS_custom_ROM[tempoffset]))); //Read the data from the ROM!
								memory_datasize[(index >> 5) & 1] = tempoffset = 2 - (temp - tempoffset); //What is read from the whole word!
								memory_dataread[0] >>= ((2 - tempoffset) << 3); //Discard the bytes that are not to be read(before the requested address)!
								return 1; //Done: we've been read!				
							}
							else //Enough to read a byte only?
							{
								memory_dataread[0] = BIOS_custom_ROM[temp]; //Read the data from the ROM!
								memory_datasize[(index >> 5) & 1] = 1; //Only 1 byte!
								return 1; //Done: we've been read!				
							}
						}
					}
				}
			}
			else //Enough to read a byte only?
			#endif
			{
				memory_dataread[0] = BIOS_custom_ROM[tempoffset]; //Read the data from the ROM, reversed!
				memory_datasize[(index >> 5) & 1] = 1; //Only 1 byte!
				return 1; //Done: we've been read!				
			}
		}
		else //Custom ROM, but nothing to give? Give 0x00!
		{
			memory_dataread[0] = 0x00; //Dummy value for the ROM!
			memory_datasize[(index >> 5) & 1] = 1; //Only 1 byte!
			return 1; //Abort!
		}
		tempoffset = basepos; //Restore the temporary offset!
	}

	INLINEREGISTER uint_32 segment; //Current segment!
	switch (BIOS_ROM_type) //What ROM type are we emulating?
	{
		case BIOSROMTYPE_U18_19: //U18&19 combo?
			tempoffset = basepos;
			tempoffset &= 0x7FFF; //Our offset within the ROM!
			segment = (((basepos >> 15) & 1) ^ 1); //ROM number: 0x8000+:u18, 0+:u19
			segment += 18; //The ROM number!
			if (likely(BIOS_ROM_size[segment]>tempoffset)) //Within range?
			{
				#ifdef USE_MEMORY_CACHING
				if (likely((index & 3) == 0))
				{
					temp = tempoffset; //Backup address!
					tempoffset &= ~0xF; //Round down to the dword address!
					if (likely(((tempoffset | 0xF) < BIOS_ROM_size[segment]))) //Enough to read a dword?
					{
						memory_dataread[0] = SDL_SwapLE64(*((uint_64*)(&BIOS_ROMS[segment][tempoffset]))); //Read the data from the ROM!
						memory_dataread[1] = SDL_SwapLE64(*((uint_64*)(&BIOS_ROMS[segment][tempoffset+8]))); //Read the data from the ROM!
						memory_datasize[(index >> 5) & 1] = tempoffset = 16 - (temp - tempoffset); //What is read from the whole dword!
						shiftr128(&memory_dataread[1],&memory_dataread[0],((16 - tempoffset) << 3)); //Discard the bytes that are not to be read(before the requested address)!
						return 1; //Done: we've been read!
					}
					else
					{
						tempoffset = temp; //Restore the original address!
						tempoffset &= ~7; //Round down to the dword address!
						if (likely(((tempoffset | 7) < BIOS_ROM_size[segment]))) //Enough to read a dword?
						{
							memory_dataread[0] = SDL_SwapLE64(*((uint_64*)(&BIOS_ROMS[segment][tempoffset]))); //Read the data from the ROM!
							memory_datasize[(index >> 5) & 1] = tempoffset = 8 - (temp - tempoffset); //What is read from the whole dword!
							memory_dataread[0] >>= ((8 - tempoffset) << 3); //Discard the bytes that are not to be read(before the requested address)!
							return 1; //Done: we've been read!
						}
						else
						{
							tempoffset = temp; //Restore the original address!
							tempoffset &= ~3; //Round down to the dword address!
							if (likely(((tempoffset | 3) < BIOS_ROM_size[segment]))) //Enough to read a dword?
							{
								memory_dataread[0] = SDL_SwapLE32(*((uint_32*)(&BIOS_ROMS[segment][tempoffset]))); //Read the data from the ROM!
								memory_datasize[(index >> 5) & 1] = tempoffset = 4 - (temp - tempoffset); //What is read from the whole dword!
								memory_dataread[0] >>= ((4 - tempoffset) << 3); //Discard the bytes that are not to be read(before the requested address)!
								return 1; //Done: we've been read!
							}
							else
							{
								tempoffset = temp; //Restore the original address!
								tempoffset &= ~1; //Round down to the word address!
								if (likely(((tempoffset | 1) < BIOS_combinedROM_size))) //Enough to read a word, aligned?
								{
									memory_dataread[0] = SDL_SwapLE16(*((word*)(&BIOS_ROMS[segment][tempoffset]))); //Read the data from the ROM!
									memory_datasize[(index >> 5) & 1] = tempoffset = 2 - (temp - tempoffset); //What is read from the whole word!
									memory_dataread[0] >>= ((2 - tempoffset) << 3); //Discard the bytes that are not to be read(before the requested address)!
									return 1; //Done: we've been read!				
								}
								else //Enough to read a byte only?
								{
									memory_dataread[0] = BIOS_ROMS[segment][temp]; //Read the data from the ROM!
									memory_datasize[(index >> 5) & 1] = 1; //Only 1 byte!
									return 1; //Done: we've been read!				
								}
							}
						}
					}
				}
				else //Enough to read a byte only?
				#endif
				{
					memory_dataread[0] = BIOS_ROMS[segment][tempoffset]; //Read the data from the ROM, reversed!
					memory_datasize[(index >> 5) & 1] = 1; //Only 1 byte!
					return 1; //Done: we've been read!				
				}
			}
			break;
		case BIOSROMTYPE_U34_35: //Odd/even ROM
		case BIOSROMTYPE_U27_47: //Odd/even ROM
		case BIOSROMTYPE_U13_15: //U13&15 combo? Also Odd/even ROM!
			tempoffset = basepos; //Load the offset! General for AT+ ROMs!
			if (likely(tempoffset<BIOS_ROM_U13_15_double)) //This is doubled in ROM!
			{
				if (unlikely(tempoffset>=BIOS_ROM_U13_15_single)) //Second copy?
				{
					tempoffset -= BIOS_ROM_U13_15_single; //Patch to first block to address!
				}
			}

			if (likely(BIOS_combinedROM_size > tempoffset)) //Within range?
			{
				#ifdef USE_MEMORY_CACHING
				if ((index & 3) == 0)
				{
					temp = tempoffset; //Backup address!
					tempoffset &= ~0xF; //Round down to the dword address!
					if (likely(((tempoffset | 0xF) < BIOS_combinedROM_size))) //Enough to read a dword?
					{
						memory_dataread[0] = SDL_SwapLE64(*((uint_64*)(&BIOS_combinedROM[tempoffset]))); //Read the data from the ROM!
						memory_dataread[1] = SDL_SwapLE64(*((uint_64*)(&BIOS_combinedROM[tempoffset+8]))); //Read the data from the ROM!
						memory_datasize[(index >> 5) & 1] = tempoffset = 16 - (temp - tempoffset); //What is read from the whole dword!
						shiftr128(&memory_dataread[1],&memory_dataread[0],((16 - tempoffset) << 3)); //Discard the bytes that are not to be read(before the requested address)!
						return 1; //Done: we've been read!
					}
					else
					{
						tempoffset = temp; //Restore the original address!
						tempoffset &= ~7; //Round down to the dword address!
						if (likely(((tempoffset | 7) < BIOS_combinedROM_size))) //Enough to read a dword?
						{
							memory_dataread[0] = SDL_SwapLE64(*((uint_64*)(&BIOS_combinedROM[tempoffset]))); //Read the data from the ROM!
							memory_datasize[(index >> 5) & 1] = tempoffset = 8 - (temp - tempoffset); //What is read from the whole dword!
							memory_dataread[0] >>= ((8 - tempoffset) << 3); //Discard the bytes that are not to be read(before the requested address)!
							return 1; //Done: we've been read!
						}
						else
						{
							tempoffset = temp; //Restore the original address!
							tempoffset &= ~3; //Round down to the dword address!
							if (likely(((tempoffset | 3) < BIOS_combinedROM_size))) //Enough to read a dword?
							{
								memory_dataread[0] = SDL_SwapLE32(*((uint_32*)(&BIOS_combinedROM[tempoffset]))); //Read the data from the ROM!
								memory_datasize[(index >> 5) & 1] = tempoffset = 4 - (temp - tempoffset); //What is read from the whole dword!
								memory_dataread[0] >>= ((4 - tempoffset) << 3); //Discard the bytes that are not to be read(before the requested address)!
								return 1; //Done: we've been read!
							}
							else
							{
								tempoffset = temp; //Restore the original address!
								tempoffset &= ~1; //Round down to the word address!
								if (likely(((tempoffset | 1) < BIOS_combinedROM_size))) //Enough to read a word, aligned?
								{
									memory_dataread[0] = SDL_SwapLE16(*((word*)(&BIOS_combinedROM[tempoffset]))); //Read the data from the ROM!
									memory_datasize[(index >> 5) & 1] = tempoffset = 2 - (temp - tempoffset); //What is read from the whole word!
									memory_dataread[0] >>= ((2 - tempoffset) << 3); //Discard the bytes that are not to be read(before the requested address)!
									return 1; //Done: we've been read!				
								}
								else //Enough to read a byte only?
								{
									memory_dataread[0] = BIOS_combinedROM[temp]; //Read the data from the ROM!
									memory_datasize[(index >> 5) & 1] = 1; //Only 1 byte!
									return 1; //Done: we've been read!				
								}
							}
						}
					}
				}
				else //Enough to read a byte only?
				#endif
				{
					memory_dataread[0] = BIOS_combinedROM[tempoffset]; //Read the data from the ROM, reversed!
					memory_datasize[(index >> 5) & 1] = 1; //Only 1 byte!
					return 1; //Done: we've been read!				
				}
			}
			break;
		default: break; //Unknown even/odd mapping!
	}
	return OPTROM_readhandler(offset,index); //OPTROM or nothing? Out of range (32-bit)?
}

void BIOS_registerROM()
{
	//This is called directly now!

	//Initialize flash ROM if used!
	memset(&BIOS_flash, 0, sizeof(BIOS_flash)); //Initialize the flash ROM!
	if (is_i430fx && (BIOS_custom_ROM_size==0x20000)) //i430fx/i440fx has correct flash ROM?
	{
		BIOS_flash_enabled = 1; //Start emulatihg flash ROM!
		BIOS_flash.flags = 0; //No flags!
		BIOS_flash.flash_id = 0x94; //Flash ID!
		BIOS_flash.blocks_len[BLOCK_MAIN1] = 0x1C000;
		BIOS_flash.blocks_len[BLOCK_MAIN2] = 0; //No main 2 block
		BIOS_flash.blocks_len[BLOCK_DATA1] = 0x1000;
		BIOS_flash.blocks_len[BLOCK_DATA2] = 0x1000;
		BIOS_flash.blocks_len[BLOCK_BOOT] = 0x2000;
		BIOS_flash.blocks_start[BLOCK_MAIN1] = 0; //Main block 1
		BIOS_flash.blocks_end[BLOCK_MAIN1] = 0x1BFFF;
		BIOS_flash.blocks_start[BLOCK_MAIN2] = 0xFFFFF; //Main block 2
		BIOS_flash.blocks_end[BLOCK_MAIN2] = 0xFFFFF;
		BIOS_flash.blocks_start[BLOCK_DATA1] = 0x1C000; //Data area 1 block
		BIOS_flash.blocks_end[BLOCK_DATA1] = 0x1CFFF;
		BIOS_flash.blocks_start[BLOCK_DATA2] = 0x1D000; //Data area 2 block
		BIOS_flash.blocks_end[BLOCK_DATA2] = 0x1DFFF;
		BIOS_flash.blocks_start[BLOCK_BOOT] = 0x1E000; //Boot block
		BIOS_flash.blocks_end[BLOCK_BOOT] = 0x1FFFF;
		BIOS_flash.command = CMD_READ_ARRAY; //Normal mode!
		BIOS_flash.status = 0;
		//Ready for usage of the flash ROM!
	}
	else
	{
		BIOS_flash_enabled = 0; //Disable flash emulation!
	}
	if (!is_i430fx) //Not flash ROM supported?
	{
		BIOS_writeprotect = 1; //Default: BIOS ROM is write protected!
	}
}

void BIOSROM_dumpBIOS()
{
		uint_64 baseloc, endloc;
		if (is_XT) //XT?
		{
			baseloc = BIOSROM_BASE_XT;
			endloc = 0x100000;
		}
		else if ((is_Compaq==1) || (is_i430fx) || (is_PS2)) //32-bit?
		{	
			baseloc = BIOSROM_BASE_Modern;
			endloc = 0x100000000LL;
		}
		else //AT?
		{
			baseloc = BIOSROM_BASE_AT;
			endloc = 0x1000000;
		}
		BIGFILE *f;
		char filename[2][100];
		memset(&filename,0,sizeof(filename)); //Clear/init!
		snprintf(filename[0],sizeof(filename[0]), "%s/ROMDMP.%s.BIN", ROMpath,(is_i430fx?((is_i430fx==1)?"i430fx":"i440fx"):(is_PS2?"PS2":(is_Compaq?"32":(is_XT?"XT":"AT"))))); //Create the filename for the ROM for the architecture!
		snprintf(filename[1],sizeof(filename[1]), "ROMDMP.%s.BIN",(is_i430fx?((is_i430fx==1)?"i430fx":"i440fx"):(is_PS2?"PS2":(is_Compaq?"32":(is_XT?"XT":"AT"))))); //Create the filename for the ROM for the architecture!

		f = emufopen64(filename[0],"wb");
		if (!f) return;
		for (;baseloc<endloc;++baseloc)
		{
			if (BIOS_readhandler((uint_32)baseloc,0)) //Read directly!
			{
				if (!emufwrite64(&memory_dataread,1,1,f)) //Failed to write?
				{
					emufclose64(f); //close!
					delete_file(ROMpath,filename[1]); //Remove: invalid!
					return;
				}
			}
		}
		emufclose64(f); //close!
}
