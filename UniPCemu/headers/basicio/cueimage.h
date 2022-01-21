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

#ifndef CUEIMAGE_H
#define CUEIMAGE_H

#include "headers/types.h" //Basic types!

enum CDROM_MODES {
	MODE_AUDIO = 0,
	MODE_KARAOKE = 1,
	MODE_MODE1DATA = 2,
	MODE_MODEXA = 3,
	MODE_MODECDI = 4,
};

byte is_cueimage(char *filename);
FILEPOS cueimage_getsize(char *filename);
//Results of the below functions: -1: Sector not found, 0: Error, 1: Aborted(no buffer), 2+CDROM_MODES: Read a sector of said mode + 2.
int_64 cueimage_readsector(int device, byte M, byte S, byte F, void *buffer, word size); //Read a n-byte sector! Result=Type on success, 0 on error, -1 on not found!
int_64 cueimage_getgeometry(int device, byte *M, byte *S, byte *F, byte *startM, byte *startS, byte *startF, byte *endM, byte *endS, byte *endF, byte specialfeatures); //Result=Type on success, 0 on error, -1 on not found!

#endif
