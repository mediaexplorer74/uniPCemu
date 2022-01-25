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

//src: http://wiki.osdev.org/Serial_Ports

//UART chip emulation.

#include "headers/hardware/pic.h" //IRQ support!
#include "headers/hardware/uart.h" //UART support (ourselves)!
#include "headers/hardware/ports.h"  //Port support!

//Hardware disabled?
#define __HW_DISABLED 0

struct
{
	byte used; //Are we an used UART port?
	//+0 is data register (transmit or receive data)
	//+1 as well as +0 have alternative
	byte InterruptEnableRegister; //Either this register or Divisor Latch when 
	byte InterruptIdentificationRegister;
	byte FIFOControlRegister; //FIFO Control register!
	byte LineControlRegister;
	byte ModemControlRegister; //Bit0=DTR, 1=RTS, 2=Alternative output 1, 3=Alternative output 2, 4=Loopback mode, 5=Autoflow control (16750 only)
	byte LiveModemControlRegister; //Modem control register to the hardware!
	byte oldLiveModemControlRegister; //Previous live modem control register!
	byte oldModemControlRegister; //Old modem control bits!
	byte LineStatusRegister; //Bit0=Data available, 1=Overrun error, 2=Parity error, 3=Framing error, 4=Break signal received, 5=THR is empty, 6=THR is empty and all bits are sent, 7=Errorneous data in FIFO.
	byte oldLineStatusRegister; //Old line status register to compare!
	byte activeModemStatus; //Bit0=CTS, 1=DSR, 2=Ring indicator, 3=Carrier detect
	byte ModemStatusRegister; //Bit4=CTS, 5=DSR, 6=Ring indicator, 7=Carrier detect; Bits 0-3=Bits 4-6 changes, reset when read.
	byte oldModemStatusRegister; //Last Modem status register values(high 4 bits)!
	byte ScratchRegister;
	byte transmitisloopback; //What we're transmitting is looped back for this transfer?
	//Seperate register alternative
	word DLAB; //The speed of transmission, 115200/DLAB=Speed set.
	byte TransmitterHoldingRegister; //Data to be written to the device!
	byte TransmitterShiftRegister; //Data we're transferring!
	byte DataHoldingRegister; //The data that's received (the buffer for the software to read when filled)! Aka Data Holding Register
	byte ReceiverBufferRegister; //The data that's being received.
	//This speed is the ammount of bits (data bits), stop bits (0=1, 1=1.5(with 5 bits data)/2(all other cases)) and parity bit when set, that are transferred per second.


	//The handlers for the device attached, if any!
	UART_setmodemcontrol setmodemcontrol;
	UART_getmodemstatus getmodemstatus;
	UART_receivedata receivedata;
	UART_senddata senddata;
	UART_hasdata hasdata;

	byte interrupt_causes[5]; //All possible causes of an interrupt!
	byte interrupt_pending[5]; //All possible pending causes that aren't handled yet!
	uint_32 receiveTiming; //UART receive timing!
	uint_32 sendTiming; //UART send timing!
	byte sendPhase; //What's happening on the sending side?
	byte receivePhase; //What's happening on the receiving side?
	uint_32 UART_bytetransfertiming; //UART byte received timing!
	uint_32 UART_DLABclock; //DLAB-based clock being used!
	uint_32 UART_DLABtimingdivider; //DLAB timing divider!
	byte output_is_marking; //Is the output marking?
	byte input_is_break; //Input is breaking?
} UART_port[4]; //All UART ports!

//Value = 5+DataBits
#define UART_LINECONTROLREGISTER_DATABITSR(UART) (UART_port[UART].LineControlRegister&3)
//0=1, 1=1.5(DataBits=0) or 2(All other cases).
#define UART_LINECONTROLREGISTER_STOPBITSR(UART) ((UART_port[UART].LineControlRegister>>2)&1)
//Parity enabled?
#define UART_LINECONTROLREGISTER_PARITYENABLEDR(UART) ((UART_port[UART].LineControlRegister>>3)&1)
//0=Odd, 1=Even, 2=Mark, 3=Space.
#define UART_LINECONTROREGISTERL_PARITYTYPER(UART) ((UART_port[UART].LineControlRegister>>4)&3)
//1=Break, 0=Normal mode
#define UART_LINECONTROLREGISTER_BREAK(UART) ((UART_port[UART].LineControlRegister>>6)&1)
//Enable address 0&1 mapping to divisor?
#define UART_LINECONTROLREGISTER_DLABR(UART) ((UART_port[UART].LineControlRegister>>7)&1)

//What cause to report for interrupt request using OUT2?
#define IRR_INTERRUPTREQUEST_CAUSE 0

//Simple cause. 0=Modem Status Interrupt, 1=Transmitter Holding Register Empty Interrupt, 2=Received Data Available Interrrupt, 3=Receiver Line Status Interrupt!
#define UART_INTERRUPTCAUSE_SIMPLECAUSER(UART) ((UART_port[UART].InterruptIdentificationRegister>>1)&3)
#define UART_INTERRUPTCAUSE_SIMPLECAUSEW(UART,val) UART_port[UART].InterruptIdentificationRegister=((UART_port[UART].InterruptIdentificationRegister&(~0x6))|((val&3)<<1))

