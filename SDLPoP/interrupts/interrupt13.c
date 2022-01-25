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

#include "headers/cpu/cpu.h" //Need CPU comp.
#include "headers/support/lba.h" //Need some support for LBA!
#include "headers/basicio/io.h" //Basic I/O comp.
#include "headers/cpu/easyregs.h" //Easy register functionality last!
#include "headers/emu/debugger/debugger.h" //For logging registers debugging!
#include "headers/cpu/cb_manager.h" //Callback support!
#include "headers/support/log.h" //Logging support for debugging!
#include "headers/hardware/floppy.h" //Floppy geometry support!
//Are we disabled?
#define __HW_DISABLED 0

//Look at REG_AX bit 0 of int 11h

//Extern data!
//EXT=part of the 13h Extensions which were written in the 1990s to support HDDs with more than 8GB.

/*


INT 41: Hard disk 0 Parameter table address
INT 46: Hard disk 0 copy

INT 41 points by default to memory F000:E401h

Drives 0x80@INT 41; 0x81-0x83 may follow.
		    Check by test INT 46 points tomewhere other than 16 bytes pas INT 41 and sixteen bytes starting at offset 10h are identical to int 46.
			-> or: INT 46<>INT 41+0x10 AND (INT 46 till INT 46+0x10) = offset 10h till offset 10h+10
			-> so:
				format of each table order:
				Offset:
				0x00: Primary Master
				0x10: Primary Slave
				0x20: Secondary Master
				0x30: Secondary Slave

				cylinders=0=Not set!



Format of fixed disk parameters:

Offset Size    Description     (Table 03196)
00h    WORD    number of cylinders
02h    BYTE    number of heads
03h    WORD    starting reduced write current cylinder (XT only, 0 for others)
05h    WORD    starting write precompensation cylinder number
07h    BYTE    maximum ECC burst length (XT only)
08h    BYTE    control byte (see #03197,#03198)
09h    BYTE    standard timeout (XT only, 0 for others)
0Ah    BYTE    formatting timeout (XT and WD1002 only, 0 for others)
0Bh    BYTE    timeout for checking drive (XT and WD1002 only, 0 for others)
0Ch    WORD    cylinder number of landing zone (AT and later only)
0Eh    BYTE    number of sectors per track (AT and later only)
0Fh    BYTE    reserved

Bitfields for XT fixed disk control byte:

Bit(s)  Description     (Table 03197)
2-0    drive step speed.
000  3ms.
100  200ms.
101  70ms (default).
110  3ms.
111  3ms
5-3    unused
6      disable ECC retries
7      disable access retries


Bitfields for AT fixed disk control byte:

Bit(s)  Description     (Table 03198)
0      unused
1      reserved (0)  (disable IRQ)
2      reserved (0)  (no reset)
3      set if more than 8 heads
4      always 0
5      set if manufacturer's defect map on max cylinder+1  (AT and later only)
6      disable ECC retries
7      disable access retries


*/

extern IODISK disks[6]; //All mounted disks!
byte mounteddrives[0x100]; //All mounted drives!


//Support for HDD CHS:

//Bytes per Sector
#define HDD_BPS 512
//Sectors per track
#define HDD_SPT 63
//Heads
#define HDD_HEADS 16

//# of Cylinders based on filesize
#define CYLINDERS(x) SAFEDIV(SAFEDIV(SAFEDIV(x,HEADS),SPT),BPS)

//# of Sectors based on filesize
#define SECTORS(x) (x/HDD_BPS)

//Emulate non-translating BIOS!
#define HDD_SECTORS(x) HDD_SPT

#define KB(x) (x/1024)

byte int13_buffer[512]; //Our int13_buffer!

int killRead; //For Pirates! game?

