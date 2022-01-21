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

#ifndef TIMERS_H
#define TIMERS_H

void timer_thread(); //Handler for timer!

void resetTimers(); //Reset all timers to off and turn off handler!
void addtimer(float frequency, Handler timer, char *name, uint_32 counterlimit, byte coretimer, SDL_sem *uselock);
void useTimer(char *name, byte use); //To use the timer (is the timer active?)
void cleartimers(); //Clear all running timers!
void removetimer(char *name); //Removes a timer!
void startTimers(byte core);
void stopTimers(byte core);

#endif