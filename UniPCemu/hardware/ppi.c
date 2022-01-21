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
#include "headers/hardware/ports.h" //I/O support!
#include "headers/cpu/mmu.h" //MMU support!
#include "headers/hardware/vga/vga.h" //VGA/EGA/CGA/MDA support!
#include "headers/emu/emucore.h" //Speed change and RESET line support!
#include "headers/emu/debugger/debugger.h" //Debugging support for logging POST codes!
#include "headers/support/log.h" //For logging POST codes!
#include "headers/hardware/pic.h" //Interrupt support!
#include "headers/fopen64.h" //64-bit fopen support!

byte SystemControlPortB=0x00; //System control port B!
byte SystemControlPortA=0x00; //System control port A!
byte PPI62, PPI63; //Default PPI switches!
byte TurboMode=0;
byte diagnosticsportoutput = 0x00;
extern byte singlestep; //Enable EMU-driven single step!
sword diagnosticsportoutput_breakpoint = -1; //Breakpoint set?
sword breakpoint_comparison = -1; //Breakpoint comparison value!
uint_32 breakpoint_timeout = 1; //Timeout for the breakpoint to become active, in instructions! Once it becomes 0(and was 1), it triggers the breakpoint!
uint_32 breakpoint_timeoutoriginal = 1; //Original timeout value!
extern byte NMI; //NMI control on XT support!

extern byte is_XT; //Are we using XT architecture?
extern byte is_Compaq; //Are we emulating an Compaq architecture?
extern byte is_i430fx; //Are we emulating a i430fx architecture?

byte readPPI62()
{
	byte result=0;
	//Setup PPI62 as defined by System Control Port B!
	if (SystemControlPortB&8) //Read high switches?
	{
		//Bit0=Display 0
		//Bit1=Display 1
		//Bit2=Floppy drives in system(-1) low
		//Bit3=Floppy drives in system(-1) high
		if (((getActiveVGA()->registers->specialCGAflags&0x81)==1)) //Pure CGA mode?
		{
			result |= 2; //First bit set: 80x25 CGA!
		}
		else if (((getActiveVGA()->registers->specialMDAflags&0x81)==1)) //Pure MDA mode?
		{
			result |= 3; //Both bits set: 80x25 MDA!
		}
		else //VGA?
		{
			//Leave PPI62 at zero for VGA: we're in need of auto detection!
		}
		result |= 4; //Two floppy drives installed!
	}
	else //Read low switches?
	{
		//Bit0=Loop on POST
		//Bit1=Co-processor installed
		//Bit2=Planar RAM size 0
		//Bit3=Planar RAM size 1
		result |= 0xD; //Don't loop on POST, no coprocessor, maximum RAM!
	}
	result |= (PPI62&0xC0); //Ram&IO channel check results!
	return result; //Give the switches requested, if any!
}

byte PPI_readIO(word port, byte *result)
{
	switch (port) //Special register: System control port B!
	{
	case 0x61: //System control port B(ISA,EISA)?
		if (is_XT) //XT?
		{
			*result = ((PPI62&0xC0)|(SystemControlPortB&0xC)); //Read the value! Bits 0,1,4,5 are by the PIT! The rest is by the System Control Port B!
		}
		else //AT+?
		{
			*result = (SystemControlPortB&0xCC); //Read the value! Bits 0,1,4,5 are by the PIT! The rest is by the System Control Port B!
		}
		return 1;
		break;
	case 0x62: //PPI62?
		if (is_XT) //Enabled?
		{
			*result = readPPI62(); //Read the value!
			return 1;
		}
		break;
	case 0x63: //PPI63?
		if (is_XT) //Enabled?
		{
			*result = PPI63; //Read the value(Command/mode register)!
			return 1;
		}
		break;
	case 0x92: //System control port A?
		*result = SystemControlPortA; //Read the value!
		return 1;
		break;
	case 0xA0: //NMI interrupt is enabled at highest bit on XT!
		if (is_XT) //Enabled?
		{
			*result = (byte)((~NMI)<<7); //NMI enabled? The flag itself is reversed!
			return 1;
		}
		break;
	default: //unknown port?
		break;
	}
	return 0; //No PPI!
}

