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
#include "headers/cpu/cpu.h" //For handler!
#include "headers/cpu/cb_manager.h" //Typedefs!
#include "headers/cpu/easyregs.h" //Easy register support for DOSBox!

#include "headers/interrupts/interrupt16.h" //For Dosbox compatibility.
#include "headers/interrupts/interrupt10.h" //For Video BIOS compatibility.
#include "headers/support/signedness.h" //Sign support!
#include "headers/cpu/cpu_stack.h" //For popping AX from the stack!

extern byte EMU_BIOS[0x10000]; //Full custom BIOS from 0xF0000-0xFFFFF for the emulator itself to use!
extern byte EMU_VGAROM[0x10000]; //Full VGA BIOS from 0xC0000-0xC8000 for the emulator and normal BIOS to use!

extern Int10Data int10; //Our VGA ROM data!

extern byte EMU_RUNNING;

Handler CBHandlers[CB_MAX]; //Handlers!
byte CBTypes[CB_MAX]; //Types!
struct
{
	word handlernr; //Current handle number!
	byte hascallback;
} currentcallback; //Current callback!

void CB_DataHandler() {} //Data handler dummy for callbacks with Data only!

byte CB_callback = 0; //Default: no callback!
void CB_SetCallback(byte isone)
{
	CB_callback = isone; //Set on/off!
}

byte CB_ISCallback()
{
	return CB_callback?1:0; //Callback or not!
}

void CB_handler(word handlernr) //Call an handler (from CB_Handler)?
{
	currentcallback.hascallback = 1; //Have callback!
	currentcallback.handlernr = handlernr; //The handle to process!
}

byte callbackzero = 0; //Zero callback?
byte lastcallbackzero = 0; //Last zero callback?

void CB_handleCallbacks() //Handle callbacks after CPU usage!
{
	if (currentcallback.hascallback) //Valid set?
	{
		currentcallback.hascallback = 0; //Reset to not used: we're being handled!
		REG_AX = CPU_POP16(0); //POP AX off the stack, restoring it's value for the call!
		if (currentcallback.handlernr<=1) //Special handler? (Conditional) Zero callback?
		{
			if (!currentcallback.handlernr) //Zero callback?
			{
				callbackzero = 1; //We're a zero callback!
			}
			else //Conditional zero callback? Enable when the last callback executed was a zero callback!
			{
				callbackzero = lastcallbackzero; //Only zero callback if the last was one!
			}
		}
		else
		{
			if (currentcallback.handlernr < NUMITEMS(CBHandlers)) //Do we have a handler in range to execute?
			{
				if (CBHandlers[currentcallback.handlernr]) //Gotten a handler set?
				{
					CB_SetCallback(1); //Callback!
					CBHandlers[currentcallback.handlernr](); //Run the handler!
					CB_SetCallback(0); //Not anymore!
				}
			}
			lastcallbackzero = callbackzero; //Last zero callback status!
			callbackzero = 0; //Not a zero callback anymore!
		}
	}
}

void clearCBHandlers() //Reset callbacks!
{
	int curhandler;
	for (curhandler=0; curhandler<CB_MAX; curhandler++) //Process all handlers!
	{
		CBHandlers[curhandler] = NULL; //Reset all handlers!
	}
	memset(&CBTypes,0,sizeof(CBTypes)); //Init types to unused!
	currentcallback.hascallback = 0; //Reset use of callbacks by default!
}

word CB_datasegment; //Reserved segment when adding callback!
word CB_dataoffset; //Reserved offset when adding callback!
word CB_realoffset; //Real offset we're loaded at within the custom BIOS!

#ifdef IS_BIG_ENDIAN
#define LE16(x) SDL_SwapLE16(value)
#else
#define LE16(x) (x)
#endif

void write_VGAw(uint_32 offset, word value)
{
	value = LE16(value); //Make sure we're little-endian!
	EMU_VGAROM[offset&0xFFFF] = value & 0xFF; //Low byte!
	EMU_VGAROM[(offset + 1)&0xFFFF] = (value >> 8); //High byte!
}

void write_BIOSw(uint_32 offset, word value)
{
	value = LE16(value); //Make sure we're little-endian!
	EMU_BIOS[offset&0xFFFF] = value&0xFF; //Low byte!
	EMU_BIOS[(offset+1)&0xFFFF] = (value>>8); //High byte!
}

#define Bit8u byte
#define Bit16u word

