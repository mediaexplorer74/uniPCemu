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

#include "headers/hardware/inboard.h" //Inboard support!
#include "headers/cpu/cpu.h" //CPU support! 
#include "headers/hardware/ports.h" //I/O port support!
#include "headers/hardware/8042.h" //8042 support!
#include "headers/mmu/mmuhandler.h" //MMU support!

//Define to log all information written to the Inboard hardware ports!
//#define INBOARD_DIAGNOSTICS

extern byte CPU386_WAITSTATE_DELAY; //386+ Waitstate, which is software-programmed?
extern byte is_XT; //XT?
extern Controller8042_t Controller8042; //The PS/2 Controller chip!
extern byte MoveLowMemoryHigh; //Move HMA physical memory high?
byte inboard386_speed = 0; //What speed to use? Level 0-3!
const byte effective_waitstates[2][4] = {{94,47,24,0},{30,16,8,0}}; //The Wait States! First AT(compatibility case), then XT! It seems to be roughly a x0.5 multiplier except the final one. XT contains known values, AT contains measured values using the BIOS ROM DMA test validation.
extern byte is_Compaq; //Are we emulating an Compaq architecture?
extern byte is_i430fx; //Are we emulating a i430fx architecture?

byte Inboard_readIO(word port, byte *result)
{
	return 0; //Unknown port!
}

void refresh_outputport(); //For letting the 8042 refresh the output port!

void updateInboardWaitStates()
{
	CPU386_WAITSTATE_DELAY = effective_waitstates[is_XT][inboard386_speed]; //What speed to slow down, in cycles?
}

byte Inboard_writeIO(word port, byte value)
{
	switch (port)
	{
		case 0x60: //Special flags? Used for special functions on the Inboard 386!
			switch (value) //Special Inboard 386 commands?
			{
			case 0xDD: //Disable A20 line?
			case 0xDF: //Enable A20 line?
				SETBITS(Controller8042.outputport,1,1,GETBITS(value,0,1)); //Wrap arround: disable/enable A20 line!
				refresh_outputport(); //Handle the new output port!
				return 1;
				break;
			default: //Unsupported?
				#ifdef INBOARD_DIAGNOSTICS
				dolog("inboard","Unknown Inboard port 60h command: %02X",value);
				#endif
				break;
			}
			return 1; //Handled!
			break;
		case 0x670: //Special flags 2? Used for special functions on the Inboard 386!
			MoveLowMemoryHigh = 1; //Disable/enable the HMA memory or BIOS ROM! Move memory high now!
			switch (value) //What command to execute?
			{
				//case 0x1F: //Enable/disable the cache? Unknown result!
					//MoveLowMemoryHigh = GETBITS(~value,0,1); //Disable/enable the HMA memory or BIOS ROM! Written values 1E/1F in default configuration!
					//break;
				case 0x00: //Level 1 speed? 30 Wait states!
					inboard386_speed = 0; //Level 1!
					break;
				case 0x0E: //Level 2 speed? 16 Wait states!
					inboard386_speed = 1; //Level 2!
					break;
				case 0x16: //Level 3 speed? 8 Wait States!
					inboard386_speed = 2; //Level 3!
					break;
				case 0x1E: //Level 4 speed? 0 Wait States!
					inboard386_speed = 3; //Level 4!
					break;
				default: //Unknown command?
					#ifdef INBOARD_DIAGNOSTICS
					dolog("inboard","Inboard: unknown port 670h command: %02X",value); //Set the speed level!
					#endif
					break;
			}
			MMU_updatemaxsize(); //updated the maximum size!
			updateInboardWaitStates(); //Update the 80386 Wait States!
			return 1; //Handled!
			break;
		default:
			break;
	}
	return 0; //Unknown port!
}

extern MMU_type MMU;

void initInboard(byte initFullspeed) //Initialize the Inboard chipset, if needed for the current CPU!
{
	//Default memory addressable limit is specified by the MMU itself already, so don't apply a new limit, unless we're used!
	MoveLowMemoryHigh = 7; //Default: enable the HMA memory and enable the memory hole and BIOS ROM!
	inboard386_speed = 0; //Default speed: slow!
	if (initFullspeed) //Full speed init instead?
	{
		inboard386_speed = 3; //Switch to full speed instead!
	}
	CPU386_WAITSTATE_DELAY = 0; //No Wait States!
	//Add any Inboard support!
	if (((EMULATED_CPU==CPU_80386)||(EMULATED_CPU>=CPU_80486)) && (is_Compaq==0) && (!is_i430fx)) //XT/AT 386/486? We're an Inboard 386!
	{
		MoveLowMemoryHigh = 0; //Default: disable the HMA memory and enable the memory hole and BIOS ROM!
		if (MMU.size>=0x100000) //1MB+ detected?
		{
			MMU.maxsize = (MMU.size&0xFFF00000); //Round memory down to 1MB chunks!
			if (MMU.maxsize==0) MMU.maxsize = 0x100000; //1MB at least!
			MMU.maxsize -= (0x100000-0xA0000); //Substract reserved memory to be inaddressable!
			MMU_updatemaxsize(); //updated the maximum size!
		}
		register_PORTOUT(&Inboard_writeIO);
		register_PORTIN(&Inboard_readIO);
		updateInboardWaitStates(); //Set the default Wait States!
	}
	MMU_updatemaxsize(); //updated the maximum size!
}
