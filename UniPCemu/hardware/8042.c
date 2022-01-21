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
#include "headers/hardware/pic.h" //Interrupt support (IRQ 1&12)
#include "headers/hardware/ports.h" //Port support!
#include "headers/hardware/8042.h" //Our own functions!
#include "headers/cpu/mmu.h" //For wraparround 1/3/5/... MB! (A20 line)
#include "headers/support/log.h" //Logging support!
#include "headers/support/locks.h" //Locking support!
#include "headers/cpu/cpu.h" //CPU reset support!
#include "headers/hardware/i430fx.h" //i430fx support!
#include "headers/mmu/mmuhandler.h" //MMU support!
#include "headers/emu/emucore.h" //RESET line support!

//Are we disabled?
#define __HW_DISABLED 0

#define BUFFERSIZE_8042 64

//Define this to log all 8042 reads and writes!
//#define LOG8042

byte force8042 = 0; //Force 8042 controller handling?
extern byte is_XT; //Are we emulating a XT architecture?

byte translation8042[0x100] = { //Full 8042 translation table!
	0xff, 0x43, 0x41, 0x3f,	0x3d, 0x3b, 0x3c, 0x58,	0x64, 0x44, 0x42, 0x40,	0x3e, 0x0f, 0x29, 0x59,
	0x65, 0x38, 0x2a, 0x70,	0x1d, 0x10, 0x02, 0x5a,	0x66, 0x71, 0x2c, 0x1f,	0x1e, 0x11, 0x03, 0x5b,
	0x67, 0x2e, 0x2d, 0x20,	0x12, 0x05, 0x04, 0x5c,	0x68, 0x39, 0x2f, 0x21,	0x14, 0x13, 0x06, 0x5d,
	0x69, 0x31, 0x30, 0x23,	0x22, 0x15, 0x07, 0x5e,	0x6a, 0x72, 0x32, 0x24,	0x16, 0x08, 0x09, 0x5f,
	0x6b, 0x33, 0x25, 0x17,	0x18, 0x0b, 0x0a, 0x60,	0x6c, 0x34, 0x35, 0x26,	0x27, 0x19, 0x0c, 0x61,
	0x6d, 0x73, 0x28, 0x74,	0x1a, 0x0d, 0x62, 0x6e,	0x3a, 0x36, 0x1c, 0x1b,	0x75, 0x2b, 0x63, 0x76,
	0x55, 0x56, 0x77, 0x78,	0x79, 0x7a, 0x0e, 0x7b,	0x7c, 0x4f, 0x7d, 0x4b,	0x47, 0x7e, 0x7f, 0x6f,
	0x52, 0x53, 0x50, 0x4c,	0x4d, 0x48, 0x01, 0x45,	0x57, 0x4e, 0x51, 0x4a,	0x37, 0x49, 0x46, 0x54,
	0x80, 0x81, 0x82, 0x41,	0x54, 0x85, 0x86, 0x87,	0x88, 0x89, 0x8a, 0x8b,	0x8c, 0x8d, 0x8e, 0x8f,
	0x90, 0x91, 0x92, 0x93,	0x94, 0x95, 0x96, 0x97,	0x98, 0x99, 0x9a, 0x9b,	0x9c, 0x9d, 0x9e, 0x9f,
	0xa0, 0xa1, 0xa2, 0xa3,	0xa4, 0xa5, 0xa6, 0xa7,	0xa8, 0xa9, 0xaa, 0xab,	0xac, 0xad, 0xae, 0xaf,
	0xb0, 0xb1, 0xb2, 0xb3,	0xb4, 0xb5, 0xb6, 0xb7,	0xb8, 0xb9, 0xba, 0xbb,	0xbc, 0xbd, 0xbe, 0xbf,
	0xc0, 0xc1, 0xc2, 0xc3,	0xc4, 0xc5, 0xc6, 0xc7,	0xc8, 0xc9, 0xca, 0xcb,	0xcc, 0xcd, 0xce, 0xcf,
	0xd0, 0xd1, 0xd2, 0xd3,	0xd4, 0xd5, 0xd6, 0xd7,	0xd8, 0xd9, 0xda, 0xdb,	0xdc, 0xdd, 0xde, 0xdf,
	0xe0, 0xe1, 0xe2, 0xe3,	0xe4, 0xe5, 0xe6, 0xe7,	0xe8, 0xe9, 0xea, 0xeb,	0xec, 0xed, 0xee, 0xef,
	0x00, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};

/*

PS/2 Controller chip (8042)

*/

extern byte is_Compaq; //To emulate Compaq 8042-compatible controller?

//Keyboard has higher input priority: IRQ1 expects data always, so higher priority!
byte ControllerPriorities[2] = {0,1}; //Port order to check if something's there 1=Second port, 0=First port else no port!

Controller8042_t Controller8042; //The PS/2 Controller chip!

void give_8042_output(byte data) //Give 8042 input (internal!)
{
	writefifobuffer(Controller8042.buffer,data);
}

void input_lastwrite_8042()
{
	fifobuffer_gotolast(Controller8042.buffer); //Goto last!	
}

OPTINLINE void PS2_lowerirq(byte which)
{
	if (which) //Second port?
	{
		Controller8042.outputport &= ~0x20; //Lower second IRQ!
		lowerirq(12); //Lower secondary IRQ!
		acnowledgeIRQrequest(12); //Acnowledge!
	}
	else //First port?
	{
		Controller8042.outputport &= ~0x10; //Lower first IRQ!
		lowerirq(1); //Lower primary IRQ!
		acnowledgeIRQrequest(1); //Acnowledge!
	}
}

OPTINLINE void PS2_raiseirq(byte which)
{
	if (which) //Second port?
	{
		Controller8042.outputport |= 0x20; //Raise second IRQ!
		raiseirq(12); //Lower secondary IRQ!
	}
	else //First port?
	{
		Controller8042.outputport |= 0x10; //Raise first IRQ!
		raiseirq(1); //Lower primary IRQ!
	}
}

