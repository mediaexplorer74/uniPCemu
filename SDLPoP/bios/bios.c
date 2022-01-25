#pragma warning(disable : 4996)

//This file is part of UniPCemu

#include "..\commonemuframework\headers\types.h"//"headers/types.h" //Basic types etc.
#include "headers/basicio/io.h" //Basic I/O support for BIOS!
#include "headers/mmu/mmuhandler.h" //CRC32 support!
#include "headers/bios/bios.h" //BIOS basic type support etc!
#include "headers/bios/boot.h" //For booting disks!
#include "headers/cpu/cpu.h" //For some constants concerning CPU!
#include "..\commonemuframework\headers\emu\gpu\gpu.h" //"headers/emu/gpu/gpu.h" //Need GPU comp!
#include "..\commonemuframework\headers\support\zalloc.h"//"headers/support/zalloc.h" //Memory allocation: freemem function!
#include "..\commonemuframework\headers\support\log.h"//"headers/support/log.h" //Logging support!
#include "..\commonemuframework\headers\emu\gpu\gpu_emu.h"//"headers/emu/gpu/gpu_emu.h" //GPU emulator support!
#include "headers/hardware/8042.h" //Basic 8042 support for keyboard initialisation!
#include "..\commonemuframework\headers\emu\emu_misc.h"//"headers/emu/emu_misc.h" //FILE_EXISTS support!
#include "headers/hardware/ports.h" //Port I/O support!
#include "..\commonemuframework\headers\emu\sound.h"//"headers/emu/sound.h" //Volume support!
#include "headers/hardware/midi/mididevice.h" //MIDI support!
#include "..\commonemuframework\headers\hardware\ps2_keyboard.h" //"headers/hardware/ps2_keyboard.h" //For timeout support!
#include "..\commonemuframework\headers\support\iniparser.h" //"headers/support/iniparser.h" //INI file parsing for our settings storage!
#include "..\commonemuframework\headers\fopen64.h" //"headers/fopen64.h" //64-bit fopen support!
#include "headers/hardware/floppy.h" //Floppy disk support!
#include "headers/hardware/i430fx.h" //i430fx support!

#ifdef IS_SWITCH
#include <sys/unistd.h> //Required for getcwd()
#endif

//Are we disabled?
#define __HW_DISABLED 0

//Log redirect, if enabled!
//#define LOG_REDIRECT

extern FLOPPY_GEOMETRY floppygeometries[NUMFLOPPYGEOMETRIES]; //All possible floppy geometries to create!
BIOS_Settings_TYPE BIOS_Settings; //Currently loaded settings!
byte exec_showchecksumerrors = 0; //Show checksum errors?

//Block size of memory (blocks of 16KB for IBM PC Compatibility)!
#define MEMORY_BLOCKSIZE_XT 0x4000
#define MEMORY_BLOCKSIZE_AT_LOW 0x10000
#define MEMORY_BLOCKSIZE_AT_HIGH 0x100000

//What file to use for saving the BIOS!
#define DEFAULT_SETTINGS_FILE "SETTINGS.INI"
#define DEFAULT_REDIRECT_FILE "redirect.txt"
#define DEFAULT_ROOT_PATH "."

char BIOS_Settings_file[256] = DEFAULT_SETTINGS_FILE; //Our settings file!
char UniPCEmu_root_dir[256] = DEFAULT_ROOT_PATH; //Our root path!
byte UniPCEmu_root_dir_setting = 0; //The current root setting to be viewed!

//Android memory limit, in MB.
#define ANDROID_MEMORY_LIMIT 1024

//All seperate paths used by the emulator!
extern char diskpath[256];
extern char soundfontpath[256];
extern char musicpath[256];
extern char capturepath[256];
extern char logpath[256];
extern char ROMpath[256];

extern byte is_XT; //Are we emulating a XT architecture?
extern byte is_Compaq; //Are we emulating a Compaq architecture?
extern byte non_Compaq; //Are we not emulating a Compaq architecture?
extern byte is_PS2; //Are we emulating PS/2 architecture extensions?

extern char currentarchtext[6][256]; //The current architecture texts!

char* getcurrentarchtext() //Get the current architecture!
{
	//First, determine the current CMOS!
	if (is_i430fx==2) //i440fx?
	{
		return &currentarchtext[5][0]; //We've used!
	}
	else if (is_i430fx==1) //i430fx?
	{
		return &currentarchtext[4][0]; //We've used!
	}
	else if (is_PS2) //PS/2?
	{
		return &currentarchtext[3][0]; //We've used!
	}
	else if (is_Compaq)
	{
		return &currentarchtext[2][0]; //We've used!
	}
	else if (is_XT)
	{
		return &currentarchtext[0][0]; //We've used!
	}
	else //AT?
	{
		return &currentarchtext[1][0]; //We've used!
	}
	//Now, give the selected CMOS's memory field!
	return &currentarchtext[1][0]; //We've used!
}

uint_32 *getarchmemory() //Get the memory field for the current architecture!
{
	//First, determine the current CMOS!
	CMOSDATA* currentCMOS;
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
	//Now, give the selected CMOS's memory field!
	return &currentCMOS->memory; //Give the memory field for the current architecture!
}

byte* getarchemulated_CPU() //Get the memory field for the current architecture!
{
	//First, determine the current CMOS!
	CMOSDATA* currentCMOS;
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
	//Now, give the selected CMOS's memory field!
	return &currentCMOS->emulated_CPU; //Give the memory field for the current architecture!
}
byte* getarchemulated_CPUs() //Get the memory field for the current architecture!
{
	//First, determine the current CMOS!
	CMOSDATA* currentCMOS;
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
	//Now, give the selected CMOS's memory field!
	return &currentCMOS->emulated_CPUs; //Give the memory field for the current architecture!
}
byte* getarchCPUIDmode() //Get the memory field for the current architecture!
{
	//First, determine the current CMOS!
	CMOSDATA* currentCMOS;
	if (is_i430fx == 2) //i440fx?
	{
		currentCMOS = &BIOS_Settings.i440fxCMOS; //We've used!
	}
	else if (is_i430fx == 1) //i430fx?
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
	//Now, give the selected CMOS's memory field!
	return &currentCMOS->CPUIDmode; //Give the CPUID mode field for the current architecture!
}
byte* getarchDataBusSize() //Get the memory field for the current architecture!
{
	//First, determine the current CMOS!
	CMOSDATA* currentCMOS;
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
	//Now, give the selected CMOS's memory field!
	return &currentCMOS->DataBusSize; //Give the memory field for the current architecture!
}
uint_32* getarchCPUSpeed() //Get the memory field for the current architecture!
{
	//First, determine the current CMOS!
	CMOSDATA* currentCMOS;
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
	//Now, give the selected CMOS's memory field!
	return &currentCMOS->CPUspeed; //Give the memory field for the current architecture!
}
uint_32* getarchTurboCPUSpeed() //Get the memory field for the current architecture!
{
	//First, determine the current CMOS!
	CMOSDATA* currentCMOS;
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
	//Now, give the selected CMOS's memory field!
	return &currentCMOS->TurboCPUspeed; //Give the memory field for the current architecture!
}
byte* getarchuseTurboCPUSpeed() //Get the memory field for the current architecture!
{
	//First, determine the current CMOS!
	CMOSDATA* currentCMOS;
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
	//Now, give the selected CMOS's memory field!
	return &currentCMOS->useTurboCPUSpeed; //Give the memory field for the current architecture!
}
byte* getarchclockingmode() //Get the memory field for the current architecture!
{
	//First, determine the current CMOS!
	CMOSDATA* currentCMOS;
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
	//Now, give the selected CMOS's memory field!
	return &currentCMOS->clockingmode; //Give the memory field for the current architecture!
}

void BIOS_updateDirectories()
{
#if defined(ANDROID) || defined(IS_LINUX) || defined(IS_VITA) || defined(IS_SWITCH)
	safestrcpy(diskpath,sizeof(diskpath),UniPCEmu_root_dir); //Root dir!
	safestrcat(diskpath,sizeof(diskpath),"/");
	safestrcpy(soundfontpath,sizeof(soundfontpath),diskpath); //Clone!
	safestrcpy(musicpath,sizeof(musicpath),diskpath); //Clone!
	safestrcpy(capturepath,sizeof(capturepath),diskpath); //Clone!
	safestrcpy(logpath,sizeof(logpath),diskpath); //Clone!
	safestrcpy(ROMpath,sizeof(ROMpath),diskpath); //Clone!
	//Now, create the actual subdirs!
	safestrcat(diskpath,sizeof(diskpath),"disks");
	safestrcat(soundfontpath,sizeof(soundfontpath),"soundfonts");
	safestrcat(musicpath,sizeof(musicpath),"music");
	safestrcat(capturepath,sizeof(capturepath),"captures");
	safestrcat(logpath,sizeof(logpath),"logs");
	safestrcat(ROMpath,sizeof(ROMpath),"ROM"); //ROM directory to use!
	//Now, all paths are loaded! Ready to run!
#endif
}

int storage_strpos(char *str, char ch)
{
	int pos=0;
	for (;(*str && (*str!=ch));++str,++pos); //Not found yet?
	if (*str==ch) return pos; //Found position!
	return -1; //Not found!
}

byte is_writablepath(char *path)
{
	char fullpath[256];
	BIGFILE *f;
	memset(&fullpath,0,sizeof(fullpath)); //init!
	safestrcpy(fullpath,sizeof(fullpath),path); //Set the path!
	safestrcat(fullpath,sizeof(fullpath),"/"); //Add directory seperator!
	safestrcat(fullpath,sizeof(fullpath),"writable.txt"); //test file!
	f = emufopen64(fullpath,"wb");
	if (f)
	{
		emufclose64(f); //Close the file!
		delete_file(path,"writable.txt"); //Delete the file!
		return 1; //We're writable!
	}
	return 0; //We're not writable!
}

#if defined(ANDROID) || defined(IS_LINUX) || defined(IS_VITA) || defined(IS_SWITCH)
byte is_textcharacter(char c)
{
	if ((c>='a') && (c<='z')) return 1; //Text!
	if ((c>='A') && (c<='Z')) return 1; //Text!
	if ((c>='0') && (c<='9')) return 1; //Text!
	switch (c) //Remaining cases?
	{
		case '~':
		case '`':
		case '!':
		case '@':
		case '#':
		case '$':
		case '%':
		case '^':
		case '&':
		case '*':
		case '(':
		case ')':
		case '-':
		case '_':
		case '+':
		case '=':
		case '{':
		case '}':
		case '[':
		case ']':
		case '|':
		case '\\':
		case ':':
		case ';':
		case '"':
		case '\'':
		case '<':
		case ',':
		case '>':
		case '.':
		case '?':
			return 1; //Valid text for a filename to end in!
		default:
			break;
	}
	return 0; //Not a text character!
}
#endif

#if (defined(IS_LINUX) && !defined(ANDROID)) || defined(IS_VITA) || defined(IS_SWITCH)
static void recursive_mkdir(const char *dir) {
        char tmp[256];
        char *p = NULL;
        size_t len;

        snprintf(tmp, sizeof(tmp),"%s",dir);
        len = safestrlen(tmp,sizeof(tmp));
        if(tmp[len - 1] == '/')
                tmp[len - 1] = 0;
        for(p = tmp + 1; *p; p++)
                if(*p == '/') {
                        *p = 0;
                        domkdir(tmp);
                        *p = '/';
                }
        domkdir(tmp);
}
#endif

