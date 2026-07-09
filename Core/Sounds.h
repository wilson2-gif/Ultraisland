#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  Sounds — sons d'événements synthétisés en mémoire (aucun fichier).
//  WAV PCM 16-bit mono 22050 Hz générés à la volée, joués via PlaySound
//  (SND_MEMORY | SND_ASYNC). Volume appliqué à la génération.
// ─────────────────────────────────────────────────────────────────────────────
namespace Sounds {
    // ── Ouverture îlot ─────────────────────────────────────────────────
    // variant : 0 Subtil (défaut) · 1 Soyeux · 2 Vif · 3 Cristal · 4 Silencieux
    void PlayExpand(float volume, int variant = 0);
    // ── Notification ───────────────────────────────────────────────────
    // variant : 0 Éclat (défaut) · 1 Bulle · 2 Trille · 3 Ding · 4 Silencieux
    void PlayNotif (float volume, int variant = 0);
    // ── Appareil connecté ──────────────────────────────────────────────
    // variant : 0 Minimal (défaut) · 1 Câble · 2 Marimba · 3 Silencieux
    void PlayDevice(float volume, int variant = 0);

    // Libellés pour l'UI du Cockpit (tableau d'options par catégorie).
    const wchar_t* const* ExpandNames(int& count);   // 5 options
    const wchar_t* const* NotifNames (int& count);   // 5 options
    const wchar_t* const* DeviceNames(int& count);   // 4 options
}
