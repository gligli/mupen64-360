#include "stdafx.h"

#include "xenos_backend.h"

#include <xenos/xe.h>
#include <libxemit/xemit.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <zlx/zlx.h>
#include <debug.h>
#include <byteswap.h>

#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif

#define MAKE_COLOR3(r,g,b) (0xff000000 | ((b)<<16) | ((g)<<8) | (r))
#define MAKE_COLOR4(r,g,b,a) ((a)<<24 | ((b)<<16) | ((g)<<8) | (r))
#define MAKE_COLOR1F(c) ((u8)(255.0f*((c)>1.0f?1.0f:(c))))
#define MAKE_COLOR4F(r,g,b,a) (MAKE_COLOR1F(a)<<24 | (MAKE_COLOR1F(b)<<16) | (MAKE_COLOR1F(g)<<8) | MAKE_COLOR1F(r))

#define MAX_VERTEX_COUNT (16384*3)

#define NEAR (-10.0)
#define FAR  (10.0)

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
	unsigned long color;
} TVertex;

struct XenosDevice *xe;
struct XenosShader *sh_ps_fb, *sh_vs;
struct XenosVertexBuffer *vertexBuffer;

extern u32 vs_table_count;
extern void * vs_data_table[];
extern u32 vs_size_table[];
extern u32 vs_indice_count;
extern u32 vs_indices[];

extern u32 ps_fb_table_count;
extern void * ps_fb_data_table[];
extern u32 ps_fb_size_table[];
extern u32 ps_fb_indice_count;
extern u32 ps_fb_indices[];

// used for double buffering
struct XenosSurface * framebuffer[2] = {NULL};
int curFB=0;

int prevVertexCount=0;

TVertex * firstVertex;
TVertex * currentVertex;

int xe_cull;
bool xe_zcompare;
bool xe_zenable;
bool xe_zwrite;
bool xe_alphatest;

float xe_origx=0;
float xe_origy=0;
float xe_scalex=1;
float xe_scaley=1;

bool xe_needEndRender=false;

int vertexCount()
{
    return currentVertex-firstVertex;
}

void resetLockVB()
{
	Xe_SetStreamSource(xe, 0, vertexBuffer, 0, 4);
	firstVertex=currentVertex=(TVertex *)Xe_VB_Lock(xe,vertexBuffer,0,MAX_VERTEX_COUNT*sizeof(TVertex),XE_LOCK_WRITE);
	prevVertexCount=0;
}

void nextVertex()
{
	++currentVertex;
	
    if (vertexCount()>=MAX_VERTEX_COUNT)
	{
		printf("[rice_xenos] too many vertices !\n");
		exit(1);
    }
}

void drawVB()
{
	if (vertexCount()>prevVertexCount)
	{
		Xe_DrawPrimitive(xe,XE_PRIMTYPE_TRIANGLELIST,prevVertexCount,(vertexCount()-prevVertexCount)/3);
        prevVertexCount=vertexCount();
    }
}

void endRenderSync()
{
	if(!xe_needEndRender) return;
	
	Xe_Sync(xe);

    Xe_SetFrameBufferSurface(xe,framebuffer[curFB]);
    curFB=1-curFB;
    Xe_SetRenderTarget(xe,framebuffer[curFB]);
    
	resetLockVB();
	
	xe_needEndRender=false;
}

void setZStuff()
{
	Xe_SetZEnable(xe,xe_zenable?1:0);
	Xe_SetZWrite(xe,xe_zwrite?1:0);
	Xe_SetZFunc(xe,xe_zcompare?XE_CMP_LESSEQUAL:XE_CMP_ALWAYS);
}

void setAlphaStuff()
{
	Xe_SetAlphaTestEnable(xe,xe_alphatest?1:0);
	Xe_SetAlphaFunc(xe,XE_CMP_GREATER);
}

////////////////////////////////////////////////////////////////////////////////
// CxeGraphicsContext
////////////////////////////////////////////////////////////////////////////////

CxeGraphicsContext::CxeGraphicsContext()
{
	static int done=0;

	xe = ZLX::g_pVideoDevice;

    XenosSurface * fb = Xe_GetFramebufferSurface(xe);

    windowSetting.uWindowDisplayWidth = windowSetting.uFullScreenDisplayWidth = fb->width;
    windowSetting.uWindowDisplayHeight = windowSetting.uFullScreenDisplayHeight = fb->height;

    if(!done)
	{

		/* initialize the GPU */

		Xe_SetRenderTarget(xe, fb);
		Xe_InvalidateState(xe);
		Xe_SetClearColor(xe,0);

		/* load pixel shaders */

		assert(ps_fb_table_count==1 && ps_fb_indice_count==1);
		sh_ps_fb = Xe_LoadShaderFromMemory(xe, ps_fb_data_table[0]);
		Xe_InstantiateShader(xe, sh_ps_fb, 0);

		/* load vertex shader */

		assert(vs_table_count==1 && vs_indice_count==1);
		sh_vs = Xe_LoadShaderFromMemory(xe, vs_data_table[0]);
		Xe_InstantiateShader(xe, sh_vs, 0);
		Xe_ShaderApplyVFetchPatches(xe, sh_vs, 0, &VertexBufferFormat);

		vertexBuffer = Xe_CreateVertexBuffer(xe,MAX_VERTEX_COUNT*sizeof(TVertex));

		// Create surface for double buffering
		framebuffer[0] = Xe_CreateTexture(xe, fb->width, fb->height, 0, XE_FMT_8888 | XE_FMT_BGRA, 1);
		framebuffer[1] = Xe_CreateTexture(xe, fb->width, fb->height, 0, XE_FMT_8888 | XE_FMT_BGRA, 1);        

		done=1;
	}
}

CxeGraphicsContext::~CxeGraphicsContext()
{

}

void CxeGraphicsContext::InitState(void)
{
}

void CxeGraphicsContext::InitOGLExtension(void)
{
    
}

bool CxeGraphicsContext::IsExtensionSupported(const char* pExtName)
{
    TRS(pExtName);
    return true;
}

bool CxeGraphicsContext::IsWglExtensionSupported(const char* pExtName)
{
    TRS(pExtName);
    return true;
}

void CxeGraphicsContext::CleanUp()
{
   	endRenderSync();
	
    if (vertexBuffer->lock.start) Xe_VB_Unlock(xe,vertexBuffer);
	Xe_SetScissor(xe,0,0,0,0,0);
    Xe_SetFrameBufferSurface(xe,&xe->default_fb);
    Xe_SetRenderTarget(xe,&xe->default_fb);
}

void CxeGraphicsContext::Clear(ClearFlag dwFlags, uint32 color, float depth)
{
   	endRenderSync();
	
	uint32 flag=0;
    if( dwFlags&CLEAR_COLOR_BUFFER )    flag |= XE_CLEAR_COLOR;
    if( dwFlags&CLEAR_DEPTH_BUFFER )	flag |= XE_CLEAR_DS;

	u8 r = (u8) ((color>>16)&0xFF);
	u8 g = (u8) ((color>> 8)&0xFF);
	u8 b = (u8) ((color    )&0xFF);
	u8 a = (u8) ((color>>24)&0xFF);
	
	Xe_SetClearColor(xe,MAKE_COLOR4(r,g,b,a));
	
//	TRF(depth);
	
	Xe_Clear(xe,flag);
}

void CxeGraphicsContext::UpdateFrame(bool swaponly)
{
	status.gFrameCount++;

	if (vertexBuffer->lock.start)
	{
		Xe_VB_Unlock(xe,vertexBuffer);

		Xe_ResolveInto(xe,xe->rt,XE_SOURCE_COLOR,0);

		Xe_Execute(xe);
		xe_needEndRender=true;

		Xe_InvalidateState(xe);

		if(g_curRomInfo.bForceScreenClear ) 
			needCleanScene = true;

		status.bScreenIsDrawn = false;	
	}
}

bool CxeGraphicsContext::SetFullscreenMode()
{
	return false;
}

bool CxeGraphicsContext::SetWindowMode()
{
	return false;
}
int CxeGraphicsContext::ToggleFullscreen()
{
	return false;
}

// This is a static function, will be called when the plugin DLL is initialized
void CxeGraphicsContext::InitDeviceParameters()
{
}

// Get methods
bool CxeGraphicsContext::IsSupportAnisotropicFiltering()
{
	return true;
}

int CxeGraphicsContext::getMaxAnisotropicFiltering()
{
	return 2;
}

////////////////////////////////////////////////////////////////////////////////
// CxeRender
////////////////////////////////////////////////////////////////////////////////

UVFlagMap xeXUVFlagMaps[] =
{
{TEXTURE_UV_FLAG_WRAP, XE_TEXADDR_WRAP},
{TEXTURE_UV_FLAG_MIRROR, XE_TEXADDR_MIRROR},
{TEXTURE_UV_FLAG_CLAMP, XE_TEXADDR_CLAMP},
};

