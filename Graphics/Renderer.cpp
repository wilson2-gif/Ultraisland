#include "Renderer.h"
#include <cmath>
#include <algorithm>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ── Palette Apple Dynamic Island ─────────────────────────────────────────────
namespace K {
    const D2D1_COLOR_F BG    = {0.0f,0.0f,0.0f,1.0f};
    const D2D1_COLOR_F WHITE = {0.940f,0.950f,0.975f,1.00f};
    const D2D1_COLOR_F GR    = {0.440f,0.460f,0.520f,1.00f};
    const D2D1_COLOR_F GR2   = {0.660f,0.675f,0.720f,1.00f};
    const D2D1_COLOR_F GRN   = {0.196f,0.875f,0.455f,1.00f};
    const D2D1_COLOR_F BLU   = {0.275f,0.620f,1.000f,1.00f};
    const D2D1_COLOR_F ORG   = {1.000f,0.465f,0.175f,1.00f};
    const D2D1_COLOR_F RED   = {1.000f,0.265f,0.265f,1.00f};
    const D2D1_COLOR_F PUR   = {0.648f,0.390f,1.000f,1.00f};
}
static float Lf(float a,float b,float t){return a+(b-a)*t;}
static float SpEase(float t){
    if(t<=0)return 0;if(t>=1)return 1;
    const float c=(float)(2.0*M_PI/3.0);
    return powf(2.f,-9.f*t)*sinf((t*11.f-.8f)*c)+1.f;}
static float CuEase(float t){float u=1-t;return 1-u*u*u;}

Renderer::Renderer() {}
Renderer::~Renderer(){Release();}

bool Renderer::Initialize(HWND hwnd)
{
    if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory1),(void**)&m_f))) return false;
    if(FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory3),(IUnknown**)&m_dw))) return false;
        
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&m_wicFactory)))) { /* ignoré */ }

    auto MF=[&](const wchar_t* face,float sz,DWRITE_FONT_WEIGHT w,IDWriteTextFormat** o){
        m_dw->CreateTextFormat(face,nullptr,w,DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,sz,L"",o);
        if(*o){(*o)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
               (*o)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);}
    };
    MF(L"Segoe UI Variable Display",14.f,DWRITE_FONT_WEIGHT_SEMI_BOLD,&m_fT);
    MF(L"Segoe UI Variable Display",17.f,DWRITE_FONT_WEIGHT_BOLD,     &m_fTBig);
    MF(L"Segoe UI Variable Text",12.f,DWRITE_FONT_WEIGHT_NORMAL,   &m_fS);
    MF(L"Segoe UI Variable Text",10.f,DWRITE_FONT_WEIGHT_NORMAL,   &m_fX);
    MF(L"Segoe UI Variable Display",13.f,DWRITE_FONT_WEIGHT_SEMI_BOLD,&m_fTSemi);
    
    // Heure personnalisée : Book Antiqua, Gras, Italique
    m_dw->CreateTextFormat(L"Book Antiqua", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_ITALIC,
        DWRITE_FONT_STRETCH_NORMAL, 13.f, L"", &m_fC);
    if(m_fC){
        m_fC->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        m_fC->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_fC->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    MF(L"Segoe UI Variable Display",28.f,DWRITE_FONT_WEIGHT_THIN,     &m_fH);
    MF(L"Segoe MDL2 Assets",13.f,DWRITE_FONT_WEIGHT_NORMAL,   &m_fI14);
    MF(L"Segoe MDL2 Assets",20.f,DWRITE_FONT_WEIGHT_NORMAL,   &m_fI20);
    if(m_fX) m_fX->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);

    RECT rc; GetClientRect(hwnd,&rc);
    D2D1_RENDER_TARGET_PROPERTIES rtp=D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
    D2D1_HWND_RENDER_TARGET_PROPERTIES hw=D2D1::HwndRenderTargetProperties(
        hwnd,D2D1::SizeU(rc.right-rc.left,rc.bottom-rc.top));
    if(FAILED(m_f->CreateHwndRenderTarget(rtp,hw,&m_rt))) return false;
    return CreateDevRes();
}

bool Renderer::CreateDevRes(){
    if(!m_rt)return false;
    m_rt->CreateSolidColorBrush(K::BG,    &m_b0);
    m_rt->CreateSolidColorBrush(K::WHITE, &m_b1);
    RebuildGrads(); return m_b0!=nullptr;
}

void Renderer::RebuildGrads(){
    auto SR=[](auto& p){if(p){p->Release();p=nullptr;}};
    SR(m_bGlass);SR(m_bSd);SR(m_bGl);SR(m_bBorder);
    auto GS=[&](D2D1_GRADIENT_STOP* s,int n,ID2D1GradientStopCollection** c){
        m_rt->CreateGradientStopCollection(s,n,c);};

    // Liquid glass tint (fond vitrifié)
    {D2D1_GRADIENT_STOP s[]={{0,{1,1,1,0.03f}},{1,{1,1,1,0.18f}}};
     ID2D1GradientStopCollection*c=nullptr;GS(s,2,&c);
     if(c){m_rt->CreateLinearGradientBrush(
         D2D1::LinearGradientBrushProperties({0,0},{0,1}),c,&m_bGlass);c->Release();}}

    // Shadow
    {D2D1_GRADIENT_STOP s[]={{0,{0,0,0,0}},{1,{0,0,0,.20f}}};
     ID2D1GradientStopCollection*c=nullptr;GS(s,2,&c);
     if(c){m_rt->CreateLinearGradientBrush(
         D2D1::LinearGradientBrushProperties({0,0},{0,1}),c,&m_bSd);c->Release();}}

    // Inner glow (top)
    {D2D1_GRADIENT_STOP s[]={{0,{1,1,1,.065f}},{1,{1,1,1,0}}};
     ID2D1GradientStopCollection*c=nullptr;GS(s,2,&c);
     if(c){m_rt->CreateRadialGradientBrush(
         D2D1::RadialGradientBrushProperties({210,0},{0,0},260,55),c,&m_bGl);c->Release();}}

    // Bordure réflexion glass
    {D2D1_GRADIENT_STOP s[]={{0,{1,1,1,0.40f}},{0.5f,{1,1,1,0.12f}},{1,{1,1,1,0.0f}}};
     ID2D1GradientStopCollection*c=nullptr;GS(s,3,&c);
     if(c){m_rt->CreateLinearGradientBrush(
         D2D1::LinearGradientBrushProperties({0,0},{1,1}),c,&m_bBorder);c->Release();}}
}

void Renderer::DropDevRes(){
    auto SR=[](auto& p){if(p){p->Release();p=nullptr;}};
    SR(m_b0);SR(m_b1);SR(m_bGlass);SR(m_bSd);SR(m_bGl);SR(m_bBorder);
    SR(m_pillGeometry);
}

void Renderer::Release(){
    DropDevRes();
    auto SR=[](auto& p){if(p){p->Release();p=nullptr;}};
    SR(m_fT);SR(m_fS);SR(m_fX);SR(m_fC);SR(m_fH);SR(m_fI14);SR(m_fI20);SR(m_fTBig);SR(m_fTSemi);
    SR(m_rt);SR(m_dw);SR(m_f);
    SR(m_albumArt); SR(m_wicFactory);
}

