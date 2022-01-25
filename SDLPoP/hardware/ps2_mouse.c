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

#include "headers/hardware/8042.h" //PS/2 Controller support!
#include "headers/hardware/ps2_mouse.h" //Our support!
#include "headers/support/fifobuffer.h"
#include "headers/support/zalloc.h" //Zero free allocation for linked lists!
#include "headers/emu/input.h" //For timing packets!
#include "headers/support/signedness.h" //Signedness support!

//To disable the mouse?
#define MOUSE_DISABLED Mouse.disabled

//Are we disabled?
#define __HW_DISABLED 0

//How long does a step in the BAT take?
#define PS2_MOUSE_BAT_TIMEOUT 100000.0

extern Controller8042_t Controller8042; //The 8042 controller!

typedef struct MOUSE_PACKET
{
	int_32 xmove;
	int_32 ymove;
	byte buttons; //1=Left, 2=Right, 4=Middle bitmask.
	sbyte scroll; //scroll up(MAX -8)/down(MAX +7); Used during 4-byte packets only! After setsamplerate 200,200,80 and request ID 4.
	struct MOUSE_PACKET* next; //Next packet!
} MOUSE_PACKET;

struct
{
	union //All mouse data!
	{
		struct
		{
			byte has_command; //Have command?
			byte command_step; //Command step!
			byte command; //What command has been written?
			byte last_was_error; //Last command was an error?

			byte mode; //0=stream;1=wrap;2=remote
			byte lastmode; //Last mode!
			byte data_reporting; //Use data reporting?

			byte packetindex; //For helping processing the current byte of a mouse packet!

			byte samplerate; //Sample rate!
			byte resolution; //Counts/mm
			byte scaling21; //1:1=0 or 2:1=1
			byte Resend; //Use last read once?
			byte buttonstatus; //Button status for status bytes!
			byte buttonstatus_dirty; //Buttons dirty?
			byte disabled; //Is the mouse input disabled atm?
			byte pollRemote; //Set when requested to poll a remote packet!
		};
		byte data[15]; //All mouse data!
	};

	FIFOBUFFER *buffer; //FIFO buffer for commands etc.

	MOUSE_PACKET *packets; //Contains all packets!
	MOUSE_PACKET *lastpacket; //Last send packet!

	float xmove; //X movement recorded, in mm!
	float ymove; //Y movement recorded, in mm!
	float effectiveresolution; //Effective resolution, in mm!

	byte supported; //PS/2 mouse supported on this system?
	DOUBLE timeout; //Timeout for reset commands!
	float activesamplerate; //Currently active samplerate!
} Mouse; //Ourselves!

OPTINLINE void give_mouse_output(byte data)
{
	if (__HW_DISABLED) return; //Abort!
	writefifobuffer(Mouse.buffer,data); //Write to the buffer, ignore the result!
}

OPTINLINE void input_lastwrite_mouse()
{
	if (__HW_DISABLED) return; //Abort!
	fifobuffer_gotolast(Mouse.buffer); //Goto last!
}

OPTINLINE void next_mousepacket() //Reads the next mouse packet, if any!
{
	if (__HW_DISABLED) return; //Abort!
	MOUSE_PACKET *oldpacket = Mouse.lastpacket; //Save the last packet!
	MOUSE_PACKET *currentpacket = Mouse.packets; //Current packet!
	if (currentpacket) //Gotten a packet?
	{
		Mouse.packets = currentpacket->next; //Set to next packet!
		Mouse.lastpacket = currentpacket; //Move the now old packet to the last packet!
		if (oldpacket!=Mouse.lastpacket) //Original old packet is finished (not a resend)?
		{
			freez((void **)&oldpacket,sizeof(MOUSE_PACKET),"Old MousePacket"); //Release the last packet!
		}
	}
	//Else: nothing to next!
}

