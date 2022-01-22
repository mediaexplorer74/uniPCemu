//

#include "headers/support/locks.h" //Locking support!
#include "headers/emu/gpu/gpu.h" //GPU typedefs etc.
#include "headers/support/zalloc.h" //For registering our data we've allocated!
#include "headers/emu/gpu/gpu_sdl.h" //SDL support!
#include "headers/support/log.h" //Logging support!

//Log put_pixel_row errors?
//#define PPRLOG

//SDL1 vs SDL2 compatibility support!

/*
SDL_rotozoom.c: rotozoomer, zoomer and shrinker for 32bit or 8bit surfaces
*/

//Speed patches to SDL_gfx's zoomSurfaceRGBA only(the only used function) to work faster in this implementation(keeping lookup tables between resizes, preventing recalculation and reallocations when not needed(same source and destination resolutions))!
#include "headers/packed.h"
typedef union
{
	uint_32 RGBA; //Full color!
	struct PACKED
	{
		byte r; //Red channel!
		byte g; //Green channel!
		byte b; //Blue channel!
		byte a; //Alpha channel!
	};
} tColorRGBA;
#include "headers/endpacked.h"

//zoomSurfaceRGBA from SDL_gfx(the only functionality used from the project): Zooms a surface from src to dst (flipx&y=flip), SMOOTH=SMOOTHING_ON.
//It's been adjusted to store it's precalculation tables in the destination surface for easier recalculation.
//1 on error, 0 on rendered.
byte zoomSurfaceRGBA(GPU_SDL_Surface * src, GPU_SDL_Surface * dst, byte dounlockGPU)
{
	int x, y, sx, sy, ssx, ssy, *sax, *say, *csax, *csay, *salast, csx, csy;
	INLINEREGISTER int ex,ey;
	int cx, cy, sstep, sstepx, sstepy;
	uint_32 *c00, *c01, *c10, *c11;
	INLINEREGISTER uint_32 c00c, c01c, c10c, c11c; //Full colors loaded!

	tColorRGBA *sp, *csp;
	INLINEREGISTER tColorRGBA *dp;
	int spixelgap, spixelw, spixelh, dgap, t1, t2;

	/*
	* Precalculate row increments
	*/
	spixelw = (src->sdllayer->w - 1);
	spixelh = (src->sdllayer->h - 1);
	sx = (int)(65536.0 * (float)spixelw / (float)(dst->sdllayer->w - 1));
	sy = (int)(65536.0 * (float)spixelh / (float)(dst->sdllayer->h - 1));

	/* Maximum scaled source size */
	ssx = (src->sdllayer->w << 16) - 1;
	ssy = (src->sdllayer->h << 16) - 1;

	/* Precalculate horizontal row increments */
	
	//Different conversion?
	//RnD
	if ((!dst->hrowincrements) //|| (dst->hrowincrements_precalcs!=((src->sdllayer->w<<16)|dst->sdllayer->w))
		)
	{
		if ((sax = (int *)zalloc((dst->sdllayer->w + 1) * sizeof(Uint32),"RESIZE_XPRECALCS",NULL)) == NULL) return 1; //Error allocating!
		csx = 0;
		csax = sax;
		for (x = 0; x <= dst->sdllayer->w; x++) {
			*csax = csx;
			csax++;
			csx += sx;

			/* Guard from overflows */
			if (csx > ssx) {
				csx = ssx;
			}
		}
		if (dst->hrowincrements) //Already allocated?
		{
			freez((void **)&dst->hrowincrements, dst->hrowincrements_size, "RESIZE_XPRECALCS"); //Release the old precalcs! We're updating it!
		}
		dst->hrowincrements = sax; //Save the table for easier lookup!
		dst->hrowincrements_precalcs = ((src->sdllayer->w << 16) | dst->sdllayer->w); //We're adjusted to this size!
		dst->hrowincrements_size = (dst->sdllayer->w + 1) * sizeof(Uint32); //Save the size of the LUT!
	}
	else
	{
		sax = dst->hrowincrements; //Load the stored table for reuse!
	}

	/* Precalculate vertical row increments */
	if ((!dst->vrowincrements) || (dst->vrowincrements_precalcs!= ((src->sdllayer->h << 16) | dst->sdllayer->h))) //Different conversion?
	{
		if ((say = (int *)zalloc((dst->sdllayer->h + 1) * sizeof(Uint32),"RESIZE_YPRECALCS",NULL)) == NULL)	return 1; //Error allocating!
		csy = 0;
		csay = say;
		for (y = 0; y <= dst->sdllayer->h; y++) {
			*csay = csy;
			csay++;
			csy += sy;

			/* Guard from overflows */
			if (csy > ssy) {
				csy = ssy;
			}
		}
		if (dst->vrowincrements) //Already allocated?
		{
			freez((void **)&dst->vrowincrements,dst->vrowincrements_size,"RESIZE_YPRECALCS"); //Release the old precalcs! We're updating it!
		}
		dst->vrowincrements = say; //Save the table for easier lookup!
		dst->vrowincrements_precalcs = ((src->sdllayer->h << 16) | dst->sdllayer->h); //We're adjusted to this size!
		dst->vrowincrements_size = (dst->sdllayer->h + 1) * sizeof(Uint32); //Save the size of the LUT!
	}
	else
	{
		say = dst->vrowincrements; //Load the stored table for reuse!
	}

	sp = (tColorRGBA *)src->sdllayer->pixels;
	dp = (tColorRGBA *)dst->sdllayer->pixels;
	dgap = dst->sdllayer->pitch - dst->sdllayer->w * 4;
	spixelgap = src->sdllayer->pitch / 4;

	/*
	* Interpolating Zoom
	*/
	if (dounlockGPU) unlockGPU(); //Unlock te GPU during rendering!
	csay = say;
	for (y = 0; y < dst->sdllayer->h; y++) {
		csp = sp;
		csax = sax;
		for (x = 0; x < dst->sdllayer->w; x++) {
			/*
			* Setup color source pointers
			*/
			ex = (*csax & 0xffff);
			ey = (*csay & 0xffff);
			cx = (*csax >> 16);
			cy = (*csay >> 16);
			sstepx = cx < spixelw;
			sstepy = cy < spixelh;
			c00 = (uint_32 *)sp;
			c01 = (uint_32 *)sp;
			c10 = (uint_32 *)sp;
			if (sstepy) {
				c10 += spixelgap;
			}
			c11 = c10;
			if (sstepx) {
				c01++;
				c11++;
			}

			/*
			* Draw and interpolate colors
			*/
			c00c = *c00; //Load c00!
			c01c = *c01; //Load c01!
			c10c = *c10; //Load c10!
			c11c = *c11; //Load c11!
			t1 = (((((c01c&0xFF) - (c00c&0xFF)) * ex) >> 16) + (c00c&0xFF)) & 0xff;
			t2 = (((((c11c&0xFF) - (c10c&0xFF)) * ex) >> 16) + (c10c&0xFF)) & 0xff;
			dp->r = (((t2 - t1) * ey) >> 16) + t1;
			c00c >>= 8; //Next channel!
			c01c >>= 8; //Next channel!
			c10c >>= 8; //Next channel!
			c11c >>= 8; //Next channel!
			t1 = (((((c01c & 0xFF) - (c00c & 0xFF)) * ex) >> 16) + (c00c & 0xFF)) & 0xff;
			t2 = (((((c11c & 0xFF) - (c10c & 0xFF)) * ex) >> 16) + (c10c & 0xFF)) & 0xff;
			dp->g = (((t2 - t1) * ey) >> 16) + t1;
			c00c >>= 8; //Next channel!
			c01c >>= 8; //Next channel!
			c10c >>= 8; //Next channel!
			c11c >>= 8; //Next channel!
			t1 = (((((c01c & 0xFF) - (c00c & 0xFF)) * ex) >> 16) + (c00c & 0xFF)) & 0xff;
			t2 = (((((c11c & 0xFF) - (c10c & 0xFF)) * ex) >> 16) + (c10c & 0xFF)) & 0xff;
			dp->b = (((t2 - t1) * ey) >> 16) + t1;
			c00c >>= 8; //Next channel!
			c01c >>= 8; //Next channel!
			c10c >>= 8; //Next channel!
			c11c >>= 8; //Next channel!
			t1 = (((((c01c & 0xFF) - (c00c & 0xFF)) * ex) >> 16) + (c00c & 0xFF)) & 0xff;
			t2 = (((((c11c & 0xFF) - (c10c & 0xFF)) * ex) >> 16) + (c10c & 0xFF)) & 0xff;
			dp->a = (((t2 - t1) * ey) >> 16) + t1;
			/*
			* Advance source pointer x
			*/
			salast = csax;
			csax++;
			sstep = (*csax >> 16) - (*salast >> 16);
			sp += sstep;

			/*
			* Advance destination pointer x
			*/
			++dp;
		}
		/*
		* Advance source pointer y
		*/
		salast = csay;
		csay++;
		sstep = (*csay >> 16) - (*salast >> 16);
		sstep *= spixelgap;
		sp = csp + sstep;

		/*
		* Advance destination pointer y
		*/
		dp = (tColorRGBA *)((Uint8 *)dp + dgap);
	}
	if (dounlockGPU) lockGPU(); //Unlock te GPU during rendering!

	return 0; //OK!
}

