
// This file is part of The Common Emulator Framework.

//We're the GPU!
#define IS_GPU

#include "headers/types.h" //Global stuff!
#include "headers/emu/gpu/gpu.h" //Our stuff!
#include "headers/emu/threads.h" //Thread support!
#include "headers/support/highrestimer.h" //High resolution timer!
#include "headers/support/zalloc.h" //Memory allocation support!
#include "headers/support/log.h" //Log support!
#include "headers/emu/gpu/gpu_framerate.h" //GPU framerate support!"
#include "headers/emu/gpu/gpu_sdl.h" //SDL support!
#include "headers/emu/gpu/gpu_emu.h" //Emulator support (for resetting)!
#include "headers/emu/gpu/gpu_renderer.h" //Renderer support!
#include "headers/emu/gpu/gpu_text.h" //Text delta position support!
#include "headers/support/locks.h" //Lock support!
#ifndef UNIPCEMU//#ifdef UNIPCEMU
#include "headers/bios/biosmenu.h" //Allocating BIOS menu layer support!
#include "headers/emu/emucore.h" //Core support!
#endif

//Are we disabled?
#define __HW_DISABLED 0

//How many frames to render max?
#define GPU_FRAMERATE 60.0f

extern BIOS_Settings_TYPE BIOS_Settings; //Current settings!

GPU_type GPU; //The GPU itself!

GPU_SDL_Surface *rendersurface = NULL; //The PSP's surface to use when flipping!
SDL_Surface *originalrenderer = NULL; //Original renderer from above! We can only be freed using SDL_Quit. Above is just the wrapper!
extern GPU_SDL_Surface *resized; //Standard resized data, keep between unchanged screens!

byte rshift=0, gshift=0, bshift=0, ashift=0; //All shift values!
uint_32 rmask=0, gmask=0, bmask=0, amask=0; //All mask values!

uint_32 transparentpixel = 0xFFFFFFFF; //Transparent pixel!

/*
VIDEO BASICS!
*/

byte firstwindow = 1;
word window_xres = 0;
word window_yres = 0;
uint_32 window_flags = 0; //Current flags for the window!
byte video_aspectratio = 0; //Current aspect ratio!
byte window_moved = 0; //Has this window been moved?
uint_32 window_x=0,window_y=0; //Set location when moved!

TicksHolder renderTiming;
DOUBLE currentRenderTiming = 0.0;
DOUBLE renderTimeout = 0.0; //60Hz refresh!

#ifndef SDL2//#ifdef SDL2
SDL_Window *sdlWindow = NULL;
SDL_Renderer *sdlRenderer = NULL;
SDL_Texture *sdlTexture = NULL;
#endif

byte usefullscreenwindow = 0; //Fullscreen Window?


float screen_xDPI = 96.0f, screen_yDPI = 96.0f; //x and Y DPI for the screen!
float GPU_xDTM = 1.0, GPU_yDTM = 1.0; //INCH*25.4=mm
float GPU_xDTmickey = 200.0f, GPU_yDTmickey = 200.0f; //INCH*200.0=mm
byte textureUpdateRequired = 0; //Texture update required?

void GPU_messagebox(char* title, byte type, char* text, ...)
{
#ifndef SDL2//#ifdef SDL2
	//Supported?
	char msg[256];
	char result[256]; //Result!
	cleardata(&msg[0], sizeof(msg)); //Init!
	cleardata(&result[0], sizeof(result)); //Init!

	va_list args; //Going to contain the list!
	va_start(args, text); //Start list!
	vsnprintf(msg, sizeof(msg), text, args); //Compile list!
	va_end(args); //Destroy list!
	if (title == NULL) //Automatic?
	{
		if (type == MESSAGEBOX_FATAL) //Fatal?
		{
			title = FATAL_WINDOWTITLE; //Fatal window title!
		}
		else
		{
			title = WINDOWTITLE; //Use the default window title!
		}
	}
	switch (type)
	{
	case MESSAGEBOX_ERROR:
	case MESSAGEBOX_FATAL:
		if (SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, msg, NULL)<0) //Show the message box!
		{
			//Error occurred!
			return;
		}
		break;
	case MESSAGEBOX_WARNING:
		if (SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, title, msg, NULL) < 0) //Show the message box!
		{
			//Error occurred!
			return;
		}
		break;
	case MESSAGEBOX_INFORMATION:
	default:
		if (SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title, msg, NULL) < 0) //Show the message box!
		{
			//Error occurred!
			return;
		}
	}
#else
	char msg[256];
	char result[256]; //Result!
	cleardata(&msg[0], sizeof(msg)); //Init!
	cleardata(&result[0], sizeof(result)); //Init!

	va_list args; //Going to contain the list!
	va_start(args, text); //Start list!
	vsnprintf(msg, sizeof(msg), text, args); //Compile list!
	va_end(args); //Destroy list!
	dolog(title, msg); //Try to log it to emu!
#endif
}

