#pragma once
#include <Windows.h>
#include <string>
#include <functional>
#include <vector>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>

// ─────────────────────────────────────────────────────────────────────────────
//  MediaManager  — SMTC via GlobalSystemMediaTransportControlsSessionManager
// ─────────────────────────────────────────────────────────────────────────────
class MediaManager
{
public:
    MediaManager();
    ~MediaManager();

    void Initialize();
    void Shutdown();

    void PlayPause();
    void SkipNext();
    void SkipPrevious();
    bool IsSessionActive() const;

    void SetMediaChangedCallback(
        std::function<void(const std::wstring& title,
                           const std::wstring& artist,
                           const std::wstring& sourceApp,
                           bool playing,
                           float progress,
                           const std::vector<uint8_t>& thumbnailData)> callback);

private:
    void SubscribeToSession(winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession const& session);
    void UnsubscribeFromSession();
    winrt::fire_and_forget UpdateMediaInfoAsync();

    void OnSessionChanged(winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager const& sender, winrt::Windows::Media::Control::SessionsChangedEventArgs const& args);
    void OnMediaPropertiesChanged(winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession const& sender, winrt::Windows::Media::Control::MediaPropertiesChangedEventArgs const& args);
    void OnPlaybackInfoChanged(winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession const& sender, winrt::Windows::Media::Control::PlaybackInfoChangedEventArgs const& args);
    void OnTimelinePropertiesChanged(winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession const& sender, winrt::Windows::Media::Control::TimelinePropertiesChangedEventArgs const& args);

    std::function<void(const std::wstring&,
                       const std::wstring&,
                       const std::wstring&,
                       bool,
                       float,
                       const std::vector<uint8_t>&)> m_callback;

    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager m_manager{ nullptr };
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession m_session{ nullptr };

    winrt::event_token m_sessionChangedToken;
    winrt::event_token m_propertiesChangedToken;
    winrt::event_token m_playbackInfoChangedToken;
    winrt::event_token m_timelineChangedToken;
};
