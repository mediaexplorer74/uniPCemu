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

#ifndef __EMU_WIN_H
#define __EMU_WIN_H

#include "..\commonemuframework\headers\types_base.h" //"headers/types_base.h" //Base types!

//Convert the current info to support Visual C++ vs MinGW/GNU detection!
#ifdef _WIN32
#ifndef __GNUC__
#ifndef __MINGW32__
#pragma comment(lib, "User32.lib")
#ifndef VISUALC
#define VISUALC
#endif
#endif
#endif
#endif

#ifdef VISUALC
#ifdef _DEBUG
#ifdef _VLD
//Visual Leak detector when debugging!
#include <vld.h>
#endif
#endif
#endif

//Windows specific structures!
#ifdef _WIN32
#include <direct.h> //For mkdir and directory support! Visual C++ only!
#endif

#include <windows.h> //Both for Visual c++ and MinGW/GNU, this is used!

#define realdelay(x) (((x)>=1000)?((x)/1000):1)
#define delay(us) SDL_Delay(realdelay(us))
#define dosleep() for (;;) delay(1000000)

#ifdef VISUALC
//Visual C++ version of unlikely/likely!
#ifdef unlikely
#undef unlikely
#endif
#define unlikely(x) (x)
#ifdef likely
#undef likely
#endif
#define likely(x) (x)
#endif

#ifdef VISUALC
//Visual C++ needs the result!
#define domkdir(dir) int ok = _mkdir(dir)
#else
//Don't use the result with MinGW!
#define domkdir(dir) _mkdir(dir)
#endif

#ifdef VISUALC
//Visual C++ needs the result!
#define removedirectory(dir) int ok = _rmdir(dir)
#else
//Don't use the result with MinGW!
#define removedirectory(dir) _rmdir(dir)
#endif

//INLINE options!
#ifdef OPTINLINE
#undef OPTINLINE
#endif

#ifdef __ENABLE_INLINE
//For some reason inline functions don't work on windows?
#define OPTINLINE static __inline
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

#ifdef _WIN64
#ifdef __MINGW64__
#undef LONGLONGSPRINTF
#undef LONGLONGSPRINTX
#undef LONGLONGSPRINTx
#undef LONG64SPRINTF
#define LONGLONGSPRINTF "%I64llu"
#define LONGLONGSPRINTX "%016I64X"
#define LONGLONGSPRINTx "%016I64x"
#define LONG64SPRINTF uint_64
#endif
typedef uint_64 ptrnum;
#else
#ifdef __MINGW32__
#undef LONGLONGSPRINTF
#undef LONGLONGSPRINTX
#undef LONGLONGSPRINTx
#undef LONG64SPRINTF
#define LONGLONGSPRINTF "%I64llu"
#define LONGLONGSPRINTX "%016I64X"
#define LONGLONGSPRINTx "%016I64x"
#define LONG64SPRINTF uint_64
#endif
typedef uint_32 ptrnum;
#endif

//Enable below define to enable Windows-style line-endings in logs etc!
#define WINDOWS_LINEENDING

//We're Windows!
#define IS_WINDOWS

#if 0
//Use long DOUBLE!
#define IS_LONGDOUBLE
#endif

#ifdef VISUALC
//Normal SDL libraries on Visual C++!
#ifndef SDL2
#include "..\commonemuframework-sdl2\include\SDL.h" //"SDL.h" //SDL library for windows!
#else
//SDL2?
#include "..\commonemuframework-sdl2\include\SDL.h"//#include "SDL.h" //SDL library for windows!
#endif
#else
//Not Visual C?
#ifdef SDL2
//Basic SDL for rest platforms!
#include <SDL2/SDL.h> //SDL library!
#else
#include <SDL/SDL.h> //SDL library!
#endif
#endif

#endif
