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

#include "headers/types.h" //Basic type support!
#include "headers/hardware/pic.h" //Basic data!
#include "headers/hardware/ports.h" //Port support!
#include "headers/mmu/mmuhandler.h" //Basic MMU handler support!
#include "headers/cpu/cpu.h" //Emulated CPU support!

//PIC Info: http://www.brokenthorn.com/Resources/OSDevPic.html

//Are we disabled?
#define __HW_DISABLED 0

#define DELIVERYPENDING (1<<12)
#define REMOTEPENDING (1<<14)

byte numemulatedcpus = 1; //Amount of emulated CPUs!

PIC i8259;
byte irr3_dirty = 0; //IRR3/IRR3_a is changed?

struct
{
	sbyte enabled; //Enabled? -1=CPU disabled, 0=Soft disable, 1=Enabled
	//Basic information?
	word needstermination; //APIC needs termination?
	//CPU MSR information!
	uint_32 windowMSRlo;
	uint_32 windowMSRhi; //Window register that's written in the CPU!
	//Runtime information!
	uint_64 baseaddr; //Base address of the APIC!
	//Remaining variables? All memory that's stored in the APIC!

	//Differential detection
	uint_32 prevSpuriousInterruptVectorRegister; //The previous value before the write!
	uint_64 LAPIC_timerremainder; //How much time remained?
	byte LAPIC_timerdivider; //The divider of the timer!

	//Now, the actual memory for the LAPIC!
	byte LAPIC_requirestermination[0x400]; //Dword requires termination?
	byte LAPIC_globalrequirestermination; //Is termination required at all for the Local APIC?
	uint_32 LAPIC_arbitrationIDregister; //Arbitration ID, set at INIT deassert and RESET!
	uint_32 LAPIC_ID; //0020
	uint_32 LAPIC_version; //0030
	uint_32 TaskPriorityRegister; //0080
	uint_32 ArbitrationPriorityRegister; //0090
	uint_32 ProcessorPriorityRegister; //00A0
	uint_32 EOIregister; //00B0
	uint_32 RemoteReadRegister; //00C0
	uint_32 LogicalDestinationRegister; //00D0
	uint_32 DestinationFormatRegister; //00E0
	uint_32 SpuriousInterruptVectorRegister; //00F0
	uint_32 ISR[8]; //ISRs! 0100-0170
	uint_32 TMR[8]; //TMRs! 0180-01F0
	uint_32 IRR[8]; //IRRs! 0200-0270
	uint_32 ErrorStatusRegister; //0280
	uint_32 LVTCorrectedMachineCheckInterruptRegister; //02F0
	uint_32 InterruptCommandRegisterLo; //0300
	uint_32 InterruptCommandRegisterHi; //0310
	uint_32 LVTTimerRegister; //0320
	byte LVTTimerRegisterDirty;
	uint_32 LVTThermalSensorRegister; //0330
	uint_32 LVTPerformanceMonitoringCounterRegister; //0340
	uint_32 LVTLINT0Register; //0350. Connected to PIC master.
	byte LVTLINT0RegisterDirty;
	uint_32 LVTLINT1Register; //0560. Connectd to NMI pin.
	byte LVTLINT1RegisterDirty;
	uint_32 LVTErrorRegister; //0370
	byte LVTErrorRegisterDirty;
	uint_32 InitialCountRegister; //0380
	uint_32 CurrentCountRegister; //0390
	uint_32 DivideConfigurationRegister; //03E0

	uint_32 CurrentCountRegisterlatched; //Latched timer!
	byte CurrentCountRegisterTicking; //Is the count register ticking now?
	sword LAPIC_extIntPending;
	uint_32 errorstatusregisterpending; //Pending bits in the error status register!

	//Bookkeeping of the sending packet to various APICs pending!
	uint_32 InterruptCommandRegisterPendingReceiver; //Receivers still pending receiving!
	uint_32 InterruptCommandRegisterPendingIOAPIC; //IO APIC still pending receiving!
	uint_32 InterruptCommandRegisterReceivers; //What receivers have been determined?
	byte InterruptCommandRegisterReceiversDetermined; //Receivers have been determined?
} LAPIC[MAXCPUS]; //The Local APIC that's emulated!

byte lastLAPICAccepted[MAXCPUS]; //Last APIC accepted LVT result!
byte discardErrorTriggerResult[MAXCPUS]; //Discarded error trigger result!

struct
{
	byte enabled; //Enabled?
	//Basic information?
	word needstermination; //APIC needs termination?
	uint_64 IObaseaddr; //Base address of the I/O APIC!
	//Remaining variables? All memory that's stored in the APIC!
	uint_32 APIC_address; //Address register for the extended memory!

	//IRQ detection
	uint_32 IOAPIC_currentliveIRR; //Real live IRR status!
	uint_32 IOAPIC_liveIRR; //Live IRR status!
	uint_32 IOAPIC_IRRreq; //Is the IRR requested, but masked(1-bit values)
	uint_32 IOAPIC_IRRset; //Is the IRR set(1-bit values)
	uint_32 IOAPIC_IMRset; //Is the IMR routine set(1-bit values)

	//IO APIC address registers!
	uint_32 IOAPIC_Address; //Address register for the IOAPIC registers! 0000
	uint_32 IOAPIC_Value; //Value register for the IOAPIC registers! 0010

	//Now, the IO APIC registers
	uint_32 IOAPIC_ID; //00: ID
	uint_32 IOAPIC_version_numredirectionentries; //01: Version(bits 0-7), # of redirection entries(16-23).
	uint_32 IOAPIC_arbitrationpriority; //02: Arbitration priority(Bits 24-27), remainder is reserved.
	uint_32 IOAPIC_redirectionentry[24][2]; //10-3F: 2 dwords for each redirection entry setting! Total 48 dwords!
	byte IOAPIC_requirestermination[0x40]; //Termination required for this entry?
	byte IOAPIC_globalrequirestermination; //Is termination required for the IO APIC?
	uint_32 IOAPIC_redirectionentryReceivers[24]; //What receivers have been determined?
	byte IOAPIC_redirectionentryReceiversDetermined[24]; //Receivers have been determined?
} IOAPIC; //Only 1 IO APIC is possible?

byte addr22 = 0; //Address select of port 22h!
byte IMCR = 0; //Address selected. 00h=Connect INTR and NMI to the CPU. 01h=Disconnect INTR and NMI from the CPU.
extern byte NMIQueued; //NMI raised to handle? This can be handled by an Local APIC! This then clears said flag to acnowledge it!
extern byte APICNMIQueued[MAXCPUS]; //APIC-issued NMI queued?
byte recheckLiveIRRs = 0;

//i8259.irr is the complete status of all 8 interrupt lines at the moment. Any software having raised it's line, raises this. Otherwise, it's lowered(irr3 are all cleared)!
//i8259.irr2 is the live status of each of the parallel interrupt lines!
//i8259.irr3 is the identifier for request subchannels that are pending to be acnowledged(cleared when acnowledge and the interrupt is fired).

//Result: 0=Block to let the APIC handle NMI, otherwise, handle NMI directly by the CPU instead of the APIC!
byte CPU_NMI_APIC(byte whichCPU)
{
	return (((LAPIC[whichCPU].enabled==-1) && (whichCPU))?1:0); //APIC not disabled by the CPU! When not BSP, disable NMI (not connected to the other CPUs)!
}

void updateLAPICTimerSpeed(byte whichCPU)
{
	byte divider;
	divider = (LAPIC[whichCPU].DivideConfigurationRegister & 3) | ((LAPIC[whichCPU].DivideConfigurationRegister & 8) >> 1); //Divider set!
	if (divider == 7) //Actually 1?
	{
		LAPIC[whichCPU].LAPIC_timerdivider = 0; //Divide by 1!
	}
	else //2^n
	{
		LAPIC[whichCPU].LAPIC_timerdivider = 1+divider; //Calculate it!
	}
}

//Handle everything that needs to be done when resetting the APIC!
void resetIOAPIC(byte isHardReset)
{
	byte IRQnr;
	//Mask all interrupts!
	for (IRQnr = 0; IRQnr < NUMITEMS(IOAPIC.IOAPIC_redirectionentry); ++IRQnr) //Handle all IRQ handlers we support!
	{
		IOAPIC.IOAPIC_redirectionentry[IRQnr][0] |= 0x10000; //Masked, nothing else set yet, edge mode, active high!
	}
	IOAPIC.IOAPIC_IMRset = ~0; //Mask all set!
	IOAPIC.IOAPIC_IRRreq = 0; //Remove all pending requests!
	if (isHardReset) //Hard reset?
	{
		IOAPIC.APIC_address = 0; //Clear the address register as well!
	}
}

void updateLAPICArbitrationIDregister(byte whichCPU)
{
	LAPIC[whichCPU].LAPIC_arbitrationIDregister = LAPIC[whichCPU].LAPIC_ID & (0xFF << 24); //Load the Arbitration ID register from the Local APIC ID register! All 8-bits are loaded!
}

void updateIOAPICArbitrationIDregister()
{
	IOAPIC.IOAPIC_arbitrationpriority = IOAPIC.IOAPIC_ID & (0xFF << 24); //Load the Arbitration ID register from the Local APIC ID register! All 8-bits are loaded!
}

void initAPIC(byte whichCPU)
{
	LAPIC[whichCPU].enabled = 0; //Is the APIC enabled?
//Initialize only 1 Local APIC!
	LAPIC[whichCPU].baseaddr = 0xFEE00000; //Default base address!
	LAPIC[whichCPU].needstermination = 0; //Doesn't need termination!
	LAPIC[whichCPU].LAPIC_version = 0x0010;
	LAPIC[whichCPU].DestinationFormatRegister = ~0; //All bits set!
	LAPIC[whichCPU].SpuriousInterruptVectorRegister = 0xFF; //Needs to be 0xFF!

	switch (EMULATED_CPU)
	{
	case CPU_PENTIUM:
		LAPIC[whichCPU].LAPIC_version |= 0x30000; //4 LVT entries
		break;
	case CPU_PENTIUMPRO:
	case CPU_PENTIUM2:
		LAPIC[whichCPU].LAPIC_version |= 0x40000; //4 LVT entries? Or 5? Bochs says P6=4 entries? We use 5!
		break;
	default:
		break;
	}
	LAPIC[whichCPU].LAPIC_version |= (1 << 24); //Broadcast EOI suppression supported!

	//Update only 1 Local APIC!
	resetLAPIC(whichCPU,3); //Reset the APIC as well!
	updateLAPICTimerSpeed(whichCPU); //Update the used timer speed!
	updateLAPICArbitrationIDregister(whichCPU); //Update the Arbitration ID register with it's defaults!

	//Local APIC interrupt support!
	LAPIC[whichCPU].LAPIC_extIntPending = -1; //No external interrupt pending yet!
	APIC_updateWindowMSR(whichCPU, LAPIC[whichCPU].windowMSRlo, LAPIC[whichCPU].windowMSRhi); //Make sure that our enabled status is up-to-date!
}

void resetLAPIC(byte whichCPU, byte isHardReset)
{
	byte backupactiveCPU;
	//Do something when resetting?
	if (isHardReset)
	{
		if ((isHardReset != 3) && (isHardReset!=2)) //Not called by the initAPIC function? Also not a INIT call(type 2)?
		{
			initAPIC(whichCPU); //Initialize the APIC!
		}

		//Updating the local APIC ID always!
		LAPIC[whichCPU].LAPIC_ID = ((whichCPU & 0xFF) << 24); //Physical CPU number to receive at!

		if (isHardReset & 1) //Full reset of the LAPIC? Type 2(INIT) isn't applied here! Types 1(normal reset) and 3(Initialization of the board) apply here as a RESET handling!
		{
			//Power-up or RESET state specific!
			LAPIC[whichCPU].LVTCorrectedMachineCheckInterruptRegister = 0x10000; //Reset CMCI register!
			LAPIC[whichCPU].LVTTimerRegister = 0x10000; //Reset Timer register!
			LAPIC[whichCPU].LVTThermalSensorRegister = 0x10000; //Thermal sensor register!
			LAPIC[whichCPU].LVTPerformanceMonitoringCounterRegister = 0x10000; //Performance monitoring counter register!
			LAPIC[whichCPU].LVTLINT0Register = 0x10000; //Reset LINT0 register!
			LAPIC[whichCPU].LVTLINT1Register = 0x10000; //Reset LINT1 register!
			LAPIC[whichCPU].LVTErrorRegister = 0x10000; //Reset Error register!
			memset(&LAPIC[whichCPU].IRR, 0, sizeof(LAPIC[whichCPU].IRR)); //Cleared!
			memset(&LAPIC[whichCPU].ISR, 0, sizeof(LAPIC[whichCPU].ISR)); //Cleared!
			memset(&LAPIC[whichCPU].TMR, 0, sizeof(LAPIC[whichCPU].TMR)); //Cleared!
			LAPIC[whichCPU].InterruptCommandRegisterLo = LAPIC[whichCPU].InterruptCommandRegisterHi = 0; //Cleared!
			LAPIC[whichCPU].LogicalDestinationRegister = 0; //Cleared!
			LAPIC[whichCPU].TaskPriorityRegister = 0; //Cleared!
			LAPIC[whichCPU].DestinationFormatRegister = ~0; //All 1s!
			LAPIC[whichCPU].InitialCountRegister = 0; //Cleared!
			LAPIC[whichCPU].CurrentCountRegister = 0; //Cleared!
			LAPIC[whichCPU].CurrentCountRegisterlatched = 0; //Not latched anymore!
			LAPIC[whichCPU].CurrentCountRegisterTicking = 0; //Not ticking right now!
			LAPIC[whichCPU].needstermination = ~0 & ~4; //Init all statuses! Don't set ICR pending bit!
			LAPIC[whichCPU].LAPIC_globalrequirestermination = ~0; //Init all statuses!
			LAPIC[whichCPU].SpuriousInterruptVectorRegister = 0xFF; //Always set to this value, it's soft disabling the Local APIC by default!
			backupactiveCPU = activeCPU; //Backup!
			activeCPU = whichCPU; //Which one to reset!
			APIC_handletermination(); //Handle the termination!
			activeCPU = backupactiveCPU; //Restore the active CPU!
			updateLAPICTimerSpeed(whichCPU); //Update the used timer speed!
			IMCR = addr22 = 0; //Reset the IMCR register as well, as is documented for RESET!
		}
	}
	else //Soft disabled?
	{
		//Set the mask on all LVT entries! They become read-only!
		LAPIC[whichCPU].LVTCorrectedMachineCheckInterruptRegister |= 0x10000; //Reset CMCI register!
		LAPIC[whichCPU].LVTTimerRegister |= 0x10000; //Reset Timer register!
		LAPIC[whichCPU].LVTThermalSensorRegister |= 0x10000; //Thermal sensor register!
		LAPIC[whichCPU].LVTPerformanceMonitoringCounterRegister |= 0x10000; //Performance monitoring counter register!
		LAPIC[whichCPU].LVTLINT0Register |= 0x10000; //Reset LINT0 register!
		LAPIC[whichCPU].LVTLINT1Register |= 0x10000; //Reset LINT1 register!
		LAPIC[whichCPU].LVTErrorRegister |= 0x10000; //Reset Error register!
	}
	//Enabled is already handled automatically by the call to the updating of the Window MSR!
	//Soft reset doesn't clear any data of the Local APIC!
}

void APIC_enableIOAPIC(byte enabled)
{
	IOAPIC.enabled = enabled; //Enabled the IO APIC?
	if (IOAPIC.enabled == 0) //Disabled?
	{
		resetIOAPIC(0); //Reset a IO APIC by software!
	}
}

void init8259()
{
	byte whichCPU;
	if (__HW_DISABLED) return; //Abort!
	memset(&i8259, 0, sizeof(i8259));
	memset(&LAPIC, 0, sizeof(LAPIC));
	memset(&IOAPIC, 0, sizeof(IOAPIC));
	memset(&lastLAPICAccepted, 0, sizeof(lastLAPICAccepted)); //Nothing is accepted yet!
	//Now the port handling!
	//PIC0!
	register_PORTOUT(&out8259);
	register_PORTIN(&in8259);
	//All set up!

	i8259.imr[0] = 0xFF; //Mask off all interrupts to start!
	i8259.imr[1] = 0xFF; //Mask off all interrupts to start!
	irr3_dirty = 0; //Default: not dirty!

	//Initialize IO APIC!
	IOAPIC.enabled = 0; //Is the APIC enabled? This needs to be enabled based on hardware!

	//Initialize the IO APIC!
	IOAPIC.IObaseaddr = 0xFEC00000; //Default base address!
	IOAPIC.needstermination = 0; //Doesn't need termination!
	IOAPIC.IOAPIC_version_numredirectionentries = 0x11 | ((24 - 1) << 16); //How many IRQs can we handle(24) and version number!
	IOAPIC.IOAPIC_ID = 0x00; //Default IO APIC phyiscal ID!

	//External INTR support!
	addr22 = IMCR = 0x00; //Default values after powerup for the IMCR and related register!

	//Initialize all Local APICs!
	for (whichCPU = 0; whichCPU < numemulatedcpus; ++whichCPU)
	{
		initAPIC(whichCPU); //Only all Local APIC supported right now!
	}
	recheckLiveIRRs = 0; //Nothing to check!
}

byte APIC_errorTrigger(byte whichCPU); //Error has been triggered! Prototype!