CxeRender::CxeRender()
{
    m_bSupportFogCoordExt = false;
    m_bSupportClampToEdge = true;

    m_maxTexUnits = 8;

    for( int i=0; i<8; i++ )
    {
        m_curBoundTex[i]=0;
        m_textureUnitMap[i] = -1;
    }

    m_textureUnitMap[0] = 0;    // T0 is usually using texture unit 0
    m_textureUnitMap[1] = 1;    // T1 is usually using texture unit 1
}

CxeRender::~CxeRender()
{
    ClearDeviceObjects();
}

bool CxeRender::InitDeviceObjects()
{
    // enable Z-buffer by default
    ZBufferEnable(true);
    return true;
}

bool CxeRender::ClearDeviceObjects()
{
    return true;
}

void CxeRender::Initialize(void)
{
	xe_cull=XE_CULL_NONE;
	xe_zenable=true;
	xe_zwrite=true;
	xe_zcompare=true;
	m_bPolyOffset=false;
	
    glViewportWrapper(0, windowSetting.statusBarHeightToUse, windowSetting.uDisplayWidth, windowSetting.uDisplayHeight,true);
	
	Xe_SetClearColor(xe,0);
	Xe_SetRenderTarget(xe,framebuffer[0]);
	Xe_Clear(xe,XE_CLEAR_COLOR|XE_CLEAR_DS);
	Xe_SetRenderTarget(xe,framebuffer[1]);
	Xe_Clear(xe,XE_CLEAR_COLOR|XE_CLEAR_DS);

	xe_needEndRender=false;
	
	resetLockVB();
    RenderReset();
}

void CxeRender::ApplyTextureFilter()
{
    for( int i=0; i<m_maxTexUnits; i++ )
    {
        int iMinFilter = (m_dwMinFilter == FILTER_LINEAR ? 1 : 0);
        int iMagFilter = (m_dwMagFilter == FILTER_LINEAR ? 1 : 0);
        if( m_texUnitEnabled[i] )
        {
			if (m_curBoundTex[i] && m_curBoundTex[i]->tex) 
			{
				m_curBoundTex[i]->tex->use_filtering=iMinFilter | iMagFilter;
			}
        }
    }
}

void CxeRender::SetShadeMode(RenderShadeMode mode)
{

}

void CxeRender::OneCLRVtx(u32 i,u32 j, float depth)
{
	float dw=windowSetting.uDisplayWidth;
	float dh=windowSetting.uDisplayHeight;
	
	float xv[2]= {0.0f,dw};
	float yv[2]= {0.0f,dh};
	
	currentVertex->x=xv[i];
	currentVertex->y=yv[j];
	currentVertex->z=depth;
	currentVertex->w=1.0;
	currentVertex->color=0;
	currentVertex->u0=currentVertex->u1=currentVertex->v0=currentVertex->v1=0.0;
	nextVertex();
}

void CxeRender::ClearBuffer(bool cbuffer, bool zbuffer)
{
	float depth = ((gRDP.originalFillColor&0xFFFF)>>2)/(float)0x3FFF;
	
#if 1
    int flag=0;
    if( cbuffer )    flag |= CLEAR_COLOR_BUFFER;
    if( zbuffer )    flag |= CLEAR_DEPTH_BUFFER;

	CGraphicsContext::Get()->Clear((ClearFlag)flag,0,depth);
#else
	endRenderSync();
	
	Xe_SetScissor(xe,0,0,0,0,0);

	glViewportWrapper(0, windowSetting.statusBarHeightToUse, windowSetting.uDisplayWidth, windowSetting.uDisplayHeight,true);

	Xe_SetAlphaTestEnable(xe,0);
	if(cbuffer)
	{
		Xe_SetBlendControl(xe,XE_BLEND_ONE,XE_BLENDOP_ADD,XE_BLEND_ZERO,XE_BLEND_ONE,XE_BLENDOP_ADD,XE_BLEND_ZERO);
	}
	else
	{
		Xe_SetBlendControl(xe,XE_BLEND_ZERO,XE_BLENDOP_ADD,XE_BLEND_ONE,XE_BLEND_ZERO,XE_BLENDOP_ADD,XE_BLEND_ONE);
	}
	
	if(zbuffer)
	{
		Xe_SetZEnable(xe,1);
		Xe_SetZWrite(xe,1);
		Xe_SetZFunc(xe,XE_CMP_ALWAYS);
	}
	else
	{
		Xe_SetZEnable(xe,0);
		Xe_SetZWrite(xe,0);
	}
	
    ApplyZBias(0);  // disable z offsets
	Xe_SetCullMode(xe,XE_CULL_NONE);

	OneCLRVtx(0,0,depth);
	OneCLRVtx(1,0,depth);
	OneCLRVtx(1,1,depth);

	OneCLRVtx(0,0,depth);
	OneCLRVtx(1,1,depth);
	OneCLRVtx(0,1,depth);
	
	drawVB();

	Xe_SetCullMode(xe,xe_cull);
    ApplyZBias(m_dwZBias);          // set Z offset back to previous value

	setZStuff();
	setAlphaStuff();
#endif
}

void CxeRender::ClearZBuffer(float depth)
{
#if 1	
	CGraphicsContext::Get()->Clear(CLEAR_DEPTH_BUFFER,0,depth);
#else	
	endRenderSync();
	
	glViewportWrapper(0, windowSetting.statusBarHeightToUse, windowSetting.uDisplayWidth, windowSetting.uDisplayHeight,true);

	Xe_SetScissor(xe,0,0,0,0,0);

	Xe_SetAlphaTestEnable(xe,0);
	Xe_SetBlendControl(xe,XE_BLEND_ZERO,XE_BLENDOP_ADD,XE_BLEND_ONE,XE_BLEND_ZERO,XE_BLENDOP_ADD,XE_BLEND_ONE);
	
	Xe_SetZEnable(xe,1);
	Xe_SetZWrite(xe,1);
	Xe_SetZFunc(xe,XE_CMP_ALWAYS);

    ApplyZBias(0);  // disable z offsets
	Xe_SetCullMode(xe,XE_CULL_NONE);

	OneCLRVtx(0,0,depth);
	OneCLRVtx(1,0,depth);
	OneCLRVtx(1,1,depth);

	OneCLRVtx(0,0,depth);
	OneCLRVtx(1,1,depth);
	OneCLRVtx(0,1,depth);
	
	drawVB();

	Xe_SetCullMode(xe,xe_cull);
    ApplyZBias(m_dwZBias);          // set Z offset back to previous value

	setZStuff();
	setAlphaStuff();
#endif
}

void CxeRender::ZBufferEnable(BOOL bZBuffer)
{
	gRSP.bZBufferEnabled = bZBuffer;
    if( g_curRomInfo.bForceDepthBuffer )
        bZBuffer = TRUE;

	xe_zenable=bZBuffer;
	xe_zcompare=bZBuffer;
	setZStuff();
}

void CxeRender::SetZCompare(BOOL bZCompare)
{
    if( g_curRomInfo.bForceDepthBuffer )
        bZCompare = TRUE;

    gRSP.bZBufferEnabled = bZCompare;

	xe_zcompare=bZCompare;
	setZStuff();
}

void CxeRender::SetZUpdate(BOOL bZUpdate)
{
    if( g_curRomInfo.bForceDepthBuffer )
        bZUpdate = TRUE;

	xe_zwrite=bZUpdate;
	setZStuff();
}

void CxeRender::ApplyZBias(int bias)
{
	if (bias > 0)
	{
		m_bPolyOffset = true;
	}
	else 
	{
		m_bPolyOffset = false;
	}
//	float f1 = bias > 0 ? -3.0f : 0.0f;  // z offset = -3.0 * max(abs(dz/dx),abs(dz/dy)) per pixel delta z slope
//	loat f2 = bias > 0 ? -3.0f : 0.0f;  // z offset += -3.0 * 1 bit
}

void CxeRender::SetZBias(int bias)
{
    // set member variable and apply the setting in opengl
    m_dwZBias = bias;
    ApplyZBias(bias);
}

void CxeRender::SetAlphaTestEnable(BOOL bAlphaTestEnable)
{
    if( bAlphaTestEnable )
        xe_alphatest=true;
    else
        xe_alphatest=false;
	
	setAlphaStuff();
}

void CxeRender::SetAlphaRef(uint32 dwAlpha)
{
	m_dwAlpha = dwAlpha;
	float ref = dwAlpha/255.0f-1.0f/255.0f;
	if(ref<0.0f) ref=0.0f;

	Xe_SetAlphaRef(xe,ref);

	setAlphaStuff();
}

