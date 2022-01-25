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

#ifndef BOOT_H
#define BOOT_H

//Boot successfull or error?
#define BOOT_OK TRUE
#define BOOT_ERROR FALSE

//CD-ROM boot image for when booting. (is written to from cd-rom, destroyed at closing the emulator)
#define BOOT_CD_IMG "tmpcdrom.img"

int CPU_boot(int device); //Boots from an i/o device (result TRUE: booted, FALSE: unable to boot/unbootable/read error etc.)!

#endif