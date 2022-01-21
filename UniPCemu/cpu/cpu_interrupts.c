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
#include "headers/cpu/cpu.h" //CPU!
#include "headers/cpu/easyregs.h" //Easy registers!
#include "headers/cpu/protection.h" //Protection support!
#include "headers/emu/debugger/debugger.h" //For logging registers!
#include "headers/mmu/mmuhandler.h" //Direct memory access support! 
#include "headers/support/log.h" //Logging support for debugging!
#include "headers/cpu/cpu_OP8086.h" //8086 support!
#include "headers/cpu/cpu_execution.h" //Execution phase support!
#include "headers/cpu/biu.h" //PIQ flushing support!
#include "headers/cpu/multitasking.h" //Task switching/faulting support!
#include "headers/cpu/cpu_stack.h" //Stack support!
#include "headers/cpu/easyregs.h" //Easy register support!
#include "headers/hardware/pic.h" //NMI mapping support!

//Are we to disable NMI's from All(or Memory only)?
#define DISABLE_MEMNMI
//#define DISABLE_NMI
//Log the INT10h call to set 640x480x256 color mode.
//#define LOG_ET34K640480256_SET
//Log the INT calls and IRETs when defined.
//#define LOG_INTS

void CPU_setint(byte intnr, word segment, word offset) //Set real mode IVT entry!
{
	MMU_ww(-1,0x0000,((intnr<<2)|2),segment,0); //Copy segment!
	MMU_ww(-1,0x0000,(intnr<<2),offset,0); //Copy offset!
}

void CPU_getint(byte intnr, word *segment, word *offset) //Set real mode IVT entry!
{
	*segment = MMU_rw(-1,0x0000,((intnr<<2)|2),0,0); //Copy segment!
	*offset = MMU_rw(-1,0x0000,(intnr<<2),0,0); //Copy offset!
}

//Interrupt support for timings!
extern byte singlestep; //Enable EMU-driven single step!
extern byte allow_debuggerstep; //Disabled by default: needs to be enabled by our BIOS!
extern byte advancedlog; //Advanced log setting