void int13_init(int floppy0, int floppy1, int hdd0, int hdd1, int cdrom0, int cdrom1)
{
	byte hdd;
	if (__HW_DISABLED) return; //Abort!	
	//We don't need to worry about file sizes: automatically done at Init!
	killRead = 0; //Init killRead!
	//Reset disk parameter table to 0's

	//Now set mounted drives according to set!
	memset(&mounteddrives,0xFF,sizeof(mounteddrives)); //Reset all to unmounted!
	
	//Floppy=optional!
	if (floppy0) //Floppy0?
	{
		mounteddrives[0] = FLOPPY0; //Floppy0!
	}
	if (floppy0 && floppy1) //Floppy0&1?
	{
		mounteddrives[1] = FLOPPY1; //Floppy1!
	}
	else if (floppy1) //Floppy1 only?
	{
		mounteddrives[0] = FLOPPY1; //Floppy1 on floppy0!
	}

	//HDD=Dynamic!
	hdd = 0x80; //Init to first drive!
	if (hdd0) //Have?
	{
		mounteddrives[hdd++] = HDD0; //Set!
	}
	if (hdd1) //Have?
	{
		mounteddrives[hdd++] = HDD1; //Set!
	}
	//2 CD-ROM drives: solid and optional!
	if (cdrom0)
	{
		mounteddrives[hdd++] = CDROM0; //Set!
	}
	if (cdrom1)
	{
		mounteddrives[hdd++] = CDROM1; //Set!
	}
}

byte getdiskbymount(int drive) //Drive to disk converter (reverse of int13_init)!
{
	word i;
	for (i = 0; i < 0xFF; i++)
	{
		if (mounteddrives[i] == drive) //Found?
		{
			return (byte)i; //Give the drive number!
		}
	}
	return 0xFF; //Unknown disk!
}

OPTINLINE word gethddheads(uint_64 disksize)
{
	if (disksize<=(1000*63*16*512)) //1-504MB?
	{
		return 16; //16 heads!
	}
	else if (disksize<=(1000*63*32*512)) //504-1008MB?
	{
		return 32; //32 heads!
	}
	else if (disksize<=(1000*63*64*512)) //1008-2016MB?
	{
		return 64; //64 heads!
	}
	else if (disksize<=4128768000LL) //2016-4032MB?
	{
		return 128; //128 heads!
	}
	//4032-8032.5MB?
	return 255; //255 heads!
}

OPTINLINE word gethddbps()
{
	return 512; //Always 512 bytes per sector!
}

void getDiskGeometry(byte disk, word *heads, word *cylinders, uint_64 *sectors, uint_64 *bps)
{
	byte head;
	byte sector;
	if ((disk==FLOPPY0) || (disk==FLOPPY1)) //Floppy0 or Floppy1?
	{
		uint_64 oursize;
		oursize = disksize(disk); //Get size!
		*heads = floppy_sides(oursize);
		*cylinders = floppy_tracks(oursize);
		*sectors = floppy_spt(oursize);
		*bps = 512;
	}
	else if ((disk==HDD0) || (disk==HDD1) || (disk==CDROM0) || (disk==CDROM1)) //HDD0 or HDD1?
	{
		LBA2CHS((uint_32)(disksize(disk)/gethddbps()),cylinders,&head,&sector,gethddheads(disksize(disk)),(uint_32)SECTORS(disksize(disk))); //Convert to emulated value!
		*heads = (uint_64)head;
		*sectors = (uint_64)sector; //Transfer rest!
		*bps = 512; //Assume 512 Bytes Per Sector!
	}
	else //Unknown disk?
	{
		*heads = 0;
		*cylinders = 0;
		*sectors = 0;
		*bps = 0;
		CALLBACK_SCF(1); //Set carry flag!
	}
}

OPTINLINE byte GetBIOSType(byte disk)
{
	//Not harddrive? Get from geometry list! Not implemented yet!
	return 0; //Else: only HDD type!
}

//Status flags for I/O!
byte last_status; //Status of last operation
byte last_drive; //Last drive something done to
extern byte advancedlog; //Advanced log setting

