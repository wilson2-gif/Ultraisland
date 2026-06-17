#include "LockScreenManager.h"
#include <wtsapi32.h>
#pragma comment(lib, "wtsapi32.lib")

LockScreenManager::LockScreenManager() = default;
LockScreenManager::~LockScreenManager() { Shutdown(); }

void LockScreenManager::Initialize()
{
    WNDCLASSEX wc = { sizeof(WNDCLASSEX) };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"LockScreenManagerClass";
    RegisterClassEx(&wc);

    m_hwnd = CreateWindowEx(0, L"LockScreenManagerClass", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, this);

    if (m_hwnd) {
        WTSRegisterSessionNotification(m_hwnd, NOTIFY_FOR_THIS_SESSION);
    }
}

void LockScreenManager::Shutdown()
{
    if (m_hwnd) {
        WTSUnRegisterSessionNotification(m_hwnd);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void LockScreenManager::SetLockScreenStatusCallback(std::function<void(bool)> callback)
{
    m_callback = std::move(callback);
}

LRESULT CALLBACK LockScreenManager::WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    LockScreenManager* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lp);
        self = reinterpret_cast<LockScreenManager*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = reinterpret_cast<LockScreenManager*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (self && msg == WM_WTSSESSION_CHANGE) {
        if (wp == WTS_SESSION_LOCK && self->m_callback) {
            self->m_callback(true);
        } else if (wp == WTS_SESSION_UNLOCK && self->m_callback) {
            self->m_callback(false);
        }
    }

    return DefWindowProc(hwnd, msg, wp, lp);
}
