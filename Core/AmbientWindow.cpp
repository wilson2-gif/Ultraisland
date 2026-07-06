#include "AmbientWindow.h"
#include "../Graphics/Renderer.h"     // IslandContent
#include <d2d1effects.h>
#include <initguid.h>
#include <windowsx.h>
#include <cmath>
#include <algorithm>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "dxguid.lib")

namespace AK {
    const D2D1_COLOR_F WHITE = {0.96f, 0.96f, 0.97f, 1.f};
    const D2D1_COLOR_F DIM   = {0.75f, 0.75f, 0.80f, 1.f};
    const D2D1_COLOR_F MUTED = {0.55f, 0.55f, 0.60f, 1.f};
}

// Ease-out cubique 0..1 (transitions douces)
static float Ease01(float t) {
    t = std::clamp(t, 0.f, 1.f);
    float u = 1.f - t;
    return 1.f - u * u * u;
}

AmbientWindow::~AmbientWindow()
{
    m_onClosed = nullptr;   // pas de callback pendant la destruction de l'app
    DestroyNow();
}

void AmbientWindow::Configure(const IslandContent* content,
                              std::function<const std::vector<uint8_t>&()> thumbGetter,
                              MediaAction onMedia,
                              MediaSeekAction onSeek,
                              std::function<void()> onClosed,
                              VolumeGet volGet, VolumeSet volSet)
{
    m_content     = content;
    m_thumbGetter = std::move(thumbGetter);
    m_onMedia     = std::move(onMedia);
    m_onSeek      = std::move(onSeek);
    m_onClosed    = std::move(onClosed);
    m_volGet      = std::move(volGet);
    m_volSet      = std::move(volSet);
}

float AmbientWindow::VolumeFromX(int x) const
{
    // La zone est reconstruite au paint (action=5). On calcule ici juste le %.
    for (const auto& z : m_zones)
        if (z.action == 5) {
            float t = ((float)x - z.rc.left) / (z.rc.right - z.rc.left);
            return std::clamp(t, 0.f, 1.f);
        }
    return m_volume;
}

void AmbientWindow::Open(HINSTANCE hi)
{
    if (m_hwnd) { SetForegroundWindow(m_hwnd); return; }

    static bool s_reg = false;
    if (!s_reg) {
        WNDCLASSEX wc = {};
        wc.cbSize = sizeof(wc); wc.lpfnWndProc = WndProc; wc.hInstance = hi;
        wc.lpszClassName = L"UltraAmbient";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassEx(&wc);
        s_reg = true;
    }
    int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN);
    // WS_EX_TOOLWINDOW : pas de bouton dans la barre des tâches (fenêtre utilitaire).
    m_hwnd = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"UltraAmbient", L"Mode ambiant",
        WS_POPUP | WS_VISIBLE, 0, 0, w, h, nullptr, nullptr, hi, this);
    if (!m_hwnd) return;
    m_openTick = GetTickCount();
    m_closing  = false;
    if (m_volGet) m_volume = std::clamp(m_volGet(), 0.f, 1.f);
    SetForegroundWindow(m_hwnd);
    SetTimer(m_hwnd, 1, 33, nullptr);   // ~30 fps : fondus + progression fluides
}