byte APIC_getISRV(byte whichCPU)
{
	byte IRgroup;
	byte IR;
	uint_32 APIC_IRQsrequested[8], APIC_requestbit, APIC_requestsleft;
	//Determine PPR from ISRV(highest ISR vector number) and TPR.

	//First, find the MSb of the ISR to get the ISRV!
	APIC_IRQsrequested[0] = LAPIC[whichCPU].ISR[0]; //What can we handle!
	APIC_IRQsrequested[1] = LAPIC[whichCPU].ISR[1]; //What can we handle!
	APIC_IRQsrequested[2] = LAPIC[whichCPU].ISR[2]; //What can we handle!
	APIC_IRQsrequested[3] = LAPIC[whichCPU].ISR[3]; //What can we handle!
	APIC_IRQsrequested[4] = LAPIC[whichCPU].ISR[4]; //What can we handle!
	APIC_IRQsrequested[5] = LAPIC[whichCPU].ISR[5]; //What can we handle!
	APIC_IRQsrequested[6] = LAPIC[whichCPU].ISR[6]; //What can we handle!
	APIC_IRQsrequested[7] = LAPIC[whichCPU].ISR[7]; //What can we handle!
	if (!(APIC_IRQsrequested[0] | APIC_IRQsrequested[1] | APIC_IRQsrequested[2] | APIC_IRQsrequested[3] | APIC_IRQsrequested[4] | APIC_IRQsrequested[5] | APIC_IRQsrequested[6] | APIC_IRQsrequested[7]))
	{
		//No active ISR!
		IRgroup = IR = 0; //Nothing!
		goto foundPrioritizedISRV; //Found the vector!
	}
	//Find the most prioritized interrupt to fire!
	for (IRgroup = 7;; --IRgroup) //Process all possible groups to handle!
	{
		if (APIC_IRQsrequested[IRgroup]) //Something requested here?
		{
			//First, determine the highest priority IR to use!
			APIC_requestbit = (1U << 31); //What bit is requested first!
			APIC_requestsleft = 32; //How many are left!
			//Note: this way of handling the priority is done by the LAPIC as well(high nibble of the interrupt vector determines the priority)!
			for (IR = 31; APIC_requestsleft; --IR) //Check all requests!
			{
				if (APIC_IRQsrequested[IRgroup] & APIC_requestbit) //Are we requested to fire?
				{
					//Priority is based on the high nibble of the interrupt vector. The low nibble is ignored!
					goto foundPrioritizedISRV; //handle it!
				}
				APIC_requestbit >>= 1; //Next bit to check!
				--APIC_requestsleft; //One processed!
			}
		}
	}
foundPrioritizedISRV: //No ISR found?
	return (IRgroup << 5) | IR; //The interrupt that was fired!
}

byte APIC_getIRRV(byte whichCPU)
{
	byte IRgroup;
	byte IR;
	uint_32 APIC_IRQsrequested[8], APIC_requestbit, APIC_requestsleft;
	//Determine PPR from ISRV(highest ISR vector number) and TPR.

	//First, find the MSb of the ISR to get the ISRV!
	APIC_IRQsrequested[0] = LAPIC[whichCPU].IRR[0]; //What can we handle!
	APIC_IRQsrequested[1] = LAPIC[whichCPU].IRR[1]; //What can we handle!
	APIC_IRQsrequested[2] = LAPIC[whichCPU].IRR[2]; //What can we handle!
	APIC_IRQsrequested[3] = LAPIC[whichCPU].IRR[3]; //What can we handle!
	APIC_IRQsrequested[4] = LAPIC[whichCPU].IRR[4]; //What can we handle!
	APIC_IRQsrequested[5] = LAPIC[whichCPU].IRR[5]; //What can we handle!
	APIC_IRQsrequested[6] = LAPIC[whichCPU].IRR[6]; //What can we handle!
	APIC_IRQsrequested[7] = LAPIC[whichCPU].IRR[7]; //What can we handle!
	if (!(APIC_IRQsrequested[0] | APIC_IRQsrequested[1] | APIC_IRQsrequested[2] | APIC_IRQsrequested[3] | APIC_IRQsrequested[4] | APIC_IRQsrequested[5] | APIC_IRQsrequested[6] | APIC_IRQsrequested[7]))
	{
		//No active ISR!
		IRgroup = IR = 0; //Nothing!
		goto foundPrioritizedIRRV; //Found the vector!
	}
	//Find the most prioritized interrupt to fire!
	for (IRgroup = 7;; --IRgroup) //Process all possible groups to handle!
	{
		if (APIC_IRQsrequested[IRgroup]) //Something requested here?
		{
			//First, determine the highest priority IR to use!
			APIC_requestbit = (1U << 31); //What bit is requested first!
			APIC_requestsleft = 32; //How many are left!
			//Note: this way of handling the priority is done by the LAPIC as well(high nibble of the interrupt vector determines the priority)!
			for (IR = 31; APIC_requestsleft; --IR) //Check all requests!
			{
				if (APIC_IRQsrequested[IRgroup] & APIC_requestbit) //Are we requested to fire?
				{
					//Priority is based on the high nibble of the interrupt vector. The low nibble is ignored!
					goto foundPrioritizedIRRV; //handle it!
				}
				APIC_requestbit >>= 1; //Next bit to check!
				--APIC_requestsleft; //One processed!
			}
		}
	}
foundPrioritizedIRRV: //No ISR found?
	return (IRgroup << 5) | IR; //The interrupt that was fired!
}

//Updated for ISR changes!
void LAPIC_updatedISR(byte whichCPU)
{
	byte ISRV;
	ISRV = APIC_getISRV(whichCPU); //Get the ISRV!
	//Now, we have selected the highest priority IR! Start using it!
	LAPIC[whichCPU].ProcessorPriorityRegister = MAX((ISRV & 0xF0U), (LAPIC[whichCPU].TaskPriorityRegister & 0xF0U)); //Maximum of the two is Processor Priority Class
	//Determine the Processor Priority Sub-class
	if ((LAPIC[whichCPU].TaskPriorityRegister & 0xF0U) > (ISRV & 0xF0U)) //Use TPR 3:0!
	{
		LAPIC[whichCPU].ProcessorPriorityRegister |= (LAPIC[whichCPU].TaskPriorityRegister & 0xFU); //TPR 3:0!
	}
	else if ((LAPIC[whichCPU].TaskPriorityRegister & 0xF0U) == (ISRV & 0xF0U)) //Equal? TPR 3:0 or 0? Model-specific!
	{
		LAPIC[whichCPU].ProcessorPriorityRegister |= (LAPIC[whichCPU].TaskPriorityRegister & 0xFU); //TPR 3:0!
	}
	//Otherwise, zero!
}

//Updated for IRR and ISR changes!
void LAPIC_updatedIRRISR(byte whichCPU)
{
	byte IRRV, ISRV;
	IRRV = APIC_getIRRV(whichCPU); //Get the IRRV!
	ISRV = APIC_getISRV(whichCPU); //Get the ISRV!

	if (((LAPIC[whichCPU].TaskPriorityRegister & 0xF0U) >= (IRRV & 0xF0U)) && ((LAPIC[whichCPU].TaskPriorityRegister & 0xF0U) > (ISRV & 0xF0U))) //TPR is at least request and more than service?
	{
		LAPIC[whichCPU].ArbitrationPriorityRegister = (LAPIC[whichCPU].TaskPriorityRegister&0xFFU); //It's the TPR!
	}
	else
	{
		LAPIC[whichCPU].ArbitrationPriorityRegister = MAX((LAPIC[whichCPU].TaskPriorityRegister & 0xF0U), MAX((IRRV & 0xF0U), (ISRV & 0xF0U))); //Maximum of IRR, ISR and TPR! Lower 3 bits are 0!
	}
}

void LAPIC_broadcastEOI(byte whichCPU, byte vectornumber)
{
	byte intnr;
	//Do something with it on the IO APIC?
	for (intnr = 0; intnr < 24; ++intnr) //Check all
	{
		if ((IOAPIC.IOAPIC_redirectionentry[intnr][0] & 0xFF) == vectornumber) //Found a matching vector?
		{
			IOAPIC.IOAPIC_redirectionentry[intnr][0] &= ~REMOTEPENDING; //Clear bit 14: EOI received!
			recheckLiveIRRs = 1; //Recheck the live IRRs!
		}
	}
}

void LAPIC_handleunpendingerror(byte whichCPU)
{
	if (LAPIC[whichCPU].errorstatusregisterpending) //Pending error?
	{
		LAPIC[whichCPU].ErrorStatusRegister |= LAPIC[whichCPU].errorstatusregisterpending;
		LAPIC[whichCPU].errorstatusregisterpending = 0; //Not pending anymore!
	}
}

byte APIC_errorTriggerDummy;
void LAPIC_reportErrorStatus(byte whichcpu, uint_32 errorstatus, byte ignoreTrigger)
{
	APIC_errorTriggerDummy = APIC_errorTrigger(whichcpu); //Trigger it when possible!
	//Always set the error status register, even when the LVT is masked off!
	{
		LAPIC[whichcpu].errorstatusregisterpending |= errorstatus; //Reporting this delayed if needed, on the ESR!
		if ((((LAPIC[whichcpu].LAPIC_version >> 16) & 0xFF)) > 3) //No delayed reporting?
		{
			LAPIC_handleunpendingerror(whichcpu); //Unpend the error status register (ESR) immediately!
		}
	}
}

void LAPIC_handletermination() //Handle termination on the APIC!
{
	byte MSb;
	word MSBleft;
	//Handle any writes to APIC addresses!
	if (likely((LAPIC[activeCPU].needstermination|LAPIC[activeCPU].LAPIC_globalrequirestermination) == 0)) return; //No termination needed?

	//Now, handle the termination of the various registers!
	if (LAPIC[activeCPU].needstermination & 1) //Needs termination due to possible reset?
	{
		if (((LAPIC[activeCPU].SpuriousInterruptVectorRegister & 0x100) == 0) && ((LAPIC[activeCPU].prevSpuriousInterruptVectorRegister & 0x100))) //Cleared?
		{
			LAPIC[activeCPU].prevSpuriousInterruptVectorRegister = LAPIC[activeCPU].SpuriousInterruptVectorRegister; //Prevent loops!
			resetLAPIC(activeCPU,0); //Reset the APIC!
			LAPIC[activeCPU].enabled = 0; //Soft disabled!
		}
		else if (((LAPIC[activeCPU].prevSpuriousInterruptVectorRegister & 0x100) == 0) && ((LAPIC[activeCPU].SpuriousInterruptVectorRegister & 0x100))) //Set?
		{
			LAPIC[activeCPU].enabled = 1; //Soft enabled!
		}
	}

	if (LAPIC[activeCPU].needstermination & 2) //Needs termination due to possible EOI?
	{
		//if (LAPIC[activeCPU].EOIregister == 0) //Properly written 0?
		{
			if (LAPIC[activeCPU].ISR[0]|LAPIC[activeCPU].ISR[1]|LAPIC[activeCPU].ISR[2]|LAPIC[activeCPU].ISR[3]|LAPIC[activeCPU].ISR[4]|LAPIC[activeCPU].ISR[5]|LAPIC[activeCPU].ISR[6]|LAPIC[activeCPU].ISR[7]) //Anything set to acnowledge?
			{
				MSBleft = 256; //How many are left!
				for (MSb = 255; MSBleft; --MSBleft) //Check all possible interrupts!
				{
					if (LAPIC[activeCPU].ISR[MSb >> 5] & (1 << (MSb&0x1F))) //Highest IRQ found (MSb)?
					{
						LAPIC[activeCPU].ISR[MSb >> 5] &= ~(1 << (MSb & 0x1F)); //Clear said ISR!
						LAPIC_updatedISR(activeCPU); //Update the ISR!
						LAPIC_updatedIRRISR(activeCPU); //Update the ISR!
						if ((LAPIC[activeCPU].SpuriousInterruptVectorRegister & (1 << 12)) == 0) //Not suppressed broadcast EOI?
						{
							if (LAPIC[activeCPU].TMR[MSb >> 5] & (1 << (MSb & 0x1F))) //Send an EOI to the IO APIC when the TMR is set!
							{
								LAPIC_broadcastEOI(activeCPU, MSb); //Broadcast the EOI!
							}
						}
						goto finishupEOI; //Only acnlowledge the MSb IRQ!
					}
					--MSb;
				}
			}
		}
	}

	finishupEOI: //Finish up an EOI comand: continue onwards!
	if (LAPIC[activeCPU].needstermination & 4) //Needs termination due to sending a command?
	{
		LAPIC[activeCPU].InterruptCommandRegisterLo |= DELIVERYPENDING; //Start to become pending!
		LAPIC[activeCPU].InterruptCommandRegisterPendingIOAPIC = ~0; //Any possible pending!
		LAPIC[activeCPU].InterruptCommandRegisterPendingReceiver = ~0; //Any possible pending!
		LAPIC[activeCPU].InterruptCommandRegisterReceiversDetermined = 0; //Receivers not determined yet!
	}

	if (LAPIC[activeCPU].needstermination & 8) //Error status register needs termination?
	{
		LAPIC[activeCPU].ErrorStatusRegister = 0; //Clear the status register for new errors to be reported!
		LAPIC_handleunpendingerror(activeCPU); //Handle pending!
		//Also rearm the error reporting?
	}

	if (LAPIC[activeCPU].needstermination & 0x10) //Initial count register is written?
	{
		if (LAPIC[activeCPU].InitialCountRegister == 0) //Stop the timer?
		{
			LAPIC[activeCPU].CurrentCountRegister = 0; //Stop the timer!
			LAPIC[activeCPU].CurrentCountRegisterTicking = 0; //Don't tick?
		}
		else //Timer started?
		{
			LAPIC[activeCPU].CurrentCountRegister = LAPIC[activeCPU].InitialCountRegister; //Load the current count and start timing!
			LAPIC[activeCPU].CurrentCountRegisterTicking = 1; //Start to tick?
		}
	}

	if (LAPIC[activeCPU].needstermination & 0x20) //Divide configuationregister is written?
	{
		updateLAPICTimerSpeed(activeCPU); //Update the timer speed!
	}

	if (LAPIC[activeCPU].needstermination & 0x40) //Error Status Interrupt LVT is written?
	{
		//Don't trigger any error when this error status LVT register is written!
	}

	if (LAPIC[activeCPU].needstermination & 0x80) //TPR needs termination?
	{
		LAPIC_updatedISR(activeCPU); //Update the values depending on it!
		LAPIC_updatedIRRISR(activeCPU); //Update the values depending on it!
	}

	if (LAPIC[activeCPU].needstermination & 0x100) //Needs termination?
	{
		LAPIC[activeCPU].LVTTimerRegisterDirty = 0; //Ready for use!
		LAPIC[activeCPU].LVTLINT0RegisterDirty = 0; //Ready for use!
		LAPIC[activeCPU].LVTLINT1RegisterDirty = 0; //Ready for use!
		LAPIC[activeCPU].LVTErrorRegisterDirty = 0; //Ready for use!
	}

	if (LAPIC[activeCPU].needstermination & 0x400) //Timer needs to stop when the Timer LVT is written?
	{
		//LAPIC[activeCPU].CurrentCountRegisterTicking = 0; //Stop the counter from ticking!
	}

	//0x200 is timer count latched!

	LAPIC[activeCPU].needstermination = 0; //No termination is needed anymore!
	LAPIC[activeCPU].LAPIC_globalrequirestermination = 0; //No termination is needed anymore!
}

void IOAPIC_handletermination() //Handle termination on the APIC!
{
	//Handle any writes to APIC addresses!
	if (likely((IOAPIC.IOAPIC_globalrequirestermination) == 0)) return; //No termination needed?

	if (IOAPIC.IOAPIC_globalrequirestermination & 0x8) //IO APIC needs termination on entry writes?
	{
		memset(&IOAPIC.IOAPIC_requirestermination, 0, sizeof(IOAPIC.IOAPIC_requirestermination)); //Not requiring termination anymore!
	}

	IOAPIC.needstermination = 0; //No termination is needed anymore!
	IOAPIC.IOAPIC_globalrequirestermination = 0; //No termination is needed anymore!
}

void APIC_handletermination()
{
	LAPIC_handletermination(); //Handle termination of the local APIC!
	IOAPIC_handletermination(); //Handle termination of the IO APIC!
}

OPTINLINE byte getint(byte PIC, byte IR) //Get interrupt!
{
	if (__HW_DISABLED) return 0; //Abort!
	byte realir = IR; //Default: nothing changed!
	return ((i8259.icw[PIC][1] & 0xF8) | (realir & 0x7)); //Get interrupt!
}

byte isLAPIClogicaldestination(byte whichCPU, byte logicaldestination)
{
	byte ourid;
	byte idtomatch;
	switch ((LAPIC[whichCPU].DestinationFormatRegister >> 28) & 0xF) //What destination mode?
	{
	case 0: //Cluster model?
		//high 4 bits are encoded address of destination cluster
		//low 4 bits are the 4 APICs within the cluster.
		//the matching is done like with flat model, but on both the destination cluster and APIC number!
		if (logicaldestination == 0xFF) return 1; //Broadcast?
		ourid = (((LAPIC[whichCPU].LogicalDestinationRegister >> 24) & logicaldestination)&0xF); //Simply logical AND of the APICs within the cluster!
		idtomatch = (LAPIC[whichCPU].LogicalDestinationRegister >> 24); //ID to match!
		return ((ourid != 0) && ((idtomatch & 0xF0) == (logicaldestination & 0xF0))); //Received?
		break;
	case 0xF: //Flat model?
		ourid = ((LAPIC[whichCPU].LogicalDestinationRegister >> 24) & logicaldestination); //Simply logical AND on both the destination cluster and selected APIC!
		return (ourid!=0); //Received on the single APIC?
		break;
	default: //Unknown model?
		break;
	}
	return 0; //Default: not the selected destination!
}

//isLAPICorIOAPIC=0: LAPIC, 1=APIC! result: 0=No match. 1=Local APIC, 2=IO APIC.
byte isAPICPhysicaldestination(byte whichCPU, byte isLAPICorIOAPIC, byte physicaldestination)
{
	switch (isLAPICorIOAPIC) //Which chip is addressed?
	{
	case 0: //LAPIC?
		if (physicaldestination == 0xF) //Broadcast?
		{
			return 1; //Match!
		}
		else if (physicaldestination == ((LAPIC[whichCPU].LAPIC_ID >> 24) & 0xF)) //Match?
		{
			return 1;
		}
		else //No match!
		{
			return 0; //Not matched!
		}
		break;
	case 1: //IO APIC?
		if (physicaldestination == 0xF) //Broadcast?
		{
			return 2; //Match!
		}
		else if (physicaldestination == ((IOAPIC.IOAPIC_ID >> 24) & 0xF)) //Match?
		{
			return 2; //Match!
		}
		else //No match!
		{
			return 0; //Not matched!
		}
		break;
	default: //Unknown?
		break;
	}
	return 0; //No match!
}

