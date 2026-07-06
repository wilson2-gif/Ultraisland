#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <dcomp.h>

// ─────────────────────────────────────────────────────────────────────────────
//  GlassSurface — chemin de rendu DirectComposition + ID2D1DeviceContext.
//
//  Pour une fenêtre WS_EX_NOREDIRECTIONBITMAP topmost (sans surface GDI), on
//  doit composer via DComp + flip-model swap-chain. C'est la voie moderne
//  recommandée pour l'alpha per-pixel propre + coins antialiasés + absence
//  de lag à 60 fps (le SetWindowRgn devient inutile : on clippe via géométrie).
//
//  Le `ID2D1DeviceContext*` exposé hérite de ID2D1RenderTarget — donc tout
//  le code Renderer (FillGeometry, DrawText, PushAxisAlignedClip, etc.)
//  marche tel quel via le cast.
// ─────────────────────────────────────────────────────────────────────────────

class GlassSurface
{
public:
    GlassSurface() = default;
    ~GlassSurface() { Release(); }

    bool Init(HWND hwnd, UINT w, UINT h, ID2D1Factory1* f);
    bool Resize(UINT w, UINT h);   // false si le device est perdu (swapchain cassé)
    void Release();

    // Encadrement d'une frame
    void Begin() { if (m_dc) { m_dc->BeginDraw(); m_dc->Clear(D2D1::ColorF(0,0,0,0)); } }
    HRESULT End()  { HRESULT hr = m_dc ? m_dc->EndDraw() : E_FAIL;
                     if (m_swap) m_swap->Present(1, 0);
                     if (m_comp) m_comp->Commit();
                     return hr; }

    ID2D1DeviceContext* DC() const { return m_dc; }

private:
    bool CreateTargetBitmap();

    ID3D11Device*          m_d3d    = nullptr;
    IDXGIDevice*           m_dxgi   = nullptr;
    ID2D1Device*           m_d2dDev = nullptr;
    ID2D1DeviceContext*    m_dc     = nullptr;
    IDXGISwapChain1*       m_swap   = nullptr;
    ID2D1Bitmap1*          m_target = nullptr;
    IDCompositionDevice*   m_comp   = nullptr;
    IDCompositionTarget*   m_ct     = nullptr;
    IDCompositionVisual*   m_visual = nullptr;
};
