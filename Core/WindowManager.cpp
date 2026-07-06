#include "WindowManager.h"
#include "AppConfig.h"
#include "Sounds.h"
#include "../Modules/SystemMonitor.h"
#include "../Modules/MediaManager.h"
#include <dbt.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <propsys.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wlanapi.h>
#include <BluetoothAPIs.h>
#include <physicalmonitorenumerationapi.h>
#include <highlevelmonitorconfigurationapi.h>
#include <winreg.h>
#include <shellapi.h>
#include <stdexcept>
#include <thread>
#include <string>
// WinRT pour vrai toggle Bluetooth
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Radios.h>
// WinRT pour énumérer les appareils Bluetooth appairés (liste + statut connecté)
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Bluetooth.h>
// WinRT pour la pochette sur l'écran de verrouillage (requiert identité MSIX)
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.UserProfile.h>
// GDI+ : composition de l'image de verrouillage (pochette NETTE centrée)
// NB : gdiplus.h exige min/max, incompatibles avec NOMINMAX → alias std::
#include <objidl.h>
#include <algorithm>
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>
#include <shlwapi.h>       // SHCreateMemStream
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")
// WMI direct pour luminosité laptop (plus aucun cmd PowerShell)
#include <Wbemidl.h>
#include <comdef.h>
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib,"dwmapi.lib")
#pragma comment(lib,"winmm.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"propsys.lib")
#pragma comment(lib,"wlanapi.lib")
#pragma comment(lib,"Bthprops.lib")
#pragma comment(lib,"Dxva2.lib")

// ── IPolicyConfig (interface COM non documentée) ─────────────────────────────
MIDL_INTERFACE("F8679F50-850A-41CF-9C72-430F290290C8")
IPolicyConfig : public IUnknown {
public:
    virtual HRESULT __stdcall GetMixFormat(PCWSTR,WAVEFORMATEX**)=0;
    virtual HRESULT __stdcall GetDeviceFormat(PCWSTR,BOOL,WAVEFORMATEX**)=0;
    virtual HRESULT __stdcall ResetDeviceFormat(PCWSTR)=0;
    virtual HRESULT __stdcall SetDeviceFormat(PCWSTR,WAVEFORMATEX*,WAVEFORMATEX*)=0;
    virtual HRESULT __stdcall GetProcessingPeriod(PCWSTR,BOOL,PINT64,PINT64)=0;
    virtual HRESULT __stdcall SetProcessingPeriod(PCWSTR,PINT64)=0;
    virtual HRESULT __stdcall GetShareMode(PCWSTR,void*)=0;
    virtual HRESULT __stdcall SetShareMode(PCWSTR,void*)=0;
    virtual HRESULT __stdcall GetPropertyValue(PCWSTR,const PROPERTYKEY&,PROPVARIANT*)=0;
    virtual HRESULT __stdcall SetPropertyValue(PCWSTR,const PROPERTYKEY&,PROPVARIANT*)=0;
    virtual HRESULT __stdcall SetDefaultEndpoint(PCWSTR wszDeviceId,ERole eRole)=0;
    virtual HRESULT __stdcall SetEndpointVisibility(PCWSTR,BOOL)=0;
};
static const CLSID CLSID_PolicyConfig =
    {0x870AF99C,0x171D,0x4F9E,{0xAF,0x0D,0xE6,0x3D,0xF4,0x0C,0x2B,0xC9}};

// ── Ease cubique locale ───────────────────────────────────────────────────────
static float LocalCubicEase(float t){ float u=1-t; return 1-u*u*u; }

// Forward decl — défini plus bas, utilisé par TriggerMusic pour le nom convivial
static void ResolveAppStyle(const std::wstring&, D2D1_COLOR_F&, std::wstring&, std::wstring&);

// ─────────────────────────────────────────────────────────────────────────────
//  Constructeur / Destructeur
// ─────────────────────────────────────────────────────────────────────────────
WindowManager::WindowManager()
    : m_renderer (std::make_unique<Renderer>())
    , m_animation(std::make_unique<AnimationEngine>())
    , m_tray     (std::make_unique<TrayIcon>())
{
    m_content.state=IslandState::Idle;
    m_content.animT=1.f;
    m_content.isMusicPlaying=false;
    m_notifDismissAt = 0;
}

WindowManager::~WindowManager()
{
    if(m_hwnd){
        UnregisterHotKey(m_hwnd, 1);
        KillTimer(m_hwnd,TIMER_ANIM);
        KillTimer(m_hwnd,TIMER_CLOCK);
        KillTimer(m_hwnd,TIMER_CHECK);
        KillTimer(m_hwnd,TIMER_LONGPRESS);
        m_tray->Remove();
        m_renderer->Release();
        DestroyWindow(m_hwnd);
        timeEndPeriod(1);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  API Thread-safe — Post* (appelables depuis n'importe quel thread)
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::PostMusicUpdate(const std::wstring& title,
                                     const std::wstring& artist,
                                     const std::wstring& sourceApp,
                                     bool playing, float progress, float durationSec,
                                     const std::vector<uint8_t>& thumbnailData)
{
    MusicUpdateData data;
    data.title         = title;
    data.artist        = artist;
    data.sourceApp     = sourceApp;
    data.playing       = playing;
    data.progress      = progress;
    data.durationSec   = durationSec;
    data.thumbnailData = thumbnailData;

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_updateQueue.push(std::move(data));
    }
    if(m_hwnd) PostMessage(m_hwnd, WM_ISLAND_UPDATE, 0, 0);
}

void WindowManager::PostNotificationUpdate(const std::wstring& appName,
                                            const std::wstring& title,
                                            const std::wstring& msg)
{
    NotifUpdateData data;
    data.appName = appName;
    data.title   = title;
    data.msg     = msg;

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_updateQueue.push(std::move(data));
    }
    if(m_hwnd) PostMessage(m_hwnd, WM_ISLAND_UPDATE, 0, 0);
}

void WindowManager::PostCollapseUpdate()
{
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_updateQueue.push(CollapseUpdateData{});
    }
    if(m_hwnd) PostMessage(m_hwnd, WM_ISLAND_UPDATE, 0, 0);
}

void WindowManager::PostSpotifyQueue(const std::vector<SpotifyTrack>& tracks)
{
    SpotifyQueueData d; d.tracks = tracks;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_updateQueue.push(std::move(d));
    }
    if(m_hwnd) PostMessage(m_hwnd, WM_ISLAND_UPDATE, 0, 0);
}

void WindowManager::PostAmbientOpen()
{
    // Thread-safe : poste un message vers le thread UI
    if(m_hwnd) PostMessage(m_hwnd, WM_ISLAND_AMBIENT, 0, 0);
}