void CxeRender::ForceAlphaRef(uint32 dwAlpha)
{
    float ref = dwAlpha/255.0f-1.0f/255.0f;
	if(ref<0.0f) ref=0.0f;

    Xe_SetAlphaRef(xe,ref);

	setAlphaStuff();
}

void CxeRender::SetFillMode(FillMode mode)
{
/*	if( mode == RICE_FILLMODE_WINFRAME )
    {
        Xe_SetFillMode(xe,XE_FILL_WIREFRAME,XE_FILL_WIREFRAME);
    }
    else
    {
        Xe_SetFillMode(xe,XE_FILL_SOLID,XE_FILL_SOLID);
    }*/
}

void CxeRender::SetCullMode(bool bCullFront, bool bCullBack)
{
    CRender::SetCullMode(bCullFront, bCullBack);
    if( bCullFront && bCullBack )
    {
        xe_cull=XE_CULL_NONE;
    }
    else if( bCullFront )
    {
        xe_cull=XE_CULL_CCW;
    }
    else if( bCullBack )
    {
        xe_cull=XE_CULL_CW;
    }
    else
    {
        xe_cull=XE_CULL_NONE;
    }

	Xe_SetCullMode(xe,xe_cull);
}

bool CxeRender::SetCurrentTexture(int tile, CTexture *handler,uint32 dwTileWidth, uint32 dwTileHeight, TxtrCacheEntry *pTextureEntry)
{
    RenderTexture &texture = g_textures[tile];
    texture.pTextureEntry = pTextureEntry;

    if( handler!= NULL  && texture.m_lpsTexturePtr != handler->GetTexture() )
    {
        texture.m_pCTexture = handler;
        texture.m_lpsTexturePtr = handler->GetTexture();

        texture.m_dwTileWidth = dwTileWidth;
        texture.m_dwTileHeight = dwTileHeight;

        if( handler->m_bIsEnhancedTexture )
        {
            texture.m_fTexWidth = (float)pTextureEntry->pTexture->m_dwCreatedTextureWidth;
            texture.m_fTexHeight = (float)pTextureEntry->pTexture->m_dwCreatedTextureHeight;
        }
        else
        {
            texture.m_fTexWidth = (float)handler->m_dwCreatedTextureWidth;
            texture.m_fTexHeight = (float)handler->m_dwCreatedTextureHeight;
        }
    }
	
	
    return true;
}

bool CxeRender::SetCurrentTexture(int tile, TxtrCacheEntry *pEntry)
{
    if (pEntry != NULL && pEntry->pTexture != NULL)
    {   
        SetCurrentTexture( tile, pEntry->pTexture,  pEntry->ti.WidthToCreate, pEntry->ti.HeightToCreate, pEntry);
        return true;
    }
    else
    {
        SetCurrentTexture( tile, NULL, 64, 64, NULL );
        return false;
    }
    return true;
}

void CxeRender::SetTexWrapS(int unitno,u32 flag)
{
	assert(false);
}
void CxeRender::SetTexWrapT(int unitno,u32 flag)
{
	assert(false);
}

void CxeRender::SetTextureUFlag(TextureUVFlag dwFlag, uint32 dwTile)
{
    TileUFlags[dwTile] = dwFlag;
    int tex;
    if( dwTile == gRSP.curTile )
        tex=0;
    else if( dwTile == ((gRSP.curTile+1)&7) )
        tex=1;
    else
    {
        if( dwTile == ((gRSP.curTile+2)&7) )
            tex=2;
        else if( dwTile == ((gRSP.curTile+3)&7) )
            tex=3;
        else
        {
            TRACE2("Incorrect tile number for OGL SetTextureUFlag: cur=%d, tile=%d", gRSP.curTile, dwTile);
            return;
        }
    }

    for( int textureNo=0; textureNo<8; textureNo++)
    {
        if( m_textureUnitMap[textureNo] == tex )
        {
            CxeTexture* pTexture = g_textures[(gRSP.curTile+tex)&7].m_pCxeTexture;
            if( pTexture ) 
            {
                EnableTexUnit(textureNo,TRUE);
				
				pTexture->tex->u_addressing=xeXUVFlagMaps[dwFlag].realFlag;
				
	            BindTexture(pTexture, textureNo);
            }
            //SetTexWrapS(textureNo, xeXUVFlagMaps[dwFlag].realFlag);
        }
    }
}

void CxeRender::SetTextureVFlag(TextureUVFlag dwFlag, uint32 dwTile)
{
    TileVFlags[dwTile] = dwFlag;
    int tex;
    if( dwTile == gRSP.curTile )
        tex=0;
    else if( dwTile == ((gRSP.curTile+1)&7) )
        tex=1;
    else
    {
        if( dwTile == ((gRSP.curTile+2)&7) )
            tex=2;
        else if( dwTile == ((gRSP.curTile+3)&7) )
            tex=3;
        else
        {
            TRACE2("Incorrect tile number for OGL SetTextureVFlag: cur=%d, tile=%d", gRSP.curTile, dwTile);
            return;
        }
    }
	
    for( int textureNo=0; textureNo<8; textureNo++)
    {
        if( m_textureUnitMap[textureNo] == tex )
        {
            CxeTexture* pTexture = g_textures[(gRSP.curTile+tex)&7].m_pCxeTexture;
            if( pTexture )
            {
                EnableTexUnit(textureNo,TRUE);
	           
				pTexture->tex->v_addressing=xeXUVFlagMaps[dwFlag].realFlag;
				
				BindTexture(pTexture, textureNo);
            }
            //SetTexWrapT(textureNo, xeXUVFlagMaps[dwFlag].realFlag);
        }
    }
}

void CxeRender::SetTextureToTextureUnitMap(int tex, int unit)
{
    if( unit < 8 && (tex >= -1 || tex <= 1))
        m_textureUnitMap[unit] = tex;
}



// Basic render drawing functions

void CxeRender::OneRTRVtx(u32 i)
{
    float depth =-(g_texRectTVtx[3].z*2-1);

	currentVertex->x=g_texRectTVtx[i].x;
	currentVertex->y=g_texRectTVtx[i].y;
	currentVertex->z=depth;
	currentVertex->w=1.0;
	currentVertex->color=MAKE_COLOR4(g_texRectTVtx[i].r,g_texRectTVtx[i].g,g_texRectTVtx[i].b,g_texRectTVtx[i].a);
	currentVertex->u0=g_texRectTVtx[i].tcord[0].u;
	currentVertex->v0=g_texRectTVtx[i].tcord[0].v;
	currentVertex->u1=g_texRectTVtx[i].tcord[1].u;
	currentVertex->v1=g_texRectTVtx[i].tcord[1].v;

	nextVertex();
}

bool CxeRender::RenderTexRect()
{
//return true;
	endRenderSync();
	
	glViewportWrapper(0, windowSetting.statusBarHeightToUse, windowSetting.uDisplayWidth, windowSetting.uDisplayHeight,true);

	Xe_SetCullMode(xe,XE_CULL_NONE);

	OneRTRVtx(0);
	OneRTRVtx(1);
	OneRTRVtx(2);

	OneRTRVtx(0);
	OneRTRVtx(2);
	OneRTRVtx(3);
	
	drawVB();

	Xe_SetCullMode(xe,xe_cull);

    return true;
}

void CxeRender::OneRFRVtx(u32 i,u32 j, u32 dwColor, float depth)
{
	u8 r = (u8) ((dwColor>>16)&0xFF);
	u8 g = (u8) ((dwColor>>8)&0xFF);
	u8 b = (u8) (dwColor&0xFF);
	u8 a = (u8) (dwColor >>24);
	
	currentVertex->x=m_fillRectVtx[i].x;
	currentVertex->y=m_fillRectVtx[j].y;
	currentVertex->z=depth;
	currentVertex->w=1.0;
	currentVertex->color=MAKE_COLOR4(r,g,b,a);
	currentVertex->u0=currentVertex->u1=currentVertex->v0=currentVertex->v1=0.0;
	nextVertex();
}

bool CxeRender::RenderFillRect(uint32 dwColor, float depth)
{
//return true;
	endRenderSync();
	
	glViewportWrapper(0, windowSetting.statusBarHeightToUse, windowSetting.uDisplayWidth, windowSetting.uDisplayHeight,true);

	Xe_SetCullMode(xe,XE_CULL_NONE);
	
//	depth=-(depth*2-1);
	
	OneRFRVtx(0,0,dwColor,depth);
	OneRFRVtx(1,0,dwColor,depth);
	OneRFRVtx(1,1,dwColor,depth);

	OneRFRVtx(0,0,dwColor,depth);
	OneRFRVtx(1,1,dwColor,depth);
	OneRFRVtx(0,1,dwColor,depth);

	drawVB();

	Xe_SetCullMode(xe,xe_cull);

    return true;
}