byte i8259_INTA(byte whichCPU, byte fromAPIC); //Prototype for the vector execution of the LAPIC for ExtINT modes!

//Execute a requested vector on the Local APIC! Specify IR=0xFF for no actual IR! Result: 1=Accepted, 0=Not accepted!
byte LAPIC_executeVector(byte whichCPU, uint_32* vectorlo, byte IR, byte isIOAPIC)
{
	byte resultadd;
	byte backupactiveCPU;
	byte APIC_intnr;
	APIC_intnr = (*vectorlo & 0xFF); //What interrupt number?
	resultadd = 0; //Nothing to add!
	switch ((*vectorlo >> 8) & 7) //What destination mode?
	{
	case 0: //Interrupt?
	case 1: //Lowest priority?
		if (LAPIC[whichCPU].enabled != 1) return 0; //Don't accept if disabled!
	//Now, we have selected the highest priority IR! Start using it!
		if (APIC_intnr < 0x10) //Invalid?
		{
			LAPIC_reportErrorStatus(whichCPU,(1 << 6),0); //Report an illegal vector being received!
			return 1; //Abort and Accepted!
		}
		if (LAPIC[whichCPU].IRR[APIC_intnr >> 5] & (1 << (APIC_intnr & 0x1F))) //Already pending?
		{
			return 0; //Can't accept: we're already pending!
		}
		//Accept it!
		LAPIC[whichCPU].IRR[APIC_intnr >> 5] |= (1 << (APIC_intnr & 0x1F)); //Mark the interrupt requested to fire!
		if (*vectorlo & 0x8000) //Level triggered?
		{
			if (isIOAPIC) //IO APIC?
			{
				*vectorlo |= REMOTEPENDING; //The IO or Local APIC has received the request for servicing!
			}
			LAPIC[whichCPU].TMR[APIC_intnr >> 5] |= (1 << (APIC_intnr & 0x1F)); //Mark the interrupt requested to fire!
		}
		else //Edge triggered?
		{
			LAPIC[whichCPU].TMR[APIC_intnr >> 5] &= ~(1 << (APIC_intnr & 0x1F)); //Mark the interrupt requested to fire!
		}
		LAPIC_updatedIRRISR(whichCPU); //Updated the IIR!
		//The IO APIC ignores the received message?
		break;
	case 2: //SMI?
		//Not implemented yet!
		//Can't be masked, bypasses IRR/ISR!
		break;
	case 4: //NMI?
		if (APICNMIQueued[whichCPU]) //Already pending?
		{
			return 0; //Don't accept it!
		}
		APICNMIQueued[whichCPU] = 1; //APIC-issued NMI queued!
		//Can't be masked, bypasses IRR/ISR!
		break;
	case 5: //INIT?
		backupactiveCPU = activeCPU; //Backup!
		activeCPU = whichCPU; //Active for reset!
		resetCPU(0x80); //Special reset of the CPU: INIT only!
		activeCPU = backupactiveCPU; //Restore backup!
		break;
	case 7: //extINT?
		if ((isIOAPIC&3)==1) //IOAPIC in Virtual Wire mode? Don't accept it, let the CPU handle this! The only exception being if this is the CPU acnowledging the interrupt(bit 2 is also set)!
		{
			return 0; //Don't accept the INTA request, because the CPU needs to handle this!
		}
		if (LAPIC[whichCPU].enabled != 1) return 0; //Don't accept if disabled!
		if (LAPIC[whichCPU].LAPIC_extIntPending != -1) return 0; //Don't accept if it's already pending!
		APIC_intnr = (sword)i8259_INTA(whichCPU, 1); //Perform an INTA-style interrupt retrieval!
		//Execute immediately!
		LAPIC[whichCPU].LAPIC_extIntPending = (sword)APIC_intnr; //We're pending now!
		resultadd |= 2; //INTA processed!
		break;
	default: //Unsupported yet?
		break;
	}

	*vectorlo &= ~DELIVERYPENDING; //The IO or Local APIC has received the request!
	recheckLiveIRRs = 1; //Recheck!
	return (1|resultadd); //Accepted!
}

void updateAPIC(uint_64 clockspassed, DOUBLE timepassed)
{
	if (LAPIC[activeCPU].enabled != 1) return; //APIC not enabled?
	if (!clockspassed) return; //Nothing passed?
	//First, divide up!
	LAPIC[activeCPU].LAPIC_timerremainder += clockspassed; //How much more is passed!
	if (LAPIC[activeCPU].LAPIC_timerremainder >> LAPIC[activeCPU].LAPIC_timerdivider) //Something passed?
	{
		clockspassed = (LAPIC[activeCPU].LAPIC_timerremainder >> LAPIC[activeCPU].LAPIC_timerdivider); //How much passed!
		LAPIC[activeCPU].LAPIC_timerremainder -= clockspassed << LAPIC[activeCPU].LAPIC_timerdivider; //How much time is left!
	}
	else
	{
		return; //Nothing is ticked! So, abort!
	}
	//Now, the clocks

	if (LAPIC[activeCPU].CurrentCountRegisterTicking == 0) //Not ticking?
	{
		return; //Don't tick!
	}

	if ((LAPIC[activeCPU].CurrentCountRegister > clockspassed)) //Still timing more than what's needed?
	{
		LAPIC[activeCPU].CurrentCountRegister -= (uint_32)clockspassed; //Time some clocks!
	}
	else //Finished counting?
	{
		if (LAPIC[activeCPU].InitialCountRegister) //Gotten an initial count to wrap arround?
		{
			clockspassed -= LAPIC[activeCPU].CurrentCountRegister; //Time until 0!

			for (; (clockspassed >= LAPIC[activeCPU].InitialCountRegister);) //Multiple blocks?
			{
				clockspassed -= LAPIC[activeCPU].InitialCountRegister; //What is the remaining time?
			}
		}
		else //Wrapping around 0? Simply underflow the counter and let it run onwards!
		{
			LAPIC[activeCPU].CurrentCountRegister -= (uint_32)clockspassed; //Simply underflow the counter!
		}
		LAPIC[activeCPU].CurrentCountRegister = (uint_32)clockspassed; //How many clocks are left!
		if (!(LAPIC[activeCPU].LVTTimerRegister & 0x20000)) //One-shot mode?
		{
			LAPIC[activeCPU].CurrentCountRegister = 0; //Stop(ped) counting!
			LAPIC[activeCPU].CurrentCountRegisterTicking = 0; //Stop ticking the counter now!
		}
		else if (LAPIC[activeCPU].CurrentCountRegister == 0) //Needs to load a new value, otherwise already set! Otherwise, still counting on!
		{
			LAPIC[activeCPU].CurrentCountRegister = LAPIC[activeCPU].InitialCountRegister; //Reload the initial count!
		}

		if (LAPIC[activeCPU].LVTTimerRegisterDirty == 0) //Ready to parse?
		{
			if ((LAPIC[activeCPU].LVTTimerRegister & 0x10000)==0) //Not masked?
			{
				if ((LAPIC[activeCPU].LVTTimerRegister & DELIVERYPENDING) == 0) //The IO or Local APIC can receive the request!
				{
					LAPIC[activeCPU].LVTTimerRegister |= DELIVERYPENDING; //Start pending!
				}
			}
		}
	}
}

byte APIC_errorTrigger(byte whichCPU) //Error has been triggered!
{
	if ((LAPIC[whichCPU].LVTErrorRegister & 0x10000)==0) //Not masked?
	{
		if ((LAPIC[whichCPU].LVTErrorRegister & DELIVERYPENDING) == 0) //The IO or Local APIC can receive the request!
		{
			LAPIC[whichCPU].LVTErrorRegister |= DELIVERYPENDING; //Start pending!
			return 1; //Pending!
		}
	}
	return 0; //Masked off or already pending!
}

void updateAPICliveIRRs(); //Update the live IRRs as needed!

byte receiveCommandRegister(byte whichCPU, uint_32 destinationCPU, uint_32 *commandregister, byte isIOAPIC)
{
	uint_32 *whatregister;
	byte backupactiveCPU;
	uint_32 address;
	switch ((*commandregister >> 8) & 7) //What is requested?
	{
	case 0: //Interrupt raise?
	case 1: //Lowest priority?
		if (isIOAPIC) return 1; //Not on IO APIC!
		if (LAPIC[destinationCPU].enabled != 1) return 0; //Don't accept if disabled!
		if ((*commandregister & 0xFF) < 0x10) //Invalid vector?
		{
			LAPIC_reportErrorStatus(destinationCPU, (1 << 5),1); //Report an illegal vector being sent!
		}
		else if ((LAPIC[destinationCPU].IRR[(*commandregister & 0xFF) >> 5] & (1 << ((*commandregister & 0xFF) & 0x1F))) == 0) //Ready to receive?
		{
			LAPIC[destinationCPU].IRR[(*commandregister & 0xFF) >> 5] |= (1 << ((*commandregister & 0xFF) & 0x1F)); //Raise the interrupt on the Local APIC!
			if (*commandregister & 0x8000) //Level triggered?
			{
				LAPIC[destinationCPU].TMR[(*commandregister & 0xFF) >> 5] |= (1 << ((*commandregister & 0xFF) & 0x1F)); //Mark the interrupt requested to fire!
			}
			else //Edge triggered?
			{
				LAPIC[destinationCPU].TMR[(*commandregister & 0xFF) >> 5] &= ~(1 << ((*commandregister & 0xFF) & 0x1F)); //Mark the interrupt requested to fire!
			}
			LAPIC_updatedIRRISR(destinationCPU); //Updated the IRR!
		}
		//Otherwise, busy? Execute retry status?
		else
		{
			//According to Bochs: accept anyways?
		}
		break;
	case 2: //SMI raised?
		if (isIOAPIC) return 1; //Not on IO APIC!
		break;
	case 3: //Remote Read?
		if (!isIOAPIC) //Not valid on IO APIC!
		{
			address = ((*commandregister & 0xFF)<<4); //The APIC address being addressed(multiple of 16, being addressed divided by 16, e.g. address 020h=02h)
			whatregister = NULL; //Default: unmapped!
			switch (address) //What is addressed?
			{
			case 0x0020:
				whatregister = &LAPIC[destinationCPU].LAPIC_ID; //0020
				break;
			case 0x0030:
				whatregister = &LAPIC[destinationCPU].LAPIC_version; //0030
				break;
			case 0x0080:
				whatregister = &LAPIC[destinationCPU].TaskPriorityRegister; //0080
				break;
			case 0x0090:
				whatregister = &LAPIC[destinationCPU].ArbitrationPriorityRegister; //0090
				break;
			case 0x00A0:
				whatregister = &LAPIC[destinationCPU].ProcessorPriorityRegister; //00A0
				break;
			case 0x00B0:
				whatregister = &LAPIC[destinationCPU].EOIregister; //00B0
				break;
			case 0x00C0:
				whatregister = &LAPIC[destinationCPU].RemoteReadRegister; //00C0
				break;
			case 0x00D0:
				whatregister = &LAPIC[destinationCPU].LogicalDestinationRegister; //00D0
				break;
			case 0x00E0:
				whatregister = &LAPIC[destinationCPU].DestinationFormatRegister; //00E0
				break;
			case 0x00F0:
				whatregister = &LAPIC[destinationCPU].SpuriousInterruptVectorRegister; //00F0
				break;
			case 0x0100:
			case 0x0110:
			case 0x0120:
			case 0x0130:
			case 0x0140:
			case 0x0150:
			case 0x0160:
			case 0x0170:
				whatregister = &LAPIC[destinationCPU].ISR[((address - 0x100) >> 4)]; //ISRs! 0100-0170
				break;
			case 0x0180:
			case 0x0190:
			case 0x01A0:
			case 0x01B0:
			case 0x01C0:
			case 0x01D0:
			case 0x01E0:
			case 0x01F0:
				whatregister = &LAPIC[destinationCPU].TMR[((address - 0x180) >> 4)]; //TMRs! 0180-01F0
				break;
			case 0x0200:
			case 0x0210:
			case 0x0220:
			case 0x0230:
			case 0x0240:
			case 0x0250:
			case 0x0260:
			case 0x0270:
				whatregister = &LAPIC[destinationCPU].IRR[((address - 0x200) >> 4)]; //ISRs! 0200-0270
				break;
			case 0x280:
				whatregister = &LAPIC[destinationCPU].ErrorStatusRegister; //0280
				break;
			case 0x2F0:
				whatregister = &LAPIC[destinationCPU].LVTCorrectedMachineCheckInterruptRegister; //02F0
				break;
			case 0x300:
				whatregister = &LAPIC[destinationCPU].InterruptCommandRegisterLo; //0300
				break;
			case 0x310:
				whatregister = &LAPIC[destinationCPU].InterruptCommandRegisterHi; //0310
				break;
			case 0x320:
				whatregister = &LAPIC[destinationCPU].LVTTimerRegister; //0320
				break;
			case 0x330:
				whatregister = &LAPIC[destinationCPU].LVTThermalSensorRegister; //0330
				break;
			case 0x340:
				whatregister = &LAPIC[destinationCPU].LVTPerformanceMonitoringCounterRegister; //0340
				break;
			case 0x350:
				whatregister = &LAPIC[destinationCPU].LVTLINT0Register; //0350
				break;
			case 0x360:
				whatregister = &LAPIC[destinationCPU].LVTLINT1Register; //0560
				break;
			case 0x370:
				whatregister = &LAPIC[destinationCPU].LVTErrorRegister; //0370
				break;
			case 0x380:
				whatregister = &LAPIC[destinationCPU].InitialCountRegister; //0380
				break;
			case 0x390:
				whatregister = &LAPIC[destinationCPU].CurrentCountRegister; //0390
				break;
			case 0x3E0:
				whatregister = &LAPIC[destinationCPU].DivideConfigurationRegister; //03E0
				break;
			default: //Unmapped?
				whatregister = NULL; //Unmapped!
				break;
			}
			if (whatregister) //Mapped register?
			{
				LAPIC[whichCPU].RemoteReadRegister = *whatregister; //Set the remote read register accordingly?
				LAPIC[whichCPU].InterruptCommandRegisterLo |= 0x20000; //Remote Read valid!
			}
			//Invalid register: leave the result being invalid or not! Leave the last loaded value in place! If any hardware succeeds, it's overwriting the result with a valid value and correct data!
		}
		else //IO APIC?
		{
			return 1; //Accept and ignore! Invalid result!
		}
		break;
	case 4: //NMI raised?
		if (isIOAPIC) return 1; //Not on IO APIC!
		if (APICNMIQueued[destinationCPU]) //Already queued?
		{
			return 0; //Don't accept it yet!
		}
		else //Accepted?
		{
			APICNMIQueued[destinationCPU] = 1; //Queue the APIC NMI!
		}
		break;
	case 5: //INIT or INIT deassert?
		if (((*commandregister >> 14) & 3) == 2) //De-assert?
		{
			//Setup Arbitration ID registers on all APICs!
			//Operation on Pentium and P6: Arbitration ID register = APIC ID register.
			if (isIOAPIC) //IO APIC!
				updateIOAPICArbitrationIDregister(); //Update the register!
			else //Local APIC?
				updateLAPICArbitrationIDregister(destinationCPU); //Update the register!
		}
		else
		{
			if (isIOAPIC) return 1; //Not on IO APIC!
			else //INIT to a CPU?
			{
				backupactiveCPU = activeCPU; //Backup!
				activeCPU = destinationCPU; //Active for reset!
				resetCPU(0x80); //Special reset of the CPU: INIT only!
				activeCPU = backupactiveCPU; //Restore backup!
			}
		}
		break;
	case 6: //SIPI?
		if (isIOAPIC) return 1; //Not on IO APIC!
		if ((*commandregister & 0xFF) < 0x10) //Invalid vector?
		{
			LAPIC_reportErrorStatus(destinationCPU, (1 << 5),1); //Report an illegal vector being sent!
		}
		else //Valid vector!
		{
			if (CPU[destinationCPU].waitingforSIPI && ((CPU[destinationCPU].SIPIreceived & 0x100) == 0)) //Waiting for a SIPI and not received yet?
			{
				CPU[destinationCPU].SIPIreceived = 0x100 | (*commandregister & 0xFF); //We've received a SIPI!
			}
		}
		break;
	default: //Unknown?
		//Don't handle it!
		return 0; //Don't accept it!
		break;
	}
	return 1; //Accept it!
}

uint_32 determineLowestPriority(byte intnr, uint_32 receiver)
{
	byte destinationCPU;
	word lowestPriority;
	uint_32 lowestPriorityCPU;
	lowestPriority = 0x100; //Out of range to always match!
	lowestPriorityCPU = 0; //Which CPU has lowest priority (default if none match)! Default to no CPU matched!
	for (destinationCPU = 0; destinationCPU < MIN(NUMITEMS(LAPIC), numemulatedcpus); ++destinationCPU)
	{
		if ((LAPIC[destinationCPU].SpuriousInterruptVectorRegister & 0x200) == 0) //Focus enabled?
		{
			if (!((LAPIC[activeCPU].IRR[intnr >> 5] & (1 << (intnr & 0x1F))) || (LAPIC[activeCPU].ISR[intnr >> 5] & (1 << (intnr & 0x1F))))) //Not requested yet? Able to accept said message!
			{
				//Ignore the request and deliver at the focus CPU!
				return (1 << destinationCPU); //Deliver at the focused CPU only!
			}
		}
		if (receiver & (1 << destinationCPU)) //To consider this to be the receiver?
		{
			if ((LAPIC[destinationCPU].ArbitrationPriorityRegister & 0xFF) < lowestPriority) //Lower priority found?
			{
				lowestPriority = (LAPIC[destinationCPU].ArbitrationPriorityRegister & 0xFF); //New lowest priority found!
				lowestPriorityCPU = (1 << destinationCPU); //Lowest CPU found to match!
			}
		}
	}
	return lowestPriorityCPU; //Give the CPU with the lowest priority match!
}

