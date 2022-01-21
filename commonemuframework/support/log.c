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

#include "headers/types.h"
#include "headers/support/highrestimer.h" //Our own typedefs etc.
#include "headers/fopen64.h" //64-bit fopen support!

TicksHolder logticksholder; //Log ticks holder!
SDL_sem *log_Lock = NULL;
SDL_sem *log_stampLock = NULL;
byte log_timestamp = 1; //Are we to log the timestamp?
char lastfile[256] = ""; //Last file we've been logging to!
BIGFILE *logfile = NULL; //The log file to use!

char logpath[256] = "logs"; //Log path!

byte logdebuggertoprintf = 0; //Log debugger to printf?
byte verifydebuggerfrominput = 0; //Verify debugger from input?

//Windows line-ending = \r\n. Wordpad line-ending is \n.

#ifdef WINDOWS_LINEENDING
char lineending[2] = {'\r','\n'}; //CRLF!
#else
char lineending[1] = {'\n'}; //Line-ending used in other systems!
#endif

void closeLogFile(byte islocked)
{
	if (islocked == 0) WaitSem(log_Lock) //Only one instance allowed!
	//PSP doesn't buffer, because it's too slow! Android is still in the making, so constantly log, don't buffer!
		if (unlikely(logfile)) //Are we logging?
		{
			emufclose64(logfile);
			logfile = NULL; //We're finished!
		}
	if (islocked == 0) PostSem(log_Lock)
}

void donelog(void)
{
	closeLogFile(1); //Close the log file, if needed!
	SDL_DestroySemaphore(log_Lock);
	SDL_DestroySemaphore(log_stampLock);
}

char log_filenametmp[256];
char log_logtext[0x80000], log_logtext2[0x100000]; //Original and prepared text!
char log_thetimestamp[256];


void initlog()
{
	initTicksHolder(&logticksholder); //Initialize our time!
	startHiresCounting(&logticksholder); //Init our timer to the starting point!
	log_Lock = SDL_CreateSemaphore(1); //Create our sephamore!
	log_stampLock = SDL_CreateSemaphore(1); //Create our sephamore!
	atexit(&donelog); //Our cleanup function!
	cleardata(&log_filenametmp[0],sizeof(log_filenametmp)); //Init filename!
	cleardata(&log_logtext[0],sizeof(log_logtext)); //Init logging text!
	cleardata(&log_logtext2[0],sizeof(log_logtext2)); //Init logging text!
	cleardata(&log_thetimestamp[0],sizeof(log_thetimestamp)); //Init timestamp text!
}

OPTINLINE void addnewline(char *s, uint_32 size)
{
	char s2[2] = {'\0','\0'}; //String to add!
	byte i;
	for (i = 0;i < sizeof(lineending);i++) //Process the entire line-ending!
	{
		s2[0] = lineending[i]; //The character to add!
		safestrcat(s,size,s2); //Add the line-ending character(s) to the text!
	}
}

byte log_logtimestamp(byte logtimestamp)
{
	byte result;
	WaitSem(log_stampLock) //Only one instance allowed!
	result = log_timestamp; //Are we loggin the timestamp?
	if (logtimestamp<2) //Valid?
	{
		log_timestamp = (logtimestamp!=0)?1:0; //Set the new timestamp setting!
	}
	//Unlock
	PostSem(log_stampLock)
	return result; //Give the result!
}

