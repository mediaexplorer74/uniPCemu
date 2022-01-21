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

#ifndef IO_H
#define IO_H

#include "headers/types.h"
#include "headers/support/isoreader.h" //Need for structure!

typedef byte (*SECTORHANDLER)(char *filename,uint_32 sector, void *buffer); //Write/read a 512-byte sector! Result=1 on success, 0 on error!
typedef void(*DISKCHANGEDHANDLER)(int disk); //Disk has been changed!

typedef struct
{
char filename[256]; //The filename of the disk!
char rawfilename[256]; //The unmodified filename (no paths) of the disk!
uint_64 start; //Base positions of the images in the files!
byte readonly; //Readonly!
uint_64 size; //Disk size!
BOOTIMGINFO customdisk; //Boot image info!
byte dynamicimage; //Are we a dynamic image?
byte staticimage; //Are we a static image?
byte DSKimage; //Are we a DSK image?
byte IMDimage; //Are we a IMD image?
byte cueimage; //Are we a CUE image?
SECTORHANDLER readhandler, writehandler; //Read&write handlers!
uint_32 selectedtrack; //The track selected for this disk!
uint_32 selectedsubtrack; //The subtrack selected for this disk!
byte writeErrorIsReadOnly; //Default: not written! 1=Cause of failure is R/O disk image!
} IODISK; //I/O mounted disk info.

//Basic img/ms0 input/output for BIOS I/O
#define UNMOUNTED -1
#define FLOPPY0 0x00
#define FLOPPY1 0x01
#define HDD0 0x02
#define HDD1 0x03
#define CDROM0 0x04
#define CDROM1 0x05

void ioInit(); //Resets/unmounts all disks!
void iofloppy0(char *filename, uint_64 startpos, byte readonly, uint_32 customsize);
void iofloppy1(char *filename, uint_64 startpos, byte readonly, uint_32 customsize);
void iohdd0(char *filename, uint_64 startpos, byte readonly, uint_32 customsize);
void iohdd1(char *filename, uint_64 startpos, byte readonly, uint_32 customsize);
void iocdrom0(char *filename, uint_64 startpos, byte readonly, uint_32 customsize);
void iocdrom1(char *filename, uint_64 startpos, byte readonly, uint_32 customsize);
byte readdata(int device, void *buffer, uint_64 startpos, uint_32 bytestoread);
byte writedata(int device, void *buffer, uint_64 startpos, uint_32 bytestowrite);
byte is_mounted(int drive); //Have drive?
byte drivereadonly(int drive); //Drive is read-only?
byte drivewritereadonly(int drive); //After a write, were we read-only?
FILEPOS getdisksize(int device); //Retrieve a dynamic/static image size!
byte io_getgeometry(int device, word *cylinders, word *heads, word *SPT); //Get geometry, if possible!
uint_64 disksize(int disknumber); //Currently mounted disk size!
void register_DISKCHANGE(int device, DISKCHANGEDHANDLER diskchangedhandler); //Register a disk changed handler!
char *getDSKimage(int drive); //Get DSK image filename OR NULL if not a DSK image!
char* getIMDimage(int drive); //Get IMD image filename OR NULL if not a IMD image!
char *getCUEimage(int drive); //Get CUE image filename or NULL if not a CUE image!

void CDROM_selecttrack(int device, uint_32 track); //Select a track for CD-ROM devices to read!
void CDROM_selectsubtrack(int device, uint_32 subtrack); //Select a subtrack for CD-ROM devices to read!
void requestEjectDisk(int drive); //Request for an ejectable disk to be ejected!
#endif