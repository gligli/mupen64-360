/**
 * glN64_GX - glN64.cpp
 * Copyright (C) 2003 Orkin
 * Copyright (C) 2008, 2009, 2010 sepp256 (Port to Wii/Gamecube/PS3)
 *
 * glN64 homepage: http://gln64.emulation64.com
 * Wii64 homepage: http://www.emulatemii.com
 * email address: sepp256@gmail.com
 *
**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../main/winlnxdefs.h"
#include "../main/main.h"

#include "GFXPlugin.h"
#include "xenos_gfx.h"
#include "Debug.h"
#include "Zilmar GFX 1.3.h"
//#include "FrameBuffer.h"
#include "DepthBuffer.h"
#include "N64.h"
#include "RSP.h"
#include "RDP.h"
#include "VI.h"
//#include "Config.h"
#include "Textures.h"
#include "Combiner.h"
#include "3DMath.h"
#include <math.h>

#include <xenos/xe.h>
#include <xenos/edram.h>
#include <ppc/timebase.h>
#include <time/time.h>

#ifdef DEBUGON
extern "C" { void _break(); }
#endif

char		pluginName[] = "glN64 v0.4.1 by Orkin - GX port by sepp256";
char		*screenDirectory;

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

#define MAKE_COLOR3(r,g,b) (0xff000000 | ((b)<<16) | ((g)<<8) | (r))
#define MAKE_COLOR4(r,g,b,a) ((a)<<24 | ((b)<<16) | ((g)<<8) | (r))
#define MAKE_COLOR4F(r,g,b,a) (((u8)(255.0*(a)))<<24 | (((u8)(255.0*(b)))<<16) | (((u8)(255.0*(g)))<<8) | ((u8)(255.0*(r))))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define MIN(a,b) (((a)<(b))?(a):(b))


#define MAX_VERTEX_COUNT 16384

const struct XenosVBFFormat VertexBufferFormat = {
    4, {
        {XE_USAGE_POSITION, 0, XE_TYPE_FLOAT4},
	    {XE_USAGE_TEXCOORD, 0, XE_TYPE_FLOAT2},
	    {XE_USAGE_TEXCOORD, 1, XE_TYPE_FLOAT2},
        {XE_USAGE_COLOR,    0, XE_TYPE_FLOAT4},
    }
};

typedef struct
#ifdef __GNUC__
    __attribute__((__packed__)) VertexStruct
#endif
{
	float x,y,z,w;
	float u0,v0;
	float u1,v1;
    float r,g,b,a;
} TVertex;

TVertex rect[] = {        
    {-1,  1, 1, 1, 0.0f, 0.0f, 0, 0, 0, 0, 0, 0},
    {1,  1,  1, 1, 1.0f, 0.0f, 0, 0, 0, 0, 0, 0},
    {-1, -1, 1, 1, 0.0f, 1.0f, 0, 0, 0, 0, 0, 0},
    {-1, -1, 1, 1, 0.0f, 1.0f, 0, 0, 0, 0, 0, 0},
    {1,  1,  1, 1, 1.0f, 0.0f, 0, 0, 0, 0, 0, 0},
	{1, -1,  1, 1, 1.0f, 1.0f, 0, 0, 0, 0, 0, 0},
};

struct XenosDevice _xe, *xe;
struct XenosShader *sh_ps_combiner, *sh_ps_combiner_1c, *sh_ps_combiner_1a, *sh_ps_combiner_1c1a, *sh_ps_combiner_slow, *sh_ps_fb, *sh_vs;
struct XenosVertexBuffer *vertexBuffer;
struct XenosVertexBuffer *screenRectVB;

extern char inc_vs[];
extern char inc_ps_fb[];
extern char inc_ps_combiner[];
extern char inc_ps_combiner_1c[];
extern char inc_ps_combiner_1a[];
extern char inc_ps_combiner_1c1a[];
extern char inc_ps_combiner_slow[];


TVertex * firstVertex;
TVertex * currentVertex;
int prevVertexCount;
bool drawPrepared=false;
bool hadTriangles=false;
float tmpTmr=0;

int rendered_frames_ratio=1;

void drawVB();

void updateScissor(){
	Xe_SetScissor(xe,1,
		MAX(gSP.viewport.x,gDP.scissor.ulx)*Xe_GetFramebufferSurface(xe)->width/VI.width,
		MAX(gSP.viewport.y,gDP.scissor.uly)*Xe_GetFramebufferSurface(xe)->height/VI.height,
		MIN(gSP.viewport.x+gSP.viewport.width,gDP.scissor.lrx)*Xe_GetFramebufferSurface(xe)->width/VI.width,
		MIN(gSP.viewport.y+gSP.viewport.height,gDP.scissor.lry)*Xe_GetFramebufferSurface(xe)->height/VI.height
	); 
}

void updateViewport()
{
//    printf("updateViewport %f %f %f %f %d %d\n",gSP.viewport.x,gSP.viewport.y,gSP.viewport.width,gSP.viewport.height,VI.width,VI.height);

    float x,y,w,h;

    x=gSP.viewport.x/VI.width;
    w=gSP.viewport.width/VI.width;
    y=gSP.viewport.y/VI.height;
    h=gSP.viewport.height/VI.height;

    float persp[4][4] = {
        {w,0,0,2*x+w-1.0f},
        {0,h,0,-2*y-h+1.0f},
        {0,0,0.5f,0.5f},
	    {0,0,0,1},
    };

    Xe_SetVertexShaderConstantF(xe,0,(float*)persp,4);
	
	updateScissor();
}

void updateCullFace()
{
	if (gSP.geometryMode & G_CULL_BOTH)
	{
		if (gSP.geometryMode & G_CULL_BACK)
            Xe_SetCullMode(xe,XE_CULL_CW);
		else
            Xe_SetCullMode(xe,XE_CULL_CCW);
	}
	else
        Xe_SetCullMode(xe,XE_CULL_NONE);
}

void updateDepthUpdate()
{
    if (gDP.otherMode.depthUpdate)
        Xe_SetZWrite(xe,1);
	else                   
        Xe_SetZWrite(xe,0);
}

void updateStates(){
    drawVB();

	if (gSP.changed & CHANGED_GEOMETRYMODE)
	{
		updateCullFace();

/*
        if ((gSP.geometryMode & G_FOG) && OGL.EXT_fog_coord && OGL.fog)
			glEnable( GL_FOG );
		else
			glDisable( GL_FOG );
*/

		gSP.changed &= ~CHANGED_GEOMETRYMODE;
	}

	if (gSP.geometryMode & G_ZBUFFER)
        Xe_SetZEnable(xe,1);
	else
        Xe_SetZEnable(xe,0);

	if (gDP.changed & CHANGED_RENDERMODE)
	{
		if (gDP.otherMode.depthCompare)
            Xe_SetZFunc(xe,XE_CMP_LESSEQUAL);
		else
            Xe_SetZFunc(xe,XE_CMP_ALWAYS);

		updateDepthUpdate();
    }

    if (gSP.changed & CHANGED_VIEWPORT)
	{
		updateViewport();
	}
    
    if ((gDP.changed & CHANGED_ALPHACOMPARE) || (gDP.changed & CHANGED_RENDERMODE))
	{
		// Enable alpha test for threshold mode
		if ((gDP.otherMode.alphaCompare == G_AC_THRESHOLD) && !(gDP.otherMode.alphaCvgSel))
		{
            Xe_SetAlphaTestEnable(xe,1);

            Xe_SetAlphaFunc(xe,(gDP.blendColor.a > 0.0f) ? XE_CMP_GREATEREQUAL : XE_CMP_GREATER);
            Xe_SetAlphaRef(xe,gDP.blendColor.a);
		}
		// Used in TEX_EDGE and similar render modes
		else if (gDP.otherMode.cvgXAlpha)
		{
            Xe_SetAlphaTestEnable(xe,1);

			// Arbitrary number -- gives nice results though
            Xe_SetAlphaFunc(xe,XE_CMP_GREATER);
            Xe_SetAlphaRef(xe,0.4f);
		}
		else
            Xe_SetAlphaTestEnable(xe,0);

/*
        if (OGL.usePolygonStipple && (gDP.otherMode.alphaCompare == G_AC_DITHER) && !(gDP.otherMode.alphaCvgSel))
			glEnable( GL_POLYGON_STIPPLE );
		else
			glDisable( GL_POLYGON_STIPPLE );
*/
	}

	if (gDP.changed & CHANGED_SCISSOR)
	{
//		printf("clipplane %f %f %f %f\n",gDP.scissor.ulx,gDP.scissor.uly,gDP.scissor.lrx,gDP.scissor.lry);
		updateScissor();
	}

    if ((gDP.changed & CHANGED_COMBINE) || (gDP.changed & CHANGED_CYCLETYPE))
	{
		if (gDP.otherMode.cycleType == G_CYC_COPY)
			Combiner_SetCombine( EncodeCombineMode( 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0 ) );
		else if (gDP.otherMode.cycleType == G_CYC_FILL)
			Combiner_SetCombine( EncodeCombineMode( 0, 0, 0, SHADE, 0, 0, 0, 1, 0, 0, 0, SHADE, 0, 0, 0, 1 ) );
		else
			Combiner_SetCombine( gDP.combine.mux );
	}

	if (gDP.changed & CHANGED_COMBINE_COLORS)
	{
		Combiner_UpdateCombineColors();
	}

	if ((gSP.changed & CHANGED_TEXTURE) || (gDP.changed & CHANGED_TILE) || (gDP.changed & CHANGED_TMEM))
	{
		Combiner_BeginTextureUpdate();

        if (combiner.usesT0)
		{
			TextureCache_Update( 0 );

			gSP.changed &= ~CHANGED_TEXTURE;
			gDP.changed &= ~CHANGED_TILE;
			gDP.changed &= ~CHANGED_TMEM;
		}
		else
		{
			TextureCache_ActivateDummy( 0 );
		}

        if (combiner.usesT1)
		{
			TextureCache_Update( 1 );

			gSP.changed &= ~CHANGED_TEXTURE;
			gDP.changed &= ~CHANGED_TILE;
			gDP.changed &= ~CHANGED_TMEM;
		}
		else
		{
			TextureCache_ActivateDummy( 1 );
		}

		Combiner_EndTextureUpdate();
	}

	if ((gDP.changed & CHANGED_RENDERMODE) || (gDP.changed & CHANGED_CYCLETYPE))
	{
		if ((gDP.otherMode.forceBlender) &&
			(gDP.otherMode.cycleType != G_CYC_COPY) &&
			(gDP.otherMode.cycleType != G_CYC_FILL) &&
			!(gDP.otherMode.alphaCvgSel))
		{
            Xe_SetBlendOp(xe,XE_BLENDOP_ADD);
            Xe_SetBlendOpAlpha(xe,XE_BLENDOP_ADD);


			switch (gDP.otherMode.l >> 16)
			{
				case 0x0448: // Add
				case 0x055A:
                    Xe_SetSrcBlend(xe,XE_BLEND_ONE);
                    Xe_SetDestBlend(xe,XE_BLEND_ONE);
                    Xe_SetSrcBlendAlpha(xe,XE_BLEND_ONE);
                    Xe_SetDestBlendAlpha(xe,XE_BLEND_ONE);
                    break;
				case 0x0C08: // 1080 Sky
				case 0x0F0A: // Used LOTS of places
                    Xe_SetSrcBlend(xe,XE_BLEND_ONE);
                    Xe_SetDestBlend(xe,XE_BLEND_ZERO);
                    Xe_SetSrcBlendAlpha(xe,XE_BLEND_ONE);
                    Xe_SetDestBlendAlpha(xe,XE_BLEND_ZERO);
					break;
				case 0xC810: // Blends fog
				case 0xC811: // Blends fog
				case 0x0C18: // Standard interpolated blend
				case 0x0C19: // Used for antialiasing
				case 0x0050: // Standard interpolated blend
				case 0x0055: // Used for antialiasing
                    Xe_SetSrcBlend(xe,XE_BLEND_SRCALPHA);
                    Xe_SetDestBlend(xe,XE_BLEND_INVSRCALPHA);
                    Xe_SetSrcBlendAlpha(xe,XE_BLEND_SRCALPHA);
                    Xe_SetDestBlendAlpha(xe,XE_BLEND_INVSRCALPHA);
					break;
				case 0x0FA5: // Seems to be doing just blend color - maybe combiner can be used for this?
				case 0x5055: // Used in Paper Mario intro, I'm not sure if this is right...
                    Xe_SetSrcBlend(xe,XE_BLEND_ZERO);
                    Xe_SetDestBlend(xe,XE_BLEND_ONE);
                    Xe_SetSrcBlendAlpha(xe,XE_BLEND_ZERO);
                    Xe_SetDestBlendAlpha(xe,XE_BLEND_ONE);
					break;
				default:
                    Xe_SetSrcBlend(xe,XE_BLEND_SRCALPHA);
                    Xe_SetDestBlend(xe,XE_BLEND_INVSRCALPHA);
                    Xe_SetSrcBlendAlpha(xe,XE_BLEND_SRCALPHA);
                    Xe_SetDestBlendAlpha(xe,XE_BLEND_INVSRCALPHA);
					break;
			}
		}
		else
        {
            // disable
            Xe_SetSrcBlend(xe,XE_BLEND_ONE);
            Xe_SetDestBlend(xe,XE_BLEND_ZERO);
            Xe_SetBlendOp(xe,XE_BLENDOP_ADD);
            Xe_SetSrcBlendAlpha(xe,XE_BLEND_ONE);
            Xe_SetDestBlendAlpha(xe,XE_BLEND_ZERO);
            Xe_SetBlendOpAlpha(xe,XE_BLENDOP_ADD);
		}

		if (gDP.otherMode.cycleType == G_CYC_FILL)
		{
            Xe_SetSrcBlend(xe,XE_BLEND_SRCALPHA);
            Xe_SetDestBlend(xe,XE_BLEND_INVSRCALPHA);
            Xe_SetBlendOp(xe,XE_BLENDOP_ADD);
            Xe_SetSrcBlendAlpha(xe,XE_BLEND_SRCALPHA);
            Xe_SetDestBlendAlpha(xe,XE_BLEND_INVSRCALPHA);
            Xe_SetBlendOpAlpha(xe,XE_BLENDOP_ADD);
		}
	}
    gDP.changed &= CHANGED_TILE | CHANGED_TMEM;
	gSP.changed &= CHANGED_TEXTURE | CHANGED_MATRIX;
}

