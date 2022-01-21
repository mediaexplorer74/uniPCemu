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

#include "headers/cpu/modrm.h" //Need support!
#include "headers/cpu/cpu.h" //For the registers!
#include "headers/cpu/mmu.h" //For MMU!
#include "headers/cpu/easyregs.h" //Easy register compatibility!
#include "headers/support/signedness.h" //Basic CPU support (mainly for unsigned2signed)
#include "headers/cpu/protection.h" //Protection support!
#include "headers/support/zalloc.h" //memory and cpu register protection!
#include "headers/support/log.h" //For logging invalid registers!

//For CPU support:
#include "headers/cpu/modrm.h" //MODR/M (type) support!
#include "headers/emu/debugger/debugger.h" //debugging() functionality!
#include "headers/emu/gpu/gpu_text.h" //Text support!
#include "headers/cpu/biu.h" //BIU support!
#include "headers/cpu/protecteddebugging.h" //Protected mode debugger support!
#include "headers/cpu/cpu_opcodeinformation.h" //Current opcode information!

//Log invalid registers?
#define LOG_INVALID_REGISTERS 0

//First write 8-bits, 16-bits and 32-bits!

char modrm_sizes[4][256] = {"byte","word","dword","byte"}; //What size is used for the parameter?

extern byte advancedlog; //Advanced log setting

//whichregister: 1=R/M, other=register!
OPTINLINE byte modrm_useSIB(MODRM_PARAMS *params, int size) //Use SIB byte?
{
	if (CPU[activeCPU].CPU_Address_size) //32-bit mode?
	{
		if ((params->specialflags == 3) || (params->specialflags == 4) || (params->specialflags == 7)) return 0; //No SIB on register-only operands(forced to mode 3): REQUIRED FOR SOME OPCODES!!!
		if ((MODRM_RM(params->modrm) == 4) && (MODRM_MOD(params->modrm) != 3)) //Have a SIB byte?
		{
			return 1; //We use a SIB byte!
		}
	}
	return 0; //NO SIB!
}


OPTINLINE byte modrm_useDisplacement(MODRM_PARAMS *params, int size)
{
	/*
	Result:
	0: No displacement
	1: Byte displacement
	2: Word displacement
	4: DWord displacement
	*/

	if (((params->specialflags==3) || (params->specialflags==4) || (params->specialflags==7))) return 0; //No displacement on register-only operands(forced to mode 3): REQUIRED FOR SOME OPCODES!!!

	if (CPU[activeCPU].CPU_Address_size==0)   //16 bits operand size?
	{
		//figure out 16 bit displacement size
		switch (MODRM_MOD(params->modrm)) //MOD?
		{
		case 0:
			if (MODRM_RM(params->modrm) == MODRM_MEM_DISP16) //[sword]?
				return 2; //Word displacement!
			else
				return 0; //No displacement!
			break;
		case 1:
			return 1; //Byte displacement!
			break;
		case 2:
			return 2; //Word displacement!
			break;
		default: //Any register?
		case 3:
			return 0; //No displacement!
			break;
		}
	}
	else //32/64 bit operand size?
	{
		//figure out 32/64 bit displacement size
		switch (MODRM_MOD(params->modrm)) //MOD?
		{
		case 0:
			if ((MODRM_RM(params->modrm) == MODRM_MEM_DISP32) || ((MODRM_RM(params->modrm)==MODRM_MEM_SIB) && (SIB_BASE(params->SIB)==MODRM_SIB_DISP32))) //[dword] displacement?
				return 3; //DWord displacement!
			else
				return 0; //No displacement!
			break;
		case 1:
			return 1; //Byte displacement!
			break;
		case 2:
			return 3; //DWord displacement!
			break;
		default:
		case 3: //Any register?
			return 0; //No displacement!
			break;
		}
	}

	return 0; //Give displacement size in bytes (unknown)!
//Use displacement (1||2||4) else 0?
}

//Retrieves base offset to use

void modrm_updatedsegment(word *location, word value, byte isJMPorCALL) //Check for updated segment registers!
{
	//Check for updated registers!
	int index = get_segment_index(location); //Get the index!
	if (index!=-1) //Gotten?
	{
		if (segmentWritten(index,value,isJMPorCALL)) return; //Update when possible!
	}
	else //valid to write?
	{
		*location = value; //Write the value!
	}
}

void reset_modrm()
{
	CPU[activeCPU].params.EA_cycles = 0; //Default: no cycles used!
	CPU[activeCPU].params.notdecoded = 1; //Default: no ModR/M has been decoded!
}

void reset_modrmall()
{
	CPU[activeCPU].last_modrm = 0; //Last wasn't a modr/m anymore by default!
	CPU[activeCPU].modrm_addoffset = 0; //Add this offset to ModR/M reads: default to none!
}

void modrm_notdecoded(MODRM_PARAMS *params)
{
	dolog("modrm", "Not properly loaded and used with opcode: is32:%i 0F: %i OP:%02X R/M:%02X", CPU[activeCPU].CPU_Operand_size, CPU[activeCPU].is0Fopcode, CPU[activeCPU].currentopcode, params->modrm); //Log the invalid access!
}

byte modrm_check8(MODRM_PARAMS *params, int whichregister, byte isread)
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	uint_32 offset;
	switch (params->info[whichregister].isreg) //What type?
	{
	case 1: //Register?
		return 0; //OK!
		break;
	case 2: //Memory?
		CPU[activeCPU].last_modrm = 1; //ModR/M!
		offset = params->info[whichregister].mem_offset; //Get the base offset!
		if (!CPU[activeCPU].modrm_addoffset) //We're the offset itself?
		{
			CPU[activeCPU].modrm_lastsegment = params->info[whichregister].mem_segment;
			CPU[activeCPU].modrm_lastoffset = offset&params->info[whichregister].memorymask;
		}
		offset += CPU[activeCPU].modrm_addoffset; //Add to get the destination offset!
		return checkMMUaccess(params->info[whichregister].segmentregister_index, params->info[whichregister].mem_segment, offset&params->info[whichregister].memorymask,isread,getCPL(),(params->info[whichregister].is16bit),0); //Check the data to memory using byte depth!
		break;
	default:
		halt_modrm("MODRM: Unknown MODR/M8!");
		break;
	}
	return 0; //Ignore invalid!
}
void modrm_write8(MODRM_PARAMS *params, int whichregister, byte value)
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	byte *result; //The result holder if needed!
	uint_32 offset;
	switch (params->info[whichregister].isreg) //What type?
	{
	case 1: //Register?
		result = (byte *)/*memprotect(*/params->info[whichregister].reg8/*,1,"CPU_REGISTERS")*/; //Give register!
		if (result) //Gotten?
		{
			*result = value; //Write the data to the result!
		}
		else if (LOG_INVALID_REGISTERS && advancedlog)
		{
			dolog("debugger","Write to 8-bit register failed: not registered!");
		}
		break;
	case 2: //Memory?
		CPU[activeCPU].last_modrm = 1; //ModR/M!
		offset = params->info[whichregister].mem_offset; //Get the base offset!
		if (!CPU[activeCPU].modrm_addoffset) //We're the offset itself?
		{
			CPU[activeCPU].modrm_lastsegment = params->info[whichregister].mem_segment;
			CPU[activeCPU].modrm_lastoffset = offset&params->info[whichregister].memorymask;
		}
		offset += CPU[activeCPU].modrm_addoffset; //Add to get the destination offset!
		MMU_wb(params->info[whichregister].segmentregister_index, params->info[whichregister].mem_segment, offset&params->info[whichregister].memorymask,value,(params->info[whichregister].is16bit)); //Write the data to memory using byte depth!
		break;
	default:
		halt_modrm("MODRM: Unknown MODR/M8!");
		break;
	}
}

byte modrm_write8_BIU(MODRM_PARAMS *params, int whichregister, byte value)
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	byte *result; //The result holder if needed!
	uint_32 offset;
	switch (params->info[whichregister].isreg) //What type?
	{
	case 1: //Register?
		result = (byte *)/*memprotect(*/params->info[whichregister].reg8/*,1,"CPU_REGISTERS")*/; //Give register!
		if (result) //Gotten?
		{
			*result = value; //Write the data to the result!
		}
		else if (LOG_INVALID_REGISTERS && advancedlog)
		{
			dolog("debugger","Write to 8-bit register failed: not registered!");
		}
		return 2; //Ready!
		break;
	case 2: //Memory?
		CPU[activeCPU].last_modrm = 1; //ModR/M!
		offset = params->info[whichregister].mem_offset; //Get the base offset!
		if (!CPU[activeCPU].modrm_addoffset) //We're the offset itself?
		{
			CPU[activeCPU].modrm_lastsegment = params->info[whichregister].mem_segment;
			CPU[activeCPU].modrm_lastoffset = offset&params->info[whichregister].memorymask;
		}
		offset += CPU[activeCPU].modrm_addoffset; //Add to get the destination offset!
		return CPU_request_MMUwb(params->info[whichregister].segmentregister_index, offset&params->info[whichregister].memorymask,value,(params->info[whichregister].is16bit)); //Write the data to memory using byte depth!
		break;
	default:
		halt_modrm("MODRM: Unknown MODR/M8!");
		break;
	}
	return 0; //Not ready!
}

void modrm_write16(MODRM_PARAMS *params, int whichregister, word value, byte isJMPorCALL)
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	word *result; //The result holder if needed!
	uint_32 offset;
	switch (params->info[whichregister].isreg) //What type?
	{
	case 1: //Register?
		result = (word *)/*memprotect(*/params->info[whichregister].reg16/*,2,"CPU_REGISTERS")*/; //Give register!
		if (result) //Gotten?
		{
			CPU[activeCPU].destEIP = REG_EIP; //Our instruction pointer!
			modrm_updatedsegment(result,value,isJMPorCALL); //Plain update of the segment register, if needed!
		}
		else if (LOG_INVALID_REGISTERS && advancedlog)
		{
			dolog("debugger","Write to 16-bit register failed: not registered!");
		}
		break;
	case 2: //Memory?
		CPU[activeCPU].last_modrm = 1; //ModR/M!
		offset = params->info[whichregister].mem_offset;
		if (!CPU[activeCPU].modrm_addoffset) //We're the offset itself?
		{
			CPU[activeCPU].modrm_lastsegment = params->info[whichregister].mem_segment;
			CPU[activeCPU].modrm_lastoffset = offset&params->info[whichregister].memorymask;
		}
		offset += CPU[activeCPU].modrm_addoffset; //Add to get the destination offset!
		MMU_ww(params->info[whichregister].segmentregister_index, params->info[whichregister].mem_segment, offset&params->info[whichregister].memorymask, value,(params->info[whichregister].is16bit)); //Write the data to memory using byte depth!
		break;
	default:
		halt_modrm("MODRM: Unknown MODR/M16!");
		break;
	}	
}

byte modrm_write16_BIU(MODRM_PARAMS *params, int whichregister, word value, byte isJMPorCALL)
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	word *result; //The result holder if needed!
	uint_32 offset;
	switch (params->info[whichregister].isreg) //What type?
	{
	case 1: //Register?
		result = (word *)/*memprotect(*/params->info[whichregister].reg16/*,2,"CPU_REGISTERS")*/; //Give register!
		if (result) //Gotten?
		{
			CPU[activeCPU].destEIP = REG_EIP; //Our instruction pointer!
			modrm_updatedsegment(result,value,isJMPorCALL); //Plain update of the segment register, if needed!
		}
		else if (LOG_INVALID_REGISTERS && advancedlog)
		{
			dolog("debugger","Write to 16-bit register failed: not registered!");
		}
		return 2; //Ready!
		break;
	case 2: //Memory?
		CPU[activeCPU].last_modrm = 1; //ModR/M!
		offset = params->info[whichregister].mem_offset;
		if (!CPU[activeCPU].modrm_addoffset) //We're the offset itself?
		{
			CPU[activeCPU].modrm_lastsegment = params->info[whichregister].mem_segment;
			CPU[activeCPU].modrm_lastoffset = offset&params->info[whichregister].memorymask;
		}
		offset += CPU[activeCPU].modrm_addoffset; //Add to get the destination offset!
		return CPU_request_MMUww(params->info[whichregister].segmentregister_index, offset&params->info[whichregister].memorymask, value,(params->info[whichregister].is16bit)); //Write the data to memory using byte depth!
		break;
	default:
		halt_modrm("MODRM: Unknown MODR/M16!");
		break;
	}
	return 0; //Not ready!
}

