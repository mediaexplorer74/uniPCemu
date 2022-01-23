#pragma warning(disable : 4996)

/***** Routines to read profile strings --  by Joseph J. Graf, modified by Superfury ******/
#include "headers/types.h" //Basic support!
#include "headers/fopen64.h" //64-bit fopen support!
#include "headers/support/iniparser.h"   /* function prototypes in here */
#include "headers/support/zalloc.h" //Memory allocation support!

//Generate (if not existing yet) and add an entry to an ini file section!
int addinientry(INIFILE_SECTION* section, char *name, char *value)
{
    INIFILE_ENTRY* currententry;
    INIFILE_ENTRY* newentry;
    if (strcmp(name, "")==0) //Empty entry name is forbidden!
    {
        return 0; //Invalid: forbidden!
    }
    currententry = section->firstentry; //Take the first entry, if any!
    for (; currententry;) //Check all entries!
    {
        if (strcmp(currententry->name, name) == 0) //Name matched?
        {
            break; //Stop searching: duplicate entry found!
        }
        currententry = currententry->nextentry; //Check the next entry!
    }
    if (currententry) //Existing entry found?
    {
        safe_strcpy(&currententry->value[0], sizeof(currententry->value), value); //Save the entry's latest data to the created file!
    }
    else //Not an existing entry?
    {
        newentry = (INIFILE_ENTRY*)zalloc(sizeof(INIFILE_ENTRY), "INIFILE_ENTRY", NULL); //Allocate an entry!
        if (newentry == NULL) //Failed to allocate?
        {
            return 0; //Failed to allocate required data!
        }
        currententry = section->firstentry; //Take the first entry!
        if (currententry) //Valid entry?
        {
            for (; currententry->nextentry;) //Next left?
            {
                currententry = currententry->nextentry; //Take the next entry while there's a next one!
            }
            //Now, we're at the final entry!
            currententry->nextentry = newentry; //The next entry is the new entry!
        }
        else //First entry?
        {
            section->firstentry = newentry; //We're the first entry!
        }
        //Fill the entry data!
        newentry->nextentry = NULL; //No next entry!
        safe_strcpy(&newentry->name[0], sizeof(newentry->name), name); //The name of the section!
        safe_strcpy(&newentry->value[0] , sizeof(newentry->value), value); //The name of the section!
    }
    return 1; //Success!
}

//Generate (if not existing yet) and add a section to an ini file container!
INIFILE_SECTION* addinisection(INI_FILE* i, char* name, char* comments)
{
    INIFILE_SECTION* currentsection;
    if (strcmp(name, "")==0) //Empty section name is forbidden!
    {
        return NULL; //Invalid: forbidden!
    }
    //First, search for the section in the already loaded file to prevent duplicates!
    currentsection = i->firstsection; //The first section!
    for (; currentsection;) //Section not found yet?
    {
        if (strcmp(currentsection->name, name) == 0) //Section name found?
        {
            break; //Stop searching!
        }
        currentsection = currentsection->nextsection; //Next section!
    }
    if (!currentsection) //Section not found?
    {
        if (i->firstsection) //Sections already exist?
        {
            currentsection = i->firstsection; //Start at the first section!
            for (; currentsection->nextsection;) //Sections left to scroll?
            {
                currentsection = currentsection->nextsection; //Scroll to the final section!
            }
            //Now on the final section!
            currentsection->nextsection = (INIFILE_SECTION*)zalloc(sizeof(INIFILE_SECTION), "INIFILE_SECTION", NULL); //Allocate a section!
            if (currentsection->nextsection == NULL) //Failed to allocate?
            {
                closeinifile((INI_FILE**)&i); //Close it!
                return NULL; //Failed to allocate required data!
            }
            currentsection = currentsection->nextsection; //Make said section active!
        }
        else //First section?
        {
            currentsection = (INIFILE_SECTION*)zalloc(sizeof(INIFILE_SECTION), "INIFILE_SECTION", NULL); //Allocate a section!
            if (currentsection == NULL) //Failed to allocate?
            {
                closeinifile((INI_FILE**)&i); //Close it!
                return NULL; //Failed to allocate required data!
            }
            i->firstsection = currentsection; //Set it to be the first section!
        }
        //Now, fill the section data!
        safe_strcpy(currentsection->name, sizeof(currentsection->name), name); //The name of the section!
        currentsection->firstentry = NULL; //No first entry!
        currentsection->nextsection = NULL; //No next entry!
        currentsection->comments = comments; //The comments to load!
    }
    return currentsection; //Give the section that's found/created!
}

