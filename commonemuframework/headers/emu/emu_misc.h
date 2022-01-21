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

#ifndef EMU_MISC_H
#define EMU_MISC_H

int FILE_EXISTS(char *filename);
void BREAKPOINT(); //Break point!
int move_file(char *fromfile, char *tofile); //Move a file, gives an error code or 0!
float frand(); //Floating point random
float RandomFloat(float min, float max); //Random float within range!
short shortrand(); //Short random
short RandomShort(short min, short max);
uint_32 converthex2int(char* s);

//shiftl/r128/256: A is always the most significant part, k is the shift to apply
//256 bits shift
void shiftl256 (uint_64 *a, uint_64 *b, uint_64 *c, uint_64 *d, size_t k);
void shiftr256 (uint_64 *a, uint_64 *b, uint_64 *c, uint_64 *d, size_t k);
//128 bits shift
void shiftl128(uint_64 *a, uint_64 *b, size_t k);
void shiftr128(uint_64 *a, uint_64 *b, size_t k);

#endif