/*

Copyright (C) 2019 - 2021 Superfury

This file is part of The Common Emulator Framework.

The Common Emulator Framework is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

The Common Emulator Framework is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with The Common Emulator Framework.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "headers/types.h"
#include "headers/emu/input.h" //Own typedefs.
#include "headers/bios/bios.h" //BIOS Settings for keyboard input!
#include "headers/support/zalloc.h" //Zero allocation support for mouse packets!
#include "headers/emu/gpu/gpu_emu.h" //GPU emulator color support!
#include "headers/emu/gpu/gpu_sdl.h" //GPU SDL support!
#include "headers/emu/gpu/gpu_text.h" //GPU text layer support!
#include "headers/support/log.h" //Logging support!
#include "headers/support/highrestimer.h" //High resolution timer support!
#ifdef UNIPCEMU
#include "headers/hardware/sermouse.h" //Serial mouse support!
#endif
#include "headers/support/signedness.h" //Signedness support!
#ifdef UNIPCEMU
#include "headers/bios/biosmenu.h" //BIOS menu support for recording audio!
#include "headers/hardware/vga/vga.h" //Video adapter dumping support!
#endif
#include "headers/emu/timers.h" //Keyboard swap timer!
#ifdef UNIPCEMU
#include "headers/hardware/joystick.h" //Joystick support!
#endif
#include "headers/emu/threads.h" //BIOS Thread support!
#include "headers/support/locks.h" //Locking support!
#include "headers/hardware/ps2_keyboard.h" //Key I/O support!
#include "headers/support/keyboard.h" //Keyboard I/O support!
#include "headers/emu/sound.h" //Sound support for connect/disconnect support!
#ifdef VISUALC
#include "sdl_joystick.h" //Joystick support!
#include "sdl_events.h" //Event support!
#else
#ifdef ANDROID
#include "SDL_joystick.h" //Joystick support!
#include "SDL_events.h" //Event support!
#else
#if defined(__MINGW64__) || defined(__MINGW32__)
#include <SDL_joystick.h> //Joystick support!
#include <SDL_events.h> //Event support!
#else
#ifdef IS_LINUX
#include <SDL_joystick.h> //Joystick support!
#include <SDL_events.h> //Event support!
#else
#ifdef IS_VITA
#include <SDL2/SDL_joystick.h> //Joystick support!
#include <SDL2/SDL_events.h> //Event support!
#else
#ifdef IS_SWITCH
#ifdef SDL2
#include <SDL2/SDL_joystick.h> //Joystick support!
#include <SDL2/SDL_events.h> //Event support!
#else
#include <SDL/SDL_joystick.h> //Joystick support!
#include <SDL/SDL_events.h> //Event support!
#endif
#else
#include <SDL/SDL_joystick.h> //Joystick support!
#include <SDL/SDL_events.h> //Event support!
#endif
#endif
#endif
#endif
#endif
#endif

#ifndef SDL2
#define SDL_SCANCODE_UNKNOWN ~0
#endif

//Log input and output to compare?
//#define __DEBUG_INPUT

extern float GPU_xDTM, GPU_yDTM;
extern float GPU_xDTmickey, GPU_yDTmickey;

byte backgroundpolicy = 0; //Background task policy. 0=Full halt of the application, 1=Keep running without video and audio muted, 2=Keep running with audio playback, recording muted, 3=Keep running fully without video.

DOUBLE keyboard_mouseinterval = 0.0; //Check mouse 30 times per second during mouse mode!
DOUBLE keyboard_mousemovementspeed = 0.0; //How much to move the mouse in scale with full speed each interval!

float mouse_xmove = 0, mouse_ymove = 0; //Movement of the mouse not processed yet (in mm)!
float mouse_xmovemickey = 0, mouse_ymovemickey = 0; //Movement of the mouse not processed yet (in mm)!
byte Mouse_buttons = 0; //Currently pressed mouse buttons. 1=Left, 2=Right, 4=Middle.
byte Mouse_buttons2 = 0; //Second mouse button input!

byte mousebuttons = 0; //Active mouse buttons!

//Use direct input in windows!
byte Direct_Input = 0; //Direct input enabled?

extern byte EMU_RUNNING; //Are we running?
extern byte request_render; //Requesting for rendering once?

byte keysactive; //Ammount of keys active!

DOUBLE mouse_interval = 0.0f; //Check never by default (unused timer)!

byte precisemousemovement = 0; //Precise mouse movement enabled?

byte Settings_request = 0; //Requesting settings to be loaded?

word firstsplitx=(GPU_TEXTSURFACE_WIDTH/3)+1; //End of the first horizontal area(left button becomes middle button)!
word secondsplitx=((GPU_TEXTSURFACE_WIDTH/3)*2)+1; //End of the second horizontal area(middle button becomes right button)!
word thirdsplity=(GPU_TEXTSURFACE_HEIGHT/3)+1; //End of the first vertical area(middle button becomes no button)!

//#ifdef SDL2
extern SDL_Window *sdlWindow; //Our Window!
//#endif

#if !defined(IS_VITA) && !defined(IS_SWITCH)
//PSP map
enum input_button_map { //All buttons we support!
INPUT_BUTTON_TRIANGLE, INPUT_BUTTON_CIRCLE, INPUT_BUTTON_CROSS, INPUT_BUTTON_SQUARE,
INPUT_BUTTON_LTRIGGER, INPUT_BUTTON_RTRIGGER,
INPUT_BUTTON_DOWN, INPUT_BUTTON_LEFT, INPUT_BUTTON_UP, INPUT_BUTTON_RIGHT,
INPUT_BUTTON_SELECT, INPUT_BUTTON_START, INPUT_BUTTON_HOME, INPUT_BUTTON_HOLD };
#else
#ifndef IS_SWITCH
//PS Vita map!
enum input_button_map { //All buttons we support!
	INPUT_BUTTON_TRIANGLE, INPUT_BUTTON_CIRCLE, INPUT_BUTTON_CROSS, INPUT_BUTTON_SQUARE,
	INPUT_BUTTON_LTRIGGER, INPUT_BUTTON_RTRIGGER,
	INPUT_BUTTON_DOWN, INPUT_BUTTON_LEFT, INPUT_BUTTON_UP, INPUT_BUTTON_RIGHT,
	INPUT_BUTTON_SELECT, INPUT_BUTTON_START
};
#else
//Switch Mapping is as follows: A=0,B=1,X=2,Y=3,Lstick=4,Rstick=5,Lbumber=6,Rbumber=7,Ltrigger=8,Rtrigger=9,Plus=10,Minus=11,Left=12,Up=13,Right=14,Down=15
enum input_button_map {
	INPUT_BUTTON_CIRCLE=0, INPUT_BUTTON_CROSS=1, INPUT_BUTTON_TRIANGLE=2, INPUT_BUTTON_SQUARE=3,
	INPUT_BUTTON_LTRIGGER = 8, INPUT_BUTTON_RTRIGGER = 9,
	INPUT_BUTTON_START=10, INPUT_BUTTON_SELECT = 11,
	INPUT_BUTTON_LEFT=12, INPUT_BUTTON_UP=13, INPUT_BUTTON_RIGHT=14, INPUT_BUTTON_DOWN=15
	//Home&Hold unsupported by us!
};
#endif
#endif

//Remapping state!
byte remap_delete_active = 0;
byte remap_lwin_active = 0;
byte remap_ralt_active = 0;
byte remap_lalt_active = 0;
byte remap_tab_active = 0;

byte emu_keys_state[104]; //All states for all emulated keys!
//SDL 1.0 states!
#ifdef SDL2//#ifndef SDL2
int emu_keys_SDL[104] = {
#else
//SDL2 variant
uint_32 emu_keys_SDL[104] = {
#endif
	//column 1
	SDLK_a, //A
	SDLK_b, //B
	SDLK_c, //C
	SDLK_d,//D
	SDLK_e, //E
	SDLK_f, //F
	SDLK_g, //G
	SDLK_h, //H
	SDLK_i, //I
	SDLK_j, //J
	SDLK_k, //K
	SDLK_l, //L
	SDLK_m, //M
	SDLK_n, //N
	SDLK_o, //O
	SDLK_p, //P
	SDLK_q, //Q
	SDLK_r, //R
	SDLK_s, //S
	SDLK_t, //T
	SDLK_u, //U
	SDLK_v, //V
	SDLK_w, //W
	SDLK_x, //X
	SDLK_y, //Y
	SDLK_z, //Z

	SDLK_0, //0
	SDLK_1, //1
	SDLK_2, //2
	SDLK_3, //3
	SDLK_4, //4
	SDLK_5, //5
	SDLK_6, //6
	SDLK_7, //7
	SDLK_8, //8
	SDLK_9, //9

	//Column 2! (above '9' included)
	SDLK_BACKQUOTE, //`
	SDLK_MINUS, //-
	SDLK_EQUALS, //=
	SDLK_BACKSLASH, //BACKSLASH

	SDLK_BACKSPACE, //BKSP
	SDLK_SPACE, //SPACE
	SDLK_TAB, //TAB
	SDLK_CAPSLOCK, //CAPS

	SDLK_LSHIFT, //L SHFT
	SDLK_LCTRL, //L CTRL
#ifdef SDL2//#ifndef SDL2
	SDLK_LSUPER, //L WIN
#else
	SDLK_LGUI, //L WIN
#endif
	SDLK_LALT, //L ALT
	SDLK_RSHIFT, //R SHFT
	SDLK_RCTRL, //R CTRL
#ifdef SDL2//#ifndef SDL2
	SDLK_RSUPER, //R WIN
#else
	SDLK_RGUI, //R WIN
#endif
	SDLK_RALT, //R ALT

#ifndef SDL2//#ifdef SDL2
	SDLK_APPLICATION, //APPS
#else
	SDLK_MENU, //APPS
#endif
	SDLK_RETURN, //ENTER
	SDLK_ESCAPE, //ESC

	SDLK_F1, //F1
	SDLK_F2, //F2
	SDLK_F3, //F3
	SDLK_F4, //F4
	SDLK_F5, //F5
	SDLK_F6, //F6
	SDLK_F7, //F7
	SDLK_F8, //F8
	SDLK_F9, //F9
	SDLK_F10, //F10
	SDLK_F11, //F11
	SDLK_F12, //F12

	SDLK_SYSREQ, //PRNT SCRN

#ifndef SDL2 //#ifdef SDL2
	SDLK_SCROLLLOCK, //SCROLL
#else
	SDLK_SCROLLOCK, //SCROLL
#endif
	SDLK_PAUSE, //PAUSE

			 //Column 3!
	SDLK_LEFTBRACKET, //[

	SDLK_INSERT, //INSERT
	SDLK_HOME, //HOME
	SDLK_PAGEUP, //PG UP
	SDLK_DELETE, //DELETE
	SDLK_END, //END
	SDLK_PAGEDOWN, //PG DN
	SDLK_UP, //U ARROW
	SDLK_LEFT, //L ARROW
	SDLK_DOWN, //D ARROW
	SDLK_RIGHT, //R ARROW

#ifndef SDL2//#ifdef SDL2
	SDLK_NUMLOCKCLEAR, //NUM
#else
	SDLK_NUMLOCK, //NUM
#endif
	SDLK_KP_DIVIDE, //KP /
	SDLK_KP_MULTIPLY, //KP *
	SDLK_KP_MINUS, //KP -
	SDLK_KP_PLUS, //KP +
	SDLK_KP_ENTER, //KP EN
	SDLK_KP_PERIOD, //KP .

#ifndef SDL2//#ifdef SDL2
	SDLK_KP_0, //KP 0
	SDLK_KP_1, //KP 1
	SDLK_KP_2, //KP 2
	SDLK_KP_3, //KP 3
	SDLK_KP_4, //KP 4
	SDLK_KP_5, //KP 5
	SDLK_KP_6, //KP 6
	SDLK_KP_7, //KP 7
	SDLK_KP_8, //KP 8
	SDLK_KP_9, //KP 9
#else
	SDLK_KP0, //KP 0
	SDLK_KP1, //KP 1
	SDLK_KP2, //KP 2
	SDLK_KP3, //KP 3
	SDLK_KP4, //KP 4
	SDLK_KP5, //KP 5
	SDLK_KP6, //KP 6
	SDLK_KP7, //KP 7
	SDLK_KP8, //KP 8
	SDLK_KP9, //KP 9
#endif

	SDLK_RIGHTBRACKET, //]
	SDLK_SEMICOLON, //;
	SDLK_QUOTE, //'
	SDLK_COMMA, //,
	SDLK_PERIOD, //.
	SDLK_SLASH  ///
};

#ifdef SDL2//#ifndef SDL2
int emu_keys_sdl_rev[UINT16_MAX+1]; //Reverse of emu_keys_sdl!
#else
//SDL 2.0 variant, without lookup table, but as fast as possible!
int emu_keys_sdl_rev(uint_32 key)
{
	switch (key) //What key?
	{
	case SDLK_a: return 0x00; //A
	case SDLK_b: return 0x01; //B
	case SDLK_c: return 0x02; //C
	case SDLK_d: return 0x03; //D
	case SDLK_e: return 0x04; //E
	case SDLK_f: return 0x05; //F
	case SDLK_g: return 0x06; //G
	case SDLK_h: return 0x07; //H
	case SDLK_i: return 0x08; //I
	case SDLK_j: return 0x09; //J
	case SDLK_k: return 0x0A; //K
	case SDLK_l: return 0x0B; //L
	case SDLK_m: return 0x0C; //M
	case SDLK_n: return 0x0D; //N
	case SDLK_o: return 0x0E; //O
	case SDLK_p: return 0x0F; //P
	case SDLK_q: return 0x10; //Q
	case SDLK_r: return 0x11; //R
	case SDLK_s: return 0x12; //S
	case SDLK_t: return 0x13; //T
	case SDLK_u: return 0x14; //U
	case SDLK_v: return 0x15; //V
	case SDLK_w: return 0x16; //W
	case SDLK_x: return 0x17; //X
	case SDLK_y: return 0x18; //Y
	case SDLK_z: return 0x19; //Z

	case SDLK_0: return 0x1A; //0
	case SDLK_1: return 0x1B; //1
	case SDLK_2: return 0x1C; //2
	case SDLK_3: return 0x1D; //3
	case SDLK_4: return 0x1E; //4
	case SDLK_5: return 0x1F; //5
	case SDLK_6: return 0x20; //6
	case SDLK_7: return 0x21; //7
	case SDLK_8: return 0x22; //8
	case SDLK_9: return 0x23; //9

	//Column 2! (above '9' included)
	case SDLK_BACKQUOTE: return 0x24; //`
	case SDLK_MINUS: return 0x25; //-
	case SDLK_EQUALS: return 0x26; //=
	case SDLK_BACKSLASH: return 0x27; //BACKSLASH

	case SDLK_BACKSPACE: return 0x28; //BKSP
	case SDLK_SPACE: return 0x29; //SPACE
	case SDLK_TAB: return 0x2A; //TAB
	case SDLK_CAPSLOCK: return 0x2B; //CAPS

	case SDLK_LSHIFT: return 0x2C; //L SHFT
	case SDLK_LCTRL: return 0x2D; //L CTRL
	case SDLK_LGUI: return 0x2E; //L WIN
	case SDLK_LALT: return 0x2F; //L ALT
	case SDLK_RSHIFT: return 0x30; //R SHFT
	case SDLK_RCTRL: return 0x31; //R CTRL
	case SDLK_RGUI: return 0x32; //R WIN
	case SDLK_RALT: return 0x33; //R ALT

	case SDLK_APPLICATION: return 0x34; //APPS
	case SDLK_RETURN: return 0x35; //ENTER
	case SDLK_ESCAPE: return 0x36; //ESC

	case SDLK_F1: return 0x37; //F1
	case SDLK_F2: return 0x38; //F2
	case SDLK_F3: return 0x39; //F3
	case SDLK_F4: return 0x3A; //F4
	case SDLK_F5: return 0x3B; //F5
	case SDLK_F6: return 0x3C; //F6
	case SDLK_F7: return 0x3D; //F7
	case SDLK_F8: return 0x3E; //F8
	case SDLK_F9: return 0x3F; //F9
	case SDLK_F10: return 0x40; //F10
	case SDLK_F11: return 0x41; //F11
	case SDLK_F12: return 0x42; //F12

	case SDLK_SYSREQ: return 0x43; //PRNT SCRN

	case SDLK_SCROLLLOCK: return 0x44; //SCROLL
	case SDLK_PAUSE: return 0x45; //PAUSE

	//Column 3!
	case SDLK_LEFTBRACKET: return 0x46; //[

	case SDLK_INSERT: return 0x47; //INSERT
	case SDLK_HOME: return 0x48; //HOME
	case SDLK_PAGEUP: return 0x49; //PG UP
	case SDLK_DELETE: return 0x4A; //DELETE
	case SDLK_END: return 0x4B; //END
	case SDLK_PAGEDOWN: return 0x4C; //PG DN
	case SDLK_UP: return 0x4D; //U ARROW
	case SDLK_LEFT: return 0x4E; //L ARROW
	case SDLK_DOWN: return 0x4F; //D ARROW
	case SDLK_RIGHT: return 0x50; //R ARROW

	case SDLK_NUMLOCKCLEAR: return 0x51; //NUM
	case SDLK_KP_DIVIDE: return 0x52; //KP /
	case SDLK_KP_MULTIPLY: return 0x53; //KP *
	case SDLK_KP_MINUS: return 0x54; //KP -
	case SDLK_KP_PLUS: return 0x55; //KP +
	case SDLK_KP_ENTER: return 0x56; //KP EN
	case SDLK_KP_PERIOD: return 0x57; //KP .

	case SDLK_KP_0: return 0x58; //KP 0
	case SDLK_KP_1: return 0x59; //KP 1
	case SDLK_KP_2: return 0x5A; //KP 2
	case SDLK_KP_3: return 0x5B; //KP 3
	case SDLK_KP_4: return 0x5C; //KP 4
	case SDLK_KP_5: return 0x5D; //KP 5
	case SDLK_KP_6: return 0x5E; //KP 6
	case SDLK_KP_7: return 0x5F; //KP 7
	case SDLK_KP_8: return 0x60; //KP 8
	case SDLK_KP_9: return 0x61; //KP 9

	case SDLK_RIGHTBRACKET: return 0x62; //]
	case SDLK_SEMICOLON: return 0x63; //;
	case SDLK_QUOTE: return 0x64; //'
	case SDLK_COMMA: return 0x65; //,
	case SDLK_PERIOD: return 0x66; //.
	case SDLK_SLASH: return 0x67;  ///
	default: //Unknown key?
		return -1; //Unknown key!
		break;
	}
	return -1; //Unknown key!
}
#endif

//Location of the finger OSK and it's rows on the screen!
#define FINGEROSK_ROW0 0
#define FINGEROSK_ROW1 3
#define FINGEROSK_ROW2 6
#define FINGEROSK_ROW3 9
#define FINGEROSK_ROW4 12
#define FINGEROSK_ROW5 15

#define FINGEROSK_NUMROW0 18
#define FINGEROSK_NUMROW1 21
#define FINGEROSK_NUMROW2 24
#define FINGEROSK_NUMROW3 27
#define FINGEROSK_NUMROW4 30

#define FINGEROSK_BASEX 0
#define FINGEROSK_BASEY (GPU_TEXTSURFACE_HEIGHT-(FINGEROSK_NUMROW4+1)-1)

//This moves the keyboard right by n characters.
#define FINGEROSK_KEYBOARDLEFT 18

//For the list of scancodes used, see the table http://www.computer-engineering.org/ps2keyboard/scancodes1.html for the original columns this is based on.

typedef struct
{
	char facetext[20]; //Text of the key!
	word x; //Key OSK x position
	word y; //Key OSK y position
	byte pressed; //Are we pressed on the OSK?
} EMU_KEYINFO;

typedef struct
{
	word xsize; //Horizontal size detected!
	word ysize; //Vertical size detected!
} EMU_KEYSIZE; //The detected size of an OSK key!

EMU_KEYSIZE OSKsize[104]; //The size of all OSK keys!
EMU_KEYINFO OSKinfo[104] = {
	{"A",FINGEROSK_KEYBOARDLEFT + 5,FINGEROSK_ROW3,0}, //A
	{"B",FINGEROSK_KEYBOARDLEFT + 14,FINGEROSK_ROW4,0}, //B
	{"C",FINGEROSK_KEYBOARDLEFT + 10,FINGEROSK_ROW4,0}, //C
	{"D",FINGEROSK_KEYBOARDLEFT + 9,FINGEROSK_ROW3,0}, //D
	{"E",FINGEROSK_KEYBOARDLEFT + 8,FINGEROSK_ROW2,0}, //E
	{"F",FINGEROSK_KEYBOARDLEFT + 11,FINGEROSK_ROW3,0}, //F
	{"G",FINGEROSK_KEYBOARDLEFT + 13,FINGEROSK_ROW3,0}, //G
	{"H",FINGEROSK_KEYBOARDLEFT + 15,FINGEROSK_ROW3,0}, //H
	{"I",FINGEROSK_KEYBOARDLEFT + 18,FINGEROSK_ROW2,0}, //I
	{"J",FINGEROSK_KEYBOARDLEFT + 17,FINGEROSK_ROW3,0}, //J
	{"K",FINGEROSK_KEYBOARDLEFT + 19,FINGEROSK_ROW3,0}, //K
	{"L",FINGEROSK_KEYBOARDLEFT + 21,FINGEROSK_ROW3,0}, //L
	{"M",FINGEROSK_KEYBOARDLEFT + 18,FINGEROSK_ROW4,0}, //M
	{"N",FINGEROSK_KEYBOARDLEFT + 16,FINGEROSK_ROW4,0}, //N
	{"O",FINGEROSK_KEYBOARDLEFT + 20,FINGEROSK_ROW2,0}, //O
	{"P",FINGEROSK_KEYBOARDLEFT + 22,FINGEROSK_ROW2,0}, //P
	{"Q",FINGEROSK_KEYBOARDLEFT + 4,FINGEROSK_ROW2,0}, //Q
	{"R",FINGEROSK_KEYBOARDLEFT + 10,FINGEROSK_ROW2,0}, //R
	{"S",FINGEROSK_KEYBOARDLEFT + 7,FINGEROSK_ROW3,0}, //S
	{"T",FINGEROSK_KEYBOARDLEFT + 12,FINGEROSK_ROW2,0}, //T
	{"U",FINGEROSK_KEYBOARDLEFT + 16,FINGEROSK_ROW2,0}, //U
	{"V",FINGEROSK_KEYBOARDLEFT + 12,FINGEROSK_ROW4,0}, //V
	{"W",FINGEROSK_KEYBOARDLEFT + 6,FINGEROSK_ROW2,0}, //W
	{"X",FINGEROSK_KEYBOARDLEFT + 8,FINGEROSK_ROW4,0}, //X
	{"Y",FINGEROSK_KEYBOARDLEFT + 14,FINGEROSK_ROW2,0}, //Y
	{"Z",FINGEROSK_KEYBOARDLEFT + 6,FINGEROSK_ROW4,0}, //Z

	{")\n\t0",FINGEROSK_KEYBOARDLEFT + 20,FINGEROSK_ROW1,0}, //0
	{"!\n\t1",FINGEROSK_KEYBOARDLEFT + 2,FINGEROSK_ROW1,0}, //1
	{"@\n\t2",FINGEROSK_KEYBOARDLEFT + 4,FINGEROSK_ROW1,0}, //2
	{"#\n\t3",FINGEROSK_KEYBOARDLEFT + 6,FINGEROSK_ROW1,0}, //3
	{"$\n\t4",FINGEROSK_KEYBOARDLEFT + 8,FINGEROSK_ROW1,0}, //4
	{"%\n\t5",FINGEROSK_KEYBOARDLEFT + 10,FINGEROSK_ROW1,0}, //5
	{"^\n\t6",FINGEROSK_KEYBOARDLEFT + 12,FINGEROSK_ROW1,0}, //6
	{"&\n\t7",FINGEROSK_KEYBOARDLEFT + 14,FINGEROSK_ROW1,0}, //7
	{"*\n\t8",FINGEROSK_KEYBOARDLEFT + 16,FINGEROSK_ROW1,0}, //8
	{"(\n\t9",FINGEROSK_KEYBOARDLEFT + 18,FINGEROSK_ROW1,0}, //9

	//Column 2! (above '9' included)
	{"~\n\t`",FINGEROSK_KEYBOARDLEFT + 0,FINGEROSK_ROW1,0}, //`
	{"_\n\t-",FINGEROSK_KEYBOARDLEFT + 22,FINGEROSK_ROW1,0}, //-
	{"+\n\t=",FINGEROSK_KEYBOARDLEFT + 24,FINGEROSK_ROW1,0}, //=
	{"|\n\t\\",FINGEROSK_KEYBOARDLEFT + 28,FINGEROSK_ROW2,0}, //BACKSLASH

	{"Bksp\n\t\x1B",FINGEROSK_KEYBOARDLEFT + 28,FINGEROSK_ROW1,0}, //BKSP
	{"Space",FINGEROSK_KEYBOARDLEFT + 11,FINGEROSK_ROW5,0}, //SPACE
	{"Tab",FINGEROSK_KEYBOARDLEFT + 0,FINGEROSK_ROW2,0}, //TAB
	{"Caps\n\tLock",FINGEROSK_KEYBOARDLEFT + 0,FINGEROSK_ROW3,0}, //CAPS

	{"Shift",FINGEROSK_KEYBOARDLEFT + 0,FINGEROSK_ROW4,0}, //L SHFT
	{"Ctrl",FINGEROSK_KEYBOARDLEFT + 0,FINGEROSK_ROW5,0}, //L CTRL
	{"\xC3\xB7",FINGEROSK_KEYBOARDLEFT + 5,FINGEROSK_ROW5,0}, //L WIN
	{"Alt",FINGEROSK_KEYBOARDLEFT + 7,FINGEROSK_ROW5,0}, //L ALT
	{"Shift",FINGEROSK_KEYBOARDLEFT + 28,FINGEROSK_ROW4,0}, //R SHFT
	{"Ctrl",FINGEROSK_KEYBOARDLEFT + 28,FINGEROSK_ROW5,0}, //R CTRL
	{"\xC3\xB7",FINGEROSK_KEYBOARDLEFT + 24,FINGEROSK_ROW5,0}, //R WIN
	{"Alt Gr",FINGEROSK_KEYBOARDLEFT + 17,FINGEROSK_ROW5,0}, //R ALT

	{"\xC3\xB0",FINGEROSK_KEYBOARDLEFT + 26,FINGEROSK_ROW5,0}, //APPS
	{"Enter",FINGEROSK_KEYBOARDLEFT + 28,FINGEROSK_ROW3,0}, //ENTER
	{"Esc",FINGEROSK_KEYBOARDLEFT + 0,0,0}, //ESC

	{"F1",FINGEROSK_KEYBOARDLEFT + 4,FINGEROSK_ROW0,0}, //F1
	{"F2",FINGEROSK_KEYBOARDLEFT + 7,FINGEROSK_ROW0,0}, //F2
	{"F3",FINGEROSK_KEYBOARDLEFT + 10,FINGEROSK_ROW0,0}, //F3
	{"F4",FINGEROSK_KEYBOARDLEFT + 13,FINGEROSK_ROW0,0}, //F4
	{"F5",FINGEROSK_KEYBOARDLEFT + 16,FINGEROSK_ROW0,0}, //F5
	{"F6",FINGEROSK_KEYBOARDLEFT + 19,FINGEROSK_ROW0,0}, //F6
	{"F7",FINGEROSK_KEYBOARDLEFT + 22,FINGEROSK_ROW0,0}, //F7
	{"F8",FINGEROSK_KEYBOARDLEFT + 25,FINGEROSK_ROW0,0}, //F8
	{"F9",FINGEROSK_KEYBOARDLEFT + 28,FINGEROSK_ROW0,0}, //F9
	{"F10",FINGEROSK_KEYBOARDLEFT + 31,FINGEROSK_ROW0,0}, //F10
	{"F11",FINGEROSK_KEYBOARDLEFT + 35,FINGEROSK_ROW0,0}, //F11
	{"F12",FINGEROSK_KEYBOARDLEFT + 39,FINGEROSK_ROW0,0}, //F12

	{"PrtScn\n\tSysRq",24,FINGEROSK_NUMROW0,0}, //PRNT SCRN

	{"Scroll\n\tLock",31,FINGEROSK_NUMROW0,0}, //SCROLL
	{"Pause\n\tBreak",38,FINGEROSK_NUMROW0,0}, //PAUSE

	//Column 3!
	{"{\n\t[",FINGEROSK_KEYBOARDLEFT + 24,FINGEROSK_ROW2,0}, //[

	{"Insert",24,FINGEROSK_NUMROW1,0}, //INSERT
	{"Home",24,FINGEROSK_NUMROW2,0}, //HOME
	{"Page\n\tUp",24,FINGEROSK_NUMROW3,0}, //PG UP
	{"Delete",31,FINGEROSK_NUMROW1,0}, //DELETE
	{"End",31,FINGEROSK_NUMROW2,0}, //END
	{"Page\n\tDown",31,FINGEROSK_NUMROW3,0}, //PG DN
	{"\x18",FINGEROSK_KEYBOARDLEFT + 35,FINGEROSK_ROW4,0}, //U ARROW
	{"\x1B",FINGEROSK_KEYBOARDLEFT + 33,FINGEROSK_ROW5,0}, //L ARROW
	{"\x19",FINGEROSK_KEYBOARDLEFT + 35,FINGEROSK_ROW5,0}, //D ARROW
	{"\x1A",FINGEROSK_KEYBOARDLEFT + 37,FINGEROSK_ROW5,0}, //R ARROW

	{"Num\n\tLock",0,FINGEROSK_NUMROW0,0}, //NUM
	{"/",5,FINGEROSK_NUMROW0,0}, //KP /
	{"*",11,FINGEROSK_NUMROW0,0}, //KP *
	{"-",16,FINGEROSK_NUMROW0,0}, //KP -
	{"+",16,FINGEROSK_NUMROW1,0}, //KP +
	{"Enter",16,FINGEROSK_NUMROW4,0}, //KP EN
	{".\n\tDel",11,FINGEROSK_NUMROW4,0}, //KP .

	{"0\n\tIns",0,FINGEROSK_NUMROW4,0}, //KP 0
	{"1\n\tEnd",0,FINGEROSK_NUMROW3,0}, //KP 1
	{"2\n\t\x19",5,FINGEROSK_NUMROW3,0}, //KP 2
	{"3\n\tPgDn",11,FINGEROSK_NUMROW3,0}, //KP 3
	{"4\n\t\x1B",0,FINGEROSK_NUMROW2,0}, //KP 4
	{"5",5,FINGEROSK_NUMROW2,0}, //KP 5
	{"6\n\t\x1A",11,FINGEROSK_NUMROW2,0}, //KP 6
	{"7\n\tHome",0,FINGEROSK_NUMROW1,0}, //KP 7
	{"8\n\t\x18",5,FINGEROSK_NUMROW1,0}, //KP 8
	{"9\n\tPgUp",11,FINGEROSK_NUMROW1,0}, //KP 9

	{"}\n\t]",FINGEROSK_KEYBOARDLEFT + 26,FINGEROSK_ROW2,0}, //]
	{":\n\t;",FINGEROSK_KEYBOARDLEFT + 23,FINGEROSK_ROW3,0}, //;
	{"\"\n\t'",FINGEROSK_KEYBOARDLEFT + 25,FINGEROSK_ROW3,0}, //'
	{"<\n\t,",FINGEROSK_KEYBOARDLEFT + 20,FINGEROSK_ROW4,0}, //,
	{">\n\t.",FINGEROSK_KEYBOARDLEFT + 22,FINGEROSK_ROW4,0}, //.
	{"?\n\t/",FINGEROSK_KEYBOARDLEFT + 24,FINGEROSK_ROW4,0}  ///
}; //All keys to be used with the OSK!

//Are we disabled?
#define __HW_DISABLED 0

//Time between keyboard set swapping.
#define KEYSWAP_DELAY 100000
//Keyboard enabled?
#define KEYBOARD_ENABLED 1
//Allow input?
#define ALLOW_INPUT 1

//Default mode: 0=Mouse, 1=Keyboard
#define DEFAULT_KEYBOARD 0

extern GPU_type GPU; //The real GPU, for rendering the keyboard!

byte input_enabled = 0; //To show the input dialog?
byte input_buffer_enabled = 0; //To buffer, instead of straight into emulation (giving the below value the key index)?
byte input_buffer_shift = 0; //Ctrl-Shift-Alt Status for the pressed key!
byte input_buffer_mouse = 0; //Mouse input buffer!
sword input_buffer = -1; //To contain the pressed key!

byte oldshiftstatus = 0, oldMouse_buttons = 0; //Old shift status, used for keyboard/gaming mode!
byte currentmouse = 0; //Calculate current mouse status!
byte shiftstatus = 0; //New shift status!
byte currentshiftstatus_inputbuffer = 0; //Current input buffer shift status latch!
sword last_input_key = -1; //The last key to have been released (or pressed if unavailable)!

sword lastkey = 0, lastx = 0, lasty = 0, lastset = 0;
byte lastshift = 0; //Previous key that was pressed!


PSP_INPUTSTATE curstat; //Current status!

#define CAS_LCTRL 0x01
#define CAS_RCTRL 0x02
#define CAS_CTRL 0x03
#define CAS_LALT 0x04
#define CAS_RALT 0x08
#define CAS_ALT 0x0C
#define CAS_LSHIFT 0x10
#define CAS_RSHIFT 0x20
#define CAS_SHIFT 0x30

RAW_INPUTSTATUS input;

int psp_inputkey() //Simple key sampling!
{
	return input.Buttons; //Give buttons pressed!
}

int psp_inputkeydelay(uint_32 waittime) //Don't use within any timers! This will hang up itself!
{
	uint_32 counter; //Counter for inputkeydelay!
	int key = psp_inputkey(); //Check for keys!
	if (key) //Key pressed?
	{
		counter = waittime; //Init counter!
		while ((int_64)counter>0) //Still waiting?
		{
			if (counter>(uint_32)INPUTKEYDELAYSTEP) //Big block?
			{
				unlock(LOCK_INPUT);
				delay(INPUTKEYDELAYSTEP); //Wait a delay step!
				lock(LOCK_INPUT);
				counter -= INPUTKEYDELAYSTEP; //Substract!
			}
			else
			{
				unlock(LOCK_INPUT);
				delay(counter); //Wait rest!
				lock(LOCK_INPUT);
				counter = 0; //Done!
			}
		}
		if (waittime==0) //No waiting time?
		{
			unlock(LOCK_INPUT);
			delay(0); //Allow for updating of the keys!
			lock(LOCK_INPUT);
		}
	}
	else
	{
		unlock(LOCK_INPUT);
		delay(0); //Allow for updating of the keys!
		lock(LOCK_INPUT);
	}
	return key; //Give the key!
}

int psp_readkey() //Wait for keypress and release!
{
	int result;
	result = psp_inputkeydelay(0); //Check for input!
	while (result==0) //No input?
	{
		result = psp_inputkeydelay(0); //Check for input!
	}
	return result; //Give the pressed key!
}

int psp_keypressed(int key) //Key pressed?
{
	return ((psp_inputkey()&key)==key); //Key(s) pressed?
}

extern BIOS_Settings_TYPE BIOS_Settings; //Our BIOS Settings!

void get_analog_state(PSP_INPUTSTATE *state) //Get the current state for mouse/analog driver!
{
	//Clear all we set!
	state->analogdirection_mouse_x = 0; //No mouse X movement!
	state->analogdirection_mouse_y = 0; //No mouse Y movement!
	state->buttonpress = 0; //No buttons pressed!
	state->analogdirection_keyboard_x = 0; //No keyboard X movement!
	state->analogdirection_keyboard_y = 0; //No keyboard Y movement!
	//We preserve the input mode and gaming mode flags, to be able to go back when using gaming mode!

	
	int x; //Analog x!
	int y; //Analog y!
	x = input.Lx; //Convert to signed!
	y = input.Ly; //Convert to signed!

	sword minrange;
	minrange = (((sword)(BIOS_Settings.input_settings.analog_minrange&0x7F)<<8)); //Minimum horizontal&vertical range, limited within range!
	float rangemult;
	if (minrange>=SHRT_MAX) minrange = 0; //Direct usage when the multiplier has 0 range()!
	rangemult = (1.0f/(SHRT_MAX-minrange))*SHRT_MAX; //For converting the range after applying range!
	
	//Now, apply analog_minrange!
	
	if (x>0) //High?
	{
		if (x<minrange) //Not enough?
		{
			x = 0; //Nothing!
		}
		else //Patch?
		{
			x -= minrange; //Patch!
		}
	}
	else if (x<0) //Low?
	{
		if (x>(0-minrange)) //Not enough?
		{
			x = 0; //Nothing!
		}
		else //Patch?
		{
			x += minrange; //Patch!
		}
	}

	if (y>0) //High?
	{
		if (y<minrange) //Not enough?
		{
			y = 0; //Nothing!
		}
		else //Patch?
		{
			y -= minrange; //Patch!
		}
	}
	else if (y<0) //Low?
	{
		if (y>(0-minrange)) //Not enough?
		{
			y = 0; //Nothing!
		}
		else //Patch?
		{
			y += minrange; //Patch!
		}
	}

	x = (int)(((float)x)*rangemult); //Apply the range conversion!
	y = (int)(((float)y)*rangemult); //Apply the range conversion!

	if (state->gamingmode) //Gaming mode?
	{
		state->analogdirection_mouse_x = x; //Mouse X movement!
		state->analogdirection_mouse_y = y; //Mouse Y movement! Only to use mouse movement without analog settings!

		state->analogdirection_keyboard_x = x; //Keyboard X movement!
		state->analogdirection_keyboard_y = y; //Keyboard Y movement! Use this with ANALOG buttons!
		//Patch keyboard to -1&+1
		if (state->analogdirection_keyboard_x>0) //Positive?
		{
			state->analogdirection_keyboard_x = 1; //Positive simple!
		}
		else if (state->analogdirection_keyboard_x<0) //Negative?
		{
			state->analogdirection_keyboard_x = -1; //Negative simple!
		}

		if (state->analogdirection_keyboard_y>0) //Positive?
		{
			state->analogdirection_keyboard_y = 1; //Positive simple!
		}
		else if (state->analogdirection_keyboard_y<0) //Negative?
		{
			state->analogdirection_keyboard_y = -1; //Negative simple!
		}

		if ((input.Buttons&BUTTON_TRIANGLE)>0) //Triangle?
		{
			state->buttonpress |= 2; //Triangle!
		}
		if ((input.Buttons&BUTTON_SQUARE)>0) //Square?
		{
			state->buttonpress |= 1; //Square!
		}
		if ((input.Buttons&BUTTON_CROSS)>0) //Cross?
		{
			state->buttonpress |= 8; //Cross!
		}
		if ((input.Buttons&BUTTON_CIRCLE)>0) //Circle?
		{
			state->buttonpress |= 4; //Circle!
		}

		if ((input.Buttons&BUTTON_LEFT)>0) //Left?
		{
			state->buttonpress |= 16; //Left!
		}

		if ((input.Buttons&BUTTON_UP)>0) //Up?
		{
			state->buttonpress |= 32; //Up!
		}

		if ((input.Buttons&BUTTON_RIGHT)>0) //Right?
		{
			state->buttonpress |= 64; //Right!
		}

		if ((input.Buttons&BUTTON_DOWN)>0) //Down?
		{
			state->buttonpress |= 128; //Down!
		}

		if ((input.Buttons&BUTTON_LTRIGGER)>0) //L?
		{
			state->buttonpress |= 256; //L!
		}

		if ((input.Buttons&BUTTON_RTRIGGER)>0) //R?
		{
			state->buttonpress |= 512; //R!
		}

		if ((input.Buttons&BUTTON_START)>0) //START?
		{
			state->buttonpress |= 1024; //START!
		}

		state->analogdirection2_x = input.Lx2; //X of second input!
		state->analogdirection2_y = input.Ly2; //Y of second input!
		state->analogdirection_rudder = input.LxRudder; //X of the rudder!
	}
	else //Normal mode?
	{
		state->analogdirection2_x = 0; //No X of second input!
		state->analogdirection2_y = 0; //No Y of second input!
		state->analogdirection_rudder = 0; //No X of the rudder!
		switch (state->mode) //Which input mode?
		{
		case 0: //Mouse?
			state->analogdirection_mouse_x = x; //Mouse X movement!
			state->analogdirection_mouse_y = y; //Mouse Y movement!
			//The face buttons are OR-ed!
			if ((input.Buttons&BUTTON_TRIANGLE)>0) //Triangle?
			{
				state->buttonpress |= 2; //Triangle!
			}
			else if ((input.Buttons&BUTTON_SQUARE)>0) //Square?
			{
				state->buttonpress |= 1; //Square!
			}
			else if ((input.Buttons&BUTTON_CROSS)>0) //Cross?
			{
				state->buttonpress |= 8; //Cross!
			}
			else if ((input.Buttons&BUTTON_CIRCLE)>0) //Circle?
			{
				state->buttonpress |= 4; //Circle!
			}
	
			if ((input.Buttons&BUTTON_LEFT)>0) //Left?
			{
				state->buttonpress |= 16; //Left!
			}
	
			if ((input.Buttons&BUTTON_UP)>0) //Up?
			{
				state->buttonpress |= 32; //Up!
			}
	
			if ((input.Buttons&BUTTON_RIGHT)>0) //Right?
			{
				state->buttonpress |= 64; //Right!
			}
	
			if ((input.Buttons&BUTTON_DOWN)>0) //Down?
			{
				state->buttonpress |= 128; //Down!
			}
			
			if ((input.Buttons&BUTTON_LTRIGGER)>0) //L?
			{
				state->buttonpress |= 256; //L!
			}
	
			if ((input.Buttons&BUTTON_RTRIGGER)>0) //R?
			{
				state->buttonpress |= 512; //R!
			}		
			
			if ((input.Buttons&BUTTON_START)) //START?
			{
				state->buttonpress |= 1024; //START!
			}
			break;
		case 1: //Keyboard; Same as mouse, but keyboard -1..+1!
			state->analogdirection_keyboard_x = x; //Keyboard X movement!
			state->analogdirection_keyboard_y = y; //Keyboard Y movement!
	
			//Patch keyboard to -1&+1
			if (state->analogdirection_keyboard_x>0) //Positive?
			{
				state->analogdirection_keyboard_x = 1; //Positive simple!
			}
			else if (state->analogdirection_keyboard_x<0) //Negative?
			{
				state->analogdirection_keyboard_x = -1; //Negative simple!
			}
	
			if (state->analogdirection_keyboard_y>0) //Positive?
			{
				state->analogdirection_keyboard_y = 1; //Positive simple!
			}
			else if (state->analogdirection_keyboard_y<0) //Negative?
			{
				state->analogdirection_keyboard_y = -1; //Negative simple!
			}
	
			//The face buttons are OR-ed!
			if ((input.Buttons&BUTTON_TRIANGLE)>0) //Triangle?
			{
				state->buttonpress |= 2; //Triangle!
			}
			else if ((input.Buttons&BUTTON_SQUARE)>0) //Square?
			{
				state->buttonpress |= 1; //Square!
			}
			else if ((input.Buttons&BUTTON_CROSS)>0) //Cross?
			{
				state->buttonpress |= 8; //Cross!
			}
			else if ((input.Buttons&BUTTON_CIRCLE)>0) //Circle?
			{
				state->buttonpress |= 4; //Circle!
			}
	
			if ((input.Buttons&BUTTON_LEFT)>0) //Left?
			{
				state->buttonpress |= 16; //Left!
			}
	
			if ((input.Buttons&BUTTON_UP)>0) //Up?
			{
				state->buttonpress |= 32; //Up!
			}
	
			if ((input.Buttons&BUTTON_RIGHT)>0) //Right?
			{
				state->buttonpress |= 64; //Right!
			}
	
			if ((input.Buttons&BUTTON_DOWN)>0) //Down?
			{
				state->buttonpress |= 128; //Down!
			}
	
			if ((input.Buttons&BUTTON_LTRIGGER)>0) //L?
			{
				state->buttonpress |= 256; //L!
			}
	
			if ((input.Buttons&BUTTON_RTRIGGER)>0) //R?
			{
				state->buttonpress |= 512; //R!
			}
			
			if ((input.Buttons&BUTTON_START)) //START?
			{
				state->buttonpress |= 1024; //START!
			}		
			break;
		default: //Unknown?
			//Can't handle unknown input devices!
			break;
		}
	}
}

int is_gamingmode()
{
	return curstat.gamingmode; //Are in gaming mode?
}

void mouse_handler() //Mouse handler at current packet speed (MAX 255 packets/second!)
{
#ifdef UNIPCEMU
	byte buttons;
	if ((!curstat.mode || curstat.gamingmode) || Direct_Input) //Mouse mode or gaming mode?
	{
		if (useMouseTimer() || useSERMouse()) //We're using the mouse?
		{
			//Fill the mouse data!
			//Mouse movement handling!
			buttons = Mouse_buttons | ((Mouse_buttons2&4)?0:Mouse_buttons2); //What buttons are active! Touch inputs are taken as long as the middle button isn't pressed on it!
			if (!PS2mouse_packet_handler(buttons, &mouse_xmove, &mouse_ymove, &mouse_xmovemickey, &mouse_ymovemickey)) //Add the mouse packet! Not supported PS/2 mouse?
			{
				SERmouse_packet_handler(buttons, &mouse_xmove, &mouse_ymove, &mouse_xmovemickey, &mouse_ymovemickey); //Send the mouse packet to the serial mouse!
			}
		}
		else //No timer? Discard movement!
		{
			lock(LOCK_INPUT);
			mouse_xmove = 0.0f; //None anymore!
			mouse_ymove = 0.0f; //None anymore!
			mouse_xmovemickey = 0.0f; //None anymore!
			mouse_ymovemickey = 0.0f; //None anymore!
			unlock(LOCK_INPUT);
		}
	}
#endif
}

//Rows: 3, one for top, middle, bottom.
#define KEYBOARD_NUMY 9
//Columns: 21: 10 for left, 1 space, 10 for right
#define KEYBOARD_NUMX 21

//Calculate the middle!
#define CALCMIDDLE(size,length) ((size/2)-(length/2))

//Right

//Down

GPU_TEXTSURFACE *keyboardsurface = NULL; //Framerate surface!

void initKeyboardOSK()
{
	keyboardsurface = alloc_GPUtext(); //Allocate GPU text surface for us to use!
	if (!keyboardsurface) //Couldn't allocate?
	{
		raiseError("GPU","Error allocating OSK layer!");
		return;
	}
	GPU_enableDelta(keyboardsurface, 1, 1); //Enable both x and y delta coordinates: we want to be at the bottom-right of the screen always!
	GPU_addTextSurface(keyboardsurface,&keyboard_renderer); //Register our renderer!
}

void doneKeyboardOSK()
{
	free_GPUtext(&keyboardsurface); //Release the framerate!
}

byte displaytokeyboard[5] = {0,3,1,4,2}; //1,2,3,4 (keyboard display)=>3,1,4,2 (getkeyboard)

//Keyboard layout: shift,set,sety,setx,itemnr,values
char active_keyboard[3][3][3][2][5][10]; //Active keyboard!

//US keyboard!
byte keyboard_active = 1; //What's the active keyboard (from below list)?
char keyboard_names[2][20] = {"US(default)","US(linear)"}; //Names of the different keyboards!

char keyboards[2][3][3][3][2][5][10] = { //X times 3 sets of 3x3 key choices, every choise has 4 choises (plus 1 at the start to determine enabled, "enable" for enabled, else disabled. Also, empty is no key assigned.). Order: left,right,up,down.
										{ //US(default)
											{ //Set 1!
												//enable, first, second, third, last
												{ //Row 1
													{{"enable","q","w","e","r"},{"enable","Q","W","E","R"}},
													{{"enable","t","y","u","i"},{"enable","T","Y","U","I"}},
													{{"enable","o","p","[","]"},{"enable","O","P","{","}"}}
												},
												{ //Row 2
													{{"enable","a","s","d","f"},{"enable","A","S","D","F"}},
													{{"enable","g","h","j","k"},{"enable","G","H","J","K"}},
													{{"enable","l",";","'","enter"},{"enable","L",":","\"","enter"}}
												},
												{ //Row 3
													{{"enable","z","x","c","v"},{"enable","Z","X","C","V"}},
													{{"enable","b","n","m",","},{"enable","B","N","M","<"}},
													{{"enable",".","/","\\","space"},{"enable",">","?","|","space"}}
												}
											},

											{ //Set 2!
												{ //Row 1
													{{"enable","1","2","3","4"},{"enable","!","@","#","$"}},
													{{"enable","5","6","7","8"},{"enable","%","^","&","*"}},
													{{"enable","9","0","-","="},{"enable","(",")","_","+"}}
												},
												{ //Row 2
													{{"enable","f1","f2","f3","f4"},{"enable","f1","f2","f3","f4"}},
													{{"enable","f5","f6","f7","f8"},{"enable","f5","f6","f7","f8"}},
													{{"enable","f9","f10","f11","f12"},{"enable","f9","f10","f11","f12"}}
												},
												{ //Row 3
													{{"enable","home","end","pgup","pgdn"},{"enable","home","end","pgup","pgdn"}},
													{{"enable","left","right","up","down"},{"enable","left","right","up","down"}},
													{{"enable","esc","del","enter","bksp"},{"enable","esc","del","enter","bksp"}}
												}
											},

											{ //Set 3!
												{ //Row 1
													{{"enable","kp0","kp1","kp2","kp3"},{"enable","kpins","kpend","kpdown","kppgdn"}},
													{{"enable","kp4","kp5","kp6","kp7"},{"enable","kpleft","kp5","kpright","kphome"}},
													{{"enable","kp8","kp9","kp.","kpen"},{"enable","kpup","kppgup","kpdel","kpen"}}
												},
												{ //Row 2
													{{"enable","num","capslock","scroll","tab"},{"enable","num","capslock","scroll","tab"}},
													{{"enable","prtsc","pause","insert","lwin"},{"enable","prtsc","pause","insert","lwin"}},
													{{"enable","num/","num*","num-","num+"},{"enable","num/","num*","num-","num+"}}
												},
												{ //Row 3
													{{"enable","","","CAPTURE","`"},{"enable","","","CAPTURE","~"}},
													{{"enable","rctrl","ralt","rshift","rwin"},{"enable","rctrl","ralt","rshift","rwin"}},
													{{"","","","",""},{"","","","",""}}
												} //Not used.
											}
										},
										{ //US(linear)
											{ //Set 1!
												//enable, first, last, second, third
												{ //Row 1
													{{"enable","q","r","w","e"},{"enable","Q","R","W","E"}},
													{{"enable","t","i","y","u"},{"enable","T","I","Y","U"}},
													{{"enable","o","]","p","["},{"enable","O","}","P","{"}}
												},
												{ //Row 2
													{{"enable","a","f","s","d"},{"enable","A","F","S","D"}},
													{{"enable","g","k","h","j"},{"enable","G","K","H","J"}},
													{{"enable","l","enter",";","'"},{"enable","L","enter",":","\""}}
												},
												{ //Row 3
													{{"enable","z","v","x","c"},{"enable","Z","V","X","C"}},
													{{"enable","b",",","n","m"},{"enable","B",",","N","M"}},
													{{"enable",".","space","/","\\"},{"enable",">","space","?","|"}}
												}
											},

											{ //Set 2!
												{ //Row 1
													{{"enable","1","4","2","3"},{"enable","!","$","@","#"}},
													{{"enable","5","8","6","7"},{"enable","%","*","^","&"}},
													{{"enable","9","=","0","-"},{"enable","(","+",")","_"}}
												},
												{ //Row 2
													{{"enable","f1","f4","f2","f3"},{"enable","f1","f4","f2","f3"}},
													{{"enable","f5","f8","f6","f7"},{"enable","f5","f8","f6","f7"}},
													{{"enable","f9","f12","f10","f11"},{"enable","f9","f12","f10","f11"}}
												},
												{ //Row 3
													{{"enable","home","end","pgup","pgdn"},{"enable","home","end","pgup","pgdn"}},
													{{"enable","left","right","up","down"},{"enable","left","right","up","down"}},
													{{"enable","esc","del","enter","bksp"},{"enable","esc","del","enter","bksp"}}
												}
											},

											{ //Set 3!
												{ //Row 1
													{{"enable","kp0","kp3","kp1","kp2"},{"enable","kpins","kppgdn","kpend","kpdown"}},
													{{"enable","kp4","kp7","kp5","kp6"},{"enable","kpleft","kphome","kp5","kpright"}},
													{{"enable","kp8","kpen","kp9","kp."},{"enable","kpup","kppgup","kpdel","kpen"}}
												},
												{ //Row 2
													{{"enable","num","capslock","scroll","tab"},{"enable","num","capslock","scroll","tab"}},
													{{"enable","prtsc","pause","insert","lwin"},{"enable","prtsc","pause","insert","lwin"}},
													{{"enable","num/","num+","num*","num-"},{"enable","num/","num+","num*","num-"}}
												},
												{ //Row 3
													{{"enable","","","CAPTURE","`"},{"enable","","","CAPTURE","~"}},
													{{"enable","rctrl","ralt","rshift","rwin"},{"enable","rctrl","ralt","rshift","rwin"}},
													{{"","","","",""},{"","","","",""}}
												} //Barely used input.
											}
										}										
									};
									
/*

Calling the keyboard_us structure:
enabled = keyboard_us[0][set][row][column][0]=="enable"
onscreentext = keyboard_us[have_shift][set][row][column]
key = keyboard_us[0][set][row][column][pspkey]
pspkey = 1=square,2=circle,3=triangle,4=cross
*/