OPTINLINE void flushPackets() //Flushes all mouse packets!
{
	if (__HW_DISABLED) return; //Abort!
	while (likely(Mouse.packets))
	{
		next_mousepacket(); //Flush all packets!
	}
	if (unlikely(Mouse.lastpacket))
	{
		freez((void **)&Mouse.lastpacket,sizeof(MOUSE_PACKET),"Mouse_FlushPacket");
	}
	Mouse.buttonstatus = 0; //No buttons!
	Mouse.buttonstatus_dirty = 0; //Not dirty!
	
}

OPTINLINE void resend_lastpacket() //Resends the last packet!
{
	if (__HW_DISABLED) return; //Abort!
	if (likely(Mouse.lastpacket && !Mouse.packetindex)) //Gotten a last packet and at the start of a packet?
	{
		Mouse.packets = Mouse.lastpacket; //Resend the last packet!
	}
	//If we don't have a last packet, take the start of the current packet, else the start of the last packet!
	Mouse.packetindex = 0; //Reset packet index always!
}

OPTINLINE void update_mouseresolution() //Resolution specified for the mouse!
{
	if (__HW_DISABLED) return; //Abort!
	switch (Mouse.resolution) //What resolution?
	{
	case 0x00: case 0x01: case 0x02: case 0x03:
		Mouse.effectiveresolution = 1.0f/((float)(1 << Mouse.resolution)); //1/2/4/8 count/mm!
		break;
	default: //Unknown?
		Mouse.effectiveresolution = 1.0f; //Unchanged!
		break;
	}
}

byte mouse_movepending()
{
	return ((int_32)(Mouse.xmove/Mouse.effectiveresolution)) || ((int_32)(Mouse.ymove/Mouse.effectiveresolution)) || (Mouse.buttonstatus_dirty); //Movement or buttons changing registered?
}

byte add_mouse_packet(byte buttons, float* xmovemm, float* ymovemm, float* xmovemickeys, float* ymovemickeys) //Add an allocated mouse packet!
{
	MOUSE_PACKET *packet, *currentpacket;
	int_32 xmove, ymove;
	if (__HW_DISABLED) return 1; //Abort!

	//We're a valid packet to receive?
	if (Mouse.packets) //A packet is already queued? We can't send another one!
	{
		return 0; //Discard the packet until we can receive it!
	}
	Mouse.buttonstatus_dirty |= (Mouse.buttonstatus != buttons); //Buttons dirty?
	Mouse.buttonstatus = buttons; //Save the current button status!
	Mouse.xmove += *xmovemm; //X movement!
	Mouse.ymove += *ymovemm; //Y movement!
	*xmovemm = 0.0f; //Clear: we're processed!
	*ymovemm = 0.0f; //Clear: we're processed!

	if (Mouse.packets) //A packet is already queued? We can't send another one!
	{
		return 0; //Discard the packet until we can receive it!
	}
	if (mouse_movepending()) //Movement pending?
	{
		xmove = (int_32)(Mouse.xmove / Mouse.effectiveresolution); //How much movement?
		ymove = (int_32)(Mouse.ymove / Mouse.effectiveresolution); //How much movement?
		xmove = LIMITRANGE(xmove, -0x100, 0xFF); //X movement limited to container!
		ymove = LIMITRANGE(ymove, -0x100, 0xFF); //Y movement limited to container!

		packet = (MOUSE_PACKET*)zalloc(sizeof(MOUSE_PACKET), "Mouse_Packet", NULL); //Allocate a mouse packet!
		if (packet) //Packet successfully allocated?
		{
			Mouse.buttonstatus_dirty = 0; //Not dirty anymore!
			Mouse.xmove -= (xmove * Mouse.effectiveresolution); //Substract handled movement!
			Mouse.ymove -= (ymove * Mouse.effectiveresolution); //Substract handled movement!

			//Apply the status to the packet!
			packet->buttons = Mouse.buttonstatus; //The buttons!
			packet->xmove = xmove; //X movement!
			packet->ymove = ymove; //Y movement!
			packet->scroll = 0; //Scrolling not supported!

			//Now, add the packet to the queue!
			currentpacket = Mouse.packets; //Current packet!
			if (unlikely(Mouse.packets)) //Already have one?
			{
				while (likely(currentpacket->next)) //Gotten next?
				{
					currentpacket = (MOUSE_PACKET*)currentpacket->next; //Next packet!
				}
				currentpacket->next = packet; //Set next packet to the new packet!
			}
			else
			{
				Mouse.packets = packet; //Set as current packet!
			}
		}
		else //Couldn't parse?
		{
			return 1; //Packet parsed, but not handled!
		}
	}
	return 1; //Packet ready!
}

