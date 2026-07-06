#include "LyricsClient.h"
#include <Windows.h>
#include <winhttp.h>
#include <thread>
#include <algorithm>
#include <cstdlib>
#pragma comment(lib, "winhttp.lib")

namespace {

std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
std::string UrlEncode(const std::string& s)
{
    std::string out; out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        bool safe = (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')
                    || c=='-' || c=='_' || c=='.' || c=='~';
        if (safe) out.push_back((char)c);
        else { char b[4]; sprintf_s(b, "%%%02X", c); out += b; }
    }
    return out;
}

// Extrait la valeur string de "key" (avec désescape \n \" \\ \uXXXX ignoré)
std::string JsonStr(const std::string& json, const char* key)
{
    std::string pat = "\"" + std::string(key) + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos) return {};
    p = json.find(':', p + pat.size());
    if (p == std::string::npos) return {};
    ++p;
    while (p < json.size() && (unsigned char)json[p] <= ' ') ++p;
    if (p >= json.size() || json[p] != '"') return {};   // null ou autre type
    std::string out; bool esc = false;
    for (++p; p < json.size(); ++p) {
        char c = json[p];
        if (esc) {
            if      (c == 'n') out += '\n';
            else if (c == 't') out += '\t';
            else if (c == 'r') { /* skip */ }
            else               out += c;
            esc = false;
        }
        else if (c == '\\') esc = true;
        else if (c == '"')  break;
        else out += c;
    }
    return out;
}

// Parse LRC : lignes « [mm:ss.xx]texte » (timestamps multiples autorisés)
std::vector<LyricLine> ParseLrc(const std::string& lrcUtf8)
{
    std::vector<LyricLine> out;
    size_t pos = 0;
    while (pos < lrcUtf8.size()) {
        size_t eol = lrcUtf8.find('\n', pos);
        std::string line = lrcUtf8.substr(pos, eol == std::string::npos
                                               ? std::string::npos : eol - pos);
        pos = (eol == std::string::npos) ? lrcUtf8.size() : eol + 1;

        std::vector<float> times;
        size_t i = 0;
        while (i < line.size() && line[i] == '[') {
            size_t close = line.find(']', i);
            if (close == std::string::npos) break;
            std::string tag = line.substr(i + 1, close - i - 1);
            // mm:ss.xx ?
            size_t colon = tag.find(':');
            if (colon != std::string::npos && colon > 0 &&
                isdigit((unsigned char)tag[0])) {
                float mm = (float)atof(tag.substr(0, colon).c_str());
                float ss = (float)atof(tag.substr(colon + 1).c_str());
                times.push_back(mm * 60.f + ss);
            }
            i = close + 1;
        }
        std::string text = line.substr(i);
        while (!text.empty() && (text.back() == '\r' || text.back() == ' '))
            text.pop_back();
        if (!times.empty() && !text.empty()) {
            std::wstring wt = Utf8ToWide(text);
            for (float t : times) out.push_back({t, wt});
        }
    }
    std::sort(out.begin(), out.end(),
              [](const LyricLine& a, const LyricLine& b){ return a.t < b.t; });
    return out;
}

std::string HttpsGet(const std::wstring& host, const std::wstring& path)
{
    std::string body;
    HINTERNET hS = WinHttpOpen(L"Ultraisland/1.0 (Dynamic Island for Windows)",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS) return body;
    HINTERNET hC = WinHttpConnect(hS, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET hR = hC ? WinHttpOpenRequest(hC, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    if (hR && WinHttpSendRequest(hR, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 nullptr, 0, 0, 0)
           && WinHttpReceiveResponse(hR, nullptr)) {
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(hR, &avail) && avail) {
            std::string chunk(avail, 0); DWORD rd = 0;
            if (!WinHttpReadData(hR, chunk.data(), avail, &rd)) break;
            chunk.resize(rd); body += chunk;
        }
    }
    if (hR) WinHttpCloseHandle(hR);
    if (hC) WinHttpCloseHandle(hC);
    if (hS) WinHttpCloseHandle(hS);
    return body;
}

} // namespace

namespace LyricsClient {

void FetchAsync(std::wstring artist, std::wstring title, int durationSec,
                std::function<void(std::vector<LyricLine>)> cb)
{
    if (title.empty() || !cb) return;
    std::thread([artist = std::move(artist), title = std::move(title),
                 durationSec, cb = std::move(cb)]() {
        char durBuf[16]; sprintf_s(durBuf, "%d", durationSec);
        std::string path = "/api/get?track_name=" + UrlEncode(WideToUtf8(title))
                         + "&artist_name="        + UrlEncode(WideToUtf8(artist));
        if (durationSec > 0) path += std::string("&duration=") + durBuf;

        std::string json = HttpsGet(L"lrclib.net", Utf8ToWide(path));
        std::vector<LyricLine> lines;
        if (!json.empty()) {
            std::string lrc = JsonStr(json, "syncedLyrics");
            if (!lrc.empty()) lines = ParseLrc(lrc);
        }
        cb(std::move(lines));
    }).detach();
}

} // namespace LyricsClient
