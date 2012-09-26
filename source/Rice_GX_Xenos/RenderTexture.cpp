/*
Rice_GX - RenderTexture.cpp
Copyright (C) 2005 Rice1964
Copyright (C) 2010, 2011, 2012 sepp256 (Port to Wii/Gamecube/PS3)
Wii64 homepage: http://www.emulatemii.com
email address: sepp256@gmail.com

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

#if !defined(__GX__) && !defined(XENON)
#include <SDL_opengl.h>
#endif //!__GX__

#include "stdafx.h"

// ===========================================================================
COGLRenderTexture::COGLRenderTexture(int width, int height, RenderTextureInfo* pInfo, TextureUsage usage)
    :   CRenderTexture(width, height, pInfo, usage),
#ifndef XENON
        m_pOGLTexture(NULL)
#else
        m_pxeTexture(NULL)
#endif
{
    if( usage == AS_BACK_BUFFER_SAVE )
    {
#ifndef XENON
        m_pTexture = m_pOGLTexture = new COGLTexture(width, height, usage);
#else
        m_pTexture = m_pxeTexture = new CxeTexture(width, height, usage);
#endif		
        if( !m_pTexture )
        {
            TRACE0("Error to create OGL render_texture");
            SAFE_DELETE(m_pTexture);
        }
    }

    m_width = width;
    m_height = height;
    m_beingRendered = false;
}

COGLRenderTexture::~COGLRenderTexture()
{
    if( m_beingRendered )
    {
        g_pFrameBufferManager->CloseRenderTexture(false);
        SetAsRenderTarget(false);
    }

    ShutdownPBuffer();
    SAFE_DELETE(m_pTexture);
#ifndef XENON
    m_pOGLTexture = NULL;
#else
    m_pxeTexture = NULL;
#endif
    m_beingRendered = false;
}

bool COGLRenderTexture::InitPBuffer( void )
{
    return true;
}

void COGLRenderTexture::ShutdownPBuffer(void)
{
}

bool COGLRenderTexture::SetAsRenderTarget(bool enable)
{
   return true;
}

void COGLRenderTexture::LoadTexture(TxtrCacheEntry* pEntry)
{
}

void COGLRenderTexture::StoreToRDRAM(int infoIdx)
{
}

#if 0//def __GX__
CGXRenderTexture::CGXRenderTexture(int width, int height, RenderTextureInfo* pInfo, TextureUsage usage)
: CRenderTexture(width, height, pInfo, usage)
{
//	m_pTexture = new CDirectXTexture(width, height, usage);
	m_pTexture = new COGLTexture(width, height, usage);
	if( m_pTexture )
	{
		m_width = width;
		m_height = height;
	}
	else
	{
		TRACE0("Error to create DX render_texture");
		SAFE_DELETE(m_pTexture);
	}

	m_pColorBufferSave = NULL;
	m_pDepthBufferSave = NULL;
	m_beingRendered = false;
}

CGXRenderTexture::~CGXRenderTexture()
{
	if( m_beingRendered )
	{
		g_pFrameBufferManager->CloseRenderTexture(false);
		SetAsRenderTarget(false);
	}

	SAFE_DELETE(m_pTexture);

	m_beingRendered = false;
}

bool CGXRenderTexture::SetAsRenderTarget(bool enable)
{
	if( m_usage != AS_RENDER_TARGET )	return false;

#if 0
	//This function saves a pointer to the current back color/depth buffer and points the device context 
	//to the color buffer in m_pTexture
	if( enable )
	{
		if( !m_beingRendered )
		{
			if(m_pTexture )
			{
				MYLPDIRECT3DSURFACE pColorBuffer;

				// save the current back buffer
				g_pD3DDev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &m_pColorBufferSave);
				g_pD3DDev->GetDepthStencilSurface(&m_pDepthBufferSave);

				// Activate the render_texture
				(MYLPDIRECT3DTEXTURE(m_pTexture->GetTexture()))->GetSurfaceLevel(0,&pColorBuffer);
				HRESULT res = g_pD3DDev->SetRenderTarget(0, pColorBuffer);
				SAFE_RELEASE(pColorBuffer);
				if( res != S_OK )
				{
					return false;
				}

				m_beingRendered = true;
				return true;
			}
			else
			{
				return false;
			}
		}
		else
		{
			return true;
		}
	}
	else
	{
		if( m_beingRendered )
		{
			if( m_pColorBufferSave && m_pDepthBufferSave )
			{
				g_pD3DDev->SetRenderTarget(0, m_pColorBufferSave);
				m_beingRendered = false;
				SAFE_RELEASE(m_pColorBufferSave);
				SAFE_RELEASE(m_pDepthBufferSave);
				return true;
			}
			else
			{
				return false;
			}
		}
		else
		{
			return true;
		}
	}
#endif
}

void CGXRenderTexture::LoadTexture(TxtrCacheEntry* pEntry)
{
#if 0
	//This function copies the current color buffer (m_pTexture for this RenderTexture) to pEntry->pTexture
	bool IsBeingRendered = m_beingRendered;
	if( IsBeingRendered )
	{
		TXTRBUF_DUMP(TRACE0("Warning, loading from render_texture while it is being rendered"));

		SetAsRenderTarget(false);
		//return;
	}

	CTexture *pSurf = pEntry->pTexture;
	TxtrInfo &ti = pEntry->ti;

	// Need to load texture from the saved back buffer

	MYLPDIRECT3DTEXTURE pNewTexture = MYLPDIRECT3DTEXTURE(pSurf->GetTexture());
	MYLPDIRECT3DSURFACE pNewSurface = NULL;
	HRESULT res = pNewTexture->GetSurfaceLevel(0,&pNewSurface);
	MYLPDIRECT3DSURFACE pSourceSurface = NULL;
	(MYLPDIRECT3DTEXTURE(m_pTexture->GetTexture()))->GetSurfaceLevel(0,&pSourceSurface);

	int left = (pEntry->ti.Address- m_pInfo->CI_Info.dwAddr )%m_pInfo->CI_Info.bpl + pEntry->ti.LeftToLoad;
	int top = (pEntry->ti.Address- m_pInfo->CI_Info.dwAddr)/m_pInfo->CI_Info.bpl + pEntry->ti.TopToLoad;
	RECT srcrect = {uint32(left*m_pInfo->scaleX) ,uint32(top*m_pInfo->scaleY), 
		uint32(min(m_width, left+(int)ti.WidthToLoad)*m_pInfo->scaleX), 
		uint32(min(m_height,top+(int)ti.HeightToLoad)*m_pInfo->scaleY) };

	if( pNewSurface != NULL && pSourceSurface != NULL )
	{
		if( left < m_width && top<m_height )
		{
			RECT dstrect = {0,0,ti.WidthToLoad,ti.HeightToLoad};
			HRESULT res = D3DXLoadSurfaceFromSurface(pNewSurface,NULL,&dstrect,pSourceSurface,NULL,&srcrect,D3DX_FILTER_POINT ,0xFF000000);
			DEBUGGER_IF_DUMP(( res != S_OK), {DebuggerAppendMsg("Error to reload texture from render_texture, res=%x", res);} );
		}
	}

	if( IsBeingRendered )
	{
		SetAsRenderTarget(true);
	}

	pSurf->SetOthersVariables();
	SAFE_RELEASE(pSourceSurface);
	TXTRBUF_DETAIL_DUMP(DebuggerAppendMsg("Load texture from render_texture"););
#endif
}

void CGXRenderTexture::StoreToRDRAM(int infoIdx)
{
	if( !frameBufferOptions.bRenderTextureWriteBack )	return;

#if 0
	RenderTextureInfo &info = gRenderTextureInfos[infoIdx];
	DXFrameBufferManager &FBmgr = *(DXFrameBufferManager*)g_pFrameBufferManager;

	uint32 fmt = info.CI_Info.dwFormat;

	MYLPDIRECT3DSURFACE pSourceSurface = NULL;
	(MYLPDIRECT3DTEXTURE(m_pTexture->GetTexture()))->GetSurfaceLevel(0,&pSourceSurface);

	// Ok, we are using texture render target right now
	// Need to copy content from the texture render target back to frame buffer
	// then reset the current render target

	// Here we need to copy the content from the texture frame buffer to RDRAM memory

	TXTRBUF_DUMP(TRACE2("Saving TextureBuffer %d to N64 RDRAM addr=%08X", infoIdx, info.CI_Info.dwAddr));

	if( pSourceSurface )
	{
		uint32 width, height, bufWidth, bufHeight, memsize; 
		width = info.N64Width;
		height = info.N64Height;
		bufWidth = info.bufferWidth;
		bufHeight = info.bufferHeight;
		if( info.CI_Info.dwSize == TXT_SIZE_8b && fmt == TXT_FMT_CI )
		{
			info.CI_Info.dwFormat = TXT_FMT_I;
			height = info.knownHeight ? info.N64Height : info.maxUsedHeight;
			memsize = info.N64Width*height;
			FBmgr.CopyD3DSurfaceToRDRAM(info.CI_Info.dwAddr, fmt, info.CI_Info.dwSize, width, height,
				bufWidth, bufHeight, info.CI_Info.dwAddr, memsize, info.N64Width, D3DFMT_A8R8G8B8, pSourceSurface);
			info.CI_Info.dwFormat = TXT_FMT_CI;
		}
		else
		{
			if( info.CI_Info.dwSize == TXT_SIZE_8b )
			{
				height = info.knownHeight ? info.N64Height : info.maxUsedHeight;
				memsize = info.N64Width*height;
				FBmgr.CopyD3DSurfaceToRDRAM(info.CI_Info.dwAddr, fmt, info.CI_Info.dwSize, width, height,
					bufWidth, bufHeight, info.CI_Info.dwAddr, memsize, info.N64Width, D3DFMT_A8R8G8B8, pSourceSurface);
			}
			else
			{
				height = info.knownHeight ? info.N64Height : info.maxUsedHeight;
				memsize = g_pRenderTextureInfo->N64Width*height*2;
				FBmgr.CopyD3DSurfaceToRDRAM(info.CI_Info.dwAddr, fmt, info.CI_Info.dwSize, width, height,
					bufWidth, bufHeight, info.CI_Info.dwAddr, memsize, info.N64Width, D3DFMT_X8R8G8B8, pSourceSurface);
			}
		}
		TXTRBUF_DUMP(TRACE2("Write back: width=%d, height=%d", width, height));	

		SAFE_RELEASE(pSourceSurface);
	}
	else
	{
		TRACE0("Error, cannot lock the render_texture");
	}
#endif
}
#endif //__GX__