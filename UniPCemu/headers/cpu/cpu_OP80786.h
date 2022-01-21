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

#ifndef CPU_OP80786_H
#define CPU_OP80786_H

void CPU786_OP0F30(); //WRMSR
void CPU786_OP0F32(); //RDMSR
void CPU786_OP0F34(); //SYSENTER
void CPU786_OP0F35(); //SYSEXIT

#endif