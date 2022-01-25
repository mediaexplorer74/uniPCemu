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

#ifndef PROTECTEDMODEDEBUGGING_H
#define PROTECTEDMODEDEBUGGING_H

#include "headers/types.h" //Basic types!

#define PROTECTEDMODEDEBUGGER_TYPE_EXECUTION 0x00
#define PROTECTEDMODEDEBUGGER_TYPE_DATAWRITE 0x01
#define PROTECTEDMODEDEBUGGER_TYPE_DATAREAD 0x02
#define PROTECTEDMODEDEBUGGER_TYPE_IOREADWRITE 0x03

byte checkProtectedModeDebugger(uint_32 linearaddress, byte type); //Access at memory/IO port?
void protectedModeDebugger_taskswitching(); //Task switched?
void checkProtectedModeDebuggerAfter(); //Check after instruction for the protected mode debugger!
byte protectedModeDebugger_taskswitched(); //Handle task switch debugger! Complete instruction when cleared, otherwise, handle the interrupt!
void protectedModeDebugger_updateBreakpoints(); //Update the breakpoints that are set!

#endif
