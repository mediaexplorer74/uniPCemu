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

#include "headers/hardware/sermouse.h" //Our typedefs!
#include "headers/support/zalloc.h" //Allocation support!
#include "headers/hardware/uart.h" //UART support for the COM port!
#include "headers/support/fifobuffer.h" //FIFO buffer support!
#include "headers/support/locks.h" //Locking support!
#include "headers/emu/input.h" //setMouseRate support!
#include "headers/support/signedness.h" //Sign conversion support!

struct
{
	byte supported; //Are we supported?
	FIFOBUFFER *buffer; //The input buffer!
	byte buttons; //Current button status!
	byte movement; //Movement detection is powered on?
	byte powered; //Are we powered on?
	byte port;
	byte previousline;
	float xmove;
	float ymove;
	byte buttons_dirty;
} SERMouse;

byte useSERMouse() //Serial mouse enabled?
{
	return SERMouse.supported; //Are we supported?
}

void SERmouse_packet_handler(byte buttons, float *xmovemm, float *ymovemm, float *xmovemickeys, float *ymovemickeys)
{
	int_32 curxmove;
	int_32 curymove;
	byte effectivebuttons;
	SERMouse.xmove += *xmovemickeys; //X movement!
	SERMouse.ymove += *ymovemickeys; //Y movement!
	*xmovemickeys = 0.0f; //Clear: we're processed!
	*ymovemickeys = 0.0f; //Clear: we're processed!
	SERMouse.buttons_dirty |= (buttons != SERMouse.buttons); //Buttons dirtied?
	SERMouse.buttons = buttons; //Save last button status for dirty detection!

	curxmove = (int_32)SERMouse.xmove; //x movement, in whole mickeys!
	curymove = (int_32)SERMouse.ymove; //y movement, in whole mickeys!
	if (unlikely(((((curxmove) || (curymove)) && SERMouse.movement) || (SERMouse.buttons_dirty)) && SERMouse.powered)) //Something to do and powered on?
	{
		//Process the packet into the buffer, if possible!
		if (fifobuffer_freesize(SERMouse.buffer) > 2) //Gotten enough space to process?
		{
			SERMouse.buttons_dirty = 0; //Not dirty anymore!
			//Convert buttons (packet=1=left, 2=right, 4=middle) to output (1=right, 2=left)!
			effectivebuttons = SERMouse.buttons; //Left/right/middle mouse button!
			effectivebuttons &= 3; //Only left&right mouse buttons!
			effectivebuttons = (effectivebuttons >> 1) | ((effectivebuttons & 1) << 1);  //Left mouse button and right mouse buttons are switched in the packet vs our mouse handler packet!
			byte highbits;
			byte xmove, ymove;
			//Translate our movement to valid values if needed!
			curxmove = MAX(MIN(curxmove,0x7F),-0x80); //Limit consumption!
			curymove = MAX(MIN(curymove,0x7F),-0x80); //Limit consumption!

			SERMouse.xmove -= (float)curxmove; //Handle movement!
			SERMouse.ymove -= (float)curymove; //Handle movement!

			xmove = signed2unsigned8(curxmove); //Apply consumption!
			ymove = signed2unsigned8(curymove); //Apply consumption!

			if (SERMouse.movement==0) //Not gotten movement masked?
			{
				xmove = ymove = 0; //No movement!
			}
			//Bits 0-1 are X6&X7. Bits 2-3 are Y6&Y7. They're signed values.
			highbits = ((xmove >> 6) & 0x3); //X6&X7 to bits 0-1!
			highbits |= ((ymove >> 4) & 0xC); //Y6&7 to bits 2-3!
			writefifobuffer(SERMouse.buffer, 0x40 | (effectivebuttons << 4) | highbits); //Give info and buttons!
			writefifobuffer(SERMouse.buffer, (xmove&0x3F)); //X movement!
			writefifobuffer(SERMouse.buffer, (ymove&0x3F)); //Y movement!
		}
	}
}

byte SERmouse_getStatus()
{
	//0: Clear to Send, 1: Data Set Ready, 2: Ring Indicator, 3: Carrrier detect
	return ((SERMouse.powered ? 1 : 0) | (SERMouse.movement ? 2 : 0)); //Route RTS to CTS and DTR to DSR!
}

void SERmouse_setModemControl(byte line) //Set output lines of the Serial Mouse!
{
	//0: Data Terminal Ready(we can are ready to work), 1: Request to Send(UART can receive data), 4=Set during mark state of the TxD line.
	line &= 0xF; //Ignore unused lines!
	INLINEREGISTER byte previouspower; //Previous power line detected!
	previouspower = SERMouse.powered; //Previous power present?
	SERMouse.powered = (line & 2); //Are we powered on? This is done by the RTS output!
	if (SERMouse.powered&(previouspower^SERMouse.powered)) //Powered on? We're performing a mouse reset(Repowering the mouse)!
	{
		fifobuffer_clear(SERMouse.buffer); //Flush the FIFO buffer until last input!
		writefifobuffer(SERMouse.buffer, 'M'); //We respond with an ASCII 'M' character on reset.
	}
	SERMouse.movement = (line&1); //Allow movement to be used? Clearing DTR makes it not give Movement Input(it powers the lights that detect movement).
	SERMouse.previousline = line; //Previous line!
}

byte serMouse_readData()
{
	byte result;
	if (readfifobuffer(SERMouse.buffer, &result))
	{
		return result; //Give the data!
	}
	return 0; //Nothing to give!
}

byte serMouse_hasData() //Do we have data for input?
{
	byte temp;
	return peekfifobuffer(SERMouse.buffer, &temp); //Do we have data to receive?
}

void initSERMouse(byte enabled)
{
	memset(&SERMouse, 0, sizeof(SERMouse));
	SERMouse.supported = enabled; //Use serial mouse?
	if (useSERMouse()) //Is this mouse enabled?
	{
		SERMouse.port = allocUARTport(); //Try to allocate a port to use!
		if (SERMouse.port==0xFF) //Unable to allocate?
		{
			SERMouse.supported = 0; //Unsupported!
			goto unsupportedUARTMouse;
		}
		SERMouse.buffer = allocfifobuffer(16,1); //Small input buffer!
		UART_registerdevice(SERMouse.port,&SERmouse_setModemControl,&SERmouse_getStatus,&serMouse_hasData,&serMouse_readData,NULL); //Register our UART device!
		setMouseRate(40.0f); //We run at 40 packets per second!
	}
	else
	{
		SERMouse.port = allocUARTport(); //Try to allocate a port to use!
		//No need to register the device: just us an empty port with nothing connected to it!
		unsupportedUARTMouse:
		SERMouse.buffer = NULL; //No buffer present!
	}
}

void doneSERMouse()
{
	if (SERMouse.buffer) //Allocated?
	{
		free_fifobuffer(&SERMouse.buffer); //Free our buffer!
	}
}