//Handle a mouse packet!
byte PS2mouse_packet_handler(byte buttons, float* xmovemm, float* ymovemm, float* xmovemickeys, float* ymovemickeys) //Packet muse be allocated using zalloc!
{
	if (__HW_DISABLED) return 0; //Abort!
	if (likely(Mouse.supported==0)) return 0; //PS/2 mouse not supported!
	if (!PS2_SECONDPORTDISABLED(Controller8042)) //We're enabled?
	{
		if (add_mouse_packet(buttons, xmovemm, ymovemm, xmovemickeys, ymovemickeys)) //Add a mouse packet, and according to timing!
		{} //Do nothing when added!
	}
	return 1; //We're supported!
}

float HWmouse_getsamplerate() //Which repeat rate to use after the repeat delay! (chars/second)
{
	if (__HW_DISABLED) return 1.0f; //Abort!
	float result;
	result = Mouse.samplerate; //Get result!
	return result; //Give the repeat rate!
}

int useMouseTimer()
{
	if (__HW_DISABLED) return 0; //Abort!
	if (MOUSE_DISABLED) return 0; //No usage!
	if (PS2_SECONDPORTDISABLED(Controller8042)) return 0; //No usage!
	switch (Mouse.mode) //What mode?
	{
	case 0: //Streaming mode!
		if (Mouse.data_reporting == 0) return 0; //Don't fill when receiving packets with data reporting off!
		break;
	case 1: //Echo mode!
		return 0; //Packets disabled!
		break;
	case 2: //Remote mode!
		if ((Mouse.data_reporting == 0) && (Mouse.packets)) return 0; //Don't fill when receiving packets with data reporting off or when in remote mode and the buffer is filled!
		break;
	}
	return 1; //We're enabled!
}

void update_mouseTimer()
{
	if (__HW_DISABLED) return; //Abort!
	if (MOUSE_DISABLED) return; //Mouse disabled?
	Mouse.activesamplerate = HWmouse_getsamplerate(); //Update the active sample rate!
	setMouseRate(Mouse.activesamplerate); //Start using this samplerate!
}

//cause: 0=Disable, 1=Enable port, 3=BAT
OPTINLINE void resetPS2Mouse(byte cause)
{
	if (__HW_DISABLED) return; //Abort!
	flushPackets(); //Flush all packets!
	memset(&Mouse.data,0,sizeof(Mouse.data)); //Reset the mouse!
	Mouse.samplerate = 100; //100 packets/second!
	update_mouseTimer(); //Update the timer!
	//No data reporting!
	Mouse.resolution = 0x02; //4 pixel/mm resolution!
	update_mouseresolution(); //Update it!
}

