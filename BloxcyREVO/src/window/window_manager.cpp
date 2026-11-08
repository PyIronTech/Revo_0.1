/**
 * ============================================================================
 * Project: Bloxcy REVO Browser
 * File: window_manager.cpp
 * Description: Topless / Frameless Window Manager (Chrome-style integrated top with tabs)
 * ============================================================================
 * 
 * PREREQUISITES & DEPENDENCIES:
 * 1. Operating System: Windows 10 (Version 1809+) or Windows 11 (64-bit recommended)
 * 2. C++ Compiler: C++17 or later
 *    - MinGW-w64 (GCC 10+) OR
 *    - Microsoft Visual C++ (MSVC / Visual Studio 2019/2022 Build Tools) OR
 *    - Clang (LLVM)
 * 3. Windows SDK / Core System Libraries:
 *    - user32.lib   (-luser32)
 *    - gdi32.lib    (-lgdi32)
 *    - dwmapi.lib   (-ldwmapi)
 *    - uxtheme.lib  (-luxtheme)
 *    - comctl32.lib (-lcomctl32)
 *    - bcrypt.lib   (-lbcrypt)
 * 4. SQLite development files, the WebView2 SDK, and the WebView2 Runtime.
 * 5. Account data is stored below %APPDATA%\BloxcyREVO. Login passwords use
 *    PBKDF2-HMAC-SHA-256; saved website passwords are AES-GCM encrypted.
 * 
 * COMMAND LINE TO DOWNLOAD & INSTALL ALL DEPENDENCIES:
 * ----------------------------------------------------------------------------
 * Option 1 (Using Windows Package Manager - Recommended):
 *   winget install MSYS2.MSYS2
 *   # In MSYS2 UCRT64 terminal, run:
 *   pacman -Syu --noconfirm
 *   pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-sqlite3
 * 
 * Option 2 (Visual Studio 2022 C++ Build Tools via winget):
 *   winget install Microsoft.VisualStudio.2022.BuildTools --force --passive --includeRecommended
 * 
 * Option 3 (Chocolatey):
 *   choco install mingw
 * 
 * ----------------------------------------------------------------------------
 * COMMAND LINE TO COMPILE & RUN:
 * ----------------------------------------------------------------------------
 * From the workspace root, use build.bat or:
 *   cmake -B build -S .
 *   cmake --build build --config Release
 * ============================================================================
 */

#include "window_manager.h"
#include "../../rendering engine/browser_host.h"
#include <WebView2.h>
#include <commdlg.h>
#include <commctrl.h>
#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <utility>

#if defined(_MSC_VER)
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "comctl32.lib")
#endif

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

