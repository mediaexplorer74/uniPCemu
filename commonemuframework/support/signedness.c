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

#include "headers/types.h" //Basic type support!
//Number (signed/unsigned) conversions!

typedef union
{
	byte u; //Unsigned version!
	sbyte s; //Signed version!
} convertor8bit;

typedef union
{
	word u; //Unsigned version!
	sword s; //Signed version!
} convertor16bit;

typedef union
{
	uint_32 u; //Unsigned version!
	int_32 s; //Signed version!
} convertor32bit;

sbyte unsigned2signed8(byte u)
{
	convertor8bit convertor8;
	convertor8.u = u;
	return convertor8.s; //Give signed!
}

sword unsigned2signed16(word u)
{
	INLINEREGISTER convertor16bit convertor16;
	convertor16.u = u;
	return convertor16.s; //Give signed!
}

int_32 unsigned2signed32(uint_32 u)
{
	INLINEREGISTER convertor32bit convertor32;
	convertor32.u = u;
	return convertor32.s; //Give signed!
}

byte signed2unsigned8(sbyte s)
{
	INLINEREGISTER convertor8bit convertor8;
	convertor8.s = s;
	return convertor8.u; //Give unsigned!
}
word signed2unsigned16(sword s)
{
	INLINEREGISTER convertor16bit convertor16;
	convertor16.s = s;
	return convertor16.u; //Give unsigned!
}
uint_32 signed2unsigned32(int_32 s)
{
	INLINEREGISTER convertor32bit convertor32;
	convertor32.s = s;
	return convertor32.u; //Give unsigned!
}