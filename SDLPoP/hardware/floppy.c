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

#include "headers/types.h" //Basic typedefs!
#include "headers/hardware/8237A.h" //DMA controller support!
#include "headers/hardware/ports.h" //Port support!
#include "headers/basicio/io.h" //Basic I/O functionality!
#include "headers/hardware/pic.h" //PIC support!
#include "headers/basicio/dskimage.h" //DSK image support!
#include "headers/basicio/imdimage.h" //DSK image support!
#include "headers/support/log.h" //Logging support!
#include "headers/hardware/floppy.h" //Our type definitions!
#include "headers/bios/biosrom.h" //ROM support for Turbo XT BIOS detection!
#include "headers/emu/debugger/debugger.h" //For logging extra information when debugging!
#include "headers/hardware/cmos.h" //CMOS setting support!

//Configuration of the FDC...

//Enable density errors or gap length errors?
#define EMULATE_DENSITY 0
#define EMULATE_GAPLENGTH 0

//Double logging if FLOPPY_LOGFILE2 is defined!
#define FLOPPY_LOGFILE "debugger"
//#define FLOPPY_LOGFILE2 "floppy"
//#define FLOPPY_FORCELOG

//What IRQ is expected of floppy disk I/O
#define FLOPPY_IRQ 6
//What DMA channel is expected of floppy disk I/O
#define FLOPPY_DMA 2

//Floppy DMA transfer pulse time, in nanoseconds! How long to take to transfer one byte! Use the sector byte speed for now!
#define FLOPPY_DMA_TIMEOUT FLOPPY.DMArate

//Automatic setup.
#ifdef FLOPPY_LOGFILE
#ifdef FLOPPY_LOGFILE2
#define FLOPPY_LOG(...) { dolog(FLOPPY_LOGFILE,__VA_ARGS__); dolog(FLOPPY_LOGFILE2,__VA_ARGS__); }
#else
#define FLOPPY_LOG(...) { dolog(FLOPPY_LOGFILE,__VA_ARGS__); }
#endif
#else
#define FLOPPY_LOG(...)
#endif

//Logging with debugger only!
#ifdef FLOPPY_LOGFILE
#ifdef FLOPPY_FORCELOG
#define FLOPPY_LOGD(...) {FLOPPY_LOG(__VA_ARGS__)}
#else
#define FLOPPY_LOGD(...) if (MMU_logging==1) {FLOPPY_LOG(__VA_ARGS__)}
#endif
#else
#define FLOPPY_LOGD(...)
#endif

extern byte MMU_logging; //Are we logging from the MMU?

//Redirect to direct log always if below is uncommented?
/*
#undef FLOPPY_LOGD
#define FLOPPY_LOGD FLOPPY_LOG
*/

struct
{
	byte DOR; //DOR
	byte MSR; //MSR
	byte CCR; //CCR
	byte DIR; //DIR
	byte DSR;
	byte ST0;
	byte ST1;
	byte ST2;
	byte ST3;
	struct
	{
		byte data[2]; //Both data bytes!
		DOUBLE headloadtime, headunloadtime, steprate; //Current head load time, unload time and step rate for this drive!
	} DriveData[4]; //Specify for each of the 4 floppy drives!
	union
	{
		byte data[3]; //All data bytes!
		struct
		{
			byte FirstParameter0; //Set to 0
			byte SecondParameterByte;
			byte PreComp; //Precompensation value!
		};
	} Configuration; //The data from the Configure command!
	byte Locked; //Are we locked?
	byte commandstep; //Current command step! 0=Command, 1=Parameter, 2=Data, 3=Result, 0xFD: Give result then lockup, 0xFE: Locked up, 0xFF: Give error code and reset.
	byte commandbuffer[0x10000]; //Outgoing command buffer!
	word commandposition; //What position in the command (starts with commandstep=commandposition=0).
	byte databuffer[0x10000]; //Incoming data buffer!
	byte formatbuffer[0x400]; //Enough data to contain everything that can be formatted on a track using the normal track format command!
	word databufferposition; //How much data is buffered!
	word databuffersize; //How much data are we to buffer!
	byte resultbuffer[0x10]; //Incoming result buffer!
	byte resultposition; //The position in the result!
	uint_64 disk_startpos; //The start position of the buffered data in the floppy disk!
	byte IRQPending; //Are we waiting for an IRQ?
	byte DMAPending; //Pending DMA transfer?
	byte diskchanged[4]; //Disk changed?
	FLOPPY_GEOMETRY *geometries[4]; //Disk geometries!
	FLOPPY_GEOMETRY customgeometry[4]; //Custom disk geometries!
	byte reset_pending,reset_pended; //Reset pending?
	byte reset_pending_size; //Size of the pending reset max value! A maximum set of 3 with 4 drives reset!
	byte currentcylinder[4], currenthead[4], currentsector[4]; //Current head for all 4 drives(current cylinder = the idea the FDC has of the current cylinder)! Currenthead is the HD field!
	byte currentphysicalhead[4]; //Current physical head selected by the command(the byte in the command)
	byte currentformatsector[4]; //Current formatting sector(for IMD images)!
	byte physicalcylinder[4]; //Actual physical drive cyclinder that's been selected on the drive(the physical cylinder on the drive)!
	byte activecommand[4]; //What command is running to time?
	byte TC; //Terminal count triggered?
	uint_32 sectorstransferred; //Ammount of sectors transferred!
	byte MT,MFM,Skip; //MT bit, Double Density(MFM) bit and Skip bit  as set by the command, if any!
	byte floppy_resetted; //Are we resetted?
	byte ignorecommands; //Locked up by an invalid Sense Interrupt?
	byte recalibratestepsleft[4]; //Starts out at 79. Counts down with each step! Error if 0 and not track 0 reached yet!
	byte seekdestination[4]; //Where to seek to?
	byte seekrel[4]; //Seek relatively?
	byte seekrelup[4]; //Seek relatively upwards(towards larger cylinders)?
	byte MTMask; //Allow MT to be used in sector increase operations?
	DOUBLE DMArate, DMAratePending; //Current DMA transfer rate!
	byte RWRequestedCylinder; //Read/Write requested cylinder!
	byte PerpendicularMode; //Perpendicular mode enabled for these drives!
	byte readIDerror; //Error condition on read ID command?
	byte readIDdrive; //Read ID command's drive!
	byte readID_lastsectornumber; //Current sector, increasing number as a physical disk value from the index hole!
	byte datamark; //What datamark is required to be read by the command? Set for Deleted, cleared for normal.
	byte floppy_abort; //Abort the command after finishing the data phase?
	byte floppy_scanningforSectorID; //Scanning for an exact match on the sector ID from index?
	byte erroringtiming; //Are we erroring for this drive?
	byte ejectingpending[4]; //Eject any of these drives pending?
} FLOPPY; //Our floppy drive data!

//DOR

//What drive to address?
#define FLOPPY_DOR_DRIVENUMBERR (FLOPPY.DOR&3)
//Enable controller when set!
#define FLOPPY_DOR_RESTR ((FLOPPY.DOR>>2)&1)
//0=IRQ channel(Disable IRQ), 1=DMA mode(Enable IRQ)
#define FLOPPY_DOR_DMA_IRQR ((FLOPPY.DOR>>3)&1)
//All drive motor statuses!
#define FLOPPY_DOR_MOTORCONTROLR ((FLOPPY.DOR>>4)&0xF)

//Configuration byte 1

//Threadhold!
#define FLOPPY_CONFIGURATION_THRESHOLDW(val) FLOPPY.Configuration.SecondParameterByte=((FLOPPY.Configuration.SecondParameterByte&~0xF)|((val)&0xF))
#define FLOPPY_CONFIGURATION_THRESHOLDR (FLOPPY.Configuration.SecondParameterByte&0xF)
//Disable drive polling mode if set!
#define FLOPPY_CONFIGURATION_DRIVEPOLLINGMODEDISABLER ((FLOPPY.Configuration.SecondParameterByte>>4)&1)
//Disable FIFO if set!
#define FLOPPY_CONFIGURATION_FIFODISABLEW(val) FLOPPY.Configuration.SecondParameterByte=((FLOPPY.Configuration.SecondParameterByte&~0x20)&1)|(((val)&1)<<5)
#define FLOPPY_CONFIGURATION_FIFODISABLER ((FLOPPY.Configuration.SecondParameterByte>>5)&1)
//Enable Implied Seek if set!
#define FLOPPY_IMPLIEDSEEKENABLER ((FLOPPY.Configuration.SecondParameterByte>>6)&1)

//Drive data

#define FLOPPY_DRIVEDATA_HEADUNLOADTIMER(drive) (FLOPPY.DriveData[drive].data[0]&0xF)
#define FLOPPY_DRIVEDATA_STEPRATER(drive) ((FLOPPY.DriveData[drive].data[0]>>4)&0xF)
#define FLOPPY_DRIVEDATA_NDMR(drive) (FLOPPY.DriveData[drive].data[1]&1)
#define FLOPPY_DRIVEDATA_HEADLOADTIMER(drive) ((FLOPPY.DriveData[drive].data[1]>>1)&0x7F)

//MSR

//1 if busy in seek mode.
#define FLOPPY_MSR_BUSYINPOSITIONINGMODEW(drive,val) FLOPPY.MSR=((FLOPPY.MSR&~(1<<(drive)))|(((val)&1)<<(drive)))
#define FLOPPY_MSR_BUSYINPOSITIONINGMODER(drive) ((FLOPPY.MSR&(1<<(drive)))>>(drive))
//Busy: read/write command of FDC in progress. Set when received command byte, cleared at end of result phase
#define FLOPPY_MSR_COMMANDBUSYW(val) FLOPPY.MSR=((FLOPPY.MSR&~0x10)|(((val)&1)<<4))
//1 when not in DMA mode, else DMA mode, during execution phase.
#define FLOPPY_MSR_NONDMAW(val) FLOPPY.MSR=((FLOPPY.MSR&~0x20)|(((val)&1)<<5))
//1 when has data for CPU, 0 when expecting data.
#define FLOPPY_MSR_HAVEDATAFORCPUW(val) FLOPPY.MSR=((FLOPPY.MSR&~0x40)|(((val)&1)<<6))
//1 when ready for data transfer, 0 when not ready.
#define FLOPPY_MSR_RQMW(val) FLOPPY.MSR=((FLOPPY.MSR&~0x80)|(((val)&1)<<7))

//CCR
//0=500kbits/s, 1=300kbits/s, 2=250kbits/s, 3=1Mbits/s
#define FLOPPY_CCR_RATER (FLOPPY.CCR&3)
#define FLOPPY_CCR_RATEW(val) FLOPPY.CCR=((FLOPPY.CCR&~3)|((val)&3))

//DIR
//1 if high density, 0 otherwise.
#define FLOPPY_DIR_HIGHDENSITYW(val) FLOPPY.DIR=((FLOPPY.DIR&~1)|((val)&1))
//0=500, 1=300, 2=250, 3=1MBit/s
#define FLOPPY_DIR_DATARATEW(val) FLOPPY.DIR=((FLOPPY.DIR&~6)|(((val)&3)<<1))
//Always 0xF
#define FLOPPY_DIR_ALWAYSFW(val) FLOPPY.DIR=((FLOPPY.DIR&~0x78)|(((val)&0xF)<<3))
//1 when disk changed. Executing a command clears this.
#define FLOPPY_DIR_DISKCHANGE(val) FLOPPY.DIR==((FLOPPY.DIR&0x7F)|(((val)&1)<<7))

//DSR
#define FLOPPY_DSR_DRATESELR (FLOPPY.DSR&3)
#define FLOPPY_DSR_DRATESELW(val) FLOPPY.DSR=((FLOPPY.DSR&~3)|((val)&3))
#define FLOPPY_DSR_PRECOMPR ((FLOPPY.DSR>>2)&7)
#define FLOPPY_DSR_DSR_0R ((FLOPPY.DSR>>5)&1)
#define FLOPPY_DSR_POWERDOWNR ((FLOPPY.DSR>>6)&1)
#define FLOPPY_DSR_SWRESETR ((FLOPPY.DSR>>7)&1)
#define FLOPPY_DSR_SWRESETW(val) FLOPPY.DSR=((FLOPPY.DSR&~0x80)|(((val)&1)<<7))

//Status registers:

//ST0
#define FLOPPY_ST0_UNITSELECTW(val) FLOPPY.ST0=((FLOPPY.ST0&~3)|((val)&3))
#define FLOPPY_ST0_CURRENTHEADW(val) FLOPPY.ST0=((FLOPPY.ST0&~4)|(((val)&1)<<2))
#define FLOPPY_ST0_NOTREADYW(val) FLOPPY.ST0=((FLOPPY.ST0&~8)|(((val)&1)<<3))
//Set with drive fault or cannot find track 0 after 79 pulses!
#define FLOPPY_ST0_UNITCHECKW(val) FLOPPY.ST0=((FLOPPY.ST0&~0x10)|(((val)&1)<<4))
#define FLOPPY_ST0_SEEKENDW(val) FLOPPY.ST0=((FLOPPY.ST0&~0x20)|(((val)&1)<<5))
#define FLOPPY_ST0_INTERRUPTCODEW(val) FLOPPY.ST0=((FLOPPY.ST0&~0xC0)|(((val)&3)<<6))

//ST1
#define FLOPPY_ST1_NOADDRESSMARKW(val) FLOPPY.ST1=((FLOPPY.ST1&~1)|((val)&1))
#define FLOPPY_ST1_NOTWRITABLEDURINGWRITECOMMANDW(val) FLOPPY.ST1=((FLOPPY.ST1&~2)|(((val)&1)<<1))
#define FLOPPY_ST1_NODATAW(val) FLOPPY.ST1=((FLOPPY.ST1&~4)|(((val)&1)<<2))
#define FLOPPY_ST1_ALWAYS0_1(val) FLOPPY.ST1=((FLOPPY.ST1&~8)|(((val)&1)<<3))
#define FLOPPY_ST1_TIMEOUTW(val) FLOPPY.ST1=((FLOPPY.ST1&~0x10)|(((val)&1)<<4))
#define FLOPPY_ST1_DATAERRORW(val) FLOPPY.ST1=((FLOPPY.ST1&~0x20)|(((val)&1)<<5))
#define FLOPPY_ST1_ALWAYS0_2(val) FLOPPY.ST1=((FLOPPY.ST1&~0x40)|(((val)&1)<<6))
#define FLOPPY_ST1_ENDOFCYCLINDER(val) FLOPPY.ST1=((FLOPPY.ST1&~0x80)|(((val)&1)<<7))

//ST2
#define FLOPPY_ST2_NODATAADDRESSMASKDAMW(val) FLOPPY.ST2=((FLOPPY.ST2&~1)|((val)&1))
#define FLOPPY_ST2_BADCYCLINDERW(val) FLOPPY.ST2=((FLOPPY.ST2&~2)|(((val)&1)<<1))
#define FLOPPY_ST2_SEEKERRORW(val) FLOPPY.ST2=((FLOPPY.ST2&~4)|(((val)&1)<<2))
#define FLOPPY_ST2_SEEKEQUALW(val) FLOPPY.ST2=((FLOPPY.ST2&~8)|(((val)&1)<<3))
#define FLOPPY_ST2_WRONGCYCLINDERW(val) FLOPPY.ST2=((FLOPPY.ST2&~0x10)|(((val)&1)<<4))
#define FLOPPY_ST2_CRCERRORW(val) FLOPPY.ST2=((FLOPPY.ST2&~0x20)|(((val)&1)<<5))
#define FLOPPY_ST2_DELETEDADDRESSMARKW(val) FLOPPY.ST2=((FLOPPY.ST2&~0x40)|(((val)&1)<<6))
#define FLOPPY_ST2_UNUSEDW(val) FLOPPY.ST2=((FLOPPY.ST2&~0x80)|(((val)&1)<<7))

//ST3
#define FLOPPY_ST3_DRIVESELECTW(val) FLOPPY.ST3=((FLOPPY.ST3&~3)|((val)&3))
#define FLOPPY_ST3_HEAD1ACTIVEW(val) FLOPPY.ST3=((FLOPPY.ST3&~4)|(((val)&1)<<2))
#define FLOPPY_ST3_DOUBLESIDEDW(val) FLOPPY.ST3=((FLOPPY.ST3&~8)|(((val)&1)<<3))
#define FLOPPY_ST3_TRACK0W(val) FLOPPY.ST3=((FLOPPY.ST3&~0x10)|(((val)&1)<<4))
#define FLOPPY_ST3_DRIVEREADYW(val) FLOPPY.ST3=((FLOPPY.ST3&~0x20)|(((val)&1)<<5))
#define FLOPPY_ST3_WRITEPROTECTIONW(val) FLOPPY.ST3=((FLOPPY.ST3&~0x40)|(((val)&1)<<6))
#define FLOPPY_ST3_ERRORSIGNATUREW(val) FLOPPY.ST3=((FLOPPY.ST3&~0x80)|(((val)&1)<<7))

//Values loaded in ST1/2 when a disk is ejected mid-transfer
#define ST1_MEDIAEJECTED 0x24
#define ST2_MEDIAEJECTED 0x01

//Start normal data!

byte density_forced = 0; //Default: don't ignore the density with the CPU!

DOUBLE floppytimer[5] = {0.0,0.0,0.0,0.0,0.0}; //The timer for ticking floppy disk actions!
DOUBLE floppytime[5] = {0.0,0.0,0.0,0.0}; //Buffered floppy disk time!
byte floppytiming = 0; //Are we timing?

extern byte is_XT; //Are we emulating a XT architecture?

//Formulas for the different rates:

/*

Step rate(ms):
1M = 8-(val*0.5)
500k = 16-val
300k = (26+(2/3))-(val*(1+(2/3)))
250k = 32-(val*2)

Head Unload Time:
val 0h becomes 10h.
1M = 8*val
500k = 16*val
300k = (26+(2/3))*val
250k = 32*val

Head Load Time:
val 0h becomes 80h.
1M = val
500k = 2*val
300k = (3+(1/3))*val
250k = 4*val

*/

DOUBLE floppy_steprate[4][0x10]; //All possible step rates!
DOUBLE floppy_headunloadtimerate[4][0x10]; //All possible head (un)load times!
DOUBLE floppy_headloadtimerate[4][0x80]; //All possible head load times!

void initFloppyRates()
{
	DOUBLE steprate_base[4] = {0,0,0,0}; //The base to take, in ms!
	DOUBLE steprate_addition[4] = {0,0,0,0}; //The multiplier to add, in ms
	DOUBLE headunloadtime_addition[4] = {0,0,0,0}; //The multiplier to add, in ms
	DOUBLE headloadtime_addition[4] = {0,0,0,0}; //The multiplier to add, in ms
	//We initialize all floppy disk rates, in milliseconds!
	//The order is of the rates is: 500k, 300k, 250k, 1M
	//Step rate!
	#ifdef IS_LONGDOUBLE
	steprate_base[0] = 16.0L;
	steprate_base[1] = 26.0L+(2.0L/3.0L);
	steprate_base[2] = 32.0L;
	steprate_base[3] = 8.0L;
	steprate_addition[0] = -1.0L;
	steprate_addition[1] = -(1.0L+(2.0L/3.0L));
	steprate_addition[2] = -2.0L;
	steprate_addition[3] = -0.5L;
	
	//Head Unload Time
	headunloadtime_addition[0] = 16.0L;
	headunloadtime_addition[1] = 26.0L+(2.0L/3.0L);
	headunloadtime_addition[2] = 32.0L;
	headunloadtime_addition[3] = 8.0L;

	//Head Load Time
	headloadtime_addition[0] = 2.0L;
	headloadtime_addition[1] = 3.0L+(1.0L/3.0L);
	headloadtime_addition[2] = 4.0L;
	headloadtime_addition[3] = 1.0L;
	#else
	steprate_base[0] = 16.0;
	steprate_base[1] = 26.0+(2.0/3.0);
	steprate_base[2] = 32.0;
	steprate_base[3] = 8.0;
	steprate_addition[0] = -1.0;
	steprate_addition[1] = -(1.0+(2.0/3.0));
	steprate_addition[2] = -2.0;
	steprate_addition[3] = -0.5;
	
	//Head Unload Time
	headunloadtime_addition[0] = 16.0;
	headunloadtime_addition[1] = 26.0+(2.0/3.0);
	headunloadtime_addition[2] = 32.0;
	headunloadtime_addition[3] = 8.0;

	//Head Load Time
	headloadtime_addition[0] = 2.0;
	headloadtime_addition[1] = 3.0+(1.0/3.0);
	headloadtime_addition[2] = 4.0;
	headloadtime_addition[3] = 1.0;
	#endif

	//Now, to be based on the used data, calculate all used lookup tables!
	byte rate,ratesel,usedrate;
	for (ratesel=0;ratesel<4;++ratesel) //All rate selections!
	{
		for (rate=0;rate<0x10;++rate) //Process all rate timings for step rate&head unload time!
		{
			usedrate = rate?rate:0x10; //0 sets bit 4 for head unload time!
			#ifdef IS_LONGDOUBLE
			floppy_steprate[ratesel][rate] = (steprate_base[ratesel]+(steprate_addition[ratesel]*(DOUBLE)rate))*1000000.0L; //Time, in nanoseconds!
			floppy_headunloadtimerate[ratesel][rate] = (headunloadtime_addition[ratesel]*(DOUBLE)usedrate)*1000000.0L; //Time, in nanoseconds!
			#else
			floppy_steprate[ratesel][rate] = (steprate_base[ratesel]+(steprate_addition[ratesel]*(DOUBLE)rate))*1000000.0; //Time, in nanoseconds!
			floppy_headunloadtimerate[ratesel][rate] = (headunloadtime_addition[ratesel]*(DOUBLE)usedrate)*1000000.0; //Time, in nanoseconds!
			#endif
		}
		for (rate=0;rate<0x80;++rate) //Process all rate timings for head load time!
		{
			usedrate = rate?rate:0x80; //0 sets bit 8!
			#ifdef IS_LONGDOUBLE
			floppy_headloadtimerate[ratesel][rate] = (headloadtime_addition[ratesel]*(DOUBLE)usedrate)*1000000.0L; //Time, in nanoseconds!
			#else
			floppy_headloadtimerate[ratesel][rate] = (headloadtime_addition[ratesel]*(DOUBLE)usedrate)*1000000.0; //Time, in nanoseconds!
			#endif
		}
	}
}

//Step rate is the duration between pulses of a Seek/Recalibrate command.
OPTINLINE DOUBLE FLOPPY_steprate(byte drivenumber)
{
	return floppy_steprate[FLOPPY_DSR_DRATESELR][FLOPPY_DRIVEDATA_STEPRATER(drivenumber)]; //Look up the step rate for this disk!
}

//Head Load Time is applied when the head is unloaded and an operation doing anything with floppy media is executed(before data transfer).
OPTINLINE DOUBLE FLOPPY_headloadtimerate(byte drivenumber)
{
	return floppy_headloadtimerate[FLOPPY_DSR_DRATESELR][FLOPPY_DRIVEDATA_HEADLOADTIMER(drivenumber)]; //Look up the head load time rate for this disk!
}

//Head Unload Time is the time after the read/write data operation, at which the head is unloaded.
OPTINLINE DOUBLE FLOPPY_headunloadtimerate(byte drivenumber)
{
	return floppy_headunloadtimerate[FLOPPY_DSR_DRATESELR][FLOPPY_DRIVEDATA_HEADUNLOADTIMER(drivenumber)]; //Look up the head load time rate for this disk!
}

//Floppy sector reading rate, depending on RPM and Sectors per Track! Each round reads/writes a full track always! Gives the amount of nanoseconds per sector!
OPTINLINE DOUBLE FLOPPY_sectorrate(byte drivenumber)
{
	if (FLOPPY.geometries[drivenumber]) //Valid geometry?
	{
		#ifdef IS_LONGDOUBLE
		return (60000000000.0L/(DOUBLE)FLOPPY.geometries[drivenumber]->RPM)/((FLOPPY.activecommand[drivenumber] == 0xD) ? MAX(FLOPPY.commandbuffer[3],1) : (DOUBLE)FLOPPY.geometries[drivenumber]->SPT); //We're at a constant speed, which is RPM divided up by Sectors per Track(Each track takes one round to read always)!
		#else
		return (60000000000.0/(DOUBLE)FLOPPY.geometries[drivenumber]->RPM)/((FLOPPY.activecommand[drivenumber] == 0xD) ? MAX(FLOPPY.commandbuffer[3], 1) : (DOUBLE)FLOPPY.geometries[drivenumber]->SPT); //We're at a constant speed, which is RPM divided up by Sectors per Track(Each track takes one round to read always)!
		#endif
	}
	else //Default rate for unknown disk geometries!
	{
		#ifdef IS_LONGDOUBLE
		return (60000000000.0L/(DOUBLE)300)/ ((FLOPPY.activecommand[drivenumber] == 0xD) ? MAX(FLOPPY.commandbuffer[3], 1) : (DOUBLE)80); //We're at a constant speed, which is RPM divided up by Sectors per Track(Each track takes one round to read always)!
		#else
		return (60000000000.0/(DOUBLE)300)/ ((FLOPPY.activecommand[drivenumber] == 0xD) ? MAX(FLOPPY.commandbuffer[3], 1) : (DOUBLE)80); //We're at a constant speed, which is RPM divided up by Sectors per Track(Each track takes one round to read always)!
		#endif
	}
}

//Normal floppy specific stuff

#define KB(x) (x/1024)

//Floppy commands from OSDev.
enum FloppyCommands
{
   READ_TRACK =                 2,	// generates IRQ6
   SPECIFY =                    3,      // * set drive parameters
   SENSE_DRIVE_STATUS =         4,
   WRITE_DATA =                 5,      // * write to the disk
   READ_DATA =                  6,      // * read from the disk
   RECALIBRATE =                7,      // * seek to cylinder 0
   SENSE_INTERRUPT =            8,      // * ack IRQ6, get status of last command
   WRITE_DELETED_DATA =         9,
   READ_ID =                    10,	// generates IRQ6
   READ_DELETED_DATA =          12,
   FORMAT_TRACK =               13,     // *
   DUMPREG =                    14, //extended controller only!
   SEEK =                       15,     // * seek both heads to cylinder X
   VERSION =                    16,	// * used during initialization, once
   SCAN_EQUAL =                 17,
   PERPENDICULAR_MODE =         18,	// * used during initialization, once, maybe
   CONFIGURE =                  19,     // * set controller parameters
   LOCK =                       20,     // * protect controller params from a reset
   VERIFY =                     22,
   SCAN_LOW_OR_EQUAL =          25,
   SCAN_HIGH_OR_EQUAL =         29
};

//Allowed transfer rates!
#define TRANSFERRATE_500k 0
#define TRANSFERRATE_300k 1
#define TRANSFERRATE_250k 2
#define TRANSFERRATE_1M 3

//Allowed gap length!
#define GAPLENGTH_IGNORE 0
#define GAPLENGTH_STD 42
#define GAPLENGTH_5_14 32
#define GAPLENGTH_3_5 27

//Flags when executing commands!
#define CMD_EXT_SKIPDELETEDADDRESSMARKS 0x20
#define CMD_EXT_MFMMODE 0x40
#define CMD_EXT_MULTITRACKOPERATION 0x80

//Density limits and specification!
#define DENSITY_SINGLE 0
#define DENSITY_DOUBLE 1
#define DENSITY_HD 2
#define DENSITY_ED 4
//Ignore density on specific target BIOS/CPU?
#define DENSITY_IGNORE 8

//82072AA diskette EHD controller board jumper settings
#define FLOPPYTYPE_12MB 0
#define FLOPPYTYPE_720K 1
#define FLOPPYTYPE_28MB 2
#define FLOPPYTYPE_14MB 3

//Simple defines for optimizing floppy disk speed in the lookup table!
#define FLOPPYDISK_LOWSPEED TRANSFERRATE_250k|(TRANSFERRATE_300k<<2)|(TRANSFERRATE_500k<<4)|(TRANSFERRATE_500k<<6)
#define FLOPPYDISK_MIDSPEED TRANSFERRATE_250k|(TRANSFERRATE_300k<<2)|(TRANSFERRATE_500k<<4)|(TRANSFERRATE_500k<<6)
#define FLOPPYDISK_HIGHSPEED (TRANSFERRATE_250k|(TRANSFERRATE_300k<<2)|(TRANSFERRATE_500k<<4)|(TRANSFERRATE_1M<<6))