bool CxeRender::RenderLine3D()
{
    ApplyZBias(0);  // disable z offsets

#if 0	
    glBegin(GL_TRIANGLE_FAN);

    glColor4f(m_line3DVtx[1].r, m_line3DVtx[1].g, m_line3DVtx[1].b, m_line3DVtx[1].a);
    glVertex3f(m_line3DVector[3].x, m_line3DVector[3].y, -m_line3DVtx[1].z);
    glVertex3f(m_line3DVector[2].x, m_line3DVector[2].y, -m_line3DVtx[0].z);
    
    glColor4ub(m_line3DVtx[0].r, m_line3DVtx[0].g, m_line3DVtx[0].b, m_line3DVtx[0].a);
    glVertex3f(m_line3DVector[1].x, m_line3DVector[1].y, -m_line3DVtx[1].z);
    glVertex3f(m_line3DVector[0].x, m_line3DVector[0].y, -m_line3DVtx[0].z);

    glEnd();
    OPENGL_CHECK_ERRORS;
#endif
	
    ApplyZBias(m_dwZBias);          // set Z offset back to previous value

    return true;
}

extern FiddledVtx * g_pVtxBase;

// This is so weired that I can not do vertex transform by myself. I have to use
// OpenGL internal transform
bool CxeRender::RenderFlushTris()
{
	endRenderSync();
	
    if( !m_bSupportFogCoordExt )    
        SetFogFlagForNegativeW();
    else
    {
        if( !gRDP.bFogEnableInBlender && gRSP.bFogEnabled )
        {
//            glDisable(GL_FOG);
        }
    }

    ApplyZBias(m_dwZBias);                    // set the bias factors
	
//	Xe_SetZEnable(xe,0);
//	Xe_SetZWrite(xe,0);
//	Xe_SetCullMode(xe,XE_CULL_NONE);
//	Xe_SetFillMode(xe,XE_FILL_WIREFRAME,XE_FILL_WIREFRAME);
	
    glViewportWrapper(windowSetting.vpLeftW, windowSetting.uDisplayHeight-windowSetting.vpTopW-windowSetting.vpHeightW+windowSetting.statusBarHeightToUse, windowSetting.vpWidthW, windowSetting.vpHeightW, false);

	u32 i;
	u32 nvr=gRSP.numVertices;
	
	for(i=0;i<nvr;++i)
	{
#if 0
		currentVertex->x=g_vtxBuffer[i].x;
		currentVertex->y=g_vtxBuffer[i].y;
		currentVertex->z=g_vtxBuffer[i].z;
		currentVertex->w=1.0f;//g_vtxBuffer[i].rhw;
		
		currentVertex->color=MAKE_COLOR4(g_vtxBuffer[i].r,g_vtxBuffer[i].g,g_vtxBuffer[i].b,g_vtxBuffer[i].a);
#else
		int vi=g_vtxIndex[i];
		float zoffset=m_bPolyOffset?0.999f:1.0f;
#if 0
		float invW = 1.0f/g_vtxProjected5[vi][3];

		currentVertex->x=g_vtxProjected5[vi][0]*invW;
		currentVertex->y=g_vtxProjected5[vi][1]*invW;
		currentVertex->z=g_vtxProjected5[vi][2]*invW;
		currentVertex->w=1.0f;
#else
		currentVertex->x=g_vtxProjected5[vi][0];
		currentVertex->y=g_vtxProjected5[vi][1];
		currentVertex->z=g_vtxProjected5[vi][2]*zoffset;
		currentVertex->w=g_vtxProjected5[vi][3];
#endif
		
		currentVertex->color=MAKE_COLOR4(g_oglVtxColors[vi][0],g_oglVtxColors[vi][1],g_oglVtxColors[vi][2],g_oglVtxColors[vi][3]);
#endif		
		currentVertex->u0=g_vtxBuffer[i].tcord[0].u;
		currentVertex->v0=g_vtxBuffer[i].tcord[0].v;
		currentVertex->u1=g_vtxBuffer[i].tcord[1].u;
		currentVertex->v1=g_vtxBuffer[i].tcord[1].v;
		
//		printf("%.3f %.3f %.3f %.3f\n",currentVertex->x,currentVertex->y,currentVertex->z,currentVertex->w);
		
		nextVertex();
	}

	drawVB();
	
    if( !m_bSupportFogCoordExt )    
        RestoreFogFlag();
    else
    {
        if( !gRDP.bFogEnableInBlender && gRSP.bFogEnabled )
        {
//            glEnable(GL_FOG);
        }
    }
    return true;
}


void CxeRender::OneDSTVtx(u32 i)
{
	u8 r = (u8) ((g_texRectTVtx[0].dcDiffuse>>16)&0xFF);
	u8 g = (u8) ((g_texRectTVtx[0].dcDiffuse>>8)&0xFF);
	u8 b = (u8) (g_texRectTVtx[0].dcDiffuse&0xFF);
	u8 a = (u8) (g_texRectTVtx[0].dcDiffuse >>24);
	
	currentVertex->x=g_texRectTVtx[i].x;
	currentVertex->y=g_texRectTVtx[i].y;
	currentVertex->z=-g_texRectTVtx[i].z;
	currentVertex->w=1.0f;
	currentVertex->color=MAKE_COLOR4(r,g,b,a);
	currentVertex->u0=g_texRectTVtx[i].tcord[0].u;
	currentVertex->v0=g_texRectTVtx[i].tcord[0].v;
	currentVertex->u1=g_texRectTVtx[i].tcord[1].u;
	currentVertex->v1=g_texRectTVtx[i].tcord[1].v;
	nextVertex();

}

void CxeRender::DrawSimple2DTexture(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, COLOR dif, COLOR spe, float z, float rhw)
{
	endRenderSync();
	
	if( status.bVIOriginIsUpdated == true && currentRomOptions.screenUpdateSetting==SCREEN_UPDATE_AT_1ST_PRIMITIVE )
    {
        status.bVIOriginIsUpdated=false;
        CGraphicsContext::Get()->UpdateFrame();
        DEBUGGER_PAUSE_AND_DUMP_NO_UPDATE(NEXT_SET_CIMG,{DebuggerAppendMsg("Screen Update at 1st Simple2DTexture");});
    }

    StartDrawSimple2DTexture(x0, y0, x1, y1, u0, v0, u1, v1, dif, spe, z, rhw);

	Xe_SetCullMode(xe,XE_CULL_NONE);
	
    glViewportWrapper(0, windowSetting.statusBarHeightToUse, windowSetting.uDisplayWidth, windowSetting.uDisplayHeight,true);

	OneDSTVtx(0);
	OneDSTVtx(1);
	OneDSTVtx(2);

	OneDSTVtx(0);
	OneDSTVtx(2);
	OneDSTVtx(3);
	
	drawVB();

	Xe_SetCullMode(xe,xe_cull);
}

void CxeRender::DrawSimpleRect(int nX0, int nY0, int nX1, int nY1, uint32 dwColor, float depth, float rhw)
{
	assert(false);
}

void CxeRender::DrawText(const char* str, RECT *rect)
{
	assert(false);
}

void CxeRender::OneDSRRVtx(u32 i)
{
	u8 r = (u8) (gRDP.fvPrimitiveColor[0]*255.0f);
	u8 g = (u8) (gRDP.fvPrimitiveColor[1]*255.0f);
	u8 b = (u8) (gRDP.fvPrimitiveColor[2]*255.0f);
	u8 a = (u8) (gRDP.fvPrimitiveColor[3]*255.0f);
	
	currentVertex->x=g_texRectTVtx[i].x;
	currentVertex->y=g_texRectTVtx[i].y;
	currentVertex->z=-g_texRectTVtx[i].z;
	currentVertex->w=1.0;
	currentVertex->color=MAKE_COLOR4(r,g,b,a);
	currentVertex->u0=g_texRectTVtx[i].tcord[0].u;
	currentVertex->v0=g_texRectTVtx[i].tcord[0].v;
	currentVertex->u1=g_texRectTVtx[i].tcord[1].u;
	currentVertex->v1=g_texRectTVtx[i].tcord[1].v;
	nextVertex();
}


void CxeRender::DrawSpriteR_Render()    // With Rotation
{
	endRenderSync();
	
    glViewportWrapper(0, windowSetting.statusBarHeightToUse, windowSetting.uDisplayWidth, windowSetting.uDisplayHeight,true);

	Xe_SetCullMode(xe,XE_CULL_NONE);

	OneDSRRVtx(0);
	OneDSRRVtx(1);
	OneDSRRVtx(2);

	OneDSRRVtx(0);
	OneDSRRVtx(2);
	OneDSRRVtx(3);
	
	drawVB();
	
	Xe_SetCullMode(xe,xe_cull);
}