void GPU_updateDPI()
{
	float xDPI, yDPI;
#ifndef SDL2//#ifdef SDL2
	if (SDL_GetDisplayDPI(0, NULL, &xDPI, &yDPI) != 0) //DPI not retrieved?
	{
		xDPI = yDPI = 96.0f; //Default value: Assume 96DPI is the default for Windows!
	}
#else
	xDPI = yDPI = 96.0f; //Default value: 96DPI is the default for Windows!
#endif
	screen_xDPI = xDPI; //X DPI!
	screen_yDPI = yDPI; //Y DPI!

	//Now, calculate ther factors to convert x/y to mm and mickey!
	/*
	pixels/inch=DPI
	inch = pixels / DPI
	inch to mm = inch*25.4
	inch to mickeys = inch*200
	*/

	//First, convert the DPI to a factor to convert to inch!
	xDPI = 1.0f / xDPI; //Divide pixels by pixels/inch to get the amount of inch!
	yDPI = 1.0f / yDPI; //Divide pixels by pixels/inch to get the amount of inch!

	//Now, xDPI and yDPI is the factor to convert the pixels to inch!
	GPU_xDTM = (xDPI * 25.4f); //pixels to mm!
	GPU_yDTM = (yDPI * 25.4f); //pixels to mm!
	GPU_xDTmickey = (xDPI * 200.0f); //pixels to mickeys!
	GPU_yDTmickey = (yDPI * 200.0f); //pixels to mickeys!
}

void updateWindow(word xres, word yres, uint_32 flags)
{
	byte useFullscreen; //Are we to use fullscreen?
	if ((xres!=window_xres) || (yres!=window_yres) || (flags!=window_flags) || textureUpdateRequired || !originalrenderer) //Do we need to update the Window?
	{
		textureUpdateRequired = 0; //No update required anymore!

#include "headers/emu/icon.h" //We need our icon!
		
		SDL_Surface *icon = NULL; //Our icon!
		icon = SDL_CreateRGBSurfaceFrom((void *)&icondata,ICON_BMPWIDTH,ICON_BMPHEIGHT,32,ICON_BMPWIDTH<<2, 0x000000FF, 0x0000FF00,0x00FF0000,0); //We have a RGB icon only!
		window_xres = xres;
		window_yres = yres;
		window_flags = flags;
		
#ifdef SDL2//#ifndef SDL2
		useFullscreen = (flags & SDL_FULLSCREEN) ? 1 : 0; //Fullscreen specified?
		char posstr[256];
		memset(&posstr,0,sizeof(posstr)); //Init when needed!
		//SDL1?
		if (icon) //Gotten an icon?
		{
			SDL_WM_SetIcon(icon,NULL); //Set the icon to use!
		}
		#ifndef IS_PSP
		if ((window_moved==0) && (useFullscreen==0)) //Not moved? We're centering the window now!
		{
			SDL_putenv("SDL_VIDEO_WINDOW_POS=center");
		}
		else if (useFullscreen==0) //Not fullscreen at same position?
		{
			snprintf(&posstr[0],sizeof(posstr),"SDL_VIDEO_WINDOW_POS=%u,%u",window_x,window_y); //The position to restore!
			SDL_putenv(&posstr[0]); //Old position maintained, if possible!
		}
		#endif
		originalrenderer = SDL_SetVideoMode(xres, yres, 32, flags); //Start rendered display, 32BPP pixel mode! Don't use double buffering: this changes our address (too slow to use without in hardware surface, so use sw surface)!
#else
		useFullscreen = 0; //Default: not fullscreen!
		if (flags&SDL_WINDOW_FULLSCREEN) //Fullscreen specified?
		{
			flags &= ~SDL_WINDOW_FULLSCREEN; //Don't apply fullscreen this way!
			useFullscreen = 1; //We're using fullscreen!
		}
		if (sdlTexture)
		{
			SDL_DestroyTexture(sdlTexture);
			sdlTexture = NULL; //Nothing!
		}
		if (sdlRenderer)
		{
			SDL_DestroyRenderer(sdlRenderer);
			sdlRenderer = NULL; //Nothing!
		}
		if (rendersurface) //Gotten a surface we're rendering?
		{
			rendersurface = freeSurface(rendersurface); //Release our rendering surface!
		}
		if (!sdlWindow) //We don't have a window&renderer yet?
		{
			#ifndef STATICSCREEN
			if ((!useFullscreen) && (usefullscreenwindow == 0)) //Normal window?
			{
			#endif
				sdlWindow = SDL_CreateWindow(WINDOWTITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, xres, yres, SDL_WINDOW_SHOWN); //Create the window and renderer we use at our resolution!
			#ifndef STATICSCREEN
			}
			else //Automatic scaling?
			{
				sdlWindow = SDL_CreateWindow(WINDOWTITLE, 0, 0, 0, 0, SDL_WINDOW_SHOWN); //Create the window and renderer we use at our resolution!
			}
			#endif
		}
		else
		{
			#ifndef STATICSCREEN
			if ((!useFullscreen) && (usefullscreenwindow == 0)) //Normal window?
			{
			#endif
				SDL_SetWindowSize(sdlWindow, xres, yres); //Set the new window size!
			#ifndef STATICSCREEN
			}
			#endif
		}
		if ((window_moved==0) && (useFullscreen==0)
			#ifndef STATICSCREEN
			&& (usefullscreenwindow==0)
			#endif
			) //Not moved? We're centering the window now!
		{
			SDL_SetWindowPosition(sdlWindow,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED); //Recenter the window!
		}
		SDL_SetWindowFullscreen(sdlWindow,useFullscreen?SDL_WINDOW_FULLSCREEN:0); //Are we to apply fullscreen?
		if (sdlWindow) //Gotten a window?
		{
			#ifndef STATICSCREEN
			if ((!useFullscreen) && (usefullscreenwindow == 0)) //Normal window?
			{
				SDL_SetWindowSize(sdlWindow, xres, yres); //Set the new window size!
			}
			else if ((!useFullscreen) && (usefullscreenwindow)) //Might need clipping?
			{
				int display_index;
				SDL_Rect usable_bounds;
				display_index = SDL_GetWindowDisplayIndex(sdlWindow);
				if (display_index >= 0) //Valid?
				{
					if (SDL_GetDisplayUsableBounds(display_index, &usable_bounds) == 0) //Limits?
					{
						SDL_SetWindowPosition(sdlWindow, usable_bounds.x, usable_bounds.y); //Full desktop the window!
						SDL_SetWindowSize(sdlWindow, usable_bounds.w, usable_bounds.h); //Full desktop the window!
					}
				}
			}
			#endif
			if (icon) //Gotten an icon?
			{
				SDL_SetWindowIcon(sdlWindow,icon); //Set the icon to use!
			}
			if (!sdlRenderer) //No renderer yet?
			{
				#ifdef IS_SWITCH
				sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_SOFTWARE); //Switch can't render hardware-style!
				#else
				sdlRenderer = SDL_CreateRenderer(sdlWindow,-1,0);
				#endif
			}
		}

		if (sdlRenderer) //Gotten a renderer?
		{
			#ifndef STATICSCREEN
			SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear"); //What kind of scaling to use!
			#endif
			SDL_RenderSetLogicalSize(sdlRenderer,window_xres,window_yres); //Set the new resolution!
			sdlTexture = SDL_CreateTexture(sdlRenderer,
				SDL_PIXELFORMAT_ARGB8888,
				SDL_TEXTUREACCESS_STREAMING,
				xres, yres); //The texture we use!
		}

		originalrenderer = SDL_CreateRGBSurface(0, window_xres, window_yres, 32,
			0x00FF0000,
			0x0000FF00,
			0x000000FF,
			0xFF000000); //The SDL Surface we render to!
#endif
		if (icon)
		{
			SDL_FreeSurface(icon); //Free the icon!
			icon = NULL; //No icon anymore!
		}
	}
}