void BIOS_DetectStorage() //Auto-Detect the current storage to use, on start only!
{
	#if defined(ANDROID) || defined(IS_LINUX) || defined(IS_VITA) || defined(IS_SWITCH)
		#ifndef ANDROID
        #ifndef SDL2//#ifdef SDL2
		char* base_path;
		char* linuxpath = SDL_getenv("UNIPCEMU");
		if (linuxpath) //Linux environment path specified?
		{
			safestrcpy(UniPCEmu_root_dir, sizeof(UniPCEmu_root_dir), linuxpath);
			//SDL_free(linuxpath); //Release it, now that we have it! For some reason, this cannot be done on a linux version?
			if (safestrlen(UniPCEmu_root_dir, sizeof(UniPCEmu_root_dir)) > 1) //Valid length?
			{
				if (UniPCEmu_root_dir[safestrlen(UniPCEmu_root_dir, sizeof(UniPCEmu_root_dir) - 1)] == '/') //Ending with a slash? Check to strip!
				{
					if (UniPCEmu_root_dir[safestrlen(UniPCEmu_root_dir, sizeof(UniPCEmu_root_dir) - 2)] != '/') //Not ending with a double slash? Valid to strip!
					{
						UniPCEmu_root_dir[safestrlen(UniPCEmu_root_dir, sizeof(UniPCEmu_root_dir) - 1)] = '\0'; //Strip off the trailing slash!
					}
				}
			}
			else //Invalid length? Fallback!
			{
				if (strcmp(UniPCEmu_root_dir, ".") != 0) //Not CWD?
				{
					goto handleLinuxBasePathSDL2; //Fallback!
				}
			}
		}
		else
		{
			handleLinuxBasePathSDL2: //Fallback!
			base_path = SDL_GetPrefPath("Superfury", "UniPCemu");
			if (base_path) //Gotten?
			{
				safestrcpy(UniPCEmu_root_dir, sizeof(UniPCEmu_root_dir), base_path);
				SDL_free(base_path); //Release it, now that we have it!
				if (safestrlen(UniPCEmu_root_dir, sizeof(UniPCEmu_root_dir)) > 1) //Valid length?
				{
					if (UniPCEmu_root_dir[safestrlen(UniPCEmu_root_dir, sizeof(UniPCEmu_root_dir) - 1)] == '/') //Ending with a slash? Check to strip!
					{
						if (UniPCEmu_root_dir[safestrlen(UniPCEmu_root_dir, sizeof(UniPCEmu_root_dir) - 2)] != '/') //Not ending with a double slash? Valid to strip!
						{
							UniPCEmu_root_dir[safestrlen(UniPCEmu_root_dir, sizeof(UniPCEmu_root_dir) - 1)] = '\0'; //Strip off the trailing slash!
						}
					}
				}
				else //Default length?
				{
					#ifdef IS_LINUX
					safestrcpy(UniPCEmu_root_dir, sizeof(UniPCEmu_root_dir), "~/UniPCemu"); //Default path!
					#else
					#ifdef IS_VITA
					safestrcpy(UniPCEmu_root_dir, sizeof(UniPCEmu_root_dir), "ux0:/data/Superfury/UniPCemu"); //CWD!
					#else
					#ifdef IS_SWITCH
					if (getcwd(&UniPCEmu_root_dir[0],sizeof(UniPCEmu_root_dir))==NULL)
					#endif
					safestrcpy(UniPCEmu_root_dir, sizeof(UniPCEmu_root_dir), "."); //CWD!
					#endif
					#endif
				}
			}
			else //Fallback to default path?
			{
				#ifdef IS_LINUX
				safestrcpy(UniPCEmu_root_dir, sizeof(UniPCEmu_root_dir), "~/UniPCemu");
				#else
				#ifdef IS_VITA
				safestrcpy(UniPCEmu_root_dir, sizeof(UniPCEmu_root_dir), "ux0:/data/Superfury/UniPCemu"); //CWD!
				#else
				#ifdef IS_SWITCH
				if (getcwd(&UniPCEmu_root_dir[0],sizeof(UniPCEmu_root_dir))==NULL)
				#endif
				safestrcpy(UniPCEmu_root_dir, sizeof(UniPCEmu_root_dir), "."); //CWD!
				#endif
				#endif
			}
		}
		recursive_mkdir(UniPCEmu_root_dir); //Make sure our directory exists, if it doesn't yet!
		#else
		char* linuxpath = SDL_getenv("UNIPCEMU");
		if (linuxpath) //Linux path specified?
		{
			safestrcpy(UniPCEmu_root_dir, sizeof(UniPCEmu_root_dir), linuxpath);
			//SDL_free(linuxpath); //Release it, now that we have it! This doesn't seem to work on linux versions of SDL?
			if (safestrlen(UniPCEmu_root_dir, sizeof(UniPCEmu_root_dir)) > 1) //Valid length?
			{
				if (UniPCEmu_root_dir[safestrlen(UniPCEmu_root_dir, sizeof(UniPCEmu_root_dir) - 1)] == '/') //Trailing slash?
				{
					UniPCEmu_root_dir[safestrlen(UniPCEmu_root_dir, sizeof(UniPCEmu_root_dir) - 1)] = '\0'; //Strip off the trailing slash!
				}
			}
			else //Invalid length? Fallback!
			{
				if (strcmp(UniPCEmu_root_dir, ".") != 0) //Not CWD?
				{
					#ifdef IS_LINUX
					goto handleLinuxBasePathSDL; //Fallback!
					#endif
				}
			}
		}
		else
		{
			#ifdef IS_LINUX
			handleLinuxBasePathSDL:
			#endif
			//SDL1.2.x on linux?
			#ifdef IS_LINUX
			safestrcpy(UniPCEmu_root_dir, sizeof(UniPCEmu_root_dir), "~/UniPCemu");
			#else
			#ifdef IS_VITA
			safestrcpy(UniPCEmu_root_dir, sizeof(UniPCEmu_root_dir), "ux0:/data/Superfury/UniPCemu"); //CWD!
			#else
			#ifdef IS_SWITCH
			if (getcwd(&UniPCEmu_root_dir[0],sizeof(UniPCEmu_root_dir))==NULL)
			#endif
			safestrcpy(UniPCEmu_root_dir, sizeof(UniPCEmu_root_dir), "."); //CWD!
			#endif
			#endif
		}
		recursive_mkdir(UniPCEmu_root_dir); //Make sure our directory exists, if it doesn't yet!
		#endif
		#endif

		#ifdef IS_SWITCH
		if (strcmp(UniPCEmu_root_dir, "/") == 0) //Invalid path to use?
		{
			safe_strcat(UniPCEmu_root_dir, sizeof(UniPCEmu_root_dir), "Superfury/UniPCemu"); //Proper directory to use!
			recursive_mkdir(UniPCEmu_root_dir); //Make sure our directory exists, if it doesn't yet!
		}
		#endif

		BIGFILE *f;
		byte is_redirected=0;
		is_redirected = 0; //Init redirect status for main directory!
		//Try external media first!
		char *environment;
		int multipathseperator; //Multi path seperator position in the current string! When set, it's changed into a NULL character to read out the current path in the list!

		char redirectdir[256]; //Redirect directory!
		int_32 redirectdirsize; //How much has been read?

		#ifdef PELYAS_SDL
		if (environment = getenv("SECONDARY_STORAGE")) //Autodetected try secondary storage?
		#else
		#ifdef ANDROID
		if ((environment = SDL_getenv("SECONDARY_STORAGE"))!=NULL) //Autodetected try secondary storage?
		#else
		if (0) //Don't use on non-Android!
		#endif
		#endif
		{
			scanNextSecondaryPath:
			if (environment==NULL) goto scanDefaultpath; //Start scanning the default path, nothing found!
			if (*environment=='\0') goto scanDefaultpath; //Start scanning the default path, nothing found!
			if ((multipathseperator = storage_strpos(environment,':'))!=-1) //Multiple environments left to check?
			{
				environment[multipathseperator] = '\0'; //Convert the seperator into an EOS for reading the current value out!
			}
			//Check the currently loaded path for writability!
			if (is_writablepath(environment)) //Writable?
			{
				safestrcpy(UniPCEmu_root_dir,sizeof(UniPCEmu_root_dir),environment); //Root path of the disk!
				if (multipathseperator!=-1) //To revert multiple path seperator?
				{
					environment[multipathseperator] = ':'; //Restore the path seperator from the EOS!
				}
				//Root directory loaded!
				safestrcat(UniPCEmu_root_dir,sizeof(UniPCEmu_root_dir),"/UniPCemu"); //Our storage path!
				domkdir(UniPCEmu_root_dir); //Make sure to create our parent directory, if needed!
				safestrcat(UniPCEmu_root_dir,sizeof(UniPCEmu_root_dir),"/files"); //Subdirectory to store the files!
				goto finishpathsetting;
			}
			//To check the next path?
			if (multipathseperator!=-1) //To revert multiple path seperator?
			{
				environment[multipathseperator] = ':'; //Restore the path seperator from the EOS!
				environment += (multipathseperator+1); //Skip past the multiple path seperator!
			}
			else goto scanDefaultpath; //Finished scanning without multiple paths left!
			goto scanNextSecondaryPath; //Scan the next path in the list!
		}

		scanDefaultpath:
		#ifdef ANDROID
		//Android changes the root path!
		#ifdef PELYAS_SDL
			if ((environment = getenv("SDCARD"))!=NULL)
			{
				safestrcpy(UniPCEmu_root_dir,sizeof(UniPCEmu_root_dir), environment); //path!
				safestrcat(UniPCEmu_root_dir,sizeof(UniPCEmu_root_dir), "/Android/data/com.unipcemu.app/files");
			}
		#else
			if ((environment = SDL_getenv("SDCARD"))!=NULL) //Autodetected?
			{
				safestrcpy(UniPCEmu_root_dir,sizeof(UniPCEmu_root_dir), environment); //path!
				safestrcat(UniPCEmu_root_dir,sizeof(UniPCEmu_root_dir), "/Android/data/com.unipcemu.app/files");
			}
			else if (SDL_AndroidGetExternalStorageState() == (SDL_ANDROID_EXTERNAL_STORAGE_WRITE | SDL_ANDROID_EXTERNAL_STORAGE_READ)) //External settings exist?
			{
				if (SDL_AndroidGetExternalStoragePath()) //Try external.
				{
					safestrcpy(UniPCEmu_root_dir,sizeof(UniPCEmu_root_dir), SDL_AndroidGetExternalStoragePath()); //External path!
				}
				else if (SDL_AndroidGetInternalStoragePath()) //Try internal.
				{
					safestrcpy(UniPCEmu_root_dir,sizeof(UniPCEmu_root_dir), SDL_AndroidGetInternalStoragePath()); //Internal path!
				}
			}
			else
			{
				if (SDL_AndroidGetInternalStoragePath()) //Try internal.
				{
					safestrcpy(UniPCEmu_root_dir,sizeof(UniPCEmu_root_dir), SDL_AndroidGetInternalStoragePath()); //Internal path!
				}
			}
		#endif
		#endif

		finishpathsetting:
		safestrcpy(BIOS_Settings_file,sizeof(BIOS_Settings_file),UniPCEmu_root_dir); //Our settings file location!
		safestrcat(BIOS_Settings_file,sizeof(BIOS_Settings_file),"/"); //Inside the directory!
		safestrcat(BIOS_Settings_file,sizeof(BIOS_Settings_file),DEFAULT_SETTINGS_FILE); //Our settings file!
		domkdir(UniPCEmu_root_dir); //Auto-create our root directory!
		BIOS_updateDirectories(); //Update all directories!
		//Normal devices? Don't detect!

		//Check for redirection to apply!
		memset(&redirectdir,0,sizeof(redirectdir)); //Init!
		safestrcpy(redirectdir,sizeof(redirectdir),UniPCEmu_root_dir); //Check for redirects!
		safestrcat(redirectdir,sizeof(redirectdir),"/"); //Inside the directory!
		safestrcat(redirectdir,sizeof(redirectdir),DEFAULT_REDIRECT_FILE); //Our redirect file!
		#ifdef LOG_REDIRECT
		char temppath[256];
		memset(&temppath,0,sizeof(temppath)); //Set log path!
		safestrcpy(temppath,sizeof(temppath),logpath); //Set log path!
		safestrcat(temppath,sizeof(temppath),"/"); //Inside the directory!
		safestrcat(temppath,sizeof(temppath),DEFAULT_REDIRECT_FILE); //Log file for testing!
		char buffer[256];
		FILE *f2;
		char lb[2] = {0xD,0xA}; //Line break!
		memset(&buffer,0,sizeof(buffer)); //Init buffer!
		snprintf(buffer,sizeof(buffer),"Redirect file: %s",redirectdir);
		f2 = emufopen64(temppath,"wb"); //Log the filename!
		if (f2) //Valid?
		{
			emufwrite64(&buffer,1,safestrlen(buffer,sizeof(buffer)),f2); //Log!
			emufwrite64(&lb,1,sizeof(lb),f2); //Line break!
			emufclose64(f2); //Close!
		}
		#endif
		if (file_exists(redirectdir) && (is_redirected==0)) //Redirect for main directory?
		{
			#ifdef LOG_REDIRECT
			memset(&buffer,0,sizeof(buffer)); //Init buffer!
			snprintf(buffer,sizeof(buffer),"Attempting redirect...");
			f2 = emufopen64(temppath,"ab"); //Log the filename!
			if (f2) //Valid?
			{
				emufwrite64(&buffer,1,safestrlen(buffer,sizeof(buffer)),f2); //Log!
				emufwrite64(&lb,1,sizeof(lb),f2); //Line break!
				emufclose64(f2); //Close!
			}
			#endif
			f = emufopen64(redirectdir,"rb");
			if (f) //Valid?
			{
				#ifdef LOG_REDIRECT
				memset(&buffer,0,sizeof(buffer)); //Init buffer!
				snprintf(buffer,sizeof(buffer),"Valid file!");
				f2 = emufopen64(temppath,"ab"); //Log the filename!
				if (f2) //Valid?
				{
					emufwrite64(&buffer,1,safestrlen(buffer,sizeof(buffer)),f2); //Log!
					emufwrite64(&lb,1,sizeof(lb),f2); //Line break!
					emufclose64(f2); //Close!
				}
				#endif
				emufseek64(f,0,SEEK_END); //Goto EOF!
				if (((redirectdirsize = emuftell64(f))<sizeof(redirectdir)) && redirectdirsize) //Valid to read?
				{
					#ifdef LOG_REDIRECT
					memset(&buffer,0,sizeof(buffer)); //Init buffer!
					snprintf(buffer,sizeof(buffer),"Valid size!");
					f2 = emufopen64(temppath,"ab"); //Log the filename!
					if (f2) //Valid?
					{
						emufwrite64(&buffer,1,safestrlen(buffer,sizeof(buffer)),f2); //Log!
						emufwrite64(&lb,1,sizeof(lb),f2); //Line break!
						emufclose64(f2); //Close!
					}
					#endif
					emufseek64(f,0,SEEK_SET); //Goto BOF!
					memset(&redirectdir,0,sizeof(redirectdir)); //Clear for our result to be stored safely!
					if (emufread64(&redirectdir,1,redirectdirsize,f)==redirectdirsize) //Read?
					{
						#ifdef LOG_REDIRECT
						memset(&buffer,0,sizeof(buffer)); //Init buffer!
						snprintf(buffer,sizeof(buffer),"Valid content!");
						f2 = emufopen64(temppath,"ab"); //Log the filename!
						if (f2) //Valid?
						{
							emufwrite64(&buffer,1,safestrlen(buffer,sizeof(buffer)),f2); //Log!
							emufwrite64(&lb,1,sizeof(lb),f2); //Line break!
							emufclose64(f2); //Close!
						}
						#endif
						for (;safestrlen(redirectdir,sizeof(redirectdir));) //Valid to process?
						{
							switch (redirectdir[safestrlen(redirectdir,sizeof(redirectdir))-1]) //What is the final character?
							{
								case '/': //Invalid? Take it off!
									if (safestrlen(redirectdir,sizeof(redirectdir))>1) //More possible? Check for special path specification(e.g. :// etc.)!
									{
										if (!is_textcharacter(redirectdir[safestrlen(redirectdir,sizeof(redirectdir))-2])) //Not normal path?
										{
											redirectdir[safestrlen(redirectdir,sizeof(redirectdir))-1] = '\0'; //Take it off, we're specifying the final slash ourselves!
											goto redirect_validpath;
										}
									}
									//Invalid normal path: handle normally!
								case '\n':
								case '\r':
									redirectdir[safestrlen(redirectdir,sizeof(redirectdir))-1] = '\0'; //Take it off!
									break;
								default:
									redirect_validpath: //Apply valid directory for a root domain!
									#ifdef LOG_REDIRECT
									memset(&buffer,0,sizeof(buffer)); //Init buffer!
									snprintf(buffer,sizeof(buffer),"Trying: Redirecting to: %s",redirectdir); //Where are we redirecting to?
									f2 = emufopen64(temppath,"ab"); //Log the filename!
									if (f2) //Valid?
									{
										emufwrite64(&buffer,1,safestrlen(buffer,sizeof(buffer)),f2); //Log!
										emufwrite64(&lb,1,sizeof(lb),f2); //Line break!
										emufclose64(f2); //Close!
									}
									#endif
									if (is_writablepath(redirectdir)) //Writable path?
									{
										is_redirected = 1; //We're redirecting!
									}
									else
									{
										is_redirected = 0; //We're not redirecting after all!
									}
									goto finishredirect;
									break;
							}
						}
					}
				}
				finishredirect: //Finishing redirect!
				emufclose64(f); //Stop checking!
				#ifdef LOG_REDIRECT
				dolog("redirect","Content:%s/%i!",redirectdir,is_redirected);
				#endif
				if (is_redirected && redirectdir[0]) //To redirect?
				{
					safestrcpy(UniPCEmu_root_dir,sizeof(UniPCEmu_root_dir),redirectdir); //The new path to use!
					#ifdef LOG_REDIRECT
					memset(&buffer,0,sizeof(buffer)); //Init buffer!
					snprintf(buffer,sizeof(buffer),"Redirecting to: %s",UniPCEmu_root_dir); //Where are we redirecting to?
					f2 = emufopen64(temppath,"ab"); //Log the filename!
					if (f2) //Valid?
					{
						emufwrite64(&buffer,1,safestrlen(buffer,sizeof(buffer)),f2); //Log!
						emufwrite64(&lb,1,sizeof(lb),f2); //Line break!
						emufclose64(f2); //Close!
					}
					#endif
					goto finishpathsetting; //Go and apply the redirection!
				}
			}
		}
	#endif
}

void forceBIOSSave()
{
	BIOS_SaveData(); //Save the BIOS, ignoring the result!
}

void autoDetectArchitecture()
{
	is_XT = (BIOS_Settings.architecture==ARCHITECTURE_XT); //XT architecture?
	is_Compaq = 0; //Default to not being Compaq!
	is_PS2 = 0; //Default to not being PS/2!
	is_i430fx = 0; //Default to not using the i430fx!

	if (BIOS_Settings.architecture==ARCHITECTURE_COMPAQ) //Compaq architecture?
	{
		is_XT = 0; //No XT!
		is_Compaq = 1; //Compaq!
	}
	if (BIOS_Settings.architecture==ARCHITECTURE_PS2) //PS/2 architecture?
	{
		is_PS2 = 1; //PS/2 extensions enabled!
		is_XT = 0; //AT compatible!
		is_Compaq = 1; //Compaq compatible!
	}
	if ((BIOS_Settings.architecture==ARCHITECTURE_i430fx) || (BIOS_Settings.architecture==ARCHITECTURE_i440fx)) //i430fx architecture?
	{
		is_i430fx = (BIOS_Settings.architecture==ARCHITECTURE_i430fx)?1:2; //i430fx/i440fx architecture!
		is_PS2 = 1; //PS/2 extensions enabled!
		is_XT = 0; //AT compatible!
		is_Compaq = 0; //Compaq compatible!
	}
	non_Compaq = !is_Compaq; //Are we not using a Compaq architecture?
}

//Custom feof, because Windows feof seems to fail in some strange cases?
byte is_EOF(FILE *fp)
{
	byte res;
	long currentOffset = ftell(fp);

	fseek(fp, 0, SEEK_END);

	if(currentOffset >= ftell(fp))
		res = 1; //EOF!
    else
		res = 0; //Not EOF!
	fseek(fp, currentOffset, SEEK_SET);
	return res;
}

#define MAX_LINE_LENGTH 256

void autoDetectMemorySize(int tosave) //Auto detect memory size (tosave=save BIOS?)
{
	char line[MAX_LINE_LENGTH];
	if (__HW_DISABLED) return; //Ignore updates to memory!
	debugrow("Detecting MMU memory size to use...");
	
	uint_32 freememory;
	int_32 memoryblocks;
	uint_64 maximummemory;
	byte AThighblocks; //Are we using AT high blocks instead of low blocks?
	byte memorylimitshift;

	freememory = freemem(); //The free memory available!

	memorylimitshift = 20; //Default to MB (2^20) chunks!
	
	#ifdef ANDROID
	maximummemory = ANDROID_MEMORY_LIMIT; //Default limit in MB!
	#else
	maximummemory = SHRT_MAX; //Default: maximum memory limit!
	#endif
	char limitfilename[256];
	memset(&limitfilename,0,sizeof(limitfilename)); //Init!
	safestrcpy(limitfilename,sizeof(limitfilename),UniPCEmu_root_dir); //Root directory!
	safestrcat(limitfilename,sizeof(limitfilename),"/memorylimit.txt"); //Limit file path!

	if (file_exists(limitfilename)) //Limit specified?
	{
		int memorylimitMB=SHRT_MAX;
		char memorylimitsize='?';
		char *linepos;
		byte limitread;
		BIGFILE *f;
		f = emufopen64(limitfilename,"rb");
		limitread = 0; //Default: not read!
		if (f) //Valid file?
		{
			for (;!emufeof64(f);) //Read a line, processing all possible lines?
			{
				if (read_line64(f, &line[0], sizeof(line))) //Read a line?
				{
					if (sscanf(line, "%d", &memorylimitMB)) //Read up to 4 bytes to the buffer!
					{
						linepos = &line[0]; //Init!
						for (; ((*linepos >= '0') && (*linepos <= '9'));) //Skip numbers!
						{
							++linepos; //SKip ahead!
						}
						if (sscanf(linepos, "%c", &memorylimitsize)) //Read size?
						{
							limitread = 2; //We're read!
							switch (memorylimitsize) //What size?
							{
							case 'b':
							case 'B': //KB?
								memorylimitsize = 'B'; //Default to Bytes!
								break;
							case 'k':
							case 'K': //KB?
								memorylimitsize = 'K'; //Default to KB!
								break;
							case 'm':
							case 'M': //MB?
								memorylimitsize = 'M'; //Default to MB!
								break;
							case 'g':
							case 'G': //GB?
								memorylimitsize = 'G'; //Default to GB!
								break;
							default: //Unknown size?
								memorylimitsize = 'M'; //Default to MB!
								break;
							}
						}
						else
						{
							memorylimitsize = 'B'; //Default to Bytes!
						}
					}
				}
			}
			emufclose64(f); //Close the file!
			f = NULL; //Deallocated!
		}
		if (limitread) //Are we read?
		{
			maximummemory = (uint_32)memorylimitMB; //Set the memory limit, in MB!
			switch (memorylimitsize) //What shift to apply?
			{
				case 'B':
					memorylimitshift = 0; //No shift: we're in bytes!
					break;
				case 'K':
					memorylimitshift = 10; //Shift: we're in KB!
					break;
				case 'G':
					memorylimitshift = 30; //Shift: we're in GB!
					break;
				default:
				case 'M':
				case '?': //Unknown?
					memorylimitshift = 20; //Shift: we're in MB!
					break;			
			}
		}
	}
	maximummemory <<= memorylimitshift; //Convert to MB of memory limit!

	if (maximummemory<0x10000) //Nothing? use bare minumum!
	{
		maximummemory = 0x10000; //Bare minumum: 64KB + reserved memory!
	}

	maximummemory += FREEMEMALLOC; //Required free memory is always to be applied as a limit!

	if (((uint_64)freememory)>=maximummemory) //Limit broken?
	{
		freememory = (uint_32)maximummemory; //Limit the memory as specified!
	}

	//Architecture limits are placed on the detected memmory, which is truncated by the set memory limit!

	if (freememory>=FREEMEMALLOC) //Can we substract?
	{
		freememory -= FREEMEMALLOC; //What to leave!
	}
	else
	{
		freememory = 0; //Nothing to substract: ran out of memory!
	}

	autoDetectArchitecture(); //Detect the architecture to use!
	if (is_XT) //XT?
	{
		memoryblocks = SAFEDIV((freememory),MEMORY_BLOCKSIZE_XT); //Calculate # of free memory size and prepare for block size!
	}
	else //AT?
	{
		memoryblocks = SAFEDIV((freememory), MEMORY_BLOCKSIZE_AT_LOW); //Calculate # of free memory size and prepare for block size!
	}
	AThighblocks = 0; //Default: we're using low blocks!
	if (is_XT==0) //AT+?
	{
		if ((memoryblocks*MEMORY_BLOCKSIZE_AT_LOW)>=MEMORY_BLOCKSIZE_AT_HIGH) //Able to divide in big blocks?
		{
			memoryblocks = SAFEDIV((memoryblocks*MEMORY_BLOCKSIZE_AT_LOW),MEMORY_BLOCKSIZE_AT_HIGH); //Convert to high memory blocks!
			AThighblocks = 1; //Wer� using high blocks instead!
		}
	}
	if (memoryblocks<0) memoryblocks = 0; //No memory left?
	uint_32* archmem;
	archmem = getarchmemory(); //Get the architecture's memory field!
	if (is_XT) //XT?
	{
		*archmem = memoryblocks * MEMORY_BLOCKSIZE_XT; //Whole blocks of memory only!
	}
	else
	{
		*archmem = memoryblocks * (AThighblocks?MEMORY_BLOCKSIZE_AT_HIGH:MEMORY_BLOCKSIZE_AT_LOW); //Whole blocks of memory only, either low memory or high memory blocks!
	}
	if (EMULATED_CPU<=CPU_NECV30) //80286-? We don't need more than 1MB memory(unusable memory)!
	{
		if (*archmem>=0x100000) *archmem = 0x100000; //1MB memory max!
	}
	else if (EMULATED_CPU<=CPU_80286) //80286-? We don't need more than 16MB memory(unusable memory)!
	{
		if (*archmem>=0xF00000) *archmem = 0xF00000; //16MB memory max!
	}
	else if (is_Compaq && (!is_i430fx)) //Compaq is limited to 16MB
	{
		if (*archmem >= 0x1000000) *archmem = 0x1000000; //16MB memory max!
	}
	else if (is_i430fx==1) //i430fx is limited to 128MB
	{
		if (*archmem >= 0x8000000) *archmem = 0x8000000; //128MB memory max!
	}
	else if (is_i430fx==2) //i440fx is limited to 1GB
	{
		if (*archmem >= 0x40000000) *archmem = 0x40000000; //1GB memory max!
	}

	if ((uint_64)*archmem>=((uint_64)4096<<20)) //Past 4G?
	{
		*archmem = (uint_32)((((uint_64)4096)<<20)-MEMORY_BLOCKSIZE_AT_HIGH); //Limit to the max, just below 4G!
	}

	debugrow("Finished detecting MMU memory size to use...");

	if (tosave)
	{
		forceBIOSSave(); //Force BIOS save!
	}

	debugrow("Detected memory size ready.");
}



