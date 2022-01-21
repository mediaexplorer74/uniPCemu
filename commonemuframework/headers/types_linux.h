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

#ifndef __EMU_LINUX_H
#define __EMU_LINUX_H

#include <sys/stat.h> //Directory listing & file check support!
#include "headers/types_base.h" //Base types!

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

//We're Linux!
#define IS_LINUX

#ifdef VISUALC
//Normal SDL libraries on Visual C++!
#ifndef SDL2
#include "SDL.h" //SDL library for windows!
#else
//SDL2?
#include "SDL.h" //SDL library for windows!
#endif
#else
//Not Visual C?
#ifdef SDL2
#ifdef ANDROID
#include "SDL.h" //SDL library!
#else
//Basic SDL for rest platforms!
#include <SDL2/SDL.h> //SDL library!
#endif
#else
#ifdef IS_PSP
//We're handling main ourselves!
#define SDL_MAIN_HANDLED
#endif
#ifdef ANDROID
#include "SDL.h" //SDL library!
#else
#include <SDL/SDL.h> //SDL library!
#endif
#endif
#endif

#endif