#define UART_INTERRUPTIDENTIFICATIONREGISTER_INTERRUPTNOTPENDINGR(UART) (UART_port[UART].InterruptIdentificationRegister&1)
#define UART_INTERRUPTIDENTIFICATIONREGISTER_INTERRUPTNOTPENDINGW(UART,val) UART_port[UART].InterruptIdentificationRegister=((UART_port[UART].InterruptIdentificationRegister&(~1))|(val&1))
//0=No FIFO present, 1=Reserved, 2=FIFO Enabled, but not functioning, 3=FIFO Enabled.
#define UART_INTERRUPTIDENTIFICATIONREGISTER_ENABLE64BYTEFIFOR(UART) ((UART_port[UART].InterruptIdentificationRegister>>4)&1)
#define UART_INTERRUPTIDENTIFICATIONREGISTER_ENABLE64BYTEFIFOW(UART,val) UART_port[UART].InterruptIdentificationRegister=((UART_port[UART].InterruptIdentificationRegister&(~0x10))|((val&1)<<4))

//Full Interrupt Identification Register!

DOUBLE UART_clock = 0.0, UART_clocktick = 0.0; //The UART clock ticker!

void UART_handleInputs(); //Handle any input to the UART! Prototype!

byte allocatedUARTs;
byte allocUARTport()
{
	if (allocatedUARTs>=NUMITEMS(UART_port)) return 0xFF; //Port available?
	return allocatedUARTs++; //Get an ascending UART number!
}

void launchUARTIRQ(byte COMport, byte cause) //Simple 2-bit cause.
{
	if (!UART_port[COMport].used) return; //Unused COM port!
	UART_port[COMport].interrupt_pending[cause] |= !UART_port[COMport].interrupt_causes[cause]; //We're pending when not already causing it!
	switch (cause) //What cause?
	{
	case 0: //Modem status changed?
		if (!(UART_port[COMport].InterruptEnableRegister & 8)) return; //Don't trigger if it's disabled!
		break;
	case 1: //Ready to send? (Transmitter Register Holder Register became empty)
		if (!(UART_port[COMport].InterruptEnableRegister & 2)) return; //Don't trigger if it's disabled!
		break;
	case 2: //Received data is available?
		if (!(UART_port[COMport].InterruptEnableRegister & 1)) return; //Don't trigger if it's disabled!
		break;
	case 3: //Receiver line status changed?
		if (!(UART_port[COMport].InterruptEnableRegister & 4)) return; //Don't trigger if it's disabled!
		break;
	case 4: //IRQ request
		break; //Always valid!
	default:
		return; //Invalid cause!
		break;
	}
	//We're raising an IRQ because of the reason! Stop being pending and start being causing!
	//Prepare our info!
	UART_port[COMport].interrupt_pending[cause] = 0; //We're requesting an interrupt for this cause, when available! We're not pending anymore because we're becoming a cause!
	UART_port[COMport].interrupt_causes[cause] = 1; //We're requesting an interrupt for this cause!

	if (UART_INTERRUPTIDENTIFICATIONREGISTER_INTERRUPTNOTPENDINGR(COMport)) //Can we safely raise it(are we ready to handle it)?
	{
		//Finally launch the IRQ!
		if (COMport & 1) //COM2&COM4?
		{
			raiseirq(3); //Do IRQ!
		}
		else //COM1&COM3?
		{
			raiseirq(4); //Do IRQ!
		}
	}
}

void startUARTIRQ(byte IRQ)
{
	byte cause, port, numcauses; //What cause are we?
	byte portbase, actualport;
	portbase = (IRQ == 4) ? 0 : ((IRQ==3)?1:2); //Base port!
	if (portbase == 2) return; //Not us?
	for (port = 0;port < 2;port++) //List ports!
	{
		actualport = portbase + (port << 1); //Take the actual port!
		for (cause = 4, numcauses=5;numcauses;--cause, --numcauses) //Check all causes, in order of priority!
		{
			if ((UART_port[actualport].interrupt_causes[cause]) && (UART_INTERRUPTIDENTIFICATIONREGISTER_INTERRUPTNOTPENDINGR(actualport))) //We're is the cause?
			{
				UART_port[actualport].interrupt_causes[cause] = 0; //Reset the cause!
				UART_port[actualport].InterruptIdentificationRegister = 0; //Reset for our cause!
				if (cause == 4) //IRQ request?
				{
					UART_INTERRUPTCAUSE_SIMPLECAUSEW(actualport, IRR_INTERRUPTREQUEST_CAUSE); //Load the simple cause (8250 way)!
				}
				else
				{
					UART_INTERRUPTCAUSE_SIMPLECAUSEW(actualport, cause); //Load the simple cause (8250 way)!
				}
				UART_INTERRUPTIDENTIFICATIONREGISTER_INTERRUPTNOTPENDINGW(actualport,0); //We've activated!
				return; //Stop scanning!
			}
		}
	}
}

byte getCOMport(word port) //What COM port?
{
	byte highnibble = (port>>8); //3 or 2
	byte lownibble = ((port>>3)&0x1F); //F or E
	
	byte result;
	result = 0; //Init COM port!
	switch (lownibble) //Get COM1/3?
	{
		case 0x1F: //COM1/2
			//Base 0
			break;
		case 0x1D: //COM3/4
			result |= 2; //Base 2 (port 3/4)
			break;
		default:
			result = 4; //Illegal!
			break;
	}

	switch (highnibble)
	{
		case 0x3: //Even COM port (COM1/2)
			break;
		case 0x2: //Odd COM port (COM3/4)
			result |= 1; //Add 1!
			break;
		default:
			result = 4; //Illegal!
			break;
	}
	
	return ((result<allocatedUARTs) && (result<4))?result:4; //Invalid by default!; //Give the COM port or 4 for unregistered COM port!
}

