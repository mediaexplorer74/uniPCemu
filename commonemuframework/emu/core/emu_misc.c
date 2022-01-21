//This file is part of The Common Emulator Framework.

#include "..\commonemuframework\headers\types.h" //"headers/types.h" //Basic types!
#include "..\commonemuframework\headers\emu\gpu\gpu_text.h" //"headers/emu/gpu/gpu_text.h" //Emulator support!
#include "..\commonemuframework\headers\emu\directorylist.h" // "headers/emu/directorylist.h" //Directory listing support!
#include "..\commonemuframework\headers\emu\threads.h" //"headers/emu/threads.h" //For terminating all threads on a breakpoint!
#include "..\commonemuframework\headers\emu\gpu\gpu_framerate.h" //"headers/emu/gpu/gpu_framerate.h" //For refreshing the framerate surface only!
#include "..\commonemuframework\headers\fopen64.h" //"headers/fopen64.h" //64-bit fopen support!
#include "..\commonemuframework\headers\support\locks.h" //"headers/support/locks.h" //Locking support!

uint_32 convertrel(uint_32 src, uint_32 fromres, uint_32 tores) //Relative int conversion!
{
	DOUBLE data;
	data = (DOUBLE)src; //Load src!
	if (fromres!=0)
	{
		data /= (DOUBLE)fromres; //Divide!
	}
	else
	{
		data = 0.0f; //Clear!
	}
	data *= (DOUBLE)tores; //Generate the result!
	return (int)data; //Give the result!
}

//CONCAT support!
char concatinations_constsprintf[256];
char *constsprintf(char *text, ...)
{
	cleardata(&concatinations_constsprintf[0],sizeof(concatinations_constsprintf)); //Init!
	va_list args; //Going to contain the list!
	va_start (args, text); //Start list!
	vsnprintf (concatinations_constsprintf,sizeof(concatinations_constsprintf), text, args); //Compile list!
	va_end (args); //Destroy list!
	
	return &concatinations_constsprintf[0]; //Give the concatinated string!
}

extern GPU_TEXTSURFACE *frameratesurface; //The framerate surface!
void BREAKPOINT() //Break point!
{
	termThreads(); //Terminate all other threads!
	GPU_text_locksurface(frameratesurface);
	GPU_textgotoxy(frameratesurface,0,0); //Left-up!
	GPU_textprintf(frameratesurface,RGB(0xFF,0xFF,0xFF),RGB(0,0,0),"Breakpoint reached!");
	GPU_text_releasesurface(frameratesurface);
	renderFramerateOnly(); //Render the framerate surface only!
	unlock(LOCK_MAINTHREAD); //Make us quittable!
	dosleep(); //Stop!
}

/*

FILE_EXISTS

*/

int FILE_EXISTS(char *filename)
{
	BIGFILE *f = emufopen64(filename,"r"); //Try to open!
	if (!f) //Failed?
	{
		return 0; //Doesn't exist!
	}
	emufclose64(f); //Close the file!
	return 1; //Exists!
}

char *substr(char *s,int startpos) //Simple substr function, with string and starting position!
{
	if ((int)safestrlen(s,256)<(startpos+1)) //Out of range?
	{
		return NULL; //Nothing!
	}
	return (char *)s+startpos; //Give the string from the start position!
}

void delete_file(char *directory, char *filename)
{
	if (!filename) return; // Not a file?
	if (*filename=='*') //Wildcarding?
	{
		if (!directory) return; //Not a directory?
		char *f2 = substr(filename,1); //Take off the *!

		char direntry[256];
		byte isfile;
		DirListContainer_t dir;
		if (!opendirlist(&dir,directory,&direntry[0],&isfile,0))
		{
			return; //Nothing found!
		}
		
		/* open directory */
		do //Files left to check?
		{
			if (direntry[0] == '.') continue; //. or ..?
			if (strcmp(substr(direntry,(int)safestrlen(direntry,sizeof(direntry))-(int)safestrlen(f2,256)),f2)==0) //Match?
			{
				delete_file(directory,direntry); //Delete the file!
			}
		}
		while (readdirlist(&dir,&direntry[0],&isfile)); //Files left to check?)
		closedirlist(&dir); //Close the directory!
		return; //Don't process normally!
	}
	//Compose the full path!
	char fullpath[256];
	memset(&fullpath,0,sizeof(fullpath)); //Clear the directory!
	if (directory) //Directory specified?
	{
		safestrcpy(fullpath,sizeof(fullpath),directory);
		safestrcat(fullpath,sizeof(fullpath),"/");
	}
	else
	{
		safestrcpy(fullpath,sizeof(fullpath),""); //Start without directory, used by the filename itself!
	}
	safestrcat(fullpath,sizeof(fullpath),filename); //Full filename!

	BIGFILE *f = emufopen64(fullpath,"r"); //Try to open!
	if (f) //Found?
	{
		emufclose64(f); //Close it!
		remove(fullpath); //Remove it, ignore the result!
	}
}


