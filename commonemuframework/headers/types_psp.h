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

#ifndef __EMU_PSP_H
#define __EMU_PSP_H

#include <pspkernel.h> //PSP kernel support!
#include "headers/types_base.h" //Base types!

#define delay(us) sceKernelDelayThread((us)?(us):1)
#define dosleep sceKernelSleepThread

#define domkdir(dir) sceIoMkdir(dir,0777)
#define removedirectory(dir) sceIoRmdir(dir)

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
#define ENUM8
#define ENUMS8
#define ENUM16
#define ENUMS16
#define ENUM32
#define ENUMS32

//PSP pointers are always 32-bit!
typedef uint_32 ptrnum;

//We're PSP!
#define IS_PSP

#undef SPRINTF_u_UINT32
#undef SPRINTF_u_UINT64
#undef SPRINTF_x_UINT32
#undef SPRINTF_X_UINT32
#undef SPRINTF_x_UINT64
#undef SPRINTF_X_UINT64

#define SPRINTF_u_UINT32 "lu"
#define SPRINTF_u_UINT64 "llu"
#define SPRINTF_x_UINT32 "lx"
#define SPRINTF_X_UINT32 "lX"
#define SPRINTF_x_UINT64 "llx"
#define SPRINTF_X_UINT64 "llX"

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