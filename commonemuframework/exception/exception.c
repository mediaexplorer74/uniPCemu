/*

Copyright (C) 2019 - 2021 Superfury

This file is part of The Common Emulator Framework.

The Common Emulator Framework is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

The Common Emulator Framework is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with The Common Emulator Framework.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "headers/types.h" //Basic info!
#include "headers/fopen64.h" //Our own types!
#ifdef IS_PSP
//PSP Only: exception handler!
//Use original sleep!
#undef sleep
#include <pspsdk.h>
#include <pspctrl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "headers/emu/threads.h" //Thread support (for all emulator's threads!)

PspDebugRegBlock exception_regs;

extern SceModule module_info;
extern int _ftext;

static const char *codeTxt[32] =
{
    "Interrupt", "TLB modification", "TLB load/inst fetch", "TLB store",
    "Address load/inst fetch", "Address store", "Bus error (instr)",
    "Bus error (data)", "Syscall", "Breakpoint", "Reserved instruction",
    "Coprocessor unusable", "Arithmetic overflow", "Unknown 14",
    "Unknown 15", "Unknown 16", "Unknown 17", "Unknown 18", "Unknown 19",
    "Unknown 20", "Unknown 21", "Unknown 22", "Unknown 23", "Unknown 24",
    "Unknown 25", "Unknown 26", "Unknown 27", "Unknown 28", "Unknown 29",
    "Unknown 31"
};

static const unsigned char regName[32][5] =
{
    "zr", "at", "v0", "v1", "a0", "a1", "a2", "a3",
    "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
    "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"
};

void ExceptionHandler(PspDebugRegBlock * regs)
{
    int i;
    SceCtrlData pad;
    termThreads(); //Stop all our threads, but our own!

    pspDebugScreenInit();
    pspDebugScreenSetBackColor(0x00FF0000);
    pspDebugScreenSetTextColor(0xFFFFFFFF);
    pspDebugScreenClear();
    pspDebugScreenPrintf("Your PSP has just crashed!\n");
    pspDebugScreenPrintf("Exception details:\n\n");

    pspDebugScreenPrintf("Exception - %s\n", codeTxt[(regs->cause >> 2) & 31]);
    pspDebugScreenPrintf("EPC       - %08X / %s.text + %08X\n", (int)regs->epc, module_info.modname, (unsigned int)(regs->epc-(int)&_ftext));
    pspDebugScreenPrintf("Cause     - %08X\n", (int)regs->cause);
    pspDebugScreenPrintf("Status    - %08X\n", (int)regs->status);
    pspDebugScreenPrintf("BadVAddr  - %08X\n", (int)regs->badvaddr);
    for(i=0; i<32; i+=4) pspDebugScreenPrintf("%s:%08X %s:%08X %s:%08X %s:%08X\n", regName[i], (int)regs->r[i], regName[i+1], (int)regs->r[i+1], regName[i+2], (int)regs->r[i+2], regName[i+3], (int)regs->r[i+3]);

	pspDebugScreenPrintf("\nThe offending routine may be identified with:\n\n"
		"psp-addr2line -e target.elf -f -C 0x%" SPRINTF_x_UINT32 " 0x%" SPRINTF_x_UINT32 " 0x%" SPRINTF_x_UINT32 "\n",
		regs->epc - 0x08800000, regs->badvaddr - 0x08800000, regs->r[31] - 0x08800000);

	sceKernelDelayThread(1000000);
    pspDebugScreenPrintf("\n\nPress X to dump information on file exception.log and quit");
    pspDebugScreenPrintf("\nPress O to quit");

    for (;;){
        sceCtrlReadBufferPositive(&pad, 1);
        if (pad.Buttons & PSP_CTRL_CROSS){
            BIGFILE *log = emufopen64("exception.log", "w");
            if (log != NULL){
                char testo[512];
				cleardata(testo,sizeof(testo)); //Init!
                snprintf(testo,sizeof(testo), "Exception details:\n\n");
                emufwrite64(testo, 1, strnlen(testo,sizeof(testo)), log);
                snprintf(testo,sizeof(testo), "Exception - %s\n", codeTxt[(regs->cause >> 2) & 31]);
                emufwrite64(testo, 1, strnlen(testo,sizeof(testo)), log);
                snprintf(testo,sizeof(testo), "EPC       - %08X / %s.text + %08X\n", (int)regs->epc, module_info.modname, (unsigned int)(regs->epc-(int)&_ftext));
                emufwrite64(testo, 1, strnlen(testo,sizeof(testo)), log);
                snprintf(testo,sizeof(testo), "Cause     - %08X\n", (int)regs->cause);
                emufwrite64(testo, 1, strnlen(testo,sizeof(testo)), log);
                snprintf(testo,sizeof(testo), "Status    - %08X\n", (int)regs->status);
                emufwrite64(testo, 1, strnlen(testo,sizeof(testo)), log);
                snprintf(testo,sizeof(testo), "BadVAddr  - %08X\n", (int)regs->badvaddr);
                emufwrite64(testo, 1, strnlen(testo,sizeof(testo)), log);
                for(i=0; i<32; i+=4){
                    snprintf(testo,sizeof(testo), "%s:%08X %s:%08X %s:%08X %s:%08X\n", regName[i], (int)regs->r[i], regName[i+1], (int)regs->r[i+1], regName[i+2], (int)regs->r[i+2], regName[i+3], (int)regs->r[i+3]);
                    emufwrite64(testo, 1, strnlen(testo,sizeof(testo)), log);
                }
		snprintf(testo,sizeof(testo),"\nThe offending routine may be identified with:\n\n"
		"psp-addr2line -e eboot.elf -f -C 0x%" SPRINTF_x_UINT32 " 0x%" SPRINTF_x_UINT32 " 0x%" SPRINTF_x_UINT32 "\n",
		regs->epc - 0x08800000, regs->badvaddr - 0x08800000, regs->r[31] - 0x08800000);
                emufwrite64(testo, 1, strnlen(testo,sizeof(testo)), log); //Also give information about debugging!
                emufclose64(log);
            }
            break;
        }else if (pad.Buttons & PSP_CTRL_CIRCLE){
            break;
        }
		sceKernelDelayThread(100000);
    }    
    sceKernelExitGame();
}

void initExceptionHandler()
{
   SceKernelLMOption option;
   int args[2], fd, modid;

   memset(&option, 0, sizeof(option));
   option.size = sizeof(option);
   option.mpidtext = PSP_MEMORY_PARTITION_KERNEL;
   option.mpiddata = PSP_MEMORY_PARTITION_KERNEL;
   option.position = 0;
   option.access = 1;

   if((modid = sceKernelLoadModule("exception.prx", 0, &option)) >= 0)
   {
      args[0] = (int)ExceptionHandler;
      args[1] = (int)&exception_regs;
      sceKernelStartModule(modid, 8, args, &fd, NULL);
   }
}
#else
void initExceptionHandler() {} //We don't handle exceptions on other platforms: they already have correct handlers?
#endif