//Offset calculator!
#define COMPORT_offset(port) (port&0x7)

void updateUARTSpeed(byte COMport, word DLAB)
{
	uint_32 newdivider;
	uint_32 transfertime;
	transfertime = (7 + UART_LINECONTROLREGISTER_DATABITSR(COMport) + UART_LINECONTROLREGISTER_STOPBITSR(COMport)); //The total amount of bits that needs to be sent! Start, Data and Stop bits!

	//Every DLAB+1 / Line Control Register-dependant bytes per second! Simple formula instead of full emulation, like the PIT!
	//The UART is based on a 1.8432 clock, which is divided by 16 for the bit clock(start, data and stop bits).
	UART_port[COMport].UART_bytetransfertiming = transfertime; //Master clock divided by 16, divided by DLAB, divider by individual transfer time is the actual data rate!

	newdivider = ((uint_32)(DLAB + 1) << 4); //Calculate the new divider!
	if (UART_port[COMport].UART_DLABtimingdivider != newdivider) //Divider changed?
	{
		UART_port[COMport].UART_DLABtimingdivider = newdivider; //Divide by 16 times DLAB for the actual clock to transfer data!
		UART_port[COMport].UART_DLABclock = 0; //Reset the clock to tick!
	}
}

byte PORT_readUART(word port, byte *result) //Read from the uart!
{
	byte COMport;
	if ((COMport = getCOMport(port))==4) //Unknown?
	{
		return 0; //Error: not our port!
	}
	switch (COMPORT_offset(port)) //What offset?
	{
		case 0: //Receiver buffer OR Low byte of Divisor Value?
			if (UART_LINECONTROLREGISTER_DLABR(COMport)) //DLAB?
			{
				*result = (UART_port[COMport].DLAB&0xFF); //Low byte!
			}
			else //Receiver buffer?
			{
				//Read from input buffer!
				UART_port[COMport].interrupt_pending[2] = 0; //We're handling this cause, if pending!
				if ((!UART_INTERRUPTIDENTIFICATIONREGISTER_INTERRUPTNOTPENDINGR(COMport)) && (UART_INTERRUPTCAUSE_SIMPLECAUSER(COMport)==2)) //We're to clear?
				{
					UART_port[COMport].InterruptIdentificationRegister = 0; //Reset the register!
					UART_INTERRUPTIDENTIFICATIONREGISTER_INTERRUPTNOTPENDINGW(COMport,1); //Reset interrupt pending!
					switch (COMport) //What port?
					{
					case 0:
					case 2:
						lowerirq(4); //Lower our IRQ if it's raised!
						acnowledgeIRQrequest(4); //Acnowledge!
						break;
					case 1:
					case 3:
						lowerirq(3); //Lower our IRQ if it's raised!
						acnowledgeIRQrequest(3); //Acnowledge!
						break;
					default:
						break;
					}
				}
				//return value with bits toggled by Line Control Register!
				*result = UART_port[COMport].DataHoldingRegister; //Receive the data, if any is available!
				if (UART_port[COMport].LineStatusRegister&0x01) //Buffer full?
				{
					UART_port[COMport].LineStatusRegister &= ~0x01; //We don't have any data anymore!
				}
				UART_handleInputs(); //Make sure that the change in line control is properly detected!
			}
			break;
		case 1: //Interrupt Enable Register?
			if (UART_LINECONTROLREGISTER_DLABR(COMport)) //DLAB?
			{
				*result = ((UART_port[COMport].DLAB>>8)&0xFF); //High byte!
			}
			else //Interrupt enable register?
			{
				//bit0 = data available
				//bit1 = transmitter empty
				//bit2 = break/error
				//bit3 = status change
				*result = UART_port[COMport].InterruptEnableRegister; //Give the register!
			}
			break;
		case 2: //Interrupt ID registers?
			*result = UART_port[COMport].InterruptIdentificationRegister&(~0xF8); //Give the register! The high 5 bits are always cleared, as per the documentation!
			UART_port[COMport].interrupt_pending[4] = 0; //We're handling this cause, if pending!
			if ((!UART_INTERRUPTIDENTIFICATIONREGISTER_INTERRUPTNOTPENDINGR(COMport)) && ((UART_INTERRUPTCAUSE_SIMPLECAUSER(COMport) == 1) || ((UART_INTERRUPTCAUSE_SIMPLECAUSER(COMport) == IRR_INTERRUPTREQUEST_CAUSE) && (IRR_INTERRUPTREQUEST_CAUSE>3)))) //We're to clear?
			{
				if (UART_INTERRUPTCAUSE_SIMPLECAUSER(COMport) == 1) //Transmit was the cause?
				{
					UART_port[COMport].interrupt_pending[1] = 0; //We're handling this cause, if pending!
				}
				UART_port[COMport].InterruptIdentificationRegister = 0; //Reset the register!
				UART_INTERRUPTIDENTIFICATIONREGISTER_INTERRUPTNOTPENDINGW(COMport,1); //Reset interrupt pending!
				switch (COMport) //What port?
				{
				case 0:
				case 2:
					lowerirq(4); //Lower our IRQ if it's raised!
					acnowledgeIRQrequest(4); //Acnowledge!
					break;
				case 1:
				case 3:
					lowerirq(3); //Lower our IRQ if it's raised!
					acnowledgeIRQrequest(3); //Acnowledge!
					break;
				default:
					break;
				}
			}
			break;
		case 3: //Line Control Register?
			*result = UART_port[COMport].LineControlRegister; //Give the register!
			break;
		case 4:  //Modem Control Register?
			*result = UART_port[COMport].ModemControlRegister; //Give the register!
			break;
		case 5: //Line Status Register?
			UART_port[COMport].interrupt_pending[3] = 0; //We're handling this cause, if pending!
			if ((!UART_INTERRUPTIDENTIFICATIONREGISTER_INTERRUPTNOTPENDINGR(COMport)) && (UART_INTERRUPTCAUSE_SIMPLECAUSER(COMport) == 3)) //We're to clear?
			{
				UART_port[COMport].InterruptIdentificationRegister = 0; //Reset the register!
				UART_INTERRUPTIDENTIFICATIONREGISTER_INTERRUPTNOTPENDINGW(COMport,1); //Reset interrupt pending!
				switch (COMport) //What port?
				{
				case 0:
				case 2:
					lowerirq(4); //Lower our IRQ if it's raised!
					acnowledgeIRQrequest(4); //Acnowledge!
					break;
				case 1:
				case 3:
					lowerirq(3); //Lower our IRQ if it's raised!
					acnowledgeIRQrequest(3); //Acnowledge!
					break;
				default:
					break;
				}
			}
			*result = (UART_port[COMport].LineStatusRegister&0x7F); //Give the register! The highest bit is always cleared, according to the documentation!
			UART_port[COMport].LineStatusRegister &= ~0x1E; //Clear the register error flags!
			break;
		case 6: //Modem Status Register?
			UART_port[COMport].interrupt_pending[0] = 0; //We're handling this cause, if pending!
			if ((!UART_INTERRUPTIDENTIFICATIONREGISTER_INTERRUPTNOTPENDINGR(COMport)) && (UART_INTERRUPTCAUSE_SIMPLECAUSER(COMport) == 0)) //We're to clear?
			{
				UART_port[COMport].InterruptIdentificationRegister = 0; //Reset the register!
				UART_INTERRUPTIDENTIFICATIONREGISTER_INTERRUPTNOTPENDINGW(COMport,1); //Reset interrupt pending!
				switch (COMport) //What port?
				{
				case 0:
				case 2:
					lowerirq(4); //Lower our IRQ if it's raised!
					acnowledgeIRQrequest(4); //Acnowledge!
					break;
				case 1:
				case 3:
					lowerirq(3); //Lower our IRQ if it's raised!
					acnowledgeIRQrequest(3); //Acnowledge!
					break;
				default:
					break;
				}
			}

			*result = UART_port[COMport].ModemStatusRegister; //Give the register!
			UART_port[COMport].ModemStatusRegister &= 0xF0; //Only keep the relevant bits! The change bits are cleared!
			UART_port[COMport].oldModemStatusRegister = UART_port[COMport].ModemStatusRegister; //Update the old modem status register!
			break;
		case 7: //Scratch register?
			//Scratch register doesn't exist on a 8250!
		default:
			return 0; //Unknown port!
	}
	return 1; //Defined port!
}