void CxeRender::DrawObjBGCopy(uObjBg &info)
{
    if( IsUsedAsDI(g_CI.dwAddr) )
    {
        ErrorMsg("Unimplemented: write into Z buffer.  Was mostly commented out in Rice Video 6.1.0");
        return;
    }
    else
    {
        CRender::LoadObjBGCopy(info);
        CRender::DrawObjBGCopy(info);
    }
}


void CxeRender::InitCombinerBlenderForSimpleRectDraw(uint32 tile)
{
    EnableTexUnit(0,FALSE);
    Xe_SetBlendControl(xe,XE_BLEND_SRCALPHA,XE_BLENDOP_ADD,XE_BLEND_INVSRCALPHA,XE_BLEND_SRCALPHA,XE_BLENDOP_ADD,XE_BLEND_INVSRCALPHA);
}

COLOR CxeRender::PostProcessDiffuseColor(COLOR curDiffuseColor)
{
    uint32 color = curDiffuseColor;
    uint32 colorflag = m_pColorCombiner->m_pDecodedMux->m_dwShadeColorChannelFlag;
    uint32 alphaflag = m_pColorCombiner->m_pDecodedMux->m_dwShadeAlphaChannelFlag;
    if( colorflag+alphaflag != MUX_0 )
    {
        if( (colorflag & 0xFFFFFF00) == 0 && (alphaflag & 0xFFFFFF00) == 0 )
        {
            color = (m_pColorCombiner->GetConstFactor(colorflag, alphaflag, curDiffuseColor));
        }
        else
            color = (CalculateConstFactor(colorflag, alphaflag, curDiffuseColor));
    }

    //return (color<<8)|(color>>24);
    return color;
}

COLOR CxeRender::PostProcessSpecularColor()
{
    return 0;
}

void CxeRender::SetViewportRender()
{
    glViewportWrapper(windowSetting.vpLeftW, windowSetting.uDisplayHeight-windowSetting.vpTopW-windowSetting.vpHeightW+windowSetting.statusBarHeightToUse, windowSetting.vpWidthW, windowSetting.vpHeightW,true);
}

void CxeRender::RenderReset()
{
	endRenderSync();

    CRender::RenderReset();
	
	Xe_SetShader(xe,SHADER_TYPE_VERTEX,sh_vs,0);
	m_pColorCombiner->DisableCombiner();
}


void CxeRender::BindTexture(CxeTexture *texture, int unitno)
{
	if( unitno < m_maxTexUnits )
	{
		if(texture)
		{
			texture->tex->use_filtering=m_dwMinFilter == FILTER_LINEAR || m_dwMagFilter == FILTER_LINEAR;
			Xe_SetTexture(xe,unitno,texture->tex);
		}
		else
		{
			Xe_SetTexture(xe,unitno,NULL);
		}
		m_curBoundTex[unitno] = texture;
	}
}

void CxeRender::DisBindTexture(CxeTexture *texture, int unitno)
{
	assert(false);
}

void CxeRender::EnableTexUnit(int unitno, BOOL flag)
{
    if( m_texUnitEnabled[unitno] != flag )
    {
        m_texUnitEnabled[unitno] = flag;
		BindTexture(m_curBoundTex[unitno],unitno);
    }
}

void CxeRender::TexCoord2f(float u, float v)
{
	assert(false);
}

void CxeRender::TexCoord(TLITVERTEX &vtxInfo)
{
	for( int i=0; i<8; i++ )
	{
		if( m_textureUnitMap[i] >= 0 )
		{
			//TODO
		}
	}
}

void CxeRender::UpdateScissor()
{
    if( options.bEnableHacks && g_CI.dwWidth == 0x200 && gRDP.scissor.right == 0x200 && g_CI.dwWidth>(*g_GraphicsInfo.VI_WIDTH_REG & 0xFFF) )
    {
        // Hack for RE2
        uint32 width = *g_GraphicsInfo.VI_WIDTH_REG & 0xFFF;
        uint32 height = (gRDP.scissor.right*gRDP.scissor.bottom)/width;
		
		float ulx = xe_origx;
		float uly = xe_origy;
		float lrx = max(xe_origx + min((width*windowSetting.fMultX) * xe_scalex,windowSetting.uDisplayWidth), 0);
		float lry = max(xe_origy + min((height*windowSetting.fMultY) * xe_scaley,windowSetting.uDisplayHeight), 0);
		Xe_SetScissor(xe,1,(u32) ulx,(u32) uly,(u32) lrx,(u32) lry);
    }
    else
    {
        UpdateScissorWithClipRatio();
    }
}

void CxeRender::ApplyRDPScissor(bool force)
{
    if( !force && status.curScissor == RDP_SCISSOR )    return;

    if( options.bEnableHacks && g_CI.dwWidth == 0x200 && gRDP.scissor.right == 0x200 && g_CI.dwWidth>(*g_GraphicsInfo.VI_WIDTH_REG & 0xFFF) )
    {
        // Hack for RE2
        uint32 width = *g_GraphicsInfo.VI_WIDTH_REG & 0xFFF;
        uint32 height = (gRDP.scissor.right*gRDP.scissor.bottom)/width;

		//Notes: windowSetting.statusBarHeightToUse = 0 for fullscreen mode; uly may be incorrect
		float ulx = xe_origx;
		float uly = xe_origy;
		float lrx = max(xe_origx + min((width*windowSetting.fMultX) * xe_scalex,windowSetting.uDisplayWidth), 0);
		float lry = max(xe_origy + min((height*windowSetting.fMultY) * xe_scaley,windowSetting.uDisplayHeight), 0);
		Xe_SetScissor(xe,1,(u32) ulx,(u32) uly,(u32) lrx,(u32) lry);
    }
    else
    {
		float ulx = max(xe_origx + (gRDP.scissor.left*windowSetting.fMultX) * xe_scalex, 0);
		float uly = max(xe_origy + (gRDP.scissor.top*windowSetting.fMultY) * xe_scaley, 0);
		float lrx = max(xe_origx + min((gRDP.scissor.right*windowSetting.fMultX) * xe_scalex,windowSetting.uDisplayWidth), 0);
		float lry = max(xe_origy + min((gRDP.scissor.bottom*windowSetting.fMultY) * xe_scaley,windowSetting.uDisplayHeight), 0);
		Xe_SetScissor(xe,1,(u32) ulx,(u32) uly,(u32) lrx,(u32) lry);
    }

    status.curScissor = RDP_SCISSOR;
}

void CxeRender::ApplyScissorWithClipRatio(bool force)
{
    if( !force && status.curScissor == RSP_SCISSOR )    return;

	float ulx = max(xe_origx + windowSetting.clipping.left * xe_scalex, 0);
	float uly = max(xe_origy + ((gRSP.real_clip_scissor_top)*windowSetting.fMultY) * xe_scaley, 0);
	float lrx = max(xe_origx + min((windowSetting.clipping.left+windowSetting.clipping.width) * xe_scalex,windowSetting.uDisplayWidth), 0);
	float lry = max(xe_origy + min(((gRSP.real_clip_scissor_top*windowSetting.fMultY) + windowSetting.clipping.height) * xe_scaley,windowSetting.uDisplayHeight), 0);

	Xe_SetScissor(xe,1,(u32) ulx,(u32) uly,(u32) lrx,(u32) lry);

    status.curScissor = RSP_SCISSOR;
}

void CxeRender::SetFogMinMax(float fMin, float fMax)
{

#if 0
	glFogf(GL_FOG_START, gRSPfFogMin); // Fog Start Depth
    OPENGL_CHECK_ERRORS;
    glFogf(GL_FOG_END, gRSPfFogMax); // Fog End Depth
    OPENGL_CHECK_ERRORS;
#endif
}

void CxeRender::TurnFogOnOff(bool flag)
{

#if 0
    if( flag )
        glEnable(GL_FOG);
    else
        glDisable(GL_FOG);
    OPENGL_CHECK_ERRORS;
#endif
}

void CxeRender::SetFogEnable(bool bEnable)
{
    DEBUGGER_IF_DUMP( (gRSP.bFogEnabled != (bEnable==TRUE) && logFog ), TRACE1("Set Fog %s", bEnable? "enable":"disable"));

    gRSP.bFogEnabled = bEnable;
    

#if 0
    if( gRSP.bFogEnabled )
    {
        //TRACE2("Enable fog, min=%f, max=%f",gRSPfFogMin,gRSPfFogMax );
        glFogfv(GL_FOG_COLOR, gRDP.fvFogColor); // Set Fog Color
        OPENGL_CHECK_ERRORS;
        glFogf(GL_FOG_START, gRSPfFogMin); // Fog Start Depth
        OPENGL_CHECK_ERRORS;
        glFogf(GL_FOG_END, gRSPfFogMax); // Fog End Depth
        OPENGL_CHECK_ERRORS;
        glEnable(GL_FOG);
        OPENGL_CHECK_ERRORS;
    }
    else
    {
        glDisable(GL_FOG);
        OPENGL_CHECK_ERRORS;
    }
#endif
}

