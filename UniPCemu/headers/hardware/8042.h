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

#ifndef HW8042_H
#define HW8042_H
#include "headers/types.h" //Basic types!
#include "headers/support/fifobuffer.h" //FIFO buffer support for our data!

typedef void (*PS2OUT)(byte);    /* A pointer to a PS/2 device handler Write function */
typedef byte (*PS2IN)();    /* A pointer to a PS/2 device handler Read function */
typedef int (*PS2PEEK)(byte *result);    /* A pointer to a PS/2 device handler Peek (Read without flush) function */
typedef void (*PS2ENABLEDHANDLER)(byte flags); //Reset handler for when the device is enabled using the 8042!

typedef struct
{
	//RAM!
	union
	{
		byte RAM[0x20]; //A little RAM the user can use!
		struct
		{
			byte data[0x20]; //Rest of RAM, unused by us!
		};
	};

	//Input and output buffer!
	byte input_buffer; //Contains byte read from device; read only
	byte output_buffer; //Contains byte to-be-written to device; write only

	//Status buffer!
	byte status_buffer; //8 status flags; read-only
	byte status_buffermask; //The full mask of the status buffer to use when reading it!
	/*

	bit 0: 1=Output buffer full (port 0x60)
	bit 1: 1=Input buffer full (port 0x60). There is something to read.
	bit 2: 0=Power-on reset; 1=Soft reset (by software)
	bit 3: 0=Port 0x60 was last written to; 1=Port 0x64 was last written to
	bit 4: 0=Keyboard communication is inhabited; 1=Not inhabited.
	bit 5: 1=AUX: Data is from the mouse!
	bit 6: 1=Receive timeout: keyboard probably broke.
	bit 7: 1=Parity error.

	*/

	byte Write_RAM; //To write the byte written to 0x60 to RAM index-1, reset to 0 afterwards?
	byte Read_RAM; //See Write_RAM!

	//Command written!
	byte command; //Current command given!

	//PS/2 output devices!
	byte has_port[2]; //First/second PS/2 port command/data when output?
	PS2OUT portwrite[2]; //Port Output handler!
	PS2IN portread[2]; //Port Input has been read?
	PS2PEEK portpeek[2]; //Port has data&peek function!
	PS2ENABLEDHANDLER portenabledhandler[2]; //Port has enabled handler!
	
	//Direct feedback support!
	byte port60toFirstPS2Output; //Redirect write to port 0x60 to input of first PS/2 device!
	byte port60toSecondPS2Output; //Redirect write to port 0x60 to input of second PS/2 device!
	byte port60toKeyboardMode; //Redirect write to port 0x60 to keyboard mode!

	//PS/2 output port support!
	byte writeoutputport; //PS/2 Controller Output (to the 8042 output port)? On port 60h!
	byte readoutputport; //PS/2 Controller Input (from the 8042 output port)? On port 60h!
	byte outputport; //The data output for port 60 when read/writeoutputport.
	byte inputport; //The input port value, which contains system flags!
	byte keyboardmode; //Keyboard mode for AMI BIOS!

	//Security password
	uint_32 securitychecksum; //Some ROM data containing the security string entered by the user!
	uint_32 securitykey; //The actual key of the installed security!
	byte has_security;
	
	//The output buffer itself!
	FIFOBUFFER *buffer; //The buffer for our data!
	
	word inputtingsecurity; //Inputting security string?
	byte PortB; //Port B?

	byte status_high; //Enable high status? Bits 4-7: non-zero to enable, bits 0-3: Amount of bits to shift left for the high nibble to have it's correct value!

	//Extra support for timing output/input!
	byte WritePending; //A write is pending to port 0/1!
	byte TranslationEscaped; //Are we an escaped (0xF0) output?
} Controller8042_t; //The 8042 Controller!

#define PS2_FIRSTPORTINTERRUPTENABLED(device) (device.data[0]&1)
#define PS2_SECONDPORTINTERRUPTENABLED(device) ((device.data[0]&2)>>1)
#define PS2_SYSTEMPASSEDPOST(device) ((device.data[0]&4)>>2)
#define PS2_FIRSTPORTDISABLED(device) ((device.data[0]&0x10)>>4)
#define PS2_SECONDPORTDISABLED(device) ((device.data[0]&0x20)>>5)
#define PS2_FIRSTPORTTRANSLATION(device) ((device.data[0]&0x40)>>6)

void BIOS_init8042(); //Init 8042&Load all BIOS!
void BIOS_done8042(); //Deallocates the 8042!

//The input buffer!
void give_8042_output(byte value); //Give 8042 input from hardware or 8042!

//The output port has changed?
void refresh_outputport(); //Refresh the output port actions!

//Read and write port handlers!
byte write_8042(word port, byte value); //Prototype for init port!
byte read_8042(word port, byte *result); //Prototype for init port!

//Registration of First and Second PS/2 controller!
void register_PS2PortEnabled(byte port, PS2ENABLEDHANDLER enabledhandler);
void register_PS2PortWrite(byte port, PS2OUT handler);
void register_PS2PortRead(byte port, PS2IN handler, PS2PEEK peekhandler);

void update8042(DOUBLE timepassed); //Update 8042 input/output timings!
#endif