void UART_update_modemcontrol(byte COMport, byte isportwrite)
{
	if (UART_port[COMport].setmodemcontrol) //Line handler is connected and not in loopback mode?
	{
		UART_port[COMport].setmodemcontrol(UART_port[COMport].LiveModemControlRegister | ((UART_port[COMport].output_is_marking & 1) << 4) | ((UART_LINECONTROLREGISTER_BREAK(COMport)<<5))); //Update the output lines for the peripheral!
	}
	if ((UART_port[COMport].LiveModemControlRegister & 8) && isportwrite) //IRQ line raised?
	{
		//launchUARTIRQ(COMport, 4); //Launch a UART IRQ Request!
	}
	UART_port[COMport].oldLiveModemControlRegister = UART_port[COMport].LiveModemControlRegister; //Difference detection!
}

byte PORT_writeUART(word port, byte value)
{
	byte COMport;
	if ((COMport = getCOMport(port))==4) //Unknown?
	{
		return 0; //Error!
	}
	switch (COMPORT_offset(port)) //What offset?
	{
		case 0: //Output buffer OR Low byte of Divisor Value?
			if (UART_LINECONTROLREGISTER_DLABR(COMport)) //DLAB?
			{
				UART_port[COMport].DLAB &= ~0xFF; //Clear the low byte!
				UART_port[COMport].DLAB |= value; //Low byte!
				updateUARTSpeed(COMport,UART_port[COMport].DLAB); //We're updated!
			}
			else //Output buffer?
			{
				UART_port[COMport].interrupt_pending[1] = 0; //We're handling this cause, if pending!
				if ((!UART_INTERRUPTIDENTIFICATIONREGISTER_INTERRUPTNOTPENDINGR(COMport)) && (UART_INTERRUPTCAUSE_SIMPLECAUSER(COMport) == 1)) //We're to clear?
				{
					UART_port[COMport].InterruptIdentificationRegister = 0; //Reset the register!
					UART_INTERRUPTIDENTIFICATIONREGISTER_INTERRUPTNOTPENDINGW(COMport,1); //Reset interrupt pending!
					switch (COMport) //What port?
					{
					case 0:
					case 2:
						lowerirq(4); //Lower our IRQ if it's raised!
						acnowledgeIRQrequest(4); //Acnowledge!
						break;
					case 1:
					case 3:
						lowerirq(3); //Lower our IRQ if it's raised!
						acnowledgeIRQrequest(3); //Acnowledge!
						break;
					default:
						break;
					}
				}

				//Write to output buffer, toggling bits by Line Control Register!
				UART_port[COMport].LineStatusRegister &= ~0x60; //We're full, ready to transmit!
				UART_port[COMport].TransmitterHoldingRegister = value; //We're sending this!
				//Apply a local loopback for this transfer accordingly, shifting from the Transmitter Shift Register to the Receiver Shift Register!
				UART_port[COMport].LineStatusRegister = ((UART_port[COMport].LineStatusRegister & ~0x80) | ((UART_port[COMport].ModemControlRegister & 0x10) << 3)); //Are we using a loopback adapter?
				UART_handleInputs(); //Make sure that the change in line control is properly detected!
			}
			break;
		case 1: //Interrupt Enable Register?
			if (UART_LINECONTROLREGISTER_DLABR(COMport)) //DLAB?
			{
				UART_port[COMport].DLAB &= ~0xFF00; //Clear the high byte!
				UART_port[COMport].DLAB |= (value<<8); //High!
				updateUARTSpeed(COMport, UART_port[COMport].DLAB); //We're updated!
			}
			else //Interrupt enable register?
			{
				//bit0 = data available
				//bit1 = transmitter empty
				//bit2 = break/error
				//bit3 = status change
				UART_port[COMport].InterruptEnableRegister = (value & 0xF); //Set the register! Clear the undefined bits, as per the documentation!
				/*
				8250A UART documentation (PC16450C/NS16450, PC8250A/INS8250A
				Universal Asynchronous ReceiverITransmitter) says this about line status register bit 5:
				"In addition. this bit causes the UART to issue an interrupt to the CPU when the Transmit Holding Register Empty Interrupt enable is set high."
				*/
				if ((value & 2) && (UART_port[COMport].LineStatusRegister & 0x20))
				{
					UART_port[COMport].interrupt_pending[1] = 1; //Start pending an interrupt always!
				}
			}
			break;
		case 2: //FIFO control register?
			UART_port[COMport].FIFOControlRegister = value; //Set the register! Prevent bits from being set to indicate we don't have a FIFO!
			//Not used in the original 8250 UART.
			break;
		case 3: //Line Control Register?
			if ((UART_port[COMport].LineControlRegister ^ value) & 0x40) //Changed break signal?
			{
				UART_port[COMport].LineControlRegister = value; //Set the register!
				UART_update_modemcontrol(COMport, 1); //Update the modem control output!
			}
			UART_port[COMport].LineControlRegister = value; //Set the register!
			break;
		case 4: //Modem Control Register?
			UART_port[COMport].ModemControlRegister = (value & 0x1F); //Set the register! The high 3 bits are always cleared, as documented!
			//Handle anything concerning this?
			if ((UART_port[COMport].ModemControlRegister&0x10)==0) //Line handler is connected and not in loopback mode?
			{
				UART_port[COMport].LiveModemControlRegister = (UART_port[COMport].ModemControlRegister&0xF); //Save the set modem control register state for the live output and mark logic!
				if (UART_port[COMport].ModemControlRegister^UART_port[COMport].oldModemControlRegister) //Modem control register changes are posted only?
				{
					UART_update_modemcontrol(COMport,1); //Update the modem control output!
				}
			}
			if ((UART_port[COMport].ModemControlRegister^UART_port[COMport].oldModemControlRegister)&0x10) //Loopback mode enabled or disabled?
			{
				//Leave the receiver buffer alone: it will overflow if it's still filled once any transfer is made in a normal way!
				//Since we're resetting the send/receive phase(because the output lines being disconnected when entering loopback mode makes the transfer fail(anything that's sent is corrupted, while anything that's received is ignored), while disabling loopback mode aborts any transfer in progress(since it's sending the remainder of the byte, which is incorrect by itself)), 
				UART_port[COMport].LineStatusRegister |= 0x60; //The Transmitter Holding Register and Shift Register are both empty. Any transfer that's running is aborted, because the SOUT is set to marking state!
				UART_port[COMport].output_is_marking = 1; //We're in marking state now!
				UART_port[COMport].sendPhase = 0; //Abort any sending phase, as the line is disconnected!
				UART_port[COMport].receivePhase = 0; //Abort any receiving phase, as the line is disconnected!
				if (UART_port[COMport].ModemControlRegister & 0x10) //Loopback is enabled? Clear the buffers!
				{
					UART_port[COMport].LiveModemControlRegister &= 0xC; //Cleared the live output(RTS and CTS in particular)!
					UART_update_modemcontrol(COMport, 0); //Update the modem control output!
				}
				UART_handleInputs(); //Update the loopback status as required by updating the status register!
			}
			else if ((UART_port[COMport].ModemControlRegister & 0x10) && (UART_port[COMport].ModemControlRegister ^ UART_port[COMport].oldModemControlRegister)) //Loopback is enabled? Handle status changes immediately!
			{
				UART_handleInputs(); //Update the loopback status as required by updating the status register!
			}
			UART_port[COMport].oldModemControlRegister = UART_port[COMport].ModemControlRegister; //Save the old value for reference!
			break;
		case 7: //Scratch register?
			//Scratch register doesn't exist on a 8250!
		default: //Unknown write register?
			return 0;
			break;
	}
	return 1; //We're supported!
}

