#include "SettingsWindow.h"
#include <dwmapi.h>
#include <commctrl.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")

// ── IDs ──────────────────────────────────────────────────────────────────────
#define ID_CHK_ISLAND   200
#define ID_CHK_MUSIC    201
#define ID_CHK_NOTIF    202
#define ID_CHK_SYSTEM   203
#define ID_CHK_LOCK     204
#define ID_CHK_STARTUP  205
#define ID_BTN_LAUNCH   206
#define ID_BTN_QUIT     207

// ── Couleurs ─────────────────────────────────────────────────────────────────
static const COLORREF C_BG     = RGB(15,  15,  20 );
static const COLORREF C_CARD   = RGB(26,  26,  34 );
static const COLORREF C_LINE   = RGB(48,  48,  60 );
static const COLORREF C_WHITE  = RGB(235, 235, 242);
static const COLORREF C_GRAY   = RGB(130, 130, 148);
static const COLORREF C_GREEN  = RGB(48,  205, 115);
static const COLORREF C_GBTN   = RGB(38,  175,  95);

static const int W = 480;
static const int H = 590;
static const int PAD = 28;

// ─────────────────────────────────────────────────────────────────────────────
SettingsWindow::SettingsWindow() = default;

SettingsWindow::~SettingsWindow()
{
    if (m_fontTitle)   DeleteObject(m_fontTitle);
    if (m_fontSection) DeleteObject(m_fontSection);
    if (m_fontNormal)  DeleteObject(m_fontNormal);
    if (m_bgBrush)     DeleteObject(m_bgBrush);
}

// ─────────────────────────────────────────────────────────────────────────────
bool SettingsWindow::ShowAndWait(HINSTANCE hInstance, AppSettings& settings)
{
    m_settings = &settings;

    m_fontTitle   = CreateFont(20,0,0,0,FW_BOLD,    0,0,0,DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
                    DEFAULT_PITCH, L"Segoe UI Variable Display");

    m_fontSection = CreateFont(11,0,0,0,FW_SEMIBOLD, 0,0,0,DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
                    DEFAULT_PITCH, L"Segoe UI Variable");

    m_fontNormal  = CreateFont(13,0,0,0,FW_NORMAL,   0,0,0,DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
                    DEFAULT_PITCH, L"Segoe UI Variable");

    m_bgBrush = CreateSolidBrush(C_BG);

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

    m_hwnd = CreateWindowEx(
        WS_EX_APPWINDOW,
        L"WDI_Settings",
        L"Ultraisland",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        px, py, W, H,
        nullptr, nullptr, hInstance, this);

    if (!m_hwnd) return false;

    // Activer le dark mode sur la barre de titre (Windows 10 20H1+)
    BOOL dark = TRUE;
    DwmSetWindowAttribute(m_hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));

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
//  CreateControls — positions calculées dynamiquement (pas de hardcode Y)
// ─────────────────────────────────────────────────────────────────────────────
void SettingsWindow::CreateControls()
{
    HINSTANCE hi = GetModuleHandle(nullptr);
    int cw = W - PAD * 2;
    int y  = 20;

    auto Label = [&](const wchar_t* t, int dy, HFONT f, int indent = 0) {
        HWND h = CreateWindowW(L"STATIC", t,
            WS_CHILD|WS_VISIBLE|SS_LEFT,
            PAD+indent, y, cw-indent, dy, m_hwnd,
            nullptr, hi, nullptr);
        SendMessage(h, WM_SETFONT, (WPARAM)f, TRUE);
        y += dy + 4;
        return h;
    };

    auto Check = [&](const wchar_t* t, UINT id, bool on) {
        HWND h = CreateWindowW(L"BUTTON", t,
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
            PAD+8, y, cw-8, 22, m_hwnd,
            (HMENU)(UINT_PTR)id, hi, nullptr);
        SendMessage(h, WM_SETFONT,   (WPARAM)m_fontNormal, TRUE);
        SendMessage(h, BM_SETCHECK,  on ? BST_CHECKED : BST_UNCHECKED, 0);
        y += 26;
        return h;
    };

    auto Sub = [&](const wchar_t* t) {
        HWND h = CreateWindowW(L"STATIC", t,
            WS_CHILD|WS_VISIBLE|SS_LEFT,
            PAD+10, y, cw-10, 15, m_hwnd,
            nullptr, hi, nullptr);
        SendMessage(h, WM_SETFONT, (WPARAM)m_fontSection, TRUE);
        y += 20;
        return h;
    };

    // Séparateur visuel (ligne via STATIC SS_ETCHEDHORZ ne marche pas en dark)
    // On utilise WM_PAINT pour les lignes — juste avancer Y ici
    auto Sep = [&]() { y += 10; };

    // ── Titre ────────────────────────────────────────────────────────────
    Label(L"Windows Dynamic Island", 26, m_fontTitle);
    Label(L"Activez les modules avant de lancer.", 18, m_fontSection);
    m_sepY[0] = y + 4; Sep();

    // ── General ───────────────────────────────────────────────────────────
    Label(L"GENERAL", 16, m_fontSection);
    m_chkIsland  = Check(L"  Activer Dynamic Island (pilule flottante)", ID_CHK_ISLAND,  m_settings->islandEnabled);
    m_chkStartup = Check(L"  Demarrer avec Windows",                     ID_CHK_STARTUP, m_settings->startWithWindows);
    m_sepY[1] = y + 4; Sep();

    // ── Musique ───────────────────────────────────────────────────────────
    Label(L"MUSIQUE", 16, m_fontSection);
    m_chkMusic = Check(L"  Afficher la musique en cours  (SMTC)", ID_CHK_MUSIC, m_settings->musicEnabled);
    Sub(L"  Spotify, YouTube Music, VLC, Windows Media Player...");
    m_sepY[2] = y + 4; Sep();

    // ── Notifications ─────────────────────────────────────────────────────
    Label(L"NOTIFICATIONS", 16, m_fontSection);
    m_chkNotif = Check(L"  Afficher les notifications systeme", ID_CHK_NOTIF, m_settings->notifEnabled);
    Sub(L"  Discord, Outlook, Teams, WhatsApp...");
    m_sepY[3] = y + 4; Sep();

    // ── Systeme ───────────────────────────────────────────────────────────
    Label(L"SYSTEME", 16, m_fontSection);
    m_chkSystem = Check(L"  Afficher CPU / RAM / Reseau (clic sur la pilule)", ID_CHK_SYSTEM, m_settings->systemEnabled);
    m_sepY[4] = y + 4; Sep();

    // ── Ecran de verrouillage ─────────────────────────────────────────────
    Label(L"ECRAN DE VERROUILLAGE", 16, m_fontSection);
    m_chkLock = Check(L"  Retracter l'island au verrouillage", ID_CHK_LOCK, m_settings->lockScreenEnabled);

    y += 14;

    // ── Boutons ───────────────────────────────────────────────────────────
    m_btnLaunch = CreateWindowW(L"BUTTON", L"  Activer Dynamic Island",
        WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|BS_FLAT,
        PAD, y, cw - 120, 40, m_hwnd,
        (HMENU)ID_BTN_LAUNCH, hi, nullptr);
    SendMessage(m_btnLaunch, WM_SETFONT, (WPARAM)m_fontNormal, TRUE);

    m_btnQuit = CreateWindowW(L"BUTTON", L"Quitter",
        WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|BS_FLAT,
        W - PAD - 110, y, 110, 40, m_hwnd,
        (HMENU)ID_BTN_QUIT, hi, nullptr);
    SendMessage(m_btnQuit, WM_SETFONT, (WPARAM)m_fontNormal, TRUE);
}

