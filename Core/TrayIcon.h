#pragma once
#include <Windows.h>
#include <shellapi.h>
#include <functional>

#pragma comment(lib, "shell32.lib")

// ────────────────────────────────────────────────────────────────────────────
//  TrayIcon  — icone dans la barre système + menu contextuel
// ────────────────────────────────────────────────────────────────────────────

#define WM_TRAY_ICON  (WM_APP + 100)
#define IDM_TRAY_QUIT  1001
#define IDM_TRAY_MUSIC 1002
#define IDM_TRAY_NOTIF 1003
#define IDM_TRAY_ABOUT 1004

class TrayIcon
{
public:
    TrayIcon();
    ~TrayIcon();

    bool Install(HWND hwnd);
    void Remove();

    // Retourner true si le message a ete traite
    bool HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    // Callbacks
    std::function<void()>                  OnQuit;
    std::function<void()>                  OnTestMusic;
    std::function<void()>                  OnTestNotif;

private:
    NOTIFYICONDATA m_nid = {};
    bool           m_installed = false;

    void ShowContextMenu(HWND hwnd);
};