void BIOS_LoadDefaults(int tosave) //Load BIOS defaults, but not memory size!
{
	if (exec_showchecksumerrors)
	{
		printmsg(0xF,"\r\nSettings Checksum Error. "); //Checksum error.
	}

	uint_32 memorytypes[6];
	//Backup memory settings first!
	memorytypes[0] = BIOS_Settings.XTCMOS.memory;
	memorytypes[1] = BIOS_Settings.ATCMOS.memory;
	memorytypes[2] = BIOS_Settings.CompaqCMOS.memory;
	memorytypes[3] = BIOS_Settings.PS2CMOS.memory;
	memorytypes[4] = BIOS_Settings.i430fxCMOS.memory;
	memorytypes[5] = BIOS_Settings.i440fxCMOS.memory;

	//Zero out!
	memset(&BIOS_Settings,0,sizeof(BIOS_Settings)); //Reset to empty!

	//Restore memory settings!
	BIOS_Settings.XTCMOS.memory = memorytypes[0];
	BIOS_Settings.ATCMOS.memory = memorytypes[1];
	BIOS_Settings.CompaqCMOS.memory = memorytypes[2];
	BIOS_Settings.PS2CMOS.memory = memorytypes[3];
	BIOS_Settings.i430fxCMOS.memory = memorytypes[4];
	BIOS_Settings.i440fxCMOS.memory = memorytypes[5];

	if (!file_exists(BIOS_Settings_file)) //New file?
	{
		BIOS_Settings.firstrun = 1; //We're the first run!
	}
	
	//Now load the defaults.

	memset(&BIOS_Settings.floppy0[0],0,sizeof(BIOS_Settings.floppy0));
	BIOS_Settings.floppy0_readonly = 0; //Not read-only!
	memset(&BIOS_Settings.floppy1[0],0,sizeof(BIOS_Settings.floppy1));
	BIOS_Settings.floppy1_readonly = 0; //Not read-only!
	memset(&BIOS_Settings.hdd0[0],0,sizeof(BIOS_Settings.hdd0));
	BIOS_Settings.hdd0_readonly = 0; //Not read-only!
	memset(&BIOS_Settings.hdd1[0],0,sizeof(BIOS_Settings.hdd1));
	BIOS_Settings.hdd1_readonly = 0; //Not read-only!

	memset(&BIOS_Settings.cdrom0[0],0,sizeof(BIOS_Settings.cdrom0));
	memset(&BIOS_Settings.cdrom1[0],0,sizeof(BIOS_Settings.cdrom1));
//CD-ROM always read-only!

	memset(&BIOS_Settings.SoundFont[0],0,sizeof(BIOS_Settings.SoundFont)); //Reset the currently mounted soundfont!

	BIOS_Settings.bootorder = DEFAULT_BOOT_ORDER; //Default boot order!
	*(getarchemulated_CPU()) = DEFAULT_CPU; //Which CPU to be emulated?
	*(getarchemulated_CPUs()) = DEFAULT_CPUS; //Which CPU to be emulated?

	BIOS_Settings.debugmode = DEFAULT_DEBUGMODE; //Default debug mode!
	BIOS_Settings.executionmode = DEFAULT_EXECUTIONMODE; //Default execution mode!
	BIOS_Settings.debugger_log = DEFAULT_DEBUGGERLOG; //Default debugger logging!

	BIOS_Settings.GPU_AllowDirectPlot = DEFAULT_DIRECTPLOT; //Default: automatic 1:1 mapping!
	BIOS_Settings.aspectratio = DEFAULT_ASPECTRATIO; //Don't keep aspect ratio by default!
	BIOS_Settings.bwmonitor = DEFAULT_BWMONITOR; //Default B/W monitor setting!
	BIOS_Settings.bwmonitor_luminancemode = DEFAULT_BWMONITOR_LUMINANCEMODE; //Default B/W monitor setting!
	BIOS_Settings.SoundSource_Volume = DEFAULT_SSOURCEVOL; //Default soundsource volume knob!
	BIOS_Settings.GameBlaster_Volume = DEFAULT_BLASTERVOL; //Default Game Blaster volume knob!
	BIOS_Settings.ShowFramerate = DEFAULT_FRAMERATE; //Default framerate setting!
	BIOS_Settings.SVGA_DACmode = DEFAULT_SVGA_DACMODE; //Default SVGA DAC mode!
	BIOS_Settings.video_blackpedestal = DEFAULT_VIDEO_BLACKPEDESTAL; //Default VGA black pedestal!
	BIOS_Settings.ET4000_extensions = DEFAULT_ET4000_EXTENSIONS; //Default SVGA DAC mode!
	BIOS_Settings.VGASynchronization = DEFAULT_VGASYNCHRONIZATION; //Default VGA synchronization setting!
	BIOS_Settings.diagnosticsportoutput_breakpoint = DEFAULT_DIAGNOSTICSPORTOUTPUT_BREAKPOINT; //Default breakpoint setting!
	BIOS_Settings.diagnosticsportoutput_timeout = DEFAULT_DIAGNOSTICSPORTOUTPUT_TIMEOUT; //Default breakpoint setting!
	BIOS_Settings.useDirectMIDI = DEFAULT_DIRECTMIDIMODE; //Default breakpoint setting!
	BIOS_Settings.BIOSROMmode = DEFAULT_BIOSROMMODE; //Default BIOS ROM mode setting!
	BIOS_Settings.modemlistenport = DEFAULT_MODEMLISTENPORT; //Default modem listen port!
	
	BIOS_Settings.version = BIOS_VERSION; //Current version loaded!
	keyboard_loadDefaults(); //Load the defaults for the keyboard!
	
	BIOS_Settings.useAdlib = 0; //Emulate Adlib?
	BIOS_Settings.useLPTDAC = 0; //Emulate Covox/Disney Sound Source?
	BIOS_Settings.usePCSpeaker = 0; //Sound PC Speaker?

	if (tosave) //Save settings?
	{
		forceBIOSSave(); //Save the BIOS!
	}
	if (exec_showchecksumerrors)
	{
		printmsg(0xF,"Defaults loaded.\r\n"); //Show that the defaults are loaded.
	}
}

byte telleof(BIGFILE *f) //Are we @eof?
{
	FILEPOS curpos = 0; //Cur pos!
	FILEPOS endpos = 0; //End pos!
	byte result = 0; //Result!
	curpos = emuftell64(f); //Cur position!
	emufseek64(f,0,SEEK_END); //Goto EOF!
	endpos = emuftell64(f); //End position!

	emufseek64(f,curpos,SEEK_SET); //Return!
	result = (curpos==endpos); //@EOF?
	return result; //Give the result!
}

uint_32 BIOS_getChecksum() //Get the BIOS checksum!
{
	uint_32 result=0,total=sizeof(BIOS_Settings); //Initialise our info!
	byte *data = (byte *)&BIOS_Settings; //First byte of data!
	for (;total;) //Anything left?
	{
		result += (uint_32)*data++; //Add the data to the result!
		--total; //One byte of data processed!
	}
	return result; //Give the simple checksum of the loaded settings!
}

void loadBIOSCMOS(CMOSDATA *CMOS, char *section, INI_FILE *i)
{
	word index;
	char field[256];
	CMOS->memory = (uint_32)get_private_profile_uint64(section, "memory", 0, i);
	CMOS->timedivergeance = get_private_profile_int64(section,"TimeDivergeance_seconds",0,i);
	CMOS->timedivergeance2 = get_private_profile_int64(section,"TimeDivergeance_microseconds",0,i);
	CMOS->s100 = (byte)get_private_profile_uint64(section,"s100",0,i);
	CMOS->s10000 = (byte)get_private_profile_uint64(section,"s10000",0,i);
	CMOS->centuryisbinary = (byte)get_private_profile_uint64(section,"centuryisbinary",0,i);
	CMOS->cycletiming = (byte)get_private_profile_uint64(section,"cycletiming",0,i);
	CMOS->floppy0_nodisk_type = (byte)get_private_profile_uint64(section, "floppy0_nodisk_type", 0, i);
	if (CMOS->floppy0_nodisk_type >= NUMFLOPPYGEOMETRIES) CMOS->floppy0_nodisk_type = 0; //Default if invalid!
	CMOS->floppy1_nodisk_type = (byte)get_private_profile_uint64(section, "floppy1_nodisk_type", 0, i);
	if (CMOS->floppy1_nodisk_type >= NUMFLOPPYGEOMETRIES) CMOS->floppy1_nodisk_type = 0; //Default if invalid!

	CMOS->emulated_CPU = LIMITRANGE((byte)get_private_profile_uint64(section, "cpu", BIOS_Settings.emulated_CPU, i),CPU_MIN,CPU_MAX); //Limited CPU range!
	CMOS->emulated_CPUs = LIMITRANGE((byte)get_private_profile_uint64(section, "cpus", DEFAULT_CPUS, i),0,MAXCPUS); //Limited CPU range!
	CMOS->DataBusSize = LIMITRANGE((byte)get_private_profile_uint64(section, "databussize", BIOS_Settings.DataBusSize, i),0,1); //The size of the emulated BUS. 0=Normal bus, 1=8-bit bus when available for the CPU!
	CMOS->CPUspeed = (uint_32)get_private_profile_uint64(section, "cpuspeed", BIOS_Settings.CPUSpeed, i);
	CMOS->TurboCPUspeed = (uint_32)get_private_profile_uint64(section, "turbocpuspeed", BIOS_Settings.TurboCPUSpeed, i);
	CMOS->useTurboCPUSpeed = LIMITRANGE((byte)get_private_profile_uint64(section, "useturbocpuspeed", BIOS_Settings.useTurboSpeed, i),0,1); //Are we to use Turbo CPU speed?
	CMOS->clockingmode = LIMITRANGE((byte)get_private_profile_uint64(section, "clockingmode", BIOS_Settings.clockingmode, i),CLOCKINGMODE_MIN,CLOCKINGMODE_MAX); //Are we using the IPS clock?
	CMOS->CPUIDmode = LIMITRANGE((byte)get_private_profile_uint64(section, "CPUIDmode", DEFAULT_CPUIDMODE, i), 0, 2); //Are we using the CPUID mode?

	for (index=0;index<NUMITEMS(CMOS->DATA80.data);++index) //Process extra RAM data!
	{
		snprintf(field,sizeof(field),"RAM%02X",index); //The field!
		CMOS->DATA80.data[index] = (byte)get_private_profile_uint64(section,&field[0],0,i);
	}
	for (index=0;index<NUMITEMS(CMOS->extraRAMdata);++index) //Process extra RAM data!
	{
		snprintf(field,sizeof(field),"extraRAM%02X",index); //The field!
		CMOS->extraRAMdata[index] = (byte)get_private_profile_uint64(section,&field[0],0,i);
	}
}

char phonebookentry[256] = "";

byte loadedsettings_loaded = 0;
BIOS_Settings_TYPE loadedsettings; //The settings that have been loaded!

