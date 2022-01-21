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

#ifndef __SERMOUSE_H
#define __SERMOUSE_H

#include "headers/types.h" //Basic types!
#include "headers/hardware/ps2_mouse.h" //PS/2 mouse packet support!

byte useSERMouse(); //Serial mouse enabled?
void SERmouse_packet_handler(byte buttons, float* xmovemm, float* ymovemm, float* xmovemickeys, float* ymovemickeys);
void initSERMouse(byte enabled); //Inititialise serial mouse!
void doneSERMouse(); //Finish our serial mouse hardware emulation!

void updateSERmouse(float timepassed);

#endif