//Original functionality
OPTINLINE word getlayerwidth(GPU_SDL_Surface *img)
{
	return img->sdllayer->w; //The width!
}

OPTINLINE word getlayerheight(GPU_SDL_Surface *img)
{
	return img->sdllayer->h; //The height!
}

OPTINLINE uint_32 getlayervirtualwidth(GPU_SDL_Surface *surface)
{
	return surface->pixelpitch; //Give the pixel pitch, in pixels!
}

OPTINLINE uint_32 **getlayerpixels(GPU_SDL_Surface *img)
{
	return (uint_32 **)&img->sdllayer->pixels; //A pointer to the pixels pointer itself!
}

//Basic Container/wrapper support
void freeSurfacePtr(void **ptr, uint_32 size, SDL_sem *lock) //Free a pointer (used internally only) allocated with nzalloc/zalloc and our internal functions!
{
	GPU_SDL_Surface *surface = (GPU_SDL_Surface *)*ptr; //Take the surface out of the pointer!
	if (surface->lock) WaitSem(surface->lock)
	if (!(surface->flags&SDL_FLAG_NODELETE)) //The surface is allowed to be deleted?
	{
		//Start by freeing the surfaces in the handlers!
		uint_32 pixels_size = (getlayerheight(surface)*get_pixelrow_pitch(surface))<<2; //Calculate surface pixels size!
		if (!(surface->flags&SDL_FLAG_NODELETE_PIXELS)) //Valid to delete?
		{
			unregisterptr(*getlayerpixels(surface),pixels_size); //Release the pixels within the surface!
		}
		if (unregisterptr(surface->sdllayer,sizeof(*surface->sdllayer))) //The surface itself!
		{
			//Next release the data associated with it using the official functionality!
			SDL_FreeSurface(surface->sdllayer); //Release the surface fully using native support!
		}
	}
	if (surface->hrowincrements) freez((void **)&surface->hrowincrements,surface->hrowincrements_size,"RESIZE_XPRECALCS"); //Release the horizontal row increments!
	if (surface->vrowincrements) freez((void **)&surface->vrowincrements, surface->vrowincrements_size, "RESIZE_YPRECALCS"); //Release the vertical row increments!
	if (surface->lock) PostSem(surface->lock) //We're done with the contents!
	changedealloc(surface, sizeof(*surface), getdefaultdealloc()); //Change the deallocation function back to it's default!
	//We're always allowed to release the container.
	if (surface->lock)
	{
		SDL_DestroySemaphore(surface->lock); //Destory the semaphore!
		surface->lock = NULL; //No lock anymore!
	}
	freez((void **)ptr, sizeof(GPU_SDL_Surface), "freeSurfacePtr GPU_SDL_Surface");
}

