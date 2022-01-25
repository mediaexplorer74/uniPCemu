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

#include "headers/types.h"

//LBA=Straight adress, much like flat memory
//CHS=Sectorized address, much like pointers.

//Convert Cylinder, Head, Sector to logical adress (LBA)

//Drive parameters:

//For nheads and nsectors, see Drive Parameters!
uint_32 CHS2LBA(word cylinder, byte head, byte sector, word nheads, uint_32 nsectors)
{
	return (sector-1)+(head*nsectors)+(cylinder*(nheads+1)*nsectors); //Calculate straight adress (LBA)
}

//Backwards comp.
void LBA2CHS(uint_32 LBA, word *cylinder, byte *head, byte *sector, word nheads, uint_32 nsectors)
{
	uint_32 cylhead;
	*sector = (byte)SAFEMOD(LBA,nsectors)+1;
	cylhead = SAFEDIV(LBA,nsectors); //Rest!
	*head = (byte)SAFEMOD(cylhead,(nheads+1));
	*cylinder = (word)SAFEDIV(cylhead,(nheads+1));
}