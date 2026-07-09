// main.cpp — Windows Dynamic Island (UltraIsland)
#include <Windows.h>
#include <shellapi.h>
#include <appmodel.h>   // GetCurrentPackageFullName / APPMODEL_ERROR_NO_PACKAGE (détection MSIX)
#include <winrt/Windows.Foundation.h>
#include "Core/WindowManager.h"
#include "Core/SettingsWindow.h"
#include "Core/AppConfig.h"
#include "Core/CockpitWindow.h"
#include "Modules/MediaManager.h"
#include "Modules/NotificationManager.h"
#include "Modules/LockScreenManager.h"
#include "Modules/SystemMonitor.h"

// Journal de démarrage — %LOCALAPPDATA%\Ultraisland\boot.log
// (diagnostique les blocages d'init sans debugger)
static void BootLog(const char* step)
{
    wchar_t path[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    if (!n) return;
    wcscat_s(path, L"\\Ultraisland\\boot.log");
    HANDLE h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    char line[160];
    int len = sprintf_s(line, "[%lu] %s\r\n", GetTickCount(), step);
    DWORD wr; WriteFile(h, line, (DWORD)len, &wr, nullptr);
    CloseHandle(h);
}

// Le processus est-il déjà élevé (admin) ?
static bool IsProcessElevated()
{
    bool elevated = false; HANDLE tok = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        TOKEN_ELEVATION te; DWORD sz = sizeof(te);
        if (GetTokenInformation(tok, TokenElevation, &te, sizeof(te), &sz))
            elevated = te.TokenIsElevated != 0;
        CloseHandle(tok);
    }
    return elevated;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow)
{
    // DPI Per-Monitor V2 — fallback si le manifeste n'est pas honoré (Win10 1607+).
    // Doit être appelé AVANT toute création de fenêtre.
    typedef BOOL (WINAPI *SetProcessDpiAwarenessContextFn)(HANDLE);
    if(HMODULE u32 = GetModuleHandleW(L"user32.dll")){
        auto fn = (SetProcessDpiAwarenessContextFn)
                  GetProcAddress(u32, "SetProcessDpiAwarenessContext");
        if(fn) fn((HANDLE)-4);   // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    }
    winrt::init_apartment();

    // ── 1. Configuration persistante ──────────────────────────────────────
    AppConfig cfg;
    bool hasCfg     = cfg.Load();
    bool isRelaunch = pCmdLine && wcsstr(pCmdLine, L"/elevated");   // relance UAC

    // First-run : pas de config → assistant de démarrage (une seule fois).
    // Ensuite, l'île démarre DIRECTEMENT ; le Cockpit (tray → Réglages…)
    // permet de tout modifier à chaud.
    if (!hasCfg && !isRelaunch) {
        AppSettings s;
        SettingsWindow first;
        if (!first.ShowAndWait(hInstance, s) || !s.islandEnabled) {
            winrt::uninit_apartment();
            return 0;
        }
        cfg.FromAppSettings(s);
        cfg.Save();
    }

    if (!cfg.islandEnabled) { winrt::uninit_apartment(); return 0; }

    // ── 2. Élévation à la demande (config runAsAdmin) ─────────────────────
    // IMPORTANT : sous MSIX, relancer via 'runas' lance l'exe HORS du contexte de
    // package → PERTE de l'identité Ultraisland.App → UserNotificationListener et
    // LockScreen CESSENT de fonctionner (or l'identité est justement le but du MSIX).
    // De plus AUCUNE action de l'app n'exige l'admin (radios Wi-Fi/BT, night light,
    // thème, luminosité, lockscreen marchent toutes en utilisateur standard). Donc :
    //  - packagé MSIX → on n'élève JAMAIS (l'invite UAC à chaque démarrage disparaît).
    //  - exe nu (non packagé) → l'ancien comportement runAsAdmin reste possible.
    {
        UINT32 len = 0;
        bool packaged = (GetCurrentPackageFullName(&len, nullptr) != APPMODEL_ERROR_NO_PACKAGE);
        if (!packaged && cfg.runAsAdmin && !IsProcessElevated()) {
            wchar_t path[MAX_PATH] = {}; GetModuleFileName(nullptr, path, MAX_PATH);
            SHELLEXECUTEINFOW sei = { sizeof(sei) };
            sei.lpVerb = L"runas"; sei.lpFile = path; sei.lpParameters = L"/elevated";
            sei.nShow = SW_SHOWNORMAL;
            if (ShellExecuteExW(&sei)) {          // l'instance élevée relit config.json
                winrt::uninit_apartment();
                return 0;
            }
            // UAC refusé → on continue sans élévation
        }
    }

    // ── 2b. Instance unique ───────────────────────────────────────────────
    // Sans ce garde-fou, lancer la version MSIX ET l'exe (ou deux fois l'exe,
    // ou un démarrage auto + un lancement manuel) empile plusieurs pilules
    // TOPMOST semi-transparentes AU MÊME ENDROIT : les couleurs se superposent
    // et les clics ne vont qu'à la fenêtre du dessus (souvent dans un autre
    // état) → « rien ne réagit ». FindWindow traverse la frontière MSIX↔exe
    // (même bureau) ; le mutex resserre la course au double-lancement simultané.
    // Placé APRÈS le bloc d'élévation : l'instance qui relance en admin quitte
    // avant d'arriver ici, donc elle ne se bloque pas elle-même.
    {
        HANDLE hInst = CreateMutexW(nullptr, TRUE, L"Local\\UltraislandSingleInstance");
        bool raceLoser = (hInst && GetLastError() == ERROR_ALREADY_EXISTS);
        HWND existing = FindWindowW(L"UltraislandClass", nullptr);
        // Course : mutex déjà pris mais fenêtre pas encore créée → on patiente.
        for (int i = 0; !existing && raceLoser && i < 50; ++i) {
            Sleep(20);
            existing = FindWindowW(L"UltraislandClass", nullptr);
        }
        if (existing) {
            // Une pilule tourne déjà → on la remet au premier plan et on quitte.
            SetWindowPos(existing, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            if (hInst) CloseHandle(hInst);
            winrt::uninit_apartment();
            return 0;
        }
        // Sinon : on est la première instance. Le handle reste ouvert jusqu'à la
        // fin du process (fermé par l'OS) pour signaler notre présence.
    }

    // ── 3. Runtime : config → PillRT (dimensions/position/opacité/accent) ──
    BootLog("cfg ok, apply runtime");
    cfg.ApplyRuntime();

    // ── 4. Modules ────────────────────────────────────────────────────────
    SystemMonitor        sysMonitor;
    WindowManager        windowManager;
    MediaManager         mediaManager;
    NotificationManager  notificationManager;
    LockScreenManager    lockScreenManager;
    CockpitWindow        cockpit;

    if (cfg.systemEnabled)
        sysMonitor.Start();

    // ── 5. Callbacks — délèguent via la queue thread-safe du WindowManager ─
    if (cfg.musicEnabled)
    {
        mediaManager.SetMediaChangedCallback(
            [&](const std::wstring& title,
                const std::wstring& artist,
                const std::wstring& sourceApp,
                bool playing,
                float progress,
                float durationSec,
                const std::vector<uint8_t>& thumbnailData)
            {
                windowManager.PostMusicUpdate(title, artist, sourceApp, playing,
                                              progress, durationSec, thumbnailData);
            });
    }

    if (cfg.notifEnabled)
    {
        notificationManager.SetNotificationCallback(
            [&](const std::wstring& app,
                const std::wstring& title,
                const std::wstring& msg)
            {
                windowManager.PostNotificationUpdate(app, title, msg);
            });
    }

    if (cfg.lockScreenEnabled)
    {
        lockScreenManager.SetLockScreenStatusCallback(
            [&](bool locked) {
                if (locked) {
                    // Ouvre l'overlay ambiant AVANT le vrai verrouillage Windows
                    windowManager.PostAmbientOpen();
                    windowManager.PostCollapseUpdate();
                } else if (HWND h = windowManager.GetHwnd()) {
                    // Au déverrouillage : ré-ouvre l'ambiant automatiquement SI musique
                    // (wParam=1 signale « auto » → soumis à ambientOnUnlock).
                    PostMessage(h, WM_ISLAND_AMBIENT, 1, 0);
                }
            });
    }

    // Tray « Réglages… » → Cockpit (non bloquant, l'île continue)
    windowManager.SetOnOpenSettings([&]() {
        cockpit.Open(hInstance, &cfg, nullptr);   // Appliquer notifie l'île lui-même
    });

    // ── 6. Initialisation ─────────────────────────────────────────────────
    if (cfg.musicEnabled)
    {
        BootLog("media init...");
        mediaManager.Initialize();
        BootLog("media init OK");
        windowManager.SetMediaManager(&mediaManager);
    }
    if (cfg.notifEnabled)      { BootLog("notif init..."); notificationManager.Initialize(); BootLog("notif init OK"); }
    if (cfg.lockScreenEnabled) { BootLog("lock init...");  lockScreenManager.Initialize();  BootLog("lock init OK"); }

    SystemMonitor* sysPtr = cfg.systemEnabled ? &sysMonitor : nullptr;
    BootLog("wm init...");
    windowManager.Initialize(hInstance, nCmdShow, sysPtr, &cfg);
    BootLog("wm init OK -> boucle messages");

    // ── 7. Boucle principale ──────────────────────────────────────────────
    MSG msg = {};
    BOOL ret;
    while ((ret = GetMessage(&msg, nullptr, 0, 0)) != 0)
    {
        if (ret == -1) break;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // ── 8. Shutdown ───────────────────────────────────────────────────────
    if (cfg.musicEnabled)      mediaManager.Shutdown();
    if (cfg.notifEnabled)      notificationManager.Shutdown();
    if (cfg.lockScreenEnabled) lockScreenManager.Shutdown();
    if (cfg.systemEnabled)     sysMonitor.Stop();

    winrt::uninit_apartment();
    return static_cast<int>(msg.wParam);
}
