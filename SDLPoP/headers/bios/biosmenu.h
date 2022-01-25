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

#ifndef BIOSMENU_H
#define BIOSMENU_H

//Cancelled!
#define FILELIST_CANCEL -1
//No files with this extension!
#define FILELIST_NOFILES -2
//Default item!
#define FILELIST_DEFAULT -3

#define ITEMLIST_MAXITEMS 1000

void allocBIOSMenu(); //Stuff that take extra video memory etc. for seperated BIOS allocation (so before MMU, because it may take it all)!
void freeBIOSMenu(); //Free up all BIOS related memory!

int CheckBIOSMenu(uint_32 timeout); //To run the BIOS Menus! Result: to reboot?
byte runBIOS(byte showloadingtext); //Run the BIOS!

typedef void(*list_information)(char *filename); //Displays information about a harddisk to mount!
int ExecuteList(int x, int y, char *defaultentry, int maxlen, list_information information_handler, int blockActions); //Runs the file list!

byte BIOS_InputAddressWithMode(byte x, byte y, char* filename, uint_32 maxlength, byte allowModeAndAddressIgnore, byte allowsegment, byte allowSingleStep); //Inputs an address with mode support.
void BIOS_Title(char* text); //Prints the BIOS title on the screen
void BIOSClearScreen(); //Resets the BIOS's screen!
void BIOSDoneScreen(); //Cleans up the BIOS's screen!
#endif