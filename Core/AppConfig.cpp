#include "AppConfig.h"
#include "SettingsWindow.h"
#include "IslandDim.h"
#include <Windows.h>
#include <shlobj.h>
#include <cstdio>
#include <vector>

// ── Helpers parse JSON plat (clé numérique/booléenne) ────────────────────────
static bool FindKey(const std::string& s, const char* k, size_t& valPos)
{
    std::string pat = "\"" + std::string(k) + "\"";
    size_t p = s.find(pat);
    if (p == std::string::npos) return false;
    p = s.find(':', p + pat.size());
    if (p == std::string::npos) return false;
    ++p;
    while (p < s.size() && (s[p]==' '||s[p]=='\t')) ++p;
    valPos = p;
    return true;
}
static void GetF(const std::string& s, const char* k, float& out)
{
    size_t p; if (FindKey(s, k, p)) out = (float)atof(s.c_str() + p);
}
static void GetI(const std::string& s, const char* k, int& out)
{
    size_t p; if (FindKey(s, k, p)) out = atoi(s.c_str() + p);
}
static void GetB(const std::string& s, const char* k, bool& out)
{
    size_t p; if (FindKey(s, k, p)) out = (s.compare(p, 4, "true") == 0);
}

// ─────────────────────────────────────────────────────────────────────────────
std::wstring AppConfig::PathW()
{
    PWSTR appData = nullptr;
    SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appData);
    std::wstring p = appData ? appData : L".";
    if (appData) CoTaskMemFree(appData);
    p += L"\\Ultraisland";
    CreateDirectoryW(p.c_str(), nullptr);
    p += L"\\config.json";
    return p;
}

