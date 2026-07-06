#include "Sounds.h"
#include <Windows.h>
#include <mmsystem.h>
#include <vector>
#include <cmath>
#include <algorithm>
#pragma comment(lib, "winmm.lib")

namespace {

constexpr int   SR = 22050;      // sample rate
constexpr float PI = 3.14159265f;

// Construit un WAV PCM16 mono en mémoire à partir d'échantillons float [-1..1].
// Buffer statique : PlaySound(SND_MEMORY|SND_ASYNC) lit le buffer pendant la
// lecture → il doit survivre à l'appel.
void PlayPcm(const std::vector<float>& smp)
{
    static std::vector<unsigned char> wav;   // survit à l'appel (async)
    const int n = (int)smp.size();
    const int dataBytes = n * 2;
    wav.resize(44 + dataBytes);
    unsigned char* p = wav.data();
    auto W32=[&](int off, unsigned v){ p[off]=v&0xFF; p[off+1]=(v>>8)&0xFF; p[off+2]=(v>>16)&0xFF; p[off+3]=(v>>24)&0xFF; };
    auto W16=[&](int off, unsigned v){ p[off]=v&0xFF; p[off+1]=(v>>8)&0xFF; };
    memcpy(p,   "RIFF", 4); W32(4, 36 + dataBytes);
    memcpy(p+8, "WAVE", 4);
    memcpy(p+12,"fmt ", 4); W32(16, 16); W16(20, 1 /*PCM*/); W16(22, 1 /*mono*/);
    W32(24, SR); W32(28, SR*2); W16(32, 2); W16(34, 16);
    memcpy(p+36,"data", 4); W32(40, dataBytes);
    for (int i = 0; i < n; ++i) {
        float v = std::clamp(smp[i], -1.f, 1.f);
        short s = (short)(v * 32000.f);
        W16(44 + i*2, (unsigned short)s);
    }
    PlaySoundW(nullptr, nullptr, 0);   // stoppe un éventuel son en cours
    PlaySoundW((LPCWSTR)wav.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

// Ton sinus avec glissando et enveloppe exponentielle douce
void AddTone(std::vector<float>& out, float f0, float f1, float ms,
             float gain, float attackMs = 4.f)
{
    int n = (int)(SR * ms / 1000.f);
    size_t base = out.size();
    out.resize(base + n, 0.f);
    float phase = 0.f;
    for (int i = 0; i < n; ++i) {
        float t   = (float)i / n;
        float f   = f0 + (f1 - f0) * t;
        phase    += 2.f * PI * f / SR;
        float env = std::min(1.f, (i / (SR * attackMs / 1000.f))) * expf(-3.2f * t);
        out[base + i] = sinf(phase) * env * gain;
    }
}

} // namespace

namespace Sounds {

// ─────────────────────────────────────────────────────────────────────────────
//  Ouverture d'îlot — 5 variantes
// ─────────────────────────────────────────────────────────────────────────────
void PlayExpand(float volume, int variant)
{
    std::vector<float> s;
    switch (variant) {
    case 1:   // Soyeux : glide long, doux
        AddTone(s, 660.f, 520.f, 220.f, 0.28f * volume, 18.f);
        break;
    case 2:   // Vif : rise brillant court
        AddTone(s, 520.f, 1040.f, 90.f, 0.34f * volume, 3.f);
        break;
    case 3:   // Cristal : deux tons haut cristallins
        AddTone(s, 1568.f, 1760.f, 60.f, 0.24f * volume, 4.f);
        AddTone(s, 2093.f, 2093.f, 80.f, 0.20f * volume, 3.f);
        break;
    case 4: return;   // Silencieux
    default:  // 0 = Subtil (historique)
        AddTone(s, 880.f, 660.f, 120.f, 0.35f * volume);
    }
    PlayPcm(s);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Notification — 5 variantes
// ─────────────────────────────────────────────────────────────────────────────
void PlayNotif(float volume, int variant)
{
    std::vector<float> s;
    switch (variant) {
    case 1:   // Bulle : double blip descendant compact
        AddTone(s, 1000.f, 1000.f, 55.f, 0.30f * volume, 3.f);
        AddTone(s, 800.f,  800.f,  70.f, 0.26f * volume, 3.f);
        break;
    case 2:   // Trille : 3 tons rapides ascendants
        AddTone(s, 1200.f, 1200.f, 55.f, 0.28f * volume, 3.f);
        AddTone(s, 1500.f, 1500.f, 55.f, 0.28f * volume, 3.f);
        AddTone(s, 1800.f, 1800.f, 90.f, 0.30f * volume, 3.f);
        break;
    case 3:   // Ding : cloche fine avec queue
        AddTone(s, 1568.f, 1568.f, 260.f, 0.32f * volume, 4.f);
        break;
    case 4: return;   // Silencieux
    default:  // 0 = Éclat (historique)
        AddTone(s, 1318.f, 1318.f, 70.f, 0.32f * volume);
        AddTone(s, 1760.f, 1760.f, 110.f, 0.30f * volume);
    }
    PlayPcm(s);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Appareil connecté — 4 variantes
// ─────────────────────────────────────────────────────────────────────────────
void PlayDevice(float volume, int variant)
{
    std::vector<float> s;
    switch (variant) {
    case 1:   // Câble : descente franche 660→330
        AddTone(s, 660.f, 330.f, 140.f, 0.32f * volume, 4.f);
        break;
    case 2:   // Marimba : ton chaud + harmonique
        AddTone(s, 523.f, 523.f, 180.f, 0.30f * volume, 4.f);
        AddTone(s, 784.f, 784.f, 140.f, 0.18f * volume, 6.f);
        break;
    case 3: return;   // Silencieux
    default:  // 0 = Minimal (historique)
        AddTone(s, 440.f, 500.f, 90.f, 0.30f * volume);
    }
    PlayPcm(s);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Libellés pour le Cockpit
// ─────────────────────────────────────────────────────────────────────────────
static const wchar_t* EXPAND_NAMES[5] = { L"Subtil", L"Soyeux", L"Vif", L"Cristal", L"Silence" };
static const wchar_t* NOTIF_NAMES [5] = { L"Éclat",  L"Bulle",  L"Trille", L"Ding", L"Silence" };
static const wchar_t* DEVICE_NAMES[4] = { L"Minimal", L"Câble", L"Marimba", L"Silence" };

const wchar_t* const* ExpandNames(int& count) { count = 5; return EXPAND_NAMES; }
const wchar_t* const* NotifNames (int& count) { count = 5; return NOTIF_NAMES;  }
const wchar_t* const* DeviceNames(int& count) { count = 4; return DEVICE_NAMES; }

} // namespace Sounds