int currentset = 0; //Active set?
int currentkey = 0; //Current key. 0=none, next 1=triangle, square, cross, circle?
byte currentshift = 0; //No shift (1=shift)
byte currentctrl = 0; //No ctrl (1=Ctrl)
byte currentalt = 0; //No alt (1=Alt)
int setx = 0; //X within set -1 to 1!
int sety = 0; //Y within set -1 to 1!

byte keyboard_attribute[KEYBOARD_NUMY][KEYBOARD_NUMX];

byte keyboard_special[KEYBOARD_NUMY][KEYBOARD_NUMX];
byte keyboard_display[KEYBOARD_NUMY][KEYBOARD_NUMX];

//Keyboard layout: shift,set,sety,setx,itemnr,values
//Shift,set,row,item=0=enable?;1+=left,right,up,down
#define getkeyboard(shift,set,sety,setx,itemnr) &active_keyboard[set][1+sety][1+setx][(shift)?1:0][itemnr][0]

#ifdef UNIPCEMU
extern PS2_KEYBOARD Keyboard; //Active keyboard!
#endif
extern byte SCREEN_CAPTURE; //Screen capture requested?

byte FINGEROSK = 0; //Finger OSK enabled?
byte Stickykeys = 0; //Use sticky keys?

extern byte lightpen_status; //Are we capturing lightpen motion and presses?
extern byte lightpen_pressed; //Lightpen pressed?