byte fill8042_output_buffer(byte flags) //Fill input buffer from full buffer!
{
	byte whatport;
	static byte readdata = 0x00;
	if (!(Controller8042.status_buffer&1)) //Buffer empty?
	{
		Controller8042.status_buffermask = ~0; //Enable all bits to be viewed by default!
		if (is_XT==0) //We're an AT?
		{
			if (Controller8042.readoutputport) //Read the output port?
			{
				if (Controller8042.readoutputport==1) //8042-compatible read?
				{
					Controller8042.output_buffer = Controller8042.outputport; //Read the output port directly!
				}
				else //Compaq special read?
				{
					/*
					Compaq: 8042 places the real values of port 2 except for bits 4 and 5 which are given a new definition in the output buffer. No output buffer full is generated.
					if bit 5 = 0, a 9-bit keyboard is in use. Superfury: This must be an XT keyboard(receiving: 10 bits: 1 start bit, 8 data bits, 1 stop bit
					if bit 5 = 1, an 11-bit keyboard is in use. Superfury: This must be an AT keyboard receiving(receiving: 11 bits: 1 start bit, 8 data bits, 1 parity bit(adds to the checksum, which must be odd to make this transfer valid), 1 stop bit
					if bit 4 = 0, outp-buff-full interrupt disabled
					if bit 4 = 1, output-buffer-full int. enabled
					*/
					Controller8042.output_buffer = (Controller8042.outputport&0xCF)|(PS2_FIRSTPORTINTERRUPTENABLED(Controller8042)<<4); //Read the output port directly!
					Controller8042.status_buffermask &= ~1; //Disable the full bit: hide it!
				}
				Controller8042.status_buffer &= ~0x20; //Clear AUX bit!
				Controller8042.status_buffer |= 0x1; //Set output buffer 
				Controller8042.readoutputport = 0; //We're done reading!
				whatport = 0; //Port #0!
				goto processoutput; //Don't process normally!
			}
			if (Controller8042.Read_RAM) //Write to VRAM byte?
			{
				Controller8042.output_buffer = Controller8042.RAM[Controller8042.Read_RAM-1]; //Get data in RAM!
				Controller8042.Read_RAM = 0; //Not anymore!
				Controller8042.status_buffer &= ~0x20; //Clear AUX bit!
				Controller8042.status_buffer |= 0x1; //Set output buffer 
				whatport = 0; //Port #0!
				goto processoutput; //Don't process normally!
			}
		}
		if (readfifobuffer(Controller8042.buffer, &Controller8042.output_buffer)) //Gotten something from 8042?
		{
			Controller8042.status_buffer &= ~0x20; //Clear AUX bit!
			Controller8042.status_buffer |= 0x1; //Set output buffer full!
			whatport = 0; //Port #0!
			goto processoutput; //We've received something!
		}
		else //Input from hardware?
		{
			byte portorder;
			for (portorder = 0;portorder < 2;portorder++) //Process all our ports!
			{
				whatport = ControllerPriorities[portorder]; //The port to check!
				if (whatport < 2) //Port has priority and available?
				{
					if ((PS2_FIRSTPORTDISABLED(Controller8042)|(PS2_SECONDPORTDISABLED(Controller8042)<<1))&(1<<whatport)) //Port disabled?
						continue; //This port is disabled, don't receive!
					if (Controller8042.portread[whatport] && Controller8042.portpeek[whatport]) //Read handlers from the first PS/2 port available?
					{
						if (Controller8042.portpeek[whatport](&readdata)) //Got something?
						{
 							readdata = Controller8042.portread[whatport](); //Execute the handler!
							//Handle first port translation here, if needed!
							if (PS2_FIRSTPORTTRANSLATION(Controller8042) && (!whatport)) //Translating the first port?
							{
								if (readdata == 0xF0) //Escaped data?
								{
									Controller8042.TranslationEscaped = 0x80; //We're escaped!
									return 1; //Abort: we're escaped, so translate us!
								}
								else //Non-escaped or escaped data?
								{
									readdata = Controller8042.TranslationEscaped|translation8042[readdata]; //Translate it according to our lookup table!
									Controller8042.TranslationEscaped = 0; //We're not escaped anymore!
								}
							}
							Controller8042.output_buffer = readdata; //This has been read!
							Controller8042.status_buffer |= 0x1; //Set input buffer full!
							processoutput:
							if (whatport) //AUX port?
							{
								Controller8042.status_buffer |= 0x20; //Set AUX bit!
								if (PS2_SECONDPORTINTERRUPTENABLED(Controller8042))
								{
									PS2_lowerirq(1); //Lower secondary IRQ!
									if (flags&1) PS2_raiseirq(1); //Raise secondary IRQ!
								}
								else
								{
									if (flags&1)
									{
										PS2_lowerirq(1); //Lower secondary IRQ!
									}
								}
								if (flags&1)
								{
									PS2_lowerirq(0); //Lower primary IRQ!
								}
							}
							else //Non-AUX?
							{
								Controller8042.status_buffer &= ~0x20; //Clear AUX bit!

								if (PS2_FIRSTPORTINTERRUPTENABLED(Controller8042))
								{
									PS2_lowerirq(0); //Lower primary IRQ!
									if (flags&1) PS2_raiseirq(0); //Raise primary IRQ!
								}
								else
								{
									if (flags&1)
									{
										PS2_lowerirq(0); //Lower primary IRQ!
									}
								}
								if (flags&1)
								{
									PS2_lowerirq(1); //Lower secondary IRQ!
								}
							}
							return 1; //We've received something!
						}
					}
				}
			}
		}
	}
	return 0; //We've received nothing!
}

