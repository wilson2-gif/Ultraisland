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
    BluetoothList,      // liste appareils Bluetooth (clic sur Bluetooth)
    Expanding,
    Collapsing,
    CollapsingNotif,
};

enum class HUDType { Volume, Brightness, Network };

// Dimensions du pill (zone visible centrée)
namespace Pill {
    // Idle — capsule compacte Apple
    constexpr float W_IDLE  = 180.f;
    constexpr float H_IDLE  = 26.f;

    // Notification toast — vrai toast (icône + app + message), nettement plus
    // grand que l'idle pour que l'arrivée d'une notif soit VISIBLE.
    constexpr float W_NOTIF = 300.f;   // toast COMPACT (net, strict nécessaire)
    constexpr float H_NOTIF = 52.f;

    // Notification list — panneau historique (taille adaptative)
    // Largeur unifiée à 380 avec les autres panneaux à onglets (musique/système)
    // → en changeant d'onglet, SEULE la hauteur s'anime : transition beaucoup plus douce.
    constexpr float W_NOTIF_LIST  = 380.f;
    constexpr float H_NOTIF_LIST  = 84.f;   // base header; hauteur calculée dynamiquement
    constexpr float CR_NOTIF_LIST = 30.f;

    // Music expanded — carte arrondie Apple (largeur unifiée onglets)
    constexpr float W_MUSIC = 380.f;
    constexpr float H_MUSIC = 180.f;

    // HUD (volume / luminosité) — style iOS : pill vertical fin
    constexpr float W_HUD   = 58.f;    // ← pill étroit comme iOS
    constexpr float H_HUD   = 210.f;   // ← tall comme iOS

    // System controls panel (largeur unifiée onglets)
    // Hauteur ajustée au contenu réel : dernier élément = slider luminosité à
    // sy2≈179 (+ knob ≈12) → on borne à 212 pour supprimer l'espace vide du bas.
    constexpr float W_SYS   = 380.f;
    constexpr float H_SYS   = 212.f;
    constexpr float CR_SYS  = 30.f;

    // Music queue (Playing Next) — MÊME largeur que MusicExpanded (380), seule la
    // hauteur grandit. 470 px → ~4 titres visibles (au lieu de 2 à 340).
    constexpr float W_QUEUE  = 380.f;
    constexpr float H_QUEUE  = 470.f;
    constexpr float CR_QUEUE = 30.f;

    // WiFi list panel
    constexpr float W_WIFI  = 400.f;
    constexpr float H_WIFI  = 220.f;
    constexpr float CR_WIFI = 30.f;

    // Bluetooth list panel (même gabarit que la liste Wi-Fi)
    constexpr float W_BT    = 400.f;
    constexpr float H_BT    = 220.f;
    constexpr float CR_BT   = 30.f;

    // Corner radii par état — Apple utilise des radii très généreux
    constexpr float CR_IDLE  = H_IDLE  / 2.f;   // 13 — capsule parfaite
    constexpr float CR_NOTIF = H_NOTIF / 2.f;   // 16 — capsule
    constexpr float CR_MUSIC = 30.f;             // grande carte arrondie Apple
    constexpr float CR_HUD   = W_HUD   / 2.f;   // 29 — pill iOS vertical
}

// ─────────────────────────────────────────────────────────────────────────────
//  PillRT — paramètres RUNTIME de l'encoche (pilotés par AppConfig/Cockpit).
//  Défauts = constantes Pill::*. Modifiés par PillRT::Apply(cfg) au chargement
//  et à chaque « Appliquer » du Cockpit (WM_ISLAND_RELOADCFG).
// ─────────────────────────────────────────────────────────────────────────────
namespace PillRT {
    inline float W_IDLE     = Pill::W_IDLE;
    inline float H_IDLE     = Pill::H_IDLE;
    inline float CR_IDLE    = Pill::CR_IDLE;
    inline float BASE_ALPHA = 0.82f;      // opacité du verre (plus translucide)
    inline int   POS        = 1;          // 0=gauche 1=centre 2=droite
    inline bool  ADAPTIVE   = true;       // teinte pochette vs accent fixe
    inline float ACC_R = 0.235f, ACC_G = 0.60f, ACC_B = 1.0f;
    inline bool  GRAIN      = false;      // texture de bruit
    inline int   SHAPE      = 0;          // 0=Pilule 1=Rectangle 2=Cercle 3=Goutte 4=Courbe
    // Typographie (Phase B — appliquée au pill idle par le Renderer)
    inline int   FONT_FAM   = 0;          // 0=Segoe UI 1=Inter 2=Manrope 3=IBM Plex
    inline int   FONT_WT    = 1;          // 0=Normal 1=Semi-Bold 2=Bold
    inline float FONT_SCALE = 1.0f;
}

// Position X du pill selon PillRT::POS (marge 24 px des bords en G/D).
inline float PillX(float screenW, float pw) {
    switch (PillRT::POS) {
    case 0:  return 24.f;
    case 2:  return screenW - pw - 24.f;
    default: return (screenW - pw) * 0.5f;
    }
}

// Hauteur adaptative du panneau notifications (max 3 visibles sans swipe)
inline float ComputeNotifListHeight(int count) {
    count = std::min(count, 3);
    if (count == 0) return 110.f;   // état vide (icône + texte)
    constexpr float HEADER_H = 72.f;
    constexpr float ITEM_H   = 58.f;
    constexpr float GAP      = 6.f;
    constexpr float BOTTOM   = 14.f;
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
    case IslandState::BluetoothList:             return Pill::CR_BT;
    default:                                     return PillRT::CR_IDLE;   // idle = runtime
    }
}
