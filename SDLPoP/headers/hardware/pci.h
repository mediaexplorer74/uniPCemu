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

#ifndef __PCI_H
#define __PCI_H

#include "headers/types.h" //Basic types!

typedef void (*PCIConfigurationChangeHandler)(uint_32 address, byte device, byte function, byte size);

#include "headers/packed.h" //Packed structure!
typedef struct PACKED
{
	word VendorID;
	word DeviceID;
	word Command;
	word Status;
	byte RevisionID;
	byte ProgIF;
	byte Subclass;
	byte ClassCode;
	byte CacheLineSize;
	byte LatencyTimer;
	byte HeaderType;
	byte BIST;
	uint_32 BAR[6]; //Our BARs!
	uint_32 CardBusCISPointer;
	word SubsystemVendorID;
	word SubsystemID;
	uint_32 ExpansionROMBaseAddress; //Header type 00 only!
	byte CapabilitiesPointer;
	word ReservedLow;
	byte ReservedHigh;
	union
	{
		uint_32 Reserved; //Header type 00 only!
		uint_32 PCIBridgeExpansionROMBaseAddress; //Header type 01 only!
	};
	byte InterruptLine;
	byte InterruptPIN;
	byte MinGrant;
	byte MaxLatency;
} PCI_GENERALCONFIG; //The entire PCI data structure!
#include "headers/endpacked.h" //End of packed structure!

/*

Note on the BAR format:
bit0=1, bit1=0: I/O port. Bits 2+ are the port.
Bit0=0: Memory address:
	Bit1-2: Memory size(0=32-bit, 1=20-bit, 2=64-bit).
	Bit3: Prefetchable
	Bits 4-31: The base address.
		The value of the BAR becomes the negated size of the memory area this takes up((~x)+1). x must be 16-byte multiple due to the low 4 bits being ROM values(see bits 0-3 above)).
		For I/O areas, this is a 2-bit mask.
		The size of the lowest set bit is the size of the window the BAR represents, which has a minimum of 16(memory) or 4(IO). (So mask FFFC for a 4-byte aperture, FFF8 for a 8-byte aperture etc.)

*/

void initPCI();
/*
register_PCI: Regiwsters a PCI device.
device: An unique device ID to use for it's handlers
function: The function number for the configuration space.
size: The size of the space, in dwords
configurationspacehandler: A handler to be called when a byte in the configuration space is changed.
*/
void register_PCI(void *config, byte device, byte function, byte size, PCIConfigurationChangeHandler configurationchangehandler); //Register a new device/function to the PCI configuration space!

void PCI_finishtransfer(); //Finished a BIU transfer?

void PCI_unusedBAR(PCI_GENERALCONFIG* config, byte BAR); //Handle updating an unused BAR!

#endif