void updatePS2Mouse(DOUBLE timepassed)
{
	if (unlikely(Mouse.timeout)) //Gotten a timeout?
	{
		Mouse.timeout -= timepassed; //Pass some time!
		if (unlikely(Mouse.timeout <= 0.0)) //Done?
		{
			Mouse.timeout = (DOUBLE)0; //Finished!
			switch (Mouse.command) //What command to execute?
			{
				case 0xFF:
					switch (Mouse.command_step) //What step?
					{
					case 1: //First stage?
						input_lastwrite_mouse(); //Force 0x00(dummy byte) to user!
						give_mouse_output(0xFA); //Acnowledge!
						resetPS2Mouse(3); //Reset the Keyboard Controller! Don't give a result(this will be done in time)!
						Mouse.timeout = MOUSE_DEFAULTTIMEOUT; //A small delay for the result code to appear!
						Mouse.command_step = 2; //Step 2!
						Mouse.command = 0xFF; //Restore the command byte, so that we can continue!
						Mouse.has_command = 1; //We're stil executing a command!
						Mouse.last_was_error = 0; //Last is OK!
						break;
					case 2: //Final stage?
						give_mouse_output(0xAA); //Give the result code!
						Mouse.timeout = MOUSE_DEFAULTTIMEOUT; //A small delay for the result code to appear!
						Mouse.command_step = 3; //Step 3! Give the ID next!
						break;
					case 3: //ID mode?
						Mouse.timeout = (DOUBLE)0; //Finished!
						give_mouse_output(0x00); //Give the ID code: PS/2 mouse!
						Mouse.command_step = 0; //Finished!
						Mouse.has_command = 0; //Finished command!
						break;
					default:
						break;
					}
					break;
				default:
					break;
			}
		}
	}
}

OPTINLINE void initPS2Mouse()
{
	if (__HW_DISABLED) return; //Abort!
	flushPackets(); //Flush all packets!
	memset(&Mouse.data,0,sizeof(Mouse.data)); //Reset the mouse!
	Mouse.samplerate = 100; //100 packets/second!
	update_mouseTimer(); //Update the timer!

	//No data reporting!
	Mouse.resolution = 0x02; //4 pixel/mm resolution!
	update_mouseresolution(); //Update it!
	
	Mouse.command = 0xFF; //Reset!
	Mouse.timeout = (DOUBLE)0; //Start timing for our message!
}

OPTINLINE void mouse_handleinvalidcall()
{
	if (__HW_DISABLED) return; //Abort!
	if (!Mouse.last_was_error) //Last wasn't an error?
	{
		give_mouse_output(0xFE); //NACK!
		input_lastwrite_mouse(); //Give byte to the user!		
	}
	else //Error!
	{
		give_mouse_output(0xFC); //Error!
		input_lastwrite_mouse(); //Give byte to the user!
	}
	Mouse.last_was_error = 1; //Last was an error!
}

OPTINLINE void give_mouse_status() //Gives the mouse status buffer!
{
	if (__HW_DISABLED) return; //Abort!
	byte buttonstatus = (
				((Mouse.buttonstatus&1)<<2)| //Left button!
				((Mouse.buttonstatus&4)>>1)| //Middle button!
				((Mouse.buttonstatus&2)>>1) //Right button!
				); //Button status!
	give_mouse_output(
				(
				((Mouse.mode==2)?0x40:0)| //Remote/stream mode?
				(Mouse.data_reporting?0x20:0)| //Data reporting?
				(Mouse.scaling21?0x10:0)| //Scaling 2:1?
				buttonstatus //Apply left-middle-right bits!
				)
				); //Give the info!
	give_mouse_output(Mouse.resolution); //2nd byte is the resolution!
	give_mouse_output(Mouse.samplerate); //3rd byte is the sample rate!
}

OPTINLINE void loadMouseDefaults() //Load the Mouse Defaults!
{
	Mouse.data_reporting = 0;
	update_mouseTimer(); //Update the timer!
	Mouse.resolution = 2; //4 Pixels/mm!
	update_mouseresolution(); //update it!
	Mouse.scaling21 = 0; //Set the default scaling to 1:1 scaling!
}

