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

#ifndef SUPPORT_H
#define SUPPORT_H
uint_32 CHS2LBA(word cylinder, byte head, byte sector, word nheads, uint_32 nsectors);
void LBA2CHS(uint_32 LBA, word *cylinder, byte *head, byte *sector, word nheads, uint_32 nsectors);
#endif