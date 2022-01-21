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

//We're the BIU!
#define IS_BIU

#include "headers/cpu/biu.h" //Our own typedefs!
#include "headers/cpu/cpu.h" //CPU!
#include "headers/support/fifobuffer.h" //FIFO support!
#include "headers/cpu/protection.h" //Protection support!
#include "headers/cpu/mmu.h" //MMU support!
#include "headers/hardware/ports.h" //Hardware port support!
#include "headers/support/signedness.h" //Unsigned and signed support!
#include "headers/cpu/paging.h" //Paging support for paging access!
#include "headers/mmu/mmuhandler.h" //MMU direct access support!
#include "headers/emu/debugger/debugger.h" //Debugger support!
#include "headers/mmu/mmu_internals.h" //Internal MMU call support!
#include "headers/mmu/mmuhandler.h" //MMU handling support!
#include "headers/cpu/easyregs.h" //Easy register support!
#include "headers/hardware/pci.h" //Bus termination supoort!
#include "headers/hardware/pic.h" //APIC support!
#include "headers/hardware/vga/svga/tseng.h" //Tseng support for termination of transfers!

//Define the below to throw faults on instructions causing an invalid jump somewhere!
//#define FAULT_INVALID_JUMPS

//16-bits compatibility for reading parameters!
#define LE_16BITS(x) SDL_SwapLE16(x)
//32-bits compatibility for reading parameters!
#define LE_32BITS(x) SDL_SwapLE32((LE_16BITS((x)&0xFFFF))|(uint_32)((LE_16BITS(((x)>>16)&0xFFFF))<<16))

//Types of request(low 4 bits)!
#define REQUEST_NONE 0

//Type
#define REQUEST_TYPEMASK 7
#define REQUEST_MMUREAD 1
#define REQUEST_MMUWRITE 2
#define REQUEST_IOREAD 3
#define REQUEST_IOWRITE 4

//Size to access
#define REQUEST_SIZEMASK 0x18
#define REQUEST_16BIT 0x08
#define REQUEST_32BIT 0x10

//Extra extension for 16/32-bit accesses(bitflag) to identify high value to be accessed!
#define REQUEST_SUBMASK 0x60
#define REQUEST_SUBSHIFT 5
#define REQUEST_SUB0 0x00
#define REQUEST_SUB1 0x20
#define REQUEST_SUB2 0x40
#define REQUEST_SUB3 0x60


//80X86 bus waitstate for XT!
#define CPU80X86_XTBUSWAITSTATE_DELAY 1

#define CPU286_WAITSTATE_DELAY 1
//BUS delay is supposed to be 4 waitstates?
#define CPU286_BUSWAITSTATE_DELAY 1

byte blockDMA; //Blocking DMA ?
byte BIU_buslocked = 0; //BUS locked?
byte BUSactive; //Are we allowed to control the BUS? 0=Inactive, 1=CPU, 2=DMA
BIU_type BIU[MAXCPUS]; //All possible BIUs!

extern byte PIQSizes[2][NUMCPUS]; //The PIQ buffer sizes!
extern byte BUSmasks[2][NUMCPUS]; //The bus masks, for applying 8/16/32-bit data buses to the memory accesses!
byte CPU_databussize = 0; //0=16/32-bit bus! 1=8-bit bus when possible (8088/80188) or 16-bit when possible(286+)!
byte CPU_databusmask = 0; //The mask from the BUSmasks lookup table!
Handler BIU_activeCycleHandler = NULL;
byte BIU_is_486 = 0;
byte BIU_numcyclesmask;

byte CompaqWrapping[0x1000]; //Compaq Wrapping precalcs!
extern byte is_Compaq; //Are we emulating a Compaq architecture?

void detectBIUactiveCycleHandler(); //For detecting the cycle handler to use for this CPU!

byte useIPSclock = 0; //Are we using the IPS clock instead of cycle accurate clock?
extern CPU_type CPU[MAXCPUS]; //The CPU!

void BIU_handleRequestsNOP(); //Prototype dummy handler!

Handler BIU_handleRequests = &BIU_handleRequestsNOP; //Handle all pending requests at once when to be processed!

void CPU_initBIU()
{
	word b;
	if (BIU[activeCPU].ready) //Are we ready?
	{
		CPU_doneBIU(); //Finish us first!
	}

	if (PIQSizes[CPU_databussize][EMULATED_CPU]) //Gotten any PIQ installed with the CPU?
	{
		BIU[activeCPU].PIQ = allocfifobuffer(PIQSizes[CPU_databussize][EMULATED_CPU], 0); //Our PIQ we use!
	}
	CPU_databusmask = BUSmasks[CPU_databussize][EMULATED_CPU]; //Our data bus mask we use for splitting memory chunks!
	BIU[activeCPU].requests = allocfifobuffer(24, 0); //Our request buffer to use(1 64-bit entry being 2 32-bit entries, for 2 64-bit entries(payload) and 1 32-bit expanded to 64-bit entry(the request identifier) for speed purposes)!
	BIU[activeCPU].responses = allocfifobuffer(sizeof(uint_32) << 1, 0); //Our response buffer to use(1 64-bit entry as 2 32-bit entries)!
	BIU_is_486 = (EMULATED_CPU >= CPU_80486); //486+ handling?
	detectBIUactiveCycleHandler(); //Detect the active cycle handler to use!
	BIU[activeCPU].ready = 1; //We're ready to be used!
	BIU[activeCPU].PIQ_checked = 0; //Reset to not checked!
	BIU[activeCPU].terminationpending = 0; //No termination pending!
	CPU_flushPIQ(-1); //Init us to start!
	BIU_numcyclesmask = (1 | ((((EMULATED_CPU > CPU_NECV30) & 1) ^ 1) << 1)); //1(80286+) or 3(80(1)86)!
	if (is_Compaq) //Compaq wrapping instead?
	{
		CompaqWrapping[0] = CompaqWrapping[1] = 0; //Wrap only for the 1-2MB on Compaq!
		for (b = 2; b < NUMITEMS(CompaqWrapping); ++b)
		{
			CompaqWrapping[b] = 1; //Don't wrap A20 for all other lines when A20 is disabled!
		}
	}
	else
	{
		memset(&CompaqWrapping, 0, sizeof(CompaqWrapping)); //Wrapping applied always!
	}
	BIU[activeCPU].handlerequestPending = &BIU_handleRequestsNOP; //Nothing is actively being handled!
	BIU[activeCPU].newrequest = 0; //Not a new request loaded!
}

void CPU_doneBIU()
{
	free_fifobuffer(&BIU[activeCPU].PIQ); //Release our PIQ!
	free_fifobuffer(&BIU[activeCPU].requests); //Our request buffer to use(1 64-bit entry as 2 32-bit entries)!
	free_fifobuffer(&BIU[activeCPU].responses); //Our response buffer to use(1 64-bit entry as 2 32-bit entries)!
	BIU[activeCPU].ready = 0; //We're not ready anymore!
	memset(&BIU[activeCPU],0,sizeof(BIU)); //Full init!
}

void checkBIUBUSrelease()
{
	byte whichCPU;
	if (unlikely(BUSactive==1)) //Needs release?
	{
		whichCPU = 0;
		do
		{
			if (BIU[whichCPU].BUSactive) return; //Don't release when any is still active!
		} while(++whichCPU<MAXCPUS); //Check all!
		BUSactive = 0; //Fully release the bus! 
	}
}

void BIU_recheckmemory() //Recheck any memory that's preloaded and/or validated for the BIU!
{
	BIU[activeCPU].PIQ_checked = 0; //Recheck anything that's fetching from now on!
}

byte condflushtriggered = 0;

byte CPU_condflushPIQ(int_64 destaddr)
{
	if (BIU[activeCPU].PIQ) fifobuffer_clear(BIU[activeCPU].PIQ); //Clear the Prefetch Input Queue!
	REG_EIP &= CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS].PRECALCS.roof; //Wrap EIP as needed!
	BIU[activeCPU].PIQ_Address = (destaddr!=-1)?(uint_32)destaddr:REG_EIP; //Use actual IP!
	CPU[activeCPU].repeating = 0; //We're not repeating anymore!
	BIU_recheckmemory(); //Recheck anything that's fetching from now on!
	BIU_instructionStart(); //Prepare for a new instruction!

	//Check for any instruction faults that's pending for the next to be executed instruction!
#ifdef FAULT_INVALID_JUMPS
	condflushtriggered = 0;
	if (unlikely(checkMMUaccess(CPU_SEGMENT_CS, REG_CS, REG_EIP, 3, getCPL(), !CODE_SEGMENT_DESCRIPTOR_D_BIT(), 0))) //Error accessing memory?
	{
		condflushtriggered = 1;
	}
	if (unlikely(condflushtriggered)) return 1;
#endif
	return 0; //No error!
}

byte dummyresult=0;
void CPU_flushPIQ(int_64 destaddr) //Flush the PIQ! Returns 0 without abort, 1 with abort!
{
	dummyresult = CPU_condflushPIQ(destaddr); //Conditional one, but ignore the result!
}


//Internal helper functions for requests and responses!
OPTINLINE byte BIU_haveRequest() //BIU: Does the BIU have a request?
{
	return ((fifobuffer_freesize(BIU[activeCPU].requests)==0) && (fifobuffer_freesize(BIU[activeCPU].responses)==fifobuffer_size(BIU[activeCPU].responses))); //Do we have a request and enough size for a response?
}

OPTINLINE byte BIU_readRequest(uint_32 *requesttype, uint_64 *payload1, uint_64 *payload2) //BIU: Read a request to process!
{
	uint_32 temppayload1, temppayload2, dummypayload;
	if (BIU[activeCPU].requestready==0) return 0; //Not ready!
	if (readfifobuffer32_2u(BIU[activeCPU].requests,requesttype,&dummypayload)==0) //Type?
	{
		return 0; //No request!
	}
	if (readfifobuffer32_2u(BIU[activeCPU].requests,&temppayload1,&temppayload2)) //Read the payload?
	{
		*payload1 = (((uint_64)temppayload2<<32)|(uint_64)temppayload1); //Give the request!
		if (readfifobuffer32_2u(BIU[activeCPU].requests,&temppayload1,&temppayload2)) //Read the payload?
		{
			*payload2 = (((uint_64)temppayload2<<32)|(uint_64)temppayload1); //Give the request!
			return 1; //OK! We're having the request!
		}
	}
	return 0; //Invalid request!
}

OPTINLINE byte BIU_request(uint_32 requesttype, uint_64 payload1, uint_64 payload2) //CPU: Request something from the BIU by the CPU!
{
	byte result;
	uint_32 request1, request2;
	if ((BIU[activeCPU].requestready==0) || (fifobuffer_freesize(BIU[activeCPU].responses)==0)) return 0; //Not ready! Don't allow requests while responses are waiting to be handled!
	request1 = (payload1&0xFFFFFFFF); //Low!
	request2 = (payload1>>32); //High!
	if (fifobuffer_freesize(BIU[activeCPU].requests)>=24) //Enough to accept?
	{
		result = writefifobuffer32_2u(BIU[activeCPU].requests,requesttype,0); //Request type!
		result &= writefifobuffer32_2u(BIU[activeCPU].requests,request1,request2); //Payload!
		request1 = (payload2&0xFFFFFFFF); //Low!
		request2 = (payload2>>32); //High!
		result &= writefifobuffer32_2u(BIU[activeCPU].requests,request1,request2); //Payload!
		return result; //Are we requested?
	}
	return 0; //Not available!
}

OPTINLINE byte BIU_response(uint_64 response) //BIU: Response given from the BIU!
{
	uint_32 response1, response2;
	response1 = (response&0xFFFFFFFF); //Low!
	response2 = (response>>32); //High!
	return (writefifobuffer32_2u(BIU[activeCPU].responses,response1,response2)); //Response!
}

OPTINLINE byte BIU_readResponse(uint_64 *response) //CPU: Read a response from the BIU!
{
	uint_32 response1, response2;
	if (BIU[activeCPU].requestready==0) return 0; //Not ready!
	if (readfifobuffer32_2u(BIU[activeCPU].responses,&response1,&response2)) //Do we have a request and enough size for a response?
	{
		*response = (((uint_64)response2<<32)|(uint_64)response1); //Give the request!
		return 1; //OK!
	}
	return 0; //No request!
}

