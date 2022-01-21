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

#include "headers/types.h" //Basic stuff!
#include "headers/emu/gpu/gpu.h" //GPU typedefs etc.
#include "headers/support/highrestimer.h" //High resolution timer!
#include "headers/emu/gpu/gpu_sdl.h" //SDL support!
#include "headers/support/log.h" //Logging support!
#include "headers/emu/gpu/gpu_framerate.h" //Framerate support!
#include "headers/support/bmp.h" //Bitmap support!
#include "headers/support/zalloc.h" //Zalloc support!
#include "headers/emu/gpu/gpu_text.h" //Text rendering support!
#include "headers/support/locks.h" //Locking support!

//Are we disabled?
#define __HW_DISABLED 0

//Allow HW rendering? (VGA or other hardware)
#define ALLOW_HWRENDERING 1

extern BIOS_Settings_TYPE BIOS_Settings; //The BIOS Settings!

byte SCREEN_CAPTURE = 0; //To capture a screen? Set to 1 to make a capture next frame!

extern GPU_type GPU; //GPU!

extern GPU_SDL_Surface *rendersurface; //The PSP's surface to use when flipping!
extern uint_32 frames; //Frames processed!

uint_32 frames_rendered = 0;

extern byte haswindowactive; //Are we displayed on-screen?

char capturepath[256] = "captures"; //Capture path!

void renderScreenFrame() //Render the screen frame!
{
	if (__HW_DISABLED) return; //Abort?
	if (SDL_WasInit(SDL_INIT_VIDEO) && rendersurface) //Rendering using SDL?
	{
		++frames_rendered; //Increase ammount of frames rendered!
		if ((haswindowactive&3)==3) //Are we even visible and allowed to update?
		{
			safeFlip(rendersurface); //Set the new resized screen to use, if possible!
		}
		return; //Done!
	}
	//Already on-screen rendered: We're using direct mode!
}

char filename[256];
OPTINLINE char *get_screencapture_filename() //Filename for a screen capture!
{
	domkdir(capturepath); //Captures directory!
	uint_32 i=0; //For the number!
	char filename2[256];
	memset(&filename2,0,sizeof(filename2)); //Init filename!
	do
	{
		snprintf(filename2,sizeof(filename),"%s/%" SPRINTF_u_UINT32 ".bmp",capturepath,++i); //Next bitmap file!
	} while (file_exists(filename2)); //Still exists?
	snprintf(filename,sizeof(filename),"%s/%" SPRINTF_u_UINT32,capturepath,i); //The capture filename!
	return &filename[0]; //Give the filename for quick reference!
}

uint_32 *row_empty = NULL; //A full row, non-initialised!
uint_32 row_empty_size = 0; //No size!
GPU_SDL_Surface *resized = NULL; //Standard resized data, keep between unchanged screens!

OPTINLINE void init_rowempty()
{
	if (__HW_DISABLED) return; //Abort?
	if (!row_empty) //Not allocated yet?
	{
		row_empty_size = EMU_MAX_X*sizeof(uint_32); //Load the size of an empty row for deallocation purposes!
		row_empty = (uint_32 *)zalloc(row_empty_size,"Empty row",NULL); //Initialise empty row!
	}
}

OPTINLINE void GPU_finishRenderer() //Finish the rendered surface!
{
	if (__HW_DISABLED) return; //Abort?
	if (resized) //Resized still buffered?
	{
		resized = freeSurface(resized); //Try and free the surface!
	}
}

void done_GPURenderer() //Cleanup only!
{
	if (__HW_DISABLED) return; //Abort?
	if (row_empty) //Allocated?
	{
		freez((void **)&row_empty,row_empty_size,"GPURenderer_EmptyRow"); //Clean up!
	}
	GPU_finishRenderer(); //Finish the renderer!
}

uint_32 *get_rowempty()
{
	if (__HW_DISABLED) return NULL; //Abort?
	init_rowempty(); //Init empty row!
	return row_empty; //Give the empty row!
}

word renderarea_x_start=0, renderarea_y_start=0; //X and Y start of the rendering area of the real rendered active display!