#define incoffset (dataoffset++&0xFFFF)
#define incoffsetv (incoffset&0xFFFF)

//In the case of CB_INTERRUPT, how much to add to the address to get the CALL version
#define CB_INTERRUPT_CALLSIZE 12

void CB_createlongjmp(word entrypoint, word segment, word offset) //Create an alias for BIOS compatibility!
{
	EMU_BIOS[entrypoint++] = 0x9A; //CALL!
	offset += CB_INTERRUPT_CALLSIZE; //We're a CALL!
	EMU_BIOS[entrypoint++] = (offset & 0xFF); //Low!
	EMU_BIOS[entrypoint++] = ((offset >> 8) & 0xFF); //High!
	EMU_BIOS[entrypoint++] = (segment & 0xFF); //Low!
	EMU_BIOS[entrypoint++] = ((segment>>8) & 0xFF); //High!
	EMU_BIOS[entrypoint++] = 0xCB; //RETF
}

void CB_updatevectoroffsets(uint_32 intnr, word offset)
{
	word entrypoint = 0xfef3 + (intnr<<1); //Our entry point!
	if (entrypoint < 0xff52) //Within range of the IVT?
	{
		EMU_BIOS[entrypoint++] = (offset & 0xFF); //Low bits!
		offset >>= 8; //Shift to low side!
		EMU_BIOS[entrypoint] = (offset & 0xFF); //High bits!
	}
}

//The size of a callback instruction!
#define CALLBACKSIZE 6

void CB_createcallback(byte isVGA, word callback, word *offset) //Create VGA/BIOS callback!
{
	word addrmask = isVGA?0xFFFF:0xFFFF; //The mask to use!
	byte *datapoint;
	datapoint = isVGA?&EMU_VGAROM[0]:&EMU_BIOS[0]; //What ROM to use?
	datapoint[(*offset)++&addrmask] = 0x50; //PUSH AX on the stack for the handler!
	datapoint[(*offset)++&addrmask] = 0xB8; //MOV AX,callback
	datapoint[(*offset)++&addrmask] = (callback&0xFF); //Callback low!
	datapoint[(*offset)++&addrmask] = ((callback>>8)&0xFF); //Callback high!
	datapoint[(*offset)++&addrmask] = 0xE7; //OUT callbackport,AX
	datapoint[(*offset)++&addrmask] = IO_CALLBACKPORT; //Our callback port!
}

