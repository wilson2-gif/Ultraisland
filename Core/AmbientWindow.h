#pragma once
#include <Windows.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <functional>
#include <vector>
#include <mutex>
#include <string>
#include "../Modules/LyricsClient.h"

struct IslandContent;

// ─────────────────────────────────────────────────────────────────────────────
//  AmbientWindow — « mode verrouillage » plein écran (maquettes Visily) :
//  grande horloge + date, pochette floutée en fond, paroles synchronisées
//  (lrclib), carte musique glassmorphique avec contrôles précédent/play/suivant.
//  Ouvert par le tray « Mode ambiant » ou Ctrl+Alt+L ; fermé par Échap / ✕.
//  L'île se masque pendant l'affichage (callback onClosed pour la restaurer).
// ─────────────────────────────────────────────────────────────────────────────
class AmbientWindow
{
public:
    ~AmbientWindow();

    using MediaAction = std::function<void(int)>;   // 0=préc 1=play/pause 2=suiv
    using MediaSeekAction = std::function<void(float)>; // seek en pourcentage (0..1)
    using VolumeGet = std::function<float()>;
    using VolumeSet = std::function<void(float)>;

    void Configure(const IslandContent* content,
                   std::function<const std::vector<uint8_t>&()> thumbGetter,
                   MediaAction onMedia,
                   MediaSeekAction onSeek,
                   std::function<void()> onClosed,
                   VolumeGet volGet = {}, VolumeSet volSet = {});

    void Open(HINSTANCE hi);
    void Close();
    bool IsOpen() const { return m_hwnd != nullptr; }

    // Appelé par WindowManager à chaque changement de piste (thread UI)
    void OnTrackChanged(const std::wstring& title, const std::wstring& artist,
                        float durationSec);
    // Précharge les paroles du PROCHAIN titre (appelé qq s avant la fin du courant)
    void PrefetchLyrics(const std::wstring& title, const std::wstring& artist,
                        float durationSec);

private:
    HWND m_hwnd = nullptr;
    const IslandContent* m_content = nullptr;
    std::function<const std::vector<uint8_t>&()> m_thumbGetter;
    MediaAction           m_onMedia;
    MediaSeekAction       m_onSeek;
    std::function<void()> m_onClosed;

    // UI State
    bool  m_isDraggingProgress = false;
    float m_dragProgress       = 0.f;
    D2D1_RECT_F m_progressBarRect = {};

    // D2D
    ID2D1Factory*          m_f   = nullptr;
    ID2D1HwndRenderTarget* m_rt  = nullptr;
    ID2D1DeviceContext*    m_dc  = nullptr;   // QI du RT — pour l'effet de flou
    IDWriteFactory*        m_dw  = nullptr;
    IWICImagingFactory*    m_wic = nullptr;
    ID2D1SolidColorBrush*  m_b   = nullptr;
    ID2D1Bitmap*           m_art = nullptr;   // pochette décodée pour CE RT
    size_t                 m_artBytes = 0;    // taille du thumbnail décodé (cache)
    IDWriteTextFormat *m_fClock=nullptr, *m_fDate=nullptr, *m_fTitle=nullptr,
                      *m_fSub=nullptr,  *m_fSmall=nullptr, *m_fIcon=nullptr,
                      *m_fLyr=nullptr,  *m_fLyrDim=nullptr;

    // Lyrics (écrites par le thread de fetch, lues au paint)
    std::mutex             m_lyrMtx;
    std::vector<LyricLine> m_lyrics;
    std::wstring           m_lastTrackKey;
    unsigned               m_lyrGen = 0;     // invalide les fetchs périmés
    int                    m_lyrScanHint = 0;// index de départ du scan (perf)

    // Préchargement du PROCHAIN titre (élimine le « gap » de chargement).
    std::wstring           m_prefetchKey;
    std::vector<LyricLine> m_prefetchLyrics;
    // Transition « morph » au changement de piste (crossfade contenu).
    DWORD                  m_trackMorphTick = 0;

    // Transitions douces (fondu global + changement de ligne de paroles)
    DWORD m_openTick    = 0;      // fondu d'ouverture (280 ms)
    bool  m_closing     = false;  // fondu de fermeture (220 ms) puis destruction
    DWORD m_closeTick   = 0;
    int   m_lyrIdxShown = -1;     // dernière ligne active (anime le changement)
    DWORD m_lyrTick     = 0;

    // Zones cliquables (reconstruites au paint)
    // action : 0 préc · 1 play · 2 suiv · 5 slider volume · 9 fermer
    struct Zone { D2D1_RECT_F rc; int action; };
    std::vector<Zone> m_zones;

    // Échelle DPI (1.25 à 125 %…). Le RT dessine en LOGIQUES (SetDpi) et les zones
    // de clic sont en logiques ; les coords souris Win32 arrivent en PHYSIQUES →
    // diviser par m_dpi avant le hit-test, sinon les boutons ne réagissent pas.
    float m_dpi = 1.0f;

    // Slider volume (Ambiant++)
    bool  m_dragVolume = false;
    float m_volume     = 0.75f;     // 0..1, synchronisé au démarrage
    float VolumeFromX(int x) const;

    // Feedback (WindowManager sets/gets réel système, via callbacks)
    VolumeGet m_volGet;
    VolumeSet m_volSet;

    bool EnsureDevRes();
    void DropDevRes();
    void DestroyNow();       // destruction réelle (après le fondu de fermeture)
    void DecodeArt();
    void OnPaint();
    void OnClick(int x, int y);
    void OnMouseDown(int x, int y);
    void OnMouseMove(int x, int y);

    void Txt(const std::wstring& t, IDWriteTextFormat* f, D2D1_RECT_F rc,
             D2D1_COLOR_F c, DWRITE_TEXT_ALIGNMENT a = DWRITE_TEXT_ALIGNMENT_CENTER);

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
};