INI_FILE* readinifile(char* filename) //Loads a INI file to memory without comments!
{
    BIGFILE* f;
    INI_FILE* i;
    INIFILE_SECTION* currentsection;
    char line[INI_MAX_LINE_LENGTH]; //A read line!
    char *sectionname;
    char* datastart;
    f = emufopen64(filename, "rb"); //Open the file!
    if (!f) return NULL; //Nothing to open!

    i = (INI_FILE*)zalloc(sizeof(INI_FILE), "INIFILE", NULL); //Create a container!

    if (!i) //Failed to allocate the container?
    {
        emufclose64(f); //Close it!
        return NULL; //Failed to allocate!
    }

    safe_strcpy(i->filename, sizeof(i->filename), filename); //A copy of the filename!
    i->f = f; //A copy of the file that's opened!
    i->readwritemode = 0; //Read mode!
    i->firstsection = NULL; //No first section!

    currentsection = NULL; //No active section yet!

    //Valid file, read it!
    for (; !emufeof64(f);) //Not reaced EOF yet?
    {
        if (!read_line64(f, &line[0], INI_MAX_LINE_LENGTH)) //Failed to read a line?
        {
            break; //Stop parsing!
        }
        //Now that the line is read, parse it!
        if ((line[0] == ';') || (line[0]=='\0')) //Comment or empty line?
        {
            //Ignore comments and empty lines!
        }
        else if (line[0] == '[') //Start of a section?
        {
            if (line[safe_strlen(line, sizeof(line)) - 1] == ']') //Valid section?
            {
                sectionname = &line[1]; //Start of a section name!
                line[safe_strlen(line, sizeof(line)) - 1] = '\0'; //Remove the termination!
                if (!(currentsection = addinisection(i, sectionname, NULL))) //Failed to add/continue a section in the INI file?
                {
                    closeinifile(&i); //Close the ini file!
                    return NULL; //Failed!
                }
            }
        }
        else //Normal entry?
        {
            if (currentsection) //Active section?
            {
                //Parse the entry name and data from the line!
                if (line[0]) //Anything on the line?
                {
                    datastart = &line[0]; //Start parsing here!
                    for (; datastart;++datastart) //not End of Line?
                    {
                        if (*datastart == '=') //Data seperator encountered?
                        {
                            break; //Stop searching!
                        }
                    }
                    //now datastart is the location of the seperator, if any!
                    if (*datastart) //Valid seperator?
                    {
                        if (*datastart == '=') //Valid entry found?
                        {
                            *datastart = '\0'; //Create a string seperator for the name to stop at!
                            ++datastart; //The data starts after this!
                            if (!addinientry(currentsection, &line[0], datastart)) //Failed adding the entry?
                            {
                                closeinifile(&i); //Close it!
                                return NULL; //Failed to allocate required data!
                            }
                        }
                    }
                }
            }
        }
    }

    return i; //Give the loaded ini file!
}
INI_FILE* newinifile(char* filename) //Creates a INI file to write in memory without contents!
{
    INI_FILE* i;
    i = (INI_FILE*)zalloc(sizeof(INI_FILE), "INIFILE", NULL); //Create a container!

    if (!i) //Failed to allocate the container?
    {
        return NULL; //Failed to allocate!
    }

    safe_strcpy(i->filename, sizeof(i->filename), filename); //A copy of the filename!
    i->f = NULL; //No copy of the file that's opened!
    i->readwritemode = 1; //Write mode!
    i->firstsection = NULL; //No first section!
    return i; //Give the empty ini file to fill!
}