int vertexCount(){
    return currentVertex-firstVertex;
}

#ifndef USE_VB_POOL
void resetLockVB(){
    firstVertex=currentVertex=(TVertex *)Xe_VB_Lock(xe,vertexBuffer,0,MAX_VERTEX_COUNT*sizeof(TVertex),XE_LOCK_WRITE);
    prevVertexCount=0;
}
#endif

void nextVertex(){
    ++currentVertex;
    if (vertexCount()>=MAX_VERTEX_COUNT-1){
        printf("[xenos_gfx] too many vertices !\n");
        exit(1);
    }
}

void prepareDraw(){
	if (drawPrepared) return;

	Xe_Sync(xe); // wait for background render to finish !
 
//	printf("time %f\n",(float)mftb()/(PPC_TIMEBASE_FREQ/1000)-tmpTmr);

	Xe_InvalidateState(xe);

    //updateViewport();

#ifndef USE_VB_POOL
    Xe_SetStreamSource(xe, 0, vertexBuffer, 0, 4);
    resetLockVB();
#endif

	drawPrepared=true;
}

void drawVB(){
#ifdef USE_VB_POOL
	if (vertexCount()){
//		printf("vc %d\n",vertexCount());

		Xe_VBBegin(xe,sizeof(TVertex)/sizeof(float));
		Xe_VBPut(xe,firstVertex,vertexCount()*sizeof(TVertex)/sizeof(float));

		vertexBuffer=Xe_VBEnd(xe);
		Xe_VBPoolAdd(xe,vertexBuffer);

		while (vertexBuffer){
			Xe_Draw(xe, vertexBuffer, NULL);
			vertexBuffer=vertexBuffer->next;
		}
		
		currentVertex=firstVertex;
		hadTriangles=true;
	}
#else
	if (vertexCount()>prevVertexCount){
        Xe_DrawPrimitive(xe,XE_PRIMTYPE_TRIANGLELIST,prevVertexCount,(vertexCount()-prevVertexCount)/3);
        prevVertexCount=vertexCount();
		hadTriangles=true;
    }
#endif
}

