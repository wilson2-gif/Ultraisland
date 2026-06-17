#pragma once
#include <Windows.h>
#include <functional>

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
    HWND m_btnLaunch  = nullptr;
    HWND m_btnQuit    = nullptr;

    // Positions Y des séparateurs (calculées dynamiquement dans CreateControls)
    int  m_sepY[5]    = {};

    // Ressources GDI
    HFONT  m_fontTitle   = nullptr;
    HFONT  m_fontSection = nullptr;
    HFONT  m_fontNormal  = nullptr;
    HBRUSH m_bgBrush     = nullptr;

    void CreateControls();
    void ApplySettings();
    void SetStartupRegistry(bool enable);

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
};
