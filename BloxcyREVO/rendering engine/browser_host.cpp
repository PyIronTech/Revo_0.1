#include "browser_host.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#endif
#include <WebView2.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include <sqlite3.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <cstring>
#include <fstream>
#include <functional>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "windowscodecs.lib")
#endif

namespace Bloxcy {
namespace {

constexpr int kExpectedSiteCount = 1000000;
template <typename Interface, typename... Args>
class CallbackHandler final : public Interface {
public:
    CallbackHandler(REFIID interfaceIid, std::function<HRESULT(Args...)> callback)
        : m_referenceCount(1), m_interfaceIid(interfaceIid), m_callback(std::move(callback)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, m_interfaceIid)) {
            *object = static_cast<Interface*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&m_referenceCount));
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG references = static_cast<ULONG>(InterlockedDecrement(&m_referenceCount));
        if (references == 0) delete this;
        return references;
    }

    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override {
        return m_callback(args...);
    }

private:
    LONG m_referenceCount;
    IID m_interfaceIid;
    std::function<HRESULT(Args...)> m_callback;
};

std::wstring GetAppDataDirectory() {
    DWORD length = GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
    if (length == 0) return {};

    std::wstring path(length, L'\0');
    const DWORD written = GetEnvironmentVariableW(L"APPDATA", path.data(), length);
    if (written == 0 || written >= length) return {};
    path.resize(written);

    path += L"\\BloxcyREVO";
    if (!CreateDirectoryW(path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        return {};
    }
    return path;
}

std::wstring GetExecutableDirectory() {
    std::wstring path(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return {};
    path.resize(length);
    const size_t separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos) return {};
    path.resize(separator);
    return path;
}

std::wstring Trim(const std::wstring& value) {
    const size_t first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    const size_t last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::wstring EncodeQueryParameter(const std::wstring& value) {
    if (value.empty()) return {};
    const int byteCount = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (byteCount <= 0) return {};
    std::string utf8(static_cast<size_t>(byteCount), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), utf8.data(), byteCount, nullptr, nullptr) != byteCount) {
        return {};
    }

    static constexpr wchar_t hex[] = L"0123456789ABCDEF";
    std::wstring encoded;
    for (unsigned char byte : utf8) {
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' ||
            byte == '.' || byte == '~') {
            encoded += static_cast<wchar_t>(byte);
        } else {
            encoded += L'%';
            encoded += hex[byte >> 4];
            encoded += hex[byte & 0x0f];
        }
    }
    return encoded;
}

bool IsSupportedSearchEngine(const std::wstring& engine) {
    static const std::set<std::wstring> engines = {
        L"bloxcy", L"google", L"bing", L"duckduckgo", L"yahoo", L"brave",
        L"ecosia", L"startpage", L"qwant", L"mojeek", L"kagi", L"swisscows",
        L"youtube", L"wikipedia"
    };
    return engines.find(engine) != engines.end();
}

std::wstring GetSearchEngineUrl(const std::wstring& engine, const std::wstring& query) {
    const std::wstring encoded = EncodeQueryParameter(query);
    if (engine == L"google") return L"https://www.google.com/search?q=" + encoded;
    if (engine == L"bing") return L"https://www.bing.com/search?q=" + encoded;
    if (engine == L"duckduckgo") return L"https://duckduckgo.com/?q=" + encoded;
    if (engine == L"yahoo") return L"https://search.yahoo.com/search?p=" + encoded;
    if (engine == L"brave") return L"https://search.brave.com/search?q=" + encoded;
    if (engine == L"ecosia") return L"https://www.ecosia.org/search?q=" + encoded;
    if (engine == L"startpage") return L"https://www.startpage.com/sp/search?query=" + encoded;
    if (engine == L"qwant") return L"https://www.qwant.com/?q=" + encoded;
    if (engine == L"mojeek") return L"https://www.mojeek.com/search?q=" + encoded;
    if (engine == L"kagi") return L"https://kagi.com/search?q=" + encoded;
    if (engine == L"swisscows") return L"https://swisscows.com/en/web?query=" + encoded;
    if (engine == L"youtube") return L"https://www.youtube.com/results?search_query=" + encoded;
    if (engine == L"wikipedia") return L"https://en.wikipedia.org/w/index.php?search=" + encoded;
    return {};
}

std::wstring ExtractDictionaryWord(const std::wstring& query) {
    static const std::wregex singleWord(
        LR"(^\s*([a-z][a-z'-]{0,79})\s*[?!.,]*\s*$)", std::regex_constants::icase);
    static const std::wregex wordMeaning(
        LR"(^\s*\(?([a-z][a-z'-]{0,79})\)?\s+(?:meaning|definition)\s*[?!.,]*\s*$)",
        std::regex_constants::icase);
    static const std::wregex defineWord(
        LR"(^\s*(?:define|meaning\s+of|definition\s+of)\s+\(?([a-z][a-z'-]{0,79})\)?\s*[?!.,]*\s*$)",
        std::regex_constants::icase);
    static const std::wregex whatMeans(
        LR"(^\s*what\s+does\s+\(?([a-z][a-z'-]{0,79})\)?\s+mean\s*[?!.,]*\s*$)",
        std::regex_constants::icase);
    static const std::wregex whatIs(
        LR"(^\s*what\s+is\s+\(?([a-z][a-z'-]{0,79})\)?\s*[?!.,]*\s*$)",
        std::regex_constants::icase);

    std::wsmatch match;
    for (const std::wregex* pattern : { &singleWord, &wordMeaning, &defineWord, &whatMeans, &whatIs }) {
        if (std::regex_match(query, match, *pattern)) {
            std::wstring word = match[1].str();
            std::transform(word.begin(), word.end(), word.begin(), towlower);
            return word;
        }
    }
    return {};
}

bool IsOneEditAway(const std::wstring& first, const std::wstring& second) {
    if (first == second) return false;
    if (first.size() + 1 < second.size() || second.size() + 1 < first.size()) return false;
    if (first.size() == second.size()) {
        std::vector<size_t> mismatches;
        for (size_t index = 0; index < first.size(); ++index) {
            if (first[index] != second[index]) mismatches.push_back(index);
        }
        if (mismatches.size() == 2 && mismatches[1] == mismatches[0] + 1 &&
            first[mismatches[0]] == second[mismatches[1]] &&
            first[mismatches[1]] == second[mismatches[0]]) {
            return true;
        }
    }
    size_t left = 0, right = 0, edits = 0;
    while (left < first.size() && right < second.size()) {
        if (first[left] == second[right]) {
            ++left;
            ++right;
            continue;
        }
        if (++edits > 1) return false;
        if (first.size() > second.size()) ++left;
        else if (second.size() > first.size()) ++right;
        else {
            ++left;
            ++right;
        }
    }
    return edits + (left < first.size() || right < second.size() ? 1 : 0) == 1;
}

std::wstring GetHttpsOrigin(const std::wstring& url) {
    if (url.size() < 8 || CompareStringOrdinal(url.data(), 8, L"https://", 8, TRUE) != CSTR_EQUAL) {
        return {};
    }
    const size_t authorityStart = 8;
    const size_t authorityEnd = url.find_first_of(L"/?#", authorityStart);
    std::wstring authority = url.substr(authorityStart,
        authorityEnd == std::wstring::npos ? std::wstring::npos : authorityEnd - authorityStart);
    const size_t credentials = authority.rfind(L'@');
    if (credentials != std::wstring::npos) authority.erase(0, credentials + 1);
    if (authority.empty()) return {};
    std::transform(authority.begin(), authority.end(), authority.begin(), towlower);
    return L"https://" + authority;
}

bool DecodeUriComponent(const std::wstring& encoded, std::wstring& decoded) {
    std::string bytes;
    struct ByteWiper {
        std::string& value;
        ~ByteWiper() {
            if (!value.empty()) SecureZeroMemory(value.data(), value.size());
        }
    } byteWiper{ bytes };
    bytes.reserve(encoded.size());
    for (size_t index = 0; index < encoded.size(); ++index) {
        const wchar_t character = encoded[index];
        if (character == L'%') {
            if (index + 2 >= encoded.size()) return false;
            auto hexValue = [](wchar_t digit) -> int {
                if (digit >= L'0' && digit <= L'9') return digit - L'0';
                if (digit >= L'a' && digit <= L'f') return digit - L'a' + 10;
                if (digit >= L'A' && digit <= L'F') return digit - L'A' + 10;
                return -1;
            };
            const int high = hexValue(encoded[index + 1]);
            const int low = hexValue(encoded[index + 2]);
            if (high < 0 || low < 0) return false;
            bytes.push_back(static_cast<char>((high << 4) | low));
            index += 2;
        } else {
            if (character > 0x7f) return false;
            bytes.push_back(static_cast<char>(character));
        }
    }
    if (bytes.empty()) {
        decoded.clear();
        return true;
    }
    const int wideLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (wideLength <= 0) return false;
    decoded.resize(static_cast<size_t>(wideLength));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
        static_cast<int>(bytes.size()), decoded.data(), wideLength) != 0;
}

std::wstring EscapeJavaScriptString(const std::wstring& value) {
    std::wstring result = L"\"";
    for (wchar_t character : value) {
        switch (character) {
            case L'"': result += L"\\\""; break;
            case L'\\': result += L"\\\\"; break;
            case L'\b': result += L"\\b"; break;
            case L'\f': result += L"\\f"; break;
            case L'\n': result += L"\\n"; break;
            case L'\r': result += L"\\r"; break;
            case L'\t': result += L"\\t"; break;
            case L'<': result += L"\\u003c"; break;
            case L'>': result += L"\\u003e"; break;
            case L'&': result += L"\\u0026"; break;
            default:
                if (character < 0x20) {
                    wchar_t escape[7] = {};
                    swprintf_s(escape, L"\\u%04x", static_cast<unsigned int>(character));
                    result += escape;
                } else {
                    result += character;
                }
                break;
        }
    }
    result += L'"';
    return result;
}

void SecureClearString(std::wstring& value) {
    if (!value.empty()) SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
    value.clear();
}

std::wstring DecodeWebMessage(const wchar_t* json) {
    if (!json || json[0] != L'"') return {};
    std::wstring message;
    for (size_t index = 1; json[index] && json[index] != L'"'; ++index) {
        if (json[index] != L'\\') {
            message += json[index];
            continue;
        }

        const wchar_t escaped = json[++index];
        switch (escaped) {
            case L'"': message += L'"'; break;
            case L'\\': message += L'\\'; break;
            case L'/': message += L'/'; break;
            case L'b': message += L'\b'; break;
            case L'f': message += L'\f'; break;
            case L'n': message += L'\n'; break;
            case L'r': message += L'\r'; break;
            case L't': message += L'\t'; break;
            case L'u': {
                wchar_t codePoint = 0;
                for (int digit = 0; digit < 4; ++digit) {
                    const wchar_t hex = json[++index];
                    codePoint = static_cast<wchar_t>(codePoint << 4);
                    if (hex >= L'0' && hex <= L'9') codePoint += hex - L'0';
                    else if (hex >= L'a' && hex <= L'f') codePoint += hex - L'a' + 10;
                    else if (hex >= L'A' && hex <= L'F') codePoint += hex - L'A' + 10;
                }
                message += codePoint;
                break;
            }
            default:
                return {};
        }
    }
    return message;
}

bool LooksLikeUrl(const std::wstring& input) {
    if (input.find(L' ') != std::wstring::npos || input.find(L'\t') != std::wstring::npos) {
        return false;
    }
    if (input.find(L"://") != std::wstring::npos) return true;
    std::wstring lower = input;
    std::transform(lower.begin(), lower.end(), lower.begin(), towlower);
    if (lower.compare(0, 9, L"localhost") == 0) return true;
    const size_t dot = input.find(L'.');
    return dot != std::wstring::npos && dot > 0 && dot + 1 < input.size();
}

std::wstring SqliteError(sqlite3* database, int result) {
    const char* message = database ? sqlite3_errmsg(database) : sqlite3_errstr(result);
    const int length = MultiByteToWideChar(CP_UTF8, 0, message, -1, nullptr, 0);
    if (length <= 0) return L"Unknown SQLite error.";
    std::wstring wideMessage(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, message, -1, wideMessage.data(), length);
    wideMessage.resize(static_cast<size_t>(length - 1));
    return wideMessage;
}

int BrowserModeIndex(const std::wstring& mode) {
    if (mode == L"wellness") return kBrowserModeWellness;
    if (mode == L"gaming") return kBrowserModeGaming;
    if (mode == L"normal") return kBrowserModeNormal;
    if (mode == L"privacy") return kBrowserModePrivacy;
    return -1;
}

bool IsBlockedByWellnessFilter(const std::wstring& url) {
    static const wchar_t* const blockedDomains[] = {
        L"doubleclick.net", L"googlesyndication.com", L"googleadservices.com",
        L"adservice.google.com", L"adnxs.com", L"adsrvr.org", L"adform.net",
        L"adroll.com", L"ads-twitter.com", L"amazon-adsystem.com",
        L"taboola.com", L"outbrain.com", L"criteo.com", L"quantserve.com",
        L"scorecardresearch.com", L"hotjar.com", L"mixpanel.com",
        L"segment.io", L"segment.com", L"connect.facebook.net"
    };
    const size_t scheme = url.find(L"://");
    if (scheme == std::wstring::npos) return false;
    const size_t hostStart = scheme + 3;
    const size_t hostEnd = url.find_first_of(L"/:?#", hostStart);
    if (hostEnd == hostStart) return false;
    std::wstring host = url.substr(hostStart,
        hostEnd == std::wstring::npos ? std::wstring::npos : hostEnd - hostStart);
    std::transform(host.begin(), host.end(), host.begin(), towlower);
    for (const wchar_t* domain : blockedDomains) {
        const size_t domainLength = wcslen(domain);
        if (host == domain ||
            (host.size() > domainLength &&
             host.compare(host.size() - domainLength, domainLength, domain) == 0 &&
             host[host.size() - domainLength - 1] == L'.')) {
            return true;
        }
    }
    return false;
}

std::vector<std::wstring> SplitPreferenceGoals(const std::wstring& value) {
    std::vector<std::wstring> goals;
    size_t start = 0;
    while (start < value.size()) {
        const size_t separator = value.find(L',', start);
        const std::wstring goal = value.substr(start,
            separator == std::wstring::npos ? std::wstring::npos : separator - start);
        if (!goal.empty()) goals.push_back(goal);
        if (separator == std::wstring::npos) break;
        start = separator + 1;
    }
    return goals;
}

