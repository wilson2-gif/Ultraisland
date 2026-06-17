#pragma once
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
//  IslandDim.h  — dimensions Ultraisland (style Apple iOS liquid)
//
//  La FENÊTRE est toujours pleine largeur écran.
//  Le PILL est dessiné centré à l'intérieur, en taille variable.
//  Les transitions utilisent un spring damped oscillator avec
//  staggered W/H/CR pour l'effet liquide.
// ─────────────────────────────────────────────────────────────────────────────

enum class IslandState {
    Idle,
    MusicExpanded,
    MusicQueue,
    NotifExpanded,
    NotifList,          // panneau historique notifications
    HUDVolume, HUDBrightness, HUDNetwork,
    SystemExpanded,
    WifiList,           // liste réseaux Wi-Fi (appui long)
    Expanding,
    Collapsing,
    CollapsingNotif,
};

enum class HUDType { Volume, Brightness, Network };

// Dimensions du pill (zone visible centrée)
namespace Pill {
    // Idle — capsule compacte
    constexpr float W_IDLE  = 184.f;
    constexpr float H_IDLE  = 26.f;

    // Notification toast — capsule légèrement plus large
    constexpr float W_NOTIF = 240.f;
    constexpr float H_NOTIF = 26.f;

    // Notification list — panneau historique (taille adaptative, celle-ci = max 3 notifs)
    constexpr float W_NOTIF_LIST  = 420.f;
    constexpr float H_NOTIF_LIST  = 84.f;   // base header; hauteur calculée dynamiquement
    constexpr float CR_NOTIF_LIST = 28.f;

    // Music expanded — carte arrondie plein player (corrigé : contrôles ne débordent plus)
    constexpr float W_MUSIC = 350.f;
    constexpr float H_MUSIC = 178.f;        // était 160 — assez pour les contrôles

    // HUD (volume / luminosité)
    constexpr float W_HUD   = 195.f;
    constexpr float H_HUD   = 125.f;

    // System controls panel
    constexpr float W_SYS   = 420.f;
    constexpr float H_SYS   = 258.f;
    constexpr float CR_SYS  = 28.f;

    // Music queue (À venir)
    constexpr float W_QUEUE  = 490.f;
    constexpr float H_QUEUE  = 390.f;
    constexpr float CR_QUEUE = 32.f;

    // WiFi list panel
    constexpr float W_WIFI  = 400.f;
    constexpr float H_WIFI  = 220.f;
    constexpr float CR_WIFI = 28.f;

    // Corner radii par état
    constexpr float CR_IDLE  = H_IDLE  / 2.f;   // 17 — capsule parfaite
    constexpr float CR_NOTIF = H_NOTIF / 2.f;   // 17 — capsule
    constexpr float CR_MUSIC = 28.f;             // grande carte arrondie Apple
    constexpr float CR_HUD   = 24.f;
}

// Hauteur adaptative du panneau notifications (max 3 visibles sans swipe)
inline float ComputeNotifListHeight(int count) {
    count = std::min(count, 3);
    if (count == 0) return 110.f;   // état vide (icône + texte)
    constexpr float HEADER_H = 82.f; // top icons + bouton "tout effacer"
    constexpr float ITEM_H   = 56.f;
    constexpr float GAP      = 6.f;
    constexpr float BOTTOM   = 12.f;
    return HEADER_H + (float)count * (ITEM_H + GAP) - GAP + BOTTOM;
}

// Helper : corner radius pour un état donné
inline float CornerRadiusOf(IslandState s) {
    switch (s) {
    case IslandState::MusicExpanded:             return Pill::CR_MUSIC;
    case IslandState::MusicQueue:                return Pill::CR_QUEUE;
    case IslandState::NotifExpanded:
    case IslandState::CollapsingNotif:           return Pill::CR_NOTIF;
    case IslandState::NotifList:                 return Pill::CR_NOTIF_LIST;
    case IslandState::HUDVolume:
    case IslandState::HUDBrightness:
    case IslandState::HUDNetwork:                return Pill::CR_HUD;
    case IslandState::SystemExpanded:            return Pill::CR_SYS;
    case IslandState::WifiList:                  return Pill::CR_WIFI;
    default:                                     return Pill::CR_IDLE;
    }
}