void reset8042() //Reset 8042 up till loading BIOS!
{
	FIFOBUFFER *oldbuffer = Controller8042.buffer; //Our fifo buffer?
	memset(&Controller8042,0,sizeof(Controller8042)); //Init to 0!
	Controller8042.buffer = oldbuffer; //Restore buffer!
	byte newRAM0;
	newRAM0 = is_XT ? 0x01 : 0x50; //New RAM0!
	if (((Controller8042.data[0] & 0x10) == 0x10) && ((newRAM0 & 0x10) == 0)) //Was disabled and is enabled?
	{
		if (Controller8042.portenabledhandler[0]) //Enabled handler?
		{
			Controller8042.portenabledhandler[0](2); //Handle the hardware being turned on by it resetting!
		}
	}
	else if (((Controller8042.data[0] & 0x10) == 0x00) && ((newRAM0 & 0x10) == 0x10)) //Was enabled and is disabled?
	{
		if (Controller8042.portenabledhandler[0]) //Enabled handler?
		{
			Controller8042.portenabledhandler[0](0x82); //Handle the hardware being turned on by it resetting!
		}
	}
	if (((Controller8042.data[0] & 0x20) == 0x20) && ((newRAM0 & 0x20) == 0)) //Was disabled and is enabled?
	{
		if (Controller8042.portenabledhandler[1]) //Enabled handler?
		{
			Controller8042.portenabledhandler[1](2); //Handle the hardware being turned on by it resetting!
		}
	}
	else if (((Controller8042.data[0] & 0x20) == 0x00) && ((newRAM0 & 0x20) == 0x20)) //Was enabled and is disabled?
	{
		if (Controller8042.portenabledhandler[1]) //Enabled handler?
		{
			Controller8042.portenabledhandler[1](0x82); //Handle the hardware being turned on by it resetting!
		}
	}
	Controller8042.RAM[0] = newRAM0; //Init default status! Disable first port and enable translation!
	Controller8042.status_buffermask = ~0; //Default: enable all bits to be viewed!
}

DOUBLE timing8042=0.0, timing8042_tick=0.0;
uint_64 clocks8042=0; //How many clock ticks are left?

void commandwritten_8042(); //Prototype: A command has been written to the 8042 controller?

void update8042(DOUBLE timepassed) //Update 8042 input/output timings!
{
	uint_64 clocks; //Clocks to tick!
	//Sending costs 12 clocks(1 start bit, 8 data bits, 1 parity bit, 1 stop bit, 1 ACN bit), Receiving costs 11 clocks(1 start bit, 8 data bits, 1 parity bit, 1 stop bit)
	timing8042 += timepassed; //For ticking!
	clocks = (uint_64)SAFEDIV(timing8042,timing8042_tick); //How much to tick in whole ticks!
	timing8042 -= (clocks*timing8042_tick); //Substract the clocks we tick!

	//Now clocks contains the amount of clocks we're to tick! First clock input to the device(sending data to the device has higher priority), then output from the device!
	clocks8042 += clocks; //Add the clocks we're to tick to the clocks to tick to get the new amount of clocks passed!
	byte outputpending, inputpending, outputprocessed, inputprocessed;
	outputpending = (Controller8042.status_buffer & 2); //Output pending to be sent? Takes 12 clocks
	inputpending = ((Controller8042.status_buffer & 1) == 0); //Input pending to be received? Takes 11 clocks
	outputprocessed = inputprocessed = 0; //Default: not processed!
	//Information about the clocks can be found at: http://halicery.com/8042/8042_INTERN_TXT.htm
	for (;(((clocks8042>=11) && inputpending) || ((clocks8042>=12) && outputpending));) //Enough to tick at at least once?
	{
		if (outputpending && (outputprocessed==0)) //Output buffer is full?
		{
			if (clocks8042 >= 12) //Are enough clocks ready to send?
			{
				if (Controller8042.WritePending) //Write(Input buffer) is pending?
				{
					Controller8042.status_buffer &= ~0x2; //Cleared input buffer!
					if (Controller8042.WritePending==3) //To 8042 command?
					{
						Controller8042.WritePending = 0; //Not pending anymore!
						if (is_XT==0) //We're an AT?
						{
							#ifdef LOG8042
							if (force8042 == 0) //Not forced for initialization?
							{
								dolog("8042", "Write port 0x64: %02X", value);
							}
							#endif
							Controller8042.status_high = 0; //Disable high status, we're writing a new command!
							Controller8042.command = Controller8042.input_buffer; //Set command!
							commandwritten_8042(); //Written handler!
						}
					}
					else if (Controller8042.WritePending==4) //To first PS/2 Output?
					{
						Controller8042.WritePending = 0; //Not pending anymore!
						Controller8042.output_buffer = Controller8042.input_buffer; //Input to output!
						Controller8042.status_buffer |= 0x1; //Set output buffer full!
						Controller8042.status_buffer &= ~0x20; //Clear AUX bit!
						if (PS2_FIRSTPORTINTERRUPTENABLED(Controller8042))
						{
							lowerirq(12); //Remove the mouse IRQ!
							acnowledgeIRQrequest(12); //Acnowledge!
							lowerirq(1); //Remove the keyboard IRQ!
							raiseirq(1); //Call the interrupt if neccesary!
						}
						goto finishwrite; //Abort normal process!
					}
					else if (Controller8042.WritePending==5) //To second PS/2 Output?
					{
						Controller8042.WritePending = 0; //Not pending anymore!
						Controller8042.output_buffer = Controller8042.input_buffer; //Input to output!
						Controller8042.status_buffer |= 0x1; //Set output buffer full!
						Controller8042.status_buffer |= 0x20; //Set AUX bit!
						if (PS2_SECONDPORTINTERRUPTENABLED(Controller8042))
						{
							lowerirq(1); //Remove the keyboard IRQ!
							acnowledgeIRQrequest(1); //Acnowledge!
							lowerirq(12); //Remove the mouse IRQ!
							raiseirq(12); //Call the interrupt if neccesary!
						}
						goto finishwrite; //Abort normal process!
					}
					else if (Controller8042.WritePending == 6) //To second PS/2 Output?
					{
						Controller8042.WritePending = 0; //Not pending anymore!
						Controller8042.keyboardmode = Controller8042.input_buffer; //AMI: keyboard mode! 0=ISA mode!
					}
					else
					{
						if (Controller8042.inputtingsecurity) //Inputting security string?
						{
							Controller8042.securitychecksum += Controller8042.input_buffer; //Add to the value!
							if (Controller8042.input_buffer==0)
							{
								Controller8042.inputtingsecurity = 0; //Finished inputting?
								Controller8042.securitykey = Controller8042.securitychecksum; //Set the new security key!
							}
							goto finishwrite; //Don't process normally!
						}
						if (Controller8042.writeoutputport) //Write the output port?
						{
							Controller8042.outputport = Controller8042.input_buffer; //Write the output port directly!
							refresh_outputport(); //Handle the new output port!
							Controller8042.writeoutputport = 0; //Not anymore!
							goto finishwrite; //Don't process normally!
						}
						if (Controller8042.Write_RAM) //Write to VRAM byte?
						{
							if (Controller8042.Write_RAM == 1) //Might require enabling the ports?
							{
								if (((Controller8042.data[0]&0x10)==0x10) && ((Controller8042.input_buffer&0x10)==0)) //Was disabled and is enabled?
								{
									if (Controller8042.portenabledhandler[0]) //Enabled handler?
									{
										Controller8042.portenabledhandler[0](2); //Handle the hardware being turned on by it resetting!
									}
								}
								else if (((Controller8042.data[0] & 0x10) == 0x00) && ((Controller8042.input_buffer & 0x10) == 0x10)) //Was enabled and is disabled?
								{
									if (Controller8042.portenabledhandler[0]) //Enabled handler?
									{
										Controller8042.portenabledhandler[0](0x82); //Handle the hardware being turned on by it resetting!
									}
								}
								if (((Controller8042.data[0]&0x20)==0x20) && ((Controller8042.input_buffer&0x20)==0)) //Was disabled and is enabled?
								{
									if (Controller8042.portenabledhandler[1]) //Enabled handler?
									{
										Controller8042.portenabledhandler[1](2); //Handle the hardware being turned on by it resetting!
									}
								}
								else if (((Controller8042.data[0] & 0x20) == 0x00) && ((Controller8042.input_buffer & 0x20) == 0x20)) //Was enabled and is disabled?
								{
									if (Controller8042.portenabledhandler[1]) //Enabled handler?
									{
										Controller8042.portenabledhandler[1](0x82); //Handle the hardware being turned on by it resetting!
									}
								}
							}

							Controller8042.RAM[Controller8042.Write_RAM-1] = Controller8042.input_buffer; //Set data in RAM!
							Controller8042.Write_RAM = 0; //Not anymore!
							goto finishwrite; //Don't process normally!
						}
						Controller8042.portwrite[Controller8042.WritePending-1](Controller8042.input_buffer); //Write data to the specified port!
						finishwrite: //Not a normal hardware write?
						Controller8042.WritePending = 0; //Not pending anymore!
					}
					clocks8042 -= 12; //Substract the pending data, because we're now processed and sent completely!
				}
			}
			outputprocessed = 1; //We're processed!
		}
		else if ((inputpending) && (inputprocessed==0)) //Output buffer is empty?
		{
			if (clocks8042 >= 11) //Are enough clocks ready to receive?
			{
				if (fill8042_output_buffer(1)) //Have we received something?
				{
					clocks8042 -= 11; //Substract from our clocks, because we've received something!
				}
			}
			inputprocessed = 1; //We're processed!
		}
		else if ((outputprocessed || (outputpending==0)) && ((inputprocessed) || (inputpending==0))) //Nothing to be done? Both have been checked!
			break; //Stop: 
	}
	if (!((inputpending&~inputprocessed)|(outputpending&~outputprocessed))) //Input and output isn't pending?
	{
		clocks8042 = 0; //Start counting again! Reset out sending/receiving!
	}
}