void checkPPIA20()
{
	MMU_setA20(1,SystemControlPortA&2); //Update with our new value!
}

extern char ROMpath[256]; //ROM path!

byte PPI_writeIO(word port, byte value)
{
	FILE *f; 
	char platformfile[256];
	char codetranslation[256]; //Code translation!
	unsigned int rawcode; //Raw code read from the file!
	char platform[7] = {0,0,0,0,0,0,0}; //Platform!
	char beforetranslation[2] = {0,0}; //Before the translated code!
	switch (port)
	{
	case 0x61: //System control port B?
		if (is_XT) //IBM XT?
		{
			SystemControlPortB = (value&0x3C)|(SystemControlPortB&0xC3); //Set the port, only the middle 4 bits(highest 2 bits is the keyboard controller) are used: bit 5=I/O check enable, bit 4=RAM parity check enable, bit 3=Read low switches, bit2=Turbo Switch is ours!
			PPI62 &= ~(((((SystemControlPortB & 0x10) << 1)) | ((SystemControlPortB & 0x20) >> 1)) << 2); //Setting the enable(it's reversed in the AT BIOS) bits clears the status of it's corresponding error bit, according to the AT BIOS!
			if (value & 0x80) //Acnowledge?
			{
				acnowledgeIRQrequest(0); //Acnowledge the IRQ, ignore any more IRQs until raised again!
			}
		}
		else //Full set?
		{
			//Bit 7 resets the timer 0 output latch(acnowledges it?)?
			if (value & 0x80) //Acnowledge?
			{
				acnowledgeIRQrequest(0); //Acnowledge the IRQ, ignore any more IRQs until raised again!
			}
			SystemControlPortB = (value&0xC)|(SystemControlPortB&0xC0); //Set the port, ignore the upper two bits!
			SystemControlPortB &= ~(((((SystemControlPortB&4)<<1))|((SystemControlPortB&8)>>1))<<4); //Setting the enable(it's reversed in the AT BIOS) bits clears the status of it's corresponding error bit, according to the AT BIOS!
		}
		TurboMode = ((is_XT) && (value&4)); //Turbo mode enabled on XT?
		updateSpeedLimit(); //Update the speed used!
		return 1;
		break;
	case 0x62: //PPI62?
		if (is_XT) //Enabled?
		{
			PPI62 = (PPI62&0xC3)|(value&0x3C); //Set the bits that are changable!
			return 1;
		}
		break;
	case 0x63: //PPI63?
		if (is_XT) //Enabled?
		{
			PPI63 = value; //Set the value(Command/mode register)!
			return 1;
		}
		break;
	case 0x60: //IBM XT Diagnostics!
		if (is_XT) goto outputdiagnostics; //Output diagnostics!
		break;
	case 0x84: //Compaq Deskpro Diagnostics!
		if (is_Compaq==1) goto outputdiagnostics; //Output diagnostics!
		break;
	case 0x80: //IBM AT Diagnostics!
		if (is_XT || (is_Compaq==1)) break; //Don't handle this for XT&Compaq systems!
		outputdiagnostics: //Diagnostics port output!
		if (((sword)value!=breakpoint_comparison) && (diagnosticsportoutput_breakpoint == (sword)value)) //Have we reached a breakpoint?
		{
			if (breakpoint_timeout) //Breakpoint timing?
			{
				if (--breakpoint_timeout==0) //Timeout?
				{
					singlestep = 2; //Start single stepping after this breakpoint, the debugger isn't ready yet for this instruction!
					breakpoint_timeout = breakpoint_timeoutoriginal; //Reset the timeout variable to count again after triggering it, providing a skip again!
				}
			}
		}
		if (isDebuggingPOSTCodes() && ((sword)value!=breakpoint_comparison)) //Changed and debugging POST codes?
		{
			//Generate platform name!
			memset(&platformfile,0,sizeof(platformfile));
			platform[0] = 'A';
			platform[1] = 'T'; //Default: AT!
			platform[2] = 0;
			if (is_i430fx) //i430fx?
			{
				if (is_i430fx == 1) //i430fx?
				{
					safe_strcpy(&platform[0], sizeof(platform), "i430fx"); //i430fx!
				}
				else //i440fx?
				{
					safe_strcpy(&platform[0], sizeof(platform), "i440fx"); //i440fx!
				}
			}
			else if (is_XT) //XT?
			{
				platform[0] = 'X'; //XT instead!
			}
			else if (is_Compaq) //Compaq?
			{
				platform[0] = 'C';
				platform[1] = 'O';
				platform[2] = 'M';
				platform[3] = 'P';
				platform[4] = 'A';
				platform[5] = 'Q';
				platform[6] = 0;
			}
			snprintf(platformfile,sizeof(platformfile),"%s/POSTCODE.%s.TXT",ROMpath,platform); //Generate our filename to search for!
			memset(&codetranslation,0,sizeof(codetranslation)); //Initialize to no code!
			beforetranslation[0] = beforetranslation[1] = 0; //Init to none!
			f = fopen(platformfile,"rb"); //Open the platform file!
			if (f==NULL) goto notfoundpostcode;
			nextentry: //Check for a next entry?
			if (feof(f)) goto notfoundpostcode; //EOF: not found?
			//Try to read an entry!
			if (fscanf(f,"%2X %[^\r\n]255c\r\n",&rawcode,&codetranslation[0])==2) //Correctly read!
			{
				if (rawcode==(unsigned int)value) //Are we found?
				{
					beforetranslation[0] = ' '; //Seperator to use now!
					goto finishfile; //Finish up: the translation is loaded!
				}
				goto nextentry; //Try the next entry!
			}
			notfoundpostcode:
			//Not found or invalid entry?
			memset(&codetranslation,0,sizeof(codetranslation)); //Initialize to no code!
			finishfile: //Finished?
			if (f)
			{
				fclose(f); //Finished?
			}
			dolog("debugger", "POST Code: %02X%s%s", value,beforetranslation,codetranslation); //Log the new value!
		}

		breakpoint_comparison = (sword)value; //Save into the comparison for new changes!
		diagnosticsportoutput = value; //Save it to the diagnostics display!
		break;
	case 0x92: //System control port A?
		SystemControlPortA = (value&(~1)); //Set the port!
		checkPPIA20(); //Fast A20
		if (value&1) //Fast reset?
		{
			emu_raise_resetline(1); //Raise the RESET line!
		}
		return 1;
		break;
	case 0xA0: //NMI interrupt is enabled at highest bit on XT!
		if (is_XT) //Enabled?
		{
			NMI = !((value>>7)&1); //NMI disabled? This bit enables it, so reverse us!
			return 1;
		}
		break;
	default: //unknown port?
		break;
	}
	return 0; //No PPI!
}

