#include "GlassSurface.h"
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dcomp.lib")

static void SafeRelease(IUnknown* p) { if (p) p->Release(); }

bool GlassSurface::Init(HWND hwnd, UINT w, UINT h, ID2D1Factory1* f)
{
    if (!hwnd || !f || w == 0 || h == 0) return false;
    Release();

    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL fls[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1
    };
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, fls, (UINT)_countof(fls),
            D3D11_SDK_VERSION, &m_d3d, &fl, nullptr))) return false;

    if (FAILED(m_d3d->QueryInterface(__uuidof(IDXGIDevice), (void**)&m_dxgi))) return false;
    if (FAILED(f->CreateDevice(m_dxgi, &m_d2dDev))) return false;
    if (FAILED(m_d2dDev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_dc)))
        return false;

    IDXGIAdapter* adapter = nullptr;
    if (FAILED(m_dxgi->GetAdapter(&adapter)) || !adapter) return false;
    IDXGIFactory2* dxgiFactory = nullptr;
    HRESULT hr = adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&dxgiFactory);
    adapter->Release();
    if (FAILED(hr) || !dxgiFactory) return false;

    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width            = w;
    scd.Height           = h;
    scd.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount      = 2;
    scd.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    scd.AlphaMode        = DXGI_ALPHA_MODE_PREMULTIPLIED;

    hr = dxgiFactory->CreateSwapChainForComposition(m_d3d, &scd, nullptr, &m_swap);
    dxgiFactory->Release();
    if (FAILED(hr)) return false;

    if (FAILED(DCompositionCreateDevice(m_dxgi, __uuidof(IDCompositionDevice),
            (void**)&m_comp))) return false;
    if (FAILED(m_comp->CreateTargetForHwnd(hwnd, TRUE, &m_ct))) return false;
    if (FAILED(m_comp->CreateVisual(&m_visual))) return false;
    m_visual->SetContent(m_swap);
    m_ct->SetRoot(m_visual);
    m_comp->Commit();

    return CreateTargetBitmap();
}

bool GlassSurface::CreateTargetBitmap()
{
    if (!m_dc || !m_swap) return false;
    IDXGISurface* surf = nullptr;
    if (FAILED(m_swap->GetBuffer(0, __uuidof(IDXGISurface), (void**)&surf))) return false;
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    HRESULT hr = m_dc->CreateBitmapFromDxgiSurface(surf, &bp, &m_target);
    surf->Release();
    if (FAILED(hr)) return false;
    m_dc->SetTarget(m_target);
    return true;
}

bool GlassSurface::Resize(UINT w, UINT h)
{
    if (!m_swap || w == 0 || h == 0) return false;
    if (m_dc) m_dc->SetTarget(nullptr);
    if (m_target) { m_target->Release(); m_target = nullptr; }
    // Vérifie le HRESULT : en cas de DXGI_ERROR_DEVICE_REMOVED/RESET (TDR, MAJ
    // pilote, bascule GPU), ResizeBuffers échoue. On NE recrée PAS un bitmap sur
    // un swapchain cassé (GetBuffer renverrait une surface invalide → deref/plantage).
    // La cible reste nulle ; le prochain EndDraw renverra une erreur que le Renderer
    // traite (D2DERR_RECREATE_TARGET) et l'île reste stable en attendant.
    HRESULT hr = m_swap->ResizeBuffers(2, w, h, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
    if (FAILED(hr)) return false;
    return CreateTargetBitmap();
}

void GlassSurface::Release()
{
    if (m_dc) m_dc->SetTarget(nullptr);
    SafeRelease(m_target);  m_target = nullptr;
    SafeRelease(m_visual);  m_visual = nullptr;
    SafeRelease(m_ct);      m_ct     = nullptr;
    SafeRelease(m_comp);    m_comp   = nullptr;
    SafeRelease(m_swap);    m_swap   = nullptr;
    SafeRelease(m_dc);      m_dc     = nullptr;
    SafeRelease(m_d2dDev);  m_d2dDev = nullptr;
    SafeRelease(m_dxgi);    m_dxgi   = nullptr;
    SafeRelease(m_d3d);     m_d3d    = nullptr;
}