//Updates local APIC requests!
void LAPIC_pollRequests(byte whichCPU)
{
	uint_32 IOAPIC_receiver; //Up to 32 receiving IO APICs!
	uint_32 receiver; //Up to 32 receiving CPUs!
	byte destinationCPU; //What CPU is the destination?
	byte logicaldestination;

	if (LAPIC[whichCPU].enabled == 1) //Enabled?
	{
		if (NMIQueued && (LAPIC[whichCPU].LVTLINT1RegisterDirty == 0)) //NMI has been queued?
		{
			if ((LAPIC[whichCPU].LVTLINT1Register & DELIVERYPENDING) == 0) //Not waiting to be delivered!
			{
				if ((LAPIC[whichCPU].LVTLINT1Register & 0x10000) == 0) //Not masked?
				{
					NMIQueued = 0; //Not queued anymore!
					LAPIC[whichCPU].LVTLINT1Register |= DELIVERYPENDING; //Start pending!
					//Edge: raised when set(done here already). Lowered has weird effects for level-sensitive modes? So ignore them!
				}
			}
		}
	}

	if (LAPIC[whichCPU].LAPIC_extIntPending != -1) return; //Prevent any more interrupts until the extInt is properly parsed!

	receiver = IOAPIC_receiver = 0; //Initialize receivers of the packet!
	if (LAPIC[whichCPU].InterruptCommandRegisterLo & DELIVERYPENDING) //Pending command being sent?
	{
		if (LAPIC[whichCPU].InterruptCommandRegisterPendingReceiver == (uint_32)~0) //Starting up a new command that's starting to process?
		{
			if (((LAPIC[whichCPU].InterruptCommandRegisterLo >> 8) & 7) == 3) //Remote read is to be executed?
			{
				LAPIC[whichCPU].InterruptCommandRegisterLo &= ~0x20000; //Default: Remote Read invalid!
			}
		}

		if (LAPIC[whichCPU].InterruptCommandRegisterReceiversDetermined) //Already determined receivers?
		{
			goto receiveTheCommandRegister; //Handle the receiving of the command register!
		}

		switch ((LAPIC[whichCPU].InterruptCommandRegisterLo >> 18) & 3) //What destination type?
		{
		case 0: //Destination field?
			if (LAPIC[whichCPU].InterruptCommandRegisterLo & 0x800) //Logical destination?
			{
				logicaldestination = ((LAPIC[whichCPU].InterruptCommandRegisterHi >> 24) & 0xFF); //What is the logical destination?
				for (destinationCPU = 0; destinationCPU < MIN(NUMITEMS(LAPIC),numemulatedcpus); ++destinationCPU) //Check all destinations!
				{
					if (isLAPIClogicaldestination(destinationCPU, logicaldestination)) //Match on the logical destination?
					{
						receiver |= (1<<destinationCPU); //Received on LAPIC!
					}
				}
			}
			else //Physical destination?
			{
				for (destinationCPU = 0; destinationCPU < MIN(NUMITEMS(LAPIC),numemulatedcpus); ++destinationCPU)
				{
					if (isAPICPhysicaldestination(destinationCPU, 0, ((LAPIC[whichCPU].InterruptCommandRegisterHi >> 24) & 0xF)) == 1) //Local APIC?
					{
						receiver |= (1<<destinationCPU); //Receive it on LAPIC!
					}
				}
				if (isAPICPhysicaldestination(0, 1, ((LAPIC[whichCPU].InterruptCommandRegisterHi >> 24) & 0xF)) == 2) //IO APIC?
				{
					IOAPIC_receiver |= 1; //Receive it on LAPIC!
				}
			}
			if (receiver|IOAPIC_receiver) //Received on some Local APICs?
			{
				goto receiveTheCommandRegister; //Receive it!
			}
			else if ((receiver|IOAPIC_receiver) == 0) //No receivers?
			{
				LAPIC_reportErrorStatus(whichCPU,(1 << 2),1); //Report an send accept error! Nothing responded on the bus!
			}
			//Discard it!
			LAPIC[whichCPU].InterruptCommandRegisterLo &= ~DELIVERYPENDING; //We're receiving it somewhere!
			break;
		case 1: //To itself?
			receiver = (1<<whichCPU); //Self received!
			goto receiveTheCommandRegister; //Receive it!
			break;
		case 2: //All processors?
			//Receive it!
			//Handle the request!
			receiver = (1<<(MIN(NUMITEMS(LAPIC),numemulatedcpus)))-1; //All received!
			IOAPIC_receiver = 1; //IO APIC too!
		receiveTheCommandRegister:
			if (LAPIC[whichCPU].InterruptCommandRegisterReceiversDetermined == 0) //Not determined yet?
			{
				if (LAPIC[whichCPU].InterruptCommandRegisterLo & 0x800) //Logical destination?
				{
					if ((LAPIC[whichCPU].InterruptCommandRegisterLo & 0x700) == 0x100) //Lowest Priority type?
					{
						receiver = determineLowestPriority(LAPIC[whichCPU].InterruptCommandRegisterLo & 0xFF, receiver); //Determine the lowest priority receiver!
					}
				}
				LAPIC[whichCPU].InterruptCommandRegisterReceivers = receiver; //Who is to receive!
				LAPIC[whichCPU].InterruptCommandRegisterReceiversDetermined = 1; //Determined!
			}
			else
			{
				receiver = LAPIC[whichCPU].InterruptCommandRegisterReceivers; //Who is to receive!
			}
			LAPIC[whichCPU].InterruptCommandRegisterLo &= ~DELIVERYPENDING; //We're receiving it somewhere!
			if (receiver) //Received on a LAPIC?
			{
				for (destinationCPU = 0; destinationCPU < MIN(NUMITEMS(LAPIC),numemulatedcpus); ++destinationCPU) //Try all CPUs!
				{
					if (receiver & LAPIC[whichCPU].InterruptCommandRegisterPendingReceiver & (1 << destinationCPU)) //To receive and not received here yet?
					{
						if (receiveCommandRegister(whichCPU, destinationCPU, &LAPIC[whichCPU].InterruptCommandRegisterLo,0)) //Accepted?
						{
							receiver &= ~(1 << destinationCPU); //Received!
							LAPIC[whichCPU].InterruptCommandRegisterPendingReceiver &= ~(1 << destinationCPU); //Received!
						}
					}
				}
				if (IOAPIC_receiver) //IO APIC too?
				{
					if (LAPIC[whichCPU].InterruptCommandRegisterPendingIOAPIC) //Still pending?
					{
						if (receiveCommandRegister(whichCPU, 0, &LAPIC[whichCPU].InterruptCommandRegisterLo, 1)) //Accepted?
						{
							IOAPIC_receiver = LAPIC[whichCPU].InterruptCommandRegisterPendingIOAPIC = 0; //Received!
						}
					}
				}
				if ((receiver & LAPIC[whichCPU].InterruptCommandRegisterPendingReceiver) || (IOAPIC_receiver & LAPIC[whichCPU].InterruptCommandRegisterPendingIOAPIC)) //Still pending to receive somewhere?
				{
					LAPIC[whichCPU].InterruptCommandRegisterLo |= DELIVERYPENDING; //We're still receiving it somewhere!
				}
				else if (receiver) //Failed to send all when transaction completed?
				{
					LAPIC_reportErrorStatus(whichCPU, (1 << 2),1); //Report an send accept error! Not all responded on the bus!
					LAPIC[whichCPU].InterruptCommandRegisterReceiversDetermined = 0; //Receivers not determined yet!
				}
				else //Finished?
				{
					LAPIC[whichCPU].InterruptCommandRegisterReceiversDetermined = 0; //Receivers not determined yet!
				}
			}
			else //No receivers?
			{
				LAPIC_reportErrorStatus(whichCPU, (1 << 2),1); //Report an send accept error! Nothing responded on the bus!
			}
			break;
		case 3: //All but ourselves?
			receiver = (1 << (MIN(NUMITEMS(LAPIC),numemulatedcpus))) - 1; //All received!
			receiver &= ~(1 << whichCPU); //But ourselves!
			IOAPIC_receiver = 1; //IO APIC too!
			//Don't handle the request!
			LAPIC[whichCPU].InterruptCommandRegisterLo &= ~DELIVERYPENDING; //We're receiving it somewhere!
			//Send no error because there are no other APICs to receive it! Only the IO APIC receives it, which isn't using it?
			//Error out the write access!
			goto receiveTheCommandRegister; //Receive it!
			break;
		}
	}

	if ((LAPIC[whichCPU].LVTErrorRegister & DELIVERYPENDING) && ((LAPIC[activeCPU].LVTErrorRegister & 0x10000) == 0) && (LAPIC[whichCPU].LVTErrorRegisterDirty == 0)) //Error is pending?
	{
		lastLAPICAccepted[whichCPU] = LAPIC_executeVector(whichCPU, &LAPIC[whichCPU].LVTErrorRegister, 0xFF, 0); //Start the error interrupt!
	}
	if ((LAPIC[whichCPU].LVTTimerRegister & DELIVERYPENDING) && ((LAPIC[activeCPU].LVTTimerRegister & 0x10000) == 0) && (LAPIC[whichCPU].LVTTimerRegisterDirty == 0)) //Timer is pending?
	{
		lastLAPICAccepted[whichCPU] = LAPIC_executeVector(whichCPU, &LAPIC[whichCPU].LVTTimerRegister, 0xFF, 0); //Start the timer interrupt!
	}
	if ((LAPIC[whichCPU].LVTLINT0Register & DELIVERYPENDING) && ((LAPIC[activeCPU].LVTLINT0Register & 0x10000) == 0) && (LAPIC[whichCPU].LVTLINT0RegisterDirty == 0)) //LINT0 is pending?
	{
		if ((LAPIC[whichCPU].LVTLINT0Register & 0x700) != 0x700) //Not direct PIC mode?
		{
			lastLAPICAccepted[whichCPU] = LAPIC_executeVector(whichCPU, &LAPIC[whichCPU].LVTLINT0Register, 0xFF, 0); //Start the LINT0 interrupt!
		}
	}
	if ((LAPIC[whichCPU].LVTLINT1Register & DELIVERYPENDING) && ((LAPIC[activeCPU].LVTLINT1Register & 0x10000) == 0) && (LAPIC[whichCPU].LVTLINT1RegisterDirty == 0)) //LINT1 is pending?
	{
		lastLAPICAccepted[whichCPU] = LAPIC_executeVector(whichCPU, &LAPIC[whichCPU].LVTLINT1Register, 0xFF, 0); //Start the LINT0 interrupt!
	}
}

byte handleIOLAPIC_receiveCommandRegister(byte enableExtInt, byte extIntCPU, byte IR, uint_32 receiver, uint_32 APIC_IRQsrequested, uint_32 APIC_requestbit)
{
	byte result, result2;
	result = result2 = 0; //Default result to add!
	byte destinationCPU; //What CPU is the destination?
	if (IOAPIC.IOAPIC_redirectionentryReceiversDetermined[IR] == 0) //Not determined yet?
	{
		if (IOAPIC.IOAPIC_redirectionentry[IR][0] & 0x800) //Logical destination?
		{
			if ((IOAPIC.IOAPIC_redirectionentry[IR][0] & 0x700) == 0x100) //Lowest Priority type?
			{
				receiver = determineLowestPriority(IOAPIC.IOAPIC_redirectionentry[IR][0] & 0xFF, receiver); //Determine the lowest priority receiver!
			}
		}
		IOAPIC.IOAPIC_redirectionentryReceivers[IR] = receiver; //What receives it!
		IOAPIC.IOAPIC_redirectionentryReceiversDetermined[IR] = 1; //Determined!
	}
	else
	{
		receiver = IOAPIC.IOAPIC_redirectionentryReceivers[IR]; //What receives it!
	}
	if (receiver) //Local APIC received?
	{
		for (destinationCPU = 0; destinationCPU < MIN(NUMITEMS(LAPIC), numemulatedcpus); ++destinationCPU)
		{
			if (receiver & (1 << destinationCPU)) //To receive?
			{
				if ((IOAPIC.IOAPIC_redirectionentry[IR][0] & 0x700) == 0x700) //ExtINT type?
				{
					if ((destinationCPU != extIntCPU) || (!enableExtInt)) //ExtInt can't be delivered right now to this (active) CPU?
					{
						continue; //Can't receive the ExtInt on this CPU!
					}
				}
				if ((result2 = LAPIC_executeVector(destinationCPU, &IOAPIC.IOAPIC_redirectionentry[IR][0], IR, 1 + (enableExtInt << 1))) != 0) //Execute this vector from IO APIC!
				{
					IOAPIC.IOAPIC_redirectionentryReceivers[IR] &= ~(1 << destinationCPU); //Clear the single receiver!
					receiver = IOAPIC.IOAPIC_redirectionentryReceivers[IR]; //New receiver for this IR!
					result |= (result2 & 2); //INTA received?
					//Properly received! Clear the sources!
					if ((IOAPIC.IOAPIC_redirectionentryReceivers[IR] == 0) || (result&2)) //Finished all receicvers or INTA?
					{
						APIC_IRQsrequested &= ~APIC_requestbit; //Clear the request bit!
						IOAPIC.IOAPIC_IRRset &= ~APIC_requestbit; //Clear the request, because we're firing it up now!
						IOAPIC.IOAPIC_redirectionentryReceiversDetermined[IR] = 0; //Not determined anymore!
					}
					else //Not finished yet?
					{
						IOAPIC.IOAPIC_redirectionentry[IR][0] |= DELIVERYPENDING; //The IO or Local APIC hasn't finished receiving the requests!
					}
				}
			}
		}
		//Otherwise, not accepted, keep polling this IR!
	}
	else //No receivers? Finished!
	{
		APIC_IRQsrequested &= ~APIC_requestbit; //Clear the request bit!
		IOAPIC.IOAPIC_IRRset &= ~APIC_requestbit; //Clear the request, because we're firing it up now!
		LAPIC_reportErrorStatus(0, (1 << 3), 0); //Report an receive accept error!
		IOAPIC.IOAPIC_redirectionentryReceiversDetermined[IR] = 0; //Not determined anymore!
	}
	return result; //Give the result!
}

byte IOAPIC_pollRequests(byte enableExtInt, byte extIntCPU)
{
	byte result;
	uint_32 receiver; //Up to 32 receiving CPUs!
	byte destinationCPU; //What CPU is the destination?

	byte logicaldestination;
	byte IR;
	byte APIC_intnr;
	int APIC_highestpriority; //-1=Nothing yet, otherwise, highest priority level detected
	byte APIC_highestpriorityIR; //Highest priority IR detected!
	uint_32 APIC_IRQsrequested, APIC_requestbit, APIC_requestsleft, APIC_requestbithighestpriority;
	APIC_IRQsrequested = IOAPIC.IOAPIC_IRRset & (~IOAPIC.IOAPIC_IMRset); //What can we handle!

	result = 0; //Default the result!

	updateAPICliveIRRs(); //Update the live IRRs!

	if (LAPIC[activeCPU].LAPIC_extIntPending != -1) return 0; //Prevent any more interrupts until the extInt is properly parsed!

	if (likely(APIC_IRQsrequested == 0)) return 0; //Nothing to do?
//First, determine the highest priority IR to use!
	APIC_requestbit = 1; //What bit is requested first!
	APIC_requestsleft = 24; //How many are left!
	APIC_requestbithighestpriority = 0; //Default: no highest priority found yet!
	APIC_highestpriority = -1; //Default: no highest priority level found yet!
	APIC_highestpriorityIR = 0; //Default: No highest priority IR loaded yet!
	//Note: this way of handling the priority is done by the LAPIC as well(high nibble of the interrupt vector determines the priority)!
	for (IR = 0; APIC_requestsleft; ++IR) //Check all requests!
	{
		if (APIC_IRQsrequested & APIC_requestbit) //Are we requested to fire?
		{
			//Priority is based on the high nibble of the interrupt vector. The low nibble is ignored!
			if ((int)(IOAPIC.IOAPIC_redirectionentry[IR][0] & 0xF0U) >= APIC_highestpriority) //Higher priority found?
			{
				if (IOAPIC.IOAPIC_requirestermination[IR] == 0) //Skip entries that are marked dirty(still processing there)!
				{
					//Determinate the interrupt number for the priority!
					APIC_intnr = (IOAPIC.IOAPIC_redirectionentry[IR][0] & 0xFF); //What interrupt number?
					switch ((IOAPIC.IOAPIC_redirectionentry[IR][0] >> 8) & 7) //What destination mode?
					{
					case 0: //Interrupt?
					case 1: //Lowest priority?
						if ((LAPIC[activeCPU].IRR[APIC_intnr >> 5] & (1 << (APIC_intnr & 0x1F))) == 0) //Not requested yet? Able to accept said message!
						{
							APIC_highestpriority = (int)(IOAPIC.IOAPIC_redirectionentry[IR][0] & 0xF0U); //New highest priority!
							APIC_highestpriorityIR = IR; //What IR has the highest priority now!
							APIC_requestbithighestpriority = APIC_requestbit; //What bit was the highest priority?
						}
						break;
					case 2: //SMI?
					case 4: //NMI?
					case 5: //INIT or INIT deassert?
						APIC_highestpriority = (int)(IOAPIC.IOAPIC_redirectionentry[IR][0] & 0xF0U); //New highest priority!
						APIC_highestpriorityIR = IR; //What IR has the highest priority now!
						APIC_requestbithighestpriority = APIC_requestbit; //What bit was the highest priority?
						break;
					case 7: //extINT?
						if (enableExtInt) //Allowed to acnowledge ExtINT type packets?
						{
							APIC_highestpriority = (int)(IOAPIC.IOAPIC_redirectionentry[IR][0] & 0xF0U); //New highest priority!
							APIC_highestpriorityIR = IR; //What IR has the highest priority now!
							APIC_requestbithighestpriority = APIC_requestbit; //What bit was the highest priority?
							goto handleExtIntPriority; //Top priority!
						}
						break;
					}
				}
			}
		}
		APIC_requestbit <<= 1; //Next bit to check!
		--APIC_requestsleft; //One processed!
	}
	handleExtIntPriority:
	if (APIC_requestbithighestpriority) //Found anything to handle?
	{
		//First, determine what to receive!
		APIC_requestbit = APIC_requestbithighestpriority; //Highest priority IR bit
		IR = APIC_highestpriorityIR; //The IR for the highest priority!
		//Now, receive the IO APIC entry at the destination!

		receiver = 0; //Default: no receivers!
		//Only support receiving these packets on the Local APICs! Not on the IO APIC!
		if (IOAPIC.IOAPIC_redirectionentry[IR][0] & 0x800) //Logical destination?
		{
			logicaldestination = ((IOAPIC.IOAPIC_redirectionentry[IR][1] >> 24) & 0xFF); //What is the logical destination?
			//Determine destination correct by destination format and logical destination register in the LAPIC!
			for (destinationCPU = 0; destinationCPU < MIN(NUMITEMS(LAPIC),numemulatedcpus); ++destinationCPU)
			{
				if (isLAPIClogicaldestination(destinationCPU, logicaldestination)) //Match on the logical destination?
				{
					receiver |= (1<<destinationCPU); //LAPIC!
				}
			}
			if (receiver==0) //No receivers?
			{
				LAPIC_reportErrorStatus(0,(1 << 3),0); //Report an receive accept error!
			}
			else //Able to receive?
			{
				goto receiveIOLAPICCommandRegister; //Receive it!
			}
		}
		else //Physical destination?
		{
			logicaldestination = ((IOAPIC.IOAPIC_redirectionentry[IR][1] >> 24) & 0xF); //What destination!
			for (destinationCPU = 0; destinationCPU < MIN(NUMITEMS(LAPIC),numemulatedcpus); ++destinationCPU)
			{
				if (isAPICPhysicaldestination(destinationCPU, 0, logicaldestination) == 1) //Local APIC?
				{
					receiver |= (1 << destinationCPU);
				}
			}
			if (receiver == 0) //No receivers?
			{
				LAPIC_reportErrorStatus(0, (1 << 3),0); //Report an receive accept error! Where to report this?
			}
			else
			{
				goto receiveIOLAPICCommandRegister; //Receive it!
			}
		}
		return 0; //Abort: invalid destination!
	receiveIOLAPICCommandRegister:
		//Received something from the IO APIC redirection targetting the main CPU?
		result = handleIOLAPIC_receiveCommandRegister(enableExtInt, extIntCPU, IR, receiver, APIC_IRQsrequested, APIC_requestbit); //Handle this receiver!
	}
	return result; //Give the result!
}