void xeGfx_matrixDump(const char *name, float m[4][4])
{
	int i, j;
	printf("-- %s:\n", name);
	for (i=0; i<4; ++i)
	{
		for (j=0; j<4; ++j)
			printf("% 3.3f ", m[i][j]);
		printf("\n");
	}
}

void xeGfx_clearDepthBuffer(){
    //printf("xeGfx_clearDepthBuffer\n");

	prepareDraw();
    drawVB();

    int i;
    for(i=0;i<6;++i){
        *currentVertex=rect[i];
        nextVertex();
    }


    Xe_SetZFunc(xe,XE_CMP_ALWAYS);
    Xe_SetShader(xe,SHADER_TYPE_PIXEL,sh_ps_fb,0);
    Xe_SetAlphaTestEnable(xe,1);
    Xe_SetAlphaFunc(xe,XE_CMP_NEVER);

	gDP.changed |= CHANGED_RENDERMODE;
    gDP.changed |= CHANGED_COMBINE;
    drawVB();
}

void xeGfx_clearColorBuffer(float *color){
    //printf("xeGfx_clearColorBuffer\n");
    
	prepareDraw();
    drawVB();

    int i;
    for(i=0;i<6;++i){
        *currentVertex=rect[i];

        currentVertex->r=color[0];
        currentVertex->g=color[1];
        currentVertex->b=color[2];
        currentVertex->a=color[3];

        nextVertex();
    }


    Xe_SetZFunc(xe,XE_CMP_ALWAYS);
    Xe_SetShader(xe,SHADER_TYPE_PIXEL,sh_ps_fb,0);

	gDP.changed |= CHANGED_RENDERMODE;
    gDP.changed |= CHANGED_COMBINE;
    drawVB();
}

