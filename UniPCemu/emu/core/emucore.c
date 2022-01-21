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

#include "headers/types.h" //For global stuff etc!
#include "headers/cpu/mmu.h" //For MMU
#include "headers/cpu/cpu.h" //For CPU
#include "headers/emu/debugger/debugger.h" //Debugger support!
#include "headers/hardware/vga/vga.h" //For savestate support!
#include "headers/hardware/pic.h" //Interrupt controller support!
#include "headers/emu/timers.h" //Timers!
#include "headers/hardware/8042.h" //For void BIOS_init8042()
#include "headers/emu/sound.h" //PC speaker support!
#include "headers/hardware/8253.h" //82C54 support!
#include "headers/hardware/ports.h" //Port support!
#include "headers/support/log.h" //Log support!
#include "headers/support/zalloc.h" //For final freezall functionality!
#include "headers/hardware/adlib.h" //Adlib!
#include "headers/hardware/ps2_keyboard.h" //PS/2 keyboard support!
#include "headers/hardware/ps2_mouse.h" //PS/2 mouse support!
#include "headers/hardware/cmos.h" //CMOS support!
#include "headers/emu/emu_bios_sound.h" //BIOS sound support!
#include "headers/hardware/sermouse.h" //Serial mouse support!
#include "headers/mmu/mmuhandler.h" //MMU handler support!
#include "headers/bios/biosmenu.h" //For running the BIOS!
#include "headers/cpu/easyregs.h" //Easy register support!
#include "headers/emu/emu_misc.h" //RandomShort support!

//All graphics now!
#include "headers/interrupts/interrupt10.h" //Interrupt 10h support!
#include "headers/emu/gpu/gpu_renderer.h" //Renderer support!
#include "headers/emu/gpu/gpu_framerate.h" //Framerate support!
#include "headers/emu/emucore.h" //Emulation core!
#include "headers/interrupts/interrupt19.h" //INT19 support!
#include "headers/hardware/softdebugger.h" //Software debugger and Port E9 Hack.
#include "headers/hardware/8237A.h" //DMA Controller!
#include "headers/hardware/midi/midi.h" //MIDI/MPU support!
#include "headers/bios/biosrom.h" //BIOS ROM support!
#include "headers/emu/threads.h" //Multithreading support!
#include "headers/hardware/vga/vga_renderer.h" //VGA renderer for direct MAX speed dump!
#include "headers/hardware/vga/vga_dacrenderer.h" //DAC support!
#include "headers/hardware/uart.h" //UART support!
#include "headers/emu/emu_vga.h" //VGA update support!
#include "headers/support/highrestimer.h" //High resolution timer!
#include "headers/hardware/ide.h" //IDE/ATA support!
#include "headers/hardware/pci.h" //PCI support!
#include "headers/hardware/sermouse.h" //Serial mouse support!
#include "headers/emu/gpu/gpu_text.h" //GPU text surface support!
#include "headers/basicio/io.h" //I/O support!
#include "headers/hardware/floppy.h" //Floppy disk controller!
#include "headers/hardware/ppi.h" //PPI support!
#include "headers/hardware/ems.h" //EMS support!
#include "headers/hardware/ssource.h" //Disney Sound Source support!
#include "headers/hardware/parallel.h" //Parallel port support!
#include "headers/hardware/vga/vga_cga_mda.h" //CGA/MDA compatibility layer support!
#include "headers/support/dro.h" //DRO player playback support!
#include "headers/hardware/midi/midi.h" //MPU support!
#include "headers/hardware/dram.h" //DRAM support!
#include "headers/hardware/vga/svga/tseng.h" //Tseng ET3000/ET4000 SVGA card!
#include "headers/hardware/joystick.h" //Joystick support!
#include "headers/hardware/xtexpansionunit.h" //XT expansion unit!
#include "headers/cpu/cb_manager.h" //For handling callbacks!
#include "headers/hardware/gameblaster.h" //Game blaster support!
#include "headers/hardware/soundblaster.h" //Sound blaster support!
#include "headers/cpu/easyregs.h" //Flag support!
#include "headers/cpu/protection.h" //Save fault data support!
#include "headers/cpu/biu.h" //BIU support!
#include "headers/cpu/cpu_execution.h" //Execution support!

#include "headers/support/mid.h" //MIDI player support!

#include "headers/hardware/inboard.h" //Inboard support!
#include "headers/cpu/biu.h" //For checking if we're able to HLT and lock!
#include "headers/hardware/modem.h" //Modem support!
#include "headers/hardware/i430fx.h" //i430fx support!

//Emulator single step address, when enabled.
byte doEMUsinglestep[5] = { 0,0,0,0,0 }; //CPU mode plus 1
uint_64 singlestepaddress[5] = { 0,0,0,0,0 }; //The segment:offset address!
byte doEMUtasksinglestep = 0; //Enabled?
uint_64 singlestepTaskaddress = 0; //The segment:offset address!
byte doEMUFSsinglestep = 0; //Enabled?
uint_64 singlestepFSaddress = 0; //The segment:offset address!
byte doEMUCR3singlestep = 0; //Enabled?
uint_64 singlestepCR3address = 0; //The segment:offset address!
extern byte allow_debuggerstep; //Disabled by default: needs to be enabled by our BIOS!

//Log when running bogus(empty) memory?
//#define LOG_BOGUS 2

//CPU default clock speeds (in Hz)!

//The clock speed of the 8086 (~14.31818MHz divided by 3)!
#ifdef IS_LONGDOUBLE
#define CPU808X_CLOCK (MHZ14/3.0L)
#define CPU808X_TURBO_CLOCK (MHZ14/3.0L)*2.1L
#else
#define CPU808X_CLOCK (MHZ14/3.0)
#define CPU808X_TURBO_CLOCK (MHZ14/3.0)*2.1
#endif

//80286 clock is set so that the DRAM refresh ends up with a count of F952h in CX.
//Original 8086 timing adjustment:
//#define CPU80286_CLOCK 7280500.0
//The AT runs an 6MHz 80286(2nd revision)! Although most sources say 8 MHz(MIPS etc.), this is the third revision of the motherboard?
#ifdef IS_LONGDOUBLE
#define CPU80286_CLOCK 6000000.0L
#else
#define CPU80286_CLOCK 6000000.0
#endif

//Inboard 386 runs at 16MHz.
#ifdef IS_LONGDOUBLE
#define CPU80386_INBOARD_XT_CLOCK 16000000.0L
// https://www.flickr.com/photos/11812307@N03/sets/72157643447132144/ says Inboard 386/AT runs at 32MHz.
#define CPU80386_INBOARD_AT_CLOCK 32000000.0L
#else
#define CPU80386_INBOARD_XT_CLOCK 16000000.0
// https://www.flickr.com/photos/11812307@N03/sets/72157643447132144/ says Inboard 386/AT runs at 32MHz.
#define CPU80386_INBOARD_AT_CLOCK 32000000.0
#endif

//Compaq 386 runs at 16MHz.
#ifdef IS_LONGDOUBLE
#define CPU80386_COMPAQ_CLOCK 16000000.0L
//Compaq Deskpro 486DX/33M runs at 33MHz.
#define CPU80486_COMPAQ_CLOCK 33000000.0L
//440FX runs at 50MHz.
#define CPU440FX_CLOCK 66000000.0L
#else
#define CPU80386_COMPAQ_CLOCK 16000000.0
//Compaq Deskpro 486DX/33M runs at 33MHz.
#define CPU80486_COMPAQ_CLOCK 33000000.0
//440FX runs at 50MHz.
#define CPU440FX_CLOCK 66000000.0
#endif
//Timeout CPU time and instruction interval! 44100Hz or 1ms!
#define TIMEOUT_INTERVAL 100
#define TIMEOUT_TIME 16000000

//Allow GPU rendering (to show graphics)?
#define ALLOW_GRAPHICS 1
//To show the framerate?
#define DEBUG_FRAMERATE 1
//All external variables!
extern byte EMU_RUNNING; //Emulator running? 0=Not running, 1=Running, Active CPU, 2=Running, Inactive CPU (BIOS etc.)
extern byte reset; //To fully reset emu?
extern uint_32 romsize; //For checking if we're running a ROM!
extern PIC i8259; //PIC processor!
extern byte motherboard_responds_to_shutdown; //Motherboard responds to shutdown?

DOUBLE MHZ14tick = (1000000000/(DOUBLE)MHZ14); //Time of a 14 MHZ tick!
DOUBLE MHZ14_ticktiming = 0.0; //Timing of the 14MHz clock!
DOUBLE Pentiumtick = (1000000000 / (((DOUBLE)33333333.0)+(1.0/3.0))); //Timing of the Pentium IPS TimeStamp Counter clock, at 33MHz!

extern byte useIPSclock; //Are we using the IPS clock instead of cycle accurate clock?

int emu_started = 0; //Emulator started (initEMU called)?

//To debug init/doneemu?
#define DEBUG_EMU 0

//Report a memory leak has occurred?
//#define REPORT_MEMORYLEAK

extern GPU_TEXTSURFACE *frameratesurface;

byte currentbusy[6] = {0,0,0,0,0,0}; //Current busy status; default none!
byte activebusy[6] = { 0,0,0,0,0,0 }; //Current busy status; default none!