void UART_handleInputs() //Handle any input to the UART!
{
	int i;
	byte modemstatusinterrupt, checknewmodemstatus, linestatusbitsset;

	//Raise the IRQ for the first device to give input!
	for (i = 0;i < 4;i++) //Process all ports!
	{
		//Read the Modem Status, update bits, check for interrupts!
		modemstatusinterrupt = 0; //Last status!
		if (UART_port[i].getmodemstatus && ((UART_port[i].ModemControlRegister&0x10)==0)) //Modem status available and not in Loopback mode?
		{
			UART_port[i].activeModemStatus = UART_port[i].getmodemstatus(); //Retrieve the modem status from the peripheral!

			//Update the modem status register accordingly!
			SETBITS(UART_port[i].ModemStatusRegister,4,0xF,UART_port[i].activeModemStatus); //Set the high bits of the modem status to our input lines!
			if (unlikely((UART_port[i].activeModemStatus & 0x10) && (UART_port[i].input_is_break==0))) //Break set and not acnowledged yet?
			{
				if (((UART_port[i].LineStatusRegister & 1) == 0) && (UART_port[i].receivePhase == 0)) //Receiver buffer is empty that we can overwrite? We can start the teransfer?
				{
					UART_port[i].LineStatusRegister |= 0x18; //Break received! Also raise framing error!
					if (UART_port[i].receivedata)
					{
						UART_port[i].ReceiverBufferRegister = UART_port[i].receivedata(); //Read the data to receive! This also acnowledges the receiving of the data!
					}
					else
					{
						UART_port[i].ReceiverBufferRegister = 0x00; //Read the data to receive! This also acnowledges the receiving of the data!
					}

					//Start transferring break data...
					UART_port[i].receiveTiming = UART_port[i].UART_bytetransfertiming + 1; //Duration of the transfer!
					UART_port[i].receivePhase = 1; //Pending finish of transfer!
					UART_port[i].input_is_break = 1; //Break is acnowledged!
				}
			}
			else if (unlikely(((UART_port[i].activeModemStatus & 0x10) == 0) && UART_port[i].input_is_break)) //Break was cleared?
			{
				UART_port[i].input_is_break = 0; //Not breaking anymore!
			}
			checknewmodemstatus = ((UART_port[i].ModemStatusRegister^(UART_port[i].oldModemStatusRegister))&0xF0); //Check the new status!
		}
		else if (UART_port[i].ModemControlRegister & 0x10) //In loopback mode? Reroute the Modem Control Register to Modem Status Register and act accordingly!
		{
			//Update the modem status register accordingly!
			SETBITS(UART_port[i].ModemStatusRegister, 6, 0x3, GETBITS(UART_port[i].ModemControlRegister,2,0x3)); //Set the high bits of the modem status to our input lines!
			SETBITS(UART_port[i].ModemStatusRegister, 4, 0x1, GETBITS(UART_port[i].ModemControlRegister, 1, 0x1)); //RTS on CTS
			SETBITS(UART_port[i].ModemStatusRegister, 5, 0x1, GETBITS(UART_port[i].ModemControlRegister, 0, 0x1)); //DTR on DSR
			checknewmodemstatus = ((UART_port[i].ModemStatusRegister ^ (UART_port[i].oldModemStatusRegister)) & 0xF0); //Check the new status!
		}
		else //No status or device to report?
		{
			//Update the modem status register accordingly!
			SETBITS(UART_port[i].ModemStatusRegister, 6, 0x3, 0); //None!
			SETBITS(UART_port[i].ModemStatusRegister, 4, 0x1, 0); //None
			SETBITS(UART_port[i].ModemStatusRegister, 5, 0x1, 0); //None
			checknewmodemstatus = ((UART_port[i].ModemStatusRegister ^ (UART_port[i].oldModemStatusRegister)) & 0xF0); //Check the new status!
		}
		if (unlikely(checknewmodemstatus)) //Are we to verify the new modem status?
		{
			//First, check for interrupts to be triggered!
			modemstatusinterrupt |= (((UART_port[i].ModemStatusRegister^UART_port[i].oldModemStatusRegister) >> 4) & 0xB); //Bits have changed set bits 0,1,3? Ring has other indicators!
			modemstatusinterrupt |= (((UART_port[i].oldModemStatusRegister)&(~UART_port[i].ModemStatusRegister)) >> 4) & 0x4; //Only set the Ring lowered bit when the ring indicator is lowered!
			//Report the new delta status to the register and update it with it's new status, where not set yet.
			UART_port[i].ModemStatusRegister |= (((UART_port[i].ModemStatusRegister^UART_port[i].oldModemStatusRegister) >> 4) & 0xB); //Bits have changed set bits 0,1,3? Ring has other indicators!
			UART_port[i].ModemStatusRegister |= (((UART_port[i].oldModemStatusRegister&(~UART_port[i].ModemStatusRegister)) >> 4) & 0x4); //Only set the Ring lowered bit when the ring indicator is lowered!
			UART_port[i].oldModemStatusRegister = UART_port[i].ModemStatusRegister; //Update the old modem status register!
		}
		linestatusbitsset = ((UART_port[i].oldLineStatusRegister ^ UART_port[i].LineStatusRegister) & UART_port[i].LineStatusRegister); //What bits have been set?
		if (unlikely((linestatusbitsset & 0x1E) || (UART_port[i].interrupt_causes[3]|UART_port[i].interrupt_pending[3]))) //Line status has raised an error or required to be raised?
		{
			launchUARTIRQ(i, 3); //We're changing the Line Status Register!
		}
		if (unlikely((linestatusbitsset & 0x01) || (UART_port[i].interrupt_causes[2]|UART_port[i].interrupt_pending[2]))) //Have we received data or required to be raised?
		{
			launchUARTIRQ(i, 2); //We've received data!
		}
		if (unlikely((linestatusbitsset & 0x20) || (UART_port[i].interrupt_causes[1]|UART_port[i].interrupt_pending[1]))) //Sent a byte of data(full transmitter holder register becomes empty)?
		{
			launchUARTIRQ(i, 1); //We've sent data!
		}
		if (unlikely((modemstatusinterrupt) || (UART_port[i].interrupt_causes[0]|UART_port[i].interrupt_pending[0]))) //Status changed or required to be raised?
		{
			launchUARTIRQ(i, 0); //Modem status changed!
		}
		if (UART_port[i].interrupt_causes[4]|UART_port[i].interrupt_pending[4]) //UART IRQ request?
		{
			launchUARTIRQ(i, 4); //IRQ request!
		}
		UART_port[i].oldLineStatusRegister = UART_port[i].LineStatusRegister; //Save for difference checking!
	}
}