void Renderer::UpdateAlbumArt(const std::vector<uint8_t>& data) {
    if (data.empty()) {
        if (m_albumArt) { m_albumArt->Release(); m_albumArt = nullptr; }
        m_currentThumbnailData.clear();
        return;
    }
    if (data == m_currentThumbnailData && m_albumArt) return;
    if (m_albumArt) { m_albumArt->Release(); m_albumArt = nullptr; }
    m_currentThumbnailData = data;
    if (!m_wicFactory || !m_rt) return;

    IWICStream* stream = nullptr;
    if (SUCCEEDED(m_wicFactory->CreateStream(&stream))) {
        if (SUCCEEDED(stream->InitializeFromMemory(const_cast<uint8_t*>(data.data()), (DWORD)data.size()))) {
            IWICBitmapDecoder* decoder = nullptr;
            if (SUCCEEDED(m_wicFactory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder))) {
                IWICBitmapFrameDecode* frame = nullptr;
                if (SUCCEEDED(decoder->GetFrame(0, &frame))) {
                    IWICFormatConverter* conv = nullptr;
                    if (SUCCEEDED(m_wicFactory->CreateFormatConverter(&conv))) {
                        if (SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeMedianCut)))
                            m_rt->CreateBitmapFromWicBitmap(conv, nullptr, &m_albumArt);
                        conv->Release();
                    }
                    frame->Release();
                }
                decoder->Release();
            }
        }
        stream->Release();
    }
}

void Renderer::Resize(UINT w,UINT h){if(m_rt)m_rt->Resize(D2D1::SizeU(w,h));}
void Renderer::SetB0(D2D1_COLOR_F c,float a){if(m_b0){c.a*=a;m_b0->SetColor(c);}}
void Renderer::SetB1(D2D1_COLOR_F c,float a){if(m_b1){c.a*=a;m_b1->SetColor(c);}}

