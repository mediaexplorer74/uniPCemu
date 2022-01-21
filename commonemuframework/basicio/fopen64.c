//This file is part of The Common Emulator Framework.

#include "..\commonemuframework\headers\types.h" //"headers/types.h" //Basic types!
#include "..\commonemuframework\headers\support\zalloc.h" //"headers/support/zalloc.h" //Zero allocation support!
#include "..\commonemuframework\headers\fopen64.h" //"headers/fopen64.h" //Our own types!

//Compatibility with SDL 1.2 builds!
#ifndef RW_SEEK_SET
#define RW_SEEK_SET SEEK_SET
#endif

#ifndef RW_SEEK_CUR
#define RW_SEEK_CUR SEEK_CUR
#endif

#ifndef RW_SEEK_END
#define RW_SEEK_END SEEK_END
#endif

#if defined(IS_LINUX) && !defined(ANDROID)
#include <fcntl.h>
#include <sys/types.h>
//Remove sleep, as it's defined by unistd.h
#undef sleep
#include <unistd.h>
#endif

/*

This is a custom PSP&PC library for adding universal 64-bit fopen support to the project using platform-specific calls.

*/

int64_t emuftell64(BIGFILE *stream)
{
	if (!stream) return -1LL; //Error!
	BIGFILE *b = (BIGFILE *)stream; //Convert!
	return b->position; //Our position!
}

int emufeof64(BIGFILE *stream)
{
	if (!stream) return 1; //EOF!
	BIGFILE *b = (BIGFILE *)stream; //Convert!
	return (b->position >= b->size) ? 1 : 0; //Our eof marker is set with non-0 values!
}

int emufseek64(BIGFILE *stream, int64_t pos, int direction)
{
	int result;
	if (!stream) return -1; //Error!
	BIGFILE *b = (BIGFILE *)stream; //Convert!
	int_64 resposition;
	int whence;
	switch (direction)
	{
	case SEEK_CUR:
		whence = RW_SEEK_CUR;
		break;
	case SEEK_END:
		whence = RW_SEEK_END;
		break;
	case SEEK_SET:
		whence = RW_SEEK_SET;
		break;
	default: //unknown?
		return -1; //Error!
		break;
	}
	if ((resposition = (int_64)SDL_RWseek(b->f, pos, whence))>=0) //Direction is constant itself!
	{
		b->position = resposition; //Use our own position indicator!
		result = 0; //OK!
	}
	else
	{
		resposition = (int_64)SDL_RWtell(b->f); //Where are we?
		#ifdef IS_WINDOWS
		if (resposition < 0) //Invalid position?
		{
			resposition &= 0xFFFFFFFFU; //Mask off invalid bits!
		}
		#endif
		//Assume what we got from RWtell is the actual current position!
		b->position = resposition; //The current position!
		result = 0; //OK!
		if ((b->position != pos) && (direction == SEEK_SET)) //Failed to seek absolute?
		{
			result = -1; //Failed after all!
		}
	}
	return result; //Give the result!
}

int emufflush64(BIGFILE *stream)
{
	if (!stream) return 1; //EOF!
	return 0; //Ignore: not supported!
}