void updateEMUSingleStep(byte index) //Update our single-step address!
{
	switch ((BIOS_Settings.breakpoint[index]>>SETTINGS_BREAKPOINT_MODE_SHIFT)) //What mode?
	{
		case 0: //Unset?
			unknownmode:
			doEMUsinglestep[index] = 0; //Nothing!
			singlestepaddress[index] = 0; //Nothing!
			break;
		case 1: //Real mode
			doEMUsinglestep[index] = CPU_MODE_REAL+1; //Real mode breakpoint!
			goto applybreakpoint;
		case 2: //Protected mode
			doEMUsinglestep[index] = CPU_MODE_PROTECTED+1; //Protected mode breakpoint!
			goto applybreakpoint;
		case 3: //Virtual 8086 mode
			doEMUsinglestep[index] = CPU_MODE_8086+1; //Virtual 8086 mode breakpoint!
			applybreakpoint: //Apply the other breakpoints as well!
			switch (doEMUsinglestep[index]-1)
			{
				case CPU_MODE_REAL: //Real mode?
					//High 16 bits are CS, low 16 bits are IP
					singlestepaddress[index] = ((((BIOS_Settings.breakpoint[index]>>SETTINGS_BREAKPOINT_IGNOREEIP_SHIFT)&1)<<48) | (((BIOS_Settings.breakpoint[index]>>SETTINGS_BREAKPOINT_IGNOREADDRESS_SHIFT)&1)<<49) | (((BIOS_Settings.breakpoint[index]>>SETTINGS_BREAKPOINT_IGNORESEGMENT_SHIFT)&1)<<50) | (((BIOS_Settings.breakpoint[index] >> SETTINGS_BREAKPOINT_SINGLESTEP_SHIFT) & 1) << 51) | (((BIOS_Settings.breakpoint[index]>>SETTINGS_BREAKPOINT_SEGMENT_SHIFT)&SETTINGS_BREAKPOINT_SEGMENT_MASK)<<16) | ((BIOS_Settings.breakpoint[index]&SETTINGS_BREAKPOINT_OFFSET_MASK) & 0xFFFF)); //Single step address!
					break;
				case CPU_MODE_PROTECTED: //Protected mode?
				case CPU_MODE_8086: //Virtual 8086 mode?
					//High 16 bits are CS, low 32 bits are EIP
					singlestepaddress[index] = ((((BIOS_Settings.breakpoint[index]>>SETTINGS_BREAKPOINT_IGNOREEIP_SHIFT)&1)<<48) | (((BIOS_Settings.breakpoint[index]>>SETTINGS_BREAKPOINT_IGNOREADDRESS_SHIFT)&1)<<49) | (((BIOS_Settings.breakpoint[index]>>SETTINGS_BREAKPOINT_IGNORESEGMENT_SHIFT)&1)<<50) | (((BIOS_Settings.breakpoint[index] >> SETTINGS_BREAKPOINT_SINGLESTEP_SHIFT) & 1) << 51) | (((BIOS_Settings.breakpoint[index]>>SETTINGS_BREAKPOINT_SEGMENT_SHIFT)&SETTINGS_BREAKPOINT_SEGMENT_MASK)<<32) | ((BIOS_Settings.breakpoint[index]&SETTINGS_BREAKPOINT_OFFSET_MASK) & 0xFFFFFFFF)); //Single step address!
					break;
				default: //Just to be sure!
					goto unknownmode; //Count as unknown/unset!
					break;
			}
			break;
		default:
			break;
	}

	switch ((BIOS_Settings.taskBreakpoint>>SETTINGS_TASKBREAKPOINT_ENABLE_SHIFT)) //What mode?
	{
		case 0: //Unset?
			doEMUtasksinglestep = 0;
			singlestepTaskaddress = 0; //Nothing!
			break;
		default: //Enabled
			doEMUtasksinglestep = 1;
			//High 16 bits are TR, low 32 bits are base
			singlestepTaskaddress = ((((BIOS_Settings.taskBreakpoint>>SETTINGS_TASKBREAKPOINT_IGNOREBASE_SHIFT)&1)<<48) | (((BIOS_Settings.taskBreakpoint>>SETTINGS_TASKBREAKPOINT_IGNORESEGMENT_SHIFT)&1)<<50) | (((BIOS_Settings.taskBreakpoint>>SETTINGS_TASKBREAKPOINT_SEGMENT_SHIFT)&SETTINGS_TASKBREAKPOINT_SEGMENT_MASK)<<32) | ((BIOS_Settings.taskBreakpoint&SETTINGS_TASKBREAKPOINT_BASE_MASK) & 0xFFFFFFFF)); //Single step address!
			break;
	}

	switch ((BIOS_Settings.FSBreakpoint >> SETTINGS_FSBREAKPOINT_ENABLE_SHIFT)) //What mode?
	{
	case 0: //Unset?
		doEMUFSsinglestep = 0;
		singlestepFSaddress = 0; //Nothing!
		break;
	default: //Enabled
		doEMUFSsinglestep = 1;
		//High 16 bits are TR, low 32 bits are base
		singlestepFSaddress = ((((BIOS_Settings.FSBreakpoint >> SETTINGS_FSBREAKPOINT_IGNOREBASE_SHIFT) & 1) << 48) | (((BIOS_Settings.FSBreakpoint >> SETTINGS_FSBREAKPOINT_IGNORESEGMENT_SHIFT) & 1) << 50) | (((BIOS_Settings.FSBreakpoint >> SETTINGS_FSBREAKPOINT_SEGMENT_SHIFT) & SETTINGS_FSBREAKPOINT_SEGMENT_MASK) << 32) | ((BIOS_Settings.FSBreakpoint & SETTINGS_FSBREAKPOINT_BASE_MASK) & 0xFFFFFFFF)); //Single step address!
		break;
	}

	switch ((BIOS_Settings.CR3breakpoint>>SETTINGS_CR3BREAKPOINT_ENABLE_SHIFT)) //What mode?
	{
		case 0: //Unset?
			doEMUCR3singlestep = 0;
			singlestepCR3address = 0; //Nothing!
			break;
		default: //Enabled
			doEMUCR3singlestep = 1;
			//High 16 bits are TR, low 32 bits are base
			singlestepCR3address = ((BIOS_Settings.CR3breakpoint&SETTINGS_CR3BREAKPOINT_BASE_MASK) & 0xFFFFFFFF); //Single step address!
			break;
	}
}

void EMU_drawBusy(byte disk) //Draw busy on-screen!
{
	char text[2] = {' ','\0'};
	text[0] = 'A'; //Start with A and increase!
	text[0] += disk; //Increasing disk letter!
	uint_32 busycolor;
	busycolor = (activebusy[disk] == 1) ? RGB(0x00, 0xFF, 0x00) : RGB(0xFF, 0x66, 0x00); //Busy color Read/Write!
	GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 7 + disk, 1); //Goto second row column!
	if (activebusy[disk]) //Busy?
	{
		GPU_textprintf(frameratesurface, busycolor, RGB(00, 00, 00), text);
	}
	else
	{
		GPU_textprintf(frameratesurface, RGB(0x00, 0x00, 0x00), RGB(0x00, 0x00, 0x00), " ");
	}
}

void EMU_drawRecording(byte location)
{
	uint_32 busycolor;
	busycolor = RGB(0xFF, 0x00, 0x00); //Busy color Read/Write!
	GPU_textgotoxy(frameratesurface, GPU_TEXTSURFACE_WIDTH - 7 + 6, 1); //Goto second row column!
	if (sound_isRecording()) //Busy recording?
	{
		GPU_textprintf(frameratesurface, busycolor, RGB(0x00, 0x00, 0x00), "R");
	}
	else
	{
		GPU_textprintf(frameratesurface, RGB(0x00, 0x00, 0x00), RGB(0x00, 0x00, 0x00), " ");
	}
}

void EMU_setDiskBusy(byte disk, byte busy) //Are we busy?
{
	switch (disk) //What disk?
	{
	case FLOPPY0:
		lock(LOCK_DISKINDICATOR);
		currentbusy[0] = busy; //New busy status!
		unlock(LOCK_DISKINDICATOR);
		break;
	case FLOPPY1:
		lock(LOCK_DISKINDICATOR);
		currentbusy[1] = busy; //New busy status!
		unlock(LOCK_DISKINDICATOR);
		break;
	case HDD0:
		lock(LOCK_DISKINDICATOR);
		currentbusy[2] = busy; //New busy status!
		unlock(LOCK_DISKINDICATOR);
		break;
	case HDD1:
		lock(LOCK_DISKINDICATOR);
		currentbusy[3] = busy; //New busy status!
		unlock(LOCK_DISKINDICATOR);
		break;
	case CDROM0:
		lock(LOCK_DISKINDICATOR);
		currentbusy[4] = busy; //New busy status!
		unlock(LOCK_DISKINDICATOR);
		break;
	case CDROM1:
		lock(LOCK_DISKINDICATOR);
		currentbusy[5] = busy; //New busy status!
		unlock(LOCK_DISKINDICATOR);
		break;
	default:
		break;
	}
}

void UniPCemu_onRenderingFrame() //Going to be rendering a frame!
{
	byte i;
	lock(LOCK_DISKINDICATOR);
	for (i = 0; i < 6; ++i)
	{
		activebusy[i] = MAX(currentbusy[i], activebusy[i]); //Take the maximum value for the busy value to be active! Write overrides reads!
	}
	unlock(LOCK_DISKINDICATOR);
}

void UniPCemu_afterRenderingFrameFPS() //When finished rendering an update 10FPS frame!
{
	memset(&activebusy,0,sizeof(activebusy)); //Clear all busy flags!
}

void debugrow(char *text)
{
	if (DEBUG_EMU) dolog("emu",text); //Log it when enabled!
}

/*

Emulator initialisation and destruction!

*/

VGA_Type *MainVGA; //The main VGA chipset!

#ifdef REPORT_MEMORYLEAK
uint_32 initEMUmemory = 0;
#endif

TicksHolder CPU_timing; //CPU timing counter!

extern byte CPU_databussize; //0=16/32-bit bus! 1=8-bit bus when possible (8088/80188)!
extern byte CPUID_mode; //CPUID mode!

extern byte allcleared;

extern char soundfontpath[256]; //The soundfont path!

byte useGameBlaster; //Using the Game blaster?
byte useAdlib; //Using the Adlib?
byte useLPTDAC; //Using the LPT DAC?
byte useSoundBlaster; //Using the Sound Blaster?
byte useMPU; //Using the MPU-401?

byte is_XT = 0; //Are we emulating an XT architecture?
byte is_Compaq = 0; //Are we emulating an Compaq architecture?
byte non_Compaq = 1; //Are we emulating an Compaq architecture?
byte is_PS2 = 0; //Are we emulating PS/2 architecture extensions?
byte is_i430fx = 0; //Are we an i430fx motherboard?
byte emu_log_qemu = 0; //Logging qemu style enabled?

extern BIU_type BIU[MAXCPUS]; //The BIU for the BUS activity reset!

extern PCI_GENERALCONFIG* activePCI_IDE; //PCI IDE handler!

extern byte BIU_buslocked; //BUS locked?
extern byte BUSactive; //Are we allowed to control the BUS? 0=Inactive, 1=CPU, 2=DMA
extern byte numemulatedcpus; //Amount of emulated CPUs!

