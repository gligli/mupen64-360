/**
 * glN64_GX - glN64.h
 * Copyright (C) 2003 Orkin
 *
 * glN64 homepage: http://gln64.emulation64.com
 * Wii64 homepage: http://www.emulatemii.com
 *
**/

#ifndef XENOS_GFX_H
#define XENOS_GFX_H

#include "../main/winlnxdefs.h"
#include "gSP.h"

//#define DEBUG
//#define RSPTHREAD

#define MAKE_COLOR3(r,g,b) (0xff000000 | ((b)<<16) | ((g)<<8) | (r))
#define MAKE_COLOR4(r,g,b,a) ((a)<<24 | ((b)<<16) | ((g)<<8) | (r))
#define MAKE_COLOR1F(c) ((u8)(255.0f*((c)>1.0f?1.0f:(c))))
#define MAKE_COLOR4F(r,g,b,a) (MAKE_COLOR1F(a)<<24 | (MAKE_COLOR1F(b)<<16) | (MAKE_COLOR1F(g)<<8) | MAKE_COLOR1F(r))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define MIN(a,b) (((a)<(b))?(a):(b))

extern char			pluginName[];

extern void (*CheckInterrupts)( void );
extern char *screenDirectory;

void xeGfx_matrixDump(const char *name, float m[4][4]);
void xeGfx_clearDepthBuffer();
void xeGfx_clearColorBuffer(float *color);
void xeGfx_drawRect(int ulx,int uly,int lrx,int lry,float *color);
void xeGfx_drawTexturedRect(int ulx,int uly,int lrx,int lry,float uls,float ult,float lrs,float lrt,bool flip);
void xeGfx_setCombinerConstantF(int start,float * data,int count);
void xeGfx_setCombinerConstantB(int index, bool value);
void xeGfx_setCombinerShader(int colorOps,int alphaOps,bool slow);
void * xeGfx_createTexture(int width,int height);
void xeGfx_destroyTexture(void * tex);
void xeGfx_setTextureData(void * tex,void * buffer);
void xeGfx_activateTexture(void * tex,int index, int filter, int clamps, int clampt);
void xeGfx_activateFrameBufferTexture(int index);
void xeGfx_addTriangle(SPVertex *vertices,int v0,int v1,int v2,int direct);
void xeGfx_drawTriangles();
void xeGfx_init();
void xeGfx_render();

#endif

