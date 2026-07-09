#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>

// ─────────────────────────────────────────────────────────────────────────────
//  SpotifyClient
//  Authorization Code + PKCE pour app desktop (pas de client secret).
//  - Connect()       : ouvre le navigateur, listener loopback, échange code → tokens
//  - StartPolling()  : interroge /me/player/queue toutes les N secondes
//  - StopPolling()
//  - IsConnected()   : true si refresh_token persisté (DPAPI) ou access_token vivant
//
//  Le refresh_token est chiffré via DPAPI puis stocké dans
//      %LOCALAPPDATA%\Ultraisland\spotify.dat
//  Tous les callbacks sont invoqués depuis le thread de polling (background).
//  Le consommateur (WindowManager) doit pousser dans sa queue thread-safe.
// ─────────────────────────────────────────────────────────────────────────────

struct SpotifyTrack {
    std::wstring title;
    std::wstring artist;
    std::wstring albumArtUrl;
};

class SpotifyClient
{
public:
    SpotifyClient();
    ~SpotifyClient();

    // Renseigner AVANT Connect()/Restore(). Le client_id est public (pas de secret).
    void Configure(const std::string& clientId, int loopbackPort = 53682);

    // Tente de recharger le refresh_token chiffré DPAPI sur disque.
    // Retourne true si on a un refresh_token valide (peut encore échouer au refresh).
    bool Restore();

    // Lance le flow OAuth complet (ouvre le navigateur). Bloquant ~30 s max.
    // Retourne true si auth réussie ET tokens stockés.
    bool Connect();

    // Démarre/arrête le polling de /me/player/queue (intervalle en secondes).
    void StartPolling(int intervalSec = 10);
    void StopPolling();

    // Fetch immédiat one-shot (thread détaché) — appelé au changement de piste.
    void RefreshNow();

    bool IsConnected() const { return !m_refreshToken.empty(); }

    // Callback appelé sur le thread de polling avec la dernière file connue.
    using QueueCallback = std::function<void(const std::vector<SpotifyTrack>&)>;
    void SetQueueCallback(QueueCallback cb) { m_queueCb = std::move(cb); }

    // Efface tokens (déconnexion).
    void Logout();

private:
    std::string  m_clientId;
    int          m_port = 53682;
    std::string  m_redirectUri;

    std::string  m_accessToken;
    std::string  m_refreshToken;
    DWORD        m_accessExpiresAt = 0;     // GetTickCount + (expires_in*1000)

    QueueCallback m_queueCb;

    std::atomic<bool> m_polling{false};
    std::atomic<bool> m_shuttingDown{false};  // mis à true au destructeur
    std::atomic<int>  m_inFlight{0};           // fetchs détachés (RefreshNow) en vol
    std::thread       m_pollThread;
    mutable std::mutex m_mtx;

    // Internals
    bool  ExchangeCode(const std::string& code, const std::string& verifier);
    bool  RefreshAccessToken();
    bool  FetchQueueOnce();

    void  PersistTokens();
    void  LoadTokens();
    std::wstring TokenFilePath() const;
};