OPTINLINE void commandwritten_mouse() //Command has been written to the mouse?
{
	if (__HW_DISABLED) return; //Abort!
	Mouse.has_command = 1; //We have a command!
	Mouse.command_step = 0; //Reset command step!
	//Handle mouse!

	switch (Mouse.command) //What command?
	{
		case 0xFF: //Reset?
			Mouse.timeout = MOUSE_DEFAULTTIMEOUT; //A small delay for the result code to appear(needed by the AT BIOS)!
			break;
		case 0xFE: //Resend?
			Mouse.has_command = 0; //We're not a command anymore!
			resend_lastpacket(); //Resend the last (if possible) or current packet.
			input_lastwrite_mouse(); //Give byte to the user!
			Mouse.last_was_error = 0; //Last is OK!
			break; //Not used?
		case 0xF6: //Set defaults!
			Mouse.mode = 0; //Reset mode!
			loadMouseDefaults(); //Load our defaults!
			Mouse.has_command = 0; //We're not a command anymore!
			give_mouse_output(0xFA); //Acnowledge!
			input_lastwrite_mouse(); //Give byte to the user!
			Mouse.last_was_error = 0; //Last is OK!
			break;
		case 0xF5: //Disable data reporting?
			Mouse.has_command = 0; //We're not a command anymore!
			Mouse.data_reporting = 0; //Disable data reporting!
			give_mouse_output(0xFA); //Acnowledge!
			input_lastwrite_mouse(); //Give byte to the user!
			Mouse.last_was_error = 0; //Last is OK!
			break;
		case 0xF4: //Enable data reporting?
			Mouse.has_command = 0; //We're not a command anymore!
			Mouse.data_reporting = 1; //Enable data reporting!
			give_mouse_output(0xFA); //Acnowledge!
			input_lastwrite_mouse(); //Give byte to the user!
			Mouse.last_was_error = 0; //Last is OK!
			break;
		case 0xF3: //Set sample rate?
			give_mouse_output(0xFA); //Acnowledge!
			input_lastwrite_mouse(); //Give byte to the user!
			//We're expecting parameters!
			Mouse.last_was_error = 0; //Last is OK!
			break;
		case 0xF2: //Get device ID?
			Mouse.has_command = 0; //We're not a command anymore!
			give_mouse_output(0xFA); //Acnowledge!
			input_lastwrite_mouse(); //Give byte to the user!
			give_mouse_output(0x00); //Standard mouse!
			Mouse.last_was_error = 0; //Last is OK!
			break;
		case 0xF0: //Set Remote Mode?
			Mouse.has_command = 0; //We're not a command anymore!
			Mouse.data_reporting = 0; //Disable data reporting!
			Mouse.mode = 2; //Remote mode
			give_mouse_output(0xFA); //Acnowledge!
			input_lastwrite_mouse(); //Give byte to the user!
			flushPackets(); //Flush our packets!
			Mouse.last_was_error = 0; //Last is OK!
			break;
		case 0xEE: //Set Wrap Mode?
			Mouse.has_command = 0; //We're not a command anymore!
			Mouse.lastmode = Mouse.mode; //Save the last mode!
			Mouse.mode = 1; //Enter wrap mode!
			Mouse.data_reporting = 0; //Disable data reporting!
			give_mouse_output(0xFA); //Acnowledge!
			input_lastwrite_mouse(); //Give byte to the user!
			flushPackets(); //Flush our packets!
			Mouse.last_was_error = 0; //Last is OK!
			break;
		case 0xEC: //Reset Wrap Mode?
			Mouse.has_command = 0; //We're not a command anymore!
			Mouse.mode = Mouse.lastmode; //Restore the last mode we were in!
			give_mouse_output(0xFA); //Acnowledge!
			input_lastwrite_mouse(); //Give byte to the user!
			flushPackets(); //Flush our packets!
			Mouse.last_was_error = 0; //Last is OK!			
			break;
		case 0xEB: //Read data?
			Mouse.has_command = 0; //We're not a command anymore!
			if (Mouse.packets) //Do we have a packet?
			{
				give_mouse_output(0xFA); //OK!
				input_lastwrite_mouse(); //Give byte to the user!
				Mouse.pollRemote = 1; //Poll a packet!
				Mouse.packetindex = 0; //Restart the packet, if we're currently processing it(restart polling)!
				Mouse.last_was_error = 0; //Last is OK!
			}
			else
			{
				give_mouse_output(0xFE); //OK!
				input_lastwrite_mouse(); //Give byte to the user!
				Mouse.last_was_error = 1; //Last is error!
			}
			break;
		case 0xEA: //Set stream mode?
			Mouse.has_command = 0; //We're not a command anymore!
			Mouse.data_reporting = 1; //Enable data reporting!
			Mouse.mode = 0; //Set stream mode!
			give_mouse_output(0xFA); //Acnowledge!
			input_lastwrite_mouse(); //Give byte to the user!
			flushPackets(); //Flush our packets!
			Mouse.last_was_error = 0; //Last is OK!
			break;
		case 0xE9: //Status request?
			Mouse.has_command = 0; //We're not a command anymore!
			give_mouse_output(0xFA); //Acnowledge!
			input_lastwrite_mouse(); //Give byte to the user!
			give_mouse_status(); //Give the status!
			Mouse.last_was_error = 0; //Last is OK!
			break;
		case 0xE8: //Set resolution?
			give_mouse_output(0xFA); //Acnowledge!
			Mouse.last_was_error = 0; //Last is OK!
			break;
		case 0xE7: //Set Scaling 2:1?
			Mouse.has_command = 0; //We're not a command anymore!
			Mouse.scaling21 = 1; //Set it!
			give_mouse_output(0xFA); //Acnowledge!
			input_lastwrite_mouse(); //Give byte to the user!
			Mouse.last_was_error = 0; //Last is OK!
			break;
		case 0xE6: //Set Scaling 1:1?
			Mouse.has_command = 0; //We're not a command anymore!
			Mouse.scaling21 = 0; //Set it!
			give_mouse_output(0xFA); //Acnowledge!
			input_lastwrite_mouse(); //Give byte to the user!
			Mouse.last_was_error = 0; //Last is OK!
			break;
		default:
			mouse_handleinvalidcall(); //Give an error!
			Mouse.has_command = 0; //We don't have a command anymore: we ignore the mouse?
			break;
		
	}

	if (Mouse.has_command) //Still a command?
	{
		++Mouse.command_step; //Next step (step starts at 1 always)!
	}
}

