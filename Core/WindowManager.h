#pragma once
#include <Windows.h>
#include <windowsx.h>
#include <memory>
#include <functional>
#include <map>
#include <algorithm>
#include <mutex>
#include <queue>
#include <variant>
#include "../Graphics/Renderer.h"
#include "../Graphics/AnimationEngine.h"
#include "../Modules/SpotifyClient.h"
#include "TrayIcon.h"
#include "AmbientWindow.h"

class SystemMonitor;
class MediaManager;

// ─────────────────────────────────────────────────────────────────────────────
//  Messages WM custom — dispatche les events background vers le thread UI
// ─────────────────────────────────────────────────────────────────────────────
static constexpr UINT WM_ISLAND_UPDATE    = WM_USER + 100;
static constexpr UINT WM_ISLAND_RELOADCFG = WM_USER + 101;  // Cockpit « Appliquer »
static constexpr UINT WM_ISLAND_SPOTIFY   = WM_USER + 102;  // Connexion Spotify demandée
static constexpr UINT WM_ISLAND_SPOTIFY_LOGOUT = WM_USER + 103;  // Déconnexion Spotify
static constexpr UINT WM_ISLAND_AMBIENT        = WM_USER + 104;  // ouvre le mode ambiant
                                                                 // (wParam=1 : auto/unlock)
static constexpr UINT WM_ISLAND_BT_RESULT      = WM_USER + 105;  // résultat scan Bluetooth (async)

struct AppConfig;

// ─────────────────────────────────────────────────────────────────────────────
//  Types de mises à jour postées depuis les threads background
// ─────────────────────────────────────────────────────────────────────────────
struct MusicUpdateData {
    std::wstring title, artist, sourceApp;
    bool  playing     = false;
    float progress    = 0.f;
    float durationSec  = 0.f;
    std::vector<uint8_t> thumbnailData;
};
struct NotifUpdateData {
    std::wstring appName, title, msg;
};
struct CollapseUpdateData {};
struct SpotifyQueueData { std::vector<SpotifyTrack> tracks; };

using IslandUpdateVariant = std::variant<MusicUpdateData, NotifUpdateData, CollapseUpdateData, SpotifyQueueData>;

// ─────────────────────────────────────────────────────────────────────────────
//  WindowManager  v8  — Thread-safe + Liquid
// ─────────────────────────────────────────────────────────────────────────────
class WindowManager
{
public:
    WindowManager();
    ~WindowManager();

    void Initialize(HINSTANCE hInstance, int nCmdShow,
                    SystemMonitor* sysMonitor = nullptr,
                    const AppConfig* cfg = nullptr);

    void SetContent      (const IslandContent& c);
    void SetMediaManager (MediaManager* mm) { m_mediaManager = mm; }
    HWND GetHwnd() const { return m_hwnd; }

    // Callback « Réglages… » du tray (ouvre le Cockpit — câblé par main.cpp)
    void SetOnOpenSettings(std::function<void()> fn) { m_onOpenSettings = std::move(fn); }

    // ── API publique thread-safe (appelable depuis n'importe quel thread) ──
    // Ces méthodes poussent dans la queue et postent WM_ISLAND_UPDATE.
    // Le thread principal draine la queue dans WindowProc.
    void PostMusicUpdate       (const std::wstring& title, const std::wstring& artist,
                                const std::wstring& sourceApp,
                                bool playing, float progress, float durationSec,
                                const std::vector<uint8_t>& thumbnailData);
    void PostNotificationUpdate(const std::wstring& appName,
                                const std::wstring& title,
                                const std::wstring& msg);
    void PostCollapseUpdate    ();
    void PostSpotifyQueue      (const std::vector<SpotifyTrack>& tracks);

    // Ouvre le mode ambiant depuis un thread de fond (thread-safe)
    void PostAmbientOpen();

    // Connecte Spotify (flow OAuth interactif) puis démarre le polling.
    bool ConnectSpotify();