// Close() = démarre le FONDU de fermeture ; la destruction réelle a lieu
// 220 ms plus tard (WM_TIMER → DestroyNow).
void AmbientWindow::Close()
{
    if (!m_hwnd || m_closing) return;
    m_closing   = true;
    m_closeTick = GetTickCount();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void AmbientWindow::DestroyNow()
{
    if (!m_hwnd) return;
    HWND h = m_hwnd;
    m_hwnd = nullptr;          // évite la réentrance (WM_DESTROY → onClosed)
    m_closing = false;
    KillTimer(h, 1);
    DestroyWindow(h);
    DropDevRes();
    if (m_onClosed) m_onClosed();
}

void AmbientWindow::OnTrackChanged(const std::wstring& title,
                                   const std::wstring& artist, float durationSec)
{
    std::wstring key = title + L"\x1F" + artist;
    if (key == m_lastTrackKey) return;
    m_lastTrackKey = key;
    m_trackMorphTick = GetTickCount();     // déclenche la transition morph (crossfade)
    m_lyrIdxShown = -1; m_lyrScanHint = 0; // repart proprement pour la nouvelle piste
    m_playBaseTick = 0;                    // resync horloge de lecture au nouveau titre
    unsigned gen = ++m_lyrGen;
    {
        std::lock_guard<std::mutex> lk(m_lyrMtx);
        // Cache prefetch : si le prochain titre a déjà été préchargé → INSTANTANÉ,
        // aucun « gap » de chargement réseau.
        if (key == m_prefetchKey && !m_prefetchLyrics.empty()) {
            m_lyrics = std::move(m_prefetchLyrics);
            m_prefetchKey.clear();
            if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        m_lyrics.clear();
    }
    LyricsClient::FetchAsync(artist, title, (int)(durationSec + .5f),
        [this, gen](std::vector<LyricLine> lines) {
            std::lock_guard<std::mutex> lk(m_lyrMtx);
            if (gen != m_lyrGen) return;       // fetch périmé (piste changée)
            m_lyrics = std::move(lines);
            if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);  // thread-safe
        });
}

// Précharge le prochain titre (appelé ~10 s avant la fin du courant par
// WindowManager). Le résultat est gardé en cache et consommé par OnTrackChanged.
void AmbientWindow::PrefetchLyrics(const std::wstring& title, const std::wstring& artist,
                                   float durationSec)
{
    std::wstring key = title + L"\x1F" + artist;
    if (key == m_lastTrackKey || key == m_prefetchKey) return;   // courant ou déjà en cache
    m_prefetchKey = key;
    LyricsClient::FetchAsync(artist, title, (int)(durationSec + .5f),
        [this, key](std::vector<LyricLine> lines) {
            std::lock_guard<std::mutex> lk(m_lyrMtx);
            if (key != m_prefetchKey) return;   // un autre prefetch a pris la place
            m_prefetchLyrics = std::move(lines);
        });
}

// ─────────────────────────────────────────────────────────────────────────────
bool AmbientWindow::EnsureDevRes()
{
    if (m_rt) return true;
    if (!m_f  && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_f)))
        return false;
    if (!m_dw && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory), (IUnknown**)&m_dw)))
        return false;
    if (!m_wic) CoCreateInstance(CLSID_WICImagingFactory, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_wic));

    auto MF = [&](const wchar_t* face, float sz, DWRITE_FONT_WEIGHT w,
                  IDWriteTextFormat** o){
        m_dw->CreateTextFormat(face, nullptr, w, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, sz, L"", o);
        if (*o) {
            (*o)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            (*o)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    };
    MF(L"Segoe UI Variable Display", 96.f, DWRITE_FONT_WEIGHT_LIGHT,     &m_fClock);
    MF(L"Segoe UI Variable Display", 19.f, DWRITE_FONT_WEIGHT_NORMAL,    &m_fDate);
    MF(L"Segoe UI Variable Display", 21.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &m_fTitle);
    MF(L"Segoe UI Variable Text",    15.f, DWRITE_FONT_WEIGHT_NORMAL,    &m_fSub);
    MF(L"Segoe UI Variable Small",   12.f, DWRITE_FONT_WEIGHT_NORMAL,    &m_fSmall);
    MF(L"Segoe MDL2 Assets",         22.f, DWRITE_FONT_WEIGHT_NORMAL,    &m_fIcon);
    MF(L"Segoe UI Variable Display", 34.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &m_fLyr);
    MF(L"Segoe UI Variable Display", 22.f, DWRITE_FONT_WEIGHT_NORMAL,    &m_fLyrDim);

    RECT rc; GetClientRect(m_hwnd, &rc);
    D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties();
    D2D1_HWND_RENDER_TARGET_PROPERTIES hw = D2D1::HwndRenderTargetProperties(
        m_hwnd, D2D1::SizeU(rc.right, rc.bottom));
    if (FAILED(m_f->CreateHwndRenderTarget(rtp, hw, &m_rt))) return false;
    m_rt->QueryInterface(__uuidof(ID2D1DeviceContext), (void**)&m_dc);  // flou
    // DPI Per-Monitor V2 : texte NET à 125/150 %
    {
        typedef UINT (WINAPI *GetDpiFn)(HWND);
        UINT dpi = 96;
        if(HMODULE u32 = GetModuleHandleW(L"user32.dll")){
            if(auto fn = (GetDpiFn)GetProcAddress(u32, "GetDpiForWindow"))
                dpi = fn(m_hwnd);
        }
        m_rt->SetDpi((float)dpi, (float)dpi);
        m_dpi = dpi / 96.f;   // clics physiques → logiques
    }
    m_rt->CreateSolidColorBrush(AK::WHITE, &m_b);
    DecodeArt();
    return m_b != nullptr;
}

