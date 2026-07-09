#include "SettingsWindow.h"
#include <dwmapi.h>
#include <commctrl.h>
#include <uxtheme.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "msimg32.lib")

// ── IDs ──────────────────────────────────────────────────────────────────────
#define ID_CHK_ISLAND   200
#define ID_CHK_MUSIC    201
#define ID_CHK_NOTIF    202
#define ID_CHK_SYSTEM   203
#define ID_CHK_LOCK     204
#define ID_CHK_STARTUP  205
#define ID_BTN_LAUNCH   206
#define ID_BTN_QUIT     207
#define ID_CHK_ADMIN    208

// ── Palette Liquid Glass dark (cohérente avec l'encoche) ─────────────────────
static const COLORREF C_BG      = RGB(12,  12,  16 );   // fond très sombre
static const COLORREF C_CARD    = RGB(22,  22,  28 );   // carte
static const COLORREF C_CARD_BD = RGB(38,  38,  46 );   // bordure carte
static const COLORREF C_WHITE   = RGB(240, 240, 245);
static const COLORREF C_SUBTLE  = RGB(160, 160, 174);
static const COLORREF C_MUTED   = RGB(110, 110, 124);
static const COLORREF C_ACCENT  = RGB( 60, 152, 255);   // bleu iOS
static const COLORREF C_ACCENT2 = RGB( 96, 174, 255);
static const COLORREF C_TRACK_OFF = RGB(52, 52, 62);   // rail du switch éteint

static const int W      = 500;
static const int H      = 750;     // 5 cartes (la dernière finit ~666) + marge ; plus de boutons
static const int PAD    = 24;
static const int CARD_R = 14;
static const int SWITCH_W = 42;
static const int SWITCH_H = 24;

// ─────────────────────────────────────────────────────────────────────────────
SettingsWindow::SettingsWindow() = default;

SettingsWindow::~SettingsWindow()
{
    if (m_fontTitle)   DeleteObject(m_fontTitle);
    if (m_fontSection) DeleteObject(m_fontSection);
    if (m_fontNormal)  DeleteObject(m_fontNormal);
    if (m_fontSub)     DeleteObject(m_fontSub);
    if (m_fontHeader)  DeleteObject(m_fontHeader);
    if (m_bgBrush)     DeleteObject(m_bgBrush);
    if (m_cardBrush)   DeleteObject(m_cardBrush);
    if (m_accentBrush) DeleteObject(m_accentBrush);
}