void fill_keyboarddisplay() //Fills the display for displaying on-screen!
{
	memset(&keyboard_display,0,sizeof(keyboard_display)); //Init keyboard display!
	memcpy(&active_keyboard,&keyboards[keyboard_active],sizeof(active_keyboard)); //Set the active keyboard to the defined keyboard!
	memset(&keyboard_attribute,0,sizeof(keyboard_attribute)); //Default attributes to font color!
	memset(&keyboard_special,0,sizeof(keyboard_special)); //Default attributes to font color!

	//LEDs on keyboard are always visible!
#ifdef UNIPCEMU	
	if (!curstat.gamingmode) //Not gaming mode (mouse, keyboard and direct mode)?
	{
		keyboard_display[KEYBOARD_NUMY-2][KEYBOARD_NUMX-3] = 'N'; //NumberLock!
		if (Keyboard.LEDS&2) //NUMLOCK?
		{
			keyboard_attribute[KEYBOARD_NUMY-2][KEYBOARD_NUMX-3] = 3; //Special shift color active!
		}
		else
		{
			keyboard_attribute[KEYBOARD_NUMY-2][KEYBOARD_NUMX-3] = 2; //Special shift color inactive!
		}

		keyboard_display[KEYBOARD_NUMY-2][KEYBOARD_NUMX-2] = 'C'; //CAPS LOCK!
		if (Keyboard.LEDS&4) //CAPSLOCK?
		{
			keyboard_attribute[KEYBOARD_NUMY-2][KEYBOARD_NUMX-2] = 3; //Special shift color active!
		}
		else
		{
			keyboard_attribute[KEYBOARD_NUMY-2][KEYBOARD_NUMX-2] = 2; //Special shift color inactive!
		}

		keyboard_display[KEYBOARD_NUMY-2][KEYBOARD_NUMX-1] = 'S'; //Scroll lock!
		if (Keyboard.LEDS&1) //SCROLLLOCK?
		{
			keyboard_attribute[KEYBOARD_NUMY-2][KEYBOARD_NUMX-1] = 3; //Special shift color active!
		}
		else
		{
			keyboard_attribute[KEYBOARD_NUMY-2][KEYBOARD_NUMX-1] = 2; //Special shift color inactive!
		}
	}
#endif

	#ifndef IS_PSP
	//Set button
	keyboard_display[KEYBOARD_NUMY - 9][KEYBOARD_NUMX - 3] = ' '; //OSK Input mode!
	keyboard_attribute[KEYBOARD_NUMY -9][KEYBOARD_NUMX - 3] = 2; //Special shift color inactive!
	keyboard_special[KEYBOARD_NUMY - 9][KEYBOARD_NUMX - 3] = 3;
	keyboard_display[KEYBOARD_NUMY - 8][KEYBOARD_NUMX - 3] = 'S'; //OSK Input mode!
	keyboard_attribute[KEYBOARD_NUMY - 8][KEYBOARD_NUMX - 3] = 2; //Special shift color inactive!
	keyboard_special[KEYBOARD_NUMY - 8][KEYBOARD_NUMX - 3] = 3;
	keyboard_display[KEYBOARD_NUMY - 9][KEYBOARD_NUMX - 2] = ' '; //OSK Input mode!
	keyboard_attribute[KEYBOARD_NUMY - 9][KEYBOARD_NUMX - 2] = 2; //Special shift color inactive!
	keyboard_special[KEYBOARD_NUMY - 9][KEYBOARD_NUMX - 2] = 3;
	keyboard_display[KEYBOARD_NUMY - 8][KEYBOARD_NUMX - 2] = 'e'; //OSK Input mode!
	keyboard_attribute[KEYBOARD_NUMY - 8][KEYBOARD_NUMX - 2] = 2; //Special shift color inactive!
	keyboard_special[KEYBOARD_NUMY - 8][KEYBOARD_NUMX - 2] = 3;
	keyboard_display[KEYBOARD_NUMY - 9][KEYBOARD_NUMX - 1] = ' '; //OSK Input mode!
	keyboard_attribute[KEYBOARD_NUMY - 9][KEYBOARD_NUMX - 1] = 2; //Special shift color inactive!
	keyboard_special[KEYBOARD_NUMY - 9][KEYBOARD_NUMX - 1] = 3;
	keyboard_display[KEYBOARD_NUMY - 8][KEYBOARD_NUMX - 1] = 't'; //OSK Input mode!
	keyboard_attribute[KEYBOARD_NUMY - 8][KEYBOARD_NUMX - 1] = 2; //Special shift color inactive!
	keyboard_special[KEYBOARD_NUMY - 8][KEYBOARD_NUMX - 1] = 3;

	//Sti button
	keyboard_display[KEYBOARD_NUMY - 7][KEYBOARD_NUMX - 3] = ' '; //OSK sticky Input mode!
	keyboard_attribute[KEYBOARD_NUMY - 7][KEYBOARD_NUMX - 3] = Stickykeys?3:2; //Special shift color inactive!
	keyboard_special[KEYBOARD_NUMY - 7][KEYBOARD_NUMX - 3] = 4;
	keyboard_display[KEYBOARD_NUMY - 6][KEYBOARD_NUMX - 3] = 'S'; //OSK sticky Input mode!
	keyboard_attribute[KEYBOARD_NUMY - 6][KEYBOARD_NUMX - 3] = Stickykeys?3:2; //Special shift color inactive!
	keyboard_special[KEYBOARD_NUMY - 6][KEYBOARD_NUMX - 3] = 4;
	keyboard_display[KEYBOARD_NUMY - 7][KEYBOARD_NUMX - 2] = ' '; //OSK sticky Input mode!
	keyboard_attribute[KEYBOARD_NUMY - 7][KEYBOARD_NUMX - 2] = Stickykeys?3:2; //Special shift color inactive!
	keyboard_special[KEYBOARD_NUMY - 7][KEYBOARD_NUMX - 2] = 4;
	keyboard_display[KEYBOARD_NUMY - 6][KEYBOARD_NUMX - 2] = 't'; //OSK sticky Input mode!
	keyboard_attribute[KEYBOARD_NUMY - 6][KEYBOARD_NUMX - 2] = Stickykeys?3:2; //Special shift color inactive!
	keyboard_special[KEYBOARD_NUMY - 6][KEYBOARD_NUMX - 2] = 4;
	keyboard_display[KEYBOARD_NUMY - 7][KEYBOARD_NUMX - 1] = ' '; //OSK sticky Input mode!
	keyboard_attribute[KEYBOARD_NUMY - 7][KEYBOARD_NUMX - 1] = Stickykeys?3:2; //Special shift color inactive!
	keyboard_special[KEYBOARD_NUMY - 7][KEYBOARD_NUMX - 1] = 4;
	keyboard_display[KEYBOARD_NUMY - 6][KEYBOARD_NUMX - 1] = 'i'; //OSK sticky Input mode!
	keyboard_attribute[KEYBOARD_NUMY - 6][KEYBOARD_NUMX - 1] = Stickykeys?3:2; //Special shift color inactive!
	keyboard_special[KEYBOARD_NUMY - 6][KEYBOARD_NUMX - 1] = 4;
	#endif

	//Cap button
	keyboard_display[KEYBOARD_NUMY - 5][KEYBOARD_NUMX - 3] = ' '; //Screen capture!
	keyboard_special[KEYBOARD_NUMY - 5][KEYBOARD_NUMX - 3] = 1;
	keyboard_display[KEYBOARD_NUMY - 4][KEYBOARD_NUMX - 3] = 'C'; //Screen capture!
	keyboard_special[KEYBOARD_NUMY - 4][KEYBOARD_NUMX - 3] = 1;
	keyboard_display[KEYBOARD_NUMY - 5][KEYBOARD_NUMX - 2] = ' '; //Screen capture!
	keyboard_special[KEYBOARD_NUMY - 5][KEYBOARD_NUMX - 2] = 1;
	keyboard_display[KEYBOARD_NUMY - 4][KEYBOARD_NUMX - 2] = 'a'; //Screen capture!
	keyboard_special[KEYBOARD_NUMY - 4][KEYBOARD_NUMX - 2] = 1;
	keyboard_display[KEYBOARD_NUMY - 5][KEYBOARD_NUMX - 1] = ' '; //Screen capture!
	keyboard_special[KEYBOARD_NUMY - 5][KEYBOARD_NUMX - 1] = 1;
	keyboard_display[KEYBOARD_NUMY - 4][KEYBOARD_NUMX - 1] = 'p'; //Screen capture!
	keyboard_special[KEYBOARD_NUMY - 4][KEYBOARD_NUMX - 1] = 1;

	if (SCREEN_CAPTURE) //Screen capture status?
	{
		keyboard_attribute[KEYBOARD_NUMY - 4][KEYBOARD_NUMX - 3] = 3; //Special shift color active!
		keyboard_attribute[KEYBOARD_NUMY - 4][KEYBOARD_NUMX - 2] = 3; //Special shift color active!
		keyboard_attribute[KEYBOARD_NUMY - 4][KEYBOARD_NUMX - 1] = 3; //Special shift color active!
	}
	else
	{
		keyboard_attribute[KEYBOARD_NUMY - 4][KEYBOARD_NUMX - 3] = 2; //Special shift color inactive!
		keyboard_attribute[KEYBOARD_NUMY - 4][KEYBOARD_NUMX - 2] = 2; //Special shift color inactive!
		keyboard_attribute[KEYBOARD_NUMY - 4][KEYBOARD_NUMX - 1] = 2; //Special shift color inactive!
	}

	if (!input_enabled) //Input disabled atm?
	{
		if (FINGEROSK) //Finger OSK enabled?
		{
			keyboard_display[KEYBOARD_NUMY - 1][KEYBOARD_NUMX - 1] = Direct_Input?'d':'O'; //OSK Input mode!
			keyboard_attribute[KEYBOARD_NUMY - 1][KEYBOARD_NUMX - 1] = 1; //Special shift color active!		
		}
		#ifndef IS_PSP
		else //No input enabled? Not used on the PSP(has no keyboard nor mouse capability to use it)!
		{
			keyboard_display[KEYBOARD_NUMY - 1][KEYBOARD_NUMX - 1] = Direct_Input?'d':'O'; //OSK Input mode!
			keyboard_attribute[KEYBOARD_NUMY - 1][KEYBOARD_NUMX - 1] = 2; //Special shift color inactive!		
		}
		#endif
		keyboard_special[KEYBOARD_NUMY - 1][KEYBOARD_NUMX - 1] = 2; //Place a toggle for the M/K/G/D input modes to toggle the OSK!
		keyboard_special[KEYBOARD_NUMY - 1][KEYBOARD_NUMX - 2] = 2; //Place a toggle for the M/K/G/D input modes to toggle the OSK!
		keyboard_special[KEYBOARD_NUMY - 1][KEYBOARD_NUMX - 3] = 2; //Place a toggle for the M/K/G/D input modes to toggle the OSK!
		keyboard_special[KEYBOARD_NUMY - 2][KEYBOARD_NUMX - 1] = 2; //Place a toggle for the M/K/G/D input modes to toggle the OSK!
		keyboard_special[KEYBOARD_NUMY - 2][KEYBOARD_NUMX - 2] = 2; //Place a toggle for the M/K/G/D input modes to toggle the OSK!
		keyboard_special[KEYBOARD_NUMY - 2][KEYBOARD_NUMX - 3] = 2; //Place a toggle for the M/K/G/D input modes to toggle the OSK!
		return; //Keyboard disabled: don't show!
	}

	if (!Direct_Input) //Not direct input?
	{
		if (precisemousemovement)
		{
			keyboard_display[KEYBOARD_NUMY-3][KEYBOARD_NUMX-4] = 'P';
			keyboard_attribute[KEYBOARD_NUMY-3][KEYBOARD_NUMX-4] = 3; //Special shift color active!
		}
		else
		{
			keyboard_display[KEYBOARD_NUMY-3][KEYBOARD_NUMX-4] = ' ';
			keyboard_attribute[KEYBOARD_NUMY-3][KEYBOARD_NUMX-4] = 0; //Special shift color active!
		}
		if ((curstat.mode == 1) && (!curstat.gamingmode)) //Keyboard mode?
		{
			if (strcmp((char *)getkeyboard(0, currentset, sety, setx, 0), "enable") == 0) //Set enabled?
			{
				char *left = getkeyboard(currentshift, currentset, sety, setx, 1);
				char *right = getkeyboard(currentshift, currentset, sety, setx, 2);
				char *up = getkeyboard(currentshift, currentset, sety, setx, 3);
				char *down = getkeyboard(currentshift, currentset, sety, setx, 4);

				byte leftloc = CALCMIDDLE(CALCMIDDLE(KEYBOARD_NUMX, 0), safe_strlen(left, 0)); //Left location!
				byte rightloc = CALCMIDDLE(KEYBOARD_NUMX, 0); //Start at the right half!
				rightloc += CALCMIDDLE(CALCMIDDLE(KEYBOARD_NUMX, 0), safe_strlen(right, 0)); //Right location!
				byte uploc = CALCMIDDLE(KEYBOARD_NUMX, safe_strlen(up, 0)); //Up location!
				byte downloc = CALCMIDDLE(KEYBOARD_NUMX, safe_strlen(down, 0)); //Down location!

				memcpy(&keyboard_display[0][uploc], up, safe_strlen(up, 0)); //Set up!
				if (currentkey == 1) //Got a key pressed?
				{
					memset(&keyboard_attribute[0][uploc], 1, safe_strlen(up, 0)); //Active key!
				}

				memcpy(&keyboard_display[1][leftloc], left, safe_strlen(left, 0)); //Set left!
				if (currentkey == 2) //Got a key pressed?
				{
					memset(&keyboard_attribute[1][leftloc], 1, safe_strlen(left, 0)); //Active key!
				}

				memcpy(&keyboard_display[1][rightloc], right, safe_strlen(right, 0)); //Set up!
				if (currentkey == 4) //Got a key pressed?
				{
					memset(&keyboard_attribute[1][rightloc], 1, safe_strlen(right, 0)); //Active key!
				}

				memcpy(&keyboard_display[2][downloc], down, safe_strlen(down, 0)); //Set up!
				if (currentkey == 3) //Got a key pressed?
				{
					memset(&keyboard_attribute[2][downloc], 1, safe_strlen(down, 0)); //Active key!
				}
			}
		} //Keyboard mode?

		if ((!curstat.mode) && (!curstat.gamingmode)) //Mouse mode?
		{
			keyboard_display[KEYBOARD_NUMY - 1][KEYBOARD_NUMX - 1] = 'M'; //Mouse mode!
			keyboard_attribute[KEYBOARD_NUMY - 1][KEYBOARD_NUMX - 1] = 2; //Special shift color inactive!
		}

		if (!curstat.gamingmode) //Not gaming mode?
		{
			keyboard_display[KEYBOARD_NUMY - 3][KEYBOARD_NUMX - 3] = 'C'; //Ctrl!
			if (currentctrl)
			{
				keyboard_attribute[KEYBOARD_NUMY - 3][KEYBOARD_NUMX - 3] = 3; //Special shift color active!
			}
			else
			{
				keyboard_attribute[KEYBOARD_NUMY - 3][KEYBOARD_NUMX - 3] = 2; //Special shift color inactive!
			}

			keyboard_display[KEYBOARD_NUMY - 3][KEYBOARD_NUMX - 2] = 'A'; //Alt!
			if (currentalt)
			{
				keyboard_attribute[KEYBOARD_NUMY - 3][KEYBOARD_NUMX - 2] = 3; //Special shift color active!
			}
			else
			{
				keyboard_attribute[KEYBOARD_NUMY - 3][KEYBOARD_NUMX - 2] = 2; //Special shift color inactive!
			}

			keyboard_display[KEYBOARD_NUMY - 3][KEYBOARD_NUMX - 1] = 'S'; //Shift!
			if (currentshift)
			{
				keyboard_attribute[KEYBOARD_NUMY - 3][KEYBOARD_NUMX - 1] = 3; //Special shift color active!
			}
			else
			{
				keyboard_attribute[KEYBOARD_NUMY - 3][KEYBOARD_NUMX - 1] = 2; //Special shift color inactive!
			}
			if (curstat.mode) //Keyboard mode?
			{
				keyboard_display[KEYBOARD_NUMY - 1][KEYBOARD_NUMX - 1] = 'K'; //Gaming mode!
				keyboard_attribute[KEYBOARD_NUMY - 1][KEYBOARD_NUMX - 1] = 2; //Special shift color inactive!
			}
		}
		else //Gaming mode?
		{
			keyboard_display[KEYBOARD_NUMY - 1][KEYBOARD_NUMX - 1] = 'G'; //Gaming mode!
			keyboard_attribute[KEYBOARD_NUMY - 1][KEYBOARD_NUMX - 1] = 2; //Special shift color inactive!
		}
	}
	else
	{
		keyboard_display[KEYBOARD_NUMY - 1][KEYBOARD_NUMX - 1] = 'D'; //Direct Input mode!
		keyboard_attribute[KEYBOARD_NUMY - 1][KEYBOARD_NUMX - 1] = 2; //Special shift color inactive!
	}

	if (((Mouse_buttons2 & 6) == 6) || (lightpen_status&1)) //Light pen mode?
	{
		keyboard_display[KEYBOARD_NUMY - 1][KEYBOARD_NUMX - 1] = 'L'; //Light pen mode!
		if (lightpen_pressed) //Pressed the button on the light pen?
		{
			keyboard_display[KEYBOARD_NUMY - 1][KEYBOARD_NUMX - 2] = 'P'; //Light pen pressed!
			keyboard_attribute[KEYBOARD_NUMY - 1][KEYBOARD_NUMX - 2] = 2; //Special shift color inactive!
		}
	}


	if (FINGEROSK) //Finger OSK enabled?
	{
		keyboard_attribute[KEYBOARD_NUMY - 1][KEYBOARD_NUMX - 1] = 1; //Special shift color active!		
	}
	keyboard_special[KEYBOARD_NUMY - 1][KEYBOARD_NUMX - 1] = 2; //Place a toggle for the M/K/G/D input modes to toggle the OSK!
	keyboard_special[KEYBOARD_NUMY - 1][KEYBOARD_NUMX - 2] = 2; //Place a toggle for the M/K/G/D input modes to toggle the OSK!
	keyboard_special[KEYBOARD_NUMY - 1][KEYBOARD_NUMX - 3] = 2; //Place a toggle for the M/K/G/D input modes to toggle the OSK!
	keyboard_special[KEYBOARD_NUMY - 2][KEYBOARD_NUMX - 1] = 2; //Place a toggle for the M/K/G/D input modes to toggle the OSK!
	keyboard_special[KEYBOARD_NUMY - 2][KEYBOARD_NUMX - 2] = 2; //Place a toggle for the M/K/G/D input modes to toggle the OSK!
	keyboard_special[KEYBOARD_NUMY - 2][KEYBOARD_NUMX - 3] = 2; //Place a toggle for the M/K/G/D input modes to toggle the OSK!
}

uint_32 keyboard_rendertime; //Time for framerate rendering!

OPTINLINE void fingerOSK_presskey(byte index)
{
	//A key has been pressed!
	SDL_Event inputevent;
	inputevent.key.type = SDL_KEYDOWN; //Our event type!
    #ifndef SDL2//#ifdef SDL2
	inputevent.key.repeat = 0; //Not repeated key!
	#endif
	inputevent.key.keysym.sym = emu_keys_SDL[index]; //The key that is to be handled!
    #ifndef SDL2//#ifdef SDL2
	inputevent.key.windowID = SDL_GetWindowID(sdlWindow); //Our Window ID!
	#endif
	inputevent.key.state = SDL_PRESSED;
	inputevent.key.keysym.scancode = SDL_SCANCODE_UNKNOWN; //Unknown scancode, ignore this!
	inputevent.key.keysym.mod = 0; //No modifiers for now, as they're still unsupported!
	unlock(LOCK_INPUT); //Make sure we're available to handle this!
	SDL_PushEvent(&inputevent); //Convert to a normal event and send it!
	lock(LOCK_INPUT); //We're reserved again!
}

OPTINLINE void fingerOSK_releasekey(byte index)
{
	//A key has been pressed!
	SDL_Event inputevent;
	inputevent.key.type = SDL_KEYUP; //Our event type!
    #ifndef SDL2//#ifdef SDL2
	inputevent.key.repeat = 0; //Not repeated key!
	#endif
	inputevent.key.keysym.sym = emu_keys_SDL[index]; //The key that is to be handled!
    #ifndef SDL2//#ifdef SDL2
	inputevent.key.windowID = SDL_GetWindowID(sdlWindow); //Our Window ID!
	#endif
	inputevent.key.keysym.scancode = SDL_SCANCODE_UNKNOWN; //Unknown scancode, ignore this!
	inputevent.key.keysym.mod = 0; //No modifiers for now, as they're still unsupported!
	unlock(LOCK_INPUT); //Make sure we're available to handle this!
	SDL_PushEvent(&inputevent); //Convert to a normal event and send it!
	lock(LOCK_INPUT); //We're reserved again!
}

byte fingerOSK_buttons[GPU_TEXTSURFACE_HEIGHT][GPU_TEXTSURFACE_WIDTH]; //All possible locations for buttons!

void updateFingerOSK_mouse()
{
	word x, y; //The position inside the key text!
	byte screencharacter;
	uint_32 screenfont, screenborder;
	
	byte buttonarea;
	byte newbutton;
	byte buttonstatus=0; //Button status for all two buttons!
	byte buttonpending=0; //Button pending release?
	static byte fingerOSK_status=0; //Current finger OSK button status!
	for (x = FINGEROSK_BASEX;x<GPU_TEXTSURFACE_WIDTH;++x) //Process all possible areas!
	{
		for (y = FINGEROSK_BASEY;y<GPU_TEXTSURFACE_HEIGHT;++y) //Process all possible areas!
		{
			buttonarea = newbutton = 0; //Default: not our area!
			if (GPU_textgetxy(keyboardsurface, x, y, &screencharacter, &screenfont, &screenborder)) //Read location?
			{
				if ((screencharacter == 0) || (screencharacter == ' ')) //Not filled or to be used?
				{
					buttonarea = (x<firstsplitx)?1:((x<secondsplitx)?((y<thirdsplity)?3:0):2); //Are we a button? 0=None, 1=Left, 2=Right, 3=Middle
					newbutton = 1; //We're a new button!
					if ((x>=(GPU_TEXTSURFACE_WIDTH-KEYBOARD_NUMX)) && (y>=(GPU_TEXTSURFACE_HEIGHT-KEYBOARD_NUMY))) //Might be a not to be used area?
					{
						byte buttoninfo;
						buttoninfo = keyboard_special[KEYBOARD_NUMY-(GPU_TEXTSURFACE_HEIGHT-y)][KEYBOARD_NUMX-(GPU_TEXTSURFACE_WIDTH-x)]; //Get info!
						if ((buttoninfo!=0) && (buttoninfo!=0xFF)) //Disallowed button?
						{
							buttonarea = newbutton = 0; //Disable the button functionality here!
						}
						else //Overlapping button on keyboard part? Register it with the keyboard!
						{
							keyboard_special[KEYBOARD_NUMY-(GPU_TEXTSURFACE_HEIGHT-y)][KEYBOARD_NUMX-(GPU_TEXTSURFACE_WIDTH-x)] = 0xFF; //Register OSK button!
						}
					}
				}

				if (buttonarea) //A valid area to use?
				{
					if (fingerOSK_buttons[y - FINGEROSK_BASEY][x - FINGEROSK_BASEX]==0 || newbutton) //Are we a new button?
					{
						if (GPU_textsetxyclickable(keyboardsurface,x,y,screencharacter,screenfont,screenborder,0)&SETXYCLICKED_CLICKED) //Are we clicked?
						{
							buttonpending |= (1<<(buttonarea-1)); //Pend the button as released!
						}
						if (GPU_ispressed(keyboardsurface,x,y)) //Are we pressed?
						{
							buttonstatus |= (1<<(buttonarea-1)); //We're pressed!
						}
					}
					fingerOSK_buttons[y - FINGEROSK_BASEY][x - FINGEROSK_BASEX] = buttonarea;
				}
				else
				{
					fingerOSK_buttons[y - FINGEROSK_BASEY][x - FINGEROSK_BASEX] = 0; //No area!
				}
			}
		}
	}
	//Now we have the current status(buttonstatus) and pending releases(buttonpending).
	fingerOSK_status |= buttonstatus; //Press the buttons that need pressing!
	fingerOSK_status &= ~buttonpending; //Release the buttons the need releasing!

	//Now fingerOSK_status contains the current finger status to use!
	if (buttonpending&1) //Left button pending?
	{
		Mouse_buttons2 &= ~1; //Not pressed anymore!
	}
	if (buttonpending&2) //Right button pending?
	{
		Mouse_buttons2 &= ~2; //Not pressed anymore!
	}
	if (buttonpending&4) //Middle button pending?
	{
		Mouse_buttons2 &= ~4; //Not pressed anymore!
	}
	if (buttonstatus&1) //Left pressed?
	{
		Mouse_buttons2 |= 1; //Pressed now!
	}
	if (buttonstatus&2) //Right pressed?
	{
		Mouse_buttons2 |= 2; //Pressed now!
	}
	if (buttonstatus&4) //Middle pressed?
	{
		Mouse_buttons2 |= 4; //Pressed now!
	}
}

OPTINLINE void setOSKfont(EMU_KEYINFO *currentkey, uint_32 font, uint_32 back)
{
	word screenx;
	word y; //The position inside the key text!
	byte textx; //X inside the text!
	word startx; //Backup of the screen x of the key!
	screenx = currentkey->x; //Screen x!
	y = currentkey->y; //Screen y!
	startx = screenx; //Save a copy of the beginning of the character!
	for (textx = 0;textx<safestrlen(currentkey->facetext,sizeof(currentkey->facetext));++textx) //Check all our key positions!
	{
		//Based on GPU_textprintf function.
		if (currentkey->facetext[textx] == '\t') //Return to start x?
		{
			screenx = startx; //Return to the specified position!
		}
		else if ((currentkey->facetext[textx] == '\r' && !USESLASHN) || (currentkey->facetext[textx] == '\n' && USESLASHN)) //LF? If use \n, \n uses linefeed too, else just newline.
		{
			screenx = 0; //Move to the left!
		}
		if (currentkey->facetext[textx] == '\n') //CR?
		{
			++y; //Next Y!
		}
		else if (currentkey->facetext[textx] != '\r') //Never display \r!
		{
			//Normal visible character?
			GPU_textsetxyfont(keyboardsurface, FINGEROSK_BASEX + screenx++, FINGEROSK_BASEY + y,font,back); //Set the font?
		}
	}
}

OPTINLINE byte isStickyKey(byte index)
{
	switch (emu_keys_SDL[index]) //Are we a sticky key?
	{
		case SDLK_LCTRL:
		case SDLK_LALT:
		case SDLK_LSHIFT:
        #ifdef SDL2//#ifndef SDL2
		case SDLK_LSUPER:
		#else
		case SDLK_LGUI:
		#endif
		case SDLK_RCTRL:
		case SDLK_RALT:
		case SDLK_RSHIFT:
        #ifdef SDL2//#ifndef SDL2
		case SDLK_RSUPER:
		#else
		case SDLK_RGUI:
		#endif
			return 1; //Sticky!
			break;
		default: //Not sticky?
			return 0;
	}
	return 0; //Default: not sticky!
}

