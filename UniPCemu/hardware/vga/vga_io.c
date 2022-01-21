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

#include "headers/hardware/vga/vga.h" //VGA!
#include "headers/hardware/vga/vga_precalcs.h" //Precalcs!
#include "headers/hardware/ports.h" //Ports!
#include "headers/cpu/cpu.h" //NMI support!
#include "headers/hardware/pic.h" //Interrupt lowering support!
#include "headers/mmu/mmuhandler.h" //MMU mapping has been updated?

/*

Read and write ports:

Info:						Type:
3B4h: CRTC Controller Address Register		ADDRESS
3B5h: CRTC Controller Data Register		DATA

3BAh Read: Input Status #1 Register (mono)	DATA
3BAh Write: Feature Control Register		DATA

3C0h: Attribute Address/Data register		ADDRESS/DATA
3C1h: Attribute Data Read Register		DATA

3C2h Read: Input Status #0 Register		DATA
3C2h Write: Miscellaneous Output Register	DATA

3C4h: Sequencer Address Register		ADDRESS
3C5h: Sequencer Data Register			DATA

3C7h Read: DAC State Register			DATA
3C7h Write: DAC Address Read Mode Register	ADDRESS

3C8h: DAC Address Write Mode Register		ADDRESS
3C9h: DAC Data Register				DATA

3CAh Read: Feature Control Register (mono Read)	DATA

3CCh Read: Miscellaneous Output Register	DATA

3CEh: Graphics Controller Address Register	ADDRESS
3CFh: Graphics Controller Data Register		DATA

3D4h: CRTC Controller Address Register		ADDRESS
3D5h: CRTC Controller Data Register		DATA

3DAh Read: Input Status #1 Register (color)	DATA
3DAh Write: Feature Control Register (color)	DATA

*/

//Now the CPU ports!

/*

Port 3DA/3BA info:
read: Reset 3C0 flipflop to 0!

Port 3C0 info:
read:
	@flipflop 0: 0
	@flipflop 1: 0
write:
	@flipflop 0: write to address register. flipflop.
	@flipflop 1: write to data register using address. flipflop.
Port 3C1 info:
read:
	read from data register using address. DO NOT FLIPFLOP!
write:
	do nothing!

*/

//VGA extension support!
PORTIN VGA_readIOExtension = NULL;
PORTOUT VGA_writeIOExtension = NULL;
Handler VGA_initializationExtension = NULL;

//Precursor support by using NMI!
byte NMIPrecursors = 0; //Execute a NMI for our precursors?

void setVGA_NMIonPrecursors(byte enabled)
{
	NMIPrecursors = enabled?1:0; //Use precursor NMI as set with protection?
}

//CRTC

/*

Notes on VGA vs EGA differences:
CRTC index 5h bit 7: Start odd memory address (after horizontal retrace)
CRTC index 11h read: Light pen low
CRTC index 17h: bit 4: Output control. When 1, CRTC output is in high-impedance state.

Port 3CC(w): Graphics 1 position register. Bits 0-1. Usually programmed to 0.
Port 3CA(w): Graphics 2 position register. BIts 0-1. Usually programmed to 1.

Graphics index 5 bit 2: Test condition. When 1, Graphics controller output is placed in high-impedance state for testing.

Input Status register 0 bits 5-6: Feature connector code that's input.

*/

