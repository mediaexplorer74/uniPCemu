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
#include "headers/fopen64.h" //64-bit fopen support!
#include "headers/hardware/floppy.h" //Disk image size support!
#include "headers/cpu/modrm.h" //MODR/M support for boot loader creation.
#include "headers/emu/gpu/gpu_text.h" //For locking the text surface!
#include "headers/hardware/ide.h" //Geometry support!
#include "headers/emu/directorylist.h" //Directory list support.
#include "headers/emu/gpu/gpu_emu.h" //Text locking and output support!

byte is_staticimage(char *filename)
{
	char sidefile[256];
	BIGFILE *f;
	f = emufopen64(filename, "rb"); //Open file!
	if (!f)
	{
		return 0; //Invalid file: file not found!
	}
	if (emufseek64(f, 0, SEEK_END)) //Failed to seek to EOF?
	{
		emufclose64(f);
		return 0; //Not static!
	}
	int_64 filesize;
	filesize = emuftell64(f); //Get the file size!
	emufclose64(f); //Close the file!
	if (filesize <= 0) //Invalid size or empty?
	{
		return 0; //Not static: invalid file size!
	}
	if (((filesize >> 9) << 9) != filesize) //Not a multiple of 512 bytes?
	{
		return 0; //Not static: invalid sector size!
	}
	memset(&sidefile,0,sizeof(sidefile));
	safestrcpy(sidefile,sizeof(sidefile),filename);
	safestrcat(sidefile,sizeof(sidefile),".bochs.txt"); //Bochs compatibility enabled?
	if (file_exists(sidefile)) //Compatible mode?
	{
		return 1; //We're a static image: we're a multiple of 512 bytes and have contents!
	}
	safestrcpy(sidefile,sizeof(sidefile),filename);
	safestrcat(sidefile,sizeof(sidefile),".unipcemu.txt"); //UniPCemu compatibility enabled?
	if (file_exists(sidefile)) //Compatible mode?
	{
		return 2; //We're a static image: we're a multiple of 512 bytes and have contents(compatible mode)!
	}

	return 3; //We're a static image: we're a multiple of 512 bytes and have contents! We're running in minimal mode!
}

FILEPOS staticimage_getsize(char *filename)
{
	if (strcmp(filename, "") == 0) return 0; //Not mountable!
	BIGFILE *f;
	f = emufopen64(filename,"rb"); //Open!
	if (!f) //Not found?
	{
		return 0; //No size!
	}
	emufseek64(f,0,SEEK_END); //Find end!
	FILEPOS result;
	result = emuftell64(f); //Current pos = size!
	emufclose64(f); //Close the file!
	return result; //Give the result!
}

byte staticimage_getgeometry(char *filename, word *cylinders, word *heads, word *SPT)
{
	uint_64 disk_size = (staticimage_getsize(filename)>>(isext(filename,"iso")?11:9)); //CD-ROM or static disk/floppy image sector count!
	switch (is_staticimage(filename)) //What type?
	{
		case 1: //Bochs format?
			*heads = 16;
			*SPT = 63;
			*cylinders = (word)(MAX(MIN((disk_size/(63*16)),0xFFFF),1));
			return 1; //OK!
		case 2: //UniPCemu format?
			HDD_classicGeometry(disk_size,cylinders,heads,SPT); //Apply classic geometry!
			return 1; //OK!
			break;
		case 3: //Auto minimal type?
			HDD_detectOptimalGeometry(disk_size,cylinders,heads,SPT); //Apply optimal geometry!
			return 1; //OK!
			break;
		default:
			break;
		}
		return 0; //Not retrieved!
}

byte statictodynamic_imagetype(char *filename)
{
	switch (is_staticimage(filename)) //What type?
	{
	case 1: //Bochs format?
		return 3; //OK!
	case 2: //UniPCemu format?
		return 1; //OK!
		break;
	case 3: //Auto minimal type?
		return 2; //OK!
		break;
	default:
		break;
	}
		return 0; //Not retrieved!
}