void fingerOSK_OSK_presskey(EMU_KEYINFO *currentkey, byte index, uint_32 fontcolor, uint_32 bordercolor)
{
	//Handle stickykeys too?
	setOSKfont(currentkey,fontcolor,bordercolor); //Set us to the active color!
	if (currentkey->pressed==0) //Not already pressed?
	{
		fingerOSK_presskey(index); //Press normally!
	}
	if (Stickykeys) //Stickykeys active?
	{
		if (isStickyKey(index)) //Sticky and newly pressed, also not pressed again after releasing sticky?
		{
			currentkey->pressed = 3|(((currentkey->pressed&4)<<1)|(currentkey->pressed&4)); //We're pressed(sticky)! Set bit 4 again, when pressing for the second time!
		}
		else
		{
			currentkey->pressed = 1; //We're pressed!
		}
	}
	else //Normal press?
	{
		currentkey->pressed = 1; //We're pressed!
	}
}

void fingerOSK_OSK_releasekey(EMU_KEYINFO *currentkey, byte index, uint_32 fontcolor, uint_32 bordercolor, byte *releasestickykeys, byte releasingstickykeys)
{
	//Handle stickykeys too?
	if (Stickykeys) //Stickykeys active?
	{
		if (currentkey->pressed&2) //Sticky active?
		{
			if ((currentkey->pressed&4)==0) //Pressed and released for the first time?
			{
				currentkey->pressed |= 4; //Special flag: we're pressed and released!
				currentkey->pressed &= ~1; //Simulated off, but still sticky on!
			}
			else if (currentkey->pressed&8) //Pressed for the second time?
			{
				goto doublereleasekey; //Release, because we've been pressed for the second time, doing an untoggle!
			}
			if (releasingstickykeys==0) return; //Don't release when not releasing sticky keys, except when double releasing!
		}
	}
	doublereleasekey:
	currentkey->pressed = 0; //We're released!
	setOSKfont(currentkey, fontcolor, bordercolor); //Set us to the inactive color!
	fingerOSK_releasekey(index); //Press normally!
}

OPTINLINE void updateFingerOSK()
{
	static byte OSKdrawn = 0; //Are we drawn?
	byte key; //The key we're checking!
	EMU_KEYINFO *currentkey = &OSKinfo[0]; //The current key to check!
	EMU_KEYINFO emptykey;
	EMU_KEYSIZE *currentsize = &OSKsize[0]; //The current key size to check!
	word x, y; //The position inside the key text!
	byte textx; //X inside the text!
	word startx; //Backup of the screen x of the key!
	uint_32 fontcolor = getemucol16(BIOS_Settings.input_settings.fontcolor); //Font color!
	uint_32 activecolor = getemucol16(BIOS_Settings.input_settings.activecolor); //Font color!
	uint_32 bordercolor = getemucol16(BIOS_Settings.input_settings.bordercolor); //Border color!

	byte screencharacter; //Dummy!
	uint_32 screenfont, screenborder; //Dummy!

	word screenx;
	byte releasingstickykeys, pendingreleasestickykeys;
	byte OSK_ispressed=0; //Are we pressed?
	byte c; //Current character to display!
	byte is_UTF8codepoint; //Use UTF-8 code point?
	uint_32 UTF8codepoint=0; //UTF-8 code point!
	byte UTF8bytesleft; //How many bytes are left for UTF-8 parsing?
	if (FINGEROSK) //OSK enabled?
	{
		GPU_text_locksurface(keyboardsurface); //Lock us!
		if (OSKdrawn == 0) //Not drawn yet?
		{
			releasingstickykeys = (Stickykeys==0); //Default: not releasing any sticky keys now!
			startreleasestickykeys: //To jump back here when releasing!
			pendingreleasestickykeys = 0; //Pending to release sticky keys?
			currentkey = &OSKinfo[0]; //The first key to process
			currentsize = &OSKsize[0]; //The first key to process
			for (key = 0;key<NUMITEMS(OSKinfo);++key, ++currentkey, ++currentsize) //Check for all keys!
			{
				byte pressed = 0; //Are we pressed? Default to not pressed!

				if (currentsize->xsize && currentsize->ysize) //Gotten an area of effect? We might've been in use! Check for pressed key!
				{
					for (x=0;x<currentsize->xsize;++x) //Process the entire area!
					{
						for (y=0;y<currentsize->ysize;++y)
						{
							if (GPU_textgetxy(keyboardsurface, FINGEROSK_BASEX+currentkey->x+x, FINGEROSK_BASEY+currentkey->y+y, &screencharacter, &screenfont, &screenborder)) //Valid coordinate?
							{
								OSK_ispressed |= GPU_ispressed(keyboardsurface, FINGEROSK_BASEX+currentkey->x+x, FINGEROSK_BASEY+currentkey->y+y); //Are we pressed for the entire area?
							}
						}
					}
				}

				screenx = currentkey->x; //Screen x!
				y = currentkey->y; //Screen y!
				startx = screenx; //Save a copy of the beginning of the character!

				word detectedwidth = 0; //The width we have detected!
				word currentwidth = 0; //The current width we detect!
				word detectedrows = 0; //The number of detected rows!

				OSK_ispressed = 0; //Default: not pressed!

				UTF8bytesleft = 0; //Init!
				for (textx = 0;textx<safestrlen(currentkey->facetext,sizeof(currentkey->facetext));++textx) //Check all our key positions!
				{
					//Based on GPU_textprintf function.
					is_UTF8codepoint = 0; //Not UTF-8!
					c = currentkey->facetext[textx]; //Character to display!
					if (c&0x80) //UTF-8 support!
					{
						if ((c&0xC0)==0x80) //Continuation byte?
						{
							if (UTF8bytesleft) //Were we paring UTF-8?
							{
								--UTF8bytesleft; //One byte parsed!
								//6 bits added!
								UTF8codepoint <<= 6; //6 bits to add!
								UTF8codepoint |= (c&0x3F); //6 bits added!
								if (UTF8bytesleft) //Still more bytes left?
								{
									continue; //Next byte please!
								}
								else //Finished UTF-8 code point?
								{
									is_UTF8codepoint = 1; //UTF-8!
								}
							}
							else //Invalid UTF-8 string, abort it! Count as extended ASCII!
							{
								UTF8bytesleft = 0; //Abort!
							}
						}
						else if ((c&0xE0)==0xC0) //Two byte UTF-8 starting?
						{
							//5 bits for the first byte, 6 for the other bytes!
							UTF8codepoint = (c&0x1F); //5 bits to start with!
							UTF8bytesleft = 1; //1 byte left!
							continue; //Next byte please!
						}
						else if ((c&0xF0)==0xE0) //Three byte UTF-8 starting?
						{
							//4 bits for the first byte, 6 for the other bytes!
							UTF8codepoint = (c&0xF); //4 bits to start with!
							UTF8bytesleft = 2; //2 bytes left!
							continue; //Next byte please!
						}
						else if ((c&0xF8)==0xF0) //Four byte UTF-8 starting?
						{
							//3 bits for the first byte, 6 for the other bytes!
							UTF8codepoint = (c&0x7); //3 bits to start with!
							UTF8bytesleft = 3; //3 bytes left!
							continue; //Next byte please!
						}
						else //Non-UTF-8 encoded?
						{
							//Finish UTF-8 parsing!
							UTF8bytesleft = 0; //End UTF-8 parsing!
							//Handle the character normally as ASCII!
						}
					}
					c = currentkey->facetext[textx]; //Character to display!
					if (is_UTF8codepoint) //UTF-8?
					{
						if (UTF8codepoint<0x100) //Valid to use?
						{
							c = (byte)UTF8codepoint; //Set the code point to use!
						}
						else //Invalid to use?
						{
							c = 63; //Unknown character!
						}
					}
					if (c == '\t') //Return to start x?
					{
						screenx = startx; //Return to the specified position!
						currentwidth = 0; //No width to start the new position!
					}
					else if ((c == '\r' && !USESLASHN) || (c == '\n' && USESLASHN)) //LF? If use \n, \n uses linefeed too, else just newline.
					{
						screenx = 0; //Move to the left!
						currentwidth = 0; //No width to start the new position!
					}
					if (c == '\n') //CR?
					{
						++y; //Next Y!
						++detectedrows; //One more row detected!
						currentwidth = 0; //No width to start the new position!
					}
					else if ((c != '\r') && (c != '\t')) //Never display \r or \t!
					{
						//Normal visible character?
						if (GPU_textgetxy(keyboardsurface, FINGEROSK_BASEX + screenx, FINGEROSK_BASEY + y, &screencharacter, &screenfont, &screenborder)) //Valid coordinate?
						{
							pressed |= GPU_textsetxyclickable(keyboardsurface, FINGEROSK_BASEX + screenx++, FINGEROSK_BASEY + y,c,OSK_ispressed?activecolor:fontcolor,bordercolor,0)&SETXYCLICKED_CLICKED; //Are we pressed?
						}
						++currentwidth; //We're adding one to the width we're currently at!
						if (currentwidth>detectedwidth) detectedwidth = currentwidth; //New width detection!
					}
				}
				if (currentwidth>detectedwidth) detectedwidth = currentwidth; //New width detection!

				currentsize->xsize = detectedwidth+1; //Detected x size, take 1 extra for measurement!
				currentsize->ysize = detectedrows+2; //Detected y size, take 1 extra for measurement!

				for (x=0;x<currentsize->xsize;++x) //Process the entire area!
				{
					for (y=0;y<currentsize->ysize;++y)
					{
						if (GPU_textgetxy(keyboardsurface, FINGEROSK_BASEX+currentkey->x+x, FINGEROSK_BASEY+currentkey->y+y, &screencharacter, &screenfont, &screenborder)) //Valid coordinate?
						{
							pressed |= GPU_textsetxyclickable(keyboardsurface, FINGEROSK_BASEX+currentkey->x+x, FINGEROSK_BASEY+currentkey->y+y, screencharacter, screenfont, screenborder, 0)&SETXYCLICKED_CLICKED; //Are we pressed for the entire area?
						}
					}
				}

				if (pressed) //Print the text on the screen!
				{
					fingerOSK_OSK_releasekey(currentkey,key,fontcolor,bordercolor,&pendingreleasestickykeys,releasingstickykeys); //Releasing this key, because we can't be pressed when opening the keyboard!
				}
			}
			if (pendingreleasestickykeys) //Pending to release?
			{
				pendingreleasestickykeys = 0;
				releasingstickykeys = 1; //We're to release the sticky keys, while still pressed!
				goto startreleasestickykeys; //Start releasing the sticky keys!
			}
			OSKdrawn = 1; //We're drawn after this!
		}
		updateFingerOSK_mouse(); //Update our mouse handling!

		releasingstickykeys = (Stickykeys==0); //Default: not releasing any sticky keys now!
		startreleasestickykeysdrawn: //To jump back here when releasing!
		pendingreleasestickykeys = 0; //Pending to release sticky keys?
		currentkey = &OSKinfo[0]; //The first key to process
		currentsize = &OSKsize[0]; //The first key to process
		for (key = 0;key<NUMITEMS(OSKinfo);++key, ++currentkey, ++currentsize) //Check for all keys!
		{
			byte pressed = 0; //Are we pressed? Default to not pressed!
			screenx = currentkey->x; //Screen x!
			y = currentkey->y; //Screen y!
			startx = screenx; //Save a copy of the beginning of the character!
			for (x = 0;x<currentsize->xsize;++x) //Process the entire area!
			{
				for (y = 0;y<currentsize->ysize;++y)
				{
					if (GPU_textgetxy(keyboardsurface, FINGEROSK_BASEX + currentkey->x + x, FINGEROSK_BASEY + currentkey->y + y, &screencharacter, &screenfont, &screenborder)) //Valid coordinate?
					{
						pressed |= GPU_ispressed(keyboardsurface, FINGEROSK_BASEX + currentkey->x + x, FINGEROSK_BASEY + currentkey->y + y); //Are we pressed?
					}
				}
			}

			if (pressed && ((currentkey->pressed&1) == 0)) //Are we newly pressed?
			{
				fingerOSK_OSK_presskey(currentkey,key,activecolor,bordercolor); //We're pressed, supporting Sticky keys!
			}
			else if ((pressed == 0) && ((currentkey->pressed&1) || ((currentkey->pressed && (releasingstickykeys || ((currentkey->pressed&&(Stickykeys==0)))))))) //Are we released or second release, or full release by sticky key being released?
			{
				fingerOSK_OSK_releasekey(currentkey,key,fontcolor,bordercolor,&pendingreleasestickykeys,releasingstickykeys); //We're release, supporting Sticky keys!
			}
		}
		if (pendingreleasestickykeys) //Pending to release?
		{
			pendingreleasestickykeys = 0;
			releasingstickykeys = 1; //We're to release the sticky keys!
			goto startreleasestickykeysdrawn; //Start releasing the sticky keys!
		}
		GPU_text_releasesurface(keyboardsurface); //Release us!
	}
	else //OSK disabled? Clear it!
	{
		if (OSKdrawn) //Are we drawn?
		{
			GPU_text_locksurface(keyboardsurface); //Lock us!
			OSKdrawn = 0; //We're not drawn anymore after this!
			for (key = 0;key<NUMITEMS(OSKinfo);++key, ++currentkey) //Check for all keys!
			{
				GPU_textgotoxy(keyboardsurface, currentkey->x + FINGEROSK_BASEX, currentkey->y + FINGEROSK_BASEY); //Goto the location of the key!
				UTF8bytesleft = 0; //Init!
				for (x = 0;x<safestrlen(currentkey->facetext,sizeof(currentkey->facetext));x++)
				{
					is_UTF8codepoint = 0; //Not UTF-8!
					c = currentkey->facetext[x]; //Character to display!
					if (c&0x80) //UTF-8 support!
					{
						if ((c&0xC0)==0x80) //Continuation byte?
						{
							if (UTF8bytesleft) //Were we paring UTF-8?
							{
								--UTF8bytesleft; //One byte parsed!
								//6 bits added!
								UTF8codepoint <<= 6; //6 bits to add!
								UTF8codepoint |= (c&0x3F); //6 bits added!
								if (UTF8bytesleft) //Still more bytes left?
								{
									continue; //Next byte please!
								}
								else //Finished UTF-8 code point?
								{
									is_UTF8codepoint = 1; //UTF-8!
								}
							}
							else //Invalid UTF-8 string, abort it! Count as extended ASCII!
							{
								UTF8bytesleft = 0; //Abort!
							}
						}
						else if ((c&0xE0)==0xC0) //Two byte UTF-8 starting?
						{
							//5 bits for the first byte, 6 for the other bytes!
							UTF8codepoint = (c&0x1F); //5 bits to start with!
							UTF8bytesleft = 1; //1 byte left!
							continue; //Next byte please!
						}
						else if ((c&0xF0)==0xE0) //Three byte UTF-8 starting?
						{
							//4 bits for the first byte, 6 for the other bytes!
							UTF8codepoint = (c&0xF); //4 bits to start with!
							UTF8bytesleft = 2; //2 bytes left!
							continue; //Next byte please!
						}
						else if ((c&0xF8)==0xF0) //Four byte UTF-8 starting?
						{
							//3 bits for the first byte, 6 for the other bytes!
							UTF8codepoint = (c&0x7); //3 bits to start with!
							UTF8bytesleft = 3; //3 bytes left!
							continue; //Next byte please!
						}
						else //Non-UTF-8 encoded?
						{
							//Finish UTF-8 parsing!
							UTF8bytesleft = 0; //End UTF-8 parsing!
							//Handle the character normally as ASCII!
						}
					}
					if (is_UTF8codepoint) //UTF-8?
					{
						if (UTF8codepoint<0x100) //Valid to use?
						{
							c = (byte)UTF8codepoint; //Set the code point to use!
						}
						else //Invalid to use?
						{
							c = 63; //Unknown character!
						}
					}
					if ((c == '\t') || (c == '\r') || (c == '\n')) //Special?
					{
						emptykey.facetext[x] = currentkey->facetext[x]; //Control character!
					}
					else //Normal character?
					{
						emptykey.facetext[x] = ' '; //Empty text!
					}
				}
				emptykey.facetext[safestrlen(currentkey->facetext,sizeof(currentkey->facetext))] = '\0'; //End of string at the end of face text!
				GPU_textprintf(keyboardsurface, fontcolor, bordercolor, emptykey.facetext); //Print the text on the screen!
				if (currentkey->pressed)
				{
					fingerOSK_releasekey(key); //Releasing this key when pressed!
					currentkey->pressed = 0; //Not pressed anymore! Ignore sticky keys: we're forced off!
				}
			}

			GPU_text_releasesurface(keyboardsurface); //Release us!
		}
		updateFingerOSK_mouse(); //Update our mouse handling!
	}
}


extern ThreadParams_p BIOSMenuThread; //BIOS pause menu thread!

int dummyval_keyboardrenderer;
void keyboard_renderer() //Render the keyboard on-screen!
{
	static byte last_rendered = 0; //Last rendered keyboard status: 1=ON, 0=OFF!
	lock(LOCK_INPUT);
	if (!KEYBOARD_ENABLED)
	{
		unlock(LOCK_INPUT); //Finished!
		return; //Disabled?
	}

	int x;
	int y; //The coordinates in the buffer!
	int ybase,xbase;
	ybase = GPU_TEXTSURFACE_HEIGHT-KEYBOARD_NUMY; //Base Y on GPU's text screen!
	xbase = GPU_TEXTSURFACE_WIDTH-KEYBOARD_NUMX; //Base X on GPU's text screen!

	if (input_enabled==0) //Keyboard disabled atm OR Gaming mode?
	{
		if (last_rendered) //We're rendered?
		{
			last_rendered = 0; //We're not rendered now!
		}
	}

	last_rendered = 1; //We're rendered!
	fill_keyboarddisplay(); //Fill the keyboard display!
	updateFingerOSK(); //Make sure the finger OSK is updated so we know what to ignore!

	for (y=ybase;y<GPU_TEXTSURFACE_HEIGHT;y++)
	{
		for (x = xbase;x < GPU_TEXTSURFACE_WIDTH;x++)
		{
			uint_32 fontcolor = getemucol16(BIOS_Settings.input_settings.fontcolor); //Use font color by default!
			uint_32 bordercolor = getemucol16(BIOS_Settings.input_settings.bordercolor); //Use border color by default!
			switch (keyboard_attribute[y - ybase][x - xbase]) //What attribute?
			{
			case 1: //Active color?
				bordercolor = getemucol16(BIOS_Settings.input_settings.activecolor); //Use active color!
				break;
			case 2: //Special (Ctrl/Shift/Alt keys) inactive?
				fontcolor = getemucol16(BIOS_Settings.input_settings.specialcolor); //Use active color for special keys!
				bordercolor = getemucol16(BIOS_Settings.input_settings.specialbordercolor); //Use inactive border for special keys!
				break;
			case 3: //Special (Ctrl/Shift/Alt keys) active?
				fontcolor = getemucol16(BIOS_Settings.input_settings.specialcolor); //Use active color for special keys!
				bordercolor = getemucol16(BIOS_Settings.input_settings.specialactivecolor); //Use active color!
				break;
			default: //Default/standard border!
				break;
			}
			GPU_text_locksurface(keyboardsurface); //Lock us!
			if (keyboard_special[y - ybase][x - xbase] == 3) //Special Settings toggle?
			{
				if (GPU_textsetxyclickable(keyboardsurface, x, y, keyboard_display[y - ybase][x - xbase], fontcolor, bordercolor,0)&SETXYCLICKED_CLICKED) //Settings menu toggle on click?
				{
					if ((BIOSMenuThread==NULL) || (Settings_request==2))  //BIOS pause menu thread not already running?
					{
						Settings_request = 1; //Requesting settings to be loaded!
					}
				}
			}
			else if (keyboard_special[y - ybase][x - xbase] == 4) //Sticky keys toggle?
			{
				if (GPU_textsetxyclickable(keyboardsurface, x, y, keyboard_display[y - ybase][x - xbase], fontcolor, bordercolor,0)&SETXYCLICKED_CLICKED) //Sticky toggle on click?
				{
					Stickykeys ^= 1; //Toggle sticky keys!
					GPU_text_releasesurface(keyboardsurface); //Lock us!
					updateFingerOSK(); //Make sure the finger OSK is updated!
					GPU_text_locksurface(keyboardsurface); //Lock us!
				}
			}
			else if (keyboard_special[y - ybase][x - xbase]==2) //Finger OSK toggle?
			{
				if (GPU_textsetxyclickable(keyboardsurface, x, y, keyboard_display[y - ybase][x - xbase], fontcolor, bordercolor,0)&SETXYCLICKED_CLICKED) //Finger OSK toggle on click?
				{
					FINGEROSK ^= 1; //Toggle the Finger OSK!
					GPU_text_releasesurface(keyboardsurface); //Lock us!
					updateFingerOSK(); //Show/hide the finger OSK!
					GPU_text_locksurface(keyboardsurface); //Lock us!
				}
			}
			else if (keyboard_special[y - ybase][x - xbase]==1) //Screen capture?
			{
				SCREEN_CAPTURE |= (GPU_textsetxyclickable(keyboardsurface, x, y, keyboard_display[y - ybase][x - xbase], fontcolor, bordercolor,0)&SETXYCLICKED_CLICKED)?1:0; //Screen capture on click?
			}
			else if (keyboard_special[y - ybase][x - xbase] == 0xFF) //Special registered button?
			{
				dummyval_keyboardrenderer = GPU_textsetxyignoreclickable(keyboardsurface, x, y, keyboard_display[y - ybase][x - xbase], fontcolor, bordercolor); //Render normal character!
			}
			else if (keyboard_special[y - ybase][x - xbase]==0) //Normal character?
			{
				GPU_textsetxy(keyboardsurface, x, y, keyboard_display[y - ybase][x - xbase], fontcolor, bordercolor); //Render normal character!
			}
			GPU_text_releasesurface(keyboardsurface); //Unlock us!
		}
	}
	unlock(LOCK_INPUT);
}

int ticking = 0; //Already ticking?

byte req_quit_gamingmode = 0; //Requesting to quit gaming mode?
byte req_enter_gamingmode = 0; //Requesting to quit gaming mode?

char gamingmodebuttonsfacebuttonname[5][256] = { "down", "square", "triangle", "cross", "circle" }; //The names of the face buttons, in text format!

void clearBuffers(); //Clear any input buffers still filled! prototype!

void keyboard_swap_handler() //Swap handler for keyboard!
{
	lock(LOCK_INPUT);
	if (input_enabled && (!Direct_Input)) //Input enabled?
	{
		if (curstat.gamingmode) //Gaming mode?
		{
			if (psp_inputkey()&BUTTON_SELECT) //Quit gaming mode?
			{
				req_quit_gamingmode = 1; //We're requesting to quit gaming mode!
			}
			else if (req_quit_gamingmode) //Select released and requesting to quit gaming mode?
			{
				clearBuffers(); //Make sure all buffers are cleared!
				curstat.gamingmode = 0; //Disable gaming mode!
				req_quit_gamingmode = 0; //Not anymore!
			}
		}
		else //Either mouse or keyboard mode?
		{
			int curkey;
			curkey = psp_inputkey(); //Read current keys with delay!
			if (curkey&BUTTON_DOWN) //Down pressed: swap to gaming mode!
			{
				req_enter_gamingmode = 1; //We're requesting to enter gaming mode!
			}
			else if (req_enter_gamingmode) //Down released and requesting to enter gaming mode?
			{
				if (curkey & BUTTON_SQUARE) //Square still pressed?
				{
					req_enter_gamingmode = 2; //Square mode
					unlock(LOCK_INPUT);
					return; //Wait for release!
				}
				else if (curkey & BUTTON_TRIANGLE) //Triangle still pressed?
				{
					req_enter_gamingmode = 3; //Triangle mode
					unlock(LOCK_INPUT);
					return; //Wait for release!
				}
				else if (curkey & BUTTON_CROSS) //Cross still pressed?
				{
					req_enter_gamingmode = 4; //Cross mode
					unlock(LOCK_INPUT);
					return; //Wait for release!
				}
				else if (curkey & BUTTON_CIRCLE) //Circle still pressed?
				{
					req_enter_gamingmode = 5; //Circle mode
					unlock(LOCK_INPUT);
					return; //Wait for release!
				}
				currentkey = 0; //No keys pressed!
				ReleaseKeys(); //Release all keys!
				curstat.gamingmode = req_enter_gamingmode; //Enable gaming mode for the selected mode!
				req_enter_gamingmode = 0; //Not anymore!
			}
			else if (curstat.mode==1) //Keyboard active and on-screen?
			{
				if ((curkey&BUTTON_LTRIGGER) && (!(curkey&BUTTON_RTRIGGER))) //Not L&R (which is CAPS LOCK) special?
				{
					currentset = (currentset+1)%3; //Next set!
					currentkey = 0; //No keys pressed!
					//Disable all output still standing!
					ReleaseKeys(); //Release all keys!
				}
				else if (curkey&BUTTON_START) //Swap to mouse mode!
				{
					currentkey = 0; //No keys pressed!
					ReleaseKeys(); //Release all keys!
					curstat.mode = 0; //Swap to mouse mode!
				}
			}
			else if (curstat.mode==0) //Mouse active?
			{
				if (psp_inputkey()&BUTTON_START) //Swap to keyboard mode!
				{
					ReleaseKeys(); //Release all keys!
					curstat.mode = 1; //Swap to keyboard mode!
				}
			}
		}
	}
	unlock(LOCK_INPUT);
}

extern char keys_names[104][11]; //All names of the used keys (for textual representation/labeling)

DOUBLE keyboard_mousetiming = 0.0f; //Current timing!

void handleMouseMovement(DOUBLE timepassed) //Handles mouse movement using the analog direction of the mouse!
{
	keyboard_mousetiming += timepassed; //Tick time!
	for (;keyboard_mousetiming>=keyboard_mouseinterval;) //Interval passed?
	{
		if (curstat.analogdirection_mouse_x || curstat.analogdirection_mouse_y) //Mouse analog direction trigger?
		{
			mouse_xmove += (int_64)((((float)curstat.analogdirection_mouse_x) / 32767.0f)*keyboard_mousemovementspeed); //Apply x movement in mm!
			mouse_ymove += (int_64)((((float)curstat.analogdirection_mouse_y) / 32767.0f)*keyboard_mousemovementspeed); //Apply y movement in mm!
			mouse_xmovemickey += (int_64)((((((float)curstat.analogdirection_mouse_x) / 32767.0f)*keyboard_mousemovementspeed)/GPU_xDTM)*GPU_xDTmickey); //Apply x movement in mickeys!
			mouse_ymovemickey += (int_64)((((((float)curstat.analogdirection_mouse_y) / 32767.0f)*keyboard_mousemovementspeed)/GPU_yDTM)*GPU_yDTmickey); //Apply y movement in mickeys!
		}
		keyboard_mousetiming -= keyboard_mouseinterval; //Substract interval to next tick!
	}
}