byte failsafe_NMIpending;
void PPI_failsafetimer(byte line) //Triggered when the failsafe timer changes state!
{
	//Ignore the exact change in line status? Timer is timed after all!
	if (SystemControlPortB & 0x4) //Fail-safe NMI enabled?
	{
		failsafe_NMIpending = 1; //Start pending!
		if (execNMI(2)) //Raised?
		{
			failsafe_NMIpending = 0; //Not pending anymore!
		}
	}
}

void PPI_checkfailsafetimer()
{
	if (unlikely(failsafe_NMIpending)) //pending?
	{
		if (execNMI(2)) //Raised?
		{
			failsafe_NMIpending = 0; //Not pending anymore!
		}
	}
}

void initPPI(sword useDiagnosticsportoutput_breakpoint, uint_32 breakpointtimeout)
{
	SystemControlPortB = 0x3F; //Reset system control port B!
	PPI62 = 0x00; //Set the default switches!
	PPI63 = 0x00; //Set the default switches!
	diagnosticsportoutput = 0x00; //Clear diagnostics port output!
	diagnosticsportoutput_breakpoint = useDiagnosticsportoutput_breakpoint; //Breakpoint set?
	breakpoint_comparison = -1; //Default to no comparison set, so the first set will trigger a breakpoint if needed!
	breakpoint_timeout = breakpoint_timeoutoriginal = breakpointtimeout+1; //Time out after this many instructions(0=Very first instruction, 0xFFFFFFFF is 4G instructions)!
	TurboMode = 0; //Default to no turbo mode according to the switches!
	updateSpeedLimit(); //Update the speed used!
	register_PORTIN(&PPI_readIO);
	register_PORTOUT(&PPI_writeIO);
}