OPTINLINE byte readdiskdata(uint_32 startpos)
{
	byte readdata_result;
	//Detect ammount of sectors to be able to read!
	word sectors;
	word sector;
	word position; //Current position in memory!
	word left;
	word current = 0; //Current byte in the buffer!
	sectors = REG_AL; //Number of sectors to be read!
	readdata_result = 1;

	sector = 0; //Init sector!
	position = REG_BX; //Current position to write to!
	for (;sectors;) //Sectors left to read?
	{
		//Read from disk
		readdata_result = readdata(mounteddrives[REG_DL],&int13_buffer,(((uint_64)startpos)<<9)+((uint_64)sector<<9),512); //Read the data to the buffer!
		if (!readdata_result) //Error?
		{
			last_status = 0x00;
			CALLBACK_SCF(1); //Error!
			goto finishupr; //Abort with ammount of sectors read!
		}

		//Sector is read, now write it to memory!
		left = 512; //Data left!
		current = 0; //Current byte in the buffer!
		for (;;)
		{
			if (!left--) goto nextsector; //Stop when nothing left!
			MMU_wb(CPU_SEGMENT_ES,REG_ES,position,int13_buffer[current],1); //Write the data to memory!
			if (MMU_rb(CPU_SEGMENT_ES, REG_ES, position++, 0,1) != int13_buffer[current]) //Failed to write (unexistant memory, paged out or read-only)?
			{
				last_status = 0x00;
				CALLBACK_SCF(1); //Error!
				goto finishupr;
			}
			++current; //Next byte in the buffer!
		}
		nextsector: //Process next sector!
		--sectors; //One sector processed!
		++sector; //Process to the next sector!
	}

	finishupr: //Early stop: we cannot write any further!
	
	if (advancedlog)
	{
		dolog("debugger", "Read %u/%u sectors from drive %02X, start %u. Requested: Head: %u, Track: %u, Sector: %u. Start sector: %u, Destination: ES:BX=%04X:%08X",
			sector, REG_AL, REG_DL, startpos, REG_DH, REG_CH, REG_CL & 0x3F, startpos, REG_ES, REG_EBX);
	}
	return (byte)sector; //Give the ammount of sectors read!
}

OPTINLINE byte writediskdata(uint_32 startpos)
{
	//Detect ammount of sectors to be able to read!
	word sectors;
	word position; //Current position in memory!
	word sector;
	word left;
	byte writedata_result;
	word current;
	sectors = REG_AL; //Number of sectors to be read!

	sector = 0; //Init sector!
	position = REG_BX; //Current position to read from!
	for (;sectors;) //Sectors left to read?
	{
		//Fill the buffer!
		left = 512; //Data left!
		current = 0; //Current byte in the buffer!
		for (;;)
		{
			int13_buffer[current] = MMU_rb(CPU_SEGMENT_ES,REG_ES,position++,0,1); //Read the data from memory (no opcode)!
			if (!--left) goto dosector; //Stop when nothing left!
			++current; //Next byte in the buffer!
		}
		dosector: //Process next sector!
		//Write to disk!
		writedata_result = writedata(mounteddrives[REG_DL],&int13_buffer,((uint_64)startpos<<9)+((uint_64)sector<<9),512); //Write the data to the disk!
		if (!writedata_result) //Error?
		{
			last_status = 0x00;
			CALLBACK_SCF(1); //Error!
			goto finishupw; //Abort!
		}
		--sectors; //One sector processed!
		++sector; //Process to the next sector!
	}
	
finishupw:
	if (advancedlog)
	{
		dolog("debugger", "Written %u/%u sectors from drive %02X, start %u. Requested: Head: %u, Track: %u, Sector: %u. Start sector: %u", sector, REG_AL, REG_DL, startpos, REG_DH, REG_CH, REG_CL & 0x3F, startpos);
	}
	return (byte)sector; //Ammount of sectors read!
}

















//Now the functions!

void int13_00() //OK!
{
//Reset Disk Drive
//REG_DL=Drive

	/*
	FLAG_CF=Set on Error
	*/
	last_status = 0x00; //Reset status!
	CALLBACK_SCF(0); //No carry flag: OK!
}

void int13_01()
{
//Check Drive Status
//REG_DL=Drive
//Bit 7=0:floppy;Else fixed.

	/*
	REG_AL:
	00: Successful
	01: Invalid function in REG_AH or invalid parameter
	02: Cannot Find Address Mark
	03: Attemted Write On Write Protected Disk
	04: Sector Not Found/read error
	05: Reset Failed (hdd)
	06: Disk change line 'active' (disk changed (floppy))
	07: Drive parameter activity failed. (hdd)
	08: DMA overrun
	09: Attemt to DMA over 64kb boundary
	0A: Bad sector detected (hdd)
	0B: Bad cylinder (track) detected (hdd)
	0C: Unsupported track or invalid media
	0D: Invalid number of sectors on format (PS/2 hdd)
	0E: Control data adress mark detected (hdd)
	0F: DMA arbitration level out of range (hdd)
	10: Uncorrectable CRC or ECC error on read
	11: Data ECC corrected (hdd)
	20: Controller failure
	31: No media in drive (IBM/MS int 13 extensions)
	32: Incorrect drive type stored in CMOS (Compaq)
	40: Seek failure
	80: Drive timed out, assumed not ready
	AA: Drive not ready
	B0: Volume not locked in drive (int13 EXT)
	B1: Volume locked in drive (int13 EXT)
	B2: Volume not removable (int13 EXT)
	B3: Volume in use (int13 EXT)
	B4: Lock count exceeded (int13 EXT)
	B5: Valid eject request failed (int13 EXT)
	B6: Volume present but read protected (int13 EXT)
	BB: Undefined error (hdd)
	CC: Write fault (hdd)
	E0: Status error (hdd)
	FF: Sense operation failed (hdd)

	CF: Set on error, no error=cleared.

	*/

	if (last_status!=0x00)
	{
		if (advancedlog) dolog("debugger","Last status: %02X",last_status);
		REG_AH = last_status;
		CALLBACK_SCF(1);
	}
	else
	{
		if (advancedlog) dolog("debugger","Last status: unknown");	
		REG_AH = 0;
		CALLBACK_SCF(0);
	}
}