extern byte MMU_logging; //Are we logging from the MMU?
byte CPU_customint(byte intnr, word retsegment, uint_32 retoffset, int_64 errorcode, byte is_interrupt) //Used by soft (below) and exceptions/hardware!
{
	byte checkinterruptstep;
	char errorcodestr[256];
	word destCS;
	CPU[activeCPU].executed = 0; //Default: still busy executing!
	CPU[activeCPU].CPU_interruptraised = 1; //We've raised an interrupt!
	CPU[activeCPU].REPPending = CPU[activeCPU].repeating = 0; //Not repeating anymore!
	CPU[activeCPU].allowInterrupts = 1; //Allow interrupts again after this interrupt finishes(count as an instruction executing)!
	if ((getcpumode()==CPU_MODE_REAL) || (errorcode==-4)) //Use IVT structure in real mode only, or during VME when processing real-mode style interrupts(errorcode of -4)!
	{
		if (((errorcode == -4)?0x3FF:(CPU[activeCPU].registers->IDTR.limit))<((intnr<<2)|3)) //IVT limit too low?
		{
			if ((MMU_logging == 1) && advancedlog) //Are we logging?
			{
				dolog("debugger","#DF fault(-1)!");
			}

			if (CPU_faultraised(8)) //Able to fault?
			{
				CPU_executionphase_startinterrupt(8,2,0); //IVT limit problem or double fault redirect!
				return 0; //Abort!
			}
			else return 0; //Abort on triple fault!
		}
		//errorcode=-4: VME extensions are used!
		#ifdef LOG_ET34K640480256_SET
		if ((intnr==0x10) && (REG_AX==0x002E) && (errorcode==-1)) //Setting the video mode to 0x2E?
		{
			waitingforiret = 1; //Waiting for IRET!
			oldIP = retoffset;
			oldCS = retsegment; //Backup the return position!
		}
		#endif
		checkinterruptstep = 0; //Init!
		if (CPU[activeCPU].internalinterruptstep == checkinterruptstep)
		{
			if (errorcode == -4) //VME extensions require paging check as well?
			{
				if (checkMMUaccess16(-1, 0, (intnr << 2), 1|0xA0, getCPL(), 0, 0 | 0x8)) return 0; //Direct access Fault on IP?
				if (checkMMUaccess16(-1, 0, ((intnr << 2)|2), 1|0xA0, getCPL(), 0, 0 | 0x8)) return 0; //Direct access Fault on CS?
			}
			if (checkStackAccess(3, 1, 0)) return 0; //We must allow three pushes to succeed to be able to throw an interrupt!
		}
		word flags;
		flags = REG_FLAGS; //Default flags!
		if ((getcpumode() == CPU_MODE_8086) && (errorcode == -4) && (FLAG_PL != 3)) //Use virtual interrupt flag instead?
		{
			flags = (flags&~F_IF) | (FLAG_VIF ? F_IF : 0); //Replace the interrupt flag on the stack image with the Virtual Interrupt flag instead!
			flags |= 0x3000; //IOPL of 3 on the stack image!
		}

		if (CPU8086_internal_interruptPUSHw(checkinterruptstep,&flags,0)) return 0; //Busy pushing flags!
		checkinterruptstep += 2;
		if (CPU8086_internal_interruptPUSHw(checkinterruptstep,&retsegment,0)) return 0; //Busy pushing return segment!
		checkinterruptstep += 2;
		word retoffset16 = (retoffset&0xFFFF);
		if (CPU8086_internal_interruptPUSHw(checkinterruptstep,&retoffset16,0)) return 0; //Busy pushing return offset!
		checkinterruptstep += 2;
		if (CPU[activeCPU].internalinterruptstep==checkinterruptstep) //Handle specific EU timings here?
		{
			if (EMULATED_CPU==CPU_8086) //Known timings in between?
			{
				CPU[activeCPU].cycles_OP += 36; //We take 20 cycles to execute on a 8086/8088 EU!
				++CPU[activeCPU].internalinterruptstep; //Next step to be taken!
				CPU[activeCPU].executed = 0; //We haven't executed!
				return 0; //Waiting to complete!
			}
			else ++CPU[activeCPU].internalinterruptstep; //Skip anyways!
		}
		++checkinterruptstep;
		if (CPU8086_internal_stepreadinterruptw(checkinterruptstep,-1,0,(intnr<<2)+((errorcode!=-4)?CPU[activeCPU].registers->IDTR.base:0),&CPU[activeCPU].destINTIP,0)) return 0; //Read destination IP!
		checkinterruptstep += 2;
		if (CPU8086_internal_stepreadinterruptw(checkinterruptstep,-1,0,((intnr<<2)|2) + ((errorcode!=-4)?CPU[activeCPU].registers->IDTR.base:0),&CPU[activeCPU].destINTCS,0)) return 0; //Read destination CS!
		checkinterruptstep += 2;

		if ((getcpumode() == CPU_MODE_8086) && (errorcode == -4) && (FLAG_PL != 3)) //Use virtual interrupt flag instead?
		{
			FLAGW_VIF(0); //We're calling the interrupt!
		}
		else //Normal interrupt flag handling!
		{
			FLAGW_IF(0); //We're calling the interrupt!
		}
		FLAGW_TF(0); //We're calling an interrupt, resetting debuggers!
		
		if (EMULATED_CPU>=CPU_80486)
		{
			FLAGW_AC(0); //Clear Alignment Check flag too!
		}

		//Load EIP and CS destination to use from the original 16-bit data!
		CPU[activeCPU].destEIP = (uint_32)CPU[activeCPU].destINTIP;
		destCS = CPU[activeCPU].destINTCS;
		cleardata(&errorcodestr[0],sizeof(errorcodestr)); //Clear the error code!
		if (errorcode==-1) //No error code?
		{
			safestrcpy(errorcodestr,sizeof(errorcodestr),"-1");
		}
		else
		{
			snprintf(errorcodestr,sizeof(errorcodestr),"%08" SPRINTF_X_UINT32,(uint_32)errorcode); //The error code itself!
		}
		#ifdef LOG_INTS
		dolog("cpu","Interrupt %02X=%04X:%08X@%04X:%04X(%02X); ERRORCODE: %s; STACK=%04X:%08X",intnr, CPU[activeCPU].destCS, CPU[activeCPU].destEIP,REG_CS,REG_EIP,CPU[activeCPU].currentopcode,errorcodestr,REG_SS,REG_ESP); //Log the current info of the call!
		#endif
		if ((MMU_logging == 1) && advancedlog) dolog("debugger","Interrupt %02X=%04X:%08X@%04X:%04X(%02X); ERRORCODE: %s",intnr, CPU[activeCPU].destINTCS, CPU[activeCPU].destEIP,REG_CS,REG_EIP,CPU[activeCPU].currentopcode,errorcodestr); //Log the current info of the call!
		if (segmentWritten(CPU_SEGMENT_CS,destCS,0)) return 1; //Interrupt to position CS:EIP/CS:IP in table.
		if (CPU_condflushPIQ(-1)) //We're jumping to another address!
		{
			return 0; //Errored out!
		}
		CPU[activeCPU].executed = 1; //We've executed: process the next instruction!
		CPU_interruptcomplete(); //Prepare us for new instructions!

		//No error codes are pushed in (un)real mode! Only in protected mode!
		return 1; //OK!
	}
	//Use Protected mode IVT?
	return CPU_ProtectedModeInterrupt(intnr,retsegment,retoffset,errorcode,is_interrupt); //Execute the protected mode interrupt!
}

