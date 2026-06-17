#pragma once
#include <Windows.h>
#include <string>
#include <functional>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Notifications.Management.h>
#include <winrt/Windows.UI.Notifications.h>

// ─────────────────────────────────────────────────────────────────────────────
//  NotificationManager  — WinRT UserNotificationListener
// ─────────────────────────────────────────────────────────────────────────────
class NotificationManager
{
public:
    NotificationManager();
    ~NotificationManager();

    void Initialize();
    void Shutdown();

    void SetNotificationCallback(
        std::function<void(const std::wstring& appName, 
                           const std::wstring& title, 
                           const std::wstring& message)> callback);

private:
    winrt::fire_and_forget InitializeAsync();
    void OnNotificationChanged(winrt::Windows::UI::Notifications::Management::UserNotificationListener const& sender, 
                               winrt::Windows::UI::Notifications::UserNotificationChangedEventArgs const& args);

    std::function<void(const std::wstring&, const std::wstring&, const std::wstring&)> m_callback;
    winrt::Windows::UI::Notifications::Management::UserNotificationListener m_listener{ nullptr };
    winrt::event_token m_notificationChangedToken;
};