byte staticimage_writesector(char *filename,uint_32 sector, void *buffer) //Write a 512-byte sector! Result=1 on success, 0 on error!
{
	BIGFILE *f;
	f = emufopen64(filename,"rb+"); //Open!
	++sector; //Find the next sector!
	if (emufseek64(f, (uint_64)sector << 9, SEEK_SET)) //Invalid sector!
	{
		emufclose64(f); //Close the file!
		return 0; //Limit broken!
	}
	if (emuftell64(f) != ((int_64)sector << 9)) //Invalid sector!
	{
		emufclose64(f); //Close the file!
		return 0; //Limit broken!
	}
	--sector; //Goto selected sector!
	emufseek64(f, (uint_64)sector << 9, SEEK_SET); //Find block info!
	if (emuftell64(f) != ((int_64)sector << 9)) //Not found?
	{
		emufclose64(f); //Close the file!
		return FALSE; //Error!
	}
	if (emufwrite64(buffer,1,512,f)==512) //Written?
	{
		emufclose64(f); //Close!
		return TRUE; //OK!
	}
	emufclose64(f); //Close!
	return FALSE; //Error!
}

byte staticimage_readsector(char *filename,uint_32 sector, void *buffer) //Read a 512-byte sector! Result=1 on success, 0 on error!
{
	BIGFILE *f;
	f = emufopen64(filename,"rb"); //Open!
	emufseek64(f,(uint_64)sector<<9,SEEK_SET); //Find block info!
	if (emuftell64(f)!=((int_64)sector<<9)) //Not found?
	{
		emufclose64(f); //Close the file!
		return FALSE; //Error!
	}
	if (emufread64(buffer,1,512,f)==512) //Read?
	{
		emufclose64(f); //Close!
		return TRUE; //OK!
	}
	emufclose64(f); //Close!
	return FALSE; //Error!
}

extern char diskpath[256]; //Disk path!

byte generateStaticImageFormat(char *filename, byte format)
{
	BIGFILE *f;
	char fullfilename[256], fullfilenamebackup[256];
	memset(&fullfilename[0],0,sizeof(fullfilename)); //Init!
	memset(&fullfilenamebackup[0],0,sizeof(fullfilenamebackup)); //Init!
	safestrcpy(fullfilename,sizeof(fullfilename),filename); //The filename!
	safestrcpy(fullfilenamebackup,sizeof(fullfilenamebackup),filename); //The filename!
	switch (format) //Extra type conversion stuff?
	{
		case 1: //.bochs.txt
			//Delete leftovers, if any!
			safestrcat(fullfilename,sizeof(fullfilename),".unipcemu.txt"); //UniPCemu compatibility type!
			delete_file(NULL,fullfilename); //Remove, if present!
			safestrcpy(fullfilename,sizeof(fullfilename),fullfilenamebackup); //Restore!
			safestrcat(fullfilename,sizeof(fullfilename),".bochs.txt"); //Bochs type!
			f = emufopen64(fullfilename,"wb");
			if (!f) return 0; //Failed!
			else emufclose64(f);
			break;
		case 2: //.unipcemu.txt
			safestrcat(fullfilename,sizeof(fullfilename),".bochs.txt"); //Bochs type!
			delete_file(NULL,fullfilename); //Remove, if present!
			safestrcpy(fullfilename,sizeof(fullfilename),fullfilenamebackup); //Restore!
			safestrcat(fullfilename,sizeof(fullfilename),".unipcemu.txt"); //UniPCemu compatibility type!
			f = emufopen64(fullfilename,"wb");
			if (!f) return 0; //Failed!
			else emufclose64(f);
			break;
		default: //Neither, remove all identifier files!
			safestrcat(fullfilename,sizeof(fullfilename),".bochs.txt"); //Bochs type!
			delete_file(NULL,fullfilename); //Remove, if present!
			safestrcpy(fullfilename,sizeof(fullfilename),fullfilenamebackup); //Restore!
			safestrcat(fullfilename,sizeof(fullfilename),".unipcemu.txt"); //UniPCemu compatibility type!
			delete_file(NULL,fullfilename); //Remove, if present!
			break;
	}
	return 1; //OK!
}

