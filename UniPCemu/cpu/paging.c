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
//We're the paging unit itself!
#define IS_PAGING
#include "headers/cpu/mmu.h" //MMU reqs!
#include "headers/cpu/cpu.h" //CPU reqs!
#include "headers/mmu/mmu_internals.h" //Internal transfer support!
#include "headers/mmu/mmuhandler.h" //MMU direct access support!
#include "headers/cpu/easyregs.h" //Easy register support!
#include "headers/support/log.h" //Logging support!
#include "headers/emu/debugger/debugger.h" //Debugger support!
#include "headers/cpu/protection.h" //Fault raising support!
#include "headers/cpu/cpu_execution.h" //Execution phase support!
#include "headers/cpu/biu.h" //Memory support!
#include "headers/support/signedness.h" //Sign support!
#include "headers/cpu/paging.h" //Our own defintions!

extern byte EMU_RUNNING; //1 when paging can be applied!

//Enable below to only set the access bit when actually reading/writing the memory address(and all checks have passed).
//#define ACCESS_ON_READWRITE
//Enable below define to make paging lock the bus when doing so, aborting the instruction or current action until it's granted.
#define PAGING_LOCKED

//20-bit PDBR. Could also be CR3 in total?
#define CR3_PDBR (CPU[activeCPU].registers->CR3&0xFFFFF000)
#define CR3_PAEPDBR (CPU[activeCPU].registers->CR3&0xFFFFFFE0)

//Present: 1 when below is used, 0 is invalid: not present (below not used/to be trusted).
#define PXE_P 0x00000001
//Write allowed? Else read-only.
#define PXE_RW 0x00000002
//1=User, 0=Supervisor only
#define PXE_US 0x00000004
//Write through
#define PXE_W 0x00000008
//Cache disabled
#define PDE_D 0x00000010
//Cache disable
#define PTE_C 0x00000010
//Accessed
#define PXE_A 0x00000020
//Page size (0 for 4KB, 1 for 4MB)
#define PDE_S 0x00000080
//Dirty: we've been written to(4MB Page Directory only)!
#define PDE_Dirty 0x00000040
//Dirty: we've been written to!
#define PTE_D 0x00000040
//Global flag! Must be on PTE only (PDE is cleared always)
#define PTE_G 0x00000100
//Address mask/active mask(in the lookup table)
#define PXE_ADDRESSMASK 0xFFFFF000ULL
#define PXE_PAEADDRESSMASK 0xFFFFFF000ULL
#define PDE_LARGEADDRESSMASK 0xFFC00000ULL
#define PDE_PAELARGEADDRESSMASK 0xFFFE00000ULL
//Reversed masks on the passthrough bits!
#define PXE_ACTIVEMASK 0xFFF
#define PDE_LARGEACTIVEMASK 0x3FFFFF
#define PDE_PAELARGEACTIVEMASK 0x1FFFFF
//Address shift to get the physical address
#define PXE_ADDRESSSHIFT 0
//What to ignore when reading the TLB for read accesses during normal execution? We ignore Dirty and Writable access bits!
#define TLB_IGNOREREADMASK 0xC

 //The used TAG(using a 4KB page, but the lower 10 bits are unused in 4MB pages)!
#define Paging_generateTAG(logicaladdress,W,U,D,S) ((((((((((S)<<1)|(D))<<1)|(W))<<1)|(U))<<1)|1)|((logicaladdress) & 0xFFFFF000))
#define PAGINGTAG_S 0x10

#define NUMTLBS 16

//Read TLB LWUDS parameter!
#define Paging_readTLBLWUDS(logicaladdress,W,U,D,S) Paging_generateTAG(logicaladdress,W,U,D,S)

//1=User, 0=Supervisor
#define getUserLevel(CPL) ((CPL&1)&(CPL>>1))

extern byte advancedlog; //Advanced log setting
extern byte MMU_logging; //Are we logging from the MMU?

//Extra bits for debugging
//Instruction fault to bit 4?
//#define PF_EXTRABITS ((isPrefetch&2)<<3)
#define PF_EXTRABITS 0

void raisePF(uint_32 address, word flags)
{
	if ((MMU_logging == 1) && advancedlog) //Are we logging?
	{
		dolog("debugger","#PF fault(%08X,%08X)!",address,flags);
	}
	CPU[activeCPU].registers->CR2 = address; //Fill CR2 with the address cause!
	CPU[activeCPU].PFflags = flags; //Save a copy of the flags for debugging purposes!
	//Call interrupt!
	if (CPU_faultraised(EXCEPTION_PAGEFAULT)) //Fault raising exception!
	{
		CPU_resetOP(); //Go back to the start of the instruction!
		CPU_onResettingFault(0); //Set the fault data!
		CPU_executionphase_startinterrupt(EXCEPTION_PAGEFAULT,2|8,(int_64)flags); //Call IVT entry #13 decimal!
		//Execute the interrupt!
		CPU[activeCPU].faultraised = 1; //We have a fault raised, so don't raise any more!
	}
}

byte verifyCPL(byte iswrite, byte userlevel, byte PDERW, byte PDEUS, byte PTERW, byte PTEUS, byte *isWritable) //userlevel=CPL or 0 (with special instructions LDT, GDT, TSS, IDT, ring-crossing CALL/INT)
{
	byte uslevel; //Combined US level! 0=Supervisor, 1=User
	byte rwlevel; //Combined RW level! 1=Writable, 0=Not writable
	byte combinedrwlevel;
	uslevel = ((PDEUS&PTEUS) & 1); //User level page? This is the combined U/S!
	combinedrwlevel = ((PDERW&PTERW) & 1); //Are we writable is the combination of both flags? This is the combined R/W!
	if (uslevel) //User level?
	{
		rwlevel = combinedrwlevel; //Use the combined R/W level!
	}
	else //System? Allow read/write if supervisor only! Otherwise, fault!
	{
		rwlevel = 1; //Are we writable, from either kernel or user perspective? This is the combined R/W!
		//We're the kernel? Special privileges to apply!
		rwlevel |= ((CPU[activeCPU].registers->CR0 & 0x10000) && (EMULATED_CPU >= CPU_80486)) ? 2 : 0; //Set bit 2 when to inhibit writes to read-only pages at kernel level, otherwise legacy, allow all writes on kernel level!
	}
	//Now that we know the read/write permissions and user level, determine errors!
	if ((uslevel==0) && userlevel) //Supervisor access by user isn't allowed!
	{
		return 0; //Fault: system access by user privilege!
	}
	//rwlevel: Check for illegal writes first!
	if ((rwlevel!=1) && iswrite) //Write to read-only user page for any privilege level?
	{
		if (userlevel || ((rwlevel&2) && (!combinedrwlevel))) //We're at user level or write-protect enabled supervisor which is supposed to error out on writes? Fault!
		{
			return 0; //Fault: read-only write by user/supervisor!
		}
		else //Allow all writes to said page! At this point we're sure it's a writable page for the kernel level and a kernel level access!
		{
			rwlevel = 1; //Force allow on kernel level for this and any future writes to this entry in the TLB!
		}
	}
	//Read accesses are always valid if reached here. Do some special Read access+CR0.WP check!
	else if ((rwlevel == 3) && (!iswrite)) //Read, but special case for kernel read access with CR0.WP is to be applied for the cached TLB? Don't error out, as this read access is valid!
	{
		/*
		We check out as being accessable (whether by kernel privileges or not) during a read, but mark us read-only when the combined
		level isn't writable due to CR0.WP being in effect.
		Take the usual combined R/W level for kernel pages as well for any future references after this access. This prevents
		writes when not allowed to pass through when cached during reads, but error checking to be done again(due to TLB mismatch)
		when a write occurs after the read is cached.
		This provides supervisor-mode sensitivity to write-protected pages, no matter if it's supervisor-level or user-level.
		*/
		rwlevel = combinedrwlevel; //Become the combined R/W level for storing in the TLB after all(as per the WP bit). 
	}
	/*
	Otherwise, either a non-CR0.WP read-only page(always valid to cache normally, being value of 0) or a non-CR0.WP writable page(being value 1) without the CR0.WP being in effect, so nothing special needs to be done.
	Values 2 and 3 for writes are already handled by the first check.
	Value 2 for reads has the writeable bit already cleared, so no problems with newer writes.
	Value 3 for reads is handled by the else-clause, translating to the combined R/W level.
	So all the cases result in a rwlevel of 1 when writable(and not errored out) or 0/2 when readable.
	Thus bit 0 is always the writeability of said page, even during CR0.WP being in effect.
	*/

	*isWritable = (rwlevel&1); //Are we writable?
	return 1; //OK: verified!
}

