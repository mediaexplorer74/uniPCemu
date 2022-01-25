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
//Boot failure.

//Execute ROM BASIC which is at address 0F600h:0000h

void BIOS_int18()
{
	char msg1[256] = "Non-System disk or disk error"; //First row!
	char msg2[256] = "replace and strike any key when ready"; //Second row!
	printmsg(0xF,"%s\r\n",msg1); //First part of the message!
	printmsg(0xF,"%s\r\n",msg2); //Second part of the message!
	//Booting is left to the next step in assembly code!
}