GPU_SDL_Surface *getSurfaceWrapper(SDL_Surface *surface) //Retrieves a surface wrapper to use with our functions!
{
	GPU_SDL_Surface *wrapper = NULL;
	wrapper = (GPU_SDL_Surface *)zalloc(sizeof(GPU_SDL_Surface),"GPU_SDL_Surface",NULL); //Allocate the wrapper!
	if (!wrapper) //Failed to allocate the wrapper?
	{
		return NULL; //Error!
	}
	//SDL1?
	wrapper->sdllayer = surface; //The surface to use within the wrapper!
	wrapper->lock = SDL_CreateSemaphore(1); //The lock!
	return wrapper; //Give the allocated wrapper!
}

//registration of a wrapped surface.
void registerSurface(GPU_SDL_Surface *surface, char *name, byte allowsurfacerelease) //Register a surface!
{
	if (!surface) return; //Invalid surface!
	if (!changedealloc(surface, sizeof(*surface), &freeSurfacePtr)) //We're changing the default dealloc function for our override!
	{
		return; //Can't change registry for 'releasing the surface container' handler!
	}
	if (!registerptr(surface->sdllayer, sizeof(*surface->sdllayer), name, NULL,NULL,allowsurfacerelease?2:0)) //The surface itself!
	{
		if (!memprotect(surface->sdllayer, sizeof(*surface->sdllayer), name)) //Failed to register?
		{
			dolog("registerSurface", "Registering the surface failed.");
			return;
		}
	}

	//Pixel pitch is also pre-registered: we're not changed usually, unless released!
	INLINEREGISTER uint_32 pitch;
	pitch = surface->sdllayer->pitch; //Load the pitch!
	if (pitch >= 4) //Got pitch?
	{
		surface->pixelpitch = (pitch >> 2); //Pitch in pixels!
	}
	else //Default width to work with?
	{
		surface->pixelpitch = getlayerwidth(surface); //Just use the width as a pitch to fall back to!
	}

	uint_32 pixels_size;
	pixels_size = (getlayerheight(surface)*get_pixelrow_pitch(surface))<<2; //The size of the pixels structure!
	if (!memprotect(*getlayerpixels(surface), pixels_size, NULL)) //Not already registered (fix for call from createSurfaceFromPixels)?
	{
		if (!registerptr(*getlayerpixels(surface), pixels_size, "Surface_Pixels", NULL,NULL,allowsurfacerelease?2:0)) //The pixels within the surface! We can't be released natively!
		{
			if (!memprotect(*getlayerpixels(surface), pixels_size, "Surface_Pixels")) //Not registered?
			{
				dolog("registerSurface", "Registering the surface pixels failed.");
				logpointers("registerSurface");
				unregisterptr(surface->sdllayer, sizeof(*surface->sdllayer)); //Undo!
				return;
			}
		}
	}

	//Next our userdata!
	surface->flags |= SDL_FLAG_DIRTY; //Initialise to a dirty surface (first rendering!)
	if (!allowsurfacerelease) //Don't allow surface release?
	{
		surface->flags |= SDL_FLAG_NODELETE; //Don't delete the surface!
	}
}

