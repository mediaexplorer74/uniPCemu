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

#include "headers/types.h" //Basic types!
#include "headers/support/highrestimer.h" //Our own typedefs etc.
#include "headers/support/log.h" //Logging support!
#include "headers/support/locks.h" //Locking support!

#ifdef IS_PSP
#include <psprtc.h> //PSP Real Time Clock atm!
#endif
#ifdef IS_VITA
#include <psp2/rtc.h> 
#endif

#if defined(IS_LINUX) || defined(IS_SWITCH)
#include <sys/time.h> //Time support!
#include <time.h> //Time support!
#else
#include <time.h> //Time support!
#endif

//Allow windows timing to be used?
#define ENABLE_WINTIMING 1
#define ENABLE_PSPTIMING 1
#define ENABLE_VITATIMING 1

DOUBLE tickresolution = 0.0f; //Our tick resolution, initialised!
byte tickresolution_type = 0xFF; //What kind of ticks are we using? 0=SDL, 1=getUniversalTimeOfDay, 2=Platform specific

float msfactor, usfactor, nsfactor; //The factors!
float msfactorrev, usfactorrev, nsfactorrev; //The factors reversed!

u64 lastticks=0; //Last ticks passed!

//Our accuratetime epoch support!

byte _ytab[2][12] = { //Days within months!
	{ 31,28,31,30,31,30,31,31,30,31,30,31 }, //Normal year
	{ 31,29,31,30,31,30,31,31,30,31,30,31 } //Leap year
};

byte epochtoaccuratetime(UniversalTimeOfDay* curtime, accuratetime* datetime)
{
	//More accurate timing than default!
	datetime->us = curtime->tv_usec;
	datetime->s100 = (byte)(curtime->tv_usec / 10000); //10000us=1/100 second!
	datetime->s10000 = (byte)((curtime->tv_usec % 10000) / 100); //100us=1/10000th second!

	//Further is directly taken from the http://stackoverflow.com/questions/1692184/converting-epoch-time-to-real-date-time gmtime source code.
	uint_64 dayclock, dayno;
	uint_32 year = EPOCH_YR;

	dayclock = (uint_64)curtime->tv_sec % SECS_DAY;
	dayno = (uint_64)curtime->tv_sec / SECS_DAY;

	datetime->second = dayclock % 60;
	datetime->minute = (byte)((dayclock % 3600) / 60);
	datetime->hour = (byte)(dayclock / 3600);
	datetime->weekday = (dayno + 4) % 7;       /* day 0 was a thursday */
	for (; dayno >= (unsigned long)YEARSIZE(year);)
	{
		dayno -= YEARSIZE(year);
		year++;
	}
	datetime->year = year - YEAR0;
	datetime->day = (byte)dayno;
	datetime->month = 0;
	while (dayno >= _ytab[LEAPYEAR(year)][datetime->month]) {
		dayno -= _ytab[LEAPYEAR(year)][datetime->month];
		++datetime->month;
	}
	++datetime->month; //We're one month further(months start at one, not zero)!
	datetime->day = (byte)(dayno + 1);
	datetime->dst = 0;

	return 1; //Always successfully converted!
}

//Sizes of minutes, hours and days in Epoch time units.
#define MINUTESIZE 60
#define HOURSIZE 3600
#define DAYSIZE (3600*24)

byte accuratetimetoepoch(accuratetime* curtime, UniversalTimeOfDay* datetime)
{
	uint_64 seconds = 0;
	if ((curtime->us - (curtime->us % 100)) != (((curtime->s100) * 10000) + (curtime->s10000 * 100))) return 0; //Invalid time to convert: 100th&10000th seconds doesn't match us(this is supposed to be the same!)
	if (curtime->year < 1970) return 0; //Before 1970 isn't supported!
	datetime->tv_usec = (uint_32)curtime->us; //Save the microseconds directly!
	uint_64 year;
	byte counter;
	byte leapyear;
	byte monthspassed;
	for (year = curtime->year; year > 1970;) //Process the years!
	{
		--year; //The previous year has passed!
		seconds += YEARSIZE(year) * DAYSIZE; //Add the year that has passed!
	}
	leapyear = LEAPYEAR(curtime->year); //Are we a leap year?
	//Now, only months etc. are left!
	monthspassed = MAX(curtime->month, 1) - 1; //How many months have passed!
	for (counter = 0; counter < monthspassed;) //Process the months!
	{
		seconds += _ytab[leapyear][counter++] * DAYSIZE; //Add a month that has passed!
	}
	//Now only days, hours, minutes and seconds are left!
	seconds += DAYSIZE * (curtime->day ? (curtime->day - 1) : 0); //Days start at 1!
	seconds += HOURSIZE * curtime->hour;
	seconds += MINUTESIZE * curtime->minute;
	seconds += curtime->second;

	datetime->tv_sec = (uint_64)seconds; //The amount of seconds!
	return 1; //Successfully converted!
}

