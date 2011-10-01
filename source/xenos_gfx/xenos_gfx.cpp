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

extern "C"{
	#include "../main/winlnxdefs.h"
	#include "../main/main.h"
	#include "../main/rom.h"
}

#include "GFXPlugin.h"
#include "xenos_gfx.h"
#include "xenos_blender.h"
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
#include <ppc/timebase.h>
#include <time/time.h>

#include <zlx/zlx.h>
#include <malloc.h>

#ifdef DEBUGON
extern "C" { void _break(); }
#endif

char		pluginName[] = "glN64 v0.4.1 by Orkin - GX port by sepp256";
char		*screenDirectory;

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

#define MAX_VERTEX_COUNT 16384
#define MAX_INDICE_COUNT 32768

const struct XenosVBFFormat VertexBufferFormat = {
    4, {
        {XE_USAGE_POSITION, 0, XE_TYPE_FLOAT4},
	    {XE_USAGE_TEXCOORD, 0, XE_TYPE_FLOAT2},
	    {XE_USAGE_TEXCOORD, 1, XE_TYPE_FLOAT2},
        {XE_USAGE_COLOR,    0, XE_TYPE_UBYTE4},
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
    unsigned int color;
} TVertex;

typedef u16 TIndice;

TVertex rect[] = {        
    {-1,  1, 1, 1, 0.0f, 0.0f, 0, 0, 0},
    {1,  1,  1, 1, 1.0f, 0.0f, 0, 0, 0},
    {-1, -1, 1, 1, 0.0f, 1.0f, 0, 0, 0},
//    {-1, -1, 1, 1, 0.0f, 1.0f, 0, 0, 0},
//    {1,  1,  1, 1, 1.0f, 0.0f, 0, 0, 0},
	{1, -1,  1, 1, 1.0f, 1.0f, 0, 0, 0},
};

TIndice recti[] = {0,1,2,2,1,3};

struct XenosDevice *xe;
struct XenosShader *sh_ps_combiner[5][5], *sh_ps_combiner_slow, *sh_ps_fb, *sh_vs;
struct XenosVertexBuffer *vertexBuffer;

struct XenosIndexBuffer *indexBuffer;

extern char inc_vs[];
extern char inc_ps_fb[];
extern char inc_ps_combiner_1c1a[];
extern char inc_ps_combiner_1c2a[];
extern char inc_ps_combiner_1c3a[];
extern char inc_ps_combiner_1c4a[];
extern char inc_ps_combiner_2c1a[];
extern char inc_ps_combiner_2c2a[];
extern char inc_ps_combiner_2c3a[];
extern char inc_ps_combiner_2c4a[];
extern char inc_ps_combiner_3c1a[];
extern char inc_ps_combiner_3c2a[];
extern char inc_ps_combiner_3c3a[];
extern char inc_ps_combiner_3c4a[];
extern char inc_ps_combiner_4c1a[];
extern char inc_ps_combiner_4c2a[];
extern char inc_ps_combiner_4c3a[];
extern char inc_ps_combiner_4c4a[];
extern char inc_ps_combiner_slow[];


TVertex * firstVertex;
TVertex * currentVertex;

TIndice * firstIndice;
TIndice * currentIndice;
int prevIndiceCount;
TIndice pendingIndices[MAX_VERTEX_COUNT];
int pendingIndicesCount=0;


bool drawPrepared=false;
bool hadTriangles=false;

int rendered_frames_ratio=1;

void drawVB();

void updateScissor(){
	Xe_SetScissor(xe,1,
		MAX(gSP.viewport.x,gDP.scissor.ulx)*Xe_GetFramebufferSurface(xe)->width/VI.width,
		MAX(gSP.viewport.y,gDP.scissor.uly)*Xe_GetFramebufferSurface(xe)->height/VI.height,
		MIN(gSP.viewport.x+gSP.viewport.width-1,gDP.scissor.lrx)*Xe_GetFramebufferSurface(xe)->width/VI.width,
		MIN(gSP.viewport.y+gSP.viewport.height-1,gDP.scissor.lry)*Xe_GetFramebufferSurface(xe)->height/VI.height
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
		applyAlphaMode();
	}

	if (gDP.changed & CHANGED_SCISSOR)
	{
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
		applyBlenderMode();
	}

	gDP.changed &= CHANGED_TILE | CHANGED_TMEM;
	gSP.changed &= CHANGED_TEXTURE | CHANGED_MATRIX;
}

void processVertex(SPVertex * spv,TVertex * v){
        v->x=spv->x;
        v->y=spv->y;
        v->z=gDP.otherMode.depthSource == G_ZS_PRIM ? gDP.primDepth.z * spv->w : spv->z;
        if (gDP.otherMode.depthMode == ZMODE_DEC) v->z*=0.999f; // GL_POLYGON_OFFSET_FILL emulation
        v->w=spv->w;
		v->color=__builtin_bswap32(spv->color);
        if (combiner.usesT0){
            v->u0 = (spv->s * cache.current[0]->shiftScaleS * gSP.texture.scales - gSP.textureTile[0]->fuls + cache.current[0]->offsetS) * cache.current[0]->scaleS; 
            v->v0 = (spv->t * cache.current[0]->shiftScaleT * gSP.texture.scalet - gSP.textureTile[0]->fult + cache.current[0]->offsetT) * cache.current[0]->scaleT;
        }
        else
        {
            v->u0 = 0;
            v->v0 = 0;
        }
        
        if (combiner.usesT1){
			v->u1 = (spv->s * cache.current[1]->shiftScaleS * gSP.texture.scales - gSP.textureTile[1]->fuls + cache.current[1]->offsetS) * cache.current[1]->scaleS; 
			v->v1 = (spv->t * cache.current[1]->shiftScaleT * gSP.texture.scalet - gSP.textureTile[1]->fult + cache.current[1]->offsetT) * cache.current[1]->scaleT;
        }
        else
        {
            v->u1 = 0;
            v->v1 = 0;
        }
}

int vertexCount(){
    return currentVertex-firstVertex;
}

void resetLockVB(){
	Xe_SetStreamSource(xe, 0, vertexBuffer, 0, 4);
	firstVertex=currentVertex=(TVertex *)Xe_VB_Lock(xe,vertexBuffer,0,MAX_VERTEX_COUNT*sizeof(TVertex),XE_LOCK_WRITE);
}

void nextVertex(){
	++currentVertex;
	
    if (vertexCount()>=MAX_VERTEX_COUNT){
		printf("[xenos_gfx] too many vertices !\n");
		exit(1);
    }
}

int indiceCount(){
    return currentIndice-firstIndice;
}

void resetLockIB(){
	Xe_SetIndices(xe,indexBuffer);
	firstIndice=currentIndice=(TIndice *)Xe_IB_Lock(xe,indexBuffer,0,MAX_INDICE_COUNT*sizeof(TIndice),XE_LOCK_WRITE);
	prevIndiceCount=0;
}

void nextIndice(){
	++currentIndice;
	
    if (indiceCount()>=MAX_INDICE_COUNT){
		//printf("[xenos_gfx] too many indices !\n");
		drawVB();
    }
}

void updateVSMatrixMode(bool transform,bool ortho)
{
	Xe_SetVertexShaderConstantB(xe,0,transform);
	Xe_SetVertexShaderConstantB(xe,1,ortho);
}

void prepareDraw(bool sync){
	if (drawPrepared) return;

	if (sync) Xe_Sync(xe); // wait for background render to finish !
 
	Xe_InvalidateState(xe);

    resetLockVB();
	resetLockIB();
	
	xe_updateVSOrtho();
	updateVSMatrixMode(true,false);
	
	drawPrepared=true;
}

int dpf=0,prev_dpf=0;

void drawVB(){
	if (indiceCount()>prevIndiceCount){
		++dpf;
		Xe_DrawIndexedPrimitive(xe,XE_PRIMTYPE_TRIANGLELIST,0,0,vertexCount(),prevIndiceCount,(indiceCount()-prevIndiceCount)/3);
        prevIndiceCount=indiceCount();
		hadTriangles=true;
    }
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

void xe_updateVSOrtho()
{
	float ortho[4][4] = {
        {2.0f/VI.width,0,0,-1},
        {0,-2.0f/VI.height,0,1},
	    {0,0,0.5,0.5},
	    {0,0,0,1},
    };

    Xe_SetVertexShaderConstantF(xe,4,(float*)ortho,4);
}

void xeGfx_clearDepthBuffer(){
//	printf("xeGfx_clearDepthBuffer\n");return;

#if 1
	prepareDraw(true);
    drawVB();

    int i;
    float depth = gDP.fillColor.z/(float)0x3fff;

	for(i=0;i<6;++i){
		*currentIndice=vertexCount()+recti[i];
		nextIndice();
    }
	
	for(i=0;i<4;++i){
        *currentVertex=rect[i];
		currentVertex->z=depth;
		nextVertex();
    }

	Xe_SetScissor(xe,0,0,0,0,0);
    Xe_SetCullMode(xe,XE_CULL_NONE);
    Xe_SetZEnable(xe,1);
    Xe_SetZWrite(xe,1);
    Xe_SetZFunc(xe,XE_CMP_ALWAYS);
    Xe_SetShader(xe,SHADER_TYPE_PIXEL,sh_ps_fb,0);
	Xe_SetBlendControl(xe,XE_BLEND_ZERO,XE_BLENDOP_ADD,XE_BLEND_ONE,XE_BLEND_ZERO,XE_BLENDOP_ADD,XE_BLEND_ONE);
    updateVSMatrixMode(false,false);

	gDP.changed |= CHANGED_RENDERMODE;
    gDP.changed |= CHANGED_COMBINE;
    gDP.changed |= CHANGED_SCISSOR;
    gSP.changed |= CHANGED_GEOMETRYMODE;
    updateStates();
#else
	Xe_ResolveInto(xe,Xe_GetFramebufferSurface(xe),0,XE_CLEAR_DS);
#endif
}

void xeGfx_clearColorBuffer(float *color){
//    printf("xeGfx_clearColorBuffer\n");
    
#if 1
	prepareDraw(true);
    drawVB();

    int i;
	for(i=0;i<6;++i){
		*currentIndice=vertexCount()+recti[i];
		nextIndice();
    }
	
	for(i=0;i<4;++i){
        *currentVertex=rect[i];

		currentVertex->color=MAKE_COLOR4F(color[0],color[1],color[2],color[3]);

        nextVertex();
    }

	Xe_SetScissor(xe,0,0,0,0,0);
    Xe_SetCullMode(xe,XE_CULL_NONE);
    Xe_SetZEnable(xe,0);
    Xe_SetShader(xe,SHADER_TYPE_PIXEL,sh_ps_fb,0);
    updateVSMatrixMode(false,false);

	gDP.changed |= CHANGED_RENDERMODE;
    gDP.changed |= CHANGED_COMBINE;
    gDP.changed |= CHANGED_SCISSOR;
    gSP.changed |= CHANGED_GEOMETRYMODE;
    updateStates();
#else
	Xe_SetClearColor(xe,MAKE_COLOR4F(color[0],color[1],color[2],color[3]));
	Xe_ResolveInto(xe,Xe_GetFramebufferSurface(xe),0,XE_CLEAR_COLOR);
#endif
}

void doDrawRect(){
	updateVSMatrixMode(true,true);
	Xe_SetZEnable(xe,0);
}

void xeGfx_drawRect( int ulx, int uly, int lrx, int lry, float *color ){
	//printf("xeGfx_drawRect %d %d %d %d\n",ulx,uly,lrx,lry);return;

	prepareDraw(true);
	if (gSP.changed || gDP.changed) updateStates();

    TVertex v[4];

    memset(v,0,sizeof(rect));
    
    v[0].x=v[2].x=ulx;
    v[1].x=v[3].x=lrx;
    
    v[2].y=v[3].y=uly;
    v[0].y=v[1].y=lry;

    int i;

	for(i=0;i<6;++i){
		*currentIndice=vertexCount()+recti[i];
		nextIndice();
    }
	
    for(i=0;i<4;++i){
		v[i].color=MAKE_COLOR4F(color[0],color[1],color[2],color[3]);

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
    TVertex v[4];
    int i;
     
	prepareDraw(true);
	if (gSP.changed || gDP.changed) updateStates();

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
    
    v[0].x=v[2].x=ulx;
    v[1].x=v[3].x=lrx;

    v[0].u0=v[2].u0=ulu[0];
    v[0].u1=v[2].u1=ulu[1];
    v[1].u0=v[3].u0=lru[0];
    v[1].u1=v[3].u1=lru[1];

    v[2].y=v[3].y=uly;
    v[0].y=v[1].y=lry;

    v[2].v0=v[3].v0=ulv[0];
    v[2].v1=v[3].v1=ulv[1];
    v[0].v0=v[1].v0=lrv[0];
    v[0].v1=v[1].v1=lrv[1];

	for(i=0;i<6;++i){
		*currentIndice=vertexCount()+recti[i];
		nextIndice();
    }
	
    for(i=0;i<4;++i){
		v[i].color=-1;

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

void xeGfx_setCombinerShader(int colorOps,int alphaOps,bool slow){
//    Xe_SetShader(xe,SHADER_TYPE_PIXEL,sh_ps_fb,0);return;
    
    if (slow)
        Xe_SetShader(xe,SHADER_TYPE_PIXEL,sh_ps_combiner_slow,0);
	else{
		Xe_SetShader(xe,SHADER_TYPE_PIXEL,sh_ps_combiner[colorOps][alphaOps],0);
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

void xeGfx_addTriangle( SPVertex *vertices, int v0, int v1, int v2, int direct){
	if(direct){
		SPVertex * spv;
		int v[] = { v0, v1, v2 };
		int i;

		prepareDraw(true);
		if (gSP.changed || gDP.changed) updateStates();
	    updateVSMatrixMode(true,false);

		for(i=0;i<3;++i){
			spv=&vertices[v[i]];

			processVertex(spv,currentVertex);		

			*currentIndice=vertexCount();
			nextIndice();

			nextVertex();
		}
	}else{
		pendingIndices[pendingIndicesCount++]=v0;
		pendingIndices[pendingIndicesCount++]=v1;
		pendingIndices[pendingIndicesCount++]=v2;
	}
}

void xeGfx_drawTriangles(){

	if (pendingIndicesCount>0){
		TIndice	ind[80];
		int i;
		int numvert=0;
		
		prepareDraw(true);
		if (gSP.changed || gDP.changed) updateStates();
	    updateVSMatrixMode(true,false);
		
		memset(ind,0xff,sizeof(ind));
		
		// add vertices to the vb and get indice for each one
		for(i=0;i<pendingIndicesCount;++i){
			TIndice pi=pendingIndices[i];
			
			if (ind[pi]==0xffff){
				processVertex(&gSPAligned.vertices[pi],currentVertex);

				ind[pi]=vertexCount();
				
				nextVertex();
				
				++numvert;
			}
		}
		
		// finally add indices to ib
		for(i=0;i<pendingIndicesCount;++i){
			*currentIndice=ind[pendingIndices[i]];
			nextIndice();
		}
				
//		printf("pic %5d %5d\n",pendingIndicesCount,numvert);

		pendingIndicesCount=0;
	}
	
}

void xeGfx_init(){
	static int done=0;
	
	xe = ZLX::g_pVideoDevice;

	/* initialize the GPU */

	Xe_SetRenderTarget(xe, Xe_GetFramebufferSurface(xe));
	Xe_InvalidateState(xe);
	Xe_SetClearColor(ZLX::g_pVideoDevice,0);
	
	if(!done){
		/* load pixel shaders */
		
		XenosShader * s;

		sh_ps_combiner[1][1] = s = Xe_LoadShaderFromMemory(xe, inc_ps_combiner_1c1a);
		Xe_InstantiateShader(xe, s, 0);
		sh_ps_combiner[1][2] = s = Xe_LoadShaderFromMemory(xe, inc_ps_combiner_1c2a);
		Xe_InstantiateShader(xe, s, 0);
		sh_ps_combiner[1][3] = s = Xe_LoadShaderFromMemory(xe, inc_ps_combiner_1c3a);
		Xe_InstantiateShader(xe, s, 0);
		sh_ps_combiner[1][4] = s = Xe_LoadShaderFromMemory(xe, inc_ps_combiner_1c4a);
		Xe_InstantiateShader(xe, s, 0);
		sh_ps_combiner[2][1] = s = Xe_LoadShaderFromMemory(xe, inc_ps_combiner_2c1a);
		Xe_InstantiateShader(xe, s, 0);
		sh_ps_combiner[2][2] = s = Xe_LoadShaderFromMemory(xe, inc_ps_combiner_2c2a);
		Xe_InstantiateShader(xe, s, 0);
		sh_ps_combiner[2][3] = s = Xe_LoadShaderFromMemory(xe, inc_ps_combiner_2c3a);
		Xe_InstantiateShader(xe, s, 0);
		sh_ps_combiner[2][4] = s = Xe_LoadShaderFromMemory(xe, inc_ps_combiner_2c4a);
		Xe_InstantiateShader(xe, s, 0);
		sh_ps_combiner[3][1] = s = Xe_LoadShaderFromMemory(xe, inc_ps_combiner_3c1a);
		Xe_InstantiateShader(xe, s, 0);
		sh_ps_combiner[3][2] = s = Xe_LoadShaderFromMemory(xe, inc_ps_combiner_3c2a);
		Xe_InstantiateShader(xe, s, 0);
		sh_ps_combiner[3][3] = s = Xe_LoadShaderFromMemory(xe, inc_ps_combiner_3c3a);
		Xe_InstantiateShader(xe, s, 0);
		sh_ps_combiner[3][4] = s = Xe_LoadShaderFromMemory(xe, inc_ps_combiner_3c4a);
		Xe_InstantiateShader(xe, s, 0);
		sh_ps_combiner[4][1] = s = Xe_LoadShaderFromMemory(xe, inc_ps_combiner_4c1a);
		Xe_InstantiateShader(xe, s, 0);
		sh_ps_combiner[4][2] = s = Xe_LoadShaderFromMemory(xe, inc_ps_combiner_4c2a);
		Xe_InstantiateShader(xe, s, 0);
		sh_ps_combiner[4][3] = s = Xe_LoadShaderFromMemory(xe, inc_ps_combiner_4c3a);
		Xe_InstantiateShader(xe, s, 0);
		sh_ps_combiner[4][4] = s = Xe_LoadShaderFromMemory(xe, inc_ps_combiner_4c4a);
		Xe_InstantiateShader(xe, s, 0);

		sh_ps_combiner_slow = Xe_LoadShaderFromMemory(xe, inc_ps_combiner_slow);
		Xe_InstantiateShader(xe, sh_ps_combiner_slow, 0);

		sh_ps_fb = Xe_LoadShaderFromMemory(xe, inc_ps_fb);
		Xe_InstantiateShader(xe, sh_ps_fb, 0);

		/* load vertex shader */

		sh_vs = Xe_LoadShaderFromMemory(xe, inc_vs);
		Xe_InstantiateShader(xe, sh_vs, 0);
		Xe_ShaderApplyVFetchPatches(xe, sh_vs, 0, &VertexBufferFormat);

		vertexBuffer = Xe_CreateVertexBuffer(xe,MAX_VERTEX_COUNT*sizeof(TVertex));
		indexBuffer = Xe_CreateIndexBuffer(xe, MAX_INDICE_COUNT*sizeof(TIndice),XE_FMT_INDEX16);
		
		done=1;
	}
}

void xeGfx_start(){
	pendingIndicesCount=0;
	drawPrepared=false;
	hadTriangles=false;
	rendered_frames_ratio=1;
	dpf=0;
	prev_dpf=0;
	
    prepareDraw(true);

	Xe_SetShader(xe, SHADER_TYPE_VERTEX, sh_vs, 0);
    Xe_SetShader(xe, SHADER_TYPE_PIXEL, sh_ps_combiner_slow, 0);

	gSP.changed=gDP.changed=-1;
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
		
	    printf("%d fps, rfr=%d, %d dpf\n",frames,rendered_frames_ratio,prev_dpf);
		
		frames = 0;
		rendered_frames = 0;
	    lastTick = nowTick;
		
/*
   		extern unsigned int op_usage_fp[64];
		int i;
		for(i=0;i<64;++i) printf("%02d %8d\n",i,op_usage_fp[i]);
*/
//		extern unsigned int dyna_mem_usage[16];
//		for(i=0;i<16;++i) printf("%02d %8d\n",i,dyna_mem_usage[i]);
    }

	if (!drawPrepared || !hadTriangles) return;
	
	rendered_frames++;
	
	drawVB();

    Xe_VB_Unlock(xe,vertexBuffer);
	Xe_IB_Unlock(xe,indexBuffer);

//    Xe_Resolve(xe);
    Xe_ResolveInto(xe,Xe_GetFramebufferSurface(xe),XE_SOURCE_COLOR,0);
    Xe_Execute(xe); // render everything in background !
	drawPrepared=false;
	hadTriangles=false;
	prev_dpf=dpf;
	dpf=0;

	if(use_framelimit){
		static int last_rendered_frame=0;
		static u64 last_rendered_tb=0;
		u64 tb=0;

		do{
			tb=mftb();
		}while((tb-last_rendered_tb)<PPC_TIMEBASE_FREQ*(frame_id-last_rendered_frame)/((getVideoSystem()==SYSTEM_PAL)?50:60));

		last_rendered_tb=tb;	
		last_rendered_frame=frame_id;
	}
}


///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void (*CheckInterrupts)( void );

EXPORT void CALL CaptureScreen ( char * Directory )
{
	screenDirectory = Directory;
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
}

EXPORT void CALL ProcessRDPList(void)
{
}

EXPORT void CALL RomClosed (void)
{
	Xe_SetScissor(xe,0,0,0,0,0);
    if (vertexBuffer->lock.start) Xe_VB_Unlock(xe,vertexBuffer);
	if (indexBuffer->lock.start) Xe_IB_Unlock(xe,indexBuffer);

	Combiner_Destroy();
    TextureCache_Destroy();
    DepthBuffer_Destroy();
}

EXPORT void CALL RomOpen (void)
{
	RSP_Init();
	Combiner_Init();
	TextureCache_Init();
	DepthBuffer_Init();

	xeGfx_start();
}

EXPORT void CALL ShowCFB (void)
{	
}

EXPORT void CALL UpdateScreen (void)
{
	VI_UpdateScreen();
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

