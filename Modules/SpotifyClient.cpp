// winsock2.h DOIT être inclus AVANT Windows.h (sinon winsock.h v1 → conflit).
#include <winsock2.h>
#include <ws2tcpip.h>
#include "SpotifyClient.h"
#include <winhttp.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <shellapi.h>
#include <sstream>
#include <random>
#include <chrono>
#include <regex>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "shell32.lib")

// ── Helpers base64url + SHA256 + random ─────────────────────────────────────
static std::string Base64Url(const std::vector<BYTE>& raw)
{
    DWORD outLen = 0;
    CryptBinaryToStringA(raw.data(), (DWORD)raw.size(),
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &outLen);
    std::string out(outLen, 0);
    CryptBinaryToStringA(raw.data(), (DWORD)raw.size(),
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, out.data(), &outLen);
    out.resize(outLen);
    // base64 → base64url
    for (auto& c : out) { if (c == '+') c = '-'; else if (c == '/') c = '_'; }
    while (!out.empty() && out.back() == '=') out.pop_back();
    return out;
}

static std::vector<BYTE> Sha256(const std::string& s)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    DWORD hashLen = 0, cb = 0;
    BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (PUCHAR)&hashLen, sizeof(hashLen), &cb, 0);
    std::vector<BYTE> hash(hashLen);
    BCRYPT_HASH_HANDLE h = nullptr;
    BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0);
    BCryptHashData(h, (PUCHAR)s.data(), (ULONG)s.size(), 0);
    BCryptFinishHash(h, hash.data(), hashLen, 0);
    BCryptDestroyHash(h);
    BCryptCloseAlgorithmProvider(alg, 0);
    return hash;
}