FLOPPY_GEOMETRY floppygeometries[NUMFLOPPYGEOMETRIES] = { //Differently formatted disks, and their corresponding geometries
	//First, 5"
	{ 160,  8,  1, 40, FLOPPYTYPE_12MB, 0, FLOPPYDISK_LOWSPEED, 0xFE,512 , 1, 64 ,DENSITY_SINGLE           ,GAPLENGTH_5_14,    0x00, 300, "160KB disk 5.25\"" }, //160K 5.25" supports 250kbits, 300kbits SD!
	{ 180,  9,  1, 40, FLOPPYTYPE_12MB, 0, FLOPPYDISK_LOWSPEED, 0xFC,512 , 2, 64 ,DENSITY_SINGLE           ,GAPLENGTH_5_14,    0x00, 300, "180KB disk 5.25\"" }, //180K 5.25" supports 250kbits, 300kbits SD!
	{ 200, 10,  1, 40, FLOPPYTYPE_12MB, 0, FLOPPYDISK_LOWSPEED, 0xFC,512 , 2, 64 ,DENSITY_SINGLE           ,GAPLENGTH_5_14,    0x00, 300, "200KB disk 5.25\"" }, //200K 5.25" supports 250kbits, 300kbits SD!
	{ 320,  8,  2, 40, FLOPPYTYPE_12MB, 0, FLOPPYDISK_LOWSPEED, 0xFF,512 , 1, 112,DENSITY_SINGLE           ,GAPLENGTH_5_14,    0x00, 300, "320KB disk 5.25\"" }, //320K 5.25" supports 250kbits, 300kbits SD!
	{ 360,  9,  2, 40, FLOPPYTYPE_12MB, 0, FLOPPYDISK_LOWSPEED, 0xFD,1024, 2, 112,DENSITY_DOUBLE           ,GAPLENGTH_5_14,    0x00, 300, "360KB disk 5.25\"" }, //360K 5.25" supports 250kbits, 300kbits DD!
	{ 400, 10,  2, 40, FLOPPYTYPE_12MB, 0, FLOPPYDISK_LOWSPEED, 0xFD,1024, 2, 112,DENSITY_SINGLE           ,GAPLENGTH_5_14,    0x00, 300, "400KB disk 5.25\"" }, //400K 5.25" supports 250kbits, 300kbits SD!
	{1200, 15,  2, 80, FLOPPYTYPE_12MB, 0, FLOPPYDISK_MIDSPEED, 0xF9,512 , 7, 224,DENSITY_SINGLE           ,GAPLENGTH_5_14,    0x00, 360, "1.2MB disk 5.25\"" }, //1200K 5.25" supports 300kbits, 500kbits SD!
	//Now 3.5"
	{ 720,  9,  2, 80, FLOPPYTYPE_720K, 1, FLOPPYDISK_LOWSPEED, 0xF9,1024, 3, 112,DENSITY_DOUBLE           ,GAPLENGTH_3_5,     0xC0, 300, "720KB disk 3.5\"" }, //720K 3.5" supports 250kbits, 300kbits DD! Disable gap length checking here because we need to work without it on a XT?
	{1440, 18,  2, 80, FLOPPYTYPE_14MB, 1, FLOPPYDISK_MIDSPEED, 0xF0,512 , 9, 224,DENSITY_IGNORE|DENSITY_HD,GAPLENGTH_3_5,     0x80, 300, "1.44MB disk 3.5\"" }, //1.44M 3.5" supports 250kbits, 500kbits HD! Disable gap length checking here because we need to work without it on a XT?
	{1680, 21,  2, 80, FLOPPYTYPE_14MB, 1, FLOPPYDISK_MIDSPEED, 0xF0,512 , 9, 224,DENSITY_IGNORE|DENSITY_HD,GAPLENGTH_3_5,     0x80, 300, "1.68MB disk 3.5\"" }, //1.68M 3.5" supports 250kbits, 500kbits HD! Supporting BIOS only!
	{1722, 21,  2, 82, FLOPPYTYPE_14MB, 1, FLOPPYDISK_MIDSPEED, 0xF0,512 , 9, 224,DENSITY_IGNORE|DENSITY_HD,GAPLENGTH_3_5,     0x80, 300, "1.722MB disk 3.5\"" }, //1.722M 3.5" supports 250kbits, 500kbits HD! Supporting BIOS only!
	{1840, 23,  2, 80, FLOPPYTYPE_14MB, 1, FLOPPYDISK_MIDSPEED, 0xF0,512 , 9, 224,DENSITY_IGNORE|DENSITY_HD,GAPLENGTH_3_5,     0x80, 300, "1.84MB disk 3.5\"" }, //1.84M 3.5" supports 250kbits, 500kbits HD! Supporting BIOS only!
	{2880, 36,  2, 80, FLOPPYTYPE_28MB, 1, FLOPPYDISK_HIGHSPEED,0xF0,1024, 9, 240,DENSITY_IGNORE|DENSITY_ED,GAPLENGTH_IGNORE,  0x40, 300, "2.88MB disk 3.5\"" } //2.88M 3.5" supports 1Mbits ED!
};

//BPS=512 always(except differently programmed)!

//Floppy geometries

byte floppy_spt(uint_64 floppy_size)
{
	int i;
	for (i = 0; i<(int)NUMITEMS(floppygeometries); i++)
	{
		if (floppygeometries[i].KB == KB(floppy_size)) return (byte)floppygeometries[i].SPT; //Found?
	}
	return 0; //Unknown!
}

byte floppy_tracks(uint_64 floppy_size)
{
	int i;
	for (i = 0; i<(int)NUMITEMS(floppygeometries); i++)
	{
		if (floppygeometries[i].KB == KB(floppy_size)) return floppygeometries[i].tracks; //Found?
	}
	return 0; //Unknown!
}

byte floppy_sides(uint_64 floppy_size)
{
	int i;
	for (i = 0; i<(int)NUMITEMS(floppygeometries); i++)
	{
		if (floppygeometries[i].KB == KB(floppy_size)) return (byte)floppygeometries[i].sides; //Found?
	}
	return 0; //Unknown!
}

//Simple floppy recalibrate/seek action complete handlers!
void FLOPPY_finishrecalibrate(byte drive);
void FLOPPY_finishseek(byte drive, byte finishIRQ);
void FLOPPY_checkfinishtiming(byte drive);

extern CMOS_Type CMOS;

OPTINLINE void updateFloppyGeometries(byte floppy, byte side, byte track)
{
	IMDIMAGE_SECTORINFO IMDImage_sectorinfo, IMDImage_diskinfo;
	uint_64 floppysize = disksize(floppy); //Retrieve disk size for reference!
	byte i;
	char *DSKImageFile = NULL; //DSK image file to use?
	char* IMDImageFile = NULL; //IMD image file to use?
	DISKINFORMATIONBLOCK DSKInformation;
	TRACKINFORMATIONBLOCK DSKTrackInformation;
	FLOPPY.geometries[floppy] = NULL; //Init geometry to unknown!
	if (!((DSKImageFile = getDSKimage((floppy) ? FLOPPY1 : FLOPPY0)) || (IMDImageFile = getIMDimage((floppy) ? FLOPPY1 : FLOPPY0)))) //Are we not a DSK/IMD image file?
	{
		for (i = 0; i < NUMITEMS(floppygeometries); i++) //Update the geometry!
		{
			if (floppygeometries[i].KB == KB(floppysize)) //Found?
			{
				FLOPPY.geometries[floppy] = &floppygeometries[i]; //The geometry we use!
				return; //Stop searching!
			}
		}
	}

	//Unknown geometry!
	if ((DSKImageFile = getDSKimage((floppy) ? FLOPPY1 : FLOPPY0))) //Are we a DSK image file?
	{
		if (readDSKInfo(DSKImageFile, &DSKInformation)) //Gotten information about the DSK image?
		{
			if (readDSKTrackInfo(DSKImageFile, side, track, &DSKTrackInformation))
			{
				FLOPPY.geometries[floppy] = &FLOPPY.customgeometry[floppy]; //Apply custom geometry!
				FLOPPY.customgeometry[floppy].sides = DSKInformation.NumberOfSides; //Number of sides!
				FLOPPY.customgeometry[floppy].tracks = DSKInformation.NumberOfTracks; //Number of tracks!
				FLOPPY.customgeometry[floppy].SPT = DSKTrackInformation.numberofsectors; //Number of sectors in this track!
				//Fill in the remaining information with defaults!
				FLOPPY.customgeometry[floppy].RPM = 300; //Default to 300 RPM!
				FLOPPY.customgeometry[floppy].boardjumpersetting  = 0; //Unknown, leave at 0!
				FLOPPY.customgeometry[floppy].ClusterSize = 0; //Unknown!
				FLOPPY.customgeometry[floppy].DirectorySize = 0; //Unknown!
				FLOPPY.customgeometry[floppy].DoubleDensity = (DSKTrackInformation.numberofsectors>40); //Probably double density?
				FLOPPY.customgeometry[floppy].FATSize = 0; //Unknown!
				FLOPPY.customgeometry[floppy].GAPLength = DSKTrackInformation.GAP3Length; //Our GAP3 length used!
				FLOPPY.customgeometry[floppy].KB = (word)((uint_32)(DSKInformation.NumberOfTracks*DSKInformation.NumberOfSides*DSKInformation.TrackSize)>>10); //Raw size!
				FLOPPY.customgeometry[floppy].measurement = DSKTrackInformation.numberofsectors>40?1:0; //Unknown, take 3,5" when >40 tracks!
				FLOPPY.customgeometry[floppy].MediaDescriptorByte = 0x00; //Unknown!
				FLOPPY.customgeometry[floppy].supportedrates = 0x1B; //Support all rates!
				FLOPPY.customgeometry[floppy].TapeDriveRegister = 0x00; //Unknown!
				return; //Geometry obtained!
			}
		}
	}

	if ((IMDImageFile = getIMDimage((floppy) ? FLOPPY1 : FLOPPY0))) //Are we a IMD image file?
	{
		if (readIMDDiskInfo(IMDImageFile, &IMDImage_diskinfo)) //Gotten disk info?
		{
			if (readIMDSectorInfo(IMDImageFile, track, side, 0, &IMDImage_sectorinfo)) //Gotten information about the IMD image?
			{
				FLOPPY.geometries[floppy] = &FLOPPY.customgeometry[floppy]; //Apply custom geometry!
				FLOPPY.customgeometry[floppy].sides = (IMDImage_diskinfo.headnumber+1); //Number of sides!
				FLOPPY.customgeometry[floppy].tracks = (IMDImage_diskinfo.cylinderID+1); //Number of tracks!
				FLOPPY.customgeometry[floppy].SPT = IMDImage_sectorinfo.totalsectors; //Number of sectors in this track and side!
				//Fill in the remaining information with defaults!
				FLOPPY.customgeometry[floppy].RPM = 300; //Default to 300 RPM!
				FLOPPY.customgeometry[floppy].boardjumpersetting = 0; //Unknown, leave at 0!
				FLOPPY.customgeometry[floppy].ClusterSize = 0; //Unknown!
				FLOPPY.customgeometry[floppy].DirectorySize = 0; //Unknown!
				FLOPPY.customgeometry[floppy].DoubleDensity = (IMDImage_sectorinfo.MFM_speedmode & FORMATTED_MFM) ? 1 : 0; //Probably double density?
				FLOPPY.customgeometry[floppy].FATSize = 0; //Unknown!
				FLOPPY.customgeometry[floppy].GAPLength = GAPLENGTH_IGNORE; //Our GAP3 length used! Unknown!
				FLOPPY.customgeometry[floppy].KB = 0; //Raw size! Unknown!
				FLOPPY.customgeometry[floppy].measurement = (IMDImage_diskinfo.cylinderID >= 40) ? 1 : 0; //Unknown, take 3,5" when >40 tracks!
				FLOPPY.customgeometry[floppy].MediaDescriptorByte = 0x00; //Unknown!
				FLOPPY.customgeometry[floppy].supportedrates = 0x1B; //Support all rates for now!
				FLOPPY.customgeometry[floppy].TapeDriveRegister = 0x00; //Unknown!
				return; //Geometry obtained!
			}
		}
	}
	//Another try, find biggest fit!
	word largestKB = 0;
	FLOPPY_GEOMETRY *largestgeometry = NULL; //The largest found!
	for (i = 0; i < NUMITEMS(floppygeometries); ++i)
	{
		if ((floppygeometries[i].KB > largestKB) && (floppygeometries[i].KB <= KB(floppysize))) //New largest found within range?
		{
			largestgeometry = &floppygeometries[i]; //Use this one as the largest!
			largestKB = floppygeometries[i].KB; //Largest KB detected within range!
		}
	}

	if (largestgeometry) //Largest geometry found?
	{
		FLOPPY.geometries[floppy] = largestgeometry; //Use the largest geometry found!
		return; //Stop searching!
	}
	
	//If we reach here, we're an unmounted geometry!
	if (floppy < 2) //Valid floppy to get the default geometry from??
	{
		FLOPPY.geometries[floppy] = &floppygeometries[floppy?CMOS.DATA.floppy0_nodisk_type:CMOS.DATA.floppy1_nodisk_type]; //Set geometry!
		if (FLOPPY.physicalcylinder[floppy]>(FLOPPY.geometries[floppy]->tracks-1)) //Invalid cylinder?
		{
			FLOPPY.physicalcylinder[floppy] = (FLOPPY.geometries[floppy]->tracks - 1); //Return to the last track, physically, since no track exists there!
		}
	}
	else //Unmountable floppy?
	{
		if (FLOPPY.physicalcylinder[floppy]) //Invalid cylinder?
		{
			FLOPPY.physicalcylinder[floppy] = 0; //Return to track 0, physically, since no track exists there!
		}
	}
}

uint_32 floppy_LBA(byte floppy, word side, word track, word sector)
{
	updateFloppyGeometries(floppy,(byte)side,(byte)track); //Update the floppy geometries!
	if (!FLOPPY.geometries[floppy]) return 0; //Unknown floppy geometry!
	return (uint_32)(((track*FLOPPY.geometries[floppy]->sides) + side) * FLOPPY.geometries[floppy]->SPT) + sector - 1; //Give LBA for floppy!
}

//Sector size

OPTINLINE word translateSectorSize(byte size)
{
	return 128<<size; //Give the translated sector size!
}

void FLOPPY_notifyDiskChanged(int disk)
{
	switch (disk)
	{
	case FLOPPY0:
		FLOPPY.diskchanged[0] = 1; //Changed!
		FLOPPY.ejectingpending[0] = 1; //Ejecting pending!
		break;
	case FLOPPY1:
		FLOPPY.diskchanged[1] = 1; //Changed!
		FLOPPY.ejectingpending[1] = 1; //Ejecting pending!
		break;
	default:
		break;
	}
}

OPTINLINE void FLOPPY_raiseIRQ() //Execute an IRQ!
{
	if (FLOPPY_DOR_DMA_IRQR)
	{
		FLOPPY.IRQPending = 1; //We're waiting for an IRQ!
		raiseirq(FLOPPY_IRQ); //Execute the IRQ when enabled!
	}
}

OPTINLINE void FLOPPY_lowerIRQ()
{
	FLOPPY.IRQPending = 0; //We're not pending anymore!
	lowerirq(FLOPPY_IRQ); //Lower the IRQ!
	acnowledgeIRQrequest(FLOPPY_IRQ); //Acnowledge!
}

OPTINLINE byte FLOPPY_useDMA()
{
	return ((FLOPPY_DOR_DMA_IRQR && (FLOPPY_DRIVEDATA_NDMR(FLOPPY_DOR_DRIVENUMBERR)==0))&1); //Are we using DMA?
}

OPTINLINE byte FLOPPY_supportsrate(byte disk)
{
	if (!FLOPPY.geometries[disk]) //Unknown geometry?
	{
		#ifdef IS_LONGDOUBLE
		FLOPPY.DMAratePending = (FLOPPY_sectorrate(FLOPPY_DOR_DRIVENUMBERR)/512.0L); //Set the rate used as active to transfer data one byte at a time, simply taken the sector rate!
		#else
		FLOPPY.DMAratePending = (FLOPPY_sectorrate(FLOPPY_DOR_DRIVENUMBERR)/512.0); //Set the rate used as active to transfer data one byte at a time, simply taken the sector rate!
		#endif
		return 1; //No disk geometry, so supported by default(unknown drive)!
	}
	byte supported = 0, current=0, currentrate;
	supported = FLOPPY.geometries[disk]->supportedrates; //Load the supported rates!
	currentrate = FLOPPY_CCR_RATER; //Current rate we use (both CCR and DSR can be used, since they're both updated when either changes)!
	for (;current<4;) //Check all available rates!
	{
		if (currentrate==(supported&3))
		{
			#ifdef IS_LONGDOUBLE
			FLOPPY.DMAratePending = (FLOPPY_sectorrate(FLOPPY_DOR_DRIVENUMBERR)/512.0L); //Set the rate used as active to transfer data one byte at a time, simply taken the sector rate!
			#else
			FLOPPY.DMAratePending = (FLOPPY_sectorrate(FLOPPY_DOR_DRIVENUMBERR)/512.0); //Set the rate used as active to transfer data one byte at a time, simply taken the sector rate!
			#endif
			return 1; //We're a supported rate!
		}
		supported  >>= 2; //Check next rate!
		++current; //Next supported!
	}
	return 0; //Unsupported rate!
}

OPTINLINE void updateST3(byte drivenumber)
{
	FLOPPY.ST3 |= 0x28; //Always set according to Bochs!
	FLOPPY_ST3_TRACK0W((FLOPPY.physicalcylinder[drivenumber] == 0)?1:0); //Are we at track 0?

	if (FLOPPY.geometries[drivenumber]) //Valid drive?
	{
		FLOPPY_ST3_DOUBLESIDEDW((FLOPPY.geometries[drivenumber]->sides==2)?1:0); //Are we double sided?
	}
	else //Apply default disk!
	{
		FLOPPY_ST3_DOUBLESIDEDW(1); //Are we double sided?
	}
	FLOPPY_ST3_HEAD1ACTIVEW(FLOPPY.currentphysicalhead[drivenumber]); //Is head 1 active?
	FLOPPY_ST3_DRIVESELECTW(drivenumber); //Our selected drive!
	FLOPPY_ST3_DRIVEREADYW(1); //We're always ready on PC!
	if (drivenumber<2) //Valid drive number?
	{
		FLOPPY_ST3_WRITEPROTECTIONW((drivereadonly(drivenumber ? FLOPPY1 : FLOPPY0)||drivewritereadonly(drivenumber ? FLOPPY1 : FLOPPY0))?1:0); //Read-only drive and tried to write?
	}
	else
	{
		FLOPPY_ST3_WRITEPROTECTIONW(0); //Drive unsupported? No write protection!
	}
	FLOPPY_ST3_ERRORSIGNATUREW(0); //No errors here!
}

byte FLOPPY_hadIRQ = 0; //Did we have an IRQ raised?

OPTINLINE void FLOPPY_handlereset(byte source) //Resets the floppy disk command when needed!
{
	byte pending_size; //Our currently pending size to use!
	if ((!FLOPPY_DOR_RESTR) || FLOPPY_DSR_SWRESETR) //We're to reset by either one enabled?
	{
		if (!FLOPPY.floppy_resetted) //Not resetting yet?
		{
			if (source==1)
			{
				FLOPPY_LOGD("FLOPPY: Reset requested by DSR!")
			}
			else
			{
				FLOPPY_LOGD("FLOPPY: Reset requested by DOR!")
			}
			FLOPPY.DIR = 0; //Disk changed bit?
			FLOPPY.CCR = 0;
			FLOPPY.MSR = 0; //Default to no data!
			FLOPPY.commandposition = 0; //No command!
			FLOPPY.commandstep = 0; //Reset step to indicate we're to read the result in ST0!
			FLOPPY.ST0 = FLOPPY.ST1 = FLOPPY.ST2 = FLOPPY.ST3 =  0; //Reset all ST data!
			pending_size = 4; //Pending full size with polling mode enabled!
			if (FLOPPY_CONFIGURATION_DRIVEPOLLINGMODEDISABLER) pending_size = 0; //Don't pend when polling mode is off!
			if (pending_size) FLOPPY.ST0 |= 0xC0; //Top 2 bits are set when polling is enabled only!
			else FLOPPY.reset_pending_size = 0xFF; //Invalid to issue an Sense Interrupt command!
			FLOPPY.reset_pending_size = FLOPPY.reset_pending = pending_size; //We have a reset pending for all 4 drives, unless interrupted by an other command!
			FLOPPY.reset_pended = 1; //We're pending a reset! Clear status once we're becoming active!
			memset(&FLOPPY.currenthead, 0, sizeof(FLOPPY.currenthead)); //Clear the current heads!
			memset(&FLOPPY.currentphysicalhead, 0, sizeof(FLOPPY.currentphysicalhead)); //Clear the phyiscal heads!
			memset(&FLOPPY.currentsector, 1, sizeof(FLOPPY.currentsector)); //Clear the current sectors!
			updateST3(0); //Update ST3 only!
			FLOPPY.TC = 0; //Disable TC identifier!
			if (FLOPPY.Locked==0) //Are we not locked? Perform stuff that's not locked during reset!
			{
				FLOPPY_CONFIGURATION_THRESHOLDW(0); //Reset threshold!
				FLOPPY_CONFIGURATION_FIFODISABLEW(1); //Disable the FIFO!
			}
			FLOPPY.PerpendicularMode &= ~3; //Soft reset clears GAP and WGATE(bits 0&1). Hardware reset also clears D0-D3(bits 2/3/4/5).
			//Make sure the IRQ works when resetting always!
			FLOPPY.floppy_resetted = 1; //We're resetted!
			FLOPPY.ignorecommands = 0; //We're enabling commands again!
			//Make sure
			FLOPPY_hadIRQ = 0; //Was an IRQ Pending? Nope, we're resetting!
			FLOPPY_lowerIRQ(); //Lower the IRQ!

			//Terminate running commands timing and DMA transfers running!
			floppytimer[0] = (DOUBLE)0;
			floppytimer[1] = (DOUBLE)0;
			floppytimer[2] = (DOUBLE)0;
			floppytimer[3] = (DOUBLE)0;
			FLOPPY_checkfinishtiming(0); //Check for finished timing!
			FLOPPY_checkfinishtiming(1); //Check for finished timing!
			FLOPPY_checkfinishtiming(2); //Check for finished timing!
			FLOPPY_checkfinishtiming(3); //Check for finished timing!
			FLOPPY.DMAPending = 0; //No DMA transfer busy!
		}
	}
	else if (FLOPPY.floppy_resetted==1) //We were resetted and are activated?
	{
		//Raise an IRQ and become active after that?
		floppytimer[4] = (DOUBLE)30000.0; //30us timer!
		floppytime[4] = (DOUBLE)0.0; //Start timing this!
		floppytiming |= 0x10; //Start timing this timer!
		FLOPPY.floppy_resetted = 2; //Starting the timing, don't trigger it again!
		if (source==1)
		{
			FLOPPY_LOGD("FLOPPY: Activation requested by DSR!")
		}
		else
		{
			FLOPPY_LOGD("FLOPPY: Activation requested by DOR!")
		}
	}
}

//Execution after command and data phrases!
byte oldMSR = 0; //Old MSR!

OPTINLINE void updateFloppyMSR() //Update the floppy MSR!
{
	switch (FLOPPY.commandstep) //What command step?
	{
	case 0: //Command?
		FLOPPY.sectorstransferred = 0; //There's nothing transferred yet!
		FLOPPY_MSR_COMMANDBUSYW(0); //Not busy: we're waiting for a command!
		FLOPPY_MSR_RQMW(!FLOPPY.floppy_resetted); //Ready for data transfer when not being reset!
		FLOPPY_MSR_HAVEDATAFORCPUW(0); //We don't have data for the CPU!
		FLOPPY_MSR_NONDMAW(0); //No DMA transfer busy!
		break;
	case 1: //Parameters?
		FLOPPY_MSR_COMMANDBUSYW(1); //Default: busy!
		FLOPPY_MSR_RQMW(1); //Ready for data transfer!
		FLOPPY_MSR_HAVEDATAFORCPUW(0); //We don't have data for the CPU!
		FLOPPY_MSR_NONDMAW(0); //No DMA transfer busy!
		break;
	case 2: //Data?
		FLOPPY_MSR_COMMANDBUSYW(1); //Default: busy!
		//Check DMA, RQM and Busy flag!
		switch (FLOPPY.commandbuffer[0]) //What command are we processing?
		{
		case WRITE_DATA: //Write sector?
		case WRITE_DELETED_DATA: //Write deleted sector?
		case FORMAT_TRACK: //Format sector?
		case READ_DATA: //Read sector?
		case READ_TRACK: //Read track?
		case READ_DELETED_DATA: //Read deleted sector?
		case SCAN_EQUAL:
		case SCAN_LOW_OR_EQUAL:
		case SCAN_HIGH_OR_EQUAL:
			FLOPPY_MSR_RQMW(!FLOPPY_useDMA()); //Use no DMA? Then transfer data and set NonDMA! Else, clear non DMA and don't transfer!
			FLOPPY_MSR_NONDMAW(!FLOPPY_useDMA()); //Use no DMA? Then transfer data and set NonDMA! Else, clear non DMA and don't transfer!
			break;
		case READ_ID: //Read ID doesn't transfer data directly!
		case VERIFY: //Verify doesn't transfer data directly!
			FLOPPY_MSR_RQMW(0); //Use no DMA? Then transfer data and set NonDMA! Else, clear non DMA and don't transfer!
			FLOPPY_MSR_NONDMAW(0); //Use no DMA? Then transfer data and set NonDMA! Else, clear non DMA and don't transfer!
			break;
		default: //Unknown command?
			FLOPPY_MSR_RQMW(1); //Use no DMA by default, for safety!
			FLOPPY_MSR_NONDMAW(0); //Use no DMA by default, for safety!
			break; //Don't process!
		}

		//Check data direction!
		switch (FLOPPY.commandbuffer[0]) //Process input/output to/from controller!
		{
		case WRITE_DATA: //Write sector?
		case WRITE_DELETED_DATA: //Write deleted sector?
		case FORMAT_TRACK: //Format sector?
		case SCAN_EQUAL:
		case SCAN_LOW_OR_EQUAL:
		case SCAN_HIGH_OR_EQUAL:
			FLOPPY_MSR_HAVEDATAFORCPUW(0); //We request data from the CPU!
			break;
		case READ_DATA: //Read sector?
		case READ_TRACK: //Read track?
		case READ_DELETED_DATA: //Read deleted sector?
		case READ_ID: //Read ID?
		case VERIFY: //Verify doesn't transfer data directly!
			FLOPPY_MSR_HAVEDATAFORCPUW(1); //We have data for the CPU!
			break;
		default: //Unknown direction?
			FLOPPY_MSR_HAVEDATAFORCPUW(0); //Nothing, say output by default!
			break;
		}
		break;
	case 0xFD: //Give result and lockup?
	case 3: //Result?
		FLOPPY_MSR_COMMANDBUSYW(1); //Default: busy!
		FLOPPY_MSR_RQMW(1); //Data transfer!
		FLOPPY_MSR_HAVEDATAFORCPUW(1); //We have data for the CPU!
		FLOPPY_MSR_NONDMAW(0); //No DMA transfer busy!
		break;
	case 0xFF: //Error?
		FLOPPY_MSR_COMMANDBUSYW(1); //Default: busy!
		FLOPPY_MSR_RQMW(1); //Data transfer!
		FLOPPY_MSR_HAVEDATAFORCPUW(1); //We have data for the CPU!
		FLOPPY_MSR_NONDMAW(0); //No DMA transfer busy!
		break;
	//Locked up?
	case 0xFE: //Locked up?
		FLOPPY_MSR_COMMANDBUSYW(1); //Not busy anymore!
		FLOPPY_MSR_RQMW(0); //No Data transfer!
		FLOPPY_MSR_HAVEDATAFORCPUW(0); //We have no data for the CPU!
		FLOPPY_MSR_NONDMAW(0); //No non-DMA transfer busy!
		break;
	default: //Unknown status?
		break; //Unknown?
	}
	if (FLOPPY.MSR != oldMSR) //MSR changed?
	{
		oldMSR = FLOPPY.MSR; //Update old!
		FLOPPY_LOGD("FLOPPY: MSR changed: %02x", FLOPPY.MSR) //The updated MSR!
	}
}

OPTINLINE void updateFloppyDIR() //Update the floppy DIR!
{
	FLOPPY.DIR = 0; //Init to not changed!
	if (FLOPPY.diskchanged[0] && (FLOPPY_DOR_MOTORCONTROLR&1) && (FLOPPY_DOR_DRIVENUMBERR==0))
	{
		FLOPPY.DIR = 0x80; //Set our bit!
	}
	if (FLOPPY.diskchanged[1] && (FLOPPY_DOR_MOTORCONTROLR&2) && (FLOPPY_DOR_DRIVENUMBERR==1))
	{
		FLOPPY.DIR = 0x80; //Set our bit!
	}
	if (FLOPPY.diskchanged[2] && (FLOPPY_DOR_MOTORCONTROLR&4) && (FLOPPY_DOR_DRIVENUMBERR==2))
	{
		FLOPPY.DIR = 0x80; //Set our bit!
	}
	if (FLOPPY.diskchanged[3] && (FLOPPY_DOR_MOTORCONTROLR&8) && (FLOPPY_DOR_DRIVENUMBERR==3))
	{
		FLOPPY.DIR = 0x80; //Set our bit!
	}
	//Rest of the bits are reserved on an AT!
}

OPTINLINE void clearDiskChanged(byte drive)
{
	//Reset state for all drives!
	FLOPPY.diskchanged[drive] = 0; //Reset!
	FLOPPY.ejectingpending[drive] = 0; //No ejecting pending!
}

OPTINLINE void updateFloppyWriteProtected(byte iswrite, byte drivenumber)
{
	FLOPPY.ST1 = (FLOPPY.ST1&~2); //Default: not write protected!
	if (drivenumber < 2) //Valid drive?
	{
		if ((drivereadonly(drivenumber ? FLOPPY1 : FLOPPY0) || drivewritereadonly(drivenumber ? FLOPPY1 : FLOPPY0)) && iswrite) //Read-only drive and tried to write?
		{
			FLOPPY.ST1 |= 2; //Write protected!
		}
	}
}

