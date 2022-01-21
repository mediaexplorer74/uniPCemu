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

#ifndef BIOSROM_H
#define BIOSROM_H

void BIOS_registerROM();
void BIOS_freeOPTROMS();
byte BIOS_checkOPTROMS(); //Check and load Option ROMs!
int BIOS_load_ROM(byte nr);
int BIOS_load_custom(char *path, char *rom);
void BIOS_free_ROM(byte nr);
void BIOS_free_custom(char *rom);
int BIOS_load_systemROM(); //Load custom ROM from emulator itself!
void BIOS_free_systemROM(); //Release the system ROM from the emulator itself!

void BIOS_finishROMs();

int BIOS_load_VGAROM(); //Load custom ROM from emulator itself!
void BIOS_free_VGAROM();

void BIOS_DUMPSYSTEMROM(); //Dump the ROM currently set (debugging purposes)!

void BIOSROM_dumpBIOS(); /* For dumping the ROMs */
void BIOSROM_updateTimers(DOUBLE timepassed);

byte BIOS_readhandler(uint_32 offset, byte index); /* A pointer to a handler function */
byte BIOS_writehandler(uint_32 offset, byte value);    /* A pointer to a handler function */

void BIOS_flash_reset(); //Reset the BIOS flash because of hard or soft reset of PCI devices!

#endif