void int13_02()
{
//Read Sectors From Drive
//REG_AL=Sectors To Read Count
//REG_CH=Track
//REG_CL=Sector
//REG_DH=Head
//REG_DL=Drive
//REG_ES:REG_BX=Buffer Address Pointer

//HDD:
//cylinder := ( (REG_CX and 0xFF00) shr 8 ) or ( (REG_CX and 0xC0) shl 2)
//sector := REG_CX and 63;
	
	uint_64 startpos; //Start position in image!
	word cylinder;
	word sector;
	if (!REG_AL) //No sectors to read?
	{
		if (advancedlog) dolog("debugger","Nothing to read specified!");
		last_status = 0x01;
		CALLBACK_SCF(1);
		return; //Abort!
	}

	if (!is_mounted(mounteddrives[REG_DL])) //No drive image loaded?
	{
		if (advancedlog) dolog("debugger","Media not mounted:%02X!",REG_DL);
		last_status = 0x31; //No media in drive!
		CALLBACK_SCF(1);
		return;
	}

	switch (mounteddrives[REG_DL]) //Which drive?
	{
	case FLOPPY0: //Floppy 1
	case FLOPPY1: //Floppy 2
		startpos = floppy_LBA(mounteddrives[REG_DL],REG_DH,REG_CH,REG_CL&0x3F); //Floppy LBA!
		REG_AL = readdiskdata((uint_32)startpos); //Read the data to memory!
		break; //Done with floppy!
	case HDD0: //HDD1
	case HDD1: //HDD2
		cylinder = ((REG_CX&0xFF00)>>8)|((REG_CX&0xC0)<<2);
		sector = REG_CX&63; //Starts at 1!
		if (!sector) //Sector 0 is invalid?
		{
			last_status = 0x00;
			CALLBACK_SCF(1); //Error!
			return; //Break out!
		}
		startpos = (uint_32)CHS2LBA(cylinder,REG_DH,(byte)sector,HDD_HEADS,(uint_32)HDD_SECTORS(disksize(mounteddrives[REG_DL]))); //HDD LBA!

		REG_AL = readdiskdata((uint_32)startpos); //Read the data to memory!
		break; //Done with HDD!
	default:
		CALLBACK_SCF(1);
		last_status = 0x40; //Seek failure!
		return; //Unknown disk!
	}
	REG_AH = 0; //Reset REG_AH!

	//Beyond 8GB: Use function 42h

	/*
	CF:Set on error, not error=cleared.
	REG_AH=Return code
	REG_AL=Actual Sectors Read Count
	*/
}

void int13_03()
{
//Write Sectors To Drive
//REG_AL=Sectors To Write Count
//REG_CH=Track
//REG_CL=Sector
//REG_DH=Head
//REG_DL=Drive
//REG_ES:REG_BX=Buffer Address Pointer
	uint_64 startpos; //Start position in image!
	word cylinder;
	word sector;

	if (!is_mounted(mounteddrives[REG_DL])) //No drive image loaded?
	{
		last_status = 0x31; //No media in drive!
		CALLBACK_SCF(1);
		return;
	}

	REG_AL = 0; //Default: none written!
	switch (mounteddrives[REG_DL]) //Which drive?
	{
	case 0x00: //Floppy 1
	case 0x01: //Floppy 2
		startpos = floppy_LBA(mounteddrives[REG_DL],REG_DH,REG_CH,REG_CL); //Floppy LBA!

		REG_AL = writediskdata((uint_32)startpos); //Write the data to memory!
		break; //Done with floppy!
	case 0x80: //HDD1
	case 0x81: //HDD2
		cylinder = ((REG_CX&0xFF00)>>8)|((REG_CX&0xC0)<<2);
		sector = REG_CX&63;
		startpos = (uint_32)CHS2LBA(cylinder,REG_DH,(byte)sector-1,HDD_HEADS,(uint_32)HDD_SECTORS(disksize(mounteddrives[REG_DL]))); //HDD LBA!

		REG_AL = writediskdata((uint_32)startpos); //Write the data to memory!
		break; //Done with HDD!
	default:
		CALLBACK_SCF(1);
		last_status = 0x40; //Seek failure!
		return; //Unknown disk!
	}
	REG_AH = 0; //Reset REG_AH!

	/*
	CF: Set On Error; Clear If No Error
	REG_AH=Return code
	REG_AL=Actual Sectors Written Count
	*/
}