void emu_raise_resetline(byte resetPendingFlags)
{
	byte whichCPU;
	byte effectivePendingFlags;
	//Affect all CPUs!
	for (whichCPU = 0; whichCPU < numemulatedcpus; ++whichCPU)
	{
		effectivePendingFlags = resetPendingFlags; //Effective flags!
		if (is_i430fx && (effectivePendingFlags == 1)) //8042,PPI,Reset,Shutdown cause on i430fx/i440fx?
		{
			//Pulling CPU INIT# low!
			effectivePendingFlags = 0x8; //Perform an CPU_INIT# instead on all CPUs? This value is shifter upwards to bits 4-7, so to set bit 7, set bit 3!
		}
		if (whichCPU) //MP?
		{
			CPU[whichCPU].resetPending = (effectivePendingFlags&(~4)); //Start pending reset MP!
		}
		else
		{
			CPU[whichCPU].resetPending = effectivePendingFlags; //Start pending reset BSP!
		}
	}
	//Affect the I/O APIC as well, but that's done by the hardware reset on the i440fx!
}

void initEMU(int full) //Init!
{
	char soundfont[256];
	byte multiplier=1;
	doneEMU(); //Make sure we're finished too!

	closeLogFile(0); //Close all log files!

	MHZ14tick = (1000000000/(DOUBLE)MHZ14); //Initialize the 14 MHZ tick timing!
	MHZ14_ticktiming = 0.0; //Default to no time passed yet!

	allcleared = 0; //Not cleared anymore!

	activePCI_IDE = NULL; //Default: let the handlers decide which one to use! Either i430fx or IDE allocates this(whichever comes first).

	motherboard_responds_to_shutdown = 1; //Motherboard responds to shutdown?

	MMU_resetHandlers(NULL); //Reset all memory handlers before starting!

	initTicksHolder(&CPU_timing); //Initialise the ticks holder!

	debugrow("Initializing user input...");
	psp_input_init(); //Make sure input is set up!

#ifdef REPORT_MEMORYLEAK
	initEMUmemory = freemem(); //Log free mem!
#endif

	debugrow("Loading basic BIOS I/O...");
	BIOS_LoadIO(0); //Load basic BIOS I/O, also VGA information, don't show checksum errors!

	autoDetectArchitecture(); //Detect the architecture to use!

	emulated_CPUtype = *(getarchemulated_CPU()); //Read the emulated CPU type and store it!

	//Check for memory requirements of the system!
	if ((*(getarchmemory()) & 0xFFFF) && (EMULATED_CPU >= CPU_80286)) //IBM PC/AT has specific memory requirements? Needs to be 64K aligned!
	{
		*(getarchmemory()) &= ~0xFFFF; //We're forcing a redetection of available memory, if it's needed! Else, just round down memory to the nearest compatible 64K memory!
	}

	numemulatedcpus = *(getarchemulated_CPUs()); //How many CPUs to emulate?

	if ((EMULATED_CPU>=CPU_PENTIUMPRO) && (is_i430fx==2)) //Supports multiple CPUs?
	{
		if (numemulatedcpus == 0) //Auto to maximum?
		{
			numemulatedcpus = MAXCPUS; //Maximum!
		}
		//Otherwise, it's as setup (at least 1)!
		numemulatedcpus = MIN(numemulatedcpus,MAXCPUS); //How many to emulate?
	}
	else
	{
		numemulatedcpus = 1; //Only 1 CPU supported!
	}

	numemulatedcpus = MAX(numemulatedcpus, 1); //At least 1 emulated CPU!

	multiplier = 1; //No multiplier!
	if (emulated_CPUtype==CPU_PENTIUMPRO)
	{
		multiplier = 3; //x3 multiplier (Pentium Pro 66MHz FSB @200MHz CPU)!
	}
	else if (emulated_CPUtype==CPU_PENTIUM2)
	{
		multiplier = 5; //x5 multiplier (Pentium II 66MHz FSB @333MHz CPU)!
	}
	else //Pentium-compatible?
	{
		multiplier = 2; //x2 multiplier (Pentium 66MHz FSB @133Hz CPU)!	
	}

	if (is_i430fx == 2) //66MHz instead?
	{
		Pentiumtick = (1000000000 / (DOUBLE)((66666666.0+(2.0/3.0))*(DOUBLE)multiplier)); //Timing of the Pentium IPS TimeStamp Counter clock, at 66 2/3 MHz!
	}
	else //33MHz?
	{
		Pentiumtick = (1000000000 / (DOUBLE)((33333333.0+(1.0/3.0))*(DOUBLE)multiplier)); //Timing of the Pentium IPS TimeStamp Counter clock, at 33 1/3 MHz!
	}

	debugrow("Initializing I/O port handling...");
	Ports_Init(); //Initialise I/O port support!
	
	debugrow("Initialising PCI...");
	initPCI(); //Initialise PCI support!

	debugrow("Initializing timer service...");
	resetTimers(); //Reset all timers/disable them all!
	
	debugrow("Initialising audio subsystem...");
	resetchannels(); //Reset all channels!

	debugrow("Initializing 8259...");
	init8259(); //Initialise the 8259 (PIC)!

	init_i430fx(); //Enable the i430fx if i430fx extensions are enabled!

	useAdlib = BIOS_Settings.useAdlib|BIOS_Settings.useSoundBlaster; //Adlib used?
	if (useAdlib)
	{
		debugrow("Initialising Adlib...");
		initAdlib(); //Initialise adlib!
	}

	useGameBlaster = BIOS_Settings.useGameBlaster; //Game blaster used (optional in the Sound Blaster)?
	if (useGameBlaster)
	{
		debugrow("Initialising Game Blaster...");
		initGameBlaster(0x220); //Initialise game blaster!
		GameBlaster_setVolume((float)BIOS_Settings.GameBlaster_Volume); //Set the sound source volume!
		setGameBlaster_SoundBlaster(BIOS_Settings.useSoundBlaster?2:0); //Fully Sound Blaster compatible?
	}

	debugrow("Initialising Parallel ports...");
	initParallelPorts(1); //Initialise the Parallel ports (LPT ports)!

	useLPTDAC = BIOS_Settings.useLPTDAC; //LPT DAC used?
	if (useLPTDAC)
	{
		debugrow("Initialising Disney Sound Source...");
		initSoundsource(); //Initialise Disney Sound Source!
		ssource_setVolume((float)BIOS_Settings.SoundSource_Volume); //Set the sound source volume!
	}

	debugrow("Initialising MPU...");
	useMPU = 0; //Are we using the MPU-401?
	if ((strcmp(BIOS_Settings.SoundFont,"")!=0) || (BIOS_Settings.useDirectMIDI)) //Gotten a soundfont?
	{
		memset(&soundfont,0,sizeof(soundfont)); //Init!
		safestrcpy(soundfont,sizeof(soundfont),soundfontpath); //The path to the soundfont!
		safestrcat(soundfont,sizeof(soundfont),"/");
		safestrcat(soundfont,sizeof(soundfont),BIOS_Settings.SoundFont); //The full path to the soundfont!
		useMPU = 1; //Try to use the MPU!
		if (!initMPU(&soundfont[0],BIOS_Settings.useDirectMIDI)) //Initialise our MPU! Use the selected soundfont!
		{
			//We've failed loading!
			memset(&BIOS_Settings.SoundFont, 0, sizeof(BIOS_Settings.SoundFont));
			useMPU = 0; //Don't use the MPU: we're rejected!
		}
	}

	debugrow("Initialising DMA Controllers...");
	initDMA(); //Initialise the DMA Controller!

	//Check if we're allowed to use full Sound Blaster emulation!
	useSoundBlaster = BIOS_Settings.useSoundBlaster; //Sound blaster used?
	//Seperate the Sound Blaster from the MPU: we're allowed to be used without MPU as well!

	if (useSoundBlaster) //Sound Blaster used?
	{
		initSoundBlaster(0x220,((useSoundBlaster-1)|(useGameBlaster?0x80:0))); //Initialise sound blaster with the specified version!
	}
	else //Sound Blaster not used and allowed?
	{
		if (BIOS_Settings.useSoundBlaster) //Sound Blaster specified?
		{
			BIOS_Settings.useSoundBlaster = 0; //Don't allow Sound Blaster emulation!
		}
	}

	debugrow("Initializing PSP OSK...");
	psp_keyboard_init(); //Initialise the PSP's on-screen keyboard for the emulator!

	debugrow("Initializing video...");
	initVideo(DEBUG_FRAMERATE); //Reset video!
	GPU_AspectRatio(BIOS_Settings.aspectratio); //Keep the aspect ratio is cleared by default by the GPU initialisation?
	setGPUFramerate(BIOS_Settings.ShowFramerate); //Show the framerate?

	memset(&currentbusy,0,sizeof(currentbusy)); //Initialise busy status!

	debugrow("Initializing VGA...");	
	//First, VGA allocations for seperate systems!

	uint_32 VRAMSizeBackup;
	VRAMSizeBackup = BIOS_Settings.VRAM_size; //Save the original VRAM size for extensions!

	MainVGA = VGAalloc(0,1,(BIOS_Settings.VGA_Mode==6)?1:(BIOS_Settings.VGA_Mode==7?2:((BIOS_Settings.VGA_Mode==8)?3:0)),BIOS_Settings.video_blackpedestal); //Allocate a main VGA or compatible, automatically by BIOS!
	debugrow("Activating main VGA engine...");
	setActiveVGA(MainVGA); //Initialise primary VGA using the BIOS settings, for the system itself!
	VGA_initTimer(); //Initialise the VGA timer for usage!
	initCGA_MDA(); //Add CGA/MDA support to the VGA as an extension!
	if ((BIOS_Settings.VGA_Mode==6) || (BIOS_Settings.VGA_Mode==7)) SVGA_Setup_TsengET4K(VRAMSizeBackup,BIOS_Settings.ET4000_extensions); //Start the Super VGA card instead if enabled!

	debugrow("Starting video subsystem...");
	if (full) startVideo(); //Start the video functioning!
	
	debugrow("Initializing 8042...");
	BIOS_init8042(); //Init 8042 PS/2 controller!

	debugrow("Initialising keyboard...");
	BIOS_initKeyboard(); //Start up the keyboard!

	debugrow("Initialising mouse...");
	PS2_initMouse(is_PS2); //Start up the mouse! Not supported on the XT, AT, PS/2 and Compaq Deskpro 386!

	//Load all BIOS presets!
	debugrow("Initializing 8253...");
	init8253(); //Init Timer&PC Speaker!

	debugrow("Initialising PC Speaker...");
	initSpeakers(BIOS_Settings.usePCSpeaker); //Initialise the speaker. Enable/disable sound according to the setting!
	
	if (BIOS_Settings.architecture==ARCHITECTURE_XT) //XT architecture?
	{
		debugrow("Initialising EMS...");
		initEMS(2 * MBMEMORY,0); //2MB EMS memory!
	}

	debugrow("Initialising MMU...");
	resetMMU(); //Initialise MMU (we need the BDA from now on!)!

	if (full) //Full start?
	{
		debugrow("Registering BIOS ROM with the MMU...");
		BIOS_registerROM(); //Register the ROMs for usage!
	}

	EMU_update_VGA_Settings(); //Update the VGA Settings to it's default value!
	setupVGA(); //Set the VGA up for int10&CPU usage!

	if (BIOS_Settings.architecture == ARCHITECTURE_XT) //XT architecture?
	{
		debugrow("Registering EMS I/O mapping...");
		initEMS(2 * MBMEMORY, 1); //2MB EMS memory!
	}

	//PPI after VGA because we're dependant on the CGA/MDA only mode!
	debugrow("Initialising PPI...");
	initPPI(BIOS_Settings.diagnosticsportoutput_breakpoint,BIOS_Settings.diagnosticsportoutput_timeout); //Start PPI with our breakpoint settings!

	debugrow("Initialising Single-step breakpoint...");
	updateEMUSingleStep(0); //Start our breakpoint at the specified settings!
	updateEMUSingleStep(1); //Start our breakpoint at the specified settings!
	updateEMUSingleStep(2); //Start our breakpoint at the specified settings!
	updateEMUSingleStep(3); //Start our breakpoint at the specified settings!
	updateEMUSingleStep(4); //Start our breakpoint at the specified settings!

	debugrow("Initializing CPU...");
	CPU_databussize = *(getarchDataBusSize()); //Apply the bus to use for our emulation!
	useIPSclock = *(getarchclockingmode()); //Are we using the IPS clock instead?
	CPUID_mode = *(getarchCPUIDmode()); //CPUID mode!
	BIU_buslocked = 0; //BUS locked?
	BUSactive = 0; //Are we allowed to control the BUS? 0=Inactive, 1=CPU, 2=DMA
	for (activeCPU = 0; activeCPU < MAXCPUS; ++activeCPU)
	{
		BIU[activeCPU].BUSactive = 0; //Nobody's controlling the BUS!
		initCPU(); //Initialise CPU for emulation!
	}
	activeCPU = 0;
	debugrow("Initializing Inboard when required...");
	initInboard(BIOS_Settings.InboardInitialWaitstates?1:0); //Initialise CPU for emulation! Emulate full-speed from the start when requested!
	
	debugrow("Initialising CMOS...");
	initCMOS(); //Initialise the CMOS!
	
	debugrow("Initializing DRAM refresh...");
	initDRAM(); //Initialise the DRAM Refresh!

	debugrow("Initialising UART...");
	initUART(); //Initialise the UART (COM ports)!

	debugrow("Initialising serial mouse...");
	initSERMouse(!is_PS2); //Initilialise the serial mouse for all supported platforms not using PS/2 mouse!

	debugrow("Initialising serial modem...");
	initModem(BIOS_Settings.nullmodem?(byte)(BIOS_Settings.nullmodem+1):1); //Initilialise the serial modem to nullmodem mode or normal mode!

	debugrow("Initialising Floppy Disk Controller...");
	initFDC(); //Initialise the Floppy Disk Controller!

	debugrow("Initialising ATA...");
	initATA();

	debugrow("Initialising port E9 hack and emulator support functionality...");
	BIOS_initDebugger(emu_log_qemu); //Initialise the port E9 hack and emulator support functionality!

	debugrow("Initialising joystick...");
	joystickInit();

	if (is_XT) //XT?
	{
		initXTexpansionunit(); //Initialize the expansion unit!
	}

	//Initialize the normal debugger!
	debugrow("Initialising debugger...");
	initDebugger(); //Initialize the debugger if needed!
	
	if (full) //Full start?
	{
		debugrow("Starting timers...");
		startTimers(0); //Start the timers!
		debugrow("Loading system BIOS ROM...");
		BIOS_load_systemROM(); //Load custom ROM from emulator itself, we don't know about any system ROM!
		clearCBHandlers(); //Reset all callbacks!
		BIOS_initStart(); //Get us ready to load our own BIOS boot sequence, so load the ROM with all required data!
		CPU_flushPIQ(-1); //Make sure the PIQ is up-to-date with out newly mapped ROMs in IPS mode!
	}
	else
	{
		debugrow("No timers enabled.");
	}

	//Finally: signal we're ready!
	emu_started = 1; //We've started!

	updateSpeedLimit(); //Update the speed limit!

	debugrow("Starting VGA...");
	startVGA(); //Start the current VGA!
	BIOS_SaveData(); //Save BIOS settings!

	debugrow("EMU Ready to run.");
}