void commandwritten_8042() //A command has been written to the 8042 controller?
{
	clocks8042 = 0; //We're resetting the clock to receive!
	Controller8042.status_buffermask = ~0; //Enable all bits to be read by default (again)!
	//Handle specific PS/2 commands!
	switch (Controller8042.command) //Special command?
	{
	case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07: case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x0E: case 0x0F: //Read KBC RAM indirect(undocumented)
	case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F: //Read KBC RAM indirect(undocumented)
		Controller8042.Read_RAM = ((Controller8042.command&0x1F)+Controller8042.RAM[0xB])+1; //Read from internal RAM (value 0x01-0x20), so 1 more than our index!
		Controller8042.readoutputport = (byte)(Controller8042.inputtingsecurity = 0); //Not anymore!
		break;
	case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27: case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F: //Read "byte X" from internal RAM!
	case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37: case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D: case 0x3E: case 0x3F: //Read "byte X" from internal RAM!
		Controller8042.Read_RAM = (Controller8042.command&0x1F)+1; //Read from internal RAM (value 0x01-0x20), so 1 more than our index!
		Controller8042.readoutputport = (byte)(Controller8042.inputtingsecurity = 0); //Not anymore!
		break;
	case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47: case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F: //Write KBC RAM indirect(undocumented)
	case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57: case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F: //Write KBC RAM indirect(undocumented)
		Controller8042.Write_RAM = ((Controller8042.command&0x1F)+Controller8042.RAM[0xB])+1; //Write to internal RAM (value 0x01-0x20), so 1 more than our index!
		Controller8042.writeoutputport = (byte)(Controller8042.inputtingsecurity = 0); //Not anymore!
		break;
	case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: case 0x67: case 0x68: case 0x69: case 0x6A: case 0x6B: case 0x6C: case 0x6D: case 0x6E: case 0x6F: //Write "byte X" to internal RAM!
	case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77: case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: //Write "byte X" to internal RAM!
		Controller8042.Write_RAM = (Controller8042.command&0x1F)+1; //Write to internal RAM (value 0x01-0x20), so 1 more than our index!
		Controller8042.writeoutputport = (byte)(Controller8042.inputtingsecurity = 0); //Not anymore!
		break;
	case 0xA4: //Password Installed Test. Compaq: toggle speed.
		if (is_Compaq) break; //TODO: Compaq speed toggle
		input_lastwrite_8042(); //Force data to user!
		give_8042_output((Controller8042.has_security && (Controller8042.securitychecksum==Controller8042.securitykey))?0xFA:0xF1); //Passed: give result! No password present!
		input_lastwrite_8042(); //Force byte 0 to user!
		break;
	case 0xA5: //Load security. Compaq: Special read:
		/*
		Compaq: 8042 places the real values of port 2 except for bits 4 and 5 which are given a new definition in the output buffer. No output buffer full is generated.
			if bit 5 = 0, a 9-bit keyboard is in use
			if bit 5 = 1, an 11-bit keyboard is in use
			if bit 4 = 0, outp-buff-full interrupt disabled
			if bit 4 = 1, output-buffer-full int. enabled
		*/
		Controller8042.inputtingsecurity = is_Compaq?0:1; //We're starting to input security data until a 0 is found!
		Controller8042.readoutputport = is_Compaq?2:0; //Compaq special read?
		Controller8042.securitychecksum = 0; //Init checksum!
		if (is_Compaq) //Compaq special reed?
		{
			fill8042_output_buffer(1); //Fill the output buffer immediately, since the software has no way of identifying the special reed finished!
		}
		break;
	case 0xA6: //Enable Security. Compaq: Unknown speedfunction.
		if (is_Compaq) break; //TODO: Compaq speedfunction.
		//Unknown what to do? Simply set the new key?
		Controller8042.inputtingsecurity = 0; //Finished!
		//Superfury: This apparently starts reading the keyboard and compares it's input with the security key loaded. Result is unknown!
		break;
	case 0xA7: //Disable second PS/2 port! No ACK!
		if ((Controller8042.data[0] & 0x20) == 0) //Was turned on?
		{
			if (likely(Controller8042.portenabledhandler[1])) //Valid handler for the port?
			{
				Controller8042.portenabledhandler[1](0x82); //Reset the keyboard manually! Execute an interrupt when reset!
			}
		}
		Controller8042.data[0] |= 0x20; //Disabled!
		break;
	case 0xA8: //Enable second PS/2 port! ACK from keyboard!
		if ((Controller8042.data[0]&0x20)==0x20) //Was disabled?
		{
			if (Controller8042.portenabledhandler[1]) //Enabled handler?
			{
				Controller8042.portenabledhandler[1](2); //Handle the hardware being turned on by it resetting!
			}
		}
		Controller8042.data[0] &= ~0x20; //Enabled!
		break;
	case 0xA9: //Test second PS/2 port! Give 0x00 if passed (detected). 0x02-0x04=Not detected?
		input_lastwrite_8042(); //Force result to user!
		if (Controller8042.portwrite[1] && Controller8042.portread[1] && Controller8042.portpeek[1]) //Registered?
		{
			give_8042_output(0x00); //Passed: we have one!
		}
		else
		{
			give_8042_output(0x02); //Failed: not detected!
		}
		input_lastwrite_8042(); //Force 0xFA to user!
		break;
	case 0xAA: //Test PS/2 controller! Result: 0x55: Test passed. 0xFC: Test failed.
		//Compaq:  Initializes ports 1 and 2, disables the keyboard and clears the buffer pointers. It then places 55 in the output buffer.
		if ((Controller8042.data[0] & 0x10) == 0) //Was turned on?
		{
			if (likely(Controller8042.portenabledhandler[0])) //Valid handler for the port?
			{
				Controller8042.portenabledhandler[0](0x82); //Reset the keyboard manually! Execute an interrupt when reset!
			}
		}
		Controller8042.data[0] |= 0x10; //We disable the keyboard as a result on real hardware!
		//TODO: Compaq port 1/2 initialization
		if ((is_Compaq==0) && (!is_i430fx)) //Compaq expects 0x55!
		{
			input_lastwrite_8042(); //Force 0xFA to user!
			give_8042_output(0xFA); //ACK!
		}
		input_lastwrite_8042(); //Force 0xFA to user!
		give_8042_output(0x55); //Always OK!
		break;
	case 0xAB: //Test first PS/2 port! See Command A9!
		/*
		result values:
		0 = no error
		1 = keyboard clock line stuck low
		2 = keyboard clock line stuck high
		3 = keyboard data line is stuck low
		4 = keyboard data line stuck high
		Compaq: 5 = Compaq diagnostic feature
		*/
		//TODO: Compaq diagnostic feature
		input_lastwrite_8042(); //Force 0xFA to user!
		if (Controller8042.portwrite[0] && Controller8042.portread[0] && Controller8042.portpeek[0]) //Registered?
		{
			give_8042_output(0x00); //Passed: we have one!
		}
		else
		{
			give_8042_output(0x01); //Failed: not detected!
		}
		input_lastwrite_8042(); //Force 0xFA to user!
		break;
	case 0xAC: //Diagnostic dump: read all bytes of internal RAM!
		input_lastwrite_8042(); //Force data to user!
		give_8042_output(Controller8042.RAM[0]); //Passed: give result!
		input_lastwrite_8042(); //Force byte 0 to user!
		byte c;
		for (c=1;c<0x20;) //Process all!
		{
			give_8042_output(Controller8042.RAM[c++]); //Give result!
		}
		break;
	case 0xAD: //Disable first PS/2 port! No ACK!
		if ((Controller8042.data[0] & 0x10) == 0) //Was turned on?
		{
			if (Controller8042.portenabledhandler[0]) //Enabled handler?
			{
				Controller8042.portenabledhandler[0](0x82); //Handle the hardware being turned on by it resetting!
			}
		}
		Controller8042.data[0] |= 0x10; //Disabled!
		break;
	case 0xAE: //Enable first PS/2 port! No ACK!
		if ((Controller8042.data[0]&0x10)==0x10) //Was disabled?
		{
			if (Controller8042.portenabledhandler[0]) //Enabled handler?
			{
				Controller8042.portenabledhandler[0](2); //Handle the hardware being turned on by it resetting!
			}
		}
		Controller8042.data[0] &= ~0x10; //Enabled!
		break;
	case 0xC0: //Read controller input port?
		//Compaq:  Places status of input port in output buffer. Use this command only when the output buffer is empty
		input_lastwrite_8042(); //Force 0xFA to user!
		if ((is_Compaq == 0) || (is_i430fx)) //non-Compaq?
		{
			if (MEMsize() >= 0xA0000) Controller8042.inputport |= 0x10; //640K memory installed instead of 512K!
		}
		give_8042_output(Controller8042.inputport); //Give it fully!
		if (is_i430fx) //Special behaviour?
		{
			Controller8042.inputport = (Controller8042.inputport & ~3) | (((Controller8042.inputport + 1) & 3)); //Cycle the low input port bits!
		}
		input_lastwrite_8042(); //Force 0xFA to user!
		break;
	case 0xC1: //Copy bits 0-3 of input port to status bits 4-7. No ACK!
		Controller8042.status_buffer &= ~0xF0; //Clear bits 4-7!
		Controller8042.status_high = 0x14; //Bits 0-3 of input port shifted left 4 bits to status high!
		break;
	case 0xC2: //Copy bits 4-7 of input port to status bits 4-7. No ACK!
		Controller8042.status_buffer &= ~0xF0; //Clear bits 4-7!
		Controller8042.status_high = 0x10; //Bits 4-7 of input port shifted left 0 bits to status high!
		break;
	case 0xD0: //Next byte read from port 0x60 is read from the Controller 8042 output port!
		Controller8042.readoutputport = 1; //Next byte to port 0x60 is placed on the 8042 output port!
		Controller8042.inputtingsecurity = 0;
		Controller8042.Read_RAM = 0; //Not anymore!
		break;
	case 0xD1: //Next byte written to port 0x60 is placed on the Controller 8042 output port?
		//Compaq: The system speed bits are not set by this command. Use commands A1-A6 (!) for speed functions.
		Controller8042.writeoutputport = 1; //Next byte to port 0x60 is placed on the 8042 output port!
		Controller8042.inputtingsecurity = 0; //Not anymore!
		Controller8042.Write_RAM = 0; //Not anymore!
		break;
	case 0xD2: //Next byte written to port 0x60 is send as from the First PS/2 port!
		Controller8042.port60toFirstPS2Output = 1;
		Controller8042.inputtingsecurity = 0; //Not anymore!
		Controller8042.Write_RAM = 0; //Not anymore!
		Controller8042.port60toKeyboardMode = 0; //Not anymore!
		break;
	case 0xD3: //Next byte written to port 0x60 is send to the Second PS/2 port!
		Controller8042.port60toSecondPS2Output = 1;
		Controller8042.inputtingsecurity = 0; //Not anymore!
		Controller8042.Write_RAM = 0; //Not anymore!
		Controller8042.port60toFirstPS2Output = 0;
		Controller8042.port60toKeyboardMode = 0; //Not anymore!
		break;
	case 0xD4: //Next byte written to port 0x60 is written to the second PS/2 port
		Controller8042.has_port[1] = 1; //Send to second PS/2 port!
		Controller8042.has_port[0] = 0; //Send to second PS/2 port!
		Controller8042.inputtingsecurity = 0; //Not anymore!
		Controller8042.Write_RAM = 0; //Not anymore!
		Controller8042.port60toSecondPS2Output = 0;
		Controller8042.port60toFirstPS2Output = 0;
		Controller8042.port60toKeyboardMode = 0; //Not anymore!
		break;
	case 0xE0: //Read test inputs?
		input_lastwrite_8042(); //Force data to user!
		give_8042_output(0); //Passed: give result! Bit0=Clock, Bit1=Data
		input_lastwrite_8042(); //Force byte 0 to user!
		break;
	case 0xF0: case 0xF1: case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: case 0xF7: case 0xF8: case 0xF9: case 0xFA: case 0xFB: case 0xFC: case 0xFD: case 0xFE: case 0xFF: //Pulses!
		if (!(Controller8042.command&0x1)) //CPU reset (pulse line 0)?
		{
			emu_raise_resetline(1); //Raise the RESET line!
		}
		break;
	case 0xDD: //Enable A20 line?
	case 0xDF: //Disable A20 line?
		Controller8042.outputport = (Controller8042.outputport&(~2))|((Controller8042.command&2)^2); //Wrap arround: enable A20 line when bit 2 isn't set!
		refresh_outputport(); //Handle the new output port!
		break;
	case 0xA1: //Compaq. Unknown speedfunction?
		if (is_i430fx == 0) break; //Not i430fx? Compaq unsupported speedfunction?
		input_lastwrite_8042();
		give_8042_output(0x4E); //Needs to be above 4Dh!
		input_lastwrite_8042();
		break;
	case 0xC9: //AMI: Block PS2/PS3?
		break;
	case 0xCA: //AMI: read keyboard mode?
		if (is_i430fx == 0) break; //i430fx only!
		input_lastwrite_8042();
		give_8042_output(Controller8042.keyboardmode); //ISA mode! Could give Controller8042.keyboardmode for the last mode set? Still locked to ISA(value 00h)!
		input_lastwrite_8042();
		break;
	case 0xCB: //AMI: set keyboard mode!
		Controller8042.has_port[1] = 0; //Send to second PS/2 port!
		Controller8042.has_port[0] = 0; //Send to second PS/2 port!
		Controller8042.inputtingsecurity = 0; //Not anymore!
		Controller8042.Write_RAM = 0; //Not anymore!
		Controller8042.port60toSecondPS2Output = 0;
		Controller8042.port60toFirstPS2Output = 0;
		Controller8042.port60toKeyboardMode = 1; //To keyboard mode!
		break;
	case 0xA2: //Compaq. Unknown speedfunction?
	case 0xA3: //Compaq. Enable system speed control?
		if (is_Compaq) break; //TODO: Compaq speed functionality.
	default: //Default: output to the keyboard controller!
		//Unknown device!
		break;
	}
}