/*

Safe strlen function.

*/

#if defined(IS_LINUX) && !defined(IS_ANDROID)
size_t linux_safe_strnlen(const char *s, size_t count)
{
	const char *sc;

	for (sc = s; count-- && *sc != '\0'; ++sc)
		/* nothing */;
	return sc - s;
}
#endif

uint_32 safe_strlen(const char *str, size_t size)
{
	if (str) //Valid?
	{
		//Valid address determined!
		if (!size) //Unlimited?
		{
			return (uint_32)strlen(str); //Original function with unlimited length!
		}
		#if defined(IS_LINUX) && !defined(IS_ANDROID)
		return linux_safe_strnlen(str,(size_t)size); //Limited length, faster option!
		#else
		return (uint_32)strnlen(str,(size_t)size); //Limited length, faster option!
		#endif

		char *c = (char *)str; //Init to first character!
		int length = 0; //Init length to first character!
		for (;length<(size-1);) //Continue to the limit!
		{
			//Valid, check for EOS!
			if (*c=='\0' || !*c) //EOS?
			{
				return length; //Give the length!
			}
			++length; //Increase the current position!
			++c; //Increase the position!
		}
		return (uint_32)(size-1); //We're at the limit!
	}
	return 0; //No string = no length!
}

void safe_strcpy(char *dest, size_t size, const char *src)
{
	strncpy(dest,src,(MAX(size,1)-1));
	dest[MAX(size,1)-1]=0; //Proper termination always!
}

void safe_strcat(char *dest, size_t size, const char *src)
{
	size_t length;
	if (unlikely(size == 0)) return; //Invalid size!
	if (unlikely(dest[size - 1])) //Not properly terminated?
	{
		dest[size - 1] = 0; //Terminate properly first!
	}
	if (unlikely(size == 1)) return; //Destination is too small to add anything to!
	length = safe_strlen(dest, size); //How much is used?
	if (unlikely(length >= (size-1))) //Limit already reached?
	{
		return; //Don't do anything when unavailable!
	}
	length = (size-1)-length; //How much size is available, including a NULL character in the destination?
	if (unlikely(!length)) return; //Abort if nothing can be added!
	strncat(dest, src, length); //Append source to the destination, with up to the available length only!
	dest[MAX(size, 1) - 1] = 0; //Proper termination always!
}

char safe_scatnprintfbuf[65536];  // A small buffer for the result to be contained!
void safe_scatnprintf(char *dest, size_t size, const char *src, ...)
{
	safe_scatnprintfbuf[0] = 0; //Simple init!
	safe_scatnprintfbuf[65535] = 0; //Simple init!
	va_list args; //Going to contain the list!
	va_start (args, src); //Start list!
	vsnprintf (safe_scatnprintfbuf,sizeof(safe_scatnprintfbuf), src, args); //Compile list!
	va_end (args); //Destroy list!
	safe_scatnprintfbuf[65535] = 0; //Simple finish safety!
	//Now, the buffer is loaded, append it safely!
	safe_strcat(dest, size, safe_scatnprintfbuf); //Add the buffer to concatenate to the result, safely!
}

//Same as FILE_EXISTS?
int file_exists(char *filename)
{
	return FILE_EXISTS(filename); //Alias!
}

int move_file(char *fromfile, char *tofile)
{
	int result;
	if (file_exists(fromfile)) //Original file exists?
	{
		if (file_exists(tofile)) //Destination file exists?
		{
			if ((result = remove(tofile))!=0) //Error removing destination?
			{
				return result; //Error code!
			}
		}
		return rename(fromfile,tofile); //0 on success, anything else is an error code..
	}
	return 0; //Error: file doesn't exist, so success!
}

float RandomFloat(float min, float max)
{
    if (min>max) //Needs to be swapped?
    {
	float temp;
	temp = min;
	min = max;
	max = temp;
    }

    float random;
    random = ((float) rand()) / (float) RAND_MAX;

    DOUBLE range = max - min;  
    return (float)((DOUBLE)((random*range) + (DOUBLE)min)); //Give the result within range!
}

float frand() //Floating point random
{
	return RandomFloat(FLT_MIN,FLT_MAX); //Generate a random float!
}