OPTINLINE byte mouse_is_command(byte data) //Command has been written to the mouse?
{
	if (__HW_DISABLED) return 1; //Abort!
	//Handle mouse!

	switch (data) //What command?
	{
		case 0xFF: //Reset?
		case 0xFE: //Resend?
		case 0xF6: //Set defaults!
		case 0xF5: //Disable data reporting?
		case 0xF4: //Enable data reporting?
		case 0xF3: //Set sample rate?
		case 0xF2: //Get device ID?
		case 0xF0: //Set Remote Mode?
		case 0xEE: //Set Wrap Mode?
		case 0xEC: //Reset Wrap Mode?
		case 0xEB: //Read data?
		case 0xEA: //Set stream mode?
		case 0xE9: //Status request?
		case 0xE8: //Set resolution?
		case 0xE7: //Set Scaling 2:1?
		case 0xE6: //Set Scaling 1:1?
			return 1;
			break;
		default:
			return 0;
			break;
		
	}
	return 0;
}

OPTINLINE void datawritten_mouse(byte data) //Data has been written to the mouse?
{
	if (__HW_DISABLED) return; //Abort!
	switch (Mouse.command) //What command?
	{
		case 0xF3: //Set sample rate?
			switch (data) //What rate?
			{
			case 10:
			case 20:
			case 40:
			case 60:
			case 80:
			case 100:
			case 200: //Valid?
				Mouse.samplerate = data; //Set the sample rate (in samples/second)!
				update_mouseTimer(); //Update the timer!
				break;
			default: //incorrect?
				give_mouse_output(0xFE); //Invalid!
				break;
			}
			give_mouse_output(0xFA); //Acnowledge!
			input_lastwrite_mouse(); //Give byte to the user!
			Mouse.has_command = 0; //We don't have a command anymore!
			Mouse.last_was_error = 0; //Last is OK!
			break;
		case 0xE8: //Set resolution?
			Mouse.resolution = (data&0x03); //Set the resolution!
			update_mouseresolution(); //Update it!
			give_mouse_output(0xFA); //Acnowledge!
			input_lastwrite_mouse(); //Give byte to the user!			
			Mouse.has_command = 0; //We don't have a command anymore!
			Mouse.last_was_error = 0; //Last is OK!
			break;
		default: //Invalid command?
			mouse_handleinvalidcall(); //Give an error!
			Mouse.has_command = 0; //We don't have a command anymore!
			break;
	}
	if (Mouse.has_command) //Still a command?
	{
		++Mouse.command_step; //Next step (step starts at 1 always)!
	}
}