void addCBHandler(byte type, Handler CBhandler, uint_32 intnr) //Add a callback!
{
	if ((CBhandler==NULL || !CBhandler) && (type==CB_INTERRUPT)) return; //Don't accept NULL INTERRUPT!
	word offset;

	word curhandler;
	int found;
	found = 0;
	for (curhandler=2; curhandler<CB_MAX; curhandler++) //Check for new handler! #0&#1 is reserved for a special operation!
	{
		if (!CBTypes[curhandler]) //Unset?
		{
			found = 1; //Valid!
			break;
		}
	}

	if (!found) //Not empty found?
	{
		return; //Don't add: no handlers left!
	}

	offset = CB_SOFFSET+(curhandler*CB_SIZE); //Start of callback!
	
	CB_realoffset = offset-CB_SOFFSET+CB_BASE; //Real offset within the custom BIOS!

	if (type!=CB_DATA) //Procedure used?
	{
		CBHandlers[curhandler] = CBhandler; //Set our handler!
	}
	else //Data?
	{
		CBHandlers[curhandler] = &CB_DataHandler; //We're data only!
	}
//Now the handler and type!
	CBTypes[curhandler] = type; //Set the type we're using!

	switch (type) //Extra handlers?
	{
		case CB_VIDEOINTERRUPT: //Video interrupt, unassigned!
			CB_datasegment = 0xC000;
			CB_dataoffset = int10.rom.used; //Entry point the end of the VGA BIOS!
			CB_realoffset = CB_dataoffset; //Offset within the custom bios is this!
			break;
		case CB_VIDEOENTRY: //Video BIOS entry interrupt, unassigned!
			CB_datasegment = 0xC000;
			CB_dataoffset = 0x0003; //Entry point in the VGA BIOS for the hooking of our interrupts!
			CB_realoffset = CB_dataoffset; //Offset within the custom bios is this!
			break;
		//Dosbox stuff!
		case CB_DOSBOX_IRQ0:
		case CB_DOSBOX_IRQ1:
		case CB_DOSBOX_IRQ9:
		case CB_DOSBOX_IRQ12:
		case CB_DOSBOX_IRQ12_RET:
			CB_datasegment = (intnr>>16);
			CB_dataoffset = (intnr&0xFFFF);
			CB_realoffset = CB_dataoffset; //Offset within the custom bios is this!
			break;
		case CB_IRET: //IRET?
			CB_datasegment = CB_SEG;
			CB_dataoffset = 0xff53;
			CB_realoffset = CB_dataoffset; //We're forced to F000:FF53!
			break;
		default: //Original offset method?
			//Real offset is already set
			CB_datasegment = CB_SEG; //Segment of data allocated!
			CB_dataoffset = offset; //Start of data/code!
			break;
	}

	word dataoffset = CB_realoffset; //Load the real offset for usage by default!
	word callbackoffset; //Used in INT16 for getting a return point!

	if ((type == CB_DOSBOX_INT16) || (type == CB_DOSBOX_MOUSE)) //We need to set the interrupt vector too?
	{
		Dosbox_RealSetVec(0x16, (CB_datasegment<<16)|CB_dataoffset); //Use intnr to set the interrupt vector!
	}

	//Now process our compatibility layer for direct calls!
	switch (type) //What type?
	{
	case CB_DATA:
		break; //Data has nothing special!
	case CB_UNASSIGNEDINTERRUPT: //Unassigned interrupt?
	case CB_IRET: //Interrupt return?
	case CB_INTERRUPT: //Normal BIOS interrupt?
	case CB_VIDEOINTERRUPT: //Video (interrupt 10h) interrupt?
		switch (intnr) //What interrupt?
		{
		case 0x15:
			CB_createlongjmp(0xf859, CB_datasegment, CB_dataoffset); //Create a JMP here where software can expect it! INT 15h System Services Entry Point
			break;
		case 0x14: //INT14?
			CB_createlongjmp(0xe739, CB_datasegment, CB_dataoffset); //Create a JMP here where software can expect it! INT 14h Serial Communications Service Entry Point
			break;
		case 0x17: //INT17?
			CB_createlongjmp(0xefd2, CB_datasegment, CB_dataoffset); //Create a JMP here where software can expect it! INT 17h Printer Service Entry Point
			break;
		case 0x13: //INT13?
			CB_createlongjmp(0xe3fe, CB_datasegment, CB_dataoffset); //Create a JMP here where software can expect it! INT 13h Fixed Disk Services Entry Point
			CB_createlongjmp(0xec59, CB_datasegment, CB_dataoffset); //Create a JMP here where software can expect it! INT 13h Diskette Service Entry Point
			break;
		case 0x10: //Video services?
			CB_createlongjmp(0xf045, CB_datasegment, CB_dataoffset); //Create a JMP here where software can expect it! INT 10 Functions 0-Fh Entry Point
			CB_createlongjmp(0xf065, CB_datasegment, CB_dataoffset); //Create a JMP here where software can expect it! INT 10h Video Support Service Entry Point
			break;
		case 0x12: //memory size services?
			CB_createlongjmp(0xf841, CB_datasegment, CB_dataoffset); //Create a JMP here where software can expect it! INT 12h Memory Size Service Entry Point
			break;
		case 0x11: //Equipment list service?
			CB_createlongjmp(0xf84d, CB_datasegment, CB_dataoffset); //Create a JMP here where software can expect it! INT 11h Equipment List Service Entry Point
			break;
		case 0x05:
			CB_createlongjmp(0xff54, CB_datasegment, CB_dataoffset); //Create a JMP here where software can expect it! INT 11h Equipment List Service Entry Point
			break;
		case 0x19:
			CB_createlongjmp(0xe05b, CB_datasegment, CB_dataoffset); //Create a JMP here where software can expect it! POST Entry point
			CB_createlongjmp(0xe6f2, CB_datasegment, CB_dataoffset); //Create a JMP here where software can expect it! INT 19h Boot Load Service Entry Point
			CB_createlongjmp(0xfff0, CB_datasegment, CB_dataoffset); //Create a JMP here where software can expect it! Power-up Entry Point
			break;
		case 0x1A:
			CB_createlongjmp(0xfe6e, CB_datasegment, CB_dataoffset); //Create a JMP here where software can expect it! Power-up Entry Point			
			break;
		default: //Custom interrupt (NON original BIOS)?
			break;
		}
		CB_updatevectoroffsets(intnr, CB_dataoffset); //Update default IVT offsets!
		break;
	case CB_DOSBOX_IRQ0:
		CB_createlongjmp(0xfea5, CB_datasegment, CB_dataoffset); //Create a JMP here where software can expect it! INT 08h System Timer ISR Entry Point
		CB_updatevectoroffsets(intnr, CB_dataoffset); //Update default IVT offsets!
		break;
	case CB_DOSBOX_IRQ1:
		CB_createlongjmp(0xe987, CB_datasegment, CB_dataoffset); //Create a JMP here where software can expect it! INT 09h Keyboard Service Entry Point
		CB_updatevectoroffsets(intnr, CB_dataoffset); //Update default IVT offsets!
		break;
	case CB_DOSBOX_INT16:
		CB_createlongjmp(0xe82e, CB_datasegment, CB_dataoffset); //INT 16h Keyboard Service Entry Point
		CB_updatevectoroffsets(intnr, CB_dataoffset); //Update default IVT offsets!
		break;
	default: //Unknown special code?
		break;
	}

	switch (type)
	{
	case CB_VIDEOINTERRUPT:
	case CB_VIDEOENTRY:
		CB_createcallback(1,0,&dataoffset); //Create special callback!
		EMU_VGAROM[incoffsetv] = 0x9A; //CALL ...
		write_VGAw(incoffsetv, CB_dataoffset + CB_INTERRUPT_CALLSIZE); //... Our interrupt handler, as a function call!
		++dataoffset; //Word address!
		write_VGAw(incoffsetv, CB_datasegment); //... Our interrupt handler, as a function call!
		++dataoffset; //Word address!
		if (type == CB_VIDEOENTRY) //Video entry point call?
		{
			EMU_VGAROM[incoffsetv] = 0xCB; //RETF!
		}
		else //Normal interrupt?
		{
			EMU_VGAROM[incoffsetv] = 0xCF; //RETI: We're an interrupt handler!
		}

		//Next, our handler as a simple FAR CALL function.
		CB_createcallback(1,curhandler,&dataoffset); //Create our handler!
		EMU_VGAROM[incoffsetv] = 0xCB; //RETF!
		break;
	case CB_UNASSIGNEDINTERRUPT: //Same as below, but unassigned to an interrupt!
	case CB_INTERRUPT: //Interrupt call?
	case CB_INTERRUPT_BOOT: //Boot call?
	case CB_INTERRUPT_MISCBIOS: //Misc BIOS call?
		//First: add to jmptbl!
		if (type!=CB_UNASSIGNEDINTERRUPT) //Not unassigned?
		{
			CPU_setint(intnr,CB_datasegment,CB_dataoffset); //Set the interrupt!
		}
		CB_createcallback(0,0,&dataoffset); //Create special zero callback!

		EMU_BIOS[incoffset] = 0x9A; //CALL ...
		write_BIOSw(incoffset, CB_dataoffset+CB_INTERRUPT_CALLSIZE); //... Our interrupt handler, as a function call!
		++dataoffset; //Word address!
		write_BIOSw(incoffset, CB_datasegment); //... Our interrupt handler, as a function call!
		++dataoffset; //Word address!

		if (type == CB_INTERRUPT_MISCBIOS) //Misc BIOS interrupt?
		{
			EMU_BIOS[incoffset] = 0xCA; //RETF imm16
			write_BIOSw(incoffset, 2); //2 entries popped!
			++dataoffset; //Word address!
		}
		else //Normal interrupt?
		{
			EMU_BIOS[incoffset] = 0xCF; //RETI: We're an interrupt handler!
		}


		//Next, our handler as a simple FAR CALL function.
		CB_createcallback(0,curhandler,&dataoffset); //Create our callback!

		if (type==CB_INTERRUPT_BOOT) //Special data insertion: interrupt 18h!
		{
			EMU_BIOS[incoffset] = 0xB4;
			EMU_BIOS[incoffset] = 0x00; //MOV AH,00h: Function number
			EMU_BIOS[incoffset] = 0xCD;
			EMU_BIOS[incoffset] = 0x16; //INT 16h: Wait for key!
			EMU_BIOS[incoffset] = 0xCD;
			EMU_BIOS[incoffset] = 0x19; //INT 19h: boot!
		}

		EMU_BIOS[incoffset] = 0xCB; //RETF!

		//Finally, return!
		break;
	case CB_IRET: //Simple IRET handler (empty filler?)
		//First: add to jmptbl!
		CPU_setint(intnr,CB_datasegment,CB_dataoffset);

		EMU_BIOS[CB_realoffset] = 0xCF; //Simple IRET!
		break;
	case CB_DATA: //Data/custom code only?
		break;

	//Handlers from DOSBox!
	case CB_DOSBOX_IRQ0:	// timer int8
		CB_createcallback(0,0,&dataoffset); //Create special zero callback!

		EMU_BIOS[incoffset] = 0x9A; //CALL ...
		write_BIOSw(incoffset, CB_dataoffset + CB_INTERRUPT_CALLSIZE); //... Our interrupt handler, as a function call!
		++dataoffset; //Word address!
		write_BIOSw(incoffset, CB_datasegment); //... Our interrupt handler, as a function call!
		++dataoffset; //Word address!
		EMU_BIOS[incoffset] = 0xCF; //RETI: We're an interrupt handler!

		CB_createcallback(0,curhandler,&dataoffset); //Create our callback!
		EMU_BIOS[incoffset] =(Bit8u)0x50;		// push ax
		EMU_BIOS[incoffset] =(Bit8u)0x52;		// push dx
		EMU_BIOS[incoffset] =(Bit8u)0x1e;		// push ds
		write_BIOSw(incoffset,(Bit16u)0x1ccd);	// int 1c
		++dataoffset;
		EMU_BIOS[incoffset] =(Bit8u)0xfa;		// cli
		EMU_BIOS[incoffset] =(Bit8u)0x1f;		// pop ds
		EMU_BIOS[incoffset] =(Bit8u)0x5a;		// pop dx
		write_BIOSw(incoffset,(Bit16u)0x20b0);	// mov al, 0x20
		++dataoffset;
		write_BIOSw(incoffset,(Bit16u)0x20e6);	// out 0x20, al
		++dataoffset;
		EMU_BIOS[incoffset] =(Bit8u)0x58;		// pop ax
		EMU_BIOS[incoffset] =(Bit8u)0xcb;		//An RETF Instruction
		break;
	case CB_DOSBOX_IRQ1:	// keyboard int9
		CB_createcallback(0,0,&dataoffset); //Create special zero callback!

		EMU_BIOS[incoffset] = 0x9A; //CALL ...
		write_BIOSw(incoffset, CB_dataoffset + CB_INTERRUPT_CALLSIZE); //... Our interrupt handler, as a function call!
		++dataoffset; //Word address!
		write_BIOSw(incoffset, CB_datasegment); //... Our interrupt handler, as a function call!
		++dataoffset; //Word address!
		EMU_BIOS[incoffset] = 0xCF; //IRET: We're an interrupt handler!

		EMU_BIOS[incoffset] = (Bit8u)0x50;			// push ax
		write_BIOSw(incoffset,(Bit16u)0x60e4);		// in al, 0x60
		++dataoffset;
		write_BIOSw(incoffset,(Bit16u)0x4fb4);		// mov ah, 0x4f
		++dataoffset;
		EMU_BIOS[incoffset] =(Bit8u)0xf9;			// stc
		write_BIOSw(incoffset,(Bit16u)0x15cd);		// int 15
		++dataoffset;
			write_BIOSw(incoffset,((Bit16u)0x73)|(CALLBACKSIZE<<8));	// jnc skip: Clearing the carry flag leaves the translation to int15h!
			++dataoffset;
			CB_createcallback(0,curhandler,&dataoffset); //Create our callback!
			// jump here to (skip):
		EMU_BIOS[incoffset] =(Bit8u)0xfa;			// cli
		write_BIOSw(incoffset,(Bit16u)0x20b0);		// mov al, 0x20
		++dataoffset;
		write_BIOSw(incoffset,(Bit16u)0x20e6);		// out 0x20, al
		++dataoffset;
		EMU_BIOS[incoffset] =(Bit8u)0x58;			// pop ax
		EMU_BIOS[incoffset] =(Bit8u)0xcb;			//An RETF Instruction
		break;
	case CB_DOSBOX_IRQ9:	// pic cascade interrupt
		CB_createcallback(0,0,&dataoffset); //Create special zero callback!

		EMU_BIOS[incoffset] = 0x9A; //CALL ...
		write_BIOSw(incoffset, CB_dataoffset + CB_INTERRUPT_CALLSIZE); //... Our interrupt handler, as a function call!
		++dataoffset; //Word address!
		write_BIOSw(incoffset, CB_datasegment); //... Our interrupt handler, as a function call!
		++dataoffset; //Word address!
		EMU_BIOS[incoffset] = 0xCF; //RETI: We're an interrupt handler!

		CB_createcallback(0,curhandler,&dataoffset); //Create our callback!
		EMU_BIOS[incoffset] =(Bit8u)0x50;		// push ax
		write_BIOSw(incoffset,(Bit16u)0x61b0);	// mov al, 0x61
		++dataoffset;
		write_BIOSw(incoffset,(Bit16u)0xa0e6);	// out 0xa0, al
		++dataoffset;
		write_BIOSw(incoffset,(Bit16u)0x0acd);	// int a
		++dataoffset;
		EMU_BIOS[incoffset] =(Bit8u)0xfa;		// cli
		EMU_BIOS[incoffset] =(Bit8u)0x58;		// pop ax
		EMU_BIOS[incoffset] =(Bit8u)0xcb;		//An RETF Instruction
		break;
	case CB_DOSBOX_IRQ12:	// ps2 mouse int74
		CB_createcallback(0,0,&dataoffset); //Create special zero callback!

		EMU_BIOS[incoffset] = 0x9A; //CALL ...
		write_BIOSw(incoffset, CB_dataoffset + CB_INTERRUPT_CALLSIZE); //... Our interrupt handler, as a function call!
		++dataoffset; //Word address!
		write_BIOSw(incoffset, CB_datasegment); //... Our interrupt handler, as a function call!
		++dataoffset; //Word address!
		EMU_BIOS[incoffset] = 0xCF; //RETI: We're an interrupt handler!

		EMU_BIOS[incoffset] =(Bit8u)0x1e;		// push ds
		EMU_BIOS[incoffset] =(Bit8u)0x06;		// push es
		write_BIOSw(incoffset,(Bit16u)0x6066);	// pushad
		++dataoffset;
		EMU_BIOS[incoffset] =(Bit8u)0xfc;		// cld
		EMU_BIOS[incoffset] =(Bit8u)0xfb;		// sti
		CB_createcallback(0,curhandler,&dataoffset); //Create our callback!
		EMU_BIOS[incoffset] = (Bit8u)0xcb;		//An RETF Instruction
		break;
	case CB_DOSBOX_IRQ12_RET:	// ps2 mouse int74 return
		CB_createcallback(0,0,&dataoffset); //Create special zero callback!

		EMU_BIOS[incoffset] = 0x9A; //CALL ...
		write_BIOSw(incoffset, CB_dataoffset + CB_INTERRUPT_CALLSIZE); //... Our interrupt handler, as a function call!
		++dataoffset; //Word address!
		write_BIOSw(incoffset, CB_datasegment); //... Our interrupt handler, as a function call!
		++dataoffset; //Word address!
		EMU_BIOS[incoffset] = 0xCF; //RETI: We're an interrupt handler!

		CB_createcallback(0,curhandler,&dataoffset); //Create our callback!
		EMU_BIOS[incoffset] =(Bit8u)0xfa;		// cli
		write_BIOSw(incoffset,(Bit16u)0x20b0);	// mov al, 0x20
		++dataoffset;
		write_BIOSw(incoffset,(Bit16u)0xa0e6);	// out 0xa0, al
		++dataoffset;
		write_BIOSw(incoffset,(Bit16u)0x20e6);	// out 0x20, al
		++dataoffset;
		write_BIOSw(incoffset,(Bit16u)0x6166);	// popad
		++dataoffset;
		EMU_BIOS[incoffset] =(Bit8u)0x07;		// pop es
		EMU_BIOS[incoffset] =(Bit8u)0x1f;		// pop ds
		EMU_BIOS[incoffset] =(Bit8u)0xcb;		//An RETF Instruction
		break;
	case CB_DOSBOX_MOUSE:
		CB_createcallback(0,0,&dataoffset); //Create special zero callback!

		EMU_BIOS[incoffset] = 0x9A; //CALL ...
		write_BIOSw(incoffset, CB_dataoffset + CB_INTERRUPT_CALLSIZE); //... Our interrupt handler, as a function call!
		++dataoffset; //Word address!
		write_BIOSw(incoffset, CB_datasegment); //... Our interrupt handler, as a function call!
		++dataoffset; //Word address!
		EMU_BIOS[incoffset] = 0xCF; //RETI: We're an interrupt handler!

		write_BIOSw(incoffset, (Bit16u)0x07eb);		// jmp i33hd
		dataoffset+=7; //9-word=9-2=7.
		// jump here to (i33hd):
		CB_createcallback(0,curhandler,&dataoffset); //Create our callback!
		EMU_BIOS[incoffset] =(Bit8u)0xCB;		//An IRET Instruction
		break;
	case CB_DOSBOX_INT16:
		CB_createcallback(0,0,&dataoffset); //Create special zero callback!

		EMU_BIOS[incoffset] = 0x9A; //CALL ...
		write_BIOSw(incoffset, CB_dataoffset + CB_INTERRUPT_CALLSIZE); //... Our interrupt handler, as a function call!
		++dataoffset; //Word address!
		write_BIOSw(incoffset, CB_datasegment); //... Our interrupt handler, as a function call!
		++dataoffset; //Word address!
		EMU_BIOS[incoffset] = 0xCF; //RETI: We're an interrupt handler!

		EMU_BIOS[incoffset] = (Bit8u)0xFB;		//STI
		callbackoffset = dataoffset; //Save the callback offset!
		byte i;
		CB_createcallback(0,curhandler,&dataoffset); //Create our callback!
		EMU_BIOS[incoffset] = (Bit8u)0xCB;		//An RETF Instruction
		for (i = 0; i <= 0x0b; i++) EMU_BIOS[incoffset] = 0x90; //12 NOP instructions!
		CB_createcallback(0,1,&dataoffset); //Create special conditional zero callback: the same as zero callback, but only if set with previous call(actual zero callback was executed last time)!
			
		EMU_BIOS[incoffset] = 0xeb;
		EMU_BIOS[dataoffset] = signed2unsigned8(-(sbyte)((dataoffset+1)-callbackoffset));	//jmp callback handler
		++dataoffset; //We're increasing it now!
		EMU_BIOS[incoffset] = (Bit8u)0xCF;		//An IRET Instruction
		break;


	default: //Default: unsupported!
		break;
	}
}