byte GPU_plotsetting = 0;
extern byte request_render; //Requesting for rendering once?

SDL_Surface *getGPUSurface()
{
	uint_32 xres, yres; //Our determinated resolution!
	#ifdef IS_PSP
	//PSP?
	xres = PSP_SCREEN_COLUMNS;
	yres = PSP_SCREEN_ROWS; //Start fullscreen, 32BPP pixel mode! Don't use double buffering: this changes our address (too slow to use without in hardware surface, so use sw surface)!
	GPU.fullscreen = 1; //Forced full screen!
	goto windowready; //Skip other calculations!
	#else
    #ifndef STATICSCREEN//#ifdef STATICSCREEN
    #ifdef SDL2 //#ifndef SDL2
	//SDL Autodetection of fullscreen resolution!
	SDL_Rect **modes;
	if ((!window_xres) || (!window_yres)) //Not initialized yet?
	{
		/* Get available fullscreen/hardware modes */
		modes = SDL_ListModes(NULL, SDL_FULLSCREEN | SDL_HWSURFACE);

		/* Check is there are any modes available */
		if (modes != (SDL_Rect **)0)
		{
			/* Check if our resolution is restricted */
			if (modes != (SDL_Rect **)-1)
			{
				xres = modes[0]->w; //Use first hardware resolution!
				yres = modes[0]->h; //Use first hardware resolution!
				GPU.fullscreen = 1; //Forced full screen!	
				goto windowready; //Skip other calculations!
			}
		}
	}
	#else
	//SDL2 Autodetction of fullscreen resolution!
	//Get device display mode
	SDL_DisplayMode displayMode;
	if (SDL_GetCurrentDisplayMode(0,&displayMode)==0)
	{
		xres = displayMode.w;
		yres = displayMode.h;
		GPU.fullscreen = 1; //Forced full screen!	
		goto windowready; //Skip other calculations!
	}
	#endif
	#endif
	#endif

	//Windows etc?
	//Other architecture?
	word destxres, destyres;
	if (VIDEO_DFORCED) //Forced?
	{
		if (video_aspectratio) //Keep aspect ratio set and gotten something to take information from?
		{
			switch (video_aspectratio) //Forced resolution?
			{
			case 4: //4:3(VGA) medium-res
				destxres = 1024;
				destyres = 768;
				break;
			case 5: //4:3(VGA) high-res(fullHD)
				destxres = 1440; //We're resizing the destination ratio itself instead!
				destyres = 1080; //We're resizing the destination ratio itself instead!
				break;
			case 6: //4K
				destxres = 3840; //We're resizing the destination ratio itself instead!
				destyres = 2160; //We're resizing the destination ratio itself instead!			
				break;
#ifdef GBEMU
			case 7: //Gameboy original
				destxres = 160;
				destyres = 144;
				break;
			case 8: //Gameboy big
				destxres = 533;
				destyres = 479;
				break;
#endif
#ifndef GBEMU
			case 7: //4:3 4K
				destxres = 2880; //We're resizing the destination ratio itself instead!
				destyres = 2160; //We're resizing the destination ratio itself instead!			
				break;
#endif
			default: //Unhandled?
				destxres = 800;
				destyres = 600;
				break;
			}
			calcResize(video_aspectratio,GPU.xres,GPU.yres,destxres,destyres,&xres,&yres,1); //Calculate resize using aspect ratio set for our screen on maximum size(use the smalles window size)!
		}
		else //Default: Take the information from the monitor input resolution!
		{
			xres = GPU.xres; //Literal x resolution!
			yres = GPU.yres; //Literal y resolution!
		}
	}
	else //Normal operations? Use PSP resolution!
	{
		xres = PSP_SCREEN_COLUMNS; //PSP resolution x!
		yres = PSP_SCREEN_ROWS; //PSP resolution y!
	}

	//Apply limits!
	if (xres > EMU_MAX_X) xres = EMU_MAX_X;
	if (yres > EMU_MAX_Y) yres = EMU_MAX_Y;
	
	//Determine minimum by text/screen resolution!
	word minx, miny;
	minx = (GPU_TEXTPIXELSX > PSP_SCREEN_COLUMNS) ? GPU_TEXTPIXELSX : PSP_SCREEN_COLUMNS;
	miny = (GPU_TEXTPIXELSY > PSP_SCREEN_ROWS) ? GPU_TEXTPIXELSY : PSP_SCREEN_ROWS;

	if (xres < minx) xres = minx; //Minimum width!
	if (yres < miny) yres = miny; //Minimum height!

	uint_32 flags;
	//#if defined(STATICSCREEN) || defined(IS_PSP)
	windowready:
	//#endif
	flags = SDL_SWSURFACE; //Default flags!
    #ifdef SDL2//#ifndef SDL2
	if (GPU.fullscreen) flags |= SDL_FULLSCREEN; //Goto fullscreen mode!
	#else
	if (GPU.fullscreen) flags |= SDL_WINDOW_FULLSCREEN; //Goto fullscreen mode!
	#endif

	updateWindow(xres,yres,flags); //Update the window resolution if needed!

	if (firstwindow)
	{
		firstwindow = 0; //Not anymore!
        #ifdef SDL2//#ifndef SDL2
        #ifndef UNIPCEMU//#ifdef UNIPCEMU
		SDL_WM_SetCaption( "UniPCemu", 0 ); //Initialise our window title!
		#else
		SDL_WM_SetCaption( "GBemu", 0 ); //Initialise our window title!
		#endif
		#else
		if (sdlWindow) //Gotten a window?
		{
			#ifdef UNIPCEMU
			SDL_SetWindowTitle(sdlWindow,"UniPCemu"); //Initialise our window title!
			#else
			SDL_SetWindowTitle(sdlWindow,"GBemu"); //Initialise our window title!
			#endif
		}
		#endif
	}

	//Determine the display masks!
	if (originalrenderer) //Valid renderer?
	{
		//Load our detected settings!
		rmask = originalrenderer->format->Rmask;
		rshift = originalrenderer->format->Rshift;
		gmask = originalrenderer->format->Gmask;
		gshift = originalrenderer->format->Gshift;
		bmask = originalrenderer->format->Bmask;
		bshift = originalrenderer->format->Bshift;
		amask = originalrenderer->format->Amask;
		ashift = originalrenderer->format->Ashift;
		if (!amask) //No alpha supported?
		{
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
			amask = 0xFF;
			ashift = 0; //Default position!
#else
			amask = 0xFF000000; //High part!
			ashift = 24; //Shift by 24 bits to get alpha!
#endif
		}
	}
	GPU_updateDPI(); //Update DTM ratio!

	GPU_text_updatedelta(originalrenderer); //Update delta if needed, so the text is at the correct position!
	request_render = 1; //Requesting for rendering once, as we're an empty surface that's createn right now?
	return originalrenderer;
}

