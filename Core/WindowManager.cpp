#include "WindowManager.h"
#include "../Modules/SystemMonitor.h"
#include "../Modules/MediaManager.h"
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

void WindowManager::Initialize(HINSTANCE hi,int nCmdShow,SystemMonitor* sys)
{
    m_hInstance=hi; m_sysMonitor=sys;
    m_currentW=(int)Pill::W_IDLE;
    m_currentH=(int)Pill::H_IDLE;
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

    m_hwnd=CreateWindowEx(
        WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,
        L"UltraislandClass",L"Ultraisland",WS_POPUP,
        0,TOP_MARGIN,m_currentW,m_currentH,
        nullptr,nullptr,hi,this);
    if(!m_hwnd) throw std::runtime_error("CreateWindowEx failed");

    SetLayeredWindowAttributes(m_hwnd,0,255,LWA_ALPHA);
    DWMNCRENDERINGPOLICY pol=DWMNCRP_DISABLED;
    DwmSetWindowAttribute(m_hwnd,DWMWA_NCRENDERING_POLICY,&pol,sizeof(pol));
    MARGINS mg={-1,-1,-1,-1};
    DwmExtendFrameIntoClientArea(m_hwnd,&mg);

    if(!m_renderer->Initialize(m_hwnd))
        throw std::runtime_error("Renderer::Initialize failed");

    m_animation->SetScreenWidth(m_screenW);

    // ── Activer les gestes touchpad (WM_GESTURE) ─────────────────────────────
    GESTURECONFIG gc[]={
        {GID_PAN,     GC_PAN,     0},
        {GID_ZOOM,    0,          GC_ZOOM},
        {GID_ROTATE,  0,          GC_ROTATE},
        {GID_TWOFINGERTAP, 0,     GC_TWOFINGERTAP},
        {GID_PRESSANDTAP,  0,     GC_PRESSANDTAP},
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

        if((int)nw!=m_currentW||(int)nh!=m_currentH){
            m_currentW=(int)nw; m_currentH=(int)nh;
            RepositionWindow();
            m_renderer->Resize(nw,nh);
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

    SetTimer(m_hwnd,TIMER_ANIM,  16,  nullptr);
    SetTimer(m_hwnd,TIMER_CLOCK, 1000,nullptr);
    SetTimer(m_hwnd,TIMER_CHECK, 250, nullptr);
    
    // Initialisation forcée des dimensions pour éviter le "recalcul" visuel au démarrage
    m_content.pillW = Pill::W_IDLE;
    m_content.pillH = Pill::H_IDLE;
    m_content.cornerRadius = Pill::CR_IDLE;
    m_content.state = IslandState::Idle;
    m_content.animT = 1.0f;
    
    UpdateClock();
    UpdateSystemStats(); 
    UpdateWindowRegion();

    ShowWindow(m_hwnd,nCmdShow);
    UpdateWindow(m_hwnd);
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
    const int MARGIN=20; // Plus large pour les oreilles (ears)
    int pLeft  = (m_currentW-(int)pw)/2 - MARGIN;
    int pRight = pLeft + (int)pw + MARGIN*2;
    
    if(pLeft<0) pLeft=0;
    if(pRight>m_currentW) pRight=m_currentW;

    // On utilise un simple rectangle pour la région de la fenêtre, 
    // la forme réelle est définie par le dessin D2D et le HitTest.
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

void WindowManager::UpdateSystemStats()
{
    if(!m_sysMonitor) return;
    SystemStats s=m_sysMonitor->GetStats();
    m_content.cpuPercent=s.cpuPercent; m_content.ramPercent=s.ramPercent;
    m_content.ramUsedGB=s.ramUsedGB;   m_content.ramTotalGB=s.ramTotalGB;
    m_content.netDownKBps=s.netDownKBps; m_content.netUpKBps=s.netUpKBps;
    m_content.batteryPercent=s.batteryPercent;
    m_content.batteryCharging=s.batteryCharging;
}

void WindowManager::CheckDismiss()
{
    if(!m_notifDismissAt) return;
    if(m_hoverExpanded) return;
    if(GetTickCount()<m_notifDismissAt) return;
    m_notifDismissAt=0;
    if(m_preNotifState==IslandState::MusicExpanded)
        m_animation->StartTransition(IslandState::MusicExpanded,280);
    else
        m_animation->StartTransition(IslandState::Idle,220);
}

void WindowManager::SetContent(const IslandContent& c){m_content=c;}

void WindowManager::TriggerMusic(const std::wstring& ti,const std::wstring& ar,
                                  const std::wstring& srcApp,bool playing,float prog,
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

    m_content.musicTitle    =ti;
    m_content.musicArtist   =ar;
    m_content.musicSourceApp=srcApp;
    m_content.isMusicPlaying=playing;
    m_content.musicProgress =prog;
    m_content.musicTotalSec =210.f;
    m_content.musicCurrentSec=prog*210.f;
    m_notifDismissAt=0;

    if(m_renderer){
        m_renderer->UpdateAlbumArt(thumbnailData);
        m_content.albumArtBitmap=m_renderer->GetAlbumArt();
    }

    if(m_animation->GetDisplayState()==IslandState::Idle && m_content.isHovered){
        m_animation->StartTransition(IslandState::MusicExpanded,400);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  ResolveAppStyle — couleurs de marque exactes + icônes Segoe MDL2 Assets
// ─────────────────────────────────────────────────────────────────────────────
static void ResolveAppStyle(const std::wstring& rawApp,
                             D2D1_COLOR_F& color,
                             std::wstring& glyph,
                             std::wstring& displayName)
{
    std::wstring low = rawApp;
    for (auto& c : low) c = towlower(c);

    struct AppEntry {
        const wchar_t* key;
        const wchar_t* name;
        const wchar_t* icon;
        float r, g, b;
    };

    static const AppEntry APPS[] = {
        // ── Messagerie ────────────────────────────────────────────────────────
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
        // ── Email ────────────────────────────────────────────────────────────
        {L"outlook",     L"Outlook",         L"\uE715", 0.000f,0.471f,0.831f},
        {L"gmail",       L"Gmail",           L"\uE715", 0.918f,0.263f,0.208f},
        {L"mail",        L"Courrier",        L"\uE715", 0.000f,0.471f,0.831f},
        {L"thunderbird", L"Thunderbird",     L"\uE715", 0.016f,0.478f,0.706f},
        // ── Réseaux sociaux ───────────────────────────────────────────────────
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
        // ── Musique / Média ───────────────────────────────────────────────────
        {L"spotify",     L"Spotify",         L"\uEC4F", 0.114f,0.722f,0.329f},
        {L"deezer",      L"Deezer",          L"\uEC4F", 1.000f,0.435f,0.098f},
        {L"soundcloud",  L"SoundCloud",      L"\uEC4F", 1.000f,0.341f,0.000f},
        {L"apple music", L"Apple Music",     L"\uEC4F", 0.984f,0.173f,0.439f},
        {L"vlc",         L"VLC",             L"\uE714", 1.000f,0.667f,0.000f},
        {L"netflix",     L"Netflix",         L"\uE714", 0.898f,0.035f,0.078f},
        // ── Productivité / Outils ────────────────────────────────────────────
        {L"notion",      L"Notion",          L"\uE8A5", 0.960f,0.961f,0.965f},
        {L"obsidian",    L"Obsidian",        L"\uE8A5", 0.431f,0.247f,0.957f},
        {L"todoist",     L"Todoist",         L"\uE762", 0.886f,0.173f,0.176f},
        {L"trello",      L"Trello",          L"\uE8A5", 0.000f,0.502f,0.800f},
        {L"jira",        L"Jira",            L"\uE8A5", 0.000f,0.329f,0.933f},
        {L"github",      L"GitHub",          L"\uE8FB", 0.882f,0.882f,0.906f},
        {L"vscode",      L"VS Code",         L"\uE8FB", 0.000f,0.478f,0.847f},
        {L"code",        L"VS Code",         L"\uE8FB", 0.000f,0.478f,0.847f},
        // ── Gaming ───────────────────────────────────────────────────────────
        {L"steam",       L"Steam",           L"\uE7FC", 0.110f,0.255f,0.435f},
        {L"epic",        L"Epic Games",      L"\uE7FC", 0.933f,0.933f,0.933f},
        {L"battle.net",  L"Battle.net",      L"\uE7FC", 0.000f,0.427f,0.855f},
        {L"xbox",        L"Xbox",            L"\uE7FC", 0.039f,0.525f,0.047f},
        {L"ubisoft",     L"Ubisoft",         L"\uE7FC", 0.000f,0.627f,0.510f},
        // ── Système Windows ───────────────────────────────────────────────────
        {L"windows",     L"Windows",         L"\uE80F", 0.000f,0.471f,0.831f},
        {L"update",      L"Windows Update",  L"\uE777", 0.000f,0.471f,0.831f},
        {L"defender",    L"Sécurité",        L"\uEA18", 0.063f,0.773f,0.322f},
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

    for (const auto& e : APPS) {
        if (low.find(e.key) != std::wstring::npos) {
            color       = {e.r, e.g, e.b, 1.0f};
            glyph       = e.icon;
            displayName = e.name;
            return;
        }
    }

    color       = {0.52f, 0.52f, 0.56f, 1.0f};
    glyph       = L"\uEA8F";
    displayName = rawApp.empty() ? L"Notification" : rawApp;
}

void WindowManager::TriggerNotification(const std::wstring& app,
                                         const std::wstring& title,
                                         const std::wstring& msg)
{
    m_preNotifState = IslandState::Idle;
    m_content.notifTitle   = title;
    m_content.notifMessage = msg;

    std::wstring displayName;
    ResolveAppStyle(app,
                    m_content.notifAppColor,
                    m_content.notifIconGlyph,
                    displayName);
    m_content.notifAppName = displayName;

    m_content.bellShakeT = 0.001f;
    m_notifDismissAt = GetTickCount() + NOTIF_MS;
    m_animation->StartTransition(IslandState::NotifExpanded, 250);

    NotifHistoryItem hi;
    hi.appName   = displayName;
    hi.message   = msg.empty() ? title : msg;
    hi.iconGlyph = m_content.notifIconGlyph;
    hi.appColor  = m_content.notifAppColor;
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t tbuf[12]; swprintf_s(tbuf, L"%02d:%02d", st.wHour, st.wMinute);
    hi.timeStr = tbuf;
    m_notifHistory.push_back(hi);
    if (m_notifHistory.size() > 20) m_notifHistory.erase(m_notifHistory.begin());
    m_content.notifHistory = m_notifHistory;

    // ── App Badge ────────────────────────────────────────────────────────────
    auto& badge = m_appBadges[displayName];
    badge.appName   = displayName;
    badge.color     = m_content.notifAppColor;
    badge.iconGlyph = m_content.notifIconGlyph;
    badge.count++;
    m_badgePulseT = 0.f;
    m_content.badgePulseT = 0.f;
}

void WindowManager::TriggerHUD(HUDType type,float val,const std::wstring& lbl)
{
    m_preNotifState = IslandState::Idle;
    m_content.hudType=type; m_content.hudValue=val; m_content.hudLabel=lbl;
    IslandState hs=(type==HUDType::Volume)?IslandState::HUDVolume:
                   (type==HUDType::Brightness)?IslandState::HUDBrightness:
                                               IslandState::HUDNetwork;
    m_notifDismissAt = GetTickCount() + HUD_MS;
    m_animation->StartTransition(hs,220);
}

void WindowManager::TriggerSystem()
{
    UpdateSystemStats();
    QuerySystemControls();
    m_hoverExpanded = true;
    m_animation->StartTransition(IslandState::SystemExpanded,300);
}

void WindowManager::Collapse()
{
    m_hoverExpanded=false;
    m_animation->StartTransition(IslandState::Idle,260);
}

void WindowManager::UpdateMusicProgress(float p)
{
    m_content.musicProgress=p;
    m_content.musicCurrentSec=p*m_content.musicTotalSec;
}

// ─────────────────────────────────────────────────────────────────────────────
//  TransitionToNotifList — hauteur adaptative
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::TransitionToNotifList()
{
    m_hoverExpanded = true;
    int cnt=(int)m_notifHistory.size();
    float h=ComputeNotifListHeight(cnt);
    m_animation->StartTransitionCustom(
        IslandState::NotifList,
        Pill::W_NOTIF_LIST, h, Pill::CR_NOTIF_LIST, 300);
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
        m_animation->StartTransition(m_showQueue?IslandState::MusicQueue:IslandState::MusicExpanded,280);
    } else {
        QuerySystemControls();
        m_animation->StartTransition(IslandState::SystemExpanded,280);
    }
    InvalidateRect(m_hwnd,nullptr,FALSE);
}

// ─────────────────────────────────────────────────────────────────────────────
//  OnAnimTick
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::OnAnimTick()
{
    m_animation->Tick();
    bool needRedraw=false;

    if(m_animation->GetDisplayState()==IslandState::MusicExpanded||
       m_animation->GetDisplayState()==IslandState::MusicQueue||
       m_animation->GetDisplayState()==IslandState::Idle){
        if(m_content.isMusicPlaying){ m_content.globalT+=0.016f; needRedraw=true; }
    }

    if(m_content.bellShakeT>0.f){
        m_content.bellShakeT+=0.016f;
        if(m_content.bellShakeT>1.0f) m_content.bellShakeT=0.f;
        needRedraw=true;
    }

    UpdateSystemStats(); // Toujours mettre à jour les stats (batterie, etc.) à chaque tick
    needRedraw = true;

    // ── Slide animation entre onglets ─────────────────────────────────────────
    if(m_tabSlideT<1.f){
        DWORD elapsed=GetTickCount()-m_tabSlideStart;
        m_tabSlideT=std::min(1.f,(float)elapsed/m_tabSlideDuration);
        float ease=LocalCubicEase(m_tabSlideT);
        float pw=m_content.pillW;
        m_content.tabSlideX=m_tabSlideDir*pw*(1.f-ease);
        needRedraw=true;
    } else {
        m_content.tabSlideX=0.f;
    }

    // ── App Badges — animation pulse (0→1 en 400ms) ───────────────────────────
    if(m_badgePulseT < 1.f){
        m_badgePulseT = std::min(1.f, m_badgePulseT + 0.016f / 0.4f);
        needRedraw = true;
    }
    m_content.badgePulseT = m_badgePulseT;

    {
        m_content.appBadges.clear();
        m_content.totalUnreadCount = 0;
        for(auto& kv : m_appBadges){
            m_content.appBadges.push_back(kv.second);
            m_content.totalUnreadCount += kv.second.count;
        }
        std::sort(m_content.appBadges.begin(), m_content.appBadges.end(),
                  [](const AppBadge& a, const AppBadge& b){
                      return a.count > b.count;
                  });
    }

    if(m_animation->GetDisplayState()==IslandState::SystemExpanded)
        UpdateSystemStats();
    if(m_animation->GetDisplayState()==IslandState::NotifList)
        m_content.notifHistory=m_notifHistory;

    if(m_hoverExpanded){
        POINT pt; GetCursorPos(&pt); ScreenToClient(m_hwnd,&pt);
        RECT rc; GetClientRect(m_hwnd,&rc);
        float pw=m_content.pillW;
        float pillLeft=(rc.right-pw)*.5f, pillRight=pillLeft+pw;
        
        // Marge de sortie augmentée (hystérésis) pour éviter les tremblements (12px au lieu de 4px)
        const float EXIT_MARGIN = 12.f;
        if(pt.x < pillLeft - EXIT_MARGIN || pt.x > pillRight + EXIT_MARGIN || pt.y < -EXIT_MARGIN || pt.y > rc.bottom + EXIT_MARGIN){
            m_hoverExpanded=false;
            m_content.isHovered=false;
            m_mouseTracking=false;
            m_animation->StartTransition(IslandState::Idle,260);
            needRedraw=true;
        }
    }

    if(needRedraw) InvalidateRect(m_hwnd,nullptr,FALSE);
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

    if(m_dragSlider){
        float pw2=m_content.pillW, px2=(m_screenW-pw2)*.5f;
        float pad=13.f;
        float sx=px2+pad, sw=pw2-pad*2.f-26.f-8.f;
        float v=std::clamp((float)(x-(int)sx)/sw,0.f,1.f);
        if(m_dragSliderIdx==0)      SetSystemVolume(v);
        else if(m_dragSliderIdx==1) SetSystemBrightness(v);
        InvalidateRect(m_hwnd,nullptr,FALSE);
        return;
    }

    if(!m_content.isHovered){
        m_content.isHovered=true;
        if(m_animation->GetDisplayState()==IslandState::Idle){
            m_hoverExpanded=true;
            if(m_activeMenu==0){ QuerySystemControls(); TransitionToNotifList(); }
            else if(m_activeMenu==2){ QuerySystemControls(); m_animation->StartTransition(IslandState::SystemExpanded,300); }
            else m_animation->StartTransition(m_showQueue?IslandState::MusicQueue:IslandState::MusicExpanded,300);
        } else {
            m_hoverExpanded=true;
        }
        InvalidateRect(m_hwnd,nullptr,FALSE);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  OnMouseDown — démarrage drag sliders + détection appui long Wi-Fi
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::OnMouseDown(int x,int y)
{
    m_mouseDownTime=GetTickCount();
    m_mouseDownX=x; m_mouseDownY=y;
    m_mouseDownOnWifi=false;
    m_longPressArmed=false;
    m_dragSlider=false;

    IslandState ds=m_animation->GetDisplayState();

    if(ds==IslandState::SystemExpanded){
        float pw2=m_content.pillW, px2=(m_screenW-pw2)*.5f;
        float pad=13.f;
        float row1Y=38.f, pillH2=48.f;
        float pw1=(pw2-pad*2.f-10.f)*0.38f;
        float row2Y=row1Y+pillH2+7.f;
        float sy1=row2Y+48.f, sy2=sy1+38.f;
        float sx=px2+pad, sw=pw2-pad*2.f-26.f-8.f;

        if(y>=(int)(sy1-14)&&y<=(int)(sy1+22)&&x>=(int)sx&&x<=(int)(sx+sw)){
            m_dragSlider=true; m_dragSliderIdx=0;
            SetCapture(m_hwnd);
            float v=std::clamp((float)(x-(int)sx)/sw,0.f,1.f);
            SetSystemVolume(v);
            InvalidateRect(m_hwnd,nullptr,FALSE);
        }
        else if(y>=(int)(sy2-14)&&y<=(int)(sy2+22)&&x>=(int)sx&&x<=(int)(sx+sw)){
            m_dragSlider=true; m_dragSliderIdx=1;
            SetCapture(m_hwnd);
            float v=std::clamp((float)(x-(int)sx)/sw,0.f,1.f);
            SetSystemBrightness(v);
            InvalidateRect(m_hwnd,nullptr,FALSE);
        }
        else if(y>=(int)row1Y&&y<=(int)(row1Y+pillH2)){
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

    if(m_dragSlider){
        m_dragSlider=false;
        ReleaseCapture();
    }
    if(m_hoverExpanded){
        m_hoverExpanded=false;
        m_animation->StartTransition(IslandState::Idle,260);
    }
    InvalidateRect(m_hwnd,nullptr,FALSE);
}

// ─────────────────────────────────────────────────────────────────────────────
//  OnMouseUp — gestion des clics complets
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::OnMouseUp(int x,int y)
{
    KillTimer(m_hwnd,TIMER_LONGPRESS);
    bool wasWifi=m_mouseDownOnWifi&&m_longPressArmed;
    m_mouseDownOnWifi=false;
    m_longPressArmed=false;

    if(m_dragSlider){
        m_dragSlider=false;
        ReleaseCapture();
        return;
    }

    IslandState ds=m_animation->GetDisplayState();

    // ── Menu haut (onglets) — avec animation slide ────────────────────────────
    if(ds==IslandState::MusicExpanded||ds==IslandState::MusicQueue||
       ds==IslandState::NotifExpanded||ds==IslandState::NotifList||
       ds==IslandState::SystemExpanded||ds==IslandState::WifiList){
        float cx2=m_screenW*.5f;
        struct{float x;int idx;}menus[]={{cx2-58,0},{cx2,1},{cx2+58,2}};
        for(auto& m:menus){
            if(x>=m.x-22&&x<=m.x+22&&y>=4&&y<=30){
                SwitchMenu(m.idx);
                return;
            }
        }
    }

    // ── Retour depuis WifiList ─────────────────────────────────────────────────
    if(ds==IslandState::WifiList){
        float pw2=m_content.pillW, px2=(m_screenW-pw2)*.5f;
        float pad=14.f;
        if(x>=(int)(px2+pad)&&x<=(int)(px2+pad+28)&&y>=10&&y<=30){
            QuerySystemControls();
            m_animation->StartTransition(IslandState::SystemExpanded,260);
            return;
        }
        float itemH=48.f, gap=5.f, iy=48.f;
        for(int i=0;i<(int)m_wifiNetworks.size();++i){
            if(y>=(int)iy&&y<=(int)(iy+itemH)){
                ShellExecute(nullptr,L"open",L"ms-settings:network-wifi",nullptr,nullptr,SW_SHOWNORMAL);
                break;
            }
            iy+=itemH+gap;
        }
        return;
    }

    if(ds==IslandState::MusicExpanded||ds==IslandState::MusicQueue){
        float pw=m_content.pillW, ph=m_content.pillH;
        float cx2=m_screenW*.5f, px=cx2-pw*.5f;

        float ctrlY=(ds==IslandState::MusicExpanded)
            ? (128.f+(ph-128.f)*.5f)
            : (40.f+54.f+12.f+42.f);

        if(y>=(int)(ctrlY-28)&&y<=(int)(ctrlY+28)){
            float xs[]={px+48,cx2-76,cx2,cx2+76,px+pw-48};
            for(int i=0;i<5;++i){
                if(x>=(int)(xs[i]-28)&&x<=(int)(xs[i]+28)){
                    if(i==0){
                        m_showQueue=!m_showQueue;
                        m_animation->StartTransition(m_showQueue?IslandState::MusicQueue:IslandState::MusicExpanded,320);
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
    } else if(ds==IslandState::NotifList){
        float pw2=m_content.pillW, px2=(m_screenW-pw2)*.5f;
        float pad=14.f, btnW=110.f;
        float btnX=px2+pw2-pad-btnW;
        if(x>=(int)btnX&&x<=(int)(btnX+btnW)&&y>=38&&y<=62){
            m_notifHistory.clear(); m_content.notifHistory.clear();
            m_appBadges.clear();
            m_content.appBadges.clear();
            m_content.totalUnreadCount = 0;
            m_badgePulseT = 1.f;
            m_content.badgePulseT = 1.f;
            TransitionToNotifList();
            InvalidateRect(m_hwnd,nullptr,FALSE);
        }
    } else if(ds==IslandState::SystemExpanded){
        float pw2=m_content.pillW, px2=(m_screenW-pw2)*.5f;
        float pad=13.f;
        float row1Y=38.f, pillH2=48.f;
        float pw1=(pw2-pad*2.f-10.f)*0.38f;
        float pw2b=pw1;
        float pw3=pw2-pad*2.f-pw1*2.f-10.f;
        float row2Y=row1Y+pillH2+7.f;

        if(y>=(int)row1Y&&y<=(int)(row1Y+pillH2)){
            float x1=px2+pad, x2=x1+pw1+5.f, x3=x2+pw2b+5.f;
            if(x>=(int)x1&&x<=(int)(x1+pw1)){
                if(!wasWifi) ToggleWifi();
            } else if(x>=(int)x2&&x<=(int)(x2+pw2b)){
                ToggleBluetooth();
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
//  OnLongPress — appui long sur Wi-Fi → liste réseaux
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
                     ds==IslandState::WifiList);
    if(!isExpanded) return;

    LONG delta=screenX-m_gesturePrevX;
    m_gesturePrevX=screenX;
    m_gestureAccumX+=(float)delta;

    if(m_gestureAccumX>SWIPE_THRESH){
        m_gestureAccumX=0.f;
        int newMenu=m_activeMenu-1;
        if(newMenu<0) newMenu=2;
        SwitchMenu(newMenu);
    } else if(m_gestureAccumX<-SWIPE_THRESH){
        m_gestureAccumX=0.f;
        int newMenu=m_activeMenu+1;
        if(newMenu>2) newMenu=0;
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
                if(SUCCEEDED(pDev->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,nullptr,(void**)&pVol))&&pVol){
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
                for(DWORD i=0;i<pList->dwNumberOfItems&&!m_wifiEnabled;++i){
                    if(pList->InterfaceInfo[i].isState==wlan_interface_state_connected){
                        m_wifiEnabled=true;
                        PWLAN_CONNECTION_ATTRIBUTES pAttr=nullptr;
                        DWORD attrSz=0;
                        if(WlanQueryInterface(hWlan,&pList->InterfaceInfo[i].InterfaceGuid,
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
//  QueryWifiNetworks — liste les réseaux disponibles via WlanAPI
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
                m_wifiNetworks.push_back(item);
            }
            WlanFreeMemory(pNetList);
        }
    }
    WlanFreeMemory(pList);
    WlanCloseHandle(hWlan,nullptr);

    if(m_wifiNetworks.empty()&&m_wifiEnabled&&!m_wifiSSID.empty()){
        m_wifiNetworks.push_back({m_wifiSSID,true,75});
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Actions système
// ─────────────────────────────────────────────────────────────────────────────
void WindowManager::ToggleWifi()
{
    SHELLEXECUTEINFOW sei={sizeof(sei)};
    sei.lpVerb=L"runas"; sei.lpFile=L"netsh";
    sei.lpParameters=m_wifiEnabled?L"interface set interface \"Wi-Fi\" disable"
                                  :L"interface set interface \"Wi-Fi\" enable";
    sei.nShow=SW_HIDE;
    if(!ShellExecuteExW(&sei))
        ShellExecute(nullptr,L"open",L"ms-settings:network-wifi",nullptr,nullptr,SW_SHOWNORMAL);
    Sleep(600);
    QuerySystemControls();
}

void WindowManager::ToggleBluetooth()
{
    ShellExecute(nullptr,L"open",L"ms-settings:bluetooth",nullptr,nullptr,SW_SHOWNORMAL);
    Sleep(400); QuerySystemControls();
}

void WindowManager::ToggleDarkMode()
{
    HKEY hKey=nullptr;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0,KEY_SET_VALUE,&hKey)==ERROR_SUCCESS){
        DWORD val=m_darkMode?1:0;
        RegSetValueExW(hKey,L"AppsUseLightTheme", 0,REG_DWORD,(LPBYTE)&val,sizeof(val));
        RegSetValueExW(hKey,L"SystemUsesLightTheme",0,REG_DWORD,(LPBYTE)&val,sizeof(val));
        RegCloseKey(hKey);
        SendMessageTimeoutW(HWND_BROADCAST,WM_SETTINGCHANGE,0,(LPARAM)L"ImmersiveColorSet",SMTO_ABORTIFHUNG,500,nullptr);
        m_darkMode=!m_darkMode; m_content.darkModeEnabled=m_darkMode;
    }
}

void WindowManager::ToggleNightLight()
{
    ShellExecute(nullptr,L"open",L"ms-settings:nightlight",nullptr,nullptr,SW_SHOWNORMAL);
    Sleep(300); QuerySystemControls();
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
            if(SUCCEEDED(pDev->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,nullptr,(void**)&pVol))&&pVol){
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
                DWORD newB=(DWORD)(minB+v*(maxB-minB));
                SetMonitorBrightness(pm[0].hPhysicalMonitor,newB);
                done=true;
            }
            DestroyPhysicalMonitors(numPhys,pm.data());
        }
    }

    if(!done){
        wchar_t cmd[128];
        int pct=(int)(v*100.f);
        swprintf_s(cmd,
            L"powershell -WindowStyle Hidden -Command "
            L"\"(Get-WmiObject -Namespace root/WMI -Class WmiMonitorBrightnessMethods).WmiSetBrightness(1,%d)\"",pct);
        SHELLEXECUTEINFOW sei={sizeof(sei)};
        sei.lpVerb=L"open"; sei.lpFile=L"cmd.exe";
        wchar_t args[256]; swprintf_s(args,L"/c %s",cmd);
        sei.lpParameters=args; sei.nShow=SW_HIDE;
        ShellExecuteExW(&sei);
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
    struct DevInfo{std::wstring id,name;};
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
    int sel=(int)TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON|TPM_NONOTIFY,pt.x,pt.y,0,m_hwnd,nullptr);
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
        auto*cs=reinterpret_cast<CREATESTRUCT*>(lp);
        self=reinterpret_cast<WindowManager*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd,GWLP_USERDATA,(LONG_PTR)self);
        self->m_hwnd=hwnd;
    } else self=reinterpret_cast<WindowManager*>(GetWindowLongPtr(hwnd,GWLP_USERDATA));
    if(!self) return DefWindowProc(hwnd,msg,wp,lp);

    if(self->m_tray&&self->m_tray->HandleMessage(hwnd,msg,wp,lp)) return 0;

    switch(msg){
    case WM_DESTROY:
        KillTimer(hwnd,1);KillTimer(hwnd,2);KillTimer(hwnd,3);KillTimer(hwnd,4);
        PostQuitMessage(0); return 0;

    case WM_TIMER:
        if(wp==TIMER_ANIM)       self->OnAnimTick();
        else if(wp==TIMER_CLOCK) self->UpdateClock();
        else if(wp==TIMER_CHECK) self->CheckDismiss();
        else if(wp==TIMER_LONGPRESS){
            KillTimer(hwnd,TIMER_LONGPRESS);
            self->OnLongPress();
        }
        return 0;

    case WM_PAINT:       self->OnPaint();   return 0;
    case WM_ERASEBKGND:  return 1;
    case WM_SIZE:
        self->m_renderer->Resize(LOWORD(lp),HIWORD(lp)); return 0;

    case WM_MOUSEMOVE:
        self->OnMouseMove(GET_X_LPARAM(lp),GET_Y_LPARAM(lp)); return 0;
    case WM_MOUSELEAVE:
        self->OnMouseLeave(); return 0;
    case WM_LBUTTONDOWN:
        self->OnMouseDown(GET_X_LPARAM(lp),GET_Y_LPARAM(lp)); return 0;
    case WM_LBUTTONUP:
        self->OnMouseUp(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));   return 0;

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
        float pw=self->m_content.pillW;
        RECT rc; GetClientRect(hwnd,&rc);
        float pillLeft =(rc.right-pw)*.5f;
        float pillRight=pillLeft+pw;
        
        // On inclut une petite marge (10px) pour les oreilles (ears) en haut
        if(pt.x>=pillLeft-10.f&&pt.x<=pillRight+10.f&&pt.y>=0&&pt.y<=rc.bottom)
            return HTCLIENT;
        return HTTRANSPARENT;
    }

    default: return DefWindowProc(hwnd,msg,wp,lp);
    }
}