//Flag set/reset for interrupts called by callbacks. Compatible with both callback calls and normal calls.

uint_32 FLAGS_offset()
{
	return REG_SP+4+(callbackzero<<2); //Apply zero callback if needed (call adds 4 bytes to stack)!
}

void CALLBACK_SZF(byte val) {
	uint_32 flags=0;
	if (EMU_RUNNING == 1)
	{
		flags = REG_EFLAGS; //Read flags!
		REG_EFLAGS = MMU_rw(CPU_SEGMENT_SS, REG_SS, FLAGS_offset(), 0,0);
		updateCPUmode();
	}
	if (val) FLAGW_ZF(1);
	else FLAGW_ZF(0); 
	if (EMU_RUNNING==1)
	{
		MMU_ww(CPU_SEGMENT_SS, REG_SS, FLAGS_offset(), REG_EFLAGS,0);
		REG_EFLAGS = flags; //Restore!
		updateCPUmode();
	}
}

void CALLBACK_SCF(byte val) {
	uint_32 flags=0;
	if (EMU_RUNNING == 1)
	{
		flags = REG_EFLAGS; //Read flags!
		REG_EFLAGS = MMU_rw(CPU_SEGMENT_SS, REG_SS, FLAGS_offset(), 0,0);
		updateCPUmode();
	}
	if (val) FLAGW_CF(1);
	else FLAGW_CF(0); 
	if (EMU_RUNNING==1)
	{
		MMU_ww(CPU_SEGMENT_SS, REG_SS, FLAGS_offset(), REG_EFLAGS,0);
		REG_EFLAGS = flags; //Restore!
		updateCPUmode();
	}
}

void CALLBACK_SIF(byte val) {
	uint_32 flags=0;
	if (EMU_RUNNING == 1)
	{
		flags = REG_EFLAGS; //Read flags!
		REG_EFLAGS = MMU_rw(CPU_SEGMENT_SS, REG_SS, FLAGS_offset(), 0,0);
		updateCPUmode();
	}
	if (val) FLAGW_IF(1);
	else FLAGW_IF(0); 
	if (EMU_RUNNING==1)
	{
		MMU_ww(CPU_SEGMENT_SS, REG_SS, FLAGS_offset(), REG_EFLAGS,0);
		REG_EFLAGS = flags; //Restore!
		updateCPUmode();
	}
}