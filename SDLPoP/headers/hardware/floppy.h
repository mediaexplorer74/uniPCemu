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

#ifndef FLOPPY_H
#define FLOPPY_H

#include "headers/types.h" //Basic types!

typedef struct
{
	word KB;
	word SPT;
	word sides;
	byte tracks;
	byte boardjumpersetting; //The board jumper setting (0-3) for this drive!
	byte measurement; //0=5", 1=3.5"
	byte supportedrates; //Up to 4 supported rates (2 bits per rate) with this format!
	//Stuff needed for formatting the generated floppy disks:
	byte MediaDescriptorByte; //The floppy media descriptor byte!
	word ClusterSize; //Cluster size, multiple of 512 bytes!
	word FATSize; //FAT Size in sectors
	word DirectorySize; //Directory size in entries
	//More stuff for accurate emulation of errors:
	byte DoubleDensity; //Are we a double density drive?
	byte GAPLength; //The default GAP length used by this format!
	byte TapeDriveRegister; //Our Tape Drive Register value for this disk!
	word RPM; //Speed, either 300 or 360 RPM!
	char text[256]; //Text to represent this!
} FLOPPY_GEOMETRY; //All floppy geometries!

#define NUMFLOPPYGEOMETRIES 13

void initFDC(); //Initialise the floppy disk controller!

byte floppy_spt(uint_64 floppy_size);
byte floppy_tracks(uint_64 floppy_size);
byte floppy_sides(uint_64 floppy_size);
uint_32 floppy_LBA(byte floppy, word side, word track, word sector);
void updateFloppy(DOUBLE timepassed);
#endif