void doneEMU()
{
	if (emu_started) //Started?
	{
		debugrow("doneEMU: Finishing loaded ROMs...");
		BIOS_finishROMs(); //Release the loaded ROMs from the emulator itself!
		debugrow("doneEMU: Finishing MID player...");
		finishMIDplayer(); //Finish the MID player!
		debugrow("doneEMU: Finishing DRO player...");
		finishDROPlayer(); //Finish the DRO player!
		debugrow("doneEMU: resetTimers");
		resetTimers(); //Stop the timers!
		debugrow("doneEMU: Finishing joystick...");
		joystickDone();
		debugrow("doneEMU: Finishing port E9 hack and emulator support functionality...");
		BIOS_doneDebugger(); //Finish the port E9 hack and emulator support functionality!
		debugrow("doneEMU: Finishing ATA...");
		doneATA(); //Finish the ATA!
		debugrow("doneEMU: Finishing serial modem...");
		doneModem(); //Finish the serial modem, if present!
		debugrow("doneEMU: Finishing serial mouse...");
		doneSERMouse(); //Finish the serial mouse, if present!
		debugrow("doneEMU: Saving CMOS...");
		saveCMOS(); //Save the CMOS!
		debugrow("doneEMU: stopVideo...");
		stopVideo(); //Video can't process without MMU!
		debugrow("doneEMU: Finish keyboard PSP...");
		psp_keyboard_done(); //We're done with the keyboard!
		debugrow("doneEMU: finish active VGA...");
		doneVGA(&MainVGA); //We're done with the VGA!
		debugrow("doneEMU: finish CPU.");
		for (activeCPU = 0;activeCPU<MAXCPUS;++activeCPU)
			doneCPU(); //Finish the CPU!
		activeCPU = 0;
		debugrow("doneEMU: finish MMU...");
		doneMMU(); //Release memory!
		debugrow("doneEMU: finish EMS if enabled...");
		doneEMS(); //Finish EMS!
		debugrow("doneEMU: Finishing PC Speaker...");
		doneSpeakers();
		debugrow("doneEMU: Finishing MPU...");
		doneMPU(); //Finish our MPU!
		debugrow("doneEMU: Finishing Disney Sound Source...");
		doneSoundsource(); //Finish Disney Sound Source!
		debugrow("doneEMU: Finishing Sound Blaster...");
		doneSoundBlaster(); //Finish Sound Blaster!
		debugrow("doneEMU: Finish DMA Controller...");
		doneDMA(); //Initialise the DMA Controller!
		debugrow("doneEMU: Finishing Game Blaster...");
		doneGameBlaster(); //Finish Game Blaster!
		debugrow("doneEMU: Finishing Adlib...");
		doneAdlib(); //Finish adlib!
		debugrow("doneEMU: finish Keyboard chip...");
		BIOS_doneKeyboard(); //Done with the keyboard!
		debugrow("doneEMU: finish Mouse chip...");
		BIOS_doneMouse(); //Done with the mouse!
		debugrow("doneEMU: finish 8042...");
		BIOS_done8042(); //Done with PS/2 communications!
		debugrow("doneEMU: finish i430fx...");
		done_i430fx(); //Done with the i430fx!
		debugrow("doneEMU: reset audio channels...");
		resetchannels(); //Release audio!
		debugrow("doneEMU: Finishing Video...");
		doneVideo(); //Cleanup screen buffers!
		debugrow("doneEMU: Finishing user input...");
		psp_input_done(); //Make sure input is set up!
		debugrow("doneEMU: EMU finished!");
		emu_started = 0; //Not started anymore!
		EMU_RUNNING = 0; //We aren't running anymore!
		BIOS_SaveData(); //Save BIOS settings!
		#ifdef REPORT_MEMORYLEAK
		if (freemem()!=initEMUmemory && initEMUmemory) //Difference?
		{
			logpointers("doneEMU: warning: memory difference before and after allocating EMU services!"); //Log all pointers!
		}
		#endif
	}
	
}

void EMU_stopInput()
{
	save_keyboard_status(); //Save keyboard status to memory!
	disableKeyboard(); //Disable it!
	EMU_enablemouse(0); //Disable all mouse input packets!
}

void EMU_startInput()
{
	load_keyboard_status(); //Load keyboard status from memory!
	enableKeyboard(0); //Enable the keyboard, don't buffer!
	EMU_enablemouse(1); //Enable all mouse input packets!
}

void pauseEMU()
{
	if (emu_started) //Started?
	{
		EMU_stopInput(); //Stop all input!
		stopEMUTimers(); //Stop the timers!
		EMU_RUNNING = 3; //We've stopped, but still active (paused)!
	}
}

void resumeEMU(byte startinput)
{
	if (emu_started) //Started?
	{
		startEMUTimers(); //Start the timers!
		if (startinput) EMU_startInput(); //Start the input when allowed to!
		EMU_RUNNING = 1; //We've restarted!
		cleanKeyboard(); //Clean the keyboard timer!
		cleanMouse(); //Clean the mouse timer!
		cleanAdlib(); //Clean the adlib timer!
		cleanPIT(); //Clean the PIT timers!
		cleanATA(); //Update the ATA timer!
		cleanDMA(); //Update the DMA timer!
	}
}

void initEMUreset() //Simple reset emulator!
{
	debugrow("initEMUreset!");
	debugrow("immediatelyafter");
	EMU_RUNNING = 0; //Emulator isn't running anymore!

	reset = 0; //Not resetting!
//Shutdown check as fast as we can!
	if (shuttingdown()) //Shut down?
	{
		debugrow("shutdown!");
		doneEMU(); //Clean up if needed!
		EMU_Shutdown(0); //Done shutting down!
		quitemu(0); //Shut down!
	}

	debugrow("initemu!");
	initEMU(ALLOW_GRAPHICS); //Initialise the emulator fully!
	debugrow("Ready to run!"); //Ready to run!
}