//Actual requesting something from the BIU, for the CPU module to call!
//MMU accesses
byte BIU_request_Memoryrb(uint_32 address, byte useTLB)
{
	return BIU_request(REQUEST_MMUREAD,address,useTLB); //Request a read!
}

byte BIU_request_Memoryrw(uint_32 address, byte useTLB)
{
	return BIU_request(REQUEST_MMUREAD|REQUEST_16BIT,address,useTLB); //Request a read!
}

byte BIU_request_Memoryrdw(uint_32 address, byte useTLB)
{
	return BIU_request(REQUEST_MMUREAD|REQUEST_32BIT,address,useTLB); //Request a read!
}

byte BIU_request_Memorywb(uint_32 address, byte val, byte useTLB)
{
	return BIU_request(REQUEST_MMUWRITE,((uint_64)address|((uint_64)val<<32)),useTLB); //Request a write!
}

byte BIU_request_Memoryww(uint_32 address, word val, byte useTLB)
{
	return BIU_request(REQUEST_MMUWRITE|REQUEST_16BIT,((uint_64)address|((uint_64)val<<32)),useTLB); //Request a write!
}

byte BIU_request_Memorywdw(uint_32 address, uint_32 val, byte useTLB)
{
	return BIU_request(REQUEST_MMUWRITE|REQUEST_32BIT,((uint_64)address|((uint_64)val<<32)),useTLB); //Request a write!
}

//BUS(I/O address space) accesses for the Execution Unit to make, and their results!
byte BIU_request_BUSrb(uint_32 addr)
{
	return BIU_request(REQUEST_IOREAD,addr,0); //Request a read!
}

byte BIU_request_BUSrw(uint_32 addr)
{
	return BIU_request(REQUEST_IOREAD|REQUEST_16BIT,addr,0); //Request a read!
}

byte BIU_request_BUSrdw(uint_32 addr)
{
	return BIU_request(REQUEST_IOREAD|REQUEST_32BIT,addr,0); //Request a read!
}

byte BIU_request_BUSwb(uint_32 addr, byte value)
{
	return BIU_request(REQUEST_IOWRITE,(uint_64)addr|((uint_64)value<<32),0); //Request a read!
}

byte BIU_request_BUSww(uint_32 addr, word value)
{
	return BIU_request(REQUEST_IOWRITE|REQUEST_16BIT,((uint_64)addr|((uint_64)value<<32)),0); //Request a write!
}

byte BIU_request_BUSwdw(uint_32 addr, uint_32 value)
{
	return BIU_request(REQUEST_IOWRITE|REQUEST_32BIT,((uint_64)addr|((uint_64)value<<32)),0); //Request a write!
}

byte BIU_getcycle()
{
	return (BIU[activeCPU].prefetchclock & BIU_numcyclesmask); //What cycle are we at?
}

byte BIU_readResultb(byte *result) //Read the result data of a BUS request!
{
	byte status;
	uint_64 response;
	status = BIU_readResponse(&response); //Read the response for the user!
	if (status) //Read?
	{
		*result = (byte)response; //Give the response!
		return 1; //Read!
	}
	return 0; //Not read!
}

byte BIU_readResultw(word *result) //Read the result data of a BUS request!
{
	byte status;
	uint_64 response;
	status = BIU_readResponse(&response); //Read the response for the user!
	if (status) //Read?
	{
		*result = (word)response; //Give the response!
		return 1; //Read!
	}
	return 0; //Not read!
}

byte BIU_readResultdw(uint_32 *result) //Read the result data of a BUS request!
{
	byte status;
	uint_64 response;
	status = BIU_readResponse(&response); //Read the response for the user!
	if (status) //Read?
	{
		*result = (uint_32)response; //Give the response!
		return 1; //Read!
	}
	return 0; //Not read!
}

byte BIU_access_writeshift[4] = {32,40,48,56}; //Shift to get the result byte to write to memory!
byte BIU_access_readshift[4] = {0,8,16,24}; //Shift to put the result byte in the result!

//Linear memory access for the CPU through the Memory Unit!
extern byte MMU_logging; //Are we logging?
extern MMU_type MMU; //MMU support!
extern uint_64 effectivecpuaddresspins; //What address pins are supported?

//Some cached memory line!
uint_64 BIU_cachedmemoryaddr[MAXCPUS][2] = { {0,0},{0,0} };
uint_64 BIU_cachedmemoryread[MAXCPUS][2][2] = { {{0,0},{0,0}}, {{0,0},{0,0}} };
byte BIU_cachedmemorysize[MAXCPUS][2] = { {0,0},{0,0} };

extern uint_64 memory_dataaddr[2]; //The data address that's cached!
extern uint_64 memory_dataread[2];
extern byte memory_datasize[2]; //The size of the data that has been read!

void BIU_terminatemem()
{
	//Terminated a memory access!
	if (BIU[activeCPU].terminationpending) //Termination is pending?
	{
		BIU[activeCPU].terminationpending = 0; //Not pending anymore!
		//Handle any events requiring termination!
		APIC_handletermination(); //Handle termination of the APIC writes!
		Tseng4k_handleTermination(); //Terminate a memory cycle!
	}
}

extern byte MMU_waitstateactive; //Waitstate active?

OPTINLINE byte BIU_directrb(uint_64 realaddress, word index)
{
	INLINEREGISTER uint_64 cachedmemorybyte;
	uint_64 originaladdr;
	byte result;
	INLINEREGISTER byte isprefetch;
	isprefetch = ((index & 0x20) >> 5); //Prefetvh?
	//Apply A20!
	realaddress &= effectivecpuaddresspins; //Only 20-bits address is available on a XT without newer CPU! Only 24-bits is available on a AT!
	originaladdr = realaddress; //Save the address before the A20 is modified!
	realaddress &= (MMU.wraparround | (CompaqWrapping[(realaddress >> 20)] << 20)); //Apply A20, when to be applied, including Compaq-style wrapping!

	if (likely(BIU_cachedmemorysize[activeCPU][isprefetch])) //Anything left cached?
	{
		//First, validate the cache itself!
		if (unlikely((BIU_cachedmemorysize[activeCPU][isprefetch] != memory_datasize[isprefetch]) || (BIU_cachedmemoryaddr[activeCPU][isprefetch] != memory_dataaddr[isprefetch]))) //Not cached properly or different address in the memory cache?
		{
			goto uncachedread; //Uncached read!
		}
		//Now, validate the active address!
		cachedmemorybyte = (realaddress - BIU_cachedmemoryaddr[activeCPU][isprefetch]); //What byte in the cache are we?
		if (unlikely((cachedmemorybyte >= BIU_cachedmemorysize[activeCPU][isprefetch]) || (realaddress < BIU_cachedmemoryaddr[activeCPU][isprefetch]))) //Past or before what's cached?
		{
			goto uncachedread; //Uncached read!
		}
		//We're the same address block that's already loaded!
		cachedmemorybyte <<= 3; //Make it a multiple of 8 bits!
		result = BIU_cachedmemoryread[activeCPU][cachedmemorybyte>>6][isprefetch] >> (cachedmemorybyte&0x3F); //Read the data from the local cache!
	}
	else //Start uncached read!
	{
		uncachedread: //Perform an uncached read!
		//Normal memory access!
		result = MMU_INTERNAL_directrb_realaddr(realaddress, (index & 0xFF)); //Read from MMU/hardware!

		BIU_cachedmemoryaddr[activeCPU][isprefetch] = memory_dataaddr[isprefetch]; //The address that's cached now!
		BIU_cachedmemoryread[activeCPU][0][isprefetch] = memory_dataread[0]; //What has been read!
		BIU_cachedmemoryread[activeCPU][1][isprefetch] = memory_dataread[1]; //What has been read!
		if (unlikely((memory_datasize[isprefetch] > 1) && (MMU_waitstateactive == 0))) //Valid to cache? Not waiting for a result?
		{
			BIU_cachedmemorysize[activeCPU][isprefetch] = memory_datasize[isprefetch]; //How much has been read!
		}
		else
		{
			BIU_cachedmemorysize[activeCPU][isprefetch] = 0; //Invalidate the local cache!
		}

		if (unlikely(MMU_logging == 1) && ((index & 0x100) == 0)) //To log?
		{
			debugger_logmemoryaccess(0, originaladdr, result, LOGMEMORYACCESS_PAGED | (((index & 0x20) >> 5) << LOGMEMORYACCESS_PREFETCHBITSHIFT)); //Log it!
		}
	}

	return result; //Give the result!
}

byte BIU_directrb_external(uint_64 realaddress, word index)
{
	return BIU_directrb(realaddress, index); //External!
}

extern uint_32 memory_datawrite; //Data to be written!
extern byte memory_datawritesize; //How much bytes are requested to be written?
extern byte memory_datawrittensize; //How many bytes have been written to memory during a write!

OPTINLINE void BIU_directwb(uint_64 realaddress, byte val, word index) //Access physical memory dir
{
	//Apply A20!
	realaddress &= effectivecpuaddresspins; //Only 20-bits address is available on a XT without newer CPU! Only 24-bits is available on a AT!

	if (unlikely(MMU_logging==1) && ((index&0x100)==0)) //To log?
	{
		debugger_logmemoryaccess(1,realaddress,val,LOGMEMORYACCESS_PAGED); //Log it!
	}

	realaddress &= (MMU.wraparround | (CompaqWrapping[(realaddress >> 20)] << 20)); //Apply A20, when to be applied, including Compaq-style wrapping!

	//Normal memory access!
	MMU_INTERNAL_directwb_realaddr(realaddress,val,(byte)(index&0xFF)); //Set data!
	BIU[activeCPU].terminationpending = 1; //Termination for this write is now pending!
}

void BIU_directwb_external(uint_64 realaddress, byte val, word index) //Access physical memory dir
{
	memory_datawritesize = 1; //Work in byte chunks always for now!
	BIU_directwb(realaddress, val, index); //External!
}

word BIU_directrw(uint_64 realaddress, word index) //Direct read from real memory (with real data direct)!
{
	return BIU_directrb(realaddress, index) | (BIU_directrb(realaddress + 1, index | 1) << 8); //Get data, wrap arround!
}

void BIU_directww(uint_64 realaddress, word value, word index) //Direct write to real memory (with real data direct)!
{
	if ((index & 3) == 0) //First byte?
	{
		memory_datawritesize = 2; //Work in byte chunks always for now!
		memory_datawrite = value; //What to write!
	}
	BIU_directwb(realaddress, value & 0xFF, index); //Low!
	if (unlikely(memory_datawrittensize != 2)) //Word not written?
	{
		memory_datawritesize = 1; //1 byte only!
		BIU_directwb(realaddress + 1, (value >> 8) & 0xFF, index | 1); //High!
	}
}

//Used by paging only!
uint_32 BIU_directrdw(uint_64 realaddress, word index)
{
	return BIU_directrw(realaddress, index) | (BIU_directrw(realaddress + 2, index | 2) << 16); //Get data, wrap arround!	
}
void BIU_directwdw(uint_64 realaddress, uint_32 value, word index)
{
	memory_datawritesize = 4; //DWord!
	memory_datawrite = value; //What to write!
	BIU_directwb(realaddress, value & 0xFF, index); //Low!
	if (unlikely(memory_datawrittensize != 4)) //Not fully written? Somehow this doesn't work correctly yet with 32-bit writes?
	{
		memory_datawritesize = 1; //1 byte only!
		BIU_directwb(realaddress + 1, (value >> 8) & 0xFF, index | 1); //High!
		BIU_directww(realaddress + 2, (value >> 16) & 0xFFFF, index | 2); //High!
	}
}

extern MMU_realaddrHandler realaddrHandlerCS; //CS real addr handler!