//On starting only.
void initVideoLayer() //We're for allocating the main video layer, only deallocated using SDL_Quit (when quitting the application)!
{
	if (SDL_WasInit(SDL_INIT_VIDEO)) //Initialised?
	{
		if (!originalrenderer) //Not allocated yet?
		{
			//PSP has solid resolution!
			getGPUSurface(); //Allocate our display!
			#if defined(STATICSCREEN)
			//We don't want the cursor to show on the PSP!
			SDL_ShowCursor(SDL_DISABLE); //We don't want cursors on empty screens!
			#endif
			if (!originalrenderer) //Failed to allocate?
			{
				raiseError("GPU","Error allocating PSP Main Rendering Surface!");
			}
		}
		if (originalrenderer)
		{
			rendersurface = getSurfaceWrapper(originalrenderer); //Allocate a surface wrapper!
			if (rendersurface) //Allocated?
			{
                
				#ifdef SDL2//#ifndef SDL2
				registerSurface(rendersurface,"SDLRenderSfc",0); //Register, but don't allow release: this is done by SDL_Quit only!
				#else
				registerSurface(rendersurface, "SDLRenderSfc", 1); //Register, allow release: this is allowed in SDL2!
				#endif
				if (memprotect(rendersurface,sizeof(*rendersurface),NULL)) //Valid?
				{
					if (memprotect(rendersurface->sdllayer,sizeof(*rendersurface->sdllayer),NULL)) //Valid?
					{
						if (!memprotect(rendersurface->sdllayer->pixels,sizeof(uint_32)*get_pixelrow_pitch(rendersurface)*rendersurface->sdllayer->h,NULL)) //Valid?
						{
							raiseError("GPU","Rendering surface pixels not registered!");
						}
					}
					else
					{
						raiseError("GPU","Rendering SDL surface not registered!");
					}
				}
				else
				{
					raiseError("GPU","Rendering surface not registered!");
				}
			}
			else
			{
				raiseError("GPU","Error allocating PSP Main Rendering Surface Wrapper");
			}
		}
	}

	//Initialize our timing!
	#ifdef IS_LONGDOUBLE
	renderTimeout = (DOUBLE)(1000000000.0L / GPU_FRAMERATE);
	#else
	renderTimeout = (DOUBLE)(1000000000.0 / GPU_FRAMERATE);
	#endif
}

