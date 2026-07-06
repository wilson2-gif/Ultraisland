#pragma once
#include <Windows.h>
#include <functional>
#include <map>

// ─────────────────────────────────────────────────────────────────────────────
//  AppSettings  — options choisies par l'utilisateur au démarrage
// ─────────────────────────────────────────────────────────────────────────────
struct AppSettings
{
    bool islandEnabled      = true;
    bool musicEnabled       = true;
    bool notifEnabled       = true;
    bool systemEnabled      = true;
    bool lockScreenEnabled  = true;
    bool startWithWindows   = false;
    bool runAsAdmin         = false;   // relance élevée (UAC) au lancement
};

// ─────────────────────────────────────────────────────────────────────────────
//  SettingsWindow  — fenêtre de démarrage / configuration
// ─────────────────────────────────────────────────────────────────────────────
class SettingsWindow
{
public:
    SettingsWindow();
    ~SettingsWindow();

    // Affiche la fenêtre, attend la fermeture, retourne true si "Lancer" cliqué
    bool ShowAndWait(HINSTANCE hInstance, AppSettings& settings);
    bool WasLaunched() const { return m_launched; }

private:
    HWND  m_hwnd     = nullptr;
    bool  m_launched = false;
    AppSettings* m_settings = nullptr;

    // Contrôles
    HWND m_chkIsland  = nullptr;
    HWND m_chkMusic   = nullptr;
    HWND m_chkNotif   = nullptr;
    HWND m_chkSystem  = nullptr;
    HWND m_chkLock    = nullptr;
    HWND m_chkStartup = nullptr;
    HWND m_chkAdmin   = nullptr;
    HWND m_btnLaunch  = nullptr;
    HWND m_btnQuit    = nullptr;

    // État des switches (les BS_OWNERDRAW ne mémorisent PAS BM_SETCHECK de façon
    // fiable → on stocke nous-mêmes : id contrôle → coché)
    std::map<UINT, bool> m_toggle;

    // Géométrie de chaque carte (pour WM_PAINT : dessin du fond arrondi)
    struct CardRect { int y; int h; };
    CardRect m_cards[5] = {};

    // Ressources GDI
    HFONT  m_fontTitle   = nullptr;
    HFONT  m_fontSection = nullptr;
    HFONT  m_fontNormal  = nullptr;
    HFONT  m_fontSub     = nullptr;
    HFONT  m_fontHeader  = nullptr;
    HBRUSH m_bgBrush     = nullptr;
    HBRUSH m_cardBrush   = nullptr;
    HBRUSH m_accentBrush = nullptr;

    // Hover/animation des switches (id contrôle → t [0..1])
    HWND m_hoverCtrl = nullptr;
    DWORD m_lastTick = 0;

    void DrawSwitch (HDC hdc, const RECT& rc, bool on, bool hovered);
    void DrawButton (HDC hdc, const RECT& rc, const wchar_t* text, HFONT font, bool accent, bool hovered);
    void DrawCard   (HDC hdc, int x, int y, int w, int h);

    void CreateControls();
    void ApplySettings();
    void SetStartupRegistry(bool enable);

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
};
