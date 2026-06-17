#include "NotificationManager.h"
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.h>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::UI::Notifications;
using namespace Windows::UI::Notifications::Management;

NotificationManager::NotificationManager() = default;
NotificationManager::~NotificationManager() { Shutdown(); }

void NotificationManager::Initialize()
{
    InitializeAsync();
}

void NotificationManager::Shutdown()
{
    if (m_listener && m_notificationChangedToken) {
        try {
            m_listener.NotificationChanged(m_notificationChangedToken);
        } catch (...) {}
        m_notificationChangedToken = {};
    }
    m_listener = nullptr;
}

void NotificationManager::SetNotificationCallback(
    std::function<void(const std::wstring&, const std::wstring&, const std::wstring&)> callback)
{
    m_callback = std::move(callback);
}

fire_and_forget NotificationManager::InitializeAsync()
{
    try {
        m_listener = UserNotificationListener::Current();
        auto accessStatus = co_await m_listener.RequestAccessAsync();

        if (accessStatus == UserNotificationListenerAccessStatus::Allowed) {
            m_notificationChangedToken = m_listener.NotificationChanged(
                { this, &NotificationManager::OnNotificationChanged });
        }
    } catch (const winrt::hresult_error&) {
        m_listener = nullptr;
    } catch (...) {
        m_listener = nullptr;
    }
}

void NotificationManager::OnNotificationChanged(UserNotificationListener const& sender, UserNotificationChangedEventArgs const& args)
{
    if (!m_callback || args.ChangeKind() != UserNotificationChangedKind::Added) return;

    try {
        auto notif = sender.GetNotification(args.UserNotificationId());
        if (!notif) return;

        std::wstring appName = notif.AppInfo().DisplayInfo().DisplayName().c_str();

        auto binding = notif.Notification().Visual().GetBinding(KnownNotificationBindings::ToastGeneric());
        if (binding) {
            auto elements = binding.GetTextElements();
            std::wstring title, message;

            if (elements.Size() > 0)
                title = elements.GetAt(0).Text().c_str();
            if (elements.Size() > 1)
                message = elements.GetAt(1).Text().c_str();

            m_callback(appName, title, message);
        }
    } catch (const winrt::hresult_error&) {
    } catch (...) {
    }
}
