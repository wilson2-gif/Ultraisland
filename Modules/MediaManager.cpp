#include "MediaManager.h"
#include <winrt/Windows.Foundation.h>
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
            auto currentSession = m_manager.GetCurrentSession();
            if (currentSession) {
                SubscribeToSession(currentSession);
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
    std::function<void(const std::wstring&, const std::wstring&, const std::wstring&, bool, float, const std::vector<uint8_t>&)> callback)
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

bool MediaManager::IsSessionActive() const
{
    return m_session != nullptr;
}

void MediaManager::SubscribeToSession(GlobalSystemMediaTransportControlsSession const& session)
{
    UnsubscribeFromSession();
    m_session = session;
    if (!m_session) return;

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

void MediaManager::OnSessionChanged(GlobalSystemMediaTransportControlsSessionManager const&, SessionsChangedEventArgs const&)
{
    try {
        if (!m_manager) return;
        auto currentSession = m_manager.GetCurrentSession();
        SubscribeToSession(currentSession);
    } catch (...) {}
}

void MediaManager::OnMediaPropertiesChanged(GlobalSystemMediaTransportControlsSession const&, MediaPropertiesChangedEventArgs const&)
{
    UpdateMediaInfoAsync();
}

void MediaManager::OnPlaybackInfoChanged(GlobalSystemMediaTransportControlsSession const&, PlaybackInfoChangedEventArgs const&)
{
    UpdateMediaInfoAsync();
}

void MediaManager::OnTimelinePropertiesChanged(GlobalSystemMediaTransportControlsSession const&, TimelinePropertiesChangedEventArgs const&)
{
    UpdateMediaInfoAsync();
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
        auto timeline = session.GetTimelineProperties();
        if (timeline) {
            auto pos = timeline.Position().count();
            auto end = timeline.EndTime().count();
            if (end > 0)
                progress = static_cast<float>(pos) / static_cast<float>(end);
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
            m_callback(title, artist, sourceApp, isPlaying, progress, thumbnailData);
    } catch (const winrt::hresult_error&) {
    } catch (...) {
    }
}