byte modrm_check16(MODRM_PARAMS *params, int whichregister, byte isread)
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	uint_32 offset;
	switch (params->info[whichregister].isreg) //What type?
	{
	case 1: //Register?
		return 0; //OK!
		break;
	case 2: //Memory?
		CPU[activeCPU].last_modrm = 1; //ModR/M!
		offset = params->info[whichregister].mem_offset;
		if (!CPU[activeCPU].modrm_addoffset) //We're the offset itself?
		{
			CPU[activeCPU].modrm_lastsegment = params->info[whichregister].mem_segment;
			CPU[activeCPU].modrm_lastoffset = offset&params->info[whichregister].memorymask;
		}
		offset += CPU[activeCPU].modrm_addoffset; //Add to get the destination offset!
		return checkMMUaccess16(params->info[whichregister].segmentregister_index, params->info[whichregister].mem_segment, offset&params->info[whichregister].memorymask,isread,getCPL(),(params->info[whichregister].is16bit),0|0x8); //Check the data to memory using byte depth!
		break;
	default:
		halt_modrm("MODRM: Unknown MODR/M16!");
		break;
	}	
	return 0; //Ignore invalid!
}

byte modrm_check32(MODRM_PARAMS *params, int whichregister, byte isread)
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	uint_32 offset;
	switch (params->info[whichregister].isreg) //What type?
	{
	case 1: //Register?
		return 0; //OK!
		break;
	case 2: //Memory?
		CPU[activeCPU].last_modrm = 1; //ModR/M!
		offset = params->info[whichregister].mem_offset; //Load the base offset!
		if (!CPU[activeCPU].modrm_addoffset) //We're the offset itself?
		{
			CPU[activeCPU].modrm_lastsegment = params->info[whichregister].mem_segment;
			CPU[activeCPU].modrm_lastoffset = offset&params->info[whichregister].memorymask;
		}
		offset += CPU[activeCPU].modrm_addoffset; //Add to get the destination offset!
		return checkMMUaccess32(params->info[whichregister].segmentregister_index, params->info[whichregister].mem_segment, offset&params->info[whichregister].memorymask,isread,getCPL(),(params->info[whichregister].is16bit),0|0x10); //Check the data to memory using byte depth!
		break;
	default:
		halt_modrm("MODRM: Unknown MODR/M32!");
		break;
	}
	return 0; //Ignore invalid!
}

extern byte CPU_databussize; //0=16/32-bit bus! 1=8-bit bus when possible (8088/80188)!

void CPU_writeCR0(uint_32 backupval, uint_32 value)
{
	if (((EMULATED_CPU == CPU_80386) && (CPU_databussize)) || (EMULATED_CPU >= CPU_80486)) //16-bit data bus on 80386? 80386SX hardwires ET to 1! Both 80486SX and DX hardwire ET to 1!
	{
		value |= CR0_ET; //ET is hardwired to 1 on a 80386SX/CX/EX/CL.
	}
	if (EMULATED_CPU >= CPU_80486) //80486+?
	{
		value &= 0xE005003F; //Only defined bits!
		value |= 0x10; //Stuck bits!
	}
	else if (EMULATED_CPU >= CPU_80386) //80386+?
	{
		value &= 0x8000001F; //Only defined bits!
	}
	backupval ^= value; //Check for changes!
	CPU[activeCPU].registers->CR0 = value; //Set!
	if (backupval & 0x80010001) //Paging changed?
	{
		Paging_initTLB(); //Fully clear the TLB!
		CPU_flushPIQ(-1); //Flush the PIQ!
	}
	updateCPUmode(); //Try to update the CPU mode, if needed!
}

void modrm_write32(MODRM_PARAMS *params, int whichregister, uint_32 value)
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	uint_32 *result; //The result holder if needed!
	uint_32 backupval;
	uint_32 offset;
	switch (params->info[whichregister].isreg) //What type?
	{
	case 1: //Register?
		if (params->info[whichregister].is_segmentregister) //Are we a segment register?
		{
			if (params->info[whichregister].reg16) //Give register!
			{
				modrm_write16(params,whichregister,value,0); //Redirect to segment register instead!
				return;
			}
		}
		result = (uint_32 *)/*memprotect(*/params->info[whichregister].reg32/*,4,"CPU_REGISTERS")*/; //Give register!
		if (result) //Gotten?
		{
			backupval = *result;
			*result = value; //Write the data to the result!
			if (result==&CPU[activeCPU].registers->CR0) //CR0 has been updated? Update the CPU mode, if needed!
			{
				*result = backupval; //Restore!
				CPU_writeCR0(backupval, value); //Common CR0 handling!
			}
			else if (result==&CPU[activeCPU].registers->CR3) //CR3 has been updated?
			{
				Paging_clearTLB(); //Clear the TLB!
				CPU_flushPIQ(-1); //Flush the PIQ!
			}
			else if ((result == &CPU[activeCPU].registers->CR4) && //CR4 paging affected?
					(
						(((*result ^ backupval) & 0x10) && (EMULATED_CPU >= CPU_PENTIUM)) || //PSE changed when supported?
						(((*result ^ backupval) & 0xA0) && (EMULATED_CPU >= CPU_PENTIUMPRO)) //PAE/PGE changed when supported?
					)
				)
			{
				Paging_initTLB(); //Fully clear the TLB!
				CPU_flushPIQ(-1); //Flush the PIQ!
			}
			else if (result == &CPU[activeCPU].registers->DR7)
			{
				protectedModeDebugger_updateBreakpoints(); //Update the breakpoints to use!
			}
			else if (result==&CPU[activeCPU].registers->TR6) //TR6?
			{
				Paging_TestRegisterWritten(6); //We're updated!
			}
			else if (result==&CPU[activeCPU].registers->TR7) //TR7?
			{
				Paging_TestRegisterWritten(7); //We're updated!
			}
		}
		else if (LOG_INVALID_REGISTERS && advancedlog)
		{
			dolog("debugger","Write to 32-bit register failed: not registered!");
		}
		break;
	case 2: //Memory?
		CPU[activeCPU].last_modrm = 1; //ModR/M!
		offset = params->info[whichregister].mem_offset; //Load the base offset!
		if (!CPU[activeCPU].modrm_addoffset) //We're the offset itself?
		{
			CPU[activeCPU].modrm_lastsegment = params->info[whichregister].mem_segment;
			CPU[activeCPU].modrm_lastoffset = offset&params->info[whichregister].memorymask;
		}
		offset += CPU[activeCPU].modrm_addoffset; //Add to get the destination offset!
		MMU_wdw(params->info[whichregister].segmentregister_index, params->info[whichregister].mem_segment, offset&params->info[whichregister].memorymask,value,(params->info[whichregister].is16bit)); //Write the data to memory using byte depth!
		break;
	default:
		halt_modrm("MODRM: Unknown MODR/M32!");
		break;
	}
}

byte modrm_write32_BIU(MODRM_PARAMS *params, int whichregister, uint_32 value)
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	uint_32 *result; //The result holder if needed!
	uint_32 backupval;
	uint_32 offset;
	switch (params->info[whichregister].isreg) //What type?
	{
	case 1: //Register?
		if (params->info[whichregister].is_segmentregister) //Are we a segment register?
		{
			if (params->info[whichregister].reg16) //Give register!
			{
				return modrm_write16_BIU(params,whichregister,value,0); //Redirect to segment register instead!
			}
		}
		result = (uint_32 *)/*memprotect(*/params->info[whichregister].reg32/*,4,"CPU_REGISTERS")*/; //Give register!
		if (result) //Gotten?
		{
			backupval = *result;
			*result = value; //Write the data to the result!
			if (result==&CPU[activeCPU].registers->CR0) //CR0 has been updated? Update the CPU mode, if needed!
			{
				*result = backupval; //Restore!
				CPU_writeCR0(backupval, value); //Common CR0 handling!
			}
			else if (result == &CPU[activeCPU].registers->CR3) //CR3 has been updated?
			{
				Paging_clearTLB(); //Clear the TLB!
				CPU_flushPIQ(-1); //Flush the PIQ!
			}
			else if ((result == &CPU[activeCPU].registers->CR4) && //CR4 paging affected?
					(
						(((*result ^ backupval) & 0x10) && (EMULATED_CPU >= CPU_PENTIUM)) || //PSE changed when supported?
						(((*result ^ backupval) & 0xA0) && (EMULATED_CPU >= CPU_PENTIUMPRO)) //PAE/PGE changed when supported?
					)
				)
			{
				Paging_initTLB(); //Fully clear the TLB!
				CPU_flushPIQ(-1); //Flush the PIQ!
			}
			else if (result == &CPU[activeCPU].registers->DR7)
			{
				protectedModeDebugger_updateBreakpoints(); //Update the breakpoints to use!
			}
			else if (result==&CPU[activeCPU].registers->TR6) //TR6?
			{
				Paging_TestRegisterWritten(6); //We're updated!
			}
			else if (result == &CPU[activeCPU].registers->TR7) //TR7?
			{
				Paging_TestRegisterWritten(7); //We're updated!
			}
		}
		else if (LOG_INVALID_REGISTERS && advancedlog)
		{
			dolog("debugger","Write to 32-bit register failed: not registered!");
		}
		return 2; //Ready!
		break;
	case 2: //Memory?
		CPU[activeCPU].last_modrm = 1; //ModR/M!
		offset = params->info[whichregister].mem_offset; //Load the base offset!
		if (!CPU[activeCPU].modrm_addoffset) //We're the offset itself?
		{
			CPU[activeCPU].modrm_lastsegment = params->info[whichregister].mem_segment;
			CPU[activeCPU].modrm_lastoffset = offset&params->info[whichregister].memorymask;
		}
		offset += CPU[activeCPU].modrm_addoffset; //Add to get the destination offset!
		return CPU_request_MMUwdw(params->info[whichregister].segmentregister_index, offset&params->info[whichregister].memorymask,value,(params->info[whichregister].is16bit)); //Write the data to memory using byte depth!
		break;
	default:
		halt_modrm("MODRM: Unknown MODR/M32!");
		break;
	}
	return 0; //Not ready!
}

byte modrm_read8(MODRM_PARAMS *params, int whichregister)
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	byte *result; //The result holder if needed!
	uint_32 offset;
	switch (params->info[whichregister].isreg) //What type?
	{
	case 1: //Register?
		result = (byte *)/*memprotect(*/params->info[whichregister].reg8/*,1,"CPU_REGISTERS")*/; //Give register!
		if (result) //Valid?
		{
			return *result; //Read register!
		}
		else if (LOG_INVALID_REGISTERS && advancedlog)
		{
			dolog("debugger","Read from 8-bit register failed: not registered!");
		}
		break;
	case 2: //Memory?
		CPU[activeCPU].last_modrm = 1; //ModR/M!
		offset = params->info[whichregister].mem_offset; //Load the base offset!
		if (!CPU[activeCPU].modrm_addoffset) //We're the offset itself?
		{
			CPU[activeCPU].modrm_lastsegment = params->info[whichregister].mem_segment;
			CPU[activeCPU].modrm_lastoffset = offset&params->info[whichregister].memorymask;
		}
		offset += CPU[activeCPU].modrm_addoffset; //Add to get the destination offset!
		return MMU_rb(params->info[whichregister].segmentregister_index, params->info[whichregister].mem_segment, offset&params->info[whichregister].memorymask, 0,(params->info[whichregister].is16bit)); //Read the value from memory!
	default:
		halt_modrm("MODRM: Unknown MODR/M8!");
		return 0; //Unknown!
	}	
	return 0; //Default: unknown value!!
}

byte modrm_read8_BIU(MODRM_PARAMS *params, int whichregister, byte *result) //Returns: 0: Busy, 1=Finished request(memory to be read back from BIU), 2=Register written, no BIU to read a response from.
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	byte *resultsrc; //The result holder if needed!
	uint_32 offset;
	switch (params->info[whichregister].isreg) //What type?
	{
	case 1: //Register?
		resultsrc = (byte *)/*memprotect(*/params->info[whichregister].reg8/*,1,"CPU_REGISTERS")*/; //Give register!
		if (resultsrc) //Valid?
		{
			*result = *resultsrc; //Read register!
		}
		else if (LOG_INVALID_REGISTERS && advancedlog)
		{
			dolog("debugger","Read from 8-bit register failed: not registered!");
		}
		return 2; //We're a register!
		break;
	case 2: //Memory?
		CPU[activeCPU].last_modrm = 1; //ModR/M!
		offset = params->info[whichregister].mem_offset; //Load the base offset!
		if (!CPU[activeCPU].modrm_addoffset) //We're the offset itself?
		{
			CPU[activeCPU].modrm_lastsegment = params->info[whichregister].mem_segment;
			CPU[activeCPU].modrm_lastoffset = offset&params->info[whichregister].memorymask;
		}
		offset += CPU[activeCPU].modrm_addoffset; //Add to get the destination offset!
		return CPU_request_MMUrb(params->info[whichregister].segmentregister_index, offset&params->info[whichregister].memorymask,(params->info[whichregister].is16bit)); //Read the value from memory!
	default:
		halt_modrm("MODRM: Unknown MODR/M8!");
		return 0; //Unknown!
	}	
	return 0; //Default: unknown value!!
}