OPTINLINE uint_32 getusedTLBindex(byte S, uint_32 logicaladdress)
{
	INLINEREGISTER uint_32 base;
	INLINEREGISTER byte is4K,S2; //Is a 4KB page(is4K) or large(S2) page? Flipped of S bit(is4K) or unflipped. Both 1 bit wide!
	is4K = !S; //Flipped S bit means 4KB page! Perform NOT instead of XOR to guarantee a 1-bit value to use!
	S2 = (is4K^1); //S bit, which doubles as a 2MB/4MB identifier for the base adddress and to be combined with PAE being enabled for adding 1 bit to the table index bits for looking up!
	base = (S2 << 20); //Base address within the lookup table (1MB for 4MB/2MB or 0 for 4KB)!
	S2 &= CPU[activeCPU].Paging_TLB.PAEenabled; //Only apply one extra displacement bit of 2MB pages when PAE is enabled!
	is4K <<= 1; //Shift to the base position to become +0(not 4K page) or +2(4K page)!
	S2 |= is4K; //How much bits less than 22 page bits for the table index base bit (1 extra bit for 2MB entries. 10 extra bits for 4KB entries).
	is4K <<= 2; //Shift to the base position to become +0(not 4K page) or +8(4K page)!
	S2 |= is4K; //Add +8 to +2 if 4K to become 10 bits to shift for 4K pages, otherwise 0(for 4M/2M pages)!
	//The amount of bits to shift to obtain the frame number in out table is 22 for 4MB, 21 for 2MB, 12 for 4KB. Thus 22-S2. 
	return base + (logicaladdress>>(22 - S2)); //Shift the page number to it's location and add it's used base address in the table!
}

#define getUsedTLBentry(S,logicaladdress) CPU[activeCPU].Paging_TLB.TLB_usedlist_indexes[CPU[activeCPU].Paging_TLB.TLB_usedlist_index[getusedTLBindex(S, logicaladdress)]]
//Give the used TLB entry, if any (NULL for not found)!

//Automatic set determination when using a set number <0!
#define Paging_TLBSet(logicaladdress,S) ((((logicaladdress >> ((S << 3) | (S << 1))) & 0x7000) >> 12) | (S << 3))
//The set is determined by the lower 3 bits of the entry(according to the i486 programmer's reference manual), the memory block!
//Assume that 4MB entries do the same, but on a larger scale(10 times as large, due to 10 times more logical address bits being covered)!

//Move a TLB entry index from an old list to a new list!
OPTINLINE void Paging_moveListItem(TLB_ptr* listitem, TLB_ptr** newlist_head, TLB_ptr** newlist_tail, TLB_ptr** oldlist_head, TLB_ptr** oldlist_tail)
{
	if (likely(*newlist_head == listitem)) return; //Don't do anything when it's already at the correct spot!

	//First, remove us from the old head list!
	if (listitem->prev) //Do we have anything before us?
	{
		((TLB_ptr*)listitem->prev)->next = listitem->next; //Remove us from the previous item of the list!
	}
	else //We're the head, so remove us from the list!
	{
		*oldlist_head = listitem->next; //Remove us from the head of the list and assign the new head!
	}

	if (listitem->next) //Did we have a next item?
	{
		((TLB_ptr*)listitem->next)->prev = listitem->prev; //Remove us from the next item of the list!
	}
	else //We're the tail?
	{
		*oldlist_tail = listitem->prev; //Remove us from the tail of the list and assign the new tail!
	}

	listitem->next = NULL; //We don't have a next!
	listitem->prev = NULL; //We don't have a previous!

	/* Now, we're removed from the old list and a newly unmapped item! */

	//Now, insert us into the start of the new list!
	if (*newlist_head) //Anything in the new list already?
	{
		(*newlist_head)->prev = listitem; //We're at the start of the new list, so point the head to us, us to the head and make us the new head!
		listitem->next = *newlist_head; //Our next is the old head!
		*newlist_head = listitem; //We're the new head!
	}
	else //We're the new list?
	{
		*newlist_head = listitem; //We're the new head!
		*newlist_tail = listitem; //We're the new tail!
	}
}

OPTINLINE TLB_ptr* allocTLB(sbyte set) //Allocate a TLB entry!
{
	TLB_ptr* result;
	if (CPU[activeCPU].Paging_TLB.TLB_freelist_head[set]) //Anything available?
	{
		result = CPU[activeCPU].Paging_TLB.TLB_freelist_head[set]; //What item are we allocating, take it from the free list!
		//Now take the item from the pool and move it to the used list!
		CPU[activeCPU].Paging_TLB.TLB_freelist_head[set]->allocated = 1; //We're allocated now!
		Paging_moveListItem(result, //What item to take!
			&CPU[activeCPU].Paging_TLB.TLB_usedlist_head[set], //destination head
			&CPU[activeCPU].Paging_TLB.TLB_usedlist_tail[set], //destination tail
			&CPU[activeCPU].Paging_TLB.TLB_freelist_head[set], //source head
			&CPU[activeCPU].Paging_TLB.TLB_freelist_tail[set]); //source tail
		return result; //Give the result!
	}
	return NULL; //Nothing to allocate!
}


OPTINLINE void freeTLB(sbyte set, TLB_ptr* listitem) //Make an entry available again!
{
	if (listitem->allocated) //Are we allocated at all?
	{
		listitem->allocated = 0; //Mark us as freed!
		CPU[activeCPU].Paging_TLB.TLB_usedlist_index[listitem->memoryindex] = 0; //Deallocated!
		listitem->entry->TAG = 0; //Clear the entry to unused!
		Paging_moveListItem(listitem, //What item to take!
			&CPU[activeCPU].Paging_TLB.TLB_freelist_head[set], //destination head
			&CPU[activeCPU].Paging_TLB.TLB_freelist_tail[set], //destination tail
			&CPU[activeCPU].Paging_TLB.TLB_usedlist_head[set], //source head
			&CPU[activeCPU].Paging_TLB.TLB_usedlist_tail[set]); //source tail
	}
}

OPTINLINE void Paging_setNewestTLB(sbyte set, TLB_ptr* listitem) //Tick an TLB entry for making it the most recently used!
{
	if (listitem->allocated) //Are we allocated at all?
	{
		Paging_moveListItem(listitem,
			&CPU[activeCPU].Paging_TLB.TLB_usedlist_head[set],
			&CPU[activeCPU].Paging_TLB.TLB_usedlist_tail[set],
			&CPU[activeCPU].Paging_TLB.TLB_usedlist_head[set],
			&CPU[activeCPU].Paging_TLB.TLB_usedlist_tail[set]); //Move us to the start of the TLB used list to mark us as the most recently accessed!
	}
	else //We're not allocated, but marked as newest? Allocate us!
	{
		//Now take the item from the pool and move it to the used list!
		listitem->allocated = 1; //We're allocated now!
		Paging_moveListItem(listitem, //What item to take!
			&CPU[activeCPU].Paging_TLB.TLB_usedlist_head[set], //destination head
			&CPU[activeCPU].Paging_TLB.TLB_usedlist_tail[set], //destination tail
			&CPU[activeCPU].Paging_TLB.TLB_freelist_head[set], //source head
			&CPU[activeCPU].Paging_TLB.TLB_freelist_tail[set]); //source tail
	}
}

