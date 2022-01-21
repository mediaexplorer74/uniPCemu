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

#ifndef CPUSUPPORT_H
#define CPUSUPPORT_H

byte signed2unsigned8(sbyte s);
word signed2unsigned16(sword s);
uint_32 signed2unsigned32(int_32 s);

sbyte unsigned2signed8(byte u);
sword unsigned2signed16(word u);
int_32 unsigned2signed32(uint_32 u);

#endif