void handle_mousewrite(byte data)
{
	if (__HW_DISABLED) return; //Abort!
	if ((!Mouse.has_command) || mouse_is_command(data)) //Not processing a command or issuing a new command?
	{
		Mouse.command = data; //Becomes a command!
		if (((Mouse.command != 0xEC) && (Mouse.command != 0xFF)) && (Mouse.mode == 1)) //Wrap mode?
		{
			give_mouse_output(Mouse.command); //Wrap mode!
			return; //Don't process as a command!
		}
		commandwritten_mouse(); //Process mouse command?
	}
	else //Data?
	{
		if (Mouse.mode == 1) //Wrap mode?
		{
			give_mouse_output(data); //Wrap mode!
			return; //Don't process as a command!
		}
		datawritten_mouse(data); //Data has been written!
	}
	if (!Mouse.has_command) //No command anymore?
	{
		Mouse.command_step = 0; //Reset command step!
	}
}

OPTINLINE int apply_scaling(int movement) //Apply scaling on a mouse packet x/ymove!
{
	if (__HW_DISABLED) return 0; //Abort!
	if (!Mouse.scaling21) return movement; //Unchanged!
	if (Mouse.pollRemote) return movement; //Don't affect polling mode!
	switch (movement)
	{
		case 0:	return 0; //No movement!
		case 1: return 1; //1!
		case -1: return -1; //-1!
		case 2: return 1; //1!
		case -2: return -1; //-1!
		case 3: return 3; //3!
		case -3: return -3; //-3!
		case 4: return 6; //6!
		case -4: return -6; //-6!
		case 5: return 9; //9!
		case -5: return -9; //-9!
		default:
			return 2*movement; //2*Movement counter!
	}	
}

OPTINLINE int applypacketmovement(int movement)
{
	if (__HW_DISABLED) return 0; //Abort!
	return apply_scaling(movement); //Apply resolution (in mm), then scaling!
}

OPTINLINE byte processMousePacket(MOUSE_PACKET *packet, byte index)
{
	if (__HW_DISABLED) return 0; //Abort!
	if (!packet) return 0; //Nothing to process!
	word xmove, ymove;
	byte xoverflow, yoverflow;
	//First process all movement info!
	int packetmovementx = applypacketmovement(packet->xmove); //Apply x movement!
	int packetmovementy = applypacketmovement(packet->ymove); //Apply y movement!
	packetmovementy = -packetmovementy; //Y is reversed for some unknown reason!
	
	xoverflow = ((packetmovementx < -0x100) || (packetmovementx > 0xFF)); //X overflow?
	yoverflow = ((packetmovementy < -0x100) || (packetmovementy > 0xFF)); //Y overflow?
	xmove = signed2unsigned16(MAX(MIN(packetmovementx, 0xFF), -0x100)); //Limit!
	ymove = signed2unsigned16(MAX(MIN(packetmovementy, 0xFF), -0x100)); //Limit!

	switch (index) //What index?
	{
		case 0:
			return (
				(yoverflow?0x80:0)| //Y overflow?
				(xoverflow?0x40:0)| //X overflow?
				((ymove&0x100)?0x20:0)| //Y negative?
				((xmove&0x100)?0x10:0)| //X negative?
				0x08| //Always 1!
				((packet->buttons&4)?0x04:0)| //Middle button?
				((packet->buttons&2)?0x02:0)| //Right button?
				((packet->buttons&1)?0x01:0) //Left button?
				); //Give the packet byte!
			break;
		case 1:
			return (xmove&0xFF); //X movement, lower 8 bits!
			break;
		case 2:
			return (ymove&0xFF); //X movement, lower 8 bits!
			break;
		default: //Unknown index?
			break;
	}
	return 0; //Nothing to process!
}