byte CPU_INT(byte intnr, int_64 errorcode, byte is_interrupt) //Call an software interrupt; WARNING: DON'T HANDLE ANYTHING BUT THE REGISTERS ITSELF!
{
	//Now, jump to it!
	return CPU_customint(intnr, CPU[activeCPU].INTreturn_CS, CPU[activeCPU].INTreturn_EIP,errorcode,is_interrupt); //Execute real interrupt, returning to current address!
}

void CPU_IRET()
{
	word V86SegRegs[4]; //All V86 mode segment registers!
	byte V86SegReg; //Currently processing segment register!
	byte oldCPL = getCPL(); //Original CPL
	word tempCS, tempSS;
	uint_32 tempEFLAGS;
	//Special effect: re-enable NMI! This isn't dependent on the instruction succeeding or not(nor documented, just when it's executed)!
	CPU[activeCPU].NMIMasked = 0; //We're allowing NMI again!

	if (getcpumode()==CPU_MODE_REAL) //Use IVT?
	{
		if (CPU[activeCPU].stackchecked==0) { if (checkStackAccess(3,0, CPU[activeCPU].CPU_Operand_size)) { return; } ++CPU[activeCPU].stackchecked; } //3 Word POPs!
		if (CPU8086_internal_POPw(0,&CPU[activeCPU].IRET_IP, CPU[activeCPU].CPU_Operand_size)) return; //POP IP!
		if (CPU8086_internal_POPw(2,&CPU[activeCPU].IRET_CS, CPU[activeCPU].CPU_Operand_size)) return; //POP CS!
		if (CPU8086_internal_POPw(4,&CPU[activeCPU].IRET_FLAGS, CPU[activeCPU].CPU_Operand_size)) return; //POP FLAGS!
		CPU[activeCPU].destEIP = (uint_32)CPU[activeCPU].IRET_IP; //POP IP!
		if (segmentWritten(CPU_SEGMENT_CS, CPU[activeCPU].IRET_CS, 3)) return; //We're loading because of an IRET!
		REG_FLAGS = CPU[activeCPU].IRET_FLAGS; //Pop flags!
		CPU_flushPIQ(-1); //We're jumping to another address!
		#ifdef LOG_INTS
		dolog("cpu","IRET@%04X:%08X to %04X:%04X; STACK=%04X:%08X",CPU[activeCPU].exec_CS,CPU[activeCPU].exec_EIP,REG_CS,REG_EIP,tempSS,backupESP); //Log the current info of the call!
		#endif
		#ifdef LOG_ET34K640480256_SET
		if (waitingforiret) //Waiting for IRET?
		{
			//if ((REG_CS==oldCS) && (REG_IP==oldIP)) //Returned?
			{
				waitingforiret = 0; //We're finished with the logging information!
			}
		}
		#endif
		CPU[activeCPU].unaffectedRF = 1; //Default: affected!
		return; //Finished!
	}

	//Use protected mode IRET?
	if (FLAG_V8) //Virtual 8086 mode?
	{
		//According to: http://x86.renejeschke.de/html/file_module_x86_id_145.html
		if (FLAG_PL==3) //IOPL==3? Processor is in virtual-8086 mode when IRET is executed and stays in virtual-8086 mode
		{
			if (CPU[activeCPU].CPU_Operand_size) //32-bit operand size?
			{
				if (checkStackAccess(3,0,1)) return; //3 DWord POPs!
				CPU[activeCPU].destEIP = CPU_POP32();
				tempCS = (CPU_POP32()&0xFFFF);
				tempEFLAGS = CPU_POP32();
				if (segmentWritten(CPU_SEGMENT_CS,tempCS,3)) return; //Jump to the CS, IRET style!
				//VM&IOPL aren't changed by the POP!
				tempEFLAGS = (tempEFLAGS&~(0x23000|F_VIF|F_VIP))|(REG_EFLAGS&(0x23000|F_VIF|F_VIP)); //Don't modfiy changed flags that we're not allowed to!
				REG_EFLAGS = tempEFLAGS; //Restore EFLAGS!
				updateCPUmode();
			}
			else //16-bit operand size?
			{
				if (checkStackAccess(3,0,0)) return; //3 Word POPs!
				CPU[activeCPU].destEIP = (uint_32)CPU_POP16(0);
				tempCS = CPU_POP16(0);
				tempEFLAGS = CPU_POP16(0);
				if (segmentWritten(CPU_SEGMENT_CS, tempCS, 3)) return; //Jump to the CS, IRET style!
				//VM&IOPL aren't changed by the POP!
				tempEFLAGS = (tempEFLAGS&~(0x23000|F_VIF|F_VIP))|(REG_EFLAGS&(0x23000|F_VIF|F_VIP)); //Don't modfiy changed flags that we're not allowed to!
				REG_FLAGS = tempEFLAGS; //Restore FLAGS, leave high DWord unmodified(VM, IOPL, VIP and VIF are unmodified, only bits 0-15)!
			}
			CPU[activeCPU].unaffectedRF = 1; //Default: affected!
		}
		else //PL!=3?
		{
			if (((CPU[activeCPU].registers->CR4 & 1) && (EMULATED_CPU>=CPU_PENTIUM)) && (CPU[activeCPU].CPU_Operand_size==0)) //Pentium with VME enabled, executing 16-bit IRET?
			{
				//Execute 16-bit POP normally
				if (checkStackAccess(3, 0, 0)) return; //3 Word POPs!
				CPU[activeCPU].destEIP = (uint_32)CPU_POP16(0);
				tempCS = CPU_POP16(0);
				tempEFLAGS = CPU_POP16(0);
				if (segmentWritten(CPU_SEGMENT_CS, tempCS, 3)) return; //Jump to the CS, IRET style!
				//VM&IOPL aren't changed by the POP!
				if (tempEFLAGS&F_TF) //Trap flag set?
				{
					THROWDESCGP(0, 0, 0); //Throw #GP(0) to trap to the VM monitor!
				}
				else //VIF from IF!
				{
					FLAGW_VIF((tempEFLAGS&F_IF) ? 1 : 0); //Virtual interrupt flag from Interrupt flag on the stack!
				}
				tempEFLAGS = (tempEFLAGS&~(0x23000 | F_VIF | F_VIP | F_IF)) | (REG_EFLAGS&(0x23000 | F_VIF | F_VIP | F_IF)); //Don't modfiy changed flags that we're not allowed to!
				if (FLAG_VIP && FLAG_VIF) //Virtual interrupt flag has been set by software while pending?
				{
					THROWDESCGP(0, 0, 0); //Throw #GP(0) to trap to the VM monitor!
				}
				else //Finish normally!
				{
					REG_FLAGS = tempEFLAGS; //Restore FLAGS, leave high DWord unmodified(VM, IOPL, VIP and VIF are unmodified, only bits 0-15)!
				}
				CPU[activeCPU].unaffectedRF = 1; //Default: affected!
			}
			else //Normal handling?
			{
				THROWDESCGP(0, 0, 0); //Throw #GP(0) to trap to the VM monitor!
			}
		}
		return; //Finished!
	}

	//NT flag is set? If so, perform a task switch back to the task we're nested in(undocumented)! http://nicolascormier.com/documentation/hardware/microprocessors/intel/i80386/Chap15.html 
	if (FLAG_NT && (getcpumode() != CPU_MODE_REAL)) //Protected mode Nested Task IRET?
	{
		SEGMENT_DESCRIPTOR newdescriptor; //Temporary storage!
		word desttask;
		sbyte loadresult;
		if (checkMMUaccess16(CPU_SEGMENT_TR, REG_TR, 0, 1 | 0x40, 0, 0, 0)) return; //Error out!
		if (checkMMUaccess16(CPU_SEGMENT_TR, REG_TR, 0, 1 | 0xA0, 0, 0, 0)) return; //Error out!
		desttask = MMU_rw(CPU_SEGMENT_TR, REG_TR, 0, 0,0); //Read the destination task!
		if ((loadresult = LOADDESCRIPTOR(CPU_SEGMENT_TR, desttask, &newdescriptor,3))<=0) //Error loading new descriptor? The backlink is always at the start of the TSS!
		{
			if ((loadresult == -1) || (loadresult==-2)) return; //Abort on page fault!
			CPU_TSSFault(desttask,0,(desttask&4)?EXCEPTION_TABLE_LDT:EXCEPTION_TABLE_GDT); //Throw error!
			return; //Error, by specified reason!
		}
		CPU_executionphase_starttaskswitch(CPU_SEGMENT_TR,&newdescriptor,&REG_TR,desttask,3,0,-1); //Execute an IRET to the interrupted task!
		return; //Finished!
	}

	//Normal protected mode IRET?
	uint_32 tempesp;
	if (CPU[activeCPU].CPU_Operand_size) //32-bit?
	{
		if (checkStackAccess(3,0,1)) return; //Top 12 bytes!
	}
	else //16-bit?
	{
		if (checkStackAccess(3,0,0)) return; //Top 6 bytes!
	}
			
	if (CPU[activeCPU].CPU_Operand_size) //32-bit mode?
	{
		CPU[activeCPU].destEIP = CPU_POP32(); //POP EIP!
	}
	else
	{
		CPU[activeCPU].destEIP = (uint_32)CPU_POP16(0); //POP IP!
	}
	tempCS = CPU_POP16(CPU[activeCPU].CPU_Operand_size); //CS to be loaded!
	if (CPU[activeCPU].CPU_Operand_size) //32-bit mode?
	{
		tempEFLAGS = CPU_POP32(); //Pop eflags!
	}
	else
	{
		tempEFLAGS = (uint_32)CPU_POP16(0); //Pop flags!
	}

	if ((tempEFLAGS&0x20000) && (!oldCPL)) //Returning to virtual 8086 mode?
	{
		if (checkStackAccess(6,0,1)) return; //First level IRET data?
		tempesp = CPU_POP32(); //POP ESP!
		tempSS = (CPU_POP32()&0xFFFF); //POP SS!
		for (V86SegReg=0;V86SegReg<NUMITEMS(V86SegRegs);)//POP required remaining registers into buffers first!
		{
			V86SegRegs[V86SegReg++] = (CPU_POP32()&0xFFFF); //POP segment register! Throw away high word!
		}
		REG_EFLAGS = tempEFLAGS; //Set EFLAGS to the tempEFLAGS
		updateCPUmode(); //Update the CPU mode to return to Virtual 8086 mode!
		//Load POPped registers into the segment registers, CS:EIP and SS:ESP in V86 mode(raises no faults) to restore the task.
		if (segmentWritten(CPU_SEGMENT_CS,tempCS,3)) return; //We're loading because of an IRET!
		if (segmentWritten(CPU_SEGMENT_SS,tempSS,0)) return; //Load SS!
		REG_ESP = tempesp; //Set the new ESP of the V86 task!
		if (segmentWritten(CPU_SEGMENT_ES,V86SegRegs[0],0)) return; //Load ES!
		if (segmentWritten(CPU_SEGMENT_DS,V86SegRegs[1],0)) return; //Load DS!
		if (segmentWritten(CPU_SEGMENT_FS, V86SegRegs[2], 0)) return; //Load FS!
		if (segmentWritten(CPU_SEGMENT_GS,V86SegRegs[3],0)) return; //Load GS!
		CPU[activeCPU].unaffectedRF = 1; //Default: affected!
	}
	else //Normal protected mode return?
	{
		if (CPU[activeCPU].CPU_Operand_size==0) tempEFLAGS |= (REG_EFLAGS&0xFFFF0000); //Pop flags only, not EFLAGS!
		//Check unchanging bits!
		tempEFLAGS = (tempEFLAGS&~F_V8)|(REG_EFLAGS&F_V8); //When returning to a V86-mode task from a non-PL0 handler, the VM flag isn't updated, so it stays in protected mode!
		if (getCPL())
		{
			tempEFLAGS = (tempEFLAGS&~F_IOPL) | (REG_EFLAGS&F_IOPL); //Disallow IOPL being changed!
			if (EMULATED_CPU >= CPU_PENTIUM) //VIP and VIF as well are protected?
			{
				tempEFLAGS = (tempEFLAGS&~(F_VIP|F_VIF)) | (REG_EFLAGS&(F_VIP|F_VIF)); //Disallow VIP&VIF being changed!
			}
		}
		if (getCPL()>FLAG_PL) tempEFLAGS = (tempEFLAGS&~F_IF)|(REG_EFLAGS&F_IF); //Disallow IF being changed!

		//Flags are OK now!
		REG_EFLAGS = tempEFLAGS; //Restore EFLAGS normally.
		updateCPUmode();
		if (segmentWritten(CPU_SEGMENT_CS,tempCS,3)) return; //We're loading because of an IRET!
		CPU[activeCPU].unaffectedRF = 1; //Default: affected!
		if (CPU_condflushPIQ(-1)) //We're jumping to another address!
		{
			return;
		}
		if ((tempEFLAGS&(F_VIP | F_VIF)) == (F_VIP | F_VIF)) //VIP and VIF both set on the new code?
		{
			CPU_commitState(); //Commit to the new instruction!
			THROWDESCGP(0, 0, 0); //#GP(0)!
		}
	}
}