void AmbientWindow::DropDevRes()
{
    auto SR = [](auto& p){ if (p) { p->Release(); p = nullptr; } };
    SR(m_art); SR(m_b); SR(m_dc); SR(m_rt);
    SR(m_fClock); SR(m_fDate); SR(m_fTitle); SR(m_fSub); SR(m_fSmall);
    SR(m_fIcon); SR(m_fLyr); SR(m_fLyrDim);
    SR(m_wic); SR(m_dw); SR(m_f);
    m_artBytes = 0;
}

// Décode la pochette (bytes bruts → bitmap de CE render target)
void AmbientWindow::DecodeArt()
{
    if (!m_rt || !m_wic || !m_thumbGetter) return;
    const std::vector<uint8_t>& data = m_thumbGetter();
    if (data.empty()) { if (m_art) { m_art->Release(); m_art = nullptr; } m_artBytes = 0; return; }
    if (data.size() == m_artBytes && m_art) return;   // inchangé
    if (m_art) { m_art->Release(); m_art = nullptr; }
    m_artBytes = data.size();

    IWICStream* stream = nullptr;
    if (SUCCEEDED(m_wic->CreateStream(&stream))) {
        if (SUCCEEDED(stream->InitializeFromMemory(
                const_cast<uint8_t*>(data.data()), (DWORD)data.size()))) {
            IWICBitmapDecoder* dec = nullptr;
            if (SUCCEEDED(m_wic->CreateDecoderFromStream(
                    stream, nullptr, WICDecodeMetadataCacheOnLoad, &dec))) {
                IWICBitmapFrameDecode* frame = nullptr;
                if (SUCCEEDED(dec->GetFrame(0, &frame))) {
                    IWICFormatConverter* conv = nullptr;
                    if (SUCCEEDED(m_wic->CreateFormatConverter(&conv))) {
                        if (SUCCEEDED(conv->Initialize(frame,
                                GUID_WICPixelFormat32bppPBGRA,
                                WICBitmapDitherTypeNone, nullptr, 0.f,
                                WICBitmapPaletteTypeMedianCut)))
                            m_rt->CreateBitmapFromWicBitmap(conv, nullptr, &m_art);
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

void AmbientWindow::Txt(const std::wstring& t, IDWriteTextFormat* f,
                         D2D1_RECT_F rc, D2D1_COLOR_F c, DWRITE_TEXT_ALIGNMENT a)
{
    if (!f || t.empty()) return;
    if (f->GetTextAlignment() != a) f->SetTextAlignment(a);
    m_b->SetColor(c);
    m_rt->DrawText(t.c_str(), (UINT32)t.size(), f, rc, m_b,
                   D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

// ─────────────────────────────────────────────────────────────────────────────
//  PlaybackSec — horloge de lecture LISSE (suivi doux façon PLL). Avance avec le
//  TEMPS RÉEL quand ça joue, et corrige EN DOUCEUR vers la position rapportée
//  (12 %/frame) : pas de saut ni de va-et-vient sur les petits recalages SMTC, et
//  pas de retard car elle converge en ~0,4 s. Snap seulement sur un vrai seek
//  (>1,5 s). À appeler UNE SEULE FOIS par frame (elle fait avancer l'horloge).
// ─────────────────────────────────────────────────────────────────────────────
float AmbientWindow::PlaybackSec(const IslandContent& c)
{
    DWORD now = GetTickCount();
    if (m_playBaseTick == 0) { m_playBaseSec = c.musicCurrentSec; m_playBaseTick = now; return m_playBaseSec; }
    float dt = (now - m_playBaseTick) * 0.001f;
    m_playBaseTick = now;
    if (c.isMusicPlaying) m_playBaseSec += dt;              // avance temps réel
    float err = c.musicCurrentSec - m_playBaseSec;
    if (std::abs(err) > 1.5f) m_playBaseSec = c.musicCurrentSec;   // seek → snap
    else                      m_playBaseSec += err * 0.12f;        // correction douce (PLL)
    if (m_playBaseSec < 0.f) m_playBaseSec = 0.f;
    return m_playBaseSec;
}

// ─────────────────────────────────────────────────────────────────────────────
void AmbientWindow::OnPaint()
{
    if (!EnsureDevRes() || !m_content) return;
    DecodeArt();   // rafraîchit si la pochette a changé
    m_zones.clear();
    const IslandContent& c = *m_content;
    float playSec = PlaybackSec(c);   // horloge lisse (lyrics + barre de progression)

    m_rt->BeginDraw();
    D2D1_SIZE_F sz = m_rt->GetSize();

    // ── Fond : pochette floutée plein écran (moins flou pour le style Visily) ─
    m_rt->Clear(D2D1::ColorF(0.03f, 0.03f, 0.05f, 1.f));
    if (m_art && m_dc) {
        D2D1_SIZE_F as = m_art->GetSize();
        // PIÈGE DPI : m_dc->DrawImage (sortie d'effet) compose en PIXELS DEVICE et
        // n'est PAS ré-agrandi par le SetDpi du RT — contrairement à Clear/FillRect/
        // DrawText (vectoriels, DPI-scalés). Si on dimensionne le flou sur sz=GetSize()
        // (LOGIQUE 1536x864), il ne couvre que 1536x864 px physiques → bande noire de
        // (physique-logique)=384 px à droite (et 216 en bas) à 125%. On cible donc la
        // taille PHYSIQUE szPx = sz*m_dpi pour que DrawImage remplisse tout l'écran.
        D2D1_SIZE_F szPx = { sz.width * m_dpi, sz.height * m_dpi };
        float scale = std::max(szPx.width / as.width, szPx.height / as.height) * 1.15f;
        ID2D1Effect* blur = nullptr;
        if (SUCCEEDED(m_dc->CreateEffect(CLSID_D2D1GaussianBlur, &blur)) && blur) {
            blur->SetInput(0, m_art);
            blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 14.f);
            blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE,
                           D2D1_BORDER_MODE_HARD);
            D2D1_MATRIX_3X2_F xf =
                D2D1::Matrix3x2F::Scale(scale, scale) *
                D2D1::Matrix3x2F::Translation(
                    (szPx.width  - as.width  * scale) * .5f,
                    (szPx.height - as.height * scale) * .5f);
            m_dc->SetTransform(xf);
            m_dc->DrawImage(blur);
            m_dc->SetTransform(D2D1::Matrix3x2F::Identity());
            blur->Release();
        }
        // Assombrissement léger (pochette reconnaissable)
        m_b->SetColor({0.f, 0.f, 0.f, 0.35f});
        m_rt->FillRectangle({0, 0, sz.width, sz.height}, m_b);
    }

    // ── GRANDE POCHETTE LATÉRALE (gauche) — réintroduite à la demande ─────────
    //   Layout split : pochette nette à GAUCHE (grande), lyrics à DROITE, carte
    //   contrôle centrée en bas. La pochette est dessinée par-DESSUS le fond flouté.
    // Y a-t-il des lyrics ? (verrou bref) — SANS lyrics : pochette + carte CENTRÉES.
    bool hasLyrics; { std::lock_guard<std::mutex> lk(m_lyrMtx); hasLyrics = !m_lyrics.empty(); }
    float MX = sz.width * 0.04f, MY = sz.height * 0.035f;   // marges reduites
    const float CARD_H = 118.f, BOT_M = 22.f, COL_GAP = 44.f, V_GAP = 14.f;  // carte -20%
    float artSide = std::min(sz.width * 0.46f,
                             sz.height - MY - V_GAP - CARD_H - BOT_M);
    // Décalage ANIMÉ (doux) entre pochette centrée (sans lyrics) et à gauche (avec) :
    // m_layoutT ease vers la cible → glissement fluide au lieu d'un saut.
    float targetT = hasLyrics ? 1.f : 0.f;
    m_layoutT += (targetT - m_layoutT) * 0.12f;
    if (std::abs(targetT - m_layoutT) < 0.002f) m_layoutT = targetT;
    float artLc = (sz.width - artSide) * 0.5f;          // centré
    float artL  = artLc + (MX - artLc) * m_layoutT;     // interpolé vers la gauche
    float artT = MY, artR = artL + artSide, artB = artT + artSide;
    float cardL = artL, cardR = artR, cardT = artB + V_GAP, cardB = cardT + CARD_H;
    float lyrL = artR + COL_GAP, lyrR = sz.width - MX, lyrT = artT, lyrB = artB;
    if (m_art) {
        // Zone verticale disponible au-dessus de la carte contrôle (CH=200, marge 80).
        // (geometrie pochette calculee au niveau de OnPaint ci-dessus)
        // (Ombre portée retirée : le rect noir 0.45 débordant de 10 px SOUS la pochette
        //  créait un halo sombre asymétrique en bas. Le fond flouté suffit.)

        // Pochette NETTE en coin arrondi via bitmap-brush (cover-fill centré)
        D2D1_SIZE_F as2 = m_art->GetSize();
        ID2D1BitmapBrush* bb = nullptr;
        if (SUCCEEDED(m_rt->CreateBitmapBrush(m_art, &bb)) && bb) {
            bb->SetExtendModeX(D2D1_EXTEND_MODE_CLAMP);
            bb->SetExtendModeY(D2D1_EXTEND_MODE_CLAMP);
            bb->SetInterpolationMode(D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            float sc = artSide / std::min(as2.width, as2.height);   // COVER
            float ox = (artSide - as2.width  * sc) * .5f;
            float oy = (artSide - as2.height * sc) * .5f;
            bb->SetTransform(D2D1::Matrix3x2F::Scale(sc, sc) *
                             D2D1::Matrix3x2F::Translation(artL + ox, artT + oy));
            m_rt->FillRoundedRectangle({{artL, artT, artR, artB}, 24.f, 24.f}, bb);
            bb->Release();
        }
        // Liseret subtil
        m_b->SetColor({1.f, 1.f, 1.f, 0.14f});
        m_rt->DrawRoundedRectangle({{artL, artT, artR, artB}, 24.f, 24.f}, m_b, 1.f);
    }

    // ── LAYOUT SPLIT : grande pochette à GAUCHE (dessinée ci-dessus) + lyrics
    //    dans la colonne DROITE + carte contrôle acrylique centrée en bas.
    //    Fond = pochette floutée plein écran. Lyrics façon Spotify : défilement
    //    vertical continu, cross-fade de taille, Ease-Out + anticipation 300 ms.
    // ── Lyrics façon Spotify — colonne droite, Ease-Out + anticipation ─────
    // Techniques appliquées (conseils Gemini) :
    //   1) Translation Y globale : les 5 lignes glissent VERS LE HAUT ensemble
    //      quand la ligne active change (défilement continu, pas de « saut »).
    //   2) Fade in/out : ligne active à 100 %, inactives 30..50 %.
    //   3) Scale 1.0 → 1.10 sur la ligne active (effet de focus).
    //   4) Ease-Out cubique déjà via Ease01().
    //   5) Anticipation 300 ms : la transition commence AVANT le timestamp exact
    //      → la ligne est en place au moment où le chanteur prononce le mot.
    if (hasLyrics) {   // sans lyrics : bloc SKIPPÉ → pochette + carte centrées, rien d'autre
        std::lock_guard<std::mutex> lk(m_lyrMtx);
        // Pas de nappe : les lyrics flottent directement sur le fond flouté.
        float lx  = lyrL;                 // colonne droite
        float lxr = lyrR;
        float ly  = (lyrT + lyrB) * 0.5f; // point d'ancrage = centre de la colonne

        if (m_lyrics.empty()) {
            Txt(L"♪  Paroles indisponibles", m_fLyrDim,
                {lx, ly - 20.f, lxr, ly + 20.f},
                {0.85f, 0.85f, 0.88f, 0.40f}, DWRITE_TEXT_ALIGNMENT_CENTER);
        } else {
            // Index MONOTONE depuis l'horloge lisse `playSec` (calculée 1×/frame) :
            // avance d'un cran quand le temps dépasse la ligne suivante ; ne recule
            // QUE sur un vrai seek arrière. Anticipation pour compenser la latence
            // SMTC (les paroles étaient « en retard »).
            const float ANTICIPATE = 0.45f;
            float cur = playSec + ANTICIPATE;
            int idx = m_lyrIdxShown;
            while (idx + 1 < (int)m_lyrics.size() && m_lyrics[idx + 1].t <= cur) ++idx;
            while (idx >= 0 && m_lyrics[idx].t > cur) --idx;   // seek arrière uniquement
            if (idx != m_lyrIdxShown) { m_lyrIdxShown = idx; m_lyrTick = GetTickCount(); }

            // Ease-Out cubique — durée ADAPTATIVE bornée à l'écart réel vers la
            // ligne suivante : une durée fixe (700 ms) > écart entre 2 lignes
            // rapprochées faisait redémarrer l'anim à mi-course (m_lyrTick réinit
            // avant lt=1) → défilement saccadé sur les refrains. On borne à
            // [180, 520] ms pour que la ligne se stabilise avant la suivante.
            float dt  = (idx >= 0 && idx + 1 < (int)m_lyrics.size())
                        ? (m_lyrics[idx + 1].t - m_lyrics[idx].t) : 3.0f;
            float dur = std::clamp(dt * 1000.f * 0.5f, 180.f, 520.f);
            float lt  = Ease01((GetTickCount() - m_lyrTick) / dur);

            // Espacement adaptatif (rythme réel de la chanson)
            float avgGap = 3.0f;
            if ((int)m_lyrics.size() >= 4)
                avgGap = (m_lyrics.back().t - m_lyrics.front().t) / (m_lyrics.size()-1);
            float lineH = std::clamp(56.f * (avgGap / 3.f), 48.f, 72.f);

            // Défilement VERTICAL GLOBAL du conteneur : quand la ligne change,
            // tout le bloc glisse vers le haut de `lineH` (Ease-Out).
            float scrollY = (1.f - lt) * lineH;

            // CROSS-FADE DE TAILLE (plus de saut 34↔22 px). Auparavant la ligne
            // active utilisait m_fLyr (34) et les voisines m_fLyrDim (22) : au
            // changement de ligne, la MÊME ligne basculait instantanément de fonte
            // → « pop » visible. Ici UN SEUL format (m_fLyr, 34 px) pour toutes les
            // lignes, et on interpole le SCALE autour du centre : voisines à k≈0.66
            // (≈22 px), active à 1.0 (34 px). La ligne entrante (0) grandit de k→1
            // et la sortante (-1) rétrécit de 1→k, en continuité EXACTE avec l'état
            // précédent (à lt=0, la sortante est encore à 1.0, l'entrante encore à k).
            const float k = 0.66f;   // échelle des lignes voisines (34*0.66 ≈ 22)

            auto drawLine = [&](int off, float alpha, float sc){
                int i = idx + off;
                if (i < 0 || i >= (int)m_lyrics.size()) return;
                float yPos = ly + off * lineH + scrollY;
                D2D1_RECT_F rc = {lx, yPos - 26.f, lxr, yPos + 26.f};
                D2D1_COLOR_F col = {0.98f, 0.98f, 0.99f, alpha};
                float cxs = (rc.left + rc.right) * .5f;
                float cys = (rc.top  + rc.bottom) * .5f;
                D2D1_MATRIX_3X2_F old; m_rt->GetTransform(&old);
                m_rt->SetTransform(D2D1::Matrix3x2F::Scale(sc, sc, {cxs, cys}) * old);
                Txt(m_lyrics[i].text, m_fLyr, rc, col, DWRITE_TEXT_ALIGNMENT_CENTER);
                m_rt->SetTransform(old);
            };

            // Taille ET alpha varient continûment sur lt (aucune marche de fonte).
            drawLine(-2, 0.18f,                       k);
            drawLine(-1, 0.30f + 0.68f * (1.f - lt),  k + (1.f - k) * (1.f - lt));  // sortante : active → voisine
            drawLine( 0, 0.30f + 0.68f * lt,          k + (1.f - k) * lt);          // entrante : voisine → active
            drawLine(+1, 0.30f,                       k);
            drawLine(+2, 0.18f,                       k);

            if (idx < 0) {
                // Avant la 1re ligne : petit ♪ centré
                Txt(L"♪", m_fLyr, {lx, ly-26.f, lxr, ly+26.f},
                    {1,1,1, 0.35f}, DWRITE_TEXT_ALIGNMENT_CENTER);
            }
        }
    }

    // ── CARTE CONTRÔLE UNIQUE — acrylique flouté uniforme (maquette utilisateur)
    //    Plus de grande pochette latérale : juste une carte glass centrée en bas
    //    avec titre + artiste + barre + prev/play/next + slider volume.
    if (!c.musicTitle.empty()) {
        const float CW  = cardR - cardL;
        const float CH  = cardB - cardT;
        float cx  = (cardL + cardR) * .5f;
        float cy0 = cardT;
        D2D1_RECT_F card = {cardL, cardT, cardR, cardB};

        // Fond acrylique flouté UNIFORME (sombre translucide + reflet supérieur)
        // Acrylique TRANSPARENT (sans couleur) : laisse voir le fond flute, juste un
        // voile clair + reflet haut + fin liseret. Plus aucun fond sombre teinte.
        // Acrylique UNIFORME : un seul voile clair + fin liseret. Plus de demi-fill
        // supérieur (qui créait une couture blanche horizontale / effet « découpé »).
        m_b->SetColor({1.f, 1.f, 1.f, 0.07f});
        m_rt->FillRoundedRectangle({card, 22, 22}, m_b);
        m_b->SetColor({1.f, 1.f, 1.f, 0.16f});
        m_rt->DrawRoundedRectangle({card, 22, 22}, m_b, 1.f);

        // Titre + artiste CENTRÉS
        float pad = 24.f;
        Txt(c.musicTitle,  m_fTitle, {card.left+pad, cy0+10, card.right-pad, cy0+34},
            AK::WHITE, DWRITE_TEXT_ALIGNMENT_LEADING);
        Txt(c.musicArtist, m_fSub,   {card.left+pad, cy0+32, card.right-pad, cy0+50},
            AK::DIM, DWRITE_TEXT_ALIGNMENT_LEADING);

        // Barre progression + temps
        float bx = card.left + pad, bw = CW - pad*2.f, by = cy0 + 56.f;
        m_progressBarRect = {bx, by - 10.f, bx + bw, by + 10.f};
        m_b->SetColor({1,1,1,0.18f});
        m_rt->FillRoundedRectangle({{bx, by, bx+bw, by+3.5f}, 1.75f, 1.75f}, m_b);
        float prog = std::clamp(m_isDraggingProgress ? m_dragProgress
                     : (c.musicTotalSec > 0.f ? playSec / c.musicTotalSec : 0.f), 0.f, 1.f);
        m_b->SetColor(AK::WHITE);
        m_rt->FillRoundedRectangle({{bx, by, bx+bw*prog, by+3.5f}, 1.75f, 1.75f}, m_b);
        m_rt->FillEllipse({{bx+bw*prog, by+1.75f}, 6.f, 6.f}, m_b);

        auto FT = [](float s, wchar_t* b){ swprintf_s(b, 8, L"%d:%02d",
                                                     (int)(s/60), (int)s%60); };
        wchar_t l[8], r[8];
        FT(m_isDraggingProgress ? m_dragProgress * c.musicTotalSec
                                : playSec, l);
        FT(c.musicTotalSec, r);
        Txt(l, m_fSmall, {bx, by+10, bx+60, by+26}, AK::MUTED,
            DWRITE_TEXT_ALIGNMENT_LEADING);
        Txt(r, m_fSmall, {bx+bw-60, by+10, bx+bw, by+26}, AK::MUTED,
            DWRITE_TEXT_ALIGNMENT_TRAILING);

        // Contrôles centrés
        float ctrlY = cy0 + 82.f;
        const wchar_t* gl[3] = {L"",
                                c.isMusicPlaying ? L"" : L"",
                                L""};
        float xs[3] = {cx - 80.f, cx, cx + 80.f};
        for (int i = 0; i < 3; ++i) {
            Txt(gl[i], m_fIcon, {xs[i]-18, ctrlY-18, xs[i]+18, ctrlY+18}, AK::WHITE);
            m_zones.push_back({{xs[i]-28, ctrlY-28, xs[i]+28, ctrlY+28}, i});
        }

        // Slider VOLUME
        float vy    = cy0 + 104.f;
        float vpad  = 46.f;
        float vx1   = card.left + vpad + 22.f;
        float vx2   = card.right - vpad;
        float vprog = std::clamp(m_volume, 0.f, 1.f);
        Txt(L"", m_fIcon, {vx1-30, vy-14, vx1-2, vy+14}, AK::DIM);
        m_b->SetColor({1.f, 1.f, 1.f, 0.18f});
        m_rt->FillRoundedRectangle({{vx1, vy-2, vx2, vy+2}, 2, 2}, m_b);
        float vfx = vx1 + (vx2-vx1) * vprog;
        m_b->SetColor(AK::WHITE);
        m_rt->FillRoundedRectangle({{vx1, vy-2, vfx, vy+2}, 2, 2}, m_b);
        m_rt->FillEllipse({{vfx, vy}, 7.f, 7.f}, m_b);
        m_zones.push_back({{vx1-8, vy-14, vx2+8, vy+14}, 5});
    }


    // ── Bouton fermer (haut-droite) ────────────────────────────────────────
    {
        // Bouton fermer retire (demande utilisateur) \u2014 Echap ferme toujours l'ambiant.
        (void)sz;
    }

    // ── Fondu global : ouverture 280 ms / fermeture 220 ms + MORPH piste ────
    {
        float fadeIn  = Ease01((GetTickCount() - m_openTick) / 280.f);
        float fadeOut = m_closing
                      ? 1.f - Ease01((GetTickCount() - m_closeTick) / 220.f) : 1.f;
        // Morph au changement de piste : le contenu se refond en douceur (crossfade
        // via un voile qui s'estompe sur 480 ms) → transition « morphose » entre 2 sons.
        float morph = m_trackMorphTick ? Ease01((GetTickCount() - m_trackMorphTick) / 480.f) : 1.f;
        float visible = fadeIn * fadeOut * (0.35f + 0.65f * morph);
        if (visible < 0.999f) {
            m_b->SetColor({0.f, 0.f, 0.f, 1.f - visible});
            m_rt->FillRectangle({0, 0, sz.width, sz.height}, m_b);
        }
    }

    HRESULT hr = m_rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) DropDevRes();
}

void AmbientWindow::OnClick(int x, int y)
{
    if (m_isDraggingProgress) {
        m_isDraggingProgress = false;
        if (m_onSeek) m_onSeek(m_dragProgress);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (m_dragVolume) {
        m_dragVolume = false;
        if (m_volSet) m_volSet(m_volume);   // applique au système
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    for (int i = (int)m_zones.size() - 1; i >= 0; --i) {
        const auto& z = m_zones[i];
        if (x >= z.rc.left && x <= z.rc.right && y >= z.rc.top && y <= z.rc.bottom) {
            if (z.action == 9) { Close(); return; }
            if (z.action == 5) {   // slider volume : clic direct
                m_volume = VolumeFromX(x);
                if (m_volSet) m_volSet(m_volume);
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
            if (m_onMedia) m_onMedia(z.action);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }
}

void AmbientWindow::OnMouseDown(int x, int y)
{
    // Slider VOLUME : capte AVANT la barre de progression
    for (const auto& z : m_zones) {
        if (z.action == 5 &&
            x >= z.rc.left && x <= z.rc.right &&
            y >= z.rc.top  && y <= z.rc.bottom) {
            m_dragVolume = true;
            m_volume     = VolumeFromX(x);
            if (m_volSet) m_volSet(m_volume);   // feedback temps réel
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }
    if (x >= m_progressBarRect.left && x <= m_progressBarRect.right &&
        y >= m_progressBarRect.top && y <= m_progressBarRect.bottom) {
        m_isDraggingProgress = true;
        float bw = m_progressBarRect.right - m_progressBarRect.left;
        m_dragProgress = std::clamp((x - m_progressBarRect.left) / bw, 0.f, 1.f);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void AmbientWindow::OnMouseMove(int x, int y)
{
    if (m_dragVolume) {
        m_volume = VolumeFromX(x);
        if (m_volSet) m_volSet(m_volume);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (m_isDraggingProgress) {
        float bw = m_progressBarRect.right - m_progressBarRect.left;
        m_dragProgress = std::clamp((x - m_progressBarRect.left) / bw, 0.f, 1.f);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK AmbientWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    AmbientWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lp);
        self = reinterpret_cast<AmbientWindow*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = reinterpret_cast<AmbientWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProc(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
        self->OnPaint();
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_TIMER:
        // Fin du fondu de fermeture → destruction réelle
        if (self->m_closing &&
            GetTickCount() - self->m_closeTick >= 220) {
            self->DestroyNow();
            return 0;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    // Coords souris PHYSIQUES → LOGIQUES (÷ m_dpi) pour matcher les zones dessinées.
    case WM_LBUTTONDOWN:
        self->OnMouseDown((int)(GET_X_LPARAM(lp)/self->m_dpi), (int)(GET_Y_LPARAM(lp)/self->m_dpi));
        return 0;
    case WM_MOUSEMOVE:
        self->OnMouseMove((int)(GET_X_LPARAM(lp)/self->m_dpi), (int)(GET_Y_LPARAM(lp)/self->m_dpi));
        return 0;
    case WM_LBUTTONUP:
        self->OnClick((int)(GET_X_LPARAM(lp)/self->m_dpi), (int)(GET_Y_LPARAM(lp)/self->m_dpi));
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { self->Close(); return 0; }
        return 0;
    case WM_DESTROY:
        // Close() gère la séquence normale ; si détruit autrement (fin app),
        // ne pas rappeler onClosed.
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}
