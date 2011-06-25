/**
 * Mupen64 - vi.cpp
 * Copyright (C) 2002 Hacktarux
 *
 * Mupen64 homepage: http://mupen64.emulation64.com
 * email address: hacktarux@yahoo.fr
 * 
 * If you want to contribute to the project please contact
 * me first (maybe someone is already making what you are
 * planning to do).
 *
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
 *
 * This program is distributed in the hope that it will be use-
 * ful, but WITHOUT ANY WARRANTY; without even the implied war-
 * ranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public Licence for more details.
 *
 * You should have received a copy of the GNU General Public
 * Licence along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
**/

#include <stdio.h>
#include <math.h>

#include "vi.h"
#include "global.h"
#include "color.h"

VI::VI(GFX_INFO info) : gfxInfo(info), bpp(0)
{
}

VI::~VI()
{
}

void VI::statusChanged()
{
   switch (*gfxInfo.VI_STATUS_REG & 3)
     {
      case 2:
	if (bpp != 16)
	  {
	     bpp = 16;
	     setVideoMode(640, 480);
	  }
	break;
      case 3:
	if (bpp != 32)
	  {
	     printf("VI:32bits\n");
	     bpp =32;
	  }
	break;
     }
}

void VI::widthChanged()
{
   /*switch(gfxInfo.HEADER[0x3c])
     {
      case 0x44:
      case 0x46:
      case 0x49:
      case 0x50:
      case 0x53:
      case 0x55:
      case 0x58:
      case 0x59:
	printf("VI:pal rom\n");
	break;
     }
   width = *gfxInfo.VI_WIDTH_REG;
   height = width * 3 / 4;
   initMode();*/
}

void VI::updateScreen()
{
   if (!bpp) return;
   if (!*gfxInfo.VI_WIDTH_REG) return;
   int h_end = *gfxInfo.VI_H_START_REG & 0x3FF;
   int h_start = (*gfxInfo.VI_H_START_REG >> 16) & 0x3FF;
   int v_end = *gfxInfo.VI_V_START_REG & 0x3FF;
   int v_start = (*gfxInfo.VI_V_START_REG >> 16) & 0x3FF;
   float scale_x = ((int)*gfxInfo.VI_X_SCALE_REG & 0xFFF) / 1024.0f;
   float scale_y = (((int)*gfxInfo.VI_Y_SCALE_REG & 0xFFF)>>1) / 1024.0f;
   
   short *im16 = (short*)((char*)gfxInfo.RDRAM +
			  (*gfxInfo.VI_ORIGIN_REG & 0x7FFFFF));
   short *buf16 = (short*)getScreenPointer();
   int pitch = getScreenPitch();
   int minx = (640-(h_end-h_start))/2;
   int maxx = 640-minx;
   int miny = (480-(v_end-v_start))/2;
   int maxy = 480-miny;
   float px, py;
   py=0;

   if ((*gfxInfo.VI_STATUS_REG & 0x30) == 0x30) // not antialiased
     {
	for (int j=0; j<480; j++)
	  {
	     if (j < miny || j > maxy)
	       for (int i=0; i<640; i++)
		 buf16[j*pitch+i] = 0;
	     else
	       {
		  px=0;
		  for (int i=0; i<640; i++)
		    {
		       if (i < minx || i > maxx)
			 buf16[j*pitch+i] = 0;
		       else
			 {
			    buf16[j*pitch+i] = 
			      im16[((int)py*(*gfxInfo.VI_WIDTH_REG)+(int)px)^S16]>>1;
			    px += scale_x;
			 }
		    }
		  py += scale_y;
	       }
	  }
     }
   else
     {
	for (int j=0; j<480; j++)
	  {
	     if (j < miny || j > maxy)
	       for (int i=0; i<640; i++)
		 buf16[j*pitch+i] = 0;
	     else
	       {
		  px=0;
		  for (int i=0; i<640; i++)
		    {
		       if (i < minx || i > maxx)
			 buf16[j*pitch+i] = 0;
		       else
			 {
			    bool xint = (px - (int)px) == 0.0f, yint = (py - (int)py) == 0.0f;
			    if (xint && yint)
			      {
				 buf16[j*pitch+i] = 
				   im16[((int)py*(*gfxInfo.VI_WIDTH_REG)+(int)px)^S16]>>1;
			      }
			    else if (yint)
			      {
				 Color16 l,r;
				 int w = *gfxInfo.VI_WIDTH_REG;
				 l=im16[((int)py*w+(int)px)^S16];
				 r=im16[((int)py*w+(int)(px+1.0f))^S16];
				 buf16[j*pitch+i] = 
				   (int)(l*(1.0f-(px-(int)px))+r*(px-(int)px))>>1;
			      }
			    else if (xint)
			      {
				 Color16 t,b;
				 int w = *gfxInfo.VI_WIDTH_REG;
				 t=im16[((int)py*w+(int)px)^S16];
				 b=im16[((int)(py+1)*w+(int)px)^S16];
				 buf16[j*pitch+i] = 
				   (int)(t*(1-(py-(int)py))+b*(py-(int)py))>>1;
			      }
			    else
			      {
				 Color16 t,b,l,r;
				 int w = *gfxInfo.VI_WIDTH_REG;
				 l=im16[((int)py*w+(int)px)^S16];
				 r=im16[((int)py*w+(int)(px+1))^S16];
				 t=l*(1-(px-(int)px))+r*(px-(int)px);
				 l=im16[((int)(py+1)*w+(int)px)^S16];
				 r=im16[((int)(py+1)*w+(int)(px+1))^S16];
				 b=l*(1-(px-(int)px))+r*(px-(int)px);
				 buf16[j*pitch+i] = 
				   (int)(t*(1-(py-(int)py))+b*(py-(int)py))>>1;
			      }
			    px += scale_x;
			 }
		    }
		  py += scale_y;
	       }
	  }
     }

   blit();
}

void VI::debug_plot(int x, int y, int c)
{
   short *buf16 = (short*)getScreenPointer();
   buf16[y*640+x] = c>>1;
}

void VI::flush()
{
   blit();
}