OPTINLINE void render_EMU_direct() //Plot directly 1:1 on-screen!
{
#ifdef IS_PSP
	int pspy = 0;
	int pspx = 0;
#endif
	if (__HW_DISABLED) return; //Abort?
	if (SDL_WasInit(SDL_INIT_VIDEO) && rendersurface) //Rendering using SDL?
	{
		word widthclear, width; //Width and height to process!
		if (!rendersurface->sdllayer) return; //Abort with invalid rendering surface!
		uint_32 virtualrow = 0; //Virtual row to use! (From the source)
		uint_32 start = 0; //Start row of the drawn part!
		word y = 0; //Init Y to the beginning!
		widthclear = MIN(rendersurface->sdllayer->w,EMU_MAX_X);
		if (GPU.aspectratio) //Using letterbox for aspect ratio?
		{
			if (!check_surface(rendersurface)) goto abortrendering; //Error occurred?
			#if !defined(STATICSCREEN)
				if (VIDEO_DFORCED) //Forced video?
				{
					goto drawpixels; //No letterbox top!
				}
			#endif
			if (!rendersurface->sdllayer) goto abortrendering; //Error occurred?
			if (resized) //Valid?
			{
				if (resized->sdllayer)
				{
					if (resized->sdllayer->h) //Gotten height?
					{
						if (check_surface(resized)) //Valid to render from?
						{
							start = (rendersurface->sdllayer->h / 2) - (resized->sdllayer->h / 2); //Calculate start row of contents!
							for (; y < start;) //Process top!
							{
								put_pixel_row(rendersurface, y++, widthclear, get_rowempty(), 0, 0, NULL); //Plot empty row, don't care about more black!
								if (!rendersurface) goto abortrendering; //Error occurred?
								if (!rendersurface->sdllayer) goto abortrendering; //Error occurred?
							}
						}
					}
				}
			}
		}

#if !defined(STATICSCREEN)
		drawpixels:
#endif
		renderarea_y_start = y; //Start of the render area on real display!
		if (check_surface(rendersurface)) //Valid surface to render?
		{
			if (resized) //Valid surface to render?
			{
				if (resized->sdllayer) //Valid layer?
				{
					if (check_surface(resized)) //Valid surface to render from?
					{
						if (resized->sdllayer->h && resized->sdllayer->w) //Gotten height and width?
						{
							width = MIN(resized->sdllayer->w, rendersurface->sdllayer->w);
							for (;
								((int_32)virtualrow < (int_32)resized->sdllayer->h) //Protect against source overflow!
								&& ((int_32)y < rendersurface->sdllayer->h) //Protect against destination overflow!
								;) //Process row-by-row!
							{
								put_pixel_row(rendersurface, y++, width, get_pixel_row(resized, virtualrow++, 0), 0, 0, &renderarea_x_start); //Copy the row to the screen buffer, centered horizontally if needed, from virtual if needed!
								if (!resized) goto cantrender; //Error occurred?
								if (!resized->sdllayer) goto cantrender; //Error occurred?
							}
						}
					}
				}
			}
		}
		cantrender:

		if (!check_surface(rendersurface)) goto abortrendering; //Error occurred?
		if (!rendersurface->sdllayer) goto abortrendering; //Error occurred?

		//Always clear the bottom: nothing, letterbox and direct plot both have to clear the bottom!
		for (; y<rendersurface->sdllayer->h;) //Process bottom!
		{
			put_pixel_row(rendersurface, y++, widthclear, get_rowempty(), 0, 0,NULL); //Plot empty row for the bottom, don't care about more black!
			if (!rendersurface) goto abortrendering; //Error occurred?
			if (!rendersurface->sdllayer) goto abortrendering; //Error occurred?
		}
		abortrendering:
		if (memprotect(resized, sizeof(*resized), NULL) && resized) //Using resized?
		{
			resized->flags &= ~SDL_FLAG_DIRTY; //Not dirty anymore!
		}
		return; //Don't render anymore!
	}

#ifdef IS_PSP
	//PSP only?
	if (GPU.emu_buffer_dirty) //Dirty?
	{
		//Old method, also fine&reasonably fast!
		for (; pspy<PSP_SCREEN_ROWS;) //Process row!
		{
			pspx = 0; //Init row location!
			for (; pspx<PSP_SCREEN_COLUMNS;) //Process column!
			{
				if ((pspx>=GPU.xres) || (pspy>=GPU.yres)) //Out of range?
				{
					PSP_BUFFER(pspx, pspy) = 0; //Clear color for out of range!
				}
				else //Exists in buffer?
				{
					PSP_BUFFER(pspx, pspy) = GPU_GETPIXEL(pspx, pspy); //Get pixel from buffer!
				}
				++pspx; //Next X!
			}
			++pspy; //Next Y!
		}
		GPU.emu_buffer_dirty = 0; //Not dirty anymore!
	}
#endif

	//We can't use the keyboard with the old renderer, so you just have to do it from the top of your head!
	//OK: rendered to PSP buffer!
	if (memprotect(resized, sizeof(*resized), NULL) && resized) //Using resized?
	{
		resized->flags &= ~SDL_FLAG_DIRTY; //Not dirty anymore!
	}
}