//RWDirtyMask: mask for ignoring set bits in the tag, use them otherwise!
OPTINLINE byte Paging_readTLB(byte* TLB_way, uint_32 logicaladdress, uint_32 LWUDS, byte S, uint_32 WDMask, uint_64* result, uint_32* passthroughmask, byte updateAges)
{
	INLINEREGISTER uint_32 TAG, TAGMask;
	INLINEREGISTER TLB_ptr* curentry;
	//Valid list, so search through it!
	curentry = getUsedTLBentry(S, logicaladdress); //The entry to try!
	if (likely(curentry)) //Check all entries that are allocated!
	{
		TAGMask = ~WDMask; //Store for fast usage to mask the tag bits unused off!
		TAGMask &= curentry->entry->addrmaskset; //The full search mask, with the address width(KB vs MB) applied!
		TAG = LWUDS; //Generate a TAG!
		TAG &= TAGMask; //Premask the search tag for faster comparison!
		if (likely((curentry->entry->TAG & TAGMask) == TAG)) //Found and allocated?
		{
			*result = curentry->entry->data; //Give the stored data!
			*passthroughmask = curentry->entry->passthroughmask; //What bits to pass through!
			if (unlikely(TLB_way)) //Requested way?
			{
				*TLB_way = curentry->index; //The way found!; //What way was found!
			}
			Paging_setNewestTLB(Paging_TLBSet(logicaladdress, S), curentry); //Set us as the newest TLB!
			return 1; //Found!
		}
		//Otherwise, allocated, but invalid for use for this case.
	}
	return 0; //Not found to be valid to use for this case!
}

void Paging_freeOppositeTLB(uint_32 logicaladdress, byte W, byte U, byte D, byte S); //To free the opposite TLB when writing the TLB!
void Paging_writeTLB(sbyte TLB_way, uint_32 logicaladdress, byte W, byte U, byte D, byte S, byte G, uint_32 passthroughmask, byte is2M, uint_64 result); //To actually commit a TLB entry!

