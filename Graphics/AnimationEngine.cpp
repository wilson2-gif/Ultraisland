#include "AnimationEngine.h"
#include <cmath>
#include <algorithm>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ── Ressort très amorti — mouvement DOUX, quasi sans rebond ───────────────────
//  ζ=0.85 → dépassement < 1% (imperceptible) : décélération soyeuse, pas de « boing ».
//  Le décalage W/H/CR + le squash léger suffisent à garder le caractère « liquid ».
float AnimationEngine::Spring(float t) {
    if (t <= 0) return 0.f;
    if (t >= 1) return 1.f;
    const float omega = 8.0f;    // fréquence propre
    const float zeta  = 0.85f;   // fort amortissement → mouvement doux
    float wd  = omega * sqrtf(1.f - zeta*zeta);
    float env = expf(-zeta * omega * t);
    return 1.f - env * (cosf(wd * t) + (zeta * omega / wd) * sinf(wd * t));
}

float AnimationEngine::Cubic(float t) {
    float u = 1 - t;
    return 1 - u * u * u;
}

std::pair<float, float> AnimationEngine::PillSizeOf(IslandState s) {
    switch (s) {
    case IslandState::MusicExpanded:             return {Pill::W_MUSIC,      Pill::H_MUSIC};
    case IslandState::MusicQueue:                return {Pill::W_QUEUE,      Pill::H_QUEUE};
    case IslandState::NotifExpanded:
    case IslandState::CollapsingNotif:           return {Pill::W_NOTIF,      Pill::H_NOTIF};
    case IslandState::NotifList:                 return {Pill::W_NOTIF_LIST, Pill::H_NOTIF_LIST};
    case IslandState::HUDVolume:
    case IslandState::HUDBrightness:
    case IslandState::HUDNetwork:                return {Pill::W_HUD,        Pill::H_HUD};
    case IslandState::SystemExpanded:            return {Pill::W_SYS,        Pill::H_SYS};
    case IslandState::WifiList:                  return {Pill::W_WIFI,       Pill::H_WIFI};
    case IslandState::BluetoothList:             return {Pill::W_BT,         Pill::H_BT};
    default:                                     return {PillRT::W_IDLE,     PillRT::H_IDLE};   // idle = runtime (Cockpit)
    }
}

AnimationEngine::AnimationEngine() = default;

// ── Logique commune de démarrage ──────────────────────────────────────────────
void AnimationEngine::BeginTransition(float dstW, float dstH, float dstCR,
                                       IslandState target, IslandState displayDuring,
                                       DWORD durationMs)
{
    if (m_animating) {
        float cw, ch;
        GetCurrentPillSize(cw, ch);
        m_srcPW = cw;
        m_srcPH = ch;
        m_srcCR = GetCurrentCornerRadius();
    } else {
        m_srcPW = m_dstPW;
        m_srcPH = m_dstPH;
        m_srcCR = m_dstCR;
    }

    m_dstPW = dstW;
    m_dstPH = dstH;
    m_dstCR = dstCR;
    m_targetState = target;
    m_currentState = displayDuring;
    m_durationMs = durationMs;
    m_startTime = GetTickCount();
    m_t = 0.f;
    m_animating = true;
}

void AnimationEngine::StartTransition(IslandState target, DWORD ms)
{
    if (target == m_targetState && !m_animating && m_t >= 1.f) return;

    auto [dw, dh] = PillSizeOf(target);
    float dcr = CornerRadiusOf(target);

    bool bigger  = (dh > m_dstPH + 8.f);
    bool smaller = (m_dstPH > dh + 8.f);

    IslandState displayDuring;
    if      (target == IslandState::NotifExpanded)   displayDuring = IslandState::NotifExpanded;
    else if (target == IslandState::Idle && smaller)  displayDuring = IslandState::Collapsing;
    else if (target == IslandState::Idle)             displayDuring = IslandState::CollapsingNotif;
    else if (bigger)                                  displayDuring = IslandState::Expanding;
    else                                              displayDuring = target;

    BeginTransition(dw, dh, dcr, target, displayDuring, ms);
}