byte floppy_getTC(byte drive, byte EOT)
{
	if (!FLOPPY_useDMA()) //non-DMA mode?
	{
		return (FLOPPY.databufferposition == FLOPPY.databuffersize) && (FLOPPY.currentsector[drive] == EOT) && (FLOPPY.currenthead[drive] == FLOPPY.MT); //EOT reached?
	}
	return FLOPPY.TC; //Give DMA TC directly!
}

//result: 0=Finish, 1=Read/write another time, 2=Error out(error handled by floppy_increasesector).
byte floppy_increasesector(byte floppy, byte EOTfield, byte isformatcommand) //Increase the sector number automatically!
{
	byte result = 2; //Default: read/write more
	byte useMT=0;
	useMT = FLOPPY.MT&FLOPPY.MTMask; //Used MT?
	++FLOPPY.currentsector[floppy]; //Next sector!
	result |= (floppy_getTC(floppy,EOTfield)) ? 0 : 1; //No terminal count triggered? Then we read the next sector!
	if (((FLOPPY.currentsector[floppy] > (FLOPPY.geometries[floppy]?FLOPPY.geometries[floppy]->SPT:0)) && (!isformatcommand)) || ((FLOPPY.currentsector[floppy]>EOTfield) && (!(useMT&&FLOPPY_useDMA())))) //Overflow next sector by parameter?
	{
		FLOPPY.currentsector[floppy] = 1; //Reset sector number!

		//Apply Multi Track accordingly!
		if (useMT) //Multi Track used?
		{
			FLOPPY.resultbuffer[4] = FLOPPY.currenthead[floppy]; //The head number of the last sector read by default!
			FLOPPY.currenthead[floppy] = ((FLOPPY.currenthead[floppy]+1)&1); //Toggle the head to 1 or 0!
			FLOPPY.currentphysicalhead[floppy] = ((FLOPPY.currentphysicalhead[floppy] + 1) & 1);
			if (FLOPPY.currenthead[floppy]==0) //Overflown, EOT, switching to head 0? We were the last sector on side 1 with MT!
			{
				result &= ~2; //Finish!
				FLOPPY.resultbuffer[3] = FLOPPY.currentcylinder[floppy]+1; //Report the next cylinder number instead!
				FLOPPY.resultbuffer[4] = FLOPPY.currenthead[floppy]; //The flipped head number of the last sector read!
			}
			else //Same track?
			{
				FLOPPY.resultbuffer[3] = FLOPPY.currentcylinder[floppy]; //The current cylinder number!
			}
		}
		else //Single track mode reached end-of-track?
		{
			result &= ~2; //Finish!
			FLOPPY.resultbuffer[3] = FLOPPY.currentcylinder[floppy]+1; //The next cylinder number!
			FLOPPY.resultbuffer[4] = FLOPPY.currenthead[floppy]; //The current head number!
		}

		updateST3(floppy); //Update ST3 only!
	}
	else //Busy transfer on the current track? Report the current track number for these!
	{
		FLOPPY.resultbuffer[3] = FLOPPY.currentcylinder[floppy]; //The current cylinder number!
		FLOPPY.resultbuffer[4] = FLOPPY.currenthead[floppy]; //The current head number!
	}
	
	FLOPPY_ST0_CURRENTHEADW(FLOPPY.currentphysicalhead[floppy]); //Our idea of the current head!

	if (FLOPPY_useDMA()) //DMA mode determines our triggering?
	{
		if (result&1) //OK to transfer more according to TC(not set)?
		{
			if (result & 2) //Not finished transferring data?
			{
				result = 1; //Transfer more!
			}
			else if (FLOPPY.activecommand[floppy] == FORMAT_TRACK) //Formatting a track? Don't error out!
			{
				result = 0; //Finished!
			}
			else //Error occurred during DMA transfer? Requesting more by DMA than we can handle? EOT is reached but TC isn't set!
			{
				result = 2; //Abort!
				FLOPPY_ST0_INTERRUPTCODEW(1); //Couldn't finish correctly!
				FLOPPY_ST1_ENDOFCYCLINDER(1); //Set EN! Only when EOT is reached but TC isn't set!
			}
		}
		else //Terminal count with/without EOT?
		{
			//Finished transferring! Enter result phase!
			result = 0; //Finished!
		}
	}
	else //Non-DMA?
	{
		result = ((result >> 1) & 1); //Read/Write more or not?
	}

	++FLOPPY.sectorstransferred; //Increase the amount of sectors transferred.

	return result; //Give the result: we've overflown the max sector number!
}

OPTINLINE void FLOPPY_dataReady() //Data transfer ready to transfer!
{
	if (FLOPPY_DRIVEDATA_NDMR(FLOPPY_DOR_DRIVENUMBERR)) //Interrupt for each byte transferred?
	{
		FLOPPY_raiseIRQ(); //Raise the floppy IRQ: We have data to transfer!
	}
}

OPTINLINE void FLOPPY_startData(byte drive) //Start a Data transfer if needed!
{
	FLOPPY.databufferposition = 0; //Start with the new buffer!
	if (FLOPPY.commandstep != 2) //Entering data phase?
	{
		FLOPPY_LOGD("FLOPPY: Start transfer of data (DMA: %u)...",FLOPPY_useDMA())
	}
	switch (FLOPPY.commandbuffer[0]) //What kind of transfer?
	{
	case SCAN_EQUAL: //Equal mismatch?
	case SCAN_LOW_OR_EQUAL: //Low or equal mismatch?
	case SCAN_HIGH_OR_EQUAL: //High or equal mismatch?
		FLOPPY_ST2_SEEKERRORW(0); //No seek error yet!
		FLOPPY_ST2_SEEKEQUALW(1); //Equal by default: we're starting to match until we don't anymore!
		break;
	default:
		break;
	}
	FLOPPY.commandstep = 2; //Move to data phrase!
	if ((FLOPPY.commandbuffer[0]==VERIFY) || (FLOPPY.commandbuffer[0]==READ_ID)) //Verify and Read ID doesn't transfer data directly?
	{
		FLOPPY_supportsrate(drive); //Make sure we have a rate set!
		FLOPPY.DMArate = FLOPPY.DMAratePending; //Start running at the specified speed!		
		if ((FLOPPY.erroringtiming & (1<<FLOPPY_DOR_DRIVENUMBERR))==0) //Not timing?
		{
				floppytimer[drive] = FLOPPY_DMA_TIMEOUT; //Time the timeout for floppy!
				floppytiming |= (1<<drive); //Make sure we're timing on the specified disk channel!
				FLOPPY.DMAPending = 0; //Not pending DMA!
		}
		else //Timing 0.5s?
		{
				floppytimer[drive] = (DOUBLE)(500000000.0/((DOUBLE)FLOPPY.databuffersize)); //Time the timeout for floppy!
				floppytiming |= (1<<FLOPPY_DOR_DRIVENUMBERR); //Make sure we're timing on the specified disk channel!
				FLOPPY.DMAPending = 0; //Not pending DMA!
		}
	}
	else //Normal data transfer?
	{
		if (FLOPPY_useDMA()) //DMA mode?
		{
			FLOPPY.DMAPending = 1; //Pending DMA! Start when available!
			FLOPPY_supportsrate(drive); //Make sure we have a rate set!
			FLOPPY.DMArate = FLOPPY.DMAratePending; //Start running at the specified speed!
			if (FLOPPY.activecommand[drive] == FORMAT_TRACK) //Different rate?
			{
				FLOPPY.DMArate *= (DOUBLE)128.0; //Different rate(see format track command)!
			}
		}
		else //Non-DMA transfer!
		{
			FLOPPY.DMAPending = 0; //Not pending DMA!
		}
		FLOPPY_dataReady(); //We have data to transfer!
	}
}

void floppy_erroringout() //Generic handling of when a floppy errors out!
{
	FLOPPY.DMAPending = 0; //DMA not pending anymore, so stop handling that!
}

void FLOPPY_fillST0(byte drive)
{
	FLOPPY_ST0_UNITSELECTW(drive); //What unit!
	FLOPPY_ST0_CURRENTHEADW(FLOPPY.currentphysicalhead[drive]); //What head!
}

void floppy_common_sectoraccess_nomedia(byte drive)
{
	FLOPPY.commandstep = 0xFE; //Lock up, according to Bochs!

	FLOPPY.ST0 = 0x40 | (FLOPPY.ST0 & 0x30) | drive | (FLOPPY.currentphysicalhead[drive] << 2); //Abnormal termination!
	FLOPPY.ST1 = 1 /* Missing address mark */ | 4 /* No data */;
	FLOPPY.ST2 = 1; //Missing data address mark

	FLOPPY.resultposition = 0;
	FLOPPY.resultbuffer[0] = FLOPPY.ST0; //ST0!
	FLOPPY.resultbuffer[1] = FLOPPY.ST1; //ST1!
	FLOPPY.resultbuffer[2] = FLOPPY.ST2; //ST2!
	FLOPPY.resultbuffer[3] = FLOPPY.currentcylinder[drive]; //The cylinder is set by floppy_increasesector!
	FLOPPY.resultbuffer[4] = FLOPPY.currenthead[drive]; //The head is set by floppy_increasesector!
	FLOPPY.resultbuffer[5] = FLOPPY.currentsector[drive];
	FLOPPY.resultbuffer[6] = (FLOPPY.commandbuffer[0]==FORMAT_TRACK)?FLOPPY.commandbuffer[2]:FLOPPY.commandbuffer[5]; //Sector size from the command buffer!
	floppy_erroringout(); //Erroring out!
}

void floppy_performimplicitseek(byte destinationtrack)
{
	FLOPPY.seekdestination[FLOPPY.commandbuffer[1] & 3] = destinationtrack; //Our destination!
	FLOPPY.seekrel[FLOPPY.commandbuffer[1] & 3] = /*FLOPPY.MT*/ 0; //Seek relative?
	FLOPPY.seekrelup[FLOPPY.commandbuffer[1] & 3] = /*FLOPPY.MFM*/ 0; //Seek relative up(when seeking relatively)
	FLOPPY.ST0 &= ~0x20; //We start to seek!
	floppytime[FLOPPY.commandbuffer[1] & 3] = 0.0;
	floppytiming |= (1<<(FLOPPY.commandbuffer[1] & 3)); //Timing!
	floppytimer[FLOPPY.commandbuffer[1] & 3] = FLOPPY.DriveData[FLOPPY.commandbuffer[1] & 3].steprate; //Step rate!
	FLOPPY_MSR_BUSYINPOSITIONINGMODEW((FLOPPY.commandbuffer[1] & 3),1); //Seeking!
	if (((FLOPPY.commandbuffer[1] & 3)<2) && (((FLOPPY.currentcylinder[FLOPPY.commandbuffer[1] & 3]==FLOPPY.seekdestination[FLOPPY.commandbuffer[1] & 3]) && (FLOPPY.currentcylinder[FLOPPY.commandbuffer[1] & 3] < FLOPPY.geometries[FLOPPY.commandbuffer[1] & 3]->tracks) && (FLOPPY.seekrel[FLOPPY.commandbuffer[1] & 3]==0)) || (FLOPPY.seekrel[FLOPPY.commandbuffer[1] & 3] && (FLOPPY.seekdestination[FLOPPY.commandbuffer[1] & 3]==0)))) //Found and existant?
	{
		updateFloppyGeometries((FLOPPY.commandbuffer[1]&3), FLOPPY.currentphysicalhead[FLOPPY.commandbuffer[1] & 3], FLOPPY.physicalcylinder[FLOPPY.commandbuffer[1] & 3]); //Up
		if (FLOPPY.geometries[(FLOPPY.commandbuffer[1] & 3)])
		{
			FLOPPY.readID_lastsectornumber = FLOPPY.geometries[(FLOPPY.commandbuffer[1] & 3)]->SPT + 1; //Act like the track has changed!
		}
		else
		{
			FLOPPY.readID_lastsectornumber = 0; //Act like the track has changed!
		}
		FLOPPY_finishseek(FLOPPY.commandbuffer[1] & 3,1); //Finish the recalibration automatically(we're eating up the command)!
		FLOPPY_checkfinishtiming(FLOPPY.commandbuffer[1] & 3); //Finish if required!
	}
}

void floppy_readsector_failresult() //Failed implicit seeking?
{
	FLOPPY.floppy_scanningforSectorID = 0; //Not scanning anymore!
	//Plain error reading the sector!
	//ENTER RESULT PHASE
	FLOPPY_LOGD("FLOPPY: Finished transfer of data (%u sector(s)).", FLOPPY.sectorstransferred) //Log the completion of the sectors written!
	FLOPPY.resultposition = 0;
	FLOPPY_fillST0(FLOPPY_DOR_DRIVENUMBERR); //Setup ST0!
	//FLOPPY.resultbuffer[0] = FLOPPY.ST0 = 0x40 | ((FLOPPY.ST0 & 0x3B) | FLOPPY_DOR_DRIVENUMBERR) | ((FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR] & 1) << 2); //Abnormal termination! ST0!
	FLOPPY.resultbuffer[1] = FLOPPY.ST1;
	FLOPPY.resultbuffer[2] = FLOPPY.ST2;
	FLOPPY.resultbuffer[3] = FLOPPY.currentcylinder[FLOPPY_DOR_DRIVENUMBERR]; //Error cylinder!
	FLOPPY.resultbuffer[4] = FLOPPY.currenthead[FLOPPY_DOR_DRIVENUMBERR]; //Error head!
	FLOPPY.resultbuffer[5] = FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR]; //Error sector!
	FLOPPY.resultbuffer[6] = FLOPPY.commandbuffer[2]; //Sector size from the command buffer!
	if ((FLOPPY.erroringtiming & (1<<FLOPPY_DOR_DRIVENUMBERR))==0) //Not timing?
	{
		FLOPPY.commandstep = 3; //Move to result phrase and give the result!
		FLOPPY_raiseIRQ(); //Entering result phase!
	}
	else //Timing 0.5 second?
	{
		FLOPPY.commandstep = 2; //Simulating no transfer!
		floppytime[FLOPPY_DOR_DRIVENUMBERR] = (DOUBLE)0.0; //Start in full delay!
		floppytimer[FLOPPY_DOR_DRIVENUMBERR] = (DOUBLE)(500000000.0); //Time the timeout for floppy!
		floppytiming |= (1<<FLOPPY_DOR_DRIVENUMBERR); //Make sure we're timing on the specified disk channel!
		FLOPPY.DMAPending = 0; //Not pending DMA!
	}
}

void floppy_readsector() //Request a read sector command!
{
	char *DSKImageFile = NULL; //DSK image file to use?
	char* IMDImageFile = NULL; //IMD image file to use?
	SECTORINFORMATIONBLOCK sectorinfo; //Information about the sector!
	TRACKINFORMATIONBLOCK trackinfo;
	IMDIMAGE_SECTORINFO IMD_sectorinfo; //Information about the sector!
	word sectornr;
	byte retryingloop = 0;

	FLOPPY.erroringtiming &= ~(1<<FLOPPY_DOR_DRIVENUMBERR); //Default: not erroring!
	if ((!FLOPPY.geometries[FLOPPY_DOR_DRIVENUMBERR]) || ((FLOPPY_DOR_DRIVENUMBERR<2)?(!is_mounted(FLOPPY_DOR_DRIVENUMBERR?FLOPPY1:FLOPPY0)):1)) //Not inserted or valid?
	{
		FLOPPY_LOGD("FLOPPY: Error: Invalid drive!")
		floppy_common_sectoraccess_nomedia(FLOPPY_DOR_DRIVENUMBERR); //No media!
		return;
		{
			FLOPPY_ST0_UNITSELECTW(FLOPPY_DOR_DRIVENUMBERR); //Current unit!
			FLOPPY_ST0_CURRENTHEADW(FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR] & 1); //Current head!
			FLOPPY_ST0_NOTREADYW(1); //We're not ready yet!
			FLOPPY_ST0_UNITCHECKW(0); //Clear unit check and Interrupt code: we're OK. Also clear SE flag: we're still busy!
			FLOPPY_ST0_INTERRUPTCODEW(0); //Clear unit check and Interrupt code: we're OK. Also clear SE flag: we're still busy!
			FLOPPY_ST0_SEEKENDW(0); //Clear seek end: we're reading a sector!
			FLOPPY.ST1 = 0x01|0x04; //Couldn't find any sector!
			FLOPPY.ST2 = 0x01; //Data address mark not found!
			FLOPPY.erroringtiming |= (1<<FLOPPY_DOR_DRIVENUMBERR); //Erroring!
			goto floppy_errorread; //Error out!
		}
		return;
	}
	if ((FLOPPY.geometries[FLOPPY_DOR_DRIVENUMBERR]->DoubleDensity!=(FLOPPY.MFM&~DENSITY_IGNORE)) && (!(FLOPPY.geometries[FLOPPY_DOR_DRIVENUMBERR]->DoubleDensity&DENSITY_IGNORE) || density_forced) && EMULATE_DENSITY) //Wrong density?
	{
		FLOPPY_LOGD("FLOPPY: Error: Invalid density!")
		FLOPPY.ST1 = 0x01|0x04; //Couldn't find any sector!
		FLOPPY.ST2 = 0x01; //Data address mark not found!
		FLOPPY.erroringtiming |= (1<<FLOPPY_DOR_DRIVENUMBERR); //Erroring!
		goto floppy_errorread; //Error out!
	}

	FLOPPY.databuffersize = translateSectorSize(FLOPPY.commandbuffer[5]); //Sector size into data buffer!
	if (!FLOPPY.commandbuffer[5]) //Special case? Use given info!
	{
		FLOPPY.databuffersize = FLOPPY.commandbuffer[8]; //Use data length!
	}
	FLOPPY.disk_startpos = floppy_LBA(FLOPPY_DOR_DRIVENUMBERR, FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR], FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR], FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR]); //The start position, in sectors!
	FLOPPY_LOGD("FLOPPY: Read sector #%u", FLOPPY.disk_startpos) //We're reading this sector!
	if (FLOPPY.commandstep != 2) { FLOPPY_LOGD("FLOPPY: Sector size: %u bytes", FLOPPY.databuffersize) }
	FLOPPY.disk_startpos *= FLOPPY.databuffersize; //Calculate the start sector!
	if (FLOPPY.commandstep != 2) { FLOPPY_LOGD("FLOPPY: Requesting transfer for %u bytes.", FLOPPY.databuffersize) } //Transfer this many sectors!

	if (!(FLOPPY_DOR_MOTORCONTROLR&(1 << FLOPPY_DOR_DRIVENUMBERR))) //Not motor ON?
	{
		FLOPPY_LOGD("FLOPPY: Error: drive motor not ON!")
		FLOPPY_ST0_UNITSELECTW(FLOPPY_DOR_DRIVENUMBERR); //Current unit!
		FLOPPY_ST0_CURRENTHEADW(FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR] & 1); //Current head!
		FLOPPY_ST0_NOTREADYW(1); //We're not ready yet!
		FLOPPY_ST0_UNITCHECKW(0); //Clear unit check and Interrupt code: we're OK. Also clear SE flag: we're still busy!
		FLOPPY_ST0_INTERRUPTCODEW(0); //Clear unit check and Interrupt code: we're OK. Also clear SE flag: we're still busy!
		FLOPPY_ST0_SEEKENDW(0); //Clear seek end: we're reading a sector!
		FLOPPY.ST1 = 0x01|0x04; //Couldn't find any sector!
		FLOPPY.ST2 = 0x01; //Data address mark not found!
		FLOPPY.erroringtiming |= (1<<FLOPPY_DOR_DRIVENUMBERR); //Erroring!
		goto floppy_errorread; //Error out!
	}

	FLOPPY_ST0_UNITSELECTW(FLOPPY_DOR_DRIVENUMBERR); //Current unit!
	FLOPPY_ST0_CURRENTHEADW(FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR] & 1); //Current head!
	FLOPPY_ST0_NOTREADYW(0); //We're not ready yet!
	FLOPPY_ST0_UNITCHECKW(0); //Clear unit check and Interrupt code: we're OK. Also clear SE flag: we're still busy!
	FLOPPY_ST0_INTERRUPTCODEW(0); //Clear unit check and Interrupt code: we're OK. Also clear SE flag: we're still busy!

	if (!FLOPPY_supportsrate(FLOPPY_DOR_DRIVENUMBERR)) //We don't support the rate?
	{
		FLOPPY.ST1 = 0x01; //Couldn't find any sector!
		FLOPPY.ST2 = 0x01; //Data address mark not found!
		goto floppy_errorread; //Error out!
	}

	if (FLOPPY_IMPLIEDSEEKENABLER) //Implied seek?
	{
		if (FLOPPY.RWRequestedCylinder != FLOPPY.currentcylinder[FLOPPY_DOR_DRIVENUMBERR]) //Wrong idea of a cylinder?
		{
			floppy_performimplicitseek(FLOPPY.RWRequestedCylinder); //Perform an implicit seek now!
			return; //We're seeking now!
		}
	}

	FLOPPY.ST1 = 0x04; //Couldn't find any sector!
	FLOPPY.ST2 = 0x01; //Data address mark not found!
	if (readdata(FLOPPY_DOR_DRIVENUMBERR ? FLOPPY1 : FLOPPY0, &FLOPPY.databuffer, FLOPPY.disk_startpos, FLOPPY.databuffersize)) //Read the data into memory?
	{
		if (FLOPPY.ejectingpending[FLOPPY_DOR_DRIVENUMBERR]) //Disk changed?
		{
			FLOPPY.ST1 = 0x04; //Couldn't find any sector!
			FLOPPY.ST2 = 0x01; //Data address mark not found!
			goto floppy_errorread; //Error out!
		}
		if (FLOPPY.RWRequestedCylinder != FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR]) //Wrong cylinder to access?
		{
			FLOPPY.ST1 = 0x04; //Couldn't find any sector!
			FLOPPY.ST2 = 0x01; //Data address mark not found!
			goto floppy_errorread; //Error out!
		}
		if ((FLOPPY.commandbuffer[7]!=FLOPPY.geometries[FLOPPY_DOR_DRIVENUMBERR]->GAPLength) && (FLOPPY.geometries[FLOPPY_DOR_DRIVENUMBERR]->GAPLength!=GAPLENGTH_IGNORE) && EMULATE_GAPLENGTH) //Wrong GAP length?
		{
			FLOPPY.ST1 = 0x04; //Couldn't find any sector!
			FLOPPY.ST2 = 0x01; //Data address mark not found!
			goto floppy_errorread; //Error out!
		}
		FLOPPY.ST1 &= ~4; //Found!
		FLOPPY.ST2 &= ~1; //Found!
		FLOPPY.readID_lastsectornumber = FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR]; //Last sector accessed!
		FLOPPY_startData(FLOPPY_DOR_DRIVENUMBERR);
	}
	else //DSK or error?
	{
		if ((DSKImageFile = getDSKimage((FLOPPY_DOR_DRIVENUMBERR) ? FLOPPY1 : FLOPPY0)) || (IMDImageFile = getIMDimage((FLOPPY_DOR_DRIVENUMBERR) ? FLOPPY1 : FLOPPY0))) //Are we a DSK/IMD image file?
		{
			if (FLOPPY.ejectingpending[FLOPPY_DOR_DRIVENUMBERR]) //Disk changed?
			{
				FLOPPY.ST1 = 0x04; //Couldn't find any sector!
				FLOPPY.ST2 = 0x01; //Data address mark not found!
				goto floppy_errorread;
			}
			if (DSKImageFile) //DSK image?
			{
				if (readDSKTrackInfo(DSKImageFile, FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR], FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR], &trackinfo) == 0) //Read?
				{
					FLOPPY.ST1 = 0x01; //Couldn't find any sector!
					FLOPPY.ST2 = 0x01; //Data address mark not found!
					goto floppy_errorread;
				}
			}
			else if (IMDImageFile) //IMD image?
			{
				if (readIMDSectorInfo(IMDImageFile, FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR],FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR],0, &IMD_sectorinfo) == 0) //Read track info?
				{
					FLOPPY.ST1 = 0x01; //Couldn't find any sector!
					FLOPPY.ST2 = 0x01; //Data address mark not found!
					goto floppy_errorread;
				}
			}
			FLOPPY.ST1 = 0x04; //Couldn't find any sector!
			FLOPPY.ST2 = 0x01; //Data address mark not found!
		retryread:
			if (FLOPPY.readID_lastsectornumber >= (MAX((IMDImageFile ? IMD_sectorinfo.totalsectors : (word)trackinfo.numberofsectors),1)-1)) //End of range?
			{
				FLOPPY.readID_lastsectornumber = 0; //Start at the beginning!
			}
			else
			{
				++FLOPPY.readID_lastsectornumber; //Next sector number to parse!
			}
			for (sectornr = FLOPPY.readID_lastsectornumber; sectornr < (IMDImageFile?IMD_sectorinfo.totalsectors:(word)trackinfo.numberofsectors); ++sectornr) //Find the sector that's to be requested!
			{
				if (DSKImageFile) //DSK file format?
				{
					if (readDSKSectorInfo(DSKImageFile, FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR], FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR], (byte)sectornr, &sectorinfo)) //Read?
					{
						if (((sectorinfo.SectorID == FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR]) && (FLOPPY.commandbuffer[0]!=READ_TRACK)) && (sectorinfo.side == FLOPPY.currenthead[FLOPPY_DOR_DRIVENUMBERR]) && (sectorinfo.track == FLOPPY.currentcylinder[FLOPPY_DOR_DRIVENUMBERR])) //Found the requested sector as indicated?
						{
							FLOPPY.ST1 &= ~4; //Found!
							FLOPPY.ST2 &= ~1; //Found!
							goto foundsectorIDread; //Found it!
						}
						else if (!FLOPPY.floppy_scanningforSectorID) //Not scanning for the sector ID? Mismatch on the exact sector ID we're searching!
						{
							if ((sectorinfo.track != FLOPPY.currentcylinder[FLOPPY_DOR_DRIVENUMBERR])) //Cylindere mismatch?
							{
								if (IMD_sectorinfo.cylinderID == 0xFF) //Cylinder says, according to IBM Soft Sector Format, Bad Track with a physical error?
								{
									FLOPPY.ST2 |= 0x2; //BC set!
								}
								FLOPPY.ST2 |= 0x10; //WC set!
							}
							FLOPPY.readID_lastsectornumber = sectornr; //We're the last that has been read!
							goto floppy_errorread; //Error out!
						}
					}
				}
				else if (IMDImageFile) //IMD image format?
				{
					if (readIMDSectorInfo(IMDImageFile, FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR], FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR], (byte)sectornr, &IMD_sectorinfo)) //Found some sector information?
					{
						if (((IMD_sectorinfo.sectorID == FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR]) && (FLOPPY.commandbuffer[0] != READ_TRACK)) && (IMD_sectorinfo.headnumber == FLOPPY.currenthead[FLOPPY_DOR_DRIVENUMBERR]) && (IMD_sectorinfo.cylinderID == FLOPPY.currentcylinder[FLOPPY_DOR_DRIVENUMBERR])) //Found the requested sector as indicated?
						{
							if (IMD_sectorinfo.datamark == DATAMARK_INVALID) //Invalid sector?
							{
								//Continue searching!
							}
							else if ((IMD_sectorinfo.datamark == DATAMARK_NORMALDATA) || (IMD_sectorinfo.datamark==DATAMARK_DELETEDDATA) || (IMD_sectorinfo.datamark == DATAMARK_NORMALDATA_DATAERROR) || (IMD_sectorinfo.datamark == DATAMARK_DELETEDDATA_DATAERROR)) //Normal data mark or deleted data mark found?
							{
								FLOPPY.ST1 &= ~4; //Found!
								FLOPPY.ST2 &= ~1; //Found!
								goto foundsectorIDread; //Found it!
							}
							else //Invalid data mark encountered?
							{
								FLOPPY.ST1 &= ~4; //Found!
								FLOPPY.ST2 &= ~1; //Found!
								FLOPPY.ST1 |= 0x20; //CRC error!
								goto floppy_errorread; //Error out!
							}
						}
						else if (!FLOPPY.floppy_scanningforSectorID) //Not scanning for the sector ID? Mismatch on the exact sector ID we're searching!
						{
							if ((IMD_sectorinfo.cylinderID != FLOPPY.currentcylinder[FLOPPY_DOR_DRIVENUMBERR])) //Cylindere mismatch?
							{
								if (IMD_sectorinfo.cylinderID == 0xFF) //Cylinder says, according to IBM Soft Sector Format, Bad Track with a physical error?
								{
									FLOPPY.ST2 |= 0x2; //BC set!
								}
								FLOPPY.ST2 |= 0x10; //WC set!
							}
							FLOPPY.readID_lastsectornumber = sectornr; //We're the last that has been read!
							goto floppy_errorread; //Error out!
						}
					}
				}
			}
			if (DSKImageFile) //DSK image file?
			{
				FLOPPY.readID_lastsectornumber = (trackinfo.numberofsectors - 1); //Last sector reached, go back to the first one!
			}
			else if (IMDImageFile) //IMD image file?
			{
				FLOPPY.readID_lastsectornumber = (IMD_sectorinfo.totalsectors - 1); //Last sector reached, go back to the first one!
			}
			if (retryingloop == 0) //Not retrying the loop?
			{
				retryingloop = 1; //Retrying the loop!
				goto retryread;
			}
			FLOPPY.floppy_scanningforSectorID = 0; //Not scanning anymore!
			if (IMDImageFile) //IMD image?
			{
				if ((FLOPPY.readID_lastsectornumber==0) && (IMD_sectorinfo.datamark == DATAMARK_INVALID)) //Unformatted track?
				{
					FLOPPY.ST1 &= ~4; //Not set!
					FLOPPY.ST1 |= 0x01 /* | 0x80*/; //Couldn't find any sector!
				}
				else
				{
					goto readsector_formattednotfound; //Handle!
				}
			}
			else //Formatted, not found?
			{
			readsector_formattednotfound:
				FLOPPY.ST1 |= 0x04 /* | 0x80*/; //Couldn't find any sector!
			}
			FLOPPY.ST2 |= 0x04; //Sector not found!
			goto floppy_errorread;
		foundsectorIDread: //Found the sector ID for the write!
			if (DSKImageFile) //DSK image file?
			{
				if (readDSKSectorData(DSKImageFile, FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR], FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR], (byte)sectornr, FLOPPY.commandbuffer[5], &FLOPPY.databuffersize)) //Read the data into memory?
				{
					if (readDSKSectorInfo(DSKImageFile, FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR], FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR], (byte)sectornr, &sectorinfo)) //Read the sector information too!
					{
						FLOPPY.ST1 = sectorinfo.ST1; //Load ST1!
						FLOPPY.ST2 = sectorinfo.ST2; //Load ST2!
					}
					FLOPPY.readID_lastsectornumber = (byte)sectornr; //This was the last sector we've read!
					FLOPPY.floppy_scanningforSectorID = 0; //Not scanning anymore!
					FLOPPY_startData(FLOPPY_DOR_DRIVENUMBERR);
					return; //Just execute it!
				}
			}
			else if (IMDImageFile) //IMD image file?
			{
				if (readIMDSector(IMDImageFile, FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR],FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR], (byte)sectornr, FLOPPY.databuffersize,&FLOPPY.databuffer)) //Read the data into memory?
				{
					if (readIMDSectorInfo(IMDImageFile, FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR],FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR], (byte)sectornr, &IMD_sectorinfo)) //Read the sector information too!
					{
						if (((IMD_sectorinfo.datamark == DATAMARK_DELETEDDATA) || (IMD_sectorinfo.datamark == DATAMARK_DELETEDDATA_DATAERROR)) && FLOPPY.Skip) //Skipping deleted data?
						{
							skippingreaddata:
							FLOPPY.readID_lastsectornumber = sectornr; //Next sector to process(Important to advance here, because otherwise it would be an infinite loop)!
							FLOPPY.ST1 |= 0x04; //Couldn't find any sector!
							FLOPPY.ST2 |= 0x01; //Data address mark not found!
							goto retryread; //Skip this data mark!
						}
						if ((((IMD_sectorinfo.datamark == DATAMARK_DELETEDDATA) || (IMD_sectorinfo.datamark == DATAMARK_DELETEDDATA_DATAERROR)) ? 1 : 0) != FLOPPY.datamark) //Different data mark than requested?
						{
							//Invalid data mark being read!
							FLOPPY.ST1 |= 0x40; //DDAM is requested to be read and found, but can't read!
							FLOPPY.readID_lastsectornumber = (byte)sectornr; //This was the last sector we've read!
							//Give the result data, but report it's deleted!
							FLOPPY.floppy_abort = 1; //Abort after giving the result!
							FLOPPY.ST1 &= ~4; //Load ST1!
							FLOPPY.ST2 &= ~1; //Load ST2!
						}
						else //Valid address to read?
						{
							FLOPPY.ST1 &= ~4; //Load ST1!
							FLOPPY.ST2 &= ~1; //Load ST2!
						}
						if ((IMD_sectorinfo.datamark == DATAMARK_DELETEDDATA_DATAERROR) || (IMD_sectorinfo.datamark == DATAMARK_NORMALDATA_DATAERROR)) //Data error?
						{
							FLOPPY.ST1 |= 0x20; //Data error!
							FLOPPY.floppy_abort = 1; //Abort after giving the result!
						}
						FLOPPY.floppy_scanningforSectorID = 0; //Not scanning anymore!
						FLOPPY.readID_lastsectornumber = (byte)sectornr; //This was the last sector we've read!
					}
					else //Skip when we cannot read!
					{
						goto skippingreaddata; //Same case as it being skipped!
					}
					FLOPPY_startData(FLOPPY_DOR_DRIVENUMBERR);
					return; //Just execute it!
				}
				else
				{
					goto skippingreaddata; //Same case as it being skipped!
				}
			}
		}
		else //Not found?
		{
			if (FLOPPY.RWRequestedCylinder != FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR]) //Wrong cylinder to access?
			{
				FLOPPY.ST1 = 0x04; //Couldn't find any sector!
				FLOPPY.ST2 = 0x01; //Data address mark not found!
				goto floppy_errorread; //Error out!
			}
			FLOPPY.ST1 = 0x04; //Couldn't find any sector!
			FLOPPY.ST2 = 0x01; //Data address mark not found!
		}

	floppy_errorread: //Error reading data?
		FLOPPY.floppy_scanningforSectorID = 0; //Not scanning anymore!
		//Plain error reading the sector!
		//ENTER RESULT PHASE
		FLOPPY_LOGD("FLOPPY: Finished transfer of data (%u sector(s)).", FLOPPY.sectorstransferred) //Log the completion of the sectors written!
		FLOPPY.resultposition = 0;
		FLOPPY_fillST0(FLOPPY_DOR_DRIVENUMBERR); //Setup ST0!
		FLOPPY.resultbuffer[0] = FLOPPY.ST0 = 0x40 | ((FLOPPY.ST0 & 0x3B) | FLOPPY_DOR_DRIVENUMBERR) | ((FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR] & 1) << 2); //Abnormal termination! ST0!
		FLOPPY.resultbuffer[1] = FLOPPY.ST1;
		FLOPPY.resultbuffer[2] = FLOPPY.ST2;
		FLOPPY.resultbuffer[3] = FLOPPY.currentcylinder[FLOPPY_DOR_DRIVENUMBERR]; //Error cylinder!
		FLOPPY.resultbuffer[4] = FLOPPY.currenthead[FLOPPY_DOR_DRIVENUMBERR]; //Error head!
		FLOPPY.resultbuffer[5] = FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR]; //Error sector!
		FLOPPY.resultbuffer[6] = FLOPPY.commandbuffer[2]; //Sector size from the command buffer!
		if ((FLOPPY.erroringtiming & (1<<FLOPPY_DOR_DRIVENUMBERR))==0) //Not timing?
		{
			FLOPPY.commandstep = 3; //Move to result phrase and give the result!
			FLOPPY_raiseIRQ(); //Entering result phase!
		}
		else //Timing 0.5 second?
		{
			FLOPPY.commandstep = 2; //Simulating no transfer!
			floppytime[FLOPPY_DOR_DRIVENUMBERR] = (DOUBLE)0.0; //Start in full delay!
			floppytimer[FLOPPY_DOR_DRIVENUMBERR] = (DOUBLE)(500000000.0); //Time the timeout for floppy!
			floppytiming |= (1<<FLOPPY_DOR_DRIVENUMBERR); //Make sure we're timing on the specified disk channel!
			FLOPPY.DMAPending = 0; //Not pending DMA!
		}
	}
}

