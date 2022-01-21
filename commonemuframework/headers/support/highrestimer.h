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

#ifndef HIGHRESTIMER_H
#define HIGHRESTIMER_H

#include "headers/types.h" //Basic types etc.

typedef struct
{
u64 oldticks; //Old ticks!
u64 newticks; //New ticks!
float ticksrest; //Ticks left after ticks have been processed (ticks left after division to destination time rate (ms/us/ns))
char lockname[256]; //Full lock name!
SDL_sem *lock; //Our lock when calculating time passed!
} TicksHolder; //Info for checking differences between ticks!

typedef struct
{
	uint_64 tv_sec;
	uint_64 tv_usec;
} UniversalTimeOfDay;

typedef struct
{
	uint_64 year;
	byte month;
	byte day;
	byte hour;
	byte minute;
	byte second;
	byte s100; //100th seconds(use either this or microseconds, since they both give the same time, only this one is rounded down!)
	byte s10000; //10000th seconds!
	uint_64 us; //Microseconds?
	byte dst;
	byte weekday;
} accuratetime;

#define MS_SECOND 1000
#define US_SECOND 1000000
#define NS_SECOND 1000000000

//Epoch time values for supported OS!
#define EPOCH_YR 1970
#define SECS_DAY (3600*24)
#define YEAR0 0
//Is this a leap year?
#define LEAPYEAR(year) ( (year % 4 == 0 && year % 100 != 0) || ( year % 400 == 0))
//What is the size of this year in days?
#define YEARSIZE(year) (LEAPYEAR(year)?366:365)

void initHighresTimer(); //Global init!

void initTicksHolder(TicksHolder *ticksholder); //Initialise ticks holder!
float getmspassed(TicksHolder *ticksholder); //Get ammount of ms passed since last use!
float getuspassed(TicksHolder *ticksholder); //Get ammount of us passed since last use!
float getnspassed(TicksHolder *ticksholder); //Get ammount of ns passed since last use!
float getmspassed_k(TicksHolder *ticksholder); //Same as getuspassed, but doesn't update the start of timing, allowing for timekeeping normally.
float getuspassed_k(TicksHolder *ticksholder); //Same as above, but keep old time data!
float getnspassed_k(TicksHolder *ticksholder); //Same as above, but keep old time data!

void convertTime(float time, char *holder, uint_32 holdersize); //Convert time to hh:mm:ss:s100.s1000.s1k!

void startHiresCounting(TicksHolder *ticksholder); //Start counting!
void stopHiresCounting(char *src, char *what, TicksHolder *ticksholder); //Stop counting&log!
int getUniversalTimeOfDay(UniversalTimeOfDay *result); //Universal time of day support!

byte epochtoaccuratetime(UniversalTimeOfDay* curtime, accuratetime* datetime);
byte accuratetimetoepoch(accuratetime* curtime, UniversalTimeOfDay* datetime);

#endif