static std::string RandomVerifier()
{
    // 64 octets aléatoires → base64url (~86 chars). Spotify exige 43..128.
    std::vector<BYTE> raw(64);
    BCryptGenRandom(nullptr, raw.data(), (ULONG)raw.size(), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return Base64Url(raw);
}

static std::string UrlEncode(const std::string& s)
{
    std::string out; out.reserve(s.size()*3);
    for (unsigned char c : s) {
        bool safe = (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')
                    || c=='-' || c=='_' || c=='.' || c=='~';
        if (safe) out.push_back((char)c);
        else { char b[4]; sprintf_s(b, "%%%02X", c); out += b; }
    }
    return out;
}

static std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

// Log de debug → fenêtre « Sortie » de Visual Studio (préfixe [Spotify])
static void Dbg(const std::string& msg)
{
    OutputDebugStringA(("[Spotify] " + msg + "\n").c_str());
}
static std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// ── HTTP helpers (WinHTTP) ──────────────────────────────────────────────────
struct HttpResult { DWORD status = 0; std::string body; };

static HttpResult HttpsRequest(const std::wstring& host, const std::wstring& path,
                                const std::wstring& method,
                                const std::vector<std::pair<std::wstring,std::wstring>>& headers,
                                const std::string& body)
{
    HttpResult r;
    HINTERNET hSess = WinHttpOpen(L"Ultraisland/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSess) return r;
    HINTERNET hCon = WinHttpConnect(hSess, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET hReq = hCon ? WinHttpOpenRequest(hCon, method.c_str(), path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    if (!hReq) { if (hCon) WinHttpCloseHandle(hCon); WinHttpCloseHandle(hSess); return r; }
    std::wstring hdrAll;
    for (auto& kv : headers) hdrAll += kv.first + L": " + kv.second + L"\r\n";
    if (WinHttpSendRequest(hReq,
            hdrAll.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdrAll.c_str(),
            hdrAll.empty() ? 0 : (DWORD)hdrAll.size(),
            (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0)) {
        if (WinHttpReceiveResponse(hReq, nullptr)) {
            DWORD sc = 0, dw = sizeof(sc);
            WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &sc, &dw, WINHTTP_NO_HEADER_INDEX);
            r.status = sc;
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(hReq, &avail) && avail) {
                std::string chunk(avail, 0); DWORD read = 0;
                if (!WinHttpReadData(hReq, chunk.data(), avail, &read)) break;
                chunk.resize(read);
                r.body += chunk;
            }
        }
    }
    WinHttpCloseHandle(hReq); WinHttpCloseHandle(hCon); WinHttpCloseHandle(hSess);
    return r;
}

// ── Mini extracteur de strings/champs JSON (suffit pour /api/token et /queue) ─
static std::string JsonString(const std::string& json, const std::string& key)
{
    // cherche "key" : "..." (gère échappements simples \" et \\)
    std::regex re("\"" + key + "\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
    std::smatch m;
    if (std::regex_search(json, m, re)) {
        std::string v = m[1].str();
        std::string out; out.reserve(v.size());
        for (size_t i = 0; i < v.size(); ++i) {
            if (v[i] == '\\' && i+1 < v.size()) {
                char n = v[i+1];
                if      (n == 'n')  { out += '\n'; ++i; }
                else if (n == 't')  { out += '\t'; ++i; }
                else if (n == '"')  { out += '"';  ++i; }
                else if (n == '\\') { out += '\\'; ++i; }
                else if (n == '/')  { out += '/';  ++i; }
                else out += v[i];
            } else out += v[i];
        }
        return out;
    }
    return {};
}
static int JsonInt(const std::string& json, const std::string& key)
{
    std::regex re("\"" + key + "\"\\s*:\\s*(-?\\d+)");
    std::smatch m;
    if (std::regex_search(json, m, re)) try { return std::stoi(m[1].str()); } catch (...) {}
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
SpotifyClient::SpotifyClient() = default;
SpotifyClient::~SpotifyClient()
{
    // Empêche tout nouveau fetch détaché puis attend ceux en vol : sans ça, un
    // thread RefreshNow survivant touchait m_mtx/m_accessToken/m_queueCb d'un objet
    // détruit → crash à la fermeture de l'app.
    m_shuttingDown.store(true);
    StopPolling();
    for (int i = 0; i < 300 && m_inFlight.load() > 0; ++i) Sleep(10);  // ~3 s max
}

void SpotifyClient::Configure(const std::string& clientId, int loopbackPort)
{
    m_clientId    = clientId;
    m_port        = loopbackPort;
    char rb[64]; sprintf_s(rb, "http://127.0.0.1:%d/callback", m_port);
    m_redirectUri = rb;
}

std::wstring SpotifyClient::TokenFilePath() const
{
    PWSTR appData = nullptr;
    SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appData);
    std::wstring p = appData ? appData : L".";
    if (appData) CoTaskMemFree(appData);
    p += L"\\Ultraisland";
    CreateDirectoryW(p.c_str(), nullptr);
    p += L"\\spotify.dat";
    return p;
}

void SpotifyClient::PersistTokens()
{
    if (m_refreshToken.empty()) return;
    DATA_BLOB in = { (DWORD)m_refreshToken.size(), (BYTE*)m_refreshToken.data() };
    DATA_BLOB out = {};
    if (!CryptProtectData(&in, L"Ultraisland.SpotifyRefresh", nullptr, nullptr, nullptr, 0, &out)) return;
    HANDLE h = CreateFileW(TokenFilePath().c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD wr = 0;
        WriteFile(h, out.pbData, out.cbData, &wr, nullptr);
        CloseHandle(h);
    }
    LocalFree(out.pbData);
}

void SpotifyClient::LoadTokens()
{
    HANDLE h = CreateFileW(TokenFilePath().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD size = GetFileSize(h, nullptr);
    if (size == 0 || size > 16*1024) { CloseHandle(h); return; }
    std::vector<BYTE> buf(size); DWORD rd = 0;
    if (ReadFile(h, buf.data(), size, &rd, nullptr) && rd == size) {
        DATA_BLOB in = { size, buf.data() };
        DATA_BLOB out = {};
        if (CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) {
            m_refreshToken.assign((char*)out.pbData, (char*)out.pbData + out.cbData);
            LocalFree(out.pbData);
        }
    }
    CloseHandle(h);
}

bool SpotifyClient::Restore()
{
    std::lock_guard<std::mutex> lk(m_mtx);
    LoadTokens();
    if (m_refreshToken.empty()) return false;
    return RefreshAccessToken();
}

void SpotifyClient::Logout()
{
    StopPolling();
    std::lock_guard<std::mutex> lk(m_mtx);
    m_refreshToken.clear();
    m_accessToken.clear();
    m_accessExpiresAt = 0;
    DeleteFileW(TokenFilePath().c_str());
}

// ── OAuth Connect : navigateur → loopback → /api/token ──────────────────────
bool SpotifyClient::Connect()
{
    if (m_clientId.empty()) return false;

    std::string verifier = RandomVerifier();
    std::string challenge = Base64Url(Sha256(verifier));

    std::string scope = "user-read-playback-state user-read-currently-playing";
    std::ostringstream url;
    url << "https://accounts.spotify.com/authorize"
        << "?client_id=" << UrlEncode(m_clientId)
        << "&response_type=code"
        << "&redirect_uri=" << UrlEncode(m_redirectUri)
        << "&scope=" << UrlEncode(scope)
        << "&code_challenge_method=S256"
        << "&code_challenge=" << challenge;

    // Démarre listener loopback AVANT d'ouvrir le navigateur
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
    SOCKET srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == INVALID_SOCKET) { WSACleanup(); return false; }
    sockaddr_in addr = {}; addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((u_short)m_port);
    int opt = 1; setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    if (bind(srv, (sockaddr*)&addr, sizeof(addr)) || listen(srv, 1)) {
        closesocket(srv); WSACleanup(); return false;
    }

    // Ouvre le navigateur système
    std::wstring wurl = Utf8ToWide(url.str());
    ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

    // Timeout 90 s pour qu'utilisateur consente
    fd_set fs; FD_ZERO(&fs); FD_SET(srv, &fs);
    timeval tv = { 90, 0 };
    std::string code;
    if (select(0, &fs, nullptr, nullptr, &tv) > 0) {
        SOCKET cli = accept(srv, nullptr, nullptr);
        if (cli != INVALID_SOCKET) {
            char buf[4096] = {};
            int n = recv(cli, buf, sizeof(buf)-1, 0);
            if (n > 0) {
                std::string req(buf, n);
                // Trouver "?code=..."
                auto p = req.find("code=");
                if (p != std::string::npos) {
                    auto e = req.find_first_of(" &", p);
                    code = req.substr(p+5, e == std::string::npos ? std::string::npos : e-(p+5));
                }
                const char* resp =
                    "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                    "Connection: close\r\n\r\n"
                    "<!doctype html><meta charset=utf-8><body style='background:#0c0c10;"
                    "color:#eee;font-family:Segoe UI;text-align:center;padding:40px'>"
                    "<h2>Spotify connecté ✓</h2><p>Vous pouvez fermer cet onglet.</p>";
                send(cli, resp, (int)strlen(resp), 0);
            }
            closesocket(cli);
        }
    }
    closesocket(srv); WSACleanup();
    if (code.empty()) { Dbg("Connect: aucun code reçu (annulé/timeout)"); return false; }
    Dbg("Connect: code reçu, échange en cours…");
    return ExchangeCode(code, verifier);
}

bool SpotifyClient::ExchangeCode(const std::string& code, const std::string& verifier)
{
    std::ostringstream body;
    body << "grant_type=authorization_code"
         << "&code=" << UrlEncode(code)
         << "&redirect_uri=" << UrlEncode(m_redirectUri)
         << "&client_id=" << UrlEncode(m_clientId)
         << "&code_verifier=" << UrlEncode(verifier);
    auto r = HttpsRequest(L"accounts.spotify.com", L"/api/token", L"POST",
        {{L"Content-Type", L"application/x-www-form-urlencoded"}}, body.str());
    Dbg("ExchangeCode: HTTP " + std::to_string(r.status));
    if (r.status != 200) { Dbg("  body: " + r.body.substr(0, 300)); return false; }
    std::lock_guard<std::mutex> lk(m_mtx);
    m_accessToken      = JsonString(r.body, "access_token");
    auto newRefresh    = JsonString(r.body, "refresh_token");
    if (!newRefresh.empty()) m_refreshToken = newRefresh;
    int expires        = JsonInt(r.body, "expires_in");
    m_accessExpiresAt  = GetTickCount() + (expires > 0 ? (DWORD)expires*1000 : 3600u*1000);
    PersistTokens();
    return !m_accessToken.empty();
}

bool SpotifyClient::RefreshAccessToken()
{
    if (m_refreshToken.empty()) return false;
    std::ostringstream body;
    body << "grant_type=refresh_token"
         << "&refresh_token=" << UrlEncode(m_refreshToken)
         << "&client_id=" << UrlEncode(m_clientId);
    auto r = HttpsRequest(L"accounts.spotify.com", L"/api/token", L"POST",
        {{L"Content-Type", L"application/x-www-form-urlencoded"}}, body.str());
    Dbg("RefreshAccessToken: HTTP " + std::to_string(r.status));
    if (r.status != 200) { Dbg("  body: " + r.body.substr(0, 300)); return false; }
    m_accessToken = JsonString(r.body, "access_token");
    auto newRefresh = JsonString(r.body, "refresh_token");
    if (!newRefresh.empty()) m_refreshToken = newRefresh;   // rotation 2024+
    int expires = JsonInt(r.body, "expires_in");
    m_accessExpiresAt = GetTickCount() + (expires > 0 ? (DWORD)expires*1000 : 3600u*1000);
    PersistTokens();
    return !m_accessToken.empty();
}

// ── Extraction à PROFONDEUR 1 d'un objet JSON ────────────────────────────────
// INDISPENSABLE : un track Spotify contient album.artists[].name et album.name
// AVANT son propre "name" → une recherche naïve du 1er "name" renvoie le nom
// d'artiste de l'album au lieu du TITRE (bug « Rubi Rose / Rubi Rose »).
static size_t TopLevelColon(const std::string& obj, const std::string& key)
{
    int depth = 0; bool inStr = false, esc = false, haveKey = false;
    std::string lastStr; size_t strStart = 0;
    for (size_t i = 0; i < obj.size(); ++i) {
        char c = obj[i];
        if (inStr) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') { inStr = false; lastStr.assign(obj, strStart, i - strStart); haveKey = true; }
            continue;
        }
        switch (c) {
        case '"': inStr = true; strStart = i + 1; break;
        case '{': case '[': ++depth; haveKey = false; break;
        case '}': case ']': --depth; haveKey = false; break;
        case ':': if (depth == 1 && haveKey && lastStr == key) return i;
                  haveKey = false; break;
        case ',': haveKey = false; break;
        default: break;
        }
    }
    return std::string::npos;
}

// Valeur string d'une clé de niveau 1 (ex. "name" du track lui-même)
static std::string TopLevelString(const std::string& obj, const std::string& key)
{
    size_t colon = TopLevelColon(obj, key);
    if (colon == std::string::npos) return {};
    size_t i = colon + 1;
    while (i < obj.size() && (unsigned char)obj[i] <= ' ') ++i;
    if (i >= obj.size() || obj[i] != '"') return {};
    std::string v; bool esc = false;
    for (++i; i < obj.size(); ++i) {
        char c = obj[i];
        if (esc) {
            if      (c == 'n') v += '\n';
            else if (c == 't') v += '\t';
            else               v += c;
            esc = false;
        }
        else if (c == '\\') esc = true;
        else if (c == '"')  break;
        else v += c;
    }
    return v;
}

// Texte brut d'un tableau/objet de niveau 1 (ex. "artists":[...] du track)
static std::string TopLevelSpan(const std::string& obj, const std::string& key)
{
    size_t colon = TopLevelColon(obj, key);
    if (colon == std::string::npos) return {};
    size_t i = colon + 1;
    while (i < obj.size() && (unsigned char)obj[i] <= ' ') ++i;
    if (i >= obj.size() || (obj[i] != '[' && obj[i] != '{')) return {};
    size_t start = i; int depth = 0; bool inStr = false, esc = false;
    for (; i < obj.size(); ++i) {
        char c = obj[i];
        if (inStr) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') inStr = true;
        else if (c == '{' || c == '[') ++depth;
        else if (c == '}' || c == ']') { if (--depth == 0) return obj.substr(start, i - start + 1); }
    }
    return {};
}

// Parse /me/player/queue → liste de SpotifyTrack (max 12).
// On isole chaque objet de TOP-NIVEAU du tableau "queue" (suivi de profondeur +
// gestion correcte des chaînes/échappements), puis titre/artiste au NIVEAU 1.
static std::vector<SpotifyTrack> ParseQueue(const std::string& json)
{
    std::vector<SpotifyTrack> out;
    // Localise la CLÉ top-level "queue": [ ...  (pas un titre de chanson "queue")
    size_t qPos = 0, arr = std::string::npos;
    while ((qPos = json.find("\"queue\"", qPos)) != std::string::npos) {
        size_t j = qPos + 7;
        while (j < json.size() && (unsigned char)json[j] <= ' ') ++j;
        if (j < json.size() && json[j] == ':') {
            ++j;
            while (j < json.size() && (unsigned char)json[j] <= ' ') ++j;
            if (j < json.size() && json[j] == '[') { arr = j; break; }
        }
        qPos += 7;
    }
    if (arr == std::string::npos) return out;
    size_t i = arr + 1;  // passe le '['

    int    depth    = 0;
    size_t objStart = std::string::npos;
    bool   inStr = false, esc = false;

    for (; i < json.size(); ++i) {
        char c = json[i];
        if (inStr) {
            if (esc)            esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"')  inStr = false;
            continue;
        }
        if (c == '"') { inStr = true; continue; }
        if (c == '{') { if (depth == 0) objStart = i; ++depth; }
        else if (c == '}') {
            if (--depth == 0 && objStart != std::string::npos) {
                std::string obj = json.substr(objStart, i - objStart + 1);
                SpotifyTrack t;
                // "name" du NIVEAU 1 = titre du track (PAS album.artists[].name)
                t.title = Utf8ToWide(TopLevelString(obj, "name"));
                // "artists" du NIVEAU 1 = artistes du track (PAS ceux de l'album)
                std::string artists = TopLevelSpan(obj, "artists");
                if (!artists.empty())
                    t.artist = Utf8ToWide(JsonString(artists, "name"));
                if (!t.title.empty()) out.push_back(std::move(t));
                objStart = std::string::npos;
                if (out.size() >= 12) break;
            }
        }
        else if (c == ']' && depth == 0) break;  // fin du tableau queue
    }
    Dbg("ParseQueue -> " + std::to_string(out.size()) + " pistes");
    return out;
}

bool SpotifyClient::FetchQueueOnce()
{
    // RACE : m_accessToken peut être lu ici pendant que RefreshAccessToken l'écrit
    // depuis un AUTRE thread (polling + RefreshNow détaché) → lecture d'un std::string
    // en cours de réallocation = UB/crash. On copie TOUJOURS le token sous lock.
    // Comparaison additive (GetTickCount()+30000 >= expiresAt) pour éviter le
    // sous-dépassement unsigned quand m_accessExpiresAt < 30000 (démarrage/wrap).
    std::string tok;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        if (m_accessToken.empty() || GetTickCount() + 30000 >= m_accessExpiresAt) {
            if (!RefreshAccessToken()) return false;
        }
        tok = m_accessToken;
    }
    std::wstring auth = L"Bearer " + Utf8ToWide(tok);
    auto r = HttpsRequest(L"api.spotify.com", L"/v1/me/player/queue", L"GET",
        {{L"Authorization", auth}}, "");
    if (r.status == 401) {
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            if (!RefreshAccessToken()) return false;
            tok = m_accessToken;
        }
        auth = L"Bearer " + Utf8ToWide(tok);
        r = HttpsRequest(L"api.spotify.com", L"/v1/me/player/queue", L"GET",
            {{L"Authorization", auth}}, "");
    }
    Dbg("FetchQueue: HTTP " + std::to_string(r.status));
    if (r.status != 200) {
        // 204 = pas de device actif ; 403 = Premium requis ; 401 = token expiré.
        Dbg("  (204=aucun device actif, 403=Premium requis, 401=token)");
        if (m_queueCb) m_queueCb({});
        return false;
    }
    auto tracks = ParseQueue(r.body);
    if (m_queueCb) m_queueCb(tracks);
    return true;
}

void SpotifyClient::StartPolling(int intervalSec)
{
    if (m_polling.exchange(true)) return;
    int interval = intervalSec < 5 ? 5 : intervalSec;
    m_pollThread = std::thread([this, interval]() {
        while (m_polling.load()) {
            FetchQueueOnce();
            for (int i = 0; i < interval*10 && m_polling.load(); ++i)
                Sleep(100);
        }
    });
}

void SpotifyClient::StopPolling()
{
    if (!m_polling.exchange(false)) return;
    if (m_pollThread.joinable()) m_pollThread.join();
}

void SpotifyClient::RefreshNow()
{
    if (m_refreshToken.empty() || m_shuttingDown.load()) return;
    // Compteur de fetchs en vol : le destructeur attend qu'ils se terminent avant
    // de libérer l'objet (sinon accès à this détruit).
    m_inFlight.fetch_add(1);
    std::thread([this]{
        if (!m_shuttingDown.load()) FetchQueueOnce();
        m_inFlight.fetch_sub(1);
    }).detach();
}
