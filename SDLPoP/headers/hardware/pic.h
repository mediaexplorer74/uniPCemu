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

#ifndef PIC_H
#define PIC_H

#include "headers/types.h" //Basic type support!

/*

List of IRQs:
0: System Timer
1: Keyboard controller
2: <Must not be used: Used to cascade signals from IRQ 8-15>
3: Serial Port Controller for COM2 (Shared with COM4)
4: Serial Port Controller for COM1 (Shared with COM3)
5: LPT Port 2 or sound card
6: Floppy Disk Controller
7: LPT Port 1 or Printers or for any parallel port if a printer isn't present.

Slave PIC:
8: RTC Timer
9: Left open (Or SCSI Host Adapter)
10: Left open (Or SCSI Or NIC)
11: See 10
12: Mouse on PS/2 connector
13: Math Co-Procesor or integrated FPU or inter-processor interrupt (use depends on OS)
14: Primary ATA channel
15: Secondary ATA channel

Info: ATA interface usually serves hard disks and CD drives.

*/

typedef void(*IRQHandler)(byte IRQ);

typedef struct
{
	uint8_t imr[2]; //mask register
	uint8_t irr[2]; //request register to be read by the emulated CPU!
	uint8_t irr2[2][0x10]; //Extended IRR for determining requesting hardware! This is the actual status of an IR line(high and low)!
	uint8_t irr3[2][0x10]; //Extended IRR for determining requesting hardware! This one is actually used to store the status from hardware until it's handled!
	uint8_t irr3_a[2][0x10]; //Ack line!
	uint8_t irr3_b[2][0x10]; //Ack line(for handling the interrupt)!
	uint8_t isr[2]; //service register
	uint8_t isr2[2][0x10]; //Alternative in-service register, for handling sources!
	uint8_t icwstep[2]; //used during initialization to keep track of which ICW we're at
	uint8_t icw[2][4]; //4 ICW bytes are used!
	uint8_t readmode[2]; //remember what to return on read register from OCW3
	uint8_t pollingmode[2]; //Polling mode enabled for this channel?
	IRQHandler acceptirq[0x10][0x10], finishirq[0x10][0x10]; //All IRQ handlers!
	byte activePIC; //What PIC is currently processing?
	byte lastinterruptIR[2]; //Last interrupt IR!
	byte intreqtracking[2]; //INTRQ tracked!
} PIC;

void init8259(); //For initialising the 8259 module!
byte in8259(word portnum, byte *result); //In port
byte out8259(word portnum, byte value); //Out port
byte PICInterrupt(); //We have an interrupt ready to process?
byte nextintr(); //Next interrupt to handle
void acnowledgeirrs(); //Acnowledge IRR!
void updateAPIC(uint_64 clockspassed, DOUBLE timepassed); //Tick the APIC in CPU clocks!
byte CPU_NMI_APIC(byte whichCPU); //NMI pin directly connected?
void registerIRQ(byte IRQ, IRQHandler acceptIRQ, IRQHandler finishIRQ); //Register IRQ handler!

//IRQnum: bits 0-3=IR number, bits 4-7=Shared line number. bit 8-10=PCI lane IR when set, bit 11=Slave PIC IR!
void raiseirq(word irqnum); //Raise IRQ from hardware request!
void lowerirq(word irqnum); //Lower IRQ from hardware request!

void acnowledgeIRQrequest(byte irqnum); //Acnowledge an IRQ request!

//APIC emulation support!
void APIC_handletermination(); //Handle termination on the APIC!
void APIC_updateWindowMSR(byte whichCPU, uint_32 lo, uint_32 hi); //Update the window MSR of the APIC!
byte APIC_memIO_rb(uint_32 offset, byte index); //Read handler for the APIC!
byte APIC_memIO_wb(uint_32 offset, byte value); //Write handler for the APIC!

void APIC_raisedIRQ(byte PIC, word irqnum);
void APIC_loweredIRQ(byte PIC, word irqnum);
void APIC_enableIOAPIC(byte enabled); //Enable the IO APIC?
void resetLAPIC(byte whichCPU, byte isHardReset); //Soft or hard reset of the APIC!
void resetIOAPIC(byte isHardReset); //Soft or hard reset of the I/O APIC!

#endif