word modrm_read16(MODRM_PARAMS *params, int whichregister)
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	word *result; //The result holder if needed!
	uint_32 offset;
	switch (params->info[whichregister].isreg) //What type?
	{
	case 1: //Register?
		result = (word *)/*memprotect(*/params->info[whichregister].reg16/*,2,"CPU_REGISTERS")*/; //Give register!
		if (result) //Valid?
		{
			return *result; //Read register!
		}
		else if (LOG_INVALID_REGISTERS && advancedlog)
		{
			dolog("debugger","Read from 16-bit register failed: not registered!");
		}
		break;
	case 2: //Memory?
		CPU[activeCPU].last_modrm = 1; //ModR/M!
		offset = params->info[whichregister].mem_offset; //Load base offset!
		if (!CPU[activeCPU].modrm_addoffset) //We're the offset itself?
		{
			CPU[activeCPU].modrm_lastsegment = params->info[whichregister].mem_segment;
			CPU[activeCPU].modrm_lastoffset = offset&params->info[whichregister].memorymask;
		}
		offset += CPU[activeCPU].modrm_addoffset; //Add to get the destination offset!
		return MMU_rw(params->info[whichregister].segmentregister_index, params->info[whichregister].mem_segment, offset&params->info[whichregister].memorymask, 0,(params->info[whichregister].is16bit)); //Read the value from memory!
		
	default:
		halt_modrm("MODRM: Unknown MODR/M16!");
		return 0; //Unknown!
	}
	
	return 0; //Default: not reached!
}

byte modrm_read16_BIU(MODRM_PARAMS *params, int whichregister, word *result) //Returns: 0: Busy, 1=Finished request(memory to be read back from BIU), 2=Register written, no BIU to read a response from.
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	word *resultsrc; //The result holder if needed!
	uint_32 offset;
	switch (params->info[whichregister].isreg) //What type?
	{
	case 1: //Register?
		resultsrc = (word *)/*memprotect(*/params->info[whichregister].reg16/*,2,"CPU_REGISTERS")*/; //Give register!
		if (resultsrc && result) //Valid?
		{
			*result = *resultsrc; //Read register!
		}
		else if (LOG_INVALID_REGISTERS && advancedlog)
		{
			dolog("debugger","Read from 16-bit register failed: not registered!");
		}
		return 2; //We're a register!
		break;
	case 2: //Memory?
		CPU[activeCPU].last_modrm = 1; //ModR/M!
		offset = params->info[whichregister].mem_offset; //Load base offset!
		if (!CPU[activeCPU].modrm_addoffset) //We're the offset itself?
		{
			CPU[activeCPU].modrm_lastsegment = params->info[whichregister].mem_segment;
			CPU[activeCPU].modrm_lastoffset = offset&params->info[whichregister].memorymask;
		}
		offset += CPU[activeCPU].modrm_addoffset; //Add to get the destination offset!
		return CPU_request_MMUrw(params->info[whichregister].segmentregister_index, offset&params->info[whichregister].memorymask, (params->info[whichregister].is16bit)); //Read the value from memory!
		
	default:
		halt_modrm("MODRM: Unknown MODR/M16!");
		return 0; //Unknown!
	}
	
	return 0; //Default: not reached!
}

uint_32 modrm_read32(MODRM_PARAMS *params, int whichregister)
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	uint_32 *result; //The result holder if needed!
	uint_32 offset;
	switch (params->info[whichregister].isreg) //What type?
	{
	case 1: //Register?
		if (params->info[whichregister].is_segmentregister) //Are we a segment register?
		{
			if (params->info[whichregister].reg16) //Give register!
			{
				return modrm_read16(params,whichregister); //Redirect to segment register instead!
			}
		}
		result = (uint_32 *)/*memprotect(*/params->info[whichregister].reg32/*,4,"CPU_REGISTERS")*/; //Give register!
		if (result) //Valid?
		{
			return *result; //Read register!
		}
		else if (LOG_INVALID_REGISTERS && advancedlog)
		{
			dolog("debugger","Read from 32-bit register failed: not registered!");
		}
		break;
	case 2: //Memory?
		CPU[activeCPU].last_modrm = 1; //ModR/M!
		offset = params->info[whichregister].mem_offset; //Load base offset!
		if (!CPU[activeCPU].modrm_addoffset) //We're the offset itself?
		{
			CPU[activeCPU].modrm_lastsegment = params->info[whichregister].mem_segment;
			CPU[activeCPU].modrm_lastoffset = offset&params->info[whichregister].memorymask;
		}
		offset += CPU[activeCPU].modrm_addoffset; //Add the destination offset!
		return MMU_rdw(params->info[whichregister].segmentregister_index, params->info[whichregister].mem_segment,offset&params->info[whichregister].memorymask, 0,(params->info[whichregister].is16bit)); //Read the value from memory!
		
	default:
		halt_modrm("MODRM: Unknown MODR/M32!");
		return 0; //Unknown!
	}	
	
	return 0; //Default: not reached!
}

byte modrm_read32_BIU(MODRM_PARAMS *params, int whichregister, uint_32 *result) //Returns: 0: Busy, 1=Finished request(memory to be read back from BIU), 2=Register written, no BIU to read a response from.
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	uint_32 *resultsrc; //The result holder if needed!
	uint_32 offset;
	switch (params->info[whichregister].isreg) //What type?
	{
	case 1: //Register?
		if (params->info[whichregister].is_segmentregister) //Are we a segment register?
		{
			if (params->info[whichregister].reg16) //Give register!
			{
				*result = 0; //Clear the upper half!
				if (modrm_read16_BIU(params,whichregister,(word *)result)) //Redirect to segment register instead!
				{
					return 2; //We're a register!
				}
				return 2; //Nothing to be done?
			}
		}
		resultsrc = (uint_32 *)/*memprotect(*/params->info[whichregister].reg32/*,4,"CPU_REGISTERS")*/; //Give register!
		if (resultsrc) //Valid?
		{
			*result = *resultsrc; //Read register!
		}
		else if (LOG_INVALID_REGISTERS && advancedlog)
		{
			dolog("debugger","Read from 32-bit register failed: not registered!");
		}
		return 2; //We're a register!
		break;
	case 2: //Memory?
		CPU[activeCPU].last_modrm = 1; //ModR/M!
		offset = params->info[whichregister].mem_offset; //Load base offset!
		if (!CPU[activeCPU].modrm_addoffset) //We're the offset itself?
		{
			CPU[activeCPU].modrm_lastsegment = params->info[whichregister].mem_segment;
			CPU[activeCPU].modrm_lastoffset = offset&params->info[whichregister].memorymask;
		}
		offset += CPU[activeCPU].modrm_addoffset; //Add the destination offset!
		return CPU_request_MMUrdw(params->info[whichregister].segmentregister_index, offset&params->info[whichregister].memorymask, (params->info[whichregister].is16bit)); //Read the value from memory!
		
	default:
		halt_modrm("MODRM: Unknown MODR/M32!");
		return 0; //Unknown!
	}	
	
	return 0; //Default: not reached!
}

uint_32 dummy_ptr; //Dummy pointer!
word dummy_ptr16; //16-bit dummy ptr!

//Simple adressing functions:

//Conversion to signed text:

//Our decoders:

OPTINLINE void modrm_get_segmentregister(byte reg, MODRM_PTR *result) //REG1/2 is segment register!
{
	result->isreg = 1; //Register!
	result->regsize = 1; //Word register!
	if (EMULATED_CPU==CPU_8086) //808X?
	{
		reg &= 0x3; //Incomplete decoding on 808X!
	}
	switch (reg) //What segment register?
	{
	case MODRM_SEG_ES:
		result->reg16 = CPU[activeCPU].SEGMENT_REGISTERS[CPU_SEGMENT_ES];
		if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"ES");
		result->is_segmentregister = 1; //We're a segment register!
		break;
	case MODRM_SEG_CS:
		result->reg16 = CPU[activeCPU].SEGMENT_REGISTERS[CPU_SEGMENT_CS];
		if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"CS");
		result->is_segmentregister = 1; //We're a segment register!
		break;
	case MODRM_SEG_SS:
		result->reg16 = CPU[activeCPU].SEGMENT_REGISTERS[CPU_SEGMENT_SS];
		if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"SS");
		result->is_segmentregister = 1; //We're a segment register!
		break;
	case MODRM_SEG_DS:
		result->reg16 = CPU[activeCPU].SEGMENT_REGISTERS[CPU_SEGMENT_DS];
		if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"DS");
		result->is_segmentregister = 1; //We're a segment register!
		break;
	case MODRM_SEG_FS:
		if (EMULATED_CPU<CPU_80386) goto unkseg; //Unsupported on 80(1)86!
		result->reg16 = CPU[activeCPU].SEGMENT_REGISTERS[CPU_SEGMENT_FS];
		if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"FS");
		result->is_segmentregister = 1; //We're a segment register!
		break;
	case MODRM_SEG_GS:
		if (EMULATED_CPU<CPU_80386) goto unkseg; //Unsupported on 80(1)86!
		result->reg16 = CPU[activeCPU].SEGMENT_REGISTERS[CPU_SEGMENT_GS];
		if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"GS");
		result->is_segmentregister = 1; //We're a segment register!
		break;

	default: //Catch handler!
		unkseg:
		result->reg16 = NULL; //Unknown!
		if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"<UNKSREG>");
		break;

	}
}

OPTINLINE void modrm_get_controlregister(byte reg, MODRM_PTR *result) //REG1/2 is segment register!
{
	result->isreg = 1; //Register!
	result->regsize = 2; //DWord register!
	switch (reg) //What control register?
	{
	case MODRM_REG_CR0:
		result->reg32 = &CPU[activeCPU].registers->CR0;
		if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"CR0");
		break;
	case MODRM_REG_CR2:
		result->reg32 = &CPU[activeCPU].registers->CR2;
		if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"CR2");
		break;
	case MODRM_REG_CR3:
		result->reg32 = &CPU[activeCPU].registers->CR3;
		if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"CR3");
		break;
	case MODRM_REG_CR4:
		if (EMULATED_CPU<CPU_80486) goto unkcreg; //Invalid: register 4 doesn't exist?
		result->reg32 = &CPU[activeCPU].registers->CR[4];
		if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"CR4");
		break;
	case MODRM_REG_CR1:
	case MODRM_REG_CR5:
	case MODRM_REG_CR6:
	case MODRM_REG_CR7:
		//CR1 and CR5-7 are undefined, raising an #UD!
	default: //Catch handler!
		unkcreg:
		result->reg32 = NULL; //Unknown!
		if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"<UNKCREG>");
		break;
	}
}

OPTINLINE void modrm_get_debugregister(byte reg, MODRM_PTR *result) //REG1/2 is segment register!
{
	result->isreg = 1; //Register!
	result->regsize = 2; //DWord register!
	switch (reg) //What debug register?
	{
	case MODRM_REG_DR0:
		result->reg32 = &CPU[activeCPU].registers->DR0;
		if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"DR0");
		break;
	case MODRM_REG_DR1:
		result->reg32 = &CPU[activeCPU].registers->DR1;
		if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"DR1");
		break;
	case MODRM_REG_DR2:
		result->reg32 = &CPU[activeCPU].registers->DR2;
		if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"DR2");
		break;
	case MODRM_REG_DR3:
		result->reg32 = &CPU[activeCPU].registers->DR3;
		if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"DR3");
		break;
	case MODRM_REG_DR4: //4 becomes 6 until Pentium, Pentium either redirects to 6(386-compatible) when the Debug Extension bit in CR4 is clear(Pentium) or when 80386/80486. Else CR4 raises #UD.
		if (EMULATED_CPU>=CPU_80486) //80486+? DR4 exists, with extra rules on redirecting!
		{
			if (CPU[activeCPU].registers->CR4&8) //Debugging extensions?
			{
				goto unkdreg; //Unknown DR-register, actually DR4!
			}
		}
		//Passthrough to DR6!
	case MODRM_REG_DR6:
		result->reg32 = &CPU[activeCPU].registers->DR6;
		if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"DR6");
		break;
	case MODRM_REG_DR5:
		if (EMULATED_CPU>=CPU_80486) //80486+? DR4 exists, with extra rules on redirecting!
		{
			if (CPU[activeCPU].registers->CR4&8) //Debugging extensions?
			{
				goto unkdreg; //Unknown DR-register, actually DR5!
			}
		}
		//Passthrough to DR7!
	case MODRM_REG_DR7:
		result->reg32 = &CPU[activeCPU].registers->DR7;
		if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"DR7");
		break;

	default: //Catch handler!
		unkdreg:
		result->reg32 = NULL; //Unknown!
		if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"<UNKDREG>");
		break;
	}
}

