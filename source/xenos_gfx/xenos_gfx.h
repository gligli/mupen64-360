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
void xeGfx_setCombinerShader(bool oneColorOp,bool oneAlphaOp,bool slow);
void * xeGfx_createTexture(int width,int height);
void xeGfx_destroyTexture(void * tex);
void xeGfx_setTextureData(void * tex,void * buffer);
void xeGfx_activateTexture(void * tex,int index, int filter, int clamps, int clampt);
void xeGfx_activateFrameBufferTexture(int index);
void xeGfx_addTriangle(SPVertex *vertices,int v0,int v1,int v2);
void xeGfx_drawTriangles();
void xeGfx_init();
void xeGfx_render();

#endif