    // ── API directe (thread UI uniquement) ───────────────────────────────
    void TriggerMusic       (const std::wstring& title, const std::wstring& artist,
                             const std::wstring& sourceApp = {},
                             bool playing = false, float progress = 0.f, float durationSec = 0.f,
                             const std::vector<uint8_t>& thumbnailData = {});
    void TriggerNotification(const std::wstring& appName,
                             const std::wstring& title,
                             const std::wstring& msg);
    void TriggerHUD         (HUDType type, float value, const std::wstring& label);
    void TriggerSystem      ();
    void Collapse           ();
    void UpdateMusicProgress(float progress);

    Renderer* GetRenderer() { return m_renderer.get(); }

private:
    HWND       m_hwnd      = nullptr;
    HINSTANCE  m_hInstance = nullptr;
    float      m_dpiScale  = 1.0f;   // 1.0=100%, 1.25=125%, 1.5=150%…

    std::unique_ptr<Renderer>        m_renderer;
    std::unique_ptr<AnimationEngine> m_animation;
    std::unique_ptr<TrayIcon>        m_tray;
    SystemMonitor*                   m_sysMonitor = nullptr;

    IslandContent m_content;
    int m_screenW = 0, m_screenH = 0;
    int m_currentW = 0, m_currentH = 0;

    // Largeur d'écran en unités LOGIQUES (= physique / dpiScale). Le dessin (D2D
    // via SetDpi) et WM_NCHITTEST raisonnent en logiques ; les coords de clic
    // reçues par OnMouse* sont déjà divisées par m_dpiScale. TOUT hit-test qui
    // appelle PillX doit donc passer par cette largeur logique — sinon, à 125 %,
    // la pilule est calculée à (1920-pw)/2 au lieu de (1536-pw)/2 → décalage de
    // ~192 px → les clics tombent à côté des boutons.
    float ScreenWLogical() const { return m_dpiScale > 0.f ? m_screenW / m_dpiScale : (float)m_screenW; }

    bool  m_hoverExpanded = false;
    bool  m_mouseTracking = false;

    MediaManager* m_mediaManager = nullptr;
    const AppConfig* m_appCfg = nullptr;          // config (sons, modules)
    std::function<void()> m_onOpenSettings;       // ouvre le Cockpit
    AmbientWindow m_ambient;                      // mode verrouillage/ambiant
    SpotifyClient m_spotify;
    std::vector<MusicQueueItem> m_spotifyQueue;   // alimenté par /me/player/queue
    float m_queueScroll = 0.f;                    // défilement liste Playing Next
    int  m_activeMenu = 1;
    bool m_showQueue  = false;
    std::vector<MusicQueueItem> m_recentTracks;
    DWORD m_trackStartTick = 0;

    // Progression musicale continue (avance entre 2 events SMTC)
    float m_musicBaseSec = 0.f;   // position connue au dernier event SMTC
    DWORD m_musicPosTick = 0;     // GetTickCount de cet event

    std::vector<NotifHistoryItem> m_notifHistory;

    std::map<std::wstring, AppBadge> m_appBadges;
    float m_badgePulseT = 0.f;

    bool  m_wifiEnabled   = false;
    std::wstring m_wifiSSID;
    bool  m_btEnabled     = false;
    bool  m_darkMode      = false;
    bool  m_nightLight    = false;
    float m_sysVolume     = 0.5f;
    float m_sysBrightness = 0.5f;

    IslandState m_preNotifState  = IslandState::Idle;
    DWORD       m_notifDismissAt = 0;

    float m_tabSlideT        = 1.f;
    DWORD m_tabSlideStart    = 0;
    DWORD m_tabSlideDuration = 300;
    float m_tabSlideDir      = 0.f;
    int   m_tabPrevMenu      = 1;

    bool  m_dragSlider    = false;
    int   m_dragSliderIdx = 0;
    bool  m_dragSeek      = false;   // drag de la barre de progression musique
    int   m_refreshDiv    = 0;       // diviseur du resync média (~2 s)
    DWORD  m_lockArtTick  = 0;       // throttle fond d'écran de verrouillage
    size_t m_lockArtBytes = 0;

    DWORD m_mouseDownTime   = 0;
    int   m_mouseDownX      = 0;
    int   m_mouseDownY      = 0;
    bool  m_mouseDownOnWifi = false;
    bool  m_longPressArmed  = false;