void CxeRender::SetFogColor(uint32 r, uint32 g, uint32 b, uint32 a)
{
    gRDP.fogColor = COLOR_RGBA(r, g, b, a); 
    gRDP.fvFogColor[0] = r/255.0f;      //r
    gRDP.fvFogColor[1] = g/255.0f;      //g
    gRDP.fvFogColor[2] = b/255.0f;          //b
    gRDP.fvFogColor[3] = a/255.0f;      //a

#if 0
    glFogfv(GL_FOG_COLOR, gRDP.fvFogColor); // Set Fog Color
    OPENGL_CHECK_ERRORS;
#endif
}

void CxeRender::EndRendering(void)
{
    if( CRender::gRenderReferenceCount > 0 ) 
        CRender::gRenderReferenceCount--;
}

void CxeRender::glViewportWrapper(int x, int y, int width, int height, bool ortho)
{
    static int mx=0,my=0;
    static int m_width=0, m_height=0;
    static bool mflag=true;
    static bool mbias=false;

    if( x!=mx || y!=my || width!=m_width || height!=m_height || mflag!=ortho || mbias!=m_bPolyOffset)
    {
        mx=x;
        my=y;
        m_width=width;
        m_height=height;
        mflag=ortho;
		mbias=m_bPolyOffset;
		
//		printf("glViewportWrapper %d %d %d %d %d %d %d\n",x,y,width,height,windowSetting.uDisplayWidth,windowSetting.uDisplayHeight,ortho);
		
		if (ortho)
		{
			float ortho_matrix[4][4] = {
				{2.0f/windowSetting.uDisplayWidth,0,0,-1},
				{0,-2.0f/windowSetting.uDisplayHeight,0,1},
				{0,0,1/(FAR-NEAR),NEAR/(NEAR-FAR)},
				{0,0,0,1},
			};

			Xe_SetVertexShaderConstantF(xe,0,(float*)ortho_matrix,4);
		}
		else
		{
			float vp_x=(f32) (xe_origx + x) / windowSetting.uDisplayWidth;
			float vp_y=(f32) (xe_origy + (windowSetting.uDisplayHeight-(y+height))*xe_scaley) / windowSetting.uDisplayHeight;
			float vp_w=(f32) (xe_scalex * width) / windowSetting.uDisplayWidth;
			float vp_h=(f32) (xe_scaley * height) / windowSetting.uDisplayHeight;

			float viewport_matrix[4][4] = {
				{vp_w,0,0,2.0f*vp_x},
				{0,vp_h,0,-2.0f*vp_y},
				{0,0,1/(FAR-NEAR),NEAR/(NEAR-FAR)},
				{0,0,0,1},
			};

			Xe_SetVertexShaderConstantF(xe,0,(float*)viewport_matrix,4);
		}
    }
}

////////////////////////////////////////////////////////////////////////////////
// CxeBlender
////////////////////////////////////////////////////////////////////////////////

uint32 xe_BlendFuncMaps [] =
{
    XE_BLEND_SRCALPHA,         //Nothing
    XE_BLEND_ZERO,             //BLEND_ZERO               = 1,
    XE_BLEND_ONE,              //BLEND_ONE                = 2,
    XE_BLEND_SRCCOLOR,         //BLEND_SRCCOLOR           = 3,
    XE_BLEND_INVSRCCOLOR,      //BLEND_INVSRCCOLOR        = 4,
    XE_BLEND_SRCALPHA,         //BLEND_SRCALPHA           = 5,
    XE_BLEND_INVSRCALPHA,      //BLEND_INVSRCALPHA        = 6,
    XE_BLEND_DESTALPHA,        //BLEND_DESTALPHA          = 7,
    XE_BLEND_INVDESTALPHA,     //BLEND_INVDESTALPHA       = 8,
    XE_BLEND_DESTCOLOR,        //BLEND_DESTCOLOR          = 9,
    XE_BLEND_INVDESTCOLOR,     //BLEND_INVDESTCOLOR       = 10,
    XE_BLEND_SRCALPHASAT,      //BLEND_SRCALPHASAT        = 11,
    XE_BLEND_SRCALPHASAT,      //BLEND_BOTHSRCALPHA       = 12,    
    XE_BLEND_SRCALPHASAT,      //BLEND_BOTHINVSRCALPHA    = 13,
};


void CxeBlender::NormalAlphaBlender(void)
{
	blend_src=XE_BLEND_SRCALPHA;
	blend_dst=XE_BLEND_INVSRCALPHA;
	Enable();
}

void CxeBlender::DisableAlphaBlender(void)
{
	blend_src=XE_BLEND_ONE;
	blend_dst=XE_BLEND_ZERO;
	Disable();
}

void CxeBlender::BlendFunc(uint32 srcFunc, uint32 desFunc)
{
	blend_src=xe_BlendFuncMaps[srcFunc];
	blend_dst=xe_BlendFuncMaps[desFunc];
}

void CxeBlender::Enable()
{
	Xe_SetBlendControl(xe,blend_src,XE_BLENDOP_ADD,blend_dst,blend_src,XE_BLENDOP_ADD,blend_dst);
}

void CxeBlender::Disable()
{
	Xe_SetBlendControl(xe,XE_BLEND_ONE,XE_BLENDOP_ADD,XE_BLEND_ZERO,XE_BLEND_ONE,XE_BLENDOP_ADD,XE_BLEND_ZERO);
}

////////////////////////////////////////////////////////////////////////////////
// CxeTexture
////////////////////////////////////////////////////////////////////////////////

// thank you ced2911 for this :)
static inline void handle_small_surface(struct XenosSurface * surf, void * buffer){
	int width;
	int height;
	int wpitch;
	int hpitch;
	uint32_t * surf_data;
	uint32_t * data;
	uint32_t * src;	
	
	// don't handle big texture
	if( surf->width>128 && surf->height>32) {
		return;
	}	
	
	width = surf->width;
	height = surf->height;
	wpitch = surf->wpitch / 4;
	hpitch = surf->hpitch;	
	
	if(buffer)
        surf_data = (uint32_t *)buffer;
    else
        surf_data = (uint32_t *)Xe_Surface_LockRect(xe, surf, 0, 0, 0, 0, XE_LOCK_WRITE);
	
	src = data = surf_data;
		
	for(int yp=0; yp<hpitch;yp+=height) {
		int max_h = height;
		if (yp + height> hpitch)
				max_h = hpitch % height;
		for(int y = 0; y<max_h; y++){
			//x order
			for(int xp = 0;xp<wpitch;xp+=width) {
				int max_w = width;
				if (xp + width> wpitch)
					max_w = wpitch % width;

				for(int x = 0; x<max_w; x++) {
					data[x+xp + ((y+yp)*wpitch)]=src[x+ (y*wpitch)];
				}
			}
		}
	}
	
    if(!buffer)
        Xe_Surface_Unlock(xe, surf);
} 

CxeTexture::CxeTexture(uint32 dwWidth, uint32 dwHeight, TextureUsage usage) :
    CTexture(dwWidth,dwHeight,usage)
{
  // Make the width and height be the power of 2
    uint32 w;
    for (w = 1; w < dwWidth; w <<= 1);
    m_dwCreatedTextureWidth = w;
    for (w = 1; w < dwHeight; w <<= 1);
    m_dwCreatedTextureHeight = w;
   
	indirect=dwWidth<32 || dwHeight<32;
	
    if (dwWidth*dwHeight > 256*256)
        TRACE4("Large texture: (%d x %d), created as (%d x %d)", 
            dwWidth, dwHeight,m_dwCreatedTextureWidth,m_dwCreatedTextureHeight);
    
    m_fYScale = (float)m_dwCreatedTextureHeight/(float)m_dwHeight;
    m_fXScale = (float)m_dwCreatedTextureWidth/(float)m_dwWidth;

	tex=Xe_CreateTexture(xe,m_dwCreatedTextureWidth,m_dwCreatedTextureHeight,0,XE_FMT_8888|XE_FMT_ARGB,0);
	
	if(!indirect)
	{
		m_pTexture = Xe_Surface_LockRect(xe,tex,0,0,0,0,XE_LOCK_WRITE);	
		Xe_Surface_Unlock(xe,tex);
	}
	else
	{
		m_pTexture = malloc(m_dwCreatedTextureWidth * m_dwCreatedTextureHeight * GetPixelSize());
	}
}

CxeTexture::~CxeTexture()
{
	if (indirect) free(m_pTexture);
	
	Xe_DestroyTexture(xe,tex);
	tex = NULL;
    m_pTexture = NULL;
    m_dwWidth = 0;
    m_dwHeight = 0;
}