// ─────────────────────────────────────────────────────────────────────────────
bool SettingsWindow::ShowAndWait(HINSTANCE hInstance, AppSettings& settings)
{
    m_settings = &settings;

    auto MakeFont = [](int sz, int weight, const wchar_t* face){
        return CreateFont(sz, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH, face);
    };
    m_fontHeader  = MakeFont(28, FW_SEMIBOLD, L"Segoe UI Variable Display");
    m_fontTitle   = MakeFont(18, FW_SEMIBOLD, L"Segoe UI Variable Display");
    m_fontSection = MakeFont(11, FW_BOLD,     L"Segoe UI Variable Small");
    m_fontNormal  = MakeFont(14, FW_NORMAL,   L"Segoe UI Variable Text");
    m_fontSub     = MakeFont(12, FW_NORMAL,   L"Segoe UI Variable Small");

    m_bgBrush     = CreateSolidBrush(C_BG);
    m_cardBrush   = CreateSolidBrush(C_CARD);
    m_accentBrush = CreateSolidBrush(C_ACCENT);

    WNDCLASSEX wc    = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = L"WDI_Settings";
    wc.hbrBackground = m_bgBrush;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassEx(&wc);

    int px = (GetSystemMetrics(SM_CXSCREEN) - W) / 2;
    int py = (GetSystemMetrics(SM_CYSCREEN) - H) / 2;

    // WS_CLIPCHILDREN : le WM_PAINT custom du parent ne dessine plus par-dessus
    //                   les contrôles enfants (switches/boutons) → clics fiables.
    m_hwnd = CreateWindowEx(
        WS_EX_APPWINDOW,
        L"WDI_Settings",
        L"Ultraisland",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        px, py, W, H,
        nullptr, nullptr, hInstance, this);

    if (!m_hwnd) return false;

    // Dark mode sur la barre de titre (Windows 10 20H1+)
    BOOL dark = TRUE;
    DwmSetWindowAttribute(m_hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    // Coins arrondis Windows 11 (DWMWA_WINDOW_CORNER_PREFERENCE = 33, ROUND = 2)
    int corner = 2;
    DwmSetWindowAttribute(m_hwnd, 33, &corner, sizeof(corner));

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return m_launched;
}

// ─────────────────────────────────────────────────────────────────────────────
//  CreateControls — cartes + switches owner-draw
// ─────────────────────────────────────────────────────────────────────────────
void SettingsWindow::CreateControls()
{
    HINSTANCE hi = GetModuleHandle(nullptr);

    // Le header occupe ~95 px en haut. Y de départ des cartes.
    int y = 110;

    auto Card = [&](int rows, int slot){
        // rows = nb de switches dans la carte (1 switch = 44 px)
        int extraH = 18 + 22;   // padding interne haut+bas + label de section
        int h = extraH + rows * 44;
        m_cards[slot] = { y, h };
        y += h + 12;
    };

    // ── Switch owner-draw (BS_OWNERDRAW) ; l'état est stocké dans m_toggle ──
    auto Switch = [&](UINT id, bool on, int cardSlot, int rowInCard){
        int sy = m_cards[cardSlot].y + 16 /*pad*/ + 22 /*label section*/ + rowInCard * 44;
        int x  = PAD + 16;
        int w  = W - PAD*2 - 32;
        HWND h = CreateWindowW(L"BUTTON", L"",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            x, sy, w, 38, m_hwnd, (HMENU)(UINT_PTR)id, hi, nullptr);
        m_toggle[id] = on;   // ← source de vérité (BM_SETCHECK non fiable sur OWNERDRAW)
        return h;
    };

    // Slot 0 : GENERAL (3 lignes : island, startup, admin)
    // « Activer Dynamic Island » démarre sur OFF : le basculer sur ON = LANCER l'île.
    Card(3, 0);
    m_chkIsland  = Switch(ID_CHK_ISLAND,  false,                        0, 0);
    m_chkStartup = Switch(ID_CHK_STARTUP, m_settings->startWithWindows, 0, 1);
    m_chkAdmin   = Switch(ID_CHK_ADMIN,   m_settings->runAsAdmin,       0, 2);

    // Slot 1 : MUSIQUE (1 ligne)
    Card(1, 1);
    m_chkMusic = Switch(ID_CHK_MUSIC, m_settings->musicEnabled, 1, 0);

    // Slot 2 : NOTIFICATIONS (1 ligne)
    Card(1, 2);
    m_chkNotif = Switch(ID_CHK_NOTIF, m_settings->notifEnabled, 2, 0);

    // Slot 3 : SYSTÈME (1 ligne)
    Card(1, 3);
    m_chkSystem = Switch(ID_CHK_SYSTEM, m_settings->systemEnabled, 3, 0);

    // Slot 4 : VERROUILLAGE (1 ligne)
    Card(1, 4);
    m_chkLock = Switch(ID_CHK_LOCK, m_settings->lockScreenEnabled, 4, 0);

    // Plus de gros boutons : le switch « Activer Dynamic Island » EST le déclencheur.
}

// ─────────────────────────────────────────────────────────────────────────────
void SettingsWindow::ApplySettings()
{
    if (!m_settings) return;
    m_settings->islandEnabled     = m_toggle[ID_CHK_ISLAND];
    m_settings->musicEnabled      = m_toggle[ID_CHK_MUSIC];
    m_settings->notifEnabled      = m_toggle[ID_CHK_NOTIF];
    m_settings->systemEnabled     = m_toggle[ID_CHK_SYSTEM];
    m_settings->lockScreenEnabled = m_toggle[ID_CHK_LOCK];
    m_settings->startWithWindows  = m_toggle[ID_CHK_STARTUP];
    m_settings->runAsAdmin        = m_toggle[ID_CHK_ADMIN];
    SetStartupRegistry(m_settings->startWithWindows);
}

void SettingsWindow::SetStartupRegistry(bool enable)
{
    HKEY hKey = nullptr;
    if (RegOpenKeyEx(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) return;
    if (enable) {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileName(nullptr, path, MAX_PATH);
        RegSetValueEx(hKey, L"WindowsDynamicIsland", 0, REG_SZ,
            (BYTE*)path, (DWORD)((wcslen(path)+1)*sizeof(wchar_t)));
    } else {
        RegDeleteValue(hKey, L"WindowsDynamicIsland");
    }
    RegCloseKey(hKey);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Dessin d'une carte arrondie (fond légèrement plus clair que le bg)
// ─────────────────────────────────────────────────────────────────────────────
void SettingsWindow::DrawCard(HDC hdc, int x, int y, int w, int h)
{
    // Fond carte
    HBRUSH bf = CreateSolidBrush(C_CARD);
    HPEN   bp = CreatePen(PS_SOLID, 1, C_CARD_BD);
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, bf);
    HPEN   oldPn = (HPEN)  SelectObject(hdc, bp);
    RoundRect(hdc, x, y, x+w, y+h, CARD_R*2, CARD_R*2);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPn);
    DeleteObject(bf);
    DeleteObject(bp);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Switch iOS — fond du contrôle BS_OWNERDRAW
//  rc = rect total du contrôle ; on dessine : label gauche + switch droite
// ─────────────────────────────────────────────────────────────────────────────
void SettingsWindow::DrawSwitch(HDC hdc, const RECT& rc, bool on, bool /*hovered*/)
{
    const int sw = SWITCH_W, sh = SWITCH_H;
    int sx = rc.right - sw - 4;
    int sy = rc.top + (rc.bottom - rc.top - sh) / 2;

    // Rail
    HBRUSH br = CreateSolidBrush(on ? C_ACCENT : C_TRACK_OFF);
    HPEN   pn = CreatePen(PS_SOLID, 1, on ? C_ACCENT2 : RGB(70,70,82));
    HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
    HPEN   op = (HPEN)  SelectObject(hdc, pn);
    RoundRect(hdc, sx, sy, sx+sw, sy+sh, sh, sh);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(br); DeleteObject(pn);

    // Bouton (cercle blanc)
    int knobR = (sh - 6) / 2;
    int knobCx = on ? (sx + sw - knobR - 3) : (sx + knobR + 3);
    int knobCy = sy + sh / 2;
    HBRUSH kb = CreateSolidBrush(C_WHITE);
    HPEN   kp = CreatePen(PS_SOLID, 1, RGB(200,200,210));
    ob = (HBRUSH)SelectObject(hdc, kb);
    op = (HPEN)  SelectObject(hdc, kp);
    Ellipse(hdc, knobCx-knobR, knobCy-knobR, knobCx+knobR, knobCy+knobR);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(kb); DeleteObject(kp);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Bouton owner-draw (accent vert = launch, neutre = quitter)
// ─────────────────────────────────────────────────────────────────────────────
void SettingsWindow::DrawButton(HDC hdc, const RECT& rc, const wchar_t* text, HFONT font, bool accent, bool hovered)
{
    COLORREF fill, textCol;
    if (accent) {
        fill    = hovered ? RGB(80, 168, 255) : C_ACCENT;
        textCol = RGB(255,255,255);
    } else {
        fill    = hovered ? RGB(42, 42, 52) : RGB(34, 34, 42);
        textCol = C_SUBTLE;
    }

    HBRUSH bf = CreateSolidBrush(fill);
    HPEN   bp = CreatePen(PS_SOLID, 1, accent ? RGB(120,188,255) : C_CARD_BD);
    HBRUSH ob = (HBRUSH)SelectObject(hdc, bf);
    HPEN   op = (HPEN)  SelectObject(hdc, bp);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 22, 22);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(bf); DeleteObject(bp);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, textCol);
    HFONT of = (HFONT)SelectObject(hdc, font);
    DrawTextW(hdc, text, -1, (RECT*)&rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, of);
}

// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT msg,
                                          WPARAM wp, LPARAM lp)
{
    SettingsWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lp);
        self = reinterpret_cast<SettingsWindow*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProc(hwnd, msg, wp, lp);

    switch (msg)
    {
    case WM_CREATE:
        self->CreateControls();
        return 0;

    case WM_ERASEBKGND:
        return 1;

    // ── Fond des contrôles STATIC (legacy, plus utilisés mais safety) ──────
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
        HDC hdc = (HDC)wp;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, C_WHITE);
        return (LRESULT)self->m_bgBrush;
    }

    // ── Dessin custom des contrôles owner-draw ──────────────────────────────
    case WM_DRAWITEM:
    {
        LPDRAWITEMSTRUCT di = (LPDRAWITEMSTRUCT)lp;
        UINT id = (UINT)wp;
        HDC hdc = di->hDC;

        // Switch : label gauche + switch droite. Le texte du label est fixé par id.
        struct Row { UINT id; const wchar_t* title; const wchar_t* sub; };
        static const Row rows[] = {
            {ID_CHK_ISLAND,  L"Activer Dynamic Island",       L"Basculez sur ON pour lancer l'île maintenant"},
            {ID_CHK_STARTUP, L"Démarrer avec Windows",        L"Au démarrage de la session"},
            {ID_CHK_ADMIN,   L"Démarrer en administrateur",   L"UAC à chaque lancement — plus de privilèges"},
            {ID_CHK_MUSIC,   L"Musique en cours",             L"Spotify, YouTube Music, VLC, WMP…"},
            {ID_CHK_NOTIF,   L"Notifications système",        L"Discord, Outlook, Teams, WhatsApp…"},
            {ID_CHK_SYSTEM,  L"Statistiques système",         L"CPU, RAM, réseau, batterie"},
            {ID_CHK_LOCK,    L"Rétracter au verrouillage",    L"L'île se replie à l'écran de verrouillage"},
        };
        const Row* row = nullptr;
        for (const auto& r : rows) if (r.id == id) { row = &r; break; }
        if (!row) break;

        bool on = self->m_toggle[id];   // état réel (pas BM_GETCHECK)
        bool hovered = (di->itemState & ODS_HOTLIGHT) != 0;

        SetBkMode(hdc, TRANSPARENT);
        // Titre
        SetTextColor(hdc, C_WHITE);
        HFONT of = (HFONT)SelectObject(hdc, self->m_fontNormal);
        RECT tr = di->rcItem; tr.left += 6; tr.bottom = tr.top + 22;
        DrawTextW(hdc, row->title, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        // Sous-titre
        SetTextColor(hdc, C_MUTED);
        SelectObject(hdc, self->m_fontSub);
        RECT sr = di->rcItem; sr.left += 6; sr.top = tr.bottom - 4;
        DrawTextW(hdc, row->sub, -1, &sr, DT_LEFT | DT_TOP | DT_SINGLELINE);
        SelectObject(hdc, of);

        // Switch à droite
        self->DrawSwitch(hdc, di->rcItem, on, hovered);
        return TRUE;
    }

    // ── Peinture du fond : header + cartes ──────────────────────────────────
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc; GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, self->m_bgBrush);

        SetBkMode(hdc, TRANSPARENT);

        // ── Header ──────────────────────────────────────────────────────────
        // Petite "pilule" décorative (rappel de l'encoche)
        HBRUSH pillBr = CreateSolidBrush(RGB(35, 35, 44));
        HPEN   pillPn = CreatePen(PS_SOLID, 1, RGB(60, 60, 72));
        HBRUSH ob = (HBRUSH)SelectObject(hdc, pillBr);
        HPEN   op = (HPEN)  SelectObject(hdc, pillPn);
        RoundRect(hdc, PAD, 32, PAD+58, 32+22, 22, 22);
        SelectObject(hdc, ob); SelectObject(hdc, op);
        DeleteObject(pillBr); DeleteObject(pillPn);

        // Titre principal
        SetTextColor(hdc, C_WHITE);
        HFONT oldF = (HFONT)SelectObject(hdc, self->m_fontHeader);
        RECT tr = { PAD + 70, 24, W - PAD, 24 + 36 };
        DrawTextW(hdc, L"Ultraisland", -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Sous-titre
        SetTextColor(hdc, C_SUBTLE);
        SelectObject(hdc, self->m_fontSub);
        RECT sr = { PAD, 70, W - PAD, 90 };
        DrawTextW(hdc, L"Configurez les modules, puis activez Dynamic Island.", -1, &sr,
                  DT_LEFT | DT_TOP | DT_SINGLELINE);
        SelectObject(hdc, oldF);

        // ── Cartes ──────────────────────────────────────────────────────────
        static const wchar_t* SECTIONS[5] = {
            L"GÉNÉRAL", L"MUSIQUE", L"NOTIFICATIONS", L"SYSTÈME", L"VERROUILLAGE"
        };
        for (int i = 0; i < 5; ++i) {
            const auto& c = self->m_cards[i];
            if (c.h == 0) continue;
            self->DrawCard(hdc, PAD, c.y, W - PAD*2, c.h);

            // Label de section dans la carte
            SetTextColor(hdc, C_ACCENT2);
            HFONT of = (HFONT)SelectObject(hdc, self->m_fontSection);
            RECT lr = { PAD + 18, c.y + 14, PAD + W - PAD*2, c.y + 32 };
            DrawTextW(hdc, SECTIONS[i], -1, &lr, DT_LEFT | DT_TOP | DT_SINGLELINE);
            SelectObject(hdc, of);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    // ── Clic sur un switch = toggle de son état BM_CHECK ────────────────────
    case WM_COMMAND:
    {
        UINT id = LOWORD(wp);
        UINT code = HIWORD(wp);
        if (code == BN_CLICKED) {
            HWND hCtl = (HWND)lp;
            // Bascule l'état dans notre map (source de vérité)
            bool now = !self->m_toggle[id];
            self->m_toggle[id] = now;
            InvalidateRect(hCtl, nullptr, TRUE);

            // Le switch « Activer Dynamic Island » EST le bouton de lancement :
            // dès qu'on le met sur ON → on applique et on lance l'île.
            if (id == ID_CHK_ISLAND && now) {
                self->ApplySettings();          // lit toute la map (island=true inclus)
                self->m_launched = true;
                DestroyWindow(hwnd);
            }
        }
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wp, lp);
}