OPTINLINE void modrm_get_testregister(byte reg, MODRM_PTR *result) //REG1/2 is segment register!
{
	result->isreg = 1; //Register!
	result->regsize = 2; //DWord register!
	switch (reg) //What debug register?
	{
	case MODRM_REG_TR6:
		result->reg32 = &CPU[activeCPU].registers->TR6;
		if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"TR6");
		break;
	case MODRM_REG_TR7:
		result->reg32 = &CPU[activeCPU].registers->TR7;
		if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"TR7");
		break;
	case MODRM_REG_TR3:
		if (EMULATED_CPU == CPU_80486) //80486 registers?
		{
			result->reg32 = &CPU[activeCPU].registers->TR3;
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text, sizeof(result->text), "TR3");
			return;
		}
		goto invalidTR;
	case MODRM_REG_TR4:
		if (EMULATED_CPU == CPU_80486) //80486 registers?
		{
			result->reg32 = &CPU[activeCPU].registers->TR4;
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text, sizeof(result->text), "TR4");
			return;
		}
		goto invalidTR;
	case MODRM_REG_TR5:
		if (EMULATED_CPU == CPU_80486) //80486 registers?
		{
			result->reg32 = &CPU[activeCPU].registers->TR5;
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text, sizeof(result->text), "TR5");
			return;
		}
	case MODRM_REG_TR0:
	case MODRM_REG_TR1:
	case MODRM_REG_TR2:
	default: //Catch handler!
		invalidTR:
		result->reg32 = NULL; //Unknown!
		if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"<UNKTREG>");
		break;
	}
}

//Whichregister:
/*
0=reg1 (params->specialflags==0) or segment register (params->specialflags==2)
1+=reg2(r/m) as register (params->specialflags special cases) or memory address (RM value).
*/

//First the decoders:

//is_base: 1=Base, 0=Index
uint_32 modrm_SIB_reg(MODRM_PARAMS *params, byte reg, byte mod, uint_32 disp32, byte is_base, char *result,uint_32 resultsize, byte *useSS)
{
	uint_32 disprel=0;
	uint_32 effectivedisp;
	char format[6] = "+%04X";
	char textnr[18] = "";
	switch (mod) //What kind of parameter are we(signed vs unsigned, if used)?
	{
		case 0: //Mem? 32-bit size!
			disprel = disp32; //Same!
			effectivedisp = disprel; //Same!
			format[3] = '8'; //DWord!
			break;
		case 1: //8-bit? Needs sign-extending!
			disprel = (byte)disp32;
			effectivedisp = disprel|((disprel&0x80)?0xFFFFFF00ULL:0ULL); //Sign extended form!
			format[3] = '2'; //Byte!
			break;
		case 2: //32-bit?
			disprel = disp32; //Sign is included!
			effectivedisp = disprel; //Same!
			format[3] = '8'; //DWord!
			break;
		default: //Unknown?
			//Don't apply!
			effectivedisp = 0; //Nothing!
			break;
	}
	if (unlikely(CPU[activeCPU].cpudebugger))
	{
		if ((mod==1) && (disprel&0x80)) //Negative instead?
		{
			disprel = (byte)0-unsigned2signed8(disprel&0xFF); //Make positive!
			format[0] = '-'; //Substract instead!
		}
		snprintf(textnr,sizeof(textnr),format,disprel); //Text!
	}
	switch (reg)
	{
	case MODRM_REG_EAX:
		if (is_base==0) //We're the scaled index?
		{
			if (mod == 0) textnr[0] = '\0'; //No displacement on mod 0!
			if ((mod == MOD_MEM) && (SIB_BASE(params->SIB) != MODRM_REG_EBP)) { disprel = effectivedisp = 0; } //No displacement on mod 0 when using non-base only mode!
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result,resultsize,"+EAX*%i%s",(1<<SIB_SCALE(params->SIB)),textnr);
			return (REG_EAX<<SIB_SCALE(params->SIB))+effectivedisp;
		}
		else //Base?
		{
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result,resultsize,"EAX");
			return REG_EAX;
		}
		break;
	case MODRM_REG_EBX:
		if (is_base==0) //We're the scaled index?
		{
			if (mod == 0) textnr[0] = '\0'; //No displacement on mod 0!
			if ((mod == MOD_MEM) && (SIB_BASE(params->SIB) != MODRM_REG_EBP)) { disprel = effectivedisp = 0; } //No displacement on mod 0 when using non-base only mode!
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result,resultsize,"+EBX*%i%s",(1<<SIB_SCALE(params->SIB)),textnr);
			return (REG_EBX<<SIB_SCALE(params->SIB))+effectivedisp;
		}
		else //Base?
		{
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result,resultsize,"EBX");
			return REG_EBX;
		}
		break;
	case MODRM_REG_ECX:
		if (is_base==0) //We're the scaled index?
		{
			if (mod == 0) textnr[0] = '\0'; //No displacement on mod 0!
			if ((mod == MOD_MEM) && (SIB_BASE(params->SIB) != MODRM_REG_EBP)) { disprel = effectivedisp = 0; } //No displacement on mod 0 when using non-base only mode!
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result,resultsize,"+ECX*%i%s",(1<<SIB_SCALE(params->SIB)),textnr);
			return (REG_ECX<<SIB_SCALE(params->SIB))+effectivedisp;
		}
		else //Base?
		{
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result,resultsize,"ECX");
			return REG_ECX;
		}
		break;
	case MODRM_REG_EDX:
		if (is_base==0) //We're the scaled index?
		{
			if (mod == 0) textnr[0] = '\0'; //No displacement on mod 0!
			if ((mod == MOD_MEM) && (SIB_BASE(params->SIB) != MODRM_REG_EBP)) { disprel = effectivedisp = 0; } //No displacement on mod 0 when using non-base only mode!
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result,resultsize,"+EDX*%i%s",(1<<SIB_SCALE(params->SIB)),textnr);
			return (REG_EDX<<SIB_SCALE(params->SIB))+effectivedisp;
		}
		else //Base?
		{
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result,resultsize,"EDX");
			return REG_EDX;
		}
		break;
	case MODRM_REG_ESP: //none/ESP(base), depending on base/index.
		if (is_base==0) //We're the scaled index?
		{
			if (mod == 0) textnr[0] = '\0'; //No displacement on mod 0!
			if ((mod == MOD_MEM) && (SIB_BASE(params->SIB) != MODRM_REG_EBP)) { disprel = effectivedisp = 0; } //No displacement on mod 0 when using non-base only mode!
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result,resultsize,"%s",textnr); //None, according to http://www.sandpile.org/x86/opc_sib.htm !
			return effectivedisp; //REG+DISP. No reg when ESP!
		}
		else //Base?
		{
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result,resultsize,"ESP");
			*useSS = 1; //Use SS default!
			return REG_ESP;
		}
		break; //SIB doesn't have ESP as a scaled index: it's none!
	case MODRM_REG_EBP:
		if (is_base==0) //We're the scaled index?
		{
			if (mod == 0) textnr[0] = '\0'; //No displacement on mod 0!
			if ((mod == MOD_MEM) && (SIB_BASE(params->SIB) != MODRM_REG_EBP)) { disprel = effectivedisp = 0; } //No displacement on mod 0 when using non-base only mode!
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result,resultsize,"+EBP*%i%s",(1<<SIB_SCALE(params->SIB)),textnr);
			//*useSS = 1; //Use SS default! Only with (E)SP/(E)BP base!
			return (REG_EBP<<SIB_SCALE(params->SIB))+effectivedisp;
		}
		else //Base?
		{
			if (mod==MOD_MEM) //We're disp32 instead?
			{
				if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result,resultsize,"%08" SPRINTF_X_UINT32,disp32);
				return 0; //Disp32 instead! This is handled by the scaled index only! This way, we prevent it from doubling the value instead of only a single time.
			}
			else //EBP!
			{
				if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result,resultsize,"EBP");
				*useSS = 1; //Use SS default!
				return REG_EBP;
			}
		}
		break;
	case MODRM_REG_ESI:
		if (is_base==0) //We're the scaled index?
		{
			if (mod == 0) textnr[0] = '\0'; //No displacement on mod 0!
			if ((mod == MOD_MEM) && (SIB_BASE(params->SIB) != MODRM_REG_EBP)) { disprel = effectivedisp = 0; } //No displacement on mod 0 when using non-base only mode!
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result,resultsize,"+ESI*%i%s",(1<<SIB_SCALE(params->SIB)),textnr);
			return (REG_ESI<<SIB_SCALE(params->SIB))+effectivedisp;
		}
		else //Index? Entry exists!
		{
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result,resultsize,"ESI");
			return REG_ESI;
		}
		break;
	case MODRM_REG_EDI:
		if (is_base==0) //We're the scaled index?
		{
			if (mod == 0) textnr[0] = '\0'; //No displacement on mod 0!
			if ((mod == MOD_MEM) && (SIB_BASE(params->SIB) != MODRM_REG_EBP)) { disprel = effectivedisp = 0; } //No displacement on mod 0 when using non-base only mode!
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result,resultsize,"+EDI*%i%s",(1<<SIB_SCALE(params->SIB)),textnr);
			return (REG_EDI<<SIB_SCALE(params->SIB))+effectivedisp;
		}
		else //Index? Entry exists!
		{
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result,resultsize,"EDI");
			return REG_EDI;
		}
		break;
	default:
		break;
	}
	halt_modrm("Unknown modr/mSIB: reg: %u",reg);
	return 0; //Unknown register!
}

//Prototype for 32-bit decoding redirecting to 16-bit!
void modrm_decode8(MODRM_PARAMS *params, MODRM_PTR *result, byte whichregister); //8-bit address/reg decoder!
void modrm_decode16(MODRM_PARAMS *params, MODRM_PTR *result, byte whichregister); //16-bit address/reg decoder!