void handleKeyboardMouse(DOUBLE timepassed) //Handles keyboard input during mouse operations!
{
	//Also handle mouse movement here (constant factor)!
	handleMouseMovement(timepassed); //Handle mouse movement!

	if (!input_buffer_enabled) //Not buffering input?
	{
		Mouse_buttons = (curstat.buttonpress & 1) ? 1 : 0; //Left mouse button pressed?
		Mouse_buttons |= (curstat.buttonpress & 4) ? 2 : 0; //Right mouse button pressed?
		Mouse_buttons |= (curstat.buttonpress & 2) ? 4 : 0; //Middle mouse button pressed?
	}

	oldshiftstatus = shiftstatus; //Save the old shift status!
	shiftstatus = 0; //Init shift status!
	shiftstatus |= ((curstat.buttonpress&512)>0)*SHIFTSTATUS_SHIFT; //Apply shift status!
	shiftstatus |= ((curstat.buttonpress&(16|32))>0)*SHIFTSTATUS_CTRL; //Apply ctrl status!
	shiftstatus |= ((curstat.buttonpress&(64|32))>0)*SHIFTSTATUS_ALT; //Apply alt status!
	currentshift = (shiftstatus&SHIFTSTATUS_SHIFT)>0; //Shift pressed?
	currentctrl = (shiftstatus&SHIFTSTATUS_CTRL)>0; //Ctrl pressed?
	currentalt = (shiftstatus&SHIFTSTATUS_ALT)>0; //Alt pressed?

	if (!input_buffer_enabled) //Not buffering?
	{
		//First, process Ctrl,Alt,Shift Releases!
		if (((oldshiftstatus&SHIFTSTATUS_CTRL)>0) && (!(shiftstatus&SHIFTSTATUS_CTRL))) //Released CTRL?
		{
			onKeyRelease("lctrl");
		}
		if (((oldshiftstatus&SHIFTSTATUS_ALT)>0) && (!(shiftstatus&SHIFTSTATUS_ALT))) //Released ALT?
		{
			onKeyRelease("lalt");
		}
		if (((oldshiftstatus&SHIFTSTATUS_SHIFT)>0) && (!(shiftstatus&SHIFTSTATUS_SHIFT))) //Released SHIFT?
		{
			onKeyRelease("lshift");
		}
		//Next, process Ctrl,Alt,Shift presses!
		if ((shiftstatus&SHIFTSTATUS_CTRL)>0) //Pressed CTRL?
		{
			onKeyPress("lctrl");
		}
		if ((shiftstatus&SHIFTSTATUS_ALT)>0) //Pressed ALT?
		{
			onKeyPress("lalt");
		}
		if ((shiftstatus&SHIFTSTATUS_SHIFT)>0) //Pressed SHIFT?
		{
			onKeyPress("lshift");
		}
	} //Not buffering?
	else //Buffering input?
	{
		oldMouse_buttons = currentmouse; //Save the old mouse buttons!

		//Calculate current mouse state!
		currentmouse = (curstat.buttonpress & 1) ? 1 : 0; //Left mouse button pressed?
		currentmouse |= (curstat.buttonpress & 4) ? 2 : 0; //Right mouse button pressed?
		currentmouse |= (curstat.buttonpress & 2) ? 4 : 0; //Middle mouse button pressed?
		
		if ((currentmouse<oldMouse_buttons) || (shiftstatus<oldshiftstatus)) //Less buttons/keys pressed than earlier?
		{
			if ((input_buffer==-1) && (!input_buffer_shift) && (!input_buffer_mouse)) //No input yet?
			{
				if (oldMouse_buttons || oldshiftstatus) //Mouse button release or shift status release?
				{
					input_buffer_shift = oldshiftstatus; //Shift status!
					input_buffer_mouse = oldMouse_buttons; //Mouse status!
					input_buffer = -1; //No key!
				}
			}
		}
	}
}

void handleKeyboard() //Handles keyboard input!
{
	keysactive = 0; //Reset keys active!
	setx = curstat.analogdirection_keyboard_x; //X in keyboard set!
	sety = curstat.analogdirection_keyboard_y; //Y in keyboard set!

	//Now handle current keys!
	currentkey = 0; //Default: no key pressed!
	//Order of currentkey: Up left down right.
	if (curstat.buttonpress & 1) //Left?
	{
		currentkey = 2; //Pressed square!
	}
	else if (curstat.buttonpress & 2) //Up?
	{
		currentkey = 1; //Pressed triangle!
	}
	else if (curstat.buttonpress & 4) //Right?
	{
		currentkey = 4; //Circle pressed!
	}
	else if (curstat.buttonpress & 8) //Down?
	{
		currentkey = 3; //Cross pressed!
	}

	//Now, process the keys!

	oldshiftstatus = shiftstatus; //Make sure to save the old status!
	shiftstatus = 0; //Init shift status!
	shiftstatus |= ((curstat.buttonpress & 512) > 0)*SHIFTSTATUS_SHIFT; //Apply shift status!
	if ((curstat.buttonpress & 0x300) == 0x300) //L&R hold?
	{
		shiftstatus &= ~SHIFTSTATUS_SHIFT; //Shift isn't pressed: it's CAPS LOCK special case!
	}
	shiftstatus |= ((curstat.buttonpress&(16 | 32)) > 0)*SHIFTSTATUS_CTRL; //Apply ctrl status!
	shiftstatus |= ((curstat.buttonpress&(64 | 32)) > 0)*SHIFTSTATUS_ALT; //Apply alt status!
	currentshift = (shiftstatus&SHIFTSTATUS_SHIFT) > 0; //Shift pressed?
	currentctrl = (shiftstatus&SHIFTSTATUS_CTRL) > 0; //Ctrl pressed?
	currentalt = (shiftstatus&SHIFTSTATUS_ALT) > 0; //Alt pressed?

	if (!input_buffer_enabled) //Not buffering?
	{
		//First, process Ctrl,Alt,Shift Releases!
		if (((oldshiftstatus&SHIFTSTATUS_CTRL) > 0) && (!currentctrl)) //Released CTRL?
		{
			onKeyRelease("lctrl");
		}
		if (((oldshiftstatus&SHIFTSTATUS_ALT) > 0) && (!currentalt)) //Released ALT?
		{
			onKeyRelease("lalt");
		}
		if (((oldshiftstatus&SHIFTSTATUS_SHIFT) > 0) && (!currentshift)) //Released SHIFT?
		{
			onKeyRelease("lshift");
		}
		//Next, process Ctrl,Alt,Shift presses!
		if (currentctrl) //Pressed CTRL?
		{
			onKeyPress("lctrl");
		}
		if (currentalt) //Pressed ALT?
		{
			onKeyPress("lalt");
		}
		if (currentshift) //Pressed SHIFT?
		{
			onKeyPress("lshift");
		}

		if ((curstat.buttonpress & 0x300) == 0x300) //L&R hold? CAPS LOCK PRESSED! (Special case)
		{
			onKeyPress("capslock"); //Shift isn't pressed: it's CAPS LOCK special case!
		}
		else //No CAPS LOCK?
		{
			onKeyRelease("capslock"); //Release if needed, forming a button click!
		}

		if (currentkey) //Key pressed?
		{
			if (lastkey && ((lastkey != currentkey) || (lastx != setx) || (lasty != sety) || (lastset != currentset))) //We had a last key that's different?
			{
				onKeyRelease(getkeyboard(shiftstatus&SHIFTSTATUS_SHIFT, lastset, lasty, lastx, displaytokeyboard[lastkey])); //Release the last key!
			}
			if (strcmp(getkeyboard(shiftstatus&SHIFTSTATUS_SHIFT, lastset, lasty, lastx, displaytokeyboard[lastkey]),"lctrl") && strcmp(getkeyboard(shiftstatus&SHIFTSTATUS_SHIFT, lastset, lasty, lastx, displaytokeyboard[lastkey]),"lalt") && strcmp(getkeyboard(shiftstatus&SHIFTSTATUS_SHIFT, lastset, lasty, lastx, displaytokeyboard[lastkey]),"lshift") && strcmp(getkeyboard(shiftstatus&SHIFTSTATUS_SHIFT, lastset, lasty, lastx, displaytokeyboard[lastkey]),"capslock")) //Ignore already processed keys!
			{
				onKeyPress(getkeyboard(0, currentset, sety, setx, displaytokeyboard[currentkey]));
			}
			//Save the active key information!
			lastset = currentset;
			lastx = setx;
			lasty = sety;
			lastkey = currentkey;
		}
		else if (lastkey) //We have a last key with nothing pressed?
		{
			if (strcmp(getkeyboard(shiftstatus&SHIFTSTATUS_SHIFT, lastset, lasty, lastx, displaytokeyboard[lastkey]),"lctrl") && strcmp(getkeyboard(shiftstatus&SHIFTSTATUS_SHIFT, lastset, lasty, lastx, displaytokeyboard[lastkey]),"lalt") && strcmp(getkeyboard(shiftstatus&SHIFTSTATUS_SHIFT, lastset, lasty, lastx, displaytokeyboard[lastkey]),"lshift") && strcmp(getkeyboard(shiftstatus&SHIFTSTATUS_SHIFT, lastset, lasty, lastx, displaytokeyboard[lastkey]),"capslock")) //Ignore already processed keys!
			{
				onKeyRelease(getkeyboard(0, lastset, lasty, lastx, displaytokeyboard[lastkey])); //Release the last key!
			}
			lastkey = 0; //We didn't have a last key!			
		}
	} //Not buffering?
	else //Buffering?
	{
		if (!(shiftstatus&SHIFTSTATUS_CTRL) && ((lastshift&SHIFTSTATUS_CTRL) > 0)) //Released CTRL?
		{
			goto keyreleased; //Released!
		}
		if (!(shiftstatus&SHIFTSTATUS_ALT) && ((lastshift&SHIFTSTATUS_ALT) > 0)) //Released ALT?
		{
			goto keyreleased; //Released!
		}
		if (!(shiftstatus&SHIFTSTATUS_SHIFT) && ((lastshift&SHIFTSTATUS_SHIFT) > 0)) //Released SHIFT?
		{
			goto keyreleased; //Released!
		}

		if (currentkey || shiftstatus) //More keys pressed?
		{
			if (lastkey && ((lastkey != currentkey) || (lastx != setx) || (lasty != sety) || (lastset != currentset))) //We had a last key that's different?
			{
				goto keyreleased; //Released after all!
			}
			//Save the active key information!

			lastset = currentset;
			lastx = setx;
			lasty = sety;
			lastkey = currentkey;
			lastshift = shiftstatus; //Shift status!
		}
		else //Key/shift released?
		{
			sword key;
		keyreleased:
			if (!lastkey && !lastshift) //Nothing yet?
			{
				return; //Abort: we're nothing pressed!
			}
			key = EMU_keyboard_handler_nametoid(getkeyboard(0, lastset, lasty, lastx, displaytokeyboard[lastkey])); //Our key?
			if (input_buffer_enabled && (input_buffer==-1) && (!input_buffer_shift) && (!input_buffer_mouse)) //Buffering?
			{
				input_buffer_shift = lastshift; //Set shift status!
				input_buffer = key; //Last key!
				input_buffer_mouse = 0; //No mouse input during this mode!
			}
			//Update current information!
			lastkey = 0; //Update current information!
			lastx = setx;
			lasty = sety;
			lastset = currentset;
			lastshift = shiftstatus;
		}
		//Key presses aren't buffered: we only want to know the key and shift state when fully pressed, nothing more!
	}
}

byte gamingmode_keys_pressed[15] = {0,0,0,0,0,0,0,0,0,0,0}; //We have pressed the key?

void handleGaming(DOUBLE timepassed) //Handles gaming mode input!
{
	//Test for all keys and process!
	if (input_buffer_enabled) return; //Don't handle buffered input, we don't allow mapping gaming mode to gaming mode!

	sword keys[15]; //Key index for this key, -1 for none/nothing happened!
	byte keymappings[15] = {GAMEMODE_START, GAMEMODE_LEFT, GAMEMODE_UP, GAMEMODE_RIGHT, GAMEMODE_DOWN, GAMEMODE_LTRIGGER, GAMEMODE_RTRIGGER, GAMEMODE_TRIANGLE, GAMEMODE_CIRCLE, GAMEMODE_CROSS, GAMEMODE_SQUARE, GAMEMODE_ANALOGLEFT, GAMEMODE_ANALOGUP, GAMEMODE_ANALOGRIGHT, GAMEMODE_ANALOGDOWN};
	//Order: START, LEFT, UP, RIGHT, DOWN, L, R, TRIANGLE, CIRCLE, CROSS, SQUARE, ANALOGLEFT, ANALOGUP, ANALOGRIGHT, ANALOGDOWN
	uint_32 keybits[15] = {1024,16,32,64,128,256,512,2,4,8,1,0,0,0,0}; //Button mapped to input, analog=0.
	byte analogmapped; //Is analog mapped?

	oldshiftstatus = shiftstatus; //Save backup for press/release!
	shiftstatus = Mouse_buttons = 0; //Default new shift/mouse status: none!
	analogmapped = 0; //Default: analog isn't mapped!

	int i;
	byte keystat,keymapping,isanalog;
	for (i=0;i<15;i++)
	{
		keys[i] = -1; //We have no index and nothing happened!
		isanalog = 0;
		if (keybits[i]) //Mapped to a key?
		{
			keystat = ((curstat.buttonpress&keybits[i])>0);
		}
		else //Mapped to analog?
		{
			isanalog = 1; //Default: analog (below cases)!
			switch (keymappings[i]) //What analog key?
			{
				case GAMEMODE_ANALOGLEFT: keystat = ((curstat.analogdirection_keyboard_x<0) && (!curstat.analogdirection_keyboard_y)); break; //ANALOG LEFT?
				case GAMEMODE_ANALOGRIGHT: keystat = ((curstat.analogdirection_keyboard_x>0) && (!curstat.analogdirection_keyboard_y)); break; //ANALOG RIGHT?
				case GAMEMODE_ANALOGUP: keystat = ((!curstat.analogdirection_keyboard_x) && (curstat.analogdirection_keyboard_y<0)); break; //ANALOG UP?
				case GAMEMODE_ANALOGDOWN: keystat = ((!curstat.analogdirection_keyboard_x) && (curstat.analogdirection_keyboard_y>0)); break; //ANALOG DOWN?
				default: keystat = isanalog = 0; break; //Unknown key! Not analog!
				break;
			}
		}
		keymapping = keymappings[i]; //Mapping to use for this key.

		//Process press/release of key!
		if ((BIOS_Settings.input_settings.keyboard_gamemodemappings[curstat.gamingmode-1][keymapping]!=-1) || (BIOS_Settings.input_settings.keyboard_gamemodemappings_alt[curstat.gamingmode-1][keymapping]) || (BIOS_Settings.input_settings.mouse_gamemodemappings[curstat.gamingmode-1][keymapping])) //Mapped on?
		{
			analogmapped |= isanalog; //Analog is mapped when we're analog!
			if (keystat) //Pressed?
			{
				keys[i] = BIOS_Settings.input_settings.keyboard_gamemodemappings[curstat.gamingmode-1][keymapping]; //Set the key: we've changed!
				gamingmode_keys_pressed[i] = 1; //We're pressed!
				shiftstatus |= BIOS_Settings.input_settings.keyboard_gamemodemappings_alt[curstat.gamingmode-1][keymapping]; //Ctrl-alt-shift status!
				Mouse_buttons |= BIOS_Settings.input_settings.mouse_gamemodemappings[curstat.gamingmode-1][keymapping]; //Mouse status!
			}
			else if (gamingmode_keys_pressed[i]) //Try release!
			{
				keys[i] = BIOS_Settings.input_settings.keyboard_gamemodemappings[curstat.gamingmode-1][keymapping]; //Set the key: we've changed!
				gamingmode_keys_pressed[i] = 0; //We're released!
			}
		}
	}

	if (!analogmapped) //No analog assigned? Process analog mouse movement!
	{
		//Also handle mouse movement here (constant factor)!
		handleMouseMovement(timepassed); //Handle mouse movement!
	}

	//First, process Ctrl,Alt,Shift Releases!
	if (((oldshiftstatus&SHIFTSTATUS_CTRL)>0) && (!(shiftstatus&SHIFTSTATUS_CTRL))) //Released CTRL?
	{
		onKeyRelease("lctrl");
	}
	if (((oldshiftstatus&SHIFTSTATUS_ALT)>0) && (!(shiftstatus&SHIFTSTATUS_ALT))) //Released ALT?
	{
		onKeyRelease("lalt");
	}
	if (((oldshiftstatus&SHIFTSTATUS_SHIFT)>0) && (!(shiftstatus&SHIFTSTATUS_SHIFT))) //Released SHIFT?
	{
		onKeyRelease("lshift");
	}
	//Next, process Ctrl,Alt,Shift presses!
	if ((shiftstatus&SHIFTSTATUS_CTRL)>0) //Pressed CTRL?
	{
		onKeyPress("lctrl");
	}
	if ((shiftstatus&SHIFTSTATUS_ALT)>0) //Pressed ALT?
	{
		onKeyPress("lalt");
	}
	if ((shiftstatus&SHIFTSTATUS_SHIFT)>0) //Pressed SHIFT?
	{
		onKeyPress("lshift");
	}

	//Next, process the keys!
	for (i=0;i<15;i++) //Process all keys!
	{
		if (keys[i]!=-1) //Action is mapped?
		{
			char keyname[256]; //For storing the name of the key!
			if (EMU_keyboard_handler_idtoname(keys[i],&keyname[0])) //Gotten ID (valid key)?
			{
				if (strcmp(keyname,"lctrl") && strcmp(keyname,"lalt") && strcmp(keyname,"lshift")) //Ignore already processed keys!
				{
					if (gamingmode_keys_pressed[i]) //Pressed?
					{
						onKeyPress(keyname); //Press the key!
					}
					else //Released?
					{
						onKeyRelease(keyname); //Release the key!
					}
				}
			}
		}
	}
}

//Information based on http://www.epanorama.net/documents/joystick/pc_special.html
void handleJoystick(DOUBLE timepassed) //Handles gaming mode input!
{
#ifdef UNIPCEMU
	byte stick_disabled = 1;
#endif
	//Test for all keys and process!
	if (input_buffer_enabled) return; //Don't handle buffered input, we don't allow mapping joystick mode to gaming mode!
#ifdef UNIPCEMU
	stick_disabled = 1; //Default: not enabled!
	if (input_enabled && ALLOW_INPUT) //Input enabled?
	{
		if (!Direct_Input) //Not executing direct input?
		{
			//Determine stuff for output!
			//Don't process shift atm!

			if (curstat.gamingmode) //Gaming mode?
			{
				if (BIOS_Settings.input_settings.usegamingmode_joystick[curstat.gamingmode - 1]) //Using the joystick for this gaming mode?
				{
					stick_disabled = 0; //Use stick input for gaming mode!
				}
			}
		}
	}

	switch (BIOS_Settings.input_settings.gamingmode_joystick) //What joystick mapping mode?
	{
	case 1: //Cross=Button 1, Circle=Button 2(analog)?
		setJoystickModel(MODEL_NONE); //No extended model!
		enableJoystick(0,JOYSTICK_ENABLED); //Enable joystick A!
		enableJoystick(1,JOYSTICK_DISABLED); //Disable joystick B!		
		if (stick_disabled) //No stick input?
		{
			setJoystick(0, 0, 0, 0, 0); //Unused!
			setJoystick(1, 0, 0, 0, 0); //Unused!
		}
		else //Used!
		{
			setJoystick(0, curstat.buttonpress & 0x08, curstat.buttonpress & 0x04, curstat.analogdirection_mouse_x, curstat.analogdirection_mouse_y); //Cross=Button 1, Circle=Button 2?
			setJoystick(1, 0, 0, 0, 0); //Unused!
		}
		break;
	case 2: //Cross=Button 2, Circle=Button 1(analog)?
		setJoystickModel(MODEL_NONE); //No extended model!
		enableJoystick(0,JOYSTICK_ENABLED); //Enable joystick A!
		enableJoystick(1,JOYSTICK_DISABLED); //Disable joystick B!		
		if (stick_disabled) //No stick input?
		{
			setJoystick(0, 0, 0, 0, 0); //Unused!
			setJoystick(1, 0, 0, 0, 0); //Unused!
		}
		else //Used!
		{
			setJoystick(0, curstat.buttonpress & 0x04, curstat.buttonpress & 0x08, curstat.analogdirection_mouse_x, curstat.analogdirection_mouse_y); //Cross=Button 2, Circle=Button 1?
			setJoystick(1, 0, 0, 0, 0); //Unused!
		}
		break;
	case 3: //Gravis Gamepad(analog)? Use full mode!
		setJoystickModel(MODEL_NONE); //No extended model!
		enableJoystick(0,JOYSTICK_ENABLED); //Enable joystick A!
		enableJoystick(1,JOYSTICK_ENABLED_BUTTONSONLY); //Enable joystick B partially(no analog, only digital buttons)!
		if (stick_disabled) //No stick input?
		{
			setJoystick(0, 0, 0, 0, 0); //Unused!
			setJoystick(1, 0, 0, 0, 0); //Unused!
		}
		else //Used!
		{
			/*

			Gravis gamepad controller buttons:
			    blue
			red      green
			    yellow

			PSP inputs(bitmask for the bit):
			    2
			1       4
			    8

			Mapping to buttons with Turbo:
			    -
			1       -
			    2

			Mapping in full mode:
			    2
			1       4
			    3
			*/

			//Map in full mode!

			setJoystick(0, curstat.buttonpress & 0x01, curstat.buttonpress & 0x02, curstat.analogdirection_mouse_x, curstat.analogdirection_mouse_y); //Joystick A! Square=A1, Triangle=A2!
			setJoystick(1, curstat.buttonpress & 0x08, curstat.buttonpress & 0x04, 0, 0); //Joystick B! Cross=B1, Circle=B2!
		}
		break;
	case 4: //Gravis Analog Pro(analog)?
		setJoystickModel(MODEL_NONE); //No extended model!
		enableJoystick(0,JOYSTICK_ENABLED); //Enable joystick A!
		enableJoystick(1,JOYSTICK_ENABLED_BUTTONS_AND_XONLY); //Enable joystick B partially(analog x and digital buttons)!
		if (stick_disabled) //No stick input?
		{
			setJoystick(0, 0, 0, 0, 0); //Unused!
			setJoystick(1, 0, 0, 0, 0); //Unused!
		}
		else //Used!
		{
			setJoystick(0, curstat.buttonpress & 0x01, curstat.buttonpress & 0x02, curstat.analogdirection_mouse_x, curstat.analogdirection_mouse_y); //Joystick A! Square=A1, Triangle=A2!
			setJoystick(1, curstat.buttonpress & 0x04, curstat.buttonpress & 0x08, curstat.analogdirection2_y, 0); //Joystick B! Circle=B1, Cross=B2(reverse of Gravis Gamepad)! The X axis is the throttle!
		}
		break;
	case 5: //Logitech WingMan Extreme Digital?
		setJoystickModel(MODEL_LOGITECH_WINGMAN_EXTREME_DIGITAL); //Extended model!
		enableJoystick(0,JOYSTICK_ENABLED); //Enable joystick A!
		enableJoystick(1,JOYSTICK_ENABLED); //Enable joystick B!
		if (stick_disabled) //No stick input?
		{
			setJoystick_other(0, 0, 0, 0, 0, 0, //6 buttons
				0, 0, 0, 0, //Left:16,Up:32,Right:64,Down:128
				0, 0, 0, 0 //Analog sticks!
			); //Joystick A&B with extensions!
		}
		else //Used!
		{
			setJoystick_other(curstat.buttonpress & 0x08, curstat.buttonpress & 0x04, curstat.buttonpress & 0x01, curstat.buttonpress & 0x02, 0, 0, //6 buttons
				curstat.buttonpress & 16, curstat.buttonpress & 64, curstat.buttonpress & 32, curstat.buttonpress & 128, //Left:16,Up:32,Right:64,Down:128
				curstat.analogdirection_mouse_x, curstat.analogdirection_mouse_y, curstat.analogdirection2_x, curstat.analogdirection2_y //Analog sticks!
			); //Joystick A&B with extensions!
		}
		break;
	default: //Unknown mapping or no joystick connected?
		setJoystickModel(MODEL_NONE); //No extended model!
		enableJoystick(0, JOYSTICK_DISABLED); //Disable joystick A!
		enableJoystick(1, JOYSTICK_DISABLED); //Disable joystick B!		
		setJoystick(0, 0, 0, 0, 0); //Unknown mapping, ignore input?
		setJoystick(1, 0, 0, 0, 0); //Unknown mapping, ignore input?
		break;
	}
#endif
}

OPTINLINE void handleKeyPressRelease(int key)
{
	byte lastshiftstatus;
	byte isCAS;
	switch (emu_keys_state[key]) //What state are we in?
	{
	case 0: //Not pressed?
		break;
	case 1: //Pressed?
		//Shift status for buffering!
		if (!strcmp(keys_names[key],"lctrl"))
		{
			currentshiftstatus_inputbuffer |= SHIFTSTATUS_CTRL;
		}
		else if (!strcmp(keys_names[key],"lalt"))
		{
			currentshiftstatus_inputbuffer |= SHIFTSTATUS_ALT;
		}
		else if (!strcmp(keys_names[key],"lshift"))
		{
			currentshiftstatus_inputbuffer |= SHIFTSTATUS_SHIFT;
		}
		else if (strcmp(keys_names[key],"rctrl")!=0 && strcmp(keys_names[key],"ralt")!=0 && strcmp(keys_names[key],"rshift")!=0) //Ignore Right Ctrl, Alt and Shift!
		{
			last_input_key = key; //Save the last key for input!
		}
		if (!input_buffer_enabled) //Not inputting anything though the buffer? We don't count those as input for emulation!
		{
			//Normal handling always!
			onKeyPress(&keys_names[key][0]); //Tick the keypress!
		}
		break;
	case 2: //Released without pressed?
		emu_keys_state[key] = 0; //Fix us: we're not pressed after all!
		break;
	case 3: //Releasing?
		//Shift status for buffering!
		lastshiftstatus = currentshiftstatus_inputbuffer; //Save the last shift status for comparison!
		isCAS = 0; //Init!
		if (!strcmp(keys_names[key],"lctrl"))
		{
			currentshiftstatus_inputbuffer &= ~SHIFTSTATUS_CTRL; //Release CTRL!
			isCAS = SHIFTSTATUS_CTRL;
		}
		else if (!strcmp(keys_names[key],"lalt"))
		{
			currentshiftstatus_inputbuffer &= ~SHIFTSTATUS_ALT; //Release ALT!
			isCAS = SHIFTSTATUS_ALT;
		}
		else if (!strcmp(keys_names[key],"lshift"))
		{
			currentshiftstatus_inputbuffer &= ~SHIFTSTATUS_SHIFT; //Release SHIFT!
			isCAS = SHIFTSTATUS_SHIFT;
		}
		else if (strcmp(keys_names[key],"rctrl")!=0 && strcmp(keys_names[key],"ralt")!=0 && strcmp(keys_names[key],"rshift")!=0) //Ignore Right Ctrl, Alt and Shift!
		{
			last_input_key = key; //This is the key we need to use, since we're a normal released key, else use the last key pressed!
		}
		
		if (input_buffer_enabled && (input_buffer==-1) && (!input_buffer_shift) && (!input_buffer_mouse)) //Buffering and input not buffered yet?
		{
			input_buffer_shift = lastshiftstatus; //Set shift status to the last state!
			if ((isCAS == 0) || (input_buffer_enabled==1)) //Not Ctrl/Alt/Shift or normal mode?
			{
				input_buffer = last_input_key; //Last key pressed, if any(don't report Ctrl/Alt/Shift)!
			}
			input_buffer_mouse = mousebuttons; //Mouse button status!
		}

		//Normal handling always, as a release when not running must be repressed anyways!
		if (onKeyRelease(&keys_names[key][0])) //Handle key release!
		{
			emu_keys_state[key] = 0; //We're released when released(not pending anymore)!
		}
		break;
	default: //Unknown?
		break;
	}
}