bool WindowManager::ConnectSpotify()
{
    // client_id public — à remplacer par le tien sur developer.spotify.com
    // (Redirect URI EXACTE : http://127.0.0.1:53682/callback)
    static const char* SPOTIFY_CLIENT_ID = "1ba0cbdfc7c64403bb7e1b2a4358f69c";

    std::string id = SPOTIFY_CLIENT_ID;
    if (id.empty() || id.rfind("REPLACE", 0) == 0) {
        // Pas configuré → message clair, on N'OUVRE PAS le navigateur (évite
        // la page "invalid client" + l'ancien gel/crash).
        PostNotificationUpdate(L"spotify", L"Spotify",
            L"Configurez d'abord votre Client ID Spotify");
        return false;
    }

    m_spotify.Configure(SPOTIFY_CLIENT_ID, 53682);
    m_spotify.SetQueueCallback([this](const std::vector<SpotifyTrack>& tracks){
        PostSpotifyQueue(tracks);
    });

    // Suivi « pro » : feedback immédiat, puis résultat une fois l'autorisation reçue.
    PostNotificationUpdate(L"spotify", L"Spotify",
                           L"Autorisez UltraIsland dans le navigateur…");

    // ── OAuth sur un thread de fond : ne bloque JAMAIS l'UI (plus de gel/crash) ──
    std::thread([this]() {
        bool ok = m_spotify.Connect();
        if (ok) {
            m_spotify.StartPolling(5);   // 1er fetch immédiat → file visible en ~2 s
            PostNotificationUpdate(L"spotify", L"Spotify",
                                   L"Connecté ✓ — chargement de la file…");
        } else {
            PostNotificationUpdate(L"spotify", L"Spotify",
                                   L"Autorisation non reçue (annulé ou expiré)");
        }
    }).detach();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrainUpdateQueue — appelé UNIQUEMENT depuis le thread UI (WindowProc)
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::DrainUpdateQueue()
{
    std::queue<IslandUpdateVariant> local;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        std::swap(local, m_updateQueue);
    }

    while(!local.empty()){
        auto item = std::move(local.front());
        local.pop();

        std::visit([this](auto&& arg){
            using T = std::decay_t<decltype(arg)>;

            if constexpr(std::is_same_v<T, MusicUpdateData>){
                TriggerMusic(arg.title, arg.artist, arg.sourceApp,
                             arg.playing, arg.progress, arg.durationSec, arg.thumbnailData);
            }
            else if constexpr(std::is_same_v<T, NotifUpdateData>){
                TriggerNotification(arg.appName, arg.title, arg.msg);
            }
            else if constexpr(std::is_same_v<T, CollapseUpdateData>){
                Collapse();
            }
            else if constexpr(std::is_same_v<T, SpotifyQueueData>){
                m_spotifyQueue.clear();
                for (const auto& t : arg.tracks) {
                    MusicQueueItem mi;
                    mi.title  = t.title;
                    mi.artist = t.artist;
                    m_spotifyQueue.push_back(std::move(mi));
                }
                m_content.upcomingTracks = m_spotifyQueue;
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
        }, item);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Initialize
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::Initialize(HINSTANCE hi,int nCmdShow,SystemMonitor* sys,
                               const AppConfig* cfg)
{
    m_hInstance=hi; m_sysMonitor=sys; m_appCfg=cfg;
    m_currentW=(int)PillRT::W_IDLE;
    m_currentH=(int)PillRT::H_IDLE;
    m_screenW=GetSystemMetrics(SM_CXSCREEN);
    m_screenH=GetSystemMetrics(SM_CYSCREEN);

    timeBeginPeriod(1);

    WNDCLASSEX wc={};
    wc.cbSize=sizeof(wc); wc.lpfnWndProc=WindowProc;
    wc.hInstance=hi; wc.lpszClassName=L"UltraislandClass";
    wc.hbrBackground=nullptr;
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    RegisterClassEx(&wc);

    m_currentW=m_screenW;

    // WS_EX_NOREDIRECTIONBITMAP : la fenêtre n'a PAS de surface GDI redirigée.
    // Obligatoire pour le chemin DirectComposition + swap-chain alpha per-pixel.
    m_hwnd=CreateWindowEx(
        WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,
        L"UltraislandClass",L"Ultraisland",WS_POPUP,
        0,TOP_MARGIN,m_currentW,m_currentH,
        nullptr,nullptr,hi,this);
    if(!m_hwnd) throw std::runtime_error("CreateWindowEx failed");

    // ── DPI Per-Monitor V2 : lit le DPI de la fenêtre → m_dpiScale.
    // Utilisé par D2D (SetDpi sur le DeviceContext) et pour scaler les hit-tests.
    {
        typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
        UINT dpi = 96;
        if(HMODULE u32 = GetModuleHandleW(L"user32.dll")){
            if(auto fn = (GetDpiForWindowFn)GetProcAddress(u32, "GetDpiForWindow"))
                dpi = fn(m_hwnd);
        }
        m_dpiScale = dpi / 96.f;
    }

    // Fenêtre en PIXELS PHYSIQUES (Per-Monitor V2) : on retaille la fenêtre créée
    // avec des dimensions logiques × scale — le D2D reste en coords logiques via
    // SetDpi ci-dessous → net à 125/150 %.
    m_currentW = (int)(m_screenW);   // m_screenW est déjà en pixels physiques ici
    m_currentH = (int)(PillRT::H_IDLE * m_dpiScale);
    SetWindowPos(m_hwnd, nullptr, 0, TOP_MARGIN, m_currentW, m_currentH,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    // Avec DComp : plus besoin de DwmExtendFrameIntoClientArea (hack alpha de
    // l'ancien chemin HwndRenderTarget). La composition fournit l'alpha directement.
    DWMNCRENDERINGPOLICY pol=DWMNCRP_DISABLED;
    DwmSetWindowAttribute(m_hwnd,DWMWA_NCRENDERING_POLICY,&pol,sizeof(pol));

    if(!m_renderer->Initialize(m_hwnd))
        throw std::runtime_error("Renderer::Initialize failed");
    // D2D en unités LOGIQUES → tout le code de dessin reste inchangé, mais
    // s'affiche NET à n'importe quel DPI.
    m_renderer->SetDpi(m_dpiScale * 96.f);

    // Screen width en unités LOGIQUES (le code interne raisonne en logiques)
    m_animation->SetScreenWidth((int)(m_screenW / m_dpiScale));

    // ── Activer les gestes touchpad (WM_GESTURE) ─────────────────────────────
    GESTURECONFIG gc[]={
        {GID_PAN,          GC_PAN, 0},
        {GID_ZOOM,         0,      GC_ZOOM},
        {GID_ROTATE,       0,      GC_ROTATE},
        {GID_TWOFINGERTAP, 0,      GC_TWOFINGERTAP},
        {GID_PRESSANDTAP,  0,      GC_PRESSANDTAP},
    };
    SetGestureConfig(m_hwnd, 0, (UINT)ARRAYSIZE(gc), gc, sizeof(GESTURECONFIG));

    m_animation->SetFrameCallback([this](){
        m_content.state=m_animation->GetDisplayState();
        m_content.animT=m_animation->GetT();

        UINT nw,nh; m_animation->GetCurrentSize(nw,nh);
        float pw,ph; m_animation->GetCurrentPillSize(pw,ph);
        m_content.pillW=pw; m_content.pillH=ph;
        m_content.cornerRadius=m_animation->GetCurrentCornerRadius();
        m_content.squashFactor=m_animation->GetSquashFactor();
        m_content.wobbleAmount=m_animation->GetWobbleAmount();
        m_content.wobblePhase =m_animation->GetWobblePhase();

        // Convertit les tailles LOGIQUES → PIXELS PHYSIQUES pour la fenêtre.
        UINT nwPx = (UINT)(nw * m_dpiScale);
        UINT nhPx = (UINT)(nh * m_dpiScale);
        if((int)nwPx!=m_currentW||(int)nhPx!=m_currentH){
            m_currentW=(int)nwPx; m_currentH=(int)nhPx;
            RepositionWindow();
            m_renderer->Resize(nwPx, nhPx);   // swapchain reste en physiques…
            m_renderer->SetDpi(m_dpiScale * 96.f);   // …mais le DC reste logique
        } else {
            UpdateWindowRegion();
        }
        InvalidateRect(m_hwnd,nullptr,FALSE);
    });

    m_tray->Install(m_hwnd);
    m_tray->OnQuit=[this](){ DestroyWindow(m_hwnd); };
    m_tray->OnTestMusic=[this](){
        TriggerMusic(L"Espresso",L"Sabrina Carpenter",L"Spotify",true,.42f);
    };
    m_tray->OnTestNotif=[this](){
        TriggerNotification(L"whatsapp",L"Message",L"Tu es l\u00E0 ?");
    };
    m_tray->OnConnectSpotify=[this](){
        // Async : ConnectSpotify poste lui-m\u00EAme le r\u00E9sultat (thread-safe). Ne bloque pas.
        ConnectSpotify();
    };
    m_tray->OnSettings=[this](){ if(m_onOpenSettings) m_onOpenSettings(); };
    m_tray->OnAmbient =[this](){ ToggleAmbient(); };

    // ── Mode ambiant : configuration + raccourci global Ctrl+Alt+L ──────────
    m_ambient.Configure(
        &m_content,
        [this]() -> const std::vector<uint8_t>& {
            static const std::vector<uint8_t> kEmpty;
            return m_renderer ? m_renderer->GetThumbnailData() : kEmpty;
        },
        [this](int action){
            if(!m_mediaManager) return;
            if(action==0) m_mediaManager->SkipPrevious();
            if(action==1){ m_mediaManager->PlayPause();
                           m_content.isMusicPlaying=!m_content.isMusicPlaying; }
            if(action==2) m_mediaManager->SkipNext();
        },
        [this](float progress){
            if(m_mediaManager && m_content.musicTotalSec > 0.f) {
                m_mediaManager->SeekTo(progress * m_content.musicTotalSec);
            }
        },
        [this](){ ShowWindow(m_hwnd, SW_SHOWNOACTIVATE); },   // île restaurée
        // Volume : getter (lit le vrai volume système)
        [this]() -> float {
            QuerySystemControls();
            return m_sysVolume;
        },
        // Volume : setter (applique au système)
        [this](float v){ SetSystemVolume(v); });
    RegisterHotKey(m_hwnd, 1, MOD_CONTROL | MOD_ALT, 'L');
    // TIMER_CHECK va aussi surveiller l'inactivité (voir CheckDismiss)

    SetTimer(m_hwnd, TIMER_ANIM,  16,   nullptr);
    SetTimer(m_hwnd, TIMER_CLOCK, 1000, nullptr);
    SetTimer(m_hwnd, TIMER_CHECK, 250,  nullptr);

    // Initialisation forcée des dimensions (runtime — pilotées par le Cockpit)
    m_content.pillW        = PillRT::W_IDLE;
    m_content.pillH        = PillRT::H_IDLE;
    m_content.cornerRadius = PillRT::CR_IDLE;
    m_content.state        = IslandState::Idle;
    m_content.animT        = 1.0f;

    // Spotify : restaurer la connexion silencieusement (refresh token DPAPI)
    static const char* SPOTIFY_CLIENT_ID = "1ba0cbdfc7c64403bb7e1b2a4358f69c";
    m_spotify.Configure(SPOTIFY_CLIENT_ID, 53682);
    m_spotify.SetQueueCallback([this](const std::vector<SpotifyTrack>& tracks){
        PostSpotifyQueue(tracks);
    });
    // Restauration silencieuse sur thread de fond (le refresh réseau ne doit pas
    // bloquer le démarrage de l'UI).
    std::thread([this](){ if (m_spotify.Restore()) m_spotify.StartPolling(5); }).detach();

    UpdateClock();
    UpdateSystemStats();
    UpdateWindowRegion();

    ShowWindow(m_hwnd,nCmdShow);
    UpdateWindow(m_hwnd);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Seek — clic/drag sur la barre de progression musique
// ─────────────────────────────────────────────────────────────────────────────
float WindowManager::SeekFracFromX(int x) const
{
    float pw = m_content.pillW, px = PillX(ScreenWLogical(), pw);
    return std::clamp(((float)x - (px + 14.f)) / (pw - 28.f), 0.f, 1.f);
}

void WindowManager::ApplySeekVisual(float frac)
{
    m_content.musicProgress   = frac;
    m_content.musicCurrentSec = frac * m_content.musicTotalSec;
    m_musicBaseSec = m_content.musicCurrentSec;   // l'avance continue repart d'ici
    m_musicPosTick = GetTickCount();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

// ─────────────────────────────────────────────────────────────────────────────
//  PlayQueueIndex — saute au i-ème titre de la file (multi-skip SMTC).
//  L'API Spotify n'expose NI « jouer l'élément N » NI le réordonnancement de la
//  file : enchaîner (idx+1) « suivant » est la seule voie (comme les apps tierces).
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::PlayQueueIndex(int idx)
{
    if(!m_mediaManager || idx < 0) return;
    std::thread([this, idx](){
        for(int k = 0; k <= idx; ++k){
            m_mediaManager->SkipNext();
            Sleep(380);   // laisse Spotify enchaîner proprement
        }
        m_spotify.RefreshNow();   // re-fetch la file après le saut
    }).detach();
}

// ─────────────────────────────────────────────────────────────────────────────
//  UpdateLockScreenArt — pochette → fond d'écran de verrouillage.
//  Requiert une IDENTITÉ DE PACKAGE (version MSIX installée) ; lancé depuis
//  l'exe nu, l'appel WinRT échoue → catch silencieux (no-op).
// ─────────────────────────────────────────────────────────────────────────────
// Trouve le CLSID de l'encodeur JPEG GDI+
static bool GdipJpegClsid(CLSID& out)
{
    UINT n = 0, sz = 0;
    Gdiplus::GetImageEncodersSize(&n, &sz);
    if (!sz) return false;
    std::vector<uint8_t> buf(sz);
    auto* codecs = (Gdiplus::ImageCodecInfo*)buf.data();
    Gdiplus::GetImageEncoders(n, sz, codecs);
    for (UINT i = 0; i < n; ++i)
        if (wcscmp(codecs[i].MimeType, L"image/jpeg") == 0) { out = codecs[i].Clsid; return true; }
    return false;
}

void WindowManager::UpdateLockScreenArt(const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> copy = data;
    int scrW = m_screenW, scrH = m_screenH;
    std::thread([copy = std::move(copy), scrW, scrH](){
        try {
            wchar_t base[MAX_PATH];
            if(!GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH)) return;
            std::wstring dir = std::wstring(base) + L"\\Ultraisland";
            CreateDirectoryW(dir.c_str(), nullptr);
            std::wstring file = dir + L"\\lockart.jpg";

            // ── Composition GDI+ à la RÉSOLUTION ÉCRAN (image NETTE) ─────────
            // Fond = pochette étirée plein écran + voile sombre ;
            // centre = grande pochette NETTE (interpolation bicubique HQ).
            bool composed = false;
            {
                ULONG_PTR tok = 0; Gdiplus::GdiplusStartupInput gsi;
                if (Gdiplus::GdiplusStartup(&tok, &gsi, nullptr) == Gdiplus::Ok) {
                    IStream* stm = SHCreateMemStream(copy.data(), (UINT)copy.size());
                    if (stm) {
                        Gdiplus::Bitmap art(stm);
                        if (art.GetLastStatus() == Gdiplus::Ok && art.GetWidth() > 0) {
                            int W = scrW > 0 ? scrW : 1920, H = scrH > 0 ? scrH : 1080;
                            Gdiplus::Bitmap canvas(W, H, PixelFormat24bppRGB);
                            Gdiplus::Graphics g(&canvas);
                            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

                            // Fond : cover-fill (l'upscale massif fait office de flou doux)
                            float aw = (float)art.GetWidth(), ah = (float)art.GetHeight();
                            float s  = std::max(W/aw, H/ah) * 1.08f;
                            g.DrawImage(&art, (W - aw*s)*.5f, (H - ah*s)*.5f, aw*s, ah*s);
                            Gdiplus::SolidBrush veil(Gdiplus::Color(120, 8, 8, 12));
                            g.FillRectangle(&veil, 0, 0, W, H);

                            // Pochette nette + descendue (~10 cm sous l'encoche
                            // Windows) → centre vertical passe à 62 % de H,
                            // taille réduite pour équilibrer l'horloge au-dessus.
                            float side = H * 0.44f;
                            float x = (W - side)*.5f, y = H * 0.62f - side*.5f, r = side*0.06f;
                            Gdiplus::GraphicsPath rr;
                            rr.AddArc(x, y, r*2, r*2, 180, 90);
                            rr.AddArc(x+side-r*2, y, r*2, r*2, 270, 90);
                            rr.AddArc(x+side-r*2, y+side-r*2, r*2, r*2, 0, 90);
                            rr.AddArc(x, y+side-r*2, r*2, r*2, 90, 90);
                            rr.CloseFigure();
                            g.SetClip(&rr);
                            g.DrawImage(&art, x, y, side, side);
                            g.ResetClip();
                            Gdiplus::Pen edge(Gdiplus::Color(70, 255, 255, 255), 2.f);
                            g.DrawPath(&edge, &rr);

                            CLSID jpeg;
                            if (GdipJpegClsid(jpeg) &&
                                canvas.Save(file.c_str(), &jpeg, nullptr) == Gdiplus::Ok)
                                composed = true;
                        }
                        stm->Release();
                    }
                    Gdiplus::GdiplusShutdown(tok);
                }
            }
            if (!composed) {
                // Repli : pochette brute (mieux que rien)
                HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr,
                                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if(h == INVALID_HANDLE_VALUE) return;
                DWORD wr; WriteFile(h, copy.data(), (DWORD)copy.size(), &wr, nullptr);
                CloseHandle(h);
            }

            auto sf = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(
                          winrt::hstring(file)).get();
            winrt::Windows::System::UserProfile::LockScreen::SetImageFileAsync(sf).get();
        } catch (...) { /* exe non packagé ou accès refusé : no-op */ }
    }).detach();
}

// ─────────────────────────────────────────────────────────────────────────────
//  ToggleAmbient — mode verrouillage/ambiant plein écran
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::ToggleAmbient()
{
    if (m_ambient.IsOpen()) {
        m_ambient.Close();                    // onClosed restaure l'île
    } else {
        ShowWindow(m_hwnd, SW_HIDE);          // l'île se masque pendant l'ambiant
        m_ambient.Open(m_hInstance);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  ReloadConfig — « Appliquer » du Cockpit (WM_ISLAND_RELOADCFG)
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::ReloadConfig()
{
    if(m_appCfg) m_appCfg->ApplyRuntime();
    if(m_appCfg && !m_appCfg->islandEnabled){ ShowWindow(m_hwnd, SW_HIDE); return; }
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);

    // USE-AFTER-FREE : m_renderer->Release() libère m_albumArt (le bitmap pochette),
    // mais m_content.albumArtBitmap détient encore l'ANCIEN pointeur. Entre Release()
    // et Initialize(), tout WM_PAINT (RepositionWindow/InvalidateRect, timers) ferait
    // DrawAlbumArt sur un bitmap libéré (lié à un device détruit) → crash au clic
    // « Appliquer ». On invalide donc la référence dupliquée AVANT le Release.
    m_content.albumArtBitmap = nullptr;

    // Force le rebuild des polices/pinceaux → prise en compte immédiate de la
    // typographie (Phase B). D2DERR_RECREATE_TARGET les recrée au prochain Draw.
    if(m_renderer) m_renderer->Release();

    // Retaille immédiatement vers l'idle runtime (Custom force même si déjà Idle)
    m_hoverExpanded = false;
    m_animation->StartTransitionCustom(IslandState::Idle,
        PillRT::W_IDLE, PillRT::H_IDLE, PillRT::CR_IDLE, 240);
    RepositionWindow();
    // Réinit renderer avec la nouvelle typo
    if(m_renderer){
        m_renderer->Initialize(m_hwnd);
        // Initialize recrée un renderer à 96 DPI → ré-appliquer le scale courant,
        // sinon la pilule s'affiche minuscule après « Appliquer » à 125/150 %.
        m_renderer->SetDpi(m_dpiScale * 96.f);
        // Reconstruire la pochette sur le NOUVEAU device (la donnée thumbnail CPU
        // survit au Release) et re-publier le pointeur bitmap valide.
        m_renderer->UpdateAlbumArt(m_renderer->GetThumbnailData());
        m_content.albumArtBitmap = m_renderer->GetAlbumArt();
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::RepositionWindow()
{
    SetWindowPos(m_hwnd,HWND_TOPMOST,
                 0,TOP_MARGIN,m_currentW,m_currentH,
                 SWP_NOACTIVATE|SWP_NOZORDER);
    UpdateWindowRegion();
}

void WindowManager::UpdateWindowRegion()
{
    if(!m_hwnd||m_currentW<=0||m_currentH<=0) return;
    float pw=m_content.pillW;
    // SetWindowRgn restreint la zone d'interception souris à la pilule + 2 px.
    // Avec DComp les bords visuels sont anti-aliasés par la géométrie D2D, mais
    // on garde ce clip pour que WM_NCHITTEST de la fenêtre ne capture pas tout.
    const int MARGIN=2;
    // Fenêtre + SetWindowRgn en PIXELS PHYSIQUES → convertir pw (logique) × scale.
    float pwPx    = pw * m_dpiScale;
    float logRight= m_currentW / m_dpiScale;
    int pLeft  = (int)((PillX(logRight, pw) - MARGIN) * m_dpiScale);
    int pRight = pLeft + (int)pwPx + (int)(MARGIN*2*m_dpiScale);
    if(pLeft<0)          pLeft=0;
    if(pRight>m_currentW) pRight=m_currentW;

    HRGN rgn=CreateRectRgn(pLeft, 0, pRight, m_currentH);
    if(rgn) SetWindowRgn(m_hwnd, rgn, TRUE);
}

// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::UpdateClock()
{
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t tb[10]; swprintf_s(tb,L"%02d:%02d",st.wHour,st.wMinute);
    m_content.clockTime=tb;
    InvalidateRect(m_hwnd,nullptr,FALSE);
}

// P0-FIX : UpdateSystemStats appelé uniquement via TIMER_CHECK (250ms)
// et non plus à chaque frame TIMER_ANIM (16ms) — évite les appels COM/Registry à 60fps
void WindowManager::UpdateSystemStats()
{
    if(!m_sysMonitor) return;
    SystemStats s=m_sysMonitor->GetStats();
    m_content.cpuPercent      = s.cpuPercent;
    m_content.ramPercent      = s.ramPercent;
    m_content.ramUsedGB       = s.ramUsedGB;
    m_content.ramTotalGB      = s.ramTotalGB;
    m_content.netDownKBps     = s.netDownKBps;
    m_content.netUpKBps       = s.netUpKBps;
    m_content.batteryPercent  = s.batteryPercent;
    m_content.batteryCharging = s.batteryCharging;
}

void WindowManager::CheckDismiss()
{
    // P0-FIX : UpdateSystemStats déplacé ici (TIMER_CHECK = 250ms), retiré de OnAnimTick
    UpdateSystemStats();

    // Filet de sécurité média (~2 s) : les events SMTC se perdent parfois en fin
    // de piste → resync forcée pour ne JAMAIS rester figé sur l'ancien titre.
    if(m_mediaManager && (++m_refreshDiv & 7) == 0)
        m_mediaManager->RequestRefresh();

    // Rafraîchissement LIVE des panneaux Wi-Fi / Bluetooth (~2 s) : le scan Wi-Fi
    // prend quelques secondes → sans ça la liste reste vide au premier affichage,
    // et le statut « connecté » n'apparaît qu'après re-scan.
    if((m_refreshDiv & 7) == 0){
        IslandState ls = m_animation->GetDisplayState();
        if(ls==IslandState::WifiList && !m_wifiPassMode){
            QueryWifiNetworks();
            m_content.wifiNetworks = m_wifiNetworks;
            if(m_content.wifiConnectingIdx>=0)
                for(const auto& n:m_wifiNetworks) if(n.connected){ m_content.wifiConnectingIdx=-1; break; }
            InvalidateRect(m_hwnd,nullptr,FALSE);
        } else if(ls==IslandState::BluetoothList && m_btEnabled){
            QueryBluetoothDevices();
            m_content.btDevices = m_btDevices;
            InvalidateRect(m_hwnd,nullptr,FALSE);
        }
    }

    // Ambient++ : ouverture auto après N min d'inactivité si musique en cours
    if(m_appCfg && m_appCfg->ambientIdleMin > 0 && m_content.isMusicPlaying
       && !m_ambient.IsOpen()){
        LASTINPUTINFO li = { sizeof(li) };
        if(GetLastInputInfo(&li)){
            DWORD idleMs = GetTickCount() - li.dwTime;
            if(idleMs >= (DWORD)m_appCfg->ambientIdleMin * 60000u)
                ToggleAmbient();   // ouvre l'ambiant automatiquement
        }
    }

    // Filet ULTIME : quel que soit l'état, si `m_notifDismissAt` est planifié
    // et le délai atteint → on force la fermeture. La règle "hover conserve" ne
    // s'applique que 500 ms après l'expiration (petite tolérance ergonomique).
    if(!m_notifDismissAt) return;
    DWORD now = GetTickCount();
    if(now < m_notifDismissAt) return;

    IslandState ds = m_animation->GetDisplayState();
    bool isNotif = (ds == IslandState::NotifExpanded ||
                    ds == IslandState::CollapsingNotif);
    // Notif : dismiss TOUJOURS. HUD/System : conserver au hover 500 ms, puis force.
    bool graceOver = (now - m_notifDismissAt) > 500;
    if(!isNotif && m_hoverExpanded && !graceOver) return;

    m_notifDismissAt=0;
    if(m_preNotifState==IslandState::MusicExpanded)
        m_animation->StartTransition(IslandState::MusicExpanded,280);
    else
        m_animation->StartTransition(IslandState::Idle,280);
}

void WindowManager::SetContent(const IslandContent& c){m_content=c;}

// ─────────────────────────────────────────────────────────────────────────────
//  TriggerMusic
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::TriggerMusic(const std::wstring& ti,const std::wstring& ar,
                                  const std::wstring& srcApp,bool playing,float prog,
                                  float durationSec,
                                  const std::vector<uint8_t>& thumbnailData)
{
    m_preNotifState = IslandState::Idle;
    if(!ti.empty()&&m_content.musicTitle!=ti&&!m_content.musicTitle.empty()){
        DWORD elapsed=m_trackStartTick?(GetTickCount()-m_trackStartTick):0;
        if(elapsed>=20000){
            if(m_recentTracks.empty()||m_recentTracks.front().title!=m_content.musicTitle){
                MusicQueueItem item={m_content.musicTitle,m_content.musicArtist};
                m_recentTracks.insert(m_recentTracks.begin(),item);
                if(m_recentTracks.size()>5) m_recentTracks.pop_back();
                m_content.queueItems=m_recentTracks;
            }
        }
        m_trackStartTick=GetTickCount();
    } else if(m_trackStartTick==0&&!ti.empty()){
        m_trackStartTick=GetTickCount();
    }

    m_content.musicTitle     = ti;
    m_content.musicArtist    = ar;
    // Nom convivial : "SpotifyAB.SpotifyMusic_zpdnekdr" → "Spotify"
    if(!srcApp.empty()){
        D2D1_COLOR_F _c; std::wstring _g, _friendly;
        ResolveAppStyle(srcApp, _c, _g, _friendly);
        m_content.musicSourceApp = _friendly;
    } else {
        m_content.musicSourceApp.clear();
    }
    m_content.isMusicPlaying = playing;
    m_content.musicProgress  = prog;
    if(durationSec > 1.f) m_content.musicTotalSec = durationSec;   // vraie durée SMTC
    m_content.musicCurrentSec = prog * m_content.musicTotalSec;
    // Base pour l'avance continue de la barre entre deux events SMTC
    m_musicBaseSec = m_content.musicCurrentSec;
    m_musicPosTick = GetTickCount();
    m_notifDismissAt=0;

    // Mode ambiant : pré-charge les paroles synchronisées de la nouvelle piste
    m_ambient.OnTrackChanged(ti, ar, m_content.musicTotalSec);

    // File Spotify : re-fetch immédiat à chaque nouvelle piste (temps réel)
    static std::wstring s_lastQueueTrack;
    if(!ti.empty() && ti != s_lastQueueTrack){
        s_lastQueueTrack = ti;
        m_spotify.RefreshNow();
    }

    if(m_renderer){
        m_renderer->UpdateAlbumArt(thumbnailData);
        m_content.albumArtBitmap=m_renderer->GetAlbumArt();

        // Pochette → écran de verrouillage (option Cockpit ; effectif en MSIX)
        if(m_appCfg && m_appCfg->lockWallpaper && !thumbnailData.empty()
           && thumbnailData.size() != m_lockArtBytes
           && GetTickCount() - m_lockArtTick > 10000){
            m_lockArtBytes = thumbnailData.size();
            m_lockArtTick  = GetTickCount();
            UpdateLockScreenArt(thumbnailData);
        }
    }

    if(m_animation->GetDisplayState()==IslandState::Idle && m_content.isHovered)
        m_animation->StartTransition(IslandState::MusicExpanded,400);
}

// ─────────────────────────────────────────────────────────────────────────────
//  ResolveAppStyle
// ─────────────────────────────────────────────────────────────────────────────
static void ResolveAppStyle(const std::wstring& rawApp,
                             D2D1_COLOR_F& color,
                             std::wstring& glyph,
                             std::wstring& displayName)
{
    std::wstring low = rawApp;
    for(auto& c : low) c = towlower(c);

    struct AppEntry {
        const wchar_t* key;
        const wchar_t* name;
        const wchar_t* icon;
        float r, g, b;
    };
    static const AppEntry APPS[] = {
        {L"whatsapp",    L"WhatsApp",        L"\uE717", 0.145f,0.827f,0.400f},
        {L"telegram",    L"Telegram",        L"\uE8F2", 0.165f,0.671f,0.933f},
        {L"signal",      L"Signal",          L"\uE8F2", 0.227f,0.467f,0.941f},
        {L"messenger",   L"Messenger",       L"\uE8F2", 0.000f,0.635f,1.000f},
        {L"discord",     L"Discord",         L"\uE715", 0.345f,0.396f,0.949f},
        {L"skype",       L"Skype",           L"\uE717", 0.000f,0.686f,0.941f},
        {L"viber",       L"Viber",           L"\uE717", 0.475f,0.200f,0.635f},
        {L"line",        L"LINE",            L"\uE8F2", 0.063f,0.773f,0.322f},
        {L"slack",       L"Slack",           L"\uE8F2", 0.290f,0.082f,0.294f},
        {L"teams",       L"Teams",           L"\uE8F2", 0.384f,0.392f,0.655f},
        {L"zoom",        L"Zoom",            L"\uE714", 0.176f,0.549f,1.000f},
        {L"webex",       L"Webex",           L"\uE714", 0.000f,0.498f,0.863f},
        {L"outlook",     L"Outlook",         L"\uE715", 0.000f,0.471f,0.831f},
        {L"gmail",       L"Gmail",           L"\uE715", 0.918f,0.263f,0.208f},
        {L"mail",        L"Courrier",        L"\uE715", 0.000f,0.471f,0.831f},
        {L"thunderbird", L"Thunderbird",     L"\uE715", 0.016f,0.478f,0.706f},
        {L"twitter",     L"Twitter / X",     L"\uE8F2", 0.114f,0.631f,0.949f},
        {L"instagram",   L"Instagram",       L"\uE722", 0.843f,0.157f,0.349f},
        {L"facebook",    L"Facebook",        L"\uE8F2", 0.231f,0.349f,0.596f},
        {L"tiktok",      L"TikTok",          L"\uE714", 0.992f,0.043f,0.290f},
        {L"reddit",      L"Reddit",          L"\uE8F2", 1.000f,0.263f,0.000f},
        {L"linkedin",    L"LinkedIn",        L"\uE77B", 0.039f,0.400f,0.761f},
        {L"snapchat",    L"Snapchat",        L"\uE722", 1.000f,0.988f,0.000f},
        {L"pinterest",   L"Pinterest",       L"\uE722", 0.898f,0.059f,0.188f},
        {L"twitch",      L"Twitch",          L"\uE714", 0.569f,0.275f,1.000f},
        {L"youtube",     L"YouTube",         L"\uE714", 1.000f,0.000f,0.000f},
        {L"spotify",     L"Spotify",         L"\uEC4F", 0.114f,0.722f,0.329f},
        {L"deezer",      L"Deezer",          L"\uEC4F", 1.000f,0.435f,0.098f},
        {L"soundcloud",  L"SoundCloud",      L"\uEC4F", 1.000f,0.341f,0.000f},
        {L"apple music", L"Apple Music",     L"\uEC4F", 0.984f,0.173f,0.439f},
        {L"vlc",         L"VLC",             L"\uE714", 1.000f,0.667f,0.000f},
        {L"netflix",     L"Netflix",         L"\uE714", 0.898f,0.035f,0.078f},
        {L"notion",      L"Notion",          L"\uE8A5", 0.960f,0.961f,0.965f},
        {L"obsidian",    L"Obsidian",        L"\uE8A5", 0.431f,0.247f,0.957f},
        {L"todoist",     L"Todoist",         L"\uE762", 0.886f,0.173f,0.176f},
        {L"trello",      L"Trello",          L"\uE8A5", 0.000f,0.502f,0.800f},
        {L"jira",        L"Jira",            L"\uE8A5", 0.000f,0.329f,0.933f},
        {L"github",      L"GitHub",          L"\uE8FB", 0.882f,0.882f,0.906f},
        {L"vscode",      L"VS Code",         L"\uE8FB", 0.000f,0.478f,0.847f},
        {L"code",        L"VS Code",         L"\uE8FB", 0.000f,0.478f,0.847f},
        {L"steam",       L"Steam",           L"\uE7FC", 0.110f,0.255f,0.435f},
        {L"epic",        L"Epic Games",      L"\uE7FC", 0.933f,0.933f,0.933f},
        {L"battle.net",  L"Battle.net",      L"\uE7FC", 0.000f,0.427f,0.855f},
        {L"xbox",        L"Xbox",            L"\uE7FC", 0.039f,0.525f,0.047f},
        {L"ubisoft",     L"Ubisoft",         L"\uE7FC", 0.000f,0.627f,0.510f},
        {L"windows",     L"Windows",         L"\uE80F", 0.000f,0.471f,0.831f},
        {L"update",      L"Windows Update",  L"\uE777", 0.000f,0.471f,0.831f},
        {L"defender",    L"S\u00E9curit\u00E9", L"\uEA18", 0.063f,0.773f,0.322f},
        {L"firewall",    L"Pare-feu",        L"\uEA18", 1.000f,0.400f,0.000f},
        {L"onedrive",    L"OneDrive",        L"\uEC8C", 0.000f,0.471f,0.831f},
        {L"google drive",L"Drive",           L"\uEC8C", 0.000f,0.678f,0.576f},
        {L"dropbox",     L"Dropbox",         L"\uEC8C", 0.000f,0.420f,1.000f},
        {L"calendar",    L"Calendrier",      L"\uE787", 0.918f,0.263f,0.208f},
        {L"clock",       L"Horloge",         L"\uE823", 0.055f,0.553f,0.847f},
        {L"camera",      L"Appareil photo",  L"\uE722", 0.200f,0.200f,0.220f},
        {L"photos",      L"Photos",          L"\uE722", 0.000f,0.471f,0.831f},
        {L"amazon",      L"Amazon",          L"\uE7BF", 1.000f,0.600f,0.000f},
        {L"ebay",        L"eBay",            L"\uE7BF", 0.918f,0.263f,0.208f},
        {L"paypal",      L"PayPal",          L"\uE7BF", 0.000f,0.157f,0.510f},
        {L"uber",        L"Uber",            L"\uE8B0", 0.000f,0.000f,0.000f},
        {L"maps",        L"Maps",            L"\uE707", 0.918f,0.263f,0.208f},
        {L"waze",        L"Waze",            L"\uE707", 0.090f,0.812f,0.749f},
    };

    for(const auto& e : APPS){
        if(low.find(e.key)!=std::wstring::npos){
            color       = {e.r,e.g,e.b,1.0f};
            glyph       = e.icon;
            displayName = e.name;
            return;
        }
    }
    color       = {0.52f,0.52f,0.56f,1.0f};
    glyph       = L"\uEA8F";
    displayName = rawApp.empty() ? L"Notification" : rawApp;
}

// ─────────────────────────────────────────────────────────────────────────────
//  TriggerNotification
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::TriggerNotification(const std::wstring& app,
                                         const std::wstring& title,
                                         const std::wstring& msg)
{
    m_preNotifState = IslandState::Idle;
    m_content.notifTitle   = title;
    m_content.notifMessage = msg;

    std::wstring displayName;
    ResolveAppStyle(app, m_content.notifAppColor, m_content.notifIconGlyph, displayName);
    m_content.notifAppName = displayName;

    m_content.bellShakeT = 0.001f;
    m_notifDismissAt = GetTickCount() + NOTIF_MS;
    if(m_appCfg && m_appCfg->soundNotif)
        Sounds::PlayNotif(m_appCfg->volNotif, m_appCfg->soundNotifVar);             // son « Éclat »
    m_animation->StartTransition(IslandState::NotifExpanded, 320);

    NotifHistoryItem hi;
    hi.appName   = displayName;
    hi.message   = msg.empty() ? title : msg;
    hi.iconGlyph = m_content.notifIconGlyph;
    hi.appColor  = m_content.notifAppColor;
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t tbuf[12]; swprintf_s(tbuf,L"%02d:%02d",st.wHour,st.wMinute);
    hi.timeStr = tbuf;
    m_notifHistory.push_back(hi);
    if(m_notifHistory.size()>20) m_notifHistory.erase(m_notifHistory.begin());
    m_content.notifHistory = m_notifHistory;

    auto& badge      = m_appBadges[displayName];
    badge.appName    = displayName;
    badge.color      = m_content.notifAppColor;
    badge.iconGlyph  = m_content.notifIconGlyph;
    badge.count++;
    m_badgePulseT        = 0.f;
    m_content.badgePulseT= 0.f;
}

void WindowManager::TriggerHUD(HUDType type,float val,const std::wstring& lbl)
{
    m_preNotifState = IslandState::Idle;
    m_content.hudType=type; m_content.hudValue=val; m_content.hudLabel=lbl;
    IslandState hs=(type==HUDType::Volume)?IslandState::HUDVolume:
                   (type==HUDType::Brightness)?IslandState::HUDBrightness:
                                               IslandState::HUDNetwork;
    m_notifDismissAt = GetTickCount() + HUD_MS;
    m_animation->StartTransition(hs,280);
}

void WindowManager::TriggerSystem()
{
    UpdateSystemStats();
    QuerySystemControls();
    m_hoverExpanded=true;
    m_animation->StartTransition(IslandState::SystemExpanded,300);
}

void WindowManager::Collapse()
{
    m_hoverExpanded=false;
    m_animation->StartTransition(IslandState::Idle,300);
}

void WindowManager::UpdateMusicProgress(float p)
{
    m_content.musicProgress  =p;
    m_content.musicCurrentSec=p*m_content.musicTotalSec;
}

// ─────────────────────────────────────────────────────────────────────────────
//  TransitionToNotifList — hauteur adaptative
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::TransitionToNotifList()
{
    m_hoverExpanded=true;
    int cnt=(int)m_notifHistory.size();
    float h=ComputeNotifListHeight(cnt);
    m_animation->StartTransitionCustom(
        IslandState::NotifList, Pill::W_NOTIF_LIST, h, Pill::CR_NOTIF_LIST, 300);
}

// ─────────────────────────────────────────────────────────────────────────────
//  SwitchMenu — change d'onglet avec animation slide
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::SwitchMenu(int newMenu)
{
    if(newMenu==m_activeMenu) return;

    m_tabSlideDir = (newMenu > m_activeMenu) ? -1.f : +1.f;
    m_tabPrevMenu = m_activeMenu;
    m_activeMenu  = newMenu;
    m_content.activeMenuIndex = newMenu;

    m_tabSlideT     = 0.f;
    m_tabSlideStart = GetTickCount();
    m_hoverExpanded = true;

    if(m_activeMenu==0){
        QuerySystemControls();
        TransitionToNotifList();
    } else if(m_activeMenu==1){
        m_queueScroll=0.f; m_content.queueScrollY=0.f;
        m_animation->StartTransition(
            m_showQueue?IslandState::MusicQueue:IslandState::MusicExpanded,280);
    } else {
        QuerySystemControls();
        m_animation->StartTransition(IslandState::SystemExpanded,280);
    }
    InvalidateRect(m_hwnd,nullptr,FALSE);
}

// ─────────────────────────────────────────────────────────────────────────────
//  OnAnimTick — P0-FIX : plus d'appel UpdateSystemStats ici
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::OnAnimTick()
{
    m_animation->Tick();
    bool needRedraw=false;

    // Animation waveform musique
    if(m_animation->GetDisplayState()==IslandState::MusicExpanded ||
       m_animation->GetDisplayState()==IslandState::MusicQueue    ||
       m_animation->GetDisplayState()==IslandState::Idle){
        if(m_content.isMusicPlaying){
            // P0-FIX : globalT borné à [0, 1000] pour éviter la perte de précision float
            m_content.globalT = fmodf(m_content.globalT + 0.016f, 1000.f);
            // Avance continue de la progression (entre 2 events SMTC)
            if(m_content.musicTotalSec > 1.f){
                float cur = m_musicBaseSec + (GetTickCount()-m_musicPosTick)/1000.f;
                if(cur > m_content.musicTotalSec) cur = m_content.musicTotalSec;
                m_content.musicCurrentSec = cur;
                m_content.musicProgress   = cur / m_content.musicTotalSec;
                // Préchargement des paroles du PROCHAIN titre ~10 s avant la fin
                // (uniquement en ambiant + si la file Spotify donne le titre suivant)
                // → élimine le « gap » de chargement au changement de piste.
                if(m_ambient.IsOpen() && !m_content.upcomingTracks.empty()){
                    float remain = m_content.musicTotalSec - cur;
                    if(remain > 0.f && remain < 10.f){
                        const auto& nx = m_content.upcomingTracks[0];
                        m_ambient.PrefetchLyrics(nx.title, nx.artist, 210.f);
                    }
                }
            }
            needRedraw=true;
        }
    }

    // Animation cloche notification
    if(m_content.bellShakeT>0.f){
        m_content.bellShakeT+=0.016f;
        if(m_content.bellShakeT>1.0f) m_content.bellShakeT=0.f;
        needRedraw=true;
    }

    // Slide entre onglets
    if(m_tabSlideT<1.f){
        DWORD elapsed=GetTickCount()-m_tabSlideStart;
        m_tabSlideT=std::min(1.f,(float)elapsed/(float)m_tabSlideDuration);
        float ease=LocalCubicEase(m_tabSlideT);
        float pw=m_content.pillW;
        m_content.tabSlideX=m_tabSlideDir*pw*(1.f-ease);
        needRedraw=true;
    } else {
        m_content.tabSlideX=0.f;
    }

    // App Badges — animation pulse (0→1 en 400ms)
    if(m_badgePulseT<1.f){
        m_badgePulseT=std::min(1.f, m_badgePulseT+0.016f/0.4f);
        needRedraw=true;
    }
    m_content.badgePulseT=m_badgePulseT;

    // Rebuild badge list
    {
        m_content.appBadges.clear();
        m_content.totalUnreadCount=0;
        for(auto& kv : m_appBadges){
            m_content.appBadges.push_back(kv.second);
            m_content.totalUnreadCount+=kv.second.count;
        }
        std::sort(m_content.appBadges.begin(),m_content.appBadges.end(),
                  [](const AppBadge& a,const AppBadge& b){ return a.count>b.count; });
    }

    if(m_animation->GetDisplayState()==IslandState::NotifList)
        m_content.notifHistory=m_notifHistory;

    // Détection sortie hover — CRITIQUE : les coords Win32 sont en PIXELS
    // PHYSIQUES ; m_content.pillW / PillX sont en unités LOGIQUES. Sans
    // conversion, l'expansion ne se referme JAMAIS en 125/150 % (le curseur
    // paraît toujours à l'intérieur du pill → carte musique/notif figée).
    if(m_hoverExpanded){
        POINT pt; GetCursorPos(&pt); ScreenToClient(m_hwnd,&pt);
        RECT rc; GetClientRect(m_hwnd,&rc);
        // Convertit tout en LOGIQUES pour comparer avec pillLeft/pillRight
        float px = (float)pt.x / m_dpiScale;
        float py = (float)pt.y / m_dpiScale;
        float rcRight  = rc.right  / m_dpiScale;
        float rcBottom = rc.bottom / m_dpiScale;
        float pw=m_content.pillW;
        float pillLeft=PillX(rcRight,pw), pillRight=pillLeft+pw;
        const float EXIT_MARGIN=4.f;
        if(px<pillLeft-EXIT_MARGIN||px>pillRight+EXIT_MARGIN||
           py<-EXIT_MARGIN        ||py>rcBottom+EXIT_MARGIN){
            m_hoverExpanded=false;
            m_content.isHovered=false;
            m_mouseTracking=false;
            m_animation->StartTransition(IslandState::Idle,300);
            needRedraw=true;
        }
    }

    if(needRedraw || m_animation->IsAnimating())
        InvalidateRect(m_hwnd,nullptr,FALSE);
}

void WindowManager::OnPaint()
{
    PAINTSTRUCT ps; BeginPaint(m_hwnd,&ps);
    m_renderer->Draw(m_content);
    EndPaint(m_hwnd,&ps);
}

// ─────────────────────────────────────────────────────────────────────────────
//  OnMouseMove
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::OnMouseMove(int x,int y)
{
    if(!m_mouseTracking){
        TRACKMOUSEEVENT tme={sizeof(tme),TME_LEAVE,m_hwnd,0};
        TrackMouseEvent(&tme);
        m_mouseTracking=true;
    }

    if(m_dragSeek){ ApplySeekVisual(SeekFracFromX(x)); return; }

    if(m_dragSlider){
        float pw2=m_content.pillW, px2=PillX(ScreenWLogical(),pw2);
        float pad=13.f;
        // sw DOIT correspondre exactement à DrawSystem (iconW=22), sinon le drag est décalé.
        float sx=px2+pad, sw=pw2-pad*2.f-22.f-8.f;
        float v=std::clamp((float)(x-(int)sx)/sw,0.f,1.f);
        if(m_dragSliderIdx==0)      SetSystemVolume(v);
        else if(m_dragSliderIdx==1){ m_sysBrightness=v; m_content.systemBrightness=v; } // HW au relâchement
        InvalidateRect(m_hwnd,nullptr,FALSE);
        return;
    }

    if(!m_content.isHovered){
        m_content.isHovered=true;
        if(m_animation->GetDisplayState()==IslandState::Idle){
            m_hoverExpanded=true;
            if(m_appCfg && m_appCfg->soundExpand)
                Sounds::PlayExpand(m_appCfg->volExpand, m_appCfg->soundExpandVar);   // son « Subtil »
            if(m_activeMenu==0){
                QuerySystemControls(); TransitionToNotifList();
            } else if(m_activeMenu==2){
                QuerySystemControls();
                m_animation->StartTransition(IslandState::SystemExpanded,300);
            } else {
                m_animation->StartTransition(
                    m_showQueue?IslandState::MusicQueue:IslandState::MusicExpanded,300);
            }
        } else {
            m_hoverExpanded=true;
        }
        InvalidateRect(m_hwnd,nullptr,FALSE);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  OnMouseDown
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::OnMouseDown(int x,int y)
{
    m_mouseDownTime=GetTickCount();
    m_mouseDownX=x; m_mouseDownY=y;
    m_mouseDownOnWifi=false;
    m_longPressArmed=false;
    m_dragSlider=false;

    IslandState ds=m_animation->GetDisplayState();

    // ── Barre de progression musique : clic/drag = SEEK ──────────────────
    if(ds==IslandState::MusicExpanded || ds==IslandState::MusicQueue){
        float pw=m_content.pillW, px=PillX(ScreenWLogical(),pw);
        float barY = (ds==IslandState::MusicExpanded) ? 98.f : 104.f;  // miroir Renderer
        if(y>=(int)(barY-10) && y<=(int)(barY+16) &&
           x>=(int)(px+8) && x<=(int)(px+pw-8)){
            m_dragSeek=true;
            SetCapture(m_hwnd);
            ApplySeekVisual(SeekFracFromX(x));
            return;
        }
    }

    if(ds==IslandState::SystemExpanded){
        float pw2=m_content.pillW, px2=PillX(ScreenWLogical(),pw2);
        float pad=13.f;
        float row1Y=38.f, pillH2=48.f;
        float pw1=(pw2-pad*2.f-10.f)*0.38f;
        float row2Y=row1Y+pillH2+7.f;
        float sy1=row2Y+48.f, sy2=sy1+38.f;
        // sw aligné sur DrawSystem (iconW=22) pour que le clic tombe pile sur le slider.
        float sx=px2+pad, sw=pw2-pad*2.f-22.f-8.f;

        if(y>=(int)(sy1-14)&&y<=(int)(sy1+22)&&x>=(int)sx&&x<=(int)(sx+sw)){
            m_dragSlider=true; m_dragSliderIdx=0;
            SetCapture(m_hwnd);
            SetSystemVolume(std::clamp((float)(x-(int)sx)/sw,0.f,1.f));
            InvalidateRect(m_hwnd,nullptr,FALSE);
        } else if(y>=(int)(sy2-14)&&y<=(int)(sy2+22)&&x>=(int)sx&&x<=(int)(sx+sw)){
            m_dragSlider=true; m_dragSliderIdx=1;
            SetCapture(m_hwnd);
            float bv=std::clamp((float)(x-(int)sx)/sw,0.f,1.f);
            m_sysBrightness=bv; m_content.systemBrightness=bv; // visuel; HW au relâchement
            InvalidateRect(m_hwnd,nullptr,FALSE);
        } else if(y>=(int)row1Y&&y<=(int)(row1Y+pillH2)){
            float x1=px2+pad;
            float pw1b=(pw2-pad*2.f-10.f)*0.38f;
            if(x>=(int)x1&&x<=(int)(x1+pw1b)){
                m_mouseDownOnWifi=true;
                m_longPressArmed=true;
                SetTimer(m_hwnd,TIMER_LONGPRESS,620,nullptr);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  OnMouseLeave
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::OnMouseLeave()
{
    m_mouseTracking=false;
    m_content.isHovered=false;
    if(m_dragSlider){ m_dragSlider=false; ReleaseCapture(); }
    if(m_dragSeek){ m_dragSeek=false; ReleaseCapture(); }
    if(m_hoverExpanded){
        m_hoverExpanded=false;
        m_animation->StartTransition(IslandState::Idle,300);
    }
    InvalidateRect(m_hwnd,nullptr,FALSE);
}

// ─────────────────────────────────────────────────────────────────────────────
//  OnMouseUp
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::OnMouseUp(int x,int y)
{
    KillTimer(m_hwnd,TIMER_LONGPRESS);
    bool wasWifi=m_mouseDownOnWifi&&m_longPressArmed;
    m_mouseDownOnWifi=false;
    m_longPressArmed=false;

    if(m_dragSeek){
        m_dragSeek=false; ReleaseCapture();
        float frac=SeekFracFromX(x);
        ApplySeekVisual(frac);
        if(m_mediaManager) m_mediaManager->SeekTo(frac * m_content.musicTotalSec);
        return;
    }

    if(m_dragSlider){
        bool wasBright=(m_dragSliderIdx==1); float bv=m_sysBrightness;
        m_dragSlider=false; ReleaseCapture();
        if(wasBright) SetSystemBrightness(bv);  // une seule application → plus de crash/spam de process
        return;
    }

    IslandState ds=m_animation->GetDisplayState();

    // Onglets menu (haut)
    if(ds==IslandState::MusicExpanded||ds==IslandState::MusicQueue||
       ds==IslandState::NotifExpanded||ds==IslandState::NotifList||
       ds==IslandState::SystemExpanded||ds==IslandState::WifiList){
        float cx2=PillX(ScreenWLogical(),m_content.pillW)+m_content.pillW*.5f;   // centre du PILL (pas de l'écran)
        struct{float x;int idx;}menus[]={{cx2-58,0},{cx2,1},{cx2+58,2}};
        for(auto& m:menus){
            if(x>=m.x-22&&x<=m.x+22&&y>=4&&y<=30){
                SwitchMenu(m.idx); return;
            }
        }
    }

    // ── Panneau liste Wi-Fi : retour / interrupteur / réseaux / mot de passe ──
    if(ds==IslandState::WifiList){
        float pw2=m_content.pillW, px2=PillX(ScreenWLogical(),pw2), pad=14.f;
        // Mode saisie mot de passe : boutons Connecter (droite) / Annuler (gauche)
        if(m_wifiPassMode){
            float by=m_content.pillH-40.f;
            if(y>=(int)(by-6)&&y<=(int)(by+28)){
                if(x>=(int)(px2+pw2-pad-104)){          // Connecter
                    ConnectWifi(m_wifiPassSsid, m_wifiPassInput);
                    SetWifiPassMode(false);
                    m_content.wifiStatusMsg=L"Connexion…";
                } else if(x<=(int)(px2+pad+90)){          // Annuler
                    SetWifiPassMode(false);
                }
                InvalidateRect(m_hwnd,nullptr,FALSE);
            }
            return;
        }
        // Flèche retour (haut-gauche) → panneau système
        if(x>=(int)(px2+pad)&&x<=(int)(px2+pad+28)&&y>=10&&y<=32){
            SetWifiPassMode(false);
            QuerySystemControls();
            m_animation->StartTransition(IslandState::SystemExpanded,260);
            return;
        }
        // Interrupteur Wi-Fi (haut-droite) → activer/désactiver puis rafraîchir
        float tglX=px2+pw2-pad-40.f;
        if(x>=(int)(tglX-4)&&x<=(int)(px2+pw2-pad+4)&&y>=8&&y<=40){
            ToggleWifi(); OpenWifiList(); return;
        }
        // Clic sur un réseau → connexion native (ou champ mot de passe si sécurisé nouveau)
        float itemH=48.f, gap=5.f, iy=48.f;
        for(int i=0;i<(int)m_wifiNetworks.size();++i){
            if(y>=(int)iy&&y<=(int)(iy+itemH)){
                const auto& n=m_wifiNetworks[i];
                if(!n.connected){
                    if(n.hasProfile || !n.secured){     // enregistré ou ouvert → direct
                        ConnectWifi(n.ssid, L"");
                        m_content.wifiStatusMsg=L"Connexion…";
                        m_content.wifiConnectingIdx=i;
                    } else {                             // sécurisé nouveau → mot de passe
                        SetWifiPassMode(true, n.ssid);
                    }
                    InvalidateRect(m_hwnd,nullptr,FALSE);
                }
                break;
            }
            iy+=itemH+gap;
        }
        return;
    }

    // ── Panneau liste Bluetooth : retour / interrupteur / appareils ──────────
    if(ds==IslandState::BluetoothList){
        float pw2=m_content.pillW, px2=PillX(ScreenWLogical(),pw2), pad=14.f;
        // Flèche retour (haut-gauche) → panneau système
        if(x>=(int)(px2+pad)&&x<=(int)(px2+pad+28)&&y>=10&&y<=32){
            QuerySystemControls();
            m_animation->StartTransition(IslandState::SystemExpanded,260);
            return;
        }
        // Interrupteur (haut-droite) → activer/désactiver le BT puis rafraîchir
        float tglX=px2+pw2-pad-40.f;
        if(x>=(int)(tglX-4)&&x<=(int)(px2+pw2-pad+4)&&y>=8&&y<=40){
            ToggleBluetooth();
            OpenBluetoothList();   // re-query + reste sur le panneau (nouveau statut)
            return;
        }
        // Clic sur un appareil → connexion/déconnexion/couplage in-island.
        // Miroir EXACT du layout DrawBluetoothList (en-têtes de section + scroll).
        float itemH2=48.f, gap2=5.f, sepY2=40.f;
        float yy2=sepY2+8.f - m_content.btScrollY;
        bool hp=false, ha=false;
        for(int i=0;i<(int)m_btDevices.size();++i){
            const auto& d=m_btDevices[i];
            if(d.paired && !hp){ hp=true; yy2+=22.f; }
            if(!d.paired && !ha){ ha=true; yy2+=26.f; }
            if(y>=(int)yy2 && y<=(int)(yy2+itemH2)){
                ConnectBluetoothDevice(d.name);   // toggle : connecte / déconnecte / couple
                break;
            }
            yy2+=itemH2+gap2;
        }
        return;
    }

    if(ds==IslandState::MusicExpanded||ds==IslandState::MusicQueue){
        float pw=m_content.pillW, ph=m_content.pillH;
        float px=PillX(ScreenWLogical(),pw), cx2=px+pw*.5f;
        float ctrlY=(ds==IslandState::MusicExpanded)
            ?(128.f+(ph-128.f)*.5f):(40.f+54.f+12.f+42.f);

        if(y>=(int)(ctrlY-28)&&y<=(int)(ctrlY+28)){
            float xs[]={px+48,cx2-76,cx2,cx2+76,px+pw-48};
            for(int i=0;i<5;++i){
                if(x>=(int)(xs[i]-28)&&x<=(int)(xs[i]+28)){
                    if(i==0){
                        m_showQueue=!m_showQueue;
                        m_queueScroll=0.f; m_content.queueScrollY=0.f;   // repart en haut
                        m_animation->StartTransition(
                            m_showQueue?IslandState::MusicQueue:IslandState::MusicExpanded,320);
                    }
                    if(i==4) ShowAudioDevicesMenu();
                    if(m_mediaManager){
                        if(i==1) m_mediaManager->SkipPrevious();
                        if(i==2){
                            m_mediaManager->PlayPause();
                            m_content.isMusicPlaying=!m_content.isMusicPlaying;
                        }
                        if(i==3) m_mediaManager->SkipNext();
                    }
                    InvalidateRect(m_hwnd,nullptr,FALSE);
                    return;
                }
            }
        }
    }

    // ── Clic sur un titre de la file → y sauter (multi-skip) ──────────────
    if(ds==IslandState::MusicQueue && !m_content.upcomingTracks.empty()){
        float pw=m_content.pillW, px=PillX(ScreenWLogical(),pw);
        const float ITEM_H=40.f, GAP=5.f;
        float listTop = 38.f+54.f+12.f+42.f+28.f+5.f+20.f;   // miroir DrawMusicQueue (=199)
        if(x>=(int)(px+12) && x<=(int)(px+pw-12) && y>=(int)listTop){
            float fy=(float)y - listTop + m_queueScroll;
            int   idx=(int)(fy/(ITEM_H+GAP));
            float within=fy - idx*(ITEM_H+GAP);
            if(idx>=0 && idx<(int)m_content.upcomingTracks.size() && within<=ITEM_H){
                PlayQueueIndex(idx);
                return;
            }
        }
    }

    if(ds==IslandState::NotifList){
        float pw2=m_content.pillW, px2=PillX(ScreenWLogical(),pw2);
        float pad=14.f, btnW=110.f;
        float btnX=px2+pw2-pad-btnW;
        if(x>=(int)btnX&&x<=(int)(btnX+btnW)&&y>=38&&y<=62){
            m_notifHistory.clear(); m_content.notifHistory.clear();
            m_appBadges.clear();
            m_content.appBadges.clear();
            m_content.totalUnreadCount=0;
            m_badgePulseT=1.f; m_content.badgePulseT=1.f;
            TransitionToNotifList();
            InvalidateRect(m_hwnd,nullptr,FALSE);
        }
    } else if(ds==IslandState::SystemExpanded){
        float pw2=m_content.pillW, px2=PillX(ScreenWLogical(),pw2), pad=13.f;
        float row1Y=38.f, pillH2=48.f;
        float pw1=(pw2-pad*2.f-10.f)*0.38f;
        float pw2b=pw1;
        float pw3=pw2-pad*2.f-pw1*2.f-10.f;
        float row2Y=row1Y+pillH2+7.f;

        if(y>=(int)row1Y&&y<=(int)(row1Y+pillH2)){
            float x1=px2+pad, x2=x1+pw1+5.f, x3=x2+pw2b+5.f;
            if(x>=(int)x1&&x<=(int)(x1+pw1)){
                OpenWifiList();        // ouvre la liste Wi-Fi in-island (toggle DANS le panneau)
            } else if(x>=(int)x2&&x<=(int)(x2+pw2b)){
                OpenBluetoothList();   // ouvre la liste des appareils BT (au lieu de toggler)
            } else if(x>=(int)x3&&x<=(int)(x3+pw3)){
                ToggleDarkMode();
            }
        } else if(y>=(int)row2Y&&y<=(int)(row2Y+38)){
            if(x>=(int)(px2+pad)&&x<=(int)(px2+pad+140)) ToggleNightLight();
        } else {
            Collapse();
        }
        InvalidateRect(m_hwnd,nullptr,FALSE);
    } else if(ds==IslandState::HUDVolume||
              ds==IslandState::HUDBrightness||
              ds==IslandState::HUDNetwork){
        Collapse();
    } else if(ds==IslandState::Idle&&m_content.musicTitle.empty()){
        TriggerSystem();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  OnMouseWheel — défilement de la liste Playing Next (vue MusicQueue)
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::OnMouseWheel(short delta)
{
    // Scroll de la liste Bluetooth (couplés + disponibles).
    if(m_animation->GetDisplayState()==IslandState::BluetoothList){
        const float ITEM_H=48.f, GAP=5.f;
        int n=(int)m_btDevices.size();
        // hauteur contenu ≈ items + en-têtes (22 couplés + 26 dispo si présents)
        bool anyP=false, anyA=false;
        for(const auto& d:m_btDevices){ if(d.paired)anyP=true; else anyA=true; }
        float contentH = n*(ITEM_H+GAP)-GAP + (anyP?22.f:0.f) + (anyA?26.f:0.f);
        float visibleH = Pill::H_BT - 48.f - 8.f;
        float maxScroll=(std::max)(0.f, contentH-visibleH);
        m_content.btScrollY = std::clamp(m_content.btScrollY-(float)delta/120.f*(ITEM_H+GAP),
                                         0.f, maxScroll);
        InvalidateRect(m_hwnd,nullptr,FALSE);
        return;
    }
    if(m_animation->GetDisplayState()!=IslandState::MusicQueue) return;
    const auto& tracks = m_content.upcomingTracks;   // file Spotify uniquement
    int n=(int)tracks.size(); if(n<=0) return;

    // Miroir des métriques de DrawMusicQueue (convention dessin ↔ logique)
    const float ITEM_H=40.f, GAP=5.f;
    float ctrlY   = 38.f+54.f+12.f+42.f;          // artY+artS+12 → barY, +42 → ctrlY
    float listTop = ctrlY+28.f+5.f+20.f;          // +sepY +qlY +label
    float visibleH= Pill::H_QUEUE - listTop - 8.f;
    float contentH= n*(ITEM_H+GAP)-GAP;
    float maxScroll=(std::max)(0.f, contentH-visibleH);

    m_queueScroll = std::clamp(m_queueScroll-(float)delta/120.f*(ITEM_H+GAP),
                               0.f, maxScroll);
    m_content.queueScrollY = m_queueScroll;
    InvalidateRect(m_hwnd,nullptr,FALSE);
}

// ─────────────────────────────────────────────────────────────────────────────
//  OnLongPress
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::OnLongPress()
{
    if(!m_mouseDownOnWifi) return;
    m_mouseDownOnWifi=false;
    m_longPressArmed=false;
    QueryWifiNetworks();
    m_content.wifiNetworks=m_wifiNetworks;
    m_animation->StartTransition(IslandState::WifiList,280);
    InvalidateRect(m_hwnd,nullptr,FALSE);
}

// ─────────────────────────────────────────────────────────────────────────────
//  OnGesturePan — swipe deux doigts pour changer d'onglet
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::OnGesturePan(LONG screenX, DWORD flags)
{
    if(flags & GF_BEGIN){
        m_gestureActive=true;
        m_gesturePrevX=screenX;
        m_gestureAccumX=0.f;
        return;
    }
    if(flags & GF_END){
        m_gestureActive=false;
        m_gestureAccumX=0.f;
        return;
    }
    if(!m_gestureActive) return;

    IslandState ds=m_animation->GetDisplayState();
    bool isExpanded=(ds==IslandState::MusicExpanded||ds==IslandState::MusicQueue||
                     ds==IslandState::NotifList||ds==IslandState::SystemExpanded||
                     ds==IslandState::WifiList||ds==IslandState::BluetoothList);
    if(!isExpanded) return;

    LONG delta=screenX-m_gesturePrevX;
    m_gesturePrevX=screenX;
    m_gestureAccumX+=(float)delta;

    if(m_gestureAccumX>SWIPE_THRESH){
        m_gestureAccumX=0.f;
        int newMenu=m_activeMenu-1; if(newMenu<0) newMenu=2;
        SwitchMenu(newMenu);
    } else if(m_gestureAccumX<-SWIPE_THRESH){
        m_gestureAccumX=0.f;
        int newMenu=m_activeMenu+1; if(newMenu>2) newMenu=0;
        SwitchMenu(newMenu);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  QuerySystemControls
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::QuerySystemControls()
{
    // Volume
    {
        IMMDeviceEnumerator* pEnum=nullptr;
        if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,
                                      __uuidof(IMMDeviceEnumerator),(void**)&pEnum))&&pEnum){
            IMMDevice* pDev=nullptr;
            if(SUCCEEDED(pEnum->GetDefaultAudioEndpoint(eRender,eConsole,&pDev))&&pDev){
                IAudioEndpointVolume* pVol=nullptr;
                if(SUCCEEDED(pDev->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,
                                            nullptr,(void**)&pVol))&&pVol){
                    float v=0.f; pVol->GetMasterVolumeLevelScalar(&v);
                    m_sysVolume=v; m_content.systemVolume=v;
                    pVol->Release();
                }
                pDev->Release();
            }
            pEnum->Release();
        }
    }
    // Mode sombre
    {
        HKEY hKey=nullptr;
        if(RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0,KEY_READ,&hKey)==ERROR_SUCCESS){
            DWORD val=1,sz=sizeof(val);
            RegQueryValueExW(hKey,L"AppsUseLightTheme",nullptr,nullptr,(LPBYTE)&val,&sz);
            RegCloseKey(hKey);
            m_darkMode=(val==0); m_content.darkModeEnabled=m_darkMode;
        }
    }
    // Night light
    {
        HKEY hNL=nullptr;
        const wchar_t* nlKey=
            L"Software\\Microsoft\\Windows\\CurrentVersion\\CloudStore\\Store\\"
            L"DefaultAccount\\Current\\default$windows.data.bluelightreduction."
            L"bluelightreductionstate\\Current";
        m_nightLight=false;
        if(RegOpenKeyExW(HKEY_CURRENT_USER,nlKey,0,KEY_READ,&hNL)==ERROR_SUCCESS){
            DWORD sz=0;
            RegQueryValueExW(hNL,L"Data",nullptr,nullptr,nullptr,&sz);
            if(sz>=22){
                std::vector<BYTE> data(sz);
                if(RegQueryValueExW(hNL,L"Data",nullptr,nullptr,data.data(),&sz)==ERROR_SUCCESS)
                    m_nightLight=(data[18]==0x10||data[18]==0x13||data[18]==0x15);
            }
            RegCloseKey(hNL);
        }
        m_content.nightLightEnabled=m_nightLight;
    }
    // Wi-Fi
    {
        m_wifiEnabled=false; m_wifiSSID.clear();
        HANDLE hWlan=nullptr; DWORD ver=0;
        if(WlanOpenHandle(2,nullptr,&ver,&hWlan)==ERROR_SUCCESS){
            PWLAN_INTERFACE_INFO_LIST pList=nullptr;
            if(WlanEnumInterfaces(hWlan,nullptr,&pList)==ERROR_SUCCESS&&pList){
                for(DWORD i=0;i<pList->dwNumberOfItems;++i){
                    const auto& intf = pList->InterfaceInfo[i];
                    // « Activé » = état RADIO logicielle ON (pas l'état de connexion) :
                    // se déconnecter d'un réseau ne désactive PAS le Wi-Fi.
                    PWLAN_RADIO_STATE prs=nullptr; DWORD rsz=0;
                    if(WlanQueryInterface(hWlan,&intf.InterfaceGuid,wlan_intf_opcode_radio_state,
                                          nullptr,&rsz,(PVOID*)&prs,nullptr)==ERROR_SUCCESS&&prs){
                        for(DWORD p=0;p<prs->dwNumberOfPhys;++p)
                            if(prs->PhyRadioState[p].dot11SoftwareRadioState==dot11_radio_state_on &&
                               prs->PhyRadioState[p].dot11HardwareRadioState==dot11_radio_state_on){
                                m_wifiEnabled=true; break;
                            }
                        WlanFreeMemory(prs);
                    }
                    // SSID courant si connecté (pour l'affichage).
                    if(intf.isState==wlan_interface_state_connected){
                        m_wifiEnabled=true;   // connecté ⇒ forcément actif
                        PWLAN_CONNECTION_ATTRIBUTES pAttr=nullptr; DWORD attrSz=0;
                        if(WlanQueryInterface(hWlan,&intf.InterfaceGuid,
                                              wlan_intf_opcode_current_connection,
                                              nullptr,&attrSz,(PVOID*)&pAttr,nullptr)==ERROR_SUCCESS&&pAttr){
                            auto& ssid=pAttr->wlanAssociationAttributes.dot11Ssid;
                            if(ssid.uSSIDLength>0)
                                m_wifiSSID=std::wstring(ssid.ucSSID,ssid.ucSSID+ssid.uSSIDLength);
                            WlanFreeMemory(pAttr);
                        }
                    }
                }
                WlanFreeMemory(pList);
            }
            WlanCloseHandle(hWlan,nullptr);
        }
        m_content.wifiEnabled=m_wifiEnabled;
        m_content.wifiSSID   =m_wifiSSID;
    }
    // Bluetooth
    {
        m_btEnabled=false;
        BLUETOOTH_FIND_RADIO_PARAMS bp={sizeof(bp)};
        HANDLE hRadio=nullptr;
        HBLUETOOTH_RADIO_FIND hFind=BluetoothFindFirstRadio(&bp,&hRadio);
        if(hFind){ m_btEnabled=true; if(hRadio)CloseHandle(hRadio); BluetoothFindRadioClose(hFind); }
        m_content.bluetoothEnabled=m_btEnabled;
    }
    // Luminosité
    {
        HMONITOR hMon=MonitorFromWindow(m_hwnd,MONITOR_DEFAULTTOPRIMARY);
        DWORD numPhys=0;
        if(GetNumberOfPhysicalMonitorsFromHMONITOR(hMon,&numPhys)&&numPhys>0){
            std::vector<PHYSICAL_MONITOR> pm(numPhys);
            if(GetPhysicalMonitorsFromHMONITOR(hMon,numPhys,pm.data())){
                DWORD minB=0,curB=50,maxB=100;
                if(GetMonitorBrightness(pm[0].hPhysicalMonitor,&minB,&curB,&maxB)&&maxB>minB)
                    m_sysBrightness=(float)(curB-minB)/(float)(maxB-minB);
                DestroyPhysicalMonitors(numPhys,pm.data());
            }
        }
        m_content.systemBrightness=m_sysBrightness;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  QueryWifiNetworks
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::QueryWifiNetworks()
{
    m_wifiNetworks.clear();
    HANDLE hWlan=nullptr; DWORD ver=0;
    if(WlanOpenHandle(2,nullptr,&ver,&hWlan)!=ERROR_SUCCESS) return;

    PWLAN_INTERFACE_INFO_LIST pList=nullptr;
    if(WlanEnumInterfaces(hWlan,nullptr,&pList)!=ERROR_SUCCESS||!pList){
        WlanCloseHandle(hWlan,nullptr); return;
    }
    for(DWORD ii=0;ii<pList->dwNumberOfItems;++ii){
        GUID guid=pList->InterfaceInfo[ii].InterfaceGuid;
        WlanScan(hWlan,&guid,nullptr,nullptr,nullptr);
        PWLAN_AVAILABLE_NETWORK_LIST pNetList=nullptr;
        if(WlanGetAvailableNetworkList(hWlan,&guid,
            WLAN_AVAILABLE_NETWORK_INCLUDE_ALL_ADHOC_PROFILES,nullptr,&pNetList)==ERROR_SUCCESS&&pNetList){
            for(DWORD n=0;n<pNetList->dwNumberOfItems&&m_wifiNetworks.size()<8;++n){
                const auto& net=pNetList->Network[n];
                if(net.dot11Ssid.uSSIDLength==0) continue;
                WifiNetworkItem item;
                item.ssid=std::wstring(net.dot11Ssid.ucSSID,
                                       net.dot11Ssid.ucSSID+net.dot11Ssid.uSSIDLength);
                item.signal=(int)net.wlanSignalQuality;
                item.connected=(net.dwFlags&WLAN_AVAILABLE_NETWORK_CONNECTED)!=0;
                item.secured=(net.bSecurityEnabled!=FALSE);
                item.hasProfile=(net.strProfileName[0]!=L'\0');   // profil enregistré
                m_wifiNetworks.push_back(item);
            }
            WlanFreeMemory(pNetList);
        }
    }
    WlanFreeMemory(pList);
    WlanCloseHandle(hWlan,nullptr);

    if(m_wifiNetworks.empty()&&m_wifiEnabled&&!m_wifiSSID.empty())
        m_wifiNetworks.push_back({m_wifiSSID,true,75});
}

// ─────────────────────────────────────────────────────────────────────────────
//  QueryBluetoothDevices — appareils Bluetooth APPAIRÉS + statut connecté.
//  Classiques (BluetoothDevice) + BLE (BluetoothLEDevice), dédupliqués, connectés
//  en tête. WinRT bloquant (.get()) — appelé ponctuellement à l'ouverture du panneau.
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::QueryBluetoothDevices()
{
    m_btDevices.clear();
    try {
        using namespace winrt::Windows::Devices::Enumeration;
        using namespace winrt::Windows::Devices::Bluetooth;
        // 1) Appareils COUPLÉS (classiques + LE) + statut connecté + id.
        auto sel = BluetoothDevice::GetDeviceSelectorFromPairingState(true);
        auto devs = DeviceInformation::FindAllAsync(sel).get();
        for (auto const& di : devs) {
            if (m_btDevices.size() >= 8) break;
            std::wstring nm(di.Name().c_str());
            if (nm.empty()) continue;
            BtDeviceItem item; item.name = nm; item.paired = true; item.id = di.Id().c_str();
            try {
                auto bt = BluetoothDevice::FromIdAsync(di.Id()).get();
                if (bt) item.connected =
                    (bt.ConnectionStatus() == BluetoothConnectionStatus::Connected);
            } catch (...) {}
            m_btDevices.push_back(item);
        }
        auto selLe = BluetoothLEDevice::GetDeviceSelectorFromPairingState(true);
        auto devsLe = DeviceInformation::FindAllAsync(selLe).get();
        for (auto const& di : devsLe) {
            if (m_btDevices.size() >= 12) break;
            std::wstring nm(di.Name().c_str());
            if (nm.empty()) continue;
            bool dup=false; for(auto&e:m_btDevices) if(e.name==nm){dup=true;break;}
            if (dup) continue;
            BtDeviceItem item; item.name = nm; item.paired = true; item.id = di.Id().c_str();
            try {
                auto le = BluetoothLEDevice::FromIdAsync(di.Id()).get();
                if (le) item.connected =
                    (le.ConnectionStatus() == BluetoothConnectionStatus::Connected);
            } catch (...) {}
            m_btDevices.push_back(item);
        }
        // 2) Appareils DISPONIBLES non couplés (snapshot récent, best-effort).
        try {
            auto selUnp = BluetoothLEDevice::GetDeviceSelectorFromPairingState(false);
            auto devsUnp = DeviceInformation::FindAllAsync(selUnp).get();
            for (auto const& di : devsUnp) {
                if (m_btDevices.size() >= 18) break;
                std::wstring nm(di.Name().c_str());
                if (nm.empty()) continue;
                bool dup=false; for(auto&e:m_btDevices) if(e.name==nm){dup=true;break;}
                if (dup) continue;
                BtDeviceItem item; item.name = nm; item.paired = false;
                item.connected = false; item.id = di.Id().c_str();
                m_btDevices.push_back(item);
            }
        } catch (...) {}
        // Tri : connectés d'abord, puis couplés, puis disponibles.
        std::sort(m_btDevices.begin(), m_btDevices.end(),
                  [](const BtDeviceItem& a, const BtDeviceItem& b){
                      if(a.connected!=b.connected) return a.connected>b.connected;
                      return a.paired>b.paired;
                  });
    } catch (...) { /* accès BT refusé → liste vide (le panneau invite via le toggle) */ }
}

// ─────────────────────────────────────────────────────────────────────────────
//  OpenBluetoothList — ouvre le panneau liste Bluetooth (clic sur le pill BT).
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::OpenBluetoothList()
{
    QuerySystemControls();                       // rafraîchit m_btEnabled (radio)
    if (m_btEnabled) QueryBluetoothDevices();    // inutile de scanner si BT off
    else             m_btDevices.clear();
    m_content.bluetoothEnabled = m_btEnabled;
    m_content.btDevices        = m_btDevices;
    m_content.btStatusMsg.clear();
    m_content.btScrollY        = 0.f;
    m_animation->StartTransition(IslandState::BluetoothList, 280);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

// ─────────────────────────────────────────────────────────────────────────────
//  OpenWifiList — ouvre le panneau liste Wi-Fi (clic sur le pill Wi-Fi), miroir BT.
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::OpenWifiList()
{
    QuerySystemControls();                       // rafraîchit m_wifiEnabled + SSID courant
    if (m_wifiEnabled) QueryWifiNetworks();
    else               m_wifiNetworks.clear();
    m_content.wifiNetworks     = m_wifiNetworks;
    m_content.wifiConnectingIdx = -1;
    m_wifiPassMode = false; m_wifiPassInput.clear(); m_wifiPassSsid.clear();
    m_content.wifiPassMode = false; m_content.wifiPassText.clear();
    m_content.wifiPassSsid.clear(); m_content.wifiStatusMsg.clear();
    m_animation->StartTransition(IslandState::WifiList, 280);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

// ─────────────────────────────────────────────────────────────────────────────
//  ConnectWifi — connexion NATIVE via WlanConnect (API privilégiée wlanapi).
//   • réseau enregistré (hasProfile) → connexion directe par profil.
//   • réseau sécurisé nouveau        → crée un profil WPA2PSK avec le mot de passe.
//   • réseau ouvert                  → crée un profil « open ».
//   WlanConnect est NON bloquant (l'OS connecte en fond) → sûr sur le thread UI.
// ─────────────────────────────────────────────────────────────────────────────
// Active/désactive la saisie du mot de passe. L'encoche étant WS_EX_NOACTIVATE
// (jamais focus clavier), on retire ce style le temps de la saisie pour recevoir
// les WM_CHAR, puis on le rétablit.
void WindowManager::SetWifiPassMode(bool on, const std::wstring& ssid)
{
    m_wifiPassMode = on;
    m_content.wifiPassMode = on;
    LONG_PTR ex = GetWindowLongPtr(m_hwnd, GWL_EXSTYLE);
    if (on) {
        m_wifiPassSsid = ssid; m_wifiPassInput.clear();
        m_content.wifiPassSsid = ssid; m_content.wifiPassText.clear();
        SetWindowLongPtr(m_hwnd, GWL_EXSTYLE, ex & ~(LONG_PTR)WS_EX_NOACTIVATE);
        SetForegroundWindow(m_hwnd); SetFocus(m_hwnd);
    } else {
        m_wifiPassInput.clear();
        m_content.wifiPassText.clear(); m_content.wifiPassSsid.clear();
        SetWindowLongPtr(m_hwnd, GWL_EXSTYLE, ex | WS_EX_NOACTIVATE);
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void WindowManager::ConnectWifi(const std::wstring& ssid, const std::wstring& password)
{
    // Retrouve les flags du réseau ciblé (sécurisé / déjà enregistré).
    bool secured=true, hasProfile=false;
    for (const auto& n : m_wifiNetworks)
        if (n.ssid == ssid) { secured=n.secured; hasProfile=n.hasProfile; break; }

    HANDLE hWlan=nullptr; DWORD ver=0;
    if (WlanOpenHandle(2,nullptr,&ver,&hWlan)!=ERROR_SUCCESS) return;
    PWLAN_INTERFACE_INFO_LIST pList=nullptr;
    if (WlanEnumInterfaces(hWlan,nullptr,&pList)==ERROR_SUCCESS && pList && pList->dwNumberOfItems>0) {
        GUID guid=pList->InterfaceInfo[0].InterfaceGuid;

        // Crée/écrase un profil uniquement si nécessaire (jamais pour un profil
        // enregistré existant, qu'on réutilise tel quel).
        if (!hasProfile) {
            std::wstring xml;
            if (secured && !password.empty()) {
                xml = L"<?xml version=\"1.0\"?>\r\n"
                    L"<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">\r\n"
                    L"<name>"+ssid+L"</name>\r\n"
                    L"<SSIDConfig><SSID><name>"+ssid+L"</name></SSID></SSIDConfig>\r\n"
                    L"<connectionType>ESS</connectionType><connectionMode>auto</connectionMode>\r\n"
                    L"<MSM><security>\r\n"
                    L"<authEncryption><authentication>WPA2PSK</authentication><encryption>AES</encryption><useOneX>false</useOneX></authEncryption>\r\n"
                    L"<sharedKey><keyType>passPhrase</keyType><protected>false</protected><keyMaterial>"+password+L"</keyMaterial></sharedKey>\r\n"
                    L"</security></MSM>\r\n</WLANProfile>";
            } else if (!secured) {
                xml = L"<?xml version=\"1.0\"?>\r\n"
                    L"<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">\r\n"
                    L"<name>"+ssid+L"</name>\r\n"
                    L"<SSIDConfig><SSID><name>"+ssid+L"</name></SSID></SSIDConfig>\r\n"
                    L"<connectionType>ESS</connectionType><connectionMode>auto</connectionMode>\r\n"
                    L"<MSM><security><authEncryption><authentication>open</authentication><encryption>none</encryption><useOneX>false</useOneX></authEncryption></security></MSM>\r\n"
                    L"</WLANProfile>";
            }
            if (!xml.empty()) {
                DWORD reason=0;
                WlanSetProfile(hWlan,&guid,0,xml.c_str(),nullptr,TRUE,nullptr,&reason);
            }
        }

        // En mode « profile », seul strProfile (= nom du profil = SSID) compte ;
        // pDot11Ssid peut rester nul (évite une conversion wstring→bytes tronquante).
        WLAN_CONNECTION_PARAMETERS cp={};
        cp.wlanConnectionMode = wlan_connection_mode_profile;
        cp.strProfile         = ssid.c_str();
        cp.pDot11Ssid         = nullptr;
        cp.dot11BssType       = dot11_BSS_type_infrastructure;
        cp.dwFlags            = 0;
        WlanConnect(hWlan,&guid,&cp,nullptr);
    }
    if (pList) WlanFreeMemory(pList);
    WlanCloseHandle(hWlan,nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
//  ConnectBluetoothDevice — best-effort in-island.
//   BLE : accéder aux services GATT INITIE la connexion (thread de fond).
//   Audio classique (A2DP/HFP) : AUCUNE API publique de connexion forcée sur
//   Windows → on tente, sinon on affiche la limite (pas d'ouverture de Réglages).
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::ConnectBluetoothDevice(const std::wstring& name)
{
    // Retrouve l'état + l'id de l'appareil ciblé.
    std::wstring id; bool connected=false, paired=true;
    for (const auto& d : m_btDevices)
        if (d.name==name) { id=d.id; connected=d.connected; paired=d.paired; break; }
    if (id.empty()) return;

    m_content.btStatusMsg = connected ? L"Déconnexion…"
                          : paired    ? L"Connexion…"
                                      : L"Couplage…";
    InvalidateRect(m_hwnd, nullptr, FALSE);
    std::thread([id, connected, paired]() {
        try {
            using namespace winrt::Windows::Devices::Enumeration;
            using namespace winrt::Windows::Devices::Bluetooth;
            if (!paired) {
                // Coupler (best-effort) — déclenche l'appairage système.
                auto di = DeviceInformation::CreateFromIdAsync(id).get();
                if (di && di.Pairing() && di.Pairing().CanPair())
                    di.Pairing().PairAsync().get();
            } else {
                auto le = BluetoothLEDevice::FromIdAsync(id).get();
                if (le) {
                    if (connected) le.Close();                       // déconnexion GATT (BLE)
                    else { auto r = le.GetGattServicesAsync().get(); (void)r; }  // connexion
                }
            }
        } catch (...) {}
    }).detach();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Actions système
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::ToggleWifi()
{
    // SOFT-toggle de la RADIO Wi-Fi (comme le panneau rapide Windows), PAS une
    // désactivation de la carte réseau (netsh disable). On mise d'abord sur WinRT
    // Radios (identique au Bluetooth), fallback natif wlanapi radio_state.
    using namespace winrt::Windows::Devices::Radios;
    bool toggled=false;
    try{
        auto access=Radio::RequestAccessAsync().get();
        if(access==RadioAccessStatus::Allowed){
            auto radios=Radio::GetRadiosAsync().get();
            for(auto const& r:radios){
                if(r.Kind()==RadioKind::WiFi){
                    RadioState target=(r.State()==RadioState::On)?RadioState::Off:RadioState::On;
                    auto st=r.SetStateAsync(target).get();
                    if(st==RadioAccessStatus::Allowed){ m_wifiEnabled=(target==RadioState::On); toggled=true; }
                    break;
                }
            }
        }
    }catch(...){}

    if(!toggled){
        // Fallback natif privilégié : software radio state (soft block), pas la carte.
        HANDLE hWlan=nullptr; DWORD ver=0;
        if(WlanOpenHandle(2,nullptr,&ver,&hWlan)==ERROR_SUCCESS){
            PWLAN_INTERFACE_INFO_LIST pList=nullptr;
            if(WlanEnumInterfaces(hWlan,nullptr,&pList)==ERROR_SUCCESS&&pList&&pList->dwNumberOfItems>0){
                WLAN_PHY_RADIO_STATE rs={};
                rs.dwPhyIndex=0;
                rs.dot11SoftwareRadioState = m_wifiEnabled?dot11_radio_state_off:dot11_radio_state_on;
                WlanSetInterface(hWlan,&pList->InterfaceInfo[0].InterfaceGuid,
                                 wlan_intf_opcode_radio_state,sizeof(rs),&rs,nullptr);
            }
            if(pList) WlanFreeMemory(pList);
            WlanCloseHandle(hWlan,nullptr);
        }
    }
    Sleep(300);
    QuerySystemControls();
}

// Vrai toggle Bluetooth via Windows.Devices.Radios (PAS d'ouverture de Paramètres).
// La requête d'accès doit avoir été accordée (CapabilityAccessStatus::Allowed) ;
// sinon on retombe sur les Paramètres pour que l'utilisateur l'autorise.
void WindowManager::ToggleBluetooth()
{
    using namespace winrt::Windows::Devices::Radios;
    using namespace winrt::Windows::Foundation;
    bool toggled = false;
    try {
        auto access = Radio::RequestAccessAsync().get();
        if (access == RadioAccessStatus::Allowed) {
            auto radios = Radio::GetRadiosAsync().get();
            for (auto const& r : radios) {
                if (r.Kind() == RadioKind::Bluetooth) {
                    RadioState target = (r.State() == RadioState::On)
                                        ? RadioState::Off : RadioState::On;
                    auto st = r.SetStateAsync(target).get();
                    if (st == RadioAccessStatus::Allowed) {
                        m_btEnabled = (target == RadioState::On);
                        m_content.bluetoothEnabled = m_btEnabled;
                        toggled = true;
                    }
                    break;
                }
            }
        }
    } catch (...) { /* fallback ci-dessous */ }

    if (!toggled) {
        // Pas autorisé ou pas de radio BT → ouvrir Paramètres (comportement précédent)
        ShellExecute(nullptr,L"open",L"ms-settings:bluetooth",nullptr,nullptr,SW_SHOWNORMAL);
        Sleep(400); QuerySystemControls();
    } else {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void WindowManager::ToggleDarkMode()
{
    HKEY hKey=nullptr;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0,KEY_SET_VALUE,&hKey)==ERROR_SUCCESS){
        DWORD val=m_darkMode?1:0;
        RegSetValueExW(hKey,L"AppsUseLightTheme",  0,REG_DWORD,(LPBYTE)&val,sizeof(val));
        RegSetValueExW(hKey,L"SystemUsesLightTheme",0,REG_DWORD,(LPBYTE)&val,sizeof(val));
        RegCloseKey(hKey);
        SendMessageTimeoutW(HWND_BROADCAST,WM_SETTINGCHANGE,0,
            (LPARAM)L"ImmersiveColorSet",SMTO_ABORTIFHUNG,500,nullptr);
        m_darkMode=!m_darkMode; m_content.darkModeEnabled=m_darkMode;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  ToggleNightLight — vrai bascule via écriture du blob binaire du registre.
//  Aucune API publique n'existe → on modifie le blob « Data » de
//  HKCU\...\bluelightreduction.settings\Current : octet[23] contient l'état
//  d'activation (0x00=off, 0x10..0x15=on selon le build) et octet[24] = timestamp
//  byte ; on incrémente le timestamp pour forcer le service à reconsidérer.
//  Fallback → ouverture des Paramètres si l'écriture échoue.
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::ToggleNightLight()
{
    static const wchar_t* KEY =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\CloudStore\\Store\\"
        L"DefaultAccount\\Current\\default$windows.data.bluelightreduction."
        L"bluelightreductionstate\\Current";

    HKEY hKey = nullptr; bool ok = false;
    if(RegOpenKeyExW(HKEY_CURRENT_USER, KEY, 0,
                     KEY_READ|KEY_WRITE, &hKey) == ERROR_SUCCESS)
    {
        DWORD sz = 0, type = 0;
        RegQueryValueExW(hKey, L"Data", nullptr, &type, nullptr, &sz);
        if(sz > 26 && type == REG_BINARY){
            std::vector<BYTE> data(sz);
            if(RegQueryValueExW(hKey, L"Data", nullptr, nullptr,
                                data.data(), &sz) == ERROR_SUCCESS){
                bool wasOn = (data[18]==0x10 || data[18]==0x13 || data[18]==0x15);
                if(wasOn){
                    // Passage OFF : retire les 2 octets marqueurs après l'octet 22
                    // (structure quand l'éclairage nocturne est activé)
                    if(sz >= 43 && data[23]==0x10 && data[24]==0x00){
                        data.erase(data.begin()+23, data.begin()+25);
                        data[18] = 0x11;   // état inactif
                    } else {
                        data[18] = 0x11;
                    }
                } else {
                    // Passage ON : insère les 2 octets marqueurs
                    data[18] = 0x15;
                    if(sz < 43 || data[23]!=0x10 || data[24]!=0x00){
                        BYTE marker[2] = {0x10, 0x00};
                        data.insert(data.begin()+23, marker, marker+2);
                    }
                }
                // Timestamp (octets 10..15 en little-endian FILETIME/100ns) — on
                // ajoute une seconde pour forcer la prise en compte
                DWORD now = GetTickCount();
                data[10] ^= (BYTE)(now & 0xFF);
                if(RegSetValueExW(hKey, L"Data", 0, REG_BINARY,
                                  data.data(), (DWORD)data.size()) == ERROR_SUCCESS){
                    ok = true;
                    m_nightLight = !wasOn;
                    m_content.nightLightEnabled = m_nightLight;
                }
            }
        }
        RegCloseKey(hKey);
    }
    if(!ok){
        // Écriture bloquée (droits/format) → fallback Paramètres
        ShellExecute(nullptr,L"open",L"ms-settings:nightlight",
                     nullptr,nullptr,SW_SHOWNORMAL);
        Sleep(300);
    }
    QuerySystemControls();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void WindowManager::SetSystemVolume(float v)
{
    v=std::clamp(v,0.f,1.f);
    IMMDeviceEnumerator* pEnum=nullptr;
    if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),(void**)&pEnum))&&pEnum){
        IMMDevice* pDev=nullptr;
        if(SUCCEEDED(pEnum->GetDefaultAudioEndpoint(eRender,eConsole,&pDev))&&pDev){
            IAudioEndpointVolume* pVol=nullptr;
            if(SUCCEEDED(pDev->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,
                                        nullptr,(void**)&pVol))&&pVol){
                pVol->SetMasterVolumeLevelScalar(v,nullptr);
                pVol->Release();
            }
            pDev->Release();
        }
        pEnum->Release();
    }
    m_sysVolume=v; m_content.systemVolume=v;
}

void WindowManager::SetSystemBrightness(float v)
{
    v=std::clamp(v,0.f,1.f);
    bool done=false;
    HMONITOR hMon=MonitorFromWindow(m_hwnd,MONITOR_DEFAULTTOPRIMARY);
    DWORD numPhys=0;
    if(GetNumberOfPhysicalMonitorsFromHMONITOR(hMon,&numPhys)&&numPhys>0){
        std::vector<PHYSICAL_MONITOR> pm(numPhys);
        if(GetPhysicalMonitorsFromHMONITOR(hMon,numPhys,pm.data())){
            DWORD minB=0,curB=0,maxB=100;
            if(GetMonitorBrightness(pm[0].hPhysicalMonitor,&minB,&curB,&maxB)&&maxB>minB){
                SetMonitorBrightness(pm[0].hPhysicalMonitor,(DWORD)(minB+v*(maxB-minB)));
                done=true;
            }
            DestroyPhysicalMonitors(numPhys,pm.data());
        }
    }
    if(!done){
        // Fallback laptop : WMI DIRECT (plus aucun cmd PowerShell qui spawn de process).
        // root\WMI :: WmiMonitorBrightnessMethods :: WmiSetBrightness(Timeout=1, Brightness=v*100)
        int pct = std::clamp((int)(v*100.f), 0, 100);
        IWbemLocator* loc = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&loc))) && loc) {
            IWbemServices* svc = nullptr;
            BSTR ns = SysAllocString(L"root\\WMI");
            if (SUCCEEDED(loc->ConnectServer(ns, nullptr, nullptr, nullptr,
                                             WBEM_FLAG_CONNECT_USE_MAX_WAIT,
                                             nullptr, nullptr, &svc)) && svc) {
                CoSetProxyBlanket(svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                                  RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                                  nullptr, EOAC_NONE);
                BSTR q = SysAllocString(L"SELECT * FROM WmiMonitorBrightnessMethods");
                BSTR wql = SysAllocString(L"WQL");
                IEnumWbemClassObject* en = nullptr;
                if (SUCCEEDED(svc->ExecQuery(wql, q,
                        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                        nullptr, &en)) && en) {
                    IWbemClassObject* obj = nullptr; ULONG ret = 0;
                    while (en->Next(WBEM_INFINITE, 1, &obj, &ret) == S_OK && obj) {
                        VARIANT path; VariantInit(&path);
                        if (SUCCEEDED(obj->Get(L"__PATH", 0, &path, nullptr, nullptr))) {
                            IWbemClassObject* inSig = nullptr;
                            IWbemClassObject* cls = nullptr;
                            BSTR clsName = SysAllocString(L"WmiMonitorBrightnessMethods");
                            BSTR method  = SysAllocString(L"WmiSetBrightness");
                            if (SUCCEEDED(svc->GetObject(clsName, 0, nullptr, &cls, nullptr)) && cls) {
                                cls->GetMethod(method, 0, &inSig, nullptr);
                                cls->Release();
                            }
                            if (inSig) {
                                IWbemClassObject* params = nullptr;
                                inSig->SpawnInstance(0, &params);
                                if (params) {
                                    VARIANT vTo; VariantInit(&vTo); vTo.vt=VT_BSTR;
                                    vTo.bstrVal = SysAllocString(L"1");
                                    params->Put(L"Timeout", 0, &vTo, 0);
                                    VariantClear(&vTo);
                                    VARIANT vB; VariantInit(&vB); vB.vt=VT_UI1;
                                    vB.bVal = (BYTE)pct;
                                    params->Put(L"Brightness", 0, &vB, 0);
                                    svc->ExecMethod(path.bstrVal, method, 0, nullptr,
                                                    params, nullptr, nullptr);
                                    params->Release();
                                    done = true;
                                }
                                inSig->Release();
                            }
                            SysFreeString(method);
                            SysFreeString(clsName);
                        }
                        VariantClear(&path);
                        obj->Release(); obj = nullptr;
                        if (done) break;
                    }
                    en->Release();
                }
                SysFreeString(wql); SysFreeString(q);
                svc->Release();
            }
            SysFreeString(ns);
            loc->Release();
        }
    }
    m_sysBrightness=v; m_content.systemBrightness=v;
}

// ─────────────────────────────────────────────────────────────────────────────
//  ShowAudioDevicesMenu
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::ShowAudioDevicesMenu()
{
    IMMDeviceEnumerator* pEnum=nullptr;
    if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,
                               __uuidof(IMMDeviceEnumerator),(void**)&pEnum))||!pEnum) return;

    IMMDevice* pDefault=nullptr; LPWSTR defaultId=nullptr;
    if(SUCCEEDED(pEnum->GetDefaultAudioEndpoint(eRender,eConsole,&pDefault))&&pDefault){
        pDefault->GetId(&defaultId); pDefault->Release();
    }

    IMMDeviceCollection* pColl=nullptr;
    pEnum->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&pColl);
    struct DevInfo{ std::wstring id,name; };
    std::vector<DevInfo> devices;
    if(pColl){
        UINT count=0; pColl->GetCount(&count);
        for(UINT i=0;i<count&&i<16;++i){
            IMMDevice* pDev=nullptr;
            if(FAILED(pColl->Item(i,&pDev))||!pDev) continue;
            LPWSTR devId=nullptr; pDev->GetId(&devId);
            std::wstring name;
            IPropertyStore* pProps=nullptr;
            if(SUCCEEDED(pDev->OpenPropertyStore(STGM_READ,&pProps))&&pProps){
                PROPVARIANT var; PropVariantInit(&var);
                if(SUCCEEDED(pProps->GetValue(PKEY_Device_FriendlyName,&var))&&var.vt==VT_LPWSTR)
                    name=var.pwszVal;
                PropVariantClear(&var); pProps->Release();
            }
            if(!name.empty()&&devId) devices.push_back({devId,name});
            if(devId) CoTaskMemFree(devId);
            pDev->Release();
        }
        pColl->Release();
    }
    pEnum->Release();

    if(devices.empty()){
        if(defaultId) CoTaskMemFree(defaultId);
        ShellExecute(nullptr,L"open",L"ms-settings:sound",nullptr,nullptr,SW_SHOWNORMAL);
        return;
    }

    HMENU menu=CreatePopupMenu();
    for(int i=0;i<(int)devices.size();++i){
        bool isDefault=defaultId&&(devices[i].id==defaultId);
        AppendMenuW(menu,MF_STRING|(isDefault?MF_CHECKED:0),(UINT_PTR)(i+1),devices[i].name.c_str());
    }
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING,999,L"Param\u00E8tres son\u2026");
    if(defaultId) CoTaskMemFree(defaultId);

    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(m_hwnd);
    int sel=(int)TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON|TPM_NONOTIFY,
                                pt.x,pt.y,0,m_hwnd,nullptr);
    DestroyMenu(menu);

    if(sel==999){ ShellExecute(nullptr,L"open",L"ms-settings:sound",nullptr,nullptr,SW_SHOWNORMAL); return; }
    if(sel<1||sel>(int)devices.size()) return;

    IPolicyConfig* pPolicy=nullptr;
    if(SUCCEEDED(CoCreateInstance(CLSID_PolicyConfig,nullptr,CLSCTX_ALL,
                                  __uuidof(IPolicyConfig),(void**)&pPolicy))&&pPolicy){
        const std::wstring& devId=devices[sel-1].id;
        pPolicy->SetDefaultEndpoint(devId.c_str(),eConsole);
        pPolicy->SetDefaultEndpoint(devId.c_str(),eMultimedia);
        pPolicy->SetDefaultEndpoint(devId.c_str(),eCommunications);
        pPolicy->Release();
    } else {
        ShellExecute(nullptr,L"open",L"ms-settings:sound",nullptr,nullptr,SW_SHOWNORMAL);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  WindowProc
// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK WindowManager::WindowProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)
{
    WindowManager* self=nullptr;
    if(msg==WM_NCCREATE){
        auto* cs=reinterpret_cast<CREATESTRUCT*>(lp);
        self=reinterpret_cast<WindowManager*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd,GWLP_USERDATA,(LONG_PTR)self);
        self->m_hwnd=hwnd;
    } else {
        self=reinterpret_cast<WindowManager*>(GetWindowLongPtr(hwnd,GWLP_USERDATA));
    }
    if(!self) return DefWindowProc(hwnd,msg,wp,lp);

    if(self->m_tray&&self->m_tray->HandleMessage(hwnd,msg,wp,lp)) return 0;

    switch(msg){
    case WM_DESTROY:
        KillTimer(hwnd,TIMER_ANIM); KillTimer(hwnd,TIMER_CLOCK);
        KillTimer(hwnd,TIMER_CHECK); KillTimer(hwnd,TIMER_LONGPRESS);
        PostQuitMessage(0);
        return 0;

    // ── P0-FIX : traitement des mises à jour thread-safe ──────────────────
    case WM_ISLAND_UPDATE:
        self->DrainUpdateQueue();
        return 0;

    case WM_ISLAND_RELOADCFG:      // « Appliquer » du Cockpit
        self->ReloadConfig();
        return 0;
    case WM_ISLAND_SPOTIFY:        // connexion Spotify demandée par le Cockpit
        self->ConnectSpotify();
        return 0;
    case WM_ISLAND_AMBIENT:        // ouverture du mode ambiant
        // wParam=1 : déclenchement AUTO au déverrouillage — l'ambiant « prolonge »
        // l'écran de verrouillage (on ne peut PAS dessiner sur le vrai lock screen,
        // bureau sécurisé Winlogon ; ceci est l'approche maximale possible).
        if(wp == 1){
            if(self->m_appCfg && self->m_appCfg->ambientOnUnlock
               && self->m_content.isMusicPlaying && !self->m_ambient.IsOpen())
                self->ToggleAmbient();
        } else if(!self->m_ambient.IsOpen()){
            self->ToggleAmbient();
        }
        return 0;

    case WM_ISLAND_SPOTIFY_LOGOUT: // déconnexion (Cockpit)
        self->m_spotify.Logout();
        self->m_spotifyQueue.clear();
        self->m_content.upcomingTracks.clear();
        self->TriggerNotification(L"spotify", L"Spotify", L"Déconnecté");
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_DEVICECHANGE:          // appareil branché → son « Minimal »
        if(wp==DBT_DEVICEARRIVAL && self->m_appCfg && self->m_appCfg->soundDevice)
            Sounds::PlayDevice(self->m_appCfg->volDevice, self->m_appCfg->soundDeviceVar);
        return TRUE;

    case WM_HOTKEY:                // Ctrl+Alt+L → mode ambiant
        if(wp==1) self->ToggleAmbient();
        return 0;

    case WM_DPICHANGED:            // changement de DPI (autre moniteur, réglage)
        self->m_dpiScale = LOWORD(wp) / 96.f;
        if(self->m_renderer) self->m_renderer->SetDpi(self->m_dpiScale * 96.f);
        // Recalcule la région (fenêtre en pixels physiques)
        self->UpdateWindowRegion();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_TIMER:
        if     (wp==TIMER_ANIM)  self->OnAnimTick();
        else if(wp==TIMER_CLOCK) self->UpdateClock();
        else if(wp==TIMER_CHECK) self->CheckDismiss();
        else if(wp==TIMER_LONGPRESS){
            KillTimer(hwnd,TIMER_LONGPRESS);
            self->OnLongPress();
        }
        return 0;

    case WM_PAINT:      self->OnPaint();  return 0;
    case WM_ERASEBKGND: return 1;

    case WM_SIZE:
        self->m_renderer->Resize(LOWORD(lp),HIWORD(lp));
        return 0;

    case WM_MOUSEMOVE:
        // Coords souris (physiques) → unités LOGIQUES pour matcher le dessin.
        self->OnMouseMove((int)(GET_X_LPARAM(lp) / self->m_dpiScale),
                          (int)(GET_Y_LPARAM(lp) / self->m_dpiScale));
        return 0;
    case WM_MOUSELEAVE:
        self->OnMouseLeave();
        return 0;
    case WM_LBUTTONDOWN:
        self->OnMouseDown((int)(GET_X_LPARAM(lp) / self->m_dpiScale),
                          (int)(GET_Y_LPARAM(lp) / self->m_dpiScale));
        return 0;
    case WM_LBUTTONUP:
        self->OnMouseUp((int)(GET_X_LPARAM(lp) / self->m_dpiScale),
                        (int)(GET_Y_LPARAM(lp) / self->m_dpiScale));
        return 0;
    case WM_MOUSEWHEEL:
        // (Win10/11 route la molette vers la fenêtre survolée par défaut,
        //  même sans focus — compatible avec WS_EX_NOACTIVATE.)
        self->OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wp));
        return 0;

    // Saisie du mot de passe Wi-Fi in-island (actif seulement en mode saisie).
    case WM_CHAR:
        if (self->m_wifiPassMode) {
            wchar_t ch = (wchar_t)wp;
            if (ch == L'\r') {                      // Entrée → connecter
                self->ConnectWifi(self->m_wifiPassSsid, self->m_wifiPassInput);
                self->SetWifiPassMode(false);
                self->m_content.wifiStatusMsg = L"Connexion…";
            } else if (ch == 0x1B) {                // Échap → annuler
                self->SetWifiPassMode(false);
            } else if (ch == L'\b') {               // Backspace
                if (!self->m_wifiPassInput.empty()) self->m_wifiPassInput.pop_back();
            } else if (ch >= 32) {                  // caractère imprimable
                self->m_wifiPassInput.push_back(ch);
            }
            self->m_content.wifiPassText.assign(self->m_wifiPassInput.size(), L'•');  // •
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        return DefWindowProc(hwnd, msg, wp, lp);

    case WM_GESTURE: {
        GESTUREINFO gi={sizeof(GESTUREINFO)};
        if(GetGestureInfo((HGESTUREINFO)lp,&gi)){
            if(gi.dwID==GID_PAN)
                self->OnGesturePan(gi.ptsLocation.x, gi.dwFlags);
            CloseGestureInfoHandle((HGESTUREINFO)lp);
        }
        return DefWindowProc(hwnd,msg,wp,lp);
    }

    case WM_NCHITTEST: {
        POINT pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd,&pt);
        // NCHITTEST reçoit des coords PHYSIQUES → passer en LOGIQUES
        pt.x = (LONG)(pt.x / self->m_dpiScale);
        pt.y = (LONG)(pt.y / self->m_dpiScale);
        float pw=self->m_content.pillW;
        RECT rc; GetClientRect(hwnd,&rc);
        // ClientRect en PIXELS PHYSIQUES → convertir en LOGIQUES
        float logicalRight = rc.right / self->m_dpiScale;
        float pillLeft =PillX(logicalRight, pw);
        float pillRight=pillLeft+pw;
        // Tolérance 1 px (était 10 → la pilule « réagissait » 10 px avant que le
        // curseur ne la touche réellement → sensation que ça « s'ouvre tout seul »).
        if(pt.x>=pillLeft-1.f&&pt.x<=pillRight+1.f&&pt.y>=0&&pt.y<=rc.bottom)
            return HTCLIENT;
        return HTTRANSPARENT;
    }

    default: return DefWindowProc(hwnd,msg,wp,lp);
    }
}