void FLOPPY_formatsector(byte nodata) //Request a read sector command!
{
	byte* sectorbuffer;
	char *DSKImageFile;
	char *IMDImageFile=NULL;
	SECTORINFORMATIONBLOCK sectorinfo;
	//IMDIMAGE_SECTORINFO IMD_sectorinfo; //Information about the sector!
	++FLOPPY.sectorstransferred; //A sector has been transferred!
	FLOPPY.erroringtiming &= ~(1<<FLOPPY_DOR_DRIVENUMBERR); //Default: not erroring!
	if (!FLOPPY.geometries[FLOPPY_DOR_DRIVENUMBERR] || ((FLOPPY_DOR_DRIVENUMBERR < 2) ? (!is_mounted(FLOPPY_DOR_DRIVENUMBERR ? FLOPPY1 : FLOPPY0)) : 1)) //We don't support the rate or geometry?
	{
		floppy_common_sectoraccess_nomedia(FLOPPY_DOR_DRIVENUMBERR); //No media!
		return;
		format_nomedia:
		FLOPPY_ST0_UNITSELECTW(FLOPPY_DOR_DRIVENUMBERR); //Current unit!
		FLOPPY_ST0_CURRENTHEADW(FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR] & 1); //Current head!
		FLOPPY_ST0_NOTREADYW(1); //We're not ready yet!
		FLOPPY_ST0_UNITCHECKW(0); //Clear unit check and Interrupt code: we're OK. Also clear SE flag: we're still busy!
		//FLOPPY_ST0_SEEKENDW(0); //Clear unit check and Interrupt code: we're OK. Also clear SE flag: we're still busy!
		FLOPPY_ST0_INTERRUPTCODEW(1); //Clear unit check and Interrupt code: we're OK. Also clear SE flag: we're still busy!
		FLOPPY_ST0_SEEKENDW(0); //Clear seek end: we're reading a sector!
		FLOPPY.ST1 = 0x01|0x04; //Couldn't find any sector!
		FLOPPY.ST2 = 0x01; //Data address mark not found!
		FLOPPY.erroringtiming |= (1<<FLOPPY_DOR_DRIVENUMBERR); //Erroring!
		goto floppy_errorformat; //Handle the error!
	}

	if (!FLOPPY_supportsrate(FLOPPY_DOR_DRIVENUMBERR)) //We don't support the rate or geometry?
	{
		goto format_nomedia;
	}

	if ((FLOPPY.geometries[FLOPPY_DOR_DRIVENUMBERR]->DoubleDensity!=(FLOPPY.MFM&~DENSITY_IGNORE)) && (!(FLOPPY.geometries[FLOPPY_DOR_DRIVENUMBERR]->DoubleDensity&DENSITY_IGNORE) || density_forced) && EMULATE_DENSITY) //Wrong density?
	{
		FLOPPY_LOGD("FLOPPY: Error: Invalid density!")
		goto format_nomedia;
	}

	if ((FLOPPY.commandbuffer[5]!=FLOPPY.geometries[FLOPPY_DOR_DRIVENUMBERR]->GAPLength) && (FLOPPY.geometries[FLOPPY_DOR_DRIVENUMBERR]->GAPLength!=GAPLENGTH_IGNORE) && EMULATE_GAPLENGTH) //Wrong GAP length?
	{
		goto format_nomedia;
	}


	if (drivereadonly(FLOPPY_DOR_DRIVENUMBERR ? FLOPPY1 : FLOPPY0)) //Read only drive?
	{
		format_readonlydrive:
		FLOPPY_LOGD("FLOPPY: Finished transfer of data (%u sector(s)).", FLOPPY.sectorstransferred) //Log the completion of the sectors written!
		FLOPPY.resultposition = 0;
		FLOPPY_fillST0(FLOPPY_DOR_DRIVENUMBERR); //Setup ST0!
		FLOPPY.resultbuffer[0] = FLOPPY.ST0 = 0x40 | ((FLOPPY.ST0 & 0x3B) | FLOPPY_DOR_DRIVENUMBERR) | ((FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR] & 1) << 2); //Abnormal termination! ST0!
		FLOPPY.resultbuffer[1] = FLOPPY.ST1 = 0x27; //Drive write-protected! ST1!
		FLOPPY.resultbuffer[2] = FLOPPY.ST2 = 0x31; //ST2!
		FLOPPY.resultbuffer[0] = FLOPPY.ST0;
		FLOPPY.resultbuffer[1] = FLOPPY.ST1;
		FLOPPY.resultbuffer[2] = FLOPPY.ST2;
		FLOPPY.resultbuffer[3] = FLOPPY.currentcylinder[FLOPPY_DOR_DRIVENUMBERR];
		FLOPPY.resultbuffer[4] = FLOPPY.currenthead[FLOPPY_DOR_DRIVENUMBERR];
		FLOPPY.resultbuffer[5] = FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR];
		FLOPPY.resultbuffer[6] = FLOPPY.commandbuffer[2]; //Sector size from the command buffer!
		FLOPPY.commandstep = 3; //Move to result phrase and give the result!
		FLOPPY_raiseIRQ(); //Entering result phase!
		return; //Abort!
	}
	else //Writeable disk?
	{
		//Check disk specific information!
		if (nodata) goto format_havenodata;
		if ((DSKImageFile = getDSKimage((FLOPPY_DOR_DRIVENUMBERR) ? FLOPPY1 : FLOPPY0)) || (IMDImageFile = getIMDimage((FLOPPY_DOR_DRIVENUMBERR) ? FLOPPY1 : FLOPPY0))) //Are we a DSK/IMD image file?
		{
			if (DSKImageFile) //DSK image file?
			{
				if (!readDSKSectorInfo(DSKImageFile, FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR], FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR], (FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR] - 1), &sectorinfo)) //Failed to read sector information block?
				{
					FLOPPY.ST1 = 0x01 | 0x04; //Couldn't find any sector!
					FLOPPY.ST2 = 0x01; //Data address mark not found!
					goto floppy_errorformat;
				}
			}
			else if (IMDImageFile) //IMD image file?
			{
				//Ignore what's on the disk!
			}
			
			if (DSKImageFile) //DSK image?
			{
				FLOPPY.readID_lastsectornumber = (FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR] - 1); //This was the last sector we've read!
			}
			else if (IMDImageFile) //IMD image?
			{
				FLOPPY.readID_lastsectornumber = (FLOPPY.currentformatsector[FLOPPY_DOR_DRIVENUMBERR]); //This is the last sector we've read!
			}

			if (DSKImageFile) //DSK image?
			{
				if ((sectorinfo.track != FLOPPY.databuffer[0]) ||
					(sectorinfo.side != FLOPPY.databuffer[1]) ||
					(sectorinfo.SectorID != FLOPPY.databuffer[2])) //Sector ID mismatch?
				{
					FLOPPY.ST1 = 0x01 | 0x04; //Couldn't find any sector!
					FLOPPY.ST2 = 0x01; //Data address mark not found!
					goto floppy_errorformat;
				}
			}
			else if (IMDImageFile) //IMD image?
			{
				//Allow any custom amount of formatting information to be used with this format!
				sectorbuffer = &FLOPPY.formatbuffer[(FLOPPY.currentformatsector[FLOPPY_DOR_DRIVENUMBERR])<<2]; //The location of the packet containing the sector to format!
				//Construct the format information buffer for the selected sector!
				sectorbuffer[0] = FLOPPY.databuffer[0];//Cylinder
				sectorbuffer[1] = FLOPPY.databuffer[1];//Head
				sectorbuffer[2] = FLOPPY.databuffer[2];//Sector
				sectorbuffer[3] = FLOPPY.databuffer[3];//Size(127*(2^n))
			}

			//Verify sector size!
			if (DSKImageFile) //DSK image?
			{
				if (sectorinfo.SectorSize != FLOPPY.databuffer[3]) //Invalid sector size?
				{
					FLOPPY.ST1 = 0x01 | 0x04; //Couldn't find any sector!
					FLOPPY.ST2 = 0x01; //Data address mark not found!
					goto floppy_errorformat;
				}
				if (FLOPPY.commandbuffer[2] != FLOPPY.databuffer[3]) //Not supported for this format?
				{
					FLOPPY.ST1 = 0x01 | 0x04; //Couldn't find any sector!
					FLOPPY.ST2 = 0x01; //Data address mark not found!
					goto floppy_errorformat;
				}
			}

			//Fill the sector buffer and write it!
			if (DSKImageFile) //DSK image?
			{
				if (drivereadonly(FLOPPY_DOR_DRIVENUMBERR ? FLOPPY1 : FLOPPY0)) //Read-only?
				{
					updateFloppyWriteProtected(1, FLOPPY_DOR_DRIVENUMBERR); //Tried to write!
					FLOPPY.ST1 = 0x01 | 0x04; //Couldn't find any sector!
					FLOPPY.ST2 = 0x01; //Data address mark not found!
					goto format_readonlydrive; //Read-only drive formatting!
				}
				if (FLOPPY.ejectingpending[FLOPPY_DOR_DRIVENUMBERR]) //Disk changed?
				{
					FLOPPY.ST1 = 0x01 | 0x04; //Couldn't find any sector!
					FLOPPY.ST2 = 0x01; //Data address mark not found!
					goto floppy_errorformat; //Error out!
				}
				memset(&FLOPPY.databuffer, FLOPPY.commandbuffer[5], MIN(((size_t)1 << sectorinfo.SectorSize), sizeof(FLOPPY.databuffer))); //Clear our buffer with the fill byte!
				if (!writeDSKSectorData(DSKImageFile, FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR], FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR], (FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR] - 1), sectorinfo.SectorSize, &FLOPPY.databuffer)) //Failed writing the formatted sector?
				{
					updateFloppyWriteProtected(1, FLOPPY_DOR_DRIVENUMBERR); //Tried to write!
					FLOPPY.ST1 = 0x01 | 0x04; //Couldn't find any sector!
					FLOPPY.ST2 = 0x01; //Data address mark not found!
					goto floppy_errorformat;
				}
			}
			else if (IMDImageFile) //IMD image?
			{
				if (drivereadonly(FLOPPY_DOR_DRIVENUMBERR ? FLOPPY1 : FLOPPY0)) //Read-only?
				{
					updateFloppyWriteProtected(1, FLOPPY_DOR_DRIVENUMBERR); //Tried to write!
					FLOPPY.ST1 = 0x01 | 0x04; //Couldn't find any sector!
					FLOPPY.ST2 = 0x01; //Data address mark not found!
					goto format_readonlydrive; //Read-only drive formatting!
				}
				//Formatting is performed at the end of the track, having collected all sector information!
			}
			updateFloppyWriteProtected(1, FLOPPY_DOR_DRIVENUMBERR); //Tried to write!
		}
		else //Are we a normal image file?
		{
			if (FLOPPY.RWRequestedCylinder != FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR]) //Wrong cylinder to access?
			{
				FLOPPY.ST1 = 0x01 | 0x04; //Couldn't find any sector!
				FLOPPY.ST2 = 0x01; //Data address mark not found!
				goto floppy_errorformat; //Error out!
			}
			FLOPPY.readID_lastsectornumber = FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR]; //This was the last sector we've accessed!
			if (FLOPPY.databuffer[0] != FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR]) //Not current track?
			{
				goto format_nomedia;
			floppy_errorformat:
				FLOPPY_LOGD("FLOPPY: Finished transfer of data (%u sector(s)).", FLOPPY.sectorstransferred) //Log the completion of the sectors written!
				FLOPPY.resultposition = 0;
				FLOPPY_fillST0(FLOPPY_DOR_DRIVENUMBERR); //Setup ST0!
				FLOPPY.resultbuffer[0] = FLOPPY.ST0 = 0x40 | ((FLOPPY.ST0 & 0x3B) | FLOPPY_DOR_DRIVENUMBERR) | 0x10 | ((FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR] & 1) << 2); //Abnormal termination! ST0!
				FLOPPY.resultbuffer[1] = FLOPPY.ST1 = 0; //Drive write-protected! ST1!
				FLOPPY.resultbuffer[2] = FLOPPY.ST2 = 0; //ST2!
				FLOPPY.resultbuffer[0] = FLOPPY.ST0;
				FLOPPY.resultbuffer[1] = FLOPPY.ST1;
				FLOPPY.resultbuffer[2] = FLOPPY.ST2;
				FLOPPY.resultbuffer[3] = FLOPPY.currentcylinder[FLOPPY_DOR_DRIVENUMBERR];
				FLOPPY.resultbuffer[4] = FLOPPY.currenthead[FLOPPY_DOR_DRIVENUMBERR];
				FLOPPY.resultbuffer[5] = FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR];
				FLOPPY.resultbuffer[6] = FLOPPY.commandbuffer[2]; //Sector size from the command buffer!
				if ((FLOPPY.erroringtiming & (1<<FLOPPY_DOR_DRIVENUMBERR))==0) //Not timing?
				{
					FLOPPY.commandstep = 3; //Move to result phrase and give the result!
					FLOPPY_raiseIRQ(); //Entering result phase!
				}
				else //Timing 0.5 second!
				{
					FLOPPY.commandstep = 2; //Simulating no transfer!
					floppytime[FLOPPY_DOR_DRIVENUMBERR] = (DOUBLE)0.0; //Start in full delay!
					floppytimer[FLOPPY_DOR_DRIVENUMBERR] = (DOUBLE)(500000000.0); //Time the timeout for floppy!
					floppytiming |= (1<<FLOPPY_DOR_DRIVENUMBERR); //Make sure we're timing on the specified disk channel!
					FLOPPY.DMAPending = 0; //Not pending DMA!
				}
				return;
			}
			if (FLOPPY.databuffer[1] != FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR]) //Not current head?
			{
				FLOPPY.ST1 = 0x01 | 0x04; //Couldn't find any sector!
				FLOPPY.ST2 = 0x01; //Data address mark not found!
				goto floppy_errorformat;
			}
			if (FLOPPY.databuffer[2] != FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR]) //Not current sector?
			{
				FLOPPY.ST1 = 0x01 | 0x04; //Couldn't find any sector!
				FLOPPY.ST2 = 0x01; //Data address mark not found!
				goto floppy_errorformat;
			}

			if (FLOPPY.databuffer[3] != 0x2) //Not 512 bytes/sector?
			{
				FLOPPY.ST1 = 0x01 | 0x04; //Couldn't find any sector!
				FLOPPY.ST2 = 0x01; //Data address mark not found!
				goto floppy_errorformat;
			}
			if (FLOPPY.commandbuffer[2] != 0x2) //Not supported for this format?
			{
				FLOPPY.ST1 = 0x01 | 0x04; //Couldn't find any sector!
				FLOPPY.ST2 = 0x01; //Data address mark not found!
				goto floppy_errorformat;
			}
			memset(&FLOPPY.databuffer, FLOPPY.commandbuffer[5], 512); //Clear our buffer with the fill byte!
			if (FLOPPY.ejectingpending[FLOPPY_DOR_DRIVENUMBERR]) //Disk changed?
			{
				FLOPPY.ST1 = 0x01 | 0x04; //Couldn't find any sector!
				FLOPPY.ST2 = 0x01; //Data address mark not found!
				goto floppy_errorformat; //Error out!
			}
			if (drivereadonly(FLOPPY_DOR_DRIVENUMBERR ? FLOPPY1 : FLOPPY0)) //Read-only?
			{
				updateFloppyWriteProtected(1, FLOPPY_DOR_DRIVENUMBERR); //Tried to write!
				FLOPPY.ST1 = 0x01 | 0x04; //Couldn't find any sector!
				FLOPPY.ST2 = 0x01; //Data address mark not found!
				goto format_readonlydrive; //Read-only drive formatting!
			}
			if (!writedata(FLOPPY_DOR_DRIVENUMBERR ? FLOPPY1 : FLOPPY0, &FLOPPY.databuffer, (floppy_LBA(FLOPPY_DOR_DRIVENUMBERR, FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR], FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR], FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR])<<9),512)) //Failed writing the formatted sector?
			{
				updateFloppyWriteProtected(1, FLOPPY_DOR_DRIVENUMBERR); //Tried to write!
				if (drivewritereadonly(FLOPPY_DOR_DRIVENUMBERR ? FLOPPY1 : FLOPPY0)) //Read-only after all?
				{
					goto format_readonlydrive; //Read-only drive formatting!
				}
				FLOPPY.ST1 = 0x01 | 0x04; //Couldn't find any sector!
				FLOPPY.ST2 = 0x01; //Data address mark not found!
				goto floppy_errorformat;
			}
		}
	}

	format_havenodata:
	FLOPPY_ST0_CURRENTHEADW(FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR]); //Our idea of the current head!

	if (!nodata) //Not no data?
	{
		++FLOPPY.currentformatsector[FLOPPY_DOR_DRIVENUMBERR]; //Handled this raw sector for the formats requiring it!
	}
	switch (floppy_increasesector(FLOPPY_DOR_DRIVENUMBERR, FLOPPY.commandbuffer[3],1))
	{
	case 1: //OK?
		//More to be written?
		//Start transfer of the next sector!
		FLOPPY_startData(FLOPPY_DOR_DRIVENUMBERR);
		return; //Continue onwards!
	case 2: //Error during transfer?
		//Let the floppy_increasesector determine the error!
		break;
	case 0: //OK? Finished correctly?
		if (IMDImageFile) //IMD image?
		{
			if (FLOPPY.ejectingpending[FLOPPY_DOR_DRIVENUMBERR]) //Disk changed?
			{
				FLOPPY.ST1 = 0x01 | 0x04; //Couldn't find any sector!
				FLOPPY.ST2 = 0x01; //Data address mark not found!
				goto floppy_errorformat; //Error out!
			}
			if (drivereadonly(FLOPPY_DOR_DRIVENUMBERR ? FLOPPY1 : FLOPPY0)) //Read-only?
			{
				updateFloppyWriteProtected(1, FLOPPY_DOR_DRIVENUMBERR); //Tried to write!
				FLOPPY.ST1 = 0x01 | 0x04; //Couldn't find any sector!
				FLOPPY.ST2 = 0x01; //Data address mark not found!
				goto format_readonlydrive; //Read-only drive formatting!
			}
			if (formatIMDTrack(IMDImageFile, FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR], FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR], (FLOPPY.MFM ? FORMATTING_MFM : FORMATTING_FM), ((FLOPPY_CCR_RATER == 0) ? FORMAT_SPEED_500 : ((FLOPPY_CCR_RATER == 1) ? FORMAT_SPEED_300 : ((FLOPPY_CCR_RATER == 2) ? FORMAT_SPEED_250 : FORMAT_SPEED_1M))), FLOPPY.commandbuffer[5], FLOPPY.commandbuffer[2], FLOPPY.currentformatsector[FLOPPY_DOR_DRIVENUMBERR], &FLOPPY.formatbuffer[0]) == 0) //Error formatting the track in IMD formatting mode?
			{
				updateFloppyWriteProtected(1, FLOPPY_DOR_DRIVENUMBERR); //Tried to write!
				FLOPPY.ST1 = 0x01 | 0x04; //Couldn't find any sector!
				FLOPPY.ST2 = 0x01; //Data address mark not found!
				goto floppy_errorformat;
			}
		}
		FLOPPY.ST1 = 0; //No errors!
		FLOPPY.ST2 = 0; //No errors!
	default: //Unknown?
		FLOPPY.ST1 = 0; //OK!
		FLOPPY.ST2 = 0; //OK!
		FLOPPY_ST0_INTERRUPTCODEW(0); //Normal termination!
		FLOPPY_ST0_NOTREADYW(0); //We're ready!
		break;
	}

	//Enter result phase!
	FLOPPY.resultposition = 0; //Reset result position!
	FLOPPY.commandstep = 3; //Enter the result phase!
	FLOPPY_fillST0(FLOPPY_DOR_DRIVENUMBERR); //Setup ST0!
	FLOPPY.resultbuffer[0] = FLOPPY.ST0;
	FLOPPY.resultbuffer[1] = FLOPPY.ST1;
	FLOPPY.resultbuffer[2] = FLOPPY.ST2;
	//The cylinder is set by floppy_increasesector!
	//The head is set by floppy_increasesector!
	FLOPPY.resultbuffer[5] = FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR];
	FLOPPY.resultbuffer[6] = FLOPPY.commandbuffer[2]; //Sector size from the command buffer!
	FLOPPY_raiseIRQ(); //Raise the IRQ!
	return; //Finished!
}

void floppy_writesector_failresult() //Failed implicit seeking?
{
	//FLOPPY_ST0_INTERRUPTCODEW(1); //Clear unit check and Interrupt code: we're OK. Also clear SE flag: we're still busy!
	FLOPPY_LOGD("FLOPPY: Finished transfer of data (%u sector(s)).", FLOPPY.sectorstransferred) //Log the completion of the sectors written!
	FLOPPY.resultposition = 0;
	FLOPPY_fillST0(FLOPPY_DOR_DRIVENUMBERR); //Setup ST0!
	FLOPPY.resultbuffer[0] = FLOPPY.ST0; //ST0!
	FLOPPY.resultbuffer[1] = FLOPPY.ST1; //ST1!
	FLOPPY.resultbuffer[2] = FLOPPY.ST2; //ST2!
	//The cylinder is set by floppy_increasesector!
	//The head is set by floppy_increasesector!
	FLOPPY.resultbuffer[5] = FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR];
	FLOPPY.resultbuffer[6] = FLOPPY.commandbuffer[5]; //Sector size from the command buffer!
	FLOPPY.erroringtiming |= (1<<FLOPPY_DOR_DRIVENUMBERR); //Erroring!
	if ((FLOPPY.erroringtiming & (1<<FLOPPY_DOR_DRIVENUMBERR))==0) //Not timing?
	{
		FLOPPY.commandstep = 3; //Move to result phrase and give the result!
		FLOPPY_raiseIRQ(); //Entering result phase!
	}
	else //Timing 0.5s!
	{
		FLOPPY.commandstep = 2; //Simulating no transfer!
		floppytime[FLOPPY_DOR_DRIVENUMBERR] = (DOUBLE)0.0; //Start in full delay!
		floppytimer[FLOPPY_DOR_DRIVENUMBERR] = (DOUBLE)(500000000.0); //Time the timeout for floppy!
		floppytiming |= (1<<FLOPPY_DOR_DRIVENUMBERR); //Make sure we're timing on the specified disk channel!
		FLOPPY.DMAPending = 0; //Not pending DMA!
	}
}