// ─────────────────────────────────────────────────────────────────────────────
//  Slide clip helpers — effet glissement entre onglets
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::PushSlideClip(float px, float pw, float ph, float slideX)
{
    m_rt->PushAxisAlignedClip({px, 0.f, px + pw, ph}, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (fabsf(slideX) > 0.5f) {
        D2D1_MATRIX_3X2_F tr = D2D1::Matrix3x2F::Translation(slideX, 0.f);
        m_rt->SetTransform(tr);
    }
}
void Renderer::PopSlideClip(float slideX)
{
    if (fabsf(slideX) > 0.5f)
        m_rt->SetTransform(D2D1::Matrix3x2F::Identity());
    m_rt->PopAxisAlignedClip();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Draw — point d'entrée
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::Draw(const IslandContent& c)
{
    if(!m_rt)return;
    if(!m_b0){CreateDevRes();if(!m_b0)return;}
    m_rt->BeginDraw();
    m_rt->Clear(D2D1::ColorF(0,0,0,0));
    D2D1_SIZE_F sz=m_rt->GetSize();

    float pw=c.pillW, ph=c.pillH;
    float px=(sz.width-pw)*.5f;

    // 1. Dessiner la forme du Pill (Le Conteneur Parent)
    DrawLiquidPill(px,pw,ph,c);

    // 2. Appliquer le Masquage (Clipping)
    // Tout ce qui est dessiné après sera limité par la forme du Pill
    if (m_pillGeometry) {
        ID2D1Layer* layer = nullptr;
        m_rt->CreateLayer(nullptr, &layer);
        if (layer) {
            m_rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), m_pillGeometry), layer);

            float e=SpEase(c.animT);
            float fade=std::clamp((e-.38f)/.62f,0.f,1.f);

            switch(c.state){
            case IslandState::MusicExpanded:  DrawMusicFull(px,pw,ph,c);  break;
            case IslandState::MusicQueue:     DrawMusicQueue(px,pw,ph,c); break;
            case IslandState::NotifList:      DrawNotifList(px,pw,ph,c);  break;
            case IslandState::NotifExpanded:
            case IslandState::CollapsingNotif:DrawNotif(px,pw,ph,c);      break;
            case IslandState::HUDVolume:
            case IslandState::HUDBrightness:
            case IslandState::HUDNetwork:     DrawHUD(px,pw,ph,c);        break;
            case IslandState::SystemExpanded: DrawSystem(px,pw,ph,c);     break;
            case IslandState::WifiList:       DrawWifiList(px,pw,ph,c);   break;
            case IslandState::Expanding:
                if(fade>.4f && c.musicTitle.size()>0) DrawMusicCompact(px,pw,ph,c);
                break;
            case IslandState::Collapsing:     DrawIdle(px,pw,ph,c);       break;
            default:
                if(c.musicTitle.size()>0 && c.state==IslandState::Idle && c.isHovered)
                     DrawMusicCompact(px,pw,ph,c);
                else DrawIdle(px,pw,ph,c);
                break;
            }

            m_rt->PopLayer();
            layer->Release();
        }
    } else {
        // Fallback simple si pas de géométrie
        m_rt->PushAxisAlignedClip(D2D1::RectF(px, 0.f, px + pw, ph), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        DrawIdle(px,pw,ph,c); // Contenu minimal
        m_rt->PopAxisAlignedClip();
    }
    HRESULT hr=m_rt->EndDraw();
    if(hr==D2DERR_RECREATE_TARGET){DropDevRes();CreateDevRes();}
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawLiquidPill — Liquid Glass complet (iOS 26 / Dynamic Island style)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawLiquidPill(float px,float pw,float ph,const IslandContent& c)
{
    float cr = c.cornerRadius;
    float wob = c.wobbleAmount;
    float wobPh = c.wobblePhase;
    float dyL = sinf(wobPh) * wob;
    float dyR = sinf(wobPh + (float)M_PI * 0.8f) * wob;

    // "Ears" - transition concave vers le haut de l'écran (Style Apple / Modern Notch)
    // On réduit l'effet quand l'ile s'agrandit beaucoup.
    float earR = 9.0f;
    float earFade = std::clamp(1.0f - (ph - Pill::H_IDLE) / 60.f, 0.0f, 1.0f);
    float eR = earR * earFade;

    if (m_pillGeometry) { m_pillGeometry->Release(); m_pillGeometry = nullptr; }
    m_f->CreatePathGeometry(&m_pillGeometry);
    ID2D1GeometrySink* sink = nullptr;
    m_pillGeometry->Open(&sink);
    if (sink) {
        sink->SetFillMode(D2D1_FILL_MODE_WINDING);
        // Start from top-left ear outer edge
        sink->BeginFigure({px - eR, 0.f}, D2D1_FIGURE_BEGIN_FILLED);
        
        // Left Ear (concave transition)
        // Control points for a smooth transition from screen edge to pill side
        sink->AddBezier({{px - eR * 0.45f, 0.f}, {px, eR * 0.45f}, {px, eR}});
        
        // Left side down
        sink->AddLine({px, ph - cr});
        
        // Bottom-left corner (Squircle-ish)
        sink->AddBezier({{px, ph - cr * 0.45f + dyL}, {px + cr * 0.45f, ph + dyL}, {px + cr, ph + dyL}});
        
        // Bottom line
        sink->AddLine({px + pw - cr, ph + dyR});
        
        // Bottom-right corner (Squircle-ish)
        sink->AddBezier({{px + pw - cr * 0.45f, ph + dyR}, {px + pw, ph - cr * 0.45f + dyR}, {px + pw, ph - cr}});
        
        // Right side up
        sink->AddLine({px + pw, eR});
        
        // Right Ear (concave transition)
        sink->AddBezier({{px + pw, eR * 0.45f}, {px + pw + eR * 0.45f, 0.f}, {px + pw + eR, 0.f}});

        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        sink->Release();
    }

    // 1 ── Fond NOIR PROFOND
    D2D1_COLOR_F deepBlack = {0.0f, 0.0f, 0.0f, 1.0f};
    SetB0(deepBlack);
    if (m_pillGeometry) m_rt->FillGeometry(m_pillGeometry, m_b0);
    else m_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(px,0,px+pw,ph),cr,cr), m_b0);

    // Les effets de glass et glow sont désactivés pour garder le noir profond pur

    // 4 ── Bordure réflexion
    if (m_bBorder && m_pillGeometry) {
        m_bBorder->SetStartPoint({px, 0.f});
        m_bBorder->SetEndPoint({px + pw, ph});
        m_rt->DrawGeometry(m_pillGeometry, m_bBorder, 0.75f);
    } else {
        SetB1({1,1,1,.085f});
        if (m_pillGeometry) m_rt->DrawGeometry(m_pillGeometry, m_b1, .55f);
    }

    // 5 ── Ombres empilées bas
    if (ph > Pill::H_IDLE * 1.5f && m_bSd) {
        float t01 = std::clamp((ph - Pill::H_IDLE) / (Pill::H_MUSIC - Pill::H_IDLE), 0.f, 1.f);
        float sh = 6.f * t01;
        m_bSd->SetStartPoint({0, ph - sh}); m_bSd->SetEndPoint({0, ph + sh * 1.5f});
        m_bSd->SetOpacity(0.55f);
        D2D1_ROUNDED_RECT sr1 = {{px+4, ph-sh, px+pw-4, ph+sh*1.5f}, cr*.5f, cr*.5f};
        m_rt->FillRoundedRectangle(sr1, m_bSd);
        sh = 24.f * t01;
        m_bSd->SetStartPoint({0, ph - sh * .4f}); m_bSd->SetEndPoint({0, ph + sh});
        m_bSd->SetOpacity(0.08f);
        D2D1_ROUNDED_RECT sr2 = {{px-8, ph-sh*.4f, px+pw+8, ph+sh}, cr, cr};
        m_rt->FillRoundedRectangle(sr2, m_bSd);
        m_bSd->SetOpacity(1.f);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawGlassZone — Zone frosted pour les contrôles
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawGlassZone(float px,float pw,float yStart,float yEnd,float cr,float alpha)
{
    if(!m_pillGeometry||!m_bGlass||yEnd<=yStart) return;
    m_bGlass->SetStartPoint({0,yStart}); m_bGlass->SetEndPoint({0,yEnd});
    m_bGlass->SetOpacity(alpha * 1.2f);
    m_rt->PushAxisAlignedClip({px,yStart,px+pw,yEnd},D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    m_rt->FillGeometry(m_pillGeometry,m_bGlass);
    SetB1({1,1,1,0.18f*alpha});
    m_rt->DrawLine({px,yStart},{px+pw,yStart},m_b1,0.6f);
    m_rt->PopAxisAlignedClip();
    m_bGlass->SetOpacity(1.f);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawIdle — waveform + heure + badges apps + batterie
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawIdle(float px,float pw,float ph,const IslandContent& c)
{
    float cy=ph*.5f;
    float alpha=1.0f;
    if(c.state==IslandState::Collapsing)     alpha=c.animT;
    else if(c.state==IslandState::Expanding) alpha=1.0f-c.animT;

    // 1. Gauche : Waveform (ce qui était là)
    DrawWaveform(px + 23.f, cy, 13.f, c.isMusicPlaying, c.globalT, K::GRN, alpha);

    // 2. Milieu : Heure réelle centrée (Book Antiqua, Gras, Italique, Bleu Clair)
    if(!c.clockTime.empty()) {
        D2D1_COLOR_F lightBlue = D2D1::ColorF(0.529f, 0.808f, 0.922f, 1.0f); // Sky Blue style
        // Ajustement pour hauteur très fine (26px)
        Txt(c.clockTime, m_fC, D2D1::RectF(px, cy - 9.f, px + pw, cy + 9.f), lightBlue, alpha * 0.95f,
            DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // 3. Droite : Batterie réelle (pourcentage Book Antiqua + icône Flat)
    if(c.batteryPercent >= 0){
        float batX = px + pw - 30.f; // Décalé vers la droite
        DrawBat(batX, cy - 5.f, c.batteryPercent, c.batteryCharging);
        
        wchar_t b[8]; swprintf_s(b, L"%.0f%%", c.batteryPercent);
        // Utilisation de m_fC (Book Antiqua) pour le pourcentage, ajusté verticalement
        float txtWidth = 45.f;
        Txt(b, m_fC, D2D1::RectF(batX - txtWidth - 8.f, cy - 9.f, batX - 8.f, cy + 9.f), K::GR2, alpha * 0.88f,
            DWRITE_TEXT_ALIGNMENT_TRAILING);
    }

    // Badges si présents
    if(c.totalUnreadCount > 0)
        DrawAppBadges(px, pw, ph, c);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawAppBadges — clusters de dots colorés par app + compteur total
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawAppBadges(float px,float pw,float ph,const IslandContent& c)
{
    if(c.appBadges.empty()) return;

    float cy  = ph * .5f;
    float ease = SpEase(std::clamp(c.badgePulseT, 0.f, 1.f));
    float anim = ease;

    const float DOT_R   = 5.5f;
    const float OVERLAP = 3.5f;
    const float STEP    = DOT_R * 2.f - OVERLAP;
    const int   MAX_DOTS = 3;

    int ndots = (int)std::min(c.appBadges.size(), (size_t)MAX_DOTS);
    bool hasExtra = (int)c.appBadges.size() > MAX_DOTS;

    float bx0 = px + pw * 0.64f;

    SetB0({1,1,1, 0.12f * anim});
    m_rt->DrawLine({bx0 - 5.f, cy - 8.f}, {bx0 - 5.f, cy + 8.f}, m_b0, 0.6f);

    for(int i = ndots - 1; i >= 0; --i){
        const auto& badge = c.appBadges[i];
        float dotX = bx0 + i * STEP + DOT_R;

        SetB0(K::BG, anim * 0.9f);
        m_rt->FillEllipse({{dotX, cy}, DOT_R + 1.5f, DOT_R + 1.5f}, m_b0);

        SetB0(badge.color, anim * 0.92f);
        m_rt->FillEllipse({{dotX, cy}, DOT_R, DOT_R}, m_b0);

        if(badge.count > 1 && ndots <= 2){
            wchar_t cntStr[6];
            if(badge.count > 99) wcscpy_s(cntStr, L"99+");
            else swprintf_s(cntStr, L"%d", badge.count);
            float bx2 = dotX + DOT_R * 0.55f;
            float by2 = cy   - DOT_R * 0.55f;
            float br  = (badge.count > 9) ? 5.5f : 4.5f;
            SetB0(K::RED, anim * 0.95f);
            m_rt->FillEllipse({{bx2, by2}, br, br}, m_b0);
            SetB1({1,1,1, anim});
            if(m_fX) Txt(cntStr, m_fX,
                {bx2 - br, by2 - br, bx2 + br, by2 + br},
                K::WHITE, anim * 0.95f, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }

    if(hasExtra || c.totalUnreadCount > 9){
        float extraX = bx0 + ndots * STEP + DOT_R + 2.f;
        wchar_t extra[8];
        if(hasExtra)
            swprintf_s(extra, L"+%d", (int)c.appBadges.size() - MAX_DOTS);
        else
            swprintf_s(extra, L"+%d", c.totalUnreadCount - 9);
        float tw2 = 18.f;
        SetB0({1,1,1, 0.12f * anim});
        m_rt->FillRoundedRectangle({{extraX - 2, cy - DOT_R, extraX + tw2, cy + DOT_R},
                                    DOT_R, DOT_R}, m_b0);
        Txt(extra, m_fX,
            {extraX, cy - DOT_R, extraX + tw2, cy + DOT_R},
            K::GR2, anim * 0.75f, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    if(c.totalUnreadCount > 0 && c.totalUnreadCount <= 9){
        float pulse = 1.f + 0.08f * sinf(c.globalT * 4.f) * (1.f - anim);
        float bx2   = bx0 - 11.f;
        float br    = 5.5f * pulse;
        SetB0(K::RED, anim * 0.88f);
        m_rt->FillEllipse({{bx2, cy - 9.f}, br, br}, m_b0);
        wchar_t tot[4]; swprintf_s(tot, L"%d", c.totalUnreadCount);
        Txt(tot, m_fX,
            {bx2 - br - 1, cy - 9.f - br, bx2 + br + 1, cy - 9.f + br},
            K::WHITE, anim * 0.95f, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawNotifBadge
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawNotifBadge(float cx,float cy,float radius,D2D1_COLOR_F color,
                               const std::wstring& glyph,float alpha)
{
    SetB0(color,alpha);
    m_rt->FillEllipse({{cx,cy},radius,radius},m_b0);
    if(!glyph.empty())
        Txt(glyph,m_fI14,{cx-10,cy-10,cx+10,cy+10},K::WHITE,alpha,DWRITE_TEXT_ALIGNMENT_CENTER);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawNotif — toast compact
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawNotif(float px,float pw,float ph,const IslandContent& c)
{
    float cy=ph*.5f;
    bool out=(c.state==IslandState::CollapsingNotif);
    float e=out?1.f-CuEase(c.animT):CuEase(c.animT);
    float slideX=(1.0f-e)*-20.f;

    if(c.bellShakeT>0.f && c.notifIconGlyph==L"\uEA8F"){
        float angle=sinf(c.bellShakeT*6.f*(float)M_PI)*15.f*expf(-3.f*c.bellShakeT);
        D2D1_MATRIX_3X2_F old; m_rt->GetTransform(&old);
        m_rt->SetTransform(D2D1::Matrix3x2F::Rotation(angle,{px+18+slideX,cy})*old);
        DrawNotifBadge(px+18+slideX,cy,12.f,c.notifAppColor,c.notifIconGlyph,e);
        m_rt->SetTransform(old);
    } else {
        DrawNotifBadge(px+18+slideX,cy,12.f,c.notifAppColor,c.notifIconGlyph,e);
    }

    std::wstring app=c.notifAppName.empty()?L"Notification":c.notifAppName;
    // FIXED: Keep text box width constant - only reveal by opacity (e)
    Txt(app,m_fT,{px+38,cy-11,px+pw-40,cy+11},K::WHITE,e);

    if(!c.clockTime.empty())
        Txt(c.clockTime,m_fC,{px+pw-50,cy-11,px+pw-8,cy+11},K::GR,e*.82f,
            DWRITE_TEXT_ALIGNMENT_TRAILING);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawNotifList — panneau historique adaptatif
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawNotifList(float px,float pw,float ph,const IslandContent& c)
{
    float fade=std::clamp((CuEase(c.animT)-.22f)/.78f,0.f,1.f);

    PushSlideClip(px, pw, ph, c.tabSlideX);

    DrawTopIcons(px,pw,fade,c.activeMenuIndex,c.bellShakeT, c.tabSlideX);

    float pad=14.f, iy=38.f;

    float btnW=110.f,btnH=22.f;
    float btnX=px+pw-pad-btnW;
    SetB0({1,1,1,0.09f*fade});
    m_rt->FillRoundedRectangle({{btnX,iy+1,btnX+btnW,iy+1+btnH},11.f,11.f},m_b0);
    Txt(L"Tout effacer",m_fX,{btnX,iy+3,btnX+btnW,iy+1+btnH},K::GR,fade*.85f,
        DWRITE_TEXT_ALIGNMENT_CENTER);

    iy += 30.f;
    float cx = px + pw * 0.5f;
    float textHalf = pw * 0.5f;

    if(c.notifHistory.empty()){
        float cy=iy+(ph-iy)*.5f;
        if(m_fI20) Txt(L"\uEA8F",m_fI20,{px+pw*.5f-14,cy-20,px+pw*.5f+14,cy},
                       K::GR,fade*.25f,DWRITE_TEXT_ALIGNMENT_CENTER);
        Txt(L"Aucune notification",m_fS,{px+pad,cy+4,px+pw-pad,cy+22},
            K::GR,fade*.35f,DWRITE_TEXT_ALIGNMENT_CENTER);
        PopSlideClip(c.tabSlideX);
        return;
    }

    float itemH=56.f,gap=6.f;
    int maxN=(int)std::min(c.notifHistory.size(),(size_t)3);

    for(int i=0;i<maxN;++i){
        const auto& ni=c.notifHistory[c.notifHistory.size()-1-i];
        float iy2=iy+i*(itemH+gap);
        if(iy2+itemH>ph-6) break;

        SetB0({1,1,1,0.07f*fade});
        m_rt->FillRoundedRectangle({{px+pad,iy2,px+pw-pad,iy2+itemH},13.f,13.f},m_b0);
        SetB1({1,1,1,0.08f*fade});
        m_rt->DrawRoundedRectangle({{px+pad,iy2,px+pw-pad,iy2+itemH},13.f,13.f},m_b1,0.5f);

        float bx=px+pad+22.f, by=iy2+itemH*.5f;
        SetB0(ni.appColor,fade*.85f);
        m_rt->FillEllipse({{bx,by},16.f,16.f},m_b0);
        if(m_fI14)
            Txt(ni.iconGlyph,m_fI14,{bx-10,by-10,bx+10,by+10},K::WHITE,fade,
                DWRITE_TEXT_ALIGNMENT_CENTER);

        float nleft = cx - textHalf + 46.f;
        float nright = cx + textHalf - 40.f;
        Txt(ni.appName,m_fT,{nleft,iy2+8,nright,iy2+24},K::WHITE,fade*.95f);
        Txt(ni.message,m_fS,{nleft,iy2+26,nright+32,iy2+itemH-8},K::GR,fade*.68f);
        Txt(ni.timeStr,m_fX,{nright-22,iy2+8,nright+18,iy2+22},
            K::GR,fade*.45f,DWRITE_TEXT_ALIGNMENT_TRAILING);
    }

    if(c.notifHistory.size()>3){
        float dotY=ph-10.f;
        for(int i=0;i<3;++i){
            float dx=px+pw*.5f+(i-1)*10.f;
            SetB0({1,1,1, i==0?0.55f:0.20f},fade);
            m_rt->FillEllipse({{dx,dotY},2.f,2.f},m_b0);
        }
    }

    PopSlideClip(c.tabSlideX);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawMusicCompact — player hover
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawMusicCompact(float px,float pw,float ph,const IslandContent& c)
{
    float fade=1.0f;
    if(c.state==IslandState::Expanding)  fade=c.animT;
    if(c.state==IslandState::Collapsing) fade=1.0f-c.animT;
    float cy=ph*.5f;

    float artS=24.f;
    DrawAlbumArt(px+8,cy-artS*.5f,artS,fade,c.albumArtBitmap);

    std::wstring ti=c.musicTitle.empty()?L"Sans titre":c.musicTitle;
    // Keep text rect FIXED in screen center, simply fade it in/out
    Txt(ti,m_fS,{px+40,cy-8,px+pw-40,cy+8},K::WHITE,fade);
    DrawWaveform(px+pw-20,cy,12,c.isMusicPlaying,c.globalT,K::GRN,fade);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawMusicFull — Player complet
//  Layout H_MUSIC=178px :
//   0-24    : icônes menu haut
//   28-84   : album art (56×56) + infos musicales
//   96-122  : barre de progression + timecodes
//   128-178 : zone glass + contrôles
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawMusicFull(float px,float pw,float ph,const IslandContent& c)
{
    float e=SpEase(c.animT);
    float fade=std::clamp((e-.18f)/.82f,0.f,1.f);

    PushSlideClip(px, pw, ph, c.tabSlideX);

    DrawTopIcons(px,pw,fade,c.activeMenuIndex,c.bellShakeT, c.tabSlideX);

    float artX=px+14.f, artY=28.f, artS=56.f;
    DrawAlbumArt(artX,artY,artS,fade,c.albumArtBitmap);

    float tx=artX+artS+12.f;
    // FIXED: Keep text width constant (don't scale with pw) - only fade in/out
    const float tw=150.f;

    std::wstring ti=c.musicTitle.empty()?L"Sans titre":c.musicTitle;
    Txt(ti,m_fTBig,{tx,artY+2,tx+tw,artY+22},K::WHITE,fade);

    std::wstring ar=c.musicArtist.empty()?L"":c.musicArtist;
    Txt(ar,m_fS,{tx,artY+24,tx+tw,artY+40},K::GR2,fade*.85f);

    if(!c.musicSourceApp.empty())
        Txt(L"\u25CF  "+c.musicSourceApp,m_fX,{tx,artY+44,tx+tw,artY+57},{0.55f,0.55f,0.58f,1.f},fade*.55f);

    DrawWaveform(px+pw-26,artY+16,12,c.isMusicPlaying,c.globalT,K::GRN,fade);

    float barY=96.f;
    DrawPBar(px+14,barY,pw-28,c.musicProgress,c.musicCurrentSec,c.musicTotalSec,fade);

    float glassY=128.f;
    DrawGlassZone(px,pw,glassY,ph,c.cornerRadius,fade);
    float ctrlY=glassY+(ph-glassY)*.5f;
    DrawControls(px,pw,ctrlY,c.isMusicPlaying,fade,K::WHITE);

    PopSlideClip(c.tabSlideX);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawMusicQueue — À venir (pistes suivantes)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawMusicQueue(float px,float pw,float ph,const IslandContent& c)
{
    float e=SpEase(c.animT);
    float fade=std::clamp((e-.14f)/.86f,0.f,1.f);

    PushSlideClip(px, pw, ph, c.tabSlideX);
    float cx = px + pw * 0.5f;
    float textHalf = pw * 0.5f;

    DrawTopIcons(px,pw,fade,c.activeMenuIndex,c.bellShakeT, c.tabSlideX);

    float artX=px+14.f, artY=36.f, artS=54.f;
    DrawAlbumArt(artX,artY,artS,fade,c.albumArtBitmap);

    float tx=artX+artS+12.f;
    // FIXED: Keep text width constant (don't scale with pw) - only fade in/out
    const float tw=150.f;
    std::wstring ti=c.musicTitle.empty()?L"Sans titre":c.musicTitle;
    Txt(ti,m_fTBig,{tx,artY+2,tx+tw,artY+22},K::WHITE,fade);
    std::wstring ar=c.musicArtist.empty()?L"":c.musicArtist;
    Txt(ar,m_fS,{tx,artY+24,tx+tw,artY+40},K::GR2,fade*.85f);
    if(!c.musicSourceApp.empty())
        Txt(L"\u25CF  "+c.musicSourceApp,m_fX,{tx,artY+44,tx+tw,artY+56},K::GR,fade*.52f);
    DrawWaveform(px+pw-26,artY+14,12,c.isMusicPlaying,c.globalT,K::GRN,fade);

    float barY=artY+artS+12.f;
    DrawPBar(px+14,barY,pw-28,c.musicProgress,c.musicCurrentSec,c.musicTotalSec,fade);

    float ctrlY=barY+42.f;

    SetB0({1,1,1,.13f*fade});
    m_rt->FillEllipse({{px+48.f,ctrlY},19.f,19.f},m_b0);
    SetB1({1,1,1,.20f*fade});
    m_rt->DrawEllipse({{px+48.f,ctrlY},19.f,19.f},m_b1,.6f);

    DrawControls(px,pw,ctrlY,c.isMusicPlaying,fade,K::WHITE);

    float sepY=ctrlY+36.f;
    SetB0({1,1,1,.07f*fade});
    m_rt->DrawLine({px+14,sepY},{px+pw-14,sepY},m_b0,.5f);

    float qlY=sepY+10.f;
    Txt(L"\u00C0 venir",m_fT,{px+14,qlY,px+pw-14,qlY+18},K::WHITE,fade);

    const auto& tracks = c.upcomingTracks.empty() ? c.queueItems : c.upcomingTracks;

    if(tracks.empty()){
        Txt(L"Aucune piste suivante",m_fS,{px+14,qlY+24,px+pw-14,qlY+42},
            K::GR,fade*.45f,DWRITE_TEXT_ALIGNMENT_CENTER);
        Txt(L"D\u00E9pend de l\u2019application",m_fX,{px+14,qlY+44,px+pw-14,qlY+58},
            K::GR,fade*.30f,DWRITE_TEXT_ALIGNMENT_CENTER);
        PopSlideClip(c.tabSlideX);
        return;
    }

    static const D2D1_COLOR_F ART_COLS[]={
        {0.12f,0.55f,0.22f,1.f},{0.22f,0.10f,0.38f,1.f},
        {0.52f,0.12f,0.12f,1.f},{0.08f,0.22f,0.58f,1.f},{0.55f,0.32f,0.04f,1.f}};

    float itemY=qlY+26.f;
    const float ITEM_H=52.f, ITEM_GAP=6.f, ART_S=38.f;

    for(int i=0;i<(int)tracks.size()&&itemY+ITEM_H<ph-6;++i){
        const auto& item=tracks[i];

        SetB0({1,1,1,.045f*fade});
        m_rt->FillRoundedRectangle({{px+10,itemY,px+pw-10,itemY+ITEM_H},9.f,9.f},m_b0);

        float aX=px+18.f, aY=itemY+(ITEM_H-ART_S)*.5f;
        D2D1_COLOR_F col=ART_COLS[i%5];
        SetB0(col,fade*.90f);
        m_rt->FillRoundedRectangle({{aX,aY,aX+ART_S,aY+ART_S},6.f,6.f},m_b0);
        SetB1({1,1,1,.08f*fade});
        m_rt->DrawRoundedRectangle({{aX,aY,aX+ART_S,aY+ART_S},6.f,6.f},m_b1,.5f);
        for(float r=ART_S*.13f;r<ART_S*.40f;r+=ART_S*.10f){
            SetB0({1,1,1,.04f*fade});
            m_rt->DrawEllipse({{aX+ART_S*.5f,aY+ART_S*.5f},r,r},m_b0,.5f);}

        float nleft_title = cx - textHalf + ART_S + 8.f;
        float nright_title = cx + textHalf - 62.f;
        Txt(item.title,m_fT,{nleft_title,itemY+6,nright_title,itemY+22},K::WHITE,fade);
        Txt(item.artist,m_fX,{nleft_title,itemY+24,nright_title,itemY+38},K::GR,fade*.62f);

        float dhX=px+pw-54.f, dhCY=itemY+ITEM_H*.5f;
        for(int row=0;row<2;++row)for(int col2=0;col2<3;++col2){
            SetB0(K::GR,fade*.38f);
            m_rt->FillEllipse({{dhX+col2*4.5f,dhCY+(row-.5f)*5.f},1.2f,1.2f},m_b0);}

        float xCX=px+pw-26.f, xCY=itemY+ITEM_H*.5f;
        SetB0({1,1,1,.07f*fade}); m_rt->FillEllipse({{xCX,xCY},10.f,10.f},m_b0);
        SetB1(K::GR,fade*.35f); m_rt->DrawEllipse({{xCX,xCY},10.f,10.f},m_b1,.6f);
        Txt(L"\uE711",m_fI14,{xCX-8,xCY-8,xCX+8,xCY+8},K::GR2,fade*.70f,DWRITE_TEXT_ALIGNMENT_CENTER);

        itemY+=ITEM_H+ITEM_GAP;
    }

    if(m_bSd&&m_pillGeometry){
        float fadeH=64.f;
        m_bSd->SetStartPoint({0,ph-fadeH}); m_bSd->SetEndPoint({0,ph+8.f});
        m_bSd->SetOpacity(fade*.75f);
        m_rt->FillGeometry(m_pillGeometry,m_bSd);
        m_bSd->SetOpacity(1.f);
    }

    PopSlideClip(c.tabSlideX);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawTopIcons — 3 icônes menu
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawTopIcons(float px,float pw,float alpha,int activeMenu,
                             float bellShakeT, float /*slideX*/)
{
    float cx=px+pw*.5f;
    struct{float x;const wchar_t*g;}ic[]={{cx-58,L"\uEA8F"},{cx,L"\uEC4F"},{cx+58,L"\uE713"}};
    for(int i=0;i<3;++i){
        D2D1_COLOR_F col=(i==activeMenu)?K::WHITE:K::GR;
        float iconY1=4.f, iconY2=26.f;

        if(i==0&&bellShakeT>0.f){
            float angle=sinf(bellShakeT*6.f*(float)M_PI)*15.f*expf(-3.f*bellShakeT);
            D2D1_MATRIX_3X2_F old; m_rt->GetTransform(&old);
            m_rt->SetTransform(D2D1::Matrix3x2F::Rotation(angle,{ic[i].x,15.f})*old);
            Txt(ic[i].g,m_fI14,{ic[i].x-12,iconY1,ic[i].x+12,iconY2},col,alpha,DWRITE_TEXT_ALIGNMENT_CENTER);
            m_rt->SetTransform(old);
        } else {
            Txt(ic[i].g,m_fI14,{ic[i].x-12,iconY1,ic[i].x+12,iconY2},col,alpha,DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        if(i==activeMenu){
            SetB0(K::WHITE,alpha*.55f);
            m_rt->FillEllipse({{ic[i].x,iconY2+3.f},1.8f,1.8f},m_b0);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawAlbumArt — bitmap ou fallback vinyl
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawAlbumArt(float x,float y,float s,float alpha,ID2D1Bitmap* bmp)
{
    D2D1_ROUNDED_RECT rr={{x,y,x+s,y+s},s*0.15f,s*0.15f};
    if(bmp){
        m_rt->DrawBitmap(bmp,{x,y,x+s,y+s},alpha);
        SetB1({1,1,1,.10f*alpha}); m_rt->DrawRoundedRectangle(rr,m_b1,.55f);
        return;
    }
    SetB0({0.10f,0.08f,0.15f,alpha}); m_rt->FillRoundedRectangle(rr,m_b0);
    auto band=[&](float fy,float fh,float r,float g,float b){
        SetB0({r,g,b,alpha*.32f});
        m_rt->FillRectangle({x+1,y+fy,x+s-1,y+fy+fh},m_b0);};
    band(0,s*.38f,0.52f,0.22f,0.72f);
    band(s*.28f,s*.42f,0.22f,0.42f,0.82f);
    band(s*.58f,s*.42f,0.18f,0.68f,0.58f);
    for(float r=s*.08f;r<s*.42f;r+=s*.09f){
        SetB0({1,1,1,alpha*.04f}); m_rt->DrawEllipse({{x+s*.5f,y+s*.5f},r,r},m_b0,.5f);}
    SetB0({0.06f,0.05f,0.10f,alpha*.8f});
    m_rt->FillEllipse({{x+s*.5f,y+s*.5f},s*.12f,s*.12f},m_b0);
    SetB1({1,1,1,.10f*alpha}); m_rt->DrawRoundedRectangle(rr,m_b1,.55f);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawWaveform — barres équaliseur animées
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawWaveform(float cx,float cy,float maxH,bool pl,
                             float t,D2D1_COLOR_F col,float alpha)
{
    const int N=5; const float BW=2.5f,GAP=2.f,MINH=2.5f;
    static const float PH[]={0.f,1.3f,.65f,1.9f,.4f};
    float tw=N*BW+(N-1)*GAP, sx=cx-tw*.5f;
    for(int i=0;i<N;++i){
        float h=MINH;
        if(pl) h=MINH+(maxH-MINH)*(.5f+.5f*sinf(t*(float)M_PI*5.f+PH[i]));
        float x=sx+i*(BW+GAP),y=cy-h*.5f;
        D2D1_ROUNDED_RECT r={{x,y,x+BW,y+h},1.2f,1.2f};
        SetB0(col,alpha*.92f); m_rt->FillRoundedRectangle(r,m_b0);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawPBar — barre de progression Apple style
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawPBar(float x,float y,float w,float prog,
                         float cur,float tot,float alpha)
{
    float H=3.5f;
    SetB0({1,1,1,.14f*alpha});
    m_rt->FillRoundedRectangle({{x,y,x+w,y+H},H*.5f,H*.5f},m_b0);
    float fw=(std::max)(H,w*std::clamp(prog,0.f,1.f));
    SetB0(K::WHITE,alpha*.88f);
    m_rt->FillRoundedRectangle({{x,y,x+fw,y+H},H*.5f,H*.5f},m_b0);
    SetB0(K::WHITE,alpha);
    m_rt->FillEllipse({{x+fw,y+H*.5f},5.5f,5.5f},m_b0);
    auto FT=[](float s,wchar_t*b){swprintf_s(b,10,L"%d:%02d",(int)(s/60),(int)s%60);};
    wchar_t l[10],r2[10]; FT(cur,l); FT(tot,r2);
    float ty=y+H+7.f;
    Txt(l,m_fX,{x,ty,x+44,ty+14},K::GR,alpha*.72f);
    Txt(r2,m_fX,{x+w-44,ty,x+w,ty+14},K::GR,alpha*.72f,DWRITE_TEXT_ALIGNMENT_TRAILING);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawControls — ≡ |< ⏸ >| □
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawControls(float px,float pw,float cy,bool pl,float alpha,D2D1_COLOR_F iconColor)
{
    if(!m_fI14||!m_fI20) return;
    float cx=px+pw*.5f;
    float xs[]={px+48,cx-76,cx,cx+76,px+pw-48};
    const wchar_t* gl[]={L"\uE8FD",L"\uE892",pl?L"\uE769":L"\uE768",L"\uE893",L"\uE7F4"};

    for(int i=0;i<5;++i){
        bool isMain=(i==2);
        float r=isMain?22.f:16.f, hs=isMain?14.f:10.f;
        IDWriteTextFormat* fmt=isMain?m_fI20:m_fI14;
        float bgA=isMain?.13f:.07f;
        SetB0({iconColor.r,iconColor.g,iconColor.b,bgA*alpha});
        m_rt->FillEllipse({{xs[i],cy},r,r},m_b0);
        Txt(gl[i],fmt,{xs[i]-hs,cy-hs,xs[i]+hs,cy+hs},iconColor,alpha,DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawHUD — volume / luminosité
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawHUD(float px,float pw,float ph,const IslandContent& c)
{
    float fade=std::clamp((CuEase(c.animT)-.25f)/.75f,0.f,1.f);
    float cx=px+pw*.5f, cy=ph*.5f+4;
    D2D1_COLOR_F ac=K::WHITE;
    const wchar_t* gl=L"\uE767";
    switch(c.hudType){
    case HUDType::Brightness: ac={1.f,.92f,.38f,1.f};gl=L"\uE706";break;
    case HUDType::Network:    ac=K::BLU;             gl=L"\uE704";break;
    default: break;}
    DrawArc(cx,cy-6,30,c.hudValue,{1,1,1,.10f},ac,3.6f);
    if(m_fI20) Txt(gl,m_fI20,{cx-13,cy-26,cx+13,cy-4},ac,fade*.78f,DWRITE_TEXT_ALIGNMENT_CENTER);
    Txt(c.hudLabel,m_fH,{cx-38,cy-6,cx+38,cy+24},K::WHITE,fade,DWRITE_TEXT_ALIGNMENT_CENTER);
    const wchar_t* lb=(c.hudType==HUDType::Volume)?L"Volume":
                      (c.hudType==HUDType::Brightness)?L"Luminosit\u00E9":L"R\u00E9seau";
    Txt(lb,m_fX,{cx-48,cy+26,cx+48,cy+38},K::GR,fade*.62f,DWRITE_TEXT_ALIGNMENT_CENTER);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawSystem — panneau paramètres
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawSystem(float px,float pw,float ph,const IslandContent& c)
{
    float fade=std::clamp((CuEase(c.animT)-.22f)/.78f,0.f,1.f);

    PushSlideClip(px, pw, ph, c.tabSlideX);
    DrawTopIcons(px,pw,fade,c.activeMenuIndex,c.bellShakeT, c.tabSlideX);

    float pad=13.f, row1Y=38.f, pillH2=48.f;

    float pw1=(pw-pad*2.f-10.f)*0.38f;
    float pw2=pw1;
    float pw3=pw-pad*2.f-pw1*2.f-10.f;

    DrawTogglePill(px+pad, row1Y, pw1, pillH2,
        L"\uE701",L"Wi-Fi",
        c.wifiEnabled?(c.wifiSSID.empty()?L"Connect\u00E9":c.wifiSSID.c_str()):L"D\u00E9sactiv\u00E9",
        c.wifiEnabled,fade);

    DrawTogglePill(px+pad+pw1+5,row1Y,pw2,pillH2,
        L"\uE702",L"Bluetooth",
        c.bluetoothEnabled?L"Actif":L"D\u00E9sactiv\u00E9",
        c.bluetoothEnabled,fade);

    float px3=px+pad+pw1*2+10;
    DrawTogglePill(px3,row1Y,pw3,pillH2,nullptr,nullptr,nullptr,c.darkModeEnabled,fade);
    if(m_fI20){
        const wchar_t* modeIco=c.darkModeEnabled?L"\uE708":L"\uE793";
        Txt(modeIco,m_fI20,{px3+7,row1Y+8,px3+28,row1Y+pillH2-8},K::WHITE,fade*.85f,DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    {
        float cx3=px3+pw3*.5f+8, cy3=row1Y+pillH2*.5f;
        float tw=32.f, th=17.f, tx=cx3-tw*.5f, ty=cy3-th*.5f;
        D2D1_COLOR_F tgBg=c.darkModeEnabled?D2D1::ColorF(0.f,0.47f,1.f,1.f):D2D1::ColorF(0.26f,0.26f,0.28f,1.f);
        SetB0(tgBg,fade*.9f);
        m_rt->FillRoundedRectangle({{tx,ty,tx+tw,ty+th},th*.5f,th*.5f},m_b0);
        float knobX=c.darkModeEnabled?tx+tw-th*.5f:tx+th*.5f;
        SetB0(K::WHITE,fade);
        m_rt->FillEllipse({{knobX,cy3},th*.5f-2.f,th*.5f-2.f},m_b0);
    }

    float row2Y=row1Y+pillH2+7.f;
    DrawTogglePill(px+pad,row2Y,140.f,38.f,L"\uE706",L"\u00C9clai. nocturne",L"",c.nightLightEnabled,fade);

    float iconW=22.f;
    float sx=px+pad, sw=pw-pad*2.f-iconW-8.f;
    float sy1=row2Y+48.f, sy2=sy1+38.f;
    DrawSlider(sx,sy1,sw,c.systemVolume,L"\uE767",fade);
    DrawSlider(sx,sy2,sw,c.systemBrightness,L"\uE706",fade);

    PopSlideClip(c.tabSlideX);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawWifiList — panneau liste réseaux Wi-Fi (appui long)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawWifiList(float px,float pw,float ph,const IslandContent& c)
{
    float fade=std::clamp((CuEase(c.animT)-.20f)/.80f,0.f,1.f);

    float pad=14.f;
    float hY=14.f;

    SetB0({1,1,1,.07f*fade});
    m_rt->FillEllipse({{px+pad+10,hY+11},12.f,12.f},m_b0);
    if(m_fI14) Txt(L"\uE72B",m_fI14,{px+pad,hY+3,px+pad+24,hY+21},K::WHITE,fade*.85f,DWRITE_TEXT_ALIGNMENT_CENTER);

    Txt(L"Wi-Fi",m_fT,{px+pad+28,hY+2,px+pw-pad-40,hY+20},K::WHITE,fade);

    D2D1_COLOR_F wifiCol=c.wifiEnabled?K::BLU:K::GR;
    if(m_fI14) Txt(L"\uE701",m_fI14,{px+pw-pad-22,hY+2,px+pw-pad,hY+20},wifiCol,fade*.9f,DWRITE_TEXT_ALIGNMENT_CENTER);

    float sepY=hY+26.f;
    SetB1({1,1,1,.07f*fade});
    m_rt->DrawLine({px+pad,sepY},{px+pw-pad,sepY},m_b1,.5f);

    float itemH=48.f, gap=5.f;
    float iy=sepY+8.f;

    if(c.wifiNetworks.empty()){
        Txt(L"Recherche de r\u00E9seaux\u2026",m_fS,{px+pad,iy+8,px+pw-pad,iy+26},
            K::GR,fade*.55f,DWRITE_TEXT_ALIGNMENT_CENTER);
    } else {
        for(int i=0;i<(int)c.wifiNetworks.size()&&iy+itemH<ph-8;++i){
            const auto& n=c.wifiNetworks[i];
            bool isCon=n.connected;

            D2D1_COLOR_F bg=isCon?D2D1::ColorF(0.f,0.30f,0.65f,0.25f*fade)
                                  :D2D1::ColorF(1.f,1.f,1.f,0.06f*fade);
            SetB0(bg,1.f);
            m_rt->FillRoundedRectangle({{px+pad,iy,px+pw-pad,iy+itemH},11.f,11.f},m_b0);
            if(isCon){
                SetB1({0.2f,0.6f,1.f,.22f*fade},1.f);
                m_rt->DrawRoundedRectangle({{px+pad,iy,px+pw-pad,iy+itemH},11.f,11.f},m_b1,.75f);
            }

            float icX=px+pad+20.f, icY=iy+itemH*.5f;
            SetB0(isCon?K::BLU:D2D1::ColorF(0.25f,0.25f,0.30f,1.f), isCon?fade*.9f:fade*.5f);
            m_rt->FillEllipse({{icX,icY},16.f,16.f},m_b0);
            if(m_fI14) Txt(L"\uE701",m_fI14,{icX-9,icY-9,icX+9,icY+9},
                K::WHITE,fade*.9f,DWRITE_TEXT_ALIGNMENT_CENTER);

            float tx2=px+pad+44.f;
            Txt(n.ssid,m_fT,{tx2,iy+7,px+pw-pad-38,iy+23},K::WHITE,fade*.95f);
            Txt(isCon?L"CONNECT\u00C9":L"DISPONIBLE",m_fX,{tx2,iy+25,px+pw-pad-38,iy+38},
                isCon?K::BLU:K::GR,fade*(isCon?.75f:.50f));

            DrawSignalBars(px+pw-pad-16.f, icY, n.signal, fade);

            iy+=itemH+gap;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawSignalBars — 4 barres de signal Wi-Fi style iOS
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawSignalBars(float cx,float cy,int quality,float alpha)
{
    const int BARS=4;
    float bw=3.f, gap=2.f;
    float totalW=BARS*bw+(BARS-1)*gap;
    float x=cx-totalW*.5f;
    for(int i=0;i<BARS;++i){
        float bh=4.f+i*4.f;
        float by=cy+8.f-bh;
        bool lit=(quality >= (i+1)*25);
        SetB0(lit?K::WHITE:K::GR, alpha*(lit?.70f:.25f));
        m_rt->FillRoundedRectangle({{x,by,x+bw,cy+8.f},1.2f,1.2f},m_b0);
        x+=bw+gap;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawTogglePill — pill toggle (Wi-Fi actif = bleu + glass)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawTogglePill(float x,float y,float w,float h,
                               const wchar_t* icon,const wchar_t* label,
                               const wchar_t* sub,bool active,float alpha)
{
    D2D1_COLOR_F bg = active
        ? D2D1::ColorF(0.08f,0.18f,0.35f,1.f)
        : D2D1::ColorF(0.13f,0.13f,0.15f,1.f);
    SetB0(bg,alpha*.96f);
    m_rt->FillRoundedRectangle({{x,y,x+w,y+h},h*.45f,h*.45f},m_b0);

    D2D1_COLOR_F borderCol = active
        ? D2D1::ColorF(0.2f,0.6f,1.f,.30f)
        : D2D1::ColorF(1.f,1.f,1.f,.07f);
    SetB1(borderCol,alpha);
    m_rt->DrawRoundedRectangle({{x,y,x+w,y+h},h*.45f,h*.45f},m_b1,0.6f);

    float iconX=x+13.f, cy2=y+h*.5f;
    if(icon&&m_fI14)
        Txt(icon,m_fI14,{iconX-8,cy2-9,iconX+10,cy2+9},
            active?K::BLU:K::GR, alpha*.88f,DWRITE_TEXT_ALIGNMENT_CENTER);

    float tx3=(icon?iconX+14:x+9);
    if(label&&label[0])
        Txt(label,m_fT,{tx3,y+5,x+w-6,y+h*.5f+3},K::WHITE,alpha*.92f);
    if(sub&&sub[0])
        Txt(sub,m_fX,{tx3,y+h*.5f+1,x+w-6,y+h-5},
            active?K::BLU:K::GR,alpha*.58f);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawSlider — slider volume ou luminosité (temps réel + thumb large)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawSlider(float x,float y,float w,float val,const wchar_t* icon,float alpha)
{
    float th=8.f, ty2=y+7.f;

    SetB0({1,1,1,0.10f*alpha});
    m_rt->FillRoundedRectangle({{x,ty2,x+w,ty2+th},th*.5f,th*.5f},m_b0);

    float fw=w*std::clamp(val,0.f,1.f);
    if(fw>th){
        SetB0({1,1,1,0.80f*alpha});
        m_rt->FillRoundedRectangle({{x,ty2,x+fw,ty2+th},th*.5f,th*.5f},m_b0);
    }

    float thumbX=x+fw;
    SetB0(K::WHITE,alpha*.96f);
    m_rt->FillEllipse({{thumbX,ty2+th*.5f},9.5f,9.5f},m_b0);
    SetB0({0,0,0,0.16f*alpha});
    m_rt->DrawEllipse({{thumbX,ty2+th*.5f},9.5f,9.5f},m_b0,1.2f);

    if(icon&&m_fI14)
        Txt(icon,m_fI14,{x+w+7,ty2-4,x+w+26,ty2+th+5},K::GR,alpha*.72f,DWRITE_TEXT_ALIGNMENT_CENTER);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawArc — arc circulaire (HUD)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawArc(float cx,float cy,float r,float val,
                        D2D1_COLOR_F tk,D2D1_COLOR_F fl,float sw)
{
    const int S=64;
    float ST=-(float)M_PI*0.5f, SW=(float)M_PI*2.0f;
    for(int i=0;i<S;++i){
        float a1=ST+SW*(float)i/S, a2=ST+SW*(float)(i+1)/S;
        if(tk.a > 0.01f){
            SetB0(tk); m_rt->DrawLine({cx+cosf(a1)*r,cy+sinf(a1)*r},{cx+cosf(a2)*r,cy+sinf(a2)*r},m_b0,sw);
        }
    }
    int fs=(int)(S*std::clamp(val,0.f,1.f));
    for(int i=0;i<fs;++i){
        float a1=ST+SW*(float)i/S, a2=ST+SW*(float)(i+1)/S;
        SetB0(fl); m_rt->DrawLine({cx+cosf(a1)*r,cy+sinf(a1)*r},{cx+cosf(a2)*r,cy+sinf(a2)*r},m_b0,sw*1.1f);
    }
}

void Renderer::DrawBar(float x,float y,float w,float h,float val,D2D1_COLOR_F col){
    SetB0({1,1,1,.09f});
    m_rt->FillRoundedRectangle({{x,y,x+w,y+h},h*.5f,h*.5f},m_b0);
    float fw=(std::max)(h,w*std::clamp(val,0.f,1.f));
    SetB0(col); m_rt->FillRoundedRectangle({{x,y,x+fw,y+h},h*.5f,h*.5f},m_b0);
}

void Renderer::DrawStat(float x,float y,float w,const wchar_t* lbl,
                         float val,D2D1_COLOR_F col,const wchar_t* unit){
    Txt(lbl,m_fX,{x,y,x+28,y+14},K::GR,.58f);
    if(val>0.f) DrawBar(x+32,y+4,w-68,5,val,col);
    wchar_t b[32]={};
    if(val>0.f) swprintf_s(b,L"%.0f%s",val*100.f,unit);
    else        wcsncpy_s(b,unit,31);
    Txt(b,m_fX,{x+w-30,y,x+w,y+14},K::GR2,.78f,DWRITE_TEXT_ALIGNMENT_TRAILING);
}

void Renderer::DrawBat(float x,float y,float pct,bool ch){
    float BW=20,BH=10;
    // Couleur plate (Flat design) : Vert si chargé, Rouge si faible, Blanc sinon
    D2D1_COLOR_F white = {0.9f, 0.9f, 0.9f, 1.0f};
    D2D1_COLOR_F col = pct < 20 ? K::RED : (ch ? K::PUR : white);
    
    // Corps de la batterie (fond semi-transparent)
    D2D1_COLOR_F bgCol = {1,1,1, 0.15f};
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + BW, y + BH), 2.f, 2.f), m_b0);
    
    // Remplissage dynamique (Flat)
    float fw = std::clamp(pct / 100.f, 0.f, 1.f) * (BW - 2.f);
    if(fw > 0) {
        SetB0(col, 0.9f);
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + 1.f, y + 1.f, x + 1.f + fw, y + BH - 1.f), 1.5f, 1.5f), m_b0);
    }
    
    // Petit embout de la batterie
    D2D1_COLOR_F tipCol = {1,1,1, 0.4f};
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + BW + 1.f, y + BH * 0.3f, x + BW + 3.f, y + BH * 0.7f), 1.f, 1.f), m_b0);

    if(ch){
        D2D1_COLOR_F boltCol = {1.f, 1.f, 0.2f, 0.9f};
        SetB0(boltCol);
        float mx=x+BW*.5f,ty=y+1.5f,by=y+BH-1.5f,my=y+BH*.5f;
        m_rt->DrawLine(D2D1::Point2F(mx+2.f,ty), D2D1::Point2F(mx-1.f,my), m_b0, 1.2f);
        m_rt->DrawLine(D2D1::Point2F(mx-1.f,my), D2D1::Point2F(mx+1.5f,my), m_b0, 1.2f);
        m_rt->DrawLine(D2D1::Point2F(mx+1.5f,my), D2D1::Point2F(mx-1.5f,by), m_b0, 1.2f);
    }
}

void Renderer::Txt(const std::wstring& t,IDWriteTextFormat* f,
                    D2D1_RECT_F rc,D2D1_COLOR_F col,float op,
                    DWRITE_TEXT_ALIGNMENT al)
{
    if(!f||!m_b0||t.empty()) return;
    if(rc.right<=rc.left+1||rc.bottom<=rc.top+1) return;
    if(f->GetTextAlignment()!=al) f->SetTextAlignment(al);
    SetB0(col,op);
    m_rt->DrawText(t.c_str(),(UINT32)t.size(),f,rc,m_b0,D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