void BIOS_LoadData() //Load BIOS settings!
{
	if (__HW_DISABLED) return; //Abort!
	BIGFILE *f;
	INI_FILE* inifile;
	byte defaultsapplied = 0; //Defaults have been applied?
	word c;
	memset(&phonebookentry, 0, sizeof(phonebookentry)); //Init!

	if (loadedsettings_loaded)
	{
		memcpy(&BIOS_Settings, &loadedsettings, sizeof(BIOS_Settings)); //Reload from buffer!
		return;
	}
	f = emufopen64(BIOS_Settings_file, "rb"); //Open BIOS file!

	if (!f) //Not loaded?
	{
		BIOS_LoadDefaults(1); //Load the defaults, save!
		return; //We've loaded the defaults!
	}

	emufclose64(f); //Close the settings file!

	memset(&BIOS_Settings, 0, sizeof(BIOS_Settings)); //Init settings to their defaults!

	inifile = readinifile(BIOS_Settings_file); //Read the ini file!
	if (!inifile) //Failed?
	{
		BIOS_LoadDefaults(1); //Load the defaults, save!
		return; //We've loaded the defaults!
	}

	//General
	BIOS_Settings.version = (byte)get_private_profile_uint64("general", "version", BIOS_VERSION, inifile);
	BIOS_Settings.firstrun = (byte)get_private_profile_uint64("general", "firstrun", 1, inifile)?1:0; //Is this the first run of this BIOS?
	BIOS_Settings.BIOSmenu_font = (byte)get_private_profile_uint64("general", "settingsmenufont", 0, inifile); //The selected font for the BIOS menu!
	BIOS_Settings.backgroundpolicy = (byte)get_private_profile_uint64("general", "backgroundpolicy", DEFAULT_BACKGROUNDPOLICY, inifile); //The selected font for the BIOS menu!

	//Machine
	BIOS_Settings.emulated_CPU = LIMITRANGE((word)get_private_profile_uint64("machine", "cpu", DEFAULT_CPU, inifile),CPU_MIN,CPU_MAX);
	BIOS_Settings.DataBusSize = (byte)get_private_profile_uint64("machine", "databussize", 0, inifile); //The size of the emulated BUS. 0=Normal bus, 1=8-bit bus when available for the CPU!
	BIOS_Settings.architecture = LIMITRANGE((byte)get_private_profile_uint64("machine", "architecture", ARCHITECTURE_XT, inifile),ARCHITECTURE_MIN,ARCHITECTURE_MAX); //Are we using the XT/AT/PS/2 architecture?
	BIOS_Settings.executionmode = LIMITRANGE((byte)get_private_profile_uint64("machine", "executionmode", DEFAULT_EXECUTIONMODE, inifile),EXECUTIONMODE_MIN,EXECUTIONMODE_MAX); //What mode to execute in during runtime?
	BIOS_Settings.CPUSpeed = (uint_32)get_private_profile_uint64("machine", "cpuspeed", 0, inifile);
	BIOS_Settings.ShowCPUSpeed = (byte)get_private_profile_uint64("machine", "showcpuspeed", 0, inifile); //Show the relative CPU speed together with the framerate?
	BIOS_Settings.TurboCPUSpeed = (uint_32)get_private_profile_uint64("machine", "turbocpuspeed", 0, inifile);
	BIOS_Settings.useTurboSpeed = LIMITRANGE((byte)get_private_profile_uint64("machine", "useturbocpuspeed", 0, inifile),0,1); //Are we to use Turbo CPU speed?
	BIOS_Settings.clockingmode = LIMITRANGE((byte)get_private_profile_uint64("machine", "clockingmode", DEFAULT_CLOCKINGMODE, inifile),CLOCKINGMODE_MIN,CLOCKINGMODE_MAX); //Are we using the IPS clock?
	BIOS_Settings.BIOSROMmode = LIMITRANGE((byte)get_private_profile_uint64("machine", "BIOSROMmode", DEFAULT_BIOSROMMODE, inifile),BIOSROMMODE_MIN,BIOSROMMODE_MAX); //BIOS ROM mode.
	BIOS_Settings.InboardInitialWaitstates = LIMITRANGE((byte)get_private_profile_uint64("machine", "inboardinitialwaitstates", DEFAULT_INBOARDINITIALWAITSTATES, inifile),0,1); //Inboard 386 initial delay used?

	//Debugger
	BIOS_Settings.debugmode = (byte)get_private_profile_uint64("debugger", "debugmode", DEFAULT_DEBUGMODE, inifile);
	BIOS_Settings.debugger_log = LIMITRANGE((byte)get_private_profile_uint64("debugger", "debuggerlog", DEFAULT_DEBUGGERLOG, inifile),DEBUGGERLOG_MIN,DEBUGGERLOG_MAX);
	BIOS_Settings.debugger_logstates = LIMITRANGE((byte)get_private_profile_uint64("debugger", "logstates", DEFAULT_DEBUGGERSTATELOG, inifile),DEBUGGERSTATELOG_MIN,DEBUGGERSTATELOG_MAX); //Are we logging states? 1=Log states, 0=Don't log states!
	BIOS_Settings.debugger_logregisters = LIMITRANGE((byte)get_private_profile_uint64("debugger", "logregisters", DEFAULT_DEBUGGERREGISTERSLOG, inifile),DEBUGGERREGISTERSLOG_MIN,DEBUGGERREGISTERSLOG_MAX); //Are we logging states? 1=Log states, 0=Don't log states!
	BIOS_Settings.breakpoint[0] = get_private_profile_uint64("debugger", "breakpoint", 0, inifile); //The used breakpoint segment:offset and mode!
	BIOS_Settings.breakpoint[1] = get_private_profile_uint64("debugger", "breakpoint2", 0, inifile); //The used breakpoint segment:offset and mode!
	BIOS_Settings.breakpoint[2] = get_private_profile_uint64("debugger", "breakpoint3", 0, inifile); //The used breakpoint segment:offset and mode!
	BIOS_Settings.breakpoint[3] = get_private_profile_uint64("debugger", "breakpoint4", 0, inifile); //The used breakpoint segment:offset and mode!
	BIOS_Settings.breakpoint[4] = get_private_profile_uint64("debugger", "breakpoint5", 0, inifile); //The used breakpoint segment:offset and mode!
	BIOS_Settings.taskBreakpoint = get_private_profile_uint64("debugger", "taskbreakpoint", 0, inifile); //The used breakpoint segment:offset and mode!
	BIOS_Settings.FSBreakpoint = get_private_profile_uint64("debugger", "FSbreakpoint", 0, inifile); //The used breakpoint segment:offset and mode!
	BIOS_Settings.CR3breakpoint = get_private_profile_uint64("debugger", "CR3breakpoint", 0, inifile); //The used breakpoint segment:offset and mode!
	BIOS_Settings.diagnosticsportoutput_breakpoint = LIMITRANGE((sword)get_private_profile_int64("debugger", "diagnosticsport_breakpoint", DEFAULT_DIAGNOSTICSPORTOUTPUT_BREAKPOINT, inifile),-1,0xFF); //Use a diagnostics port breakpoint?
	BIOS_Settings.diagnosticsportoutput_timeout = (uint_32)get_private_profile_uint64("debugger", "diagnosticsport_timeout", DEFAULT_DIAGNOSTICSPORTOUTPUT_TIMEOUT, inifile); //Breakpoint timeout used!
	BIOS_Settings.advancedlog = (byte)get_private_profile_uint64("debugger", "advancedlog", DEFAULT_ADVANCEDLOG, inifile); //The selected font for the BIOS menu!

	//Video
	BIOS_Settings.VGA_Mode = (byte)get_private_profile_uint64("video", "videocard", DEFAULT_VIDEOCARD, inifile); //Enable VGA NMI on precursors?
	BIOS_Settings.CGAModel = (byte)get_private_profile_uint64("video", "CGAmodel", DEFAULT_CGAMODEL, inifile); //What kind of CGA is emulated? Bit0=NTSC, Bit1=New-style CGA
	BIOS_Settings.VRAM_size = (uint_32)get_private_profile_uint64("video", "VRAM", 0, inifile); //(S)VGA VRAM size!
	BIOS_Settings.VGASynchronization = (byte)get_private_profile_uint64("video", "synchronization", DEFAULT_VGASYNCHRONIZATION, inifile); //VGA synchronization setting. 0=Automatic synchronization based on Host CPU. 1=Tight VGA Synchronization with the CPU.
	BIOS_Settings.GPU_AllowDirectPlot = (byte)get_private_profile_uint64("video", "directplot", DEFAULT_DIRECTPLOT, inifile); //Allow VGA Direct Plot: 1 for automatic 1:1 mapping, 0 for always dynamic, 2 for force 1:1 mapping?
	BIOS_Settings.aspectratio = (byte)get_private_profile_uint64("video", "aspectratio", DEFAULT_ASPECTRATIO, inifile); //The aspect ratio to use?
	BIOS_Settings.bwmonitor = LIMITRANGE((byte)get_private_profile_uint64("video", "bwmonitor", DEFAULT_BWMONITOR, inifile),BWMONITOR_MIN,BWMONITOR_MAX); //Are we a b/w monitor?
	BIOS_Settings.bwmonitor_luminancemode = LIMITRANGE((byte)get_private_profile_uint64("video", "bwmonitor_luminancemode", DEFAULT_BWMONITOR_LUMINANCEMODE, inifile),BWMONITOR_LUMINANCEMODE_MIN,BWMONITOR_LUMINANCEMODE_MAX); //b/w monitor luminance mode?
	BIOS_Settings.ShowFramerate = LIMITRANGE((byte)get_private_profile_uint64("video", "showframerate", DEFAULT_FRAMERATE, inifile),0,1); //Show the frame rate?
	BIOS_Settings.SVGA_DACmode = LIMITRANGE((byte)get_private_profile_uint64("video", "SVGA_DACmode", DEFAULT_FRAMERATE, inifile), SVGA_DACMODE_MIN, SVGA_DACMODE_MAX); //Show the frame rate?
	BIOS_Settings.video_blackpedestal = LIMITRANGE((byte)get_private_profile_uint64("video", "blackpedestal", DEFAULT_FRAMERATE, inifile), VIDEO_BLACKPEDESTAL_MIN, VIDEO_BLACKPEDESTAL_MAX); //Show the frame rate?
	BIOS_Settings.ET4000_extensions = LIMITRANGE((byte)get_private_profile_uint64("video", "ET4000_extensions", DEFAULT_FRAMERATE, inifile), ET4000_EXTENSIONS_MIN, ET4000_EXTENSIONS_MAX); //Show the frame rate?

	//Sound
	BIOS_Settings.usePCSpeaker = LIMITRANGE((byte)get_private_profile_uint64("sound", "speaker", 1, inifile),0,1); //Emulate PC Speaker sound?
	BIOS_Settings.useAdlib = LIMITRANGE((byte)get_private_profile_uint64("sound", "adlib", 1, inifile),0,1); //Emulate Adlib?
	BIOS_Settings.useLPTDAC = LIMITRANGE((byte)get_private_profile_uint64("sound", "LPTDAC", 1, inifile),0,1); //Emulate Covox/Disney Sound Source?
	get_private_profile_string("sound", "soundfont", "", &BIOS_Settings.SoundFont[0], sizeof(BIOS_Settings.SoundFont), inifile); //Read entry!
	BIOS_Settings.useDirectMIDI = LIMITRANGE((byte)get_private_profile_uint64("sound", "directmidi", DEFAULT_DIRECTMIDIMODE, inifile),0,1); //Use Direct MIDI synthesis by using a passthrough to the OS?
	BIOS_Settings.useGameBlaster = LIMITRANGE((byte)get_private_profile_uint64("sound", "gameblaster", 1, inifile),0,1); //Emulate Game Blaster?
	BIOS_Settings.GameBlaster_Volume = (uint_32)get_private_profile_uint64("sound", "gameblaster_volume", 100, inifile); //The Game Blaster volume knob!
	BIOS_Settings.useSoundBlaster = (byte)get_private_profile_uint64("sound", "soundblaster", DEFAULT_SOUNDBLASTER, inifile); //Emulate Sound Blaster?
	BIOS_Settings.SoundSource_Volume = (uint_32)get_private_profile_uint64("sound", "soundsource_volume", DEFAULT_SSOURCEVOL, inifile); //The sound source volume knob!

	//Modem
	BIOS_Settings.modemlistenport = LIMITRANGE((word)get_private_profile_uint64("modem", "listenport", DEFAULT_MODEMLISTENPORT, inifile),0,0xFFFF); //Modem listen port!
	BIOS_Settings.nullmodem = LIMITRANGE((word)get_private_profile_uint64("modem", "nullmodem", DEFAULT_NULLMODEM, inifile), NULLMODEM_MIN, NULLMODEM_MAX); //nullmodem mode!
	for (c = 0; c < NUMITEMS(BIOS_Settings.phonebook); ++c) //Process all phonebook entries!
	{
		snprintf(phonebookentry, sizeof(phonebookentry), "phonebook%u", c); //The entry to use!
		get_private_profile_string("modem", phonebookentry, "", &BIOS_Settings.phonebook[c][0], sizeof(BIOS_Settings.phonebook[0]), inifile); //Read entry!
	}

	BIOS_Settings.ethernetserver_settings.ethernetcard = get_private_profile_int64("modem", "ethernetcard", -1, inifile); //Ethernet card to use!
	get_private_profile_string("modem", "hostMACaddress", "", &BIOS_Settings.ethernetserver_settings.MACaddress[0], sizeof(BIOS_Settings.ethernetserver_settings.MACaddress), inifile); //Read entry!
	get_private_profile_string("modem", "hostIPaddress", "", &BIOS_Settings.ethernetserver_settings.hostIPaddress[0], sizeof(BIOS_Settings.ethernetserver_settings.hostIPaddress), inifile); //Read entry!
	get_private_profile_string("modem", "hostsubnetmaskIPaddress", "", &BIOS_Settings.ethernetserver_settings.hostsubnetmaskIPaddress[0], sizeof(BIOS_Settings.ethernetserver_settings.subnetmaskIPaddress), inifile); //Read entry!
	get_private_profile_string("modem", "gatewayMACaddress", "", &BIOS_Settings.ethernetserver_settings.gatewayMACaddress[0], sizeof(BIOS_Settings.ethernetserver_settings.gatewayMACaddress), inifile); //Read entry!
	get_private_profile_string("modem", "gatewayIPaddress", "", &BIOS_Settings.ethernetserver_settings.gatewayIPaddress[0], sizeof(BIOS_Settings.ethernetserver_settings.gatewayIPaddress), inifile); //Read entry!
	get_private_profile_string("modem", "DNS1IPaddress", "", &BIOS_Settings.ethernetserver_settings.DNS1IPaddress[0], sizeof(BIOS_Settings.ethernetserver_settings.DNS1IPaddress), inifile); //Read entry!
	get_private_profile_string("modem", "DNS2IPaddress", "", &BIOS_Settings.ethernetserver_settings.DNS2IPaddress[0], sizeof(BIOS_Settings.ethernetserver_settings.DNS2IPaddress), inifile); //Read entry!
	get_private_profile_string("modem", "NBNS1IPaddress", "", &BIOS_Settings.ethernetserver_settings.NBNS1IPaddress[0], sizeof(BIOS_Settings.ethernetserver_settings.NBNS1IPaddress), inifile); //Read entry!
	get_private_profile_string("modem", "NBNS2IPaddress", "", &BIOS_Settings.ethernetserver_settings.NBNS2IPaddress[0], sizeof(BIOS_Settings.ethernetserver_settings.NBNS2IPaddress), inifile); //Read entry!
	get_private_profile_string("modem", "subnetmaskIPaddress", "", &BIOS_Settings.ethernetserver_settings.subnetmaskIPaddress[0], sizeof(BIOS_Settings.ethernetserver_settings.subnetmaskIPaddress), inifile); //Read entry!

	for (c = 0; c < NUMITEMS(BIOS_Settings.phonebook); ++c) //Process all phonebook entries!
	{
		if (c) //Normal user?
		{
			snprintf(phonebookentry, sizeof(phonebookentry), "username%u", c); //The entry to use!
		}
		else
		{
			safestrcpy(phonebookentry, sizeof(phonebookentry), "username"); //The entry to use!
		}
		get_private_profile_string("modem", phonebookentry, "", &BIOS_Settings.ethernetserver_settings.users[c].username[0], sizeof(BIOS_Settings.ethernetserver_settings.users[c].username), inifile); //Read entry!
		if (c) //Normal user?
		{
			snprintf(phonebookentry, sizeof(phonebookentry), "password%u", c); //The entry to use!
		}
		else
		{
			safestrcpy(phonebookentry, sizeof(phonebookentry), "password"); //The entry to use!
		}
		get_private_profile_string("modem", phonebookentry, "", &BIOS_Settings.ethernetserver_settings.users[c].password[0], sizeof(BIOS_Settings.ethernetserver_settings.users[c].password), inifile); //Read entry!
		if (c) //Normal user?
		{
			snprintf(phonebookentry, sizeof(phonebookentry), "IPaddress%u", c); //The entry to use!
		}
		else
		{
			safestrcpy(phonebookentry, sizeof(phonebookentry), "IPaddress"); //The entry to use!
		}
		get_private_profile_string("modem", phonebookentry, "", &BIOS_Settings.ethernetserver_settings.users[c].IPaddress[0], sizeof(BIOS_Settings.ethernetserver_settings.users[c].IPaddress), inifile); //Read entry!
	}

	//Disks
	get_private_profile_string("disks","floppy0","",&BIOS_Settings.floppy0[0],sizeof(BIOS_Settings.floppy0),inifile); //Read entry!
	BIOS_Settings.floppy0_readonly = (byte)get_private_profile_uint64("disks","floppy0_readonly",0,inifile);
	get_private_profile_string("disks","floppy1","",&BIOS_Settings.floppy1[0],sizeof(BIOS_Settings.floppy1),inifile); //Read entry!
	BIOS_Settings.floppy1_readonly = (byte)get_private_profile_uint64("disks","floppy1_readonly",0,inifile);
	get_private_profile_string("disks","hdd0","",&BIOS_Settings.hdd0[0],sizeof(BIOS_Settings.hdd0),inifile); //Read entry!
	BIOS_Settings.hdd0_readonly = (byte)get_private_profile_uint64("disks","hdd0_readonly",0,inifile);
	get_private_profile_string("disks","hdd1","",&BIOS_Settings.hdd1[0],sizeof(BIOS_Settings.hdd1),inifile); //Read entry!
	BIOS_Settings.hdd1_readonly = (byte)get_private_profile_uint64("disks","hdd1_readonly",0,inifile);
	get_private_profile_string("disks","cdrom0","",&BIOS_Settings.cdrom0[0],sizeof(BIOS_Settings.cdrom0),inifile); //Read entry!
	get_private_profile_string("disks","cdrom1","",&BIOS_Settings.cdrom1[0],sizeof(BIOS_Settings.cdrom1),inifile); //Read entry!


	//BIOS
	BIOS_Settings.bootorder = (byte)get_private_profile_uint64("bios","bootorder",DEFAULT_BOOT_ORDER,inifile);

	//Input
	BIOS_Settings.input_settings.analog_minrange = (byte)get_private_profile_uint64("input","analog_minrange",0,inifile); //Minimum adjustment x&y(0,0) for keyboard&mouse to change states (from center)
	BIOS_Settings.input_settings.fontcolor = (byte)get_private_profile_uint64("input","keyboard_fontcolor",0xFF,inifile);
	BIOS_Settings.input_settings.bordercolor = (byte)get_private_profile_uint64("input","keyboard_bordercolor",0xFF,inifile);
	BIOS_Settings.input_settings.activecolor = (byte)get_private_profile_uint64("input","keyboard_activecolor",0xFF,inifile);
	BIOS_Settings.input_settings.specialcolor = (byte)get_private_profile_uint64("input","keyboard_specialcolor",0xFF,inifile);
	BIOS_Settings.input_settings.specialbordercolor = (byte)get_private_profile_uint64("input","keyboard_specialbordercolor",0xFF,inifile);
	BIOS_Settings.input_settings.specialactivecolor = (byte)get_private_profile_uint64("input","keyboard_specialactivecolor",0xFF,inifile);
	BIOS_Settings.input_settings.DirectInput_remap_RCTRL_to_LWIN = LIMITRANGE((byte)get_private_profile_uint64("input","DirectInput_remap_RCTRL_to_LWIN",0,inifile),0,1); //Remap RCTRL to LWIN in Direct Input?
	BIOS_Settings.input_settings.DirectInput_remap_accentgrave_to_tab = LIMITRANGE((byte)get_private_profile_uint64("input","DirectInput_remap_accentgrave_to_tab",0,inifile),0,1); //Remap Accent Grave to Tab during LALT?
	BIOS_Settings.input_settings.DirectInput_remap_NUM0_to_Delete = LIMITRANGE((byte)get_private_profile_uint64("input", "DirectInput_remap_NUM0_to_Delete", 0, inifile),0,1); //Remap NUM0 to Delete?
	BIOS_Settings.input_settings.DirectInput_Disable_RALT = LIMITRANGE((byte)get_private_profile_uint64("input","DirectInput_disable_RALT",0,inifile),0,1); //Disable RALT?
	for (c=0;c<6;++c) //Validate colors and set default colors when invalid!
	{
		if (BIOS_Settings.input_settings.colors[c]>0xF) keyboard_loadDefaultColor((byte)c); //Set default color when invalid!
	}

	//Gamingmode
	byte modefields[16][256] = {"","_triangle","_square","_cross","_circle"};
	char buttons[15][256] = {"start","left","up","right","down","ltrigger","rtrigger","triangle","circle","cross","square","analogleft","analogup","analogright","analogdown"}; //The names of all mappable buttons!
	byte button, modefield;
	char buttonstr[256];
	memset(&buttonstr,0,sizeof(buttonstr)); //Init button string!
	for (modefield = 0; modefield < 5; ++modefield)
	{
		snprintf(buttonstr, sizeof(buttonstr), "gamingmode_map_joystick%s", modefields[modefield]);
		BIOS_Settings.input_settings.usegamingmode_joystick[modefield] = (sword)get_private_profile_int64("gamingmode", buttonstr, (modefield?0:1), inifile);
		for (button = 0; button < 15; ++button) //Process all buttons!
		{
			snprintf(buttonstr, sizeof(buttonstr), "gamingmode_map_%s_key%s", buttons[button],modefields[modefield]);
			BIOS_Settings.input_settings.keyboard_gamemodemappings[modefield][button] = (sword)get_private_profile_int64("gamingmode", buttonstr, -1, inifile);
			snprintf(buttonstr, sizeof(buttonstr), "gamingmode_map_%s_shiftstate%s", buttons[button], modefields[modefield]);
			BIOS_Settings.input_settings.keyboard_gamemodemappings_alt[modefield][button] = (byte)get_private_profile_uint64("gamingmode", buttonstr, 0, inifile);
			snprintf(buttonstr, sizeof(buttonstr), "gamingmode_map_%s_mousebuttons%s", buttons[button], modefields[modefield]);
			BIOS_Settings.input_settings.mouse_gamemodemappings[modefield][button] = (byte)get_private_profile_uint64("gamingmode", buttonstr, 0, inifile);
		}
	}

	BIOS_Settings.input_settings.gamingmode_joystick = (byte)get_private_profile_uint64("gamingmode","joystick",0,inifile); //Use the joystick input instead of mapped input during gaming mode?

	//XTCMOS
	BIOS_Settings.got_XTCMOS = (byte)get_private_profile_uint64("XTCMOS","gotCMOS",0,inifile); //Gotten an CMOS?
	loadBIOSCMOS(&BIOS_Settings.XTCMOS,"XTCMOS",inifile); //Load the CMOS from the file!

	//ATCMOS
	BIOS_Settings.got_ATCMOS = (byte)get_private_profile_uint64("ATCMOS","gotCMOS",0,inifile); //Gotten an CMOS?
	loadBIOSCMOS(&BIOS_Settings.ATCMOS,"ATCMOS",inifile); //Load the CMOS from the file!

	//CompaqCMOS
	BIOS_Settings.got_CompaqCMOS = (byte)get_private_profile_uint64("CompaqCMOS","gotCMOS",0,inifile); //Gotten an CMOS?
	loadBIOSCMOS(&BIOS_Settings.CompaqCMOS,"CompaqCMOS",inifile); //The full saved CMOS!

	//PS2CMOS
	BIOS_Settings.got_PS2CMOS = (byte)get_private_profile_uint64("PS2CMOS","gotCMOS",0,inifile); //Gotten an CMOS?
	loadBIOSCMOS(&BIOS_Settings.PS2CMOS,"PS2CMOS",inifile); //Load the CMOS from the file!

	//i430fxCMOS
	BIOS_Settings.got_i430fxCMOS = (byte)get_private_profile_uint64("i430fxCMOS", "gotCMOS", 0, inifile); //Gotten an CMOS?
	loadBIOSCMOS(&BIOS_Settings.i430fxCMOS, "i430fxCMOS",inifile); //Load the CMOS from the file!

	//i440fxCMOS
	BIOS_Settings.got_i440fxCMOS = (byte)get_private_profile_uint64("i440fxCMOS", "gotCMOS", 0, inifile); //Gotten an CMOS?
	loadBIOSCMOS(&BIOS_Settings.i440fxCMOS, "i440fxCMOS", inifile); //Load the CMOS from the file!

	//BIOS settings have been loaded.

	closeinifile(&inifile); //Close the ini file!

	if (BIOS_Settings.version!=BIOS_VERSION) //Not compatible with our version?
	{
		dolog("Settings","Error: Invalid settings version.");
		BIOS_LoadDefaults(1); //Load the defaults, save!
		return; //We've loaded the defaults because 
	}

	if (defaultsapplied) //To save, because defaults have been applied?
	{
		BIOS_SaveData(); //Save our Settings data!
	}

	memcpy(&loadedsettings, &BIOS_Settings, sizeof(BIOS_Settings)); //Reload from buffer!
	loadedsettings_loaded = 1; //Buffered in memory!
}