void floppy_writesector() //Request a write sector command!
{
	FLOPPY.databuffersize = translateSectorSize(FLOPPY.commandbuffer[5]); //Sector size into data buffer!
	if (!FLOPPY.commandbuffer[5]) //Special case? Use given info!
	{
		FLOPPY.databuffersize = FLOPPY.commandbuffer[8]; //Use data length!
	}
	FLOPPY.disk_startpos = floppy_LBA(FLOPPY_DOR_DRIVENUMBERR, FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR], FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR], FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR]); //The start position, in sectors!
	if (FLOPPY.commandstep != 2) { FLOPPY_LOGD("FLOPPY: Write sector #%u", FLOPPY.disk_startpos) } //We're reading this sector!
	if (FLOPPY.commandstep != 2) { FLOPPY_LOGD("FLOPPY: Sector size: %u bytes", FLOPPY.databuffersize) }
	FLOPPY.disk_startpos *= FLOPPY.databuffersize; //Calculate the start sector!
	if (FLOPPY.commandstep != 2) { FLOPPY_LOGD("FLOPPY: Requesting transfer for %u bytes.", FLOPPY.databuffersize) } //Transfer this many sectors!

	if (FLOPPY.commandstep != 2) { FLOPPY_LOGD("FLOPPY: Write sector: CHS=%u,%u,%u; Params: %02X%02X%02x%02x%02x%02x%02x%02x", FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR], FLOPPY.currenthead[FLOPPY_DOR_DRIVENUMBERR], FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR], FLOPPY.commandbuffer[1], FLOPPY.commandbuffer[2], FLOPPY.commandbuffer[3], FLOPPY.commandbuffer[4], FLOPPY.commandbuffer[5], FLOPPY.commandbuffer[6], FLOPPY.commandbuffer[7], FLOPPY.commandbuffer[8]) } //Log our request!

	FLOPPY.erroringtiming &= ~(1<<FLOPPY_DOR_DRIVENUMBERR); //Default: not erroring!
	if (!(FLOPPY_DOR_MOTORCONTROLR&(1 << FLOPPY_DOR_DRIVENUMBERR))) //Not motor ON?
	{
		FLOPPY_LOGD("FLOPPY: Error: drive motor not ON!")
		FLOPPY_ST0_UNITSELECTW(FLOPPY_DOR_DRIVENUMBERR); //Current unit!
		FLOPPY_ST0_CURRENTHEADW(FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR] & 1); //Current head!
		FLOPPY_ST0_NOTREADYW(1); //We're not ready yet!
		FLOPPY_ST0_UNITCHECKW(0); //Clear unit check and Interrupt code: we're OK. Also clear SE flag: we're still busy!
		FLOPPY_ST0_SEEKENDW(0); //Clear seek end: we're reading a sector!
		FLOPPY.ST1 = 0x01; //Couldn't find any sector!
		FLOPPY.ST2 = 0x01; //Data address mark not found!
		floppy_errorwritesector: //Error out!
		FLOPPY_ST0_INTERRUPTCODEW(1); //Clear unit check and Interrupt code: we're OK. Also clear SE flag: we're still busy!
		FLOPPY_LOGD("FLOPPY: Finished transfer of data (%u sector(s)).", FLOPPY.sectorstransferred) //Log the completion of the sectors written!
		FLOPPY.resultposition = 0;
		FLOPPY_fillST0(FLOPPY_DOR_DRIVENUMBERR); //Setup ST0!
		FLOPPY.resultbuffer[0] = FLOPPY.ST0; //ST0!
		FLOPPY.resultbuffer[1] = FLOPPY.ST1; //ST1!
		FLOPPY.resultbuffer[2] = FLOPPY.ST2; //ST2!
		//The cylinder is set by floppy_increasesector!
		//The head is set by floppy_increasesector!
		FLOPPY.resultbuffer[5] = FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR];
		FLOPPY.resultbuffer[6] = FLOPPY.commandbuffer[5]; //Sector size from the command buffer!
		FLOPPY.erroringtiming |= (1<<FLOPPY_DOR_DRIVENUMBERR); //Erroring!
		if ((FLOPPY.erroringtiming & (1<<FLOPPY_DOR_DRIVENUMBERR))==0) //Not timing?
		{
			FLOPPY.commandstep = 3; //Move to result phrase and give the result!
			FLOPPY_raiseIRQ(); //Entering result phase!
		}
		else //Timing 0.5s!
		{
			FLOPPY.commandstep = 2; //Simulating no transfer!
			floppytime[FLOPPY_DOR_DRIVENUMBERR] = (DOUBLE)0.0; //Start in full delay!
			floppytimer[FLOPPY_DOR_DRIVENUMBERR] = (DOUBLE)(500000000.0); //Time the timeout for floppy!
			floppytiming |= (1<<FLOPPY_DOR_DRIVENUMBERR); //Make sure we're timing on the specified disk channel!
			FLOPPY.DMAPending = 0; //Not pending DMA!
		}
		return; //Abort!
	}

	if (!FLOPPY.geometries[FLOPPY_DOR_DRIVENUMBERR] || ((FLOPPY_DOR_DRIVENUMBERR < 2) ? (!is_mounted(FLOPPY_DOR_DRIVENUMBERR ? FLOPPY1 : FLOPPY0)) : 1)) //Not properly mounted?
	{
		floppy_common_sectoraccess_nomedia(FLOPPY_DOR_DRIVENUMBERR); //No media!
		return;
		FLOPPY_ST0_UNITSELECTW(FLOPPY_DOR_DRIVENUMBERR); //Current unit!
		FLOPPY_ST0_CURRENTHEADW(FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR] & 1); //Current head!
		FLOPPY_ST0_NOTREADYW(1); //We're not ready yet!
		FLOPPY_ST0_UNITCHECKW(0); //Clear unit check and Interrupt code: we're OK. Also clear SE flag: we're still busy!
		FLOPPY_ST0_INTERRUPTCODEW(0); //Clear unit check and Interrupt code: we're OK. Also clear SE flag: we're still busy!
		FLOPPY_ST0_SEEKENDW(0); //Clear seek end: we're reading a sector!
		FLOPPY.ST1 = 0x01 | 0x04; //Couldn't find any sector!
		FLOPPY.ST2 = 0x01; //Data address mark not found!
		FLOPPY.erroringtiming |= (1<<FLOPPY_DOR_DRIVENUMBERR); //Erroring!
		goto floppy_errorwritesector; //Handle the error!
	}

	if (!FLOPPY_supportsrate(FLOPPY_DOR_DRIVENUMBERR)) //We don't support the rate or geometry?
	{
		FLOPPY.ST1 = 0x01; //Couldn't find any sector!
		FLOPPY.ST2 = 0x01; //Data address mark not found!
		goto floppy_errorwritesector; //Error out!
	}

	FLOPPY_ST0_UNITSELECTW(FLOPPY_DOR_DRIVENUMBERR); //Current unit!
	FLOPPY_ST0_CURRENTHEADW(FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR] & 1); //Current head!
	FLOPPY_ST0_NOTREADYW(0); //We're not ready yet!
	FLOPPY_ST0_UNITCHECKW(0); //Clear unit check and Interrupt code: we're OK. Also clear SE flag: we're still busy!
	FLOPPY_ST0_INTERRUPTCODEW(0); //Clear unit check and Interrupt code: we're OK. Also clear SE flag: we're still busy!

	if (!(FLOPPY_DOR_MOTORCONTROLR&(1 << FLOPPY_DOR_DRIVENUMBERR))) //Not motor ON?
	{
		FLOPPY.ST1 = 0x01|0x04; //Couldn't find any sector!
		FLOPPY.ST2 = 0x01; //Data address mark not found!
		FLOPPY.erroringtiming |= (1<<FLOPPY_DOR_DRIVENUMBERR); //Erroring!
		goto floppy_errorwritesector; //Error out!
	}

	if (FLOPPY.ejectingpending[FLOPPY_DOR_DRIVENUMBERR]) //Disk changed?
	{
		FLOPPY.ST1 = 0x04; //Couldn't find any sector!
		FLOPPY.ST2 = 0x01; //Data address mark not found!
		goto floppy_errorwritesector; //Error out!
	}

	if (FLOPPY_IMPLIEDSEEKENABLER) //Implied seek?
	{
		if (FLOPPY.RWRequestedCylinder != FLOPPY.currentcylinder[FLOPPY_DOR_DRIVENUMBERR]) //Wrong idea of a cylinder?
		{
			floppy_performimplicitseek(FLOPPY.RWRequestedCylinder); //Perform an implicit seek now!
			return; //We're seeking now!
		}
	}

	FLOPPY_startData(FLOPPY_DOR_DRIVENUMBERR); //Start the DMA transfer if needed!
}

void floppy_executeWriteData()
{
	word sectornr;
	char *DSKImageFile = NULL; //DSK image file to use?
	char *IMDImageFile = NULL; //DSK image file to use?
	SECTORINFORMATIONBLOCK sectorinfo;
	IMDIMAGE_SECTORINFO IMD_sectorinfo; //Information about the sector!
	TRACKINFORMATIONBLOCK trackinfo;
	byte DSKIMDsuccess=0;

	FLOPPY.erroringtiming &= ~(1<<FLOPPY_DOR_DRIVENUMBERR); //Default: not erroring!
	if (!FLOPPY.geometries[FLOPPY_DOR_DRIVENUMBERR] || ((FLOPPY_DOR_DRIVENUMBERR < 2) ? (!is_mounted(FLOPPY_DOR_DRIVENUMBERR ? FLOPPY1 : FLOPPY0)) : 1)) //Not mounted?
	{
		floppy_common_sectoraccess_nomedia(FLOPPY_DOR_DRIVENUMBERR); //No media!
		return;
		FLOPPY_ST0_UNITSELECTW(FLOPPY_DOR_DRIVENUMBERR); //Current unit!
		FLOPPY_ST0_CURRENTHEADW(FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR] & 1); //Current head!
		FLOPPY_ST0_NOTREADYW(1); //We're not ready yet!
		FLOPPY_ST0_UNITCHECKW(0); //Clear unit check and Interrupt code: we're OK. Also clear SE flag: we're still busy!
		FLOPPY_ST0_INTERRUPTCODEW(0); //Clear unit check and Interrupt code: we're OK. Also clear SE flag: we're still busy!
		FLOPPY_ST0_SEEKENDW(0); //Clear seek end: we're reading a sector!
		FLOPPY.ST1 = 0x01 | 0x04; //Couldn't find any sector!
		FLOPPY.ST2 = 0x01; //Data address mark not found!
		FLOPPY.erroringtiming |= (1<<FLOPPY_DOR_DRIVENUMBERR); //Erroring!
		goto floppy_errorwrite; //Error out!
	}

	if (!FLOPPY_supportsrate(FLOPPY_DOR_DRIVENUMBERR)) //We don't support the rate?
	{
		//Normal result?
		FLOPPY_LOGD("FLOPPY: Error: Invalid disk rate/geometry!")
		FLOPPY.ST1 = 0x01; //Couldn't find any sector!
		FLOPPY.ST2 = 0x01; //Data address mark not found!
		goto floppy_errorwrite; //Error out!
	}
	if ((FLOPPY.geometries[FLOPPY_DOR_DRIVENUMBERR]->DoubleDensity!=(FLOPPY.MFM&~DENSITY_IGNORE)) && (!(FLOPPY.geometries[FLOPPY_DOR_DRIVENUMBERR]->DoubleDensity&DENSITY_IGNORE) || density_forced) && EMULATE_DENSITY) //Wrong density?
	{
		FLOPPY_LOGD("FLOPPY: Error: Invalid density!")
		FLOPPY.ST1 = 0x01; //Couldn't find any sector!
		FLOPPY.ST2 = 0x01; //Data address mark not found!
		goto floppy_errorwrite; //Error out!
	}

	if (FLOPPY.ejectingpending[FLOPPY_DOR_DRIVENUMBERR]) //Disk changed?
	{
		FLOPPY.ST1 = 0x04; //Couldn't find any sector!
		FLOPPY.ST2 = 0x01; //Data address mark not found!
		goto floppy_errorwrite; //Error out!
	}
	if (writedata(FLOPPY_DOR_DRIVENUMBERR ? FLOPPY1 : FLOPPY0, &FLOPPY.databuffer, FLOPPY.disk_startpos, FLOPPY.databuffersize)) //Written the data to disk?
	{
		if (FLOPPY.RWRequestedCylinder != FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR]) //Wrong cylinder to access?
		{
			FLOPPY.ST1 = 0x04; //Couldn't find any sector!
			FLOPPY.ST2 = 0x01; //Data address mark not found!
			goto floppy_errorwrite; //Error out!
		}
		updateFloppyWriteProtected(1, FLOPPY_DOR_DRIVENUMBERR); //Tried to write!
		FLOPPY.readID_lastsectornumber = (FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR]); //Last accessed sector!
		switch (floppy_increasesector(FLOPPY_DOR_DRIVENUMBERR, FLOPPY.commandbuffer[6],0)) //Goto next sector!
		{
		case 1: //OK?
			//More to be written?
			floppy_writesector(); //Write another sector!
			return; //Finished!
		case 2: //Error during transfer?
			//Let the floppy_increasesector determine the error!
			break;
		case 0: //OK?
		default: //Unknown?
			FLOPPY.ST1 = 0; //OK!
			FLOPPY.ST2 = 0; //OK!
			FLOPPY_ST0_INTERRUPTCODEW(0); //Normal termination!
			FLOPPY_ST0_NOTREADYW(0); //We're ready!
			break;
		}
		FLOPPY_LOGD("FLOPPY: Finished transfer of data (%u sector(s)).", FLOPPY.sectorstransferred) //Log the completion of the sectors written!
		FLOPPY.resultposition = 0;
		FLOPPY_fillST0(FLOPPY_DOR_DRIVENUMBERR); //Setup ST0!
		FLOPPY.resultbuffer[0] = FLOPPY.ST0; //ST0!
		FLOPPY.resultbuffer[1] = FLOPPY.ST1; //ST1!
		FLOPPY.resultbuffer[2] = FLOPPY.ST2; //ST2!
		//The cylinder is set by floppy_increasesector!
		//The head is set by floppy_increasesector!
		FLOPPY.resultbuffer[5] = FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR];
		FLOPPY.resultbuffer[6] = FLOPPY.commandbuffer[5]; //Sector size from the command buffer!
		FLOPPY.commandstep = 3; //Move to result phrase and give the result!
		FLOPPY_raiseIRQ(); //Entering result phase!
	}
	else
	{
		if (drivereadonly(FLOPPY_DOR_DRIVENUMBERR ? FLOPPY1 : FLOPPY0) || drivewritereadonly(FLOPPY_DOR_DRIVENUMBERR ? FLOPPY1 : FLOPPY0)) //Read-only drive?
		{
			updateFloppyWriteProtected(1, FLOPPY_DOR_DRIVENUMBERR); //Tried to write!
			if (readdata(FLOPPY_DOR_DRIVENUMBERR ? FLOPPY1 : FLOPPY0, &FLOPPY.databuffer, FLOPPY.disk_startpos, FLOPPY.databuffersize)) //Readable the data from disk?
			{
				FLOPPY.readID_lastsectornumber = FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR]; //Last sector accessed!
			}
			FLOPPY_LOGD("FLOPPY: Finished transfer of data (readonly).") //Log the completion of the sectors written!
			FLOPPY.resultposition = 0;
			FLOPPY.resultbuffer[0] = FLOPPY.ST0 = 0x40|((FLOPPY.ST0 & 0x3B) | FLOPPY_DOR_DRIVENUMBERR) | ((FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR] & 1) << 2); //Abnormal termination! ST0!
			FLOPPY.resultbuffer[1] = FLOPPY.ST1 = 0x27; //Drive write-protected! ST1!
			FLOPPY.resultbuffer[2] = FLOPPY.ST2 = 0x31; //ST2!
			FLOPPY.resultbuffer[3] = FLOPPY.currentcylinder[FLOPPY_DOR_DRIVENUMBERR];
			FLOPPY.resultbuffer[4] = FLOPPY.currenthead[FLOPPY_DOR_DRIVENUMBERR];
			FLOPPY.resultbuffer[5] = FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR];
			FLOPPY.resultbuffer[6] = FLOPPY.commandbuffer[5]; //Sector size!
			FLOPPY.commandstep = 3; //Move to result phase!
			FLOPPY_raiseIRQ(); //Entering result phase!
		}
		else //DSK/IMD or error?
		{
			updateFloppyWriteProtected(1, FLOPPY_DOR_DRIVENUMBERR); //Tried to write!
			FLOPPY.ST1 = 0x04; //Couldn't find any sector!
			FLOPPY.ST2 = 0x01; //Data address mark not found!
			if ((DSKImageFile = getDSKimage((FLOPPY_DOR_DRIVENUMBERR) ? FLOPPY1 : FLOPPY0)) || (IMDImageFile = getIMDimage((FLOPPY_DOR_DRIVENUMBERR) ? FLOPPY1 : FLOPPY0))) //Are we a DSK image file?
			{
				if (DSKImageFile) //DSK image?
				{
					if (readDSKTrackInfo(DSKImageFile, FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR], FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR], &trackinfo) == 0) //Read?
					{
						FLOPPY.ST1 = 0x01; //Not found!
						goto didntfindsectoridwrite;
					}
				}
				else if (IMDImageFile) //IMD image?
				{
					if (readIMDSectorInfo(IMDImageFile, FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR], FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR], 0, &IMD_sectorinfo) == 0) //Read?
					{
						FLOPPY.ST1 = 0x01; //Not found!
						goto didntfindsectoridwrite;
					}
				}
				for (sectornr = 0; sectornr < (IMDImageFile ? IMD_sectorinfo.totalsectors : ((word)trackinfo.numberofsectors)); ++sectornr) //Find the sector that's to be requested!
				{
					if (DSKImageFile) //DSK image?
					{
						if (readDSKSectorInfo(DSKImageFile, FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR], FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR], (byte)sectornr, &sectorinfo)) //Read?
						{
							if ((sectorinfo.SectorID == FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR]) && (sectorinfo.side == FLOPPY.currenthead[FLOPPY_DOR_DRIVENUMBERR]) && (sectorinfo.track == FLOPPY.currentcylinder[FLOPPY_DOR_DRIVENUMBERR])) //Found the requested sector as indicated?
							{
								FLOPPY.ST1 &= ~(0x04 | 0x01); //Found!
								FLOPPY.ST2 &= ~1; //Found!
								goto foundsectorIDwrite; //Found it!
							}
						}
					}
					else if (IMDImageFile) //IMD image?
					{
						if (readIMDSectorInfo(IMDImageFile, FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR], FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR], (byte)sectornr, &IMD_sectorinfo)) //Read?
						{
							if ((IMD_sectorinfo.sectorID == FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR]) && (IMD_sectorinfo.headnumber == FLOPPY.currenthead[FLOPPY_DOR_DRIVENUMBERR]) && (IMD_sectorinfo.cylinderID == FLOPPY.currentcylinder[FLOPPY_DOR_DRIVENUMBERR])) //Found the requested sector as indicated?
							{
								if (IMD_sectorinfo.datamark == DATAMARK_INVALID) //Invalid sector?
								{
									//Continue searching!
								}
								else //Valid data mark?
								{
									goto foundsectorIDwrite; //Found it!
								}
							}
						}
					}
				}
				if (DSKImageFile) //DSK image?
				{
					FLOPPY.readID_lastsectornumber = (trackinfo.numberofsectors - 1); //Last sector reached, go back to the first one!
				}
				else if (IMDImageFile) //IMD image?
				{
					FLOPPY.readID_lastsectornumber = (IMD_sectorinfo.totalsectors - 1); //Last sector reached, go back to the first one!
				}
				if (IMDImageFile) //IMD image?
				{
					if ((FLOPPY.readID_lastsectornumber==0) && (IMD_sectorinfo.datamark == DATAMARK_INVALID)) //Unformatted track?
					{
						FLOPPY.ST1 &= ~4; //Not set!
						FLOPPY.ST1 |= 0x01 /* | 0x80*/; //Couldn't find any sector!
					}
					else
					{
						goto writesector_formattednotfound; //Handle!
					}
				}
				else //Formatted, not found?
				{
				writesector_formattednotfound:
					FLOPPY.ST1 |= 0x04 /* | 0x80*/; //Couldn't find any sector!
				}
				FLOPPY.ST2 |= 0x04; //Not found!
				goto didntfindsectoridwrite;
				foundsectorIDwrite: //Found the sector ID for the write!
				FLOPPY.readID_lastsectornumber = (byte)sectornr; //This was the last sector we've read!
				if (DSKImageFile) //DSK image file?
				{
					DSKIMDsuccess = writeDSKSectorData(DSKImageFile, FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR], FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR], (byte)sectornr, FLOPPY.commandbuffer[5], &FLOPPY.databuffer); //Success?
				}
				else if (IMDImageFile)
				{
					DSKIMDsuccess = writeIMDSector(IMDImageFile, FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR], FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR], (byte)sectornr, (FLOPPY.datamark?1:0), FLOPPY.databuffersize, &FLOPPY.databuffer); //Try to read the sector as requested!
				}
				if (DSKIMDsuccess) //Read the data into memory?
				{
					switch (floppy_increasesector(FLOPPY_DOR_DRIVENUMBERR, FLOPPY.commandbuffer[6],0)) //Goto next sector!
					{
					case 1: //OK?
						//More to be written?
						floppy_writesector(); //Write another sector!
						return; //Finished!
					case 2: //Error during transfer?
						//Let the floppy_increasesector determine the error!
						break;
					case 0: //OK?
					default: //Unknown?
						FLOPPY.ST1 = 0x00;
						FLOPPY.ST2 = 0x00;
						FLOPPY_ST0_INTERRUPTCODEW(0); //Normal termination!
						FLOPPY_ST0_NOTREADYW(0); //We're ready!
						break;
					}
					FLOPPY_LOGD("FLOPPY: Finished transfer of data (%u sector(s)).", FLOPPY.sectorstransferred) //Log the completion of the sectors written!
					enterFloppyWriteResultPhase:
					FLOPPY.resultposition = 0;
					FLOPPY.resultbuffer[0] = FLOPPY.ST0 = ((FLOPPY.ST0 & 0x3B) | FLOPPY_DOR_DRIVENUMBERR) | ((FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR] & 1) << 2); //Abnormal termination! ST0!
					FLOPPY.resultbuffer[1] = FLOPPY.ST1; //Drive write-protected! ST1!
					FLOPPY.resultbuffer[2] = FLOPPY.ST2; //ST2!
					//The cylinder is set by floppy_increasesector!
					//The head is set by floppy_increasesector!
					FLOPPY.resultbuffer[5] = FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR];
					FLOPPY.resultbuffer[6] = FLOPPY.commandbuffer[5]; //Sector size!
					if ((FLOPPY.erroringtiming & (1<<FLOPPY_DOR_DRIVENUMBERR))==0) //Not timing?
					{
						FLOPPY.commandstep = 3; //Move to result phase!
						FLOPPY_raiseIRQ(); //Entering result phase!
					}
					else //Timing 0.5s?
					{
						FLOPPY.commandstep = 2; //Simulating no transfer!
						floppytime[FLOPPY_DOR_DRIVENUMBERR] = (DOUBLE)0.0; //Start in full delay!
						floppytimer[FLOPPY_DOR_DRIVENUMBERR] = (DOUBLE)(500000000.0); //Time the timeout for floppy!
						floppytiming |= (1<<FLOPPY_DOR_DRIVENUMBERR); //Make sure we're timing on the specified disk channel!
						FLOPPY.DMAPending = 0; //Not pending DMA!
					}
					return; //Abort!
				}
				else
				{
					if (FLOPPY.RWRequestedCylinder != FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR]) //Wrong cylinder to access?
					{
						FLOPPY.ST1 = 0x04; //Couldn't find any sector!
						FLOPPY.ST2 = 0x01; //Data address mark not found!
						goto floppy_errorwrite; //Error out!
					}
					FLOPPY.ST1 = 0x04; //Couldn't find any sector!
					FLOPPY.ST2 = 0x01; //Data address mark not found!
				}
			}
		floppy_errorwrite:
		didntfindsectoridwrite: //Couldn't find the sector ID!
			//Plain error!
			FLOPPY.ST0 |= 0x40; //Error out!
			goto enterFloppyWriteResultPhase; //Result phase starts!
		}
	}
}

void floppy_executeReadData()
{
	switch (floppy_increasesector(FLOPPY_DOR_DRIVENUMBERR, FLOPPY.commandbuffer[6],0)) //Goto next sector!
	{
	case 1: //Read more?
		//More to be written?
		if (FLOPPY.floppy_abort == 0) //Not aborting?
		{
			floppy_readsector(); //Read another sector!
			return; //Finished!
		}
	case 2: //Error during transfer?
		//Let the floppy_increasesector determine the error!
		break;
	case 0: //OK?
	default: //Unknown?
		FLOPPY_ST0_INTERRUPTCODEW(0); //Normal termination!
		FLOPPY_ST0_NOTREADYW(0); //We're ready!
		break;
	}
	FLOPPY.floppy_abort = 0; //Finished aborting if aborting!
	FLOPPY.resultposition = 0;
	FLOPPY_fillST0(FLOPPY_DOR_DRIVENUMBERR); //Setup ST0!
	FLOPPY.resultbuffer[0] = FLOPPY.ST0;
	FLOPPY.resultbuffer[1] = FLOPPY.ST1;
	FLOPPY.resultbuffer[2] = FLOPPY.ST2;
	//The cylinder is set by floppy_increasesector!
	//The head is set by floppy_increasesector!
	FLOPPY.resultbuffer[5] = FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR];
	FLOPPY.resultbuffer[6] = FLOPPY.commandbuffer[5]; //Sector size from the command buffer!
	FLOPPY.commandstep = 3; //Move to result phrase and give the result!
	FLOPPY_LOGD("FLOPPY: Finished transfer of data (%u sectors).", FLOPPY.sectorstransferred) //Log the completion of the sectors written!
	FLOPPY_raiseIRQ(); //Entering result phase!
}