//Main index 0=VGA+, index 1=EGA
byte VGA_RegisterWriteMasks_CRTC[2][0x25] = {{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, //VGA
												{0xFF,0xFF,0xFF,0x7F,0xFF,0xFF,0xFF,0x3F,0x1F,0x1F,0x1F,0x7F,0xFF,0xFF,0xFF,0xFF,/*idx 10h*/0xFF,0x3F,0xFF,0xFF,0x1F,0xFF,0x1F,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}}; //EGA
byte VGA_RegisterWriteMasks_Attribute[2][0x15] = {{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, //VGA
													{0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x0F,0x3F,0x3F,0x0F,0x00}}; //EGA
byte VGA_RegisterWriteMasks_InputStatus0[2] = {0xFF,0x90};
byte VGA_RegisterWriteMasks_InputStatus1[2] = {0xFF,0x7F};
byte VGA_RegisterWriteMasks_FeatureControl[2] = {0x03,0x03};
byte VGA_RegisterWriteMasks_MiscOutput[2] = {0xFF,0xFF};
byte VGA_RegisterWriteMasks_Sequencer[2][8] = {{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, //VGA
												{0x03,0x0F,0x0F,0x0F,0x07,0x00,0x00,0x00}}; //EGA
byte VGA_RegisterWriteMasks_Graphics[2][9] = {{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, //VGA
												{0x0F,0x0F,0x0F,0x1F,0x07,0x3F,0x0F,0x0F,0xFF}}; //EGA
//Limits are the maximum valid index+1(the total amount of registers that are addressable by index in the index register(including holes)).
byte VGA_RegisterWriteLimits_CRTC[2] = {0x25,0x19};
byte VGA_RegisterWriteLimits_Attribute[2] = {0x15,0x14};
byte VGA_RegisterWriteLimits_Sequencer[2] = {8,5};
byte VGA_RegisterWriteLimits_Graphics[2] = {9,9};

byte VGA_RegisterWriteMask_CRTCIndex[2] = {0xFF,0x1F};
byte VGA_RegisterWriteMask_AttributeIndex[2] = {0xFF,0x3F};
byte VGA_RegisterWriteMask_SequencerIndex[2] = {0xFF,0x1F};
byte VGA_RegisterWriteMask_GraphicsIndex[2] = {0xFF,0x0F}; //A.k.a. Graphics 1 and 2 Address register

OPTINLINE byte PORT_readCRTC_3B5() //Read CRTC registers!
{
	if ((getActiveVGA()->registers->CRTControllerRegisters_Index>0xF) && (getActiveVGA()->registers->CRTControllerRegisters_Index<0x12) && (((!GETBITS(getActiveVGA()->registers->CRTControllerRegisters.REGISTERS.ENDHORIZONTALBLANKINGREGISTER,7,1)) || (getActiveVGA()->enable_SVGA == 3)))) //Reading from light pen location registers?
	{
		switch (getActiveVGA()->registers->CRTControllerRegisters_Index) //What index?
		{
		case 0x10: //Light pen high?
			return getActiveVGA()->registers->lightpen_high; //High lightpen!
		case 0x11: //Light pen low?
			return getActiveVGA()->registers->lightpen_low; //Low lightpen!
		default: //Unknown?
			break; //Run normally.
		}
	}

	if (getActiveVGA()->registers->CRTControllerRegisters_Index>=VGA_RegisterWriteLimits_CRTC[(getActiveVGA()->enable_SVGA==3)?1:0]) //Out of range?
	{
		return PORT_UNDEFINED_RESULT; //Undefined!
	}
	return getActiveVGA()->registers->CRTControllerRegisters.DATA[getActiveVGA()->registers->CRTControllerRegisters_Index]; //Give normal index!
}

extern byte is_XT; //Are we emulating an XT architecture?

OPTINLINE void PORT_write_CRTC_3B5(byte value)
{
	byte temp; //For index 7 write protected!
	byte index;
	index = getActiveVGA()->registers->CRTControllerRegisters_Index; //What index?
	if ((index<8) && (GETBITS(getActiveVGA()->registers->CRTControllerRegisters.REGISTERS.VERTICALRETRACEENDREGISTER,7,1))) //Protected?
	{
		if (index==7) //Overflow register (allow changes in the bit 4 (line compare))
		{
			value &= 0x10; //Only line compare 8 can be changed!
			temp = getActiveVGA()->registers->CRTControllerRegisters.DATA[0x07]; //Load the overflow register!
			temp &= 0xEF; //Clear our value!
			value |= temp; //Add the line compare 8 value!
		}
		else
		{
			return; //Write protected, so don't process!
		}
	}
	if ((index>0x2F) && (index<0x40)) //30-3F=Odd->Clear screen early!
	{
		if ((value&1) && (getActiveVGA()->enable_SVGA==0)) //Odd=Set flag! Only on plain VGA!
		{
			getActiveVGA()->registers->VerticalDisplayTotalReached = 1; //Force end-of-screen reached!
		}
		return; //Don't do anything on this register anymore!
	}
	if (index>0x18) //Not a VGA register OR is a READ-ONLY register (undocumented)?
	{
		return; //Write protected OR invalid register!
	}
	if (index<VGA_RegisterWriteLimits_CRTC[(getActiveVGA()->enable_SVGA==3)?1:0]) //Within range?
	{
		value &= VGA_RegisterWriteMasks_CRTC[(getActiveVGA()->enable_SVGA==3)?1:0][index]; //Apply the write mask to the data written to the register!
		//Normal register update?
		//EVRA only takes effect on reads, not on writes!
		getActiveVGA()->registers->CRTControllerRegisters.DATA[index] = value; //Set!
		if (index==0x11) //Bit 4&5 of the Vertical Retrace End register have other effects!
		{
			//Bit 5: Input status Register 0, bit 7 needs to be updated with Bit 5?
			if (!GETBITS(getActiveVGA()->registers->CRTControllerRegisters.REGISTERS.VERTICALRETRACEENDREGISTER,4,1)) //Vertical interrupt cleared?
			{
				if (getActiveVGA()->registers->verticalinterruptflipflop) //Set the flipflop to raise the IRQ?
				{
					getActiveVGA()->registers->verticalinterruptflipflop = 0; //We're handling the flipflop lowering now!
					lowerirq(is_XT ? VGA_IRQ_XT : VGA_IRQ_AT); //Lower our IRQ if present!
					acnowledgeIRQrequest(is_XT ? VGA_IRQ_XT : VGA_IRQ_AT); //Acnowledge us!
				}
			}
		}
		if ((!GETBITS(getActiveVGA()->registers->CRTControllerRegisters.REGISTERS.ENDHORIZONTALBLANKINGREGISTER,7,1)) && (getActiveVGA()->enable_SVGA!=3)) //Force to 1?
		{
			SETBITS(getActiveVGA()->registers->CRTControllerRegisters.REGISTERS.ENDHORIZONTALBLANKINGREGISTER,7,1,1); //Force to 1!
			if (index!=3) //We've been updated too?
			{
				VGA_calcprecalcs(getActiveVGA(),WHEREUPDATED_CRTCONTROLLER|3); //We have been updated!
			}
		}
		VGA_calcprecalcs(getActiveVGA(),WHEREUPDATED_CRTCONTROLLER|index); //We have been updated!
	}
}

//ATTRIBUTE CONTROLLER

OPTINLINE void PORT_write_ATTR_3C0(byte value) //Attribute controller registers!
{
	if (!VGA_3C0_FLIPFLOPR) //Index mode?
	{
		value &= VGA_RegisterWriteMask_AttributeIndex[(getActiveVGA()->enable_SVGA==3)?1:0]; //Apply the write mask to the data written to the register!
		//Mirror to state register!
		VGA_3C0_PALW((value&0x20)>>5); //Palette Address Source!
		VGA_3C0_INDEXW(value&0x1F); //Which index?
		VGA_calcprecalcs(getActiveVGA(),WHEREUPDATED_INDEX|INDEX_ATTRIBUTECONTROLLER); //Updated index!
	}
	else //Data mode?
	{
		if ((VGA_3C0_INDEXR >= 0x10) || (VGA_3C0_PALR == 0)) //Palette writable or not palette?
		{
			if (VGA_3C0_INDEXR < VGA_RegisterWriteLimits_Attribute[(getActiveVGA()->enable_SVGA == 3) ? 1 : 0]) //Within range?
			{
				value &= VGA_RegisterWriteMasks_Attribute[(getActiveVGA()->enable_SVGA == 3) ? 1 : 0][VGA_3C0_INDEXR]; //Apply the write mask to the data written to the register!		
				getActiveVGA()->registers->AttributeControllerRegisters.DATA[VGA_3C0_INDEXR] = value; //Set!
				VGA_calcprecalcs(getActiveVGA(), WHEREUPDATED_ATTRIBUTECONTROLLER | VGA_3C0_INDEXR); //We have been updated!
			}
		}
	}

	VGA_3C0_FLIPFLOPW(!VGA_3C0_FLIPFLOPR); //Flipflop!
	VGA_calcprecalcs(getActiveVGA(), WHEREUPDATED_CRTCONTROLLER | VGA_CRTC_ATTRIBUTECONTROLLERTOGGLEREGISTER); //Our actual location!
}

//MISC

void PORT_write_MISC_3C2(byte value) //Misc Output register!
{
	value &= VGA_RegisterWriteMasks_MiscOutput[(getActiveVGA()->enable_SVGA==3)?1:0]; //Apply the write mask to the data written to the register!
	getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER = value; //Set!
	VGA_calcprecalcs(getActiveVGA(),WHEREUPDATED_MISCOUTPUTREGISTER); //We have been updated!
}

//DAC

OPTINLINE byte PORT_read_DAC_3C9() //DAC Data register!
{
	if (GETBITS(getActiveVGA()->registers->ColorRegisters.DAC_STATE_REGISTER, 0, 3) != 3) //Not ready for reads?
	{
		return (0x3F|getActiveVGA()->precalcs.emulatedDACextrabits); //According to Dosbox it gives this value when not ready yet!
	}
	word index = getActiveVGA()->registers->ColorRegisters.DAC_ADDRESS_READ_MODE_REGISTER; //Load current DAC index!
	index <<= 2; //Multiply for the index!
	index |= getActiveVGA()->registers->current_3C9; //Current index!
	byte result; //The result!
	result = getActiveVGA()->registers->DAC[index]; //Read the result!

	if (++getActiveVGA()->registers->current_3C9>2) //Next entry?
	{
		++getActiveVGA()->registers->ColorRegisters.DAC_ADDRESS_READ_MODE_REGISTER; //Next entry!
		getActiveVGA()->registers->ColorRegisters.DAC_ADDRESS_READ_MODE_REGISTER &= 0xFF; //Reset when needed!
		//VGA_calcprecalcs(getActiveVGA(),WHEREUPDATED_INDEX|INDEX_DACREAD); //Updated index!
		getActiveVGA()->registers->current_3C9 = 0; //Reset!
	}
	return result; //Give the result!
}

OPTINLINE void PORT_write_DAC_3C9(byte value) //DAC Data register!
{
	byte entrynumber = getActiveVGA()->registers->ColorRegisters.DAC_ADDRESS_WRITE_MODE_REGISTER; //Current entry number!
	word index = entrynumber; //Load current DAC index!
	index <<= 2; //Multiply for the index!
	index |= getActiveVGA()->registers->current_3C9; //Current index!
	getActiveVGA()->registers->DAC[index] = (value&(0x3F|getActiveVGA()->precalcs.emulatedDACextrabits)); //Write the data!
	VGA_calcprecalcs(getActiveVGA(),WHEREUPDATED_DAC|entrynumber); //We've been updated!
	
	if (++getActiveVGA()->registers->current_3C9>2) //Next entry?
	{
		++entrynumber; //Overflow when needed!
		getActiveVGA()->registers->ColorRegisters.DAC_ADDRESS_WRITE_MODE_REGISTER = (byte)entrynumber; //Update the entry number!
		//VGA_calcprecalcs(getActiveVGA(),WHEREUPDATED_INDEX|INDEX_DACWRITE); //Updated index!
		getActiveVGA()->registers->current_3C9 = 0; //Reset!
	}
}

/*

Finally: the read/write handlers themselves!

*/

byte PORT_readVGA(word port, byte *result) //Read from a port/register!
{
	byte switchval;
	byte ok = 0;
	if (!getActiveVGA()) //No active VGA?
	{
		return 0;
	}
	if (VGA_readIOExtension) //Extension installed?
	{
		if ((ok = VGA_readIOExtension(port,result))) goto finishinput; //Finish us! Don't use the VGA registers!
	}
	if (((getActiveVGA()->registers->VGA_enabled) == 0) && (port != 0x3C3)) return 0; //Disabled I/O?
	switch (port) //What port?
	{
	case 0x3B0:
	case 0x3B2:
	case 0x3B6:
	case 0x3B4: //Decodes to 3B4!
		if (GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER,0,1)) goto finishinput; //Block: we're a color mode addressing as mono!
		goto readcrtaddress;
	case 0x3D4: //CRTC Controller Address Register		ADDRESS
		if (!GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER,0,1)) goto finishinput; //Block: we're a mono mode addressing as color!
		readcrtaddress:
		if (getActiveVGA()->enable_SVGA!=3) //Not EGA?
		{
			*result = getActiveVGA()->registers->CRTControllerRegisters_IndexRegister; //Give!
			ok = 1;
		}
		break;
	case 0x3B1:
	case 0x3B3:
	case 0x3B7: //Decodes to 3B5!
	case 0x3B5: //CRTC Controller Data Register		5DATA
		if (GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER,0,1)) goto finishinput; //Block: we're a color mode addressing as mono!
		goto readcrtvalue;
	case 0x3D5: //CRTC Controller Data Register		DATA
		if (!GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER,0,1)) goto finishinput; //Block: we're a mono mode addressing as color!
		readcrtvalue:
		if ((getActiveVGA()->enable_SVGA!=3) || (((getActiveVGA()->registers->CRTControllerRegisters_Index>=0x0C) || (getActiveVGA()->registers->CRTControllerRegisters_Index<=0x11)) && (getActiveVGA()->enable_SVGA==3))) //Not EGA or EGA light pen registers (or we're reading the readable cursor/start address)?
		{
			*result = PORT_readCRTC_3B5(); //Read port 3B5!
			ok = 1;
		}
		break;
	case 0x3C0: //Attribute Address/Data register		ADDRESS/DATA
		//Do nothing: write only port! Undefined!
		if (getActiveVGA()->enable_SVGA!=3) //Not EGA?
		{
			*result = (VGA_3C0_PALR<<5)|VGA_3C0_INDEXR; //Give the saved information!
			ok = 1;
		}
		break;
	case 0x3C1: //Attribute Data Read Register		DATA
		if (VGA_3C0_INDEXR>=VGA_RegisterWriteLimits_Attribute[(getActiveVGA()->enable_SVGA==3)?1:0]) break; //Out of range!
		if (getActiveVGA()->enable_SVGA!=3) //Not EGA?
		{
			*result = getActiveVGA()->registers->AttributeControllerRegisters.DATA[VGA_3C0_INDEXR]; //Read from current index!
			ok = 1;
		}
		break;
	case 0x3C2: //Read: Input Status #0 Register		DATA
		//Switch sense: 0=Switch closed(value of the switch being 1)
		switchval = ((getActiveVGA()->registers->switches)>>GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER,2,3)); //Switch value to set!
		switchval = ~switchval; //Reverse the switch for EGA+!
		SETBITS(getActiveVGA()->registers->ExternalRegisters.INPUTSTATUS0REGISTER,4,1,(switchval&1)); //Depends on the switches. This is the reverse of the actual switches used! Originally stuck to 1s, but reported as 0110!
		*result = getActiveVGA()->registers->ExternalRegisters.INPUTSTATUS0REGISTER; //Give the register!
		SETBITS(*result, 5, 3, (getActiveVGA()->registers->ExternalRegisters.FEATURECONTROLREGISTER & 3)); //Feature bits 0&1!
		SETBITS(*result, 7, 1, (getActiveVGA()->registers->verticalinterruptflipflop^((getActiveVGA()->enable_SVGA==3)?1:0))); //Vertical retrace interrupt pending? Inverted on the EGA!
		*result &= VGA_RegisterWriteMasks_InputStatus0[(getActiveVGA()->enable_SVGA==3)?1:0]; //Apply the write mask to the data written to the register!
		ok = 1;
		break;
	case 0x3C3: //Video subsystem enable
		if (getActiveVGA()->enable_SVGA < 3) //VGA+?
		{
			*result = GETBITS(getActiveVGA()->registers->VGA_enabled, 1, 1); //Get from the register!
			ok = 1;
		}
		break;
	case 0x3C4: //Sequencer Address Register		ADDRESS
		if (getActiveVGA()->enable_SVGA!=3) //Not EGA?
		{
			*result = getActiveVGA()->registers->SequencerRegisters_IndexRegister; //Give the index!
			ok = 1;
		}
		break;
	case 0x3C5: //Sequencer Data Register			DATA
		if (getActiveVGA()->enable_SVGA!=3) //Not EGA?
		{
			if (getActiveVGA()->registers->SequencerRegisters_Index>=VGA_RegisterWriteLimits_Sequencer[(getActiveVGA()->enable_SVGA==3)?1:0]) break; //Out of range!
			*result = getActiveVGA()->registers->SequencerRegisters.DATA[getActiveVGA()->registers->SequencerRegisters_Index]; //Give the data!
			ok = 1;
		}
		break;
	case 0x3C6: //DAC Mask Register?
		if (getActiveVGA()->enable_SVGA!=3) //Not EGA?
		{
			*result = getActiveVGA()->registers->DACMaskRegister; //Give!
			ok = 1;
		}
		break;
	case 0x3C7: //Read: DAC State Register			DATA
		if (getActiveVGA()->enable_SVGA!=3) //Not EGA?
		{
			*result = getActiveVGA()->registers->ColorRegisters.DAC_STATE_REGISTER; //Give!
			ok = 1;
		}
		break;
	case 0x3C8: //DAC Address Write Mode Register		ADDRESS
		if (getActiveVGA()->enable_SVGA!=3) //Not EGA?
		{
			*result = getActiveVGA()->registers->ColorRegisters.DAC_ADDRESS_WRITE_MODE_REGISTER; //Give!
			ok = 1;
		}
		else //EGA?
		{
			*result = 2; //Unknown what this is, but it's returning this in other emulators(86box)!
			ok = 1;
		}
		break;
	case 0x3C9: //DAC Data Register				DATA
		if (getActiveVGA()->enable_SVGA!=3) //Not EGA?
		{
			*result = PORT_read_DAC_3C9(); //Read port 3C9!
			ok = 1;
		}
		break;
	case 0x3CA: //Read: Feature Control Register		DATA
		if (getActiveVGA()->enable_SVGA!=3) //Not EGA?
		{
			*result = getActiveVGA()->registers->ExternalRegisters.FEATURECONTROLREGISTER; //Give!
			ok = 1;
		}
		break;
	case 0x3CC: //Read: Miscellaneous Output Register	DATA
		if (getActiveVGA()->enable_SVGA!=3) //Not EGA?
		{
			*result = getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER; //Give!
			ok = 1;
		}
		break;
	case 0x3CE: //Graphics Controller Address Register	ADDRESS
		if (getActiveVGA()->enable_SVGA!=3) //Not EGA?
		{
			*result = getActiveVGA()->registers->GraphicsRegisters_IndexRegister; //Give!
			ok = 1;
		}
		break;
	case 0x3CF: //Graphics Controller Data Register		DATA
		if (getActiveVGA()->enable_SVGA!=3) //Not EGA?
		{
			if (getActiveVGA()->registers->GraphicsRegisters_Index>=VGA_RegisterWriteLimits_Graphics[(getActiveVGA()->enable_SVGA==3)?1:0]) break; //Out of range!
			*result = getActiveVGA()->registers->GraphicsRegisters.DATA[getActiveVGA()->registers->GraphicsRegisters_Index]; //Give!
			ok = 1;
		}
		break;
	case 0x3BA:	//Read: Input Status #1 Register (mono)	DATA
		if (GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER,0,1)) goto finishinput; //Block: we're a color mode addressing as mono!
		goto readInputStatus1;
	case 0x3DA: //Input Status #1 Register (color)	DATA
		if (GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER,0,1)) //Block: we're a mono mode addressing as color!
		{
			readInputStatus1:
			SETBITS(getActiveVGA()->registers->CRTControllerRegisters.REGISTERS.ATTRIBUTECONTROLLERTOGGLEREGISTER,7,1,0); //Reset flipflop for 3C0!

			*result = getActiveVGA()->registers->ExternalRegisters.INPUTSTATUS1REGISTER; //Give!
			//VGA according to the document Second Sight VGA registers
			const static byte bittablelow[2][4] = {{0,4,1,6},{0,1,4,8}}; //Bit 6 is undefined on EGA!
			const static byte bittablehigh[2][4] = {{2,5,3,7},{2,3,5,8}}; //Bit 7 is undefined on EGA!
			byte DACOutput = getActiveVGA()->CRTC.DACOutput; //Current DAC output to give!
			SETBITS(*result,4,1,GETBITS(DACOutput,bittablelow[(getActiveVGA()->enable_SVGA==3)?1:0][GETBITS(getActiveVGA()->registers->AttributeControllerRegisters.REGISTERS.COLORPLANEENABLEREGISTER,4,3)],1));
			SETBITS(*result,5,1,GETBITS(DACOutput,bittablehigh[(getActiveVGA()->enable_SVGA==3)?1:0][GETBITS(getActiveVGA()->registers->AttributeControllerRegisters.REGISTERS.COLORPLANEENABLEREGISTER,4,3)],1));
			if (getActiveVGA()->enable_SVGA==3) //EGA has lightpen support here and special functionality?
			{
				SETBITS(*result,1,1,GETBITS(getActiveVGA()->registers->EGA_lightpenstrobeswitch,1,1)); //Light pen has been triggered and stopped pending? Set light pen trigger!
				SETBITS(*result,2,1,GETBITS(~getActiveVGA()->registers->EGA_lightpenstrobeswitch,2,1)); //Light pen switch is open(not pressed)?
			}
			ok = 1;
		}
		break;
	
	//Precursors compatibility
	case 0x3D8: //Precursor port
	case 0x3D9: //Precursor port
	case 0x3B8: //Precursor port
		if (NMIPrecursors) ok = !execNMI(0); //Execute an NMI from Bus!
		break;
	default: //Unknown?
		break; //Not used address!
	}
	finishinput:
	if (ok==2) ok = 0; //Special extension state: we're undefined!
	return ok; //Disabled for now or unknown port!
}

