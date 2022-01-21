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

//PCI emulation

#include "headers/types.h" //Basic types!
#include "headers/hardware/ports.h" //I/O port support!
#include "headers/hardware/pci.h" //PCI configuration space!
#include "headers/hardware/i430fx.h" //i430fx support!
#include "headers/cpu/cpu.h" //Multi core CPU support!

byte *configurationspaces[0x100]; //All possible configuation spaces!
byte configurationsizes[0x100]; //The size of the configuration!
byte configurationfunctions[0x100]; //The hardware ID of the device configuration(0 when only one device), as used by the hardware(when registering)!
byte configurationdevices[0x100]; //The function of the configuration(0 when only one device)!
byte configurationactivedevices[0x100]; //The function of the configuration(0 when only one device)!
byte PCI_terminated = 0; //Terminated PCI access?
PCIConfigurationChangeHandler configurationchanges[0x100]; //The change handlers of PCI area data!

byte newPCIdevice;
uint_32 PCI_address, PCI_data, PCI_status; //Address data and status buffers!
byte lastwriteindex = 0;

uint_32 PCI_device, PCI_currentaddress; //What registered device and data address is used(valid after a call to PCI_decodedevice)?
byte PCI_transferring[MAXCPUS] = { 0,0 };
byte PCI_lastindex[MAXCPUS] = { 0,0 };

void PCI_finishtransfer()
{
	if (unlikely(PCI_transferring[activeCPU])) //Were we tranaferring?
	{
		PCI_transferring[activeCPU] = 0; //Not anymore!
		if (configurationchanges[PCI_device]) //Change registered?
		{
			configurationchanges[PCI_device](PCI_currentaddress|PCI_lastindex[activeCPU],configurationdevices[PCI_device],configurationfunctions[PCI_device],1); //We've updated 1 byte of configuration data!
		}
	}
}

byte PCI_decodedevice(uint_32 address)
{
	byte bus, device, function, whatregister; //To load our data into!
	if ((address&0x80000000)==0) //Disabled?
	{
		PCI_status = 0xFFFFFFFF; //Error!
		return 1; //Non-existant data port!
	}
	if (address&3) //Bits 0-1 set?
	{
		PCI_status = 0xFFFFFFFF; //Error!
		return 1; //Non-existant data port!
	}
	bus = (address>>16)&0xFF; //BUS!
	device = ((address>>11)&0x1F); //Device!
	function = ((address>>8)&0x07); //Function!
	whatregister = ((address>>2)&0x3F); //What entry into the table!
	if (bus) //Only 1 BUS supported!
	{
		PCI_status = 0xFFFFFFFF; //Error!
		return 1; //Non-existant data port!		
	}
	for (PCI_device=0;PCI_device<NUMITEMS(configurationspaces);++PCI_device) //Check all available devices!
	{
		if (configurationspaces[device]) //Valid device to check?
		{
			if (device==configurationactivedevices[PCI_device]) //Device that's targeted?
			{
				if (function==configurationfunctions[PCI_device]) //The function that's targeted?
				{
					if (whatregister<configurationsizes[PCI_device]) //Within range of allowed size of the configuration space?
					{
						PCI_currentaddress = (whatregister<<2); //What address is selected within the device, DWORD address!
						PCI_status = 0x80000000; //OK!
						return 0; //Non-existant data port!		
					}
				}
			}
		}
	}

	//Unsupported device(nothing connected)?
	PCI_status = 0xFFFFFFFF; //Error!
	return 1; //Invalid device number!
}

byte PCI_read_data(uint_32 address, byte index) //Read data from the PCI space!
{
	if (PCI_decodedevice(address))
	{
		return 0xFF; //Unknown device!
	}
	return configurationspaces[PCI_device][PCI_currentaddress|index]; //Give the configuration entry!
}

OPTINLINE void PCI_write_data(uint_32 address, byte index, byte value) //Write data to the PCI space!
{
	if (PCI_decodedevice(address)) return; //Unknown device?
	if ((PCI_currentaddress|index) > 6) //Not write protected data (identification and status)?
	{
		configurationspaces[PCI_device][PCI_currentaddress|index] = value; //Set the data!
		if (configurationchanges[PCI_device]) //Change registered?
		{
			PCI_transferring[activeCPU] = 1; //Transferring!
			PCI_lastindex[activeCPU] = index; //Last index written!
			configurationchanges[PCI_device](PCI_currentaddress|index,configurationdevices[PCI_device],configurationfunctions[PCI_device],1); //We've updated 1 byte of configuration data!
		}
	}
}