BIGFILE *emufopen64(char *filename, char *mode)
{
	char newmode[256];
	char c[2];
	byte isappend;
	Sint64 pos;
	char *curmode;
	BIGFILE *stream;
	if (!filename) //Invalid filename?
	{
		return NULL; //Invalid filename!
	}
	if (!mode) //Invalid mode?
	{
		return NULL; //Invalid mode!
	}
	if (!safe_strlen(filename, 255)) //Empty filename?
	{
		return NULL; //Invalid filename!
	}

	stream = (BIGFILE *)zalloc(sizeof(BIGFILE), "BIGFILE", NULL); //Allocate the big file!
	if (!stream) return NULL; //Nothing to be done!

	char *modeidentifier = mode; //First character of the mode!
	if (strcmp(modeidentifier, "") == 0) //Nothing?
	{
		freez((void **)&stream, sizeof(BIGFILE), "Unused BIGFILE");
		return NULL; //Failed!
	}

	isappend = 0;
	curmode = mode;
	for (; *curmode; ++curmode) //Check the mode!
	{
		if (*curmode == 'a')
		{
			isappend = 1; //We're appending!
		}
	}
	memset(&newmode, 0, sizeof(newmode)); //Init!
	if (isappend)
	{
		c[0] = 0;
		c[1] = 0;
		curmode = mode;
		for (; *curmode; ++curmode)
		{
			if ((*curmode != 'a') && (*curmode!='+') && (*curmode!='r')) //Valid for appending?
			{
				c[0] = *curmode; //Use us!
				safestrcat(newmode,sizeof(newmode),c); //Convert to a normal read/write option instead!
			}
			else if (*curmode == 'a') //Append?
			{
				safestrcat(newmode,sizeof(newmode),"r+"); //Read and update rights!
			}
		}
		mode = &newmode[0]; //Use the new mode instead!
	}
	stream->isappending = isappend; //Are we appending?

	stream->f = SDL_RWFromFile(filename, mode); //Just call fopen!
	if (stream->f==NULL) //Failed?
	{
		freez((void **)&stream, sizeof(*stream), "fopen@InvalidStream"); //Free it!
		return NULL; //Failed!
	}

	memset(&stream->filename,0,sizeof(stream->filename)); //Init filename buffer!
	safestrcpy(&stream->filename[0],sizeof(stream->filename),filename); //Set the filename!

	//Detect file size!
	#ifndef IS_PSP
	#ifdef SDL2
	stream->size = SDL_RWsize(stream->f); //This is the size!
	//Otherwise, detect manually for SDL v1!
	#else
	stream->size = 0; //Unknown size, determine later!
	#endif
	#endif
	pos = SDL_RWtell(stream->f); //What do we start out as?
	stream->position = 0; //Default to position 0!
	if (pos > 0) //Valid position to use?
	{
		stream->position = pos;
		if (stream->size < 0) //Invalid size?
		{
			stream->size = pos; //Also the size(we're at EOF always)!
		}
	}
	else if (pos < 0) //Invalid position to use?
	{
		if (!emufseek64(stream, 0, SEEK_END)) //Goto EOF succeeded?
		{
			emufseek64(stream, 0, SEEK_SET); //Goto BOF!
		}
	}
#if (!defined(IS_PSP) && !defined(SDL2))
	//Determine the size for non-PSP SDL 1.2 builds!
	if (emufseek64(stream, 0, SEEK_END) >= 0) //Goto EOF!
	{
		stream->size = emuftell64(stream); //The file size!
		emufseek64(stream, pos, SEEK_SET); //Return to where we came from!
	}
	else
	{
		stream->size = 0; //Unknown size, assume 0!
	}
#endif
#ifdef IS_PSP
	if (emufseek64(stream, 0, SEEK_END) >= 0) //Goto EOF!
	{
		stream->size = emuftell64(stream); //The file size!
		emufseek64(stream, pos, SEEK_SET); //Return to where we came from!
	}
	else
	{
		stream->size = 0; //Unknown size, assume 0!
	}
#endif
	if (isappend) //Appending by default?
	{
		emufseek64(stream, 0, SEEK_END); //Goto EOF!
	}
	return (BIGFILE *)stream; //Opened!
}

int64_t emufwrite64(void *data,int64_t size,int64_t count,BIGFILE *stream)
{
	if (!stream) return -1; //Error!
	BIGFILE *b = (BIGFILE *)stream; //Convert!
	if (b->isappending) //Appending?
	{
		if (emuftell64(stream) != stream->size) //Not EOF?
		{
			if (emufseek64(stream, 0, SEEK_END) < 0) //Goto EOF before writing!
			{
				return 0; //Error out, we couldn't get to EOF!
			}
		}
	}
	int64_t numwritten = SDL_RWwrite(b->f, data, size, count); //Try to write, keep us compatible!

	if (numwritten>0) //No error?
	{
		b->position += numwritten*size; //Add to the position!
	}
	if (b->position>b->size) //Overflow?
	{
		b->size = b->position; //Update the size!
	}
	return numwritten; //The size written!
}

int64_t emufread64(void *data,int64_t size,int64_t count,BIGFILE *stream)
{
	if (!stream) return -1; //Error!
	BIGFILE *b = (BIGFILE *)stream; //Convert!
	Sint64 pos;
	int64_t numread = SDL_RWread(b->f, data, size, count); //Try to write, keep us compatible!

	if (stream->isappending) //Are we in appending mode?
	{
		pos = SDL_RWtell(b->f); //Where are we?
		if (pos>=0) //Where are we is a valid location?
		{
			stream->position = pos; //Update our position!
		}
		stream->isappending = 2; //Mark us as moved after appending!
	}
	else //Normal mode?
	{
		//We can safely assume our position!
		if (numread > 0) //No error?
		{
			b->position += (numread*size); //Add to the position!
		}
	}
	return numread; //The size written!
}

char fprintf64msg[4096];
char fprintf64msg2[4096 * 3]; //Up to thrice as large!

