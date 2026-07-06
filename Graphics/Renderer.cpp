#include "Renderer.h"
#include <d2d1effects.h>
#include <cmath>
#include <algorithm>
#pragma comment(lib, "dxguid.lib")
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ── Palette Apple Dynamic Island ─────────────────────────────────────────────
namespace K {
    const D2D1_COLOR_F BG    = {0.0f, 0.0f, 0.0f, 1.0f};
    // P1-Apple : blanc légèrement chaud, pas pur blanc
    const D2D1_COLOR_F WHITE = {0.960f, 0.960f, 0.972f, 1.00f};
    const D2D1_COLOR_F GR    = {0.420f, 0.430f, 0.480f, 1.00f};
    const D2D1_COLOR_F GR2   = {0.620f, 0.635f, 0.680f, 1.00f};
    // Apple green (SF Symbol fill)
    const D2D1_COLOR_F GRN   = {0.196f, 0.845f, 0.400f, 1.00f};
    const D2D1_COLOR_F BLU   = {0.235f, 0.600f, 1.000f, 1.00f};
    const D2D1_COLOR_F ORG   = {1.000f, 0.455f, 0.165f, 1.00f};
    const D2D1_COLOR_F RED   = {1.000f, 0.245f, 0.220f, 1.00f};
    const D2D1_COLOR_F PUR   = {0.620f, 0.365f, 1.000f, 1.00f};
    // P1 : couleur HUD — iOS utilise un gris clair sur fond presque noir
    const D2D1_COLOR_F HUD_FILL = {0.88f, 0.88f, 0.90f, 1.0f};
}

static float Lf(float a, float b, float t) { return a + (b-a)*t; }
static float SpEase(float t) {
    if(t<=0) return 0; if(t>=1) return 1;
    const float c = (float)(2.0*M_PI/3.0);
    return powf(2.f,-9.f*t)*sinf((t*11.f-.8f)*c)+1.f;
}
static float CuEase(float t) { float u=1-t; return 1-u*u*u; }

Renderer::Renderer()  {}
Renderer::~Renderer() { Release(); }

bool Renderer::Initialize(HWND hwnd)
{
    if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory1),(void**)&m_f))) return false;
    if(FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory3),(IUnknown**)&m_dw))) return false;

    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&m_wicFactory)))) { /* ignoré — album art désactivé */ }

    // ── Polices — P1 Apple : Segoe UI Variable Display partout
    // "Segoe UI Variable Display" est le plus proche de SF Pro sur Windows
    auto MF=[&](const wchar_t* face, float sz, DWRITE_FONT_WEIGHT w, IDWriteTextFormat** o){
        m_dw->CreateTextFormat(face, nullptr, w,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, sz, L"", o);
        if(*o){
            (*o)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            (*o)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    };

    // Corps texte principal
    MF(L"Segoe UI Variable Display", 14.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &m_fT);
    MF(L"Segoe UI Variable Display", 17.f, DWRITE_FONT_WEIGHT_BOLD,      &m_fTBig);
    MF(L"Segoe UI Variable Text",    12.f, DWRITE_FONT_WEIGHT_NORMAL,    &m_fS);
    MF(L"Segoe UI Variable Text",    10.f, DWRITE_FONT_WEIGHT_NORMAL,    &m_fX);
    MF(L"Segoe UI Variable Display", 13.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &m_fTSemi);

    // P1-APPLE : heure idle → Segoe UI Variable Display Light
    // Beaucoup plus proche de SF Pro Display Light qu'utilisé par Apple pour l'heure
    // Police du pill idle — pilotée par PillRT (Phase B : famille/graisse/scale)
    static const wchar_t* FAM[4] = {
        L"Segoe UI Variable Display", L"Inter", L"Manrope", L"IBM Plex Sans"
    };
    DWRITE_FONT_WEIGHT WTS[3] = {
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_WEIGHT_BOLD
    };
    int fi = (PillRT::FONT_FAM  >= 0 && PillRT::FONT_FAM < 4) ? PillRT::FONT_FAM : 0;
    int wi = (PillRT::FONT_WT   >= 0 && PillRT::FONT_WT  < 3) ? PillRT::FONT_WT  : 1;
    MF(FAM[fi], 13.f * PillRT::FONT_SCALE, WTS[wi], &m_fC);
    if(m_fC){
        m_fC->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_fC->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_fC->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    // Chiffre HUD (grand) — Apple: SF Rounded Regular très grand
    // Segoe UI Variable Display Thin ≈ SF Pro Thin
    MF(L"Segoe UI Variable Display", 26.f, DWRITE_FONT_WEIGHT_THIN, &m_fH);

    // Icônes MDL2
    MF(L"Segoe MDL2 Assets", 13.f, DWRITE_FONT_WEIGHT_NORMAL, &m_fI14);
    MF(L"Segoe MDL2 Assets", 20.f, DWRITE_FONT_WEIGHT_NORMAL, &m_fI20);

    if(m_fX) m_fX->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);

    RECT rc; GetClientRect(hwnd, &rc);
    UINT w = (UINT)std::max(1L, rc.right - rc.left);
    UINT h = (UINT)std::max(1L, rc.bottom - rc.top);
    if (!m_glass.Init(hwnd, w, h, m_f)) return false;
    m_rt = m_glass.DC();   // ID2D1DeviceContext hérite de ID2D1RenderTarget
    return CreateDevRes();
}

bool Renderer::CreateDevRes() {
    if(!m_rt) return false;
    m_rt->CreateSolidColorBrush(K::BG,    &m_b0);
    m_rt->CreateSolidColorBrush(K::WHITE, &m_b1);

    // Texture de grain 64×64 (blanc, alpha aléatoire faible) — tuile en wrap
    {
        const UINT S = 64;
        std::vector<UINT32> px(S * S);
        unsigned seed = 0x9E3779B9u;
        for (auto& p : px) {
            seed = seed * 1664525u + 1013904223u;          // LCG déterministe
            UINT32 a = (seed >> 24) % 26;                   // alpha 0..25
            p = (a << 24) | 0x00FFFFFF;                     // BGRA blanc premult-ok
        }
        ID2D1Bitmap* bmp = nullptr;
        D2D1_BITMAP_PROPERTIES bp = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (SUCCEEDED(m_rt->CreateBitmap(D2D1::SizeU(S, S), px.data(), S*4, bp, &bmp)) && bmp) {
            D2D1_BITMAP_BRUSH_PROPERTIES bbp = D2D1::BitmapBrushProperties(
                D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP,
                D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
            m_rt->CreateBitmapBrush(bmp, bbp, &m_bGrain);
            bmp->Release();
        }
    }

    RebuildGrads();
    return m_b0 != nullptr;
}

void Renderer::RebuildGrads() {
    auto SR=[](auto& p){ if(p){p->Release();p=nullptr;} };
    SR(m_bGlass); SR(m_bSd); SR(m_bGl); SR(m_bBorder);

    auto GS=[&](D2D1_GRADIENT_STOP* s, int n, ID2D1GradientStopCollection** c){
        m_rt->CreateGradientStopCollection(s, n, c);
    };
    {   // VERRE FROID translucide (plus de blanc au coin bas-droit).
        // Stops : haut = highlight verre très discret, milieu ardoise faible,
        // bas = teinte froide assombrie (ne devient JAMAIS blanche).
        D2D1_GRADIENT_STOP s[]={
            {0.00f, {0.62f, 0.66f, 0.78f, 0.045f}},
            {0.55f, {0.30f, 0.33f, 0.42f, 0.028f}},
            {1.00f, {0.10f, 0.12f, 0.18f, 0.100f}},
        };
        ID2D1GradientStopCollection*c=nullptr; GS(s,3,&c);
        if(c){ m_rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0,0},{0,1}),c,&m_bGlass); c->Release(); }
    }
    {   D2D1_GRADIENT_STOP s[]={{0,{0,0,0,0}},{1,{0,0,0,0.22f}}};
        ID2D1GradientStopCollection*c=nullptr; GS(s,2,&c);
        if(c){ m_rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0,0},{0,1}),c,&m_bSd); c->Release(); }
    }
    {   D2D1_GRADIENT_STOP s[]={{0,{1,1,1,0.06f}},{1,{1,1,1,0}}};
        ID2D1GradientStopCollection*c=nullptr; GS(s,2,&c);
        if(c){ m_rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties({210,0},{0,0},260,55),c,&m_bGl); c->Release(); }
    }
    {   // P1 : bordure réflexion plus subtile — Apple ne fait pas de glow fort
        D2D1_GRADIENT_STOP s[]={{0,{1,1,1,0.32f}},{0.5f,{1,1,1,0.08f}},{1,{1,1,1,0.0f}}};
        ID2D1GradientStopCollection*c=nullptr; GS(s,3,&c);
        if(c){ m_rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0,0},{1,1}),c,&m_bBorder); c->Release(); }
    }
}

void Renderer::DropDevRes() {
    auto SR=[](auto& p){ if(p){p->Release();p=nullptr;} };
    SR(m_b0); SR(m_b1); SR(m_bGlass); SR(m_bSd); SR(m_bGl); SR(m_bBorder);
    SR(m_bGrain);
    SR(m_fxBlur);
    SR(m_pillGeometry);
}

void Renderer::Release() {
    DropDevRes();
    auto SR=[](auto& p){ if(p){p->Release();p=nullptr;} };
    SR(m_fT); SR(m_fS); SR(m_fX); SR(m_fC); SR(m_fH);
    SR(m_fI14); SR(m_fI20); SR(m_fTBig); SR(m_fTSemi);
    // m_rt n'est pas relâché ici : c'est un cast sur GlassSurface::DC()
    m_rt = nullptr;
    m_glass.Release();
    SR(m_dw); SR(m_f);
    SR(m_albumArt); SR(m_wicFactory);
}