//For cross-platform compatibility!
int getUniversalTimeOfDay(UniversalTimeOfDay *result)
{
#ifdef IS_WINDOWS
	// Note: some broken versions only have 8 trailing zero's, the correct epoch has 9 trailing zero's
	static const uint64_t EPOCH = ((uint64_t)116444736000000000ULL);

	SYSTEMTIME  system_time;
	FILETIME    file_time;
	INLINEREGISTER uint64_t    time;

	GetSystemTime(&system_time);
	SystemTimeToFileTime(&system_time, &file_time);
	time = (((uint64_t)file_time.dwHighDateTime) << 32)|((uint64_t)file_time.dwLowDateTime);

	result->tv_sec = (long)((time - EPOCH) / 10000000L);
	result->tv_usec = (long)(system_time.wMilliseconds * 1000);
	return 0; //Always OK!
#else
	//PSP and Linux already have proper gettimeofday support built into the compiler!
#if defined(IS_PSP) || defined(IS_LINUX) || defined(IS_SWITCH)
	//Not supported on PS Vita/Switch!
	struct timeval tp;
	int temp;
	temp = gettimeofday(&tp,NULL); //Get the time of the day!
	if (temp==0) //Success?
	{
		result->tv_sec = tp.tv_sec; //Seconds
		result->tv_usec = tp.tv_usec; //Microseconds
	}
	return temp; //Give the result!
#else
#ifdef IS_VITA
	//Convert using the most accurate clock we have!
	SceDateTime time;
	sceRtcGetCurrentClock(&time, 0);
	accuratetime accuratetime;
	accuratetime.year = sceRtcGetYear(&time),
	accuratetime.month = sceRtcGetMonth(&time),
	accuratetime.day = sceRtcGetDay(&time),
	accuratetime.hour = sceRtcGetHour(&time),
	accuratetime.minute = sceRtcGetMinute(&time),
	accuratetime.second = sceRtcGetSecond(&time),
	accuratetime.us = sceRtcGetMicrosecond(&time);
	accuratetime.s100 = (byte)(accuratetime.us / 10000); //10000us=1/100 second!
	accuratetime.s10000 = (byte)((accuratetime.us % 10000) / 100); //100us=1/10000th second!
	if (accuratetimetoepoch(&accuratetime, result))
	{
		return 0; //Success!
	}
	return -1; //Error!
#endif
#endif
#endif
	return -1; //Unsupported!
}

//Normal High-resolution clock support:
OPTINLINE u64 getcurrentticks() //Retrieve the current ticks!
{
#ifdef IS_VITA
	SceRtcTick tick;
#endif
	UniversalTimeOfDay tp;
	memset(&tp, 0, sizeof(tp)); //Init properly!
	switch (tickresolution_type) //What type are we using?
	{
	case 0: //SDL?
		return (u64)SDL_GetTicks(); //Give the ticks passed using SDL default handling!
	case 2: //System specific?
#ifdef IS_PSP
	{
		u64 result = 0; //The result!
		if (!sceRtcGetCurrentTick(&result)) //Try to retrieve current ticks as old ticks until we get it!
		{
			return result; //Give the result!
		}
		return lastticks; //Invalid result!		
	}
#else
#ifdef IS_VITA
	{
		if (!sceRtcGetCurrentTick(&tick)) //Try to retrieve current ticks as old ticks until we get it!
		{
			return tick.tick; //Give the result!
		}
		return lastticks; //Invalid result!		
	}
#endif
#endif
#ifdef IS_WINDOWS
	{
		LARGE_INTEGER temp;
		if (QueryPerformanceCounter(&temp)==0) return lastticks; //Invalid result?
		return temp.QuadPart; //Give the result by the performance counter of windows!
	}
#endif
	case 1: //gettimeofday counter?
		if (getUniversalTimeOfDay(&tp)==0) //Time gotten?
		{
			return (tp.tv_sec*1000000)+tp.tv_usec; //Give the result!
		}
		return lastticks; //Invalid result!		
		break;
	default: //Unknown method?
		return lastticks; //Invalid result!		
		break;
	}
}