bool StartsWithHttpUrl(const std::wstring& url) {
    const size_t prefixLength = url.size() >= 8 &&
        CompareStringOrdinal(url.c_str(), 8, L"https://", 8, TRUE) == CSTR_EQUAL ? 8 :
        url.size() >= 7 && CompareStringOrdinal(url.c_str(), 7, L"http://", 7, TRUE) == CSTR_EQUAL ? 7 : 0;
    if (prefixLength == 0 || url.size() <= prefixLength ||
        url.find_first_of(L"\r\n\t ") != std::wstring::npos) return false;
    const size_t hostEnd = url.find_first_of(L"/?#", prefixLength);
    return (hostEnd == std::wstring::npos ? url.size() : hostEnd) > prefixLength;
}

bool StartsWithFileUrl(const std::wstring& url) {
    return url.size() >= 8 &&
        CompareStringOrdinal(url.c_str(), 8, L"file:///", 8, TRUE) == CSTR_EQUAL;
}

bool ResolveLocalFilePath(const std::wstring& input, std::wstring& path,
                          std::wstring& fileUrl) {
    path.clear();
    fileUrl.clear();
    if (input.empty() || input.find_first_of(L"\r\n\t") != std::wstring::npos) return false;

    if (StartsWithFileUrl(input)) {
        std::wstring url = input;
        const size_t suffix = url.find_first_of(L"?#", 8);
        if (suffix != std::wstring::npos) url.resize(suffix);
        std::vector<wchar_t> decoded(url.size() + 2, L'\0');
        DWORD decodedLength = static_cast<DWORD>(decoded.size());
        if (FAILED(PathCreateFromUrlW(url.c_str(), decoded.data(), &decodedLength, 0))) return false;
        path.assign(decoded.data());
    } else {
        if (PathIsRelativeW(input.c_str()) || PathIsUNCW(input.c_str())) return false;
        path = input;
    }

    if (PathIsUNCW(path.c_str())) return false;
    const DWORD fullPathLength = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (fullPathLength == 0) return false;
    std::vector<wchar_t> fullPath(static_cast<size_t>(fullPathLength) + 1, L'\0');
    const DWORD fullPathWritten = GetFullPathNameW(
        path.c_str(), static_cast<DWORD>(fullPath.size()), fullPath.data(), nullptr);
    if (fullPathWritten == 0 || fullPathWritten >= fullPath.size()) return false;
    path.assign(fullPath.data(), fullPathWritten);

    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return false;

    std::vector<wchar_t> encoded(path.size() * 12 + 32, L'\0');
    DWORD encodedLength = static_cast<DWORD>(encoded.size());
    if (FAILED(UrlCreateFromPathW(path.c_str(), encoded.data(), &encodedLength, 0))) return false;
    fileUrl.assign(encoded.data());
    return StartsWithFileUrl(fileUrl);
}

bool ParseHexColor(const std::wstring& value, COLORREF& color) {
    if (value.size() != 7 || value[0] != L'#') return false;
    unsigned int red = 0, green = 0, blue = 0;
    if (swscanf_s(value.c_str() + 1, L"%2x%2x%2x", &red, &green, &blue) != 3) return false;
    color = RGB(red, green, blue);
    return true;
}

HBITMAP CreateBitmapFromPngStream(IStream* stream) {
    if (!stream) return nullptr;
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    HBITMAP image = nullptr;
    HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&factory));
    if (SUCCEEDED(result)) {
        result = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
    }
    if (SUCCEEDED(result)) result = decoder->GetFrame(0, &frame);
    UINT width = 0, height = 0;
    if (SUCCEEDED(result)) result = frame->GetSize(&width, &height);
    if (SUCCEEDED(result) && (width == 0 || height == 0 || width > 512 || height > 512)) {
        result = E_INVALIDARG;
    }
    if (SUCCEEDED(result)) result = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(result)) {
        result = converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    }
    if (SUCCEEDED(result)) {
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = static_cast<LONG>(width);
        info.bmiHeader.biHeight = -static_cast<LONG>(height);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        image = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!image || !pixels) {
            if (image) DeleteObject(image);
            image = nullptr;
            result = E_OUTOFMEMORY;
        } else {
            result = converter->CopyPixels(nullptr, width * 4, width * height * 4,
                static_cast<BYTE*>(pixels));
            if (FAILED(result)) {
                DeleteObject(image);
                image = nullptr;
            }
        }
    }
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();
    return image;
}

} // namespace

BrowserHost::BrowserHost()
    : m_parent(nullptr)
    , m_loaderModule(nullptr)
    , m_environment(nullptr)
    , m_controller(nullptr)
    , m_webView(nullptr)
    , m_database(nullptr)
    , m_searchBackendProcess(nullptr)
    , m_searchBackendPort(0)
    , m_accountStore()
    , m_account()
    , m_themeName(L"obsidian")
    , m_browserMode(L"normal")
    , m_trustDetails(L"Trust signals are calculated for HTTP and HTTPS websites.")
    , m_trustScore(-1)
    , m_isHomePage(true)
    , m_isSearchPage(false)
    , m_isSettingsPage(false)
    , m_isOnboardingPage(false)
    , m_isHibernating(false)
    , m_pendingNavigate(false)
    , m_pendingHome(true)
    , m_pendingSearch(false)
    , m_forceDark(false)
    , m_onboardingCompleted(false)
    , m_comInitialized(false)
    , m_shutdown(false) {}

BrowserHost::~BrowserHost() {
    Shutdown();
}

bool BrowserHost::Initialize(HWND parent, const std::wstring& appDirectory,
                             const AccountSession& session) {
    m_parent = parent;
    m_shutdown = false;
    m_account.id = session.id;
    m_account.username = session.username;
    m_account.profileDirectory = session.profileDirectory;
    m_account.vaultKey = session.vaultKey;
    m_preferencesPath = m_account.profileDirectory + L"\\preferences.ini";
    const bool previousOnboardingCompleted =
        GetPrivateProfileIntW(L"Onboarding", L"Completed", 0, m_preferencesPath.c_str()) == 1;
    wchar_t preferenceValue[2048] = {};
    GetPrivateProfileStringW(L"Preferences", L"Mode", L"",
        preferenceValue, static_cast<DWORD>(sizeof(preferenceValue) / sizeof(preferenceValue[0])),
        m_preferencesPath.c_str());
    const bool hasSavedBrowserMode = preferenceValue[0] != L'\0';
    m_browserMode = preferenceValue;
    const bool hasValidBrowserMode = BrowserModeIndex(m_browserMode) >= 0;
    m_onboardingCompleted = previousOnboardingCompleted && hasSavedBrowserMode && hasValidBrowserMode;
    if (!hasValidBrowserMode) m_browserMode = L"normal";
    GetPrivateProfileStringW(L"Preferences", L"Goals", L"",
        preferenceValue, static_cast<DWORD>(sizeof(preferenceValue) / sizeof(preferenceValue[0])),
        m_preferencesPath.c_str());
    m_preferenceGoals = preferenceValue;
    const std::vector<std::wstring> savedGoals = SplitPreferenceGoals(m_preferenceGoals);
    m_forceDark = std::find(savedGoals.begin(), savedGoals.end(), L"visual-comfort") != savedGoals.end();
    if (!InitializeSearchDatabase(appDirectory)) return false;

    const std::wstring settingsPath = appDirectory + L"\\ui\\settings\\index.html";
    DWORD settingsUriLength = 32768;
    m_settingsUri.assign(settingsUriLength, L'\0');
    const HRESULT settingsPathResult = UrlCreateFromPathW(
        settingsPath.c_str(), m_settingsUri.data(), &settingsUriLength, 0);
    if (FAILED(settingsPathResult)) {
        ShowError(L"resolve the settings page path", settingsPathResult);
        return false;
    }
    m_settingsUri.resize(settingsUriLength);

    const std::wstring resultsPath = appDirectory + L"\\src\\search_engine\\results.html";
    DWORD resultsUriLength = 32768;
    m_resultsUri.assign(resultsUriLength, L'\0');
    const HRESULT resultsPathResult = UrlCreateFromPathW(
        resultsPath.c_str(), m_resultsUri.data(), &resultsUriLength, 0);
    if (FAILED(resultsPathResult)) {
        ShowError(L"resolve the search results page path", resultsPathResult);
        return false;
    }
    m_resultsUri.resize(resultsUriLength);

    const std::wstring onboardingPath = appDirectory + L"\\ui\\onboarding\\index.html";
    DWORD onboardingUriLength = 32768;
    m_onboardingUri.assign(onboardingUriLength, L'\0');
    const HRESULT onboardingPathResult = UrlCreateFromPathW(
        onboardingPath.c_str(), m_onboardingUri.data(), &onboardingUriLength, 0);
    if (FAILED(onboardingPathResult)) {
        ShowError(L"resolve the first-run preferences page path", onboardingPathResult);
        return false;
    }
    m_onboardingUri.resize(onboardingUriLength);

    const std::wstring webViewData = m_account.profileDirectory + L"\\WebView2";
    if (!CreateDirectoryW(webViewData.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        MessageBoxW(parent, L"Could not create the WebView2 profile folder.",
                    L"Bloxcy REVO", MB_OK | MB_ICONERROR);
        return false;
    }

    const std::wstring loaderPath = appDirectory + L"\\WebView2Loader.dll";
    m_loaderModule = LoadLibraryW(loaderPath.c_str());
    if (!m_loaderModule) {
        MessageBoxW(parent, L"WebView2Loader.dll is missing. Rebuild the app using build.bat.",
                    L"Bloxcy REVO", MB_OK | MB_ICONERROR);
        return false;
    }

    using CreateEnvironmentFunction = HRESULT (STDAPICALLTYPE*)(
        PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
        ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    CreateEnvironmentFunction createEnvironment = nullptr;
    FARPROC createEnvironmentAddress = GetProcAddress(m_loaderModule, "CreateCoreWebView2EnvironmentWithOptions");
    static_assert(sizeof(createEnvironment) == sizeof(createEnvironmentAddress));
    std::memcpy(&createEnvironment, &createEnvironmentAddress, sizeof(createEnvironment));
    if (!createEnvironment) {
        MessageBoxW(parent, L"The installed WebView2 loader does not support environment creation.",
                    L"Bloxcy REVO", MB_OK | MB_ICONERROR);
        return false;
    }

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult)) {
        ShowError(L"initialize COM", comResult);
        return false;
    }
    m_comInitialized = true;

    NavigateHome();
    const std::weak_ptr<BrowserHost> weakSelf = weak_from_this();
    auto* callback = new CallbackHandler<
        ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
        HRESULT, ICoreWebView2Environment*>(
            IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
            [weakSelf](HRESULT error, ICoreWebView2Environment* environment) {
                const auto self = weakSelf.lock();
                if (!self || self->m_shutdown) return S_OK;
                if (FAILED(error) || !environment) {
                    self->HandleEnvironmentError(FAILED(error) ? error : E_FAIL);
                    return S_OK;
                }
                self->CreateController(environment);
                return S_OK;
            });

    const HRESULT result = createEnvironment(nullptr, webViewData.c_str(), nullptr, callback);
    callback->Release();
    if (FAILED(result)) {
        ShowError(L"start WebView2", result);
        return false;
    }
    return true;
}

