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

//ATA-1 harddisk emulation

#include "headers/types.h" //Basic types!
#include "headers/basicio/io.h" //I/O support!
#include "headers/hardware/ports.h" //I/O port support!
#include "headers/hardware/pci.h" //PCI support!
#include "headers/hardware/pic.h" //PIC support!
#include "headers/support/log.h" //Logging support for debugging!
#include "headers/basicio/cueimage.h" //CUE image support!
//Now, for the audio player support:
#include "headers/emu/sound.h" //Sound support!
#include "headers/support/sounddoublebuffer.h" //Double buffered sound!
#include "headers/support/signedness.h" //Sign conversion support!
#include "headers/hardware/i430fx.h" //i430fx PCI IDE controller support!

//#define ATA_LOG

//Define to use traditional CHS translation!
//#define TRADITIONALCHSTRANSLATION

//Timeout for a reset! We're up to 300ms! Take a short while to be properly detected!
#define ATA_RESET_TIMEOUT 300000000.0
//Timing for drive select(documented as 400ns).
#define ATA_DRIVESELECT_TIMEOUT 400.0
//Timing to execute an ATAPI command
#define ATAPI_PENDINGEXECUTECOMMANDTIMING 20000.0
//Timing for ATAPI to prepare data and give it to the host!
#define ATAPI_PENDINGEXECUTETRANSFER_DATATIMING 20000.0
//Timing for ATAPI to prepare result phase and give it to the host!
#define ATAPI_PENDINGEXECUTETRANSFER_RESULTTIMING 7000.0
//Timing until ATAPI becomes ready for a new command.
#define ATAPI_FINISHREADYTIMING 20000.0

//Base clock for the IDE devices(10us)!
#define IDE_BASETIMING 10000.0

#define ATA_FINISHREADYTIMING(mul) (IDE_BASETIMING*mul)

//Time between inserting/removing a disk, must be at least the sum of a transfer, something human usable!
#define ATAPI_DISKCHANGETIMING 100000.0

//What action to perform when ticking the ATAPI disk change timer?
#define ATAPI_DISKCHANGEREMOVED 0
#define ATAPI_DISKCHANGEINSERTED 1
//The disk change timer is finished, backend disk is ready to use again:
#define ATAPI_DISKCHANGEUNCHANGED 2
//Using the dynamic ATAPI disk loading/spin/unloading process?
#define ATAPI_DYNAMICLOADINGPROCESS 3

enum
{
	ATAPI_SPINDOWN=0,
	ATAPI_SPINUP=1,
	ATAPI_CDINSERTED=2,
	ATAPI_DONTSPIN=3
};

//Some timeouts for the spindown/spinup timing!
//Spinning down automatically time
#define ATAPI_SPINDOWN_TIMEOUT 10000000000.0
//Spinning up time
#define ATAPI_SPINUP_TIMEOUT 1000000000.0
//Spinning down manually time
#define ATAPI_SPINDOWNSTOP_TIMEOUT 1000000000.0
//Immediate loading/ejecting time for the tray to load/eject by software (1 second to load the tray)!
#define ATAPI_INSERTION_EJECTING_FASTTIME 1000000000.0
//Loading time for manual load by a user (3 seconds to load a disc, 1 second to load the tray)!
#define ATAPI_INSERTION_TIME 3000000000.0

//What has happened during a ATAPI_DISKCHANGETIMEOUT?

//Hard disk IRQ!
#define ATA_PRIMARYIRQ_AT 0x0E
#define ATA_SECONDARYIRQ_AT 0x0F
#define ATA_PRIMARYIRQ_XT 0x15
#define ATA_SECONDARYIRQ_XT 0x25

//Bits 6-7 of byte 2 of the Mode Sense command
//Current values
#define CDROM_PAGECONTROL_CURRENT 0
//Changable values
#define CDROM_PAGECONTROL_CHANGEABLE 1
//Default values
#define CDROM_PAGECONTROL_DEFAULT 2
//Saved values
#define CDROM_PAGECONTROL_SAVED 3

//Sense key etc. defines
#define SENSE_NONE 0
#define SENSE_NOT_READY 2
#define SENSE_ILLEGAL_REQUEST 5
#define SENSE_UNIT_ATTENTION 6

//ASC extended sense information!
#define ASC_ILLEGAL_OPCODE 0x20
#define ASC_LOGICAL_BLOCK_OOR 0x21
#define ASC_INV_FIELD_IN_CMD_PACKET 0x24
#define ASC_MEDIUM_MAY_HAVE_CHANGED 0x28
#define ASC_SAVING_PARAMETERS_NOT_SUPPORTED 0x39
#define ASC_MEDIUM_NOT_PRESENT 0x3a
#define ASC_END_OF_USER_AREA_ENCOUNTERED_ON_THIS_TRACK 0x63
#define ASC_ILLEGAL_MODE_FOR_THIS_TRACK_OR_INCOMPATIBLE_MEDIUM 0x64

/* Start of the audio player settings and defines */

//We're rending seperate samples, so for maximum accuracy, default in the sample buffer!
#define __CDROM_SAMPLEBUFFERSIZE 4096
#define __CDROM_VOLUME 100.0f

//sff8020i - figure 15: play sequencing
enum
{
	PLAYER_INITIALIZED = 0, //Stopped
	PLAYER_PLAYING = 1, //Playing
	PLAYER_SCANNING = 2, //Scanning(also paused, but using a scan command)
	PLAYER_PAUSED = 3 //Paused
};

enum
{
	PLAYER_STATUS_NOTSUPPORTED = 0,
	PLAYER_STATUS_PLAYING_IN_PROGRESS = 0x11,
	PLAYER_STATUS_PAUSED = 0x12,
	PLAYER_STATUS_FINISHED = 0x13,
	PLAYER_STATUS_ERROREDOUT = 0x14,
	PLAYER_STATUS_NONE = 0x15
};

/* End of the audio player settings and defines */

PCI_GENERALCONFIG PCI_IDE;
PCI_GENERALCONFIG *activePCI_IDE = &PCI_IDE; //Active PCI IDE interface!

//Index: 0=HDD, 1=CD-ROM! Swapped in the command! Empty is padded with spaces!
byte MODEL[2][41] = {"Generic HDD","Generic CD-ROM"}; //Word #27-46.
byte SERIAL[2][21] = {"UniPCemu HDD0","UniPCemu CD-ROM0"}; //Word #5-10.
byte FIRMWARE[2][9] = {"1.0","1.0"}; //Word #23-26.

typedef struct
{
	int_64 cueresult, cuepostgapresult, cue_trackskip, cue_trackskip2, cue_postgapskip;
	byte cue_M, cue_S, cue_F, cue_startS, cue_startF, cue_startM, cue_endM, cue_endS, cue_endF;
	byte cue_postgapM, cue_postgapS, cue_postgapF, cue_postgapstartM, cue_postgapstartS, cue_postgapstartF, cue_postgapendM, cue_postgapendS, cue_postgapendF;
	uint_32 pregapsize, postgapsize;
	byte tracktype;
} TRACK_GEOMETRY;

typedef struct
{
	struct
	{
		byte multipletransferred; //How many sectors were transferred in multiple mode this block?
		byte multiplemode; //Enable multiple mode transfer transfers to be used? 0=Disabled(according to the ATA-1 documentation)!
		byte multiplesectors; //How many sectors to currently transfer in multiple mode for the current command?
		byte longop; //Long operation instead of a normal one?
		uint_32 datapos; //Data position?
		uint_32 datablock; //How large is a data block to be transferred?
		uint_32 datasize; //Data size in blocks to transfer?
		byte data[0x20000]; //Full sector data, large enough to buffer anything we throw at it (normal buffering)! Up to 10000 
		byte command;
		byte commandstatus; //Do we have a command?
		byte ATAPI_processingPACKET; //Are we processing a packet or data for the ATAPI device?
		DOUBLE ATAPI_PendingExecuteCommand; //How much time is left pending?
		DOUBLE ATAPI_PendingExecuteTransfer; //How much time is left pending for transfer timing?
		DOUBLE ATAPI_diskchangeTimeout; //Disk change timer!
		byte ATAPI_diskchangeDirection; //What direction are we? Inserted or Removed!
		byte ATAPI_diskchangepending; //Disk change pending until packet is given!
		uint_32 ATAPI_bytecount; //How many data to transfer in one go at most!
		uint_32 ATAPI_bytecountleft; //How many data is left to transfer!
		byte ATAPI_bytecountleft_IRQ; //Are we to fire an IRQ when starting a new ATAPI data transfer subblock?
		byte ATAPI_PACKET[12]; //Full ATAPI packet!
		byte ATAPI_ModeData[0x10000]; //All possible mode selection data, that's specified!
		byte ATAPI_DefaultModeData[0x10000]; //All possible default mode selection data, that's specified!
		byte ATAPI_SupportedMask[0x10000]; //Supported mask bits for all saved values! 0=Not supported, 1=Supported!
		byte ERRORREGISTER;
		byte STATUSREGISTER;
		byte readmultipleerror;
		word readmultiple_partialtransfer; //For error cases, how much is actually transferred(in sectors)!

		byte SensePacket[0x12]; //Data of a request sense packet.

		byte diskInserted; //Is the disk even inserted, from the CD-ROM-drive perspective(isn't inserted when 0, inserted only when both this and backend is present)?
		byte ATAPI_diskChanged; //Is the disk changed, from the CD-ROM-drive perspective(not ready becoming ready)?
		byte ATAPI_mediaChanged; //Has the inserted media been ejected or inserted?
		byte ATAPI_mediaChanged2; //Has the inserted media been ejected or inserted?

		byte PendingLoadingMode; //What loading mode is to be applied? Defaulting to 0=Idle!
		byte PendingSpinType; //What type to execute(spindown/up)?

		struct
		{
			union
			{
				struct
				{
					byte sectornumber; //LBA bits 0-7!
					byte cylinderlow; //LBA bits 8-15!
					byte cylinderhigh; //LBA bits 16-23!
					byte drivehead; //LBA 24-27!
				};
				uint_32 LBA; //LBA address in LBA mode (28 bits value)!
			};
			byte features;
			byte sectorcount;
			byte reportReady; //Not ready and above ROM until received ATAPI command!
		} PARAMETERS;
		word driveparams[0x100]; //All drive parameters for a drive!
		uint_32 current_LBA_address; //Current LBA address!
		byte Enable8BitTransfers; //Enable 8-bit transfers?
		byte EnableMediaStatusNotification; //Enable Media Status Notification?
		byte preventMediumRemoval; //Are we preventing medium removal for removable disks(CD-ROM)?
		byte allowDiskInsertion; //Allow a disk to be inserted?
		byte ATAPI_caddyejected; //Caddy ejected? 0=Inserted, 1=Ejected, 2=Request insertion.
		byte ATAPI_caddyinsertion_fast; //Fast automatic insertion and startup of the caddy?
		byte ATAPI_diskchangependingspeed; //The latched speed of disk change pending, to be automatically cleared!
		byte MediumChangeRequested; //Is the user requesting the drive to be ejected?
		uint_32 ATAPI_LBA; //ATAPI LBA storage!
		uint_32 ATAPI_lastLBA; //ATAPI last LBA storage!
		uint_32 ATAPI_disksize; //The ATAPI disk size!
		DOUBLE resetTiming;
		byte resetTriggersIRQ;
		DOUBLE ReadyTiming; //Timing until we become ready after executing a command!
		DOUBLE IRQTimeout; //Timeout until we're to fire an IRQ!
		byte IRQTimeout_busy; //Busy while timing the IRQ timeout? 0=Not busy, raise IRQ, 1=Keep busy, raise IRQ, 2=Keep busy, no IRQ!
		DOUBLE BusyTiming; //Timing until we're not busy anymore!
		byte resetSetsDefaults;
		byte expectedReadDataType; //Expected read data format!
		byte IRQraised; //Did we raise the IRQ line?
		struct
		{
			//Track related information of the start frame!
			byte trackref_track; //The track number
			byte trackref_type; //The type of track!
			byte trackref_M; //Reference M
			byte trackref_S; //Reference S
			byte trackref_F; //Reference F
			//Normal playback info!
			byte M; //M address to play next!
			byte S; //S address to play next!
			byte F; //F address to play next!
			byte endM; //End M address to stop playing!
			byte endS; //End S address to stop playing!
			byte endF; //End F address to stop playing!
			byte samples[2352]; //One frame of audio we're playing!
			word samplepos; //The position of the current sample we're playing!
			byte status; //The current player status!
			byte effectiveplaystatus; //Effective play status!
			SOUNDDOUBLEBUFFER soundbuffer; //Our two sound buffers for our two chips!
		} AUDIO_PLAYER; //The audio player itself!
		byte lasttrack; //Last requested track!
		byte lastformat; //Last requested data format!
		byte lastM;
		byte lastS;
		byte lastF;
		TRACK_GEOMETRY geometries[100]; //All possible track geometries preloaded!
	} Drive[2]; //Two drives!

	byte DriveControlRegister;
	byte DriveAddressRegister;

	byte activedrive; //What drive are we currently?
	byte DMAPending; //DMA pending?
	byte TC; //Terminal count occurred in DMA transfer?
	DOUBLE driveselectTiming;
	DOUBLE playerTiming; //The timer for the player samples!
	DOUBLE playerTick; //The time of one sample!
	byte use_PCImode; //Enable PCI mode for this controller? Bit0: Set=PCI mode, Clear=Compatiblity. Bit1: Set=Use BAR0 and BAR1 instead for the BAR2 and BAR3.
} ATA_ChannelContainerType;

ATA_ChannelContainerType ATA[2]; //Two channels of ATA drives!

byte CDROM_channel = 0xFF; //Default: no CD-ROM channel!

enum {
	LOAD_IDLE=0,			/* disc is stationary, not spinning */
	LOAD_NO_DISC=1,			/* caddy inserted, not spinning, no disc */
	LOAD_INSERT_CD=2,			/* user is "inserting" the CD */
	LOAD_DISC_LOADING=3,		/* disc is "spinning up" */
	LOAD_DISC_READIED=4,		/* disc just "became ready" */
	LOAD_READY=5,
	LOAD_SPINDOWN=6, /* disc is requested to spin down */
	LOAD_EJECTING=7 /* disc is ejecting */
};

//Drive/Head register
#define ATA_DRIVEHEAD_HEADR(channel,drive) (ATA[channel].Drive[drive].PARAMETERS.drivehead&0xF)
#define ATA_DRIVEHEAD_HEADW(channel,drive,val) ATA[channel].Drive[drive].PARAMETERS.drivehead=((ATA[channel].Drive[drive].PARAMETERS.drivehead&~0xF)|(val&0xF))
#define ATA_DRIVEHEAD_SLAVEDRIVER(channel,drive) ((ATA[channel].Drive[drive].PARAMETERS.drivehead>>4)&1)
#define ATA_DRIVEHEAD_LBAMODE_2R(channel,drive) ((ATA[channel].Drive[drive].PARAMETERS.drivehead>>6)&1)
#define ATA_DRIVEHEAD_LBAMODE_2W(channel,drive,val) ATA[channel].Drive[drive].PARAMETERS.drivehead=((ATA[channel].Drive[drive].PARAMETERS.drivehead&~0x40)|((val&1)<<6))
#define ATA_DRIVEHEAD_LBAHIGHR(channel,drive) (ATA[channel].Drive[drive].PARAMETERS.drivehead&0x3F)
#define ATA_DRIVEHEAD_LBAHIGHW(channel,drive,val) ATA[channel].Drive[drive].PARAMETERS.drivehead=((ATA[channel].Drive[drive].PARAMETERS.drivehead&~0x3F)|(val&0x3F))
#define ATA_DRIVEHEAD_LBAMODER(channel,drive) ((ATA[channel].Drive[drive].PARAMETERS.drivehead>>6)&1)

//Drive Control Register
//nIEN: Disable interrupts when set or not the drive selected!
#define DRIVECONTROLREGISTER_NIENR(channel) ((ATA[channel].DriveControlRegister>>1)&1)
//Reset!
#define DRIVECONTROLREGISTER_SRSTR(channel) ((ATA[channel].DriveControlRegister>>2)&1)

//Status Register

//An error has occurred when 1!
#define ATA_STATUSREGISTER_ERRORR(channel,drive) (ATA[channel].Drive[drive].STATUSREGISTER&1)
//An error has occurred when 1!
#define ATA_STATUSREGISTER_ERRORW(channel,drive,val) ATA[channel].Drive[drive].STATUSREGISTER=((ATA[channel].Drive[drive].STATUSREGISTER&~1)|((val)&1))
//Set once per disk revolution.
#define ATA_STATUSREGISTER_INDEXW(channel,drive,val) ATA[channel].Drive[drive].STATUSREGISTER=((ATA[channel].Drive[drive].STATUSREGISTER&~2)|(((val)&1)<<1))
//Data has been corrected.
#define ATA_STATUSREGISTER_CORRECTEDDATAW(channel,drive,val) ATA[channel].Drive[drive].STATUSREGISTER=((ATA[channel].Drive[drive].STATUSREGISTER&~4)|(((val)&1)<<2))
//Ready to transfer a word or byte of data between the host and the drive.
#define ATA_STATUSREGISTER_DATAREQUESTREADYW(channel,drive,val) ATA[channel].Drive[drive].STATUSREGISTER=((ATA[channel].Drive[drive].STATUSREGISTER&~8)|(((val)&1)<<3))
//Drive heads are settled on a track.
#define ATA_STATUSREGISTER_DRIVESEEKCOMPLETEW(channel,drive,val) ATA[channel].Drive[drive].STATUSREGISTER=((ATA[channel].Drive[drive].STATUSREGISTER&~0x10)|(((val)&1)<<4))
//Write fault status.
#define ATA_STATUSREGISTER_DRIVEWRITEFAULTW(channel,drive,val) ATA[channel].Drive[drive].STATUSREGISTER=((ATA[channel].Drive[drive].STATUSREGISTER&~0x20)|(((val)&1)<<5))
//Ready to accept a command?
#define ATA_STATUSREGISTER_DRIVEREADYW(channel,drive,val) ATA[channel].Drive[drive].STATUSREGISTER=((ATA[channel].Drive[drive].STATUSREGISTER&~0x40)|(((val)&1)<<6))
//The drive has access to the Command Block Registers.
#define ATA_STATUSREGISTER_BUSYW(channel,drive,val) ATA[channel].Drive[drive].STATUSREGISTER=((ATA[channel].Drive[drive].STATUSREGISTER&~0x80)|(((val)&1)<<7))
#define ATA_STATUSREGISTER_BUSYR(channel,drive) ((ATA[channel].Drive[drive].STATUSREGISTER&0x80)>>7)

//Error Register
#define ATA_ERRORREGISTER_NOADDRESSMARKW(channel,drive,val) ATA[channel].Drive[drive].ERRORREGISTER=((ATA[channel].Drive[drive].ERRORREGISTER&~1)|(val&1))
#define ATA_ERRORREGISTER_TRACK0NOTFOUNDW(channel,drive,val) ATA[channel].Drive[drive].ERRORREGISTER=((ATA[channel].Drive[drive].ERRORREGISTER&~2)|((val&1)<<1))
#define ATA_ERRORREGISTER_COMMANDABORTEDW(channel,drive,val) ATA[channel].Drive[drive].ERRORREGISTER=((ATA[channel].Drive[drive].ERRORREGISTER&~4)|((val&1)<<2))
#define ATA_ERRORREGISTER_MEDIACHANGEREQUESTEDW(channel,drive,val) ATA[channel].Drive[drive].ERRORREGISTER=((ATA[channel].Drive[drive].ERRORREGISTER&~8)|((val&1)<<3))
#define ATA_ERRORREGISTER_IDMARKNOTFOUNDW(channel,drive,val) ATA[channel].Drive[drive].ERRORREGISTER=((ATA[channel].Drive[drive].ERRORREGISTER&~0x10)|((val&1)<<4))
#define ATA_ERRORREGISTER_MEDIACHANGEDW(channel,drive,val) ATA[channel].Drive[drive].ERRORREGISTER=((ATA[channel].Drive[drive].ERRORREGISTER&~0x20)|((val&1)<<5))
#define ATA_ERRORREGISTER_UNCORRECTABLEDATAW(channel,drive,val) ATA[channel].Drive[drive].ERRORREGISTER=((ATA[channel].Drive[drive].ERRORREGISTER&~0x40)|((val&1)<<6))
#define ATA_ERRORREGISTER_BADSECTORW(channel,drive,val) ATA[channel].Drive[drive].ERRORREGISTER=((ATA[channel].Drive[drive].ERRORREGISTER&~0x80)|((val&1)<<7))

//ATAPI Error Register!
#define ATAPI_ERRORREGISTER_ILI(channel,drive,val) ATA[channel].Drive[drive].ERRORREGISTER=((ATA[channel].Drive[drive].ERRORREGISTER&~1)|(val&1))
#define ATAPI_ERRORREGISTER_EOM(channel,drive,val) ATA[channel].Drive[drive].ERRORREGISTER=((ATA[channel].Drive[drive].ERRORREGISTER&~2)|((val&1)<<1))
#define ATAPI_ERRORREGISTER_ABRT(channel,drive,val) ATA[channel].Drive[drive].ERRORREGISTER=((ATA[channel].Drive[drive].ERRORREGISTER&~4)|((val&1)<<2))
#define ATAPI_ERRORREGISTER_MCR(channel,drive,val) ATA[channel].Drive[drive].ERRORREGISTER=((ATA[channel].Drive[drive].ERRORREGISTER&~8)|((val&1)<<3))
#define ATAPI_ERRORREGISTER_SENSEKEY(channel,drive,val) ATA[channel].Drive[drive].ERRORREGISTER=((ATA[channel].Drive[drive].ERRORREGISTER&~0xF0)|((val&0xF)<<4))

//ATAPI Media Status extension results!
#define ATAPI_MEDIASTATUS_RSRVD(channel,drive,val) ATA[channel].Drive[drive].ERRORREGISTER=((ATA[channel].Drive[drive].ERRORREGISTER&~1)|(val&1))
#define ATAPI_MEDIASTATUS_NOMED(channel,drive,val) ATA[channel].Drive[drive].ERRORREGISTER=((ATA[channel].Drive[drive].ERRORREGISTER&~2)|((val&1)<<1))
#define ATAPI_MEDIASTATUS_RSRVD2(channel,drive,val) ATA[channel].Drive[drive].ERRORREGISTER=((ATA[channel].Drive[drive].ERRORREGISTER&~4)|((val&1)<<2))
#define ATAPI_MEDIASTATUS_MCR(channel,drive,val) ATA[channel].Drive[drive].ERRORREGISTER=((ATA[channel].Drive[drive].ERRORREGISTER&~8)|((val&1)<<3))
#define ATAPI_MEDIASTATUS_RSRVD3(channel,drive,val) ATA[channel].Drive[drive].ERRORREGISTER=((ATA[channel].Drive[drive].ERRORREGISTER&~0x10)|((val&1)<<4))
#define ATAPI_MEDIASTATUS_MC(channel,drive,val) ATA[channel].Drive[drive].ERRORREGISTER=((ATA[channel].Drive[drive].ERRORREGISTER&~0x20)|((val&1)<<5))
#define ATAPI_MEDIASTATUS_WT_PT(channel,drive,val) ATA[channel].Drive[drive].ERRORREGISTER=((ATA[channel].Drive[drive].ERRORREGISTER&~0x40)|((val&1)<<6))
#define ATAPI_MEDIASTATUS_RSRVD4(channel,drive,val) ATA[channel].Drive[drive].ERRORREGISTER=((ATA[channel].Drive[drive].ERRORREGISTER&~0x80)|((val&1)<<7))

//ATAPI Sense Packet

//0x70
#define ATAPI_SENSEPACKET_ERRORCODEW(channel,drive,val) ATA[channel].Drive[drive].SensePacket[0]=((ATA[channel].Drive[drive].SensePacket[0]&~0x7F)|(val&0x7F))
#define ATAPI_SENSEPACKET_VALIDW(channel,drive,val) ATA[channel].Drive[drive].SensePacket[0]=((ATA[channel].Drive[drive].SensePacket[0]&~0x80)|((val&1)<<7))
#define ATAPI_SENSEPACKET_RESERVED1W(channel,drive,val) ATA[channel].Drive[drive].SensePacket[1]=val
#define ATAPI_SENSEPACKET_RESERVED2W(channel,drive,val) ATA[channel].Drive[drive].SensePacket[2]=((ATA[channel].Drive[drive].SensePacket[2]&~0xF0)|((val&0xF)<<4))
#define ATAPI_SENSEPACKET_SENSEKEYW(channel,drive,val) ATA[channel].Drive[drive].SensePacket[2]=((ATA[channel].Drive[drive].SensePacket[2]&~0xF)|(val&0xF))
#define ATAPI_SENSEPACKET_ILIW(channel,drive,val) ATA[channel].Drive[drive].SensePacket[2]=((ATA[channel].Drive[drive].SensePacket[2]&~0x20)|((val&0x1)<<5))
#define ATAPI_SENSEPACKET_INFORMATION0W(channel,drive,val) ATA[channel].Drive[drive].SensePacket[3]=val
#define ATAPI_SENSEPACKET_INFORMATION1W(channel,drive,val) ATA[channel].Drive[drive].SensePacket[4]=val
#define ATAPI_SENSEPACKET_INFORMATION2W(channel,drive,val) ATA[channel].Drive[drive].SensePacket[5]=val
#define ATAPI_SENSEPACKET_INFORMATION3W(channel,drive,val) ATA[channel].Drive[drive].SensePacket[6]=val
#define ATAPI_SENSEPACKET_ADDITIONALSENSELENGTHW(channel,drive,val) ATA[channel].Drive[drive].SensePacket[7]=val
#define ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION0W(channel,drive,val) ATA[channel].Drive[drive].SensePacket[8]=val
#define ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION1W(channel,drive,val) ATA[channel].Drive[drive].SensePacket[9]=val
#define ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION2W(channel,drive,val) ATA[channel].Drive[drive].SensePacket[0xA]=val
#define ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION3W(channel,drive,val) ATA[channel].Drive[drive].SensePacket[0xB]=val
#define ATAPI_SENSEPACKET_ADDITIONALSENSECODEW(channel,drive,val) ATA[channel].Drive[drive].SensePacket[0xC]=val
#define ATAPI_SENSEPACKET_RESERVED3_0W(channel,drive,val) ATA[channel].Drive[drive].SensePacket[0xD]=val
#define ATAPI_SENSEPACKET_ASCQW(channel,drive,val) ATA[channel].Drive[drive].SensePacket[0xD]=val
#define ATAPI_SENSEPACKET_RESERVED3_1W(channel,drive,val) ATA[channel].Drive[drive].SensePacket[0xE]=val
#define ATAPI_SENSEPACKET_RESERVED3_2W(channel,drive,val) ATA[channel].Drive[drive].SensePacket[0xF]=val
//CD is unsupported, so always report 0?
#define ATAPI_SENSEPACKET_CD(channel,drive,val) ATA[channel].Drive[drive].SensePacket[0xF]=/*(((val)&1)<<6)*/ 0

//ATAPI interrupt reason!
//CD: 1 for command packet, 0 for data transfer
#define ATAPI_INTERRUPTREASON_CD(channel,drive,val) ATA[channel].Drive[drive].PARAMETERS.sectorcount=(ATA[channel].Drive[drive].PARAMETERS.sectorcount&(~0x01))|(val&1)
//IO: 1 for transfer from the device, 0 for transfer to the device.
#define ATAPI_INTERRUPTREASON_IO(channel,drive,val) ATA[channel].Drive[drive].PARAMETERS.sectorcount=(ATA[channel].Drive[drive].PARAMETERS.sectorcount&(~0x02))|((val&1)<<1)
//REL: Release: the device has released the ATA bus before completing the command in process.
#define ATAPI_INTERRUPTREASON_REL(channel,drive,val) ATA[channel].Drive[drive].PARAMETERS.sectorcount=(ATA[channel].Drive[drive].PARAMETERS.sectorcount&(~0x04))|((val&1)<<2)
//TAG: ???
#define ATAPI_INTERRUPTREASON_TAG(channel,drive,val) ATA[channel].Drive[drive].PARAMETERS.sectorcount=(ATA[channel].Drive[drive].PARAMETERS.sectorcount&(~0xF8))|((val&0x1F)<<3)

byte ATA_channel = 0; //What channel are we processing?
byte ATA_slave = 0; //Are we processing master or slave?

uint_32 MSF2LBAbin(byte M, byte S, byte F)
{
	return (((M * 60) + S) * 75) + F; //75 frames per second, 60 seconds in a minute!
}

void LBA2MSFbin(uint_32 LBA, byte *M, byte *S, byte *F)
{
	uint_32 rest;
	rest = LBA; //Load LBA!
	*M = rest / (60 * 75); //Minute!
	rest -= *M*(60 * 75); //Rest!
	*S = rest / 75; //Second!
	rest -= *S * 75;
	*F = rest % 75; //Frame, if any!
}

OPTINLINE byte ATA_activeDrive(byte channel)
{
	return ATA[channel].activedrive; //Give the drive or 0xFF if invalid!
}

OPTINLINE uint_32 ATA_CHS2LBA(byte channel, byte slave, word cylinder, byte head, byte sector)
{
	return (((cylinder*ATA[channel].Drive[slave].driveparams[55]) + head)*ATA[channel].Drive[slave].driveparams[56]) + sector - 1; //Give the LBA value!
}

OPTINLINE byte ATA_LBA2CHS(byte channel, byte slave, uint_32 LBA, word *cylinder, byte *head, byte *sector)
{
	uint_32 temp;
	temp = (ATA[channel].Drive[slave].driveparams[55] * ATA[channel].Drive[slave].driveparams[56]); //Sectors per cylinder!
	if (temp && ATA[channel].Drive[slave].driveparams[56]) //Valid geometry to use?
	{
		*cylinder = (word)(LBA / temp); //Cylinder!
		LBA -= *cylinder * temp; //Decrease LBA to get heads&sectors!
		temp = ATA[channel].Drive[slave].driveparams[56]; //SPT!
		*head = (LBA / temp) & 0xF; //The head!
		LBA -= *head * temp; //Decrease LBA to get sectors!
		*sector = ((LBA + 1) & 0xFF); //The sector!
		return 1; //OK!
	}
	//Invalid geometry?
	*head = *sector = 0xFF; //Invalid!
	*cylinder = 0xFFFF; //Invalid!
	return 0; //Error: invalid geometry!
}

int ATA_Drives[2][2]; //All ATA mounted drives to disk conversion!
byte ATA_DrivesReverse[4][2]; //All Drive to ATA mounted drives conversion!

extern byte is_XT; //Are we emulating a XT architecture?

OPTINLINE void ATA_IRQ(byte channel, byte slave, DOUBLE timeout, byte enforceBusy)
{
	if (timeout) //Timeout specified to use?
	{
		ATA[channel].Drive[slave].IRQTimeout = timeout; //Set the timeout for the IRQ!
		ATA[channel].Drive[slave].IRQTimeout_busy = enforceBusy; //Are we to enforce busy?
	}
	else //No timeout? Fire IRQ immediately!
	{
		if ((!DRIVECONTROLREGISTER_NIENR(channel)) && (!DRIVECONTROLREGISTER_SRSTR(channel)) && ((ATA_activeDrive(channel)==slave) || (ATA_Drives[channel][slave]>=CDROM0))) //Allow interrupts?
		{
			ATA[channel].Drive[slave].IRQraised = 1; //Raised!
			if (is_XT)
			{
				switch (channel)
				{
				case 0: //Primary channel?
					raiseirq(ATA_PRIMARYIRQ_XT); //Execute the IRQ!
					break;
				case 1:
					raiseirq(ATA_SECONDARYIRQ_XT); //Execute the IRQ!
					break;
				default: //Unknown channel?
					break;
				}
			}
			else
			{
				switch (channel)
				{
				case 0: //Primary channel?
					raiseirq(ATA_PRIMARYIRQ_AT); //Execute the IRQ!
					break;
				case 1:
					raiseirq(ATA_SECONDARYIRQ_AT); //Execute the IRQ!
					break;
				default: //Unknown channel?
					break;
				}
			}
		}
	}
}

OPTINLINE void ATA_removeIRQ(byte channel, byte slave)
{
	ATA[channel].Drive[slave].IRQraised = 0; //Lowered!
	if (is_XT)
	{
		//Always allow removing an IRQ if it's raised! This doesn't depend on any flags set in registers!
		switch (channel)
		{
		case 0: //Primary channel?
			lowerirq(ATA_PRIMARYIRQ_XT); //Execute the IRQ!
			acnowledgeIRQrequest(ATA_PRIMARYIRQ_XT); //Acnowledge!
			break;
		case 1:
			lowerirq(ATA_SECONDARYIRQ_XT); //Execute the IRQ!
			acnowledgeIRQrequest(ATA_SECONDARYIRQ_XT); //Acnowledge!
			break;
		default: //Unknown channel?
			break;
		}
	}
	else
	{
		//Always allow removing an IRQ if it's raised! This doesn't depend on any flags set in registers!
		switch (channel)
		{
		case 0: //Primary channel?
			lowerirq(ATA_PRIMARYIRQ_AT); //Execute the IRQ!
			acnowledgeIRQrequest(ATA_PRIMARYIRQ_AT); //Acnowledge!
			break;
		case 1:
			lowerirq(ATA_SECONDARYIRQ_AT); //Execute the IRQ!
			acnowledgeIRQrequest(ATA_SECONDARYIRQ_AT); //Acnowledge!
			break;
		default: //Unknown channel?
			break;
		}
	}
}

void cleanATA()
{
	//Unused ATM!
}

void ATAPI_executeCommand(byte channel, byte drive); //Prototype for ATAPI execute Command!

void ATAPI_generateInterruptReason(byte channel, byte drive)
{
	/*
	IO DRQ CoD
	0 1 1 Command - Ready to Accept Command Packet Bytes
	1 1 1 Message (Future) - Ready to Send Message data to Host
	1 1 0 Data To Host- Send command parameter data (e.g. Read
	Data) to the host
	0 1 0 Data From Host - Receive command parameter data (e.g.
	Write Data) from the host
	1 0 1 Status - Register contains Completion Status
	*/
	if (!(ATA_Drives[channel][drive] >= CDROM0)) return; //Don't handle for non CD-ROM drives!
	if (ATA[channel].Drive[drive].ATAPI_diskchangepending==2)
	{
		ATAPI_INTERRUPTREASON_CD(channel,drive,1); //Not a command packet!
		ATAPI_INTERRUPTREASON_IO(channel,drive,1); //Transfer to device!
		ATAPI_INTERRUPTREASON_REL(channel,drive,0); //Don't Release, to be cleared!
		ATAPI_ERRORREGISTER_SENSEKEY(channel,drive,SENSE_UNIT_ATTENTION); //Signal an Unit Attention Sense key!
		ATAPI_ERRORREGISTER_ABRT(channel,drive,0); //Signal no Abort!
		ATA_STATUSREGISTER_ERRORW(channel,drive,1); //Error(Unit Attention)!
		ATA[channel].Drive[drive].ATAPI_processingPACKET = 3; //We're triggering the reason read to reset!
		ATA[channel].Drive[drive].ATAPI_diskchangepending = 3; //Not pending anymore, pending to give sense packet instead!
	}
	else if (ATA[channel].Drive[drive].ATAPI_processingPACKET==1) //We're processing a packet?
	{
		ATAPI_INTERRUPTREASON_CD(channel,drive,1); //Command packet!
		ATAPI_INTERRUPTREASON_IO(channel,drive,0); //Transfer to device!
		ATAPI_INTERRUPTREASON_REL(channel,drive,0); //Don't Release, to be cleared!
	}
	else if (ATA[channel].Drive[drive].ATAPI_processingPACKET==2) //Processing data?
	{
		ATAPI_INTERRUPTREASON_CD(channel,drive,0); //Not a command packet: we're data!
		ATAPI_INTERRUPTREASON_IO(channel,drive,(ATA[channel].Drive[drive].commandstatus==1)?1:0); //IO is set when reading data to the Host(CPU), through PORT IN!
		ATAPI_INTERRUPTREASON_REL(channel,drive,0); //Don't Release, to be cleared!
	}
	else if (ATA[channel].Drive[drive].ATAPI_processingPACKET==3) //Result phase? We contain the Completion Status!
	{
		ATAPI_INTERRUPTREASON_CD(channel,drive,1); //Not a command packet: we're data!
		ATAPI_INTERRUPTREASON_IO(channel,drive,1); //IO is set when reading data to the Host(CPU), through PORT IN!
		ATAPI_INTERRUPTREASON_REL(channel,drive,0); //Don't Release, to be cleared!

		//Now, also make sure that BSY and DRQ are cleared!
		ATA[channel].Drive[drive].ATAPI_PendingExecuteTransfer = (DOUBLE)0; //Don't use any timers anymore!
		ATA[channel].Drive[drive].ReadyTiming = (DOUBLE)0; //We're reedy immediately!
	}
	else //Inactive? Indicate command to be sent!
	{
		ATAPI_INTERRUPTREASON_CD(channel,drive,0); //Not a command packet!
		ATAPI_INTERRUPTREASON_IO(channel,drive,0); //Transfer to device!
		ATAPI_INTERRUPTREASON_REL(channel,drive,0); //Don't Release, to be cleared!
		if (ATA[channel].Drive[drive].ATAPI_processingPACKET==0) //Finished packet transfer? We're becoming ready still?
		{
			ATA[channel].Drive[drive].ReadyTiming = ATAPI_FINISHREADYTIMING; //Timeout for becoming ready after finishing an command!
		}
	}
}

void ATAPI_setModePages(byte disk_channel, byte disk_slave)
{
	word speed = 1024; //Speed in KBPS!

	//Setup the changable bits!
	ATA[disk_channel].Drive[disk_slave].ATAPI_SupportedMask[(0x0E << 8)|(0x02-2)] |= 0x02; //Stop on Track Crossing supported
	//ATA[disk_channel].Drive[disk_slave].ATAPI_SupportedMask[(0x0E << 8)|(0x06-2)] |= 0xFF; //Logical Block Per Second of Audio supported?
	//ATA[disk_channel].Drive[disk_slave].ATAPI_SupportedMask[(0x0E << 8) | (0x08-2)] |= 0x0F; //CDDA Output Port 0 channel selection. 0=Muted, 1=Channel 0, 2=Channel 1, 3=Channel 0&1, 4=Channel 2, 8=Channel 3. Supported?
	//ATA[disk_channel].Drive[disk_slave].ATAPI_SupportedMask[(0x0E << 8) | (0x0A-2)] |= 0x0F; //CDDA Output Port 1 channel selection. 0=Muted, 1=Channel 0, 2=Channel 1, 3=Channel 0&1, 4=Channel 2, 8=Channel 3. Supported?
	//ATA[disk_channel].Drive[disk_slave].ATAPI_SupportedMask[(0x0E << 8) | (0x09-2)] |= 0xFF; //Output Port 0/1 volume(0-FF). This is an attenuation of 20 log (val/256).
	//ATA[disk_channel].Drive[disk_slave].ATAPI_SupportedMask[(0x0E << 8) | (0x0B-2)] |= 0xFF; //Output Port 0/1 volume(0-FF). This is an attenuation of 20 log (val/256).

	//Setup the capabilities and Mechanical Status page(unmodifyable values)!
	ATA[disk_channel].Drive[disk_slave].ATAPI_ModeData[(0x2A << 8) | (4 - 2)] = 0x09; //Bit 0:1(supports CD-DA), bit 4:1(Mode 2 Format 1(XA) format supported)
	ATA[disk_channel].Drive[disk_slave].ATAPI_ModeData[(0x2A << 8) | (5 - 2)] = 0x03; //Bit 0:1(supports reading audio using Read CD command?), bit 1:1(can resume play without loss of position)
	ATA[disk_channel].Drive[disk_slave].ATAPI_ModeData[(0x2A << 8) | (6 - 2)] = 0x29|(ATA[disk_channel].Drive[disk_slave].allowDiskInsertion?0:2); //Bit 0:1(lock command available), bit 1:x(media ejection impossible due to locked state: 1; otherwise 0), bit 3:1(ejection possible using the start/stop command, bits 5-7:001(Tray type loading mechanism)
	ATA[disk_channel].Drive[disk_slave].ATAPI_ModeData[(0x2A << 8) | (8 - 2)] = ((speed>>8)&0xFF); //Maximum speed supported (MSB)
	ATA[disk_channel].Drive[disk_slave].ATAPI_ModeData[(0x2A << 8) | (9 - 2)] = (speed&0xFF); //Maximum speed supported (LSB)
	ATA[disk_channel].Drive[disk_slave].ATAPI_ModeData[(0x2A << 8) | (14 - 2)] = ((speed >> 8) & 0xFF); //Current speed selected (MSB)
	ATA[disk_channel].Drive[disk_slave].ATAPI_ModeData[(0x2A << 8) | (15 - 2)] = (speed & 0xFF); //Current speed selected (LSB)
}

void ATAPI_command_reportError(byte channel, byte slave); //Prototype!
void ATAPI_SET_SENSE(byte channel, byte drive, byte SK, byte ASC, byte ASCQ, byte isCommandCause); //Prototype!

void ATAPI_insertCD(int disk, byte disk_channel, byte disk_drive); //Prototype for inserting a new CD, whether present or not, inserting the caddy!

void ATAPI_diskchangedhandler(byte channel, byte drive, byte inserted)
{
	//We do anything a real drive does when a medium is removed or inserted!
	if (inserted) //Inserted?
	{
		ATA[channel].Drive[drive].diskInserted = 1; //We're inserted!
		if (ATA[channel].Drive[drive].EnableMediaStatusNotification) //Enabled the notification of media being inserted?
		{
			ATAPI_SET_SENSE(channel, drive, SENSE_UNIT_ATTENTION, ASC_MEDIUM_MAY_HAVE_CHANGED, 0x00, 0); //Set the error sense!
			ATA[channel].Drive[drive].ATAPI_diskchangepending = 2; //Special: disk inserted!
			ATAPI_command_reportError(channel, drive); //Prototype!
		}
		else //ATAPI drive might have something to do now?
		{
			ATA[channel].Drive[drive].ATAPI_diskChanged = 1; //We've been changed!
			ATA[channel].Drive[drive].ATAPI_diskchangepending = 3; //Special: disk inserted sense packet only!
		}
	}
	else //Not inserted anymore, if inserted?
	{
		ATA[channel].Drive[drive].diskInserted = 0; //We're not inserted(anymore)!
		ATAPI_setModePages(channel, drive); //Update with the new status!
		//Don't handle anything when not inserted!
	}
	//Don't handle removed?
}

void ATAPI_dynamicloadingprocess_spindown(byte channel, byte drive)
{
	switch (ATA[channel].Drive[drive].PendingLoadingMode)
	{
	case LOAD_DISC_READIED:
	case LOAD_READY:
	case LOAD_SPINDOWN: //Requested to spin down?
		ATA[channel].Drive[drive].PendingLoadingMode = LOAD_IDLE; //Becoming idle!
		ATA[channel].Drive[drive].ATAPI_diskchangeTimeout = 0.0f; //Nothing!
		ATAPI_setModePages(channel, drive); //Update with the new status!
		break;
	default:
		break;
	}
}

void ATAPI_dynamicloadingprocess_CDinserted(byte channel, byte drive)
{
	switch (ATA[channel].Drive[drive].PendingLoadingMode)
	{
	case LOAD_INSERT_CD: //A CD-ROM has been inserted into or removed from the caddy?
		if (ATA[channel].Drive[drive].diskInserted) //Inserted?
		{
			ATA[channel].Drive[drive].ATAPI_caddyejected = 0; //Not ejected anymore!
			EMU_setDiskBusy(ATA_Drives[channel][drive], 0 | (ATA[channel].Drive[drive].ATAPI_caddyejected << 1)); //We're not reading anymore!
			ATA[channel].Drive[drive].PendingLoadingMode = LOAD_DISC_LOADING; //Start loading!
			ATA[channel].Drive[drive].PendingSpinType = ATAPI_SPINUP; //Spin up!
			ATA[channel].Drive[drive].ATAPI_diskchangeTimeout = ATAPI_SPINUP_TIMEOUT; //Timeout to spinup complete!
			ATA[channel].Drive[drive].ATAPI_diskchangeDirection = ATAPI_DYNAMICLOADINGPROCESS; //We're unchanged from now on!
		}
		else //No disc?
		{
			ATA[channel].Drive[drive].ATAPI_caddyejected = 0; //Not ejected anymore!
			EMU_setDiskBusy(ATA_Drives[channel][drive], 0 | (ATA[channel].Drive[drive].ATAPI_caddyejected << 1)); //We're not reading anymore!
			ATA[channel].Drive[drive].PendingLoadingMode = LOAD_NO_DISC; //No disc inserted!
			ATA[channel].Drive[drive].PendingSpinType = ATAPI_SPINUP; //Spin up!
			ATA[channel].Drive[drive].ATAPI_diskchangeTimeout = 0.0f; //Timeout to spinup complete!
			ATA[channel].Drive[drive].ATAPI_diskchangeDirection = ATAPI_DISKCHANGEUNCHANGED; //We're unchanged from now on!
		}
		break;
	default:
		break;
	}
}

void ATAPI_dynamicloadingprocess_SpinUpComplete(byte channel, byte drive)
{
	switch (ATA[channel].Drive[drive].PendingLoadingMode)
	{
	case LOAD_DISC_LOADING:
		ATA[channel].Drive[drive].PendingLoadingMode = LOAD_DISC_READIED; //Start loading!
		ATA[channel].Drive[drive].PendingSpinType = ATAPI_SPINDOWN; //Spin down!
		ATA[channel].Drive[drive].ATAPI_diskchangeTimeout = ATAPI_SPINDOWN_TIMEOUT; //Timeout to spindown!
		ATA[channel].Drive[drive].ATAPI_diskchangeDirection = ATAPI_DYNAMICLOADINGPROCESS; //We're unchanged from now on!
		ATAPI_setModePages(channel, drive); //Update with the new status!
		break;
	default:
		break;
	}
}

void ATAPI_dynamicloadingprocess(byte channel, byte drive)
{
	ATA[channel].Drive[drive].ATAPI_diskchangeDirection = ATAPI_DISKCHANGEUNCHANGED; //We're unchanged from now on, by default!
	switch (ATA[channel].Drive[drive].PendingSpinType)
	{
	case ATAPI_SPINDOWN:
		ATAPI_dynamicloadingprocess_spindown(channel,drive);
		break;
	case ATAPI_SPINUP:
		ATAPI_dynamicloadingprocess_SpinUpComplete(channel,drive);
		break;
	case ATAPI_CDINSERTED:
		ATAPI_dynamicloadingprocess_CDinserted(channel,drive);
		break;
	case ATAPI_DONTSPIN: //Don't spin(ejected)?
		//Don't do anything: this is when we're ejected!
		ATA[channel].Drive[drive].ATAPI_diskchangeTimeout = (DOUBLE)0; //Stop the timeout, don't count anything more! We're fully ejected!
		break;
	default: //Unknown?
		break;
	}
}

void tickATADiskChange(byte channel, byte drive)
{
	switch (ATA[channel].Drive[drive].ATAPI_diskchangeDirection) //What action to take?
	{
		case ATAPI_DISKCHANGEREMOVED: //Removed? Tick removed, pend inserted when inserted!
			if (ATA[channel].Drive[drive].commandstatus==0) //Ready for a new command?
			{
				ATAPI_diskchangedhandler(channel,drive,0); //We're removed!
				if (is_mounted(ATA_Drives[channel][drive])) //Are we mounted? Simulate a disk being inserted very soon!
				{
					ATA[channel].Drive[drive].ATAPI_diskchangeDirection = ATAPI_DISKCHANGEINSERTED; //We're unchanged from now on!
					ATA[channel].Drive[drive].ATAPI_diskchangeTimeout = ATAPI_DISKCHANGETIMING; //Start timing to release!
				}
				else //Finished?
				{
					ATA[channel].Drive[drive].ATAPI_diskchangeDirection = ATAPI_DISKCHANGEUNCHANGED; //We're unchanged from now on!
					ATA[channel].Drive[drive].ATAPI_diskchangeTimeout = 0.0; //No timer anymore!
				}
			}
			else //Command still pending? We still pend as well!
			{
				ATA[channel].Drive[drive].ATAPI_diskchangeTimeout = ATAPI_DISKCHANGETIMING; //Wait for availability!
			}
			break;
		case ATAPI_DISKCHANGEINSERTED: //Inserted? Tick inserted, finish!
			if (ATA[channel].Drive[drive].commandstatus==0) //Ready for a new command?
			{
				ATAPI_diskchangedhandler(channel,drive,1); //We're inserted!
				ATA[channel].Drive[drive].ATAPI_diskchangeDirection = ATAPI_DISKCHANGEUNCHANGED; //We're unchanged from now on!
			}
			else //Command still pending? We still pend as well!
			{
				ATA[channel].Drive[drive].ATAPI_diskchangeTimeout = ATAPI_DISKCHANGETIMING; //Wait for availability!
			}
			break;
		case ATAPI_DYNAMICLOADINGPROCESS: //Dynamic loading process? Also triggered when a disk is inserted!
			ATAPI_dynamicloadingprocess(channel,drive); //Apply the dynamic loading process! This also must clear the timer if becoming unused!
			break;
		default: //Finished by default(NOP)?
			ATA[channel].Drive[drive].ATAPI_diskchangeDirection = ATAPI_DISKCHANGEUNCHANGED; //We're unchanged from now on!
			ATA[channel].Drive[drive].ATAPI_diskchangeTimeout = 0.0; //No timer anymore!
			break;
	}
}

void ATAPI_SET_SENSE(byte channel, byte drive, byte SK,byte ASC,byte ASCQ,byte isCommandCause)
{
	ATAPI_SENSEPACKET_SENSEKEYW(channel, drive, SK); //Reason of the error
	ATAPI_SENSEPACKET_RESERVED2W(channel, drive, 0); //Reserved field!
	ATAPI_SENSEPACKET_ADDITIONALSENSECODEW(channel, drive, ASC); //Extended reason code
	ATAPI_SENSEPACKET_ASCQW(channel, drive, ASCQ); //ASCQ code!
	ATAPI_SENSEPACKET_ILIW(channel, drive, 0); //ILI bit cleared!
	ATAPI_SENSEPACKET_ERRORCODEW(channel, drive, 0x70); //Default error code?
	ATAPI_SENSEPACKET_ADDITIONALSENSELENGTHW(channel, drive, 10); //Additional Sense Length = 10?
	ATAPI_SENSEPACKET_INFORMATION0W(channel, drive, 0); //No info!
	ATAPI_SENSEPACKET_INFORMATION1W(channel, drive, 0); //No info!
	ATAPI_SENSEPACKET_INFORMATION2W(channel, drive, 0); //No info!
	ATAPI_SENSEPACKET_INFORMATION3W(channel, drive, 0); //No info!
	ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION0W(channel, drive, 0); //No command specific information?
	ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION1W(channel, drive, 0); //No command specific information?
	ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION2W(channel, drive, 0); //No command specific information?
	ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION3W(channel, drive, 0); //No command specific information?
	ATAPI_SENSEPACKET_VALIDW(channel, drive, 1); //We're valid!
	ATAPI_SENSEPACKET_CD(channel, drive, (isCommandCause?1:0)); //Command/Data! 
}

void ATAPI_PendingExecuteCommand(byte channel, byte drive) //We're pending until execution!
{
	ATA[channel].Drive[drive].ATAPI_PendingExecuteCommand = ATAPI_PENDINGEXECUTECOMMANDTIMING; //Initialize timing to 20us!
	ATA[channel].Drive[drive].commandstatus = 3; //We're pending until ready!
}

byte ATAPI_common_spin_response(byte channel, byte drive, byte spinupdown, byte dowait)
{
	if (ATA[channel].Drive[drive].ATAPI_caddyejected == 1) //Caddy is ejected?
	{
		ATAPI_SET_SENSE(channel, drive, 0x02, 0x3A, 0x02, 1); //Drive not ready. Tray open.
		return 0; //Abort the command!
	}
	switch (ATA[channel].Drive[drive].PendingLoadingMode)
	{
	case LOAD_IDLE:
		if (spinupdown && (spinupdown!=2)) //To spin up and not a keep spinning operation?
		{
			ATA[channel].Drive[drive].PendingLoadingMode = LOAD_DISC_LOADING;
			ATA[channel].Drive[drive].ATAPI_diskchangeTimeout = ATAPI_DISKCHANGETIMING; //Wait for availability!
			ATA[channel].Drive[drive].ATAPI_diskchangeDirection = ATAPI_DYNAMICLOADINGPROCESS; //We're unchanged from now on!
			ATA[channel].Drive[drive].PendingSpinType = ATAPI_SPINUP; //We're spinning up!
			goto applyDiscLoadingState; //We're reporting to load!
		}
		else if (spinupdown == 2) //Need to be kept running?
		{
			//Not ready!
			ATAPI_SET_SENSE(channel, drive, 0x02, 0x04, 0x00, 1); //Drive not ready
			return 0; //Abort the command!
		}
		break;
	case LOAD_READY:
	case LOAD_SPINDOWN: //Requested to be spinning down?
		if (spinupdown) //To spin up or a keep spinning operation?
		{
			//Tick the timer if needed!
			if (ATA[channel].Drive[drive].ATAPI_diskchangeTimeout)
			{
				ATA[channel].Drive[drive].ATAPI_diskchangeTimeout = ATAPI_SPINDOWN_TIMEOUT; //Wait for availability!
			}
			else //Wait more!
			{
				ATA[channel].Drive[drive].ATAPI_diskchangeTimeout = ATAPI_SPINDOWN_TIMEOUT; //Wait for availability!
			}
			ATA[channel].Drive[drive].ATAPI_diskchangeDirection = ATAPI_DYNAMICLOADINGPROCESS; //We're unchanged from now on!
			ATA[channel].Drive[drive].PendingSpinType = ATAPI_SPINDOWN; //We're spinning down!
			ATA[channel].Drive[drive].PendingLoadingMode = LOAD_READY; //Handle it like a loading ready, abort any spindown!
		}
		break;
	case LOAD_NO_DISC:
		ATAPI_SET_SENSE(channel, drive, 0x02, 0x3A, 0x01, 1); //Medium not present - Tray closed
		return 0; //Abort the command!
		break;
	case LOAD_INSERT_CD:
	case LOAD_EJECTING: //Ejecting the disc?
		ATAPI_SET_SENSE(channel,drive,0x02,0x3A,0x02, 1); //Medium not present - Tray open
		return 0; //Abort the command!
		break;
	case LOAD_DISC_LOADING:
		applyDiscLoadingState:
		if ((ATA[channel].Drive[drive].ATAPI_diskChanged || ATA[channel].Drive[drive].ATAPI_mediaChanged2) && (dowait==0))
		{
			ATAPI_SET_SENSE(channel,drive,0x02,0x04,0x01, 1); //Medium is becoming available
			return 0; //Abort the command!
		}
		else if (dowait) //Waiting?
		{
			ATAPI_PendingExecuteCommand(channel, drive); //Start pending again four our wait time execution!
			return 2; //Pending to execute!
		}
		else //Becoming ready and not waiting?
		{
			ATAPI_SET_SENSE(channel, drive, 0x02, 0x04, 0x01, 1); //Medium is becoming available
			return 0; //Abort the command!
		}
		break;
	case LOAD_DISC_READIED:
		ATA[channel].Drive[drive].PendingLoadingMode = LOAD_READY;
		if (ATA[channel].Drive[drive].ATAPI_diskChanged || ATA[channel].Drive[drive].ATAPI_mediaChanged2)
		{
			if (spinupdown==1)
			{
				ATA[channel].Drive[drive].ATAPI_diskChanged = 0; //Not changed anymore!
				ATA[channel].Drive[drive].ATAPI_mediaChanged2 = 0; //Not changed anymore!
			}
			ATAPI_SET_SENSE(channel,drive,0x02,0x28,0x00, 1); //Medium is ready (has changed)
			return 0; //Abort the command!
		}
		break;
	default: //abort()?
		break;
	}
	return 1; //Continue the command normally?
}

byte CDROM_soundGenerator(void* buf, uint_32 length, byte stereo, void *userdata) //Generate a sample!
{
	uint_32 c;
	c = length; //Init c!

	static uint_32 last = 0;
	INLINEREGISTER uint_32 buffer;

	SOUNDDOUBLEBUFFER *doublebuffer = (SOUNDDOUBLEBUFFER *)userdata; //Our double buffered sound input to use!
	int_32 mono_converter;
	sample_stereo_p data_stereo;
	sword *data_mono;
	if (stereo) //Stereo processing?
	{
		data_stereo = (sample_stereo_p)buf; //The data in correct samples!
		for (;;) //Fill it!
		{
			//Left and right samples are the same: we're a mono signal!
			readDoubleBufferedSound32(doublebuffer, &last); //Generate a stereo sample if it's available!
			buffer = last; //Load the last sample for processing!
			data_stereo->l = unsigned2signed16((word)buffer); //Load the last generated sample(left)!
			buffer >>= 16; //Shift low!
			data_stereo->r = unsigned2signed16((word)buffer); //Load the last generated sample(right)!
			++data_stereo; //Next stereo sample!
			if (!--c) return SOUNDHANDLER_RESULT_FILLED; //Next item!
		}
	}
	else //Mono processing?
	{
		data_mono = (sword *)buf; //The data in correct samples!
		for (;;) //Fill it!
		{
			//Left and right samples are the same: we're a mono signal!
			readDoubleBufferedSound32(doublebuffer, &last); //Generate a stereo sample if it's available!
			buffer = last; //Load the last sample for processing!
			mono_converter = unsigned2signed16((word)buffer); //Load the last generated sample(left)!
			buffer >>= 16; //Shift low!
			mono_converter += unsigned2signed16((word)buffer); //Load the last generated sample(right)!
			mono_converter = LIMITRANGE(mono_converter, SHRT_MIN, SHRT_MAX); //Clip our data to prevent overflow!
			*data_mono++ = mono_converter; //Save the sample and point to the next mono sample!
			if (!--c) return SOUNDHANDLER_RESULT_FILLED; //Next item!
		}
	}
}

void ATAPI_renderAudioSample(byte channel, byte slave, sword left, sword right)
{
	writeDoubleBufferedSound32(&ATA[channel].Drive[slave].AUDIO_PLAYER.soundbuffer, (signed2unsigned16(right) << 16) | signed2unsigned16(left)); //Output the sample to the renderer!
}

void ATAPI_loadtrackinfo(byte channel, byte slave) //Retrieves the track number of a MSF address!
{
	TRACK_GEOMETRY *g;
	byte cue_track;
	byte M, S, F;
	for (cue_track = 1; cue_track < 100; ++cue_track) //Check all tracks!
	{
		g = &ATA[channel].Drive[slave].geometries[cue_track-1]; //The track's geometry precalcs!
		CDROM_selecttrack(ATA_Drives[channel][slave], cue_track); //Specified track!
		CDROM_selectsubtrack(ATA_Drives[channel][slave], 0); //All subtracks!
		if ((g->cueresult = cueimage_getgeometry(ATA_Drives[channel][slave], &g->cue_M, &g->cue_S, &g->cue_F, &g->cue_startM, &g->cue_startS, &g->cue_startF, &g->cue_endM, &g->cue_endS, &g->cue_endF, 0)) != 0) //Geometry gotten?
		{
			g->cuepostgapresult = cueimage_getgeometry(ATA_Drives[channel][slave], &g->cue_postgapM, &g->cue_postgapS, &g->cue_postgapF, &g->cue_postgapstartM, &g->cue_postgapstartS, &g->cue_postgapstartF, &g->cue_postgapendM, &g->cue_postgapendS, &g->cue_postgapendF, 2); //Geometry gotten?
			//Give the start M,S,F for this track!
			if ((g->cue_trackskip = cueimage_readsector(ATA_Drives[channel][slave], g->cue_startM, g->cue_startS, g->cue_startF, NULL, 0)) != 0) //Try to read as specified!
			{
				g->cue_postgapskip = cueimage_readsector(ATA_Drives[channel][slave], g->cue_postgapstartM, g->cue_postgapstartS, g->cue_postgapstartF, NULL, 0); //Try to read as specified!
				if (g->cue_trackskip < -2) //Skipping more?
				{
					g->pregapsize = (uint_32)(-(g->cue_trackskip + 2)); //More pregap to skip!
				}
				else
				{
					g->pregapsize = 0;
				}
				if (g->cuepostgapresult != 0) //Gotten postgap?
				{
					if (g->cue_postgapskip < -2) //Skipping more after this track?
					{
						g->postgapsize = (uint_32)(-(g->cue_postgapskip + 2)); //More postgap to skip for the next track!
					}
					else
					{
						g->cue_postgapskip = 0;
					}
				}
				else
				{
					g->cue_postgapskip = 0;
				}
				LBA2MSFbin(MSF2LBAbin(g->cue_startM, g->cue_startS, g->cue_startF) + g->pregapsize, &M, &S, &F); //Add the pregap size for a valid start address to get the type!
				if ((g->cue_trackskip2 = cueimage_readsector(ATA_Drives[channel][slave], M, S, F, &ATA[channel].Drive[slave].data[0], 0)) >= 1) //Try to find out if we're here!
				{
					switch (g->cue_trackskip2)
					{
					case 1 + MODE_MODE1DATA: //Mode 1 block?
					case 1 + MODE_MODEXA: //Mode XA block?
					case 1 + MODE_AUDIO: //Audio block?
						//Valid track to use?
						g->tracktype = (byte)g->cue_trackskip2; //The track type!
						continue; //Continue searching!
					default: //Unknown/unsupported mode/OOR?
						if (g->cue_trackskip >= 0) //Valid to report?
						{
							g->tracktype = (byte)g->cue_trackskip2; //The track type!
						}
						else
						{
							g->tracktype = 0; //The unknown track type!
						}
						continue; //Continue searching!
					}
				}
				else
				{
					g->tracktype = 0; //Unknown track type!
				}
				//Not found yet? Continue searching!
			}
			//Failed checking the track skip? Ignore the track!
		}
	}
}

sword ATAPI_gettrackinfo(byte channel, byte slave, byte M, byte S, byte F, byte *tracknumber, uint_32 *pregapsize, uint_32 *postgapsize, byte *startM, byte *startS, byte *startF, byte *tracktype) //Retrieves the track number of a MSF address!
{
	TRACK_GEOMETRY *g;
	sword result = -1; //Default: not found!
	byte requestedtrack = 0;
	byte cue_track;
	result = -1; //Default: not found!
	uint_32 reqLBA;
	reqLBA = MSF2LBAbin(M, S, F); //What do we want to find out?
	for (cue_track = 1; cue_track < 100; ++cue_track) //Check all tracks!
	{
		g = &ATA[channel].Drive[slave].geometries[cue_track-1]; //Get the tracks' geometry information that was preloaded!
		if ((g->cueresult) != 0) //Geometry gotten?
		{
			requestedtrack = ((reqLBA >= MSF2LBAbin(g->cue_startM, g->cue_startS, g->cue_startF)) && (reqLBA <= MSF2LBAbin(g->cue_endM, g->cue_endS, g->cue_endF))); //Are we the requested track?
			if (requestedtrack) //Is this track requested into for?
			{
				result = 1; //We've found the track that's requested!
				//Give the start M,S,F for this track!
				if (startM) *startM = g->cue_startM; //Start of the track!
				if (startS) *startS = g->cue_startS; //Start of the track!
				if (startF) *startF = g->cue_startF; //Start of the track!
				if (tracknumber) *tracknumber = cue_track; //What track number are we!
			}
			if ((g->cue_trackskip) != 0) //Try to read as specified!
			{
				if (g->cue_trackskip < -2) //Skipping more?
				{
					if (pregapsize && requestedtrack) //Want to know the pregap size for this track?
					{
						*pregapsize = (uint_32)(-(g->cue_trackskip + 2)); //More pregap to skip!
					}
				}
				if (g->cuepostgapresult != 0) //Gotten postgap?
				{
					if (g->cue_postgapskip < -2) //Skipping more after this track?
					{
						if (postgapsize && requestedtrack) //Want to know the postgap size?
						{
							*postgapsize = (uint_32)(-(g->cue_postgapskip + 2)); //More postgap to skip for the next track!
						}
					}
				}
				if ((g->cue_trackskip2) >= 1) //Try to find out if we're here!
				{
					switch (g->cue_trackskip2)
					{
					case 1 + MODE_MODE1DATA: //Mode 1 block?
					case 1 + MODE_MODEXA: //Mode XA block?
					case 1 + MODE_AUDIO: //Audio block?
						//Valid track to use?
						if (tracktype && requestedtrack) *tracktype = (byte)g->cue_trackskip2; //The track type!
						continue; //Continue searching!
					default: //Unknown/unsupported mode/OOR?
						if (tracktype >= 0) //Valid to report?
						{
							if (tracktype && requestedtrack) *tracktype = (byte)g->cue_trackskip2; //The track type!
						}
						else
						{
							if (tracktype && requestedtrack) *tracktype = 0; //The unknown track type!
						}
						continue; //Continue searching!
					}
				}
				//Not found yet? Continue searching!
			}
			//Failed checking the track skip? Ignore the track!
		}
	}
	return result; //Give the result!
}

byte curtrack_type = 0, curtrack_nr=0;
void ATAPI_tickAudio(byte channel, byte slave)
{
	word sampleleft, sampleright, samplepos;
	int_64 loadstatus;
	if (likely(ATA[channel].Drive[slave].AUDIO_PLAYER.status != PLAYER_PLAYING)) //Not running?
	{
		finishPlayback: //Playback is finished, no sample!
		//Render a silent sample!
		ATAPI_renderAudioSample(channel, slave, 0, 0); //Render a silent sample!
		switch (ATA[channel].Drive[slave].AUDIO_PLAYER.status) //What are we doing?
		{
		case PLAYER_PAUSED: //Being paused?
		case PLAYER_SCANNING: //Scanning?
		default: //Default(initialized)?
		case PLAYER_INITIALIZED: //Initialized?
			//Do something while idling?
			break;
		}
	}
	else //Playback?
	{
		if (ATA[channel].Drive[slave].AUDIO_PLAYER.samplepos < 2349) //Rendering a buffer?
		{
			samplepos = ATA[channel].Drive[slave].AUDIO_PLAYER.samplepos; //Load the sample position!
			ATAPI_renderSamplepos:
			sampleleft = ATA[channel].Drive[slave].AUDIO_PLAYER.samples[samplepos++]; //Low byte!
			sampleleft |= (ATA[channel].Drive[slave].AUDIO_PLAYER.samples[samplepos++]<<8); //High byte!
			sampleright = ATA[channel].Drive[slave].AUDIO_PLAYER.samples[samplepos++]; //Low byte!
			sampleright |= ATA[channel].Drive[slave].AUDIO_PLAYER.samples[samplepos++]<<8; //High byte!
			ATA[channel].Drive[slave].AUDIO_PLAYER.samplepos = samplepos; //Load the new sample position!
			//Render an audio sample!
			ATAPI_renderAudioSample(channel, slave, unsigned2signed16(sampleleft), unsigned2signed16(sampleright)); //Render a silent sample!
		}
		else //Need to load a new frame?
		{
			if ((ATA[channel].Drive[slave].AUDIO_PLAYER.M == ATA[channel].Drive[slave].AUDIO_PLAYER.endM) &&
				(ATA[channel].Drive[slave].AUDIO_PLAYER.S == ATA[channel].Drive[slave].AUDIO_PLAYER.endS) &&
				(ATA[channel].Drive[slave].AUDIO_PLAYER.F == ATA[channel].Drive[slave].AUDIO_PLAYER.endF)) //Final frame reached?
			{
				ATA[channel].Drive[slave].AUDIO_PLAYER.status = PLAYER_INITIALIZED; //Finished playback, go back to initialized state!
				ATA[channel].Drive[slave].AUDIO_PLAYER.effectiveplaystatus = PLAYER_STATUS_FINISHED; //We're finished!
				goto finishPlayback;
			}
			//Load a new sample buffer from the disk!
			switch (ATAPI_gettrackinfo(channel, slave, ATA[channel].Drive[slave].AUDIO_PLAYER.M, ATA[channel].Drive[slave].AUDIO_PLAYER.S, ATA[channel].Drive[slave].AUDIO_PLAYER.F, &curtrack_nr, NULL, NULL, NULL, NULL, NULL, &curtrack_type)) //Is the track found?
			{
			case 0: //Errored out?
				ATA[channel].Drive[slave].AUDIO_PLAYER.status = PLAYER_INITIALIZED; //We're erroring out!
				ATAPI_SET_SENSE(channel, slave, SENSE_ILLEGAL_REQUEST, ASC_END_OF_USER_AREA_ENCOUNTERED_ON_THIS_TRACK, 0x00, 0); //Medium is becoming available
				ATA[channel].Drive[slave].AUDIO_PLAYER.effectiveplaystatus = PLAYER_STATUS_ERROREDOUT; //We're finished!
				ATAPI_command_reportError(channel, slave);
				goto finishPlayback;
				break;
			case 1: //Playing?
				if (curtrack_type != (1 + MODE_AUDIO)) //Invalid track type?
				{
					ATA[channel].Drive[slave].lastformat = 0x14; //Last format seen: data track!
					ATA[channel].Drive[slave].lastM = ATA[channel].Drive[slave].AUDIO_PLAYER.M; //Our last position!
					ATA[channel].Drive[slave].lastS = ATA[channel].Drive[slave].AUDIO_PLAYER.S; //Our last position!
					ATA[channel].Drive[slave].lastF = ATA[channel].Drive[slave].AUDIO_PLAYER.F; //Our last position!
					ATA[channel].Drive[slave].AUDIO_PLAYER.status = PLAYER_INITIALIZED; //We're erroring out!
					//Error out on transition of track type!
					ATAPI_SET_SENSE(channel, slave, SENSE_ILLEGAL_REQUEST, ASC_END_OF_USER_AREA_ENCOUNTERED_ON_THIS_TRACK, 0x00, 0); //Medium is becoming available
					ATA[channel].Drive[slave].AUDIO_PLAYER.effectiveplaystatus = PLAYER_STATUS_ERROREDOUT; //We're finished!
					ATAPI_command_reportError(channel, slave);
					goto finishPlayback;
				}
				if ((ATA[channel].Drive[slave].AUDIO_PLAYER.trackref_track != curtrack_nr) && (ATA[channel].Drive[slave].ATAPI_ModeData[(0x0E << 8) | (0x02 - 2)] & 2)) //Stop on track crossing activated?
				{
					ATA[channel].Drive[slave].AUDIO_PLAYER.status = PLAYER_INITIALIZED; //We're erroring out!
					//Error out on transition of track number crossing!
					ATA[channel].Drive[slave].AUDIO_PLAYER.effectiveplaystatus = PLAYER_STATUS_ERROREDOUT; //We're finished!
					ATAPI_SET_SENSE(channel, slave, SENSE_ILLEGAL_REQUEST, ASC_ILLEGAL_MODE_FOR_THIS_TRACK_OR_INCOMPATIBLE_MEDIUM, 0x00, 0); //Medium is becoming available
					ATAPI_command_reportError(channel, slave);
					goto finishPlayback;
				}
				//Otherwise, just play the audio track!
				break; //Start playing normally!
			case -1: //Track not found?
				ATA[channel].Drive[slave].AUDIO_PLAYER.status = PLAYER_INITIALIZED; //We're erroring out!
				ATA[channel].Drive[slave].AUDIO_PLAYER.effectiveplaystatus = PLAYER_STATUS_ERROREDOUT; //We're finished!
				//Error out on the track being out of range!
				ATAPI_SET_SENSE(channel, slave, SENSE_ILLEGAL_REQUEST, ASC_ILLEGAL_MODE_FOR_THIS_TRACK_OR_INCOMPATIBLE_MEDIUM, 0x00, 0); //Medium is becoming available
				ATAPI_command_reportError(channel, slave);
				goto finishPlayback;
				break;
			}
			ATA[channel].Drive[slave].lasttrack = curtrack_nr; //What track are we on!
			CDROM_selecttrack(ATA_Drives[channel][slave], 0); //Any track!
			CDROM_selectsubtrack(ATA_Drives[channel][slave], 0); //All subtracks!
			if ((loadstatus = cueimage_readsector(ATA_Drives[channel][slave], ATA[channel].Drive[slave].AUDIO_PLAYER.M, ATA[channel].Drive[slave].AUDIO_PLAYER.S, ATA[channel].Drive[slave].AUDIO_PLAYER.F, &ATA[channel].Drive[slave].AUDIO_PLAYER.samples[0], sizeof(ATA[channel].Drive[slave].AUDIO_PLAYER.samples))) != 0) //Try to find out if we're here!
			{
				switch (loadstatus) //How did the load go?
				{
				case 1 + MODE_AUDIO: //Audio track? It's valid!
					ATA[channel].Drive[slave].lasttrack = curtrack_nr; //Last track seen!
					ATA[channel].Drive[slave].lastformat = 0x10; //Last format seen: audio track!
					ATA[channel].Drive[slave].lastM = ATA[channel].Drive[slave].AUDIO_PLAYER.M; //Our last position!
					ATA[channel].Drive[slave].lastS = ATA[channel].Drive[slave].AUDIO_PLAYER.S; //Our last position!
					ATA[channel].Drive[slave].lastF = ATA[channel].Drive[slave].AUDIO_PLAYER.F; //Our last position!
					LBA2MSFbin(MSF2LBAbin(ATA[channel].Drive[slave].AUDIO_PLAYER.M, ATA[channel].Drive[slave].AUDIO_PLAYER.S, ATA[channel].Drive[slave].AUDIO_PLAYER.F) + 1, &ATA[channel].Drive[slave].AUDIO_PLAYER.M, &ATA[channel].Drive[slave].AUDIO_PLAYER.S, &ATA[channel].Drive[slave].AUDIO_PLAYER.F); //Increase the MSF address to the next frame to check next!
					break; //Success!
				default: //Invalid type or gap?
					if (loadstatus < -2) //Gap?
					{
						memset(&ATA[channel].Drive[slave].AUDIO_PLAYER.samples, 0, sizeof(ATA[channel].Drive[slave].AUDIO_PLAYER.samples)); //Clear the samples buffer for silence!
						LBA2MSFbin(MSF2LBAbin(ATA[channel].Drive[slave].AUDIO_PLAYER.M, ATA[channel].Drive[slave].AUDIO_PLAYER.S, ATA[channel].Drive[slave].AUDIO_PLAYER.F) + 1, &ATA[channel].Drive[slave].AUDIO_PLAYER.M, &ATA[channel].Drive[slave].AUDIO_PLAYER.S, &ATA[channel].Drive[slave].AUDIO_PLAYER.F); //Increase the MSF address to the next frame to check next!
					}
					else //Invalid type!
					{
						memset(&ATA[channel].Drive[slave].AUDIO_PLAYER.samples, 0, sizeof(ATA[channel].Drive[slave].AUDIO_PLAYER.samples)); //Clear the samples buffer!
						ATA[channel].Drive[slave].AUDIO_PLAYER.effectiveplaystatus = PLAYER_STATUS_ERROREDOUT; //We're finished!
						ATA[channel].Drive[slave].AUDIO_PLAYER.status = PLAYER_INITIALIZED; //Finished playback, go back to initialized state!
						if (loadstatus == -1) //End of track reached?
						{
							//Error out!
							ATAPI_SET_SENSE(channel, slave, SENSE_ILLEGAL_REQUEST, ASC_END_OF_USER_AREA_ENCOUNTERED_ON_THIS_TRACK, 0x00, 0); //Medium is becoming available
							ATAPI_command_reportError(channel, slave);
						}
						else //Invalid type!
						{
							//Error out!
							ATAPI_SET_SENSE(channel, slave, SENSE_ILLEGAL_REQUEST, ASC_ILLEGAL_MODE_FOR_THIS_TRACK_OR_INCOMPATIBLE_MEDIUM, 0x00, 0); //Medium is becoming available
							ATAPI_command_reportError(channel, slave);
						}
						goto finishPlayback; //Finished playback!
					}
				}

				//Start the new sample playback!
				samplepos = 0; //The start of the new buffer!
				goto ATAPI_renderSamplepos; //Start the normal renderer for the first sample!
			}
			else //Failed to load new audio?
			{
				//Error out!
				ATA[channel].Drive[slave].AUDIO_PLAYER.status = PLAYER_INITIALIZED; //We're erroring out!
				ATA[channel].Drive[slave].AUDIO_PLAYER.effectiveplaystatus = PLAYER_STATUS_ERROREDOUT; //We're finished!
				goto finishPlayback; //Finished playback!
			}
		}
	}
}

byte ATAPI_audioplayer_startPlayback(byte channel, byte drive, byte startM, byte startS, byte startF, byte endM, byte endS, byte endF) //Start playback in this range!
{
	uint_32 noCUELBA;
	ATA[channel].Drive[drive].AUDIO_PLAYER.trackref_M = startM; //Where to start playing(track reference)!
	ATA[channel].Drive[drive].AUDIO_PLAYER.trackref_S = startS; //Where to start playing(track reference)!
	ATA[channel].Drive[drive].AUDIO_PLAYER.trackref_F = startF; //Where to start playing(track reference)!
	ATA[channel].Drive[drive].AUDIO_PLAYER.M = startM; //Where to start playing!
	ATA[channel].Drive[drive].AUDIO_PLAYER.S = startS; //Where to start playing!
	ATA[channel].Drive[drive].AUDIO_PLAYER.F = startF; //Where to start playing!
	ATA[channel].Drive[drive].AUDIO_PLAYER.endM = endM; //Where to stop playing!
	ATA[channel].Drive[drive].AUDIO_PLAYER.endS = endS; //Where to stop playing!
	ATA[channel].Drive[drive].AUDIO_PLAYER.endF = endF; //Where to stop playing!
	ATA[channel].Drive[drive].AUDIO_PLAYER.status = PLAYER_PLAYING; //We're playing now!
	ATA[channel].Drive[drive].AUDIO_PLAYER.effectiveplaystatus = PLAYER_STATUS_PLAYING_IN_PROGRESS; //We're finished!
	ATA[channel].Drive[drive].AUDIO_PLAYER.samplepos = 2352; //We're starting a new transfer, start loading the new frame to render!
	if (!getCUEimage(ATA_Drives[channel][drive])) //Not a valid cue image to play back?
	{
		noCUELBA = MSF2LBAbin(startM, startS, startF);
		ATA[channel].Drive[drive].AUDIO_PLAYER.status = PLAYER_INITIALIZED; //We're erroring out!
		ATA[channel].Drive[drive].AUDIO_PLAYER.effectiveplaystatus = PLAYER_STATUS_ERROREDOUT; //We're finished!
		if (noCUELBA >= 150)
		{
			noCUELBA -= 150; //Discard the pregap, if possible to get the physical LBA address!
		}
		else //In the pregap?
		{
			goto playback_noCUELBA_invalidtype;
		}
		if (noCUELBA > ATA[channel].Drive[drive].ATAPI_disksize) //Block Out of range?
		{
		playback_noCUELBA_invalidtype: //Invalid type due to OOR in the pregap?
			//Error out because it's Out of Range!
			ATAPI_SET_SENSE(channel, drive, SENSE_ILLEGAL_REQUEST, ASC_LOGICAL_BLOCK_OOR, 0x00, 0); //Medium is becoming available
			ATAPI_command_reportError(channel, drive);
		}
		else //Invalid data type?
		{
			//Fill our last read data for the request!
			ATA[channel].Drive[drive].lastM = startM; //Last address
			ATA[channel].Drive[drive].lastS = startS; //Last address
			ATA[channel].Drive[drive].lastF = startF; //Last address
			ATA[channel].Drive[drive].lastformat = 0x14; //Data track!
			ATA[channel].Drive[drive].lasttrack = 1; //Last track!
			//Error out because it's data type instead of audio type!
			ATAPI_SET_SENSE(channel, drive, SENSE_ILLEGAL_REQUEST, ASC_ILLEGAL_MODE_FOR_THIS_TRACK_OR_INCOMPATIBLE_MEDIUM, 0x00, 0); //Medium is becoming available
			ATAPI_command_reportError(channel, drive);
		}
		return 0; //Failure!
	}
	switch (ATAPI_gettrackinfo(channel, drive, startM, startS, startF, &ATA[channel].Drive[drive].AUDIO_PLAYER.trackref_track, NULL, NULL, &ATA[channel].Drive[drive].AUDIO_PLAYER.trackref_M, &ATA[channel].Drive[drive].AUDIO_PLAYER.trackref_S, &ATA[channel].Drive[drive].AUDIO_PLAYER.trackref_F, &ATA[channel].Drive[drive].AUDIO_PLAYER.trackref_type)) //Is the track found?
	{
	case 0: //Errored out?
		ATA[channel].Drive[drive].AUDIO_PLAYER.status = PLAYER_INITIALIZED; //We're erroring out!
		ATA[channel].Drive[drive].AUDIO_PLAYER.effectiveplaystatus = PLAYER_STATUS_ERROREDOUT; //We're finished!
		ATAPI_SET_SENSE(channel, drive, SENSE_ILLEGAL_REQUEST, ASC_LOGICAL_BLOCK_OOR, 0x00, 0); //Medium is becoming available
		ATAPI_command_reportError(channel, drive);
		break;
	case 1: //Playing?
		if (ATA[channel].Drive[drive].AUDIO_PLAYER.trackref_type != (1 + MODE_AUDIO)) //Invalid track type?
		{
			ATA[channel].Drive[drive].AUDIO_PLAYER.status = PLAYER_INITIALIZED; //We're erroring out!
			ATA[channel].Drive[drive].AUDIO_PLAYER.effectiveplaystatus = PLAYER_STATUS_ERROREDOUT; //We're finished!
			//Error out!
			ATAPI_SET_SENSE(channel, drive, SENSE_ILLEGAL_REQUEST, ASC_ILLEGAL_MODE_FOR_THIS_TRACK_OR_INCOMPATIBLE_MEDIUM, 0x00, 0); //Medium is becoming available
			ATAPI_command_reportError(channel, drive);
			return 0; //Failure!
		}
		//Otherwise, just play!
		return 1; //OK
		break; //Start playing normally!
	case -1: //Track not found?
		ATA[channel].Drive[drive].AUDIO_PLAYER.status = PLAYER_INITIALIZED; //We're erroring out!
		ATA[channel].Drive[drive].AUDIO_PLAYER.effectiveplaystatus = PLAYER_STATUS_ERROREDOUT; //We're finished!
		ATAPI_SET_SENSE(channel, drive, SENSE_ILLEGAL_REQUEST, ASC_LOGICAL_BLOCK_OOR, 0x00, 0); //Medium is becoming available
		ATAPI_command_reportError(channel, drive);
		break;
	}
	return 0; //Failure!
}

void handleATAPIcaddyeject(byte channel, byte drive)
{
	requestEjectDisk(ATA_Drives[channel][drive]); //Request for the specified disk to be ejected!
	ATA[channel].Drive[drive].allowDiskInsertion = 1; //Allow the disk to be inserted afterwards!
	ATA[channel].Drive[drive].ATAPI_caddyejected = 1; //We're ejected!
	EMU_setDiskBusy(ATA_Drives[channel][drive], 0 | (ATA[channel].Drive[drive].ATAPI_caddyejected << 1)); //We're not reading anymore!
	if (!ATA[channel].Drive[drive].ATAPI_diskchangeTimeout) //Not already pending?
	{
		ATA[channel].Drive[drive].ATAPI_diskchangeTimeout = ATAPI_INSERTION_EJECTING_FASTTIME; //New timer!
	}
	else
	{
		ATA[channel].Drive[drive].ATAPI_diskchangeTimeout = ATAPI_INSERTION_EJECTING_FASTTIME; //Add to pending timing!
	}
	ATA[channel].Drive[drive].ATAPI_diskchangeDirection = ATAPI_DYNAMICLOADINGPROCESS; //Start the insertion mechanism!
	ATA[channel].Drive[drive].PendingLoadingMode = LOAD_EJECTING; //Loading and inserting the CD is now starting!
	ATA[channel].Drive[drive].PendingSpinType = ATAPI_DONTSPIN; //We're firing an don't spin event!
}

void updateATA(DOUBLE timepassed) //ATA timing!
{
	uint_32 samples;
	if (timepassed) //Anything passed?
	{
		//Tick the audio player!
		if (CDROM_channel != 0xFF) //Is there a CD-ROM channel?
		{
			if (likely(ATA[CDROM_channel].playerTick)) //Valid to tick?
			{
				ATA[CDROM_channel].playerTiming += timepassed; //Tick what's needed!
				if (ATA[CDROM_channel].playerTiming >= ATA[CDROM_channel].playerTick) //One sample or more to be timed?
				{
					samples = (uint_32)(ATA[CDROM_channel].playerTiming / ATA[CDROM_channel].playerTick); //How many samples to tick?
					ATA[CDROM_channel].playerTiming -= (ATA[CDROM_channel].playerTick*((DOUBLE)samples)); //We're ticking them off!
					for (; samples;--samples) //Samples left to tick?
					{
						ATAPI_tickAudio(CDROM_channel, 0); //Tick the Master!
						ATAPI_tickAudio(CDROM_channel, 1); //Tick the Slave!
					}
				}
			}
		}

		if (ATA[0].Drive[0].ATAPI_caddyejected == 2) //Request caddy to be inserted?
		{
			ATAPI_insertCD(ATA_Drives[0][0], 0, 0); //Insert the caddy!
		}

		if (ATA[0].Drive[1].ATAPI_caddyejected == 2) //Request caddy to be inserted?
		{
			ATAPI_insertCD(ATA_Drives[0][1], 0, 1); //Insert the caddy!
		}

		if (ATA[1].Drive[0].ATAPI_caddyejected == 2) //Request caddy to be inserted?
		{
			ATAPI_insertCD(ATA_Drives[1][0], 1, 0); //Insert the caddy!
		}

		if (ATA[1].Drive[1].ATAPI_caddyejected == 2) //Request caddy to be inserted?
		{
			ATAPI_insertCD(ATA_Drives[1][1], 1, 1); //Insert the caddy!
		}

		//Handle ATA drive select timing!
		if (ATA[0].driveselectTiming) //Timing driveselect?
		{
			ATA[0].driveselectTiming -= timepassed; //Time until timeout!
			if (ATA[0].driveselectTiming<=0.0) //Timeout?
			{
				ATA[0].driveselectTiming = 0.0; //Timer finished!
			}
		}
		if (ATA[1].driveselectTiming) //Timing driveselect?
		{
			ATA[1].driveselectTiming -= timepassed; //Time until timeout!
			if (ATA[1].driveselectTiming<=0.0) //Timeout?
			{
				ATA[1].driveselectTiming = 0.0; //Timer finished!
			}
		}

		//Handle ATA reset timing!

		if (ATA[0].Drive[0].resetTiming) //Timing reset?
		{
			ATA[0].Drive[0].resetTiming -= timepassed; //Time until timeout!
			if (ATA[0].Drive[0].resetTiming<=0.0) //Timeout?
			{
				ATA[0].Drive[0].resetTiming = 0.0; //Timer finished!
				if (ATA[0].Drive[0].commandstatus==3) //Busy only?
					ATA[0].Drive[0].commandstatus = 0; //We're ready now!
				if (ATA[0].Drive[0].resetTriggersIRQ) //Triggers an IRQ(for ATAPI devices)?
				{
					ATA_channel = 0;
					ATA_slave = 0;
					ATAPI_generateInterruptReason(0, 0); //Generate our reason!
					ATA_IRQ(0, 0, (DOUBLE)0, 0); //Finish timeout!
				}
			}
		}

		if (ATA[0].Drive[1].resetTiming) //Timing reset?
		{
			ATA[0].Drive[1].resetTiming -= timepassed; //Time until timeout!
			if (ATA[0].Drive[1].resetTiming<=0.0) //Timeout?
			{
				ATA[0].Drive[1].resetTiming = 0.0; //Timer finished!
				if (ATA[0].Drive[1].commandstatus==3) //Busy only?
					ATA[0].Drive[1].commandstatus = 0; //We're ready now!
				if (ATA[0].Drive[1].resetTriggersIRQ) //Triggers an IRQ(for ATAPI devices)?
				{
					ATA_channel = 0;
					ATA_slave = 1;
					ATAPI_generateInterruptReason(0, 1); //Generate our reason!
					ATA_IRQ(0, 1, (DOUBLE)0, 0); //Finish timeout!
				}
			}
		}

		if (ATA[1].Drive[0].resetTiming) //Timing reset?
		{
			ATA[1].Drive[0].resetTiming -= timepassed; //Time until timeout!
			if (ATA[1].Drive[0].resetTiming<=0.0) //Timeout?
			{
				ATA[1].Drive[0].resetTiming = 0.0; //Timer finished!
				if (ATA[1].Drive[0].commandstatus==3) //Busy only?
					ATA[1].Drive[0].commandstatus = 0; //We're ready now!
				if (ATA[1].Drive[0].resetTriggersIRQ) //Triggers an IRQ(for ATAPI devices)?
				{
					ATA_channel = 1;
					ATA_slave = 0;
					ATAPI_generateInterruptReason(1, 0); //Generate our reason!
					ATA_IRQ(1, 0, (DOUBLE)0, 0); //Finish timeout!
				}
			}
		}

		if (ATA[1].Drive[1].resetTiming) //Timing reset?
		{
			ATA[1].Drive[1].resetTiming -= timepassed; //Time until timeout!
			if (ATA[1].Drive[1].resetTiming<=0.0) //Timeout?
			{
				ATA[1].Drive[1].resetTiming = 0.0; //Timer finished!
				if (ATA[1].Drive[1].commandstatus==3) //Busy only?
					ATA[1].Drive[1].commandstatus = 0; //We're ready now!
				if (ATA[1].Drive[1].resetTriggersIRQ) //Triggers an IRQ(for ATAPI devices)?
				{
					ATA_channel = 1;
					ATA_slave = 1;
					ATAPI_generateInterruptReason(1, 1); //Generate our reason!
					ATA_IRQ(1, 1, (DOUBLE)0, 0); //Finish timeout!
				}
			}
		}

		//Handle ATA(PI) IRQ timing!
		if (ATA[0].Drive[0].IRQTimeout) //Timing IRQ?
		{
			ATA[0].Drive[0].IRQTimeout -= timepassed; //Time until timeout!
			if (ATA[0].Drive[0].IRQTimeout<=0.0) //Timeout?
			{
				ATA[0].Drive[0].IRQTimeout = (DOUBLE)0; //Timer finished!
				ATA_channel = 0;
				ATA_slave = 0;
				if (ATA[0].Drive[0].IRQTimeout_busy != 2) //Raise an IRQ?
				{
					ATAPI_generateInterruptReason(0, 0); //Generate our reason!
					ATA_IRQ(0, 0, (DOUBLE)0, 0); //Finish timeout!
				}
			}
		}

		if (ATA[0].Drive[1].IRQTimeout) //Timing IRQ?
		{
			ATA[0].Drive[1].IRQTimeout -= timepassed; //Time until timeout!
			if (ATA[0].Drive[1].IRQTimeout<=0.0) //Timeout?
			{
				ATA[0].Drive[1].IRQTimeout = (DOUBLE)0; //Timer finished!
				ATA_channel = 0;
				ATA_slave = 1;
				if (ATA[0].Drive[1].IRQTimeout_busy != 2) //Raise an IRQ?
				{
					ATAPI_generateInterruptReason(0, 1); //Generate our reason!
					ATA_IRQ(0, 1, (DOUBLE)0, 0); //Finish timeout!
				}
			}
		}

		if (ATA[1].Drive[0].IRQTimeout) //Timing IRQ?
		{
			ATA[1].Drive[0].IRQTimeout -= timepassed; //Time until timeout!
			if (ATA[1].Drive[0].IRQTimeout<=0.0) //Timeout?
			{
				ATA[1].Drive[0].IRQTimeout = (DOUBLE)0; //Timer finished!
				ATA_channel = 1;
				ATA_slave = 0;
				if (ATA[1].Drive[0].IRQTimeout_busy != 2) //Raise an IRQ?
				{
					ATAPI_generateInterruptReason(1, 0); //Generate our reason!
					ATA_IRQ(1, 0, (DOUBLE)0, 0); //Finish timeout!
				}
			}
		}

		if (ATA[1].Drive[1].IRQTimeout) //Timing IRQ?
		{
			ATA[1].Drive[1].IRQTimeout -= timepassed; //Time until timeout!
			if (ATA[1].Drive[1].IRQTimeout<=0.0) //Timeout?
			{
				ATA[1].Drive[1].IRQTimeout = (DOUBLE)0; //Timer finished!
				ATA_channel = 1;
				ATA_slave = 1;
				if (ATA[1].Drive[1].IRQTimeout_busy != 2) //Raise an IRQ?
				{
					ATAPI_generateInterruptReason(1, 1); //Generate our reason!
					ATA_IRQ(1, 1, (DOUBLE)0, 0); //Finish timeout!
				}
			}
		}

		//Handle ATA busy timing!
		if (ATA[0].Drive[0].BusyTiming) //Timing reset?
		{
			ATA[0].Drive[0].BusyTiming -= timepassed; //Time until timeout!
			if (ATA[0].Drive[0].BusyTiming <= 0.0) //Timeout?
			{
				ATA[0].Drive[0].BusyTiming = 0.0; //Timer finished!
			}
		}

		if (ATA[0].Drive[1].BusyTiming) //Timing reset?
		{
			ATA[0].Drive[1].BusyTiming -= timepassed; //Time until timeout!
			if (ATA[0].Drive[1].BusyTiming <= 0.0) //Timeout?
			{
				ATA[0].Drive[1].BusyTiming = 0.0; //Timer finished!
			}
		}

		if (ATA[1].Drive[0].BusyTiming) //Timing reset?
		{
			ATA[1].Drive[0].BusyTiming -= timepassed; //Time until timeout!
			if (ATA[1].Drive[0].BusyTiming <= 0.0) //Timeout?
			{
				ATA[1].Drive[0].BusyTiming = 0.0; //Timer finished!
			}
		}

		if (ATA[1].Drive[1].BusyTiming) //Timing reset?
		{
			ATA[1].Drive[1].BusyTiming -= timepassed; //Time until timeout!
			if (ATA[1].Drive[1].BusyTiming <= 0.0) //Timeout?
			{
				ATA[1].Drive[1].BusyTiming = 0.0; //Timer finished!
			}
		}

		//Handle ATAPI Ready Timing!

		if (ATA[0].Drive[0].ReadyTiming) //Timing reset?
		{
			ATA[0].Drive[0].ReadyTiming -= timepassed; //Time until timeout!
			if (ATA[0].Drive[0].ReadyTiming<=0.0) //Timeout?
			{
				ATA[0].Drive[0].ReadyTiming = 0.0; //Timer finished!
			}
		}

		if (ATA[0].Drive[1].ReadyTiming) //Timing reset?
		{
			ATA[0].Drive[1].ReadyTiming -= timepassed; //Time until timeout!
			if (ATA[0].Drive[1].ReadyTiming<=0.0) //Timeout?
			{
				ATA[0].Drive[1].ReadyTiming = 0.0; //Timer finished!
			}
		}

		if (ATA[1].Drive[0].ReadyTiming) //Timing reset?
		{
			ATA[1].Drive[0].ReadyTiming -= timepassed; //Time until timeout!
			if (ATA[1].Drive[0].ReadyTiming<=0.0) //Timeout?
			{
				ATA[1].Drive[0].ReadyTiming = 0.0; //Timer finished!
			}
		}

		if (ATA[1].Drive[1].ReadyTiming) //Timing reset?
		{
			ATA[1].Drive[1].ReadyTiming -= timepassed; //Time until timeout!
			if (ATA[1].Drive[1].ReadyTiming<=0.0) //Timeout?
			{
				ATA[1].Drive[1].ReadyTiming = 0.0; //Timer finished!
			}
		}

		//Handle ATAPI execute command delay!
		if (ATA[0].Drive[0].ATAPI_PendingExecuteCommand) //Pending execute command?
		{
			ATA[0].Drive[0].ATAPI_PendingExecuteCommand -= timepassed; //Time until finished!
			if (ATA[0].Drive[0].ATAPI_PendingExecuteCommand<=0.0) //Finished?
			{
				ATA[0].Drive[0].ATAPI_PendingExecuteCommand = 0.0; //Timer finished!
				ATA_channel = 0;
				ATA_slave = 0;
				ATAPI_executeCommand(0,0); //Execute the command!
			}
		}
		if (ATA[0].Drive[1].ATAPI_PendingExecuteCommand) //Pending execute command?
		{
			ATA[0].Drive[1].ATAPI_PendingExecuteCommand -= timepassed; //Time until finished!
			if (ATA[0].Drive[1].ATAPI_PendingExecuteCommand<=0.0) //Finished?
			{
				ATA[0].Drive[1].ATAPI_PendingExecuteCommand = 0.0; //Timer finished!
				ATA_channel = 0;
				ATA_slave = 1;
				ATAPI_executeCommand(0,1); //Execute the command!
			}
		}
		if (ATA[1].Drive[0].ATAPI_PendingExecuteCommand) //Pending execute command?
		{
			ATA[1].Drive[0].ATAPI_PendingExecuteCommand -= timepassed; //Time until finished!
			if (ATA[1].Drive[0].ATAPI_PendingExecuteCommand<=0.0) //Finished?
			{
				ATA[1].Drive[0].ATAPI_PendingExecuteCommand = 0.0; //Timer finished!
				ATA_channel = 1;
				ATA_slave = 0;
				ATAPI_executeCommand(1,0); //Execute the command!
			}
		}
		if (ATA[1].Drive[1].ATAPI_PendingExecuteCommand) //Pending execute command?
		{
			ATA[1].Drive[1].ATAPI_PendingExecuteCommand -= timepassed; //Time until finished!
			if (ATA[1].Drive[1].ATAPI_PendingExecuteCommand<=0.0) //Finished?
			{
				ATA[1].Drive[1].ATAPI_PendingExecuteCommand = 0.0; //Timer finished!
				ATA_channel = 1;
				ATA_slave = 1;
				ATAPI_executeCommand(1,1); //Execute the command!
			}
		}

		//Handle ATAPI disk change input!
		if (ATA[0].Drive[0].ATAPI_diskchangeTimeout) //Pending execute transfer?
		{
			ATA[0].Drive[0].ATAPI_diskchangeTimeout -= timepassed; //Time until finished!
			if (ATA[0].Drive[0].ATAPI_diskchangeTimeout<=0.0) //Finished?
			{
				ATA_channel = 0;
				ATA_slave = 0;
				tickATADiskChange(0,0); //Tick the disk changing mechanism!
			}
		}

		if (ATA[0].Drive[1].ATAPI_diskchangeTimeout) //Pending execute transfer?
		{
			ATA[0].Drive[1].ATAPI_diskchangeTimeout -= timepassed; //Time until finished!
			if (ATA[0].Drive[1].ATAPI_diskchangeTimeout<=0.0) //Finished?
			{
				ATA_channel = 0;
				ATA_slave = 1;
				tickATADiskChange(0,1); //Tick the disk changing mechanism!
			}
		}

		if (ATA[1].Drive[0].ATAPI_diskchangeTimeout) //Pending execute transfer?
		{
			ATA[1].Drive[0].ATAPI_diskchangeTimeout -= timepassed; //Time until finished!
			if (ATA[1].Drive[0].ATAPI_diskchangeTimeout<=0.0) //Finished?
			{
				ATA_channel = 1;
				ATA_slave = 0;
				tickATADiskChange(1,0); //Tick the disk changing mechanism!
			}
		}

		if (ATA[1].Drive[1].ATAPI_diskchangeTimeout) //Pending execute transfer?
		{
			ATA[1].Drive[1].ATAPI_diskchangeTimeout -= timepassed; //Time until finished!
			if (ATA[1].Drive[1].ATAPI_diskchangeTimeout<=0.0) //Finished?
			{
				ATA_channel = 1;
				ATA_slave = 1;
				tickATADiskChange(1,1); //Tick the disk changing mechanism!
			}
		}

		//Handle ATAPI execute transfer delay!
		if (ATA[0].Drive[0].ATAPI_PendingExecuteTransfer) //Pending execute transfer?
		{
			ATA[0].Drive[0].ATAPI_PendingExecuteTransfer -= timepassed; //Time until finished!
			if (ATA[0].Drive[0].ATAPI_PendingExecuteTransfer<=0.0) //Finished?
			{
				ATA[0].Drive[0].ATAPI_PendingExecuteTransfer = 0.0; //Timer finished!
				if ((ATA[0].Drive[0].ATAPI_bytecountleft_IRQ==1) || (ATA[0].Drive[0].ATAPI_bytecountleft_IRQ==3)) //Anything left to give an IRQ for? Bytecountleft: >0=Data left to transfer(raise IRQ with reason), 0=Finishing interrupt, entering result phase, IRQ=3:no IRQ!
				{
					ATA_channel = 0;
					ATA_slave = 0;
					ATAPI_generateInterruptReason(0,0); //Generate our reason!
					if (ATA[0].Drive[0].ATAPI_bytecountleft_IRQ != 3) //Raise IRQ?
					{
						ATA_IRQ(0, 0, (DOUBLE)0, 0); //Raise an IRQ!
					}
				}
			}
		}

		if (ATA[0].Drive[1].ATAPI_PendingExecuteTransfer) //Pending execute transfer?
		{
			ATA[0].Drive[1].ATAPI_PendingExecuteTransfer -= timepassed; //Time until finished!
			if (ATA[0].Drive[1].ATAPI_PendingExecuteTransfer<=0.0) //Finished?
			{
				ATA[0].Drive[1].ATAPI_PendingExecuteTransfer = 0.0; //Timer finished!
				if ((ATA[0].Drive[1].ATAPI_bytecountleft_IRQ == 1) || (ATA[0].Drive[1].ATAPI_bytecountleft_IRQ == 3)) //Anything left to give an IRQ for? Bytecountleft: >0=Data left to transfer(raise IRQ with reason), 0=Finishing interrupt, entering result phase, IRQ=3:no IRQ!
				{
					ATA_channel = 0;
					ATA_slave = 1;
					ATAPI_generateInterruptReason(0,1); //Generate our reason!
					if (ATA[0].Drive[1].ATAPI_bytecountleft_IRQ != 3) //Raise IRQ?
					{
						ATA_IRQ(0, 1, (DOUBLE)0, 0); //Raise an IRQ!
					}
				}
			}
		}

		if (ATA[1].Drive[0].ATAPI_PendingExecuteTransfer) //Pending execute transfer?
		{
			ATA[1].Drive[0].ATAPI_PendingExecuteTransfer -= timepassed; //Time until finished!
			if (ATA[1].Drive[0].ATAPI_PendingExecuteTransfer<=0.0) //Finished?
			{
				ATA[1].Drive[0].ATAPI_PendingExecuteTransfer = 0.0; //Timer finished!
				if ((ATA[1].Drive[0].ATAPI_bytecountleft_IRQ == 1) || (ATA[1].Drive[0].ATAPI_bytecountleft_IRQ == 3)) //Anything left to give an IRQ for? Bytecountleft: >0=Data left to transfer(raise IRQ with reason), 0=Finishing interrupt, entering result phase, IRQ=3:no IRQ!
				{
					ATA_channel = 1;
					ATA_slave = 0;
					ATAPI_generateInterruptReason(1, 0); //Generate our reason!
					if (ATA[1].Drive[0].ATAPI_bytecountleft_IRQ != 3) //Raise IRQ?
					{
						ATA_IRQ(1, 0, (DOUBLE)0, 0); //Raise an IRQ!
					}
				}
			}
		}

		if (ATA[1].Drive[1].ATAPI_PendingExecuteTransfer) //Pending execute transfer?
		{
			ATA[1].Drive[1].ATAPI_PendingExecuteTransfer -= timepassed; //Time until finished!
			if (ATA[1].Drive[1].ATAPI_PendingExecuteTransfer<=0.0) //Finished?
			{
				ATA[1].Drive[1].ATAPI_PendingExecuteTransfer = 0.0; //Timer finished!
				if ((ATA[1].Drive[1].ATAPI_bytecountleft_IRQ == 1) || (ATA[1].Drive[1].ATAPI_bytecountleft_IRQ == 3)) //Anything left to give an IRQ for? Bytecountleft: >0=Data left to transfer(raise IRQ with reason), 0=Finishing interrupt, entering result phase, IRQ=3:no IRQ!
				{
					ATA_channel = 1;
					ATA_slave = 1;
					ATAPI_generateInterruptReason(1,1); //Generate our reason!
					if (ATA[1].Drive[1].ATAPI_bytecountleft_IRQ != 3) //Raise IRQ?
					{
						ATA_IRQ(1, 1, (DOUBLE)0, 0); //Raise an IRQ!
					}
				}
			}
		}
	}
}

OPTINLINE byte controller_enabled()
{
	if (activePCI_IDE->Command & 1) //Is the PCI controller enabled?
	{
		return 1; //Enabled!
	}
	return 0; //Disabled!
}

OPTINLINE uint_32 getPORTaddress(byte channel)
{
	switch (channel)
	{
	case 0: //First?
		return (((ATA[0].use_PCImode&1)==0)?0x1F0:((activePCI_IDE->BAR[0] > 3) ? (activePCI_IDE->BAR[0]&~3) : 0x1F0)); //Give the BAR!
		break;
	case 1: //Second?
		return (((ATA[1].use_PCImode&1)==0)?0x170:((activePCI_IDE->BAR[2-(ATA[1].use_PCImode&2)] > 3) ? (activePCI_IDE->BAR[2-(ATA[1].use_PCImode&2)]&~3) : 0x170)); //Give the BAR!
		break;
	default:
		return ~0; //Error!
	}
}

OPTINLINE uint_32 getControlPORTaddress(byte channel)
{
	switch (channel)
	{
	case 0: //First?
		return (((ATA[0].use_PCImode&1)==0)?0x3F4:((activePCI_IDE->BAR[1] > 3) ? (activePCI_IDE->BAR[1]&~3) : 0x3F4)); //Give the BAR!
		break;
	case 1: //Second?
		return (((ATA[1].use_PCImode&1)==0)?0x374:((activePCI_IDE->BAR[3-(ATA[1].use_PCImode&2)] > 3) ? (activePCI_IDE->BAR[3-(ATA[1].use_PCImode&2)]&~3) : 0x374)); //Give the BAR!
		break;
	default:
		return ~0; //Error!
	}
}

void ATA_updateCapacity(byte channel, byte slave)
{
	uint_32 sectors;
	sectors = ATA[channel].Drive[slave].driveparams[54]; //Current cylinders!
	sectors *= ATA[channel].Drive[slave].driveparams[55]; //Current heads!
	sectors *= ATA[channel].Drive[slave].driveparams[56]; //Current sectors per track!
	ATA[channel].Drive[slave].driveparams[57] = (word)(sectors&0xFFFF);
	sectors >>= 16;
	ATA[channel].Drive[slave].driveparams[58] = (word)(sectors&0xFFFF);
}

void HDD_classicGeometry(uint_64 disk_size, word *cylinders, word *heads, word *SPT)
{
	uint_32 tempcylinders=0;
	*SPT = (word)((disk_size>=63)?63:disk_size); //How many sectors use for each track? No more than 63!
	*heads = (word)(((disk_size/ *SPT)>=16)?16:((disk_size/ *SPT)?(disk_size/ *SPT):1)); //1-16 heads!
	tempcylinders = (uint_32)(disk_size / (63*16)); //How many cylinders!
	*cylinders = (tempcylinders>=0x3FFF)?0x3FFF:(tempcylinders?tempcylinders:1); //Give the maximum amount of cylinders allowed!
}

void HDD_detectOptimalGeometry(uint_64 disk_size, word *cylinders, word *heads, word *SPT)
{
	//Plain CHS geometry detection!
	//Optimal size detection?
	word C, H, S; //To detect the size!
	uint_64 CHSsize; //Found size!
	word optimalC, optimalH, optimalS; //Optimal found size!
	uint_64 optimalsize;
	optimalsize = 1; //Optimal size found!
	optimalC = 1; //Init!
	optimalH = 1; //Init!
	optimalS = 1; //Init!

	//Basic requirement rule initialization!
	byte limitcylinders;
	limitcylinders = ((disk_size>1032192) && (disk_size<=16514064))?1:((disk_size<=1032192)?3:0); //Limit available using table?
	if (disk_size>15481935) limitcylinders = 2; //Force 0x3FFF!

	word limitheads;
	limitheads = (disk_size<=8257536)?16:((disk_size>16514064)?15:0); //Head limit, if any!

	word limitSPT;
	limitSPT = (disk_size>1032192)?63:0; //Limit SPT?

	C=0xFFFF; //Init!
	do //Process all cylinder combinations!
	{
		H=16; //Init!
		do
		{
			S=63; //Init!
			do
			{
				CHSsize = (C*H*S); //Found size!
				if (unlikely((CHSsize>optimalsize) && (CHSsize<=disk_size))) //New optimal found?
				{
					//Check additional requirements? Rules based on http://www.t13.org/Documents/UploadedDocuments/project/d1321r3-ATA-ATAPI-5.pdf appendix C!
					if (unlikely(limitcylinders)) //Cylinder limited rule?
					{
						switch (limitcylinders) //What method?
						{
							case 1: //Using table?
								if (H<5) //Maximum FFFFh?
								{
									if (unlikely(C>0xFFFF)) goto ignoreDetection; //Don't allow invalid combinations!
								}
								else if (H<9) //Maximum 7FFFh?
								{
									if (unlikely(C>0x7FFF)) goto ignoreDetection; //Don't allow invalid combinations!
								}
								else //Maximum 3FFFh?
								{
									if (unlikely(C>0x3FFF)) goto ignoreDetection; //Don't allow invalid combinations!
								}
								break;
							case 2: //Force 0x3FFF?
								if (unlikely(C!=0x3FFF)) goto ignoreDetection; //Don't allow invalid combinations!
								break;
							case 3: //Force 1024 cylinder limit?
								if (unlikely(C>0x400)) goto ignoreDetection; //Don't allow invalid combinations!
								break;
							default: //Unknown?
								break;
						}
					}
					if (unlikely(limitheads && (H!=limitheads))) goto ignoreDetection; //Don't allow invalid combinations!
					if (unlikely(limitSPT && (S!=limitSPT))) goto ignoreDetection; //Don't allow invalid combinations!

					//Accept the new found size!
					optimalC = C; //Optimal C!
					optimalH = H; //Optimal H!
					optimalS = S; //Optimal S!
					optimalsize = CHSsize; //New optimal size!
				}
				ignoreDetection:
				--S;
			} while (S);
			--H;
		} while (H);
		--C;
	} while (C);
	*cylinders = optimalC; //Optimally found cylinders!
	*heads = optimalH; //Optimally found heads!
	*SPT = optimalS; //Optimally found sectors!
}

void HDD_detectGeometry(int disk, int_64 disk_size,word *cylinders, word *heads, word *SPT)
{
	if (io_getgeometry(disk,cylinders,heads,SPT)) //Gotten?
	{
		return; //Success!
	}
	HDD_classicGeometry(disk_size,cylinders,heads,SPT); //Fallback to classic by default!
}

word get_SPT(int disk, int_64 disk_size)
{
	word result,dummy1,dummy2;
	HDD_detectGeometry(disk,disk_size,&dummy1,&dummy2,&result);
	return result; //Give the result!
}

word get_heads(int disk, int_64 disk_size)
{
	word result,dummy1,dummy2;
	HDD_detectGeometry(disk,disk_size,&dummy1,&result,&dummy2);
	return result; //Give the result!
}

word get_cylinders(int disk, int_64 disk_size)
{
	word result,dummy1,dummy2;
	HDD_detectGeometry(disk,disk_size,&result,&dummy1,&dummy2);
	return result; //Give the result!	
}

//LBA address support with CHS/LBA input/output!
OPTINLINE void ATA_increasesector(byte channel) //Increase the current sector to the next sector!
{
	++ATA[channel].Drive[ATA_activeDrive(channel)].current_LBA_address; //Increase the current sector!
}

OPTINLINE void ATAPI_increasesector(byte channel, byte drive) //Increase the current sector to the next sector!
{
	++ATA[channel].Drive[drive].ATAPI_LBA; //Increase the current sector!
	ATA[channel].Drive[drive].ATAPI_lastLBA = ATA[channel].Drive[drive].ATAPI_LBA; //Update the last LBA!
}

void ATA_readLBACHS(byte channel)
{
	if (ATA_DRIVEHEAD_LBAMODER(channel,ATA_activeDrive(channel))) //Are we in LBA mode?
	{
		ATA[channel].Drive[ATA_activeDrive(channel)].current_LBA_address = (ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.LBA & 0xFFFFFFF); //The LBA address!
	}
	else //Normal CHS address?
	{
		ATA[channel].Drive[ATA_activeDrive(channel)].current_LBA_address = ATA_CHS2LBA(channel,ATA_activeDrive(channel),
			((ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.cylinderhigh << 8) | (ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.cylinderlow)),
			ATA_DRIVEHEAD_HEADR(channel,ATA_activeDrive(channel)),
			ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.sectornumber); //The LBA address based on the CHS address!

	}
}

void ATA_writeLBACHS(byte channel) //Update the current sector!
{
	word cylinder;
	byte head, sector;
	if (ATA_DRIVEHEAD_LBAMODER(channel,ATA_activeDrive(channel))) //LBA mode?
	{
		ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.LBA &= ~0xFFFFFFF; //Clear the LBA part!
		ATA[channel].Drive[ATA_activeDrive(channel)].current_LBA_address &= 0xFFFFFFF; //Truncate the address to it's size!
		ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.LBA |= ATA[channel].Drive[ATA_activeDrive(channel)].current_LBA_address; //Set the LBA part only!
	}
	else
	{
		if (ATA_LBA2CHS(channel, ATA_activeDrive(channel), ATA[channel].Drive[ATA_activeDrive(channel)].current_LBA_address, &cylinder, &head, &sector)) //Convert the current LBA address into a CHS value!
		{
			ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.cylinderhigh = ((cylinder >> 8) & 0xFF); //Cylinder high!
			ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.cylinderlow = (cylinder & 0xFF); //Cylinder low!
			ATA_DRIVEHEAD_HEADW(channel, ATA_activeDrive(channel), head); //Head!
			ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.sectornumber = sector; //Sector number!
		}
	}
}

void strcpy_padded(byte *buffer, byte sizeinbytes, byte *s)
{
	byte counter, data;
	word length;
	length = (word)safestrlen((char *)s,sizeinbytes); //Check the length for the copy!
	for (counter=0;counter<sizeinbytes;++counter) //Step words!
	{
		data = 0x20; //Initialize to unused!
		if (length>counter) //Byte available?
		{
			data = s[counter]; //High byte as low byte!
		}
		buffer[counter] = data; //Set the byte information!
	}
}

OPTINLINE byte ATA_readsector(byte channel, byte command) //Read the current sector set up!
{
	byte multiple = 1; //Multiple to read!
	word counter;
	word partialtransfer;
	uint_32 disk_size = ((ATA[channel].Drive[ATA_activeDrive(channel)].driveparams[61] << 16) | ATA[channel].Drive[ATA_activeDrive(channel)].driveparams[60]); //The size of the disk in sectors!
	if (ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus == 1) //We're reading already?
	{
		if (ATA[channel].Drive[ATA_activeDrive(channel)].readmultipleerror && ATA[channel].Drive[ATA_activeDrive(channel)].multiplesectors) //Error during the previous part of the read multiple command?)
		{
			ATA[channel].Drive[ATA_activeDrive(channel)].datasize -= ATA[channel].Drive[ATA_activeDrive(channel)].readmultiple_partialtransfer; //How much was actually transferred!
			goto handleReadSectorRangeError;
		}
		if (!(ATA[channel].Drive[ATA_activeDrive(channel)].datasize-=ATA[channel].Drive[ATA_activeDrive(channel)].multipletransferred)) //Finished?
		{
			ATA_writeLBACHS(channel); //Update the current sector!
			ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus = 0; //We're back in command mode!
			EMU_setDiskBusy(ATA_Drives[channel][ATA_activeDrive(channel)], 0); //We're not reading anymore!
			ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.sectorcount = 0; //How many sectors are left is updated!
			return 0; //We're finished!
		}
		else
		{
			ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.sectorcount = (ATA[channel].Drive[ATA_activeDrive(channel)].datasize&0xFF); //How many sectors are left is updated!
		}
	}
	else //New read command?
	{
		ATA[channel].Drive[ATA_activeDrive(channel)].readmultipleerror = 0; //Don't allow errors to occur on read multiple yet!
		ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.sectorcount = (ATA[channel].Drive[ATA_activeDrive(channel)].datasize&0xFF); //How many sectors are left is initialized!
	}
	if ((ATA[channel].Drive[ATA_activeDrive(channel)].current_LBA_address > disk_size) && //Past the end of the disk?
		(
			(ATA[channel].Drive[ATA_activeDrive(channel)].readmultipleerror && ATA[channel].Drive[ATA_activeDrive(channel)].multiplesectors) //During the previous part of the read multiple command?
			|| (ATA[channel].Drive[ATA_activeDrive(channel)].multiplesectors==0) //Not in multiple mode?
		)
		)
	{
		handleReadSectorRangeError:
#ifdef ATA_LOG
		dolog("ATA", "Read Sector out of range:%u,%u=%08X/%08X!", channel, ATA_activeDrive(channel), ATA[channel].Drive[ATA_activeDrive(channel)].current_LBA_address, disk_size);
#endif
		ATA_ERRORREGISTER_IDMARKNOTFOUNDW(channel,ATA_activeDrive(channel),1); //Not found!
		ATA_STATUSREGISTER_ERRORW(channel,ATA_activeDrive(channel),1); //Set error bit!
		ATA_writeLBACHS(channel); //Update the current sector!
		ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus = 0xFF; //Error!
		EMU_setDiskBusy(ATA_Drives[channel][ATA_activeDrive(channel)], 0); //We're not reading anymore!
		return 1; //Stop! Error interrupt!
	}

	if (ATA[channel].Drive[ATA_activeDrive(channel)].multiplesectors) //Enabled multiple mode?
	{
		multiple = ATA[channel].Drive[ATA_activeDrive(channel)].multiplesectors; //Multiple sectors instead!
	}

	if (multiple>ATA[channel].Drive[ATA_activeDrive(channel)].datasize) //More than requested left?
	{
		multiple = ATA[channel].Drive[ATA_activeDrive(channel)].datasize; //Only take what's requested!
	}
	ATA[channel].Drive[ATA_activeDrive(channel)].multipletransferred = multiple; //How many have we transferred?

	//Safety: verify LBA past end of disk, if it's happening!
	for (counter = 0; counter < multiple; ++counter) //Check all that we try to read!
	{
		if ((ATA[channel].Drive[ATA_activeDrive(channel)].current_LBA_address + counter) > disk_size) //Past the end of the disk?
		{
			break; //Give us an indication of how far we can read!
		}
	}

	partialtransfer = counter; //How much has been transferred!
	EMU_setDiskBusy(ATA_Drives[channel][ATA_activeDrive(channel)], 1); //We're reading!
	memset(&ATA[channel].Drive[ATA_activeDrive(channel)].data,0, (multiple<<9)); //Clear the buffer for any errors we take!
	if ((readdata(ATA_Drives[channel][ATA_activeDrive(channel)], &ATA[channel].Drive[ATA_activeDrive(channel)].data, ((uint_64)ATA[channel].Drive[ATA_activeDrive(channel)].current_LBA_address << 9), (partialtransfer<<9))) || ((partialtransfer==0) && multiple)) //Read the data from disk as far as we can?
	{
		for (counter=0;counter<partialtransfer;++counter) //Increase sector count as much as required!
		{
			ATA_increasesector(channel); //Increase the current sector!
		}

		ATA[channel].Drive[ATA_activeDrive(channel)].datapos = 0; //Initialise our data position!
		ATA[channel].Drive[ATA_activeDrive(channel)].datablock = 0x200*multiple; //We're refreshing after this many bytes!
		ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus = 1; //Transferring data IN!
		ATA[channel].Drive[ATA_activeDrive(channel)].command = command; //Set the command to use when reading!

		if (partialtransfer != multiple) //Not all was transferred correctly?
		{
			ATA[channel].Drive[ATA_activeDrive(channel)].readmultiple_partialtransfer = partialtransfer; //How much was actually transferred!
			ATA[channel].Drive[ATA_activeDrive(channel)].readmultipleerror = 1; //Don't allow errors to occur on read multiple yet! Raise the error at the next block!
		}

		ATA[channel].Drive[ATA_activeDrive(channel)].ERRORREGISTER = 0x00; //Not needed by the spec, but required by Windows apparently, according to Qemu!
		return 2; //Process the block! Don't raise an interrupt when continuing to transfer(which automatically happens due to the larger block size applied)!
	}
	else //Error reading?
	{
		ATA_ERRORREGISTER_IDMARKNOTFOUNDW(channel,ATA_activeDrive(channel),1); //Not found!
		ATA_STATUSREGISTER_ERRORW(channel,ATA_activeDrive(channel),1); //Set error bit!
		ATA_writeLBACHS(channel); //Update the current sector!
		ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus = 0xFF; //Error!
		EMU_setDiskBusy(ATA_Drives[channel][ATA_activeDrive(channel)], 0); //We're doing nothing!
		return 0; //Stop!
	}
	return 1; //We're finished!
}

OPTINLINE byte ATA_writesector(byte channel, byte command)
{
	byte multiple = 1; //Multiple to read!
	uint_32 disk_size = ((ATA[channel].Drive[ATA_activeDrive(channel)].driveparams[61] << 16) | ATA[channel].Drive[ATA_activeDrive(channel)].driveparams[60]); //The size of the disk in sectors!
	if (ATA[channel].Drive[ATA_activeDrive(channel)].current_LBA_address > disk_size) //Past the end of the disk?
	{
		writeoutofbounds:
#ifdef ATA_LOG
		dolog("ATA", "Write Sector out of range:%u,%u=%08X/%08X!",channel,ATA_activeDrive(channel), ATA[channel].Drive[ATA_activeDrive(channel)].current_LBA_address,disk_size);
#endif
		ATA_ERRORREGISTER_IDMARKNOTFOUNDW(channel,ATA_activeDrive(channel),1); //Not found!
		ATA_STATUSREGISTER_ERRORW(channel,ATA_activeDrive(channel),1); //Set error bit!
		ATA_writeLBACHS(channel); //Update the current sector!
		ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.sectorcount = (ATA[channel].Drive[ATA_activeDrive(channel)].datasize&0xFF); //How many sectors are left is updated!
		ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus = 0xFF; //Error!
		EMU_setDiskBusy(ATA_Drives[channel][ATA_activeDrive(channel)], 0); //We're doing nothing!
		return 1; //We're finished!
	}

#ifdef ATA_LOG
	dolog("ATA", "Writing sector #%u!", ATA[channel].Drive[ATA_activeDrive(channel)].current_LBA_address); //Log the sector we're writing to!
#endif

	byte numwritten;
	byte writeresult;
	byte *p;
	writeresult = 1; //Written correctly?
	p = &ATA[channel].Drive[ATA_activeDrive(channel)].data[0]; //What to start writing!
	for (numwritten = 0; ((numwritten < ATA[channel].Drive[ATA_activeDrive(channel)].multipletransferred) && writeresult); ++numwritten) //Write the sectors to disk!
	{
		if (ATA[channel].Drive[ATA_activeDrive(channel)].current_LBA_address > disk_size) //Past the end of the disk?
		{
			goto writeoutofbounds; //We're out of bounds!
		}
		writeresult = writedata(ATA_Drives[channel][ATA_activeDrive(channel)], p, ((uint_64)ATA[channel].Drive[ATA_activeDrive(channel)].current_LBA_address << 9), 0x200); //Try to write the sector!
		p += 0x200; //How much have we written!
		if (writeresult) //Written without error?
		{
			ATA_increasesector(channel); //Increase the current sector!
		}
	}

	if (writeresult) //Written all the data to the disk?
	{
		if (!(ATA[channel].Drive[ATA_activeDrive(channel)].datasize-=ATA[channel].Drive[ATA_activeDrive(channel)].multipletransferred)) //Finished?
		{
			ATA_writeLBACHS(channel); //Update the current sector!
			ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus = 0; //We're back in command mode!
#ifdef ATA_LOG
			dolog("ATA", "All sectors to be written written! Ready.");
#endif
			ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.sectorcount = (ATA[channel].Drive[ATA_activeDrive(channel)].datasize&0xFF); //How many sectors are left is updated!
			EMU_setDiskBusy(ATA_Drives[channel][ATA_activeDrive(channel)], 0); //We're doing nothing!
			return 1; //We're finished!
		}
		else //Busy transferring?
		{
			ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.sectorcount = (ATA[channel].Drive[ATA_activeDrive(channel)].datasize&0xFF); //How many sectors are left is updated!
		}

#ifdef ATA_LOG
		dolog("ATA", "Process next sector...");
#endif
		//Process next sector!
		if (ATA[channel].Drive[ATA_activeDrive(channel)].multiplesectors) //Enabled multiple mode?
		{
			multiple = ATA[channel].Drive[ATA_activeDrive(channel)].multiplesectors; //Multiple sectors instead!
		}
		if (multiple>ATA[channel].Drive[ATA_activeDrive(channel)].datasize) //More than requested left?
		{
			multiple = ATA[channel].Drive[ATA_activeDrive(channel)].datasize; //Only take what's requested!
		}
		ATA[channel].Drive[ATA_activeDrive(channel)].multipletransferred = multiple; //How much do we want transferred?
		ATA[channel].Drive[ATA_activeDrive(channel)].command = command; //Set the command to use when writing!
		ATA[channel].Drive[ATA_activeDrive(channel)].datapos = 0; //Initialise our data position!
		ATA[channel].Drive[ATA_activeDrive(channel)].datablock = 0x200*multiple; //We're refreshing after this many bytes!
		ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus = 2; //Transferring data OUT!
		return 1; //Process the block! Don't raise an interrupt when continuing to transfer!
	}
	else //Write failed?
	{
#ifdef ATA_LOG
		dolog("ATA", "Write failed!"); //Log the sector we're writing to!
#endif
		if (drivereadonly(ATA_Drives[channel][ATA_activeDrive(channel)])) //R/O drive?
		{
#ifdef ATA_LOG
			dolog("ATA", "Because the drive is readonly!"); //Log the sector we're writing to!
#endif
			ATA_STATUSREGISTER_DRIVEWRITEFAULTW(channel,ATA_activeDrive(channel),1); //Write fault!
		}
		else
		{
#ifdef ATA_LOG
			dolog("ATA", "Because there was an error with the mounted disk image itself!"); //Log the sector we're writing to!
#endif
			ATA_ERRORREGISTER_UNCORRECTABLEDATAW(channel,ATA_activeDrive(channel),1); //Not found!
		}
		ATA_STATUSREGISTER_ERRORW(channel,ATA_activeDrive(channel),1); //Set error bit!
		ATA_writeLBACHS(channel); //Update the current sector!
		ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.sectorcount = (ATA[channel].Drive[ATA_activeDrive(channel)].datasize&0xFF); //How many sectors are left is updated!
		ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus = 0xFF; //Error!
		EMU_setDiskBusy(ATA_Drives[channel][ATA_activeDrive(channel)], 0); //We're doing nothing!
	}
	return 1; //Finished!
}

OPTINLINE void ATAPI_giveresultsize(byte channel, byte drive, uint_64 size, byte raiseIRQ) //Store the result size to use in the Task file
{
	//Apply the maximum size to transfer, saving the full packet size to count down from!

	if (size) //Is something left to be transferred? We're not a finished transfer(size=0)?
	{
		size = MIN(size,ATA[channel].Drive[drive].ATAPI_bytecount); //Limit the size of a ATAPI-block to transfer in one go!
		if (size == 0x10000) //64K can't be expressed?
		{
			size = 0xFFFE; //Maximum size that can be expressed!
		}
		ATA[channel].Drive[drive].ATAPI_bytecountleft = (uint_32)size; //How much is left to transfer?
		ATA[channel].Drive[drive].ATAPI_bytecountleft_IRQ = raiseIRQ; //Are we to raise an IRQ when starting a new data transfer?
		ATA[channel].Drive[drive].ATAPI_PendingExecuteTransfer = ATAPI_PENDINGEXECUTETRANSFER_DATATIMING; //Wait 20us before giving the new data that's to be transferred!

		if (raiseIRQ != 3) //Without IRQ being raised, we're not giving the size in the registers(leave it alone for the execution phase to start!)!
		{
			ATA[channel].Drive[drive].PARAMETERS.cylinderlow = (size & 0xFF); //Low byte of the result size!
			ATA[channel].Drive[drive].PARAMETERS.cylinderhigh = ((size >> 8) & 0xFF); //High byte of the result size!
		}
	}
	else //Finishing an transfer and entering result phase? This is what we do when nothing is to be transferred anymore!
	{
		ATA[channel].Drive[drive].ATAPI_bytecountleft = (uint_32)size; //How much is left to transfer?
		ATA[channel].Drive[drive].ATAPI_bytecountleft_IRQ = raiseIRQ?1:2; //Are we to raise an IRQ when starting a new data transfer?
		ATA[channel].Drive[drive].ATAPI_PendingExecuteTransfer = ATAPI_PENDINGEXECUTETRANSFER_RESULTTIMING; //Wait a bit before giving the new data that's to be transferred!		
		if (raiseIRQ) //Raise an IRQ after some time?
		{
			ATA[channel].Drive[drive].ATAPI_bytecount = 0; //We're special: indicating end of transfer is to be executed only by setting an invalid value!
		}
	}
}

OPTINLINE uint_32 ATAPI_getresultsize(byte channel, byte drive) //Retrieve the current result size from the Task file
{
	uint_32 result;
	result = ((ATA[channel].Drive[drive].PARAMETERS.cylinderhigh<<8)|ATA[channel].Drive[drive].PARAMETERS.cylinderlow); //Low byte of the result size!
	if (result==0)
	{
		result = 0x10000; //Maximum instead: 0 is illegal!
	}
	return result; //Give the result!
}

byte decreasebuffer[2352];

byte ATAPI_aborted=0;

OPTINLINE byte ATAPI_readsector(byte channel, byte drive) //Read the current sector set up!
{
	byte ascq;
	uint_32 skipPregap; //To skip pregap!
	byte spinresponse;
	byte abortreason, additionalsensecode;
	int_64 cueresult, cuepostgapresult, cuepostgappending, cuepostgapactive;
	byte datablock_ready = 0;
	byte M, S, F;
	char *cuedisk;
	byte *datadest = NULL; //Destination of our loaded data!
	byte cue_M, cue_S, cue_F, cue_startM, cue_startS, cue_startF, cue_endM, cue_endS, cue_endF, cue_track;
	byte cue_postgapM, cue_postgapS, cue_postgapF, cue_postgapstartM, cue_postgapstartS, cue_postgapstartF, cue_postgapendM, cue_postgapendS, cue_postgapendF;
	int_64 cue_trackskip, cue_trackskip2, cue_postgapskip;
	uint_32 reqLBA;
	uint_32 disk_size = ATA[channel].Drive[drive].ATAPI_disksize; //The size of the disk in sectors!
	ascq = 0; //Init!
	if (ATA[channel].Drive[drive].commandstatus == 1) //We're reading already?
	{
		if (!--ATA[channel].Drive[drive].datasize) //Finished?
		{
			finishedreadingATAPI: //No logical blocks shall be transferred?
			ATA_STATUSREGISTER_ERRORW(channel, drive, 0); //Error bit is reset when a new command is received, as defined in the documentation!
			ATA_STATUSREGISTER_DRIVESEEKCOMPLETEW(channel,drive,0); //Seek complete!
			ATA[channel].Drive[drive].commandstatus = 0; //We're back in command mode!
			EMU_setDiskBusy(ATA_Drives[channel][drive], 0| (ATA[channel].Drive[ATA_activeDrive(channel)].ATAPI_caddyejected << 1)); //We're not reading anymore!
			ATA[channel].Drive[drive].ATAPI_processingPACKET = 3; //We've finished transferring ATAPI data now!
			ATAPI_giveresultsize(channel,drive,0,1); //No result size!
			return 0; //We're finished!
		}
	}

	if (ATA[channel].Drive[drive].datasize == 0) goto finishedreadingATAPI; //No logical blocks shall be transferred?

	if ((spinresponse = ATAPI_common_spin_response(channel,drive,2,0))!=1)
	{
		if (spinresponse != 2) //Not simply waiting?
		{
			ATAPI_command_reportError(channel, drive); //Report the error!
			ATAPI_aborted = 1; //We're aborted!
		}
		//When waiting, simply wait!
		return 0; //We're finished or waiting!
	}

	if ((cuedisk = getCUEimage(ATA_Drives[channel][drive]))) //Is a CUE disk?
	{
		if (is_cueimage(cuedisk)) //Valid disk image?
		{
			skipPregap = 0; //Default: don't skip!

			//Determine the pregap to use!
			reqLBA = ATA[channel].Drive[drive].ATAPI_LBA; //What LBA are we calculating for?
			cuepostgappending = 0; //Default: no postgap pending!
			for (cue_track=1;cue_track<100;++cue_track) //Check all tracks!
			{
				CDROM_selecttrack(ATA_Drives[channel][drive],cue_track); //All tracks!
				CDROM_selectsubtrack(ATA_Drives[channel][drive],0); //All subtracks!
				if ((cueresult = cueimage_getgeometry(ATA_Drives[channel][drive], &cue_M, &cue_S, &cue_F, &cue_startM, &cue_startS, &cue_startF, &cue_endM, &cue_endS, &cue_endF,0)) != 0) //Geometry gotten?
				{
					cuepostgapresult = cueimage_getgeometry(ATA_Drives[channel][drive], &cue_postgapM, &cue_postgapS, &cue_postgapF, &cue_postgapstartM, &cue_postgapstartS, &cue_postgapstartF, &cue_postgapendM, &cue_postgapendS, &cue_postgapendF, 2); //Geometry gotten?
					if ((cue_trackskip = cueimage_readsector(ATA_Drives[channel][drive], cue_startM, cue_startS, cue_startF, NULL, 0))!=0) //Try to read as specified!
					{
						cue_postgapskip = cueimage_readsector(ATA_Drives[channel][drive], cue_postgapstartM, cue_postgapstartS, cue_postgapstartF, NULL, 0); //Try to read as specified!
						if (cue_trackskip<-2) //Skipping more?
						{
							skipPregap += (uint_32)(-(cue_trackskip+2)); //More pregap to skip!
						}
						skipPregap += (uint_32)cuepostgappending; //Apply pending postgap from the previous track as well!
						cuepostgapactive = cuepostgappending; //Save the active postgap as well!
						if (cuepostgapresult != 0) //Gotten postgap?
						{
							if (cue_postgapskip < -2) //Skipping more after this track?
							{
								cuepostgappending = -(cue_postgapskip + 2); //More postgap to skip for the next track!
							}
						}
						LBA2MSFbin(reqLBA+skipPregap, &M, &S, &F); //Generate a MSF address to use with CUE images!
						
						if ((cue_trackskip2 = cueimage_readsector(ATA_Drives[channel][drive], M, S, F, &ATA[channel].Drive[drive].data[0], 0))>=1) //Try to find out if we're here!
						{
							switch (cue_trackskip2)
							{
							case 1+MODE_MODE1DATA: //Mode 1 block?
							if (ATA[channel].Drive[drive].expectedReadDataType == 0) goto startCUEread; //Invalid type to read!
							case 1+MODE_MODEXA: //Mode XA block?
								if (ATA[channel].Drive[drive].expectedReadDataType == 0) goto startCUEread; //Invalid type to read!
							goto startCUEread; //Ready to process!
							break;
							case 1+MODE_AUDIO: //Audio block?
								if ((ATA[channel].Drive[drive].expectedReadDataType != 1) && (ATA[channel].Drive[drive].expectedReadDataType != 0)) goto startCUEread; //Invalid type to read!
								if (cue_trackskip <= -2) //Some pregap for this song? Include the pregap in the read?
								{
									skipPregap -= (uint_32)(cue_trackskip + 2); //Undo the pregap, as this is raw audio we're reading in this case!
								}
								if (cuepostgapactive <= -2) //Some postgap for this song? Include the postgap in the read?
								{
									skipPregap -= (uint_32)(cuepostgapactive + 2); //Undo the pregap, as this is raw audio we're reading in this case!
								}
								goto startCUEread; //Ready to process
							default: //Unknown/unsupported mode/OOR?
								continue; //Continue searching!
							}
						}
						//Not found yet? Continue searching!
					}
					//Failed checking the track skip? Ignore the track!
				}
			}
			
		startCUEread:

			//Now, start reading past the determined pregap!
			LBA2MSFbin(ATA[channel].Drive[drive].ATAPI_LBA+skipPregap, &M, &S, &F); //Generate a MSF address to use with CUE images!
			ATA[channel].Drive[drive].lasttrack = cue_track; //What track are we on!
			ATA[channel].Drive[drive].lastM = M; //Our last position!
			ATA[channel].Drive[drive].lastS = S; //Our last position!
			ATA[channel].Drive[drive].lastF = F; //Our last position!
			CDROM_selecttrack(ATA_Drives[channel][drive], 0); //All tracks!
			CDROM_selectsubtrack(ATA_Drives[channel][drive],1); //Subtrack 1 only!
			if ((cueresult = cueimage_readsector(ATA_Drives[channel][drive], M, S, F,&ATA[channel].Drive[drive].data[0], ATA[channel].Drive[drive].datablock))>=1) //Try to read as specified!
			{
				if (cueresult == -1) goto ATAPI_readSector_OOR; //Out of range?
				switch (cueresult)
				{
				case 1+MODE_MODE1DATA: //Mode 1 block?
					ATA[channel].Drive[drive].lastformat = 0x14; //Last format seen: data track!
					if ((ATA[channel].Drive[drive].expectedReadDataType != 2) && (ATA[channel].Drive[drive].expectedReadDataType != 0) && (ATA[channel].Drive[drive].expectedReadDataType != 0xFF)) break; //Invalid type to read!
					goto ready1;
				case 1+MODE_MODEXA: //Mode XA block?
					ATA[channel].Drive[drive].lastformat = 0x14; //Last format seen: data track!
					if (((ATA[channel].Drive[drive].expectedReadDataType != 4) && (ATA[channel].Drive[drive].expectedReadDataType != 5)) && (ATA[channel].Drive[drive].expectedReadDataType != 0) && (ATA[channel].Drive[drive].expectedReadDataType != 0xFF)) break; //Invalid type to read!
				ready1:
					datablock_ready = 1; //Read and ready to process!
					break;
				case 1+MODE_AUDIO: //Audio block?
					ATA[channel].Drive[drive].lastformat = 0x10; //Last format seen: audio track!
					if (ATA[channel].Drive[drive].expectedReadDataType==0xFF) break; //Invalid in read sector(n) mode!
					if ((ATA[channel].Drive[drive].expectedReadDataType != 1) && (ATA[channel].Drive[drive].expectedReadDataType != 0)) break; //Invalid type to read!
					datablock_ready = 1; //Read and ready to process!
				default: //Unknown/unsupported mode?
					if (cueresult < -2) //Pregap?
					{
						ATA[channel].Drive[drive].lastformat = 0x10; //Last format seen: audio track!
						if ((ATA[channel].Drive[drive].expectedReadDataType != 1) && (ATA[channel].Drive[drive].expectedReadDataType != 0)) //Not audio to read? don't handle!
							break; //Unknown data!
						//Otherwise, supported audio read from pregap/postgap!
						//Lasttrack is already done during the track scan!
						memset(&ATA[channel].Drive[drive].data[0], 0, ATA[channel].Drive[drive].datablock); //Empty block for pregap/postgap!
						datablock_ready = 1; //Read and ready to process!
					}
					break; //Unknown data!
				}
			}
			else
			{
				if (ATA[channel].Drive[drive].datablock == 2352) //Needs extensions from usual size?
				{
					datadest = &ATA[channel].Drive[drive].data[0x10]; //Start of our read sector!
					memset(&ATA[channel].Drive[drive].data, 0, 2352); //Clear any data we use!
					memset(&ATA[channel].Drive[drive].data[1], 0xff, 10);
					uint_32 raw_block = ATA[channel].Drive[drive].ATAPI_LBA + skipPregap;
					ATA[channel].Drive[drive].data[12] = (raw_block / 75) / 60;
					ATA[channel].Drive[drive].data[13] = (raw_block / 75) % 60;
					ATA[channel].Drive[drive].data[14] = (raw_block % 75);
					ATA[channel].Drive[drive].data[15] = 0x01;
					datadest = &ATA[channel].Drive[drive].data[0x10]; //Start of our read sector!
					if ((cueresult = cueimage_readsector(ATA_Drives[channel][drive], M, S, F, datadest, 0x800))!=0) //Try to read as specified!
					{
						if (cueresult == -1) goto ATAPI_readSector_OOR; //Out of range?
						switch (cueresult)
						{
						case 1 + MODE_MODE1DATA: //Mode 1 block?
							if ((ATA[channel].Drive[drive].expectedReadDataType != 2) && (ATA[channel].Drive[drive].expectedReadDataType != 0) && (ATA[channel].Drive[drive].expectedReadDataType != 0xFF)) break; //Invalid type to read!
							datablock_ready = 1; //Read and ready to process!
							break;
						case 1 + MODE_AUDIO: //Audio block?
							if ((ATA[channel].Drive[drive].expectedReadDataType != 1) && (ATA[channel].Drive[drive].expectedReadDataType != 0)) break; //Invalid type to read!
							break;
						case 1 + MODE_MODEXA: //Mode XA block?
							if (((ATA[channel].Drive[drive].expectedReadDataType != 4) && (ATA[channel].Drive[drive].expectedReadDataType != 5)) && (ATA[channel].Drive[drive].expectedReadDataType != 0)  && (ATA[channel].Drive[drive].expectedReadDataType != 0xFF)) break; //Invalid type to read!
							break;
						default: //Unknown/unsupported mode?
							if (cueresult < -2) //Pregap?
							{
								if ((ATA[channel].Drive[drive].expectedReadDataType != 1) && (ATA[channel].Drive[drive].expectedReadDataType != 0)) //Not audio to read? don't handle!
									break; //Unknown data!
								//Otherwise, supported audio read from pregap/postgap!
								memset(&ATA[channel].Drive[drive].data[0], 0, ATA[channel].Drive[drive].datablock); //Empty block for pregap/postgap!
								datablock_ready = 1; //Read and ready to process!
							}
							break; //Unknown data!
						}
					}
				}
				else if (ATA[channel].Drive[drive].datablock == 2048) //Needs stripping from usual size?
				{
					if ((cueresult = cueimage_readsector(ATA_Drives[channel][drive], M, S, F, &decreasebuffer, 2352))!=0) //Try to read as specified!
					{
						if (cueresult == -1) goto ATAPI_readSector_OOR; //Out of range?
						switch (cueresult)
						{
						case 1 + MODE_MODE1DATA: //Mode 1 block?
							if ((ATA[channel].Drive[drive].expectedReadDataType != 2) && (ATA[channel].Drive[drive].expectedReadDataType != 0) && (ATA[channel].Drive[drive].expectedReadDataType != 0xFF)) break; //Invalid type to read!
							goto ready2;
						case 1 + MODE_MODEXA: //Mode XA block?
							if (((ATA[channel].Drive[drive].expectedReadDataType != 4) && (ATA[channel].Drive[drive].expectedReadDataType != 5)) && (ATA[channel].Drive[drive].expectedReadDataType != 0) && (ATA[channel].Drive[drive].expectedReadDataType != 0xFF)) break; //Invalid type to read!
							ready2:
							memcpy(&ATA[channel].Drive[drive].data, &decreasebuffer[0x10], 0x800); //Take the sector data out of the larger buffer!
							datablock_ready = 1; //Read and ready to process!
							break;
						case 1 + MODE_AUDIO: //Audio block?
							if (ATA[channel].Drive[drive].expectedReadDataType==0xFF) break; //Invalid in read sector(n) mode!
							if ((ATA[channel].Drive[drive].expectedReadDataType != 1) && (ATA[channel].Drive[drive].expectedReadDataType != 0)) break; //Invalid type to read!
						default: //Unknown/unsupported mode?
							if (ATA[channel].Drive[drive].expectedReadDataType == 0xFF) break; //Invalid in read sector(n) mode!
							if (cueresult < -2) //Pregap?
							{
								if ((ATA[channel].Drive[drive].expectedReadDataType != 1) && (ATA[channel].Drive[drive].expectedReadDataType != 0)) //Not audio to read? don't handle!
									break; //Unknown data!
								//Otherwise, supported audio read from pregap/postgap!
								memset(&ATA[channel].Drive[drive].data[0], 0, ATA[channel].Drive[drive].datablock); //Empty block for pregap/postgap!
								datablock_ready = 1; //Read and ready to process!
							}
							break; //Unknown data!
						}
					}
				}
				if (datablock_ready==0) //Invalid?
				{
					//For all other sector types, the device shall set the ILI bit in the Request Sense Standard Data(for read sector(s) only) and return a ILLEGAL MODE FOR THIS TRACK error!
					//Fill the Request Sense standard data with ILLEGAL MODE FOR THIS TRACK!
					abortreason = SENSE_ILLEGAL_REQUEST; //Illegal request:
					additionalsensecode = ASC_ILLEGAL_MODE_FOR_THIS_TRACK_OR_INCOMPATIBLE_MEDIUM; //Illegal mode or incompatible medium!
					ascq = 0;

					ATA[channel].Drive[drive].ATAPI_processingPACKET = 3; //Result phase!
					ATA[channel].Drive[drive].commandstatus = 0xFF; //Move to error mode!
					ATAPI_giveresultsize(channel,drive,0,1); //No result size!
					ATA[channel].Drive[drive].ERRORREGISTER = /*4|*/(abortreason<<4); //Reset error register! This also contains a copy of the Sense Key!
					ATAPI_SENSEPACKET_SENSEKEYW(channel, drive,abortreason); //Reason of the error
					ATAPI_SENSEPACKET_RESERVED2W(channel, drive, 0); //Reserved field!
					ATAPI_SENSEPACKET_ADDITIONALSENSECODEW(channel, drive,additionalsensecode); //Extended reason code
					ATAPI_SENSEPACKET_ASCQW(channel, drive, ascq); //ASCQ code!
					if (ATA[channel].Drive[drive].expectedReadDataType==0xFF) //Set ILI bit for read sector(nn)?
					{
						ATAPI_SENSEPACKET_ILIW(channel, drive,1); //ILI bit set!
					}
					else
					{
						ATAPI_SENSEPACKET_ILIW(channel, drive,0); //ILI bit cleared!
					}
					ATAPI_SENSEPACKET_ERRORCODEW(channel, drive,0x70); //Default error code?
					ATAPI_SENSEPACKET_ADDITIONALSENSELENGTHW(channel, drive,10); //Additional Sense Length = 10?
					ATAPI_SENSEPACKET_INFORMATION0W(channel, drive,0); //No info!
					ATAPI_SENSEPACKET_INFORMATION1W(channel, drive,0); //No info!
					ATAPI_SENSEPACKET_INFORMATION2W(channel, drive,0); //No info!
					ATAPI_SENSEPACKET_INFORMATION3W(channel, drive,0); //No info!
					ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION0W(channel, drive,0); //No command specific information?
					ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION1W(channel, drive,0); //No command specific information?
					ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION2W(channel, drive,0); //No command specific information?
					ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION3W(channel, drive,0); //No command specific information?
					ATAPI_SENSEPACKET_VALIDW(channel, drive,1); //We're valid!
					ATAPI_SENSEPACKET_CD(channel, drive, 0); //Error in the packet parameters!
					ATA[channel].Drive[drive].STATUSREGISTER = 0x40; //Clear status!
					ATA_STATUSREGISTER_DRIVEREADYW(channel, drive,1); //Ready!
					ATA_STATUSREGISTER_ERRORW(channel, drive,1); //Ready!
					ATAPI_aborted = 1; //Aborted!
					goto ATAPI_erroroutread; //Error out!
				}
			}
		}
	}

	if ((ATA[channel].Drive[drive].ATAPI_LBA > disk_size) && (datablock_ready==0)) //Past the end of the disk?
	{
		ATAPI_readSector_OOR:
		//For all other sector types, the device shall set the ILI bit in the Request Sense Standard Data(for read sector(s) only) and return a ILLEGAL MODE FOR THIS TRACK error!
		//Fill the Request Sense standard data with ILLEGAL MODE FOR THIS TRACK!
		abortreason = SENSE_ILLEGAL_REQUEST; //Illegal request:
		additionalsensecode = ASC_LOGICAL_BLOCK_OOR; //Illegal mode or incompatible medium!
		ascq = 0;
#ifdef ATA_LOG
		dolog("ATA", "Read Sector out of range:%u,%u=%08X/%08X!", channel, drive, ATA[channel].Drive[drive].ATAPI_LBA, disk_size);
#endif

		ATA[channel].Drive[drive].ATAPI_processingPACKET = 3; //Result phase!
		ATA[channel].Drive[drive].commandstatus = 0xFF; //Move to error mode!
		ATAPI_giveresultsize(channel,drive, 0, 1); //No result size!
		ATA[channel].Drive[drive].ERRORREGISTER = /*4 |*/ (abortreason << 4); //Reset error register! This also contains a copy of the Sense Key!
		ATAPI_SENSEPACKET_SENSEKEYW(channel, drive, abortreason); //Reason of the error
		ATAPI_SENSEPACKET_RESERVED2W(channel, drive, 0); //Reserved field!
		ATAPI_SENSEPACKET_ADDITIONALSENSECODEW(channel, drive, additionalsensecode); //Extended reason code
		ATAPI_SENSEPACKET_ASCQW(channel, drive, ascq); //ASCQ code!
		if (ATA[channel].Drive[drive].expectedReadDataType == 0xFF) //Set ILI bit for read sector(nn)?
		{
			ATAPI_SENSEPACKET_ILIW(channel, drive, 1); //ILI bit set!
		}
		else
		{
			ATAPI_SENSEPACKET_ILIW(channel, drive, 0); //ILI bit cleared!
		}
		ATAPI_SENSEPACKET_ERRORCODEW(channel, drive, 0x70); //Default error code?
		ATAPI_SENSEPACKET_ADDITIONALSENSELENGTHW(channel, drive, 10); //Additional Sense Length = 10?
		ATAPI_SENSEPACKET_INFORMATION0W(channel, drive, 0); //No info!
		ATAPI_SENSEPACKET_INFORMATION1W(channel, drive, 0); //No info!
		ATAPI_SENSEPACKET_INFORMATION2W(channel, drive, 0); //No info!
		ATAPI_SENSEPACKET_INFORMATION3W(channel, drive, 0); //No info!
		ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION0W(channel, drive, 0); //No command specific information?
		ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION1W(channel, drive, 0); //No command specific information?
		ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION2W(channel, drive, 0); //No command specific information?
		ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION3W(channel, drive, 0); //No command specific information?
		ATAPI_SENSEPACKET_VALIDW(channel, drive, 1); //We're valid!
		ATAPI_SENSEPACKET_CD(channel, drive, 0); //Error in the packet parameters!
		ATA[channel].Drive[drive].STATUSREGISTER = 0x40; //Clear status!
		ATA_STATUSREGISTER_DRIVEREADYW(channel, drive, 1); //Ready!
		ATA_STATUSREGISTER_ERRORW(channel, drive, 1); //Ready!
		ATAPI_aborted = 1; //Aborted!
		goto ATAPI_erroroutread; //Error out!
	}

	if ((ATA[channel].Drive[drive].datablock==2352) && (datablock_ready==0)) //Raw CD-ROM data requested? Add the header, based on Bochs cdrom.cc!
	{
		memset(&ATA[channel].Drive[drive].data, 0, 2352); //Clear any data we use!
		memset(&ATA[channel].Drive[drive].data[1], 0xff, 10);
		uint_32 raw_block = ATA[channel].Drive[drive].ATAPI_LBA + 150;
		ATA[channel].Drive[drive].data[12] = (raw_block / 75) / 60;
		ATA[channel].Drive[drive].data[13] = (raw_block / 75) % 60;
		ATA[channel].Drive[drive].data[14] = (raw_block % 75);
		ATA[channel].Drive[drive].data[15] = 0x01;
		datadest = &ATA[channel].Drive[drive].data[0x10]; //Start of our read sector!
	}
	else
	{
		datadest = &ATA[channel].Drive[drive].data[0]; //Start of our buffer!
	}

	EMU_setDiskBusy(ATA_Drives[channel][drive], 1| (ATA[channel].Drive[drive].ATAPI_caddyejected << 1)); //We're reading!
	if (ATA[channel].Drive[drive].ATAPI_diskchangepending)
	{
		ATA[channel].Drive[drive].ATAPI_diskchangepending = 0; //Not pending anymore!
	}

	if (!(is_mounted(ATA_Drives[channel][drive])&&ATA[channel].Drive[drive].diskInserted)) { //Error out if not present!
		ascq = 0;
		//Handle like an invalid command!
		EMU_setDiskBusy(ATA_Drives[channel][drive], 0| (ATA[channel].Drive[drive].ATAPI_caddyejected << 1)); //We're doing nothing!
		ATA[channel].Drive[drive].ATAPI_processingPACKET = 3; //Result phase!
		ATA[channel].Drive[drive].commandstatus = 0xFF; //Move to error mode!
		ascq = (ATA[channel].Drive[drive].ATAPI_caddyejected?0x02:0x01);
		ATAPI_giveresultsize(channel,drive,0,1); //No result size!
		ATA[channel].Drive[drive].ERRORREGISTER = /*4|*/(SENSE_NOT_READY<<4); //Reset error register! This also contains a copy of the Sense Key!
		ATAPI_SENSEPACKET_SENSEKEYW(channel,drive,SENSE_NOT_READY); //Reason of the error
		ATAPI_SENSEPACKET_RESERVED2W(channel, drive, 0); //Reserved field!
		ATAPI_SENSEPACKET_ILIW(channel, drive,0); //ILI bit cleared!
		ATAPI_SENSEPACKET_ADDITIONALSENSECODEW(channel,drive,ASC_MEDIUM_NOT_PRESENT); //Extended reason code
		ATAPI_SENSEPACKET_ASCQW(channel, drive, ascq); //ASCQ code!
		ATAPI_SENSEPACKET_ERRORCODEW(channel,drive,0x70); //Default error code?
		ATAPI_SENSEPACKET_ADDITIONALSENSELENGTHW(channel,drive,10); //Additional Sense Length = 10?
		ATAPI_SENSEPACKET_INFORMATION0W(channel,drive,0); //No info!
		ATAPI_SENSEPACKET_INFORMATION1W(channel,drive,0); //No info!
		ATAPI_SENSEPACKET_INFORMATION2W(channel,drive,0); //No info!
		ATAPI_SENSEPACKET_INFORMATION3W(channel,drive,0); //No info!
		ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION0W(channel,drive,0); //No command specific information?
		ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION1W(channel,drive,0); //No command specific information?
		ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION2W(channel,drive,0); //No command specific information?
		ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION3W(channel,drive,0); //No command specific information?
		ATAPI_SENSEPACKET_VALIDW(channel,drive,1); //We're valid!
		ATAPI_SENSEPACKET_CD(channel, drive, 0); //Error in the packet parameters!
		ATA[channel].Drive[drive].STATUSREGISTER = 0x40; //Clear status!
		ATA_STATUSREGISTER_DRIVEREADYW(channel,drive,1); //Ready!
		ATA_STATUSREGISTER_ERRORW(channel,drive,1); //Ready!
		return 0; //Process the error as we're ready!
	}
	if (datablock_ready) goto ATAPI_alreadyread; //Already read? Skip normal reading if so!
	if (readdata(ATA_Drives[channel][drive], datadest, ((uint_64)ATA[channel].Drive[drive].ATAPI_LBA << 11), 0x800)) //Read the data from disk?
	{
		//Fill out the information for the data track read for the non-CUE image!
		LBA2MSFbin(ATA[channel].Drive[drive].ATAPI_LBA+150, &M, &S, &F); //Convert to MSF address!
		ATA[channel].Drive[drive].lastformat = 0x14; //Last format: data track!
		ATA[channel].Drive[drive].lasttrack = 1; //Last track!
		ATA[channel].Drive[drive].lastM = M; //Last address!
		ATA[channel].Drive[drive].lastS = S; //Last address!
		ATA[channel].Drive[drive].lastF = F; //Last address!
		ATAPI_alreadyread: //Already read!
		ATAPI_increasesector(channel,drive); //Increase the current sector!

		ATA[channel].Drive[drive].datapos = 0; //Initialise our data position!
		ATA[channel].Drive[drive].commandstatus = 1; //Transferring data IN!
		ATA[channel].Drive[drive].ATAPI_processingPACKET = 2; //We're transferring ATAPI data now!
		ATAPI_giveresultsize(channel,drive,ATA[channel].Drive[drive].datablock*ATA[channel].Drive[drive].datasize,1); //Result size!
		return 0; //Process the block once we're ready!
	}
	else //Error reading?
	{
		abortreason = SENSE_ILLEGAL_REQUEST; //Illegal request:
		additionalsensecode = ASC_ILLEGAL_MODE_FOR_THIS_TRACK_OR_INCOMPATIBLE_MEDIUM; //Illegal mode or incompatible medium!
		ascq = 0;

		ATA[channel].Drive[drive].ATAPI_processingPACKET = 3; //Result phase!
		ATA[channel].Drive[drive].commandstatus = 0xFF; //Move to error mode!
		ATAPI_giveresultsize(channel,drive, 0, 1); //No result size!
		ATA[channel].Drive[drive].ERRORREGISTER = /*4 |*/ (abortreason << 4); //Reset error register! This also contains a copy of the Sense Key!
		ATAPI_SENSEPACKET_SENSEKEYW(channel, drive, abortreason); //Reason of the error
		ATAPI_SENSEPACKET_RESERVED2W(channel, drive, 0); //Reserved field!
		ATAPI_SENSEPACKET_ADDITIONALSENSECODEW(channel, drive, additionalsensecode); //Extended reason code
		ATAPI_SENSEPACKET_ASCQW(channel, drive, ascq); //ASCQ code!
		if (ATA[channel].Drive[drive].expectedReadDataType == 0xFF) //Set ILI bit for read sector(nn)?
		{
			ATAPI_SENSEPACKET_ILIW(channel, drive, (additionalsensecode==ASC_ILLEGAL_MODE_FOR_THIS_TRACK_OR_INCOMPATIBLE_MEDIUM) ? 1:0); //ILI bit set!
		}
		else
		{
			ATAPI_SENSEPACKET_ILIW(channel, drive, 0); //ILI bit cleared!
		}
		ATAPI_SENSEPACKET_ERRORCODEW(channel, drive, 0x70); //Default error code?
		ATAPI_SENSEPACKET_ADDITIONALSENSELENGTHW(channel, drive, 10); //Additional Sense Length = 10?
		ATAPI_SENSEPACKET_INFORMATION0W(channel, drive, 0); //No info!
		ATAPI_SENSEPACKET_INFORMATION1W(channel, drive, 0); //No info!
		ATAPI_SENSEPACKET_INFORMATION2W(channel, drive, 0); //No info!
		ATAPI_SENSEPACKET_INFORMATION3W(channel, drive, 0); //No info!
		ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION0W(channel, drive, 0); //No command specific information?
		ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION1W(channel, drive, 0); //No command specific information?
		ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION2W(channel, drive, 0); //No command specific information?
		ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION3W(channel, drive, 0); //No command specific information?
		ATAPI_SENSEPACKET_VALIDW(channel, drive, 1); //We're valid!
		ATAPI_SENSEPACKET_CD(channel, drive, 0); //Error in the packet parameters!
		ATA[channel].Drive[drive].STATUSREGISTER = 0x40; //Clear status!
		ATA_STATUSREGISTER_DRIVEREADYW(channel, drive, 1); //Ready!
		ATA_STATUSREGISTER_ERRORW(channel, drive, 1); //Ready!
		ATAPI_aborted = 1; //Aborted!
	ATAPI_erroroutread:
		ATA_STATUSREGISTER_ERRORW(channel,drive,1); //Set error bit!
		EMU_setDiskBusy(ATA_Drives[channel][drive], 0|(ATA[channel].Drive[drive].ATAPI_caddyejected << 1)); //We're doing nothing!
		ATA[channel].Drive[drive].commandstatus = 0xFF; //Error!
		ATA[channel].Drive[drive].ATAPI_processingPACKET = 3; //We've finished transferring ATAPI data now!
		ATAPI_giveresultsize(channel,drive,0,1); //No result size!
		return 0; //Stop! IRQ and finish!
	}
	ATA[channel].Drive[drive].commandstatus = 0; //Error!
	ATA[channel].Drive[drive].ATAPI_processingPACKET = 3; //We've finished transferring ATAPI data now!
	ATAPI_giveresultsize(channel,drive,0,1); //No result size!
	return 0; //We're finished!
}

byte ATA_caddyejected(int disk) //Is the caddy ejected?
{
	byte disk_drive, disk_channel, disk_nr;
	switch (disk) //What disk?
	{
		//Four disk numbers!
	case HDD0:
		disk_nr = 0;
		break;
	case HDD1:
		disk_nr = 1;
		break;
	case CDROM0:
		disk_nr = 2;
		break;
	case CDROM1:
		disk_nr = 3;
		break;
	default: //Unsupported?
		return 1; //Abort, we're unsupported, so allow changes!
	}
	disk_channel = ATA_DrivesReverse[disk_nr][0]; //The channel of the disk!
	disk_drive = ATA_DrivesReverse[disk_nr][1]; //The master/slave of the disk!
	return ATA[disk_channel].Drive[disk_drive].ATAPI_caddyejected; //Is the caddy ejected?
}

//ejectRequested: 0=Normal behaviour, 1=Eject/mount from disk mounting request, 2=Eject from CPU.
byte ATA_allowDiskChange(int disk, byte ejectRequested) //Are we allowing this disk to be changed?
{
	byte disk_drive, disk_channel, disk_nr;
	switch (disk) //What disk?
	{
		//Four disk numbers!
	case HDD0:
		disk_nr = 0;
		break;
	case HDD1:
		disk_nr = 1;
		break;
	case CDROM0:
		disk_nr = 2;
		break;
	case CDROM1:
		disk_nr = 3;
		break;
	default: //Unsupported?
		return 1; //Abort, we're unsupported, so allow changes!
	}
	disk_channel = ATA_DrivesReverse[disk_nr][0]; //The channel of the disk!
	disk_drive = ATA_DrivesReverse[disk_nr][1]; //The master/slave of the disk!
	if ((ejectRequested==1) && (ATA[disk_channel].Drive[disk_drive].EnableMediaStatusNotification|(ATA[disk_channel].Drive[disk_drive].preventMediumRemoval&2))) //Requesting eject button from user while media status notification is enabled(the OS itself handes us) or locked by ATAPI?
	{
		if (ATA[disk_channel].Drive[disk_drive].ATAPI_caddyejected) //Caddy is ejected?
		{
			return 1; //Allow changing of the mounted media always!
		}
		//Caddy is inserted? Block us!
		return 0; //Deny access to the mounted disk!
	}
	return (!(ATA[disk_channel].Drive[disk_drive].preventMediumRemoval && (ejectRequested!=2))) || (ATA[disk_channel].Drive[disk_drive].allowDiskInsertion || ATA[disk_channel].Drive[disk_drive].ATAPI_caddyejected); //Are we not preventing removal of this medium?
}

byte ATAPI_supportedmodepagecodes[0x4] = { 0x01, 0x0D, 0x0E, 0x2A }; //Supported pages!
word ATAPI_supportedmodepagecodes_length[0x4] = {0x6,0x6,0xD,0xC}; //The length of the pages stored in our memory!

OPTINLINE void ATAPI_calculateByteCountLeft(byte channel, byte drive)
{
	if (ATA[channel].Drive[drive].ATAPI_bytecountleft) //Byte counter is running for this device?
	{
		--ATA[channel].Drive[drive].ATAPI_bytecountleft; //Decrease the counter that's transferring!
		if ((ATA[channel].Drive[drive].ATAPI_bytecountleft==0) && (ATA[channel].Drive[drive].datasize)) //Finished transferring the subblock and something left to transfer?
		{
			ATAPI_giveresultsize(channel,drive,MIN(ATA[channel].Drive[drive].datablock-ATA[channel].Drive[drive].datapos,0xFFFE),ATA[channel].Drive[drive].ATAPI_bytecountleft_IRQ); //Start waiting until we're to transfer the next subblock for the remaining data!
		}
	}
}

OPTINLINE byte ATA_dataIN(byte channel) //Byte read from data!
{
	byte readsector_result;
	byte result;
	switch (ATA[channel].Drive[ATA_activeDrive(channel)].command) //What command?
	{
	case 0x20:
	case 0x21: //Read sectors?
	case 0x22: //Read long (w/retry)?
	case 0x23: //Read long (w/o retry)?
	case 0xC4: //Read multiple?
		result = ATA[channel].Drive[ATA_activeDrive(channel)].data[ATA[channel].Drive[ATA_activeDrive(channel)].datapos++]; //Read the data byte!
		if (ATA[channel].Drive[ATA_activeDrive(channel)].datapos == ATA[channel].Drive[ATA_activeDrive(channel)].datablock) //Full block read?
		{
			if ((readsector_result = ATA_readsector(channel,ATA[channel].Drive[ATA_activeDrive(channel)].command))) //Next sector read?
			{
				if (readsector_result == 1) //Continuing?
				{
					ATA_IRQ(channel, ATA_activeDrive(channel), ATA_FINISHREADYTIMING(6.0),/*ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus?1:0*/ 0); //Give our requesting IRQ!
				}
				else //Finishing?
				{
					ATA_IRQ(channel, ATA_activeDrive(channel), ATA_FINISHREADYTIMING(6.0),/*ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus?1:0*/ 0); //Give our requesting IRQ!
				}
			}
		}
	        return result; //Give the result!
		break;
	case 0xA0: //PACKET?
		if (ATA[channel].Drive[ATA_activeDrive(channel)].ATAPI_processingPACKET!=1) //Sending data?
		{
			switch (ATA[channel].Drive[ATA_activeDrive(channel)].ATAPI_PACKET[0]) //What command?
			{
			case 0x25: //Read capacity?
			case 0x12: //Inquiry?
			case 0x03: //REQUEST SENSE(Mandatory)?
			case 0x5A: //MODE SENSE(10)(Mandatory)?
			case 0x42: //Read sub-channel (mandatory)?
			case 0x43: //Read TOC (mandatory)?
			case 0x44: //Read header (mandatory)?
			case 0xBD: //Mechanism status(mandatory)
				result = ATA[channel].Drive[ATA_activeDrive(channel)].data[ATA[channel].Drive[ATA_activeDrive(channel)].datapos++]; //Read the data byte!
				if (ATA[channel].Drive[ATA_activeDrive(channel)].datapos == ATA[channel].Drive[ATA_activeDrive(channel)].datablock) //Full block read?
				{
					ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus = 0; //Reset to enter a new command!
					ATA[channel].Drive[ATA_activeDrive(channel)].ATAPI_processingPACKET = 3; //We've finished transferring ATAPI data now!
					ATAPI_giveresultsize(channel,ATA_activeDrive(channel),0,1); //Raise an final IRQ to signify we're finished, busy in the meanwhile!
				}
				else //Still transferring data?
				{
					ATAPI_calculateByteCountLeft(channel,ATA_activeDrive(channel)); //Update!
				}
				return result; //Give the result!
				break;
			case 0x28: //Read sectors (10) command(Mandatory)?
			case 0xA8: //Read sector (12) command(Mandatory)?
			case 0xBE: //Read CD command(mandatory)?
			case 0xB9: //Read CD MSF (mandatory)?
				result = ATA[channel].Drive[ATA_activeDrive(channel)].data[ATA[channel].Drive[ATA_activeDrive(channel)].datapos++]; //Read the data byte!
				if (ATA[channel].Drive[ATA_activeDrive(channel)].datapos==ATA[channel].Drive[ATA_activeDrive(channel)].datablock) //Full block read?
				{
					if (ATAPI_readsector(channel,ATA_activeDrive(channel))) //Next sector read?
					{
						ATA_IRQ(channel,ATA_activeDrive(channel),ATAPI_FINISHREADYTIMING,0); //Raise an IRQ: we're needing attention!
					}
				}
				else //Still transferring data?
				{
					ATAPI_calculateByteCountLeft(channel,ATA_activeDrive(channel)); //Update!
				}
				return result; //Give the result!
				break;
			default: //Unknown command?
				break;
			}
		}
		break;
	case 0xEC: //Identify?
	case 0xA1: //IDENTIFY PACKET DEVICE?
		result = ATA[channel].Drive[ATA_activeDrive(channel)].data[ATA[channel].Drive[ATA_activeDrive(channel)].datapos++]; //Read the result byte!
		if (ATA[channel].Drive[ATA_activeDrive(channel)].datapos == ATA[channel].Drive[ATA_activeDrive(channel)].datablock) //Fully read?
		{
			ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus = 0; //Reset command!
			if (ATA[channel].Drive[ATA_activeDrive(channel)].command == 0xA1) //For CD-ROM drives only, raise another IRQ?
			{
				ATA_IRQ(channel, ATA_activeDrive(channel),ATA[channel].Drive[ATA_activeDrive(channel)].command==0xA1?ATAPI_FINISHREADYTIMING:ATA_FINISHREADYTIMING(1.0),0); //Raise an IRQ: we're needing attention!
			}
		}
		return result; //Give the result byte!
	default: //Unknown?
		break;
	}
	return 0; //Unknown data!
}

void ATAPI_executeData(byte channel, byte drive); //Prototype for ATAPI data processing!

OPTINLINE void ATA_dataOUT(byte channel, byte data) //Byte written to data!
{
	switch (ATA[channel].Drive[ATA_activeDrive(channel)].command) //What command?
	{
	case 0x30: //Write sector(s) (w/retry)?
	case 0x31: //Write sectors (w/o retry)?
	case 0x32: //Write long (w/retry)?
	case 0x33: //Write long (w/o retry)?
	case 0xC5: //Write multiple?
		ATA[channel].Drive[ATA_activeDrive(channel)].data[ATA[channel].Drive[ATA_activeDrive(channel)].datapos++] = data; //Write the data byte!
		if (ATA[channel].Drive[ATA_activeDrive(channel)].datapos == ATA[channel].Drive[ATA_activeDrive(channel)].datablock) //Full block read?
		{
			if (ATA_writesector(channel,ATA[channel].Drive[ATA_activeDrive(channel)].command)) //Sector written and to write another sector?
			{
				ATA_IRQ(channel, ATA_activeDrive(channel),ATA_FINISHREADYTIMING(6.0),1); //Give our requesting IRQ!
			}
		}
		break;
	case 0xA0: //ATAPI: PACKET!
		if (ATA[channel].Drive[ATA_activeDrive(channel)].ATAPI_processingPACKET==1) //Are we processing a packet?
		{
			ATA[channel].Drive[ATA_activeDrive(channel)].ATAPI_PACKET[ATA[channel].Drive[ATA_activeDrive(channel)].datapos++] = data; //Add the packet byte!
			if (ATA[channel].Drive[ATA_activeDrive(channel)].datapos==12) //Full packet written?
			{
				//Cancel DRQ, Set BSY and read Features and Byte count from the Task File.
				ATA[channel].Drive[ATA_activeDrive(channel)].ATAPI_bytecount = ATAPI_getresultsize(channel,ATA_activeDrive(channel)); //Read the size to transfer at most!
				ATA[channel].Drive[ATA_activeDrive(channel)].ATAPI_processingPACKET = 0; //We're not processing a packet anymore, from now on we're data only!
				ATAPI_PendingExecuteCommand(channel, ATA_activeDrive(channel)); //Execute the ATAPI command!
			}
		}
		else //We're processing data for an ATAPI packet?
		{
			ATA[channel].Drive[ATA_activeDrive(channel)].data[ATA[channel].Drive[ATA_activeDrive(channel)].datapos++] = data; //Write the data byte!
			if (ATA[channel].Drive[ATA_activeDrive(channel)].datapos == ATA[channel].Drive[ATA_activeDrive(channel)].datablock) //Full block read?
			{
				ATAPI_executeData(channel,ATA_activeDrive(channel)); //Execute the data process!
			}
			else //Still transferring data?
			{
				ATAPI_calculateByteCountLeft(channel,ATA_activeDrive(channel)); //Update!
			}
		}
		break;
	default: //Unknown?
		break;
	}
}

void ATAPI_executeData(byte channel, byte drive)
{
	word pageaddr;
	byte pagelength; //The length of the page!
	ATA[channel].Drive[drive].ATAPI_processingPACKET = 3; //We're not processing a packet anymore! Default to result phase!
	switch (ATA[channel].Drive[drive].ATAPI_PACKET[0]) //What command?
	{
	case 0x55: //MODE SELECT(10)(Mandatory)?
		//Store the data, just ignore it!
		//Copy pages that are supported to their location in the Active Mode data!
		for (pageaddr=0;pageaddr<(ATA[channel].Drive[drive].datablock-1);) //Process all available data!
		{
			pagelength = ATA[channel].Drive[drive].data[pageaddr + 1]-1; //This value is the last byte used minus 1(zero-based)!
			switch (ATA[channel].Drive[drive].data[pageaddr]&0x3F) //What page code?
			{
				case 0x01: //Read error recovery page (Mandatory)?
					pagelength = MIN(pagelength, 0x6); //Maximum length to apply!
					memcpy(&ATA[channel].Drive[drive].ATAPI_ModeData[0x01 << 8], &ATA[channel].Drive[drive].data[pageaddr + 2], pagelength); //Copy the page data to our position, simply copy all data!
					break;
				case 0x0D: //CD-ROM page?
					pagelength = MIN(pagelength, 0x6); //Maximum length to apply!
					memcpy(&ATA[channel].Drive[drive].ATAPI_ModeData[0x0D << 8], &ATA[channel].Drive[drive].data[pageaddr + 2], pagelength); //Copy the page data to our position, simply copy all data!
					break;
				case 0x0E: //CD-ROM audio control page?
					pagelength = MIN(pagelength,0xD); //Maximum length to apply!
					memcpy(&ATA[channel].Drive[drive].ATAPI_ModeData[0x0E<<8],&ATA[channel].Drive[drive].data[pageaddr+2],pagelength); //Copy the page data to our position, simply copy all data!
					break;
				case 0x2A: //CD-ROM capabilities & Mechanical Status Page?
					pagelength = MIN(pagelength, 0xC); //Maximum length to apply!
					memcpy(&ATA[channel].Drive[drive].ATAPI_ModeData[0x2A << 8], &ATA[channel].Drive[drive].data[pageaddr + 2], pagelength); //Copy the page data to our position, simply copy all data!
					break;
				default: //Unknown page? Ignore it!
					break;
			}
			pageaddr += ATA[channel].Drive[drive].data[pageaddr+1]+1; //Jump to the next block, if any!
		}
		ATAPI_setModePages(channel, drive); //Reset any ROM values!
		ATA[channel].Drive[drive].commandstatus = 0; //Reset status: we're done!
		ATAPI_giveresultsize(channel,drive,0,1); //Raise an final IRQ to signify we're finished, busy in the meanwhile!
		break;
	default:
		break;
	}	
}

//read_TOC conversion from http://bochs.sourceforge.net/cgi-bin/lxr/source/iodev/hdimage/cdrom.cc
//adjusted to allow multiple tracks to be reported.
byte ATAPI_generateTOC(byte* buf, sword* length, byte msf, sword start_track, sword format, byte channel, byte drive)
{
	byte track; //Track counter!
	char *cuedisk;
	int_64 cueresult=0;
	byte cue_startM, cue_startS, cue_startF, cue_endM, cue_endS, cue_endF, cue_M, cue_S, cue_F;
	byte cue_skipM, cue_skipS, cue_skipF; //The address after checking the format of the track, where the format starts!
	unsigned i;
	uint_32 blocks;
	int len = 4;
	byte trackfound = 0;
	byte iscue=0;
	if ((cuedisk = getCUEimage(ATA_Drives[channel][drive]))) //Is a CUE disk?
	{
		if (is_cueimage(cuedisk)) //Valid disk image?
		{
			CDROM_selecttrack(ATA_Drives[channel][drive],0); //All tracks!
			CDROM_selectsubtrack(ATA_Drives[channel][drive],0); //All subtracks!
			LBA2MSFbin(ATA[channel].Drive[drive].ATAPI_LBA, &cue_M, &cue_S, &cue_F); //Generate a MSF address to use with CUE images!
			cueresult = cueimage_getgeometry(ATA_Drives[channel][drive], &cue_M, &cue_S, &cue_F, &cue_startM, &cue_startS, &cue_startF, &cue_endM, &cue_endS, &cue_endF,0); //Try to read as specified!
			iscue = 1; //Loaded!
		}
	}
	else //Default track/subtrack!
	{
			CDROM_selecttrack(ATA_Drives[channel][drive],1); //All tracks!
			CDROM_selectsubtrack(ATA_Drives[channel][drive],1); //All subtracks!
	}
	switch (format) {
		case 0:
				// From atapi specs : start track can be 0-63, AA
				if ((start_track > 63) && (start_track != 0xaa))
					return 0;
				//Lead in track!
				buf[2] = 1; //First track number
				buf[3] = 1; //Last track number
				for (track = 1; track < 100; ++track) //Process all possible tracks!
				{
					if (track >= start_track) //Process this track in the result?
					{
						if ((track == 1) && (!iscue)) //Not a cue disk, but track 1?
						{
							trackfound = 1; //Found track 1 only!
						}
						else if (iscue) //Valid CUE disk to report a track for?
						{
							CDROM_selecttrack(ATA_Drives[channel][drive], track); //Selected track!
							CDROM_selectsubtrack(ATA_Drives[channel][drive], 0); //All subtracks!
							LBA2MSFbin(ATA[channel].Drive[drive].ATAPI_LBA, &cue_M, &cue_S, &cue_F); //Generate a MSF address to use with CUE images!
							cueresult = cueimage_getgeometry(ATA_Drives[channel][drive], &cue_M, &cue_S, &cue_F, &cue_startM, &cue_startS, &cue_startF, &cue_endM, &cue_endS, &cue_endF,0); //Try to read as specified!
							cueresult = cueimage_getgeometry(ATA_Drives[channel][drive], &cue_M, &cue_S, &cue_F, &cue_startM, &cue_startS, &cue_startF, &cue_endM, &cue_endS, &cue_endF,0); //Try to read as specified!
							if ((cueresult>=1) || (cueresult<=-2)) //Track found?
							{
								trackfound = 1; //Track found!
								if (cueresult <= -2) //To skip some tracks?
								{
									LBA2MSFbin(MSF2LBAbin(cue_startM, cue_startS, cue_startF) + (uint_32)(-(cueresult + 2)), &cue_skipM, &cue_skipS, &cue_skipF); //Skip this much!
									cueresult = cueimage_readsector(ATA_Drives[channel][drive], cue_skipM, cue_skipS, cue_skipF,NULL,0); //Try to read as specified!
								}
							}
							else
							{
								trackfound = 0; //Track not found!
							}
						}
						else
						{
							trackfound = 0; //Track not found!
						}
						if (trackfound) //Track found? Report a track!
						{
							buf[len++] = 0; // Reserved
							if (!iscue)
							{
								buf[len++] = 0x14; //ADR / control
							}
							else if (cueresult == (1 + MODE_AUDIO)) //Audio track?
							{
								buf[len++] = 0x10; //ADR / control
							}
							else //Data track?
							{
								buf[len++] = 0x14; //ADR / control
							}
							buf[len++] = track; // Track number
							buf[len++] = 0; // Reserved
							// Start address
							if (iscue == 0) //Non-cue track!
							{
								if (msf) {
									buf[len++] = 0; // reserved
									buf[len++] = 0; // minute
									buf[len++] = 2; // second
									buf[len++] = 0; // frame
								}
								else {
									buf[len++] = 0;
									buf[len++] = 0;
									buf[len++] = 0;
									buf[len++] = 0; // logical sector 0
								}
							}
							else //CUE track reporting!
							{
								blocks = MSF2LBAbin(cue_startM, cue_startS, cue_startF); //Take the blocks of the CUE image!
								if (msf) {
									buf[len++] = 0; // reserved
									buf[len++] = cue_startM; // minute
									buf[len++] = cue_startS; // second
									buf[len++] = cue_startF; // frame
								}
								else {
									buf[len++] = (blocks >> 24) & 0xff;
									buf[len++] = (blocks >> 16) & 0xff;
									buf[len++] = (blocks >> 8) & 0xff;
									buf[len++] = (blocks >> 0) & 0xff;
								}
							}
						}
						else //Track not found? Last track reached! Start the lead-out!
						{
							--track; //One track up to get the last track!
							goto startleadout0; //Stop searching for more tracks!
						}
					}
					else //Maybe exists, but check anyways!
					{
						if ((track == 1) && (!iscue)) //Not a cue disk, but track 1?
						{
							trackfound = 1; //Found track 1 only!
						}
						else if (iscue) //Valid CUE disk to report a track for?
						{
							CDROM_selecttrack(ATA_Drives[channel][drive], track); //Selected track!
							CDROM_selectsubtrack(ATA_Drives[channel][drive], 0); //All subtracks!
							LBA2MSFbin(ATA[channel].Drive[drive].ATAPI_LBA, &cue_M, &cue_S, &cue_F); //Generate a MSF address to use with CUE images!
							cueresult = cueimage_getgeometry(ATA_Drives[channel][drive], &cue_M, &cue_S, &cue_F, &cue_startM, &cue_startS, &cue_startF, &cue_endM, &cue_endS, &cue_endF,0); //Try to read as specified!
							if ((cueresult>=1) || (cueresult<=-2)) //Track found?
							{
								trackfound = 1; //Track found!
							}
							else
							{
								trackfound = 0; //Track not found!
							}
						}
						else
						{
							trackfound = 0; //Track not found!
						}
						if (!trackfound) //Track not found? Last track reached! Start the lead-out!
						{
							--track; //One track up to get the last track!
							goto startleadout0; //Stop searching for more tracks!
						}
					}
				}
				startleadout0: //Start the leadout!
				if (track >= 1) //Valid track count to report?
				{
					buf[3] = track; //The last track number!
				}
				// Lead out track
				if (iscue) //Valid CUE disk?
				{
					CDROM_selecttrack(ATA_Drives[channel][drive], 0); //All tracks!
					CDROM_selectsubtrack(ATA_Drives[channel][drive], 0); //All subtracks!
					LBA2MSFbin(ATA[channel].Drive[drive].ATAPI_LBA, &cue_M, &cue_S, &cue_F); //Generate a MSF address to use with CUE images!
					cueresult = cueimage_getgeometry(ATA_Drives[channel][drive], &cue_M, &cue_S, &cue_F, &cue_startM, &cue_startS, &cue_startF, &cue_endM, &cue_endS, &cue_endF,0); //Try to read as specified!
				}
				buf[len++] = 0; // Reserved
				buf[len++] = 0x16; // ADR, control
				buf[len++] = 0xaa; // Track number
				buf[len++] = 0; // Reserved
				if (iscue == 0) //Not a cue result?
				{
					blocks = ATA[channel].Drive[drive].ATAPI_disksize; //Get the drive size from the disk information, in 2KB blocks!
					// End address
					if (msf) {
						buf[len++] = 0; // reserved
						buf[len++] = (byte)(((blocks + 150) / 75) / 60); // minute
						buf[len++] = (byte)(((blocks + 150) / 75) % 60); // second
						buf[len++] = (byte)((blocks + 150) % 75); // frame;
					}
					else {
						buf[len++] = (blocks >> 24) & 0xff;
						buf[len++] = (blocks >> 16) & 0xff;
						buf[len++] = (blocks >> 8) & 0xff;
						buf[len++] = (blocks >> 0) & 0xff;
					}
				}
				else
				{
					blocks = MSF2LBAbin(cue_M, cue_S, cue_F); //Take the blocks of the CUE image!
					// End address
					if (msf) {
						buf[len++] = 0; // reserved
						buf[len++] = cue_M; // minute
						buf[len++] = cue_S; // second
						buf[len++] = cue_F; // frame;
					}
					else {
						buf[len++] = (blocks >> 24) & 0xff;
						buf[len++] = (blocks >> 16) & 0xff;
						buf[len++] = (blocks >> 8) & 0xff;
						buf[len++] = (blocks >> 0) & 0xff;
					}
				}
				buf[0] = ((len - 2) >> 8) & 0xff;
				buf[1] = (len - 2) & 0xff;
				break;
			case 1:
				// multi session stuff - emulate a single session only
				buf[0] = 0;
				buf[1] = 0x0a; //TOC data length
				buf[2] = 1; //First session number
				buf[3] = 1; //Last session number
				track = 1; //Track 1 only!
				if (iscue) //CUE?
				{
					CDROM_selecttrack(ATA_Drives[channel][drive], track); //Selected track!
					CDROM_selectsubtrack(ATA_Drives[channel][drive], 0); //All subtracks!
					LBA2MSFbin(ATA[channel].Drive[drive].ATAPI_LBA, &cue_M, &cue_S, &cue_F); //Generate a MSF address to use with CUE images!
					cueresult = cueimage_getgeometry(ATA_Drives[channel][drive], &cue_M, &cue_S, &cue_F, &cue_startM, &cue_startS, &cue_startF, &cue_endM, &cue_endS, &cue_endF,0); //Try to read as specified!
					if ((cueresult >= 1) || (cueresult<=-2)) //Track found?
					{
						trackfound = 1; //Track found!
						if (cueresult <= -2) //To skip some tracks?
						{
							LBA2MSFbin(MSF2LBAbin(cue_startM, cue_startS, cue_startF) + (uint_32)(-(cueresult + 2)), &cue_skipM, &cue_skipS, &cue_skipF); //Skip this much!
							cueresult = cueimage_readsector(ATA_Drives[channel][drive], cue_skipM, cue_skipS, cue_skipF, NULL, 0); //Try to read as specified!
						}
					}
					else
					{
						trackfound = 0; //Track not found!
					}
					if (trackfound)
					{
						blocks = MSF2LBAbin(cue_startM, cue_startS, cue_startF); //Start LBA of the disc!
					}
					else
					{
						blocks = 0; //Unknown?
					}
				}
				else //Normal disc image?
				{
					blocks = 0; //Normal start of the disc! Just a plain LBA at the start of the disc!
				}
				buf[4] = 0; //Reserved
				buf[5] = trackfound ? ((cueresult == 1 + MODE_AUDIO) ? 0x10 : 0x14) : 0x14; //ADR / Control: Audio/data track?
				buf[6] = 1; //First track number in last complete session
				buf[7] = 0; //Reserved
				//Start Address of the first track(in LBA format) in Last Session
				buf[8] = (blocks >> 24) & 0xff;
				buf[9] = (blocks >> 16) & 0xff;
				buf[10] = (blocks >> 8) & 0xff;
				buf[11] = (blocks >> 0) & 0xff;
				len = 12;
				break;
			case 2:
				// raw toc - emulate a single session only (ported from qemu)
				buf[2] = 1; //First session number
				buf[3] = 1; //Last session number

				//First, check the amount of tracks!
				for (track = 1; track < 100; ++track) //Process all possible tracks!
				{
					if (track >= start_track) //Process this track in the result?
					{
						if ((track == 1) && (!iscue)) //Not a cue disk, but track 1?
						{
							trackfound = 1; //Found track 1 only!
						}
						else if (iscue) //Valid CUE disk to report a track for?
						{
							CDROM_selecttrack(ATA_Drives[channel][drive], track); //Selected track!
							CDROM_selectsubtrack(ATA_Drives[channel][drive], 0); //All subtracks!
							LBA2MSFbin(ATA[channel].Drive[drive].ATAPI_LBA, &cue_M, &cue_S, &cue_F); //Generate a MSF address to use with CUE images!
							cueresult = cueimage_getgeometry(ATA_Drives[channel][drive], &cue_M, &cue_S, &cue_F, &cue_startM, &cue_startS, &cue_startF, &cue_endM, &cue_endS, &cue_endF,0); //Try to read as specified!
							cueresult = cueimage_getgeometry(ATA_Drives[channel][drive], &cue_M, &cue_S, &cue_F, &cue_startM, &cue_startS, &cue_startF, &cue_endM, &cue_endS, &cue_endF,0); //Try to read as specified!
							if ((cueresult >= 1) || (cueresult<=-2)) //Track found?
							{
								trackfound = 1; //Track found!
								if (cueresult <= -2) //To skip some tracks?
								{
									LBA2MSFbin(MSF2LBAbin(cue_startM, cue_startS, cue_startF) + (uint_32)(-(cueresult + 2)), &cue_skipM, &cue_skipS, &cue_skipF); //Skip this much!
									cueresult = cueimage_readsector(ATA_Drives[channel][drive], cue_skipM, cue_skipS, cue_skipF, NULL, 0); //Try to read as specified!
								}
							}
							else
							{
								trackfound = 0; //Track not found!
							}
						}
						else
						{
							trackfound = 0; //Track not found!
						}
						if (!trackfound) //Track not found? Last track reached! Start the lead-out!
						{
							--track; //One track up to get the last track!
							goto startleadout2; //Stop searching for more tracks!
						}
					}
					else //Maybe exists, but check anyways!
					{
						if ((track == 1) && (!iscue)) //Not a cue disk, but track 1?
						{
							trackfound = 1; //Found track 1 only!
						}
						else if (iscue) //Valid CUE disk to report a track for?
						{
							CDROM_selecttrack(ATA_Drives[channel][drive], track); //Selected track!
							CDROM_selectsubtrack(ATA_Drives[channel][drive], 0); //All subtracks!
							LBA2MSFbin(ATA[channel].Drive[drive].ATAPI_LBA, &cue_M, &cue_S, &cue_F); //Generate a MSF address to use with CUE images!
							cueresult = cueimage_getgeometry(ATA_Drives[channel][drive], &cue_M, &cue_S, &cue_F, &cue_startM, &cue_startS, &cue_startF, &cue_endM, &cue_endS, &cue_endF,0); //Try to read as specified!
							if ((cueresult >= 1) || (cueresult<=-2)) //Track found?
							{
								trackfound = 1; //Track found!
								if (cueresult <= -2) //To skip some tracks?
								{
									LBA2MSFbin(MSF2LBAbin(cue_startM, cue_startS, cue_startF) + (uint_32)(-(cueresult + 2)), &cue_skipM, &cue_skipS, &cue_skipF); //Skip this much!
									cueresult = cueimage_readsector(ATA_Drives[channel][drive], cue_skipM, cue_skipS, cue_skipF, NULL, 0); //Try to read as specified!
								}
							}
							else
							{
								trackfound = 0; //Track not found!
							}
						}
						else
						{
							trackfound = 0; //Track not found!
						}
						if (!trackfound) //Track not found? Last track reached! Start the lead-out!
						{
							--track; //One track up to get the last track!
							goto startleadout2; //Stop searching for more tracks!
						}
					}
				}
			startleadout2:
				if (!track) track = 1; //One track at least!

				//Now, build the rest of the information!
				for (i = 0; i < 4U+(track-1); i++) { //A0-A2 and all the tracks!
					buf[len++] = 1; //Session number
					if (!iscue)
					{
						buf[len++] = 0x14; //ADR / control
					}
					else if (cueresult == (1+MODE_AUDIO)) //Audio track?
					{
						buf[len++] = 0x10; //ADR / control
					}
					else //Data track?
					{
						buf[len++] = 0x14; //ADR / control
					}
					buf[len++] = 0; //Track (TOC = 0)
					if (i < 3) { //A0-A2 pointers?
						buf[len++] = 0xa0 + i; //Point
						//Track is the final track!
					}
					else {
						track = 1 + (i - 3); //The track number to give!
						buf[len++] = track; //Point
					}
					//MSF start of track!
					buf[len++] = 0; //Min
					buf[len++] = 0; //Sec
					buf[len++] = 0; //Frame

					if (i < 2) { //A0-A2 pointers?
						switch (i) //What A record?
						{
						case 0: //A0? First track number/disk type!
							if (iscue == 0) //Not a cue result?
							{
								buf[len++] = 0; //Zero
								buf[len++] = 1; //Min: First track number
								buf[len++] = 0; //Sec: Disc type
								buf[len++] = 0; //Frame
							}
							else //CUE?
							{
								buf[len++] = 0; //Zero
								buf[len++] = 0; //Min
								buf[len++] = 0; //Sec
								buf[len++] = 0; //Frame
							}
							break;
						case 1: //A1? Last track number
							if (iscue == 0) //Not a cue result?
							{
								buf[len++] = 0; //Zero
								buf[len++] = track; //Min: Last track number
								buf[len++] = 0; //Sec
								buf[len++] = 0; //Frame
							}
							else //CUE?
							{
								buf[len++] = 0; //Zero
								buf[len++] = track; //Min: Last track number
								buf[len++] = 0; //Sec
								buf[len++] = 0; //Frame
							}
							break;
						}
					}
					else if (i == 2) { //A2? Start position of lead-out!
						if (iscue == 0) //Not a cue result?
						{
							blocks = ATA[channel].Drive[drive].ATAPI_disksize; //Get the drive size from the disk information, in 2KB blocks!
							// Start address
							buf[len++] = 0; // Zero
							buf[len++] = (byte)(((blocks + 150) / 75) / 60); // minute
							buf[len++] = (byte)(((blocks + 150) / 75) % 60); // second
							buf[len++] = (byte)((blocks + 150) % 75); // frame;
						}
						else
						{
							// Start address
							blocks = MSF2LBAbin(cue_M, cue_S, cue_F); //Take the blocks of the CUE image!
							buf[len++] = 0; // Zero
							buf[len++] = cue_M; // minute
							buf[len++] = cue_S; // second
							buf[len++] = cue_F; // frame;
						}
					}
					else { //The actual tracks?
						if ((track == 1) && (!iscue)) //Not a cue disk, but track 1?
						{
							trackfound = 1; //Found track 1 only!
							cue_startM = 0;
							cue_startS = 2;
							cue_startF = 0;
						}
						else if (iscue) //Valid CUE disk to report a track for?
						{
							CDROM_selecttrack(ATA_Drives[channel][drive], track); //Selected track!
							CDROM_selectsubtrack(ATA_Drives[channel][drive], 0); //All subtracks!
							LBA2MSFbin(ATA[channel].Drive[drive].ATAPI_LBA, &cue_M, &cue_S, &cue_F); //Generate a MSF address to use with CUE images!
							cueresult = cueimage_getgeometry(ATA_Drives[channel][drive], &cue_M, &cue_S, &cue_F, &cue_startM, &cue_startS, &cue_startF, &cue_endM, &cue_endS, &cue_endF,0); //Try to read as specified!
							if ((cueresult >= 1) || (cueresult<=-2)) //Track found?
							{
								trackfound = 1; //Track found!
								//We don't need the type, so no extra parsing of gaps!
							}
							else
							{
								trackfound = 0; //Track not found!
							}
						}
						else
						{
							trackfound = 0; //Track not found!
						}
						if (!trackfound) //Track not found?
						{
							buf[len++] = 0;
							buf[len++] = 0;
							buf[len++] = 0;
							buf[len++] = 0;
						}
						else //Start position of the track?
						{
							buf[len++] = 0; //Zero
							buf[len++] = cue_startM; //M
							buf[len++] = cue_startS; //S
							buf[len++] = cue_startF; //F
						}
					}
				}
				buf[0] = ((len - 2) >> 8) & 0xff;
				buf[1] = (len - 2) & 0xff;
			break;
		default:
			return 0;
	}
	*length = len;
	return 1;
}

void ATAPI_command_reportError(byte channel, byte slave)
{
	//State=Ready?
	ATA[channel].Drive[slave].ATAPI_processingPACKET = 3; //Result phase!
	ATA[channel].Drive[slave].ERRORREGISTER = ((ATA[channel].Drive[slave].SensePacket[2]&0xF)<<4)|/*((ATA[channel].Drive[slave].SensePacket[2]&0xF)?4 / abort? / :0)*/ 0;
	ATA[channel].Drive[slave].commandstatus = 0xFF; //Error!
	ATA_STATUSREGISTER_DRIVEREADYW(channel,slave,1); //Ready!
	ATA[channel].Drive[slave].STATUSREGISTER = 0x40; //Ready!
	if (ATA[channel].Drive[slave].SensePacket[2]&0xF) //Error?
	{
		ATA_STATUSREGISTER_ERRORW(channel,slave,1);
	}
	ATA_STATUSREGISTER_DRIVESEEKCOMPLETEW(channel,slave,0); //No service(when enabled), nor drive seek complete!
	ATA[channel].Drive[slave].commandstatus = 0xFF; //Move to error mode!
	ATAPI_giveresultsize(channel,slave,0,1); //No result size!
}

//List of mandatory commands from http://www.bswd.com/sff8020i.pdf page 106 (ATA packet interface for CD-ROMs SFF-8020i Revision 2.6)
void ATAPI_executeCommand(byte channel, byte drive) //Prototype for ATAPI execute Command!
{
	char CDROM_id[256];
	//We're to move to either HPD3(raising an IRQ when enabled, which moves us to HPD2) or HPD2(data phase). Busy must be cleared to continue transferring, otherwise software's waiting. Next we start HPD4(data transfer phase) to transfer data if needed, finish otherwise.
	//Stuff based on Bochs
	byte Mseek, Sseek, Fseek;
	byte MSF; //MSF bit!
	byte sub_Q; //SubQ bit!
	byte data_format; //Sub-channel Data Format
	//byte track_number; //Track number
	word alloc_length; //Allocation length!
	word ret_len; //Returned length of possible data!
	byte starting_track;
	byte format;
	sword toc_length = 0;
	byte transfer_req;
	uint_32 endLBA; //When does the LBA addressing end!
	byte spinresponse;
	byte startM, startS, startF, endM, endS, endF; //Start/End MSF of an audio play operation!
	byte curtrack_nr;
	byte tracktype;

	//Our own stuff!
	ATAPI_aborted = 0; //Init aborted status!
	byte abortreason = 5; //Error cause is no disk inserted? Default to 5&additional sense code 0x20 for invalid command.
	byte additionalsensecode = 0; //Invalid command operation code.
	byte ascq; //extra code!
	byte isvalidpage = 0; //Valid page?
	uint_32 packet_datapos;
	byte i;
	uint_32 disk_size,LBA;
	disk_size = ATA[channel].Drive[drive].ATAPI_disksize; //Disk size in 4096 byte sectors!
	ATA_STATUSREGISTER_DRIVESEEKCOMPLETEW(channel, drive, 0); //No service(when enabled), nor drive seek complete!
	ascq = 0; //Default!
	switch (ATA[channel].Drive[drive].ATAPI_PACKET[0]) //What command?
	{
	case 0x00: //TEST UNIT READY(Mandatory)?
		if ((spinresponse = ATAPI_common_spin_response(channel,drive,0,0))==1) //Common response OK?
		{
			if (!(is_mounted(ATA_Drives[channel][drive]) && ATA[channel].Drive[drive].diskInserted)) { abortreason = SENSE_NOT_READY; additionalsensecode = ASC_MEDIUM_NOT_PRESENT; ascq = (ATA[channel].Drive[drive].ATAPI_caddyejected ? 0x02 : 0x01); goto ATAPI_invalidcommand; } //Error out if not present!
			//Valid disk loaded?
			ATA[channel].Drive[drive].ATAPI_processingPACKET = 3; //Result phase!
			ATA[channel].Drive[drive].commandstatus = 0; //OK!
			ATA_STATUSREGISTER_ERRORW(channel, drive, 0); //Error bit is reset when a new command is received, as defined in the documentation!
			ATAPI_giveresultsize(channel,drive,0,1); //No result size!
		}
		else if (spinresponse == 2) //Busy waiting?
		{
			return; //Start busy waiting!
		}
		else //Report error!
		{
			ATAPI_command_reportError(channel,drive); //Report the error!
			ATAPI_aborted = 1; //We're aborted!
		}
		break;
	case 0x03: //REQUEST SENSE(Mandatory)?
		//Byte 4 = allocation length
		ATA[channel].Drive[drive].datapos = 0; //Start of data!
		ATA[channel].Drive[drive].datablock = MIN(ATA[channel].Drive[drive].ATAPI_PACKET[4],sizeof(ATA[channel].Drive[drive].SensePacket)); //Size of a block to transfer!
		ATA[channel].Drive[drive].datasize = 1; //How many blocks to transfer!

		//Now fill the packet with data!
		memcpy(&ATA[channel].Drive[drive].data, &ATA[channel].Drive[drive].SensePacket, ATA[channel].Drive[drive].datablock); //Give the result!
		if (ATA[channel].Drive[drive].SensePacket[2] == SENSE_UNIT_ATTENTION) //Unit attention?
		{
			ATAPI_SENSEPACKET_SENSEKEYW(channel, drive, SENSE_NONE); //No sense anymore!
		}

		if (ATA[channel].Drive[drive].ATAPI_diskchangepending) //Disk change pending? Doesn't matter if an IRQ has been given!
		{
			ATA[channel].Drive[drive].ATAPI_diskchangepending = 0; //Not pending anymore!
			ATAPI_SENSEPACKET_SENSEKEYW(channel,drive,SENSE_UNIT_ATTENTION); //Reason of the error
			ATAPI_SENSEPACKET_RESERVED2W(channel, drive, 0); //Reserved field!
			ATAPI_SENSEPACKET_ADDITIONALSENSECODEW(channel,drive,ASC_MEDIUM_MAY_HAVE_CHANGED); //Extended reason code
			ATAPI_SENSEPACKET_ASCQW(channel, drive, 0); //ASCQ also is cleared!
			ATAPI_SENSEPACKET_ILIW(channel,drive,0); //ILI bit cleared!
			ATAPI_SENSEPACKET_ERRORCODEW(channel,drive,0x70); //Default error code?
			ATAPI_SENSEPACKET_ADDITIONALSENSELENGTHW(channel,drive,10); //Additional Sense Length = 10?
			ATAPI_SENSEPACKET_INFORMATION0W(channel,drive,0); //No info!
			ATAPI_SENSEPACKET_INFORMATION1W(channel,drive,0); //No info!
			ATAPI_SENSEPACKET_INFORMATION2W(channel,drive,0); //No info!
			ATAPI_SENSEPACKET_INFORMATION3W(channel,drive,0); //No info!
			ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION1W(channel,drive,0); //No command specific information?
			ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION2W(channel,drive,0); //No command specific information?
			ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION3W(channel,drive,0); //No command specific information?
			ATAPI_SENSEPACKET_VALIDW(channel,drive,1); //We're valid!
			ATAPI_SENSEPACKET_CD(channel, drive, 0); //Error in the packet parameters!
		}

		//Leave the rest of the information cleared (unknown/unspecified)
		ATA[channel].Drive[drive].commandstatus = 1; //Transferring data IN!
		ATAPI_giveresultsize(channel,drive,ATA[channel].Drive[drive].datablock*ATA[channel].Drive[drive].datasize,1); //Result size, Raise an IRQ: we're needing attention!
		ATA[channel].Drive[drive].ATAPI_processingPACKET = 2; //We're transferring ATAPI data now!

		//Clear the condition!
		ATAPI_ERRORREGISTER_SENSEKEY(channel, drive, SENSE_NONE); //Signal an Unit Attention Sense key!
		ATAPI_ERRORREGISTER_ILI(channel, drive, 0); //No Illegal length indication!
		ATA_STATUSREGISTER_ERRORW(channel, drive, 0); //Error bit is reset when a new command is received, as defined in the documentation!
		break;
	case 0x12: //INQUIRY(Mandatory)?
		//We do succeed without media?
		//Byte 4 = allocation length
		ATA[channel].Drive[drive].datapos = 0; //Start of data!
		ATA[channel].Drive[drive].datablock = ATA[channel].Drive[drive].ATAPI_PACKET[4]; //Size of a block to transfer!
		ATA[channel].Drive[drive].datasize = 1; //How many blocks to transfer!
		memset(&ATA[channel].Drive[drive].data,0,ATA[channel].Drive[drive].datablock); //Clear the result!
		//Now fill the packet with data!
		ATA[channel].Drive[drive].data[0] = 0x05; //We're a CD-ROM drive!
		ATA[channel].Drive[drive].data[1] = 0x80; //We're always removable!
		ATA[channel].Drive[drive].data[3] = ((2<<4)|(1)); //We're ATAPI version 2(high nibble, from SFF-8020i documentation we're based on), response data format 1?
		ATA[channel].Drive[drive].data[4] = 31; //Amount of bytes following this byte for the full buffer? Total 36, so 31 more.
		strcpy_padded(&ATA[channel].Drive[drive].data[8],8,(byte *)"UniPCemu"); //Vendor ID
		memset(&CDROM_id, 0, sizeof(CDROM_id)); //Init!
		safescatnprintf(&CDROM_id[0], sizeof(CDROM_id), "CD-ROM%i", (ATA_Drives[channel][drive] == CDROM1) ? 1 : 0); //Autonumbering CD-ROM number!
		strcpy_padded(&ATA[channel].Drive[drive].data[16],16,(byte *)&CDROM_id[0]); //Product ID
		strcpy_padded(&ATA[channel].Drive[drive].data[32],4,&FIRMWARE[1][0]); //Product revision level
		//Leave the rest of the information cleared (unknown/unspecified)
		ATA[channel].Drive[drive].commandstatus = 1; //Transferring data IN!
		ATA_STATUSREGISTER_ERRORW(channel, drive, 0); //Error bit is reset when a new command is received, as defined in the documentation!
		ATAPI_giveresultsize(channel,drive,ATA[channel].Drive[drive].datablock*ATA[channel].Drive[drive].datasize,1); //Result size, Raise an IRQ: we're needing attention!
		ATA[channel].Drive[drive].ATAPI_processingPACKET = 2; //We're transferring ATAPI data now!
		break;
	case 0x55: //MODE SELECT(10)(Mandatory)?
		//Byte 4 = allocation length
		ATA[channel].Drive[drive].datapos = 0; //Start of data!
		ATA[channel].Drive[drive].datablock = (ATA[channel].Drive[drive].ATAPI_PACKET[7]<<8)|ATA[channel].Drive[drive].ATAPI_PACKET[8]; //Size of a block to transfer!
		ATA[channel].Drive[drive].datasize = 1; //How many blocks to transfer!
		memset(&ATA[channel].Drive[drive].data, 0, ATA[channel].Drive[drive].datablock); //Clear the result!
		//Leave the rest of the information cleared (unknown/unspecified)
		ATA[channel].Drive[drive].commandstatus = 2; //Transferring data OUT!
		ATAPI_giveresultsize(channel,drive,ATA[channel].Drive[drive].datablock*ATA[channel].Drive[drive].datasize,1); //Result size, Raise an IRQ: we're needing attention!
		ATA[channel].Drive[drive].ATAPI_processingPACKET = 2; //We're transferring ATAPI data now!
		ATA_STATUSREGISTER_ERRORW(channel, drive, 0); //Error bit is reset when a new command is received, as defined in the documentation!
		break;
	case 0x5A: //MODE SENSE(10)(Mandatory)?
		ATA[channel].Drive[drive].datapos = 0; //Start of data!
		ATA[channel].Drive[drive].datablock = (ATA[channel].Drive[drive].ATAPI_PACKET[7] << 8) | ATA[channel].Drive[drive].ATAPI_PACKET[8]; //Size of a block to transfer!
		ATA[channel].Drive[drive].datasize = 1; //How many blocks to transfer!
		ATAPI_giveresultsize(channel,drive,ATA[channel].Drive[drive].datablock*ATA[channel].Drive[drive].datasize,1); //Result size!
		memset(&ATA[channel].Drive[drive].data, 0, ATA[channel].Drive[drive].datablock); //Clear the result!
		//Leave the rest of the information cleared (unknown/unspecified)
		ATA[channel].Drive[drive].commandstatus = 1; //Transferring data IN for the result!
		ATAPI_setModePages(channel, drive); //Reset any ROM values to be properly reported!

		for (i=0;i<NUMITEMS(ATAPI_supportedmodepagecodes);i++) //Check all supported codes!
		{
			if (ATAPI_supportedmodepagecodes[i] == (ATA[channel].Drive[drive].ATAPI_PACKET[2]&0x3F)) //Page found in our page storage?
			{
				//Valid?
				ATA[channel].Drive[drive].datablock = MIN(ATA[channel].Drive[drive].datablock, ATAPI_supportedmodepagecodes_length[i]+8U); //Limit tothe maximum available length, with the header added to it!
				//if (ATAPI_supportedmodepagecodes_length[i]<=ATA[channel].Drive[drive].datablock) //Valid page size?
				{
					//Generate a header for the packet!
					ATA[channel].Drive[drive].data[0] = (ATAPI_supportedmodepagecodes_length[i] >> 8); //MSB of Side of data following the header!
					ATA[channel].Drive[drive].data[1] = (byte)ATAPI_supportedmodepagecodes_length[i]; //LSB of Size of the data following the header!

					//Disc in drive and type of said disc:
					if (ATA[channel].Drive[drive].ATAPI_caddyejected) //Caddy is ejected?
					{
						ATA[channel].Drive[drive].data[2] = 0x71; //Door open
					}
					else switch (ATA[channel].Drive[drive].PendingLoadingMode)
					{
					case LOAD_SPINDOWN: //Spinning down requested?
						if (!ATA[channel].Drive[drive].diskInserted) //Disc not inserted?
						{
							goto nodiscpresentmode;
						}
					case LOAD_DISC_LOADING:
					case LOAD_DISC_READIED:
					case LOAD_READY:
					case LOAD_IDLE:
						ATA[channel].Drive[drive].data[2] = 0x05; //Data CD inserted!
						break;
					default:
					case LOAD_NO_DISC: //No disc inserted?
					nodiscpresentmode: //No disc present mode?
						ATA[channel].Drive[drive].data[2] = 0x70; //Closed and no disc
						break;
					case LOAD_INSERT_CD: //Door open and inserting/removing disc?
					case LOAD_EJECTING: //Ejecting the disc tray?
						ATA[channel].Drive[drive].data[2] = 0x71; //Door open
						break;
					}

					memset(&ATA[channel].Drive[drive].data[3], 0, 5); //Remainder of the header is reserved, so clear it!

					//Generate the page itself!
					ATA[channel].Drive[drive].data[8] = ATAPI_supportedmodepagecodes[i]; //The page code and PS bit!
					ATA[channel].Drive[drive].data[9] = (byte)ATAPI_supportedmodepagecodes_length[i]; //Actual page length that's stored(which follows right after, either fully or partially)!
					switch (ATA[channel].Drive[drive].ATAPI_PACKET[2]>>6) //What kind of packet are we requesting?
					{
					case CDROM_PAGECONTROL_CHANGEABLE: //1 bits for all changable values?
						if (ATA[channel].Drive[drive].datablock >= 10) //Valid to give a data result?
						{
							for (packet_datapos = 0; packet_datapos < (ATA[channel].Drive[drive].datablock - 10); ++packet_datapos) //Process all our bits that are changable!
							{
								ATA[channel].Drive[drive].data[packet_datapos + 10] = ATA[channel].Drive[drive].ATAPI_SupportedMask[(ATAPI_supportedmodepagecodes[i] << 8) | packet_datapos]; //Give the raw mask we're using!
							}
						}
						break;
					case CDROM_PAGECONTROL_CURRENT: //Current values?
						if (ATA[channel].Drive[drive].datablock >= 10) //Valid to give a data result?
						{
							for (packet_datapos = 0; packet_datapos < (ATA[channel].Drive[drive].datablock - 10); ++packet_datapos) //Process all our bits that are changable!
							{
								ATA[channel].Drive[drive].data[packet_datapos + 10] = ATA[channel].Drive[drive].ATAPI_ModeData[(ATAPI_supportedmodepagecodes[i] << 8) | packet_datapos] & ATA[channel].Drive[drive].ATAPI_SupportedMask[(ATAPI_supportedmodepagecodes[i] << 8) | packet_datapos]; //Give the raw mask we're using!
							}
						}
						break;
					case CDROM_PAGECONTROL_DEFAULT: //Default values?
						if (ATA[channel].Drive[drive].datablock >= 10) //Valid to give a data result?
						{
							for (packet_datapos = 0; packet_datapos < (ATA[channel].Drive[drive].datablock - 10); ++packet_datapos) //Process all our bits that are changable!
							{
								ATA[channel].Drive[drive].data[packet_datapos + 10] = ATA[channel].Drive[drive].ATAPI_DefaultModeData[(ATAPI_supportedmodepagecodes[i] << 8) | packet_datapos] & ATA[channel].Drive[drive].ATAPI_SupportedMask[(ATAPI_supportedmodepagecodes[i] << 8) | packet_datapos]; //Give the raw mask we're using!
							}
						}
						break;
					case CDROM_PAGECONTROL_SAVED: //Currently saved values?
						abortreason = SENSE_ILLEGAL_REQUEST; //Illegal!
						additionalsensecode = ASC_SAVING_PARAMETERS_NOT_SUPPORTED; //Not supported!
						ascq = 0;
						goto ATAPI_invalidcommand; //Saved data isn't supported!
						break;
					default:
						break;
					}
					ATA_STATUSREGISTER_ERRORW(channel, drive, 0); //Error bit is reset when a new command is received, as defined in the documentation!
					isvalidpage = 1; //Were valid!
					ATA[channel].Drive[drive].ATAPI_processingPACKET = 2; //We're transferring ATAPI data now!
					ATAPI_giveresultsize(channel,drive,ATA[channel].Drive[drive].datablock*ATA[channel].Drive[drive].datasize,1); //No result size!
				}
				break; //Stop searching!
			}
		}
		if (isvalidpage==0) //Invalid page?
		{
			abortreason = SENSE_ILLEGAL_REQUEST; //Illegal!
			additionalsensecode = 26; //Not supported!
			ascq = 0; //Invalid field in CSB!
			goto ATAPI_invalidcommand; //Error out!
		}
		break;
	case 0x1E: //Prevent/Allow Medium Removal(Mandatory)?
		ATA[channel].Drive[drive].preventMediumRemoval = (ATA[channel].Drive[drive].preventMediumRemoval&~2)|((ATA[channel].Drive[drive].ATAPI_PACKET[4]&1)<<1); //Are we preventing the storage medium to be removed?
		ATA[channel].Drive[drive].ATAPI_processingPACKET = 3; //Result phase!
		ATA[channel].Drive[drive].commandstatus = 0; //New command can be specified!
		ATA_STATUSREGISTER_ERRORW(channel, drive, 0); //Error bit is reset when a new command is received, as defined in the documentation!
		ATAPI_giveresultsize(channel,drive,0,1); //No result size! Raise and interrupt to end the transfer after busy!
		break;
	case 0xBE: //Read CD command(mandatory)?
		if ((spinresponse = ATAPI_common_spin_response(channel, drive, 1, 1))==1)
		{
			if (!(is_mounted(ATA_Drives[channel][drive]) && ATA[channel].Drive[drive].diskInserted)) { abortreason = SENSE_NOT_READY; additionalsensecode = ASC_MEDIUM_NOT_PRESENT; ascq = (ATA[channel].Drive[drive].ATAPI_caddyejected ? 0x02 : 0x01); goto ATAPI_invalidcommand; } //Error out if not present!
			LBA = (((((ATA[channel].Drive[drive].ATAPI_PACKET[2] << 8) | ATA[channel].Drive[drive].ATAPI_PACKET[3]) << 8) | ATA[channel].Drive[drive].ATAPI_PACKET[4]) << 8) | ATA[channel].Drive[drive].ATAPI_PACKET[5]; //The LBA address!
			ATA[channel].Drive[drive].datasize = (((ATA[channel].Drive[drive].ATAPI_PACKET[6] << 8) | ATA[channel].Drive[drive].ATAPI_PACKET[7]) << 8) | ATA[channel].Drive[drive].ATAPI_PACKET[8]; //The amount of sectors to transfer!
			transfer_req = ATA[channel].Drive[drive].ATAPI_PACKET[9]; //Requested type of packets!
			if (!ATA[channel].Drive[drive].datasize) //Nothing to transfer?
			{
				//Execute NOP command!
			readCDNOP: //NOP for reading CD directly!
				ATA[channel].Drive[drive].ATAPI_processingPACKET = 3; //Result phase!
				ATA[channel].Drive[drive].commandstatus = 0; //New command can be specified!
				ATA_STATUSREGISTER_ERRORW(channel, drive, 0); //Error bit is reset when a new command is received, as defined in the documentation!
				ATAPI_giveresultsize(channel,drive, 0, 1); //Result size!
			}
			else //Normal processing!
			{
				if (!getCUEimage(ATA_Drives[channel][drive])) //Not a CUE image?
				{
					if ((LBA > disk_size) || ((LBA + MIN(ATA[channel].Drive[drive].datasize, 1) - 1) > disk_size)) { abortreason = SENSE_ILLEGAL_REQUEST; additionalsensecode = ASC_LOGICAL_BLOCK_OOR; ascq = 0; goto ATAPI_invalidcommand; } //Error out when invalid sector!
				}

				ATA[channel].Drive[drive].datapos = 0; //Start at the beginning properly!
				ATA[channel].Drive[drive].datablock = 0x800; //Default block size!

				ATA[channel].Drive[drive].expectedReadDataType = ((ATA[channel].Drive[drive].ATAPI_PACKET[1] & 0x1C) >> 2); //What type of sector are we expecting?

				switch (transfer_req & 0xF8) //What type to transfer?
				{
				case 0x00: goto readCDNOP; //Same as NOP!
				case 0xF8: ATA[channel].Drive[drive].datablock = 2352; //We're using CD direct packets! Different kind of format wrapper!
				case 0x10: //Normal 2KB sectors?
					ATA_STATUSREGISTER_ERRORW(channel, drive, 0); //Error bit is reset when a new command is received, as defined in the documentation!
					ATA[channel].Drive[drive].ATAPI_LBA = ATA[channel].Drive[drive].ATAPI_lastLBA = LBA; //The LBA to use!
					if (ATAPI_readsector(channel,drive)) //Sector read?
					{
						ATA_IRQ(channel, drive, ATAPI_FINISHREADYTIMING, 0); //Raise an IRQ: we're needing attention!
					}
					break;
				default: //Unknown request?
					abortreason = SENSE_ILLEGAL_REQUEST; //Error category!
					additionalsensecode = ASC_INV_FIELD_IN_CMD_PACKET; //Invalid Field in command packet!
					ascq = 0;
					ATA[channel].Drive[drive].ATAPI_processingPACKET = 3; //Result phase!
					ATAPI_giveresultsize(channel,drive, 0, 0); //Result size!
					goto ATAPI_invalidcommand;
				}
			}
		}
		else if (spinresponse == 2) //Busy waiting?
		{
			return; //Start busy waiting!
		}
		else //Report error!
		{
			ATAPI_command_reportError(channel, drive); //Report the error!
			ATAPI_aborted = 1; //We're aborted!
		}
		break;
	case 0xB9: //Read CD MSF (mandatory)?
		if ((spinresponse = ATAPI_common_spin_response(channel, drive, 1, 1))==1)
		{
			if (!(is_mounted(ATA_Drives[channel][drive]) && ATA[channel].Drive[drive].diskInserted)) { abortreason = SENSE_NOT_READY; additionalsensecode = ASC_MEDIUM_NOT_PRESENT; ascq = (ATA[channel].Drive[drive].ATAPI_caddyejected ? 0x02 : 0x01); goto ATAPI_invalidcommand; } //Error out if not present!
			LBA = MSF2LBAbin(ATA[channel].Drive[drive].ATAPI_PACKET[3], ATA[channel].Drive[drive].ATAPI_PACKET[4], ATA[channel].Drive[drive].ATAPI_PACKET[5]); //The LBA address!
			endLBA = MSF2LBAbin(ATA[channel].Drive[drive].ATAPI_PACKET[6], ATA[channel].Drive[drive].ATAPI_PACKET[7], ATA[channel].Drive[drive].ATAPI_PACKET[8]); //The LBA address!

			if (!getCUEimage(ATA_Drives[channel][drive])) //Not a CUE image?
			{
				if (LBA > endLBA) //LBA shall not be past the end!
				{
					abortreason = SENSE_ILLEGAL_REQUEST; //Error category!
					additionalsensecode = ASC_LOGICAL_BLOCK_OOR; //Invalid Field in command packet!
					ascq = 0;
					goto ATAPI_invalidcommand;
				}
			}

			ATA[channel].Drive[drive].datasize = (endLBA - LBA); //The amount of sectors to transfer! 0 is valid!
			transfer_req = ATA[channel].Drive[drive].ATAPI_PACKET[9]; //Requested type of packets!
			if (!ATA[channel].Drive[drive].datasize) //Nothing to transfer?
			{
				//Execute NOP command!
			readCDMSFNOP: //NOP for reading CD directly!
				ATA_STATUSREGISTER_ERRORW(channel, drive, 0); //Error bit is reset when a new command is received, as defined in the documentation!
				ATA[channel].Drive[drive].ATAPI_processingPACKET = 3; //Result phase!
				ATA[channel].Drive[drive].commandstatus = 0; //New command can be specified!
				ATAPI_giveresultsize(channel,drive, 0, 1); //No result size!
			}
			else //Normal processing!
			{
				ATA[channel].Drive[drive].datapos = 0; //Start at the beginning properly!
				ATA[channel].Drive[drive].datablock = 0x800; //Default block size!

				if (!getCUEimage(ATA_Drives[channel][drive])) //Not a CUE image?
				{
					if ((LBA > disk_size) || ((LBA + MIN(ATA[channel].Drive[drive].datasize, 1) - 1) > disk_size)) { abortreason = SENSE_ILLEGAL_REQUEST; additionalsensecode = ASC_LOGICAL_BLOCK_OOR; ascq = 0; goto ATAPI_invalidcommand; } //Error out when invalid sector!
				}

				ATA[channel].Drive[drive].expectedReadDataType = ((ATA[channel].Drive[drive].ATAPI_PACKET[1] & 0x1C) >> 2); //What type of sector are we expecting?

				switch (transfer_req & 0xF8) //What type to transfer?
				{
				case 0x00: goto readCDMSFNOP; //Same as NOP!
				case 0xF8: ATA[channel].Drive[drive].datablock = 2352; //We're using CD direct packets! Different kind of format wrapper!
				case 0x10: //Normal 2KB sectors?
					ATA[channel].Drive[drive].ATAPI_LBA = ATA[channel].Drive[drive].ATAPI_lastLBA = LBA; //The LBA to use!
					if (ATAPI_readsector(channel,drive)) //Sector read?
					{
						ATA_IRQ(channel, drive, ATAPI_FINISHREADYTIMING, 0); //Raise an IRQ: we're needing attention!
					}
					break;
				default: //Unknown request?
					abortreason = SENSE_ILLEGAL_REQUEST; //Error category!
					additionalsensecode = ASC_INV_FIELD_IN_CMD_PACKET; //Invalid Field in command packet!
					ascq = 0;
					goto ATAPI_invalidcommand;
				}
			}
		}
		else if (spinresponse == 2) //Busy waiting?
		{
			return; //Start busy waiting!
		}
		else //Report error!
		{
			ATAPI_command_reportError(channel, drive); //Report the error!
			ATAPI_aborted = 1; //We're aborted!
		}
		break;
	case 0x44: //Read header (mandatory)?
		if ((spinresponse = ATAPI_common_spin_response(channel, drive, 1, 1))==1)
		{
			if (!(is_mounted(ATA_Drives[channel][drive]) && ATA[channel].Drive[drive].diskInserted)) { abortreason = SENSE_NOT_READY; additionalsensecode = ASC_MEDIUM_NOT_PRESENT; ascq = (ATA[channel].Drive[drive].ATAPI_caddyejected ? 0x02 : 0x01); goto ATAPI_invalidcommand; } //Error out if not present!

			LBA = (((((ATA[channel].Drive[drive].ATAPI_PACKET[2] << 8) | ATA[channel].Drive[drive].ATAPI_PACKET[3]) << 8) | ATA[channel].Drive[drive].ATAPI_PACKET[4]) << 8) | ATA[channel].Drive[drive].ATAPI_PACKET[5]; //The LBA address!
			alloc_length = (ATA[channel].Drive[drive].ATAPI_PACKET[7] << 8) | (ATA[channel].Drive[drive].ATAPI_PACKET[8]); //Allocated length!
			//[9]=Amount of sectors, [2-5]=LBA address, LBA mid/high=2048.

			if (!getCUEimage(ATA_Drives[channel][drive])) //Not a CUE image?
			{
				if (LBA > disk_size) { abortreason = SENSE_ILLEGAL_REQUEST; additionalsensecode = ASC_LOGICAL_BLOCK_OOR; ascq = 0; goto ATAPI_invalidcommand; } //Error out when invalid sector!
			}

			//Now, build the packet!

			ret_len = 8; //Always try to return 8 bytes of data!

			memset(&ATA[channel].Drive[drive].data, 0, 8); //Clear all possible data!
			if (ATA[channel].Drive[drive].ATAPI_PACKET[1] & 2) //MSF packet requested?
			{
				ATA[channel].Drive[drive].data[0] = 1; //User data here! 2048 bytes, mode 1 sector!
				LBA2MSFbin(LBA, &ATA[channel].Drive[drive].data[5], &ATA[channel].Drive[drive].data[6], &ATA[channel].Drive[drive].data[7]); //Try and get the MSF address based on the LBA!
			}
			else //LBA packet requested?
			{
				ATA[channel].Drive[drive].data[0] = 1; //User data here! 2048 bytes, mode 1 sector!
				ATA[channel].Drive[drive].data[4] = (LBA >> 24) & 0xFF;
				ATA[channel].Drive[drive].data[5] = (LBA >> 16) & 0xFF;
				ATA[channel].Drive[drive].data[6] = (LBA >> 8) & 0xFF;
				ATA[channel].Drive[drive].data[7] = (LBA & 0xFF);
			}

			//Process the command normally!
			//Leave the rest of the information cleared (unknown/unspecified)
			ATA[channel].Drive[drive].datasize = 1; //One block to transfer!
			ATA[channel].Drive[drive].datapos = 0; //Start at the beginning properly!
			ATA[channel].Drive[drive].datablock = MIN(alloc_length, ret_len); //Give the smallest result, limit by allocation length!
			ATA[channel].Drive[drive].commandstatus = 1; //Transferring data IN for the result!
			ATA[channel].Drive[drive].ATAPI_processingPACKET = 2; //We're transferring ATAPI data now!
			ATA_STATUSREGISTER_ERRORW(channel, drive, 0); //Error bit is reset when a new command is received, as defined in the documentation!
			ATAPI_giveresultsize(channel,drive, ATA[channel].Drive[drive].datablock*ATA[channel].Drive[drive].datasize, 1); //Result size!
		}
		else if (spinresponse == 2) //Busy waiting?
		{
			return; //Start busy waiting!
		}
		else //Report error!
		{
			ATAPI_command_reportError(channel, drive); //Report the error!
			ATAPI_aborted = 1; //We're aborted!
		}
		break;
	case 0x42: //Read sub-channel (mandatory)?
		if ((spinresponse = ATAPI_common_spin_response(channel,drive,1,1))==1)
		{
			MSF = (ATA[channel].Drive[drive].ATAPI_PACKET[1]&2); //MSF bit!
			sub_Q = (ATA[channel].Drive[drive].ATAPI_PACKET[2] & 0x40); //SubQ bit!
			data_format = ATA[channel].Drive[drive].ATAPI_PACKET[3]; //Sub-channel Data Format
			alloc_length = (ATA[channel].Drive[drive].ATAPI_PACKET[7]<<8)|ATA[channel].Drive[drive].ATAPI_PACKET[8]; //Allocation length!
			ret_len = 4;
			if (!(is_mounted(ATA_Drives[channel][drive]) && ATA[channel].Drive[drive].diskInserted)) { abortreason = SENSE_NOT_READY; additionalsensecode = ASC_MEDIUM_NOT_PRESENT; ascq = (ATA[channel].Drive[drive].ATAPI_caddyejected ? 0x02 : 0x01); goto ATAPI_invalidcommand; } //Error out if not present!
			memset(&ATA[channel].Drive[drive].data,0,24); //Clear any and all data we might be using!
			ATA[channel].Drive[drive].data[0] = 0;
			ATA[channel].Drive[drive].data[1] = ATA[channel].Drive[drive].AUDIO_PLAYER.effectiveplaystatus; //Effective play status!
			if ((ATA[channel].Drive[drive].AUDIO_PLAYER.effectiveplaystatus == 0x13) || (ATA[channel].Drive[drive].AUDIO_PLAYER.effectiveplaystatus == 0x14))
			{
				ATA[channel].Drive[drive].AUDIO_PLAYER.effectiveplaystatus = PLAYER_STATUS_NONE; //Subsequent requests become 
			}
			ATA[channel].Drive[drive].data[2] = 0;
			ATA[channel].Drive[drive].data[3] = 0;
			if (sub_Q) //!sub_q==header only
			{
				if ((data_format==1) || (data_format==2) || (data_format==3)) //Current Position or UPC or ISRC
				{
					ret_len = 24;
					ATA[channel].Drive[drive].data[3] = 24-4; //Data length
					ATA[channel].Drive[drive].data[4] = data_format;
					ATA[channel].Drive[drive].data[8] = 0; //No MCval(format 2) or TCval(format 3)
					if (data_format == 1) //CD-ROM Current Position?
					{
						ret_len = 16;
						ATA[channel].Drive[drive].data[5] = ATA[channel].Drive[drive].lastformat; //During active audio playback, be a audio track, otherwise a data track.
						ATA[channel].Drive[drive].data[6] = ATA[channel].Drive[drive].lasttrack;
						if (getCUEimage(ATA_Drives[channel][drive])) //Supported? Report the current position!
						{
							if (MSF)
							{
								ATA[channel].Drive[drive].data[8] = 0;
								ATA[channel].Drive[drive].data[9] = ATA[channel].Drive[drive].lastM;
								ATA[channel].Drive[drive].data[10] = ATA[channel].Drive[drive].lastS;
								ATA[channel].Drive[drive].data[11] = ATA[channel].Drive[drive].lastF;
							}
							else
							{
								LBA = MSF2LBAbin(ATA[channel].Drive[drive].lastM, ATA[channel].Drive[drive].lastS, ATA[channel].Drive[drive].lastF);
								ATA[channel].Drive[drive].data[8] = ((LBA>>24)&0xFF);
								ATA[channel].Drive[drive].data[9] = ((LBA>>16)&0xFF);
								ATA[channel].Drive[drive].data[10] = ((LBA>>8)&0xFF);
								ATA[channel].Drive[drive].data[11] = (LBA&0xFF);
							}
							if (ATAPI_gettrackinfo(channel, drive, ATA[channel].Drive[drive].lastM, ATA[channel].Drive[drive].lastS, ATA[channel].Drive[drive].lastF, NULL, NULL, NULL, &startM, &startS, &startF, NULL) == 1) //What track information?
							{
								endLBA = MSF2LBAbin(ATA[channel].Drive[drive].lastM, ATA[channel].Drive[drive].lastS, ATA[channel].Drive[drive].lastF);
								LBA = MSF2LBAbin(startM, startS, startF); //Begin position of the track!
								endLBA -= LBA; //Relative track position!
								LBA2MSFbin(endLBA, &endM, &endS, &endF); //Get the relative track position as MSF!
								if (MSF)
								{
									ATA[channel].Drive[drive].data[12] = 0;
									ATA[channel].Drive[drive].data[13] = endM;
									ATA[channel].Drive[drive].data[14] = endS;
									ATA[channel].Drive[drive].data[15] = endF;
								}
								else
								{
									LBA = MSF2LBAbin(ATA[channel].Drive[drive].lastM, ATA[channel].Drive[drive].lastS, ATA[channel].Drive[drive].lastF);
									ATA[channel].Drive[drive].data[12] = ((endLBA >> 24) & 0xFF);
									ATA[channel].Drive[drive].data[13] = ((endLBA >> 16) & 0xFF);
									ATA[channel].Drive[drive].data[14] = ((endLBA >> 8) & 0xFF);
									ATA[channel].Drive[drive].data[15] = (endLBA & 0xFF);
								}
							}
							else //Couldn't get the track information required to handle this?
							{
								//Give MSF 00:00:00 or LBA 0.
								ATA[channel].Drive[drive].data[12] = 0;
								ATA[channel].Drive[drive].data[13] = 0;
								ATA[channel].Drive[drive].data[14] = 0;
								ATA[channel].Drive[drive].data[15] = 0;
							}
						}
						else //Not supported!
						{
							ATA[channel].Drive[drive].data[8] = 0; //No MCval(format 2) or TCval(format 3)
							ATA[channel].Drive[drive].data[9] = 0; //No MCval(format 2) or TCval(format 3)
							ATA[channel].Drive[drive].data[10] = 0; //No MCval(format 2) or TCval(format 3)
							ATA[channel].Drive[drive].data[11] = 0; //No MCval(format 2) or TCval(format 3)
							ATA[channel].Drive[drive].data[12] = 0; //No MCval(format 2) or TCval(format 3)
							ATA[channel].Drive[drive].data[13] = 0; //No MCval(format 2) or TCval(format 3)
							ATA[channel].Drive[drive].data[14] = 0; //No MCval(format 2) or TCval(format 3)
							ATA[channel].Drive[drive].data[15] = 0; //No MCval(format 2) or TCval(format 3)
						}
					}
				}
				else
				{
					abortreason = SENSE_ILLEGAL_REQUEST; //Error category!
					additionalsensecode = ASC_INV_FIELD_IN_CMD_PACKET; //Invalid Field in command packet!
					ascq = 0;
					goto ATAPI_invalidcommand;
				}
			}

			//Process the command normally!
			//Leave the rest of the information cleared (unknown/unspecified)
			ATA[channel].Drive[drive].datasize = 1; //One block to transfer!
			ATA[channel].Drive[drive].datapos = 0; //Start at the beginning properly!
			ATA[channel].Drive[drive].datablock = MIN(alloc_length,ret_len); //Give the smallest result, limit by allocation length!
			ATA[channel].Drive[drive].commandstatus = 1; //Transferring data IN for the result!
			ATA[channel].Drive[drive].ATAPI_processingPACKET = 2; //We're transferring ATAPI data now!
			ATA_STATUSREGISTER_ERRORW(channel, drive, 0); //Error bit is reset when a new command is received, as defined in the documentation!
			ATAPI_giveresultsize(channel,drive,ATA[channel].Drive[drive].datablock*ATA[channel].Drive[drive].datasize,1); //Result size!
		}
		else if (spinresponse == 2) //Busy waiting?
		{
			return; //Start busy waiting!
		}
		else //Report error!
		{
			ATAPI_command_reportError(channel,drive); //Report the error!
			ATAPI_aborted = 1; //We're aborted!
		}
		break;
	case 0x43: //Read TOC (mandatory)?
		if ((spinresponse = ATAPI_common_spin_response(channel,drive,1,1))==1)
		{
			if (!(is_mounted(ATA_Drives[channel][drive]) && ATA[channel].Drive[drive].diskInserted)) { abortreason = SENSE_NOT_READY; additionalsensecode = ASC_MEDIUM_NOT_PRESENT; ascq = (ATA[channel].Drive[drive].ATAPI_caddyejected ? 0x02 : 0x01); goto ATAPI_invalidcommand; } //Error out if not present!
			MSF = (ATA[channel].Drive[drive].ATAPI_PACKET[1]>>1)&1;
			starting_track = ATA[channel].Drive[drive].ATAPI_PACKET[6]; //Starting track!
			alloc_length = (ATA[channel].Drive[drive].ATAPI_PACKET[7]<<8)|(ATA[channel].Drive[drive].ATAPI_PACKET[8]); //Allocated length!
			format = (ATA[channel].Drive[drive].ATAPI_PACKET[9]>>6); //The format of the packet!
			switch (format)
			{
			case 0:
			case 1:
			case 2:
				if (!ATAPI_generateTOC(&ATA[channel].Drive[drive].data[0],&toc_length,MSF,starting_track,format,channel,drive))
				{
					goto invalidTOCrequest; //Invalid TOC request!
				}
				ATA[channel].Drive[drive].datapos = 0; //Init position for the transfer!
				ATA[channel].Drive[drive].datablock = MIN(toc_length,alloc_length); //Take the lesser length!
				ATA[channel].Drive[drive].datasize = 1; //One block to transfer!
				ATA[channel].Drive[drive].commandstatus = 1; //Transferring data IN for the result!
				ATA[channel].Drive[drive].ATAPI_processingPACKET = 2; //We're transferring ATAPI data now!
				ATA_STATUSREGISTER_ERRORW(channel, drive, 0); //Error bit is reset when a new command is received, as defined in the documentation!
				ATAPI_giveresultsize(channel,drive,ATA[channel].Drive[drive].datablock*ATA[channel].Drive[drive].datasize,1); //Result size!
				break;
			default:
				invalidTOCrequest:
				abortreason = SENSE_ILLEGAL_REQUEST; //Error category!
				additionalsensecode = ASC_INV_FIELD_IN_CMD_PACKET; //Invalid Field in command packet!
				ascq = 0;
				goto ATAPI_invalidcommand;
			}
		}
		else if (spinresponse == 2) //Busy waiting?
		{
			return; //Start busy waiting!
		}
		else //Report error!
		{
			ATAPI_command_reportError(channel,drive); //Report the error!
			ATAPI_aborted = 1; //We're aborted!
		}
		break;
	case 0x2B: //Seek (Mandatory)?
		if ((spinresponse = ATAPI_common_spin_response(channel,drive,1,1))==1)
		{
			//Clear sense data
			if (!(is_mounted(ATA_Drives[channel][drive]) && ATA[channel].Drive[drive].diskInserted)) { abortreason = SENSE_NOT_READY; additionalsensecode = ASC_MEDIUM_NOT_PRESENT; ascq = (ATA[channel].Drive[drive].ATAPI_caddyejected ? 0x02 : 0x01); goto ATAPI_invalidcommand; } //Error out if not present!
			//[9]=Amount of sectors, [2-5]=LBA address, LBA mid/high=2048.
			LBA = (((((ATA[channel].Drive[drive].ATAPI_PACKET[2] << 8) | ATA[channel].Drive[drive].ATAPI_PACKET[3]) << 8) | ATA[channel].Drive[drive].ATAPI_PACKET[4]) << 8) | ATA[channel].Drive[drive].ATAPI_PACKET[5]; //The LBA address!

			if (ATA[channel].Drive[drive].AUDIO_PLAYER.status != PLAYER_INITIALIZED) //Playing or scanning? Stop the player!
			{
				ATA[channel].Drive[drive].AUDIO_PLAYER.effectiveplaystatus = PLAYER_STATUS_FINISHED; //We're finished!
				ATA[channel].Drive[drive].AUDIO_PLAYER.status = PLAYER_INITIALIZED; //Stop playing!
			}


			if (!getCUEimage(ATA_Drives[channel][drive])) //Not a CUE image?
			{
				if (LBA > disk_size) goto illegalseekaddress; //Illegal address?
				LBA2MSFbin(LBA, &Mseek, &Sseek, &Fseek); //Convert to MSF address!
				ATA[channel].Drive[drive].lastformat = 0x14; //Last format: data track!
				ATA[channel].Drive[drive].lasttrack = 1; //Last track!
				ATA[channel].Drive[drive].lastM = Mseek; //Last address!
				ATA[channel].Drive[drive].lastS = Sseek; //Last address!
				ATA[channel].Drive[drive].lastF = Fseek; //Last address!
			}
			else
			{
					CDROM_selecttrack(ATA_Drives[channel][drive],0); //All tracks!
					CDROM_selectsubtrack(ATA_Drives[channel][drive],0); //All subtracks!
					LBA2MSFbin(LBA,&Mseek,&Sseek,&Fseek); //Convert to MSF!

					if (ATAPI_gettrackinfo(channel, drive, Mseek, Sseek, Fseek, &curtrack_nr, NULL, NULL, &startM, &startS, &startF, &tracktype) == 1) //What track information?
					{
						ATA[channel].Drive[drive].lasttrack = curtrack_nr; //Last track!
						ATA[channel].Drive[drive].lastM = Mseek; //Last address!
						ATA[channel].Drive[drive].lastS = Sseek; //Last address!
						ATA[channel].Drive[drive].lastF = Fseek; //Last address!
						switch (tracktype)
						{
						case 1+MODE_AUDIO: //Audio track?
							ATA[channel].Drive[drive].lastformat = 0x14; //Last format: data track!
							break;
						case 1+MODE_MODE1DATA: //Data mode?
						case 1+MODE_MODEXA: //XA mode?
							ATA[channel].Drive[drive].lastformat = 0x14; //Last format: data track!
							break;
						default: //Unknown mode?
							ATA[channel].Drive[drive].lastformat = 0x00; //Last format: unknown track!
							break;
						}
					}
					else //Failed to find the track information, thus out of bounds?
					{
					illegalseekaddress:
						abortreason = SENSE_ILLEGAL_REQUEST;
						additionalsensecode = ASC_LOGICAL_BLOCK_OOR;
						ascq = 0;
						goto ATAPI_invalidcommand;
					}
			} //Error out when invalid sector!

			ATA_STATUSREGISTER_DRIVESEEKCOMPLETEW(channel,drive,0); //Seek complete! Are we supposed to set it here?

			//Position the CD-ROM idea of LBA location!
			ATA[channel].Drive[drive].ATAPI_lastLBA = LBA; //We are positioned here now!

			//Save the Seeked LBA somewhere? Currently unused?

			ATA[channel].Drive[drive].ATAPI_processingPACKET = 3; //Result phase!
			ATA[channel].Drive[drive].commandstatus = 0; //New command can be specified!
			ATA_STATUSREGISTER_ERRORW(channel, drive, 0); //Error bit is reset when a new command is received, as defined in the documentation!
			ATAPI_giveresultsize(channel,drive,0,1); //No result size!
		}
		else if (spinresponse == 2) //Busy waiting?
		{
			return; //Start busy waiting!
		}
		else //Report error!
		{
			ATAPI_command_reportError(channel,drive); //Report the error!
			ATAPI_aborted = 1; //We're aborted!
		}
		break;
	/* Audio support */
	case 0x4E: //Stop play/scan (Mandatory)?
		//Simply ignore the command for now, as audio is unsupported?
		if (!(is_mounted(ATA_Drives[channel][drive]) && ATA[channel].Drive[drive].diskInserted)) { abortreason = SENSE_NOT_READY; additionalsensecode = ASC_MEDIUM_NOT_PRESENT; ascq = (ATA[channel].Drive[drive].ATAPI_caddyejected ? 0x02 : 0x01); goto ATAPI_invalidcommand; } //Error out if not present!

		//Issuing this command while scanning makes the play command continue. Issuing this command while paused shall stop the play command.
		if (ATA[channel].Drive[drive].AUDIO_PLAYER.status == PLAYER_PAUSED) //Paused?
		{
			ATA[channel].Drive[drive].AUDIO_PLAYER.effectiveplaystatus = PLAYER_STATUS_FINISHED; //We're finished!
			ATA[channel].Drive[drive].AUDIO_PLAYER.status = PLAYER_INITIALIZED; //Stop playing!
		}
		ATA[channel].Drive[drive].ATAPI_processingPACKET = 3; //Result phase!
		ATA[channel].Drive[drive].commandstatus = 0; //New command can be specified!
		ATA_STATUSREGISTER_ERRORW(channel, drive, 0); //Error bit is reset when a new command is received, as defined in the documentation!
		ATAPI_giveresultsize(channel, drive, 0, 1); //No result size!
		break;
	case 0x4B: //Pause/Resume (audio mandatory)?
		#if 0
		//This is as long as audio is unimplemented!
		#ifdef ATA_LOG
		dolog("ATAPI", "Executing unknown SCSI command: %02X", ATA[channel].Drive[drive].ATAPI_PACKET[0]); //Error: invalid command!
		#endif

		abortreason = SENSE_ILLEGAL_REQUEST; //Illegal request:
		additionalsensecode = ASC_ILLEGAL_OPCODE; //Illegal opcode!
		ascq = 0;

		goto ATAPI_invalidcommand; //See https://www.kernel.org/doc/htmldocs/libata/ataExceptions.html
		#endif
		if ((spinresponse = ATAPI_common_spin_response(channel, drive, 1, 1)) == 1)
		{
			if (!(is_mounted(ATA_Drives[channel][drive]) && ATA[channel].Drive[drive].diskInserted)) { abortreason = SENSE_NOT_READY; additionalsensecode = ASC_MEDIUM_NOT_PRESENT; ascq = (ATA[channel].Drive[drive].ATAPI_caddyejected ? 0x02 : 0x01); goto ATAPI_invalidcommand; } //Error out if not present!
			if (ATA[channel].Drive[drive].ATAPI_PACKET[8] & 1) //Resume?
			{
				//Resume if paused, otherwise, NOP!
				if (ATA[channel].Drive[drive].AUDIO_PLAYER.status == PLAYER_PAUSED) //Paused?
				{
					if (getCUEimage(ATA_Drives[channel][drive])) //Valid cue image?
					{
						ATA[channel].Drive[drive].AUDIO_PLAYER.effectiveplaystatus = PLAYER_STATUS_PLAYING_IN_PROGRESS; //We're finished!
						ATA[channel].Drive[drive].AUDIO_PLAYER.status = PLAYER_PLAYING; //Resume playing!
					}
				}
			}
			else //Pause?
			{
				//Pause if playing, otherwise, NOP!
				if (ATA[channel].Drive[drive].AUDIO_PLAYER.status == PLAYER_PLAYING) //Playing?
				{
					ATA[channel].Drive[drive].AUDIO_PLAYER.effectiveplaystatus = PLAYER_STATUS_PAUSED; //We're finished!
					ATA[channel].Drive[drive].AUDIO_PLAYER.status = PLAYER_PAUSED; //Pause playing!
				}
			}
			ATA[channel].Drive[drive].ATAPI_processingPACKET = 3; //Result phase!
			ATA[channel].Drive[drive].commandstatus = 0; //New command can be specified!
			ATA_STATUSREGISTER_ERRORW(channel, drive, 0); //Error bit is reset when a new command is received, as defined in the documentation!
			ATAPI_giveresultsize(channel, drive, 0, 1); //No result size!
		}
		else if (spinresponse == 2) //Busy waiting?
		{
			return; //Start busy waiting!
		}
		else //Report error!
		{
			ATAPI_command_reportError(channel, drive); //Report the error!
			ATAPI_aborted = 1; //We're aborted!
		}
		break;
	case 0x45: //Play audio (10) (audio mandatory)?
		#if 0
		//This is as long as audio is unimplemented!
		#ifdef ATA_LOG
		dolog("ATAPI", "Executing unknown SCSI command: %02X", ATA[channel].Drive[drive].ATAPI_PACKET[0]); //Error: invalid command!
		#endif

		abortreason = SENSE_ILLEGAL_REQUEST; //Illegal request:
		additionalsensecode = ASC_ILLEGAL_OPCODE; //Illegal opcode!
		ascq = 0;

		goto ATAPI_invalidcommand; //See https://www.kernel.org/doc/htmldocs/libata/ataExceptions.html
		#endif
		if ((spinresponse = ATAPI_common_spin_response(channel, drive, 1, 1)) == 1)
		{
			if (!(is_mounted(ATA_Drives[channel][drive]) && ATA[channel].Drive[drive].diskInserted)) { abortreason = SENSE_NOT_READY; additionalsensecode = ASC_MEDIUM_NOT_PRESENT; ascq = (ATA[channel].Drive[drive].ATAPI_caddyejected ? 0x02 : 0x01); goto ATAPI_invalidcommand; } //Error out if not present!

			LBA = ((((((ATA[channel].Drive[drive].ATAPI_PACKET[2] << 8) | (ATA[channel].Drive[drive].ATAPI_PACKET[3])) << 8) | (ATA[channel].Drive[drive].ATAPI_PACKET[4])) << 8) | (ATA[channel].Drive[drive].ATAPI_PACKET[5])); //Starting LBA address!
			alloc_length = ((ATA[channel].Drive[drive].ATAPI_PACKET[7] << 8) | (ATA[channel].Drive[drive].ATAPI_PACKET[8])); //Amount of frames to play (0 is valid, which means end MSF = start MSF)!
			//LBA FFFFFFFF=Current playback position, otherwise, add 150 for the MSF address(00:02:00). So pregap IS skipped with this one.
			//Add alloc_length to LBA for the finishing frame(same frame also counts and has priority over playing the frame)!
			//The SOTC bit and settings on page 0E(audio control page) is honoured.
			//Check the track type. If not an audio track, SENSE KEY: SENSE_ILLEGAL_REQUEST & ASCQ:ASC_ILLEGAL_MODE_FOR_THIS_TRACK_OR_INCOMPATIBLE_MEDIUM
			//If the media changes from audio to data, giving the error: SENSE KEY: SENSE_ILLEGAL_REQUEST & ASCQ: END_OF_USER_AREA_ENCOUNTERED_ON_THIS_TRACK
			if (LBA == 0xFFFFFFFF) //Current position?
			{
				//Take the current position we're at into startM, startS, startF!
				startM = ATA[channel].Drive[drive].AUDIO_PLAYER.M; //Start M!
				startS = ATA[channel].Drive[drive].AUDIO_PLAYER.S; //Start S!
				startF = ATA[channel].Drive[drive].AUDIO_PLAYER.F; //Start F!
			}
			else //Start time specified?
			{
				LBA2MSFbin(LBA, &startM, &startS, &startF); //Convert to MSF for playback!
			}
			//Generate the ending MSF!
			//Take the end position based on the start position!
			endLBA = MSF2LBAbin(startM, startS, startF); //Take the start position!
			endLBA += alloc_length; //How much to play, or none if the same!
			LBA2MSFbin(endLBA, &endM, &endS, &endF); //Where to stop playing, even on the same location as startM, startS, startF!
			//Start the playback operation with the startMSF and endMSF as beginning and end points, or stop when equal(no error)!
			if (ATAPI_audioplayer_startPlayback(channel, drive, startM, startS, startF, endM, endS, endF)) //Start playback in this range!
			{
				//Set DSC on completion!
				ATA_STATUSREGISTER_DRIVESEEKCOMPLETEW(channel, drive, 0); //Drive Seek Complete!
				ATA[channel].Drive[drive].ATAPI_processingPACKET = 3; //Result phase!
				ATA[channel].Drive[drive].commandstatus = 0; //New command can be specified!
				ATA_STATUSREGISTER_ERRORW(channel, drive, 0); //Error bit is reset when a new command is received, as defined in the documentation!
				ATAPI_giveresultsize(channel, drive, 0, 1); //No result size!
			}
			else //Otherwise, if errored out, abort!
			{
				ATAPI_aborted = 1; //We're aborted!
			}
		}
		else if (spinresponse == 2) //Busy waiting?
		{
			return; //Start busy waiting!
		}
		else //Report error!
		{
			ATAPI_command_reportError(channel, drive); //Report the error!
			ATAPI_aborted = 1; //We're aborted!
		}
		break;
	case 0x47: //Play audio MSF (audio mandatory)?
		#if 0
		//This is as long as audio is unimplemented!
		#ifdef ATA_LOG
		dolog("ATAPI", "Executing unknown SCSI command: %02X", ATA[channel].Drive[drive].ATAPI_PACKET[0]); //Error: invalid command!
		#endif

		abortreason = SENSE_ILLEGAL_REQUEST; //Illegal request:
		additionalsensecode = ASC_ILLEGAL_OPCODE; //Illegal opcode!
		ascq = 0;

		goto ATAPI_invalidcommand; //See https://www.kernel.org/doc/htmldocs/libata/ataExceptions.html
		#endif
		if ((spinresponse = ATAPI_common_spin_response(channel, drive, 1, 1)) == 1)
		{
			if (!(is_mounted(ATA_Drives[channel][drive]) && ATA[channel].Drive[drive].diskInserted)) { abortreason = SENSE_NOT_READY; additionalsensecode = ASC_MEDIUM_NOT_PRESENT; ascq = (ATA[channel].Drive[drive].ATAPI_caddyejected ? 0x02 : 0x01); goto ATAPI_invalidcommand; } //Error out if not present!
			startM = ATA[channel].Drive[drive].ATAPI_PACKET[3]; //Start M!
			startS = ATA[channel].Drive[drive].ATAPI_PACKET[4]; //Start S!
			startF = ATA[channel].Drive[drive].ATAPI_PACKET[5]; //Start F!
			endM = ATA[channel].Drive[drive].ATAPI_PACKET[6]; //End M!
			endS = ATA[channel].Drive[drive].ATAPI_PACKET[7]; //End S!
			endF = ATA[channel].Drive[drive].ATAPI_PACKET[8]; //End F!
			//The SOTC bit and settings on page 0E(audio control page) is honoured.

			if ((startM == 0xFF) && (startS == 0xFF) && (startF == 0xFF)) //Current position?
			{
				//Take the current position into startM, startS, startF!
				startM = ATA[channel].Drive[drive].AUDIO_PLAYER.M; //Start M!
				startS = ATA[channel].Drive[drive].AUDIO_PLAYER.S; //Start S!
				startF = ATA[channel].Drive[drive].AUDIO_PLAYER.F; //Start F!
			}
			else //Start time specified?
			{
				LBA = MSF2LBAbin(startM, startS, startF);
				LBA2MSFbin(LBA, &startM, &startS, &startF); //New time!
			}
			//Otherwise, start MM:SS:FF is already loaded!

			LBA = MSF2LBAbin(endM, endS, endF);
			LBA2MSFbin(LBA, &endM, &endS, &endF); //New time!

			if (MSF2LBAbin(startM, startS, startF) > MSF2LBAbin(endM, endS, endF)) //Check condition status of SENSE_ILLEGAL_REQUEST!
			{
				//Throw the error!
				ATAPI_SET_SENSE(channel, drive, SENSE_ILLEGAL_REQUEST, ASC_LOGICAL_BLOCK_OOR, 0x00, 0); //Medium is becoming available
				ATAPI_command_reportError(channel, drive);
				goto playAudioMSF_handleMSFpositionerror;
			}

			//Start the playback operation with the startMSF and endMSF as beginning and end points, or stop when equal(no error)!
			if (ATAPI_audioplayer_startPlayback(channel, drive, startM, startS, startF, endM, endS, endF)) //Start playback in this range!
			{
				//Set DSC on completion!
				ATA_STATUSREGISTER_DRIVESEEKCOMPLETEW(channel, drive, 0); //Drive Seek Complete!
				ATA[channel].Drive[drive].ATAPI_processingPACKET = 3; //Result phase!
				ATA[channel].Drive[drive].commandstatus = 0; //New command can be specified!
				ATA_STATUSREGISTER_ERRORW(channel, drive, 0); //Error bit is reset when a new command is received, as defined in the documentation!
				ATAPI_giveresultsize(channel, drive, 0, 1); //No result size!
			}
			else //Otherwise, if errored out, abort!
			{
				ATAPI_aborted = 1; //We're aborted!
			}
		}
		else if (spinresponse == 2) //Busy waiting?
		{
			return; //Start busy waiting!
		}
		else //Report error!
		{
			ATAPI_command_reportError(channel, drive); //Report the error!
			playAudioMSF_handleMSFpositionerror:
			ATAPI_aborted = 1; //We're aborted!
		}
		break;
	/* End of audio support */
	case 0x1B: //Start/stop unit(Mandatory)?
		switch (ATA[channel].Drive[drive].ATAPI_PACKET[4] & 3) //What kind of action to take?
		{
		case 0: //Stop the disc?
			switch (ATA[channel].Drive[drive].PendingLoadingMode) //What loading mode?
			{
			case LOAD_IDLE: /* disc is stationary, not spinning */
			case LOAD_NO_DISC: /* caddy inserted, not spinning, no disc */
			case LOAD_INSERT_CD: /* user is "inserting" the CD */
			case LOAD_DISC_LOADING: /* disc is "spinning up" */
			case LOAD_DISC_READIED: /* disc just "became ready" */
				break; //Don't do anything!
			case LOAD_SPINDOWN: /* disc is requested to spin down */
				if ((ATA[channel].Drive[drive].ATAPI_PACKET[1] & 1) == 0) //Waiting for completion to complete?
				{
					ATAPI_PendingExecuteCommand(channel, drive); //Start pending again four our wait time execution!
					return; //Wait for the drive to become idle first!
				}
			case LOAD_EJECTING: //Ejecting the disc?
				//Don't stop the disc if it's already stopping to spin (NOP)!
				break;
			case LOAD_READY: /* Disc is ready to be read and spinning! */
				ATA[channel].Drive[drive].PendingLoadingMode = LOAD_SPINDOWN; //Start becoming idle!
				ATA[channel].Drive[drive].PendingSpinType = ATAPI_SPINDOWN; //Spin down!
				ATA[channel].Drive[drive].ATAPI_diskchangeTimeout = ATAPI_SPINDOWNSTOP_TIMEOUT; //Timeout to spinup complete!
				ATA[channel].Drive[drive].ATAPI_diskchangeDirection = ATAPI_DYNAMICLOADINGPROCESS; //We're unchanged from now on!
				if ((ATA[channel].Drive[drive].ATAPI_PACKET[1] & 1) == 0) //Waiting for completion to complete?
				{
					ATAPI_PendingExecuteCommand(channel, drive); //Start pending again four our wait time execution!
					return;
				}
			}
			break;
		case 1: //Start the disc and read the TOC?
			if ((spinresponse = ATAPI_common_spin_response(channel, drive, 1, 1)) == 1) //Spinning up?
			{
				//OK! We're starting up or are started!
			}
			else if (spinresponse == 2) //Busy waiting?
			{
				if ((ATA[channel].Drive[drive].ATAPI_PACKET[1] & 1) == 0) //Waiting for completion to complete?
				{
					ATAPI_PendingExecuteCommand(channel, drive); //Start pending again four our wait time execution!
					return;
				}
			}
			else //Report error!
			{
				ATAPI_command_reportError(channel, drive); //Report the error!
				ATAPI_aborted = 1; //We're aborted!
			}
			break;
		case 2: //Eject the disc if possible?
			if (ATA[channel].Drive[drive].PendingLoadingMode == LOAD_EJECTING) //Busy ejecting?
			{
				goto handleBusyEjecting;
			}
			if (ATA_allowDiskChange(ATA_Drives[channel][drive],2)) //Do we allow the disc to be changed? Stop spinning if spinning!
			{
				switch (ATA[channel].Drive[drive].PendingLoadingMode)
				{
				case LOAD_SPINDOWN: /* disc is requested to spin down */
					if ((ATA[channel].Drive[drive].ATAPI_PACKET[1] & 1) == 0) //Waiting for completion to complete?
					{
						ATAPI_PendingExecuteCommand(channel, drive); //Start pending again four our wait time execution!
						return; //Wait for the drive to become idle first!
					}
					break;
				case LOAD_IDLE: /* disc is stationary, not spinning */
				case LOAD_NO_DISC: /* caddy inserted, not spinning, no disc */
				case LOAD_DISC_LOADING: /* disc is "spinning up" */
				case LOAD_DISC_READIED: /* disc just "became ready" */
				case LOAD_READY: /* Disc is ready to be read and spinning! */
					//What to do with the different loading modes?
					handleATAPIcaddyeject(channel, drive); //Handle the ejection of the caddy!
					if ((ATA[channel].Drive[drive].ATAPI_PACKET[1] & 1) == 0) //Waiting for completion to complete?
					{
						ATAPI_PendingExecuteCommand(channel, drive); //Start pending again four our wait time execution!
						return;
					}
					break; //Don't do anything!
				case LOAD_EJECTING: //Ejecting the disc?
					handleBusyEjecting: //Handle when busy ejecting!
					if ((ATA[channel].Drive[drive].ATAPI_diskchangeTimeout != (DOUBLE)0) &&
						(ATA[channel].Drive[drive].ATAPI_diskchangeDirection == ATAPI_DYNAMICLOADINGPROCESS)
						) //Still ejecting?
					{
						if ((ATA[channel].Drive[drive].ATAPI_PACKET[1] & 1) == 0) //Waiting for completion to complete?
						{
							ATAPI_PendingExecuteCommand(channel, drive); //Start pending again four our wait time execution!
							return;
						}
					}
					break;
				case LOAD_INSERT_CD: /* user is "inserting" the CD */
					//Don't eject the disc if it's already ejected (NOP)!
					break;
				}
			}
			else //Not allowed to change?
			{
				abortreason = SENSE_NOT_READY; //Not ready!
				additionalsensecode = 0x53; //Media removal prevented!
				ascq = 0;
				goto ATAPI_invalidcommand; //Not ready, media removal prevented!
			}
			break;
		case 3: //Load the disc (Close tray)?
			if (ATA[channel].Drive[drive].ATAPI_caddyejected == 1) //Caddy is ejected?
			{
				ATA[channel].Drive[drive].ATAPI_caddyejected = 2; //Request the caddy to be inserted again!
				ATA[channel].Drive[drive].ATAPI_caddyinsertion_fast = 1; //Fast insertion!
			}
			if (ATA[channel].Drive[drive].ATAPI_caddyejected == 2) //Still ejected and waiting to be inserted?
			{
				if ((ATA[channel].Drive[drive].ATAPI_PACKET[1] & 1) == 0) //Waiting for completion to complete?
				{
					ATAPI_PendingExecuteCommand(channel, drive); //Start pending again four our wait time execution!
					return;
				}
			}
			else if (ATA[channel].Drive[drive].PendingLoadingMode == LOAD_INSERT_CD) //Still loading the CD-ROM into the drive?
			{
				if ((ATA[channel].Drive[drive].ATAPI_PACKET[1] & 1) == 0) //Waiting for completion to complete?
				{
					ATAPI_PendingExecuteCommand(channel, drive); //Start pending again four our wait time execution!
					return;
				}
			}
			break;
		default:
			break;
		}
		ATA[channel].Drive[drive].ATAPI_processingPACKET = 3; //Result phase!
		ATA[channel].Drive[drive].commandstatus = 0; //New command can be specified!
		ATA_STATUSREGISTER_ERRORW(channel, drive, 0); //Error bit is reset when a new command is received, as defined in the documentation!
		ATAPI_giveresultsize(channel,drive,0,1); //No result size!
		break;
	case 0x28: //Read sectors (10) command(Mandatory)?
	case 0xA8: //Read sectors (12) command(Mandatory)!
		if ((spinresponse = ATAPI_common_spin_response(channel,drive,1,1))==1)
		{
			if (!(is_mounted(ATA_Drives[channel][drive]) && ATA[channel].Drive[drive].diskInserted)) { abortreason = SENSE_NOT_READY; additionalsensecode = ASC_MEDIUM_NOT_PRESENT; ascq = (ATA[channel].Drive[drive].ATAPI_caddyejected ? 0x02 : 0x01); goto ATAPI_invalidcommand; } //Error out if not present!
			//[9]=Amount of sectors, [2-5]=LBA address, LBA mid/high=2048.
			LBA = (((((ATA[channel].Drive[drive].ATAPI_PACKET[2]<<8) | ATA[channel].Drive[drive].ATAPI_PACKET[3])<<8)| ATA[channel].Drive[drive].ATAPI_PACKET[4]) << 8)| ATA[channel].Drive[drive].ATAPI_PACKET[5]; //The LBA address!
			ATA[channel].Drive[drive].datasize = (ATA[channel].Drive[drive].ATAPI_PACKET[7]<<8)|(ATA[channel].Drive[drive].ATAPI_PACKET[8]); //How many sectors to transfer
			if (ATA[channel].Drive[drive].ATAPI_PACKET[0]==0xA8) //Extended sectors to transfer?
			{
				ATA[channel].Drive[drive].datasize = (ATA[channel].Drive[drive].ATAPI_PACKET[6]<<24) | (ATA[channel].Drive[drive].ATAPI_PACKET[7]<<16) | (ATA[channel].Drive[drive].ATAPI_PACKET[8] << 8) | (ATA[channel].Drive[drive].ATAPI_PACKET[9]); //How many sectors to transfer
			}

			if (!getCUEimage(ATA_Drives[channel][drive])) //Not a CUE image?
			{
				if ((LBA > disk_size) || ((LBA + MIN(ATA[channel].Drive[drive].datasize, 1) - 1) > disk_size)) { abortreason = SENSE_ILLEGAL_REQUEST; additionalsensecode = ASC_LOGICAL_BLOCK_OOR; ascq = 0; goto ATAPI_invalidcommand; } //Error out when invalid sector!
			}
		
			ATA[channel].Drive[drive].datapos = 0; //Start of data!
			ATA[channel].Drive[drive].datablock = 0x800; //We're refreshing after this many bytes! Use standard CD-ROM 2KB blocks!
			ATA[channel].Drive[drive].ATAPI_LBA = ATA[channel].Drive[drive].ATAPI_lastLBA = LBA; //The LBA to use!
			ATA[channel].Drive[drive].expectedReadDataType = 0xFF; //Any read sector(nn) type is allowed!
			ATA_STATUSREGISTER_ERRORW(channel, drive, 0); //Error bit is reset when a new command is received, as defined in the documentation!
			if (ATAPI_readsector(channel,drive)) //Sector read?
			{
				ATA_IRQ(channel,drive,ATAPI_FINISHREADYTIMING,0); //Raise an IRQ: we're needing attention!
			}
		}
		else if (spinresponse == 2) //Busy waiting?
		{
			return; //Start busy waiting!
		}
		else
		{
			ATAPI_command_reportError(channel,drive); //Report the error!
			ATAPI_aborted = 1; //We're aborted!
		}
		break;
	case 0x25: //Read CD-ROM capacity(Mandatory)?
		if ((spinresponse = ATAPI_common_spin_response(channel, drive, 1, 1))==1)
		{
			if (!(is_mounted(ATA_Drives[channel][drive]) && ATA[channel].Drive[drive].diskInserted)) { abortreason = SENSE_NOT_READY; additionalsensecode = ASC_MEDIUM_NOT_PRESENT; ascq = (ATA[channel].Drive[drive].ATAPI_caddyejected ? 0x02 : 0x01); goto ATAPI_invalidcommand; } //Error out if not present!
			ATA[channel].Drive[drive].datapos = 0; //Start of data!
			ATA[channel].Drive[drive].datablock = 8; //Size of a block of information to transfer!
			ATA[channel].Drive[drive].datasize = 1; //Number of blocks of information to transfer!
			ATA[channel].Drive[drive].data[0] = ((disk_size >> 24) & 0xFF);
			ATA[channel].Drive[drive].data[1] = ((disk_size >> 16) & 0xFF);
			ATA[channel].Drive[drive].data[2] = ((disk_size >> 8) & 0xFF);
			ATA[channel].Drive[drive].data[3] = (disk_size & 0xFF);
			ATA[channel].Drive[drive].data[4] = 0;
			ATA[channel].Drive[drive].data[5] = 0;
			ATA[channel].Drive[drive].data[6] = 8;
			ATA[channel].Drive[drive].data[7] = 0; //We're 4096 byte sectors!
			ATA[channel].Drive[drive].commandstatus = 1; //Transferring data IN!
			ATA[channel].Drive[drive].ATAPI_processingPACKET = 2; //We're transferring ATAPI data now!
			ATA_STATUSREGISTER_ERRORW(channel, drive, 0); //Error bit is reset when a new command is received, as defined in the documentation!
			ATAPI_giveresultsize(channel,drive, ATA[channel].Drive[drive].datablock*ATA[channel].Drive[drive].datasize, 1); //Result size!
		}
		else if (spinresponse == 2) //Busy waiting?
		{
			return; //Start busy waiting!
		}
		else
		{
			ATAPI_command_reportError(channel, drive); //Report the error!
			ATAPI_aborted = 1; //We're aborted!
		}
		break;
	case 0xBD: //Mechanism status(mandatory)
		ATA[channel].Drive[drive].datablock = MIN((ATA[channel].Drive[drive].ATAPI_PACKET[8] << 8) | (ATA[channel].Drive[drive].ATAPI_PACKET[9]),12); //How much data to transfer
		ATA[channel].Drive[drive].datapos = 0; //Start of data!
		ATA[channel].Drive[drive].datasize = 1; //Number of blocks of information to transfer!
		memset(&ATA[channel].Drive[drive].data,0,12); //Init data to zero!
		ATA[channel].Drive[drive].data[0] |= (0<<5); //Always ready!
		ATA[channel].Drive[drive].data[1] |= (0<<4)|(0<<5); //Bit4=door open, bit5-7=0:idle,1:playing,2:scanning,7:initializing
		//result 2-4=current LBA
		ATA[channel].Drive[drive].data[5] = 1; //number of slots available(5 bits)
		ATA[channel].Drive[drive].data[6] = 0; //Length of slot tables(msb)
		ATA[channel].Drive[drive].data[7] = 4; //Length of slot tables(lsb)
		//Slot table entry(size: 4 bytes)
		ATA[channel].Drive[drive].data[8] = (((is_mounted(ATA_Drives[channel][drive])&&ATA[channel].Drive[drive].diskInserted)?0x80:0x00)|(ATA[channel].Drive[drive].ATAPI_mediaChanged2?1:0)); //Bit0=disk changed(since last load), Bit 8=Disk present
		ATA[channel].Drive[drive].commandstatus = 1; //Transferring data IN!
		ATA[channel].Drive[drive].ATAPI_processingPACKET = 2; //We're transferring ATAPI data now!
		ATA_STATUSREGISTER_ERRORW(channel, drive, 0); //Error bit is reset when a new command is received, as defined in the documentation!
		ATAPI_giveresultsize(channel,drive,ATA[channel].Drive[drive].datablock*ATA[channel].Drive[drive].datasize,1); //Result size!
		break;
	default:
		#ifdef ATA_LOG
		dolog("ATAPI","Executing unknown SCSI command: %02X", ATA[channel].Drive[drive].ATAPI_PACKET[0]); //Error: invalid command!
		#endif

		abortreason = SENSE_ILLEGAL_REQUEST; //Illegal request:
		additionalsensecode = ASC_ILLEGAL_OPCODE; //Illegal opcode!
		ascq = 0;

		ATAPI_invalidcommand: //See https://www.kernel.org/doc/htmldocs/libata/ataExceptions.html
		ATA[channel].Drive[drive].ATAPI_processingPACKET = 3; //Result phase!
		ATA[channel].Drive[drive].commandstatus = 0xFF; //Move to error mode!
		ATAPI_giveresultsize(channel,drive,0,1); //No result size!
		ATA[channel].Drive[drive].ERRORREGISTER = /*4|*/(abortreason<<4); //Reset error register! This also contains a copy of the Sense Key!
		ATAPI_SENSEPACKET_SENSEKEYW(channel,drive,abortreason); //Reason of the error
		ATAPI_SENSEPACKET_RESERVED2W(channel, drive, 0); //Reserved field!
		ATAPI_SENSEPACKET_ADDITIONALSENSECODEW(channel,drive,additionalsensecode); //Extended reason code
		ATAPI_SENSEPACKET_ASCQW(channel, drive, ascq); //ASCQ code!
		ATAPI_SENSEPACKET_ILIW(channel,drive,0); //ILI bit cleared!
		ATAPI_SENSEPACKET_ERRORCODEW(channel,drive,0x70); //Default error code?
		ATAPI_SENSEPACKET_ADDITIONALSENSELENGTHW(channel,drive,10); //Additional Sense Length = 10?
		ATAPI_SENSEPACKET_INFORMATION0W(channel,drive,0); //No info!
		ATAPI_SENSEPACKET_INFORMATION1W(channel,drive,0); //No info!
		ATAPI_SENSEPACKET_INFORMATION2W(channel,drive,0); //No info!
		ATAPI_SENSEPACKET_INFORMATION3W(channel,drive,0); //No info!
		ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION0W(channel,drive,0); //No command specific information?
		ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION1W(channel,drive,0); //No command specific information?
		ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION2W(channel,drive,0); //No command specific information?
		ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION3W(channel,drive,0); //No command specific information?
		ATAPI_SENSEPACKET_VALIDW(channel,drive,1); //We're valid!
		ATAPI_SENSEPACKET_CD(channel,drive,0); //Error in the packet command itself!
		ATA[channel].Drive[drive].STATUSREGISTER = 0x40; //Clear status!
		ATA_STATUSREGISTER_DRIVEREADYW(channel,drive,1); //Ready!
		ATA_STATUSREGISTER_ERRORW(channel,drive,1); //Ready!
	//Reset of the status register is 0!
		ATAPI_aborted = 1; //We're aborted!
		break;
	}
}

OPTINLINE void giveATAPISignature(byte channel, byte drive)
{
	ATA[channel].Drive[drive].PARAMETERS.sectorcount = 0x01; //Sector count
	ATA[channel].Drive[drive].PARAMETERS.cylinderhigh = 0xEB; //LBA 16-23
	ATA[channel].Drive[drive].PARAMETERS.cylinderlow = 0x14; //LBA 8-15
	ATA[channel].Drive[drive].PARAMETERS.sectornumber = 0x01; //LBA 0-7
	ATA[channel].Drive[drive].PARAMETERS.drivehead = ((drive & 1) << 4); //Drive/Head register!
}

OPTINLINE void giveATASignature(byte channel, byte drive)
{
	ATA[channel].Drive[drive].PARAMETERS.sectorcount = 0x01;
	ATA[channel].Drive[drive].PARAMETERS.cylinderhigh = 0x00;
	ATA[channel].Drive[drive].PARAMETERS.cylinderlow = 0x00;
	ATA[channel].Drive[drive].PARAMETERS.sectornumber = 0x01;
	ATA[channel].Drive[drive].PARAMETERS.drivehead = ((drive & 1) << 4); //Drive/Head register!
}

OPTINLINE void giveSignature(byte channel, byte drive)
{
	if ((ATA_Drives[channel][drive] >= CDROM0)) //CD-ROM specified? Act according to the ATA/ATAPI-4 specification?
	{
		giveATAPISignature(channel,drive); //We're a CD-ROM, give ATAPI signature!
	}
	else if (ATA_Drives[channel][drive]) //Normal IDE harddrive(ATA-1)?
	{
		giveATASignature(channel,drive); //We're a harddisk, give ATA signature!
	}
	else //No drive?
	{
		ATA[channel].Drive[drive].PARAMETERS.sectorcount = 0x01;
		ATA[channel].Drive[drive].PARAMETERS.cylinderhigh = 0xFF;
		ATA[channel].Drive[drive].PARAMETERS.cylinderlow = 0xFF;
		ATA[channel].Drive[drive].PARAMETERS.sectornumber = 0x01;
		ATA[channel].Drive[drive].PARAMETERS.drivehead = ((drive&1) << 4); //Drive/Head register!
	}
}

void ATAPI_disableMediaStatusNotification(byte channel, byte drive)
{
	ATA[channel].Drive[drive].EnableMediaStatusNotification = 0; //Disable the status notification!
	ATA[channel].Drive[drive].preventMediumRemoval &= ~1; //Leave us in an unlocked state!
	ATA[channel].Drive[drive].allowDiskInsertion = 1; //Allow disk insertion always now?
	ATA[channel].Drive[drive].driveparams[86] &= ~0x10; //Media status notification has been disabled!
}

void ATA_reset(byte channel, byte slave)
{
	byte fullslaveinfo;
	fullslaveinfo = slave; //Complete slave info!
	slave &= 0x7F; //Are we a master or slave!
	if ((ATA_Drives[channel][slave]==0) || (ATA_Drives[channel][slave] >= CDROM0)) //CD-ROM style reset?
	{
		if (ATA_Drives[channel][slave] == 0) //Drive not present? NOP!
		{
			memset(&ATA[channel].Drive[slave].PARAMETERS, 0, sizeof(ATA[channel].Drive[slave].PARAMETERS)); //Clear the parameters for the OS to see!
			ATA[channel].Drive[slave].STATUSREGISTER = 0x40; //Report ready, no error?
			ATA[channel].Drive[slave].ERRORREGISTER = 0x01; //Clear the error register!
			ATA[channel].Drive[slave].PARAMETERS.reportReady = 0; //Report not ready?
			ATA_DRIVEHEAD_HEADW(channel, slave, 0); //What head?
			ATA_DRIVEHEAD_LBAMODE_2W(channel, slave, 0); //LBA mode?
			ATA[channel].Drive[slave].PARAMETERS.drivehead |= 0xA0; //Always 1!
			ATA[channel].Drive[slave].resetTiming = ATA_RESET_TIMEOUT; //How long to wait in reset!
			//Don't fill in a signature: we're not a valid drive anyways!
			giveSignature(channel, slave); //Give the signature!
			return; //Don't perform anything on the drive!
		}
		ATA[channel].Drive[slave].ERRORREGISTER = 0x01; //No error, but being a reserved value of 1 usually!
		if (fullslaveinfo & 0x80) //ATAPI reset?
		{
			ATA[channel].Drive[slave].PARAMETERS.reportReady = 0; //Report not ready now!
		}
		//Otherwise, Keep being ready if already enabled!
	}
	else //ATA-style reset?
	{
		ATA[channel].Drive[slave].ERRORREGISTER = 0x01; //No error, but being a reserved value of 1 usually!
		//Clear errors!
		ATA_STATUSREGISTER_ERRORW(channel, slave, 0); //Error bit is reset when a new command is received, as defined in the documentation!
		ATA[channel].Drive[slave].PARAMETERS.reportReady = 1; //Report ready now!
	}

	//Clear Drive/Head register, leaving the specified drive as it is!
	ATA_DRIVEHEAD_HEADW(channel,slave,0); //What head?
	ATA_DRIVEHEAD_LBAMODE_2W(channel,slave,0); //LBA mode?
	ATA[channel].Drive[slave].PARAMETERS.drivehead |= 0xA0; //Always 1!
	if ((ATA_Drives[channel][slave]<CDROM0) || (fullslaveinfo&0x80)) //Not a CD-ROM drive or ATAPI DEVICE RESET?
	{
		ATA[channel].Drive[slave].commandstatus = 3; //We're busy waiting! Reset to command mode afterwards!
		ATA[channel].Drive[slave].command = 0; //Full reset!
		ATA[channel].Drive[slave].ATAPI_processingPACKET = 0; //Not processing any packet!
	}
	ATA[channel].Drive[slave].resetTiming = ATA_RESET_TIMEOUT; //How long to wait in reset!
	if (ATA[channel].Drive[slave].resetSetsDefaults && (!(((fullslaveinfo & 0x80) == 0) && ATA_Drives[channel][slave]>=CDROM0))) //Allow resetting to defaults except SRST for CD-ROM drives?
	{
		ATA[channel].Drive[slave].multiplemode = 0; //Disable multiple mode!
		ATA[channel].Drive[slave].Enable8BitTransfers = 0; //Disable 8-bit transfers only!
		if (ATA_Drives[channel][slave] >= CDROM0) //ATAPI drive?
		{
			ATAPI_disableMediaStatusNotification(channel, slave); //Disable media status notification on resetting defaults!
			ATA[channel].Drive[slave].MediumChangeRequested = 0; //Disable any pending medium changes!
		}
	}
	giveSignature(channel, slave); //Give the signature!
	EMU_setDiskBusy(ATA_Drives[channel][slave], 0| (ATA[channel].Drive[slave].ATAPI_caddyejected << 1)); //We're not reading or writing anything anymore!

	//Bochs and Dosbox: Both SRST and ATAPI reset don't trigger an IRQ!
	ATA[channel].Drive[slave].resetTriggersIRQ = 0; //No IRQ on completion!
	if (is_mounted(ATA_Drives[channel][slave]) && ATA_Drives[channel][slave] && (ATA_Drives[channel][slave] < CDROM0)) //Mounted as non-CD-ROM?
		ATA_STATUSREGISTER_DRIVESEEKCOMPLETEW(channel, slave, 1); //Not seeking anymore, since we're ready to run!
}

OPTINLINE void ATA_executeCommand(byte channel, byte command) //Execute a command!
{
	uint_64 verifyaddr;
	uint_32 multiple=1;
#ifdef ATA_LOG
	dolog("ATA", "ExecuteCommand: %02X", command); //Execute this command!
#endif
	ATA[channel].Drive[ATA_activeDrive(channel)].longop = 0; //Default: no long operation!
	ATA[channel].Drive[ATA_activeDrive(channel)].multiplesectors = 0; //Multiple operation!
	int drive;
	byte temp;
	uint_32 disk_size; //For checking against boundaries!
	if (!(ATA_Drives[channel][ATA_activeDrive(channel)] >= CDROM0)) //Special action for non-CD-ROM drives?
	{
		ATA_STATUSREGISTER_ERRORW(channel, ATA_activeDrive(channel), 0); //Error bit is reset when a new command is received, as defined in the documentation!
		ATA[channel].Drive[ATA_activeDrive(channel)].ERRORREGISTER = 0x00; //Clear the error register according to make sure no error is left pending (according to Qemu)!
	}
	ATA_ERRORREGISTER_COMMANDABORTEDW(channel, ATA_activeDrive(channel), 0); //Error bit is reset when a new command is received, as defined in the documentation!
	switch (command) //What command?
	{
	case 0x90: //Execute drive diagnostic (Mandatory)?
#ifdef ATA_LOG
		dolog("ATA", "DIAGNOSTICS:%u,%u=%02X", channel, ATA_activeDrive(channel), command);
#endif
		ATA[channel].Drive[0].ERRORREGISTER = 0x1; //OK!
		ATA[channel].Drive[1].ERRORREGISTER = 0x1; //OK!

		if (ATA_Drives[channel][1]==0) //No second drive?
		{
			ATA[channel].Drive[1].ERRORREGISTER = 0x1; //Not detected!
		}

		ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus = 0; //Reset status!
		//Set the correct signature for detection!
		if (ATA_Drives[channel][0] >= CDROM0) //CD-ROM(ATAPI-4) specifies Signature? ATA-1 doesn't!
		{
			giveSignature(channel,0); //Give our signature!
		}
		if (ATA_Drives[channel][1] >= CDROM0) //CD-ROM(ATAPI-4) specifies Signature? ATA-1 doesn't!
		{
			giveSignature(channel,1); //Give our signature!
		}
		ATA_IRQ(channel, 0, ATA_FINISHREADYTIMING(205.0), 1); //IRQ from Master(after the slave has it's say with PDIAG communication), if selected!
		ATA_IRQ(channel, 1, ATA_FINISHREADYTIMING(200.0), 1); //IRQ from Slave, if selected!
		break;
	case 0xDB: //Acnowledge media change?
#ifdef ATA_LOG
		dolog("ATA", "ACNMEDIACHANGE:%u,%u=%02X", channel, ATA_activeDrive(channel), command);
#endif
		switch (ATA_Drives[channel][ATA_activeDrive(channel)]) //What kind of drive?
		{
		case CDROM0:
		case CDROM1: //CD-ROM?
			ATA_ERRORREGISTER_MEDIACHANGEDW(channel,ATA_activeDrive(channel),0); //Not changed anymore!
			ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus = 0; //Reset status!
			break;
		default:
			goto invalidcommand;
		}
		break;
	case 0x10:
	case 0x11:
	case 0x12:
	case 0x13:
	case 0x14:
	case 0x15:
	case 0x16:
	case 0x17:
	case 0x18:
	case 0x19:
	case 0x1A:
	case 0x1B:
	case 0x1C:
	case 0x1D:
	case 0x1E:
	case 0x1F: //Recalibrate?
#ifdef ATA_LOG
		dolog("ATA", "RECALIBRATE:%u,%u=%02X", channel, ATA_activeDrive(channel), command);
#endif
		if ((ATA_Drives[channel][ATA_activeDrive(channel)] >= CDROM0)) goto invalidcommand; //Special action for CD-ROM drives?
		ATA[channel].Drive[ATA_activeDrive(channel)].ERRORREGISTER = 0; //Default to no error!
		if (is_mounted(ATA_Drives[channel][ATA_activeDrive(channel)]) && (ATA_Drives[channel][ATA_activeDrive(channel)]>=HDD0) && (ATA_Drives[channel][ATA_activeDrive(channel)]<=HDD1)) //Gotten drive and is a hard disk?
		{
#ifdef ATA_LOG
			dolog("ATA", "Recalibrated!");
#endif
			temp = (ATA[channel].Drive[ATA_activeDrive(channel)].command & 0xF); //???
			ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.cylinderhigh = ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.cylinderlow = 0; //Clear cylinder #!
			ATA_STATUSREGISTER_DRIVESEEKCOMPLETEW(channel,ATA_activeDrive(channel),1); //We've completed seeking!
			ATA[channel].Drive[ATA_activeDrive(channel)].ERRORREGISTER = 0; //No error!
			ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus = 0; //Reset status!
			ATA_IRQ(channel, ATA_activeDrive(channel),ATA_FINISHREADYTIMING(200.0),1); //Raise the IRQ!
		}
		else
		{
			ATA_STATUSREGISTER_DRIVESEEKCOMPLETEW(channel,ATA_activeDrive(channel),0); //We've not completed seeking!
			ATA[channel].Drive[ATA_activeDrive(channel)].ERRORREGISTER = 0; //Track 0 couldn't be found!
			ATA_ERRORREGISTER_TRACK0NOTFOUNDW(channel,ATA_activeDrive(channel),1); //Track 0 couldn't be found!
			ATA_STATUSREGISTER_ERRORW(channel,ATA_activeDrive(channel),1); //Set error bit!
			ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus = 0xFF; //Error!
		}
		break;
	case 0x70:
	case 0x71:
	case 0x72:
	case 0x73:
	case 0x74:
	case 0x75:
	case 0x76:
	case 0x77:
	case 0x78:
	case 0x79:
	case 0x7A:
	case 0x7B:
	case 0x7C:
	case 0x7D:
	case 0x7E:
	case 0x7F: //Seek?
#ifdef ATA_LOG
		dolog("ATA", "SEEK:%u,%u=%02X", channel, ATA_activeDrive(channel), command);
#endif
		if ((ATA_Drives[channel][ATA_activeDrive(channel)] >= CDROM0)) goto invalidcommand; //Special action for CD-ROM drives?
		temp = (command & 0xF); //The head to select!
		if (((ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.cylinderhigh << 8) | ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.cylinderlow) < ATA[channel].Drive[ATA_activeDrive(channel)].driveparams[54]) //Cylinder correct?
		{
			if (temp < ATA[channel].Drive[ATA_activeDrive(channel)].driveparams[55]) //Head within range?
			{
#ifdef ATA_LOG
				dolog("ATA", "Seeked!");
#endif
				ATA[channel].Drive[ATA_activeDrive(channel)].STATUSREGISTER = 0; //No error!
				ATA_STATUSREGISTER_DRIVESEEKCOMPLETEW(channel,ATA_activeDrive(channel),1); //We've completed seeking!
				ATA[channel].Drive[ATA_activeDrive(channel)].ERRORREGISTER = 0; //No error!
				ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus = 0; //Reset status!
				ATA_DRIVEHEAD_HEADW(channel,ATA_activeDrive(channel),(command & 0xF)); //Select the following head!
				ATA_IRQ(channel, ATA_activeDrive(channel),ATA_FINISHREADYTIMING(200.0),1); //Raise the IRQ!
			}
			else goto invalidcommand; //Error out!
		}
		else goto invalidcommand; //Error out!
		break;
	case 0xC4: //Read multiple?
		if (ATA[channel].Drive[ATA_activeDrive(channel)].multiplemode==0) //Disabled?
		{
			goto invalidcommand; //Invalid command!
		}
		ATA[channel].Drive[ATA_activeDrive(channel)].multiplesectors = ATA[channel].Drive[ATA_activeDrive(channel)].multiplemode; //Multiple operation!
		goto readsectors; //Start the write sector command normally!
	case 0x22: //Read long (w/retry, ATAPI Mandatory)?
	case 0x23: //Read long (w/o retry, ATAPI Mandatory)?
		ATA[channel].Drive[ATA_activeDrive(channel)].longop = 1; //Long operation!
	case 0x20: //Read sector(s) (w/retry, ATAPI Mandatory)?
	case 0x21: //Read sector(s) (w/o retry, ATAPI Mandatory)?
#ifdef ATA_LOG
		dolog("ATA", "READ(long:%u):%u,%u=%02X", ATA[channel].longop,channel, ATA_activeDrive(channel), command);
#endif
		readsectors:
		if ((ATA_Drives[channel][ATA_activeDrive(channel)] >= CDROM0)) //Special action for CD-ROM drives?
		{
			//Enter reserved ATAPI result!
			giveSignature(channel,ATA_activeDrive(channel)); //Give our signature!
			goto invalidatedcommand; //Execute an invalid command result!
		}
		ATA[channel].Drive[ATA_activeDrive(channel)].datasize = ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.sectorcount; //Load sector count!
		if (!ATA[channel].Drive[ATA_activeDrive(channel)].datasize) ATA[channel].Drive[ATA_activeDrive(channel)].datasize = 0x100; //0 becomes 256!
		ATA_readLBACHS(channel); //Read the LBA/CHS address!
		ATA_STATUSREGISTER_DRIVESEEKCOMPLETEW(channel, ATA_activeDrive(channel), 1); //Seek complete!
		if (ATA_readsector(channel,command)) //OK?
		{
			ATA_IRQ(channel, ATA_activeDrive(channel),ATA_FINISHREADYTIMING(200.0),1); //Give our requesting IRQ!
		}
		break;
	case 0x40: //Read verify sector(s) (w/retry)?
	case 0x41: //Read verify sector(s) (w/o retry)?
		if ((ATA_Drives[channel][ATA_activeDrive(channel)] >= CDROM0)) goto invalidcommand; //Special action for CD-ROM drives?
		disk_size = ((ATA[channel].Drive[ATA_activeDrive(channel)].driveparams[61] << 16) | ATA[channel].Drive[ATA_activeDrive(channel)].driveparams[60]); //The size of the disk in sectors!
		ATA[channel].Drive[ATA_activeDrive(channel)].datasize = ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.sectorcount; //Load sector count!
		if (!ATA[channel].Drive[ATA_activeDrive(channel)].datasize) ATA[channel].Drive[ATA_activeDrive(channel)].datasize = 0x100; //0 becomes 256!
		ATA_readLBACHS(channel);
		//First, check if it's all within range!
		verifyaddr = ATA[channel].Drive[ATA_activeDrive(channel)].current_LBA_address; //Load the address!
		if (verifyaddr<=disk_size) //First sector OK?
		{
			verifyaddr += ATA[channel].Drive[ATA_activeDrive(channel)].datasize; //End address
			--verifyaddr; //Last accessed!
			if (verifyaddr>disk_size) //Not fully within range?
			{
				verifyaddr = disk_size; //Make within range!
				ATA[channel].Drive[ATA_activeDrive(channel)].current_LBA_address = (disk_size+1); //Faulting address!
				ATA[channel].Drive[ATA_activeDrive(channel)].datasize -= (uint_32)(verifyaddr-ATA[channel].Drive[ATA_activeDrive(channel)].current_LBA_address); //How much is left!
				ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.sectorcount = (ATA[channel].Drive[ATA_activeDrive(channel)].datasize&0xFF); //How many sectors are left is initialized!
				goto verify_outofrange;
			}
			else //Fully within range?
			{
				++verifyaddr; //Next sector!
				ATA[channel].Drive[ATA_activeDrive(channel)].current_LBA_address = (uint_32)verifyaddr; //Faulting address!
				ATA[channel].Drive[ATA_activeDrive(channel)].datasize = 0; //Nothing left!
				ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.sectorcount = (ATA[channel].Drive[ATA_activeDrive(channel)].datasize&0xFF); //How many sectors are left is initialized!
				ATA_writeLBACHS(channel); //Update the current sector!
			}
		}
		else //Out of range?
		{
			verify_outofrange:
			ATA[channel].Drive[ATA_activeDrive(channel)].ERRORREGISTER = 0; //Reset error register!
			ATA_ERRORREGISTER_IDMARKNOTFOUNDW(channel,ATA_activeDrive(channel),1); //Not found!
			ATA_STATUSREGISTER_ERRORW(channel,ATA_activeDrive(channel),1); //Error!
			ATA_writeLBACHS(channel); //Update the current sector!
			ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus = 0xFF; //Error!
		}
		if (!ATA_STATUSREGISTER_ERRORR(channel,ATA_activeDrive(channel))) //Finished OK?
		{
			ATA_IRQ(channel, ATA_activeDrive(channel),ATA_FINISHREADYTIMING(200.0),1); //Raise the OK IRQ!
		}
		break;
	case 0xC5: //Write multiple?
		if (ATA[channel].Drive[ATA_activeDrive(channel)].multiplemode==0) //Disabled?
		{
			goto invalidcommand; //Invalid command!
		}
		ATA[channel].Drive[ATA_activeDrive(channel)].multiplesectors = ATA[channel].Drive[ATA_activeDrive(channel)].multiplemode; //Multiple operation!
		goto writesectors; //Start the write sector command normally!
	case 0x32: //Write long (w/retry)?
	case 0x33: //Write long (w/o retry)?
		ATA[channel].Drive[ATA_activeDrive(channel)].longop = 1; //Long operation!
	case 0x30: //Write sector(s) (w/retry)?
	case 0x31: //Write sectors (w/o retry)?
		writesectors:
#ifdef ATA_LOG
		dolog("ATA", "WRITE(LONG:%u):%u,%u=%02X; Length=%02X", ATA[channel].longop, channel, ATA_activeDrive(channel), command, ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.sectorcount);
#endif
		if ((ATA_Drives[channel][ATA_activeDrive(channel)] >= CDROM0)) goto invalidcommand; //Special action for CD-ROM drives?
		ATA[channel].Drive[ATA_activeDrive(channel)].datasize = ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.sectorcount; //Load sector count!
		if (!ATA[channel].Drive[ATA_activeDrive(channel)].datasize) ATA[channel].Drive[ATA_activeDrive(channel)].datasize = 0x100; //0 becomes 256!
		ATA_readLBACHS(channel);
		ATA_IRQ(channel, ATA_activeDrive(channel),ATA_FINISHREADYTIMING(200.0),2); //Give our requesting IRQ! Just keep busy a bit then start the transfer phase!

		if (ATA[channel].Drive[ATA_activeDrive(channel)].multiplesectors) //Enabled multiple mode?
		{
			multiple = ATA[channel].Drive[ATA_activeDrive(channel)].multiplesectors; //Multiple sectors instead!
		}

		if (multiple>ATA[channel].Drive[ATA_activeDrive(channel)].datasize) //More than requested left?
		{
			multiple = ATA[channel].Drive[ATA_activeDrive(channel)].datasize; //Only take what's requested!
		}
		ATA[channel].Drive[ATA_activeDrive(channel)].multipletransferred = multiple; //How many have we transferred?

		ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus = 2; //Transferring data OUT!
		ATA[channel].Drive[ATA_activeDrive(channel)].datapos = 0; //Start at the beginning of the sector buffer!
		ATA[channel].Drive[ATA_activeDrive(channel)].datablock = 0x200*multiple; //We're writing 512 bytes to our output at a time!
		ATA[channel].Drive[ATA_activeDrive(channel)].command = command; //We're executing this command!
		EMU_setDiskBusy(ATA_Drives[channel][ATA_activeDrive(channel)], 2); //We're writing!
		ATA_STATUSREGISTER_DRIVESEEKCOMPLETEW(channel, ATA_activeDrive(channel), 1); //Seek complete!
		break;
	case 0x91: //Initialise device parameters?
#ifdef ATA_LOG
		dolog("ATA", "INITDRVPARAMS:%u,%u=%02X", channel, ATA_activeDrive(channel), command);
#endif
		if ((ATA_Drives[channel][ATA_activeDrive(channel)] >= CDROM0)) goto invalidcommand; //Special action for CD-ROM drives?
		ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus = 0; //Requesting command again!
		ATA[channel].Drive[ATA_activeDrive(channel)].driveparams[55] = (ATA_DRIVEHEAD_HEADR(channel,ATA_activeDrive(channel)) + 1); //Set the current maximum head!
		ATA[channel].Drive[ATA_activeDrive(channel)].driveparams[56] = (ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.sectorcount); //Set the current sectors per track!
		ATA_updateCapacity(channel,ATA_activeDrive(channel)); //Update the capacity!
		ATA[channel].Drive[ATA_activeDrive(channel)].ERRORREGISTER = 0; //No errors!
		ATA_IRQ(channel,ATA_activeDrive(channel),ATA_FINISHREADYTIMING(200.0),1); //Keep us busy for a bit, raise IRQ afterwards!
		break;
	case 0xA1: //ATAPI: IDENTIFY PACKET DEVICE (ATAPI Mandatory)!
		if ((ATA_Drives[channel][ATA_activeDrive(channel)]>=CDROM0) && ATA_Drives[channel][ATA_activeDrive(channel)]) //CDROM drive?
		{
			ATA[channel].Drive[ATA_activeDrive(channel)].command = 0xA1; //We're running this command!
			ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.reportReady = 1; //Report ready now!
			goto CDROMIDENTIFY; //Execute CDROM identification!
		}
		goto invalidcommand; //We're an invalid command: we're not a CDROM drive!
	case 0xEC: //Identify device (Mandatory)?
#ifdef ATA_LOG
		dolog("ATA", "IDENTIFY:%u,%u=%02X", channel, ATA_activeDrive(channel), command);
#endif
		if (!ATA_Drives[channel][ATA_activeDrive(channel)]) goto invalidcommand; //No drive errors out!
		if (ATA_Drives[channel][ATA_activeDrive(channel)] >= CDROM0) //Special action for CD-ROM drives?
		{
			//Enter reserved ATAPI result!
			giveSignature(channel,ATA_activeDrive(channel)); //Give our signature!
			goto invalidatedcommand; //Execute an invalid command result!
		}
		ATA[channel].Drive[ATA_activeDrive(channel)].command = 0xEC; //We're running this command!
		CDROMIDENTIFY:
		memcpy(&ATA[channel].Drive[ATA_activeDrive(channel)].data, &ATA[channel].Drive[ATA_activeDrive(channel)].driveparams, sizeof(ATA[channel].Drive[ATA_activeDrive(channel)].driveparams)); //Set drive parameters currently set!
		ATA[channel].Drive[ATA_activeDrive(channel)].STATUSREGISTER &= 0x10; //Clear any errors!
		ATA[channel].Drive[ATA_activeDrive(channel)].ERRORREGISTER = 0; //No errors!
		ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.cylinderlow = 0; //Needs to be 0 to detect!
		ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.cylinderhigh = 0; //Needs to be 0 to detect!
		if (ATA_Drives[channel][ATA_activeDrive(channel)] < CDROM0) //ATA harddisk?
		{
			ATA_STATUSREGISTER_DRIVESEEKCOMPLETEW(channel,ATA_activeDrive(channel),1); //Set Drive Seek Complete!
		}
		//Finish up!
		ATA[channel].Drive[ATA_activeDrive(channel)].datapos = 0; //Initialise data position for the result!
		ATA[channel].Drive[ATA_activeDrive(channel)].datablock = sizeof(ATA[channel].Drive[ATA_activeDrive(channel)].driveparams); //512 byte result!
		ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus = 1; //We're requesting data to be read!
		ATA_IRQ(channel, ATA_activeDrive(channel),ATA_FINISHREADYTIMING(200.0),1); //Execute an IRQ from us!
		break;
	case 0xA0: //ATAPI: PACKET (ATAPI mandatory)!
		if ((ATA_Drives[channel][ATA_activeDrive(channel)] < CDROM0) || !ATA_Drives[channel][ATA_activeDrive(channel)]) goto invalidcommand; //HDD/invalid disk errors out!
		ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.reportReady |= 2; //Report ready now after the command finishes executing! Not BUSY anymore!
		ATA[channel].Drive[ATA_activeDrive(channel)].command = 0xA0; //We're sending a ATAPI packet!
		ATA[channel].Drive[ATA_activeDrive(channel)].datapos = 0; //Initialise data position for the packet!
		ATA[channel].Drive[ATA_activeDrive(channel)].datablock = 12; //We're receiving 12 bytes for the ATAPI packet!
		ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus = 2; //We're requesting data to be written!
		ATA[channel].Drive[ATA_activeDrive(channel)].ATAPI_processingPACKET = 1; //We're processing an ATAPI/SCSI packet!
		//Packet doesn't raise an IRQ! Just Busy/DRQ is used here!
		ATAPI_giveresultsize(channel,ATA_activeDrive(channel),12,3); //We're entering a mini-Busy-result phase: don't raise an IRQ afterwards(according to )!
		break;
	case 0xDA: //Get media status?
#ifdef ATA_LOG
		dolog("ATA", "GETMEDIASTATUS:%u,%u=%02X", channel, ATA_activeDrive(channel), command);
#endif
		drive = ATA_Drives[channel][ATA_activeDrive(channel)]; //Load the drive identifier!
		if (ATA[channel].Drive[ATA_activeDrive(channel)].MediumChangeRequested || //Media change requested?
			ATA[channel].Drive[ATA_activeDrive(channel)].ATAPI_mediaChanged //Report media changd?
			) //Required to answer?
		{
			ATAPI_MEDIASTATUS_RSRVD(channel, ATA_activeDrive(channel), 0); //Reserved!
			ATAPI_MEDIASTATUS_RSRVD2(channel, ATA_activeDrive(channel), 0); //Reserved!
			ATAPI_MEDIASTATUS_RSRVD3(channel, ATA_activeDrive(channel), 0); //Reserved!
			ATAPI_MEDIASTATUS_RSRVD4(channel, ATA_activeDrive(channel), 0); //Reserved!
			ATAPI_MEDIASTATUS_NOMED(channel, ATA_activeDrive(channel), (is_mounted(drive) && (ATA[channel].Drive[ATA_activeDrive(channel)].ATAPI_caddyejected==0)) ? 0 : 1); //No media?
			ATAPI_MEDIASTATUS_MCR(channel, ATA_activeDrive(channel), ATA[channel].Drive[ATA_activeDrive(channel)].MediumChangeRequested); //Media change requests is handled by a combination of this module and the disk manager(which sets it on requests from the user)?
			ATAPI_MEDIASTATUS_MC(channel, ATA_activeDrive(channel), ATA[channel].Drive[ATA_activeDrive(channel)].ATAPI_mediaChanged); //Disk has been ejected/inserted?
			ATA[channel].Drive[ATA_activeDrive(channel)].ATAPI_mediaChanged = 0; //Only set this when the disk has actually changed(inserted/removed). Afterwards, clear it on next calls.
			ATA[channel].Drive[ATA_activeDrive(channel)].MediumChangeRequested = 0; //Requesting the medium to change is only reported once!
			if (is_mounted(drive)) //Drive inserted?
			{
				ATAPI_MEDIASTATUS_WT_PT(channel, ATA_activeDrive(channel), drivereadonly(drive)); //Are we read-only!
			}
			else
			{
				ATAPI_MEDIASTATUS_WT_PT(channel, ATA_activeDrive(channel), 0); //Are we read-only!
			}
			ATA_IRQ(channel, ATA_activeDrive(channel), ATAPI_FINISHREADYTIMING, 1); //Raise IRQ!
			ATA_STATUSREGISTER_ERRORW(channel, ATA_activeDrive(channel), 1); //Error bit is set to report our status!
			ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus = 0xFF; //Reset status: error command!
		}
		else if (ATA_Drives[channel][ATA_activeDrive(channel)]>=CDROM0) //CD-ROM drive?
		{
			ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus = 0; //Reset status: No error to report, since there's nothing to report!
			ATA_IRQ(channel, ATA_activeDrive(channel), ATAPI_FINISHREADYTIMING, 1); //Raise IRQ!
		}
		else //Invalid command?
		{
			goto invalidcommand; //Error out!
		}
		break;
	case 0xEF: //Set features (Mandatory)?
#ifdef ATA_LOG
		dolog("ATA", "Set features:%u,%u=%02X", channel, ATA_activeDrive(channel), ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.features); //Set these features!
#endif
		switch (ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.features) //What features to set?
		{
		case 0x01: //Enable 8-bit data transfers!
			ATA[channel].Drive[ATA_activeDrive(channel)].Enable8BitTransfers = 1; //Enable 8-bit transfers!
			break;
		case 0x81: //Disable 8-bit data transfers!
			ATA[channel].Drive[ATA_activeDrive(channel)].Enable8BitTransfers = 0; //Disable 8-bit transfers!
			break;
		case 0x03: //Set transfer mode!
			if ((ATA_Drives[channel][ATA_activeDrive(channel)] < CDROM0) || !ATA_Drives[channel][ATA_activeDrive(channel)]) goto invalidcommand; //HDD/invalid disk errors out!
			if (ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.sectorcount) //Invalid setting(only supporting default PIO)!
			{
				goto invalidcommand;
			}
			//Allow the default setting of 0:0(PIO Default Mode)! Don't store it!
			break;
		case 0x02: //Enable write cache!
			//OK! Ignore!
			break;
		case 0x82: //Disable write cache!
			//OK! Ignore!
			break;
		case 0x31: //Disable Media Status Notification
			if ((ATA_Drives[channel][ATA_activeDrive(channel)] < CDROM0) || !ATA_Drives[channel][ATA_activeDrive(channel)]) goto invalidcommand; //HDD/invalid disk errors out!
			ATAPI_disableMediaStatusNotification(channel, ATA_activeDrive(channel)); //Disable the media status notification!
			break;
		case 0x95: //Enable Media Status Notification
			if ((ATA_Drives[channel][ATA_activeDrive(channel)] < CDROM0) || !ATA_Drives[channel][ATA_activeDrive(channel)]) goto invalidcommand; //HDD/invalid disk errors out!
			ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.cylinderlow = 0; //Version 0!
			ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.cylinderhigh = (ATA[channel].Drive[ATA_activeDrive(channel)].EnableMediaStatusNotification?1:0); //Media Status Notification was enabled?
			if (ATA[channel].Drive[ATA_activeDrive(channel)].EnableMediaStatusNotification) //Already enabled?
			{
				ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.cylinderhigh |= 1; //Are we already enabled?
			}
			ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.cylinderhigh |= 2; //Are we lockable?
			ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.cylinderhigh |= 4; //Can we physically eject the media, in other words: are we locking the media and leaving the ejection mechanism to the OS(only set when not under software control, e.g. lever of floppy disk drives)?
			ATA[channel].Drive[ATA_activeDrive(channel)].EnableMediaStatusNotification = 1; //Enable the status notification(report medium change requests)!
			ATA[channel].Drive[ATA_activeDrive(channel)].preventMediumRemoval |= 1; //Prevent Medium Removal, to facilitate Medium Change Requests!
			ATA[channel].Drive[ATA_activeDrive(channel)].allowDiskInsertion = !is_mounted(ATA_Drives[channel][ATA_activeDrive(channel)]); //Allow disk insertion?
			ATA[channel].Drive[ATA_activeDrive(channel)].driveparams[86] |= 0x10; //Media status notification has been enabled!
			break;
		case 0x66: //Soft Reset will not change feature selections to power-up defaults?
			ATA[channel].Drive[ATA_activeDrive(channel)].resetSetsDefaults = 0; //Don't change to power-up defaults!
			break;
		case 0xCC: //Soft Reset will change feature selections to power-up defaults?
			ATA[channel].Drive[ATA_activeDrive(channel)].resetSetsDefaults = 1; //Change to defaults when reset!
			break;
		default: //Invalid feature!
#ifdef ATA_LOG
			dolog("ATA", "Invalid feature set: %02X", ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.features);
#endif
			goto invalidcommand; //Error out!
			break;
		}
		ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus = 0; //Reset command status!
		ATA[channel].Drive[ATA_activeDrive(channel)].STATUSREGISTER &= 0x10; //Reset data register, except DSC/Release!
		ATA_IRQ(channel, ATA_activeDrive(channel), ATA_FINISHREADYTIMING(1.0), 1); //Raise IRQ!
		break;
	case 0x00: //NOP (ATAPI Mandatory)?
		break;
	case 0x08: //DEVICE RESET(ATAPI Mandatory)?
		if (!(ATA_Drives[channel][ATA_activeDrive(channel)] >= CDROM0)) //ATA device? Unsupported!
		{
			#ifdef ATA_LOG
			dolog("ATA", "Invalid ATAPI on ATA drive command: %02X", command);
			#endif
			goto invalidcommand;
		}
		ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus = 0; //Reset command status!
		ATA_reset(channel,ATA_activeDrive(channel)|0x80); //Reset the channel's device!
		break;
	case 0xC6: //Set multiple mode?
		if (ATA_Drives[channel][ATA_activeDrive(channel)] >= CDROM0) //ATAPI device? Unsupported!
		{
			#ifdef ATA_LOG
			dolog("ATA", "Invalid ATAPI on ATA drive command: %02X", command);
			#endif
			goto invalidcommand;
		}
		if ((((uint_64)ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.sectorcount)<<9)>sizeof(ATA[channel].Drive[ATA_activeDrive(channel)].data)) //Not enough space to store the sectors? We're executing an invalid command result(invalid parameter)!
		{
			ATA[channel].Drive[ATA_activeDrive(channel)].multiplemode = 0; //Disable multiple mode, according to ATA-1!
			goto invalidcommand;
		}
		ATA[channel].Drive[ATA_activeDrive(channel)].multiplemode = ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.sectorcount; //Sector count register is used!

		ATA[channel].Drive[ATA_activeDrive(channel)].driveparams[59] = (ATA[channel].Drive[ATA_activeDrive(channel)].multiplemode?0x100:0)|(ATA[channel].Drive[ATA_activeDrive(channel)].multiplemode); //Current multiple sectors setting! Bit 8 is set when updated!
		ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus = 0; //Reset command status!
		ATA[channel].Drive[ATA_activeDrive(channel)].STATUSREGISTER &= 0x10; //Reset data register!
		ATA_IRQ(channel, ATA_activeDrive(channel), ATA_FINISHREADYTIMING(1.0), 1); //Raise IRQ!
		break;
	case 0xDC: //BIOS - post-boot?
	case 0xDD: //BIOS - pre-boot?
	case 0x50: //Format track?
	case 0x97:
	case 0xE3: //Idle?
	case 0x95:
	case 0xE1: //Idle immediate?
	case 0xE4: //Read buffer?
	case 0xC8: //Read DMA (w/retry)?
	case 0xC9: //Read DMA (w/o retry)?
	case 0x99:
	case 0xE6: //Sleep?
	case 0x96:
	case 0xE2: //Standby?
	case 0x94:
	case 0xE0: //Standby immediate?
	case 0xE8: //Write buffer?
	case 0xCA: //Write DMA (w/retry)?
	case 0xCB: //Write DMA (w/o retry)?
	case 0xE9: //Write same?
	case 0x3C: //Write verify?
	default: //Unknown command?
		//Invalid command?
		invalidcommand: //See https://www.kernel.org/doc/htmldocs/libata/ataExceptions.html
#ifdef ATA_LOG
		dolog("ATA", "INVALIDCOMMAND:%u,%u=%02X", channel, ATA_activeDrive(channel), command);
#endif
		invalidatedcommand:
		//Present ABRT error! BSY=0 in status, ERR=1 in status, ABRT(4) in error register.
		if (ATA_Drives[channel][ATA_activeDrive(channel)] >= CDROM0) //ATAPI device?
		{
			ATAPI_ERRORREGISTER_ABRT(channel, ATA_activeDrive(channel), 1); //Set ABRT!
		}
		else //ATA device?
		{
			ATA[channel].Drive[ATA_activeDrive(channel)].ERRORREGISTER = 4; //Reset error register!
		}
		ATA[channel].Drive[ATA_activeDrive(channel)].STATUSREGISTER &= 0x10; //Clear status!
		ATA_STATUSREGISTER_ERRORW(channel, ATA_activeDrive(channel), 1); //Error occurred: wee're executing an invalid command!
		ATA_STATUSREGISTER_DRIVEREADYW(channel,ATA_activeDrive(channel),1); //Ready!
		//Reset of the status register is 0!
		ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus = 0xFF; //Move to error mode!
		ATA_IRQ(channel, ATA_activeDrive(channel),ATA_FINISHREADYTIMING(0.0),0);
		break;
	}
}

OPTINLINE void ATA_updateStatus(byte channel)
{
	switch (ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus) //What command status?
	{
	case 0: //Ready for command?
		if (unlikely(ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.reportReady & 2)) //Pending becoming ready on commmand completion?
		{
			ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.reportReady = 1; //Now we're ready!
		}
		ATA_STATUSREGISTER_BUSYW(channel,ATA_activeDrive(channel),(((((ATA[channel].Drive[ATA_activeDrive(channel)].ATAPI_PendingExecuteTransfer && (ATA[channel].Drive[ATA_activeDrive(channel)].ATAPI_processingPACKET<3 /* 3(result)/4(pending result status) clear busy */)))||DRIVECONTROLREGISTER_SRSTR(channel)||(ATA[channel].Drive[ATA_activeDrive(channel)].resetTiming))?1:0) | (((ATA[channel].Drive[ATA_activeDrive(channel)].IRQTimeout && ATA[channel].Drive[ATA_activeDrive(channel)].IRQTimeout_busy) || ATA[channel].Drive[ATA_activeDrive(channel)].BusyTiming) ? 1 : 0))); //Not busy! You can write to the CBRs! We're busy during the ATAPI transfer still pending the result phase! Result phase pending doesn't set it!
		ATA_STATUSREGISTER_DRIVEREADYW(channel,ATA_activeDrive(channel),(((((ATA[channel].driveselectTiming||ATA[channel].Drive[ATA_activeDrive(channel)].ReadyTiming) && (ATA[channel].Drive[ATA_activeDrive(channel)].ATAPI_processingPACKET<4 /* 4(pending result status) sets ready */)))||DRIVECONTROLREGISTER_SRSTR(channel))?0:1)); //We're ready to process a command!
		ATA_STATUSREGISTER_DRIVEWRITEFAULTW(channel,ATA_activeDrive(channel),0); //No write fault!
		ATA_STATUSREGISTER_DATAREQUESTREADYW(channel,ATA_activeDrive(channel),0); //We're requesting data to transfer!
		if (ATA_Drives[channel][ATA_activeDrive(channel)] < CDROM0) //Hard disk?
		{
			ATA_STATUSREGISTER_ERRORW(channel, ATA_activeDrive(channel), 0); //No error!
		}
		if (ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.reportReady == 0) //Reporting non-existant?
		{
			ATA_STATUSREGISTER_INDEXW(channel, ATA_activeDrive(channel), 0); //Nothing!
			ATA_STATUSREGISTER_CORRECTEDDATAW(channel, ATA_activeDrive(channel), 0); //Nothing!
		}
		break;
	case 1: //Transferring data IN?
		ATA_STATUSREGISTER_BUSYW(channel,ATA_activeDrive(channel),((ATA[channel].Drive[ATA_activeDrive(channel)].ATAPI_PendingExecuteTransfer?1:0)) | (((ATA[channel].Drive[ATA_activeDrive(channel)].IRQTimeout && ATA[channel].Drive[ATA_activeDrive(channel)].IRQTimeout_busy) || ATA[channel].Drive[ATA_activeDrive(channel)].BusyTiming || (ATA[channel].Drive[ATA_activeDrive(channel)].resetTiming))?1:0)); //Not busy! You can write to the CBRs! We're busy when waiting.
		ATA_STATUSREGISTER_DRIVEREADYW(channel,ATA_activeDrive(channel),(((ATA[channel].driveselectTiming)||ATA_STATUSREGISTER_BUSYR(channel,ATA_activeDrive(channel)))?0:1)); //We're ready to process a command!
		ATA_STATUSREGISTER_DATAREQUESTREADYW(channel,ATA_activeDrive(channel),((ATA[channel].Drive[ATA_activeDrive(channel)].ATAPI_PendingExecuteTransfer||ATA_STATUSREGISTER_BUSYR(channel, ATA_activeDrive(channel)))?0:1)); //We're requesting data to transfer! Not transferring when waiting.
		break;
	case 2: //Transferring data OUT?
		ATA_STATUSREGISTER_BUSYW(channel,ATA_activeDrive(channel),(ATA[channel].Drive[ATA_activeDrive(channel)].ATAPI_PendingExecuteTransfer?1:0) | (((ATA[channel].Drive[ATA_activeDrive(channel)].IRQTimeout && ATA[channel].Drive[ATA_activeDrive(channel)].IRQTimeout_busy) || ATA[channel].Drive[ATA_activeDrive(channel)].BusyTiming || (ATA[channel].Drive[ATA_activeDrive(channel)].resetTiming)) ? 1 : 0)); //Not busy! You can write to the CBRs! We're busy when waiting.
		ATA_STATUSREGISTER_DRIVEREADYW(channel,ATA_activeDrive(channel),(((ATA[channel].driveselectTiming||ATA_STATUSREGISTER_BUSYR(channel,ATA_activeDrive(channel))))?0:1)); //We're ready to process a command!
		ATA_STATUSREGISTER_DATAREQUESTREADYW(channel,ATA_activeDrive(channel),((ATA[channel].Drive[ATA_activeDrive(channel)].ATAPI_PendingExecuteTransfer||ATA_STATUSREGISTER_BUSYR(channel, ATA_activeDrive(channel)))?0:1)); //We're requesting data to transfer! Not transferring when waiting.
		break;
	case 3: //Busy waiting?
		ATA_STATUSREGISTER_BUSYW(channel,ATA_activeDrive(channel),1); //Busy! You can write to the CBRs!
		ATA_STATUSREGISTER_DRIVEREADYW(channel,ATA_activeDrive(channel),0); //We're not ready to process a command!
		ATA_STATUSREGISTER_DATAREQUESTREADYW(channel,ATA_activeDrive(channel),0); //We're requesting data to transfer!
		break;
	default: //Unknown?
		ATA_STATUSREGISTER_ERRORW(channel,ATA_activeDrive(channel),1); //Error!
	case 0xFF: //Error? See https://www.kernel.org/doc/htmldocs/libata/ataExceptions.html
		if (unlikely(ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.reportReady & 2)) //Pending becoming ready on commmand completion?
		{
			ATA[channel].Drive[ATA_activeDrive(channel)].PARAMETERS.reportReady = 1; //Now we're ready!
		}
		ATA_STATUSREGISTER_BUSYW(channel,ATA_activeDrive(channel),0); //Error occurred: wee're executing an invalid command!
		ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus = 0; //Reset command status: we've reset!
		break;
	}
}

OPTINLINE void ATA_writedata(byte channel, byte value)
{
	if (!ATA_Drives[channel][ATA_activeDrive(channel)]) //Invalid drive?
	{
		return; //OK!
	}
	switch (ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus) //Current status?
	{
	case 2: //DATA OUT?
		ATA_dataOUT(channel,value); //Read data!
		break;
	default: //Unknown status?
		break;
	}
}

byte outATA16(word port, word value)
{
	byte channel = 0; //What channel?
	if ((port != getPORTaddress(channel)) || (!controller_enabled())) //Primary channel?
	{
		channel = 1; //Try secondary channel!
		if ((port != getPORTaddress(channel)) || (!controller_enabled())) //Secondary channel?
		{
			return 0; //Not our port?
		}
	}
	if (ATA[channel].Drive[ATA_activeDrive(channel)].ATAPI_processingPACKET!=1) //Not sending an ATAPI packet?
	{
		if (ATA[channel].Drive[ATA_activeDrive(channel)].Enable8BitTransfers) return 0; //We're only 8-bit data transfers!
	}
	if (!ATA_Drives[channel][ATA_activeDrive(channel)]) return 0; //Invalid drive!
	ATA_writedata(channel, (value&0xFF)); //Write the data low!
	ATA_writedata(channel, ((value >> 8) & 0xFF)); //Write the data high!
	return 1;
}

byte outATA32(word port, uint_32 value)
{
	byte channel = 0; //What channel?
	if ((port != getPORTaddress(channel)) || (!controller_enabled())) //Primary channel?
	{
		channel = 1; //Try secondary channel!
		if ((port != getPORTaddress(channel)) || (!controller_enabled())) //Secondary channel?
		{
			return 0; //Not our port?
		}
	}
	if (ATA[channel].Drive[ATA_activeDrive(channel)].ATAPI_processingPACKET!=1) //Not sending an ATAPI packet?
	{
		if (ATA[channel].Drive[ATA_activeDrive(channel)].Enable8BitTransfers) return 0; //We're only 8-bit data transfers!
	}
	if (!ATA_Drives[channel][ATA_activeDrive(channel)]) return 0; //Invalid drive!
	outATA16(port, (value&0xFFFF)); //Write the data low!
	outATA16(port, ((value >> 16) & 0xFFFF)); //Write the data high!
	return 1;
}

byte outATA8(word port, byte value)
{
	byte pendingreset = 0;
	ATA_channel = 0; //Init!
	if ((port<getPORTaddress(ATA_channel)) || (port>(getPORTaddress(ATA_channel) + 0x7)) || (!controller_enabled())) //Primary channel?
	{
		if ((port == ((getControlPORTaddress(ATA_channel))+2)) && controller_enabled()) goto port3_write;
		ATA_channel = 1; //Try secondary channel!
		if ((port<getPORTaddress(ATA_channel)) || (port>(getPORTaddress(ATA_channel) + 0x7)) || (!controller_enabled())) //Secondary channel?
		{
			if ((port == ((getControlPORTaddress(ATA_channel))+2)) && (controller_enabled())) goto port3_write;
			return 0; //Not our port?
		}
	}
	port -= getPORTaddress(ATA_channel); //Get the port from the base!
	ATA_slave = ATA[ATA_channel].activedrive; //Slave?
	if (!(ATA_Drives[ATA_channel][0] || ATA_Drives[ATA_channel][1])) //Invalid controller?
	{
		return 0; //Float the bus: nothing is connected!
	}
	switch (port) //What port?
	{
	case 0: //DATA?
		if (ATA_Drives[ATA_channel][ATA_slave]) //Enabled transfers?
		{
			ATA_writedata(ATA_channel, value); //Write the data!
			return 1; //We're enabled!
		}
		return 0; //We're non-existant!
		break;
	case 1: //Features?
#ifdef ATA_LOG
		dolog("ATA", "Feature register write: %02X %u.%u", value, ATA_channel, ATA_activeDrive(ATA_channel));
#endif
		ATA[ATA_channel].Drive[0].PARAMETERS.features = value; //Use the set data! Ignore!
		ATA[ATA_channel].Drive[1].PARAMETERS.features = value; //Use the set data! Ignore!
		return 1; //OK!
		break;
	case 2: //Sector count?
#ifdef ATA_LOG
		dolog("ATA", "Sector count write: %02X %u.%u", value, ATA_channel, ATA_activeDrive(ATA_channel));
#endif
		//if (!(ATA_Drives[ATA_channel][ATA_activeDrive(ATA_channel)] >= CDROM0)) //ATAPI device? Unsupported! Otherwise, supported and process!
		{
				ATA[ATA_channel].Drive[0].PARAMETERS.sectorcount = value; //Set sector count!
				ATA[ATA_channel].Drive[1].PARAMETERS.sectorcount = value; //Set sector count!
		}
		return 1; //OK!
		break;
	case 3: //Sector number?
#ifdef ATA_LOG
		dolog("ATA", "Sector number write: %02X %u.%u", value, ATA_channel, ATA_activeDrive(ATA_channel));
#endif
			ATA[ATA_channel].Drive[0].PARAMETERS.sectornumber = value; //Set sector number!
			ATA[ATA_channel].Drive[1].PARAMETERS.sectornumber = value; //Set sector number!
		return 1; //OK!
		break;
	case 4: //Cylinder low?
#ifdef ATA_LOG
		dolog("ATA", "Cylinder low write: %02X %u.%u", value, ATA_channel, ATA_activeDrive(ATA_channel));
#endif
			ATA[ATA_channel].Drive[0].PARAMETERS.cylinderlow = value; //Set cylinder low!
			ATA[ATA_channel].Drive[1].PARAMETERS.cylinderlow = value; //Set cylinder low!
		return 1; //OK!
		break;
	case 5: //Cylinder high?
#ifdef ATA_LOG
		dolog("ATA", "Cylinder high write: %02X %u.%u", value, ATA_channel, ATA_activeDrive(ATA_channel));
#endif
			ATA[ATA_channel].Drive[0].PARAMETERS.cylinderhigh = value; //Set cylinder high!
			ATA[ATA_channel].Drive[1].PARAMETERS.cylinderhigh = value; //Set cylinder high!
		return 1; //OK!
		break;
	case 6: //Drive/head?
#ifdef ATA_LOG
		dolog("ATA", "Drive/head write: %02X %u.%u", value, ATA_channel, ATA_activeDrive(ATA_channel));
#endif
		if (ATA[ATA_channel].activedrive!=((value>>4)&1)) //Changing drives between Master and Slave?
		{
			ATA[ATA_channel].driveselectTiming = ATA_DRIVESELECT_TIMEOUT; //Drive select timing to use!
		}
		ATA[ATA_channel].activedrive = (value >> 4) & 1; //The active drive!
		ATA[ATA_channel].Drive[0].PARAMETERS.drivehead = 0xA0; //Set drive head!
		ATA[ATA_channel].Drive[1].PARAMETERS.drivehead = 0xA0; //Set drive head!
		ATA[ATA_channel].Drive[0].PARAMETERS.drivehead |= (value&0x4F); //Set drive head and LBA mode!
		ATA[ATA_channel].Drive[1].PARAMETERS.drivehead |= ((value&0x4F)|0x10); //Set drive head and LBA mode!
		return 1; //OK!
		break;
	case 7: //Command?
		if (ATA_Drives[ATA_channel][ATA_activeDrive(ATA_channel)] == 0) //Invalid drive?
		{
			return 0; //Commands are ignored!
		}
		ATA_removeIRQ(ATA_channel,ATA_activeDrive(ATA_channel)); //Lower the IRQ by writes too, not just reads!
		ATA_executeCommand(ATA_channel,value); //Execute a command!
		return 1; //OK!
		break;
	default: //Unsupported!
		break;
	}
	return 0; //Safety!
port3_write: //Special port #3?
	port -= (getControlPORTaddress(ATA_channel)+2); //Get the port from the base!
	if (!(ATA_Drives[ATA_channel][0]|ATA_Drives[ATA_channel][1])) //Invalid controller?
	{
		return 0; //Ignore!
	}
	ATA_slave = ATA[ATA_channel].activedrive; //Slave?
	switch (port) //What port?
	{
	case 0: //Control register?
#ifdef ATA_LOG
		dolog("ATA", "Control register write: %02X %u.%u",value, ATA_channel, ATA_activeDrive(ATA_channel));
#endif
		if (DRIVECONTROLREGISTER_SRSTR(ATA_channel)==0) pendingreset = 1; //We're pending reset!
		ATA[ATA_channel].DriveControlRegister = value; //Set the data!
		if (DRIVECONTROLREGISTER_SRSTR(ATA_channel) && pendingreset) //Reset line raised?
		{
			//We cause all drives to reset on this channel!
			ATA_removeIRQ(ATA_channel,0); //Resetting lowers the IRQ when transitioning from 0 to 1!
			ATA_removeIRQ(ATA_channel,1); //Resetting lowers the IRQ when transitioning from 0 to 1!
			ATA_reset(ATA_channel,0); //Reset the specified channel Master!
			ATA_reset(ATA_channel,1); //Reset the specified channel Slave!
			ATA[ATA_channel].activedrive = 0; //Drive 0 becomes active!
		}
		return 1; //OK!
		break;
	default: //Unsupported!
		break;
	}
	return 0; //Unsupported!
}

OPTINLINE void ATA_readdata(byte channel, byte *result)
{
	if (!ATA_Drives[channel][ATA_activeDrive(channel)])
	{
		*result = 0; //No result!
		return; //Abort!
	}
	ATA_slave = ATA[ATA_channel].activedrive; //Slave?
	switch (ATA[channel].Drive[ATA_activeDrive(channel)].commandstatus) //Current status?
	{
	case 1: //DATA IN?
		*result = ATA_dataIN(channel); //Read data!
		break;
	default: //Unknown status?
		*result = 0; //Unsupported for now!
		break;
	}
}

byte inATA16(word port, word *result)
{
	byte channel = 0; //What channel?
	if ((port!=getPORTaddress(channel)) || (!controller_enabled())) //Primary channel?
	{
		channel = 1; //Try secondary channel!
		if ((port!=getPORTaddress(channel)) || (!controller_enabled())) //Secondary channel?
		{
			return 0; //Not our port?
		}
	}
	if (ATA_Drives[channel][ATA_activeDrive(channel)] == 0) return 0; //Invalid drive!
	if (ATA[channel].Drive[ATA_activeDrive(channel)].Enable8BitTransfers) return 0; //We're only 8-bit data transfers!
	byte buffer;
	word resultbuffer;
	buffer = 0x00; //Default for nothing read!
	ATA_readdata(channel, &buffer); //Read the low data!
	resultbuffer = buffer; //Load the low byte!
	buffer = 0x00; //Default for nothing read!
	ATA_readdata(channel, &buffer); //Read the high data!
	resultbuffer |= (buffer << 8); //Load the high byte!
	*result = resultbuffer; //Set the result!
	return 1;
}

byte inATA32(word port, uint_32 *result)
{
	byte channel = 0; //What channel?
	if ((port!=getPORTaddress(channel)) || (!controller_enabled())) //Primary channel?
	{
		channel = 1; //Try secondary channel!
		if ((port!=getPORTaddress(channel)) || (!controller_enabled())) //Secondary channel?
		{
			return 0; //Not our port?
		}
	}
	if (ATA_Drives[channel][ATA_activeDrive(channel)] == 0) return 0; //Invalid drive!
	if (ATA[channel].Drive[ATA_activeDrive(channel)].Enable8BitTransfers) return 0; //We're only 8-bit data transfers!
	word buffer;
	uint_32 resultbuffer;
	buffer = 0x0000; //Default for nothing read!
	inATA16(port, &buffer); //Read the low data!
	resultbuffer = buffer; //Load the low byte!
	buffer = 0x0000; //Default for nothing read!
	inATA16(port, &buffer); //Read the high data!
	resultbuffer |= (buffer << 16); //Load the high byte!
	*result = resultbuffer; //Set the result!
	return 1;
}

//Give the status register, masked if required!
byte ATA_maskStatus(byte result)
{
	if ((ATA[ATA_channel].Drive[ATA_slave].PARAMETERS.reportReady & 1) == 0) //Not ready yet (for ATAPI drives)?
	{
		switch (ATA[ATA_channel].Drive[ATA_slave].commandstatus)  //What command status?
		{
		case 0: //New command?
			return (result&~0x40); //DRDY off!
			break;
		case 1: //DATA IN
		case 2: //DATA OUT
			return (result&~0x40); //Only DRDY off!
			break;
		default: //No masking needed?
			//Don't mask BSY or DRDY!
			break;
		}
	}
	return result; //Give the status unmodified!
}

byte inATA8(word port, byte *result)
{
	ATA_channel = 0; //Init!
	if ((port<getPORTaddress(ATA_channel)) || (port>(getPORTaddress(ATA_channel) + 0x7)) || (!controller_enabled())) //Primary channel?
	{
		if ((port >= (getControlPORTaddress(ATA_channel)+2)) && (port <= (getControlPORTaddress(ATA_channel)+3)) && (controller_enabled())) goto port3_read;
		ATA_channel = 1; //Try secondary channel!
		if ((port<getPORTaddress(ATA_channel)) || (port>(getPORTaddress(ATA_channel) + 0x7)) || (!controller_enabled())) //Secondary channel?
		{
			if ((port >= (getControlPORTaddress(ATA_channel)+2)) && (port <= (getControlPORTaddress(ATA_channel)+3)) && controller_enabled()) goto port3_read;
			return 0; //Not our port?
		}
	}
	port -= getPORTaddress(ATA_channel); //Get the port from the base!
	ATA_slave = ATA[ATA_channel].activedrive; //Slave?
	if (((!ATA_Drives[ATA_channel][0]) && (!ATA_Drives[ATA_channel][1])) && (port<6)) //Invalid controller when no drives present for most registers?
	{
		*result = 0; //Nothing to store!
		return 1; //Float the bus: nothing is connected!
	}
	switch (port) //What port?
	{
	case 0: //DATA?
		if (ATA_Drives[ATA_channel][ATA_slave]) //Enabled transfers?
		{
			ATA_readdata(ATA_channel, result); //Read the data!
			return 1; //We're enabled!
		}
		return 0; //We're 16-bit only!
		break;
	case 1: //Error register?
		*result = ATA[ATA_channel].Drive[ATA_activeDrive(ATA_channel)].ERRORREGISTER; //Error register!
#ifdef ATA_LOG
		dolog("ATA", "Error register read: %02X %u.%u", *result, ATA_channel, ATA_activeDrive(ATA_channel));
#endif
		return 1;
		break;
	case 2: //Sector count?
		*result = ATA[ATA_channel].Drive[ATA_activeDrive(ATA_channel)].PARAMETERS.sectorcount; //Get sector count!
#ifdef ATA_LOG
		dolog("ATA", "Sector count register read: %02X %u.%u", *result, ATA_channel, ATA_activeDrive(ATA_channel));
#endif
		return 1;
		break;
	case 3: //Sector number?
		*result = ATA[ATA_channel].Drive[ATA_activeDrive(ATA_channel)].PARAMETERS.sectornumber; //Get sector number!
#ifdef ATA_LOG
		dolog("ATA", "Sector number register read: %02X %u.%u", *result, ATA_channel, ATA_activeDrive(ATA_channel));
#endif
		return 1; //OK!
		break;
	case 4: //Cylinder low?
		*result = ATA[ATA_channel].Drive[ATA_activeDrive(ATA_channel)].PARAMETERS.cylinderlow; //Get cylinder low!
#ifdef ATA_LOG
		dolog("ATA", "Cylinder low read: %02X %u.%u", *result, ATA_channel, ATA_activeDrive(ATA_channel));
#endif
		return 1; //OK!
		break;
	case 5: //Cylinder high?
		*result = ATA[ATA_channel].Drive[ATA_activeDrive(ATA_channel)].PARAMETERS.cylinderhigh; //Get cylinder high!
#ifdef ATA_LOG
		dolog("ATA", "Cylinder high read: %02X %u.%u", *result, ATA_channel, ATA_activeDrive(ATA_channel));
#endif
		return 1; //OK!
		break;
	case 6: //Drive/head?
		if (!(ATA_Drives[ATA_channel][0] || ATA_Drives[ATA_channel][1])) //Invalid controller?
		{
			return 0; //Float the bus: nothing is connected!
		}
		*result = ATA[ATA_channel].Drive[ATA_activeDrive(ATA_channel)].PARAMETERS.drivehead; //Get drive/head!
#ifdef ATA_LOG
		dolog("ATA", "Drive/head register read: %02X %u.%u", *result, ATA_channel, ATA_activeDrive(ATA_channel));
#endif
		return 1; //OK!
		break;
	case 7: //Status?
		if (!(ATA_Drives[ATA_channel][0] || ATA_Drives[ATA_channel][1])) //Invalid controller?
		{
			*result = 0x7F; //Busy is pulled down!
			return 1; //Float the bus: nothing is connected!
		}
		ATA_updateStatus(ATA_channel); //Update the status register if needed!
		ATA_removeIRQ(ATA_channel,ATA_activeDrive(ATA_channel)); //Acnowledge IRQ!
		*result = ATA_maskStatus(ATA[ATA_channel].Drive[ATA_activeDrive(ATA_channel)].STATUSREGISTER); //Get status!
		if (!(ATA_Drives[ATA_channel][ATA_activeDrive(ATA_channel)])) //Invalid drive?
		{
			*result = 0x00; //Nothing to report, according to documentation!
		}
		//Allow normal being ready for a command!
		ATA_STATUSREGISTER_DRIVEWRITEFAULTW(ATA_channel,ATA_activeDrive(ATA_channel),0); //Reset write fault flag!
#ifdef ATA_LOG
		dolog("ATA", "Status register read: %02X %u.%u", *result, ATA_channel, ATA_activeDrive(ATA_channel));
#endif
		return 1; //OK!
		break;
	default: //Unsupported?
		break;
	}
	return 0; //Unsupported!
port3_read: //Special port #3?
	port -= (getControlPORTaddress(ATA_channel)+2); //Get the port from the base!
	ATA_slave = ATA[ATA_channel].activedrive; //Slave?
	switch (port) //What port?
	{
	case 0: //Alternate status register?
		if (!(ATA_Drives[ATA_channel][0] || ATA_Drives[ATA_channel][1])) //Invalid controller?
		{
			return 0; //Float the bus: nothing is connected!
		}
		if (!ATA_Drives[ATA_channel][ATA_activeDrive(ATA_channel)]) //Invalid drive?
		{
			*result = 0; //Give 0: we're not present!
			return 1; //OK!
		}
		ATA_updateStatus(ATA_channel); //Update the status register if needed!
		*result = ATA_maskStatus(ATA[ATA_channel].Drive[ATA_activeDrive(ATA_channel)].STATUSREGISTER); //Get status!
#ifdef ATA_LOG
		dolog("ATA", "Alternate status register read: %02X %u.%u", *result, ATA_channel, ATA_activeDrive(ATA_channel));
#endif
		return 1; //OK!
		break;
	case 1: //Drive address register?
		if (activePCI_IDE!=&PCI_IDE) return 0; //Disable on i430fx hardware to prevent port conflicts!
		if (!(ATA_Drives[ATA_channel][0] || ATA_Drives[ATA_channel][1])) //Invalid controller?
		{
			return 0; //Float the bus: nothing is connected!
		}
		*result = (ATA[ATA_channel].DriveAddressRegister&0x7F); //Give the data, make sure we don't apply the flag shared with the Floppy Disk Controller!
#ifdef ATA_LOG
		dolog("ATA", "Drive address register read: %02X %u.%u", *result, ATA_channel, ATA_activeDrive(ATA_channel));
#endif
		return 1; //OK!
		break;
	default: //Unsupported!
		break;
	}
	return 0; //Unsupported!
}

void resetPCISpaceIDE()
{
	//Info from: http://wiki.osdev.org/PCI
	PCI_IDE.DeviceID = 1;
	PCI_IDE.VendorID = 1; //DEVICEID::VENDORID: We're a ATA device! This is only done with non-extended ATA controllers!
	PCI_IDE.ProgIF &= 0x8F; //We use our own set interrupts and we're a parallel ATA controller!
	PCI_IDE.ProgIF |= 0x8A; //Always set, indicating we're a ATA controller that's programmable!
	activePCI_IDE->ClassCode = 1; //We...
	activePCI_IDE->Subclass = 1; //Are an IDE controller
	activePCI_IDE->HeaderType = 0x00; //Normal header!
	activePCI_IDE->CacheLineSize = 0x00; //No cache supported!
	activePCI_IDE->InterruptLine = 0xFF; //What IRQ are we using?
}

extern byte PCI_transferring;

void ATA_ConfigurationSpaceChanged(uint_32 address, byte device, byte function, byte size)
{
	byte *addr;
	//Ignore device,function: we only have one!
	addr = (((byte *)activePCI_IDE)+address); //Actual update location?
	if (((addr<(byte *)&activePCI_IDE->BAR[0]) || (addr>=((byte *)&activePCI_IDE->BAR[5]+sizeof(PCI_IDE.BAR[5])))) && //Not a BAR?
		((addr < (byte*)&activePCI_IDE->ExpansionROMBaseAddress) || (addr >= ((byte*)&activePCI_IDE->ExpansionROMBaseAddress + sizeof(PCI_IDE.ExpansionROMBaseAddress)))) //Not the CIS pointer?
			) //Unsupported update to unsupported location?
	{
		switch (address) //What setting is changed?
		{
		case 0x9: //ProgIF? Programming Interface(ProgIF) byte in the PCI IDE controller specification Revision 1.0
			activePCI_IDE->ProgIF &= 5; //Bits 0 and 2 are programmable!
			activePCI_IDE->ProgIF |= 0x8A; //Bits that are always set! Bit 3 and 1 are always set, allowing for the primary and secondary bits(bits 0 and 2) to be programmable.
			activePCI_IDE->ProgIF &= ~0x80; //Don't allow IDE Bus Mastering to be set: we're not emulated!
			ATA[0].use_PCImode = (activePCI_IDE->ProgIF & 1); //Primary controller in PCI mode?
			ATA[1].use_PCImode = ((activePCI_IDE->ProgIF & 4) >> 2); //Secondary controller in PCI mode?
			ATA[1].use_PCImode |= (ATA[1].use_PCImode && (ATA[0].use_PCImode == 0)) ? 2 : 0; //Move secondary controller PCI mode to channel 0's settings when the primary is in compatiblity mode?
			break;
		default:
			if (is_i430fx == 0) //Not i430fx?
			{
				memset(addr, 0, 1); //Clear the set data!
			}
			break;
		}
	}
	else if (PCI_transferring==0) //Finished transferring data for an entry?
	{
		//Fix BAR reserved bits! The lower unchangable bits are the size of the BAR.
		activePCI_IDE->BAR[0] = ((activePCI_IDE->BAR[0]&((~7)&0xFFFFFFFFU))|1); //IO BAR! 8 bytes of IO space!
		activePCI_IDE->BAR[1] = ((activePCI_IDE->BAR[1]&((~3)&0xFFFFFFFFU))|1); //IO BAR! 4 bytes of IO space!
		activePCI_IDE->BAR[2] = ((activePCI_IDE->BAR[2]&((~7)&0xFFFFFFFFU))|1); //IO BAR! 8 bytes of IO space!
		activePCI_IDE->BAR[3] = ((activePCI_IDE->BAR[3]&((~3)&0xFFFFFFFFU))|1); //IO BAR! 4 bytes of IO space!
		activePCI_IDE->BAR[4] = ((activePCI_IDE->BAR[4]&((~0xF)&0xFFFFFFFFU))|1); //IO BAR! 8 bytes of IO space!
		activePCI_IDE->BAR[5] = ((activePCI_IDE->BAR[5]&((~3)&0xFFFFFFFFU))|1); //IO BAR! Unused!
		PCI_unusedBAR(activePCI_IDE, 4); //Unused!
		PCI_unusedBAR(activePCI_IDE, 5); //Unused!
		PCI_unusedBAR(activePCI_IDE, 6); //Unused!
	}
	resetPCISpaceIDE(); //For read-only fields!
}

byte CDROM_DiskChanged = 0;

void strcpy_swappedpadded(word *buffer, byte sizeinwords, byte *s)
{
	byte counter, lowbyte, highbyte;
	word length;
	length = (word)safestrlen((char *)s,((size_t)sizeinwords<<(size_t)1)); //Check the length for the copy!
for (counter = 0; counter < sizeinwords; ++counter) //Step words!
{
	lowbyte = highbyte = 0x20; //Initialize to unused!
	if (length >= ((counter << 1) | 1)) //Low byte available?
	{
		lowbyte = s[(counter << 1) | 1]; //Low byte as high byte!
	}
	if (length >= (counter << 1)) //High byte available?
	{
		highbyte = s[(counter << 1)]; //High byte as low byte!
	}
	buffer[counter] = lowbyte | (highbyte << 8); //Set the byte information!
}
}

//Internal use only:
void ATAPI_insertCD(int disk, byte disk_channel, byte disk_drive)
{
	DOUBLE retractingtime;
	DOUBLE timeoutspeed;
	//if (ATA_allowDiskChange(disk, 1)) //Allow changing of said disk?
	{
		//Normal handling of automatic insertion after some time!
		byte abortreason, additionalsensecode, ascq = 0;
		//Disable the IRQ for now to let the software know we've changed!
		if (ATA[disk_channel].Drive[disk_drive].ATAPI_caddyejected == 2)
		{
			if (ATA[disk_channel].Drive[disk_drive].ATAPI_caddyinsertion_fast) //Fast insertion instead?
			{
				ATA[disk_channel].Drive[disk_drive].ATAPI_diskchangependingspeed = 1; //Fast speed of loading!
			}
			else
			{
				ATA[disk_channel].Drive[disk_drive].ATAPI_diskchangependingspeed = 0; //Slow speed of loading!
			}
		}
		//Otherwise, use last provided speed to handle the disk insertion!

		timeoutspeed = ATAPI_INSERTION_TIME; //Default timeout speed!
		if (ATA[disk_channel].Drive[disk_drive].ATAPI_diskchangependingspeed) //Speed is immediate?
		{
			timeoutspeed = (DOUBLE)0.0f; //Fast insertion time: immediate!
		}

		if (ATA[disk_channel].Drive[disk_drive].PendingLoadingMode == LOAD_EJECTING) //We were ejecting the disc currently?
		{
			retractingtime = MAX(ATA[disk_channel].Drive[disk_drive].ATAPI_diskchangeTimeout,(DOUBLE)0.0f); //How much time is left of the ejecting!
			if (retractingtime != ATAPI_INSERTION_EJECTING_FASTTIME) //Already started ejecting?
			{
				retractingtime = ATAPI_INSERTION_EJECTING_FASTTIME - retractingtime; //How much time does it take to go from the ejecting state to the inserted state?
			}
			else //Take the full timing anyways!
			{
				retractingtime = ATAPI_INSERTION_EJECTING_FASTTIME; //How much time does it take to go from the ejecting state to the inserted state?
			}
		}
		else //We don't know how far the tray is ejected, so assume fully ejected!
		{
			retractingtime = ATAPI_INSERTION_EJECTING_FASTTIME; //How much time does it take to go from the ejecting state to the inserted state?
		}

		timeoutspeed += retractingtime; //Take the time to load the tray into account!

		if (!ATA[disk_channel].Drive[disk_drive].ATAPI_diskchangeTimeout) //Not already pending?
		{
			ATA[disk_channel].Drive[disk_drive].ATAPI_diskchangeTimeout = timeoutspeed; //New timer!
		}
		else
		{
			ATA[disk_channel].Drive[disk_drive].ATAPI_diskchangeTimeout = timeoutspeed; //Add to pending timing!
		}

		ATA[disk_channel].Drive[disk_drive].ATAPI_diskchangeDirection = ATAPI_DYNAMICLOADINGPROCESS; //Start the insertion mechanism!
		ATA[disk_channel].Drive[disk_drive].PendingLoadingMode = LOAD_INSERT_CD; //Loading and inserting the CD is now starting!
		ATA[disk_channel].Drive[disk_drive].PendingSpinType = ATAPI_CDINSERTED; //We're firing an CD inserted event!
		ATA[disk_channel].Drive[disk_drive].ATAPI_caddyejected = 3; //Inserting the disc. Becoming ready soon! Don't trigger this again!
		ATA[disk_channel].Drive[disk_drive].ATAPI_diskChanged = 1; //Is the disc changed?
		ATA[disk_channel].Drive[disk_drive].ATAPI_mediaChanged = 1; //Media has been changed(Microsoft way)?
		ATA[disk_channel].Drive[disk_drive].ATAPI_mediaChanged2 = 1; //Media has been changed(Documented way)?
		ATA[disk_channel].Drive[disk_drive].diskInserted = is_mounted(ATA_Drives[disk_channel][disk_drive]); //Are we inserted from the emulated point of view?
		EMU_setDiskBusy(ATA_Drives[disk_channel][disk_drive], 0 | (ATA[disk_channel].Drive[disk_drive].ATAPI_caddyejected << 1)); //We're not reading anymore!
		//Run an event handler for the OS!
		if (ATA[disk_channel].Drive[disk_drive].PARAMETERS.reportReady) //Ready?
		{
			ATA[disk_channel].activedrive = disk_drive; //Make us active! Unknown how real hardware handles this(for interrupts)!
			abortreason = SENSE_UNIT_ATTENTION;
			additionalsensecode = ASC_MEDIUM_MAY_HAVE_CHANGED;
			ascq = 0;
			ATA[disk_channel].Drive[disk_drive].ATAPI_processingPACKET = 3; //Result phase!
			ATA[disk_channel].Drive[disk_drive].commandstatus = 0xFF; //Move to error mode!
			ATA[disk_channel].activedrive = disk_drive; //Make us active!
			ATAPI_giveresultsize(disk_channel, disk_drive, 0, 1); //No result size!
			ATA[disk_channel].Drive[disk_drive].ERRORREGISTER = /*4|*/(abortreason << 4); //Reset error register! This also contains a copy of the Sense Key!
			ATAPI_SENSEPACKET_SENSEKEYW(disk_channel, disk_drive, abortreason); //Reason of the error
			ATAPI_SENSEPACKET_RESERVED2W(disk_channel, disk_drive, 0); //Reserved field!
			ATAPI_SENSEPACKET_ADDITIONALSENSECODEW(disk_channel, disk_drive, additionalsensecode); //Extended reason code
			ATAPI_SENSEPACKET_ASCQW(disk_channel, disk_drive, ascq); //ASCQ code!
			ATAPI_SENSEPACKET_ILIW(disk_channel, disk_drive, 0); //ILI bit cleared!
			ATAPI_SENSEPACKET_ERRORCODEW(disk_channel, disk_drive, 0x70); //Default error code?
			ATAPI_SENSEPACKET_ADDITIONALSENSELENGTHW(disk_channel, disk_drive, 10); //Additional Sense Length = 10?
			ATAPI_SENSEPACKET_INFORMATION0W(disk_channel, disk_drive, 0);	 //No info!
			ATAPI_SENSEPACKET_INFORMATION1W(disk_channel, disk_drive, 0); //No info!
			ATAPI_SENSEPACKET_INFORMATION2W(disk_channel, disk_drive, 0); //No info!
			ATAPI_SENSEPACKET_INFORMATION3W(disk_channel, disk_drive, 0); //No info!
			ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION0W(disk_channel, disk_drive, 0); //No command specific information?
			ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION1W(disk_channel, disk_drive, 0); //No command specific information?
			ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION2W(disk_channel, disk_drive, 0); //No command specific information?
			ATAPI_SENSEPACKET_COMMANDSPECIFICINFORMATION3W(disk_channel, disk_drive, 0); //No command specific information?
			ATAPI_SENSEPACKET_VALIDW(disk_channel, disk_drive, 1); //We're valid!
			ATAPI_SENSEPACKET_CD(disk_channel, disk_drive, 0); //Error in the packet data itself!
			ATA[disk_channel].Drive[disk_drive].STATUSREGISTER = 0x40; //Clear status!
			ATA_STATUSREGISTER_DRIVEREADYW(disk_channel, disk_drive, 1); //Ready!
			ATA_STATUSREGISTER_ERRORW(disk_channel, disk_drive, 1); //Ready!
		}
	}
	//Otherwise, just requested?
}

//Request to eject the caddy by the settings menu!
byte ATAPI_ejectcaddy(int disk)
{
	byte disk_drive, disk_channel, disk_nr;
	switch (disk) //What disk?
	{
		//Four disk numbers!
	case HDD0:
		disk_nr = 0;
		break;
	case HDD1:
		disk_nr = 1;
		break;
	case CDROM0:
		disk_nr = 2;
		break;
	case CDROM1:
		disk_nr = 3;
		break;
	default: //Unsupported?
		return 0; //Abort!
	}
	disk_channel = ATA_DrivesReverse[disk_nr][0]; //The channel of the disk!
	disk_drive = ATA_DrivesReverse[disk_nr][1]; //The master/slave of the disk!
	ATA_channel = disk_channel; //The channel of the access?
	ATA_slave = disk_drive; //Slave?
	if ((disk_nr >= 2) && CDROM_DiskChanged) //CDROM changed?
	{
		if (ATA[disk_channel].Drive[disk_drive].ATAPI_caddyejected == 0) //Not ejected?
		{
			ATA[disk_channel].Drive[disk_drive].MediumChangeRequested = 1; //We're requesting the medium to change!
			if (ATA_allowDiskChange(disk, 1)) //Request to be ejected immediately? We're not handled by software?
			{
				ATA[disk_channel].Drive[disk_drive].MediumChangeRequested = 0; //We're not requesting the medium to change anymore!
				//Caddy is ejected!
				handleATAPIcaddyeject(disk_channel, disk_drive); //Handle the ejection of the caddy directly!
				return 1; //OK!
			}
			//Otherwise, let the OS handle it!
		}
		else //Already ejected?
		{
			return 1; //Success!
		}
	}
	return 0; //Not ejectable!
}

//Request to insert the caddy by the settings menu!
byte ATAPI_insertcaddy(int disk)
{
	byte disk_drive, disk_channel, disk_nr;
	switch (disk) //What disk?
	{
		//Four disk numbers!
	case HDD0:
		disk_nr = 0;
		break;
	case HDD1:
		disk_nr = 1;
		break;
	case CDROM0:
		disk_nr = 2;
		break;
	case CDROM1:
		disk_nr = 3;
		break;
	default: //Unsupported?
		return 0; //Abort!
	}
	disk_channel = ATA_DrivesReverse[disk_nr][0]; //The channel of the disk!
	disk_drive = ATA_DrivesReverse[disk_nr][1]; //The master/slave of the disk!
	ATA_channel = disk_channel; //The channel of the access?
	ATA_slave = disk_drive; //Slave?
	if ((disk_channel != 0xFF) && (disk_drive != 0xFF)) //Valid?
	{
		if ((disk_nr >= 2) && CDROM_DiskChanged) //CDROM changed?
		{
			if (ATA[disk_channel].Drive[disk_drive].ATAPI_caddyejected == 0) //Not ejected?
			{
				return 0; //Can't insert what's not ejected!
			}
			else if (ATA[disk_channel].Drive[disk_drive].ATAPI_caddyejected!=3) //Already ejected and not already inserting?
			{
				//if (ATA_allowDiskChange(disk, 1)) //Request to be ejected immediately? We're not handled by software?
				{
					ATA[disk_channel].Drive[disk_drive].ATAPI_caddyejected = 2; //Request to insert the caddy when running again!
					ATA[disk_channel].Drive[disk_drive].ATAPI_caddyinsertion_fast = 1; //Fast insertion!
					//Don't update any LEDs: the disk stays ejected until handled by the OS or hardware!
					return 1; //OK!
				}
			}
			else if (ATA[disk_channel].Drive[disk_drive].ATAPI_caddyejected == 3) //Already ejected and not already inserting?
			{
				//Don't update any LEDs: the disk is already inserting!
				return 1; //OK!
			}
		}
		else if (disk_nr >= 2) //Always valid?
		{
			return 1; //OK!
		}
		else //HDD?
		{
			return 0; //Fail on HDD!
		}
	}
	return 0; //Couldn't insert caddy!
}

void ATA_DiskChanged(int disk)
{
	word wordbackup86;
	byte cue_M, cue_S, cue_F, cue_startM, cue_startS, cue_startF, cue_endM, cue_endS, cue_endF;
	char *cueimage;
	char newserial[21]; //A serial to build!
	byte disk_drive, disk_channel, disk_nr;
	switch (disk) //What disk?
	{
	//Four disk numbers!
	case HDD0:
		disk_nr = 0;
		break;
	case HDD1:
		disk_nr = 1;
		break;
	case CDROM0:
		disk_nr = 2;
		break;
	case CDROM1:
		disk_nr = 3;
		break;
	default: //Unsupported?
		return; //Abort!
	}
	disk_channel = ATA_DrivesReverse[disk_nr][0]; //The channel of the disk!
	disk_drive = ATA_DrivesReverse[disk_nr][1]; //The master/slave of the disk!
	ATA_channel = disk_channel; //The channel of the access?
	ATA_slave = disk_drive; //Slave?
	if ((disk_nr >= 2) && CDROM_DiskChanged) //CDROM changed?
	{
		ATA[disk_channel].Drive[disk_drive].lasttrack = 1; //What track are we on!
		//Initialize the audio player and make it non-active!
		ATA[disk_channel].Drive[disk_drive].AUDIO_PLAYER.status = PLAYER_INITIALIZED; //Initialized!
		ATA[disk_channel].Drive[disk_drive].AUDIO_PLAYER.effectiveplaystatus = PLAYER_STATUS_NONE; //Not playing!
		ATA[disk_channel].Drive[disk_drive].lastM = 0; //Our last position!
		ATA[disk_channel].Drive[disk_drive].lastS = 0; //Our last position!
		ATA[disk_channel].Drive[disk_drive].lastF = 0; //Our last position!
		ATA[disk_channel].Drive[disk_drive].lastformat = 0x14; //Unknown format, nothing read yet, assume data!
		ATA[disk_channel].Drive[disk_drive].lasttrack = 1; //Our last track!
	}
	byte IS_CDROM = ((disk==CDROM0)||(disk==CDROM1))?1:0; //CD-ROM drive?
	if ((disk_channel == 0xFF) || (disk_drive == 0xFF)) return; //Not mounted!
	byte disk_mounted = is_mounted(disk); //Are we mounted?
	uint_64 disk_size;
	switch (disk)
	{
	case HDD0: //HDD0 changed?
	case HDD1: //HDD1 changed?
	case CDROM0: //CDROM0 changed?
	case CDROM1: //CDROM1 changed?
		//Initialize the drive parameters!
		wordbackup86 = ATA[disk_channel].Drive[disk_drive].driveparams[86]; //Backup to not clear!
		memset(&ATA[disk_channel].Drive[disk_drive].driveparams, 0, sizeof(ATA[disk_channel].Drive[disk_drive].driveparams)); //Clear the information on the drive: it's non-existant!
		ATA[disk_channel].Drive[disk_drive].driveparams[86] = wordbackup86; //Backup to not clear!
		if (disk_mounted) //Do we even have this drive?
		{
			if ((cueimage = getCUEimage(disk))) //CUE image?
			{
				CDROM_selecttrack(disk,0); //All tracks!
				CDROM_selectsubtrack(disk,0); //All subtracks!
				if (cueimage_getgeometry(disk, &cue_M, &cue_S, &cue_F, &cue_startM, &cue_startS, &cue_startF, &cue_endM, &cue_endS, &cue_endF,0) != 0) //Geometry gotten?
				{
					disk_size = (MSF2LBAbin(cue_endM, cue_endS, cue_endF))+1; //The disk size in sectors!
				}
				else //Failed to get the geometry?
				{
					disk_size = 0; //No disk size available!
				}
				if (IS_CDROM) //CD-ROM drive?
				{
					ATAPI_loadtrackinfo(disk_channel, disk_drive); //Refresh the track information!
				}
			}
			else
			{
				disk_size = disksize(disk); //Get the disk's size!
				disk_size >>= IS_CDROM ? 11 : 9; //Get the disk size in sectors!
			}
		}
		else
		{
			disk_size = 0; //Nothing!
		}
		if (disk_mounted)
		{
			if (IS_CDROM==0) //Not with CD-ROM?
			{
				if ((disk ==HDD0) || (disk==HDD1)) ATA[disk_channel].Drive[disk_drive].driveparams[0] = (1<<6)|(1<<10)|(1<<1); //Hard sectored, Fixed drive! Disk transfer rate>10MBs, hard-sectored.
				ATA[disk_channel].Drive[disk_drive].driveparams[1] = ATA[disk_channel].Drive[disk_drive].driveparams[54] = get_cylinders(disk,disk_size); //1=Number of cylinders
				ATA[disk_channel].Drive[disk_drive].driveparams[3] = ATA[disk_channel].Drive[disk_drive].driveparams[55] = get_heads(disk,disk_size); //3=Number of heads
				ATA[disk_channel].Drive[disk_drive].driveparams[6] = ATA[disk_channel].Drive[disk_drive].driveparams[56] = get_SPT(disk,disk_size); //6=Sectors per track
				ATA[disk_channel].Drive[disk_drive].driveparams[5] = 0x200; //512 bytes per sector unformatted!
				ATA[disk_channel].Drive[disk_drive].driveparams[4] = 0x200*(ATA[disk_channel].Drive[disk_drive].driveparams[6]); //512 bytes per sector per track unformatted!
			}
		}
		memset(&newserial,0,sizeof(newserial));
		safestrcpy(&newserial[0],sizeof(newserial),(char *)&SERIAL[IS_CDROM][0]); //Copy the serial to use!
		if (safestrlen(newserial,sizeof(newserial))) //Any length at all?
		{
			newserial[safestrlen(newserial,sizeof(newserial))-1] = 48+(((!IS_CDROM)?(disk_channel<<1):0)|disk_drive); //Unique identifier for the disk, acting as the serial number!
		}
		strcpy_swappedpadded(&ATA[disk_channel].Drive[disk_drive].driveparams[10],10,(byte *)newserial);
		if (IS_CDROM==0)
		{
			ATA[disk_channel].Drive[disk_drive].driveparams[20] = 1; //Only single port I/O (no simultaneous transfers) on HDD only(ATA-1)!
		}

		//Fill text fields, padded with spaces!
		strcpy_swappedpadded(&ATA[disk_channel].Drive[disk_drive].driveparams[23],4,&FIRMWARE[IS_CDROM][0]);
		strcpy_swappedpadded(&ATA[disk_channel].Drive[disk_drive].driveparams[27],20,&MODEL[IS_CDROM][0]);

		ATA[disk_channel].Drive[disk_drive].driveparams[47] = IS_CDROM?0:(MIN(sizeof(ATA[disk_channel].Drive[disk_drive].data)>>9,0x7F)&0xFF); //Amount of read/write multiple supported, in sectors!
		ATA[disk_channel].Drive[disk_drive].driveparams[49] = (1<<9); //LBA supported(bit 9), DMA unsupported(bit 8)!
		ATA[disk_channel].Drive[disk_drive].driveparams[51] = 0x200; //PIO data transfer timing node(high 8 bits)! Specify mode 2(which is the fastest)!
		--disk_size; //LBA is 0-based, not 1 based!
		if (IS_CDROM==0) //HDD only!
		{
			ATA[disk_channel].Drive[disk_drive].driveparams[53] = 1; //The data at 54-58 are valid on ATA-1!
			ATA[disk_channel].Drive[disk_drive].driveparams[59] = (ATA[disk_channel].Drive[disk_drive].multiplemode?0x100:0)|(ATA[disk_channel].Drive[disk_drive].multiplemode); //Current multiple sectors setting! Bit 8 is set when updated!
			ATA[disk_channel].Drive[disk_drive].driveparams[60] = (word)(disk_size & 0xFFFF); //Number of addressable LBA sectors, low word!
			ATA[disk_channel].Drive[disk_drive].driveparams[61] = (word)(disk_size >> 16); //Number of addressable LBA sectors, high word!
		}
		else
		{
			ATA[disk_channel].Drive[disk_drive].ATAPI_disksize = (uint_32)disk_size; //Number of addressable LBA sectors, minus one!
		}
		//ATA-1 supports up to word 63 only. Above is filled on ATAPI only(newer ATA versions)!
		ATA[disk_channel].Drive[disk_drive].driveparams[72] = 0; //Major version! We're ATA/ATAPI 4 on CD-ROM, ATA-1 on HDD!
		ATA[disk_channel].Drive[disk_drive].driveparams[72] = 0; //Minor version! We're ATA/ATAPI 4!
		if (IS_CDROM) //CD-ROM only?
		{
			ATA[disk_channel].Drive[disk_drive].driveparams[80] = (1<<4); //Supports ATA-1 on HDD, ATA-4 on CD-ROM!
			ATA[disk_channel].Drive[disk_drive].driveparams[81] = 0x0017; //ATA/ATAPI-4 T13 1153D revision 17 on CD-ROM, ATA (ATA-1) X3T9.2 781D prior to revision 4 for hard disk(=1, but 0 due to ATA-1 specification not mentioning it).
			ATA[disk_channel].Drive[disk_drive].driveparams[82] = ((1<<4)|(1<<9)|(1<<14)); //On CD-ROM, PACKET; DEVICE RESET; NOP is supported, ON hard disk, only NOP is supported. This word is valid(bit 14 set, bit 15 cleared.)
			ATA[disk_channel].Drive[disk_drive].driveparams[83] = (1<<4)|(1<<14); //On CD-ROM, removable status notification feature set!
			ATA[disk_channel].Drive[disk_drive].driveparams[85] = (1 << 4); //On CD-ROM, PACKET command feature is enabled!
			ATA[disk_channel].Drive[disk_drive].driveparams[87] = (1 << 14); //On CD-ROM, PACKET command feature isn't default!
			ATA[disk_channel].Drive[disk_drive].driveparams[127] = 0x0001; //01 in bit 0-1 means that we're using the removable media Microsoft feature set.
		}
		ATA_updateCapacity(disk_channel,disk_drive); //Update the drive capacity!
		if ((disk == CDROM0) || (disk == CDROM1)) //CDROM?
		{
			ATA[disk_channel].Drive[disk_drive].driveparams[0] = ((2 << 14) /*ATAPI DEVICE*/ | (5 << 8) /* Command packet set used by device */ | (1 << 7) /* Removable media device */ | (2 << 5) /* DRQ within 50us of receiving PACKET command */ | (0 << 0) /* 12-byte command packet */ ); //CDROM drive ID!
		}
		break;
	default: //Unknown?
		break;
	}
}

void startIDEIRQ(byte IRQ)
{
	byte channel, drive;
	channel = 0xFF; //None detected!
	if ((is_XT && (IRQ == ATA_PRIMARYIRQ_XT)) || ((is_XT == 0) && (IRQ == ATA_PRIMARYIRQ_AT))) //Primary?
	{
		channel = 0; //Primary!
	}
	else if ((is_XT && (IRQ == ATA_SECONDARYIRQ_XT)) || ((is_XT == 0) && (IRQ == ATA_SECONDARYIRQ_AT))) //Secondary?
	{
		channel = 1; //Secondary!
	}
	if (unlikely(channel == 0xFF)) return; //Don't handle if it's not us!
	drive = ATA_activeDrive(channel); //Active drive!
	if (ATA[channel].Drive[drive].IRQraised==1) //IRQ has been raised and not acnowledged?
	{
		ATA[channel].Drive[drive].IRQraised = 3; //We're fully raised and acnowledged!
	}
}

void initATA()
{
	byte slave;
	memset(&ATA, 0, sizeof(ATA)); //Initialise our data!

	//We don't register a disk change handler, because ATA doesn't change disks when running!
	//8-bits ports!
	register_PORTIN(&inATA8);
	register_PORTOUT(&outATA8);
	//16-bits port!
	register_PORTINW(&inATA16);
	register_PORTOUTW(&outATA16);
	//32-bits port!
	register_PORTIND(&inATA32);
	register_PORTOUTD(&outATA32);


	if (is_XT)
	{
		registerIRQ(ATA_PRIMARYIRQ_XT, &startIDEIRQ, NULL); //Register our IRQ finish!
		registerIRQ(ATA_SECONDARYIRQ_XT, &startIDEIRQ, NULL); //Register our IRQ finish!
	}
	else
	{
		registerIRQ(ATA_PRIMARYIRQ_AT, &startIDEIRQ, NULL); //Register our IRQ finish!
		registerIRQ(ATA_SECONDARYIRQ_AT, &startIDEIRQ, NULL); //Register our IRQ finish!
	}


	//We don't implement DMA: this is done by our own DMA controller!
	//First, detect HDDs!
	memset(&ATA_Drives, 0, sizeof(ATA_Drives)); //Init drives to unused!
	memset(&ATA_DrivesReverse, 0, sizeof(ATA_DrivesReverse)); //Init reverse drives to unused!
	ATA[0].Drive[0].resetSetsDefaults = ATA[0].Drive[1].resetSetsDefaults = ATA[1].Drive[0].resetSetsDefaults = ATA[1].Drive[1].resetSetsDefaults = 1; //Reset sets defaults by default after poweron!
	CDROM_channel = 1; //CDROM is the second channel by default!
	if (is_mounted(HDD0)) //Have HDD0?
	{
		ATA_Drives[0][0] = HDD0; //Mount HDD0!
		if (is_mounted(HDD1)) //Have HDD1?
		{
			ATA_Drives[0][1] = HDD1; //Mount HDD1!
		}
	}
	else if (is_mounted(HDD1)) //Have HDD1?
	{
		ATA_Drives[0][0] = HDD1; //Mount HDD1!
	}
	ATA_Drives[CDROM_channel][0] = CDROM0; //CDROM0 always present as master!
	ATA_Drives[CDROM_channel][1] = CDROM1; //CDROM1 always present as slave!
	int i,j,k;
	int disk_reverse[4] = { HDD0,HDD1,CDROM0,CDROM1 }; //Our reverse lookup information values!
	for (i = 0;i < 4;i++) //Check all drives mounted!
	{
		ATA_DrivesReverse[i][0] = 0xFF; //Unassigned!
		ATA_DrivesReverse[i][1] = 0xFF; //Unassigned!
		for (j = 0;j < 2;j++)
		{
			for (k = 0;k < 2;k++)
			{
				if (ATA_Drives[j][k] == disk_reverse[i]) //Found?
				{
					if ((disk_reverse[i] == HDD0) || (disk_reverse[i] == HDD1))
					{
						ATA[j].Drive[k].preventMediumRemoval = 1; //We're preventing medium removal, when running the emulation!
					}
					ATA_DrivesReverse[i][0] = j;
					ATA_DrivesReverse[i][1] = k; //Load reverse lookup!
				}
			}
		}
	}
	//Now, apply the basic disk information (disk change/initialisation of parameters)!
	register_DISKCHANGE(HDD0, &ATA_DiskChanged);
	register_DISKCHANGE(HDD1, &ATA_DiskChanged);
	register_DISKCHANGE(CDROM0, &ATA_DiskChanged);
	register_DISKCHANGE(CDROM1, &ATA_DiskChanged);
	CDROM_DiskChanged = 0; //Init!
	ATA[CDROM_channel].Drive[0].PendingLoadingMode = is_mounted(CDROM0)?LOAD_IDLE:LOAD_NO_DISC; //Default: no disc is present or idle!
	ATA[CDROM_channel].Drive[1].PendingLoadingMode = is_mounted(CDROM1)?LOAD_IDLE:LOAD_NO_DISC; //Default: no disc is present or idle!
	ATA_DiskChanged(HDD0); //Init HDD0!
	ATA_DiskChanged(HDD1); //Init HDD1!
	ATA_DiskChanged(CDROM0); //Init CDROM0!
	ATA_DiskChanged(CDROM1); //Init CDROM1!
	ATA[CDROM_channel].Drive[0].diskInserted = is_mounted(CDROM0); //Init Mounted and inserted?
	ATA[CDROM_channel].Drive[1].diskInserted = is_mounted(CDROM1); //Init Mounted and inserted?
	ATA[CDROM_channel].Drive[0].allowDiskInsertion = 1; //Allow disk insertion and caddy ejected!
	ATA[CDROM_channel].Drive[1].allowDiskInsertion = 1; //Allow disk insertion and caddy ejected!
	ATA[CDROM_channel].Drive[0].ATAPI_caddyejected = 0; //Caddy ejected?
	ATA[CDROM_channel].Drive[1].ATAPI_caddyejected = 0; //Caddy ejected?
	EMU_setDiskBusy(ATA_Drives[CDROM_channel][0], 0 | (ATA[CDROM_channel].Drive[0].ATAPI_caddyejected << 1)); //We're not reading anymore!
	EMU_setDiskBusy(ATA_Drives[CDROM_channel][1], 0 | (ATA[CDROM_channel].Drive[1].ATAPI_caddyejected << 1)); //We're not reading anymore!
	CDROM_DiskChanged = 1; //We're changing when updating!
	memset(&PCI_IDE, 0, sizeof(PCI_IDE)); //Initialise to 0!
	if (activePCI_IDE == NULL) //To allocate?
	{
		register_PCI(&PCI_IDE, 1, 0, (sizeof(PCI_IDE) >> 2), &ATA_ConfigurationSpaceChanged); //Register the PCI data area!
		activePCI_IDE = &PCI_IDE; //Use the IDE handler!
		PCI_unusedBAR(activePCI_IDE, 0); //Unused!
		PCI_unusedBAR(activePCI_IDE, 1); //Unused!
		PCI_unusedBAR(activePCI_IDE, 2); //Unused!
		PCI_unusedBAR(activePCI_IDE, 3); //Unused!
		PCI_unusedBAR(activePCI_IDE, 4); //Unused!
		PCI_unusedBAR(activePCI_IDE, 5); //Unused!
		PCI_unusedBAR(activePCI_IDE, 6); //Unused!
	}
	ATA_ConfigurationSpaceChanged(0x9, 1, 0, 1); //Make sure that the setting is up-to-date!
	//Initialise our data area!
	resetPCISpaceIDE();
	PCI_IDE.Command = 0x01; //Enable the device by default for compatibility with older motherboards!
	activePCI_IDE->ProgIF |= 0xA; //We're always having a programmable ProgIF setting for primary and secondary controller legacy mode!
	activePCI_IDE->BAR[0] = 1; //I/O!
	activePCI_IDE->BAR[1] = 1; //I/O!
	activePCI_IDE->BAR[2] = 1; //I/O!
	activePCI_IDE->BAR[3] = 1; //I/O!
	activePCI_IDE->BAR[4] = 1; //I/O!
	activePCI_IDE->BAR[5] = 1; //I/O!
	
	ATA[0].Drive[0].resetTiming = ATA[0].Drive[1].resetTiming = 0.0; //Clear the reset timing!
	ATA[1].Drive[0].resetTiming = ATA[1].Drive[1].resetTiming = 0.0; //Clear the reset timing!
	ATA[0].DriveAddressRegister = ATA[1].DriveAddressRegister = 0xFF; //According to Bochs, it's always 1's when unsupported!
	ATA_reset(0,0); //Hardware reset!
	ATA_reset(0,1); //Hardware reset!
	ATA_reset(1,0); //Hardware reset!
	ATA_reset(1,1); //Hardware reset!
	ATAPI_setModePages(CDROM_channel, 0); //Init specific mode pages!
	ATAPI_setModePages(CDROM_channel, 1); //Init specifc mode pages!
	ATA_channel = ATA_slave = 0; //Default to channel 0, Master!
	ATA[0].Drive[0].lastformat = 0x14; //Data track last seen(data track)!

	//Initialize the CD-ROM player data!
	ATA[CDROM_channel].Drive[0].AUDIO_PLAYER.status = PLAYER_INITIALIZED; //Initialized player status(stopped)!
	ATA[CDROM_channel].Drive[0].AUDIO_PLAYER.effectiveplaystatus = PLAYER_STATUS_NONE; //Initialized player status(stopped)!
	ATA[CDROM_channel].Drive[0].lasttrack = 1; //What track are we on!
	ATA[CDROM_channel].Drive[1].lasttrack = 1; //What track are we on!
	ATA[CDROM_channel].Drive[1].AUDIO_PLAYER.status = PLAYER_INITIALIZED; //Initialized player status(stopped)!
	ATA[CDROM_channel].Drive[1].AUDIO_PLAYER.effectiveplaystatus = PLAYER_STATUS_NONE; //Initialized player status(stopped)!
	ATA[CDROM_channel].playerTiming = (DOUBLE)0.0f; //Initialize the player timing!
	ATA[CDROM_channel].playerTick = (DOUBLE)(1000000000.0 / 44100.0); //The time of one sample to render!

	for (slave = 0; slave < 2; ++slave) //Create the audio outputs!
	{
		if (allocDoubleBufferedSound32(__CDROM_SAMPLEBUFFERSIZE, &ATA[CDROM_channel].Drive[slave].AUDIO_PLAYER.soundbuffer, 0, 44100.0)) //Valid buffer?
		{
			if (!addchannel(&CDROM_soundGenerator, &ATA[CDROM_channel].Drive[slave].AUDIO_PLAYER.soundbuffer, "CDROMaudio", (float)44100.0, __CDROM_SAMPLEBUFFERSIZE, 1, SMPL16S,1)) //Start the sound emulation (stereo) with automatic samples buffer?
			{
				dolog("CDROM", "Error registering sound channel for output!");
			}
			else
			{
				setVolume(&CDROM_soundGenerator, &ATA[CDROM_channel].Drive[slave].AUDIO_PLAYER.soundbuffer, __CDROM_VOLUME);
			}
		}
		else
		{
			dolog("CDROM", "Error registering first double buffer for output!");
		}
	}
}

void doneATA()
{
	byte slave;
	if (CDROM_channel != 0xFF) //Valid?
	{
		for (slave = 0; slave < 2; ++slave) //Process all CD-ROM channels!
		{
			removechannel(&CDROM_soundGenerator, &ATA[CDROM_channel].Drive[slave].AUDIO_PLAYER.soundbuffer, 0); //Stop the sound emulation?
			freeDoubleBufferedSound(&ATA[CDROM_channel].Drive[slave].AUDIO_PLAYER.soundbuffer);
		}
	}
}