//isPrefetch: bit 0=set during prefetches, bit 1=Code read when set, bit 2=Suppress faults
byte isvalidpage(uint_32 address, byte iswrite, byte CPL, byte isPrefetch) //Do we have paging without error? userlevel=CPL usually.
{
	word DIR, TABLE;
	byte PTEUPDATED = 0, PDEUPDATED = 0; //Not update!
	uint_64 PDE, PTE=0, PDPT=0; //PDE/PTE entries currently used!
	uint_64 PXEsize = PXE_ADDRESSMASK; //The mask to use for the PDE/PTE entries!
	uint_64 PDEbase;
	byte PDEsize = 2, PTEsize = 2; //Size of an entry in the PDE/PTE, in shifts(2^n)!
	byte isPAE; //PAE is enabled?
	uint_32 passthroughmask;
	if (!CPU[activeCPU].registers) return 0; //No registers available!
	DIR = (address>>22)&0x3FF; //The directory entry!
	TABLE = (address>>12)&0x3FF; //The table entry!
	
	byte effectiveUS;
	byte RW;
	byte isS;
	byte isG; //G set in any entry?
	byte useG; //G enabled in processor and used?
	isG = 0; //Default: no Global enabled!
	useG = ((EMULATED_CPU>=CPU_PENTIUMPRO) & ((CPU[activeCPU].registers->CR4&0x80)>>7)); //Global emulated and enabled?
	uint_32 tag;
	RW = iswrite?1:0; //Are we trying to write?
	effectiveUS = getUserLevel(CPL); //Our effective user level!

	uint_64 temp;
	if (likely(RW==0)) //Are we reading? Allow all other combinations of dirty/read/write to be used for this!
	{
		tag = Paging_readTLBLWUDS(address,1, effectiveUS, 0, 1); //Large page tag!
		if (unlikely(EMULATED_CPU >= CPU_PENTIUM)) //Needs 4MB pages?
		{
			if (likely(Paging_readTLB(NULL, address, tag,1,TLB_IGNOREREADMASK, &temp,&passthroughmask, 0))) //Cache hit (non)dirty for reads/writes?
			{
				return 1; //Valid!
			}
		}
		tag &= ~PAGINGTAG_S; //Without S-bit!
		if (likely(Paging_readTLB(NULL, address, tag,0,TLB_IGNOREREADMASK, &temp,&passthroughmask,0))) //Cache hit (non)dirty for reads/writes?
		{
			return 1; //Valid!
		}
	}
	else //Write?
	{
		tag = Paging_readTLBLWUDS(address,1, effectiveUS, 1, 1); //Large page tag!
		if (unlikely(EMULATED_CPU >= CPU_PENTIUM)) //Needs 4MB pages?
		{
			if (likely(Paging_readTLB(NULL, address, tag,1,0, &temp, &passthroughmask, 0))) //Cache hit dirty for writes?
			{
				return 1; //Valid!
			}
		}
		tag &= ~PAGINGTAG_S; //Without S-bit!
		if (likely(Paging_readTLB(NULL, address, tag,0,0, &temp, &passthroughmask, 0))) //Cache hit dirty for writes?
		{
			return 1; //Valid!
		}
	}

	if (isPrefetch&1) return 0; //Stop the prefetch when not in the TLB!

	#ifdef PAGING_LOCKED
	if (BIU_obtainbuslock()) //Obtaining the bus lock?
	{
		CPU[activeCPU].executed = 0; //Didn't finish executing yet!
		if (EUphasehandlerrequiresreset()) //Requires reset to execute properly?
		{
			CPU_onResettingFault(1); //Set the fault data to restart any instruction-related things!
			if ((MMU_logging == 1) && advancedlog) //Are we logging?
			{
				dolog("debugger", "Paging pending (full instruction state reset)!");
			}
		}
		else
		{
			if ((MMU_logging == 1) && advancedlog) //Are we logging?
			{
				dolog("debugger", "Paging pending!");
			}
		}
		return 2; //Stop and wait to obtain the bus lock first!
	}
	else
	{
		if ((MMU_logging == 1) && advancedlog) //Are we logging?
		{
			dolog("debugger", "Paging pending: bus locked!");
		}
	}
	#endif

	if (CPU[activeCPU].Paging_TLB.PAEenabled) //PAE enabled?
	{
		PDPT = (uint_64)memory_BIUdirectrdw(CR3_PAEPDBR + ((DIR>>8) << 3)); //Read the page directory entry low!
		PDPT |= ((uint_64)memory_BIUdirectrdw(CR3_PAEPDBR + (((DIR >> 8) << 3)|4)))<<32; //Read the page directory entry high!
		PDEsize = PTEsize = 3; //Double the size of the entries to be 64-bits!
		PXEsize = PXE_PAEADDRESSMASK; //Large address mask!
		//Check present only!
		if (!(PDPT & PXE_P)) //Not present?
		{
			CPU[activeCPU].PageFault_PDPT = PDPT; //Save!
			CPU[activeCPU].PageFault_PDE = 0; //Save!
			CPU[activeCPU].PageFault_PTE = 0; //Save!
			if (!(isPrefetch&4)) //To fault?
			{
				raisePF(address, PF_EXTRABITS | (RW << 1) | (effectiveUS << 2)); //Run a not present page fault!
			}
			return 0; //We have an error, abort!
		}

		//The PDPT has no protection bits?

		DIR = (address >> 21) & 0x1FF; //The directory entry has half the entries!
		TABLE = (address >> 12) & 0x1FF; //The table entry has half the entries!

		//Check PDE
		PDEbase = (PDPT&PXEsize); //What is the PDE base address!
		PDE = (uint_64)memory_BIUdirectrdw((PDPT&PXEsize) + (DIR << 3)); //Read the page directory entry low!
		PDE |= ((uint_64)memory_BIUdirectrdw((PDPT&PXEsize) + ((DIR << 3)|4)))<<32; //Read the page directory entry high!
		if (!(PDE & PXE_P)) //Not present?
		{
			CPU[activeCPU].PageFault_PDPT = PDPT; //Save!
			CPU[activeCPU].PageFault_PDE = PDE; //Save!
			CPU[activeCPU].PageFault_PTE = 0; //Save!
			if (!(isPrefetch&4)) //To fault?
			{
				raisePF(address, PF_EXTRABITS | (RW << 1) | (effectiveUS << 2)); //Run a not present page fault!
			}
			return 0; //We have an error, abort!
		}
		isS = ((PDE & PDE_S) >> 7); //Large page of 2M? Is mandatory to be supported! CR4's PSE bit is ignored!
		isPAE = 1; //Enforce reserved bits!
	}
	else //PAE disabled?
	{
		PDEbase = CR3_PDBR; //What is the PDE base address!
		//Check PDE
		PDE = (uint_64)memory_BIUdirectrdw(PDEbase + (DIR << 2)); //Read the page directory entry!
		if (!(PDE & PXE_P)) //Not present?
		{
			CPU[activeCPU].PageFault_PDPT = 0; //Save!
			CPU[activeCPU].PageFault_PDE = PDE; //Save!
			CPU[activeCPU].PageFault_PTE = 0; //Save!
			if (!(isPrefetch&4)) //To fault?
			{
				raisePF(address, PF_EXTRABITS | (RW << 1) | (effectiveUS << 2)); //Run a not present page fault!
			}
			return 0; //We have an error, abort!
		}
		isS = ((((PDE & PDE_S) >> 7) & ((CPU[activeCPU].registers->CR4 & 0x10) >> 4) & (EMULATED_CPU >= CPU_PENTIUM)) & 1); //Effective size!
		isPAE = 0; //Don't enforce reserved bits!
	}

	//Check PTE
	if (likely(isS == 0)) //Not 4MB?
	{
		PTE = (uint_64)memory_BIUdirectrdw(((PDE&PXEsize) >> PXE_ADDRESSSHIFT) + (TABLE << PTEsize)); //Read the page table entry!
		if (PTEsize == 3) //Needs high half too?
		{
			PTE |= (((uint_64)memory_BIUdirectrdw(((PDE & PXEsize) >> PXE_ADDRESSSHIFT) + ((TABLE << PTEsize)|4)))<<32); //Read the page table entry!
		}
		if (!(PTE&PXE_P)) //Not present?
		{
			CPU[activeCPU].PageFault_PDPT = PDPT; //Save!
			CPU[activeCPU].PageFault_PDE = PDE; //Save!
			CPU[activeCPU].PageFault_PTE = PTE; //Save!
			if (!(isPrefetch&4)) //To fault?
			{
				raisePF(address, PF_EXTRABITS | (RW << 1) | (effectiveUS << 2)); //Run a not present page fault!
			}
			return 0; //We have an error, abort!
		}
	}

	if (unlikely(isS)) //4MB? Only check the PDE, not the PTE!
	{
		if ((PDE & (0x3FF000 ^ (isPAE << 21))) && ((((CPU[activeCPU].registers->CR4 & 0x10) >> 4) & (EMULATED_CPU >= CPU_PENTIUM)) | isPAE)) //Reserved bit in PDE?
		{
			CPU[activeCPU].PageFault_PDPT = PDPT; //Save!
			CPU[activeCPU].PageFault_PDE = PDE; //Save!
			CPU[activeCPU].PageFault_PTE = 0; //Save!
			if (!(isPrefetch&4)) //To fault?
			{
				raisePF(address, PF_EXTRABITS | PXE_P | (RW << 1) | (effectiveUS << 2) | 8); //Run a reserved page fault!
			}
			return 0; //We have an error, abort!
		}
		if (!verifyCPL(RW,effectiveUS,((PDE&PXE_RW)>>1),((PDE&PXE_US)>>2),((PDE&PXE_RW)>>1),((PDE&PXE_US)>>2),&RW)) //Protection fault on combined flags?
		{
			CPU[activeCPU].PageFault_PDPT = PDPT; //Save!
			CPU[activeCPU].PageFault_PDE = PDE; //Save!
			CPU[activeCPU].PageFault_PTE = 0; //Save!
			if (!(isPrefetch&4)) //To fault?
			{
				raisePF(address, PF_EXTRABITS | PXE_P|(RW<<1)|(effectiveUS<<2)); //Run a not present page fault!
			}
			return 0; //We have an error, abort!		
		}
		isG = ((PDE>>8)&1); //Bit 8=Global!
	}
	else //4KB?
	{
		if (!verifyCPL(RW, effectiveUS, ((PDE&PXE_RW) >> 1), ((PDE&PXE_US) >> 2), ((PTE&PXE_RW) >> 1), ((PTE&PXE_US) >> 2), &RW)) //Protection fault on combined flags?
		{
			CPU[activeCPU].PageFault_PDPT = PDPT; //Save!
			CPU[activeCPU].PageFault_PDE = PDE; //Save!
			CPU[activeCPU].PageFault_PTE = PTE; //Save!
			if (!(isPrefetch&4)) //To fault?
			{
				raisePF(address, PF_EXTRABITS | PXE_P | (RW << 1) | (effectiveUS << 2)); //Run a not present page fault!
			}
			return 0; //We have an error, abort!		
		}
		isG = ((PTE>>8)&1); //Bit 8=Global!
	}
	//RW=Are we writable?
	if (likely(isS == 0)) //PTE-only?
	{
		if (iswrite) //Writing and marking dirty?
		{
			if (unlikely(!(PTE&PTE_D)))
			{
				PTEUPDATED = 1; //Updated!
				PTE |= PTE_D; //Dirty!
			}
		}
	}
	else //Large page?
	{
		if (iswrite) //Writing and marking dirty?
		{
			if (unlikely(!(PDE&PDE_Dirty)))
			{
				PDEUPDATED = 1; //Updated!
				PDE |= PDE_Dirty; //Dirty!
			}
		}
	}
	if (!(PDE&PXE_A)) //Not accessed yet?
	{
		PDE |= PXE_A; //Accessed!
		PDEUPDATED = 1; //Updated!
	}
	if (likely(isS == 0)) //PTE-only?
	{
		if (!(PTE&PXE_A))
		{
			PTEUPDATED = 1; //Updated!
			PTE |= PXE_A; //Accessed!
		}
	}
	if (PDEUPDATED) //Updated?
	{
		memory_BIUdirectwdw(PDEbase + (DIR << PDEsize), (uint_32)PDE); //Update in memory!
		BIU_terminatemem(); //Terminate memory access!
		if (PDEsize == 3) //Needs high half too?
		{
			memory_BIUdirectwdw(PDEbase + ((DIR << PDEsize) | 4), (uint_32)(PDE>>32)); //Update in memory!
			BIU_terminatemem(); //Terminate memory access!
		}
	}
	if (PTEUPDATED) //Updated?
	{
		memory_BIUdirectwdw(((PDE&PXEsize)>>PXE_ADDRESSSHIFT)+(TABLE<<PTEsize),(uint_32)PTE); //Update in memory!
		BIU_terminatemem(); //Terminate memory access!
		if (PDEsize == 3) //Needs high half too?
		{
			memory_BIUdirectwdw(((PDE& PXEsize) >> PXE_ADDRESSSHIFT) + ((TABLE << PTEsize) | 4), (uint_32)(PTE>>32)); //Update in memory!
			BIU_terminatemem(); //Terminate memory access!
		}
	}
	Paging_freeOppositeTLB(address, RW, effectiveUS, (isS == 0) ? ((PTE&PTE_D) ? 1 : 0) : ((PDE&PDE_Dirty) ? 1 : 0), isS); //Clear the opposite TLB entry from existence!
	Paging_writeTLB(-1,address,RW,effectiveUS,(isS==0)?((PTE&PTE_D)?1:0):((PDE&PDE_Dirty)?1:0),isS,(isG&useG), (((isS == 0) ? (PXE_ACTIVEMASK) : (isPAE?PDE_PAELARGEACTIVEMASK:PDE_LARGEACTIVEMASK))), (((isS == 0) ? (0) : (isPAE?1:0))), (((isS==0)?(PTE&(isPAE?PXE_PAEADDRESSMASK:PXE_ADDRESSMASK)):(PDE&(isPAE?PDE_PAELARGEADDRESSMASK:PDE_LARGEADDRESSMASK))))); //Save the PTE 32-bit address in the TLB! PDE is always dirty when using 2MB/4MB pages!
	return 1; //Valid!
}

