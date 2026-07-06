#pragma once
#include <Windows.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <functional>
#include <vector>
#include <string>

struct AppConfig;

// ─────────────────────────────────────────────────────────────────────────────
//  CockpitWindow — app de réglages « UltraIsland Pro » (maquettes Visily).
//  Rendu Direct2D immediate-mode : chaque frame redessine les widgets ET
//  enregistre leurs zones interactives (toggle/slider/segmented/palette/bouton).
//  Non bloquante : vit dans la boucle de messages principale, l'île continue.
// ─────────────────────────────────────────────────────────────────────────────
class CockpitWindow
{
public:
    ~CockpitWindow();

    // Ouvre (ou ramène au premier plan) le Cockpit.
    // onApply est appelé après « Appliquer » (main y sauve + notifie l'île).
    void Open(HINSTANCE hi, AppConfig* cfg, std::function<void()> onApply);
    bool IsOpen() const { return m_hwnd != nullptr; }

private:
    HWND        m_hwnd = nullptr;
    AppConfig*  m_cfg  = nullptr;
    std::function<void()> m_onApply;

    // D2D / DWrite
    ID2D1Factory*          m_f  = nullptr;
    ID2D1HwndRenderTarget* m_rt = nullptr;
    IDWriteFactory*        m_dw = nullptr;
    ID2D1SolidColorBrush*  m_b  = nullptr;
    IDWriteTextFormat *m_fH1=nullptr, *m_fH2=nullptr, *m_fBody=nullptr,
                      *m_fSmall=nullptr, *m_fIcon=nullptr, *m_fSection=nullptr;

    int m_page = 4;   // page active (défaut : Encoche)

    // Échelle DPI (1.25 à 125 %…). Le RT dessine en LOGIQUES (SetDpi) et les zones
    // de clic (m_zones) sont en logiques ; les coords souris Win32 arrivent en
    // PHYSIQUES → il FAUT diviser par m_dpi avant le hit-test, sinon décalage ×dpi.
    float m_dpi = 1.0f;

    // ── Scroll vertical des pages (contenu peut dépasser la zone visible) ──
    float m_scroll   = 0.f;   // offset de défilement courant (logique)
    float m_contentH = 0.f;   // hauteur totale du contenu de la page courante
    float m_pageMaxY = 0.f;   // suivi du plus bas élément dessiné (Fill/Txt) pour mesurer le contenu
    void  ClampScroll();      // borne m_scroll à [0, contenu - zone visible]

    // ── Interaction immediate-mode ───────────────────────────────────────
    // kind : 0 toggle(bool*) · 1 slider(float*) · 2 segmented(int*, idx)
    //        3 bouton(action) · 4 sidebar(idx) · 5 palette(idx)
    struct Zone { D2D1_RECT_F rc; int kind; void* val; float mn, mx; int idx; int action; };
    std::vector<Zone> m_zones;
    int   m_hot  = -1;    // zone survolée
    int   m_drag = -1;    // slider en cours de drag
    POINT m_mouse{};

    bool EnsureDevRes();
    void DropDevRes();
    void OnPaint();
    void OnMouseMove(int x, int y);
    void OnMouseDown(int x, int y);
    void OnMouseUp  (int x, int y);
    void ApplySliderDrag(int x);
    void DoAction(int action, int idx);
    int  HitZone(int x, int y) const;

    // ── Widgets ──────────────────────────────────────────────────────────
    void Fill (D2D1_RECT_F rc, D2D1_COLOR_F c, float r = 0.f);
    void Frame(D2D1_RECT_F rc, D2D1_COLOR_F c, float r = 0.f, float w = 1.f);
    void Txt  (const std::wstring& t, IDWriteTextFormat* f, D2D1_RECT_F rc,
               D2D1_COLOR_F c, DWRITE_TEXT_ALIGNMENT a = DWRITE_TEXT_ALIGNMENT_LEADING);
    void Card     (float x, float y, float w, float h);
    void RowLabel (float x, float y, const wchar_t* title, const wchar_t* sub);
    void Toggle   (float x, float y, bool* v);
    void SliderF  (float x, float y, float w, float* v, float mn, float mx,
                   const wchar_t* fmt);
    void Segmented(float x, float y, float w, float h, int* v,
                   const wchar_t* const* opts, int n, int action = 0);
    void Swatch   (float cx, float cy, D2D1_COLOR_F col, bool sel, int idx);
    void ButtonW  (float x, float y, float w, float h, const wchar_t* label,
                   bool accent, int action, int idx = 0);

    // ── Pages ────────────────────────────────────────────────────────────
    void PageApplication(float x, float y, float w);
    void PageApparence  (float x, float y, float w);
    void PageSons       (float x, float y, float w);
    void PageAutoris    (float x, float y, float w);
    void PageEncoche    (float x, float y, float w);

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
};