int_64 fprintf64(BIGFILE *fp, const char *format, ...)
{
	cleardata(&fprintf64msg[0],sizeof(fprintf64msg)); //Init!
	cleardata(&fprintf64msg2[0],sizeof(fprintf64msg2)); //Init!

	va_list args; //Going to contain the list!
	va_start (args, format); //Start list!
	vsnprintf (fprintf64msg,sizeof(fprintf64msg), format, args); //Compile list!
	va_end (args); //Destroy list!

	#ifdef WINDOWS_LINEENDING
	uint_32 length;
	char *c;
	c = &fprintf64msg[0]; //Convert line endings appropriately!
	length = safestrlen(fprintf64msg, sizeof(fprintf64msg)); //Length!
	for (; length;) //Anything left?
	{
		if (*c) //Not end?
		{
			if ((*c == '\n') || (*c=='\r')) //Single-byte newline?
			{
				safescatnprintf(fprintf64msg2, sizeof(fprintf64msg2), "\r\n");
			}
			else if (*c!='\r') //Normal character? Treat as a normal character!
			{
				safescatnprintf(fprintf64msg2, sizeof(fprintf64msg2), "%c", *c);
			}
		}
		++c; //Next character!
		--length; //Next character!
	}
	#else
	memcpy(&fprintf64msg2, &fprintf64msg, sizeof(fprintf64msg)); //Same!
	#endif

	if (emufwrite64(&fprintf64msg2, 1, safestrlen(fprintf64msg2, sizeof(fprintf64msg2)), fp) != safestrlen(fprintf64msg2, sizeof(fprintf64msg2)))
	{
		return -1;
	}
	return safestrlen(fprintf64msg2, sizeof(fprintf64msg2)); //Result!
}

int getc64(BIGFILE *fp)
{
	sbyte result;
	if (emufeof64(fp)) //EOF?
	{
		return EOF; //EOF reached!
	}
	if (emufread64(&result, 1, 1, fp) != 1)
	{
		return -2; //Error out!
	}
	return (int)result;
}

int read_line64(BIGFILE* fp, char* bp, uint_32 bplength)
{
	FILEPOS filepos;
	int c = (int)'\0';
	int c2 = (int)'\0';
	uint_32 i = 0;
	/* Read one line from the source file */
	*bp = '\0'; //End of line: nothing loaded yet!
	if (emufeof64(fp)) return (0); //EOF reached?
	while ((c = getc64(fp)) != 0)
	{
		if ((c == '\n') || (c == '\r')) //Carriage return?
		{
			if (c == '\n') //Unix-style line-break?
			{
				break; //Stop searching: Unix-style newline detected!
			}
			//We're either a Windows-style newline or a Macintosh-style newline!
			filepos = emuftell64(fp); //File position to return to in case we're a Macintosh-style line break!
			c2 = getc64(fp); //Try the next byte to verify either a next line or Windows-style newline!
			if ((c == '\r') && (c2 == '\n')) //Windows newline detected?
			{
				break; //Stop searching: newline detected!
			}
			else //Macintosh newline followed by any character or EOF? Ignore which one, track back and apply!
			{
				emufseek64(fp, filepos, SEEK_SET); //Track back one character or EOF to the start of the new line!
				break; //Stop seaching: newline detected!
			}
		}
		if (i > (bplength - 1)) return (-1); //Overflow detected!
		if (c == EOF)         /* return FALSE on unexpected EOF */
		{
			bp[i] = '\0'; //End of line!
			return (1); //Finish up!
		}
		if (c == -1) //Error?
		{
			bp[i] = '\0'; //End of line!
			return (0); //Finish up!
		}
		if (likely(c)) //Valid character to add?
		{
			bp[i++] = (char)c;
		}
	}
	bp[i] = '\0';
	return (1);
}

int emufclose64(BIGFILE *stream)
{
	if (!stream)
	{
		return -1; //Error!
	}
	BIGFILE *b = (BIGFILE *)stream; //Convert!
	if (!memprotect(b, sizeof(BIGFILE), "BIGFILE"))
	{
		return EOF; //Error: no file!
	}
	char filename[256];
	memset(&filename[0],0,sizeof(filename));
	safestrcpy(filename,sizeof(filename),b->filename); //Set filename!
	if (SDL_RWclose(b->f)!=0)
	{
		return EOF; //Error closing the file!
	}

	freez((void **)&b,sizeof(*b),"fclose@Free_BIGFILE"); //Free the object safely!
	if (b) //Still set?
	{
		return EOF; //Error!
	}
	return 0; //OK!
}