//Wrapped arround the EMU.
void initVideoMain() //Everything SDL PRE-EMU!
{
	memset(&GPU,0,sizeof(GPU)); //Init all GPU data!
	if (SDL_WasInit(SDL_INIT_VIDEO)) //Initialised?
	{
		//The order we allocate here is from background to foreground!
		initFramerate(); //Start the framerate handler!
		#ifdef UNIPCEMU
		allocBIOSMenu(); //BIOS menu has the highest priority!
		#endif
		initKeyboardOSK(); //Start the OSK handler!
	}
}

void doneVideoMain() //Everything SDL POST-EMU!
{
	doneKeyboardOSK(); //Stop the OSK handler!
	#ifdef UNIPCEMU
	freeBIOSMenu(); //We're done with the BIOS menu!
	#endif
	doneFramerate(); //Finish framerate!
	done_GPURenderer(); //Finish up any rest rendering stuff!
}

//Below is called during emulation itself!

void initGPUdata(); //Initialize all GPU tracking data etc!

void initVideo(int show_framerate) //Initialises the video
{
	if (__HW_DISABLED) return; //Abort!

	debugrow("Video: Initialising screen buffers...");
	
	debugrow("Video: Waiting for access to GPU...");
	lockGPU(); //Wait for access!
	debugrow("Video: Allocating screen buffer...");
	GPU.emu_screenbuffer = (uint_32 *)zalloc(EMU_SCREENBUFFERSIZE * 4, "EMU_ScreenBuffer", NULL); //Emulator screen buffer, 32-bits (x4)!
	if (!GPU.emu_screenbuffer) //Failed to allocate?
	{
		unlockGPU(); //Unlock the GPU for Software access!
		raiseError("GPU InitVideo", "Failed to allocate the emulator screen buffer!");
		return; //Just here to shut Visual C++ code checks up. We cannot be here because the application should have already terminated because of the raiseError call.
	}

	GPU.emu_screenbufferend = &GPU.emu_screenbuffer[EMU_SCREENBUFFERSIZE]; //A quick reference to end of the display buffer!

	debugrow("Video: Setting up misc. settings...");
	GPU.show_framerate = show_framerate; //Show framerate?

//VRAM access enable!
	debugrow("Video: Setting up VRAM Access...");
	GPU.vram = (uint_32 *)VRAM_START; //VRAM access enabled!

	debugrow("Video: Setting up pixel emulation...");
	GPU.showpixels = ALLOW_GPU_GRAPHICS; //Video is turned on!

	debugrow("Video: Setting up video basic...");
	GPU.video_on = 0; //Start video?

	debugrow("Video: Setting up debugger...");
	resetVideo(); //Initialise the video!

	GPU.aspectratio = video_aspectratio = 0; //Default aspect ratio by default!

	transparentpixel = RGBA(SDL_ALPHA_TRANSPARENT, SDL_ALPHA_TRANSPARENT, SDL_ALPHA_TRANSPARENT, SDL_ALPHA_TRANSPARENT); //Set up the transparent pixel!

//We're running with SDL?
	unlockGPU(); //Unlock the GPU for Software access!

	debugrow("Video: Setting up frameskip...");
	setGPUFrameskip(0); //No frameskip, by default!

	debugrow("Video: Device ready.");

	initTicksHolder(&renderTiming);
	currentRenderTiming = 0.0; //Init!

	initGPUdata(); //Initialize all GPU tracking data etc!
}

byte needvideoupdate = 0; //Default: no update needed!

extern byte haswindowactive; //Are we displayed on-screen?

void CPU_updateVideo()
{
	lock(LOCK_VIDEO);
	if (needvideoupdate && ((haswindowactive&3)==3)) //We need to update the screen resolution and we're not hidden (We can't update the Window resolution correctly when we're hidden)?
	{
		unlock(LOCK_VIDEO);
		lockGPU(); //Lock the GPU: we're working on it!
		SDL_Surface *oldwindow; //Old window!
		#ifdef ANDROID
		originalrenderer = (SDL_Surface *)0; //Force refresh of the renderer, since we're invalid now!
		#endif
		oldwindow = originalrenderer; //Old rendering surface!
		if (getGPUSurface()) //Update the current surface if needed!
		{
			if (oldwindow!=originalrenderer) //We're changed?
			{
				freez((void **)&rendersurface, sizeof(*rendersurface), "SDL Main Rendering Surface"); //Release the rendering surface!
			}
			if (!rendersurface) //We don't have a valid rendering surface?
			{
				rendersurface = getSurfaceWrapper(originalrenderer); //New wrapper!
			}
			registerSurface(rendersurface, "SDLRenderSfc", 0); //Register, but don't allow release: this is done by SDL_Quit only!
		}
		needvideoupdate = 0; //Not needed anymore!
		unlockGPU(); //We're finished with the GPU!
	}
	else unlock(LOCK_VIDEO); //We're done with the video!
}

