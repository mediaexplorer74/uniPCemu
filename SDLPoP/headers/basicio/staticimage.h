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

#ifndef STATICIMAGE_H
#define STATICIMAGE_H

#include "headers/types.h" //Basic types!
#include "headers/hardware/floppy.h" //Geometry support!

byte is_staticimage(char *filename); //Are we a static image?
byte staticimage_writesector(char *filename, uint_32 sector, void *buffer); //Write a 512-byte sector! Result=1 on success, 0 on error!
byte staticimage_readsector(char *filename,uint_32 sector, void *buffer); //Read a 512-byte sector! Result=1 on success, 0 on error!
FILEPOS staticimage_getsize(char *filename);
byte staticimage_getgeometry(char *filename, word *cylinders, word *heads, word *SPT);
byte statictodynamic_imagetype(char *filename);


byte deleteStaticImageCompletely(char *filename); //Remove a static disk image completely!
byte generateStaticImageFormat(char *filename, byte format); //Generate the format for a static disk image!
void generateStaticImage(char *filename, FILEPOS size, int percentagex, int percentagey, byte format); //Generate a static image!
void generateFloppyImage(char *filename, FLOPPY_GEOMETRY *geometry, int percentagex, int percentagey); //Generate a floppy image!

#endif