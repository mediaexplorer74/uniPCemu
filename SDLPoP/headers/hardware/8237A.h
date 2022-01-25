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

#ifndef __HW_8237A_H
#define __HW_8237A_H

typedef void(*DMAWriteBHandler)(byte data); //Write handler to DMA hardware!
typedef byte(*DMAEOPHandler)(); //EOP handler from DMA hardware!
typedef byte(*DMAReadBHandler)(); //Read handler from DMA hardware!
typedef void(*DMAWriteWHandler)(word data); //Write handler to DMA hardware!
typedef word(*DMAReadWHandler)(); //Read handler from DMA hardware!
typedef void(*DMATickHandler)(); //Tick handler for DMA hardware!

void initDMA(); //Initialise the DMA support!
void doneDMA(); //Finish the DMA support!

void registerDMA8(byte channel, DMAReadBHandler readhandler, DMAWriteBHandler writehandler);
void registerDMA16(byte channel, DMAReadWHandler readhandler, DMAWriteWHandler writehandler);
void registerDMATick(byte channel, DMATickHandler DREQHandler, DMATickHandler DACKHandler, DMATickHandler TCHandler, DMAEOPHandler EOPHandler);

void DMA_SetDREQ(byte channel, byte DREQ); //Set DREQ from hardware!

//CPU related timing!
void updateDMA(uint_32 MHZ14passed, uint_32 CPUcyclespassed); //Tick the DMA controller when needed!
void cleanDMA(); //Skip all ticks up to now!

#endif