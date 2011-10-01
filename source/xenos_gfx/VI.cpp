/**
 * glN64_GX - VI.cpp
 * Copyright (C) 2003 Orkin
 * Copyright (C) 2008, 2009 sepp256 (Port to Wii/Gamecube/PS3)
 *
 * glN64 homepage: http://gln64.emulation64.com
 * Wii64 homepage: http://www.emulatemii.com
 * email address: sepp256@gmail.com
 *
**/

#include <stdio.h>
#include <malloc.h>

#include "xenos_gfx.h"
#include "Types.h"
#include "VI.h"
#include "fakegl.h"
#include "N64.h"
#include "gSP.h"
#include "gDP.h"
#include "RSP.h"
//#include "FrameBuffer.h"
#include "Debug.h"

VIInfo VI;

void VI_UpdateSize()
{
	f32 xScale = _FIXED2FLOAT( _SHIFTR( *REG.VI_X_SCALE, 0, 12 ), 10 );
	//f32 xOffset = _FIXED2FLOAT( _SHIFTR( *REG.VI_X_SCALE, 16, 12 ), 10 );

	f32 yScale = _FIXED2FLOAT( _SHIFTR( *REG.VI_Y_SCALE, 0, 12 ), 10 );
	//f32 yOffset = _FIXED2FLOAT( _SHIFTR( *REG.VI_Y_SCALE, 16, 12 ), 10 );

	u32 hEnd = _SHIFTR( *REG.VI_H_START, 0, 10 );
	u32 hStart = _SHIFTR( *REG.VI_H_START, 16, 10 );

	// These are in half-lines, so shift an extra bit
	u32 vEnd = _SHIFTR( *REG.VI_V_START, 1, 9 );
	u32 vStart = _SHIFTR( *REG.VI_V_START, 17, 9 );

	VI.width = (unsigned long)((hEnd - hStart) * xScale);
	VI.height = (unsigned long)((vEnd - vStart) * yScale * 1.0126582f);

	if (VI.width == 0.0f) VI.width = (unsigned long)320.0f;
	if (VI.height == 0.0f) VI.height = (unsigned long)240.0f;
	
	xe_updateVSOrtho();
}

void VI_UpdateScreen()
{
    xeGfx_render();
#if 0
#ifndef __GX__
	glFinish();

	if (OGL.frameBufferTextures)
	{
		FrameBuffer *current = FrameBuffer_FindBuffer( *REG.VI_ORIGIN );

		if ((*REG.VI_ORIGIN != VI.lastOrigin) || ((current) && current->changed))
		{
			if (gDP.colorImage.changed)
			{
				FrameBuffer_SaveBuffer( gDP.colorImage.address, gDP.colorImage.size, gDP.colorImage.width, gDP.colorImage.height );
				gDP.colorImage.changed = FALSE;
			}

			FrameBuffer_RenderBuffer( *REG.VI_ORIGIN );

			gDP.colorImage.changed = FALSE;
			VI.lastOrigin = *REG.VI_ORIGIN;
#ifdef DEBUG
			while (Debug.paused && !Debug.step);
			Debug.step = FALSE;
#endif
		}
	}
	else
	{
		if (gSP.changed & CHANGED_COLORBUFFER)
		{
#ifndef __LINUX__
			SwapBuffers( OGL.hDC );
#else
			OGL_SwapBuffers();
#endif
			gSP.changed &= ~CHANGED_COLORBUFFER;
#ifdef DEBUG
			while (Debug.paused && !Debug.step);
			Debug.step = FALSE;
#endif
		}
	}
	glFinish();
#else // !__GX__
	if (renderCpuFramebuffer)
	{
		//Only render N64 framebuffer in RDRAM and not EFB
		VI_GX_cleanUp();
		VI_GX_renderCpuFramebuffer();
		VI_GX_showFPS();
		VI_GX_showDEBUG();
		GX_SetCopyClear ((GXColor){0,0,0,255}, 0xFFFFFF);
		GX_CopyDisp (VI.xfb[VI.which_fb]+GX_xfb_offset, GX_FALSE);
		GX_DrawDone(); //Wait until EFB->XFB copy is complete
		VI.enableLoadIcon = true;
		VI.EFBcleared = false;
		VI.copy_fb = true;
	}

	if (OGL.frameBufferTextures)
	{
		FrameBuffer *current = FrameBuffer_FindBuffer( *REG.VI_ORIGIN );

		if ((*REG.VI_ORIGIN != VI.lastOrigin) || ((current) && current->changed))
		{
			FrameBuffer_IncrementVIcount();
			if (gDP.colorImage.changed)
			{
				FrameBuffer_SaveBuffer( gDP.colorImage.address, gDP.colorImage.size, gDP.colorImage.width, gDP.colorImage.height );
				gDP.colorImage.changed = FALSE;
			}

			FrameBuffer_RenderBuffer( *REG.VI_ORIGIN );

			//Draw DEBUG to screen
			VI_GX_cleanUp();
			VI_GX_showFPS();
			VI_GX_showDEBUG();
			GX_SetCopyClear ((GXColor){0,0,0,255}, 0xFFFFFF);
			//Copy EFB->XFB
			GX_CopyDisp (VI.xfb[VI.which_fb]+GX_xfb_offset, GX_FALSE);
			GX_DrawDone(); //Wait until EFB->XFB copy is complete
			VI.updateOSD = false;
			VI.enableLoadIcon = true;
			VI.copy_fb = true;

			//Restore current EFB
			FrameBuffer_RestoreBuffer( gDP.colorImage.address, gDP.colorImage.size, gDP.colorImage.width );

			gDP.colorImage.changed = FALSE;
			VI.lastOrigin = *REG.VI_ORIGIN;
		}
	}
	else
	{
/*		if (gSP.changed & CHANGED_COLORBUFFER)
		{
			OGL_SwapBuffers();
			gSP.changed &= ~CHANGED_COLORBUFFER;
		}*/
		if(VI.updateOSD && (gSP.changed & CHANGED_COLORBUFFER))
		{
			VI_GX_cleanUp();
			VI_GX_showFPS();
			VI_GX_showDEBUG();
			GX_SetCopyClear ((GXColor){0,0,0,255}, 0xFFFFFF);
			GX_CopyDisp (VI.xfb[VI.which_fb]+GX_xfb_offset, GX_FALSE);
			GX_DrawDone(); //Wait until EFB->XFB copy is complete
			VI.updateOSD = false;
			VI.enableLoadIcon = true;
			VI.EFBcleared = false;
			VI.copy_fb = true;
			gSP.changed &= ~CHANGED_COLORBUFFER;
		}
	}
#endif // __GX__
#endif
}