void int13_04()
{
//Verify Sectors From Drive
//REG_AL=Sectors To Verify Count
//REG_CH=Track
//REG_CL=Sector
//REG_DH=Head
//REG_DL=Drive
//REG_ES:REG_BX=Buffer Address Pointer

	/*
	CF: Set On Error, Clear On No Error
	REG_AH=Return code
	REG_AL=Actual Sectors Verified Count
	*/

	int i;
	int sectorverified; //Sector verified?
	byte sectorsverified; //Sectors verified?
	uint_64 startpos; //Start position in image!

	REG_AH = 0; //Reset!
	CALLBACK_SCF(0); //Default: OK!

	if (!REG_AL) //NO sectors?
	{
		REG_AH = 0x01;
		CALLBACK_SCF(1);
		return;
	}

	if (!is_mounted(mounteddrives[REG_DL])) //No drive image loaded?
	{
		last_status = 0x31; //No media in drive!
		CALLBACK_SCF(1);
		return;
	}

	sectorsverified = 0; //Default: none verified!
	for (i=0; i<REG_AL; i++) //Process sectors!
	{

		int readdata_result = 0;
		word cylinder;
		word sector;
		int t;

		switch (mounteddrives[REG_DL]) //Which drive?
		{
		case 0x00: //Floppy 1
		case 0x01: //Floppy 2
			startpos = floppy_LBA(mounteddrives[REG_DL],REG_DH,REG_CH,REG_CL); //Floppy LBA!

			//Detect ammount of sectors to be able to read!
			readdata_result = readdata(mounteddrives[REG_DL],&int13_buffer,(uint_32)startpos+(i<<9),512); //Read the data to memory!
			if (!readdata_result) //Read OK?
			{
				last_status = 0x05; //Error reading?
				CALLBACK_SCF(1); //Error!
			}
			sectorverified = 1; //Default: verified!
			for (t=0; t<512; t++)
			{
				if (int13_buffer[t]!=MMU_rb(CPU_SEGMENT_ES,REG_ES,REG_BX+(i<<9)+t,0,1)) //Error?
				{
					sectorverified = 0; //Not verified!
					break; //Stop checking!
				}
			}
			if (sectorverified) //Verified?
			{
				++sectorsverified; //Verified!
			}
			break; //Done with floppy!
		case 0x80: //HDD1
		case 0x81: //HDD2
			cylinder = ((REG_CX&0xFF00)>>8)|((REG_CX&0xC0)<<2);
			sector = REG_CX&63;
			startpos = CHS2LBA(cylinder,REG_DH,(byte)sector,HDD_HEADS,(uint_32)HDD_SECTORS(disksize(mounteddrives[REG_DL]))); //HDD LBA!

			readdata_result = (uint_32)readdata(mounteddrives[REG_DL],&int13_buffer,startpos,512); //Write the data from memory!
			if (!readdata_result) //Read OK?
			{
				last_status = 0x05; //Error reading?
				CALLBACK_SCF(1); //Error!
			}
			sectorverified = 1; //Default: verified!
			for (t=0; t<512; t++)
			{
				if (int13_buffer[t]!=MMU_rb(CPU_SEGMENT_ES,REG_ES,REG_BX+(i<<9)+t,0,1)) //Error?
				{
					sectorverified = 0; //Not verified!
					break; //Stop checking!
				}
			}
			if (sectorverified) //Verified?
			{
				++sectorsverified; //Verified!
			}
			break; //Done with HDD!
		default:
			CALLBACK_SCF(1);
			last_status = 0x40; //Seek failure!
			return; //Unknown disk!
		}
	}
	REG_AL = sectorsverified; //Sectors verified!
	REG_AH = 0; //No error code?
}