extern uint_32 checkMMUaccess_linearaddr; //Saved linear address for the BIU to use!
byte PIQ_block[MAXCPUS] = { 0,0 }; //Blocking any PIQ access now?
#ifdef IS_WINDOWS
void CPU_fillPIQ() //Fill the PIQ until it's full!
#else
//Non-Windows doesn't have the overhead or profiling requirement of this function!
OPTINLINE void CPU_fillPIQ() //Fill the PIQ until it's full!
#endif
{
	uint_32 realaddress, linearaddress;
	INLINEREGISTER uint_64 physaddr;
	byte value;
	if (unlikely(((PIQ_block[activeCPU]==1) || (PIQ_block[activeCPU]==9)) && (useIPSclock==0))) { PIQ_block[activeCPU] = 0; return; /* Blocked access: only fetch one byte/word instead of a full word/dword! */ }
	if (unlikely(BIU[activeCPU].PIQ==0)) return; //Not gotten a PIQ? Abort!
	realaddress = BIU[activeCPU].PIQ_Address; //Next address to fetch(Logical address)!
	physaddr = checkMMUaccess_linearaddr = realaddrHandlerCS(CPU_SEGMENT_CS, REG_CS, realaddress, 0,0); //Linear adress!
	if (likely(BIU[activeCPU].PIQ_checked)) //Checked left not performing any memory checks?
	{
		--BIU[activeCPU].PIQ_checked; //Tick checked data to not check!
		linearaddress = checkMMUaccess_linearaddr; //Linear address isn't retranslated!
	}
	else //Full check and translation to a linear address?
	{
		if (unlikely(checkMMUaccess(CPU_SEGMENT_CS, REG_CS, realaddress, 0x10 | 3, getCPL(), 0, 0))) return; //Abort on fault!
		physaddr = linearaddress = checkMMUaccess_linearaddr; //Linear address!
	}
	if (unlikely(checkMMUaccess_linearaddr & 1)) //Read an odd address?
	{
		PIQ_block[activeCPU] &= 5; //Start blocking when it's 3(byte fetch instead of word fetch), also include dword odd addresses. Otherwise, continue as normally!		
	}
	if (is_paging()) //Are we paging?
	{
		physaddr = mappage((uint_32)physaddr,0,getCPL()); //Map it using the paging mechanism to a physical address!		
	}
	value = BIU_directrb(physaddr, 0 | 0x20 | 0x100); //Read the memory location!
	if (MMU_waitstateactive) //No result yet?
	{
		return; //Keep polling!
	}
	writefifobuffer(BIU[activeCPU].PIQ, value); //Add the next byte from memory into the buffer!

	//Next data! Take 4 cycles on 8088, 2 on 8086 when loading words/4 on 8086 when loading a single byte.
	BUSactive = BIU[activeCPU].BUSactive = 1; //Start memory cycles!
	
	if (unlikely(MMU_logging == 1)) //To log?
	{
		debugger_logmemoryaccess(0, linearaddress, value, LOGMEMORYACCESS_PAGED | ((((0 | 0x20 | 0x100) & 0x20) >> 5) << LOGMEMORYACCESS_PREFETCHBITSHIFT)); //Log it!
		debugger_logmemoryaccess(0, BIU[activeCPU].PIQ_Address, value, LOGMEMORYACCESS_NORMAL | ((((0 | 0x20 | 0x100) & 0x20) >> 5) << LOGMEMORYACCESS_PREFETCHBITSHIFT)); //Log it!
	}

	//Prepare the next address to be read(EIP of the BIU)!
	++realaddress; //Increase the address to the next location!
	realaddress &= CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS].PRECALCS.roof; //Wrap EIP as needed!
	BIU[activeCPU].PIQ_Address = realaddress; //Save the increased&wrapped EIP!
	BIU[activeCPU].requestready = 0; //We're starting a request!
}

byte BIU_DosboxTickPending[MAXCPUS] = { 0,0 }; //We're pending to reload the entire buffer with whatever's available?
byte instructionlimit[6] = {10,15,15,15,15,15}; //What is the maximum instruction length in bytes?
void BIU_dosboxTick()
{
	byte faultcode;
	uint_32 BIUsize, BIUsize2;
	uint_32 realaddress;
	uint_64 maxaddress, endpos;
	if (BIU[activeCPU].PIQ) //Prefetching?
	{
		recheckmemory: //Recheck the memory that we're fetching!
		//Precheck anything that can be checked!
		BIUsize = BIUsize2 = fifobuffer_freesize(BIU[activeCPU].PIQ); //How much might be filled?
		realaddress = BIU[activeCPU].PIQ_Address; //Where to start checking!
		endpos = (((uint_64)realaddress + (uint_64)BIUsize) - 1ULL); //Our last byte fetched!
		maxaddress = 0xFFFFFFFF; //Default to a top-down segment's maximum size being the limit!
		if (likely(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS].PRECALCS.topdown == 0)) //Not a top-down segment?
		{
			maxaddress = CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS].PRECALCS.limit; //The limit of the CS segment is the limit instead!
			if (unlikely(realaddress > maxaddress)) //Limit broken?
			{
				BIU_DosboxTickPending[activeCPU] = 0; //Not pending anymore!
				return; //Abort on fault! 
			}
		}
		else if (unlikely(((uint_64)realaddress) <= CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS].PRECALCS.limit)) //Limit broken?
		{
			return; //Abort on fault! 
		}
		maxaddress = MIN((uint_64)((realaddress + (uint_64)BIUsize) - 1ULL), maxaddress); //Prevent 32-bit overflow and segmentation limit from occurring!
		if (unlikely(endpos > maxaddress)) //More left than we can handle(never less than 1 past us)?
		{
			BIUsize -= (uint_32)(endpos - maxaddress); //Only check until the maximum address!
		}

		BIUsize = MAX(BIUsize, 1); //Must be at least 1, just for safety!

		//Perform the little remainder of the segment limit check here instead of during the checkMMUaccess check!
		if (likely(GENERALSEGMENT_S(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS]))) //System segment? Check for additional type information!
		{
			if (unlikely(CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS].PRECALCS.rwe_errorout[3])) //Are we to error out on this read/write/execute operation?
			{
				return; //Abort on fault! 
			}
		}

		//Now, check the paging half of protection checks!
		//First, check the lower bound! If this fails, we can't continue(we're immediately failing)!
		MMU_resetaddr(); //Reset the address error line for trying some I/O!
		if (unlikely(faultcode = checkMMUaccess(CPU_SEGMENT_CS, REG_CS, realaddress, 0xA0 | 0x10 | 3, getCPL(), 0, 0)))
		{
			return; //Abort on fault! 
		}

		//Next, check the higher bound! While it fails, decrease until we don't anymore!
		if (likely(BIUsize > 1)) //Different ending address?
		{
			realaddress += (BIUsize - 1); //Take the last byte we might be fetching!
			for (;;) //When the below check fails, try for the next address!
			{
				if (unlikely((faultcode = checkMMUaccess(CPU_SEGMENT_CS, REG_CS, realaddress, 0xA0 | 0x10 | 3, getCPL(), 0, 0)) && BIUsize)) //Couldn't fetch?
				{
					if (faultcode == 2) //Pending?
					{
						return; //Abort pending!
					}
					//The only thing stopping us here is the page boundary, so round down to a lower one, if possible!
					endpos = realaddrHandlerCS(CPU_SEGMENT_CS, REG_CS, realaddress, 0, 0); //Linear address of the failing byte!
					maxaddress = 0; //Our flag for determining if we can just take the previous page by calculating it normally!
					endpos -= (((endpos & 0xFFFFF000ULL) - 1) & 0xFFFFFFFFULL); //How much to substract for getting the valid previous page!
					endpos &= 0xFFFFFFFFULL; //Make sure we're proper 32-bit!
					maxaddress = (endpos <= BIUsize); //Valid to use(and not underflowing the remainder we're able to fetch)?
					if (maxaddress) //Can we just take the previous page?
					{
						realaddress -= (uint_32)endpos; //Round down to the previous page!
						BIUsize -= (uint_32)endpos; //Some bytes are not available to fetch!
					}
					else //Rounding down to the previous page not possible? Just step back!
					{
						--realaddress; //Go back one byte!
						--BIUsize; //One less byte is available to fetch!
					}
					MMU_resetaddr(); //Reset the address error line for trying some I/O!
				}
				else break; //Finished!
			}
		}

		BIU[activeCPU].PIQ_checked = BIUsize; //Check off any that we have verified!

		MMU_resetaddr(); //Reset the address error line for trying some I/O!
		if ((EMULATED_CPU>=CPU_80286) && BIUsize2) //Can we limit what we fetch, instead of the entire prefetch buffer?
		{
			if (unlikely((fifobuffer_size(BIU[activeCPU].PIQ)-BIUsize2)>=instructionlimit[EMULATED_CPU - CPU_80286])) //Already buffered enough?
			{
				BIUsize2 = 0; //Don't buffer more, enough is buffered!
			}
			else //Not buffered enough to the limit yet?
			{
				BIUsize2 = MIN(instructionlimit[EMULATED_CPU - CPU_80286]-(fifobuffer_size(BIU[activeCPU].PIQ)-BIUsize2),BIUsize2); //Limit by what we can use for an instruction!
			}
		}
		for (;BIUsize2 && (MMU_invaddr()==0);)
		{
			if (likely(((BIU[activeCPU].PIQ_checked == 0) && BIUsize)==0)) //Not rechecking yet(probably not)?
			{
				PIQ_block[activeCPU] = 0; //We're never blocking(only 1 access)!
				CPU_fillPIQ(); //Keep the FIFO fully filled!
				BIU[activeCPU].BUSactive = 0; //Inactive BUS!
				checkBIUBUSrelease(); //Check for release!
				BIU[activeCPU].requestready = 1; //The request is ready to be served!
				--BIUsize2; //One item has been processed!
			}
			else goto recheckmemory; //Recheck anything that's needed, only when not starting off as zeroed!
		}
		BIU[activeCPU].BUSactive = 0; //Inactive BUS!
		checkBIUBUSrelease(); //Check for release!
		BIU[activeCPU].requestready = 1; //The request is ready to be served!
	}
	BIU_DosboxTickPending[activeCPU] = 0; //Not pending anymore!
}

void BIU_instructionStart() //Handle all when instructions are starting!
{
	if (unlikely(useIPSclock)) //Using IPS clock?
	{
		BIU_DosboxTickPending[activeCPU] = 1; //We're pending to reload!
	}
}

byte CPU_readOP(byte *result, byte singlefetch) //Reads the operation (byte) at CS:EIP
{
	uint_32 instructionEIP = (REG_EIP&CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS].PRECALCS.roof); //Our current instruction position is increased always!
	if (unlikely(CPU[activeCPU].resetPending)) return 1; //Disable all instruction fetching when we're resetting!
	if (likely(BIU[activeCPU].PIQ)) //PIQ present?
	{
		if (unlikely(BIU_DosboxTickPending[activeCPU])) //Tick is pending? Handle any that needs ticking when fetching!
		{
			BIU_dosboxTick(); //Tick like DOSBox does(fill the PIQ up as much as possible without cycle timing)!
		}
		//PIQ_retry: //Retry after refilling PIQ!
		//if ((CPU[activeCPU].prefetchclock&(((EMULATED_CPU<=CPU_NECV30)<<1)|1))!=((EMULATED_CPU<=CPU_NECV30)<<1)) return 1; //Stall when not T3(80(1)8X) or T0(286+).
		//Execution can start on any cycle!
		//Protection checks have priority over reading the PIQ! The prefetching stops when errors occur when prefetching, we handle the prefetch error when reading the opcode from the BIU, which has to happen before the BIU is retrieved!
		uint_32 instructionEIP = (REG_EIP&CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS].PRECALCS.roof); //Our current instruction position is increased always!
		if (unlikely(checkMMUaccess(CPU_SEGMENT_CS, REG_CS, instructionEIP,3,getCPL(),!CODE_SEGMENT_DESCRIPTOR_D_BIT(),0))) //Error accessing memory?
		{
			return 1; //Abort on fault!
		}
		if (unlikely(MMU.invaddr)) //Was an invalid address signaled? We might have to update the prefetch unit to prefetch all that's needed, since it's validly mapped now!
		{
			BIU_instructionStart();
		}
		if (unlikely(BIU_DosboxTickPending[activeCPU])) //Tick is pending? Handle any that needs ticking when fetching!
		{
			BIU_dosboxTick(); //Tick like DOSBox does(fill the PIQ up as much as possible without cycle timing)!
		}
		if (EMULATED_CPU >= CPU_80286)
		{
			if (unlikely((CPU[activeCPU].OPlength + 1)>instructionlimit[EMULATED_CPU - CPU_80286])) //Instruction limit broken this fetch?
			{
				THROWDESCGP(0, 0, 0); //#GP(0)
				return 1; //Abort on fault!
			}
		}
		if (readfifobuffer(BIU[activeCPU].PIQ,result)) //Read from PIQ?
		{
			MMU_addOP(*result); //Add to the opcode cache!
			++REG_EIP; //Increase EIP to give the correct point to use!
			REG_EIP &= CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS].PRECALCS.roof; //Wrap EIP as is required!
			if (likely(singlefetch)) ++CPU[activeCPU].cycles_Prefetch; //Fetching from prefetch takes 1 cycle!
			return 0; //Give the prefetched data!
		}
		else if (unlikely(useIPSclock)) //Using the IPS clocking mode? Since we're short on buffer, reload more into the buffer!
		{
			BIU_DosboxTickPending[activeCPU] = 1; //Make sure we fill more buffer for this instruction, as not enough can be buffered!
		}
		//Not enough data in the PIQ? Refill for the next data!
		return 1; //Wait for the PIQ to have new data! Don't change EIP(this is still the same)!
	}
	if (checkMMUaccess(CPU_SEGMENT_CS, REG_CS, instructionEIP,3,getCPL(),!CODE_SEGMENT_DESCRIPTOR_D_BIT(),0)) //Error accessing memory?
	{
		return 1; //Abort on fault!
	}
	if (EMULATED_CPU >= CPU_80286)
	{
		if (unlikely((CPU[activeCPU].OPlength + 1)>instructionlimit[EMULATED_CPU - CPU_80286])) //Instruction limit broken this fetch?
		{
			THROWDESCGP(0, 0, 0); //#GP(0)
			return 1; //Abort on fault!
		}
	}
	*result = MMU_rb(CPU_SEGMENT_CS, REG_CS, instructionEIP, 3,!CODE_SEGMENT_DESCRIPTOR_D_BIT()); //Read OPcode directly from memory!
	MMU_addOP(*result); //Add to the opcode cache!
	++REG_EIP; //Increase EIP, since we don't have to worrt about the prefetch!
	REG_EIP &= CPU[activeCPU].SEG_DESCRIPTOR[CPU_SEGMENT_CS].PRECALCS.roof; //Wrap EIP as is required!
	if (likely(singlefetch)) ++CPU[activeCPU].cycles_Prefetch; //Fetching from prefetch takes 1 cycle!
	return 0; //Give the result!
}