void updateVideo() //Update the screen resolution on change!
{
	//We're disabled with the PSP&Android: it doesn't update resolution!
	#if !defined(STATICSCREEN)
	byte reschange = 0, restype = 0; //Resolution change and type!
	static word xres=0;
	static word yres=0;
	static byte fullscreen = 0; //Are we fullscreen?
	static byte resolutiontype = 0; //Last resolution type!
	static byte plotsetting = 0; //Direct plot setting!
	static byte aspectratio = 0; //Last aspect ratio!
	if ((VIDEO_DIRECT) && (!video_aspectratio)) //Direct aspect ratio?
	{
		lock(LOCK_VIDEO);
		reschange = ((window_xres!=GPU.xres) || (window_yres!=GPU.yres)); //Resolution update based on Window Resolution?
		restype = 0; //Default resolution type!
		unlock(LOCK_VIDEO);
	}
	else if (resized) //Resized available?
	{
		reschange = ((xres!=resized->sdllayer->w) || (yres!=resized->sdllayer->h)); //This is the effective resolution!
		restype = 1; //Resized resolution type!
	}
	else
	{
		reschange = 0; //No resolution change when unknown!
	}
	if (reschange || textureUpdateRequired || (fullscreen!=GPU.fullscreen) || (aspectratio!=video_aspectratio) || (resolutiontype!=restype) || (BIOS_Settings.GPU_AllowDirectPlot!=plotsetting)) //Resolution (type) changed or fullscreen changed or plot setting changed?
	{
		lock(LOCK_VIDEO);
		GPU.forceRedraw = 1; //We're forcing a full redraw next frame to make sure the screen is always updated nicely!
		xres = restype?resized->sdllayer->w:GPU.xres;
		yres = restype?resized->sdllayer->h:GPU.yres;
		plotsetting = GPU_plotsetting = BIOS_Settings.GPU_AllowDirectPlot; //Update the plot setting!
		resolutiontype = restype; //Last resolution type!
		fullscreen = GPU.fullscreen;
		aspectratio = video_aspectratio; //Save the new values for comparing the next time we're changed!
		needvideoupdate = 1; //We need a video update!
		unlock(LOCK_VIDEO); //Finished with the GPU!
	}
	#endif
}

void doneVideo() //We're done with video operations?
{
	int i; //For processing surfaces!
	if (__HW_DISABLED) return; //Abort!
	stopVideo(); //Make sure we've stopped!
	//Nothing to do!
	if (GPU.emu_screenbuffer) //Allocated?
	{
		if (!lockGPU()) return; //Lock ourselves!
		freez((void **)&GPU.emu_screenbuffer, EMU_SCREENBUFFERSIZE * 4, "doneVideo_EMU_ScreenBuffer"); //Free!
		unlockGPU(); //Unlock the GPU for Software access!
	}
	done_GPURenderer(); //Clean up renderer stuff!
	for (i = 0; i < (int)NUMITEMS(GPU.textsurfaces); i++) //Process all text surfaces!
	{
		if (GPU.textsurfaces[i]) //Stop on first surface not specified?
		{
			GPU_text_locksurface(GPU.textsurfaces[i]); //Lock before checking!
			freeTextSurfacePrecalcs(GPU.textsurfaces[i]); //Release the precalcs!
			GPU_text_releasesurface(GPU.textsurfaces[i]); //Unlock now!
		}
	}
}

void startVideo()
{
	if (__HW_DISABLED) return; //Abort!
	lockGPU(); //Lock us!
	GPU.video_on = ALLOW_VIDEO; //Turn video on when allowed!
	unlockGPU(); //Unlock us!
}

void stopVideo()
{
	if (__HW_DISABLED) return; //Abort!
	lockGPU(); //Lock us!
	GPU.video_on = 0; //Turn video off!
	unlockGPU(); //Unlock us!
}

void GPU_AspectRatio(byte aspectratio) //Keep aspect ratio with letterboxing?
{
	if (__HW_DISABLED) return; //Abort!
	lockGPU(); //Lock us!
	#ifdef GBEMU
	GPU.aspectratio = video_aspectratio = (aspectratio < 9) ? aspectratio : 0; //To use aspect ratio?
	#else
	GPU.aspectratio = video_aspectratio = (aspectratio < 8) ? aspectratio : 0; //To use aspect ratio?
	#endif
	GPU.forceRedraw = 1; //We're forcing a redraw of the screen using the new aspect ratio!
	unlockGPU(); //Unlock us!
}

void resetVideo() //Resets the screen (clears)
{
	if (__HW_DISABLED) return; //Abort!
	EMU_textcolor(0xF); //Default color: white on black!
}

void GPU_addTextSurface(void *surface, Handler handler) //Register a text surface for usage with the GPU!
{
	int i=0;
	for (;i<(int)NUMITEMS(GPU.textsurfaces);i++)
	{
		if (GPU.textsurfaces[i]==surface) //Already registered?
		{
			return; //Abort!
		}
	}
	i = 0; //Reset!
	for (;i<(int)NUMITEMS(GPU.textsurfaces);i++) //Process all entries!
	{
		if (!GPU.textsurfaces[i]) //Unused?
		{
			GPU.textrenderers[i] = handler; //Register the handler!
			GPU.textsurfaces[i] = surface; //Register the surface!
			return; //Done!
		}
	}
}

void GPU_removeTextSurface(void *surface)
{
	int i=0;
	for (;i<(int)NUMITEMS(GPU.textsurfaces);i++)
	{
		if (GPU.textsurfaces[i]==surface) //Already registered?
		{
			GPU.textsurfaces[i] = NULL; //Unregister!
			GPU.textrenderers[i] = NULL; //Unregister!
			return; //Done!
		}
	}	
}