void int13_05()
{
//Format Track
//REG_AL=Sectors To Format Count
//REG_CH=Track
//REG_CL=Sector
//REG_DH=Head
//REG_DL=Drive
//REG_ES:REG_BX=Buffer Address Pointer

	/*
	CF: Set On Error, Clear If No Error
	REG_AH=Return code
	*/

	CALLBACK_SCF(0);
	REG_AH = 0x00;

}

void int13_06()
{
//Format Track Set Bad Sector Flags
//REG_AL=Interleave
//REG_CH=Track
//REG_CL=Sector
//REG_DH=Head
//REG_DL=Drive
	CALLBACK_SCF(1); //Error!
	REG_AH = 0; //Error!
	/*
	CF: Set On Error, Clear If No Error
	REG_AH=Return code
	*/
}

void int13_07()
{
	int13_06(); //Same!
}

void int13_08()
{
//Read Drive Parameters
//REG_DL=Drive index (1st HDD =80h; 2nd=81h; else floppy)
//REG_ES:REG_DI=Set to 0000:0000 to workaround some buggy BIOS
	/*

	[bits A:B] = bits A to B of this value

	CF: Set on Error, CLear If No Error
	REG_AH=Return code
	REG_DL=Number of Hard Disk Drives
	REG_DH=Logical last index of heads = number_of - 1 (index starts with 0)
	REG_CX=[bits 7:6][bits 15:8] logical last index of cylinders = numbert_of - 1 (because starts with 0)
	   [bits 5:0] logical last index of sectors per track = number_of (because index starts with 1)
	REG_BL=Drive type:
		01h: 360k
		02h: 1.2M
		03h: 720k
		04h: 1.44M
		05h: ??? (obscure drive type on IBM, 2.88M on at least AMI 486 BIOS)
		06h: 2.88M
		10h: ATAPI Removable Media Device
	*/
//status&ah=0x07&CF=1 on invalid drive!

	word tmpheads, tmpcyl;
	uint_64 tmpsize, tmpsect;

	if (!is_mounted(mounteddrives[REG_DL])) //No drive image loaded?
	{
		last_status = 0x31; //No media in drive!
		CALLBACK_SCF(1);
		return;
	}

	CALLBACK_SCF(0); //Reset carry flag by default!

	REG_AX = 0x00;
	REG_BL = GetBIOSType(mounteddrives[REG_DL]);

	getDiskGeometry(mounteddrives[REG_DL],&tmpheads,&tmpcyl,&tmpsect,&tmpsize); //Get geometry!

	if (FLAG_CF) //Error within disk geometry (unknown disk)?
	{
		return; //STOP!
	}

	if (tmpcyl!=0) --tmpcyl; //Cylinder count -> max!
	
	if (mounteddrives[REG_DL]==FLOPPY0 || mounteddrives[REG_DL]==FLOPPY1) //Floppy?
	{
	}
	else //HDD, custom format?
	{
		REG_CH = (byte)(tmpcyl&0xFF);
		REG_CL = (byte)(((tmpcyl>>2)&0xC0) | (tmpsect * 0x3F));
		REG_DH = (byte)tmpheads;
		last_status = 0x00;
	}
	if (REG_DL&0x80) //Harddisks
	{
		REG_DL = 0;
		if (is_mounted(HDD0)) ++REG_DL;
		if (is_mounted(HDD1)) ++REG_DL;
	}
	else //Floppy disks?
	{
		REG_DL = 0;
		if (is_mounted(FLOPPY0)) ++REG_DL;
		if (is_mounted(FLOPPY1)) ++REG_DL;
	}
}

void int13_09()
{
//HDD: Init Drive Pair Characteristics
//REG_DL=Drive
	/*
	CF: Set On Error, Clear If No Error
	REG_AH=Return code
	*/
	last_status = 0x01; //Unknown command!
	REG_AH = 0x00;
	CALLBACK_SCF(1);
}

void int13_0A()
{
//HDD: Read Long Sectors From Drive
//See function 02, but with bonus of 4 bytes ECC (Error Correction Code: =Sector Data Checksum)
	last_status = 0x01; //Unknown command!
	REG_AH = 0x00;
	CALLBACK_SCF(1);
}

void int13_0B()
{
//HDD: Write Long Sectors To Drive
	last_status = 0x01; //Unknown command!
	REG_AH = 0x00;
	CALLBACK_SCF(1);
}

