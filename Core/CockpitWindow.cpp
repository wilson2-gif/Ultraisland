#include "CockpitWindow.h"
#include "AppConfig.h"
#include "Sounds.h"
#include "IslandDim.h"
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")

// Messages île (mêmes valeurs que WindowManager.h — découplé via FindWindow)
static constexpr UINT MSG_RELOADCFG      = WM_USER + 101;
static constexpr UINT MSG_SPOTIFY        = WM_USER + 102;
static constexpr UINT MSG_SPOTIFY_LOGOUT = WM_USER + 103;

// Le refresh token Spotify (DPAPI) existe-t-il ? → état « Connecté »
static bool SpotifyTokenExists()
{
    wchar_t p[MAX_PATH];
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", p, MAX_PATH)) return false;
    std::wstring f = std::wstring(p) + L"\\Ultraisland\\spotify.dat";
    return GetFileAttributesW(f.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// ── Palette Cockpit (dark, cohérente avec l'île) ─────────────────────────────
namespace CK {
    const D2D1_COLOR_F BG      = {0.047f, 0.047f, 0.063f, 1.f};  // #0C0C10
    const D2D1_COLOR_F SIDEBAR = {0.070f, 0.070f, 0.090f, 1.f};
    const D2D1_COLOR_F CARD    = {0.086f, 0.086f, 0.110f, 1.f};
    const D2D1_COLOR_F CARD_BD = {0.150f, 0.150f, 0.180f, 1.f};
    const D2D1_COLOR_F WHITE   = {0.941f, 0.941f, 0.960f, 1.f};
    const D2D1_COLOR_F SUBTLE  = {0.627f, 0.627f, 0.680f, 1.f};
    const D2D1_COLOR_F MUTED   = {0.430f, 0.430f, 0.480f, 1.f};
    const D2D1_COLOR_F ACCENT  = {0.235f, 0.600f, 1.000f, 1.f};  // bleu iOS
    const D2D1_COLOR_F TRACK   = {0.200f, 0.200f, 0.240f, 1.f};
    const D2D1_COLOR_F GREEN   = {0.196f, 0.845f, 0.400f, 1.f};
}

static const int WIN_W = 1000;
static const int WIN_H = 720;
static const float SB_W = 224.f;   // sidebar

// Palette d'accents (maquette Apparence)
static const D2D1_COLOR_F ACCENTS[8] = {
    {0.235f,0.600f,1.000f,1.f}, {0.640f,0.400f,0.960f,1.f},
    {0.950f,0.310f,0.640f,1.f}, {0.960f,0.290f,0.340f,1.f},
    {0.980f,0.450f,0.160f,1.f}, {0.990f,0.700f,0.150f,1.f},
    {0.196f,0.845f,0.400f,1.f}, {0.150f,0.800f,0.800f,1.f},
};

// ─────────────────────────────────────────────────────────────────────────────
CockpitWindow::~CockpitWindow() { DropDevRes(); }

void CockpitWindow::Open(HINSTANCE hi, AppConfig* cfg, std::function<void()> onApply)
{
    m_cfg = cfg; m_onApply = std::move(onApply);
    if (m_hwnd) { ShowWindow(m_hwnd, SW_SHOW); SetForegroundWindow(m_hwnd); return; }

    static bool s_reg = false;
    if (!s_reg) {
        WNDCLASSEX wc = {};
        wc.cbSize = sizeof(wc); wc.lpfnWndProc = WndProc; wc.hInstance = hi;
        wc.lpszClassName = L"UltraCockpit";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        RegisterClassEx(&wc);
        s_reg = true;
    }

    int px = (GetSystemMetrics(SM_CXSCREEN) - WIN_W) / 2;
    int py = (GetSystemMetrics(SM_CYSCREEN) - WIN_H) / 2;
    m_hwnd = CreateWindowEx(WS_EX_APPWINDOW, L"UltraCockpit",
        L"UltraIsland — Cockpit",
        // WS_OVERLAPPEDWINDOW = + WS_THICKFRAME|WS_MAXIMIZEBOX → redimensionnable
        // et maximisable (avant : figé). WM_SIZE ré-applique SetDpi, WM_GETMINMAXINFO
        // plancher la taille, WM_MOUSEWHEEL fait défiler le contenu.
        WS_OVERLAPPEDWINDOW,
        px, py, WIN_W, WIN_H, nullptr, nullptr, hi, this);
    if (!m_hwnd) return;

    BOOL dark = TRUE;
    DwmSetWindowAttribute(m_hwnd, 20, &dark, sizeof(dark));   // dark title bar
    int corner = 2;
    DwmSetWindowAttribute(m_hwnd, 33, &corner, sizeof(corner)); // coins arrondis

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
}

// ─────────────────────────────────────────────────────────────────────────────
bool CockpitWindow::EnsureDevRes()
{
    if (m_rt) return true;
    if (!m_f  && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_f)))
        return false;
    if (!m_dw && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory), (IUnknown**)&m_dw)))
        return false;

    auto MF = [&](const wchar_t* face, float sz, DWRITE_FONT_WEIGHT w,
                  IDWriteTextFormat** o){
        m_dw->CreateTextFormat(face, nullptr, w, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, sz, L"", o);
        if (*o) {
            (*o)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            (*o)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    };
    if (!m_fH1)     MF(L"Segoe UI Variable Display", 26.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &m_fH1);
    if (!m_fH2)     MF(L"Segoe UI Variable Display", 16.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &m_fH2);
    if (!m_fBody)   MF(L"Segoe UI Variable Text",    14.f, DWRITE_FONT_WEIGHT_NORMAL,    &m_fBody);
    if (!m_fSmall)  MF(L"Segoe UI Variable Small",   11.5f,DWRITE_FONT_WEIGHT_NORMAL,    &m_fSmall);
    if (!m_fSection)MF(L"Segoe UI Variable Small",   11.f, DWRITE_FONT_WEIGHT_BOLD,      &m_fSection);
    if (!m_fIcon)   MF(L"Segoe MDL2 Assets",         15.f, DWRITE_FONT_WEIGHT_NORMAL,    &m_fIcon);

    RECT rc; GetClientRect(m_hwnd, &rc);
    D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties();
    D2D1_HWND_RENDER_TARGET_PROPERTIES hw = D2D1::HwndRenderTargetProperties(
        m_hwnd, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top));
    if (FAILED(m_f->CreateHwndRenderTarget(rtp, hw, &m_rt))) return false;
    // DPI Per-Monitor V2 : textes et vecteurs NETS à 125/150 % — le RT scale
    // automatiquement les coords logiques vers les pixels physiques du swapchain.
    {
        typedef UINT (WINAPI *GetDpiFn)(HWND);
        UINT dpi = 96;
        if(HMODULE u32 = GetModuleHandleW(L"user32.dll")){
            if(auto fn = (GetDpiFn)GetProcAddress(u32, "GetDpiForWindow"))
                dpi = fn(m_hwnd);
        }
        m_rt->SetDpi((float)dpi, (float)dpi);
        m_dpi = dpi / 96.f;   // pour convertir les clics physiques → logiques
    }
    m_rt->CreateSolidColorBrush(CK::WHITE, &m_b);
    return m_b != nullptr;
}