byte CPU_readOPw(word *result, byte singlefetch) //Reads the operation (word) at CS:EIP
{
	if (EMULATED_CPU>=CPU_80286) //80286+ reads it in one go(one single cycle)?
	{
		if (likely(BIU[activeCPU].PIQ)) //PIQ installed?
		{
			if (checkMMUaccess16(CPU_SEGMENT_CS, REG_CS, REG_EIP,3,getCPL(),!CODE_SEGMENT_DESCRIPTOR_D_BIT(),0|0x8)) //Error accessing memory?
			{
				return 1; //Abort on fault!
			}
			if (unlikely(MMU.invaddr)) //Was an invalid address signaled? We might have to update the prefetch unit to prefetch all that's needed, since it's validly mapped now!
			{
				BIU_instructionStart();
			}
			if (unlikely(BIU_DosboxTickPending[activeCPU])) //Tick is pending? Handle any that needs ticking when fetching!
			{
				BIU_dosboxTick(); //Tick like DOSBox does(fill the PIQ up as much as possible without cycle timing)!
			}
			if (fifobuffer_freesize(BIU[activeCPU].PIQ)<(fifobuffer_size(BIU[activeCPU].PIQ)-1)) //Enough free to read the entire part?
			{
				if (CPU_readOP(&BIU[activeCPU].temp,0)) return 1; //Read OPcode!
				if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
				++CPU[activeCPU].instructionfetch.CPU_fetchparameterPos; //Next position!
				goto fetchsecondhalfw; //Go fetch the second half
			}
			return 1; //Abort: not loaded in the PIQ yet!
		}
		//No PIQ installed? Use legacy method!
	}
	if (unlikely(BIU_DosboxTickPending[activeCPU])) //Tick is pending? Handle any that needs ticking when fetching!
	{
		BIU_dosboxTick(); //Tick like DOSBox does(fill the PIQ up as much as possible without cycle timing)!
	}
	if ((CPU[activeCPU].instructionfetch.CPU_fetchparameterPos&1)==0) //First opcode half?
	{
		if (CPU_readOP(&BIU[activeCPU].temp,1)) return 1; //Read OPcode!
		if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
		++CPU[activeCPU].instructionfetch.CPU_fetchparameterPos; //Next position!
	}
	if ((CPU[activeCPU].instructionfetch.CPU_fetchparameterPos&1)==1) //First second half?
	{
		fetchsecondhalfw: //Fetching the second half of the data?
		if (CPU_readOP(&BIU[activeCPU].temp2,singlefetch)) return 1; //Read OPcode!
		if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
		++CPU[activeCPU].instructionfetch.CPU_fetchparameterPos; //Next position!
		*result = LE_16BITS(BIU[activeCPU].temp|(BIU[activeCPU].temp2<<8)); //Give result!
	}
	return 0; //We're fetched!
}

byte CPU_readOPdw(uint_32 *result, byte singlefetch) //Reads the operation (32-bit unsigned integer) at CS:EIP
{
	if (likely(EMULATED_CPU>=CPU_80386)) //80386+ reads it in one go(one single cycle)?
	{
		if (likely(BIU[activeCPU].PIQ)) //PIQ installed?
		{
			if (checkMMUaccess32(CPU_SEGMENT_CS, REG_CS, REG_EIP,3,getCPL(),!CODE_SEGMENT_DESCRIPTOR_D_BIT(),0|0x10)) //Error accessing memory?
			{
				return 1; //Abort on fault!
			}
			if (unlikely(MMU.invaddr)) //Was an invalid address signaled? We might have to update the prefetch unit to prefetch all that's needed, since it's validly mapped now!
			{
				BIU_instructionStart();
			}
			if (unlikely(BIU_DosboxTickPending[activeCPU])) //Tick is pending? Handle any that needs ticking when fetching!
			{
				BIU_dosboxTick(); //Tick like DOSBox does(fill the PIQ up as much as possible without cycle timing)!
			}
			if (fifobuffer_freesize(BIU[activeCPU].PIQ)<(fifobuffer_size(BIU[activeCPU].PIQ)-3)) //Enough free to read the entire part?
			{
				if (CPU_readOPw(&BIU[activeCPU].resultw1,0)) return 1; //Read OPcode!
				if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
				++CPU[activeCPU].instructionfetch.CPU_fetchparameterPos; //Next position!
				goto fetchsecondhalfd; //Go fetch the second half
			}
			return 1; //Abort: not loaded in the PIQ yet!
		}
		//No PIQ installed? Use legacy method!
	}
	if (unlikely(BIU_DosboxTickPending[activeCPU])) //Tick is pending? Handle any that needs ticking when fetching!
	{
		BIU_dosboxTick(); //Tick like DOSBox does(fill the PIQ up as much as possible without cycle timing)!
	}
	if ((CPU[activeCPU].instructionfetch.CPU_fetchparameterPos&2)==0) //First opcode half?
	{
		if (CPU_readOPw(&BIU[activeCPU].resultw1,1)) return 1; //Read OPcode!
		if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
	}
	if ((CPU[activeCPU].instructionfetch.CPU_fetchparameterPos&2)==2) //Second opcode half?
	{
		fetchsecondhalfd: //Fetching the second half of the data?
		if (CPU_readOPw(&BIU[activeCPU].resultw2,singlefetch)) return 1; //Read OPcode!
		if (CPU[activeCPU].faultraised) return 1; //Abort on fault!
		*result = LE_32BITS((((uint_32)BIU[activeCPU].resultw2)<<16)|((uint_32)BIU[activeCPU].resultw1)); //Give result!
	}
	return 0; //We're fetched!
}

byte BIU_obtainbuslock()
{
	if (BIU_buslocked && (!BIU[activeCPU].BUSlockowned)) //Locked by another CPU?
	{
		BIU[activeCPU]._lock = 2; //Waiting for the lock to release!
		return 1; //Waiting for the lock to be obtained!
	}
	else
	{
		if (BIU[activeCPU].BUSlockrequested == 2) //Acnowledged?
		{
			BIU[activeCPU]._lock = 3; //Lock obtained!
			BIU_buslocked = 1; //A BIU has locked the bus!
			BIU[activeCPU].BUSlockowned = 1; //We own the lock!
		}
		else
		{
			BIU[activeCPU].BUSlockrequested = 1; //Request the lock from the bus!
			BIU[activeCPU]._lock = 2; //Waiting for the lock to release!
			return 1; //Waiting for the lock to be obtained!
		}
	}
	return 0; //Obtained the bus lock!
}