byte PORT_writeVGA(word port, byte value) //Write to a port/register!
{
	if (!getActiveVGA()) //No active VGA?
	{
		return 0;
	}
	byte ok = 0;
	if (VGA_writeIOExtension) //Extension installed?
	{
		if ((ok = VGA_writeIOExtension(port,value))) goto finishoutput; //Finish us! Don't use the VGA registers!
	}
	if (((getActiveVGA()->registers->VGA_enabled) == 0) && (port != 0x3C3)) return 0; //Disabled I/O?
	switch (port) //What port?
	{
	case 0x3B0:
	case 0x3B2:
	case 0x3B6: //Decodes to 3B4!
	case 0x3B4: //CRTC Controller Address Register		ADDRESS
		if (GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER,0,1)) goto finishoutput; //Block: we're a color mode addressing as mono!
		goto accesscrtaddress;
	case 0x3D4: //CRTC Controller Address Register		ADDRESS
		if (!GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER,0,1)) goto finishoutput; //Block: we're a mono mode addressing as color!
		accesscrtaddress:
		value &= VGA_RegisterWriteMask_CRTCIndex[(getActiveVGA()->enable_SVGA==3)?1:0]; //Apply the write mask to the data written to the register!
		getActiveVGA()->registers->CRTControllerRegisters_IndexRegister = value; //Set!
		getActiveVGA()->registers->CRTControllerRegisters_Index = (value&0x3F); //Set!
		ok = 1;
		break;
	case 0x3B1:
	case 0x3B3:
	case 0x3B7: //Decodes to 3B5!
	case 0x3B5: //CRTC Controller Data Register		DATA
		if (GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER,0,1)) goto finishoutput; //Block: we're a color mode addressing as mono!
		goto accesscrtvalue;
	case 0x3D5: //CRTC Controller Data Register		DATA
		if (!GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER,0,1)) goto finishoutput; //Block: we're a mono mode addressing as color!
		accesscrtvalue:
		PORT_write_CRTC_3B5(value); //Write CRTC!
		ok = 1;
		break;
	case 0x3BA: //Write: Feature Control Register (mono)		DATA
		if (GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER,0,1)) goto finishoutput; //Block: we're a color mode addressing as mono!
	case 0x3CA: //Same as above!
		if ((getActiveVGA()->enable_SVGA == 3) && (port==0x3CA)) //EGA? Graphics 2 position!
		{
			getActiveVGA()->registers->EGA_graphics2position = value; //Graphics 2 position!
			ok = 1;
			goto finishoutput;
		}
		else //Non-EGA?
		{
			goto accessfc; //Always access at this address!
		}
	case 0x3DA: //Same!
		if (!GETBITS(getActiveVGA()->registers->ExternalRegisters.MISCOUTPUTREGISTER,0,1)) goto finishoutput; //Block: we're a mono mode addressing as color!
		accessfc: //Allow!
		value &= VGA_RegisterWriteMasks_FeatureControl[(getActiveVGA()->enable_SVGA==3)?1:0]; //Apply the write mask to the data written to the register!
		getActiveVGA()->registers->ExternalRegisters.FEATURECONTROLREGISTER = value; //Set our used bits only!
		VGA_calcprecalcs(getActiveVGA(),WHEREUPDATED_FEATURECONTROLREGISTER); //We have been updated!
		ok = 1;
		break;
	case 0x3C0: //Attribute Address/Data register		ADDRESS/DATA
		PORT_write_ATTR_3C0(value); //Write to 3C0!
		ok = 1;
		break;
	case 0x3C1: //Attribute Data Read Register		DATA
		//Undefined on most chipsets!
		if (getActiveVGA()->enable_SVGA == 3) //EGA?
		{
			if (VGA_3C0_FLIPFLOPR) //Flipflop set? Write to the register and toggle the flipflop! Undocumented EGA BIOS behaviour!
			{
				PORT_write_ATTR_3C0(value); 
				ok = 1;
			}
		}
		goto finishoutput; //Unknown port! Ignore our call!
		break;
	case 0x3CC: //Same as above!
		if (getActiveVGA()->enable_SVGA == 3) //EGA? Graphics 1 position!
		{
			getActiveVGA()->registers->EGA_graphics1position = value; //Graphics 1 position!
			ok = 1;
			goto finishoutput; //Finish output!
		}
	case 0x3C2: //Write: Miscellaneous Output Register	DATA
		PORT_write_MISC_3C2(value); //Write to 3C2!
		ok = 1;
		break;
	case 0x3C3: //Video subsystem enable
		if (getActiveVGA()->enable_SVGA < 3) //VGA+?
		{
			SETBITS(getActiveVGA()->registers->VGA_enabled, 1, 1, (value & 1)); //RAM and I/O enabled?
			MMU_mappingupdated(); //A memory mapping has been updated?
			VGA_calcprecalcs(getActiveVGA(),WHEREUPDATED_MISCOUTPUTREGISTER); //Updated index!
			ok = 1;
		}
		break;
	case 0x3C4: //Sequencer Address Register		ADDRESS
		value &= VGA_RegisterWriteMask_SequencerIndex[(getActiveVGA()->enable_SVGA==3)?1:0]; //Apply the write mask to the data written to the register!
		getActiveVGA()->registers->SequencerRegisters_IndexRegister = value; //Set!
		getActiveVGA()->registers->SequencerRegisters_Index = (value&7); //Set!
		ok = 1;
		break;
	case 0x3C5: //Sequencer Data Register			DATA
		if (getActiveVGA()->registers->SequencerRegisters_Index>=VGA_RegisterWriteLimits_Sequencer[(getActiveVGA()->enable_SVGA==3)?1:0]) break; //Invalid data!
		if (getActiveVGA()->registers->SequencerRegisters_Index==7) //Disable display till write to sequencer registers 0-6?
		{
			getActiveVGA()->registers->CRTControllerDontRender = 1; //Force to 1 indicating display disabled!
		}
		else if ((getActiveVGA()->registers->SequencerRegisters_Index<7) && (getActiveVGA()->registers->CRTControllerDontRender)) //Disabled and enabled again?
		{
			getActiveVGA()->registers->CRTControllerDontRender = 0x00; //Reset, effectively enabling VGA rendering!
		}
		if (getActiveVGA()->registers->SequencerRegisters_Index>=VGA_RegisterWriteLimits_Sequencer[(getActiveVGA()->enable_SVGA==3)?1:0])
		{
			break; //Out of range!
		}
		if (getActiveVGA()->registers->SequencerRegisters_Index<MIN(sizeof(VGA_RegisterWriteMasks_Sequencer[0]),sizeof(getActiveVGA()->registers->SequencerRegisters.DATA))) //Within range?
		{
			value &= VGA_RegisterWriteMasks_Sequencer[(getActiveVGA()->enable_SVGA==3)?1:0][getActiveVGA()->registers->SequencerRegisters_Index]; //Apply the write mask to the data written to the register!
			getActiveVGA()->registers->SequencerRegisters.DATA[getActiveVGA()->registers->SequencerRegisters_Index] = value; //Set!
		}
		VGA_calcprecalcs(getActiveVGA(),WHEREUPDATED_SEQUENCER|getActiveVGA()->registers->SequencerRegisters_Index); //We have been updated!		
		ok = 1;
		break;
	case 0x3C6: //DAC Mask Register?
		if (getActiveVGA()->enable_SVGA!=3) //Not EGA?
		{
			getActiveVGA()->registers->DACMaskRegister = value; //Set!
			VGA_calcprecalcs(getActiveVGA(),WHEREUPDATED_DACMASKREGISTER); //We have been updated!				
			ok = 1;
		}
		break;
	case 0x3C7: //Write: DAC Address Read Mode Register	ADDRESS
		if (getActiveVGA()->enable_SVGA!=3) //Not EGA?
		{
			getActiveVGA()->registers->ColorRegisters.DAC_ADDRESS_READ_MODE_REGISTER = value; //Set!
			SETBITS(getActiveVGA()->registers->ColorRegisters.DAC_STATE_REGISTER,0,3,3); //Prepared for reads!
			getActiveVGA()->registers->current_3C9 = 0; //Reset!
			ok = 1;
		}
		break;
	case 0x3C8: //DAC Address Write Mode Register		ADDRESS
		if (getActiveVGA()->enable_SVGA!=3) //Not EGA?
		{
			getActiveVGA()->registers->ColorRegisters.DAC_ADDRESS_WRITE_MODE_REGISTER = value; //Set index!
			SETBITS(getActiveVGA()->registers->ColorRegisters.DAC_STATE_REGISTER,0,3,0); //Prepared for writes!
			getActiveVGA()->registers->current_3C9 = 0; //Reset!
			ok = 1;
		}
		break;
	case 0x3C9: //DAC Data Register				DATA
		if (getActiveVGA()->enable_SVGA!=3) //Not EGA?
		{
			PORT_write_DAC_3C9(value); //Write to 3C9!
			ok = 1;
		}
		break;
	case 0x3CE: //Graphics Controller Address Register	ADDRESS
		value &= VGA_RegisterWriteMask_GraphicsIndex[(getActiveVGA()->enable_SVGA==3)?1:0]; //Apply the write mask to the data written to the register!
		getActiveVGA()->registers->GraphicsRegisters_IndexRegister = value; //Set index!
		getActiveVGA()->registers->GraphicsRegisters_Index = (value&0xF); //Set index!
		ok = 1;
		break;
	case 0x3CF: //Graphics Controller Data Register		DATA
		if (getActiveVGA()->registers->GraphicsRegisters_Index>=VGA_RegisterWriteLimits_Graphics[(getActiveVGA()->enable_SVGA==3)?1:0]) break; //Invalid index!
		value &= VGA_RegisterWriteMasks_Graphics[(getActiveVGA()->enable_SVGA==3)?1:0][getActiveVGA()->registers->GraphicsRegisters_Index]; //Apply the write mask to the data written to the register!
		getActiveVGA()->registers->GraphicsRegisters.DATA[getActiveVGA()->registers->GraphicsRegisters_Index] = value; //Set!
		VGA_calcprecalcs(getActiveVGA(),WHEREUPDATED_GRAPHICSCONTROLLER|getActiveVGA()->registers->GraphicsRegisters_Index); //We have been updated!				
		ok = 1;
		break;
	
	//Precursors compatibility
	//EGA compatibility!
	case 0x3DB:
		if (getActiveVGA()->enable_SVGA==3) //EGA?
		{
			//Light pen latch is to be cleared!
			getActiveVGA()->registers->EGA_lightpenstrobeswitch &= ~3; //Stop strobe(pending) and latch once triggered by the light pen?
		}
		break;
	case 0x3DC:
		if (getActiveVGA()->enable_SVGA==3) //EGA?
		{
			//Light pen latch is to be triggered!
			getActiveVGA()->registers->EGA_lightpenstrobeswitch |= 1; //Start strobing even if the light pen isn't found at the current locartion?
		}
		break;

	//CGA/MDA comparitility
	case 0x3D8: //Precursor port
	case 0x3D9: //Precursor port
	case 0x3B8: //Precursor port
		if (NMIPrecursors) ok = !execNMI(0); //Execute an NMI from Bus!
		break;
	default: //Unknown?
		goto finishoutput; //Unknown port! Ignore our call!
		break; //Not used!
	}
	//Extra universal handling!

	finishoutput: //Finishing up our call?
	if (ok==2) ok = 0; //Special extension state: we're undefined!
	return ok; //Give if we're handled!
}

