/*
Copyright (C) 2003 Rice1964

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

/* edited to fit in xenon_gfx */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../main/winlnxdefs.h"
#include "../main/main.h"

#include "GFXPlugin.h"
#include "xenos_blender.h"

#include "Debug.h"
#include "Zilmar GFX 1.3.h"
#include "DepthBuffer.h"
#include "N64.h"
#include "RSP.h"
#include "RDP.h"
#include "VI.h"

#include <xenos/xe.h>

extern struct XenosDevice *xe;


int blend_src=XE_BLEND_ONE;
int blend_dst=XE_BLEND_ZERO;

static void SetAlphaTestEnable(int at){
	Xe_SetAlphaTestEnable(xe,at);
}

static void ForceAlphaRef(float ar){
	Xe_SetAlphaFunc(xe,/*(ar > 0.0f) ? XE_CMP_GREATEREQUAL :*/ XE_CMP_GREATER);
	Xe_SetAlphaRef(xe,ar);
}

static void BlendFunc(int src,int dst){
	blend_src=src;
	blend_dst=dst;
}

static void Disable(){
	Xe_SetBlendControl(xe,XE_BLEND_ONE,XE_BLENDOP_ADD,XE_BLEND_ZERO,XE_BLEND_ONE,XE_BLENDOP_ADD,XE_BLEND_ZERO);
}

static void Enable(){
	Xe_SetBlendControl(xe,blend_src,XE_BLENDOP_ADD,blend_dst,blend_src,XE_BLENDOP_ADD,blend_dst);
}


void applyAlphaMode(void)
{
    if ( gDP.otherMode.alphaCompare == 0 )
    {
        if ( gDP.otherMode.cvgXAlpha && (gDP.otherMode.alphaCvgSel || gDP.otherMode.AAEnable ) )
        {
            ForceAlphaRef(0.5f); // Strange, I have to use value=2 for pixel shader combiner for Nvidia FX5200
                                // for other video cards, value=1 is good enough.
            SetAlphaTestEnable(TRUE);
        }
        else
        {
            SetAlphaTestEnable(FALSE);
        }
    }
    else if ( gDP.otherMode.alphaCompare == 3 )
    {
        //RDP_ALPHA_COMPARE_DITHER
        SetAlphaTestEnable(FALSE);
    }
    else
    {
        if( (gDP.otherMode.alphaCvgSel ) && !gDP.otherMode.cvgXAlpha )
        {
            // Use CVG for pixel alpha
            SetAlphaTestEnable(FALSE);
        }
        else
        {
            // RDP_ALPHA_COMPARE_THRESHOLD || RDP_ALPHA_COMPARE_DITHER
            ForceAlphaRef(gDP.blendColor.a);
            SetAlphaTestEnable(TRUE);
        }
    }
}