void backupCMOSglobalsettings(CMOSDATA *CMOS, CMOSGLOBALBACKUPDATA *backupdata)
{
	backupdata->memorybackup = CMOS->memory; //Backup!
	backupdata->emulated_CPUbackup = CMOS->emulated_CPU; //Emulated CPU?
	backupdata->emulated_CPUsbackup = CMOS->emulated_CPUs; //Emulated CPU?
	backupdata->CPUspeedbackup = CMOS->CPUspeed; //CPU speed
	backupdata->TurboCPUspeedbackup = CMOS->TurboCPUspeed; //Turbo CPU speed
	backupdata->useTurboCPUSpeedbackup = CMOS->useTurboCPUSpeed; //Are we to use Turbo CPU speed?
	backupdata->clockingmodebackup = CMOS->clockingmode; //Are we using the IPS clock instead of cycle-accurate clock?
	backupdata->DataBusSizebackup = CMOS->DataBusSize; //The size of the emulated BUS. 0=Normal bus, 1=8-bit bus when available for the CPU!
	backupdata->CPUIDmodebackup = CMOS->CPUIDmode; //CPU ID mode?
}

void restoreCMOSglobalsettings(CMOSDATA *CMOS, CMOSGLOBALBACKUPDATA *backupdata)
{
	CMOS->memory = backupdata->memorybackup;
	CMOS->emulated_CPU = backupdata->emulated_CPUbackup; //Emulated CPU?
	CMOS->emulated_CPUs = backupdata->emulated_CPUsbackup; //Emulated CPU?
	CMOS->CPUspeed = backupdata->CPUspeedbackup; //CPU speed
	CMOS->TurboCPUspeed = backupdata->TurboCPUspeedbackup; //Turbo CPU speed
	CMOS->useTurboCPUSpeed = backupdata->useTurboCPUSpeedbackup; //Are we to use Turbo CPU speed?
	CMOS->clockingmode = backupdata->clockingmodebackup; //Are we using the IPS clock instead of cycle-accurate clock?
	CMOS->DataBusSize = backupdata->DataBusSizebackup; //The size of the emulated BUS. 0=Normal bus, 1=8-bit bus when available for the CPU!
	CMOS->CPUIDmode = backupdata->CPUIDmodebackup; //CPU ID mode?
}

byte saveBIOSCMOS(CMOSDATA *CMOS, char *section, char *section_comment, INI_FILE *i)
{
	word index;
	char field[256];
	if (!write_private_profile_uint64(section, section_comment, "memory", CMOS->memory, i)) return 0;
	if (!write_private_profile_int64(section,section_comment,"TimeDivergeance_seconds",CMOS->timedivergeance,i)) return 0;
	if (!write_private_profile_int64(section,section_comment,"TimeDivergeance_microseconds",CMOS->timedivergeance2,i)) return 0;
	if (!write_private_profile_uint64(section,section_comment,"s100",CMOS->s100,i)) return 0;
	if (!write_private_profile_uint64(section,section_comment,"s10000",CMOS->s10000,i)) return 0;
	if (!write_private_profile_uint64(section,section_comment,"centuryisbinary",CMOS->centuryisbinary,i)) return 0;
	if (!write_private_profile_uint64(section,section_comment,"cycletiming",CMOS->cycletiming,i)) return 0;
	if (!write_private_profile_uint64(section, section_comment, "floppy0_nodisk_type", CMOS->floppy0_nodisk_type, i)) return 0;
	if (!write_private_profile_uint64(section, section_comment, "floppy1_nodisk_type", CMOS->floppy1_nodisk_type, i)) return 0;
	if (!write_private_profile_uint64(section, section_comment, "cpu", CMOS->emulated_CPU, i)) return 0;
	if (!write_private_profile_uint64(section, section_comment, "cpus", CMOS->emulated_CPUs, i)) return 0;
	if (!write_private_profile_uint64(section, section_comment, "databussize", CMOS->DataBusSize, i)) return 0; //The size of the emulated BUS. 0=Normal bus, 1=8-bit bus when available for the CPU!
	if (!write_private_profile_uint64(section, section_comment, "cpuspeed", CMOS->CPUspeed, i)) return 0;
	if (!write_private_profile_uint64(section, section_comment, "turbocpuspeed", CMOS->TurboCPUspeed, i)) return 0;
	if (!write_private_profile_uint64(section, section_comment, "useturbocpuspeed", CMOS->useTurboCPUSpeed, i)) return 0; //Are we to use Turbo CPU speed?
	if (!write_private_profile_uint64(section, section_comment, "clockingmode", CMOS->clockingmode, i)) return 0; //Are we using the IPS clock?
	if (!write_private_profile_uint64(section, section_comment, "CPUIDmode", CMOS->CPUIDmode, i)) return 0; //Are we using the CPUID mode?
	for (index=0;index<NUMITEMS(CMOS->DATA80.data);++index) //Process extra RAM data!
	{
		snprintf(field,sizeof(field),"RAM%02X",index); //The field!
		if (!write_private_profile_uint64(section,section_comment,&field[0],CMOS->DATA80.data[index],i)) return 0;
	}
	for (index=0;index<NUMITEMS(CMOS->extraRAMdata);++index) //Process extra RAM data!
	{
		snprintf(field,sizeof(field),"extraRAM%02X",index); //The field!
		if (!write_private_profile_uint64(section,section_comment,&field[0],CMOS->extraRAMdata[index],i)) return 0;
	}
	return 1; //Successfully written!
}

extern char BOOT_ORDER_STRING[15][30]; //Boot order, string values!

extern char colors[0x10][15]; //All 16 colors!

//Comments to build:
char general_comment[4096] = "version: version number, DO NOT CHANGE\nfirstrun: 1 for opening the settings menu automatically, 0 otherwise\nsettingsmenufont: the font to use for the Settings menu: 0=Default, 1=Phoenix Laptop, 2=Phoenix - Award Workstation\nbackgroundpolicy: 0=Full halt, 1=Run without audio playing and recording, 2=Run without recording, 3=Run without rendering the display"; //General comment!
char machine_comment[4096] = ""; //Machine comment!
char debugger_comment[4096] = ""; //Debugger comment!
char video_comment[4096] = ""; //Video comment!
char sound_comment[4096] = ""; //Sound comment!
char modem_comment[4096] = ""; //Sound comment!
char disks_comment[4096] = ""; //Disks comment!
char bios_comment[4096] = ""; //BIOS comment!
char currentstr[4096] = ""; //Current boot order to dump!
char input_comment[4096] = ""; //Input comment!
char gamingmode_comment[4096] = ""; //Gamingmode comment!
char bioscomment_currentkey[4096] = "";
char buttons[15][256] = {"start","left","up","right","down","ltrigger","rtrigger","triangle","circle","cross","square","analogleft","analogup","analogright","analogdown"}; //The names of all mappable buttons!
byte modefields[16][256] = { "","_triangle","_square","_cross","_circle" };
char cmos_comment[4096] = ""; //PrimaryCMOS comment!

extern uint8_t maclocal_default[6]; //Default MAC of the sender!

#define ABORT_SAVEDATA {closeinifile(&inifile); return 0;}

