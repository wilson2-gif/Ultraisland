#include "SystemMonitor.h"
#include <pdh.h>
#include <iphlpapi.h>
#include <psapi.h>
#include <algorithm>
#include <vector>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "psapi.lib")

// ────────────────────────────────────────────────────────────────────────────
SystemMonitor::SystemMonitor()
{
    InitializeCriticalSection(&m_cs);
}

SystemMonitor::~SystemMonitor()
{
    Stop();
    DeleteCriticalSection(&m_cs);
    if (m_pdhQuery) PdhCloseQuery((PDH_HQUERY)m_pdhQuery);
}

// ────────────────────────────────────────────────────────────────────────────
bool SystemMonitor::Start()
{
    // Init PDH pour le CPU
    PDH_HQUERY query;
    if (PdhOpenQuery(nullptr, 0, &query) == ERROR_SUCCESS)
    {
        m_pdhQuery = query;
        PDH_HCOUNTER counter;
        PdhAddEnglishCounter(query, L"\\Processor(_Total)\\% Processor Time",
                             0, &counter);
        m_pdhCounter = counter;
        PdhCollectQueryData(query);  // premier echantillon
    }

    m_stopEvt = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    m_running = true;

    m_thread = CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr);
    return m_thread != nullptr;
}

void SystemMonitor::Stop()
{
    if (m_stopEvt) SetEvent(m_stopEvt);
    if (m_thread)
    {
        WaitForSingleObject(m_thread, 3000);
        CloseHandle(m_thread);
        m_thread = nullptr;
    }
    if (m_stopEvt) { CloseHandle(m_stopEvt); m_stopEvt = nullptr; }
    m_running = false;
}

// ────────────────────────────────────────────────────────────────────────────
SystemStats SystemMonitor::GetStats() const
{
    EnterCriticalSection(const_cast<CRITICAL_SECTION*>(&m_cs));
    SystemStats copy = m_stats;
    LeaveCriticalSection(const_cast<CRITICAL_SECTION*>(&m_cs));
    return copy;
}

// ────────────────────────────────────────────────────────────────────────────
DWORD WINAPI SystemMonitor::ThreadProc(LPVOID param)
{
    reinterpret_cast<SystemMonitor*>(param)->WorkerThread();
    return 0;
}

void SystemMonitor::WorkerThread()
{
    while (WaitForSingleObject(m_stopEvt, 1000) == WAIT_TIMEOUT)
    {
        UpdateCPU();
        UpdateRAM();
        UpdateNetwork();
        UpdateBattery();

        EnterCriticalSection(&m_cs);
        m_stats.timestamp = GetTickCount();
        SystemStats copy  = m_stats;
        LeaveCriticalSection(&m_cs);

        if (m_callback) m_callback(copy);
    }
}

// ────────────────────────────────────────────────────────────────────────────
void SystemMonitor::UpdateCPU()
{
    if (!m_pdhQuery || !m_pdhCounter) return;

    PdhCollectQueryData((PDH_HQUERY)m_pdhQuery);

    PDH_FMT_COUNTERVALUE val;
    if (PdhGetFormattedCounterValue((PDH_HCOUNTER)m_pdhCounter,
                                    PDH_FMT_DOUBLE, nullptr, &val) == ERROR_SUCCESS)
    {
        EnterCriticalSection(&m_cs);
        m_stats.cpuPercent = static_cast<float>(
            std::clamp(val.doubleValue, 0.0, 100.0));
        LeaveCriticalSection(&m_cs);
    }
}

void SystemMonitor::UpdateRAM()
{
    MEMORYSTATUSEX ms = {};
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) return;

    float total = static_cast<float>(ms.ullTotalPhys) / (1024.0f * 1024.0f * 1024.0f);
    float used  = total - static_cast<float>(ms.ullAvailPhys) / (1024.0f * 1024.0f * 1024.0f);

    EnterCriticalSection(&m_cs);
    m_stats.ramTotalGB  = total;
    m_stats.ramUsedGB   = used;
    m_stats.ramPercent  = (total > 0.0f) ? (used / total * 100.0f) : 0.0f;
    LeaveCriticalSection(&m_cs);
}

void SystemMonitor::UpdateNetwork()
{
    // Recuperer les stats de toutes les interfaces
    DWORD size = 0;
    GetIfTable(nullptr, &size, FALSE);

    std::vector<BYTE> buf(size);
    auto* table = reinterpret_cast<MIB_IFTABLE*>(buf.data());

    if (GetIfTable(table, &size, FALSE) != NO_ERROR) return;

    ULONGLONG totalIn = 0, totalOut = 0;
    for (DWORD i = 0; i < table->dwNumEntries; ++i)
    {
        const auto& row = table->table[i];
        // Ignorer loopback et interfaces virtuelles
        if (row.dwType == MIB_IF_TYPE_LOOPBACK) continue;
        totalIn  += row.dwInOctets;
        totalOut += row.dwOutOctets;
    }

    DWORD now = GetTickCount();
    if (m_prevNetTs > 0 && now > m_prevNetTs)
    {
        float dt = (now - m_prevNetTs) / 1000.0f;  // secondes
        EnterCriticalSection(&m_cs);
        m_stats.netDownKBps = static_cast<float>(
            (totalIn  >= m_prevNetIn  ? totalIn  - m_prevNetIn  : 0) / 1024.0f / dt);
        m_stats.netUpKBps   = static_cast<float>(
            (totalOut >= m_prevNetOut ? totalOut - m_prevNetOut : 0) / 1024.0f / dt);
        LeaveCriticalSection(&m_cs);
    }

    m_prevNetIn  = totalIn;
    m_prevNetOut = totalOut;
    m_prevNetTs  = now;
}

void SystemMonitor::UpdateBattery()
{
    SYSTEM_POWER_STATUS ps;
    if (!GetSystemPowerStatus(&ps)) return;

    float pct = (ps.BatteryLifePercent == 255) ? -1.0f
               : static_cast<float>(ps.BatteryLifePercent);
    // SYSTEM_POWER_STATUS::BatteryFlag :
    //   1 High · 2 Low · 4 Critical · 8 CHARGING · 128 No system battery
    // Ancien code : `& 8` était commenté "no battery" et INVERSAIT la logique
    // → la batterie affichait "faible" même en charge. Correction :
    bool hasBattery = (ps.BatteryFlag & 128) == 0;
    bool charging   = hasBattery && ( (ps.BatteryFlag & 8) != 0     // bit charging
                                    || ps.ACLineStatus == 1);       // ou branché

    EnterCriticalSection(&m_cs);
    m_stats.batteryPercent  = pct;
    m_stats.batteryCharging = charging;
    LeaveCriticalSection(&m_cs);
}