extern byte SystemControlPortA; //System control port A data!
extern byte SystemControlPortB; //System control port B data!
extern byte PPI62; //For XT support!
byte NMI = 1; //NMI Disabled?

byte NMIQueued = 0; //NMI raised to handle? This can be handled by an APIC!
byte APICNMIQueued[MAXCPUS] = { 0, 0 }; //APIC-issued NMI queued?

void CPU_INTERNAL_execNMI()
{
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger", "#NMI fault(-1)!");
	}

	if (CPU_faultraised(EXCEPTION_NMI)) //OK to trigger the NMI exception?
	{
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
		call_hard_inthandler(EXCEPTION_NMI); //Trigger the hardware interrupt-style NMI!
	}
	CPU[activeCPU].cycles_HWOP = 50; /* Normal interrupt as hardware interrupt */
}

byte CPU_checkNMIAPIC(byte isHLT)
{
	if (APICNMIQueued[activeCPU]) //APIC NMI queued?
	{
		if (isHLT) return 0; //Leave HLT first!
		APICNMIQueued[activeCPU] = 0; //Not queued anymore!
		CPU_INTERNAL_execNMI(); //Start it up!
		return 0; //NNI handled!
	}
	return 1; //NMI not handled!
}

extern byte IMCR; //Address selected. 00h=Connect INTR and NMI to the CPU. 01h=Disconnect INTR and NMI from the CPU.
byte CPU_handleNMI(byte isHLT)
{
	if (((IMCR&1) == 0x01) || (CPU_NMI_APIC(activeCPU)==0)) //Not connected or available to handle directly?
	{
		return 1; //Don't perform the NMI as part of the NMI interrupt line!
	}

	if (NMIQueued == 0) return 1; //No NMI Pending!
	if (isHLT) return 0; //Leave HLT first!
	NMIQueued = 0; //Not anymore, we're handling it!

	CPU_INTERNAL_execNMI(); //Perform the NMI!

	return 0; //NMI handled!
}