bool CxeTexture::StartUpdate(DrawInfo *di)
{
	Xe_Surface_LockRect(xe,tex,0,0,0,0,XE_LOCK_READ | XE_LOCK_WRITE);	
	
    di->dwHeight = (uint16)m_dwHeight;
    di->dwWidth = (uint16)m_dwWidth;
    di->dwCreatedHeight = m_dwCreatedTextureHeight;
    di->dwCreatedWidth = m_dwCreatedTextureWidth;
    di->lpSurface = m_pTexture;

	if(!indirect)
	{
		di->lPitch = tex->wpitch;
	}
	else
	{
		di->lPitch = GetPixelSize()*m_dwCreatedTextureWidth;
	}

    return true;
}

void CxeTexture::EndUpdate(DrawInfo *di)
{
	if(indirect)
	{
		u32 i;
		for(i=0;i<m_dwCreatedTextureHeight;++i)
		{
			memcpy(&((u8*)tex->base)[i*tex->wpitch],&((u8*)m_pTexture)[i*di->lPitch],di->lPitch);
		}
	}
	
	handle_small_surface(tex,tex->base);
	Xe_Surface_Unlock(xe,tex);
}

////////////////////////////////////////////////////////////////////////////////
// CxeColorCombiner
////////////////////////////////////////////////////////////////////////////////

CxeColorCombiner::CxeColorCombiner(CRender *pRender)
: CColorCombiner(pRender)
{
	m_pxeRender=(CxeRender*)pRender;
	m_pDecodedMux = new DecodedMuxForPixelShader;
	m_dwLastMux0 = m_dwLastMux1 = 0;
	m_lastIndex = -1;
}

CxeColorCombiner::~CxeColorCombiner()
{
    int size = m_vCompiledShaders.size();
    for (int i=0; i<size; i++)
    {
		// TODO: !!! destroy it
        m_vCompiledShaders[i].shader = 0;
    }

    m_vCompiledShaders.clear();
}

bool CxeColorCombiner::Initialize(void)
{
	return true;
}

void CxeColorCombiner::DisableCombiner(void)
{
	Xe_SetShader(xe,SHADER_TYPE_PIXEL,sh_ps_fb,0);
	m_lastIndex=-1;
}

void CxeColorCombiner::InitCombinerCycle12(void)
{
#ifdef _DEBUG
    if( debuggerDropCombiners )
    {
        UpdateCombiner(m_pDecodedMux->m_dwMux0,m_pDecodedMux->m_dwMux1);
        m_vCompiledShaders.clear();
        m_dwLastMux0 = m_dwLastMux1 = 0;
        debuggerDropCombiners = false;
    }
#endif

    bool combinerIsChanged = false;

    if( m_pDecodedMux->m_dwMux0 != m_dwLastMux0 || m_pDecodedMux->m_dwMux1 != m_dwLastMux1 || m_lastIndex < 0 )
    {
        combinerIsChanged = true;
        m_lastIndex = FindCompiledMux();
        if( m_lastIndex < 0 )       // Can not found
        {
            m_lastIndex = ParseDecodedMux();
        }

        m_dwLastMux0 = m_pDecodedMux->m_dwMux0;
        m_dwLastMux1 = m_pDecodedMux->m_dwMux1;
    }


//    GenerateCombinerSettingConstants(m_lastIndex);
    if( m_bCycleChanged || combinerIsChanged || gRDP.texturesAreReloaded || gRDP.colorsAreReloaded )
    {
        if( m_bCycleChanged || combinerIsChanged )
        {
            GenerateCombinerSettingConstants(m_lastIndex);
            GenerateCombinerSetting(m_lastIndex);
        }
        else if( gRDP.colorsAreReloaded )
        {
            GenerateCombinerSettingConstants(m_lastIndex);
        }

        m_pxeRender->SetAllTexelRepeatFlag();

        gRDP.colorsAreReloaded = false;
        gRDP.texturesAreReloaded = false;
    }
    else
    {
        m_pxeRender->SetAllTexelRepeatFlag();
    }
}

void CxeColorCombiner::InitCombinerCycleCopy(void)
{
	Xe_SetPixelShaderConstantB(xe,0,1); // texture
	DisableCombiner();
	
    m_pxeRender->EnableTexUnit(0,TRUE);
    CxeTexture* pTexture = g_textures[gRSP.curTile].m_pCxeTexture;
    if( pTexture )
    {
        m_pxeRender->BindTexture(pTexture, 0);
        m_pxeRender->SetTexelRepeatFlags(gRSP.curTile);
    }
#ifdef _DEBUG
    else
    {
        DebuggerAppendMsg("Check me, texture is NULL");
    }
#endif
}

void CxeColorCombiner::InitCombinerCycleFill(void)
{
	Xe_SetPixelShaderConstantB(xe,0,0); //no texture
	DisableCombiner();

    for( int i=0; i<m_supportedStages; i++ )
    {
        m_pxeRender->EnableTexUnit(i,FALSE);
    }
}

void CxeColorCombiner::InitCombinerBlenderForSimpleTextureDraw(uint32 tile)
{
	Xe_SetPixelShaderConstantB(xe,0,1); // texture
	DisableCombiner();

    if( g_textures[tile].m_pCxeTexture )
    {
        m_pxeRender->EnableTexUnit(0,TRUE);

		g_textures[tile].m_pCxeTexture->tex->use_filtering=1;
		g_textures[tile].m_pCxeTexture->tex->u_addressing=XE_TEXADDR_CLAMP;
		g_textures[tile].m_pCxeTexture->tex->v_addressing=XE_TEXADDR_CLAMP;

		Xe_SetTexture(xe,0,g_textures[tile].m_pCxeTexture->tex);
    }
    m_pxeRender->SetAllTexelRepeatFlag();
    m_pxeRender->SetAlphaTestEnable(FALSE);
}


void CxeColorCombiner::GenerateCombinerSettingConstants(int index)
{
	float frac = gRDP.LODFrac / 255.0f;
    float frac2 = gRDP.primLODFrac / 255.0f;

    float consts[7][4]=
	{
		{0,0,0,0},
		{1,1,1,1},
		{gRDP.fvPrimitiveColor[0],gRDP.fvPrimitiveColor[1],gRDP.fvPrimitiveColor[2],gRDP.fvPrimitiveColor[3]},
		{gRDP.fvEnvColor[0],gRDP.fvEnvColor[1],gRDP.fvEnvColor[2],gRDP.fvEnvColor[3]},
		{frac,frac,frac,frac},
		{frac2,frac2,frac2,frac2},
		{-1,-1,-1,-1},
	};
	
    Xe_SetPixelShaderConstantF(xe,0,(float*)consts,7);
}

struct muxToReg_s
{
	const char * rt;
	int rn;
	const char * sw;
};

#define CM1 6

#define RCB 4
#define RT0 5
#define RT1 6
#define RTP 7
#define RLAST 7 

struct muxToReg_s muxToReg_Map[][2] = 
{
	{{"c",0,"rgba"},	{"c",0,"aaaa"},		},  //MUX_0            
	{{"c",1,"rgba"},	{"c",1,"aaaa"},		},	//MUX_1            
	{{"r",RCB,"rgba"},	{"r",RCB,"aaaa"},	},	//MUX_COMBINED,    
	{{"r",RT0,"rgba"},	{"r",RT0,"aaaa"},	},	//MUX_TEXEL0,      
	{{"r",RT1,"rgba"},	{"r",RT1,"aaaa"},	},	//MUX_TEXEL1,      
	{{"c",2,"rgba"},	{"c",2,"aaaa"},		},	//MUX_PRIM,        
	{{"r",2,"rgba"},	{"r",2,"aaaa"},		},	//MUX_SHADE,       
	{{"c",3,"rgba"},	{"c",3,"aaaa"},		},	//MUX_ENV,         
	{{"r",RCB,"aaaa"},	{"r",RCB,"aaaa"},	},	//MUX_COMBALPHA,   
	{{"r",RT0,"aaaa"},	{"r",RT0,"aaaa"},	},	//MUX_T0_ALPHA,    
	{{"r",RT1,"aaaa"},	{"r",RT1,"aaaa"},	},	//MUX_T1_ALPHA,    
	{{"c",2,"aaaa"},	{"c",2,"aaaa"},		},	//MUX_PRIM_ALPHA,  
	{{"r",2,"aaaa"},	{"r",2,"aaaa"},		},	//MUX_SHADE_ALPHA, 
	{{"c",3,"aaaa"},	{"c",3,"aaaa"},		},	//MUX_ENV_ALPHA,   
	{{"c",4,"rgba"},	{"c",4,"aaaa"},		},	//MUX_LODFRAC,     
	{{"c",5,"rgba"},	{"c",5,"aaaa"},		},	//MUX_PRIMLODFRAC, 
	{{"c",1,"rgba"},	{"c",1,"aaaa"},		},	//MUX_K5           
	{{"c",1,"rgba"},	{"c",1,"aaaa"},		},	//MUX_UNK          
};