void floppy_executeData() //Execute a floppy command. Data is fully filled!
{
	switch (FLOPPY.commandbuffer[0]) //What command!
	{
		case WRITE_DATA: //Write sector
		case WRITE_DELETED_DATA: //Write deleted sector
			//Write sector to disk!
			if (FLOPPY.ejectingpending[FLOPPY_DOR_DRIVENUMBERR]) //Ejected while transferring?
			{
				FLOPPY.resultposition = 0;
				FLOPPY.resultbuffer[0] = FLOPPY.ST0 = 0x40|0x10|((FLOPPY.ST0 & 0x3B) | FLOPPY_DOR_DRIVENUMBERR) | ((FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR] & 1) << 2); //Abnormal termination! ST0!
				FLOPPY.resultbuffer[1] = FLOPPY.ST1 = ST1_MEDIAEJECTED; //Drive write-protected! ST1!
				FLOPPY.resultbuffer[2] = FLOPPY.ST2 = ST2_MEDIAEJECTED; //ST2!
				FLOPPY.resultbuffer[3] = FLOPPY.currentcylinder[FLOPPY_DOR_DRIVENUMBERR];
				FLOPPY.resultbuffer[4] = FLOPPY.currenthead[FLOPPY_DOR_DRIVENUMBERR];
				FLOPPY.resultbuffer[5] = FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR];
				FLOPPY.resultbuffer[6] = FLOPPY.commandbuffer[5]; //Sector size from the command buffer!
				FLOPPY.commandstep = 3; //Move to result phase!
				FLOPPY_raiseIRQ(); //Entering result phase!
				return;
			}
			if (FLOPPY.databufferposition == FLOPPY.databuffersize) //Fully buffered?
			{
				floppy_executeWriteData(); //Execute us for now!
			}
			else //Unfinished buffer? Terminate!
			{
				FLOPPY.resultposition = 0;
				FLOPPY.resultbuffer[0] = FLOPPY.ST0 = ((FLOPPY.ST0 & 0x3B) | FLOPPY_DOR_DRIVENUMBERR) | ((FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR] & 1) << 2); //Abnormal termination! ST0!
				FLOPPY.resultbuffer[1] = FLOPPY.ST1; //Drive write-protected! ST1!
				FLOPPY.resultbuffer[2] = FLOPPY.ST2; //ST2!
				FLOPPY.resultbuffer[3] = FLOPPY.currentcylinder[FLOPPY_DOR_DRIVENUMBERR];
				FLOPPY.resultbuffer[4] = FLOPPY.currenthead[FLOPPY_DOR_DRIVENUMBERR];
				FLOPPY.resultbuffer[5] = FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR];
				FLOPPY.resultbuffer[6] = FLOPPY.commandbuffer[5]; //Sector size from the command buffer!
				FLOPPY.commandstep = 3; //Move to result phase!
				FLOPPY_raiseIRQ(); //Entering result phase!
			}
			break;
		case READ_TRACK: //Read complete track
		case READ_DATA: //Read sector
		case READ_DELETED_DATA: //Read deleted sector
		case SCAN_EQUAL:
		case SCAN_LOW_OR_EQUAL:
		case SCAN_HIGH_OR_EQUAL:
		case VERIFY: //Verify doesn't transfer data directly!
			if (FLOPPY.ejectingpending[FLOPPY_DOR_DRIVENUMBERR]) //Ejected while transferring?
			{
				//TODO: ST1/2?
				FLOPPY.resultposition = 0;
				FLOPPY.resultbuffer[0] = FLOPPY.ST0 = 0x40|0x10|((FLOPPY.ST0 & 0x3B) | FLOPPY_DOR_DRIVENUMBERR) | ((FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR] & 1) << 2); //Abnormal termination! ST0!
				FLOPPY.resultbuffer[1] = FLOPPY.ST1 = ST1_MEDIAEJECTED; //Drive write-protected! ST1!
				FLOPPY.resultbuffer[2] = FLOPPY.ST2 = ST2_MEDIAEJECTED; //ST2!
				FLOPPY.resultbuffer[3] = FLOPPY.currentcylinder[FLOPPY_DOR_DRIVENUMBERR];
				FLOPPY.resultbuffer[4] = FLOPPY.currenthead[FLOPPY_DOR_DRIVENUMBERR];
				FLOPPY.resultbuffer[5] = FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR];
				FLOPPY.resultbuffer[6] = FLOPPY.commandbuffer[5]; //Sector size!
				FLOPPY.commandstep = 3; //Move to result phase!
				FLOPPY_raiseIRQ(); //Entering result phase!
				return;
			}
			//We've finished reading the read data!
			if (FLOPPY.databufferposition == FLOPPY.databuffersize) //Fully processed?
			{
				floppy_executeReadData(); //Execute us for now!
			}
			else //Unfinished buffer? Terminate!
			{
				FLOPPY.resultposition = 0;
				FLOPPY.resultbuffer[0] = FLOPPY.ST0 = ((FLOPPY.ST0 & 0x3B) | FLOPPY_DOR_DRIVENUMBERR) | ((FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR] & 1) << 2); //Abnormal termination! ST0!
				FLOPPY.resultbuffer[1] = FLOPPY.ST1; //Drive write-protected! ST1!
				FLOPPY.resultbuffer[2] = FLOPPY.ST2; //ST2!
				FLOPPY.resultbuffer[3] = FLOPPY.currentcylinder[FLOPPY_DOR_DRIVENUMBERR];
				FLOPPY.resultbuffer[4] = FLOPPY.currenthead[FLOPPY_DOR_DRIVENUMBERR];
				FLOPPY.resultbuffer[5] = FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR];
				FLOPPY.resultbuffer[6] = FLOPPY.commandbuffer[5]; //Sector size!
				FLOPPY.commandstep = 3; //Move to result phase!
				FLOPPY_raiseIRQ(); //Entering result phase!
			}
			break;
		case FORMAT_TRACK: //Format sector
			if (FLOPPY.ejectingpending[FLOPPY_DOR_DRIVENUMBERR]) //Ejected while transferring?
			{
				FLOPPY.resultposition = 0;
				FLOPPY_fillST0(FLOPPY_DOR_DRIVENUMBERR); //Setup ST0!
				FLOPPY.resultbuffer[0] = FLOPPY.ST0 = 0x40|0x10| ((FLOPPY.ST0 & 0x3B) | FLOPPY_DOR_DRIVENUMBERR) | 0x10 | ((FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR] & 1) << 2); //Abnormal termination! ST0!
				FLOPPY.resultbuffer[1] = FLOPPY.ST1 = ST1_MEDIAEJECTED; //Drive write-protected! ST1!
				FLOPPY.resultbuffer[2] = FLOPPY.ST2 = ST2_MEDIAEJECTED; //ST2!
				FLOPPY.resultbuffer[0] = FLOPPY.ST0;
				FLOPPY.resultbuffer[1] = FLOPPY.ST1;
				FLOPPY.resultbuffer[2] = FLOPPY.ST2;
				FLOPPY.resultbuffer[3] = FLOPPY.currentcylinder[FLOPPY_DOR_DRIVENUMBERR];
				FLOPPY.resultbuffer[4] = FLOPPY.currenthead[FLOPPY_DOR_DRIVENUMBERR];
				FLOPPY.resultbuffer[5] = FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR];
				FLOPPY.resultbuffer[6] = FLOPPY.commandbuffer[2]; //Sector size from the command buffer!
				if ((FLOPPY.erroringtiming & (1<<FLOPPY_DOR_DRIVENUMBERR))==0) //Not timing?
				{
					FLOPPY.commandstep = 3; //Move to result phrase and give the result!
					FLOPPY_raiseIRQ(); //Entering result phase!
				}
				else //Timing 0.5 second!
				{
					FLOPPY.commandstep = 2; //Simulating no transfer!
					floppytime[FLOPPY_DOR_DRIVENUMBERR] = (DOUBLE)0.0; //Start in full delay!
					floppytimer[FLOPPY_DOR_DRIVENUMBERR] = (DOUBLE)(500000000.0); //Time the timeout for floppy!
					floppytiming |= (1<<FLOPPY_DOR_DRIVENUMBERR); //Make sure we're timing on the specified disk channel!
					FLOPPY.DMAPending = 0; //Not pending DMA!
				}
				return;
			}
			updateFloppyWriteProtected(1,FLOPPY_DOR_DRIVENUMBERR); //Try to write with(out) protection!
			FLOPPY_formatsector(0); //Execute a format sector command!
			break;
		default: //Unknown command?
			FLOPPY.commandstep = 0xFF; //Move to error phrase!
			FLOPPY.ST0 = 0x80 | (FLOPPY.ST0&0x30) | FLOPPY_DOR_DRIVENUMBERR | (FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR] << 2); //Invalid command!
			floppy_erroringout(); //Erroring out!
			FLOPPY_raiseIRQ(); //Raise an IRQ because of the error!
			break;
	}
}

void floppy_executeCommand() //Execute a floppy command. Buffers are fully filled!
{
	word IMDsectorsizerem; //Remainder to apply!
	word IMDsectorsizeshift; //Calculated IMD sector size!
	byte drive;
	char *DSKImageFile = NULL; //DSK image file to use?
	char *IMDImageFile = NULL; //DSK image file to use?
	SECTORINFORMATIONBLOCK sectorinfo; //Information about the sector!
	IMDIMAGE_SECTORINFO IMD_sectorinfo; //Information about the sector!
	TRACKINFORMATIONBLOCK trackinfo;
	word sectornr;
	byte ReadID_loopdetection;
	FLOPPY.TC = 0; //Reset TC flag!
	FLOPPY.resultposition = 0; //Default: start of the result!
	FLOPPY.databuffersize = 0; //Default: nothing to write/read!
	FLOPPY.floppy_abort = 0; //Not aborting the command after the data by default!
	FLOPPY_LOGD("FLOPPY: executing command: %02X", FLOPPY.commandbuffer[0]) //Executing this command!
	updateFloppyGeometries(FLOPPY_DOR_DRIVENUMBERR, FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR], FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR]); //Update the floppy geometries!
	FLOPPY.erroringtiming &= ~(1<<FLOPPY_DOR_DRIVENUMBERR); //Default: not erroring!
	FLOPPY.ejectingpending[FLOPPY_DOR_DRIVENUMBERR] = 0; //No ejecting pending!
	switch (FLOPPY.commandbuffer[0]) //What command!
	{
		case WRITE_DELETED_DATA: //Write deleted sector
			FLOPPY.datamark = 1; //Deleted!
			goto skipwritedatamark_deleted;
		case WRITE_DATA: //Write sector
			FLOPPY.datamark = 0; //Not deleted!
			skipwritedatamark_deleted:
			FLOPPY.activecommand[FLOPPY_DOR_DRIVENUMBERR] = FLOPPY.commandbuffer[0]; //Our command to execute!
			FLOPPY.RWRequestedCylinder = FLOPPY.commandbuffer[2]; //Requested cylinder!
			FLOPPY.currenthead[FLOPPY_DOR_DRIVENUMBERR] = FLOPPY.commandbuffer[3]; //Current head!
			FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR] = ((FLOPPY.commandbuffer[1] & 4) >> 2); //Physical head select!
			FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR] = FLOPPY.commandbuffer[4]; //Current sector!
			updateFloppyGeometries(FLOPPY_DOR_DRIVENUMBERR, FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR], FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR]); //Update our geometry to use!
			updateST3(FLOPPY_DOR_DRIVENUMBERR); //Update ST3 only!
			floppy_writesector(); //Start writing a sector!
			break;
		case READ_DELETED_DATA: //Read deleted sector
			FLOPPY.datamark = 1; //Deleted!
			goto skipreaddatamark_deleted;
		case READ_TRACK: //Read track
		case READ_DATA: //Read sector
		case SCAN_EQUAL:
		case SCAN_LOW_OR_EQUAL:
		case SCAN_HIGH_OR_EQUAL:
		case VERIFY: //Verify doesn't transfer data directly!
			floppytime[FLOPPY_DOR_DRIVENUMBERR] = (DOUBLE)0.0; //Start in full delay!
			FLOPPY.datamark = 0; //Not deleted!
			skipreaddatamark_deleted:
			FLOPPY.activecommand[FLOPPY_DOR_DRIVENUMBERR] = FLOPPY.commandbuffer[0]; //Our command to execute!
			FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR] = ((FLOPPY.commandbuffer[1] & 4) >> 2); //Physical head select!
			FLOPPY.RWRequestedCylinder = FLOPPY.commandbuffer[2]; //Requested cylinder!
			FLOPPY.currenthead[FLOPPY_DOR_DRIVENUMBERR] = FLOPPY.commandbuffer[3]; //Current head!
			FLOPPY.currentsector[FLOPPY_DOR_DRIVENUMBERR] = FLOPPY.commandbuffer[4]; //Current sector!
			FLOPPY.floppy_scanningforSectorID = 1; //Scanning for the sector ID on the disk!
			updateST3(FLOPPY_DOR_DRIVENUMBERR); //Update ST3 only!
			updateFloppyGeometries(FLOPPY_DOR_DRIVENUMBERR, FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR],FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR]); //Update our geometry to use!
			floppy_readsector(); //Start reading a sector!
			break;
		case SPECIFY: //Fix drive data/specify command
			FLOPPY.DriveData[FLOPPY_DOR_DRIVENUMBERR].data[0] = FLOPPY.commandbuffer[1]; //Set setting byte 1/2!
			FLOPPY.DriveData[FLOPPY_DOR_DRIVENUMBERR].data[1] = FLOPPY.commandbuffer[2]; //Set setting byte 2/2!
			FLOPPY.DriveData[FLOPPY_DOR_DRIVENUMBERR].headloadtime = FLOPPY_headloadtimerate(FLOPPY_DOR_DRIVENUMBERR); //Head load rate!
			FLOPPY.DriveData[FLOPPY_DOR_DRIVENUMBERR].headunloadtime = FLOPPY_headunloadtimerate(FLOPPY_DOR_DRIVENUMBERR); //Head unload rate!
			FLOPPY.DriveData[FLOPPY_DOR_DRIVENUMBERR].steprate = FLOPPY_steprate(FLOPPY_DOR_DRIVENUMBERR); //Step rate!
			FLOPPY.commandstep = 0; //Reset controller command status!
			//No interrupt, according to http://wiki.osdev.org/Floppy_Disk_Controller
			FLOPPY_lowerIRQ(); //Lower the IRQ anyways!
			break;
		case RECALIBRATE: //Calibrate drive
			FLOPPY.commandstep = 0; //Start our timed execution!
			FLOPPY.currentphysicalhead[FLOPPY.commandbuffer[1]&3] = 0; //Physical head select!
			FLOPPY.activecommand[FLOPPY.commandbuffer[1]&3] = FLOPPY.commandbuffer[0]; //Our command to execute timing!
			FLOPPY.ST0 &= ~0x20; //We start to seek!
			floppytime[FLOPPY.commandbuffer[1] & 3] = 0.0;
			floppytimer[FLOPPY.commandbuffer[1] & 3] = FLOPPY.DriveData[FLOPPY.commandbuffer[1]&3].steprate; //Step rate!
			floppytiming |= (1<<(FLOPPY.commandbuffer[1] & 3)); //Timing!
			FLOPPY.recalibratestepsleft[FLOPPY.commandbuffer[1] & 3] = 79; //Up to 79 pulses!
			FLOPPY_MSR_BUSYINPOSITIONINGMODEW((FLOPPY.commandbuffer[1] & 3),1); //Seeking!
			if (!FLOPPY.physicalcylinder[FLOPPY.commandbuffer[1] & 3]) //Already there?
			{
				updateFloppyGeometries((FLOPPY.commandbuffer[1] & 3), FLOPPY.currentphysicalhead[FLOPPY.commandbuffer[1] & 3], FLOPPY.physicalcylinder[FLOPPY.commandbuffer[1] & 3]); //Up
				if (FLOPPY.geometries[(FLOPPY.commandbuffer[1] & 3)])
				{
					FLOPPY.readID_lastsectornumber = FLOPPY.geometries[(FLOPPY.commandbuffer[1] & 3)]->SPT+1; //Act like the track has changed!
				}
				else
				{
					FLOPPY.readID_lastsectornumber = 0; //Act like the track has changed!
				}
				FLOPPY_finishrecalibrate(FLOPPY.commandbuffer[1] & 3); //Finish the recalibration automatically(we're eating up the command)!
				FLOPPY_checkfinishtiming(FLOPPY.commandbuffer[1] & 3); //Finish if required!
			}
			else
			{
				clearDiskChanged(FLOPPY.commandbuffer[1] & 3); //Clear the disk changed flag for the new command!
			}
			break;
		case SENSE_INTERRUPT: //Check interrupt status
			//Set result
			FLOPPY_hadIRQ = FLOPPY.IRQPending; //Was an IRQ Pending?
			FLOPPY.commandstep = 3; //Move to result phrase!
			byte datatemp;
			datatemp = FLOPPY.ST0; //Save default!
			//Reset IRQ line!
			if (FLOPPY.reset_pending) //Reset is pending?
			{
				if (FLOPPY.reset_pending == 0xFF) //Invalid reset sense interupt without polling mode on?
				{
					FLOPPY.reset_pending = 0; //Actual supposed value!
					FLOPPY_LOGD("FLOPPY: Warning: Checking interrupt status without reset IRQ pending!")
					goto floppy_reset_errorIRQ; //Handle the erroring out because no sense interrupt is allowed!
				}
				byte reset_drive;
				reset_drive = FLOPPY.reset_pending_size - (FLOPPY.reset_pending--); //We're pending this drive!
				FLOPPY_LOGD("FLOPPY: Reset Sense Interrupt, pending drive %u/%u...",reset_drive,FLOPPY.reset_pending_size)
				FLOPPY.ST0 &= 0xF8; //Clear low 3 bits!
				FLOPPY_ST0_UNITSELECTW(reset_drive); //What drive are we giving!
				FLOPPY_ST0_CURRENTHEADW(FLOPPY.currentphysicalhead[reset_drive] & 1); //Set the current head of the drive!
				FLOPPY_ST0_UNITCHECKW(0); //We're valid, because polling more is valid by default!
				datatemp = FLOPPY.ST0; //Use the current data, not the cleared data! Polling is set here always!
				if (FLOPPY.reset_pending == 0) //Finished reset pending after the second drive?
				{
					FLOPPY.ST0 &= 0x3F; //Remove the polling status and become normal from now on!
				}
			}
			else if (!FLOPPY_hadIRQ) //Not an pending IRQ?
			{
				FLOPPY_LOGD("FLOPPY: Warning: Checking interrupt status without IRQ pending!")
				floppy_reset_errorIRQ:
				FLOPPY.ST0 = 0x80; //Error!
				FLOPPY.resultbuffer[0] = FLOPPY.ST0; //Give ST0!
				FLOPPY.resultbuffer[1] = FLOPPY.currentcylinder[FLOPPY_DOR_DRIVENUMBERR]; //Our idea of the current cylinder!
				FLOPPY.resultposition = 0; //Start result!
				FLOPPY.commandstep = 3; //Move to result phase!
				FLOPPY.commandstep = 0xFF; //Give ST0, then return to command mode!
				return;
			}
			FLOPPY_lowerIRQ(); //Lower the IRQ!

			FLOPPY_LOGD("FLOPPY: Sense interrupt: ST0=%02X, Currentcylinder=%02X", datatemp, FLOPPY.physicalcylinder[FLOPPY_DOR_DRIVENUMBERR])
			FLOPPY.resultbuffer[0] = datatemp; //Give old ST0 if changed this call!
			FLOPPY.resultbuffer[1] = FLOPPY.currentcylinder[FLOPPY_DOR_DRIVENUMBERR]; //Our idea of the current cylinder!
			FLOPPY.resultposition = 0; //Start result!
			FLOPPY.commandstep = 3; //Result phase!
			break;
		case SEEK: //Seek/park head
			FLOPPY.commandstep = 0; //Start our timed execution!
			FLOPPY.currentphysicalhead[FLOPPY.commandbuffer[1] & 3] = ((FLOPPY.commandbuffer[1] & 4) >> 2); //Physical head select!
			FLOPPY.currenthead[FLOPPY.commandbuffer[1] & 3] = ((FLOPPY.commandbuffer[1] & 4) >> 2); //The selected head!
			FLOPPY.activecommand[FLOPPY.commandbuffer[1] & 3] = FLOPPY.commandbuffer[0]; //Our command to execute!
			FLOPPY.seekdestination[FLOPPY.commandbuffer[1] & 3] = FLOPPY.commandbuffer[2]; //Our destination!
			FLOPPY.seekrel[FLOPPY.commandbuffer[1] & 3] = FLOPPY.MT; //Seek relative?
			FLOPPY.seekrelup[FLOPPY.commandbuffer[1] & 3] = FLOPPY.MFM; //Seek relative up(when seeking relatively)
			FLOPPY.ST0 &= ~0x20; //We start to seek!
			floppytime[FLOPPY.commandbuffer[1] & 3] = 0.0;
			floppytiming |= (1<<(FLOPPY.commandbuffer[1] & 3)); //Timing!
			floppytimer[FLOPPY.commandbuffer[1] & 3] = FLOPPY.DriveData[FLOPPY.commandbuffer[1] & 3].steprate; //Step rate!
			FLOPPY_MSR_BUSYINPOSITIONINGMODEW((FLOPPY.commandbuffer[1] & 3),1); //Seeking!
			if (((FLOPPY.commandbuffer[1] & 3)<2) && (((FLOPPY.currentcylinder[FLOPPY.commandbuffer[1] & 3]==FLOPPY.seekdestination[FLOPPY.commandbuffer[1] & 3]) && (FLOPPY.currentcylinder[FLOPPY.commandbuffer[1] & 3] < FLOPPY.geometries[FLOPPY.commandbuffer[1] & 3]->tracks) && (FLOPPY.seekrel[FLOPPY.commandbuffer[1] & 3]==0)) || (FLOPPY.seekrel[FLOPPY.commandbuffer[1] & 3] && (FLOPPY.seekdestination[FLOPPY.commandbuffer[1] & 3]==0)))) //Found and existant?
			{
				updateFloppyGeometries((FLOPPY.commandbuffer[1]&3), FLOPPY.currentphysicalhead[FLOPPY.commandbuffer[1] & 3], FLOPPY.physicalcylinder[FLOPPY.commandbuffer[1] & 3]); //Up
				if (FLOPPY.geometries[(FLOPPY.commandbuffer[1] & 3)])
				{
					FLOPPY.readID_lastsectornumber = FLOPPY.geometries[(FLOPPY.commandbuffer[1] & 3)]->SPT + 1; //Act like the track has changed!
				}
				else
				{
					FLOPPY.readID_lastsectornumber = 0; //Act like the track has changed!
				}
				FLOPPY_finishseek(FLOPPY.commandbuffer[1] & 3,1); //Finish the recalibration automatically(we're eating up the command)!
				FLOPPY_checkfinishtiming(FLOPPY.commandbuffer[1] & 3); //Finish if required!
			}
			else
			{
				clearDiskChanged(FLOPPY.commandbuffer[1] & 3); //Clear the disk changed flag for the new command!
			}
			break;
		case SENSE_DRIVE_STATUS: //Check drive status
			FLOPPY.currenthead[FLOPPY.commandbuffer[1]&3] = (FLOPPY.commandbuffer[1]&4)>>2; //Set the new head from the parameters!
			FLOPPY.currentphysicalhead[FLOPPY.commandbuffer[1] & 3] = (FLOPPY.commandbuffer[1] & 4) >> 2; //Set the new head from the parameters!
			updateST3(FLOPPY.commandbuffer[1]&3); //Update ST3 only!
			FLOPPY.resultbuffer[0] = FLOPPY.ST3; //Give ST3!
			FLOPPY.resultposition = 0; //Start the result!
			FLOPPY.commandstep = 3; //Result phase!
			break;
		case READ_ID: //Read sector ID
			drive = FLOPPY.commandbuffer[1] & 3; //What drive!
			FLOPPY.currentphysicalhead[drive] = ((FLOPPY.commandbuffer[1] & 4) >> 2); //Physical head select!
			FLOPPY.activecommand[drive] = FLOPPY.commandbuffer[0]; //Our command to execute!
			FLOPPY.currenthead[drive] = ((FLOPPY.commandbuffer[1] & 4) >> 2); //The head to use!
			FLOPPY.erroringtiming &= ~(1<<FLOPPY_DOR_DRIVENUMBERR); //Default: not erroring!
			if ((!FLOPPY.geometries[drive]) || ((drive < 2) ? (!is_mounted(drive ? FLOPPY1 : FLOPPY0)) : 1)) //Not mounted?
			{
				floppy_common_sectoraccess_nomedia(FLOPPY_DOR_DRIVENUMBERR); //No media!
				return;
				FLOPPY_ST0_UNITSELECTW(drive); //Current unit!
				FLOPPY_ST0_CURRENTHEADW(FLOPPY.currentphysicalhead[drive] & 1); //Current head!
				FLOPPY_ST0_NOTREADYW(1); //We're not ready yet!
				FLOPPY_ST0_UNITCHECKW(0); //Clear unit check and Interrupt code: we're OK. Also clear SE flag: we're still busy!
				FLOPPY_ST0_INTERRUPTCODEW(0); //Clear unit check and Interrupt code: we're OK. Also clear SE flag: we're still busy!
				FLOPPY_ST0_SEEKENDW(0); //Clear seek end: we're reading a sector!
				FLOPPY.ST1 = 0x01|0x04; //Couldn't find any sector!
				FLOPPY.ST2 = 0x01; //Data address mark not found!
				FLOPPY.erroringtiming |= (1<<FLOPPY_DOR_DRIVENUMBERR); //Erroring!
				goto floppy_errorReadID; //Error out!
			}
			FLOPPY.RWRequestedCylinder = FLOPPY.currentcylinder[drive]; //Cylinder to access?
			if (!FLOPPY_supportsrate(drive)) //We don't support the rate?
			{
				FLOPPY.ST1 = 0x01; //Couldn't find any sector!
				FLOPPY.ST2 = 0x01; //Data address mark not found!
				goto floppy_errorReadID; //Error out!
			}

			if (!(FLOPPY_DOR_MOTORCONTROLR&(1 << FLOPPY_DOR_DRIVENUMBERR))) //Not motor ON?
			{
				FLOPPY.ST1 = 0x01|0x04; //Couldn't find any sector!
				FLOPPY.ST2 = 0x01; //Data address mark not found!
				FLOPPY.erroringtiming |= (1<<FLOPPY_DOR_DRIVENUMBERR); //Erroring!
				goto floppy_errorReadID; //Error out!
			}

			FLOPPY.ST0 = 0; //According to Bochs, ST0 gets fully cleared, including the Seek End bit!
			updateFloppyGeometries(drive, FLOPPY.currentphysicalhead[drive], FLOPPY.physicalcylinder[drive]); //Update our geometry to use!

			if ((FLOPPY.geometries[drive]->DoubleDensity != (FLOPPY.MFM & ~DENSITY_IGNORE)) && (!(FLOPPY.geometries[drive]->DoubleDensity & DENSITY_IGNORE) || density_forced) && EMULATE_DENSITY) //Wrong density?
			{
				FLOPPY_LOGD("FLOPPY: Error: Invalid density!")
				FLOPPY.ST1 = 0x01; //Couldn't find any sector!
				FLOPPY.ST2 = 0x01; //Data address mark not found!
				goto didntfindsectoridreadid;
			}

			FLOPPY_ST0_UNITCHECKW(0); //Not faulted!
			FLOPPY_ST0_NOTREADYW(0); //Ready!
			FLOPPY_ST0_INTERRUPTCODEW(0); //OK! Correctly executed!
			FLOPPY_ST0_CURRENTHEADW(FLOPPY.currentphysicalhead[drive]&1); //Head!
			FLOPPY_ST0_UNITSELECTW(drive); //Unit selected!
			FLOPPY_ST0_SEEKENDW(0); //Clear seek end in this case: we're acting like the Read sectors command!
			if ((DSKImageFile = getDSKimage((drive) ? FLOPPY1 : FLOPPY0)) || (IMDImageFile = getIMDimage((drive) ? FLOPPY1 : FLOPPY0))) //Are we a DSK/IMD image file?
			{
				if (DSKImageFile) //DSK image?
				{
					if (readDSKTrackInfo(DSKImageFile, FLOPPY.currentphysicalhead[drive], FLOPPY.physicalcylinder[drive], &trackinfo) == 0) //Read?
					{
						FLOPPY.ST1 = 0x01; //Couldn't find any sector!
						FLOPPY.ST2 = 0x01; //Data address mark not found!
						goto didntfindsectoridreadid;
					}
				}
				else if (IMDImageFile) //IMD image?
				{
					if (readIMDSectorInfo(IMDImageFile, FLOPPY.physicalcylinder[drive], FLOPPY.currentphysicalhead[drive], 0, &IMD_sectorinfo) == 0) //Read?
					{
						FLOPPY.ST1 = 0x01; //Couldn't find any sector!
						FLOPPY.ST2 = 0x01; //Data address mark not found!
						goto didntfindsectoridreadid;
					}
				}
				FLOPPY.ST1 = 0; //Reset ST1!
				FLOPPY.ST2 = 0; //Reset ST2!
				ReadID_loopdetection = 2; //Allow looping back to the index hole twice(first time for the end being reached, second time for the sector not being found)!
				retryReadID: //Try again for end-of-disc!
				if (FLOPPY.readID_lastsectornumber >= (IMDImageFile?IMD_sectorinfo.totalsectors:trackinfo.numberofsectors)) //Out of range?
				{
					sectornr = 0; //Try to use the first sector on the disk!
					if (IMDImageFile?IMD_sectorinfo.totalsectors:trackinfo.numberofsectors) //Valid?
					{
						goto tryReadIDnewsector; //Found the last sector number!
					}
				}
				
				for (sectornr = (FLOPPY.readID_lastsectornumber+1); sectornr < (word)(IMDImageFile ? IMD_sectorinfo.totalsectors : trackinfo.numberofsectors); ++sectornr) //Find the next sector that's to be requested!
				{
				tryReadIDnewsector: //Try to read a new sector number!
					if (DSKImageFile) //DSK image?
					{
						if (readDSKSectorInfo(DSKImageFile, FLOPPY.currentphysicalhead[drive], FLOPPY.physicalcylinder[drive], (byte)sectornr, &sectorinfo)) //Read?
						{
							//if ((sectorinfo.SectorID == FLOPPY.currentsector[drive]) && (sectorinfo.side==FLOPPY.currenthead[drive]) && (sectorinfo.track==FLOPPY.currentcylinder[drive])) //Found the requested sector as indicated?
							{
								FLOPPY.readID_lastsectornumber = (byte)sectornr; //This was the last sector we've read!
								goto foundsectorIDreadid; //Found it!
							}
						}
					}
					else if (IMDImageFile) //IMD image?
					{
						if (readIMDSectorInfo(IMDImageFile, FLOPPY.physicalcylinder[drive], FLOPPY.currentphysicalhead[drive], (byte)sectornr, &IMD_sectorinfo)) //Read?
						{
							if (IMD_sectorinfo.datamark == DATAMARK_INVALID) //Invalid sector?
							{
								//Continue searching!
							}
							else
							//if ((IMD_sectorinfo.SectorID == FLOPPY.currentsector[drive]) && (IMD_sectorinfo.head==FLOPPY.currenthead[drive]) && (IMD_sectorinfo.cylinder==FLOPPY.currentcylinder[drive])) //Found the requested sector as indicated?
							{
								FLOPPY.readID_lastsectornumber = (byte)sectornr; //This was the last sector we've read!
								goto foundsectorIDreadid; //Found it!
							}
						}
					}
				}
				if (DSKImageFile) //DSK image?
				{
					FLOPPY.readID_lastsectornumber = trackinfo.numberofsectors; //Last sector reached, go back to the first one!
				}
				else if (IMDImageFile) //IMD image?
				{
					FLOPPY.readID_lastsectornumber = IMD_sectorinfo.totalsectors; //Last sector reached, go back to the first one!
				}
				if (ReadID_loopdetection--) //Allowed to loop back to the start of the disc?
				{
					goto retryReadID; //Try again, from the index hole!
				}
				if (IMDImageFile) //IMD image?
				{
					if ((IMD_sectorinfo.totalsectors<=1) && (IMD_sectorinfo.datamark == DATAMARK_INVALID)) //Unformatted track?
					{
						FLOPPY.ST1 &= ~4; //Not set!
						FLOPPY.ST1 |= 0x01 /* | 0x80*/; //Couldn't find any sector!
					}
					else
					{
						goto readID_formattednotfound; //Handle!
					}
				}
				else //Formatted, not found?
				{
				readID_formattednotfound:
					FLOPPY.ST1 |= 0x04 /* | 0x80*/; //Couldn't find any sector!
				}
				FLOPPY.ST2 |= 0x04; //Sector not found!
				goto didntfindsectoridreadidresult; //Couldn't find a sector to give!
			foundsectorIDreadid: //Found the sector ID for the write!
				if (DSKImageFile) //DSK image?
				{
					if (readDSKSectorInfo(DSKImageFile, FLOPPY.currentphysicalhead[drive], FLOPPY.physicalcylinder[drive], (byte)sectornr, &sectorinfo)) //Read the sector information too!
					{
						FLOPPY.readID_lastsectornumber = (byte)sectornr; //This was the last sector we've read!
						FLOPPY.ST1 = sectorinfo.ST1; //Load ST1!
						FLOPPY.ST2 = sectorinfo.ST2; //Load ST2!
						FLOPPY.resultbuffer[6] = sectorinfo.SectorSize; //Sector size!
						FLOPPY.resultbuffer[3] = sectorinfo.track; //Cylinder(exception: actually give what we read from the disk)!
						FLOPPY.resultbuffer[4] = sectorinfo.side; //Head!
						FLOPPY.resultbuffer[5] = sectorinfo.SectorID; //Sector!
					}
					else
					{
						didntfindsectoridreadid:
						FLOPPY.ST1 = 0x04; //Not found!
						FLOPPY.ST2 = 0x00; //Not found!
						didntfindsectoridreadidresult:
						FLOPPY.resultbuffer[6] = 0; //Unknown sector size!
						goto floppy_errorReadID; //Error out!
					}
				}
				else if (IMDImageFile) //IMD image?
				{
					if (readIMDSectorInfo(IMDImageFile, FLOPPY.physicalcylinder[drive], FLOPPY.currentphysicalhead[drive], (byte)sectornr, &IMD_sectorinfo)) //Read the sector information too!
					{
						FLOPPY.readID_lastsectornumber = (byte)sectornr; //This was the last sector we've read!
						FLOPPY.ST1 = 0x00; //Load ST1!
						FLOPPY.ST2 = 0x00; //Load ST2!
						FLOPPY.resultbuffer[6] = 0; //Default: no shift!
						//Convert the sector size to a valid number to give as a result!
						IMDsectorsizerem = (IMD_sectorinfo.sectorsize >> 7); //Initialize shifting value!
						IMDsectorsizeshift = 0; //Default: nothing shifted!
						for (;IMDsectorsizerem;) //Bits left set?
						{
							IMDsectorsizerem >>= 1; //Shift one bit off!
							++IMDsectorsizeshift; //One more set bit has been shifted off!
						}
						if ((IMD_sectorinfo.sectorsize >> 7) == 0) //Zero? Less than 128 bytes/sector!
						{
							IMDsectorsizeshift = 0xFF; //Is this supposed to be 0xFF since that's what's used when specifying sectors with <128 bytes per sector? Otherwise perhaps 0?
						}
						else
						{
							--IMDsectorsizeshift; //One bit less for the 128<<n value!
						}
						FLOPPY.resultbuffer[6] = IMDsectorsizeshift; //Sector size in 128<<n format!
						FLOPPY.resultbuffer[3] = IMD_sectorinfo.cylinderID; //Cylinder(exception: actually give what we read from the disk)!
						FLOPPY.resultbuffer[4] = IMD_sectorinfo.headnumber; //Head!
						FLOPPY.resultbuffer[5] = IMD_sectorinfo.sectorID; //Sector!
					}
					else //Couldn't read sector information?
					{
						FLOPPY.ST1 = 0x04; //Couldn't find any sector!
						goto didntfindsectoridreadidresult; //Same as above!
					}
				}
			}
			else //Normal disk? Generate valid data!
			{
				FLOPPY.ST1 = 0x00; //Clear ST1!
				FLOPPY.ST2 = 0x00; //Clear ST2!
				updateST3(drive); //Update track 0!
				//Clip the sector number first!
				if (!FLOPPY.readID_lastsectornumber) FLOPPY.readID_lastsectornumber = 1; //Sector number from 1 to SPT!
				else if (FLOPPY.readID_lastsectornumber > (FLOPPY.geometries[drive] ? FLOPPY.geometries[drive]->SPT : 0)) FLOPPY.readID_lastsectornumber = 1; //Limit to SPT!
				//Simulate the sectors moving for the software to see!
				else if (FLOPPY.readID_lastsectornumber < (FLOPPY.geometries[drive] ? FLOPPY.geometries[drive]->SPT : 0)) //Gotten next?
				{
					++FLOPPY.readID_lastsectornumber; //Next sector!
				}
				else //First sector reached again with index hole!
				{
					FLOPPY.readID_lastsectornumber = 1; //Back at sector 1!
				}
				//Start validating the sector number!
				if (FLOPPY.geometries[drive] && ((drive < 2) ? (is_mounted(drive ? FLOPPY1 : FLOPPY0)) : 0)) //Valid geometry?
				{
					if ((int_32)floppy_LBA(drive, FLOPPY.currentphysicalhead[drive], FLOPPY.physicalcylinder[drive], FLOPPY.readID_lastsectornumber) >= (int_32)(FLOPPY.geometries[drive]->KB * 1024)) //Invalid address within our image!
					{
						goto floppy_errorReadID; //Error out!
					}
				}
				else //No geometry? Always error out!
				{
					goto floppy_errorReadID; //Error out!
				}
				FLOPPY.resultbuffer[6] = 2; //Always 512 byte sectors!
				FLOPPY.resultbuffer[3] = FLOPPY.physicalcylinder[drive]; //Cylinder(exception: actually give what we read from the disk)!
				FLOPPY.resultbuffer[4] = FLOPPY.currenthead[drive]; //Head!
				FLOPPY.resultbuffer[5] = FLOPPY.readID_lastsectornumber; //Last sector read!
			}

			//Start the reading of the ID on the timer!
			FLOPPY.databuffersize = 0x200; //Sector size into data buffer!
			FLOPPY.readIDdrive = drive; //Setup ST0!
			FLOPPY.readIDerror = 0; //No error!
			FLOPPY_startData(drive); //Start the data phase!
			return; //Correct read!
		floppy_errorReadID:
			FLOPPY.databuffersize = 0x200; //Sector size into data buffer!
			FLOPPY.readIDdrive = drive; //Setup ST0!
			FLOPPY.readIDerror = 1; //Error!
			FLOPPY_startData(drive); //Start the data phase!
			return; //Incorrect read!
			break;
		case FORMAT_TRACK: //Format sector
			drive = (FLOPPY.commandbuffer[1] & 3); //What drive!
			FLOPPY.RWRequestedCylinder = FLOPPY.physicalcylinder[drive]; //What track to format!
			FLOPPY.currentphysicalhead[drive] = ((FLOPPY.commandbuffer[1] & 4) >> 2); //Physical head select!
			FLOPPY.activecommand[drive] = FLOPPY.commandbuffer[0]; //Our command to execute!
			FLOPPY.currenthead[drive] = (FLOPPY.commandbuffer[1] & 4) >> 2; //Set the new head from the parameters!
			FLOPPY.currentsector[drive] = 1; //Start out with sector #1(first sector of the track on DSK images)!
			FLOPPY.currentformatsector[drive] = 0; //Currently formatting sector number on the track for IMD images, 0-based, sides in any order!
			updateFloppyGeometries(drive, FLOPPY.currentphysicalhead[drive], FLOPPY.physicalcylinder[drive]); //Update our geometry to use!
			if (!(FLOPPY_DOR_MOTORCONTROLR&(1 << drive))) //Not motor ON?
			{
				FLOPPY_LOGD("FLOPPY: Error: drive motor not ON!")
				FLOPPY_ST0_UNITSELECTW(drive); //Current unit!
				FLOPPY_ST0_CURRENTHEADW(FLOPPY.currentphysicalhead[drive] & 1); //Current head!
				FLOPPY_ST0_NOTREADYW(1); //We're not ready yet!
				FLOPPY_ST0_UNITCHECKW(0); //Clear unit check and Interrupt code: we're OK. Also clear SE flag: we're still busy!
				FLOPPY_ST0_INTERRUPTCODEW(0); //Clear unit check and Interrupt code: we're OK. Also clear SE flag: we're still busy!
				FLOPPY_ST0_SEEKENDW(0); //Clear seek end: we're reading a sector!
				FLOPPY.ST1 = 0x01; //Couldn't find any sector!
				FLOPPY.ST2 = 0x01; //Data address mark not found!
		//Plain error reading the sector!
		//ENTER RESULT PHASE
				FLOPPY_LOGD("FLOPPY: Finished transfer of data (%u sector(s)).", FLOPPY.sectorstransferred) //Log the completion of the sectors written!
				FLOPPY.resultposition = 0;
				FLOPPY_fillST0(drive); //Setup ST0!
				FLOPPY.resultbuffer[0] = FLOPPY.ST0 = 0x40 | ((FLOPPY.ST0 & 0x3B) | drive) | ((FLOPPY.currentphysicalhead[drive] & 1) << 2); //Abnormal termination! ST0!
				FLOPPY.resultbuffer[1] = FLOPPY.ST1;
				FLOPPY.resultbuffer[2] = FLOPPY.ST2;
				FLOPPY.resultbuffer[3] = FLOPPY.currentcylinder[drive]; //Error cylinder!
				FLOPPY.resultbuffer[4] = FLOPPY.currenthead[drive]; //Error head!
				FLOPPY.resultbuffer[5] = FLOPPY.currentsector[drive]; //Error sector!
				FLOPPY.resultbuffer[6] = FLOPPY.commandbuffer[2]; //Sector size from the command buffer!
				FLOPPY.commandstep = 3; //Move to result phrase and give the result!
				FLOPPY_raiseIRQ(); //Entering result phase!
				return; //Abort!
			}

			if (!FLOPPY.geometries[drive] || ((drive < 2) ? (!is_mounted(drive ? FLOPPY1 : FLOPPY0)) : 1)) //No geometry?
			{
				floppy_common_sectoraccess_nomedia(FLOPPY_DOR_DRIVENUMBERR); //No media!
				return;
				FLOPPY_ST0_UNITSELECTW(drive); //Current unit!
				FLOPPY_ST0_CURRENTHEADW(FLOPPY.currentphysicalhead[drive] & 1); //Current head!
				FLOPPY_ST0_NOTREADYW(1); //We're not ready yet!
				FLOPPY_ST0_UNITCHECKW(0); //Clear unit check and Interrupt code: we're OK. Also clear SE flag: we're still busy!
				//FLOPPY_ST0_SEEKENDW(0); //Clear unit check and Interrupt code: we're OK. Also clear SE flag: we're still busy!
				FLOPPY_ST0_INTERRUPTCODEW(0); //Clear unit check and Interrupt code: we're OK. Also clear SE flag: we're still busy!
				FLOPPY_ST0_SEEKENDW(0); //Clear seek end: we're reading a sector!
				FLOPPY.ST1 = 0x01; //Couldn't find any sector!
				FLOPPY.ST2 = 0x01; //Data address mark not found!
		//Plain error reading the sector!
		//ENTER RESULT PHASE
				FLOPPY_LOGD("FLOPPY: Finished transfer of data (%u sector(s)).", FLOPPY.sectorstransferred) //Log the completion of the sectors written!
				FLOPPY.resultposition = 0;
				FLOPPY_fillST0(drive); //Setup ST0!
				FLOPPY.resultbuffer[0] = FLOPPY.ST0 = 0x40 | ((FLOPPY.ST0 & 0x3B) | drive) | ((FLOPPY.currentphysicalhead[drive] & 1) << 2); //Abnormal termination! ST0!
				FLOPPY.resultbuffer[1] = FLOPPY.ST1;
				FLOPPY.resultbuffer[2] = FLOPPY.ST2;
				FLOPPY.resultbuffer[3] = FLOPPY.currentcylinder[drive]; //Error cylinder!
				FLOPPY.resultbuffer[4] = FLOPPY.currenthead[drive]; //Error head!
				FLOPPY.resultbuffer[5] = FLOPPY.currentsector[drive]; //Error sector!
				FLOPPY.resultbuffer[6] = FLOPPY.commandbuffer[2]; //Sector size from the command buffer!
				FLOPPY.commandstep = 3; //Move to result phrase and give the result!
				FLOPPY_raiseIRQ(); //Entering result phase!
				return; //Abort!
			}

			if ((FLOPPY.commandbuffer[3] != FLOPPY.geometries[drive]->SPT) && (!getIMDimage((drive)?FLOPPY1:FLOPPY0))) //Invalid SPT?
			{
				floppy_common_sectoraccess_nomedia(drive); //No media!
				return;
			}

			//Reset ST0 to have proper values!
			FLOPPY_ST0_UNITCHECKW(0); //Not faulted!
			FLOPPY_ST0_NOTREADYW(0); //Ready!
			FLOPPY_ST0_INTERRUPTCODEW(0); //OK! Correctly executed!
			FLOPPY_ST0_CURRENTHEADW(FLOPPY.currentphysicalhead[drive] & 1); //Head!
			FLOPPY_ST0_UNITSELECTW(drive); //Unit selected!

			FLOPPY.ST1 = 0; //Reset!
			FLOPPY.ST2 = 0; //Reset!

			if ((DSKImageFile = getDSKimage((drive) ? FLOPPY1 : FLOPPY0)) || (IMDImageFile = getIMDimage((drive) ? FLOPPY1 : FLOPPY0))) //Are we a DSK image file?
			{
				FLOPPY.databuffersize = 4; //We're 4 bytes per sector!
				FLOPPY_startData(drive); //Start the data transfer!
			}
			else //Normal standard emulated sector?
			{
				if (FLOPPY.commandbuffer[2] != 0x2) //Not 512 bytes/sector?
				{
					floppy_common_sectoraccess_nomedia(drive); //No media!
					return;
				}
				else
				{
					FLOPPY.databuffersize = 4; //We're 4 bytes per sector!
					FLOPPY_startData(drive); //Start the data transfer!
				}
			}

			if (FLOPPY.commandbuffer[3] == 0) //Nothing to transfer?
			{
				//Make any DMA transfer stop immediately, for applying the result phase using the timer instead!
				FLOPPY.DMAPending |= 2; //We're not pending anymore, until timed out!
				floppytimer[FLOPPY.commandbuffer[1] & 3] = FLOPPY_DMA_TIMEOUT; //Time the timeout for floppy! Take it 4 times as high to simulate one sector using the rate conversion for 4 bytes being processed at once(instead of in 1/4th the time for the entire IDX search)!
				floppytimer[FLOPPY.commandbuffer[1] & 3] *= 512.0; //times 512 sector byte times for a full sector to be transferred!
				floppytiming |= (1 << (FLOPPY.commandbuffer[1] & 3)); //Make sure we're timing on the specified disk channel!
				floppytime[FLOPPY.commandbuffer[1] & 3] = 0.0;
			}
			break;
		case VERSION: //Version command?
			FLOPPY.resultposition = 0; //Start our result phase!
			FLOPPY.resultbuffer[0] = 0x90; //We're a 82077AA!
			FLOPPY.commandstep = 3; //We're starting the result phase!
			break;
		case CONFIGURE: //Configuration command?
			//Load our 3 parameters for usage!
			FLOPPY.Configuration.data[0] = FLOPPY.commandbuffer[1];
			FLOPPY.Configuration.data[1] = FLOPPY.commandbuffer[2];
			FLOPPY.Configuration.data[2] = FLOPPY.commandbuffer[3];
			FLOPPY.commandstep = 0; //Finish silently! No result bytes or interrupt!
			FLOPPY_lowerIRQ(); //Lower the IRQ anyways!
			break;
		case LOCK: //Lock command?
			FLOPPY.Locked = FLOPPY.MT; //Set/unset the lock depending on the MT bit!
			FLOPPY.resultposition = 0; //Start our result phase!
			FLOPPY.resultbuffer[0] = (FLOPPY.Locked<<4); //Give the lock bit as a result!
			FLOPPY.commandstep = 3; //We're starting the result phase!
			break;
		case DUMPREG: //Dumpreg command
			FLOPPY.resultposition = 0; //Start our result phase!
			FLOPPY.resultbuffer[0] = FLOPPY.currentcylinder[0]; //Give the cylinder as a result!
			FLOPPY.resultbuffer[1] = FLOPPY.currentcylinder[1]; //Give the cylinder as a result!
			FLOPPY.resultbuffer[2] = FLOPPY.currentcylinder[2]; //Give the cylinder as a result!
			FLOPPY.resultbuffer[3] = FLOPPY.currentcylinder[3]; //Give the cylinder as a result!
			FLOPPY.resultbuffer[4] = FLOPPY.DriveData[FLOPPY_DOR_DRIVENUMBERR].data[0]; //Give the cylinder as a result!
			FLOPPY.resultbuffer[5] = FLOPPY.DriveData[FLOPPY_DOR_DRIVENUMBERR].data[1]; //Give the cylinder as a result!
			if (FLOPPY.geometries[FLOPPY_DOR_DRIVENUMBERR] && ((FLOPPY_DOR_DRIVENUMBERR < 2) ? (is_mounted(FLOPPY_DOR_DRIVENUMBERR ? FLOPPY1 : FLOPPY0)) : 0))
			{
				FLOPPY.resultbuffer[6] = (byte)((FLOPPY.geometries[FLOPPY_DOR_DRIVENUMBERR]->SPT)&0xFF); //Give the cylinder size in sectors as a result!
			}
			else
			{
				FLOPPY.resultbuffer[6] = 0; //Give the sectors/track!
			}
			FLOPPY.resultbuffer[7] = (FLOPPY.Locked ? 0x80 : 0x00) | (FLOPPY.PerpendicularMode & 0x7F); //Locked and perpendicular!
			FLOPPY.resultbuffer[8] = FLOPPY.Configuration.data[1]; //Configure second parameter byte!
			FLOPPY.resultbuffer[9] = FLOPPY.Configuration.data[2]; //Configure third parameter byte!
			FLOPPY.commandstep = 3; //We're starting the result phase!
			break;
		case PERPENDICULAR_MODE:	// * used during initialization, once, maybe
			//What perpendicular mode! Bits 0=WGATE, 1=GAP, 2-5=Drives 0-3(D0-D3), 7=OW!
			//OW specifies if bits D0-D3 are actually overwriting the D0-D3 to be set or not! 0=Don't overwrite, 1=Overwrite!
			//WGATE enables perpendicular mode. Gap then selects between 500Kb and 1Mb if enabled. D0=D3 force the perpendicular mode on for said drive?
			if ((FLOPPY.commandbuffer[1] & 0x80) == 0) //Only overwrite D0-D3 when bit 7 is set!
			{
				FLOPPY.PerpendicularMode = ((FLOPPY.commandbuffer[1] & ~0x3C) | (FLOPPY.PerpendicularMode & 0x3C)); //Overwrite all but D0-D3!
			}
			else //Normal setting, overwriting D0-D3, but not OW!
			{
				FLOPPY.PerpendicularMode = (FLOPPY.commandbuffer[1]&0x7F); //Don't write the OW bit!
			}
			
			FLOPPY.ST0 = 0x00 | (FLOPPY.ST0 & 0x38) | FLOPPY_DOR_DRIVENUMBERR | (FLOPPY.currentphysicalhead[FLOPPY_DOR_DRIVENUMBERR] << 2); //OK!
			FLOPPY.commandstep = 0; //Ready for a new command!
			//No interrupt!
			FLOPPY_lowerIRQ(); //Lower the IRQ anyways!
			break;
		default: //Unknown command?
			FLOPPY_lowerIRQ(); //Lower the IRQ anyways!
			FLOPPY.commandstep = 0xFF; //Move to error phrase!
			FLOPPY.ST0 = 0x80; //Invalid command!
			floppy_erroringout(); //Erroring out!
			break;
	}
}