//Memory value comparision.

//Returns 1 on not zero, 0 on zero!
OPTINLINE byte filledmem(void *start, uint_32 size)
{
	INLINEREGISTER uint_32 *current = (uint_32 *)start; //Convert to byte list!
	INLINEREGISTER uint_32 *ending = &current[size]; //The end of the list to check!
	INLINEREGISTER byte result = 0; //Default: equal!
	if (size) //Gotten size?
	{
		do //Check the data!
		{
			if (*current) //Gotten a different value?
			{
				result = 1; //Set changed!
				break;
			}
		} while (++current!=ending); //Loop while not finished checking!
	}
	return result; //Give the result!
}

//Returns 1 on not equal, 0 on equal!
OPTINLINE byte memdiff(void *start, void *value, uint_32 size)
{
	return !!memcmp(start,value,((size_t)size)<<2); //Simply convert memory difference to 0-1!
}

//Color key matching.
OPTINLINE void matchColorKeys(const GPU_SDL_Surface* src, GPU_SDL_Surface* dest ){
	if (!(src && dest)) return; //Abort: invalid src/dest!
	if (!memprotect((void *)src,sizeof(*src),NULL) || !memprotect((void *)dest,sizeof(dest),NULL)) return; //Invalid?
    #ifdef SDL2//#ifndef SDL2
	if( src->sdllayer->flags & SDL_SRCCOLORKEY )
	#endif
	{
        #ifdef SDL2//#ifndef SDL2
		Uint32 colorkey = src->sdllayer->format->colorkey;
		SDL_SetColorKey( dest->sdllayer, SDL_SRCCOLORKEY, colorkey );
		#else
		//SDL2?
		SDL_SetColorKey( dest->sdllayer, SDL_TRUE, SDL_MapRGBA(src->sdllayer->format,0xFF,0xFF,0xFF,0xFF));
		#endif
	}
}

void calcResize(int aspectratio, uint_32 originalwidth, uint_32 originalheight, uint_32 newwidth, uint_32 newheight, uint_32 *n_width, uint_32 *n_height, byte is_renderer)
{
	*n_width = newwidth;
	*n_height = newheight; //New width/height!
	if (aspectratio) //Keeping the aspect ratio?
	{
		#if !defined(STATICSCREEN)
		//Only with windows used!
		#ifdef GBEMU
		if (((aspectratio >= 2) || (aspectratio <= 8)) && is_renderer) //Render to the window of forced size?
		#else
		if (((aspectratio>=2) || (aspectratio<=7)) && is_renderer) //Render to the window of forced size?
		#endif
		{
			switch (aspectratio)
			{
				case 2: //4:3(VGA)
				case 3: //CGA
				case 4: //4:3
				case 5: //4:3
				case 6: //4K
				#ifdef GBEMU
				case 7: //Gameboy original
				case 8: //Gameboy big
				#else
				case 7: //4:3 4K
				#endif
					originalwidth = newwidth; //We're resizing the destination ratio itself instead!
					originalheight = newheight; //We're resizing the destination ratio itself instead!
					break;
				default: break; //Unknown mode!
			}
		}
		#endif
		DOUBLE ar = (DOUBLE)originalwidth / (DOUBLE)originalheight; //Source surface aspect ratio!
		DOUBLE newAr = (DOUBLE)*n_width / (DOUBLE)*n_height; //Destination surface aspect ratio!
		switch (aspectratio) //Force aspect ratio?
		{
			case 2: //4:3
			case 4: //4:3
			case 5: //4:3
			#ifndef GBEMU
			case 7: //4:3
			#endif
				ar = (DOUBLE)(4.0 / 3.0); //We're taking 4:3 aspect ratio instead of the aspect ratio of the image!
				break;
			case 3: //CGA
				ar = (DOUBLE)(379.83 / 242.5); //We're taking CGA aspect ratio instead of the aspect ratio of the image!
				break;
			case 6: //4K
				ar = (DOUBLE)(3840.0 / 2160.0); //We're taking 4K aspect ratio instead of the aspect ratio of the image!
				break;
			default: //Keep the aspect ratio!
				break;
		}
		DOUBLE f = MAX(ar, newAr);
		if (f == ar) //Fit to width?
		{
			*n_height = (uint_32)(((DOUBLE)*n_width) / ar);
		}
		else //Fit to height?
		{
			*n_width = (uint_32)(*n_height*ar);
		}
	}
}