void keyboard_type_handler(DOUBLE timepassed) //Handles keyboard typing: we're an interrupt!
{
	lock(LOCK_INPUT);
	if (input_enabled && ALLOW_INPUT) //Input enabled?
	{
		if (!Direct_Input) //Not executing direct input?
		{
			get_analog_state(&curstat); //Get the analog&buttons status for the keyboard!
			//Determine stuff for output!
			//Don't process shift atm!

			if (curstat.gamingmode) //Gaming mode?
			{
				if (BIOS_Settings.input_settings.gamingmode_joystick && BIOS_Settings.input_settings.usegamingmode_joystick[curstat.gamingmode]) //Gaming mode is mapped to the joystick instead?
				{
					handleJoystick(timepassed); //Handle joystick input?
				}
				else //Normal gaming mode input!
				{
					handleJoystick(timepassed); //Handle joystick input?
					handleGaming(timepassed); //Handle gaming input?
				}
			}
			else //Normal input mode?
			{
				handleJoystick(timepassed); //Handle the joysticks in normal input mode as well!
				switch (curstat.mode) //What input mode?
				{
				case 0: //Mouse mode?
					handleKeyboardMouse(timepassed); //Handle keyboard input during mouse operations?
					break;
				case 1: //Keyboard mode?
					handleKeyboard(); //Handle keyboard input?
					break;
				default: //Unknown state?
					curstat.mode = 0; //Reset mode!
					break;
				}
			}
		} //Input enabled?
		else //No input enabled?
		{
			handleJoystick(timepassed); //Handle the joysticks in no-input mode as well!
		}
	}
	else
	{
		handleJoystick(timepassed); //Handle the joysticks in no-input mode as well!
	}
	tickPendingKeys(timepassed); //Handle any pending keys if possible!
	unlock(LOCK_INPUT);
}

void setMouseRate(float packetspersecond)
{
	if (packetspersecond) //Valid packets?
	{
		#ifdef IS_LONGDOUBLE
		mouse_interval = (DOUBLE)(1000000000.0L/(DOUBLE)packetspersecond); //Handles mouse input: we're a normal timer!
		#else
		mouse_interval = (DOUBLE)(1000000000.0/(DOUBLE)packetspersecond); //Handles mouse input: we're a normal timer!
		#endif
	}
	else
	{
		mouse_interval = (float)0; //Disabled timer!
	}
}

int KEYBOARD_STARTED = 0; //Default not started yet!
void psp_keyboard_init()
{
	if (__HW_DISABLED) return; //Abort!
	lock(LOCK_INPUT);
	input_enabled = 0; //Default: input disabled!
	oldshiftstatus = shiftstatus = currentshiftstatus_inputbuffer =  0; //Init all shifts to unused/not pressed!
	curstat.mode = DEFAULT_KEYBOARD; //Keyboard mode enforced by default for now!

	if (!KEYBOARD_ENABLED) //Keyboard disabled?
	{
		unlock(LOCK_INPUT);
		return; //Keyboard disabled?
	}

	initEMUKeyboard(); //Initialise the keyboard support!
	unlock(LOCK_INPUT);

	
	addtimer(3.0f,&keyboard_swap_handler,"Keyboard PSP Swap",1,1,NULL); //Handles keyboard set swapping: we're an interrupt!
	
	lock(LOCK_INPUT);
	setMouseRate(1.0f); //No mouse atm, so default to 1 packet/second!
	KEYBOARD_STARTED = 1; //Started!
	unlock(LOCK_INPUT);
}

void psp_keyboard_done()
{
	if (__HW_DISABLED) return; //Abort!
	removetimer("Keyboard PSP Swap"); //No swapping!
}

void keyboard_loadDefaultColor(byte color)
{
	switch (color)
	{
	case 0:
		BIOS_Settings.input_settings.colors[0] = 0x1; //Blue font!
		break;
	case 1:
		BIOS_Settings.input_settings.colors[1] = 0x8; //Dark gray border inactive!
		break;
	case 2:
		BIOS_Settings.input_settings.colors[2] = 0xE; //Yellow border active!
		break;
	case 3:
		BIOS_Settings.input_settings.colors[3] = 0x7; //Special: Brown font!
		break;
	case 4:
		BIOS_Settings.input_settings.colors[4] = 0x6; //Special: Dark gray border inactive!
		break;
	case 5:
		BIOS_Settings.input_settings.colors[5] = 0xE; //Special: Yellow border active!
		break;
	default: //Unknown color?
		break;
	}
}

void keyboard_loadDefaults() //Load the defaults for the keyboard font etc.!
{
	BIOS_Settings.input_settings.analog_minrange = (byte)(127/2); //Default to half to use!
	//Default: no game mode mappings!
	//Standard keys:
	int i,j;
	for (i = 0; i < 6; i++) keyboard_loadDefaultColor(i); //Load all default colors!
	for (j = 0; j < 5; j++)
	{
		for (i = 0; i < (int)NUMITEMS(BIOS_Settings.input_settings.keyboard_gamemodemappings[0]); i++) //Process all keymappings!
		{
			BIOS_Settings.input_settings.keyboard_gamemodemappings[j][i] = -1; //Disable by default!
			BIOS_Settings.input_settings.keyboard_gamemodemappings_alt[j][i] = 0; //Disable by default!
			BIOS_Settings.input_settings.mouse_gamemodemappings[j][i] = 0; //Disable by default!
		}
		BIOS_Settings.input_settings.usegamingmode_joystick[j] = 0; //Disable by default!
	}
	BIOS_Settings.input_settings.usegamingmode_joystick[0] = 1; //Enable by default for compatiblity!
	BIOS_Settings.input_settings.gamingmode_joystick = 0; //Not using the joystick as input instead of normal gaming mode!
}

struct
{
byte input_buffer_shift;
int input_buffer;
byte input_buffer_enabled;
byte input_enabled;
} SAVED_KEYBOARD_STATUS;

void save_keyboard_status() //Save keyboard status to memory!
{
	lock(LOCK_INPUT);
	SAVED_KEYBOARD_STATUS.input_buffer_shift = input_buffer_shift;
	SAVED_KEYBOARD_STATUS.input_buffer = input_buffer;
	SAVED_KEYBOARD_STATUS.input_buffer_enabled = input_buffer_enabled;
	SAVED_KEYBOARD_STATUS.input_enabled = input_enabled; //Save all!
	unlock(LOCK_INPUT);
}

void load_keyboard_status() //Load keyboard status from memory!
{
	lock(LOCK_INPUT);
	input_buffer_shift = SAVED_KEYBOARD_STATUS.input_buffer_shift;
	input_buffer = SAVED_KEYBOARD_STATUS.input_buffer;
	input_buffer_enabled = SAVED_KEYBOARD_STATUS.input_buffer_enabled;
	input_enabled = SAVED_KEYBOARD_STATUS.input_enabled; //Load all that was saved!
	unlock(LOCK_INPUT);
}

void clearBuffers() //Clear any input buffers still filled!
{
	uint_32 i;
	for (i=0;i<NUMITEMS(emu_keys_state);i++) //Process all keys!
	{
		if (emu_keys_state[i]&1) //We're still pressed, even though the buffers need to be cleared?
		{
			emu_keys_state[i] |= 2; //Emulate the release of the key!
			handleKeyPressRelease(i); //Release the key, since we become unwanted data after we've reset, triggering a new key input instead of a new one
		}
	}
	oldshiftstatus = shiftstatus = currentshiftstatus_inputbuffer = 0; //Init all shifts to unused/not pressed!
	input_buffer_shift = input_buffer_mouse = 0; //Shift/mouse status: nothing pressed yet!
	input_buffer = last_input_key = -1; //Disable any output!
	lastkey = lastshift = oldMouse_buttons = 0; //Disable keyboard status, mouse buttons, leave x, y and set alone(not required to clear)!
	req_quit_gamingmode = req_enter_gamingmode = 0; //Not requesting quitting/entering the gaming mode anymore!
	ReleaseKeys(); //Release all keys still pressed, if possible!
	memset(&gamingmode_keys_pressed,0,sizeof(gamingmode_keys_pressed)); //Clear gaming mode keys pressed: we're all released!
}

void disableKeyboard() //Disables the keyboard/mouse functionality!
{
	lock(LOCK_INPUT);
	input_enabled = 0; //Disable input!
	input_buffer_enabled = 0; //Not buffering input!
	clearBuffers(); //Make sure our buffers are cleared!
	unlock(LOCK_INPUT);
}

void enableKeyboard(byte bufferinput) //Enables the keyboard/mouse functionnality param: to buffer into input_buffer(1=On any released key, 2=On any released key, except Ctrl/Alt/Shift)?!
{
	disableKeyboard(); //Make sure the keyboard if off to start with and all things are cleared!
	lock(LOCK_INPUT);
	input_buffer_enabled = bufferinput; //To buffer?
	input_enabled = ALLOW_INPUT; //Enable input!
	unlock(LOCK_INPUT);
}

/* All update functionality for input */

SDL_Joystick *joystick = NULL; //Our joystick!

word mouse_x=0, mouse_y=0; //Current mouse coordinates of the actual mouse!

void updateMOD()
{
	const float precisemovement = 0.5f; //Precise mouse movement constant!
	word minrange; //Minimum range, if used!
	if ((input.cas&CAS_RCTRL) && (!Direct_Input)) //Ctrl pressed, mapped to home?
	{
		input.Buttons |= BUTTON_HOME; //Pressed!
	}
	else
	{
		input.Buttons &= ~BUTTON_HOME; //Released!
	}

	if ((input.cas&CAS_RSHIFT) && ((!Direct_Input) && (!curstat.mode) && (!curstat.gamingmode))) //Shift pressed, mapped to mouse slowdown?
	{
		precisemousemovement = 1; //Enabled!
	}
	else
	{
		precisemousemovement = 0; //Disabled!
	}

	float axisx, axisy;
	axisx = axisy = 0.0f; //Init axis to none!

	if (input.keyboardjoy_direction & 4) //Left?
	{
		axisx -= 32768.0f; //Decrease
	}
	if (input.keyboardjoy_direction & 8) //Right?
	{
		axisx += 32767.0f; //Increase!
	}

	if (input.keyboardjoy_direction&1) //Up?
	{
		axisy -= 32768.0f; //Decrease
	}
	if (input.keyboardjoy_direction&2) //Down?
	{
		axisy += 32767.0f; //Increase!
	}

	//Now the basic axis is loaded!
	if (precisemousemovement && (axisx || axisy)) //Enable precise movement?
	{
		minrange = ((sword)BIOS_Settings.input_settings.analog_minrange << 8); //Minimum horizontal&vertical range!
		if (axisx!=0.0f) //Used?
		{
			if (axisx>0) //Positive?
			{
				axisx -= minrange; //Decrease into valid range!
				axisx *= precisemovement; //Enable precise movement to the applyable range!
				axisx += minrange; //Increase into valid range!
				axisx = (axisx<0.0f) ? 0.0f : ((axisx>SHRT_MAX) ? SHRT_MIN : axisx); //Clip the range!
			}
			else //Negative?
			{
				axisx += minrange; //Decrease into valid range!
				axisx *= precisemovement; //Enable precise movement to the applyable range!
				axisx -= minrange; //Increase into valid range!
				axisx = (axisx>0.0f)?0.0f:((axisx<SHRT_MIN)?SHRT_MIN:axisx); //Clip the range!
			}
		}
		if (axisy != 0.0f) //Used?
		{
			if (axisy>0) //Positive?
			{
				axisy -= minrange; //Decrease into valid range!
				axisy *= precisemovement; //Enable precise movement to the applyable range!
				axisy += minrange; //Increase into valid range!
				axisy = (axisy<0.0f) ? 0.0f : ((axisy>SHRT_MAX) ? SHRT_MIN : axisy); //Clip the range!
			}
			else //Negative?
			{
				axisy += minrange; //Decrease into valid range!
				axisy *= precisemovement; //Enable precise movement to the applyable range!
				axisy -= minrange; //Increase into valid range!
				axisy = (axisy>0.0f) ? 0.0f : ((axisy<SHRT_MIN) ? SHRT_MIN : axisy); //Clip the range!
			}
		}
	}
	input.Lx = (sword)axisx; //Horizontal axis!
	input.Ly = (sword)axisy; //Vertical axis!
}

byte DirectInput_Middle = 0; //Is direct input toggled by middle mouse button?

//Toggle direct input on/off!
void toggleDirectInput(byte cause)
{
	if (Direct_Input && (cause!=DirectInput_Middle)) return; //Disable toggling off with other methods than we originally started with!
	//OK to toggle on/off? Toggle direct input!
	Direct_Input = !Direct_Input;
	if (Direct_Input) //Enabled Direct Input?
	{
		DirectInput_Middle = cause; //Are we toggled on by the middle mouse button?
        #ifdef SDL2//#ifndef SDL2
			SDL_WM_GrabInput(SDL_GRAB_ON); //Grab the mouse!
			SDL_ShowCursor(SDL_DISABLE); //Don't show the cursor!
		#else
			SDL_SetRelativeMouseMode(SDL_TRUE); //SDL2: Enter relative mouse mode!
		#endif
	}
	else //Disabled?
	{
		DirectInput_Middle = 0; //Reset middle mouse button flag!
        #ifdef SDL2//#ifndef SDL2
			SDL_WM_GrabInput(SDL_GRAB_OFF); //Don't grab the mouse!
			SDL_ShowCursor(SDL_ENABLE); //Show the cursor!
		#else
			SDL_SetRelativeMouseMode(SDL_FALSE); //SDL2: Leave relative mouse mode!
		#endif
	}

	//Also disable mouse buttons pressed!
	Mouse_buttons &= ~3; //Disable left/right mouse buttons being input!
	clearBuffers(); //Make sure the buffers are cleared when toggling, so we start fresh!
}

byte haswindowactive = 7; //Are we displayed on-screen and other active/inactive flags! Bit0: 1=Not iconified, Bit1: 1=Not backgrounded, 2=Sound output enabled. Bit3=Discard time while set. Bit4=Time discarded on Sound Blaster recording.
byte hasmousefocus = 1; //Do we have mouse focus?
byte hasinputfocus = 1; //Do we have input focus?

extern byte SCREEN_CAPTURE; //Screen capture support!

#ifdef VGAIODUMP
//CGA/VGA dumping support!
extern DOUBLE VGA_debugtiming; //Debug countdown if applyable!
extern byte VGA_debugtiming_enabled; //Are we applying right now?
#endif

byte specialdebugger = 0; //Enable special debugger input?

int_32 whatJoystick = 0; //What joystick are we connected to? Default to the first joystick(for PSP compatibility)!

OPTINLINE byte getjoystick(SDL_Joystick *joystick, int_32 which) //Are we a supported joystick? If supported, what joystick?
{
	char name[256]; //The name of the joystick!
	if (!joystick) return 0; //No joystick connected?
	if (!SDL_NumJoysticks()) return 0; //No joysticks connected after all!
	#ifdef IS_PSP
		return (which==0)?1:0; //Is PSP always (name is unreliable?)!
	#endif
	#ifdef IS_SWITCH
		return ((SDL_JoystickInstanceID(joystick)==0) && (which==0))?1:0; //Our default joystick?
	#endif
	
    #ifndef SDL2//#ifdef SDL2
	if ((which!=-1) && (SDL_JoystickInstanceID(joystick)!=which)) return 0; //Not our joystick!
	#endif
	memset(&name,0,sizeof(name)); //Init!
	
    #ifdef SDL2//#ifndef SDL2
	safestrcpy(name,sizeof(name),SDL_JoystickName(whatJoystick)); //Get the joystick name, with max length limit!
	#else
	safestrcpy(name,sizeof(name),SDL_JoystickName(joystick)); //Get the joystick name, with max length limit!
	#endif
	name[255] = '\0'; //End of string safety!
	
    #ifdef IS_VITA
	if ((!strcmp(name, "PSVita Controller"))) //PS Vita controller?
	{
		return 1; //Is Vita controller!
	}
	#endif
	
    #ifdef IS_SWITCH
	if ((!strcmp(name, "Switch Controller"))) //Switch controller?
	{
		return 1; //Is Switch controller!
	}
	#endif
	
    #ifdef IS_WINDOWS
		if ((!strcmp(name,"XBOX 360 For Windows (Controller)")) || (!strcmp(name, "Controller (XBOX 360 For Windows)")) || //SDL1 controller names!
			(!strcmp(name,"XInput Controller #1")) //From SDL2!
			) //XBox 360 controller?
	#else
	
    #ifdef IS_LINUX
		if ((!strcmp(name, "Microsoft X-Box 360 pad")) || (!strcmp(name, "XInput Controller #1"))) //XBox 360 controller?
	#else
		if (0) //Unsupported system!
	#endif
	#endif
	{
		return 2; //Is XBox 360 controller!
	}
	else //Unknown/unsupported controller?
	{
		return 0; //Not supported!
	}
}

//A joystick has been disconnected!
void disconnectJoystick(int_32 index)
{
	if (joystick) //A joystick is connected?
	{
        #ifdef SDL2//#ifndef SDL2
		if (whatJoystick == index) //Our joystick is disconnected?
		#else
		if (SDL_JoystickInstanceID(joystick) == index) //Our joystick is disconnected?
		#endif
		{
			lock(LOCK_INPUT); //We're clearing all our relevant data!
			SDL_JoystickClose(joystick); //Disconnect our joystick!
			input.Buttons = 0;
			input.Lx = input.Ly = 0;
			joystick = NULL; //Removed!
			unlock(LOCK_INPUT);
		}
	}
}

//A joystick has been connected!
void connectJoystick(int index)
{
	if (joystick) //Joystick connected?
	{
		SDL_JoystickClose(joystick); //Disconnect our old joystick!
		lock(LOCK_INPUT); //We're clearing all our relevant data!
		input.Buttons = 0;
		input.Lx = input.Ly = 0;
		unlock(LOCK_INPUT);
		joystick = NULL; //Removed!
	}
	lock(LOCK_INPUT);
	joystick = SDL_JoystickOpen(index); //Open the new joystick as new input device!
    #ifndef SDL2//#ifdef SDL2
	if (joystick) //Loaded?
	{
		whatJoystick = SDL_JoystickInstanceID(joystick); //Set our joystick to this joystick!
	}
	#endif
	unlock(LOCK_INPUT);
}

void reconnectJoystick0() //For non-SDL2 compilations!
{
		disconnectJoystick(whatJoystick); //Disconnect the joystick!
		if (SDL_NumJoysticks()) //Any joystick attached?
		{
			connectJoystick(!whatJoystick); //Connect the new joystick(either joystick #0 or no joystick)!
		}
		else
		{
			connectJoystick(0); //Connect the new joystick(always joystick #0)!
		}
}

extern word window_xres;
extern word window_yres;

OPTINLINE word getxres()
{
	return window_xres;
}

OPTINLINE word getyres()
{
	return window_yres;
}

#ifdef SDL2//#ifndef SDL2
typedef int_64 SDL_FingerID; //Finger ID type!
#endif

sword lastxy[0x100][2]; //Last coordinates registered!
byte ismovablexy[0x100]; //Movable?
extern float render_xfactor, render_yfactor; //X and Y factor during rendering!

void touch_fingerDown(float x, float y, SDL_FingerID fingerId)
{
	byte blockmovement;
	//Convert the touchId and fingerId to finger! For now, allow only one finger!
	lock(LOCK_INPUT);
	blockmovement = (GPU_mousebuttondown((word)(getxres() * x), (word)(getyres() * y), (fingerId & 0xFF))); //We're released at the current coordinates!
	int_64 relxfull, relyfull;
	relxfull = (int_64)((float)getxres()*x); //X coordinate on the screen!
	relyfull = (int_64)((float)getyres()*y); //Y coordinate on the screen!
	fingerId &= 0xFF; //Only use lower 8-bits to limit us to a good usable range!
	lastxy[fingerId][0] = (sword)relxfull; //Save the last coordinate for getting movement!
	lastxy[fingerId][1] = (sword)relyfull; //Save the last coordinate for getting movement!
	byte buttonarea=0;
	if ((relxfull>=keyboardsurface->xdelta) && (relyfull>=keyboardsurface->ydelta)) //Within range?
	{
		relxfull -= keyboardsurface->xdelta;
		relyfull -= keyboardsurface->ydelta;
		relxfull = (int_64)(((float)relxfull)*render_xfactor); //X pixel!
		relyfull = (int_64)(((float)relyfull)*render_yfactor); //Y pixel!
		if ((relxfull<GPU_TEXTPIXELSX) && (relyfull<GPU_TEXTPIXELSY)) //Within range?
		{
			relxfull = (int_64)(((word)(relxfull))>>3); //X pixel!
			relyfull = (int_64)(((word)(relyfull))>>3); //Y pixel!
			buttonarea = ((word)relxfull<firstsplitx)?1:(((word)relxfull<secondsplitx)?(((word)relyfull<thirdsplity)?3:0):2); //Are we a button? 0=None, 1=Left, 2=Right, 3=Middle
		}
	}
	ismovablexy[fingerId] = ((buttonarea==0) && (blockmovement==0)); //Movable when fully handled and not blocked?
	updateFingerOSK();
	unlock(LOCK_INPUT);
}

void touch_fingerUp(float x, float y, SDL_FingerID fingerId)
{
	//Convert the touchId and fingerId to finger! For now, allow only one finger!
	lock(LOCK_INPUT);
	GPU_mousebuttonup((word)(getxres()*x), (word)(getyres()*y), (fingerId & 0xFF)); //We're released at the current coordinates!
	updateFingerOSK();
	unlock(LOCK_INPUT);
}

void touch_fingerMotion(float relx, float rely, SDL_FingerID fingerId)
{
	//Convert the touchId and fingerId to finger! For now, allow only one finger!
	//Fingermotion uses dx,dy to indicate movement, fingerdown/fingerup declares hold/release!
	//For now, move the mouse!
	int_64 relxfull, relyfull;
	relxfull = (int_64)((float)getxres()*relx); //X coordinate on the screen!
	relyfull = (int_64)((float)getyres()*rely); //Y coordinate on the screen!

	fingerId &= 0xFF; //Only use lower 8-bits to limit us to a good usable range!
	lock(LOCK_INPUT);
	if (Direct_Input) //Direct input? Move the mouse in the emulator itself!
	{
		if (ismovablexy[fingerId]) //Movable finger?
		{
			mouse_xmove += (relxfull-lastxy[fingerId][0])*GPU_xDTM; //Move the mouse horizontally, in mm!
			mouse_ymove += (relyfull-lastxy[fingerId][1])*GPU_yDTM; //Move the mouse vertically, in mm!
			mouse_xmovemickey += (relxfull-lastxy[fingerId][0])*GPU_xDTmickey; //Move the mouse horizontally, in mm!
			mouse_ymovemickey += (relyfull-lastxy[fingerId][1])*GPU_yDTmickey; //Move the mouse vertically, in mm!
		}
	}
	GPU_mousemove((word)relxfull, (word)relyfull, (fingerId & 0xFF)); //We're moved to the current coordinates!

	lastxy[fingerId][0] = (sword)relxfull; //Save the last coordinate for getting movement!
	lastxy[fingerId][1] = (sword)relyfull; //Save the last coordinate for getting movement!
	unlock(LOCK_INPUT);
}

#ifdef PELYAS_SDL
byte touchstatus[0x10] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}; //All Pelya finger statuses!
float touchscreencoordinates_x[0x10] = {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f}; //Current screen coordinates of each finger!
float touchscreencoordinates_y[0x10] = {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f}; //Current screen coordinates of each finger!
#endif

#if defined(ANDROID) || SDL_VERSION_ATLEAST(2,0,4)
extern byte needvideoupdate; //For resolution updates!
#endif

byte RDP = 0, RDPDelta = 0;

extern byte window_moved; //Has this window been moved(Owned by the GPU)?
extern uint_32 window_x,window_y; //Set location when moved!
extern byte allowInput; //Do we allow input logging?

void remap_keys(int *key, int *extrakeyhandler, byte status)
{
	*extrakeyhandler = -1; //Default: no extra handler!
	switch (*key) //What key needs remapping?
	{
	case 0x33: //RALT?
		if (BIOS_Settings.input_settings.DirectInput_Disable_RALT && status) //Disabled and pressed?
		{
			remap_ralt_active = status; //Are we starting to remap to nothing?
			*key = -1; //Disable the key from being pressed!
		}
		else
		{
			remap_ralt_active = 0; //Are we starting to remap to LWIN?
			//Allow the key to be released always, even when it was pressed(or pressed too, when the disable setting isn't set)!
		}
		//Otherwise, plain RALT!
		break;
	case 0x2F: //LALT?
		if (BIOS_Settings.input_settings.DirectInput_remap_accentgrave_to_tab && status) //Disabled and pressed?
		{
			remap_lalt_active = status; //Are we starting to remap to Accent Grave?
		}
		else
		{
			remap_lalt_active = 0; //Are we starting to remap Accent Grave to TAB?
			//Allow the key to be released always, even when it was pressed(or pressed too, when the disable setting isn't set)!
		}
		//Otherwise, plain LALT!
		break;
	case 0x31: //RCTRL?
		if (BIOS_Settings.input_settings.DirectInput_remap_RCTRL_to_LWIN) //Remapped to LWIN?
		{
			*key = 0x2E; //Remap to LWIN!
			remap_lwin_active = status; //Are we starting to remap to LWIN?
		}
		else
		{
			if (remap_lwin_active) //LWin is active?
			{
				*extrakeyhandler = 0x2E; //Release LWIN as well!
				remap_lwin_active = status; //Are we starting to remap to LWIN?
			}
		}
		//Otherwise, plain RCTRL!
		break;
	case 0x24: //`?
		if (BIOS_Settings.input_settings.DirectInput_remap_accentgrave_to_tab && remap_lalt_active) //Fully remapping to TAB?
		{
			remap_tab_active = status; //Remap to TAB!
			*key = 0x2A; //Handle the remapping to TAB fully as well!
		}
		else //Functionality disabled or released "`"-key?
		{
			if (remap_tab_active) //Disabled and we we're pressing it to map to TAB?
			{
				*extrakeyhandler = 0x2A; //Press/Release TAB as well!
				remap_tab_active = status; //This happens to the tab remapping as well!
			}
		}
		break;
	case 0x58: //NUM0?
		if (BIOS_Settings.input_settings.DirectInput_remap_NUM0_to_Delete) //Fully remapping to TAB?
		{
			remap_delete_active = status; //Remap to TAB!
			*key = 0x4A; //Handle the remapping to Delete fully as well!
		}
		else //Functionality disabled or released "`"-key?
		{
			if (remap_delete_active) //Disabled and we we're pressing it to map to TAB?
			{
				*extrakeyhandler = 0x4A; //Press/Release Delete as well!
				remap_delete_active = status; //This happens to the tab remapping as well!
			}
		}
		break;
	case 0x2E: //LGUI?
	case 0x2A: //TAB?
	case 0x4A: //Delete?
		//Passthrough normally!
		break;
	default: //No remapping!
		break;	
	}
}

byte CTRLALTGRdetection = 0;
SDL_Event CTRLALTGRdetection_CtrlEvent; //A backup of the control event when it's supposed to re-fire!

byte alwaysdetectjoysticks = 1; //Always detect joysticks?

void preUpdateInput()
{
	CTRLALTGRdetection = 0; //Reset detection of ctrl-altgr!
}

void postUpdateInput()
{
	if (CTRLALTGRdetection) //Detected LCTRL but not LALT?
	{
		updateInput(&CTRLALTGRdetection_CtrlEvent); //Send the LCTRL event again, but this time it's activated!
	}
	CTRLALTGRdetection = 0; //Reset detection of ctrl-altgr!
}

