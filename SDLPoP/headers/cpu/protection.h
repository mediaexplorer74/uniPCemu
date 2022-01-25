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

#ifndef PROTECTION_H
#define PROTECTION_H

#include "headers/cpu/cpu.h" //Basic CPU support!

#define getCPL() CPU[activeCPU].CPL
#define getRPL(segment) ((segment)&3)
#define setRPL(segment,RPL) segment = ((segment&~3)|(RPL&3))
#define getDescriptorIndex(segmentval) (((segmentval)>>3)&0x1FFF)

int CPU_segment_index(byte defaultsegment); //Plain segment to use, direct access!
int get_segment_index(word *location);
void protection_nextOP(); //We're executing the next OPcode?
byte segmentWritten(int segment, word value, word isJMPorCALL); //A segment register has been written to! isJMPorCALL: 1=JMP, 2=CALL.

int CPU_MMU_checklimit(int segment, word segmentval, uint_64 offset, word forreading, byte is_offset16); //Determines the limit of the segment, forreading=2 when reading an opcode!
byte CPU_MMU_checkrights_jump(int segment, word segmentval, uint_64 offset, byte forreading, SEGMENT_DESCRIPTOR *descriptor, byte addrtest, byte is_offset16); //Check rights for VERR/VERW!

byte CPU_faultraised(byte type); //A fault has been raised (286+)?

//Special support for error handling!
void THROWDESCGP(word segmentval, byte external, byte tbl);
void THROWDESCSS(word segmentval, byte external, byte tbl);
void THROWDESCNP(word segmentval, byte external, byte tbl);
void THROWDESCTS(word segmentval, byte external, byte tbl);

//Internal usage by the protection modules! Result: 1=OK, 0=Error out by caller, -1=Already errored out, abort error handling(caused by Paging Unit faulting)!
sbyte LOADDESCRIPTOR(int segment, word segmentval, SEGMENT_DESCRIPTOR *container, word isJMPorCALL); //Bits 8-15 of isJMPorCALL are reserved, only 8-bit can be supplied by others than SAVEDESCRIPTOR!
sbyte SAVEDESCRIPTOR(int segment, word segmentval, SEGMENT_DESCRIPTOR *container, word isJMPorCALL); //Save a loaded descriptor back to memory!
sbyte touchSegment(int segment, word segmentval, SEGMENT_DESCRIPTOR *container, word isJMPorCALL); //Touch a system segment descriptor that's successfully loaded into the cache!

byte checkPortRights(word port); //Are we allowed to not use this port?
byte getTSSIRmap(word intnr); //What are we to do with this interrupt? 0=Perform V86 real-mode interrupt. 1=Perform protected mode interrupt(legacy). 2=Faulted on the TSS; Abort INT instruction processing.
byte disallowPOPFI(); //Allow POPF to not change the interrupt flag?
byte checkSTICLI(); //Check STI/CLI rights! 1 when allowed, 0 when to be ignored!

byte CPU_ProtectedModeInterrupt(byte intnr, word returnsegment, uint_32 returnoffset, int_64 errorcode, byte is_interrupt); //Execute a protected mode interrupt!

byte STACK_SEGMENT_DESCRIPTOR_B_BIT(); //80386+: Gives the B-Bit of the DATA DESCRIPTOR TABLE FOR SS-register!
byte CODE_SEGMENT_DESCRIPTOR_D_BIT(); //80386+: Gives the D-Bit of the CODE DESCRIPTOR TABLE FOR CS-register!
uint_32 getstackaddrsizelimiter(); //80286+: Gives the stack address size mask to use!
void CPU_commitState(); //Prepare for a fault by saving all required data!
void CPU_commitStateESP(); //Prepare for a page handler after an instruction has changed ESP!
void CPU_onResettingFault(byte is_paginglock); //When resetting the current instruction for a fault!

void CPU_AC(int_64 errorcode); //Alignment check fault!

byte switchStacks(byte newCPL); //Returns 1 on error, 0 on success!
void updateCPL(); //Update the CPL to be the currently loaded CPL depending on the mode and descriptors!
void CPU_calcSegmentPrecalcsPrecalcs(); //Calculatet the segment precalcs precalcs!
void CPU_calcSegmentPrecalcs(byte is_CS, SEGMENT_DESCRIPTOR *descriptor);
int getLoadedTYPE(SEGMENT_DESCRIPTOR *loadeddescriptor);
#endif
