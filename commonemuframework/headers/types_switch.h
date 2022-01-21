#ifndef __EMU_SWITCH_H
#define __EMU_SWITCH_H

#include "headers/types_base.h" //Base types!
#include <sys/stat.h> //For mkdir

#define realdelay(x) (((x)>=1000)?((x)/1000):1)
#define delay(us) SDL_Delay(realdelay(us))
#define dosleep() for (;;) delay(1000000)

#define domkdir(path) mkdir(path, 0755)

#define removedirectory(dir) remove(dir)

//INLINE options!
#ifdef OPTINLINE
#undef OPTINLINE
#endif

#ifdef __ENABLE_INLINE
#define OPTINLINE static inline
#else
#define OPTINLINE static
#endif

//Enum safety
#define ENUM8 :byte
#define ENUMS8 :sbyte
#define ENUM16 :word
#define ENUMS16 :sword
#define ENUM32 :uint_32
#define ENUMS32 :int_32

#ifdef _LP64
#undef LONG64SPRINTF
//Linux needs this too!
#define LONG64SPRINTF long long
typedef uint_64 ptrnum;
#else
typedef uint_32 ptrnum;
#endif

void sleepthread(uint_64 ns);

//We're Switch!
#define IS_SWITCH

//Not Visual C?
#ifdef SDL2
//Basic SDL for rest platforms!
#include <SDL2/SDL.h> //SDL library!
#else
#ifdef IS_PSP
//We're handling main ourselves!
#define SDL_MAIN_HANDLED
#endif
#include <SDL/SDL.h> //SDL library!
#endif

#endif