void deallocateinisections(INI_FILE* f)
{
    INIFILE_SECTION* section, *thenextsection; //A section!
    INIFILE_ENTRY* entry, *thenextentry; //A entry!
    section = f->firstsection; //Processing section!
    for (;section;) //Sections left to deallocate?
    {
        entry = section->firstentry; //Parse the entries within!
        for (;entry;) //Entries left?
        {
            thenextentry = entry->nextentry; //What to parse next!
            freez((void**)&entry, sizeof(INIFILE_ENTRY), "INIFILE_ENTRY"); //Release the entry!
            entry = thenextentry; //Start checking the next entry!
        }
        thenextsection = section->nextsection; //The next section!
        freez((void**)&section, sizeof(INIFILE_SECTION), "INIFILE_SECTION"); //Release the entry!
        section = thenextsection; //Start checking the next section!
    }
}

int writesectioncomment(char* comment, BIGFILE* wfp)
{
    if (comment != NULL) //Gotten a comment to create as well?
    {
        if (fprintf64(wfp, "; ") != strlen("; ")) //Start a comment!
        {
            return 0; //Failed!
        }
        for (; *comment;) //Process the comment!
        {
            if (*comment == '\n') //Newline?
            {
                ++comment; //Skip the newline!
                #ifdef WINDOWS_LINEENDING
                if (fprintf64(wfp, "\n; ") != (strlen("\n; ")+1)) //Newline in the comment!
                #else
                if (fprintf64(wfp, "\n; ") != strlen("\n; ")) //Newline in the comment!
                #endif
                {
                    return 0; //Failed!
                }
            }
            else
            {
                if (fprintf64(wfp, "%c", *comment++) != 1) //Write the character!
                {
                    return 0; //Failed!
                }
            }
        }
        #ifdef WINDOWS_LINEENDING
        if (fprintf64(wfp, "\n") != (strlen("\n") + 1)) //Newline in the comment!
        #else
        if (fprintf64(wfp, "\n") != strlen("\n")) //Newline in the comment!
        #endif
        {
            return 0; //Failed!
        }
    }
    return 1; //Success!
}