namespace Bloxcy {

namespace {

constexpr UINT kThemeObsidian = 1001;
constexpr UINT kThemeLight = 1002;
constexpr UINT kThemeOcean = 1003;
constexpr UINT kThemeForest = 1004;
constexpr UINT kCommandNewTab = 2001;
constexpr UINT kCommandReload = 2002;
constexpr UINT kCommandDeveloperTools = 2003;
constexpr UINT kCommandBack = 2004;
constexpr UINT kCommandForward = 2005;
constexpr UINT kCommandFocusAddress = 2006;
constexpr UINT kCommandKeyboardShortcuts = 2007;
constexpr UINT kCommandReopenTab = 2008;
constexpr UINT kCommandToggleSidebar = 2009;
constexpr UINT kCommandExpandSidebar = 2010;
constexpr UINT kCommandCompactTabs = 2011;
constexpr UINT kCreateTabGroupRed = 3201;
constexpr UINT kCreateTabGroupOrange = 3202;
constexpr UINT kCreateTabGroupYellow = 3203;
constexpr UINT kCreateTabGroupGreen = 3204;
constexpr UINT kCreateTabGroupBlue = 3205;
constexpr UINT kCreateTabGroupPurple = 3206;
constexpr UINT kCreateTabGroupGray = 3207;
constexpr UINT kMoveToTabGroupBase = 3300;
constexpr UINT kRemoveFromTabGroup = 3400;
constexpr UINT kHibernateTab = 3401;
constexpr int kHotkeyNewTab = 1;
constexpr int kHotkeyCloseTab = 2;
constexpr int kHotkeyFocusAddress = 3;
constexpr int kHotkeyReload = 4;
constexpr int kHotkeyReloadF5 = 5;
constexpr int kHotkeyDeveloperTools = 6;
constexpr int kHotkeyDeveloperToolsShortcut = 7;
constexpr int kHotkeyBack = 8;
constexpr int kHotkeyForward = 9;
constexpr int kHotkeyNextTab = 10;
constexpr int kHotkeyPreviousTab = 11;
constexpr int kHotkeyResetZoom = 12;
constexpr int kHotkeyReopenTab = 13;
constexpr int kHotkeyDeveloperToolsJ = 14;
constexpr int kHotkeyZoomIn = 15;
constexpr int kHotkeyZoomInShift = 16;
constexpr int kHotkeyZoomOut = 17;
constexpr int kHotkeyMenuAltF = 18;
constexpr int kHotkeyMenuAltE = 19;
constexpr int kHotkeyTab1 = 20;
constexpr int kHotkeyHome = 29;
constexpr int kHotkeyReadAloud = 30;
constexpr int kHotkeyOpenFile = 31;
constexpr UINT_PTR kMemoryEstimateTimer = 73;
constexpr UINT_PTR kEyeWellnessTimer = 74;

COLORREF BlendColor(COLORREF first, COLORREF second, double ratio) {
    const auto blend = [ratio](BYTE a, BYTE b) {
        return static_cast<BYTE>(a * (1.0 - ratio) + b * ratio);
    };
    return RGB(blend(GetRValue(first), GetRValue(second)),
               blend(GetGValue(first), GetGValue(second)),
               blend(GetBValue(first), GetBValue(second)));
}

std::wstring TabLogoText(const BrowserTab& tab) {
    if (tab.isSettings) return L"S";
    if (tab.isHome) return L"B";
    const size_t scheme = tab.url.find(L"://");
    const size_t hostStart = scheme == std::wstring::npos ? 0 : scheme + 3;
    const size_t hostEnd = tab.url.find(L'/', hostStart);
    std::wstring host = tab.url.substr(hostStart, hostEnd == std::wstring::npos
        ? std::wstring::npos : hostEnd - hostStart);
    if (host.compare(0, 4, L"www.") == 0) host.erase(0, 4);
    const size_t dot = host.find(L'.');
    if (dot != std::wstring::npos) host.resize(dot);
    if (host.empty()) return L"?";
    std::transform(host.begin(), host.end(), host.begin(), towupper);
    return host.substr(0, 1);
}

bool IsSafeHttpUrl(const std::wstring& url) {
    const size_t prefixLength = url.size() >= 8 &&
        CompareStringOrdinal(url.c_str(), 8, L"https://", 8, TRUE) == CSTR_EQUAL ? 8 :
        url.size() >= 7 && CompareStringOrdinal(url.c_str(), 7, L"http://", 7, TRUE) == CSTR_EQUAL ? 7 : 0;
    if (prefixLength == 0 || url.size() <= prefixLength ||
        url.find_first_of(L"\r\n\t ") != std::wstring::npos) return false;
    const size_t hostEnd = url.find_first_of(L"/?#", prefixLength);
    return (hostEnd == std::wstring::npos ? url.size() : hostEnd) > prefixLength;
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

const wchar_t* GetThemeName(Theme theme) {
    switch (theme) {
        case Theme::Light: return L"light";
        case Theme::Ocean: return L"ocean";
        case Theme::Forest: return L"forest";
        case Theme::Obsidian:
        default: return L"obsidian";
    }
}

std::wstring GetSettingsPath() {
    DWORD length = GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
    if (length == 0) return {};

    std::wstring appData(length, L'\0');
    DWORD written = GetEnvironmentVariableW(L"APPDATA", appData.data(), length);
    if (written == 0 || written >= length) return {};
    appData.resize(written);

    const std::wstring directory = appData + L"\\BloxcyREVO";
    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        return {};
    }
    return directory + L"\\settings.ini";
}

} // namespace

WindowManager::WindowManager()
    : m_hWnd(nullptr)
    , m_hInstance(nullptr)
    , m_addressEdit(nullptr)
    , m_addressFont(nullptr)
    , m_paintBufferDC(nullptr)
    , m_paintBufferBitmap(nullptr)
    , m_paintBufferPreviousBitmap(nullptr)
    , m_paintBufferWidth(0)
    , m_paintBufferHeight(0)
    , m_title(L"Bloxcy REVO")
    , m_className(L"BloxcyRevoBrowserWindow")
    , m_baseTabStripHeight(38)
    , m_baseNavBarHeight(42)
    , m_baseCaptionBtnWidth(46)
    , m_baseVerticalTabsWidth(212)
    , m_dpi(96)
    , m_theme(Theme::Obsidian)
    , m_activeTabIndex(0)
    , m_hoveredTabIndex(-1)
    , m_hoveredTabCloseIndex(-1)
    , m_draggedTabIndex(-1)
    , m_dragStartX(0)
    , m_dragStartY(0)
    , m_isDraggingTab(false)
    , m_nextTabGroupId(1)
    , m_tabScrollOffset(0)
    , m_hoveredNewTabBtn(false)
    , m_usesCustomPalette(false)
    , m_sidebarVisible(true)
    , m_sidebarExpanded(false)
    , m_browserMode(kBrowserModeNormal)
    , m_activeSidebarPanel(SidebarButton::None)
    , m_cleanerSelections{ true, true, true, true, true, false }
    , m_resourceMode(0)
    , m_sidebarScrollOffset(0)
    , m_compactTabs(false)
    , m_customPaletteBackground(RGB(14, 17, 27))
    , m_customPaletteAccent(RGB(100, 211, 255))
    , m_hoveredCaptionBtn(CaptionButton::None)
    , m_pressedCaptionBtn(CaptionButton::None)
    , m_hoveredNavBtn(NavButton::None)
    , m_pressedNavBtn(NavButton::None)
    , m_hoveredSidebarButton(SidebarButton::None)
    , m_hoveredShortcutIndex(-1)
    , m_isTrackingMouse(false)
{
    const std::wstring settingsPath = GetSettingsPath();
    if (!settingsPath.empty()) {
        const int savedTheme = GetPrivateProfileIntW(L"Appearance", L"Theme",
                                                       static_cast<int>(Theme::Obsidian),
                                                       settingsPath.c_str());
        if (savedTheme >= static_cast<int>(Theme::Obsidian) &&
            savedTheme <= static_cast<int>(Theme::Forest)) {
            m_theme = static_cast<Theme>(savedTheme);
        }
        m_sidebarVisible = true;
        m_sidebarExpanded = GetPrivateProfileIntW(L"Appearance", L"SidebarExpanded", 0,
                                                    settingsPath.c_str()) != 0;
        if (m_sidebarExpanded) m_activeSidebarPanel = SidebarButton::Controls;
        m_resourceMode = std::clamp(
            static_cast<int>(GetPrivateProfileIntW(L"Performance", L"ResourceMode", 0,
                                                    settingsPath.c_str())), 0, 2);
        m_compactTabs = GetPrivateProfileIntW(L"Appearance", L"CompactTabs", 0,
                                               settingsPath.c_str()) != 0;
    }

    // Initialize default tabs (Chrome style)
    m_tabs.push_back({ L"Bloxcy Home", L"bloxcy://home", RGB(78, 228, 255), true, {} });
    m_tabs.push_back({ L"GitHub", L"https://github.com", RGB(230, 237, 243), false, {} });
    m_tabs.push_back({ L"YouTube", L"https://youtube.com", RGB(255, 0, 0), false, {} });
    m_tabs[1].isHibernated = true;
    m_tabs[2].isHibernated = true;

    // Initialize homepage shortcuts
    m_shortcuts.push_back({ L"GitHub", L"https://github.com", RGB(230, 237, 243), {} });
    m_shortcuts.push_back({ L"YouTube", L"https://youtube.com", RGB(255, 0, 0), {} });
    m_shortcuts.push_back({ L"Discord", L"https://discord.com", RGB(88, 101, 242), {} });
    m_shortcuts.push_back({ L"Figma", L"https://figma.com", RGB(26, 188, 254), {} });
    m_shortcuts.push_back({ L"ChatGPT", L"https://chatgpt.com", RGB(16, 163, 127), {} });
    m_shortcuts.push_back({ L"Notion", L"https://notion.so", RGB(255, 255, 255), {} });
}

WindowManager::~WindowManager() {
    if (m_hWnd && IsWindow(m_hWnd)) {
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
    for (const auto& favicon : m_favicons) DeleteObject(favicon.second);
    m_favicons.clear();
    ReleasePaintBuffer();
    if (m_addressFont) {
        DeleteObject(m_addressFont);
        m_addressFont = nullptr;
    }
}

int WindowManager::GetTabStripHeight() const {
    return MulDiv(m_compactTabs ? 32 : m_baseTabStripHeight, m_dpi, 96);
}

int WindowManager::GetNavBarHeight() const {
    return MulDiv(m_baseNavBarHeight, m_dpi, 96);
}

int WindowManager::GetTotalTopHeight() const {
    return GetTabStripHeight() + GetNavBarHeight();
}

int WindowManager::GetVerticalTabsWidth() const {
    return MulDiv(m_baseVerticalTabsWidth, m_dpi, 96);
}

int WindowManager::GetSidebarWidth() const {
    if (!m_sidebarVisible) return 0;
    return MulDiv(m_sidebarExpanded ? 356 : 58, m_dpi, 96);
}

void WindowManager::ReleasePaintBuffer() {
    if (m_paintBufferDC && m_paintBufferPreviousBitmap) {
        SelectObject(m_paintBufferDC, m_paintBufferPreviousBitmap);
    }
    if (m_paintBufferBitmap) DeleteObject(m_paintBufferBitmap);
    if (m_paintBufferDC) DeleteDC(m_paintBufferDC);
    m_paintBufferDC = nullptr;
    m_paintBufferBitmap = nullptr;
    m_paintBufferPreviousBitmap = nullptr;
    m_paintBufferWidth = 0;
    m_paintBufferHeight = 0;
}

int WindowManager::GetVisibleTabCount(int totalWidth) const {
    const int reserved = MulDiv(m_baseCaptionBtnWidth * 3 + 54, m_dpi, 96);
    const int available = std::max(MulDiv(120, m_dpi, 96), totalWidth - reserved);
    const int preferredWidth = MulDiv(190, m_dpi, 96);
    return std::max(1, available / preferredWidth);
}

void WindowManager::AddTab(const std::wstring& title, const std::wstring& url, COLORREF color, bool isHome) {
    if (m_activeTabIndex < m_tabs.size()) {
        ClearPrivacyCookiesForTabChange();
        UpdateActiveTabFromBrowser();
        m_tabs[m_activeTabIndex].isHibernated = true;
        m_tabs[m_activeTabIndex].memoryEstimateBytes = 0;
    }
    m_tabs.push_back({ title, url, color, isHome, {} });
    m_tabs.back().isHibernated = false;
    m_activeTabIndex = m_tabs.size() - 1;
    RECT client = {};
    GetClientRect(m_hWnd, &client);
    const int visibleCount = GetVisibleTabCount(client.right);
    if (static_cast<int>(m_tabs.size()) - m_tabScrollOffset > visibleCount) {
        m_tabScrollOffset = static_cast<int>(m_tabs.size()) - visibleCount;
    }
    if (m_browser) {
        if (isHome) m_browser->NavigateHome();
        else m_browser->Navigate(url);
    }
    if (m_hWnd) InvalidateRect(m_hWnd, nullptr, FALSE);
}

void WindowManager::CloseTab(size_t index) {
    if (index >= m_tabs.size()) return;

    if (m_tabs.size() == 1) {
        const int result = MessageBoxW(m_hWnd, L"Close the last tab and exit Bloxcy REVO?",
                                       L"Close browser", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        if (result == IDYES) Close();
        return;
    }

    const size_t previousActive = m_activeTabIndex;
    if (index == previousActive) {
        ClearPrivacyCookiesForTabChange();
        UpdateActiveTabFromBrowser();
    }
    m_closedTabs.push_back(m_tabs[index]);
    if (m_closedTabs.size() > 20) m_closedTabs.erase(m_closedTabs.begin());
    m_tabs.erase(m_tabs.begin() + index);
    if (index < previousActive) m_activeTabIndex = previousActive - 1;
    else if (index == previousActive) m_activeTabIndex = std::min(index, m_tabs.size() - 1);
    else m_activeTabIndex = previousActive;

    if (m_browser) {
        const auto& activeTab = m_tabs[m_activeTabIndex];
        m_tabs[m_activeTabIndex].isHibernated = false;
        m_tabs[m_activeTabIndex].memoryEstimateBytes = 0;
        if (activeTab.isSettings) m_browser->NavigateSettings();
        else if (activeTab.isHome) m_browser->NavigateHome();
        else if (!activeTab.searchQuery.empty()) m_browser->Search(activeTab.searchQuery);
        else m_browser->Navigate(activeTab.url);
    }

    if (m_hWnd) InvalidateRect(m_hWnd, nullptr, FALSE);
}

void WindowManager::ReopenClosedTab() {
    if (m_closedTabs.empty()) return;
    if (m_activeTabIndex < m_tabs.size()) {
        ClearPrivacyCookiesForTabChange();
        m_tabs[m_activeTabIndex].isHibernated = true;
        m_tabs[m_activeTabIndex].memoryEstimateBytes = 0;
    }
    BrowserTab tab = std::move(m_closedTabs.back());
    m_closedTabs.pop_back();
    m_tabs.push_back(std::move(tab));
    m_activeTabIndex = m_tabs.size() - 1;
    m_tabs[m_activeTabIndex].isHibernated = false;
    m_tabs[m_activeTabIndex].memoryEstimateBytes = 0;
    const BrowserTab& activeTab = m_tabs[m_activeTabIndex];
    if (m_browser) {
        if (activeTab.isSettings) m_browser->NavigateSettings();
        else if (activeTab.isHome) m_browser->NavigateHome();
        else if (!activeTab.searchQuery.empty()) m_browser->Search(activeTab.searchQuery);
        else m_browser->Navigate(activeTab.url);
    }
    if (m_hWnd) InvalidateRect(m_hWnd, nullptr, FALSE);
}

void WindowManager::OpenSettingsTab() {
    for (size_t index = 0; index < m_tabs.size(); ++index) {
        if (m_tabs[index].isSettings) {
            SelectTab(index);
            return;
        }
    }
    if (m_activeTabIndex < m_tabs.size()) {
        ClearPrivacyCookiesForTabChange();
        UpdateActiveTabFromBrowser();
        m_tabs[m_activeTabIndex].isHibernated = true;
        m_tabs[m_activeTabIndex].memoryEstimateBytes = 0;
    }
    m_tabs.push_back({ L"Settings", L"bloxcy://settings", RGB(131, 202, 255),
                       false, {}, 0, true });
    m_activeTabIndex = m_tabs.size() - 1;
    m_tabs[m_activeTabIndex].isHibernated = false;
    if (m_browser) m_browser->NavigateSettings();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void WindowManager::HibernateTab(size_t index) {
    if (index >= m_tabs.size()) return;
    if (index == m_activeTabIndex) {
        UpdateActiveTabFromBrowser();
        if (m_browser) m_browser->Hibernate();
    }
    m_tabs[index].isHibernated = true;
    m_tabs[index].memoryEstimateBytes = 0;
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void WindowManager::SelectTab(size_t index) {
    if (index < m_tabs.size()) {
        if (m_browser && m_activeTabIndex < m_tabs.size()) {
            UpdateActiveTabFromBrowser();
            if (m_activeTabIndex != index) {
                ClearPrivacyCookiesForTabChange();
                m_tabs[m_activeTabIndex].isHibernated = true;
                m_tabs[m_activeTabIndex].memoryEstimateBytes = 0;
            }
        }
        m_activeTabIndex = index;
        m_tabs[index].isHibernated = false;
        m_tabs[index].memoryEstimateBytes = 0;
        if (m_browser) {
            const auto& activeTab = m_tabs[m_activeTabIndex];
            if (activeTab.isSettings) m_browser->NavigateSettings();
            else if (activeTab.isHome) m_browser->NavigateHome();
            else if (!activeTab.searchQuery.empty()) m_browser->Search(activeTab.searchQuery);
            else m_browser->Navigate(activeTab.url);
        }
        if (m_hWnd) InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

void WindowManager::ClearPrivacyCookiesForTabChange() {
    if (m_browser && m_browser->IsPrivacyMode()) {
        m_browser->ClearCookiesForPrivacyMode();
    }
}

void WindowManager::EnableImmersiveDarkMode() {
    if (!m_hWnd) return;

    BOOL darkMode = m_theme == Theme::Light ? FALSE : TRUE;
    DwmSetWindowAttribute(m_hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

    DWORD cornerPreference = DWMWCP_ROUND;
    DwmSetWindowAttribute(m_hWnd, static_cast<DWMWINDOWATTRIBUTE>(DWMWA_WINDOW_CORNER_PREFERENCE),
                          &cornerPreference, sizeof(cornerPreference));
}

ThemePalette WindowManager::GetThemePalette() const {
    if (m_usesCustomPalette) {
        const COLORREF background = m_customPaletteBackground;
        const COLORREF accent = m_customPaletteAccent;
        const unsigned int luminance =
            299u * GetRValue(background) + 587u * GetGValue(background) +
            114u * GetBValue(background);
        const COLORREF text = luminance > 128000u ? RGB(25, 34, 48) : RGB(244, 247, 255);
        const COLORREF secondary = BlendColor(text, background, 0.34);
        const COLORREF muted = BlendColor(text, background, 0.56);
        const COLORREF surface = BlendColor(background,
            luminance > 128000u ? RGB(255, 255, 255) : RGB(0, 0, 0), 0.055);
        return {
            BlendColor(background, RGB(0, 0, 0), 0.08), surface,
            BlendColor(background, text, 0.18), BlendColor(background, accent, 0.2),
            BlendColor(background, text, 0.08), text, secondary, muted, accent,
            background, surface, BlendColor(surface, accent, 0.12),
            BlendColor(background, text, 0.16), surface,
            BlendColor(background, text, 0.2), BlendColor(surface, accent, 0.13),
            BlendColor(surface, accent, 0.2)
        };
    }
    switch (m_theme) {
        case Theme::Light:
            return {
                RGB(231, 237, 247), RGB(246, 248, 252), RGB(215, 223, 235),
                RGB(255, 255, 255), RGB(237, 243, 250), RGB(30, 41, 59),
                RGB(79, 93, 116), RGB(111, 125, 146), RGB(8, 126, 181),
                RGB(249, 251, 255), RGB(255, 255, 255), RGB(242, 247, 253),
                RGB(224, 231, 241), RGB(255, 255, 255), RGB(218, 227, 239),
                RGB(230, 237, 247), RGB(218, 228, 241)
            };
        case Theme::Ocean:
            return {
                RGB(10, 24, 42), RGB(15, 34, 56), RGB(30, 57, 82),
                RGB(20, 42, 66), RGB(27, 55, 82), RGB(231, 244, 255),
                RGB(157, 184, 209), RGB(112, 145, 175), RGB(69, 190, 226),
                RGB(8, 20, 36), RGB(16, 36, 58), RGB(24, 49, 74),
                RGB(35, 60, 84), RGB(14, 33, 54), RGB(37, 65, 91),
                RGB(27, 53, 78), RGB(38, 68, 96)
            };
        case Theme::Forest:
            return {
                RGB(17, 29, 25), RGB(24, 40, 33), RGB(45, 67, 55),
                RGB(29, 48, 39), RGB(39, 61, 49), RGB(239, 248, 240),
                RGB(171, 194, 174), RGB(123, 151, 129), RGB(112, 213, 151),
                RGB(13, 24, 20), RGB(24, 41, 32), RGB(34, 55, 42),
                RGB(48, 70, 55), RGB(20, 37, 29), RGB(48, 73, 56),
                RGB(39, 61, 47), RGB(51, 77, 58)
            };
        case Theme::Obsidian:
        default:
            return {
                RGB(14, 17, 27), RGB(23, 27, 40), RGB(48, 55, 75),
                RGB(26, 31, 45), RGB(36, 43, 60), RGB(240, 244, 255),
                RGB(163, 174, 197), RGB(111, 123, 150), RGB(100, 211, 255),
                RGB(13, 16, 25), RGB(22, 27, 40), RGB(31, 39, 56),
                RGB(43, 51, 71), RGB(22, 28, 42), RGB(52, 63, 86),
                RGB(39, 47, 66), RGB(51, 61, 82)
            };
    }
}

void WindowManager::SetTheme(Theme theme) {
    m_theme = theme;
    m_usesCustomPalette = false;
    EnableImmersiveDarkMode();
    if (m_browser) m_browser->SetTheme(GetThemeName(m_theme));
    InvalidateRect(m_hWnd, nullptr, FALSE);

    const std::wstring settingsPath = GetSettingsPath();
    if (!settingsPath.empty()) {
        const std::wstring savedTheme = std::to_wstring(static_cast<int>(m_theme));
        if (!WritePrivateProfileStringW(L"Appearance", L"Theme", savedTheme.c_str(),
                                        settingsPath.c_str())) {
            MessageBoxW(m_hWnd, L"Could not save your theme preference.",
                        L"Bloxcy REVO Settings", MB_OK | MB_ICONWARNING);
        }
    } else {
        MessageBoxW(m_hWnd, L"Could not access your AppData folder to save the theme.",
                    L"Bloxcy REVO Settings", MB_OK | MB_ICONWARNING);
    }
}

void WindowManager::SetCustomPalette(COLORREF background, COLORREF accent) {
    m_customPaletteBackground = background;
    m_customPaletteAccent = accent;
    m_usesCustomPalette = true;
    if (m_hWnd) InvalidateRect(m_hWnd, nullptr, FALSE);
}

void WindowManager::SaveAppearanceSetting(const wchar_t* key, bool enabled) {
    const std::wstring settingsPath = GetSettingsPath();
    const wchar_t* value = enabled ? L"1" : L"0";
    if (settingsPath.empty() ||
        !WritePrivateProfileStringW(L"Appearance", key, value, settingsPath.c_str())) {
        MessageBoxW(m_hWnd, L"Could not save this appearance preference.",
                    L"Bloxcy REVO Settings", MB_OK | MB_ICONWARNING);
    }
}

bool WindowManager::RefreshSidebarData() {
    std::wstring error;
    if (!m_accountStore.LoadBookmarks(m_accountSession, m_bookmarks, error)) {
        MessageBoxW(m_hWnd, error.c_str(), L"Sidebar data", MB_OK | MB_ICONERROR);
        return false;
    }
    if (!m_accountStore.LoadSearchHistory(m_accountSession, m_searchHistory, error)) {
        MessageBoxW(m_hWnd, error.c_str(), L"Sidebar data", MB_OK | MB_ICONERROR);
        return false;
    }
    const size_t activeCount = m_activeSidebarPanel == SidebarButton::Bookmarks
        ? m_bookmarks.size() : m_searchHistory.size();
    m_sidebarScrollOffset = std::clamp(m_sidebarScrollOffset, 0,
        std::max(0, static_cast<int>(activeCount) - 8));
    return true;
}

void WindowManager::SaveCurrentBookmark() {
    if (!m_browser) return;
    UpdateActiveTabFromBrowser();
    const std::wstring url = m_browser->GetCurrentUrl();
    const std::wstring title = m_browser->GetDocumentTitle();
    std::wstring error;
    if (!m_accountStore.SaveBookmark(m_accountSession,
            title.empty() ? url : title, url, error)) {
        MessageBoxW(m_hWnd, error.c_str(), L"Bookmarks", MB_OK | MB_ICONWARNING);
        return;
    }
    if (!RefreshSidebarData()) return;
    MessageBoxW(m_hWnd, L"Saved to bookmarks and quick access.",
                L"Bookmarks", MB_OK | MB_ICONINFORMATION);
}

void WindowManager::ApplyResourceMode(int mode) {
    static const DWORD priorityClasses[] = {
        NORMAL_PRIORITY_CLASS, BELOW_NORMAL_PRIORITY_CLASS, IDLE_PRIORITY_CLASS
    };
    if (mode < 0 || mode >= static_cast<int>(std::size(priorityClasses))) return;
    if (!SetPriorityClass(GetCurrentProcess(), priorityClasses[mode])) {
        MessageBoxW(m_hWnd, L"Windows could not apply the selected app scheduling priority.",
                    L"Efficiency controls", MB_OK | MB_ICONERROR);
        return;
    }
    m_resourceMode = mode;
    const std::wstring path = GetSettingsPath();
    const std::wstring value = std::to_wstring(mode);
    if (path.empty() || !WritePrivateProfileStringW(L"Performance", L"ResourceMode",
                                                     value.c_str(), path.c_str())) {
        MessageBoxW(m_hWnd, L"The mode was applied for this session but could not be saved.",
                    L"Efficiency controls", MB_OK | MB_ICONWARNING);
    }
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void WindowManager::StartCleaner() {
    const bool anySelected = std::any_of(std::begin(m_cleanerSelections),
        std::end(m_cleanerSelections), [](bool selected) { return selected; });
    if (!anySelected) {
        MessageBoxW(m_hWnd, L"Select at least one item to clean.",
                    L"Cleaner", MB_OK | MB_ICONINFORMATION);
        return;
    }
    const int confirmation = MessageBoxW(m_hWnd,
        L"Clear the selected browser data? This cannot be undone.\n\n"
        L"Download history removes records only; it does not delete downloaded files. "
        L"Bookmarks selection removes saved bookmarks and quick-access favorites.",
        L"Confirm cleanup", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (confirmation != IDYES) return;

    unsigned int dataKinds = 0;
    if (m_cleanerSelections[0])
        dataKinds |= COREWEBVIEW2_BROWSING_DATA_KINDS_DISK_CACHE;
    if (m_cleanerSelections[1])
        dataKinds |= COREWEBVIEW2_BROWSING_DATA_KINDS_COOKIES;
    if (m_cleanerSelections[2])
        dataKinds |= COREWEBVIEW2_BROWSING_DATA_KINDS_BROWSING_HISTORY;
    if (m_cleanerSelections[3])
        dataKinds |= COREWEBVIEW2_BROWSING_DATA_KINDS_DOWNLOAD_HISTORY;
    if (dataKinds != 0 && (!m_browser || !m_browser->ClearBrowsingData(dataKinds))) {
        MessageBoxW(m_hWnd, L"WebView2 could not start clearing the selected browser data.",
                    L"Cleaner", MB_OK | MB_ICONERROR);
        return;
    }

    std::wstring error;
    if (m_cleanerSelections[2] &&
        !m_accountStore.ClearSearchHistory(m_accountSession, error)) {
        MessageBoxW(m_hWnd, error.c_str(), L"Cleaner", MB_OK | MB_ICONERROR);
        return;
    }
    if (m_cleanerSelections[5]) {
        if (!RefreshSidebarData()) return;
        for (const SavedBookmark& bookmark : m_bookmarks) {
            if (!m_accountStore.DeleteBookmark(m_accountSession, bookmark.url, error)) {
                MessageBoxW(m_hWnd, error.c_str(), L"Cleaner", MB_OK | MB_ICONERROR);
                return;
            }
        }
    }
    if (m_cleanerSelections[4]) {
        for (size_t index = m_tabs.size(); index > 0; --index) {
            if (index - 1 != m_activeTabIndex) CloseTab(index - 1);
        }
    }
    RefreshSidebarData();
    MessageBoxW(m_hWnd, L"Selected cleanup actions have been started.",
                L"Cleaner", MB_OK | MB_ICONINFORMATION);
}

void WindowManager::HandleSidebarPanelClick(int x, int y) {
    const int railWidth = MulDiv(58, m_dpi, 96);
    const int localX = x - railWidth;
    const int localY = y - GetTotalTopHeight();
    if (localY < 0) return;

    switch (m_activeSidebarPanel) {
        case SidebarButton::Controls:
            if (localY >= MulDiv(88, m_dpi, 96) &&
                localY < MulDiv(148, m_dpi, 96)) {
                const int cardWidth = MulDiv(82, m_dpi, 96);
                const int gap = MulDiv(7, m_dpi, 96);
                const int relativeX = localX - MulDiv(14, m_dpi, 96);
                if (relativeX >= 0 && relativeX < 3 * (cardWidth + gap) &&
                    relativeX % (cardWidth + gap) < cardWidth) {
                    ApplyResourceMode(relativeX / (cardWidth + gap));
                }
            }
            break;
        case SidebarButton::Cleaner:
            if (localY >= MulDiv(76, m_dpi, 96) &&
                localY < MulDiv(328, m_dpi, 96)) {
                const int row = (localY - MulDiv(76, m_dpi, 96)) /
                    MulDiv(42, m_dpi, 96);
                if (row >= 0 && row < 6) {
                    m_cleanerSelections[row] = !m_cleanerSelections[row];
                    InvalidateRect(m_hWnd, nullptr, FALSE);
                }
            } else if (localY >= MulDiv(348, m_dpi, 96) &&
                       localY < MulDiv(398, m_dpi, 96)) {
                StartCleaner();
            }
            break;
        case SidebarButton::Player:
            if (localY >= MulDiv(84, m_dpi, 96) &&
                localY < MulDiv(234, m_dpi, 96)) {
                const int row = (localY - MulDiv(84, m_dpi, 96)) /
                    MulDiv(50, m_dpi, 96);
                const wchar_t* urls[] = {
                    L"https://open.spotify.com", L"https://music.youtube.com",
                    L"https://soundcloud.com"
                };
                const wchar_t* names[] = { L"Spotify", L"YouTube Music", L"SoundCloud" };
                if (row >= 0 && row < 3)
                    AddTab(names[row], urls[row], RGB(100, 211, 255), false);
            } else if (localY >= MulDiv(256, m_dpi, 96) &&
                       localY < MulDiv(300, m_dpi, 96)) {
                m_activeSidebarPanel = SidebarButton::Bookmarks;
                m_sidebarScrollOffset = 0;
                RefreshSidebarData();
                InvalidateRect(m_hWnd, nullptr, FALSE);
            }
            break;
        case SidebarButton::Shortcuts: {
            const int row = (localY - MulDiv(78, m_dpi, 96)) / MulDiv(42, m_dpi, 96);
            if (row >= 0 && static_cast<size_t>(row) < m_shortcuts.size()) {
                const ShortcutTile& shortcut = m_shortcuts[static_cast<size_t>(row)];
                AddTab(shortcut.name, shortcut.url, shortcut.iconBg, false);
            }
            break;
        }
        case SidebarButton::Bookmarks:
            if (localY >= MulDiv(70, m_dpi, 96) &&
                localY < MulDiv(112, m_dpi, 96)) {
                SaveCurrentBookmark();
            } else if (localY >= MulDiv(124, m_dpi, 96)) {
                const int row = (localY - MulDiv(124, m_dpi, 96)) /
                    MulDiv(42, m_dpi, 96);
                const size_t index = static_cast<size_t>(row + m_sidebarScrollOffset);
                if (row >= 0 && index < m_bookmarks.size()) {
                    const SavedBookmark& bookmark = m_bookmarks[index];
                    AddTab(bookmark.title, bookmark.url, RGB(100, 211, 255), false);
                }
            }
            break;
        case SidebarButton::History:
            if (localY >= MulDiv(76, m_dpi, 96) &&
                localY < MulDiv(356, m_dpi, 96)) {
                const int row = (localY - MulDiv(76, m_dpi, 96)) /
                    MulDiv(34, m_dpi, 96);
                const size_t index = static_cast<size_t>(row + m_sidebarScrollOffset);
                if (row >= 0 && index < m_searchHistory.size()) {
                    AddTab(L"Search", L"bloxcy://home", RGB(100, 211, 255), true);
                    if (m_browser) m_browser->NavigateOrSearch(m_searchHistory[index]);
                }
            } else if (localY >= MulDiv(374, m_dpi, 96) &&
                       localY < MulDiv(420, m_dpi, 96)) {
                OpenSettingsTab();
            } else if (localY >= MulDiv(430, m_dpi, 96) &&
                       localY < MulDiv(476, m_dpi, 96)) {
                const int result = MessageBoxW(m_hWnd,
                    L"Clear locally saved search queries? Browser history is managed separately in Cleaner.",
                    L"Search history", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
                if (result == IDYES) {
                    std::wstring error;
                    if (!m_accountStore.ClearSearchHistory(m_accountSession, error)) {
                        MessageBoxW(m_hWnd, error.c_str(), L"Search history",
                                    MB_OK | MB_ICONERROR);
                    } else {
                        RefreshSidebarData();
                        InvalidateRect(m_hWnd, nullptr, FALSE);
                    }
                }
            }
            break;
        default:
            break;
    }
}

void WindowManager::OpenFile() {
    std::vector<wchar_t> path(32768, L'\0');
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = m_hWnd;
    dialog.lpstrFilter = L"All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    dialog.lpstrTitle = L"Open a local file";
    if (!GetOpenFileNameW(&dialog)) {
        const DWORD error = CommDlgExtendedError();
        if (error != 0) {
            MessageBoxW(m_hWnd, L"The file picker could not be opened.",
                        L"Bloxcy REVO", MB_OK | MB_ICONERROR);
        }
        return;
    }
    const std::wstring fullPath(path.data());
    const size_t separator = fullPath.find_last_of(L"\\/");
    const std::wstring name = separator == std::wstring::npos
        ? fullPath : fullPath.substr(separator + 1);
    AddTab(name, fullPath, RGB(156, 163, 175), false);
}

void WindowManager::ShowSettingsMenu() {
    HMENU menu = CreatePopupMenu();
    HMENU themeMenu = CreatePopupMenu();
    if (!menu || !themeMenu) {
        if (menu) DestroyMenu(menu);
        if (themeMenu) DestroyMenu(themeMenu);
        MessageBoxW(m_hWnd, L"Could not open the settings menu.",
                    L"Bloxcy REVO Settings", MB_OK | MB_ICONERROR);
        return;
    }

    AppendMenuW(themeMenu, MF_STRING, kThemeObsidian, L"Obsidian");
    AppendMenuW(themeMenu, MF_STRING, kThemeLight, L"Light");
    AppendMenuW(themeMenu, MF_STRING, kThemeOcean, L"Ocean");
    AppendMenuW(themeMenu, MF_STRING, kThemeForest, L"Forest");

    const UINT selectedTheme = kThemeObsidian + static_cast<UINT>(m_theme);
    CheckMenuRadioItem(themeMenu, kThemeObsidian, kThemeForest, selectedTheme, MF_BYCOMMAND);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(themeMenu), L"Color theme");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (m_sidebarVisible ? MF_CHECKED : MF_UNCHECKED),
                kCommandToggleSidebar, L"Show sidebar");
    AppendMenuW(menu, MF_STRING | (m_sidebarExpanded ? MF_CHECKED : MF_UNCHECKED),
                kCommandExpandSidebar, L"Expand sidebar");
    AppendMenuW(menu, MF_STRING | (m_compactTabs ? MF_CHECKED : MF_UNCHECKED),
                kCommandCompactTabs, L"Compact tabs");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCommandNewTab, L"New tab\tCtrl+T");
    AppendMenuW(menu, MF_STRING, kHotkeyOpenFile, L"Open local file...\tCtrl+O");
    AppendMenuW(menu, MF_STRING, kCommandReload, L"Reload page\tCtrl+R");
    AppendMenuW(menu, MF_STRING, kCommandFocusAddress, L"Focus address bar\tCtrl+L");
    AppendMenuW(menu, MF_STRING, kCommandBack, L"Back\tAlt+Left");
    AppendMenuW(menu, MF_STRING, kCommandForward, L"Forward\tAlt+Right");
    AppendMenuW(menu, MF_STRING, kCommandDeveloperTools, L"Developer Tools\tF12");
    AppendMenuW(menu, MF_STRING, kCommandKeyboardShortcuts, L"Keyboard shortcuts");
    AppendMenuW(menu, MF_STRING, kCommandReopenTab, L"Reopen closed tab\tCtrl+Shift+T");

    RECT clientRect = {};
    GetClientRect(m_hWnd, &clientRect);
    const RECT settingsRect = GetNavButtonRect(NavButton::Settings, clientRect.right);
    POINT menuPoint = { settingsRect.left, settingsRect.bottom };
    ClientToScreen(m_hWnd, &menuPoint);

    SetForegroundWindow(m_hWnd);
    const UINT command = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD,
                                        menuPoint.x, menuPoint.y, 0, m_hWnd, nullptr);
    PostMessageW(m_hWnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    switch (command) {
        case kThemeObsidian: SetTheme(Theme::Obsidian); break;
        case kThemeLight: SetTheme(Theme::Light); break;
        case kThemeOcean: SetTheme(Theme::Ocean); break;
        case kThemeForest: SetTheme(Theme::Forest); break;
        case kCommandToggleSidebar:
            m_sidebarVisible = !m_sidebarVisible;
            SaveAppearanceSetting(L"SidebarVisible", m_sidebarVisible);
            ResizeBrowserView();
            InvalidateRect(m_hWnd, nullptr, FALSE);
            break;
        case kCommandExpandSidebar:
            m_sidebarExpanded = !m_sidebarExpanded;
            SaveAppearanceSetting(L"SidebarExpanded", m_sidebarExpanded);
            ResizeBrowserView();
            InvalidateRect(m_hWnd, nullptr, FALSE);
            break;
        case kCommandCompactTabs:
            m_compactTabs = !m_compactTabs;
            SaveAppearanceSetting(L"CompactTabs", m_compactTabs);
            ResizeBrowserView();
            InvalidateRect(m_hWnd, nullptr, FALSE);
            break;
        case kCommandNewTab: AddTab(L"New Tab", L"bloxcy://home", RGB(78, 228, 255), true); break;
        case kHotkeyOpenFile: OpenFile(); break;
        case kCommandReload: if (m_browser) m_browser->Reload(); break;
        case kCommandFocusAddress: BeginAddressEdit(); break;
        case kCommandBack: if (m_browser) m_browser->Back(); break;
        case kCommandForward: if (m_browser) m_browser->Forward(); break;
        case kCommandDeveloperTools: if (m_browser) m_browser->OpenDevTools(); break;
        case kCommandKeyboardShortcuts: if (m_browser) m_browser->ShowKeyboardShortcuts(); break;
        case kCommandReopenTab: ReopenClosedTab(); break;
        default: break;
    }
}

void WindowManager::ExtendFrameIntoClientArea() {
    if (!m_hWnd) return;

    // Extend 1px frame into client area to preserve native DWM drop shadow
    MARGINS margins = { 1, 1, 1, 1 };
    DwmExtendFrameIntoClientArea(m_hWnd, &margins);
}

bool WindowManager::Initialize(HINSTANCE hInstance, const std::wstring& title, int width, int height) {
    m_hInstance = hInstance ? hInstance : GetModuleHandleW(nullptr);
    m_title = title;
    m_appDirectory = GetExecutableDirectory();
    if (m_appDirectory.empty()) return false;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls = {};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = StaticWndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = m_hInstance;
    wcex.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    wcex.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wcex.hbrBackground = nullptr;
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = m_className.c_str();
    wcex.hIconSm = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));

    RegisterClassExW(&wcex);

    // Modern Chrome-style frameless window style:
    // WS_POPUP removes the ugly native title bar.
    // WS_THICKFRAME provides native resize handles on all borders.
    // WS_MINIMIZEBOX and WS_MAXIMIZEBOX provide taskbar integration and Aero animations.
    DWORD dwStyle = WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    DWORD dwExStyle = WS_EX_APPWINDOW;

    RECT workArea = {};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0)) {
        workArea = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    }
    const int availableWidth = workArea.right - workArea.left;
    const int availableHeight = workArea.bottom - workArea.top;
    width = std::min(width, std::max(600, availableWidth - 80));
    height = std::min(height, std::max(450, availableHeight - 80));
    const int posX = workArea.left + (availableWidth - width) / 2;
    const int posY = workArea.top + (availableHeight - height) / 2;

    m_hWnd = CreateWindowExW(
        dwExStyle,
        m_className.c_str(),
        m_title.c_str(),
        dwStyle,
        posX,
        posY,
        width,
        height,
        nullptr,
        nullptr,
        m_hInstance,
        this
    );

    if (!m_hWnd) return false;

    m_dpi = GetDpiForWindow(m_hWnd);
    if (m_dpi == 0) m_dpi = 96;

    EnableImmersiveDarkMode();
    ExtendFrameIntoClientArea();

    SetWindowPos(m_hWnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    m_browser = std::make_shared<BrowserHost>();
    if (m_accountSession.username.empty() ||
        !m_browser->Initialize(m_hWnd, m_appDirectory, m_accountSession)) return false;
    m_browser->SetTheme(GetThemeName(m_theme));
    ApplyResourceMode(m_resourceMode);
    if (!SetTimer(m_hWnd, kMemoryEstimateTimer, 3000, nullptr)) {
        MessageBoxW(m_hWnd, L"Could not start active-page memory sampling.",
                    L"Bloxcy REVO Performance", MB_OK | MB_ICONWARNING);
    }

    RegisterHotKey(m_hWnd, kHotkeyNewTab, MOD_CONTROL | MOD_NOREPEAT, 'T');
    RegisterHotKey(m_hWnd, kHotkeyCloseTab, MOD_CONTROL | MOD_NOREPEAT, 'W');
    RegisterHotKey(m_hWnd, kHotkeyFocusAddress, MOD_CONTROL | MOD_NOREPEAT, 'L');
    RegisterHotKey(m_hWnd, kHotkeyReload, MOD_CONTROL | MOD_NOREPEAT, 'R');
    RegisterHotKey(m_hWnd, kHotkeyReloadF5, MOD_NOREPEAT, VK_F5);
    RegisterHotKey(m_hWnd, kHotkeyDeveloperTools, MOD_NOREPEAT, VK_F12);
    RegisterHotKey(m_hWnd, kHotkeyDeveloperToolsShortcut, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'I');
    RegisterHotKey(m_hWnd, kHotkeyBack, MOD_ALT | MOD_NOREPEAT, VK_LEFT);
    RegisterHotKey(m_hWnd, kHotkeyForward, MOD_ALT | MOD_NOREPEAT, VK_RIGHT);
    RegisterHotKey(m_hWnd, kHotkeyNextTab, MOD_CONTROL | MOD_NOREPEAT, VK_TAB);
    RegisterHotKey(m_hWnd, kHotkeyPreviousTab, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_TAB);
    RegisterHotKey(m_hWnd, kHotkeyResetZoom, MOD_CONTROL | MOD_NOREPEAT, '0');
    RegisterHotKey(m_hWnd, kHotkeyReopenTab, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'T');
    RegisterHotKey(m_hWnd, kHotkeyDeveloperToolsJ, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'J');
    RegisterHotKey(m_hWnd, kHotkeyZoomIn, MOD_CONTROL | MOD_NOREPEAT, VK_OEM_PLUS);
    RegisterHotKey(m_hWnd, kHotkeyZoomInShift, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_OEM_PLUS);
    RegisterHotKey(m_hWnd, kHotkeyZoomOut, MOD_CONTROL | MOD_NOREPEAT, VK_OEM_MINUS);
    RegisterHotKey(m_hWnd, kHotkeyMenuAltF, MOD_ALT | MOD_NOREPEAT, 'F');
    RegisterHotKey(m_hWnd, kHotkeyMenuAltE, MOD_ALT | MOD_NOREPEAT, 'E');
    RegisterHotKey(m_hWnd, kHotkeyHome, MOD_ALT | MOD_NOREPEAT, VK_HOME);
    RegisterHotKey(m_hWnd, kHotkeyReadAloud, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'S');
    RegisterHotKey(m_hWnd, kHotkeyOpenFile, MOD_CONTROL | MOD_NOREPEAT, 'O');
    for (int index = 0; index < 9; ++index) {
        RegisterHotKey(m_hWnd, kHotkeyTab1 + index, MOD_CONTROL | MOD_NOREPEAT, '1' + index);
    }

    return true;
}

void WindowManager::Show(int nCmdShow) {
    if (m_hWnd) {
        (void)nCmdShow;
        ShowWindow(m_hWnd, SW_SHOWNORMAL);
        UpdateWindow(m_hWnd);
    }
}

int WindowManager::RunMessageLoop() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void WindowManager::Close() {
    if (m_hWnd) {
        PostMessageW(m_hWnd, WM_CLOSE, 0, 0);
    }
}

void WindowManager::SetAccountSession(AccountSession&& session) {
    m_accountSession = std::move(session);
}

void WindowManager::ResizeBrowserView() {
    if (!m_hWnd || !m_browser) return;
    RECT clientRect = {};
    GetClientRect(m_hWnd, &clientRect);
    RECT bounds = { GetSidebarWidth(), GetTotalTopHeight(),
                    clientRect.right, clientRect.bottom };
    m_browser->SetBounds(bounds);

    if (m_addressEdit && IsWindowVisible(m_addressEdit)) {
        RECT addressRect = GetOmniboxRect(clientRect.right);
        const RECT trustRect = GetNavButtonRect(NavButton::TrustSignals, clientRect.right);
        addressRect.left += MulDiv(38, m_dpi, 96);
        addressRect.right = trustRect.left - MulDiv(8, m_dpi, 96);
        addressRect.top += MulDiv(4, m_dpi, 96);
        addressRect.bottom -= MulDiv(4, m_dpi, 96);
        SetWindowPos(m_addressEdit, HWND_TOP, addressRect.left, addressRect.top,
                     addressRect.right - addressRect.left, addressRect.bottom - addressRect.top,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

void WindowManager::BeginAddressEdit() {
    if (!m_hWnd) return;
    RECT clientRect = {};
    GetClientRect(m_hWnd, &clientRect);
    const RECT omniboxRect = GetOmniboxRect(clientRect.right);
    RECT addressRect = omniboxRect;
    const RECT trustRect = GetNavButtonRect(NavButton::TrustSignals, clientRect.right);
    addressRect.left += MulDiv(38, m_dpi, 96);
    addressRect.right = trustRect.left - MulDiv(8, m_dpi, 96);
    addressRect.top += MulDiv(4, m_dpi, 96);
    addressRect.bottom -= MulDiv(4, m_dpi, 96);

    if (!m_addressEdit) {
        m_addressEdit = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
            addressRect.left, addressRect.top,
            addressRect.right - addressRect.left, addressRect.bottom - addressRect.top,
            m_hWnd, reinterpret_cast<HMENU>(1), m_hInstance, nullptr);
        if (!m_addressEdit) {
            MessageBoxW(m_hWnd, L"Could not create the address bar.",
                        L"Bloxcy REVO", MB_OK | MB_ICONERROR);
            return;
        }
        m_addressFont = CreateFontW(
            -MulDiv(12, m_dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable");
        if (!m_addressFont) {
            m_addressFont = CreateFontW(
                -MulDiv(12, m_dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        }
        if (!m_addressFont) {
            DestroyWindow(m_addressEdit);
            m_addressEdit = nullptr;
            MessageBoxW(m_hWnd, L"Could not create the address bar font.",
                        L"Bloxcy REVO", MB_OK | MB_ICONERROR);
            return;
        }
        SendMessageW(m_addressEdit, WM_SETFONT, reinterpret_cast<WPARAM>(m_addressFont), TRUE);
        if (!SetWindowSubclass(m_addressEdit, AddressEditProc, 1,
                               reinterpret_cast<DWORD_PTR>(m_hWnd))) {
            DestroyWindow(m_addressEdit);
            m_addressEdit = nullptr;
            MessageBoxW(m_hWnd, L"Could not enable address bar keyboard handling.",
                        L"Bloxcy REVO", MB_OK | MB_ICONERROR);
            return;
        }
    }

    std::wstring address;
    if (m_activeTabIndex < m_tabs.size()) {
        const auto& tab = m_tabs[m_activeTabIndex];
        if (!tab.isHome) address = tab.searchQuery.empty() ? tab.url : tab.searchQuery;
    }
    SetWindowTextW(m_addressEdit, address.c_str());
    SetWindowPos(m_addressEdit, HWND_TOP, addressRect.left, addressRect.top,
                 addressRect.right - addressRect.left, addressRect.bottom - addressRect.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetFocus(m_addressEdit);
    SendMessageW(m_addressEdit, EM_SETSEL, 0, -1);
}

void WindowManager::NavigateFromAddressEdit() {
    if (!m_addressEdit || !m_browser) return;
    const int length = GetWindowTextLengthW(m_addressEdit);
    std::wstring input(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(m_addressEdit, input.data(), length + 1);
    input.resize(static_cast<size_t>(length));
    ShowWindow(m_addressEdit, SW_HIDE);
    SetFocus(m_hWnd);
    m_browser->NavigateOrSearch(input);
}

void WindowManager::UpdateActiveTabFromBrowser() {
    if (!m_browser || m_activeTabIndex >= m_tabs.size()) return;
    BrowserTab& tab = m_tabs[m_activeTabIndex];
    if (tab.isHibernated) return;
    if (m_browser->IsSettingsPage()) {
        tab.isHome = false;
        tab.isSettings = true;
        tab.url = L"bloxcy://settings";
        tab.searchQuery.clear();
    } else if (m_browser->IsHomePage()) {
        tab.isHome = true;
        tab.isSettings = false;
        tab.url = L"bloxcy://home";
        tab.searchQuery.clear();
    } else if (m_browser->IsSearchPage()) {
        tab.isHome = false;
        tab.isSettings = false;
        tab.url = L"bloxcy://search";
        tab.searchQuery = m_browser->GetLastSearchQuery();
    } else {
        const std::wstring url = m_browser->GetCurrentUrl();
        if (!url.empty()) tab.url = url;
        tab.isHome = false;
        tab.isSettings = false;
        tab.searchQuery.clear();
    }
    tab.isHibernated = false;

    const std::wstring title = m_browser->GetDocumentTitle();
    if (!title.empty()) tab.title = title;
    SetWindowTextW(m_hWnd, (tab.title + L" - Bloxcy REVO").c_str());
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void WindowManager::HandleHotkey(int hotkeyId) {
    if (hotkeyId >= kHotkeyTab1 && hotkeyId <= kHotkeyTab1 + 8) {
        const size_t tabIndex = static_cast<size_t>(hotkeyId - kHotkeyTab1);
        if (!m_tabs.empty()) SelectTab(std::min(tabIndex, m_tabs.size() - 1));
        return;
    }

    switch (hotkeyId) {
        case kHotkeyNewTab:
            AddTab(L"New Tab", L"bloxcy://home", RGB(78, 228, 255), true);
            break;
        case kHotkeyCloseTab:
            CloseTab(m_activeTabIndex);
            break;
        case kHotkeyFocusAddress:
            BeginAddressEdit();
            break;
        case kHotkeyReload:
        case kHotkeyReloadF5:
            if (m_browser) m_browser->Reload();
            break;
        case kHotkeyDeveloperTools:
        case kHotkeyDeveloperToolsShortcut:
        case kHotkeyDeveloperToolsJ:
            if (m_browser) m_browser->OpenDevTools();
            break;
        case kHotkeyBack:
            if (m_browser) m_browser->Back();
            break;
        case kHotkeyForward:
            if (m_browser) m_browser->Forward();
            break;
        case kHotkeyNextTab:
            if (!m_tabs.empty()) SelectTab((m_activeTabIndex + 1) % m_tabs.size());
            break;
        case kHotkeyPreviousTab:
            if (!m_tabs.empty()) SelectTab((m_activeTabIndex + m_tabs.size() - 1) % m_tabs.size());
            break;
        case kHotkeyResetZoom:
            if (m_browser) m_browser->ResetZoom();
            break;
        case kHotkeyReopenTab:
            ReopenClosedTab();
            break;
        case kHotkeyZoomIn:
        case kHotkeyZoomInShift:
            if (m_browser) m_browser->ChangeZoom(0.1);
            break;
        case kHotkeyZoomOut:
            if (m_browser) m_browser->ChangeZoom(-0.1);
            break;
        case kHotkeyMenuAltF:
        case kHotkeyMenuAltE:
            ShowSettingsMenu();
            break;
        case kHotkeyHome:
            if (m_browser) m_browser->NavigateHome();
            break;
        case kHotkeyReadAloud:
            if (m_browser) m_browser->SpeakPage();
            break;
        case kHotkeyOpenFile:
            OpenFile();
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Layout Geometry Calculations
// ---------------------------------------------------------------------------

RECT WindowManager::GetCaptionButtonRect(CaptionButton btn) const {
    RECT clientRect = {};
    if (!m_hWnd) return clientRect;
    GetClientRect(m_hWnd, &clientRect);

    int topHeight = GetTabStripHeight();
    int btnWidth = MulDiv(m_baseCaptionBtnWidth, m_dpi, 96);
    RECT r = { 0, 0, 0, topHeight };

    switch (btn) {
        case CaptionButton::Close:
            r.right = clientRect.right;
            r.left = r.right - btnWidth;
            break;
        case CaptionButton::Maximize:
            r.right = clientRect.right - btnWidth;
            r.left = r.right - btnWidth;
            break;
        case CaptionButton::Minimize:
            r.right = clientRect.right - (btnWidth * 2);
            r.left = r.right - btnWidth;
            break;
        default:
            break;
    }
    return r;
}

RECT WindowManager::GetTabRect(size_t index, int totalWidth) const {
    RECT r = { 0, 0, 0, 0 };
    if (index >= m_tabs.size()) return r;

    const int visibleCount = GetVisibleTabCount(totalWidth);
    const int reserved = MulDiv(m_baseCaptionBtnWidth * 3 + 54, m_dpi, 96);
    const int available = std::max(MulDiv(120, m_dpi, 96), totalWidth - reserved);
    const int tabWidth = std::max(MulDiv(120, m_dpi, 96), available / visibleCount);
    const int visibleIndex = static_cast<int>(index) - m_tabScrollOffset;
    const int gap = MulDiv(4, m_dpi, 96);
    r.left = MulDiv(8, m_dpi, 96) + visibleIndex * tabWidth;
    r.right = r.left + tabWidth - gap;
    r.top = MulDiv(5, m_dpi, 96);
    r.bottom = GetTabStripHeight() - MulDiv(4, m_dpi, 96);
    return r;
}

RECT WindowManager::GetTabCloseRect(const RECT& tabRect) const {
    const int closeSize = MulDiv(20, m_dpi, 96);
    const int padRight = MulDiv(7, m_dpi, 96);
    const int centerY = tabRect.top + (tabRect.bottom - tabRect.top) / 2;

    RECT r;
    r.right = tabRect.right - padRight;
    r.left = r.right - closeSize;
    r.top = centerY - closeSize / 2;
    r.bottom = r.top + closeSize;
    return r;
}

RECT WindowManager::GetNewTabButtonRect(int totalWidth) const {
    const int visibleCount = GetVisibleTabCount(totalWidth);
    const int reserved = MulDiv(m_baseCaptionBtnWidth * 3 + 54, m_dpi, 96);
    const int available = std::max(MulDiv(120, m_dpi, 96), totalWidth - reserved);
    const int tabWidth = std::max(MulDiv(120, m_dpi, 96), available / visibleCount);
    const int visibleTabs = std::min(static_cast<int>(m_tabs.size()) - m_tabScrollOffset,
                                     visibleCount);
    const int left = MulDiv(8, m_dpi, 96) + std::max(0, visibleTabs) * tabWidth;
    const int top = MulDiv(8, m_dpi, 96);
    const int size = MulDiv(26, m_dpi, 96);
    RECT r = { left, top, left + size, top + size };
    return r;
}

RECT WindowManager::GetNavButtonRect(NavButton btn, int totalWidth) const {
    (void)totalWidth;
    int stripH = GetTabStripHeight();
    int navH = GetNavBarHeight();
    int btnSize = MulDiv(28, m_dpi, 96);
    int centerY = stripH + (navH / 2);
    int topY = centerY - btnSize / 2;

    RECT r = { 0, topY, 0, topY + btnSize };
    int pad = MulDiv(10, m_dpi, 96);
    int step = MulDiv(34, m_dpi, 96);

    switch (btn) {
        case NavButton::Back:
            r.left = pad;
            r.right = r.left + btnSize;
            break;
        case NavButton::Forward:
            r.left = pad + step;
            r.right = r.left + btnSize;
            break;
        case NavButton::Reload:
            r.left = pad + step * 2;
            r.right = r.left + btnSize;
            break;
        case NavButton::Settings:
            r.right = totalWidth - pad;
            r.left = r.right - btnSize;
            break;
        case NavButton::ForceDark: {
            const RECT settings = GetNavButtonRect(NavButton::Settings, totalWidth);
            r.right = settings.left - MulDiv(5, m_dpi, 96);
            r.left = r.right - btnSize;
            break;
        }
        case NavButton::TrustSignals: {
            const RECT omnibox = GetOmniboxRect(totalWidth);
            r.left = omnibox.right - MulDiv(86, m_dpi, 96);
            r.right = omnibox.right - MulDiv(32, m_dpi, 96);
            r.top = omnibox.top + MulDiv(4, m_dpi, 96);
            r.bottom = omnibox.bottom - MulDiv(4, m_dpi, 96);
            break;
        }
        default:
            break;
    }
    return r;
}

RECT WindowManager::GetOmniboxRect(int totalWidth) const {
    int stripH = GetTabStripHeight();
    int navH = GetNavBarHeight();
    int omniH = MulDiv(30, m_dpi, 96);
    int centerY = stripH + (navH / 2);

    int pad = MulDiv(10, m_dpi, 96);
    int step = MulDiv(34, m_dpi, 96);
    int left = pad + step * 3 + MulDiv(4, m_dpi, 96);
    int right = totalWidth - pad - step * 2 - MulDiv(4, m_dpi, 96);

    RECT r = { left, centerY - omniH / 2, right, centerY + omniH / 2 };
    return r;
}

RECT WindowManager::GetSidebarButtonRect(SidebarButton button) const {
    if (!m_sidebarVisible) return { 0, 0, 0, 0 };
    const int top = GetTotalTopHeight();
    const int inset = MulDiv(7, m_dpi, 96);
    const int height = MulDiv(40, m_dpi, 96);
    RECT rect = { inset, top, MulDiv(58, m_dpi, 96) - inset, top + height };
    switch (button) {
        case SidebarButton::Home:
        case SidebarButton::OpenFile:
        case SidebarButton::Search:
        case SidebarButton::Controls:
        case SidebarButton::Cleaner:
        case SidebarButton::Player:
        case SidebarButton::Shortcuts:
        case SidebarButton::Bookmarks:
        case SidebarButton::History:
        case SidebarButton::Settings:
            rect.top += MulDiv(8 + 44 * (static_cast<int>(button) -
                static_cast<int>(SidebarButton::Home)), m_dpi, 96);
            break;
        case SidebarButton::ToggleSize: {
            RECT client = {};
            GetClientRect(m_hWnd, &client);
            rect.top = std::max(top, static_cast<int>(client.bottom) - MulDiv(52, m_dpi, 96));
            break;
        }
        default:
            return { 0, 0, 0, 0 };
    }
    rect.bottom = rect.top + height;
    return rect;
}

SidebarButton WindowManager::HitTestSidebarButton(int x, int y) const {
    if (!m_sidebarVisible || x < 0 || x >= MulDiv(58, m_dpi, 96) ||
        y < GetTotalTopHeight()) {
        return SidebarButton::None;
    }
    const SidebarButton buttons[] = {
        SidebarButton::Home, SidebarButton::OpenFile, SidebarButton::Search,
        SidebarButton::Controls, SidebarButton::Cleaner, SidebarButton::Player,
        SidebarButton::Shortcuts, SidebarButton::Bookmarks, SidebarButton::History,
        SidebarButton::Settings, SidebarButton::ToggleSize
    };
    for (const SidebarButton button : buttons) {
        const RECT rect = GetSidebarButtonRect(button);
        if (x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom) {
            return button;
        }
    }
    return SidebarButton::None;
}

// ---------------------------------------------------------------------------
// Hit Testing
// ---------------------------------------------------------------------------

CaptionButton WindowManager::HitTestCaptionButtons(int x, int y) const {
    if (y < 0 || y > GetTabStripHeight()) return CaptionButton::None;

    CaptionButton btns[] = { CaptionButton::Close, CaptionButton::Maximize, CaptionButton::Minimize };
    for (CaptionButton b : btns) {
        RECT r = GetCaptionButtonRect(b);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) {
            return b;
        }
    }
    return CaptionButton::None;
}

int WindowManager::HitTestTabs(int x, int y, int totalWidth) const {
    if (x < 0 || x >= totalWidth || y < 0 || y >= GetTabStripHeight()) return -1;
    const size_t first = static_cast<size_t>(std::max(0, m_tabScrollOffset));
    const size_t end = std::min(m_tabs.size(),
        first + static_cast<size_t>(GetVisibleTabCount(totalWidth)));
    for (size_t i = first; i < end; ++i) {
        RECT r = GetTabRect(i, totalWidth);
        if (x >= r.left && x < r.right && y >= r.top && y <= r.bottom) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int WindowManager::HitTestTabClose(int x, int y, int totalWidth) const {
    int tabIdx = HitTestTabs(x, y, totalWidth);
    if (tabIdx >= 0) {
        RECT tabR = GetTabRect(static_cast<size_t>(tabIdx), totalWidth);
        RECT closeR = GetTabCloseRect(tabR);
        if (x >= closeR.left && x <= closeR.right && y >= closeR.top && y <= closeR.bottom) {
            return tabIdx;
        }
    }
    return -1;
}

bool WindowManager::HitTestNewTabButton(int x, int y, int totalWidth) const {
    RECT r = GetNewTabButtonRect(totalWidth);
    return (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom);
}

NavButton WindowManager::HitTestNavButtons(int x, int y, int totalWidth) const {
    NavButton btns[] = {
        NavButton::Back, NavButton::Forward, NavButton::Reload,
        NavButton::TrustSignals, NavButton::ForceDark, NavButton::Settings
    };
    for (NavButton b : btns) {
        RECT r = GetNavButtonRect(b, totalWidth);
        if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
            return b;
        }
    }
    RECT omniR = GetOmniboxRect(totalWidth);
    if (x >= omniR.left && x <= omniR.right && y >= omniR.top && y <= omniR.bottom) {
        return NavButton::Omnibox;
    }
    return NavButton::None;
}

int WindowManager::HitTestShortcutTiles(int x, int y) const {
    for (size_t i = 0; i < m_shortcuts.size(); ++i) {
        const RECT& r = m_shortcuts[i].rect;
        if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Window Messages & Frame Handling
// ---------------------------------------------------------------------------

LRESULT CALLBACK WindowManager::StaticWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WindowManager* pThis = nullptr;
    if (msg == WM_NCCREATE) {
        auto* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = reinterpret_cast<WindowManager*>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        if (pThis) pThis->m_hWnd = hWnd;
    } else {
        pThis = reinterpret_cast<WindowManager*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    if (pThis) return pThis->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK WindowManager::AddressEditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                                 UINT_PTR subclassId, DWORD_PTR refData) {
    (void)subclassId;
    HWND parent = reinterpret_cast<HWND>(refData);
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        PostMessageW(parent, kNavigateAddressMessage, 0, 0);
        return 0;
    }
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
        PostMessageW(parent, kCancelAddressMessage, 0, 0);
        return 0;
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

LRESULT WindowManager::HandleNcCalcSize(WPARAM wParam, LPARAM lParam) {
    if (wParam == TRUE) {
        // Return 0: The client area extends to the exact top edge of the window.
        // This removes the ugly standard titlebar completely!
        if (IsZoomed(m_hWnd)) {
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
            HMONITOR hMonitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = {};
            mi.cbSize = sizeof(MONITORINFO);
            if (GetMonitorInfoW(hMonitor, &mi)) {
                params->rgrc[0] = mi.rcWork;
            }
        }
        return 0;
    }
    return DefWindowProcW(m_hWnd, WM_NCCALCSIZE, wParam, lParam);
}

LRESULT WindowManager::HandleNcHitTest(WPARAM wParam, LPARAM lParam) {
    (void)wParam;
    POINT cursor = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

    RECT windowRect;
    GetWindowRect(m_hWnd, &windowRect);

    // Native resize borders when not maximized
    if (!IsZoomed(m_hWnd)) {
        const int resizeBorder = MulDiv(6, m_dpi, 96);
        bool top    = cursor.y >= windowRect.top && cursor.y < windowRect.top + resizeBorder;
        bool bottom = cursor.y < windowRect.bottom && cursor.y >= windowRect.bottom - resizeBorder;
        bool left   = cursor.x >= windowRect.left && cursor.x < windowRect.left + resizeBorder;
        bool right  = cursor.x < windowRect.right && cursor.x >= windowRect.right - resizeBorder;

        if (top && left)     return HTTOPLEFT;
        if (top && right)    return HTTOPRIGHT;
        if (bottom && left)  return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (top)             return HTTOP;
        if (bottom)          return HTBOTTOM;
        if (left)            return HTLEFT;
        if (right)           return HTRIGHT;
    }

    // Convert cursor to client coordinates
    POINT clientPt = cursor;
    ScreenToClient(m_hWnd, &clientPt);

    RECT clientRect = {};
    GetClientRect(m_hWnd, &clientRect);
    int totalW = clientRect.right;
    int stripH = GetTabStripHeight();

    // Inside Tab Strip Area (top of window)
    if (clientPt.y >= 0 && clientPt.y < stripH) {
        // Caption control buttons (Min, Max, Close)
        if (HitTestCaptionButtons(clientPt.x, clientPt.y) != CaptionButton::None) {
            return HTCLIENT;
        }

        // Tabs or '+' button
        if (HitTestTabs(clientPt.x, clientPt.y, totalW) >= 0 ||
            HitTestNewTabButton(clientPt.x, clientPt.y, totalW)) {
            return HTCLIENT;
        }

        // Empty space in tab strip: Acts as HTCAPTION for window dragging and double-click maximize!
        return HTCAPTION;
    }

    return HTCLIENT;
}

// ---------------------------------------------------------------------------
// Mouse Event Handlers
// ---------------------------------------------------------------------------

void WindowManager::HandleMouseMove(int x, int y) {
    if (!m_isTrackingMouse) {
        TRACKMOUSEEVENT tme = {};
        tme.cbSize = sizeof(TRACKMOUSEEVENT);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = m_hWnd;
        TrackMouseEvent(&tme);
        m_isTrackingMouse = true;
    }

    RECT clientRect;
    GetClientRect(m_hWnd, &clientRect);
    int totalW = clientRect.right - clientRect.left;

    if (m_draggedTabIndex >= 0 && (GetKeyState(VK_LBUTTON) & 0x8000)) {
        if (!m_isDraggingTab &&
            (std::abs(x - m_dragStartX) >= GetSystemMetrics(SM_CXDRAG) ||
             std::abs(y - m_dragStartY) >= GetSystemMetrics(SM_CYDRAG))) {
            m_isDraggingTab = true;
        }
        if (m_isDraggingTab) {
            const int targetIndex = HitTestTabs(x, y, totalW);
            if (targetIndex >= 0 && targetIndex != m_draggedTabIndex) {
                ReorderTab(static_cast<size_t>(m_draggedTabIndex),
                           static_cast<size_t>(targetIndex));
                m_draggedTabIndex = targetIndex;
                InvalidateRect(m_hWnd, nullptr, FALSE);
            }
        }
    }

    // Caption button hover
    CaptionButton capBtn = HitTestCaptionButtons(x, y);
    if (capBtn != m_hoveredCaptionBtn) {
        m_hoveredCaptionBtn = capBtn;
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }

    // Tabs hover
    int tabIdx = HitTestTabs(x, y, totalW);
    int tabCloseIdx = HitTestTabClose(x, y, totalW);
    bool newTabBtn = HitTestNewTabButton(x, y, totalW);

    if (tabIdx != m_hoveredTabIndex || tabCloseIdx != m_hoveredTabCloseIndex || newTabBtn != m_hoveredNewTabBtn) {
        m_hoveredTabIndex = tabIdx;
        m_hoveredTabCloseIndex = tabCloseIdx;
        m_hoveredNewTabBtn = newTabBtn;
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }

    // Nav button hover
    NavButton navBtn = HitTestNavButtons(x, y, totalW);
    if (navBtn != m_hoveredNavBtn) {
        m_hoveredNavBtn = navBtn;
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }

    const SidebarButton sidebarButton = HitTestSidebarButton(x, y);
    if (sidebarButton != m_hoveredSidebarButton) {
        m_hoveredSidebarButton = sidebarButton;
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }

    // Shortcut tiles hover
    int shortcutIdx = HitTestShortcutTiles(x, y);
    if (shortcutIdx != m_hoveredShortcutIndex) {
        m_hoveredShortcutIndex = shortcutIdx;
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

void WindowManager::HandleMouseLeave() {
    m_isTrackingMouse = false;
    m_hoveredCaptionBtn = CaptionButton::None;
    m_hoveredTabIndex = -1;
    m_hoveredTabCloseIndex = -1;
    m_hoveredNewTabBtn = false;
    m_hoveredNavBtn = NavButton::None;
    m_hoveredSidebarButton = SidebarButton::None;
    m_hoveredShortcutIndex = -1;
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void WindowManager::HandleMouseWheel(short delta, int x, int y) {
    if (m_sidebarExpanded && x >= MulDiv(58, m_dpi, 96) &&
        x < GetSidebarWidth() && y >= GetTotalTopHeight()) {
        size_t itemCount = 0;
        if (m_activeSidebarPanel == SidebarButton::Bookmarks)
            itemCount = m_bookmarks.size();
        else if (m_activeSidebarPanel == SidebarButton::History)
            itemCount = m_searchHistory.size();
        const int maxOffset = std::max(0, static_cast<int>(itemCount) - 8);
        m_sidebarScrollOffset = std::clamp(
            m_sidebarScrollOffset + (delta < 0 ? 1 : -1), 0, maxOffset);
        InvalidateRect(m_hWnd, nullptr, FALSE);
        return;
    }
    if (y < 0 || y >= GetTabStripHeight()) return;
    RECT client = {};
    GetClientRect(m_hWnd, &client);
    const int visibleCount = GetVisibleTabCount(client.right);
    const int maxOffset = std::max(0, static_cast<int>(m_tabs.size()) - visibleCount);
    m_tabScrollOffset = std::clamp(m_tabScrollOffset + (delta < 0 ? 1 : -1), 0, maxOffset);
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void WindowManager::HandleLButtonDown(int x, int y) {
    RECT clientRect;
    GetClientRect(m_hWnd, &clientRect);
    int totalW = clientRect.right - clientRect.left;

    if (m_sidebarExpanded && x >= MulDiv(58, m_dpi, 96) &&
        x < GetSidebarWidth() && y >= GetTotalTopHeight()) {
        HandleSidebarPanelClick(x, y);
        return;
    }

    const SidebarButton sidebarButton = HitTestSidebarButton(x, y);
    if (sidebarButton != SidebarButton::None) {
        switch (sidebarButton) {
            case SidebarButton::Home:
                if (m_browser) m_browser->NavigateHome();
                break;
            case SidebarButton::OpenFile:
                OpenFile();
                break;
            case SidebarButton::Search:
                BeginAddressEdit();
                break;
            case SidebarButton::Controls:
            case SidebarButton::Cleaner:
            case SidebarButton::Player:
            case SidebarButton::Shortcuts:
            case SidebarButton::Bookmarks:
            case SidebarButton::History:
                m_activeSidebarPanel = sidebarButton;
                m_sidebarScrollOffset = 0;
                m_sidebarExpanded = true;
                if (sidebarButton == SidebarButton::Bookmarks ||
                    sidebarButton == SidebarButton::History) {
                    RefreshSidebarData();
                }
                SaveAppearanceSetting(L"SidebarExpanded", true);
                ResizeBrowserView();
                break;
            case SidebarButton::Settings:
                OpenSettingsTab();
                break;
            case SidebarButton::ToggleSize:
                m_sidebarExpanded = !m_sidebarExpanded;
                if (m_sidebarExpanded &&
                    m_activeSidebarPanel == SidebarButton::None) {
                    m_activeSidebarPanel = SidebarButton::Controls;
                }
                SaveAppearanceSetting(L"SidebarExpanded", m_sidebarExpanded);
                ResizeBrowserView();
                break;
            default:
                break;
        }
        InvalidateRect(m_hWnd, nullptr, FALSE);
        return;
    }

    // Caption button press
    CaptionButton capBtn = HitTestCaptionButtons(x, y);
    if (capBtn != CaptionButton::None) {
        m_pressedCaptionBtn = capBtn;
        SetCapture(m_hWnd);
        InvalidateRect(m_hWnd, nullptr, FALSE);
        return;
    }

    // Tab close button press
    int tabCloseIdx = HitTestTabClose(x, y, totalW);
    if (tabCloseIdx >= 0) {
        CloseTab(static_cast<size_t>(tabCloseIdx));
        return;
    }

    // Tab selection press
    int tabIdx = HitTestTabs(x, y, totalW);
    if (tabIdx >= 0) {
        SelectTab(static_cast<size_t>(tabIdx));
        m_draggedTabIndex = tabIdx;
        m_dragStartX = x;
        m_dragStartY = y;
        m_isDraggingTab = false;
        SetCapture(m_hWnd);
        return;
    }

    // New Tab '+' button press
    if (HitTestNewTabButton(x, y, totalW)) {
        AddTab(L"New Tab", L"bloxcy://home", RGB(78, 228, 255), true);
        return;
    }

    // Nav buttons press
    NavButton navBtn = HitTestNavButtons(x, y, totalW);
    if (navBtn != NavButton::None) {
        m_pressedNavBtn = navBtn;
        SetCapture(m_hWnd);
        InvalidateRect(m_hWnd, nullptr, FALSE);
        return;
    }

    // Shortcut tiles press
    int shortcutIdx = HitTestShortcutTiles(x, y);
    if (shortcutIdx >= 0 && static_cast<size_t>(shortcutIdx) < m_shortcuts.size()) {
        const auto& sc = m_shortcuts[shortcutIdx];
        AddTab(sc.name, sc.url, sc.iconBg, false);
        return;
    }
}

void WindowManager::HandleLButtonUp(int x, int y) {
    if (GetCapture() == m_hWnd) {
        ReleaseCapture();
    }
    m_draggedTabIndex = -1;
    m_isDraggingTab = false;

    // Handle Caption Button Click Release
    CaptionButton pressed = m_pressedCaptionBtn;
    m_pressedCaptionBtn = CaptionButton::None;

    if (pressed != CaptionButton::None) {
        CaptionButton releasedOn = HitTestCaptionButtons(x, y);
        if (pressed == releasedOn) {
            switch (pressed) {
                case CaptionButton::Minimize:
                    ShowWindow(m_hWnd, SW_MINIMIZE);
                    break;
                case CaptionButton::Maximize:
                    if (IsZoomed(m_hWnd)) {
                        ShowWindow(m_hWnd, SW_RESTORE);
                    } else {
                        ShowWindow(m_hWnd, SW_MAXIMIZE);
                    }
                    break;
                case CaptionButton::Close:
                    Close();
                    break;
                default:
                    break;
            }
        }
    }

    // Handle Nav Button Click Release
    NavButton pressedNav = m_pressedNavBtn;
    m_pressedNavBtn = NavButton::None;
    if (pressedNav != NavButton::None) {
        RECT clientRect = {};
        GetClientRect(m_hWnd, &clientRect);
        if (pressedNav == HitTestNavButtons(x, y, clientRect.right)) {
            switch (pressedNav) {
                case NavButton::Back:
                    if (m_browser) m_browser->Back();
                    break;
                case NavButton::Forward:
                    if (m_browser) m_browser->Forward();
                    break;
                case NavButton::Reload:
                    if (m_browser) m_browser->Reload();
                    break;
                case NavButton::Omnibox:
                    BeginAddressEdit();
                    break;
                case NavButton::Settings:
                    OpenSettingsTab();
                    break;
                case NavButton::ForceDark:
                    if (m_browser) {
                        m_browser->SetForceDark(!m_browser->IsForceDarkEnabled());
                        InvalidateRect(m_hWnd, nullptr, FALSE);
                    }
                    break;
                case NavButton::TrustSignals:
                    if (m_browser) {
                        const std::wstring details = m_browser->GetTrustDetails();
                        MessageBoxW(m_hWnd, details.c_str(), L"Website trust signals",
                                    MB_OK | MB_ICONINFORMATION);
                    }
                    break;
                default:
                    break;
            }
        }
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

void WindowManager::ReorderTab(size_t fromIndex, size_t toIndex) {
    if (fromIndex >= m_tabs.size() || toIndex >= m_tabs.size() || fromIndex == toIndex) return;

    BrowserTab tab = std::move(m_tabs[fromIndex]);
    m_tabs.erase(m_tabs.begin() + fromIndex);
    m_tabs.insert(m_tabs.begin() + toIndex, std::move(tab));

    if (m_activeTabIndex == fromIndex) {
        m_activeTabIndex = toIndex;
    } else if (fromIndex < m_activeTabIndex && toIndex >= m_activeTabIndex) {
        --m_activeTabIndex;
    } else if (fromIndex > m_activeTabIndex && toIndex <= m_activeTabIndex) {
        ++m_activeTabIndex;
    }
}

void WindowManager::ShowTabContextMenu(size_t index, int x, int y) {
    if (index >= m_tabs.size()) return;

    HMENU menu = CreatePopupMenu();
    HMENU createGroupMenu = CreatePopupMenu();
    HMENU existingGroupsMenu = CreatePopupMenu();
    if (!menu || !createGroupMenu || !existingGroupsMenu) {
        if (menu) DestroyMenu(menu);
        if (createGroupMenu) DestroyMenu(createGroupMenu);
        if (existingGroupsMenu) DestroyMenu(existingGroupsMenu);
        MessageBoxW(m_hWnd, L"Could not open the tab group menu.",
                    L"Bloxcy REVO", MB_OK | MB_ICONERROR);
        return;
    }

    AppendMenuW(createGroupMenu, MF_STRING, kCreateTabGroupRed, L"Red");
    AppendMenuW(createGroupMenu, MF_STRING, kCreateTabGroupOrange, L"Orange");
    AppendMenuW(createGroupMenu, MF_STRING, kCreateTabGroupYellow, L"Yellow");
    AppendMenuW(createGroupMenu, MF_STRING, kCreateTabGroupGreen, L"Green");
    AppendMenuW(createGroupMenu, MF_STRING, kCreateTabGroupBlue, L"Blue");
    AppendMenuW(createGroupMenu, MF_STRING, kCreateTabGroupPurple, L"Purple");
    AppendMenuW(createGroupMenu, MF_STRING, kCreateTabGroupGray, L"Gray");
    AppendMenuW(menu, MF_POPUP | MF_STRING,
                reinterpret_cast<UINT_PTR>(createGroupMenu), L"Create group");

    std::vector<int> groupIds;
    if (!m_tabGroups.empty()) {
        for (const auto& group : m_tabGroups) {
            const UINT commandId = kMoveToTabGroupBase + static_cast<UINT>(groupIds.size());
            AppendMenuW(existingGroupsMenu, MF_STRING, commandId, group.name.c_str());
            groupIds.push_back(group.id);
        }
        AppendMenuW(menu, MF_POPUP | MF_STRING,
                    reinterpret_cast<UINT_PTR>(existingGroupsMenu), L"Move to group");
    } else {
        DestroyMenu(existingGroupsMenu);
        existingGroupsMenu = nullptr;
    }
    if (m_tabs[index].groupId != 0) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kRemoveFromTabGroup, L"Remove from group");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kHibernateTab,
                m_tabs[index].isHibernated ? L"Restore tab" : L"Hibernate tab");

    POINT screenPoint = { x, y };
    ClientToScreen(m_hWnd, &screenPoint);
    SetForegroundWindow(m_hWnd);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                        screenPoint.x, screenPoint.y, 0, m_hWnd, nullptr);
    DestroyMenu(menu);

    const COLORREF colors[] = {
        RGB(239, 68, 68), RGB(249, 115, 22), RGB(234, 179, 8),
        RGB(34, 197, 94), RGB(59, 130, 246), RGB(168, 85, 247), RGB(148, 163, 184)
    };
    const wchar_t* names[] = { L"Red", L"Orange", L"Yellow", L"Green", L"Blue", L"Purple", L"Gray" };
    if (command >= kCreateTabGroupRed && command <= kCreateTabGroupGray) {
        const size_t colorIndex = command - kCreateTabGroupRed;
        std::wstring name = names[colorIndex];
        int sameNameCount = 0;
        for (const auto& group : m_tabGroups) {
            if (group.name.compare(0, name.size(), name) == 0) ++sameNameCount;
        }
        if (sameNameCount > 0) name += L" " + std::to_wstring(sameNameCount + 1);
        const int groupId = m_nextTabGroupId++;
        m_tabGroups.push_back({ groupId, name, colors[colorIndex] });
        m_tabs[index].groupId = groupId;
        if (m_activeTabIndex != index) SelectTab(index);
        InvalidateRect(m_hWnd, nullptr, FALSE);
    } else if (command >= kMoveToTabGroupBase &&
               command < kMoveToTabGroupBase + groupIds.size()) {
        const size_t groupIndex = command - kMoveToTabGroupBase;
        m_tabs[index].groupId = groupIds[groupIndex];
        if (m_activeTabIndex != index) SelectTab(index);
        InvalidateRect(m_hWnd, nullptr, FALSE);
    } else if (command == kRemoveFromTabGroup) {
        m_tabs[index].groupId = 0;
        if (m_activeTabIndex != index) SelectTab(index);
        InvalidateRect(m_hWnd, nullptr, FALSE);
    } else if (command == kHibernateTab) {
        if (m_tabs[index].isHibernated) SelectTab(index);
        else HibernateTab(index);
    }
}

void WindowManager::HandleRButtonUp(int x, int y) {
    if (m_sidebarExpanded && m_activeSidebarPanel == SidebarButton::Bookmarks &&
        x >= MulDiv(58, m_dpi, 96) && x < GetSidebarWidth() &&
        y >= GetTotalTopHeight() + MulDiv(124, m_dpi, 96)) {
        const int row = (y - GetTotalTopHeight() - MulDiv(124, m_dpi, 96)) /
            MulDiv(42, m_dpi, 96);
        const size_t index = static_cast<size_t>(row + m_sidebarScrollOffset);
        if (row >= 0 && row < 8 && index < m_bookmarks.size()) {
            const SavedBookmark bookmark = m_bookmarks[index];
            const int result = MessageBoxW(m_hWnd,
                (L"Remove \"" + bookmark.title + L"\" from bookmarks?").c_str(),
                L"Remove bookmark", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
            if (result == IDYES) {
                std::wstring error;
                if (!m_accountStore.DeleteBookmark(m_accountSession, bookmark.url, error)) {
                    MessageBoxW(m_hWnd, error.c_str(), L"Bookmarks", MB_OK | MB_ICONERROR);
                } else {
                    RefreshSidebarData();
                    InvalidateRect(m_hWnd, nullptr, FALSE);
                }
            }
        }
        return;
    }
    RECT clientRect = {};
    GetClientRect(m_hWnd, &clientRect);
    const int tabIndex = HitTestTabs(x, y, clientRect.right);
    if (tabIndex >= 0) ShowTabContextMenu(static_cast<size_t>(tabIndex), x, y);
}

// ---------------------------------------------------------------------------
// Chrome-Style Double-Buffered Painting
// ---------------------------------------------------------------------------

void WindowManager::DrawTabs(HDC memDC, int width) {
    DrawHorizontalTabs(memDC, width);
    return;

#if 0
    int stripH = GetTabStripHeight();
    const ThemePalette palette = GetThemePalette();

    RECT stripR = { 0, 0, width, stripH };
    HBRUSH bgBrush = CreateSolidBrush(palette.tabStrip);
    FillRect(memDC, &stripR, bgBrush);
    DeleteObject(bgBrush);

    RECT clientRect = {};
    GetClientRect(m_hWnd, &clientRect);
    const int sidebarWidth = GetVerticalTabsWidth();
    RECT sidebarRect = { 0, GetTotalTopHeight(), sidebarWidth, clientRect.bottom };
    HBRUSH sidebarBrush = CreateSolidBrush(palette.navSurface);
    FillRect(memDC, &sidebarRect, sidebarBrush);
    DeleteObject(sidebarBrush);
    RECT sidebarEdge = { sidebarWidth - 1, GetTotalTopHeight(), sidebarWidth, clientRect.bottom };
    HBRUSH edgeBrush = CreateSolidBrush(palette.navBorder);
    FillRect(memDC, &sidebarEdge, edgeBrush);
    DeleteObject(edgeBrush);

    HFONT sectionFont = CreateFontW(
        -MulDiv(10, m_dpi, 96), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT previousFont = static_cast<HFONT>(SelectObject(memDC, sectionFont));
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, palette.mutedText);
    RECT sectionLabel = { MulDiv(14, m_dpi, 96), GetTotalTopHeight() + MulDiv(4, m_dpi, 96),
                          sidebarWidth - MulDiv(14, m_dpi, 96),
                          GetTotalTopHeight() + MulDiv(21, m_dpi, 96) };
    DrawTextW(memDC, L"YOUR TABS", -1, &sectionLabel, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

    // 2. Draw each tab
    HFONT tabFont = CreateFontW(
        -MulDiv(12, m_dpi, 96), 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    SelectObject(memDC, tabFont);

    RECT firstTabRect = GetTabRect(0, width);
    RECT lastTabRect = GetTabRect(m_tabs.empty() ? 0 : m_tabs.size() - 1, width);
    const int visibleTop = GetTotalTopHeight() + MulDiv(62, m_dpi, 96);
    const int visibleBottom = std::max(visibleTop,
        static_cast<int>(clientRect.bottom) - MulDiv(52, m_dpi, 96));
    (void)firstTabRect;
    (void)lastTabRect;
    const int clipState = SaveDC(memDC);
    IntersectClipRect(memDC, 0, visibleTop, sidebarWidth, visibleBottom);

    for (size_t i = 0; i < m_tabs.size(); ++i) {
        const auto& tab = m_tabs[i];
        RECT tabR = GetTabRect(i, width);
        bool isActive = (i == m_activeTabIndex);
        bool isHovered = (static_cast<int>(i) == m_hoveredTabIndex);

        int cornerR = MulDiv(m_browserMode == kBrowserModeWellness ? 11 : 8, m_dpi, 96);

        if (isActive) {
            HBRUSH activeBr = CreateSolidBrush(palette.activeTab);
            HPEN nullPen = CreatePen(PS_NULL, 0, RGB(0, 0, 0));
            HGDIOBJ ob = SelectObject(memDC, activeBr);
            HGDIOBJ op = SelectObject(memDC, nullPen);

            RoundRect(memDC, tabR.left, tabR.top, tabR.right, tabR.bottom + cornerR, cornerR * 2, cornerR * 2);

            SelectObject(memDC, ob);
            SelectObject(memDC, op);
            DeleteObject(activeBr);
            DeleteObject(nullPen);
        } else if (isHovered) {
            HBRUSH hoverBr = CreateSolidBrush(palette.inactiveHover);
            HPEN nullPen = CreatePen(PS_NULL, 0, RGB(0, 0, 0));
            HGDIOBJ ob = SelectObject(memDC, hoverBr);
            HGDIOBJ op = SelectObject(memDC, nullPen);

            RoundRect(memDC, tabR.left + 2, tabR.top + 2, tabR.right - 2, tabR.bottom, cornerR * 2, cornerR * 2);

            SelectObject(memDC, ob);
            SelectObject(memDC, op);
            DeleteObject(hoverBr);
            DeleteObject(nullPen);
        } else {
            // Inactive subtle separator line on the right
            HPEN sepPen = CreatePen(PS_SOLID, 1, palette.navBorder);
            HGDIOBJ op = SelectObject(memDC, sepPen);
            int sepY1 = tabR.top + MulDiv(7, m_dpi, 96);
            int sepY2 = tabR.bottom - MulDiv(7, m_dpi, 96);
            MoveToEx(memDC, tabR.right - 1, sepY1, nullptr);
            LineTo(memDC, tabR.right - 1, sepY2);
            SelectObject(memDC, op);
            DeleteObject(sepPen);
        }

        void WindowManager::DrawHorizontalTabs(HDC memDC, int width) {
            const ThemePalette palette = GetThemePalette();
            RECT strip = { 0, 0, width, GetTabStripHeight() };
            HBRUSH stripBrush = CreateSolidBrush(palette.tabStrip);
            FillRect(memDC, &strip, stripBrush);
            DeleteObject(stripBrush);
            RECT stripEdge = { 0, GetTabStripHeight() - MulDiv(1, m_dpi, 96), width, GetTabStripHeight() };
            HBRUSH edgeBrush = CreateSolidBrush(palette.navBorder);
            FillRect(memDC, &stripEdge, edgeBrush);
            DeleteObject(edgeBrush);
            SetBkMode(memDC, TRANSPARENT);

            const int visibleCount = GetVisibleTabCount(width);
            const size_t first = static_cast<size_t>(std::max(0, m_tabScrollOffset));
            const size_t end = std::min(m_tabs.size(), first + static_cast<size_t>(visibleCount));
            const int clip = SaveDC(memDC);
            IntersectClipRect(memDC, 0, 0, width - MulDiv(m_baseCaptionBtnWidth * 3, m_dpi, 96),
                              GetTabStripHeight());

            HFONT tabFont = CreateFontW(-MulDiv(12, m_dpi, 96), 0, 0, 0, FW_MEDIUM,
                FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            HFONT oldFont = static_cast<HFONT>(SelectObject(memDC, tabFont));
            for (size_t index = first; index < end; ++index) {
                const BrowserTab& tab = m_tabs[index];
                const RECT rect = GetTabRect(index, width);
                const bool active = index == m_activeTabIndex;
                const bool hovered = static_cast<int>(index) == m_hoveredTabIndex;
                if (active || hovered) {
                    HBRUSH background = CreateSolidBrush(active ? palette.activeTab : palette.inactiveHover);
                    HPEN border = CreatePen(PS_SOLID, 1, active ? palette.cardBorder : palette.navBorder);
                    HGDIOBJ oldBrush = SelectObject(memDC, background);
                    HGDIOBJ oldPen = SelectObject(memDC, border);
                    RoundRect(memDC, rect.left, rect.top, rect.right, rect.bottom,
                              MulDiv(12, m_dpi, 96), MulDiv(12, m_dpi, 96));
                    SelectObject(memDC, oldBrush);
                    SelectObject(memDC, oldPen);
                    DeleteObject(background);
                    DeleteObject(border);
                }

                if (active) {
                    RECT activeMark = { rect.left + MulDiv(16, m_dpi, 96),
                        rect.bottom - MulDiv(3, m_dpi, 96),
                        rect.right - MulDiv(16, m_dpi, 96), rect.bottom };
                    HBRUSH markBrush = CreateSolidBrush(palette.accent);
                    FillRect(memDC, &activeMark, markBrush);
                    DeleteObject(markBrush);
                }

                if (tab.groupId != 0) {
                    const auto group = std::find_if(m_tabGroups.begin(), m_tabGroups.end(),
                        [&tab](const TabGroup& candidate) { return candidate.id == tab.groupId; });
                    if (group != m_tabGroups.end()) {
                        RECT groupLine = { rect.left + MulDiv(8, m_dpi, 96), rect.top,
                            rect.right - MulDiv(8, m_dpi, 96), rect.top + MulDiv(3, m_dpi, 96) };
                        HBRUSH groupBrush = CreateSolidBrush(group->color);
                        FillRect(memDC, &groupLine, groupBrush);
                        DeleteObject(groupBrush);
                    }
                }

                const int iconSize = MulDiv(18, m_dpi, 96);
                const int iconLeft = rect.left + MulDiv(9, m_dpi, 96);
                const int iconTop = rect.top + (rect.bottom - rect.top - iconSize) / 2;
                const auto favicon = m_favicons.find(tab.url);
                if (favicon != m_favicons.end() && favicon->second) {
                    HDC iconDc = CreateCompatibleDC(memDC);
                    HBITMAP previous = static_cast<HBITMAP>(SelectObject(iconDc, favicon->second));
                    BITMAP bitmap = {};
                    GetObjectW(favicon->second, sizeof(bitmap), &bitmap);
                    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
                    AlphaBlend(memDC, iconLeft, iconTop, iconSize, iconSize,
                        iconDc, 0, 0, bitmap.bmWidth, bitmap.bmHeight, blend);
                    SelectObject(iconDc, previous);
                    DeleteDC(iconDc);
                } else {
                    HBRUSH iconBrush = CreateSolidBrush(tab.badgeColor);
                    RECT iconRect = { iconLeft, iconTop, iconLeft + iconSize, iconTop + iconSize };
                    HBRUSH previous = static_cast<HBRUSH>(SelectObject(memDC, iconBrush));
                    RoundRect(memDC, iconRect.left, iconRect.top, iconRect.right, iconRect.bottom,
                        MulDiv(5, m_dpi, 96), MulDiv(5, m_dpi, 96));
                    SelectObject(memDC, previous);
                    DeleteObject(iconBrush);
                    SetTextColor(memDC, RGB(255, 255, 255));
                    DrawTextW(memDC, TabLogoText(tab).c_str(), -1, &iconRect,
                        DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                }

                const RECT close = GetTabCloseRect(rect);
                RECT label = { iconLeft + iconSize + MulDiv(8, m_dpi, 96), rect.top,
                    close.left - MulDiv(4, m_dpi, 96), rect.bottom };
                SetTextColor(memDC, active ? palette.primaryText : palette.secondaryText);
                DrawTextW(memDC, tab.title.c_str(), -1, &label,
                    DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

                if (static_cast<int>(index) == m_hoveredTabCloseIndex) {
                    HBRUSH closeBrush = CreateSolidBrush(palette.controlHover);
                    RECT closeBackground = { close.left, close.top, close.right, close.bottom };
                    FillRect(memDC, &closeBackground, closeBrush);
                    DeleteObject(closeBrush);
                }
                SetTextColor(memDC, palette.secondaryText);
                RECT closeGlyph = { close.left, close.top - MulDiv(1, m_dpi, 96), close.right, close.bottom };
                DrawTextW(memDC, L"\x00D7", -1, &closeGlyph, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            }
            SelectObject(memDC, oldFont);
            DeleteObject(tabFont);
            RestoreDC(memDC, clip);

            const RECT newTab = GetNewTabButtonRect(width);
            if (m_hoveredNewTabBtn) {
                HBRUSH buttonBrush = CreateSolidBrush(palette.controlHover);
                HGDIOBJ oldBrush = SelectObject(memDC, buttonBrush);
                HGDIOBJ oldButtonPen = SelectObject(memDC, GetStockObject(NULL_PEN));
                RoundRect(memDC, newTab.left, newTab.top, newTab.right, newTab.bottom,
                    MulDiv(12, m_dpi, 96), MulDiv(12, m_dpi, 96));
                SelectObject(memDC, oldBrush);
                SelectObject(memDC, oldButtonPen);
                DeleteObject(buttonBrush);
            }
            HPEN plusPen = CreatePen(PS_SOLID, 1, palette.secondaryText);
            HGDIOBJ previousPen = SelectObject(memDC, plusPen);
            const int centerX = newTab.left + (newTab.right - newTab.left) / 2;
            const int centerY = newTab.top + (newTab.bottom - newTab.top) / 2;
            const int radius = MulDiv(4, m_dpi, 96);
            MoveToEx(memDC, centerX - radius, centerY, nullptr);
            LineTo(memDC, centerX + radius + 1, centerY);
            MoveToEx(memDC, centerX, centerY - radius, nullptr);
            LineTo(memDC, centerX, centerY + radius + 1);
            SelectObject(memDC, previousPen);
            DeleteObject(plusPen);

            for (CaptionButton button : { CaptionButton::Minimize, CaptionButton::Maximize, CaptionButton::Close }) {
                const RECT rect = GetCaptionButtonRect(button);
                const bool hovered = button == m_hoveredCaptionBtn;
                if (hovered) {
                    HBRUSH hoverBrush = CreateSolidBrush(button == CaptionButton::Close
                        ? RGB(193, 69, 89) : palette.controlHover);
                    FillRect(memDC, &rect, hoverBrush);
                    DeleteObject(hoverBrush);
                }
                const COLORREF color = button == CaptionButton::Close && hovered
                    ? RGB(255, 255, 255) : palette.secondaryText;
                HPEN pen = CreatePen(PS_SOLID, 1, color);
                HGDIOBJ oldPen = SelectObject(memDC, pen);
                const int x = rect.left + (rect.right - rect.left) / 2;
                const int y = rect.top + (rect.bottom - rect.top) / 2;
                const int size = MulDiv(5, m_dpi, 96);
                if (button == CaptionButton::Minimize) {
                    MoveToEx(memDC, x - size, y, nullptr);
                    LineTo(memDC, x + size + 1, y);
                } else if (button == CaptionButton::Maximize) {
                    Rectangle(memDC, x - size, y - size, x + size + 1, y + size + 1);
                } else {
                    MoveToEx(memDC, x - size, y - size, nullptr);
                    LineTo(memDC, x + size + 1, y + size + 1);
                    MoveToEx(memDC, x + size, y - size, nullptr);
                    LineTo(memDC, x - size - 1, y + size + 1);
                }
                SelectObject(memDC, oldPen);
                DeleteObject(pen);
            }
        }

        // Tab Icon Dot
        if (tab.groupId != 0) {
            const auto group = std::find_if(m_tabGroups.begin(), m_tabGroups.end(),
                [&tab](const TabGroup& candidate) { return candidate.id == tab.groupId; });
            if (group != m_tabGroups.end()) {
                RECT groupAccent = {
                    tabR.left,
                    tabR.top + MulDiv(5, m_dpi, 96),
                    tabR.left + MulDiv(3, m_dpi, 96),
                    tabR.bottom - MulDiv(5, m_dpi, 96)
                };
                HBRUSH groupBrush = CreateSolidBrush(group->color);
                FillRect(memDC, &groupAccent, groupBrush);
                DeleteObject(groupBrush);
            }
        }

        // Tab Icon Dot
        int dotSize = MulDiv(8, m_dpi, 96);
        int dotX = tabR.left + MulDiv(10, m_dpi, 96);
        int dotY = tabR.top + MulDiv(10, m_dpi, 96);

        HBRUSH dotBr = CreateSolidBrush(tab.badgeColor);
        HPEN dotPen = CreatePen(PS_NULL, 0, RGB(0, 0, 0));
        HGDIOBJ ob = SelectObject(memDC, dotBr);
        HGDIOBJ op = SelectObject(memDC, dotPen);
        Ellipse(memDC, dotX, dotY, dotX + dotSize, dotY + dotSize);
        SelectObject(memDC, ob);
        SelectObject(memDC, op);
        DeleteObject(dotBr);
        DeleteObject(dotPen);

        // Tab Title Text
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, isActive ? palette.primaryText : palette.secondaryText);

        RECT closeR = GetTabCloseRect(tabR);
        RECT textR = {
            dotX + dotSize + MulDiv(8, m_dpi, 96),
            tabR.top + MulDiv(2, m_dpi, 96),
            closeR.left - MulDiv(4, m_dpi, 96),
            tabR.top + MulDiv(22, m_dpi, 96)
        };
        DrawTextW(memDC, tab.title.c_str(), -1, &textR, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

        const unsigned long long memoryTenths =
            static_cast<unsigned long long>(tab.memoryEstimateBytes) * 10 / (1024 * 1024);
        const std::wstring memoryLabel =
            std::to_wstring(memoryTenths / 10) + L"." + std::to_wstring(memoryTenths % 10);
        const std::wstring statusText = tab.isHibernated ? L"HIBERNATED  \xB7  click to restore" :
            (tab.memoryEstimateBytes > 0
                ? L"JS heap \x2248 " + memoryLabel + L" MB"
                : L"ACTIVE  \xB7  JS heap estimate");
        RECT statusRect = { tabR.left + MulDiv(13, m_dpi, 96),
            tabR.top + MulDiv(23, m_dpi, 96), closeR.left - MulDiv(3, m_dpi, 96),
            tabR.bottom - MulDiv(2, m_dpi, 96) };
        SetTextColor(memDC, tab.isHibernated ? palette.mutedText : palette.secondaryText);
        DrawTextW(memDC, statusText.c_str(), -1, &statusRect,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

        // Tab Close Button '✕'
        bool isCloseHovered = (static_cast<int>(i) == m_hoveredTabCloseIndex);
        if (isCloseHovered) {
            HBRUSH clBr = CreateSolidBrush(palette.controlHover);
            HPEN np = CreatePen(PS_NULL, 0, RGB(0, 0, 0));
            HGDIOBJ ob2 = SelectObject(memDC, clBr);
            HGDIOBJ op2 = SelectObject(memDC, np);
            RoundRect(memDC, closeR.left, closeR.top, closeR.right, closeR.bottom, 6, 6);
            SelectObject(memDC, ob2);
            SelectObject(memDC, op2);
            DeleteObject(clBr);
            DeleteObject(np);
        }

        HPEN crossPen = CreatePen(PS_SOLID, 1, isCloseHovered ? palette.primaryText : palette.mutedText);
        HGDIOBJ op3 = SelectObject(memDC, crossPen);
        int ccX = closeR.left + (closeR.right - closeR.left) / 2;
        int ccY = closeR.top + (closeR.bottom - closeR.top) / 2;
        int cr = MulDiv(3, m_dpi, 96);
        MoveToEx(memDC, ccX - cr, ccY - cr, nullptr);
        LineTo(memDC, ccX + cr + 1, ccY + cr + 1);
        MoveToEx(memDC, ccX + cr, ccY - cr, nullptr);
        LineTo(memDC, ccX - cr - 1, ccY + cr + 1);
        SelectObject(memDC, op3);
        DeleteObject(crossPen);
    }

    RestoreDC(memDC, clipState);
    SelectObject(memDC, sectionFont);
    DeleteObject(tabFont);

    // 3. New Tab '+' Button
    RECT newTabR = GetNewTabButtonRect(width);
    HBRUSH newTabBrush = CreateSolidBrush(m_hoveredNewTabBtn ? palette.controlHover : palette.card);
    HPEN newTabPen = CreatePen(PS_SOLID, 1, palette.cardBorder);
    HGDIOBJ newTabOldBrush = SelectObject(memDC, newTabBrush);
    HGDIOBJ newTabOldPen = SelectObject(memDC, newTabPen);
    RoundRect(memDC, newTabR.left, newTabR.top, newTabR.right, newTabR.bottom,
              MulDiv(10, m_dpi, 96), MulDiv(10, m_dpi, 96));
    SelectObject(memDC, newTabOldBrush);
    SelectObject(memDC, newTabOldPen);
    DeleteObject(newTabBrush);
    DeleteObject(newTabPen);

    HPEN plusPen = CreatePen(PS_SOLID, 1, palette.accent);
    HGDIOBJ opPlus = SelectObject(memDC, plusPen);
    int pX = newTabR.left + (newTabR.right - newTabR.left) / 2;
    int pY = newTabR.top + (newTabR.bottom - newTabR.top) / 2;
    int pr = MulDiv(4, m_dpi, 96);
    MoveToEx(memDC, pX - pr, pY, nullptr);
    LineTo(memDC, pX + pr + 1, pY);
    MoveToEx(memDC, pX, pY - pr, nullptr);
    LineTo(memDC, pX, pY + pr + 1);
    SelectObject(memDC, opPlus);
    DeleteObject(plusPen);
    SetTextColor(memDC, palette.primaryText);
    RECT newTabLabel = { newTabR.left + MulDiv(32, m_dpi, 96), newTabR.top,
                         newTabR.right - MulDiv(8, m_dpi, 96), newTabR.bottom };
    DrawTextW(memDC, L"New tab", -1, &newTabLabel,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT);

    if (m_hoveredTabIndex >= 0 &&
        static_cast<size_t>(m_hoveredTabIndex) < m_tabs.size()) {
        const BrowserTab& previewTab = m_tabs[static_cast<size_t>(m_hoveredTabIndex)];
        const RECT hoveredRect = GetTabRect(static_cast<size_t>(m_hoveredTabIndex), width);
        const int previewLeft = sidebarWidth + MulDiv(12, m_dpi, 96);
        const int previewTop = std::clamp(static_cast<int>(hoveredRect.top),
            GetTotalTopHeight() + MulDiv(8, m_dpi, 96),
            std::max(GetTotalTopHeight() + MulDiv(8, m_dpi, 96),
                     static_cast<int>(clientRect.bottom) - MulDiv(116, m_dpi, 96)));
        RECT previewRect = { previewLeft, previewTop,
            previewLeft + MulDiv(260, m_dpi, 96), previewTop + MulDiv(104, m_dpi, 96) };
        HBRUSH previewBrush = CreateSolidBrush(palette.card);
        HPEN previewPen = CreatePen(PS_SOLID, 1, palette.cardBorder);
        HGDIOBJ oldPreviewBrush = SelectObject(memDC, previewBrush);
        HGDIOBJ oldPreviewPen = SelectObject(memDC, previewPen);
        RoundRect(memDC, previewRect.left, previewRect.top, previewRect.right,
                  previewRect.bottom, MulDiv(12, m_dpi, 96), MulDiv(12, m_dpi, 96));
        SelectObject(memDC, oldPreviewBrush);
        SelectObject(memDC, oldPreviewPen);
        DeleteObject(previewBrush);
        DeleteObject(previewPen);
        SetTextColor(memDC, palette.accent);
        RECT previewHeading = { previewRect.left + MulDiv(12, m_dpi, 96),
            previewRect.top + MulDiv(8, m_dpi, 96),
            previewRect.right - MulDiv(8, m_dpi, 96),
            previewRect.top + MulDiv(24, m_dpi, 96) };
        DrawTextW(memDC, L"TAB PREVIEW", -1, &previewHeading,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        SetTextColor(memDC, palette.primaryText);
        RECT previewTitle = { previewRect.left + MulDiv(12, m_dpi, 96),
            previewRect.top + MulDiv(27, m_dpi, 96),
            previewRect.right - MulDiv(12, m_dpi, 96),
            previewRect.top + MulDiv(51, m_dpi, 96) };
        DrawTextW(memDC, previewTab.title.c_str(), -1, &previewTitle,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        SetTextColor(memDC, palette.secondaryText);
        RECT previewUrl = { previewRect.left + MulDiv(12, m_dpi, 96),
            previewRect.top + MulDiv(52, m_dpi, 96),
            previewRect.right - MulDiv(12, m_dpi, 96),
            previewRect.top + MulDiv(76, m_dpi, 96) };
        const std::wstring previewTarget = previewTab.isSettings ? L"Browser settings" :
            previewTab.isHibernated ? L"Paused \xB7  " + previewTab.url : previewTab.url;
        DrawTextW(memDC, previewTarget.c_str(), -1, &previewUrl,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        RECT previewMemoryRect = { previewRect.left + MulDiv(12, m_dpi, 96),
            previewRect.top + MulDiv(78, m_dpi, 96),
            previewRect.right - MulDiv(12, m_dpi, 96), previewRect.bottom };
        const unsigned long long previewMemoryTenths =
            static_cast<unsigned long long>(previewTab.memoryEstimateBytes) * 10 / (1024 * 1024);
        const std::wstring previewMemoryValue =
            std::to_wstring(previewMemoryTenths / 10) + L"." +
            std::to_wstring(previewMemoryTenths % 10);
        const std::wstring memoryText = previewTab.isHibernated ? L"Not currently using page JS memory" :
            previewTab.memoryEstimateBytes > 0
                ? L"JS heap estimate: " + previewMemoryValue + L" MB"
                : L"JS heap estimate: waiting for page data";
        DrawTextW(memDC, memoryText.c_str(), -1, &previewMemoryRect,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    }

    SelectObject(memDC, previousFont);
    DeleteObject(sectionFont);

    // 4. Custom Window Control Buttons (Minimize, Maximize, Close)
    CaptionButton btns[] = { CaptionButton::Minimize, CaptionButton::Maximize, CaptionButton::Close };
    for (CaptionButton btn : btns) {
        RECT r = GetCaptionButtonRect(btn);
        bool isHovered = (m_hoveredCaptionBtn == btn);
        bool isPressed = (m_pressedCaptionBtn == btn);

        if (btn == CaptionButton::Close) {
            if (isPressed) {
                HBRUSH b = CreateSolidBrush(RGB(196, 16, 31));
                FillRect(memDC, &r, b);
                DeleteObject(b);
            } else if (isHovered) {
                HBRUSH b = CreateSolidBrush(RGB(232, 17, 35));
                FillRect(memDC, &r, b);
                DeleteObject(b);
            }
        } else {
            if (isPressed) {
                HBRUSH b = CreateSolidBrush(palette.controlPressed);
                FillRect(memDC, &r, b);
                DeleteObject(b);
            } else if (isHovered) {
                HBRUSH b = CreateSolidBrush(palette.controlHover);
                FillRect(memDC, &r, b);
                DeleteObject(b);
            }
        }

        COLORREF iconColor = (btn == CaptionButton::Close && (isHovered || isPressed)) ?
                             RGB(255, 255, 255) : palette.secondaryText;
        HPEN pen = CreatePen(PS_SOLID, 1, iconColor);
        HGDIOBJ oldPen = SelectObject(memDC, pen);

        int cX = r.left + (r.right - r.left) / 2;
        int cY = r.top + (r.bottom - r.top) / 2;
        int rad = MulDiv(5, m_dpi, 96);

        if (btn == CaptionButton::Minimize) {
            MoveToEx(memDC, cX - rad, cY, nullptr);
            LineTo(memDC, cX + rad + 1, cY);
        } else if (btn == CaptionButton::Maximize) {
            if (IsZoomed(m_hWnd)) {
                int off = MulDiv(2, m_dpi, 96);
                RECT back = { cX - rad + off, cY - rad - off, cX + rad + 1, cY + rad - off + 1 };
                MoveToEx(memDC, back.left, back.top, nullptr);
                LineTo(memDC, back.right, back.top);
                LineTo(memDC, back.right, back.bottom);
                LineTo(memDC, back.left + off * 2, back.bottom);

                RECT front = { cX - rad, cY - rad + off, cX + rad - off + 1, cY + rad + 1 };
                HGDIOBJ oldB = SelectObject(memDC, GetStockObject(NULL_BRUSH));
                Rectangle(memDC, front.left, front.top, front.right, front.bottom);
                SelectObject(memDC, oldB);
            } else {
                HGDIOBJ oldB = SelectObject(memDC, GetStockObject(NULL_BRUSH));
                Rectangle(memDC, cX - rad, cY - rad, cX + rad + 1, cY + rad + 1);
                SelectObject(memDC, oldB);
            }
        } else if (btn == CaptionButton::Close) {
            MoveToEx(memDC, cX - rad, cY - rad, nullptr);
            LineTo(memDC, cX + rad + 1, cY + rad + 1);
            MoveToEx(memDC, cX + rad, cY - rad, nullptr);
            LineTo(memDC, cX - rad - 1, cY + rad + 1);
        }

        SelectObject(memDC, oldPen);
        DeleteObject(pen);
    }
#endif
}

void WindowManager::DrawSidebarPanel(HDC hdc, const RECT& panel,
                                     const ThemePalette& palette) {
    const int scale = static_cast<int>(m_dpi);
    const auto px = [scale](int value) { return MulDiv(value, scale, 96); };
    const int left = panel.left + px(14);
    const int right = panel.right - px(14);
    const int top = panel.top;
    HBRUSH panelBrush = CreateSolidBrush(palette.content);
    FillRect(hdc, &panel, panelBrush);
    DeleteObject(panelBrush);
    HFONT font = CreateFontW(-px(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT headingFont = CreateFontW(-px(17), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);

    const auto text = [hdc, px, right, palette](int x, int y, const std::wstring& value,
                                                COLORREF color, UINT flags = DT_LEFT) {
        SetTextColor(hdc, color);
        RECT rect = { x, y, right, y + px(24) };
        DrawTextW(hdc, value.c_str(), static_cast<int>(value.size()), &rect,
                  flags | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    };
    const auto button = [hdc, px, left, right, palette, text](int y, const std::wstring& label,
                                                             bool selected = false) {
        RECT rect = { left, y, right, y + px(38) };
        HBRUSH brush = CreateSolidBrush(selected ? palette.controlPressed : palette.card);
        HPEN pen = CreatePen(PS_SOLID, 1, selected ? palette.accent : palette.cardBorder);
        HGDIOBJ oldBrush = SelectObject(hdc, brush);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, px(10), px(10));
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);
        text(left + px(10), y + px(6), label, palette.primaryText);
    };
    const auto heading = [hdc, left, top, palette, px, &text, headingFont, font](const wchar_t* title,
                                                                                const wchar_t* detail) {
        SelectObject(hdc, headingFont);
        SetTextColor(hdc, palette.primaryText);
        RECT titleRect = { left, top + px(20), left + px(265), top + px(46) };
        DrawTextW(hdc, title, -1, &titleRect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(hdc, font);
        text(left, top + px(48), detail, palette.mutedText);
    };

    switch (m_activeSidebarPanel) {
        case SidebarButton::Controls: {
            heading(L"Efficiency controls", L"Best-effort reductions, not hard quotas.");
            const wchar_t* modes[] = { L"Standard", L"Balanced", L"Efficiency" };
            const int cardWidth = px(82);
            const int gap = px(7);
            for (int index = 0; index < 3; ++index) {
                RECT rect = { left + index * (cardWidth + gap), top + px(90),
                             left + index * (cardWidth + gap) + cardWidth, top + px(150) };
                HBRUSH brush = CreateSolidBrush(m_resourceMode == index
                    ? palette.controlPressed : palette.card);
                HPEN pen = CreatePen(PS_SOLID, 1, m_resourceMode == index
                    ? palette.accent : palette.cardBorder);
                HGDIOBJ oldBrush = SelectObject(hdc, brush);
                HGDIOBJ oldPen = SelectObject(hdc, pen);
                RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, px(10), px(10));
                SelectObject(hdc, oldBrush);
                SelectObject(hdc, oldPen);
                DeleteObject(brush);
                DeleteObject(pen);
                SetTextColor(hdc, palette.primaryText);
                DrawTextW(hdc, modes[index], -1, &rect,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
            text(left, top + px(168), L"Inactive tabs are unloaded to reduce their memory use.",
                 palette.secondaryText, DT_WORDBREAK);
            text(left, top + px(208), L"Balanced/Efficiency lower this app's Windows scheduling priority.",
                 palette.secondaryText, DT_WORDBREAK);
            text(left, top + px(260), L"WebView2 subprocesses are separate; exact CPU/RAM caps aren't available.",
                 palette.mutedText, DT_WORDBREAK);
            break;
        }
        case SidebarButton::Cleaner: {
            heading(L"Cleaner", L"Choose items. Downloaded files are never deleted.");
            const wchar_t* labels[] = {
                L"Cache", L"Cookies", L"Browsing & search history",
                L"Download history (records only)", L"Other open tabs",
                L"Bookmarks & quick-access favorites"
            };
            for (int index = 0; index < 6; ++index) {
                const int y = top + px(76 + index * 42);
                RECT card = { left, y, right, y + px(34) };
                HBRUSH brush = CreateSolidBrush(palette.card);
                HPEN pen = CreatePen(PS_SOLID, 1, palette.cardBorder);
                HGDIOBJ oldBrush = SelectObject(hdc, brush);
                HGDIOBJ oldPen = SelectObject(hdc, pen);
                RoundRect(hdc, card.left, card.top, card.right, card.bottom, px(8), px(8));
                SelectObject(hdc, oldBrush);
                SelectObject(hdc, oldPen);
                DeleteObject(brush);
                DeleteObject(pen);
                RECT box = { left + px(9), y + px(8), left + px(25), y + px(24) };
                HGDIOBJ oldBoxBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Rectangle(hdc, box.left, box.top, box.right, box.bottom);
                SelectObject(hdc, oldBoxBrush);
                if (m_cleanerSelections[index]) {
                    MoveToEx(hdc, box.left + px(3), box.top + px(8), nullptr);
                    LineTo(hdc, box.left + px(7), box.top + px(12));
                    LineTo(hdc, box.left + px(14), box.top + px(3));
                }
                text(left + px(32), y + px(5), labels[index], palette.primaryText);
            }
            button(top + px(348), L"Start cleaning");
            break;
        }
        case SidebarButton::Player:
            heading(L"Player", L"Open a music service in a browser tab.");
            button(top + px(84), L"Spotify");
            button(top + px(134), L"YouTube Music");
            button(top + px(184), L"SoundCloud");
            text(left, top + px(238), L"Favorites are saved in Bookmarks.", palette.mutedText);
            button(top + px(256), L"Open favorites");
            break;
        case SidebarButton::Shortcuts:
            heading(L"Shortcuts", L"Quick links to frequently used sites.");
            for (size_t index = 0; index < m_shortcuts.size(); ++index) {
                button(top + px(78 + static_cast<int>(index) * 42),
                       m_shortcuts[index].name);
            }
            break;
        case SidebarButton::Bookmarks:
            heading(L"Bookmarks", L"Saved locally in your browser profile.");
            button(top + px(70), L"Save current page");
            if (m_bookmarks.empty()) {
                text(left, top + px(126), L"No bookmarks yet.", palette.mutedText);
            }
            for (size_t row = 0; row < 8 &&
                 row + static_cast<size_t>(m_sidebarScrollOffset) < m_bookmarks.size(); ++row) {
                const size_t index = row + static_cast<size_t>(m_sidebarScrollOffset);
                button(top + px(124 + static_cast<int>(row) * 42), m_bookmarks[index].title);
            }
            text(left, top + px(452), L"Right-click a bookmark to remove it.",
                 palette.mutedText);
            break;
        case SidebarButton::History: {
            heading(L"History & settings", L"Recent searches saved on this device.");
            if (m_searchHistory.empty()) {
                text(left, top + px(78), L"No saved searches.", palette.mutedText);
            }
            for (size_t row = 0; row < 8 &&
                 row + static_cast<size_t>(m_sidebarScrollOffset) < m_searchHistory.size(); ++row) {
                const size_t index = row + static_cast<size_t>(m_sidebarScrollOffset);
                text(left, top + px(76 + static_cast<int>(row) * 34),
                     m_searchHistory[index], palette.primaryText);
            }
            button(top + px(374), L"Open settings");
            button(top + px(430), L"Clear search history");
            break;
        }
        default:
            break;
    }

    SelectObject(hdc, oldFont);
    DeleteObject(font);
    DeleteObject(headingFont);
}

void WindowManager::DrawSidebar(HDC hdc, int height) {
    const int width = GetSidebarWidth();
    if (width <= 0) return;

    const ThemePalette palette = GetThemePalette();
    RECT rail = { 0, GetTotalTopHeight(), width, height };
    HBRUSH background = CreateSolidBrush(palette.navSurface);
    FillRect(hdc, &rail, background);
    DeleteObject(background);

    RECT edge = { width - 1, rail.top, width, rail.bottom };
    HBRUSH border = CreateSolidBrush(palette.navBorder);
    FillRect(hdc, &edge, border);
    DeleteObject(border);

    HFONT font = CreateFontW(-MulDiv(12, m_dpi, 96), 0, 0, 0, FW_MEDIUM,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);

    const SidebarButton buttons[] = {
        SidebarButton::Home, SidebarButton::OpenFile, SidebarButton::Search,
        SidebarButton::Controls, SidebarButton::Cleaner, SidebarButton::Player,
        SidebarButton::Shortcuts, SidebarButton::Bookmarks, SidebarButton::History,
        SidebarButton::Settings, SidebarButton::ToggleSize
    };
    for (size_t index = 0; index < std::size(buttons); ++index) {
        const RECT button = GetSidebarButtonRect(buttons[index]);
        const bool hovered = buttons[index] == m_hoveredSidebarButton;
        if (hovered) {
            HBRUSH hover = CreateSolidBrush(palette.controlHover);
            HPEN noPen = CreatePen(PS_NULL, 0, RGB(0, 0, 0));
            HGDIOBJ oldBrush = SelectObject(hdc, hover);
            HGDIOBJ oldPen = SelectObject(hdc, noPen);
            RoundRect(hdc, button.left, button.top, button.right, button.bottom,
                MulDiv(10, m_dpi, 96), MulDiv(10, m_dpi, 96));
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(hover);
            DeleteObject(noPen);
        }

        const int centerY = button.top + (button.bottom - button.top) / 2;
        RECT icon = { button.left + MulDiv(10, m_dpi, 96),
                      centerY - MulDiv(10, m_dpi, 96),
                      button.left + MulDiv(30, m_dpi, 96),
                      centerY + MulDiv(10, m_dpi, 96) };
        if (!m_sidebarExpanded) {
            const int iconWidth = icon.right - icon.left;
            icon.left = button.left + (button.right - button.left - iconWidth) / 2;
            icon.right = icon.left + iconWidth;
        }
        const COLORREF iconColor = hovered ? palette.primaryText : palette.secondaryText;
        HPEN iconPen = CreatePen(PS_SOLID, std::max(1, MulDiv(1, m_dpi, 96)), iconColor);
        HGDIOBJ oldPen = SelectObject(hdc, iconPen);
        const int centerX = icon.left + (icon.right - icon.left) / 2;
        const int unit = MulDiv(1, m_dpi, 96);
        switch (buttons[index]) {
            case SidebarButton::Home:
                MoveToEx(hdc, centerX - 7 * unit, centerY, nullptr);
                LineTo(hdc, centerX, centerY - 7 * unit);
                LineTo(hdc, centerX + 7 * unit, centerY);
                MoveToEx(hdc, centerX - 5 * unit, centerY - unit, nullptr);
                LineTo(hdc, centerX - 5 * unit, centerY + 6 * unit);
                LineTo(hdc, centerX + 5 * unit, centerY + 6 * unit);
                LineTo(hdc, centerX + 5 * unit, centerY - unit);
                MoveToEx(hdc, centerX - unit, centerY + 6 * unit, nullptr);
                LineTo(hdc, centerX - unit, centerY + 2 * unit);
                LineTo(hdc, centerX + 2 * unit, centerY + 2 * unit);
                LineTo(hdc, centerX + 2 * unit, centerY + 6 * unit);
                break;
            case SidebarButton::OpenFile:
                MoveToEx(hdc, centerX - 7 * unit, centerY - 5 * unit, nullptr);
                LineTo(hdc, centerX - 2 * unit, centerY - 5 * unit);
                LineTo(hdc, centerX, centerY - 3 * unit);
                LineTo(hdc, centerX + 7 * unit, centerY - 3 * unit);
                LineTo(hdc, centerX + 7 * unit, centerY + 6 * unit);
                LineTo(hdc, centerX - 7 * unit, centerY + 6 * unit);
                LineTo(hdc, centerX - 7 * unit, centerY - 5 * unit);
                break;
            case SidebarButton::Search: {
                HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Ellipse(hdc, centerX - 6 * unit, centerY - 6 * unit,
                        centerX + 3 * unit, centerY + 3 * unit);
                SelectObject(hdc, oldBrush);
                MoveToEx(hdc, centerX + 2 * unit, centerY + 2 * unit, nullptr);
                LineTo(hdc, centerX + 7 * unit, centerY + 7 * unit);
                break;
            }
            case SidebarButton::Settings: {
                HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Ellipse(hdc, centerX - 5 * unit, centerY - 5 * unit,
                        centerX + 5 * unit, centerY + 5 * unit);
                Ellipse(hdc, centerX - 2 * unit, centerY - 2 * unit,
                        centerX + 2 * unit, centerY + 2 * unit);
                SelectObject(hdc, oldBrush);
                MoveToEx(hdc, centerX, centerY - 8 * unit, nullptr);
                LineTo(hdc, centerX, centerY - 5 * unit);
                MoveToEx(hdc, centerX, centerY + 5 * unit, nullptr);
                LineTo(hdc, centerX, centerY + 8 * unit);
                MoveToEx(hdc, centerX - 8 * unit, centerY, nullptr);
                LineTo(hdc, centerX - 5 * unit, centerY);
                MoveToEx(hdc, centerX + 5 * unit, centerY, nullptr);
                LineTo(hdc, centerX + 8 * unit, centerY);
                break;
            }
            case SidebarButton::Controls:
                MoveToEx(hdc, centerX - 7 * unit, centerY + 6 * unit, nullptr);
                LineTo(hdc, centerX - 2 * unit, centerY - 5 * unit);
                LineTo(hdc, centerX + 2 * unit, centerY + 2 * unit);
                LineTo(hdc, centerX + 7 * unit, centerY - 6 * unit);
                break;
            case SidebarButton::Cleaner:
                MoveToEx(hdc, centerX - 6 * unit, centerY + 5 * unit, nullptr);
                LineTo(hdc, centerX + 5 * unit, centerY - 6 * unit);
                MoveToEx(hdc, centerX - 7 * unit, centerY - 5 * unit, nullptr);
                LineTo(hdc, centerX + 5 * unit, centerY + 7 * unit);
                break;
            case SidebarButton::Player:
                MoveToEx(hdc, centerX - 4 * unit, centerY - 7 * unit, nullptr);
                LineTo(hdc, centerX + 7 * unit, centerY);
                LineTo(hdc, centerX - 4 * unit, centerY + 7 * unit);
                LineTo(hdc, centerX - 4 * unit, centerY - 7 * unit);
                break;
            case SidebarButton::Shortcuts:
                MoveToEx(hdc, centerX - 7 * unit, centerY - 5 * unit, nullptr);
                LineTo(hdc, centerX + 7 * unit, centerY - 5 * unit);
                MoveToEx(hdc, centerX - 7 * unit, centerY, nullptr);
                LineTo(hdc, centerX + 7 * unit, centerY);
                MoveToEx(hdc, centerX - 7 * unit, centerY + 5 * unit, nullptr);
                LineTo(hdc, centerX + 7 * unit, centerY + 5 * unit);
                break;
            case SidebarButton::Bookmarks:
                MoveToEx(hdc, centerX - 5 * unit, centerY - 7 * unit, nullptr);
                LineTo(hdc, centerX + 5 * unit, centerY - 7 * unit);
                LineTo(hdc, centerX + 5 * unit, centerY + 7 * unit);
                LineTo(hdc, centerX, centerY + 3 * unit);
                LineTo(hdc, centerX - 5 * unit, centerY + 7 * unit);
                LineTo(hdc, centerX - 5 * unit, centerY - 7 * unit);
                break;
            case SidebarButton::History: {
                HGDIOBJ oldClockBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Ellipse(hdc, centerX - 7 * unit, centerY - 7 * unit,
                        centerX + 7 * unit, centerY + 7 * unit);
                SelectObject(hdc, oldClockBrush);
                MoveToEx(hdc, centerX, centerY - 4 * unit, nullptr);
                LineTo(hdc, centerX, centerY);
                LineTo(hdc, centerX + 4 * unit, centerY + 2 * unit);
                break;
            }
            case SidebarButton::ToggleSize:
                if (m_sidebarExpanded) {
                    MoveToEx(hdc, centerX + 3 * unit, centerY - 6 * unit, nullptr);
                    LineTo(hdc, centerX - 3 * unit, centerY);
                    LineTo(hdc, centerX + 3 * unit, centerY + 6 * unit);
                } else {
                    MoveToEx(hdc, centerX - 3 * unit, centerY - 6 * unit, nullptr);
                    LineTo(hdc, centerX + 3 * unit, centerY);
                    LineTo(hdc, centerX - 3 * unit, centerY + 6 * unit);
                }
                break;
            default:
                break;
        }
        SelectObject(hdc, oldPen);
        DeleteObject(iconPen);

    }

    SelectObject(hdc, oldFont);
    DeleteObject(font);
    if (m_sidebarExpanded) {
        RECT panel = { MulDiv(58, m_dpi, 96), rail.top, width - 1, rail.bottom };
        DrawSidebarPanel(hdc, panel, palette);
    }
}

void WindowManager::DrawNavBar(HDC memDC, int width) {
    int stripH = GetTabStripHeight();
    int navH = GetNavBarHeight();
    const ThemePalette palette = GetThemePalette();

    RECT navR = { 0, stripH, width, stripH + navH };
    HBRUSH navBr = CreateSolidBrush(palette.navSurface);
    FillRect(memDC, &navR, navBr);
    DeleteObject(navBr);

    RECT btmR = { 0, stripH + navH - 1, width, stripH + navH };
    HBRUSH btmBr = CreateSolidBrush(palette.navBorder);
    FillRect(memDC, &btmR, btmBr);
    DeleteObject(btmBr);

    NavButton navBtns[] = {
        NavButton::Back, NavButton::Forward, NavButton::Reload,
        NavButton::ForceDark, NavButton::Settings
    };
    for (NavButton b : navBtns) {
        RECT r = GetNavButtonRect(b, width);
        bool isHover = (m_hoveredNavBtn == b);
        bool isPress = (m_pressedNavBtn == b);

        if (isPress) {
            HBRUSH br = CreateSolidBrush(palette.controlPressed);
            HPEN np = CreatePen(PS_NULL, 0, RGB(0, 0, 0));
            HGDIOBJ ob = SelectObject(memDC, br);
            HGDIOBJ op = SelectObject(memDC, np);
            RoundRect(memDC, r.left, r.top, r.right, r.bottom, 10, 10);
            SelectObject(memDC, ob);
            SelectObject(memDC, op);
            DeleteObject(br);
            DeleteObject(np);
        } else if (isHover) {
            HBRUSH br = CreateSolidBrush(palette.controlHover);
            HPEN np = CreatePen(PS_NULL, 0, RGB(0, 0, 0));
            HGDIOBJ ob = SelectObject(memDC, br);
            HGDIOBJ op = SelectObject(memDC, np);
            RoundRect(memDC, r.left, r.top, r.right, r.bottom, 10, 10);
            SelectObject(memDC, ob);
            SelectObject(memDC, op);
            DeleteObject(br);
            DeleteObject(np);
        }

        const bool forceDarkActive = b == NavButton::ForceDark &&
            m_browser && m_browser->IsForceDarkEnabled();
        const COLORREF iconColor = forceDarkActive ? palette.accent
            : isHover ? palette.primaryText : palette.secondaryText;
        HPEN iconPen = CreatePen(PS_SOLID, 1, iconColor);
        HGDIOBJ op2 = SelectObject(memDC, iconPen);
        int cX = r.left + (r.right - r.left) / 2;
        int cY = r.top + (r.bottom - r.top) / 2;

        if (b == NavButton::Back) {
            // Arrow Left: ←
            int aL = MulDiv(5, m_dpi, 96);
            MoveToEx(memDC, cX + aL, cY, nullptr);
            LineTo(memDC, cX - aL, cY);
            LineTo(memDC, cX - aL + MulDiv(3, m_dpi, 96), cY - MulDiv(3, m_dpi, 96));
            MoveToEx(memDC, cX - aL, cY, nullptr);
            LineTo(memDC, cX - aL + MulDiv(3, m_dpi, 96), cY + MulDiv(3, m_dpi, 96));
        } else if (b == NavButton::Forward) {
            // Arrow Right: →
            int aL = MulDiv(5, m_dpi, 96);
            MoveToEx(memDC, cX - aL, cY, nullptr);
            LineTo(memDC, cX + aL, cY);
            LineTo(memDC, cX + aL - MulDiv(3, m_dpi, 96), cY - MulDiv(3, m_dpi, 96));
            MoveToEx(memDC, cX + aL, cY, nullptr);
            LineTo(memDC, cX + aL - MulDiv(3, m_dpi, 96), cY + MulDiv(3, m_dpi, 96));
        } else if (b == NavButton::Reload) {
            // Circular reload arc: ↻
            int arcR = MulDiv(5, m_dpi, 96);
            Arc(memDC, cX - arcR, cY - arcR, cX + arcR, cY + arcR,
                cX, cY - arcR, cX + arcR, cY);
            MoveToEx(memDC, cX, cY - arcR - MulDiv(2, m_dpi, 96), nullptr);
            LineTo(memDC, cX, cY - arcR + MulDiv(3, m_dpi, 96));
            LineTo(memDC, cX + MulDiv(3, m_dpi, 96), cY - arcR);
        } else if (b == NavButton::Settings) {
            const int radius = MulDiv(5, m_dpi, 96);
            const int tooth = MulDiv(2, m_dpi, 96);
            HGDIOBJ oldBrush = SelectObject(memDC, GetStockObject(NULL_BRUSH));
            Ellipse(memDC, cX - radius, cY - radius, cX + radius + 1, cY + radius + 1);
            Ellipse(memDC, cX - MulDiv(2, m_dpi, 96), cY - MulDiv(2, m_dpi, 96),
                    cX + MulDiv(2, m_dpi, 96) + 1, cY + MulDiv(2, m_dpi, 96) + 1);
            MoveToEx(memDC, cX - radius - tooth, cY, nullptr);
            LineTo(memDC, cX - radius + 1, cY);
            MoveToEx(memDC, cX + radius, cY, nullptr);
            LineTo(memDC, cX + radius + tooth, cY);
            MoveToEx(memDC, cX, cY - radius - tooth, nullptr);
            LineTo(memDC, cX, cY - radius + 1);
            MoveToEx(memDC, cX, cY + radius, nullptr);
            LineTo(memDC, cX, cY + radius + tooth);
            SelectObject(memDC, oldBrush);
        } else if (b == NavButton::ForceDark) {
            const int radius = MulDiv(5, m_dpi, 96);
            HGDIOBJ oldBrush = SelectObject(memDC, GetStockObject(NULL_BRUSH));
            Ellipse(memDC, cX - radius, cY - radius, cX + radius + 1, cY + radius + 1);
            SelectObject(memDC, oldBrush);
            HBRUSH cutout = CreateSolidBrush(isPress ? palette.controlPressed
                : isHover ? palette.controlHover : palette.navSurface);
            oldBrush = SelectObject(memDC, cutout);
            Ellipse(memDC, cX - radius / 3, cY - radius - 1,
                    cX + radius + radius / 2, cY + radius / 2);
            SelectObject(memDC, oldBrush);
            DeleteObject(cutout);
        }

        SelectObject(memDC, op2);
        DeleteObject(iconPen);
    }

    // 3. Omnibox / URL Address Bar
    RECT omniR = GetOmniboxRect(width);
    bool isOmniHover = (m_hoveredNavBtn == NavButton::Omnibox);

    HBRUSH omniBr = CreateSolidBrush(palette.searchSurface);
    HPEN omniPen = CreatePen(PS_SOLID, 1, isOmniHover ? palette.accent : palette.searchBorder);
    HGDIOBJ obOmni = SelectObject(memDC, omniBr);
    HGDIOBJ opOmni = SelectObject(memDC, omniPen);

    RoundRect(memDC, omniR.left, omniR.top, omniR.right, omniR.bottom, 16, 16);

    SelectObject(memDC, obOmni);
    SelectObject(memDC, opOmni);
    DeleteObject(omniBr);
    DeleteObject(omniPen);

    // Omnibox security lock / icon
    int lockSize = MulDiv(10, m_dpi, 96);
    int lockX = omniR.left + MulDiv(12, m_dpi, 96);
    int lockY = omniR.top + (omniR.bottom - omniR.top - lockSize) / 2;

    HPEN lockPen = CreatePen(PS_SOLID, 1, palette.accent);
    HGDIOBJ opLock = SelectObject(memDC, lockPen);
    Rectangle(memDC, lockX, lockY + MulDiv(4, m_dpi, 96), lockX + lockSize, lockY + lockSize);
    Arc(memDC, lockX + MulDiv(2, m_dpi, 96), lockY,
        lockX + lockSize - MulDiv(2, m_dpi, 96), lockY + MulDiv(7, m_dpi, 96),
        lockX + lockSize - MulDiv(2, m_dpi, 96), lockY + MulDiv(4, m_dpi, 96),
        lockX + MulDiv(2, m_dpi, 96), lockY + MulDiv(4, m_dpi, 96));
    SelectObject(memDC, opLock);
    DeleteObject(lockPen);

    // Omnibox Text (shows current tab URL)
    std::wstring displayUrl = L"bloxcy://home";
    if (m_activeTabIndex < m_tabs.size()) {
        displayUrl = m_tabs[m_activeTabIndex].url;
    }
    const bool isLocalFile = displayUrl.compare(0, 8, L"file:///") == 0;
    if (isLocalFile) displayUrl = L"(!FILE) " + displayUrl;

    HFONT omniFont = CreateFontW(
        -MulDiv(12, m_dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    HFONT oldFont = static_cast<HFONT>(SelectObject(memDC, omniFont));

    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, palette.primaryText);

    RECT urlTextR = {
        lockX + lockSize + MulDiv(8, m_dpi, 96),
        omniR.top,
        GetNavButtonRect(NavButton::TrustSignals, width).left - MulDiv(6, m_dpi, 96),
        omniR.bottom
    };
    DrawTextW(memDC, displayUrl.c_str(), -1, &urlTextR, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

    const RECT trustRect = GetNavButtonRect(NavButton::TrustSignals, width);
    const int trustScore = m_browser ? m_browser->GetTrustScore() : -1;
    const COLORREF trustColor = isLocalFile ? palette.controlHover
        : trustScore < 0 ? palette.controlHover
        : trustScore <= 33 ? RGB(176, 54, 58)
        : trustScore <= 67 ? RGB(171, 119, 18)
        : RGB(35, 126, 78);
    HBRUSH trustBrush = CreateSolidBrush(trustColor);
    HGDIOBJ oldTrustBrush = SelectObject(memDC, trustBrush);
    HGDIOBJ oldTrustPen = SelectObject(memDC, GetStockObject(NULL_PEN));
    RoundRect(memDC, trustRect.left, trustRect.top, trustRect.right, trustRect.bottom,
              MulDiv(8, m_dpi, 96), MulDiv(8, m_dpi, 96));
    SelectObject(memDC, oldTrustBrush);
    SelectObject(memDC, oldTrustPen);
    DeleteObject(trustBrush);
    SetTextColor(memDC, trustScore < 0 || isLocalFile ? palette.secondaryText : RGB(255, 255, 255));
    const std::wstring trustLabel = isLocalFile ? L"FILE" : trustScore < 0
        ? L"T: ?" : L"T: " + std::to_wstring(trustScore);
    RECT trustText = trustRect;
    DrawTextW(memDC, trustLabel.c_str(), -1, &trustText, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    // Bookmark star icon on right of omnibox
    int starX = omniR.right - MulDiv(20, m_dpi, 96);
    int starY = omniR.top + (omniR.bottom - omniR.top) / 2;
    HPEN starPen = CreatePen(PS_SOLID, 1, palette.mutedText);
    HGDIOBJ opStar = SelectObject(memDC, starPen);
    int sR = MulDiv(4, m_dpi, 96);
    MoveToEx(memDC, starX, starY - sR, nullptr);
    LineTo(memDC, starX + sR, starY + sR);
    LineTo(memDC, starX - sR, starY);
    LineTo(memDC, starX + sR, starY);
    LineTo(memDC, starX - sR, starY + sR);
    LineTo(memDC, starX, starY - sR);
    SelectObject(memDC, opStar);
    DeleteObject(starPen);

    SelectObject(memDC, oldFont);
    DeleteObject(omniFont);
}

void WindowManager::DrawHorizontalTabs(HDC memDC, int width) {
    const ThemePalette palette = GetThemePalette();
    RECT strip = { 0, 0, width, GetTabStripHeight() };
    HBRUSH stripBrush = CreateSolidBrush(palette.tabStrip);
    FillRect(memDC, &strip, stripBrush);
    DeleteObject(stripBrush);
    SetBkMode(memDC, TRANSPARENT);

    const int visibleCount = GetVisibleTabCount(width);
    const size_t first = static_cast<size_t>(std::max(0, m_tabScrollOffset));
    const size_t end = std::min(m_tabs.size(), first + static_cast<size_t>(visibleCount));
    const int clip = SaveDC(memDC);
    IntersectClipRect(memDC, 0, 0, width - MulDiv(m_baseCaptionBtnWidth * 3, m_dpi, 96),
                      GetTabStripHeight());

    HFONT font = CreateFontW(-MulDiv(12, m_dpi, 96), 0, 0, 0, FW_MEDIUM,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT oldFont = static_cast<HFONT>(SelectObject(memDC, font));
    for (size_t index = first; index < end; ++index) {
        const BrowserTab& tab = m_tabs[index];
        const RECT rect = GetTabRect(index, width);
        const bool active = index == m_activeTabIndex;
        if (active || static_cast<int>(index) == m_hoveredTabIndex) {
            HBRUSH background = CreateSolidBrush(active ? palette.activeTab : palette.inactiveHover);
            HPEN border = CreatePen(PS_SOLID, 1, active ? palette.cardBorder : palette.navBorder);
            HGDIOBJ oldBrush = SelectObject(memDC, background);
            HGDIOBJ oldPen = SelectObject(memDC, border);
            RoundRect(memDC, rect.left, rect.top, rect.right, rect.bottom,
                MulDiv(9, m_dpi, 96), MulDiv(9, m_dpi, 96));
            SelectObject(memDC, oldBrush);
            SelectObject(memDC, oldPen);
            DeleteObject(background);
            DeleteObject(border);
        }

        if (tab.groupId != 0) {
            const auto group = std::find_if(m_tabGroups.begin(), m_tabGroups.end(),
                [&tab](const TabGroup& candidate) { return candidate.id == tab.groupId; });
            if (group != m_tabGroups.end()) {
                RECT groupLine = { rect.left + MulDiv(8, m_dpi, 96), rect.top,
                    rect.right - MulDiv(8, m_dpi, 96), rect.top + MulDiv(3, m_dpi, 96) };
                HBRUSH groupBrush = CreateSolidBrush(group->color);
                FillRect(memDC, &groupLine, groupBrush);
                DeleteObject(groupBrush);
            }
        }

        const int iconSize = MulDiv(18, m_dpi, 96);
        const int iconLeft = rect.left + MulDiv(9, m_dpi, 96);
        const int iconTop = rect.top + (rect.bottom - rect.top - iconSize) / 2;
        const auto favicon = m_favicons.find(tab.url);
        if (favicon != m_favicons.end() && favicon->second) {
            HDC iconDc = CreateCompatibleDC(memDC);
            if (iconDc) {
                HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(iconDc, favicon->second));
                BITMAP bitmap = {};
                GetObjectW(favicon->second, sizeof(bitmap), &bitmap);
                StretchBlt(memDC, iconLeft, iconTop, iconSize, iconSize,
                    iconDc, 0, 0, bitmap.bmWidth, bitmap.bmHeight, SRCCOPY);
                SelectObject(iconDc, oldBitmap);
                DeleteDC(iconDc);
            }
        } else {
            HBRUSH iconBrush = CreateSolidBrush(tab.badgeColor);
            RECT iconRect = { iconLeft, iconTop, iconLeft + iconSize, iconTop + iconSize };
            HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(memDC, iconBrush));
            RoundRect(memDC, iconRect.left, iconRect.top, iconRect.right, iconRect.bottom,
                MulDiv(5, m_dpi, 96), MulDiv(5, m_dpi, 96));
            SelectObject(memDC, oldBrush);
            DeleteObject(iconBrush);
            SetTextColor(memDC, RGB(255, 255, 255));
            DrawTextW(memDC, TabLogoText(tab).c_str(), -1, &iconRect,
                DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        }

        const RECT close = GetTabCloseRect(rect);
        RECT label = { iconLeft + iconSize + MulDiv(8, m_dpi, 96), rect.top,
            close.left - MulDiv(4, m_dpi, 96), rect.bottom };
        SetTextColor(memDC, active ? palette.primaryText : palette.secondaryText);
        DrawTextW(memDC, tab.title.c_str(), -1, &label,
            DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        if (static_cast<int>(index) == m_hoveredTabCloseIndex) {
            HBRUSH closeBrush = CreateSolidBrush(palette.controlHover);
            RECT closeBackground = { close.left, close.top, close.right, close.bottom };
            FillRect(memDC, &closeBackground, closeBrush);
            DeleteObject(closeBrush);
        }
        SetTextColor(memDC, palette.secondaryText);
        RECT closeGlyph = { close.left, close.top - MulDiv(1, m_dpi, 96), close.right, close.bottom };
        DrawTextW(memDC, L"\x00D7", -1, &closeGlyph, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    }
    SelectObject(memDC, oldFont);
    DeleteObject(font);
    RestoreDC(memDC, clip);

    const RECT newTab = GetNewTabButtonRect(width);
    HPEN plusPen = CreatePen(PS_SOLID, 1, palette.secondaryText);
    HGDIOBJ oldPen = SelectObject(memDC, plusPen);
    const int centerX = newTab.left + (newTab.right - newTab.left) / 2;
    const int centerY = newTab.top + (newTab.bottom - newTab.top) / 2;
    const int radius = MulDiv(4, m_dpi, 96);
    MoveToEx(memDC, centerX - radius, centerY, nullptr);
    LineTo(memDC, centerX + radius + 1, centerY);
    MoveToEx(memDC, centerX, centerY - radius, nullptr);
    LineTo(memDC, centerX, centerY + radius + 1);
    SelectObject(memDC, oldPen);
    DeleteObject(plusPen);

    for (CaptionButton button : { CaptionButton::Minimize, CaptionButton::Maximize, CaptionButton::Close }) {
        const RECT rect = GetCaptionButtonRect(button);
        const bool hovered = button == m_hoveredCaptionBtn;
        if (hovered) {
            HBRUSH hoverBrush = CreateSolidBrush(button == CaptionButton::Close
                ? RGB(232, 17, 35) : palette.controlHover);
            FillRect(memDC, &rect, hoverBrush);
            DeleteObject(hoverBrush);
        }
        const COLORREF color = button == CaptionButton::Close && hovered
            ? RGB(255, 255, 255) : palette.secondaryText;
        HPEN pen = CreatePen(PS_SOLID, 1, color);
        HGDIOBJ previousPen = SelectObject(memDC, pen);
        const int x = rect.left + (rect.right - rect.left) / 2;
        const int y = rect.top + (rect.bottom - rect.top) / 2;
        const int size = MulDiv(5, m_dpi, 96);
        if (button == CaptionButton::Minimize) {
            MoveToEx(memDC, x - size, y, nullptr);
            LineTo(memDC, x + size + 1, y);
        } else if (button == CaptionButton::Maximize) {
            Rectangle(memDC, x - size, y - size, x + size + 1, y + size + 1);
        } else {
            MoveToEx(memDC, x - size, y - size, nullptr);
            LineTo(memDC, x + size + 1, y + size + 1);
            MoveToEx(memDC, x + size, y - size, nullptr);
            LineTo(memDC, x - size - 1, y + size + 1);
        }
        SelectObject(memDC, previousPen);
        DeleteObject(pen);
    }
}

void WindowManager::DrawContent(HDC memDC, int width, int height) {
    int topOffset = GetTotalTopHeight();
    RECT contentR = { GetSidebarWidth(), topOffset, width, height };
    const ThemePalette palette = GetThemePalette();

    HBRUSH bgBrush = CreateSolidBrush(palette.content);
    FillRect(memDC, &contentR, bgBrush);
    DeleteObject(bgBrush);

    // If active tab is not home, show a sleek page frame with the tab's url
    if (m_activeTabIndex < m_tabs.size() && !m_tabs[m_activeTabIndex].isHome) {
        const auto& currentTab = m_tabs[m_activeTabIndex];

        HFONT titleFont = CreateFontW(
            -MulDiv(26, m_dpi, 96), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
        HFONT oldF = static_cast<HFONT>(SelectObject(memDC, titleFont));
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, palette.primaryText);

        RECT tR = { 0, topOffset + MulDiv(80, m_dpi, 96), width, topOffset + MulDiv(130, m_dpi, 96) };
        DrawTextW(memDC, currentTab.title.c_str(), -1, &tR, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

        SelectObject(memDC, oldF);
        DeleteObject(titleFont);

        HFONT subFont = CreateFontW(
            -MulDiv(14, m_dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
        oldF = static_cast<HFONT>(SelectObject(memDC, subFont));
        SetTextColor(memDC, palette.secondaryText);

        RECT uR = { 0, topOffset + MulDiv(130, m_dpi, 96), width, topOffset + MulDiv(160, m_dpi, 96) };
        DrawTextW(memDC, currentTab.url.c_str(), -1, &uR, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

        SelectObject(memDC, oldF);
        DeleteObject(subFont);
        return;
    }

    // --- BLOXCY REVO HOMEPAGE CONTENT ---
    int centerY = topOffset + (height - topOffset) / 2;
    int brandingY = centerY - MulDiv(160, m_dpi, 96);

    // 1. BLOXCY Slab Serif Title
    HFONT logoFont = CreateFontW(
        -MulDiv(62, m_dpi, 96), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Rockwell"
    );
    HFONT oldF = static_cast<HFONT>(SelectObject(memDC, logoFont));

    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, palette.primaryText);

    RECT logoR = { 0, brandingY, width, brandingY + MulDiv(70, m_dpi, 96) };
    DrawTextW(memDC, L"BLOXCY", -1, &logoR, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    SelectObject(memDC, oldF);
    DeleteObject(logoFont);

    // 2. Glowing Neon Cyan Subtitle: "REVO IS HERE!!"
    HFONT revoFont = CreateFontW(
        -MulDiv(16, m_dpi, 96), 0, 0, 0, FW_EXTRABOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    oldF = static_cast<HFONT>(SelectObject(memDC, revoFont));

    SetTextColor(memDC, palette.accent);
    RECT revoR = { 0, brandingY + MulDiv(72, m_dpi, 96), width, brandingY + MulDiv(102, m_dpi, 96) };
    DrawTextW(memDC, L"REVO IS HERE!!", -1, &revoR, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    SelectObject(memDC, oldF);
    DeleteObject(revoFont);

    // 3. Search Bar Widget
    int searchW = MulDiv(580, m_dpi, 96);
    int searchH = MulDiv(50, m_dpi, 96);
    int searchX = (width - searchW) / 2;
    int searchY = brandingY + MulDiv(120, m_dpi, 96);

    RECT searchR = { searchX, searchY, searchX + searchW, searchY + searchH };
    HBRUSH sBr = CreateSolidBrush(palette.searchSurface);
    HPEN sPen = CreatePen(PS_SOLID, 1, palette.searchBorder);
    HGDIOBJ obS = SelectObject(memDC, sBr);
    HGDIOBJ opS = SelectObject(memDC, sPen);
    RoundRect(memDC, searchR.left, searchR.top, searchR.right, searchR.bottom, searchH, searchH);
    SelectObject(memDC, obS);
    SelectObject(memDC, opS);
    DeleteObject(sBr);
    DeleteObject(sPen);

    // Search Icon
    int magX = searchR.left + MulDiv(20, m_dpi, 96);
    int magY = searchR.top + searchH / 2;
    HPEN magPen = CreatePen(PS_SOLID, 2, palette.mutedText);
    HGDIOBJ opMag = SelectObject(memDC, magPen);
    int mr = MulDiv(6, m_dpi, 96);
    HGDIOBJ obMag = SelectObject(memDC, GetStockObject(NULL_BRUSH));
    Ellipse(memDC, magX - mr, magY - mr, magX + mr, magY + mr);
    MoveToEx(memDC, magX + mr - 2, magY + mr - 2, nullptr);
    LineTo(memDC, magX + mr + MulDiv(5, m_dpi, 96), magY + mr + MulDiv(5, m_dpi, 96));
    SelectObject(memDC, obMag);
    SelectObject(memDC, opMag);
    DeleteObject(magPen);

    // Search Placeholder Text
    HFONT phFont = CreateFontW(
        -MulDiv(14, m_dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    oldF = static_cast<HFONT>(SelectObject(memDC, phFont));
    SetTextColor(memDC, palette.mutedText);

    RECT phTextR = {
        magX + mr + MulDiv(14, m_dpi, 96),
        searchR.top,
        searchR.right - MulDiv(40, m_dpi, 96),
        searchR.bottom
    };
    DrawTextW(memDC, L"Search or enter address...", -1, &phTextR, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

    SelectObject(memDC, oldF);
    DeleteObject(phFont);

    // 4. Quick Link / Shortcut Cards
    int tileW = MulDiv(66, m_dpi, 96);
    int tileH = MulDiv(66, m_dpi, 96);
    int gap = MulDiv(22, m_dpi, 96);
    int totalTilesW = static_cast<int>(m_shortcuts.size()) * tileW + (static_cast<int>(m_shortcuts.size()) - 1) * gap;
    int startX = (width - totalTilesW) / 2;
    int tileY = searchY + searchH + MulDiv(38, m_dpi, 96);

    HFONT labelFont = CreateFontW(
        -MulDiv(12, m_dpi, 96), 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    oldF = static_cast<HFONT>(SelectObject(memDC, labelFont));

    for (size_t i = 0; i < m_shortcuts.size(); ++i) {
        auto& sc = m_shortcuts[i];
        int x = startX + static_cast<int>(i) * (tileW + gap);
        sc.rect = { x, tileY, x + tileW, tileY + tileH };

        bool isTileHover = (static_cast<int>(i) == m_hoveredShortcutIndex);
        int currentY = isTileHover ? (tileY - MulDiv(4, m_dpi, 96)) : tileY;

        // Tile Box
        HBRUSH tBr = CreateSolidBrush(isTileHover ? palette.cardHover : palette.card);
        HPEN tPen = CreatePen(PS_SOLID, 1, isTileHover ? palette.accent : palette.cardBorder);
        HGDIOBJ obT = SelectObject(memDC, tBr);
        HGDIOBJ opT = SelectObject(memDC, tPen);

        RoundRect(memDC, x, currentY, x + tileW, currentY + tileH, 18, 18);

        SelectObject(memDC, obT);
        SelectObject(memDC, opT);
        DeleteObject(tBr);
        DeleteObject(tPen);

        // Center Icon Dot
        int iconDotSize = MulDiv(18, m_dpi, 96);
        int icX = x + (tileW - iconDotSize) / 2;
        int icY = currentY + (tileH - iconDotSize) / 2;

        HBRUSH icBr = CreateSolidBrush(sc.iconBg);
        HPEN np = CreatePen(PS_NULL, 0, RGB(0, 0, 0));
        HGDIOBJ obIc = SelectObject(memDC, icBr);
        HGDIOBJ opIc = SelectObject(memDC, np);
        RoundRect(memDC, icX, icY, icX + iconDotSize, icY + iconDotSize, 6, 6);
        SelectObject(memDC, obIc);
        SelectObject(memDC, opIc);
        DeleteObject(icBr);
        DeleteObject(np);

        // Label below
        SetTextColor(memDC, isTileHover ? palette.primaryText : palette.secondaryText);
        RECT lblR = { x - MulDiv(10, m_dpi, 96), currentY + tileH + MulDiv(8, m_dpi, 96),
                      x + tileW + MulDiv(10, m_dpi, 96), currentY + tileH + MulDiv(30, m_dpi, 96) };
        DrawTextW(memDC, sc.name.c_str(), -1, &lblR, DT_SINGLELINE | DT_CENTER | DT_TOP);
    }

    SelectObject(memDC, oldF);
    DeleteObject(labelFont);
}

void WindowManager::HandlePaint(HDC hdc) {
    RECT clientRect;
    GetClientRect(m_hWnd, &clientRect);
    int width = clientRect.right - clientRect.left;
    int height = clientRect.bottom - clientRect.top;

    if (width <= 0 || height <= 0) return;

    if (m_paintBufferDC && (m_paintBufferWidth != width || m_paintBufferHeight != height)) {
        ReleasePaintBuffer();
    }
    if (!m_paintBufferDC) {
        m_paintBufferDC = CreateCompatibleDC(hdc);
        m_paintBufferBitmap = m_paintBufferDC
            ? CreateCompatibleBitmap(hdc, width, height) : nullptr;
        if (m_paintBufferDC && m_paintBufferBitmap) {
            m_paintBufferPreviousBitmap = static_cast<HBITMAP>(
                SelectObject(m_paintBufferDC, m_paintBufferBitmap));
            if (m_paintBufferPreviousBitmap) {
                m_paintBufferWidth = width;
                m_paintBufferHeight = height;
            } else {
                ReleasePaintBuffer();
            }
        } else {
            ReleasePaintBuffer();
        }
    }
    HDC const renderDC = m_paintBufferDC ? m_paintBufferDC : hdc;

    // 1. Draw Google Chrome-style tab strip at top
    DrawTabs(renderDC, width);

    // 2. Draw connected navigation toolbar
    DrawNavBar(renderDC, width);

    // The WebView owns page content once initialized; avoid repainting duplicate
    // homepage content underneath it on every toolbar hover.
    if (!m_browser) DrawContent(renderDC, width, height);

    // Keep the sidebar outside the WebView bounds so it remains native and clickable.
    DrawSidebar(renderDC, height);

    // 4. BitBlt to display
    if (m_paintBufferDC) BitBlt(hdc, 0, 0, width, height, m_paintBufferDC, 0, 0, SRCCOPY);
}

LRESULT WindowManager::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_NCCALCSIZE:
            return HandleNcCalcSize(wParam, lParam);

        case WM_NCHITTEST:
            return HandleNcHitTest(wParam, lParam);

        case WM_ERASEBKGND:
            return 1;

        case WM_CTLCOLOREDIT:
            if (reinterpret_cast<HWND>(lParam) == m_addressEdit) {
                HDC editDc = reinterpret_cast<HDC>(wParam);
                const ThemePalette palette = GetThemePalette();
                SetTextColor(editDc, palette.primaryText);
                SetBkColor(editDc, palette.searchSurface);
                SetDCBrushColor(editDc, palette.searchSurface);
                return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
            }
            break;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(m_hWnd, &ps);
            HandlePaint(hdc);
            EndPaint(m_hWnd, &ps);
            return 0;
        }

        case WM_MOUSEMOVE:
            HandleMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_MOUSELEAVE:
            HandleMouseLeave();
            return 0;

        case WM_MOUSEWHEEL: {
            POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(m_hWnd, &point);
            HandleMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam), point.x, point.y);
            return 0;
        }

        case WM_LBUTTONDOWN:
            HandleLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_LBUTTONUP:
            HandleLButtonUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_RBUTTONUP:
            HandleRButtonUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_CAPTURECHANGED:
            m_draggedTabIndex = -1;
            m_isDraggingTab = false;
            return 0;

        case WM_SIZE:
            ResizeBrowserView();
            InvalidateRect(m_hWnd, nullptr, FALSE);
            return 0;

        case WM_COMMAND:
            if (LOWORD(wParam) == 1 && HIWORD(wParam) == EN_KILLFOCUS && m_addressEdit) {
                ShowWindow(m_addressEdit, SW_HIDE);
                return 0;
            }
            break;

        case WM_HOTKEY:
            HandleHotkey(static_cast<int>(wParam));
            return 0;

        case kNavigateAddressMessage:
            NavigateFromAddressEdit();
            return 0;

        case kCancelAddressMessage:
            if (m_addressEdit) ShowWindow(m_addressEdit, SW_HIDE);
            SetFocus(m_hWnd);
            return 0;

        case kBrowserStateChangedMessage:
            UpdateActiveTabFromBrowser();
            return 0;

        case kSetThemeMessage:
            SetTheme(static_cast<Theme>(wParam));
            return 0;

        case kSetBrowserModeMessage:
            if (wParam <= kBrowserModePrivacy) {
                m_browserMode = static_cast<int>(wParam);
                if (m_browserMode == kBrowserModeWellness) {
                    if (!SetTimer(m_hWnd, kEyeWellnessTimer, 15 * 60 * 1000, nullptr)) {
                        MessageBoxW(m_hWnd, L"Could not start the 15-minute eye wellness reminder.",
                                    L"Wellness mode", MB_OK | MB_ICONWARNING);
                    }
                } else {
                    KillTimer(m_hWnd, kEyeWellnessTimer);
                }
                if (lParam != 0) {
                    const Theme modeTheme = m_browserMode == kBrowserModeWellness ||
                        m_browserMode == kBrowserModeNormal ? Theme::Light : Theme::Obsidian;
                    SetTheme(modeTheme);
                }
                InvalidateRect(m_hWnd, nullptr, FALSE);
            }
            return 0;

        case kRequestMemoryEstimateMessage:
            if (m_browser) m_browser->RequestMemoryEstimate();
            return 0;

        case kBrowserMemoryEstimateMessage:
            if (m_activeTabIndex < m_tabs.size() &&
                !m_tabs[m_activeTabIndex].isHibernated) {
                m_tabs[m_activeTabIndex].memoryEstimateBytes = static_cast<size_t>(wParam);
                InvalidateRect(m_hWnd, nullptr, FALSE);
            }
            return 0;

        case kSetCustomPaletteMessage:
            if (wParam == static_cast<WPARAM>(-1)) {
                m_usesCustomPalette = false;
                InvalidateRect(m_hWnd, nullptr, FALSE);
            } else {
                SetCustomPalette(static_cast<COLORREF>(wParam), static_cast<COLORREF>(lParam));
            }
            return 0;

        case kOpenUrlMessage: {
            std::unique_ptr<std::wstring> target(reinterpret_cast<std::wstring*>(wParam));
            if (target && target->size() <= 2048 && IsSafeHttpUrl(*target)) {
                AddTab(L"New tab", *target, RGB(90, 150, 220), false);
            }
            return 0;
        }

        case kBrowserFaviconMessage: {
            std::unique_ptr<BrowserFaviconUpdate> update(
                reinterpret_cast<BrowserFaviconUpdate*>(wParam));
            if (update && update->bitmap && m_browser &&
                m_activeTabIndex < m_tabs.size() &&
                m_browser->GetCurrentUrl() == update->url) {
                UpdateActiveTabFromBrowser();
                const std::wstring key = m_tabs[m_activeTabIndex].url;
                const auto existing = m_favicons.find(key);
                if (existing != m_favicons.end()) {
                    DeleteObject(existing->second);
                    existing->second = update->bitmap;
                } else {
                    m_favicons.emplace(key, update->bitmap);
                }
                update->bitmap = nullptr;
                InvalidateRect(m_hWnd, nullptr, FALSE);
            }
            if (update && update->bitmap) DeleteObject(update->bitmap);
            return 0;
        }

        case kBrowserDataClearedMessage: {
            const HRESULT result = static_cast<HRESULT>(wParam);
            if (FAILED(result)) {
                MessageBoxW(m_hWnd, L"WebView2 reported an error while clearing browser data.",
                            L"Cleaner", MB_OK | MB_ICONERROR);
            } else {
                MessageBoxW(m_hWnd, L"WebView2 browser data was cleared.",
                            L"Cleaner", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        }

        case kFocusAddressMessage:
            BeginAddressEdit();
            return 0;

        case kNewTabMessage:
            AddTab(L"New Tab", L"bloxcy://home", RGB(78, 228, 255), true);
            return 0;

        case WM_DPICHANGED: {
            m_dpi = HIWORD(wParam);
            if (m_addressFont) {
                HFONT replacement = CreateFontW(
                    -MulDiv(12, m_dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable");
                if (!replacement) {
                    replacement = CreateFontW(
                        -MulDiv(12, m_dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
                }
                if (replacement) {
                    if (m_addressEdit) {
                        SendMessageW(m_addressEdit, WM_SETFONT,
                                     reinterpret_cast<WPARAM>(replacement), TRUE);
                    }
                    DeleteObject(m_addressFont);
                    m_addressFont = replacement;
                }
            }
            auto* prcNewWindow = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(m_hWnd, nullptr,
                         prcNewWindow->left, prcNewWindow->top,
                         prcNewWindow->right - prcNewWindow->left,
                         prcNewWindow->bottom - prcNewWindow->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            InvalidateRect(m_hWnd, nullptr, FALSE);
            return 0;
        }

        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = MulDiv(600, m_dpi, 96);
            mmi->ptMinTrackSize.y = MulDiv(450, m_dpi, 96);
            return 0;
        }

        case WM_DESTROY:
            KillTimer(m_hWnd, kMemoryEstimateTimer);
            if (m_addressEdit) {
                RemoveWindowSubclass(m_addressEdit, AddressEditProc, 1);
                DestroyWindow(m_addressEdit);
                m_addressEdit = nullptr;
            }
            if (m_browser) m_browser->Shutdown();
            for (int id = kHotkeyNewTab; id <= kHotkeyOpenFile; ++id) {
                UnregisterHotKey(m_hWnd, id);
            }
            PostQuitMessage(0);
            return 0;

        case WM_TIMER:
            if (wParam == kMemoryEstimateTimer && m_browser) {
                m_browser->RequestMemoryEstimate();
                return 0;
            }
            if (wParam == kEyeWellnessTimer && m_browserMode == kBrowserModeWellness) {
                MessageBoxW(m_hWnd,
                    L"Take a 20-second eye break. Look at something about 20 feet away, "
                    L"blink, and relax your shoulders.",
                    L"Wellness reminder", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            break;

        default:
            break;
    }
    return DefWindowProcW(m_hWnd, msg, wParam, lParam);
}

} // namespace Bloxcy

/**
 * WinMain application entry point.
 */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    Bloxcy::AccountStore accountStore;
    Bloxcy::AccountSession accountSession;
    std::wstring sessionError;
    if (!accountStore.GetDefaultSession(accountSession, sessionError)) {
        if (!accountStore.Authenticate(nullptr, accountSession)) return 0;
    }

    Bloxcy::WindowManager winMgr;
    winMgr.SetAccountSession(std::move(accountSession));

    if (!winMgr.Initialize(hInstance, L"Bloxcy REVO", 1240, 820)) {
        MessageBoxW(nullptr, L"Failed to initialize Bloxcy REVO window.", L"Error", MB_ICONERROR | MB_OK);
        return 1;
    }

    winMgr.Show(nCmdShow);
    return winMgr.RunMessageLoop();
}

/**
 * Standard main fallback for console/CLI builds.
 */
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    return WinMain(GetModuleHandleW(nullptr), nullptr, nullptr, SW_SHOWNORMAL);
}