void generateStaticImage(char *filename, FILEPOS size, int percentagex, int percentagey, byte format) //Generate a static image!
{
	FILEPOS sizeleft = size; //Init size left!
	byte buffer[4096]; //Buffer!
	DOUBLE percentage;
	BIGFILE *f;
	int_64 byteswritten, totalbyteswritten = 0;
	char fullfilename[256];
	char originalfilename[256];
	memset(&fullfilename[0],0,sizeof(fullfilename)); //Init!
	memset(&originalfilename[0],0,sizeof(originalfilename)); //Init!
	safestrcpy(originalfilename,sizeof(originalfilename),filename); //Backup!
	safestrcpy(fullfilename,sizeof(fullfilename),diskpath); //Disk path!
	safestrcat(fullfilename,sizeof(fullfilename),"/");
	safestrcat(fullfilename,sizeof(fullfilename),filename); //The full filename!

	domkdir(diskpath); //Make sure our directory we're creating an image in exists!

	f = emufopen64(fullfilename,"wb"); //Generate file!
	if ((percentagex!=-1) && (percentagey!=-1)) //To show percentage?
	{
		EMU_locktext();
		GPU_EMU_printscreen(percentagex,percentagey,"%2.1f%%",0.0f); //Show first percentage!
		EMU_unlocktext();
	}

	memset(&buffer, 0, sizeof(buffer)); //Clear!

	while (sizeleft) //Left?
	{
		if (shuttingdown()) //Shutting down?
		{
			//Abort creating the disk image!
			emufclose64(f);
			delete_file(diskpath, originalfilename); //Format creation failed, thus file creation failed
			return;
		}
		byteswritten = emufwrite64(&buffer,1,sizeof(buffer),f); //We've processed some!
		if (byteswritten != sizeof(buffer)) //An error occurred!
		{
			emufclose64(f); //Close the file!
			delete_file(diskpath,filename); //Remove the file!
			return; //Abort!
		}
		if ((percentagex!=-1) && (percentagey!=-1)) //To show percentage?
		{
			sizeleft -= byteswritten; //Less left to write!
			totalbyteswritten += byteswritten; //Add to the ammount processed!
			percentage = (DOUBLE)totalbyteswritten;
			percentage /= (DOUBLE)size;
			percentage *= 100.0f;
			EMU_locktext();
			GPU_EMU_printscreen(percentagex,percentagey,"%2.1f%%",(float)percentage); //Show percentage!
			EMU_unlocktext();
			#ifdef IS_PSP
				delay(0); //Allow update of the screen, if needed!
			#endif
		}
	}
	emufclose64(f);
	if (!generateStaticImageFormat(fullfilename,format)) //Generate the correct format for this disk image!
	{
		delete_file(diskpath,originalfilename); //Format creation failed, thus file creation failed!
	}
	if ((percentagex!=-1) && (percentagey!=-1)) //To show percentage?
	{
		EMU_locktext();
		GPU_EMU_printscreen(percentagex,percentagey,"%2.1f%%",100.0f); //Show percentage!
		EMU_unlocktext();
	}
}

byte deleteStaticImageCompletely(char *filename)
{
	byte format;
	if ((format = is_staticimage(filename))) //Gotten format?
	{
		char fullfilename[256], fullfilenamebackup[256];
		memset(&fullfilename[0],0,sizeof(fullfilename)); //Init!
		memset(&fullfilenamebackup[0],0,sizeof(fullfilenamebackup)); //Init!
		safestrcpy(fullfilename,sizeof(fullfilename),filename); //The filename!
		safestrcpy(fullfilenamebackup,sizeof(fullfilenamebackup),filename); //The filename!
		switch (format) //Extra type conversion stuff?
		{
			case 1: //.bochs.txt
				//Delete leftovers, if any!
				safestrcat(fullfilename,sizeof(fullfilename),".bochs.txt"); //Bochs type!
				if (!remove(fullfilenamebackup)) return 0; //Failed removing the disk image!
				if (!remove(fullfilename)) return 0; //Failed removing the leftovers!
				break;
			case 2: //.unipcemu.txt
				safestrcat(fullfilename,sizeof(fullfilename),".unipcemu.txt"); //UniPCemu compatibility type!
				if (!remove(fullfilenamebackup)) return 0; //Failed removing the disk image!
				if (!remove(fullfilename)) return 0; //Failed removing the leftovers!
				break;
			default: //Neither, remove all identifier files!
				if (!remove(fullfilenamebackup)) return 0; //Failed removing the disk image!
				break;
		}
		return 1; //OK!		
	}
	return 0; //Failed!
}

byte bootmessage[] = "This is a non-bootable disk.\r\nPress any key to reboot...\r\n\0"; //Our boot message!