void doDrawRect(){
    float ortho[4][4] = {
        {2.0f/VI.width,0,0,-1},
        {0,-2.0f/VI.height,0,1},
	    {0,0,0.5,0.5},
	    {0,0,0,1},
    };

    Xe_SetCullMode(xe,XE_CULL_NONE);
    Xe_SetZEnable(xe,0);

    Xe_SetVertexShaderConstantF(xe,0,(float*)ortho,4);

    gSP.changed |= CHANGED_GEOMETRYMODE;
    gDP.changed |= CHANGED_RENDERMODE;
    drawVB();

    updateViewport();
}

void xeGfx_drawRect( int ulx, int uly, int lrx, int lry, float *color ){
    //printf("xeGfx_drawRect %d %d %d %d\n",ulx,uly,lrx,lry);

    prepareDraw();
    updateStates();

    TVertex v[6];

    memset(v,0,sizeof(rect));
    
    v[0].x=v[2].x=v[3].x=ulx;
    v[1].x=v[4].x=v[5].x=lrx;
    
    v[2].y=v[3].y=v[5].y=uly;
    v[0].y=v[1].y=v[4].y=lry;

    int i;
    for(i=0;i<6;++i){
        v[i].r=color[0];
        v[i].g=color[1];
        v[i].b=color[2];
        v[i].a=color[3];

        v[i].z=(gDP.otherMode.depthSource == G_ZS_PRIM) ? gDP.primDepth.z : gSP.viewport.nearz;
        v[i].w=1.0f;

        *currentVertex=v[i];
        nextVertex();
    }
    
    doDrawRect();
}

