//This file is part of The Common Emulator Framework.

#ifndef TYPESEMU_H
#define TYPESEMU_H

//Windows safety!
#ifdef RGB
//We overwrite this!
#undef RGB
#endif

#ifdef _FILE_OFFSET_BITS
#undef _FILE_OFFSET_BITS
#endif
#define _FILE_OFFSET_BITS 64

#ifdef _LARGEFILE_SOURCE
#undef _LARGEFILE_SOURCE
#endif
#define _LARGEFILE_SOURCE 1

#ifdef _LARGEFILE64_SOURCE
#undef _LARGEFILE64_SOURCE
#endif
#define _LARGEFILE64_SOURCE 1

#if defined(__linux__) || defined(ANDROID)
#ifdef _POSIX_C_SOURCE
#undef _POSIX_C_SOURCE
#endif

#define _POSIX_C_SOURCE 200808L
#endif

//Default used libraries!
#include <stdlib.h>

#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h> //C type!
#include <stdlib.h>
#include <float.h> //FLT_MAX support!
#include <math.h>
#include <limits.h>
#include <stdio.h>

#ifndef WINDOWTITLE
//Define the window title if not defined yet!
#ifdef UNIPCEMU
#define WINDOWTITLE "UniPCemu"
#else
#define WINDOWTITLE "GBemu"
#endif
#endif

#ifndef FATAL_WINDOWTITLE
//Default fatal error window title!
#define FATAL_WINDOWTITLE WINDOWTITLE
#endif

//Enable inlining if set!
#ifndef _DEBUG
//Disable inlining when debugging!
#ifndef __DISABLE_INLINE
//Only inline when not explicitely disabled!
#define __ENABLE_INLINE
#endif
#endif

//MIN/MAX: Easy calculation of min/max data!
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
//Range limiter!
#define LIMITRANGE(v,min,max)(unlikely((v)<(min))?min:(unlikely((v)>(max))?(max):(v)))

//Default long long(uint_64) definition!
#define LONGLONGSPRINTF "%llu"
#define LONGLONGSPRINTX "%016llX"
#define LONGLONGSPRINTx "%016llx"
#define LONG64SPRINTF uint_64
//Various unsigned and hex definitions suffixes for 32-bit and 64-bit
#define SPRINTF_u_UINT32 "u"
#define SPRINTF_u_UINT64 "u"
#define SPRINTF_x_UINT32 "x"
#define SPRINTF_X_UINT32 "X"
#define SPRINTF_x_UINT64 "x"
#define SPRINTF_X_UINT64 "X"

//Platform specific stuff!
#ifdef _WIN32
//Windows?
#include "..\commonemuframework\headers\types_win.h"//"headers/types_win.h" //Windows specific stuff!
#else
#ifdef __psp__
//PSP?
#include "..\commonemuframework\headers\types_psp.h" //PSP specific stuff!
#else
#ifdef __VITA__
#include "..\commonemuframework\headers\types_vita.h" //PS Vita specific stuff!
#else
#ifdef __SWITCH__
#include "..\commonemuframework\headers\types_switch.h" //Switch specific stuff!
#else
//Linux?
#include "..\commonemuframework\headers\types_linux.h" //Linux specific stuff!
#endif
#endif
#endif
#endif

#include "..\commonemuframework\headers\sdl_rest.h" //"headers/sdl_rest.h" //Rest SDL support!

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
//We're compiling for a Big-Endian CPU!
#define IS_BIG_ENDIAN
#endif

#ifdef IS_LONGDOUBLE
//Use long DOUBLE!
#define DOUBLE long double
#else
#define DOUBLE double
#define IS_FLOATDOUBLE
#endif

//Univeral 8-bit character type? Given as a define!
#define CharacterType char
//Our basic functionality we need for running this program!
//We have less accuracy using SDL delay: ms instead of us. Round to 0ms(minimal time) if needed!

//Semaphores not defined yet?
#ifndef WaitSem
#define WaitSem(s) SDL_SemWait(s);
#define PostSem(s) SDL_SemPost(s);
#endif

//Halt is redirected to the exit function!
#define quitemu exit

//The port used for emulator callbacks! Must be DWORD-aligned to always archieve correct behaviour!
#define IO_CALLBACKPORT 0xEC

//Short versions of 64-bit integers!
#define u64 uint_64
#define s64 int_64

//Below is a definition of an universal bzero function(UniPCemu replacement). v is a void pointer always!
#define cleardata(v,size) memset((v),0,(size))

#define EXIT_PRIORITY 0x11
//Exit priority, higest of all!

typedef uint8_t byte;
typedef uint16_t word;
typedef int8_t sbyte; //Signed!
typedef int16_t sword; //Signed!

#define TRUE 1
#define FALSE 0

typedef uint_64 FILEPOS;

//RGB, with and without A (full)

#ifndef IS_GPU
//Pixel component information as determined by the system!
extern byte rshift, gshift, bshift, ashift; //All shift values!
extern uint_32 rmask, gmask, bmask, amask; //All mask values!
#endif

