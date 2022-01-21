/*

Copyright (C) 2019 - 2021 Superfury

This file is part of The Common Emulator Framework.

The Common Emulator Framework is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

The Common Emulator Framework is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with The Common Emulator Framework.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef FOPEN64_H
#define FOPEN64_H

#include "..\commonemuframework\headers\types.h" //"headers/types.h" //Basic types!

typedef struct
{
	char filename[256]; //The full filename!
	uint_64 position; //The position!
	uint_64 size; //The size!
	SDL_RWops *f;
	byte isappending;
} BIGFILE; //64-bit fopen result!

BIGFILE *emufopen64(char *filename, char *mode);
int emufseek64(BIGFILE *stream, int64_t pos, int direction);
int emufflush64(BIGFILE *stream);
int64_t emuftell64(BIGFILE *stream);
int emufeof64(BIGFILE *stream);
int64_t emufread64(void *data,int64_t size,int64_t count,BIGFILE *stream);
int64_t emufwrite64(void *data,int64_t size,int64_t count,BIGFILE *stream);
int_64 fprintf64(BIGFILE *fp, const char *format, ...);
int getc64(BIGFILE *fp);
int read_line64(BIGFILE* fp, char* bp, uint_32 bplength);
int emufclose64(BIGFILE *stream);
#endif