void Renderer::UpdateAlbumArt(const std::vector<uint8_t>& data) {
    if(data.empty()){
        if(m_albumArt){ m_albumArt->Release(); m_albumArt=nullptr; }
        m_currentThumbnailData.clear();
        m_albumAccent = {0.40f, 0.42f, 0.50f, 1.f};   // accent neutre
        return;
    }
    if(data==m_currentThumbnailData && m_albumArt) return;
    if(m_albumArt){ m_albumArt->Release(); m_albumArt=nullptr; }
    m_currentThumbnailData = data;
    if(!m_wicFactory||!m_rt) return;

    IWICStream* stream=nullptr;
    if(SUCCEEDED(m_wicFactory->CreateStream(&stream))){
        if(SUCCEEDED(stream->InitializeFromMemory(
                const_cast<uint8_t*>(data.data()), (DWORD)data.size()))){
            IWICBitmapDecoder* dec=nullptr;
            if(SUCCEEDED(m_wicFactory->CreateDecoderFromStream(
                    stream, nullptr, WICDecodeMetadataCacheOnLoad, &dec))){
                IWICBitmapFrameDecode* frame=nullptr;
                if(SUCCEEDED(dec->GetFrame(0,&frame))){
                    IWICFormatConverter* conv=nullptr;
                    if(SUCCEEDED(m_wicFactory->CreateFormatConverter(&conv))){
                        if(SUCCEEDED(conv->Initialize(frame,
                                GUID_WICPixelFormat32bppPBGRA,
                                WICBitmapDitherTypeNone, nullptr, 0.f,
                                WICBitmapPaletteTypeMedianCut))){
                            m_rt->CreateBitmapFromWicBitmap(conv, nullptr, &m_albumArt);
                            ComputeAlbumAccent(conv);   // couleur d'accent dérivée de la pochette
                        }
                        conv->Release();
                    }
                    frame->Release();
                }
                dec->Release();
            }
        }
        stream->Release();
    }
}

// Couleur d'accent vibrante dérivée de la pochette : on réduit la pochette à 12×12,
// on prend le pixel le plus saturé (le plus « vivant »), puis on normalise/booste.
void Renderer::ComputeAlbumAccent(IWICBitmapSource* src) {
    m_albumAccent = {0.40f, 0.42f, 0.50f, 1.f};
    if(!src || !m_wicFactory) return;
    IWICBitmapScaler* sc=nullptr;
    if(FAILED(m_wicFactory->CreateBitmapScaler(&sc)) || !sc) return;
    const UINT S=12;
    if(SUCCEEDED(sc->Initialize(src, S, S, WICBitmapInterpolationModeFant))){
        std::vector<BYTE> buf((size_t)S*S*4);
        WICRect rc={0,0,(INT)S,(INT)S};
        if(SUCCEEDED(sc->CopyPixels(&rc, S*4, (UINT)buf.size(), buf.data()))){
            float bestSat=-1.f, br=0.5f, bg=0.5f, bb=0.5f;     // pixel le plus saturé
            for(UINT i=0;i<S*S;++i){
                float b=buf[i*4]/255.f, g=buf[i*4+1]/255.f, r=buf[i*4+2]/255.f;  // PBGRA
                float mx=(std::max)(r,(std::max)(g,b)), mn=(std::min)(r,(std::min)(g,b));
                float sat=mx-mn;
                if(sat>bestSat && mx>0.18f){ bestSat=sat; br=r; bg=g; bb=b; }
            }
            float r=br,g=bg,b=bb, mx=(std::max)(r,(std::max)(g,b));
            if(mx>0.04f){
                float k=0.92f/mx; r*=k; g*=k; b*=k;            // normalise la luminosité
                float mean=(r+g+b)/3.f;                         // booste la saturation
                r=std::clamp(mean+(r-mean)*1.55f,0.f,1.f);
                g=std::clamp(mean+(g-mean)*1.55f,0.f,1.f);
                b=std::clamp(mean+(b-mean)*1.55f,0.f,1.f);
                m_albumAccent={r,g,b,1.f};
            }
        }
    }
    sc->Release();
}

void Renderer::Resize(UINT w, UINT h) {
    DropDevRes();
    m_rt = nullptr;
    // Si le device est perdu, m_glass.Resize renvoie false : on laisse m_rt à null
    // pour que Draw() sorte proprement (early-return) au lieu de dessiner sur une
    // cible cassée. Le prochain Resize réussi (ou une reconstruction) rétablit tout.
    if (!m_glass.Resize(w, h)) return;
    m_rt = m_glass.DC();
    CreateDevRes();
}

// DPI Per-Monitor V2 : SetDpi sur le DeviceContext → toute la géométrie de dessin
// (Draw*, coords en dur) reste en unités LOGIQUES ; D2D scale pour matcher les
// pixels physiques du swapchain. Résultat = TEXTE ET FORMES parfaitement nets à
// 125/150 %.
void Renderer::SetDpi(float dpi) {
    if(m_rt) m_rt->SetDpi(dpi, dpi);
}
void Renderer::SetB0(D2D1_COLOR_F c, float a) { if(m_b0){ c.a*=a; m_b0->SetColor(c); } }
void Renderer::SetB1(D2D1_COLOR_F c, float a) { if(m_b1){ c.a*=a; m_b1->SetColor(c); } }