void Paging_debuggerTLB(sbyte TLB_way, uint_32 logicaladdress, byte W, byte U, byte D, byte S, byte G, uint_32 passthroughmask, byte is2M, uint_64 result, TLBEntry* curentry); //Special for address translation!
byte CPU_paging_translateaddr(uint_32 address, byte CPL, uint_64 *physaddr) //Do we have paging without error? userlevel=CPL usually.
{
	TLBEntry curentry;
	word DIR, TABLE;
	uint_64 PDE, PTE = 0, PDPT = 0; //PDE/PTE entries currently used!
	uint_64 PXEsize = PXE_ADDRESSMASK; //The mask to use for the PDE/PTE entries!
	uint_64 PDEbase;
	byte PTEsize = 2; //Size of an entry in the PDE/PTE, in shifts(2^n)!
	byte isPAE; //PAE is enabled?
	*physaddr = address; //Default: untranslated (special case for this address translation)!
	if (!CPU[activeCPU].registers) return 0; //No registers available!
	DIR = (address >> 22) & 0x3FF; //The directory entry!
	TABLE = (address >> 12) & 0x3FF; //The table entry!

	if (CPU[activeCPU].is_paging == 0) //Not paging?
	{
		return 1; //Always map to physical!
	}

	byte effectiveUS;
	byte RW;
	byte isS;
	byte isG; //G set in any entry?
	byte useG; //G enabled in processor and used?
	RW = 0; //Are we trying to write?
	effectiveUS = getUserLevel(CPL); //Our effective user level!
	isG = 0; //Default: no Global enabled!
	useG = ((EMULATED_CPU >= CPU_PENTIUMPRO) & ((CPU[activeCPU].registers->CR4 & 0x80) >> 7)); //Global emulated and enabled?

	if (CPU[activeCPU].Paging_TLB.PAEenabled) //PAE enabled?
	{
		PDPT = (uint_64)memory_BIUdirectrdw(CR3_PAEPDBR + ((DIR >> 8) << 3)); //Read the page directory entry low!
		PDPT |= ((uint_64)memory_BIUdirectrdw(CR3_PAEPDBR + (((DIR >> 8) << 3) | 4))) << 32; //Read the page directory entry high!
		PTEsize = 3; //Double the size of the entries to be 64-bits!
		PXEsize = PXE_PAEADDRESSMASK; //Large address mask!
		//Check present only!
		if (!(PDPT & PXE_P)) //Not present?
		{
			//CPU[activeCPU].PageFault_PDPT = PDPT; //Save!
			//CPU[activeCPU].PageFault_PDE = 0; //Save!
			//CPU[activeCPU].PageFault_PTE = 0; //Save!
			return 0; //We have an error, abort!
		}

		//The PDPT has no protection bits?

		DIR = (address >> 21) & 0x1FF; //The directory entry has half the entries!
		TABLE = (address >> 12) & 0x1FF; //The table entry has half the entries!

		//Check PDE
		PDEbase = (PDPT & PXEsize); //What is the PDE base address!
		PDE = (uint_64)memory_BIUdirectrdw((PDPT & PXEsize) + (DIR << 3)); //Read the page directory entry low!
		PDE |= ((uint_64)memory_BIUdirectrdw((PDPT & PXEsize) + ((DIR << 3) | 4))) << 32; //Read the page directory entry high!
		if (!(PDE & PXE_P)) //Not present?
		{
			//CPU[activeCPU].PageFault_PDPT = PDPT; //Save!
			//CPU[activeCPU].PageFault_PDE = PDE; //Save!
			//CPU[activeCPU].PageFault_PTE = 0; //Save!
			return 0; //We have an error, abort!
		}
		isS = ((PDE & PDE_S) >> 7); //Large page of 2M? Is mandatory to be supported! CR4's PSE bit is ignored!
		isPAE = 1; //Enforce reserved bits!
	}
	else //PAE disabled?
	{
		PDEbase = CR3_PDBR; //What is the PDE base address!
		//Check PDE
		PDE = (uint_64)memory_BIUdirectrdw(PDEbase + (DIR << 2)); //Read the page directory entry!
		if (!(PDE & PXE_P)) //Not present?
		{
			//CPU[activeCPU].PageFault_PDPT = 0; //Save!
			//CPU[activeCPU].PageFault_PDE = PDE; //Save!
			//CPU[activeCPU].PageFault_PTE = 0; //Save!
			return 0; //We have an error, abort!
		}
		isS = ((((PDE & PDE_S) >> 7) & ((CPU[activeCPU].registers->CR4 & 0x10) >> 4) & (EMULATED_CPU >= CPU_PENTIUM)) & 1); //Effective size!
		isPAE = 0; //Don't enforce reserved bits!
	}

	//Check PTE
	if (likely(isS == 0)) //Not 4MB?
	{
		PTE = (uint_64)memory_BIUdirectrdw(((PDE & PXEsize) >> PXE_ADDRESSSHIFT) + (TABLE << PTEsize)); //Read the page table entry!
		if (PTEsize == 3) //Needs high half too?
		{
			PTE |= (((uint_64)memory_BIUdirectrdw(((PDE & PXEsize) >> PXE_ADDRESSSHIFT) + ((TABLE << PTEsize) | 4))) << 32); //Read the page table entry!
		}
		if (!(PTE & PXE_P)) //Not present?
		{
			//CPU[activeCPU].PageFault_PDPT = PDPT; //Save!
			//CPU[activeCPU].PageFault_PDE = PDE; //Save!
			//CPU[activeCPU].PageFault_PTE = PTE; //Save!
			return 0; //We have an error, abort!
		}
	}

	if (unlikely(isS)) //4MB? Only check the PDE, not the PTE!
	{
		if ((PDE & (0x3FF000 ^ (isPAE << 21))) && ((((CPU[activeCPU].registers->CR4 & 0x10) >> 4) & (EMULATED_CPU >= CPU_PENTIUM)) | isPAE)) //Reserved bit in PDE?
		{
			//CPU[activeCPU].PageFault_PDPT = PDPT; //Save!
			//CPU[activeCPU].PageFault_PDE = PDE; //Save!
			//CPU[activeCPU].PageFault_PTE = 0; //Save!
			return 0; //We have an error, abort!
		}
		if (!verifyCPL(RW, effectiveUS, ((PDE & PXE_RW) >> 1), ((PDE & PXE_US) >> 2), ((PDE & PXE_RW) >> 1), ((PDE & PXE_US) >> 2), &RW)) //Protection fault on combined flags?
		{
			//CPU[activeCPU].PageFault_PDPT = PDPT; //Save!
			//CPU[activeCPU].PageFault_PDE = PDE; //Save!
			//CPU[activeCPU].PageFault_PTE = 0; //Save!
			return 0; //We have an error, abort!		
		}
		isG = ((PDE >> 8) & 1); //Bit 8=Global!
	}
	else //4KB?
	{
		if (!verifyCPL(RW, effectiveUS, ((PDE & PXE_RW) >> 1), ((PDE & PXE_US) >> 2), ((PTE & PXE_RW) >> 1), ((PTE & PXE_US) >> 2), &RW)) //Protection fault on combined flags?
		{
			//CPU[activeCPU].PageFault_PDPT = PDPT; //Save!
			//CPU[activeCPU].PageFault_PDE = PDE; //Save!
			//CPU[activeCPU].PageFault_PTE = PTE; //Save!
			return 0; //We have an error, abort!		
		}
		isG = ((PTE >> 8) & 1); //Bit 8=Global!
	}

	//RW=Are we writable?
	//Use the debugging TLB creation to create a proper TLB entry to translate with!
	Paging_debuggerTLB(-1, address, RW, effectiveUS, (isS == 0) ? ((PTE & PTE_D) ? 1 : 0) : ((PDE & PDE_Dirty) ? 1 : 0), isS, (isG & useG), (((isS == 0) ? (PXE_ACTIVEMASK) : (isPAE ? PDE_PAELARGEACTIVEMASK : PDE_LARGEACTIVEMASK))), (((isS == 0) ? (0) : (isPAE ? 1 : 0))), (((isS == 0) ? (PTE & (isPAE ? PXE_PAEADDRESSMASK : PXE_ADDRESSMASK)) : (PDE & (isPAE ? PDE_PAELARGEADDRESSMASK : PDE_LARGEADDRESSMASK)))),&curentry); //'Save' the PTE 32-bit address in the unsaving TLB! PDE is always dirty when using 2MB/4MB pages!
	if (isS) //Large one?
	{
		*physaddr = (curentry.data | (address & curentry.passthroughmask)); //Give the actual address from the TLB!
	}
	else //Small one?
	{
		*physaddr = (curentry.data | (address & PXE_ACTIVEMASK)); //Give the actual address from the TLB!
	}
	return 1; //Valid!
}

byte CPU_Paging_checkPage(uint_32 address, word readflags, byte CPL)
{
	byte result;
	result = isvalidpage(address,((readflags&0x3)==0),CPL,((readflags&0x10)>>4)|(((readflags&2)&((readflags<<1)&2))|((readflags&0x100)>>6))); //Are we an invalid page? We've raised an error! Bit2 is set during Prefetch operations! Bit 1 is set during code accesses.
	if (result == 1) //OK?
	{
		return 0; //OK!
	}
	else if (result == 2) //Paging waiting for the lock?
	{
		return 2; //Paging waiting for the data!
	}
	return 1; //Error out?
}