void modrm_decode32(MODRM_PARAMS *params, MODRM_PTR *result, byte whichregister) //32-bit address/reg decoder!
{
	INLINEREGISTER byte curreg;
	INLINEREGISTER byte reg; //What register?
	INLINEREGISTER byte isregister;
	char textnr[17] = "";

	if (whichregister) //reg2?
	{
		reg = MODRM_RM(params->modrm); //Take reg2!
		curreg = 2; //reg2!
	}
	else //1 or default (unknown)?
	{
		reg = MODRM_REG(params->modrm); //Default/reg1!
		curreg = 1; //reg1!
	}

	isregister = 0; //Init!
	if (!whichregister) //REG1?
	{
		isregister = 1; //Register!
	}
	else if (MODRM_MOD(params->modrm)==MOD_REG) //Register R/M?
	{
		isregister = 1; //Register!
	}
	else //No register?
	{
		isregister = 0; //No register (R/M=Memory Address)!
	}

	memset(result,0,sizeof(*result)); //Init!
	result->is16bit = 0; //32-bit offset by default!

	if (params->reg_is_segmentregister && (!whichregister)) //Segment register?
	{
		modrm_get_segmentregister(reg,result); //Return segment register!
		return; //Give the segment register!
	}

	if (params->specialflags==3) //Reg is CR, R/M is General Purpose Register?
	{
		if (curreg==1) //CR?
		{
			modrm_get_controlregister(reg,result); //Return CR register!
			return; //Give the segment register!
		}
		else
		{
			isregister = 1; //We're a General Purpose register!
		}
	}
	else if (params->specialflags==4) //Reg is DR? R/M is General Purpose register?
	{
		if (curreg==1) //DR?
		{
			modrm_get_debugregister(reg,result); //Return CR register!
			return; //Give the segment register!
		}
		else
		{
			isregister = 1; //We're a General Purpose register!
		}
	}
	else if (params->specialflags==7) //Reg is TR? R/M is General Purpose register?
	{
		if (curreg==1) //TR?
		{
			modrm_get_testregister(reg,result); //Return TR register!
			return; //Give the segment register!
		}
		else
		{
			isregister = 1; //We're a General Purpose register!
		}
	}

	if ((params->specialflags==5) && ((curreg==2) && isregister)) //r/m is 8-bits instead?
	{
		modrm_decode8(params,result,whichregister); //Redirect to 8-bit register!
		return;
	}
	else if ((params->specialflags==6) && ((curreg==2) && isregister)) //r/m is 16-bits instead?
	{
		modrm_decode16(params,result,whichregister); //Redirect to 16-bit register!
		return;
	}

	if (isregister) //Is register data?
	{
		result->isreg = 1; //Is register!
		result->regsize = 2; //DWord register!
		switch (reg) //Which register?
		{
		case MODRM_REG_EAX: //AX?
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"EAX");
			result->reg32 = &REG_EAX; //Give addr!
			return;
			break;
		case MODRM_REG_EBX: //BX?
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"EBX");
			result->reg32 = &REG_EBX; //Give addr!
			return;
			break;
		case MODRM_REG_ECX: //CX?
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"ECX");
			result->reg32 = &REG_ECX; //Give addr!
			return;
			break;
		case MODRM_REG_EDX: //DX?
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"EDX");
			result->reg32 = &REG_EDX; //Give addr!
			return;
			break;
		case MODRM_REG_EBP: //BP?
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"EBP");
			result->reg32 = &REG_EBP; //Give addr!
			return;
			break;
		case MODRM_REG_ESP: //SP?
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"ESP");
			result->reg32 = &REG_ESP; //Give addr!
			return;
			break;
		case MODRM_REG_ESI: //SI?
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"ESI");
			result->reg32 = &REG_ESI; //Give addr!
			return;
			break;
		case MODRM_REG_EDI: //DI?
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"EDI");
			result->reg32 = &REG_EDI; //Give addr!
			return;
			break;
		default:
			break;
		} //register?

		halt_modrm("Unknown modr/m16REG: MOD:%u, REG: %u, operand size: %u", MODRM_MOD(params->modrm), reg, CPU[activeCPU].CPU_Operand_size);
	}

	uint_32 index; //Going to contain the values for SIB!
	uint_32 base; //Going to contain the values for SIB!
	char indexstr[256]; //Index!
	char basestr[256]; //Base!
	byte useSS;
	useSS = 0; //Default: use DS!

	//Determine R/M (reg2=>RM) pointer!

	if (CPU[activeCPU].CPU_Address_size==0) //We need to decode as a 16-bit pointer instead?
	{
		modrm_decode16(params,result,whichregister); //Decode 16-bit pointer!
		return; //Don't decode ourselves!
	}

	result->isreg = 2; //Memory!
	result->memorymask = ~0; //No memory mask!
	result->is16bit = 0; //32-bit!

	switch (MODRM_MOD(params->modrm)) //Which mod?
	{
	case MOD_MEM: //[register]
		switch (reg) //Which register?
		{
		case MODRM_MEM_EAX: //[EAX] etc.?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[EAX]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS)); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			result->mem_offset = REG_EAX; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			break;
		case MODRM_MEM_EBX: //EBX?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[EBX]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS)); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			result->mem_offset = REG_EBX; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			break;
		case MODRM_MEM_ECX: //ECX
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[ECX]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS)); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			result->mem_offset = REG_ECX; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			break;
		case MODRM_MEM_EDX: //EDX
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[EDX]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS)); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			result->mem_offset = REG_EDX; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			break;
		case MODRM_MEM_ESI: //ESI
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[ESI]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS)); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			result->mem_offset = REG_ESI; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			break;
		case MODRM_MEM_EDI: //EDI
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[EDI]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS)); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			result->mem_offset = REG_EDI; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			break;
		case MODRM_MEM_SIB: //SIB?
			//SIB
			
			index = modrm_SIB_reg(params,SIB_INDEX(params->SIB),0,params->displacement.dword,0,&indexstr[0],sizeof(indexstr),&useSS);
			base = modrm_SIB_reg(params,SIB_BASE(params->SIB),0,params->displacement.dword,1,&basestr[0],sizeof(basestr),&useSS);

			if (unlikely(CPU[activeCPU].cpudebugger))
			{
				snprintf(result->text,sizeof(result->text),"%s %s:[%s%s]",modrm_sizes[params->size],CPU_textsegment(useSS?CPU_SEGMENT_SS:CPU_SEGMENT_DS),basestr,indexstr); //Give addr!
			}
			result->mem_segment = CPU_segment(useSS?CPU_SEGMENT_SS:CPU_SEGMENT_DS);
			result->mem_offset = base+index;
			result->segmentregister = CPU_segment_ptr(useSS?CPU_SEGMENT_SS:CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(useSS?CPU_SEGMENT_SS:CPU_SEGMENT_DS);
			break;
		case MODRM_MEM_DISP32: //EBP->32-bit Displacement-Only mode?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(textnr,sizeof(textnr),"%08" SPRINTF_X_UINT32,params->displacement.dword); //Text!
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS),textnr);
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			result->mem_offset = params->displacement.dword; //Give addr (Displacement Only)!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			break;
		default:
			halt_modrm("Unknown modr/m32(mem): MOD:%u, RM: %u, operand size: %u", MODRM_MOD(params->modrm), reg, CPU[activeCPU].CPU_Operand_size);
			break;
		}
		break;
	case MOD_MEM_DISP8: //[register+DISP8]
		if (unlikely(CPU[activeCPU].cpudebugger))
		{
			if (params->displacement.low16_low & 0x80) //Negative?
			{
				snprintf(textnr, sizeof(textnr), "-%02X", 0 - unsigned2signed8(params->displacement.low16_low)); //Text negative!
			}
			else
			{
				snprintf(textnr, sizeof(textnr), "+%02X", params->displacement.low16_low); //Text positive!
			}
		}
		switch (reg) //Which register?
		{
		case MODRM_MEM_EAX: //EAX?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[EAX%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			result->mem_offset = REG_EAX+unsigned2signed8(params->displacement.low16_low); //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			break;
		case MODRM_MEM_EBX: //EBX?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[EBX%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			result->mem_offset = REG_EBX+unsigned2signed8(params->displacement.low16_low); //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			break;
		case MODRM_MEM_ECX: //ECX?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[ECX%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			result->mem_offset = REG_ECX+unsigned2signed8(params->displacement.low16_low); //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			break;
		case MODRM_MEM_EDX: //EDX?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[EDX%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			result->mem_offset = REG_EDX+unsigned2signed8(params->displacement.low16_low); //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			break;
		case MODRM_MEM_ESI: //ESI?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[ESI%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			result->mem_offset = REG_ESI+unsigned2signed8(params->displacement.low16_low); //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			break;
		case MODRM_MEM_EDI: //EDI?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[EDI%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			result->mem_offset = REG_EDI+unsigned2signed8(params->displacement.low16_low); //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			break;
		case MODRM_MEM_SIB: //SIB/ESP?
			index = modrm_SIB_reg(params,SIB_INDEX(params->SIB),1,params->displacement.low16_low,0,&indexstr[0],sizeof(indexstr),&useSS);
			base = modrm_SIB_reg(params,SIB_BASE(params->SIB),1,params->displacement.low16_low,1,&basestr[0],sizeof(basestr),&useSS);

			if (unlikely(CPU[activeCPU].cpudebugger))
			{
				snprintf(result->text,sizeof(result->text),"%s %s:[%s%s]",modrm_sizes[params->size],CPU_textsegment(useSS?CPU_SEGMENT_SS:CPU_SEGMENT_DS),basestr,indexstr); //Give addr!
			}
			result->mem_segment = CPU_segment(useSS?CPU_SEGMENT_SS:CPU_SEGMENT_DS);
			result->mem_offset = base+index;
			result->segmentregister = CPU_segment_ptr(useSS?CPU_SEGMENT_SS:CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(useSS?CPU_SEGMENT_SS:CPU_SEGMENT_DS);
			break;
		case MODRM_MEM_EBP: //EBP?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[EBP%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_SS),textnr);
			result->mem_segment = CPU_segment(CPU_SEGMENT_SS);
			result->mem_offset = REG_EBP+unsigned2signed8(params->displacement.low16_low); //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_SS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_SS);
			break;
		default:
			halt_modrm("Unknown modr/m32(8-bit): MOD:%u, RM: %u, operand size: %u", MODRM_MOD(params->modrm), reg, CPU[activeCPU].CPU_Operand_size);
			result->isreg = 0; //Unknown modr/m!
			return;
			break;
		}
		break;
	case MOD_MEM_DISP32: //[register+DISP32]
		if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(textnr,sizeof(textnr),"%08" SPRINTF_X_UINT32,params->displacement.dword); //Text!
		switch (reg) //Which register?
		{
		case MODRM_MEM_EAX: //EAX?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[EAX+%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			result->mem_offset = REG_EAX+params->displacement.dword; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			break;
		case MODRM_MEM_EBX: //EBX?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[EBX+%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			result->mem_offset = REG_EBX+params->displacement.dword; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			break;
		case MODRM_MEM_ECX: //ECX?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[ECX+%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			result->mem_offset = REG_ECX+params->displacement.dword; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			break;
		case MODRM_MEM_EDX: //EDX?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[EDX+%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			result->mem_offset = REG_EDX+params->displacement.dword; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			break;
		case MODRM_MEM_ESI: //ESI?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[ESI+%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			result->mem_offset = REG_ESI+params->displacement.dword; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			break;
		case MODRM_MEM_EDI: //EDI?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[EDI+%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			result->mem_offset = REG_EDI+params->displacement.dword; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			break;
		case MODRM_MEM_SIB: //SIB/ESP?
			index = modrm_SIB_reg(params,SIB_INDEX(params->SIB),2,params->displacement.dword,0,&indexstr[0],sizeof(indexstr),&useSS);
			base = modrm_SIB_reg(params,SIB_BASE(params->SIB),2,params->displacement.dword,1,&basestr[0],sizeof(basestr),&useSS);

			if (unlikely(CPU[activeCPU].cpudebugger))
			{
				snprintf(result->text,sizeof(result->text),"%s %s:[%s%s]",modrm_sizes[params->size],CPU_textsegment(useSS?CPU_SEGMENT_SS:CPU_SEGMENT_DS),basestr,indexstr); //Give addr!
			}
			result->mem_segment = CPU_segment(useSS?CPU_SEGMENT_SS:CPU_SEGMENT_DS);
			result->mem_offset = base+index;
			result->segmentregister = CPU_segment_ptr(useSS?CPU_SEGMENT_SS:CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(useSS?CPU_SEGMENT_SS:CPU_SEGMENT_DS);
			break;
		case MODRM_MEM_EBP: //EBP?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[EBP+%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_SS),textnr);
			result->mem_segment = CPU_segment(CPU_SEGMENT_SS);
			result->mem_offset = REG_EBP+params->displacement.dword; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_SS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_SS);
			break;
		default:
			halt_modrm("Unknown modr/m32(32-bit): MOD:%u, RM: %u, operand size: %u", MODRM_MOD(params->modrm), reg, CPU[activeCPU].CPU_Operand_size);
			result->isreg = 0; //Unknown modr/m!
			return;
			break;
		}
		break;
	default:
		break;
	} //Which MOD?
	CPU[activeCPU].last_modrm = 1; //ModR/M!
	if (!CPU[activeCPU].modrm_addoffset) //We're the offset itself?
	{
		CPU[activeCPU].modrm_lastsegment = result->mem_segment;
		CPU[activeCPU].modrm_lastoffset = result->mem_offset&params->info[whichregister].memorymask;
	}
}

extern byte CPU_databussize; //0=16/32-bit bus! 1=8-bit bus when possible (8088/80188)!

