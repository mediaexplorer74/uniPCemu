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

#include "headers/bios/boot.h" //Need const comp!
#include "headers/cpu/cpu.h" //Need CPU comp!
#include "headers/cpu/mmu.h" //Need MMU comp!
#include "headers/emu/gpu/gpu.h" //Need GPU comp!
#include "headers/basicio/io.h" //Need low level I/O comp!
#include "headers/support/isoreader.h" //Need ISO reading comp!
#include "headers/bios/bios.h" //Need BIOS comp!
#include "headers/interrupts/interrupt13.h" //We need disk support!
#include "headers/cpu/protection.h" //Protection support for segment register loading!
#include "headers/cpu/easyregs.h" //Easy register support!

extern BIOS_Settings_TYPE BIOS_Settings; //Currently loaded settings!
extern IODISK disks[6]; //All mounted disks!

int customsegment = 0; //Custom segment?
extern word ISOREADER_SEGMENT; //Segment to load ISO boot record in!

byte boot_bootsector[512]; //The full boot sector to use!

int CPU_boot(int device) //Boots from an i/o device (result TRUE: booted, FALSE: unable to boot/unbootable/read error etc.)!
{
	int imagegotten = 0; //Image gotten?
	word loadedsegment = BOOT_SEGMENT;
	BOOTIMGINFO imageinfo; //The info for the image from CD-ROM!
	int emuread = 0; //Ammount of bytes read!
	word dataindex;

	loadedsegment = BOOT_SEGMENT; //Default segment!
	if (customsegment) //Use custom segment?
	{
		loadedsegment = ISOREADER_SEGMENT; //Use custom segment!
		customsegment = 0; //Disable for future boots!
	}
	switch (device) //Which device to boot?
	{
	case FLOPPY0:
	case FLOPPY1:
	case HDD0:
	case HDD1: //Floppy or hard disk?
		memset(&boot_bootsector,0,sizeof(boot_bootsector)); //Initialize the boot sector!
		if (readdata(device,&boot_bootsector,0,512)) //Read boot sector!
		{
			if ((boot_bootsector[0x1fe] != 0x55) || (boot_bootsector[0x1ff] != 0xAA)) //Valid boot sector? Not officially, but nice as an extra check!
			{
				return BOOT_ERROR; //Not booted!
			}
			for (dataindex=0;dataindex<0x200;++dataindex)
			{
				MMU_wb(-1,loadedsegment,BOOT_OFFSET+dataindex,boot_bootsector[dataindex],1); //Write the data to memory!
			}
			REG_DL = getdiskbymount(device); //Drive number we loaded from!
			CPU[activeCPU].destEIP = BOOT_OFFSET; //Where to start booting! Loaded boot sector executable!
			segmentWritten(CPU_SEGMENT_CS,loadedsegment,1); //Jump to the boot sector!
			return BOOT_OK; //Booted!
		}
		else
		{
			return BOOT_ERROR; //Not booted!
		}
	case CDROM0:
	case CDROM1: //CD-ROM?
		imagegotten = getBootImageInfo(device,&imageinfo); //Try and get the CD-ROM image info!
		if (!imagegotten) //Couldn't get boot image (not bootable?)
		{
			return BOOT_ERROR; //Not bootable!
		}

		switch (imagegotten) //Detect kind of image gotten!
		{
		case 0x01: //Diskette emulation?
			customsegment = 1; //Custom segment!
			iofloppy1(BIOS_Settings.floppy0,0,0,0); //Move floppy to next drive!
			iofloppy0("",0,0,0); //Unmount floppy0!
			memcpy(&disks[0].customdisk,&imageinfo,sizeof(imageinfo)); //Do specifications copy!
			return CPU_boot(FLOPPY0); //Boot from floppy!
		case 0x80: //HDD0 emulation?
			customsegment = 1; //Custom segment!
			iohdd1(BIOS_Settings.hdd0,0,0,0); //Move hdd further!
			memcpy(&disks[2].customdisk,&imageinfo,sizeof(imageinfo)); //Do specifications copy!
			return CPU_boot(HDD0); //Boot from harddisk image!
		case 0xFF: //No emulation?
			//First, load the image to memory!
			emuread = readdata(device,MMU_ptr(-1,ISOREADER_SEGMENT,0x7C00,0,imageinfo.imagesize),imageinfo.startpos,imageinfo.imagesize); //Read entire boot block for booting!
			if (!emuread) //Not read?
			{
				return FALSE; //Error loading data file!
			}
			REG_DL = getdiskbymount(device); //Drive number we loaded from!
			CPU[activeCPU].destEIP = (uint_32)0x7C00; //Where to start booting! Loaded boot sector executable!
			segmentWritten(CPU_SEGMENT_CS,ISOREADER_SEGMENT,1); //Jump to the boot sector!
			REG_DL = getdiskbymount(device); //Drive number we loaded from!
			return BOOT_OK; //Booted!
			break;
		default: //Unknown!
			return BOOT_ERROR; //Unknown!
		}
		break;
	default: //Unknown device or other error?
		return BOOT_ERROR; //Unknown device!
	}
}