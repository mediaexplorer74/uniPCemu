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

//Ports compatibility (e.g. for keyboard, mouse and joystick), input/output to hw ports from software!

#include "headers/types.h"
#include "headers/hardware/ports.h" //Full PORTIN/OUT compatibility!
#include "headers/support/log.h" //Logging support!
#include "headers/cpu/cpu.h" //BIU support!
#include "headers/cpu/biu.h" //BIU support!

//Log unhandled port IN/OUT?
//#define LOG_UNHANDLED_PORTS

/*

We handle direct input/output to/from hardware ports by the CPU!

*/

/*

Initialises ports support!

*/

void Ports_Init()
{
	reset_ports(); //Passtrough: reset all ports!
}

byte PORT_IN_B(word port)
{
	byte result;
	if (EXEC_PORTIN(port,&result)) //Passtrough!
	{
#ifdef LOG_UNHANDLED_PORTS
		dolog("emu", "Warning: Unhandled PORT IN from port %04X (src:%04X:%08X)", port,CPU[activeCPU].exec_CS,CPU[activeCPU].exec_EIP);
#endif
	}
	return result; //Give the result!
}

void PORT_OUT_B(word port, byte b)
{
	if (EXEC_PORTOUT(port, b)) //Passtrough and error?
	{
#ifdef LOG_UNHANDLED_PORTS
		dolog("emu", "Warning: Unhandled PORT OUT to port %04X value %02X (src:%04X:%08X)", port, b, CPU[activeCPU].exec_CS, CPU[activeCPU].exec_EIP); //Report unhandled NMI!
#endif
	}
}

extern BIU_type BIU[MAXCPUS]; //BIU!

word PORT_IN_W(word port) //IN result,port
{
	word w;
	if (port & 1) //Not aligned?
	{
		goto bytetransferr; //Force byte transfer!
	}
	if (EXEC_PORTINW(port, &w)) //Passtrough!
	{
		bytetransferr:
		w = PORT_IN_B(port); //Low first
		++BIU[activeCPU].newtransfer; //We're a next transfer!
		w |= (PORT_IN_B(port + 1)<<8); //High last!
		++BIU[activeCPU].newtransfer; //We're a next transfer!
	}
	return w; //Give word!
}

void PORT_OUT_W(word port, word w) //OUT port,w
{
	if (port & 1) //Not aligned?
	{
		goto bytetransferw; //Force byte transfer!
	}
	if (EXEC_PORTOUTW(port, w)) //Passtrough!
	{
		bytetransferw:
		PORT_OUT_B(port, (w&0xFF)); //First low byte!
		++BIU[activeCPU].newtransfer; //We're a next transfer!
		PORT_OUT_B(port + 1, ((w>>8)&0xFF)); //Next high byte!
		++BIU[activeCPU].newtransfer; //We're a next transfer!
	}
}

uint_32 PORT_IN_D(word port) //IN result,port
{
	uint_32 dw;
	if (port & 3) //Not aligned?
	{
		goto wordtransferr; //Force word transfer!
	}
	if (EXEC_PORTIND(port, &dw)) //Passtrough!
	{
		wordtransferr:
		dw = PORT_IN_W(port); //Low first
		dw |= (PORT_IN_W(port + 2)<<16); //High last!
	}
	return dw; //Give dword!
}

void PORT_OUT_D(word port, uint_32 dw) //OUT port,w
{
	if (port & 3) //Not aligned?
	{
		goto wordtransferw; //Force word transfer!
	}
	if (EXEC_PORTOUTD(port, dw)) //Passtrough!
	{
		wordtransferw:
		PORT_OUT_W(port, (dw&0xFFFF)); //First low byte!
		PORT_OUT_W(port + 2, ((dw>>16)&0xFFFF)); //Next high byte!
	}
}