byte handle_mouseread() //Read from the mouse!
{
	if (__HW_DISABLED) return 0; //Abort!
	byte result;
	if (readfifobuffer(Mouse.buffer,&result)) //Try to read?
	{
		return result; //Read successful!
	}
	else if (Mouse.packets) //Gotten a packet?
	{
		if (Mouse.mode == 1) return 0; //Wrap(echo) mode?
		if ((Mouse.mode == 2) && (Mouse.pollRemote==0)) return 0; //Nothing there: we're requiring a poll!
		result = processMousePacket(Mouse.packets,Mouse.packetindex); //Process it!
		++Mouse.packetindex; //Next index!
		if (Mouse.packetindex>2) //Over our limit?
		{
			Mouse.pollRemote = 0; //Stop polling if we're in Remote mode.
			Mouse.packetindex = 0; //Reset packet index!
			next_mousepacket(); //Next mouse packet!
		}
		return result; //Give the result!
	}
	else //Nothing to read?
	{
		return 0x00; //NULL!
	}
}

int handle_mousepeek(byte *result) //Peek at the mouse!
{
	if (__HW_DISABLED) return 0; //Abort!
	if (peekfifobuffer(Mouse.buffer,result)) //Peek at the buffer!
	{
		return 1; //Buffered!
	}
	else if (Mouse.packets) //Gotten a packet?
	{
		if (Mouse.mode == 1) return 0; //Wrap(echo) mode?
		if ((Mouse.mode == 2) && (Mouse.pollRemote == 0)) return 0; //Nothing there: we're requiring a poll!
		*result = processMousePacket(Mouse.packets,Mouse.packetindex); //Process it!
		return 1; //Read!
	}
	return 0; //No packet!
}

void EMU_enablemouse(byte enabled) //Enable mouse input (disable during EMU, enable during CPU emulation (not paused))?
{
	if (__HW_DISABLED) return; //Abort!
	Mouse.disabled = !enabled; //Are we enabled?
}

void handle_mouseenabled(byte flags)
{
	return; //Don't do anything when the mouse port is enabled or disabled!
	if (flags & 0x80) //We're disabled?
	{
		resetPS2Mouse(0); //Reset the mouse!
		update_mouseTimer(); //Disable the timer if required!
	}
	else //We're enabled?
	{
		resetPS2Mouse(1); //Reset the mouse!
		loadMouseDefaults(); //Load the default settings into the mouse!
		update_mouseTimer(); //Enable the timer if required!
	}
}

void PS2_initMouse(byte enabled) //Initialise the mouse to reset mode?
{
	if (__HW_DISABLED) return; //Abort!
	memset(&Mouse.data, 0, sizeof(Mouse.data)); //Clear the mouse information!
	Mouse.supported = enabled; //Are we enabled!
	if (Mouse.supported) //Are we enabled?
	{
		Controller8042.RAM[0] |= 0x22; //Disable the mouse by default!
		//Register ourselves!
		register_PS2PortWrite(1, &handle_mousewrite); //Write functionnality!
		register_PS2PortRead(1, &handle_mouseread, &handle_mousepeek); //Read functionality!
		register_PS2PortEnabled(1, &handle_mouseenabled); //Enabled functionality!

		Mouse.buffer = allocfifobuffer(16,1); //Allocate a small mouse buffer!

		initPS2Mouse(); //Reset the mouse to power-on defaults!

		Mouse.samplerate = 100; //100 packets/second!
		update_mouseTimer(); //(Re)set mouse timer!
		Mouse.disabled = 1; //Default: disabled!
		update_mouseresolution(); //update the resolution!
	}
}

void BIOS_doneMouse()
{
	if (__HW_DISABLED) return; //Abort!
	free_fifobuffer(&Mouse.buffer); //Free the keyboard buffer!
	flushPackets(); //Flush all mouse packets!
}