void initHighresTimer()
{
	if (tickresolution_type!=0xFF) goto resolution_ready; //Already set? Don't detect it again!
	//SDL timing by default?
	tickresolution = 1000.0f; //We have a resolution in ms as given by SDL!
	tickresolution_type = 0; //We're using SDL ticks!
	#ifdef IS_PSP
		//Try PSP timing!
		if (ENABLE_PSPTIMING)
		{
			tickresolution = sceRtcGetTickResolution(); //Get the tick resolution, as defined on the PSP!
			tickresolution_type = 2; //Don't use SDL!
		}
	#endif
	#ifdef IS_VITA
		//Try Vita timing!
		if (ENABLE_VITATIMING)
		{
			tickresolution = sceRtcGetTickResolution(); //Get the tick resolution, as defined on the PSP!
			tickresolution_type = 2; //Don't use SDL!
		}
#endif

	#ifdef IS_WINDOWS
		//Try Windows timing!
		LARGE_INTEGER tickresolution_win;
		if (QueryPerformanceFrequency(&tickresolution_win) && ENABLE_WINTIMING)
		{
			tickresolution = (DOUBLE)tickresolution_win.QuadPart; //Apply the tick resolution!
			tickresolution_type = 2; //Don't use SDL!
		}
	#endif

	//Finally: getUniversalTimeOfDay provides 10us accuracy at least!
	if (tickresolution_type==0) //We're unchanged? Default to getUniversalTimeOfDay counter!
	{
		tickresolution = 1000000.0f; //Microsecond accuracy!
		tickresolution_type = 1; //Don't use SDL: we're the getUniversalTimeOfDay counter!
	}

	resolution_ready: //Ready?
	//Calculate needed precalculated factors!
	usfactor = (float)(1.0f/tickresolution)*US_SECOND; //US factor!
	nsfactor = (float)(1.0f/tickresolution)*NS_SECOND; //NS factor!
	msfactor = (float)(1.0f/tickresolution)*MS_SECOND; //MS factor!
	usfactorrev = 1.0f/usfactor; //Reverse!
	nsfactorrev = 1.0f/nsfactor; //Reverse!
	msfactorrev = 1.0f/msfactor; //Reverse!
	lastticks = getcurrentticks(); //Initialize the last tick to be something valid!
}

void initTicksHolder(TicksHolder *ticksholder)
{
	memset(ticksholder, 0, sizeof(*ticksholder)); //Clear the holder!}
	ticksholder->newticks = getcurrentticks(); //Initialize the ticks to the current time!
}

OPTINLINE float getrealtickspassed(TicksHolder *ticksholder)
{
    INLINEREGISTER u64 temp;
	INLINEREGISTER u64 currentticks = getcurrentticks(); //Fist: get current ticks to be sure we're right!
	//We're not initialising/first call?
	temp = ticksholder->newticks; //Move new ticks to old ticks! Store it for quicker reference later on!
	ticksholder->oldticks = temp; //Store the old ticks!
	ticksholder->newticks = currentticks; //Set current ticks as new ticks!
	if (currentticks<temp)//Overflown time?
	{
	    //Temp is already equal to oldticks!
	    temp -= currentticks; //Difference between the numbers(old-new=difference)!
		currentticks = (u64)~0; //Max to substract from instead of the current ticks!
		if (tickresolution_type==0) //Are we SDL ticks?
		{
			currentticks &= (u64)(((uint_32)~0)); //We're limited to the uint_32 type, so wrap around it!
		}
	}
	currentticks -= temp; //Substract the old ticks for the difference!
	return (float)currentticks; //Give the result: amount of ticks passed!
}