void int13_0C()
{
//HDD: Move Drive Head To Cylinder
	last_status = 0x01; //Unknown command!
	REG_AH = 0x00;
	CALLBACK_SCF(1);
}

void int13_0D()
{
//HDD: Reset Disk Drives
	last_status = 0x01; //Unknown command!
	REG_AH = 0x00;
	CALLBACK_SCF(1);
}

void int13_0E()
{
//For Hard Disk on PS/2 system only: Controller Read Test
	last_status = 0x01; //Unknown command!
	REG_AH = 0x00;
	CALLBACK_SCF(1);
}

void int13_0F()
{
//For Hard Disk on PS/2 system only: Controller Write Test
	last_status = 0x01; //Unknown command!
	REG_AH = 0x00;
	CALLBACK_SCF(1);
}

void int13_10()
{
//HDD: Test Whether Drive Is Ready
	last_status = 0x01; //Unknown command!
	REG_AH = 0x00;
	CALLBACK_SCF(1);
}

void int13_11()
{
//HDD: Recalibrate Drive
	last_status = 0x01; //Unknown command!
	REG_AH = 0x00;
	CALLBACK_SCF(1);
}

void int13_12()
{
//For Hard Disk on PS/2 system only: Controller RAM Test
	last_status = 0x01; //Unknown command!
	REG_AH = 0x00;
	CALLBACK_SCF(1);
}

void int13_13()
{
//For Hard Disk on PS/2 system only: Drive Test
	last_status = 0x01; //Unknown command!
	REG_AH = 0x00;
	CALLBACK_SCF(1);
}

void int13_14()
{
//HDD: Controller Diagnostic
	last_status = 0x01; //Unknown command!
	REG_AH = 0x00;
	CALLBACK_SCF(1);
}

void int13_15()
{
//Read Drive Type
	last_status = 0x01; //Unknown command!
	REG_AH = 0x00;
	CALLBACK_SCF(1);
}

void int13_16()
{
//Floppy disk: Detect Media Change
	last_status = 0x01; //Unknown command!
	REG_AH = 0x00;
	CALLBACK_SCF(1);
}

void int13_17()
{
//Floppy Disk: Set Media Type For Format (used by DOS versions <= 3.1)
	killRead = TRUE;
	last_status = 0x01; //Unknown command!
	REG_AH = 0x00;
	CALLBACK_SCF(1);
}

void int13_18()
{
//Floppy Disk: Set Media Type For Format (used by DOS versions <= 3.2)
	last_status = 0x01; //Unknown command!
	REG_AH = 0; //We're unsupported!
	CALLBACK_SCF(1);
}

void int13_19()
{
//Park Heads
	last_status = 0x01; //Unknown command!
	REG_AH = 0; //We're unsupported!
	CALLBACK_SCF(1);
}

void int13_41()
{
//EXT: Check Extensions Present
//REG_DL=Drive index (1st HDD=80h etc.)
//REG_BX=55AAh
	/*
	CF: Set On Not Present, Clear If Present
	REG_AH=Error Code or Major Version Number
	REG_BX=AA55h
	REG_CX=Interfact support bitmask:
		1: Device Access using the packet structure
		2: Drive Locking and Ejecting
		4: Enhanced Disk Drive Support (EDD)
	*/
	last_status = 0x01; //Unknown command!
	REG_AH = 0; //We're unsupported!
	REG_BX = 0xAA55;
	REG_CX = 0;
	CALLBACK_SCF(1);
}

void int13_42()
{
//EXT: Extended Read Sectors From Drive
//REG_DL=Drive index (1st HDD=80h etc.)
//REG_DS;REG_SI=Segment:offset pointer to the DAP:
	/*
		DAP:
		Offset range	Size	Description
		00h		1byte	size of DAP = 16 = 10h
		01h		1byte	unused, should be 0
		02h		2bytes	number of sectors to be read
		04h		4bytes	Segment:offset pointer to the memory buffer; note that x86 has first offset bytes (word), next segment!
		08h		8bytes	Absolute number of the start of the sectors to be read (first sector of drive has number 0)
	*/
	/*
	CF: Set on Error, Clear if No Error
	REG_AH=Return code
	*/
	last_status = 0x01; //Unknown command!
	REG_AH = 0; //We're unsupported!
	CALLBACK_SCF(1);
}

