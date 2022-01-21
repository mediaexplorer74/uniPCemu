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

#ifndef GPU_EMU_H
#define GPU_EMU_H

void EMU_textcolor(byte color);
void EMU_gotoxy(word x, word y);
void EMU_getxy(word *x, word *y);
uint_32 getemucol16(byte color); //Special for the emulator, like the keyboard presets etc.!
void GPU_EMU_printscreen(sword x, sword y, char *text, ...); //Direct text output (from emu)!
void EMU_clearscreen(); //Clear the screen!

//Locking support for block actions!
void EMU_locktext();
void EMU_unlocktext();

#endif