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

#ifndef GPU_RENDERER_H
#define GPU_RENDERER_H

void renderScreenFrame(); //Render the screen frame!
int GPU_directRenderer(); //Plot directly 1:1 on-screen!
int GPU_fullRenderer();

/*

THE RENDERER!

*/

void renderHWFrame(); //Render a hardware frame!

uint_32 *get_rowempty(); //Gives an empty row (and clears it)!
void done_GPURenderer(); //Cleanup only!

/*

FPS LIMITER!

*/

void refreshscreen(); //Handler for a screen frame (60 fps) MAXIMUM.

#endif