OPTINLINE float gettimepassed(TicksHolder *ticksholder, float secondfactor, float secondfactorreversed)
{
	INLINEREGISTER float result;
	INLINEREGISTER float tickspassed;
	tickspassed = getrealtickspassed(ticksholder); //Start with checking the current ticks!
	tickspassed += ticksholder->ticksrest; //Add the time we've left unused last time!
	result = floorf(tickspassed*secondfactor); //The ammount of ms that has passed as precise as we can use!
	tickspassed -= (result*secondfactorreversed); //The ticks left unprocessed this call!
	ticksholder->ticksrest = tickspassed; //Add the rest ticks unprocessed to the next time we're counting!
	return result; //Ordinary result!
}

float getmspassed(TicksHolder *ticksholder) //Get ammount of ms passed since last use!
{
	return gettimepassed(ticksholder, msfactor,msfactorrev); //Factor us!
}

float getuspassed(TicksHolder *ticksholder) //Get ammount of ms passed since last use!
{
	return gettimepassed(ticksholder,usfactor,usfactorrev); //Factor us!
}

float getnspassed(TicksHolder *ticksholder)
{
	return gettimepassed(ticksholder,nsfactor,nsfactorrev); //Factor ns!
}

float getmspassed_k(TicksHolder *ticksholder) //Same as getuspassed, but doesn't update the start of timing, allowing for timekeeping normally.
{
	TicksHolder temp;
	memcpy(&temp, ticksholder, sizeof(temp)); //Copy the old one!
	return gettimepassed(&temp, msfactor, msfactorrev); //Factor us!
}

float getuspassed_k(TicksHolder *ticksholder) //Same as getuspassed, but doesn't update the start of timing, allowing for timekeeping normally.
{
	TicksHolder temp;
	memcpy(&temp,ticksholder,sizeof(temp)); //Copy the old one!
	return gettimepassed(&temp, usfactor, usfactorrev); //Factor us!
}

float getnspassed_k(TicksHolder *ticksholder) //Same as getuspassed, but doesn't update the start of timing, allowing for timekeeping normally.
{
	TicksHolder temp;
	memcpy(&temp,ticksholder,sizeof(temp)); //Copy the old one!
	return gettimepassed(&temp, nsfactor, nsfactorrev); //Factor us!
}

void startHiresCounting(TicksHolder *ticksholder)
{
	getrealtickspassed(ticksholder); //Start with counting!
}

void stopHiresCounting(char *src, char *what, TicksHolder *ticksholder)
{
	char time[30]; //Some time holder!
	float passed = getuspassed(ticksholder); //Get the time that has passed!
	cleardata(&time[0],sizeof(time)); //Init holder!
	convertTime(passed,&time[0],sizeof(time)); //Convert the time!
	dolog(src,"Counter %s took %s",what,time); //Log it!
}

void convertTime(float time, char *holder, uint_32 holdersize) //Convert time to hh:mm:ss:s100.s1000.s1k!
{
	uint_32 h, m, s, s100,sus;
	h = (uint_32)(time/3600000000ll); //Hours!
	time -= h*3600000000ll; //Left!
	m = (uint_32)(time/60000000ll); //Minutes!
	time -= m*60000000ll; //Left!
	s = (uint_32)(time/1000000ll); //Seconds!
	time -= s*1000000ll; //Left!
	s100 = (uint_32)(time/10000ll); //1/100th second!
	time -= (s100*10000ll); //Left!
	sus = (uint_32)time; //Microseconds left (1/1000 and ns)!
	snprintf(holder,holdersize,"%02" SPRINTF_u_UINT32 ":%02" SPRINTF_u_UINT32 ":%02" SPRINTF_u_UINT32 ":%02" SPRINTF_u_UINT32 ".%05" SPRINTF_u_UINT32,h,m,s,s100,sus); //Generate the final text!
}