void modrm_decode16(MODRM_PARAMS *params, MODRM_PTR *result, byte whichregister) //16-bit address/reg decoder!
{
	INLINEREGISTER byte curreg;
	INLINEREGISTER byte reg;
	INLINEREGISTER byte isregister;
	char textnr[17] = "";

	reg = params->modrm; //Load the modr/m byte!
	if (whichregister) //reg2?
	{
		reg = MODRM_RM(reg); //Take rm!
		curreg = 2;
	}
	else //1 or default (unknown)?
	{
		reg = MODRM_REG(reg); //Default/reg!
		curreg = 1;
	}

	memset(result,0,sizeof(*result)); //Init!
	result->is16bit = 0; //32-bit offset by default!

	if (params->reg_is_segmentregister && (!whichregister)) //Segment register?
	{
		modrm_get_segmentregister(reg,result); //Return segment register!
		return; //Give the segment register!
	}

	if (!whichregister) //REG1?
	{
		isregister = 1; //Register!
	}
	else if (MODRM_MOD(params->modrm)==MOD_REG) //Register R/M?
	{
		isregister = 1; //Register!
	}
	else //No register?
	{
		isregister = 0; //No register (R/M=Memory Address)!
	}

	if (params->specialflags==3) //Reg is CR, R/M is General Purpose Register?
	{
		if (curreg==1) //CR?
		{
			modrm_get_controlregister(reg,result); //Return CR register!
			return; //Give the segment register!
		}
		else
		{
			isregister = 1; //We're a General Purpose register!
		}
	}
	else if (params->specialflags==4) //Reg is DR? R/M is General Purpose register?
	{
		if (curreg==1) //DR?
		{
			modrm_get_debugregister(reg,result); //Return CR register!
			return; //Give the segment register!
		}
		else
		{
			isregister = 1; //We're a General Purpose register!
		}
	}
	else if (params->specialflags==7) //Reg is TR? R/M is General Purpose register?
	{
		if (curreg==1) //TR?
		{
			modrm_get_testregister(reg,result); //Return TR register!
			return; //Give the segment register!
		}
		else
		{
			isregister = 1; //We're a General Purpose register!
		}
	}

	if ((params->specialflags==5) && ((curreg==2) && isregister)) //r/m is 8-bits instead?
	{
		modrm_decode8(params,result,whichregister); //Redirect to 8-bit register!
		return;
	}

	if (isregister) //Is register data?
	{
		result->isreg = 1; //Is register!
		result->regsize = 1; //Word register!
		switch (reg) //What register to use?
		{
		case MODRM_REG_AX: //AX
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"AX");
			result->reg16 = &REG_AX;
			return;
		case MODRM_REG_CX: //CX
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"CX");
			result->reg16 = &REG_CX;
			return;
		case MODRM_REG_DX: //DX
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"DX");
			result->reg16 = &REG_DX;
			return;
		case MODRM_REG_BX: //BX
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"BX");
			result->reg16 = &REG_BX;
			return;
		case MODRM_REG_SP: //SP
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"SP");
			result->reg16 = &REG_SP;
			return;
		case MODRM_REG_BP: //BP
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"BP");
			result->reg16 = &REG_BP;
			return;
		case MODRM_REG_SI: //SI
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"SI");
			result->reg16 = &REG_SI;
			return;
		case MODRM_REG_DI: //DI
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"DI");
			result->reg16 = &REG_DI;
			return;
		default:
			break;
		}
		result->isreg = 0; //Unknown!
		if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"<UNKREG>"); //Unknown!

		halt_modrm("Unknown modr/m16REG: MOD:%u, REG: %u, operand size: %u", MODRM_MOD(params->modrm), reg, CPU[activeCPU].CPU_Operand_size);
		return;
	}

	//Determine R/M (reg2=>RM) pointer!

	if (CPU[activeCPU].CPU_Address_size) //We need to decode as a 32-bit pointer instead?
	{
		modrm_decode32(params,result,whichregister); //Decode 32-bit pointer!
		return; //Don't decode ourselves!
	}

	result->isreg = 2; //Memory!

	INLINEREGISTER uint_32 offset=0; //The offset calculated!
	INLINEREGISTER byte segmentoverridden=0; //Segment is overridden?
	switch (MODRM_MOD(params->modrm)) //Which mod?
	{
	case MOD_MEM: //[register]
		switch (reg) //Which register?
		{
		case MODRM_MEM_BXSI: //BX+SI?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[BX+SI]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS)); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			offset = REG_BX+REG_SI; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			segmentoverridden = (CPU[activeCPU].segment_register!=CPU_SEGMENT_DEFAULT); //Is the segment overridden?
			params->EA_cycles = 7; //Based indexed!
			break;
		case MODRM_MEM_BXDI: //BX+DI?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[BX+DI]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS)); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			offset = REG_BX+REG_DI; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			segmentoverridden = (CPU[activeCPU].segment_register!=CPU_SEGMENT_DEFAULT); //Is the segment overridden?
			params->EA_cycles = 8; //Based indexed!
			break;
		case MODRM_MEM_BPSI: //BP+SI?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[BP+SI]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_SS)); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_SS);
			offset = REG_BP+REG_SI; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_SS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_SS);
			segmentoverridden = (CPU[activeCPU].segment_register!=CPU_SEGMENT_DEFAULT); //Is the segment overridden?
			params->EA_cycles = 8; //Based indexed!
			break;
		case MODRM_MEM_BPDI: //BP+DI?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[BP+DI]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_SS)); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_SS);
			offset = REG_BP+REG_DI; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_SS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_SS);
			segmentoverridden = (CPU[activeCPU].segment_register!=CPU_SEGMENT_DEFAULT); //Is the segment overridden?
			params->EA_cycles = 7; //Based indexed!
			break;
		case MODRM_MEM_SI: //SI?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[SI]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS)); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			offset = REG_SI; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			segmentoverridden = (CPU[activeCPU].segment_register!=CPU_SEGMENT_DEFAULT); //Is the segment overridden?
			params->EA_cycles = 5; //Register Indirect!
			break;
		case MODRM_MEM_DI: //DI?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[DI]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS)); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			offset = REG_DI; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			segmentoverridden = (CPU[activeCPU].segment_register!=CPU_SEGMENT_DEFAULT); //Is the segment overridden?
			params->EA_cycles = 5; //Register Indirect!
			break;
		case MODRM_MEM_DISP16: //BP = disp16?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(textnr,sizeof(textnr),"%04X",params->displacement.low16); //Text!
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[%04X]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS),params->displacement.low16); //Simple [word] displacement!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			offset = params->displacement.low16; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			segmentoverridden = (CPU[activeCPU].segment_register!=CPU_SEGMENT_DEFAULT); //Is the segment overridden?
			params->EA_cycles = 6; //Direct!
			break;
		case MODRM_MEM_BX: //BX?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[BX]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS)); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			offset = REG_BX; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			segmentoverridden = (CPU[activeCPU].segment_register!=CPU_SEGMENT_DEFAULT); //Is the segment overridden?
			params->EA_cycles = 5; //Register Indirect!
			break;
		default:
			halt_modrm("Unknown modr/m16(mem): MOD:%u, RM: %u, operand size: %u",MODRM_MOD(params->modrm),reg, CPU[activeCPU].CPU_Operand_size);
			result->isreg = 0; //Unknown modr/m!
			return;
			break;
		}
		result->memorymask = 0xFFFF; //Only 16-bit offsets are used, full 32-bit otherwise for both checks and memory?
		result->is16bit = 1; //16-bit offset!
		break;
	case MOD_MEM_DISP8: //[register+DISP8]
		if (unlikely(CPU[activeCPU].cpudebugger))
		{
			if (params->displacement.low16_low & 0x80) //Negative?
			{
				snprintf(textnr, sizeof(textnr), "-%02X", 0 - unsigned2signed8(params->displacement.low16_low)); //Text negative!
			}
			else
			{
				snprintf(textnr, sizeof(textnr), "+%02X", params->displacement.low16_low); //Text positive!
			}
		}
		switch (reg) //Which register?
		{
		case MODRM_MEM_BXSI: //BX+SI?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[BX+SI%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			offset = REG_BX+REG_SI+unsigned2signed8(params->displacement.low16_low); //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			segmentoverridden = (CPU[activeCPU].segment_register!=CPU_SEGMENT_DEFAULT); //Is the segment overridden?
			params->EA_cycles = 11; //Based indexed relative!
			params->havethreevariables = 1; //3 params added!
			break;
		case MODRM_MEM_BXDI: //BX+DI?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[BX+DI%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			offset = REG_BX+REG_DI+unsigned2signed8(params->displacement.low16_low); //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			segmentoverridden = (CPU[activeCPU].segment_register!=CPU_SEGMENT_DEFAULT); //Is the segment overridden?
			params->EA_cycles = 12; //Based indexed relative!
			params->havethreevariables = 1; //3 params added!
			break;
		case MODRM_MEM_BPSI: //BP+SI?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[BP+SI%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_SS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_SS);
			offset = REG_BP+REG_SI+unsigned2signed8(params->displacement.low16_low); //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_SS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_SS);
			segmentoverridden = (CPU[activeCPU].segment_register!=CPU_SEGMENT_DEFAULT); //Is the segment overridden?
			params->EA_cycles = 12; //Based indexed relative!
			params->havethreevariables = 1; //3 params added!
			break;
		case MODRM_MEM_BPDI: //BP+DI?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[BP+DI%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_SS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_SS);
			offset = REG_BP+REG_DI+unsigned2signed8(params->displacement.low16_low); //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_SS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_SS);
			segmentoverridden = (CPU[activeCPU].segment_register!=CPU_SEGMENT_DEFAULT); //Is the segment overridden?
			params->EA_cycles = 11; //Based indexed relative!
			params->havethreevariables = 1; //3 params added!
			break;
		case MODRM_MEM_SI: //SI?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[SI%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			offset = REG_SI+unsigned2signed8(params->displacement.low16_low); //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			segmentoverridden = (CPU[activeCPU].segment_register!=CPU_SEGMENT_DEFAULT); //Is the segment overridden?
			params->EA_cycles = 9; //Register relative!
			break;
		case MODRM_MEM_DI: //DI?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[DI%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			offset = REG_DI+unsigned2signed8(params->displacement.low16_low); //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			segmentoverridden = (CPU[activeCPU].segment_register!=CPU_SEGMENT_DEFAULT); //Is the segment overridden?
			params->EA_cycles = 9; //Register relative!
			break;
		case MODRM_MEM_BP: //BP?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[BP%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_SS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_SS);
			offset = REG_BP+unsigned2signed8(params->displacement.low16_low); //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_SS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_SS);
			segmentoverridden = (CPU[activeCPU].segment_register!=CPU_SEGMENT_DEFAULT); //Is the segment overridden?
			params->EA_cycles = 9; //Register relative!
			break;
		case MODRM_MEM_BX: //BX?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[BX%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			offset = REG_BX+unsigned2signed8(params->displacement.low16_low); //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			segmentoverridden = (CPU[activeCPU].segment_register!=CPU_SEGMENT_DEFAULT); //Is the segment overridden?
			params->EA_cycles = 9; //Register relative!
			break;
		default:
			halt_modrm("Unknown modr/m16(8-bit): MOD:%u, RM: %u, operand size: %u",MODRM_MOD(params->modrm),reg, CPU[activeCPU].CPU_Operand_size);
			result->isreg = 0; //Unknown modr/m!
			return;
			break;
		}
		result->memorymask = 0xFFFF; //Only 16-bit offsets are used, full 32-bit otherwise for both checks and memory?
		result->is16bit = 1; //16-bit offset!
		break;
	case MOD_MEM_DISP16: //[register+DISP16]
		if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(textnr,sizeof(textnr),"%04X",params->displacement.low16); //Text!
		switch (reg) //Which register?
		{
		case MODRM_MEM_BXSI: //BX+SI?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[BX+SI+%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			offset = REG_BX+REG_SI+params->displacement.low16; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			segmentoverridden = (CPU[activeCPU].segment_register!=CPU_SEGMENT_DEFAULT); //Is the segment overridden?
			params->EA_cycles = 11; //Based indexed relative!
			params->havethreevariables = 1; //3 params added!
			break;
		case MODRM_MEM_BXDI: //BX+DI?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[BX+DI+%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			offset = REG_BX+REG_DI+params->displacement.low16; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			segmentoverridden = (CPU[activeCPU].segment_register!=CPU_SEGMENT_DEFAULT); //Is the segment overridden?
			params->EA_cycles = 12; //Based indexed relative!
			params->havethreevariables = 1; //3 params added!
			break;
		case MODRM_MEM_BPSI: //BP+SI?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[BP+SI+%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_SS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_SS);
			offset = REG_BP+REG_SI+params->displacement.low16; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_SS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_SS);
			segmentoverridden = (CPU[activeCPU].segment_register!=CPU_SEGMENT_DEFAULT); //Is the segment overridden?
			params->EA_cycles = 12; //Based indexed relative!
			params->havethreevariables = 1; //3 params added!
			break;
		case MODRM_MEM_BPDI: //BP+DI?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[BP+DI+%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_SS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_SS);
			offset = REG_BP+REG_DI+params->displacement.low16; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_SS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_SS);
			segmentoverridden = (CPU[activeCPU].segment_register!=CPU_SEGMENT_DEFAULT); //Is the segment overridden?
			params->EA_cycles = 11; //Based indexed relative!
			params->havethreevariables = 1; //3 params added!
			break;
		case MODRM_MEM_SI: //SI?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[SI+%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			offset = REG_SI+params->displacement.low16; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			segmentoverridden = (CPU[activeCPU].segment_register!=CPU_SEGMENT_DEFAULT); //Is the segment overridden?
			params->EA_cycles = 9; //Register relative!
			break;
		case MODRM_MEM_DI: //DI?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[DI+%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			offset = REG_DI+params->displacement.low16; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			segmentoverridden = (CPU[activeCPU].segment_register!=CPU_SEGMENT_DEFAULT); //Is the segment overridden?
			params->EA_cycles = 9; //Register relative!
			break;
		case MODRM_MEM_BP: //BP?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[BP+%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_SS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_SS);
			offset = REG_BP+params->displacement.low16; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_SS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_SS);
			segmentoverridden = (CPU[activeCPU].segment_register!=CPU_SEGMENT_DEFAULT); //Is the segment overridden?
			params->EA_cycles = 9; //Register relative!
			break;
		case MODRM_MEM_BX: //REG_BX?
			if (unlikely(CPU[activeCPU].cpudebugger)) snprintf(result->text,sizeof(result->text),"%s %s:[BX+%s]",modrm_sizes[params->size],CPU_textsegment(CPU_SEGMENT_DS),textnr); //Give addr!
			result->mem_segment = CPU_segment(CPU_SEGMENT_DS);
			offset = REG_BX+params->displacement.low16; //Give addr!
			result->segmentregister = CPU_segment_ptr(CPU_SEGMENT_DS);
			result->segmentregister_index = CPU_segment_index(CPU_SEGMENT_DS);
			segmentoverridden = (CPU[activeCPU].segment_register!=CPU_SEGMENT_DEFAULT); //Is the segment overridden?
			params->EA_cycles = 9; //Register relative!
			break;
		default:
			halt_modrm("Unknown modr/m16(16-bit): MOD:%u, RM: %u, operand size: %u",MODRM_MOD(params->modrm),reg, CPU[activeCPU].CPU_Operand_size);
			result->isreg = 0; //Unknown modr/m!
			return;
			break;
		}
		result->memorymask = 0xFFFF; //Only 16-bit offsets are used, full 32-bit otherwise for both checks and memory?
		result->is16bit = 1; //16-bit offset!
		break;
	default:
		break;
	} //Which MOD?
	if (segmentoverridden) //Segment overridden with cycle accuracy?
	{
		params->EA_cycles += 2; //Add two clocks for the segment override!
	}
	CPU[activeCPU].last_modrm = 1; //ModR/M!
	if (!CPU[activeCPU].modrm_addoffset) //We're the offset itself?
	{
		CPU[activeCPU].modrm_lastsegment = result->mem_segment;
		CPU[activeCPU].modrm_lastoffset = offset&params->info[whichregister].memorymask; //Save the last loaded offset!
	}
	result->mem_offset = offset; //Save the offset we use!
}