extern byte is_Compaq; //Are we emulating an Compaq architecture?

byte execNMI(byte causeisMemory) //Execute an NMI!
{
	byte doNMI = 0; //Default: no NMI is to be triggered!
	if (causeisMemory) //I/O error on memory or failsafe timer?
	{
		if (!(((causeisMemory == 2) && is_Compaq) || ((causeisMemory != 2) && (!is_Compaq)))) //Not Fail safe timer for compaq or Memory for non-Compaq?
		{
			return 1; //Unhandled NMI!
		}
		if (EMULATED_CPU >= CPU_80286) //AT?
		{
			if ((SystemControlPortB & 4)==0) //Parity check enabled(the enable bits are reversed according to the AT BIOS)?
			{
				doNMI |= 0x80; //Signal a Memory error!
			}
		}
		else //XT?
		{
			if ((SystemControlPortB & 0x10)==0) //Enabled?
			{
				doNMI |= 0x80; //Signal a Memory error on a XT!
			}
		}
		#ifdef DISABLE_MEMNMI
			return 1; //We don't handle any NMI's from Bus or Memory through the NMI PIN!
		#endif
	}
	else //Cause is I/O?
	{
		//Bus error?
		if (EMULATED_CPU >= CPU_80286) //AT?
		{
			if ((SystemControlPortB & 8)==0) //Channel check enabled(the enable bits are reversed according to the AT BIOS)?
			{
				doNMI |= 0x40; //Signal a Bus error!
			}
		}
		else //XT?
		{
			if ((SystemControlPortB & 0x20)==0) //Parity check enabled?
			{
				doNMI |= 0x40; //Signal a Parity error on a XT!
			}
		}
	}

#ifdef DISABLE_NMI
	return 1; //We don't handle any NMI's from Bus or Memory through the NMI PIN!
#endif
	if (!(NMI| CPU[activeCPU].NMIMasked)) //NMI interrupt enabled and not masked off?
	{
		if (doNMI && (NMIQueued==0)) //I/O error on memory or bus and we can handle it(nothing is queued yet)?
		{
			CPU[activeCPU].NMIMasked = 1; //Mask future NMI!
			if (EMULATED_CPU >= CPU_80286) //AT?
			{
				SystemControlPortB |= doNMI; //Signal an error, AT-compatible style!
			}
			else //XT?
			{
				PPI62 |= doNMI; //Signal an error on a XT!
			}
			NMIQueued = 1; //Enqueue the NMI to be executed when the CPU is ready!
			return 0; //We're handled!
		}
	}
	return 1; //Unhandled NMI!
}
