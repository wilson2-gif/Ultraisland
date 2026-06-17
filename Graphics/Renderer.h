#pragma once
#include <Windows.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <dwrite_3.h>
#include <string>
#include <wincodec.h>
#pragma comment(lib,"d2d1.lib")
#pragma comment(lib,"dwrite.lib")
#pragma comment(lib,"windowscodecs.lib")
#include <vector>
#include "../Core/IslandDim.h"

// ─────────────────────────────────────────────────────────────────────────────
//  AppBadge — badge non-lu par application (affiché sur le pill idle)
// ─────────────────────────────────────────────────────────────────────────────
struct AppBadge {
    std::wstring appName;
    D2D1_COLOR_F color    = {0.55f, 0.55f, 0.58f, 1.f};
    int          count    = 0;
    std::wstring iconGlyph= L"\uEA8F";  // Segoe MDL2 Assets
};

// ─────────────────────────────────────────────────────────────────────────────
struct MusicQueueItem {
    std::wstring title;
    std::wstring artist;
};

// ─────────────────────────────────────────────────────────────────────────────
struct NotifHistoryItem {
    std::wstring appName;
    std::wstring message;
    std::wstring timeStr;
    std::wstring iconGlyph  = L"\uEA8F";
    D2D1_COLOR_F appColor   = {0.55f, 0.55f, 0.58f, 1.f};
};

// ─────────────────────────────────────────────────────────────────────────────
struct WifiNetworkItem {
    std::wstring ssid;
    bool         connected   = false;
    int          signal      = 0;   // 0..100
};

// ─────────────────────────────────────────────────────────────────────────────
struct IslandContent {
    // Taille pill courante (mise à jour par WindowManager à chaque frame)
    float pillW = Pill::W_IDLE;
    float pillH = Pill::H_IDLE;
    float cornerRadius = Pill::CR_IDLE;
    float squashFactor = 1.0f;
    float wobbleAmount = 0.0f;
    float wobblePhase  = 0.0f;

    // ── Slide animation entre onglets ───────────────────────────────────────
    float tabSlideX    = 0.f;   // décalage horizontal contenu (px) — slide

    std::wstring musicTitle, musicArtist, musicSourceApp;
    bool  isMusicPlaying  = false;
    float musicProgress   = 0.f;
    float musicCurrentSec = 0.f;
    float musicTotalSec   = 210.f;
    float musicThumbnailProgress = 0.f;
    ID2D1Bitmap* albumArtBitmap = nullptr;

    // Pistes à venir (À venir — queue)
    std::vector<MusicQueueItem> upcomingTracks;
    // Pistes récentes (fallback si upcomingTracks vide)
    std::vector<MusicQueueItem> queueItems;

    // Notification toast (compact)
    std::wstring notifAppName, notifTitle, notifMessage;
    std::wstring notifIconGlyph = L"\uEA8F";
    D2D1_COLOR_F notifAppColor = {0.5f, 0.5f, 0.5f, 1.f};

    // Notification history list
    std::vector<NotifHistoryItem> notifHistory;

    // ── App Badges (pill idle) ──────────────────────────────────────────────
    // Triés par count décroissant, max 4 affichés
    std::vector<AppBadge> appBadges;
    int totalUnreadCount = 0;        // somme de tous les counts
    float badgePulseT    = 0.f;      // animation apparition (0→1)

    HUDType      hudType  = HUDType::Volume;
    float        hudValue = 0.f;
    std::wstring hudLabel;

    // System stats (gardés pour compat)
    float cpuPercent=0,ramPercent=0,ramUsedGB=0,ramTotalGB=0;
    float netDownKBps=0,netUpKBps=0;
    float batteryPercent=-1; bool batteryCharging=false;

    // System controls panel
    bool  wifiEnabled      = false;
    std::wstring wifiSSID;
    bool  bluetoothEnabled = false;
    bool  darkModeEnabled  = false;
    bool  nightLightEnabled= false;
    float systemVolume     = 0.5f;   // 0..1
    float systemBrightness = 0.5f;   // 0..1

    // Wi-Fi network list (WifiList state)
    std::vector<WifiNetworkItem> wifiNetworks;
    int   wifiConnectingIdx = -1;   // -1 = aucun en cours

    std::wstring clockTime, clockDate;
    bool        isHovered = false;
    IslandState state     = IslandState::Idle;
    float       animT     = 1.f;
    float       globalT   = 0.f;

    // Menu system (0=notif, 1=music, 2=settings)
    int   activeMenuIndex = 1;
    bool  showQueue       = false;
    float bellShakeT      = 0.f;
};

// ─────────────────────────────────────────────────────────────────────────────
class Renderer {
public:
    Renderer(); ~Renderer();
    bool Initialize(HWND hwnd);
    void Resize(UINT w, UINT h);
    void Draw(const IslandContent& c);
    void Release();
    