void modrm_decode8(MODRM_PARAMS *params, MODRM_PTR *result, byte whichregister)
{
	INLINEREGISTER byte isregister;
	INLINEREGISTER byte reg = 0;

	if (whichregister) //reg2?
	{
		reg = MODRM_RM(params->modrm); //Take reg2/RM!
	}
	else //1 or default (unknown)?
	{
		reg = MODRM_REG(params->modrm); //Default/reg1!
	}

	if (!whichregister) //REG1?
	{
		isregister = 1; //Register!
	}
	else if (MODRM_MOD(params->modrm)==MOD_REG) //Register R/M?
	{
		isregister = 1; //Register!
	}
	else //No register?
	{
		isregister = 0; //No register, so use memory R/M!
	}

	if (params->specialflags==3) //Reg is CR, R/M is General Purpose Register?
	{
		if (isregister) //CR?
		{
			modrm_get_controlregister(reg,result); //Return CR register!
			return; //Give the segment register!
		}
		else
		{
			isregister = 1; //We're a General Purpose register!
		}
	}
	else if (params->specialflags==4) //Reg is DR? R/M is General Purpose register?
	{
		if (isregister) //CR?
		{
			modrm_get_debugregister(reg,result); //Return CR register!
			return; //Give the segment register!
		}
		else
		{
			isregister = 1; //We're a General Purpose register!
		}
	}
	else if (params->specialflags==7) //Reg is TR? R/M is General Purpose register?
	{
		if (isregister) //TR?
		{
			modrm_get_testregister(reg,result); //Return TR register!
			return; //Give the segment register!
		}
		else
		{
			isregister = 1; //We're a General Purpose register!
		}
	}

	memset(result,0,sizeof(*result)); //Init!

	if (isregister) //Is register data?
	{
		result->isreg = 1; //Register!
		result->regsize = 0; //Byte register!
		switch (reg)
		{
		case MODRM_REG_AL:
			result->reg8 = &REG_AL;
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"AL");
			return;
		case MODRM_REG_CL:
			result->reg8 = &REG_CL;
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"CL");
			return;
		case MODRM_REG_DL:
			result->reg8 = &REG_DL;
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"DL");
			return;
		case MODRM_REG_BL:
			result->reg8 = &REG_BL;
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"BL");
			return;
		case MODRM_REG_AH:
			result->reg8 = &REG_AH;
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"AH");
			return;
		case MODRM_REG_CH:
			result->reg8 = &REG_CH;
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"CH");
			return;
		case MODRM_REG_DH:
			result->reg8 = &REG_DH;
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"DH");
			return;
		case MODRM_REG_BH:
			result->reg8 = &REG_BH;
			if (unlikely(CPU[activeCPU].cpudebugger)) safestrcpy(result->text,sizeof(result->text),"BH");
			return;
		default:
			break;
		}
		result->isreg = 0; //Unknown register!

		halt_modrm("Unknown modr/m8Reg: %02x; MOD:%u, reg: %u, operand size: %u",MODRM_MOD(params->modrm),reg, CPU[activeCPU].CPU_Operand_size);
		return; //Stop decoding!
	}


	switch (MODRM_MOD(params->modrm)) //Which mod?
	{
	case MOD_MEM: //[register]
	case MOD_MEM_DISP8: //[register+DISP8]
	case MOD_MEM_DISP32: //[register+DISP32]
		modrm_decode16(params,result,whichregister); //Use 16-bit decoder!
		return;
	default: //Shouldn't be here!
		halt_modrm("Reg MODRM when shouldn't be!");
		return;
	} //Which MOD?
	halt_modrm("Unknown modr/m8: %02x; MOD:%u, reg: %u, operand size: %u",MODRM_MOD(params->modrm),reg, CPU[activeCPU].CPU_Operand_size);
	result->isreg = 0; //Unknown modr/m!
}

byte *modrm_addr8(MODRM_PARAMS *params, int whichregister, int forreading)
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	switch (params->info[whichregister].isreg) //What type?
	{
	case 1: //Register?
		if (!params->info[whichregister].reg8)
		{
			halt_modrm("MODRM:NULL REG8\nValue:%s", params->info[whichregister].text);
		}
		return (byte *)/*memprotect(*/params->info[whichregister].reg8/*,1,"CPU_REGISTERS")*/; //Give register!
	case 2: //Memory?
		return NULL; //We don't do memory addresses! Use direct memory access here!
	default:
		halt_modrm("MODRM: Unknown MODR/M8!");
		return NULL; //Unknown!
	}
}

word *modrm_addr16(MODRM_PARAMS *params, int whichregister, int forreading)
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	switch (params->info[whichregister].isreg) //What type?
	{
	case 1: //Register?
		if (!params->info[whichregister].reg16)
		{
			halt_modrm("MODRM:NULL REG16\nValue:%s",params->info[whichregister].text);
		}
		return (word *)/*memprotect(*/params->info[whichregister].reg16/*,2,"CPU_REGISTERS")*/; //Give register!
	case 2: //Memory?
		return NULL; //We don't do memory addresses! Use direct memory access here!
	default:
		halt_modrm("MODRM: Unknown MODR/M16!");
		return NULL; //Unknown!
	}
}

void modrm_text8(MODRM_PARAMS *params, int whichregister, char *result)
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	safestrcpy(result,256,params->info[whichregister].text); //Use the text representation!
}

void modrm_text16(MODRM_PARAMS *params, int whichregister, char *result)
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	safestrcpy(result,256,params->info[whichregister].text); //Use the text representation!
}

void modrm_text32(MODRM_PARAMS *params, int whichregister, char *result)
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	safestrcpy(result,256, params->info[whichregister].text); //Use the text representation!
}

word modrm_lea16(MODRM_PARAMS *params, int whichregister) //For LEA instructions!
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	INLINEREGISTER uint_32 result;
	switch (params->info[whichregister].isreg) //What type?
	{
	case 1: //Register?
		//Don't update the last offset!
		CPU[activeCPU].last_modrm = 1; //ModR/M!
		result = CPU[activeCPU].modrm_lastoffset;
		result += CPU[activeCPU].modrm_addoffset; //Add offset!
		return result; //No registers allowed officially, but we return the last offset in this case (undocumented)!
	case 2: //Memory?
		CPU[activeCPU].last_modrm = 1; //ModR/M!
		result = params->info[whichregister].mem_offset;
		if (!CPU[activeCPU].modrm_addoffset) //We're the offset itself?
		{
			CPU[activeCPU].modrm_lastsegment = 0; //No segment used!
			CPU[activeCPU].modrm_lastoffset = result&params->info[whichregister].memorymask; //Load the result into the last offset!
		}
		result += CPU[activeCPU].modrm_addoffset; //Relative offset!

		return result&params->info[whichregister].memorymask; //Give memory offset!
	default:
		return 0; //Unknown!
	}
}

uint_32 modrm_lea32(MODRM_PARAMS *params, int whichregister) //For LEA instructions!
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	INLINEREGISTER uint_32 result;
	switch (params->info[whichregister].isreg) //What type?
	{
	case 1: //Register?
		CPU[activeCPU].last_modrm = 1; //ModR/M!
		result = CPU[activeCPU].modrm_lastoffset; //Last offset!
		result += CPU[activeCPU].modrm_addoffset; //Add offset!
		return result; //No registers allowed officially, but we return the last offset in this case (undocumented)!
	case 2: //Memory?
		CPU[activeCPU].last_modrm = 1; //ModR/M!
		result = params->info[whichregister].mem_offset;
		if (!CPU[activeCPU].modrm_addoffset) //We're the offset itself?
		{
			CPU[activeCPU].modrm_lastsegment = 0; //No segment used!
			CPU[activeCPU].modrm_lastoffset = result&params->info[whichregister].memorymask; //Load the result into the last offset!
		}
		result += CPU[activeCPU].modrm_addoffset; //Relative offset!

		return result&params->info[whichregister].memorymask; //Give memory offset!
	default:
		return 0; //Unknown!
	}
}

void modrm_lea16_text(MODRM_PARAMS *params, int whichregister, char *result) //For LEA instructions!
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	switch (params->info[whichregister].isreg) //What type?
	{
	case 1: //Register?
		safestrcpy(result,256,params->info[whichregister].text); //No registers allowed!
		return;
	case 2: //Memory?
		safestrcpy(result,256,params->info[whichregister].text); //Set the text literally!
		return; //Memory is valid!
	default:
		safestrcpy(result,256,"<UNKNOWN>");
		return; //Unknown!
	}
}

void modrm_lea32_text(MODRM_PARAMS *params, int whichregister, char *result) //For LEA instructions!
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	switch (params->info[whichregister].isreg) //What type?
	{
	case 1: //Register?
		safestrcpy(result,256, params->info[whichregister].text); //No registers allowed!
		return;
	case 2: //Memory?
		safestrcpy(result,256, params->info[whichregister].text); //Set the text literally!
		return; //Memory is valid!
	default:
		safestrcpy(result,256, "<UNKNOWN>");
		return; //Unknown!
	}
}