OPTINLINE void floppy_abnormalpolling()
{
	FLOPPY_LOGD("FLOPPY: Abnormal termination because of abnormal polling!")
	FLOPPY_ST0_INTERRUPTCODEW(3); //Abnormal termination by polling!
	FLOPPY_ST0_NOTREADYW(0); //We became not ready!
	FLOPPY.commandstep = 0xFF; //Error!
	floppy_erroringout(); //Erroring out!
	FLOPPY_raiseIRQ(); //Raise an IRQ because of the error!
}

OPTINLINE void floppy_scanbyte(byte fdcbyte, byte cpubyte)
{
	//Bit 2=Seek error(ST3)
	//Bit 3=Seek equal(ST3)
	if ((FLOPPY.ST2&0xC)!=8) return; //Don't do anything on mismatch!
	if ((fdcbyte==cpubyte) || (fdcbyte==0xFF) || (cpubyte==0xFF)) return; //Bytes match?
	//Bytes do differ!
	switch (FLOPPY.commandbuffer[0]) //What kind of mismatch?
	{
	case SCAN_EQUAL: //Equal mismatch?
		FLOPPY_ST2_SEEKERRORW(1); //Seek error!
		FLOPPY_ST2_SEEKEQUALW(0); //Not equal!
		break;
	case SCAN_LOW_OR_EQUAL: //Low or equal mismatch?
		if (fdcbyte<cpubyte)
		{
			FLOPPY_ST2_SEEKERRORW(0);
			FLOPPY_ST2_SEEKEQUALW(0);
		}
		if (fdcbyte>cpubyte)
		{
			FLOPPY_ST2_SEEKERRORW(1);
			FLOPPY_ST2_SEEKEQUALW(0);
		}
		break;
	case SCAN_HIGH_OR_EQUAL: //High or equal mismatch?
		if (fdcbyte<cpubyte)
		{
			FLOPPY_ST2_SEEKERRORW(1);
			FLOPPY_ST2_SEEKEQUALW(0);
		}
		if (fdcbyte>cpubyte)
		{
			FLOPPY_ST2_SEEKERRORW(0);
			FLOPPY_ST2_SEEKEQUALW(0);
		}
		break;
	default:
		break;
	}
}

OPTINLINE void floppy_writeData(byte isDMA, byte value)
{
	byte isscan = 0; //We're not scanning something by default!
	byte commandlength[0x20] = {
		0, //0
		0, //1
		8, //2
		2, //3
		1, //4
		8, //5
		8, //6
		1, //7
		0, //8
		8, //9
		1, //A
		0, //B
		8, //C
		5, //D
		0, //E
		2 //F
		,0 //10
		,8 //11
		,1 //12
		,3 //13
		,1 //14
		,0 //15
		,8 //16
		,0 //17
		,0 //18
		,8 //19
		,0 //1A
		,0 //1B
		,0 //1C
		,8 //1D
		,0 //1E
		,0 //1F
		};
	isscan = 0; //Init scan type!
	switch (FLOPPY.commandstep) //What step are we at?
	{
		case 0: //Command
			if (isDMA) //DMA? Error out!
			{
				floppy_abnormalpolling(); //Abnormal polling!
				return; //Error out!
			}
			if (FLOPPY.ignorecommands) return; //Ignore commands: we're locked up!
			FLOPPY.commandstep = 1; //Start inserting parameters!
			FLOPPY.commandposition = 1; //Start at position 1 with out parameters/data!
			FLOPPY_LOGD("FLOPPY: Command byte sent: %02X", value) //Log our information about the command byte!
			FLOPPY.MT = (value & CMD_EXT_MULTITRACKOPERATION)?1:0; //Multiple track mode?
			FLOPPY.MFM = (value & CMD_EXT_MFMMODE)?1:0; //MFM(Double Density)/Seek direction mode?
			FLOPPY.Skip = (value & CMD_EXT_SKIPDELETEDADDRESSMARKS)?1:0; //Multiple track mode?
			FLOPPY.MTMask = 1; //Default: allow the MT bit to be applied during sector calculations!
			value &= 0x1F; //Make sure that the high data is filtered out!
			floppytimer[4] = (DOUBLE)0; //Disable the floppy reset timer!
			floppytiming &= ~0x10; //Clear the reset timer!
			switch (value) //What command?
			{
				case DUMPREG: //Dumpreg command
				case VERSION: //Version
				case LOCK: //Lock
					FLOPPY.reset_pending = 0; //Stop pending reset if we're pending it: we become active!
					if (FLOPPY.reset_pended) //Finished reset?
					{
						FLOPPY_LOGD("FLOPPY: Reset for all drives has been finished!");
						FLOPPY.ST0 &= 0x20; //Reset the ST0 register after we've all been read!
						FLOPPY.reset_pended = 0; //Not pending anymore, so don't check for it!
					}
				case SENSE_INTERRUPT: //Check interrupt status
					FLOPPY.commandbuffer[0] = value; //Set the command to use!
					floppy_executeCommand(); //Execute the command!
					break;
				case READ_TRACK: //Read complete track
				case FORMAT_TRACK: //Format track
					FLOPPY.MTMask = 0; //Don't allow the MT bit to be applied during sector calculations!
				case WRITE_DATA: //Write sector
				case WRITE_DELETED_DATA: //Write deleted sector
				case READ_DATA: //Read sector
				case VERIFY: //Verify
				case READ_DELETED_DATA: //Read deleted sector
				case SPECIFY: //Fix drive data
				case SENSE_DRIVE_STATUS: //Check drive status
				case RECALIBRATE: //Calibrate drive
				case SEEK: //Seek/park head
				case READ_ID: //Read sector ID
				case CONFIGURE: //Configure
				case SCAN_EQUAL:
				case SCAN_LOW_OR_EQUAL:
				case SCAN_HIGH_OR_EQUAL:
				case PERPENDICULAR_MODE:	// * used during initialization, once, maybe
					if (is_XT) //XT somehow needs lowering of the IRQ?
					{
						FLOPPY_hadIRQ = FLOPPY.IRQPending; //Was an IRQ Pending?
						FLOPPY_lowerIRQ(); //Lower the IRQ!
					}
					FLOPPY.reset_pending = 0; //Stop pending reset if we're pending it: we become active!
					if (FLOPPY.reset_pended) //Finished reset?
					{
						FLOPPY_LOGD("FLOPPY: Reset for all drives has been finished!");
	 					FLOPPY.ST0 &= 0x20; //Reset the ST0 register after we've all been read!
						FLOPPY.reset_pended = 0; //Not pending anymore, so don't check for it!
					}
					FLOPPY.commandbuffer[0] = value; //Set the command to use!
					break;
				default: //Invalid command
					FLOPPY_LOGD("FLOPPY: Invalid or unsupported command: %02X",value); //Detection of invalid/unsupported command!
					FLOPPY.ST0 = 0x80; //Invalid command!
					FLOPPY.commandstep = 0xFF; //Error: lockup!
					floppy_erroringout(); //Erroring out!
					break;
			}
			break;
		case 1: //Parameters
			if (isDMA) //DMA? Error out!
			{
				floppy_abnormalpolling(); //Abnormal polling!
				return; //Error out!
			}
			FLOPPY_LOGD("FLOPPY: Parameter sent: %02X(#%u/%u)", value, FLOPPY.commandposition, commandlength[FLOPPY.commandbuffer[0]]) //Log the parameter!
			FLOPPY.commandbuffer[FLOPPY.commandposition++] = value; //Set the command to use!
			if (FLOPPY.commandposition > (commandlength[FLOPPY.commandbuffer[0]])) //All parameters have been processed?
			{
				floppy_executeCommand(); //Execute!
				break;
			}
			break;
		case 2: //Data
			switch (FLOPPY.commandbuffer[0]) //What command?
			{
				case SCAN_EQUAL:
				case SCAN_LOW_OR_EQUAL:
				case SCAN_HIGH_OR_EQUAL:
					if (FLOPPY_useDMA() != isDMA) //Wrong type!
					{
						goto floppy_abnormalpollingwrite; //Abnormal polling used!
					}
					if (isDMA == 0) //Non-DMA?
					{
						FLOPPY_hadIRQ = FLOPPY.IRQPending; //Was an IRQ Pending?
						FLOPPY_lowerIRQ(); //Lower the IRQ!
					}
					isscan = 1; //We're scanning instead!
					floppy_scanbyte(FLOPPY.databuffer[FLOPPY.databufferposition++],value); //Execute the scanning!
					goto skipDMAwritecheck; //Skip the below DMA check!
				case WRITE_DATA: //Write sector
				case WRITE_DELETED_DATA: //Write deleted sector
				case FORMAT_TRACK: //Format track
					if (FLOPPY_useDMA() != isDMA) //Wrong type!
					{
						goto floppy_abnormalpollingwrite; //Abnormal polling used!
					}
					if (isDMA == 0) //Non-DMA?
					{
						FLOPPY_hadIRQ = FLOPPY.IRQPending; //Was an IRQ Pending?
						FLOPPY_lowerIRQ(); //Lower the IRQ!
					}
					skipDMAwritecheck:
					if (likely(isscan==0)) //Not Scanning? We're writing to the buffer!
					{
						FLOPPY.databuffer[FLOPPY.databufferposition++] = value; //Set the command to use!
					}
					if (FLOPPY.databufferposition==FLOPPY.databuffersize) //Finished?
					{
						floppy_executeData(); //Execute the command with the given data!
					}
					else //Not completed?
					{
						FLOPPY_dataReady(); //We have data ready to transfer!
						if (FLOPPY_useDMA() && FLOPPY.TC) //DMA mode, Terminal count and not completed? We're ending too soon!
						{
							FLOPPY_LOGD("FLOPPY: Terminal count reached in the middle of a data transfer! Position: %u/%u bytes",FLOPPY.databufferposition,FLOPPY.databuffersize)
							floppy_executeData(); //Execute the command with the given data!
						}
					}
					break;
				default: //Invalid command
					floppy_abnormalpollingwrite:
					floppy_abnormalpolling(); //Abnormal polling!
					break;
			}
			break;
		case 3: //Result
			floppy_abnormalpolling();
			break; //We don't write during the result phrase!
		case 0xFF: //Error
			floppy_abnormalpolling();
			//We can't do anything! Ignore any writes now!
			break;
		case 0xFD: //Give the result and lockup?
			//Ignore DMA writes for the error to be given!
		case 0xFE: //Locked up?
		default:
			break; //Unknown status, hang the controller or do nothing!
	}
}

