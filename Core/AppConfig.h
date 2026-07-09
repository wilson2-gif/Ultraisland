#pragma once
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
//  AppConfig — configuration persistante d'Ultraisland
//  Stockée en JSON plat dans %LOCALAPPDATA%\Ultraisland\config.json.
//  Tous les réglages du Cockpit vivent ici et survivent aux redémarrages.
// ─────────────────────────────────────────────────────────────────────────────
struct AppSettings;   // Core/SettingsWindow.h (first-run)

struct AppConfig
{
    // ── Modules ──────────────────────────────────────────────────────────
    bool islandEnabled     = true;
    bool musicEnabled      = true;
    bool notifEnabled      = true;
    bool systemEnabled     = true;
    bool lockScreenEnabled = true;
    bool startWithWindows  = false;
    bool runAsAdmin        = false;

    // ── Encoche : géométrie idle ─────────────────────────────────────────
    int   shape   = 0;      // 0=pilule 1=rectangle 2=cercle (préréglage UI)
    float idleW   = 180.f;  // 120..320
    float idleH   = 26.f;   // 22..48
    float idleCR  = 13.f;   // 4..24
    int   position= 1;      // 0=haut-gauche 1=haut-centre 2=haut-droite

    // ── Apparence ────────────────────────────────────────────────────────
    float baseOpacity = 0.72f;   // 0.40..1.00 — alpha du verre (translucide par défaut)
    bool  adaptiveTint= true;    // teinte dérivée de la pochette
    float accentR = 0.235f, accentG = 0.60f, accentB = 1.0f;  // accent fixe sinon
    bool  grain   = false;       // texture de bruit sur le verre
    bool  lockWallpaper = true;  // pochette → fond d'écran de verrouillage (MSIX)
    bool  ambientOnUnlock = true; // mode ambiant auto au déverrouillage (si musique)
    int   ambientIdleMin  = 5;    // ouverture auto après N min d'inactivité (0=off)

    // ── Typographie (Phase B) — appliquée dans le pill idle ────────────────
    int   fontFamily = 0;        // 0=Segoe UI Variable Display 1=Inter 2=Manrope 3=IBM Plex
    int   fontWeight = 1;        // 0=Normal 1=Semi-Bold 2=Bold
    float fontScale  = 1.0f;     // 0.8..1.4

    // ── Sons ─────────────────────────────────────────────────────────────
    bool  soundExpand = true;   float volExpand = 0.75f;   int soundExpandVar = 0;
    bool  soundNotif  = true;   float volNotif  = 0.75f;   int soundNotifVar  = 0;
    bool  soundDevice = true;   float volDevice = 0.75f;   int soundDeviceVar = 0;

    // ── IO ───────────────────────────────────────────────────────────────
    static std::wstring PathW();   // chemin complet du config.json
    bool Load();                   // false si absent/illisible
    bool Save() const;
    void FromAppSettings(const AppSettings& s);   // first-run → config
    void ApplyRuntime() const;     // copie vers PillRT (l'île lit PillRT)
};