int BIOS_SaveData() //Save BIOS settings!
{
	word c;
	if (__HW_DISABLED) return 1; //Abort!
	if ((memcmp(&loadedsettings, &BIOS_Settings, sizeof(BIOS_Settings)) == 0) && loadedsettings_loaded) //Unchanged from buffer?
	{
		return 1; //Nothing to do! Report success!
	}

	memset(&phonebookentry, 0, sizeof(phonebookentry)); //Init!

	INI_FILE* inifile;
	inifile = newinifile(BIOS_Settings_file); //Creating a new ini file!
	if (!inifile) //Failed to create?
	{
		return 0; //Error out!
	}

	//General
	char *general_commentused = NULL;
	if (general_comment[0]) general_commentused = &general_comment[0];
	if (!write_private_profile_uint64("general", general_commentused, "version", BIOS_VERSION, inifile)) ABORT_SAVEDATA
	if (!write_private_profile_uint64("general", general_commentused, "firstrun", BIOS_Settings.firstrun, inifile)) ABORT_SAVEDATA //Is this the first run of this BIOS?
	if (!write_private_profile_uint64("general", general_commentused, "settingsmenufont", BIOS_Settings.BIOSmenu_font, inifile)) ABORT_SAVEDATA //The selected font for the BIOS menu!
	if (!write_private_profile_uint64("general", general_commentused, "backgroundpolicy", BIOS_Settings.backgroundpolicy, inifile)) ABORT_SAVEDATA //The selected font for the BIOS menu!

	//Machine
	memset(&machine_comment, 0, sizeof(machine_comment)); //Init!
	safestrcat(machine_comment, sizeof(machine_comment), "architecture: 0=XT, 1=AT, 2=Compaq Deskpro 386, 3=Compaq Deskpro 386 with PS/2 mouse, 4=i430fx, 5=i440fx\n");
	safestrcat(machine_comment, sizeof(machine_comment), "executionmode: 0=Use emulator internal BIOS, 1=Run debug directory files, else TESTROM.DAT at 0000:0000, 2=Run TESTROM.DAT at 0000:0000, 3=Debug video card output, 4=Load BIOS from ROM directory as BIOSROM.u* and OPTROM.*, 5=Run sound test\n");
	safestrcat(machine_comment, sizeof(machine_comment), "showcpuspeed: 0=Don't show, 1=Show\n");
	safestrcat(machine_comment, sizeof(machine_comment), "BIOSROMmode: 0=Normal BIOS ROM, 1=Diagnostic ROM, 2=Enforce normal U-ROMs\n");
	safestrcat(machine_comment, sizeof(machine_comment), "inboardinitialwaitstates: 0=Default waitstates, 1=No waitstates");
	char *machine_commentused = NULL;
	if (machine_comment[0]) machine_commentused = &machine_comment[0];
	if (!write_private_profile_uint64("machine", machine_commentused, "architecture", BIOS_Settings.architecture, inifile)) ABORT_SAVEDATA //Are we using the XT/AT/PS/2 architecture?
	if (!write_private_profile_uint64("machine", machine_commentused, "executionmode", BIOS_Settings.executionmode, inifile)) ABORT_SAVEDATA //What mode to execute in during runtime?
	if (!write_private_profile_uint64("machine", machine_commentused, "showcpuspeed", BIOS_Settings.ShowCPUSpeed, inifile)) ABORT_SAVEDATA //Show the relative CPU speed together with the framerate?
	if (!write_private_profile_uint64("machine", machine_commentused, "BIOSROMmode", BIOS_Settings.BIOSROMmode, inifile)) ABORT_SAVEDATA //BIOS ROM mode.
	if (!write_private_profile_uint64("machine", machine_commentused, "inboardinitialwaitstates", BIOS_Settings.InboardInitialWaitstates, inifile)) ABORT_SAVEDATA //Inboard 386 initial delay used?

	//Debugger
	memset(&debugger_comment, 0, sizeof(debugger_comment)); //Init!
	safestrcat(debugger_comment, sizeof(debugger_comment), "debugmode: 0=Disabled, 1=Enabled, RTrigger=Step, 2=Enabled, Step through, 3=Enabled, just run, ignore shoulder buttons, 4=Enabled, just run, don't show, ignore shoulder buttons\n");
	safestrcat(debugger_comment, sizeof(debugger_comment), "debuggerlog: 0=Don't log, 1=Only when debugging, 2=Always log, 3=Interrupt calls only, 4=BIOS Diagnostic codes only, 5=Always log, no register state, 6=Always log, even during skipping, 7=Always log, even during skipping, single line format, 8=Only when debugging, single line format, 9=Always log, even during skipping, single line format, simplified, 10=Only when debugging, single line format, simplified, 11=Always log, common log format, 12=Always log, even during skipping, common log format, 13=Only when debugging, common log format\n");
	safestrcat(debugger_comment, sizeof(debugger_comment), "logstates: 0=Disabled, 1=Enabled\n");
	safestrcat(debugger_comment, sizeof(debugger_comment), "logregisters: 0=Disabled, 1=Enabled\n");
	safestrcat(debugger_comment, sizeof(debugger_comment), "breakpoint: bits 60-61: 0=Not set, 1=Real mode, 2=Protected mode, 3=Virtual 8086 mode; bit 59: Break on CS only; bit 58: Break on mode only. bit 57: Break on EIP only. bit 56: enforce single step. bits 32-47: segment, bits 31-0: offset(truncated to 16-bits in Real/Virtual 8086 mode\n");
	safestrcat(debugger_comment, sizeof(debugger_comment), "taskbreakpoint: bits 60-61: 0=Not set, 1=Enabled; bit 59: Break on TR only; bit 57: Break on base address only. bits 32-47: TR segment, bits 31-0: base address within the descriptor cache\n");
	safestrcat(debugger_comment, sizeof(debugger_comment), "FSbreakpoint: bits 60-61: 0=Not set, 1=Enabled; bit 59: Break on FS only; bit 57: Break on base address only. bits 32-47: FS segment, bits 31-0: base address within the descriptor cache\n");
	safestrcat(debugger_comment, sizeof(debugger_comment), "CR3breakpoint: bits 60-61: 0=Not set, 1=Enabled; bits 31-0: Base address\n");
	safestrcat(debugger_comment, sizeof(debugger_comment), "diagnosticsport_breakpoint: -1=Disabled, 0-255=Value to trigger the breakpoint\n");
	safestrcat(debugger_comment, sizeof(debugger_comment), "diagnosticsport_timeout: 0=At first instruction, 1+: At the n+1th instruction\n");
	safestrcat(debugger_comment, sizeof(debugger_comment), "advancedlog: 0=Disable advanced logging, 1: Use advanced logging");
	char *debugger_commentused = NULL;
	if (debugger_comment[0]) debugger_commentused = &debugger_comment[0];
	if (!write_private_profile_uint64("debugger", debugger_commentused, "debugmode", BIOS_Settings.debugmode, inifile)) ABORT_SAVEDATA
	if (!write_private_profile_uint64("debugger", debugger_commentused, "debuggerlog", BIOS_Settings.debugger_log, inifile)) ABORT_SAVEDATA
	if (!write_private_profile_uint64("debugger", debugger_commentused, "logstates", BIOS_Settings.debugger_logstates, inifile)) ABORT_SAVEDATA //Are we logging states? 1=Log states, 0=Don't log states!
	if (!write_private_profile_uint64("debugger", debugger_commentused, "logregisters", BIOS_Settings.debugger_logregisters, inifile)) ABORT_SAVEDATA //Are we logging states? 1=Log states, 0=Don't log states!
	if (!write_private_profile_uint64("debugger", debugger_commentused, "breakpoint", BIOS_Settings.breakpoint[0], inifile)) ABORT_SAVEDATA //The used breakpoint segment:offset and mode!
	if (!write_private_profile_uint64("debugger", debugger_commentused, "breakpoint2", BIOS_Settings.breakpoint[1], inifile)) ABORT_SAVEDATA //The used breakpoint segment:offset and mode!
	if (!write_private_profile_uint64("debugger", debugger_commentused, "breakpoint3", BIOS_Settings.breakpoint[2], inifile)) ABORT_SAVEDATA //The used breakpoint segment:offset and mode!
	if (!write_private_profile_uint64("debugger", debugger_commentused, "breakpoint4", BIOS_Settings.breakpoint[3], inifile)) ABORT_SAVEDATA //The used breakpoint segment:offset and mode!
	if (!write_private_profile_uint64("debugger", debugger_commentused, "breakpoint5", BIOS_Settings.breakpoint[4], inifile)) ABORT_SAVEDATA //The used breakpoint segment:offset and mode!
	if (!write_private_profile_uint64("debugger", debugger_commentused, "taskbreakpoint", BIOS_Settings.taskBreakpoint, inifile)) ABORT_SAVEDATA //The used breakpoint segment:offset and enable!
	if (!write_private_profile_uint64("debugger", debugger_commentused, "FSbreakpoint", BIOS_Settings.FSBreakpoint, inifile)) ABORT_SAVEDATA //The used breakpoint segment:offset and enable!
	if (!write_private_profile_uint64("debugger", debugger_commentused, "CR3breakpoint", BIOS_Settings.CR3breakpoint, inifile)) ABORT_SAVEDATA //The used breakpoint offset ans enable!
	if (!write_private_profile_int64("debugger", debugger_commentused, "diagnosticsport_breakpoint", BIOS_Settings.diagnosticsportoutput_breakpoint, inifile)) ABORT_SAVEDATA //Use a diagnostics port breakpoint?
	if (!write_private_profile_uint64("debugger", debugger_commentused, "diagnosticsport_timeout", BIOS_Settings.diagnosticsportoutput_timeout, inifile)) ABORT_SAVEDATA //Breakpoint timeout used!
	if (!write_private_profile_uint64("debugger", debugger_commentused, "advancedlog", BIOS_Settings.advancedlog, inifile)) ABORT_SAVEDATA //Advanced logging feature!

	//Video
	memset(&video_comment, 0, sizeof(video_comment)); //Init!
	safestrcat(video_comment, sizeof(video_comment), "videocard: 0=Pure VGA, 1=VGA with NMI, 2=VGA with CGA, 3=VGA with MDA, 4=Pure CGA, 5=Pure MDA, 6=Tseng ET4000, 7=Tseng ET3000, 8=Pure EGA\n");
	safestrcat(video_comment, sizeof(video_comment), "CGAmodel: 0=Old-style RGB, 1=Old-style NTSC, 2=New-style RGB, 3=New-style NTSC\n");
	safestrcat(video_comment, sizeof(video_comment), "VRAM: Ammount of VRAM installed, in bytes\n");
	safestrcat(video_comment, sizeof(video_comment), "synchronization: 0=Old synchronization depending on host, 1=Synchronize depending on host, 2=Full CPU synchronization\n");
	safestrcat(video_comment, sizeof(video_comment), "directplot: 0=Disabled, 1=Automatic, 2=Forced\n");
	safestrcat(video_comment, sizeof(video_comment), "aspectratio: 0=Fullscreen stretching, 1=Keep the same, 2=Force 4:3(VGA), 3=Force CGA, 4=Force 4:3(SVGA 768p), 5=Force 4:3(SVGA 1080p), 6=Force 4K, 7=Force 4:3(SVGA 4K)\n");
	safestrcat(video_comment, sizeof(video_comment), "bwmonitor: 0=Color, 1=B/W monitor: white, 2=B/W monitor: green, 3=B/W monitor: amber\n");
	safestrcat(video_comment, sizeof(video_comment), "bwmonitor_luminancemode: 0=Averaged, 1=Luminance\n");
	safestrcat(video_comment, sizeof(video_comment), "showframerate: 0=Disabled, otherwise Enabled\n");
	safestrcat(video_comment, sizeof(video_comment), "SVGA_DACmode: 0=Sierra SC11487, 1=UMC UM70C178, 2=AT&T 20C490, 3=Sierra SC15025\n");
	safestrcat(video_comment, sizeof(video_comment), "blackpedestal: 0=Black, 1=7.5 IRE\n");
	safestrcat(video_comment, sizeof(video_comment), "ET4000_extensions: 0=ET4000AX, 1=ET4000/W32");
	char *video_commentused = NULL;
	if (video_comment[0]) video_commentused = &video_comment[0];
	if (!write_private_profile_uint64("video", video_commentused, "videocard", BIOS_Settings.VGA_Mode, inifile)) ABORT_SAVEDATA //Enable VGA NMI on precursors?
	if (!write_private_profile_uint64("video", video_commentused, "CGAmodel", BIOS_Settings.CGAModel, inifile)) ABORT_SAVEDATA //What kind of CGA is emulated? Bit0=NTSC, Bit1=New-style CGA
	if (!write_private_profile_uint64("video", video_commentused, "VRAM", BIOS_Settings.VRAM_size, inifile)) ABORT_SAVEDATA //(S)VGA VRAM size!
	if (!write_private_profile_uint64("video", video_commentused, "synchronization", BIOS_Settings.VGASynchronization, inifile)) ABORT_SAVEDATA //VGA synchronization setting. 0=Automatic synchronization based on Host CPU. 1=Tight VGA Synchronization with the CPU.
	if (!write_private_profile_uint64("video", video_commentused, "directplot", BIOS_Settings.GPU_AllowDirectPlot, inifile)) ABORT_SAVEDATA //Allow VGA Direct Plot: 1 for automatic 1:1 mapping, 0 for always dynamic, 2 for force 1:1 mapping?
	if (!write_private_profile_uint64("video", video_commentused, "aspectratio", BIOS_Settings.aspectratio, inifile)) ABORT_SAVEDATA //The aspect ratio to use?
	if (!write_private_profile_uint64("video", video_commentused, "bwmonitor", BIOS_Settings.bwmonitor, inifile)) ABORT_SAVEDATA //Are we a b/w monitor?
	if (!write_private_profile_uint64("video", video_commentused, "bwmonitor_luminancemode", BIOS_Settings.bwmonitor_luminancemode, inifile)) ABORT_SAVEDATA //Are we a b/w monitor?
	if (!write_private_profile_uint64("video", video_commentused, "showframerate", BIOS_Settings.ShowFramerate, inifile)) ABORT_SAVEDATA //Show the frame rate?
	if (!write_private_profile_uint64("video", video_commentused, "SVGA_DACmode", BIOS_Settings.SVGA_DACmode, inifile)) ABORT_SAVEDATA //Show the frame rate?
	if (!write_private_profile_uint64("video", video_commentused, "blackpedestal", BIOS_Settings.video_blackpedestal, inifile)) ABORT_SAVEDATA //Show the frame rate?
	if (!write_private_profile_uint64("video", video_commentused, "ET4000_extensions", BIOS_Settings.ET4000_extensions, inifile)) ABORT_SAVEDATA //Show the frame rate?

	//Sound
	memset(&sound_comment, 0, sizeof(sound_comment)); //Init!
	safestrcat(sound_comment, sizeof(sound_comment), "speaker: 0=Disabled, 1=Enabled\n");
	safestrcat(sound_comment, sizeof(sound_comment), "adlib: 0=Disabled, 1=Enabled\n");
	safestrcat(sound_comment, sizeof(sound_comment), "LPTDAC: 0=Disabled, 1=Enabled\n");
	safestrcat(sound_comment, sizeof(sound_comment), "soundfont: The path to the soundfont file. Empty for none.\n");
	safestrcat(sound_comment, sizeof(sound_comment), "directmidi: 0=Disabled, 1=Enabled\n");
	safestrcat(sound_comment, sizeof(sound_comment), "gameblaster: 0=Disabled, 1=Enabled\n");
	safestrcat(sound_comment, sizeof(sound_comment), "gameblaster_volume: Volume of the game blaster, in percent(>=0)\n");
	safestrcat(sound_comment, sizeof(sound_comment), "soundblaster: 0=Disabled, 1=Version 1.0(with Game Blaster) or 1.5(without Game Blaster), 2=Version 2.0\n");
	safestrcat(sound_comment, sizeof(sound_comment), "soundsource_volume: Volume of the sound source, in percent(>=0)");
	char *sound_commentused = NULL;
	if (sound_comment[0]) sound_commentused = &sound_comment[0];
	if (!write_private_profile_uint64("sound", sound_commentused, "speaker", BIOS_Settings.usePCSpeaker, inifile)) ABORT_SAVEDATA //Emulate PC Speaker sound?
	if (!write_private_profile_uint64("sound", sound_commentused, "adlib", BIOS_Settings.useAdlib, inifile)) ABORT_SAVEDATA //Emulate Adlib?
	if (!write_private_profile_uint64("sound", sound_commentused, "LPTDAC", BIOS_Settings.useLPTDAC, inifile)) ABORT_SAVEDATA //Emulate Covox/Disney Sound Source?
	if (!write_private_profile_string("sound", sound_commentused, "soundfont", &BIOS_Settings.SoundFont[0], inifile)) ABORT_SAVEDATA //Read entry!
	if (!write_private_profile_uint64("sound", sound_commentused, "directmidi", BIOS_Settings.useDirectMIDI, inifile)) ABORT_SAVEDATA //Use Direct MIDI synthesis by using a passthrough to the OS?
	if (!write_private_profile_uint64("sound", sound_commentused, "gameblaster", BIOS_Settings.useGameBlaster, inifile)) ABORT_SAVEDATA //Emulate Game Blaster?
	if (!write_private_profile_uint64("sound", sound_commentused, "gameblaster_volume", BIOS_Settings.GameBlaster_Volume, inifile)) ABORT_SAVEDATA //The Game Blaster volume knob!
	if (!write_private_profile_uint64("sound", sound_commentused, "soundblaster", BIOS_Settings.useSoundBlaster, inifile)) ABORT_SAVEDATA //Emulate Sound Blaster?
	if (!write_private_profile_uint64("sound", sound_commentused, "soundsource_volume", BIOS_Settings.SoundSource_Volume, inifile)) ABORT_SAVEDATA //The sound source volume knob!

	//Modem
	memset(&modem_comment, 0, sizeof(modem_comment)); //Init!
	memset(currentstr, 0, sizeof(currentstr)); //Init!
	snprintf(modem_comment, sizeof(modem_comment), "listenport: listen port to listen on when not connected(defaults to %u)\n", DEFAULT_MODEMLISTENPORT);
	snprintf(currentstr, sizeof(currentstr), "nullmodem: make the modem behave as a nullmodem cable(defaults to %u). 0=Normal modem, 1=simple nullmodem cable, 2=nullmodem cable with line signalling, 3=nullmodem cable with line signalling and outgoing manual connect using phonebook entry #0\n", DEFAULT_NULLMODEM);
	safestrcat(modem_comment, sizeof(modem_comment), currentstr); //MAC address information!
	snprintf(currentstr, sizeof(currentstr), "phonebook0-%u: Phonebook entry #n", (byte)(NUMITEMS(BIOS_Settings.phonebook) - 1)); //Information about the phonebook!
	safestrcat(modem_comment, sizeof(modem_comment), currentstr); //MAC address information!
	safestrcat(modem_comment, sizeof(modem_comment), "\nethernetcard: -1 for disabled(use normal emulation), -2 for local loopback, 1+ selected and use a network card, 0 to generate a list of network cards to select\n");
	snprintf(currentstr, sizeof(currentstr), "hostMACaddress: MAC address to emulate as a virtual NIC and send/receive packets on(defaults to %02x:%02x:%02x:%02x:%02x:%02x)\n", maclocal_default[0], maclocal_default[1], maclocal_default[2], maclocal_default[3], maclocal_default[4], maclocal_default[5]);
	safestrcat(modem_comment, sizeof(modem_comment), "hostIPaddress: host IP address for the PPP client to use by default and the host IP address\n");
	safestrcat(modem_comment, sizeof(modem_comment), currentstr); //MAC address information!
	safestrcat(modem_comment, sizeof(modem_comment), "hostsubnetmaskIPaddress: subnet mask IP address for the host to use\n");
	safestrcat(modem_comment, sizeof(modem_comment), "gatewayMACaddress: gateway MAC address to send/receive packets on\n");
	safestrcat(modem_comment, sizeof(modem_comment), "gatewayIPaddress: default gateway IP address for the PPP client to use\n");
	safestrcat(modem_comment, sizeof(modem_comment), "DNS1IPaddress: DNS #1 IP address for the PPP client to use\n");
	safestrcat(modem_comment, sizeof(modem_comment), "DNS2IPaddress: DNS #2 IP address for the PPP client to use\n");
	safestrcat(modem_comment, sizeof(modem_comment), "NBNS1IPaddress: NBNS #1 IP address for the PPP client to use\n");
	safestrcat(modem_comment, sizeof(modem_comment), "NBNS2IPaddress: NBNS #2 IP address for the PPP client to use\n");
	safestrcat(modem_comment, sizeof(modem_comment), "subnetmaskIPaddress: subnet mask IP address for the PPP client to use\n");
	safestrcat(modem_comment, sizeof(modem_comment), "username: set username and password to non-empty values for a credential protected server\n");
	safestrcat(modem_comment, sizeof(modem_comment), "password: set username and password to non-empty values for a credential protected server\n");
	safestrcat(modem_comment, sizeof(modem_comment), "IPaddress: static IP address to use for this NIC (account). Format 0123456789AB for IP 012.345.678.9AB\n");
	safestrcat(modem_comment, sizeof(modem_comment), "Specify username/password/IPaddress for the default account(required when using authentication).");
	if (NUMITEMS(BIOS_Settings.ethernetserver_settings.users) > 1) //More than one available?
	{
		snprintf(currentstr, sizeof(currentstr), "\nAdd 1 - %i to the username/password/IPaddress key for multiple accounts(when username and password are non - empty, it's used).", (int)NUMITEMS(BIOS_Settings.ethernetserver_settings.users) - 1);
		safestrcat(modem_comment, sizeof(modem_comment), currentstr); //Extra user information!
		safestrcat(modem_comment, sizeof(modem_comment), "Specifying no or an invalid IP address for the numbered IPaddress fields other than the default will use the default field instead.\n");
	}
	char *modem_commentused=NULL;
	if (modem_comment[0]) modem_commentused = &modem_comment[0];
	if (!write_private_profile_uint64("modem",modem_commentused,"listenport",BIOS_Settings.modemlistenport,inifile)) ABORT_SAVEDATA //Modem listen port!
	if (!write_private_profile_uint64("modem",modem_commentused,"nullmodem",BIOS_Settings.nullmodem,inifile)) ABORT_SAVEDATA //nullmodem mode!
	for (c = 0; c < NUMITEMS(BIOS_Settings.phonebook); ++c) //Process all phonebook entries!
	{
		snprintf(phonebookentry, sizeof(phonebookentry), "phonebook%u", c); //The entry to use!
		if (!write_private_profile_string("modem", modem_commentused, phonebookentry, &BIOS_Settings.phonebook[c][0], inifile)) ABORT_SAVEDATA //Entry!
	}
	if (!write_private_profile_int64("modem", modem_commentused, "ethernetcard", BIOS_Settings.ethernetserver_settings.ethernetcard, inifile)) ABORT_SAVEDATA //Ethernet card to use!
	if (!write_private_profile_string("modem", modem_commentused, "hostMACaddress", &BIOS_Settings.ethernetserver_settings.MACaddress[0], inifile)) ABORT_SAVEDATA //MAC address to use!
	if (!write_private_profile_string("modem", modem_commentused, "hostIPaddress", &BIOS_Settings.ethernetserver_settings.hostIPaddress[0], inifile)) ABORT_SAVEDATA //MAC address to use!
	if (!write_private_profile_string("modem", modem_commentused, "hostsubnetmaskIPaddress", &BIOS_Settings.ethernetserver_settings.hostsubnetmaskIPaddress[0], inifile)) ABORT_SAVEDATA //MAC address to use!
	if (!write_private_profile_string("modem", modem_commentused, "gatewayMACaddress", &BIOS_Settings.ethernetserver_settings.gatewayMACaddress[0], inifile)) ABORT_SAVEDATA //MAC address to use!
	if (!write_private_profile_string("modem", modem_commentused, "gatewayIPaddress", &BIOS_Settings.ethernetserver_settings.gatewayIPaddress[0], inifile)) ABORT_SAVEDATA //MAC address to use!
	if (!write_private_profile_string("modem", modem_commentused, "DNS1IPaddress", &BIOS_Settings.ethernetserver_settings.DNS1IPaddress[0], inifile)) ABORT_SAVEDATA //MAC address to use!
	if (!write_private_profile_string("modem", modem_commentused, "DNS2IPaddress", &BIOS_Settings.ethernetserver_settings.DNS2IPaddress[0], inifile)) ABORT_SAVEDATA //MAC address to use!
	if (!write_private_profile_string("modem", modem_commentused, "NBNS1IPaddress", &BIOS_Settings.ethernetserver_settings.NBNS1IPaddress[0], inifile)) ABORT_SAVEDATA //MAC address to use!
	if (!write_private_profile_string("modem", modem_commentused, "NBNS2IPaddress", &BIOS_Settings.ethernetserver_settings.NBNS2IPaddress[0], inifile)) ABORT_SAVEDATA //MAC address to use!
	if (!write_private_profile_string("modem", modem_commentused, "subnetmaskIPaddress", &BIOS_Settings.ethernetserver_settings.subnetmaskIPaddress[0], inifile)) ABORT_SAVEDATA //MAC address to use!

	for (c = 0; c < NUMITEMS(BIOS_Settings.ethernetserver_settings.users); ++c) //Process all phonebook entries!
	{
		if (c) //Normal user?
		{
			snprintf(phonebookentry, sizeof(phonebookentry), "username%u", c); //The entry to use!
		}
		else
		{
			safestrcpy(phonebookentry, sizeof(phonebookentry), "username"); //The entry to use!
		}
		if (!write_private_profile_string("modem", modem_commentused, phonebookentry, &BIOS_Settings.ethernetserver_settings.users[c].username[0], inifile)) ABORT_SAVEDATA //MAC address to use!
		if (c) //Normal user?
		{
			snprintf(phonebookentry, sizeof(phonebookentry), "password%u", c); //The entry to use!
		}
		else
		{
			safestrcpy(phonebookentry, sizeof(phonebookentry), "password"); //The entry to use!
		}
		if (!write_private_profile_string("modem", modem_commentused, phonebookentry, &BIOS_Settings.ethernetserver_settings.users[c].password[0], inifile)) ABORT_SAVEDATA //MAC address to use!
		if (c) //Normal user?
		{
			snprintf(phonebookentry, sizeof(phonebookentry), "IPaddress%u", c); //The entry to use!
		}
		else
		{
			safestrcpy(phonebookentry, sizeof(phonebookentry), "IPaddress"); //The entry to use!
		}
		if (!write_private_profile_string("modem", modem_commentused, phonebookentry, &BIOS_Settings.ethernetserver_settings.users[c].IPaddress[0], inifile)) ABORT_SAVEDATA //MAC address to use!
	}

	//Disks
	memset(&disks_comment,0,sizeof(disks_comment)); //Init!
	safestrcat(disks_comment,sizeof(disks_comment),"floppy[number]/hdd[number]/cdrom[number]: The disk to be mounted. Empty for none.\n");
	safestrcat(disks_comment,sizeof(disks_comment),"floppy[number]_readonly/hdd[number]_readonly: 0=Writable, 1=Read-only");
	char *disks_commentused=NULL;
	if (disks_comment[0]) disks_commentused = &disks_comment[0];
	if (!write_private_profile_string("disks",disks_commentused,"floppy0",&BIOS_Settings.floppy0[0],inifile)) ABORT_SAVEDATA //Read entry!
	if (!write_private_profile_uint64("disks",disks_commentused,"floppy0_readonly",BIOS_Settings.floppy0_readonly,inifile)) ABORT_SAVEDATA
	if (!write_private_profile_string("disks",disks_commentused,"floppy1",&BIOS_Settings.floppy1[0],inifile)) ABORT_SAVEDATA //Read entry!
	if (!write_private_profile_uint64("disks",disks_commentused,"floppy1_readonly",BIOS_Settings.floppy1_readonly,inifile)) ABORT_SAVEDATA
	if (!write_private_profile_string("disks",disks_commentused,"hdd0",&BIOS_Settings.hdd0[0],inifile)) ABORT_SAVEDATA //Read entry!
	if (!write_private_profile_uint64("disks",disks_commentused,"hdd0_readonly",BIOS_Settings.hdd0_readonly,inifile)) ABORT_SAVEDATA
	if (!write_private_profile_string("disks",disks_commentused,"hdd1",&BIOS_Settings.hdd1[0],inifile)) ABORT_SAVEDATA //Read entry!
	if (!write_private_profile_uint64("disks",disks_commentused,"hdd1_readonly",BIOS_Settings.hdd1_readonly,inifile)) ABORT_SAVEDATA
	if (!write_private_profile_string("disks",disks_commentused,"cdrom0",&BIOS_Settings.cdrom0[0],inifile)) ABORT_SAVEDATA //Read entry!
	if (!write_private_profile_string("disks",disks_commentused,"cdrom1",&BIOS_Settings.cdrom1[0],inifile)) ABORT_SAVEDATA //Read entry!

	//BIOS
	memset(&bios_comment,0,sizeof(bios_comment)); //Init!
	safestrcat(bios_comment,sizeof(bios_comment),"bootorder: The boot order of the internal BIOS:");
	byte currentitem;
	for (currentitem=0;currentitem<NUMITEMS(BOOT_ORDER_STRING);++currentitem)
	{
		snprintf(currentstr,sizeof(currentstr),"\n%u=%s",currentitem,BOOT_ORDER_STRING[currentitem]); //A description of all boot orders!
		safestrcat(bios_comment,sizeof(bios_comment),currentstr); //Add the string!
	}
	char *bios_commentused=NULL;
	if (bios_comment[0]) bios_commentused = &bios_comment[0];
	if (!write_private_profile_uint64("bios",bios_commentused,"bootorder",BIOS_Settings.bootorder,inifile)) ABORT_SAVEDATA

	//Input
	memset(&currentstr,0,sizeof(currentstr)); //Current boot order to dump!
	memset(&gamingmode_comment,0,sizeof(gamingmode_comment)); //Gamingmode comment!
	memset(&input_comment,0,sizeof(input_comment)); //Init!
	safestrcat(input_comment,sizeof(input_comment),"analog_minrange: Minimum range for the analog stick to repond. 0-255\n");
	safestrcat(input_comment,sizeof(input_comment),"Color codes are as follows:");
	for (currentitem=0;currentitem<0x10;++currentitem)
	{
		snprintf(currentstr,sizeof(currentstr),"\n%u=%s",currentitem,colors[currentitem]); //A description of all colors!
		safestrcat(input_comment,sizeof(input_comment),currentstr); //Add the string!
	}
	safestrcat(input_comment,sizeof(input_comment),"\n\n"); //Empty line!
	safestrcat(input_comment,sizeof(input_comment),"keyboard_fontcolor: font color for the (PSP) OSK.\n");
	safestrcat(input_comment,sizeof(input_comment),"keyboard_bordercolor: border color for the (PSP) OSK.\n");
	safestrcat(input_comment,sizeof(input_comment),"keyboard_activecolor: active color for the (PSP) OSK. Also color of pressed keys on the touch OSK.\n");
	safestrcat(input_comment,sizeof(input_comment),"keyboard_specialcolor: font color for the LEDs.\n");
	safestrcat(input_comment,sizeof(input_comment),"keyboard_specialbordercolor: border color for the LEDs.\n");
	safestrcat(input_comment,sizeof(input_comment),"keyboard_specialactivecolor: active color for the LEDs.\n");
	safestrcat(input_comment, sizeof(input_comment), "DirectInput_remap_RCTRL_to_LWIN: Remap RCTRL to LWIN during Direct Input.\n");
	safestrcat(input_comment, sizeof(input_comment), "DirectInput_remap_accentgrave_to_tab: Remap Accent Grave to Tab during LALT.\n");
	safestrcat(input_comment, sizeof(input_comment), "DirectInput_remap_NUM0_to_Delete: Remap NUM0 to Delete during Direct Input.\n");
	safestrcat(input_comment, sizeof(input_comment), "DirectInput_disable_RALT: Disable RALT being pressed during Direct Input mode.");
	char *input_commentused=NULL;
	if (input_comment[0]) input_commentused = &input_comment[0];
	if (!write_private_profile_uint64("input",input_commentused,"analog_minrange",BIOS_Settings.input_settings.analog_minrange,inifile)) ABORT_SAVEDATA //Minimum adjustment x&y(0,0) for keyboard&mouse to change states (from center)
	if (!write_private_profile_uint64("input",input_commentused,"keyboard_fontcolor",BIOS_Settings.input_settings.fontcolor,inifile)) ABORT_SAVEDATA
	if (!write_private_profile_uint64("input",input_commentused,"keyboard_bordercolor",BIOS_Settings.input_settings.bordercolor,inifile)) ABORT_SAVEDATA
	if (!write_private_profile_uint64("input",input_commentused,"keyboard_activecolor",BIOS_Settings.input_settings.activecolor,inifile)) ABORT_SAVEDATA
	if (!write_private_profile_uint64("input",input_commentused,"keyboard_specialcolor",BIOS_Settings.input_settings.specialcolor,inifile)) ABORT_SAVEDATA
	if (!write_private_profile_uint64("input",input_commentused,"keyboard_specialbordercolor",BIOS_Settings.input_settings.specialbordercolor,inifile)) ABORT_SAVEDATA
	if (!write_private_profile_uint64("input",input_commentused,"keyboard_specialactivecolor",BIOS_Settings.input_settings.specialactivecolor,inifile)) ABORT_SAVEDATA
	if (!write_private_profile_uint64("input",input_commentused,"DirectInput_remap_RCTRL_to_LWIN",BIOS_Settings.input_settings.DirectInput_remap_RCTRL_to_LWIN,inifile)) ABORT_SAVEDATA
	if (!write_private_profile_uint64("input",input_commentused,"DirectInput_remap_accentgrave_to_tab",BIOS_Settings.input_settings.DirectInput_remap_accentgrave_to_tab,inifile)) ABORT_SAVEDATA
	if (!write_private_profile_uint64("input", input_commentused, "DirectInput_remap_NUM0_to_Delete", BIOS_Settings.input_settings.DirectInput_remap_NUM0_to_Delete, inifile)) ABORT_SAVEDATA
	if (!write_private_profile_uint64("input", input_commentused,"DirectInput_disable_RALT",BIOS_Settings.input_settings.DirectInput_Disable_RALT,inifile)) ABORT_SAVEDATA

	//Gamingmode
	memset(&bioscomment_currentkey,0,sizeof(bioscomment_currentkey)); //Init!
	for (currentitem=0;currentitem<104;++currentitem) //Give translations for all keys!
	{
		safestrcpy(currentstr,sizeof(currentstr),""); //Init current string!
		safestrcpy(bioscomment_currentkey,sizeof(bioscomment_currentkey),""); //Init current string!
		if (EMU_keyboard_handler_idtoname(currentitem,&bioscomment_currentkey[0])) //Name retrieved?
		{
			snprintf(currentstr,sizeof(currentstr),"Key number %u is %s\n",currentitem,bioscomment_currentkey); //Generate a key row!
			safestrcat(gamingmode_comment,sizeof(gamingmode_comment),currentstr); //Add the key to the list!
		}
	}
	safestrcat(gamingmode_comment,sizeof(gamingmode_comment),"gamingmode_map_[key]_key[facebutton]: The key to be mapped. -1 for unmapped. Otherwise, the key number(0-103)\n");
	snprintf(currentstr,sizeof(currentstr),"gamingmode_map_[key]_shiftstate[facebutton]: The summed state of ctrl/alt/shift keys to be pressed. %u=Ctrl, %u=Alt, %u=Shift. 0/empty=None.\n",SHIFTSTATUS_CTRL,SHIFTSTATUS_ALT,SHIFTSTATUS_SHIFT);
	safestrcat(gamingmode_comment,sizeof(gamingmode_comment),currentstr);
	safestrcat(gamingmode_comment,sizeof(gamingmode_comment),"gamingmode_map_[key]_mousebuttons[facebutton]: The summed state of mouse buttons to be pressed(0=None pressed, 1=Left, 2=Right, 4=Middle).\n");
	safestrcat(gamingmode_comment, sizeof(gamingmode_comment), "gamingmode_map_joystick[facebutton]: 0=Normal gaming mode mapped input, 1=Enable joystick input");
	safestrcat(gamingmode_comment,sizeof(gamingmode_comment),"joystick: 0=Normal gaming mode mapped input, 1=Joystick, Cross=Button 1, Circle=Button 2, 2=Joystick, Cross=Button 2, Circle=Button 1, 3=Joystick, Gravis Gamepad, 4=Joystick, Gravis Analog Pro, 5=Joystick, Logitech WingMan Extreme Digital");
	char *gamingmode_commentused=NULL;
	if (gamingmode_comment[0]) gamingmode_commentused = &gamingmode_comment[0];
	byte button,modefield;
	char buttonstr[256];
	memset(&buttonstr,0,sizeof(buttonstr)); //Init button string!
	for (modefield = 0; modefield < 5; ++modefield)
	{
		snprintf(buttonstr, sizeof(buttonstr), "gamingmode_map_joystick%s", modefields[modefield]);
		if (!write_private_profile_int64("gamingmode", gamingmode_commentused, buttonstr, BIOS_Settings.input_settings.usegamingmode_joystick[modefield], inifile)) ABORT_SAVEDATA
		for (button = 0; button < 15; ++button) //Process all buttons!
		{
			snprintf(buttonstr, sizeof(buttonstr), "gamingmode_map_%s_key%s", buttons[button],modefields[modefield]);
			if (!write_private_profile_int64("gamingmode", gamingmode_commentused, buttonstr, BIOS_Settings.input_settings.keyboard_gamemodemappings[modefield][button], inifile)) ABORT_SAVEDATA
			snprintf(buttonstr, sizeof(buttonstr), "gamingmode_map_%s_shiftstate%s", buttons[button],modefields[modefield]);
			if (!write_private_profile_uint64("gamingmode", gamingmode_commentused, buttonstr, BIOS_Settings.input_settings.keyboard_gamemodemappings_alt[modefield][button], inifile)) ABORT_SAVEDATA
			snprintf(buttonstr, sizeof(buttonstr), "gamingmode_map_%s_mousebuttons%s", buttons[button],modefields[modefield]);
			if (!write_private_profile_uint64("gamingmode", gamingmode_commentused, buttonstr, BIOS_Settings.input_settings.mouse_gamemodemappings[modefield][button], inifile)) ABORT_SAVEDATA
		}
	}

	if (!write_private_profile_uint64("gamingmode",gamingmode_commentused,"joystick",BIOS_Settings.input_settings.gamingmode_joystick,inifile)) ABORT_SAVEDATA //Use the joystick input instead of mapped input during gaming mode?

	//CMOS
	memset(&cmos_comment,0,sizeof(cmos_comment)); //Init!
	safestrcat(cmos_comment,sizeof(cmos_comment),"gotCMOS: 0=Don't load CMOS. 1=CMOS data is valid and to be loaded.\n");
	safestrcat(cmos_comment,sizeof(cmos_comment),"memory: memory size in bytes\n");
	safestrcat(cmos_comment,sizeof(cmos_comment),"TimeDivergeance_seconds: Time to be added to get the emulated time, in seconds.\n");
	safestrcat(cmos_comment,sizeof(cmos_comment),"TimeDivergeance_microseconds: Time to be added to get the emulated time, in microseconds.\n");
	safestrcat(cmos_comment,sizeof(cmos_comment),"s100: 100th second register content on XT RTC (0-255, Usually BCD stored as integer)\n");
	safestrcat(cmos_comment,sizeof(cmos_comment),"s10000: 10000th second register content on XT RTC (0-255, Usually BCD stored as integer)\n");
	safestrcat(cmos_comment,sizeof(cmos_comment),"RAM[hexnumber]: The contents of the CMOS RAM location(0-255)\n");
	safestrcat(cmos_comment,sizeof(cmos_comment),"extraRAM[hexnumber]: The contents of the extra RAM location(0-255)\n");
	safestrcat(cmos_comment,sizeof(cmos_comment),"centuryisbinary: The contents of the century byte is to be en/decoded as binary(value 1) instead of BCD(value 0), not to be used as a century byte.\n");
	safestrcat(cmos_comment,sizeof(cmos_comment),"cycletiming: 0=Time divergeance is relative to realtime. Not 0=Time is relative to 1-1-1970 midnight and running on the CPU timing.\n");
	safestrcat(cmos_comment, sizeof(cmos_comment), "floppy[number]_nodisk_type: The disk geometry to use as a base without a disk mounted. Values: ");
	for (c = 0; c < NUMITEMS(floppygeometries); ++c) //Parse all possible geometries!
	{
		safescatnprintf(cmos_comment, sizeof(cmos_comment), (c == 0) ? "%i=%s" : ", %i=%s", c, floppygeometries[c].text);
	}
	safestrcat(cmos_comment, sizeof(cmos_comment), "\n"); //End of the nodisk_type setting!

	safestrcat(cmos_comment, sizeof(cmos_comment), "cpu: 0=8086/8088, 1=NEC V20/V30, 2=80286, 3=80386, 4=80486, 5=Intel Pentium(without FPU), 6=Intel Pentium Pro(without FPU), 7=Intel Pentium II(without FPU)\n");
	safestrcat(cmos_comment, sizeof(cmos_comment), "cpus: 0=All available CPUs, 1+=fixed amount of CPUs(as many as supported)\n");
	safestrcat(cmos_comment, sizeof(cmos_comment), "databussize: 0=Full sized data bus of 16/32-bits, 1=Reduced data bus size\n");
	safestrcat(cmos_comment, sizeof(cmos_comment), "cpuspeed: 0=default, otherwise, limited to n cycles(>=0)\n");
	safestrcat(cmos_comment, sizeof(cmos_comment), "turbocpuspeed: 0=default, otherwise, limit to n cycles(>=0)\n");
	safestrcat(cmos_comment, sizeof(cmos_comment), "useturbocpuspeed: 0=Don't use, 1=Use\n");
	safestrcat(cmos_comment, sizeof(cmos_comment), "clockingmode: 0=Cycle-accurate clock, 1=IPS clock\n");
	safestrcat(cmos_comment, sizeof(cmos_comment), "CPUIDmode: 0=Modern mode, 1=Limited to leaf 1, 2=Set to DX on start");

	char *cmos_commentused=NULL;
	if (cmos_comment[0]) cmos_commentused = &cmos_comment[0];

	//XTCMOS
	if (!write_private_profile_uint64("XTCMOS",cmos_commentused,"gotCMOS",BIOS_Settings.got_XTCMOS,inifile)) ABORT_SAVEDATA //Gotten an CMOS?
	if (!saveBIOSCMOS(&BIOS_Settings.XTCMOS,"XTCMOS",cmos_commentused,inifile)) ABORT_SAVEDATA //Load the CMOS from the file!

	//ATCMOS
	if (!write_private_profile_uint64("ATCMOS",cmos_commentused,"gotCMOS",BIOS_Settings.got_ATCMOS,inifile)) ABORT_SAVEDATA //Gotten an CMOS?
	if (!saveBIOSCMOS(&BIOS_Settings.ATCMOS,"ATCMOS",cmos_commentused,inifile)) ABORT_SAVEDATA //Load the CMOS from the file!

	//CompaqCMOS
	if (!write_private_profile_uint64("CompaqCMOS",cmos_commentused,"gotCMOS",BIOS_Settings.got_CompaqCMOS,inifile)) ABORT_SAVEDATA //Gotten an CMOS?
	if (!saveBIOSCMOS(&BIOS_Settings.CompaqCMOS,"CompaqCMOS",cmos_commentused,inifile)) ABORT_SAVEDATA //The full saved CMOS!

	//PS2CMOS
	if (!write_private_profile_uint64("PS2CMOS",cmos_commentused,"gotCMOS",BIOS_Settings.got_PS2CMOS,inifile)) ABORT_SAVEDATA //Gotten an CMOS?
	if (!saveBIOSCMOS(&BIOS_Settings.PS2CMOS,"PS2CMOS",cmos_commentused,inifile)) ABORT_SAVEDATA //The full saved CMOS!

	//i430fxCMOS
	if (!write_private_profile_uint64("i430fxCMOS", cmos_commentused, "gotCMOS", BIOS_Settings.got_i430fxCMOS, inifile)) ABORT_SAVEDATA //Gotten an CMOS?
	if (!saveBIOSCMOS(&BIOS_Settings.i430fxCMOS, "i430fxCMOS", cmos_commentused,inifile)) ABORT_SAVEDATA //The full saved CMOS!

	//i440fxCMOS
	if (!write_private_profile_uint64("i440fxCMOS", cmos_commentused, "gotCMOS", BIOS_Settings.got_i440fxCMOS, inifile)) ABORT_SAVEDATA //Gotten an CMOS?
	if (!saveBIOSCMOS(&BIOS_Settings.i440fxCMOS, "i440fxCMOS", cmos_commentused, inifile)) ABORT_SAVEDATA //The full saved CMOS!

	if (!closeinifile(&inifile)) //Failed to write the ini file?
	{
		return 0; //Failed to write the ini file!
	}

	memcpy(&loadedsettings,&BIOS_Settings,sizeof(BIOS_Settings)); //Reload from buffer!
	loadedsettings_loaded = 1; //Buffered in memory!

	//Fully written!
	return 1; //BIOS Written & saved successfully!
}

