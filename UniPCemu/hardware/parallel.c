/*

Copyright (C) 2019 - 2021 Superfury

This file is part of UniPCemu.

UniPCemu is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

UniPCemu is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with UniPCemu.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "headers/types.h" //Basic types!
#include "headers/hardware/parallel.h" //Our own interface!
#include "headers/hardware/ports.h" //I/O support!
#include "headers/hardware/pic.h" //Interrupt support!

struct
{
ParallelOutputHandler outputhandler;
ParallelControlOUTHandler controlouthandler;
ParallelControlINHandler controlinhandler;
ParallelStatusHandler statushandler;
byte outputdata; //Mirror of last written data!
byte controldata; //Mirror of last written control!
byte IRQEnabled; //IRQs enabled?
byte IRQraised; //IRQ raised?
byte statusregister; //Status register!
} PARALLELPORT[4]; //All parallel ports!
byte numparallelports = 0; //How many ports?

DOUBLE paralleltiming = 0.0, paralleltick = (DOUBLE)0;

void registerParallel(byte port, ParallelOutputHandler outputhandler, ParallelControlOUTHandler controlouthandler, ParallelControlINHandler controlinhandler, ParallelStatusHandler statushandler)
{
	PARALLELPORT[port].outputhandler = outputhandler;
	PARALLELPORT[port].controlouthandler = controlouthandler;
	PARALLELPORT[port].controlinhandler = controlinhandler;
	PARALLELPORT[port].statushandler = statushandler;
	PARALLELPORT[port].statusregister = 0xC0; //Default the status register to floating bus!
	if (PARALLELPORT[port].controlouthandler) //Registered control handler? Needs initializing with the current pinout!
	{
		PARALLELPORT[port].controlouthandler(0 ^ 0xF); //Initialize the output port with it's initial value!
	}
	PARALLELPORT[port].outputdata = 0x00; //Initialize output data!
	PARALLELPORT[port].controldata = 0x00; //Initialize control data(2 lower bits in the upper nibble)
	PARALLELPORT[port].IRQEnabled = 0; //IRQ disabled!
	PARALLELPORT[port].IRQraised = 0; //No IRQ raised!
}

void updateParallelStatus(byte port)
{
	INLINEREGISTER byte result;
	//Check for a new status!
	result = 0; //Default: clear input!
	if (PARALLELPORT[port].statushandler) //Valid?
	{
		result |= ((PARALLELPORT[port].statushandler())^0xC0); //Output the data? The ACK lines and BUSY lines are inversed in the parallel port itself!
	}
	else
	{
		result |= 0xC0; //Output the data? The data for ACK and BUSY is inversed, so set them always!
	}
	if ((((result^PARALLELPORT[port].statusregister)&PARALLELPORT[port].statusregister)&0x40) && (PARALLELPORT[port].IRQEnabled)) //ACK raised(this line is inverted on the read side, so the value is actually nACK we're checking) causes an IRQ?
	{
		PARALLELPORT[port].IRQraised |= 1; //Raise an IRQ!
	}
	else if ((((result^PARALLELPORT[port].statusregister)&result)&0x40) || ((PARALLELPORT[port].IRQEnabled==0) && (PARALLELPORT[port].IRQraised))) //ACK lowered or IRQs disabled and raised/requested? Lower the IRQ line!
	{
		if (PARALLELPORT[port].IRQraised & 2) //Was the IRQ raised?
		{
			switch (port) //What port are we?
			{
			case 0: //IRQ 7!
				lowerirq(7); //Throw the IRQ!
				acnowledgeIRQrequest(7); //Acnowledge!
				break;
			case 1: //IRQ 6!
				lowerirq(0x16); //Throw the IRQ!
				acnowledgeIRQrequest(0x16); //Acnowledge!
				break;
			case 2: //IRQ 5!
				lowerirq(5); //Throw the IRQ!
				acnowledgeIRQrequest(5); //Acnowledge!
			default: //unknown IRQ?
				//Don't handle: we're an unknown IRQ!
				break;
			}
		}
		PARALLELPORT[port].IRQraised = 0; //Not raised anymore!
	}
	PARALLELPORT[port].statusregister = result; //Status register!
}

void tickParallel(DOUBLE timepassed)
{
	INLINEREGISTER byte port=0;
	if (unlikely(numparallelports)) //Something to do?
	{
		paralleltiming += timepassed; //To tick!
		if (unlikely((paralleltiming>=paralleltick) && paralleltick)) //Timed?
		{
			do
			{
				port = 0; //Init port!
				do //Only process the ports we have!
				{
					updateParallelStatus(port); //Update the status!
					if (PARALLELPORT[port].IRQEnabled) //Enabled IRQ?
					{
						if ((PARALLELPORT[port].IRQraised & 3) == 1) //Are we raised high?
						{
							switch (port)
							{
								case 0: //IRQ 7!
									raiseirq(7); //Throw the IRQ!
									break;
								case 1: //IRQ 6!
									raiseirq(0x16); //Throw the IRQ!
									break;
								case 2: //IRQ 5!
									raiseirq(0x5); //Throw the IRQ!
								default: //unknown IRQ?
									//Don't handle: we're an unknown IRQ!
									break;
							}
							PARALLELPORT[port].IRQraised |= 2; //Not raised anymore! Set to a special bit value to detect by software!
						}
					}
				} while (++port<numparallelports); //Loop while not done!
				paralleltiming -= paralleltick; //Ticked!
			} while (paralleltiming>=paralleltick); //Tick as needed!
		}
	}
}

byte getParallelport(word port) //What COM port?
{
	byte result=4;
	byte highnibble = (port>>8); //3 or 2
	byte lownibble = ((port>>2)&0x3F); //2F=0, 1E=1/2
	
	switch (lownibble)
	{
	case 0x2F: //Might be port 0?
		result = (highnibble==3)?2:4; break; //LPT3 or invalid!
	case 0x1E: //Might be port 1/2?
		switch (highnibble)
		{
			case 3: result = 0; break; //LPT1!
			case 2: result = 1; break; //LPT2!
			default: result = 4; //Invalid!
		}
		break;
	default: result = 4; break;
	}
	return ((result<numparallelports) && (result<4))?result:4; //Invalid by default!
}

//Offset calculator!
#define ParallelPORT_offset(port) (port&0x3)

byte outparallel(word port, byte value)
{
	byte Parallelport;
	if ((Parallelport = getParallelport(port))==4) //Unknown?
	{
		return 0; //Error: not our port!
	}
	switch (ParallelPORT_offset(port))
	{
	case 0: //Data output?
		if (PARALLELPORT[Parallelport].outputhandler) //Valid?
		{
			PARALLELPORT[Parallelport].outputhandler(value); //Output the new data
		}

		PARALLELPORT[Parallelport].outputdata = value; //We've written data on this port!
		return 1; //We're handled!
		break;
	case 2: //Control register?
		if (PARALLELPORT[Parallelport].controlouthandler) //Valid?
		{
			PARALLELPORT[Parallelport].controlouthandler((value^0xF)&0xF); //Output the new control! INIT is active low, so inverse it!
			updateParallelStatus(Parallelport); //Make sure that the status is up-to-date!
		}
		PARALLELPORT[Parallelport].controldata = (value&0x30); //The new control data last written, only the Bi-Directional pins and IRQ pins!
		PARALLELPORT[Parallelport].IRQEnabled = (value&0x10)?1:0; //Is the IRQ enabled?
		return 1; //We're handled!
		break;
	default: //Unknown port?
		break;
	}
	return 0; //We're not handled!
}

byte inparallel(word port, byte *result)
{
	byte Parallelport;
	if ((Parallelport = getParallelport(port))==4) //Unknown?
	{
		return 0; //Error: not our port!
	}
	switch (ParallelPORT_offset(port))
	{
	case 0: //Data?
		*result = PARALLELPORT[Parallelport].outputdata; //The last written data!
		return 1; //We're handled!
		break;
	case 1: //Status?
		*result = PARALLELPORT[Parallelport].statusregister; //The status register!
		//Fill in IRQ status!
		*result &= ~4; //Clear IRQ status bit by default(IRQ occurred)!
		*result |= ((~PARALLELPORT[port].IRQraised) & 2) << 1; //Set the nIRQ bit if an interrupt didn't occurred!
		return 1; //We're handled!
		break;
	case 2: //Control register?
		if (PARALLELPORT[Parallelport].controlinhandler)
		{
			*result = ((PARALLELPORT[Parallelport].controlinhandler()^0xF)&0xF);
		}
		*result |= PARALLELPORT[Parallelport].controldata; //Our own control data!
		return 1; //We're handled!
		break;
	default: //Unknown port?
		break;
	}
	return 0; //Not supported port!
}

void initParallelPorts(byte numports)
{
	memset(&PARALLELPORT,0,sizeof(PARALLELPORT)); //Initialise our ports!
	numparallelports = MIN(numports, NUMITEMS(PARALLELPORT)); //Set with safeguard!
	register_PORTIN(&inparallel); //Register the read handler!
	register_PORTOUT(&outparallel); //Register the write handler!
	paralleltiming = 0.0;
	#ifdef IS_LONGDOUBLE
	paralleltick = (1000000000.0L/150000.0L); //150kBPS speed!
	#else
	paralleltick = (1000000000.0/150000.0); //150kBPS speed!
	#endif
}
