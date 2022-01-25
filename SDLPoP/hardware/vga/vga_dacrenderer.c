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

#define VGA_DACRENDERER

#include "headers/types.h" //Basic types!
#include "headers/hardware/vga/vga.h" //VGA support!
#include "headers/hardware/vga/vga_precalcs.h" //Precalculation typedefs etc.
#include "headers/hardware/vga/vga_dacrenderer.h" //Our defs!
#include "headers/support/bmp.h" //BMP support for dumping color information!

uint_32 DACBitmap[0x8000]; //Full DAC 1-row bitmap!
extern char capturepath[256]; //Capture path!

void VGA_DUMPColors() //Dumps the full DAC and Color translation tables!
{
	char filename[256];
	cleardata(&filename[0],sizeof(filename)); //Init
	domkdir(capturepath); //Make sure our directory exists!
	safestrcpy(filename,sizeof(filename), capturepath); //Capture path!
	safestrcat(filename,sizeof(filename), "/");
	safestrcat(filename,sizeof(filename), "VGA_DAC"); //Generate log of this mode!
	int c,r;
	INLINEREGISTER uint_32 DACVal;
	for (c=0;c<0x100;c++)
	{
		DACVal = getActiveVGA()->precalcs.DAC[c]; //The DAC value!
		if ((DACVal==(uint_32)RGB(0x00,0x00,0x00)) || (!(DACVal&0xFF000000))) //Black or unfilled?
		{
			DACBitmap[c] = 0; //Clear entry!
		}
		else
		{
			DACBitmap[c] = DACVal; //Load the DAC value!
		}
	}
	writeBMP(filename,&DACBitmap[0],16,16,4,4,16); //Simple 1-row dump of the DAC results!
	//Now, write the Attribute results through the DAC pallette!
	for (r=0;r<0x20;r++) //Show all lines available to render!
	{
		for (c=0;c<0x400;c++) //All possible attributes (font and back color)!
		{
			word lookup;
			word ordering;
			lookup = (c&0xFF); //What attribute!
			lookup <<= 5; //Make room for charinner_y and blink/font!
			lookup |= r; //Add the row to dump!
			lookup <<= 2; //Generate room for the ordering!
			ordering = 3; //Load for ordering(blinking on with foreground by default, giving foreground when possible)!
			if (c&0x200) ordering &= 1; //3nd row+? This is our unblinked(force background) value!
			if (c&0x100) ordering &= 2; //Odd row? This is our background pixel!
			lookup |= ordering; //Apply the font/back and blink status!
			//The lookup points to the index!
			DACVal = getActiveVGA()->precalcs.DAC[getActiveVGA()->precalcs.attributeprecalcs[lookup]]; //The DAC value looked up!
			if ((DACVal==(uint_32)RGB(0x00,0x00,0x00)) || (!(DACVal&0xFF000000))) //Black or unfilled?
			{
				DACBitmap[(r<<10)|c] = 0; //Clear entry!
			}
			else
			{
				DACBitmap[(r<<10)|c] = DACVal; //Load the DAC value!
			}		
		}
	}
	//Attributes are in order top to bottom: attribute foreground, attribute background, attribute foreground blink, attribute background blink affected for all attributes!
	safestrcpy(filename,sizeof(filename), capturepath); //Capture path!
	safestrcat(filename,sizeof(filename), "/");
	safestrcat(filename,sizeof(filename), "VGA_ATT"); //Generate log of this mode!
	writeBMP(filename,&DACBitmap[0],256,4*0x20,0,0,256); //Simple 4-row dump of every scanline of the attributes through the DAC!
}

byte DAC_whatBWColor = 0; //Default: none!
typedef uint_32(*BWconversion_handler)(uint_32 color); //A B/W conversion handler, if used!

//Use a weighted luminance?
#define LUMINANCE_WEIGHTED

#ifdef LUMINANCE_WEIGHTED
//Using weighted value factors below on the different channels!
#define LUMINANCE_RFACTOR 0.2126
#define LUMINANCE_GFACTOR 0.7152
#define LUMINANCE_BFACTOR 0.0722
#endif

byte RGBAconversion_ready = 0;
byte RGBA_channel[0x20000]; //What to use for red/green/blue for mode and depth(24/32-bit)!

