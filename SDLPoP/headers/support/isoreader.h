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

#ifndef ISOREADER_H
#define ISOREADER_H
#include "headers/types.h"
typedef struct
{
	int device; //The device used!
	uint_64 startpos; //Startpos within the file!
	uint_32 imagesize; //The size of the image!
	int used; //Is this info used (0 for normal image, 1 for read-only BOOTIMGINFO)
} BOOTIMGINFO;

int getBootImage(int device, char *imagefile); //Returns TRUE on bootable (image written to imagefile), else FALSE!
int getBootImageInfo(int device, BOOTIMGINFO *imagefile); //Returns TRUE on bootable (image written to imagefile), else FALSE!
#endif