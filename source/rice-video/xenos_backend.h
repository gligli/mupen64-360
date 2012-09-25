#pragma once

#include <time/time.h>
#include <ppc/timebase.h>
#include <xenos/xe.h>
#include <debug.h>

#include "GraphicsContext.h"
#include "Render.h"
#include "Combiner.h"
#include "Blender.h"
#include "TextureManager.h"

#define xe_GetTicks() (mftb()/(PPC_TIMEBASE_FREQ/(u64)1000))
#define usleep(x) udelay(x)

class CxeGraphicsContext : public CGraphicsContext
{
    friend class CxeRender;
    friend class CxeRenderTexture;
public:
    virtual ~CxeGraphicsContext();

    bool Initialize(uint32 dwWidth, uint32 dwHeight, BOOL bWindowed );
    void CleanUp();
    void Clear(ClearFlag dwFlags, uint32 color=0xFF000000, float depth=1.0f);

    void UpdateFrame(bool swaponly=false);
    int ToggleFullscreen();     // return 0 as the result is windowed

    bool IsExtensionSupported(const char* pExtName);
    bool IsWglExtensionSupported(const char* pExtName);
    static void InitDeviceParameters();

    //Get methods (TODO, remove all friend class and use get methods instead)
    bool IsSupportAnisotropicFiltering();
    int  getMaxAnisotropicFiltering();

protected:
    friend class CxeDeviceBuilder;
    CxeGraphicsContext();
    void InitState(void);
    void InitOGLExtension(void);
    bool SetFullscreenMode();
    bool SetWindowMode();
};

class CxeRender : public CRender
{
    friend class CxeColorCombiner;
    friend class CxeBlender;
    friend class CxeDeviceBuilder;
    
protected:
    CxeRender();

public:
    ~CxeRender();
    void Initialize(void);

    bool InitDeviceObjects();
    bool ClearDeviceObjects();

    void ApplyTextureFilter();

    void SetShadeMode(RenderShadeMode mode);
    void ZBufferEnable(BOOL bZBuffer);
    void ClearBuffer(bool cbuffer, bool zbuffer);
    void ClearZBuffer(float depth);
    void SetZCompare(BOOL bZCompare);
    void SetZUpdate(BOOL bZUpdate);
    void SetZBias(int bias);
    void ApplyZBias(int bias);
    void SetAlphaRef(uint32 dwAlpha);
    void ForceAlphaRef(uint32 dwAlpha);
    void SetFillMode(FillMode mode);
    void SetViewportRender();
    void RenderReset();
    void SetCullMode(bool bCullFront, bool bCullBack);
    void SetAlphaTestEnable(BOOL bAlphaTestEnable);
    void UpdateScissor();
    void ApplyRDPScissor(bool force=false);
    void ApplyScissorWithClipRatio(bool force=false);

    bool SetCurrentTexture(int tile, CTexture *handler,uint32 dwTileWidth, uint32 dwTileHeight, TxtrCacheEntry *pTextureEntry);
    bool SetCurrentTexture(int tile, TxtrCacheEntry *pTextureEntry);
    void SetAddressUAllStages(uint32 dwTile, TextureUVFlag dwFlag);
    void SetAddressVAllStages(uint32 dwTile, TextureUVFlag dwFlag);
    void SetTextureUFlag(TextureUVFlag dwFlag, uint32 tile);
    void SetTextureVFlag(TextureUVFlag dwFlag, uint32 tile);
    virtual void BindTexture(struct XenosSurface *tex, int unitno);
    virtual void DisBindTexture(struct XenosSurface *tex, int unitno);
    virtual void TexCoord2f(float u, float v);
    virtual void TexCoord(TLITVERTEX &vtxInfo);

    void DrawSimple2DTexture(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, COLOR dif, COLOR spe, float z, float rhw);
    void DrawSimpleRect(int nX0, int nY0, int nX1, int nY1, uint32 dwColor, float depth, float rhw);
    void InitCombinerBlenderForSimpleRectDraw(uint32 tile=0);
    void DrawSpriteR_Render();
    void DrawObjBGCopy(uObjBg &info);
    void DrawText(const char* str, RECT *rect);

    void SetFogMinMax(float fMin, float fMax);
    void SetFogEnable(bool bEnable);
    void TurnFogOnOff(bool flag);
    void SetFogColor(uint32 r, uint32 g, uint32 b, uint32 a);

    void DisableMultiTexture();
    void EnableMultiTexture() {m_bEnableMultiTexture=true;}
    void EndRendering(void);

    void glViewportWrapper(int x, int y, int width, int height, bool ortho);
    virtual void EnableTexUnit(int unitno, BOOL flag);

protected:
    COLOR PostProcessDiffuseColor(COLOR curDiffuseColor);
    COLOR PostProcessSpecularColor();

    // Basic render drawing functions
    bool RenderFlushTris();
    bool RenderTexRect();
    bool RenderFillRect(uint32 dwColor, float depth);
    bool RenderLine3D();

    bool m_bSupportFogCoordExt;
    bool m_bMultiTexture;
    bool m_bSupportClampToEdge;

    struct XenosSurface * m_curBoundTex[8];
    BOOL    m_texUnitEnabled[8];

    bool m_bEnableMultiTexture;
	
	int xe_cull;
};

class CxeColorCombiner : public CColorCombiner
{
public:
    bool Initialize(void);
    void InitCombinerBlenderForSimpleTextureDraw(uint32 tile=0);
protected:
    friend class CxeDeviceBuilder;

    void DisableCombiner(void);
    void InitCombinerCycleCopy(void);
    void InitCombinerCycleFill(void);
    void InitCombinerCycle12(void);

    CxeColorCombiner(CRender *pRender);
    ~CxeColorCombiner();

    CxeRender *m_pxeRender;
};

class CxeBlender : public CBlender
{
public:
    void NormalAlphaBlender(void);
    void DisableAlphaBlender(void);
    void BlendFunc(uint32 srcFunc, uint32 desFunc);
    void Enable();
    void Disable();

protected:
    friend class CxeDeviceBuilder;
    CxeBlender(CRender *pRender) : CBlender(pRender),blend_src(XE_BLEND_ONE),blend_dst(XE_BLEND_ZERO) {};
    ~CxeBlender() {};

	int blend_src;
	int blend_dst;
};

class CxeTexture : public CTexture
{
    friend class CxeRenderTexture;
public:
    ~CxeTexture();

    bool StartUpdate(DrawInfo *di);
    void EndUpdate(DrawInfo *di);

    struct XenosSurface * tex;
protected:
    friend class CxeDeviceBuilder;
    CxeTexture(uint32 dwWidth, uint32 dwHeight, TextureUsage usage = AS_NORMAL);
};