void updateUART(DOUBLE timepassed)
{
	byte UART; //Check all UARTs!
	uint_32 DLAB_clockticks; //DLAB clock ticks!
	uint_32 clockticks; //The clock ticks to process!
	uint_32 clocking;
	UART_clock += timepassed; //Tick our master clock!
	if (unlikely(UART_clock>=UART_clocktick)) //Ticking the UART clock?
	{
		clockticks = (uint_32)(UART_clock/UART_clocktick); //Divide the clock by the ticks to apply!
		UART_clock -= (DOUBLE)clockticks*UART_clocktick; //Rest the clocks!

		//Now we have the amount of raw clock ticks! Apply the DLAB ticking!

		//Check all UART received data!
		for (UART=0;UART<4;++UART) //Check all UARTs!
		{
			UART_port[UART].UART_DLABclock += clockticks; //Tick the DLAB-based clock!
			if (UART_port[UART].UART_DLABclock >= UART_port[UART].UART_DLABtimingdivider) //Divided tick?
			{
				DLAB_clockticks = (uint_32)(UART_port[UART].UART_DLABclock / UART_port[UART].UART_DLABtimingdivider); //Tick this much!
				UART_port[UART].UART_DLABclock -= DLAB_clockticks * UART_port[UART].UART_DLABtimingdivider; //Rest the clocks!

				clocking = DLAB_clockticks; //How many ticks to tick!
				for (; clocking; --clocking) //Process all clocks!
				{
					//Tick receiver!
					switch (UART_port[UART].receivePhase) //What receive phase?
					{
					case 0: //Checking for start of transfer?
						if ((UART_port[UART].ModemControlRegister&0x10)==0x10) break; //Can't start to receive  from connected hardware during loopback or when receiving anything from loopback mode(the receiver line is disconnected)!
						if (unlikely(!(UART_port[UART].hasdata&&UART_port[UART].receivedata))) break; //Can't receive?
						if (unlikely(UART_port[UART].hasdata())) //Do we have data to receive and not prioritizing sending data?
						{
							if (likely((UART_port[UART].LineStatusRegister & 0x01) == 0)) //No data received yet(small hack to prevent normal buffer overrun)?
							{
								UART_port[UART].ReceiverBufferRegister = UART_port[UART].receivedata(); //Read the data to receive!

								//Start transferring data...
								UART_port[UART].receiveTiming = UART_port[UART].UART_bytetransfertiming + 1; //Duration of the transfer!
								UART_port[UART].receivePhase = 1; //Pending finish of transfer!
							}
							else break; //Can't receive!
						}
						else break; //Nothing to receive?
						//Finish transferring fallthrough!
					case 1: //Transferring data?
						if (--UART_port[UART].receiveTiming) break; //Busy transferring?
						UART_port[UART].receivePhase = 2; //Finish transferring!
					case 2: //Finish transfer!
						//Finished transferring data.
						if (UART_port[UART].LineStatusRegister & 0x01) //Receiver buffer filled? Overrun!
						{
							UART_port[UART].LineStatusRegister |= 0x2; //Signal overrun! Receive the byte as normally, overwriting what's there!
						}
						UART_port[UART].DataHoldingRegister = UART_port[UART].ReceiverBufferRegister; //We've received this data!
						UART_port[UART].LineStatusRegister |= 0x01; //We've received data!
						UART_port[UART].receivePhase = 0; //Start polling again!
						break;
					}

					switch (UART_port[UART].sendPhase) //What receive phase?
					{
					case 0: //Checking for start of transfer?
						if (unlikely((UART_port[UART].LineStatusRegister & 0x20) == 0)) //Something to transfer with connected hardware or loopback?
						{
							if ((UART_port[UART].LineStatusRegister & 0x80) == 0) //Not loopback being sent?
							{
								UART_port[UART].output_is_marking = 0; //Not marking anymmore!
								UART_update_modemcontrol(UART, 0); //Updated the marking state!
								UART_port[UART].transmitisloopback = 0; //Transmit is not from loopback!
							}
							else //Sending from loopback?
							{
								UART_port[UART].transmitisloopback = 1; //Transmit is from loopback!
								UART_port[UART].LineStatusRegister &= ~0x80; //Not from loopback anymore!
							}
							//Start transferring data...
							UART_port[UART].LineStatusRegister |= 0x20; //The Transmitter Holding Register is empty!
							UART_port[UART].TransmitterShiftRegister = UART_port[UART].TransmitterHoldingRegister; //Move to shift register!
							UART_port[UART].sendTiming = UART_port[UART].UART_bytetransfertiming + 1; //Duration of the transfer!
							UART_port[UART].sendPhase = 1; //Pending finish of transfer!
						}
						else break; //Nothing to send!
						//Finish transferring fallthrough!
					case 1: //Transferring data?
						if (--UART_port[UART].sendTiming) break; //Busy transferring?
						UART_port[UART].sendPhase = 2; //Finish transferring!
					case 2: //Finish transfer!
						if (UART_port[UART].transmitisloopback)//Transmitting into loopback instead?
						{
							if (UART_port[UART].LineStatusRegister & 0x01) //Receiver buffer filled? Overrun!
							{
								UART_port[UART].LineStatusRegister |= 0x2; //Signal overrun! Receive the byte as normally, overwriting what's there!
							}
							UART_port[UART].DataHoldingRegister = UART_port[UART].TransmitterShiftRegister; //We've received this data!
							UART_port[UART].LineStatusRegister |= 0x01; //We've received data!
							UART_port[UART].transmitisloopback = 0; //We're properly received!
						}
						else //Not transmitting to the loopback adapter?
						{
							//Finished transferring data to an actual device.
							if (UART_port[UART].senddata) //Handler installed?
							{
								if (!UART_port[UART].senddata(UART_port[UART].TransmitterShiftRegister)) //Send the data, if possible!
								{
									break; //Don't handle it yet if we can't send it yet!
								}
							}
						}

						//Data is sent, so update status when finished!
						if ((UART_port[UART].LineStatusRegister & 0x20) == 0x20) //Transmitter Shift emptied to peripheral and Holding Register is still empty?
						{
							UART_port[UART].LineStatusRegister |= 0x40; //The Transmitter Holding Register and Shift Register are both empty!
						}
						UART_port[UART].sendPhase = 0; //Start polling again!
						UART_port[UART].output_is_marking = 1; //We're marking again!
						UART_update_modemcontrol(UART,0); //Updated the marking state!
						break;
					}
				}
			}
		}

		UART_handleInputs(); //Handle the input received, when needed, as well as other conditions required!
	}
}

