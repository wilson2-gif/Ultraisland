#include "MediaManager.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>   // itération de GetSessions()
#include <winrt/Windows.Storage.Streams.h>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Media::Control;

MediaManager::MediaManager() = default;
MediaManager::~MediaManager() { Shutdown(); }

void MediaManager::Initialize()
{
    try {
        auto asyncOp = GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
        m_manager = asyncOp.get();

        if (m_manager) {
            m_sessionChangedToken = m_manager.SessionsChanged(
                { this, &MediaManager::OnSessionChanged });
            auto best = PickBestSession();
            if (best) {
                SubscribeToSession(best);
            }
        }
    } catch (const winrt::hresult_error&) {
        m_manager = nullptr;
    } catch (...) {
        m_manager = nullptr;
    }
}

void MediaManager::Shutdown()
{
    if (m_manager && m_sessionChangedToken) {
        try {
            m_manager.SessionsChanged(m_sessionChangedToken);
        } catch (...) {}
        m_sessionChangedToken = {};
    }
    UnsubscribeFromSession();
    m_manager = nullptr;
}

void MediaManager::SetMediaChangedCallback(
    std::function<void(const std::wstring&, const std::wstring&, const std::wstring&, bool, float, float, const std::vector<uint8_t>&)> callback)
{
    m_callback = std::move(callback);
}

void MediaManager::PlayPause()
{
    if (!m_session) return;
    try {
        auto info = m_session.GetPlaybackInfo();
        if (info && info.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing)
            m_session.TryPauseAsync().get();
        else
            m_session.TryPlayAsync().get();
    } catch (...) {}
}

void MediaManager::SkipNext()
{
    if (!m_session) return;
    try { m_session.TrySkipNextAsync().get(); } catch (...) {}
}

void MediaManager::SkipPrevious()
{
    if (!m_session) return;
    try { m_session.TrySkipPreviousAsync().get(); } catch (...) {}
}

void MediaManager::SeekTo(double seconds)
{
    if (!m_session) return;
    // Fire-and-forget (pas de .get() : ne bloque jamais le thread UI)
    try { m_session.TryChangePlaybackPositionAsync((int64_t)(seconds * 1e7)); }
    catch (...) {}
}

// Filet de sécurité appelé périodiquement par l'île (~2 s) : les events SMTC
// se perdent parfois en fin de piste → resync forcée + re-pick si nécessaire.
void MediaManager::RequestRefresh()
{
    try {
        bool playing = false;
        if (m_session) {
            auto info = m_session.GetPlaybackInfo();
            playing = info && info.PlaybackStatus() ==
                GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
        }
        if (!playing) {
            auto best = PickBestSession();
            if (best && (!m_session ||
                best.SourceAppUserModelId() != m_session.SourceAppUserModelId())) {
                SubscribeToSession(best);
                return;
            }
        }
        UpdateMediaInfoAsync();
    } catch (...) {}
}

bool MediaManager::IsSessionActive() const
{
    return m_session != nullptr;
}

void MediaManager::SubscribeToSession(GlobalSystemMediaTransportControlsSession const& session)
{
    // GARDE CRITIQUE : ne JAMAIS remplacer une session valide par null —
    // sinon plus aucun événement n'arrive et l'affichage reste figé à vie.
    if (!session) return;
    UnsubscribeFromSession();
    m_session = session;

    try {
        m_propertiesChangedToken = m_session.MediaPropertiesChanged({ this, &MediaManager::OnMediaPropertiesChanged });
        m_playbackInfoChangedToken = m_session.PlaybackInfoChanged({ this, &MediaManager::OnPlaybackInfoChanged });
        m_timelineChangedToken = m_session.TimelinePropertiesChanged({ this, &MediaManager::OnTimelinePropertiesChanged });
        UpdateMediaInfoAsync();
    } catch (...) {
        UnsubscribeFromSession();
    }
}

void MediaManager::UnsubscribeFromSession()
{
    if (!m_session) return;

    try {
        if (m_propertiesChangedToken) m_session.MediaPropertiesChanged(m_propertiesChangedToken);
        if (m_playbackInfoChangedToken) m_session.PlaybackInfoChanged(m_playbackInfoChangedToken);
        if (m_timelineChangedToken) m_session.TimelinePropertiesChanged(m_timelineChangedToken);
    } catch (...) {}

    m_propertiesChangedToken = {};
    m_playbackInfoChangedToken = {};
    m_timelineChangedToken = {};
    m_session = nullptr;
}