OPTINLINE void render_EMU_fullscreen() //Render the EMU buffer to the screen!
{
	if (!check_surface(rendersurface)) return; //Nothing to render to!
	//Now, render our screen, or clear it!
	word y = 0; //Current row counter!
	word count, clearwidth;
	clearwidth = MIN(rendersurface->sdllayer->w, EMU_MAX_X); //Our width to be able to render empty data!
	if (check_surface(resized)) //Resized anti-invalid protection?
	{
		uint_32 virtualrow = 0; //Virtual row to use! (From the source)
		
		byte letterbox = GPU.aspectratio; //Use letterbox?
		if (letterbox && resized && (rendersurface->sdllayer->h>resized->sdllayer->h)) //Using letterbox for aspect ratio?
		{
			count = ((rendersurface->sdllayer->h/2) - (resized->sdllayer->h/2))-1; //The total ammount to process: up to end+1!
			nextrowtop: //Process top!
			{
				if (!count--) goto startemurendering; //Done?
				put_pixel_row(rendersurface,y++,clearwidth,get_rowempty(),0,0,NULL); //Plot empty row, don't care about more black!
				goto nextrowtop; //Next row!
			}
		}
		
		startemurendering:
		renderarea_y_start = y; //Start of the render area on real display!
		renderarea_x_start = 0; //Default to the start of the row!
		if (check_surface(resized) && resized) //Valid layer?
		{
			if (resized->sdllayer) //Valid layer?
			{
				if (resized->sdllayer->h && resized->sdllayer->w) //Gotten height and width?
				{
					count = resized->sdllayer->h; //How many!
					nextrowemu: //Process row-by-row!
					{
						if (!count--) goto startbottomrendering; //Stop when done!
						if (!resized) goto startbottomrendering; //Skip when no resized anymore!
						put_pixel_row(rendersurface, y++, resized->sdllayer->w, get_pixel_row(resized,virtualrow++,0), letterbox?1:0, 0,&renderarea_x_start); //Copy the row to the screen buffer, centered horizontally if needed, from virtual if needed!
						goto nextrowemu;
					}
				}
			}
		}
		
		//Always clear the bottom: nothing, letterbox and direct plot both have to clear the bottom!
	startbottomrendering:
		if (y>=rendersurface->sdllayer->h) goto finishbottomrendering; //Don't finish what isn't needed!
		count = rendersurface->sdllayer->h-y; //How many left to process!
		nextrowbottom: //Process bottom!
		{
			if (!count--) goto finishbottomrendering; //Stop when done!
			put_pixel_row(rendersurface,y++,clearwidth,get_rowempty(),0,0,NULL); //Plot empty row for the bottom, don't care about more black!
			goto nextrowbottom;
		}

		finishbottomrendering:
		if (memprotect(resized, sizeof(*resized), NULL) && resized) //Using resized?
		{
			resized->flags &= ~SDL_FLAG_DIRTY; //Not dirty anymore!
		}
	}
	else //No resized available? Render black!
	{
		//Always clear the bottom: nothing, letterbox and direct plot both have to clear the bottom!
		for (; y<rendersurface->sdllayer->h;) //Process bottom!
		{
			put_pixel_row(rendersurface, y++, clearwidth, get_rowempty(), 0, 0,NULL); //Plot empty row for the bottom, don't care about more black!
			if (!rendersurface) return; //Error occurred?
			if (!rendersurface->sdllayer) return; //Error occurred?
		}
	}
}