void CockpitWindow::DropDevRes()
{
    auto SR = [](auto& p){ if (p) { p->Release(); p = nullptr; } };
    SR(m_b); SR(m_rt);
    SR(m_fH1); SR(m_fH2); SR(m_fBody); SR(m_fSmall); SR(m_fSection); SR(m_fIcon);
    SR(m_dw); SR(m_f);
}

// ── Primitives ───────────────────────────────────────────────────────────────
void CockpitWindow::Fill(D2D1_RECT_F rc, D2D1_COLOR_F c, float r)
{
    if (rc.bottom > m_pageMaxY) m_pageMaxY = rc.bottom;   // mesure du contenu (scroll)
    m_b->SetColor(c);
    if (r > 0.f) m_rt->FillRoundedRectangle({rc, r, r}, m_b);
    else         m_rt->FillRectangle(rc, m_b);
}
void CockpitWindow::Frame(D2D1_RECT_F rc, D2D1_COLOR_F c, float r, float w)
{
    m_b->SetColor(c);
    if (r > 0.f) m_rt->DrawRoundedRectangle({rc, r, r}, m_b, w);
    else         m_rt->DrawRectangle(rc, m_b, w);
}
void CockpitWindow::Txt(const std::wstring& t, IDWriteTextFormat* f,
                         D2D1_RECT_F rc, D2D1_COLOR_F c, DWRITE_TEXT_ALIGNMENT a)
{
    if (!f || t.empty()) return;
    if (rc.bottom > m_pageMaxY) m_pageMaxY = rc.bottom;   // mesure du contenu (scroll)
    if (f->GetTextAlignment() != a) f->SetTextAlignment(a);
    m_b->SetColor(c);
    m_rt->DrawText(t.c_str(), (UINT32)t.size(), f, rc, m_b,
                   D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

// ── Widgets ──────────────────────────────────────────────────────────────────
void CockpitWindow::Card(float x, float y, float w, float h)
{
    Fill ({x, y, x+w, y+h}, CK::CARD,    14.f);
    Frame({x, y, x+w, y+h}, CK::CARD_BD, 14.f, 1.f);
}

void CockpitWindow::RowLabel(float x, float y, const wchar_t* title, const wchar_t* sub)
{
    Txt(title, m_fBody,  {x, y,      x+560, y+22}, CK::WHITE);
    if (sub && sub[0])
        Txt(sub, m_fSmall, {x, y+22, x+560, y+38}, CK::MUTED);
}

void CockpitWindow::Toggle(float x, float y, bool* v)
{
    const float W = 42.f, H = 24.f;
    D2D1_RECT_F rc = {x, y, x+W, y+H};
    Fill(rc, *v ? CK::ACCENT : CK::TRACK, H*.5f);
    float kx = *v ? (x + W - H*.5f - 3.f) : (x + H*.5f + 3.f);
    m_b->SetColor(CK::WHITE);
    m_rt->FillEllipse({{kx, y + H*.5f}, H*.5f - 4.f, H*.5f - 4.f}, m_b);
    m_zones.push_back({{x-6, y-6, x+W+6, y+H+6}, 0, v, 0, 0, 0, 0});
}

void CockpitWindow::SliderF(float x, float y, float w, float* v,
                             float mn, float mx, const wchar_t* fmt)
{
    const float H = 5.f;
    float t = (mx > mn) ? std::clamp((*v - mn) / (mx - mn), 0.f, 1.f) : 0.f;
    Fill({x, y, x+w, y+H}, CK::TRACK, H*.5f);
    Fill({x, y, x + w*t, y+H}, CK::ACCENT, H*.5f);
    m_b->SetColor(CK::WHITE);
    m_rt->FillEllipse({{x + w*t, y + H*.5f}, 8.f, 8.f}, m_b);
    // Valeur affichée à droite
    wchar_t buf[48]; swprintf_s(buf, fmt, *v);
    Txt(buf, m_fSmall, {x + w + 12, y - 8, x + w + 92, y + 14}, CK::SUBTLE);
    m_zones.push_back({{x-8, y-12, x+w+8, y+H+12}, 1, v, mn, mx, 0, 0});
}

void CockpitWindow::Segmented(float x, float y, float w, float h, int* v,
                               const wchar_t* const* opts, int n, int action)
{
    Fill({x, y, x+w, y+h}, CK::TRACK, h*.5f);
    float seg = w / n;
    for (int i = 0; i < n; ++i) {
        D2D1_RECT_F rc = {x + i*seg + 3, y + 3, x + (i+1)*seg - 3, y + h - 3};
        if (*v == i) Fill(rc, CK::ACCENT, (h-6)*.5f);
        Txt(opts[i], m_fSmall, rc, (*v == i) ? CK::WHITE : CK::SUBTLE,
            DWRITE_TEXT_ALIGNMENT_CENTER);
        m_zones.push_back({rc, 2, v, 0, 0, i, action});
    }
}

void CockpitWindow::Swatch(float cx, float cy, D2D1_COLOR_F col, bool sel, int idx)
{
    m_b->SetColor(col);
    m_rt->FillEllipse({{cx, cy}, 15.f, 15.f}, m_b);
    if (sel) {
        m_b->SetColor(CK::WHITE);
        m_rt->DrawEllipse({{cx, cy}, 18.f, 18.f}, m_b, 2.f);
    }
    m_zones.push_back({{cx-18, cy-18, cx+18, cy+18}, 5, nullptr, 0, 0, idx, 0});
}

void CockpitWindow::ButtonW(float x, float y, float w, float h,
                             const wchar_t* label, bool accent, int action, int idx)
{
    D2D1_RECT_F rc = {x, y, x+w, y+h};
    Fill (rc, accent ? CK::ACCENT : CK::CARD, h*.4f);
    Frame(rc, accent ? CK::ACCENT : CK::CARD_BD, h*.4f, 1.f);
    Txt(label, m_fBody, rc, accent ? CK::WHITE : CK::SUBTLE,
        DWRITE_TEXT_ALIGNMENT_CENTER);
    m_zones.push_back({rc, 3, nullptr, 0, 0, idx, action});
}

// ─────────────────────────────────────────────────────────────────────────────
//  Pages
// ─────────────────────────────────────────────────────────────────────────────
void CockpitWindow::PageApplication(float x, float y, float w)
{
    Txt(L"Cockpit", m_fH1, {x, y, x+w, y+34}, CK::WHITE);
    Txt(L"Personnalisez le comportement et l'esthétique de votre UltraIsland.",
        m_fSmall, {x, y+38, x+w, y+56}, CK::SUBTLE);
    float cy = y + 74;

    // ── Prévisualisation directe ────────────────────────────────────────
    Card(x, cy, w, 220);
    Txt(L"PRÉVISUALISATION DIRECTE", m_fSection, {x+18, cy+14, x+w-18, cy+30}, CK::ACCENT);
    {
        // fond dégradé simulé (2 bandes) + pilule dessinée avec les réglages courants
        D2D1_RECT_F pv = {x+18, cy+40, x+w-18, cy+200};
        Fill(pv, {0.16f, 0.10f, 0.22f, 1.f}, 12.f);
        Fill({pv.left, pv.top, pv.right, pv.top + 80}, {0.22f, 0.12f, 0.18f, 1.f}, 12.f);

        float pw = m_cfg->idleW, ph = m_cfg->idleH, pr = m_cfg->idleCR;
        float cxm = (pv.left + pv.right) * .5f;
        if (m_cfg->position == 0) cxm = pv.left + 30 + pw*.5f;
        if (m_cfg->position == 2) cxm = pv.right - 30 - pw*.5f;
        D2D1_RECT_F pill = {cxm - pw*.5f, pv.top + 8, cxm + pw*.5f, pv.top + 8 + ph};
        D2D1_COLOR_F base = m_cfg->adaptiveTint
            ? D2D1_COLOR_F{0.10f, 0.07f, 0.05f, m_cfg->baseOpacity}
            : D2D1_COLOR_F{m_cfg->accentR*0.22f, m_cfg->accentG*0.22f,
                           m_cfg->accentB*0.22f, m_cfg->baseOpacity};
        m_b->SetColor(base);
        m_rt->FillRoundedRectangle({pill, pr, pr}, m_b);
        m_b->SetColor({1,1,1,0.25f});
        m_rt->DrawRoundedRectangle({pill, pr, pr}, m_b, 1.f);
        Txt(L"12:34", m_fSmall, pill, CK::WHITE, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    cy += 236;

    // ── Intelligence des couleurs ───────────────────────────────────────
    Card(x, cy, w, 154);
    Txt(L"INTELLIGENCE DES COULEURS", m_fSection, {x+18, cy+14, x+w-18, cy+30}, CK::ACCENT);
    RowLabel(x+18, cy+40, L"Fond adaptatif",
             L"L'encoche affiche la pochette de l'album, floutée");
    Toggle(x + w - 66, cy + 44, &m_cfg->adaptiveTint);
    RowLabel(x+18, cy+76, L"Texture de grain (Noise)", L"Ajoute un aspect organique au verre");
    Toggle(x + w - 66, cy + 80, &m_cfg->grain);
    RowLabel(x+18, cy+112, L"Pochette sur l'écran de verrouillage",
             L"Fond du lock screen = album en cours (version MSIX)");
    Toggle(x + w - 66, cy + 116, &m_cfg->lockWallpaper);
    // Note : ambientOnUnlock est un toggle prévu — carte à ~186 px si on l'ajoute.
    // Pour l'instant on garde la carte à 154, l'option se règle par config.json.
}

void CockpitWindow::PageApparence(float x, float y, float w)
{
    Txt(L"Apparence", m_fH1, {x, y, x+w, y+34}, CK::WHITE);
    Txt(L"Personnalisez l'esthétique de votre îlot.",
        m_fSmall, {x, y+38, x+w, y+56}, CK::SUBTLE);
    float cy = y + 74;

    Card(x, cy, w, 150);
    Txt(L"PALETTE DE COULEURS", m_fSection, {x+18, cy+14, x+w-18, cy+30}, CK::ACCENT);
    Txt(L"Couleur d'accentuation (barres, pastilles). « Adaptatif » suit la pochette.",
        m_fSmall, {x+18, cy+34, x+w-18, cy+50}, CK::MUTED);

    // Pastille « adaptatif » (dégradé simulé) + 8 couleurs
    float sx = x + 44, sy = cy + 86;
    {
        bool sel = m_cfg->adaptiveTint;
        m_b->SetColor({0.55f, 0.35f, 0.95f, 1.f});
        m_rt->FillEllipse({{sx, sy}, 15.f, 15.f}, m_b);
        m_b->SetColor({0.95f, 0.55f, 0.25f, 1.f});
        m_rt->FillEllipse({{sx+5, sy-4}, 7.f, 7.f}, m_b);
        if (sel) { m_b->SetColor(CK::WHITE); m_rt->DrawEllipse({{sx, sy}, 18.f, 18.f}, m_b, 2.f); }
        m_zones.push_back({{sx-18, sy-18, sx+18, sy+18}, 5, nullptr, 0, 0, -1, 0});
        Txt(L"Adaptatif", m_fSmall, {sx-30, sy+22, sx+34, sy+38}, CK::MUTED,
            DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    for (int i = 0; i < 8; ++i) {
        float cxs = sx + 62.f + i * 52.f;
        bool sel = !m_cfg->adaptiveTint
                && fabsf(m_cfg->accentR - ACCENTS[i].r) < 0.01f
                && fabsf(m_cfg->accentG - ACCENTS[i].g) < 0.01f
                && fabsf(m_cfg->accentB - ACCENTS[i].b) < 0.01f;
        Swatch(cxs, sy, ACCENTS[i], sel, i);
    }
    cy += 166;

    Card(x, cy, w, 84);
    Txt(L"GLASSMORPHISME", m_fSection, {x+18, cy+14, x+w-18, cy+30}, CK::ACCENT);
    RowLabel(x+18, cy+40, L"Opacité du verre", L"Réglable aussi dans la page Encoche");
    SliderF(x + w - 320, cy + 52, 200.f, &m_cfg->baseOpacity,
            0.40f, 1.00f, L"%.2f");
    cy += 100;

    // ── Typographie (Phase B) ─────────────────────────────────────────────
    Card(x, cy, w, 172);
    Txt(L"TYPOGRAPHIE", m_fSection, {x+18, cy+14, x+w-18, cy+30}, CK::ACCENT);
    RowLabel(x+18, cy+40, L"Police de caractères",
             L"Segoe UI Variable est la police système par défaut");
    static const wchar_t* FONTS[4] = {L"Segoe UI", L"Inter", L"Manrope", L"IBM Plex"};
    Segmented(x + w - 400, cy + 46, 380, 30, &m_cfg->fontFamily, FONTS, 4);
    RowLabel(x+18, cy+80, L"Graisse", nullptr);
    static const wchar_t* WEIGHTS[3] = {L"Normal", L"Semi-Bold", L"Bold"};
    Segmented(x + w - 300, cy + 84, 280, 30, &m_cfg->fontWeight, WEIGHTS, 3);
    RowLabel(x+18, cy+120, L"Taille du texte", nullptr);
    SliderF(x + w - 300, cy + 128, 200.f, &m_cfg->fontScale, 0.80f, 1.40f, L"%.2fx");
}

void CockpitWindow::PageSons(float x, float y, float w)
{
    Txt(L"Sons & Notifications", m_fH1, {x, y, x+w, y+34}, CK::WHITE);
    Txt(L"Personnalisez l'expérience auditive de votre îlot dynamique.",
        m_fSmall, {x, y+38, x+w, y+56}, CK::SUBTLE);
    float cy = y + 74;

    struct Row {
        const wchar_t* t; const wchar_t* s;
        bool*  on; float* vol; int* var; int play;
        const wchar_t* const* opts; int nOpts;
    };
    int nE, nN, nD;
    auto expandOpts = Sounds::ExpandNames(nE);
    auto notifOpts  = Sounds::NotifNames (nN);
    auto deviceOpts = Sounds::DeviceNames(nD);
    Row rows[3] = {
        {L"Ouverture îlot",       L"Se déclenche quand l'îlot s'agrandit",           &m_cfg->soundExpand, &m_cfg->volExpand, &m_cfg->soundExpandVar, 20, expandOpts, nE},
        {L"Nouvelle notification",L"Alerte pour les messages",                       &m_cfg->soundNotif,  &m_cfg->volNotif,  &m_cfg->soundNotifVar,  21, notifOpts,  nN},
        {L"Appareil connecté",    L"Confirmation au branchement d'un périphérique",  &m_cfg->soundDevice, &m_cfg->volDevice, &m_cfg->soundDeviceVar, 22, deviceOpts, nD},
    };
    for (auto& r : rows) {
        Card(x, cy, w, 128);
        RowLabel(x+18, cy+16, r.t, r.s);
        Toggle (x + w - 66, cy + 20, r.on);
        // Variante — menu déroulant (Segmented)
        Segmented(x + 18, cy + 54, w - 36, 26, r.var, r.opts, r.nOpts);
        // Volume + preview
        SliderF (x + 18, cy + 96, w - 220, r.vol, 0.f, 1.f, L"%.2f");
        ButtonW (x + w - 92, cy + 88, 70, 28, L"▶", false, r.play);
        cy += 140;
    }
}

void CockpitWindow::PageAutoris(float x, float y, float w)
{
    Txt(L"Autorisations", m_fH1, {x, y, x+w, y+34}, CK::WHITE);
    Txt(L"Permissions et intégrations système.",
        m_fSmall, {x, y+38, x+w, y+56}, CK::SUBTLE);
    float cy = y + 74;

    Card(x, cy, w, 96);
    Txt(L"SPOTIFY", m_fSection, {x+18, cy+14, x+w-18, cy+30}, CK::ACCENT);
    if (SpotifyTokenExists()) {
        RowLabel(x+18, cy+40, L"File « Playing Next »", L"Compte lié — file synchronisée");
        Txt(L"● Connecté", m_fBody, {x + w - 330, cy + 48, x + w - 190, cy + 72},
            CK::GREEN, DWRITE_TEXT_ALIGNMENT_TRAILING);
        ButtonW(x + w - 170, cy + 44, 150, 32, L"Déconnecter", false, 31);
    } else {
        RowLabel(x+18, cy+40, L"File « Playing Next »",
                 L"OAuth Spotify (compte Premium requis)");
        ButtonW(x + w - 170, cy + 44, 150, 32, L"Connecter Spotify", true, 30);
    }
    cy += 112;

    Card(x, cy, w, 128);
    Txt(L"SYSTÈME", m_fSection, {x+18, cy+14, x+w-18, cy+30}, CK::ACCENT);
    RowLabel(x+18, cy+40, L"Démarrer avec Windows", L"Lancement à l'ouverture de session");
    Toggle(x + w - 66, cy + 44, &m_cfg->startWithWindows);
    RowLabel(x+18, cy+82, L"Démarrer en administrateur", L"UAC au lancement — privilèges étendus");
    Toggle(x + w - 66, cy + 86, &m_cfg->runAsAdmin);
    cy += 144;

    Card(x, cy, w, 76);
    Txt(L"NOTIFICATIONS RÉELLES", m_fSection, {x+18, cy+14, x+w-18, cy+30}, CK::ACCENT);
    RowLabel(x+18, cy+38, L"Interception des notifications Windows",
             L"Nécessite le packaging MSIX — bientôt disponible");
    Txt(L"À VENIR", m_fSection, {x + w - 100, cy + 42, x + w - 18, cy + 62}, CK::MUTED,
        DWRITE_TEXT_ALIGNMENT_TRAILING);
}

void CockpitWindow::PageEncoche(float x, float y, float w)
{
    Txt(L"Configuration de l'Encoche", m_fH1, {x, y, x+w, y+34}, CK::WHITE);
    Txt(L"Adaptez l'affichage d'UltraIsland à la géométrie de votre matériel.",
        m_fSmall, {x, y+38, x+w, y+56}, CK::SUBTLE);

    // État global (droite du titre)
    Txt(L"ÉTAT", m_fSection, {x + w - 150, y, x + w - 60, y + 18}, CK::MUTED,
        DWRITE_TEXT_ALIGNMENT_TRAILING);
    Toggle(x + w - 48, y + 2, &m_cfg->islandEnabled);

    float cy = y + 74;
    float colW = (w - 20) * 0.5f;

    // ── Formes prédéfinies ──────────────────────────────────────────────
    Card(x, cy, colW, 108);
    Txt(L"FORMES PRÉDÉFINIES", m_fSection, {x+18, cy+14, x+colW-18, cy+30}, CK::ACCENT);
    static const wchar_t* SHAPES[5] = {L"Pilule", L"Rectangle", L"Cercle", L"Goutte", L"Courbe"};
    Segmented(x+18, cy+46, colW-36, 36, &m_cfg->shape, SHAPES, 5, 1);

    // ── Géométrie ───────────────────────────────────────────────────────
    Card(x + colW + 20, cy, colW, 196);
    {
        float gx = x + colW + 38, gw = colW - 130;
        Txt(L"GÉOMÉTRIE & DIMENSIONS", m_fSection,
            {gx, cy+14, gx+colW, cy+30}, CK::ACCENT);
        Txt(L"Largeur",       m_fSmall, {gx, cy+42, gx+150, cy+58}, CK::SUBTLE);
        SliderF(gx, cy+64, gw, &m_cfg->idleW, 120.f, 320.f, L"%.0f px");
        Txt(L"Hauteur",       m_fSmall, {gx, cy+88, gx+150, cy+104}, CK::SUBTLE);
        SliderF(gx, cy+110, gw, &m_cfg->idleH, 22.f, 48.f, L"%.0f px");
        Txt(L"Rayon d'angle", m_fSmall, {gx, cy+134, gx+150, cy+150}, CK::SUBTLE);
        SliderF(gx, cy+156, gw, &m_cfg->idleCR, 4.f, 24.f, L"%.0f px");
    }

    // ── Style ───────────────────────────────────────────────────────────
    float cy2 = cy + 124;
    Card(x, cy2, colW, 88);
    Txt(L"STYLE & APPARENCE", m_fSection, {x+18, cy2+14, x+colW-18, cy2+30}, CK::ACCENT);
    Txt(L"Opacité", m_fSmall, {x+18, cy2+40, x+150, cy2+56}, CK::SUBTLE);
    SliderF(x+18, cy2+62, colW - 130, &m_cfg->baseOpacity, 0.40f, 1.00f, L"%.2f");

    // ── Position ────────────────────────────────────────────────────────
    float cy3 = cy2 + 104;
    Card(x, cy3, colW, 104);
    Txt(L"POSITIONNEMENT ÉCRAN", m_fSection, {x+18, cy3+14, x+colW-18, cy3+30}, CK::ACCENT);
    static const wchar_t* POSN[3] = {L"Haut-Gauche", L"Haut-Centre", L"Haut-Droite"};
    Segmented(x+18, cy3+46, colW-36, 36, &m_cfg->position, POSN, 3);
}

// ─────────────────────────────────────────────────────────────────────────────
//  OnPaint — sidebar + page + barre de boutons
// ─────────────────────────────────────────────────────────────────────────────
void CockpitWindow::OnPaint()
{
    if (!EnsureDevRes()) return;
    m_zones.clear();

    m_rt->BeginDraw();
    m_rt->Clear(CK::BG);
    D2D1_SIZE_F sz = m_rt->GetSize();

    // ── Sidebar ─────────────────────────────────────────────────────────
    Fill({0, 0, SB_W, sz.height}, CK::SIDEBAR);
    // Logo : pilule décorative + nom
    Fill({24, 26, 76, 48}, {0.16f, 0.16f, 0.20f, 1.f}, 11.f);
    Txt(L"UltraIsland", m_fH2, {88, 24, SB_W - 8, 50}, CK::WHITE);

    struct Item { const wchar_t* icon; const wchar_t* label; };
    static const Item ITEMS[5] = {
        {L"", L"Application"},
        {L"", L"Apparence"},
        {L"", L"Sons & Notifications"},
        {L"", L"Autorisations"},
        {L"", L"Encoche"},
    };
    for (int i = 0; i < 5; ++i) {
        float iy = 84.f + i * 46.f;
        D2D1_RECT_F rc = {12, iy, SB_W - 12, iy + 40};
        if (m_page == i) Fill(rc, {0.13f, 0.13f, 0.17f, 1.f}, 10.f);
        Txt(ITEMS[i].icon,  m_fIcon, {26, iy, 50, iy + 40},
            m_page == i ? CK::ACCENT : CK::MUTED);
        Txt(ITEMS[i].label, m_fBody, {56, iy, SB_W - 16, iy + 40},
            m_page == i ? CK::WHITE : CK::SUBTLE);
        m_zones.push_back({rc, 4, nullptr, 0, 0, i, 0});
    }
    Txt(L"VERSION 2.4.0", m_fSection, {24, sz.height - 58, SB_W - 24, sz.height - 42}, CK::MUTED);
    Txt(L"UltraIsland Pro", m_fSmall, {24, sz.height - 40, SB_W - 24, sz.height - 24}, CK::SUBTLE);

    // ── Page active (avec offset de scroll + clip à la zone visible) ────
    float by = sz.height - 64.f;                 // haut de la barre d'actions
    float px = SB_W + 36.f, pw = sz.width - px - 36.f;
    float py = 30.f - m_scroll;                  // défilement vertical appliqué
    // Clip : le contenu scrollé ne doit pas déborder sous la barre d'actions
    // ni recouvrir la sidebar.
    m_rt->PushAxisAlignedClip({SB_W, 0.f, sz.width, by - 12.f},
                              D2D1_ANTIALIAS_MODE_ALIASED);
    m_pageMaxY = 0.f;                            // remis à zéro : on mesure la page
    switch (m_page) {
    case 0: PageApplication(px, py, pw); break;
    case 1: PageApparence  (px, py, pw); break;
    case 2: PageSons       (px, py, pw); break;
    case 3: PageAutoris    (px, py, pw); break;
    case 4: PageEncoche    (px, py, pw); break;
    }
    // Hauteur du contenu (repère non-scrollé) = bas dessiné - origine page + marge.
    m_contentH = (m_pageMaxY - py) + 30.f;
    m_rt->PopAxisAlignedClip();

    // ── Barre d'actions commune (fixe, hors clip) ───────────────────────
    Fill({SB_W, by - 12, sz.width, sz.height}, CK::BG);
    ButtonW(px, by, 220, 40, L"✓  Appliquer", true, 2);
    ButtonW(px + 236, by, 170, 40, L"Réinitialiser", false, 3);

    m_rt->EndDraw();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Interaction
// ─────────────────────────────────────────────────────────────────────────────
int CockpitWindow::HitZone(int x, int y) const
{
    for (int i = (int)m_zones.size() - 1; i >= 0; --i) {
        const auto& z = m_zones[i];
        if (x >= z.rc.left && x <= z.rc.right && y >= z.rc.top && y <= z.rc.bottom)
            return i;
    }
    return -1;
}

void CockpitWindow::ClampScroll()
{
    // m_contentH = bas naturel du contenu (repère y=0). La zone de contenu visible
    // va de y=0 au haut de la barre d'actions (s.height-76). Le scroll max amène
    // donc le bas du contenu au bas de la zone visible.
    float visibleBottom = 0.f;
    if (m_rt) { D2D1_SIZE_F s = m_rt->GetSize(); visibleBottom = s.height - 76.f; }
    float maxS = std::max(0.f, m_contentH - visibleBottom);
    m_scroll = std::clamp(m_scroll, 0.f, maxS);
}

void CockpitWindow::ApplySliderDrag(int x)
{
    if (m_drag < 0 || m_drag >= (int)m_zones.size()) return;
    const auto& z = m_zones[m_drag];
    if (z.kind != 1 || !z.val) return;
    float t = std::clamp(((float)x - z.rc.left - 8.f) / (z.rc.right - z.rc.left - 16.f), 0.f, 1.f);
    *(float*)z.val = z.mn + t * (z.mx - z.mn);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void CockpitWindow::OnMouseMove(int x, int y)
{
    m_mouse = {x, y};
    if (m_drag >= 0) { ApplySliderDrag(x); return; }
    int h = HitZone(x, y);
    if (h != m_hot) { m_hot = h; }
}

void CockpitWindow::OnMouseDown(int x, int y)
{
    int h = HitZone(x, y);
    if (h < 0) return;
    const auto& z = m_zones[h];
    if (z.kind == 1) { m_drag = h; SetCapture(m_hwnd); ApplySliderDrag(x); }
}

void CockpitWindow::OnMouseUp(int x, int y)
{
    if (m_drag >= 0) { m_drag = -1; ReleaseCapture(); return; }
    int h = HitZone(x, y);
    if (h < 0) return;
    const auto& z = m_zones[h];
    switch (z.kind) {
    case 0:   // toggle
        if (z.val) { *(bool*)z.val = !*(bool*)z.val; }
        break;
    case 2:   // segmented
        if (z.val) { *(int*)z.val = z.idx; if (z.action) DoAction(z.action, z.idx); }
        break;
    case 3:   // bouton
        DoAction(z.action, z.idx);
        break;
    case 4:   // sidebar
        m_page = z.idx;
        m_scroll = 0.f;   // repartir en haut à chaque changement de page
        break;
    case 5:   // palette (idx -1 = adaptatif)
        if (z.idx < 0) m_cfg->adaptiveTint = true;
        else {
            m_cfg->adaptiveTint = false;
            m_cfg->accentR = ACCENTS[z.idx].r;
            m_cfg->accentG = ACCENTS[z.idx].g;
            m_cfg->accentB = ACCENTS[z.idx].b;
        }
        break;
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void CockpitWindow::DoAction(int action, int idx)
{
    switch (action) {
    case 1:   // préréglage de forme
        if      (idx == 0) { m_cfg->idleW = 180; m_cfg->idleH = 26; m_cfg->idleCR = 13; }
        else if (idx == 1) { m_cfg->idleW = 210; m_cfg->idleH = 30; m_cfg->idleCR = 8;  }
        else if (idx == 2) { m_cfg->idleW = 44;  m_cfg->idleH = 44; m_cfg->idleCR = 22; }
        else if (idx == 3) { m_cfg->idleW = 160; m_cfg->idleH = 30; m_cfg->idleCR = 15; }  // Goutte
        else               { m_cfg->idleW = 220; m_cfg->idleH = 28; m_cfg->idleCR = 14; }  // Courbe
        break;

    case 2: { // Appliquer : save + registre Run + notifier l'île
        m_cfg->Save();
        // Démarrage Windows
        HKEY hKey = nullptr;
        if (RegOpenKeyEx(HKEY_CURRENT_USER,
                L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            if (m_cfg->startWithWindows) {
                wchar_t path[MAX_PATH] = {};
                GetModuleFileName(nullptr, path, MAX_PATH);
                RegSetValueEx(hKey, L"WindowsDynamicIsland", 0, REG_SZ,
                    (BYTE*)path, (DWORD)((wcslen(path)+1)*sizeof(wchar_t)));
            } else {
                RegDeleteValue(hKey, L"WindowsDynamicIsland");
            }
            RegCloseKey(hKey);
        }
        // Recharge l'île (si elle tourne)
        if (HWND isl = FindWindowW(L"UltraislandClass", nullptr))
            PostMessageW(isl, MSG_RELOADCFG, 0, 0);
        if (m_onApply) m_onApply();
        break;
    }

    case 3: { // Réinitialiser (garde les modules actifs)
        AppConfig def;
        def.islandEnabled     = m_cfg->islandEnabled;
        def.musicEnabled      = m_cfg->musicEnabled;
        def.notifEnabled      = m_cfg->notifEnabled;
        def.systemEnabled     = m_cfg->systemEnabled;
        def.lockScreenEnabled = m_cfg->lockScreenEnabled;
        *m_cfg = def;
        break;
    }

    case 20: Sounds::PlayExpand(m_cfg->volExpand, m_cfg->soundExpandVar); break;
    case 21: Sounds::PlayNotif (m_cfg->volNotif,  m_cfg->soundNotifVar);  break;
    case 22: Sounds::PlayDevice(m_cfg->volDevice, m_cfg->soundDeviceVar); break;

    case 30:  // Connecter Spotify (via l'île)
        if (HWND isl = FindWindowW(L"UltraislandClass", nullptr))
            PostMessageW(isl, MSG_SPOTIFY, 0, 0);
        break;

    case 31: { // Déconnecter Spotify : l'île purge tokens + file ; on supprime
               // aussi le fichier au cas où l'île ne tourne pas.
        if (HWND isl = FindWindowW(L"UltraislandClass", nullptr))
            PostMessageW(isl, MSG_SPOTIFY_LOGOUT, 0, 0);
        wchar_t p[MAX_PATH];
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", p, MAX_PATH)) {
            std::wstring f = std::wstring(p) + L"\\Ultraisland\\spotify.dat";
            DeleteFileW(f.c_str());
        }
        break;
    }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK CockpitWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    CockpitWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lp);
        self = reinterpret_cast<CockpitWindow*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<CockpitWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProc(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
        self->OnPaint();
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:  return 1;
    // Coords souris PHYSIQUES → LOGIQUES (÷ m_dpi) pour matcher les zones dessinées.
    case WM_MOUSEMOVE:   self->OnMouseMove((int)(GET_X_LPARAM(lp)/self->m_dpi), (int)(GET_Y_LPARAM(lp)/self->m_dpi)); return 0;
    case WM_LBUTTONDOWN: self->OnMouseDown((int)(GET_X_LPARAM(lp)/self->m_dpi), (int)(GET_Y_LPARAM(lp)/self->m_dpi)); return 0;
    case WM_LBUTTONUP:   self->OnMouseUp  ((int)(GET_X_LPARAM(lp)/self->m_dpi), (int)(GET_Y_LPARAM(lp)/self->m_dpi)); return 0;
    case WM_SIZE:
        // PIÈGE: ID2D1HwndRenderTarget::Resize() REMET le DPI du RT à 96. Sans
        // re-SetDpi, GetSize() renvoie la taille physique → layout minuscule/décalé
        // et zones de clic désalignées (m_dpi resterait 1.25). On ré-applique donc
        // SetDpi + m_dpi à CHAQUE resize/maximisation.
        if (self->m_rt) {
            self->m_rt->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
            UINT dpi = 96;
            if (HMODULE u32 = GetModuleHandleW(L"user32.dll"))
                if (auto fn = (UINT(WINAPI*)(HWND))GetProcAddress(u32, "GetDpiForWindow"))
                    dpi = fn(hwnd);
            self->m_rt->SetDpi((float)dpi, (float)dpi);
            self->m_dpi = dpi / 96.f;
            self->ClampScroll();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEWHEEL: {
        // Scroll vertical des pages (contenu Sons/Apparence dépassait sous la barre
        // d'actions sans moyen de défiler).
        float d = GET_WHEEL_DELTA_WPARAM(wp) / 120.f * 48.f;
        self->m_scroll -= d;
        self->ClampScroll();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        // Plancher de taille : le layout utilise des coords absolues (sidebar 224,
        // cartes fixes) qui se chevauchent sous une certaine taille.
        auto* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = 900;
        mmi->ptMinTrackSize.y = 640;
        return 0;
    }
    case WM_DESTROY:
        // Fermer le Cockpit ne quitte PAS l'app (l'île continue).
        self->DropDevRes();
        self->m_hwnd = nullptr;
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}