uint_32 BIOS_GetMMUSize() //For MMU!
{
	if (__HW_DISABLED) return MBMEMORY; //Abort with default value (1MB memory)!
	return *(getarchmemory()); //Use all available memory always!
}

extern byte backgroundpolicy; //Background task policy. 0=Full halt of the application, 1=Keep running without video and audio muted, 2=Keep running with audio playback, recording muted, 3=Keep running fully without video.
extern byte EMU_RUNNING; //Emulator running? 0=Not running, 1=Running, Active CPU, 2=Running, Inactive CPU (BIOS etc.)

void BIOS_ValidateData() //Validates all data and unmounts/remounts if needed!
{
	char soundfont[256];
	if (__HW_DISABLED) return; //Abort!
	//Mount all devices!
	iofloppy0(BIOS_Settings.floppy0,0,BIOS_Settings.floppy0_readonly,0);
	iofloppy1(BIOS_Settings.floppy1,0,BIOS_Settings.floppy1_readonly,0);
	iohdd0(BIOS_Settings.hdd0,0,BIOS_Settings.hdd0_readonly,0);
	iohdd1(BIOS_Settings.hdd1,0,BIOS_Settings.hdd1_readonly,0);
	iocdrom0(BIOS_Settings.cdrom0,0,1,0);
	iocdrom1(BIOS_Settings.cdrom1,0,1,0);

	byte buffer[512]; //Little buffer for checking the files!
	int bioschanged = 0; //BIOS changed?
	bioschanged = 0; //Reset if the BIOS is changed!

	if ((!readdata(FLOPPY0,&buffer,0,sizeof(buffer))) && (strcmp(BIOS_Settings.floppy0,"")!=0)) //No disk mounted but listed?
	{
		if (!(getDSKimage(FLOPPY0) || getIMDimage(FLOPPY0))) //NOT a DSK/IMD image?
		{
			memset(&BIOS_Settings.floppy0[0],0,sizeof(BIOS_Settings.floppy0)); //Unmount!
			BIOS_Settings.floppy0_readonly = 0; //Reset readonly flag!
			bioschanged = 1; //BIOS changed!
		}
	}
	if ((!readdata(FLOPPY1,&buffer,0,sizeof(buffer))) && (strcmp(BIOS_Settings.floppy1,"")!=0)) //No disk mounted but listed?
	{
		if (!(getDSKimage(FLOPPY1) || getIMDimage(FLOPPY1))) //NOT a DSK/IMD image?
		{
			memset(&BIOS_Settings.floppy1[0],0,sizeof(BIOS_Settings.floppy1)); //Unmount!
			BIOS_Settings.floppy1_readonly = 0; //Reset readonly flag!
			bioschanged = 1; //BIOS changed!
		}
	}
	
	if ((!readdata(HDD0,&buffer,0,sizeof(buffer))) && (strcmp(BIOS_Settings.hdd0,"")!=0)) //No disk mounted but listed?
	{
		if (EMU_RUNNING == 0) //Not running?
		{
			memset(&BIOS_Settings.hdd0[0], 0, sizeof(BIOS_Settings.hdd0)); //Unmount!
			BIOS_Settings.hdd0_readonly = 0; //Reset readonly flag!
			bioschanged = 1; //BIOS changed!
		}
	}
	
	if ((!readdata(HDD1,&buffer,0,sizeof(buffer))) && (strcmp(BIOS_Settings.hdd1,"")!=0)) //No disk mounted but listed?
	{
		if (EMU_RUNNING == 0) //Not running?
		{
			memset(&BIOS_Settings.hdd1[0], 0, sizeof(BIOS_Settings.hdd1)); //Unmount!
			BIOS_Settings.hdd1_readonly = 0; //Reset readonly flag!
			bioschanged = 1; //BIOS changed!
		}
	}
	if (!getCUEimage(CDROM0)) //Not a CUE image?
	{
		if ((!readdata(CDROM0, &buffer, 0, sizeof(buffer))) && (strcmp(BIOS_Settings.cdrom0, "") != 0)) //No disk mounted but listed?
		{
			memset(&BIOS_Settings.cdrom0[0], 0, sizeof(BIOS_Settings.cdrom0)); //Unmount!
			bioschanged = 1; //BIOS changed!
		}
	}
	
	if (!getCUEimage(CDROM1))
	{
		if ((!readdata(CDROM1, &buffer, 0, sizeof(buffer))) && (strcmp(BIOS_Settings.cdrom1, "") != 0)) //No disk mounted but listed?
		{
			memset(&BIOS_Settings.cdrom1[0], 0, sizeof(BIOS_Settings.cdrom1)); //Unmount!
			bioschanged = 1; //BIOS changed!
		}
	}

	//Unmount/remount!
	iofloppy0(BIOS_Settings.floppy0,0,BIOS_Settings.floppy0_readonly,0);
	iofloppy1(BIOS_Settings.floppy1,0,BIOS_Settings.floppy1_readonly,0);
	iohdd0(BIOS_Settings.hdd0,0,BIOS_Settings.hdd0_readonly,0);
	iohdd1(BIOS_Settings.hdd1,0,BIOS_Settings.hdd1_readonly,0);
	iocdrom0(BIOS_Settings.cdrom0,0,1,0); //CDROM always read-only!
	iocdrom1(BIOS_Settings.cdrom1,0,1,0); //CDROM always read-only!

	if (BIOS_Settings.SoundFont[0]) //Gotten a soundfont set?
	{
		memset(&soundfont, 0, sizeof(soundfont)); //Init!
		safestrcpy(soundfont,sizeof(soundfont), soundfontpath); //The path to the soundfont!
		safestrcat(soundfont,sizeof(soundfont), "/");
		safestrcat(soundfont,sizeof(soundfont), BIOS_Settings.SoundFont); //The full path to the soundfont!
		if (!FILE_EXISTS(soundfont)) //Not found?
		{
			memset(BIOS_Settings.SoundFont, 0, sizeof(BIOS_Settings.SoundFont)); //Invalid soundfont!
			bioschanged = 1; //BIOS changed!
		}
	}

	if (BIOS_Settings.useDirectMIDI && !directMIDISupported()) //Unsupported Direct MIDI?
	{
		BIOS_Settings.useDirectMIDI = 0; //Unsupported: disable the functionality!
		bioschanged = 1; //BIOS changed!
	}

	if (*(getarchDataBusSize()) > 1) //Invalid bus size?
	{
		*(getarchDataBusSize()) = 0; //Default bus size!
		bioschanged = 1; //BIOS changed!
	}

	if (bioschanged)
	{
		forceBIOSSave(); //Force saving!
	}
}