/* coreHandler: The core emulation handler (running CPU and external hardware required to run it.) */

extern byte BPsinglestep; //Breakpoint-enforced single-step triggered?
extern byte singlestep; //Enable EMU-driven single step!
extern byte interruptsaved; //Primary interrupt saved?

byte HWINT_nr = 0, HWINT_saved = 0; //HW interrupt saved?

extern byte MMU_logging; //Are we logging from the MMU?

extern byte Direct_Input; //Are we in direct input mode?

DOUBLE last_timing = 0.0, timeemulated = 0.0; //Last timing!

DOUBLE CPU_speed_cycle = 0.0; //808X signal cycles by default!

ThreadParams_p BIOSMenuThread; //BIOS pause menu thread!
extern ThreadParams_p debugger_thread; //Debugger menu thread!

void BIOSMenuResumeEMU()
{
	getnspassed(&CPU_timing); //We start off at this point with no time running! We start counting the last timing from now!
	updateSpeedLimit(); //Update the speed limit!
}

void BIOSMenuExecution()
{
	pauseEMU(); //Stop timers!
	if (runBIOS(0)) //Run the emulator BIOS!
	{
		lock(LOCK_CPU); //We're updating the CPU!
		reset = 1; //We're to reset!
		unlock(LOCK_CPU);
	}
	resumeEMU(1); //Resume!
	//Update CPU speed!
	lock(LOCK_CPU); //We're updating the CPU!
	BIOSMenuResumeEMU(); //Resume the emulator from the BIOS menu thread!
	unlock(LOCK_CPU);
}

extern byte TurboMode; //Are we in Turbo mode?

void setCPUCycles(uint_32 cycles)
{
	//Actual clock cycles?
	#ifdef IS_LONGDOUBLE
	CPU_speed_cycle = 1000000000.0L / (DOUBLE)(cycles*1000.0); //Apply the cycles in kHz!	
	#else
	CPU_speed_cycle = 1000000000.0 / (DOUBLE)(cycles*1000.0); //Apply the cycles in kHz!	
	#endif
}

void updateSpeedLimit()
{
	uint_32 CPUSpeed;
	byte is_Turbo=0; //Turbo mode?
	CPUSpeed = *(getarchCPUSpeed()); //Default speed!
	if (TurboMode && *(getarchuseTurboCPUSpeed())) //Turbo Speed enabled?
	{
		CPUSpeed = *(getarchTurboCPUSpeed());
		is_Turbo = 1; //Enable Turbo Speed calculations!
	}

	if (CPUSpeed==0) //Default cycles specified?
	{
		switch (EMULATED_CPU) //What CPU to speed?
		{
			case CPU_8086:
			case CPU_NECV30: //First generation? Use 808X speed!
				if (useIPSclock) //Using the IPS clock?
				{
					setCPUCycles(315); //Default to 315 cycles(4.77MHz)!
				}
				else
				{
					if (is_Turbo) //Turbo speed instead?
					{
						#ifdef IS_LONGDOUBLE
						CPU_speed_cycle = 1000000000.0L / CPU808X_TURBO_CLOCK; //8086 CPU cycle length in us, since no other CPUs are known yet! Use the 10MHz Turbo version by default!					
						#else
						CPU_speed_cycle = 1000000000.0 / CPU808X_TURBO_CLOCK; //8086 CPU cycle length in us, since no other CPUs are known yet! Use the 10MHz Turbo version by default!					
						#endif
					}
					else //Normal speed?
					{
						#ifdef IS_LONGDOUBLE
						CPU_speed_cycle = 1000000000.0L/CPU808X_CLOCK; //8086 CPU cycle length in us, since no other CPUs are known yet!	
						#else
						CPU_speed_cycle = 1000000000.0/CPU808X_CLOCK; //8086 CPU cycle length in us, since no other CPUs are known yet!	
						#endif
					}
				}
				break;
			case CPU_80286: //286?
			case CPU_80386: //386?
			case CPU_80486: //486?
			case CPU_PENTIUM: //586?
			case CPU_PENTIUMPRO: //686?
			case CPU_PENTIUM2: //786?
				if (useIPSclock) //Using the IPS clock?
				{
					switch (EMULATED_CPU) //What CPU, if supported!
					{
					case CPU_80286: //80286 12.5MHz?
						setCPUCycles(2750); //Supported so far! Default cycles!
						break;
					case CPU_80386: //80386 33MHz?
						setCPUCycles(3000); //Supported so far! Default cycles!
						//setCPUCycles(7800); //Supported so far! Default cycles!
						break;
					case CPU_80486: //80486 66MHz?
						setCPUCycles(3000); //Supported so far! Default cycles!
						//setCPUCycles(10000); //Supported so far! Default cycles! Originally 26.8MIPS!
						break;
					case CPU_PENTIUM: //Pentium 100MHz?
					case CPU_PENTIUMPRO: //Pentium Pro 100MHz?
					case CPU_PENTIUM2: //Pentium II 100MHz?
						setCPUCycles(3000); //Supported so far! Default cycles!
						//setCPUCycles(10000); //Supported so far! Default cycles! Original 77MIPS. Now 10MIPs!
						break;
					default: //Unknown?
						setCPUCycles(3000); //Unsupported so far! Default to 3000 cycles!
						break;
					}
				}
				else
				{
					if (is_Turbo) //Turbo speed instead?
					{
						#ifdef IS_LONGDOUBLE
						CPU_speed_cycle = 1000000000.0L / CPU80286_CLOCK; //8086 CPU cycle length in us, since no other CPUs are known yet! Use the 10MHz Turbo version by default!					
						#else
						CPU_speed_cycle = 1000000000.0 / CPU80286_CLOCK; //8086 CPU cycle length in us, since no other CPUs are known yet! Use the 10MHz Turbo version by default!					
						#endif
					}
					else //Normal speed?
					{
						#ifdef IS_LONGDOUBLE
						CPU_speed_cycle = 1000000000.0L / CPU80286_CLOCK; //80286 8MHz for DMA speed check compatibility(Type 3 motherboard)!
						#else
						CPU_speed_cycle = 1000000000.0 / CPU80286_CLOCK; //80286 8MHz for DMA speed check compatibility(Type 3 motherboard)!
						#endif
					}
					if (((EMULATED_CPU==CPU_80386) || (EMULATED_CPU>=CPU_80486)) || (is_Compaq==1) || (is_i430fx)) //80386/80486 or Compaq?
					{
						if (!(is_Compaq||is_i430fx) && ((EMULATED_CPU==CPU_80386)||(EMULATED_CPU>=CPU_80486))) //XT/AT 386? 16MHz clock!
						{
							if (is_XT) //Inboard 386/486 XT?
							{
								#ifdef IS_LONGDOUBLE
								CPU_speed_cycle = 1000000000.0L / CPU80386_INBOARD_XT_CLOCK; //80386/80486 16MHz!
								#else
								CPU_speed_cycle = 1000000000.0 / CPU80386_INBOARD_XT_CLOCK; //80386/80486 16MHz!
								#endif
							}
							else //Inboard 386/486 AT?
							{
								#ifdef IS_LONGDOUBLE
								CPU_speed_cycle = 1000000000.0L / CPU80386_INBOARD_AT_CLOCK; //80386/80486 32MHz(Type 3 motherboard)!
								#else
								CPU_speed_cycle = 1000000000.0 / CPU80386_INBOARD_AT_CLOCK; //80386/80486 32MHz(Type 3 motherboard)!
								#endif
							}
						}
						else if ((is_Compaq==1) || (is_i430fx)) //Compaq Deskpro 386+?
						{
							#ifdef IS_LONGDOUBLE
							CPU_speed_cycle = 1000000000.0L / CPU80386_COMPAQ_CLOCK; //80386 15MHz for DMA speed check compatibility(Type 3 motherboard)!
							#else
							CPU_speed_cycle = 1000000000.0 / CPU80386_COMPAQ_CLOCK; //80386 15MHz for DMA speed check compatibility(Type 3 motherboard)!
							#endif
							if (EMULATED_CPU>=CPU_80486) //80486 default clock instead (Pentium too)?
							{
								if (is_i430fx == 2) //i440fx?
								{
									#ifdef IS_LONGDOUBLE
									CPU_speed_cycle = 1000000000.0L / CPU440FX_CLOCK; //80486 33MHz!
									#else
									CPU_speed_cycle = 1000000000.0 / CPU440FX_CLOCK; //80486 33MHz!
									#endif
								}
								else
								{
									#ifdef IS_LONGDOUBLE
									CPU_speed_cycle = 1000000000.0L / CPU80486_COMPAQ_CLOCK; //80486 33MHz!
									#else
									CPU_speed_cycle = 1000000000.0 / CPU80486_COMPAQ_CLOCK; //80486 33MHz!
									#endif
								}
							}
						}
						//Use AT speed for AT compatiblity for AT architectures!
					}
				}
				break;
			default: //Unknown CPU?
				if (useIPSclock) //Using the IPS clock?
				{
					setCPUCycles(3000); //Default to 3000 cycles!
				}
				else
				{
					setCPUCycles(8000); //Unsupported so far! Default to 8MHz cycles!
				}
				break;
		}
	}
	else //Cycles specified?
	{
		setCPUCycles(CPUSpeed); //Use either Dosbox clock(Instructions per millisecond) or actual clocks when supported!
	}
}

extern byte allcleared;

DOUBLE currenttiming = 0.0; //Current timing spent to emulate!

extern byte Settings_request; //Settings requested to be executed?

extern byte skipstep; //Skip while stepping? 1=repeating, 2=EIP destination, 3=Stop asap.

extern byte haswindowactive; //For detecting paused operation!
extern byte backgroundpolicy; //Background task policy. 0=Full halt of the application, 1=Keep running without video and audio muted, 2=Keep running with audio playback, recording muted, 3=Keep running fully without video.

extern byte lastHLTstatus; //Last halt status for debugger! 1=Was halting, 0=Not halting!

extern byte debugger_is_logging; //Are we logging?
extern byte MMU_waitstateactive; //Waitstate active?

byte applysinglestep;
byte applysinglestepBP;

