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

#ifndef DYNAMICIMAGE_H
#define DYNAMICIMAGE_H

#include "headers/types.h" //Basic types!

byte is_dynamicimage(char *filename); //Is dynamic image, 1=Dynamic, 0=Static/non-existant!
byte dynamicimage_writesector(char *filename,uint_32 sector, void *buffer); //Write a 512-byte sector! Result=1 on success, 0 on error!
byte dynamicimage_readsector(char *filename,uint_32 sector, void *buffer); //Read a 512-byte sector! Result=1 on success, 0 on error!
FILEPOS dynamicimage_getsize(char *filename);
byte dynamicimage_getgeometry(char *filename, word *cylinders, word *heads, word *SPT);
byte dynamictostatic_imagetype(char *filename);

FILEPOS generateDynamicImage(char *filename, FILEPOS size, int percentagex, int percentagey, byte dynamicimagetype); //Generate dynamic image; returns size.
byte dynamicimage_readexistingsector(char *filename,uint_32 sector, void *buffer); //Has a 512-byte sector! Result=1 on present&filled(buffer filled), 0 on not present or error! Used for simply copying the sector to a different image!
sbyte dynamicimage_nextallocatedsector(char *filename, uint_32 *sector); //Finds the next allocated sector. 0=EOF reached, 1=Found sector, -1=Invalid or corrupt file.
#endif