void refresh_outputport()
{
	if ((Controller8042.outputport&1)==0) //This keeps the CPU reset permanently?
	{
		emu_raise_resetline(1); //Raise the RESET line!
		Controller8042.outputport &= ~1; //Keep us locked down!
		CPU[activeCPU].permanentreset = 1; //Enter a permanent reset state!
	}
	MMU_setA20(0,(Controller8042.outputport&2)); //Enable/disable wrap arround depending on bit 2 (1=Enable, 0=Disable)!
}

void datawritten_8042(byte iscommandregister, byte data) //Data has been written?
{
	clocks8042 = 0; //We're resetting the clock to receive!

	if (Controller8042.WritePending) //Write is already pending?
	{
		return; //Abort: a write is pending!
	}

	Controller8042.input_buffer = data; //Set the data/command to send!

	if (iscommandregister) //Command register write?
	{
		Controller8042.status_buffer |= 0x8; //We've last sent a byte to the command port!
		Controller8042.status_buffer |= 2; //We're pending data to write!
		Controller8042.WritePending = 3; //This port is pending to write!
		return; //We're redirecting to the 8042!
	}

	Controller8042.status_buffer &= ~0x8; //We've last sent a byte to the data port!

	if (Controller8042.port60toFirstPS2Output) //port 60 to first/second PS2 output?
	{
		Controller8042.port60toFirstPS2Output = 0; //Not anymore!
		Controller8042.status_buffer |= 2; //We're pending data to write!
		Controller8042.WritePending = 4; //This port is pending to write!
		return; //Abort normal process!
	}

	if (Controller8042.port60toSecondPS2Output) //port 60 to first/second PS2 output?
	{
		Controller8042.port60toSecondPS2Output = 0; //Not anymore!
		Controller8042.status_buffer |= 2; //We're pending data to write!
		Controller8042.WritePending = 5; //This port is pending to write!
		return; //Abort normal process!
	}

	if (Controller8042.port60toKeyboardMode) //Port 60 to keyboard mode?
	{
		Controller8042.port60toKeyboardMode = 0; //Not anymore!
		Controller8042.status_buffer |= 2; //We're pending data to write!
		Controller8042.WritePending = 6; //This port is pending to write!
		return; //Abort normal process!
	}

	if (Controller8042.inputtingsecurity || Controller8042.writeoutputport || Controller8042.Write_RAM) //Inputting security string? Write the output port? Write to RAM byte?
	{
		Controller8042.status_buffer |= 2; //We're pending data to write!
		Controller8042.WritePending = 1; //This port is pending to write, simulate keyboard!
		return; //Don't process normally!
	}

	//Normal device write?

	if (!(Controller8042.has_port[0] || Controller8042.has_port[1])) //Neither port?
	{
		Controller8042.has_port[0] = 1; //Default to port 0!
	}

	byte c;
	for (c=0;c<2;c++) //Process output!
	{
		if (Controller8042.has_port[c]) //To this port?
		{
			if (Controller8042.RAM[0] & (0x10 << c)) //Was said port disabled?
			{
				if (Controller8042.portenabledhandler[c]) //Enabled handler?
				{
					Controller8042.portenabledhandler[c](0x2); //Handle the hardware being turned on by it resetting!
				}
			}
			Controller8042.RAM[0] &= ~(0x10<<c); //Automatically enable the controller when we're sent to!
			if (Controller8042.portwrite[c]) //Gotten handler?
			{
				Controller8042.status_buffer |= 2; //We're pending data to write!
				Controller8042.WritePending = (c+1); //This port is pending to write!
				Controller8042.has_port[c] = 0; //Reset!
				break; //Stop searching for a device to output!
			}
		}
	}
}