void dolog(char *filename, const char *format, ...) //Logging functionality!
{
	byte isexisting = 0;
	char *debuggerverification; //Verification of debugger!
	byte toprintf = 0;
	byte frominput = 0;
	word i;
	uint_32 logtextlen = 0;
	char c, newline = 0;
	static char newline1 = 0, newline2 = 0; //Newline status on the inputs to compare!
	int d;
	va_list args; //Going to contain the list!
	float time;
	char log_notequal[] = "<VERIFY:MISMATCH>"; //Not equal string to debug
	byte isntequal = 0; //Not equal is triggered?
	static byte isnewline=0,havenewline=0; //Newline detected?

	//Lock
	WaitSem(log_Lock) //Only one instance allowed!

	//First: init variables!
	safestrcpy(&log_filenametmp[0],sizeof(log_filenametmp),""); //Init filename!
	safestrcpy(&log_logtext[0],sizeof(log_logtext),""); //Init logging text!
	safestrcpy(&log_thetimestamp[0],sizeof(log_thetimestamp),""); //Init timestamp text!
	
	safestrcpy(log_filenametmp,sizeof(log_filenametmp),logpath); //Base directory!
	safestrcat(log_filenametmp,sizeof(log_filenametmp),"/");
	safestrcat(log_filenametmp,sizeof(log_filenametmp),filename); //Add the filename to the directory!
	if (!*filename) //Empty filename?
	{
		safestrcpy(log_filenametmp,sizeof(log_filenametmp),"unknown"); //Empty filename = unknown.log!
	}
	#ifdef ANDROID
	safestrcat(log_filenametmp,sizeof(log_filenametmp),".txt"); //Do log here!
	#else
	safestrcat(log_filenametmp,sizeof(log_filenametmp),".log"); //Do log here!
	#endif

	if (logdebuggertoprintf)
	{
		if (strcmp(filename, "debugger")) //Debugger?
		{
			toprintf = 1; //Run through printf as well!
		}
	}

	if (verifydebuggerfrominput)
	{
		if (strcmp(filename, "debugger")) //Debugger?
		{
			frominput = 1; //Verify from input as well!
		}
	}

	va_start (args, format); //Start list!
	vsnprintf(log_logtext,sizeof(log_logtext), format, args); //Compile list!
	va_end (args); //Destroy list!

	safestrcpy(log_logtext2,sizeof(log_logtext2),""); //Clear the data to dump!

	logtextlen = safe_strlen(log_logtext, sizeof(log_logtext)); //Get our length to log!
	for (i=0;i<logtextlen;) //Process the log text!
	{
		c = log_logtext[i++]; //Read the character to process!
		if ((c == '\n') || (c == '\r')) //Newline character?
		{
			//we count \n, \r, \n\r and \r\n as the same: newline!
			if (!newline) //First newline character?
			{
				addnewline(&log_logtext2[0],sizeof(log_logtext2)); //Flush!
				newline = c; //Detect for further newlines!
			}
			else //Second newline+?
			{
				if (newline == c) //Same newline as before?
				{
					addnewline(&log_logtext2[0],sizeof(log_logtext2)); //Flush!
					//Continue counting newlines!
				}
				else //No newline, clear the newline flag!
				{
					newline = 0; //Not a newline anymore!
				}
			}
		}
		else //Normal character?
		{
			newline = 0; //Not a newline character anymore!
			safe_scatnprintf(log_logtext2,sizeof(log_logtext2), "%c", c); //Add to the debugged data!
		}
	}

	if (safe_strlen(log_logtext2,sizeof(log_logtext2)) && log_logtimestamp(2)) //Got length and logging timestamp?
	{
		time = getuspassed_k(&logticksholder); //Get the current time!
		convertTime(time,&log_thetimestamp[0],sizeof(log_thetimestamp)); //Convert the time!
		safestrcat(log_thetimestamp,sizeof(log_thetimestamp),": "); //Suffix!
	}

	if ((!logfile) || (strcmp(lastfile,log_filenametmp)!=0)) //Other file or new file?
	{
		log_retrywrite: //Keep retrying until we can log when appending?
		closeLogFile(1); //Close the old log if needed!
		domkdir(logpath); //Create a logs directory if needed!
		logfile = emufopen64(log_filenametmp, "rb"); //Open for testing!
		if (logfile) //Existing?
		{
			emufclose64(logfile); //Close it!
			logfile = emufopen64(log_filenametmp, "ab"); //Reopen for appending!
			isexisting = 1; //Existing!
		}
		else
		{
			logfile = emufopen64(log_filenametmp, "wb"); //Reopen for writing new!
		}
		safestrcpy(lastfile,sizeof(lastfile), log_filenametmp); //Set the last file we've opened!
	}

	//Now log!
	if (logfile) //Opened?
	{
		isntequal = 0; //Default: we're equal, don't log anything!
		if (safe_strlen(log_logtext2,sizeof(log_logtext2))) //Got length?
		{
			if (toprintf) //Debugger to printf?
			{
				printf("%s", log_thetimestamp); //Log!
				printf("%s", log_logtext2);
			}
			emufwrite64(&log_thetimestamp,1,safe_strlen(log_thetimestamp,sizeof(log_thetimestamp)),logfile); //Write the timestamp!
			if (unlikely(frominput)) //Verify debugger from input?
			{
				debuggerverification = &log_thetimestamp[0]; //What to verify!
				for (; *debuggerverification;) //Check the entire string!
				{
					d = fgetc(stdin); //Read a byte from input!
					if (d  == EOF) //EOF reached?
					{
						isntequal = 1; //Not equal detected!
						break; //Stop searching!
					}
					else //Valid data read for verification?
					{
						//First, process newlines on the input!
						isnewline = 0; //Default: not a newline!
						if ((d == '\n') || (d == '\r')) //Newline character?
						{
							//we count \n, \r, \n\r and \r\n as the same: newline!
							if (!newline1) //First newline character?
							{
								isnewline = 1; //Flush!
								newline1 = d; //Detect for further newlines!
							}
							else //Second newline+?
							{
								if (newline1 == d) //Same newline as before?
								{
									isnewline = 1; //Flush!
									//Continue counting newlines!
								}
								else //No newline, clear the newline flag!
								{
									newline1 = 0; //Not a newline anymore!
								}
							}
						}
						else //Normal character?
						{
							newline1 = 0; //Not a newline character anymore!
							isnewline = 0;
						}

						//Next, verify newline on our own output!
						havenewline = 0; //Default: not a newline!
						c = *debuggerverification++; //Read the character to process!
						if ((c == '\n') || (c == '\r')) //Newline character?
						{
							//we count \n, \r, \n\r and \r\n as the same: newline!
							if (!newline) //First newline character?
							{
								havenewline = 1; //Flush!
								newline2 = c; //Detect for further newlines!
							}
							else //Second newline+?
							{
								if (newline2 == c) //Same newline as before?
								{
									havenewline = 1; //Flush!
									//Continue counting newlines!
								}
								else //No newline, clear the newline flag!
								{
									newline2 = 0; //Not a newline anymore!
								}
							}
						}
						else //Normal character?
						{
							newline2 = 0; //Not a newline character anymore!
							havenewline = 0; //Normal character!
						}

						if (isnewline != havenewline) //Newline mismatch?
						{
							isntequal = 1; //Not equal detected!
							break; //Stop searching!
						}
						else if (!isnewline && (c != d)) //Non-newline character mismatch?
						{
							isntequal = 1; //Not equal detected!
							break; //Stop searching!
						}
					}
				}
			}

			//Verify the log text
			if (unlikely(frominput)) //Verify debugger from input?
			{
				debuggerverification = &log_logtext2[0]; //What to verify!
				for (; *debuggerverification;) //Check the entire string!
				{
					d = fgetc(stdin); //Read a byte from input!
					if (d == EOF) //EOF reached?
					{
						isntequal = 1; //Not equal detected!
						break; //Stop searching!
					}
					else //Valid data read for verification?
					{
						//First, process newlines on the input!
						isnewline = 0; //Default: not a newline!
						if ((d == '\n') || (d == '\r')) //Newline character?
						{
							//we count \n, \r, \n\r and \r\n as the same: newline!
							if (!newline1) //First newline character?
							{
								isnewline = 1; //Flush!
								newline1 = d; //Detect for further newlines!
							}
							else //Second newline+?
							{
								if (newline1 == d) //Same newline as before?
								{
									isnewline = 1; //Flush!
									//Continue counting newlines!
								}
								else //No newline, clear the newline flag!
								{
									newline1 = 0; //Not a newline anymore!
								}
							}
						}
						else //Normal character?
						{
							newline1 = 0; //Not a newline character anymore!
							isnewline = 0;
						}

						//Next, verify newline on our own output!
						havenewline = 0; //Default: not a newline!
						c = *debuggerverification++; //Read the character to process!
						if ((c == '\n') || (c == '\r')) //Newline character?
						{
							//we count \n, \r, \n\r and \r\n as the same: newline!
							if (!newline) //First newline character?
							{
								havenewline = 1; //Flush!
								newline2 = c; //Detect for further newlines!
							}
							else //Second newline+?
							{
								if (newline2 == c) //Same newline as before?
								{
									havenewline = 1; //Flush!
									//Continue counting newlines!
								}
								else //No newline, clear the newline flag!
								{
									newline2 = 0; //Not a newline anymore!
								}
							}
						}
						else //Normal character?
						{
							newline2 = 0; //Not a newline character anymore!
							havenewline = 0; //Normal character!
						}

						if (isnewline != havenewline) //Newline mismatch?
						{
							isntequal = 1; //Not equal detected!
							break; //Stop searching!
						}
						else if (!isnewline && (c != d)) //Non-newline character mismatch?
						{
							isntequal = 1; //Not equal detected!
							break; //Stop searching!
						}
					}
				}
			}
			emufwrite64(&log_logtext2,1,safe_strlen(log_logtext2,sizeof(log_logtext2)),logfile); //Write string to file!
		}
		if (toprintf) //Debugger to printf?
		{
			printf("%s", lineending);
		}
		//Verify the line ending
		if (unlikely(frominput)) //Verify debugger from input?
		{
			debuggerverification = &lineending[0]; //What to verify!
			for (; *debuggerverification;) //Check the entire string!
			{
				d = fgetc(stdin); //Read a byte from input!
				if (d == EOF) //EOF reached?
				{
					isntequal = 1; //Not equal detected!
					break; //Stop searching!
				}
				else //Valid data read for verification?
				{
					//First, process newlines on the input!
					isnewline = 0; //Default: not a newline!
					if ((d == '\n') || (d == '\r')) //Newline character?
					{
						//we count \n, \r, \n\r and \r\n as the same: newline!
						if (!newline1) //First newline character?
						{
							isnewline = 1; //Flush!
							newline1 = d; //Detect for further newlines!
						}
						else //Second newline+?
						{
							if (newline1 == d) //Same newline as before?
							{
								isnewline = 1; //Flush!
								//Continue counting newlines!
							}
							else //No newline, clear the newline flag!
							{
								newline1 = 0; //Not a newline anymore!
							}
						}
					}
					else //Normal character?
					{
						newline1 = 0; //Not a newline character anymore!
						isnewline = 0;
					}

					//Next, verify newline on our own output!
					havenewline = 0; //Default: not a newline!
					c = *debuggerverification++; //Read the character to process!
					if ((c == '\n') || (c == '\r')) //Newline character?
					{
						//we count \n, \r, \n\r and \r\n as the same: newline!
						if (!newline) //First newline character?
						{
							havenewline = 1; //Flush!
							newline2 = c; //Detect for further newlines!
						}
						else //Second newline+?
						{
							if (newline2 == c) //Same newline as before?
							{
								havenewline = 1; //Flush!
								//Continue counting newlines!
							}
							else //No newline, clear the newline flag!
							{
								newline2 = 0; //Not a newline anymore!
							}
						}
					}
					else //Normal character?
					{
						newline2 = 0; //Not a newline character anymore!
						havenewline = 0; //Normal character!
					}

					if (isnewline != havenewline) //Newline mismatch?
					{
						isntequal = 1; //Not equal detected!
						break; //Stop searching!
					}
					else if (!isnewline && (c != d)) //Non-newline character mismatch?
					{
						isntequal = 1; //Not equal detected!
						break; //Stop searching!
					}
				}
			}
		}

		emufwrite64(&lineending, 1, sizeof(lineending), logfile); //Write the line feed appropriate for the system after any write operation!

		if (unlikely(frominput)) //not equal detected? Show said in the log!
		{
			if (unlikely(isntequal))
			{
				emufwrite64(&log_notequal, 1, sizeof(log_notequal), logfile); //Log not equal!
				emufwrite64(&lineending, 1, sizeof(lineending), logfile); //Write the line feed appropriate for the system after any write operation!
			}
		}
#if defined(IS_PSP) || defined(ANDROID) || defined(IS_VITA) || defined(IS_SWITCH)
		closeLogFile(1); //Close the current log file!
#endif
	}
	else if (isexisting) //Existing couldn't be opened for appending?
	{
		delay(0); //Wait a bit for it to become available!
		goto log_retrywrite;
	}

	//Unlock
	PostSem(log_Lock)
}