//Acnowledge an INTA style interrupt from the local APIC!
sword LAPIC_acnowledgeRequests(byte whichCPU)
{
	byte IRgroup;
	byte IRgroupsleft;
	byte IR;
	byte APIC_intnr;
	uint_32 APIC_IRQsrequested[8], APIC_requestbit, APIC_requestsleft;
	APIC_IRQsrequested[0] = LAPIC[whichCPU].IRR[0] & (~LAPIC[whichCPU].ISR[0]); //What can we handle!
	APIC_IRQsrequested[1] = LAPIC[whichCPU].IRR[1] & (~LAPIC[whichCPU].ISR[1]); //What can we handle!
	APIC_IRQsrequested[2] = LAPIC[whichCPU].IRR[2] & (~LAPIC[whichCPU].ISR[2]); //What can we handle!
	APIC_IRQsrequested[3] = LAPIC[whichCPU].IRR[3] & (~LAPIC[whichCPU].ISR[3]); //What can we handle!
	APIC_IRQsrequested[4] = LAPIC[whichCPU].IRR[4] & (~LAPIC[whichCPU].ISR[4]); //What can we handle!
	APIC_IRQsrequested[5] = LAPIC[whichCPU].IRR[5] & (~LAPIC[whichCPU].ISR[5]); //What can we handle!
	APIC_IRQsrequested[6] = LAPIC[whichCPU].IRR[6] & (~LAPIC[whichCPU].ISR[6]); //What can we handle!
	APIC_IRQsrequested[7] = LAPIC[whichCPU].IRR[7] & (~LAPIC[whichCPU].ISR[7]); //What can we handle!
	if (!(APIC_IRQsrequested[0] | APIC_IRQsrequested[1] | APIC_IRQsrequested[2] | APIC_IRQsrequested[3] | APIC_IRQsrequested[4] | APIC_IRQsrequested[5] | APIC_IRQsrequested[6] | APIC_IRQsrequested[7]))
	{
		return -1; //Nothing to do!
	}
	//Find the most prioritized interrupt to fire!
	IRgroupsleft = 8;
	for (IRgroup = 7;IRgroupsleft; --IRgroup) //Process all possible groups to handle!
	{
		if (APIC_IRQsrequested[IRgroup]) //Something requested here?
		{
			//First, determine the highest priority IR to use!
			APIC_requestbit = (1U << 31); //What bit is requested first!
			APIC_requestsleft = 32; //How many are left!
			//Note: this way of handling the priority is done by the LAPIC as well(high nibble of the interrupt vector determines the priority)!
			for (IR = 31; APIC_requestsleft; --IR) //Check all requests!
			{
				if (APIC_IRQsrequested[IRgroup] & APIC_requestbit) //Are we requested to fire?
				{
					APIC_intnr = (IRgroup << 5) | IR; //The priority to fire!
					//Priority filtered? Then only fire if higher priority class than the processor priority class!
					if ((LAPIC[whichCPU].ProcessorPriorityRegister & 0xF0U) && ((APIC_intnr&0xF0U) <= (LAPIC[whichCPU].ProcessorPriorityRegister & 0xF0U)))
					{
						goto skipPriorityIRR; //Skip this group!
					}
					//Priority is based on the high nibble of the interrupt vector. The low nibble is ignored!
					goto firePrioritizedIR; //handle it!
				}
				skipPriorityIRR: //Skipping it because of priority!
				APIC_requestbit >>= 1; //Next bit to check!
				--APIC_requestsleft; //One processed!
			}
		}
		--IRgroupsleft; //One group processed!
	}
	//Nothing found to fire due to priority?
	return -1; //Nothing to do!

firePrioritizedIR: //Fire the IR that has the most priority!
//Now, we have selected the highest priority IR! Start using it!
	APIC_intnr = (IRgroup << 5) | IR; //The interrupt to fire!
	LAPIC[whichCPU].IRR[IRgroup] &= ~APIC_requestbit; //Mark the interrupt in-service!
	LAPIC[whichCPU].ISR[IRgroup] |= APIC_requestbit; //Mark the interrupt in-service!
	LAPIC_updatedISR(whichCPU); //Updated the ISR!
	LAPIC_updatedIRRISR(whichCPU); //Updated the IRR and ISR!
	return (sword)APIC_intnr; //Give the interrupt number to fire!
}

extern uint_32 i440fx_ioapic_base_mask;
extern uint_32 i440fx_ioapic_base_match;

extern byte memory_datawrittensize; //How many bytes have been written to memory during a write!
extern uint_64 BIU_cachedmemoryaddr[MAXCPUS][2];
extern byte BIU_cachedmemorysize[MAXCPUS][2];
extern byte memory_datasize[2]; //The size of the data that has been read!
byte APIC_memIO_wb(uint_32 offset, byte value)
{
	byte is_internalexternalAPIC;
	uint_32 storedvalue, ROMbits, address;
	uint_32* whatregister; //What register is addressed?
	byte updateredirection;
	updateredirection = 0; //Init!

	is_internalexternalAPIC = 0; //Default: no APIC chip!
	if (((offset & 0xFFFFFF000ULL) == LAPIC[activeCPU].baseaddr)) //LAPIC?
	{
		is_internalexternalAPIC |= 1; //LAPIC!
	}
	if ((((offset & 0xFFFFFF000ULL) == IOAPIC.IObaseaddr))) //IO APIC?
	{
		is_internalexternalAPIC |= 2; //IO APIC!
	}
	else if (is_internalexternalAPIC==0) //Neither?
	{
		return 0; //Neither!
	}

	address = (offset & 0xFFC); //What address is addressed?

	ROMbits = ~0; //All bits are ROM bits by default?

	if (((offset&i440fx_ioapic_base_mask)==i440fx_ioapic_base_match) && (is_internalexternalAPIC&2)) //I/O APIC?
	{
		if (IOAPIC.enabled == 0) return 0; //Not the APIC memory space enabled?
		switch (address&0x10) //What is addressed?
		{
		case 0x0000: //IOAPIC address?
			whatregister = &IOAPIC.APIC_address; //Address register!
			ROMbits = 0; //Upper 24 bits are reserved!
			break;
		case 0x0010: //IOAPIC data?
			switch (IOAPIC.APIC_address&0xFF) //What address is selected (8-bits)?
			{
			case 0x00:
				whatregister = &IOAPIC.IOAPIC_ID; //ID register!
				ROMbits = ~(0xFU<<24); //Bits 24-27 writable!
				break;
			case 0x01:
				whatregister = &IOAPIC.IOAPIC_version_numredirectionentries; //Version/Number of direction entries!
				break;
			case 0x02:
				whatregister = &IOAPIC.IOAPIC_arbitrationpriority; //Arbitration priority register!
				break;
			case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F:
			case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27: case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F:
			case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37: case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D: case 0x3E: case 0x3F:
				whatregister = &IOAPIC.IOAPIC_redirectionentry[(IOAPIC.APIC_address - 0x10) >> 1][(IOAPIC.APIC_address - 0x10) & 1]; //Redirection entry addressed!
				if (((IOAPIC.APIC_address - 0x10) & 1) != 0) //High dword?
				{
					ROMbits = 0; //Fully writable?
				}
				else //Low DWord?
				{
					ROMbits = (1U << 12) | (1U << 14); //Fully writable, except bits 12, 14 and 17-55(writable?)!
				}
				IOAPIC.IOAPIC_globalrequirestermination |= 0x8; //Needs termination to finish below's value!
				IOAPIC.IOAPIC_requirestermination[(IOAPIC.APIC_address - 0x10) >> 1] = 1; //Dirtied and currently unusable!
				updateredirection = (((IOAPIC.APIC_address - 0x10) & 1) == 0); //Update status when the first dword is updated!
				break;
			default: //Unmapped?
				if (is_internalexternalAPIC & 1) //LAPIC?
				{
					goto notIOAPICW;
				}
				else
				{
					return 0; //Unmapped!
				}
				break;
			}
			break;
		default: //Unmapped?
			if (is_internalexternalAPIC & 1) //LAPIC?
			{
				goto notIOAPICW;
			}
			else
			{
				return 0; //Unmapped!
			}
			break;
		}
	}
	else if (is_internalexternalAPIC&1) //LAPIC?
	{
	notIOAPICW:
		if (LAPIC[activeCPU].enabled == -1) return 0; //Not the APIC memory space enabled?
		switch (address) //What is addressed?
		{
		case 0x0020:
			whatregister = &LAPIC[activeCPU].LAPIC_ID; //0020
			ROMbits = 0; //Fully writable!
			break;
		case 0x0030:
			whatregister = &LAPIC[activeCPU].LAPIC_version; //0030
			break;
		case 0x0080:
			whatregister = &LAPIC[activeCPU].TaskPriorityRegister; //0080
			LAPIC[activeCPU].needstermination |= 0x80; //Task priority has been updated!
			ROMbits = 0; //Fully writable!
			break;
		case 0x0090:
			whatregister = &LAPIC[activeCPU].ArbitrationPriorityRegister; //0090
			break;
		case 0x00A0:
			whatregister = &LAPIC[activeCPU].ProcessorPriorityRegister; //00A0
			break;
		case 0x00B0:
			whatregister = &LAPIC[activeCPU].EOIregister; //00B0
			ROMbits = 0; //Fully writable!
			//Only writable with value 0! Otherwise, #GP(0) is encountered!
			break;
		case 0x00C0:
			whatregister = &LAPIC[activeCPU].RemoteReadRegister; //00C0
			break;
		case 0x00D0:
			whatregister = &LAPIC[activeCPU].LogicalDestinationRegister; //00D0
			ROMbits = 0; //Fully writable!
			break;
		case 0x00E0:
			whatregister = &LAPIC[activeCPU].DestinationFormatRegister; //00E0
			ROMbits = 0; //Fully writable!
			break;
		case 0x00F0:
			whatregister = &LAPIC[activeCPU].SpuriousInterruptVectorRegister; //00F0
			ROMbits = 0xF; //Fully writable! P6: Bits 0-3 are hardwared to logical ones!
			break;
		case 0x0100:
		case 0x0110:
		case 0x0120:
		case 0x0130:
		case 0x0140:
		case 0x0150:
		case 0x0160:
		case 0x0170:
			whatregister = &LAPIC[activeCPU].ISR[((address - 0x100) >> 4)]; //ISRs! 0100-0170
			break;
		case 0x0180:
		case 0x0190:
		case 0x01A0:
		case 0x01B0:
		case 0x01C0:
		case 0x01D0:
		case 0x01E0:
		case 0x01F0:
			whatregister = &LAPIC[activeCPU].TMR[((address - 0x180) >> 4)]; //TMRs! 0180-01F0
			break;
		case 0x0200:
		case 0x0210:
		case 0x0220:
		case 0x0230:
		case 0x0240:
		case 0x0250:
		case 0x0260:
		case 0x0270:
			whatregister = &LAPIC[activeCPU].IRR[((address - 0x200) >> 4)]; //ISRs! 0200-0270
			break;
		case 0x280:
			whatregister = &LAPIC[activeCPU].ErrorStatusRegister; //0280
			break;
		case 0x2F0:
			whatregister = &LAPIC[activeCPU].LVTCorrectedMachineCheckInterruptRegister; //02F0
			ROMbits = DELIVERYPENDING; //Fully writable!
			if (LAPIC[activeCPU].enabled == 0) //Soft disabled?
			{
				ROMbits |= (1 << 16); //The mask is ROM!
			}
			break;
		case 0x300:
			whatregister = &LAPIC[activeCPU].InterruptCommandRegisterLo; //0300
			ROMbits = DELIVERYPENDING|(1<<17); //Fully writable! Pending to send isn't writable! Remote read status isn't writable!
			break;
		case 0x310:
			whatregister = &LAPIC[activeCPU].InterruptCommandRegisterHi; //0310
			ROMbits = 0; //Fully writable!
			break;
		case 0x320:
			whatregister = &LAPIC[activeCPU].LVTTimerRegister; //0320
			ROMbits = DELIVERYPENDING; //Fully writable!
			LAPIC[activeCPU].LVTTimerRegisterDirty = 1; //Dirty!
			LAPIC[activeCPU].needstermination |= 0x100; //Needs termination!
			LAPIC[activeCPU].needstermination |= 0x400; //Needs termination!
			if (LAPIC[activeCPU].enabled == 0) //Soft disabled?
			{
				ROMbits |= (1 << 16); //The mask is ROM!
			}
			break;
		case 0x330:
			whatregister = &LAPIC[activeCPU].LVTThermalSensorRegister; //0330
			ROMbits = DELIVERYPENDING; //Fully writable!
			if (LAPIC[activeCPU].enabled == 0) //Soft disabled?
			{
				ROMbits |= (1 << 16); //The mask is ROM!
			}
			break;
		case 0x340:
			whatregister = &LAPIC[activeCPU].LVTPerformanceMonitoringCounterRegister; //0340
			ROMbits = DELIVERYPENDING; //Fully writable!
			if (LAPIC[activeCPU].enabled == 0) //Soft disabled?
			{
				ROMbits |= (1 << 16); //The mask is ROM!
			}
			break;
		case 0x350:
			whatregister = &LAPIC[activeCPU].LVTLINT0Register; //0350
			ROMbits = DELIVERYPENDING; //Fully writable!
			LAPIC[activeCPU].LVTLINT0RegisterDirty = 1; //Dirty!
			LAPIC[activeCPU].needstermination |= 0x100; //Needs termination!
			if (LAPIC[activeCPU].enabled == 0) //Soft disabled?
			{
				ROMbits |= (1 << 16); //The mask is ROM!
			}
			break;
		case 0x360:
			whatregister = &LAPIC[activeCPU].LVTLINT1Register; //0560
			ROMbits = DELIVERYPENDING; //Fully writable!
			LAPIC[activeCPU].LVTLINT1RegisterDirty = 1; //Dirty!
			LAPIC[activeCPU].needstermination |= 0x100; //Needs termination!
			if (LAPIC[activeCPU].enabled == 0) //Soft disabled?
			{
				ROMbits |= (1 << 16); //The mask is ROM!
			}
			break;
		case 0x370:
			whatregister = &LAPIC[activeCPU].LVTErrorRegister; //0370
			ROMbits = DELIVERYPENDING; //Fully writable!
			LAPIC[activeCPU].LVTErrorRegisterDirty = 1; //Dirty!
			LAPIC[activeCPU].needstermination |= 0x100; //Needs termination!
			if (LAPIC[activeCPU].enabled == 0) //Soft disabled?
			{
				ROMbits |= (1 << 16); //The mask is ROM!
			}
			break;
		case 0x380:
			whatregister = &LAPIC[activeCPU].InitialCountRegister; //0380
			ROMbits = 0; //Fully writable!
			break;
		case 0x390:
			whatregister = &LAPIC[activeCPU].CurrentCountRegister; //0390
			break;
		case 0x3E0:
			whatregister = &LAPIC[activeCPU].DivideConfigurationRegister; //03E0
			ROMbits = 0; //Fully writable!
			break;
		default: //Unmapped?
			LAPIC_reportErrorStatus(activeCPU, (1 << 7),0); //Illegal address error!
			return 0; //Unmapped!
			break;
		}
	}
	else
		return 0; //Abort!

	//Get stored value!
	storedvalue = *whatregister; //What value is read at said address?

	//Create the value with adjusted data for storing it back!
	storedvalue = (storedvalue & ((~(0xFF << ((offset & 3) << 3))) | ROMbits)) | ((value<<((offset&3)<<3)) & ((0xFF << ((offset & 3) << 3)) & ~ROMbits)); //Stored value without the ROM bits!

	if (is_internalexternalAPIC & 1) //LAPIC?
	{
		if (address == 0xF0) //Needs to handle resetting the APIC?
		{
			if ((LAPIC[activeCPU].needstermination & 1) == 0) //Not backed up yet?
			{
				LAPIC[activeCPU].prevSpuriousInterruptVectorRegister = LAPIC[activeCPU].SpuriousInterruptVectorRegister; //Backup the old value for change detection!
				LAPIC[activeCPU].needstermination |= 1; //We're in need of termination handling due to possible reset!
			}
		}
	}

	//Store the value back to the register!
	*whatregister = storedvalue; //Store the new value inside the register, if allowed to be changed!

	if (is_internalexternalAPIC & 1) //LAPIC?
	{
		if (address == 0xB0) //Needs to handle EOI?
		{
			LAPIC[activeCPU].needstermination |= 2; //Handle an EOI?
		}
		else if (address == 0x300) //Needs to send a command?
		{
			LAPIC[activeCPU].InterruptCommandRegisterLo &= ~DELIVERYPENDING; //Not sent yet is kept cleared!
			LAPIC[activeCPU].needstermination |= 4; //Handle a command?
		}
		else if (address == 0x280) //Error status register?
		{
			LAPIC[activeCPU].needstermination |= 8; //Error status register is written!
		}
		else if (address == 0x380) //Initial count register?
		{
			LAPIC[activeCPU].needstermination |= 0x10; //Initial count register is written!
		}
		else if (address == 0x3E0) //Divide configuration register?
		{
			LAPIC[activeCPU].needstermination |= 0x20; //Divide configuration register is written!
		}
		else if (address == 0x370) //Error register?
		{
			LAPIC[activeCPU].needstermination |= 0x40; //Error register is written!
		}
	}

	if (updateredirection) //Update redirection?
	{
		if (IOAPIC.IOAPIC_redirectionentry[(IOAPIC.APIC_address - 0x10) >> 1][0] & 0x10000) //Mask set?
		{
			IOAPIC.IOAPIC_IMRset |= (1 << ((IOAPIC.APIC_address - 0x10) >> 1)); //Set the mask!
		}
		else //Mask cleared?
		{
			IOAPIC.IOAPIC_IMRset &= ~(1 << ((IOAPIC.APIC_address - 0x10) >> 1)); //Clear the mask!
		}
	}

	if (unlikely(isoverlappingw((uint_64)offset, 1, (uint_64)BIU_cachedmemoryaddr[0][0], BIU_cachedmemorysize[0][0]))) //Cached?
	{
		memory_datasize[0] = 0; //Invalidate the read cache to re-read memory!
		BIU_cachedmemorysize[0][0] = 0; //Invalidate the BIU cache as well!
	}
	if (unlikely(isoverlappingw((uint_64)offset, 1, (uint_64)BIU_cachedmemoryaddr[1][0], BIU_cachedmemorysize[1][0]))) //Cached?
	{
		memory_datasize[0] = 0; //Invalidate the read cache to re-read memory!
		BIU_cachedmemorysize[1][0] = 0; //Invalidate the BIU cache as well!
	}
	if (unlikely(isoverlappingw((uint_64)offset, 1, (uint_64)BIU_cachedmemoryaddr[0][1], BIU_cachedmemorysize[0][1]))) //Cached?
	{
		memory_datasize[1] = 0; //Invalidate the read cache to re-read memory!
		BIU_cachedmemorysize[0][1] = 0; //Invalidate the BIU cache as well!
	}
	if (unlikely(isoverlappingw((uint_64)offset, 1, (uint_64)BIU_cachedmemoryaddr[1][1], BIU_cachedmemorysize[1][1]))) //Cached?
	{
		memory_datasize[1] = 0; //Invalidate the read cache to re-read memory!
		BIU_cachedmemorysize[1][1] = 0; //Invalidate the BIU cache as well!
	}

	memory_datawrittensize = 1; //Only 1 byte written!
	return 1; //Data has been written!
}