//Resizing.
byte resizeImage( GPU_SDL_Surface *img, GPU_SDL_Surface **dstimg, const uint_32 newwidth, const uint_32 newheight, int aspectratio, byte dounlockGPU)
{
	if ((!img) || (!dstimg)) //No image to resize or resize to?
	{
		return 0; //Nothin to resize is nothing back!
	}
	if ((!getlayerwidth(img)) || (!getlayervirtualwidth(img)) || (!getlayerheight(img)) || (!newwidth) || (!newheight)) //No size to resize?
	{
		return 0; //Nothing to resize!
	}

	//Calculate destination resolution!
	uint_32 n_width, n_height;
	calcResize(aspectratio,getlayerwidth(img),getlayerheight(img),newwidth,newheight,&n_width,&n_height,0); //Calculate the resize size!

	if (!n_width || !n_height) //No size in src or dest?
	{
		return 0; //Nothing to render, so give nothing!
	}

	//Calculate factor to destination resolution!

	if (*dstimg) //Gotten a destination image already?
	{
		if ((((GPU_SDL_Surface *)*dstimg)->sdllayer->w!=n_width) || (((GPU_SDL_Surface *)*dstimg)->sdllayer->h!=n_height)) //Destination size doesn't match?
		{
			freez((void **)dstimg,sizeof(**dstimg),"GPU_SDL_Surface"); //Release the surface: we're recreating it!
		}
	}
	if (!*dstimg) //Do we need to recreate the destination surface?
	{
		*dstimg = createSurface(n_width,n_height); //Create the destination surface to plot to!
		if (!*dstimg) //Failed to allocate the destination?
		{
			return 0; //Failed to resize: not enough memory?
		}
	}
	//Now the destination surface is ready for the resizing process!
	//Apply smoothing always, since disabling it will result in black scanline insertions!
	if (zoomSurfaceRGBA( img, *dstimg, dounlockGPU)) //Resize the image to the destination size!
	{
		//We've failed zooming!
		return 0; //Failed zooming!
	}
	((GPU_SDL_Surface *)(*dstimg))->flags |= SDL_FLAG_DIRTY; //Mark as dirty by default!
	matchColorKeys( img, *dstimg ); //Match the color keys!

	return 1; //We've been resized!
}

//Pixels between rows.
uint_32 get_pixelrow_pitch(GPU_SDL_Surface *surface) //Get the difference between two rows!
{
	if (unlikely(surface==0))
	{
		dolog("GPP","Pitch: invalid NULL-surface!");
		return 0; //No surface = no pitch!
	}
	return getlayervirtualwidth(surface); //Give the virtual width!
}

//Retrieve a pixel
uint_32 get_pixel(GPU_SDL_Surface* surface, const int x, const int y ){
	if (!surface) return 0; //Disable if no surface!
	Uint32 *pixels = (Uint32*)getlayerpixels(surface);
	if (((y * get_pixelrow_pitch(surface) ) + x)<((get_pixelrow_pitch(surface)*getlayerheight(surface))<<2)) //Valid?
	{
		return pixels[ ( y * get_pixelrow_pitch(surface) ) + x ];
	}
	return 0; //Invalid pixel!
}

//Is this a valid surface to use?
byte check_surface(GPU_SDL_Surface *surface)
{
	if (!surface) return 0; //Disable if no surface!
	if (!memprotect(surface, sizeof(*surface), NULL)) return 0; //Invalid surface!
	if (!memprotect(surface->sdllayer, sizeof(*surface->sdllayer), NULL)) return 0; //Invalid layer!
	if (!memprotect(surface->sdllayer->pixels,(getlayerheight(surface)*getlayervirtualwidth(surface))<<2,NULL)) return 0; //Invalid pixels!
	return 1; //Valid surface!
}