void calcGenericSinglestep(byte index)
{
	byte appliedBP;
	if (doEMUsinglestep[index] && (getcpumode() == (doEMUsinglestep[index] - 1))) //Single step enabled?
	{
		switch (getcpumode()) //What CPU mode are we to debug?
		{
		case CPU_MODE_REAL: //Real mode?
			appliedBP = ((((REG_CS == ((singlestepaddress[index] >> 16) & 0xFFFF)) | (singlestepaddress[index] & 0x4000000000000ULL)) && ((REG_IP == (singlestepaddress[index] & 0xFFFF)) || (singlestepaddress[index] & 0x1000000000000ULL))) || (singlestepaddress[index] & 0x2000000000000ULL)); //Single step enabled?
			applysinglestep |= appliedBP; //Applying this breakpoint?
			if (unlikely(appliedBP && (singlestepaddress[index] & 0x8000000000000ULL))) //Single step enforced?
			{
				applysinglestepBP = 1; //Apply an enforced single step!
			}
			break;
		case CPU_MODE_PROTECTED: //Protected mode?
		case CPU_MODE_8086: //Virtual 8086 mode?
			appliedBP = ((((REG_CS == ((singlestepaddress[index] >> 32) & 0xFFFF)) | (singlestepaddress[index] & 0x4000000000000ULL)) && ((REG_EIP == (singlestepaddress[index] & 0xFFFFFFFF)) || (singlestepaddress[index] & 0x1000000000000ULL))) || (singlestepaddress[index] & 0x2000000000000ULL)); //Single step enabled?
			applysinglestep |= appliedBP; //Applying this breakpoint?
			if (unlikely(appliedBP && (singlestepaddress[index] & 0x8000000000000ULL))) //Single step enforced?
			{
				applysinglestepBP = 1; //Apply an enforced single step!
			}
			break;
		default: //Invalid mode?
			break;
		}
	}
	else if (unlikely(((doEMUFSsinglestep | doEMUtasksinglestep | doEMUCR3singlestep) && ((doEMUsinglestep[0]|doEMUsinglestep[1]|doEMUsinglestep[2]|doEMUsinglestep[3]|doEMUsinglestep[4]) == 0)))) //Task&CR3 singlestep for any address?
	{
		applysinglestep = 1; //Use combined rights!
	}
	//Use the other breakpoint settings combined and default to 0!
}