void xeGfx_drawTexturedRect(int ulx,int uly,int lrx,int lry,float uls,float ult,float lrs,float lrt,bool flip){
    //printf("xeGfx_drawTexturedRect %d %d %d %d %f %f %f %f %d\n",ulx,uly,lrx,lry,uls,ult,lrs,lrt,flip);
	
    float ulu[2]={uls,uls},
          ulv[2]={ult,ult},
          lru[2]={lrs,lrs},
          lrv[2]={lrt,lrt};
    TVertex v[6];
    int i;
     
    prepareDraw();
    updateStates();

    for(i=0;i<2;++i){
        if ((i==0 && !combiner.usesT0) || (i==1 && !combiner.usesT1)) continue;

	    ulu[i] = ulu[i] * cache.current[i]->shiftScaleS - gSP.textureTile[i]->fuls;
	    ulv[i] = ulv[i] * cache.current[i]->shiftScaleT - gSP.textureTile[i]->fult;
	    lru[i] = (lru[i] + 1.0f) * cache.current[i]->shiftScaleS - gSP.textureTile[i]->fuls;
	    lrv[i] = (lrv[i] + 1.0f) * cache.current[i]->shiftScaleT - gSP.textureTile[i]->fult;

	    if ((cache.current[i]->maskS) && (fmod( ulu[i], cache.current[i]->width ) == 0.0f) && !(cache.current[i]->mirrorS))
	    {
		    lru[i] -= ulu[i];
		    ulu[i] = 0.0f;
	    }

	    if ((cache.current[i]->maskT) && (fmod( ulv[i], cache.current[i]->height ) == 0.0f) && !(cache.current[i]->mirrorT))
	    {
		    lrv[i] -= ulv[i];
		    ulv[i] = 0.0f;
	    }

	    if (cache.current[i]->frameBufferTexture)
	    {
		    ulu[i] = cache.current[i]->offsetS + ulu[i];
		    ulv[i] = cache.current[i]->offsetT - ulv[i];
		    lru[i] = cache.current[i]->offsetS + lru[i];
		    lrv[i] = cache.current[i]->offsetT - lrv[i];
	    }

        XenosSurface * surf=(XenosSurface *)cache.current[i]->xeSurface;
        bool clamp=false;

	    if ((ulu[i] == 0.0f) && (lru[i] <= cache.current[i]->width)){
            surf->u_addressing=XE_TEXADDR_CLAMP;
            clamp=true;
        }

	    if ((ulv[i] == 0.0f) && (lrv[i] <= cache.current[i]->height)){
            surf->v_addressing=XE_TEXADDR_CLAMP;
            clamp=true;
        }

        if (clamp) Xe_SetTexture(xe,i,surf);

	    ulu[i] *= cache.current[i]->scaleS;
	    ulv[i] *= cache.current[i]->scaleT;
	    lru[i] *= cache.current[i]->scaleS;
	    lrv[i] *= cache.current[i]->scaleT;
    }

    memset(v,0,sizeof(rect));
    
    v[0].x=v[2].x=v[3].x=ulx;
    v[1].x=v[4].x=v[5].x=lrx;

    v[0].u0=v[2].u0=v[3].u0=ulu[0];
    v[0].u1=v[2].u1=v[3].u1=ulu[1];
    v[1].u0=v[4].u0=v[5].u0=lru[0];
    v[1].u1=v[4].u1=v[5].u1=lru[1];

    v[2].y=v[3].y=v[5].y=uly;
    v[0].y=v[1].y=v[4].y=lry;

    v[2].v0=v[3].v0=v[5].v0=ulv[0];
    v[2].v1=v[3].v1=v[5].v1=ulv[1];
    v[0].v0=v[1].v0=v[4].v0=lrv[0];
    v[0].v1=v[1].v1=v[4].v1=lrv[1];

    for(i=0;i<6;++i){
        v[i].r=1.0f;
        v[i].g=1.0f;
        v[i].b=1.0f;
        v[i].a=1.0f;

        v[i].z=(gDP.otherMode.depthSource == G_ZS_PRIM) ? gDP.primDepth.z : gSP.viewport.nearz;
        v[i].w=1.0f;

        *currentVertex=v[i];
        nextVertex();
    }
    
    doDrawRect();
}


void xeGfx_setCombinerConstantF(int start,float * data,int count){
    Xe_SetPixelShaderConstantF(xe,start,data,count);
}

void xeGfx_setCombinerConstantB(int index, bool value){
    Xe_SetPixelShaderConstantB(xe,index,value);
}

void xeGfx_setCombinerShader(bool oneColorOp,bool oneAlphaOp,bool slow){
//    Xe_SetShader(xe,SHADER_TYPE_PIXEL,sh_ps_fb,0);return;
    
    if (slow)
        Xe_SetShader(xe,SHADER_TYPE_PIXEL,sh_ps_combiner_slow,0);
    else{
        if (oneColorOp && !oneAlphaOp)
            Xe_SetShader(xe,SHADER_TYPE_PIXEL,sh_ps_combiner_1c,0);
        else if (!oneColorOp && oneAlphaOp)
            Xe_SetShader(xe,SHADER_TYPE_PIXEL,sh_ps_combiner_1a,0);
        else if (oneColorOp && oneAlphaOp)
            Xe_SetShader(xe,SHADER_TYPE_PIXEL,sh_ps_combiner_1c1a,0);
        else 
            Xe_SetShader(xe,SHADER_TYPE_PIXEL,sh_ps_combiner,0);
    }
}
void * xeGfx_createTexture(int width,int height){
//    printf("xeGfx_createTexture %d %d\n",width,height);
    XenosSurface * tex = Xe_CreateTexture(xe,width,height,1,XE_FMT_8888|XE_FMT_ARGB,0);
    return tex;
}

void xeGfx_destroyTexture(void * tex){
//    printf("xeGfx_destroyTexture\n");
    Xe_DestroyTexture(xe,(XenosSurface *)tex);
}

void xeGfx_setTextureData(void * tex,void * buffer){
//    printf("xeGfx_setTextureData\n");
    XenosSurface * surf = (XenosSurface *)tex;
    u8 * buf = (u8*)buffer;

    u8 * surfbuf;
    int j,i;
    surfbuf=(u8*)Xe_Surface_LockRect(xe,surf,0,0,0,0,XE_LOCK_WRITE);
    for(j=0;j<surf->hpitch;++j)
        for(i=0;i<surf->wpitch;i+=surf->width*4)
            memcpy(&surfbuf[surf->wpitch*j+i],&buf[surf->width*(j%surf->height)*4],surf->width*4);
        
    Xe_Surface_Unlock(xe,surf);
}