//Draw a pixel
void put_pixel(GPU_SDL_Surface *surface, const int x, const int y, const Uint32 pixel ){
	if (unlikely(y >= getlayerheight(surface))) return; //Invalid row!
	if (unlikely(x >= getlayerwidth(surface))) return; //Invalid column!
	Uint32 *pixels = (Uint32 *)*getlayerpixels(surface);
	Uint32 *pixelpos = &pixels[ ( y * get_pixelrow_pitch(surface) ) + x ]; //The pixel!
	if (unlikely(*pixelpos!=pixel)) //Different?
	{
		surface->flags |= SDL_FLAG_DIRTY; //Mark as dirty!
		*pixelpos = pixel;
	}
}

//Retrieve a pixel/row(pixel=0).
OPTINLINE void *get_pixel_ptr(GPU_SDL_Surface *surface, const int y, const int x)
{
	if (!surface) return NULL; //Invalid surface altogether!
	if ((y<getlayerheight(surface)) && (x<getlayerwidth(surface))) //Within range?
	{
		uint_32 *pixels = (uint_32 *)*getlayerpixels(surface);
		uint_32 *result = &pixels[ ( y * get_pixelrow_pitch(surface) ) + x ]; //Our result!
		//No pitch? Use width to fall back!
			return result; //The pixel ptr!
	}
	#ifdef PPRLOG
		else
		{
			dolog("PPR", "Get_pixel_ptr: Invalid row!");
		}
	#endif
	return NULL; //Out of range!
}

//Row functions, by me!
uint_32 *get_pixel_row(GPU_SDL_Surface *surface, const int y, const int x)
{
	return (uint_32 *)get_pixel_ptr(surface,y,x); //Give the pointer!
}

/*

put_pixel_row: Puts an entire row in the buffer!
parameters:
	surface: The surface to write to.
	y: The line to write to.
	rowsize: The ammount of pixels to consider to copy.
	pixels: The pixels to copy itself (uint_32 array).
	center: Centering flags:
		Bits 0-1: What centering action to use:
			=0: Left centering (Default)
			=1: Horizontal centering
			=2: Right centering
		Bit 2: To disable clearing on the line (for multiple data copies per row).
			=0: Clear both sides if available.
			=1: Disable clearing
	row_start: Where to start copying the pixels on the surface line. Only used when aligning left. Also affect left align clearing (As the screen is shifted to the right).


*/