// ── Transition avec dimensions explicites (ex: notifications adaptatives) ──────
void AnimationEngine::StartTransitionCustom(IslandState target, float w, float h, float cr, DWORD ms)
{
    bool bigger  = (h > m_dstPH + 8.f);
    bool smaller = (m_dstPH > h + 8.f);

    IslandState displayDuring;
    if      (target == IslandState::Idle && smaller)  displayDuring = IslandState::Collapsing;
    else if (target == IslandState::Idle)             displayDuring = IslandState::CollapsingNotif;
    else if (bigger)                                  displayDuring = IslandState::Expanding;
    else                                              displayDuring = target;

    BeginTransition(w, h, cr, target, displayDuring, ms);
}

bool AnimationEngine::Tick()
{
    if (!m_animating) return false;
    DWORD el = GetTickCount() - m_startTime;
    if (el >= m_durationMs) {
        m_t = 1.f;
        m_currentState = m_targetState;
        m_animating = false;
    } else {
        m_t = (float)el / (float)m_durationMs;
    }
    if (m_frameCallback) m_frameCallback();
    return m_animating;
}

// ── Fenêtre : plein écran en largeur, hauteur = hauteur pill ─────────────────
void AnimationEngine::GetCurrentSize(UINT& outW, UINT& outH) const
{
    float _w, h;
    GetCurrentPillSize(_w, h);
    outW = (UINT)m_screenW;
    outH = (UINT)(std::max)(4.f, h);
}

// ── Pill : effet liquide staggered ───────────────────────────────────────────
void AnimationEngine::GetCurrentPillSize(float& outW, float& outH) const
{
    // Hauteur : spring complet
    float eH = Spring(m_t);

    // Largeur : délai de 4%
    float tW  = (std::max)(0.f, m_t - 0.04f) / 0.96f;
    float eW  = Spring(tW);

    // Squash & stretch appliqué
    float sq = GetSquashFactor();

    outW = (std::max)(4.f, Lf(m_srcPW, m_dstPW, eW) * sq);
    outH = (std::max)(4.f, Lf(m_srcPH, m_dstPH, eH));
}

// ── Corner radius animé avec délai 3% ────────────────────────────────────────
float AnimationEngine::GetCurrentCornerRadius() const
{
    float tCR = (std::max)(0.f, m_t - 0.03f) / 0.97f;
    float eCR = Spring(tCR);
    float cr  = Lf(m_srcCR, m_dstCR, eCR);
    return (std::max)(2.f, cr);
}

// ── Wobble : intensité de la déformation blob (adoucie) ───────────────────
float AnimationEngine::GetWobbleAmount() const
{
    if (!m_animating) return 0.f;
    float sizeDelta = fabsf(m_dstPH - m_srcPH) + fabsf(m_dstPW - m_srcPW);
    float intensity = (std::min)(1.f, sizeDelta / 300.f);
    // Étaler et réduire l'amplitude pour un rendu plus délicat
    float bell = expf(-12.f * (m_t - 0.35f) * (m_t - 0.35f));
    return bell * intensity * 2.f;
}

// ── Wobble phase ─────────────────────────────────────────────────────────────
float AnimationEngine::GetWobblePhase() const
{
    return m_t * 14.f;
}

// ── Squash & stretch (adouci) ───────────────────────────────────────────────
float AnimationEngine::GetSquashFactor() const
{
    if (!m_animating) return 1.f;
    // Bell centrée sur le pic de vitesse ; amplitude discrète (mouvement doux)
    float bell = expf(-11.f * (m_t - 0.28f) * (m_t - 0.28f));
    bool expanding = (m_dstPH > m_srcPH + 4.f);
    float squeeze = bell * 0.020f;
    return expanding ? (1.f - squeeze) : (1.f + squeeze);
}