bool BrowserHost::EnsureSearchBackend(std::wstring& error) {
    if (m_searchBackendProcess &&
        WaitForSingleObject(m_searchBackendProcess, 0) == WAIT_TIMEOUT &&
        m_searchBackendPort != 0 && !m_searchBackendToken.empty()) {
        return true;
    }
    if (m_searchBackendProcess) {
        CloseHandle(m_searchBackendProcess);
        m_searchBackendProcess = nullptr;
    }
    m_searchBackendPort = 0;
    m_searchBackendToken.clear();

    const std::wstring appDirectory = GetExecutableDirectory();
    std::wstring pythonPath;
    DWORD envLength = GetEnvironmentVariableW(L"BLOXCY_PYTHON", nullptr, 0);
    if (envLength > 1) {
        pythonPath.resize(envLength);
        const DWORD written = GetEnvironmentVariableW(
            L"BLOXCY_PYTHON", pythonPath.data(), envLength);
        if (written > 0 && written < envLength) {
            pythonPath.resize(written);
        } else {
            pythonPath.clear();
        }
    }
    if (pythonPath.empty()) {
        const std::wstring localPython = appDirectory + L"\\.venv\\Scripts\\python.exe";
        if (GetFileAttributesW(localPython.c_str()) != INVALID_FILE_ATTRIBUTES) {
            pythonPath = localPython;
        }
    }
    if (pythonPath.empty()) {
        const size_t parentSeparator = appDirectory.find_last_of(L"\\/");
        if (parentSeparator != std::wstring::npos) {
            const std::wstring parentPython =
                appDirectory.substr(0, parentSeparator) + L"\\.venv\\Scripts\\python.exe";
            if (GetFileAttributesW(parentPython.c_str()) != INVALID_FILE_ATTRIBUTES) {
                pythonPath = parentPython;
            }
        }
    }
    if (pythonPath.empty()) {
        for (const wchar_t* executable : { L"python.exe", L"python3.exe" }) {
            std::wstring candidate(32768, L'\0');
            const DWORD length = SearchPathW(nullptr, executable, nullptr,
                static_cast<DWORD>(candidate.size()), candidate.data(), nullptr);
            if (length > 0 && length < candidate.size()) {
                candidate.resize(length);
                pythonPath = std::move(candidate);
                break;
            }
        }
    }
    if (pythonPath.empty() || GetFileAttributesW(pythonPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = L"Python was not found. Install Python and add python.exe to PATH, or set BLOXCY_PYTHON to its full path.";
        return false;
    }

    const std::wstring serverPath = appDirectory + L"\\src\\apis\\search_server.py";
    if (appDirectory.empty() || GetFileAttributesW(serverPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = L"The local search provider is missing. Rebuild the app and include src\\apis\\search_server.py.";
        return false;
    }

    auto quoteArgument = [](const std::wstring& argument) {
        std::wstring quoted = L"\"";
        size_t backslashes = 0;
        for (wchar_t character : argument) {
            if (character == L'\\') {
                ++backslashes;
            } else if (character == L'"') {
                quoted.append(backslashes * 2 + 1, L'\\');
                quoted += L'"';
                backslashes = 0;
            } else {
                quoted.append(backslashes, L'\\');
                backslashes = 0;
                quoted += character;
            }
        }
        quoted.append(backslashes * 2, L'\\');
        quoted += L'"';
        return quoted;
    };

    SECURITY_ATTRIBUTES security = {};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) {
        error = L"Could not create the local search provider pipe.";
        return false;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    HANDLE nullInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE nullOutput = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (nullInput == INVALID_HANDLE_VALUE || nullOutput == INVALID_HANDLE_VALUE) {
        if (nullInput != INVALID_HANDLE_VALUE) CloseHandle(nullInput);
        if (nullOutput != INVALID_HANDLE_VALUE) CloseHandle(nullOutput);
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        error = L"Could not prepare the local search provider process.";
        return false;
    }

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nullInput;
    startup.hStdOutput = writePipe;
    startup.hStdError = nullOutput;
    PROCESS_INFORMATION process = {};
    std::wstring commandLine = quoteArgument(pythonPath) + L" -u " + quoteArgument(serverPath);
    const BOOL created = CreateProcessW(pythonPath.c_str(), commandLine.data(),
        nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, appDirectory.c_str(),
        &startup, &process);
    CloseHandle(nullInput);
    CloseHandle(nullOutput);
    CloseHandle(writePipe);
    if (!created) {
        CloseHandle(readPipe);
        error = L"Could not start Python search providers (Windows error " +
            std::to_wstring(GetLastError()) + L").";
        return false;
    }
    CloseHandle(process.hThread);

    std::string readyLine;
    const ULONGLONG deadline = GetTickCount64() + 10000;
    while (GetTickCount64() < deadline && readyLine.find('\n') == std::string::npos) {
        DWORD available = 0;
        if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr)) break;
        if (available > 0) {
            char buffer[256] = {};
            DWORD read = 0;
            if (!ReadFile(readPipe, buffer, std::min<DWORD>(available, sizeof(buffer)), &read, nullptr) ||
                read == 0) break;
            readyLine.append(buffer, read);
        } else if (WaitForSingleObject(process.hProcess, 0) != WAIT_TIMEOUT) {
            break;
        } else {
            Sleep(25);
        }
    }
    CloseHandle(readPipe);

    const size_t newline = readyLine.find('\n');
    if (newline != std::string::npos) readyLine.resize(newline);
    if (!readyLine.empty() && readyLine.back() == '\r') readyLine.pop_back();
    const size_t firstTab = readyLine.find('\t');
    const size_t secondTab = firstTab == std::string::npos
        ? std::string::npos : readyLine.find('\t', firstTab + 1);
    if (firstTab != 5 || secondTab == std::string::npos ||
        readyLine.compare(0, 5, "READY") != 0) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 1000);
        CloseHandle(process.hProcess);
        error = L"Python search providers did not start. Install dependencies from requirements.txt and verify BLOXCY_PYTHON.";
        return false;
    }

    const std::string portText = readyLine.substr(firstTab + 1, secondTab - firstTab - 1);
    const std::wstring widePort(portText.begin(), portText.end());
    wchar_t* portEnd = nullptr;
    const unsigned long port = std::wcstoul(widePort.c_str(), &portEnd, 10);
    const std::string token = readyLine.substr(secondTab + 1);
    const bool validToken = std::all_of(token.begin(), token.end(), [](unsigned char character) {
        return (character >= 'a' && character <= 'z') ||
               (character >= 'A' && character <= 'Z') ||
               (character >= '0' && character <= '9') ||
               character == '_' || character == '-';
    });
    if (!portEnd || *portEnd != L'\0' || port == 0 || port > 65535 ||
        token.size() < 32 || token.size() > 128 || !validToken) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 1000);
        CloseHandle(process.hProcess);
        error = L"Python search providers returned an invalid startup response.";
        return false;
    }

    m_searchBackendPort = static_cast<unsigned short>(port);
    m_searchBackendToken.assign(token.begin(), token.end());
    m_searchBackendProcess = process.hProcess;
    return true;
}

void BrowserHost::Shutdown() {
    if (m_shutdown) return;
    m_shutdown = true;
    if (m_searchBackendProcess) {
        if (WaitForSingleObject(m_searchBackendProcess, 0) == WAIT_TIMEOUT) {
            TerminateProcess(m_searchBackendProcess, 0);
            WaitForSingleObject(m_searchBackendProcess, 1000);
        }
        CloseHandle(m_searchBackendProcess);
        m_searchBackendProcess = nullptr;
        m_searchBackendPort = 0;
        m_searchBackendToken.clear();
    }
    if (m_controller) {
        m_controller->Close();
        m_controller->Release();
        m_controller = nullptr;
    }
    if (m_webView) {
        m_webView->Release();
        m_webView = nullptr;
    }
    if (m_environment) {
        m_environment->Release();
        m_environment = nullptr;
    }
    if (m_database) {
        sqlite3_close(m_database);
        m_database = nullptr;
    }
    if (m_loaderModule) {
        FreeLibrary(m_loaderModule);
        m_loaderModule = nullptr;
    }
    if (m_comInitialized) {
        CoUninitialize();
        m_comInitialized = false;
    }
    SecureZeroMemory(m_account.vaultKey.data(), m_account.vaultKey.size());
    m_parent = nullptr;
}

