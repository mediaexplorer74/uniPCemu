/*

Copyright (C) 2020 - 2021 Superfury

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

#ifndef I430FX_H
#define I430FX_H

#include "headers/types.h" //Basic types!

#ifndef IS_I430FX
extern byte is_i430fx; //Are we an i430fx motherboard?
extern byte i430fx_memorymappings_read[16]; //All read memory/PCI! Set=DRAM, clear=PCI!
extern byte i430fx_memorymappings_write[16]; //All write memory/PCI! Set=DRAM, clear=PCI!
#endif

void i430fx__SMIACT(byte active); //SMIACT# signal
void i430fx_writeaddr(byte index, byte *value); //Written an address?
void init_i430fx();
void done_i430fx();
void i430fx_MMUready(); //Memory is ready to use?

#endif