word BWconversion_ready = 0; //Are we to initialise our tables?
byte DAC_luminancemethod = 1; //What B/W luminance method?
byte Luminance_R[0x100]; //What to add to red!
byte Luminance_G[0x100]; //What to add to green!
byte Luminance_B[0x100]; //What to add to blue!
uint_32 BWconversion_white[0x10000]; //Conversion table for b/w totals(white)!
uint_32 BWconversion_green[0x10000]; //Green channel conversion!
uint_32 BWconversion_amber[0x10000]; //Amber channel conversion!
uint_32* BWconversion_palette = &BWconversion_white[0];

byte DAC_BWColor(byte use) //What B/W color to use?
{
	if (use < 4)
	{
		DAC_whatBWColor = use; //Use?
		switch (DAC_whatBWColor) //What color scheme?
		{
		case BWMONITOR_WHITE: //Black/white?
			BWconversion_palette = &BWconversion_white[0]; //RGB Greyscale!
			break;
		case BWMONITOR_GREEN: //Green?
			BWconversion_palette = &BWconversion_green[0]; //RGB Green monitor!
			break;
		case BWMONITOR_AMBER: //Brown?
			BWconversion_palette = &BWconversion_amber[0]; //RGB Amber monitor!
			break;
		default: //Unknown scheme?
			BWconversion_palette = &BWconversion_white[0]; //RGB Greyscale!
			break;
		}
	}
	return DAC_whatBWColor;
}

void VGA_initRGBAconversion()
{
	if (RGBAconversion_ready) return; //Abort when already ready!
	RGBAconversion_ready = 1; //We're ready after this!
	const float channelfactor = ((float)1.0f / (float)255); //Red/green/blue part normalized!
	INLINEREGISTER byte a,b; //8/1(c)-bit!
	INLINEREGISTER uint_32 n; //32-bit!
	for (n=0;n<0x20000;n++) //Process all possible values!
	{
		a = n & 0xFF; //Input and output!
		b = (n>>8) & 0xFF; //Input alpha!
		if (((n>>16)&1)!=0) //Input 32-bit?
		{
			a = (byte)(((float)b)*channelfactor*((float)a)); //Output!
		}
		RGBA_channel[n] = a; //Luminance for said channel in said luminance and transparency!
	}
}

void VGA_initBWConversion()
{
	if (BWconversion_ready==(((word)DAC_luminancemethod)+1)) return; //Abort when already ready!
	BWconversion_ready = ((word)DAC_luminancemethod)+1; //We're ready after this!
	//RGB factors for all possible greyscales to apply
	const float whitefactor_R = ((float)0xFF / (float)255); //Red part!
	const float whitefactor_G = ((float)0xFF / (float)255); //Green part!
	const float whitefactor_B = ((float)0xFF / (float)255); //Blue part!
	const float greenfactor_R = ((float)0x4A / (float)255); //Red part!
	const float greenfactor_G = ((float)0xFF / (float)255); //Green part!
	const float greenfactor_B = ((float)0x00 / (float)255); //Blue part!
	const float amberfactor_R = ((float)0xFF / (float)255); //Red part!
	const float amberfactor_G = ((float)0xB7 / (float)255); //Green part!
	const float amberfactor_B = ((float)0x00 / (float)255); //Blue part!
	INLINEREGISTER word a,b; //16-bit!
	INLINEREGISTER uint_32 n; //32-bit!
	for (n=0;n<0x10000;n++) //Process all possible values!
	{
		//Apply the average method!
		a = n & 0xFF; //Input!
		if (DAC_luminancemethod) //Weighted?
		{
			//Here, luminance adds to 255, but R,G,B weigh differently towards that. The greyscale lookup table contains entries for the 0-255 values repeated each 256 items.
			Luminance_R[a] = (byte)((double)a * LUMINANCE_RFACTOR); //Red weighted!
			Luminance_G[a] = (byte)((double)a * LUMINANCE_GFACTOR); //Green weighted!
			Luminance_B[a] = (byte)((double)a * LUMINANCE_BFACTOR); //Blue weighted!
			//The input is the weighted sum because all factors together add up to 1 for the full luminance range!
			//a stays the same as the input low 8 bits for a direct 8-bit lookup to be archieved(the input values is the luminance factor of 0-255 because of the above weights)!
		}
		else //Unweighted?
		{
			//Here, luninance is ignored(R+G+B adds to up to 3*255), while the total is divided by 3 by the greyscale lookup table!
			Luminance_R[a] = a; //Unweighted!
			Luminance_G[a] = a; //Unweighted!
			Luminance_B[a] = a; //Unweighted!
			//The input is in the addition of the unweighted sum!
			//Optimized way of dividing by 3?
			a = n >> 2;
			b = (a >> 2);
			a += b;
			b >>= 2;
			a += b;
			b >>= 2;
			a += b;
			b >>= 2;
			a += b;
			//a is now the division of the input luminance unweighted(the sum being n), so use the divided value as output!
		}
		//Now store the results for greyscale, green and brown! Use the (un)weighted addition as input, depending on the Luminance additions!
		BWconversion_white[n] = RGB((byte)(((float)a) * whitefactor_R), (byte)(((float)a) * whitefactor_G), (byte)(((float)a) * whitefactor_B)); //Apply basic color: Create RGB in white!
		BWconversion_green[n] = RGB((byte)(((float)a) * greenfactor_R), (byte)(((float)a) * greenfactor_G), (byte)(((float)a) * greenfactor_B)); //Apply basic color: Create RGB in green!
		BWconversion_amber[n] = RGB((byte)(((float)a) * amberfactor_R), (byte)(((float)a) * amberfactor_G), (byte)(((float)a) * amberfactor_B)); //Apply basic color: Create RGB in amber!
	}
}