OPTINLINE byte BIU_processRequests(byte memory_waitstates, byte bus_waitstates)
{
	INLINEREGISTER uint_64 physicaladdress;
	INLINEREGISTER byte value;
	if (BIU[activeCPU].currentrequest) //Do we have a pending request we're handling? This is used for 16-bit and 32-bit requests!
	{
		if (BIU[activeCPU].newrequest) goto handleNewRequest; //A new request instead!
		BUSactive = BIU[activeCPU].BUSactive = 1; //Start memory or BUS cycles!
		switch (BIU[activeCPU].currentrequest&REQUEST_TYPEMASK) //What kind of request?
		{
			//Memory operations!
			case REQUEST_MMUREAD:
			fulltransferMMUread:
				physicaladdress = BIU[activeCPU].currentaddress;
				if (BIU[activeCPU].currentpayload[1] & 1) //Requires logical to physical address translation?
				{
					if (is_paging()) //Are we paging?
					{
						physicaladdress = mappage((uint_32)physicaladdress, 0, getCPL()); //Map it using the paging mechanism!
					}
				}

				BIU[activeCPU].currentresult |= ((value = BIU_directrb((physicaladdress),(((BIU[activeCPU].currentrequest&REQUEST_SUBMASK)>>REQUEST_SUBSHIFT)>>8)|0x100))<<(BIU_access_readshift[((BIU[activeCPU].currentrequest&REQUEST_SUBMASK)>>REQUEST_SUBSHIFT)])); //Read subsequent byte!
				if (MMU_waitstateactive) //No result yet?
				{
					return 1; //Keep polling!
				}
				if (unlikely((MMU_logging == 1) && (BIU[activeCPU].currentpayload[1] & 1))) //To log the paged layer?
				{
					debugger_logmemoryaccess(0, BIU[activeCPU].currentaddress, value, LOGMEMORYACCESS_PAGED | (((0 & 0x20) >> 5) << LOGMEMORYACCESS_PREFETCHBITSHIFT)); //Log it!
				}
				if ((BIU[activeCPU].currentrequest&REQUEST_SUBMASK)==((BIU[activeCPU].currentrequest&REQUEST_16BIT)?REQUEST_SUB1:REQUEST_SUB3)) //Finished the request?
				{
					if (BIU_response(BIU[activeCPU].currentresult)) //Result given?
					{
						BIU[activeCPU].waitstateRAMremaining += memory_waitstates; //Apply the waitstates for the fetch!
						BIU[activeCPU].currentrequest = REQUEST_NONE; //No request anymore! We're finished!
					}
				}
				else
				{
					BIU[activeCPU].currentrequest += REQUEST_SUB1; //Request next 8-bit half next(high byte)!
					++BIU[activeCPU].currentaddress; //Next address!
					if (unlikely((BIU[activeCPU].currentaddress&CPU_databusmask)==0))
					{
						BIU[activeCPU].waitstateRAMremaining += memory_waitstates; //Apply the waitstates for the fetch!
						return 1; //Handled, but broken up at this point due to the data bus not supporting transferring the rest of the word in one go!
					}
					goto fulltransferMMUread;
				}
				return 1; //Handled!
				break;
			case REQUEST_MMUWRITE:
			fulltransferMMUwrite:
				physicaladdress = BIU[activeCPU].currentaddress;
				if (BIU[activeCPU].currentpayload[1] & 1) //Requires logical to physical address translation?
				{
					if (is_paging()) //Are we paging?
					{
						physicaladdress = mappage((uint_32)physicaladdress, 1, getCPL()); //Map it using the paging mechanism!
					}
				}
				value = (BIU[activeCPU].currentpayload[0] >> (BIU_access_writeshift[((BIU[activeCPU].currentrequest&REQUEST_SUBMASK) >> REQUEST_SUBSHIFT)]) & 0xFF);
				if (unlikely((MMU_logging == 1) && (BIU[activeCPU].currentpayload[1] & 1))) //To log the paged layer?
				{
					debugger_logmemoryaccess(1, BIU[activeCPU].currentaddress, value, LOGMEMORYACCESS_PAGED | (((0 & 0x20) >> 5) << LOGMEMORYACCESS_PREFETCHBITSHIFT)); //Log it!
				}
				if (unlikely(BIU[activeCPU].datawritesizeexpected==1)) //Required to write manually?
				{
					memory_datawritesize = BIU[activeCPU].datawritesizeexpected = 1; //1 bvte only for now!
					BIU_directwb((physicaladdress), value, ((BIU[activeCPU].currentrequest & REQUEST_SUBMASK) >> REQUEST_SUBSHIFT) | 0x100); //Write directly to memory now!
					if (MMU_waitstateactive) //No result yet?
					{
						return 1; //Keep polling!
					}
				}
				if ((BIU[activeCPU].currentrequest&REQUEST_SUBMASK)==((BIU[activeCPU].currentrequest&REQUEST_16BIT)?REQUEST_SUB1:REQUEST_SUB3)) //Finished the request?
				{
					if (BIU_response(1)) //Result given? We're giving OK!
					{
						BIU_terminatemem(); //Terminate memory access!
						BIU[activeCPU].waitstateRAMremaining += memory_waitstates; //Apply the waitstates for the fetch!
						BIU[activeCPU].currentrequest = REQUEST_NONE; //No request anymore! We're finished!
					}
				}
				else
				{
					BIU[activeCPU].currentrequest += REQUEST_SUB1; //Request next 8-bit half next(high byte)!
					++BIU[activeCPU].currentaddress; //Next address!
					if (unlikely((BIU[activeCPU].currentaddress&CPU_databusmask)==0))
					{
						BIU[activeCPU].waitstateRAMremaining += memory_waitstates; //Apply the waitstates for the fetch!
						return 1; //Handled, but broken up at this point due to the data bus not supporting transferring the rest of the word in one go!
					}
					goto fulltransferMMUwrite;
				}
				return 1; //Handled!
				break;
			//I/O operations!
			case REQUEST_IOREAD:
				fulltransferIOread:
				if ((BIU[activeCPU].currentrequest&REQUEST_SUBMASK)==((BIU[activeCPU].currentrequest&REQUEST_16BIT)?REQUEST_SUB1:REQUEST_SUB3)) //Finished the request?
				{
					if (BIU_response(BIU[activeCPU].currentresult)) //Result given?
					{
						BIU[activeCPU].waitstateRAMremaining += bus_waitstates; //Apply the waitstates for the fetch!
						BIU[activeCPU].currentrequest = REQUEST_NONE; //No request anymore! We're finished!
					}
				}
				else
				{
					BIU[activeCPU].currentrequest += REQUEST_SUB1; //Request next 8-bit half next(high byte)!
					++BIU[activeCPU].currentaddress; //Next address!
					if (unlikely((BIU[activeCPU].currentaddress&CPU_databusmask)==0))
					{
						BIU[activeCPU].waitstateRAMremaining += bus_waitstates; //Apply the waitstates for the fetch!
						return 1; //Handled, but broken up at this point due to the data bus not supporting transferring the rest of the word in one go!
					}
					goto fulltransferIOread;
				}
				return 1; //Handled!
				break;
			case REQUEST_IOWRITE:
				fulltransferIOwrite:
				if ((BIU[activeCPU].currentrequest&REQUEST_SUBMASK)==((BIU[activeCPU].currentrequest&REQUEST_16BIT)?REQUEST_SUB1:REQUEST_SUB3)) //Finished the request?
				{
					if (BIU_response(1)) //Result given? We're giving OK!
					{
						BIU[activeCPU].waitstateRAMremaining += bus_waitstates; //Apply the waitstates for the fetch!
						BIU[activeCPU].currentrequest = REQUEST_NONE; //No request anymore! We're finished!
					}
				}
				else
				{
					BIU[activeCPU].currentrequest += REQUEST_SUB1; //Request next 8-bit half next(high byte)!
					++BIU[activeCPU].currentaddress; //Next address!
					if (unlikely((BIU[activeCPU].currentaddress&CPU_databusmask)==0))
					{
						BIU[activeCPU].waitstateRAMremaining += bus_waitstates; //Apply the waitstates for the fetch!
						return 1; //Handled, but broken up at this point due to the data bus not supporting transferring the rest of the word in one go!
					}
					goto fulltransferIOwrite;
				}
				return 1; //Handled!
				break;
			default:
			case REQUEST_NONE: //Unknown request?
				BIU[activeCPU].currentrequest = REQUEST_NONE; //No request anymore! We're finished!
				break; //Ignore the entire request!
		}
	}
	else
	{
		if (BIU_haveRequest()) //Do we have a request to handle first?
		{
			if (BIU_readRequest(&BIU[activeCPU].currentrequest, &BIU[activeCPU].currentpayload[0], &BIU[activeCPU].currentpayload[1])) //Read the request, if available!
			{
				BIU[activeCPU].newrequest = 1; //We're a new request!
			handleNewRequest:
				switch (BIU[activeCPU].currentrequest & REQUEST_TYPEMASK) //What kind of request?
				{
					//Memory operations!
				case REQUEST_MMUREAD:
					if (BUSactive == 2) return 1; //BUS taken?
					//Wait for other CPUs to release their lock on the bus if enabled?
					if (CPU_getprefix(0xF0)) //Locking requested?
					{
						if (BIU_obtainbuslock()) //Bus lock not obtained yet?
						{
							return 1; //Waiting for the lock to be obtained!
						}
					}
					BIU[activeCPU].newtransfer = 1; //We're a new transfer!
					BIU[activeCPU].newtransfer_size = 1; //We're a new transfer!
					BUSactive = BIU[activeCPU].BUSactive = 1; //Start memory or BUS cycles!
					if ((BIU[activeCPU].currentrequest & REQUEST_16BIT) || (BIU[activeCPU].currentrequest & REQUEST_32BIT)) //16/32-bit?
					{
						BIU[activeCPU].newtransfer_size = 2; //We're a new transfer!
						BIU[activeCPU].currentrequest |= REQUEST_SUB1; //Request 16-bit half next(high byte)!
						if (BIU[activeCPU].currentrequest & REQUEST_32BIT) //32-bit?
						{
							BIU[activeCPU].newtransfer_size = 4; //We're a new transfer!
						}
					}
					physicaladdress = BIU[activeCPU].currentaddress = (BIU[activeCPU].currentpayload[0] & 0xFFFFFFFF); //Address to use!
					if (BIU[activeCPU].currentpayload[1] & 1) //Requires logical to physical address translation?
					{
						if (is_paging()) //Are we paging?
						{
							physicaladdress = mappage((uint_32)physicaladdress, 0, getCPL()); //Map it using the paging mechanism!
						}
					}
					BIU[activeCPU].currentresult = ((value = BIU_directrb((physicaladdress), 0x100)) << BIU_access_readshift[0]); //Read first byte!
					if (MMU_waitstateactive) //No result yet?
					{
						BIU[activeCPU].currentrequest &= ~REQUEST_SUB1; //Request 8-bit half again(low byte)!
						BIU[activeCPU].newrequest = 1; //We're a new request!
						return 1; //Keep polling!
					}
					if (unlikely((MMU_logging == 1) && (BIU[activeCPU].currentpayload[1] & 1))) //To log the paged layer?
					{
						debugger_logmemoryaccess(0, BIU[activeCPU].currentaddress, value, LOGMEMORYACCESS_PAGED | (((0 & 0x20) >> 5) << LOGMEMORYACCESS_PREFETCHBITSHIFT)); //Log it!
					}
					if ((BIU[activeCPU].currentrequest & REQUEST_SUBMASK) == REQUEST_SUB0) //Finished the request?
					{
						if (BIU_response(BIU[activeCPU].currentresult)) //Result given?
						{
							BIU[activeCPU].newrequest = 0; //We're not a new request!
							BIU[activeCPU].waitstateRAMremaining += memory_waitstates; //Apply the waitstates for the fetch!
							BIU[activeCPU].currentrequest = REQUEST_NONE; //No request anymore! We're finished!
						}
						else //Response failed?
						{
							BIU[activeCPU].currentrequest &= ~REQUEST_SUB1; //Request low 8-bit half again(low byte)!
						}
					}
					else
					{
						if (useIPSclock && (BIU[activeCPU].newtransfer_size) && (BIU[activeCPU].newtransfer_size <= BIU_cachedmemorysize[activeCPU][0]) && (BIU_cachedmemorysize[activeCPU][0] > 1) && (BIU_cachedmemoryaddr[activeCPU][0] == physicaladdress)) //Data already fully read in IPS clocking mode?
						{
							BIU[activeCPU].currentresult |= ((value = BIU_directrb((physicaladdress+1), 0x100)) << BIU_access_readshift[1]); //Second byte!
							if (BIU[activeCPU].newtransfer_size==4) //Two more needed?
							{
								BIU[activeCPU].currentresult |= ((value = BIU_directrb((physicaladdress+2), 0x100)) << BIU_access_readshift[2]); //Third byte!
								BIU[activeCPU].currentresult |= ((value = BIU_directrb((physicaladdress+3), 0x100)) << BIU_access_readshift[3]); //Fourth byte!
							}
							if (BIU_response(BIU[activeCPU].currentresult)) //Result given? We're giving OK!
							{
								BIU_terminatemem(); //Terminate memory access!
								BIU[activeCPU].waitstateRAMremaining += memory_waitstates; //Apply the waitstates for the fetch!
								BIU[activeCPU].currentrequest = REQUEST_NONE; //No request anymore! We're finished!
								BIU[activeCPU].newrequest = 0; //We're not a new request!
								return 1; //Handled!
							}
						}
						++BIU[activeCPU].currentaddress; //Next address!
						if (unlikely((BIU[activeCPU].currentaddress & CPU_databusmask) == 0))
						{
							BIU[activeCPU].waitstateRAMremaining += memory_waitstates; //Apply the waitstates for the fetch!
							BIU[activeCPU].newrequest = 0; //We're not a new request!
							return 1; //Handled, but broken up at this point due to the data bus not supporting transferring the rest of the word in one go!
						}
						BIU[activeCPU].newrequest = 0; //We're not a new request!
						goto fulltransferMMUread; //Start Full transfer, when available?
					}
					return 1; //Handled!
					break;
				case REQUEST_MMUWRITE:
					if (BUSactive == 2) return 1; //BUS taken?
					//Wait for other CPUs to release their lock on the bus if enabled?
					if (CPU_getprefix(0xF0)) //Locking requested?
					{
						if (BIU_obtainbuslock()) //Bus lock not obtained yet?
						{
							return 1; //Waiting for the lock to be obtained!
						}
					}
					BIU[activeCPU].newtransfer = 1; //We're a new transfer!
					BIU[activeCPU].newtransfer_size = 1; //We're a new transfer!
					BUSactive = BIU[activeCPU].BUSactive = 1; //Start memory or BUS cycles!
					if ((BIU[activeCPU].currentrequest & REQUEST_16BIT) || (BIU[activeCPU].currentrequest & REQUEST_32BIT)) //16/32-bit?
					{
						BIU[activeCPU].newtransfer_size = 2; //We're a new transfer!
						BIU[activeCPU].currentrequest |= REQUEST_SUB1; //Request 16-bit half next(high byte)!
						if (BIU[activeCPU].currentrequest & REQUEST_32BIT) //32-bit?
						{
							BIU[activeCPU].newtransfer_size = 4; //We're a new transfer!
						}
					}
					physicaladdress = BIU[activeCPU].currentaddress = (BIU[activeCPU].currentpayload[0] & 0xFFFFFFFF); //Address to use!
					if (BIU[activeCPU].currentpayload[1] & 1) //Requires logical to physical address translation?
					{
						if (is_paging()) //Are we paging?
						{
							physicaladdress = mappage((uint_32)physicaladdress, 1, getCPL()); //Map it using the paging mechanism!
						}
					}
					if ((BIU[activeCPU].currentrequest & REQUEST_SUBMASK) == REQUEST_SUB0) //Finished the request?
					{
						if (BIU_response(1)) //Result given? We're giving OK!
						{
							value = ((BIU[activeCPU].currentpayload[0] >> BIU_access_writeshift[0]) & 0xFF); //What to write?
							if (unlikely((MMU_logging == 1) && (BIU[activeCPU].currentpayload[1] & 1))) //To log the paged layer?
							{
								debugger_logmemoryaccess(1, BIU[activeCPU].currentaddress, value, LOGMEMORYACCESS_PAGED | (((0 & 0x20) >> 5) << LOGMEMORYACCESS_PREFETCHBITSHIFT)); //Log it!
							}
							memory_datawritesize = 1; //1 byte only!
							BIU_directwb(physicaladdress, value, 0x100); //Write directly to memory now!
							if (MMU_waitstateactive) //No result yet?
							{
								uint_64 temp;
								temp = BIU_readResponse(&temp); //Discard the response!
								BIU[activeCPU].currentrequest &= ~REQUEST_SUB1; //Request 8-bit half again(low byte)!
								BIU[activeCPU].newrequest = 1; //We're a new request!
								return 1; //Keep polling!
							}
							BIU[activeCPU].waitstateRAMremaining += memory_waitstates; //Apply the waitstates for the fetch!
							BIU[activeCPU].currentrequest = REQUEST_NONE; //No request anymore! We're finished!
							BIU_terminatemem();
							BIU[activeCPU].newrequest = 0; //We're not a new request!
						}
						else //Response failed? Try again!
						{
							BIU[activeCPU].currentrequest &= ~REQUEST_SUB1; //Request 8-bit half again(low byte)!
							BIU[activeCPU].newrequest = 0; //We're not a new request!
						}
					}
					else //Busy request?
					{
						value = ((BIU[activeCPU].currentpayload[0] >> BIU_access_writeshift[0]) & 0xFF); //What to write?
						if (unlikely((MMU_logging == 1) && (BIU[activeCPU].currentpayload[1] & 1))) //To log the paged layer?
						{
							debugger_logmemoryaccess(1, BIU[activeCPU].currentaddress, value, LOGMEMORYACCESS_PAGED | (((0 & 0x20) >> 5) << LOGMEMORYACCESS_PREFETCHBITSHIFT)); //Log it!
						}
						if (BIU[activeCPU].currentrequest & REQUEST_32BIT) //32-bit request?
						{
							memory_datawritesize = 4; //4 bytes only!
							memory_datawrite = (BIU[activeCPU].currentpayload[0] >> 32); //What to write!
							BIU[activeCPU].datawritesizeexpected = 4; //We expect 4 to be set!
						}
						else //16-bit request?
						{
							memory_datawritesize = 2; //2 bytes only!
							memory_datawrite = ((BIU[activeCPU].currentpayload[0] >> 32) & 0xFFFF); //What to write!
							BIU[activeCPU].datawritesizeexpected = 2; //We expect 2 to be set!
						}
						if (unlikely((physicaladdress & 0xFFF) > (((physicaladdress + memory_datawritesize) - 1) & 0xFFF))) //Ending address in a different page? We can't write more!
						{
							memory_datawritesize = 1; //1 byte only!
							BIU[activeCPU].datawritesizeexpected = 1; //1 byte only!
						}
						BIU_directwb(physicaladdress, value, 0x100); //Write directly to memory now!
						if (MMU_waitstateactive) //No result yet?
						{
							BIU[activeCPU].currentrequest &= ~REQUEST_SUB1; //Request 8-bit half again(low byte)!
							BIU[activeCPU].newrequest = 1; //We're a new request!
							return 1; //Keep polling!
						}
						if (unlikely(memory_datawrittensize != BIU[activeCPU].datawritesizeexpected)) //Wrong size than expected?
						{
							BIU[activeCPU].datawritesizeexpected = 1; //Expect 1 byte for all other bytes!
							memory_datawritesize = 1; //1 byte from now on!
						}
						else if (useIPSclock && (BIU[activeCPU].datawritesizeexpected == BIU[activeCPU].newtransfer_size)) //Data already fully written in IPS clocking mode?
						{
							if (BIU_response(1)) //Result given? We're giving OK!
							{
								BIU_terminatemem(); //Terminate memory access!
								BIU[activeCPU].waitstateRAMremaining += memory_waitstates; //Apply the waitstates for the fetch!
								BIU[activeCPU].currentrequest = REQUEST_NONE; //No request anymore! We're finished!
								BIU[activeCPU].newrequest = 0; //We're not a new request!
								return 1; //Handled!
							}
						}
						++BIU[activeCPU].currentaddress; //Next address!
						if (unlikely((BIU[activeCPU].currentaddress & CPU_databusmask) == 0))
						{
							BIU[activeCPU].waitstateRAMremaining += memory_waitstates; //Apply the waitstates for the fetch!
							BIU[activeCPU].newrequest = 0; //We're not a new request!
							return 1; //Handled, but broken up at this point due to the data bus not supporting transferring the rest of the word in one go!
						}
						BIU[activeCPU].newrequest = 0; //We're not a new request!
						goto fulltransferMMUwrite; //Start Full transfer, when available?
					}
					return 1; //Handled!
					break;
					//I/O operations!
				case REQUEST_IOREAD:
					if (BUSactive == 2) return 1; //BUS taken?
					BUSactive = BIU[activeCPU].BUSactive = 1; //Start memory or BUS cycles!
					BIU[activeCPU].newtransfer = 1; //We're a new transfer!
					BIU[activeCPU].newtransfer_size = 1; //We're a new transfer!
					if ((BIU[activeCPU].currentrequest & REQUEST_16BIT) || (BIU[activeCPU].currentrequest & REQUEST_32BIT)) //16/32-bit?
					{
						BIU[activeCPU].newtransfer_size = 2; //We're a new transfer!
						BIU[activeCPU].currentrequest |= REQUEST_SUB1; //Request 16-bit half next(high byte)!
						if (BIU[activeCPU].currentrequest & REQUEST_32BIT) //32-bit?
						{
							BIU[activeCPU].newtransfer_size = 4; //We're a new transfer!
						}
					}
					BIU[activeCPU].currentaddress = (BIU[activeCPU].currentpayload[0] & 0xFFFFFFFF); //Address to use!
					if (BIU[activeCPU].currentrequest & REQUEST_32BIT) //32-bit?
					{
						BIU[activeCPU].currentresult = PORT_IN_D(BIU[activeCPU].currentaddress & 0xFFFF); //Read byte!
					}
					else if (BIU[activeCPU].currentrequest & REQUEST_16BIT) //16-bit?
					{
						BIU[activeCPU].currentresult = PORT_IN_W(BIU[activeCPU].currentaddress & 0xFFFF); //Read byte!
					}
					else //8-bit?
					{
						BIU[activeCPU].currentresult = PORT_IN_B(BIU[activeCPU].currentaddress & 0xFFFF); //Read byte!
					}
					PCI_finishtransfer(); //Terminate the bus cycle!
					if ((BIU[activeCPU].currentrequest & REQUEST_SUBMASK) == REQUEST_SUB0) //Finished the request?
					{
						if (BIU_response(BIU[activeCPU].currentresult)) //Result given?
						{
							BIU[activeCPU].waitstateRAMremaining += bus_waitstates; //Apply the waitstates for the fetch!
							BIU[activeCPU].currentrequest = REQUEST_NONE; //No request anymore! We're finished!
							BIU[activeCPU].newrequest = 0; //We're not a new request!
						}
						else //Response failed?
						{
							BIU[activeCPU].currentrequest &= ~REQUEST_SUB1; //Request low 8-bit half again(low byte)!
							BIU[activeCPU].newrequest = 0; //We're not a new request!
						}
					}
					else
					{
						++BIU[activeCPU].currentaddress; //Next address!
						if (unlikely((BIU[activeCPU].currentaddress & CPU_databusmask) == 0))
						{
							BIU[activeCPU].waitstateRAMremaining += bus_waitstates; //Apply the waitstates for the fetch!
							BIU[activeCPU].newrequest = 0; //We're not a new request!
							return 1; //Handled, but broken up at this point due to the data bus not supporting transferring the rest of the word in one go!
						}
						BIU[activeCPU].newrequest = 0; //We're not a new request!
						goto fulltransferIOread; //Start Full transfer, when available?
					}
					return 1; //Handled!
					break;
				case REQUEST_IOWRITE:
					if (BUSactive == 2) return 1; //BUS taken?
					BUSactive = BIU[activeCPU].BUSactive = 1; //Start memory or BUS cycles!
					BIU[activeCPU].newtransfer = 1; //We're a new transfer!
					BIU[activeCPU].newtransfer_size = 1; //We're a new transfer!
					if ((BIU[activeCPU].currentrequest & REQUEST_16BIT) || (BIU[activeCPU].currentrequest & REQUEST_32BIT)) //16/32-bit?
					{
						BIU[activeCPU].newtransfer_size = 2; //We're a new transfer!
						BIU[activeCPU].currentrequest |= REQUEST_SUB1; //Request 16-bit half next(high byte)!
						if (BIU[activeCPU].currentrequest & REQUEST_32BIT) //32-bit?
						{
							BIU[activeCPU].newtransfer_size = 4; //We're a new transfer!
						}
					}
					BIU[activeCPU].currentaddress = (BIU[activeCPU].currentpayload[0] & 0xFFFFFFFF); //Address to use!
					if (BIU[activeCPU].currentrequest & REQUEST_32BIT) //32-bit?
					{
						BIU[activeCPU].currentrequest |= REQUEST_SUB1; //Request 16-bit half next(high byte)!
						PORT_OUT_D((word)(BIU[activeCPU].currentpayload[0] & 0xFFFF), (uint_32)((BIU[activeCPU].currentpayload[0] >> 32) & 0xFFFFFFFF)); //Write to memory now!									
					}
					else if (BIU[activeCPU].currentrequest & REQUEST_16BIT) //16-bit?
					{
						BIU[activeCPU].currentrequest |= REQUEST_SUB1; //Request 16-bit half next(high byte)!
						PORT_OUT_W((word)(BIU[activeCPU].currentpayload[0] & 0xFFFF), (word)((BIU[activeCPU].currentpayload[0] >> 32) & 0xFFFFFFFF)); //Write to memory now!									
					}
					else //8-bit?
					{
						PORT_OUT_B((word)(BIU[activeCPU].currentpayload[0] & 0xFFFF), (byte)((BIU[activeCPU].currentpayload[0] >> 32) & 0xFFFFFFFF)); //Write to memory now!									
					}
					PCI_finishtransfer(); //Terminate the bus cycle!
					if ((BIU[activeCPU].currentrequest & REQUEST_SUBMASK) == REQUEST_SUB0) //Finished the request?
					{
						if (BIU_response(1)) //Result given? We're giving OK!
						{
							BIU[activeCPU].waitstateRAMremaining += bus_waitstates; //Apply the waitstates for the fetch!
							BIU[activeCPU].currentrequest = REQUEST_NONE; //No request anymore! We're finished!
							BIU[activeCPU].newrequest = 0; //We're not a new request!
						}
						else //Response failed?
						{
							BIU[activeCPU].currentrequest &= ~REQUEST_SUB1; //Request low 8-bit half again(low byte)!
							BIU[activeCPU].newrequest = 0; //We're not a new request!
						}
					}
					else
					{
						++BIU[activeCPU].currentaddress; //Next address!
						if (unlikely((BIU[activeCPU].currentaddress & CPU_databusmask) == 0))
						{
							BIU[activeCPU].waitstateRAMremaining += bus_waitstates; //Apply the waitstates for the fetch!
							BIU[activeCPU].newrequest = 0; //We're not a new request!
							return 1; //Handled, but broken up at this point due to the data bus not supporting transferring the rest of the word in one go!
						}
						BIU[activeCPU].newrequest = 0; //We're not a new request!
						goto fulltransferIOwrite; //Start Full transfer, when available?
					}
					return 1; //Handled!
					break;
				default:
				case REQUEST_NONE: //Unknown request?
					BIU[activeCPU].currentrequest = REQUEST_NONE; //No request anymore! We're finished!
					BIU[activeCPU].newrequest = 0; //We're not a new request!
					break; //Ignore the entire request!
				}
			}
		}
	}
	return 0; //No requests left!
}

