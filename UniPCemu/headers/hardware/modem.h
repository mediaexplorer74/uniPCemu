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

#ifndef MODEM_H
#define MODEM_H

#include "headers/types.h" //Basic types!

void initModem(byte enabled); //Initialise modem!
void doneModem(); //Finish modem!

void cleanModem();
void updateModem(DOUBLE timepassed); //Sound tick. Executes every instruction.
void initPcap(); //PCAP initialization, when supported!
void termPcap(); //PCAP termination, when supported!

//Manual dialing and hangup support!
byte modem_passthrough(); //Passthrough mode enabled?
void connectModem(char* number);
byte modem_connected();
void modem_hangup(); //Hang up, if possible!

#endif