#define RGBA(r, g, b, a) (((a)<<ashift)|((b)<<bshift)|((g)<<gshift)|((r)<<rshift))
#define GETR(x) (((x)&rmask)>>rshift)
#define GETG(x) (((x)&gmask)>>gshift)
#define GETB(x) (((x)&bmask)>>bshift)
#define GETA(x) (((x)&amask)>>ashift)

#ifdef RGB
//We're overwriting default RGB functionality, so remove RGB definition!
#undef RGB
#endif

//RGB is by default fully opaque
#define RGB(r, g, b) RGBA((r),(g),(b),SDL_ALPHA_OPAQUE)

#ifndef IS_GPU
extern uint_32 transparentpixel; //Our define!
#endif

//Special transparent pixel!
#define TRANSPARENTPIXEL transparentpixel

typedef void (*Handler)();    /* A pointer to a handler function */

//Ammount of items in a buffer!
#define NUMITEMS(buffer) (sizeof(buffer)/sizeof(buffer[0]))
//Timeout for shutdown: force shutdown!
//When set to 0, shutdown immediately shuts down, ignoring the emulated machine!
#define SHUTDOWN_TIMEOUT 60000000ULL

//Overlap detection, both on a(beginning and end coordinates) and w(beginning coordinates and width). x=first coordinate/width/endcoord, y=second coordinate/width/endcoord
#define isoverlappinga(x1,x2,y1,y2) (((x2)>=(y1)) && ((y2)>=(x1)))
#define isoverlappingw(x1,xw,y1,yw) (isoverlappinga((x1),((x1)+(xw)-1),(y1),((y1)+(yw)-1)) && (xw) && (yw))

//Optimized DIV/MUL when possible.
//SAFEDIV/MOD: Safe divide/modulo function. Divide by 0 is caught into becoming 0!
#define SAFEDIVUINT(x,divideby) ((!(divideby))?0:OPTDIV(x,divideby))
#define SAFEMODUINT(x,divideby) ((!(divideby))?0:OPTMOD(x,divideby))
#define SAFEDIVUINT32(x,divideby) ((!(divideby))?0:OPTDIV32(x,divideby))
#define SAFEMODUINT32(x,divideby) ((!(divideby))?0:OPTMOD32(x,divideby))
#define SAFEDIV(x,divideby) ((!(divideby))?0:((x)/(divideby)))
#define SAFEMOD(x,divideby) ((!(divideby))?0:((x)%(divideby)))

//Bit manipulation!
//Turn multiple bits on!
#define BITON(x,bit) ((x)|(bit))
//Turn multiple bits off!
#define BITOFF(x,bit) ((x)&(~(bit)))

//Get a bit value (0 or 1))
#define GETBIT(x,bitnr) (((x)>>(bitnr))&1)

//Set a bit on!
#define SETBIT1(x,bitnr) BITON((x),(1<<(bitnr)))
//Set a bit off!
#define SETBIT0(x,bitnr) BITOFF((x),(1<<(bitnr)))

//Getting/setting bitfields as byte/word/doubleword values!
#define GETBITS(x,shift,mask) ((x&(mask<<shift))>>shift)
#define SETBITS(x,shift,mask,val) x=((x&(~(mask<<shift)))|(((val)&mask)<<shift))

//Easy rotating!
#define ror(x,moves) ((x >> moves) | (x << (sizeof(x)*8 - moves)))
#define rol(x,moves) ((x << moves) | (x >> (sizeof(x)*8 - moves)))

//Emulator itself:
#define VIDEOMODE_EMU 0x03

//GPU debugging:
//Row with info about the CPU etc.
#define GPU_TEXT_INFOROW 3
//Row with the current CPU ASM command.
#define GPU_TEXT_DEBUGGERROW 4

//Actual handlers for strcpy/strcat on a safe way!
void safe_scatnprintf(char *dest, size_t size, const char *src, ...);
void safe_strcpy(char *dest, size_t size, const char *src);
void safe_strcat(char *dest, size_t size, const char *src);
uint_32 safe_strlen(const char *str, size_t size); //Safe safe_strlen function!
#define safescatnprintf safe_scatnprintf
#define safestrcpy(s,size,y) safe_strcpy(s,size,y)
#define safestrcat(s,size,y) safe_strcat(s,size,y)
#define safestrlen(s,size) safe_strlen(s,size)

void BREAKPOINT(); //Safe breakpoint function!

uint_32 convertrel(uint_32 src, uint_32 fromres, uint_32 tores); //Relative convert!
char *constsprintf(char *str1, ...); //Concatinate strings (or constants)!

void EMU_setDiskBusy(byte disk, byte busy); //Are we busy?
void EMU_Shutdown(byte execshutdown); //Shut down the emulator?
byte shuttingdown(); //Shutting down?
void raiseError(char *source, const char *text, ...); //Raises an error!
void printmsg(byte attribute, char *text, ...); //Prints a message to the screen!
void raiseNonFatalError(char* source, const char* text, ...); //Raise a non-fatal error once!
void delete_file(char *directory, char *filename); //Delete one or more files!
int file_exists(char *filename); //File exists?
byte emu_use_profiler(); //To use the profiler?
unsigned int OPTDIV(unsigned int val, unsigned int division);
unsigned int OPTMOD(unsigned int val, unsigned int division);
unsigned int OPTMUL(unsigned int val, unsigned int multiplication);
uint_32 OPTDIV32(uint_32 val, uint_32 division);
uint_32 OPTMOD32(uint_32 val, uint_32 division);
uint_32 OPTMUL32(uint_32 val, uint_32 multiplication);