extern uint_64 memory_dataread[2];
extern byte memory_datasize[2]; //The size of the data that has been read!
byte APIC_memIO_rb(uint_32 offset, byte index)
{
	byte uncachableaddr;
	byte is_internalexternalAPIC;
	uint_32 temp, tempoffset, address;
	union
	{
		uint_32 value32;
		word value16;
	} converter16;
	uint_32* whatregister; //What register is accessed?
	tempoffset = offset; //Backup!

	uncachableaddr = 0; //Default: cachable address!

	is_internalexternalAPIC = 0; //Default: no APIC chip!
	if (((offset & 0xFFFFFF000ULL) == LAPIC[activeCPU].baseaddr)) //LAPIC?
	{
		is_internalexternalAPIC |= 1; //LAPIC!
	}
	if ((((offset & 0xFFFFFF000ULL) == IOAPIC.IObaseaddr))) //IO APIC?
	{
		is_internalexternalAPIC |= 2; //IO APIC!
	}
	else if (is_internalexternalAPIC == 0) //Neither?
	{
		return 0; //Neither!
	}

	address = (offset & 0xFFC); //What address is addressed?

	if (((offset&i440fx_ioapic_base_mask)==i440fx_ioapic_base_match) && (is_internalexternalAPIC & 2)) //I/O APIC?
	{
		if (IOAPIC.enabled == 0) return 0; //Not the APIC memory space enabled?
		switch (address&0x10) //What is addressed?
		{
		case 0x0000: //IOAPIC address?
			whatregister = &IOAPIC.APIC_address; //Address register!
			break;
		case 0x0010: //IOAPIC data?
			switch (IOAPIC.APIC_address&0xFF) //What address is selected (8-bits)?
			{
			case 0x00:
				whatregister = &IOAPIC.IOAPIC_ID; //ID register!
				break;
			case 0x01:
				whatregister = &IOAPIC.IOAPIC_version_numredirectionentries; //Version/Number of direction entries!
				break;
			case 0x02:
				whatregister = &IOAPIC.IOAPIC_arbitrationpriority; //Arbitration priority register!
				break;
			case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F:
			case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27: case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F:
			case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37: case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D: case 0x3E: case 0x3F:
				whatregister = &IOAPIC.IOAPIC_redirectionentry[(IOAPIC.APIC_address - 0x10) >> 1][(IOAPIC.APIC_address - 0x10) & 1]; //Redirection entry addressed!
				uncachableaddr = 1; //Uncachable!
				break;
			default: //Unmapped?
				if (is_internalexternalAPIC & 1) //LAPIC?
				{
					goto notIOAPICR;
				}
				else
				{
					return 0; //Unmapped!
				}
				break;
			}
			break;
		default: //Unmapped?
			if (is_internalexternalAPIC & 1) //LAPIC?
			{
				goto notIOAPICR;
			}
			else
			{
				return 0; //Unmapped!
			}
			break;
		}
	}
	else if (is_internalexternalAPIC & 1) //LAPIC?
	{
		notIOAPICR:
		if (LAPIC[activeCPU].enabled == -1) return 0; //Not the APIC memory space enabled?
		switch (address) //What is addressed?
		{
		case 0x0020:
			whatregister = &LAPIC[activeCPU].LAPIC_ID; //0020
			break;
		case 0x0030:
			whatregister = &LAPIC[activeCPU].LAPIC_version; //0030
			break;
		case 0x0080:
			whatregister = &LAPIC[activeCPU].TaskPriorityRegister; //0080
			break;
		case 0x0090:
			whatregister = &LAPIC[activeCPU].ArbitrationPriorityRegister; //0090
			break;
		case 0x00A0:
			whatregister = &LAPIC[activeCPU].ProcessorPriorityRegister; //00A0
			uncachableaddr = 1; //Uncachable!
			break;
		case 0x00B0:
			whatregister = &LAPIC[activeCPU].EOIregister; //00B0
			break;
		case 0x00C0:
			whatregister = &LAPIC[activeCPU].RemoteReadRegister; //00C0
			uncachableaddr = 1; //Uncachable!
			break;
		case 0x00D0:
			whatregister = &LAPIC[activeCPU].LogicalDestinationRegister; //00D0
			break;
		case 0x00E0:
			whatregister = &LAPIC[activeCPU].DestinationFormatRegister; //00E0
			break;
		case 0x00F0:
			whatregister = &LAPIC[activeCPU].SpuriousInterruptVectorRegister; //00F0
			break;
		case 0x0100:
		case 0x0110:
		case 0x0120:
		case 0x0130:
		case 0x0140:
		case 0x0150:
		case 0x0160:
		case 0x0170:
			whatregister = &LAPIC[activeCPU].ISR[((address - 0x100) >> 4)]; //ISRs! 0100-0170
			uncachableaddr = 1; //Uncachable!
			break;
		case 0x0180:
		case 0x0190:
		case 0x01A0:
		case 0x01B0:
		case 0x01C0:
		case 0x01D0:
		case 0x01E0:
		case 0x01F0:
			whatregister = &LAPIC[activeCPU].TMR[((address - 0x180) >> 4)]; //TMRs! 0180-01F0
			uncachableaddr = 1; //Uncachable!
			break;
		case 0x0200:
		case 0x0210:
		case 0x0220:
		case 0x0230:
		case 0x0240:
		case 0x0250:
		case 0x0260:
		case 0x0270:
			whatregister = &LAPIC[activeCPU].IRR[((address - 0x200) >> 4)]; //ISRs! 0200-0270
			uncachableaddr = 1; //Uncachable!
			break;
		case 0x280:
			whatregister = &LAPIC[activeCPU].ErrorStatusRegister; //0280
			uncachableaddr = 1; //Uncachable!
			break;
		case 0x2F0:
			whatregister = &LAPIC[activeCPU].LVTCorrectedMachineCheckInterruptRegister; //02F0
			uncachableaddr = 1; //Uncachable!
			break;
		case 0x300:
			whatregister = &LAPIC[activeCPU].InterruptCommandRegisterLo; //0300
			uncachableaddr = 1; //Uncachable!
			break;
		case 0x310:
			whatregister = &LAPIC[activeCPU].InterruptCommandRegisterHi; //0310
			break;
		case 0x320:
			whatregister = &LAPIC[activeCPU].LVTTimerRegister; //0320
			uncachableaddr = 1; //Uncachable!
			break;
		case 0x330:
			whatregister = &LAPIC[activeCPU].LVTThermalSensorRegister; //0330
			uncachableaddr = 1; //Uncachable!
			break;
		case 0x340:
			whatregister = &LAPIC[activeCPU].LVTPerformanceMonitoringCounterRegister; //0340
			uncachableaddr = 1; //Uncachable!
			break;
		case 0x350:
			whatregister = &LAPIC[activeCPU].LVTLINT0Register; //0350
			uncachableaddr = 1; //Uncachable!
			break;
		case 0x360:
			whatregister = &LAPIC[activeCPU].LVTLINT1Register; //0560
			uncachableaddr = 1; //Uncachable!
			break;
		case 0x370:
			whatregister = &LAPIC[activeCPU].LVTErrorRegister; //0370
			uncachableaddr = 1; //Uncachable!
			break;
		case 0x380:
			whatregister = &LAPIC[activeCPU].InitialCountRegister; //0380
			break;
		case 0x390:
			whatregister = &LAPIC[activeCPU].CurrentCountRegister; //0390
			if ((LAPIC[activeCPU].needstermination&0x200)==0) //Not latched yet?
			{
				LAPIC[activeCPU].CurrentCountRegisterlatched = LAPIC[activeCPU].CurrentCountRegister; //Latch it!
				LAPIC[activeCPU].needstermination |= 0x200; //Error register is written!
			}
			uncachableaddr = 1; //Uncachable!
			break;
		case 0x3E0:
			whatregister = &LAPIC[activeCPU].DivideConfigurationRegister; //03E0
			break;
		default: //Unmapped?
			LAPIC_reportErrorStatus(activeCPU, (1 << 7),0); //Illegal address error!
			return 0; //Unmapped!
			break;
		}
	}
	else
		return 0; //Abort!

	converter16.value32 = *whatregister; //Take the register's value that's there!
	if (whatregister == &LAPIC[activeCPU].CurrentCountRegister) //Latched?
	{
		converter16.value32 = LAPIC[activeCPU].CurrentCountRegisterlatched; //Latch read!
	}

	tempoffset = (offset & 3); //What DWord byte is addressed?
	temp = tempoffset;
	#ifdef USE_MEMORY_CACHING
	if (((index & 3) == 0) && (uncachableaddr==0)) //Cachable address?
	{
		temp &= 3; //Single DWord read only!
		tempoffset &= 3; //Single DWord read only!
		temp = tempoffset; //Backup address!
		tempoffset &= ~3; //Round down to the dword address!
		if (likely(((tempoffset | 3) < 0x1000))) //Enough to read a dword?
		{
			memory_dataread[0] = SDL_SwapLE32(*((uint_32*)(&converter16.value32))); //Read the data from the result!
			memory_datasize[(index >> 5) & 1] = tempoffset = 4 - (temp - tempoffset); //What is read from the whole dword!
			memory_dataread[0] >>= ((4 - tempoffset) << 3); //Discard the bytes that are not to be read(before the requested address)!
			return 1; //Done: we've been read!
		}
		else
		{
			tempoffset = temp; //Restore the original address!
			tempoffset &= ~1; //Round down to the word address!
			if (likely(((tempoffset | 1) < 0x1000))) //Enough to read a word, aligned?
			{
				converter16.value32 >>= ((offset&2)<<3); //Take the lower or upper word correctly to read!
				memory_dataread[0] = SDL_SwapLE16(*((word*)(&converter16.value16))); //Read the data from the result!
				memory_datasize[(index >> 5) & 1] = tempoffset = 2 - (temp - tempoffset); //What is read from the whole word!
				memory_dataread[0] >>= ((2 - tempoffset) << 3); //Discard the bytes that are not to be read(before the requested address)!
				return 1; //Done: we've been read!
			}
			else //Enough to read a byte only?
			{
				memory_dataread[0] = converter16.value32>>((tempoffset&3)<<3); //Read the data from the result!
				memory_datasize[(index >> 5) & 1] = 1; //Only 1 byte!
				return 1; //Done: we've been read!
			}
		}
	}
	else //Enough to read a byte only?
	#endif
	{
		memory_dataread[0] = converter16.value32>>((tempoffset&3)<<3); //Read the data from the ROM, reversed!
		memory_datasize[(index >> 5) & 1] = 1; //Only 1 byte!
		return 1; //Done: we've been read!				
	}
	return 0; //Not implemented yet!
}

void APIC_updateWindowMSR(byte whichCPU, uint_32 lo, uint_32 hi)
{
	//Update the window MSR!
	LAPIC[whichCPU].windowMSRhi = hi; //High value of the MSR!
	LAPIC[whichCPU].windowMSRlo = lo; //Low value of the MSR!
	LAPIC[whichCPU].baseaddr = (uint_64)(LAPIC[whichCPU].windowMSRlo & 0xFFFFF000); //Base address for the APIC!
	if (EMULATED_CPU >= CPU_PENTIUMPRO) //4 more pins for the Pentium Pro!
	{
		LAPIC[whichCPU].baseaddr |= (((uint_64)(LAPIC[whichCPU].windowMSRhi & 0xF)) << 32); //Extra bits from the high MSR on Pentium II and up!
	}
	LAPIC[whichCPU].enabled = ((LAPIC[whichCPU].windowMSRlo & 0x800) >> 11)?((LAPIC[whichCPU].SpuriousInterruptVectorRegister & 0x100)>>8):-1; //APIC space enabled? Leave soft mode alone(leave it as the register is set) or set to fully disabled!
}

byte readPollingMode(byte pic); //Prototype!

byte in8259(word portnum, byte *result)
{
	if (__HW_DISABLED) return 0; //Abort!
	if (portnum == 0x22)
	{
		*result = addr22; //Read addr22
		return 1;
	}
	else if (portnum == 0x23)
	{
		if (addr22 == 0x70) //Selected IMCR?
		{
			*result = IMCR; //Give IMCR!
			return 1;
		}
		else
		{
			*result = 0xFF; //Unknown register!
			return 1;
		}
	}
	byte pic = ((portnum&~1)==0xA0)?1:(((portnum&~1)==0x20)?0:2); //PIC0/1/unknown!
	if (pic == 2) return 0; //Not our PIC!
	switch (portnum & 1)
	{
	case 0:
		if (i8259.pollingmode[pic]) //Polling mode enabled?
		{
			*result = readPollingMode(pic); //Read the polling mode!
			i8259.pollingmode[pic] = 0; //Not anymore!
		}
		else //Normal mode?
		{
			if (i8259.readmode[pic] == 0) *result = i8259.irr[pic];
			else *result = i8259.isr[pic];
		}
		break;
	case 1: //read mask register
		if (i8259.pollingmode[pic]) //Polling mode enabled?
		{
			*result = readPollingMode(pic); //Read the polling mode!
			i8259.pollingmode[pic] = 0; //Not anymore!
		}
		else //Normal mode?
		{
			*result = i8259.imr[pic];
		}
		break;
	default:
		break;
	}
	return 1; //The result is given!
}

OPTINLINE void EOI(byte PIC, byte source) //Process and (Automatic) EOI send to an PIC!
{
	if (__HW_DISABLED) return; //Abort!
	byte i;
	for (i = 0; i < 8; i++)
	{
		if ((i8259.isr[PIC] >> i) & 1)
		{
			i8259.isr[PIC] ^= (1 << i);
			byte IRQ;
			IRQ = (PIC << 3) | i; //The IRQ we've finished!
			byte currentsrc;
			currentsrc = source; //Check the specified source!
			if (i8259.isr2[PIC][currentsrc]&(1<<(IRQ&7))) //We've finished?
			{
				if (i8259.finishirq[IRQ][currentsrc]) //Gotten a handler?
				{
					i8259.finishirq[IRQ][currentsrc](IRQ|(currentsrc<<4)); //We're done with this IRQ!
				}
				i8259.isr2[PIC][currentsrc] ^= (1 << i); //Not in service anymore!
			}
			return;
		}
	}
}

extern byte is_XT; //Are we emulating a XT architecture?

void LINT0_raiseIRQ(byte updatelivestatus); //Prototype!
void LINT0_lowerIRQ(); //Prototype!