uint_64 mappagenonPSE(uint_32 address, byte iswrite, byte CPL) //Maps a page to real memory when needed!
{
	uint_64 result; //What address?
	uint_32 passthroughmask;
	CPU[activeCPU].successfullpagemapping = 1; //Set the flag for debugging!
	if (is_paging()==0) return address; //Direct address when not paging!
	byte effectiveUS;
	byte RW;
	uint_32 tag;
	RW = iswrite?1:0; //Are we trying to write?
	effectiveUS = getUserLevel(CPL); //Our effective user level!
	if (unlikely(iswrite)) //Writes are limited?
	{
		tag = Paging_readTLBLWUDS(address,1, effectiveUS, 1, 0); //Small page tag!
		if (likely(Paging_readTLB(NULL,address, tag, 0, 0, &result, &passthroughmask, 1))) //Cache hit for a written dirty entry? Match completely only!
		{
			return (result|(address&PXE_ACTIVEMASK)); //Give the actual address from the TLB!
		}
	}
	else //Read?
	{
		tag = Paging_readTLBLWUDS(address,RW, effectiveUS, RW, 0); //Small paging tag!
		if (likely(Paging_readTLB(NULL, address, tag, 0, TLB_IGNOREREADMASK, &result, &passthroughmask, 1))) //Cache hit for an the entry, any during reads, Write Dirty on write?
		{
			return (result | (address&PXE_ACTIVEMASK)); //Give the actual address from the TLB!
		}
	}
	CPU[activeCPU].successfullpagemapping = 0; //Insuccessful: errored out!
	return address; //Untranslated!
}

uint_64 mappagePSE(uint_32 address, byte iswrite, byte CPL) //Maps a page to real memory when needed!
{
	uint_64 result; //What address?
	uint_32 passthroughmask;
	CPU[activeCPU].successfullpagemapping = 1; //Set the flag for debugging!
	if (is_paging() == 0) return address; //Direct address when not paging!
	byte effectiveUS;
	byte RW;
	uint_32 tag;
	RW = iswrite ? 1 : 0; //Are we trying to write?
	effectiveUS = getUserLevel(CPL); //Our effective user level!
	if (unlikely(iswrite)) //Writes are limited?
	{
		tag = Paging_readTLBLWUDS(address, 1, effectiveUS, 1, 1); //Large page tag!
		if (likely(Paging_readTLB(NULL, address, tag, 1, 0, &result, &passthroughmask, 1))) //Cache hit for a written dirty entry? Match completely only!
		{
			return (result | (address & passthroughmask)); //Give the actual address from the TLB!
		}
		tag &= ~PAGINGTAG_S; //Without S-bit!
		if (likely(Paging_readTLB(NULL, address, tag, 0, 0, &result, &passthroughmask, 1))) //Cache hit for a written dirty entry? Match completely only!
		{
			return (result | (address & PXE_ACTIVEMASK)); //Give the actual address from the TLB!
		}
	}
	else //Read?
	{
		tag = Paging_readTLBLWUDS(address, RW, effectiveUS, RW, 1); //Large paging tag!
		if (likely(Paging_readTLB(NULL, address, tag, 1, TLB_IGNOREREADMASK, &result, &passthroughmask, 1))) //Cache hit for an the entry, any during reads, Write Dirty on write?
		{
			return (result | (address & passthroughmask)); //Give the actual address from the TLB!
		}
		tag &= ~PAGINGTAG_S; //Without S-bit!
		if (likely(Paging_readTLB(NULL, address, tag, 0, TLB_IGNOREREADMASK, &result, &passthroughmask, 1))) //Cache hit for an the entry, any during reads, Write Dirty on write?
		{
			return (result | (address & PXE_ACTIVEMASK)); //Give the actual address from the TLB!
		}
	}
	CPU[activeCPU].successfullpagemapping = 0; //Insuccessful: errored out!
	return address; //Untranslated!
}

mappageHandler effectivemappageHandler = &mappagenonPSE; //Default to the non-PSE paging handler!

OPTINLINE void PagingTLB_initlists()
{
	byte set; //What set?
	byte index; //What index?
	byte setsize;
	byte indexsize;
	byte whichentry;
	setsize = /*(8<<((EMULATED_CPU>=CPU_80486)?1:0))*/ NUMTLBS;
	indexsize = /*(8>>((EMULATED_CPU>=CPU_80486)?1:0))*/ 4;
	for (set = 0; set < setsize; ++set) //process all sets!
	{
		//Allocate a list-to-entry-mapping from the available entry space, with all items in ascending order in a linked list and index!
		for (index = 0; index<indexsize; ++index) //process all indexes!
		{
			whichentry = (set*indexsize)+index; //Which one?
			CPU[activeCPU].Paging_TLB.TLB_listnodes[whichentry].entry = &CPU[activeCPU].Paging_TLB.TLB[whichentry]; //What entry(constant value)!
			CPU[activeCPU].Paging_TLB.TLB_listnodes[whichentry].index = index; //What index are we(for lookups)?
			CPU[activeCPU].Paging_TLB.TLB_listnodes[whichentry].entrynr = whichentry; //What index are we ourselves (for lookups)?
			CPU[activeCPU].Paging_TLB.TLB[whichentry].TLB_listnode = (void *)&CPU[activeCPU].Paging_TLB.TLB_listnodes[whichentry]; //Tell the TLB which node it belongs to!
		}
	}
}

OPTINLINE void PagingTLB_clearlists()
{
	byte set; //What set?
	byte index; //What index?
	TLB_ptr *us; //What is the current entry!
	byte setsize;
	byte indexsize;
	byte whichentry;
	setsize = /*(8<<((EMULATED_CPU>=CPU_80486)?1:0)) =*/ NUMTLBS;
	indexsize = /*(8>>((EMULATED_CPU>=CPU_80486)?1:0))*/ 4;
	for (set = 0; set < setsize; ++set) //process all sets!
	{
		//Allocate a list from the available entry space, with all items in ascending order in a linked list and index!
		CPU[activeCPU].Paging_TLB.TLB_freelist_head[set] = CPU[activeCPU].Paging_TLB.TLB_freelist_tail[set] = NULL; //Nothing!
		CPU[activeCPU].Paging_TLB.TLB_usedlist_head[set] = CPU[activeCPU].Paging_TLB.TLB_usedlist_tail[set] = NULL; //Nothing!
		for (index = (indexsize-1); ((index&0xFF)!=0xFF); --index) //process all indexes!
		{
			whichentry = (set*indexsize)+index; //Which one?
			CPU[activeCPU].Paging_TLB.TLB_listnodes[whichentry].allocated = 0; //We're in the free list!
			us = &CPU[activeCPU].Paging_TLB.TLB_listnodes[whichentry]; //What entry are we?
			us->prev = NULL; //We start out as the head for the added items here, so never anything before us!
			us->next = NULL; //We start out as the head, so next is automatically filled!
			if (likely(CPU[activeCPU].Paging_TLB.TLB_freelist_head[set])) //Head already set?
			{
				CPU[activeCPU].Paging_TLB.TLB_freelist_head[set]->prev = us; //We're the previous for the current head!
				us->next = CPU[activeCPU].Paging_TLB.TLB_freelist_head[set]; //Our next is the head!
			}
			CPU[activeCPU].Paging_TLB.TLB_freelist_head[set] = us; //We're the new head!
			if (unlikely(CPU[activeCPU].Paging_TLB.TLB_freelist_tail[set] == NULL)) //No tail yet?
			{
				CPU[activeCPU].Paging_TLB.TLB_freelist_tail[set] = CPU[activeCPU].Paging_TLB.TLB_freelist_head[set]; //Tail=Head when starting out!
			}
		}
	}
}