    void UpdateAlbumArt(const std::vector<uint8_t>& data);
    ID2D1Bitmap* GetAlbumArt() const { return m_albumArt; }

private:
    ID2D1Bitmap* m_albumArt = nullptr;
    IWICImagingFactory* m_wicFactory = nullptr;
    std::vector<uint8_t> m_currentThumbnailData;
    ID2D1Factory1*            m_f   = nullptr;
    ID2D1HwndRenderTarget*    m_rt  = nullptr;
    IDWriteFactory3*          m_dw  = nullptr;
    ID2D1SolidColorBrush*     m_b0  = nullptr;
    ID2D1SolidColorBrush*     m_b1  = nullptr;
    ID2D1LinearGradientBrush* m_bSh = nullptr;
    ID2D1LinearGradientBrush* m_bSd = nullptr;
    ID2D1RadialGradientBrush* m_bGl = nullptr;
    ID2D1LinearGradientBrush* m_bGlass = nullptr;
    ID2D1LinearGradientBrush* m_bBorder = nullptr;  // bordure réflexion glass

    IDWriteTextFormat *m_fT=nullptr, *m_fS=nullptr, *m_fX=nullptr;
    IDWriteTextFormat *m_fC=nullptr, *m_fH=nullptr;
    IDWriteTextFormat *m_fI14=nullptr, *m_fI20=nullptr;
    IDWriteTextFormat *m_fTBig=nullptr;
    IDWriteTextFormat *m_fTSemi=nullptr;

    ID2D1PathGeometry* m_pillGeometry = nullptr;

    // ── Draw helpers ─────────────────────────────────────────────────────────
    void DrawLiquidPill    (float px, float pw, float ph, const IslandContent& c);
    void DrawGlassZone     (float px, float pw, float yStart, float yEnd, float cr, float alpha);
    void DrawIdle          (float px, float pw, float ph, const IslandContent& c);
    void DrawMusicCompact  (float px, float pw, float ph, const IslandContent& c);
    void DrawMusicFull     (float px, float pw, float ph, const IslandContent& c);
    void DrawMusicQueue    (float px, float pw, float ph, const IslandContent& c);
    void DrawNotif         (float px, float pw, float ph, const IslandContent& c);
    void DrawNotifList     (float px, float pw, float ph, const IslandContent& c);
    void DrawHUD           (float px, float pw, float ph, const IslandContent& c);
    void DrawSystem        (float px, float pw, float ph, const IslandContent& c);
    void DrawWifiList      (float px, float pw, float ph, const IslandContent& c);

    void DrawTopIcons      (float px, float pw, float alpha, int activeMenu, float bellShakeT, float slideX = 0.f);
    void DrawAlbumArt      (float x, float y, float s, float alpha, ID2D1Bitmap* bmp = nullptr);
    void DrawWaveform      (float cx, float cy, float maxH,
                            bool playing, float t, D2D1_COLOR_F col, float alpha);
    void DrawPBar          (float x, float y, float w, float prog,
                            float cur, float tot, float alpha);
    void DrawControls      (float px, float pw, float cy, bool playing, float alpha,
                            D2D1_COLOR_F iconColor);
    void DrawArc           (float cx, float cy, float r, float val,
                            D2D1_COLOR_F tk, D2D1_COLOR_F fl, float sw);
    void DrawBar           (float x, float y, float w, float h,
                            float val, D2D1_COLOR_F col);
    void DrawStat          (float x, float y, float w, const wchar_t* lbl,
                            float val, D2D1_COLOR_F col, const wchar_t* unit);
    void DrawBat           (float x, float y, float pct, bool ch);
    void DrawNotifBadge    (float cx, float cy, float radius, D2D1_COLOR_F color,
                            const std::wstring& glyph, float alpha);
    void DrawAppBadges     (float px, float pw, float ph, const IslandContent& c);
    // System controls helpers
    void DrawTogglePill    (float x, float y, float w, float h,
                            const wchar_t* icon, const wchar_t* label,
                            const wchar_t* sub, bool active, float alpha);
    void DrawSlider        (float x, float y, float w, float val,
                            const wchar_t* icon, float alpha);
    void DrawSignalBars    (float cx, float cy, int quality, float alpha);
    void Txt               (const std::wstring& t, IDWriteTextFormat* f,
                            D2D1_RECT_F r, D2D1_COLOR_F col, float op,
                            DWRITE_TEXT_ALIGNMENT al=DWRITE_TEXT_ALIGNMENT_LEADING);
    void SetB0(D2D1_COLOR_F c, float a=1.f);
    void SetB1(D2D1_COLOR_F c, float a=1.f);

    // Clip + slide helper
    void PushSlideClip(float px, float pw, float ph, float slideX);
    void PopSlideClip(float slideX);

    bool CreateDevRes(); void DropDevRes(); void RebuildGrads();
};
