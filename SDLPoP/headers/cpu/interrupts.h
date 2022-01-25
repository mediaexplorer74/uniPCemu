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

#ifndef BIOS_INTERRUPTS_H
#define BIOS_INTERRUPTS_H

void CPU_setint(byte intnr, word segment, word offset); //Set real mode IVT entry!
byte CPU_INT(byte intnr, int_64 errorcode, byte is_interrupt); //Call an interrupt!
void CPU_IRET();

void BIOS_unkint(); //Unknown/unhandled interrupt (<0x20 only!)
byte CPU_customint(byte intnr, word retsegment, uint_32 retoffset, int_64 errorcode, byte is_interrupt); //Used by soft (below) and exceptions/hardware!
void CPU_INTERNAL_execNMI();

#endif