bool BrowserHost::InitializeSearchDatabase(const std::wstring& appDirectory) {
    const std::wstring dataDirectory = GetAppDataDirectory();
    if (dataDirectory.empty()) {
        MessageBoxW(m_parent, L"Could not access AppData to create the search database.",
                    L"Bloxcy REVO Search", MB_OK | MB_ICONERROR);
        return false;
    }

    const std::wstring databasePath = dataDirectory + L"\\sites.sqlite3";
    if (sqlite3_open16(databasePath.c_str(), &m_database) != SQLITE_OK) {
        const std::wstring error = L"Could not open the search database: " + SqliteError(m_database, SQLITE_ERROR);
        MessageBoxW(m_parent, error.c_str(), L"Bloxcy REVO Search", MB_OK | MB_ICONERROR);
        return false;
    }

    char* errorMessage = nullptr;
    int result = sqlite3_exec(m_database,
        "CREATE TABLE IF NOT EXISTS websites (rank INTEGER PRIMARY KEY, domain TEXT NOT NULL COLLATE NOCASE);",
        nullptr, nullptr, &errorMessage);
    if (result != SQLITE_OK) {
        const std::wstring error = L"Could not prepare the search database: " + SqliteError(m_database, result);
        sqlite3_free(errorMessage);
        MessageBoxW(m_parent, error.c_str(), L"Bloxcy REVO Search", MB_OK | MB_ICONERROR);
        return false;
    }

    sqlite3_stmt* countStatement = nullptr;
    result = sqlite3_prepare_v2(m_database, "SELECT COUNT(*) FROM websites;", -1, &countStatement, nullptr);
    if (result != SQLITE_OK) {
        const std::wstring error = L"Could not inspect the search database: " + SqliteError(m_database, result);
        MessageBoxW(m_parent, error.c_str(), L"Bloxcy REVO Search", MB_OK | MB_ICONERROR);
        return false;
    }
    const int countResult = sqlite3_step(countStatement);
    const sqlite3_int64 currentCount = countResult == SQLITE_ROW ? sqlite3_column_int64(countStatement, 0) : -1;
    sqlite3_finalize(countStatement);
    if (countResult != SQLITE_ROW) {
        const std::wstring error = L"Could not inspect the search database: " + SqliteError(m_database, countResult);
        MessageBoxW(m_parent, error.c_str(), L"Bloxcy REVO Search", MB_OK | MB_ICONERROR);
        return false;
    }
    if (currentCount == kExpectedSiteCount) return true;

    const std::wstring csvPath = appDirectory + L"\\databases\\tranco_GQ3LK-1m.csv\\top-1m.csv";
    std::ifstream csv(csvPath.c_str());
    if (!csv) {
        MessageBoxW(m_parent, L"Could not open databases\\tranco_GQ3LK-1m.csv\\top-1m.csv.",
                    L"Bloxcy REVO Search", MB_OK | MB_ICONERROR);
        return false;
    }

    result = sqlite3_exec(m_database, "BEGIN IMMEDIATE; DELETE FROM websites;", nullptr, nullptr, &errorMessage);
    if (result != SQLITE_OK) {
        const std::wstring error = L"Could not refresh the search database: " + SqliteError(m_database, result);
        sqlite3_free(errorMessage);
        MessageBoxW(m_parent, error.c_str(), L"Bloxcy REVO Search", MB_OK | MB_ICONERROR);
        return false;
    }

    sqlite3_stmt* insertStatement = nullptr;
    result = sqlite3_prepare_v2(m_database, "INSERT INTO websites(rank, domain) VALUES(?1, ?2);",
                                -1, &insertStatement, nullptr);
    if (result != SQLITE_OK) {
        sqlite3_exec(m_database, "ROLLBACK;", nullptr, nullptr, nullptr);
        const std::wstring error = L"Could not prepare the site import: " + SqliteError(m_database, result);
        MessageBoxW(m_parent, error.c_str(), L"Bloxcy REVO Search", MB_OK | MB_ICONERROR);
        return false;
    }

    std::string line;
    int imported = 0;
    bool importFailed = false;
    while (std::getline(csv, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t separator = line.find(',');
        if (separator == std::string::npos || separator + 1 >= line.size()) {
            importFailed = true;
            break;
        }

        char* end = nullptr;
        const long rank = std::strtol(line.c_str(), &end, 10);
        if (end != line.c_str() + separator || rank <= 0) {
            importFailed = true;
            break;
        }
        const std::string domain = line.substr(separator + 1);
        if (domain.find_first_of(" \t") != std::string::npos) {
            importFailed = true;
            break;
        }

        sqlite3_bind_int64(insertStatement, 1, rank);
        sqlite3_bind_text(insertStatement, 2, domain.c_str(), static_cast<int>(domain.size()), SQLITE_TRANSIENT);
        if (sqlite3_step(insertStatement) != SQLITE_DONE) {
            importFailed = true;
            break;
        }
        sqlite3_reset(insertStatement);
        sqlite3_clear_bindings(insertStatement);
        ++imported;
    }
    sqlite3_finalize(insertStatement);

    if (importFailed || imported != kExpectedSiteCount) {
        sqlite3_exec(m_database, "ROLLBACK;", nullptr, nullptr, nullptr);
        const std::wstring error = L"Could not import the complete top-1m.csv dataset (loaded " +
            std::to_wstring(imported) + L" of " + std::to_wstring(kExpectedSiteCount) + L" rows).";
        MessageBoxW(m_parent, error.c_str(), L"Bloxcy REVO Search", MB_OK | MB_ICONERROR);
        return false;
    }

    result = sqlite3_exec(m_database,
        "CREATE INDEX IF NOT EXISTS websites_domain_idx ON websites(domain COLLATE NOCASE); COMMIT;",
        nullptr, nullptr, &errorMessage);
    if (result != SQLITE_OK) {
        sqlite3_exec(m_database, "ROLLBACK;", nullptr, nullptr, nullptr);
        const std::wstring error = L"Could not finish importing the search database: " + SqliteError(m_database, result);
        sqlite3_free(errorMessage);
        MessageBoxW(m_parent, error.c_str(), L"Bloxcy REVO Search", MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}

void BrowserHost::CreateController(ICoreWebView2Environment* environment) {
    if (m_shutdown) return;
    m_environment = environment;
    m_environment->AddRef();

    const std::weak_ptr<BrowserHost> weakSelf = weak_from_this();
    auto* callback = new CallbackHandler<
        ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
        HRESULT, ICoreWebView2Controller*>(
            IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
            [weakSelf](HRESULT error, ICoreWebView2Controller* controller) {
                const auto self = weakSelf.lock();
                if (!self || self->m_shutdown) return S_OK;
                if (FAILED(error) || !controller) {
                    self->HandleControllerError(FAILED(error) ? error : E_FAIL);
                    return S_OK;
                }

                self->m_controller = controller;
                self->m_controller->AddRef();
                RECT bounds = {};
                GetClientRect(self->m_parent, &bounds);
                bounds.top = MulDiv(80, GetDpiForWindow(self->m_parent), 96);
                HRESULT setupResult = self->m_controller->put_Bounds(bounds);
                if (FAILED(setupResult)) self->ShowError(L"size the browser view", setupResult);
                setupResult = self->m_controller->put_IsVisible(TRUE);
                if (FAILED(setupResult)) self->ShowError(L"show the browser view", setupResult);
                setupResult = self->m_controller->get_CoreWebView2(&self->m_webView);
                if (FAILED(setupResult) || !self->m_webView) {
                    self->HandleControllerError(FAILED(setupResult) ? setupResult : E_FAIL);
                    return S_OK;
                }

                ICoreWebView2Settings* settings = nullptr;
                setupResult = self->m_webView->get_Settings(&settings);
                if (FAILED(setupResult) || !settings) {
                    self->ShowError(L"configure browser settings", FAILED(setupResult) ? setupResult : E_FAIL);
                    return S_OK;
                }
                setupResult = settings->put_AreHostObjectsAllowed(FALSE);
                if (FAILED(setupResult)) {
                    self->ShowError(L"disable native host objects for web content", setupResult);
                }
                ICoreWebView2Settings7* extendedSettings = nullptr;
                setupResult = settings->QueryInterface(IID_ICoreWebView2Settings7,
                                                       reinterpret_cast<void**>(&extendedSettings));
                settings->Release();
                if (FAILED(setupResult) || !extendedSettings) {
                    self->ShowError(L"enable browser developer tools", FAILED(setupResult) ? setupResult : E_FAIL);
                    return S_OK;
                }
                setupResult = extendedSettings->put_AreDevToolsEnabled(TRUE);
                if (FAILED(setupResult)) self->ShowError(L"enable browser developer tools", setupResult);
                setupResult = extendedSettings->put_AreDefaultContextMenusEnabled(TRUE);
                if (FAILED(setupResult)) self->ShowError(L"enable the browser context menu", setupResult);
                extendedSettings->Release();

                if (self->m_shutdown) return S_OK;

                auto* messageHandler = new CallbackHandler<
                    ICoreWebView2WebMessageReceivedEventHandler,
                    ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*>(
                        IID_ICoreWebView2WebMessageReceivedEventHandler,
                        [weakSelf](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) {
                            const auto host = weakSelf.lock();
                            if (!host || host->m_shutdown) return S_OK;
                            LPWSTR source = nullptr;
                            const HRESULT sourceResult = args->get_Source(&source);
                            if (FAILED(sourceResult) || !source) {
                                if (FAILED(sourceResult)) {
                                    host->ShowError(L"verify a message from the homepage", sourceResult);
                                }
                                CoTaskMemFree(source);
                                return S_OK;
                            }
                            const bool isHomepage = !host->m_homeUri.empty() &&
                                CompareStringOrdinal(source, -1, host->m_homeUri.c_str(), -1, TRUE) == CSTR_EQUAL;
                            const bool isSettingsPage = !host->m_settingsUri.empty() &&
                                CompareStringOrdinal(source, -1, host->m_settingsUri.c_str(), -1, TRUE) == CSTR_EQUAL;
                            const bool isResultsPage = !host->m_resultsUri.empty() &&
                                CompareStringOrdinal(source, -1, host->m_resultsUri.c_str(), -1, TRUE) == CSTR_EQUAL;
                            const bool isOnboardingPage = !host->m_onboardingUri.empty() &&
                                CompareStringOrdinal(source, -1, host->m_onboardingUri.c_str(), -1, TRUE) == CSTR_EQUAL;
                            const std::wstring sourceUrl = source;
                            CoTaskMemFree(source);

                            LPWSTR json = nullptr;
                            const HRESULT messageResult = args->get_WebMessageAsJson(&json);
                            if (SUCCEEDED(messageResult) && json) {
                                std::wstring message = DecodeWebMessage(json);
                                if (!message.empty() && (isHomepage || isSettingsPage || isResultsPage ||
                                    isOnboardingPage ||
                                    message.compare(0, 20, L"password-form-ready|") == 0 ||
                                    message.compare(0, 14, L"save-password|") == 0)) {
                                    host->OnWebMessage(message, sourceUrl);
                                }
                                SecureClearString(message);
                                SecureZeroMemory(json, wcslen(json) * sizeof(wchar_t));
                                CoTaskMemFree(json);
                            } else if (FAILED(messageResult)) {
                                host->ShowError(L"read a message from the browser page", messageResult);
                            }
                            return S_OK;
                        });
                EventRegistrationToken messageToken = {};
                HRESULT eventResult = self->m_webView->add_WebMessageReceived(messageHandler, &messageToken);
                messageHandler->Release();
                if (FAILED(eventResult)) self->ShowError(L"enable browser messages", eventResult);

                auto* navigationStartingHandler = new CallbackHandler<
                    ICoreWebView2NavigationStartingEventHandler,
                    ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*>(
                        IID_ICoreWebView2NavigationStartingEventHandler,
                        [weakSelf](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) {
                            const auto host = weakSelf.lock();
                            if (!host || host->m_shutdown) return S_OK;
                            LPWSTR uri = nullptr;
                            HRESULT result = args->get_Uri(&uri);
                            if (FAILED(result)) {
                                host->ShowError(L"check a requested navigation address", result);
                                args->put_Cancel(TRUE);
                                return S_OK;
                            }
                            const std::wstring target = uri ? uri : L"";
                            CoTaskMemFree(uri);
                            const bool internalPage =
                                (!host->m_homeUri.empty() &&
                                 CompareStringOrdinal(target.c_str(), -1,
                                     host->m_homeUri.c_str(), -1, TRUE) == CSTR_EQUAL) ||
                                (!host->m_settingsUri.empty() &&
                                 CompareStringOrdinal(target.c_str(), -1,
                                     host->m_settingsUri.c_str(), -1, TRUE) == CSTR_EQUAL) ||
                                (!host->m_resultsUri.empty() &&
                                 CompareStringOrdinal(target.c_str(), -1,
                                     host->m_resultsUri.c_str(), -1, TRUE) == CSTR_EQUAL) ||
                                (!host->m_onboardingUri.empty() &&
                                 CompareStringOrdinal(target.c_str(), -1,
                                     host->m_onboardingUri.c_str(), -1, TRUE) == CSTR_EQUAL);
                            std::wstring navigatedFilePath;
                            std::wstring navigatedFileUrl;
                            const bool allowedFile = StartsWithFileUrl(target) &&
                                ResolveLocalFilePath(target, navigatedFilePath, navigatedFileUrl) &&
                                !host->m_allowedFilePath.empty() &&
                                CompareStringOrdinal(navigatedFilePath.c_str(), -1,
                                    host->m_allowedFilePath.c_str(), -1, TRUE) == CSTR_EQUAL;
                            const bool allowed = StartsWithHttpUrl(target) || allowedFile || internalPage ||
                                CompareStringOrdinal(target.c_str(), -1, L"about:blank", -1, TRUE) == CSTR_EQUAL;
                            if (!allowed) {
                                args->put_Cancel(TRUE);
                                BOOL userInitiated = FALSE;
                                if (SUCCEEDED(args->get_IsUserInitiated(&userInitiated)) &&
                                    userInitiated && host->m_parent) {
                                    MessageBoxW(host->m_parent,
                                        L"Navigation to this address type was blocked. Open local files from the browser's file picker.",
                                        L"Bloxcy REVO Security", MB_OK | MB_ICONWARNING);
                                }
                            }
                            return S_OK;
                        });
                EventRegistrationToken navigationStartingToken = {};
                eventResult = self->m_webView->add_NavigationStarting(
                    navigationStartingHandler, &navigationStartingToken);
                navigationStartingHandler->Release();
                if (FAILED(eventResult)) {
                    self->ShowError(L"protect browser navigation", eventResult);
                }

                auto* permissionHandler = new CallbackHandler<
                    ICoreWebView2PermissionRequestedEventHandler,
                    ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs*>(
                        IID_ICoreWebView2PermissionRequestedEventHandler,
                        [weakSelf](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) {
                            const auto host = weakSelf.lock();
                            if (!host || host->m_shutdown) return S_OK;
                            const HRESULT result = args->put_State(
                                COREWEBVIEW2_PERMISSION_STATE_DENY);
                            if (FAILED(result)) host->ShowError(L"deny a website permission request", result);
                            return S_OK;
                        });
                EventRegistrationToken permissionToken = {};
                eventResult = self->m_webView->add_PermissionRequested(permissionHandler, &permissionToken);
                permissionHandler->Release();
                if (FAILED(eventResult)) {
                    self->ShowError(L"protect browser permissions", eventResult);
                }

                ICoreWebView2_7* resourceWebView = nullptr;
                eventResult = self->m_webView->QueryInterface(
                    IID_ICoreWebView2_7, reinterpret_cast<void**>(&resourceWebView));
                if (FAILED(eventResult) || !resourceWebView) {
                    self->ShowError(L"initialize wellness ad and tracker blocking",
                                    FAILED(eventResult) ? eventResult : E_NOINTERFACE);
                } else {
                    eventResult = resourceWebView->AddWebResourceRequestedFilter(
                        L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
                    if (FAILED(eventResult)) {
                        self->ShowError(L"filter web requests for wellness protection", eventResult);
                    } else {
                        auto* resourceHandler = new CallbackHandler<
                            ICoreWebView2WebResourceRequestedEventHandler,
                            ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs*>(
                                IID_ICoreWebView2WebResourceRequestedEventHandler,
                                [weakSelf](ICoreWebView2* sender,
                                           ICoreWebView2WebResourceRequestedEventArgs* args) {
                                    const auto host = weakSelf.lock();
                                    if (!host || host->m_shutdown ||
                                        host->m_browserMode != L"wellness") return S_OK;
                                    ICoreWebView2WebResourceRequest* request = nullptr;
                                    HRESULT result = args->get_Request(&request);
                                    if (FAILED(result) || !request) return S_OK;
                                    LPWSTR uri = nullptr;
                                    result = request->get_Uri(&uri);
                                    request->Release();
                                    if (FAILED(result) || !uri) {
                                        CoTaskMemFree(uri);
                                        return S_OK;
                                    }
                                    const bool blocked = IsBlockedByWellnessFilter(uri);
                                    CoTaskMemFree(uri);
                                    if (!blocked) return S_OK;
                                    ICoreWebView2WebResourceResponse* response = nullptr;
                                    result = host->m_environment->CreateWebResourceResponse(
                                        nullptr, 204, L"No Content", L"", &response);
                                    if (SUCCEEDED(result) && response) {
                                        result = args->put_Response(response);
                                        response->Release();
                                    }
                                    if (FAILED(result)) {
                                        host->ShowError(L"block an ad or tracking request", result);
                                    }
                                    return result;
                                });
                        EventRegistrationToken resourceToken = {};
                        eventResult = self->m_webView->add_WebResourceRequested(
                            resourceHandler, &resourceToken);
                        resourceHandler->Release();
                        if (FAILED(eventResult)) {
                            self->ShowError(L"enable wellness ad and tracker blocking", eventResult);
                        }
                    }
                    resourceWebView->Release();
                }

                const wchar_t* passwordManagerScript =
                    L"(()=>{if(window.top!==window||!window.chrome||!window.chrome.webview)return;"
                    L"const bridge=window.chrome.webview,origin=encodeURIComponent(location.origin);"
                    L"const setup=()=>{"
                    L"document.addEventListener('submit',event=>{"
                    L"if(window.__bloxcySubmitting){window.__bloxcySubmitting=false;return;}"
                    L"const form=event.target;if(!form||!form.querySelectorAll)return;"
                    L"const inputs=Array.from(form.querySelectorAll('input[type=password]'));"
                    L"const passwordInput=inputs.reverse().find(input=>input.value);if(!passwordInput)return;"
                    L"const usernameInput=form.querySelector('input[autocomplete=username],input[type=email],input[type=text],input[type=tel]');"
                    L"if(!usernameInput||!usernameInput.value)return;"
                    L"event.preventDefault();const token=Math.random().toString(36).slice(2)+Date.now().toString(36);"
                    L"window.__bloxcyPendingForms=window.__bloxcyPendingForms||new Map();"
                    L"window.__bloxcyPendingForms.set(token,form);"
                    L"bridge.postMessage('save-password|'+origin+'|'+encodeURIComponent(usernameInput.value)+'|'+"
                    L"encodeURIComponent(passwordInput.value)+'|'+token);},true);"
                    L"bridge.addEventListener('message',event=>{const message=event.data;"
                    L"if(typeof message!=='string'||!message.startsWith('resume-password-form:'))return;"
                    L"const token=message.slice(21),form=window.__bloxcyPendingForms&&window.__bloxcyPendingForms.get(token);"
                    L"if(!form)return;window.__bloxcyPendingForms.delete(token);window.__bloxcySubmitting=true;"
                    L"if(form.requestSubmit)form.requestSubmit();else form.submit();});"
                    L"bridge.postMessage('password-form-ready|'+origin);};"
                    L"if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',setup,{once:true});"
                    L"else setup();})();";
                auto* scriptHandler = new CallbackHandler<
                    ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler,
                    HRESULT, LPCWSTR>(
                        IID_ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler,
                        [weakSelf](HRESULT error, LPCWSTR) {
                            const auto host = weakSelf.lock();
                            if (host && !host->m_shutdown && FAILED(error)) {
                                host->ShowError(L"install the password manager on web pages", error);
                            }
                            return S_OK;
                        });
                eventResult = self->m_webView->AddScriptToExecuteOnDocumentCreated(
                    passwordManagerScript, scriptHandler);
                scriptHandler->Release();
                if (FAILED(eventResult)) {
                    self->ShowError(L"install the password manager on web pages", eventResult);
                }

                auto* navigationHandler = new CallbackHandler<
                    ICoreWebView2NavigationCompletedEventHandler,
                    ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*>(
                        IID_ICoreWebView2NavigationCompletedEventHandler,
                        [weakSelf](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs*) {
                            const auto host = weakSelf.lock();
                            if (host && !host->m_shutdown && host->m_parent) {
                                LPWSTR source = nullptr;
                                if (SUCCEEDED(sender->get_Source(&source)) && source) {
                                    const bool isHome = !host->m_homeUri.empty() &&
                                        CompareStringOrdinal(source, -1, host->m_homeUri.c_str(), -1, TRUE) == CSTR_EQUAL;
                                    const bool isSettings = !host->m_settingsUri.empty() &&
                                        CompareStringOrdinal(source, -1, host->m_settingsUri.c_str(), -1, TRUE) == CSTR_EQUAL;
                                    const bool isResults = !host->m_resultsUri.empty() &&
                                        CompareStringOrdinal(source, -1, host->m_resultsUri.c_str(), -1, TRUE) == CSTR_EQUAL;
                                    const bool isOnboardingPage = !host->m_onboardingUri.empty() &&
                                        CompareStringOrdinal(source, -1, host->m_onboardingUri.c_str(), -1, TRUE) == CSTR_EQUAL;
                                    if (host->m_isHibernating && wcscmp(source, L"about:blank") == 0) {
                                        CoTaskMemFree(source);
                                        return S_OK;
                                    }
                                    host->m_isSettingsPage = isSettings;
                                    host->m_isSearchPage = isResults;
                                    host->m_isOnboardingPage = isOnboardingPage;
                                    if (isHome) {
                                        host->m_isHomePage = true;
                                        const std::wstring themeMessage = L"\"set-theme:" + host->m_themeName + L"\"";
                                        const HRESULT themeResult =
                                            sender->PostWebMessageAsJson(themeMessage.c_str());
                                        if (FAILED(themeResult)) {
                                            host->ShowError(L"apply the selected homepage theme", themeResult);
                                        }
                                        host->NotifyBrowserModeChanged();
                                    } else if (isSettings) {
                                        const std::wstring settingsMessage =
                                            L"{\"type\":\"settings-data\",\"theme\":" +
                                            EscapeJavaScriptString(host->m_themeName) +
                                            L",\"browserMode\":" +
                                            EscapeJavaScriptString(host->m_browserMode) +
                                            L",\"username\":" + EscapeJavaScriptString(host->m_account.username) +
                                            L",\"profile\":" + EscapeJavaScriptString(host->m_account.profileDirectory) +
                                            L"}";
                                        const HRESULT settingsResult =
                                            sender->PostWebMessageAsJson(settingsMessage.c_str());
                                        if (FAILED(settingsResult)) {
                                            host->ShowError(L"load account information in settings", settingsResult);
                                        }
                                    } else if (isResults) {
                                        host->m_isHomePage = false;
                                    } else if (isOnboardingPage) {
                                        host->m_isHomePage = false;
                                    } else if (wcscmp(source, L"about:blank") != 0) {
                                        host->m_isHomePage = false;
                                        host->m_isHibernating = false;
                                        host->m_isSearchPage = false;
                                        host->m_isOnboardingPage = false;
                                    }
                                    if (StartsWithHttpUrl(source)) {
                                        host->m_trustScore = -1;
                                        host->m_trustDetails = L"Collecting website trust signals…";
                                        host->EvaluateTrustSignals();
                                    } else {
                                        host->m_trustScore = -1;
                                        host->m_trustDetails =
                                            L"Trust signals are available for HTTP and HTTPS websites only.";
                                    }
                                    if (host->m_forceDark) host->SetForceDark(true);
                                    CoTaskMemFree(source);
                                }
                                host->RequestFavicon();
                                PostMessageW(host->m_parent, kBrowserStateChangedMessage, 0, 0);
                            }
                            return S_OK;
                        });
                EventRegistrationToken navigationToken = {};
                eventResult = self->m_webView->add_NavigationCompleted(navigationHandler, &navigationToken);
                navigationHandler->Release();
                if (FAILED(eventResult)) self->ShowError(L"track browser navigation", eventResult);

                ICoreWebView2_15* faviconWebView = nullptr;
                if (SUCCEEDED(self->m_webView->QueryInterface(
                        IID_ICoreWebView2_15, reinterpret_cast<void**>(&faviconWebView))) &&
                    faviconWebView) {
                    auto* faviconHandler = new CallbackHandler<
                        ICoreWebView2FaviconChangedEventHandler, ICoreWebView2*, IUnknown*>(
                            IID_ICoreWebView2FaviconChangedEventHandler,
                            [weakSelf](ICoreWebView2*, IUnknown*) {
                                const auto host = weakSelf.lock();
                                if (host && !host->m_shutdown) host->RequestFavicon();
                                return S_OK;
                            });
                    EventRegistrationToken faviconToken = {};
                    const HRESULT faviconResult =
                        faviconWebView->add_FaviconChanged(faviconHandler, &faviconToken);
                    faviconHandler->Release();
                    if (FAILED(faviconResult)) {
                        self->ShowError(L"track website favicon changes", faviconResult);
                    }
                    faviconWebView->Release();
                }

                auto* newWindowHandler = new CallbackHandler<
                    ICoreWebView2NewWindowRequestedEventHandler,
                    ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*>(
                        IID_ICoreWebView2NewWindowRequestedEventHandler,
                        [weakSelf](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) {
                            const auto host = weakSelf.lock();
                            if (!host || host->m_shutdown) return S_OK;
                            LPWSTR uri = nullptr;
                            HRESULT result = args->get_Uri(&uri);
                            if (FAILED(result)) {
                                host->ShowError(L"read a requested pop-up address", result);
                                args->put_Handled(TRUE);
                                return S_OK;
                            }
                            args->put_Handled(TRUE);
                            if (uri && StartsWithHttpUrl(uri) && host->m_parent) {
                                auto* target = new std::wstring(uri);
                                if (!PostMessageW(host->m_parent, kOpenUrlMessage,
                                        reinterpret_cast<WPARAM>(target), 0)) {
                                    delete target;
                                }
                            }
                            CoTaskMemFree(uri);
                            return S_OK;
                        });
                EventRegistrationToken newWindowToken = {};
                eventResult = self->m_webView->add_NewWindowRequested(newWindowHandler, &newWindowToken);
                newWindowHandler->Release();
                if (FAILED(eventResult)) self->ShowError(L"block and safely route website pop-ups", eventResult);

                if (self->m_pendingNavigate) {
                    self->m_pendingNavigate = false;
                    if (self->m_pendingSearch) {
                        self->m_pendingSearch = false;
                        self->Search(self->m_pendingUrl);
                    } else if (self->m_pendingHome) {
                        self->NavigateHomePage();
                    } else {
                        self->Navigate(self->m_pendingUrl);
                    }
                } else {
                    self->NavigateHomePage();
                }
                return S_OK;
            });

    const HRESULT result = m_environment->CreateCoreWebView2Controller(m_parent, callback);
    callback->Release();
    if (FAILED(result)) HandleControllerError(result);
}

void BrowserHost::SetBounds(const RECT& bounds) {
    if (m_controller) {
        const HRESULT result = m_controller->put_Bounds(bounds);
        if (FAILED(result)) ShowError(L"resize the browser view", result);
    }
}

bool BrowserHost::ClearBrowsingData(unsigned int dataKinds) {
    return ClearBrowsingDataInternal(dataKinds, true);
}

void BrowserHost::ClearCookiesForPrivacyMode() {
    if (!IsPrivacyMode()) return;
    if (!ClearBrowsingDataInternal(COREWEBVIEW2_BROWSING_DATA_KINDS_COOKIES, false)) {
        MessageBoxW(m_parent,
            L"Could not start cookie cleanup while leaving this tab. "
            L"Privacy mode cannot guarantee the selected page's cookies were removed.",
            L"Bloxcy REVO Privacy", MB_OK | MB_ICONERROR);
    }
}

bool BrowserHost::ClearBrowsingDataInternal(unsigned int dataKinds, bool notify) {
    if (m_shutdown || !m_webView || dataKinds == 0) return false;

    ICoreWebView2_13* webViewWithProfile = nullptr;
    HRESULT result = m_webView->QueryInterface(
        IID_ICoreWebView2_13, reinterpret_cast<void**>(&webViewWithProfile));
    if (FAILED(result)) return false;

    ICoreWebView2Profile* profile = nullptr;
    result = webViewWithProfile->get_Profile(&profile);
    webViewWithProfile->Release();
    if (FAILED(result) || !profile) return false;

    ICoreWebView2Profile2* profileWithClearData = nullptr;
    result = profile->QueryInterface(
        IID_ICoreWebView2Profile2, reinterpret_cast<void**>(&profileWithClearData));
    profile->Release();
    if (FAILED(result)) return false;

    HWND parent = m_parent;
    auto* callback =
        new CallbackHandler<ICoreWebView2ClearBrowsingDataCompletedHandler, HRESULT>(
            IID_ICoreWebView2ClearBrowsingDataCompletedHandler,
            [parent, notify](HRESULT errorCode) -> HRESULT {
                if (!notify) return S_OK;
                if (!PostMessageW(parent, kBrowserDataClearedMessage,
                                  static_cast<WPARAM>(errorCode), 0)) {
                    return HRESULT_FROM_WIN32(GetLastError());
                }
                return S_OK;
            });
    result = profileWithClearData->ClearBrowsingData(
        static_cast<COREWEBVIEW2_BROWSING_DATA_KINDS>(dataKinds), callback);
    callback->Release();
    profileWithClearData->Release();
    return SUCCEEDED(result);
}

void BrowserHost::Navigate(const std::wstring& url) {
    if (m_shutdown) return;
    if (url == L"bloxcy://home") {
        NavigateHome();
        return;
    }
    if (url == L"bloxcy://settings") {
        NavigateSettings();
        return;
    }
    if (url == L"bloxcy://search" && !m_lastSearchQuery.empty()) {
        Search(m_lastSearchQuery);
        return;
    }
    std::wstring filePath;
    std::wstring normalizedFileUrl;
    const bool localFile = ResolveLocalFilePath(url, filePath, normalizedFileUrl);
    if (!StartsWithHttpUrl(url) && !localFile) {
        MessageBoxW(m_parent,
            L"Only HTTP, HTTPS, and existing local files can be opened in a browser tab.",
            L"Bloxcy REVO Navigation", MB_OK | MB_ICONWARNING);
        return;
    }
    if (localFile) {
        m_allowedFilePath = std::move(filePath);
    } else {
        m_allowedFilePath.clear();
    }
    m_isHibernating = false;
    m_isHomePage = false;
    m_isSearchPage = false;
    m_isSettingsPage = false;
    m_isOnboardingPage = false;
    m_pendingHome = false;
    m_pendingSearch = false;
    m_pendingNavigate = true;
    m_pendingUrl = localFile ? normalizedFileUrl : url;
    if (!m_webView) return;
    m_pendingNavigate = false;
    const HRESULT result = m_webView->Navigate(m_pendingUrl.c_str());
    if (FAILED(result)) ShowError(L"navigate to the requested page", result);
}

void BrowserHost::NavigateHome() {
    m_allowedFilePath.clear();
    m_isHibernating = false;
    m_isHomePage = true;
    m_isSearchPage = false;
    m_isSettingsPage = false;
    m_isOnboardingPage = false;
    m_pendingHome = true;
    m_pendingSearch = false;
    m_pendingNavigate = true;
    if (!m_webView || m_shutdown) return;
    m_pendingNavigate = false;
    NavigateHomePage();
}

void BrowserHost::NavigateSettings() {
    m_allowedFilePath.clear();
    if (m_shutdown || m_settingsUri.empty()) return;
    m_isHibernating = false;
    m_isHomePage = false;
    m_isSearchPage = false;
    m_isSettingsPage = true;
    m_isOnboardingPage = false;
    m_pendingHome = false;
    m_pendingSearch = false;
    m_pendingNavigate = true;
    m_pendingUrl = m_settingsUri;
    if (!m_webView) return;
    m_pendingNavigate = false;
    const HRESULT result = m_webView->Navigate(m_settingsUri.c_str());
    if (FAILED(result)) ShowError(L"load settings", result);
}

void BrowserHost::Hibernate() {
    if (!m_webView || m_shutdown) return;
    m_isHibernating = true;
    m_isHomePage = false;
    m_isSearchPage = false;
    m_isSettingsPage = false;
    const HRESULT result = m_webView->Navigate(L"about:blank");
    if (FAILED(result)) {
        m_isHibernating = false;
        ShowError(L"hibernate the active tab", result);
    }
}

void BrowserHost::NavigateOrSearch(const std::wstring& input) {
    const std::wstring trimmed = Trim(input);
    if (trimmed.empty()) return;

    std::wstring filePath;
    std::wstring fileUrl;
    if (ResolveLocalFilePath(trimmed, filePath, fileUrl)) {
        Navigate(fileUrl);
        return;
    }

    if (LooksLikeUrl(trimmed)) {
        std::wstring lower = trimmed;
        std::transform(lower.begin(), lower.end(), lower.begin(), towlower);
        if (trimmed.find(L"://") != std::wstring::npos &&
            lower.compare(0, 7, L"http://") != 0 && lower.compare(0, 8, L"https://") != 0) {
            MessageBoxW(m_parent,
                L"For safety, only HTTP and HTTPS addresses are supported.",
                L"Bloxcy REVO Navigation", MB_OK | MB_ICONWARNING);
            return;
        }
        Navigate(lower.compare(0, 7, L"http://") == 0 ||
                        lower.compare(0, 8, L"https://") == 0
                     ? trimmed : L"https://" + trimmed);
    } else {
        Search(trimmed);
    }
}

void BrowserHost::Search(const std::wstring& query, const std::wstring& requestedEngine) {
    if (m_shutdown) return;
    m_allowedFilePath.clear();
    const std::wstring trimmed = Trim(query);
    if (trimmed.empty()) return;
    const std::wstring engine = IsSupportedSearchEngine(requestedEngine)
        ? requestedEngine : L"bloxcy";
    if (!m_webView) {
        m_pendingNavigate = true;
        m_pendingSearch = true;
        m_pendingHome = false;
        m_pendingUrl = trimmed;
        m_lastSearchQuery = trimmed;
        return;
    }

    m_isHibernating = false;
    m_isSettingsPage = false;
    m_isHomePage = false;
    m_isOnboardingPage = false;
    m_lastSearchQuery = trimmed;
    if (engine != L"bloxcy") {
        std::wstring historyError;
        if (!m_accountStore.RecordSearch(m_account, trimmed, historyError)) {
            MessageBoxW(m_parent, historyError.c_str(), L"Bloxcy REVO Search History",
                        MB_OK | MB_ICONERROR);
        }
        const std::wstring searchUrl = GetSearchEngineUrl(engine, trimmed);
        if (searchUrl.empty()) {
            MessageBoxW(m_parent, L"The selected search engine is unavailable.",
                        L"Bloxcy REVO Search", MB_OK | MB_ICONWARNING);
            return;
        }
        Navigate(searchUrl);
        return;
    }

    std::wstring backendError;
    const bool backendAvailable = EnsureSearchBackend(backendError);

    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "SELECT domain, rank FROM websites "
        "WHERE domain LIKE ?1 ESCAPE '\\' COLLATE NOCASE OR instr(lower(domain), lower(?2)) > 0 "
        "ORDER BY CASE WHEN lower(domain) = lower(?2) THEN 0 "
        "WHEN domain LIKE ?1 ESCAPE '\\' COLLATE NOCASE THEN 1 ELSE 2 END, rank LIMIT 30;";
    int result = sqlite3_prepare_v2(m_database, sql, -1, &statement, nullptr);
    if (result != SQLITE_OK) {
        const std::wstring error = L"Search failed: " + SqliteError(m_database, result);
        MessageBoxW(m_parent, error.c_str(), L"Bloxcy REVO Search", MB_OK | MB_ICONERROR);
        return;
    }

    std::wstring siteQuery = trimmed;
    const size_t scheme = siteQuery.find(L"://");
    if (scheme != std::wstring::npos) siteQuery.erase(0, scheme + 3);
    if (siteQuery.compare(0, 4, L"www.") == 0) siteQuery.erase(0, 4);
    const size_t pathSeparator = siteQuery.find_first_of(L"/?#");
    if (pathSeparator != std::wstring::npos) siteQuery.resize(pathSeparator);
    siteQuery = Trim(siteQuery);
    std::transform(siteQuery.begin(), siteQuery.end(), siteQuery.begin(), towlower);

    std::wstring pattern;
    pattern.reserve(siteQuery.size() * 2 + 1);
    for (wchar_t character : siteQuery) {
        if (character == L'%' || character == L'_' || character == L'\\') pattern += L'\\';
        pattern += character;
    }
    pattern += L'%';
    sqlite3_bind_text16(statement, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(statement, 2, siteQuery.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<std::pair<std::wstring, int>> results;
    std::set<std::wstring> foundDomains;
    auto addResult = [&results, &foundDomains](const wchar_t* domainText, int rank) {
        const std::wstring domain = domainText ? domainText : L"";
        std::wstring normalized = domain;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), towlower);
        if (!domain.empty() && foundDomains.insert(normalized).second) {
            results.emplace_back(domain, rank);
        }
    };
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        const auto* domain = static_cast<const wchar_t*>(sqlite3_column_text16(statement, 0));
        addResult(domain, sqlite3_column_int(statement, 1));
    }
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) {
        const std::wstring error = L"Search failed: " + SqliteError(m_database, result);
        MessageBoxW(m_parent, error.c_str(), L"Bloxcy REVO Search", MB_OK | MB_ICONERROR);
        return;
    }

    if (results.empty()) {
        sqlite3_stmt* tokenStatement = nullptr;
        const char* tokenSql =
            "SELECT domain, rank FROM websites "
            "WHERE domain LIKE ?1 ESCAPE '\\' COLLATE NOCASE "
            "ORDER BY rank LIMIT 30;";
        result = sqlite3_prepare_v2(m_database, tokenSql, -1, &tokenStatement, nullptr);
        if (result != SQLITE_OK) {
            const std::wstring error = L"Could not prepare typo-tolerant search: " +
                SqliteError(m_database, result);
            MessageBoxW(m_parent, error.c_str(), L"Bloxcy REVO Search", MB_OK | MB_ICONERROR);
            return;
        }
        const std::set<std::wstring> ignoredTerms = {
            L"a", L"an", L"and", L"for", L"how", L"is", L"of", L"the", L"to", L"what"
        };
        std::vector<std::wstring> tokens;
        size_t tokenStart = 0;
        while (tokenStart < trimmed.size()) {
            while (tokenStart < trimmed.size() && !iswalnum(trimmed[tokenStart])) ++tokenStart;
            size_t tokenEnd = tokenStart;
            while (tokenEnd < trimmed.size() &&
                   (iswalnum(trimmed[tokenEnd]) || trimmed[tokenEnd] == L'-')) ++tokenEnd;
            if (tokenEnd > tokenStart) {
                std::wstring token = trimmed.substr(tokenStart, tokenEnd - tokenStart);
                std::transform(token.begin(), token.end(), token.begin(), towlower);
                if (token.size() >= 3 && token.size() <= 32 &&
                    ignoredTerms.find(token) == ignoredTerms.end()) {
                    tokens.push_back(std::move(token));
                }
            }
            tokenStart = tokenEnd + 1;
        }

        for (const std::wstring& token : tokens) {
            std::wstring tokenPattern;
            for (wchar_t character : token) {
                if (character == L'%' || character == L'_' || character == L'\\') tokenPattern += L'\\';
                tokenPattern += character;
            }
            tokenPattern += L'%';
            sqlite3_bind_text16(tokenStatement, 1, tokenPattern.c_str(), -1, SQLITE_TRANSIENT);
            while ((result = sqlite3_step(tokenStatement)) == SQLITE_ROW) {
                const auto* domain = static_cast<const wchar_t*>(sqlite3_column_text16(tokenStatement, 0));
                addResult(domain, sqlite3_column_int(tokenStatement, 1));
            }
            if (result != SQLITE_DONE) {
                sqlite3_finalize(tokenStatement);
                const std::wstring error = L"Search failed: " + SqliteError(m_database, result);
                MessageBoxW(m_parent, error.c_str(), L"Bloxcy REVO Search", MB_OK | MB_ICONERROR);
                return;
            }
            sqlite3_reset(tokenStatement);
            sqlite3_clear_bindings(tokenStatement);
            if (results.size() >= 30) break;
        }

        if (results.empty()) {
            sqlite3_stmt* fuzzyStatement = nullptr;
            const char* fuzzySql =
                "SELECT domain, rank FROM websites "
                "WHERE domain LIKE ?1 ESCAPE '\\' COLLATE NOCASE "
                "ORDER BY rank LIMIT 6000;";
            result = sqlite3_prepare_v2(m_database, fuzzySql, -1, &fuzzyStatement, nullptr);
            if (result != SQLITE_OK) {
                sqlite3_finalize(tokenStatement);
                const std::wstring error = L"Could not prepare typo-tolerant search: " +
                    SqliteError(m_database, result);
                MessageBoxW(m_parent, error.c_str(), L"Bloxcy REVO Search", MB_OK | MB_ICONERROR);
                return;
            }
            for (const std::wstring& token : tokens) {
                std::wstring prefix = token.substr(0, 1) + L'%';
                sqlite3_bind_text16(fuzzyStatement, 1, prefix.c_str(), -1, SQLITE_TRANSIENT);
                while ((result = sqlite3_step(fuzzyStatement)) == SQLITE_ROW) {
                    const auto* domainText =
                        static_cast<const wchar_t*>(sqlite3_column_text16(fuzzyStatement, 0));
                    const std::wstring domain = domainText ? domainText : L"";
                    const size_t dot = domain.find(L'.');
                    std::wstring label = domain.substr(0, dot);
                    std::transform(label.begin(), label.end(), label.begin(), towlower);
                    if (IsOneEditAway(token, label)) {
                        addResult(domain.c_str(), sqlite3_column_int(fuzzyStatement, 1));
                    }
                }
                if (result != SQLITE_DONE) {
                    sqlite3_finalize(fuzzyStatement);
                    sqlite3_finalize(tokenStatement);
                    const std::wstring error = L"Typo-tolerant search failed: " +
                        SqliteError(m_database, result);
                    MessageBoxW(m_parent, error.c_str(), L"Bloxcy REVO Search", MB_OK | MB_ICONERROR);
                    return;
                }
                sqlite3_reset(fuzzyStatement);
                sqlite3_clear_bindings(fuzzyStatement);
                if (results.size() >= 30) break;
            }
            sqlite3_finalize(fuzzyStatement);
        }
        sqlite3_finalize(tokenStatement);
    }

    const std::wstring dictionaryWord = ExtractDictionaryWord(trimmed);
    const std::wstring backendUrl = L"http://127.0.0.1:" +
        std::to_wstring(m_searchBackendPort);
    std::wstring resultsJson = L"{\"query\":" + EscapeJavaScriptString(trimmed) +
        L",\"theme\":" + EscapeJavaScriptString(m_themeName) +
        L",\"browserMode\":" + EscapeJavaScriptString(m_browserMode) +
        L",\"dictionaryWord\":" + EscapeJavaScriptString(dictionaryWord) +
        L",\"apiBase\":" + EscapeJavaScriptString(backendUrl) +
        L",\"token\":" + EscapeJavaScriptString(m_searchBackendToken) +
        L",\"providersAvailable\":" + (backendAvailable ? L"true" : L"false") +
        L",\"providerError\":" + EscapeJavaScriptString(backendError) +
        L",\"results\":[";
    for (size_t index = 0; index < results.size(); ++index) {
        if (index != 0) resultsJson += L",";
        resultsJson += L"{\"domain\":" + EscapeJavaScriptString(results[index].first) +
            L",\"rank\":" + std::to_wstring(results[index].second) + L"}";
    }
    resultsJson += L"]}";
    m_pendingResultsJson = std::move(resultsJson);
    m_isHomePage = false;
    m_isSettingsPage = false;
    m_isSearchPage = true;
    m_pendingHome = false;
    m_pendingSearch = false;
    const HRESULT navigationResult = m_webView->Navigate(m_resultsUri.c_str());
    if (FAILED(navigationResult)) ShowError(L"load search results", navigationResult);
}

void BrowserHost::Back() {
    if (!m_webView || m_shutdown) return;
    BOOL canGoBack = FALSE;
    const HRESULT result = m_webView->get_CanGoBack(&canGoBack);
    if (FAILED(result)) {
        ShowError(L"check browser history", result);
    } else if (canGoBack) {
        m_webView->GoBack();
    }
}

void BrowserHost::Forward() {
    if (!m_webView || m_shutdown) return;
    BOOL canGoForward = FALSE;
    const HRESULT result = m_webView->get_CanGoForward(&canGoForward);
    if (FAILED(result)) {
        ShowError(L"check browser history", result);
    } else if (canGoForward) {
        m_webView->GoForward();
    }
}

void BrowserHost::Reload() {
    if (!m_webView || m_shutdown) return;
    const HRESULT result = m_webView->Reload();
    if (FAILED(result)) ShowError(L"reload the page", result);
}

void BrowserHost::OpenDevTools() {
    if (!m_webView || m_shutdown) return;
    const HRESULT result = m_webView->OpenDevToolsWindow();
    if (FAILED(result)) ShowError(L"open Developer Tools", result);
}

void BrowserHost::SpeakPage() {
    if (!m_webView || m_shutdown) return;
    auto* callback = new CallbackHandler<ICoreWebView2ExecuteScriptCompletedHandler,
        HRESULT, LPCWSTR>(IID_ICoreWebView2ExecuteScriptCompletedHandler,
        [weakSelf = weak_from_this()](HRESULT error, LPCWSTR) {
            const auto host = weakSelf.lock();
            if (host && !host->m_shutdown && FAILED(error)) {
                host->ShowError(L"read this page aloud", error);
            }
            return S_OK;
        });
    const HRESULT result = m_webView->ExecuteScript(
        L"(()=>{const text=(document.body?.innerText||'').trim().slice(0,12000);"
        L"if(!text||!window.speechSynthesis)return;"
        L"speechSynthesis.cancel();speechSynthesis.speak(new SpeechSynthesisUtterance(text));})()",
        callback);
    callback->Release();
    if (FAILED(result)) ShowError(L"start text-to-speech", result);
}

void BrowserHost::RequestFavicon() {
    if (!m_webView || m_shutdown || !m_parent) return;
    ICoreWebView2_15* faviconWebView = nullptr;
    if (FAILED(m_webView->QueryInterface(IID_ICoreWebView2_15,
            reinterpret_cast<void**>(&faviconWebView))) || !faviconWebView) return;

    LPWSTR source = nullptr;
    const HRESULT sourceResult = m_webView->get_Source(&source);
    if (FAILED(sourceResult) || !source || !StartsWithHttpUrl(source)) {
        CoTaskMemFree(source);
        faviconWebView->Release();
        return;
    }
    const std::wstring expectedUrl(source);
    CoTaskMemFree(source);
    const std::weak_ptr<BrowserHost> weakSelf = weak_from_this();
    auto* callback = new CallbackHandler<ICoreWebView2GetFaviconCompletedHandler,
        HRESULT, IStream*>(IID_ICoreWebView2GetFaviconCompletedHandler,
        [weakSelf, expectedUrl](HRESULT error, IStream* stream) {
            const auto host = weakSelf.lock();
            if (!host || host->m_shutdown || FAILED(error) || !stream ||
                host->GetCurrentUrl() != expectedUrl) return S_OK;
            HBITMAP image = CreateBitmapFromPngStream(stream);
            if (!image) return S_OK;
            auto* update = new BrowserFaviconUpdate{ expectedUrl, image };
            if (!PostMessageW(host->m_parent, kBrowserFaviconMessage,
                    reinterpret_cast<WPARAM>(update), 0)) {
                DeleteObject(image);
                delete update;
            }
            return S_OK;
        });
    const HRESULT result = faviconWebView->GetFavicon(
        COREWEBVIEW2_FAVICON_IMAGE_FORMAT_PNG, callback);
    callback->Release();
    faviconWebView->Release();
    if (FAILED(result) && result != E_NOINTERFACE) {
        ShowError(L"load the website tab logo", result);
    }
}

void BrowserHost::SetTheme(const std::wstring& themeName) {
    if (themeName != L"obsidian" && themeName != L"light" &&
        themeName != L"ocean" && themeName != L"forest") {
        return;
    }
    m_themeName = themeName;
    if (m_webView && (m_isHomePage || m_isSettingsPage) && !m_shutdown) {
        const std::wstring message = L"\"native-theme:" + m_themeName + L"\"";
        const HRESULT result = m_webView->PostWebMessageAsJson(message.c_str());
        if (FAILED(result)) ShowError(L"apply the selected homepage theme", result);
    }
}

void BrowserHost::SetForceDark(bool enabled) {
    m_forceDark = enabled;
    if (!m_webView || m_shutdown) return;

    const std::wstring script = enabled
        ? LR"JS((()=>{const id="bloxcy-forced-dark";let style=document.getElementById(id);const parse=s=>{const m=s.match(/rgba?\((\d+),\s*(\d+),\s*(\d+)/i);if(!m)return 1;const r=+m[1]/255,g=+m[2]/255,b=+m[3]/255;return .2126*r+.7152*g+.0722*b;};const scan=root=>{const nodes=[];if(root.nodeType===1)nodes.push(root);if(root.querySelectorAll)nodes.push(...root.querySelectorAll("*"));for(const el of nodes){if(/^(IMG|VIDEO|CANVAS|SVG|IFRAME|PICTURE)$/i.test(el.tagName))continue;const cs=getComputedStyle(el);if(parse(cs.backgroundColor)>.62)el.setAttribute("data-bloxcy-dark-bg","");if(parse(cs.color)>.68)el.setAttribute("data-bloxcy-dark-text","");}};scan(document.body||document.documentElement);document.documentElement.setAttribute("data-bloxcy-forced-dark","");if(!style){style=document.createElement("style");style.id=id;(document.head||document.documentElement).appendChild(style);}style.textContent='html[data-bloxcy-forced-dark]{color-scheme:dark!important;background:#121212!important}html[data-bloxcy-forced-dark] body{background:#121212!important;color:#e8eaed!important}html[data-bloxcy-forced-dark] [data-bloxcy-dark-bg]{background-color:#202124!important}html[data-bloxcy-forced-dark] [data-bloxcy-dark-text]{color:#e8eaed!important}html[data-bloxcy-forced-dark] a{color:#8ab4f8!important}html[data-bloxcy-forced-dark] input,html[data-bloxcy-forced-dark] textarea,html[data-bloxcy-forced-dark] select{background:#202124!important;color:#e8eaed!important;border-color:#5f6368!important}';if(window.__bloxcyDarkObserver)window.__bloxcyDarkObserver.disconnect();window.__bloxcyDarkObserver=new MutationObserver(records=>{for(const r of records)for(const n of r.addedNodes)scan(n);});window.__bloxcyDarkObserver.observe(document.documentElement,{childList:true,subtree:true});})())JS"
        : LR"JS((()=>{document.documentElement.removeAttribute("data-bloxcy-forced-dark");const s=document.getElementById("bloxcy-forced-dark");if(s)s.remove();if(window.__bloxcyDarkObserver){window.__bloxcyDarkObserver.disconnect();delete window.__bloxcyDarkObserver;}for(const el of document.querySelectorAll("[data-bloxcy-dark-bg],[data-bloxcy-dark-text]")){el.removeAttribute("data-bloxcy-dark-bg");el.removeAttribute("data-bloxcy-dark-text");}})())JS";
    auto* callback = new CallbackHandler<ICoreWebView2ExecuteScriptCompletedHandler,
        HRESULT, LPCWSTR>(IID_ICoreWebView2ExecuteScriptCompletedHandler,
        [weakSelf = weak_from_this()](HRESULT error, LPCWSTR) {
            const auto host = weakSelf.lock();
            if (host && !host->m_shutdown && FAILED(error)) {
                host->ShowError(L"apply forced dark mode", error);
            }
            return S_OK;
        });
    const HRESULT result = m_webView->ExecuteScript(script.c_str(), callback);
    callback->Release();
    if (FAILED(result)) ShowError(L"apply forced dark mode", result);
}

void BrowserHost::RequestMemoryEstimate() {
    if (!m_webView || m_shutdown || m_isHibernating) return;
    const std::weak_ptr<BrowserHost> weakSelf = weak_from_this();
    auto* callback = new CallbackHandler<ICoreWebView2ExecuteScriptCompletedHandler,
        HRESULT, LPCWSTR>(IID_ICoreWebView2ExecuteScriptCompletedHandler,
        [weakSelf](HRESULT error, LPCWSTR result) {
            const auto host = weakSelf.lock();
            if (!host || host->m_shutdown || !host->m_parent) return S_OK;
            size_t bytes = 0;
            if (SUCCEEDED(error) && result && wcscmp(result, L"null") != 0) {
                wchar_t* end = nullptr;
                const unsigned long long parsed = wcstoull(result, &end, 10);
                if (end != result && *end == L'\0') bytes = static_cast<size_t>(parsed);
            }
            PostMessageW(host->m_parent, kBrowserMemoryEstimateMessage,
                         static_cast<WPARAM>(bytes), 0);
            if (host->m_isSettingsPage) {
                const std::wstring message =
                    L"{\"type\":\"memory-estimate\",\"bytes\":" +
                    std::to_wstring(static_cast<unsigned long long>(bytes)) + L"}";
                const HRESULT postResult = host->m_webView->PostWebMessageAsJson(message.c_str());
                if (FAILED(postResult)) {
                    host->ShowError(L"update the memory estimate in settings", postResult);
                }
            }
            return S_OK;
        });
    const HRESULT result = m_webView->ExecuteScript(
        L"(()=>{const memory=performance&&performance.memory;"
        L"return memory?memory.usedJSHeapSize:null;})()",
        callback);
    callback->Release();
    if (FAILED(result)) ShowError(L"estimate active-page JavaScript memory", result);
}

void BrowserHost::ResetZoom() {
    if (!m_controller || m_shutdown) return;
    const HRESULT result = m_controller->put_ZoomFactor(1.0);
    if (FAILED(result)) ShowError(L"reset browser zoom", result);
}

void BrowserHost::ChangeZoom(double delta) {
    if (!m_controller || m_shutdown) return;
    double zoom = 1.0;
    HRESULT result = m_controller->get_ZoomFactor(&zoom);
    if (FAILED(result)) {
        ShowError(L"read the current browser zoom", result);
        return;
    }
    zoom = std::clamp(zoom + delta, 0.25, 5.0);
    result = m_controller->put_ZoomFactor(zoom);
    if (FAILED(result)) ShowError(L"change browser zoom", result);
}

void BrowserHost::ShowKeyboardShortcuts() const {
    const wchar_t* shortcuts =
        L"Tabs and navigation\r\n"
        L"Ctrl+T       New tab\r\n"
        L"Ctrl+W       Close tab (confirm to exit on the last tab)\r\n"
        L"Ctrl+Shift+T Reopen the most recently closed tab\r\n"
        L"Ctrl+Tab     Next tab\r\n"
        L"Ctrl+1-8     Switch to a tab; Ctrl+9 selects the last tab\r\n"
        L"Ctrl+L       Focus the address bar\r\n"
        L"Alt+Left     Back\r\n"
        L"Alt+Right    Forward\r\n"
        L"Ctrl+R / F5  Reload\r\n"
        L"Ctrl+Plus    Zoom in; Ctrl+Minus zoom out; Ctrl+0 reset\r\n\r\n"
        L"Developer tools\r\n"
        L"F12 or Ctrl+Shift+I/J";
    MessageBoxW(m_parent, shortcuts, L"Bloxcy REVO Keyboard Shortcuts",
                MB_OK | MB_ICONINFORMATION);
}

std::wstring BrowserHost::GetCurrentUrl() const {
    if (m_isHomePage) return L"bloxcy://home";
    if (m_isSettingsPage) return L"bloxcy://settings";
    if (m_isSearchPage) return L"bloxcy://search";
    if (!m_webView) return {};
    LPWSTR url = nullptr;
    const HRESULT result = m_webView->get_Source(&url);
    if (FAILED(result)) return {};
    std::wstring value = url ? url : L"";
    CoTaskMemFree(url);
    return value;
}

std::wstring BrowserHost::GetDocumentTitle() const {
    if (!m_webView) return {};
    LPWSTR title = nullptr;
    const HRESULT result = m_webView->get_DocumentTitle(&title);
    if (FAILED(result)) return {};
    std::wstring value = title ? title : L"";
    CoTaskMemFree(title);
    return value;
}

void BrowserHost::OnWebMessage(const std::wstring& message, const std::wstring& source) {
    const bool isHomepage = !m_homeUri.empty() &&
        CompareStringOrdinal(source.c_str(), -1, m_homeUri.c_str(), -1, TRUE) == CSTR_EQUAL;
    const bool isSettings = !m_settingsUri.empty() &&
        CompareStringOrdinal(source.c_str(), -1, m_settingsUri.c_str(), -1, TRUE) == CSTR_EQUAL;
    const bool isResults = !m_resultsUri.empty() &&
        CompareStringOrdinal(source.c_str(), -1, m_resultsUri.c_str(), -1, TRUE) == CSTR_EQUAL;
    const bool isOnboarding = !m_onboardingUri.empty() &&
        CompareStringOrdinal(source.c_str(), -1, m_onboardingUri.c_str(), -1, TRUE) == CSTR_EQUAL;
    if (isResults) {
        if (message == L"results-ready" && !m_pendingResultsJson.empty() && m_webView) {
            const std::wstring resultsMessage = EscapeJavaScriptString(m_pendingResultsJson);
            const HRESULT result = m_webView->PostWebMessageAsJson(resultsMessage.c_str());
            if (FAILED(result)) {
                ShowError(L"send search results to the results page", result);
            } else {
                m_pendingResultsJson.clear();
            }
        } else if (message.compare(0, 15, L"results:search:") == 0) {
            const std::wstring query = Trim(message.substr(15));
            if (!query.empty()) Search(query);
        }
        return;
    }
    if (isOnboarding) {
    if (message.compare(0, 20, L"onboarding:complete|") == 0 &&
        SaveBrowserMode(message, true)) {
        NavigateHome();
    }
    return;
    }
    if (isSettings) {
        constexpr wchar_t prefix[] = L"settings:theme:";
        constexpr size_t prefixLength = 15;
        if (message.compare(0, prefixLength, prefix) == 0 && m_parent) {
            const std::wstring theme = message.substr(prefixLength);
            WPARAM themeValue = 0;
            if (theme == L"obsidian") themeValue = 0;
            else if (theme == L"light") themeValue = 1;
            else if (theme == L"ocean") themeValue = 2;
            else if (theme == L"forest") themeValue = 3;
            else return;
            PostMessageW(m_parent, kSetThemeMessage, themeValue, 0);
        } else if (message == L"settings:refresh-memory" && m_parent) {
            PostMessageW(m_parent, kRequestMemoryEstimateMessage, 0, 0);
        } else if (message.compare(0, 14, L"settings:mode:") == 0) {
            if (SaveBrowserMode(message, false) && m_webView) {
                const HRESULT result = m_webView->PostWebMessageAsJson(
                    L"\"settings:mode-saved\"");
                if (FAILED(result)) ShowError(L"confirm the saved browser preferences", result);
            }
        }
        return;
    }
    if (!isHomepage) {
        if (IsPrivacyMode()) return;
        HandlePasswordMessage(message, source);
        return;
    }
    if (message.compare(0, 15, L"homepage:theme:") == 0 && m_parent) {
        const std::wstring theme = message.substr(15);
        WPARAM themeValue = 0;
        if (theme == L"obsidian") themeValue = 0;
        else if (theme == L"light") themeValue = 1;
        else if (theme == L"ocean") themeValue = 2;
        else if (theme == L"forest") themeValue = 3;
        else return;
        PostMessageW(m_parent, kSetThemeMessage, themeValue, 0);
    } else if (message.compare(0, 16, L"homepage:colors:") == 0 && m_parent) {
        const std::wstring colors = message.substr(16);
        const size_t separator = colors.find(L',');
        COLORREF background = 0, accent = 0;
        if (separator == std::wstring::npos ||
            !ParseHexColor(colors.substr(0, separator), background) ||
            !ParseHexColor(colors.substr(separator + 1), accent)) return;
        PostMessageW(m_parent, kSetCustomPaletteMessage,
                     static_cast<WPARAM>(background), static_cast<LPARAM>(accent));
    } else if (message == L"homepage:reset-colors" && m_parent) {
        PostMessageW(m_parent, kSetCustomPaletteMessage, static_cast<WPARAM>(-1), 0);
    } else if (message.compare(0, 9, L"open-url:") == 0 && m_parent) {
        const std::wstring url = message.substr(9);
        if (!StartsWithHttpUrl(url) || url.size() > 2048 ||
            url.find_first_of(L"\r\n\t ") != std::wstring::npos) return;
        auto* target = new std::wstring(url);
        if (!PostMessageW(m_parent, kOpenUrlMessage, reinterpret_cast<WPARAM>(target), 0)) {
            delete target;
        }
    } else if (message.compare(0, 7, L"search:") == 0) {
        const std::wstring payload = message.substr(7);
        const size_t separator = payload.find(L':');
        if (separator != std::wstring::npos) {
            const std::wstring engine = payload.substr(0, separator);
            const std::wstring query = payload.substr(separator + 1);
            if (IsSupportedSearchEngine(engine)) {
                if (engine == L"bloxcy" || LooksLikeUrl(Trim(query))) {
                    NavigateOrSearch(query);
                } else {
                    Search(query, engine);
                }
                return;
            }
        }
        NavigateOrSearch(payload);
    } else if (message == L"focus-address") {
        PostMessageW(m_parent, kFocusAddressMessage, 0, 0);
    } else if (message == L"new-tab") {
        PostMessageW(m_parent, kNewTabMessage, 0, 0);
    } else if (message == L"open-devtools") {
        OpenDevTools();
    }
}

bool BrowserHost::SaveBrowserMode(const std::wstring& message, bool completeOnboarding) {
    const std::wstring prefix = completeOnboarding
        ? L"onboarding:complete|" : L"settings:mode:";
    if (message.compare(0, prefix.size(), prefix) != 0 ||
        message.size() > 128 || message.find(L'|', prefix.size()) != std::wstring::npos) {
        return false;
    }
    const std::wstring mode = message.substr(prefix.size());
    const int modeIndex = BrowserModeIndex(mode);
    if (modeIndex < 0) return false;

    if (mode == L"privacy" && !IsPrivacyMode() &&
        !ClearBrowsingDataInternal(COREWEBVIEW2_BROWSING_DATA_KINDS_COOKIES, false)) {
        MessageBoxW(m_parent,
            L"Privacy mode could not start cookie cleanup in this WebView2 Runtime. "
            L"The mode was not changed.",
            L"Bloxcy REVO Privacy", MB_OK | MB_ICONERROR);
        return false;
    }
    if (!WritePrivateProfileStringW(L"Preferences", L"Mode", mode.c_str(),
                                    m_preferencesPath.c_str()) ||
        (completeOnboarding &&
         !WritePrivateProfileStringW(L"Onboarding", L"Completed", L"1",
                                     m_preferencesPath.c_str()))) {
        MessageBoxW(m_parent, L"Could not save your browser mode to this local profile.",
                    L"Bloxcy REVO Preferences", MB_OK | MB_ICONERROR);
        return false;
    }

    m_browserMode = mode;
    NotifyBrowserModeChanged(true);
    return true;
}

void BrowserHost::NotifyBrowserModeChanged(bool applyNativePreset) {
    const int modeIndex = BrowserModeIndex(m_browserMode);
    if (modeIndex < 0) return;
    if (m_parent && !PostMessageW(m_parent, kSetBrowserModeMessage,
                                  static_cast<WPARAM>(modeIndex),
                                  applyNativePreset ? 1 : 0)) {
        ShowError(L"apply the selected browser mode", HRESULT_FROM_WIN32(GetLastError()));
    }
    if (m_webView && !m_shutdown) {
        const std::wstring message = L"\"browser-mode:" + m_browserMode + L"\"";
        const HRESULT result = m_webView->PostWebMessageAsJson(message.c_str());
        if (FAILED(result)) ShowError(L"apply the selected browser mode to the page", result);
    }
}

int BrowserHost::LookupTrancoRank(const std::wstring& rawHost, std::wstring& error) const {
    if (!m_database) {
        error = L"The local Tranco database is unavailable.";
        return -1;
    }
    std::wstring host = rawHost;
    std::transform(host.begin(), host.end(), host.begin(), towlower);
    const size_t port = host.rfind(L':');
    if (port != std::wstring::npos && host.find(L']') == std::wstring::npos) host.resize(port);
    if (host.compare(0, 4, L"www.") == 0) host.erase(0, 4);

    sqlite3_stmt* statement = nullptr;
    const int prepared = sqlite3_prepare_v2(m_database,
        "SELECT rank FROM websites WHERE lower(domain)=lower(?1) LIMIT 1;",
        -1, &statement, nullptr);
    if (prepared != SQLITE_OK) {
        error = L"The local Tranco lookup failed: " + SqliteError(m_database, prepared);
        return -1;
    }

    int rank = -1;
    std::wstring candidate = host;
    while (!candidate.empty()) {
        sqlite3_bind_text16(statement, 1, candidate.c_str(), -1, SQLITE_TRANSIENT);
        const int result = sqlite3_step(statement);
        if (result == SQLITE_ROW) {
            rank = sqlite3_column_int(statement, 0);
            break;
        }
        if (result != SQLITE_DONE) {
            error = L"The local Tranco lookup failed: " + SqliteError(m_database, result);
            break;
        }
        sqlite3_reset(statement);
        sqlite3_clear_bindings(statement);
        const size_t dot = candidate.find(L'.');
        if (dot == std::wstring::npos) break;
        candidate.erase(0, dot + 1);
    }
    sqlite3_finalize(statement);
    return rank;
}

void BrowserHost::EvaluateTrustSignals() {
    if (!m_webView || m_shutdown) return;
    const std::wstring expectedUrl = GetCurrentUrl();
    if (!StartsWithHttpUrl(expectedUrl)) return;
    const std::wstring script =
        LR"JS((()=>{const trackers=["google-analytics.com","googletagmanager.com","doubleclick.net","facebook.net","hotjar.com","clarity.ms","segment.io","mixpanel.com","amplitude.com","newrelic.com","fullstory.com","crazyegg.com","scorecardresearch.com","quantserve.com","adsrvr.org","rubiconproject.com","pubmatic.com","openx.net","taboola.com","outbrain.com","criteo.com"];const seen=new Set();for(const entry of performance.getEntriesByType("resource")){try{const host=new URL(entry.name).hostname.toLowerCase();if(trackers.some(domain=>host===domain||host.endsWith("."+domain)))seen.add(host);}catch{}}const visible=el=>el.getClientRects().length>0;const controls=Array.from(document.querySelectorAll("button,input,select,textarea,[role=button]")).filter(el=>visible(el)&&el.type!=="hidden"&&!el.getAttribute("aria-label")&&!el.getAttribute("aria-labelledby")&&!el.title&&!el.labels?.length&&!el.innerText?.trim());const missingAlt=Array.from(document.querySelectorAll("img:not([alt])")).filter(visible).length;const overflow=Math.max(document.documentElement.scrollWidth,document.body?.scrollWidth||0)>innerWidth*1.15;return [seen.size,controls.length,missingAlt,overflow?1:0].join("|");})())JS";
    auto* callback = new CallbackHandler<ICoreWebView2ExecuteScriptCompletedHandler,
        HRESULT, LPCWSTR>(IID_ICoreWebView2ExecuteScriptCompletedHandler,
        [weakSelf = weak_from_this(), expectedUrl](HRESULT error, LPCWSTR result) {
            const auto host = weakSelf.lock();
            if (!host || host->m_shutdown || host->GetCurrentUrl() != expectedUrl) return S_OK;
            if (FAILED(error) || !result) {
                host->m_trustScore = -1;
                host->m_trustDetails =
                    L"Trust signals could not be collected from this page. This is not a safety verdict.";
                PostMessageW(host->m_parent, kBrowserStateChangedMessage, 0, 0);
                return S_OK;
            }

            const std::wstring signals = DecodeWebMessage(result);
            std::vector<unsigned long> values;
            size_t start = 0;
            while (start <= signals.size()) {
                const size_t separator = signals.find(L'|', start);
                const std::wstring field = signals.substr(start,
                    separator == std::wstring::npos ? std::wstring::npos : separator - start);
                wchar_t* end = nullptr;
                const unsigned long value = std::wcstoul(field.c_str(), &end, 10);
                if (field.empty() || end == field.c_str() || *end != L'\0' || value > 10000) {
                    values.clear();
                    break;
                }
                values.push_back(value);
                if (separator == std::wstring::npos) break;
                start = separator + 1;
            }
            if (values.size() != 4) {
                host->m_trustScore = -1;
                host->m_trustDetails =
                    L"Trust signals returned an invalid response. This is not a safety verdict.";
                PostMessageW(host->m_parent, kBrowserStateChangedMessage, 0, 0);
                return S_OK;
            }

            std::wstring hostName = expectedUrl;
            const size_t scheme = hostName.find(L"://");
            hostName.erase(0, scheme + 3);
            const size_t path = hostName.find_first_of(L"/?#");
            if (path != std::wstring::npos) hostName.resize(path);
            std::wstring trancoError;
            const int rank = host->LookupTrancoRank(hostName, trancoError);
            const bool https = expectedUrl.size() >= 8 &&
                CompareStringOrdinal(expectedUrl.c_str(), 8, L"https://", 8, TRUE) == CSTR_EQUAL;
            int popularity = 0;
            if (rank > 0 && rank <= 1000) popularity = 15;
            else if (rank <= 10000 && rank > 0) popularity = 11;
            else if (rank <= 100000 && rank > 0) popularity = 7;
            else if (rank <= 1000000 && rank > 0) popularity = 3;
            const int trackerPenalty = static_cast<int>(std::min<unsigned long>(30, values[0] * 8));
            const int accessibilityPenalty = static_cast<int>(
                std::min<unsigned long>(10, (values[1] + values[2]) * 2)) +
                (values[3] != 0 ? 5 : 0);
            host->m_trustScore = std::clamp(
                40 + (https ? 20 : -20) + popularity - trackerPenalty - accessibilityPenalty,
                0, 100);

            const wchar_t* band = host->m_trustScore <= 33
                ? L"Red / dangerous signals (0-33)"
                : host->m_trustScore <= 67
                    ? L"Yellow / caution (34-67)"
                    : L"Green / favorable signals (68-100)";
            host->m_trustDetails = L"Trust signals score: " +
                std::to_wstring(host->m_trustScore) + L" / 100. " + band +
                L". This is a heuristic, not a safety guarantee.";
            host->m_trustDetails += https ? L"\nConnection: HTTPS." : L"\nConnection: HTTP (not encrypted).";
            if (rank > 0) {
                host->m_trustDetails += L"\nTranco popularity: rank #" + std::to_wstring(rank) + L".";
            } else {
                host->m_trustDetails += L"\nTranco popularity: site not found in the local top-1m list.";
                if (!trancoError.empty()) host->m_trustDetails += L" " + trancoError;
            }
            host->m_trustDetails += L"\nKnown tracker domains observed in page resources: " +
                std::to_wstring(values[0]) + L".";
            host->m_trustDetails += L"\nAccessibility hints: " + std::to_wstring(values[1]) +
                L" unlabeled controls, " + std::to_wstring(values[2]) +
                L" images without alt text, horizontal overflow " +
                (values[3] != 0 ? L"detected." : L"not detected.");
            host->m_trustDetails +=
                L"\nScoring: HTTPS +20 (HTTP -20); Tranco +3 to +15 by popularity tier; "
                L"known trackers -8 each (max -30); accessibility hints up to -10; "
                L"horizontal overflow -5; unranked sites receive no Tranco points.";
            host->m_trustDetails +=
                L"\nPopularity and page heuristics cannot establish a site's intent or prove it is safe.";
            PostMessageW(host->m_parent, kBrowserStateChangedMessage, 0, 0);
            return S_OK;
        });
    const HRESULT result = m_webView->ExecuteScript(script.c_str(), callback);
    callback->Release();
    if (FAILED(result)) {
        m_trustScore = -1;
        m_trustDetails =
            L"Trust signals could not be requested from this page. This is not a safety verdict.";
        ShowError(L"collect website trust signals", result);
    }
}

void BrowserHost::HandlePasswordMessage(const std::wstring& message, const std::wstring& source) {
    const std::wstring actualOrigin = GetHttpsOrigin(source);
    if (actualOrigin.empty()) return;

    std::vector<std::wstring> fields;
    size_t start = 0;
    while (start <= message.size()) {
        const size_t separator = message.find(L'|', start);
        fields.push_back(message.substr(start,
            separator == std::wstring::npos ? std::wstring::npos : separator - start));
        if (separator == std::wstring::npos) break;
        start = separator + 1;
    }

    if (fields.size() == 2 && fields[0] == L"password-form-ready") {
        std::wstring reportedOrigin;
        if (DecodeUriComponent(fields[1], reportedOrigin) &&
            CompareStringOrdinal(reportedOrigin.c_str(), -1, actualOrigin.c_str(), -1, TRUE) == CSTR_EQUAL) {
            FillSavedPasswords(actualOrigin);
        }
        return;
    }
    if (fields.size() != 5 || fields[0] != L"save-password") return;

    const std::wstring& token = fields[4];
    const bool validToken = !token.empty() && token.size() <= 64 &&
        std::all_of(token.begin(), token.end(), [](wchar_t character) {
            return (character >= L'a' && character <= L'z') ||
                   (character >= L'0' && character <= L'9');
        });
    auto resumeForm = [this, &token, validToken]() {
        if (!validToken || !m_webView || m_shutdown) return;
        const std::wstring messageJson = L"\"resume-password-form:" + token + L"\"";
        const HRESULT result = m_webView->PostWebMessageAsJson(messageJson.c_str());
        if (FAILED(result)) ShowError(L"continue the website sign-in", result);
    };

    std::wstring reportedOrigin;
    std::wstring username;
    std::wstring password;
    if (!validToken || !DecodeUriComponent(fields[1], reportedOrigin) ||
        CompareStringOrdinal(reportedOrigin.c_str(), -1, actualOrigin.c_str(), -1, TRUE) != CSTR_EQUAL ||
        !DecodeUriComponent(fields[2], username) || !DecodeUriComponent(fields[3], password) ||
        username.empty() || username.size() > 256 || password.empty() || password.size() > 4096) {
        resumeForm();
        SecureClearString(password);
        return;
    }

    const std::wstring prompt = L"Save or update the password for " + username +
        L" at " + actualOrigin + L"?\n\nThe password is encrypted in this account's profile.";
    const int answer = MessageBoxW(m_parent, prompt.c_str(), L"Save password?",
                                  MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
    if (answer == IDYES) {
        std::wstring error;
        if (!m_accountStore.SavePassword(m_account, actualOrigin, username, password, error)) {
            MessageBoxW(m_parent, error.c_str(), L"Bloxcy REVO Password Manager",
                        MB_OK | MB_ICONERROR);
        }
    }
    SecureClearString(password);
    resumeForm();
}

void BrowserHost::FillSavedPasswords(const std::wstring& origin) {
    if (!m_webView || m_shutdown) return;
    std::vector<SavedCredential> credentials;
    std::wstring error;
    if (!m_accountStore.LoadPasswords(m_account, origin, credentials, error)) {
        MessageBoxW(m_parent, error.c_str(), L"Bloxcy REVO Password Manager",
                    MB_OK | MB_ICONERROR);
        return;
    }
    if (credentials.empty()) return;

    std::wstring credentialArray = L"[";
    for (size_t index = 0; index < credentials.size(); ++index) {
        if (index != 0) credentialArray += L",";
        credentialArray += L"{username:" + EscapeJavaScriptString(credentials[index].username) +
            L",password:" + EscapeJavaScriptString(credentials[index].password) + L"}";
    }
    credentialArray += L"]";
    std::wstring script =
        L"(()=>{if(window.top!==window)return;const credentials=" + credentialArray +
        L";for(const form of Array.from(document.forms)){"
        L"let destination;try{destination=new URL(form.action||location.href,location.href).origin;}"
        L"catch(_){continue;}if(destination!==location.origin)continue;"
        L"const passwordInput=form.querySelector('input[type=password]');if(!passwordInput||passwordInput.value)continue;"
        L"const usernameInput=form.querySelector('input[autocomplete=username],input[type=email],input[type=text],input[type=tel]');"
        L"const current=usernameInput&&usernameInput.value.toLowerCase();"
        L"const credential=credentials.find(item=>current&&item.username.toLowerCase()===current)||credentials[0];"
        L"if(usernameInput&&!usernameInput.value){usernameInput.value=credential.username;"
        L"usernameInput.dispatchEvent(new Event('input',{bubbles:true}));"
        L"usernameInput.dispatchEvent(new Event('change',{bubbles:true}));}"
        L"passwordInput.value=credential.password;"
        L"passwordInput.dispatchEvent(new Event('input',{bubbles:true}));"
        L"passwordInput.dispatchEvent(new Event('change',{bubbles:true}));break;}})();";

    const std::weak_ptr<BrowserHost> weakSelf = weak_from_this();
    auto* callback = new CallbackHandler<ICoreWebView2ExecuteScriptCompletedHandler,
        HRESULT, LPCWSTR>(IID_ICoreWebView2ExecuteScriptCompletedHandler,
        [weakSelf](HRESULT result, LPCWSTR) {
            const auto host = weakSelf.lock();
            if (host && !host->m_shutdown && FAILED(result)) {
                host->ShowError(L"fill a saved password", result);
            }
            return S_OK;
        });
    const HRESULT result = m_webView->ExecuteScript(script.c_str(), callback);
    callback->Release();
    for (auto& credential : credentials) SecureClearString(credential.password);
    SecureClearString(script);
    SecureClearString(credentialArray);
    if (FAILED(result)) ShowError(L"fill a saved password", result);
}

void BrowserHost::HandleEnvironmentError(HRESULT error) {
    ShowError(L"initialize WebView2. Check that the Microsoft Edge WebView2 Runtime is installed", error);
}

void BrowserHost::HandleControllerError(HRESULT error) {
    ShowError(L"create the browser view", error);
}

void BrowserHost::ShowError(const wchar_t* action, HRESULT error) const {
    wchar_t* systemMessage = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, static_cast<DWORD>(error), 0,
                   reinterpret_cast<LPWSTR>(&systemMessage), 0, nullptr);
    std::wstring message = L"Could not " + std::wstring(action) + L".";
    if (systemMessage) {
        message += L"\n\n";
        message += systemMessage;
        LocalFree(systemMessage);
    }
    message += L"\n\nError code: 0x";
    wchar_t code[16] = {};
    swprintf_s(code, L"%08X", static_cast<unsigned int>(error));
    message += code;
    MessageBoxW(m_parent, message.c_str(), L"Bloxcy REVO", MB_OK | MB_ICONERROR);
}

void BrowserHost::NavigateHomePage() {
    if (!m_webView || m_shutdown) return;
    m_isHomePage = true;
    std::wstring appDirectory = GetExecutableDirectory();
    if (appDirectory.empty()) {
        MessageBoxW(m_parent, L"Could not locate the app folder to load the homepage.",
                    L"Bloxcy REVO", MB_OK | MB_ICONERROR);
        return;
    }

    std::wstring homepagePath = appDirectory + L"\\ui\\homepage\\index.html";
    DWORD uriLength = 32768;
    std::wstring uri(uriLength, L'\0');
    const HRESULT pathResult = UrlCreateFromPathW(homepagePath.c_str(), uri.data(), &uriLength, 0);
    if (FAILED(pathResult)) {
        ShowError(L"resolve the homepage path", pathResult);
        return;
    }
    uri.resize(uriLength);
    m_homeUri = uri;
    const HRESULT result = m_webView->Navigate(m_homeUri.c_str());
    if (FAILED(result)) ShowError(L"load the homepage", result);
}

void BrowserHost::NavigateOnboardingPage() {
    if (!m_webView || m_shutdown || m_onboardingUri.empty()) return;
    m_isHomePage = false;
    m_isSearchPage = false;
    m_isSettingsPage = false;
    m_isOnboardingPage = true;
    const HRESULT result = m_webView->Navigate(m_onboardingUri.c_str());
    if (FAILED(result)) ShowError(L"load first-run preferences", result);
}

} // namespace Bloxcy