extern byte advancedlog; //Advanced log setting!

void BIOS_LoadIO(int showchecksumerrors) //Loads basic I/O drives from BIOS!
{
	if (__HW_DISABLED) return; //Abort!
	ioInit(); //Reset I/O system!
	exec_showchecksumerrors = showchecksumerrors; //Allow checksum errors to be shown!
	BIOS_LoadData();//Load BIOS options!
	BIOS_ValidateData(); //Validate all data!
	GPU_AspectRatio(BIOS_Settings.aspectratio); //Keep the aspect ratio?
	exec_showchecksumerrors = 0; //Don't Allow checksum errors to be shown!
	backgroundpolicy = MIN(BIOS_Settings.backgroundpolicy,3); //Load the new background policy!
	advancedlog = LIMITRANGE(BIOS_Settings.advancedlog,0,1);
}

void BIOS_ShowBIOS() //Shows mounted drives etc!
{
	uint_32 blocksize;
	if (__HW_DISABLED) return; //Abort!
	exec_showchecksumerrors = 0; //No checksum errors to show!
	BIOS_LoadData();
	BIOS_ValidateData(); //Validate all data before continuing!

	printmsg(0xF,"Memory installed: ");
	blocksize = (is_XT) ? MEMORY_BLOCKSIZE_XT : MEMORY_BLOCKSIZE_AT_LOW; //What block size is used?
	printmsg(0xE,"%u blocks (%uKB / %uMB)\r\n",SAFEDIV(BIOS_GetMMUSize(),blocksize),(BIOS_GetMMUSize()/1024),(BIOS_GetMMUSize()/MBMEMORY));

	printmsg(0xF,"\r\n"); //A bit of space between memory and disks!
	int numdrives = 0;
	if (strcmp(BIOS_Settings.hdd0,"")!=0) //Have HDD0?
	{
		printmsg(0xF,"Primary master: %s",BIOS_Settings.hdd0);
		if (BIOS_Settings.hdd0_readonly) //Read-only?
		{
			printmsg(0x4," <R>");
		}
		printmsg(0xF,"\r\n"); //Newline!
		++numdrives;
	}
	if (strcmp(BIOS_Settings.hdd1,"")!=0) //Have HDD1?
	{
		printmsg(0xF,"Primary slave: %s",BIOS_Settings.hdd1);
		if (BIOS_Settings.hdd1_readonly) //Read-only?
		{
			printmsg(0x4," <R>");
		}
		printmsg(0xF,"\r\n"); //Newline!
		++numdrives;
	}
	if (strcmp(BIOS_Settings.cdrom0,"")!=0) //Have CDROM0?
	{
		printmsg(0xF,"Secondary master: %s\r\n",BIOS_Settings.cdrom0);
		++numdrives;
	}
	if (strcmp(BIOS_Settings.cdrom1,"")!=0) //Have CDROM1?
	{
		printmsg(0xF,"Secondary slave: %s\r\n",BIOS_Settings.cdrom1);
		++numdrives;
	}

	if (((strcmp(BIOS_Settings.floppy0,"")!=0) || (strcmp(BIOS_Settings.floppy1,"")!=0)) && numdrives>0) //Have drives and adding floppy?
	{
		printmsg(0xF,"\r\n"); //Insert empty row between floppy and normal disks!
	}

	if (strcmp(BIOS_Settings.floppy0,"")!=0) //Have FLOPPY0?
	{
		printmsg(0xF,"Floppy disk detected: %s",BIOS_Settings.floppy0);
		if (BIOS_Settings.floppy0_readonly) //Read-only?
		{
			printmsg(0x4," <R>");
		}
		printmsg(0xF,"\r\n"); //Newline!
		++numdrives;
	}

	if (strcmp(BIOS_Settings.floppy1,"")!=0) //Have FLOPPY1?
	{
		printmsg(0xF,"Floppy disk detected: %s",BIOS_Settings.floppy1);
		if (BIOS_Settings.floppy1_readonly) //Read-only?
		{
			printmsg(0x4," <R>");
		}
		printmsg(0xF,"\r\n"); //Newline!
		++numdrives;
	}

	if (*(getarchemulated_CPU())==CPU_8086) //8086?
	{
		if (*(getarchDataBusSize())) //8-bit bus?
		{
			printmsg(0xF, "Installed CPU: Intel 8088\r\n"); //Emulated CPU!
		}
		else //16-bit bus?
		{
			printmsg(0xF,"Installed CPU: Intel 8086\r\n"); //Emulated CPU!
		}
	}
	else if (*(getarchemulated_CPU())==CPU_NECV30) //NECV30?
	{
		if (*(getarchDataBusSize())) //8-bit bus?
		{
			printmsg(0xF, "Installed CPU: NEC V20\r\n"); //Emulated CPU!
		}
		else //16-bit bus?
		{
			printmsg(0xF, "Installed CPU: NEC V30\r\n"); //Emulated CPU!
		}
	}
	else if (*(getarchemulated_CPU()) == CPU_80286) //80286?
	{
		printmsg(0xF, "Installed CPU: Intel 80286\r\n"); //Emulated CPU!
	}
	else if (*(getarchemulated_CPU()) == CPU_80386) //80386?
	{
		printmsg(0xF, "Installed CPU: Intel 80386\r\n"); //Emulated CPU!
	}
	else if (*(getarchemulated_CPU()) == CPU_80486) //80486?
	{
		printmsg(0xF, "Installed CPU: Intel 80486\r\n"); //Emulated CPU!
	}
	else if (*(getarchemulated_CPU()) == CPU_PENTIUM) //80586?
	{
		printmsg(0xF, "Installed CPU: Intel Pentium(without FPU)\r\n"); //Emulated CPU!
	}
	else if (*(getarchemulated_CPU()) == CPU_PENTIUMPRO) //80686?
	{
		printmsg(0xF, "Installed CPU: Intel Pentium Pro(without FPU)\r\n"); //Emulated CPU!
	}
	else if (*(getarchemulated_CPU()) == CPU_PENTIUM2) //80786?
	{
		printmsg(0xF, "Installed CPU: Intel Pentium II(without FPU)\r\n"); //Emulated CPU!
	}
	else //Unknown CPU?
	{
		printmsg(0x4,"Installed CPU: Unknown\r\n"); //Emulated CPU!
	}

	if (numdrives==0) //No drives?
	{
		printmsg(0x4,"Warning: no drives have been detected!\r\nPlease enter settings and specify some disks.\r\n");
	}
}

//Defines for booting!
#define BOOT_FLOPPY 0
#define BOOT_HDD 1
#define BOOT_CDROM 2
#define BOOT_NONE 3

//Boot order for boot sequence!
byte BOOT_ORDER[15][3] =
{
//First full categories (3 active)
	{BOOT_FLOPPY, BOOT_CDROM, BOOT_HDD}, //Floppy, Cdrom, Hdd?
	{BOOT_FLOPPY, BOOT_HDD, BOOT_CDROM}, //Floppy, Hdd, Cdrom?
	{BOOT_CDROM, BOOT_FLOPPY, BOOT_HDD}, //Cdrom, Floppy, Hdd?
	{BOOT_CDROM, BOOT_HDD, BOOT_FLOPPY}, //Cdrom, Hdd, Floppy?
	{BOOT_HDD, BOOT_FLOPPY, BOOT_CDROM}, //Hdd, Floppy, Cdrom?
	{BOOT_HDD, BOOT_CDROM, BOOT_FLOPPY}, //Hdd, Cdrom, Floppy?
//Now advanced categories (2 active)!
	{BOOT_FLOPPY, BOOT_CDROM, BOOT_NONE}, //Floppy, Cdrom?
	{BOOT_FLOPPY, BOOT_HDD, BOOT_NONE}, //Floppy, Hdd?
	{BOOT_CDROM, BOOT_FLOPPY, BOOT_NONE}, //Cdrom, Floppy?
	{BOOT_CDROM, BOOT_HDD, BOOT_NONE}, //Cdrom, Hdd?
	{BOOT_HDD, BOOT_FLOPPY, BOOT_NONE}, //Hdd, Floppy?
	{BOOT_HDD, BOOT_CDROM, BOOT_NONE}, //Hdd, Cdrom?
//Finally single categories (1 active)
	{BOOT_FLOPPY, BOOT_NONE, BOOT_NONE}, //Floppy only?
	{BOOT_CDROM, BOOT_NONE, BOOT_NONE}, //CDROM only?
	{BOOT_HDD, BOOT_NONE, BOOT_NONE} //HDD only?
};

//Boot order (string representation)
char BOOT_ORDER_STRING[15][30] =
{
//Full categories (3 active)
	"FLOPPY, CDROM, HDD",
	"FLOPPY, HDD, CDROM",
	"CDROM, FLOPPY, HDD",
	"CDROM, HDD, FLOPPY",
	"HDD, FLOPPY, CDROM",
	"HDD, CDROM, FLOPPY",
//Advanced categories (2 active)
	"FLOPPY, CDROM",
	"FLOPPY, HDD",
	"CDROM, FLOPPY",
	"CDROM, HDD",
	"HDD, FLOPPY",
	"HDD, CDROM",
//Finally single categories (1 active)
	"FLOPPY ONLY",
	"CDROM ONLY",
	"HDD ONLY",
};

//Try to boot a category (BOOT_FLOPPY, BOOT_HDD, BOOT_CDROM)

int try_boot(byte category)
{
	if (__HW_DISABLED) return 0; //Abort!
	switch (category)
	{
	case BOOT_FLOPPY: //Boot FLOPPY?
		if (CPU_boot(FLOPPY0)) //Try floppy0!
		{
			return 1; //OK: booted!
		}
		else
		{
			return CPU_boot(FLOPPY1); //Try floppy1!
		}
	case BOOT_HDD: //Boot HDD?
		if (CPU_boot(HDD0)) //Try hdd0!
		{
			return 1; //OK: booted!
		}
		else
		{
			return CPU_boot(HDD1); //Try hdd1!
		}
	case BOOT_CDROM: //Boot CDROM?
		if (CPU_boot(CDROM0)) //Try cdrom0!
		{
			return 1; //OK: booted!
		}
		else
		{
			return CPU_boot(CDROM1); //Try cdrom1!
		}
	case BOOT_NONE: //No device?
		break; //Don't boot!
	default: //Default?
		break; //Don't boot!
	}
	return 0; //Not booted!
}

/*

boot_system: boots using BIOS boot order!
returns: TRUE on booted, FALSE on no bootable disk found.

*/

int boot_system()
{
	if (__HW_DISABLED) return 0; //Abort!
	int c;
	for (c=0; c<3; c++) //Try 3 boot devices!
	{
		if (try_boot(BOOT_ORDER[BIOS_Settings.bootorder][c])) //Try boot using currently specified boot order!
		{
			return 1; //Booted!
		}
	}
	return 0; //Not booted at all!
}

/*

Basic BIOS Keyboard support!

*/

void BIOS_writeKBDCMD(byte cmd)
{
	if (__HW_DISABLED) return; //Abort!
	write_8042(0x60,cmd); //Write the command directly to the controller!
}

extern byte force8042; //Force 8042 style handling?

void BIOSKeyboardInit() //BIOS part of keyboard initialisation!
{
	if (__HW_DISABLED) return; //Abort!
	if (is_XT) return;
	byte result; //For holding the result from the hardware!
	force8042 = 1; //We're forcing 8042 style init!

	for (;(PORT_IN_B(0x64) & 0x2);) //Wait for output of data?
	{
		update8042(KEYBOARD_DEFAULTTIMEOUT); //Update the keyboard when allowed!
		updatePS2Keyboard(KEYBOARD_DEFAULTTIMEOUT); //Update the keyboard when allowed!
	}

	BIOS_writeKBDCMD(0xED); //Set/reset status indicators!

	for (;(PORT_IN_B(0x64) & 0x2);) //Wait for output of data?
	{
		update8042(KEYBOARD_DEFAULTTIMEOUT); //Update the keyboard when allowed!
		updatePS2Keyboard(KEYBOARD_DEFAULTTIMEOUT); //Update the keyboard when allowed!
	}

	for (;!(PORT_IN_B(0x64) & 0x1);) //Wait for input data?
	{
		updatePS2Keyboard(KEYBOARD_DEFAULTTIMEOUT); //Update the keyboard when allowed!
		update8042(KEYBOARD_DEFAULTTIMEOUT); //Update the keyboard when allowed!
	}

	result = PORT_IN_B(0x60); //Check the result!
	if (result!=0xFA) //NAC?
	{
		raiseError("Keyboard BIOS initialisation","Set/reset status indication command result: %02X",result);
	}

	write_8042(0x60,0x02); //Turn on NUM LOCK led!

	for (;(PORT_IN_B(0x64) & 0x2);) //Wait for output of data?
	{
		update8042(KEYBOARD_DEFAULTTIMEOUT); //Update the keyboard when allowed!
		updatePS2Keyboard(KEYBOARD_DEFAULTTIMEOUT); //Update the keyboard when allowed!
	}

	for (;!(PORT_IN_B(0x64) & 0x1);) //Wait for input data?
	{
		updatePS2Keyboard(KEYBOARD_DEFAULTTIMEOUT); //Update the keyboard when allowed!
		update8042(KEYBOARD_DEFAULTTIMEOUT); //Update the keyboard when allowed!
	}

	if (!(PORT_IN_B(0x64)&0x1)) //No input data?
	{
		raiseError("Keyboard BIOS initialisation","No turn on NUM lock led result!");
	}
	result = PORT_IN_B(0x60); //Must be 0xFA!
	if (result!=0xFA) //Error?
	{
		raiseError("Keyboard BIOS initialisation","Couldn't turn on Num Lock LED! Result: %02X",result);
	}

	PORT_OUT_B(0x64, 0xAE); //Enable first PS/2 port!

	for (;(PORT_IN_B(0x64) & 0x2);) //Wait for output of data?
	{
		update8042(KEYBOARD_DEFAULTTIMEOUT); //Update the keyboard when allowed!
		updatePS2Keyboard(KEYBOARD_DEFAULTTIMEOUT); //Update the keyboard when allowed!
	}

	BIOS_writeKBDCMD(0xF4); //Enable scanning!

	for (;(PORT_IN_B(0x64) & 0x2);) //Wait for output of data?
	{
		update8042(KEYBOARD_DEFAULTTIMEOUT); //Update the keyboard when allowed!
		updatePS2Keyboard(KEYBOARD_DEFAULTTIMEOUT); //Update the keyboard when allowed!
	}

	PORT_OUT_B(0x64, 0x20); //Read PS2ControllerConfigurationByte!
	for (;(PORT_IN_B(0x64) & 0x2);) //Wait for output of data?
	{
		update8042(KEYBOARD_DEFAULTTIMEOUT); //Update the keyboard when allowed!
		updatePS2Keyboard(KEYBOARD_DEFAULTTIMEOUT); //Update the keyboard when allowed!
	}


	byte PS2ControllerConfigurationByte;
	PS2ControllerConfigurationByte = PORT_IN_B(0x60); //Read result!

	PS2ControllerConfigurationByte |= 1; //Enable our interrupt!
	PORT_OUT_B(0x64, 0x60); //Write PS2ControllerConfigurationByte!
	for (;(PORT_IN_B(0x64) & 0x2);) //Wait for output of data?
	{
		update8042(KEYBOARD_DEFAULTTIMEOUT); //Update the keyboard when allowed!
		updatePS2Keyboard(KEYBOARD_DEFAULTTIMEOUT); //Update the keyboard when allowed!
	}
	PORT_OUT_B(0x60, PS2ControllerConfigurationByte); //Write the new configuration byte!
	for (;(PORT_IN_B(0x64) & 0x2);) //Wait for output of data?
	{
		update8042(KEYBOARD_DEFAULTTIMEOUT); //Update the keyboard when allowed!
		updatePS2Keyboard(KEYBOARD_DEFAULTTIMEOUT); //Update the keyboard when allowed!
	}
	force8042 = 0; //Disable 8042 style init!
}
