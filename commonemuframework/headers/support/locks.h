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

#ifndef __LOCKS_H
#define __LOCKS_H
#include "headers/types.h" //Basic types!

void initLocks();
byte lock(byte id);
void unlock(byte id);

SDL_sem *getLock(byte id); //For termination of locks!

#define LOCK_GPU 0
#define LOCK_VIDEO 1
#define LOCK_CPU 2
#define LOCK_TIMERS 3
#define LOCK_INPUT 4
#define LOCK_SHUTDOWN 5
#define LOCK_FRAMERATE 6
#define LOCK_MAINTHREAD 7
#define LOCK_SOUND 8
#define LOCK_PERFMON 9
#define LOCK_ALLOWINPUT 10
#define LOCK_THREADS 11
#define LOCK_DISKINDICATOR 12
#define LOCK_PCAP 13
#define LOCK_PCAPFLAG 14
//Finally MIDI locks, when enabled!
//#define MIDI_LOCKSTART 15

#endif