// ─────────────────────────────────────────────────────────────────────────────
void SettingsWindow::ApplySettings()
{
    if (!m_settings) return;
    m_settings->islandEnabled     = (SendMessage(m_chkIsland,  BM_GETCHECK,0,0)==BST_CHECKED);
    m_settings->musicEnabled      = (SendMessage(m_chkMusic,   BM_GETCHECK,0,0)==BST_CHECKED);
    m_settings->notifEnabled      = (SendMessage(m_chkNotif,   BM_GETCHECK,0,0)==BST_CHECKED);
    m_settings->systemEnabled     = (SendMessage(m_chkSystem,  BM_GETCHECK,0,0)==BST_CHECKED);
    m_settings->lockScreenEnabled = (SendMessage(m_chkLock,    BM_GETCHECK,0,0)==BST_CHECKED);
    m_settings->startWithWindows  = (SendMessage(m_chkStartup, BM_GETCHECK,0,0)==BST_CHECKED);
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
    // ── Création ──────────────────────────────────────────────────────────
    case WM_CREATE:
        self->CreateControls();
        return 0;

    // ── Pas d'effacement système (évite le flash blanc) ───────────────────
    case WM_ERASEBKGND:
        return 1;

    // ── Fond des contrôles STATIC et CHECKBOX ─────────────────────────────
    // Sur Windows 10/11, BS_AUTOCHECKBOX envoie WM_CTLCOLORSTATIC
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wp;
        SetBkMode   (hdc, TRANSPARENT);  // <- CRUCIAL : fond transparent
        SetTextColor(hdc, C_WHITE);
        return (LRESULT)self->m_bgBrush;
    }

    // ── Fond des boutons ──────────────────────────────────────────────────
    case WM_CTLCOLORBTN:
    {
        HDC  hdc  = (HDC)wp;
        HWND ctrl = (HWND)lp;
        SetBkMode(hdc, TRANSPARENT);
        if (ctrl == self->m_btnLaunch) {
            SetTextColor(hdc, RGB(10,10,10));
            static HBRUSH bGreen = CreateSolidBrush(C_GBTN);
            return (LRESULT)bGreen;
        }
        SetTextColor(hdc, C_WHITE);
        static HBRUSH bDark = CreateSolidBrush(RGB(50,50,62));
        return (LRESULT)bDark;
    }

    // ── Peinture personnalisée ────────────────────────────────────────────
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        // Fond
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, self->m_bgBrush);

        // Lignes séparatrices
        HPEN pen = CreatePen(PS_SOLID, 1, C_LINE);
        HPEN old = (HPEN)SelectObject(hdc, pen);
        for (int sy : self->m_sepY) {
            if (sy == 0) continue;
            MoveToEx(hdc, PAD, sy, nullptr);
            LineTo  (hdc, W - PAD, sy);
        }
        SelectObject(hdc, old);
        DeleteObject(pen);

        // Pastilles vertes devant les titres de section
        SetBkMode   (hdc, TRANSPARENT);
        SetTextColor(hdc, C_GREEN);
        SelectObject(hdc, self->m_fontSection);

        // On dessine la pastille 8px avant la position du label "GENERAL" etc.
        // Les positions Y sont celles juste après chaque sep + 2
        for (int i = 0; i < 5; ++i) {
            if (self->m_sepY[i] == 0) continue;
            TextOut(hdc, PAD, self->m_sepY[i] + 12, L"*", 1);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    // ── Commandes ─────────────────────────────────────────────────────────
    case WM_COMMAND:
        if (LOWORD(wp) == ID_BTN_LAUNCH) {
            self->ApplySettings();
            self->m_launched = true;
            DestroyWindow(hwnd);
        } else if (LOWORD(wp) == ID_BTN_QUIT) {
            self->m_launched = false;
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wp, lp);
}