byte out8259(word portnum, byte value)
{
	byte source;
	if (__HW_DISABLED) return 0; //Abort!
	if (portnum == 0x22)
	{
		addr22 = value; //Write addr22
		return 1;
	}
	else if (portnum == 0x23)
	{
		if (addr22 == 0x70) //Selected IMCR?
		{
			if (((IMCR&1) == 0) && ((value&1) == 1)) //Disconnect NMI and INTR from the CPU?
			{
				LINT0_lowerIRQ(); //IRQ line is disconnected, so lowered!
			}
			else //Reconnect NMI and INTR to the CPU?
			{
				if (IOAPIC.IOAPIC_currentliveIRR & 1) //Already raised?
				{
					LINT0_raiseIRQ(1); //Raised!
				}
				else //Not raised!
				{
					LINT0_lowerIRQ(); //Lowered!
				}
				//NMI is handled automatically!
			}
			IMCR = value; //Set IMCR!
			return 1;
		}
		else
		{
			return 1; //Unknown register!
		}
	}
	byte pic = ((portnum & ~1) == 0xA0) ? 1 : (((portnum & ~1) == 0x20) ? 0 : 2); //PIC0/1/unknown!
	if (pic == 2) return 0; //Not our PIC!
	switch (portnum & 1)
	{
	case 0:
		if (value & 0x10)   //begin initialization sequence(OCS)
		{
			i8259.icwstep[pic] = 0; //Init ICWStep!
			memset(&i8259.irr[pic],0,sizeof(i8259.irr[pic])); //Reset IRR raised sense!
			memset(&i8259.irr3[pic],0,sizeof(i8259.irr3[pic])); //Reset IRR shared raised sense!
			memset(&i8259.irr3_a[pic],0,sizeof(i8259.irr3_a[pic])); //Reset IRR shared raised sense!
			memset(&i8259.irr3_b[pic], 0, sizeof(i8259.irr3_b[pic])); //Reset IRR shared raised sense!
			irr3_dirty = 0; //Not dirty anymore!
			i8259.imr[pic] = 0; //clear interrupt mask register
			i8259.icw[pic][i8259.icwstep[pic]++] = value; //Set the ICW1!
			i8259.icw[pic][2] = 7; //Slave mode address is set to 7?
			if ((i8259.icw[pic][0] & 1)==0) //ICW4 not sent?
			{
				i8259.icw[pic][3] = 0; //ICW4 has all it's functions set to zero!
			}
			i8259.readmode[pic] = 0; //Default to IRR reading after a reset!
			return 1;
		}
		if ((value & 0x98)==0x08) //it's an OCW3
		{
			i8259.pollingmode[pic] = ((value & 4) >> 2); //Enable polling mode?
			if (value & 2) i8259.readmode[pic] = value & 1; //Read ISR instead of IRR on reads? Only modify this setting when setting this setting(bit 2 is set)!
			return 1;
		}
		if ((value & 0x18) == 0) //it's an OCW2
		{
			//We're a OCW2!
			if ((value&0xE0)!=0x40) //Ignore type! Not a NOP?
			{
				if (value & 0x20) //It's an EOI-type command(non-specific, specific, rotate on non-specific, rotate on specific)?
				{
					for (source = 0; source < 0x10; ++source) //Check all sources!
					{
						EOI(pic, source); //Send an EOI from this source!
					}
				}
			}
		}
		return 1;
		break;
	case 1:
		if (i8259.icwstep[pic]<4) //Not sent all ICW yet?
		{
			i8259.icw[pic][i8259.icwstep[pic]++] = value;
			if ((i8259.icwstep[pic] == 2) && (i8259.icw[pic][0] & 2)) //Next is not ICW3?
			{
				++i8259.icwstep[pic]; //single mode, so don't read ICW3
			}
			if ((i8259.icwstep[pic] == 3) && ((i8259.icw[pic][0] & 1)==0)) //Next is not ICW4?
			{
				++i8259.icwstep[pic]; //no ICW4 expected, so don't read ICW4
			}
			return 1;
		}
		//OCW1!
		//if we get to this point, this is just a new IMR value
		i8259.imr[pic] = value;
		break;
	default:
		break;
	}
	return 1; //We're processed!
}

byte interruptsaved = 0; //Have we gotten a primary interrupt (first PIC)?
byte lastinterrupt = 0; //Last interrupt requested!

byte isSlave(byte PIC)
{
	return PIC; //The first PIC is not a slave, all others are!
}

byte startSlaveMode(byte PIC, byte IR) //For master only! Set slave mode masterIR processing on INTA?
{
	return (i8259.icw[PIC][2]&(1<<IR)) && (!isSlave(PIC)) && ((i8259.icw[PIC][0]&2)==0); //Process slaves and set IR on the slave UD instead of 0 while in cascade mode?
}

byte respondMasterSlave(byte PIC, byte masterIR) //Process this PIC as a slave for a set slave IR?
{
	return (((i8259.icw[PIC][2]&3)==masterIR) && (isSlave(PIC))) || ((masterIR==0xFF) && (!isSlave(PIC)) && (!PIC)); //Process this masterIR as a slave or Master in Master mode connected to INTRQ?
}

OPTINLINE byte getunprocessedinterrupt(byte PIC)
{
	byte result;
	result = i8259.irr[PIC];
	result &= ~i8259.imr[PIC];
	result &= ~i8259.isr[PIC];
	return result; //Give the result!
}

void IOAPIC_raisepending(); //Prototype!
byte dummyIOAPICPollRequests = 0;
void acnowledgeirrs()
{
	byte nonedirty; //None are dirtied?
	byte recheck;
	recheck = 0;

performRecheck:
	if (recheck == 0) //Check?
	{
		IOAPIC_raisepending(); //Raise all pending!
		LAPIC_pollRequests(activeCPU); //Poll the APIC for possible requests!
		dummyIOAPICPollRequests = IOAPIC_pollRequests(0,0); //Poll the APIC for possible requests!
		if (getunprocessedinterrupt(1)) //Slave connected to master?
		{
			raiseirq(0x802); //Slave raises INTRQ!
			i8259.intreqtracking[1] = 1; //Tracking INTREQ!
		}
		else if ((recheck == 0) && i8259.intreqtracking[1]) //Slave has been lowered and needs processing?
		{
			lowerirq(0x802); //Slave lowers INTRQ before being acnowledged!
			i8259.intreqtracking[1] = 0; //Not tracking INTREQ!
		}
		//INTR on the APIC!
		if ((getunprocessedinterrupt(0)!=0)!=i8259.intreqtracking[0]) //INTR raised?
		{
			i8259.intreqtracking[0] = (getunprocessedinterrupt(0) != 0); //Tracking INTREQ!
			if (i8259.intreqtracking[0]) //Raised?
			{
				APIC_raisedIRQ(0, 2); //Raised INTR!
			}
			else //Lowered?
			{
				APIC_loweredIRQ(0, 2); //Lowered INTR!
			}
		}
	}

	if (likely(irr3_dirty == 0)) return; //Nothing to do?
	nonedirty = 1; //None are dirty!
	//Move IRR3 to IRR and acnowledge!
	byte IRQ, source, PIC, IR;
	for (PIC=0;PIC<2;++PIC)
		for (IR=0;IR<8;++IR)
		{
			IRQ = (PIC << 3) | IR; //The IRQ we're accepting!
			if (((i8259.irr[PIC]&(1<<IR))==0) || (is_XT==0)) //Nothing acnowledged yet?
			{
				for (source = 0;source < 0x10;++source) //Verify if anything is left!
				{
					if (((i8259.irr3_a[PIC][source]&(1 << IR))==0) && (i8259.irr3[PIC][source] & (1 << IR))) //Not acnowledged yet and high?
					{
						if (i8259.acceptirq[IRQ][source]) //Gotten a handler?
						{
							i8259.acceptirq[IRQ][source](IRQ|(source<<4)); //We're accepting the IRQ from this source!
						}
						i8259.irr3_a[PIC][source] |= (1 << IR); //Add the IRQ to request because of the rise!
						i8259.irr3_b[PIC][source] |= (1 << IR); //Second line for handling the interrupt itself!
						i8259.irr[PIC] |= (1 << IR); //Add the IRQ to request because of the rise!
						nonedirty = 0; //Acnowledged one!
					}
				}
			}
		}
	if (getunprocessedinterrupt(1) && (recheck==0)) //Slave connected to master?
	{
		raiseirq(0x802); //Slave raises INTRQ!
		i8259.intreqtracking[1] = 1; //Tracking INTREQ!
		recheck = 1; //Check again!
		goto performRecheck; //Check again!
	}

	//INTR on the APIC!
	if ((getunprocessedinterrupt(0)!=0)!=i8259.intreqtracking[0]) //INTR raised?
	{
		i8259.intreqtracking[0] = (getunprocessedinterrupt(0) != 0); //Tracking INTREQ!
		if (i8259.intreqtracking[0]) //Raised?
		{
			APIC_raisedIRQ(0, 2); //Raised INTR!
		}
		else //Lowered?
		{
			APIC_loweredIRQ(0, 2); //Lowered INTR!
		}
	}

	if (nonedirty) //None are dirty anymore?
	{
		irr3_dirty = 0; //Not dirty anymore!
	}
}

sword APIC_currentintnr[MAXCPUS] = { -1,-1 };

byte PICInterrupt() //We have an interrupt ready to process? This is the primary PIC's INTA!
{
	if (__HW_DISABLED) return 0; //Abort!

	if (APIC_currentintnr[activeCPU] != -1) //Interrupt pending to fire?
	{
		return 2; //APIC IRQ is pending to fire!
	}

	if (IOAPIC_pollRequests(1, activeCPU) == 2) //Poll the extInt type requests on the IO APIC! Acnowledged a INTA from the IO APIC?
	{
		goto handleIOAPIC_INTA; //Start to handle the IO APIC INTA request!
	}

	if ((LAPIC[activeCPU].LVTLINT0Register & DELIVERYPENDING) && ((LAPIC[activeCPU].LVTLINT0Register & 0x10000) == 0) && (LAPIC[activeCPU].LVTLINT0RegisterDirty == 0)) //LINT0 is pending?
	{
		if ((LAPIC[activeCPU].LVTLINT0Register & 0x700) == 0x700) //Direct PIC mode?
		{
			lastLAPICAccepted[activeCPU] = LAPIC_executeVector(activeCPU, &LAPIC[activeCPU].LVTLINT0Register, 0xFF, 0); //Start the LINT0 interrupt!
		}
	}

	handleIOAPIC_INTA:
	if (LAPIC[activeCPU].LAPIC_extIntPending != -1) //ExtInt pending?
	{
		APIC_currentintnr[activeCPU] = LAPIC[activeCPU].LAPIC_extIntPending; //Acnowledge!
		LAPIC[activeCPU].LAPIC_extIntPending = -1; //Not anymore!
		return 2; //APIC IRQ from 8259!
	}

	if ((APIC_currentintnr[activeCPU] = LAPIC_acnowledgeRequests(activeCPU))!=-1) //APIC requested?
	{
		return 2; //APIC IRQ!
	}
	if (getunprocessedinterrupt(0) && ((IMCR&1)!=0x01)) //Primary PIC interrupt? This is also affected by the IMCR!
	{
		if (activeCPU) //APIC enabled and taken control of the interrupt pin or not CPU #0? When not BSP, disable INTR (not connected to the other CPUs)!
		{
			return 0; //The connection from the INTR pin to the local APIC is active! Disable the normal interrupts(redirected to the LVT LINT0 register)!
		}
		//i8259 can handle the IRQ!
		return 1;
	}
	//Slave PICs are handled when encountered from the Master PIC!
	return 0; //No interrupt to process!
}

OPTINLINE byte IRRequested(byte PIC, byte IR, byte source) //We have this requested?
{
	if (__HW_DISABLED) return 0; //Abort!
	return (((getunprocessedinterrupt(PIC) & (i8259.irr3_b[PIC&1][source]))>> IR) & 1); //Interrupt requested on the specified source?
}

OPTINLINE void ACNIR(byte PIC, byte IR, byte source) //Acnowledge request!
{
	if (__HW_DISABLED) return; //Abort!
	i8259.irr3[PIC][source] &= ~(1 << IR); //Turn source IRR off!
	i8259.irr3_a[PIC][source] &= ~(1 << IR); //Turn source IRR off!
	i8259.irr3_b[PIC][source] &= ~(1 << IR); //Turn source IRR off!
	irr3_dirty = 1; //Dirty!
	i8259.irr[PIC] &= ~(1<<IR); //Clear the request!
	//Clearing IRR only for edge-triggered interrupts!
	i8259.isr[PIC] |= (1 << IR); //Turn in-service on!
	i8259.isr2[PIC][source] |= (1 << IR); //Turn the source on!
	if ((i8259.icw[PIC][3]&2)==2) //Automatic EOI?
	{
		EOI(PIC,source); //Send an EOI!
	}
	if (PIC) //Slave connected to Master?
	{
		lowerirq(0x802); //INTA lowers INTRQ!
		i8259.intreqtracking[1] = 0; //Not tracking INTREQ!
	}
}

byte readPollingMode(byte pic)
{
	byte IR;
	if (getunprocessedinterrupt(pic)) //Interrupt requested?
	{
		if (__HW_DISABLED) return 0; //Abort!
		//First, process the PIC!
		for (IR=0;IR<8;++IR)
		{
			byte realIR = (IR & 7); //What IR within the PIC?
			byte srcIndex;
			for (srcIndex = 0; srcIndex < 0x10; ++srcIndex) //Check all indexes!
			{
				if (IRRequested(pic, realIR, srcIndex)) //Requested?
				{
					ACNIR(pic, realIR, srcIndex); //Acnowledge it!
					lastinterrupt = getint(pic, realIR); //Give the interrupt number!
					i8259.lastinterruptIR[pic] = realIR; //Last acnowledged interrupt line!
					interruptsaved = 1; //Gotten an interrupt saved!
					//Don't perform layering to any slave, this is done at ACNIR!
					return 0x80 | realIR; //Give the raw IRQ number on the PIC!
				}
			}
		}

		i8259.lastinterruptIR[i8259.activePIC] = 7; //Last acnowledged interrupt line!
		lastinterrupt = getint(i8259.activePIC, 7); //Unknown, dispatch through IR7 of the used PIC!
		interruptsaved = 1; //Gotten!
		return i8259.lastinterruptIR[i8259.activePIC]; //No result: unk interrupt!
	}
	return 0x00; //No interrupt available!
}

byte i8259_INTA(byte whichCPU, byte fromAPIC)
{
	byte loopdet = 1;
	byte IR;
	byte PICnr;
	byte masterIR;
	//Not APIC, check the i8259 PIC now!
	PICnr = 0; //First PIC is connected to CPU INTA!
	masterIR = 0xFF; //Default: Unknown(=master device only) IR!
checkSlave:
	//First, process first PIC!
	for (IR = 0; IR < 8; IR++) //Process all IRs for this chip!
	{
		byte realIR = (IR & 7); //What IR within the PIC?
		byte srcIndex;
		for (srcIndex = 0; srcIndex < 0x10; ++srcIndex) //Check all indexes!
		{
			if (IRRequested(PICnr, realIR, srcIndex)) //Requested?
			{
				if (respondMasterSlave(PICnr, masterIR)) //PIC responds as a master or slave?
				{
					ACNIR(PICnr, realIR, srcIndex); //Acnowledge it!
					if (startSlaveMode(PICnr, realIR)) //Start slave processing for this? Let the slave give the IRQ!
					{
						if (loopdet) //Loop detection?
						{
							masterIR = IR; //Slave starts handling this IRQ!
							PICnr ^= 1; //What other PIC to check on the PIC bus?
							loopdet = 0; //Prevent us from looping on more PICs!
							goto checkSlave; //Check the slave instead!
						}
						else //Infinite loop detected!
						{
							goto unknownSlaveIR; //Unknown IR due to loop!
						}
					}

					lastinterrupt = getint(PICnr, realIR); //Give the interrupt number!
					interruptsaved = 1; //Gotten an interrupt saved!
					i8259.lastinterruptIR[PICnr] = realIR; //Last IR!
					return lastinterrupt;
				}
			}
		}
	}

unknownSlaveIR: //Slave has exited out to prevent looping!
	i8259.lastinterruptIR[PICnr] = 7; //Last IR!
	lastinterrupt = getint(PICnr, 7); //Unknown, dispatch through IR7 of the used PIC!
	if (fromAPIC) //Was it from the APIC? Spurious interrupt handling!
	{
		lastinterrupt = (LAPIC[whichCPU].SpuriousInterruptVectorRegister & 0xFF); //Give the APIC spurious interrupt vector instead!
	}
	interruptsaved = 1; //Gotten!
	return lastinterrupt; //No result: unk interrupt!
}

byte nextintr()
{
	sword result;
	if (__HW_DISABLED) return 0; //Abort!

	//Check APIC first!
	if (APIC_currentintnr[activeCPU]!=-1) //APIC interrupt requested to fire?
	{
		result = APIC_currentintnr[activeCPU]; //Accept!
		APIC_currentintnr[activeCPU] = -1; //Invalidate!
		//Now that we've selected the highest priority IR, start firing it!
		return (byte)result; //Give the interrupt vector number!
	}

	return i8259_INTA(activeCPU, 0); //Perform a normal INTA and give the interrupt number!
}

void LINT0_raiseIRQ(byte updatelivestatus)
{
	if (LAPIC[activeCPU].enabled != 1) return; //Not ready to handle?
	//Always set the LINT0 register bit!
	switch ((LAPIC[activeCPU].LVTLINT0Register >> 8) & 7) //What mode?
	{
	case 0: //Interrupt? Also named Fixed!
	case 1: //Lowest priority?
		if ((LAPIC[activeCPU].LVTLINT0Register & 0x8000) == 0) //Edge-triggered? Supported!
		{
			if ((IOAPIC.IOAPIC_liveIRR & 1) == 0) //Not yet raised? Rising edge!
			{
				LAPIC[activeCPU].LVTLINT0Register |= DELIVERYPENDING; //Perform LINT0!
			}
		}
		else
		{
			if ((LAPIC[activeCPU].LVTLINT0Register&REMOTEPENDING)!=0) return; //Kept pending by level!
			LAPIC[activeCPU].LVTLINT0Register |= DELIVERYPENDING; //Perform LINT0!
		}
		break;
	case 2: //SMI?
	case 4: //NMI?
	case 5: //INIT or INIT deassert?
		//Edge mode only! Don't do anything when lowered!
		if ((IOAPIC.IOAPIC_liveIRR & 1) == 0) //Not yet raised? Rising edge!
		{
			LAPIC[activeCPU].LVTLINT0Register |= DELIVERYPENDING; //Perform LINT0!
		}
		break;
	case 7: //extINT? Level only!
		//Always assume that the live IRR doesn't match to keep it live on the triggering!
		if ((LAPIC[activeCPU].LVTLINT0Register&REMOTEPENDING)!=0) return; //Kept pending by level!
		LAPIC[activeCPU].LVTLINT0Register |= DELIVERYPENDING; //Perform LINT0!
		break;
	}
	if (updatelivestatus)
	{
		IOAPIC.IOAPIC_liveIRR |= 1; //Live status!
	}
}