byte write_8042(word port, byte value)
{
	if ((port & 0xFFF8) != 0x60) return 0; //Not our port!
	switch (port) //What port?
	{
	case 0x60: //Data port: write output buffer?
		if (is_XT==0) //We're an AT?
		{
			#ifdef LOG8042
			if (force8042==0) //Not forced for initialization?
			{
				dolog("8042","Write port 0x60: %02X",value);
			}
			#endif
			datawritten_8042(0,value); //Written handler for data!
			return 1;
		}
		break;
	case 0x61: //PPI keyboard functionality for XT!
		if (is_XT) //XT machine only?
		{
			if (value & 0x80) //Clear interrupt flag and we're a XT system?
			{
				Controller8042.status_buffer &= ~0x21; //Clear output buffer full&AUX bits!
				PS2_lowerirq(0); //Lower primary IRQ!
				PS2_lowerirq(1); //Lower secondary IRQ!
			}
			if (((value^Controller8042.PortB)&0x40)) //Toggled hold keyboard clock low(0) or be active(1)?
			{
				if (value&0x40) //Set? We're enabling the controller!
				{
					if ((Controller8042.data[0] & 0x20) == 0x20) //Was disabled?
					{
						if (likely(Controller8042.portenabledhandler[0])) //Valid handler for the port?
						{
							Controller8042.portenabledhandler[0](1); //Reset the keyboard manually! Execute an interrupt when reset!
						}
					}
					Controller8042.data[0] &= ~0x20; //Enabled!
				}
				else //Keyboard line stuck low? Disable the controller!
				{
					if ((Controller8042.data[0] & 0x20) == 0) //Was enabled?
					{
						if (likely(Controller8042.portenabledhandler[0])) //Valid handler for the port?
						{
							Controller8042.portenabledhandler[0](0x81); //Reset the keyboard manually! Execute an interrupt when reset!
						}
					}
					Controller8042.data[0] |= 0x20; //Disabled!
				}
			}
			Controller8042.PortB = (value&0xC0); //Save values for reference!
			return 1;
		}
		break;
	case 0x64: //Command port: send command?
		if (is_XT==0) //We're an AT?
		{
			#ifdef LOG8042
			if (force8042 == 0) //Not forced for initialization?
			{
				dolog("8042", "Write port 0x64: %02X", value);
			}
			#endif
			datawritten_8042(1,value); //Written handler for command!
			return 1;
		}
		break;
	default:
		break;
	}
	return 0; //We're unhandled!
}