OPTINLINE TLBEntry *Paging_oldestTLB(byte S, sbyte set) //Find a TLB to be used/overwritten!
{
	TLB_ptr *listentry;
	if (CPU[activeCPU].Paging_TLB.TLB_freelist_head[set]) //Anything not allocated yet?
	{
		if ((listentry = allocTLB(set))) //Allocated from the free list?
		{
			return listentry->entry; //Give the index of the resulting entry that's been allocated!
		}
	}
	else //Allocate from the tail(LRU), since everything is allocated!
	{
		if (CPU[activeCPU].Paging_TLB.TLB_usedlist_tail[set]) //Gotten a tail? We're used, so take the LRU!
		{
			listentry = CPU[activeCPU].Paging_TLB.TLB_usedlist_tail[set]; //What entry to take: the LRU!
			Paging_setNewestTLB(set, listentry); //This is the newest TLB now!
			CPU[activeCPU].Paging_TLB.TLB_usedlist_index[listentry->memoryindex] = 0; //Replacing, so deallocated now!
			return listentry->entry; //What index is the LRU!
		}
	}
	byte indexsize, whichentry;
	indexsize = /*(8>>((EMULATED_CPU>=CPU_80486)?1:0))*/ 4;
	whichentry = (set*indexsize)+3; //Which one?

	listentry = (TLB_ptr*)CPU[activeCPU].Paging_TLB.TLB[whichentry].TLB_listnode; //The list entry!
	if (listentry->allocated) //Allocated?
	{
		CPU[activeCPU].Paging_TLB.TLB_usedlist_index[listentry->memoryindex] = 0; //Replacing, so deallocated now!
	}
	return listentry->entry; //Safety: return the final entry! Shouldn't happen under normal circumstances.
}

//W=Writable, U=User, D=Dirty
void Paging_writeTLB(sbyte TLB_way, uint_32 logicaladdress, byte W, byte U, byte D, byte S, byte G, uint_32 passthroughmask, byte is2M, uint_64 result)
{
	INLINEREGISTER TLBEntry* curentry = NULL;
	INLINEREGISTER TLB_ptr* effectiveentry;
	INLINEREGISTER uint_32 TAG;
	uint_32 addrmask;
	sbyte TLB_set;
	byte indexsize;
	byte whichentry;
	byte entry;
	TLB_set = Paging_TLBSet(logicaladdress, S); //Auto set?
	//Calculate and store the address mask for matching!
	addrmask = (~S) & 1; //Mask to 1 bit only. Become 0 when using 4MB(don't clear the high 10 bits), 1 for 4KB(clear the high 10 bits)!
	addrmask = 0x3FF >> ((addrmask << 3) | (addrmask << 1)); //Shift off the 4MB bits when using 4KB pages!
	addrmask >>= is2M; //2MB pages instead of 4MB pages?
	addrmask <<= 12; //Shift to page size addition of bits(12 bits)!
	addrmask |= 0xFFF; //Fill with the 4KB page mask to get a 4KB or 4MB page mask!
	addrmask = ~addrmask; //Negate the frame mask for a page mask!
	TAG = Paging_generateTAG(logicaladdress, W, U, D, S); //Generate a TAG!
	entry = 0; //Init for entry search not found!
	effectiveentry = CPU[activeCPU].Paging_TLB.TLB_usedlist_head[TLB_set]; //The first entry to verify, in order of MRU to LRU!
	if (TLB_way >= 0) //Way specified?
	{
		//Take way #n!
		entry = 1; //Force found!
		indexsize = /*(8>>((EMULATED_CPU>=CPU_80486)?1:0))*/ 4;
		whichentry = (TLB_set * indexsize) + TLB_way; //Which one?
		effectiveentry = &CPU[activeCPU].Paging_TLB.TLB_listnodes[whichentry]; //What entry are we?
		curentry = effectiveentry->entry; //The entry to use!
		//Mark us MRU!
		Paging_setNewestTLB(TLB_set, effectiveentry); //We're the newest TLB now!
		if (effectiveentry->allocated) //Already allocated?
		{
			CPU[activeCPU].Paging_TLB.TLB_usedlist_index[effectiveentry->memoryindex] = 0; //Replacing, so deallocated now!
		}
	}
	else //Try and find the way!
	{
		effectiveentry = getUsedTLBentry(S, logicaladdress); //The entry to try!
		if (effectiveentry) //If we found ourselves, always overwrite!
		{
			curentry = effectiveentry->entry; //The entry to use!
			Paging_setNewestTLB(TLB_set, effectiveentry); //We're the newest TLB now!
			entry = 1; //Reuse our own entry! We're found!
			CPU[activeCPU].Paging_TLB.TLB_usedlist_index[effectiveentry->memoryindex] = 0; //Replacing, so deallocated now!
		}
		else //Nothing allocated yet?
		{
			entry = 0; //Not found!
		}
	}
	//We reach here from the loop when nothing is found in the allocated list!
	if (unlikely(entry == 0)) //Not found? Take the LRU!
	{
		curentry = Paging_oldestTLB(S, TLB_set); //Get the oldest/unused TLB!
	}
	//Fill the found entry with our (new) data!
	curentry->data = result; //The result for the lookup!
	curentry->TAG = TAG; //The TAG to find it by!
	((TLB_ptr*)curentry->TLB_listnode)->memoryindex = getusedTLBindex(S, logicaladdress); //Save the memory index we're using when allocating!
	CPU[activeCPU].Paging_TLB.TLB_usedlist_index[((TLB_ptr*)curentry->TLB_listnode)->memoryindex] = ((((TLB_ptr*)curentry->TLB_listnode)->entrynr) + 1); //Allocated!
	curentry->addrmask = addrmask; //Save the address mask for matching a TLB entry after it's stored!
	curentry->addrmaskset = (addrmask | 0xFFF); //Save the address mask for matching a TLB entry after it's stored!
	curentry->passthroughmask = passthroughmask; //Save the passthrough mask for giving a physical address!
	curentry->isglobal = G; //Global or not!
	BIU_recheckmemory(); //Recheck anything that's fetching from now on!
}

//Build for age entry!
#define AGEENTRY_AGEENTRY(age,entry) (signed2unsigned8(age)|(entry<<8))
//What to sort by!
#define AGEENTRY_SORT(entry) unsigned2signed8(entry)
//Deconstruct for age entry!
#define AGEENTRY_AGE(entry) unsigned2signed8(entry)
#define AGEENTRY_ENTRY(entry) (entry>>8)

#define SWAP(a,b) if (unlikely(AGEENTRY_SORT(sortarray[b]) < AGEENTRY_SORT(sortarray[a]))) { tmp = sortarray[a]; sortarray[a] = sortarray[b]; sortarray[b] = tmp; }

void Paging_freeOppositeTLB(uint_32 logicaladdress, byte W, byte U, byte D, byte S)
{
	INLINEREGISTER TLB_ptr *effectiveentry, *nextentry;
	INLINEREGISTER uint_32 TAG, TAGMASKED;
	uint_32 addrmask, searchmask;
	sbyte TLB_set;
	S = ((~S)&1); //Opposite page size to search!
	TLB_set = Paging_TLBSet(logicaladdress, S); //Reversed size set!

	/* Invalidate any entries in the reversed size set(S=0 for S=1 and vice versa)! Always take the 4MB mask for either one of them(all associated 4MB pages for 4KB logical address or all associated 4KB pages for 4MB logical address) */
	//Calculate and store the address mask for matching!
	//Take 4MB pages for both 4MB and 4KB matches(only match based on 4MB granularity on the opposite size(4MB clearing all 4KB associated with it, 4KB clearing all 4MB associated with it))!
	addrmask = 0xFFC00000; //Only 4MB matching, even on 4KB pages! So that handles the opposite page size in 4MB blocks, matching all that handle the linear address!
	TAG = Paging_generateTAG(logicaladdress, W, U, D, 0); //Generate a TAG! A-bit and S-bit unused for this lookup!
	searchmask = (0x01 | addrmask); //Search mask! Ignore the S-bit and A-bit!
	TAGMASKED = (TAG&searchmask); //Masked tag for fast lookup! Match P/U/W/address only! Thus dirty updates the existing entry, while other bit changing create a new entry!
	effectiveentry = CPU[activeCPU].Paging_TLB.TLB_usedlist_head[TLB_set]; //The first entry to verify, in order of MRU to LRU!
	for (; effectiveentry;) //Verify from MRU to LRU!
	{
		if (unlikely((effectiveentry->entry->TAG & searchmask) == TAGMASKED)) //Match for our own entry?
		{
			nextentry = (TLB_ptr *)(effectiveentry->next); //The next entry to check! Prefetch it, because it will be cleared during the freeing!
			freeTLB(TLB_set, effectiveentry); //Free this entry from the TLB!
			effectiveentry = nextentry; //Process the next entry in the list from the prefetch, because the effectiveentry has it's value cleared!
		}
		else
		{
			effectiveentry = (TLB_ptr *)(effectiveentry->next); //The next entry to check!
		}
	}
}