//modrm_offset16: same as lea16, but allow registers too!
word modrm_offset16(MODRM_PARAMS *params, int whichregister) //Gives address for JMP, CALL etc.!
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	INLINEREGISTER uint_32 result;
	switch (params->info[whichregister].isreg) //What type?
	{
	case 1: //Register?
		return *params->info[whichregister].reg16; //Give register value!
	case 2: //Memory?
		CPU[activeCPU].last_modrm = 1; //ModR/M!
		result = params->info[whichregister].mem_offset; //Load offset!
		if (!CPU[activeCPU].modrm_addoffset) //We're the offset itself?
		{
			CPU[activeCPU].modrm_lastsegment = 0;
			CPU[activeCPU].modrm_lastoffset = result&params->info[whichregister].memorymask;
		}
		result += CPU[activeCPU].modrm_addoffset; //Add offset!
		return result&params->info[whichregister].memorymask; //Give memory offset!
	default:
		return 0; //Unknown!
	}
}

uint_32 modrm_offset32(MODRM_PARAMS *params, int whichregister) //Gives address for JMP, CALL etc.!
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	INLINEREGISTER uint_32 result;
	switch (params->info[whichregister].isreg) //What type?
	{
	case 1: //Register?
		return *params->info[whichregister].reg32; //Give register value!
	case 2: //Memory?
		CPU[activeCPU].last_modrm = 1; //ModR/M!
		result = params->info[whichregister].mem_offset; //Load offset!
		if (!CPU[activeCPU].modrm_addoffset) //We're the offset itself?
		{
			CPU[activeCPU].modrm_lastsegment = 0;
			CPU[activeCPU].modrm_lastoffset = result&params->info[whichregister].memorymask;
		}
		result += CPU[activeCPU].modrm_addoffset; //Add offset!
		return result&params->info[whichregister].memorymask; //Give memory offset!
	default:
		return 0; //Unknown!
	}
}

//Used for LDS, LES, LSS, LEA
word *modrm_addr_reg16(MODRM_PARAMS *params, int whichregister) //For LEA related instructions, returning the register!
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	switch (params->info[whichregister].isreg) //What type?
	{
	case 1: //Register?
		if (!params->info[whichregister].reg16)
		{
			halt_modrm("NULL REG16LEA");
		}
		return params->info[whichregister].reg16; //Give register itself!
	case 2: //Memory?
		if (!params->info[whichregister].segmentregister)
		{
			halt_modrm("NULL REG16LEA_SEGMENT");
		}
		return params->info[whichregister].segmentregister; //Give the segment register of the MODR/M!
	default:
		halt_modrm("REG16LEA_UNK");
		return NULL; //Unknown!
	}
}











/*

32-bit functionality

*/

uint_32 *modrm_addr32(MODRM_PARAMS *params, int whichregister, int forreading)
{
	if (unlikely(params->notdecoded)) modrm_notdecoded(params); //Error out!
	switch (params->info[whichregister].isreg) //What type?
	{
	case 1: //Register?
		if (params->info[whichregister].is_segmentregister) //Segment register?
		{
			if (params->info[whichregister].reg16) //Valid?
			{
				return (uint_32 *)params->info[whichregister].reg16; //Segment register itself!
			}
		}
		if (!params->info[whichregister].reg32)
		{
			halt_modrm("NULL REG32");
		}
		return (uint_32 *)/*memprotect(*/params->info[whichregister].reg32/*,4,"CPU_REGISTERS")*/; //Give register!
	case 2: //Memory?
		CPU[activeCPU].last_modrm = 1; //ModR/M!
		if (!CPU[activeCPU].modrm_addoffset) //We're the offset itself?
		{
			CPU[activeCPU].modrm_lastsegment = params->info[whichregister].mem_segment;
			CPU[activeCPU].modrm_lastoffset = params->info[whichregister].mem_offset;
			CPU[activeCPU].modrm_lastoffset &= params->info[whichregister].memorymask; //Mask!
		}
		return NULL; //We don't do memory addresses! Use direct memory access here!
	default:
		return NULL; //Unknown!
	}

}












//CPU kernel functions

//Calculate the offsets used for the modr/m byte!
void modrm_recalc(MODRM_PARAMS *param)
{
	param->EA_cycles = 0; //No EA cycles for register accesses by default!

	param->info[0].memorymask = ~0; //Default to full memory access!
	param->info[1].memorymask = ~0; //Default to full memory access!
	param->info[0].is16bit = 0; //Default to full memory access!
	param->info[1].is16bit = 0; //Default to full memory access!

	param->havethreevariables = 0; //Default: not 3 params added!

	switch (param->sizeparam&7) //What size?
	{
	case 0: //8-bits?
		modrm_decode8(param, &param->info[0], 0); //#0 reg!
		modrm_decode8(param, &param->info[1], 1); //#0 modr/m!
		break;
	case 1: //16-bits?
		modrm_decode16(param, &param->info[0], 0); //#0 reg!
		modrm_decode16(param, &param->info[1], 1); //#0 modr/m!
		break;
	case 2: //32-bits?
		modrm_decode32(param, &param->info[0], 0); //#0 reg!
		modrm_decode32(param, &param->info[1], 1); //#0 modr/m!
		break;
	default:
		halt_modrm("Unknown decoder size: %u",param->sizeparam); //Unknown size!
	}
}

/*

specialflags:
0: No special flags! (Use displacement if needed!)
1: RM=> REG2 (No displacement etc.)
2: REG1=> SEGMENTREGISTER (Use displacement if needed!)
3: Reg=> CRn, R/M is General Purpose register!
4: Reg=> DRn, R/M is General Purpose register!
5: General purpose register(Reg) is 16-bits instead!
6: General purpose register(Reg) is 32-bits instead!
7: Reg=> TRn, R/M is General Purpose register!

*/

byte modrm_readparams(MODRM_PARAMS *param, byte size, byte specialflags, byte OP)
{
//Special data we already know:
	if (param->instructionfetch.MODRM_instructionfetch==0) //To reset?
	{
		//Reset and initialize all our parameters!
		memset(param,0,sizeof(*param)); //Initialise the structure for filling it!
		param->notdecoded = 1; //Init: not decoded fully and ready for use!
		param->reg_is_segmentregister = 0; //REG2 is NORMAL!

		param->specialflags = specialflags; //Is this a /r modr/m?

		if (specialflags==2) //reg1 is segment register?
		{
			param->reg_is_segmentregister = 1; //REG2 is segment register!
		}

		param->error = 0; //Default: no errors detected during decoding!
		param->instructionfetch.MODRM_instructionfetch = 1; //Start fetching now!
	}

	//Start fetching the parameters from the opcode parser!
	if (param->instructionfetch.MODRM_instructionfetch==1) //Fetching ModR/M byte?
	{
		if (CPU_readOP(&param->modrm,1)) return 1; /* modrm byte first */
		if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
		param->instructionfetch.MODRM_instructionfetch = 2; //SIB checking now!
		if ((CPU[activeCPU].is0Fopcode) && (OP<=0x1)) //Special case for 0x0F01 opcode?
		{
			if ((OP==0x1) && (((MODRM_REG(param->modrm)==4) && ((param->modrm&0xC0)!=0xC0)) || (MODRM_REG(param->modrm)==6))) //Always 16-bit ModR/M?
			{
				size = 1; //Force word size instead of DWORD size(LMSW/SMSW(memory))!
			}
			if ((OP==0x0) && ((((MODRM_REG(param->modrm)<=1) && ((param->modrm&0xC0)!=0xC0)) || (MODRM_REG(param->modrm)>1)))) //Always 16-bit ModR/M(for memory or always)?
			{
				size = 1; //Force word size instead of DWORD size(SLDT/STR(memory) and other opcodes)!
			}
		}
		param->sizeparam = size; //Actual size!
		param->size = (size&0x80)?2:((size&0x40)?1:((size&0x20)?0:size)); //What size is used, with autodetection?
	}

	if (param->instructionfetch.MODRM_instructionfetch==2) //Fetching SIB byte if needed?
	{
		if (modrm_useSIB(param,param->sizeparam)) //Using SIB byte?
		{
			if (CPU_readOP(&param->SIB,1)) return 1; //Read SIB byte or 0!
			if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
		}
		else
		{
			param->SIB = 0; //No SIB byte!
		}
		param->instructionfetch.MODRM_instructionfetch = 3; //Fetching immediate data!
		CPU[activeCPU].instructionfetch.CPU_fetchparameterPos = 0; //Reset the parameter position for new parameters!
		param->displacement.dword = 0; //Reset DWORD (biggest) value (reset value to 0)!
	}

	if (param->instructionfetch.MODRM_instructionfetch==3) //Fetching displacement byte(s) if needed?
	{
		switch (modrm_useDisplacement(param,param->sizeparam)) //Displacement?
		{
		case 1: //DISP8?
			if (CPU_readOP(&param->displacement.low16_low,1)) return 1; //Use 8-bit!
			if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
			break;
		case 2: //DISP16?
			if (CPU_readOPw(&param->displacement.low16,1)) return 1; //Use 16-bit!
			if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
			break;
		case 3: //DISP32?
			if (CPU_readOPdw(&param->displacement.dword,1)) return 1; //Use 32-bit!
			if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
			break;
		default: //Unknown/no displacement?
			break; //No displacement!
		}
		param->instructionfetch.MODRM_instructionfetch = 4; //Finished with all stages of the modR/M data!
	}

	modrm_recalc(param); //Calculate the params!

	if (param->reg_is_segmentregister && (param->info[0].reg16==NULL)) //Invalid segment register specified?
	{
		param->error = 1; //We've detected an error!
	}
	else if (((specialflags==3) || (specialflags==4) || (specialflags==7)) && (param->info[0].reg32==NULL)) //Invalid CR/DR register specified?
	{
		param->error = 1; //We've detected an error!
	}
	if (CPU_getprefix(0xF0)) //LOCK prefix used?
	{
		if ((CPU[activeCPU].currentOpcodeInformation->readwritebackinformation&0x400) && (param->info[1].isreg!=2) && (MODRM_REG(param->modrm)<5)) //Allow for reg 5-7 only!
		{
			param->error = 1; //We've detected an error!		
		}
		else if ((CPU[activeCPU].currentOpcodeInformation->readwritebackinformation&0x400)) //Allow for reg 5-7 only!
		{
			if (MODRM_REG(param->modrm)>4) //We're a lockable instruction?
			{
				if (param->info[1].isreg!=2) //Invalid access?
				{
					param->error = 1; //We've detected an error!		
				}
			}
			else //Invalid prefix for instruction?
			{
				param->error = 1; //We've detected an error!		
			}
		}
		else //Check for reg also, if required?
		{
			switch (CPU[activeCPU].currentOpcodeInformation->readwritebackinformation&0x300)
			{
			case 0x100: //Reg!=7 faults only!
				if (MODRM_REG(param->modrm)!=7) //We're a lockable instruction?
				{
					if (param->info[1].isreg!=2) //Memory not accessed?
					{
						param->error = 1; //We've detected an error!		
					}
				}
				else //Invalid prefix for instruction?
				{
					param->error = 1; //We've detected an error!		
				}
				break;
			case 0x200: //Reg 2 or 3 faults only!
				if ((MODRM_REG(param->modrm)&0x6)==2) //We're a lockable instruction?
				{
					if (param->info[1].isreg!=2) //Memory not accessed?
					{
						param->error = 1; //We've detected an error!		
					}
				}
				else //Invalid prefix for instruction?
				{
					param->error = 1; //We've detected an error!		
				}
				break;
			case 0x300: //Reg 0 or 1 faults only!
				if ((MODRM_REG(param->modrm)&0x6)==0) //We're a lockable instruction?
				{
					if (param->info[CPU[activeCPU].currentOpcodeInformation->modrm_src0].isreg!=2) //Memory not accessed?
					{
						param->error = 1; //We've detected an error!		
					}
				}
				else //Invalid prefix for instruction?
				{
					param->error = 1; //We've detected an error!		
				}
				break;
			default: //Normal behaviour?
				if (param->info[1].isreg!=2) //Memory not accessed?
				{
					param->error = 1; //We've detected an error!
				}
				break;
			}
		}
	}
	CPU[activeCPU].thereg = MODRM_REG(CPU[activeCPU].params.modrm); //The register for multifunction grp opcodes!
	CPU[activeCPU].params.notdecoded = 0; //The ModR/M data is ready to be used!
	return 0; //We're finished fetching!
}
