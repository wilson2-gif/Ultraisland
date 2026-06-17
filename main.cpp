// main.cpp — Windows Dynamic Island
#include <Windows.h>
#include <winrt/Windows.Foundation.h>
#include "Core/WindowManager.h"
#include "Core/SettingsWindow.h"
#include "Modules/MediaManager.h"
#include "Modules/NotificationManager.h"
#include "Modules/LockScreenManager.h"
#include "Modules/SystemMonitor.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    winrt::init_apartment();

    // ── 1. Fenêtre de configuration ───────────────────────────────────────
    AppSettings   settings;
    SettingsWindow settingsWin;

    bool launched = settingsWin.ShowAndWait(hInstance, settings);
    if (!launched || !settings.islandEnabled)
    {
        winrt::uninit_apartment();
        return 0;
    }

    // ── 2. Modules (uniquement ceux activés) ─────────────────────────────
    SystemMonitor        sysMonitor;
    WindowManager        windowManager;
    MediaManager         mediaManager;
    NotificationManager  notificationManager;
    LockScreenManager    lockScreenManager;

    if (settings.systemEnabled)
        sysMonitor.Start();

    // Callbacks
    if (settings.musicEnabled)
    {
        mediaManager.SetMediaChangedCallback(
            [&](const std::wstring& title,
                const std::wstring& artist,
                const std::wstring& sourceApp,
                bool playing,
                float progress,
                const std::vector<uint8_t>& thumbnailData)
            {
                windowManager.TriggerMusic(title, artist, sourceApp, playing, progress, thumbnailData);
            });
    }

    if (settings.notifEnabled)
    {
        notificationManager.SetNotificationCallback(
            [&](const std::wstring& app,
                const std::wstring& title,
                const std::wstring& msg)
            {
                windowManager.TriggerNotification(app, title, msg);
            });
    }

    if (settings.lockScreenEnabled)
    {
        lockScreenManager.SetLockScreenStatusCallback(
            [&](bool locked) { if (locked) windowManager.Collapse(); });
    }

    // ── 3. Initialisation des modules ─────────────────────────────────────
    if (settings.musicEnabled)      
    {
        mediaManager.Initialize();
        windowManager.SetMediaManager(&mediaManager);
    }
    if (settings.notifEnabled)      notificationManager.Initialize();
    if (settings.lockScreenEnabled) lockScreenManager.Initialize();

    SystemMonitor* sysPtr = settings.systemEnabled ? &sysMonitor : nullptr;
    windowManager.Initialize(hInstance, nCmdShow, sysPtr);

    // ── 4. Boucle principale ──────────────────────────────────────────────
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (settings.musicEnabled)      mediaManager.Shutdown();
    if (settings.notifEnabled)      notificationManager.Shutdown();
    if (settings.lockScreenEnabled) lockScreenManager.Shutdown();
    if (settings.systemEnabled)     sysMonitor.Stop();

    winrt::uninit_apartment();
    return static_cast<int>(msg.wParam);
}
