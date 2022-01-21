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

#ifndef TEXTMODEDATA_H
#define TEXTMODEDATA_H

//8, 14 and 16 pixels high.
extern byte int10_font_08[256 * 8];
#ifdef UNIPCEMU
extern byte int10_font_14[256 * 14];
extern byte int10_font_16[256 * 16];
#endif
#endif