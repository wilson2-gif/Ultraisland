#pragma once
#include <Windows.h>
#include <windowsx.h>
#include <memory>
#include <functional>
#include <map>
#include <algorithm>
#include "../Graphics/Renderer.h"
#include "../Graphics/AnimationEngine.h"
#include "TrayIcon.h"

class SystemMonitor;
class MediaManager;

// ─────────────────────────────────────────────────────────────────────────────
//  WindowManager  v7  — Ultraisland Liquid
// ─────────────────────────────────────────────────────────────────────────────
class WindowManager
{
public:
    WindowManager();
    ~WindowManager();

    void Initialize(HINSTANCE hInstance, int nCmdShow,
                    SystemMonitor* sysMonitor = nullptr);

    void SetContent         (const IslandContent& c);
    void SetMediaManager    (MediaManager* mm) { m_mediaManager = mm; }
    void TriggerMusic       (const std::wstring& title, const std::wstring& artist,
                             const std::wstring& sourceApp = {},
                             bool playing = false, float progress = 0.f,
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

    std::unique_ptr<Renderer>        m_renderer;
    std::unique_ptr<AnimationEngine> m_animation;
    std::unique_ptr<TrayIcon>        m_tray;
    SystemMonitor*                   m_sysMonitor = nullptr;

    IslandContent m_content;
    int m_screenW = 0, m_screenH = 0;
    int m_currentW = 0, m_currentH = 0;

    bool  m_hoverExpanded  = false;
    bool  m_mouseTracking  = false;

    MediaManager* m_mediaManager = nullptr;
    int  m_activeMenu = 1;
    bool m_showQueue = false;
    std::vector<MusicQueueItem> m_recentTracks;
    DWORD m_trackStartTick = 0;

    // Notification history
    std::vector<NotifHistoryItem> m_notifHistory;

    // ── App Badges ──────────────────────────────────────────────────────────
    // clé = displayName de l'app (ex: "WhatsApp"), valeur = AppBadge complet
    std::map<std::wstring, AppBadge> m_appBadges;
    float m_badgePulseT = 0.f;   // animation 0→1 quand nouveaux badges

    // System controls state
    bool  m_wifiEnabled    = false;
    std::wstring m_wifiSSID;
    bool  m_btEnabled      = false;
    bool  m_darkMode       = false;
    bool  m_nightLight     = false;
    float m_sysVolume      = 0.5f;
    float m_sysBrightness  = 0.5f;

    // Notif : état à restaurer
    IslandState m_preNotifState  = IslandState::Idle;
    DWORD       m_notifDismissAt = 0;

    // ── Slide animation entre onglets ───────────────────────────────────────
    float m_tabSlideT        = 1.f;   // 0=début, 1=terminé
    DWORD m_tabSlideStart    = 0;
    DWORD m_tabSlideDuration = 220;   // ms
    float m_tabSlideDir      = 0.f;   // +1 = contenu vient de droite, -1 = gauche
    int   m_tabPrevMenu      = 1;

    // ── Drag des sliders (volume / luminosité) temps réel ──────────────────
    bool  m_dragSlider    = false;
    int   m_dragSliderIdx = 0;    // 0=volume, 1=luminosité

    // ── Détection appui long (Wi-Fi liste) ─────────────────────────────────
    DWORD m_mouseDownTime  = 0;
    int   m_mouseDownX     = 0;
    int   m_mouseDownY     = 0;
    bool  m_mouseDownOnWifi= false;
    bool  m_longPressArmed = false;

    // ── Geste deux doigts (swipe tab) ──────────────────────────────────────
    bool  m_gestureActive  = false;
    LONG  m_gesturePrevX   = 0;
    float m_gestureAccumX  = 0.f;   // pixels accumulés sur l'axe X

    // ── Wi-Fi réseaux (WifiList) ────────────────────────────────────────────
    std::vector<WifiNetworkItem> m_wifiNetworks;

    // ── Timers ──────────────────────────────────────────────────────────────
    static constexpr int   TOP_MARGIN      = 0;
    static constexpr UINT  TIMER_ANIM      = 1;
    static constexpr UINT  TIMER_CLOCK     = 2;
    static constexpr UINT  TIMER_CHECK     = 3;
    static constexpr UINT  TIMER_LONGPRESS = 4;
    static constexpr DWORD NOTIF_MS        = 3000;
    static constexpr DWORD HUD_MS          = 3000;
    static constexpr float SWIPE_THRESH    = 72.f;  // px pour changer onglet

    void RepositionWindow();
    void UpdateWindowRegion();          // SetWindowRgn pour bloquer côtés
    void UpdateClock();
    void UpdateSystemStats();
    void QuerySystemControls();
    void QueryWifiNetworks();           // scan réseaux disponibles
    void CheckDismiss();
    void OnAnimTick();
    void OnPaint();
    void OnMouseMove(int x, int y);
    void OnMouseDown(int x, int y);
    void OnMouseUp  (int x, int y);
    void OnMouseLeave();
    void OnLongPress();                 // appui long → liste Wi-Fi
    void ShowAudioDevicesMenu();

    // Geste touchpad (WM_GESTURE)
    void OnGesturePan(LONG screenX, DWORD flags);

    // Changement d'onglet avec animation slide
    void SwitchMenu(int newMenu);

    // Calcul dynamique hauteur NotifList
    void TransitionToNotifList();

    // System controls actions
    void ToggleWifi();
    void ToggleBluetooth();
    void ToggleDarkMode();
    void ToggleNightLight();
    void SetSystemVolume(float v);
    void SetSystemBrightness(float v);

    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
};