void UART_registerdevice(byte portnumber, UART_setmodemcontrol setmodemcontrol, UART_getmodemstatus getmodemstatus, UART_hasdata hasdata, UART_receivedata receivedata, UART_senddata senddata)
{
	if (portnumber > 3) return; //Invalid port!
	//Register the handlers!
	UART_port[portnumber].used = 1; //We're an used UART port!
	UART_port[portnumber].setmodemcontrol = setmodemcontrol;
	UART_port[portnumber].hasdata = hasdata;
	UART_port[portnumber].receivedata = receivedata;
	UART_port[portnumber].senddata = senddata;
	UART_port[portnumber].getmodemstatus = getmodemstatus;
	UART_update_modemcontrol(portnumber,0); //Update the port's marking state so that the hardware knows about it!
	if (getmodemstatus) //Init status!
	{
		UART_port[portnumber].activeModemStatus = UART_port[portnumber].getmodemstatus(); //Retrieve the modem status from the peripheral!
	}
	//Update the modem status register accordingly!
	SETBITS(UART_port[portnumber].ModemStatusRegister, 4, 0xF, UART_port[portnumber].activeModemStatus); //Set the high bits of the modem status to our input lines!
	UART_port[portnumber].oldModemStatusRegister = UART_port[portnumber].ModemStatusRegister; //Set the high bits of the modem status to our input lines!
}