void generateFloppyImage(char *filename, FLOPPY_GEOMETRY *geometry, int percentagex, int percentagey) //Generate a floppy image!
{
	if (!geometry) return; //Invalid geometry?
	FILEPOS size = (FILEPOS)geometry->KB; //Init KB!
	FILEPOS sizeleft; //Init size left!
	FILEPOS block = 0; //Current block!
	size <<= 10; //Convert kb to Kilobytes of data!
	sizeleft = size; //Load the size that's left!
	byte buffer[1024]; //Buffer!
	DOUBLE percentage;
	BIGFILE *f;
	int_64 byteswritten, totalbyteswritten = 0;
	char fullfilename[256];
	memset(&fullfilename[0],0,sizeof(fullfilename)); //Init!
	safestrcpy(fullfilename,sizeof(fullfilename), diskpath); //Disk path!
	safestrcat(fullfilename,sizeof(fullfilename), "/");
	safestrcat(fullfilename,sizeof(fullfilename), filename); //The full filename!
	domkdir(diskpath); //Make sure our directory we're creating an image in exists!
	f = emufopen64(fullfilename,"wb"); //Generate file!
	if ((percentagex!=-1) && (percentagey!=-1)) //To show percentage?
	{
		EMU_locktext();
		GPU_EMU_printscreen(percentagex,percentagey,"%2.1f%%",0.0f); //Show first percentage!
		EMU_unlocktext();
	}

	while (sizeleft) //Left?
	{
		if (!block) //First block?
		{
			//Create boot sector and first FAT entry!
			memset(&buffer, 0, sizeof(buffer)); //Clear!
			buffer[0] = 0xEB; //JMP 3E
			buffer[1] = 0x3C;
			buffer[2] = 0x90; //NOP!
			buffer[3] = 'M';
			buffer[4] = 'S';
			buffer[5] = 'W';
			buffer[6] = 'I';
			buffer[7] = 'N';
			buffer[8] = '4';
			buffer[9] = '.';
			buffer[10] = '1'; //Microsoft recommends MSWIN4.1
			word bps = 512; //Bytes per sector! Use 512 in our case!
			buffer[11] = (bps&0xFF); //This is...
			buffer[12] = (bps>>8)&0xFF; //... X bytes per sector!
			byte counter=0x80; //Maximum value for sectors per cluster!
			uint_32 countersize = 0x10000; //The size for that counter!
			word sectorsize = geometry->ClusterSize; //The sector size is actually the cluster size!
			for (;((countersize>sectorsize) && counter);)
			{
				counter >>= 1; //Try to find the cluster size if possible! Default to sector size!
				countersize >>= 1; //As the counter is divided, so is the effective size!
			}
			if (!counter) counter = 1; //Default to 512 bytes (counter value of 1)!
			buffer[13] = counter; //Sectors per cluster, multiple of 2!
			buffer[14] = 1; //This is...
			buffer[15] = 0; //Reserved sectors!
			buffer[16] = 1; //1 FAT copy!
			buffer[17] = geometry->DirectorySize&0xFF; //Number of...
			buffer[18] = (geometry->DirectorySize>>8)&0xFF; //Root directory entries!
			buffer[19] = (geometry->KB<<1)&0xFF; //Ammount of sectors on the disk, in sectors!
			buffer[20] = (geometry->KB>>7)&0xFF; //See above.
			buffer[21] = geometry->MediaDescriptorByte; //Our media descriptor byte!
			buffer[22] = geometry->FATSize&0xFF; //Number of sectors per FAT!
			buffer[23] = (geometry->FATSize>>8)&0xFF; //High byte of above.
			buffer[24] = (geometry->SPT&0xFF);
			buffer[25] = (geometry->SPT>>8)&0xFF; //Sectors per track!
			buffer[26] = (byte)geometry->sides;
			buffer[27] = (geometry->sides>>8)&0xFF; //How many sides!
			buffer[28] = 0; //No hidden...
			buffer[29] = 0; //... Sectors!
			//Now the bootstrap required!
			word bootprogram;
			word bootmessagelocation; //The location of our boot message indicator!
			bootprogram = 0x3E; //Start of the boot program!
			buffer[bootprogram++] = 0xFA; //CLI: We don't want to be interrupted!
			buffer[bootprogram++] = 0x31;
			buffer[bootprogram++] = 0xC0; //XOR AX,AX
			buffer[bootprogram++] = 0x8C; //MOV Sreg,reg
			buffer[bootprogram++] = 0xC0|(MODRM_SEG_DS<<3); //MOV DS,AX
			buffer[bootprogram++] = 0x8C; //MOV Sreg,reg
			buffer[bootprogram++] = 0xC0|(MODRM_SEG_ES<<3); //MOV ES,AX
			buffer[bootprogram++] = 0xBE; //MOV SI,imm16 ; Load the location of our message to display!
			bootmessagelocation = bootprogram; //This is where our boot message location is stored!
			buffer[bootprogram++] = 0x00; //Address to!
			buffer[bootprogram++] = 0x00; //Our text to display is inserted here!
			buffer[bootprogram++] = 0xFB; //STI: We can be interrupted again!
			
			//Start of output of our little text display!
			buffer[bootprogram++] = 0xAC; //LODSB: Load the current character!
			buffer[bootprogram++] = 0x3C; //CMP AL,imm8
			buffer[bootprogram++] = 0x00; //Are we zero?
			buffer[bootprogram++] = 0x74; //Jump if zero=End of String...
			buffer[bootprogram++] = 0x8; //Perform the next step, so skip over the output!

			//We still have input, so process it!
			buffer[bootprogram++] = 0xB4; //MOV AH,imm8
			buffer[bootprogram++] = 0xE; //We're teletyping output!
			buffer[bootprogram++] = 0xB3; //MOV BL,imm8
			buffer[bootprogram++] = 0; //Page #0!
			buffer[bootprogram++] = 0xCD; //INT
			buffer[bootprogram++] = 0x10; //Teletype a character for input!
			buffer[bootprogram++] = 0xEB; //JMP back to...
			buffer[bootprogram++] = 0xF3; //the start of our little procedure to check again!

			//Wait for a key!
			buffer[bootprogram++] = 0xB8; //MOV AX,imm16
			buffer[bootprogram++] = 0x00;
			buffer[bootprogram++] = 0x00; //Clear AX to call the interrupt!
			buffer[bootprogram++] = 0xCD; //INT
			buffer[bootprogram++] = 0x16; //Wait for input!

			//Reboot now!
			buffer[bootprogram++] = 0xCD; //INT
			buffer[bootprogram++] = 0x19; //Try next drive, else reboot normally!
			buffer[bootprogram++] = 0xEA; //We're an
			buffer[bootprogram++] = 0x00; //Non-bootable disk until overwritten by an OS!
			buffer[bootprogram++] = 0x00; //This is a ...
			buffer[bootprogram++] = 0xFF; //...
			buffer[bootprogram++] = 0xFF; //JMP to reboot instead, for safety only (when interrupt 19h returns we don't want to crash)!
			
			//Finally, our little boot message with patching it into the createn code!
			word location;
			location = bootprogram; //Load the final position of the boot program!
			location += 0x7C00; //Add the start of our segment to give the actual segment!
			buffer[bootmessagelocation] = (location&0xFF); //Low!
			buffer[bootmessagelocation+1] = (location>>8)&0xFF; //High!
			memcpy(&buffer[bootprogram],bootmessage,sizeof(bootmessage)); //Set our boot message!
			
			//Finally, our signature!
			buffer[510] = 0x55; //Signature 55 aa for booting.
			buffer[511] = 0xAA; //Signature 55 aa. Don't insert the signature: we're not bootable!
			//Now the FAT itself (empty)!
			buffer[512] = geometry->MediaDescriptorByte; //Copy of the media descriptor byte!
			buffer[513] = 0xF8; //High 4 bits of the first entry is F. The second entry contains FF8 for EOF.
			buffer[514] = 0xFF; //High 8 bits of the EOF marker!
			//The rest of the FAT is initialised to 0 (unallocated).
			block = 1; //We're starting normal data!
		}
		else if (block==1) //Second block?
		{
			memset(&buffer, 0, sizeof(buffer)); //Clear the buffer from now on!
			block = 2; //Start plain data to 0!
		}
		byteswritten = emufwrite64(&buffer,1,sizeof(buffer),f); //We've processed some!
		if (byteswritten != sizeof(buffer)) //An error occurred!
		{
			emufclose64(f); //Close the file!
			delete_file(diskpath,filename); //Remove the file!
			return; //Abort!
		}
		if ((percentagex!=-1) && (percentagey!=-1)) //To show percentage?
		{
			sizeleft -= byteswritten; //Less left to write!
			totalbyteswritten += byteswritten; //Add to the ammount processed!
			percentage = (DOUBLE)totalbyteswritten;
			percentage /= (DOUBLE)size;
			percentage *= 100.0f;
			EMU_locktext();
			GPU_EMU_printscreen(percentagex,percentagey,"%2.1f%%",(float)percentage); //Show percentage!
			EMU_unlocktext();
			#ifdef IS_PSP
				delay(0); //Allow update of the screen, if needed!
			#endif
		}
	}
	emufclose64(f);
	if ((percentagex!=-1) && (percentagey!=-1)) //To show percentage?
	{
		EMU_locktext();
		GPU_EMU_printscreen(percentagex,percentagey,"%2.1f%%",100.0f); //Show percentage!
		EMU_unlocktext();
	}
}
