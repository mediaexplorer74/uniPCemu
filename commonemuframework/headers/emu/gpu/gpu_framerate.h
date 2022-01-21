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

#ifndef GPU_FRAMERATE_H
#define GPU_FRAMERATE_H

void updateFramerateBar(); //Update/show the framerate bar if enabled!
void GPU_Framerate_Thread(); //One second has passed thread (called every second!)?
void finish_screen(); //Extra stuff after rendering!
void updateFramerateBar_Thread();
void GPU_FrameRendered(); //A frame has been rendered?
void initFramerate(); //Initialise framerate support!
void doneFramerate(); //Remove the framerate support!
void renderFramerate(); //Render the framerate on the surface of the current rendering!

void renderFramerateOnly(); //Render the framerate only, black background!
void logVGASpeed(); //Log VGA at max speed!

/*

Frameskip support!

*/

void setGPUFrameskip(byte Frameskip);
void setGPUFramerate(byte Show); 

#endif