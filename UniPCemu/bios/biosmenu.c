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

#include "headers/types.h" //Types and linkage!
#include "headers/bios/bios.h" //Basic BIOS compatibility types etc including myself!
#include "headers/cpu/cpu.h" //CPU constants!
#include "headers/emu/gpu/gpu.h" //GPU compatibility!
#include "headers/basicio/staticimage.h" //Static image compatibility!
#include "headers/basicio/dynamicimage.h" //Dynamic image compatibility!
#include "headers/basicio/io.h" //Basic I/O comp!
#include "headers/emu/input.h" //Basic key input!
#include "headers/hardware/vga/vga.h" //VGA!
#include "headers/cpu/interrupts.h" //Interrupt support (int10 mostly)
#include "headers/support/zalloc.h" //Memory allocation!
#include "headers/bios/biosmenu.h" //Our defines!
#include "headers/emu/emucore.h" //For init/doneEMU for memory reallocation.
#include "headers/fopen64.h" //64-bit fopen support!
#include "headers/emu/gpu/gpu_emu.h" //GPU emulator support!
#include "headers/emu/gpu/gpu_text.h" //For the framerate surface clearing!
#include "headers/interrupts/interrupt10.h" //GPU emulator support!
#include "headers/emu/directorylist.h" //Directory listing support!
#include "headers/support/log.h" //Logging for disk images!
#include "headers/hardware/ps2_keyboard.h" //PS/2 keyboard key name support!
#include "headers/emu/input.h" //We need input using psp_inputkey.
#include "headers/emu/emu_vga.h" //VGA update support!
#include "headers/basicio/dskimage.h" //DSK image support!
#include "headers/basicio/imdimage.h" //DSK image support!
#include "headers/support/mid.h" //MIDI player support!
#include "headers/hardware/ssource.h" //Sound Source volume knob support!
#include "headers/emu/gpu/gpu_framerate.h" //Framerate support!
#include "headers/support/highrestimer.h" //High resolution clock support!
#include "headers/emu/sound.h" //Recording support!
#include "headers/hardware/floppy.h" //Floppy disk support!
#include "headers/hardware/vga/vga_dacrenderer.h" //Renderer logging support of DAC colors!
#include "headers/hardware/vga/vga_vramtext.h" //VRAM font table logging support!
#include "headers/hardware/vga/vga_cga_mda.h" //CGA/MDA dumping support!
#include "headers/support/dro.h" //DRO file support!
#include "headers/support/bmp.h" //For dumping our full VGA RAM!
#include "headers/hardware/gameblaster.h" //Gameblaster volume knob support!
#include "headers/hardware/ide.h" //Disk change allowed detection!
#include "headers/hardware/vga/svga/tseng.h" //ET3000/ET4000 support!
#include "headers/hardware/midi/mididevice.h" //For Direct MIDI support!
#include "headers/mmu/mmuhandler.h" //MMU memory support!
#include "headers/hardware/vga/vga_precalcs.h" //Precalcs support!
#include "headers/cpu/easyregs.h" //Easy register support!
#include "headers/hardware/modem.h" //Connection support!
#include "headers/emu/emu_misc.h" //converthex2int support!
#include "gitcommitversion.h" //Git version support!

extern byte diagnosticsportoutput; //Diagnostics port output!

//Define below to enable the sound test with recording!
//#define SOUND_TEST

//Dump a 256-color 640x480 VRAM layout to a bitmap file!
//#define DUMP_VGATEST256COL

#ifdef SOUND_TEST
#include "headers/hardware/ports.h" //I/O support!
#endif

#define __HW_DISABLED 0

//Force the BIOS to open?
#define FORCE_BIOS 0

//BIOS width in text mode!
#define BIOS_WIDTH GPU_TEXTSURFACE_WIDTH

#define MENU_MAXITEMS 0x100

//How long to press for BIOS!
#define BIOS_TIME 10000000
#define INPUT_INTERVAL 100000

extern char diskpath[256]; //The full disk path used!

char soundfontpath[256] = "soundfonts";
char musicpath[256] = "music"; //Music directory containing all music!

char menuoptions[MENU_MAXITEMS][256]; //Going to contain the menu's for BIOS_ShowMenu!

typedef struct
{
char name[256]; //The name for display!
byte Header; //Header (BIOS Text) font/background!
byte Menubar_inactive; //Menu bar (inactive)
byte Menubar_active; //Menu bar (active)
byte Text; //Text font
byte Border; //Border font
byte Option_inactive; //Inactive option
byte Option_active; //Active option
byte Selectable_inactive; //Inactive selectable (with submenu)
byte Selectable_active; //Active selectable (with submenu)
byte Sub_border; //Selectable option border!
byte Sub_Option_inactive; //Sub Selectable option inactive
byte Sub_Option_active; //Sub Selectable option active
byte Bottom_key; //Bottom key(combination) text
byte Bottom_text; //Bottom text (information about the action of the key)
byte Background; //Simple background only! (Don't show text!)
byte HighestBitBlink; //Highest color bit is blinking? 0=Off: Use 16 colors, 1=On: Use 8 colors!
} BIOSMENU_FONT; //All BIOS fonts!

extern BIOS_Settings_TYPE BIOS_Settings; //Currently loaded settings!
extern byte exec_showchecksumerrors; //Show checksum errors?
extern GPU_TEXTSURFACE *frameratesurface;

BIOSMENU_FONT BIOSMenu_Fonts[3] = {
	{"Default",0x9F,0x17,0x71,0x70,0x70,0x71,0x1F,0x71,0x1F,0x17,0x71,0x1F,0x3F,0x30,0x77,0} //Default (our original font)
	,{"Phoenix Laptop",0x30,0x17,0x71,0x71,0x70,0x71,0x7F,0x70,0x07,0x17,0x17,0x1F,0x3F,0x30,0x77,0} //Phoenix Laptop BIOS font: src: Original Acer Aspire 7741ZG BIOS Setup menu.
	,{"Phoenix - Award Workstation",0x1F,0x11,0x11,0x19,0x1F,0x1E,0x4F,0x1E,0x4F,0x1F,0x1E,0x4F,0x1F,0x1F,0x11,0} //Src: http://www.computerhope.com/help/phoenixa.htm
};

/* BIOS PRESETS */

//Menu Font selection!
#define ActiveBIOSPreset BIOSMenu_Fonts[BIOS_Settings.BIOSmenu_font%NUMITEMS(BIOSMenu_Fonts)]

//Header background (header for section etc)
#define BIOSHEADER_ATTR ActiveBIOSPreset.Header

//Top font/background (BIOS HEADER TEXT)!
#define BIOSTOP_ATTR ActiveBIOSPreset.Menubar_active

//Now stuff for plain items!

//Gray background for entire BIOS (full screen) (|=0x70)
//Black font (plain text on gray)
#define BIOS_ATTR_TEXT ActiveBIOSPreset.Text
//Back color for selected item (white on dark blue)
#define BIOS_ATTR_ACTIVE ActiveBIOSPreset.Option_active
//Inactive item (dark blue on gray)
#define BIOS_ATTR_INACTIVE ActiveBIOSPreset.Option_inactive
//Background (Gray on Gray)
#define BIOS_ATTR_BACKGROUND ActiveBIOSPreset.Background

#define BIOS_ATTR_BLINKENABLE (ActiveBIOSPreset.HighestBitBlink>0)

/* END OF BIOS PRESETS */

//Now for the seperate menus:
void BIOS_MainMenu(); //Main menu!
void BIOS_DisksMenu(); //Manages the mounted disks!
void BIOS_floppy0_selection(); //FLOPPY0 selection menu!
void BIOS_floppy1_selection(); //FLOPPY1 selection menu!
void BIOS_hdd0_selection(); //HDD0 selection menu!
void BIOS_hdd1_selection(); //HDD1 selection menu!
void BIOS_cdrom0_selection(); //CDROM0 selection menu!
void BIOS_cdrom1_selection(); //CDROM1 selection menu!
void BIOS_AdvancedMenu(); //Advanced menu!
void BIOS_BootOrderOption(); //Boot order option!
void BIOS_InstalledCPUOption(); //Manages the installed CPU!
void BIOS_GenerateStaticHDD(); //Generate Static HDD Image!
void BIOS_GenerateDynamicHDD(); //Generate Dynamic HDD Image!
void BIOS_DebugMode(); //Switch BIOS Mode!
void BIOS_DebugLog(); //Debugger log!
void BIOS_ExecutionMode(); //Switch execution mode!
void BIOS_MemReAlloc(); //Reallocate memory!
void BIOS_DirectPlotSetting(); //Direct Plot Setting!
void BIOS_FontSetting(); //BIOS Font Setting!
void BIOS_AspectRatio(); //Keep aspect ratio?
void BIOS_ConvertStaticDynamicHDD(); //Convert static to dynamic HDD?
void BIOS_ConvertDynamicStaticHDD(); //Generate Static HDD Image from a dynamic one!
void BIOS_DefragmentDynamicHDD(); //Defragment a dynamic HDD Image!
void BIOS_BWMonitor(); //Switch b/w monitor vs color monitor!
void BIOS_inputMenu(); //Manage stuff concerning input.
void BIOS_gamingModeButtonsMenu(); //Manage stuff concerning input.
void BIOS_gamingModeButtonsFaceButtonMenu(); //Manage stuff concerning face button input.
void BIOS_gamingKeyboardColorsMenu(); //Manage stuff concerning input.
void BIOS_gamingKeyboardColor(); //Select a gaming keyboard color!
void BIOSMenu_LoadDefaults(); //Load the defaults option!
void BIOSClearScreen(); //Resets the BIOS's screen!
void BIOSDoneScreen(); //Cleans up the BIOS's screen!
void BIOS_VideoSettingsMenu(); //Manage stuff concerning video output.
void BIOS_VGAModeSetting(); //VGA Mode setting!
void BIOS_SoundMenu(); //Manage stuff concerning Sound.
void BIOS_SoundFont_selection(); //FLOPPY0 selection menu!
void BIOS_MusicPlayer(); //Music player!
void BIOS_Architecture(); //Mouse selection menu!
void BIOS_CPU(); //CPU menu!
void BIOS_CPUSpeed(); //CPU speed selection!
void BIOS_ClearCMOS(); //Clear the CMOS!
void BIOS_SoundSourceVolume(); //Set the Sound Source volume!
void BIOS_ShowFramerate(); //Show framerate setting!
void BIOS_DataBusSizeSetting(); //Data bus size setting!
void BIOS_ShowCPUSpeed(); //Show CPU speed setting!
void BIOS_SoundStartStopRecording(); //Start/stop recording sound!
void BIOS_GenerateFloppyDisk(); //Generate an floppy disk image!
void BIOS_usePCSpeaker();
void BIOS_useAdlib();
void BIOS_useLPTDAC();
void BIOS_VGASynchronization();
void BIOS_DumpVGA();
void BIOS_CGAModel();
void BIOS_gamingmodeJoystick(); //Use joystick instead of normal gaming mode?
void BIOS_JoystickReconnect(); //Reconnect the joystick (not SDL2)
void BIOS_useGameBlaster();
void BIOS_GameBlasterVolume();
void BIOS_useSoundBlaster();
void BIOS_TurboCPUSpeed(); //CPU speed selection!
void BIOS_useTurboCPUSpeed(); //CPU speed toggle!
void BIOS_diagnosticsPortBreakpoint(); //Diagnostics Port Breakpoint setting!
void BIOS_diagnosticsPortBreakpointTimeout(); //Timeout to be used for breakpoints?
void BIOS_useDirectMIDIPassthrough();
void BIOS_breakpoint();
void BIOS_syncTime(); //Reset the kept time in UniPCemu!
void BIOS_ROMMode(); //ROM mode!
void BIOS_DebugState(); //State log!
void BIOS_InboardInitialWaitstates(); //Inboard Initial Waitstates
void BIOS_ClockingMode(); //Clocking Mode toggle!
void BIOS_DebugRegisters(); //Debug registers log!
void BIOS_CMOSTiming(); //Time the CMOS!
void BIOS_BackgroundPolicySetting(); //Background policy!
void BIOS_AdvancedLogSetting(); //Advanced log policy!
void BIOS_taskBreakpoint(); //Task breakpoint!
void BIOS_FSBreakpoint(); //Task breakpoint!
void BIOS_CR3breakpoint(); //CR3 breakpoint!
void BIOS_DirectInput_remap_RCTRL_to_LWIN(); //Remap RCTRL to LWIN!
void BIOS_DirectInput_remap_accentgrave_to_tab(); //Remap accent grave to tab during LALT!
void BIOS_DirectInput_remap_NUM0_to_Delete(); //Remap NUM0 to Delete!
void BIOS_floppy0_nodisk_type();
void BIOS_floppy1_nodisk_type();
void BIOS_DirectInput_Disable_RALT(); //Disable RALT during Direct Input mode!
void BIOS_GenerateIMDFloppyDisk(); //Generate an IMD floppy disk image!
void BIOS_LoadEjectCDROM0(); //Load/Eject CD-ROM 0!
void BIOS_LoadEjectCDROM1(); //Load/Eject CD-ROM 1!
void BIOS_EmulatedCPUs(); //How many emulated CPUs!
void BIOS_CPUIDmode(); //CPUID mode!
void BIOS_connectdisconnectpassthrough(); //Connect/disconnect passthrough!
void BIOS_nullModem(); //Nullmodem setting!
void BIOS_BWMonitor_LuminanceMode(); //Luminance mode
void BIOS_SVGA_DACMode(); //DAC mode
void BIOS_ET4000_extensions(); //ET4000 extensions
void BIOS_video_blackpedestal(); //Black pedestal
void BIOS_gamingModeButtonsJoystickEnable(); //Gaming Mode Joystick Enable
void BIOS_modemListenPort(); //Modem Listen Port
void BIOS_analogMinRange(); //Analog minimum range
void BIOS_CPUDebuggerMenu(); //CPU debugger menu!
void BIOS_versionInformation(); //Version information!

//First, global handler!
Handler BIOS_Menus[] =
{
	BIOS_MainMenu //The main menu is #0!
	,BIOS_DisksMenu //The Disks menu is #1!
	,BIOS_floppy0_selection //FLOPPY0 selection for the disks menu is #2!
	,BIOS_floppy1_selection //FLOPPY1 selection for the disks menu is #3!
	,BIOS_hdd0_selection //HDD0 selection for the disks menu is #4!
	,BIOS_hdd1_selection //HDD1 selection for the disks menu is #5!
	,BIOS_cdrom0_selection //CDROM0 selection for the disks menu is #6!
	,BIOS_cdrom1_selection //CDROM1 selection for the disks menu is #7!
	,BIOS_AdvancedMenu //Advanced menu is #8
	,BIOS_BootOrderOption //Boot Order option for the Advanced menu is #9!
	,BIOS_InstalledCPUOption //Installed CPU option for the Advanced menu is #10!
	,BIOS_GenerateStaticHDD //Generate Static HDD Disk Image is #11!
	,BIOS_GenerateDynamicHDD //Generate Dynamic HDD Disk Image is #12!
	,BIOS_DebugMode //Debug mode select is #13!
	,BIOS_MemReAlloc //Memory reallocation is #14!
	,BIOS_DirectPlotSetting //Direct Plot setting is #15!
	,BIOS_FontSetting //Font setting is #16!
	,BIOS_AspectRatio //Aspect ratio setting is #17!
	,BIOSMenu_LoadDefaults //Load defaults setting is #18!
	,BIOS_ConvertStaticDynamicHDD //Convert static to dynamic HDD is #19!
	,BIOS_ConvertDynamicStaticHDD //Convert dynamic to static HDD is #20!
	,BIOS_DefragmentDynamicHDD //Defragment a dynamic HDD is #21!
	,BIOS_BWMonitor //Switch to a b/w monitor or color monitor is #22!
	,BIOS_DebugLog //Enable/disable debugger log is #23!
	,BIOS_ExecutionMode //Execution mode is #24!
	,BIOS_inputMenu //Input menu is #25!
	,BIOS_gamingModeButtonsMenu //Gaming mode buttons menu is #26!
	,BIOS_gamingKeyboardColorsMenu //Keyboard colors menu is #27!
	,BIOS_gamingKeyboardColor //Keyboard color menu is #28!
	,BIOS_VideoSettingsMenu //Manage stuff concerning Video Settings is #29!
	,BIOS_VGAModeSetting //VGA Mode setting is #30!
	,BIOS_SoundMenu //Sound settings menu is #31!
	,BIOS_SoundFont_selection //Soundfont selection menu is #32!
	,BIOS_MusicPlayer //Music Player is #33!
	,BIOS_Architecture //Architecture menu is #34!
	,BIOS_CPU //BIOS CPU menu is #35!
	,BIOS_CPUSpeed //BIOS CPU speed is #36!
	,BIOS_ClearCMOS //BIOS CMOS clear is #37!
	,BIOS_SoundSourceVolume //Sound Source Volume is #38!
	,BIOS_ShowFramerate //Show Framerate is #39!
	,BIOS_DataBusSizeSetting //Data Bus size setting is #40!
	,BIOS_ShowCPUSpeed //Show CPU speed is #41!
	,BIOS_SoundStartStopRecording //Start/stop recording sound is #42!
	,BIOS_GenerateFloppyDisk //Generate a floppy disk is #43!
	,BIOS_usePCSpeaker //Use PC Speaker is #44!
	,BIOS_useAdlib //Use Adlib is #45!
	,BIOS_useLPTDAC //Use LPT DAC is #46!
	,BIOS_VGASynchronization //Change VGA Synchronization setting is #47!
	,BIOS_DumpVGA //Dump the VGA fully is #48!
	,BIOS_CGAModel //Select the CGA Model is #49!
	,BIOS_gamingmodeJoystick //Use Joystick is #50!
	,BIOS_JoystickReconnect //Reconnect Joystick is #51!
	,BIOS_useGameBlaster //Use Game Blaster is #52!
	,BIOS_GameBlasterVolume //Game Blaster Volume is #53!
	,BIOS_useSoundBlaster //Use Sound Blaster is #54!
	,BIOS_TurboCPUSpeed //BIOS Turbo CPU speed is #55!
	,BIOS_useTurboCPUSpeed //CPU speed toggle is #56!
	,BIOS_diagnosticsPortBreakpoint //Diagnostics port breakpoint is #57!
	,BIOS_diagnosticsPortBreakpointTimeout //Timeout to be used for breakpoints is #58!
	,BIOS_useDirectMIDIPassthrough //Use Direct MIDI Passthrough is #59!
	,BIOS_breakpoint //Breakpoint is #60!
	,BIOS_syncTime //Reset timekeeping is #61!
	,BIOS_ROMMode //BIOS ROM mode is #62!
	,BIOS_DebugState //BIOS State log is #63!
	,BIOS_InboardInitialWaitstates //Inboard Initial Waitstates is #64!
	,BIOS_ClockingMode //Clocking Mode toggle is #65!
	,BIOS_DebugRegisters //Log registers is #66!
	,BIOS_CMOSTiming //Time the CMOS is #67!
	,BIOS_BackgroundPolicySetting //Background policy is #68!
	,BIOS_AdvancedLogSetting //Advanced log if #69!
	,BIOS_taskBreakpoint //Task breakpoint is #70!
	,BIOS_CR3breakpoint //CR3 breakpoint is #71!
	,BIOS_DirectInput_remap_RCTRL_to_LWIN //Remap RCTRL to LWIN is #72!
	,BIOS_DirectInput_remap_accentgrave_to_tab //Remap accent grave to tab during LALT is #73!
	,BIOS_floppy0_nodisk_type //Floppy A without disk type is #74!
	,BIOS_floppy1_nodisk_type //Floppy B without disk type is #75!
	,BIOS_DirectInput_Disable_RALT //Disable RALT during Direct Input mode is #76!
	,BIOS_DirectInput_remap_NUM0_to_Delete //Remap NUM0 to Delete is #77!
	,BIOS_GenerateIMDFloppyDisk //Generate IMD floppy disk is #78!
	,BIOS_LoadEjectCDROM0 //Load/Eject CD-ROM 0 is #79!
	,BIOS_LoadEjectCDROM1 //Load/Eject CD-ROM 1 is #80!
	,BIOS_FSBreakpoint //Task breakpoint is #81!
	,BIOS_EmulatedCPUs //Emulated CPUs is #82!
	,BIOS_CPUIDmode //CPUID mode is #83!
	,BIOS_connectdisconnectpassthrough //Connect/disconnect passthrough is #84!
	,BIOS_nullModem //Nullmodem is #85!
	,BIOS_BWMonitor_LuminanceMode //Luminance mode is #86!
	,BIOS_SVGA_DACMode //SVGA DAC mode is #87!
	,BIOS_ET4000_extensions //ET4000 extensions is #88!
	,BIOS_video_blackpedestal //Black pedestal is #89!
	,BIOS_gamingModeButtonsFaceButtonMenu //Gaming Mode Face Button menu is #90!
	,BIOS_gamingModeButtonsJoystickEnable //Gaming Mode Joystick Enable is #91!
	,BIOS_modemListenPort //Modem listen port is #92!
	,BIOS_analogMinRange //Analog minimum range is #93!
	,BIOS_CPUDebuggerMenu //CPU debugger menu is #94!
	,BIOS_versionInformation //Version information is #95!
};

//Not implemented?
#define NOTIMPLEMENTED NUMITEMS(BIOS_Menus)+1

sword BIOS_Menu = 0; //What menu are we opening (-1 for closing!)?
byte BIOS_SaveStat = 0; //To save the BIOS?
byte BIOS_Changed = 0; //BIOS Changed?

byte BIOS_EnablePlay = 0; //Enable play button=OK?

GPU_TEXTSURFACE *BIOS_Surface; //Our very own BIOS Surface!

int advancedoptions = 0; //Number of advanced options!
byte optioninfo[MENU_MAXITEMS]; //Option info for what option!

//Current architecture that's running!
extern byte is_Compaq; //Are we emulating a Compaq architecture?
extern byte is_XT; //Are we emulating a XT architecture?
extern byte is_PS2; //Are we emulating PS/2 architecture extensions?
extern byte is_i430fx; //Are we emulating a i430fx architecture?


void allocBIOSMenu() //Stuff that take extra video memory etc. for seperated BIOS allocation (so before MMU, because it may take it all)!
{
	if (__HW_DISABLED) return; //Abort!
	BIOS_Surface = alloc_GPUtext(); //Allocate a BIOS Surface!
	if (!BIOS_Surface)
	{
		raiseError("BIOS","Ran out of memory allocating BIOS Screen Layer!");
		return; //Just in case!
	}
	GPU_addTextSurface(BIOS_Surface,NULL); //Register our text surface!
}

byte EMU_Quit = 0; //Quitting emulator?
void freeBIOSMenu() //Free up all BIOS related memory!
{
	if (__HW_DISABLED) return; //Abort!
	GPU_removeTextSurface(BIOS_Surface); //Unregister!
	free_GPUtext(&BIOS_Surface); //Try to deallocate the BIOS Menu surface!
}

byte BIOS_printopentext(uint_32 timeout)
{
	byte result=SETXYCLICKED_OK;
	if (timeout) //Specified? We're before boot!
	{
		GPU_text_locksurface(BIOS_Surface);
		GPU_textgotoxy(BIOS_Surface,0,0); //Goto our location!
		result = GPU_textprintfclickable(BIOS_Surface,getemucol16(0xE),getemucol16(0x0),1, "Press SELECT to bring out the Settings menu");
		GPU_text_releasesurface(BIOS_Surface);
	}
	return result; //Give the result!
}

byte bootBIOS = 0; //Boot into BIOS?

extern byte Settings_request; //Settings requested to be executed?

int CheckBIOSMenu(uint_32 timeout) //To run the BIOS Menus! Result: to reboot?
{
	if (__HW_DISABLED) return 0; //Abort!

	int counter; //Wait X seconds for the BIOS!
	if (timeout) //Time specified before boot?
	{
		counter = timeout; //Wait this long!
	}
	else
	{
		counter = BIOS_TIME; //Default!
	}
	
	exec_showchecksumerrors = 0; //Don't show!
	BIOS_LoadData(); //Now load/reset the BIOS
	exec_showchecksumerrors = 1; //Reset!

	if (!timeout) //Normal opening the BIOS?
	{
		EMU_locktext();
		printmsg(0xE, "Press SELECT to run BIOS SETUP");
		EMU_unlocktext();
	}
	lock(LOCK_INPUT); //Make sure we're responsive to input!
	Settings_request = 2; //Special case: allow even though the BIOS Menu Thread is running!
	unlock(LOCK_INPUT); //Restore input response!
	byte BIOSClicked = 0;
	while (counter>0) //Time left?
	{
		BIOSClicked = (BIOS_printopentext(timeout)&SETXYCLICKED_CLICKED); //Are we clicked?
		counter -= INPUT_INTERVAL; //One further!
		delay(INPUT_INTERVAL); //Intervals of one!
		if (shuttingdown()) //Request shutdown?
		{
			return 1; //Reset, abort if needed!
		}
		lock(LOCK_INPUT);
		if ((psp_inputkey() & BUTTON_SELECT) || BIOS_Settings.firstrun || bootBIOS || FORCE_BIOS || BIOSClicked || (Settings_request==1)) //SELECT trigger pressed or first run? Also when clicked!
		{
			Settings_request = 0; //Requested and handled!
			unlock(LOCK_INPUT);
			bootBIOS = 0; //Not booting into BIOS anymore!
			if (timeout) //Before boot?
			{
				EMU_locktext();
				GPU_EMU_printscreen(0,0,"                                  "); //Clear our text!
				EMU_unlocktext();
			}
			if (runBIOS(!timeout)) //Run the BIOS! Show text if timeout is specified!
			{
				//We're dirty, so reset!
				return 1; //We've to reset!
			}
		}
		unlock(LOCK_INPUT);
	}
	if (timeout)
	{
		EMU_locktext();
		GPU_EMU_printscreen(0,0,"                                           "); //Clear our text!
		EMU_unlocktext();
	}
	lock(LOCK_INPUT);
	Settings_request = 0; //Not inputting anymore!
	unlock(LOCK_INPUT);
	return 0; //No reset!
}

byte EMU_RUNNING = 0; //Emulator is running (are we using the IN-EMULATOR limited menus?) 0=Not running, 1=Running with CPU, 2=Running no CPU (BIOS Menu running?)

void BIOS_clearscreen()
{
    //Clear the framerate surface!
    EMU_clearscreen(); //Clear the screen we're working on!
}

extern GPU_type GPU; //The GPU!

byte reboot_needed = 0; //Default: no reboot needed!

extern sword diagnosticsportoutput_breakpoint; //Breakpoint set?
extern byte backgroundpolicy; //Background task policy. 0=Full halt of the application, 1=Keep running without video and audio muted, 2=Keep running with audio playback, recording muted, 3=Keep running fully without video.
extern byte advancedlog; //Advanced log setting

byte VRAMtype[0x20] = { 0,0,0,0,1,2,3,4,5,0,0,0,0,0,0,0, //ET4000 extension = 0
						0,0,0,0,1,2,6,4,5,0,0,0,0,0,0,0 //ET4000 extension = 1. Extra memory type for ET4000 extension type!
						}; //Redetect VTAM size when changing this value for the detected card!
byte oldVGAMode, oldVGAextensions;

extern byte CDROM_DiskChanged;
extern IODISK disks[0x100]; //All disks available, up go 256 (drive 0-255) disks!
CharacterType oldcdrom0[256];
CharacterType oldcdrom1[256];

extern byte CPUID_mode; //CPUID mode!

void BIOS_MenuChooser(); //The menu chooser prototype for runBIOS!
byte runBIOS(byte showloadingtext) //Run the BIOS menu (whether in emulation or boot is by EMU_RUNNING)!
{
	byte tempstorage;
	if (__HW_DISABLED) return 0; //Abort!
	EMU_stopInput(); //Stop all emu input!
	terminateVGA(); //Terminate currently running VGA for a speed up!
	exec_showchecksumerrors = 0; //Not showing any checksum errors!

//Now reset/save all we need to run the BIOS!
	GPU.show_framerate = 0; //Hide the framerate surface!	

//Now do the BIOS stuff!
	if (showloadingtext) //Not in emulator?
	{
		EMU_textcolor(0xF);
		printmsg(0xF,"\r\nLoading Settings...");
		delay(500000); //0.5 sec!
	}

	stopEMUTimers(); //Stop our timers!
	
	GPU_text_locksurface(frameratesurface);
	GPU_textclearscreen(frameratesurface); //Make sure the surface is empty for a neat BIOS!
	GPU_text_releasesurface(frameratesurface);

	memset(&menuoptions,0,sizeof(menuoptions)); //Init all options that might be used!

	lock(LOCK_MAINTHREAD); //Lock the main thread!
	saveCMOS(); //Make sure the CMOS is properly saved when discarding any changes in the Settings menu.
	unlock(LOCK_MAINTHREAD); //Continue!

	BIOS_LoadData(); //Now load/reset the BIOS
	BIOS_Changed = 0; //Default: the BIOS hasn't been changed!
	BIOS_SaveStat = 0; //Default: not saving!
	exec_showchecksumerrors = 0; //Default: not showing checksum errors!
	BIOS_clearscreen(); //Clear the screen!
	BIOS_Menu = 0; //We're opening the main menu!

	oldVGAMode = BIOS_Settings.VGA_Mode; //Our old VGA mode!
	oldVGAextensions = BIOS_Settings.ET4000_extensions; //Our old VGA extensions!

	reboot_needed = 0; //Do we need to reboot?

	closeLogFile(0); //Close all log files!

	BIOS_MenuChooser(); //Show the BIOS's menu we've selected!

	if (BIOS_Settings.firstrun) //First run?
	{
		BIOS_Settings.firstrun = 0; //Not the first run anymore!
		forceBIOSSave(); //Save: we're not running again!
	}

	if (BIOS_SaveStat && BIOS_Changed) //To save the BIOS and BIOS has been changed?
	{
		EMU_locktext();
		EMU_gotoxy(0, 0); //First column,row!
		EMU_textcolor(0xF);
		GPU_EMU_printscreen(0, 0, "Saving settings...");
		EMU_unlocktext();
		delay(0); //Give us a chance to display!
		if (!BIOS_SaveData()) //Save our options and failed?
		{
			EMU_locktext();
			BIOS_clearscreen(); //Clear the screen!
			EMU_gotoxy(0,0); //First column,row!
			EMU_textcolor(0xF);
			GPU_EMU_printscreen(0,0,"Error: couldn't save the settings!");
			EMU_unlocktext();
			delay(5000000); //Wait 5 sec before rebooting!
		}
		else
		{
			BIOS_clearscreen(); //Clear the screen!

			if (!EMU_RUNNING) //Emulator isn't running?
			{
				EMU_locktext();
				EMU_gotoxy(0,0); //First column,row!
				EMU_textcolor(0xF);
				GPU_EMU_printscreen(0,0,"Settings Saved!   ");
				EMU_unlocktext();
				delay(2000000); //Wait 2 sec before rebooting!
			}
			else //Emulator running?
			{
				EMU_locktext();
				EMU_gotoxy(0,0); //First column,row!
				EMU_textcolor(0xF);
				GPU_EMU_printscreen(0,0,"Settings Saved (Returning to the emulator)!"); //Info!
				EMU_unlocktext();
				delay(2000000); //Wait 2 sec!
			}
		}
	}
	else //Discard changes?
	{
		EMU_locktext();
		EMU_gotoxy(0,0);
		EMU_textcolor(0xF);
		GPU_EMU_printscreen(0,0,"Settings Discarded!"); //Info!
		EMU_unlocktext();
		BIOS_LoadData(); //Reload!
		delay(2000000); //Wait 2 sec!
	}

	BIOSDoneScreen(); //Clean up the screen!
	//Now return to the emulator to reboot!

	tempstorage = CDROM_DiskChanged; //Store temporarily for restoring later!
	CDROM_DiskChanged = 0; //Don't trigger special disk changed actions!
	memset(&oldcdrom0, 0, sizeof(oldcdrom0)); //Init!
	memset(&oldcdrom1, 0, sizeof(oldcdrom1)); //Init!
	safestrcpy(oldcdrom0, sizeof(oldcdrom0), disks[CDROM0].rawfilename); //Save away the old filename!
	safestrcpy(oldcdrom1, sizeof(oldcdrom1), disks[CDROM1].rawfilename); //Save away the old filename!
	iocdrom0("", 0, 1, 0); //Unmount manually!
	iocdrom1("", 0, 1, 0); //Unmount manually!
	BIOS_ValidateData(); //Validate&reload all disks without triggering CD-ROM disk changes!
	iocdrom0(oldcdrom0, 0, 1, 0); //Restore manually!
	iocdrom1(oldcdrom1, 0, 1, 0); //Restore manually!
	CDROM_DiskChanged = tempstorage; //Restore the disk change handler to it's old state!
	BIOS_ValidateData(); //Validate&reload all disks, handling all disk changes normally(if any)!

	//Restore all states saved for the BIOS!
	startEMUTimers(); //Start our timers up again!

	if (shuttingdown()) return 0; //We're shutting down, discard!

	lock(LOCK_MAINTHREAD); //Lock the main thread!
	backgroundpolicy = MIN(BIOS_Settings.backgroundpolicy,3); //Load the new background policy!
	advancedlog = LIMITRANGE(BIOS_Settings.advancedlog, 0, 1);

	startVGA(); //Start the VGA up again!

	EMU_startInput(); //Start all emu input again!

	EMU_update_VGA_Settings(); //Update the VGA Settings to it's default value!

	if (((BIOS_Settings.VGA_Mode!=oldVGAMode)||(BIOS_Settings.ET4000_extensions!=oldVGAextensions)) && (VRAMtype[(BIOS_Settings.VGA_Mode&0xF)|((BIOS_Settings.ET4000_extensions&1)<<4)]==VRAMtype[(oldVGAMode&0xF)|((oldVGAextensions&1)<<4)])) //Mode changed, but not redetecting VRAM?
	{
		VGA_initIO(); //Initialise/update the VGA if needed!
	}
	ssource_setVolume((float)BIOS_Settings.SoundSource_Volume); //Set the current volume!
	GameBlaster_setVolume((float)BIOS_Settings.GameBlaster_Volume); //Set the current volume!
	GPU_AspectRatio(BIOS_Settings.aspectratio); //Keep the aspect ratio?
	setGPUFramerate(BIOS_Settings.ShowFramerate); //Show the framerate?
	diagnosticsportoutput_breakpoint = BIOS_Settings.diagnosticsportoutput_breakpoint; //Set our new breakpoint, if any!
	updateEMUSingleStep(0); //Update the single-step breakpoint!
	updateEMUSingleStep(1); //Update the single-step breakpoint!
	updateEMUSingleStep(2); //Update the single-step breakpoint!
	updateEMUSingleStep(3); //Update the single-step breakpoint!
	updateEMUSingleStep(4); //Update the single-step breakpoint!

	CPUID_mode = *(getarchCPUIDmode()); //CPUID mode!
	unlock(LOCK_MAINTHREAD); //Continue!

	return (reboot_needed&2) || ((reboot_needed&1) && (BIOS_SaveStat && BIOS_Changed)); //Do we need to reboot: when required or chosen!
}

/*

First all global stuff:

*/

//Calculates the middle of the screen!
#define CALCMIDDLE(rowwidth,textwidth) (rowwidth/2)-(textwidth/2)

void printcenter(char *text, int row) //Prints text centered on a row!
{
	if (text)
	{
		EMU_locktext();
		GPU_EMU_printscreen(CALCMIDDLE(BIOS_WIDTH, safe_strlen(text, 256)), row, text); //Show centered text!
		EMU_unlocktext();
	}
}

void printscreencenter(char *text)
{
	if (text) printcenter(text,25/2); //Print text to the middle of the screen!
}

void clearrow(int row)
{
	int i=0; //Index for on-screen characters!
	for (;i<BIOS_WIDTH;) //Clear only one row!
	{
		EMU_locktext();
		GPU_EMU_printscreen(i++,row," "); //Clear BIOS header!
		EMU_unlocktext();
	}
}

extern MMU_type MMU; //Memory unit to detect memory!
void BIOSClearScreen() //Resets the BIOS's screen!
{
	uint_32 memorysize;
	if (__HW_DISABLED) return; //Abort!
	GPU_text_locksurface(frameratesurface);
	GPU_textclearscreen(frameratesurface); //Make sure the surface is empty for a neat BIOS!
	GPU_text_releasesurface(frameratesurface);
	char BIOSText[] = "UniPCemu Settings"; //The BIOS's text!
	
	EMU_textcolor(BIOSTOP_ATTR); //TOP FONT
	clearrow(0); //Clear first row!

	//Clear the rest of the screen!
	EMU_textcolor(BIOS_ATTR_BACKGROUND); //Attr backcolor
	int i=1; //From row 1-25 clearing!
	for (;i<GPU_TEXTSURFACE_HEIGHT;) //Process all rows!
	{
		clearrow(i++); //Clear a row!
	}
	
	//Now the screen is set to go!
	EMU_locktext();
	EMU_textcolor(BIOSTOP_ATTR); //Switch to BIOS Header attribute!
	EMU_unlocktext();
	printcenter(BIOSText,0); //Show the BIOS's text!
	EMU_locktext();
	memorysize = MMU.size; //Memory size to display!
	if (memorysize>=0x100000) //Megabytes?
	{
		GPU_EMU_printscreen(BIOS_WIDTH-safe_strlen("MEM:1234MB",256),0,"MEM:%04iMB",(MMU.size/MBMEMORY)); //Show amount of memory to be able to use!
	}
	else if (memorysize>=0x400) //Kilobytes?
	{
		GPU_EMU_printscreen(BIOS_WIDTH-safe_strlen("MEM:1234KB",256),0,"MEM:%04iKB",(MMU.size/1024)); //Show amount of memory to be able to use!
	}
	else //Bytes?
	{
		GPU_EMU_printscreen(BIOS_WIDTH-safe_strlen(" MEM:1234B",256),0," MEM:%04iB",MMU.size); //Show amount of memory to be able to use!
	}
	EMU_textcolor(BIOS_ATTR_TEXT); //Std: display text!
	EMU_unlocktext();
}

void BIOSDoneScreen() //Cleans up the BIOS's screen!
{
	if (__HW_DISABLED) return; //Abort!
	EMU_textcolor(0x0F); //White on black!
	BIOS_clearscreen();
	EMU_gotoxy(0,0); //First row, first column!
}

/*

First the menu chooser:

*/

void BIOS_InvMenu() //Invalid menu!
{
	char InvMenuText[] = "Invalid menu or this menu is not implemented yet!";
	EMU_textcolor(BIOS_ATTR_TEXT); //Text fontcolor!
	printscreencenter(InvMenuText); //Show the invalid menu text!
	delay(5000000); //Wait 5 seconds before continuing to default menu!
	BIOS_Menu = 0; //Goto main menu!
}

void BIOS_MenuChooser() //The menu chooser!
{
	while (BIOS_Menu!=-1) //Still in the BIOS to open a menu?
	{
		BIOSClearScreen(); //Init the BIOS Background!
		if (BIOS_Menu>=0 && BIOS_Menu<(sword)(NUMITEMS(BIOS_Menus))) //Within range of menus?
		{
			BIOS_Menus[BIOS_Menu](); //Call the menu!
		}
		else
		{
			BIOS_InvMenu(); //Invalid menu!
		}
		if (shuttingdown()) //Are we requesting a shutdown?
		{
			BIOS_SaveStat = 0; //Ignore any changes!
			return; //Shut down!
		}
	}
}

//Now the menus itself:

void BIOS_Title(char *text)
{
	BIOSClearScreen(); //Clear our screen first!
	EMU_textcolor(BIOS_ATTR_TEXT); //Header fontcolor!
	printcenter(getcurrentarchtext(), 1); //Show title text!
	EMU_textcolor(BIOSHEADER_ATTR); //Header fontcolor!
	printcenter(text,2); //Show title text!
}

//allowspecs: allow special keys to break?

//Flags for allowspecs:
//No specs!
#define BIOSMENU_SPEC_NONE 0
//Allow return using CIRCLE
#define BIOSMENU_SPEC_RETURN 1
//Allow L/R/LEFT/RIGHT button for more menus
#define BIOSMENU_SPEC_LR 2
//Allow SQUARE for special adjusted ENTER.
#define BIOSMENU_SPEC_SQUAREOPTION 4


//Results other than valid menu items:
#define BIOSMENU_SPEC_CANCEL -1
//LTRIGGER/LEFT
#define BIOSMENU_SPEC_LTRIGGER -2
//RTRIGGER/RIGHT
#define BIOSMENU_SPEC_RTRIGGER -3

//Stats:
//Plain normal option select or SPEC (see above)!
#define BIOSMENU_STAT_OK 0
//SQUARE option pressed, item selected returned.
#define BIOSMENU_STAT_SQUARE 1

byte BIOS_printscreen(word x, word y, byte attr, char *text, ...)
{
	char buffer[256]; //Going to contain our output data!
	va_list args; //Going to contain the list!
	va_start(args, text); //Start list!
	vsnprintf(buffer,sizeof(buffer), text, args); //Compile list!

	//Now display and return!
	GPU_textgotoxy(BIOS_Surface,x,y); //Goto coordinates!
	return GPU_textprintfclickable(BIOS_Surface,getemucol16(attr&0xF),getemucol16((attr>>4)&0xF),1,buffer); //Give the contents!
}

extern byte GPU_surfaceclicked; //Surface clicked to handle?

int ExecuteMenu(int numitems, int startrow, int allowspecs, word *stat)
{
	*stat = BIOSMENU_STAT_OK; //Plain status for default!
	int key = 0; //Currently pressed key(s)
	int option = 0; //What option to choose?
	byte dirty = 1; //We're dirty! We need to be updated on the screen!
	while ((key!=BUTTON_CONFIRM) && (key!=BUTTON_START)) //Wait for the key to choose something!
	{
		if (shuttingdown()) //Cancel?
		{
			option = BIOSMENU_SPEC_CANCEL;
			break;
		}
		lock(LOCK_INPUT);
		key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input a key with delay!
		unlock(LOCK_INPUT);
		if ((key & BUTTON_UP)>0) //Up pressed?
		{
			if (option>0) //Past first?
			{
				option--; //Previous option!
				dirty = 1; //We're dirty!
			}
			else //Top option?
			{
				if (numitems>1) //More than one item?
				{
					option = numitems-1; //Goto bottom item!
					dirty = 1; //We're dirty!
				}
			}
		}
		else if ((key & BUTTON_DOWN)>0) //Down pressed?
		{
			if (option<(numitems-1)) //Not last item?
			{
				option++; //Next option!
				dirty = 1; //We're dirty!
			}
			else if (numitems>1) //Last item?
			{
				option = 0; //Goto first item from bottom!
				dirty = 1; //We're dirty!
			}
		}
		else if (((key & BUTTON_CANCEL)>0) && ((allowspecs&BIOSMENU_SPEC_RETURN)>0)) //Cancel pressed and allowed?
		{
			option = BIOSMENU_SPEC_CANCEL; //Cancelled!
			break; //Exit loop!
		}
		else if (
		    ( //Keys pressed?
		        (((key & BUTTON_LTRIGGER)>0) || ((key & BUTTON_RTRIGGER)>0)) ||
		        (((key & BUTTON_LEFT)>0) || ((key & BUTTON_RIGHT)>0))
		    )
		    && ((allowspecs&BIOSMENU_SPEC_LR)>0)) //L/R/LEFT/RIGHT pressed and allowed?
		{
			if (((key & BUTTON_LTRIGGER)>0) || ((key & BUTTON_LEFT)>0)) //LTRIGGER/LEFT?
			{
				option = BIOSMENU_SPEC_LTRIGGER; //LTRIGGER!
				break; //Exit loop!
			}
			else if (((key & BUTTON_RTRIGGER)>0) || ((key & BUTTON_RIGHT)>0)) //RTRIGGER/RIGHT?
			{
				option = BIOSMENU_SPEC_RTRIGGER; //RTRIGGER!
				break; //Exit loop!
			}
		}
		else if (((key&BUTTON_SQUARE)>0) && ((allowspecs&BIOSMENU_SPEC_SQUAREOPTION)>0)) //SQUARE and allowed?
		{
			*stat = BIOSMENU_STAT_SQUARE; //Square special option!
			break; //Exit loop!
		}

		lock(LOCK_INPUT);
		if ((GPU_surfaceclicked&0x81)==1) //We're signalled?
		{
			dirty = 1; //We're dirty!
			GPU_surfaceclicked |= 0x80; //We acnowledge this action!
		}
		unlock(LOCK_INPUT);

//Now that the options have been chosen, show them:

		if (dirty) //Do we need to update the screen and check for input?
		{
			dirty = 0; //Acnowledge being acted upon!
			int cur = 0; //Current option
			char selected[2][256] = { "  %s","> %s" }; //Selector!
			char *selector;
			byte selectorattribute;
			EMU_locktext();
			cur = 0; //Initilialize current item!
			do //Process all options!
			{
				selector = selected[(cur == option) ? 1 : 0]; //The current selector to use!
				selectorattribute = (cur == option) ?BIOS_ATTR_ACTIVE:BIOS_ATTR_INACTIVE; //Active/inactive selector!
				if (BIOS_printscreen(0,startrow+cur,selectorattribute,selector,menuoptions[cur])&SETXYCLICKED_CLICKED) //Clicked?
				{
					EMU_unlocktext();
					return cur; //This item has been chosen!
				}
			} while (++cur<MIN(numitems,MENU_MAXITEMS));
			EMU_unlocktext();
		}
	}
	while ((key == BUTTON_CONFIRM) || (key == BUTTON_START)) //Wait for the key to release something!
	{
		if (shuttingdown()) //Cancel?
		{
			option = BIOSMENU_SPEC_CANCEL;
			break;
		}
		lock(LOCK_INPUT);
		key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input a key with delay!
		unlock(LOCK_INPUT);
	}
	return option; //Give the chosen option!
}

//File list functions!

//Amount of files in the list MAX
char itemlist[ITEMLIST_MAXITEMS][256]; //Max X files listed!
char dirlist[ITEMLIST_MAXITEMS][256]; //Max X files listed!
word numlist = 0; //Number of files!
word numdirlist = 0; //Number of files!

void clearList()
{
	memset(&itemlist,0,sizeof(itemlist)); //Init!
	numlist = 0; //Nothin in there yet!
}

void clearDirList()
{
	memset(&dirlist,0,sizeof(dirlist)); //Init!
	numdirlist = 0; //Nothin in there yet!
}

void addList(char *text)
{
	if (numlist<ITEMLIST_MAXITEMS) //Maximum not reached yet?
	{
		safestrcpy(itemlist[numlist++],sizeof(itemlist[0]),text); //Add the item and increase!
	}
}

byte addDirList(char *text)
{
	if (numdirlist<ITEMLIST_MAXITEMS) //Maximum not reached yet?
	{
		safestrcpy(dirlist[numdirlist++],sizeof(dirlist[0]),text); //Add the item and increase!
		return 1; //OK!
	}
	return 0; //Ran out of room!
}

void sortDirList() //Sort the directory list!
{
	int_32 curlistx, curlisty;
	char temp[256]; //Temporary storage!
	memset(&temp,0,sizeof(temp)); //Init!
	for (curlistx=0;curlistx<numdirlist;++curlistx)
	{
		for (curlisty=0;curlisty<numdirlist;++curlisty)
		{
			if (curlisty<(numdirlist-1)) //Within range to test forward?
			{
				if (strcmp(dirlist[curlisty],dirlist[curlisty+1])>0) //Different? We're past the string? We're to move the item up!
				{
					safestrcpy(&temp[0],sizeof(temp),&dirlist[curlisty+1][0]); //Load next item to swap in!
					safestrcpy(&dirlist[curlisty+1][0],sizeof(dirlist[0]),&dirlist[curlisty][0]); //Move the current item up!
					safestrcpy(&dirlist[curlisty][0],sizeof(dirlist[0]),&temp[0]); //Move the next item to the current item!
				}
			}
		}
	}
}

//Generate file list based on extension!
void generateFileList(char *path, char *extensions, int allowms0, int allowdynamic, byte isdiskimage)
{
	byte is_dynamic;
	uint_32 curdirlist; //Current directory list item!
	numlist = 0; //Reset amount of files!
	clearDirList(); //Clear the list!
	if (allowms0) //Allow Memory Stick option?
	{
        #ifdef IS_PSP
               addList("ms0:"); //Add filename (Memory Stick)!
        #endif
	}
	char direntry[256];
	byte isfile;
	DirListContainer_t dir;
	if (opendirlist(&dir,path,&direntry[0],&isfile,1))
	{
		/* print all the files and directories within directory */
		do //Files left to check?
		{
			if (isfile) //It's a file?
			{
				if (isext(direntry, extensions)) //Check extension!
				{
					int allowed = 0;
					if (isdiskimage) //Is it a disk image? Then check for dynamic types!
					{
						is_dynamic = is_dynamicimage(direntry); //Is it a dynamic image?
						allowed = ((allowdynamic && is_dynamic) || (!is_dynamic)); //Allowed when not dynamic or dynamic is allowed!
					}
					else
					{
						allowed = 1; //Always allowed for non-disk images!
					}
					if (allowed) //Allowed?
					{
						if (!addDirList(direntry)) //Set filename!
						{
							goto nodirlistroom;
						}
					}
				}
			}
		}
		while (readdirlist(&dir,&direntry[0],&isfile)); //Files left to check?)
		nodirlistroom: //No more room left in the directory list?
		clearList(); //Clear the list!
		sortDirList(); //Sort the directory list!
		for (curdirlist=0;curdirlist<numdirlist;++curdirlist) //Add all to our final list!
		{
			addList(&dirlist[curdirlist][0]); //Add directory items!
		}
		closedirlist(&dir);
	}
}

int cmpinsensitive(char *str1, char *str2, uint_32 maxlen) //Compare, case insensitive!
{
	if (str1==NULL || str2==NULL) return 0; //Error: give not equal!
	if (safe_strlen(str1,maxlen)!=safe_strlen(str2,maxlen)) //Not equal in length?
	{
		return 0; //Not equal in length!
	}
	int length = safe_strlen(str1,maxlen); //Length!
	int counter = 0;
	while (toupper((int)*str1)==toupper((int)*str2) && *str1!='\0' && counter<length) //Equal and no overflow?
	{
		str1++; //Next character!
		str2++; //Next character!
		counter++; //For overflow check!
	}
	if (counter==length && (toupper((int)*str1)==toupper((int)*str2))) //Whole string checked and equal?
	{
		return 1; //Equal!
	}
	return 0; //Not equal!
}

void printCurrent(int x, int y, char *text, int maxlen, list_information information_handler) //Create the current item with maximum length!
{
	char buffer[1024]; //The buffered text!
	char filler[1024]; //The filler data!
	memset(&buffer,'\0',sizeof(buffer)); //Init buffer to unused!
	memset(&filler,'\0',sizeof(filler)); //Init filler to unused!
	int i,j;
	int max = safe_strlen(text,256); //Default: maximum the size of the destination!
	if (max>maxlen) //More than maximum length?
	{
		max = maxlen; //Truncate to maximum length!
	}
	if (max>(int)(sizeof(buffer)-1)) //Too much?
	{
		max = sizeof(buffer)-1; //Limit to buffer!
	}
	//First: compose text with limit!
	for (i=0;i<max;) //Process the entire length, up to maximum processable length!
	{
		buffer[i] = text[i]; //Set as text!
		++i; //Next item!
	}
	//Next: compose filler!
	j = 0; //Init second filler!
	int max2 = maxlen; //Maximum length!
	if (max2>(int)(sizeof(filler)-1)) //Limit breached?
	{
		max2 = sizeof(filler)-1; //Limit!
	}
	for (;i<max2;i++) //Anything left?
	{
		filler[j++] = ' '; //Fill filler!
	}
	
	//Finally, print the output!
	EMU_locktext();
	EMU_textcolor(BIOS_ATTR_ACTIVE); //Active item!
	GPU_EMU_printscreen(x,y,"%s",buffer); //Show item with maximum length or less!

	EMU_textcolor(BIOS_ATTR_BACKGROUND); //Background of the current item!
	GPU_EMU_printscreen(-1,-1,"%s",filler); //Show rest with filler color, update!
	if (information_handler) //Gotten an information handler?
	{
		information_handler(text); //Execute the information handler!
	}
	EMU_unlocktext();
}

//x,y = coordinates of file list
//maxlen = amount of characters for the list (width of the list)

int ExecuteList(int x, int y, char *defaultentry, int maxlen, list_information informationhandler, int blockActions) //Runs the file list!
{
	char currentstart;
	int resultcopy;
	int key = 0;
	//First, no file check!
	if (!numlist) //No files?
	{
		EMU_locktext();
		EMU_gotoxy(x,y); //Goto position of output!
		EMU_textcolor(BIOS_ATTR_TEXT); //Plain text!
		GPU_EMU_printscreen(x,y,"No files found!");
		EMU_unlocktext();
		return FILELIST_NOFILES; //Error: no files found!
	}

//Now, find the default!
	int result = 0; //Result!
	for (result=0; result<numlist; result++) //Search for our file!
	{
		if (cmpinsensitive(itemlist[result],defaultentry,sizeof(itemlist[result]))) //Default found?
		{
			break; //Use this default!
		}
	}
	if (!cmpinsensitive(itemlist[result],defaultentry,sizeof(itemlist[result])) || strcmp(defaultentry,"")==0) //Default not found or no default?
	{
		result = 0; //Goto first file: we don't have the default!
	}

	printCurrent(x,y,itemlist[result],maxlen,informationhandler); //Create our current entry!
	
	while (1) //Doing selection?
	{
		if (shuttingdown()) //Cancel?
		{
			return FILELIST_CANCEL; //Cancel!
		}
		lock(LOCK_INPUT);
		key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
		unlock(LOCK_INPUT);

		if ((key&BUTTON_UP)>0) //UP?
		{
			if (result>0) //Not first?
			{
				--result; //Up one item!
			}
			else //First?
			{
				result = numlist-1; //Bottom of the list!
			}
			printCurrent(x,y,itemlist[result],maxlen,informationhandler); //Create our current entry!
		}
		else if ((key&BUTTON_DOWN)>0) //DOWN?
		{
			if (result<(numlist-1)) //Not at the bottom?
			{
				++result; //Down one item!
			}
			else //At the bottom?
			{
				result = 0; //Top of the list!
			}
			printCurrent(x, y, itemlist[result], maxlen,informationhandler); //Create our current entry!
		}
		else if ((key&BUTTON_LEFT)>0) //LEFT?
		{
			currentstart = itemlist[result][0]; //The character to check against!
			resultcopy = result; //What item to go to!
			for (;((resultcopy>0) && (currentstart==itemlist[resultcopy][0]));--resultcopy); //While still the same? Scroll up until we don't or reach the first item!
			if (itemlist[resultcopy][0]!=currentstart) //Valid result?
			{
				result = resultcopy; //Go to the item that's found!
			}
			printCurrent(x,y,itemlist[result],maxlen,informationhandler); //Create our current entry!
		}
		else if ((key&BUTTON_RIGHT)>0) //RIGHT?
		{
			currentstart = itemlist[result][0]; //The character to check against!
			resultcopy = result; //What item to go to!
			for (;((resultcopy<(numlist-1)) && (currentstart==itemlist[resultcopy][0]));++resultcopy); //While still the same? Scroll up until we don't or reach the first item!
			if (itemlist[resultcopy][0]!=currentstart) //Valid result?
			{
				result = resultcopy; //Go to the item that's found!
			}
			printCurrent(x, y, itemlist[result], maxlen,informationhandler); //Create our current entry!
		}
		else if (((key&(~blockActions)&(BUTTON_CONFIRM|BUTTON_START))>0) || (key&BUTTON_PLAY && BIOS_EnablePlay)) //OK?
		{
			delay(500000); //Wait a bit before continuing!
			return result; //Give the result!
		}
		else if ((key&BUTTON_CANCEL)>0) //CANCEL?
		{
			return FILELIST_CANCEL; //Cancelled!
		}
		else if ((key&(~blockActions)&BUTTON_TRIANGLE)>0) //DEFAULT?
		{
			return FILELIST_DEFAULT; //Unmount!
		}
	}
}

int_64 InputPortNumber(byte x, byte y, word PortNumber) //Retrieve the size, or 0 for none!
{
	int key = 0;
	lock(LOCK_INPUT);
	key = psp_inputkeydelay(BIOS_INPUTDELAY);
	unlock(LOCK_INPUT);
	while ((key & (BUTTON_CONFIRM | BUTTON_START)) > 0) //Pressed? Wait for release!
	{
		lock(LOCK_INPUT);
		key = psp_inputkeydelay(BIOS_INPUTDELAY);
		unlock(LOCK_INPUT);
	}
	word result = PortNumber; //Size: result; default 0 for none! Must be a multiple of 4096 bytes for HDD!
	word oldvalue; //To check for high overflow!
	for (;;) //Get input; break on error!
	{
		EMU_locktext();
		EMU_textcolor(BIOS_ATTR_ACTIVE); //We're using active color for input!
		GPU_EMU_printscreen(x, y, "%u     ", result); //Show current size!
		EMU_unlocktext();
		lock(LOCK_INPUT);
		key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
		unlock(LOCK_INPUT);

		//1GB steps!
		if ((key & BUTTON_LTRIGGER) > 0) //1000 step down?
		{
			if (result == 0) {}
			else
			{
				oldvalue = result; //Load the old value!
				result -= (key & BUTTON_RIGHT) ? 100000 : ((key & BUTTON_LEFT) ? 10000 : 1000); //x100 or x10 or x1!
				if (result > oldvalue) result = 0; //Underflow!
			}
		}
		else if ((key & BUTTON_RTRIGGER) > 0) //1000 step up?
		{
			oldvalue = result; //Save the old value!
			result += (key & BUTTON_RIGHT) ? 100000 : ((key & BUTTON_LEFT) ? 10000 : 1000); //x100 or x10 or x1!
			if (result < oldvalue) result = oldvalue; //We've overflown?
		}
		else if ((key & BUTTON_DOWN) > 0) //1 step up?
		{
			if (result == 0) {}
			else
			{
				oldvalue = result;
				result -= (key & BUTTON_RIGHT) ? 100 : ((key & BUTTON_LEFT) ? 10 : 1); //x100 or x10 or x1!
				if (result > oldvalue) result = 0; //Underflow!
			}
		}
		else if ((key & BUTTON_UP) > 0) //1 step down?
		{
			oldvalue = result; //Save the old value!
			result += (key & BUTTON_RIGHT) ? 100 : ((key & BUTTON_LEFT) ? 10 : 1); //x100 or x10 or x1!
			if (result < oldvalue) result = oldvalue; //We've overflown?
		}
		//Confirmation buttons etc.
		else if ((key & (BUTTON_CONFIRM | BUTTON_START)) > 0)
		{
			while ((key & (BUTTON_CONFIRM | BUTTON_START)) > 0) //Wait for release!
			{
				lock(LOCK_INPUT);
				key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
				unlock(LOCK_INPUT);
			}
			return (int_64)result;
		}
		else if ((key & BUTTON_CANCEL) > 0)
		{
			while ((key & BUTTON_CANCEL) > 0) //Wait for release!
			{
				lock(LOCK_INPUT);
				key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
				unlock(LOCK_INPUT);
			}
			break; //Cancel!
		}
		else if ((key & BUTTON_TRIANGLE) > 0)
		{
			while ((key & BUTTON_TRIANGLE) > 0) //Wait for release!
			{
				lock(LOCK_INPUT);
				key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
				unlock(LOCK_INPUT);
			}
			return FILELIST_DEFAULT; //Default!
		}
		else if (shuttingdown()) break; //Cancel because of shutdown?
	}
	return FILELIST_CANCEL; //No size: cancel!
}

extern RAW_INPUTSTATUS input;

int_64 InputByteNumber(byte x, byte y, byte ByteNumber, byte showanalogdiverging) //Retrieve the size, or 0 for none!
{
	int key = 0;
	lock(LOCK_INPUT);
	key = psp_inputkeydelay(BIOS_INPUTDELAY);
	unlock(LOCK_INPUT);
	while ((key & (BUTTON_CONFIRM | BUTTON_START)) > 0) //Pressed? Wait for release!
	{
		lock(LOCK_INPUT);
		key = psp_inputkeydelay(BIOS_INPUTDELAY);
		unlock(LOCK_INPUT);
	}
	byte result = ByteNumber; //Size: result; default 0 for none! Must be a multiple of 4096 bytes for HDD!
	int_32 xrel, yrel;
	byte oldvalue; //To check for high overflow!
	for (;;) //Get input; break on error!
	{
		lock(LOCK_INPUT);
		key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
		xrel = input.Lx; //Raw X divergeance from hardware!
		yrel = input.Ly; //Raw Y divergeance from hardware!
		unlock(LOCK_INPUT);

		if (xrel < 0) xrel = -xrel; //Positive only!
		if (yrel < 0) yrel = -yrel; //Positive only!

		EMU_locktext();
		EMU_textcolor(BIOS_ATTR_ACTIVE); //We're using active color for input!
		GPU_EMU_printscreen(x, y, "%u     ", (result<<8)); //Show current size!
		if (showanalogdiverging)
		{
			EMU_textcolor(BIOS_ATTR_TEXT); //We're using active color for input!
			GPU_EMU_printscreen(x, y + 2, "Currently diverging %u     ", (MAX(xrel, yrel)));
		}
		EMU_unlocktext();

		if ((key & BUTTON_DOWN) > 0) //1 step up?
		{
			if (result == 0) {}
			else
			{
				oldvalue = result;
				result -= (key & BUTTON_RIGHT) ? 100 : ((key & BUTTON_LEFT) ? 10 : 1); //x100 or x10 or x1!
				if (result > oldvalue) result = 0; //Underflow!
			}
		}
		else if ((key & BUTTON_UP) > 0) //1 step down?
		{
			oldvalue = result; //Save the old value!
			result += (key & BUTTON_RIGHT) ? 100 : ((key & BUTTON_LEFT) ? 10 : 1); //x100 or x10 or x1!
			if (result < oldvalue) result = oldvalue; //We've overflown?
		}
		//Confirmation buttons etc.
		else if ((key & (BUTTON_CONFIRM | BUTTON_START)) > 0)
		{
			while ((key & (BUTTON_CONFIRM | BUTTON_START)) > 0) //Wait for release!
			{
				lock(LOCK_INPUT);
				key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
				unlock(LOCK_INPUT);
			}
			return (int_64)result;
		}
		else if ((key & BUTTON_CANCEL) > 0)
		{
			while ((key & BUTTON_CANCEL) > 0) //Wait for release!
			{
				lock(LOCK_INPUT);
				key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
				unlock(LOCK_INPUT);
			}
			break; //Cancel!
		}
		else if ((key & BUTTON_TRIANGLE) > 0)
		{
			while ((key & BUTTON_TRIANGLE) > 0) //Wait for release!
			{
				lock(LOCK_INPUT);
				key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
				unlock(LOCK_INPUT);
			}
			return FILELIST_DEFAULT; //Default!
		}
		else if (shuttingdown()) break; //Cancel because of shutdown?
	}
	return FILELIST_CANCEL; //No size: cancel!
}

byte selectingHDD = 0;
void hdd_information(char *filename) //Displays information about a harddisk to mount!
{
	char path[256];
	memset(&path,0,sizeof(path));
	safestrcpy(path,sizeof(path),diskpath);
	safestrcat(path,sizeof(path),"/");
	safestrcat(path,sizeof(path),filename);
	FILEPOS size;
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	word c,h,s;
	if (is_dynamicimage(path)) //Dynamic image?
	{
		size = dynamicimage_getsize(path); //Get the filesize!
		GPU_EMU_printscreen(0, 6, "This is a Superfury Dynamic Disk Image file."); //Show selection init!
		GPU_EMU_printscreen(0, 7, "Disk size: %08i MB %04i KB", (uint_32)(size / MBMEMORY), (uint_32)((size % MBMEMORY) / 1024)); //Show size too!
		GPU_EMU_printscreen(0, 8, "                                  "); //Clear file size info!
		if (selectingHDD && dynamicimage_getgeometry(path,&c,&h,&s)) //HDD?
		{
			GPU_EMU_printscreen(0, 8, "Geometry(C,H,S): %i,%i,%i", c, h, s); //Show geometry too!
		}
		else
			GPU_EMU_printscreen(0, 8, "                                  "); //Clear file size info!
	}
	else if (is_DSKimage(path)) //DSK disk image?
	{
		DISKINFORMATIONBLOCK dskinfo;
		if (!readDSKInfo(path, &dskinfo)) goto unknownimage;
		size = dskinfo.NumberOfSides*dskinfo.NumberOfTracks*dskinfo.TrackSize; //Get the total disk image size!
		size = dynamicimage_getsize(path); //Get the filesize!
		GPU_EMU_printscreen(0, 6, "This is a DSK disk image file.              "); //Show selection init!
		GPU_EMU_printscreen(0, 7, "Disk size: %08i MB %04i KB", (uint_32)(size / MBMEMORY), (uint_32)((size % MBMEMORY) / 1024)); //Show size too!
		GPU_EMU_printscreen(0, 8, "                                  "); //Clear file size info!
	}
	else if (is_IMDimage(path)) //IMD disk image?
	{
		IMDIMAGE_SECTORINFO sectorinfo;
		if (!readIMDDiskInfo(path,&sectorinfo)) goto unknownimage;
		GPU_EMU_printscreen(0, 6, "This is a IMD disk image file.              "); //Show selection init!
		GPU_EMU_printscreen(0, 7, "                              "); //Don't show size!
		GPU_EMU_printscreen(0, 8, "                                  "); //Clear file size info!
	}
	else if (is_staticimage(path)) //Static image?
	{
		size = staticimage_getsize(path); //Get the filesize!
		GPU_EMU_printscreen(0, 6, "This is a Static disk image file.           "); //Show selection init!
		GPU_EMU_printscreen(0, 7, "Disk size: %08i MB %04i KB", (uint_32)(size / MBMEMORY), (uint_32)((size % MBMEMORY) / 1024)); //Show size too!
		GPU_EMU_printscreen(0, 8, "                                  "); //Clear file size info!
		if (selectingHDD && staticimage_getgeometry(path,&c,&h,&s)) //HDD?
		{
			GPU_EMU_printscreen(0, 8, "Geometry(C,H,S): %i,%i,%i", c, h, s); //Show geometry too!
		}
		else
			GPU_EMU_printscreen(0, 8, "                                  "); //Clear file size info!
	}
	else //Unknown file type: no information?
	{
	unknownimage: //Unknown disk image?
		GPU_EMU_printscreen(0, 6, "This is an unknown disk image file.         "); //Show selection init!
		GPU_EMU_printscreen(0, 7, "                              "); //Clear file size info!
		GPU_EMU_printscreen(0, 8, "                                  "); //Clear file size info!
	}
}

//Menus itself:

int BIOS_dummyfile=0;
void BIOS_noentries(sword x, sword y, char* message)
{
	numlist = 0; //Reset amount of files!
	clearList(); //Clear the list!
	addList(message); //Add the message as the only item!
	BIOS_dummyfile = ExecuteList(x, y, itemlist[0], 256, NULL, (BUTTON_CONFIRM|BUTTON_START|BUTTON_TRIANGLE)); //Show options for the installed CPU!
	//Ignore the result, nothing to do with it anyways(as nothing is to be selected)!
}

//Selection menus for disk drives!

void BIOS_disk_nofiles()
{
	BIOS_noentries(12, 4, "<No disk images present>");
}

void BIOS_floppy0_selection() //FLOPPY0 selection menu!
{
	BIOS_Title("Mount FLOPPY A");
	generateFileList(diskpath,"img|ima|dsk|imd",0,0,1); //Generate file list for all .img files!
	EMU_locktext();
	EMU_gotoxy(0,4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0,4,"Disk image: "); //Show selection init!
	EMU_unlocktext();

	selectingHDD = 0; //Not selecting a HDD!
	int file = ExecuteList(12,4,BIOS_Settings.floppy0,256,&hdd_information,0); //Show menu for the disk image!
	switch (file) //Which file?
	{
	case FILELIST_NOFILES: //No files?
		BIOS_disk_nofiles(); //No files!
	case FILELIST_DEFAULT: //Unmount?
		BIOS_Changed = 1; //Changed!
		safestrcpy(BIOS_Settings.floppy0,sizeof(BIOS_Settings.floppy0),""); //Unmount!
		BIOS_Settings.floppy0_readonly = 0; //Not readonly!
		break;
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break;
	default: //File?
		BIOS_Changed = 1; //Changed!
		//if (strcmp(BIOS_Settings.floppy0,itemlist[file])!=0) BIOS_Settings.floppy0_readonly = 0; //Different resets readonly flag!
		safestrcpy(BIOS_Settings.floppy0,sizeof(BIOS_Settings.floppy0),itemlist[file]); //Use this file!
	}
	BIOS_Menu = 1; //Return to image menu!
}

void BIOS_floppy1_selection() //FLOPPY1 selection menu!
{
	BIOS_Title("Mount FLOPPY B");
	generateFileList(diskpath,"img|ima|dsk|imd",0,0,1); //Generate file list for all .img files!
	EMU_locktext();
	EMU_gotoxy(0,4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0,4,"Disk image: "); //Show selection init!
	EMU_unlocktext();
	selectingHDD = 0; //Not selecting a HDD!
	int file = ExecuteList(12,4,BIOS_Settings.floppy1,256,&hdd_information,0); //Show menu for the disk image!
	switch (file) //Which file?
	{
	case FILELIST_NOFILES: //No files?
		BIOS_disk_nofiles(); //No files!
	case FILELIST_DEFAULT: //Unmount?
		BIOS_Changed = 1; //Changed!
		safestrcpy(BIOS_Settings.floppy1,sizeof(BIOS_Settings.floppy1),""); //Unmount!
		BIOS_Settings.floppy0_readonly = 0; //Different resets readonly flag!
		break;
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	default: //File?
		BIOS_Changed = 1; //Changed!
		//if (strcmp(BIOS_Settings.floppy1, itemlist[file]) != 0) BIOS_Settings.floppy1_readonly = 0; //Different resets readonly flag!
		safestrcpy(BIOS_Settings.floppy1,sizeof(BIOS_Settings.floppy1),itemlist[file]); //Use this file!
	}
	BIOS_Menu = 1; //Return to image menu!
}

void BIOS_hdd0_selection() //HDD0 selection menu!
{
	int blockedactions;
	blockedactions = 0; //All allowed!
	if (EMU_RUNNING) //Plain select and not running (hard disks cannot be unmounted/changed during runtime)?
	{
		blockedactions |= (BUTTON_CONFIRM|BUTTON_START|BUTTON_TRIANGLE); //Don't allow changing the option when mounted and running!
	}
	//HDD is never allowed to change during running emulation!
	BIOS_Title("Mount First HDD");
	generateFileList(diskpath,"img|sfdimg",1,1,1); //Generate file list for all .img files!
	EMU_locktext();
	EMU_gotoxy(0,4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0,4,"Disk image: "); //Show selection init!
	EMU_unlocktext();
	selectingHDD = 1; //Selecting a HDD!
	int file = ExecuteList(12,4,BIOS_Settings.hdd0,256,&hdd_information,blockedactions); //Show menu for the disk image!
	switch (file) //Which file?
	{
	case FILELIST_NOFILES: //No files?
		BIOS_disk_nofiles(); //No files!
	case FILELIST_DEFAULT: //Unmount?
		BIOS_Changed = 1; //Changed!
		reboot_needed |= 1; //We need to reboot to apply the ATA changes!
		BIOS_Settings.hdd0_readonly = 0; //Different resets readonly flag!
		safestrcpy(BIOS_Settings.hdd0,sizeof(BIOS_Settings.hdd0),""); //Unmount!
		break;
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	default: //File?
		BIOS_Changed = 1; //Changed!
		reboot_needed |= 1; //We need to reboot to apply the ATA changes!
		//if (strcmp(BIOS_Settings.hdd0, itemlist[file]) != 0) BIOS_Settings.hdd0_readonly = 0; //Different resets readonly flag!
		safestrcpy(BIOS_Settings.hdd0,sizeof(BIOS_Settings.hdd0),itemlist[file]); //Use this file!
	}
	BIOS_Menu = 1; //Return to image menu!
}

void BIOS_hdd1_selection() //HDD1 selection menu!
{
	int blockedactions;
	blockedactions = 0;
	if (EMU_RUNNING) //Plain select and not running (hard disks cannot be unmounted/changed during runtime)?
	{
		blockedactions |= (BUTTON_CONFIRM | BUTTON_START | BUTTON_TRIANGLE); //Don't allow changing the option when mounted and running!
	}
	//HDD is never allowed to change during running emulation!
	BIOS_Title("Mount Second HDD");
	generateFileList(diskpath,"img|sfdimg",1,1,1); //Generate file list for all .img files!
	EMU_locktext();
	EMU_gotoxy(0,4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0,4,"Disk image: "); //Show selection init!
	EMU_unlocktext();
	selectingHDD = 1; //Selecting a HDD!
	int file = ExecuteList(12,4,BIOS_Settings.hdd1,256,&hdd_information,blockedactions); //Show menu for the disk image!
	switch (file) //Which file?
	{
	case FILELIST_NOFILES: //No files?
		BIOS_disk_nofiles(); //No files!
	case FILELIST_DEFAULT: //Unmount?
		BIOS_Changed = 1; //Changed!
		reboot_needed |= 1; //We need to reboot to apply the ATA changes!
		BIOS_Settings.hdd1_readonly = 0; //Different resets readonly flag!
		safestrcpy(BIOS_Settings.hdd1,sizeof(BIOS_Settings.hdd1),""); //Unmount!
		break;
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	default: //File?
		BIOS_Changed = 1; //Changed!
		reboot_needed |= 1; //We need to reboot to apply the ATA changes!
		//if (strcmp(BIOS_Settings.hdd1, itemlist[file]) != 0) BIOS_Settings.hdd1_readonly = 0; //Different resets readonly flag!
		safestrcpy(BIOS_Settings.hdd1,sizeof(BIOS_Settings.hdd1),itemlist[file]); //Use this file!
	}
	BIOS_Menu = 1; //Return to image menu!
}

extern byte CDROM_channel; //Default: no CD-ROM channel!

void BIOS_LoadEjectCDROM0() //Load/Eject CD-ROM 0!
{
	byte ejectstatus;
	lock(LOCK_MAINTHREAD);
	if (CDROM_channel != 0xFF) //Has a channel?
	{
		if ((ejectstatus = ATA_caddyejected(CDROM0))!=0) //Is the caddy ejected or pending to insert?
		{
			if (ejectstatus == 1) //Plain ejected?
			{
				if (ATAPI_insertcaddy(CDROM0)) //Request to be inserted!
				{
					//Do something when it's requested to be inserted?
				}
			}
			//Otherwise, it's requesting to be ejected, don't do anything!
		}
		else //Caddy is inserted?
		{
			if (ATAPI_ejectcaddy(CDROM0)) //Allowed to change? Double as the eject button!
			{
				//Caddy is now requested to be ejected?
			}
		}
	}
	unlock(LOCK_MAINTHREAD);
	BIOS_Menu = 1; //Return to image menu!
}

void BIOS_LoadEjectCDROM1() //Load/Eject CD-ROM 1!
{
	byte ejectstatus;
	lock(LOCK_MAINTHREAD);
	if (CDROM_channel != 0xFF) //Has a channel?
	{
		if ((ejectstatus = ATA_caddyejected(CDROM1)) != 0) //Is the caddy ejected or pending to insert?
		{
			if (ejectstatus == 1) //Plain ejected?
			{
				if (ATAPI_insertcaddy(CDROM1)) //Request to be inserted!
				{
					//Do something when it's requested to be inserted?
				}
			}
			//Otherwise, it's requesting to be ejected, don't do anything!
		}
		else //Caddy is inserted?
		{
			if (ATAPI_ejectcaddy(CDROM1)) //Allowed to change? Double as the eject button!
			{
				//Caddy is now requested to be ejected?
			}
		}
	}
	unlock(LOCK_MAINTHREAD);
	BIOS_Menu = 1; //Return to image menu!
}

byte BIOS_CDROM_commonejectedcaddy(int drive)
{
	byte ejectstatus;
	ejectstatus = 0; //Default: not ejected!
	lock(LOCK_MAINTHREAD);
	if (CDROM_channel != 0xFF) //Has a channel?
	{
		if ((ejectstatus = ATA_caddyejected(drive)) != 0) //Is the caddy ejected or pending to insert?
		{
			//Otherwise, it's requesting to be ejected, don't do anything!
		}
		else //Caddy is inserted?
		{
			ejectstatus = ATAPI_ejectcaddy(drive); //Allowed to change? Double as the eject button!
		}
	}
	unlock(LOCK_MAINTHREAD);
	return ejectstatus; //Ejected or not?
}

void BIOS_cdrom0_selection() //CDROM0 selection menu!
{
	if (BIOS_CDROM_commonejectedcaddy(CDROM0) == 0) goto finishcdrom0;
	if (ATA_allowDiskChange(CDROM0,1)) //Allowed to change? Double as the eject button!
	{
		BIOS_Title("Mount First CD-ROM");
		generateFileList(diskpath,"iso|cue",0,0,1); //Generate file list for all .img files!
		EMU_locktext();
		EMU_gotoxy(0, 4); //Goto 4th row!
		EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
		GPU_EMU_printscreen(0,4,"Disk image: "); //Show selection init!
		EMU_unlocktext();
		int file = ExecuteList(12,4,BIOS_Settings.cdrom0,256,NULL,0); //Show menu for the disk image!
		switch (file) //Which file?
		{
		case FILELIST_NOFILES: //No files?
			BIOS_disk_nofiles(); //No files!
		case FILELIST_DEFAULT: //Unmount?
			BIOS_Changed = 1; //Changed!
			safestrcpy(BIOS_Settings.cdrom0,sizeof(BIOS_Settings.cdrom0),""); //Unmount!
			break;
		case FILELIST_CANCEL: //Cancelled?
			//We do nothing with the selected disk!
			break; //Just calmly return!
		default: //File?
			BIOS_Changed = 1; //Changed!
			safestrcpy(BIOS_Settings.cdrom0,sizeof(BIOS_Settings.cdrom0),itemlist[file]); //Use this file!
		}
	}
	finishcdrom0:
	BIOS_Menu = 1; //Return to image menu!
}

void BIOS_ejectdisk(int disk) //Eject an ejectable disk?
{
	byte ejected = 0;
	switch (disk) //What disk?
	{
	case FLOPPY0: //Floppy 0?
		if (strcmp(BIOS_Settings.floppy0,"")!=0) //Specified?
		{
			safestrcpy(BIOS_Settings.floppy0,sizeof(BIOS_Settings.floppy0), ""); //Clear the option!
			ejected = 1; //We're ejected!
		}
		break;
	case FLOPPY1: //Floppy 1?
		if (strcmp(BIOS_Settings.floppy1, "")!=0) //Specified?
		{
			safestrcpy(BIOS_Settings.floppy1,sizeof(BIOS_Settings.floppy1), ""); //Clear the option!
			ejected = 1; //We're ejected!
		}
		break;
	case CDROM0: //CD-ROM0?
		if (strcmp(BIOS_Settings.cdrom0, "")!=0) //Specified?
		{
			if (ATA_allowDiskChange(disk,2)) //Allowed to be changed?
			{
				//Don't actually unmount it, just leave the drive open to be changed!
				ejected = 1; //We're ejected!
			}
		}
		break;
	case CDROM1: //CD-ROM1?
		if (strcmp(BIOS_Settings.cdrom1, "")!=0) //Specified?
		{
			if (ATA_allowDiskChange(disk,2)) //Allowed to be changed?
			{
				//Don't actually unmount it, just leave the drive open to be changed!
				ejected = 1; //We're ejected!
			}
		}
		break;
	default: //Unsupported disk?
		return; //Abort: invalid disk specified!
		break;
	}
	if (ejected) //Are we ejected at all?
	{
		forceBIOSSave(); //Save the Settings, if needed!
		BIOS_ValidateData(); //Validate&reload all disks!
	}
}

void BIOS_cdrom1_selection() //CDROM1 selection menu!
{
	if (BIOS_CDROM_commonejectedcaddy(CDROM1) == 0) goto finishcdrom1;
	if (ATA_allowDiskChange(CDROM1,1)) //Allowed to change? Double as the eject button!
	{
		BIOS_Title("Mount Second CD-ROM");
		generateFileList(diskpath,"iso|cue",0,0,1); //Generate file list for all .img files!
		EMU_locktext();
		EMU_gotoxy(0,4); //Goto 4th row!
		EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
		GPU_EMU_printscreen(0,4,"Disk image: "); //Show selection init!
		EMU_unlocktext();
		int file = ExecuteList(12,4,BIOS_Settings.cdrom1,256,NULL,0); //Show menu for the disk image!
		switch (file) //Which file?
		{
		case FILELIST_NOFILES: //No files?
			BIOS_disk_nofiles(); //No files!
		case FILELIST_DEFAULT: //Unmount?
			BIOS_Changed = 1; //Changed!
			safestrcpy(BIOS_Settings.cdrom1,sizeof(BIOS_Settings.cdrom1),""); //Unmount!
			break;
		case FILELIST_CANCEL: //Cancelled?
			//We do nothing with the selected disk!
			break; //Just calmly return!
		default: //File?
			BIOS_Changed = 1; //Changed!
			safestrcpy(BIOS_Settings.cdrom1,sizeof(BIOS_Settings.cdrom1),itemlist[file]); //Use this file!
		}
	}
	finishcdrom1:
	BIOS_Menu = 1; //Return to image menu!
}

word Menu_Stat; //Menu status!

byte generateHDD_type = 1; //Generate static/dynamic HDD type!

extern FLOPPY_GEOMETRY floppygeometries[NUMFLOPPYGEOMETRIES];

void BIOS_InitDisksText()
{
	int i;
	for (i=0; i<17; i++)
	{
		memset(&menuoptions[i][0],0,sizeof(menuoptions[i])); //Init!
	}
	safestrcpy(menuoptions[0],sizeof(menuoptions[0]),"Floppy A: ");
	safestrcpy(menuoptions[1],sizeof(menuoptions[0]),"Floppy B: ");
	safestrcpy(menuoptions[2],sizeof(menuoptions[0]),"First HDD: ");
	safestrcpy(menuoptions[3],sizeof(menuoptions[0]),"Second HDD: ");
	safestrcpy(menuoptions[4],sizeof(menuoptions[0]),"First CD-ROM: ");
	safestrcpy(menuoptions[5],sizeof(menuoptions[0]),"Second CD-ROM: ");
	safestrcpy(menuoptions[6], sizeof(menuoptions[0]), "Load/Eject first CD-ROM");
	safestrcpy(menuoptions[7], sizeof(menuoptions[0]), "Load/Eject second CD-ROM");
	safestrcpy(menuoptions[8],sizeof(menuoptions[0]),"Generate Floppy Image");
	safestrcpy(menuoptions[9],sizeof(menuoptions[0]),"Generate IMD Floppy Image");
	safestrcpy(menuoptions[10],sizeof(menuoptions[0]),"Generate Static HDD Image");
	safestrcpy(menuoptions[11],sizeof(menuoptions[0]),"Generate Dynamic HDD Image");
	safestrcpy(menuoptions[12],sizeof(menuoptions[0]), "Convert static to dynamic HDD Image");
	safestrcpy(menuoptions[13],sizeof(menuoptions[0]), "Convert dynamic to static HDD Image");
	safestrcpy(menuoptions[14],sizeof(menuoptions[0]), "Defragment a dynamic HDD Image");
	safestrcpy(menuoptions[15], sizeof(menuoptions[0]), "Floppy A without disk type: ");
	safestrcpy(menuoptions[16], sizeof(menuoptions[0]), "Floppy B without disk type: ");

//FLOPPY0
	if (strcmp(BIOS_Settings.floppy0,"")==0) //No disk?
	{
		safestrcat(menuoptions[0],sizeof(menuoptions[0]),"<NO DISK>"); //Add disk image!
	}
	else
	{
		safestrcat(menuoptions[0],sizeof(menuoptions[0]),BIOS_Settings.floppy0); //Add disk image!
		if (BIOS_Settings.floppy0_readonly) //Read-only?
		{
			safestrcat(menuoptions[0],sizeof(menuoptions[0])," <R>"); //Show readonly tag!
		}
	}

//FLOPPY1
	if (strcmp(BIOS_Settings.floppy1,"")==0) //No disk?
	{
		safestrcat(menuoptions[1],sizeof(menuoptions[1]),"<NO DISK>"); //Add disk image!
	}
	else
	{
		safestrcat(menuoptions[1],sizeof(menuoptions[1]),BIOS_Settings.floppy1); //Add disk image!
		if (BIOS_Settings.floppy1_readonly) //Read-only?
		{
			safestrcat(menuoptions[1],sizeof(menuoptions[1])," <R>"); //Show readonly tag!
		}
	}

//HDD0
	if (strcmp(BIOS_Settings.hdd0,"")==0) //No disk?
	{
		safestrcat(menuoptions[2],sizeof(menuoptions[2]),"<NO DISK>"); //Add disk image!
	}
	else
	{
		safestrcat(menuoptions[2],sizeof(menuoptions[2]),BIOS_Settings.hdd0); //Add disk image!
		if (BIOS_Settings.hdd0_readonly) //Read-only?
		{
			safestrcat(menuoptions[2],sizeof(menuoptions[2])," <R>"); //Show readonly tag!
		}
	}

//HDD1
	if (strcmp(BIOS_Settings.hdd1,"")==0) //No disk?
	{
		safestrcat(menuoptions[3],sizeof(menuoptions[3]),"<NO DISK>"); //Add disk image!
	}
	else
	{
		safestrcat(menuoptions[3],sizeof(menuoptions[3]),BIOS_Settings.hdd1); //Add disk image!
		if (BIOS_Settings.hdd1_readonly) //Read-only?
		{
			safestrcat(menuoptions[3],sizeof(menuoptions[3])," <R>"); //Show readonly tag!
		}
	}

//CDROM0
	if (strcmp(BIOS_Settings.cdrom0,"")==0) //No disk?
	{
		safestrcat(menuoptions[4],sizeof(menuoptions[4]),"<NO DISK>"); //Add disk image!
	}
	else
	{
		safestrcat(menuoptions[4],sizeof(menuoptions[4]),BIOS_Settings.cdrom0); //Add disk image!
	}

//CDROM1
	if (strcmp(BIOS_Settings.cdrom1,"")==0) //No disk?
	{
		safestrcat(menuoptions[5],sizeof(menuoptions[5]),"<NO DISK>"); //Add disk image!
	}
	else
	{
		safestrcat(menuoptions[5],sizeof(menuoptions[5]),BIOS_Settings.cdrom1); //Add disk image!
	}

	CMOSDATA *currentCMOS;
	if (is_i430fx==2) //i440fx?
	{
		currentCMOS = &BIOS_Settings.i440fxCMOS; //We've used!
	}
	else if (is_i430fx==1) //i430fx?
	{
		currentCMOS = &BIOS_Settings.i430fxCMOS; //We've used!
	}
	else if (is_PS2) //PS/2?
	{
		currentCMOS = &BIOS_Settings.PS2CMOS; //We've used!
	}
	else if (is_Compaq)
	{
		currentCMOS = &BIOS_Settings.CompaqCMOS; //We've used!
	}
	else if (is_XT)
	{
		currentCMOS = &BIOS_Settings.XTCMOS; //We've used!
	}
	else //AT?
	{
		currentCMOS = &BIOS_Settings.ATCMOS; //We've used!
	}

	safestrcat(menuoptions[15],sizeof(menuoptions[15]),floppygeometries[currentCMOS->floppy0_nodisk_type].text);
	safestrcat(menuoptions[16],sizeof(menuoptions[16]),floppygeometries[currentCMOS->floppy1_nodisk_type].text);
}


void BIOS_DisksMenu() //Manages the mounted disks!
{
	byte allowsaveresume;
	BIOS_Title("Manage mounted drives");
	BIOS_InitDisksText(); //First, initialise texts!
	int menuresult = ExecuteMenu(17,4,BIOSMENU_SPEC_LR|BIOSMENU_SPEC_SQUAREOPTION|BIOSMENU_SPEC_RETURN,&Menu_Stat); //Show the menu options, allow SQUARE!
	switch (menuresult)
	{
	case BIOSMENU_SPEC_CANCEL: //Return?
		allowsaveresume = (!(reboot_needed & 2)); //Allow (save)&resume?
		if (allowsaveresume) //Allow save&resume?
		{
			if (BIOS_Changed) //Save&resume?
			{
				BIOS_Menu = -1; //Quit!
				BIOS_SaveStat = 1; //Save the BIOS!
			}
			else //Discard&resume?
			{
				BIOS_Menu = -1; //Quit!
				BIOS_SaveStat = 0; //Discard changes!
			}
		}
		//When not allowed to resume, ignore the return!
		break;
	case BIOSMENU_SPEC_LTRIGGER: //L: Main menu?
		BIOS_Menu = 0; //Goto Main menu!
		break;
	case BIOSMENU_SPEC_RTRIGGER: //R: Advanced menu?
		BIOS_Menu = 8; //Goto Advanced Menu!
		break;

	case 0: //First diskette?
		if (Menu_Stat==BIOSMENU_STAT_OK) //Plain select?
		{
			BIOS_Menu = 2; //FLOPPY0 selection!
		}
		else if ((Menu_Stat==BIOSMENU_STAT_SQUARE) && (strcmp(BIOS_Settings.floppy0,"")!=0)) //SQUARE=Trigger readonly!
		{
			BIOS_Changed = 1; //Changed!
			BIOS_Settings.floppy0_readonly = !BIOS_Settings.floppy0_readonly; //Trigger!
		}
		break;
	case 1: //Second diskette?
		if (Menu_Stat==BIOSMENU_STAT_OK) //Plain select?
		{
			BIOS_Menu = 3; //FLOPPY1 selection!
		}
		else if ((Menu_Stat==BIOSMENU_STAT_SQUARE) && (strcmp(BIOS_Settings.floppy1,"")!=0)) //SQUARE=Trigger readonly!
		{
			BIOS_Changed = 1; //Changed!
			BIOS_Settings.floppy1_readonly = !BIOS_Settings.floppy1_readonly; //Trigger!
		}
		break;
	case 2: //First HDD?
		if (Menu_Stat==BIOSMENU_STAT_OK) //Plain select and not running (hard disks cannot be unmounted/changed during runtime)?
		{
			BIOS_Menu = 4; //HDD0 selection!
		}
		else if ((Menu_Stat==BIOSMENU_STAT_SQUARE) && (strcmp(BIOS_Settings.hdd0,"")!=0)) //SQUARE=Trigger readonly!
		{
			BIOS_Changed = 1; //Changed!
			BIOS_Settings.hdd0_readonly = !BIOS_Settings.hdd0_readonly; //Trigger!
		}
		break;
	case 3: //Second HDD?
		if (Menu_Stat==BIOSMENU_STAT_OK)
		{
			BIOS_Menu = 5; //HDD1 selection!
		}
		else if ((Menu_Stat==BIOSMENU_STAT_SQUARE) && (strcmp(BIOS_Settings.hdd1,"")!=0)) //SQUARE=Trigger readonly!
		{
			BIOS_Changed = 1; //Changed!
			BIOS_Settings.hdd1_readonly = !BIOS_Settings.hdd1_readonly; //Trigger!
		}
		break;
	case 4: //First CDROM?
		if (Menu_Stat==BIOSMENU_STAT_OK) //Plain status?
		{
			BIOS_Menu = 6; //CDROM0 selection!
		}
		break;
	case 5: //Second CDROM?
		if (Menu_Stat==BIOSMENU_STAT_OK) //Plain status?
		{
			BIOS_Menu = 7; //CDROM1 selection!
		}
		break;
	case 6: //Load/Eject first CD-ROM button?
		if (Menu_Stat == BIOSMENU_STAT_OK) //Plain status?
		{
			BIOS_Menu = 79; //Defragment a dynamic HDD Image!
		}
		break;
	case 7: //Load/Eject first CD-ROM button?
		if (Menu_Stat == BIOSMENU_STAT_OK) //Plain status?
		{
			BIOS_Menu = 80; //Defragment a dynamic HDD Image!
		}
		break;
	case 8: //Generate Floppy Image?
		if (Menu_Stat==BIOSMENU_STAT_OK) //Plain status?
		{
			BIOS_Menu = 43; //Generate Floppy Image!
		}
		break;
	case 9: //Generate IMD Floppy Image?
		if (Menu_Stat == BIOSMENU_STAT_OK) //Plain status?
		{
			BIOS_Menu = 78; //Generate IMD Floppy Image!
		}
		break;
	case 10: //Generate Static HDD?
		if (Menu_Stat==BIOSMENU_STAT_OK) //Bochs type?
		{
			generateHDD_type = 1; //Bochs type!
			BIOS_Menu = 11; //Generate Static HDD!
		}
		else if (Menu_Stat==BIOSMENU_STAT_SQUARE) //Minimal type?
		{
			generateHDD_type = 3; //Minimal type!
			BIOS_Menu = 11; //Generate Static HDD!
		}
		break;
	case 11: //Generate Dynamic HDD?
		if (Menu_Stat==BIOSMENU_STAT_OK) //Bochs type?
		{
			generateHDD_type = 3; //Bochs type!
			BIOS_Menu = 12; //Generate Dynamic HDD!
		}
		else if (Menu_Stat==BIOSMENU_STAT_SQUARE) //Minimal type?
		{
			generateHDD_type = 2; //Minimal type!
			BIOS_Menu = 12; //Generate Dynamic HDD!
		}
		break;
	case 12: //Convert static to dynamic HDD?
		if (Menu_Stat==BIOSMENU_STAT_OK) //Plain status?
		{
			BIOS_Menu = 19; //Convert static to dynamic HDD!
		}
		break;
	case 13: //Convert dynamic to static HDD?
		if (Menu_Stat==BIOSMENU_STAT_OK) //Plain status?
		{
			BIOS_Menu = 20; //Convert dynamic to static HDD!
		}
		break;
	case 14: //Defragment a dynamic HDD Image?
		if (Menu_Stat==BIOSMENU_STAT_OK) //Plain status?
		{
			BIOS_Menu = 21; //Defragment a dynamic HDD Image!
		}
		break;
	case 15: //Floppy A without disk type?
		if (Menu_Stat == BIOSMENU_STAT_OK) //Plain status?
		{
			BIOS_Menu = 74; //Floppy A without disk type!
		}
		break;
	case 16: //Floppy B without disk type?
		if (Menu_Stat == BIOSMENU_STAT_OK) //Plain status?
		{
			BIOS_Menu = 75; //Floppy B without disk type!
		}
		break;
	case 17: //Load/Eject CD-ROM0 button?
		if (Menu_Stat == BIOSMENU_STAT_OK) //Plain status?
		{
			BIOS_Menu = 21; //Defragment a dynamic HDD Image!
		}
		break;
	default: //Unknown option?
		BIOS_Menu = NOTIMPLEMENTED; //Not implemented yet!
		break;
	}
}

extern char BOOT_ORDER_STRING[15][30]; //Boot order, string values!

void BIOS_BootOrderOption() //Manages the boot order
{
	BIOS_Title("Boot Order");
	EMU_locktext();
	EMU_gotoxy(0,4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0,4,"Boot Order: "); //Show selection init!
	EMU_unlocktext();
	numlist = NUMITEMS(BOOT_ORDER_STRING); //Amount of files (orders)
	int i = 0; //Counter!
	for (i=0; i<numlist; i++) //Process options!
	{
		memset(&itemlist[i][0],0,sizeof(itemlist[i])); //Reset!
		safestrcpy(itemlist[i],sizeof(itemlist[0]),BOOT_ORDER_STRING[i]); //Set filename from options!
	}
	if (BIOS_Settings.bootorder>=numlist)
	{
		BIOS_Settings.bootorder = DEFAULT_BOOT_ORDER; //Set default boot order!
		BIOS_Changed = 1; //We're changed always!
	}
	int file = ExecuteList(12,4,BOOT_ORDER_STRING[BIOS_Settings.bootorder],256,NULL,0); //Show options for the boot order!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = DEFAULT_BOOT_ORDER; //First is default!
	default: //Changed?
		BIOS_Changed = 1; //Changed!
		BIOS_Settings.bootorder = (byte)file; //Use this option (need to typecast)!
	}
	BIOS_Menu = 35; //Return to CPU menu!
}

void BIOS_InstalledCPUOption() //Manages the installed CPU!
{
	BIOS_Title("Installed CPU");
	EMU_locktext();
	EMU_gotoxy(0,4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0,4,"Installed CPU: "); //Show selection init!
	EMU_unlocktext();
	int i = 0; //Counter!
	numlist = 8; //Amount of CPU types!
	for (i=0; i<8; i++) //Process options!
	{
		memset(&itemlist[i][0],0,sizeof(itemlist[i])); //Reset!
	}
	safestrcpy(itemlist[CPU_8086],sizeof(itemlist[0]),"Intel 8086/8088"); //Set filename from options!
	safestrcpy(itemlist[CPU_NECV30],sizeof(itemlist[0]),"NEC V20/V30"); //Set filename from options!
	safestrcpy(itemlist[CPU_80286],sizeof(itemlist[0]), "Intel 80286"); //Set filename from options!
	safestrcpy(itemlist[CPU_80386],sizeof(itemlist[0]), "Intel 80386"); //Set filename from options!
	safestrcpy(itemlist[CPU_80486],sizeof(itemlist[0]), "Intel 80486"); //Set filename from options!
	safestrcpy(itemlist[CPU_PENTIUM],sizeof(itemlist[0]), "Intel Pentium(without FPU)"); //Set filename from options!
	safestrcpy(itemlist[CPU_PENTIUMPRO], sizeof(itemlist[0]), "Intel Pentium Pro(without FPU)"); //Set filename from options!
	safestrcpy(itemlist[CPU_PENTIUM2], sizeof(itemlist[0]), "Intel Pentium II(without FPU)"); //Set filename from options!
	int current = 0;
	if (*(getarchemulated_CPU())==CPU_8086) //8086?
	{
		current = CPU_8086; //8086!
	}
	else if (*(getarchemulated_CPU()) == CPU_NECV30) //80286?
	{
		current = CPU_NECV30; //80186!
	}
	else if (*(getarchemulated_CPU()) == CPU_80286) //80286?
	{
		current = CPU_80286; //80286!
	}
	else if (*(getarchemulated_CPU()) == CPU_80386) //80386?
	{
		current = CPU_80386; //80386!
	}
	else if (*(getarchemulated_CPU()) == CPU_80486) //80486?
	{
		current = CPU_80486; //80486!
	}
	else if (*(getarchemulated_CPU()) == CPU_PENTIUM) //PENTIUM?
	{
		current = CPU_PENTIUM; //PENTIUM!
	}
	else if (*(getarchemulated_CPU()) == CPU_PENTIUMPRO) //PENTIUM?
	{
		current = CPU_PENTIUMPRO; //PENTIUM!
	}
	else if (*(getarchemulated_CPU()) == CPU_PENTIUM2) //PENTIUM?
	{
		current = CPU_PENTIUM2; //PENTIUM!
	}
	else //Unknown (default)?
	{
		current = DEFAULT_CPU; //NEC V20/V30!
	}
	int file = ExecuteList(15,4,itemlist[current],256,NULL,0); //Show options for the installed CPU!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = DEFAULT_CPU; //Default CPU!
	default: //Changed?
		if (file!=current) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			reboot_needed |= 1; //We need to reboot: a different CPU has been selected!
			switch (file) //Which CPU?
			{
			case CPU_8086: //8086?
				*(getarchemulated_CPU()) = CPU_8086; //Use the 8086!
				break;
			case CPU_NECV30: //NEC V20/V30?
				*(getarchemulated_CPU()) = CPU_NECV30; //Use the NEC V20/V30!
				break;
			case CPU_80286: //80286?
				*(getarchemulated_CPU()) = CPU_80286; //Use the 80286!
				break;
			case CPU_80386: //80386?
				*(getarchemulated_CPU()) = CPU_80386; //Use the 80386!
				break;
			case CPU_80486: //80486?
				*(getarchemulated_CPU()) = CPU_80486; //Use the 80486!
				break;
			case CPU_PENTIUM: //PENTIUM?
				*(getarchemulated_CPU()) = CPU_PENTIUM; //Use the PENTIUM!
				break;
			case CPU_PENTIUMPRO: //PENTIUM?
				*(getarchemulated_CPU()) = CPU_PENTIUMPRO; //Use the PENTIUM PRO!
				break;
			case CPU_PENTIUM2: //PENTIUM?
				*(getarchemulated_CPU()) = CPU_PENTIUM2; //Use the PENTIUM II!
				break;
			default: //Unknown CPU?
				*(getarchemulated_CPU()) = CPU_8086; //Use the 8086!
				break;
			}
		}
		break;
	}
	BIOS_Menu = 35; //Return to CPU menu!
}

extern byte NET_READY; //Is the network support installed?

void BIOS_InitAdvancedText()
{
	advancedoptions = 0; //Init!
	int i;
	for (i = 0; i<14; i++) //Clear all possibilities!
	{
		memset(&menuoptions[i][0],0,sizeof(menuoptions[i])); //Init!
	}

	optioninfo[advancedoptions] = 0; //CPU menu!
	safestrcpy(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "CPU Settings"); //Change installed CPU options!

	optioninfo[advancedoptions] = 1; //Video Settings
	safestrcpy(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Video Settings");

	optioninfo[advancedoptions] = 2; //Sound Settings
	safestrcpy(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Sound Settings");

	optioninfo[advancedoptions] = 3;
	safestrcpy(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Input Settings");

	if (((BIOS_Settings.got_ATCMOS) && (((is_Compaq|is_XT|is_PS2|is_i430fx)==0))) || (BIOS_Settings.got_CompaqCMOS && (is_Compaq && (is_PS2==0)))  || (BIOS_Settings.got_XTCMOS && is_XT) || (BIOS_Settings.got_PS2CMOS && is_PS2 && (!is_i430fx)) || (BIOS_Settings.got_i430fxCMOS && (is_i430fx==1)) || (BIOS_Settings.got_i440fxCMOS && (is_i430fx==2))) //XT/AT/Compaq/PS/2 CMOS saved?
	{
		optioninfo[advancedoptions] = 4; //Clear CMOS!
		safestrcpy(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Clear CMOS data");
	}

	if (!EMU_RUNNING) //Emulator not running (allow memory size change?)
	{
		optioninfo[advancedoptions] = 5; //Memory detect!
		safestrcpy(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Redetect available memory");
	}

	optioninfo[advancedoptions] = 6; //Select BIOS Font!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Settings menu Font: ");
	safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), ActiveBIOSPreset.name); //BIOS font selected!

	optioninfo[advancedoptions] = 7; //Background policy!
setBackgroundpolicytext: //For fixing it!
	safestrcpy(menuoptions[advancedoptions], sizeof(menuoptions[0]), "Background policy: ");
	switch (BIOS_Settings.backgroundpolicy) //VGA Mode?
	{
	case BACKGROUNDPOLICY_FULLHALT:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Full halt");
		break;
	case BACKGROUNDPOLICY_KEEPRUNNINGWITHOUTAUDIOANDVIDEO:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Run without audio playing and recording");
		break;
	case BACKGROUNDPOLICY_KEEPRUNNINGWITHOUTAUDIORECORDINGANDVIDEO:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Run without recording");
		break;
	case BACKGROUNDPOLICY_KEEPRUNNINGWITHOUTVIDEO:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Run without rendering the display");
		break;
	default: //Error: fix it!
		BIOS_Settings.backgroundpolicy = DEFAULT_BACKGROUNDPOLICY; //Reset/Fix!
		BIOS_Changed = 1; //We've changed!
		goto setBackgroundpolicytext; //Goto!
		break;
	}

	optioninfo[advancedoptions] = 8; //Sync timekeeping!
	safestrcpy(menuoptions[advancedoptions++],sizeof(menuoptions[0]),"Synchronize RTC");

	CMOSDATA *currentCMOS;
	if (is_i430fx==2) //i440fx?
	{
		currentCMOS = &BIOS_Settings.i440fxCMOS; //We've used!
	}
	else if (is_i430fx==1) //i430fx?
	{
		currentCMOS = &BIOS_Settings.i430fxCMOS; //We've used!
	}
	else if (is_PS2) //PS/2?
	{
		currentCMOS = &BIOS_Settings.PS2CMOS; //We've used!
	}
	else if (is_Compaq)
	{
		currentCMOS = &BIOS_Settings.CompaqCMOS; //We've used!
	}
	else if (is_XT)
	{
		currentCMOS = &BIOS_Settings.XTCMOS; //We've used!
	}	
	else //AT?
	{
		currentCMOS = &BIOS_Settings.ATCMOS; //We've used!
	}

	optioninfo[advancedoptions] = 9; //RTC mode!
	snprintf(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "RTC mode: %s", currentCMOS->cycletiming?"Cycle-accurate":"Realtime");

	if (NET_READY) //Is network support installed?
	{
		optioninfo[advancedoptions] = 10; //Null modem!
		safestrcpy(menuoptions[advancedoptions], sizeof(menuoptions[0]), "Null modem mode: ");
		switch (BIOS_Settings.nullmodem) //Null modem mode?
		{
		case 0:
			safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Normal modem");
			break;
		case 1:
			safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Simple nullmodem cable");
			break;
		case 2:
			safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Nullmodem cable with line signalling");
			break;
		default: //Error: fix it!
		case 3:
			safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Nullmodem cable with line on phonebook #0");
			break;
		}

		if (modem_passthrough()) //Passthrough mode?
		{
			optioninfo[advancedoptions] = 11; //Connect/disconnect the passthrough!
			if (modem_connected()) //Connected? Disconnect!
			{
				snprintf(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Disconnect the passthrough");
			}
			else
			{
				snprintf(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Connect the passthrough");
			}
		}

		optioninfo[advancedoptions] = 12; //Modem listen port!
		snprintf(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Modem listen port: %u", BIOS_Settings.modemlistenport);
	}
}

void BIOS_AdvancedMenu() //Manages the boot order etc!
{
	byte allowsaveresume;
	BIOS_Title("Advanced Menu");
	BIOS_InitAdvancedText(); //Init text!
	int menuresult = ExecuteMenu(advancedoptions,4,BIOSMENU_SPEC_LR|BIOSMENU_SPEC_RETURN,&Menu_Stat); //Show the menu options!
	switch (menuresult)
	{
	case BIOSMENU_SPEC_CANCEL: //Return?
		allowsaveresume = (!(reboot_needed & 2)); //Allow (save)&resume?
		if (allowsaveresume) //Allow save&resume?
		{
			if (BIOS_Changed) //Save&resume?
			{
				BIOS_Menu = -1; //Quit!
				BIOS_SaveStat = 1; //Save the BIOS!
			}
			else //Discard&resume?
			{
				BIOS_Menu = -1; //Quit!
				BIOS_SaveStat = 0; //Discard changes!
			}
		}
		//When not allowed to resume, ignore the return!
		break;
	case BIOSMENU_SPEC_LTRIGGER: //L: Disk menu?
		BIOS_Menu = 1; //Goto Disk Menu!
		break;
	case BIOSMENU_SPEC_RTRIGGER: //R: Main menu?
		BIOS_Menu = 0; //Goto Main Menu!
		break;

	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
	case 9:
	case 10:
	case 11:
	case 12: //Valid option?
		switch (optioninfo[menuresult]) //What option has been chosen, since we are dynamic size?
		{
		case 0:
			BIOS_Menu = 35; //CPU Menu!
			break;
		case 1:
			BIOS_Menu = 29; //Video Settings setting!
			break;
		case 2:
			BIOS_Menu = 31; //Sound Settings menu!
			break;
		case 3:
			BIOS_Menu = 25; //Input submenu!
			break;
		case 4:
			BIOS_Menu = 37; //Clear CMOS!
			break;
		case 5:
			BIOS_Menu = 14; //Memory reallocation!
			break;
		case 6:
			BIOS_Menu = 16; //BIOS Font setting!
			break;
		case 7: //Background policy?
			BIOS_Menu = 68; //Background policy!
			break;
		case 8: //Synchronize timekeeping?
			BIOS_Menu = 61; //Reset timekeeping!
			break;
		case 9: //RTC mode?
			BIOS_Menu = 67; //Reset timekeeping!
			break;
		case 10: //Modem passthrough mode!
			if (!EMU_RUNNING) //Emulator isn't running?
			{
				BIOS_Menu = 85; //Passthrough mode!
			}
			break;
		case 11: //Connect/disconnect the passthrough?
			BIOS_Menu = 84; //Connect/disconnect the passthrough!
			break;
		case 12: //Modem listen port
			BIOS_Menu = 92; //Modem listen port!
			break;
		default:
			break;
		}
		break;
	default: //Unknown option?
		BIOS_Menu = NOTIMPLEMENTED; //Not implemented yet!
		break;
	}
}

void BIOS_modemListenPort()
{
	BIOS_Title("Modem listen port");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Modem Listen Port: "); //Show selection init!
	EMU_unlocktext();
	int_64 file = InputPortNumber(19, 4, BIOS_Settings.modemlistenport); //Show options for the CPU speed!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected speed!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = DEFAULT_MODEMLISTENPORT; //Default setting!
	default: //Changed?
		if (file != BIOS_Settings.modemlistenport) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			reboot_needed |= 1; //We need to reboot to apply the port changes!
			BIOS_Settings.modemlistenport = (uint_32)file; //Select CPU speed setting!
		}
		break;
	}
	BIOS_Menu = 8; //Goto Advanced menu!
}

void BIOS_analogMinRange()
{
	BIOS_Title("Analog minimum range");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Analog minimum range: "); //Show selection init!
	EMU_unlocktext();
	int_64 file = InputByteNumber(22, 4, BIOS_Settings.input_settings.analog_minrange, 1); //Show options for the analog mimimum range!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected speed!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = 0; //Default setting!
	default: //Changed?
		if (file != BIOS_Settings.input_settings.analog_minrange) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			BIOS_Settings.input_settings.analog_minrange = (byte)file; //Select analog minimum range setting!
		}
		break;
	}
	BIOS_Menu = 25; //Goto Input menu!
}

extern byte UniPCEmu_root_dir_setting; //The current root setting to be viewed!

void BIOS_MainMenu() //Shows the main menu to process!
{
	byte allowsaveresume;
	BIOS_Title("Main menu");

	EMU_gotoxy(0,4); //First row of the BIOS!
	int i;
	for (i=0; i<6; i++)
	{
		memset(&menuoptions[i][0],0,sizeof(menuoptions[i])); //Init!
	}
	advancedoptions = 0; //No advanced options!
	if (BIOS_Changed) //Changed?
	{
		optioninfo[advancedoptions] = 0; //Reboot option!
		if (!reboot_needed) //Running?
		{
			safestrcpy(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Save changes & resume emulation"); //Option #0!
		}
		else
		{
			safestrcpy(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Save changes & restart emulator"); //Option #0!
		}
	}

	optioninfo[advancedoptions] = 1; //Discard option!
	if ((reboot_needed&2)==0) //Able to continue running: Reboot is optional?
	{
		safestrcpy(menuoptions[advancedoptions++],sizeof(menuoptions[0]),"Discard changes & resume emulation"); //Option #1!
	}
	else
	{
		safestrcpy(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Discard changes & restart emulator"); //Option #1!
	}

	if (EMU_RUNNING) //Emulator is running?
	{
		if ((!(BIOS_Changed && reboot_needed)) && BIOS_Changed) //We're not a duplicate?
		{
			optioninfo[advancedoptions] = 3; //Restart emulator option!
			safestrcpy(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Restart emulator (Save changes)"); //Restart emulator option!
		}
		if ((reboot_needed&2)==0) //We're not a duplicate?
		{
			optioninfo[advancedoptions] = 5; //Restart emulator and enter BIOS menu option!
			safestrcpy(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Restart emulator (Discard changes)"); // Restart emulator and enter BIOS menu option!
		}
		if (BIOS_Changed) //We're a viable option?
		{
			optioninfo[advancedoptions] = 4; //Restart emulator and enter BIOS menu option!
			safestrcpy(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Restart emulator and enter settings menu (Save changes)"); // Restart emulator and enter BIOS menu option!
		}
		optioninfo[advancedoptions] = 6; //Restart emulator and enter BIOS menu option!
		safestrcpy(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Restart emulator and enter settings menu (Discard changes)"); // Restart emulator and enter BIOS menu option!
	}
	
	if (!EMU_RUNNING) //Emulator isn't running?
	{
		optioninfo[advancedoptions] = 2; //Load defaults option!
		safestrcpy(menuoptions[advancedoptions++],sizeof(menuoptions[0]),"Load Setting defaults"); //Load defaults option!
	}

	#if defined(IS_PSP) || defined(IS_VITA) || defined(IS_SWITCH)
	optioninfo[advancedoptions] = 7; //Version information option!
	safestrcpy(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Version information"); //Version information option!
	#endif

	int menuresult = ExecuteMenu(advancedoptions,4,BIOSMENU_SPEC_LR|BIOSMENU_SPEC_RETURN,&Menu_Stat); //Plain menu, allow L&R triggers!

	switch (menuresult) //What option has been chosen?
	{
	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
		switch (optioninfo[menuresult]) //What option is chosen?
		{
		case 0: //Save&Quit?
			BIOS_Menu = -1; //Quit!
			BIOS_SaveStat = 1; //Save the BIOS!
			break;
		case 1: //Discard changes&Quit?
			BIOS_Menu = -1; //Quit!
			BIOS_SaveStat = 0; //Discard changes!
			break;
		case 2: //Load defaults?
			BIOSMenu_LoadDefaults(); //Load BIOS defaults option!
			BIOS_Changed = 1; //The BIOS has been changed!
			reboot_needed |= 2; //We need a reboot!
			break;
		case 3: //Restart emulator?
			bootBIOS = 0; //Not a forced first run!
			BIOS_Menu = -1; //Quit!
			BIOS_SaveStat = 1; //Save the BIOS!
			reboot_needed |= 2; //We need a reboot!
			break;
		case 4: //Restart emulator and enter BIOS menu?
			bootBIOS = 1; //Forced first run!
			BIOS_Menu = -1; //Quit!
			BIOS_SaveStat = 1; //Save the BIOS!
			reboot_needed |= 2; //We need a reboot!
			break;
		case 5: //Restart emulator and discard changes?
			bootBIOS = 0; //Not a forced first run!
			BIOS_Menu = -1; //Quit!
			BIOS_SaveStat = 0; //Discard changes!
			reboot_needed |= 2; //We need a reboot!
			break;
		case 6: //Restart emulator, discard changes and enter BIOS menu?
			bootBIOS = 1; //Forced first run!
			BIOS_Menu = -1; //Quit!
			BIOS_SaveStat = 0; //Discard changes!
			reboot_needed |= 2; //We need a reboot!
			break;
		case 7: //Show version
			BIOS_Menu = 95; //Goto version information!
			break;
		default:
			break;
		}
		break;
	case BIOSMENU_SPEC_CANCEL: //Return?
		allowsaveresume = (!(reboot_needed & 2)); //Allow (save)&resume?
		if (allowsaveresume) //Allow save&resume?
		{
			if (BIOS_Changed) //Save&resume?
			{
				BIOS_Menu = -1; //Quit!
				BIOS_SaveStat = 1; //Save the BIOS!
			}
			else //Discard&resume?
			{
				BIOS_Menu = -1; //Quit!
				BIOS_SaveStat = 0; //Discard changes!
			}
		}
		//When not allowed to resume, ignore the return!
		break;
	case BIOSMENU_SPEC_LTRIGGER: //L?
		BIOS_Menu = 8; //Goto Advanced menu!
		break;
	case BIOSMENU_SPEC_RTRIGGER: //R?
		BIOS_Menu = 1; //Goto Disk Menu!
		break;
	default: //Not implemented yet?
		BIOS_Menu = NOTIMPLEMENTED; //Go out-of-range for invalid/unrecognised menu!
		break;
	}
}

FILEPOS ImageGenerator_GetImageSize(byte x, byte y) //Retrieve the size, or 0 for none!
{
	int key = 0;
	lock(LOCK_INPUT);
	key = psp_inputkeydelay(BIOS_INPUTDELAY);
	unlock(LOCK_INPUT);
	while ((key&(BUTTON_CONFIRM|BUTTON_START))>0) //Pressed? Wait for release!
	{
		lock(LOCK_INPUT);
		key = psp_inputkeydelay(BIOS_INPUTDELAY);
		unlock(LOCK_INPUT);
	}
	FILEPOS result = 0; //Size: result; default 0 for none! Must be a multiple of 4096 bytes for HDD!
	FILEPOS oldvalue; //To check for high overflow!
	for (;;) //Get input; break on error!
	{
		EMU_locktext();
		EMU_textcolor(BIOS_ATTR_ACTIVE); //We're using active color for input!
		GPU_EMU_printscreen(x, y, "%08i MB %04i KB", (uint_32)(result/MBMEMORY), (uint_32)((result%MBMEMORY)/1024)); //Show current size!
		EMU_unlocktext();
		lock(LOCK_INPUT);
		key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
		unlock(LOCK_INPUT);
		FILEPOS step;
		step = 4096; //Default to 4KB steps!
		//1GB/1MB steps!
		if ((key & BUTTON_LTRIGGER)>0) //GB steps?
		{
			step = 1024*MBMEMORY; //GB steps!
		}
		else if ((key & BUTTON_RTRIGGER)>0) //MB steps?
		{
			step = MBMEMORY; //Add 1GB!
		}

		//Multiplier of steps!
		if ((key & BUTTON_LEFT)>0)
		{
			step *= 10; //x10
		}
		if ((key & BUTTON_RIGHT)>0)
		{
			step *= 100; //x100
		}

		//Apply steps to take!
		if ((key & BUTTON_DOWN)>0)
		{
			if (result==0) { }
			else
			{
				if (((int_64)(result - step)) <= 0)
				{
					result = 0;    //4KB steps!
				}
				else
				{
					result -= step;
				}
			}
		}
		else if ((key & BUTTON_UP)>0)
		{
			oldvalue = result; //Save the old value!
			result += step; //Add step!
			if (result < oldvalue) //We've overflown?
			{
				result = oldvalue; //Undo: we've overflown!
			}
		}
		else if ((key & (BUTTON_CONFIRM|BUTTON_START))>0)
		{
			while ((key&(BUTTON_CONFIRM|BUTTON_START))>0) //Wait for release!
			{
				lock(LOCK_INPUT);
				key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
				unlock(LOCK_INPUT);
			}
			return result;
		}
		else if ((key & BUTTON_CANCEL)>0)
		{
			while ((key&BUTTON_CANCEL)>0) //Wait for release!
			{
				lock(LOCK_INPUT);
				key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
				unlock(LOCK_INPUT);
			}
			break; //Cancel!
		}
		else if (shuttingdown()) break; //Cancel because of shutdown?
		if (result>0x20000000000ULL) //Past the limit of a HDD(2TB)?
		{
			result = 0x20000000000ULL; //Limit to the maximum hard disk size to generate!
		}
	}
	return 0; //No size: cancel!
}

void BIOS_WaitForInput() //Retrieve the size, or 0 for none!
{
	int key = 0;
	lock(LOCK_INPUT);
	key = psp_inputkeydelay(BIOS_INPUTDELAY);
	unlock(LOCK_INPUT);
	while ((key & (BUTTON_CONFIRM | BUTTON_START)) > 0) //Pressed? Wait for release!
	{
		lock(LOCK_INPUT);
		key = psp_inputkeydelay(BIOS_INPUTDELAY);
		unlock(LOCK_INPUT);
	}
	for (;;) //Get input; break on error!
	{
		lock(LOCK_INPUT);
		key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
		unlock(LOCK_INPUT);
		if (key > 0)
		{
			while (key > 0) //Wait for release!
			{
				lock(LOCK_INPUT);
				key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
				unlock(LOCK_INPUT);
			}
			return;
		}
		else if (shuttingdown()) break; //Cancel because of shutdown?
	}
}

extern byte input_buffer_shift; //Ctrl-Shift-Alt Status for the pressed key!
extern sword input_buffer; //To contain the pressed key!
extern byte input_buffer_mouse; //Mouse button input also supported!

byte BIOS_InputText(byte x, byte y, char *filename, uint_32 maxlength, byte isaddress)
{
	char ending[3] = "\xC3\x9B";
	delay(100000); //Wait a bit to make sure nothing's pressed!
	enableKeyboard(2); //Buffer input!
	char input[256];
	memset(&input, 0, sizeof(input)); //Init input to empty!
	TicksHolder ticks;
	initTicksHolder(&ticks); //Initialise!
	getnspassed(&ticks); //Initialise counter!

	lock(LOCK_INPUT);
	goto updatescreeninput; //Start screen with input&cursor!

	for (;;) //Main input loop!
	{
		if (shuttingdown()) //Are we shutting down?
		{
			disableKeyboard(); //Disable the keyboard!
			return 0; //Cancel!
		}
		delay(0); //Wait a bit for input, depending on input done!
		updateKeyboard(getnspassed(&ticks)); //Update the OSK keyboard with a little time!
		lock(LOCK_INPUT);
		if (input_buffer!=-1) //Given input yet?
		{
			if (EMU_keyboard_handler_idtoname(input_buffer,&input[0])) //Valid key(Don't count shift statuses only)?
			{
				if (!strcmp(input, "enter") || !strcmp(input,"esc")) //Enter or Escape? We're finished!
				{
					unlock(LOCK_INPUT);
					disableKeyboard(); //Disable the keyboard!
					EMU_locktext();
					EMU_gotoxy(x, y); //Goto position for info!
					EMU_textcolor(BIOS_ATTR_TEXT);
					GPU_EMU_printscreen(x, y, "%s", filename); //Show the filename!
					EMU_textcolor(BIOS_ATTR_ACTIVE); //Active color!
					GPU_EMU_printscreen(-1, -1, " "); //Clear cursor indicator!
					EMU_unlocktext();
					return (!strcmp(input, "enter")); //Enter=Confirm, Esc=Cancel!
				}
				//We're a normal key hit?
				else if (!strcmp(input, "bksp") || (!strcmp(input,"z") && (input_buffer_shift&SHIFTSTATUS_CTRL))) //Backspace OR CTRL-Z?
				{
					if (safestrlen(filename,maxlength+1)) //Gotten length?
					{
						filename[safestrlen(filename,maxlength+1) - 1] = '\0'; //Make us one shorter!
					}
				}
				else if (!strcmp(input, "space")) //Space?
				{
					if (safestrlen(filename,maxlength+1) < maxlength) //Not max?
					{
						safestrcat(filename,(maxlength+1), " "); //Add a space!
					}
				}
				else if (safestrlen(input,sizeof(input)) == 1) //Single character?
				{
					if ((input[0] != '`') &&
						(input[0] != '=') &&
						(input[0] != '\\') &&
						(input[0] != '[') &&
						(input[0] != ']') &&
						((input[0] != ';') || (isaddress && ((input[0] == ';') && (input_buffer_shift & SHIFTSTATUS_SHIFT)))) && //Address allows ':'!
						(strcmp(input,"'")!=0) &&
						(input[0] != ',') &&
						((input[0] != '/') || (isaddress && (input[0] == '/'))) //Address allows '/'!
						) //Not an invalid character?
					{
						if (safestrlen(filename,maxlength+1) < maxlength) //Not max?
						{
							if (input_buffer_shift&SHIFTSTATUS_SHIFT) //Shift pressed?
							{
								if ((input[0] >= 'a') && (input[0] <= 'z')) //Able to use shift on this key?
								{
									input[0] += (char)((int)'A' - (int)'a'); //Convert to uppercase!
									safestrcat(filename,(maxlength+1), input); //Add the input to the filename!
								}
								else if (input[0] == '-') //Becomes _?
								{
									input[0] = '_'; //Convert to uppercase!
									safestrcat(filename,(maxlength+1), input); //Add the input to the filename!
								}
								else if (input[0] == ';') //Becomes :?
								{
									input[0] = ':'; //Convert to uppercase!
									safestrcat(filename, (maxlength + 1), input); //Add the input to the filename!
								}
								//Invalid uppercase is ignored!
							}
							else //Non-shift valid character?
							{
								safestrcat(filename,(maxlength+1), input); //Add the input to the filename!
							}
						}
					}
				}

				updatescreeninput:
				EMU_locktext();
				EMU_gotoxy(x, y); //Goto position for info!
				EMU_textcolor(BIOS_ATTR_TEXT);
				GPU_EMU_printscreen(x, y, "%s", filename); //Show the filename!
				EMU_textcolor(BIOS_ATTR_ACTIVE); //Active color!
				GPU_EMU_printscreen(-1, -1, ending); //Cursor indicator!
				EMU_textcolor(BIOS_ATTR_TEXT); //Back to text!
				GPU_EMU_printscreen(-1, -1, " "); //Clear output after!
				EMU_unlocktext();
				input_buffer_shift = 0; //Reset!
				input_buffer_mouse = 0; //Reset!
				input_buffer = -1; //Nothing input!
			}
		}
		else if (input_buffer_shift || input_buffer_mouse) //Shift/mouse are ignored!
		{
			input_buffer_shift = input_buffer_mouse = 0; //Ignore!
		}
		unlock(LOCK_INPUT);
	}
}

byte BIOS_InputAddressWithMode(byte x, byte y, char *filename, uint_32 maxlength, byte allowModeAndAddressIgnore, byte allowsegment, byte allowSingleStep)
{
	char ending[3] = "\xC3\x9B";
	delay(100000); //Wait a bit to make sure nothing's pressed!
	enableKeyboard(2); //Buffer input!
	char input[256];
	memset(&input, 0, sizeof(input)); //Init input to empty!
	TicksHolder ticks;
	initTicksHolder(&ticks); //Initialise!
	getnspassed(&ticks); //Initialise counter!

	lock(LOCK_INPUT);
	goto updatescreeninput; //Start screen with input&cursor!

	for (;;) //Main input loop!
	{
		if (shuttingdown()) //Are we shutting down?
		{
			disableKeyboard(); //Disable the keyboard!
			return 0; //Cancel!
		}
		delay(0); //Wait a bit for input, depending on input done!
		updateKeyboard(getnspassed(&ticks)); //Update the OSK keyboard with a little time!
		lock(LOCK_INPUT);
		if (input_buffer!=-1) //Given input yet?
		{
			if (EMU_keyboard_handler_idtoname(input_buffer,&input[0])) //Valid key(Don't count shift statuses only)?
			{
				if (!strcmp(input, "enter") || !strcmp(input,"esc")) //Enter or Escape? We're finished!
				{
					unlock(LOCK_INPUT);
					disableKeyboard(); //Disable the keyboard!
					EMU_locktext();
					EMU_gotoxy(x, y); //Goto position for info!
					EMU_textcolor(BIOS_ATTR_TEXT);
					GPU_EMU_printscreen(x, y, "%s", filename); //Show the filename!
					EMU_textcolor(BIOS_ATTR_ACTIVE); //Active color!
					GPU_EMU_printscreen(-1, -1, " "); //Clear cursor indicator!
					EMU_unlocktext();
					return (!strcmp(input, "enter")); //Enter=Confirm, Esc=Cancel!
				}
				//We're a normal key hit?
				else if (!strcmp(input, "bksp") || (!strcmp(input,"z") && (input_buffer_shift&SHIFTSTATUS_CTRL))) //Backspace OR CTRL-Z?
				{
					if (safestrlen(filename,maxlength+1)) //Gotten length?
					{
						filename[safestrlen(filename,maxlength+1) - 1] = '\0'; //Make us one shorter!
					}
				}
				else if (safestrlen(input,sizeof(input)) == 1) //Single character?
				{
					switch (input[0]) //Not an invalid character?
					{
					case 's': //Single step?
						input[0] = 'S'; //Convert to upper case!
						if (safestrlen(filename, maxlength + 1) && (allowSingleStep)) //Something there?
						{
							if (((filename[safestrlen(filename, maxlength + 1) - 1])==':') || ((filename[safestrlen(filename, maxlength + 1) - 1]) == 'S')) //What character are we following?
							{
								break; //Abort!
							}
						}
						else break; //Abort: empty string not allowed!
						goto processinput; //process us!						
					case 'i': //Ignore EIP?
						input[0] = 'I'; //Convert to upper case!
						if (safestrlen(filename,maxlength+1) && (allowsegment)) //Something there?
						{
							if ((filename[safestrlen(filename,maxlength+1)-1]=='I') || (filename[safestrlen(filename, maxlength + 1) - 1] == 'O') || (filename[safestrlen(filename,maxlength+1)-1]==':') || (filename[safestrlen(filename,maxlength+1)-1]=='M') || (filename[safestrlen(filename, maxlength + 1) - 1] == 'S')) //Special identifier that we're not allowed behind?
							{
								break; //Abort!
							}
						}
						else break; //Abort: empty string not allowed!
						goto processinput; //process us!						
					case 'o': //Ignore CS(offset is to be applied only)?
						input[0] = 'O'; //Convert to upper case!
						if (safestrlen(filename, maxlength + 1) && (allowsegment)) //Something there?
						{
							if ((filename[safestrlen(filename, maxlength + 1) - 1] == 'I') || (filename[safestrlen(filename, maxlength + 1) - 1] == 'O') || (filename[safestrlen(filename, maxlength + 1) - 1] == ':') || (filename[safestrlen(filename, maxlength + 1) - 1] == 'M') || (filename[safestrlen(filename, maxlength + 1) - 1] == 'S')) //Special identifier that we're not allowed behind?
							{
								break; //Abort!
							}
						}
						else break; //Abort: empty string not allowed!
						goto processinput; //process us!						
					case 'm': //Ignore Address?
						input[0] = 'M'; //Convert to upper case!
						if (safestrlen(filename,maxlength+1) && (allowModeAndAddressIgnore)) //Something there?
						{
							if ((filename[safestrlen(filename,maxlength+1)-1]=='I') || (filename[safestrlen(filename, maxlength + 1) - 1] == 'O') || (filename[safestrlen(filename,maxlength+1)-1]==':') || (filename[safestrlen(filename,maxlength+1)-1]=='M') || (filename[safestrlen(filename, maxlength + 1) - 1] == 'S')) //Special identifier that we're not allowed behind?
							{
								break; //Abort!
							}
						}
						else break; //Abort: empty string not allowed!
						goto processinput; //process us!						
					case 'p': //Protected mode?
						input[0] = 'P'; //Convert to upper case!
						if (safestrlen(filename,maxlength+1) && (allowModeAndAddressIgnore)) //Something there?
						{
							if ((filename[safestrlen(filename,maxlength+1)-1]=='P') || (filename[safestrlen(filename,maxlength+1)-1]=='V') || (filename[safestrlen(filename,maxlength+1)-1]==':') || (filename[safestrlen(filename,maxlength+1)-1]=='I') || (filename[safestrlen(filename, maxlength + 1) - 1] == 'O') ||  (filename[safestrlen(filename,maxlength+1)-1]=='M') || (filename[safestrlen(filename, maxlength + 1) - 1] == 'S')) //Special identifier?
							{
								break; //Abort!
							}
						}
						else break; //Abort: empty string not allowed!
						goto processinput; //process us!
					case 'v': //Virtual 8086 mode?
						input[0] = 'V'; //Convert to upper case!
						if (safestrlen(filename,maxlength+1) && (allowModeAndAddressIgnore)) //Something there?
						{
							if ((filename[safestrlen(filename,maxlength+1)-1]=='P') || (filename[safestrlen(filename,maxlength+1)-1]=='V') || (filename[safestrlen(filename,maxlength+1)-1]==':') || (filename[safestrlen(filename,maxlength+1)-1]=='I') || (filename[safestrlen(filename, maxlength + 1) - 1] == 'O') || (filename[safestrlen(filename,maxlength+1)-1]=='M') || (filename[safestrlen(filename, maxlength + 1) - 1] == 'S')) //Special identifier?
							{
								break; //Abort!
							}
						}
						else break; //Abort: empty string not allowed!
						goto processinput; //process us!
					case ';':
						if ((input_buffer_shift&SHIFTSTATUS_SHIFT)==0) break; //Shift not pressed?
						input[0] = ':'; //We're a seperator instead!
						if ((safestrlen(filename,maxlength+1)) && (allowsegment)) //Something there and segments are allowed?
						{
							if ((filename[safestrlen(filename,maxlength+1)-1]=='P') || (filename[safestrlen(filename,maxlength+1)-1]=='V') || (filename[safestrlen(filename,maxlength+1)-1]==':') || (filename[safestrlen(filename,maxlength+1)-1]=='I') || (filename[safestrlen(filename, maxlength + 1) - 1] == 'O') || (filename[safestrlen(filename,maxlength+1)-1]=='M') || (filename[safestrlen(filename, maxlength + 1) - 1] == 'S')) //Special identifier?
							{
								break; //Abort!
							}
							char *c;
							c = &filename[0]; //Start of the filename!
							for (;*c;++c) //Check until EOS!
							{
								if (*c==':') //Already present?
								{
									goto abortsemicolon; //Abort: we're not allowed twice!
								}
							}
						}
						else
						{
							abortsemicolon:
							break; //Abort: empty string not allowed!
						}
					//Hexadecimal numbers for the numbers themselves:
					case '0':
					case '1':
					case '2':
					case '3':
					case '4':
					case '5':
					case '6':
					case '7':
					case '8':
					case '9':
					case 'a':
					case 'b':
					case 'c':
					case 'd':
					case 'e':
					case 'f':
						if (safestrlen(filename,maxlength+1)) //Something there?
						{
							if ((filename[safestrlen(filename,maxlength+1)-1]=='P') || (filename[safestrlen(filename,maxlength+1)-1]=='V') || (filename[safestrlen(filename,maxlength+1)-1]=='I') || (filename[safestrlen(filename,maxlength+1)-1]=='M') || (filename[safestrlen(filename, maxlength + 1) - 1] == 'S')) //Special identifier?
							{
								break; //Abort!
							}
						}
						processinput: //Process the input!
						if (safestrlen(filename,maxlength+1) < maxlength) //Not max?
						{
							if ((input[0] >= 'a') && (input[0] <= 'z')) //Able to use shift on this key?
							{
								input[0] += (char)((int)'A' - (int)'a'); //Convert to uppercase!
								safestrcat(filename,(maxlength+1), input); //Add the input to the filename!
							}
							else //Non-shift valid character?
							{
								safestrcat(filename,(maxlength+1), input); //Add the input to the filename!
							}
						}
						break;
					default: //Unknown key?
						break; //Abort: not supported!
					}
				}

				updatescreeninput:
				EMU_locktext();
				EMU_gotoxy(x, y); //Goto position for info!
				EMU_textcolor(BIOS_ATTR_TEXT);
				GPU_EMU_printscreen(x, y, "%s", filename); //Show the filename!
				EMU_textcolor(BIOS_ATTR_ACTIVE); //Active color!
				GPU_EMU_printscreen(-1, -1, ending); //Cursor indicator!
				EMU_textcolor(BIOS_ATTR_TEXT); //Back to text!
				GPU_EMU_printscreen(-1, -1, " "); //Clear output after!
				EMU_unlocktext();
				input_buffer_shift = 0; //Reset!
				input_buffer_mouse = 0; //Reset!
				input_buffer = -1; //Nothing input!
			}
		}
		else if (input_buffer_shift || input_buffer_mouse) //Shift/mouse are ignored!
		{
			input_buffer_shift = input_buffer_mouse = 0; //Ignore!
		}
		unlock(LOCK_INPUT);
	}
}

void BIOS_GenerateStaticHDD() //Generate Static HDD Image!
{
	char filename[256]; //Filename container!
	char fullfilename[256]; //Full filename container!
	FILEPOS size = 0;
	BIOSClearScreen(); //Clear the screen!
	memset(&filename[0],0,sizeof(filename)); //Init!
	char title[256];
	memset(&title,0,sizeof(title));
	snprintf(title,sizeof(title),"Generate Static(%s) HDD Image",((generateHDD_type==1)?"Bochs":((generateHDD_type==2)?"classic":"optimal")));
	BIOS_Title(title); //Full clear!
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto position for info!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Name: "); //Show the filename!
	EMU_unlocktext();
	if (BIOS_InputText(6, 4, &filename[0], 255-4, 0)) //Input text confirmed?
	{
		if (strcmp(filename, "") != 0) //Got input?
		{
			if (safestrlen(filename,sizeof(filename)) <= (255 - 4)) //Not too long?
			{
				safestrcat(filename,sizeof(filename), ".img"); //Add the extension!
				EMU_locktext();
				EMU_gotoxy(0, 4); //Goto position for info!
				EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
				GPU_EMU_printscreen(0, 4, "Filename: %s", filename); //Show the filename!
				EMU_gotoxy(0, 5); //Next row!
				GPU_EMU_printscreen(0, 5, "Image size: "); //Show image size selector!!
				EMU_unlocktext();
				size = ImageGenerator_GetImageSize(12, 5); //Get the size!
				if (size != 0) //Got size?
				{
					EMU_locktext();
					EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
					GPU_EMU_printscreen(12, 5, "%08i MB %04i KB", (uint_32)(size / MBMEMORY), (uint_32)((size % MBMEMORY) / 1024)); //Show size too!
					EMU_gotoxy(0, 6); //Next row!
					GPU_EMU_printscreen(0, 6, "Generating image: "); //Start of percentage!
					EMU_unlocktext();
					domkdir(diskpath);
					memset(&fullfilename,0,sizeof(fullfilename));
					safestrcpy(fullfilename,sizeof(fullfilename),diskpath);
					safestrcat(fullfilename,sizeof(fullfilename),"/");
					safestrcat(fullfilename,sizeof(fullfilename),filename);
					generateStaticImage(filename, size, 18, 6, generateHDD_type); //Generate a static image, Bochs/Dosbox-compatible format!
					if (!strcmp(filename, BIOS_Settings.hdd0) || !strcmp(filename, BIOS_Settings.hdd1)) //Harddisk changed?
					{
						BIOS_Changed = 1; //We've changed!
						reboot_needed |= 2; //We're in need of a reboot!
					}
					//This can be a floppy disk image being overwritten as well!
					if (!strcmp(disks[FLOPPY0].rawfilename, filename)) //Floppy #0 changed?
					{
						iofloppy0("", 0, BIOS_Settings.floppy0_readonly, 0); //Unmount!
						iofloppy0(BIOS_Settings.floppy0, 0, BIOS_Settings.floppy0_readonly, 0); //Remount to update!
					}
					if (!strcmp(disks[FLOPPY1].rawfilename, filename)) //Floppy #1 changed?
					{
						iofloppy1("", 0, BIOS_Settings.floppy1_readonly, 0); //Unmount!
						iofloppy1(BIOS_Settings.floppy1, 0, BIOS_Settings.floppy1_readonly, 0); //Remount to update!
					}

				}
			}
			//If we're too long, ignore it!
		}
	}
	BIOS_Menu = 1; //Return to Disk Menu!
}

void BIOS_GenerateDynamicHDD() //Generate Static HDD Image!
{
	char fullfilename[256]; //Full filename container!
	char title[256];
	memset(&title,0,sizeof(title));
	snprintf(title,sizeof(title),"Generate Dynamic(%s) HDD Image",((generateHDD_type==3)?"Bochs":((generateHDD_type==1)?"classic":"optimal")));
	BIOS_Title(title);
	char filename[256]; //Filename container!
	memset(&filename[0],0,sizeof(filename)); //Init!
	FILEPOS size = 0;
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto position for info!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Name: "); //Show the filename!
	EMU_unlocktext();
	if (BIOS_InputText(6, 4, &filename[0], 255-7, 0)) //Input text confirmed?
	{
		if (strcmp(filename, "") != 0) //Got input?
		{
			if (safestrlen(filename,sizeof(filename)) <= (255 - 7)) //Not too long?
			{
				safestrcat(filename,sizeof(filename), ".sfdimg"); //Add the extension!
				EMU_locktext();
				EMU_textcolor(BIOS_ATTR_TEXT);
				EMU_gotoxy(0, 4); //Goto position for info!
				GPU_EMU_printscreen(0, 4, "Filename: %s", filename); //Show the filename!
				EMU_gotoxy(0, 5); //Next row!
				GPU_EMU_printscreen(0, 5, "Image size: "); //Show image size selector!!
				EMU_unlocktext();
				size = ImageGenerator_GetImageSize(12, 5); //Get the size!
				if (size != 0) //Got size?
				{
					EMU_locktext();
					EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
					GPU_EMU_printscreen(12, 5, "%08i MB %04i KB", (uint_32)(size / MBMEMORY), (uint_32)((size % MBMEMORY) / 1024)); //Show size too!
					EMU_gotoxy(0, 6); //Next row!
					GPU_EMU_printscreen(0, 6, "Generating image: "); //Start of percentage!
					EMU_unlocktext();
					domkdir(diskpath);
					memset(&fullfilename, 0, sizeof(fullfilename));
					safestrcpy(fullfilename,sizeof(fullfilename), diskpath);
					safestrcat(fullfilename,sizeof(fullfilename), "/");
					safestrcat(fullfilename,sizeof(fullfilename), filename);
					generateDynamicImage(filename, size, 18, 6, generateHDD_type); //Generate a dynamic image!
					if (!strcmp(filename, BIOS_Settings.hdd0) || !strcmp(filename, BIOS_Settings.hdd1)) //Harddisk changed?
					{
						BIOS_Changed = 1; //We've changed!
						reboot_needed |= 2; //We're in need of a reboot!
					}
				}
			}
			//If we're too long, ignore it!
		}
	}
	BIOS_Menu = 1; //Return to Disk Menu!
}

//How many sectors to be able to transfer at once?
#define VERIFICATIONBLOCK 500

byte sector[VERIFICATIONBLOCK * 512], verificationsector[VERIFICATIONBLOCK * 512]; //Current sector!

FILEPOS sectorupdateinterval; //How many sectors interval before updating?
FILEPOS sectorupdateintervalcnt; //Counter for testing for sector update display!

void BIOS_ConvertStaticDynamicHDD() //Generate Dynamic HDD Image from a static one!
{
	uint_64 datatotransfer; //How many sectors to transfer this block?
	uint_32 sectorposition = 0; //Possible position of error!
	char filename[256]; //Filename container!
	char fullfilename[256]; //Full filename container!
	cleardata(&filename[0], sizeof(filename)); //Init!
	FILEPOS size = 0;
	BIOS_Title("Convert static to dynamic HDD Image"); //Full clear!
	generateFileList(diskpath,"img", 0, 0,1); //Generate file list for all .img files!
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Disk image: "); //Show selection init!
	EMU_unlocktext();
	selectingHDD = 1; //Selecting a HDD!
	int file = ExecuteList(12, 4, "", 256,&hdd_information,0); //Show menu for the disk image!
	switch (file) //Which file?
	{
	case FILELIST_NOFILES: //No files?
		BIOS_disk_nofiles(); //No files!
	case FILELIST_DEFAULT: //Unmount?
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	default: //File?
		safestrcpy(filename,sizeof(filename), itemlist[file]); //Use this file!
		if (strcmp(filename, "") != 0) //Got input?
		{
			BIOS_Title("Convert static to dynamic HDD Image"); //Full clear!
			EMU_locktext();
			EMU_textcolor(BIOS_ATTR_TEXT);
			EMU_gotoxy(0, 4); //Goto position for info!
			GPU_EMU_printscreen(0, 4, "Filename: %s  ", filename); //Show the filename!
			EMU_gotoxy(0, 5); //Next row!
			GPU_EMU_printscreen(0, 5, "Image size: "); //Show image size selector!!
			GPU_EMU_printscreen(0, 6, "Destination filename: ");
			EMU_unlocktext();
			char srcdisk[256];
			memset(&srcdisk, 0, sizeof(srcdisk));
			safestrcpy(srcdisk, sizeof(srcdisk), filename); //Save!
			iohdd0(filename, 0, 1, 0); //Mount the source disk!
			size = getdisksize(HDD0); //Get the original size!
			EMU_locktext();
			GPU_EMU_printscreen(12, 5, "      "); //Clear the creation process!
			GPU_EMU_printscreen(12, 5, "%uMB", (size / MBMEMORY)); //Image size
			EMU_unlocktext();
			if (BIOS_InputText(22, 6, &filename[0], (sizeof(filename) - 1) - strlen(".sfdimg"), 0))
			{
				if (safestrlen(filename, sizeof(filename)) > (sizeof(filename) - strlen(".sfdimg") - 1)) //Too long filename?
				{
					size = 0; //Abort!
				}
				safestrcat(filename, sizeof(filename), ".sfdimg"); //Generate destination filename!
				if (size != 0) //Got size?
				{
					EMU_locktext();
					EMU_gotoxy(0, 7); //Next row!
					EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
					GPU_EMU_printscreen(0, 7, "Generating image: "); //Start of percentage!
					EMU_unlocktext();
					FILEPOS sizecreated;
					domkdir(diskpath);
					memset(&fullfilename, 0, sizeof(fullfilename));
					safestrcpy(fullfilename, sizeof(fullfilename), diskpath);
					safestrcat(fullfilename, sizeof(fullfilename), "/");
					safestrcat(fullfilename, sizeof(fullfilename), filename);
					sizecreated = generateDynamicImage(filename, size, 18, 7, statictodynamic_imagetype(srcdisk)); //Generate a dynamic image!
					if (sizecreated >= size) //Correct size?
					{
						if (!strcmp(filename, BIOS_Settings.hdd0) || !strcmp(filename, BIOS_Settings.hdd1)) //Harddisk changed?
						{
							BIOS_Changed = 1; //We've changed!
							reboot_needed |= 2; //We're in need of a reboot!
						}
						EMU_locktext();
						GPU_EMU_printscreen(18, 7, "      "); //Clear the creation process!
						EMU_unlocktext();
						iohdd1(filename, 0, 0, 0); //Mount the destination disk, allow writing!
						FILEPOS sectornr;
						EMU_locktext();
						EMU_gotoxy(0, 6); //Next row!
						GPU_EMU_printscreen(0, 7, "Generating image: "); //Start of percentage!
						EMU_unlocktext();
						byte error = 0;
						sectorupdateinterval = MAX((sizecreated / 100), sizeof(sector)); //Update interval!
						sectorupdateintervalcnt = 0; //Reset counter for the first update!
						byte firstupdate = 1; //First update!
						for (sectornr = 0; sectornr < sizecreated;) //Process all sectors!
						{
							if (shuttingdown())
							{
								error = 4; //Give the fourth error!
								break;
							}
							if ((sizecreated - sectornr) > sizeof(sector)) //Too much to handle?
							{
								datatotransfer = sizeof(sector); //Limit to max!
							}
							else
							{
								datatotransfer = sizecreated;
								datatotransfer -= sectornr; //How many bytes of data to transfer?
							}
							if (readdata(HDD0, &sector, sectornr, (uint_32)datatotransfer)) //Read a sector?
							{
								if (!writedata(HDD1, &sector, sectornr, (uint_32)datatotransfer)) //Error writing a sector?
								{
									error = 2;
									break; //Stop reading!
								}
							}
							else //Error reading sector?
							{
								error = 1;
								break; //Stop reading!
							}
							sectorupdateintervalcnt += datatotransfer; //Transferred data!
							if ((sectorupdateintervalcnt >= sectorupdateinterval) || firstupdate) //Update every 10000 sectors!
							{
								sectorupdateintervalcnt %= sectorupdateinterval; //Reset counter as much as is needed!
								firstupdate = 0; //Not first update anymore!
								EMU_locktext();
								GPU_EMU_printscreen(18, 7, "%u%%", (int)(((float)sectornr / (float)size) * 100.0f)); //Current progress!
								EMU_unlocktext();
							}
							sectornr += datatotransfer; //Next sector block!
						}
						EMU_locktext();
						GPU_EMU_printscreen(18, 7, "%u%%", (int)(((float)sectornr / (float)size) * 100.0f)); //Current progress!
						EMU_unlocktext();

						//Verification!
						if (!error) //OK?
						{
							EMU_locktext();
							GPU_EMU_printscreen(0, 8, "Validating image: "); //Start of percentage!
							EMU_unlocktext();
							iohdd1(filename, 0, 1, 0); //Mount!
							sectorupdateinterval = MAX((sizecreated / 100), sizeof(sector)); //Update interval!
							sectorupdateintervalcnt = 0; //Reset counter for the first update!
							firstupdate = 1; //First update!
							for (sectornr = 0; sectornr < size;) //Process all sectors!
							{
								if (shuttingdown())
								{
									error = 4; //Give the fourth error!
									break;
								}
								if ((sizecreated - sectornr) > sizeof(sector)) //Too much to handle?
								{
									datatotransfer = sizeof(sector); //Limit to max!
								}
								else
								{
									datatotransfer = sizecreated;
									datatotransfer -= sectornr; //How many bytes of data to transfer?
								}
								if (readdata(HDD0, &sector, sectornr, (uint_32)datatotransfer)) //Read a sector?
								{
									if (!readdata(HDD1, &verificationsector, sectornr, (uint_32)datatotransfer)) //Error reading a sector?
									{
										error = 2;
										break; //Stop reading!
									}
									else if ((sectorposition = memcmp(&sector, &verificationsector, (size_t)datatotransfer)) != 0)
									{
										error = 3; //Verification error!
										break; //Stop reading!
									}
								}
								else //Error reading sector?
								{
									error = 1;
									break; //Stop reading!
								}
								sectorupdateintervalcnt += datatotransfer; //Transferred data!
								if ((sectorupdateintervalcnt >= sectorupdateinterval) || firstupdate) //Update every 10000 sectors!
								{
									sectorupdateintervalcnt %= sectorupdateinterval; //Reset counter as much as is needed!
									firstupdate = 0; //Not first anymore!
									EMU_locktext();
									GPU_EMU_printscreen(18, 8, "%u%%", (int)(((float)sectornr / (float)size) * 100.0f)); //Current progress!
									EMU_unlocktext();
								}
								sectornr += datatotransfer; //Next sector!
							}
							EMU_locktext();
							GPU_EMU_printscreen(18, 8, "%u%%", (int)(((float)sectornr / (float)size) * 100.0f)); //Current progress!
							EMU_unlocktext();
							if (error) //Error occurred?
							{
								remove(fullfilename); //Try to remove the generated file!
								dolog(filename, "Error %u validating dynamic image sector %u/%u@byte %u", error, sectornr / 512, size / 512, sectorposition ? sectorposition - 1 : 0); //Error at this sector!
							}
						}
						else //Error occurred?
						{
							dolog(filename, "Error #%u copying static image sector %u/%u", error, sectornr / 512, sizecreated / 512); //Error at this sector!
							if (!remove(fullfilename)) //Defragmented file can be removed?
							{
								dolog(filename, "Error cleaning up the new defragmented image!");
							}
						}
					}
				}
			}
		}
		break;
	}
	BIOS_Menu = 1; //Return to Disk Menu!
}

void BIOS_ConvertDynamicStaticHDD() //Generate Static HDD Image from a dynamic one!
{
	uint_64 datatotransfer;
	uint_32 sectorposition = 0; //Possible position of error!
	char filename[256]; //Filename container!
	char fullfilename[256];
	cleardata(&filename[0], sizeof(filename)); //Init!
	FILEPOS size = 0;
	BIOS_Title("Convert dynamic to static HDD Image"); //Full clear!
	generateFileList(diskpath,"sfdimg", 0, 1,1); //Generate file list for all .img files!
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Disk image: "); //Show selection init!
	EMU_unlocktext();
	selectingHDD = 1; //Selecting a HDD!
	int file = ExecuteList(12, 4, "", 256,&hdd_information,0); //Show menu for the disk image!
	switch (file) //Which file?
	{
	case FILELIST_NOFILES: //No files?
		BIOS_disk_nofiles(); //No files!
	case FILELIST_DEFAULT: //Unmount?
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	default: //File?
		safestrcpy(filename,sizeof(filename), itemlist[file]); //Use this file!
		if (strcmp(filename, "") != 0) //Got input?
		{
			BIOS_Title("Convert dynamic to static HDD Image"); //Full clear!
			EMU_locktext();
			EMU_textcolor(BIOS_ATTR_TEXT);
			EMU_gotoxy(0, 4); //Goto position for info!
			GPU_EMU_printscreen(0, 4, "Filename: %s  ", filename); //Show the filename!
			EMU_gotoxy(0, 5); //Next row!
			GPU_EMU_printscreen(0, 5, "Image size: "); //Show image size selector!!
			GPU_EMU_printscreen(0, 6, "Destination filename: ");
			EMU_unlocktext();
			iohdd0(filename, 0, 1, 0); //Mount the source disk!

			byte dynamicimage_type;
			memset(&fullfilename, 0, sizeof(fullfilename)); //Init!
			safestrcpy(fullfilename, sizeof(fullfilename), diskpath); //Disk path!
			safestrcat(fullfilename, sizeof(fullfilename), "/");
			safestrcat(fullfilename, sizeof(fullfilename), filename); //The full filename!

			dynamicimage_type = dynamictostatic_imagetype(fullfilename);

			size = getdisksize(HDD0); //Get the original size!
			EMU_locktext();
			GPU_EMU_printscreen(12, 5, "      "); //Clear the creation process!
			GPU_EMU_printscreen(12, 5, "%uMB", (size / MBMEMORY)); //Image size
			EMU_unlocktext();
			if (BIOS_InputText(22, 6, &filename[0], (sizeof(filename) - 1) - strlen(".img"), 0))
			{
				if (safestrlen(filename, sizeof(filename)) > (sizeof(filename) - strlen(".img") - 1)) //Too long filename?
				{
					size = 0; //Abort!
				}
				safestrcat(filename, sizeof(filename), ".img"); //Generate destination filename!
				if (size != 0) //Got size?
				{
					if (!strcmp(filename, BIOS_Settings.hdd0) || !strcmp(filename, BIOS_Settings.hdd1)) //Harddisk changed?
					{
						BIOS_Changed = 1; //We've changed!
						reboot_needed |= 2; //We're in need of a reboot!
					}
					if (!strcmp(disks[FLOPPY0].rawfilename, filename)) //Floppy #0 changed?
					{
						iofloppy0("", 0, BIOS_Settings.floppy0_readonly, 0); //Unmount!
						iofloppy0(BIOS_Settings.floppy0, 0, BIOS_Settings.floppy0_readonly, 0); //Remount to update!
					}
					if (!strcmp(disks[FLOPPY1].rawfilename, filename)) //Floppy #1 changed?
					{
						iofloppy1("", 0, BIOS_Settings.floppy1_readonly, 0); //Unmount!
						iofloppy1(BIOS_Settings.floppy1, 0, BIOS_Settings.floppy1_readonly, 0); //Remount to update!
					}
					EMU_locktext();
					EMU_gotoxy(0, 6); //Next row!
					GPU_EMU_printscreen(0, 7, "Generating image: "); //Start of percentage!
					GPU_EMU_printscreen(18, 7, "      "); //Clear the creation process!
					FILEPOS sectornr = 0;
					EMU_gotoxy(0, 7); //Next row!
					GPU_EMU_printscreen(0, 7, "Generating image: "); //Start of percentage!
					EMU_unlocktext();
					byte error = 0;
					sectorupdateinterval = MAX((size / 100), sizeof(sector)); //Update interval!
					sectorupdateintervalcnt = 0; //Reset counter for the first update!
					byte firstupdate = 1; //First update!
					BIGFILE* dest;
					domkdir(diskpath); //Make sure our directory we're creating an image in exists!
					memset(&fullfilename, 0, sizeof(fullfilename));
					safestrcpy(fullfilename, sizeof(fullfilename), diskpath);
					safestrcat(fullfilename, sizeof(fullfilename), "/");
					safestrcat(fullfilename, sizeof(fullfilename), filename);

					if (!generateStaticImageFormat(fullfilename, dynamicimage_type)) //Failed generating the format to use?
					{
						error = 2; //Error #2!
						goto handleDynamicStaticCreationError;
					}

					dest = emufopen64(fullfilename, "wb"); //Open the destination!
					for (sectornr = 0; sectornr < size;) //Process all sectors!
					{
						if (shuttingdown())
						{
							error = 4; //Give the fourth error!
							break;
						}
						if ((size - sectornr) > sizeof(sector)) //Too much to handle?
						{
							datatotransfer = sizeof(sector); //Limit to max!
						}
						else //What's left?
						{
							datatotransfer = size;
							datatotransfer -= sectornr; //How many bytes of data to transfer?
						}
						if (readdata(HDD0, &sector, sectornr, (uint_32)datatotransfer)) //Read a sector?
						{
							if (emufwrite64(&sector, 1, datatotransfer, dest) != (int_64)datatotransfer) //Error writing a sector?
							{
								error = 2;
								break; //Stop reading!
							}
						}
						else //Error reading sector?
						{
							error = 1;
							break; //Stop reading!
						}
						sectorupdateintervalcnt += datatotransfer; //Transferred data!
						if ((sectorupdateintervalcnt >= sectorupdateinterval) || firstupdate) //Update every 10000 sectors!
						{
							sectorupdateintervalcnt %= sectorupdateinterval; //Reset counter as much as is needed!
							firstupdate = 0; //Not first update anymore!
							EMU_locktext();
							GPU_EMU_printscreen(18, 7, "%u%%", (int)(((float)sectornr / (float)size) * 100.0f)); //Current progress!
							EMU_unlocktext();
						}
						sectornr += datatotransfer; //Next sector!
					}
					emufclose64(dest); //Close the file!

					EMU_locktext();
					GPU_EMU_printscreen(18, 7, "%u%%", (int)(((float)sectornr / (float)size) * 100.0f)); //Current progress!
					EMU_unlocktext();

					//Verification!
					if (!error) //OK?
					{
						EMU_locktext();
						GPU_EMU_printscreen(0, 8, "Validating image: "); //Start of percentage!
						EMU_unlocktext();
						iohdd1(filename, 0, 1, 0); //Mount!
						sectorupdateinterval = MAX((size / 100), sizeof(sector)); //Update interval!
						sectorupdateintervalcnt = 0; //Reset counter for the first update!
						firstupdate = 1; //First update!
						for (sectornr = 0; sectornr < size;) //Process all sectors!
						{
							if (shuttingdown())
							{
								error = 4; //Give the fourth error!
								break;
							}
							if ((size - sectornr) > sizeof(sector)) //Too much to handle?
							{
								datatotransfer = sizeof(sector); //Limit to max!
							}
							else
							{
								datatotransfer = size;
								datatotransfer -= sectornr; //How many bytes of data to transfer?
							}
							if (readdata(HDD0, &sector, sectornr, (uint_32)datatotransfer)) //Read a sector?
							{
								if (!readdata(HDD1, &verificationsector, sectornr, (uint_32)datatotransfer)) //Error reading a sector?
								{
									error = 2;
									break; //Stop reading!
								}
								else if ((sectorposition = memcmp(&sector, &verificationsector, (size_t)datatotransfer)) != 0)
								{
									error = 3; //Verification error!
									break; //Stop reading!
								}
							}
							else //Error reading sector?
							{
								error = 1;
								break; //Stop reading!
							}
							sectorupdateintervalcnt += datatotransfer; //Transferred data!
							if ((sectorupdateintervalcnt >= sectorupdateinterval) || firstupdate) //Update every 10000 sectors!
							{
								sectorupdateintervalcnt %= sectorupdateinterval; //Reset counter as much as is needed!
								firstupdate = 0; //Not first update anymore!
								EMU_locktext();
								GPU_EMU_printscreen(18, 8, "%u%%", (int)(((float)sectornr / (float)size) * 100.0f)); //Current progress!
								EMU_unlocktext();
							}
							sectornr += datatotransfer; //Next sector!
						}
						EMU_locktext();
						GPU_EMU_printscreen(18, 7, "%u%%", (int)(((float)sectornr / (float)size) * 100.0f)); //Current progress!
						EMU_unlocktext();
						if (error) //Error occurred?
						{
							deleteStaticImageCompletely(fullfilename); //Try to remove the generated file!
							dolog(filename, "Error #%u validating static image sector %u/%u@byte %u", error, sectornr / 512, size / 512, sectorposition ? sectorposition - 1 : 0); //Error at this sector!
						}
					}
					else //Error occurred?
					{
					handleDynamicStaticCreationError:
						dolog(filename, "Error #%u copying dynamic image sector %u/%u", error, sectornr / 512, size / 512); //Error at this sector!
						if (!deleteStaticImageCompletely(fullfilename)) //Defragmented file can be removed?
						{
							dolog(filename, "Error cleaning up the new defragmented image!");
						}
					}
				}
			}
		}
		break;
	}
	BIOS_Menu = 1; //Return to Disk Menu!
}

void BIOS_DefragmentDynamicHDD() //Defragment a dynamic HDD Image!
{
	char errorlog[256]; //Error log string!
	FILEPOS updateinterval=1,updatenr;
	char filename[256], originalfilename[256]; //Filename container!
	char fullfilename[256], fulloriginalfilename[256]; //Full filename container!
	sbyte srcstatus=-1,deststatus=-1; //Status on the two dynamic disk images!
	cleardata(&filename[0], sizeof(filename)); //Init!
	FILEPOS size = 0, sectorposition=0;
	BIOS_Title("Defragment a dynamic HDD Image"); //Full clear!
	generateFileList(diskpath,"sfdimg", 0, 1,1); //Generate file list for all .img files!
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Disk image: "); //Show selection init!
	EMU_unlocktext();
	selectingHDD = 1; //Selecting a HDD!
	int file = ExecuteList(12, 4, "", 256,&hdd_information,0); //Show menu for the disk image!
	switch (file) //Which file?
	{
	case FILELIST_NOFILES: //No files?
		BIOS_disk_nofiles(); //No files!
	case FILELIST_DEFAULT: //Unmount?
	case FILELIST_CANCEL: //Cancelled?
		break;
	default: //File?
		safestrcpy(filename,sizeof(filename), itemlist[file]); //Use this file!

		if (strcmp(filename, "") != 0) //Got input?
		{
			BIOS_Title("Defragment a dynamic HDD Image"); //Full clear!
			EMU_locktext();
			EMU_textcolor(BIOS_ATTR_TEXT);
			EMU_gotoxy(0, 4); //Goto position for info!
			GPU_EMU_printscreen(0, 4, "Filename: %s ", filename); //Show the filename!
			EMU_gotoxy(0, 5); //Next row!
			GPU_EMU_printscreen(0, 5, "Image size: "); //Show image size selector!!
			EMU_unlocktext();
			cleardata(&originalfilename[0], sizeof(originalfilename)); //Init!
			safestrcpy(originalfilename,sizeof(originalfilename), filename); //The original filename!

			domkdir(diskpath);

			memset(&fulloriginalfilename, 0, sizeof(fulloriginalfilename));
			safestrcpy(fulloriginalfilename,sizeof(fulloriginalfilename), diskpath);
			safestrcat(fulloriginalfilename,sizeof(fulloriginalfilename), "/");
			safestrcat(fulloriginalfilename,sizeof(fulloriginalfilename), filename);

			safestrcat(filename, sizeof(filename), ".tmp.sfdimg"); //Generate destination filename!
			memset(&fullfilename, 0, sizeof(fullfilename));
			safestrcpy(fullfilename, sizeof(fullfilename), diskpath);
			safestrcat(fullfilename, sizeof(fullfilename), "/");
			safestrcat(fullfilename, sizeof(fullfilename), filename);

			size = dynamicimage_getsize(fulloriginalfilename); //Get the original size!
			if (size != 0) //Got size?
			{
				EMU_locktext();
				EMU_gotoxy(0, 6); //Next row!
				GPU_EMU_printscreen(0, 6, "Defragmenting image: "); //Start of percentage!
				EMU_unlocktext();
				FILEPOS sizecreated;
				sizecreated = generateDynamicImage(filename, size, 21, 6,is_dynamicimage(originalfilename)); //Generate a dynamic image!
				if (sizecreated >= size) //Correct size?
				{
					EMU_locktext();
					GPU_EMU_printscreen(21, 6, "      "); //Clear the creation process!
					GPU_EMU_printscreen(12, 5, "      "); //Clear the creation process!
					GPU_EMU_printscreen(12, 5, "%uMB", (sizecreated / MBMEMORY)); //Image size
					EMU_unlocktext();
					uint_32 sectornr,destsectornr,previoussectornr=0,previousdestsectornr=0;
					byte error = 0;
					size >>= 9; //Convert to actual 512-byte sector numbers: we're allowed in this case!
					updateinterval = (size/100); //Update interval in sectors: every 1% updated!
					if (!updateinterval) updateinterval = 1; //Minimum of 1 sector interval!
					updatenr = 0; //Reset update number!
					srcstatus = 0; //Initialize to EOF!
					byte firstupdate = 1; //First update!
					for (sectornr = 0; sectornr < size;) //Process all sectors from the source image!
					{
						sectorposition = 0; //Default: no position!
						if (shuttingdown())
						{
							error = 4; //Give the fourth error!
							break;
						}
						
						if (dynamicimage_readexistingsector(fulloriginalfilename,sectornr,&sector)) //Sector exists and non-empty? Then try to copy it to the new disk image!
						{
							if (!dynamicimage_writesector(fullfilename, sectornr, &sector)) //Error writing a sector?
							{
								error = 2;
								break; //Stop reading!
							}
						}
						previoussectornr = sectornr; //Last sector number to compare to!
						srcstatus = dynamicimage_nextallocatedsector(fulloriginalfilename,&sectornr); //Next sector or block etc. which is available!
						switch (srcstatus) //What status?
						{
							case -2: //Special: termination request!
								error = 4; //Give the fourth error!
							case 0: //EOF reached?
								goto finishedphase1; //Finished transferring!
							case -1: //Error in file?
								error = 1;
								goto finishedphase1; //Finished transferring!
							default: //Unknown?
							case 1: //Next sector to process?
								break; //Continue running on the next sector to process!
						}
						updatenr += sectornr-previoussectornr; //Last processed sector number difference!
						if ((updatenr>=updateinterval) || firstupdate) //Update every 1% sectors!
						{
							updatenr = 0; //Reset!
							firstupdate = 0; //Not first update anymore!
							EMU_locktext();
							GPU_EMU_printscreen(21, 6, "%u%%", (int)(((float)sectornr / (float)size)*100.0f)); //Current progress!
							EMU_unlocktext();
						}
					}
					finishedphase1:
					EMU_locktext();
					GPU_EMU_printscreen(21, 6, "%u%%", (int)(((float)sectornr / (float)size)*100.0f)); //Current progress!
					EMU_unlocktext();

					//Verification!
					if ((error==0) && (srcstatus==0)) //OK and fully processed?
					{
						EMU_locktext();
						GPU_EMU_printscreen(0, 7, "Validating image: "); //Start of percentage!
						EMU_unlocktext();
						updatenr = 0; //Reset update number!
						destsectornr = previoussectornr = previousdestsectornr = 0; //Destination starts at sector #0 too!
						error = 0; //Default: no error!
						firstupdate = 1; //First update!
						for (sectornr = 0; sectornr < size;) //Process all sectors!
						{
							sectorposition = 0; //Default: no position!
							if (shuttingdown())
							{
								error = 4; //Give the fourth error!
								break;
							}

							if (dynamicimage_readexistingsector(fulloriginalfilename,sectornr,&sector)) //Sector exists in the old disk image? Then try to check it to the new disk image!
							{
								if (dynamicimage_readexistingsector(fullfilename,sectornr,&verificationsector)) //Sector exists in the new disk image? Then try to check it from the new disk image!
								{
									if ((sectorposition = memcmp(&sector, &verificationsector, 512)) != 0) //Data error?
									{
										error = 3; //Verification error!
										break; //Stop reading!
									}
									//We're a valid written sector!
								}
								else //Missing defragmented sector!
								{
									error = 3; //Verification error!
									break; //Stop reading!
								}
							}
							else if (dynamicimage_readexistingsector(fullfilename,sectornr,&verificationsector)) //Sector exists in the new disk image but not in the old disk image?
							{
								error = 3; //Verification error: exists within defragemented image but not in source image!
								break; //Stop reading!
							}
							//We're a valid written or non-existing sector!

							previoussectornr = sectornr; //Last sector number to compare to!
							previousdestsectornr = destsectornr; //Save the previous for comparing!
							deststatus = dynamicimage_nextallocatedsector(fullfilename,&destsectornr); //Next sector or block etc. which is available!
							srcstatus = dynamicimage_nextallocatedsector(fulloriginalfilename,&sectornr); //Next sector or block etc. which is available!
							if ((deststatus!=srcstatus) || (sectornr!=destsectornr)) //Next status or sector number differs?
							{
								error = 2; //Position/status error!
								goto finishedphase2;
							}
							switch (srcstatus) //What status?
							{
								case -2: //Special: termination request!
									error = 4; //Give the fourth error!
								case 0: //EOF reached?
									goto finishedphase2; //Finished transferring!
								case -1: //Error in file?
									error = 1; //Error!
									goto finishedphase2; //Finished transferring!
								default: //Unknown?
								case 1: //Next sector to process?
									break; //Continue running on the next sector to process!
							}
							updatenr += sectornr-previoussectornr; //Last processed sector number difference!
							if ((updatenr>=updateinterval) || firstupdate) //Update every 1% sectors!
							{
								updatenr = 0; //Reset!
								firstupdate = 0; //Not first update anymore!
								EMU_locktext();
								GPU_EMU_printscreen(18, 7, "%u%%", (int)(((float)sectornr / (float)size)*100.0f)); //Current progress!
								EMU_unlocktext();
							}
						}
						finishedphase2:
						EMU_locktext();
						GPU_EMU_printscreen(18, 7, "%u%%", (int)(((float)sectornr / (float)size)*100.0f)); //Current progress!
						EMU_unlocktext();
						if (error) //Error occurred?
						{
							cleardata(&errorlog[0],sizeof(errorlog)); //Clear the error log data!
							if (error==2) //Position/status error?
							{
								snprintf(errorlog,sizeof(errorlog),"Position/status error: Source status: %i, Destination status: %i, Error source sector: %" SPRINTF_u_UINT32 ", error destination sector: %" SPRINTF_u_UINT32,srcstatus,deststatus,sectornr,destsectornr);
							}
							switch (srcstatus) //What status?
							{
								case 0: //EOF reached?
									safestrcat(errorlog,sizeof(errorlog),"\nSource: EOF"); //Finished transferring!
									break;
								case -1: //Error in file?
									safestrcat(errorlog,sizeof(errorlog),"\nSource: ERROR"); //Finished transferring!
								case -2: //Special: termination request!
									goto finishedphase3; //Finished transferring!
								default: //Unknown?
								case 1: //Next sector to process?
									safescatnprintf(errorlog,sizeof(errorlog),"\nSource: sector %u",sectornr); //This sector!
									break; //Continue running on the next sector to process!
							}
							switch (deststatus) //What status?
							{
								case 0: //EOF reached?
									safestrcat(errorlog,sizeof(errorlog),"\nDestination: EOF"); //Finished transferring!
									break;
								case -1: //Error in file?
									safestrcat(errorlog,sizeof(errorlog),"\nDestination: ERROR"); //Finished transferring!
								case -2: //Special: termination request!
									goto finishedphase3; //Finished transferring!
								default: //Unknown?
								case 1: //Next sector to process?
									safescatnprintf(errorlog,sizeof(errorlog),"\nDestination: sector %u",sectornr); //This sector!
									break; //Continue running on the next sector to process!
							}
							finishedphase3: //Finished transferring!
							safescatnprintf(errorlog,sizeof(errorlog),"\nPrevious source sector: %u\nPrevious destination sector: %u",previoussectornr,previousdestsectornr); //Previous sector numbers!
							dolog(originalfilename, "Error %u validating dynamic image sector %u/%u@byte %u", error, sectornr, size, sectorposition?sectorposition-1:0); //Error at this sector!
							dolog(originalfilename, "\n%s",errorlog); //Error at this sector information!
							if (!remove(fullfilename)) //Incomplete file can't be removed?
							{
								dolog(originalfilename, "Error cleaning up the new defragmented image!");
							}
						}
						else //We've been defragmented?
						{
							if (!remove(fulloriginalfilename)) //Original can be removed?
							{
								if (rename(fullfilename, fulloriginalfilename)) //The destination is the new original!
								{
									dolog(originalfilename, "Error renaming the new defragmented image to the original filename!");
								}
							}
							else
							{
								dolog(originalfilename, "Error replacing the old image with the defragmented image!");
							}
						}
					}
					else //Error occurred?
					{
						dolog(originalfilename, "Error #%u copying dynamic image sector to defragmented image sector %u/%u", error, sectornr, size); //Error at this sector!
						switch (srcstatus) //What status?
						{
							case 0: //EOF reached?
								safestrcat(errorlog,sizeof(errorlog),"Source: EOF"); //Finished transferring!
								break;
							case -1: //Error in file?
								safestrcat(errorlog,sizeof(errorlog),"Source: ERROR"); //Finished transferring!
								break;
							default: //Unknown?
							case 1: //Next sector to process?
								snprintf(errorlog,sizeof(errorlog),"Source: sector %" SPRINTF_u_UINT32,sectornr); //This sector!
								break; //Continue running on the next sector to process!
						}
						if (!remove(fullfilename)) //Defragmented file can be removed?
						{
							dolog(originalfilename, "Error cleaning up the new defragmented image!");
						}
					}
				}
			}
		}
		break;
	}
	BIOS_Menu = 1; //Return to Disk Menu!
}

void BIOS_DebugMode()
{
	BIOS_Title("Debug mode");
	EMU_locktext();
	EMU_gotoxy(0,4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0,4,"Debug mode: "); //Show selection init!
	EMU_unlocktext();
	int i = 0; //Counter!
	numlist = 5; //Amount of Debug modes!
	for (i=0; i<numlist; i++) //Process options!
	{
		cleardata(&itemlist[i][0],sizeof(itemlist[i])); //Reset!
	}

	safestrcpy(itemlist[DEBUGMODE_NONE],sizeof(itemlist[0]),"Disabled"); //Set filename from options!
	safestrcpy(itemlist[DEBUGMODE_RTRIGGER],sizeof(itemlist[0]),"Enabled, RTrigger=Step"); //Set filename from options!
	safestrcpy(itemlist[DEBUGMODE_STEP],sizeof(itemlist[0]),"Enabled, Step through"); //Set filename from options!
	safestrcpy(itemlist[DEBUGMODE_SHOW_RUN],sizeof(itemlist[0]),"Enabled, just run, ignore shoulder buttons"); //Set filename from options!
	safestrcpy(itemlist[DEBUGMODE_NOSHOW_RUN],sizeof(itemlist[0]),"Enabled, just run, don't show, ignore shoulder buttons"); //Set filename from options!

	int current = 0;
	switch (BIOS_Settings.debugmode) //What debug mode?
	{
	case DEBUGMODE_NONE: //Valid
	case DEBUGMODE_RTRIGGER: //Valid
	case DEBUGMODE_STEP: //Valid
	case DEBUGMODE_SHOW_RUN: //Valid
	case DEBUGMODE_NOSHOW_RUN: //Valid
		current = BIOS_Settings.debugmode; //Valid: use!
		break;
	default: //Invalid
		current = DEFAULT_DEBUGMODE; //Default: none!
		break;
	}
	if (BIOS_Settings.debugmode!=current) //Invalid?
	{
		BIOS_Settings.debugmode = current; //Safety!
		BIOS_Changed = 1; //Changed!
	}
	int file = ExecuteList(15,4,itemlist[current],256,NULL,0); //Show options for the installed CPU!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = DEFAULT_DEBUGMODE; //Default debugmode: None!

	case DEBUGMODE_NONE:
	case DEBUGMODE_RTRIGGER:
	case DEBUGMODE_STEP:
	case DEBUGMODE_SHOW_RUN:
	case DEBUGMODE_NOSHOW_RUN:
	default: //Changed?
		if (file!=current) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			BIOS_Settings.debugmode = file; //Select Debug Mode!
		}
		break;
	}
	BIOS_Menu = 94; //Goto CPU Debugger menu!
}

void BIOS_ExecutionMode()
{
	BIOS_Title("Execution mode");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Execution mode: "); //Show selection init!
	EMU_unlocktext();
	int i = 0; //Counter!
	numlist = 6; //Amount of Execution modes!
	for (i = 0; i<numlist; i++) //Process options!
	{
		cleardata(&itemlist[i][0], sizeof(itemlist[i])); //Reset!
	}

	safestrcpy(itemlist[EXECUTIONMODE_NONE],sizeof(itemlist[0]), "Use emulator internal BIOS"); //Set filename from options!
	safestrcpy(itemlist[EXECUTIONMODE_TEST],sizeof(itemlist[0]), "Run debug directory files, else TESTROM.DAT at 0000:0000"); //Set filename from options!
	safestrcpy(itemlist[EXECUTIONMODE_TESTROM],sizeof(itemlist[0]), "Run TESTROM.DAT at 0000:0000"); //Set filename from options!
	safestrcpy(itemlist[EXECUTIONMODE_VIDEOCARD],sizeof(itemlist[0]), "Debug video card output"); //Set filename from options!
	safestrcpy(itemlist[EXECUTIONMODE_BIOS],sizeof(itemlist[0]), "Load BIOS from ROM directory as BIOSROM.U* and OPTROM.*"); //Set filename from options!
	safestrcpy(itemlist[EXECUTIONMODE_SOUND],sizeof(itemlist[0]), "Run sound test"); //Debug sound test!

	int current = 0;
	switch (BIOS_Settings.executionmode) //What execution mode?
	{
	case EXECUTIONMODE_NONE: //Valid
	case EXECUTIONMODE_TEST: //Test files or biosrom.dat!
	case EXECUTIONMODE_TESTROM: //Test ROM?
	case EXECUTIONMODE_VIDEOCARD: //Text character debugging?
	case EXECUTIONMODE_BIOS: //External BIOS?
	case EXECUTIONMODE_SOUND: //Sound test?
		current = BIOS_Settings.executionmode; //Valid: use!
		break;
	default: //Invalid
		current = DEFAULT_EXECUTIONMODE; //Default: none!
		break;
	}
	if (BIOS_Settings.executionmode != current) //Invalid?
	{
		BIOS_Settings.executionmode = current; //Safety!
		BIOS_Changed = 1; //Changed!
	}
	int file = ExecuteList(16, 4, itemlist[current], 256,NULL,0); //Show options for the installed CPU!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = DEFAULT_EXECUTIONMODE; //Default execution mode: None!

	case EXECUTIONMODE_NONE:
	case EXECUTIONMODE_TEST:
	case EXECUTIONMODE_TESTROM:
	case EXECUTIONMODE_VIDEOCARD:
	case EXECUTIONMODE_BIOS:
	case EXECUTIONMODE_SOUND:
	default: //Changed?
		if (file != current) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			BIOS_Settings.executionmode = file; //Select Debug Mode!
			reboot_needed |= (EMU_RUNNING?1:0); //We need to reboot when running: our execution mode has been changed!
		}
		break;
	}
	BIOS_Menu = 35; //Goto CPU menu!
}

void BIOS_DebugLog()
{
	BIOS_Title("Debugger log");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Debugger log: "); //Show selection init!
	EMU_unlocktext();
	int i = 0; //Counter!
	numlist = 14; //Amount of Execution modes!
	for (i = 0; i<numlist; i++) //Process options!
	{
		cleardata(&itemlist[i][0], sizeof(itemlist[i])); //Reset!
	}

	safestrcpy(itemlist[DEBUGGERLOG_NONE],sizeof(itemlist[0]), "Don't log"); //Set filename from options!
	safestrcpy(itemlist[DEBUGGERLOG_DEBUGGING],sizeof(itemlist[0]), "Only when debugging"); //Set filename from options!
	safestrcpy(itemlist[DEBUGGERLOG_ALWAYS],sizeof(itemlist[0]), "Always log"); //Set filename from options!
	safestrcpy(itemlist[DEBUGGERLOG_INT],sizeof(itemlist[0]),"Interrupt calls only");
	safestrcpy(itemlist[DEBUGGERLOG_DIAGNOSTICCODES],sizeof(itemlist[0]), "BIOS Diagnostic codes only");
	safestrcpy(itemlist[DEBUGGERLOG_ALWAYS_NOREGISTERS],sizeof(itemlist[0]),"Always log, no register state");
	safestrcpy(itemlist[DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP],sizeof(itemlist[0]),"Always log, even during skipping");
	safestrcpy(itemlist[DEBUGGERLOG_ALWAYS_SINGLELINE],sizeof(itemlist[0]),"Always log, even during skipping, single line format");
	safestrcpy(itemlist[DEBUGGERLOG_DEBUGGING_SINGLELINE],sizeof(itemlist[0]),"Only when debugging, single line format");
	safestrcpy(itemlist[DEBUGGERLOG_ALWAYS_SINGLELINE_SIMPLIFIED],sizeof(itemlist[0]),"Always log, even during skipping, single line format, simplified");
	safestrcpy(itemlist[DEBUGGERLOG_DEBUGGING_SINGLELINE_SIMPLIFIED],sizeof(itemlist[0]),"Only when debugging, single line format, simplified");
	safestrcpy(itemlist[DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT],sizeof(itemlist[0]),"Always log, common log format");
	safestrcpy(itemlist[DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT],sizeof(itemlist[0]),"Always log, even during skipping, common log format");
	safestrcpy(itemlist[DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT],sizeof(itemlist[0]),"Only when debugging, common log format");

	int current = 0;
	switch (BIOS_Settings.debugger_log) //What debugger log mode?
	{
	case DEBUGGERLOG_NONE: //None
	case DEBUGGERLOG_DEBUGGING: //Only when debugging
	case DEBUGGERLOG_ALWAYS: //Always
	case DEBUGGERLOG_INT: //Interrupt calls only
	case DEBUGGERLOG_DIAGNOSTICCODES: //Diagnostic codes only
	case DEBUGGERLOG_ALWAYS_NOREGISTERS: //Always, no register state!
	case DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP: //Always, even during skipping
	case DEBUGGERLOG_ALWAYS_SINGLELINE: //Always log, even during skipping, single line format
	case DEBUGGERLOG_DEBUGGING_SINGLELINE: //Only when debugging, single line format
	case DEBUGGERLOG_ALWAYS_SINGLELINE_SIMPLIFIED: //Always log, even during skipping, single line format, simplified
	case DEBUGGERLOG_DEBUGGING_SINGLELINE_SIMPLIFIED: //Only when debugging, single line format, simplified
	case DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT: //Always log, common log format
	case DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT: //Always log, even during skipping, common log format
	case DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT: //Only when debugging, common log format
		current = BIOS_Settings.debugger_log; //Valid: use!
		break;
	default: //Invalid
		current = DEFAULT_DEBUGGERLOG; //Default: none!
		break;
	}
	if (BIOS_Settings.debugger_log != current) //Invalid?
	{
		BIOS_Settings.debugger_log = current; //Safety!
		BIOS_Changed = 1; //Changed!
	}
	int file = ExecuteList(14, 4, itemlist[current], 256,NULL,0); //Show options for the installed CPU!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = DEFAULT_DEBUGGERLOG; //Default execution mode: None!

	case DEBUGGERLOG_NONE: //None
	case DEBUGGERLOG_DEBUGGING: //Only when debugging
	case DEBUGGERLOG_ALWAYS: //Always
	case DEBUGGERLOG_INT: //Interrupt calls only
	case DEBUGGERLOG_DIAGNOSTICCODES: //Diagnostic codes only
	case DEBUGGERLOG_ALWAYS_NOREGISTERS: //Always log, no register state!
	case DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP: //Always, even during skipping
	case DEBUGGERLOG_ALWAYS_SINGLELINE: //Always log, even during skipping, single line format
	case DEBUGGERLOG_DEBUGGING_SINGLELINE: //Only when debugging, single line format
	case DEBUGGERLOG_ALWAYS_SINGLELINE_SIMPLIFIED: //Always log, even during skipping, single line format, simplified
	case DEBUGGERLOG_DEBUGGING_SINGLELINE_SIMPLIFIED: //Only when debugging, single line format, simplified
	case DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT: //Always log, common log format
	case DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT: //Always log, even during skipping, common log format
	case DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT: //Only when debugging, common log format
	default: //Changed?
		if (file != current) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			BIOS_Settings.debugger_log = file; //Select Debug Mode!
		}
		break;
	}
	BIOS_Menu = 94; //Goto CPU Debugger menu!
}

void BIOS_DebugState()
{
	BIOS_Settings.debugger_logstates = !BIOS_Settings.debugger_logstates;
	BIOS_Changed = 1; //We're changed!
	BIOS_Menu = 94; //Goto CPU Debugger menu!
}

extern byte force_memoryredetect; //From the MMU: force memory redetect on load?

void BIOS_MemReAlloc() //Reallocates BIOS memory!
{
	BIOS_Menu = 8; //Goto Advanced menu!
	*(getarchmemory()) = 0; //Reset the memory flag!
	BIOS_Changed = 1; //We're changed!
	reboot_needed |= 2; //We need to reboot!
	return; //Disable due to the fact that memory allocations aren't 100% OK atm. Redetect the memory after rebooting!

	force_memoryredetect = 1; //We're forcing memory redetect!
	doneEMU(); //Finish the old EMU memory!

	autoDetectMemorySize(0); //Check memory size if needed!
	initEMU(1); //Start a new EMU memory!
	
	BIOS_Changed = 1; //Changed!
	BIOS_Menu = 8; //Goto Advanced menu!
	reboot_needed |= 2; //We need to reboot!
}

void BIOS_DirectPlotSetting()
{
	BIOS_Title("Direct plot");
	EMU_locktext();
	EMU_gotoxy(0,4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0,4,"Direct plot: "); //Show selection init!
	EMU_unlocktext();
	int i = 0; //Counter!
	numlist = 3; //Amount of Direct modes!
	for (i=0; i<3; i++) //Process options!
	{
		cleardata(&itemlist[i][0],sizeof(itemlist[i])); //Reset!
	}
	safestrcpy(itemlist[0],sizeof(itemlist[0]),"Disabled"); //Set filename from options!
	safestrcpy(itemlist[1],sizeof(itemlist[0]),"Automatic"); //Set filename from options!
	safestrcpy(itemlist[2],sizeof(itemlist[0]),"Forced"); //Set filename from options!
	if (BIOS_Settings.GPU_AllowDirectPlot >= numlist) //Invalid?
	{
		BIOS_Settings.GPU_AllowDirectPlot = DEFAULT_DIRECTPLOT; //Default!
		BIOS_Changed = 1; //We've changed!
	}
	int current = 0;
	switch (BIOS_Settings.GPU_AllowDirectPlot) //What direct plot?
	{
	case 0: //Valid
	case 1: //Valid
	case 2: //Valid
		current = BIOS_Settings.GPU_AllowDirectPlot; //Valid: use!
		break;
	default: //Invalid
		current = 0; //Default: none!
		break;
	}
	if (BIOS_Settings.GPU_AllowDirectPlot!=current) //Invalid?
	{
		BIOS_Settings.GPU_AllowDirectPlot = current; //Safety!
		BIOS_Changed = 1; //Changed!
	}
	int file = ExecuteList(15,4,itemlist[current],256,NULL,0); //Show options for the installed CPU!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = DEFAULT_DIRECTPLOT; //Default direct plot: None!

	case 0:
	case 1:
	default: //Changed?
		if (file!=current) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			BIOS_Settings.GPU_AllowDirectPlot = file; //Select Direct Plot setting!
		}
		break;
	}
	BIOS_Menu = 29; //Goto Video Settings menu!
}

void BIOS_FontSetting()
{
	BIOS_Title("Font");
	EMU_locktext();
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0,4,"Settings menu Font: "); //Show selection init!
	EMU_unlocktext();
	int i = 0; //Counter!
	numlist = NUMITEMS(BIOSMenu_Fonts); //Amount of Direct modes!
	for (i=0; i<numlist; i++) //Process options!
	{
		cleardata(&itemlist[i][0],sizeof(itemlist[i])); //Reset!
		safestrcpy(itemlist[i],sizeof(itemlist[0]),BIOSMenu_Fonts[i].name); //Use the name!
	}
	int current = 0;
	if (BIOS_Settings.BIOSmenu_font<NUMITEMS(BIOSMenu_Fonts)) //Valid font?
	{
		current = BIOS_Settings.BIOSmenu_font; //Valid: use!
	}
	else
	{
		current = 0; //Default: none!
	}
	if (BIOS_Settings.BIOSmenu_font!=current) //Invalid?
	{
		BIOS_Settings.BIOSmenu_font = current; //Safety!
		BIOS_Changed = 1; //Changed!
	}
	int font = ExecuteList(21,4,itemlist[current],256,NULL,0); //Show options for the installed CPU!
	switch (font) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		font = 0; //Default font: Standard!
	default: //Changed?
		if (font!=current && current<(int)NUMITEMS(BIOSMenu_Fonts)) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			BIOS_Settings.BIOSmenu_font = font; //Select Direct Plot setting!
		}
		break;
	}
	BIOS_Menu = 8; //Goto Advanced menu!
}

void BIOS_AspectRatio()
{
	BIOS_Title("Aspect ratio");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Aspect ratio: "); //Show selection init!
	EMU_unlocktext();
	int i = 0; //Counter!
	numlist = 8; //Amount of Aspect Ratio modes!
	for (i = 0; i<numlist; i++) //Process options!
	{
		cleardata(&itemlist[i][0], sizeof(itemlist[i])); //Reset!
	}
	if (BIOS_Settings.aspectratio>=numlist) //Too high?
	{
		BIOS_Settings.aspectratio = DEFAULT_ASPECTRATIO; //Set the default!
		BIOS_Changed = 1; //Changed!
	}
	safestrcpy(itemlist[0],sizeof(itemlist[0]), "Fullscreen stretching"); //Set filename from options!
	safestrcpy(itemlist[1],sizeof(itemlist[0]), "Keep the same"); //Set filename from options!
	safestrcpy(itemlist[2],sizeof(itemlist[0]), "Force 4:3(VGA)"); //Set filename from options!
	safestrcpy(itemlist[3],sizeof(itemlist[0]), "Force CGA"); //Set filename from options!
	safestrcpy(itemlist[4],sizeof(itemlist[0]), "Force 4:3(SVGA 768p)"); //Set filename from options!
	safestrcpy(itemlist[5],sizeof(itemlist[0]), "Force 4:3(SVGA 1080p)"); //Set filename from options!
	safestrcpy(itemlist[6],sizeof(itemlist[0]), "Force 4K"); //Set filename from options!
	safestrcpy(itemlist[7], sizeof(itemlist[0]), "Force 4:3(SVGA 4K)"); //Set filename from options!
	int current = 0;
	switch (BIOS_Settings.aspectratio) //What direct plot?
	{
	case 0: //Valid
	case 1: //Valid
	case 2: //Valid
	case 3: //Valid
	case 4: //Valid
	case 5: //Valid
	case 6: //Valid
	case 7: //Valid
		current = BIOS_Settings.aspectratio; //Valid: use!
		break;
	default: //Invalid
		current = 0; //Default: none!
		break;
	}
	if (BIOS_Settings.aspectratio != current) //Invalid?
	{
		BIOS_Settings.aspectratio = current; //Safety!
		BIOS_Changed = 1; //Changed!
	}
	int file = ExecuteList(15, 4, itemlist[current], 256, NULL,0); //Show options for the installed CPU!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = DEFAULT_ASPECTRATIO; //Default direct plot: None!

	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
	default: //Changed?
		if (file != current) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			BIOS_Settings.aspectratio = file; //Select Aspect Ratio setting!
		}
		break;
	}
	BIOS_Menu = 29; //Goto Video menu!
}

void BIOS_BWMonitor()
{
	BIOS_Title("Monitor");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Monitor: "); //Show selection init!
	EMU_unlocktext();
	int i = 0; //Counter!
	numlist = 4; //Amount of Execution modes!
	for (i = 0; i<numlist; i++) //Process options!
	{
		cleardata(&itemlist[i][0], sizeof(itemlist[i])); //Reset!
	}

	safestrcpy(itemlist[BWMONITOR_NONE],sizeof(itemlist[0]), "Color"); //Set filename from options!
	safestrcpy(itemlist[BWMONITOR_WHITE],sizeof(itemlist[0]), "B/W monitor: white"); //Set filename from options!
	safestrcpy(itemlist[BWMONITOR_GREEN],sizeof(itemlist[0]), "B/W monitor: green"); //Set filename from options!
	safestrcpy(itemlist[BWMONITOR_AMBER],sizeof(itemlist[0]), "B/W monitor: amber"); //Set filename from options!

	if (BIOS_Settings.bwmonitor>=numlist) //Invalid?
	{
		BIOS_Settings.bwmonitor = DEFAULT_BWMONITOR; //Default!
		BIOS_Changed = 1; //We've changed!
	}

	int current = 0;
	switch (BIOS_Settings.bwmonitor) //What B/W monitor mode?
	{
	case BWMONITOR_NONE: //None
	case BWMONITOR_WHITE: //Black/White
	case BWMONITOR_GREEN: //Green
	case BWMONITOR_AMBER: //Amber
		current = BIOS_Settings.bwmonitor; //Valid: use!
		break;
	default: //Invalid
		current = BWMONITOR_NONE; //Default: none!
		break;
	}
	if (BIOS_Settings.bwmonitor != current) //Invalid?
	{
		BIOS_Settings.bwmonitor = current; //Safety!
		BIOS_Changed = 1; //Changed!
	}
	int file = ExecuteList(10, 4, itemlist[current], 256,NULL,0); //Show options for the installed CPU!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = DEFAULT_BWMONITOR; //Default execution mode: None!

	case BWMONITOR_NONE: //None
	case BWMONITOR_WHITE: //Black/White
	case BWMONITOR_GREEN: //Greenscale
	case BWMONITOR_AMBER: //Amberscale
	default: //Changed?
		if (file != current) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			BIOS_Settings.bwmonitor = file; //Select Debug Mode!
		}
		break;
	}
	BIOS_Menu = 29; //Goto Video Settings menu!
}

void BIOS_BWMonitor_LuminanceMode()
{
	BIOS_Title("BW Monitor Luminance Mode");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Luminance Mode: "); //Show selection init!
	EMU_unlocktext();
	int i = 0; //Counter!
	numlist = 2; //Amount of Execution modes!
	for (i = 0; i<numlist; i++) //Process options!
	{
		cleardata(&itemlist[i][0], sizeof(itemlist[i])); //Reset!
	}

	safestrcpy(itemlist[BWMONITOR_LUMINANCEMODE_AVERAGED],sizeof(itemlist[0]), "Averaged"); //Set filename from options!
	safestrcpy(itemlist[BWMONITOR_LUMINANCEMODE_LUMINANCE],sizeof(itemlist[0]), "Luminance"); //Set filename from options!

	if (BIOS_Settings.bwmonitor_luminancemode>=numlist) //Invalid?
	{
		BIOS_Settings.bwmonitor_luminancemode = DEFAULT_BWMONITOR_LUMINANCEMODE; //Default!
		BIOS_Changed = 1; //We've changed!
	}

	int current = 0;
	switch (BIOS_Settings.bwmonitor_luminancemode) //What B/W monitor mode?
	{
	case BWMONITOR_LUMINANCEMODE_AVERAGED: //None
	case BWMONITOR_LUMINANCEMODE_LUMINANCE: //Black/White
		current = BIOS_Settings.bwmonitor_luminancemode; //Valid: use!
		break;
	default: //Invalid
		current = DEFAULT_BWMONITOR_LUMINANCEMODE; //Default: none!
		break;
	}
	if (BIOS_Settings.bwmonitor_luminancemode != current) //Invalid?
	{
		BIOS_Settings.bwmonitor_luminancemode = current; //Safety!
		BIOS_Changed = 1; //Changed!
	}
	int file = ExecuteList(16, 4, itemlist[current], 256,NULL,0); //Show options for the installed CPU!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = DEFAULT_BWMONITOR_LUMINANCEMODE; //Default execution mode: None!

	case BWMONITOR_LUMINANCEMODE_AVERAGED: //None
	case BWMONITOR_LUMINANCEMODE_LUMINANCE: //Black/White
	default: //Changed?
		if (file != current) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			BIOS_Settings.bwmonitor_luminancemode = file; //Select Debug Mode!
		}
		break;
	}
	BIOS_Menu = 29; //Goto Video Settings menu!
}

void BIOS_SVGA_DACMode()
{
	BIOS_Title("SVGA DAC Mode");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "SVGA DAC Mode: "); //Show selection init!
	EMU_unlocktext();
	int i = 0; //Counter!
	numlist = 4; //Amount of Execution modes!
	for (i = 0; i < numlist; i++) //Process options!
	{
		cleardata(&itemlist[i][0], sizeof(itemlist[i])); //Reset!
	}

	safestrcpy(itemlist[SVGA_DACMODE_SIERRA_SC11487], sizeof(itemlist[0]), "Sierra SC11487"); //Set filename from options!
	safestrcpy(itemlist[SVGA_DACMODE_UMC_UM70C178], sizeof(itemlist[0]), "UMC UM70C178"); //Set filename from options!
	safestrcpy(itemlist[SVGA_DACMODE_ATT_20C490], sizeof(itemlist[0]), "AT&T 20C490"); //Set filename from options!
	safestrcpy(itemlist[SVGA_DACMODE_SIERRA_SC15025], sizeof(itemlist[0]), "Sierra SC15025"); //Set filename from options!
	if (BIOS_Settings.SVGA_DACmode >= numlist) //Invalid?
	{
		BIOS_Settings.SVGA_DACmode = DEFAULT_SVGA_DACMODE; //Default!
		BIOS_Changed = 1; //We've changed!
	}

	int current = 0;
	switch (BIOS_Settings.SVGA_DACmode) //What B/W monitor mode?
	{
	case SVGA_DACMODE_SIERRA_SC11487: //Sierra SC11487
	case SVGA_DACMODE_UMC_UM70C178: //UMC UM70C178
	case SVGA_DACMODE_ATT_20C490: //AT&T 20C490
	case SVGA_DACMODE_SIERRA_SC15025: //Sierra SC15025
		current = BIOS_Settings.SVGA_DACmode; //Valid: use!
		break;
	default: //Invalid
		current = DEFAULT_SVGA_DACMODE; //Default: none!
		break;
	}
	if (BIOS_Settings.SVGA_DACmode != current) //Invalid?
	{
		BIOS_Settings.SVGA_DACmode = current; //Safety!
		BIOS_Changed = 1; //Changed!
	}
	int file = ExecuteList(15, 4, itemlist[current], 256, NULL, 0); //Show options for the installed CPU!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = DEFAULT_SVGA_DACMODE; //Default execution mode: None!

	case SVGA_DACMODE_SIERRA_SC11487: //Sierra SC11487
	case SVGA_DACMODE_UMC_UM70C178: //UMC UM70C178
	case SVGA_DACMODE_ATT_20C490: //AT&T 20C490
	case SVGA_DACMODE_SIERRA_SC15025: //Sierra SC15025
	default: //Changed?
		if (file != current) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			reboot_needed |= 1; //Reboot needed to apply!
			BIOS_Settings.SVGA_DACmode = file; //Select Debug Mode!
		}
		break;
	}
	BIOS_Menu = 29; //Goto Video Settings menu!
}

void BIOS_ET4000_extensions()
{
	BIOS_Title("ET4000 extensions");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "ET4000 extensions: "); //Show selection init!
	EMU_unlocktext();
	int i = 0; //Counter!
	numlist = 2; //Amount of Execution modes!
	for (i = 0; i < numlist; i++) //Process options!
	{
		cleardata(&itemlist[i][0], sizeof(itemlist[i])); //Reset!
	}

	safestrcpy(itemlist[ET4000_EXTENSIONS_ET4000AX], sizeof(itemlist[0]), "ET4000AX"); //Set filename from options!
	safestrcpy(itemlist[ET4000_EXTENSIONS_ET4000_W32], sizeof(itemlist[0]), "ET4000/W32"); //Set filename from options!
	if (BIOS_Settings.ET4000_extensions >= numlist) //Invalid?
	{
		BIOS_Settings.ET4000_extensions = DEFAULT_ET4000_EXTENSIONS; //Default!
		BIOS_Changed = 1; //We've changed!
	}

	int current = 0;
	switch (BIOS_Settings.ET4000_extensions) //What B/W monitor mode?
	{
	case ET4000_EXTENSIONS_ET4000AX: //ET4000AX
	case ET4000_EXTENSIONS_ET4000_W32: //ET4000/W32
		current = BIOS_Settings.ET4000_extensions; //Valid: use!
		break;
	default: //Invalid
		current = DEFAULT_ET4000_EXTENSIONS; //Default: none!
		break;
	}
	if (BIOS_Settings.ET4000_extensions != current) //Invalid?
	{
		BIOS_Settings.ET4000_extensions = current; //Safety!
		BIOS_Changed = 1; //Changed!
	}
	int file = ExecuteList(19, 4, itemlist[current], 256, NULL, 0); //Show options for the installed CPU!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = DEFAULT_ET4000_EXTENSIONS; //Default execution mode: None!

	case ET4000_EXTENSIONS_ET4000AX: //ET4000AX
	case ET4000_EXTENSIONS_ET4000_W32: //ET4000/W32
	default: //Changed?
		if (file != current) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			reboot_needed |= 1; //Reboot needed to apply!
			if ((VRAMtype[(BIOS_Settings.VGA_Mode&0xF)|((file&1)<<4)]!=VRAMtype[(BIOS_Settings.VGA_Mode&0xF)|((current&1)<<4)])) //Switching to a differently sized VRAM mode?
			{
				BIOS_Settings.VRAM_size = 0; //Autodetect current memory size!
			}
			BIOS_Settings.ET4000_extensions = file; //Select Debug Mode!
		}
		break;
	}
	BIOS_Menu = 29; //Goto Video Settings menu!
}

void BIOS_video_blackpedestal()
{
	BIOS_Title("Black pedestal");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Black pedestal: "); //Show selection init!
	EMU_unlocktext();
	int i = 0; //Counter!
	numlist = 2; //Amount of Execution modes!
	for (i = 0; i < numlist; i++) //Process options!
	{
		cleardata(&itemlist[i][0], sizeof(itemlist[i])); //Reset!
	}

	safestrcpy(itemlist[VIDEO_BLACKPEDESTAL_BLACK], sizeof(itemlist[0]), "Black"); //Set filename from options!
	safestrcpy(itemlist[VIDEO_BLACKPEDESTAL_75IRE], sizeof(itemlist[0]), "7.5 IRE"); //Set filename from options!
	if (BIOS_Settings.ET4000_extensions >= numlist) //Invalid?
	{
		BIOS_Settings.ET4000_extensions = DEFAULT_VIDEO_BLACKPEDESTAL; //Default!
		BIOS_Changed = 1; //We've changed!
	}

	int current = 0;
	switch (BIOS_Settings.ET4000_extensions) //What B/W monitor mode?
	{
	case VIDEO_BLACKPEDESTAL_BLACK: //Black
	case VIDEO_BLACKPEDESTAL_75IRE: //7.5 IRE
		current = BIOS_Settings.video_blackpedestal; //Valid: use!
		break;
	default: //Invalid
		current = DEFAULT_VIDEO_BLACKPEDESTAL; //Default: none!
		break;
	}
	if (BIOS_Settings.video_blackpedestal != current) //Invalid?
	{
		BIOS_Settings.video_blackpedestal = current; //Safety!
		BIOS_Changed = 1; //Changed!
	}
	int file = ExecuteList(16, 4, itemlist[current], 256, NULL, 0); //Show options for the installed CPU!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = DEFAULT_VIDEO_BLACKPEDESTAL; //Default execution mode: None!

	case VIDEO_BLACKPEDESTAL_BLACK: //Black
	case VIDEO_BLACKPEDESTAL_75IRE: //7.5 IRE
	default: //Changed?
		if (file != current) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			BIOS_Settings.video_blackpedestal = file; //Select black pedestal!
		}
		break;
	}
	BIOS_Menu = 29; //Goto Video Settings menu!
}

void BIOSMenu_LoadDefaults() //Load the defaults option!
{
	if (__HW_DISABLED) return; //Abort!
	int showchecksumerrors_backup = exec_showchecksumerrors; //Keep this!
	exec_showchecksumerrors = 0; //Don't show checksum errors!
	BIOS_LoadDefaults(0); //Load BIOS Defaults, don't save!
	exec_showchecksumerrors = showchecksumerrors_backup; //Restore!
	BIOS_Changed = 1; //Changed!
	BIOS_Menu = 0; //Goto Main menu!
}

void BIOS_InitInputText()
{
	advancedoptions = 0; //Init!
	int i;
	for (i = 0; i<11; i++) //Clear all possibilities!
	{
		cleardata(&menuoptions[i][0], sizeof(menuoptions[i])); //Init!
	}
	optioninfo[advancedoptions] = 0; //Gaming mode buttons!
	safestrcpy(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Map gaming mode buttons"); //Gaming mode buttons!
	optioninfo[advancedoptions] = 1; //Keyboard colors!
	safestrcpy(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Assign keyboard colors"); //Assign keyboard colors!

setJoysticktext: //For fixing it!
	optioninfo[advancedoptions] = 2; //Joystick!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Gaming mode: ");
	switch (BIOS_Settings.input_settings.gamingmode_joystick) //Joystick?
	{
	case 0:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Normal gaming mode mapped input");
		break;
	case 1:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Joystick, Cross=Button 1, Circle=Button 2");
		break;
	case 2:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Joystick, Cross=Button 2, Circle=Button 1");
		break;
	case 3:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Joystick, Gravis Gamepad");
		break;
	case 4:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Joystick, Gravis Analog Pro");
		break;
	case 5:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Joystick, Logitech WingMan Extreme Digital");
		break;
	default: //Error: fix it!
		BIOS_Settings.input_settings.gamingmode_joystick = 0; //Reset/Fix!
		BIOS_Changed = 1; //We've changed!
		goto setJoysticktext; //Goto!
		break;
	}

#ifndef SDL2
#if !defined(IS_PSP) && !defined(ANDROID) && !defined(IS_VITA) && !defined(IS_SWITCH)
	optioninfo[advancedoptions] = 3; //Reconnect joystick
	safestrcpy(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Detect joystick"); //Detect the new joystick!
#endif
#endif

	optioninfo[advancedoptions] = 4; //BIOS_DirectInput_remap_RCTRL_to_LWIN
	safestrcpy(menuoptions[advancedoptions], sizeof(menuoptions[0]), "Remap RCTRL to LWIN during Direct Input: "); //Remap RCTRL to LWIN during Direct Input!
	if (BIOS_Settings.input_settings.DirectInput_remap_RCTRL_to_LWIN)
	{
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Enabled");
	}
	else
	{
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Disabled");
	}

	optioninfo[advancedoptions] = 5; //BIOS_DirectInput_remap_accentgrave_to_tab
	safestrcpy(menuoptions[advancedoptions], sizeof(menuoptions[0]), "Remap Accent Grave to Tab during LALT: "); //Remap Accent Grave to Tab during LALT!
	if (BIOS_Settings.input_settings.DirectInput_remap_accentgrave_to_tab)
	{
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Enabled");
	}
	else
	{
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Disabled");
	}

	optioninfo[advancedoptions] = 6; //BIOS_DirectInput_remap_NUM0_to_Delete
	safestrcpy(menuoptions[advancedoptions], sizeof(menuoptions[0]), "Remap NUM0 to Delete: "); //Remap NUM0 to Delete!
	if (BIOS_Settings.input_settings.DirectInput_remap_NUM0_to_Delete)
	{
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Enabled");
	}
	else
	{
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Disabled");
	}

	optioninfo[advancedoptions] = 7; //BIOS_DirectInput_Disable_RALT
	safestrcpy(menuoptions[advancedoptions], sizeof(menuoptions[0]), "Disable RALT during Direct Input: "); //Disable RALT during Direct Input!
	if (BIOS_Settings.input_settings.DirectInput_Disable_RALT)
	{
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Enabled");
	}
	else
	{
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Disabled");
	}


	optioninfo[advancedoptions] = 8; //Analog minumum range
	safestrcpy(menuoptions[advancedoptions], sizeof(menuoptions[0]), "Analog minimum range: "); //Disable RALT during Direct Input!
	safescatnprintf(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "%u", (BIOS_Settings.input_settings.analog_minrange<<8)); //Disable RALT during Direct Input!
}

void BIOS_inputMenu() //Manage stuff concerning input.
{
	BIOS_Title("Input Settings Menu");
	BIOS_InitInputText(); //Init text!
	int menuresult = ExecuteMenu(advancedoptions, 4, BIOSMENU_SPEC_RETURN, &Menu_Stat); //Show the menu options!
	switch (menuresult)
	{
	case BIOSMENU_SPEC_CANCEL: //Return?
		BIOS_Menu = 8; //Goto Advanced Menu!
		break;
	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
	case 8: //Valid option?
		switch (optioninfo[menuresult]) //What option has been chosen, since we are dynamic size?
		{
		case 0: //Gaming mode buttons?
			BIOS_Menu = 90; //Map gaming mode face buttons Menu!
			break;
		case 1: //Keyboard colors?
			BIOS_Menu = 27; //Assign keyboard colors Menu!
			break;
		case 2:
			BIOS_Menu = 50; //Joystick option!
			break;
		case 3:
			BIOS_Menu = 51; //Joystick connect option!
			break;
		case 4:
			BIOS_Menu = 72; //Remap RCTRL to LWIN is #72!
			break;
		case 5:
			BIOS_Menu = 73; //Remap accent grave to tab during LALT is #73!
			break;
		case 6:
			BIOS_Menu = 77; //Remap NUM0 to Delete is #77!
			break;
		case 7:
			BIOS_Menu = 76; //Disable RALT during Direct Input is #76!
			break;
		case 8:
			BIOS_Menu = 93; //Analog Minimum Range is #93!
			break;
		default:
			break;
		}
		break;
	default: //Unknown option?
		BIOS_Menu = NOTIMPLEMENTED; //Not implemented yet!
		break;
	}
}

byte BIOS_gamingmodefacebuttonsselection = 0; //What face button is selected?
void BIOS_addInputText(char *s, byte inputnumber, uint_32 size)
{
	int input_key;
	byte shiftstatus;
	byte mousestatus;	

	char name[256]; //A little buffer for a name!
	if ((BIOS_Settings.input_settings.keyboard_gamemodemappings[BIOS_gamingmodefacebuttonsselection][inputnumber] != -1) || (BIOS_Settings.input_settings.keyboard_gamemodemappings_alt[BIOS_gamingmodefacebuttonsselection][inputnumber]) || (BIOS_Settings.input_settings.mouse_gamemodemappings[BIOS_gamingmodefacebuttonsselection][inputnumber])) //Got anything?
	{
		shiftstatus = BIOS_Settings.input_settings.keyboard_gamemodemappings_alt[BIOS_gamingmodefacebuttonsselection][inputnumber]; //Load shift status!
		input_key = BIOS_Settings.input_settings.keyboard_gamemodemappings[BIOS_gamingmodefacebuttonsselection][inputnumber]; //Load shift status!
		mousestatus = BIOS_Settings.input_settings.mouse_gamemodemappings[BIOS_gamingmodefacebuttonsselection][inputnumber]; //Load mouse status!
		if (shiftstatus) //Gotten alt status?
		{
			if (shiftstatus&SHIFTSTATUS_CTRL)
			{
				safestrcat(s,size, "Ctrl");
				if ((shiftstatus&SHIFTSTATUS_CTRL)!=shiftstatus) //More?
				{
					safestrcat(s,size, "-"); //Seperator!
				}
			}
			if (shiftstatus&SHIFTSTATUS_ALT)
			{
				safestrcat(s,size, "Alt");
				if ((shiftstatus&(SHIFTSTATUS_CTRL | SHIFTSTATUS_ALT)) != shiftstatus) //More?
				{
					safestrcat(s,size, "-"); //Seperator!
				}
			}
			if (shiftstatus&SHIFTSTATUS_SHIFT)
			{
				safestrcat(s,size, "Shift");
			}
			if ((input_key != -1) || mousestatus) //Gotten a key/mouse?
			{
				safestrcat(s,size, "-"); //Seperator!
			}
		}
		if (input_key != -1) //Gotten a key?
		{
			memset(&name, 0, sizeof(name)); //Init name!
			if (EMU_keyboard_handler_idtoname(input_key, &name[0]))
			{
				safestrcat(s,size, name); //Add the name of the key!
			}
			else
			{
				safestrcat(s,size, "<Unidentified key>");
			}
			if (mousestatus)
			{
				safestrcat(s,size, "-"); //Seperator!
			}
		}
		if (mousestatus) //Gotten a mouse input?
		{
			if (mousestatus&1) //Left button?
			{
				safestrcat(s,size,"Mouse left");
				if ((mousestatus&1)!=mousestatus) //More buttons?
				{
					safestrcat(s,size,"-"); //Seperator!
				}
			}
			if (mousestatus&2) //Right button?
			{
				safestrcat(s,size,"Mouse right");
				if ((mousestatus&3)!=mousestatus) //More buttons?
				{
					safestrcat(s,size,"-");
				}
			}
			if (mousestatus&4) //Middle button?
			{
				safestrcat(s,size,"Mouse middle");
			}
		}
	}
	else
	{
		safestrcat(s,size, "<Unassigned>"); //Not assigned!
	}
}

void BIOS_InitGamingModeButtonsText()
{
	advancedoptions = 0; //Init!
	int i;
	for (i = 0; i<16; i++) //Set all possibilities!
	{
		cleardata(&menuoptions[i][0], sizeof(menuoptions[i])); //Init!
		optioninfo[advancedoptions] = i; //The key!
		switch (i) //What key?
		{
			case GAMEMODE_START:
				safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Start:        "); //Gaming mode buttons!
				BIOS_addInputText(&menuoptions[advancedoptions++][0], i,sizeof(menuoptions[0]));
				break;
			case GAMEMODE_LEFT:
				safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Left:         "); //Assign keyboard colors!
				BIOS_addInputText(&menuoptions[advancedoptions++][0], i,sizeof(menuoptions[0]));
				break;
			case GAMEMODE_UP:
				safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Up:           "); //Assign keyboard colors!
				BIOS_addInputText(&menuoptions[advancedoptions++][0], i,sizeof(menuoptions[0]));
				break;
			case GAMEMODE_RIGHT:
				safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Right:        "); //Assign keyboard colors!
				BIOS_addInputText(&menuoptions[advancedoptions++][0], i,sizeof(menuoptions[0]));
				break;
			case GAMEMODE_DOWN:
				safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Down:         "); //Assign keyboard colors!
				BIOS_addInputText(&menuoptions[advancedoptions++][0], i,sizeof(menuoptions[0]));
				break;
			case GAMEMODE_LTRIGGER:
				safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "L:            "); //Assign keyboard colors!
				BIOS_addInputText(&menuoptions[advancedoptions++][0], i,sizeof(menuoptions[0]));
				break;
			case GAMEMODE_RTRIGGER:
				safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "R:            "); //Assign keyboard colors!
				BIOS_addInputText(&menuoptions[advancedoptions++][0], i,sizeof(menuoptions[0]));
				break;
			case GAMEMODE_TRIANGLE:
				safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Triangle:     "); //Assign keyboard colors!
				BIOS_addInputText(&menuoptions[advancedoptions++][0], i,sizeof(menuoptions[0]));
				break;
			case GAMEMODE_CIRCLE:
				safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Circle:       "); //Assign keyboard colors!
				BIOS_addInputText(&menuoptions[advancedoptions++][0], i,sizeof(menuoptions[0]));
				break;
			case GAMEMODE_CROSS:
				safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Cross:        "); //Assign keyboard colors!
				BIOS_addInputText(&menuoptions[advancedoptions++][0], i,sizeof(menuoptions[0]));
				break;
			case GAMEMODE_SQUARE:
				safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Square:       "); //Assign keyboard colors!
				BIOS_addInputText(&menuoptions[advancedoptions++][0], i,sizeof(menuoptions[0]));
				break;
			case GAMEMODE_ANALOGLEFT:
				safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Analog left:  "); //Assign keyboard colors!
				BIOS_addInputText(&menuoptions[advancedoptions++][0], i,sizeof(menuoptions[0]));
				break;
			case GAMEMODE_ANALOGUP:
				safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Analog up:    "); //Assign keyboard colors!
				BIOS_addInputText(&menuoptions[advancedoptions++][0], i,sizeof(menuoptions[0]));
				break;
			case GAMEMODE_ANALOGRIGHT:
				safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Analog right: "); //Assign keyboard colors!
				BIOS_addInputText(&menuoptions[advancedoptions++][0], i,sizeof(menuoptions[0]));
				break;
			case GAMEMODE_ANALOGDOWN:
				safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Analog down:  "); //Assign keyboard colors!
				BIOS_addInputText(&menuoptions[advancedoptions++][0], i,sizeof(menuoptions[0]));
				break;
			case 15: //Joystick enable?
				safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Joystick:     "); //Assign keyboard colors!
				setGamingModeJoysticktext:
				switch (BIOS_Settings.input_settings.usegamingmode_joystick[BIOS_gamingmodefacebuttonsselection])
				{
				case 0: //Disabled?
					safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Normal gaming mode mapped input");
					break;
				case 1: //Enabled?
					safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Enable joystick input");
					break;
				default: //Error: fix it!
					BIOS_Settings.input_settings.usegamingmode_joystick[BIOS_gamingmodefacebuttonsselection] = 0; //Reset/Fix!
					BIOS_Changed = 1; //We've changed!
					goto setGamingModeJoysticktext; //Goto!
					break;
				}
				break;
			default: //Unknown? Don't handle unknown cases!
				break;
		}
	}
}

extern char gamingmodebuttonsfacebuttonname[5][256]; //Face button names!

void BIOS_gamingModeButtonsMenu() //Manage stuff concerning input.
{
	char title[256];
	memset(&title, 0, sizeof(title)); //Init title!
	safescatnprintf(title,sizeof(title), "Map gaming mode buttons (%s)", gamingmodebuttonsfacebuttonname[BIOS_gamingmodefacebuttonsselection]);
	BIOS_Title(&title[0]);
	BIOS_InitGamingModeButtonsText(); //Init text!
	int menuresult = ExecuteMenu(advancedoptions, 4, BIOSMENU_SPEC_RETURN|BIOSMENU_SPEC_SQUAREOPTION, &Menu_Stat); //Show the menu options!
	switch (menuresult)
	{
	case BIOSMENU_SPEC_CANCEL: //Return?
		BIOS_Menu = 90; //Goto Gaming Mode Face Buttons Menu!
		break;
	case GAMEMODE_START:
	case GAMEMODE_LEFT:
	case GAMEMODE_UP:
	case GAMEMODE_RIGHT:
	case GAMEMODE_DOWN:
	case GAMEMODE_LTRIGGER:
	case GAMEMODE_RTRIGGER:
	case GAMEMODE_TRIANGLE:
	case GAMEMODE_CIRCLE:
	case GAMEMODE_CROSS:
	case GAMEMODE_SQUARE:
	case GAMEMODE_ANALOGLEFT:
	case GAMEMODE_ANALOGUP:
	case GAMEMODE_ANALOGRIGHT:
	case GAMEMODE_ANALOGDOWN:
	case 15:
		if (Menu_Stat == BIOSMENU_STAT_SQUARE) //Square pressed on an item?
		{
			if (optioninfo[menuresult] != 15) //A normal button?
			{
				BIOS_Changed |= ((BIOS_Settings.input_settings.keyboard_gamemodemappings[BIOS_gamingmodefacebuttonsselection][menuresult] != -1) || (BIOS_Settings.input_settings.keyboard_gamemodemappings_alt[BIOS_gamingmodefacebuttonsselection][menuresult]) || (BIOS_Settings.input_settings.mouse_gamemodemappings[BIOS_gamingmodefacebuttonsselection][menuresult])); //Did we change?
				BIOS_Settings.input_settings.keyboard_gamemodemappings[BIOS_gamingmodefacebuttonsselection][menuresult] = -1; //Set the new key!
				BIOS_Settings.input_settings.keyboard_gamemodemappings_alt[BIOS_gamingmodefacebuttonsselection][menuresult] = 0; //Set the shift status!
				BIOS_Settings.input_settings.mouse_gamemodemappings[BIOS_gamingmodefacebuttonsselection][menuresult] = 0; //Set the mouse status!
			}
			else //Joystick enable?
			{
				BIOS_Changed |= BIOS_Settings.input_settings.usegamingmode_joystick[BIOS_gamingmodefacebuttonsselection] = (BIOS_gamingmodefacebuttonsselection?0:1); //Did we change?
				BIOS_Settings.input_settings.usegamingmode_joystick[BIOS_gamingmodefacebuttonsselection] = (BIOS_gamingmodefacebuttonsselection?0:1); //Set the joystick status!
			}
		}
		else //Normal option selected?
		{
			//Valid option?
			if (optioninfo[menuresult] != 15) //A normal button?
			{
				delay(100000); //Wait a bit!
				enableKeyboard(1); //Buffer input!
				TicksHolder ticks;
				initTicksHolder(&ticks); //Initialise!
				getnspassed(&ticks); //Initialise counter!
				for (;;)
				{
					updateKeyboard(getnspassed(&ticks)); //Update the OSK keyboard!
					lock(LOCK_INPUT);
					if ((input_buffer != -1) || (input_buffer_shift) || (input_buffer_mouse)) //Given input yet?
					{
						BIOS_Changed |= ((BIOS_Settings.input_settings.keyboard_gamemodemappings[BIOS_gamingmodefacebuttonsselection][menuresult] != input_buffer) || (BIOS_Settings.input_settings.keyboard_gamemodemappings_alt[BIOS_gamingmodefacebuttonsselection][menuresult] != input_buffer_shift) || (BIOS_Settings.input_settings.mouse_gamemodemappings[BIOS_gamingmodefacebuttonsselection][menuresult] != input_buffer_mouse)); //Did we change?
						BIOS_Settings.input_settings.keyboard_gamemodemappings[BIOS_gamingmodefacebuttonsselection][menuresult] = input_buffer; //Set the new key!
						BIOS_Settings.input_settings.keyboard_gamemodemappings_alt[BIOS_gamingmodefacebuttonsselection][menuresult] = input_buffer_shift; //Set the shift status!
						BIOS_Settings.input_settings.mouse_gamemodemappings[BIOS_gamingmodefacebuttonsselection][menuresult] = input_buffer_mouse; //Set the shift status!
						unlock(LOCK_INPUT); //We're done with input: release our lock!
						disableKeyboard(); //Disable the keyboard!
						break; //Break out of the loop: we're done!
					}
					unlock(LOCK_INPUT);
					delay(0); //Wait for the key input!
				}
				//Keep in our own menu: we're not changing after a choise has been made, but simply allowing to select another button!
			}
			else //Joystick enable?
			{
				BIOS_Menu = 91; //Joystick enable option!
			}
		}
		break;
	default: //Unknown option?
		BIOS_Menu = NOTIMPLEMENTED; //Not implemented yet!
		break;
	}
}

void BIOS_gamingModeButtonsJoystickEnable()
{
	BIOS_Changed |= 1; //Did we change?
	BIOS_Settings.input_settings.usegamingmode_joystick[BIOS_gamingmodefacebuttonsselection] = !BIOS_Settings.input_settings.usegamingmode_joystick[BIOS_gamingmodefacebuttonsselection]; //Set the joystick status!
	BIOS_Menu = 26; //Map gaming mode buttons Menu!
}

void BIOS_InitGamingModeButtonsFaceButtonText()
{
	advancedoptions = 0; //Init!
	int i;
	byte mapping[5] = { 0,2,4,3,1 }; //Translates below order to: (0)down, (1)square, (2)triangle, (3)cross, (4)circle
	for (i = 0; i < 5; i++) //Set all possibilities!
	{
		cleardata(&menuoptions[advancedoptions][0], sizeof(menuoptions[i])); //Init!
		optioninfo[advancedoptions] = mapping[i]; //The face button it's mapped to!
		switch (i) //What face button?
		{
		case 0:
			safestrcpy(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Down"); //Gaming mode buttons!
			break;
		case 1:
			safestrcpy(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Triangle"); //Gaming mode buttons!
			break;
		case 2:
			safestrcpy(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Circle"); //Gaming mode buttons!
			break;
		case 3:
			safestrcpy(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Cross"); //Gaming mode buttons!
			break;
		case 4:
			safestrcpy(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Square"); //Gaming mode buttons!
			break;
		default: //Unknown? Don't handle unknown cases!
			break;
		}
	}
}

void BIOS_gamingModeButtonsFaceButtonMenu() //Manage stuff concerning input.
{
	byte button;
	BIOS_Title("Map gaming mode buttons (Face Button)");
	BIOS_InitGamingModeButtonsFaceButtonText(); //Initialize our text!
	int menuresult = ExecuteMenu(advancedoptions, 4, BIOSMENU_SPEC_RETURN | BIOSMENU_SPEC_SQUAREOPTION, &Menu_Stat); //Show the menu options!
	switch (menuresult)
	{
	case BIOSMENU_SPEC_CANCEL: //Return?
		BIOS_Menu = 25; //Goto Input Menu!
		break;
	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
		BIOS_gamingmodefacebuttonsselection = (byte)optioninfo[menuresult]; //The selected face button to change!
		if (Menu_Stat == BIOSMENU_STAT_SQUARE) //Square pressed on an item?
		{
			for (button = 0; button < NUMITEMS(BIOS_Settings.input_settings.keyboard_gamemodemappings[BIOS_gamingmodefacebuttonsselection]); ++button)
			{
				BIOS_Changed |= ((BIOS_Settings.input_settings.keyboard_gamemodemappings[BIOS_gamingmodefacebuttonsselection][button] != -1) || (BIOS_Settings.input_settings.keyboard_gamemodemappings_alt[BIOS_gamingmodefacebuttonsselection][button]) || (BIOS_Settings.input_settings.mouse_gamemodemappings[BIOS_gamingmodefacebuttonsselection][button])); //Did we change?
				BIOS_Settings.input_settings.keyboard_gamemodemappings[BIOS_gamingmodefacebuttonsselection][button] = -1; //Set the new key!
				BIOS_Settings.input_settings.keyboard_gamemodemappings_alt[BIOS_gamingmodefacebuttonsselection][button] = 0; //Set the shift status!
				BIOS_Settings.input_settings.mouse_gamemodemappings[BIOS_gamingmodefacebuttonsselection][button] = 0; //Set the mouse status!
			}
			BIOS_Changed |= (BIOS_Settings.input_settings.usegamingmode_joystick[BIOS_gamingmodefacebuttonsselection]!=(BIOS_gamingmodefacebuttonsselection?0:1)); //Did we change?
			BIOS_Settings.input_settings.usegamingmode_joystick[BIOS_gamingmodefacebuttonsselection] = (BIOS_gamingmodefacebuttonsselection?0:1); //Joystick enable!
		}
		else //Normal option selected?
		{
			//Change to the face button menu!
			BIOS_Menu = 26; //Map gaming mode buttons Menu!
		}
		break;
	default: //Unknown option?
		BIOS_Menu = NOTIMPLEMENTED; //Not implemented yet!
		break;
	}
}

char colors[0x10][15] = { "Black", "Blue", "Green", "Cyan", "Red", "Magenta", "Brown", "Light gray", "Dark gray", "Bright blue", "Bright green", "Bright cyan", "Bright red", "Bright magenta", "Yellow", "White" }; //Set color from options!
void BIOS_addColorText(char *s, byte color, uint_32 size)
{
	if (color < 0x10) //Valid color?
	{
		safestrcat(s,size, colors[color]); //Take the color!
	}
	else
	{
		safestrcat(s,size, "<UNKNOWN. CHECK SETTINGS VERSION>"); //Set color from options!
	}
}

void BIOS_InitKeyboardColorsText()
{
	advancedoptions = 0; //Init!
	int i;
	for (i = 0; i<5; i++) //Clear all possibilities!
	{
		cleardata(&menuoptions[i][0], sizeof(menuoptions[i])); //Init!
	}
	optioninfo[advancedoptions] = 0; //Gaming mode buttons!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Text font color: "); //Gaming mode buttons!
	BIOS_addColorText(&menuoptions[advancedoptions++][0], BIOS_Settings.input_settings.colors[0],sizeof(menuoptions[0])); //First color!
	optioninfo[advancedoptions] = 1; //Keyboard colors!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Text border color: "); //Assign keyboard colors!
	BIOS_addColorText(&menuoptions[advancedoptions++][0], BIOS_Settings.input_settings.colors[1],sizeof(menuoptions[0])); //First color!
	optioninfo[advancedoptions] = 2; //Keyboard colors!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Text active border color: "); //Assign keyboard colors!
	BIOS_addColorText(&menuoptions[advancedoptions++][0], BIOS_Settings.input_settings.colors[2],sizeof(menuoptions[0])); //First color!
	optioninfo[advancedoptions] = 3; //Gaming mode buttons!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "LED Font color: "); //Gaming mode buttons!
	BIOS_addColorText(&menuoptions[advancedoptions++][0], BIOS_Settings.input_settings.colors[3],sizeof(menuoptions[0])); //First color!
	optioninfo[advancedoptions] = 4; //Keyboard colors!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "LED border color: "); //Assign keyboard colors!
	BIOS_addColorText(&menuoptions[advancedoptions++][0], BIOS_Settings.input_settings.colors[4],sizeof(menuoptions[0])); //First color!
	optioninfo[advancedoptions] = 5; //Keyboard colors!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "LED active border color: "); //Assign keyboard colors!
	BIOS_addColorText(&menuoptions[advancedoptions++][0], BIOS_Settings.input_settings.colors[5],sizeof(menuoptions[0])); //First color!
}

byte gamingKeyboardColor = 0;

void BIOS_gamingKeyboardColor() //Select a gaming keyboard color!
{
	switch (gamingKeyboardColor) //What option?
	{
	case 0:
		BIOS_Title("Text font color");
		break;
	case 1:
		BIOS_Title("Text border color");
		break;
	case 2:
		BIOS_Title("Text active border color");
		break;
	case 3:
		BIOS_Title("LED font color");
		break;
	case 4:
		BIOS_Title("LED border color");
		break;
	case 5:
		BIOS_Title("LED active border color");
		break;
	default:
		break;
	}
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Color: "); //Show selection init!
	EMU_unlocktext();
	int i = 0; //Counter!
	numlist = 16; //Amount of colors!
	for (i = 0; i<numlist; i++) //Process options!
	{
		cleardata(&itemlist[i][0], sizeof(itemlist[i])); //Reset!
		safestrcpy(itemlist[i],sizeof(itemlist[0]), &colors[i][0]); //Set the color to use!
	}

	int current = 0;
	switch (BIOS_Settings.input_settings.colors[gamingKeyboardColor]) //What color?
	{
	case 0:case 1:case 2:case 3:case 4:case 5:case 6:case 7:case 8:case 9:case 0xA:case 0xB:case 0xC:case 0xD:case 0xE:case 0xF:
		current = BIOS_Settings.input_settings.colors[gamingKeyboardColor]; //Valid: use!
		break;
	default: //Invalid
		keyboard_loadDefaultColor(gamingKeyboardColor); //Default: none!
		current = BIOS_Settings.input_settings.colors[gamingKeyboardColor]; //Valid: use!
		break;
	}
	if (BIOS_Settings.input_settings.colors[gamingKeyboardColor] != current) //Invalid?
	{
		BIOS_Settings.input_settings.colors[gamingKeyboardColor] = current; //Safety!
		BIOS_Changed = 1; //Changed!
	}
	int file = ExecuteList(7, 4, itemlist[current], 256,NULL,0); //Show options for the installed CPU!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		keyboard_loadDefaultColor(gamingKeyboardColor); //Load the default value!
		file = BIOS_Settings.input_settings.colors[gamingKeyboardColor]; //Load the default value!

	default: //Changed?
		if (file != current) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			BIOS_Settings.input_settings.colors[gamingKeyboardColor] = file; //Select Debug Mode!
		}
		break;
	}
	BIOS_Menu = 27; //Goto Colors menu!
}

void BIOS_gamingKeyboardColorsMenu() //Manage stuff concerning input.
{
	BIOS_Title("Assign keyboard colors");
	BIOS_InitKeyboardColorsText(); //Init text!
	int menuresult = ExecuteMenu(advancedoptions, 4, BIOSMENU_SPEC_RETURN, &Menu_Stat); //Show the menu options!
	switch (menuresult)
	{
	case BIOSMENU_SPEC_CANCEL: //Return?
		BIOS_Menu = 25; //Goto Input Menu!
		break;
	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
	case 5: //Valid option?
		gamingKeyboardColor = optioninfo[menuresult]; //What option has been chosen, since we are dynamic size?
		BIOS_Menu = 28; //Switch to our option!
		break;
	default: //Unknown option?
		BIOS_Menu = NOTIMPLEMENTED; //Not implemented yet!
		break;
	}
}

void BIOS_VGAModeSetting()
{
	BIOS_Title("VGA Mode");
	EMU_locktext();
	EMU_gotoxy(0,4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0,4,"VGA Mode: "); //Show selection init!
	EMU_unlocktext();
	int i = 0; //Counter!
	numlist = 9; //Amount of VGA modes!
	for (i=0; i<numlist; i++) //Process options!
	{
		cleardata(&itemlist[i][0],sizeof(itemlist[i])); //Reset!
	}
	safestrcpy(itemlist[0],sizeof(itemlist[0]),"Pure VGA"); //Set filename from options!
	safestrcpy(itemlist[1],sizeof(itemlist[0]),"VGA with NMI"); //Set filename from options!
	safestrcpy(itemlist[2],sizeof(itemlist[0]),"VGA with CGA"); //Special CGA compatibility mode!
	safestrcpy(itemlist[3],sizeof(itemlist[0]),"VGA with MDA"); //Special MDA compatibility mode!
	safestrcpy(itemlist[4],sizeof(itemlist[0]),"Pure CGA"); //Special CGA pure mode!
	safestrcpy(itemlist[5],sizeof(itemlist[0]),"Pure MDA"); //Special MDA pure mode!
	safestrcpy(itemlist[6],sizeof(itemlist[0]),"Tseng ET4000"); //Tseng ET4000 card!
	safestrcpy(itemlist[7],sizeof(itemlist[0]),"Tseng ET3000"); //Tseng ET4000 card!
	safestrcpy(itemlist[8],sizeof(itemlist[0]),"Pure EGA"); //EGA card!

	int current = 0;
	switch (BIOS_Settings.VGA_Mode) //What setting?
	{
	case 0: //Valid
	case 1: //Valid
	case 2: //Valid
	case 3: //Valid
	case 4: //Valid
	case 5: //Valid
	case 6: //Valid
	case 7: //Valid
	case 8: //Valid
		current = BIOS_Settings.VGA_Mode; //Valid: use!
		break;
	default: //Invalid
		current = 0; //Default: none!
		break;
	}
	if (BIOS_Settings.VGA_Mode!=current) //Invalid?
	{
		BIOS_Settings.VGA_Mode = current; //Safety!
		BIOS_Changed = 1; //Changed!
	}
	int file = ExecuteList(10,4,itemlist[current],256,NULL,0); //Show options for the installed CPU!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = DEFAULT_VIDEOCARD; //Default setting: Disabled!

	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
	default: //Changed?
		if (file!=current) //Not current?
		{
			if (VRAMtype[((file&0xF)|((BIOS_Settings.ET4000_extensions&1)<<4))]!=VRAMtype[((current&0xF)|((BIOS_Settings.ET4000_extensions&1)<<4))]) //Switching to a differently sized VRAM mode?
			{
				BIOS_Settings.VRAM_size = 0; //Autodetect current memory size!
			}
			BIOS_Changed = 1; //Changed!
			reboot_needed |= 1; //Reboot needed to apply!
			BIOS_Settings.VGA_Mode = file; //Select VGA Mode setting!
		}
		break;
	}
	BIOS_Menu = 29; //Goto Video Settings menu!
}

void BIOS_InitVideoSettingsText()
{
	advancedoptions = 0; //Init!
	int i;
	for (i=0; i<11; i++) //Clear all possibilities!
	{
		cleardata(&menuoptions[i][0],sizeof(menuoptions[i])); //Init!
	}

	optioninfo[advancedoptions] = 0; //We're direct plot setting!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]),"VGA Direct Plot: ");
setdirectplottext: //For fixing it!
	switch (BIOS_Settings.GPU_AllowDirectPlot) //What direct plot setting?
	{
	case 2: //Forced?
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]),"Forced");
		break;
	case 1: //Yes?
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]),"Automatic");
		break;
	case 0: //No?
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]),"Disabled");
		break;
	default: //Error: fix it!
		BIOS_Settings.GPU_AllowDirectPlot = 0; //Reset/Fix!
		BIOS_Changed = 1; //We've changed!
		goto setdirectplottext; //Goto!
		break;
	}

setaspectratiotext:
	optioninfo[advancedoptions] = 4; //Keep aspect ratio!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Aspect ratio: ");
	switch (BIOS_Settings.aspectratio) //Keep aspect ratio?
	{
	case 0:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Fullscreen stretching");
		break;
	case 1:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Keep the same");
		break;
	case 2:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Force 4:3(VGA)");
		break;
	case 3:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Force CGA");
		break;
	case 4:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Force 4:3(SVGA 768p)");
		break;
	case 5:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Force 4:3(SVGA 1080p)");
		break;
	case 6:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Force 4K");
		break;
	case 7:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Force 4:3(SVGA 4K)");
		break;
	default:
		BIOS_Settings.aspectratio = 0; //Reset/Fix!
		BIOS_Changed = 1; //We've changed!
		goto setaspectratiotext;
		break;
	}

setmonitortext: //For fixing it!
	optioninfo[advancedoptions] = 1; //Monitor!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Monitor: ");
	switch (BIOS_Settings.bwmonitor) //B/W monitor?
 	{
	case BWMONITOR_NONE:
 		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Color monitor");
 		break;
	case BWMONITOR_WHITE:
 		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "B/W monitor: white");
 		break;
	case BWMONITOR_GREEN:
 		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "B/W monitor: green");
 		break;
	case BWMONITOR_AMBER:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "B/W monitor: amber");
		break;
	default: //Error: fix it!
		BIOS_Settings.bwmonitor = 0; //Reset/Fix!
		BIOS_Changed = 1; //We've changed!
		goto setmonitortext; //Goto!
		break;
	}

setmonitormodetext: //For fixing it!
	optioninfo[advancedoptions] = 8; //BW Monitor luminance mode!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "BW Monitor luminance mode: ");
	switch (BIOS_Settings.bwmonitor_luminancemode) //B/W monitor?
	{
	case BWMONITOR_LUMINANCEMODE_AVERAGED:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Averaged");
		break;
	case BWMONITOR_LUMINANCEMODE_LUMINANCE:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Luminance");
		break;
	default: //Error: fix it!
		BIOS_Settings.bwmonitor_luminancemode = DEFAULT_BWMONITOR_LUMINANCEMODE; //Reset/Fix!
		BIOS_Changed = 1; //We've changed!
		goto setmonitormodetext; //Goto!
		break;
	}

setDACmodetext: //For fixing it!
	optioninfo[advancedoptions] = 9; //DAC mode!
	safestrcpy(menuoptions[advancedoptions], sizeof(menuoptions[0]), "SVGA DAC mode: ");
	switch (BIOS_Settings.SVGA_DACmode) //DAC mode?
	{
	case SVGA_DACMODE_SIERRA_SC11487:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Sierra SC11487");
		break;
	case SVGA_DACMODE_UMC_UM70C178:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "UMC UM70C178");
		break;
	case SVGA_DACMODE_ATT_20C490:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "AT&T 20C490");
		break;
	case SVGA_DACMODE_SIERRA_SC15025:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Sierra SC15025");
		break;
	default: //Error: fix it!
		BIOS_Settings.SVGA_DACmode = DEFAULT_SVGA_DACMODE; //Reset/Fix!
		BIOS_Changed = 1; //We've changed!
		goto setDACmodetext; //Goto!
		break;
	}

setET4000extensionstext: //For fixing it!
	optioninfo[advancedoptions] = 10; //ET4000 extensions!
	safestrcpy(menuoptions[advancedoptions], sizeof(menuoptions[0]), "ET4000 extensions: ");
	switch (BIOS_Settings.ET4000_extensions) //ET4000 extensions?
	{
	case ET4000_EXTENSIONS_ET4000AX:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "ET4000AX");
		break;
	case ET4000_EXTENSIONS_ET4000_W32:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "ET4000/W32");
		break;
	default: //Error: fix it!
		BIOS_Settings.ET4000_extensions = DEFAULT_ET4000_EXTENSIONS; //Reset/Fix!
		BIOS_Changed = 1; //We've changed!
		goto setET4000extensionstext; //Goto!
		break;
	}

setBlackPedestaltext: //For fixing it!
	optioninfo[advancedoptions] = 11; //Black pedestal!
	safestrcpy(menuoptions[advancedoptions], sizeof(menuoptions[0]), "Black pedestal: ");
	switch (BIOS_Settings.video_blackpedestal) //Black pedestal?
	{
	case VIDEO_BLACKPEDESTAL_BLACK:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Black");
		break;
	case VIDEO_BLACKPEDESTAL_75IRE:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "7.5 IRE");
		break;
	default: //Error: fix it!
		BIOS_Settings.video_blackpedestal = DEFAULT_VIDEO_BLACKPEDESTAL; //Reset/Fix!
		BIOS_Changed = 1; //We've changed!
		goto setBlackPedestaltext; //Goto!
		break;
	}

setVGAModetext: //For fixing it!
	optioninfo[advancedoptions] = 2; //VGA Mode!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "VGA Mode: ");
	switch (BIOS_Settings.VGA_Mode) //VGA Mode?
	{
	case 0:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Pure VGA");
		break;
	case 1:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "VGA with NMI");
		break;
	case 2:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "VGA with CGA");
		break;
	case 3:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "VGA with MDA");
		break;
	case 4:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Pure CGA");
		break;
	case 5:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Pure MDA");
		break;
	case 6:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Tseng ET4000"); //Tseng ET4000 SVGA card!
		break;
	case 7:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Tseng ET3000"); //Tseng ET3000 SVGA card!
		break;
	case 8:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Pure EGA"); //EGA card!
		break;
	default: //Error: fix it!
		BIOS_Settings.VGA_Mode = DEFAULT_VIDEOCARD; //Reset/Fix!
		BIOS_Changed = 1; //We've changed!
		goto setVGAModetext; //Goto!
		break;
	}

setCGAModeltext: //For fixing it!
	optioninfo[advancedoptions] = 3; //CGA Model!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "CGA Model: ");
	switch (BIOS_Settings.CGAModel) //CGA Model?
	{
	case 0:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Old-style RGB");
		break;
	case 1:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Old-style NTSC");
		break;
	case 2:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "New-style RGB");
		break;
	case 3:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "New-style NTSC");
		break;
	default: //Error: fix it!
		BIOS_Settings.CGAModel = DEFAULT_CGAMODEL; //Reset/Fix!
		BIOS_Changed = 1; //We've changed!
		goto setCGAModeltext; //Goto!
		break;
	}

	optioninfo[advancedoptions] = 5; //Show framerate!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Show framerate: ");
	if (BIOS_Settings.ShowFramerate)
	{
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Enabled");
	}
	else
	{
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Disabled");
	}

	optioninfo[advancedoptions] = 6; //VGA Synchronization!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "VGA Synchronization: ");
	switch (BIOS_Settings.VGASynchronization)
	{
		default: //Unknown?
		case 0: //Old synchronization method?
			safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Old synchronization depending on host");
			break;
		case 1: //Synchronize depending on the Host?
			safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Synchronize depending on host");
			break;
		case 2: //Full CPU synchronization?
			safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Full CPU synchronization");
			break;
	}

	optioninfo[advancedoptions] = 7; //Dump VGA!
	safestrcpy(menuoptions[advancedoptions++],sizeof(menuoptions[0]),"Dump VGA");
}

void BIOS_VideoSettingsMenu() //Manage stuff concerning input.
{
	BIOS_Title("Video Settings Menu");
	BIOS_InitVideoSettingsText(); //Init text!
	int menuresult = ExecuteMenu(advancedoptions, 4, BIOSMENU_SPEC_RETURN, &Menu_Stat); //Show the menu options!
	switch (menuresult)
	{
	case BIOSMENU_SPEC_CANCEL: //Return?
		BIOS_Menu = 8; //Goto Advanced Menu!
		break;
	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
	case 9:
	case 10:
	case 11: //Valid option?
		switch (optioninfo[menuresult]) //What option has been chosen, since we are dynamic size?
		{
		case 0: //Direct plot setting?
			BIOS_Menu = 15; //Direct plot setting!
			break;
		case 1: //Monitor?
			BIOS_Menu = 22; //Monitor setting!
			break;
		case 2: //VGA Mode?
			if (!EMU_RUNNING) BIOS_Menu = 30; //VGA Mode setting!
			break;
		case 3: //CGA Model
			BIOS_Menu = 49; //CGA Model!
			break;
		case 4: //Aspect ratio setting!
			BIOS_Menu = 17; //Aspect ratio setting!
			break;
		case 5: //Show framerate setting!
			BIOS_Menu = 39; //Show framerate setting!
			break;
		case 6: //VGA Synchronization setting!
			if (!EMU_RUNNING) BIOS_Menu = 47; //VGA Synchronization setting!
			break;
		case 7: //Dump VGA?
			BIOS_Menu = 48; //Dump VGA!
			break;
		case 8: //BW monitor luminance mode
			BIOS_Menu = 86; //BW monitor luminance mode!
			break;
		case 9: //SVGA DAC mode
			if (!EMU_RUNNING) BIOS_Menu = 87; //SVGA DAC mode!
			break;
		case 10: //ET4000 extensions?
			if (!EMU_RUNNING) BIOS_Menu = 88; //ET4000 extensions!
			break;
		case 11: //Black pedestal?
			BIOS_Menu = 89; //Black pedestal!
			break;
		default:
			BIOS_Menu = NOTIMPLEMENTED; //Not implemented yet!
			break;
		}
		break;
	default: //Unknown option?
		BIOS_Menu = NOTIMPLEMENTED; //Not implemented yet!
		break;
	}
}

void BIOS_InitSoundText()
{
	advancedoptions = 0; //Init!
	int i;
	for (i = 0; i<9; i++) //Clear all possibilities!
	{
		cleardata(&menuoptions[i][0], sizeof(menuoptions[i])); //Init!
	}

	optioninfo[advancedoptions] = 0; //MPU Soundfont!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "MPU Soundfont: ");
	if (strcmp(BIOS_Settings.SoundFont, "") != 0)
	{
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), BIOS_Settings.SoundFont); //The selected soundfont!
	}
	else
	{
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "<None>");
	}

	if (directMIDISupported()) //Direct MIDI is supported?
	{
		optioninfo[advancedoptions] = 1; //Game Blaster!
		safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Direct MIDI Passthrough: ");
		if (BIOS_Settings.useDirectMIDI)
		{
			safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Enabled");
		}
		else
		{
			safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Disabled");
		}
	}

	optioninfo[advancedoptions] = 2; //PC Speaker!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "PC Speaker: ");
	if (BIOS_Settings.usePCSpeaker)
	{
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Sound");
	}
	else
	{
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "No sound");
	}

	optioninfo[advancedoptions] = 3; //Adlib!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Adlib: ");
	if (BIOS_Settings.useAdlib)
	{
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Enabled");
	}
	else
	{
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Disabled");
	}

	optioninfo[advancedoptions] = 4; //LPT DAC!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "LPT DAC: ");
	if (BIOS_Settings.useLPTDAC)
	{
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Enabled");
	}
	else
	{
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Disabled");
	}

	optioninfo[advancedoptions] = 5; //Game Blaster!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Game Blaster: ");
	if (BIOS_Settings.useGameBlaster)
	{
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Enabled");
	}
	else
	{
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Disabled");
	}

	optioninfo[advancedoptions] = 6; //Sound Blaster!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Sound Blaster: ");
	redetectSoundBlaster:
	switch (BIOS_Settings.useSoundBlaster)
	{
	case 1:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Version 1.0 (with GB)/1.5 (without GB)");
		break;
	case 2:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Version 2.0");
		break;
	case 0:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Disabled");
		break;
	default:
		BIOS_Settings.useSoundBlaster = DEFAULT_SOUNDBLASTER;
		goto redetectSoundBlaster;
	}

	optioninfo[advancedoptions] = 7; //Sound Source Volume!
	snprintf(menuoptions[advancedoptions],sizeof(menuoptions[0]),"Sound Source Volume: %u",(int)(BIOS_Settings.SoundSource_Volume)); //Sound source volume as a whole number!
	safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]),"%%"); //The percentage sign goes wrong with sprintf! Also, when converted to text layer we need to be doubled! This is the fix!

	optioninfo[advancedoptions] = 8; //Game Blaster Volume!
	snprintf(menuoptions[advancedoptions],sizeof(menuoptions[0]),"Game Blaster Volume: %u",(int)(BIOS_Settings.GameBlaster_Volume)); //Sound source volume as a whole number!
	safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]),"%%"); //The percentage sign goes wrong with sprintf! Also, when converted to text layer we need to be doubled! This is the fix!

	if (!EMU_RUNNING)
	{
		optioninfo[advancedoptions] = 9; //Music player!
		safestrcpy(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Music Player");
	}

	optioninfo[advancedoptions] = 10; //Start/stop recording sound!
	if (!sound_isRecording()) //Not recording yet?
	{
		safestrcpy(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Start recording sound"); //Sound source volume as a whole number!
	}
	else
	{
		safestrcpy(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Stop recording sound"); //Sound source volume as a whole number!
	}
}

void BIOS_SoundMenu() //Manage stuff concerning input.
{
	BIOS_Title("Sound Settings Menu");
	BIOS_InitSoundText(); //Init text!
	int menuresult = ExecuteMenu(advancedoptions, 4, BIOSMENU_SPEC_RETURN, &Menu_Stat); //Show the menu options!
	switch (menuresult)
	{
	case BIOSMENU_SPEC_CANCEL: //Return?
		BIOS_Menu = 8; //Goto Advanced Menu!
		break;
	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
	case 9:
	case 10: //Valid option?
		switch (optioninfo[menuresult]) //What option has been chosen, since we are dynamic size?
		{
		case 0: //Soundfont selection?
			if (!EMU_RUNNING) BIOS_Menu = 32; //Soundfont setting!
			break;
		case 1: //Direct MIDI Passthrough?
			if (!EMU_RUNNING) BIOS_Menu = 59; //Game Blaster setting!
			break;
		case 2: //PC Speaker?
			if (!EMU_RUNNING) BIOS_Menu = 44; //PC Speaker setting!
			break;
		case 3: //Adlib?
			if (!EMU_RUNNING) BIOS_Menu = 45; //Adlib setting!
			break;
		case 4: //LPT DAC?
			if (!EMU_RUNNING) BIOS_Menu = 46; //LPT DAC setting!
			break;
		case 5: //Game Blaster?
			if (!EMU_RUNNING) BIOS_Menu = 52; //Game Blaster setting!
			break;
		case 6: //Sound Blaster?
			if (!EMU_RUNNING) BIOS_Menu = 54; //Game Blaster setting!
			break;
		case 7: //Sound Source Volume?
			BIOS_Menu = 38; //Sound Source Volume setting!
			break;
		case 8: //Game Blaster Volume?
			BIOS_Menu = 53; //Game Blaster Volume setting!
			break;
		case 9: //Play Music file(s)?
			BIOS_Menu = 33; //Play Music file(s)!
			break;
		case 10: //Sound recording?
			BIOS_Menu = 42; //Start/stop sound recording!
			break;
		default:
			break;
		}
		break;
	default: //Unknown option?
		BIOS_Menu = NOTIMPLEMENTED; //Not implemented yet!
		break;
	}
}

void BIOS_SoundFont_selection() //SoundFont selection menu!
{
	BIOS_Title("Mount Soundfont");
	generateFileList(soundfontpath,"sf2", 0, 0,0); //Generate file list for all .sf2 files!
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Soundfont: "); //Show selection init!
	EMU_unlocktext();

	int file = ExecuteList(12, 4, BIOS_Settings.SoundFont, 256,NULL,0); //Show menu for the disk image!
	switch (file) //Which file?
	{
	case FILELIST_NOFILES: //No files?
		BIOS_noentries(12, 4, "<No soundfonts present>");
	case FILELIST_DEFAULT: //Unmount?
		if (strcmp(BIOS_Settings.SoundFont, ""))
		{
			BIOS_Changed = 1; //Changed!
			reboot_needed |= 1; //We need to reboot!
			safestrcpy(BIOS_Settings.SoundFont,sizeof(BIOS_Settings.SoundFont), ""); //Unmount!
		}
		break;
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	default: //File?
		if (strcmp(BIOS_Settings.SoundFont, itemlist[file])!=0) //Changed?
		{
			BIOS_Changed = 1; //Changed!
			reboot_needed |= 1; //We need to reboot!
		}
		safestrcpy(BIOS_Settings.SoundFont,sizeof(BIOS_Settings.SoundFont), itemlist[file]); //Use this file!
		break;
	}
	BIOS_Menu = 31; //Return to the Sound menu!
}

int Sound_file = 0; //The file selected!

int BIOS_Sound_selection(byte reload) //Music selection menu, custom for this purpose!
{
	BIOS_Title("Select a music file to play");
	if (reload) //To reload the list?
	{
		generateFileList(musicpath, "mid|midi|dro", 0, 0, 0); //Generate file list for all Sound files!
	}
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Music file: "); //Show selection init!
	EMU_unlocktext();
	BIOS_EnablePlay = 1; //Enable Play=OK!
	int file = ExecuteList(12, 4, itemlist[Sound_file], 256,NULL,0); //Show menu for the disk image!
	BIOS_EnablePlay = 0; //Disable play again!
	switch (file) //Which file?
	{
	case FILELIST_DEFAULT: //Execute default selection?
		return -2; //Give to our caller to handle!
		break;
	case FILELIST_CANCEL: //Cancelled?
		return -1; //Not selected!
		//We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_NOFILES: //No files?
		BIOS_noentries(12, 4, "<No music files present>");
		return -1; //Not selected!
		break;
	default: //File?
		return file; //Use this file!
	}
	return -1; //Just in case!
}

byte sound_playSoundfile(byte showinfo)
{
	char songpath[256];
	memset(&songpath,0,sizeof(songpath)); //Init our path!
	Sound_file = BIOS_Sound_selection(1); //Allow the user to select a Music file! Load the list from disk!
	goto handle_soundfile;
	for (;;) //Music selection loop!
	{
		Sound_file = BIOS_Sound_selection(0); //Allow the user to select a Music file! Don't reload the list!
		handle_soundfile: //First sound file handling!
		if (Sound_file < 0) //Not selected?
		{
			Sound_file = 0;
			if (Sound_file == -2) //Default selected?
			{
				break; //Stop selection of the Music file!
			}
			else //Full cancel to execute?
			{
				return 0; //Allow our caller to execute the next step!
			}
		}
		EMU_locktext();
		EMU_textcolor(0x04); //Green on black!
		GPU_EMU_printscreen(0, GPU_TEXTSURFACE_HEIGHT - 1, "Playing..."); //Show playing init!
		EMU_unlocktext();
		//Play the MIDI file!
		if (isext(&itemlist[Sound_file][0],"mid|midi")) //MIDI file?
		{
			safestrcpy(songpath,sizeof(songpath),musicpath); //Load the path!
			safestrcat(songpath,sizeof(songpath),"/");
			safestrcat(songpath,sizeof(songpath),itemlist[Sound_file]); //The full filename!
			playMIDIFile(&songpath[0], showinfo); //Play the MIDI file!
		}
		else if (isext(&itemlist[Sound_file][0],"dro")) //DRO file?
		{
			safestrcpy(songpath,sizeof(songpath), musicpath); //Load the path!
			safestrcat(songpath,sizeof(songpath), "/");
			safestrcat(songpath,sizeof(songpath), itemlist[Sound_file]); //The full filename!
			playDROFile(&songpath[0], showinfo); //Play the DRO file!
		}
		EMU_locktext();
		GPU_EMU_printscreen(0, GPU_TEXTSURFACE_HEIGHT - 1, "          "); //Show playing finished!
		EMU_unlocktext();
	}
	return 1; //Plain finish: just execute whatever you want!
}

void BIOS_MusicPlayer() //Music Player!
{
	sound_playSoundfile(0); //Play one or more Music files! Don't show any information!
	BIOS_Menu = 31; //Return to the Sound menu!
}

char currentarchtext[6][256] = { "XT","AT","Compaq Deskpro 386","Compaq Deskpro 386 with PS/2 mouse","i430fx","i440fx" }; //The text values for the results of getcurrentarch().

void BIOS_Architecture()
{
	BIOS_Title("Architecture");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Architecture: "); //Show selection init!
	EMU_unlocktext();
	int i = 0; //Counter!
	numlist = 6; //Amount of Direct modes!
	for (i = 0; i<6; i++) //Process options!
	{
		cleardata(&itemlist[i][0], sizeof(itemlist[i])); //Reset!
	}
	safestrcpy(itemlist[ARCHITECTURE_XT],sizeof(itemlist[0]), "XT"); //Set filename from options!
	safestrcpy(itemlist[ARCHITECTURE_AT],sizeof(itemlist[0]), "AT"); //Set filename from options!
	safestrcpy(itemlist[ARCHITECTURE_PS2],sizeof(itemlist[0]), "Compaq Deskpro 386 with PS/2 mouse"); //Set filename from options!
	safestrcpy(itemlist[ARCHITECTURE_COMPAQ],sizeof(itemlist[0]), "Compaq Deskpro 386"); //Set filename from options!
	safestrcpy(itemlist[ARCHITECTURE_i430fx], sizeof(itemlist[0]), "i430fx"); //Set filename from options!
	safestrcpy(itemlist[ARCHITECTURE_i440fx], sizeof(itemlist[0]), "i440fx"); //Set filename from options!
	int current = 0;
	switch (BIOS_Settings.architecture) //What setting?
	{
	case ARCHITECTURE_XT: //Valid
	case ARCHITECTURE_AT: //Valid
	case ARCHITECTURE_PS2: //Valid
	case ARCHITECTURE_COMPAQ: //Valid
	case ARCHITECTURE_i430fx: //Valid
	case ARCHITECTURE_i440fx: //Valid
		current = BIOS_Settings.architecture; //Valid: use!
		break;
	default: //Invalid
		current = ARCHITECTURE_XT; //Default: none!
		break;
	}
	if (BIOS_Settings.architecture != current) //Invalid?
	{
		BIOS_Settings.architecture = current; //Safety!
		BIOS_Changed = 1; //Changed!
	}
	int file = ExecuteList(14, 4, itemlist[current], 256,NULL,0); //Show options for the installed CPU!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = 0; //Default setting: Disabled!

	case ARCHITECTURE_XT:
	case ARCHITECTURE_AT:
	case ARCHITECTURE_PS2:
	case ARCHITECTURE_COMPAQ:
	case ARCHITECTURE_i430fx:
	case ARCHITECTURE_i440fx:
	default: //Changed?
		if (file != current) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			BIOS_Settings.architecture = file; //Select architecture setting!
			reboot_needed |= 1; //A reboot is needed when applied!
		}
		break;
	}
	BIOS_Menu = 35; //Goto CPU menu!
}

void BIOSMenu_breakpointDisplay(byte point)
{
	byte breakpointindex[5] = {10,11,12,13,14};
	if (point >= NUMITEMS(BIOS_Settings.breakpoint)) return; //Can't handle this?
	optioninfo[advancedoptions] = breakpointindex[point]; //Breakpoint!
	safestrcpy(menuoptions[advancedoptions], sizeof(menuoptions[0]), "Breakpoint: ");
	//First, convert the current breakpoint to a string format!
	switch ((BIOS_Settings.breakpoint[point] >> SETTINGS_BREAKPOINT_MODE_SHIFT)) //What mode?
	{
	case 0: //No breakpoint?
		safestrcat(menuoptions[advancedoptions], sizeof(menuoptions[0]), "Not set"); //seg16:offs16 default!
		break;
	case 1: //Real mode?
		safescatnprintf(menuoptions[advancedoptions], sizeof(menuoptions[0]), "%04X:%04X", (word)((BIOS_Settings.breakpoint[point] >> SETTINGS_BREAKPOINT_SEGMENT_SHIFT)& SETTINGS_BREAKPOINT_SEGMENT_MASK), (word)((BIOS_Settings.breakpoint[point] & SETTINGS_BREAKPOINT_OFFSET_MASK) & 0xFFFF)); //seg16:offs16!
		if ((BIOS_Settings.breakpoint[point] >> SETTINGS_BREAKPOINT_IGNOREADDRESS_SHIFT) & 1) //Ignore address?
		{
			safestrcat(menuoptions[advancedoptions], sizeof(menuoptions[0]), "M"); //Ignore Address!
		}
		else if ((BIOS_Settings.breakpoint[point] >> SETTINGS_BREAKPOINT_IGNOREEIP_SHIFT) & 1) //Ignore EIP?
		{
			safestrcat(menuoptions[advancedoptions], sizeof(menuoptions[0]), "I"); //Ignore EIP!
		}
		else if ((BIOS_Settings.breakpoint[point] >> SETTINGS_BREAKPOINT_IGNORESEGMENT_SHIFT) & 1) //Ignore CS?
		{
			safestrcat(menuoptions[advancedoptions], sizeof(menuoptions[0]), "O"); //Ignore CS!
		}
		if ((BIOS_Settings.breakpoint[point] >> SETTINGS_BREAKPOINT_SINGLESTEP_SHIFT) & 1) //Single step?
		{
			safestrcat(menuoptions[advancedoptions], sizeof(menuoptions[0]), "S"); //Single step!
		}
		break;
	case 2: //Protected mode?
		safescatnprintf(menuoptions[advancedoptions], sizeof(menuoptions[0]), "%04X:%08XP", (word)((BIOS_Settings.breakpoint[point] >> SETTINGS_BREAKPOINT_SEGMENT_SHIFT)& SETTINGS_BREAKPOINT_SEGMENT_MASK), (uint_32)(BIOS_Settings.breakpoint[point] & SETTINGS_BREAKPOINT_OFFSET_MASK)); //seg16:offs16!
		if ((BIOS_Settings.breakpoint[point] >> SETTINGS_BREAKPOINT_IGNOREADDRESS_SHIFT) & 1) //Ignore address?
		{
			safestrcat(menuoptions[advancedoptions], sizeof(menuoptions[0]), "M"); //Ignore Address!
		}
		else if ((BIOS_Settings.breakpoint[point] >> SETTINGS_BREAKPOINT_IGNOREEIP_SHIFT) & 1) //Ignore EIP?
		{
			safestrcat(menuoptions[advancedoptions], sizeof(menuoptions[0]), "I"); //Ignore EIP!
		}
		else if ((BIOS_Settings.breakpoint[point] >> SETTINGS_BREAKPOINT_IGNORESEGMENT_SHIFT) & 1) //Ignore CS?
		{
			safestrcat(menuoptions[advancedoptions], sizeof(menuoptions[0]), "O"); //Ignore CS!
		}
		if ((BIOS_Settings.breakpoint[point] >> SETTINGS_BREAKPOINT_SINGLESTEP_SHIFT) & 1) //Single step?
		{
			safestrcat(menuoptions[advancedoptions], sizeof(menuoptions[0]), "S"); //Single step!
		}
		break;
	case 3: //Virtual 8086 mode?
		safescatnprintf(menuoptions[advancedoptions], sizeof(menuoptions[0]), "%04X:%04XV", (word)((BIOS_Settings.breakpoint[point] >> SETTINGS_BREAKPOINT_SEGMENT_SHIFT)& SETTINGS_BREAKPOINT_SEGMENT_MASK), (word)((BIOS_Settings.breakpoint[point] & SETTINGS_BREAKPOINT_OFFSET_MASK) & 0xFFFF)); //seg16:offs16!
		if ((BIOS_Settings.breakpoint[point] >> SETTINGS_BREAKPOINT_IGNOREADDRESS_SHIFT) & 1) //Ignore address?
		{
			safestrcat(menuoptions[advancedoptions], sizeof(menuoptions[0]), "M"); //Ignore Address!
		}
		else if ((BIOS_Settings.breakpoint[point] >> SETTINGS_BREAKPOINT_IGNOREEIP_SHIFT) & 1) //Ignore EIP?
		{
			safestrcat(menuoptions[advancedoptions], sizeof(menuoptions[0]), "I"); //Ignore EIP!
		}
		else if ((BIOS_Settings.breakpoint[point] >> SETTINGS_BREAKPOINT_IGNORESEGMENT_SHIFT) & 1) //Ignore CS?
		{
			safestrcat(menuoptions[advancedoptions], sizeof(menuoptions[0]), "O"); //Ignore CS!
		}
		if ((BIOS_Settings.breakpoint[point] >> SETTINGS_BREAKPOINT_SINGLESTEP_SHIFT) & 1) //Single step?
		{
			safestrcat(menuoptions[advancedoptions], sizeof(menuoptions[0]), "S"); //Single step!
		}
		break;
	default: //Just in case!
		safestrcat(menuoptions[advancedoptions], sizeof(menuoptions[0]), "<UNKNOWN. CHECK SETTINGS VERSION>");
		break;
	}
	++advancedoptions; //Increase after!
}

void BIOS_InitCPUText()
{
	advancedoptions = 0; //Init!
	int i;
	for (i = 0; i<15; i++) //Clear all possibilities!
	{
		cleardata(&menuoptions[i][0], sizeof(menuoptions[i])); //Init!
	}

	optioninfo[advancedoptions] = 0; //Installed CPU!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Installed CPU: "); //Change installed CPU!
	switch (*(getarchemulated_CPU())) //8086?
	{
	case CPU_8086: //8086?
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Intel 8086/8088"); //Add installed CPU!
		break;
	case CPU_NECV30: //NEC V20/V30?
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "NEC V20/V30"); //Add installed CPU!
		break;
	case CPU_80286: //80286?
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Intel 80286"); //Add installed CPU!
		break;
	case CPU_80386: //80386?
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Intel 80386"); //Add installed CPU!
		break;
	case CPU_80486: //80486?
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Intel 80486"); //Add installed CPU!
		break;
	case CPU_PENTIUM: //PENTIUM?
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Intel Pentium(without FPU)"); //Add installed CPU!
		break;
	case CPU_PENTIUMPRO: //PENTIUM PRO?
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Intel Pentium Pro(without FPU)"); //Add installed CPU!
		break;
	case CPU_PENTIUM2: //PENTIUM II?
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Intel Pentium II(without FPU)"); //Add installed CPU!
		break;
	default:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "<UNKNOWN. CHECK SETTINGS VERSION>"); //Add uninstalled CPU!
		break;
	}

	optioninfo[advancedoptions] = 1; //Change emulated CPUs!
	safestrcpy(menuoptions[advancedoptions], sizeof(menuoptions[0]), "Emulated CPUs: ");
	switch (*(getarchemulated_CPUs())) //What CPU speed limit?
	{
	case 0: //Default cycles?
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "All available processors"); //Default!
		break;
	default: //Limited cycles?
		safescatnprintf(menuoptions[advancedoptions], sizeof(menuoptions[0]), "%u processors", *(getarchemulated_CPUs())); //Cycle limit!
		++advancedoptions;
		break;
	}

setDataBusSize: //For fixing it!
	optioninfo[advancedoptions] = 2; //Data bus size!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Data bus size: ");
	switch (*(getarchDataBusSize())) //Data bus size?
	{
	case 0:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Full sized data bus of 16/32-bits");
		break;
	case 1:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Reduced data bus size");
		break;
	default: //Error: fix it!
		*(getarchDataBusSize()) = 0; //Reset/Fix!
		BIOS_Changed = 1; //We've changed!
		goto setDataBusSize; //Goto!
		break;
	}

	optioninfo[advancedoptions] = 3; //Change CPU speed!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "CPU Speed: ");
	switch (*(getarchCPUSpeed())) //What CPU speed limit?
	{
	case 0: //Default cycles?
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Default"); //Default!
		break;
	default: //Limited cycles?
		safescatnprintf(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Limited to %u cycles",*(getarchCPUSpeed())); //Cycle limit!
		++advancedoptions;
		break;
	}

	optioninfo[advancedoptions] = 4; //Change Turbo CPU speed!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Turbo CPU Speed: ");
	switch (*(getarchTurboCPUSpeed())) //What Turbo CPU speed limit?
	{
	case 0: //Default cycles?
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Default"); //Default!
		break;
	default: //Limited cycles?
		safescatnprintf(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Limited to %u cycles", *(getarchTurboCPUSpeed())); //Cycle limit!
		++advancedoptions;
		break;
	}

	fixTurboCPUToggle:
	optioninfo[advancedoptions] = 5; //Change Turbo CPU option!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Turbo CPU Speed Mode: ");
	switch (*(getarchuseTurboCPUSpeed())) //What Turbo CPU speed limit?
	{
	case 0: //Disabled?
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Disabled"); //Default!
		break;
	case 1: //Enabled?
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Enabled"); //Default!
		break;
	default: //Limited cycles?
		*(getarchuseTurboCPUSpeed()) = 0; //Disable!
		BIOS_Changed = 1; //Changed!
		goto fixTurboCPUToggle; //Fix it!
		break;
	}

	fixClockingMode:
	optioninfo[advancedoptions] = 6; //Change Turbo CPU option!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Clocking mode: ");
	switch (*(getarchclockingmode())) //What clocking mode?
	{
	case CLOCKINGMODE_CYCLEACCURATE: //Disabled?
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Cycle-accurate clock"); //Default!
		break;
	case CLOCKINGMODE_IPSCLOCK: //Enabled?
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "IPS clock"); //Default!
		break;
	default: //Limited cycles?
		*(getarchclockingmode()) = CLOCKINGMODE_CYCLEACCURATE; //Default!
		BIOS_Changed = 1; //Changed!
		goto fixClockingMode; //Fix it!
		break;
	}

setShowCPUSpeed:
	optioninfo[advancedoptions] = 7; //Change CPU speed!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Show CPU Speed: ");
	switch (BIOS_Settings.ShowCPUSpeed) //What CPU speed limit?
	{
	case 0: //No?
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Disabled"); //Disabled!
		break;
	case 1: //Yes?
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Enabled"); //Enabled!
		break;
	default: //Error: fix it!
		BIOS_Settings.ShowCPUSpeed = 0; //Reset/Fix!
		BIOS_Changed = 1; //We've changed!
		goto setShowCPUSpeed; //Goto!
		break;
	}

	optioninfo[advancedoptions] = 8; //Boot Order!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Boot Order: "); //Change boot order!
	safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), BOOT_ORDER_STRING[BIOS_Settings.bootorder]); //Add boot order after!

	optioninfo[advancedoptions] = 9; //Execution mode!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Execution mode: ");
	switch (BIOS_Settings.executionmode) //What execution mode is active?
	{
	case EXECUTIONMODE_NONE:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Use emulator internal BIOS"); //Set filename from options!
		break;
	case EXECUTIONMODE_TEST:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Run debug directory files"); //Set filename from options!
		break;
	case EXECUTIONMODE_TESTROM:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Run TESTROM.DAT at 0000:0000"); //Set filename from options!
		break;
	case EXECUTIONMODE_VIDEOCARD:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Debug video card output"); //Set filename from options!
		break;
	case EXECUTIONMODE_BIOS:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Load BIOS from ROM directory."); //Set filename from options!
		break;
	case EXECUTIONMODE_SOUND:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Run sound test"); //Set filename from options!
		break;
	default:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "<UNKNOWN. CHECK SETTINGS VERSION>");
		break;
	}

setArchitecture: //For fixing it!
	optioninfo[advancedoptions] = 10; //Architecture!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Architecture: ");
	switch (BIOS_Settings.architecture) //What architecture?
	{
	case ARCHITECTURE_XT:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "XT");
		break;
	case ARCHITECTURE_AT:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "AT");
		break;
	case ARCHITECTURE_PS2:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Compaq Deskpro 386 with PS/2 mouse");
		break;
	case ARCHITECTURE_COMPAQ:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Compaq Deskpro 386");
		break;
	case ARCHITECTURE_i430fx:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "i430fx");
		break;
	case ARCHITECTURE_i440fx:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "i440fx");
		break;
	default: //Error: fix it!
		BIOS_Settings.architecture = ARCHITECTURE_XT; //Reset/Fix!
		BIOS_Changed = 1; //We've changed!
		goto setArchitecture; //Goto!
		break;
	}

setBIOSROMmode: //For fixing it!
	optioninfo[advancedoptions] = 11; //BIOS ROM mode!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "BIOS ROM mode: ");
	switch (BIOS_Settings.BIOSROMmode) //What architecture?
	{
	case BIOSROMMODE_NORMAL:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Normal BIOS ROM");
		break;
	case BIOSROMMODE_DIAGNOSTICS:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Diagnostic ROM");
		break;
	case BIOSROMMODE_UROMS:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Enforce normal U-ROMs");
		break;
	default: //Error: fix it!
		BIOS_Settings.BIOSROMmode = DEFAULT_BIOSROMMODE; //Reset/Fix!
		BIOS_Changed = 1; //We've changed!
		goto setBIOSROMmode; //Goto!
		break;
	}

setInboardInitialWaitstates: //For fixing it!
	optioninfo[advancedoptions] = 12; //Inboard Initial Waitstates!
	safestrcpy(menuoptions[advancedoptions],sizeof(menuoptions[0]), "Inboard Initial Waitstates: ");
	switch (BIOS_Settings.InboardInitialWaitstates) //What architecture?
	{
	case 0:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "Default waitstates");
		break;
	case 1:
		safestrcat(menuoptions[advancedoptions++],sizeof(menuoptions[0]), "No waitstates");
		break;
	default: //Error: fix it!
		BIOS_Settings.InboardInitialWaitstates = DEFAULT_INBOARDINITIALWAITSTATES; //Reset/Fix!
		BIOS_Changed = 1; //We've changed!
		goto setInboardInitialWaitstates; //Goto!
		break;
	}

	optioninfo[advancedoptions] = 13; //We're CPUID mode setting!
	safestrcpy(menuoptions[advancedoptions], sizeof(menuoptions[0]), "CPUID mode: ");
	switch (*(getarchCPUIDmode()))
	{
	case 0: //Modern mode?
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Modern mode"); //Set filename from options!
		break;
	case 1: //Limited to leaf 1
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Limited to leaf 1"); //Set filename from options!
		break;
	case 2: //Set to DX on start
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Set to DX on start"); //Set filename from options!
		break;
	default:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "<UNKNOWN. CHECK SETTINGS VERSION>"); //Set filename from options!
		break;
	}

	optioninfo[advancedoptions] = 14; //We're debugger settings!
	safestrcpy(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Debugger Settings");
}

void BIOS_InitCPUDebuggerText()
{
	advancedoptions = 0; //Init!
	int i;
	for (i = 0; i < 20; i++) //Clear all possibilities!
	{
		cleardata(&menuoptions[i][0], sizeof(menuoptions[i])); //Init!
	}

	optioninfo[advancedoptions] = 0; //Debug mode!
	safestrcpy(menuoptions[advancedoptions], sizeof(menuoptions[0]), "Debug mode: ");
	switch (BIOS_Settings.debugmode) //What debug mode is active?
	{
	case DEBUGMODE_NONE:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "No debugger enabled"); //Set filename from options!
		break;
	case DEBUGMODE_RTRIGGER:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Enabled, RTrigger=Step"); //Set filename from options!
		break;
	case DEBUGMODE_STEP:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Enabled, Step through"); //Set filename from options!
		break;
	case DEBUGMODE_SHOW_RUN:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Enabled, just run, ignore shoulder buttons"); //Set filename from options!
		break;
	case DEBUGMODE_NOSHOW_RUN:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Enabled, just run, don't show, ignore shoulder buttons"); //Set filename from options!
		break;
	default:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "<UNKNOWN. CHECK SETTINGS VERSION>");
		break;
	}

	optioninfo[advancedoptions] = 1; //We're debug log setting!
	safestrcpy(menuoptions[advancedoptions], sizeof(menuoptions[0]), "Debugger log: ");
	switch (BIOS_Settings.debugger_log)
	{
	case DEBUGGERLOG_NONE: //None
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Don't log"); //Set filename from options!
		break;
	case DEBUGGERLOG_DEBUGGING: //Only when debugging
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Only when debugging"); //Set filename from options!
		break;
	case DEBUGGERLOG_ALWAYS: //Always
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Always log"); //Set filename from options!
		break;
	case DEBUGGERLOG_INT: //Interrupt calls only
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Interrupt calls only");
		break;
	case DEBUGGERLOG_DIAGNOSTICCODES: //Diagnostic codes only
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "BIOS Diagnostic codes only");
		break;
	case DEBUGGERLOG_ALWAYS_NOREGISTERS: //Always, no register state!
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Always log, no register state");
		break;
	case DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Always log, even during skipping");
		break;
	case DEBUGGERLOG_ALWAYS_SINGLELINE:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Always log, even during skipping, single line format");
		break;
	case DEBUGGERLOG_DEBUGGING_SINGLELINE: //Only when debugging, single line format
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Only when debugging, single line format");
		break;
	case DEBUGGERLOG_ALWAYS_SINGLELINE_SIMPLIFIED:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Always log, even during skipping, single line format, simplified");
		break;
	case DEBUGGERLOG_DEBUGGING_SINGLELINE_SIMPLIFIED: //Only when debugging, single line format
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Only when debugging, single line format, simplified");
		break;
	case DEBUGGERLOG_ALWAYS_COMMONLOGFORMAT: //Always log, common log format
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Always log, common log format");
		break;
	case DEBUGGERLOG_ALWAYS_DURINGSKIPSTEP_COMMONLOGFORMAT: //Always log, even during skipping, common log format
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Always log, even during skipping, common log format");
		break;
	case DEBUGGERLOG_DEBUGGING_COMMONLOGFORMAT: //Only when debugging, common log format
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Only when debugging, common log format");
		break;
	default:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Never"); //Set filename from options!
		break;
	}

	optioninfo[advancedoptions] = 2; //We're debug log setting!
	safestrcpy(menuoptions[advancedoptions], sizeof(menuoptions[0]), "Debugger state log: ");
	switch (BIOS_Settings.debugger_logstates)
	{
	case DEBUGGERSTATELOG_DISABLED: //None
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Disabled"); //Set filename from options!
		break;
	case DEBUGGERSTATELOG_ENABLED: //Only when debugging
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Enabled"); //Set filename from options!
		break;
	default:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "<UNKNOWN. CHECK SETTINGS VERSION>"); //Set filename from options!
		break;
	}

	optioninfo[advancedoptions] = 3; //We're debug log setting!
	safestrcpy(menuoptions[advancedoptions], sizeof(menuoptions[0]), "Debugger register log: ");
	switch (BIOS_Settings.debugger_logregisters)
	{
	case DEBUGGERSTATELOG_DISABLED: //None
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Disabled"); //Set filename from options!
		break;
	case DEBUGGERSTATELOG_ENABLED: //Only when debugging
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Enabled"); //Set filename from options!
		break;
	default:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "<UNKNOWN. CHECK SETTINGS VERSION>"); //Set filename from options!
		break;
	}


	optioninfo[advancedoptions] = 4; //We're diagnostics output!
	if (BIOS_Settings.diagnosticsportoutput_breakpoint >= 0) //Diagnostics breakpoint specified?
	{
		snprintf(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Diagnostics code: %02X, Breakpoint at %02X", diagnosticsportoutput, (BIOS_Settings.diagnosticsportoutput_breakpoint & 0xFF)); //Show the diagnostics output!
	}
	else
	{
		snprintf(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Diagnostics code: %02X", diagnosticsportoutput); //Show the diagnostics output!
	}

	optioninfo[advancedoptions] = 5; //Change Diagnostics Port Breakpoint Timeout!
	safestrcpy(menuoptions[advancedoptions], sizeof(menuoptions[0]), "Diagnostics Port Breakpoint Timeout: ");
	switch (BIOS_Settings.diagnosticsportoutput_timeout) //What Diagnostics Port Breakpoint Timeout?
	{
	case 0: //Default cycles?
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "First instruction"); //Default!
		break;
	default: //Limited cycles?
		safescatnprintf(menuoptions[advancedoptions], sizeof(menuoptions[0]), "At " LONGLONGSPRINTF " instructions", ((LONG64SPRINTF)(BIOS_Settings.diagnosticsportoutput_timeout + 1))); //Cycle limit!
		++advancedoptions;
		break;
	}

	BIOSMenu_breakpointDisplay(0);
	BIOSMenu_breakpointDisplay(1);
	BIOSMenu_breakpointDisplay(2);
	BIOSMenu_breakpointDisplay(3);
	BIOSMenu_breakpointDisplay(4);

	optioninfo[advancedoptions] = 7; //Task Breakpoint!
	safestrcpy(menuoptions[advancedoptions], sizeof(menuoptions[0]), "Task Breakpoint: ");
	//First, convert the current breakpoint to a string format!
	switch ((BIOS_Settings.taskBreakpoint >> SETTINGS_TASKBREAKPOINT_ENABLE_SHIFT)) //What mode?
	{
	case 0: //No breakpoint?
		safestrcat(menuoptions[advancedoptions], sizeof(menuoptions[0]), "Not set"); //seg16:offs16 default!
		break;
	case 1: //Enabled?
		safescatnprintf(menuoptions[advancedoptions], sizeof(menuoptions[0]), "%04X:%08X", (word)((BIOS_Settings.taskBreakpoint >> SETTINGS_TASKBREAKPOINT_SEGMENT_SHIFT) & SETTINGS_TASKBREAKPOINT_SEGMENT_MASK), (uint_32)(BIOS_Settings.taskBreakpoint & SETTINGS_TASKBREAKPOINT_BASE_MASK)); //seg16:offs16!
		if ((BIOS_Settings.taskBreakpoint >> SETTINGS_TASKBREAKPOINT_IGNOREBASE_SHIFT) & 1) //Ignore Base?
		{
			safestrcat(menuoptions[advancedoptions], sizeof(menuoptions[0]), "I"); //Ignore EIP!
		}
		else if ((BIOS_Settings.taskBreakpoint >> SETTINGS_TASKBREAKPOINT_IGNORESEGMENT_SHIFT) & 1) //Ignore TR?
		{
			safestrcat(menuoptions[advancedoptions], sizeof(menuoptions[0]), "O"); //Ignore TR!
		}
		break;
	default: //Just in case!
		safestrcat(menuoptions[advancedoptions], sizeof(menuoptions[0]), "<UNKNOWN. CHECK SETTINGS VERSION>");
		break;
	}
	++advancedoptions; //Increase after!

	optioninfo[advancedoptions] = 9; //FS Breakpoint!
	safestrcpy(menuoptions[advancedoptions], sizeof(menuoptions[0]), "FS Breakpoint: ");
	//First, convert the current breakpoint to a string format!
	switch ((BIOS_Settings.FSBreakpoint >> SETTINGS_FSBREAKPOINT_ENABLE_SHIFT)) //What mode?
	{
	case 0: //No breakpoint?
		safestrcat(menuoptions[advancedoptions], sizeof(menuoptions[0]), "Not set"); //seg16:offs16 default!
		break;
	case 1: //Enabled?
		safescatnprintf(menuoptions[advancedoptions], sizeof(menuoptions[0]), "%04X:%08X", (word)((BIOS_Settings.FSBreakpoint >> SETTINGS_TASKBREAKPOINT_SEGMENT_SHIFT) & SETTINGS_TASKBREAKPOINT_SEGMENT_MASK), (uint_32)(BIOS_Settings.FSBreakpoint & SETTINGS_TASKBREAKPOINT_BASE_MASK)); //seg16:offs16!
		if ((BIOS_Settings.FSBreakpoint >> SETTINGS_FSBREAKPOINT_IGNOREBASE_SHIFT) & 1) //Ignore Base?
		{
			safestrcat(menuoptions[advancedoptions], sizeof(menuoptions[0]), "I"); //Ignore EIP!
		}
		else if ((BIOS_Settings.FSBreakpoint >> SETTINGS_FSBREAKPOINT_IGNORESEGMENT_SHIFT) & 1) //Ignore TR?
		{
			safestrcat(menuoptions[advancedoptions], sizeof(menuoptions[0]), "O"); //Ignore TR!
		}
		break;
	default: //Just in case!
		safestrcat(menuoptions[advancedoptions], sizeof(menuoptions[0]), "<UNKNOWN. CHECK SETTINGS VERSION>");
		break;
	}
	++advancedoptions; //Increase after!

	optioninfo[advancedoptions] = 8; //CR3 Breakpoint!
	safestrcpy(menuoptions[advancedoptions], sizeof(menuoptions[0]), "CR3 Breakpoint: ");
	//First, convert the current breakpoint to a string format!
	switch ((BIOS_Settings.CR3breakpoint >> SETTINGS_CR3BREAKPOINT_ENABLE_SHIFT)) //What mode?
	{
	case 0: //No breakpoint?
		safestrcat(menuoptions[advancedoptions], sizeof(menuoptions[0]), "Not set"); //seg16:offs16 default!
		break;
	case 1: //Enabled?
		safescatnprintf(menuoptions[advancedoptions], sizeof(menuoptions[0]), "%08X", (uint_32)(BIOS_Settings.CR3breakpoint & SETTINGS_CR3BREAKPOINT_BASE_MASK)); //seg16:offs16!
		break;
	default: //Just in case!
		safestrcat(menuoptions[advancedoptions], sizeof(menuoptions[0]), "<UNKNOWN. CHECK SETTINGS VERSION>");
		break;
	}
	++advancedoptions; //Increase after!

	optioninfo[advancedoptions] = 6; //We're debug advanced log setting!
	safestrcpy(menuoptions[advancedoptions], sizeof(menuoptions[0]), "Debugger advanced log: ");
	switch (BIOS_Settings.advancedlog)
	{
	case DEBUGGERSTATELOG_DISABLED: //None
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Disable advanced logging"); //Set filename from options!
		break;
	case DEBUGGERSTATELOG_ENABLED: //Only when debugging
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "Use advanced logging"); //Set filename from options!
		break;
	default:
		safestrcat(menuoptions[advancedoptions++], sizeof(menuoptions[0]), "<UNKNOWN. CHECK SETTINGS VERSION>"); //Set filename from options!
		break;
	}
}

byte BPindex; //What breakpoint to use?

void BIOS_CPUDebuggerMenu() //CPU debugger menu!
{
	BIOS_Title("CPU Debugger Menu");
	BIOS_InitCPUDebuggerText(); //Init text!
	int menuresult = ExecuteMenu(advancedoptions, 4, BIOSMENU_SPEC_RETURN | BIOSMENU_SPEC_SQUAREOPTION, &Menu_Stat); //Show the menu options!
	switch (menuresult)
	{
	case BIOSMENU_SPEC_CANCEL: //R: Main menu?
		BIOS_Menu = 35; //Goto CPU Menu!
		break;

	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
	case 9:
	case 10:
	case 11:
	case 12:
	case 13:
	case 14: //Valid option?
		switch (optioninfo[menuresult]) //What option has been chosen, since we are dynamic size?
		{
		//Debugger information
		case 0: //Debug mode?
			if (Menu_Stat == BIOSMENU_STAT_OK) //Plain select?
			{
				BIOS_Menu = 13; //Debug mode option!
			}
			break;
		case 1: //Debugger log setting!
			if (Menu_Stat == BIOSMENU_STAT_OK) //Plain select?
			{
				BIOS_Menu = 23; //Debugger log setting!
			}
			break;
		case 2: //Debugger state log setting!
			if (Menu_Stat == BIOSMENU_STAT_OK) //Plain select?
			{
				BIOS_Menu = 63; //Debugger state log setting!
			}
			break;
		case 3: //Debugger register log setting!
			if (Menu_Stat == BIOSMENU_STAT_OK) //Plain select?
			{
				BIOS_Menu = 66; //Debugger register log setting!
			}
			break;
		case 4: //Diagnostics output breakpoint setting!
			if (Menu_Stat == BIOSMENU_STAT_OK) //Plain select?
			{
				BIOS_Menu = 57; //Diagnostics Output Breakpoint setting!
			}
			else if (Menu_Stat == BIOSMENU_STAT_SQUARE) //SQUARE=Set current value as the breakpoint!
			{
				BIOS_Settings.diagnosticsportoutput_breakpoint = (sword)diagnosticsportoutput; //Set the current value as the breakpoint!
				BIOS_Changed = 1; //We've changed!
			}
			break; //We do nothing!
		case 5: //Timeout to be used for breakpoints?
			if (Menu_Stat == BIOSMENU_STAT_OK) //Plain select?
			{
				if (!EMU_RUNNING) BIOS_Menu = 58; //Timeout to be used for breakpoints?
			}
			break;
		case 6: //Debugger advanced log setting!
			if (Menu_Stat == BIOSMENU_STAT_OK) //Plain select?
			{
				BIOS_Menu = 69; //Debugger register log setting!
			}
			break;
		case 7: //Task Breakpoint
			if (Menu_Stat == BIOSMENU_STAT_OK) //Plain select?
			{
#ifndef IS_PSP
				//This option fails to compile on the PSP for some unknown reason.
				BIOS_Menu = 70; //Timeout to be used for breakpoints?
#endif
			}
			else if (Menu_Stat == BIOSMENU_STAT_SQUARE) //SQUARE=Set current address&mode as the breakpoint!
			{
				byte mode = 0;
				word segment;
				uint_32 offset;
				lock(LOCK_CPU); //Lock the CPU!
				switch (GENERALSEGMENT_P(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_TR])) //What mode are we?
				{
				case 0: //None?
					BIOS_Settings.taskBreakpoint = 0; //Disabled!
					break;
				default: //Enabled?
					mode = 1; //Enable!
					break;
				}
				if (mode) //Enabled?
				{
					segment = REG_TR; //CS!
					offset = (uint_32)CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_TR].PRECALCS.base; //Our offset!
					BIOS_Settings.taskBreakpoint = (((uint_64)mode & 1) << SETTINGS_TASKBREAKPOINT_ENABLE_SHIFT) | (((uint_64)segment & SETTINGS_TASKBREAKPOINT_SEGMENT_MASK) << SETTINGS_TASKBREAKPOINT_SEGMENT_SHIFT) | ((uint_64)offset & SETTINGS_TASKBREAKPOINT_BASE_MASK); //Set the new breakpoint!
					BIOS_Changed = 1; //We've changed!
				}
				unlock(LOCK_CPU); //Finished with the CPU!
			}
			break;
		case 8: //CR3 Breakpoint
			if (Menu_Stat == BIOSMENU_STAT_OK) //Plain select?
			{
#ifndef IS_PSP
				//This option fails to compile on the PSP for some unknown reason.
				BIOS_Menu = 71; //Timeout to be used for breakpoints?
#endif
			}
			else if (Menu_Stat == BIOSMENU_STAT_SQUARE) //SQUARE=Set current address&mode as the breakpoint!
			{
				byte mode = 0;
				uint_32 offset;
				lock(LOCK_CPU); //Lock the CPU!
				switch (getcpumode()) //What mode are we?
				{
				case CPU_MODE_REAL: //None?
					BIOS_Settings.CR3breakpoint = 0; //Disabled!
					break;
				default: //Enabled?
					mode = 1; //Enable!
					break;
				}
				if (mode) //Enabled?
				{
					offset = CPU[activeCPU].registers->CR3; //Our offset!
					BIOS_Settings.CR3breakpoint = (((uint_64)mode & 3) << SETTINGS_CR3BREAKPOINT_ENABLE_SHIFT) | ((uint_64)offset & SETTINGS_CR3BREAKPOINT_BASE_MASK); //Set the new breakpoint!
					BIOS_Changed = 1; //We've changed!
				}
				unlock(LOCK_CPU); //Finished with the CPU!
			}
			break;
		case 9: //FS Breakpoint
			if (Menu_Stat == BIOSMENU_STAT_OK) //Plain select?
			{
#ifndef IS_PSP
				//This option fails to compile on the PSP for some unknown reason.
				BIOS_Menu = 81; //Timeout to be used for breakpoints?
#endif
			}
			else if (Menu_Stat == BIOSMENU_STAT_SQUARE) //SQUARE=Set current address&mode as the breakpoint!
			{
				byte mode = 0;
				word segment;
				uint_32 offset;
				lock(LOCK_CPU); //Lock the CPU!
				switch (GENERALSEGMENT_P(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_TR])) //What mode are we?
				{
				case 0: //None?
					BIOS_Settings.FSBreakpoint = 0; //Disabled!
					break;
				default: //Enabled?
					mode = 1; //Enable!
					break;
				}
				if (mode) //Enabled?
				{
					segment = REG_FS; //CS!
					offset = (uint_32)CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_FS].PRECALCS.base; //Our offset!
					BIOS_Settings.FSBreakpoint = (((uint_64)mode & 1) << SETTINGS_FSBREAKPOINT_ENABLE_SHIFT) | (((uint_64)segment & SETTINGS_FSBREAKPOINT_SEGMENT_MASK) << SETTINGS_FSBREAKPOINT_SEGMENT_SHIFT) | ((uint_64)offset & SETTINGS_FSBREAKPOINT_BASE_MASK); //Set the new breakpoint!
					BIOS_Changed = 1; //We've changed!
				}
				unlock(LOCK_CPU); //Finished with the CPU!
			}
			break;
		case 10: //Breakpoint
		case 11: //Breakpoint
		case 12: //Breakpoint
		case 13: //Breakpoint
		case 14: //Breakpoint
			BPindex = (optioninfo[menuresult] - 10); //What index?
			if (Menu_Stat == BIOSMENU_STAT_OK) //Plain select?
			{
#ifndef IS_PSP
				//This option fails to compile on the PSP for some unknown reason.
				BIOS_Menu = 60; //Timeout to be used for breakpoints?
#endif
			}
			else if (Menu_Stat == BIOSMENU_STAT_SQUARE) //SQUARE=Set current address&mode as the breakpoint!
			{
				byte mode = 1;
				word segment;
				uint_32 offset;
				lock(LOCK_CPU); //Lock the CPU!
				switch (getcpumode()) //What mode are we?
				{
				case CPU_MODE_REAL: //Real mode?
					mode = 1; //Real mode!
					break;
				case CPU_MODE_PROTECTED: //Protected?
					mode = 2; //Protected mode!
					break;
				case CPU_MODE_8086: //Virtual 8086?
					mode = 3; //Virtual 8086 mode!
					break;
				default: //Unknown mode?
				case CPU_MODE_UNKNOWN: //Unknown?
					mode = 1; //Default to Real mode!
					break;
				}
				segment = REG_CS; //CS!
				offset = mode == 1 ? REG_IP : REG_EIP; //Our offset!
				BIOS_Settings.breakpoint[BPindex] = (((uint_64)mode & 3) << SETTINGS_BREAKPOINT_MODE_SHIFT) | (((uint_64)segment & SETTINGS_BREAKPOINT_SEGMENT_MASK) << SETTINGS_BREAKPOINT_SEGMENT_SHIFT) | ((uint_64)offset & SETTINGS_BREAKPOINT_OFFSET_MASK); //Set the new breakpoint!
				BIOS_Changed = 1; //We've changed!
				unlock(LOCK_CPU); //Finished with the CPU!
			}
			break;
		default:
			break;
		}
		break;
	default: //Unknown option?
		BIOS_Menu = NOTIMPLEMENTED; //Not implemented yet!
		break;
	}
}

void BIOS_CPU() //CPU menu!
{
	BIOS_Title("CPU Settings Menu");
	BIOS_InitCPUText(); //Init text!
	int menuresult = ExecuteMenu(advancedoptions, 4, BIOSMENU_SPEC_RETURN|BIOSMENU_SPEC_SQUAREOPTION, &Menu_Stat); //Show the menu options!
	switch (menuresult)
	{
	case BIOSMENU_SPEC_CANCEL: //R: Main menu?
		BIOS_Menu = 8; //Goto Advanced Menu!
		break;

	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
	case 9:
	case 10:
	case 11:
	case 12:
	case 13:
	case 14: //Valid option?
		switch (optioninfo[menuresult]) //What option has been chosen, since we are dynamic size?
		{
		//CPU settings
		case 0: //Installed CPU?
			if (Menu_Stat == BIOSMENU_STAT_OK) //Plain select?
			{
				if (!EMU_RUNNING) BIOS_Menu = 10; //Installed CPU selection!
			}
			break;
		case 1: //Emulated CPUs?
			if (Menu_Stat == BIOSMENU_STAT_OK) //Plain select?
			{
				if (!EMU_RUNNING) BIOS_Menu = 82; //Emulated CPUs selection!
			}
			break;
		case 2: //Data bus size?
			if (Menu_Stat == BIOSMENU_STAT_OK) //Plain select?
			{
				if (!EMU_RUNNING) BIOS_Menu = 40; //Data bus size!
			}
			break;
		case 3: //CPU speed?
			if (Menu_Stat == BIOSMENU_STAT_OK) //Plain select?
			{
				BIOS_Menu = 36; //CPU speed selection!
			}
			break;
		case 4: //Turbo CPU speed?
			if (Menu_Stat == BIOSMENU_STAT_OK) //Plain select?
			{
				BIOS_Menu = 55; //Turbo CPU speed selection!
			}
			break;
		case 5: //Use Turbo CPU Speed?
			if (Menu_Stat == BIOSMENU_STAT_OK) //Plain select?
			{
				BIOS_Menu = 56; //Turbo CPU speed selection!
			}
			break;
		case 6: //Clocking mode?
			if (Menu_Stat == BIOSMENU_STAT_OK) //Plain select?
			{
				if (!EMU_RUNNING) BIOS_Menu = 65; //Clocking mode selection!
			}
			break;
		case 7: //CPU speed display setting?
			if (Menu_Stat == BIOSMENU_STAT_OK) //Plain select?
			{
				BIOS_Menu = 41; //CPU speed display setting!
			}
			break;
		//Basic execution information
		case 8: //Boot order?
			if (Menu_Stat == BIOSMENU_STAT_OK) //Plain select?
			{
				BIOS_Menu = 9; //Boot Order Menu!
			}
			break;
		case 9:
			if (Menu_Stat == BIOSMENU_STAT_OK) //Plain select?
			{
				BIOS_Menu = 24; //Execution mode option!
			}
			break;
		//Debugger information
		case 10: //Architecture
			if (!EMU_RUNNING) BIOS_Menu = 34; //Architecture option!
			break;
		case 11: //BIOS ROM mode
			if (!EMU_RUNNING) BIOS_Menu = 62; //Architecture option!
			break;
		case 12: //Inboard Initial Waitstates?
			BIOS_Menu = 64; //Architecture option!
			break;
		case 13: //CPUID mode?
			if (Menu_Stat == BIOSMENU_STAT_OK) //Plain select?
			{
				BIOS_Menu = 83; //CPUID mode selection!
			}
			break;
		case 14: //Debug settings?
			if (Menu_Stat == BIOSMENU_STAT_OK) //Plain select?
			{
				BIOS_Menu = 94; //Debug settings option!
			}
			break;
		default:
			break;
		}
		break;
	default: //Unknown option?
		BIOS_Menu = NOTIMPLEMENTED; //Not implemented yet!
		break;
	}
}

int_64 GetCPUSpeed(byte x, byte y, uint_32 CPUSpeed) //Retrieve the size, or 0 for none!
{
	int key = 0;
	lock(LOCK_INPUT);
	key = psp_inputkeydelay(BIOS_INPUTDELAY);
	unlock(LOCK_INPUT);
	while ((key&(BUTTON_CONFIRM|BUTTON_START))>0) //Pressed? Wait for release!
	{
		lock(LOCK_INPUT);
		key = psp_inputkeydelay(BIOS_INPUTDELAY);
		unlock(LOCK_INPUT);
	}
	uint_32 result = CPUSpeed; //Size: result; default 0 for none! Must be a multiple of 4096 bytes for HDD!
	uint_32 oldvalue; //To check for high overflow!
	for (;;) //Get input; break on error!
	{
		EMU_locktext();
		EMU_textcolor(BIOS_ATTR_ACTIVE); //We're using active color for input!
		if (!result) //Default cycles?
		{
			GPU_EMU_printscreen(x, y, "Default cycles                                         ", result); //Show current size!
		}
		else
		{
			GPU_EMU_printscreen(x, y, "Limited to %u cycles", result); //Show current size!
		}
		EMU_unlocktext();
		lock(LOCK_INPUT);
		key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
		unlock(LOCK_INPUT);
		
		//1GB steps!
		if ((key & BUTTON_LTRIGGER)>0) //1000 step down?
		{
			if (result == 0) {}
			else
			{
				oldvalue = result; //Load the old value!
				result -= (key&BUTTON_RIGHT) ? 100000 : ((key&BUTTON_LEFT) ? 10000 : 1000); //x100 or x10 or x1!
				if (result>oldvalue) result = 0; //Underflow!
			}
		}
		else if ((key & BUTTON_RTRIGGER)>0) //1000 step up?
		{
			oldvalue = result; //Save the old value!
			result += (key&BUTTON_RIGHT) ? 100000 : ((key&BUTTON_LEFT) ? 10000 : 1000); //x100 or x10 or x1!
			if (result < oldvalue) result = oldvalue; //We've overflown?
		}
		else if ((key & BUTTON_DOWN)>0) //1 step up?
		{
			if (result == 0) {}
			else
			{
				oldvalue = result;
				result -= (key&BUTTON_RIGHT)?100:((key&BUTTON_LEFT)?10:1); //x100 or x10 or x1!
				if (result>oldvalue) result = 0; //Underflow!
			}
		}
		else if ((key & BUTTON_UP)>0) //1 step down?
		{
			oldvalue = result; //Save the old value!
			result += (key&BUTTON_RIGHT) ? 100 : ((key&BUTTON_LEFT) ? 10 : 1); //x100 or x10 or x1!
			if (result < oldvalue) result = oldvalue; //We've overflown?
		}
		//Confirmation buttons etc.
		else if ((key & (BUTTON_CONFIRM|BUTTON_START))>0)
		{
			while ((key&(BUTTON_CONFIRM|BUTTON_START))>0) //Wait for release!
			{
				lock(LOCK_INPUT);
				key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
				unlock(LOCK_INPUT);
			}
			return (int_64)result;
		}
		else if ((key & BUTTON_CANCEL)>0)
		{
			while ((key&BUTTON_CANCEL)>0) //Wait for release!
			{
				lock(LOCK_INPUT);
				key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
				unlock(LOCK_INPUT);
			}
			break; //Cancel!
		}
		else if ((key & BUTTON_TRIANGLE)>0)
		{
			while ((key&BUTTON_TRIANGLE)>0) //Wait for release!
			{
				lock(LOCK_INPUT);
				key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
				unlock(LOCK_INPUT);
			}
			return 0; //Default!
		}
		else if (shuttingdown()) break; //Cancel because of shutdown?
	}
	return FILELIST_CANCEL; //No size: cancel!
}

int_64 GetEmulatedCPUs(byte x, byte y, uint_32 EmulatedCPUs) //Retrieve the size, or 0 for none!
{
	int key = 0;
	lock(LOCK_INPUT);
	key = psp_inputkeydelay(BIOS_INPUTDELAY);
	unlock(LOCK_INPUT);
	while ((key & (BUTTON_CONFIRM | BUTTON_START)) > 0) //Pressed? Wait for release!
	{
		lock(LOCK_INPUT);
		key = psp_inputkeydelay(BIOS_INPUTDELAY);
		unlock(LOCK_INPUT);
	}
	uint_32 result = EmulatedCPUs; //Size: result; default 0 for none! Must be a multiple of 4096 bytes for HDD!
	uint_32 oldvalue; //To check for high overflow!
	for (;;) //Get input; break on error!
	{
		EMU_locktext();
		EMU_textcolor(BIOS_ATTR_ACTIVE); //We're using active color for input!
		if (!result) //Default cycles?
		{
			GPU_EMU_printscreen(x, y, "All available processors                               ", result); //Show current size!
		}
		else
		{
			GPU_EMU_printscreen(x, y, "%u processors", result); //Show current size!
		}
		EMU_unlocktext();
		lock(LOCK_INPUT);
		key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
		unlock(LOCK_INPUT);

		//1GB steps!
		if ((key & BUTTON_LTRIGGER) > 0) //1000 step down?
		{
			if (result == 0) {}
			else
			{
				oldvalue = result; //Load the old value!
				result -= (key & BUTTON_RIGHT) ? 100000 : ((key & BUTTON_LEFT) ? 10000 : 1000); //x100 or x10 or x1!
				if (result > oldvalue) result = 0; //Underflow!
			}
		}
		else if ((key & BUTTON_RTRIGGER) > 0) //1000 step up?
		{
			oldvalue = result; //Save the old value!
			result += (key & BUTTON_RIGHT) ? 100000 : ((key & BUTTON_LEFT) ? 10000 : 1000); //x100 or x10 or x1!
			if (result < oldvalue) result = oldvalue; //We've overflown?
		}
		else if ((key & BUTTON_DOWN) > 0) //1 step up?
		{
			if (result == 0) {}
			else
			{
				oldvalue = result;
				result -= (key & BUTTON_RIGHT) ? 100 : ((key & BUTTON_LEFT) ? 10 : 1); //x100 or x10 or x1!
				if (result > oldvalue) result = 0; //Underflow!
			}
		}
		else if ((key & BUTTON_UP) > 0) //1 step down?
		{
			oldvalue = result; //Save the old value!
			result += (key & BUTTON_RIGHT) ? 100 : ((key & BUTTON_LEFT) ? 10 : 1); //x100 or x10 or x1!
			if (result < oldvalue) result = oldvalue; //We've overflown?
		}
		//Confirmation buttons etc.
		else if ((key & (BUTTON_CONFIRM | BUTTON_START)) > 0)
		{
			while ((key & (BUTTON_CONFIRM | BUTTON_START)) > 0) //Wait for release!
			{
				lock(LOCK_INPUT);
				key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
				unlock(LOCK_INPUT);
			}
			return (int_64)result;
		}
		else if ((key & BUTTON_CANCEL) > 0)
		{
			while ((key & BUTTON_CANCEL) > 0) //Wait for release!
			{
				lock(LOCK_INPUT);
				key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
				unlock(LOCK_INPUT);
			}
			break; //Cancel!
		}
		else if ((key & BUTTON_TRIANGLE) > 0)
		{
			while ((key & BUTTON_TRIANGLE) > 0) //Wait for release!
			{
				lock(LOCK_INPUT);
				key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
				unlock(LOCK_INPUT);
			}
			return 0; //Default!
		}
		else if (shuttingdown()) break; //Cancel because of shutdown?
	}
	return FILELIST_CANCEL; //No size: cancel!
}

void BIOS_CPUSpeed() //CPU speed selection!
{
	BIOS_Title("CPU speed");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "CPU speed: "); //Show selection init!
	EMU_unlocktext();
	int_64 file = GetCPUSpeed(11, 4, *(getarchCPUSpeed())); //Show options for the CPU speed!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected speed!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = 0; //Default setting: Disabled!
	default: //Changed?
		if (file != *(getarchCPUSpeed())) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			*(getarchCPUSpeed()) = (uint_32)file; //Select CPU speed setting!
		}
		break;
	}
	BIOS_Menu = 35; //Goto CPU menu!
}

void BIOS_EmulatedCPUs() //How many emulated CPUs!
{
	BIOS_Title("Emulated CPUs");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Emulated CPUs: "); //Show selection init!
	EMU_unlocktext();
	int_64 file = GetEmulatedCPUs(15, 4, *(getarchemulated_CPUs())); //Show options for the CPU speed!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected speed!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = DEFAULT_CPUS; //Default setting: 1 processor!
	default: //Changed?
		if (file != *(getarchemulated_CPUs())) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			*(getarchemulated_CPUs()) = (uint_32)file; //Select CPU speed setting!
		}
		break;
	}
	BIOS_Menu = 35; //Goto CPU menu!
}

extern CMOS_Type CMOS; //The currently running CMOS!

void BIOS_ClearCMOS() //Clear the CMOS!
{
	byte emptycmos[128];
	memset(&emptycmos, 0, sizeof(emptycmos)); //Empty CMOS for comparision!
	if (is_i430fx==2) //i430fx?
	{
		if ((BIOS_Settings.got_i440fxCMOS) || (memcmp(&BIOS_Settings.i440fxCMOS, emptycmos, sizeof(emptycmos)) != 0)) //Gotten a CMOS?
		{
			BIOS_Changed = 1; //We've changed!
			reboot_needed |= 2; //We're needing a reboot!
		}
		else
		{
			return; //NOP!
		}
	}
	else if (is_i430fx==1) //i430fx?
	{
		if ((BIOS_Settings.got_i430fxCMOS) || (memcmp(&BIOS_Settings.i430fxCMOS, emptycmos, sizeof(emptycmos)) != 0)) //Gotten a CMOS?
		{
			BIOS_Changed = 1; //We've changed!
			reboot_needed |= 2; //We're needing a reboot!
		}
		else
		{
			return; //NOP!
		}
	}
	else if (is_PS2) //PS/2?
	{
		if ((BIOS_Settings.got_PS2CMOS) || (memcmp(&BIOS_Settings.PS2CMOS, emptycmos,sizeof(emptycmos)) != 0)) //Gotten a CMOS?
		{
			BIOS_Changed = 1; //We've changed!
			reboot_needed |= 2; //We're needing a reboot!
		}
		else
		{
			return; //NOP!
		}
	}
	else if (is_Compaq)
	{
		if ((BIOS_Settings.got_CompaqCMOS) || (memcmp(&BIOS_Settings.CompaqCMOS, emptycmos,sizeof(emptycmos)) != 0)) //Gotten a CMOS?
		{
			BIOS_Changed = 1; //We've changed!
			reboot_needed |= 2; //We're needing a reboot!
		}
		else
		{
			return; //NOP!
		}
	}
	else if (is_XT)
	{
		if ((BIOS_Settings.got_XTCMOS) || (memcmp(&BIOS_Settings.XTCMOS, emptycmos,sizeof(emptycmos)) != 0)) //Gotten a CMOS?
		{
			BIOS_Changed = 1; //We've changed!
			reboot_needed |= 2; //We're needing a reboot!
		}
		else
		{
			return; //NOP!
		}
	}
	else //AT?
	{
		if ((BIOS_Settings.got_ATCMOS) || (memcmp(&BIOS_Settings.ATCMOS, emptycmos,sizeof(emptycmos)) != 0)) //Gotten a CMOS?
		{
			BIOS_Changed = 1; //We've changed!
			reboot_needed |= 2; //We're needing a reboot!
		}
		else
		{
			return; //NOP!
		}
	}
	lock(LOCK_CPU); //Lock the CPU: we're going to change something in active emulation!
	CMOS.Loaded = 0; //Unload the CMOS: discard anything that's loaded when saving!
	memset(&CMOS.DATA,0,sizeof(CMOS.DATA)); //Clear the data!
	unlock(LOCK_CPU); //We're finished with the main thread!

	CMOSGLOBALBACKUPDATA backupglobal;
	if (is_i430fx==2) //i440fx?
	{
		backupCMOSglobalsettings(&BIOS_Settings.i440fxCMOS, &backupglobal); //Backup the memory field!
		memset(&BIOS_Settings.i440fxCMOS, 0, sizeof(BIOS_Settings.i440fxCMOS));
		restoreCMOSglobalsettings(&BIOS_Settings.i440fxCMOS, &backupglobal); //Backup the memory field!
		BIOS_Settings.got_i440fxCMOS = 0; //We haven't gotten a CMOS!
	}
	else if (is_i430fx==1) //i430fx?
	{
		backupCMOSglobalsettings(&BIOS_Settings.i430fxCMOS, &backupglobal); //Backup the memory field!
		memset(&BIOS_Settings.i430fxCMOS, 0, sizeof(BIOS_Settings.i430fxCMOS));
		restoreCMOSglobalsettings(&BIOS_Settings.i430fxCMOS, &backupglobal); //Backup the memory field!
		BIOS_Settings.got_i430fxCMOS = 0; //We haven't gotten a CMOS!
	}
	else if (is_PS2) //PS/2?
	{
		backupCMOSglobalsettings(&BIOS_Settings.PS2CMOS, &backupglobal); //Backup the memory field!
		memset(&BIOS_Settings.PS2CMOS, 0, sizeof(BIOS_Settings.PS2CMOS));
		restoreCMOSglobalsettings(&BIOS_Settings.PS2CMOS, &backupglobal); //Backup the memory field!
		BIOS_Settings.got_PS2CMOS = 0; //We haven't gotten a CMOS!
	}
	else if (is_Compaq)
	{
		backupCMOSglobalsettings(&BIOS_Settings.CompaqCMOS, &backupglobal); //Backup the memory field!
		memset(&BIOS_Settings.CompaqCMOS, 0, sizeof(BIOS_Settings.CompaqCMOS));
		restoreCMOSglobalsettings(&BIOS_Settings.CompaqCMOS, &backupglobal); //Backup the memory field!
		BIOS_Settings.got_CompaqCMOS = 0; //We haven't gotten a CMOS!
	}
	else if (is_XT)
	{
		backupCMOSglobalsettings(&BIOS_Settings.XTCMOS, &backupglobal); //Backup the memory field!
		memset(&BIOS_Settings.XTCMOS, 0, sizeof(BIOS_Settings.XTCMOS));
		restoreCMOSglobalsettings(&BIOS_Settings.XTCMOS, &backupglobal); //Backup the memory field!
		BIOS_Settings.got_XTCMOS = 0; //We haven't gotten a CMOS!
	}
	else //AT?
	{
		backupCMOSglobalsettings(&BIOS_Settings.ATCMOS, &backupglobal); //Backup the memory field!
		memset(&BIOS_Settings.ATCMOS, 0, sizeof(BIOS_Settings.ATCMOS));
		restoreCMOSglobalsettings(&BIOS_Settings.ATCMOS, &backupglobal); //Backup the memory field!
		BIOS_Settings.got_ATCMOS = 0; //We haven't gotten a CMOS!
	}
	BIOS_Menu = 8; //Goto Advanced Menu!
}

void BIOS_syncTime() //Synchronize the kept time in UniPCemu!
{
	lock(LOCK_CPU); //Lock the CPU: we're going to change something in active emulation!
	CMOS.DATA.timedivergeance = 0;
	CMOS.DATA.timedivergeance2 = 0; //No divergeance: we're current time exactly!
	CMOS.Loaded = 1; //We're loaded with new settings!
	unlock(LOCK_CPU); //We're finished with the main thread!
	BIOS_Menu = 8; //Goto Advanced Menu!
}

uint_32 GetPercentage(byte x, byte y, uint_32 Percentage) //Retrieve the size, or 0 for none!
{
	int key = 0;
	lock(LOCK_INPUT);
	key = psp_inputkeydelay(BIOS_INPUTDELAY);
	unlock(LOCK_INPUT);
	while ((key&(BUTTON_CONFIRM|BUTTON_START))>0) //Pressed? Wait for release!
	{
		lock(LOCK_INPUT);
		key = psp_inputkeydelay(BIOS_INPUTDELAY);
		unlock(LOCK_INPUT);
	}
	uint_32 result = Percentage; //Size: result; default 0 for none! Must be a multiple of 4096 bytes for HDD!
	uint_32 oldvalue; //To check for high overflow!
	for (;;) //Get input; break on error!
	{
		EMU_locktext();
		EMU_textcolor(BIOS_ATTR_ACTIVE); //We're using active color for input!
		GPU_EMU_printscreen(x, y, "%u%%                                                      ", result); //Show current percentage!
		EMU_unlocktext();
		lock(LOCK_INPUT);
		key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
		unlock(LOCK_INPUT);
		//1GB steps!
		if ((key & BUTTON_LTRIGGER)>0) //1000 step down?
		{
			if (result == 0) {}
			else
			{
				oldvalue = result; //Load the old value!
				result -= (key&BUTTON_RIGHT) ? 100000 : ((key&BUTTON_LEFT) ? 10000 : 1000); //x100 or x10 or x1!
				if (result>oldvalue) result = 0; //Underflow!
			}
		}
		else if ((key & BUTTON_RTRIGGER)>0) //1000 step up?
		{
			oldvalue = result; //Save the old value!
			result += (key&BUTTON_RIGHT) ? 100000 : ((key&BUTTON_LEFT) ? 10000 : 1000); //x100 or x10 or x1!
			if (result < oldvalue) result = oldvalue; //We've overflown?
		}
		else if ((key & BUTTON_DOWN)>0) //1 step up?
		{
			if (result == 0) {}
			else
			{
				oldvalue = result;
				result -= (key&BUTTON_RIGHT) ? 100 : ((key&BUTTON_LEFT) ? 10 : 1); //x100 or x10 or x1!
				if (result>oldvalue) result = 0; //Underflow!
			}
		}
		else if ((key & BUTTON_UP)>0) //1 step down?
		{
			oldvalue = result; //Save the old value!
			result += (key&BUTTON_RIGHT) ? 100 : ((key&BUTTON_LEFT) ? 10 : 1); //x100 or x10 or x1!
			if (result < oldvalue) result = oldvalue; //We've overflown?
		}
		//Confirmation buttons etc.
		else if ((key & (BUTTON_CONFIRM|BUTTON_START))>0)
		{
			while ((key&(BUTTON_CONFIRM|BUTTON_START))>0) //Wait for release!
			{
				lock(LOCK_INPUT);
				key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
				unlock(LOCK_INPUT);
			}
			return result; //Convert back to an ordinary factor!
		}
		else if ((key & BUTTON_CANCEL)>0)
		{
			while ((key&BUTTON_CANCEL)>0) //Wait for release!
			{
				lock(LOCK_INPUT);
				key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
				unlock(LOCK_INPUT);
			}
			break; //Cancel!
		}
		else if ((key & BUTTON_TRIANGLE)>0)
		{
			while ((key&BUTTON_TRIANGLE)>0) //Wait for release!
			{
				lock(LOCK_INPUT);
				key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
				unlock(LOCK_INPUT);
			}
			return 0; //Default!
		}
		else if (shuttingdown()) break; //Cancel because of shutdown?
	}
	return FILELIST_CANCEL; //No size: cancel!
}

void BIOS_SoundSourceVolume()
{
	BIOS_Title("Sound Source Volume");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Sound Source Volume: "); //Show selection init!
	EMU_unlocktext();
	uint_32 file = GetPercentage(21, 4, BIOS_Settings.SoundSource_Volume); //Show options for the installed CPU!
	switch ((int)file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected percentage!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = DEFAULT_SSOURCEVOL; //Default setting: Quiet!
	default: //Changed?
		if (file != BIOS_Settings.SoundSource_Volume) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			BIOS_Settings.SoundSource_Volume = file; //Select Sound Source Volume setting!
		}
		break;
	}
	BIOS_Menu = 31; //Goto Advanced menu!
}

void BIOS_ShowFramerate()
{
	BIOS_Settings.ShowFramerate = !BIOS_Settings.ShowFramerate; //Reverse!
	BIOS_Changed = 1; //We've changed!
	BIOS_Menu = 29; //Goto Video Settings menu!
}

void BIOS_DataBusSizeSetting()
{
	BIOS_Title("Data bus size");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Data bus size: "); //Show selection init!
	EMU_unlocktext();
	int i = 0; //Counter!
	numlist = 2; //Amount of Direct modes!
	for (i = 0; i<numlist; i++) //Process options!
	{
		cleardata(&itemlist[i][0], sizeof(itemlist[i])); //Reset!
	}
	safestrcpy(itemlist[0],sizeof(itemlist[0]), "Full sized data bus of 16/32-bits"); //Set filename from options!
	safestrcpy(itemlist[1],sizeof(itemlist[0]), "Reduced data bus size"); //Set filename from options!
	int current = 0;
	switch (*(getarchDataBusSize())) //What setting?
	{
	case 0: //Valid
	case 1: //Valid
		current = *(getarchDataBusSize()); //Valid: use!
		break;
	default: //Invalid
		current = 0; //Default: none!
		break;
	}
	if (*(getarchDataBusSize()) != current) //Invalid?
	{
		*(getarchDataBusSize()) = current; //Safety!
		BIOS_Changed = 1; //Changed!
	}
	int file = ExecuteList(15, 4, itemlist[current], 256, NULL,0); //Show options for the installed CPU!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = 0; //Default setting: Disabled!

	case 0:
	case 1:
	default: //Changed?
		if (file != current) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			reboot_needed |= 1; //A reboot is needed!
			*(getarchDataBusSize()) = file; //Select Data bus size setting!
		}
		break;
	}
	BIOS_Menu = 35; //Goto CPU menu!
}

void BIOS_ShowCPUSpeed()
{
	BIOS_Settings.ShowCPUSpeed = !BIOS_Settings.ShowCPUSpeed; //Reverse!
	BIOS_Changed = 1; //We've changed!
	BIOS_Menu = 35; //Goto CPU menu!
}

void BIOS_SoundStartStopRecording()
{
	lock(LOCK_MAINTHREAD); //Make sure we're not doing anything!
	if (sound_isRecording()) //Are we recording?
	{
		sound_stopRecording(); //Stop recording!
		#ifdef SOUND_TEST
		PORT_OUT_B(0x330,0xB0); //Control change!
		PORT_OUT_B(0x330,0x7B); //All notes off!
		PORT_OUT_B(0x330,0x00); //Nothing more!

		PORT_OUT_B(0x331,0xFF); //Reset ourselves!
		PORT_OUT_B(0x330,0xFF); //Reset ourselves!
		#endif
	}
	else
	{
		sound_startRecording(); //Start recording!
		#ifdef SOUND_TEST
		PORT_OUT_B(0x331,0xFF); //Reset ourselves!
		PORT_OUT_B(0x330,0xFF); //Reset ourselves!

		PORT_OUT_B(0x330,0xB0); //Control change!
		PORT_OUT_B(0x330,0x00); //Bank change MSB!
		PORT_OUT_B(0x330,0x00); //Bank change 0!
		PORT_OUT_B(0x330,0x20); //Bank change LSB!
		PORT_OUT_B(0x330,0x00); //Bank change 0!

		PORT_OUT_B(0x330,0xC0); //Instrument change!
		PORT_OUT_B(0x330,73); //Instrument change to flute!

		PORT_OUT_B(0x330,0x90); //Start note!
		PORT_OUT_B(0x330,0x60); //Central C+32 tones!
		PORT_OUT_B(0x330,0x40); //At default volume!
		#endif
	}
	unlock(LOCK_MAINTHREAD); //Make sure we're not doing anything!
	BIOS_Menu = 31; //Goto Sound menu!
}


extern FLOPPY_GEOMETRY floppygeometries[NUMFLOPPYGEOMETRIES]; //All possible floppy geometries to create!

void BIOS_GenerateFloppyDisk()
{
	char fullfilename[256];
	word size; //The size to generate, in KB!
	byte i;
	char filename[256]; //Filename container!
	cleardata(&filename[0], sizeof(filename)); //Init!
	for (i=0;i<NUMFLOPPYGEOMETRIES;i++) //Process all geometries into a list!
	{
		memset(&itemlist[i],0,sizeof(itemlist[i])); //Reset!
		strcpy(itemlist[i], floppygeometries[i].text); 
	}
	numlist = NUMFLOPPYGEOMETRIES; //The size of the list!

	BIOS_Title("Generate floppy image");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Floppy image size: "); //Show selection init!
	EMU_unlocktext();
	int result;
	result = ExecuteList(19,4,itemlist[0],256,NULL,0); //Get our result!
	if ((result>=0) && (result<NUMFLOPPYGEOMETRIES)) //Valid item?
	{
		EMU_locktext();
		EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
		EMU_gotoxy(0, 4); //Goto position for info!
		GPU_EMU_printscreen(0, 5, "Name: "); //Show the filename!
		EMU_unlocktext();
		if (BIOS_InputText(6, 5, &filename[0], 255-4, 0)) //Input text confirmed?
		{
			if (strcmp(filename, "") != 0) //Got input?
			{
				if (safestrlen(filename,sizeof(filename)) <= (255 - 4)) //Not too long?
				{
					safestrcat(filename,sizeof(filename), ".img"); //Add the extension!
					EMU_locktext();
					EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
					EMU_gotoxy(0, 5); //Goto position for info!
					GPU_EMU_printscreen(0, 5, "Filename: %s", filename); //Show the filename!
					EMU_gotoxy(0, 5); //Next row!
					GPU_EMU_printscreen(0, 6, "Image size: "); //Show image size selector!!
					EMU_unlocktext();
					size = floppygeometries[result].KB; //The size of the floppy in KB!
					if (size != 0) //Got size?
					{
						EMU_locktext();
						EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
						GPU_EMU_printscreen(12, 6, "%s", itemlist[result]); //Show size we selected!
						EMU_gotoxy(0, 6); //Next row!
						GPU_EMU_printscreen(0, 7, "Generating image: "); //Start of percentage!
						EMU_unlocktext();

						memset(&fullfilename, 0, sizeof(fullfilename));
						safestrcpy(fullfilename,sizeof(fullfilename), diskpath);
						safestrcat(fullfilename,sizeof(fullfilename), "/");
						safestrcat(fullfilename,sizeof(fullfilename), filename);

						generateFloppyImage(filename, &floppygeometries[result], 18, 7); //Generate a floppy image according to geometry data!
						//Check for disk changes on mounted floppy disks (we might be getting a new size, when we're recreaten)!
						if (!strcmp(filename, BIOS_Settings.hdd0) || !strcmp(filename, BIOS_Settings.hdd1)) //Harddisk changed?
						{
							BIOS_Changed = 1; //We've changed!
							reboot_needed |= 2; //We're in need of a reboot!
						}
						if (!strcmp(disks[FLOPPY0].rawfilename,filename)) //Floppy #0 changed?
						{
							iofloppy0("",0,BIOS_Settings.floppy0_readonly,0); //Unmount!
							iofloppy0(BIOS_Settings.floppy0,0,BIOS_Settings.floppy0_readonly,0); //Remount to update!
						}
						if (!strcmp(disks[FLOPPY1].rawfilename,filename)) //Floppy #1 changed?
						{
							iofloppy1("",0,BIOS_Settings.floppy1_readonly,0); //Unmount!
							iofloppy1(BIOS_Settings.floppy1,0,BIOS_Settings.floppy1_readonly,0); //Remount to update!
						}
					}
				}
				//If we're too long, ignore it!
			}
		}
	}
	BIOS_Menu = 1; //Return to Disk Menu!
}

void BIOS_GenerateIMDFloppyDisk()
{
	char fullfilename[256];
	word size; //The size to generate, in KB!
	byte i;
	char filename[256]; //Filename container!
	cleardata(&filename[0], sizeof(filename)); //Init!
	for (i = 0; i < NUMFLOPPYGEOMETRIES; i++) //Process all geometries into a list!
	{
		memset(&itemlist[i], 0, sizeof(itemlist[i])); //Reset!
		strcpy(itemlist[i], floppygeometries[i].text);
	}
	numlist = NUMFLOPPYGEOMETRIES; //The size of the list!

	BIOS_Title("Generate IMD floppy image");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Floppy image size: "); //Show selection init!
	EMU_unlocktext();
	int result;
	result = ExecuteList(19, 4, itemlist[0], 256, NULL,0); //Get our result!
	if ((result >= 0) && (result < NUMFLOPPYGEOMETRIES)) //Valid item?
	{
		EMU_locktext();
		EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
		EMU_gotoxy(0, 4); //Goto position for info!
		GPU_EMU_printscreen(0, 5, "Name: "); //Show the filename!
		EMU_unlocktext();
		if (BIOS_InputText(6, 5, &filename[0], 255 - 4, 0)) //Input text confirmed?
		{
			if (strcmp(filename, "") != 0) //Got input?
			{
				if (safestrlen(filename, sizeof(filename)) <= (255 - 4)) //Not too long?
				{
					safestrcat(filename, sizeof(filename), ".imd"); //Add the extension!
					EMU_locktext();
					EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
					EMU_gotoxy(0, 5); //Goto position for info!
					GPU_EMU_printscreen(0, 5, "Filename: %s", filename); //Show the filename!
					EMU_gotoxy(0, 5); //Next row!
					GPU_EMU_printscreen(0, 6, "Image size: "); //Show image size selector!!
					EMU_unlocktext();
					size = floppygeometries[result].KB; //The size of the floppy in KB!
					if (size != 0) //Got size?
					{
						EMU_locktext();
						GPU_EMU_printscreen(12, 6, "%s", itemlist[result]); //Show size we selected!
						EMU_gotoxy(0, 6); //Next row!
						GPU_EMU_printscreen(0, 7, "Generating image: "); //Start of percentage!
						EMU_unlocktext();

						memset(&fullfilename, 0, sizeof(fullfilename));
						safestrcpy(fullfilename, sizeof(fullfilename), diskpath);
						safestrcat(fullfilename, sizeof(fullfilename), "/");
						safestrcat(fullfilename, sizeof(fullfilename), filename);

						i = generateIMDImage(filename, floppygeometries[result].tracks, floppygeometries[result].sides, (floppygeometries[result].DoubleDensity ? FORMATTING_MFM : FORMATTING_FM), FORMAT_SPEED_500, 18, 7); //Generate a floppy image according to geometry data!
						//Check for disk changes on mounted floppy disks (we might be getting a new size, when we're recreaten)!
						if (!strcmp(disks[FLOPPY0].rawfilename, filename)) //Floppy #0 changed?
						{
							iofloppy0("", 0, BIOS_Settings.floppy0_readonly, 0); //Unmount!
							iofloppy0(BIOS_Settings.floppy0, 0, BIOS_Settings.floppy0_readonly, 0); //Remount to update!
						}
						if (!strcmp(disks[FLOPPY1].rawfilename, filename)) //Floppy #1 changed?
						{
							iofloppy1("", 0, BIOS_Settings.floppy1_readonly, 0); //Unmount!
							iofloppy1(BIOS_Settings.floppy1, 0, BIOS_Settings.floppy1_readonly, 0); //Remount to update!
						}
					}
				}
				//If we're too long, ignore it!
			}
		}
	}
	BIOS_Menu = 1; //Return to Disk Menu!
}


void BIOS_usePCSpeaker()
{
	BIOS_Settings.usePCSpeaker = !BIOS_Settings.usePCSpeaker; //Reverse!
	BIOS_Changed = 1; //We've changed!
	reboot_needed |= 1; //A reboot is needed!
	BIOS_Menu = 31; //Goto Sound menu!
}

void BIOS_useAdlib()
{
	BIOS_Settings.useAdlib = !BIOS_Settings.useAdlib; //Reverse!
	BIOS_Changed = 1; //We've changed!
	reboot_needed |= 1; //A reboot is needed!
	BIOS_Menu = 31; //Goto Sound menu!
}

void BIOS_useLPTDAC()
{
	BIOS_Settings.useLPTDAC = !BIOS_Settings.useLPTDAC; //Reverse!
	BIOS_Changed = 1; //We've changed!
	reboot_needed |= 1; //A reboot is needed!
	BIOS_Menu = 31; //Goto Sound menu!
}

void BIOS_VGASynchronization()
{
	BIOS_Title("VGA Synchronization");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "VGA Synchronization: "); //Show selection init!
	EMU_unlocktext();
	int i = 0; //Counter!
	numlist = 3; //Amount of Synchronization modes!
	for (i = 0; i<numlist; i++) //Process options!
	{
		cleardata(&itemlist[i][0], sizeof(itemlist[i])); //Reset!
	}
	safestrcpy(itemlist[0],sizeof(itemlist[0]), "Old synchronization depending on host"); //Set filename from options!
	safestrcpy(itemlist[1],sizeof(itemlist[0]), "Synchronize depending on host"); //Set filename from options!
	safestrcpy(itemlist[2],sizeof(itemlist[0]), "Full CPU synchronization"); //Set filename from options!
	int current = 0;
	switch (BIOS_Settings.VGASynchronization) //What setting?
	{
	case 0: //Valid
	case 1: //Valid
	case 2: //Valid
		current = BIOS_Settings.VGASynchronization; //Valid: use!
		break;
	default: //Invalid
		current = DEFAULT_VGASYNCHRONIZATION; //Default: none!
		break;
	}
	if (BIOS_Settings.VGASynchronization != current) //Invalid?
	{
		BIOS_Settings.VGASynchronization = current; //Safety!
		BIOS_Changed = 1; //Changed!
	}
	int file = ExecuteList(21, 4, itemlist[current], 256, NULL,0); //Show options for the installed CPU!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = DEFAULT_VGASYNCHRONIZATION; //Default setting: Disabled!

	case 0:
	case 1:
	case 2:
	default: //Changed?
		if (file != current) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			BIOS_Settings.VGASynchronization = file; //Select Data bus size setting!
		}
		break;
	}
	BIOS_Menu = 29; //Goto Video menu!
}

extern char capturepath[256]; //Capture path!

void BIOS_DumpVGA()
{
	char fullfilename[256];
	cleardata(&fullfilename[0], sizeof(fullfilename)); //Init!

	BIGFILE *f;
	int DACIndex;
	uint_32 DACPos;
	BIOS_Title("Dumping VGA data");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Dumping VGA..."); //Show selection init!
	EMU_unlocktext();

	//Now dump the VGA data...
	VGA_Type *VGA = getActiveVGA(); //Get the current VGA!
	if (VGA) //Valid VGA?
	{
		domkdir(capturepath); //Make sure to create the directory we need!
		safestrcpy(fullfilename,sizeof(fullfilename), capturepath); //Disk path!
		safestrcat(fullfilename,sizeof(fullfilename), "/");
		safestrcat(fullfilename,sizeof(fullfilename), "vga_vram.dat"); //The full filename!
		f = emufopen64(fullfilename,"wb");
		if (f)
		{
			emufwrite64(VGA->VRAM,1,VGA->VRAM_size,f); //Write the VRAM to the file!
			emufclose64(f); //We've written the VRAM to the file!
		}
		safestrcpy(fullfilename,sizeof(fullfilename), capturepath); //Disk path!
		safestrcat(fullfilename,sizeof(fullfilename), "/");
		safestrcat(fullfilename,sizeof(fullfilename), "vga_graphregs.dat"); //The full filename!
		f = emufopen64(fullfilename,"wb");
		if (f)
		{
			emufwrite64(&VGA->registers->GraphicsRegisters.DATA,1,sizeof(VGA->registers->GraphicsRegisters.DATA),f);
			emufclose64(f); //We've written the Graphics Registers to the file!
		}
		safestrcpy(fullfilename,sizeof(fullfilename), capturepath); //Disk path!
		safestrcat(fullfilename,sizeof(fullfilename), "/");
		safestrcat(fullfilename,sizeof(fullfilename), "vga_seqregs.dat"); //The full filename!
		f = emufopen64(fullfilename, "wb");
		if (f)
		{
			emufwrite64(&VGA->registers->SequencerRegisters.DATA,1,sizeof(VGA->registers->SequencerRegisters.DATA),f);
			emufclose64(f); //We've written the Sequencer Registers to the file!
		}

		safestrcpy(fullfilename,sizeof(fullfilename), capturepath); //Disk path!
		safestrcat(fullfilename,sizeof(fullfilename), "/");
		safestrcat(fullfilename,sizeof(fullfilename), "vga_attrregs.dat"); //The full filename!
		f = emufopen64(fullfilename, "wb");
		if (f)
		{
			emufwrite64(&VGA->registers->AttributeControllerRegisters.DATA,1,sizeof(VGA->registers->AttributeControllerRegisters.DATA),f);
			emufclose64(f); //We've written the Attribute Controller Registers to the file!
		}

		safestrcpy(fullfilename,sizeof(fullfilename), capturepath); //Disk path!
		safestrcat(fullfilename,sizeof(fullfilename), "/");
		safestrcat(fullfilename,sizeof(fullfilename), "vga_crtcregs.dat"); //The full filename!
		f = emufopen64(fullfilename, "wb");
		if (f)
		{
			emufwrite64(&VGA->registers->CRTControllerRegisters.DATA,1,sizeof(VGA->registers->CRTControllerRegisters.DATA),f);
			emufclose64(f); //We've written the Graphics Registers to the file!
		}

		safestrcpy(fullfilename,sizeof(fullfilename), capturepath); //Disk path!
		safestrcat(fullfilename,sizeof(fullfilename), "/");
		safestrcat(fullfilename,sizeof(fullfilename), "vga_dacregs.dat"); //The full filename!
		f = emufopen64(fullfilename, "wb");
		if (f)
		{
			DACPos = 0; //Start with the first entry!
			for (DACIndex=0;DACIndex<0x100;DACIndex++) //Process DAC entries!
			{
				emufwrite64(&VGA->registers->DAC[DACPos++],1,1,f); //Write the DAC R!
				emufwrite64(&VGA->registers->DAC[DACPos++],1,1,f); //Write the DAC G!
				emufwrite64(&VGA->registers->DAC[DACPos++],1,1,f); //Write the DAC B!
				++DACPos; //Skip the DAC entry for the fourth entry: we're unused!
			}
			emufwrite64(&VGA->registers->DACMaskRegister,1,1,f); //Finish with the DAC mask register!
			emufclose64(f); //We've written the Graphics Registers to the file!
		}
		
		safestrcpy(fullfilename,sizeof(fullfilename), capturepath); //Disk path!
		safestrcat(fullfilename,sizeof(fullfilename), "/");
		safestrcat(fullfilename,sizeof(fullfilename), "vga_colorregs.dat"); //The full filename!
		f = emufopen64(fullfilename, "wb");
		emufwrite64(&VGA->registers->ColorRegisters,1,sizeof(VGA->registers->ColorRegisters),f); //Literal color registers!
		emufclose64(f);

		safestrcpy(fullfilename,sizeof(fullfilename), capturepath); //Disk path!
		safestrcat(fullfilename,sizeof(fullfilename), "/");
		safestrcat(fullfilename,sizeof(fullfilename), "vga_externalregs.dat"); //The full filename!
		f = emufopen64(fullfilename, "wb");
		emufwrite64(&VGA->registers->ExternalRegisters,1,sizeof(VGA->registers->ExternalRegisters),f); //Literal color registers!
		emufclose64(f);

		if (VGA->registers->specialCGAflags&1) //CGA compatiblity enabled?
		{
			safestrcpy(fullfilename,sizeof(fullfilename), capturepath); //Disk path!
			safestrcat(fullfilename,sizeof(fullfilename), "/");
			safestrcat(fullfilename,sizeof(fullfilename), "vga_cgamdacrtcregs.dat"); //The full filename!
			f = emufopen64(fullfilename, "wb");
			emufwrite64(&VGA->registers->CGARegisters,1,sizeof(VGA->registers->CGARegisters),f); //CGA CRTC registers!
			emufclose64(f);

			safestrcpy(fullfilename,sizeof(fullfilename), capturepath); //Disk path!
			safestrcat(fullfilename,sizeof(fullfilename), "/");
			safestrcat(fullfilename,sizeof(fullfilename), "vga_cgamodecontrol.dat"); //The full filename!
			f = emufopen64(fullfilename, "wb");
			emufwrite64(&VGA->registers->Compatibility_CGAModeControl,1,1,f); //CGA mode control register!
			emufclose64(f);
			
			safestrcpy(fullfilename,sizeof(fullfilename), capturepath); //Disk path!
			safestrcat(fullfilename,sizeof(fullfilename), "/");
			safestrcat(fullfilename,sizeof(fullfilename), "vga_cgapaletteregister.dat"); //The full filename!
			f = emufopen64(fullfilename, "wb");
			emufwrite64(&VGA->registers->Compatibility_CGAPaletteRegister,1,1,f); //CGA mode control register!
			emufclose64(f);

			safestrcpy(fullfilename,sizeof(fullfilename), capturepath); //Disk path!
			safestrcat(fullfilename,sizeof(fullfilename), "/");
			safestrcat(fullfilename,sizeof(fullfilename), "vga_mdamodecontrol.dat"); //The full filename!
			f = emufopen64(fullfilename, "wb");
			emufwrite64(&VGA->registers->Compatibility_MDAModeControl,1,1,f); //MDA mode control register!
			emufclose64(f);
		}
		else //Clean up CGA compatiblity register dumps: we're not supposed to be used!
		{
			delete_file(capturepath,"vga_cgamdacrtcregs.dat");
			delete_file(capturepath,"vga_cgamodecontrol.dat");
			delete_file(capturepath,"vga_cgapaletteregister.dat");
			delete_file(capturepath,"vga_mdamodecontrol.dat");
		}

		if (VGA->enable_SVGA) //SVGA emulated?
		{
			switch (VGA->enable_SVGA) //What SVGA is emulated?
			{
			case 1: //ET4000?
				safestrcpy(fullfilename,sizeof(fullfilename), capturepath); //Disk path!
				safestrcat(fullfilename,sizeof(fullfilename), "/");
				safestrcat(fullfilename,sizeof(fullfilename), "vga_et4000.dat"); //The full filename!
				f = emufopen64(fullfilename, "wb");
				emufwrite64(&et34k(VGA)->store_et4k_3d4_31,1,1,f); //Register 31h!
				emufwrite64(&et34k(VGA)->store_et4k_3d4_32,1,1,f); //Register 32h!
				emufwrite64(&et34k(VGA)->store_et4k_3d4_33,1,1,f); //Register 33h!
				emufwrite64(&et34k(VGA)->store_et4k_3d4_34,1,1,f); //Register 34h!
				emufwrite64(&et34k(VGA)->store_et4k_3d4_35,1,1,f); //Register 35h!
				emufwrite64(&et34k(VGA)->store_et4k_3d4_36,1,1,f); //Register 36h!
				emufwrite64(&et34k(VGA)->store_et4k_3d4_37,1,1,f); //Register 37h!
				emufwrite64(&et34k(VGA)->store_et4k_3d4_3f,1,1,f); //Register 3fh!
				break;
			case 2: //ET3000?
				safestrcpy(fullfilename,sizeof(fullfilename), capturepath); //Disk path!
				safestrcat(fullfilename,sizeof(fullfilename), "/");
				safestrcat(fullfilename,sizeof(fullfilename), "vga_et3000.dat"); //The full filename!
				f = emufopen64(fullfilename, "wb");
				emufwrite64(&et34k(VGA)->store_et3k_3d4_1b,1,1,f); //Register 1bh!
				emufwrite64(&et34k(VGA)->store_et3k_3d4_1c,1,1,f); //Register 1ch!
				emufwrite64(&et34k(VGA)->store_et3k_3d4_1d,1,1,f); //Register 1dh!
				emufwrite64(&et34k(VGA)->store_et3k_3d4_1e,1,1,f); //Register 1eh!
				emufwrite64(&et34k(VGA)->store_et3k_3d4_1f,1,1,f); //Register 1fh!
				emufwrite64(&et34k(VGA)->store_et3k_3d4_20,1,1,f); //Register 20h!
				emufwrite64(&et34k(VGA)->store_et3k_3d4_21,1,1,f); //Register 21h!
				emufwrite64(&et34k(VGA)->store_et3k_3d4_23,1,1,f); //Register 23h!
				emufwrite64(&et34k(VGA)->store_et3k_3d4_24,1,1,f); //Register 24h!
				emufwrite64(&et34k(VGA)->store_et3k_3d4_25,1,1,f); //Register 25h!
				break;
			default: //Unknown SVGA?
				goto cleanSVGAdumps;
			}
			//General register and data shared by the SVGA cards!
			emufwrite64(&et34k(VGA)->store_3c0_16,1,1,f); //Register 16h!
			emufwrite64(&et34k(VGA)->store_3c0_17,1,1,f); //Register 17h!
			emufwrite64(&et34k(VGA)->store_3c4_06,1,1,f); //Register 06h!
			emufwrite64(&et34k(VGA)->store_3c4_07,1,1,f); //Register 07h!
			emufwrite64(&et34k(VGA)->herculescompatibilitymode,1,1,f); //Hercules Compatibility Mode Register!
			emufwrite64(&et34k(VGA)->segmentselectregister,1,1,f); //Segment Select Register!
			emufwrite64(&et34k(VGA)->hicolorDACcommand,1,1,f); //Hi-color DAC register!
			emufwrite64(&et34k(VGA)->CGAModeRegister,1,1,f); //CGA Mode Control Compatibility register!
			emufwrite64(&et34k(VGA)->MDAModeRegister,1,1,f); //MDA Mode Control Compatibility register!
			emufwrite64(&et34k(VGA)->CGAColorSelectRegister,1,1,f); //CGA Color Select Compatiblity register!
			emufwrite64(&et34k(VGA)->ExtendedFeatureControlRegister,1,1,f); //Extended Feature Control register!
			emufclose64(f);
		}
		else //Clean up SVGA dumps?
		{
			cleanSVGAdumps:
				delete_file(capturepath,"vga_et4000.dat"); //ET4000 dump!
				delete_file(capturepath,"vga_et3000.dat"); //ET3000 dump!
		}

		VGA_DUMPColors(); //Dump all colors!
		dumpVGATextFonts(); //Dump all fonts used!
		dump_CRTCTiming(); //Dump all CRTC timing currently in use!
		VGA_LOGCRTCSTATUS(); //Dump CRTC status itself!

	#ifdef DUMP_VGATEST256COL
		uint_32 rowwidth = (getActiveVGA()->precalcs.rowsize<<2); //The row width, in bytes and pixels!
		uint_32 activewidth = (getActiveVGA()->precalcs.horizontaldisplayend-getActiveVGA()->precalcs.horizontaldisplaystart); //Width of the active display!
		uint_32 activeheight = getActiveVGA()->precalcs.verticaldisplayend; //The height of the active display!
		activewidth = MAX(rowwidth,activewidth); //Take the bigger one, if any!
		uint_32 *pixels = (uint_32 *)zalloc(((activeheight*activewidth)<<2),"BMPDATA",NULL); //To draw our bitmap on!
		int x,y;
		for (y = 0;y<activeheight;) //Vertical active display!
		{
			for (x = 0;x < activewidth;) //Horizontal active display!
			{
				pixels[(y*activewidth)+x] = getActiveVGA()->precalcs.effectiveDAC[getActiveVGA()->VRAM[((y*rowwidth)+x)&(getActiveVGA()->VRAM_size-1)]]; //Linear VRAM assumed, converted through the DAC to a color!
				++x; //Next pixel!
			}
			++y; //Next row!
		}
		writeBMP("captures/VGA256col",pixels,activewidth,activeheight,0,0,activewidth); //Dump the VRAM direct to test!
		freez((void **)&pixels, ((activeheight*activewidth) << 2),"BMPDATA"); //Release the temporary data!
	#endif
	}

	BIOS_Menu = 29; //Goto Video menu!
}

void BIOS_CGAModel()
{
	BIOS_Title("CGA Model");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "CGA Model: "); //Show selection init!
	EMU_unlocktext();
	int i = 0; //Counter!
	numlist = 4; //Amount of CGA Models!
	for (i = 0; i<numlist; i++) //Process options!
	{
		cleardata(&itemlist[i][0], sizeof(itemlist[i])); //Reset!
	}
	safestrcpy(itemlist[0],sizeof(itemlist[0]), "Old-style RGB"); //Old-style RGB!
	safestrcpy(itemlist[1],sizeof(itemlist[0]), "Old-style NTSC"); //Old-style NTSC!
	safestrcpy(itemlist[2],sizeof(itemlist[0]), "New-style RGB"); //New-style RGB!
	safestrcpy(itemlist[3],sizeof(itemlist[0]), "New-style NTSC"); //New-style NTSC!
	int current = 0;
	switch (BIOS_Settings.CGAModel) //What setting?
	{
	case 0: //Valid
	case 1: //Valid
	case 2: //Valid
	case 3: //Valid
		current = BIOS_Settings.CGAModel; //Valid: use!
		break;
	default: //Invalid
		current = 0; //Default to the first option!
		break;
	}
	if (BIOS_Settings.CGAModel != current) //Invalid?
	{
		BIOS_Settings.CGAModel = current; //Safety!
		BIOS_Changed = 1; //Changed!
	}
	int file = ExecuteList(11, 4, itemlist[current], 256, NULL,0); //Show options for the installed CPU!
	switch (file) //Which file?
	{
	default: //Unknown result?
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = DEFAULT_CGAMODEL; //Default setting!

	case 0:
	case 1:
	case 2:
	case 3:
		if (file != current) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			BIOS_Settings.CGAModel = file; //Select Data bus size setting!
		}
		break;
	}
	BIOS_Menu = 29; //Goto Video menu!
}

void BIOS_gamingmodeJoystick()
{
	BIOS_Title("Gaming mode");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Gaming mode: "); //Show selection init!
	EMU_unlocktext();
	int i = 0; //Counter!
	numlist = 6; //Amount of Joysticks supported plus Gaming mode mapping!
	for (i = 0; i<numlist; i++) //Process options!
	{
		cleardata(&itemlist[i][0], sizeof(itemlist[i])); //Reset!
	}
	safestrcpy(itemlist[0],sizeof(itemlist[0]), "Normal gaming mode mapped input"); //Default to mapped input!
	safestrcpy(itemlist[1],sizeof(itemlist[0]), "Joystick, Cross=Button 1, Circle=Button 2"); //Joystick: Cross=Button 1, Circle=Button 2!
	safestrcpy(itemlist[2],sizeof(itemlist[0]), "Joystick, Cross=Button 2, Circle=Button 1"); //Joystick: Cross=Button 2, Circle=Button 1!
	safestrcpy(itemlist[3],sizeof(itemlist[0]), "Joystick, Gravis Gamepad"); //Gravis Gamepad!
	safestrcpy(itemlist[4],sizeof(itemlist[0]), "Joystick, Gravis Analog Pro"); //Gravis Analog Pro!
	safestrcpy(itemlist[5],sizeof(itemlist[0]), "Joystick, Logitech WingMan Extreme Digital"); //Logitech WingMan Extreme Digital!
	int current = 0;
	switch (BIOS_Settings.input_settings.gamingmode_joystick) //What setting?
	{
	case 0: //Valid
	case 1: //Valid
	case 2: //Valid
	case 3: //Valid
	case 4: //Valid
	case 5: //Valid
		current = BIOS_Settings.input_settings.gamingmode_joystick; //Valid: use!
		break;
	default: //Invalid
		current = 0; //Default to the first option!
		break;
	}
	if (BIOS_Settings.input_settings.gamingmode_joystick != current) //Invalid?
	{
		BIOS_Settings.input_settings.gamingmode_joystick = current; //Safety!
		BIOS_Changed = 1; //Changed!
	}
	int file = ExecuteList(13, 4, itemlist[current], 256, NULL,0); //Show options for the installed CPU!
	switch (file) //Which file?
	{
	default: //Unknown result?
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = 0; //Default setting!

	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
		if (file != current) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			BIOS_Settings.input_settings.gamingmode_joystick = file; //Select Gaming mode Joystick setting!
		}
		break;
	}
	BIOS_Menu = 25; //Goto Input menu!
}

void BIOS_JoystickReconnect()
{
	reconnectJoystick0(); //Reconnect joystick #0!
	BIOS_Menu = 25; //Goto Input menu!
}

void BIOS_useGameBlaster()
{
	BIOS_Settings.useGameBlaster = !BIOS_Settings.useGameBlaster; //Reverse!
	BIOS_Changed = 1; //We've changed!
	reboot_needed |= 1; //A reboot is needed!
	BIOS_Menu = 31; //Goto Sound menu!
}

void BIOS_GameBlasterVolume()
{
	BIOS_Title("Game Blaster Volume");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Game Blaster Volume: "); //Show selection init!
	EMU_unlocktext();
	uint_32 file = GetPercentage(21, 4, BIOS_Settings.GameBlaster_Volume); //Show options for the installed CPU!
	switch ((int)file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected percentage!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = DEFAULT_BLASTERVOL; //Default setting: Quiet!
	default: //Changed?
		if (file != BIOS_Settings.GameBlaster_Volume) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			BIOS_Settings.GameBlaster_Volume = file; //Select Sound Source Volume setting!
		}
		break;
	}
	BIOS_Menu = 31; //Goto Sound menu!
}

void BIOS_useSoundBlaster()
{
	BIOS_Title("Sound Blaster");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Sound Blaster: "); //Show selection init!
	EMU_unlocktext();
	int i = 0; //Counter!
	numlist = 3; //Amount of Synchronization modes!
	for (i = 0; i<numlist; i++) //Process options!
	{
		cleardata(&itemlist[i][0], sizeof(itemlist[i])); //Reset!
	}
	safestrcpy(itemlist[0],sizeof(itemlist[0]), "Disabled"); //Set filename from options!
	safestrcpy(itemlist[1],sizeof(itemlist[0]), "Version 1.0(with Game Blaster) or 1.5(without Game Blaster)"); //Set filename from options!
	safestrcpy(itemlist[2],sizeof(itemlist[0]), "Version 2.0"); //Set filename from options!
	int current = 0;
	switch (BIOS_Settings.useSoundBlaster) //What setting?
	{
	case 0: //Valid
	case 1: //Valid
	case 2: //Valid
		current = BIOS_Settings.useSoundBlaster; //Valid: use!
		break;
	default: //Invalid
		current = 0; //Default: none!
		break;
	}
	if (BIOS_Settings.useSoundBlaster != current) //Invalid?
	{
		BIOS_Settings.useSoundBlaster = current; //Safety!
		BIOS_Changed = 1; //Changed!
	}
	int file = ExecuteList(15, 4, itemlist[current], 256, NULL,0); //Show options for the installed CPU!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = DEFAULT_SOUNDBLASTER; //Default setting: Disabled!

	case 0:
	case 1:
	case 2:
	default: //Changed?
		if (file != current) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			reboot_needed |= 1; //A reboot is needed!
			BIOS_Settings.useSoundBlaster = file; //Select Sound Blaster setting!
		}
		break;
	}
	BIOS_Menu = 31; //Goto Sound menu!
}

void BIOS_TurboCPUSpeed() //CPU speed selection!
{
	BIOS_Title("Turbo CPU speed");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Turbo CPU speed: "); //Show selection init!
	EMU_unlocktext();
	int_64 file = GetCPUSpeed(17, 4, *(getarchTurboCPUSpeed())); //Show options for the CPU speed!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected speed!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = 0; //Default setting: Disabled!
	default: //Changed?
		if (file != *(getarchTurboCPUSpeed())) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			*(getarchTurboCPUSpeed()) = (uint_32)file; //Select CPU speed setting!
		}
		break;
	}
	BIOS_Menu = 35; //Goto CPU menu!
}

void BIOS_useTurboCPUSpeed() //CPU speed toggle!
{
	*(getarchuseTurboCPUSpeed()) = !*(getarchuseTurboCPUSpeed()); //Toggle!
	BIOS_Changed = 1; //Changed!
	BIOS_Menu = 35; //Goto CPU menu!
}

int_64 GetDiagnosticsPortBreakpoint(byte x, byte y, sword DiagnosticsPortBreakpoint) //Retrieve the size, or 0 for none!
{
	int key = 0;
	lock(LOCK_INPUT);
	key = psp_inputkeydelay(BIOS_INPUTDELAY);
	unlock(LOCK_INPUT);
	while ((key&(BUTTON_CONFIRM|BUTTON_START))>0) //Pressed? Wait for release!
	{
		lock(LOCK_INPUT);
		key = psp_inputkeydelay(BIOS_INPUTDELAY);
		unlock(LOCK_INPUT);
	}
	if (DiagnosticsPortBreakpoint < 0) //Disabled?
	{
		DiagnosticsPortBreakpoint = 0x00; //Default to 00!
	}
	else
	{
		DiagnosticsPortBreakpoint &= 0xFF; //Safety wrap!
	}
	uint_32 result = DiagnosticsPortBreakpoint; //Size: result; default 0 for none! Must be a multiple of 4096 bytes for HDD!
	for (;;) //Get input; break on error!
	{
		EMU_locktext();
		EMU_textcolor(BIOS_ATTR_ACTIVE); //We're using active color for input!
		GPU_EMU_printscreen(x, y, "%02X", result); //Show current size!
		EMU_unlocktext();
		lock(LOCK_INPUT);
		key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
		unlock(LOCK_INPUT);

		if ((key & BUTTON_DOWN)>0) //1 step up?
		{
			result -= ((key&BUTTON_LEFT) ? 0x10 : 0x01); //x100 or x10 or x1!
		}
		else if ((key & BUTTON_UP)>0) //1 step down?
		{
			result += ((key&BUTTON_LEFT) ? 0x10 : 0x01); //x100 or x10 or x1!
		}
		//Confirmation buttons etc.
		else if ((key & (BUTTON_CONFIRM|BUTTON_START))>0)
		{
			while ((key&(BUTTON_CONFIRM|BUTTON_START))>0) //Wait for release!
			{
				lock(LOCK_INPUT);
				key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
				unlock(LOCK_INPUT);
			}
			return (int_64)result;
		}
		else if ((key & BUTTON_CANCEL)>0)
		{
			while ((key&BUTTON_CANCEL)>0) //Wait for release!
			{
				lock(LOCK_INPUT);
				key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
				unlock(LOCK_INPUT);
			}
			break; //Cancel!
		}
		else if ((key & BUTTON_TRIANGLE)>0)
		{
			while ((key&BUTTON_TRIANGLE)>0) //Wait for release!
			{
				lock(LOCK_INPUT);
				key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
				unlock(LOCK_INPUT);
			}
			return FILELIST_DEFAULT; //Default: disabled!
		}
		else if (shuttingdown()) break; //Cancel because of shutdown?

		result &= 0xFF; //Only a byte value is allowed, so wrap around it!
	}
	return FILELIST_CANCEL; //No size: cancel!
}

int_64 GetDiagnosticsPortBreakpointTimeout(byte x, byte y, uint_32 timeout) //Retrieve the size, or 0 for none!
{
	int key = 0;
	lock(LOCK_INPUT);
	key = psp_inputkeydelay(BIOS_INPUTDELAY);
	unlock(LOCK_INPUT);
	while ((key&(BUTTON_CONFIRM|BUTTON_START))>0) //Pressed? Wait for release!
	{
		lock(LOCK_INPUT);
		key = psp_inputkeydelay(BIOS_INPUTDELAY);
		unlock(LOCK_INPUT);
	}
	uint_32 result = timeout; //Size: result; default 0 for none! Must be a multiple of 4096 bytes for HDD!
	uint_32 oldvalue; //To check for high overflow!
	for (;;) //Get input; break on error!
	{
		EMU_locktext();
		EMU_textcolor(BIOS_ATTR_ACTIVE); //We're using active color for input!
		if (!result) //Default cycles?
		{
			GPU_EMU_printscreen(x, y, "First instruction                                      ", result); //Show first instruction!
		}
		else
		{
			GPU_EMU_printscreen(x, y, "At %u instructions", (result+1)); //Show current size!
		}
		EMU_unlocktext();
		lock(LOCK_INPUT);
		key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
		unlock(LOCK_INPUT);

		//1GB steps!
		if ((key & BUTTON_LTRIGGER)>0) //1000 step down?
		{
			if (result == 0) {}
			else
			{
				oldvalue = result; //Load the old value!
				result -= (key&BUTTON_RIGHT) ? 100000 : ((key&BUTTON_LEFT) ? 10000 : 1000); //x100 or x10 or x1!
				if (result>oldvalue) result = 0; //Underflow!
			}
		}
		else if ((key & BUTTON_RTRIGGER)>0) //1000 step up?
		{
			oldvalue = result; //Save the old value!
			result += (key&BUTTON_RIGHT) ? 100000 : ((key&BUTTON_LEFT) ? 10000 : 1000); //x100 or x10 or x1!
			if (result < oldvalue) result = oldvalue; //We've overflown?
		}
		else if ((key & BUTTON_DOWN)>0) //1 step up?
		{
			if (result == 0) {}
			else
			{
				oldvalue = result;
				result -= (key&BUTTON_RIGHT) ? 100 : ((key&BUTTON_LEFT) ? 10 : 1); //x100 or x10 or x1!
				if (result>oldvalue) result = 0; //Underflow!
			}
		}
		else if ((key & BUTTON_UP)>0) //1 step down?
		{
			oldvalue = result; //Save the old value!
			result += (key&BUTTON_RIGHT) ? 100 : ((key&BUTTON_LEFT) ? 10 : 1); //x100 or x10 or x1!
			if (result < oldvalue) result = oldvalue; //We've overflown?
		}
		//Confirmation buttons etc.
		else if ((key & (BUTTON_CONFIRM|BUTTON_START))>0)
		{
			while ((key&(BUTTON_CONFIRM|BUTTON_START))>0) //Wait for release!
			{
				lock(LOCK_INPUT);
				key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
				unlock(LOCK_INPUT);
			}
			return (int_64)result;
		}
		else if ((key & BUTTON_CANCEL)>0)
		{
			while ((key&BUTTON_CANCEL)>0) //Wait for release!
			{
				lock(LOCK_INPUT);
				key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
				unlock(LOCK_INPUT);
			}
			break; //Cancel!
		}
		else if ((key & BUTTON_TRIANGLE)>0)
		{
			while ((key&BUTTON_TRIANGLE)>0) //Wait for release!
			{
				lock(LOCK_INPUT);
				key = psp_inputkeydelay(BIOS_INPUTDELAY); //Input key!
				unlock(LOCK_INPUT);
			}
			return 0; //Default!
		}
		else if (shuttingdown()) break; //Cancel because of shutdown?
	}
	return FILELIST_CANCEL; //No size: cancel!
}

void BIOS_diagnosticsPortBreakpoint()
{
	BIOS_Title("Diagnostics Port Breakpoint");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Diagnostics Port Breakpoint: "); //Show selection init!
	EMU_unlocktext();
	int_64 file = GetDiagnosticsPortBreakpoint(29, 4, BIOS_Settings.diagnosticsportoutput_breakpoint); //Show options for the CPU speed!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected speed!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = -1; //Default setting: Disabled!
	default: //Changed?
		if (file != BIOS_Settings.diagnosticsportoutput_breakpoint) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			BIOS_Settings.diagnosticsportoutput_breakpoint = (sword)file; //Select Diagnostics Port Breakpoint setting!
		}
		break;
	}
	BIOS_Menu = 94; //Goto CPU Debugger menu!
}

void BIOS_diagnosticsPortBreakpointTimeout()
{
	BIOS_Title("Diagnostics Port Breakpoint Timeout");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Diagnostics Port Breakpoint Timeout: "); //Show selection init!
	EMU_unlocktext();
	int_64 file = GetDiagnosticsPortBreakpointTimeout(39, 4, BIOS_Settings.diagnosticsportoutput_timeout); //Show options for the CPU speed!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected speed!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = DEFAULT_DIAGNOSTICSPORTOUTPUT_TIMEOUT; //Default setting: One instruction!
	default: //Changed?
		if (file != BIOS_Settings.diagnosticsportoutput_timeout) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			BIOS_Settings.diagnosticsportoutput_timeout = (uint_32)file; //Select Diagnostics Port Breakpoint setting!
		}
		break;
	}
	BIOS_Menu = 94; //Goto CPU Debugger menu!
}

void BIOS_useDirectMIDIPassthrough()
{
	//We're supported?
	if (directMIDISupported()) //Are we supported?
	{
		BIOS_Settings.useDirectMIDI = !BIOS_Settings.useDirectMIDI; //Reverse!
	}
	else //Not supported?
	{
		BIOS_Settings.useDirectMIDI = 0; //Not supported!
	}
	BIOS_Changed = 1; //We've changed!
	reboot_needed |= 1; //A reboot is needed!
	BIOS_Menu = 31; //Goto Sound menu!
}

void BIOS_setBreakpoint(byte index, char *breakpointstr, word semicolonpos, byte mode, byte ignoreEIP, byte ignoreAddress, byte ignoreSegment, byte singleStep);

void BIOS_breakpoint()
{
	char breakpointstr[256]; //32-bits offset, colon, 16-bits segment, mode if required(Protected/Virtual 8086), Ignore EIP/CS/Whole address(mode only) and final character(always zero)!
	cleardata(&breakpointstr[0],sizeof(breakpointstr));
	//First, convert the current breakpoint to a string format!
	switch ((BIOS_Settings.breakpoint[BPindex]>>SETTINGS_BREAKPOINT_MODE_SHIFT)) //What mode?
	{
		case 0: //No breakpoint?
			snprintf(breakpointstr,sizeof(breakpointstr),"%04X:%04X",0,0); //seg16:offs16 default!
			if ((BIOS_Settings.breakpoint[BPindex]>>SETTINGS_BREAKPOINT_IGNOREADDRESS_SHIFT)&1) //Ignore address?
			{
				safestrcat(breakpointstr,sizeof(breakpointstr),"M"); //Ignore mode!
			}
			else if ((BIOS_Settings.breakpoint[BPindex]>>SETTINGS_BREAKPOINT_IGNOREEIP_SHIFT)&1) //Ignore EIP?
			{
				safestrcat(breakpointstr,sizeof(breakpointstr),"I"); //Ignore EIP!
			}
			else if ((BIOS_Settings.breakpoint[BPindex] >> SETTINGS_BREAKPOINT_IGNORESEGMENT_SHIFT) & 1) //Ignore CS?
			{
				safestrcat(breakpointstr, sizeof(breakpointstr), "O"); //Ignore CS!
			}
			if ((BIOS_Settings.breakpoint[BPindex] >> SETTINGS_BREAKPOINT_SINGLESTEP_SHIFT) & 1) //Single step?
			{
				safestrcat(breakpointstr, sizeof(breakpointstr), "S"); //Single step!
			}
			break;
		case 1: //Real mode?
			snprintf(breakpointstr,sizeof(breakpointstr),"%04X:%04X",(word)((BIOS_Settings.breakpoint[BPindex]>>SETTINGS_BREAKPOINT_SEGMENT_SHIFT)&SETTINGS_BREAKPOINT_SEGMENT_MASK),(word)((BIOS_Settings.breakpoint[BPindex]&SETTINGS_BREAKPOINT_OFFSET_MASK)&0xFFFF)); //seg16:offs16!
			if ((BIOS_Settings.breakpoint[BPindex]>>SETTINGS_BREAKPOINT_IGNOREADDRESS_SHIFT)&1) //Ignore address?
			{
				safestrcat(breakpointstr,sizeof(breakpointstr),"M"); //Ignore mode!
			}
			else if ((BIOS_Settings.breakpoint[BPindex]>>SETTINGS_BREAKPOINT_IGNOREEIP_SHIFT)&1) //Ignore EIP?
			{
				safestrcat(breakpointstr,sizeof(breakpointstr),"I"); //Ignore EIP!
			}
			else if ((BIOS_Settings.breakpoint[BPindex]>>SETTINGS_BREAKPOINT_IGNORESEGMENT_SHIFT) & 1) //Ignore CS?
			{
				safestrcat(breakpointstr, sizeof(breakpointstr), "O"); //Ignore CS!
			}
			if ((BIOS_Settings.breakpoint[BPindex] >> SETTINGS_BREAKPOINT_SINGLESTEP_SHIFT) & 1) //Single step?
			{
				safestrcat(breakpointstr, sizeof(breakpointstr), "S"); //Single step!
			}
			break;
		case 2: //Protected mode?
			snprintf(breakpointstr,sizeof(breakpointstr),"%04X:%08" SPRINTF_X_UINT32 "P",(word)((BIOS_Settings.breakpoint[BPindex]>>SETTINGS_BREAKPOINT_SEGMENT_SHIFT)&SETTINGS_BREAKPOINT_SEGMENT_MASK),(uint_32)(BIOS_Settings.breakpoint[BPindex]&SETTINGS_BREAKPOINT_OFFSET_MASK)); //seg16:offs16!
			if ((BIOS_Settings.breakpoint[BPindex]>>SETTINGS_BREAKPOINT_IGNOREADDRESS_SHIFT)&1) //Ignore address?
			{
				safestrcat(breakpointstr,sizeof(breakpointstr),"M"); //Ignore mode!
			}
			else if ((BIOS_Settings.breakpoint[BPindex]>>SETTINGS_BREAKPOINT_IGNOREEIP_SHIFT)&1) //Ignore EIP?
			{
				safestrcat(breakpointstr,sizeof(breakpointstr),"I"); //Ignore EIP!
			}
			else if ((BIOS_Settings.breakpoint[BPindex]>>SETTINGS_BREAKPOINT_IGNORESEGMENT_SHIFT)&1) //Ignore CS?
			{
				safestrcat(breakpointstr, sizeof(breakpointstr), "O"); //Ignore CS!
			}
			if ((BIOS_Settings.breakpoint[BPindex] >> SETTINGS_BREAKPOINT_SINGLESTEP_SHIFT) & 1) //Single step?
			{
				safestrcat(breakpointstr, sizeof(breakpointstr), "S"); //Single step!
			}
			break;
		case 3: //Virtual 8086 mode?
			snprintf(breakpointstr,sizeof(breakpointstr),"%04X:%04XV",(word)((BIOS_Settings.breakpoint[BPindex]>>SETTINGS_BREAKPOINT_SEGMENT_SHIFT)&SETTINGS_BREAKPOINT_SEGMENT_MASK),(word)((BIOS_Settings.breakpoint[BPindex]&SETTINGS_BREAKPOINT_OFFSET_MASK)&0xFFFF)); //seg16:offs16!
			if ((BIOS_Settings.breakpoint[BPindex]>>SETTINGS_BREAKPOINT_IGNOREADDRESS_SHIFT)&1) //Ignore address?
			{
				safestrcat(breakpointstr,sizeof(breakpointstr),"M"); //Ignore mode!
			}
			else if ((BIOS_Settings.breakpoint[BPindex]>>SETTINGS_BREAKPOINT_IGNOREEIP_SHIFT)&1) //Ignore EIP?
			{
				safestrcat(breakpointstr,sizeof(breakpointstr),"I"); //Ignore EIP!
			}
			else if ((BIOS_Settings.breakpoint[BPindex]>>SETTINGS_BREAKPOINT_IGNORESEGMENT_SHIFT)&1) //Ignore CS?
			{
				safestrcat(breakpointstr, sizeof(breakpointstr), "O"); //Ignore CS!
			}
			if ((BIOS_Settings.breakpoint[BPindex] >> SETTINGS_BREAKPOINT_SINGLESTEP_SHIFT) & 1) //Single step?
			{
				safestrcat(breakpointstr, sizeof(breakpointstr), "S"); //Single step!
			}
			break;
		default: //Just in case!
			break;
	}
	//I is added with bit set when it's done(Ignore EIP).

	BIOSClearScreen(); //Clear the screen!
	BIOS_Title("Breakpoint"); //Full clear!
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto position for info!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Address: "); //Show the filename!
	EMU_unlocktext();
	byte mode; //The mode to use!
	word semicolonpos;
	char *temp;
	word maxsegmentsize = 4;
	word maxoffsetsize = 4;
	byte ignoreEIP = 0;
	byte ignoreAddress = 0;
	byte ignoreSegment = 0;
	byte singleStep = 0;
	if (BIOS_InputAddressWithMode(9, 4, &breakpointstr[0], sizeof(breakpointstr)-1,1,1,1)) //Input text confirmed?
	{
		if (strcmp(breakpointstr, "") != 0) //Got valid input?
		{
			//Convert the string back into our valid numbers for storage!
			mode = 1; //Default to real mode!
			singleStep = (breakpointstr[safestrlen(breakpointstr, sizeof(breakpointstr)) - 1] == 'S'); //Single step?
			if (singleStep) breakpointstr[safestrlen(breakpointstr, sizeof(breakpointstr)) - 1] = '\0'; //Take off the mode identifier!
			ignoreSegment = (breakpointstr[safestrlen(breakpointstr,sizeof(breakpointstr))-1] == 'O'); //Ignore code segment?
			if (ignoreSegment) breakpointstr[safestrlen(breakpointstr,sizeof(breakpointstr))-1] = '\0'; //Take off the mode identifier!
			ignoreEIP = (breakpointstr[safestrlen(breakpointstr,sizeof(breakpointstr))-1]=='I'); //Ignore EIP?
			if (ignoreEIP) breakpointstr[safestrlen(breakpointstr,sizeof(breakpointstr))-1] = '\0'; //Take off the mode identifier!
			ignoreAddress = (breakpointstr[safestrlen(breakpointstr,sizeof(breakpointstr))-1]=='M'); //Ignore address?
			if (ignoreAddress) breakpointstr[safestrlen(breakpointstr,sizeof(breakpointstr))-1] = '\0'; //Take off the mode identifier!
			switch (breakpointstr[safestrlen(breakpointstr,sizeof(breakpointstr))-1]) //Identifier for the mode?
			{
				case 'P': //Protected mode?
					mode = 2; //Protected mode!
					breakpointstr[safestrlen(breakpointstr,sizeof(breakpointstr))-1] = '\0'; //Take off the mode identifier!
					maxoffsetsize = 8; //We're up to 8 hexadecimal values in this mode!
					goto handlemode;
				case 'V': //Virtual 8086 mode?
					mode = 3; //Virtual 8086 mode!
					breakpointstr[safestrlen(breakpointstr,sizeof(breakpointstr))-1] = '\0'; //Take off the mode identifier!
				default: //Real mode?
					handlemode: //Handle the other modes!
					temp = &breakpointstr[0]; //First character!
					for (;(*temp && *temp!=':');++temp); //No seperator yet?
					if (*temp!=':') //No seperator found?
					{
						goto abortcoloninput; //Invalid: can't handle!
					}
					if (*(temp+1)=='\0') //Invalid ending?
					{
						goto abortcoloninput; //Invalid: can't handle colon at the end!							
					}
					//Temp points to the colon!
					semicolonpos = (word)(temp-&breakpointstr[0]); //length up to the semicolon, which should be valid!
					if ((semicolonpos==0) || (semicolonpos>maxsegmentsize)) //Too long segment?
					{
						goto abortcoloninput; //Invalid: can't handle segment length!							
					}
					#ifndef IS_PSP
					//This won't compile on the PSP for some unknown reason, crashing the compiler!
					if (((safe_strlen(&breakpointstr[0],sizeof(breakpointstr))-semicolonpos)-1)<=maxoffsetsize) //Offset OK?
					{
						BIOS_setBreakpoint(BPindex,&breakpointstr[0],semicolonpos,mode,ignoreEIP,ignoreAddress,ignoreSegment,singleStep);
					}
					#endif
			}
		}
		else //Unset?
		{
			BIOS_Changed = BIOS_Changed||((BIOS_Settings.breakpoint[BPindex]!=0)?1:0); //We've changed!			
			BIOS_Settings.breakpoint[BPindex] = 0; //No breakpoint!
		}
	}
	abortcoloninput:
	BIOS_Menu = 94; //Goto CPU Debugger menu!
}

void BIOS_setTaskBreakpoint(char *breakpointstr, word semicolonpos, byte enabled, byte ignoreBase,  byte ignoreSegment);

void BIOS_taskBreakpoint()
{
	char breakpointstr[256]; //32-bits offset, colon, 16-bits segment, mode if required(Protected/Virtual 8086), Ignore EIP/CS/Whole address(mode only) and final character(always zero)!
	cleardata(&breakpointstr[0],sizeof(breakpointstr));
	//First, convert the current breakpoint to a string format!
	switch ((BIOS_Settings.taskBreakpoint>>SETTINGS_TASKBREAKPOINT_ENABLE_SHIFT)) //What mode?
	{
		case 0: //No breakpoint?
			break;
		case 1: //Enabled?
			snprintf(breakpointstr,sizeof(breakpointstr),"%04X:%08" SPRINTF_X_UINT32,(word)((BIOS_Settings.taskBreakpoint>>SETTINGS_TASKBREAKPOINT_SEGMENT_SHIFT)&SETTINGS_TASKBREAKPOINT_SEGMENT_MASK),(uint_32)(BIOS_Settings.taskBreakpoint&SETTINGS_TASKBREAKPOINT_BASE_MASK)); //seg16:offs16!
			if ((BIOS_Settings.taskBreakpoint>>SETTINGS_TASKBREAKPOINT_IGNOREBASE_SHIFT)&1) //Ignore EIP?
			{
				safestrcat(breakpointstr,sizeof(breakpointstr),"I"); //Ignore EIP!
			}
			else if ((BIOS_Settings.taskBreakpoint >> SETTINGS_BREAKPOINT_IGNORESEGMENT_SHIFT) & 1) //Ignore CS?
			{
				safestrcat(breakpointstr, sizeof(breakpointstr), "O"); //Ignore CS!
			}
			break;
		default: //Just in case!
			break;
	}
	//I is added with bit set when it's done(Ignore EIP).

	BIOSClearScreen(); //Clear the screen!
	BIOS_Title("Task Breakpoint"); //Full clear!
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto position for info!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Address: "); //Show the filename!
	EMU_unlocktext();
	word semicolonpos;
	char *temp;
	word maxsegmentsize = 4;
#ifndef IS_PSP
	word maxoffsetsize = 8;
#endif
	byte ignoreBase = 0;
	byte ignoreSegment = 0;
	if (BIOS_InputAddressWithMode(9, 4, &breakpointstr[0], sizeof(breakpointstr)-1,0,1,0)) //Input text confirmed?
	{
		if (strcmp(breakpointstr, "") != 0) //Got valid input?
		{
			//Convert the string back into our valid numbers for storage!
			ignoreSegment = (breakpointstr[safestrlen(breakpointstr,sizeof(breakpointstr))-1] == 'O'); //Ignore code segment?
			if (ignoreSegment) breakpointstr[safestrlen(breakpointstr,sizeof(breakpointstr))-1] = '\0'; //Take off the mode identifier!
			ignoreBase = (breakpointstr[safestrlen(breakpointstr,sizeof(breakpointstr))-1]=='I'); //Ignore EIP?
			if (ignoreBase) breakpointstr[safestrlen(breakpointstr,sizeof(breakpointstr))-1] = '\0'; //Take off the mode identifier!
				temp = &breakpointstr[0]; //First character!
				for (;(*temp && *temp!=':');++temp); //No seperator yet?
				if (*temp!=':') //No seperator found?
				{
					goto abortcoloninput; //Invalid: can't handle!
				}
				if (*(temp+1)=='\0') //Invalid ending?
				{
					goto abortcoloninput; //Invalid: can't handle colon at the end!							
				}
				//Temp points to the colon!
				semicolonpos = (word)(temp-&breakpointstr[0]); //length up to the semicolon, which should be valid!
				if ((semicolonpos==0) || (semicolonpos>maxsegmentsize)) //Too long segment?
				{
					goto abortcoloninput; //Invalid: can't handle segment length!							
				}
				#ifndef IS_PSP
				//This won't compile on the PSP for some unknown reason, crashing the compiler!
				if (((safe_strlen(&breakpointstr[0],sizeof(breakpointstr))-semicolonpos)-1)<=maxoffsetsize) //Offset OK?
				{
					BIOS_setTaskBreakpoint(&breakpointstr[0],semicolonpos,1,ignoreBase,ignoreSegment);
				}
				#endif
		}
		else //Unset?
		{
			BIOS_Changed = BIOS_Changed||((BIOS_Settings.taskBreakpoint!=0)?1:0); //We've changed!			
			BIOS_Settings.taskBreakpoint = 0; //No breakpoint!
		}
	}
	abortcoloninput:
	BIOS_Menu = 94; //Goto CPU Debugger menu!
}

void BIOS_setFSBreakpoint(char* breakpointstr, word semicolonpos, byte enabled, byte ignoreBase, byte ignoreSegment);

void BIOS_FSBreakpoint()
{
	char breakpointstr[256]; //32-bits offset, colon, 16-bits segment, mode if required(Protected/Virtual 8086), Ignore EIP/CS/Whole address(mode only) and final character(always zero)!
	cleardata(&breakpointstr[0], sizeof(breakpointstr));
	//First, convert the current breakpoint to a string format!
	switch ((BIOS_Settings.FSBreakpoint >> SETTINGS_FSBREAKPOINT_ENABLE_SHIFT)) //What mode?
	{
	case 0: //No breakpoint?
		break;
	case 1: //Enabled?
		snprintf(breakpointstr, sizeof(breakpointstr), "%04X:%08" SPRINTF_X_UINT32, (word)((BIOS_Settings.FSBreakpoint >> SETTINGS_FSBREAKPOINT_SEGMENT_SHIFT) & SETTINGS_FSBREAKPOINT_SEGMENT_MASK), (uint_32)(BIOS_Settings.FSBreakpoint & SETTINGS_FSBREAKPOINT_BASE_MASK)); //seg16:offs16!
		if ((BIOS_Settings.FSBreakpoint >> SETTINGS_FSBREAKPOINT_IGNOREBASE_SHIFT) & 1) //Ignore EIP?
		{
			safestrcat(breakpointstr, sizeof(breakpointstr), "I"); //Ignore EIP!
		}
		else if ((BIOS_Settings.FSBreakpoint >> SETTINGS_BREAKPOINT_IGNORESEGMENT_SHIFT) & 1) //Ignore CS?
		{
			safestrcat(breakpointstr, sizeof(breakpointstr), "O"); //Ignore CS!
		}
		break;
	default: //Just in case!
		break;
	}
	//I is added with bit set when it's done(Ignore EIP).

	BIOSClearScreen(); //Clear the screen!
	BIOS_Title("FS Breakpoint"); //Full clear!
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto position for info!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Address: "); //Show the filename!
	EMU_unlocktext();
	//byte mode; //The mode to use!
	word semicolonpos;
	char* temp;
	word maxsegmentsize = 4;
#ifndef IS_PSP
	word maxoffsetsize = 8;
#endif
	byte ignoreBase = 0;
	byte ignoreSegment = 0;
	if (BIOS_InputAddressWithMode(9, 4, &breakpointstr[0], sizeof(breakpointstr) - 1, 0, 1, 0)) //Input text confirmed?
	{
		if (strcmp(breakpointstr, "") != 0) //Got valid input?
		{
			//Convert the string back into our valid numbers for storage!
			ignoreSegment = (breakpointstr[safestrlen(breakpointstr, sizeof(breakpointstr)) - 1] == 'O'); //Ignore code segment?
			if (ignoreSegment) breakpointstr[safestrlen(breakpointstr, sizeof(breakpointstr)) - 1] = '\0'; //Take off the mode identifier!
			ignoreBase = (breakpointstr[safestrlen(breakpointstr, sizeof(breakpointstr)) - 1] == 'I'); //Ignore EIP?
			if (ignoreBase) breakpointstr[safestrlen(breakpointstr, sizeof(breakpointstr)) - 1] = '\0'; //Take off the mode identifier!
			temp = &breakpointstr[0]; //First character!
			for (; (*temp && *temp != ':'); ++temp); //No seperator yet?
			if (*temp != ':') //No seperator found?
			{
				goto abortcoloninput; //Invalid: can't handle!
			}
			if (*(temp + 1) == '\0') //Invalid ending?
			{
				goto abortcoloninput; //Invalid: can't handle colon at the end!							
			}
			//Temp points to the colon!
			semicolonpos = (word)(temp - &breakpointstr[0]); //length up to the semicolon, which should be valid!
			if ((semicolonpos == 0) || (semicolonpos > maxsegmentsize)) //Too long segment?
			{
				goto abortcoloninput; //Invalid: can't handle segment length!							
			}
#ifndef IS_PSP
			//This won't compile on the PSP for some unknown reason, crashing the compiler!
			if (((safe_strlen(&breakpointstr[0], sizeof(breakpointstr)) - semicolonpos) - 1) <= maxoffsetsize) //Offset OK?
			{
				BIOS_setFSBreakpoint(&breakpointstr[0], semicolonpos, 1, ignoreBase, ignoreSegment);
			}
#endif
		}
		else //Unset?
		{
			BIOS_Changed = BIOS_Changed || ((BIOS_Settings.FSBreakpoint != 0) ? 1 : 0); //We've changed!			
			BIOS_Settings.FSBreakpoint = 0; //No breakpoint!
		}
	}
abortcoloninput:
	BIOS_Menu = 94; //Goto CPU Debugger menu!
}

void BIOS_setCR3breakpoint(char *breakpointstr, byte enabled);

void BIOS_CR3breakpoint()
{
	char breakpointstr[256]; //32-bits offset, colon, 16-bits segment, mode if required(Protected/Virtual 8086), Ignore EIP/CS/Whole address(mode only) and final character(always zero)!
	cleardata(&breakpointstr[0],sizeof(breakpointstr));
	//First, convert the current breakpoint to a string format!
	switch ((BIOS_Settings.CR3breakpoint>>SETTINGS_CR3BREAKPOINT_ENABLE_SHIFT)) //What mode?
	{
		case 0: //No breakpoint?
			break;
		case 1: //Enabled?
			snprintf(breakpointstr,sizeof(breakpointstr),"%08" SPRINTF_X_UINT32,(uint_32)(BIOS_Settings.CR3breakpoint&SETTINGS_CR3BREAKPOINT_BASE_MASK)); //offs16!
			break;
		default: //Just in case!
			break;
	}
	//I is added with bit set when it's done(Ignore EIP).

	BIOSClearScreen(); //Clear the screen!
	BIOS_Title("CR3 Breakpoint"); //Full clear!
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto position for info!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Address: "); //Show the filename!
	EMU_unlocktext();
	#ifndef IS_PSP
	word maxoffsetsize = 8;
	#endif
	if (BIOS_InputAddressWithMode(9, 4, &breakpointstr[0], sizeof(breakpointstr)-1,0,0,0)) //Input text confirmed?
	{
		if (strcmp(breakpointstr, "") != 0) //Got valid input?
		{
			//Convert the string back into our valid numbers for storage!
				#ifndef IS_PSP
				//This won't compile on the PSP for some unknown reason, crashing the compiler!
				if (((safe_strlen(&breakpointstr[0],sizeof(breakpointstr)))-1)<=maxoffsetsize) //Offset OK?
				{
					BIOS_setCR3breakpoint(&breakpointstr[0],1);
				}
				#endif
		}
		else //Unset?
		{
			BIOS_Changed = BIOS_Changed||((BIOS_Settings.CR3breakpoint!=0)?1:0); //We've changed!			
			BIOS_Settings.CR3breakpoint = 0; //No breakpoint!
		}
	}
	BIOS_Menu = 94; //Goto CPU Debugger menu!
}

void BIOS_setBreakpoint(byte index, char *breakpointstr, word semicolonpos, byte mode, byte ignoreEIP, byte ignoreAddress, byte ignoreSegment, byte singleStep)
{
	word segment;
	uint_32 offset;
	breakpointstr[semicolonpos] = '\0'; //Convert the semicolon into an EOS character to apply the string length!
	segment = converthex2int(&breakpointstr[0]); //Convert the number to our usable format!
	offset = converthex2int(&breakpointstr[semicolonpos+1]); //Convert the number to our usable format!

	//Apply the new breakpoint!
	BIOS_Settings.breakpoint[index] = (((uint_64)mode&3)<<SETTINGS_BREAKPOINT_MODE_SHIFT);
	BIOS_Settings.breakpoint[index] |= (((ignoreEIP?1LLU:0LLU)<<SETTINGS_BREAKPOINT_IGNOREEIP_SHIFT));
	BIOS_Settings.breakpoint[index] |=	(((ignoreAddress?1LLU:0LLU)<<SETTINGS_BREAKPOINT_IGNOREADDRESS_SHIFT));
	BIOS_Settings.breakpoint[index] |= (((ignoreSegment ? 1LLU : 0LLU) << SETTINGS_BREAKPOINT_IGNORESEGMENT_SHIFT));
	BIOS_Settings.breakpoint[index] |= (((singleStep ? 1LLU : 0LLU) << SETTINGS_BREAKPOINT_SINGLESTEP_SHIFT));
	BIOS_Settings.breakpoint[index] |= (((uint_64)segment&SETTINGS_BREAKPOINT_SEGMENT_MASK)<<SETTINGS_BREAKPOINT_SEGMENT_SHIFT);
	BIOS_Settings.breakpoint[index] |= ((uint_64)offset&SETTINGS_BREAKPOINT_OFFSET_MASK); //Set the new breakpoint!
	BIOS_Changed = 1; //We've changed!
}

void BIOS_setTaskBreakpoint(char *breakpointstr, word semicolonpos, byte enabled, byte ignoreBase, byte ignoreSegment)
{
	word segment;
	uint_32 offset;
	breakpointstr[semicolonpos] = '\0'; //Convert the semicolon into an EOS character to apply the string length!
	segment = converthex2int(&breakpointstr[0]); //Convert the number to our usable format!
	offset = converthex2int(&breakpointstr[semicolonpos+1]); //Convert the number to our usable format!

	//Apply the new breakpoint!
	BIOS_Settings.taskBreakpoint = (((uint_64)enabled&3)<<SETTINGS_TASKBREAKPOINT_ENABLE_SHIFT);
	BIOS_Settings.taskBreakpoint |= (((ignoreBase?1LLU:0LLU)<<SETTINGS_TASKBREAKPOINT_IGNOREBASE_SHIFT));
	BIOS_Settings.taskBreakpoint |= (((ignoreSegment ? 1LLU : 0LLU) << SETTINGS_TASKBREAKPOINT_IGNORESEGMENT_SHIFT));
	BIOS_Settings.taskBreakpoint |= (((uint_64)segment&SETTINGS_TASKBREAKPOINT_SEGMENT_MASK)<<SETTINGS_TASKBREAKPOINT_SEGMENT_SHIFT);
	BIOS_Settings.taskBreakpoint |= ((uint_64)offset&SETTINGS_TASKBREAKPOINT_BASE_MASK); //Set the new breakpoint!
	BIOS_Changed = 1; //We've changed!
}

void BIOS_setFSBreakpoint(char* breakpointstr, word semicolonpos, byte enabled, byte ignoreBase, byte ignoreSegment)
{
	word segment;
	uint_32 offset;
	breakpointstr[semicolonpos] = '\0'; //Convert the semicolon into an EOS character to apply the string length!
	segment = converthex2int(&breakpointstr[0]); //Convert the number to our usable format!
	offset = converthex2int(&breakpointstr[semicolonpos + 1]); //Convert the number to our usable format!

	//Apply the new breakpoint!
	BIOS_Settings.FSBreakpoint = (((uint_64)enabled & 3) << SETTINGS_FSBREAKPOINT_ENABLE_SHIFT);
	BIOS_Settings.FSBreakpoint |= (((ignoreBase ? 1LLU : 0LLU) << SETTINGS_FSBREAKPOINT_IGNOREBASE_SHIFT));
	BIOS_Settings.FSBreakpoint |= (((ignoreSegment ? 1LLU : 0LLU) << SETTINGS_FSBREAKPOINT_IGNORESEGMENT_SHIFT));
	BIOS_Settings.FSBreakpoint |= (((uint_64)segment & SETTINGS_FSBREAKPOINT_SEGMENT_MASK) << SETTINGS_FSBREAKPOINT_SEGMENT_SHIFT);
	BIOS_Settings.FSBreakpoint |= ((uint_64)offset & SETTINGS_FSBREAKPOINT_BASE_MASK); //Set the new breakpoint!
	BIOS_Changed = 1; //We've changed!
}

void BIOS_setCR3breakpoint(char *breakpointstr,  byte enabled)
{
	uint_32 offset;
	offset = converthex2int(&breakpointstr[0]); //Convert the number to our usable format!

	//Apply the new breakpoint!
	BIOS_Settings.CR3breakpoint = (((uint_64)enabled&3)<<SETTINGS_CR3BREAKPOINT_ENABLE_SHIFT);
	BIOS_Settings.CR3breakpoint |= ((uint_64)offset&SETTINGS_CR3BREAKPOINT_BASE_MASK); //Set the new breakpoint!
	BIOS_Changed = 1; //We've changed!
}

void BIOS_ROMMode()
{
	BIOS_Title("BIOS ROM mode");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "BIOS ROM mode: "); //Show selection init!
	EMU_unlocktext();
	int i = 0; //Counter!
	numlist = 3; //Amount of Direct modes!
	for (i = 0; i<3; i++) //Process options!
	{
		cleardata(&itemlist[i][0], sizeof(itemlist[i])); //Reset!
	}
	safestrcpy(itemlist[BIOSROMMODE_NORMAL],sizeof(itemlist[0]), "Normal BIOS ROM"); //Set filename from options!
	safestrcpy(itemlist[BIOSROMMODE_DIAGNOSTICS],sizeof(itemlist[0]), "Diagnostic ROM"); //Set filename from options!
	safestrcpy(itemlist[BIOSROMMODE_UROMS],sizeof(itemlist[0]), "Enforce normal U-ROMs"); //Set filename from options!
	int current = 0;
	switch (BIOS_Settings.BIOSROMmode) //What setting?
	{
	case BIOSROMMODE_NORMAL: //Valid
	case BIOSROMMODE_DIAGNOSTICS: //Valid
	case BIOSROMMODE_UROMS: //Valid
		current = BIOS_Settings.BIOSROMmode; //Valid: use!
		break;
	default: //Invalid
		current = DEFAULT_BIOSROMMODE; //Default: none!
		break;
	}
	if (BIOS_Settings.BIOSROMmode != current) //Invalid?
	{
		BIOS_Settings.BIOSROMmode = current; //Safety!
		BIOS_Changed = 1; //Changed!
	}
	int file = ExecuteList(15, 4, itemlist[current], 256,NULL,0); //Show options for the installed CPU!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = DEFAULT_BIOSROMMODE; //Default setting: Disabled!

	case BIOSROMMODE_NORMAL:
	case BIOSROMMODE_DIAGNOSTICS:
	case BIOSROMMODE_UROMS:
	default: //Changed?
		if (file != current) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			BIOS_Settings.BIOSROMmode = file; //Select PS/2 Mouse setting!
			reboot_needed |= 1; //A reboot is needed when applied!
		}
		break;
	}
	BIOS_Menu = 35; //Goto CPU menu!
}

void BIOS_InboardInitialWaitstates()
{
	BIOS_Title("Inboard Initial Waitstates");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Inboard Initial Waitstates: "); //Show selection init!
	EMU_unlocktext();
	int i = 0; //Counter!
	numlist = 2; //Amount of Direct modes!
	for (i = 0; i<2; i++) //Process options!
	{
		cleardata(&itemlist[i][0], sizeof(itemlist[i])); //Reset!
	}
	safestrcpy(itemlist[0],sizeof(itemlist[0]), "Default waitstates"); //Set filename from options!
	safestrcpy(itemlist[1],sizeof(itemlist[0]), "No waitstates"); //Set filename from options!
	int current = 0;
	switch (BIOS_Settings.InboardInitialWaitstates) //What setting?
	{
	case 0: //Valid
	case 1: //Valid
		current = BIOS_Settings.InboardInitialWaitstates; //Valid: use!
		break;
	default: //Invalid
		current = DEFAULT_INBOARDINITIALWAITSTATES; //Default: none!
		break;
	}
	if (BIOS_Settings.InboardInitialWaitstates != current) //Invalid?
	{
		BIOS_Settings.InboardInitialWaitstates = current; //Safety!
		BIOS_Changed = 1; //Changed!
	}
	int file = ExecuteList(28, 4, itemlist[current], 256,NULL,0); //Show options for the installed CPU!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = DEFAULT_INBOARDINITIALWAITSTATES; //Default setting: Disabled!

	case 0:
	case 1:
	default: //Changed?
		if (file != current) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			BIOS_Settings.InboardInitialWaitstates = file; //Select PS/2 Mouse setting!
			reboot_needed |= 1; //A reboot is needed when applied!
		}
		break;
	}
	BIOS_Menu = 35; //Goto CPU menu!
}

void BIOS_ClockingMode() //Clocking Mode toggle!
{
	*(getarchclockingmode()) = !*(getarchclockingmode()); //Toggle!
	BIOS_Changed = 1; //Changed!
	BIOS_Menu = 35; //Goto CPU menu!
	reboot_needed |= 1; //A reboot is needed when applied!
}

void BIOS_DebugRegisters()
{
	BIOS_Settings.debugger_logregisters = !BIOS_Settings.debugger_logregisters;
	BIOS_Changed = 1; //We're changed!
	BIOS_Menu = 94; //Goto CPU Debugger menu!
}

void BIOS_CMOSTiming() //Time the CMOS!
{
	if (is_i430fx==2) //i440fx?
	{
		BIOS_Changed = 1; //We've changed!
		reboot_needed |= 2; //We're needing a reboot!
	}
	else if (is_i430fx==1) //i430fx?
	{
		BIOS_Changed = 1; //We've changed!
		reboot_needed |= 2; //We're needing a reboot!
	}
	else if (is_PS2) //PS/2?
	{
			BIOS_Changed = 1; //We've changed!
			reboot_needed |= 2; //We're needing a reboot!
	}
	else if (is_Compaq)
	{
			BIOS_Changed = 1; //We've changed!
			reboot_needed |= 2; //We're needing a reboot!
	}
	else if (is_XT)
	{
			BIOS_Changed = 1; //We've changed!
			reboot_needed |= 2; //We're needing a reboot!
	}
	else //AT?
	{
			BIOS_Changed = 1; //We've changed!
			reboot_needed |= 2; //We're needing a reboot!
	}
	lock(LOCK_CPU); //Lock the CPU: we're going to change something in active emulation!
	CMOS.Loaded = 1; //Unload the CMOS: discard anything that's loaded when saving!
	CMOS.DATA.cycletiming = !CMOS.DATA.cycletiming; //Reverse!
	unlock(LOCK_CPU); //We're finished with the main thread!
	CMOSGLOBALBACKUPDATA backupglobal;
	if (is_i430fx==2) //i440fx?
	{
		if (!BIOS_Settings.got_i440fxCMOS)
		{
			backupCMOSglobalsettings(&BIOS_Settings.i440fxCMOS, &backupglobal); //Backup the memory field!
			memset(&BIOS_Settings.i440fxCMOS, 0, sizeof(BIOS_Settings.i440fxCMOS)); //Init!
			restoreCMOSglobalsettings(&BIOS_Settings.i440fxCMOS, &backupglobal); //Backup the memory field!
		}
		BIOS_Settings.i440fxCMOS.cycletiming = !BIOS_Settings.i440fxCMOS.cycletiming; //Reverse!
		BIOS_Settings.got_i440fxCMOS = 1; //We hav gotten a CMOS!
	}
	else if (is_i430fx==1) //i430fx?
	{
		if (!BIOS_Settings.got_i430fxCMOS)
		{
			backupCMOSglobalsettings(&BIOS_Settings.i430fxCMOS, &backupglobal); //Backup the memory field!
			memset(&BIOS_Settings.i430fxCMOS, 0, sizeof(BIOS_Settings.i430fxCMOS)); //Init!
			restoreCMOSglobalsettings(&BIOS_Settings.i430fxCMOS, &backupglobal); //Backup the memory field!
		}
		BIOS_Settings.i430fxCMOS.cycletiming = !BIOS_Settings.i430fxCMOS.cycletiming; //Reverse!
		BIOS_Settings.got_i430fxCMOS = 1; //We hav gotten a CMOS!
	}
	else if (is_PS2) //PS/2?
	{
		if (!BIOS_Settings.got_PS2CMOS)
		{
			backupCMOSglobalsettings(&BIOS_Settings.PS2CMOS, &backupglobal); //Backup the memory field!
			memset(&BIOS_Settings.PS2CMOS,0,sizeof(BIOS_Settings.PS2CMOS)); //Init!
			restoreCMOSglobalsettings(&BIOS_Settings.PS2CMOS, &backupglobal); //Backup the memory field!
		}
		BIOS_Settings.PS2CMOS.cycletiming = !BIOS_Settings.PS2CMOS.cycletiming; //Reverse!
		BIOS_Settings.got_PS2CMOS = 1; //We hav gotten a CMOS!
	}
	else if (is_Compaq)
	{
		if (!BIOS_Settings.got_CompaqCMOS)
		{
			backupCMOSglobalsettings(&BIOS_Settings.CompaqCMOS, &backupglobal); //Backup the memory field!
			memset(&BIOS_Settings.CompaqCMOS,0,sizeof(BIOS_Settings.CompaqCMOS)); //Init!
			restoreCMOSglobalsettings(&BIOS_Settings.CompaqCMOS, &backupglobal); //Backup the memory field!
		}
		BIOS_Settings.CompaqCMOS.cycletiming = !BIOS_Settings.CompaqCMOS.cycletiming; //Reverse!
		BIOS_Settings.got_CompaqCMOS = 1; //We hav gotten a CMOS!
	}
	else if (is_XT)
	{
		if (!BIOS_Settings.got_XTCMOS)
		{
			backupCMOSglobalsettings(&BIOS_Settings.XTCMOS, &backupglobal); //Backup the memory field!
			memset(&BIOS_Settings.XTCMOS,0,sizeof(BIOS_Settings.XTCMOS)); //Init!
			restoreCMOSglobalsettings(&BIOS_Settings.XTCMOS, &backupglobal); //Backup the memory field!
		}
		BIOS_Settings.XTCMOS.cycletiming = !BIOS_Settings.XTCMOS.cycletiming; //Reverse!
		BIOS_Settings.got_XTCMOS = 1; //We hav gotten a CMOS!
	}
	else //AT?
	{
		if (!BIOS_Settings.got_ATCMOS)
		{
			backupCMOSglobalsettings(&BIOS_Settings.ATCMOS, &backupglobal); //Backup the memory field!
			memset(&BIOS_Settings.ATCMOS,0,sizeof(BIOS_Settings.ATCMOS)); //Init!
			restoreCMOSglobalsettings(&BIOS_Settings.ATCMOS, &backupglobal); //Backup the memory field!
		}
		BIOS_Settings.ATCMOS.cycletiming = !BIOS_Settings.ATCMOS.cycletiming; //Reverse!
		BIOS_Settings.got_ATCMOS = 1; //We hav gotten a CMOS!
	}
	BIOS_Menu = 8; //Goto Advanced Menu!
}

void BIOS_BackgroundPolicySetting()
{
	BIOS_Title("Background policy");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Background policy: "); //Show selection init!
	EMU_unlocktext();
	int i = 0; //Counter!
	numlist = 4; //Amount of background policies!
	for (i = 0; i<numlist; i++) //Process options!
	{
		cleardata(&itemlist[i][0], sizeof(itemlist[i])); //Reset!
	}
	
	safestrcpy(itemlist[BACKGROUNDPOLICY_FULLHALT], sizeof(itemlist[0]), "Full halt"); //Set filename from options!
	safestrcpy(itemlist[BACKGROUNDPOLICY_KEEPRUNNINGWITHOUTAUDIOANDVIDEO], sizeof(itemlist[0]), "Run without audio playing and recording"); //Set filename from options!
	safestrcpy(itemlist[BACKGROUNDPOLICY_KEEPRUNNINGWITHOUTAUDIORECORDINGANDVIDEO], sizeof(itemlist[0]), "Run without recording"); //Special CGA compatibility mode!
	safestrcpy(itemlist[BACKGROUNDPOLICY_KEEPRUNNINGWITHOUTVIDEO], sizeof(itemlist[0]), "Run without rendering the display"); //Special MDA compatibility mode!

	int current = 0;
	switch (BIOS_Settings.backgroundpolicy) //What setting?
	{
	case BACKGROUNDPOLICY_FULLHALT: //Valid
	case BACKGROUNDPOLICY_KEEPRUNNINGWITHOUTAUDIOANDVIDEO: //Valid
	case BACKGROUNDPOLICY_KEEPRUNNINGWITHOUTAUDIORECORDINGANDVIDEO: //Valid
	case BACKGROUNDPOLICY_KEEPRUNNINGWITHOUTVIDEO: //Valid
		current = BIOS_Settings.backgroundpolicy; //Valid: use!
		break;
	default: //Invalid
		current = DEFAULT_BACKGROUNDPOLICY; //Default: none!
		break;
	}
	if (BIOS_Settings.backgroundpolicy != current) //Invalid?
	{
		BIOS_Settings.backgroundpolicy = current; //Safety!
		BIOS_Changed = 1; //Changed!
	}
	int file = ExecuteList(19, 4, itemlist[current], 256, NULL,0); //Show options for the installed CPU!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = DEFAULT_BACKGROUNDPOLICY; //Default setting: Disabled!

	case BACKGROUNDPOLICY_FULLHALT:
	case BACKGROUNDPOLICY_KEEPRUNNINGWITHOUTAUDIOANDVIDEO:
	case BACKGROUNDPOLICY_KEEPRUNNINGWITHOUTAUDIORECORDINGANDVIDEO:
	case BACKGROUNDPOLICY_KEEPRUNNINGWITHOUTVIDEO:
	default: //Changed?
		if (file != current) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			BIOS_Settings.backgroundpolicy = file; //Select VGA Mode setting!
		}
		break;
	}
	BIOS_Menu = 8; //Goto Advanced menu!
}

void BIOS_AdvancedLogSetting()
{
	BIOS_Title("Advanced log");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Advanced log: "); //Show selection init!
	EMU_unlocktext();
	int i = 0; //Counter!
	numlist = 2; //Amount of background policies!
	for (i = 0; i<numlist; i++) //Process options!
	{
		cleardata(&itemlist[i][0], sizeof(itemlist[i])); //Reset!
	}

	safestrcpy(itemlist[0], sizeof(itemlist[0]), "Disable advanced logging"); //Set filename from options!
	safestrcpy(itemlist[1], sizeof(itemlist[0]), "Use advanced logging"); //Set filename from options!

	int current = 0;
	switch (BIOS_Settings.advancedlog) //What setting?
	{
	case 0: //Valid
	case 1: //Valid
		current = BIOS_Settings.advancedlog; //Valid: use!
		break;
	default: //Invalid
		current = DEFAULT_ADVANCEDLOG; //Default: none!
		break;
	}
	if (BIOS_Settings.advancedlog != current) //Invalid?
	{
		BIOS_Settings.advancedlog = current; //Safety!
		BIOS_Changed = 1; //Changed!
	}
	int file = ExecuteList(14, 4, itemlist[current], 256, NULL,0); //Show options for the installed CPU!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
						  //We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = DEFAULT_ADVANCEDLOG; //Default setting: Disabled!

	case 0:
	case 1:
	default: //Changed?
		if (file != current) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			BIOS_Settings.advancedlog = file; //Select VGA Mode setting!
		}
		break;
	}
	BIOS_Menu = 94; //Goto CPU Debugger menu!
}

void BIOS_DirectInput_remap_RCTRL_to_LWIN()
{
	BIOS_Settings.input_settings.DirectInput_remap_RCTRL_to_LWIN = !BIOS_Settings.input_settings.DirectInput_remap_RCTRL_to_LWIN;
	BIOS_Changed = 1; //We're changed!
	BIOS_Menu = 25; //Goto Input menu!
}

void BIOS_DirectInput_remap_accentgrave_to_tab()
{
	BIOS_Settings.input_settings.DirectInput_remap_accentgrave_to_tab = !BIOS_Settings.input_settings.DirectInput_remap_accentgrave_to_tab;
	BIOS_Changed = 1; //We're changed!
	BIOS_Menu = 25; //Goto Input menu!
}

void BIOS_DirectInput_remap_NUM0_to_Delete()
{
	BIOS_Settings.input_settings.DirectInput_remap_NUM0_to_Delete = !BIOS_Settings.input_settings.DirectInput_remap_NUM0_to_Delete;
	BIOS_Changed = 1; //We're changed!
	BIOS_Menu = 25; //Goto Input menu!
}

void BIOS_DirectInput_Disable_RALT()
{
	BIOS_Settings.input_settings.DirectInput_Disable_RALT = !BIOS_Settings.input_settings.DirectInput_Disable_RALT;
	BIOS_Changed = 1; //We're changed!
	BIOS_Menu = 25; //Goto Input menu!
}


void BIOS_floppy0_nodisk_type()
{
	byte i;
	char filename[256]; //Filename container!
	cleardata(&filename[0], sizeof(filename)); //Init!
	for (i = 0; i < NUMFLOPPYGEOMETRIES; i++) //Process all geometries into a list!
	{
		memset(&itemlist[i], 0, sizeof(itemlist[i])); //Reset!
		strcpy(itemlist[i], floppygeometries[i].text);
	}
	numlist = NUMFLOPPYGEOMETRIES; //The size of the list!

	CMOSDATA *currentCMOS;
	if (is_i430fx==2) //i440fx?
	{
		currentCMOS = &BIOS_Settings.i440fxCMOS; //We've used!
	}
	else if (is_i430fx==1) //i430fx?
	{
		currentCMOS = &BIOS_Settings.i430fxCMOS; //We've used!
	}
	else if (is_PS2) //PS/2?
	{
		currentCMOS = &BIOS_Settings.PS2CMOS; //We've used!
	}
	else if (is_Compaq)
	{
		currentCMOS = &BIOS_Settings.CompaqCMOS; //We've used!
	}
	else if (is_XT)
	{
		currentCMOS = &BIOS_Settings.XTCMOS; //We've used!
	}
	else //AT?
	{
		currentCMOS = &BIOS_Settings.ATCMOS; //We've used!
	}

	BIOS_Title("Default Floppy A without disk type");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Floppy without disk type: "); //Show selection init!
	EMU_unlocktext();
	int result;
	result = ExecuteList(26, 4, itemlist[currentCMOS->floppy0_nodisk_type], 256, NULL,0); //Get our result!
	int newtype;
	byte confirmed;
	newtype = currentCMOS->floppy0_nodisk_type; //Old type!
	confirmed = 0; //Default: not confirmed!
	switch (result) //Which result?
	{
	case FILELIST_DEFAULT: //Unmount?
		BIOS_Changed |= 1; //Changed!
		newtype = 0; //Default!
		confirmed = 1; //Confirmed!
		break;
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break;
	default: //File?
		if ((result >= 0) && (result < NUMFLOPPYGEOMETRIES)) //Valid item?
		{
			if (result != currentCMOS->floppy0_nodisk_type) //Changed?
			{
				confirmed = 1; //Confirmed!
				newtype = result; //Set the new value!
				BIOS_Changed |= 1; //Changed!
			}
		}
		break;
	}
	BIOS_Menu = 1; //Return to Disk Menu!
	if ((newtype != currentCMOS->floppy0_nodisk_type) && confirmed) //Changed?
	{
		BIOS_Changed |= 1; //We've changed!
		lock(LOCK_CPU); //Lock the CPU: we're going to change something in active emulation!
		CMOS.Loaded = 1; //Unload the CMOS: discard anything that's loaded when saving!
		CMOS.DATA.floppy0_nodisk_type = newtype; //Reverse!
		unlock(LOCK_CPU); //We're finished with the main thread!
		CMOSGLOBALBACKUPDATA backupglobal;
		if (is_i430fx == 2) //i440fx?
		{
			if (!BIOS_Settings.got_i440fxCMOS)
			{
				backupCMOSglobalsettings(&BIOS_Settings.i440fxCMOS, &backupglobal); //Backup the memory field!
				memset(&BIOS_Settings.i440fxCMOS, 0, sizeof(BIOS_Settings.i440fxCMOS)); //Init!
				restoreCMOSglobalsettings(&BIOS_Settings.i440fxCMOS, &backupglobal); //Backup the memory field!
			}
			BIOS_Settings.i440fxCMOS.floppy0_nodisk_type = result; //Reverse!
			BIOS_Settings.got_i440fxCMOS = 1; //We hav gotten a CMOS!
		}
		else if (is_i430fx == 1) //i430fx?
		{
			if (!BIOS_Settings.got_i430fxCMOS)
			{
				backupCMOSglobalsettings(&BIOS_Settings.i430fxCMOS, &backupglobal); //Backup the memory field!
				memset(&BIOS_Settings.i430fxCMOS, 0, sizeof(BIOS_Settings.i430fxCMOS)); //Init!
				restoreCMOSglobalsettings(&BIOS_Settings.i430fxCMOS, &backupglobal); //Backup the memory field!
			}
			BIOS_Settings.i430fxCMOS.floppy0_nodisk_type = result; //Reverse!
			BIOS_Settings.got_i430fxCMOS = 1; //We hav gotten a CMOS!
		}
		else if (is_PS2) //PS/2?
		{
			if (!BIOS_Settings.got_PS2CMOS)
			{
				backupCMOSglobalsettings(&BIOS_Settings.PS2CMOS, &backupglobal); //Backup the memory field!
				memset(&BIOS_Settings.PS2CMOS, 0, sizeof(BIOS_Settings.PS2CMOS)); //Init!
				restoreCMOSglobalsettings(&BIOS_Settings.PS2CMOS, &backupglobal); //Backup the memory field!
			}
			BIOS_Settings.PS2CMOS.floppy0_nodisk_type = result; //Reverse!
			BIOS_Settings.got_PS2CMOS = 1; //We hav gotten a CMOS!
		}
		else if (is_Compaq)
		{
			if (!BIOS_Settings.got_CompaqCMOS)
			{
				backupCMOSglobalsettings(&BIOS_Settings.CompaqCMOS, &backupglobal); //Backup the memory field!
				memset(&BIOS_Settings.CompaqCMOS, 0, sizeof(BIOS_Settings.CompaqCMOS)); //Init!
				restoreCMOSglobalsettings(&BIOS_Settings.CompaqCMOS, &backupglobal); //Backup the memory field!
			}
			BIOS_Settings.CompaqCMOS.floppy0_nodisk_type = result; //Reverse!
			BIOS_Settings.got_CompaqCMOS = 1; //We hav gotten a CMOS!
		}
		else if (is_XT)
		{
			if (!BIOS_Settings.got_XTCMOS)
			{
				backupCMOSglobalsettings(&BIOS_Settings.XTCMOS, &backupglobal); //Backup the memory field!
				memset(&BIOS_Settings.XTCMOS, 0, sizeof(BIOS_Settings.XTCMOS)); //Init!
				restoreCMOSglobalsettings(&BIOS_Settings.XTCMOS, &backupglobal); //Backup the memory field!
			}
			BIOS_Settings.XTCMOS.floppy0_nodisk_type = result; //Reverse!
			BIOS_Settings.got_XTCMOS = 1; //We hav gotten a CMOS!
		}
		else //AT?
		{
			if (!BIOS_Settings.got_ATCMOS)
			{
				backupCMOSglobalsettings(&BIOS_Settings.ATCMOS, &backupglobal); //Backup the memory field!
				memset(&BIOS_Settings.ATCMOS, 0, sizeof(BIOS_Settings.ATCMOS)); //Init!
				restoreCMOSglobalsettings(&BIOS_Settings.ATCMOS, &backupglobal); //Backup the memory field!
			}
			BIOS_Settings.ATCMOS.floppy0_nodisk_type = result; //Reverse!
			BIOS_Settings.got_ATCMOS = 1; //We hav gotten a CMOS!
		}
	}
}

void BIOS_floppy1_nodisk_type()
{
	byte i;
	char filename[256]; //Filename container!
	cleardata(&filename[0], sizeof(filename)); //Init!
	for (i = 0; i < NUMFLOPPYGEOMETRIES; i++) //Process all geometries into a list!
	{
		memset(&itemlist[i], 0, sizeof(itemlist[i])); //Reset!
		strcpy(itemlist[i], floppygeometries[i].text);
	}
	numlist = NUMFLOPPYGEOMETRIES; //The size of the list!

	CMOSDATA *currentCMOS;
	if (is_i430fx==2) //i440fx?
	{
		currentCMOS = &BIOS_Settings.i440fxCMOS; //We've used!
	}
	else if (is_i430fx==1) //i430fx?
	{
		currentCMOS = &BIOS_Settings.i430fxCMOS; //We've used!
	}
	else if (is_PS2) //PS/2?
	{
		currentCMOS = &BIOS_Settings.PS2CMOS; //We've used!
	}
	else if (is_Compaq)
	{
		currentCMOS = &BIOS_Settings.CompaqCMOS; //We've used!
	}
	else if (is_XT)
	{
		currentCMOS = &BIOS_Settings.XTCMOS; //We've used!
	}
	else //AT?
	{
		currentCMOS = &BIOS_Settings.ATCMOS; //We've used!
	}

	BIOS_Title("Default Floppy B without disk type");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Floppy without disk type: "); //Show selection init!
	EMU_unlocktext();
	int result;
	result = ExecuteList(26, 4, itemlist[currentCMOS->floppy1_nodisk_type], 256, NULL,0); //Get our result!
	int newtype;
	byte confirmed;
	newtype = currentCMOS->floppy1_nodisk_type; //Old type!
	confirmed = 0; //Default: not confirmed!
	switch (result) //Which result?
	{
	case FILELIST_DEFAULT: //Unmount?
		BIOS_Changed |= 1; //Changed!
		newtype = 0; //Default!
		confirmed = 1; //Confirmed!
		break;
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break;
	default: //File?
		if ((result >= 0) && (result < NUMFLOPPYGEOMETRIES)) //Valid item?
		{
			if (result != currentCMOS->floppy1_nodisk_type) //Changed?
			{
				newtype = result; //Set the new value!
				BIOS_Changed |= 1; //Changed!
				confirmed = 1; //Confirmed!
			}
		}
		break;
	}
	BIOS_Menu = 1; //Return to Disk Menu!

	if ((newtype != currentCMOS->floppy1_nodisk_type) && confirmed) //Changed?
	{
		BIOS_Changed = 1; //We've changed!
		lock(LOCK_CPU); //Lock the CPU: we're going to change something in active emulation!
		CMOS.Loaded = 1; //Unload the CMOS: discard anything that's loaded when saving!
		CMOS.DATA.floppy1_nodisk_type = newtype; //Reverse!
		unlock(LOCK_CPU); //We're finished with the main thread!
		CMOSGLOBALBACKUPDATA backupglobal;
		if (is_i430fx == 2) //i440fx?
		{
			if (!BIOS_Settings.got_i440fxCMOS)
			{
				backupCMOSglobalsettings(&BIOS_Settings.i440fxCMOS, &backupglobal); //Backup the memory field!
				memset(&BIOS_Settings.i440fxCMOS, 0, sizeof(BIOS_Settings.i440fxCMOS)); //Init!
				restoreCMOSglobalsettings(&BIOS_Settings.i440fxCMOS, &backupglobal); //Backup the memory field!
			}
			BIOS_Settings.i440fxCMOS.floppy1_nodisk_type = result; //Reverse!
			BIOS_Settings.got_i440fxCMOS = 1; //We hav gotten a CMOS!
		}
		else if (is_i430fx == 1) //i430fx?
		{
			if (!BIOS_Settings.got_i430fxCMOS)
			{
				backupCMOSglobalsettings(&BIOS_Settings.i430fxCMOS, &backupglobal); //Backup the memory field!
				memset(&BIOS_Settings.i430fxCMOS, 0, sizeof(BIOS_Settings.i430fxCMOS)); //Init!
				restoreCMOSglobalsettings(&BIOS_Settings.i430fxCMOS, &backupglobal); //Backup the memory field!
			}
			BIOS_Settings.i430fxCMOS.floppy1_nodisk_type = result; //Reverse!
			BIOS_Settings.got_i430fxCMOS = 1; //We hav gotten a CMOS!
		}
		else if (is_PS2) //PS/2?
		{
			if (!BIOS_Settings.got_PS2CMOS)
			{
				backupCMOSglobalsettings(&BIOS_Settings.PS2CMOS, &backupglobal); //Backup the memory field!
				memset(&BIOS_Settings.PS2CMOS, 0, sizeof(BIOS_Settings.PS2CMOS)); //Init!
				restoreCMOSglobalsettings(&BIOS_Settings.PS2CMOS, &backupglobal); //Backup the memory field!
			}
			BIOS_Settings.PS2CMOS.floppy1_nodisk_type = result; //Reverse!
			BIOS_Settings.got_PS2CMOS = 1; //We hav gotten a CMOS!
		}
		else if (is_Compaq)
		{
			if (!BIOS_Settings.got_CompaqCMOS)
			{
				backupCMOSglobalsettings(&BIOS_Settings.CompaqCMOS, &backupglobal); //Backup the memory field!
				memset(&BIOS_Settings.CompaqCMOS, 0, sizeof(BIOS_Settings.CompaqCMOS)); //Init!
				restoreCMOSglobalsettings(&BIOS_Settings.CompaqCMOS, &backupglobal); //Backup the memory field!
			}
			BIOS_Settings.CompaqCMOS.floppy1_nodisk_type = result; //Reverse!
			BIOS_Settings.got_CompaqCMOS = 1; //We hav gotten a CMOS!
		}
		else if (is_XT)
		{
			if (!BIOS_Settings.got_XTCMOS)
			{
				backupCMOSglobalsettings(&BIOS_Settings.XTCMOS, &backupglobal); //Backup the memory field!
				memset(&BIOS_Settings.XTCMOS, 0, sizeof(BIOS_Settings.XTCMOS)); //Init!
				restoreCMOSglobalsettings(&BIOS_Settings.XTCMOS, &backupglobal); //Backup the memory field!
			}
			BIOS_Settings.XTCMOS.floppy1_nodisk_type = result; //Reverse!
			BIOS_Settings.got_XTCMOS = 1; //We hav gotten a CMOS!
		}
		else //AT?
		{
			if (!BIOS_Settings.got_ATCMOS)
			{
				backupCMOSglobalsettings(&BIOS_Settings.ATCMOS, &backupglobal); //Backup the memory field!
				memset(&BIOS_Settings.ATCMOS, 0, sizeof(BIOS_Settings.ATCMOS)); //Init!
				restoreCMOSglobalsettings(&BIOS_Settings.ATCMOS, &backupglobal); //Backup the memory field!
			}
			BIOS_Settings.ATCMOS.floppy1_nodisk_type = result; //Reverse!
			BIOS_Settings.got_ATCMOS = 1; //We hav gotten a CMOS!
		}
	}
}

void BIOS_CPUIDmode()
{
	BIOS_Title("CPUID mode");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "CPUID mode: "); //Show selection init!
	EMU_unlocktext();
	int i = 0; //Counter!
	numlist = 3; //Amount of Synchronization modes!
	for (i = 0; i < numlist; i++) //Process options!
	{
		cleardata(&itemlist[i][0], sizeof(itemlist[i])); //Reset!
	}
	safestrcpy(itemlist[0], sizeof(itemlist[0]), "Modern mode"); //Set filename from options!
	safestrcpy(itemlist[1], sizeof(itemlist[0]), "Limited to leaf 1"); //Set filename from options!
	safestrcpy(itemlist[2], sizeof(itemlist[0]), "Set to DX on start"); //Set filename from options!
	int current = 0;
	switch (*(getarchCPUIDmode())) //What setting?
	{
	case 0: //Valid
	case 1: //Valid
	case 2: //Valid
		current = *(getarchCPUIDmode()); //Valid: use!
		break;
	default: //Invalid
		current = DEFAULT_CPUIDMODE; //Default: none!
		break;
	}
	if (*(getarchCPUIDmode()) != current) //Invalid?
	{
		*(getarchCPUIDmode()) = current; //Safety!
		BIOS_Changed = 1; //Changed!
	}
	int file = ExecuteList(12, 4, itemlist[current], 256, NULL, 0); //Show options for the installed CPU!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = DEFAULT_CPUIDMODE; //Default setting: Disabled!

	case 0:
	case 1:
	case 2:
	default: //Changed?
		if (file != current) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			*(getarchCPUIDmode()) = file; //Select Data bus size setting!
		}
		break;
	}
	BIOS_Menu = 35; //Goto CPU menu!
}

void BIOS_connectdisconnectpassthrough()
{
	if (!modem_passthrough())
	{
		BIOS_Menu = 8; //Return to Advanced Menu!	
		return; //Not supported when not in passthrough mode!
	}
	if (modem_connected()) //Already connected? Hang up!
	{
		modem_hangup(); //Hang up the phone!
		BIOS_Menu = 8; //Return to Advanced Menu!	
		return; //Don't dial again!
	}
	char filename[256]; //Filename container!
	BIOSClearScreen(); //Clear the screen!
	memset(&filename[0], 0, sizeof(filename)); //Init!
	BIOS_Title("Connect passthrough"); //Full clear!
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto position for info!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Address: "); //Show the filename!
	EMU_unlocktext();
	if (BIOS_InputText(9, 4, &filename[0], 255 - 4, 1)) //Input text confirmed?
	{
		if (strcmp(filename, "") != 0) //Got input?
		{
			connectModem(filename); //Try and connect to the other side!
		}
	}
	BIOS_Menu = 8; //Return to Advanced Menu!	
}

void BIOS_nullModem()
{
	BIOS_Title("Nullmodem mode");
	EMU_locktext();
	EMU_gotoxy(0, 4); //Goto 4th row!
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Nullmodem mode: "); //Show selection init!
	EMU_unlocktext();
	int i = 0; //Counter!
	numlist = 4; //Amount of Synchronization modes!
	for (i = 0; i < numlist; i++) //Process options!
	{
		cleardata(&itemlist[i][0], sizeof(itemlist[i])); //Reset!
	}

	safestrcpy(itemlist[0], sizeof(itemlist[0]), "Normal modem"); //Set filename from options!
	safestrcpy(itemlist[1], sizeof(itemlist[0]), "Simple nullmodem cable"); //Set filename from options!
	safestrcpy(itemlist[2], sizeof(itemlist[0]), "Nullmodem cable with line signalling"); //Set filename from options!
	safestrcpy(itemlist[3], sizeof(itemlist[0]), "Nullmodem cable with line signalling and outgoing manual connect using phonebook entry #0"); //Set filename from options!

	int current = 0;
	switch (BIOS_Settings.nullmodem) //What setting?
	{
	case 0: //Valid
	case 1: //Valid
	case 2: //Valid
	case 3: //Valid
		current = BIOS_Settings.nullmodem; //Valid: use!
		break;
	default: //Invalid
		current = DEFAULT_NULLMODEM; //Default: none!
		break;
	}
	if (BIOS_Settings.nullmodem != current) //Invalid?
	{
		BIOS_Settings.nullmodem = current; //Safety!
		BIOS_Changed = 1; //Changed!
	}
	int file = ExecuteList(16, 4, itemlist[current], 256, NULL, 0); //Show options for the installed CPU!
	switch (file) //Which file?
	{
	case FILELIST_CANCEL: //Cancelled?
		//We do nothing with the selected disk!
		break; //Just calmly return!
	case FILELIST_DEFAULT: //Default?
		file = DEFAULT_NULLMODEM; //Default setting: Disabled!

	case 0:
	case 1:
	case 2:
	default: //Changed?
		if (file != current) //Not current?
		{
			BIOS_Changed = 1; //Changed!
			reboot_needed |= 1; //We need to reboot to apply the ATA changes!
			BIOS_Settings.nullmodem = file; //Select Data bus size setting!
		}
		break;
	}
	BIOS_Menu = 8; //Return to Advanced Menu!	
}

void BIOS_versionInformation()
{
	BIOS_Title("Version information");
	EMU_locktext();
	EMU_textcolor(BIOS_ATTR_INACTIVE); //We're using inactive color for label!
	GPU_EMU_printscreen(0, 4, "Version information:"); //Show selection init!
	GPU_EMU_printscreen(0, 5, "Build version: ");
#ifdef GITVERSION
	GPU_EMU_printscreen(0, 7, "Build %s", GITVERSION); //Show the version information!
#else
	//No version information available?
	GPU_EMU_printscreen(0, 7, "Built without version information!");
#endif
	EMU_unlocktext();
	BIOS_WaitForInput(); //Wait for any input!
	BIOS_Menu = 0; //Return to Main Menu!	
}