byte CPU386_WAITSTATE_DELAY = 0; //386+ Waitstate, which is software-programmed?

//BIU current state handling information used by below state handlers!
byte memory_waitstates[MAXCPUS], bus_waitstates[MAXCPUS];
byte PIQ_RequiredSize[MAXCPUS],PIQ_CurrentBlockSize[MAXCPUS]; //The required size for PIQ transfers!
byte BIU_active[MAXCPUS]; //Are we counted as active cycles?

OPTINLINE void BIU_WaitState() //General Waitstate handler!
{
	BIU[activeCPU].TState = 0xFF; //Waitstate RAM/BUS!
	BIU_active[activeCPU] = 0; //Count as inactive BIU: don't advance cycles!
}

void BIU_detectCycle(); //Detect the cycle to execute!
void BIU_cycle_StallingBUS() //Stalling BUS?
{
	BIU[activeCPU].stallingBUS = 1; //Stalling!
	if (unlikely(--BIU[activeCPU].currentcycleinfo->cycles_stallBUS==0)) //Stall!
	{
		BIU_detectCycle(); //Detect the next cycle to execute!
	}
}

void BIU_cycle_VideoWaitState() //Video Waitstate active?
{
	BIU[activeCPU].stallingBUS = 0; //Not stalling BUS!
	if (unlikely((CPU[activeCPU].halt&0xC) == 8)) //Are we to resume execution now?
	{
		CPU[activeCPU].halt &= ~0xC; //We're resuming execution!
		BIU_detectCycle(); //We're resuming from HLT state!
		BIU[activeCPU].currentcycleinfo->currentTimingHandler(); //Execute the new state directly!
	}
	else
	{
		BIU_WaitState(); //Execute the waitstate!
	}
}

