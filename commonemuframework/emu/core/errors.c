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

#include "headers/types.h" //Basic typedefs!
#include "headers/emu/gpu/gpu_text.h" //Text editing!
#include "headers/emu/gpu/gpu_renderer.h" //Rendering support (for rendering the surface)!
#include "headers/emu/gpu/gpu_framerate.h" //Framerate surface rendering only!
#include "headers/support/log.h" //Logging!
#include "headers/emu/threads.h" //Thread support!
#include "headers/emu/sound.h" //For stopping sound on errors!

/*

Our error handler!

raiseError: Raises an error!
parameters:
	source: The origin of the error (debugging purposes)
	text: see (s)printf parameters.

*/

byte ERROR_RAISED = 0; //No error raised by default!
byte nonfatalmessageboxraised = 0;

extern GPU_TEXTSURFACE *frameratesurface; //The framerate!

void raiseError(char *source, const char *text, ...)
{
	char msg[256];
	char result[256]; //Result!
	cleardata(&msg[0],sizeof(msg)); //Init!
	cleardata(&result[0],sizeof(result)); //Init!

	va_list args; //Going to contain the list!
	va_start (args, text); //Start list!
	vsnprintf (msg,sizeof(msg), text, args); //Compile list!
	va_end (args); //Destroy list!

	snprintf(result,sizeof(result),"Error at %s: %s",source,msg); //Generate full message!

	GPU_messagebox(NULL, MESSAGEBOX_FATAL, result); //Give the message as a message box, if supported!

	//Log the error!
	dolog("error","A fatal error occurred. %s",result); //Show the error in the log!

	dolog("error","Terminating threads...");
	termThreads(); //Stop all running threads!
	dolog("error","Stopping audio processing...");
	doneAudio(); //Stop audio processing!
	dolog("error","Displaying message...");

	GPU_text_locksurface(frameratesurface);
	GPU_textgotoxy(frameratesurface,0,0); //Goto 0,0!
	GPU_textprintf(frameratesurface,RGB(0xFF,0xFF,0xFF),RGB(0x22,0x22,0x22),result); //Show the message on our debug screen!
	GPU_text_releasesurface(frameratesurface);
	GPU_textrenderer(frameratesurface); //Render the framerate surface on top of all else!
	renderFramerateOnly(); //Render the framerate only!

	dolog("error","Waiting 5 seconds before quitting...");
	ERROR_RAISED = 1; //We've raised an error!
	delay(5000000); //Wait 5 seconds...
	//When we're exiting this thread, the main thread will become active, terminating the software!
	quitemu(0); //Just in case!
}

void raiseNonFatalError(char* source, const char* text, ...)
{
	if (nonfatalmessageboxraised)
	{
		return; //Abort: only once!
	}
	nonfatalmessageboxraised = 1; //Raise an error once only!
	char msg[256];
	char result[256]; //Result!
	cleardata(&msg[0], sizeof(msg)); //Init!
	cleardata(&result[0], sizeof(result)); //Init!

	va_list args; //Going to contain the list!
	va_start(args, text); //Start list!
	vsnprintf(msg, sizeof(msg), text, args); //Compile list!
	va_end(args); //Destroy list!

#ifdef SDL2
	GPU_messagebox(source, MESSAGEBOX_ERROR, msg); //Show the message!
#endif
}