OPTINLINE byte floppy_readData(byte isDMA)
{
	byte resultlength[0x20] = {
		0, //0
		0, //1
		7, //2
		0, //3
		1, //4
		7, //5
		7, //6
		0, //7
		2, //8
		7, //9
		7, //a
		0, //b
		7, //c
		7, //d
		10, //e
		7, //f
		1, //10
		7, //11
		0, //12
		0, //13
		1, //14
		0, //15
		7, //16
		0, //17
		0, //18
		7, //19
		0, //1a
		0, //1b
		0, //1c
		7, //1d
		0, //1e
		0 //1f
	};
	byte temp;
	switch (FLOPPY.commandstep) //What step are we at?
	{
		case 0: //Command
			if (FLOPPY.ignorecommands) return 0; //Ignore commands: we're locked up!
			floppy_abnormalpolling(); //Abnormal polling!
			break; //Nothing to read during command phrase!
		case 1: //Parameters
			floppy_abnormalpolling(); //Abnormal polling!
			break; //Nothing to read during parameter phrase!
		case 2: //Data
			switch (FLOPPY.commandbuffer[0]) //What command?
			{
				case READ_TRACK: //Read complete track
				case READ_DATA: //Read sector
				case READ_DELETED_DATA: //Read deleted sector
					if (FLOPPY_useDMA()!=isDMA) //Non-DMA mode addressing DMA or reversed?
					{
						goto abnormalpollingread;
					}
					if (isDMA == 0) //Non-DMA?
					{
						FLOPPY_hadIRQ = FLOPPY.IRQPending; //Was an IRQ Pending?
						FLOPPY_lowerIRQ(); //Lower the IRQ!
					}
					temp = FLOPPY.databuffer[FLOPPY.databufferposition++]; //Read data!
					if (FLOPPY.databufferposition==FLOPPY.databuffersize) //Finished?
					{
						floppy_executeData(); //Execute the data finished phrase!
					}
					else //Not completed?
					{
						FLOPPY_dataReady(); //We have data ready to transfer!
						if (FLOPPY_useDMA() && FLOPPY.TC) //DMA mode, Terminal count and not completed? We're ending too soon!
						{
							FLOPPY_LOGD("FLOPPY: Terminal count reached in the middle of a data transfer! Position: %u/%u bytes",FLOPPY.databufferposition,FLOPPY.databuffersize)
							floppy_executeData(); //Execute the command with the given data!
						}
					}
					return temp; //Give the result!
					break;
				default: //Invalid command: we have no data to be READ!
					abnormalpollingread:
					floppy_abnormalpolling(); //Abnormal polling!
					break;
			}
			break;
		case 3: //Result
			if (isDMA) //DMA? Error out because we're overrunning the access!
			{
				floppy_abnormalpolling(); //Abnormal polling detected!
				return 0; //Error out!
			}
			temp = FLOPPY.resultbuffer[FLOPPY.resultposition++]; //Read a result byte!
			switch (FLOPPY.commandbuffer[0]) //What command?
			{
				//Only a few result phases generate interrupts!
				case READ_TRACK: //Read complete track
				case WRITE_DATA: //Write sector
				case READ_DATA: //Read sector
				case WRITE_DELETED_DATA: //Write deleted sector
				case READ_DELETED_DATA: //Read deleted sector
				case FORMAT_TRACK: //Format sector
				case READ_ID: //Read sector ID
				case SCAN_EQUAL:
				case SCAN_LOW_OR_EQUAL:
				case SCAN_HIGH_OR_EQUAL:
					//Lower the interrupt for these!
					FLOPPY_hadIRQ = FLOPPY.IRQPending; //Was an IRQ Pending?
					FLOPPY_lowerIRQ(); //Lower the IRQ!
				case SENSE_DRIVE_STATUS: //Check drive status
				case SENSE_INTERRUPT: //Check interrupt status
				case VERSION: //Version information!
				case VERIFY:
				case DUMPREG:
				case LOCK:
					FLOPPY_LOGD("FLOPPY: Reading result byte %u/%u=%02X",FLOPPY.resultposition,resultlength[FLOPPY.commandbuffer[0]&0x1F],temp)
					if (FLOPPY.resultposition>=resultlength[FLOPPY.commandbuffer[0]]) //Result finished?
					{
						FLOPPY.commandstep = 0; //Reset step!
					}
					return temp; //Give result value!
					break;
				default: //Invalid command to read!
					floppy_abnormalpolling(); //Abnormal polling!
					break;
			}
			break;
		case 0xFD: //Give result and lockup?
		case 0xFF: //Error or reset result
			if (isDMA) //DMA? Error out!
			{
				return 0; //Error out: nothing to give until the result is fetched!
			}
			FLOPPY_hadIRQ = FLOPPY.IRQPending; //Was an IRQ Pending?
			FLOPPY_lowerIRQ(); //Lower the IRQ!
			if (FLOPPY.commandstep==0xFD) //Lock up now?
			{
				FLOPPY.commandstep = 0xFE; //New command?
			}
			else //Reset?
			{
				FLOPPY.commandstep = 0; //Reset step!
			}
			return FLOPPY.ST0; //Give ST0, containing an error!
			break;
		default:
			break; //Unknown status, hang the controller!
	}
	return ~0; //Not used yet!
}

void FLOPPY_finishrecalibrate(byte drive)
{
	//Execute interrupt!
	FLOPPY.currentcylinder[drive] = 0; //Goto cylinder #0 according to the FDC!
	FLOPPY.ST0 = 0x20|drive|(FLOPPY.currentphysicalhead[drive] << 2); //Completed command!
	updateST3(drive); //Update ST3 only!
	if (((FLOPPY_DOR_MOTORCONTROLR&(1<<(drive&3)))==0) || ((drive&3)>1) || (FLOPPY.physicalcylinder[drive]!=0)) //Motor not on or invalid drive?
	{
		FLOPPY.ST0 |= 0x50; //Completed command! 0x10: Unit Check, cannot find track 0 after 79 pulses.
	}
	FLOPPY_raiseIRQ(); //We're finished!
	FLOPPY.IRQPending = 2; //Force pending!
	FLOPPY_MSR_BUSYINPOSITIONINGMODEW(drive,0); //Not seeking anymore!
	floppytimer[drive] = 0.0; //Don't time anymore!
}

void FLOPPY_finishseek(byte drive, byte finishIRQ)
{
	FLOPPY.ST0 = 0x20 | (FLOPPY.currentphysicalhead[drive] << 2) | drive; //Valid command!
	if (((FLOPPY_DOR_MOTORCONTROLR & (1 << (drive & 3))) == 0) || ((drive & 3) > 1)) //Motor not on or invalid drive(which can't finish the seek correctly and provide the signal for completion)?
	{
		FLOPPY.ST0 |= 0x50; //Completed command! 0x10: Unit Check, cannot find track 0 after 79 pulses.
	}
	updateST3(drive); //Update ST3 only!
	if (finishIRQ) //Finishing with IRQ?
	{
		FLOPPY_raiseIRQ(); //Finished executing phase!
		FLOPPY.IRQPending = 2; //Force pending!
	}
	floppytimer[drive] = 0.0; //Don't time anymore!
	FLOPPY_MSR_BUSYINPOSITIONINGMODEW(drive,0); //Not seeking anymore!
}

void FLOPPY_checkfinishtiming(byte drive)
{
	if (!floppytimer[drive]) //Finished timing?
	{
		floppytime[drive] = (DOUBLE)0; //Clear the remaining time!
		floppytiming &= ~(1<<drive); //We're not timing anymore on this drive!
	}
}

//Timed floppy disk operations!
void updateFloppy(DOUBLE timepassed)
{
	byte drive=0; //Drive loop!
	byte movedcylinder;
	if (unlikely(floppytiming)) //Are we timing?
	{
		do
		{
			if (floppytimer[drive]) //Are we timing?
			{
				floppytime[drive] += timepassed; //We're measuring time!
				for (;(floppytime[drive]>=floppytimer[drive]) && floppytimer[drive];) //Timeout and still timing?
				{
					floppytime[drive] -= floppytimer[drive]; //Time some!
					if (drive == 4) //Reset line timer?
					{
						FLOPPY_raiseIRQ(); //Raise the IRQ: We're reset and have been activated!
						FLOPPY.floppy_resetted = 0; //Not resetted anymore!
						floppytimer[drive] = 0.0; //Don't time anymore!
						goto finishdrive;
					}
					else if ((FLOPPY.erroringtiming & (1<<drive)) && ((FLOPPY.activecommand[drive]!=READ_ID) && (FLOPPY.activecommand[drive]!=VERIFY))) //Timing error?
					{
						FLOPPY.commandstep = 3; //Move to result phrase and give the result!
						FLOPPY_raiseIRQ(); //Entering result phase!
						floppytimer[drive] = 0.0; //Don't time anymore!
						goto finishdrive;
					}
					else switch (FLOPPY.activecommand[drive]) //What command is processing?
					{
						case READ_TRACK: //Read track
						case READ_DATA: //Read sector
						case SCAN_EQUAL:
						case SCAN_LOW_OR_EQUAL:
						case SCAN_HIGH_OR_EQUAL:
							if (FLOPPY_MSR_BUSYINPOSITIONINGMODER(drive)) //To handle implied seek?
							{
								goto handleimpliedseek;
							}
							goto normalreadwritetrack;
						case SEEK: //Seek/park head
							handleimpliedseek:
							if ((drive>=2) || (!FLOPPY.geometries[drive])) //Floppy not inserted?
							{
								FLOPPY.readID_lastsectornumber = 0; //New track has been selected, search again!
								FLOPPY.ST0 = 0x20 | (FLOPPY.currentphysicalhead[drive]<<2) | drive; //Error: drive not ready!
								FLOPPY.IRQPending = 2; //Force pending!
								floppytimer[drive] = 0.0; //Don't time anymore!
								FLOPPY_MSR_BUSYINPOSITIONINGMODEW(drive,0); //Not seeking anymore!

								switch (FLOPPY.activecommand[drive]) //Where to pick up?
								{
									case VERIFY: goto seekedverify;
									case FORMAT_TRACK: goto seekedformat;
									case READ_TRACK: //Read track
									case READ_DATA: //Read sector
									case READ_DELETED_DATA: //Read deleted sector
									case SCAN_EQUAL:
									case SCAN_LOW_OR_EQUAL:
									case SCAN_HIGH_OR_EQUAL:
										floppy_readsector_failresult(); //Fail read sector!
										goto finishdrive;
										break;
									case WRITE_DATA: //Write sector
									case WRITE_DELETED_DATA: //Write deleted sector
										floppy_writesector_failresult(); //Fail write sector!
										goto finishdrive;
										break;
									default: //Unknown or non-implied seek?
										//NOP!
										break;
								}
								FLOPPY_raiseIRQ(); //Finished executing phase!
								goto finishdrive;
							}
						
							if ((FLOPPY.currentcylinder[drive]>FLOPPY.seekdestination[drive] && (FLOPPY.seekrel[drive]==0)) || (FLOPPY.seekrel[drive] && (FLOPPY.seekrelup[drive]==0) && FLOPPY.seekdestination[drive])) //Step out towards smaller cylinder numbers?
							{
								updateFloppyGeometries(drive, FLOPPY.currentphysicalhead[drive], FLOPPY.physicalcylinder[drive]); //Up
								if (FLOPPY.geometries[drive])
								{
									FLOPPY.readID_lastsectornumber = FLOPPY.geometries[drive]->SPT + 1; //Act like the track has changed!
								}
								else
								{
									FLOPPY.readID_lastsectornumber = 0; //Act like the track has changed!
								}
								--FLOPPY.currentcylinder[drive]; //Step up!
								if (FLOPPY.physicalcylinder[drive]) --FLOPPY.physicalcylinder[drive]; //Decrease when available!
								movedcylinder = 1;
							}
							else if ((FLOPPY.currentcylinder[drive]<FLOPPY.seekdestination[drive] && (FLOPPY.seekrel[drive]==0)) || (FLOPPY.seekrel[drive] && FLOPPY.seekrelup[drive] && FLOPPY.seekdestination[drive])) //Step in towards bigger cylinder numbers?
							{
								updateFloppyGeometries(drive, FLOPPY.currentphysicalhead[drive], FLOPPY.physicalcylinder[drive]); //Up
								if (FLOPPY.geometries[drive])
								{
									FLOPPY.readID_lastsectornumber = FLOPPY.geometries[drive]->SPT + 1; //Act like the track has changed!
								}
								else
								{
									FLOPPY.readID_lastsectornumber = 0; //Act like the track has changed!
								}
								++FLOPPY.currentcylinder[drive]; //Step down!
								if (FLOPPY.geometries[drive])
								{
									if (FLOPPY.physicalcylinder[drive] < (FLOPPY.geometries[drive]->tracks-1)) ++FLOPPY.physicalcylinder[drive]; //Increase when available!
								}
								movedcylinder = 1;
							}
							else movedcylinder = 0; //We didn't move?

							updateST3(drive); //Update ST3 only!

							//Check if we're there!
							if ((drive<2) && (((FLOPPY.currentcylinder[drive]==FLOPPY.seekdestination[drive]) && (FLOPPY.currentcylinder[drive] < FLOPPY.geometries[drive]->tracks) && (FLOPPY.seekrel[drive]==0)) || (FLOPPY.seekrel[drive] && (FLOPPY.seekdestination[drive]==0)))) //Found and existant?
							{
								FLOPPY_finishseek(drive,(FLOPPY.activecommand[drive]==SEEK)?1:0); //Finish up as needed, with IRQ if a SEEK command!
								if (FLOPPY.activecommand[drive]!=SEEK) //Not a seek command? Implied seek!
								{
									switch (FLOPPY.activecommand[drive]) //Where to pick up?
									{
										case VERIFY: goto seekedverify;
										case FORMAT_TRACK: goto seekedformat;
										case READ_TRACK: //Read track
										case READ_DATA: //Read sector
										case READ_DELETED_DATA: //Read deleted sector
										case SCAN_EQUAL:
										case SCAN_LOW_OR_EQUAL:
										case SCAN_HIGH_OR_EQUAL:
											floppy_readsector(); //Perform read sector again!
											break;
										case WRITE_DATA: //Write sector
										case WRITE_DELETED_DATA: //Write deleted sector
											floppy_writesector(); //Perform write sector again!
											break;
										default: //Unknown implied seek?
											//NOP!
											break;
									}
								}
								goto finishdrive; //Give an error!
							}
							else if (movedcylinder==0) //Reached no destination?
							{
								//invalidtrackseek:
								//Invalid track?
								FLOPPY.ST0 = (FLOPPY.ST0 & 0x30) | 0x00 | drive | (FLOPPY.currentphysicalhead[drive]<<2); //Valid command! Just don't report completion(invalid track to seek to)!
								FLOPPY.ST2 = 0x00; //Nothing to report! We're not completed!
								FLOPPY.IRQPending = 2; //Force pending!
								floppytimer[drive] = 0.0; //Don't time anymore!
								FLOPPY_MSR_BUSYINPOSITIONINGMODEW(drive,0); //Not seeking anymore!

								switch (FLOPPY.activecommand[drive]) //Where to pick up?
								{
									case VERIFY: goto seekedverify;
									case FORMAT_TRACK: goto seekedformat;
									case READ_TRACK: //Read track
									case READ_DATA: //Read sector
									case READ_DELETED_DATA: //Read deleted sector
									case SCAN_EQUAL:
									case SCAN_LOW_OR_EQUAL:
									case SCAN_HIGH_OR_EQUAL:
										floppy_readsector_failresult(); //Fail read sector!
										goto finishdrive;
										break;
									case WRITE_DATA: //Write sector
									case WRITE_DELETED_DATA: //Write deleted sector
										floppy_writesector_failresult(); //Fail write sector!
										goto finishdrive;
										break;
									default: //Unknown implied seek?
										//NOP!
										break;
								}
								FLOPPY_raiseIRQ(); //Finished executing phase!
								goto finishdrive;
							}
							break;
						case RECALIBRATE: //Calibrate drive
							if (FLOPPY.physicalcylinder[drive] && (drive<2)) //Not there yet?
							{
								--FLOPPY.physicalcylinder[drive]; //Step down!
								updateFloppyGeometries(drive, FLOPPY.currentphysicalhead[drive], FLOPPY.physicalcylinder[drive]); //Up
								if (FLOPPY.geometries[drive])
								{
									FLOPPY.readID_lastsectornumber = FLOPPY.geometries[drive]->SPT + 1; //Act like the track has changed!
								}
								else
								{
									FLOPPY.readID_lastsectornumber = 0; //Act like the track has changed!
								}
							}
							if (((FLOPPY.physicalcylinder[drive]) || (drive>=2)) && FLOPPY.recalibratestepsleft[drive]) //Not there yet?
							{
								--FLOPPY.recalibratestepsleft[drive];
							}
							else //Finished? Track 0 might be found!
							{
								FLOPPY_finishrecalibrate(drive); //Finish us!
								goto finishdrive;
							}
							break;
						case VERIFY: //Executing verify validation of data?
							if (FLOPPY_MSR_BUSYINPOSITIONINGMODER(drive)) //To handle implied seek?
							{
								goto handleimpliedseek;
							}
							seekedverify:
							++FLOPPY.databufferposition; //Read data!
							if (FLOPPY.databufferposition==FLOPPY.databuffersize) //Finished?
							{
								floppytimer[drive] = 0.0; //Don't time anymore!
								floppy_executeData(); //Execute the data finished phrase!
								goto finishdrive; //Finish!
							}
							//Continue while busy!
							break;
						case READ_ID: //Read ID command?
							++FLOPPY.databufferposition; //Read data!
							if (FLOPPY.databufferposition==FLOPPY.databuffersize) //Finished?
							{
								//Start the result phase for the command!
								if (FLOPPY.readIDerror == 0) //Success?
								{
									FLOPPY.resultposition = 0; //Start the result!
									FLOPPY_fillST0(FLOPPY.readIDdrive); //Setup ST0!
									FLOPPY.resultbuffer[0] = FLOPPY.ST0; //ST0!
									FLOPPY.resultbuffer[1] = FLOPPY.ST1; //ST1!
									FLOPPY.resultbuffer[2] = FLOPPY.ST2; //ST2!
									FLOPPY.commandstep = 3; //Result phase!
									FLOPPY_raiseIRQ(); //Entering result phase!
								}
								else //Error?
								{
									FLOPPY.ST0 |= 0x40; //Error!
									FLOPPY_ST1_NOADDRESSMARKW(1);
									FLOPPY_ST1_NODATAW(1); //Invalid sector!
									FLOPPY.resultposition = 0; //Start the result!
									FLOPPY_fillST0(FLOPPY.readIDdrive); //Setup ST0!
									FLOPPY.resultbuffer[0] = FLOPPY.ST0; //ST0!
									FLOPPY.resultbuffer[1] = FLOPPY.ST1; //ST1!
									FLOPPY.resultbuffer[2] = FLOPPY.ST2; //ST2!
									FLOPPY.resultbuffer[3] = FLOPPY.physicalcylinder[drive]; //Cylinder!
									FLOPPY.resultbuffer[4] = FLOPPY.currenthead[drive]; //Head!
									FLOPPY.resultbuffer[5] = FLOPPY.currentsector[drive]; //Sector!
									FLOPPY.commandstep = 3; //Result phase!
									FLOPPY_raiseIRQ(); //Entering result phase!
								}
								//We're finished with this timing now!
							}
							else break; //Still processing?
						case FORMAT_TRACK: //Formatting track?
							if (FLOPPY_MSR_BUSYINPOSITIONINGMODER(drive)) //To handle implied seek?
							{
								goto handleimpliedseek;
							}
							seekedformat:
							if (FLOPPY.activecommand[drive] == FORMAT_TRACK) //Our command is timed(needed because of the above code)?
							{
								if (FLOPPY.commandstep != 2) //Not execute phase? We're not timing yet!
									break; //Don't finish us yet!
								else //Active command?
								{
									if (FLOPPY.commandbuffer[3] == 0) //Finish us because of the IDX line being raised?
									{
										FLOPPY_formatsector(1); //Execute the execution phase, immediate IDX line raised!
										//Let us finish the timer, so fall through!
									}
									//Let us finish normally! Handle DMA transfers normally!
								}
							}
						default: //Unsupported command?
							normalreadwritetrack:
							if ((FLOPPY.commandstep==2) && FLOPPY_useDMA() && (FLOPPY.DMAPending&2) && (drive==FLOPPY_DOR_DRIVENUMBERR)) //DMA transfer busy on this channel?
							{
								FLOPPY.DMAPending &= ~2; //Start up DMA again!
								floppytimer[drive] = FLOPPY_DMA_TIMEOUT; //How long for a DMA transfer to take?
							}
							else //Unsupported?
							{
								floppytimer[drive] = 0.0; //Don't time anymore!
								goto finishdrive;
							}
							break; //Don't handle us yet!
					}
				}
				finishdrive:
				FLOPPY_checkfinishtiming(drive); //Check for finished timing!
			}
		} while (++drive<5); //Process all drives and reset!
	}
}

byte getfloppydisktype(byte floppy)
{
	if (FLOPPY.geometries[floppy]) //Gotten a known geometry?
	{
		return FLOPPY.geometries[floppy]->boardjumpersetting; //Our board jumper settings for this drive!
	}
	return FLOPPYTYPE_12MB; //Default to the default AT controller to fit all!
}

byte PORT_IN_floppy(word port, byte *result)
{
	if ((port&~7) != 0x3F0) return 0; //Not our port range!
	byte temp;
	switch (port & 0x7) //What port?
	{
	case 0: //diskette EHD controller board jumper settings (82072AA)!
		//Officially only on AT systems, but use on XT as well for proper detection!
		//Create floppy flags!
		temp = getfloppydisktype(3); //Floppy #3!
		temp <<= 2;
		temp = getfloppydisktype(2); //Floppy #2!
		temp <<= 2;
		temp = getfloppydisktype(1); //Floppy #1!
		temp <<= 2;
		temp = getfloppydisktype(0); //Floppy #0!
		FLOPPY_LOGD("FLOPPY: Read port Diskette EHD controller board jumper settings=%02X",temp);
		*result = temp; //Give the result!
		return 1; //Used!
		break;
	//IBM PC XT supports DOR, MSR and Data ports. AT also supports DIR and CCR registers.
	case 2: //DOR?
		*result = FLOPPY.DOR; //Give the DOR!
		return 1; //Used!
		break;
	case 3: //Tape Drive register (82077AA)?
		temp = 0x20; //No drive present here by default!
		if (FLOPPY.geometries[FLOPPY_DOR_DRIVENUMBERR]) //Nothing there?
		{
			temp = FLOPPY.geometries[FLOPPY_DOR_DRIVENUMBERR]->TapeDriveRegister; //What format are we?
		}
		FLOPPY_LOGD("FLOPPY: Read port Tape Drive Register=%02X",temp);
		*result = temp; //Give the result!
		return 1; //Used!
		break;
	case 4: //MSR?
		updateFloppyMSR(); //Update the MSR with current values!
		FLOPPY_LOGD("FLOPPY: Read MSR=%02X",FLOPPY.MSR)
		*result = FLOPPY.MSR; //Give MSR!
		return 1;
	case 5: //Data?
		//Process data!
		*result = floppy_readData(0); //Read data!
		return 1;
	case 7: //DIR?
		if (is_XT==0) //AT?
		{
			updateFloppyDIR(); //Update the DIR register!
			FLOPPY_LOGD("FLOPPY: Read DIR=%02X", FLOPPY.DIR)
			*result = FLOPPY.DIR; //Give DIR!
			return 1;
		}
		break;
	default: //Unknown port?
		break;
	}
	//Not one of our ports?
	return 0; //Unknown port!
}

OPTINLINE void updateMotorControl()
{
	EMU_setDiskBusy(FLOPPY0, FLOPPY_DOR_MOTORCONTROLR & 1); //Are we busy?
	EMU_setDiskBusy(FLOPPY1, (FLOPPY_DOR_MOTORCONTROLR & 2) >> 1); //Are we busy?
}

byte PORT_OUT_floppy(word port, byte value)
{
	if ((port&~7) != 0x3F0) return 0; //Not our address range!
	switch (port & 0x7) //What port?
	{
	case 2: //DOR?
		FLOPPY_LOGD("FLOPPY: Write DOR=%02X", value)
		FLOPPY.DOR = value; //Write to register!
		updateMotorControl(); //Update the motor control!
		FLOPPY_handlereset(0); //Execute a reset by DOR!
		return 1; //Finished!
	case 4: //DSR?
		if (is_XT==0) //AT?
		{
			FLOPPY_LOGD("FLOPPY: Write DSR=%02X", value)
			FLOPPY.DSR = value; //Write to register to check for reset first!
			FLOPPY_handlereset(1); //Execute a reset by DSR!
			if (FLOPPY_DSR_SWRESETR) FLOPPY_DSR_SWRESETW(0); //Reset requested? Clear the reset bit automatically!
			FLOPPY_handlereset(1); //Execute a reset by DSR if needed!
			FLOPPY_CCR_RATEW(FLOPPY_DSR_DRATESELR); //Setting one sets the other!
			return 1; //Finished!
		}
		return 0; //Not handled!
		break;
	case 5: //Data?
		floppy_writeData(0,value); //Write data!
		return 1; //Default handler!
	case 7: //CCR?
		if (is_XT==0) //AT?
		{
			FLOPPY_LOGD("FLOPPY: Write CCR=%02X", value)
			FLOPPY.CCR = value; //Set CCR!
			FLOPPY_DSR_DRATESELW(FLOPPY_CCR_RATER); //Setting one sets the other!
			return 1;
		}
		break;
	default: //Unknown port?
		break; //Unknown port!
	}
	//Not one of our ports!
	return 0; //Unknown port!
}

//DMA logic

void DMA_floppywrite(byte data)
{
	floppy_writeData(1,data); //Send the data to the FDC!
}

byte DMA_floppyread()
{
	return floppy_readData(1); //Read data!
}

void FLOPPY_DMADREQ() //For checking any new DREQ signals!
{
	DMA_SetDREQ(FLOPPY_DMA, (FLOPPY.commandstep == 2) && FLOPPY_useDMA() && (FLOPPY.DMAPending==1)); //Set DREQ from hardware when in the data phase and using DMA transfers and not busy yet(pending)!
}

void FLOPPY_DMADACK() //For processing DACK signal!
{
	DMA_SetDREQ(FLOPPY_DMA,0); //Stop the current transfer!
	if (FLOPPY.DMAPending) //Pending?
	{
		FLOPPY.DMAPending |= 2; //We're not pending anymore, until timed out!
		floppytimer[FLOPPY_DOR_DRIVENUMBERR] = FLOPPY_DMA_TIMEOUT; //Time the timeout for floppy!
		floppytiming |= (1 << FLOPPY_DOR_DRIVENUMBERR); //Make sure we're timing on the specified disk channel!
	}
}

void FLOPPY_DMATC() //Terminal count triggered?
{
	FLOPPY.TC = 1; //Terminal count triggered!
}

byte FLOPPY_DMAEOP() //EOP triggered?
{
	return FLOPPY.TC; //Are we terminal count? Then we finish!
}

void initFDC()
{
	byte drive;
	density_forced = (is_XT==0); //Allow force density check if 286+ (non XT)!
	memset(&FLOPPY, 0, sizeof(FLOPPY)); //Initialise floppy!
	FLOPPY.Configuration.data[0] = 0; //Default!
	FLOPPY.Configuration.data[1] = 0x60; //Implied seek enable, FIFO disable, Drive polling mode enable, no treshold(0)
	FLOPPY.Configuration.data[2] = 0; //No write precompensation!

	//Initialise DMA controller settings for the FDC!
	DMA_SetDREQ(FLOPPY_DMA,0); //No DREQ!
	registerDMA8(FLOPPY_DMA, &DMA_floppyread, &DMA_floppywrite); //Register our DMA channels!
	registerDMATick(FLOPPY_DMA, &FLOPPY_DMADREQ, &FLOPPY_DMADACK, &FLOPPY_DMATC, &FLOPPY_DMAEOP); //Our handlers for DREQ, DACK and TC!

	//Set basic I/O ports
	register_PORTIN(&PORT_IN_floppy);
	register_PORTOUT(&PORT_OUT_floppy);
	register_DISKCHANGE(FLOPPY0, &FLOPPY_notifyDiskChanged);
	register_DISKCHANGE(FLOPPY1, &FLOPPY_notifyDiskChanged);

	memset(&floppytime,0,sizeof(floppytime));
	memset(&floppytimer,0,sizeof(floppytimer)); //No time spent or in-use by the floppy disk!
	floppytiming = 0; //We're not timing!
	initFloppyRates(); //Initialize the floppy disk rate tables to use!

	for (drive = 0; drive < 4; ++drive)
	{
		FLOPPY.DriveData[drive].headloadtime = FLOPPY_headloadtimerate(drive); //Head load rate!
		FLOPPY.DriveData[drive].headunloadtime = FLOPPY_headunloadtimerate(drive); //Head unload rate!
		FLOPPY.DriveData[drive].steprate = FLOPPY_steprate(drive); //Step rate!
	}
}