void initUART() //Init software debugger!
{
	if (__HW_DISABLED) return; //Abort!
	memset(&UART_port,0,sizeof(UART_port)); //Clear memory used!
	register_PORTOUT(&PORT_writeUART);
	register_PORTIN(&PORT_readUART);
	registerIRQ(3, &startUARTIRQ, NULL); //Register our IRQ finish!
	registerIRQ(4, &startUARTIRQ, NULL); //Register our IRQ finish!
	int i;
	for (i = 0;i < 4;i++)
	{
		UART_INTERRUPTIDENTIFICATIONREGISTER_INTERRUPTNOTPENDINGW(i,1); //We're not executing!
		UART_port[i].LineStatusRegister = UART_port[i].oldLineStatusRegister = 0x60; //Receiver buffer not ready for reading, Transmitter Holding register and Shift register are empty.
		UART_port[i].oldLineStatusRegister &= ~0x20; //Make sure that the IRQ becomes pending automatically, because we're an empty transmitter by default!

		//Make sure the DLAB is timed correctly!
		UART_port[i].UART_DLABtimingdivider = ((uint_32)(UART_port[i].DLAB + 1) << 4); //Calculate the new divider!
		UART_port[i].output_is_marking = 1; //Not sending anything, so output is marking!
	}
	UART_clock = 0.0; //Init our clock!
	#ifdef IS_LONGDOUBLE
	UART_clocktick = 1000000000.0L/1843200.0L; //The clock of the UART ticking!
	#else
	UART_clocktick = 1000000000.0/1843200.0; //The clock of the UART ticking!
	#endif
	allocatedUARTs = 0; //Initialize the allocated UART number!
}