byte hadresized = 0; //Did we have a resized before?
byte request_render = 0; //Requesting for rendering once?
OPTINLINE byte getresizeddirty() //Is the emulated screen dirty?
{
	if (resized) //Able to check?
	{
		hadresized = 1; //We had a resized!
		return ((resized->flags&SDL_FLAG_DIRTY)>0); //Are we dirty?
	}
	else //No resized anymore?
	{
		if (unlikely(hadresized)) //Resized -> no resized?
		{
			hadresized = 0; //No resized anymore!
			return 1; //We need to update: resized has disappeared! Don't update any more after that!
		}
		//We have and had no resized!
	}
	return 0; //Default: no resized and not dirty!
}

OPTINLINE void renderFrames() //Render all frames to the screen!
{
	if (SDL_WasInit(SDL_INIT_VIDEO) && rendersurface) //Rendering using SDL?
	{
		byte dirty;
		dirty = getresizeddirty()|request_render; //Check if resized is dirty!

		int i; //For processing surfaces!
		//Check for dirty text surfaces!
		for (i=0;i<(int)NUMITEMS(GPU.textsurfaces);i++) //Process all text surfaces!
		{
			if (GPU.textsurfaces[i]) //Stop on first surface not specified?
			{
				if (GPU.textrenderers[i]) //Gotten a handler?
				{
					GPU.textrenderers[i](); //Execute the handler for filling the screen!
				}
				GPU_text_locksurface(GPU.textsurfaces[i]); //Lock before checking!
				if (GPU_textdirty(GPU.textsurfaces[i])) //Marked dirty?
				{
					dirty = 1; //We're dirty!
				}
				GPU_text_releasesurface(GPU.textsurfaces[i]); //Unlock now!
			}
		}

		if (dirty) //Any surfaces dirty?
		{
			request_render = 0; //Processing request for rendering!
			if (VIDEO_DIRECT) //Direct mode?
			{
				render_EMU_direct(); //Render directly!
			}
			else
			{
				render_EMU_fullscreen(); //Render the emulator surface to the screen!
			}
			for (i=0;i<(int)NUMITEMS(GPU.textsurfaces);i++) //Render the text surfaces to the screen!
			{
				if (GPU.textsurfaces[i]) //Specified?
				{
					GPU_textrenderer(GPU.textsurfaces[i]); //Render the text layer!
				}
			} //Leave these for now!
			
			//Render the frame!
			renderScreenFrame(); //Render the current frame!
		}
		return; //Normal!
	}

	//Fallback to direct plot!
	render_EMU_direct(); //Fallback!
}

//Rendering functionality!
OPTINLINE void render_EMU_buffer() //Render the EMU to the buffer!
{
	byte isresized; //Are we resized successfully?
	//Next, allocate all buffers!
	//First, check the emulated screen for updates and update it if needed!
	if (rendersurface && ((GPU.xres*GPU.yres)>0)) //Got emu screen to render to the PSP and not testing and dirty?
	{
		//Move entire emulator buffer to the rendering buffer when needed (updated)!
		
		if (GPU.emu_buffer_dirty || GPU.forceRedraw) //Dirty = to render again, if allowed!
		{
			GPU.forceRedraw = 0; //Not needed anymore: we're processing now!
			//First, init&fill emu_screen data!
			word xres, yres;
			xres = GPU.xres; //Load x resolution!
			yres = GPU.yres; //Load y resolution!
			//Limit broken = no display!
			if (!xres)
			{
				return; //Limit to buffer!
			}
			if (!yres)
			{
				return; //Limit to buffer!
			}
			xres = (xres>EMU_MAX_X)?EMU_MAX_X:xres; //Limit to buffer width!
			yres = (yres>EMU_MAX_Y)?EMU_MAX_Y:yres; //Limit to buffer height!

			GPU_SDL_Surface *emu_screen = createSurfaceFromPixels(xres, yres, GPU.emu_screenbuffer, EMU_BUFFERPITCH); //Create container 32BPP pixel mode of the display buffer!
			if (emu_screen) //Createn the screen buffer to render?
			{
				if (!(VIDEO_DIRECT) || GPU.aspectratio) //No direct plot or aspect ratio set?
				{
					//Resize to resized!
					isresized = resizeImage(emu_screen,&resized,rendersurface->sdllayer->w,rendersurface->sdllayer->h,GPU.aspectratio,1); //Render it to the PSP screen, keeping aspect ratio with letterboxing!
					if ((!isresized) || (!memprotect(resized,sizeof(*resized),NULL))) //Error resizing?
					{
						dolog("GPU","Error resizing the EMU screenbuffer to the displayed screen!");
					}
					else if (!memprotect(resized->sdllayer,sizeof(*resized->sdllayer),NULL)) //Invalid layer?
					{
						dolog("GPU","Error resizing the EMU screenbuffer to the displayed screen!");
					}

					//Clean up and reset flags!
					emu_screen = freeSurface(emu_screen); //Done with the emulator screen!
					GPU.emu_buffer_dirty = 0; //Not dirty anymore: we've been updated when possible!
				}
				else //No resizing needed?
				{
					if (resized) //Still allocated?
					{
						GPU_finishRenderer(); //Free the emulated screen if it's still there! Else we keep allocating a surface each time, never releasing it!
					}
					resized = emu_screen; //Use the screen directly!
					GPU.emu_buffer_dirty = 0; //Not dirty anymore: we've been updated when possible!
					emu_screen = NULL; //Not used anymore!
					if (!resized) //invalid?
					{
						dolog("GPU","Error creating the EMU screenbuffer for direct plotting!");
					}
				}
			}
		}
	}
}