void Paging_debuggerTLB(sbyte TLB_way, uint_32 logicaladdress, byte W, byte U, byte D, byte S, byte G, uint_32 passthroughmask, byte is2M, uint_64 result, TLBEntry *curentry)
{
	INLINEREGISTER uint_32 TAG;
	uint_32 addrmask;
	//Calculate and store the address mask for matching!
	addrmask = (~S) & 1; //Mask to 1 bit only. Become 0 when using 4MB(don't clear the high 10 bits), 1 for 4KB(clear the high 10 bits)!
	addrmask = 0x3FF >> ((addrmask << 3) | (addrmask << 1)); //Shift off the 4MB bits when using 4KB pages!
	addrmask >>= is2M; //2MB pages instead of 4MB pages?
	addrmask <<= 12; //Shift to page size addition of bits(12 bits)!
	addrmask |= 0xFFF; //Fill with the 4KB page mask to get a 4KB or 4MB page mask!
	addrmask = ~addrmask; //Negate the frame mask for a page mask!
	TAG = Paging_generateTAG(logicaladdress, W, U, D, S); //Generate a TAG!

	//We reach here from the loop when nothing is found in the allocated list!
	//Fill the found entry with our (new) data!
	curentry->data = result; //The result for the lookup!
	curentry->TAG = TAG; //The TAG to find it by!
	curentry->addrmask = addrmask; //Save the address mask for matching a TLB entry after it's stored!
	curentry->addrmaskset = (addrmask | 0xFFF); //Save the address mask for matching a TLB entry after it's stored!
	curentry->passthroughmask = passthroughmask; //Save the passthrough mask for giving a physical address!
	curentry->isglobal = G; //Global or not!
}

OPTINLINE byte Paging_matchTLBaddress(uint_32 logicaladdress, uint_32 TAG, uint_32 TAGmask)
{
	return (((logicaladdress & TAGmask) | 1) == ((TAG & TAGmask) | (TAG & 1))); //The used TAG matches on address and availability only! Ignore US/RW!
}

void Paging_Invalidate(uint_32 logicaladdress) //Invalidate a single address!
{
	INLINEREGISTER byte TLB_set;
	INLINEREGISTER TLB_ptr *curentry, *nextentry;
	for (TLB_set = 0; TLB_set < NUMTLBS; ++TLB_set) //Process all possible sets!
	{
		curentry = CPU[activeCPU].Paging_TLB.TLB_usedlist_head[TLB_set]; //What TLB entry to apply?
		for (;curentry;) //Check all entries that are allocated!
		{
			nextentry = (TLB_ptr *)(curentry->next); //Next entry saved!
			if (Paging_matchTLBaddress(logicaladdress, curentry->entry->TAG, curentry->entry->addrmask)) //Matched and allocated?
			{
				freeTLB(TLB_set, curentry); //Free this entry from the TLB!
			}
			curentry = nextentry; //Next entry!
		}
	}
}

void Paging_clearTLB()
{
	//Remove all TLB entries that are not marked as global!
	INLINEREGISTER byte TLB_set;
	INLINEREGISTER TLB_ptr *curentry, *nextentry;
	for (TLB_set = 0; TLB_set < NUMTLBS; ++TLB_set) //Process all possible sets!
	{
		curentry = CPU[activeCPU].Paging_TLB.TLB_usedlist_head[TLB_set]; //What TLB entry to apply?
		for (;curentry;) //Check all entries that are allocated!
		{
			nextentry = (TLB_ptr *)(curentry->next); //Next entry saved!
			if (curentry->entry->isglobal==0) //Not global?
			{
				freeTLB(TLB_set, curentry); //Free this entry from the TLB!
			}
			curentry = nextentry; //Next entry!
		}
	}
	//Finish up!
	BIU_recheckmemory(); //Recheck anything that's fetching from now on!
}

void Paging_initTLB()
{
	INLINEREGISTER word i;
	INLINEREGISTER byte TLB_set;
	INLINEREGISTER TLB_ptr* curentry;
	CPU[activeCPU].Paging_TLB.PAEenabled = ((CPU[activeCPU].registers->CR4 & 0x20) && (EMULATED_CPU >= CPU_PENTIUMPRO)); //Is PAE enabled?
	//Clear any used list entries first!
	for (TLB_set = 0; TLB_set < NUMTLBS; ++TLB_set) //Process all possible sets!
	{
		curentry = CPU[activeCPU].Paging_TLB.TLB_usedlist_head[TLB_set]; //What TLB entry to apply?
		for (; curentry;) //Check all entries that are allocated!
		{
			CPU[activeCPU].Paging_TLB.TLB_usedlist_index[curentry->memoryindex] = 0; //Clear the memory allocation!
			curentry = (TLB_ptr*)(curentry->next); //Next entry to check, if any!
		}
	}
	//Load the used list indexes lookup table!
	for (i=0;i<256;++i) //Precalculate the lookup tables!
	{
		CPU[activeCPU].Paging_TLB.TLB_usedlist_indexes[i] = ((i && (i<=NUMITEMS(CPU[activeCPU].Paging_TLB.TLB_listnodes)))?&CPU[activeCPU].Paging_TLB.TLB_listnodes[i - 1]:NULL); //The TLB entry, if available!
	}
	PagingTLB_initlists(); //Initialize the TLB lists to become empty!
	PagingTLB_clearlists(); //Initialize the TLB lists to become empty!
	effectivemappageHandler = (EMULATED_CPU >= CPU_PENTIUM) ? &mappagePSE : &mappagenonPSE; //Use either a PSE or non-PSE paging handler!
	BIU_recheckmemory(); //Recheck anything that's fetching from now on!
}

void Paging_TestRegisterWritten(byte TR)
{
	byte P, D, DC, U, UC, W, WC;
	uint_32 logicaladdress = 0;
	uint_64 result = 0;
	uint_32 passthroughmask = 0;
	byte hit=0;
	if (unlikely(TR==6)) //TR6: Test command register
	{
		//We're given a command to execute!
		P = (CPU[activeCPU].registers->TR6&0x800)?1:0;
		D = (CPU[activeCPU].registers->TR6&0x400)?1:0;
		DC = (CPU[activeCPU].registers->TR6&0x200)?1:0;
		U = (CPU[activeCPU].registers->TR6&0x100)?1:0;
		UC = (CPU[activeCPU].registers->TR6&0x80)?1:0;
		W = (CPU[activeCPU].registers->TR6&0x40)?1:0;
		WC = (CPU[activeCPU].registers->TR6&0x20)?1:0;
		logicaladdress = CPU[activeCPU].registers->TR6&PXE_ADDRESSMASK; //What address!

		if (CPU[activeCPU].registers->TR6&1) //Lookup command?
		{
			if ((DC == (D ^ 1)) && (UC == (U ^ 1)) && (WC == (W ^ 1)) && P) //Valid complements?
			{
				if (Paging_readTLB(&hit, logicaladdress, Paging_readTLBLWUDS(logicaladdress,W, U, D, 0),0,0, &result, &passthroughmask, 1)) //Read?
				{
					++hit; //Hit where!
				}
				else hit = 0; //Not hit!
				if (hit==0) //Not hit?
				{
					noTRhit: //No hit after all?
					CPU[activeCPU].registers->TR7 &= ~0x10; //Not hit!
				}
				else //Hit?
				{
					CPU[activeCPU].registers->TR7 = (0x10 | ((hit - 1) << 2) | (result&PXE_ADDRESSMASK)); //Hit!
				}
			}
			else //Invalid components?
			{
				goto noTRhit; //Apply no hit!
			}
		}
		else //Write command?
		{
			if ((DC == (D ^ 1)) && (UC == (U ^ 1)) && (WC == (W ^ 1)) && P) //Valid complements?
			{
				if (CPU[activeCPU].registers->TR6 & 0x10) //Hit?
				{
					Paging_writeTLB((sbyte)((CPU[activeCPU].registers->TR7 >> 2) & 3), logicaladdress, W, U, D, 0, 0, PXE_ACTIVEMASK, 0, (result&PXE_ADDRESSMASK)); //Write to the associated block!
				}
				else //LRU algorithm?
				{
					Paging_writeTLB(-1, logicaladdress, W, U, D, 0, 0, PXE_ACTIVEMASK, 0, (result&PXE_ADDRESSMASK)); //Write to the associated block!
				}
			}
		}
	}
}