void put_pixel_row(GPU_SDL_Surface *surface, const int y, uint_32 rowsize, uint_32 *pixels, int center, uint_32 row_start, word *xstart) //Based upon above, but for whole rows at once!
{
	uint_32 use_rowsize;
	uint_32 *row;
	if (surface && pixels) //Got surface and pixels!
	{
		if (y >= getlayerheight(surface)) return; //Invalid row detection!
		use_rowsize = MIN(get_pixelrow_pitch(surface),rowsize); //Minimum is decisive!
		if (use_rowsize) //Got something to copy and valid row?
		{
			if ((row_start+use_rowsize)>get_pixelrow_pitch(surface) && ((!(center&3)) && (!(center&4)))) //More than we can handle?
			{
				use_rowsize -= ((row_start+use_rowsize)-get_pixelrow_pitch(surface)); //Make it no larger than the surface can handle (no overflow protection)!
			}
			if (use_rowsize>0) //Gotten row size to copy?
			{
				row = get_pixel_row(surface,y,0); //Row at the left!
				if (row && (surface->sdllayer!=(SDL_Surface *)~0)) //Gotten the row (valid row?)
				{
					uint_32 restpixels = (getlayervirtualwidth(surface))-use_rowsize; //Rest ammount of pixels!
					uint_32 start = (getlayervirtualwidth(surface)>>1) - (use_rowsize>>1); //Start of the drawn part!
					switch (center&3) //What centering method?
					{
					case 2: //Right side plot?
						//Just plain plot at the right, filling with black on the left when not centering!
						if ((restpixels>0) && (!(center&4))) //Still a part of the row not rendered and valid rest location?
						{
							if (filledmem(row, restpixels)) //Different?
							{
								surface->flags |= SDL_FLAG_DIRTY; //Mark as dirty!
								memset(row, 0, ((size_t)restpixels)<<2); //Clear to the start of the row, so that only the part we specified gets something!
							}
						}
						if (xstart) *xstart = (word)restpixels; //Start of the row to apply, in pixels!
						if (memdiff(&row[restpixels],pixels,use_rowsize)) //Different?
						{
							surface->flags |= SDL_FLAG_DIRTY; //Mark as dirty!
							memcpy(&row[restpixels], pixels, ((size_t)use_rowsize)<<2); //Copy the row to the buffer as far as we can go!
						}
						break;
					case 1: //Use horizontal centering?
						if ((sword)getlayervirtualwidth(surface)>(sword)(use_rowsize+2)) //We have space left&right to plot? Also must have at least 2 pixels left&right to center!
						{
							if (!(center&4)) //Clear enabled?
							{
								if (filledmem(row, start)) //Different left or right?
								{
									surface->flags |= SDL_FLAG_DIRTY; //Mark as dirty!
									memset(row, 0, ((size_t)start) << 2); //Clear the left!
								}
								if (filledmem(&row[start+use_rowsize],(getlayervirtualwidth(surface)-(start+use_rowsize)))) //Different left or right?
								{
									surface->flags |= SDL_FLAG_DIRTY; //Mark as dirty!
									memset(&row[start + use_rowsize], 0, ((size_t)(getlayervirtualwidth(surface) - (start + use_rowsize))) << 2); //Clear the right!
								}
							}
							if (xstart) *xstart = (word)start; //Start of the row to apply, in pixels!
							if (memdiff(&row[start], pixels, use_rowsize)) //Different?
							{
								surface->flags |= SDL_FLAG_DIRTY; //Mark as dirty!
								memcpy(&row[start], pixels, ((size_t)use_rowsize) << 2); //Copy the pixels to the center!
							}
							return; //Done: we've written the pixels at the center!
						}
					//We don't need centering: just do left side plot!
					default: //We default to left side plot!
					case 0: //Left side plot?
						restpixels -= row_start; //The pixels that are left are lessened by row_start in this mode too!
						if (xstart) *xstart = (word)row_start; //Start of the row to apply, in pixels!
						if (memdiff(&row[row_start],pixels,use_rowsize)) //Different, so draw?
						{
							surface->flags |= SDL_FLAG_DIRTY; //Mark as dirty!
							memcpy(&row[row_start], pixels, ((size_t)use_rowsize) << 2); //Copy the row to the buffer as far as we can go!
						}
						//Now just render the rest part of the line to black!
						if ((restpixels>0) && (!(center&4))) //Still a part of the row not rendered and valid rest location and not disable clearing?
						{
							if (filledmem(&row[row_start + use_rowsize], restpixels)) //Different?
							{
								surface->flags |= SDL_FLAG_DIRTY; //Mark as dirty!
								memset(&row[row_start + use_rowsize], 0, ((size_t)restpixels) << 2); //Clear to the end of the row, so that only the part we specified gets something!
							}
						}
						break;
					}
				}
				else
				{
#ifdef PPRLOG
					dolog("PPR", "Invalid surface row:%u!", y);
#endif
				}
			}
		}
#ifdef PPRLOG
		else
		{
			dolog("PPR","Invalid row size: Surface: %u, Specified: %u",get_pixelrow_pitch(surface),rowsize); //Log it!
		}
#endif
	}
	else if (surface && (!(center&4))) //Surface, but no pixels: clear the row? Also clearing must be enabled to do so.
	{
#ifdef PPRLOG
		dolog("PPR", "Rendering empty pixels because of invalid data to copy.");
#endif
		row = get_pixel_row(surface,y,0); //Row at the left!
		if (row && getlayervirtualwidth(surface)) //Got row?
		{
			if (filledmem(row, getlayervirtualwidth(surface))) //Different?
			{
				surface->flags |= SDL_FLAG_DIRTY; //Mark as dirty!
				memset(row, 0, ((size_t)getlayervirtualwidth(surface)) << 2); //Clear the row, because we have no pixels!
			}
		}
	}
	else
	{
#ifdef PPRLOG
		dolog("PPR", "Invalid surface specified!");
#endif
	}
}

//Generate a byte order for SDL.
OPTINLINE void loadByteOrder(uint_32 *thermask, uint_32 *thegmask, uint_32 *thebmask, uint_32 *theamask)
{
	//Entirely dependant upon the system itself!
	*thermask = RGBA(0xFF,0x00,0x00,0x00);
	*thegmask = RGBA(0x00,0xFF,0x00,0x00);
	*thebmask = RGBA(0x00,0x00,0xFF,0x00);
	*theamask = RGBA(0x00,0x00,0x00,0xFF);
}