byte inPCI(word port, byte *result)
{
	if ((port&~7)!=0xCF8) return 0; //Not our ports?
	switch (port)
	{
	case 0xCF8: //Status low word low part?
	case 0xCF9: //Status low word high part?
	case 0xCFA: //Status high high low part?
	case 0xCFB: //Status high word high part?
		*result = ((PCI_status>> ((port & 3) << 3)) & 0xFF); //Read the current status byte!
		return 1;
		break;
	case 0xCFC: //Data low word low part?
	case 0xCFD: //Data low word high part?
	case 0xCFE: //Data high word low part?
	case 0xCFF: //Data high word high part?
		if ((PCI_address&0x80000000)==0) //Disabled?
		{
			return 0; //Disabled!
		}
		*result = PCI_read_data(PCI_address,port&3); //Read the current status byte!
		return 1;
		break;
	default:
		break;
	}
	return 0; //Not supported yet!
}

byte outPCI(word port, byte value)
{
	if ((port&~7) != 0xCF8) return 0; //Not our ports?
	byte bitpos; //0,8,16,24!
	switch (port)
	{
	case 0xCF8: //Address low word low part?
	case 0xCF9: //Address low word high part?
	case 0xCFA: //Address high word low part?
	case 0xCFB: //Address high word high part?
		bitpos = ((port & 3) << 3); //Get the bit position!
		if (is_i430fx) //i430fx support?
		{
			i430fx_writeaddr(port-0xCF8, &value); //Handle the address write!
		}
		PCI_address &= ~((0xFF)<<bitpos); //Clear the old address bits!
		PCI_address |= value << bitpos; //Set the new address bits!
		PCI_read_data(PCI_address,0); //Read the current address and update our status, don't handle the result!
		return 1;
		break;
	case 0xCFC: //Data low word low part?
	case 0xCFD: //Data low word high part?
	case 0xCFE: //Data high word low part?
	case 0xCFF: //Data high word high part?
		if ((PCI_address&0x80000000)==0) //Disabled?
		{
			return 0; //Disabled!
		}
		PCI_write_data(PCI_address,port&3,value); //Write the byte to the configuration space if allowed!
		return 1;
		break;
	default:
		break;
	}
	return 0; //Not supported yet!
}

//Device field is hardware-specific identifier!
void register_PCI(void *config, byte device, byte function, byte size, PCIConfigurationChangeHandler configurationchangehandler)
{
	sword deviceID=-1; //Default: no device found!
	int i;
	for (i = 0;i < (int)NUMITEMS(configurationspaces);i++) //Check for available configuration space!
	{
		if (configurationspaces[i] == config) //Already registered?
		{
			return; //Abort: we've already been registered!
		}
		if (configurationspaces[i]) //Registered device?
		{
			if (configurationdevices[i]==device) //Used device detected?
			{
				deviceID = configurationactivedevices[i]; //Use the device ID for this device we've now autodetected!
			}
		}
	}
	if (deviceID==-1) //Not used yet?
	{
		//We're generating a new device ID, based on the last device registered!
		deviceID = newPCIdevice++; //Generate a new PCI device, in ascending order!
	}
	for (i = 0;i < (int)NUMITEMS(configurationspaces);i++) //Check for available configuration space!
	{
		if (!configurationspaces[i]) //Not set yet?
		{
			configurationspaces[i] = config; //Set up the configuration!
			configurationsizes[i] = size; //What size (in dwords)!
			configurationchanges[i] = configurationchangehandler; //Configuration change handler!
			configurationdevices[i] = device; //What device!
			configurationfunctions[i] = function; //What function!
			configurationactivedevices[i] = (byte)deviceID; //Use this device ID!
			return; //We've registered!
		}
	}
}

void initPCI()
{
	PCI_address = PCI_data = PCI_status = PCI_device = PCI_currentaddress = newPCIdevice = 0; //Init data!
	register_PORTIN(&inPCI);
	register_PORTOUT(&outPCI);
	//We don't implement DMA: this is done by our own DMA controller!
	memset(&configurationspaces, 0, sizeof(configurationspaces)); //Clear all configuration spaces set!
	memset(&configurationsizes,0,sizeof(configurationsizes)); //No sizes!
	memset(&configurationchanges,0,sizeof(configurationchanges)); //No handlers!
	memset(&configurationdevices,0,sizeof(configurationdevices)); //No handlers!
	memset(&configurationfunctions,0,sizeof(configurationfunctions)); //No handlers!
	PCI_decodedevice(PCI_address); //Initialise our status!
	PCI_transferring[activeCPU] = 0; //Initialize!
}

void PCI_unusedBAR(PCI_GENERALCONFIG* config, byte BAR)
{
	if (BAR > 6) return; //Invalid BAR!
	if (BAR == 6) //ROM?
	{
		if ((config->HeaderType&0x7F) == 0x01) //PCI-to-PCI bridge?
		{
			config->PCIBridgeExpansionROMBaseAddress = 0; //Unused BAR!
		}
		else //Normal device?
		{
			config->ExpansionROMBaseAddress = 0; //Unused BAR!
		}
	}
	else
	{
		config->BAR[BAR] = 0; //Unused BAR!
	}
}