/*

THE RENDERER!

*/



byte candraw = 0; //Can we draw (determined by max framerate)?
byte GPU_is_rendering = 0; //We're rendering currently: for preventing multirendering?
extern float curscanlinepercentage; //Current scanline percentage (0.0-1.0)!

#ifdef UNIPCEMU
extern byte VGA_vtotal; //VTotal detection?
#endif

void renderHWFrame() //Render a frame from hardware!
{
#ifdef UNIPCEMU
	VGA_vtotal = 1; //We're detected if detecting!
#endif
	if (__HW_DISABLED) return; //Abort?
	if (!ALLOW_RENDERING) return; //Disable when not allowed to render!

	if (GPU_is_rendering) return; //Don't render multiple frames at the same time!
	GPU_is_rendering = 1; //We're rendering, so block other renderers!
	if (ALLOW_HWRENDERING)
	{
		lockGPU(); //Make sure we're locked!
		updateVideo(); //Update the video resolution if needed!
		//Start the rendering!
		if (SDL_WasInit(SDL_INIT_VIDEO) && rendersurface) //Allowed rendering?
		{
			unlockGPU();
			render_EMU_buffer(); //Render the EMU to the buffer, if updated! This is our main layer!
			lockGPU(); //Lock again!
		}
		if (SCREEN_CAPTURE) //Screen capture?
		{
			if (GPU.xres && GPU.yres) //Anything to dump at all?
			{
				if (!--SCREEN_CAPTURE) //Capture this frame?
				{
					word tempx, tempy;
					tempx = GPU.xres;
					tempy = GPU.yres;
					tempx = (tempx>EMU_MAX_X)?EMU_MAX_X:tempx;
					tempy = (tempy>EMU_MAX_Y)?EMU_MAX_Y:tempy; //Apply limits!
					writeBMP(get_screencapture_filename(),&EMU_BUFFER(0,0),tempx,tempy,0,0,EMU_BUFFERPITCH); //Dump our raw screen!
				}
			}
		}
		GPU_FrameRendered(); //A frame has been rendered, so update our stats!
		unlockGPU();
	}
	GPU_is_rendering = 0; //We're not rendering anymore!
}

/*

FPS LIMITER!

*/

void refreshscreen() //Handler for a screen frame (60 fps) MAXIMUM.
{
	if (__HW_DISABLED) return; //Abort?
	lockGPU();
	int do_render = 1; //Do render?

	if (GPU.frameskip) //Got frameskip?
	{
		do_render = !GPU.framenr; //To render the current frame each <frameskip> frames!
		GPU.framenr = (GPU.framenr+1)%(GPU.frameskip+1); //Next frame!
	}
	if ((do_render||request_render) && GPU.video_on) //Disable when Video is turned off or skipped! Always render a frame when a video rendering is required
	{
		if (!memprotect(rendersurface, sizeof(*rendersurface), NULL)) goto skiprendering; //Abort without surface to render!
		renderFrames(); //Render all frames needed!
	}

	finish_screen(); //Finish stuff on-screen(framerate counting etc.)!	
	
	skiprendering: //Skipping rendering?
	GPU_is_rendering = 0; //We're done rendering!
	unlockGPU(); //Finished with the GPU!
}