void xeGfx_activateTexture(void * tex,int index, int filter, int clamps, int clampt){
//    printf("xeGfx_activateTexture %d\n",index);
    XenosSurface * surf = (XenosSurface *)tex;

    surf->use_filtering=(filter)?1:0;
    surf->u_addressing=(clamps)?XE_TEXADDR_CLAMP:XE_TEXADDR_WRAP;
    surf->v_addressing=(clampt)?XE_TEXADDR_CLAMP:XE_TEXADDR_WRAP;

    Xe_SetTexture(xe,index,surf);
}

void xeGfx_activateFrameBufferTexture(int index){
    printf("xeGfx_activateFrameBufferTexture %d\n",index);
	Xe_SetTexture(xe,index,Xe_GetFramebufferSurface(xe));
}

void xeGfx_addTriangle( SPVertex *vertices, int v0, int v1, int v2 ){
    SPVertex * spv;
    int v[] = { v0, v1, v2 };
    int i;

    prepareDraw();

    if (gSP.changed || gDP.changed) updateStates();

    for(i=0;i<3;++i){
        spv=&vertices[v[i]];

        currentVertex->x=spv->x;
        currentVertex->y=spv->y;
        currentVertex->z=gDP.otherMode.depthSource == G_ZS_PRIM ? gDP.primDepth.z * spv->w : spv->z;
        if (gDP.otherMode.depthMode == ZMODE_DEC) currentVertex->z*=0.999f; // GL_POLYGON_OFFSET_FILL emulation
        currentVertex->w=spv->w;
        currentVertex->r=spv->r;
        currentVertex->g=spv->g;
        currentVertex->b=spv->b;
        currentVertex->a=spv->a;
        if (combiner.usesT0){
            currentVertex->u0 = (spv->s * cache.current[0]->shiftScaleS * gSP.texture.scales - gSP.textureTile[0]->fuls + cache.current[0]->offsetS) * cache.current[0]->scaleS; 
            currentVertex->v0 = (spv->t * cache.current[0]->shiftScaleT * gSP.texture.scalet - gSP.textureTile[0]->fult + cache.current[0]->offsetT) * cache.current[0]->scaleT;
        }
        else
        {
            currentVertex->u0 = 0;
            currentVertex->v0 = 0;
        }
        
        if (combiner.usesT1){
			currentVertex->u1 = (spv->s * cache.current[1]->shiftScaleS * gSP.texture.scales - gSP.textureTile[1]->fuls + cache.current[1]->offsetS) * cache.current[1]->scaleS; 
			currentVertex->v1 = (spv->t * cache.current[1]->shiftScaleT * gSP.texture.scalet - gSP.textureTile[1]->fult + cache.current[1]->offsetT) * cache.current[1]->scaleT;
        }
        else
        {
            currentVertex->u1 = 0;
            currentVertex->v1 = 0;
        }

        nextVertex();
    }
}

void xeGfx_drawTriangles(){
}

void xeGfx_init(){
    xe = &_xe;
	    /* initialize the GPU */
    Xe_Init(xe);

	Xe_SetRenderTarget(xe, Xe_GetFramebufferSurface(xe));

	/* load pixel shaders */

    sh_ps_combiner = Xe_LoadShaderFromMemory(xe, inc_ps_combiner);
    Xe_InstantiateShader(xe, sh_ps_combiner, 0);
 
    sh_ps_combiner_1c = Xe_LoadShaderFromMemory(xe, inc_ps_combiner_1c);
    Xe_InstantiateShader(xe, sh_ps_combiner_1c, 0);

    sh_ps_combiner_1a = Xe_LoadShaderFromMemory(xe, inc_ps_combiner_1a);
    Xe_InstantiateShader(xe, sh_ps_combiner_1a, 0);

    sh_ps_combiner_1c1a = Xe_LoadShaderFromMemory(xe, inc_ps_combiner_1c1a);
    Xe_InstantiateShader(xe, sh_ps_combiner_1c1a, 0);

    sh_ps_combiner_slow = Xe_LoadShaderFromMemory(xe, inc_ps_combiner_slow);
    Xe_InstantiateShader(xe, sh_ps_combiner_slow, 0);

    sh_ps_fb = Xe_LoadShaderFromMemory(xe, inc_ps_fb);
    Xe_InstantiateShader(xe, sh_ps_fb, 0);

    /* load vertex shader */
    sh_vs = Xe_LoadShaderFromMemory(xe, inc_vs);
    Xe_InstantiateShader(xe, sh_vs, 0);
    Xe_ShaderApplyVFetchPatches(xe, sh_vs, 0, &VertexBufferFormat);

#ifdef USE_VB_POOL
	currentVertex=firstVertex=(TVertex*)malloc(MAX_VERTEX_COUNT*sizeof(TVertex));
#else
	vertexBuffer=Xe_CreateVertexBuffer(xe,MAX_VERTEX_COUNT*sizeof(TVertex));
#endif
   
    screenRectVB = Xe_CreateVertexBuffer(xe, sizeof(rect));
    void *v = Xe_VB_Lock(xe, screenRectVB, 0, sizeof(rect), XE_LOCK_WRITE);
    memcpy(v, rect, sizeof(rect));
    Xe_VB_Unlock(xe, screenRectVB);
	
    Xe_SetShader(xe, SHADER_TYPE_VERTEX, sh_vs, 0);
    Xe_SetShader(xe, SHADER_TYPE_PIXEL, sh_ps_combiner, 0);

    edram_init(xe);

    prepareDraw();
}