byte read_8042(word port, byte *result)
{
	if ((port & 0xFFF8) != 0x60) return 0; //Not our port!
	switch (port)
	{
	case 0x60: //Data port: Read input buffer?
		
		*result = Controller8042.output_buffer; //Read output buffer, whether or not it's present(last value is reread)!
		if (Controller8042.status_buffer&1) //Gotten data?
		{
			if ((is_XT==0) || force8042) //We're an AT system?
			{
				Controller8042.status_buffer &= ~0x21; //Clear output buffer full&AUX bits!
			}
			Controller8042.status_buffermask = ~0; //Make the buffer respond normally again, resuming normal operation!
		}
		if (is_XT == 0) //We're an AT?
		{
			PS2_lowerirq(0); //Lower primary IRQ!
			PS2_lowerirq(1); //Lower secondary IRQ!
			#ifdef LOG8042
			if (force8042==0) //Not forced for initialization?
			{
				dolog("8042", "Read port 0x60: %02X", *result);
			}
			#endif
		}
		return 1; //We're processed!
		break;
	case 0x61: //PPI keyboard functionality for XT!
		//We don't handle this, ignore us!
		*result = 0;
		return 1; //Force us to 0 by default!
		break;
	case 0x64: //Command port: read status register?
		if (is_XT==0) //We're an AT?
		{
			if (fifobuffer_freesize(Controller8042.buffer) != BUFFERSIZE_8042) //Data in our output buffer?
			{
				fill8042_output_buffer(1); //Fill our output buffer immediately, don't depend on hardware timing!
			}
		}
		*result = (Controller8042.status_buffer|0x10)|(PS2_SYSTEMPASSEDPOST(Controller8042)<<2); //Read status buffer combined with the BIOS POST flag! We're never inhabited!
		if (is_XT==0) //We're an AT?
		{
			if (Controller8042.WritePending) //Write is pending?
			{
				*result |= 2; //The write buffer is still full!
			}
			*result &= Controller8042.status_buffermask; //Apply the buffer mask for our special case!
			if (Controller8042.status_high) //High status overwritten?
			{
				*result &= 0xF; //Only low data given!
				if ((is_Compaq == 0) || (is_i430fx)) //non-Compaq?
				{
					if (MEMsize() >= 0xA0000) Controller8042.inputport |= 0x10; //640K memory installed instead of 512K!
				}
				*result |= ((Controller8042.inputport<<(Controller8042.status_high&0xF))&0xF0); //Add the high or low data to the high part of the status!
				Controller8042.status_high = 0; //Disable high status!
			}
			#ifdef LOG8042
			if (force8042 == 0) //Not forced for initialization?
			{
				dolog("8042", "Read port 0x64: %02X", *result);
			}
			#endif
		}
		return 1; //We're processed!
		break;
	default:
		break;
	}
	return 0; //Undefined!
}

