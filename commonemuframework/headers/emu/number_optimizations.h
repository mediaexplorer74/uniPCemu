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

#ifndef NUMBER_OPTIMIZATIONS_H
#define NUMBER_OPTIMIZATIONS_H

uint_32 DIVMULPOW2_32(uint_32 val, uint_32 todiv, byte divide); //Find the lowest bit that's on!
unsigned int DIVMULPOW2_16(unsigned int val, unsigned int todiv, byte divide); //Find the lowest bit that's on!
unsigned int OPTMUL(unsigned int val, unsigned int multiplication);
unsigned int OPTDIV(unsigned int val, unsigned int division);
unsigned int OPTMOD(unsigned int val, unsigned int division);
uint_32 OPTMUL32(uint_32 val, uint_32 multiplication);
uint_32 OPTDIV32(uint_32 val, uint_32 division);
uint_32 OPTMOD32(uint_32 val, uint_32 division);

#endif