void int13_43()
{
//EXT: Write Sectors To Drive
	last_status = 0x01; //Unknown command!
	REG_AH = 0; //We're unsupported!
	CALLBACK_SCF(1);
}

void int13_44()
{
//EXT: Verify Sectors
	last_status = 0x01; //Unknown command!
	REG_AH = 0; //We're unsupported!
	CALLBACK_SCF(1);
}

void int13_45()
{
//EXT: Lock/Unlock Drive
	last_status = 0x01; //Unknown command!
	REG_AH = 0; //We're unsupported!
	CALLBACK_SCF(1);
}

void int13_46()
{
//EXT: Eject Drive
	last_status = 0x01; //Unknown command!
	REG_AH = 0; //We're unsupported!
	CALLBACK_SCF(1);
}

void int13_47()
{
//EXT: Move Drive Head To Sector
	last_status = 0x01; //Unknown command!
	REG_AH = 0; //We're unsupported!
	CALLBACK_SCF(1);
}

void int13_48()
{
//EXT: Extended Read Drive Parameters
//REG_DL=Drive index (1st HDD=80h etc.)
//REG_DS:REG_SI=Segment:offset pointer to Result Buffer, see below
	/*
	Result buffer:
		Offset:	Size:	Description:
		00h	2bytes	Size of Result Buffer = 30 = 1Eh
		02h	2bytes	information flags
		04h	4bytes	physical number of cylinders = last index + 1 (index starts with 0)
		08h	4bytes	physical number of heads = last index + 1 (index starts with 0)
		0Ch	4bytes	physical number of sectors per track = last index (index starts with 1)
		10h	8bytes	absolute number of sectors = last index + 1 (index starts with 0)
		18h	2bytes	bytes per sector
		1Ah	4bytes	Optional pointer to Enhanced Disk Drive (EDD) configuration parameters which may be used for subsequent interrupt 13h extension calls (if supported)
	*/
	/*
	CF: Set On Error, Clear If No Error
	REG_AH=Return code
	*/
	last_status = 0x01; //Unknown command!
	REG_AH = 0; //We're unsupported!
	CALLBACK_SCF(1);
//Remark: Physical CHS values of function 48h may/should differ from logical values of function 08h
}

void int13_49()
{
//EXT: Detect Media Change
	last_status = 0x01; //Unknown command!
	REG_AH = 0; //We're unsupported!
	CALLBACK_SCF(1);
}

void int13_unhandled()
{
	last_status = 0x01; //Unknown command!
	CALLBACK_SCF(1);
}

void int13_unimplemented()
{
	last_status = 0x01; //Unimplemented is unhandled in essence!
	CALLBACK_SCF(1);
}

Handler int13Functions[0x50] =
{
//0x00
	int13_00,
	int13_01,
	int13_02,
	int13_03,
	int13_04,
	int13_unimplemented,
	int13_unimplemented,
	int13_unimplemented,
	int13_08,
	int13_unimplemented,
	int13_unimplemented,
	int13_unimplemented,
	int13_unimplemented,
	int13_unimplemented,
	int13_unimplemented,
	int13_unimplemented,
//0x10
	int13_11,
	int13_unimplemented,
	int13_unimplemented,
	int13_unimplemented,
	int13_unimplemented,
	int13_unimplemented,
	int13_17,
	int13_unimplemented,
	int13_unimplemented,
	int13_unimplemented,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
//0x20
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
//0x30
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
//0x40
	int13_unhandled,
	int13_unimplemented,
	int13_unimplemented,
	int13_unimplemented,
	int13_unimplemented,
	int13_unimplemented,
	int13_unimplemented,
	int13_unimplemented,
	int13_unimplemented,
	int13_unimplemented,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled,
	int13_unhandled
}; //Interrupt 13h functions!

void BIOS_int13() //Interrupt #13h (Low Level Disk Services)!
{
	if (__HW_DISABLED) return; //Abort!
	if (REG_AH<NUMITEMS(int13Functions)) //Within range of functions support?
	{
		if (advancedlog) dolog("debugger","INT13 Function %02X called.",REG_AH); //Log our function call!
		int13Functions[REG_AH](); //Execute call!
	}
	else
	{
		if (advancedlog) dolog("debugger","Unknown call: %02X",REG_AH); //Unknown call!
//Unknown int13 call?
		last_status = 1; //Status: Invalid command!
		REG_AH = 0;
		CALLBACK_SCF(1); //Set carry flag to indicate error
	}
}