#include "TrayIcon.h"
#include <strsafe.h>

TrayIcon::TrayIcon() = default;

TrayIcon::~TrayIcon()
{
    Remove();
}

bool TrayIcon::Install(HWND hwnd)
{
    ZeroMemory(&m_nid, sizeof(m_nid));
    m_nid.cbSize           = sizeof(NOTIFYICONDATA);
    m_nid.hWnd             = hwnd;
    m_nid.uID              = 1;
    m_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAY_ICON;
    m_nid.hIcon            = LoadIcon(nullptr, IDI_APPLICATION);
    StringCchCopy(m_nid.szTip, ARRAYSIZE(m_nid.szTip), L"Windows Dynamic Island");

    m_installed = Shell_NotifyIcon(NIM_ADD, &m_nid) == TRUE;
    return m_installed;
}

void TrayIcon::Remove()
{
    if (m_installed)
    {
        Shell_NotifyIcon(NIM_DELETE, &m_nid);
        m_installed = false;
    }
}

bool TrayIcon::HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_TRAY_ICON)
    {
        if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP)
        {
            ShowContextMenu(hwnd);
            return true;
        }
    }

    if (uMsg == WM_COMMAND)
    {
        switch (LOWORD(wParam))
        {
        case IDM_TRAY_QUIT:
            if (OnQuit) OnQuit();
            return true;
        case IDM_TRAY_MUSIC:
            if (OnTestMusic) OnTestMusic();
            return true;
        case IDM_TRAY_NOTIF:
            if (OnTestNotif) OnTestNotif();
            return true;
        }
    }

    return false;
}

void TrayIcon::ShowContextMenu(HWND hwnd)
{
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    AppendMenu(hMenu, MF_STRING, IDM_TRAY_MUSIC, L"▶  Test musique");
    AppendMenu(hMenu, MF_STRING, IDM_TRAY_NOTIF, L"🔔  Test notification");
    AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenu(hMenu, MF_STRING, IDM_TRAY_ABOUT, L"Windows Dynamic Island  v1.0");
    AppendMenu(hMenu, MF_GRAYED, IDM_TRAY_ABOUT, nullptr);
    AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenu(hMenu, MF_STRING, IDM_TRAY_QUIT,  L"Quitter");

    // Obligatoire pour que le menu se ferme correctement
    SetForegroundWindow(hwnd);

    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_RIGHTALIGN,
                   pt.x, pt.y, 0, hwnd, nullptr);

    DestroyMenu(hMenu);
}
