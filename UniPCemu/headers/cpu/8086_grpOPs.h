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

#ifndef GRPOP_8086_H
#define GRPOP_8086_H

#include "headers/types.h" //Basic typedefs!

byte op_grp2_8(byte cnt, byte varshift);
word op_grp2_16(byte cnt, byte varshift);
void op_grp3_8();
void op_grp3_16();
void op_grp5();

#endif