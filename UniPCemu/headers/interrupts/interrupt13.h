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

#ifndef INTERRUPT13_H
#define INTERRUPT13_H

#include "headers/types.h" //Basic type support!
void int13_init(int floppy0, int floppy1, int hdd0, int hdd1, int cdrom0, int cdrom1); //Initialise interrupt 13h functionality!

void BIOS_int13(); //Interrupt #13h: (Low Level Disk Services)! Overridable!
byte getdiskbymount(int drive); //Drive to disk converter (reverse of int13_init)!
#endif
