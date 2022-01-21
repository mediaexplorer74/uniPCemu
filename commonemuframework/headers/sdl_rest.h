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

#ifndef COMMON_SDL_REST_H
#define COMMON_SDL_REST_H
#ifdef IS_PSP
#ifndef SDL_SwapLE64
#define SDL_SwapLE64(x) (x)
#endif
#ifndef SDL_SwapLE32
#define SDL_SwapLE32(x) (x)
#endif
#ifndef SDL_SwapLE16
#define SDL_SwapLE16(x) (x)
#endif
#ifndef SDL_SwapBE16
//PSP doesn't support SDL_SwapBE16
#define SDL_SwapBE16(x) ((((x)>>8)&0xFF)|(((x)&0xFF)<<8))
#endif
#ifndef SDL_SwapBE32
//PSP doesn't support SDL_SwapBE32
#define SDL_SwapBE32(x) (SDL_SwapBE16(((x)>>16)&0xFFFF)|(SDL_SwapBE16((x)&0xFFFF)<<16))
#endif
#endif
#ifndef SDL_SwapLE64
//SDL_SwapLE64 when not supported on the platform itself
#ifdef IS_BIG_ENDIAN
#define SDL_SwapLE64(x) (SDL_SwapLE32(((x)>>32)&0xFFFFFFFFULL)|(SDL_SwapLE32((x)&0xFFFFFFFFULL)<<32))
#else
#define SDL_SwapLE64(x) (x)
#endif
#endif
#endif