extern VGA_calcprecalcsextensionhandler VGA_precalcsextensionhandler; //The precalcs extension handler!
extern VGA_clockrateextensionhandler VGA_calcclockrateextensionhandler; //The clock rate extension handler!
extern VGA_addresswrapextensionhandler VGA_calcaddresswrapextensionhandler; //The DWord shift extension handler!

void VGA_registerExtension(PORTIN readhandler, PORTOUT writehandler, Handler initializationhandler, VGA_calcprecalcsextensionhandler precalcsextensionhandler, VGA_clockrateextensionhandler clockrateextension, VGA_addresswrapextensionhandler addresswrapextension) //Register an extension for use with the VGA!
{
	VGA_readIOExtension = readhandler; //Register the read handler, if used!
	VGA_writeIOExtension = writehandler; //Register the read handler, if used!
	VGA_initializationExtension = initializationhandler; //Register the initialization handler, if used!
	VGA_precalcsextensionhandler = precalcsextensionhandler; //Register the precalcs extension handler, if used!
	VGA_calcclockrateextensionhandler = clockrateextension; //Register the clock rate extension handler, if used!
	VGA_calcaddresswrapextensionhandler = addresswrapextension; //Register the DWord shift extension handler, if used!
}

void VGA_initIO()
{
	//Our own settings we use:
	register_PORTIN(&PORT_readVGA);
	register_PORTOUT(&PORT_writeVGA);
	if (VGA_initializationExtension) //Extension used?
	{
		VGA_initializationExtension(); //Initialise the extension if needed!
	}
}
