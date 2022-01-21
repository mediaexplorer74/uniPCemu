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

#ifndef __DIRECTORYLIST_H
#define __DIRECTORYLIST_H

#include "..\commonemuframework\headers\types.h" //"headers/types.h" //Basic typedefs!

#if defined(IS_PSP) || defined (ANDROID) || defined(IS_LINUX) || defined(IS_VITA) || defined(IS_SWITCH)
#ifdef IS_VITA
#include <psp2/io/dirent.h> 
#else
#include <dirent.h> //PSP only required?
#endif
#endif//Are we disabled?

typedef struct
{
	char path[256]; //Full path!
	byte filtertype; //Filter files or directories enabled?
#ifdef IS_WINDOWS
	//Visual C++ and MinGW?
	TCHAR szDir[MAX_PATH];
	WIN32_FIND_DATA ffd;
	HANDLE hFind;
#else
	//PSP/Linux/Android?
#ifdef IS_VITA
	SceUID dir;
	SceIoDirent dirent;
#else
	DIR *dir;
	struct dirent* dirent;
#endif
#endif
} DirListContainer_t, *DirListContainer_p;

byte isext(char *filename, char *extension); //Are we a certain extension?
byte opendirlist(DirListContainer_p dirlist, char *path, char *entry, byte *isfile, byte filterfiles); //Open a directory for reading, give the first entry if any! filterfiles: 0=All, 1=Files, 2=Directories
byte readdirlist(DirListContainer_p dirlist, char *entry, byte *isfile); //Read an entry from the directory list! Gives the next entry, if any!
void closedirlist(DirListContainer_p dirlist); //Close an opened directory list!

#endif