bool AppConfig::Load()
{
    HANDLE h = CreateFileW(PathW().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD size = GetFileSize(h, nullptr);
    if (size == 0 || size > 64*1024) { CloseHandle(h); return false; }
    std::string s(size, 0); DWORD rd = 0;
    bool ok = ReadFile(h, s.data(), size, &rd, nullptr) && rd == size;
    CloseHandle(h);
    if (!ok) return false;

    GetB(s, "islandEnabled",     islandEnabled);
    GetB(s, "musicEnabled",      musicEnabled);
    GetB(s, "notifEnabled",      notifEnabled);
    GetB(s, "systemEnabled",     systemEnabled);
    GetB(s, "lockScreenEnabled", lockScreenEnabled);
    GetB(s, "startWithWindows",  startWithWindows);
    GetB(s, "runAsAdmin",        runAsAdmin);
    GetI(s, "shape",             shape);
    GetF(s, "idleW",             idleW);
    GetF(s, "idleH",             idleH);
    GetF(s, "idleCR",            idleCR);
    GetI(s, "position",          position);
    GetF(s, "baseOpacity",       baseOpacity);
    GetB(s, "adaptiveTint",      adaptiveTint);
    GetF(s, "accentR",           accentR);
    GetF(s, "accentG",           accentG);
    GetF(s, "accentB",           accentB);
    GetB(s, "grain",             grain);
    GetB(s, "lockWallpaper",     lockWallpaper);
    GetB(s, "ambientOnUnlock",   ambientOnUnlock);
    GetI(s, "ambientIdleMin",    ambientIdleMin);
    GetI(s, "fontFamily",        fontFamily);
    GetI(s, "fontWeight",        fontWeight);
    GetF(s, "fontScale",         fontScale);
    GetB(s, "soundExpand",       soundExpand);
    GetF(s, "volExpand",         volExpand);
    GetI(s, "soundExpandVar",    soundExpandVar);
    GetB(s, "soundNotif",        soundNotif);
    GetF(s, "volNotif",          volNotif);
    GetI(s, "soundNotifVar",     soundNotifVar);
    GetB(s, "soundDevice",       soundDevice);
    GetF(s, "volDevice",         volDevice);
    GetI(s, "soundDeviceVar",    soundDeviceVar);
    return true;
}

bool AppConfig::Save() const
{
    char buf[2048];
    int n = sprintf_s(buf,
        "{\n"
        "  \"islandEnabled\": %s,\n"
        "  \"musicEnabled\": %s,\n"
        "  \"notifEnabled\": %s,\n"
        "  \"systemEnabled\": %s,\n"
        "  \"lockScreenEnabled\": %s,\n"
        "  \"startWithWindows\": %s,\n"
        "  \"runAsAdmin\": %s,\n"
        "  \"shape\": %d,\n"
        "  \"idleW\": %.1f,\n"
        "  \"idleH\": %.1f,\n"
        "  \"idleCR\": %.1f,\n"
        "  \"position\": %d,\n"
        "  \"baseOpacity\": %.3f,\n"
        "  \"adaptiveTint\": %s,\n"
        "  \"accentR\": %.4f,\n"
        "  \"accentG\": %.4f,\n"
        "  \"accentB\": %.4f,\n"
        "  \"grain\": %s,\n"
        "  \"lockWallpaper\": %s,\n"
        "  \"ambientOnUnlock\": %s,\n"
        "  \"ambientIdleMin\": %d,\n"
        "  \"fontFamily\": %d,\n"
        "  \"fontWeight\": %d,\n"
        "  \"fontScale\": %.2f,\n"
        "  \"soundExpandVar\": %d,\n"
        "  \"soundNotifVar\": %d,\n"
        "  \"soundDeviceVar\": %d,\n"
        "  \"soundExpand\": %s,\n"
        "  \"volExpand\": %.2f,\n"
        "  \"soundNotif\": %s,\n"
        "  \"volNotif\": %.2f,\n"
        "  \"soundDevice\": %s,\n"
        "  \"volDevice\": %.2f\n"
        "}\n",
        islandEnabled?"true":"false", musicEnabled?"true":"false",
        notifEnabled?"true":"false",  systemEnabled?"true":"false",
        lockScreenEnabled?"true":"false", startWithWindows?"true":"false",
        runAsAdmin?"true":"false",
        shape, idleW, idleH, idleCR, position,
        baseOpacity, adaptiveTint?"true":"false",
        accentR, accentG, accentB, grain?"true":"false",
        lockWallpaper?"true":"false",
        ambientOnUnlock?"true":"false",
        ambientIdleMin,
        fontFamily, fontWeight, fontScale,
        soundExpandVar, soundNotifVar, soundDeviceVar,
        soundExpand?"true":"false", volExpand,
        soundNotif?"true":"false",  volNotif,
        soundDevice?"true":"false", volDevice);
    if (n <= 0) return false;

    HANDLE h = CreateFileW(PathW().c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    bool ok = WriteFile(h, buf, (DWORD)n, &wr, nullptr) && wr == (DWORD)n;
    CloseHandle(h);
    return ok;
}

void AppConfig::ApplyRuntime() const
{
    PillRT::W_IDLE     = idleW;
    PillRT::H_IDLE     = idleH;
    PillRT::CR_IDLE    = idleCR;
    PillRT::BASE_ALPHA = baseOpacity;
    PillRT::POS        = position;
    PillRT::ADAPTIVE   = adaptiveTint;
    PillRT::ACC_R      = accentR;
    PillRT::ACC_G      = accentG;
    PillRT::ACC_B      = accentB;
    PillRT::GRAIN      = grain;
    PillRT::FONT_FAM   = fontFamily;
    PillRT::FONT_WT    = fontWeight;
    PillRT::FONT_SCALE = fontScale;
    PillRT::SHAPE      = shape;
}

void AppConfig::FromAppSettings(const AppSettings& s)
{
    islandEnabled     = s.islandEnabled;
    musicEnabled      = s.musicEnabled;
    notifEnabled      = s.notifEnabled;
    systemEnabled     = s.systemEnabled;
    lockScreenEnabled = s.lockScreenEnabled;
    startWithWindows  = s.startWithWindows;
    runAsAdmin        = s.runAsAdmin;
}
