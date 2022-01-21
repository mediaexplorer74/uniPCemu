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

#ifndef INIPARSER_H
#define INIPARSER_H
#include "headers/types.h" //Basic types!

#include "headers/fopen64.h" //64-bit file backend support!

#define INI_MAX_LINE_LENGTH 4096
#define INI_MAX_VALUE_LENGTH    256

typedef struct
{
    char name[256]; //Name of the entry!
    char value[INI_MAX_VALUE_LENGTH]; //Data of the entry!
    void* nextentry; //Next entry!
} INIFILE_ENTRY;

typedef struct
{
    char name[256]; //Name of the section!
    char* comments; //Comments of the section!
    INIFILE_ENTRY* firstentry; //First entry!
    void* nextsection; //Next section, as a linked list!
} INIFILE_SECTION;

typedef struct
{
    BIGFILE* f; //The used file!
    char filename[256]; //The used filename!
    byte readwritemode; //Read mode(0) or write mode(1)!
    INIFILE_SECTION* firstsection; //First section!
} INI_FILE;

INI_FILE* readinifile(char* filename); //Loads a INI file to memory without comments!
INI_FILE* newinifile(char* filename); //Creates a INI file to write in memory without contents!
int closeinifile(INI_FILE** f); //Close a new or read ini file container!

int get_private_profile_string(char* section, char* entry, char* def,
    char* buffer, int buffer_size, INI_FILE* f); //Core string reading!
int write_private_profile_string(char* section, char* section_comment,
    char* entry, char* buffer, INI_FILE* f); //Core string writing!
int_64 get_private_profile_int64(char *section,
    char *entry, int_64 def, INI_FILE *f);
uint_64 get_private_profile_uint64(char *section,
    char *entry, uint_64 def, INI_FILE *f);
int write_private_profile_int64(char *section, char *section_comment,
    char *entry, int_64 value, INI_FILE *f);
int write_private_profile_uint64(char *section, char *section_comment,
    char *entry, uint_64 value, INI_FILE *f);

#endif
