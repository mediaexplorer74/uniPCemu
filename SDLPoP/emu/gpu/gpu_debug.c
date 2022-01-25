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

#include "headers/emu/gpu/gpu.h" //GPU typedefs etc.
#include "headers/interrupts/interrupt10.h" //getscreenwidth() support!
#include "headers/fopen64.h" //64-bit fopen support!

extern GPU_type GPU; //GPU!

void dumpscreen()
{
	int emux;
	int emuy; //psp x&y!
//First, calculate the relative destination on the PSP screen!.

	BIGFILE *f;
	f = emufopen64("SCREEN.TXT","w"); //Open file!
	char lb[3];
	cleardata(&lb[0],sizeof(lb));
	safestrcpy(lb,sizeof(lb),"\r\n"); //Line break!

	char message[256];
	cleardata(&message[0],sizeof(message)); //Init!
	snprintf(message,sizeof(message),"Screen width: %u",getscreenwidth());

	emufwrite64(&message,1,safe_strlen(message,sizeof(message)),f); //Write message!
	emufwrite64(&lb,1,safe_strlen(lb,sizeof(lb)),f); //Line break!

	for (emuy=0; emuy<GPU.xres; emuy++) //Process row!
	{
		emufwrite64(&lb,1,safe_strlen(lb,sizeof(lb)),f); //Line break!
		for (emux=0; emux<GPU.xres; emux++) //Process column!
		{
			char c;
			c = (GPU.emu_screenbuffer[(emuy*GPU.xres)+emux]!=0)?'X':' '; //Data!
			emufwrite64(&c,1,sizeof(c),f); //1 or 0!
		}
	}
	emufclose64(f); //Done!
}