// ─────────────────────────────────────────────────────────────────────────────
//  Slide clip helpers
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::PushSlideClip(float px, float pw, float ph, float slideX) {
    m_rt->PushAxisAlignedClip({px,0.f,px+pw,ph}, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if(fabsf(slideX) > 0.5f)
        m_rt->SetTransform(D2D1::Matrix3x2F::Translation(slideX, 0.f));
}
void Renderer::PopSlideClip(float slideX) {
    if(fabsf(slideX) > 0.5f)
        m_rt->SetTransform(D2D1::Matrix3x2F::Identity());
    m_rt->PopAxisAlignedClip();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Draw — point d'entrée
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::Draw(const IslandContent& c) {
    if(!m_rt) return;
    if(!m_b0){ CreateDevRes(); if(!m_b0) return; }

    m_glass.Begin();   // BeginDraw + Clear sur le DeviceContext (puis Present+Commit dans End)
    D2D1_SIZE_F sz = m_rt->GetSize();
    float pw=c.pillW, ph=c.pillH, px=PillX(sz.width, pw);   // position G/C/D runtime
    // Accent : adaptatif (pochette) ou fixe (palette Cockpit)
    m_accent = PillRT::ADAPTIVE ? m_albumAccent
             : D2D1_COLOR_F{PillRT::ACC_R, PillRT::ACC_G, PillRT::ACC_B, 1.f};

    DrawLiquidPill(px, pw, ph, c);

    if(m_pillGeometry){
        ID2D1Layer* layer=nullptr;
        m_rt->CreateLayer(nullptr, &layer);
        if(layer){
            m_rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), m_pillGeometry), layer);
            float e    = SpEase(c.animT);
            float fade = std::clamp((e-.38f)/.62f, 0.f, 1.f);
            switch(c.state){
            case IslandState::MusicExpanded:   DrawMusicFull(px,pw,ph,c);  break;
            case IslandState::MusicQueue:      DrawMusicQueue(px,pw,ph,c); break;
            case IslandState::NotifList:       DrawNotifList(px,pw,ph,c);  break;
            case IslandState::NotifExpanded:
            case IslandState::CollapsingNotif: DrawNotif(px,pw,ph,c);      break;
            case IslandState::HUDVolume:
            case IslandState::HUDBrightness:
            case IslandState::HUDNetwork:      DrawHUD(px,pw,ph,c);        break;
            case IslandState::SystemExpanded:  DrawSystem(px,pw,ph,c);     break;
            case IslandState::WifiList:        DrawWifiList(px,pw,ph,c);   break;
            case IslandState::BluetoothList:   DrawBluetoothList(px,pw,ph,c); break;
            case IslandState::Expanding:
                if(fade>.4f && !c.musicTitle.empty()) DrawMusicCompact(px,pw,ph,c);
                break;
            case IslandState::Collapsing:      DrawIdle(px,pw,ph,c); break;
            default:
                if(!c.musicTitle.empty() && c.state==IslandState::Idle && c.isHovered)
                    DrawMusicCompact(px,pw,ph,c);
                else
                    DrawIdle(px,pw,ph,c);
                break;
            }
            m_rt->PopLayer();
            layer->Release();
        }
    } else {
        m_rt->PushAxisAlignedClip(D2D1::RectF(px,0.f,px+pw,ph), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        DrawIdle(px,pw,ph,c);
        m_rt->PopAxisAlignedClip();
    }

    HRESULT hr = m_glass.End();
    if(hr == D2DERR_RECREATE_TARGET){ DropDevRes(); CreateDevRes(); }
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawLiquidPill
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawLiquidPill(float px, float pw, float ph, const IslandContent& c) {
    float cr     = c.cornerRadius;
    float wob    = c.wobbleAmount;
    float wobPh  = c.wobblePhase;
    float dyL    = sinf(wobPh) * wob;
    float dyR    = sinf(wobPh + (float)M_PI*0.8f) * wob;
    // Phase B : formes Goutte (3) & Courbe (4) — biais géométrique bas asymétrique
    if(PillRT::SHAPE == 3){ dyL += ph*0.10f; dyR += ph*0.10f; }       // Goutte : bord bas descend
    else if(PillRT::SHAPE == 4){ dyL -= ph*0.03f; dyR += ph*0.08f; }  // Courbe : incliné droite
    float earR   = 8.0f;
    float earFade= std::clamp(1.0f-(ph-Pill::H_IDLE)/60.f, 0.0f, 1.0f);
    float eR     = earR * earFade;

    if(m_pillGeometry){ m_pillGeometry->Release(); m_pillGeometry=nullptr; }
    m_f->CreatePathGeometry(&m_pillGeometry);
    ID2D1GeometrySink* sink=nullptr;
    m_pillGeometry->Open(&sink);
    if(sink){
        sink->SetFillMode(D2D1_FILL_MODE_WINDING);
        sink->BeginFigure({px-eR, 0.f}, D2D1_FIGURE_BEGIN_FILLED);
        sink->AddBezier({{px-eR*0.45f, 0.f},{px, eR*0.45f},{px, eR}});
        sink->AddLine({px, ph-cr});
        sink->AddBezier({{px, ph-cr*0.45f+dyL},{px+cr*0.45f, ph+dyL},{px+cr, ph+dyL}});
        sink->AddLine({px+pw-cr, ph+dyR});
        sink->AddBezier({{px+pw-cr*0.45f, ph+dyR},{px+pw, ph-cr*0.45f+dyR},{px+pw, ph-cr}});
        sink->AddLine({px+pw, eR});
        sink->AddBezier({{px+pw, eR*0.45f},{px+pw+eR*0.45f, 0.f},{px+pw+eR, 0.f}});
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close(); sink->Release();
    }

    // ── FOND : mode adaptatif = POCHETTE FLOUTÉE clippée au pill (signature
    // Apple) ; sinon verre teinté à l'accent choisi dans le Cockpit.
    bool artBg = false;
    if (PillRT::ADAPTIVE && m_albumArt && m_pillGeometry) {
        ID2D1DeviceContext* dc = m_glass.DC();
        if (dc) {
            if (!m_fxBlur) {
                dc->CreateEffect(CLSID_D2D1GaussianBlur, &m_fxBlur);
                if (m_fxBlur) {
                    m_fxBlur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 24.f);
                    m_fxBlur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE,
                                       D2D1_BORDER_MODE_HARD);
                }
            }
            if (m_fxBlur) {
                m_fxBlur->SetInput(0, m_albumArt);
                D2D1_SIZE_F as = m_albumArt->GetSize();
                // PIÈGE DPI (identique à l'ambiant) : dc->DrawImage compose en PIXELS
                // DEVICE et ignore le SetDpi du RT, alors que le clip PushLayer(pill)
                // est DPI-scalé jusqu'au pill PHYSIQUE. Si on positionne le flou avec
                // px/pw/ph LOGIQUES, il ne couvre que pw×ph px device → le (pw*0.25)
                // droit de la carte musique reste sombre = bande noire à droite. On
                // exprime donc la cible du flou en DEVICE (× s = dpi/96).
                float dx, dy; m_rt->GetDpi(&dx, &dy);
                float s = dx > 0.f ? dx / 96.f : 1.f;
                float pxD = px*s, pwD = pw*s, phD = ph*s;
                float scale = std::max(pwD / as.width, phD / as.height) * 1.25f;
                dc->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(),
                                                     m_pillGeometry), nullptr);
                dc->SetTransform(
                    D2D1::Matrix3x2F::Scale(scale, scale) *
                    D2D1::Matrix3x2F::Translation(pxD + (pwD - as.width*scale)*.5f,
                                                  (phD - as.height*scale)*.5f));
                dc->DrawImage(m_fxBlur);
                dc->SetTransform(D2D1::Matrix3x2F::Identity());
                // Voile sombre NEUTRE (noir) pour la lisibilité du texte : c'est la
                // POCHETTE FLOUTÉE en dessous qui donne sa couleur à l'encoche, PAS un
                // voile teinté (un voile coloré masquait la pochette). Opacité réduite
                // (0.08 + 0.55*BASE_ALPHA) pour laisser la pochette bien transparaître.
                SetB0({0.f, 0.f, 0.f, 0.08f + 0.55f * PillRT::BASE_ALPHA});
                m_rt->FillGeometry(m_pillGeometry, m_b0);
                dc->PopLayer();
                artBg = true;
            }
        }
    }
    if (!artBg) {
        // Verre teinté (accent fixe, ou adaptatif sans pochette disponible)
        D2D1_COLOR_F base = { m_accent.r*0.22f, m_accent.g*0.22f, m_accent.b*0.22f,
                              PillRT::BASE_ALPHA };
        SetB0(base);
        if(m_pillGeometry) m_rt->FillGeometry(m_pillGeometry, m_b0);
        else m_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(px,0,px+pw,ph),cr,cr), m_b0);
        D2D1_COLOR_F tint = { m_accent.r, m_accent.g, m_accent.b, 0.10f };
        SetB0(tint);
        if(m_pillGeometry) m_rt->FillGeometry(m_pillGeometry, m_b0);
    }

    // Voile verre froid sur toute la surface (très subtil)
    if(m_bGlass && m_pillGeometry){
        m_bGlass->SetStartPoint({0.f, 0.f});
        m_bGlass->SetEndPoint  ({0.f, ph});
        m_bGlass->SetOpacity(0.85f);
        m_rt->FillGeometry(m_pillGeometry, m_bGlass);
        m_bGlass->SetOpacity(1.f);
    }

    // Texture de grain (option Cockpit) — aspect organique du verre
    if(PillRT::GRAIN && m_bGrain && m_pillGeometry){
        m_bGrain->SetOpacity(0.55f);
        m_rt->FillGeometry(m_pillGeometry, m_bGrain);
    }

    // Liseret blanc fin sur le contour (signature Liquid Glass)
    if(m_pillGeometry){
        SetB1({1.f, 1.f, 1.f, 0.22f});
        m_rt->DrawGeometry(m_pillGeometry, m_b1, 1.0f);
    }

    // Ombre portée (subtile, comme iOS)
    if(ph > Pill::H_IDLE*1.5f && m_bSd){
        float t01 = std::clamp((ph-Pill::H_IDLE)/(Pill::H_MUSIC-Pill::H_IDLE), 0.f, 1.f);
        float sh  = 5.f*t01;
        m_bSd->SetStartPoint({0,ph-sh}); m_bSd->SetEndPoint({0,ph+sh*1.5f});
        m_bSd->SetOpacity(0.40f);
        m_rt->FillRoundedRectangle({{px+4,ph-sh,px+pw-4,ph+sh*1.5f},cr*.5f,cr*.5f}, m_bSd);
        sh = 20.f*t01;
        m_bSd->SetStartPoint({0,ph-sh*.4f}); m_bSd->SetEndPoint({0,ph+sh});
        m_bSd->SetOpacity(0.07f);
        m_rt->FillRoundedRectangle({{px-8,ph-sh*.4f,px+pw+8,ph+sh},cr,cr}, m_bSd);
        m_bSd->SetOpacity(1.f);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawGlassZone
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawGlassZone(float px, float pw, float yStart, float yEnd, float cr, float alpha) {
    if(!m_pillGeometry||!m_bGlass||yEnd<=yStart) return;
    // Dégradé DIAGONAL vers le bas-droit → coin verre froid (plus de "blanc").
    m_bGlass->SetStartPoint({px,        yStart});
    m_bGlass->SetEndPoint  ({px + pw,   yEnd  });
    m_bGlass->SetOpacity(alpha * 0.85f);  // opacité réduite (était 1.1)
    m_rt->PushAxisAlignedClip({px,yStart,px+pw,yEnd}, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    m_rt->FillGeometry(m_pillGeometry, m_bGlass);
    // Liseret supérieur ardoise (plus blanc pur)
    SetB1({0.70f, 0.74f, 0.85f, 0.07f * alpha});
    m_rt->DrawLine({px,yStart},{px+pw,yStart}, m_b1, 0.5f);
    m_rt->PopAxisAlignedClip();
    m_bGlass->SetOpacity(1.f);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawIdle — P1 : heure en Segoe UI Variable Display Light (≈ SF Pro)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawIdle(float px, float pw, float ph, const IslandContent& c) {
    float cy = ph*.5f;
    float alpha = 1.0f;
    if(c.state==IslandState::Collapsing)     alpha = c.animT;
    else if(c.state==IslandState::Expanding) alpha = 1.0f - c.animT;

    DrawWaveform(px+22.f, cy, 13.f, c.isMusicPlaying, c.globalT, m_accent, alpha);

    if(!c.clockTime.empty()){
        // P1-APPLE : blanc pur légèrement atténué — SF Pro affiche en blanc
        // m_fC est maintenant Segoe UI Variable Display Light (≈ SF Pro Display)
        Txt(c.clockTime, m_fC,
            D2D1::RectF(px, cy-9.f, px+pw, cy+9.f),
            K::WHITE, alpha*0.90f,
            DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    if(c.batteryPercent >= 0){
        float batX = px+pw-30.f;
        DrawBat(batX, cy-5.f, c.batteryPercent, c.batteryCharging);
        wchar_t b[8]; swprintf_s(b, L"%.0f%%", c.batteryPercent);
        Txt(b, m_fX,
            D2D1::RectF(batX-50.f, cy-9.f, batX-8.f, cy+9.f),
            K::GR2, alpha*0.82f,
            DWRITE_TEXT_ALIGNMENT_TRAILING);
    }

    // Badges non-lus retirés de l'idle (le point vert + "1" rouge ne plaisaient pas).
    // La notif se signale désormais par l'animation toast + cloche, pas par un badge.
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawAppBadges
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawAppBadges(float px, float pw, float ph, const IslandContent& c) {
    if(c.appBadges.empty()) return;
    float cy = ph*.5f;
    float anim = SpEase(std::clamp(c.badgePulseT, 0.f, 1.f));
    const float DOT_R=5.5f, OVERLAP=3.5f, STEP=DOT_R*2.f-OVERLAP;
    const int MAX_DOTS=3;
    int ndots = (int)std::min(c.appBadges.size(), (size_t)MAX_DOTS);
    bool hasExtra = (int)c.appBadges.size() > MAX_DOTS;
    float bx0 = px + pw*0.64f;

    SetB0({1,1,1,0.10f*anim});
    m_rt->DrawLine({bx0-5.f,cy-8.f},{bx0-5.f,cy+8.f}, m_b0, 0.5f);

    for(int i=ndots-1; i>=0; --i){
        const auto& badge = c.appBadges[i];
        float dotX = bx0 + i*STEP + DOT_R;
        SetB0(K::BG,    anim*0.9f); m_rt->FillEllipse({{dotX,cy},DOT_R+1.5f,DOT_R+1.5f}, m_b0);
        SetB0(badge.color, anim*0.92f); m_rt->FillEllipse({{dotX,cy},DOT_R,DOT_R}, m_b0);
        if(badge.count>1 && ndots<=2){
            wchar_t cnt[6];
            if(badge.count>99) wcscpy_s(cnt, L"99+");
            else swprintf_s(cnt, L"%d", badge.count);
            float bx2=dotX+DOT_R*0.55f, by2=cy-DOT_R*0.55f;
            float br=(badge.count>9)?5.5f:4.5f;
            SetB0(K::RED, anim*0.95f); m_rt->FillEllipse({{bx2,by2},br,br}, m_b0);
            SetB1({1,1,1,anim});
            if(m_fX) Txt(cnt, m_fX, {bx2-br,by2-br,bx2+br,by2+br}, K::WHITE, anim*0.95f, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }
    if(hasExtra||c.totalUnreadCount>9){
        float extraX = bx0+ndots*STEP+DOT_R+2.f;
        wchar_t extra[8];
        if(hasExtra) swprintf_s(extra, L"+%d", (int)c.appBadges.size()-MAX_DOTS);
        else         swprintf_s(extra, L"+%d", c.totalUnreadCount-9);
        float tw2=18.f;
        SetB0({1,1,1,0.10f*anim});
        m_rt->FillRoundedRectangle({{extraX-2,cy-DOT_R,extraX+tw2,cy+DOT_R},DOT_R,DOT_R}, m_b0);
        Txt(extra, m_fX, {extraX,cy-DOT_R,extraX+tw2,cy+DOT_R}, K::GR2, anim*0.75f, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    if(c.totalUnreadCount>0 && c.totalUnreadCount<=9){
        float pulse = 1.f + 0.07f*sinf(c.globalT*4.f)*(1.f-anim);
        float bx2=bx0-11.f, br=5.5f*pulse;
        SetB0(K::RED, anim*0.85f); m_rt->FillEllipse({{bx2,cy-9.f},br,br}, m_b0);
        wchar_t tot[4]; swprintf_s(tot, L"%d", c.totalUnreadCount);
        Txt(tot, m_fX, {bx2-br-1,cy-9.f-br,bx2+br+1,cy-9.f+br}, K::WHITE, anim*0.95f, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawNotifBadge
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawNotifBadge(float cx, float cy, float radius, D2D1_COLOR_F color,
                               const std::wstring& glyph, float alpha) {
    SetB0(color, alpha); m_rt->FillEllipse({{cx,cy},radius,radius}, m_b0);
    if(!glyph.empty())
        Txt(glyph, m_fI14, {cx-10,cy-10,cx+10,cy+10}, K::WHITE, alpha, DWRITE_TEXT_ALIGNMENT_CENTER);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawNotif
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawNotif(float px, float pw, float ph, const IslandContent& c) {
    bool  out = (c.state==IslandState::CollapsingNotif);
    float e   = out ? 1.f-CuEase(c.animT) : CuEase(c.animT);
    float cy  = ph*.5f;
    float bx  = px + 28.f;               // centre de la pastille d'ic\u00F4ne

    // Pastille d'ic\u00F4ne (16 r) avec secousse \u00AB cloche \u00BB \u00E0 l'arriv\u00E9e
    if(c.bellShakeT>0.f){
        float angle = sinf(c.bellShakeT*6.f*(float)M_PI)*16.f*expf(-3.f*c.bellShakeT);
        D2D1_MATRIX_3X2_F old; m_rt->GetTransform(&old);
        m_rt->SetTransform(D2D1::Matrix3x2F::Rotation(angle,{bx,cy})*old);
        DrawNotifBadge(bx, cy, 16.f, c.notifAppColor, c.notifIconGlyph, e);
        m_rt->SetTransform(old);
    } else {
        DrawNotifBadge(bx, cy, 16.f, c.notifAppColor, c.notifIconGlyph, e);
    }

    float tx = px + 54.f;
    std::wstring app = c.notifAppName.empty() ? L"Notification" : c.notifAppName;
    std::wstring msg = c.notifMessage.empty() ? c.notifTitle    : c.notifMessage;
    // App (haut) + message (bas) \u2192 toast clairement lisible
    Txt(app, m_fT, {tx, cy-18, px+pw-50, cy-1}, K::WHITE, e);
    if(!msg.empty())
        Txt(msg, m_fS, {tx, cy+1, px+pw-16, cy+19}, K::GR2, e*.85f);
    if(!c.clockTime.empty())
        Txt(c.clockTime, m_fC, {px+pw-48, cy-18, px+pw-12, cy-1},
            K::GR, e*.70f, DWRITE_TEXT_ALIGNMENT_TRAILING);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawNotifList
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawNotifList(float px, float pw, float ph, const IslandContent& c) {
    float fade = std::clamp((CuEase(c.animT)-.22f)/.78f, 0.f, 1.f);
    PushSlideClip(px, pw, ph, c.tabSlideX);
    DrawTopIcons(px, pw, fade, c.activeMenuIndex, c.bellShakeT, c.tabSlideX);

    float pad=14.f, iy=36.f;
    float btnW=108.f, btnH=22.f, btnX=px+pw-pad-btnW;
    SetB0({1,1,1,0.08f*fade});
    m_rt->FillRoundedRectangle({{btnX,iy+1,btnX+btnW,iy+1+btnH},11.f,11.f}, m_b0);
    Txt(L"Tout effacer", m_fX, {btnX,iy+3,btnX+btnW,iy+1+btnH},
        K::GR, fade*.80f, DWRITE_TEXT_ALIGNMENT_CENTER);
    iy += 30.f;
    float cx = px+pw*0.5f, textHalf=pw*0.5f;

    if(c.notifHistory.empty()){
        float emptyCY = iy+(ph-iy)*.5f;
        if(m_fI20) Txt(L"\uEA8F", m_fI20, {px+pw*.5f-14,emptyCY-20,px+pw*.5f+14,emptyCY},
            K::GR, fade*.22f, DWRITE_TEXT_ALIGNMENT_CENTER);
        Txt(L"Aucune notification", m_fS, {px+pad,emptyCY+4,px+pw-pad,emptyCY+22},
            K::GR, fade*.32f, DWRITE_TEXT_ALIGNMENT_CENTER);
        PopSlideClip(c.tabSlideX); return;
    }

    float itemH=58.f, gap=6.f;
    int maxN = (int)std::min(c.notifHistory.size(), (size_t)3);
    for(int i=0; i<maxN; ++i){
        const auto& ni = c.notifHistory[c.notifHistory.size()-1-i];
        float iy2 = iy + i*(itemH+gap);
        if(iy2+itemH > ph-8) break;
        // P1 : fond items légèrement plus foncé — Apple iOS style
        SetB0({1,1,1,0.065f*fade});
        m_rt->FillRoundedRectangle({{px+pad,iy2,px+pw-pad,iy2+itemH},14.f,14.f}, m_b0);
        SetB1({1,1,1,0.07f*fade});
        m_rt->DrawRoundedRectangle({{px+pad,iy2,px+pw-pad,iy2+itemH},14.f,14.f}, m_b1, 0.45f);
        float bx=px+pad+22.f, by=iy2+itemH*.5f;
        SetB0(ni.appColor, fade*.82f); m_rt->FillEllipse({{bx,by},16.f,16.f}, m_b0);
        if(m_fI14) Txt(ni.iconGlyph, m_fI14, {bx-10,by-10,bx+10,by+10},
            K::WHITE, fade, DWRITE_TEXT_ALIGNMENT_CENTER);
        float nleft=cx-textHalf+46.f, nright=cx+textHalf-38.f;
        Txt(ni.appName,  m_fT,  {nleft,iy2+8, nright,iy2+24},  K::WHITE, fade*.92f);
        Txt(ni.message,  m_fS,  {nleft,iy2+26,nright+32,iy2+itemH-8}, K::GR, fade*.65f);
        Txt(ni.timeStr,  m_fX,  {nright-22,iy2+8,nright+18,iy2+22},
            K::GR, fade*.42f, DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
    if(c.notifHistory.size()>3){
        float dotY=ph-10.f;
        for(int i=0;i<3;++i){
            float dx=px+pw*.5f+(i-1)*10.f;
            SetB0({1,1,1,i==0?0.52f:0.18f}, fade);
            m_rt->FillEllipse({{dx,dotY},2.f,2.f}, m_b0);
        }
    }
    PopSlideClip(c.tabSlideX);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawMusicCompact
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawMusicCompact(float px, float pw, float ph, const IslandContent& c) {
    float fade=1.0f;
    if(c.state==IslandState::Expanding)  fade = c.animT;
    if(c.state==IslandState::Collapsing) fade = 1.0f - c.animT;
    float cy=ph*.5f, artS=24.f;
    DrawAlbumArt(px+8, cy-artS*.5f, artS, fade, c.albumArtBitmap);
    std::wstring ti = c.musicTitle.empty() ? L"Sans titre" : c.musicTitle;
    Txt(ti, m_fS, {px+40,cy-8,px+pw-40,cy+8}, K::WHITE, fade);
    DrawWaveform(px+pw-20, cy, 12, c.isMusicPlaying, c.globalT, m_accent, fade);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawMusicFull
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawMusicFull(float px, float pw, float ph, const IslandContent& c) {
    // Fondu PROGRESSIF (CuEase) : l'élastique SpEase faisait apparaître le
    // contenu d'un coup — transitions plus douces demandées.
    float fade = std::clamp((CuEase(c.animT)-.15f)/.85f, 0.f, 1.f);
    PushSlideClip(px, pw, ph, c.tabSlideX);

    // Teinte album UNIFORME \u2014 seulement en accent fixe. (En adaptatif, le fond
    // EST la pochette flout\u00E9e : pas de voile couleur par-dessus. Et jamais de
    // split haut/bas \u2192 ligne visible.)
    if(m_pillGeometry && !(PillRT::ADAPTIVE && c.albumArtBitmap)){
        SetB0(m_accent, fade*0.11f);
        m_rt->FillGeometry(m_pillGeometry, m_b0);
    }
    DrawTopIcons(px, pw, fade, c.activeMenuIndex, c.bellShakeT, c.tabSlideX);

    float artX=px+16.f, artY=28.f, artS=60.f;
    DrawAlbumArt(artX, artY, artS, fade, c.albumArtBitmap);
    float tx=artX+artS+14.f;
    const float tw=200.f;
    std::wstring ti = c.musicTitle.empty()  ? L"Sans titre" : c.musicTitle;
    std::wstring ar = c.musicArtist.empty() ? L""           : c.musicArtist;
    Txt(ti, m_fTBig, {tx,artY+4,  tx+tw,artY+26}, K::WHITE, fade);
    Txt(ar, m_fS,    {tx,artY+27, tx+tw,artY+43}, K::GR2,   fade*.82f);
    if(!c.musicSourceApp.empty())
        Txt(L"\u25CF  "+c.musicSourceApp, m_fX,
            {tx,artY+45,tx+tw,artY+58}, K::GR, fade*.50f);
    DrawWaveform(px+pw-26, artY+18, 12, c.isMusicPlaying, c.globalT, K::WHITE, fade);

    float barY = 98.f;
    DrawPBar(px+14, barY, pw-28, c.musicProgress, c.musicCurrentSec, c.musicTotalSec, fade);
    float glassY = 130.f;
    DrawGlassZone(px, pw, glassY, ph, c.cornerRadius, fade);
    DrawControls(px, pw, glassY+(ph-glassY)*.5f, c.isMusicPlaying, fade, K::WHITE);
    PopSlideClip(c.tabSlideX);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawMusicQueue
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawMusicQueue(float px, float pw, float ph, const IslandContent& c) {
    float fade = std::clamp((CuEase(c.animT)-.12f)/.88f, 0.f, 1.f);
    PushSlideClip(px, pw, ph, c.tabSlideX);
    DrawTopIcons(px, pw, fade, c.activeMenuIndex, c.bellShakeT, c.tabSlideX);

    float artX=px+14.f, artY=38.f, artS=54.f;
    DrawAlbumArt(artX, artY, artS, fade, c.albumArtBitmap);
    float tx=artX+artS+12.f;
    const float tw=150.f;
    std::wstring ti = c.musicTitle.empty()  ? L"Sans titre" : c.musicTitle;
    std::wstring ar = c.musicArtist.empty() ? L""           : c.musicArtist;
    Txt(ti, m_fTBig, {tx,artY+2,  tx+tw,artY+22}, K::WHITE, fade);
    Txt(ar, m_fS,    {tx,artY+24, tx+tw,artY+40}, K::GR2,   fade*.82f);
    if(!c.musicSourceApp.empty())
        Txt(L"\u25CF  "+c.musicSourceApp, m_fX,
            {tx,artY+44,tx+tw,artY+56}, K::GR, fade*.48f);
    DrawWaveform(px+pw-26, artY+14, 12, c.isMusicPlaying, c.globalT, K::WHITE, fade);

    float barY = artY+artS+12.f;
    DrawPBar(px+14, barY, pw-28, c.musicProgress, c.musicCurrentSec, c.musicTotalSec, fade);
    float ctrlY = barY+42.f;
    // Plus de cercle parasite autour du bouton liste : DrawControls g\u00E8re uniform\u00E9ment.
    DrawControls(px, pw, ctrlY, c.isMusicPlaying, fade, K::WHITE);

    float sepY = ctrlY+28.f;   // resserré : −50 % d'espace entre play et la liste
    SetB0({1,1,1,.06f*fade});
    m_rt->DrawLine({px+14,sepY},{px+pw-14,sepY}, m_b0, .5f);
    float qlY = sepY+5.f;
    // File Spotify UNIQUEMENT : sans connexion, on INVITE (pas de fausse liste
    // \u00AB r\u00E9cemment jou\u00E9 \u00BB qui se fait passer pour les prochains titres).
    const auto& tracks = c.upcomingTracks;
    Txt(L"Playing Next", m_fT, {px+14,qlY,px+pw-14,qlY+18}, K::WHITE, fade);

    if(tracks.empty()){
        Txt(L"Connectez Spotify pour voir la file", m_fS,
            {px+14,qlY+24,px+pw-14,qlY+42}, K::GR, fade*.50f, DWRITE_TEXT_ALIGNMENT_CENTER);
        Txt(L"Cockpit \u2192 Autorisations \u2192 Connecter Spotify", m_fX,
            {px+14,qlY+44,px+pw-14,qlY+58}, K::GR, fade*.32f, DWRITE_TEXT_ALIGNMENT_CENTER);
        PopSlideClip(c.tabSlideX); return;
    }

    // Liste DÉFILABLE (molette) : toutes les pistes, clippées à la zone —
    // on fait défiler au lieu d'agrandir l'encoche.
    float listTop = qlY + 20.f;
    const float ITEM_H = 40.f, ITEM_GAP = 5.f;
    float contentH  = (float)tracks.size() * (ITEM_H + ITEM_GAP) - ITEM_GAP;
    float visibleH  = ph - listTop - 8.f;
    float maxScroll = std::max(0.f, contentH - visibleH);
    float scroll    = std::clamp(c.queueScrollY, 0.f, maxScroll);
    m_rt->PushAxisAlignedClip({px, listTop, px+pw, ph}, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    float itemY = listTop - scroll;
    for(int i=0; i<(int)tracks.size(); ++i, itemY += ITEM_H + ITEM_GAP){
        if(itemY + ITEM_H < listTop) continue;   // au-dessus de la zone visible
        if(itemY > ph) break;                    // en dessous
        const auto& item = tracks[i];
        // Fond ligne translucide léger (verre)
        SetB0({1,1,1,.035f*fade});
        m_rt->FillRoundedRectangle({{px+12,itemY,px+pw-12,itemY+ITEM_H},9.f,9.f}, m_b0);
        // Titre + artiste uniquement (sans vignette, à la demande)
        Txt(item.title,  m_fT, {px+24,itemY+4,  px+pw-24,itemY+22}, K::WHITE, fade);
        Txt(item.artist, m_fX, {px+24,itemY+22, px+pw-24,itemY+37}, K::GR,   fade*.62f);
    }
    m_rt->PopAxisAlignedClip();

    // Indicateur de scroll (fin, \u00E0 droite) quand la liste d\u00E9borde
    if(maxScroll > 1.f){
        float trackTop = listTop + 4.f, trackBot = ph - 10.f;
        float trackH   = trackBot - trackTop;
        float thumbH   = std::max(18.f, trackH * visibleH / contentH);
        float thumbY   = trackTop + (trackH - thumbH) * (scroll / maxScroll);
        SetB0({1,1,1,.16f*fade});
        m_rt->FillRoundedRectangle({{px+pw-7,thumbY,px+pw-4,thumbY+thumbH},1.5f,1.5f}, m_b0);
    }
    // Fondu bas de liste
    if(m_bSd && m_pillGeometry){
        float fadeH=22.f;   // discret : les 3 lignes visibles restent nettes
        m_bSd->SetStartPoint({0,ph-fadeH}); m_bSd->SetEndPoint({0,ph+8.f});
        m_bSd->SetOpacity(fade*.45f); m_rt->FillGeometry(m_pillGeometry, m_bSd);
        m_bSd->SetOpacity(1.f);
    }
    PopSlideClip(c.tabSlideX);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawTopIcons
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawTopIcons(float px, float pw, float alpha, int activeMenu,
                             float bellShakeT, float /*slideX*/) {
    float cx = px+pw*.5f;
    struct { float x; const wchar_t* g; } ic[]={
        {cx-58, L"\uEA8F"}, {cx, L"\uEC4F"}, {cx+58, L"\uE713"}};
    for(int i=0; i<3; ++i){
        D2D1_COLOR_F col = (i==activeMenu) ? K::WHITE : K::GR;
        float iconY1=5.f, iconY2=25.f;
        if(i==0 && bellShakeT>0.f){
            float angle = sinf(bellShakeT*6.f*(float)M_PI)*15.f*expf(-3.f*bellShakeT);
            D2D1_MATRIX_3X2_F old; m_rt->GetTransform(&old);
            m_rt->SetTransform(D2D1::Matrix3x2F::Rotation(angle,{ic[i].x,15.f})*old);
            Txt(ic[i].g, m_fI14, {ic[i].x-12,iconY1,ic[i].x+12,iconY2},
                col, alpha, DWRITE_TEXT_ALIGNMENT_CENTER);
            m_rt->SetTransform(old);
        } else {
            Txt(ic[i].g, m_fI14, {ic[i].x-12,iconY1,ic[i].x+12,iconY2},
                col, alpha, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        if(i==activeMenu){
            SetB0(K::WHITE, alpha*.50f);
            m_rt->FillEllipse({{ic[i].x, iconY2+3.f},1.8f,1.8f}, m_b0);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawAlbumArt
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawAlbumArt(float x, float y, float s, float alpha, ID2D1Bitmap* bmp) {
    D2D1_ROUNDED_RECT rr = {{x,y,x+s,y+s}, s*0.15f, s*0.15f};
    if(bmp){
        m_rt->DrawBitmap(bmp, {x,y,x+s,y+s}, alpha);
        SetB1({1,1,1,.09f*alpha}); m_rt->DrawRoundedRectangle(rr, m_b1, .5f); return;
    }
    SetB0({0.10f,0.08f,0.15f,alpha}); m_rt->FillRoundedRectangle(rr, m_b0);
    auto band=[&](float fy,float fh,float r,float g,float b){
        SetB0({r,g,b,alpha*.30f}); m_rt->FillRectangle({x+1,y+fy,x+s-1,y+fy+fh}, m_b0);};
    band(0,s*.38f,0.52f,0.22f,0.72f);
    band(s*.28f,s*.42f,0.22f,0.42f,0.82f);
    band(s*.58f,s*.42f,0.18f,0.68f,0.58f);
    for(float r=s*.08f; r<s*.42f; r+=s*.09f){
        SetB0({1,1,1,alpha*.035f}); m_rt->DrawEllipse({{x+s*.5f,y+s*.5f},r,r}, m_b0, .5f);}
    SetB0({0.06f,0.05f,0.10f,alpha*.75f}); m_rt->FillEllipse({{x+s*.5f,y+s*.5f},s*.12f,s*.12f}, m_b0);
    SetB1({1,1,1,.08f*alpha}); m_rt->DrawRoundedRectangle(rr, m_b1, .5f);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawWaveform
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawWaveform(float cx, float cy, float maxH, bool pl, float t,
                             D2D1_COLOR_F col, float alpha) {
    const int N=5; const float BW=2.5f, GAP=2.f, MINH=2.5f;
    static const float PH[]={0.f,1.3f,.65f,1.9f,.4f};
    float tw=N*BW+(N-1)*GAP, sx=cx-tw*.5f;
    for(int i=0; i<N; ++i){
        float h=MINH;
        if(pl) h = MINH+(maxH-MINH)*(.5f+.5f*sinf(t*(float)M_PI*5.f+PH[i]));
        float x=sx+i*(BW+GAP), y=cy-h*.5f;
        SetB0(col, alpha*.90f);
        m_rt->FillRoundedRectangle({{x,y,x+BW,y+h},1.2f,1.2f}, m_b0);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawPBar
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawPBar(float x, float y, float w, float prog,
                         float cur, float tot, float alpha) {
    float H=3.0f;     // plus fine (Liquid Glass)
    SetB0({1,1,1,.10f*alpha});
    m_rt->FillRoundedRectangle({{x,y,x+w,y+H},H*.5f,H*.5f}, m_b0);
    float fw = std::max(H, w*std::clamp(prog,0.f,1.f));
    SetB0(K::WHITE, alpha*.90f);    // remplissage BLANC (plus l'accent pochette)
    m_rt->FillRoundedRectangle({{x,y,x+fw,y+H},H*.5f,H*.5f}, m_b0);
    SetB0(K::WHITE, alpha);
    m_rt->FillEllipse({{x+fw, y+H*.5f},4.5f,4.5f}, m_b0);
    auto FT=[](float s,wchar_t* b){ swprintf_s(b,10,L"%d:%02d",(int)(s/60),(int)s%60); };
    wchar_t l[10],r2[10]; FT(cur,l); FT(tot,r2);
    float ty=y+H+7.f;
    Txt(l,  m_fX, {x,ty,x+44,ty+14},    K::GR, alpha*.68f);
    Txt(r2, m_fX, {x+w-44,ty,x+w,ty+14}, K::GR, alpha*.68f, DWRITE_TEXT_ALIGNMENT_TRAILING);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawControls
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawControls(float px, float pw, float cy, bool pl,
                             float alpha, D2D1_COLOR_F /*iconColor*/) {
    if(!m_fI14||!m_fI20) return;
    float cx=px+pw*.5f;
    float xs[]={px+48,cx-76,cx,cx+76,px+pw-48};
    const wchar_t* gl[]={L"\uE8FD",L"\uE892",pl?L"\uE769":L"\uE768",L"\uE893",L"\uE7F4"};

    for(int i=0; i<5; ++i){
        if(i == 2){
            // \u2500\u2500 Play/Pause : CONTOUR BLANC fin, sans fond (signature mockup) \u2500\u2500
            const float r = 22.f, hs = 14.f;
            SetB1({0.96f, 0.96f, 0.972f, 0.90f*alpha});
            m_rt->DrawEllipse({{xs[i], cy}, r, r}, m_b1, 1.6f);
            Txt(gl[i], m_fI20, {xs[i]-hs, cy-hs, xs[i]+hs, cy+hs},
                K::WHITE, alpha, DWRITE_TEXT_ALIGNMENT_CENTER);
        } else if(i == 1 || i == 3){
            // \u2500\u2500 Pr\u00E9c\u00E9dent / Suivant : glyphe blanc nu, AUCUN cercle \u2500\u2500
            const float hs = 10.f;
            Txt(gl[i], m_fI14, {xs[i]-hs, cy-hs, xs[i]+hs, cy+hs},
                K::WHITE, alpha*0.95f, DWRITE_TEXT_ALIGNMENT_CENTER);
        } else {
            // \u2500\u2500 Liste / AirPlay : pastille SOMBRE subtile (pas blanche) \u2500\u2500
            const float r = 16.f, hs = 10.f;
            SetB0({1.f, 1.f, 1.f, 0.06f*alpha});
            m_rt->FillEllipse({{xs[i], cy}, r, r}, m_b0);
            Txt(gl[i], m_fI14, {xs[i]-hs, cy-hs, xs[i]+hs, cy+hs},
                K::GR2, alpha*0.85f, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawHUD — P1 APPLE : barre verticale iOS (pill étroit tall)
//  iOS affiche : icône en haut, barre de remplissage du bas vers le haut,
//  fond gris très sombre, remplissage blanc
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawHUD(float px, float pw, float ph, const IslandContent& c) {
    float fade = std::clamp((CuEase(c.animT)-.20f)/.80f, 0.f, 1.f);
    float cx   = px + pw*.5f;

    // Couleur et icône selon le type
    D2D1_COLOR_F ac  = K::HUD_FILL;
    const wchar_t* gl= L"\uE767";  // volume
    switch(c.hudType){
    case HUDType::Brightness: gl=L"\uE706"; break;
    case HUDType::Network:    gl=L"\uE704"; ac=K::BLU; break;
    default: break;
    }

    // ── Icône en haut du pill ──────────────────────────────────────────────
    float iconY = 14.f;
    if(m_fI14)
        Txt(gl, m_fI14, {cx-10, iconY, cx+10, iconY+16},
            ac, fade*.85f, DWRITE_TEXT_ALIGNMENT_CENTER);

    // ── Barre verticale iOS style ──────────────────────────────────────────
    float barPad  = 14.f;           // marges haut/bas de la barre
    float barTop  = iconY+22.f;     // sous l'icône
    float barBot  = ph - barPad;    // jusqu'en bas du pill
    float barH    = barBot - barTop;
    float barW    = pw*0.52f;       // largeur ~moitié du pill
    float barX    = cx - barW*.5f;

    // Fond de la barre (très sombre — style iOS)
    SetB0({1,1,1, 0.10f*fade});
    m_rt->FillRoundedRectangle(
        {{barX, barTop, barX+barW, barBot}, barW*.5f, barW*.5f}, m_b0);

    // Remplissage du bas vers le haut (comportement iOS)
    float fillH  = barH * std::clamp(c.hudValue, 0.f, 1.f);
    float fillTop= barBot - fillH;
    if(fillH > 2.f){
        SetB0(ac, fade*0.92f);
        // Clip pour ne pas dépasser le fond arrondi
        m_rt->PushAxisAlignedClip(
            {barX, barTop, barX+barW, barBot}, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        m_rt->FillRoundedRectangle(
            {{barX, fillTop, barX+barW, barBot}, barW*.5f, barW*.5f}, m_b0);
        m_rt->PopAxisAlignedClip();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawSystem
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawSystem(float px, float pw, float ph, const IslandContent& c) {
    float fade = std::clamp((CuEase(c.animT)-.22f)/.78f, 0.f, 1.f);
    PushSlideClip(px, pw, ph, c.tabSlideX);
    DrawTopIcons(px, pw, fade, c.activeMenuIndex, c.bellShakeT, c.tabSlideX);

    float pad=13.f, row1Y=38.f, pillH2=48.f;
    float pw1=(pw-pad*2.f-10.f)*0.38f, pw2=pw1;
    float pw3=pw-pad*2.f-pw1*2.f-10.f;

    DrawTogglePill(px+pad,        row1Y, pw1, pillH2, L"\uE701", L"Wi-Fi",
        c.wifiEnabled?(c.wifiSSID.empty()?L"Connect\u00E9":c.wifiSSID.c_str()):L"D\u00E9sactiv\u00E9",
        c.wifiEnabled, fade);
    DrawTogglePill(px+pad+pw1+5, row1Y, pw2, pillH2, L"\uE702", L"Bluetooth",
        c.bluetoothEnabled?L"Actif":L"D\u00E9sactiv\u00E9",
        c.bluetoothEnabled, fade);

    // Toggle Mode Sombre — switch Apple style
    float px3 = px+pad+pw1*2+10;
    DrawTogglePill(px3, row1Y, pw3, pillH2, nullptr, nullptr, nullptr, c.darkModeEnabled, fade);
    if(m_fI20){
        const wchar_t* modeIco = c.darkModeEnabled ? L"\uE708" : L"\uE793";
        Txt(modeIco, m_fI20, {px3+7,row1Y+8,px3+28,row1Y+pillH2-8},
            K::WHITE, fade*.82f, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    {
        float cx3=px3+pw3*.5f+8, cy3=row1Y+pillH2*.5f;
        float tw=32.f, th=17.f, tx=cx3-tw*.5f, ty=cy3-th*.5f;
        D2D1_COLOR_F tgBg = c.darkModeEnabled
            ? D2D1::ColorF(0.f,0.47f,1.f,1.f)
            : D2D1::ColorF(0.26f,0.26f,0.28f,1.f);
        SetB0(tgBg, fade*.90f);
        m_rt->FillRoundedRectangle({{tx,ty,tx+tw,ty+th},th*.5f,th*.5f}, m_b0);
        float knobX = c.darkModeEnabled ? tx+tw-th*.5f : tx+th*.5f;
        SetB0(K::WHITE, fade);
        m_rt->FillEllipse({{knobX,cy3}, th*.5f-2.f, th*.5f-2.f}, m_b0);
    }

    float row2Y = row1Y+pillH2+7.f;
    DrawTogglePill(px+pad, row2Y, 140.f, 38.f, L"\uE706", L"\u00C9clai. nocturne", L"",
        c.nightLightEnabled, fade);

    float iconW=22.f, sx=px+pad, sw=pw-pad*2.f-iconW-8.f;
    float sy1=row2Y+48.f, sy2=sy1+38.f;
    DrawSlider(sx, sy1, sw, c.systemVolume,     L"\uE767", fade);
    DrawSlider(sx, sy2, sw, c.systemBrightness, L"\uE706", fade);
    PopSlideClip(c.tabSlideX);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawWifiList
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawWifiList(float px, float pw, float ph, const IslandContent& c) {
    float fade = std::clamp((CuEase(c.animT)-.20f)/.80f, 0.f, 1.f);
    float pad=14.f, hY=14.f;
    // Fl\u00E8che retour
    SetB0({1,1,1,.07f*fade}); m_rt->FillEllipse({{px+pad+10,hY+11},12.f,12.f}, m_b0);
    if(m_fI14)
        Txt(L"\uE72B", m_fI14, {px+pad,hY+3,px+pad+24,hY+21},
            K::WHITE, fade*.82f, DWRITE_TEXT_ALIGNMENT_CENTER);
    Txt(L"Wi-Fi", m_fT, {px+pad+28,hY+2,px+pw-pad-64,hY+20}, K::WHITE, fade);

    // Interrupteur iOS (haut-droite) \u2014 activer/d\u00E9sactiver le Wi-Fi
    float tglW=40.f, tglH=22.f, tglX=px+pw-pad-tglW, tglY=hY;
    float tcy=tglY+tglH*.5f;
    D2D1_COLOR_F tgBg = c.wifiEnabled
        ? D2D1::ColorF(0.f,0.47f,1.f,1.f) : D2D1::ColorF(0.26f,0.26f,0.28f,1.f);
    SetB0(tgBg, fade*.92f);
    m_rt->FillRoundedRectangle({{tglX,tglY,tglX+tglW,tglY+tglH},tglH*.5f,tglH*.5f}, m_b0);
    float knobX = c.wifiEnabled ? tglX+tglW-tglH*.5f : tglX+tglH*.5f;
    SetB0(K::WHITE, fade);
    m_rt->FillEllipse({{knobX,tcy}, tglH*.5f-2.f, tglH*.5f-2.f}, m_b0);

    float sepY=hY+26.f;
    SetB1({1,1,1,.06f*fade}); m_rt->DrawLine({px+pad,sepY},{px+pw-pad,sepY}, m_b1, .45f);

    // \u2500\u2500 Mode saisie mot de passe (r\u00E9seau s\u00E9curis\u00E9 non enregistr\u00E9) \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500
    if(c.wifiPassMode){
        float fy=sepY+14.f;
        Txt(c.wifiPassSsid, m_fT, {px+pad,fy,px+pw-pad,fy+20}, K::WHITE, fade);
        Txt(L"Mot de passe", m_fX, {px+pad,fy+22,px+pw-pad,fy+36}, K::GR, fade*.60f);
        float bxx=px+pad, bxy=fy+42.f, bxw=pw-pad*2.f, bxh=34.f;
        SetB0({1,1,1,.08f*fade}); m_rt->FillRoundedRectangle({{bxx,bxy,bxx+bxw,bxy+bxh},9.f,9.f}, m_b0);
        SetB1({1,1,1,.18f*fade}); m_rt->DrawRoundedRectangle({{bxx,bxy,bxx+bxw,bxy+bxh},9.f,9.f}, m_b1, 1.f);
        bool empty = c.wifiPassText.empty();
        Txt(empty? std::wstring(L"Tapez le mot de passe\u2026") : c.wifiPassText,
            m_fT, {bxx+12,bxy+8,bxx+bxw-12,bxy+bxh-6},
            empty?K::GR:K::WHITE, fade*(empty?.45f:.92f));
        float by=ph-40.f;
        SetB0({1,1,1,.08f*fade}); m_rt->FillRoundedRectangle({{px+pad,by,px+pad+90,by+30},9.f,9.f}, m_b0);
        Txt(L"Annuler", m_fS, {px+pad,by+7,px+pad+90,by+25}, K::WHITE, fade*.80f, DWRITE_TEXT_ALIGNMENT_CENTER);
        SetB0(K::BLU, fade*.92f); m_rt->FillRoundedRectangle({{px+pw-pad-104,by,px+pw-pad,by+30},9.f,9.f}, m_b0);
        Txt(L"Connecter", m_fS, {px+pw-pad-104,by+7,px+pw-pad,by+25}, K::WHITE, fade, DWRITE_TEXT_ALIGNMENT_CENTER);
        return;
    }

    float itemH=48.f, gap=5.f, iy=sepY+8.f;
    if(!c.wifiEnabled){
        Txt(L"Wi-Fi d\u00E9sactiv\u00E9", m_fS,
            {px+pad,iy+8,px+pw-pad,iy+26}, K::GR, fade*.55f, DWRITE_TEXT_ALIGNMENT_CENTER);
    } else if(c.wifiNetworks.empty()){
        Txt(L"Recherche de r\u00E9seaux\u2026", m_fS,
            {px+pad,iy+8,px+pw-pad,iy+26}, K::GR, fade*.50f, DWRITE_TEXT_ALIGNMENT_CENTER);
    } else {
        for(int i=0; i<(int)c.wifiNetworks.size() && iy+itemH<ph-8; ++i){
            const auto& n = c.wifiNetworks[i];
            bool isCon = n.connected;
            bool isConnecting = (i==c.wifiConnectingIdx);
            D2D1_COLOR_F bg = isCon
                ? D2D1::ColorF(0.f,0.30f,0.65f,0.22f*fade)
                : D2D1::ColorF(1.f,1.f,1.f,0.055f*fade);
            SetB0(bg,1.f);
            m_rt->FillRoundedRectangle({{px+pad,iy,px+pw-pad,iy+itemH},11.f,11.f}, m_b0);
            if(isCon){
                SetB1({0.2f,0.6f,1.f,.20f*fade},1.f);
                m_rt->DrawRoundedRectangle({{px+pad,iy,px+pw-pad,iy+itemH},11.f,11.f}, m_b1, .65f);
            }
            float icX=px+pad+20.f, icY=iy+itemH*.5f;
            SetB0(isCon?K::BLU:D2D1::ColorF(0.25f,0.25f,0.30f,1.f),
                  isCon?fade*.88f:fade*.48f);
            m_rt->FillEllipse({{icX,icY},16.f,16.f}, m_b0);
            if(m_fI14)
                Txt(L"\uE701", m_fI14, {icX-9,icY-9,icX+9,icY+9},
                    K::WHITE, fade*.88f, DWRITE_TEXT_ALIGNMENT_CENTER);
            float tx2=px+pad+44.f;
            Txt(n.ssid, m_fT, {tx2,iy+7,px+pw-pad-52,iy+23}, K::WHITE, fade*.92f);
            const wchar_t* sub = isCon ? L"CONNECT\u00C9"
                               : isConnecting ? L"CONNEXION\u2026"
                               : n.secured ? L"S\u00E9curis\u00E9" : L"Ouvert";
            Txt(sub, m_fX, {tx2,iy+25,px+pw-pad-52,iy+38},
                isCon?K::BLU:K::GR, fade*(isCon?.72f:.48f));
            DrawSignalBars(px+pw-pad-16.f, icY, n.signal, fade);
            iy += itemH+gap;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawBluetoothList — liste des appareils Bluetooth appairés (miroir Wi-Fi).
//  Header : flèche retour (gauche) + « Bluetooth » + interrupteur on/off (droite).
//  Chaque appareil connecté affiche « CONNECTÉ » en dessous.
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawBluetoothList(float px, float pw, float ph, const IslandContent& c) {
    float fade = std::clamp((CuEase(c.animT)-.20f)/.80f, 0.f, 1.f);
    float pad=14.f, hY=14.f;
    // Flèche retour
    SetB0({1,1,1,.07f*fade}); m_rt->FillEllipse({{px+pad+10,hY+11},12.f,12.f}, m_b0);
    if(m_fI14)
        Txt(L"", m_fI14, {px+pad,hY+3,px+pad+24,hY+21},
            K::WHITE, fade*.82f, DWRITE_TEXT_ALIGNMENT_CENTER);
    Txt(L"Bluetooth", m_fT, {px+pad+28,hY+2,px+pw-pad-64,hY+20}, K::WHITE, fade);

    // Interrupteur iOS (haut-droite) — activer/désactiver le Bluetooth
    float tglW=40.f, tglH=22.f, tglX=px+pw-pad-tglW, tglY=hY;
    float tcy=tglY+tglH*.5f;
    D2D1_COLOR_F tgBg = c.bluetoothEnabled
        ? D2D1::ColorF(0.f,0.47f,1.f,1.f) : D2D1::ColorF(0.26f,0.26f,0.28f,1.f);
    SetB0(tgBg, fade*.92f);
    m_rt->FillRoundedRectangle({{tglX,tglY,tglX+tglW,tglY+tglH},tglH*.5f,tglH*.5f}, m_b0);
    float knobX = c.bluetoothEnabled ? tglX+tglW-tglH*.5f : tglX+tglH*.5f;
    SetB0(K::WHITE, fade);
    m_rt->FillEllipse({{knobX,tcy}, tglH*.5f-2.f, tglH*.5f-2.f}, m_b0);

    float sepY=hY+26.f;
    SetB1({1,1,1,.06f*fade}); m_rt->DrawLine({px+pad,sepY},{px+pw-pad,sepY}, m_b1, .45f);
    float itemH=48.f, gap=5.f, iy=sepY+8.f;

    if(!c.bluetoothEnabled){
        Txt(L"Bluetooth désactivé", m_fS,
            {px+pad,iy+8,px+pw-pad,iy+26}, K::GR, fade*.55f, DWRITE_TEXT_ALIGNMENT_CENTER);
    } else if(c.btDevices.empty()){
        Txt(c.btScanning ? L"Recherche des appareils…" : L"Aucun appareil trouvé", m_fS,
            {px+pad,iy+8,px+pw-pad,iy+26}, K::GR, fade*.55f, DWRITE_TEXT_ALIGNMENT_CENTER);
    } else {
        // Sections « Appareils couplés » / « Disponibles » + défilement (clip).
        m_rt->PushAxisAlignedClip({px+pad, sepY+3, px+pw-pad, ph-6}, D2D1_ANTIALIAS_MODE_ALIASED);
        float yy = sepY+8.f - c.btScrollY;
        bool hdrPaired=false, hdrAvail=false;
        for(int i=0; i<(int)c.btDevices.size(); ++i){
            const auto& d = c.btDevices[i];
            if(d.paired && !hdrPaired){ hdrPaired=true;
                Txt(L"APPAREILS COUPLÉS", m_fX, {px+pad+4,yy,px+pw-pad,yy+14}, K::GR, fade*.55f); yy+=22.f; }
            if(!d.paired && !hdrAvail){ hdrAvail=true;
                Txt(L"DISPONIBLES", m_fX, {px+pad+4,yy+4,px+pw-pad,yy+18}, K::GR, fade*.55f); yy+=26.f; }
            bool isCon = d.connected;
            bool vis = (yy+itemH > sepY && yy < ph);
            iy = yy;
            if(vis){
            D2D1_COLOR_F bg = isCon
                ? D2D1::ColorF(0.f,0.30f,0.65f,0.22f*fade)
                : D2D1::ColorF(1.f,1.f,1.f,0.055f*fade);
            SetB0(bg,1.f);
            m_rt->FillRoundedRectangle({{px+pad,iy,px+pw-pad,iy+itemH},11.f,11.f}, m_b0);
            if(isCon){
                SetB1({0.2f,0.6f,1.f,.20f*fade},1.f);
                m_rt->DrawRoundedRectangle({{px+pad,iy,px+pw-pad,iy+itemH},11.f,11.f}, m_b1, .65f);
            }
            float icX=px+pad+20.f, icY=iy+itemH*.5f;
            SetB0(isCon?K::BLU:D2D1::ColorF(0.25f,0.25f,0.30f,1.f),
                  isCon?fade*.88f:fade*.48f);
            m_rt->FillEllipse({{icX,icY},16.f,16.f}, m_b0);
            if(m_fI14)
                Txt(L"", m_fI14, {icX-9,icY-9,icX+9,icY+9},
                    K::WHITE, fade*.88f, DWRITE_TEXT_ALIGNMENT_CENTER);
            float tx2=px+pad+44.f;
            if(isCon){
                Txt(d.name, m_fT, {tx2,iy+7,px+pw-pad-16,iy+23}, K::WHITE, fade*.92f);
                Txt(L"CONNECTÉ", m_fX, {tx2,iy+25,px+pw-pad-16,iy+38},
                    K::BLU, fade*.72f);
            } else {
                Txt(d.name, m_fT, {tx2,iy+16,px+pw-pad-16,iy+34}, K::WHITE, fade*.88f);
            }
            }  // fin if(vis)
            yy += itemH+gap;
        }
        m_rt->PopAxisAlignedClip();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawSignalBars
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawSignalBars(float cx, float cy, int quality, float alpha) {
    const int BARS=4; float bw=3.f, gap=2.f;
    float totalW=BARS*bw+(BARS-1)*gap, x=cx-totalW*.5f;
    for(int i=0; i<BARS; ++i){
        float bh=4.f+i*4.f, by=cy+8.f-bh;
        bool lit = (quality >= (i+1)*25);
        SetB0(lit?K::WHITE:K::GR, alpha*(lit?.68f:.22f));
        m_rt->FillRoundedRectangle({{x,by,x+bw,cy+8.f},1.2f,1.2f}, m_b0);
        x += bw+gap;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawTogglePill — P1 : style Apple iOS toggle
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawTogglePill(float x, float y, float w, float h,
                               const wchar_t* icon, const wchar_t* label,
                               const wchar_t* sub, bool active, float alpha) {
    // Fond — Apple: bleu foncé si actif, gris très sombre si inactif
    D2D1_COLOR_F bg = active
        ? D2D1::ColorF(0.06f,0.16f,0.32f,1.f)
        : D2D1::ColorF(0.11f,0.11f,0.13f,1.f);
    SetB0(bg, alpha*.96f);
    m_rt->FillRoundedRectangle({{x,y,x+w,y+h},h*.45f,h*.45f}, m_b0);

    // Bordure légère
    D2D1_COLOR_F border = active
        ? D2D1::ColorF(0.18f,0.55f,1.f,.25f)
        : D2D1::ColorF(1.f,1.f,1.f,.06f);
    SetB1(border, alpha);
    m_rt->DrawRoundedRectangle({{x,y,x+w,y+h},h*.45f,h*.45f}, m_b1, 0.55f);

    float iconX=x+13.f, cy2=y+h*.5f;
    if(icon && m_fI14)
        Txt(icon, m_fI14, {iconX-8,cy2-9,iconX+10,cy2+9},
            active?K::BLU:K::GR, alpha*.85f, DWRITE_TEXT_ALIGNMENT_CENTER);
    float tx3 = (icon ? iconX+14 : x+9);
    if(label && label[0]) Txt(label, m_fT,  {tx3,y+5,  x+w-6,y+h*.5f+3}, K::WHITE, alpha*.90f);
    if(sub   && sub[0])   Txt(sub,   m_fX,  {tx3,y+h*.5f+1,x+w-6,y+h-5},
        active?K::BLU:K::GR, alpha*.55f);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawSlider — P1 : thumb plus gros, plus visible Apple style
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawSlider(float x, float y, float w, float val,
                           const wchar_t* icon, float alpha) {
    float th=8.f, ty2=y+7.f;
    SetB0({1,1,1,0.09f*alpha});
    m_rt->FillRoundedRectangle({{x,ty2,x+w,ty2+th},th*.5f,th*.5f}, m_b0);
    float fw = w * std::clamp(val,0.f,1.f);
    if(fw>th){
        SetB0({1,1,1,0.78f*alpha});
        m_rt->FillRoundedRectangle({{x,ty2,x+fw,ty2+th},th*.5f,th*.5f}, m_b0);
    }
    float thumbX=x+fw;
    // P1 : thumb blanc avec ombre subtile (style iOS slider)
    SetB0({0,0,0,0.18f*alpha});
    m_rt->FillEllipse({{thumbX+0.5f, ty2+th*.5f+0.5f},10.f,10.f}, m_b0);  // ombre
    SetB0(K::WHITE, alpha*.96f);
    m_rt->FillEllipse({{thumbX, ty2+th*.5f},10.f,10.f}, m_b0);
    if(icon && m_fI14)
        Txt(icon, m_fI14, {x+w+7,ty2-4,x+w+26,ty2+th+5},
            K::GR, alpha*.68f, DWRITE_TEXT_ALIGNMENT_CENTER);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawArc (conservé pour usage éventuel futur)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawArc(float cx, float cy, float r, float val,
                        D2D1_COLOR_F tk, D2D1_COLOR_F fl, float sw) {
    const int S=64;
    float ST=-(float)M_PI*0.5f, SW=(float)M_PI*2.0f;
    for(int i=0;i<S;++i){
        float a1=ST+SW*(float)i/S, a2=ST+SW*(float)(i+1)/S;
        if(tk.a>0.01f){ SetB0(tk); m_rt->DrawLine(
            {cx+cosf(a1)*r,cy+sinf(a1)*r},{cx+cosf(a2)*r,cy+sinf(a2)*r},m_b0,sw); }
    }
    int fs=(int)(S*std::clamp(val,0.f,1.f));
    for(int i=0;i<fs;++i){
        float a1=ST+SW*(float)i/S, a2=ST+SW*(float)(i+1)/S;
        SetB0(fl); m_rt->DrawLine(
            {cx+cosf(a1)*r,cy+sinf(a1)*r},{cx+cosf(a2)*r,cy+sinf(a2)*r},m_b0,sw*1.1f);
    }
}

void Renderer::DrawBar(float x, float y, float w, float h,
                        float val, D2D1_COLOR_F col) {
    SetB0({1,1,1,.08f}); m_rt->FillRoundedRectangle({{x,y,x+w,y+h},h*.5f,h*.5f},m_b0);
    float fw=(std::max)(h, w*std::clamp(val,0.f,1.f));
    SetB0(col); m_rt->FillRoundedRectangle({{x,y,x+fw,y+h},h*.5f,h*.5f},m_b0);
}

void Renderer::DrawStat(float x, float y, float w, const wchar_t* lbl,
                         float val, D2D1_COLOR_F col, const wchar_t* unit) {
    Txt(lbl, m_fX, {x,y,x+28,y+14}, K::GR, .55f);
    if(val>0.f) DrawBar(x+32, y+4, w-68, 5, val, col);
    wchar_t b[32]={};
    if(val>0.f) swprintf_s(b, L"%.0f%s", val*100.f, unit);
    else        wcsncpy_s(b, unit, 31);
    Txt(b, m_fX, {x+w-30,y,x+w,y+14}, K::GR2, .75f, DWRITE_TEXT_ALIGNMENT_TRAILING);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawBat — P0-FIX : SetB0 explicite avant chaque Fill
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawBat(float x, float y, float pct, bool ch) {
    const float BW=20.f, BH=10.f;
    D2D1_COLOR_F white = {0.9f,0.9f,0.9f,1.0f};
    D2D1_COLOR_F col   = (pct<20.f) ? K::RED : (ch ? K::PUR : white);

    // Corps (fond semi-transparent) — SetB0 REQUIS avant le Fill
    SetB0({1,1,1,0.15f});
    m_rt->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(x,y,x+BW,y+BH),2.f,2.f), m_b0);

    // Remplissage dynamique
    float fw = std::clamp(pct/100.f, 0.f, 1.f) * (BW-2.f);
    if(fw > 0.f){
        SetB0(col, 0.9f);
        m_rt->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(x+1.f,y+1.f,x+1.f+fw,y+BH-1.f),1.5f,1.5f), m_b0);
    }

    // Embout
    SetB0({1,1,1,0.40f});
    m_rt->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(x+BW+1.f,y+BH*0.3f,x+BW+3.f,y+BH*0.7f),1.f,1.f), m_b0);

    // Éclair si en charge
    if(ch){
        SetB0({1.f,1.f,0.2f,0.9f});
        float mx=x+BW*.5f, ty2=y+1.5f, by2=y+BH-1.5f, my=y+BH*.5f;
        m_rt->DrawLine({mx+2.f,ty2},{mx-1.f,my},   m_b0, 1.2f);
        m_rt->DrawLine({mx-1.f,my}, {mx+1.5f,my},  m_b0, 1.2f);
        m_rt->DrawLine({mx+1.5f,my},{mx-1.5f,by2},  m_b0, 1.2f);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Txt
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::Txt(const std::wstring& t, IDWriteTextFormat* f,
                    D2D1_RECT_F rc, D2D1_COLOR_F col, float op,
                    DWRITE_TEXT_ALIGNMENT al) {
    if(!f||!m_b0||t.empty()) return;
    if(rc.right<=rc.left+1 || rc.bottom<=rc.top+1) return;
    if(f->GetTextAlignment()!=al) f->SetTextAlignment(al);
    SetB0(col, op);
    m_rt->DrawText(t.c_str(), (UINT32)t.size(), f, rc, m_b0,
                   D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