extern byte EMU_RUNNING; //Emulator running? 0=Not running, 1=Running, Active CPU, 2=Running, Inactive CPU (BIOS etc.)

int_32 lightpen_x=-1, lightpen_y=-1; //Current lightpen location, if any!
byte lightpen_pressed = 0; //Lightpen pressed?
byte lightpen_status = 0; //Are we capturing lightpen motion and presses?

extern word renderarea_x_start, renderarea_y_start; //X and Y start of the rendering area of the real rendered active display!

//Support for the tracked light pen inputs using touch inputs!
extern byte Mouse_buttons2; //Second mouse button input!
byte lightpensourced_pressed = 0; //What sources are still pressed?
sword trackedlightpenpointerfinger = -1; //Currently tracked pointer finger for tracking !
sword trackedlightpenbuttonfinger = -1; //Currently tracked button finger for tracking !

void updateLightPenLocation(word x, word y)
{
	int_32 realx, realy;
	realx = (int_32)x;
	realy = (int_32)y;
	realx -= (int_32)renderarea_x_start; //Location on the rendering area!
	realy -= (int_32)renderarea_y_start; //Location on the rendering area!
	lightpen_x = -1; //No location by default!
	lightpen_y = -1; //No location by default!
	lockGPU(); //Lock the GPU!
	if (resized) //Valid?
	{
		if (resized->sdllayer)
		{
			if (resized->sdllayer->h && resized->sdllayer->w) //Gotten height and width?
			{
				if ((realx < resized->sdllayer->w) && (realy < resized->sdllayer->h) && (realx>=0) && (realy>=0)) //Valid location on the screen?
				{
					lightpen_x = (int_32)(SAFEDIV((float)(realx), (float)resized->sdllayer->w) * (float)GPU.xres); //Convert the X location to the GPU renderer location!
					lightpen_y = (int_32)(SAFEDIV((float)(realy), (float)resized->sdllayer->h) * (float)GPU.yres); //Convert the X location to the GPU renderer location!
				}
			}
		}
	}
	unlockGPU(); //Unlock the GPU!
}

byte GPU_mousebuttondown(word x, word y, byte finger)
{
	byte GPUtext_priorities[10]; //Priorities for all GPU surfaces!
	int maxSurfaces = MIN(NUMITEMS(GPU.textsurfaces),NUMITEMS(GPUtext_priorities))-1;
	int i; //Start with the last surface! The last registered surface has priority!
	int priority;
	byte maxpriority=0x00; //Max priority reversed by default!
	byte minpriority=0xFF; //Min priority reversed!
	byte block_textsurfaces;
	block_textsurfaces = 0; //Default: not blocking text surface inputs!
	if (EMU_RUNNING==1) //Handle light pen as well?
	{
		if ((finger==0xFF) && (EMU_RUNNING==1)) //Right mouse button? Handle as lightpen input activation!
		{
			lightpen_status |= 1; //Capture as lightpen!
			block_textsurfaces = 1; //Block all text surfaces!
		}
		else if ((Mouse_buttons2 & 4) && (EMU_RUNNING==1)) //Middle mouse button is pressed?
		{
			if (Mouse_buttons2 & 2) //Right mouse button on touch input is pressed?
			{
				if (finger < 0xFE) //Not a mouse button?
				{
					if (trackedlightpenpointerfinger == -1) //Nothing tracked yet?
					{
						lightpen_status |= 2; //Capturing the light pen!
						trackedlightpenpointerfinger = (sword)finger; //We're tracking this finger now as the cursor location input to the light pen!
						block_textsurfaces = 1; //Block all text surfaces!
					}
					else if (trackedlightpenbuttonfinger == -1) //Already tracking a pointer for the light pen? Perform as the light pen button finger instead when not registering it yet!
					{
						trackedlightpenbuttonfinger = (sword)finger; //We're tracking this finger now as the button input to the light pen!
						block_textsurfaces = 1; //Block all text surfaces!
					}
				}
			}
		}
	}
	if (lightpen_status) //Lightpen active?
	{
		if ((finger==0xFE) && (lightpen_status&1)) //Left mouse button? Handle as lightpen pressing!
		{
			lightpen_pressed = 1; //We're pressed!
			lightpensourced_pressed |= 1; //We're pressed!
			block_textsurfaces = 1; //Block all text surfaces!
		}
		if (trackedlightpenbuttonfinger != -1) //Tracking a button finger on the light pen?
		{
			lightpen_pressed = 1; //We're pressed!
			lightpensourced_pressed |= 2; //We're pressed!
		}
		if ((((finger == 0xFE)|(finger == 0xFF)) && (lightpen_status&1)) || (((sword)finger == trackedlightpenpointerfinger) && (trackedlightpenpointerfinger != -1))) //Real mouse movement or tracked light pen pointer finger?
		{
			updateLightPenLocation(x, y); //Update the light pen location!
		}
	}
	else
	{
		lightpen_x = lightpen_y = -1; //No lightpen used!
	}

	//Apply the prioritized list, but priority between equal levels based on rendering order(foreground over background)!
	if (block_textsurfaces) //Blocking the text surfaces from registering input?
	{
		return 1; //Abort: don't apply text surface inputs!
	}
	for (i = maxSurfaces; i >= 0; --i) //Process all registered surfaces in front to back order!
	{
		if (GPU.textsurfaces[i]) //Registered?
		{
			priority = GPU_textpriority(GPU.textsurfaces[i], x, y);
			if ((byte)priority > maxpriority) maxpriority = priority; //New max!
			if ((byte)priority < minpriority) minpriority = priority; //New min!
			GPUtext_priorities[i] = priority; //Set the priority!
		}
		else GPUtext_priorities[i] = 0; //Undefined!
	}
	for (priority = maxpriority; priority >= (int)minpriority; --priority) //Handle from high to low priority!
	{
		for (i = maxSurfaces; i >= 0; --i) //High to low priority order!
		{
			if (GPU.textsurfaces[i]) //Registered?
			{
				if (GPUtext_priorities[i] == (byte)priority) //Got priority?
				{
					if (GPU_textbuttondown(GPU.textsurfaces[i], finger, x, y)) //We're pressed here!
					{
						return 0; //Abort: don't let lower priority surfaces override us!
					}
				}
			}
		}
	}
	return 0; //Normal behaviour of handling!
}