int closeinifile(INI_FILE** f) //Close a new or read ini file container!
{
    INIFILE_SECTION* currentsection;
    INIFILE_ENTRY* currententry;
    byte hasprevsection;
    BIGFILE* writef;
    INI_FILE* i;
    char c;
    byte result; //The result!
    if (!f) //Error?
    {
        return 0; //Abort!
    }
    if (!*f) //invalid?
    {
        return 0; //Abort!
    }
    i = *f; //The ini file itself!
    if (i->readwritemode) //Write mode?
    {
        //Write the file to disk?
        result = 0; //What is the result of the write?
        writef = emufopen64(i->filename, "wb"); //Open the file!
        if (!writef)
        {
            goto destroywritecontainer; //Nothing to open, so failed, deallocate anyways!
        }
        i->f = writef; //What file is allocated!

        hasprevsection = 0; //Default: not a previous section present!

        //Now, write all sections and it's data to the file!
        currentsection = i->firstsection; //Parse all sections, starting with the first!
        for (; currentsection;) //Sections left to parse?
        {
            if (currentsection->firstentry) //Gotten entries in the section? Don't write empty sections!
            {
                if (hasprevsection) //Did we have a previous section? Write a section seperator!
                {
                    #ifdef WINDOWS_LINEENDING
                    if (fprintf64(i->f, "\n") != (strlen("\n") + 1)) //Newline in the comment!
                    #else
                    if (fprintf64(i->f, "\n") != strlen("\n")) //Newline in the comment!
                    #endif
                    {
                        goto destroywritecontainer; //Failed to write, abort!
                    }
                }

                hasprevsection = 1; //Starting next sections from a existing section!

                //Start up the section!
                c = '['; //Start of a section!
                if (emufwrite64(&c, 1, 1, i->f) != 1) //Failed to write start of section marker?
                {
                    goto destroywritecontainer; //Failed to write, abort!
                }
                if (emufwrite64(&currentsection->name, 1, safe_strlen(currentsection->name, sizeof(currentsection->name)), i->f) != safe_strlen(currentsection->name, sizeof(currentsection->name))) //Failed to write the section name?
                {
                    goto destroywritecontainer; //Failed to write, abort!
                }
                c = ']'; //End of a section!
                if (emufwrite64(&c, 1, 1, i->f) != 1) //Failed to write end of section marker?
                {
                    goto destroywritecontainer; //Failed to write, abort!
                }
                #ifdef WINDOWS_LINEENDING
                if (fprintf64(i->f, "\n") != (strlen("\n") + 1)) //Newline in the comment!
                #else
                if (fprintf64(i->f, "\n") != strlen("\n")) //Newline in the comment!
                #endif
                {
                    goto destroywritecontainer; //Failed to write, abort!
                }

                //First, the comments for the section next!
                if (writesectioncomment(currentsection->comments, i->f) == 0) //Write the comments!
                {
                    goto destroywritecontainer; //Failed to write, abort!
                }

                //Now, write all entries to the file!
                currententry = currentsection->firstentry; //Parse all entries, starting with the first!
                for (; currententry;)
                {
                    if (emufwrite64(&currententry->name[0], 1, safe_strlen(currententry->name, sizeof(currententry->name)), i->f)!=safe_strlen(currententry->name, sizeof(currententry->name))) //Failed to write the entry name?
                    {
                        goto destroywritecontainer; //Failed to write, abort!
                    }
                    c = '='; //Seperator of the value!
                    if (emufwrite64(&c, 1, 1, i->f) != 1) //Failed to write seperator of entry key and value?
                    {
                        goto destroywritecontainer; //Failed to write, abort!
                    }
                    if (emufwrite64(&currententry->value[0], 1, safe_strlen(currententry->value, sizeof(currententry->value)), i->f)!=safe_strlen(currententry->value, sizeof(currententry->value))) //Failed to write the entry value?
                    {
                        goto destroywritecontainer; //Failed to write, abort!
                    }
                    #ifdef WINDOWS_LINEENDING
                    if (fprintf64(i->f, "\n") != (strlen("\n") + 1)) //Newline in the comment!
                    #else
                    if (fprintf64(i->f, "\n") != strlen("\n")) //Newline in the comment!
                    #endif
                    {
                        goto destroywritecontainer; //Failed to write, abort!
                    }
                    currententry = currententry->nextentry; //Next entry to parse!
                }
            }
            currentsection = currentsection->nextsection; //Parse the next section!
        }

        result = 1; //Successfully written!

        destroywritecontainer:
        //Deallocate all structures in it!
        deallocateinisections(i); //Deallocate all sections!
        if (i->f) //File open?
        {
            emufclose64(i->f); //Close the file
        }
        freez((void**)f, sizeof(INI_FILE), "INIFILE"); //Free the ini file container itself!
        return result; //TODO: Failed!
    }
    else //Read mode?
    {
        deallocateinisections(i); //Deallocate all sections!
        emufclose64(i->f); //Close the file
        i->f = NULL; //Deallocated!
    }

    freez((void**)f, sizeof(INI_FILE), "INIFILE"); //Free the ini file container itself!
    return 1; //Successfully closed (read mode) or written and closed!
}


int get_private_profile_string(char *section, char *entry, char *def,
    char *buffer, int buffer_size, INI_FILE* f)
{
	if (buffer_size<=0) return 0; //Fail: not readable anyway!
    if (!f) return 0; //Fail: invalid file!
    INIFILE_SECTION* currentsection;
    INIFILE_ENTRY* currententry;
    currentsection = f->firstsection; //Start with the first section!
    for (; currentsection;) //Parse all sections!
    {
        if (strcmp(currentsection->name, section) == 0) //Section found?
        {
            currententry = currentsection->firstentry; //Start with the first entry!
            for (; currententry;) //Parse all entries!
            {
                if (strcmp(currententry->name, entry) == 0) //Entry found?
                {
                    safe_strcpy(buffer, buffer_size, currententry->value); //Give the value!
                    buffer[buffer_size - 1] = '\0'; //Safe termination!
                    return safe_strlen(buffer, buffer_size); //How much has been read!
                }
                currententry = currententry->nextentry; //Parse the next entry!
            }
        }
        currentsection = currentsection->nextsection; //Parse the next section!
    }
    //Failed to read! Give the default!
    safe_strcpy(buffer, buffer_size, def); //Give the default!
    return safe_strlen(buffer, buffer_size); //How much has been read!
}