void BIU_cycle_WaitStateRAMBUS() //Waiting for WaitState RAM/BUS?
{
	BIU[activeCPU].stallingBUS = 0; //Not stalling BUS!
	//WaitState RAM/BUS busy?
	BIU_WaitState();
	if (unlikely((--BIU[activeCPU].waitstateRAMremaining)==0)) //Ticked waitstate RAM to finish!
	{
		BIU_detectCycle(); //Detect the next cycle!
	}
}

void BIU_handleRequestsIPS() //Handle all pending requests at once!
{
	if (BUSactive == 2)
	{
		BIU[activeCPU].handlerequestPending = &BIU_handleRequestsIPS; //We're keeping pending to handle!
		return; //BUS taken?
	}
	if (unlikely(BIU_processRequests(0, 0))) //Processing a request?
	{
		checkBIUBUSrelease(); //Check for release!
		BIU[activeCPU].requestready = 1; //The request is ready to be served!
		for (; BIU_processRequests(0, 0);) //More requests to handle?
		{
			checkBIUBUSrelease(); //Check for release!
			BIU[activeCPU].requestready = 1; //The request is ready to be served!
			if ((BIU[activeCPU]._lock == 2) || MMU_waitstateactive) //Waiting for the BUS to be unlocked? Abort this handling!
			{
				BIU[activeCPU].handlerequestPending = &BIU_handleRequestsIPS; //We're keeping pending to handle!
				goto handleBusLockPending; //Handle the bus locking pending!
			}
			BIU[activeCPU].BUSactive = 0; //Inactive BUS!
		}
		BIU[activeCPU].handlerequestPending = &BIU_handleRequestsNOP; //Nothing is pending anymore!
		BIU[activeCPU].BUSactive = 0; //Inactive BUS!
		handleBusLockPending: //Bus lock is pending?
		checkBIUBUSrelease(); //Check for release!
		BIU[activeCPU].requestready = 1; //The request is ready to be served!
	}
	else //Nothing to do?
	{
		BIU[activeCPU].handlerequestPending = &BIU_handleRequestsIPS; //We're keeping pending to handle!
	}
}

void BIU_handleRequestsPending()
{
	BIU[activeCPU].handlerequestPending(); //Handle all pending requests if they exist!
}

void BIU_handleRequestsNOP()
{
	//NOP!
}

void BIU_cycle_active8086() //Everything not T1 cycle!
{
	BIU[activeCPU].stallingBUS = 0; //Not stalling BUS!
	if (unlikely(BUSactive==2)) //Handling a DRAM refresh? We're idling on DMA!
	{
		++CPU[activeCPU].cycles_Prefetch_DMA;
		BIU[activeCPU].TState = 0xFE; //DMA cycle special identifier!
		BIU_active[activeCPU] = 0; //Count as inactive BIU: don't advance cycles!
	}
	else //Active CPU cycle?
	{
		blockDMA = 0; //Not blocking DMA anymore!
		BIU[activeCPU].currentcycleinfo->curcycle = (BIU[activeCPU].prefetchclock&3); //Current cycle!
		if (unlikely(BIU[activeCPU].currentcycleinfo->cycles_stallBIU)) //To stall?
		{
			--BIU[activeCPU].currentcycleinfo->cycles_stallBIU; //Stall the BIU instead of normal runtime!
			BIU[activeCPU].stallingBUS = 3; //Stalling fetching!
			if (unlikely(BIU[activeCPU].BUSactive==1)) //We're active?
			{
				if (likely((BIU[activeCPU].prefetchclock&3)!=0)) //Not T1 yet?
				{
					if (unlikely((++BIU[activeCPU].prefetchclock&3)==0)) //From T4 to T1?
					{
						BIU[activeCPU].BUSactive = 0; //Inactive BUS!
						checkBIUBUSrelease(); //Check for release!
					}
				}
			}
			else
			{
				if (BIU[activeCPU].currentcycleinfo->cycles) --BIU[activeCPU].currentcycleinfo->cycles; //Decrease the cycles as needed for activity!
				BIU_active[activeCPU] = 0; //Count as inactive BIU: don't advance cycles!
			}
		}
		else if (unlikely((BIU[activeCPU].currentcycleinfo->curcycle==0) && (BIU[activeCPU].BUSactive==0))) //T1 while not busy? Start transfer, if possible!
		{
			if (unlikely(BIU[activeCPU].currentcycleinfo->prefetchcycles)) {--BIU[activeCPU].currentcycleinfo->prefetchcycles; goto tryprefetch808X;}
			else
			{
				tryprefetch808X:
				if (unlikely(BIU_processRequests(memory_waitstates[activeCPU],bus_waitstates[activeCPU]))) //Processing a request?
				{
					BIU[activeCPU].requestready = 0; //We're starting a request!
					++BIU[activeCPU].prefetchclock; //Tick!					
				}
				else if (likely(fifobuffer_freesize(BIU[activeCPU].PIQ)>=((uint_32)2>>CPU_databussize))) //Prefetch cycle when not requests are handled? Else, NOP cycle!
				{
					PIQ_block[activeCPU] = 0; //We're never blocking(only 1 access)!
					CPU_fillPIQ(); //Add a byte to the prefetch!
					if (CPU_databussize == 0) CPU_fillPIQ(); //8086? Fetch words!
					if (BIU[activeCPU].BUSactive) //Gone active?
					{
						++CPU[activeCPU].cycles_Prefetch_BIU; //Cycles spent on prefetching on BIU idle time!
						BIU[activeCPU].waitstateRAMremaining += memory_waitstates[activeCPU]; //Apply the waitstates for the fetch!
						++BIU[activeCPU].prefetchclock; //Tick!
					}
				}
				else //Nothing to do?
				{
					BIU[activeCPU].stallingBUS = 2; //Stalling!
				}
			}
		}
		else if (likely(BIU[activeCPU].currentcycleinfo->curcycle)) //Busy transfer?
		{
			++BIU[activeCPU].prefetchclock; //Tick running transfer T-cycle!
		}
		if (unlikely((BIU[activeCPU].currentcycleinfo->curcycle==3) && ((BIU[activeCPU].prefetchclock&3)!=3) && (BIU[activeCPU].BUSactive==1))) //Finishing transfer on T4?
		{
			BIU[activeCPU].BUSactive = 0; //Inactive BUS!
			checkBIUBUSrelease(); //Check for release!
			BIU[activeCPU].requestready = 1; //The request is ready to be served!
			blockDMA = 1; //We're a DMA waiting cycle, don't start yet this cycle!
		}

		if (unlikely(BIU[activeCPU].currentcycleinfo->cycles && BIU_active[activeCPU])) --BIU[activeCPU].currentcycleinfo->cycles; //Decrease the amount of cycles that's left!
	}
	BIU_detectCycle(); //Detect the next cycle!
}

void BIU_cycle_active286()
{
	if (unlikely(BUSactive==2)) //Handling a DRAM refresh? We're idling on DMA!
	{
		++CPU[activeCPU].cycles_Prefetch_DMA;
		BIU[activeCPU].TState = 0xFE; //DMA cycle special identifier!
		BIU_active[activeCPU] = 0; //Count as inactive BIU: don't advance cycles!
	}
	else //Active CPU cycle?
	{
		blockDMA = 0; //Not blocking DMA anymore!
		BIU[activeCPU].currentcycleinfo->curcycle = (BIU[activeCPU].prefetchclock&1); //Current cycle!
		if (unlikely(BIU[activeCPU].currentcycleinfo->cycles_stallBIU)) //To stall?
		{
			--BIU[activeCPU].currentcycleinfo->cycles_stallBIU; //Stall the BIU instead of normal runtime!
			BIU[activeCPU].stallingBUS = 3; //Stalling fetching!
			if (unlikely(BIU[activeCPU].BUSactive==1)) //We're active?
			{
				if (unlikely((BIU[activeCPU].prefetchclock&1)!=0)) //Not T1 yet?
				{
					if (likely((++BIU[activeCPU].prefetchclock&1)==0)) //From T2 to T1?
					{
						BIU[activeCPU].BUSactive = 0; //Inactive BUS!
						checkBIUBUSrelease(); //Check for release!
					}
				}
			}
		}
		else if (unlikely((BIU[activeCPU].currentcycleinfo->curcycle==0) && (BIU[activeCPU].BUSactive==0))) //T1 while not busy? Start transfer, if possible!
		{
			if (unlikely(BIU[activeCPU].currentcycleinfo->prefetchcycles)) {--BIU[activeCPU].currentcycleinfo->prefetchcycles; goto tryprefetch80286;}
			else
			{
				tryprefetch80286:
				PIQ_RequiredSize[activeCPU] = 1; //Minimum of 2 bytes required for a fetch to happen!
				PIQ_CurrentBlockSize[activeCPU] = 3; //We're blocking after 1 byte access when at an odd address!
				if (EMULATED_CPU>=CPU_80386) //386+?
				{
					PIQ_RequiredSize[activeCPU] |= 2; //Minimum of 4 bytes required for a fetch to happen!
					PIQ_CurrentBlockSize[activeCPU] |= 4; //Apply 32-bit quantities as well, when allowed!
				}
				if (unlikely(BIU_processRequests(memory_waitstates[activeCPU],bus_waitstates[activeCPU]))) //Processing a request?
				{
					BIU[activeCPU].requestready = 0; //We're starting a request!
					++BIU[activeCPU].prefetchclock; //Tick!
				}
				else if (likely(fifobuffer_freesize(BIU[activeCPU].PIQ)>PIQ_RequiredSize[activeCPU])) //Prefetch cycle when not requests are handled(2 free spaces only)? Else, NOP cycle!
				{
					PIQ_block[activeCPU] = PIQ_CurrentBlockSize[activeCPU]; //We're blocking after 1 byte access when at an odd address at an odd word/dword address!
					CPU_fillPIQ(); CPU_fillPIQ(); //Add a word to the prefetch!
					if (likely((PIQ_RequiredSize[activeCPU] & 2) && ((EMULATED_CPU >= CPU_80386) && (CPU_databussize == 0)))) //DWord access on a 32-bit BUS, when allowed?
					{
						CPU_fillPIQ(); CPU_fillPIQ(); //Add another word to the prefetch!
					}
					if (BIU[activeCPU].BUSactive) //Gone active?
					{
						++CPU[activeCPU].cycles_Prefetch_BIU; //Cycles spent on prefetching on BIU idle time!
						BIU[activeCPU].waitstateRAMremaining += memory_waitstates[activeCPU]; //Apply the waitstates for the fetch!
						++BIU[activeCPU].prefetchclock; //Tick!
					}
				}
				else //Nothing to do?
				{
					BIU[activeCPU].stallingBUS = 2; //Stalling!
				}
			}
		}
		else if (likely(BIU[activeCPU].currentcycleinfo->curcycle)) //Busy transfer(not on 80486+)?
		{
			++BIU[activeCPU].prefetchclock; //Tick running transfer T-cycle!
		}
		if (unlikely(((BIU[activeCPU].currentcycleinfo->curcycle==1) && ((BIU[activeCPU].prefetchclock&1)!=1)) && (BIU[activeCPU].BUSactive==1))) //Finishing transfer on T1(80486+ finishes in 1 cycle)?
		{
			BIU[activeCPU].BUSactive = 0; //Inactive BUS!
			checkBIUBUSrelease(); //Check for release!
			BIU[activeCPU].requestready = 1; //The request is ready to be served!
			blockDMA = 1; //We're a DMA waiting cycle, don't start yet this cycle!
		}
		if (unlikely(BIU[activeCPU].currentcycleinfo->cycles && BIU_active[activeCPU])) --BIU[activeCPU].currentcycleinfo->cycles; //Decrease the amount of cycles that's left!
	}
	BIU_detectCycle(); //Detect the next cycle!
}