OPTINLINE byte coreHandler()
{
	byte multilockack;
	byte lockcounter;
	byte buslocksrequested;
	word destCS;
	uint_32 MHZ14passed; //14 MHZ clock passed?
	byte BIOSMenuAllowed = 1; //Are we allowed to open the BIOS menu?
	//CPU execution, needs to be before the debugger!
	lock(LOCK_INPUT);
	if (unlikely(((haswindowactive & 0x1C) == 0xC))) //Muting recording of the Sound Blaster or earlier and resuming?
	{
		haswindowactive |= 0x10; //Start pending to mute the 
		if (backgroundpolicy < 3) //To apply a background policy?
		{
			if (backgroundpolicy == 0) getnspassed(&CPU_timing); //Completely stopping emulation?
		}
		else if ((haswindowactive & 0x30) == 0x10) //Keep running 100%!
		{
			haswindowactive |= 0x20; //Fully active again(the same the Sound Blaster does usually)? Affect nothing on the emulated side!
		}
	} //Pending to finish Soundblaster!
	currenttiming += likely(((haswindowactive&2)|backgroundpolicy))?getnspassed(&CPU_timing):0; //Check for any time that has passed to emulate! Don't emulate when not allowed to run, keeping emulation paused!
	unlock(LOCK_INPUT);
	uint_64 currentCPUtime = (uint_64)currenttiming; //Current CPU time to update to!
	uint_64 timeoutCPUtime = currentCPUtime+TIMEOUT_TIME; //We're timed out this far in the future (our timing itself)!

	DOUBLE instructiontime,timeexecuted=0.0f,effectiveinstructiontime; //How much time did the instruction last?
	byte timeout = TIMEOUT_INTERVAL; //Check every 10 instructions for timeout!
	if (unlikely((currentCPUtime-last_timing)>2000000000.0)) last_timing = currentCPUtime-1000.0; //Safety: 2 seconds or more(should be impossible normally) becomes 1us.
	for (;last_timing<currentCPUtime;) //CPU cycle loop for as many cycles as needed to get up-to-date!
	{
		if (unlikely(debugger_thread))
		{
			if (threadRunning(debugger_thread)) //Are we running the debugger?
			{
				instructiontime = currentCPUtime - last_timing; //The instruction time is the total time passed!
				updateAudio(instructiontime); //Discard the time passed!
				timeexecuted += instructiontime; //Increase CPU executed time executed this block!
				last_timing += instructiontime; //Increase the last timepoint!
				goto skipCPUtiming; //OK, but skipped!
			}
		}
		if (unlikely(BIOSMenuThread))
		{
			if (threadRunning(BIOSMenuThread)) //Are we running the BIOS menu and not permanently halted? Block our execution!
			{
				if ((CPU[activeCPU].halt&2)==0) //Are we allowed to be halted entirely?
				{
					instructiontime = currentCPUtime - last_timing; //The instruction time is the total time passed!
					updateAudio(instructiontime); //Discard the time passed!
					timeexecuted += instructiontime; //Increase CPU executed time executed this block!
					last_timing += instructiontime; //Increase the last timepoint!
					goto skipCPUtiming; //OK, but skipped!
				}
				BIOSMenuAllowed = 0; //We're running the BIOS menu! Don't open it again!
			}
		}
		if (likely((CPU[activeCPU].halt&2)==0)) //Are we running normally(not partly ran without CPU from the BIOS menu)?
		{
			BIOSMenuThread = NULL; //We don't run the BIOS menu anymore!
		}

		if (unlikely(allcleared)) return 0; //Abort: invalid buffer!

		interruptsaved = 0; //Reset PIC interrupt to not used!

		effectiveinstructiontime = (DOUBLE)0.0f; //Effective time!
		activeCPU = 0; //First CPU!
		do
		{
		//Start handling a CPU!
		if (unlikely(CPU[activeCPU].registers==0)) //We need registers at this point, but have none to use?
		{
			continue; //Invalid registers: abort, since we're invalid!
		}

		CPU_resetTimings(); //Reset all required CPU timings required!

		CPU_tickPendingReset();

		if (unlikely(CPU[activeCPU].waitingforSIPI)) //Waiting for SIPI?
		{
			if (CPU[activeCPU].SIPIreceived&0x100) //Received a command to leave HLT mode with interrupt number?
			{
				CPU[activeCPU].waitingforSIPI = 0; //Interrupt->Resume from HLT
				//Start execution at xx00:0000?
				CPU[activeCPU].destEIP = 0;
				destCS = (CPU[activeCPU].SIPIreceived&0xFF)<<8;
				segmentWritten(CPU_SEGMENT_CS,destCS,1); //Jump to the designated address!
				CPU[activeCPU].exec_lastCS = CPU[activeCPU].exec_CS;
				CPU[activeCPU].exec_lastEIP = CPU[activeCPU].exec_EIP;
				CPU[activeCPU].exec_CS = REG_CS; //Save for error handling!
				CPU[activeCPU].exec_EIP = (REG_EIP & CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS].PRECALCS.roof); //Save for error handling!
				CPU_prepareHWint(); //Prepares the CPU for hardware interrupts!
				CPU_commitState(); //Save fault data to go back to when exceptions occur!
				CPU[activeCPU].SIPIreceived = 0; //Not received anymore!
				goto resumeFromHLT; //We're resuming from HLT state!
			}
			//Otherwise, continue waiting.
		}
		else if (unlikely((CPU[activeCPU].halt&3) && (BIU_Ready() && CPU[activeCPU].resetPending==0))) //Halted normally with no reset pending? Don't count CGA wait states!
		{
			if (unlikely(romsize)) //Debug HLT?
			{
				MMU_dumpmemory("bootrom.dmp"); //Dump the memory to file!
				return 0; //Stop execution!
			}

			acnowledgeirrs(); //Acnowledge IRR!

			//Handle NMI first!
			if (likely(CPU_checkNMIAPIC(1))) //APIC NMI not fired?
			{
				if (likely(CPU_handleNMI(1))) //NMI isn't triggered?
				{
					if (unlikely(FLAG_IF && PICInterrupt() && ((CPU[activeCPU].halt&2)==0))) //We have an interrupt? Clear Halt State when allowed to!
					{
						CPU[activeCPU].halt = 0; //Interrupt->Resume from HLT
						goto resumeFromHLT; //We're resuming from HLT state!
					}
					//Otherwise, still halted!
				}
				else
				{
						CPU[activeCPU].halt = 0; //Interrupt->Resume from HLT
						goto resumeFromHLT; //We're resuming from HLT state!
				}
			}
			else //APIC NMI to handle?
			{
				CPU[activeCPU].halt = 0; //Interrupt->Resume from HLT
				goto resumeFromHLT; //We're resuming from HLT state!
			}

			//Execute using actual CPU clocks!
			CPU[activeCPU].cycles = 1; //HLT takes 1 cycle for now, since it's unknown!
			if (unlikely(CPU[activeCPU].halt==1)) //Normal halt?
			{
				//Increase the instruction counter every instruction/HLT time!
				if (lastHLTstatus != CPU[activeCPU].halt) //Just started halting?
				{
					CPU[activeCPU].cpudebugger = needdebugger(); //Debugging information required? Refresh in case of external activation!
					lastHLTstatus = CPU[activeCPU].halt; //Save for comparision!
				}
				if (CPU[activeCPU].cpudebugger) //Debugging?
				{
					debugger_beforeCPU(); //Make sure the debugger is prepared when needed!
					debugger_setcommand("<HLT>"); //We're a HLT state, so give the HLT command!
				}
				CPU[activeCPU].executed = 1; //For making the debugger execute correctly!
				//Increase the instruction counter every cycle/HLT time!
				if (activeCPU == 0) //Only the first CPU can be debugged!
				{
					debugger_step(); //Step debugger if needed, even during HLT state!
				}
			}
		}
		else //We're not halted? Execute the CPU routines!
		{
		resumeFromHLT:
			if (unlikely(CPU[activeCPU].instructionfetch.CPU_isFetching && (CPU[activeCPU].instructionfetch.CPU_fetchphase==1))) //We're starting a new instruction?
			{
				lastHLTstatus = CPU[activeCPU].halt; //Save the new HLT status!
				HWINT_saved = 0; //No HW interrupt by default!
				CPU_beforeexec(); //Everything before the execution!
				acnowledgeirrs(); //Acnowledge IRR!
				if (unlikely((!CPU[activeCPU].trapped) && CPU[activeCPU].registers && CPU[activeCPU].allowInterrupts && (CPU[activeCPU].permanentreset==0) && (CPU[activeCPU].internalinterruptstep==0) && BIU_Ready() && (CPU_executionphase_busy()==0) && (CPU[activeCPU].instructionfetch.CPU_isFetching && (CPU[activeCPU].instructionfetch.CPU_fetchphase==1)))) //Only check for hardware interrupts when not trapped and allowed to execute interrupts(not permanently reset)!
				{
					//Handle NMI first!
					if (CPU_checkNMIAPIC(0)) //APIC NMI not fired?
					{
						if (likely(CPU_handleNMI(0))) //NMI isn't triggered?
						{
							if (likely(FLAG_IF)) //Interrupts available?
							{
								if (unlikely(PICInterrupt())) //We have a hardware interrupt ready?
								{
									HWINT_nr = nextintr(); //Get the HW interrupt nr!
									HWINT_saved = 2; //We're executing a HW(PIC) interrupt!
									if (likely(((EMULATED_CPU <= CPU_80286) && CPU[activeCPU].REPPending) == 0)) //Not 80386+, REP pending and segment override?
									{
										CPU_8086REPPending(1); //Process pending REPs normally as documented!
									}
									else //Execute the CPU bug!
									{
										CPU_8086REPPending(1); //Process pending REPs normally as documented!
										REG_EIP = CPU[activeCPU].InterruptReturnEIP; //Use the special interrupt return address to return to the last prefix instead of the start!
									}
									CPU[activeCPU].exec_lastCS = CPU[activeCPU].exec_CS;
									CPU[activeCPU].exec_lastEIP = CPU[activeCPU].exec_EIP;
									CPU[activeCPU].exec_CS = REG_CS; //Save for error handling!
									CPU[activeCPU].exec_EIP = (REG_EIP & CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS].PRECALCS.roof); //Save for error handling!
									CPU_prepareHWint(); //Prepares the CPU for hardware interrupts!
									CPU_commitState(); //Save fault data to go back to when exceptions occur!
									call_hard_inthandler(HWINT_nr); //get next interrupt from the i8259, if any!
								}
							}
						}
					}
				}

				if (unlikely(CPU[activeCPU].registers && (CPU[activeCPU].permanentreset == 0) && (CPU[activeCPU].internalinterruptstep == 0) && BIU_Ready() && (CPU_executionphase_busy() == 0) && (CPU[activeCPU].instructionfetch.CPU_isFetching && (CPU[activeCPU].instructionfetch.CPU_fetchphase == 1)))) //Only check for hardware interrupts when not trapped and allowed to execute interrupts(not permanently reset)!
				{
					if (unlikely((activeCPU==0) && CPU[activeCPU].registers && allow_debuggerstep && (doEMUsinglestep[0]|doEMUsinglestep[1]|doEMUsinglestep[2]|doEMUsinglestep[3]|doEMUsinglestep[4]|doEMUtasksinglestep|doEMUFSsinglestep|doEMUCR3singlestep))) //Single step allowed, CPU mode specified?
					{
						applysinglestep = applysinglestepBP = 0; //Init!
						calcGenericSinglestep(0);
						calcGenericSinglestep(1);
						calcGenericSinglestep(2);
						calcGenericSinglestep(3);
						calcGenericSinglestep(4);
						calcGenericSinglestep(5);
						if (unlikely(doEMUtasksinglestep)) //Task filter enabled for breakpoints?
						{
							applysinglestep &= ((((REG_TR == ((singlestepTaskaddress >> 32) & 0xFFFF)) | (singlestepTaskaddress & 0x4000000000000ULL)) && (((CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_TR].PRECALCS.base == (singlestepTaskaddress & 0xFFFFFFFF)) && GENERALSEGMENT_P(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_TR])) || (singlestepTaskaddress & 0x1000000000000ULL))) || (singlestepTaskaddress & 0x2000000000000ULL)); //Single step enabled?
						}
						if (unlikely(doEMUFSsinglestep)) //Task filter enabled for breakpoints?
						{
							applysinglestep &= ((((REG_FS == ((singlestepFSaddress >> 32) & 0xFFFF)) | (singlestepFSaddress & 0x4000000000000ULL)) && (((CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_FS].PRECALCS.base == (singlestepFSaddress & 0xFFFFFFFF)) && GENERALSEGMENT_P(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_FS])) || (singlestepFSaddress & 0x1000000000000ULL))) || (singlestepFSaddress & 0x2000000000000ULL)); //Single step enabled?
						}
						if (unlikely(doEMUCR3singlestep)) //CR3 filter enabled for breakpoints?
						{
							applysinglestep &= (((CPU[activeCPU].registers->CR3&0xFFFFF000) == (singlestepCR3address & 0xFFFFF000))&CPU[activeCPU].is_paging); //Single step enabled?
						}
						singlestep |= applysinglestep; //Apply single step?
						BPsinglestep |= applysinglestepBP; //Apply forced breakpoint on single step?
					}
					CPU[activeCPU].cpudebugger = needdebugger(); //Debugging information required? Refresh in case of external activation!
					MMU_logging = debugger_is_logging; //Are we logging?
					MMU_updatedebugger();
				}

				#ifdef LOG_BOGUS
				uint_32 addr_start, addr_left, curaddr; //Start of the currently executing instruction in real memory! We're testing 5 instructions!
				addr_left=2*LOG_BOGUS;
				curaddr = 0;
				addr_start = CPU_MMU_start(CPU_SEGMENT_CS,REG_CS); //Base of the currently executing block!
				addr_start += REG_EIP; //Add the address for the address we're executing!
			
				for (;addr_left;++curaddr) //Test all addresses!
				{
					if (MMU_directrb_realaddr(addr_start+curaddr)) //Try to read the opcode! Anything found(not 0000h instruction)?
					{
						break; //Stop searching!
					}
					--addr_left; //Tick one address checked!
				}
				if (addr_left==0) //Bogus memory detected?
				{
					dolog("bogus","Bogus exection memory detected(%u 0000h opcodes) at %04X:%08X! Previous instruction: %02X(0F:%u)@%04X:%08X",LOG_BOGUS,REG_CS,REG_EIP,CPU[activeCPU].previousopcode,CPU[activeCPU].previousopcode0F,CPU[activeCPU].exec_lastCS,CPU[activeCPU].exec_lastEIP); //Log the warning of entering bogus memory!
				}
				#endif
			}

			CPU_exec(); //Run CPU!

			//Increase the instruction counter every cycle/HLT time!
			debugger_step(); //Step debugger if needed!
			if (unlikely(CPU[activeCPU].executed)) //Are we executed?
			{
				CB_handleCallbacks(); //Handle callbacks after CPU/debugger usage!
			}
		}
		//Finished handling a CPU!

		//Update current timing with calculated cycles we've executed!
		if (likely(useIPSclock==0)) //Use cycle-accurate clock?
		{
			instructiontime = CPU[activeCPU].cycles*CPU_speed_cycle; //Increase timing with the instruction time!
		}
		else
		{
			instructiontime = ((CPU[activeCPU].executed)|(((BIU[activeCPU]._lock==2)|(BUSactive==2)|(MMU_waitstateactive&1))&1))*CPU_speed_cycle; //Increase timing with the instruction time or bus lock/MMU waitstate timing in IPS clocking mode!
		}

		effectiveinstructiontime = MAX(effectiveinstructiontime,instructiontime); //Maximum CPU time passed!
		} while (++activeCPU<numemulatedcpus); //More CPUs left to handle?

		//Seperate timing for the TSC and APIC to keep them in sync!
		if (unlikely((EMULATED_CPU >= CPU_PENTIUM) && (effectiveinstructiontime>0.0))) //Pentium has a time stamp counter?
		{
			activeCPU = 0;
			do
			{
				//Tick the Pentium TSC and APIC!
				uint_64 clocks;
				CPU[activeCPU].TSCtiming += effectiveinstructiontime; //Time some in realtime!
				if (likely(CPU[activeCPU].TSCtiming >= Pentiumtick)) //Enough to tick?
				{
					clocks = (uint_64)floor(CPU[activeCPU].TSCtiming / Pentiumtick); //How much to tick!
				}
				else
				{
					clocks = 0; //Nothing ticked!
				}
				CPU[activeCPU].TSCtiming -= clocks * Pentiumtick; //Rest the time to keep us constant!
				CPU[activeCPU].TSC += clocks; //Tick the clocks to keep us running!
				updateAPIC(clocks, effectiveinstructiontime); //Clock the APIC as well!
			} while (++activeCPU < numemulatedcpus); //More CPUs left to handle?
		}

		buslocksrequested = 0; //No locks requested!
		activeCPU = 0;
		do
		{
			if (BIU[activeCPU].BUSlockrequested==1) //Requested bus lock?
			{
				++buslocksrequested; //A lock has been requested!
			}
		} while (++activeCPU < numemulatedcpus); //More CPUs left to handle?

		if (buslocksrequested && (BIU_buslocked==0) && (BUSactive!=2)) //BUS locks have been requested and pending?
		{
			if (buslocksrequested==1) //Only 1 CPU requested?
			{
				activeCPU = 0;
				do
				{
					if (BIU[activeCPU].BUSlockrequested==1) //Requested bus lock?
					{
						BIU[activeCPU].BUSlockrequested = 2; //Acnowledged!
						CPU_exec(); //Run CPU!
						//Increase the instruction counter every cycle/HLT time!
						debugger_step(); //Step debugger if needed!
						if (unlikely(CPU[activeCPU].executed)) //Are we executed?
						{
							CB_handleCallbacks(); //Handle callbacks after CPU/debugger usage!
						}
					}
				} while (++activeCPU < numemulatedcpus); //More CPUs left to handle?
			}
			else //Multiple CPUs locking?
			{
				activeCPU = 0;
				lockcounter = 0;
				multilockack = (byte)RandomShort(0,(buslocksrequested-1)); //Random lock ack, equal chance!
				do
				{
					if (BIU[activeCPU].BUSlockrequested==1) //Requested bus lock?
					{
						if (lockcounter==multilockack) //Chosen?
						{
							BIU[activeCPU].BUSlockrequested = 2; //Acnowledged!
							CPU_exec(); //Run CPU!
							//Increase the instruction counter every cycle/HLT time!
							debugger_step(); //Step debugger if needed!
							if (unlikely(CPU[activeCPU].executed)) //Are we executed?
							{
								CB_handleCallbacks(); //Handle callbacks after CPU/debugger usage!
							}
							goto finishLocked; //Finish up!
						}
						++lockcounter; //Next locked test!
					}
				} while (++activeCPU < numemulatedcpus); //More CPUs left to handle?
			}
		}
		finishLocked:

		activeCPU = 0; //Return to the BSP!
		//Now, ticking the hardware!
		instructiontime = effectiveinstructiontime; //Effective instruction time applies!
		last_timing += instructiontime; //Increase CPU time executed!
		timeexecuted += instructiontime; //Increase CPU executed time executed this block!

		//Tick 14MHz master clock, for basic hardware using it!
		MHZ14_ticktiming += instructiontime; //Add time to the 14MHz master clock!
		if (likely(MHZ14_ticktiming<MHZ14tick)) //To not tick some 14MHz clocks? This ix the case with most faster CPUs!
		{
			MHZ14passed = 0; //No time has passed on the 14MHz Master clock!
		}
		else
		{
			MHZ14passed = (uint_32)(MHZ14_ticktiming/MHZ14tick); //Tick as many as possible!
			MHZ14_ticktiming -= MHZ14tick*(float)MHZ14passed; //Rest the time passed!
		}

		MMU_logging |= 2; //Are we logging hardware memory accesses(DMA etc)?
		DOUBLE MHZ14passed_ns=0.0;
		if (unlikely(MHZ14passed)) //14MHz to be ticked?
		{
			MHZ14passed_ns = MHZ14passed*MHZ14tick; //Actual ns ticked!
			if (likely((CPU[activeCPU].halt & 0x10) == 0))
			{
				updateDMA(MHZ14passed, 0); //Update the DMA timer!
				tickPIT(MHZ14passed_ns, MHZ14passed); //Tick the PIT as much as we need to keep us in sync when running!
			}
			if (useAdlib) updateAdlib(MHZ14passed); //Tick the adlib timer if needed!
			updateMouse(MHZ14passed_ns); //Tick the mouse timer if needed!
			stepDROPlayer(MHZ14passed_ns); //DRO player playback, if any!
			updateMIDIPlayer(MHZ14passed_ns); //MIDI player playback, if any!
			updatePS2Keyboard(MHZ14passed_ns); //Tick the PS/2 keyboard timer, if needed!
			updatePS2Mouse(MHZ14passed_ns); //Tick the PS/2 mouse timer, if needed!
			update8042(MHZ14passed_ns); //Tick the PS/2 mouse timer, if needed!
			if (likely((CPU[activeCPU].halt & 0x10) == 0))
			{
				updateCMOS(MHZ14passed_ns); //Tick the CMOS, if needed!
			}
			updateFloppy(MHZ14passed_ns); //Update the floppy!
			updateMPUTimer(MHZ14passed_ns); //Update the MPU timing!
			if (useGameBlaster && ((CPU[activeCPU].halt&0x10)==0)) updateGameBlaster(MHZ14passed_ns,MHZ14passed); //Tick the Game Blaster timer if needed and running!
			if (useSoundBlaster && ((CPU[activeCPU].halt&0x10)==0)) updateSoundBlaster(MHZ14passed_ns,MHZ14passed); //Tick the Sound Blaster timer if needed and running!
			updateATA(MHZ14passed_ns); //Update the ATA timer!
			tickParallel(MHZ14passed_ns); //Update the Parallel timer!
			updateUART(MHZ14passed_ns); //Update the UART timer!
			if (useLPTDAC && ((CPU[activeCPU].halt&0x10)==0)) tickssourcecovox(MHZ14passed_ns); //Update the Sound Source / Covox Speech Thing if needed!
			if (likely((CPU[activeCPU].halt&0x10)==0)) updateVGA(0.0,MHZ14passed); //Update the video 14MHz timer, when running!
		}
		if (likely((CPU[activeCPU].halt&0x10)==0)) updateVGA(instructiontime,0); //Update the normal video timer, when running!
		if (likely((CPU[activeCPU].halt&0x10)==0)) updateDMA(0,CPU[activeCPU].cycles); //Update the DMA timer, when running!
		if (unlikely(MHZ14passed))
		{
			updateModem(MHZ14passed_ns); //Update the modem!
			updateJoystick(MHZ14passed_ns); //Update the Joystick!
			updateAudio(MHZ14passed_ns); //Update the general audio processing!
			BIOSROM_updateTimers(MHZ14passed_ns); //Update any ROM(Flash ROM) timers!
			PPI_checkfailsafetimer(); //Check for any failsafe timers to raise, if required!
		}
		MMU_logging &= ~2; //Are we logging hardware memory accesses again?
		if (unlikely(--timeout==0)) //Timed out?
		{
			timeout = TIMEOUT_INTERVAL; //Reset the timeout to check the next time!
			currenttiming += getnspassed(&CPU_timing); //Check for passed time!
			if (unlikely(currenttiming >= timeoutCPUtime)) //Timeout? We're not fast enough to run at full speed!
			{
				last_timing = currentCPUtime; //Discard any time we can't keep up!
				break;
			}
		}
	} //CPU cycle loop!

	skipCPUtiming: //Audio emulation only?
	//Slowdown to requested speed if needed!
	currenttiming += getnspassed(&CPU_timing); //Add real time!
	for (;unlikely(currenttiming < last_timing);) //Not enough time spent on instructions?
	{
		currenttiming += getnspassed(&CPU_timing); //Add to the time to wait!
		delay(0); //Update to current time every instruction according to cycles passed!
	}

	float temp;
	temp = (float)MAX(last_timing,currenttiming); //Save for substraction(time executed in real time)!
	last_timing -= temp; //Keep the CPU timing within limits!
	currenttiming -= temp; //Keep the current timing within limits!

	timeemulated += timeexecuted; //Add timing for the CPU percentage to update!

	updateKeyboard(timeexecuted); //Tick the keyboard timer if needed!

	//Check for BIOS menu!
	lock(LOCK_INPUT);
	if (unlikely((psp_keypressed(BUTTON_SELECT) || (Settings_request==1)) && (BIOSMenuThread==NULL) && (debugger_thread==NULL))) //Run in-emulator BIOS menu requested while running?
	{
		if (unlikely((!is_gamingmode() && !Direct_Input && BIOSMenuAllowed) || (Settings_request==1))) //Not gaming/direct input mode and allowed to open it(not already started)?
		{
			skipstep = 3; //Skip while stepping? 1=repeating, 2=EIP destination, 3=Stop asap.
			Settings_request = 0; //We're handling the request!
			BIOSMenuThread = startThread(&BIOSMenuExecution,"UniPCemu_BIOSMenu",NULL); //Start the BIOS menu thread!
			unlock(LOCK_INPUT);
			delay(0); //Wait a bit for the thread to start up!
			lock(LOCK_INPUT);
		}
	}
	unlock(LOCK_INPUT);
	return 1; //OK!
}

