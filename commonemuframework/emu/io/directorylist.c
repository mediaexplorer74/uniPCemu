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

#include "headers/emu/directorylist.h" //Our typedefs!
#include "headers/support/log.h" //Debugging!

//Check if the entry is a file or a directory.
byte directorylist_is_file(DirListContainer_p dirlist)
{
#ifdef IS_WINDOWS
	return ((dirlist->ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0); //Is it a file?
#else
#ifdef IS_VITA
	SceUID dir;
#else
	DIR* dir; //Directory descriptor!
#endif
	char filename[256]; //Full filename!
	memset(&filename, 0, sizeof(filename)); //Init!
	safestrcpy(filename, sizeof(filename), dirlist->path); //init to path!
	safestrcat(filename, sizeof(filename), "/"); //Add a directory seperator!
#ifdef IS_VITA
	safestrcat(filename, sizeof(filename), dirlist->dirent.d_name); //Add the filename!
#else
	safestrcat(filename, sizeof(filename), dirlist->dirent->d_name); //Add the filename!
#endif
#ifdef IS_VITA
	if ((dir = sceIoDopen(filename)) < 0) //Not found as a directory?
#else
	if ((dir = opendir(filename)) == NULL) //Not found as a directory?
#endif
	{
		#ifndef IS_PSP
		//Check manually if it's a file or not!
		return file_exists(filename); //Fallback: Is it a file the manual way?
		#else
		//PSP only has files and directories, so must be a file!
		return 1; //Not a directory!
		#endif
	}
#ifdef IS_VITA
	sceIoDclose(dir); //Close the directory!
#else
	closedir(dir); //Close the directory!
#endif
	return 0; //Is a directory!
#endif
}

byte isext(char *filename, char *extension)
{
	char ext[256]; //Temp extension holder!
	if (filename == NULL) return FALSE; //No ptr!
	if (extension == NULL) return FALSE; //No ptr!
	char temp[256];
	cleardata(&temp[0], sizeof(temp));
	safestrcat(temp,sizeof(temp), "|"); //Starting delimiter!
	safestrcat(temp,sizeof(temp), extension);
	safestrcat(temp,sizeof(temp), "|"); //Finishing delimiter!
	char *curchar;
	byte result;
	uint_32 counter; //Counter!
	extension = strtok(temp, "|"); //Start token!
	for (;safe_strlen(extension,256);) //Not an empty string?
	{
		cleardata(&ext[0], sizeof(ext)); //Init!
		safestrcpy(ext,sizeof(ext), "."); //Init!
		safestrcat(ext,sizeof(ext), extension); //Add extension to compare!
		int startpos = safe_strlen(filename, 256) - safe_strlen(ext, 256); //Start position of the extension!
		result = 0; //Default: not there yet!
		if (startpos >= 0) //Available?
		{
			char *comparedata;
			comparedata = &ext[0]; //Start of the comparision!
			curchar = &filename[startpos]; //Start of the extension!
			//Now we're at the startpos. MUST MATCH ALL CHARACTERS!
			result = 1; //Default: match!
			counter = 0; //Process the complete extension!
			while ((uint_32)counter < safe_strlen(ext, 256)) //Not end of string?
			{
				//Are we equal or not?
				if (toupper((int)*curchar) != toupper((int)*comparedata)) //Not equal (case insensitive)?
				{
					result = 0; //Not extension!
					break; //Stop comparing!
				}
				++comparedata; //Next character to compare!
				++curchar; //Next character in string to compare!
				++counter; //Next position!
			}
		}
		if (result) return 1; //Found an existing extension!
		extension = strtok(NULL, "|"); //Next token!
	}

	return 0; //NOt he extension!
}

void get_filename(const wchar_t *src, char *dest)
{
	wcstombs(dest,src , 256); //Convert to viewable size!
}

byte opendirlist(DirListContainer_p dirlist, char *path, char *entry, byte *isfile, byte filterfiles) //Open a directory for reading, give the first entry if any!
{
	memset(&dirlist->path,0,sizeof(dirlist->path)); //Clear the path!
	dirlist->filtertype = filterfiles; //What to filter?
#ifdef IS_WINDOWS
	byte isafile;
	char pathtmp[256]; //Temp data!
	//Windows?
	safestrcpy(pathtmp,sizeof(pathtmp),path); //Initialise the path!
	safestrcat(pathtmp,sizeof(pathtmp),"\\*.*"); //Add the wildcard to the filename to search!

	//Create the path variable!
	safestrcpy(dirlist->szDir,sizeof(dirlist->szDir), &pathtmp[0]); //Copy the directory to use!

	dirlist->hFind = FindFirstFile(dirlist->szDir, &dirlist->ffd); //Find the first file!
	if (dirlist->hFind==INVALID_HANDLE_VALUE) //Invalid?
	{
		return 0; //Invalid handle: not usable!
	}
	//We now have the first entry, so give it!
	//get_filename(dirlist->ffd.cFileName, entry); //Convert filename found!
#ifdef strcpy_s
	strcpy_s(entry, 256, dirlist->ffd.cFileName); //Copy the filename!
#else
	safestrcpy(entry,256, dirlist->ffd.cFileName); //Copy the filename!
#endif
	isafile = directorylist_is_file(dirlist); //Are we a file?
	if (
		((dirlist->filtertype == 1) && (!isafile)) || //Directory when filtered away?
		((dirlist->filtertype == 2) && (isafile)) //File when filtered away?
		) //Filtered away?
	{
		//Skip to next entry!
		return readdirlist(dirlist, entry, isfile); //When opened, give the next entry, if any!
	}
	*isfile = isafile; //File type!
	return 1; //We have a valid file loaded!
#else
	//PSP/Linux/Android?
#ifdef IS_VITA
	if ((dirlist->dir = sceIoDopen(path))<0) //Not found?
#else
    if ((dirlist->dir = opendir (path)) == NULL) //Not found?
#endif
    {
		return 0; //No directory list: cannot open!
	}
#ifndef IS_VITA
	dirlist->dirent = NULL; //No entry yet!
#endif
	safestrcpy(dirlist->path,sizeof(dirlist->path),path); //Save the path!
	return readdirlist(dirlist,entry,isfile); //When opened, give the first entry, if any!
#endif
}
byte readdirlist(DirListContainer_p dirlist, char *entry, byte *isfile) //Read an entry from the directory list! Gives the next entry, if any!
{
	byte isafile;
#ifdef IS_WINDOWS
	//Windows?
	if (FindNextFile(dirlist->hFind, &dirlist->ffd) != 0) //Found a next file?
	{
#ifdef strcpy_s
		strcpy_s(entry,256,dirlist->ffd.cFileName); //Copy the filename!
#else
		safestrcpy(entry,256, dirlist->ffd.cFileName); //Copy the filename!
#endif
		isafile = directorylist_is_file(dirlist); //Are we a file?
		if (
			((dirlist->filtertype == 1) && (!isafile)) || //Directory when filtered away?
			((dirlist->filtertype == 2) && (isafile)) //File when filtered away?
			) //Filtered away?
		{
			//Skip to next entry!
			return readdirlist(dirlist, entry, isfile); //When opened, give the next entry, if any!
		}
		*isfile = isafile; //File type!
		return 1; //We have a valid file loaded!
	}
	return 0; //No file found!
#else
	//PSP/Linux/Android?
#ifdef IS_VITA
	if (sceIoDread(dirlist->dir, &dirlist->dirent) <= 0) //Try to read the next entry!
	{
		return 0; //We're invalid: we don't have entries anymore!
	}
#else
	dirlist->dirent = readdir(dirlist->dir); //Try to read the next entry!
#endif
#ifndef IS_VITA
	if (dirlist->dirent != NULL) //Valid entry?
#endif
	{
		isafile = directorylist_is_file(dirlist); //Does it exist as a file?
		if (
			((dirlist->filtertype == 1) && (!isafile)) || //Directory when filtered away?
			((dirlist->filtertype == 2) && (isafile)) //File type when filtered away?
			) //Filtered away?
		{
			//Skip to next entry!
			return readdirlist(dirlist, entry, isfile); //When opened, give the next entry, if any!
		}
		#ifdef IS_VITA
		safestrcpy(entry, 256, dirlist->dirent.d_name); //Set the filename!
		#else
		safestrcpy(entry, 256, dirlist->dirent->d_name); //Set the filename!
		#endif
		*isfile = isafile; //FIle type!
		return 1; //We're valid!
	}
	return 0; //We're invalid: we don't have entries anymore!
#endif
}
void closedirlist(DirListContainer_p dirlist) //Close an opened directory list!
{
#ifdef IS_WINDOWS
	FindClose(dirlist->hFind); //Close the directory!
#else
	//PSP/Linux/Android?
	#ifdef IS_VITA
	sceIoDclose(dirlist->dir); //Close the directory!
	#else
	closedir(dirlist->dir); //Close the directory!
	#endif
	memset(dirlist,0,sizeof(*dirlist)); //Clean the directory list information!
#endif
}