int_64 get_private_profile_int64(char *section,
    char *entry, int_64 def, INI_FILE* f)
{
    char value[INI_MAX_VALUE_LENGTH];
    char ep[INI_MAX_VALUE_LENGTH];
	memset(&value,0,sizeof(value));
	memset(&ep,0,sizeof(ep));
	int i;
	byte isnegative=0;
	byte length = 0;
	get_private_profile_string(section,entry,"",&ep[0],sizeof(ep),f); //Read the entry, with default being empty!
    for(i = 0; (isdigit((int)ep[i]) || ((ep[i]=='-') && (!i))); i++ )
		if (ep[i]=='-') //Negative sign?
		{
			isnegative = 1; //Negative sign!
		}
		else
		{
	        value[length++] = ep[i];
		}
    value[length] = '\0';
	LONG64SPRINTF result;
	if (sscanf(&value[0],LONGLONGSPRINTF,&result)==1) //Convert to our result!
	{
		return result*(isnegative?-1:1); //Give the result!
	}
	else return def; //Default otherwise!
}

uint_64 get_private_profile_uint64(char *section,
    char *entry, uint_64 def, INI_FILE* f)
{
    char value[INI_MAX_VALUE_LENGTH];
	memset(&value,0,sizeof(value));
	int i;
	get_private_profile_string(section,entry,"",&value[0],sizeof(value),f); //Read the entry, with default being empty!
    for(i = 0; isdigit((int)value[i]); i++ ); //Scan until invalid characters!
    value[i] = '\0';
	LONG64SPRINTF result;
	if (sscanf(&value[0],LONGLONGSPRINTF,&result)==1) //Convert to our result!
	{
		return result; //Give the result!
	}
	else return def; //Return default!
}

int write_private_profile_string(char *section, char *section_comment,
    char *entry, char *buffer, INI_FILE* f)
{
    if (!f) return 0; //Error out!
    INIFILE_SECTION* currentsection;
    if ((currentsection = addinisection(f, section, section_comment))) //Get/create a section!
    {
        //Section is now createn, if needed! Otherwise, existant section!
        if (addinientry(currentsection, entry, buffer)) //Entry createn or updated?
        {
            return (1); //Success!
        }
        else //Failed creating an entry?
        {
            return 0; //Error out!
        }
    }
    return (0); //Couldn't create section!
}

int write_private_profile_int64(char *section, char *section_comment,
    char *entry, int_64 value, INI_FILE* f)
{
	uint_64 datasigned;
	char s[256];
	memset(&s,0,sizeof(s)); //Init!
	if (value<0) datasigned = (uint_64)-value; //Make positive if needed!
	else datasigned = (uint_64)value; //Alreadty positive!
	if (value<0) //negative?
	{
		snprintf(&s[0],sizeof(s),"-" LONGLONGSPRINTF,(LONG64SPRINTF)datasigned);
	}
	else
	{
		snprintf(&s[0],sizeof(s),LONGLONGSPRINTF,(LONG64SPRINTF)datasigned);
	}
	return write_private_profile_string(section,section_comment,entry,&s[0],f); //Write to the file, give the result!
}

int write_private_profile_uint64(char *section, char *section_comment,
    char *entry, uint_64 value, INI_FILE* f)
{
	char s[256];
	memset(&s,0,sizeof(s)); //Init!
	snprintf(&s[0],sizeof(s),LONGLONGSPRINTF,(LONG64SPRINTF)value);
	return write_private_profile_string(section,section_comment,entry,&s[0],f); //Write to the file, give the result!
}