//Create a new surface.
GPU_SDL_Surface *createSurface(int columns, int rows) //Create a new 32BPP surface!
{
	uint_32 thermask=0,thegmask=0,thebmask=0,theamask=0; //Masks!
	loadByteOrder(&rmask,&gmask,&bmask,&amask); //Load our masks!
	#ifndef SDL2
	SDL_Surface *surface = SDL_CreateRGBSurface(SDL_SWSURFACE,columns,rows, 32, thermask,thegmask,thebmask,theamask); //Try to create it!
	#else
	SDL_Surface *surface = SDL_CreateRGBSurface(0,columns,rows, 32, thermask,thegmask,thebmask,theamask); //Try to create it!
	#endif
	if (!surface) //Failed to allocate?
	{
		return NULL; //Not allocated: we've failed to allocate the pointer!
	}
	GPU_SDL_Surface *wrapper;
	wrapper = getSurfaceWrapper(surface); //Give the surface we've allocated in the standard wrapper!
	registerSurface(wrapper,"SDL_Surface",1); //Register the surface we've wrapped!
	return wrapper;
}

//Create a new surface from an existing buffer.
GPU_SDL_Surface *createSurfaceFromPixels(int columns, int rows, void *pixels, uint_32 pixelpitch) //Create a 32BPP surface, but from an allocated/solid buffer (not deallocated when freed)! Can be used for persistent buffers (always there, like the GPU screen buffer itself)
{
	uint_32 thermask=0,thegmask=0,thebmask=0,theamask=0; //Masks!
	loadByteOrder(&thermask,&thegmask,&thebmask,&theamask); //Load our masks!

	pixelpitch <<= 2; //4 bytes a pixel!
	SDL_Surface *surface = SDL_CreateRGBSurfaceFrom(pixels,columns,rows, 32, pixelpitch, thermask,thegmask,thebmask,theamask); //Try to create it!
	if (!surface) //Failed to allocate?
	{
		return NULL; //Not allocated: we've failed to allocate the pointer!
	}
	GPU_SDL_Surface *wrapper;
	wrapper = getSurfaceWrapper(surface); //Give the surface we've allocated in the standard wrapper!
	registerSurface(wrapper,"SDL_Surface",1); //Register the surface we've wrapped!
	wrapper->flags |= SDL_FLAG_NODELETE_PIXELS; //Don't delete the pixels: we're protected from being deleted together with the surface!
	return wrapper;
}

//Release a surface.
GPU_SDL_Surface *freeSurface(GPU_SDL_Surface *surface)
{
	if (!surface) return NULL; //Invalid surface?
	if (memprotect(surface,sizeof(GPU_SDL_Surface),NULL)) //Allocated?
	{
		if (memprotect(*getlayerpixels(surface),(getlayerheight(surface)*get_pixelrow_pitch(surface))<<2,NULL)) //Pixels also allocated?
		{
			GPU_SDL_Surface *newsurface = surface; //Take the surface to use!
			freeSurfacePtr((void **)&newsurface,sizeof(*newsurface),NULL); //Release the surface via our kernel function!
			return newsurface; //We're released (or not)!
		}
	}
	return surface; //Still allocated!
}

#ifdef SDL2
extern SDL_Window *sdlWindow;
extern SDL_Renderer *sdlRenderer;
extern SDL_Texture *sdlTexture;
#endif

//Draw the screen with a surface.
void safeFlip(GPU_SDL_Surface *surface) //Safe flipping (non-null)
{
	if (memprotect(surface,sizeof(GPU_SDL_Surface),NULL)) //Surface valid and allowed to show pixels?
	{
		if (surface->flags&SDL_FLAG_DIRTY) //Dirty surface needs rendering only?
		{
			if (memprotect(surface->sdllayer,sizeof(*surface->sdllayer),NULL)) //Valid?
			{
				#ifndef SDL2
				if (SDL_Flip(surface->sdllayer)==-1) //Failed to update by flipping?
					SDL_UpdateRect(surface->sdllayer, 0, 0, 0, 0); //Make sure we update!
				#else
				//SDL2!
				// Update the texture based on the pixels to be displayed!
				SDL_UpdateTexture(sdlTexture, NULL, *getlayerpixels(surface), (get_pixelrow_pitch(surface)<<2));
				// Select the color for drawing. It is set to black here.
				SDL_SetRenderDrawColor(sdlRenderer, 0x00, 0x00, 0x00, SDL_ALPHA_OPAQUE);
				// Clear the entire screen to our selected color.
				SDL_RenderClear(sdlRenderer);
				// Copy over our display!
				SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);

				// Up until now everything was drawn behind the scenes.
				// This will show the new contents of the window.
				SDL_RenderPresent(sdlRenderer);
				#endif
			}
			surface->flags &= ~SDL_FLAG_DIRTY; //Not dirty anymore!
		}
	}
}