void xeGfx_render()
{
    static unsigned long lastTick=0;
    static int frames=0,rendered_frames=0,frame_id=0;
    unsigned long nowTick;
	
	frame_id++;
	frames++;
    nowTick = mftb()/(PPC_TIMEBASE_FREQ/1000);
    if (lastTick + 1000 <= nowTick) {
		if (rendered_frames)
			rendered_frames_ratio=(float)frames/(float)rendered_frames+0.5f;
		else
			rendered_frames_ratio=0;
		
	    printf("%d fps, rfr=%d\n",frames,rendered_frames_ratio);

		frames = 0;
		rendered_frames = 0;
	    lastTick = nowTick;
    }

	if (!drawPrepared || !hadTriangles) return;
	
	rendered_frames++;
	
	drawVB();

#ifndef USE_VB_POOL
    Xe_VB_Unlock(xe,vertexBuffer);
#endif	

    Xe_Resolve(xe);
    Xe_Execute(xe); // render everything in background !
	drawPrepared=false;
	hadTriangles=false;

#ifdef USE_FRAMELIMIT
	static int last_rendered_frame=0;
	static u64 last_rendered_tb=0;
	u64 tb=0;

	do{
		tb=mftb();
	}while((tb-last_rendered_tb)<(/*PPC_TIMEBASE_FREQ*/3192000000LL/64LL)*(frame_id-last_rendered_frame)/60);

	last_rendered_tb=tb;	
	last_rendered_frame=frame_id;
#endif
}


///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void (*CheckInterrupts)( void );

EXPORT void CALL CaptureScreen ( char * Directory )
{
	screenDirectory = Directory;
#ifdef RSPTHREAD
	if (RSP.thread)
	{
		SetEvent( RSP.threadMsg[RSPMSG_CAPTURESCREEN] );
		WaitForSingleObject( RSP.threadFinished, INFINITE );
	}
#else
//	OGL_SaveScreenshot();
#endif
}

EXPORT void CALL ChangeWindow (void)
{
}

EXPORT void CALL CloseDLL (void)
{
}

EXPORT void CALL DllAbout ( HWND hParent )
{
}

EXPORT void CALL DllConfig ( HWND hParent )
{
}

EXPORT void CALL DllTest ( HWND hParent )
{
}

EXPORT void CALL DrawScreen (void)
{
}

EXPORT void CALL GetDllInfo ( PLUGIN_INFO * PluginInfo )
{
	PluginInfo->Version = 0x103;
	PluginInfo->Type = PLUGIN_TYPE_GFX;
	strcpy( PluginInfo->Name, pluginName );
	PluginInfo->NormalMemory = FALSE;
	PluginInfo->MemoryBswaped = TRUE;
}

EXPORT BOOL CALL InitiateGFX (GFX_INFO Gfx_Info)
{
//	Config_LoadConfig();
# ifndef __GX__
//	OGL.hScreen = NULL;
# endif // !__GX__
# ifdef RSPTHREAD
	RSP.thread = NULL;
# endif
	DMEM = Gfx_Info.DMEM;
	IMEM = Gfx_Info.IMEM;
	RDRAM = Gfx_Info.RDRAM;

	REG.MI_INTR = Gfx_Info.MI_INTR_REG;
	REG.DPC_START = Gfx_Info.DPC_START_REG;
	REG.DPC_END = Gfx_Info.DPC_END_REG;
	REG.DPC_CURRENT = Gfx_Info.DPC_CURRENT_REG;
	REG.DPC_STATUS = Gfx_Info.DPC_STATUS_REG;
	REG.DPC_CLOCK = Gfx_Info.DPC_CLOCK_REG;
	REG.DPC_BUFBUSY = Gfx_Info.DPC_BUFBUSY_REG;
	REG.DPC_PIPEBUSY = Gfx_Info.DPC_PIPEBUSY_REG;
	REG.DPC_TMEM = Gfx_Info.DPC_TMEM_REG;

	REG.VI_STATUS = Gfx_Info.VI_STATUS_REG;
	REG.VI_ORIGIN = Gfx_Info.VI_ORIGIN_REG;
	REG.VI_WIDTH = Gfx_Info.VI_WIDTH_REG;
	REG.VI_INTR = Gfx_Info.VI_INTR_REG;
	REG.VI_V_CURRENT_LINE = Gfx_Info.VI_V_CURRENT_LINE_REG;
	REG.VI_TIMING = Gfx_Info.VI_TIMING_REG;
	REG.VI_V_SYNC = Gfx_Info.VI_V_SYNC_REG;
	REG.VI_H_SYNC = Gfx_Info.VI_H_SYNC_REG;
	REG.VI_LEAP = Gfx_Info.VI_LEAP_REG;
	REG.VI_H_START = Gfx_Info.VI_H_START_REG;
	REG.VI_V_START = Gfx_Info.VI_V_START_REG;
	REG.VI_V_BURST = Gfx_Info.VI_V_BURST_REG;
	REG.VI_X_SCALE = Gfx_Info.VI_X_SCALE_REG;
	REG.VI_Y_SCALE = Gfx_Info.VI_Y_SCALE_REG;

	CheckInterrupts = Gfx_Info.CheckInterrupts;

	xeGfx_init();

    return TRUE;
}