void debugrow(char *text); //Log a row to debugrow log!

void speakerOut(word frequency); //Set the PC speaker to a sound or 0 for none!

//DOUBLE getCurrentClockSpeed(); //Retrieves the current clock speed!

void updateInputMain(); //Update input before an instruction (main thread only!)!

//PI: More accuracy from SDL2_rotozoom.h
#define PI 3.1415926535897932384626433832795

//One Megabyte of Memory!
#define MBMEMORY 0x100000
//Exact 14Mhz clock used on a PC!
#ifdef IS_LONGDOUBLE
#define MHZ14 ((15.75L/1.1L)*1000000.0L)
#else
#define MHZ14 ((15.75/1.1)*1000000.0)
#endif

//Inline register usage when defined.
#define INLINEREGISTER register

#if defined(IS_PSP) || defined(ANDROID) || defined(IS_VITA) || defined(IS_SWITCH)
//We're using a static, unchanging screen!
#define STATICSCREEN
#endif

#ifdef NDK_PROFILE
void monpendingcleanup(); //Cleanup function for the performance monitor on Android!
#endif

//Now other structs we need:

#include "..\commonemuframework\headers\packed.h" //"headers/packed.h" //Packed type!
typedef struct PACKED
{
	union
	{
		struct
		{
			#ifndef IS_BIG_ENDIAN
			byte low; //Low nibble
			byte high; //High nibble
			#else
			byte high;
			byte low;
			#endif
		};
		word w; //The word value!
	};
} wordsplitter; //Splits word in two bytes!

#include "..\commonemuframework\headers\endpacked.h" //"headers/endpacked.h" //End of packed type!

typedef struct
{
	union
	{
		struct
		{
			#ifndef IS_BIG_ENDIAN
			word wordlow; //Low nibble
			word wordhigh; //High nibble
			#else
			word wordhigh;
			word wordlow;
			#endif
		};
		uint_32 dword; //The word value!
	};
} dwordsplitter; //Splits dword (32 bits) in two words!

typedef union
{
	uint_32 dword; //Dword var!
	#ifdef IS_BIG_ENDIAN
	struct
	{
		byte high16_high;
		byte high16_low;
		byte low16_high;
		byte low16_low;
	};
	struct
	{
		word high16;
		word low16;
	};
	#else
	struct
	{
		byte low16_low;
		byte low16_high;
		byte high16_low;
		byte high16_high;
	};
	struct
	{
		word low16;
		word high16;
	};
	#endif
} dwordsplitterb; //Splits dword (32 bits) in four bytes and subs (high/low16_high/low)!

#include "..\commonemuframework\headers\packed.h" //"headers/packed.h" //Packed type!
typedef union PACKED
{
	struct
	{
		#ifdef IS_BIG_ENDIAN
		word val16high; //Filler
		#endif
		union
		{
			struct
			{
				#ifdef IS_BIG_ENDIAN
				byte val8high;
				#endif
				union
				{
					byte val8;
					sbyte val8s;
				};
				#ifndef IS_BIG_ENDIAN
				byte val8high;
				#endif
			};
			word val16; //Normal
			sword val16s; //Signed
		};
		#ifndef IS_BIG_ENDIAN
		word val16high; //Filler
		#endif
	};
	uint_32 val32; //Normal
	int_32 val32s; //Signed
} VAL32Splitter; //Our 32-bit value splitter!
#include "..\commonemuframework\headers\endpacked.h" //"headers/endpacked.h" //End of packed type!

#include "..\commonemuframework\headers\packed.h" //"headers/packed.h" //Packed type!
typedef union PACKED
{
	struct
	{
		#ifdef IS_BIG_ENDIAN
		uint_32 val32high; //Filler
		#endif
		union
		{
			struct
			{
				union
				{
					uint_32 val32;
					struct
					{
						#ifdef IS_BIG_ENDIAN
						word val16_high;
						#endif
						word val16;
						#ifndef IS_BIG_ENDIAN
						word val16_high;
						#endif
					};
				};
			};
			int_32 val32s;
		};
		#ifndef IS_BIG_ENDIAN
		uint_32 val32high; //Filler
		#endif
	};
	uint_64 val64; //Normal
	int_64 val64s; //Signed
} VAL64Splitter; //Our 32-bit value splitter!
#include "..\commonemuframework\headers\endpacked.h" //"headers/endpacked.h" //End of packed type!

#ifndef SDL_VERSION_ATLEAST
//Safety: dummy define when unsupported!
#define SDL_VERSION_ATLEAST(x,y,z) 0
#endif

#endif