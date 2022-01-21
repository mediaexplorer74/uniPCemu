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

#ifndef CPU_STACK_H
#define CPU_STACK_H

#include "headers/types.h"

//Information for simulating PUSH and POP on the stack!
byte checkStackAccess(uint_32 poptimes, word isPUSH, byte isdword); //How much do we need to POP from the stack?
byte checkENTERStackAccess(uint_32 poptimes, byte isdword); //How much do we need to POP from the stack?

//PUSH and POP for CPU STACK! The _BIU suffixes place a request for the BIU to handle it (and requires a response to be read, which is either the result of the operation or 1 for writes).

void CPU_PUSH8(byte val, byte is32instruction); //Push Byte!
byte CPU_PUSH8_BIU(byte val, byte is32instruction); //Push Byte!
byte CPU_POP8(byte is32instruction);
byte CPU_POP8_BIU(byte is32instruction); //Request an 8-bit POP from the BIU!

void CPU_PUSH16(word* val, byte is32instruction); //Push Word!
byte CPU_PUSH16_BIU(word* val, byte is32instruction); //Push Word!
word CPU_POP16(byte is32instruction);
byte CPU_POP16_BIU(byte is32instruction); //Pop Word!

void CPU_PUSH32(uint_32* val); //Push DWord!
byte CPU_PUSH32_BIU(uint_32* val); //Push DWord!
uint_32 CPU_POP32(); //Full stack used!
byte CPU_POP32_BIU(); //Full stack used!

//For opcode POP r/m16/32
void stack_push(byte dword); //Push 16/32-bits to stack!
void stack_pop(byte dword); //Push 16/32-bits to stack!
#endif