void applyBlenderMode(void)                    // Set Alpha Blender mode
{
#define BLEND_NOOP              0x0000

#define BLEND_NOOP5             0xcc48  // Fog * 0 + Mem * 1
#define BLEND_NOOP4             0xcc08  // Fog * 0 + In * 1
#define BLEND_FOG_ASHADE        0xc800
#define BLEND_FOG_3             0xc000  // Fog * AIn + In * 1-A
#define BLEND_FOG_MEM           0xc440  // Fog * AFog + Mem * 1-A
#define BLEND_FOG_APRIM         0xc400  // Fog * AFog + In * 1-A

#define BLEND_BLENDCOLOR        0x8c88
#define BLEND_BI_AFOG           0x8400  // Bl * AFog + In * 1-A
#define BLEND_BI_AIN            0x8040  // Bl * AIn + Mem * 1-A

#define BLEND_MEM               0x4c40  // Mem*0 + Mem*(1-0)?!
#define BLEND_FOG_MEM_3         0x44c0  // Mem * AFog + Fog * 1-A

#define BLEND_NOOP3             0x0c48  // In * 0 + Mem * 1
#define BLEND_PASS              0x0c08  // In * 0 + In * 1
#define BLEND_FOG_MEM_IN_MEM    0x0440  // In * AFog + Mem * 1-A
#define BLEND_FOG_MEM_FOG_MEM   0x04c0  // In * AFog + Fog * 1-A
#define BLEND_OPA               0x0044  //  In * AIn + Mem * AMem
#define BLEND_XLU               0x0040
#define BLEND_MEM_ALPHA_IN      0x4044  //  Mem * AIn + Mem * AMem


    uint32_t blendmode_1 = (uint32_t)( (gDP.otherMode.l>>16) & 0xcccc );
    uint32_t blendmode_2 = (uint32_t)( (gDP.otherMode.l>>16) & 0x3333 );
    uint32_t cycletype = gDP.otherMode.cycleType;

    switch( cycletype )
    {
    case G_CYC_FILL:
        //BlendFunc(BLEND_ONE, BLEND_ZERO);
        //Enable();
        Disable();
        break;
    case G_CYC_COPY:
        //Disable();
        BlendFunc(XE_BLEND_ONE, XE_BLEND_ZERO);
        Enable();
        break;
    case G_CYC_2CYCLE:
        if( gDP.otherMode.forceBlender && gDP.otherMode.depthCompare )
        {
            BlendFunc(XE_BLEND_SRCALPHA, XE_BLEND_INVSRCALPHA);
            Enable();
            break;
        }

        /*
        if( gRDP.otherMode.alpha_cvg_sel && gRDP.otherMode.cvg_x_alpha==0 )
        {
            BlendFunc(BLEND_ONE, BLEND_ZERO);
            Enable();
            break;
        }
        */

        switch( blendmode_1+blendmode_2 )
        {
        case BLEND_PASS+(BLEND_PASS>>2):    // In * 0 + In * 1
        case BLEND_FOG_APRIM+(BLEND_PASS>>2):
            BlendFunc(XE_BLEND_ONE, XE_BLEND_ZERO);
            if( gDP.otherMode.alphaCvgSel )
            {
                Enable();
            }
            else
            {
                Disable();
            }

            SetAlphaTestEnable( ((gDP.otherMode.l) & 0x3)==1 ? TRUE : FALSE);
            break;
        case BLEND_PASS+(BLEND_OPA>>2):
            // 0x0c19
            // Cycle1:  In * 0 + In * 1
            // Cycle2:  In * AIn + Mem * AMem
            if( gDP.otherMode.cvgXAlpha && gDP.otherMode.alphaCvgSel )
            {
                BlendFunc(XE_BLEND_SRCALPHA, XE_BLEND_INVSRCALPHA);
                Enable();
            }
            else
            {
                BlendFunc(XE_BLEND_ONE, XE_BLEND_ZERO);
                Enable();
            }
            break;
        case BLEND_PASS + (BLEND_XLU>>2):
            // 0x0c18
            // Cycle1:  In * 0 + In * 1
            // Cycle2:  In * AIn + Mem * 1-A
        case BLEND_FOG_ASHADE + (BLEND_XLU>>2):
            //Cycle1:   Fog * AShade + In * 1-A
            //Cycle2:   In * AIn + Mem * 1-A    
        case BLEND_FOG_APRIM + (BLEND_XLU>>2):
            //Cycle1:   Fog * AFog + In * 1-A
            //Cycle2:   In * AIn + Mem * 1-A    
        //case BLEND_FOG_MEM_FOG_MEM + (BLEND_OPA>>2):
            //Cycle1:   In * AFog + Fog * 1-A
            //Cycle2:   In * AIn + Mem * AMem   
        case BLEND_FOG_MEM_FOG_MEM + (BLEND_PASS>>2):
            //Cycle1:   In * AFog + Fog * 1-A
            //Cycle2:   In * 0 + In * 1
        case BLEND_XLU + (BLEND_XLU>>2):
            //Cycle1:   Fog * AFog + In * 1-A
            //Cycle2:   In * AIn + Mem * 1-A    
        case BLEND_BI_AFOG + (BLEND_XLU>>2):
            //Cycle1:   Bl * AFog + In * 1-A
            //Cycle2:   In * AIn + Mem * 1-A    
        case BLEND_XLU + (BLEND_FOG_MEM_IN_MEM>>2):
            //Cycle1:   In * AIn + Mem * 1-A
            //Cycle2:   In * AFog + Mem * 1-A   
        case BLEND_PASS + (BLEND_FOG_MEM_IN_MEM>>2):
            //Cycle1:   In * 0 + In * 1
            //Cycle2:   In * AFog + Mem * 1-A   
            BlendFunc(XE_BLEND_SRCALPHA, XE_BLEND_INVSRCALPHA);
            Enable();
            break;
        case BLEND_FOG_MEM_FOG_MEM + (BLEND_OPA>>2):
            //Cycle1:   In * AFog + Fog * 1-A
            //Cycle2:   In * AIn + Mem * AMem   
            BlendFunc(XE_BLEND_ONE, XE_BLEND_ZERO);
            Enable();
            break;

        case BLEND_FOG_APRIM + (BLEND_OPA>>2):
            // For Golden Eye
            //Cycle1:   Fog * AFog + In * 1-A
            //Cycle2:   In * AIn + Mem * AMem   
        case BLEND_FOG_ASHADE + (BLEND_OPA>>2):
            //Cycle1:   Fog * AShade + In * 1-A
            //Cycle2:   In * AIn + Mem * AMem   
        case BLEND_BI_AFOG + (BLEND_OPA>>2):
            //Cycle1:   Bl * AFog + In * 1-A
            //Cycle2:   In * AIn + Mem * 1-AMem 
        case BLEND_FOG_ASHADE + (BLEND_NOOP>>2):
            //Cycle1:   Fog * AShade + In * 1-A
            //Cycle2:   In * AIn + In * 1-A
        case BLEND_NOOP + (BLEND_OPA>>2):
            //Cycle1:   In * AIn + In * 1-A
            //Cycle2:   In * AIn + Mem * AMem
        case BLEND_NOOP4 + (BLEND_NOOP>>2):
            //Cycle1:   Fog * AIn + In * 1-A
            //Cycle2:   In * 0 + In * 1
        case BLEND_FOG_ASHADE+(BLEND_PASS>>2):
            //Cycle1:   Fog * AShade + In * 1-A
            //Cycle2:   In * 0 + In * 1
        case BLEND_FOG_3+(BLEND_PASS>>2):
            BlendFunc(XE_BLEND_ONE, XE_BLEND_ZERO);
            Enable();
            break;
        case BLEND_FOG_ASHADE+0x0301:
            // c800 - Cycle1:   Fog * AShade + In * 1-A
            // 0301 - Cycle2:   In * 0 + In * AMem
            BlendFunc(XE_BLEND_SRCALPHA, XE_BLEND_ZERO);
            Enable();
            break;
        case 0x0c08+0x1111:
            // 0c08 - Cycle1:   In * 0 + In * 1
            // 1111 - Cycle2:   Mem * AFog + Mem * AMem
            BlendFunc(XE_BLEND_ZERO, XE_BLEND_DESTALPHA);
            Enable();
            break;
        default:
            if( blendmode_2 == (BLEND_PASS>>2) )
            {
                BlendFunc(XE_BLEND_ONE, XE_BLEND_ZERO);
            }
            else
            {
                BlendFunc(XE_BLEND_SRCALPHA, XE_BLEND_INVSRCALPHA);
            }
            Enable();
            break;
        }
        break;
    default:    // 1/2 Cycle or Copy
        if( gDP.otherMode.forceBlender && gDP.otherMode.depthCompare && blendmode_1 != BLEND_FOG_ASHADE )
        {
            BlendFunc(XE_BLEND_SRCALPHA, XE_BLEND_INVSRCALPHA);
            Enable();
            break;
        }
/*        if( gRDP.otherMode.force_bl && options.enableHackForGames == HACK_FOR_COMMANDCONQUER )
        {
            BlendFunc(XE_BLEND_SRCALPHA, XE_BLEND_INVSRCALPHA);
            Enable();
            break;
        }*/

        switch ( blendmode_1 )
        //switch ( blendmode_2<<2 )
        {
        case BLEND_XLU: // IN * A_IN + MEM * (1-A_IN)
        case BLEND_BI_AIN:  // Bl * AIn + Mem * 1-A
        case BLEND_FOG_MEM: // c440 - Cycle1:   Fog * AFog + Mem * 1-A
        case BLEND_FOG_MEM_IN_MEM:  // c440 - Cycle1:   In * AFog + Mem * 1-A
        case BLEND_BLENDCOLOR:  //Bl * 0 + Bl * 1
        case 0x00c0:    //In * AIn + Fog * 1-A
            BlendFunc(XE_BLEND_SRCALPHA, XE_BLEND_INVSRCALPHA);
            Enable();
            break;
        case BLEND_MEM_ALPHA_IN:    //  Mem * AIn + Mem * AMem
            BlendFunc(XE_BLEND_ZERO, XE_BLEND_DESTALPHA);
            Enable();
            break;
        case BLEND_PASS:    // IN * 0 + IN * 1
            BlendFunc(XE_BLEND_ONE, XE_BLEND_ZERO);
            if( gDP.otherMode.alphaCvgSel )
            {
                Enable();
            }
            else
            {
                Disable();
            }
            break;
        case BLEND_OPA:     // IN * A_IN + MEM * A_MEM
/*            if( options.enableHackForGames == HACK_FOR_MARIO_TENNIS )
            {
                BlendFunc(XE_BLEND_SRCALPHA, XE_BLEND_INVSRCALPHA);
            }
            else*/
            {
                BlendFunc(XE_BLEND_ONE, XE_BLEND_ZERO);
            }
            Enable();
            break;
        case BLEND_NOOP:        // IN * A_IN + IN * (1 - A_IN)
        case BLEND_FOG_ASHADE:  // Fog * AShade + In * 1-A
        case BLEND_FOG_MEM_3:   // Mem * AFog + Fog * 1-A
        case BLEND_BI_AFOG:     // Bl * AFog + In * 1-A
            BlendFunc(XE_BLEND_ONE, XE_BLEND_ZERO);
            Enable();
            break;
        case BLEND_FOG_APRIM:   // Fog * AFog + In * 1-A
            BlendFunc(XE_BLEND_INVSRCALPHA, XE_BLEND_ZERO);
            Enable();
            break;
        case BLEND_NOOP3:       // In * 0 + Mem * 1
        case BLEND_NOOP5:       // Fog * 0 + Mem * 1
            BlendFunc(XE_BLEND_ZERO, XE_BLEND_ONE);
            Enable();
            break;
        case BLEND_MEM:     // Mem * 0 + Mem * 1-A
            // WaveRace
            BlendFunc(XE_BLEND_ZERO, XE_BLEND_ONE);
            Enable();
            break;
        default:
            BlendFunc(XE_BLEND_SRCALPHA, XE_BLEND_INVSRCALPHA);
            Enable();
            SetAlphaTestEnable(TRUE);
            break;
        }
    }
}