void BIOS_init8042() //Init 8042&Load all BIOS!
{
	if (__HW_DISABLED) return; //Abort!
	if (Controller8042.buffer) //Gotten a buffer?
	{
		free_fifobuffer(&Controller8042.buffer); //Release our buffer, if we have one!
	}
	Controller8042.buffer = allocfifobuffer(BUFFERSIZE_8042,0); //Allocate a small buffer for us to use to commands/data!

	//First: initialise all hardware ports for emulating!
	register_PORTOUT(&write_8042);
	register_PORTIN(&read_8042);
	reset8042(); //First 8042 controller reset!
	if (is_XT==0) //IBM AT? We're setting up the input port!
	{
		/*
		From Bochs' ports.lst:
		AT keyboard controller input port bit definitions
		  bit 7	  = 0  keyboard inhibited
		  bit 6	  = 0  CGA, else MDA
		  bit 5	  = 0  manufacturing jumper installed
		  bit 4	  = 0  system RAM 512K, else 640K
		  bit 3-0      reserved

		 AT keyboard controller input port bit definitions by Compaq
		  bit 7	  = 0  security lock is locked
		  bit 6	  = 0  Compaq dual-scan display, 1=non-Compaq display
		  bit 5	  = 0  system board dip switch 5 is ON
		  bit 4	  = 0  auto speed selected, 1=high speed selected
		  bit 3	  = 0  slow (4MHz), 1 = fast (8MHz)
		  bit 2	  = 0  80287 installed, 1= no NDP installed
		  bit 1-0      reserved

		*/
		Controller8042.inputport = 0x80|0x20; //Keyboard not inhibited, Manufacturing jumper not installed.
		switch (BIOS_Settings.VGA_Mode) //What VGA mode?
		{
		case 4: //Pure CGA?
			break; //Report CGA1
		case 5: //Pure MDA?
			Controller8042.inputport |= 0x40; //Report MDA adapter!
			break; //Report MDA!
		default: //(S)VGA?
			break; //Report CGA!
		}
		if (is_Compaq && (is_i430fx==0)) //Compaq and not a i430fx?
		{
			Controller8042.inputport = (Controller8042.inputport&0xA0)|0x4C; //Patch Compaq-compatible!
		}
		else if (is_i430fx) //i430fx?
		{
			Controller8042.inputport &= ~0x4F; //Don't allow bit 6 or 3-0 to be set!
			Controller8042.inputport |= 4; //Always set?
		}
	}
	timing8042 = 0.0; //Nothing yet!
	#ifdef IS_LONGDOUBLE
	timing8042_tick = 1000000000.0L/16700.0L; //16.7kHz signal!
	#else
	timing8042_tick = 1000000000.0/16700.0; //16.7kHz signal!
	#endif
}

void BIOS_done8042()
{
	if (__HW_DISABLED) return; //Abort!
	free_fifobuffer(&Controller8042.buffer); //Free the buffer!
}

//Registration handlers!

void register_PS2PortWrite(byte port, PS2OUT handler)
{
	if (__HW_DISABLED) return; //Abort!
	port &= 1;
	Controller8042.portwrite[port] = handler; //Register!
}

void register_PS2PortRead(byte port, PS2IN handler, PS2PEEK peekhandler)
{
	if (__HW_DISABLED) return; //Abort!
	port &= 1;
	Controller8042.portread[port] = handler; //Register!
	Controller8042.portpeek[port] = peekhandler; //Peek handler!
}

void register_PS2PortEnabled(byte port, PS2ENABLEDHANDLER handler)
{
	if (__HW_DISABLED) return; //Abort!
	port &= 1;
	Controller8042.portenabledhandler[port] = handler; //Register!
}