short RandomShort(short min, short max)
{
	float temp;
	retryOOR:
	temp = floorf(RandomFloat((float)min, (float)(((int_32)max)+1))); //Range! Take 0.9 extra on the range for equal range chance!
	if (unlikely(((short)temp)>max)) goto retryOOR; //Retry when out of range (border case)!
	return (short)LIMITRANGE(temp,min,max); //Short random generator, equal spread!
}

short shortrand() //Short random
{
	return (short)RandomShort(-SHRT_MAX,SHRT_MAX); //Short random generator!
}

uint_32 converthex2int(char* s)
{
	uint_32 result = 0; //The result!
	char* temp;
	byte tempnr;
	for (temp = s; *temp; ++temp)
	{
		switch (*temp) //What character?
		{
		case '0': tempnr = 0x0; break;
		case '1': tempnr = 0x1; break;
		case '2': tempnr = 0x2; break;
		case '3': tempnr = 0x3; break;
		case '4': tempnr = 0x4; break;
		case '5': tempnr = 0x5; break;
		case '6': tempnr = 0x6; break;
		case '7': tempnr = 0x7; break;
		case '8': tempnr = 0x8; break;
		case '9': tempnr = 0x9; break;
		case 'A': tempnr = 0xA; break;
		case 'B': tempnr = 0xB; break;
		case 'C': tempnr = 0xC; break;
		case 'D': tempnr = 0xD; break;
		case 'E': tempnr = 0xE; break;
		case 'F': tempnr = 0xF; break;
		default: //Unknown character?
			return 0; //Abort without result! Can't decode!
		}
		result <<= 4; //Shift our number high!
		result |= tempnr; //Add in our number to the resulting numbers!
	}
	return result; //Give the result we calculated!
}

//A is always the most significant part, k is the shift to apply

void shiftl256(uint_64 *a, uint_64 *b, uint_64 *c, uint_64 *d, size_t k)
{
	if (unlikely(k >= 256)) //256 bits to shift?
	{
		*a = *b = *c = *d = 0; //Clear all!
		return; //Abort!
	}
	if (k >= 64) // shifting a 64-bit integer by more than 63 bits is "undefined"
	{
		retryshiftl256:
		*a = *b;
		*b = *c;
		*c = *d;
		*d = 0;
		k -= 64;
		if (unlikely(k >= 64)) goto retryshiftl256; //Retry if needed!
	}
	if (likely(k)) //Anything to shift at all? 0-bit shifts don't work here!
	{
		//Shifting less than 64 bits!
		*a = (*a << k) | (*b >> (64 - k));
		*b = (*b << k) | (*c >> (64 - k));
		*c = (*c << k) | (*d >> (64 - k));
		*d = (*d << k);
	}
}

void shiftr256(uint_64 *a, uint_64 *b, uint_64 *c, uint_64 *d, size_t k)
{
	if (unlikely(k >= 256))
	{
		*a = *b = *c = *d = 0; //Clear all!
		return; //Abort!
	}
	if (k >= 64) // shifting a 64-bit integer by more than 63 bits is "undefined"
	{
		retryshiftr256:
		*d = *c;
		*c = *b;
		*b = *a;
		*a = 0;
		if (unlikely(k >= 64)) goto retryshiftr256; //Retry if needed!
	}
	if (likely(k)) //Anything to shift at all? 0-bit shifts don't work here!
	{
		//Shifting less than 64 bits!
		*d = (*c << (64 - k)) | (*d >> k);
		*c = (*b << (64 - k)) | (*c >> k);
		*b = (*a << (64 - k)) | (*b >> k);
		*a = (*a >> k);
	}
}

//128 bits shift based on the above.
void shiftl128(uint_64* a, uint_64* b, size_t k)
{
	if (unlikely(k >= 128))
	{
		*a = *b = 0; //Clear all!
		return; //Abort!
	}
	if (k >= 64) // shifting a 64-bit integer by more than 63 bits is "undefined"
	{
		*a = *b;
		*b = 0;
		k -= 64; //Remainde to shift!
	}
	if (likely(k)) //Anything to shift at all? 0-bit shifts don't work here!
	{
		//Shifting less than 64 bits!
		*a = (*a << k) | (*b >> (64 - k));
		*b = (*b << k);
	}
}

void shiftr128(uint_64* a, uint_64* b, size_t k)
{
	if (unlikely(k >= 128))
	{
		*a = *b = 0; //Clear all!
		return; //Abort!
	}
	if (k >= 64) // shifting a 64-bit integer by more than 63 bits is "undefined"
	{
		*b = *a;
		*a = 0;
		k -= 64; //Remainder to shift!
	}
	//Shifting less than 64 bits!
	if (likely(k)) //Anything to shift at all? 0-bit shifts don't work here!
	{
		*b = (*a << (64 - k)) | (*b >> k);
		*a = (*a >> k);
	}
}