word shutdowncounter = 0;

byte coreshutdown = 0;

//DoEmulator results:
//-1: Execute next instruction!
//0:Shutdown
//1:Softreset
//2:Reset emu
int DoEmulator() //Run the emulator (starting with the BIOS always)!
{
	EMU_enablemouse(1); //Enable all mouse input packets!

//Start normal emulation!
	if (!CPU[activeCPU].running || !hasmemory()) //Not running anymore or no memory present to use?
	{
		goto skipcpu; //Stop running!
	}
		
	if (reset)
	{
		doshutdown:
		debugrow("Reset/shutdown detected!");
		goto skipcpu; //Shutdown or reset?
	}

	if (++shutdowncounter >= 50) //Check for shutdown every X opcodes?
	{
		shutdowncounter = 0; //Reset counter!
		coreshutdown = shuttingdown(); //Are we shutting down?
		if (coreshutdown) goto doshutdown; //Execute the shutdown procedure!
	}

	if (!coreHandler()) //Run the core CPU+related handler, gotten abort?
	{
		goto skipcpu; //Abort!
	}
	goto runcpu;

skipcpu: //Finish the CPU loop!

	EMU_RUNNING = 0; //We're not running anymore!
	
	if (shuttingdown()) //Shut down?
	{
		debugrow("Shutdown requested");
		return 0; //Shut down!
	}

	if (reset) //To soft-reset?
	{
		debugrow("Reset requested!");
		return 1; //Full reset emu!
	}

	debugrow("Reset by default!");
	return 1; //Reset emu!

	runcpu: //Keep running the CPU?
	return -1; //Keep running!
}

//All emulated timers for the user.
char EMU_TIMERS[][256] = {
				"AddrPrint", //When debugging ROMs!
				"RTC", //Real-time clock!
				"PSP Mouse", //PS/2 mouse input!
				"AdlibAttackDecay",
				"Framerate"
				}; //All emulator (used when running the emulator) timers, which aren't used outside the emulator itself!

void stopEMUTimers()
{
	int i;
	for (i=0;i<(int)NUMITEMS(EMU_TIMERS);i++) //Process all emulator timers!
	{
		useTimer(EMU_TIMERS[i],0); //Disable it, if there!
	}
}

void startEMUTimers()
{
	int i;
	for (i=0;i<(int)NUMITEMS(EMU_TIMERS);i++) //Process all emulator timers!
	{
		useTimer(EMU_TIMERS[i],1); //Enable it, if there!
	}
}

extern Controller8042_t Controller8042;
extern byte SystemControlPortA;
void EMU_onCPUReset(word isInit)
{
	if ((isInit&0xF)||(isInit&0x40)) //Initializing or forced initialize(bit 4-7 are the bits 0-3 of the resetPending flag)?
	{
		SystemControlPortA &= ~2; //Clear A20 here!
		if ((is_XT == 0) || (EMULATED_CPU >= CPU_80286) || (is_Compaq == 1) || (is_i430fx)) //AT-class CPU?
		{
			Controller8042.outputport |= 2; //Set A20 here!
		}
		else
		{
			Controller8042.outputport &= ~2; //Clear A20 here!
		}
		Controller8042.outputport |= 1; //Prevent us from deadlocking(calling this function over and over infinitely within itself)!
		refresh_outputport(); //Refresh from 8042!
		checkPPIA20(); //Refresh from Fast A20!
	}
}