uint_32 color2bw(uint_32 color) //Convert color values to b/w values!
{
	INLINEREGISTER word a; //Our registers we use!
	a = Luminance_R[GETR(color)]; //Load Red channel!
	a += Luminance_G[GETG(color)]; //Load Green channel!
	a += Luminance_B[GETB(color)]; //Load Blue channel!
	return BWconversion_palette[a]; //Convert using the current palette!
}

uint_32 leavecoloralone(uint_32 color) //Leave color values alone!
{
	return color; //Normal color mode!
}

BWconversion_handler currentcolorconversion = &leavecoloralone; //Color to B/W handler!

uint_32 GA_color2bw(uint_32 color, byte is32bit) //Convert color values to b/w values!
{
	uint_32 afactor;
	afactor = ((GETA(color)|((is32bit&1)<<8))<<8); //A channel and if 32-bit or 24-bit!
	color = RGB(
							RGBA_channel[GETR(color)|afactor],
							RGBA_channel[GETG(color)|afactor],
							RGBA_channel[GETB(color)|afactor]
							); //Translate to RGB from RGBA/RGB if needed!
	return currentcolorconversion(color);
}

byte DAC_whatBWMonitor = 0; //Default: color monitor!

byte DAC_Use_BWMonitor(byte use)
{
	if (use < 2)
	{
		DAC_whatBWMonitor = use; //Use?
		if (use) //Used?
		{
			currentcolorconversion = &color2bw; //Use B/W conversion!
		}
		else //Not used?
		{
			currentcolorconversion = &leavecoloralone; //Leave the color alone!
		}
	}
	return DAC_whatBWMonitor; //Give the data!
}

byte DAC_Use_BWluminance(byte use)
{
	if (use < 2)
	{
		DAC_luminancemethod = use; //Use?
		VGA_initBWConversion(); //Update the precalcs!
		if (getActiveVGA()) //Active VGA?
		{
			DAC_updateEntries(getActiveVGA()); //Update the active VGA entries!
		}
	}
	return DAC_luminancemethod; //Give the data!
}

void DAC_updateEntry(VGA_Type *VGA, byte entry) //Update a DAC entry for rendering!
{
	VGA->precalcs.effectiveDAC[entry] = GA_color2bw(VGA->precalcs.DAC[entry],0); //Set the B/W or color entry!
}

void DAC_updateEntries(VGA_Type *VGA)
{
	int i;
	for (i=0;i<0x100;i++) //Process all entries!
	{
		DAC_updateEntry(VGA,i); //Update this entry with current values!
		VGA->precalcs.effectiveMDADAC[i] = GA_color2bw(RGB(i,i,i),0); //Update the MDA DAC!
	}
}

void VGA_initColorLevels(VGA_Type* VGA, byte enablePedestal)
{
	float startrange, endrange, rangestep, currange;
	int i;
	startrange = ((7.5/100.0)*255.0); //Start of the active range!
	endrange = 255.0f; //End of the active range!
	rangestep = (endrange - startrange) / 255.0f; //Steps for the range to take!
	currange = startrange; //What the starting level is!
	for (i = 0; i < 0x100; ++i) //Process all output levels!
	{
		if (enablePedestal) //Enable the pedestal?
		{
			VGA->DACbrightness[i] = (byte)currange; //The selected brightness for this level!
		}
		else //Pure black?
		{
			VGA->DACbrightness[i] = (byte)i; //The selected brightness for this level!
		}
		currange += rangestep; //Next entry level!
	}
}