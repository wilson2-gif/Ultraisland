#pragma once
#include <string>
#include <vector>
#include <functional>

// ─────────────────────────────────────────────────────────────────────────────
//  LyricsClient — paroles synchronisées via l'API publique lrclib.net
//  (gratuite, sans clé). GET /api/get?artist_name&track_name&duration →
//  champ "syncedLyrics" au format LRC « [mm:ss.xx] ligne ».
//  FetchAsync tourne sur un thread détaché ; le callback est invoqué sur CE
//  thread de fond → le consommateur doit se resynchroniser (mutex/Invalidate).
// ─────────────────────────────────────────────────────────────────────────────

struct LyricLine {
    float        t;      // horodatage en secondes
    std::wstring text;
};

namespace LyricsClient {
    void FetchAsync(std::wstring artist, std::wstring title, int durationSec,
                    std::function<void(std::vector<LyricLine>)> cb);
}