byte BIU_getHLDA()
{
	return 1; //Always active!
}

void BIU_cycle_active486()
{
	if (unlikely(BUSactive == 2)) //Handling a DRAM refresh? We're idling on DMA!
	{
		++CPU[activeCPU].cycles_Prefetch_DMA;
		BIU[activeCPU].TState = 0xFE; //DMA cycle special identifier!
		BIU_active[activeCPU] = 0; //Count as inactive BIU: don't advance cycles!
	}
	else //Active CPU cycle?
	{
		blockDMA = 0; //Not blocking DMA anymore!
		BIU[activeCPU].currentcycleinfo->curcycle = (BIU[activeCPU].prefetchclock & 1); //Current cycle!
		if (unlikely(BIU[activeCPU].currentcycleinfo->cycles_stallBIU)) //To stall?
		{
			--BIU[activeCPU].currentcycleinfo->cycles_stallBIU; //Stall the BIU instead of normal runtime!
			BIU[activeCPU].stallingBUS = 3; //Stalling fetching!
			BIU_active[activeCPU] = 0; //Count as inactive BUS: don't advance cycles!
			BIU[activeCPU].BUSactive = 0; //Inactive BUS!
			checkBIUBUSrelease(); //Check for release!
		}
		else if (unlikely((BIU[activeCPU].currentcycleinfo->curcycle == 0) && (BIU[activeCPU].BUSactive == 0))) //T1 while not busy? Start transfer, if possible!
		{
			if (unlikely(BIU[activeCPU].currentcycleinfo->prefetchcycles)) { --BIU[activeCPU].currentcycleinfo->prefetchcycles; goto tryprefetch80286; }
			else
			{
			tryprefetch80286:
				PIQ_RequiredSize[activeCPU] = 1; //Minimum of 2 bytes required for a fetch to happen!
				PIQ_CurrentBlockSize[activeCPU] = 3; //We're blocking after 1 byte access when at an odd address!
				if (EMULATED_CPU >= CPU_80386) //386+?
				{
					PIQ_RequiredSize[activeCPU] |= 2; //Minimum of 4 bytes required for a fetch to happen!
					PIQ_CurrentBlockSize[activeCPU] |= 4; //Apply 32-bit quantities as well, when allowed!
				}
				if (unlikely(BIU_processRequests(memory_waitstates[activeCPU], bus_waitstates[activeCPU]))) //Processing a request?
				{
					BIU[activeCPU].requestready = 0; //We're starting a request!
				}
				else if (likely(fifobuffer_freesize(BIU[activeCPU].PIQ)>PIQ_RequiredSize[activeCPU])) //Prefetch cycle when not requests are handled(2 free spaces only)? Else, NOP cycle!
				{
					PIQ_block[activeCPU] = PIQ_CurrentBlockSize[activeCPU]; //We're blocking after 1 byte access when at an odd address at an odd word/dword address!
					CPU_fillPIQ(); CPU_fillPIQ(); //Add a word to the prefetch!
					if (likely((PIQ_RequiredSize[activeCPU] & 2) && ((EMULATED_CPU >= CPU_80386) && (CPU_databussize == 0)))) //DWord access on a 32-bit BUS, when allowed?
					{
						CPU_fillPIQ(); CPU_fillPIQ(); //Add another word to the prefetch!
					}
					if (BIU[activeCPU].BUSactive) //Gone active?
					{
						++CPU[activeCPU].cycles_Prefetch_BIU; //Cycles spent on prefetching on BIU idle time!
						BIU[activeCPU].waitstateRAMremaining += memory_waitstates[activeCPU]; //Apply the waitstates for the fetch!
					}
				}
				else //Nothing to do?
				{
					BIU[activeCPU].stallingBUS = 2; //Stalling!
				}
			}
		}
		if (likely(BIU[activeCPU].BUSactive == 1)) //Finishing transfer on T1(80486+ finishes in 1 cycle)?
		{
			BIU[activeCPU].BUSactive = 0; //Inactive BUS!
			checkBIUBUSrelease(); //Check for release!
			BIU[activeCPU].requestready = 1; //The request is ready to be served!
			blockDMA = 1; //We're a DMA waiting cycle, don't start yet this cycle!
		}
		if (unlikely(BIU[activeCPU].currentcycleinfo->cycles && BIU_active[activeCPU])) --BIU[activeCPU].currentcycleinfo->cycles; //Decrease the amount of cycles that's left!
	}
	BIU_detectCycle(); //Detect the next cycle!
}

void BIU_detectCycle() //Detect the cycle to execute!
{
	if (unlikely(BIU[activeCPU].currentcycleinfo->cycles_stallBUS && ((BIU[activeCPU].BUSactive!=1) || (BUSactive==2)))) //Stall the BUS? This happens only while the BUS is released by CPU or DMA!
	{
		BIU[activeCPU].currentcycleinfo->currentTimingHandler = &BIU_cycle_StallingBUS; //We're stalling the BUS!
	}
	else if (unlikely((CPU[activeCPU].halt & 0xC) && (((BIU[activeCPU].prefetchclock&BIU_numcyclesmask)==BIU_numcyclesmask)||BIU_is_486))) //CGA wait state is active?
	{
		BIU[activeCPU].currentcycleinfo->currentTimingHandler = &BIU_cycle_VideoWaitState; //We're stalling the BUS!		
	}
	else if (unlikely((((BIU[activeCPU].prefetchclock&BIU_numcyclesmask)==BIU_numcyclesmask)||BIU_is_486) && (BIU[activeCPU].waitstateRAMremaining))) //T2/4? Check for waitstate RAM first!
	{
		BIU[activeCPU].currentcycleinfo->currentTimingHandler = &BIU_cycle_WaitStateRAMBUS; //We're stalling the BUS!		
	}
	else //Active cycle?
	{
		BIU[activeCPU].currentcycleinfo->currentTimingHandler = BIU_activeCycleHandler; //Active CPU cycle!
	}
}

void detectBIUactiveCycleHandler()
{
	BIU_activeCycleHandler = (EMULATED_CPU > CPU_NECV30) ? (BIU_is_486 ? &BIU_cycle_active486 : &BIU_cycle_active286) : &BIU_cycle_active8086; //What cycle handler are we to use?
	BIU_handleRequests = (useIPSclock) ? &BIU_handleRequestsIPS : &BIU_handleRequestsNOP; //Either NOP variant or IPS clocking version!
}

extern byte is_XT; //Are we emulating an XT architecture?

void CPU_tickBIU()
{
	if (likely(useIPSclock == 0)) //Not using IPS clocking?
	{
		BIU[activeCPU].currentcycleinfo = &BIU[activeCPU].cycleinfo; //Our cycle info to use!

		//Determine memory/bus waitstate first!
		memory_waitstates[activeCPU] = 0;
		bus_waitstates[activeCPU] = 0;
		BIU_active[activeCPU] = 1; //We're active by default!
		if (EMULATED_CPU==CPU_80286) //Process normal memory cycles!
		{
			memory_waitstates[activeCPU] += CPU286_WAITSTATE_DELAY; //One waitstate RAM!
			bus_waitstates[activeCPU] += CPU286_BUSWAITSTATE_DELAY; //Waitstate I/O!
		}
		else if (EMULATED_CPU==CPU_80386) //Waitstate memory to add?
		{
			memory_waitstates[activeCPU] += CPU386_WAITSTATE_DELAY; //One waitstate RAM!
		}
		if (is_XT && ((EMULATED_CPU!=CPU_80286) && (EMULATED_CPU!=CPU_80386))) //XT 80(1)86 has 1 bus waitstate!
		{
			bus_waitstates[activeCPU] = CPU80X86_XTBUSWAITSTATE_DELAY; //One waitstate on bus cycles!
		}

		//Now, normal processing!
		if (unlikely(BIU[activeCPU].PIQ==NULL)) return; //Disable invalid PIQ!
		if (unlikely((BIU[activeCPU].currentcycleinfo->cycles==0) && (BIU[activeCPU].currentcycleinfo->cycles_stallBUS==0))) //Are we ready to continue into the next phase?
		{
			BIU[activeCPU].currentcycleinfo->cycles = CPU[activeCPU].cycles; //How many cycles have been spent on the instruction?
			if (BIU[activeCPU].currentcycleinfo->cycles==0) BIU[activeCPU].currentcycleinfo->cycles = 1; //Take 1 cycle at least!

			BIU[activeCPU].currentcycleinfo->prefetchcycles = CPU[activeCPU].cycles_Prefetch; //Prefetch cycles!
			BIU[activeCPU].currentcycleinfo->prefetchcycles += CPU[activeCPU].cycles_EA; //EA cycles!
			BIU[activeCPU].currentcycleinfo->cycles_stallBIU = CPU[activeCPU].cycles_stallBIU; //BIU stall cycles!
			BIU[activeCPU].currentcycleinfo->cycles_stallBUS = CPU[activeCPU].cycles_stallBUS; //BUS stall cycles!
			CPU[activeCPU].cycles_Prefetch = CPU[activeCPU].cycles_EA = CPU[activeCPU].cycles_stallBIU = CPU[activeCPU].cycles_stallBUS = 0; //We don't have any of these after this!
			BIU_detectCycle(); //Detect the current cycle to execute!
		}

		//Now we have the amount of cycles we're idling.
		BIU[activeCPU].TState = ((BIU[activeCPU].prefetchclock&BIU_numcyclesmask)); //Currently emulated T-state!
		BIU[activeCPU].currentcycleinfo->currentTimingHandler(); //Run the current handler!
	}

	CPU[activeCPU].cycles = 1; //Only take 1 cycle: we're cycle-accurate emulation of the BIU(and EU by extension, since we handle that part indirectly as well in our timings, resulting in the full CPU timings)!
}

byte BIU_Busy() //Is the BIU busy on something? It's not ready at T1 state?
{
	return ((BIU[activeCPU].requestready == 0) || ((BIU[activeCPU].cycleinfo.currentTimingHandler != BIU_activeCycleHandler) && BIU[activeCPU].cycleinfo.currentTimingHandler) || (BIU[activeCPU].cycleinfo.cycles_stallBIU) || ((BIU[activeCPU].prefetchclock & BIU_numcyclesmask))); //Not ready for anything new?
}

byte BIU_Ready() //Are we ready to continue execution?
{
	return ((BIU[activeCPU].cycleinfo.cycles==0) && (BIU[activeCPU].cycleinfo.cycles_stallBUS==0) && (BIU[activeCPU].cycleinfo.prefetchcycles==0)); //We're ready to execute the next instruction (or instruction step) when all cycles are handled(no hardware interrupts are busy)!
}

byte BIU_resetRequested()
{
	return (CPU[activeCPU].resetPending && ((BIU_Ready() && (CPU[activeCPU].halt==0))||CPU[activeCPU].halt==1) && (BIU[activeCPU].BUSactive==0)); //Finished executing or halting, and reset is Pending?
}
