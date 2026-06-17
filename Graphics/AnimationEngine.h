#pragma once
#include <Windows.h>
#include <functional>
#include "../Core/IslandDim.h"

// ─────────────────────────────────────────────────────────────────────────────
//  AnimationEngine  v6  — Liquid Apple iOS 18
//
//  Effet liquide avancé :
//  - Hauteur anime en premier (spring), largeur suit avec délai ~6%
//  - Corner radius anime avec délai ~3%
//  - Squash & stretch : compression perpendiculaire pendant le mouvement
//  - Wobble : déformation blob organique aux coins inférieurs
//  - Spring damped oscillator avec overshoot ~8% calibré iOS
//  - StartTransitionCustom : taille explicite (notifications adaptatives)
// ─────────────────────────────────────────────────────────────────────────────
class AnimationEngine {
public:
    AnimationEngine();

    void SetScreenWidth(int w) { m_screenW = w; }

    void StartTransition      (IslandState target, DWORD durationMs = 340);
    // Version avec dimensions explicites (ex: hauteur adaptative notifs)
    void StartTransitionCustom(IslandState target, float w, float h, float cr, DWORD durationMs = 300);
    bool Tick();

    float       GetT()            const { return m_t; }
    IslandState GetDisplayState() const { return m_targetState; }
    IslandState GetCurrentState() const { return m_currentState; }
    bool        IsAnimating()     const { return m_animating; }

    // Taille de la FENÊTRE (plein écran × hauteur interpolée)
    void  GetCurrentSize       (UINT& outW, UINT& outH) const;
    // Taille du PILL (largeur liquide avec délai)
    void  GetCurrentPillSize   (float& outW, float& outH) const;
    // Corner radius animé (délai 3%)
    float GetCurrentCornerRadius() const;
    // Wobble blob : intensité et phase (pour déformation organique)
    float GetWobbleAmount()       const;
    float GetWobblePhase()        const;
    // Squash & stretch factor (1.0 = normal, <1 = compressed, >1 = stretched)
    float GetSquashFactor()       const;

    void SetFrameCallback(std::function<void()> cb) { m_frameCallback = cb; }

private:
    IslandState m_currentState = IslandState::Idle;
    IslandState m_targetState  = IslandState::Idle;

    // Tailles pill src/dst
    float m_srcPW = Pill::W_IDLE, m_srcPH = Pill::H_IDLE;
    float m_dstPW = Pill::W_IDLE, m_dstPH = Pill::H_IDLE;

    // Corner radius src/dst
    float m_srcCR = Pill::CR_IDLE;
    float m_dstCR = Pill::CR_IDLE;

    int    m_screenW    = 1920;
    float  m_t          = 1.f;
    bool   m_animating  = false;
    DWORD  m_startTime  = 0;
    DWORD  m_durationMs = 340;
    std::function<void()> m_frameCallback;

    static std::pair<float, float> PillSizeOf(IslandState s);
    static float Spring(float t);   // overshoot ~8% iOS
    static float Cubic (float t);   // ease-out cubique
    static float Lf(float a, float b, float t) { return a + (b - a) * t; }

    void BeginTransition(float dstW, float dstH, float dstCR, IslandState target,
                         IslandState displayDuring, DWORD durationMs);
};