struct muxToReg_s * MuxToOC(uint8 val)
{
// For color channel
if( val&MUX_ALPHAREPLICATE )
    return &muxToReg_Map[val&0x1F][1];
else
    return &muxToReg_Map[val&0x1F][0];
}

struct muxToReg_s * MuxToOA(uint8 val)
{
// For alpha channel
return &muxToReg_Map[val&0x1F][0];
}

struct XenosShader * CxeColorCombiner::GenerateShader()
{
    DecodedMuxForPixelShader &mux = *(DecodedMuxForPixelShader*)m_pDecodedMux;

    mux.splitType[0] = mux.splitType[1] = mux.splitType[2] = mux.splitType[3] = CM_FMT_TYPE_NOT_CHECKED;
    m_pDecodedMux->Reformat(false);
	
	struct XemitShader * em = Xemit_Create(SHADER_TYPE_PIXEL,RLAST);
#if 1	
	Xemit_Op3(em,"tfetch2D","r",RT0,"r",0,"tf",0);
	Xemit_Op3(em,"tfetch2D","r",RT1,"r",1,"tf",1);

	Xemit_Op2(em,"mov","r",RCB,"c",0);

    for( int cycle=0; cycle<2; cycle++ )
    {
        for( int channel=0; channel<2; channel++)
        {
            struct muxToReg_s * (*func)(uint8) = channel==0?MuxToOC:MuxToOA;
            char *dst = channel==0?(char*)"rgb":(char*)"a";
            char *dst_sw = channel==0?(char*)"rgbb":(char*)"aaaa";
            N64CombinerType &m = mux.m_n64Combiners[cycle*2+channel];
					
			struct muxToReg_s * fa=func(m.a);
			struct muxToReg_s * fb=func(m.b);
			struct muxToReg_s * fc=func(m.c);
			struct muxToReg_s * fd=func(m.d);
			
			CombinerFormatType spl=mux.splitType[cycle*2+channel];
//			TRI(spl)
					
            switch( spl )
            {
            case CM_FMT_TYPE_NOT_USED:
                // nothing here...
                break;
            case CM_FMT_TYPE_D:
                Xemit_Op2Ex(em,"mov","r",RCB,dst,fd->rt,fd->rn,fd->sw);
                break;
            case CM_FMT_TYPE_A_MOD_C:
                Xemit_Op3Ex(em,"mul","r",RCB,dst,fa->rt,fa->rn,fa->sw,fc->rt,fc->rn,fc->sw);
                break;
            case CM_FMT_TYPE_A_ADD_D:
                Xemit_Op3Ex(em,"add_sat","r",RCB,dst,fa->rt,fa->rn,fa->sw,fd->rt,fd->rn,fd->sw);
                break;
            case CM_FMT_TYPE_A_SUB_B:
                // TODO: poor man sub, until I implement register modifiers
				Xemit_Op4Ex(em,"mad","r",RCB,dst,"c",CM1,fb->sw,fb->rt,fb->rn,fb->sw,fa->rt,fa->rn,fa->sw);
                break;
            case CM_FMT_TYPE_A_MOD_C_ADD_D:
                Xemit_Op4Ex(em,"mad_sat","r",RCB,dst,fa->rt,fa->rn,fa->sw,fc->rt,fc->rn,fc->sw,fd->rt,fd->rn,fd->sw);
                break;
            case CM_FMT_TYPE_A_LERP_B_C:
                // TODO: poor man sub, until I implement register modifiers
                Xemit_Op4Ex(em,"mad","r",RTP,dst,"c",CM1,fb->sw,fb->rt,fb->rn,fb->sw,fa->rt,fa->rn,fa->sw);
                Xemit_Op4Ex(em,"mad_sat","r",RCB,dst,"r",RTP,dst_sw,fc->rt,fc->rn,fc->sw,fb->rt,fb->rn,fb->sw);
                break;
			case CM_FMT_TYPE_A_SUB_B_ADD_D:
                // TODO: poor man sub, until I implement register modifiers
                Xemit_Op4Ex(em,"mad","r",RTP,dst,"c",CM1,fb->sw,fb->rt,fb->rn,fb->sw,fa->rt,fa->rn,fa->sw);
                Xemit_Op3Ex(em,"add_sat","r",RCB,dst,"r",RTP,dst_sw,fd->rt,fd->rn,fd->sw);
                break;
			case CM_FMT_TYPE_A_SUB_B_MOD_C:
                // TODO: poor man sub, until I implement register modifiers
                Xemit_Op4Ex(em,"mad","r",RTP,dst,"c",CM1,fb->sw,fb->rt,fb->rn,fb->sw,fa->rt,fa->rn,fa->sw);
                Xemit_Op3Ex(em,"mul","r",RCB,dst,"r",RTP,dst_sw,fc->rt,fc->rn,fc->sw);
                break;
			case CM_FMT_TYPE_A_ADD_B_MOD_C:
                Xemit_Op3Ex(em,"add_sat","r",RTP,dst,fa->rt,fa->rn,fa->sw,fb->rt,fb->rn,fb->sw);
                Xemit_Op3Ex(em,"mul","r",RCB,dst,"r",RTP,dst_sw,fc->rt,fc->rn,fc->sw);
                break;
			case CM_FMT_TYPE_A_B_C_D:
                // TODO: poor man sub, until I implement register modifiers
                Xemit_Op4Ex(em,"mad","r",RTP,dst,"c",CM1,fb->sw,fb->rt,fb->rn,fb->sw,fa->rt,fa->rn,fa->sw);
                Xemit_Op4Ex(em,"mad_sat","r",RCB,dst,"r",RTP,dst_sw,fc->rt,fc->rn,fc->sw,fd->rt,fd->rn,fd->sw);
                break;
            case CM_FMT_TYPE_A_B_C_A:
                // TODO: poor man sub, until I implement register modifiers
                Xemit_Op4Ex(em,"mad","r",RTP,dst,"c",CM1,fb->sw,fb->rt,fb->rn,fb->sw,fa->rt,fa->rn,fa->sw);
                Xemit_Op4Ex(em,"mad_sat","r",RCB,dst,"r",RTP,dst_sw,fc->rt,fc->rn,fc->sw,fa->rt,fa->rn,fa->sw);
                break;
            default:
				printf("[CxeColorCombiner] unhandled split type: %d ##########################\n",spl);
            }
        }
    }
	
	Xemit_Op2(em,"mov","oC",0,"r",RCB);
	
#else
	Xemit_Op3(em,"tfetch2D","r",RCB,"r",0,"tf",0);
	Xemit_Op3(em,"mul","oC",0,"r",RCB,"r",2);
#endif	
	
	struct XenosShader * shader=Xemit_LoadGeneratedShader(xe,em);
	Xe_InstantiateShader(xe,shader,0);
	
//	Xemit_Destroy(em);
	
#if 0
	char fn[100]="";
	sprintf(fn,"uda0:/sh%d.bin",m_vCompiledShaders.size());
	
	Xemit_DumpGeneratedShaderToFile(em,fn);
#endif	
	
	return shader;
}

int CxeColorCombiner::ParseDecodedMux()
{
    xeShaderCombinerSaveType res;
	
	res.shader=GenerateShader();
    res.dwMux0 = m_pDecodedMux->m_dwMux0;
    res.dwMux1 = m_pDecodedMux->m_dwMux1;
    res.fogIsUsed = gRDP.bFogEnableInBlender && gRSP.bFogEnabled;

    m_vCompiledShaders.push_back(res);
    m_lastIndex = m_vCompiledShaders.size()-1;

//	TRI(m_lastIndex);
	
    return m_lastIndex;
}

void CxeColorCombiner::GenerateCombinerSetting(int index)
{
	Xe_SetShader(xe,SHADER_TYPE_PIXEL,m_vCompiledShaders[index].shader,0);
}

int CxeColorCombiner::FindCompiledMux()
{
#ifdef _DEBUG
    if( debuggerDropCombiners )
    {
        m_vCompiledShaders.clear();
        //m_dwLastMux0 = m_dwLastMux1 = 0;
        debuggerDropCombiners = false;
    }
#endif
    for( uint32 i=0; i<m_vCompiledShaders.size(); i++ )
    {
        if( m_vCompiledShaders[i].dwMux0 == m_pDecodedMux->m_dwMux0 
            && m_vCompiledShaders[i].dwMux1 == m_pDecodedMux->m_dwMux1 
            && m_vCompiledShaders[i].fogIsUsed == (gRDP.bFogEnableInBlender && gRSP.bFogEnabled) )
            return (int)i;
    }

    return -1;
}