// La première session EN LECTURE gagne ; sinon la « courante » de Windows.
GlobalSystemMediaTransportControlsSession MediaManager::PickBestSession()
{
    try {
        if (!m_manager) return nullptr;
        auto sessions = m_manager.GetSessions();
        for (auto const& s : sessions) {
            auto info = s.GetPlaybackInfo();
            if (info && info.PlaybackStatus() ==
                GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing)
                return s;
        }
        return m_manager.GetCurrentSession();
    } catch (...) { return nullptr; }
}

void MediaManager::OnSessionChanged(GlobalSystemMediaTransportControlsSessionManager const&, SessionsChangedEventArgs const&)
{
    try {
        if (!m_manager) return;
        auto best = PickBestSession();
        if (best) SubscribeToSession(best);   // null → on garde la session actuelle
    } catch (...) {}
}

void MediaManager::OnMediaPropertiesChanged(GlobalSystemMediaTransportControlsSession const&, MediaPropertiesChangedEventArgs const&)
{
    UpdateMediaInfoAsync();
}

void MediaManager::OnPlaybackInfoChanged(GlobalSystemMediaTransportControlsSession const&, PlaybackInfoChangedEventArgs const&)
{
    // Si la session suivie se met en pause alors qu'une AUTRE joue (ex. Spotify
    // en pause, VLC démarre), on bascule sur celle qui joue.
    try {
        bool playing = false;
        if (m_session) {
            auto info = m_session.GetPlaybackInfo();
            playing = info && info.PlaybackStatus() ==
                GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
        }
        if (!playing) {
            auto best = PickBestSession();
            if (best && m_session &&
                best.SourceAppUserModelId() != m_session.SourceAppUserModelId()) {
                SubscribeToSession(best);
                return;   // SubscribeToSession déclenche déjà UpdateMediaInfoAsync
            }
        }
    } catch (...) {}
    UpdateMediaInfoAsync();
}

void MediaManager::OnTimelinePropertiesChanged(GlobalSystemMediaTransportControlsSession const&, TimelinePropertiesChangedEventArgs const&)
{
    // Volontairement vide : la timeline change ~1×/seconde pendant la lecture.
    // Refaire un UpdateMediaInfoAsync complet (re-fetch pochette inclus) à cette
    // cadence générait un flot d'exceptions WinRT first-chance (0x80040155) et de
    // la charge inutile. La barre de progression avance désormais toute seule
    // (WindowManager::OnAnimTick) et se resynchronise sur changement de piste /
    // play-pause (MediaPropertiesChanged / PlaybackInfoChanged).
}

fire_and_forget MediaManager::UpdateMediaInfoAsync()
{
    if (!m_session || !m_callback) co_return;

    auto session = m_session;

    try {
        auto props = co_await session.TryGetMediaPropertiesAsync();
        if (!props || !m_session) co_return;

        std::wstring title = props.Title().c_str();
        std::wstring artist = props.Artist().c_str();
        std::wstring sourceApp = session.SourceAppUserModelId().c_str();

        auto playbackInfo = session.GetPlaybackInfo();
        bool isPlaying = playbackInfo &&
            (playbackInfo.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing);

        float progress = 0.f;
        float durationSec = 0.f;
        auto timeline = session.GetTimelineProperties();
        if (timeline) {
            auto pos = timeline.Position().count();   // unités 100 ns
            auto end = timeline.EndTime().count();
            if (end > 0) {
                progress = static_cast<float>(pos) / static_cast<float>(end);
                durationSec = static_cast<float>(end) / 1e7f;   // 100 ns → secondes
            }
        }

        std::vector<uint8_t> thumbnailData;
        auto thumbRef = props.Thumbnail();
        if (thumbRef) {
            try {
                auto stream = co_await thumbRef.OpenReadAsync();
                if (stream) {
                    uint32_t size = static_cast<uint32_t>(stream.Size());
                    if (size > 0 && size <= 4 * 1024 * 1024) {
                        thumbnailData.resize(size);
                        winrt::Windows::Storage::Streams::DataReader reader(stream);
                        co_await reader.LoadAsync(size);
                        reader.ReadBytes(thumbnailData);
                    }
                }
            } catch (...) {}
        }

        if (m_callback)
            m_callback(title, artist, sourceApp, isPlaying, progress, durationSec, thumbnailData);
    } catch (const winrt::hresult_error&) {
    } catch (...) {
    }
}