byte GPU_surfaceclicked = 0; //Surface clicked to handle?

void GPU_mousebuttonup(word x, word y, byte finger)
{
	int i = 0;
	for (;i<(int)NUMITEMS(GPU.textsurfaces);i++) //Process all registered surfaces!
	{
		if (GPU.textsurfaces[i]) //Registered?
		{
			GPU_textbuttonup(GPU.textsurfaces[i],finger,x,y); //We're released here!
		}
	}
	GPU_surfaceclicked = 1; //Signal a click of a GPU surface!

	//Handle mouse inputs to the light pen now!

	if ((finger==0xFF) && (trackedlightpenpointerfinger==-1)) //Right mouse button released? Handle as lightpen input deactivation (but only if it's pointer finger isn't pointing anymore)!
	{
		lightpen_status &= ~1; //Not tracking anymore!
		if (lightpen_status == 0) //Fully released?
		{
			lightpen_x = lightpen_y = -1; //Nothing pressed!
		}
	}
	else if (finger == 0xFF) //Needs partial removal from the light pen input?
	{
		lightpen_status &= ~1; //Removed the pointer only, but leave the triggered finger!
	}
	if (finger==0xFE) //Left mouse button released? Handle as lightpen button release always!
	{
		lightpensourced_pressed &= ~1; //Not pressed anymore!
		if (lightpensourced_pressed == 0) //Fully released?
		{
			lightpen_pressed = 0; //Not pressed anymore!
		}
	}

	if (trackedlightpenpointerfinger != -1) //Tracking a light pen pointer using a finger?
	{
		if ((sword)finger == trackedlightpenpointerfinger) //We've stopped pointing?
		{
			trackedlightpenpointerfinger = -1; //Not pointing anymore!
			lightpen_status &= ~2; //Stop it's capture as a light pen!
			if (lightpen_status == 0) //Fully released now?
			{
				lightpen_x = lightpen_y = -1; //No location on the light pen anymore!
			}
		}
	}
	if (((sword)finger == trackedlightpenbuttonfinger) && (trackedlightpenbuttonfinger != -1)) //Tracking a light pen button using a finger?
	{
		trackedlightpenbuttonfinger = -1; //We're released, so not tracking it anymore!
		lightpensourced_pressed &= ~2; //Not pressed anymore!
		if (lightpensourced_pressed == 0) //Fully released?
		{
			lightpen_pressed = 0; //Not pressed anymore!
		}
	}
}

void GPU_mousemove(word x, word y, byte finger)
{
	if (lightpen_status) //Lightpen is active?
	{
		if ((lightpen_status&1) && (finger >= 0xFE)) //Mouse is always light pen in this case!
		{
			updateLightPenLocation(x, y); //Update the light pen location!
		}
		else if (((sword)finger == trackedlightpenpointerfinger) && (trackedlightpenpointerfinger != -1)) //It's the tracked lightpen pointer finger?
		{
			//When pressed this way, we're always tracking!
			updateLightPenLocation(x, y); //Update the light pen location!
		}
	}
}

byte framesrendered = 0; //Frames rendered!

void GPU_tickVideo()
{
	currentRenderTiming += (DOUBLE)getnspassed(&renderTiming); //Add the time passed to calculate!
	if (currentRenderTiming >= renderTimeout) //Timeout?
	{
		currentRenderTiming = (DOUBLE)fmod(currentRenderTiming,renderTimeout); //Rest time to count!

		#ifdef UNIPCEMU
		UniPCemu_onRenderingFrame(); //Rendering a frame!
		#endif

		refreshscreen(); //Refresh the screen!
		++framesrendered;
		if (framesrendered >= 6) //10FPS rate(we're at 60FPS, so 10 FPS is each 6 frames)?
		{
			framesrendered = 0; //Reset!
			#ifdef UNIPCEMU
			UniPCemu_afterRenderingFrameFPS(); //Rendering a frame!
			#endif
		}
	}
}

void initGPUdata() //Initialize all GPU tracking data etc!
{
	framesrendered = 0; //Nothing rendered yet!
	GPU_surfaceclicked = 0; //Surface clicked to handle?
	lightpen_x = -1;
	lightpen_y = -1; //Current lightpen location, if any!
	lightpen_pressed = 0; //Lightpen pressed?
	lightpen_status = 0; //Are we capturing lightpen motion and presses?

	//Support for the tracked light pen inputs using touch inputs!
	lightpensourced_pressed = 0; //What sources are still pressed?
	trackedlightpenpointerfinger = -1; //Currently tracked pointer finger for tracking !
	trackedlightpenbuttonfinger = -1; //Currently tracked button finger for tracking !
}