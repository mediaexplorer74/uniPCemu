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

#ifndef CPU_EXECUTION_H
#define CPU_EXECUTION_H
#include "headers/cpu/protection.h" //Protection typedef support!

void CPU_executionphase_init(); //Initialize the execution phase!
void CPU_executionphase_newopcode(); //Starting a new opcode to handle?
void CPU_executionphase_startinterrupt(byte vectornr, byte type, int_64 errorcode); //Starting a new interrupt to handle?
byte CPU_executionphase_starttaskswitch(int whatsegment, SEGMENT_DESCRIPTOR *LOADEDDESCRIPTOR,word *segment, word destinationtask, byte isJMPorCALL, byte gated, int_64 errorcode); //Switching to a certain task?
void CPU_OP(); //Normal CPU opcode execution!
byte CPU_executionphase_busy(); //Are we busy(not ready to fetch a new instruction)?
byte EUphasehandlerrequiresreset(); //Requires a reset to handle BIU locking mechanisms?

#endif
