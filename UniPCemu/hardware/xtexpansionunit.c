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
#include "headers/hardware/ports.h" //Port I/O support!

struct
{
	byte enabled; //Unit enabled?

	//Basic latch&data
	byte expansion_pending; //Pending expansion read/write?
	uint_32 expansionaddress; //Expansion BUS address
	uint_32 expansiondata; //Current Expansion data

	//Receiver card latch&data
	byte receivercard_pending; //Pending receiver read/write?
	uint_32 receivercardaddress; //Receiver BUS address
	uint_32 receivercarddata; //Receiver data
	byte receivercard_flipflop;
} XTEXPANSIONUNIT;

void latchBUS(uint_32 address, uint_32 data)
{
	if (unlikely(XTEXPANSIONUNIT.expansion_pending)) //Pending expansion read/write?
	{
		XTEXPANSIONUNIT.expansion_pending = 0; //Not pending anymore!
		XTEXPANSIONUNIT.expansionaddress = address; //Save the current the address!
		XTEXPANSIONUNIT.expansiondata = data; //Save the data!
	}
	if (unlikely(XTEXPANSIONUNIT.receivercard_pending)) //Pending receiver card read/write?
	{
		XTEXPANSIONUNIT.receivercard_pending = 0; //Not pending anymore!
		XTEXPANSIONUNIT.receivercardaddress = address; //Save the current the address!
		XTEXPANSIONUNIT.receivercarddata = data; //Save the data!
	}
}


byte XTexpansionunit_readIO(word port, byte *result)
{
	if (likely((port&~7)!=0x210)) return 0; //Not our ports?
	switch (port)
	{
		case 0x210: //Verify expansion bus data
			*result = (XTEXPANSIONUNIT.expansion_pending)?0x00:0xFF; //Give all bits set/clear depending on set result!
			return 1;
			break;
		case 0x211: //High byte data address
			*result = ((XTEXPANSIONUNIT.expansionaddress>>8)&0xFF); //Give!
			return 1;
			break;
		case 0x212: //Low byte data address
			*result = (XTEXPANSIONUNIT.expansionaddress&0xFF); //Give!
			return 1;
			break;
		case 0x214: //Read data (receiver card port)
			*result = (XTEXPANSIONUNIT.receivercarddata&0xFF); //Give!
			return 1;
			break;
		case 0x215: //High byte of address, then Low byte (receiver card port)
			*result = ((XTEXPANSIONUNIT.receivercardaddress>>(XTEXPANSIONUNIT.receivercard_flipflop<<3))&0xFF); //High byte/low byte?
			XTEXPANSIONUNIT.receivercard_flipflop = !XTEXPANSIONUNIT.receivercard_flipflop; //Flipflop!
			return 1;
			break;
		default:
			break;
	}
	return 0; //Not an used port!
}

byte XTexpansionunit_writeIO(word port, byte value)
{
	if (likely((port&~7) != 0x210)) return 0; //Not our ports?
	switch (port)
	{
		case 0x210: //Latch expansion bus data
			XTEXPANSIONUNIT.expansion_pending = 1; //Latch the data!
			return 1;
			break;
		case 0x211: //Clear wait, test latch
			//Not needed to do anything now!
			return 1;
			break;
		case 0x213: //Enable/disable expansion unit
			XTEXPANSIONUNIT.enabled = (value&1); //Enable/disable expansion unit by default!
			return 1;
			break;
		case 0x214: //Latch data
			XTEXPANSIONUNIT.receivercard_pending = 1; //Latch the data!
			return 1;
			break;
		default:
			break;
	}
	return 0; //Not an used port!
}


void initXTexpansionunit() //Initialize the expansion unit!
{
	register_PORTIN(&XTexpansionunit_readIO); //Register our handler!
	register_PORTOUT(&XTexpansionunit_writeIO); //Register our handler!
	XTEXPANSIONUNIT.enabled = 0; //Disabled by default!
	XTEXPANSIONUNIT.receivercard_flipflop = 0; //Reset flipflop!
}