EXPORT void CALL MoveScreen (int xpos, int ypos)
{
}

EXPORT void CALL ProcessDList(void)
{
#ifdef DEBUGON
//	_break();
#endif

#ifdef RSPTHREAD
	if (RSP.thread)
	{
		SetEvent( RSP.threadMsg[RSPMSG_PROCESSDLIST] );
		WaitForSingleObject( RSP.threadFinished, INFINITE );
	}
#else
#ifdef __GX__
#ifdef GLN64_SDLOG
	sprintf(txtbuffer,"\nPROCESS D LIST!!\n\n");
	DEBUG_print(txtbuffer,DBG_SDGECKOPRINT);
#endif // GLN64_SDLOG
	if (VI.enableLoadIcon && !OGL.frameBufferTextures)
	{
		float color[4] = {0.0f,0.0f,0.0f,0.0f};
		OGL_ClearColorBuffer( color );
		OGL_ClearDepthBuffer();
		OGL_GXclearEFB();
//		VI_GX_clearEFB();
	}
	VI.enableLoadIcon = false;
#endif // __GX__

#if 0
	static unsigned long lastTick=0;
    static int frames=0;
    unsigned long nowTick;

    frames++;
    nowTick = mftb()/(PPC_TIMEBASE_FREQ/1000);
    if (lastTick + 1000 <= nowTick) {
	printf("%d dl/s\n",frames);
	frames = 0;
	lastTick = nowTick;
    }
#endif
    
    RSP_ProcessDList();
#endif

#ifdef DEBUGON
//	_break();
#endif

#ifdef __GX__
#ifdef SHOW_DEBUG
	sprintf(txtbuffer,"RSP: VtxMP = %d; pDcnt = %d; Zprim = %d; noZprim = %d", OGL.GXnumVtxMP, cache.GXprimDepthCnt, cache.GXZTexPrimCnt, cache.GXnoZTexPrimCnt);
	DEBUG_print(txtbuffer,DBG_RSPINFO);
#endif
	VI_GX_updateDEBUG();
#endif // __GX__
}

EXPORT void CALL ProcessRDPList(void)
{
	//*REG.DPC_CURRENT = *REG.DPC_START;
/*	RSP.PCi = 0;
	RSP.PC[RSP.PCi] = *REG.DPC_CURRENT;
	
	RSP.halt = FALSE;

	while (RSP.PC[RSP.PCi] < *REG.DPC_END)
	{
		RSP.cmd0 = *(DWORD*)&RDRAM[RSP.PC[RSP.PCi]];
		RSP.cmd1 = *(DWORD*)&RDRAM[RSP.PC[RSP.PCi] + 4];
		RSP.PC[RSP.PCi] += 8;
*/
/*		if ((RSP.cmd0 >> 24) == 0xE9)
		{
			*REG.MI_INTR |= MI_INTR_DP;
			CheckInterrupts();
		}
		if ((RSP.cmd0 >> 24) == 0xCD)
			RSP.cmd0 = RSP.cmd0;

		GFXOp[RSP.cmd0 >> 24]();*/
		//*REG.DPC_CURRENT += 8;
//	}
}

EXPORT void CALL RomClosed (void)
{
#ifdef RSPTHREAD
	int i;

	if (RSP.thread)
	{
//		if (OGL.fullscreen)
//			ChangeWindow();

		if (RSP.busy)
		{
			RSP.halt = TRUE;
			WaitForSingleObject( RSP.threadFinished, INFINITE );
		}

		SetEvent( RSP.threadMsg[RSPMSG_CLOSE] );
		WaitForSingleObject( RSP.threadFinished, INFINITE );
		for (i = 0; i < 4; i++)
			if (RSP.threadMsg[i])
				CloseHandle( RSP.threadMsg[i] );
		CloseHandle( RSP.threadFinished );
		CloseHandle( RSP.thread );
	}

	RSP.thread = NULL;
#else
    Combiner_Destroy();
    TextureCache_Destroy();
	DepthBuffer_Destroy();
#endif

#ifdef __GX__
	VIDEO_SetPreRetraceCallback(NULL);
#endif // __GX__

#ifdef DEBUG
	CloseDebugDlg();
#endif
}

EXPORT void CALL RomOpen (void)
{
#ifdef RSPTHREAD
#else
	RSP_Init();
    Combiner_Init();
    TextureCache_Init();
	DepthBuffer_Init();
#endif

//	OGL_ResizeWindow();
	
	rendered_frames_ratio=1;

#ifdef DEBUG
	OpenDebugDlg();
#endif
}

EXPORT void CALL ShowCFB (void)
{	
}

EXPORT void CALL UpdateScreen (void)
{
#ifdef RSPTHREAD
	if (RSP.thread)
	{
		SetEvent( RSP.threadMsg[RSPMSG_UPDATESCREEN] );
		WaitForSingleObject( RSP.threadFinished, INFINITE );
	}
#else
	VI_UpdateScreen();
#endif
}

EXPORT void CALL ViStatusChanged (void)
{
}

EXPORT void CALL ViWidthChanged (void)
{
}


EXPORT void CALL ReadScreen (void **dest, long *width, long *height)
{
//	OGL_ReadScreen( dest, width, height );
}