void updateInput(SDL_Event *event) //Update all input!
{
	byte joysticktype=0; //What joystick type?
	static byte RALT = 0;
	switch (event->type)
	{
	//Keyboard events
	case SDL_KEYUP: //Keyboard up?
		lock(LOCK_INPUT); //Wait!
		#if !defined(IS_VITA) && !defined(IS_SWITCH)
		if (((!(getjoystick(joystick, -1))) || Direct_Input) && hasinputfocus) //Gotten no joystick or is direct input?
		#else
		//Ignore the joystick on the Vita/Switch, which is always connected!
		if (hasinputfocus) //Just focus?
		#endif
		{
			switch (event->key.keysym.sym) //What key?
			{
				//Special first
			case SDLK_LCTRL: //LCTRL!
				input.cas &= ~CAS_LCTRL; // Released!
				break;
			case SDLK_RCTRL: //RCTRL!
				input.cas &= ~CAS_RCTRL; //Released!
				break;
			case SDLK_LALT: //LALT!
				input.cas &= ~CAS_LALT; //Released!
				break;
			case SDLK_RALT: //RALT!
				input.cas &= ~CAS_RALT; //Pressed!
				RALT = 0; //RALT is released!
				break;
			case SDLK_LSHIFT: //LSHIFT!
				input.cas &= ~CAS_LSHIFT; //Pressed!
				break;
			case SDLK_RSHIFT: //RSHIFT!
				input.cas &= ~CAS_RSHIFT; //Pressed!
				break;

				//Normal keys
			case SDLK_BACKSLASH: //HOLD?
				input.Buttons &= ~BUTTON_HOLD; //Pressed!
				break;
#if !defined(ANDROID) && !defined(IS_VITA) && !defined(IS_SWITCH)
			case SDLK_BACKSPACE: //SELECT?
#else
			case SDLK_EQUALS: //SELECT? Remapped on Android!
#endif
				input.Buttons &= ~BUTTON_SELECT; //Pressed!
				break;
#if !defined(ANDROID) && !defined(IS_VITA) && !defined(IS_SWITCH)
				//Android doesn't have start, so it's ignored!
			case SDLK_RETURN: //START?
#else
			case SDLK_QUOTE: //Remapped on Android!
#endif
				input.Buttons &= ~BUTTON_START; //Pressed!
				if (RALT) //RALT pressed too? Doubles as emulator fullscreen toggle!
				{
					GPU.fullscreen = !GPU.fullscreen; //Toggle fullscreen!
					updateVideo(); //Force an update of video!
				}
				break;
			case SDLK_UP: //UP?
				input.Buttons &= ~BUTTON_UP; //Pressed!
				break;
			case SDLK_DOWN: //DOWN?
				input.Buttons &= ~BUTTON_DOWN; //Pressed!
				break;
			case SDLK_LEFT: //LEFT?
				input.Buttons &= ~BUTTON_LEFT; //Pressed!
				break;
			case SDLK_RIGHT: //RIGHT?
				input.Buttons &= ~BUTTON_RIGHT; //Pressed!
				break;
			case SDLK_q: //LTRIGGER?
				input.Buttons &= ~BUTTON_LTRIGGER; //Pressed!
				break;
			case SDLK_w: //RTRIGGER?
				input.Buttons &= ~BUTTON_RTRIGGER; //Pressed!
				break;
			case SDLK_i: //Joy up?
				input.keyboardjoy_direction &= ~1; //Up!
				break;
			case SDLK_j: //Joy left?
				input.keyboardjoy_direction &= ~4; //Left!
				break;
			case SDLK_k: //Joy down?
				input.keyboardjoy_direction &= ~2; //Down!
				break;
			case SDLK_l: //Joy right?
				input.keyboardjoy_direction &= ~8; //Down!
				break;
#ifdef SDL2//#ifndef SDL2
#if defined(ANDROID) || defined(IS_VITA) || defined(IS_SWITCH)
				//Android uses space too!
			case SDLK_SPACE:
#endif
			case SDLK_KP8: //TRIANGLE?
#else
#if defined(ANDROID) || defined(IS_VITA) || defined(IS_SWITCH)
				//Android uses space too!
			case SDLK_SPACE:
#endif
			case SDLK_KP_8: //TRIANGLE?
#endif
				input.Buttons &= ~BUTTON_TRIANGLE; //Pressed!
				break;
#ifdef SDL2//#ifndef SDL2
			case SDLK_KP4: //SQUARE?
#else
			case SDLK_KP_4: //SQUARE?
#endif
				input.Buttons &= ~BUTTON_SQUARE; //Pressed!
				break;
			case SDLK_ESCAPE:
#ifdef SDL2//#ifndef SDL2
#if defined(ANDROID) || defined(IS_VITA) || defined(IS_SWITCH)
				//Android uses escape&backspace too!
			case SDLK_BACKSPACE:
#endif
#else
#if defined(ANDROID) || defined(IS_VITA) || defined(IS_SWITCH)
				//Android uses escape&backspace too!
			case SDLK_BACKSPACE:
#endif
#endif
				input.Buttons &= ~BUTTON_CANCEL; //Released!
				break;
#ifdef SDL2//#ifndef SDL2
			case SDLK_KP6: //CIRCLE?
#else
			case SDLK_KP_6: //CIRCLE?
#endif
				input.Buttons &= ~BUTTON_CIRCLE; //Released!
				break;
#ifdef SDL2//#ifndef SDL2
#if defined(ANDROID) || defined(IS_VITA) || defined(IS_SWITCH)
				//Android uses return too!
			case SDLK_RETURN:
#endif
#else
#if defined(ANDROID) || defined(IS_VITA) || defined(IS_SWITCH)
				//Android uses return too!
			case SDLK_RETURN:
#endif
#endif
				input.Buttons &= ~BUTTON_CONFIRM; //Pressed!
				break;
#ifdef SDL2//#ifndef SDL2
			case SDLK_KP2: //CROSS?
#else
			case SDLK_KP_2: //CROSS?
#endif
				input.Buttons &= ~BUTTON_CROSS; //Pressed!
				break;
				//Special emulator shortcuts?
			case SDLK_F4: //F4?
				if (RALT) //ALT-F4?
				{
					unlock(LOCK_INPUT);
					goto quitting; //Redirect to quitting(SDL_QUIT) event!
				}
				break;
			case SDLK_F5: //F5? Use F5 for simple compatiblity with Dosbox users. Screen shot!
				if (RALT) //ALT-F5?
				{
					SCREEN_CAPTURE = 1; //Do a screen capture next frame!
				}
				break;
			case SDLK_F6: //F6? Use F6 for simple compatiblity with Dosbox users. Start/stop sound recording!
				if (RALT) //ALT-F6?
				{
					unlock(LOCK_MAINTHREAD); //We're not doing anything right now!
					BIOS_SoundStartStopRecording(); //Start/stop recording!
					lock(LOCK_MAINTHREAD); //Relock us!
				}
				break;
			case SDLK_F9: //F9? Used to kill Dosbox. Since we use F4 for that, do special actions for debugging errors!
				if (RALT) //ALT-F9?
				{
#ifdef VGAIODUMP
					//Start the VGA I/O dump now!
					VGA_debugtiming = 0.0f; //Reset the counter as well!#endif
					VGA_debugtiming_enabled = 1; //Start dumping!
#endif
					specialdebugger = !specialdebugger; //Toggle special debugger input?
				}
				break;
			case SDLK_F10: //F10? Use F10 for simple compatiblity with Dosbox users.
				if (RALT) //ALT-F10?
				{
					toggleDirectInput(2); //Toggle direct input alternative without mouse!
				}
				break;
			default: //Unknown?
				break;
			}
			if (event->key.keysym.scancode == 34) //Play/pause?
			{
				input.Buttons &= ~BUTTON_PLAY; //Play button!
			}
			if (event->key.keysym.scancode == 36) //Stop?
			{
				input.Buttons &= ~BUTTON_STOP; //Stop button!
			}
			if (Direct_Input)
			{
				input.Buttons = 0; //Ignore pressed buttons!
				input.cas = 0; //Ignore pressed buttons!
				//Handle button press/releases!
				int key,extrakey;
#ifdef SDL2//#ifndef SDL2
				INLINEREGISTER int index;
				index = signed2unsigned16(event->key.keysym.sym); //Load the index to use!
				if (index < (int)NUMITEMS(emu_keys_sdl_rev)) //Valid key to lookup?
				{
					if ((key = emu_keys_sdl_rev[index]) != -1) //Valid key?
					{
						remap_keys(&key,&extrakey,0);
						if (key != -1) //Valid key?
						{
							if (emu_keys_state[key] & 1) //We're pressed at all?
							{
								emu_keys_state[key] |= 2; //We're released!
								handleKeyPressRelease(key); //Handle release immediately!
							}
						}
						if (extrakey!=-1) //Extra key specified?
						{
							if (emu_keys_state[extrakey] & 1) //We're pressed at all?
							{
								emu_keys_state[extrakey] |= 2; //We're released!
								handleKeyPressRelease(extrakey); //Handle release immediately!
							}
						}
					}
				}
#else
				//SDL2?
				key = emu_keys_sdl_rev(event->key.keysym.sym); //Load the index to use!
				if (key != -1) //Valid key?
				{
					remap_keys(&key,&extrakey,0);
					if (key != -1) //Valid key?
					{
						if (emu_keys_state[key] & 1) //We're pressed at all?
						{
							emu_keys_state[key] |= 2; //We're released!
							handleKeyPressRelease(key); //Handle release immediately!
						}
					}
					if (extrakey!=-1) //Extra key specified?
					{
						if (emu_keys_state[extrakey] & 1) //We're pressed at all?
						{
							emu_keys_state[extrakey] |= 2; //We're released!
							handleKeyPressRelease(extrakey); //Handle release immediately!
						}
					}
				}
#endif
			}
			updateMOD(); //Update rest keys!
		}
		unlock(LOCK_INPUT);
		break;
	case SDL_KEYDOWN: //Keyboard down?
		lock(LOCK_INPUT);
		#if !defined(IS_VITA) && !defined(IS_SWITCH)
		if (((!getjoystick(joystick, -1)) || Direct_Input) && hasinputfocus) //Gotten no joystick or is direct input?
		#else
		//Ignore the joystick on the Vita/Switch, which is always connected!
		if (hasinputfocus) //Just focus?
		#endif
		{
        #ifndef SDL2//#ifdef SDL2
			if ((event->key.keysym.sym!=SDLK_LCTRL) && event->key.repeat) //Repeating key when not left ctrl (required for the altgr detection)?
			{
				unlock(LOCK_INPUT); //Finish up!
				return; //Ignore repeating keys! 
			}
			#endif
			switch (event->key.keysym.sym) //What key?
			{
			//Special first
			case SDLK_LCTRL: //LCTRL!
				if (CTRLALTGRdetection == 0) //Not detecting it yet?
				{
					CTRLALTGRdetection = 1; //Start Ctrl-Altgr detection!
					memcpy(&CTRLALTGRdetection_CtrlEvent, event, sizeof(*event)); //Make a copy of the event!
					unlock(LOCK_INPUT); //Finish up!
					return; //Abort!
				}
                #ifndef SDL2//#ifdef SDL2
				if (input.cas&CAS_LCTRL) //Check for repeat manually, as SDL can't be trusted in RALT cases!
				{
					unlock(LOCK_INPUT); //Finish up!
					return; //Ignore repeating keys! 
				}
				#endif
				input.cas |= CAS_LCTRL; //Pressed!
				break;
			case SDLK_RCTRL: //RCTRL!
				input.cas |= CAS_RCTRL; //Pressed!
				break;
			case SDLK_LALT: //LALT!
				input.cas |= CAS_LALT; //Pressed!
				break;
			case SDLK_RALT: //RALT!
				if (CTRLALTGRdetection) //CTRL+Altgr detected at the same time?
				{
					CTRLALTGRdetection = 0; //Stop it's detection!
				}
				RALT = 1; //RALT is pressed!
				input.cas |= CAS_RALT; //Pressed!
				break;
			case SDLK_LSHIFT: //LSHIFT!
				input.cas |= CAS_LSHIFT; //Pressed!
				break;
			case SDLK_RSHIFT: //RSHIFT!
				input.cas |= CAS_RSHIFT; //Pressed!
				break;

			case SDLK_BACKSLASH: //HOLD?
				input.Buttons |= BUTTON_HOLD; //Pressed!
				break;
#if !defined(ANDROID) && !defined(IS_VITA) && !defined(IS_SWITCH)
			case SDLK_BACKSPACE: //SELECT? Not used on Android!
#else
			case SDLK_EQUALS: //SELECT? Remapped on Android!
#endif
				input.Buttons |= BUTTON_SELECT; //Pressed!
				break;
#if !defined(ANDROID) && !defined(IS_VITA) && !defined(IS_SWITCH)
			case SDLK_RETURN: //START?
#else
			case SDLK_QUOTE: //Remapped on Android?
#endif
	//Android already has this mapped!
				if (RALT) //ALT-ENTER?
				{
					unlock(LOCK_INPUT);
					return; //Ignore the input: we're reserved!
				}
				input.Buttons |= BUTTON_START; //Pressed!
				break;
			case SDLK_UP: //UP?
				input.Buttons |= BUTTON_UP; //Pressed!
				break;
			case SDLK_DOWN: //DOWN?
				input.Buttons |= BUTTON_DOWN; //Pressed!
				break;
			case SDLK_LEFT: //LEFT?
				input.Buttons |= BUTTON_LEFT; //Pressed!
				break;
			case SDLK_RIGHT: //RIGHT?
				input.Buttons |= BUTTON_RIGHT; //Pressed!
				break;
			case SDLK_q: //LTRIGGER?
				input.Buttons |= BUTTON_LTRIGGER; //Pressed!
				break;
			case SDLK_w: //RTRIGGER?
				input.Buttons |= BUTTON_RTRIGGER; //Pressed!
				break;
			case SDLK_i: //Joy up?
				input.keyboardjoy_direction |= 1; //Up!
				break;
			case SDLK_j: //Joy left?
				input.keyboardjoy_direction |= 4; //Left!
				break;
			case SDLK_k: //Joy down?
				input.keyboardjoy_direction |= 2; //Down!
				break;
			case SDLK_l: //Joy right?
				input.keyboardjoy_direction |= 8; //Down!
				break;
#ifdef SDL2//#ifndef SDL2
#if defined(ANDROID) || defined(IS_VITA) || defined(IS_SWITCH)
				//Android uses space too!
			case SDLK_SPACE:
#endif
			case SDLK_KP8: //TRIANGLE?
#else
#if defined(ANDROID) || defined(IS_VITA) || defined(IS_SWITCH)
				//Android uses space too!
			case SDLK_SPACE:
#endif
			case SDLK_KP_8: //TRIANGLE?
#endif
				input.Buttons |= BUTTON_TRIANGLE; //Pressed!
				break;
#ifdef SDL2//#ifndef SDL2
			case SDLK_KP4: //SQUARE?
#else
			case SDLK_KP_4: //SQUARE?
#endif
				input.Buttons |= BUTTON_SQUARE; //Pressed!
				break;
			case SDLK_ESCAPE:
#ifdef SDL2//#ifndef SDL2
#if defined(ANDROID) || defined(IS_VITA) || defined(IS_SWITCH)
				//Android uses escape&backspace too!
			case SDLK_BACKSPACE:
#endif
#else
#if defined(ANDROID) || defined(IS_VITA) || defined(IS_SWITCH)
				//Android uses escape&backspace too!
			case SDLK_BACKSPACE:
#endif
#endif
				input.Buttons |= BUTTON_CANCEL; //Pressed!
				break;
#ifdef SDL2//#ifndef SDL2
			case SDLK_KP6: //CIRCLE?
#else
			case SDLK_KP_6: //CIRCLE?
#endif
				input.Buttons |= BUTTON_CIRCLE; //Pressed!
				break;
#ifdef SDL2//#ifndef SDL2
#if defined(ANDROID) || defined(IS_VITA) || defined(IS_SWITCH)
				//Android uses return too!
			case SDLK_RETURN:
#endif
#else
#if defined(ANDROID) || defined(IS_VITA) || defined(IS_SWITCH)
				//Android uses return too!
			case SDLK_RETURN:
#endif
#endif
				input.Buttons |= BUTTON_CONFIRM; //Pressed!
				break;
#ifdef SDL2//#ifndef SDL2
			case SDLK_KP2: //CROSS?
#else
			case SDLK_KP_2: //CROSS?
#endif
				input.Buttons |= BUTTON_CROSS; //Pressed!
				break;
				//Special emulator shortcuts!
			case SDLK_F4: //F4?
			case SDLK_F5: //F5? Use F5 for simple compatiblity with Dosbox users. Screen shot!
			case SDLK_F6: //F6? Use F6 for simple compatiblity with Dosbox users. Start/stop sound recording!
			case SDLK_F9: //F9? Used to kill Dosbox. Since we use F4 for that, do special actions for debugging errors!
			case SDLK_F10: //F10? Use F10 for simple compatiblity with Dosbox users.
				if (RALT) //ALT combination? We're reserved input for special actions!
				{
					unlock(LOCK_INPUT);
					return; //Ignore the input: we're reserved!
				}
				break;
			default: //Unknown key?
				break;
			}
			if (event->key.keysym.scancode == 34) //Play/pause?
			{
				input.Buttons |= BUTTON_PLAY; //Play button!
			}
			if (event->key.keysym.scancode == 36) //Stop?
			{
				input.Buttons |= BUTTON_STOP; //Stop button!
			}
			if (Direct_Input)
			{
				input.Buttons = 0; //Ignore pressed buttons!
				input.cas = 0; //Ignore pressed buttons!

				//Handle button press/releases!
				int key,extrakey;
#ifdef SDL2//#ifndef SDL2
				INLINEREGISTER int index;
				index = signed2unsigned16(event->key.keysym.sym); //Load the index to use!
				if (index < (int)NUMITEMS(emu_keys_sdl_rev)) //Valid key to lookup?
				{
					if ((key = emu_keys_sdl_rev[index]) != -1) //Valid key?
					{
						remap_keys(&key,&extrakey,1);
						if (key != -1) //Valid key?
						{
							emu_keys_state[key] = 1; //We're pressed from now on, not released!
							if (input_enabled && ALLOW_INPUT) //Input enabled? Then we allow key presses!
							{
								handleKeyPressRelease(key); //Handle key press or release immediately!
							}
						}
						if (extrakey!=-1) //Extra key specified?
						{
							emu_keys_state[extrakey] = 1; //We're pressed from now on, not released!
							if (input_enabled && ALLOW_INPUT) //Input enabled? Then we allow key presses!
							{
								handleKeyPressRelease(extrakey); //Handle key press or release immediately!
							}
						}
					}
				}
#else
				//SDL2?
				key = emu_keys_sdl_rev(event->key.keysym.sym); //Load the index to use!
				if (key != -1) //Valid key?
				{
					remap_keys(&key,&extrakey,1);
					if (key != -1) //Valid key?
					{
						emu_keys_state[key] = 1; //We're pressed from now on, not released!
						if (input_enabled && ALLOW_INPUT) //Input enabled? Then we allow key presses!
						{
							handleKeyPressRelease(key); //Handle key press or release immediately!
						}
					}
					if (extrakey!=-1) //Extra key specified?
					{
	 					emu_keys_state[extrakey] = 1; //We're pressed from now on, not released!
						if (input_enabled && ALLOW_INPUT) //Input enabled? Then we allow key presses!
						{
							handleKeyPressRelease(extrakey); //Handle key press or release immediately!
						}
					}
				}
#endif
			}
			updateMOD(); //Update rest keys!
		}
		unlock(LOCK_INPUT);
		break;
		//Joystick events
	case SDL_JOYAXISMOTION:  /* Handle Joystick Motion */
		joysticktype = getjoystick(joystick, event->jaxis.which); //What joystick are we, if plugged in?
		if (joysticktype && (hasinputfocus || (alwaysdetectjoysticks == 0))) //Gotten a joystick that's supported?
		{
			lock(LOCK_INPUT);
			switch (joysticktype)
			{
			case 1: //PSP/PS Vita?
				switch (event->jaxis.axis)
				{
				case 0: /* Left-right movement code goes here */
					input.Lx = event->jaxis.value; //New value!
					break;
				case 1: /* Up-Down movement code goes here */
					input.Ly = event->jaxis.value; //New value!
					break;
				//PS Vita 2-3(right stick) and 4-5 unused.
				default:
					break;
				}
				break;
			case 2: //XBox 360?
				switch (event->jaxis.axis)
				{
					//Top-left stick?
				case 0: /* Left-right movement of the left stick */
					input.Lx = event->jaxis.value; //New value!
					break;
				case 1: /* Up-Down movement code of the left stick */
					input.Ly = event->jaxis.value; //New value!
					break;
				case 4: /* Left-right movement of the right stick */
					input.Lx2 = event->jaxis.value; //New value!
					break;
				case 3: /* Up-Down movement code of the right stick */
					input.Ly2 = event->jaxis.value; //New value!
					break;
				case 2: /* LT/RT pressed/depressed? */
					input.LxRudder = event->jaxis.value; //New value!
					break;
				default:
					break;
				}
				break;
			default:
				break;
			}
			unlock(LOCK_INPUT);
		}
		if (Direct_Input) //Direct input enabled? Ignore the joystick!
		{
			input.Lx = input.Ly = input.Lx2 = input.Ly2 = input.LxRudder = 0; //Ignore pressed buttons!
		}
		break;
	case SDL_JOYHATMOTION: /* Handle joy hat motion */
		joysticktype = getjoystick(joystick, event->jhat.which); //What joystick are we, if plugged in?
		if (joysticktype && (hasinputfocus || (alwaysdetectjoysticks == 0))) //Gotten a joystick that's supported?
		{
			lock(LOCK_INPUT);
			switch (joysticktype)
			{
			case 1: //PSP?
				//PSP doesn't have a joystick hat?
				break;
			case 2: //XBox 360?
				switch (event->jhat.hat) //What hat?
				{
				case 0: /* First hat */
					switch (event->jhat.value) //What hat position?
					{
					case SDL_HAT_LEFT: //Left?
						input.Buttons &= ~(BUTTON_RIGHT | BUTTON_UP | BUTTON_DOWN); //No buttons used!
						input.Buttons |= BUTTON_LEFT; //Left pressed!
						break;
					case SDL_HAT_RIGHT: //Right?
						input.Buttons &= ~(BUTTON_LEFT | BUTTON_UP | BUTTON_DOWN); //No buttons used!
						input.Buttons |= BUTTON_RIGHT; //Right pressed!
						break;
					case SDL_HAT_UP: //Up?
						input.Buttons &= ~(BUTTON_LEFT | BUTTON_RIGHT | BUTTON_DOWN); //No buttons used!
						input.Buttons |= BUTTON_UP; //Up pressed!
						break;
					case SDL_HAT_DOWN: //Down?
						input.Buttons &= ~(BUTTON_LEFT | BUTTON_RIGHT | BUTTON_UP); //No buttons used!
						input.Buttons |= BUTTON_DOWN; //Down pressed!
						break;
					default: //Unknown hat or unused hat position?
						input.Buttons &= ~(BUTTON_LEFT | BUTTON_RIGHT | BUTTON_UP | BUTTON_DOWN); //No buttons used!
						break;
					}
					break;
				default:
					break;
				}
				break;
			default:
				break;
			}
			unlock(LOCK_INPUT);
		}
		if (Direct_Input) //Direct input enabled? Ignore the joystick!
		{
			input.Lx = input.Ly = 0; //Ignore pressed buttons!
		}
		break;
	case SDL_JOYBUTTONDOWN:  /* Handle Joystick Button Presses */
#ifdef PELYAS_SDL
		if (event->jbutton.which == 0)
		{
			if (event->jbutton.button < 0x10) //Valid button protection?
			{
				if (!touchstatus[event->jbutton.button]) //Not touched yet?
				{
					touchstatus[event->jbutton.button] = 2; //We're pending to be touched!
				}
			}
		}
		break; //Disable normal joystick handling: this is disabled!
#endif
		joysticktype = getjoystick(joystick, event->jbutton.which); //What joystick are we, if plugged in?
		if (joysticktype && (hasinputfocus || (alwaysdetectjoysticks == 0))) //Gotten a joystick that's supported?
		{
			lock(LOCK_INPUT);
			switch (joysticktype)
			{
			case 1: //PSP/PS Vita?
				switch (event->jbutton.button) //What button?
				{
				case INPUT_BUTTON_TRIANGLE:
					input.Buttons |= BUTTON_TRIANGLE; //Press!
					break;
				case INPUT_BUTTON_SQUARE:
					input.Buttons |= BUTTON_SQUARE; //Press!
					break;
				case INPUT_BUTTON_CROSS:
					input.Buttons |= BUTTON_CROSS; //Press!
					break;
				case INPUT_BUTTON_CIRCLE:
					input.Buttons |= BUTTON_CIRCLE; //Press!
					break;
				case INPUT_BUTTON_LTRIGGER:
					input.Buttons |= BUTTON_LTRIGGER; //Press!
					break;
				case INPUT_BUTTON_RTRIGGER:
					input.Buttons |= BUTTON_RTRIGGER; //Press!
					break;
				case INPUT_BUTTON_SELECT:
					input.Buttons |= BUTTON_SELECT; //Press!
					break;
				case INPUT_BUTTON_START:
					input.Buttons |= BUTTON_START; //Press!
					break;
				#if !defined(IS_VITA) && !defined(IS_SWITCH)
				case INPUT_BUTTON_HOME:
					input.Buttons |= BUTTON_HOME; //Press!
					break;
				case INPUT_BUTTON_HOLD:
					input.Buttons |= BUTTON_HOLD; //Press!
					break;
				#endif
				case INPUT_BUTTON_UP:
					input.Buttons |= BUTTON_UP; //Press!
					break;
				case INPUT_BUTTON_DOWN:
					input.Buttons |= BUTTON_DOWN; //Press!
					break;
				case INPUT_BUTTON_LEFT:
					input.Buttons |= BUTTON_LEFT; //Press!
					break;
				case INPUT_BUTTON_RIGHT:
					input.Buttons |= BUTTON_RIGHT; //Press!
					break;
				default: //Unknown button?
					break;
				}
				break;
			case 2: //XBox 360?
				switch (event->jbutton.button) //What button?
				{
				case 0: //A
					input.Buttons |= BUTTON_CROSS; //Press!
					break;
				case 1: //B
					input.Buttons |= BUTTON_CIRCLE; //Press!
					break;
				case 2: //X
					input.Buttons |= BUTTON_SQUARE; //Press!
					break;
				case 3: //Y
					input.Buttons |= BUTTON_TRIANGLE; //Press!
					break;
				case 4: //LB
					input.Buttons |= BUTTON_LTRIGGER; //Press!
					break;
				case 5: //RB
					input.Buttons |= BUTTON_RTRIGGER; //Press!
					break;
				case 6: //BACK
					input.Buttons |= BUTTON_SELECT; //Press!
					break;
				case 7: //START
					input.Buttons |= BUTTON_START; //Press!
					break;
				case 8: //LEFT ANALOG
					input.Buttons |= BUTTON_HOME; //Press!
					break;
				case 9: //RIGHT ANALOG
					input.Buttons |= BUTTON_HOLD; //Press!
					break;
				default: //Unknown button?
					break;
				}
				break;
			default:
				break;
			}
			unlock(LOCK_INPUT);
		}
		break;
	case SDL_JOYBALLMOTION:
#ifdef PELYAS_SDL
		if (event->jball.which == 0)
		{
			if (event->jball.ball < 0x10) //Valid button protection?
			{
				float ballx, bally;
				ballx = getxres() ? ((float)event->jball.xrel / (float)getxres()) : 0.0f; //X coordinate, normalized!
				bally = getyres() ? ((float)event->jball.yrel / (float)getyres()) : 0.0f; //Y coordinate, normalized!
				touchscreencoordinates_x[event->jball.ball] = ballx; //Set screen x coordinate normalized!
				touchscreencoordinates_y[event->jball.ball] = bally; //Set screen y coordinate normalized!
				if (touchstatus[event->jball.ball]) //Already touched?
				{
					if (touchstatus[event->jball.ball] == 2) //We're pending to be pressed?
					{
						touch_fingerDown(touchscreencoordinates_x[event->jbutton.button], touchscreencoordinates_y[event->jbutton.button], (SDL_FingerID)event->jbutton.button); //Press the finger now!
						touchstatus[event->jball.ball] = 1; //We're pressed now!
					}
					else //We've moved!
					{
						touch_fingerMotion(touchscreencoordinates_x[event->jball.ball], touchscreencoordinates_y[event->jball.ball], (SDL_FingerID)event->jball.ball); //Motion of the pressed finger now!
					}
				}
			}
		}
		break; //Disable normal joystick handling: this is disabled!
#endif
//We're not handling it normally!
		break;
	case SDL_JOYBUTTONUP:  /* Handle Joystick Button Releases */
#ifdef PELYAS_SDL
		if (event->jbutton.which == 0)
		{
			if (event->jbutton.button < 0x10) //Valid button protection?
			{
				if (touchstatus[event->jbutton.button]) //Are we touched yet?
				{
					touchstatus[event->jbutton.button] = 0; //We're not touched anymore!
					touch_fingerUp(touchscreencoordinates_x[event->jbutton.button], touchscreencoordinates_y[event->jbutton.button], (SDL_FingerID)event->jbutton.button); //Press the finger now!
				}
			}
		}
		break; //Disable normal joystick handling: this is disabled!
#endif
		joysticktype = getjoystick(joystick, event->jbutton.which); //What joystick are we, if plugged in?
		if (joysticktype && (hasinputfocus || (alwaysdetectjoysticks == 0))) //Gotten a joystick that's supported?
		{
			lock(LOCK_INPUT);
			switch (joysticktype)
			{
			case 1: //PSP?
				switch (event->jbutton.button) //What button?
				{
				case INPUT_BUTTON_TRIANGLE:
					input.Buttons &= ~BUTTON_TRIANGLE; //Release!
					break;
				case INPUT_BUTTON_SQUARE:
					input.Buttons &= ~BUTTON_SQUARE; //Release!
					break;
				case INPUT_BUTTON_CROSS:
					input.Buttons &= ~BUTTON_CROSS; //Release!
					break;
				case INPUT_BUTTON_CIRCLE:
					input.Buttons &= ~BUTTON_CIRCLE; //Release!
					break;
				case INPUT_BUTTON_LTRIGGER:
					input.Buttons &= ~BUTTON_LTRIGGER; //Release!
					break;
				case INPUT_BUTTON_RTRIGGER:
					input.Buttons &= ~BUTTON_RTRIGGER; //Release!
					break;
				case INPUT_BUTTON_SELECT:
					input.Buttons &= ~BUTTON_SELECT; //Release!
					break;
				case INPUT_BUTTON_START:
					input.Buttons &= ~BUTTON_START; //Release!
					break;
				#if !defined(IS_VITA) && !defined(IS_SWITCH)
				case INPUT_BUTTON_HOME:
					input.Buttons &= ~BUTTON_HOME; //Release!
					break;
				case INPUT_BUTTON_HOLD:
					input.Buttons &= ~BUTTON_HOLD; //Release!
					break;
				#endif
				case INPUT_BUTTON_UP:
					input.Buttons &= ~BUTTON_UP; //Release!
					break;
				case INPUT_BUTTON_DOWN:
					input.Buttons &= ~BUTTON_DOWN; //Release!
					break;
				case INPUT_BUTTON_LEFT:
					input.Buttons &= ~BUTTON_LEFT; //Release!
					break;
				case INPUT_BUTTON_RIGHT:
					input.Buttons &= ~BUTTON_RIGHT; //Release!
					break;
				default: //Unknown button?
					break;
				}
				break;
			case 2: //XBox 360?
				switch (event->jbutton.button) //What button?
				{
				case 0: //A
					input.Buttons &= ~BUTTON_CROSS; //Release!
					break;
				case 1: //B
					input.Buttons &= ~BUTTON_CIRCLE; //Release!
					break;
				case 2: //X
					input.Buttons &= ~BUTTON_SQUARE; //Release!
					break;
				case 3: //Y
					input.Buttons &= ~BUTTON_TRIANGLE; //Release!
					break;
				case 4: //LB
					input.Buttons &= ~BUTTON_LTRIGGER; //Release!
					break;
				case 5: //RB
					input.Buttons &= ~BUTTON_RTRIGGER; //Release!
					break;
				case 6: //BACK
					input.Buttons &= ~BUTTON_SELECT; //Release!
					break;
				case 7: //START
					input.Buttons &= ~BUTTON_START; //Release!
					break;
				case 8: //LEFT ANALOG
					input.Buttons &= ~BUTTON_HOME; //Release!
					break;
				case 9: //RIGHT ANALOG
					input.Buttons &= ~BUTTON_HOLD; //Release!
					break;
				default: //Unknown button?
					break;
				}
				break;
			default:
				break;
			}
			unlock(LOCK_INPUT);
		}
		break;

	//Mouse events
	case SDL_MOUSEBUTTONDOWN: //Button pressed?
		if (hasmousefocus) //Do we have mouse focus?
		{
			#ifdef SDL_TOUCH_MOUSEID
			if (event->button.which == SDL_TOUCH_MOUSEID) return; //Ignore touch actions, as they're already handled!
			#endif
			lock(LOCK_INPUT);
			switch (event->button.button) //What button?
			{
			case SDL_BUTTON_LEFT:
				mousebuttons |= 1; //Left pressed!
				if (Direct_Input) //Direct input enabled?
				{
					Mouse_buttons |= 1; //Left mouse button pressed!
				}
				else //Not executing direct input?
				{
					GPU_mousebuttondown(mouse_x, mouse_y, 0xFE); //We're pressed at these coordinates!
					updateFingerOSK();
				}
				break;
			case SDL_BUTTON_RIGHT:
				mousebuttons |= 2; //Right pressed!
				if (Direct_Input) //Direct input enabled?
				{
					Mouse_buttons |= 2; //Right mouse button pressed!
				}
				else //Not executing direct input?
				{
					GPU_mousebuttondown(mouse_x, mouse_y, 0xFF); //We're pressed at these coordinates!
					updateFingerOSK();
				}
				break;
			default:
				break;
			}
			unlock(LOCK_INPUT);
		}
		break;
	case SDL_MOUSEBUTTONUP: //Special mouse button action?
		if (hasmousefocus)
		{
			#ifdef SDL_TOUCH_MOUSEID
			if (event->button.which == SDL_TOUCH_MOUSEID) return; //Ignore touch actions, as they're already handled!
			#endif
			lock(LOCK_INPUT);
			switch (event->button.button) //What button?
			{
			case SDL_BUTTON_MIDDLE: //Middle released!
				toggleDirectInput(1); //Toggle direct input by middle button!
				break;
			case SDL_BUTTON_LEFT:
				if (Direct_Input)
				{
					if (input_buffer_enabled) //Buffering?
					{
						if ((input_buffer == -1) && (!input_buffer_shift) && (!input_buffer_mouse)) //Nothing pressed yet?
						{
							input_buffer = last_input_key; //The last key pressed is used!
							input_buffer_shift = currentshiftstatus_inputbuffer; //The last shift is used, if any!
							input_buffer_mouse = mousebuttons; //Left/Right button is pressed!
						}
					}
					Mouse_buttons &= ~1; //button released!
				}
				mousebuttons &= ~1; //Left released!
				break;
			case SDL_BUTTON_RIGHT:
				if ((mousebuttons == 3) && (!DirectInput_Middle)) //Were we both pressed? Special action when not enabled by middle mouse button!
				{
					toggleDirectInput(0); //Toggle direct input by both buttons!
				}
				if (Direct_Input)
				{
					if (input_buffer_enabled) //Buffering?
					{
						if ((input_buffer == -1) && (!input_buffer_shift) && (!input_buffer_mouse)) //Nothing pressed yet?
						{
							input_buffer = last_input_key; //The last key pressed is used!
							input_buffer_shift = currentshiftstatus_inputbuffer; //The last shift is used, if any!
							input_buffer_mouse = mousebuttons; //Left/Right button is pressed!
						}
					}
					Mouse_buttons &= ~2; //button released!
				}
				mousebuttons &= ~2; //Right released!
				break;
			default:
				break;
			}
			unlock(LOCK_INPUT);
		}

		if (event->button.button == SDL_BUTTON_LEFT) //Release left button inside or outside our window?
		{
			lock(LOCK_INPUT);
			GPU_mousebuttonup(mouse_x, mouse_y, 0xFE); //We're released at the current coordinates!
			updateFingerOSK();
			unlock(LOCK_INPUT);
		}
		else if (event->button.button == SDL_BUTTON_RIGHT) //Release right button inside or outside our window?
		{
			lock(LOCK_INPUT);
			GPU_mousebuttonup(mouse_x, mouse_y, 0xFF); //We're released at the current coordinates!
			updateFingerOSK();
			unlock(LOCK_INPUT);
		}
		break;
	case SDL_MOUSEMOTION: //Mouse moved?
		if (hasmousefocus) //Do we have mouse focus?
		{
			#ifdef SDL_TOUCH_MOUSEID
			if (event->motion.which == SDL_TOUCH_MOUSEID) return; //Ignore touch actions, as they're already handled!
			#endif
			lock(LOCK_INPUT);

			if (RDPDelta & 2) //Delta toggled?
			{
				RDPDelta &= ~2; //Clear/acnowledge delta!
				GPU_updateDPI(); //Update our deltas!
			}

			if (Direct_Input) //Direct input? Move the mouse in the emulator itself!
			{
#ifdef IS_WINDOWS
#ifdef SDL2//#ifndef SDL2
	//SDL 1.*: Always patch RDP!
#define PATCHRDP 1
#else
	//When before SDL2 commit 13038, patch RDP input!
#ifdef SDL_REVISION_NUMBER
	//Check for reliable revision number. 0 means unavailable!
#if (SDL_REVISION_NUMBER!=0)
#define REVNR_RELIABLE
#endif
#endif
#ifdef REVNR_RELIABLE
	//Revision number is reliable?
#define PATCHRDP (SDL_GetRevisionNumber()<13038)
#else
#if SDL_VERSION_ATLEAST(2,0,11)
	//2.0.11 has commit 13038, so the patch for RDP isn't needed!
#define PATCHRDP 0
#else
	//The patch for RDP is needed before commit 13038 on SDL2!
#define PATCHRDP 1
#endif
#endif
#endif
#endif
#ifdef PATCHRDP
				if (RDP && PATCHRDP) //Needs adjustment?
				{
					mouse_xmove += floorf((float)event->motion.xrel*(1.0f / 34.0f))*GPU_xDTM; //Move the mouse horizontally!
					mouse_ymove += floorf((float)event->motion.yrel*(1.0f / 60.0f))*GPU_yDTM; //Move the mouse vertically!
					mouse_xmovemickey += (floorf((float)event->motion.xrel*(1.0f / 34.0f))*GPU_xDTmickey); //Move the mouse horizontally!
					mouse_ymovemickey += (floorf((float)event->motion.yrel*(1.0f / 60.0f))*GPU_yDTmickey); //Move the mouse vertically!
				}
				else //No adjustment?
#endif
				{
					mouse_xmove += (float)event->motion.xrel*GPU_xDTM; //Move the mouse horizontally!
					mouse_ymove += (float)event->motion.yrel*GPU_yDTM; //Move the mouse vertically!
					mouse_xmovemickey += ((float)event->motion.xrel*GPU_xDTmickey); //Move the mouse horizontally!
					mouse_ymovemickey += ((float)event->motion.yrel*GPU_yDTmickey); //Move the mouse vertically!
				}
			}
			else //Not direct input?
			{
				GPU_mousemove((word)event->motion.x, (word)event->motion.y, 0xFE); //We're moved to the current coordinates, identify as left mouse button!
			}

			//Always update mouse coordinates for our own GUI handling!
			mouse_x = event->motion.x; //X coordinate on the window!
			mouse_y = event->motion.y; //Y coordinate on the window!
			unlock(LOCK_INPUT);
		}
		break;

#ifndef SDL2//#ifdef SDL2
#if defined(ANDROID) || defined(IS_VITA) || defined(IS_SWITCH)
	case SDL_APP_LOWMEMORY: //Low on memory? Release memory if possible requested by the OS!
		//Not able to handle yet! Ignore it for now!
		break;
#endif
#endif

		//Misc system events
	case SDL_QUIT: //Quit?
#ifndef SDL2//#ifdef SDL2
#if defined(ANDROID) || defined(IS_VITA) || defined(IS_SWITCH)
	case SDL_APP_TERMINATING: //Terminating the application by the OS?

	//case SDL_APP_LOWMEMORY: //Low on memory?
#ifdef NDK_PROFILE
		monpendingcleanup(); //Process any pending cleanup when needed!
#endif
#endif
#endif
	quitting:
		lock(LOCK_INPUT);
		if (joystick) //Gotten a joystick connected?
		{
			SDL_JoystickClose(joystick); //Finish our joystick: we're not using it anymore!
			joystick = NULL; //No joystick connected anymore!
		}
		EMU_Shutdown(1); //Request a shutdown!
		unlock(LOCK_INPUT);
		break;
#ifdef SDL2//#ifndef SDL2
	case SDL_ACTIVEEVENT: //Window event?
		lock(LOCK_INPUT);
		if (event->active.state&SDL_APPMOUSEFOCUS)
		{
			hasmousefocus = event->active.gain; //Do we have mouse focus?
		}
		if (event->active.state&SDL_APPINPUTFOCUS) //Gain/lose keyboard focus?
		{
			hasinputfocus = event->active.gain; //Do we have input focus?
		}
		if (event->active.state&SDL_APPACTIVE) //Iconified/Restored?
		{
			haswindowactive = (haswindowactive&~1) | (event->active.gain ? 1 : 0); //0=Iconified, 1=Restored.
			if (event->active.gain) //Restored?
			{
				unlock(LOCK_INPUT);
				goto didenterforeground;
			}
			unlock(LOCK_INPUT);
			goto didenterbackground;
		}
		unlock(LOCK_INPUT);
		break;
	case SDL_VIDEOEXPOSE: //We're exposed? We need to be redrawn!
		lock(LOCK_GPU);
		request_render = 1; //Requesting for rendering once!
		unlock(LOCK_GPU);
		break;
	#else
	case SDL_WINDOWEVENT: //SDL2 window event!
		switch (event->window.event) //What event?
		{
			case SDL_WINDOWEVENT_MINIMIZED:
				lock(LOCK_INPUT);
				haswindowactive &= ~1; //Iconified!
				unlock(LOCK_INPUT);
				#ifndef ANDROID
				goto didenterbackground;
				#endif
				break;
			case SDL_WINDOWEVENT_RESTORED:
				lock(LOCK_INPUT);
				haswindowactive |= 1; //Restored!
				unlock(LOCK_INPUT);
				#ifndef ANDROID
				goto didenterforeground;
				#endif
				break;
			case SDL_WINDOWEVENT_FOCUS_GAINED:
				lock(LOCK_INPUT);
				hasinputfocus = 1; //Input focus!
				unlock(LOCK_INPUT);
				#if defined(ANDROID) || defined(IS_VITA) || defined(IS_SWITCH)
				goto didenterforeground; //Simulate foreground!
				#endif
				break;
			case SDL_WINDOWEVENT_FOCUS_LOST:
				lock(LOCK_INPUT);
				hasinputfocus = 0; //Lost input focus!
				unlock(LOCK_INPUT);
				#if defined(ANDROID) || defined(IS_VITA) || defined(IS_SWITCH)
				goto didenterbackground; //Simulate background!
				#endif
				break;
			case SDL_WINDOWEVENT_ENTER:
				lock(LOCK_INPUT);
				hasmousefocus = 1; //Mouse focus!
				unlock(LOCK_INPUT);
				break;
			case SDL_WINDOWEVENT_LEAVE:
				lock(LOCK_INPUT);
				hasmousefocus = 0; //Lost mouse focus!
				unlock(LOCK_INPUT);
				break;
			case SDL_WINDOWEVENT_CLOSE:
				goto quitting; //We're redirecting to the SDL_QUIT event!
				break;
			case SDL_WINDOWEVENT_MOVED: //We've been moved?
				lock(LOCK_GPU);
				window_moved = 1; //We've moved!
				window_x = event->window.data1; //X location of the window!
				window_y = event->window.data2; //Y location of the window!
				unlock(LOCK_GPU);
				break;
			case SDL_WINDOWEVENT_EXPOSED: //Need to redraw?
				lock(LOCK_GPU);
				request_render = 1; //Requesting for rendering once!
				unlock(LOCK_GPU);
				break;
			default: //Unknown event?
				break;
		}
		break;
	#ifdef SDL_VERSION_ATLEAST
	#if SDL_VERSION_ATLEAST(2,0,2)
	//Video event
	case SDL_RENDER_TARGETS_RESET: //The render targets have been reset and their contents need to be updated! (SDL 2.0.2+)
		#ifndef IS_WINDOWS
		//Starts spamming on Windows, so don't handle there!
		lock(LOCK_VIDEO);
		needvideoupdate = 1; //We need a video update!
		unlock(LOCK_VIDEO);
		#endif
		break;
	#endif
	#if SDL_VERSION_ATLEAST(2,0,4)
	//Video event
	case SDL_RENDER_DEVICE_RESET: //The device has been reset and needs to be recreated (SDL 2.0.4+)
		lock(LOCK_VIDEO);
		needvideoupdate = 1; //We need a video update!
		unlock(LOCK_VIDEO);
		break;
	//Audio events
	case SDL_AUDIODEVICEADDED: //An audio device has been added
		audiodevice_connected(event->adevice.which, (event->adevice.iscapture != 0)); //Capture device or output device connected?
		break;
	case SDL_AUDIODEVICEREMOVED: //An audio device has been removed
		audiodevice_disconnected(event->adevice.which, (event->adevice.iscapture != 0)); //Capture device or output device connected?
		break;
	#endif
	#endif

	case SDL_APP_DIDENTERBACKGROUND: //Are we pushed to the background?
	case SDL_APP_WILLENTERBACKGROUND: //Are we pushing to the background?
	#endif
		didenterbackground: //For focus gain/lost!
		lock(LOCK_INPUT);
		haswindowactive &= ~6; //We're iconified! This also prevents drawing and audio output! This is critical!
		haswindowactive |= 0x8; //Discard any future time!
		unlock(LOCK_INPUT);
		break;
    #ifndef SDL2//#ifdef SDL2
	case SDL_APP_WILLENTERFOREGROUND: //Are we pushing to the foreground?
		break; //Unhandled!
	case SDL_APP_DIDENTERFOREGROUND: //Are we pushed to the foreground?
	#endif
		didenterforeground: //For focus gain/lost!
		lock(LOCK_INPUT);
		haswindowactive |= 6; //We're not iconified! This also enables drawing and audio output!
		lock(LOCK_GPU);
		request_render = 1; //Requesting for rendering once!
		GPU.forceRedraw = 1; //We're forcing a full redraw next frame to make sure the screen is always updated nicely!
		unlock(LOCK_GPU);
		unlock(LOCK_INPUT);
		#if defined(ANDROID) || defined(IS_VITA) || defined(IS_SWITCH)
		lock(LOCK_VIDEO);
		needvideoupdate = 1; //We need a video update!
		unlock(LOCK_VIDEO);
		#endif
		lock(LOCK_ALLOWINPUT);
		allowInput = 1; //Allow input handling to resume!
		unlock(LOCK_ALLOWINPUT);
		break;
    #ifndef SDL2//#ifdef SDL2
	#if defined(ANDROID) || defined(IS_VITA) || defined(IS_SWITCH)
	case SDL_WINDOWEVENT_SIZE_CHANGED: //Orientation changed?
		lock(LOCK_GPU); //Lock the GPU!
		lock(LOCK_VIDEO); //Lock the video output!
		window_xres = window_yres = 0; //We're autodetecting the new resolution!
		GPU.forceRedraw = 1; //We're forcing a full redraw next frame to make sure the screen is always updated nicely!
		needvideoupdate = 1; //We need a video update!
		unlock(LOCK_VIDEO); //We're done with video!
		unlock(LOCK_GPU); //We're finshed with the GPU!
		break;
	#endif
	case SDL_JOYDEVICEADDED: //Joystick has been connected?
		if (hasinputfocus || alwaysdetectjoysticks) //Only take it if we're focused!
		{
			connectJoystick(event->jdevice.which); //Connect to this joystick, is valid!
		}
		break;
	case SDL_JOYDEVICEREMOVED: //Joystick has been removed?
		disconnectJoystick(event->jdevice.which); //Disconnect our joystick if it's ours!
		break;
	case SDL_FINGERDOWN:
		//Convert the touchId and fingerId to finger! For now, allow only one finger!
		touch_fingerDown(event->tfinger.x,event->tfinger.y, event->tfinger.fingerId);
		break;
	case SDL_FINGERUP:
		//Convert the touchId and fingerId to finger! For now, allow only one finger!
		touch_fingerUp(event->tfinger.x, event->tfinger.y, event->tfinger.fingerId);
		break;
	case SDL_FINGERMOTION:
		//Convert the touchId and fingerId to finger! For now, allow only one finger!
		touch_fingerMotion(event->tfinger.x, event->tfinger.y, event->tfinger.fingerId);
		break;
	#endif
	default: //Unhandled/unknown event?
		break; //Ignore the event!
	}
}

