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

#ifndef __TYPES_BASE_H
#define __TYPES_BASE_H

#ifndef uint_64
#define uint_64 uint64_t
#define int_64 int64_t
#endif

#ifndef uint_32
#define uint_32 uint32_t
#define int_32 int32_t
#endif

#ifndef LONG64SPRINTF
#define LONG64SPRINTF uint_64
#endif

#if defined(__GNUC__)
#ifndef likely
#define likely(expr) __builtin_expect(!!(expr), 1)
#endif
#endif

#if defined(__GNUC__)
#ifndef unlikely
#define unlikely(expr) __builtin_expect(!!(expr), 0)
#endif
#endif

#ifndef likely
#define likely(expr) (expr)
#endif

#ifndef unlikely
#define unlikely(expr) (expr)
#endif

#endif