void APIC_raisedIRQ(byte PIC, word irqnum)
{
	//A line has been raised!
	if (irqnum == 0) irqnum = 2; //IRQ0 is on APIC line 2!
	else if (irqnum == 2) irqnum = 0; //INTR to APIC line 0!
	//INTR is also on APIC line 0!
	if (irqnum == 0) //Since we're also connected to the CPU, raise LINT properly!
	{
		//Always assume live doesn't match! So that the LINT0 register keeps being up-to-date!
		if ((IMCR&1) == 0) //Connected to the CPU?
		{
			LINT0_raiseIRQ(1); //Raise LINT0 input!
		}
	}

	IOAPIC.IOAPIC_currentliveIRR |= (1 << (irqnum & 0xF)); //Live status!
	if (IOAPIC.IOAPIC_requirestermination[irqnum & 0xF]) return; //Can't handle while busy! Don't update the live IRR, because we haven't registered it yet!
	switch ((IOAPIC.IOAPIC_redirectionentry[irqnum & 0xF][0] >> 8) & 7) //What mode?
	{
	case 0: //Interrupt? Also named Fixed!
	case 1: //Lowest priority?
		if ((IOAPIC.IOAPIC_redirectionentry[irqnum & 0xF][0] & 0x8000) == 0) //Edge-triggered? Supported!
		{
			if ((IOAPIC.IOAPIC_liveIRR & (1 << (irqnum & 0xF))) == 0) //Not yet raised? Rising edge!
			{
				if ((IOAPIC.IOAPIC_redirectionentry[irqnum & 0xF][0] & 0x10000) == 0) //Not masked?
				{
					IOAPIC.IOAPIC_redirectionentry[irqnum & 0xF][0] |= DELIVERYPENDING; //Waiting to be delivered!
					if (!(IOAPIC.IOAPIC_IRRset & (1 << (irqnum & 0xF)))) //Not already pending?
					{
						IOAPIC.IOAPIC_redirectionentry[irqnum & 0xF][0] &= ~DELIVERYPENDING; //Not waiting to be delivered!
						IOAPIC.IOAPIC_IRRset |= (1 << (irqnum & 0xF)); //Set the IRR?
						IOAPIC.IOAPIC_IRRreq &= ~(1 << (irqnum & 0xF)); //Acnowledged if pending!
					}
				}
				else //Masked?
				{
					IOAPIC.IOAPIC_IRRreq |= (1 << (irqnum & 0xF)); //Requested to fire!
				}
			}
		}
		else
		{
			if ((IOAPIC.IOAPIC_redirectionentry[irqnum & 0xF][0]&REMOTEPENDING)!=0) return; //Kept pending by level!
			if ((IOAPIC.IOAPIC_redirectionentry[irqnum & 0xF][0] & 0x10000) == 0) //Not masked?
			{
				IOAPIC.IOAPIC_redirectionentry[irqnum & 0xF][0] |= DELIVERYPENDING; //Waiting to be delivered!
				if (!(IOAPIC.IOAPIC_IRRset & (1 << (irqnum & 0xF)))) //Not already pending?
				{
					IOAPIC.IOAPIC_redirectionentry[irqnum & 0xF][0] &= ~DELIVERYPENDING; //Not waiting to be delivered!
					IOAPIC.IOAPIC_IRRset |= (1 << (irqnum & 0xF)); //Set the IRR?
					IOAPIC.IOAPIC_IRRreq &= ~(1 << (irqnum & 0xF)); //Acnowledged if pending!
				}
			}
			else //Masked?
			{
				IOAPIC.IOAPIC_IRRreq |= (1 << (irqnum & 0xF)); //Requested to fire!
			}
		}
		break;
	case 2: //SMI?
	case 4: //NMI?
	case 5: //INIT or INIT deassert?
		//Edge mode only! Don't do anything when lowered!
		if ((IOAPIC.IOAPIC_liveIRR & (1 << (irqnum & 0xF))) == 0) //Not yet raised? Rising edge!
		{
			if ((IOAPIC.IOAPIC_redirectionentry[irqnum & 0xF][0] & 0x10000) == 0) //Not masked?
			{
				IOAPIC.IOAPIC_redirectionentry[irqnum & 0xF][0] |= DELIVERYPENDING; //Waiting to be delivered!
				if (!(IOAPIC.IOAPIC_IRRset & (1 << (irqnum & 0xF)))) //Not already pending?
				{
					IOAPIC.IOAPIC_redirectionentry[irqnum & 0xF][0] &= ~DELIVERYPENDING; //Not waiting to be delivered!
					IOAPIC.IOAPIC_IRRset |= (1 << (irqnum & 0xF)); //Set the IRR?
					IOAPIC.IOAPIC_IRRreq &= ~(1 << (irqnum & 0xF)); //Acnowledged if pending!
				}
			}
			else //Masked?
			{
				IOAPIC.IOAPIC_IRRreq |= (1 << (irqnum & 0xF)); //Requested to fire!
			}
		}
		break;
	case 7: //extINT? Level only!
		//Always assume that the live IRR doesn't match to keep it live on the triggering!
		if ((IOAPIC.IOAPIC_redirectionentry[irqnum & 0xF][0] & 0x10000) == 0) //Not masked?
		{
			IOAPIC.IOAPIC_redirectionentry[irqnum & 0xF][0] |= DELIVERYPENDING; //Waiting to be delivered!
			if (!(IOAPIC.IOAPIC_IRRset & (1 << (irqnum & 0xF)))) //Not already pending?
			{
				IOAPIC.IOAPIC_redirectionentry[irqnum & 0xF][0] &= ~DELIVERYPENDING; //Not waiting to be delivered!
				IOAPIC.IOAPIC_IRRset |= (1 << (irqnum & 0xF)); //Set the IRR?
				IOAPIC.IOAPIC_IRRreq &= ~(1 << (irqnum & 0xF)); //Acnowledged if pending!
				IOAPIC.IOAPIC_redirectionentryReceiversDetermined[(irqnum&0xF)] = 0; //Not determined anymore!
			}
		}
		else //Masked?
		{
			IOAPIC.IOAPIC_IRRreq |= (1 << (irqnum & 0xF)); //Requested to fire!
		}
		break;
	}
	IOAPIC.IOAPIC_liveIRR |= (1 << (irqnum & 0xF)); //Live status!
}

void IOAPIC_raisepending()
{
	if (!(IOAPIC.IOAPIC_IRRreq&~(IOAPIC.IOAPIC_IMRset)&~(IOAPIC.IOAPIC_IRRset))) return; //Nothing to do?
	byte irqnum;
	for (irqnum=0;irqnum<24;++irqnum) //Check all!
	{
		if (IOAPIC.IOAPIC_requirestermination[irqnum & 0xF]) continue; //Can't handle while busy!
		//Don't need to take the mode into account!
		if (IOAPIC.IOAPIC_redirectionentry[irqnum & 0xF][0] & DELIVERYPENDING) //Waiting to be delivered!
		{
			if ((IOAPIC.IOAPIC_redirectionentry[irqnum & 0xF][0] & 0x10000) == 0) //Not masked?
			{
				if (!(IOAPIC.IOAPIC_IRRset & (1 << (irqnum & 0xF)))) //Not already pending?
				{
					if ((IOAPIC.IOAPIC_IRRreq & (1 << (irqnum & 0xF)))) //Pending requested?
					{
						IOAPIC.IOAPIC_redirectionentry[irqnum & 0xF][0] &= ~DELIVERYPENDING; //Not waiting to be delivered!
						IOAPIC.IOAPIC_IRRset |= (1 << (irqnum & 0xF)); //Set the IRR?
						IOAPIC.IOAPIC_IRRreq &= ~(1 << (irqnum & 0xF)); //Requested to fire!
					}
				}
			}
		}
	}
}

void LINT0_lowerIRQ()
{
	if (LAPIC[activeCPU].LVTLINT0RegisterDirty) return; //Not ready to parse?
	switch ((LAPIC[activeCPU].LVTLINT0Register >> 8) & 7) //What mode?
	{
	case 0: //Interrupt? Also named Fixed!
	case 1: //Lowest priority?
		if (LAPIC[activeCPU].LVTLINT0Register & 0x8000) //Level-triggered? Supported!
		{
			LAPIC[activeCPU].LVTLINT0Register &= ~DELIVERYPENDING; //Clear LINT0!
		}
		break;
	case 2: //SMI?
	case 4: //NMI?
	case 5: //INIT or INIT deassert?
		//Edge mode only! Don't do anything when lowered!
		break;
	case 7: //extINT? Level only!
		//Always assume that the live IRR doesn't match to keep it live on the triggering!
		LAPIC[activeCPU].LVTLINT0Register &= ~DELIVERYPENDING; //Clear LINT0!
		break;
	}
	IOAPIC.IOAPIC_liveIRR &= ~1; //Live status!
}

void APIC_loweredIRQ(byte PIC, word irqnum)
{
	if (irqnum == 0) irqnum = 2; //IRQ0 is on APIC line 2!
	else if (irqnum == 2) irqnum = 0; //INTR to APIC line 0!
	//INTR is also on APIC line 0!
	//A line has been lowered!
	IOAPIC.IOAPIC_currentliveIRR &= ~(1 << (irqnum & 0xF)); //Live status!
	if (irqnum == 0) //Since we're also connected to the CPU, raise LINT properly!
	{
		LINT0_lowerIRQ();
	}
	if (IOAPIC.IOAPIC_requirestermination[irqnum & 0xF]) return; //Can't handle while busy! Don't update the live IRR, because we can't handle it yet!
	if ((IOAPIC.IOAPIC_redirectionentry[irqnum & 0xF][0] & 0x8000) == 0) //Edge-triggered? Supported!
	{
		switch ((IOAPIC.IOAPIC_redirectionentry[irqnum & 0xF][0] >> 8) & 7) //What mode?
		{
		case 0: //Interrupt? Also named Fixed!
		case 1: //Lowest priority?
			//Don't do anything for edge triggered! Lowering on edge mode has no effect on the pending status!
			break;
		case 2: //SMI?
		case 4: //NMI?
		case 5: //INIT or INIT deassert?
			//Edge mode only! Don't do anything when lowered!
			break;
		case 7: //extINT? Level only!
			//Always assume that the live IRR doesn't match to keep it live on the triggering!
			if (IOAPIC.IOAPIC_IRRset & (1 << (irqnum & 0xF))) //Waiting to be delivered?
			{
				IOAPIC.IOAPIC_redirectionentry[irqnum & 0xF][0] &= ~DELIVERYPENDING; //Not waiting to be delivered!
				IOAPIC.IOAPIC_IRRset &= ~(1 << (irqnum & 0xF)); //Clear the IRR?
				IOAPIC.IOAPIC_IRRreq &= ~(1 << (irqnum & 0xF)); //Not requested to fire!
			}
			break;
		}
	}
	else //Level-triggered?
	{
		switch ((IOAPIC.IOAPIC_redirectionentry[irqnum & 0xF][0] >> 8) & 7) //What mode?
		{
		case 0: //Interrupt? Also named Fixed!
		case 1: //Lowest priority?
			IOAPIC.IOAPIC_redirectionentry[irqnum & 0xF][0] &= ~DELIVERYPENDING; //Not waiting to be delivered!
			IOAPIC.IOAPIC_IRRset &= ~(1 << (irqnum & 0xF)); //Clear the IRR?
			IOAPIC.IOAPIC_IRRreq &= ~(1 << (irqnum & 0xF)); //Not requested to fire!
			break;
		case 2: //SMI?
		case 4: //NMI?
		case 5: //INIT or INIT deassert?
			//Edge mode only! Don't do anything when lowered!
			break;
		case 7: //extINT? Level only!
			//Always assume that the live IRR doesn't match to keep it live on the triggering!
			IOAPIC.IOAPIC_redirectionentry[irqnum & 0xF][0] &= ~DELIVERYPENDING; //Not waiting to be delivered!
			IOAPIC.IOAPIC_IRRset &= ~(1 << (irqnum & 0xF)); //Clear the IRR?
			IOAPIC.IOAPIC_IRRreq &= ~(1 << (irqnum & 0xF)); //Not requested to fire!
			break;
		}
	}
	IOAPIC.IOAPIC_liveIRR &= ~(1 << (irqnum & 0xF)); //Live status!
}

void updateAPICliveIRRs()
{
	uint_32 IRbit;
	byte IR,effectiveIR;
	if (unlikely((IOAPIC.IOAPIC_liveIRR != IOAPIC.IOAPIC_currentliveIRR) || recheckLiveIRRs)) //Different IRR lines that's pending?
	{
		IRbit = 1; //IR bit!
		for (IR = 0; IR < 24; ++IR) //Process all IRs!
		{
			if (((IOAPIC.IOAPIC_liveIRR ^ IOAPIC.IOAPIC_currentliveIRR) & IRbit) || recheckLiveIRRs) //Different requiring processing?
			{
				effectiveIR = IR;
				if (IR == 0) effectiveIR = 2; //Swapped...
				else if (IR == 2) effectiveIR = 0; //Values due to wiring on the APIC inputs!
				if (IOAPIC.IOAPIC_currentliveIRR & IRbit) //To be set?
				{
					APIC_raisedIRQ((IR >= 8) ? 1 : 0, effectiveIR); //Raised wire!
				}
				else //To be cleared?
				{
					APIC_loweredIRQ((IR >= 8) ? 1 : 0, effectiveIR); //Lowered wire!
				}
			}
			IRbit <<= 1; //Next bit to check!
		}
		recheckLiveIRRs = 0; //Don't recheck again!
	}
}

void raiseirq(word irqnum)
{
	if (__HW_DISABLED) return; //Abort!
	byte requestingindex=(irqnum&0xFF); //Save our index that's requesting!
	irqnum &= 0xF; //Only 16 IRQs!
	requestingindex >>= 4; //What index is requesting?
	byte PIC = (irqnum>>3); //IRQ8+ is high PIC!
	byte irr2index;
	byte hasirr = 0;
	byte oldIRR = 0;
	irr2index = requestingindex; //What is requested?
	//Handle edge-triggered IRR!
	hasirr = 0; //Init IRR state!
	//for (irr2index = 0;irr2index < 0x10;++irr2index) //Verify if anything is left!
	{
		if (i8259.irr2[PIC][irr2index] & (1 << (irqnum & 7))) //Request still set?
		{
			hasirr = 1; //We still have an IRR!
			//break; //Stop searching!
		}
	}
	oldIRR = hasirr; //Old IRR state!

	i8259.irr2[PIC][requestingindex] |= (1 << (irqnum & 7)); //Add the IRQ to request!
	//hasirr = 0; //Init IRR state!
	//for (irr2index = 0;irr2index < 0x10;++irr2index) //Verify if anything is left!
	{
		//if (i8259.irr2[PIC][irr2index] & (1 << (irqnum & 7))) //Request still set?
		{
			hasirr = 1; //We still have an IRR!
			//break; //Stop searching!
		}
	}

	if (hasirr && ((hasirr^oldIRR)&1)) //The line is actually raised?
	{
		if (irqnum != 2) //Not valid to cascade on the APIC!
		{
			APIC_raisedIRQ(PIC, irqnum); //We're raised!
		}
		i8259.irr3[PIC][requestingindex] |= (1 << (irqnum & 7)); //Add the IRQ to request because of the rise! This causes us to be the reason during shared IR lines!
		irr3_dirty = 1; //Dirty!
	}
}

void lowerirq(word irqnum)
{
	if (__HW_DISABLED) return; //Abort!
	byte requestingindex = (irqnum&0xFF); //Save our index that's requesting!
	irqnum &= 0xF; //Only 16 IRQs!
	requestingindex >>= 4; //What index is requesting?
	byte PIC = (irqnum>>3); //IRQ8+ is high PIC!
	byte lowerirr, lowerirr2;
	if (i8259.irr2[PIC][requestingindex]&(1 << (irqnum & 7))) //Were we raised?
	{
		i8259.irr2[PIC][requestingindex] &= ~(1 << (irqnum & 7)); //Lower the IRQ line to request!
		lowerirr = i8259.irr3[PIC][requestingindex]; //What has been lowered!
		lowerirr2 = i8259.irr3_a[PIC][requestingindex]; //What has been lowered!
		i8259.irr3[PIC][requestingindex] &= ~(1 << (irqnum & 7)); //Remove the request being used itself!
		i8259.irr3_a[PIC][requestingindex] &= ~(1 << (irqnum & 7)); //Remove the acnowledge!
		i8259.irr3_b[PIC][requestingindex] &= ~(1 << (irqnum & 7)); //Remove the acnowledge!
		irr3_dirty = 1; //Dirty!
		if (irqnum != 2) //Not valid to cascade on the APIC!
		{
			APIC_loweredIRQ(PIC, irqnum); //We're lowered!
		}
		if ((lowerirr&lowerirr2)&(1 << (irqnum & 7))) //Were we acnowledged and loaded?
		{
			i8259.irr[PIC] &= ~(1<<(irqnum&7)); //Remove the request, if any! New requests can be loaded!
		} //Is this only for level triggered interrupts?
	}
}

void acnowledgeIRQrequest(byte irqnum)
{
	//We don't lower raised interrupts!
}

void registerIRQ(byte IRQ, IRQHandler acceptIRQ, IRQHandler finishIRQ)
{
	//Register the handlers!
	i8259.acceptirq[IRQ&0xF][IRQ>>4] = acceptIRQ;
	i8259.finishirq[IRQ&0xF][IRQ>>4] = finishIRQ;
}
