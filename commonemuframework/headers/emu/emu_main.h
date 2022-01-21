/*

Copyright (C) 2019 - 2021 Superfury

This file is part of The Common Emulator Framework.

The Common Emulator Framework is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

The Common Emulator Framework is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with The Common Emulator Framework.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef EMU_MAIN_H
#define EMU_MAIN_H

//For boot process! Always mode 7 for compatibility!
#define VIDEOMODE_BOOT 0x07

void finishEMU(); //Called on emulator quit.
int EMU_BIOSPOST(); //The BIOS (INT19h) POST Loader!
void finishEMU(); //Called on emulator quit.

#endif