    bool  m_gestureActive = false;
    LONG  m_gesturePrevX  = 0;
    float m_gestureAccumX = 0.f;

    std::vector<WifiNetworkItem> m_wifiNetworks;
    std::vector<BtDeviceItem>    m_btDevices;
    // Scan Bluetooth ASYNCHRONE (les appels WinRT .get() bloquants gèleraient le
    // thread UI). Un thread de fond remplit m_btPending sous mutex puis poste
    // WM_ISLAND_BT_RESULT ; le thread UI recopie vers m_btDevices/m_content.
    std::mutex                   m_btMutex;
    std::vector<BtDeviceItem>    m_btPending;
    bool                         m_btScanning = false;   // (thread UI uniquement)

    // ── Saisie mot de passe Wi-Fi in-island ──────────────────────────────
    bool         m_wifiPassMode = false;   // panneau de saisie affiché
    std::wstring m_wifiPassSsid;           // SSID en cours de connexion
    std::wstring m_wifiPassInput;          // mot de passe tapé
    bool         m_wifiPassSecured = true; // le réseau ciblé exige un mot de passe

    // ── Thread-safe update queue ─────────────────────────────────────────
    std::mutex                         m_queueMutex;
    std::queue<IslandUpdateVariant>    m_updateQueue;

    // Draine la queue et applique toutes les mises à jour en attente
    // → Appelé uniquement depuis le thread UI (WindowProc WM_ISLAND_UPDATE)
    void DrainUpdateQueue();

    // ── Timers ────────────────────────────────────────────────────────────
    static constexpr int   TOP_MARGIN      = 0;
    static constexpr UINT  TIMER_ANIM      = 1;
    static constexpr UINT  TIMER_CLOCK     = 2;
    static constexpr UINT  TIMER_CHECK     = 3;
    static constexpr UINT  TIMER_LONGPRESS = 4;
    static constexpr DWORD NOTIF_MS        = 2000;
    static constexpr DWORD HUD_MS          = 3000;
    static constexpr float SWIPE_THRESH    = 72.f;

    void ReloadConfig();          // WM_ISLAND_RELOADCFG — réapplique PillRT + géométrie
    void ToggleAmbient();         // mode ambiant (tray / Ctrl+Alt+L)
    float SeekFracFromX(int x) const;             // position [0..1] sur la barre
    void  ApplySeekVisual(float frac);            // maj visuelle pendant le drag
    void  PlayQueueIndex(int idx);                // saute au i-ème titre de la file
    void  UpdateLockScreenArt(const std::vector<uint8_t>& data);  // pochette → lock screen
    void RepositionWindow();
    void UpdateWindowRegion();
    void UpdateClock();
    void UpdateSystemStats();
    void QuerySystemControls();
    void QueryWifiNetworks();
    void OpenWifiList();            // query + transition vers l'état WifiList (clic Wi-Fi)
    void ConnectWifi(const std::wstring& ssid, const std::wstring& password); // WlanConnect natif
    void SetWifiPassMode(bool on, const std::wstring& ssid = L"");  // saisie mot de passe + focus
    void QueryBluetoothDevices(std::vector<BtDeviceItem>& out);  // énumération PURE (thread de fond)
    void ScanBluetoothAsync();      // lance le scan BT sur un thread + poste le résultat
    void OpenBluetoothList();       // transition vers l'état BluetoothList + scan async
    void ConnectBluetoothDevice(const std::wstring& name);  // connexion in-island best-effort
    void CheckDismiss();
    void OnAnimTick();
    void OnPaint();
    void OnMouseMove(int x, int y);
    void OnMouseDown(int x, int y);
    void OnMouseUp  (int x, int y);
    void OnMouseLeave();
    void OnMouseWheel(short delta);
    void OnLongPress();
    void ShowAudioDevicesMenu();
    void OnGesturePan(LONG screenX, DWORD flags);
    void SwitchMenu(int newMenu);
    void TransitionToNotifList();
    void ToggleWifi();
    void ToggleBluetooth();
    void ToggleDarkMode();
    void ToggleNightLight();
    void SetSystemVolume(float v);
    void SetSystemBrightness(float v);

    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
};
