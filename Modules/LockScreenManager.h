#pragma once
#include <Windows.h>
#include <functional>

// ─────────────────────────────────────────────────────────────────────────────
//  LockScreenManager  — WTSRegisterSessionNotification
// ─────────────────────────────────────────────────────────────────────────────
class LockScreenManager
{
public:
    LockScreenManager();
    ~LockScreenManager();

    void Initialize();
    void Shutdown();

    void SetLockScreenStatusCallback(std::function<void(bool isLocked)> callback);

private:
    std::function<void(bool)> m_callback;
    HWND m_hwnd{ nullptr };

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};
