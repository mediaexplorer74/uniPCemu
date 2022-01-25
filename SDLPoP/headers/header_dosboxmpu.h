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

#ifndef HEADER_DOSBOXMPU_H
#define HEADER_DOSBOXMPU_H

#include "headers/header_dosbox.h" //Basic dosbox patches!
#include "headers/hardware/midi/midi.h" //MIDI OUT/IN device support!
#include "headers/hardware/pic.h" //Own typedefs etc.
#include "headers/hardware/ports.h" //I/O port support!
//Our own typedefs for easier changing of the dosbox code!
#define MIDI_RawOutByte MIDI_OUT
#define MIDI_Available() 1
#define MPU_IRQ_XT 2
#define MPU_IRQ_AT 9

//Remove overflow used in math.h
#undef OVERFLOW

//PIC support!
#define PIC_RemoveEvents(function) removeMPUTimer()
#define PIC_AddEvent(function,timeout) setMPUTimer(timeout,function)
#define PIC_ActivateIRQ(irq) raiseirq(irq)
#define PIC_DeActivateIRQ(irq) lowerirq(irq)

#define IO_RegisterWriteHandler(port,handler,name) register_PORTOUT(handler)
#define IO_RegisterReadHandler(port,handler,name) register_PORTIN(handler)
#endif
