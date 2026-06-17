#pragma once

#include <Windows.h>
#include <string>
#include <functional>

// ────────────────────────────────────────────────────────────────────────────
//  SystemMonitor  — lit CPU, RAM, réseau via PDH / GlobalMemoryStatusEx
//  Mis a jour chaque seconde par un thread de fond.
// ────────────────────────────────────────────────────────────────────────────

struct SystemStats
{
    float cpuPercent     = 0.0f;   // 0-100
    float ramPercent     = 0.0f;   // 0-100
    float ramUsedGB      = 0.0f;
    float ramTotalGB     = 0.0f;
    float netUpKBps      = 0.0f;   // KB/s montant
    float netDownKBps    = 0.0f;   // KB/s descendant
    float batteryPercent = -1.0f;  // -1 si pas de batterie
    bool  batteryCharging = false;
    DWORD timestamp      = 0;      // GetTickCount au moment de la maj
};

class SystemMonitor
{
public:
    SystemMonitor();
    ~SystemMonitor();

    // Demarre le thread de fond (appele une fois)
    bool Start();
    void Stop();

    // Thread-safe : copie atomique des stats courantes
    SystemStats GetStats() const;

    // Callback appele a chaque mise a jour (depuis le thread de fond)
    void SetUpdateCallback(std::function<void(const SystemStats&)> cb)
    {
        m_callback = cb;
    }

private:
    void WorkerThread();
    void UpdateCPU();
    void UpdateRAM();
    void UpdateNetwork();
    void UpdateBattery();

    mutable CRITICAL_SECTION m_cs;
    SystemStats              m_stats;
    HANDLE                   m_thread  = nullptr;
    HANDLE                   m_stopEvt = nullptr;
    bool                     m_running = false;

    // Handles PDH pour CPU
    void*  m_pdhQuery   = nullptr;   // HQUERY — opaque pour eviter #include pdh.h dans le .h
    void*  m_pdhCounter = nullptr;   // HCOUNTER

    // Mesure réseau precedente
    ULONGLONG m_prevNetIn  = 0;
    ULONGLONG m_prevNetOut = 0;
    DWORD     m_prevNetTs  = 0;

    std::function<void(const SystemStats&)> m_callback;

    static DWORD WINAPI ThreadProc(LPVOID param);
};