//Check for timer occurrences.
void cleanKeyboard()
{
	//Untick the timer!
}

TicksHolder Keyboardticker; //Actual keyboard timing!

void updateKeyboard(DOUBLE timepassed)
{
	keyboard_type_handler(timepassed); //Tick the timer!
}

DOUBLE mouse_ticktiming=0.0f;

//Check for timer occurrences.
void cleanMouse()
{
	//Discard the amount of time passed!
}

void updateMouse(DOUBLE timepassed)
{
	INLINEREGISTER DOUBLE temp,interval;
	temp = mouse_ticktiming; //Load old timing!
	temp += timepassed; //Get the amount of time passed!
	interval = mouse_interval; //Load the interval!
	if (interval && (temp >= interval)) //Enough time passed?
	{
		for (;temp >= interval;) //All that's left!
		{
			mouse_handler(); //Tick mouse timer!
			temp -= interval; //Decrease timer to get time left!
		}
	}
	mouse_ticktiming = temp; //Save the new timing, if any!
}

#ifndef SDL2//#ifdef SDL2
int SDLCALL myEventFilter(void *userdata, SDL_Event * event)
{
	//Emergency calls! Immediately update!
	switch (event->type) //Emergency event?
	{
		#if defined(ANDROID) || defined(IS_VITA) || defined(IS_SWITCH)
		case SDL_APP_TERMINATING: //Terminating the application by the OS?
		case SDL_APP_LOWMEMORY: //Low on memory?
		case SDL_APP_DIDENTERBACKGROUND: //Are we pushed to the background?
		case SDL_APP_WILLENTERBACKGROUND: //Are we pushing to the background?
		case SDL_APP_WILLENTERFOREGROUND: //Are we pushing to the foreground?
		case SDL_APP_DIDENTERFOREGROUND: //Are we pushed to the foreground?
			updateInput(event); //Handle this immediately!
			return 0; //Drop the event, as this is handled already!
		#endif
		default:
			lock(LOCK_ALLOWINPUT);
			if (unlikely(allowInput == 0)) //Keep running?
			{
				unlock(LOCK_ALLOWINPUT);
				updateInput(event); //Handle this immediately!
				return 0; //Drop the event, as this is handled already!
			}
			unlock(LOCK_ALLOWINPUT);
			break; //Handle normally!
	}
	// etc
	return 1; //Handle normally, as a normal event!
}
#endif

byte input_notready = 1; //To init?

void psp_input_init()
{
	if (input_notready)
	{
		memset(&input,0,sizeof(input)); //Init
		input_notready = 0; //Ready!
	}
    #ifdef SDL2//#ifndef SDL2
	uint_32 i;
	#endif
	#ifdef SDL_SYS_JoystickInit
		//Gotten initialiser for joystick?
		if (SDL_SYS_JoystickInit()==-1) quitemu(0); //No joystick present!
	#endif
	initTicksHolder(&Keyboardticker); //Initialise our timing!
	SDL_JoystickEventState(SDL_ENABLE);
	if (alwaysdetectjoysticks) //Always detect joysticks?
	{
		joystick = SDL_JoystickOpen(0); //Open our first joystick by default!
	}
    #ifdef SDL2//#ifndef SDL2
	//SDL2 can't use the reverse table anymore! It uses a simple function for lookups instead!
	for (i = 0;i < NUMITEMS(emu_keys_sdl_rev);i++) //Initialise all keys!
	{
		emu_keys_sdl_rev[i] = -1; //Default to unused!
		++i; //Next!
	}
	for (i = 0;i < NUMITEMS(emu_keys_SDL);) //Process all keys!
	{
		emu_keys_sdl_rev[signed2unsigned16(emu_keys_SDL[i])] = i; //Reverse lookup of the table!
		++i; //Next!
	}
	#endif
    
    #ifdef SDL2//#ifndef SDL2
	//SDL2 doesn't support key repeating!
	#ifdef __DEBUG_INPUT
	SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY,SDL_DEFAULT_REPEAT_INTERVAL); //Repeat pressed keys for logging!
	#else
	SDL_EnableKeyRepeat(0,0); //Don't repeat pressed keys: this isn't required while using normal input without logging!
	#endif
	#endif
	keyboard_mousetiming = mouse_ticktiming = 0.0f; //Initialise mouse timing!
    #ifndef SDL2//#ifdef SDL2
	SDL_AddEventWatch(myEventFilter, NULL); //For applying critical updates!
	#ifdef SDL_HINT_WINDOWS_NO_CLOSE_ON_ALT_F4
		SDL_SetHintWithPriority(SDL_HINT_WINDOWS_NO_CLOSE_ON_ALT_F4,"1",SDL_HINT_OVERRIDE); //We're forcing the window not to quit on ALT-F4!
	#endif
	#ifdef SDL_HINT_ANDROID_SEPARATE_MOUSE_AND_TOUCH
		SDL_SetHintWithPriority(SDL_HINT_ANDROID_SEPARATE_MOUSE_AND_TOUCH,"1",SDL_HINT_OVERRIDE); //We're forcing us to use seperate mouse and touch events!
	#endif
	#ifdef SDL_HINT_MOUSE_TOUCH_EVENTS
		SDL_SetHintWithPriority(SDL_HINT_MOUSE_TOUCH_EVENTS, "0", SDL_HINT_OVERRIDE); //We're forcing us to use seperate mouse and touch events!
	#endif
	#ifdef SDL_HINT_TOUCH_MOUSE_EVENTS
		SDL_SetHintWithPriority(SDL_HINT_TOUCH_MOUSE_EVENTS, "0", SDL_HINT_OVERRIDE); //We're forcing us to use seperate mouse and touch events!
	#endif
#endif
	//Apply joystick defaults!
#ifdef UNIPCEMU
	enableJoystick(0,0); //Disable joystick 1!
	enableJoystick(0,0); //Disable joystick 2!
#endif

	//Initialize our timing! 30 intervals per second!
	#ifdef IS_LONGDOUBLE
	keyboard_mouseinterval = (DOUBLE)(1000000000.0L / 30.0L);
	#else
	keyboard_mouseinterval = (DOUBLE)(1000000000.0 / 30.0);
	#endif

	//The movement is 1 inch/second, so an interval of 1 inch = 25.4mm/second. The value is in mm/interval!
	keyboard_mousemovementspeed = ((25.4f * 2.0f) / 30.0f); //Move 25.4mm in the 30 intervals(see keyboard_mouseinterval for the amount of intervals per second, which is stored in a ns timing for each interval)!

	memset(&lastxy,0,sizeof(lastxy)); //Initialize our last xy coordinates!
}

void psp_input_done()
{
	SDL_JoystickEventState(SDL_DISABLE);
	if (joystick)
	{
		SDL_JoystickClose(joystick); //Close our joystick!
		joystick = NULL; //No joystick anymore!
	}
	//Do nothing for now!
    #ifndef SDL2//#ifdef SDL2
	SDL_DelEventWatch(myEventFilter, NULL); //For applying critical updates!
	#endif
}
