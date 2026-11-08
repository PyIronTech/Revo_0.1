/**
 * ============================================================================
 * Project: Bloxcy REVO Browser
 * File: window_manager.h
 * Description: Topless / Frameless Window Manager (Chrome-style integrated top with tabs)
 * =====================h
 *   winget install MSYS2.MSYS2
 *   # In MSYS2 UCRT64 terminal, run:
 *   pacman -Syu --noconfirm
 *   pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make
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
 * Using GCC / MinGW-w64 (g++):
 *   g++ -std=c++17 -O2 src/window/window_manager.cpp -o bloxcy_window.exe -mwindows -ldwmapi -lgdi32 -luser32 -luxtheme -lcomctl32
 * 
 * Using MSYS2 UCRT64 installed at C:\msys64:
 *   C:\msys64\ucrt64\bin\g++.exe -std=c++17 -O2 src/window/window_manager.cpp -o bloxcy_window.exe -mwindows -ldwmapi -lgdi32 -luser32 -luxtheme -lcomctl32
 * 
 * Using MSVC cl.exe (Developer Command Prompt):
 *   cl.exe /std:c++17 /O2 /EHsc src/window/window_manager.cpp /Fe:bloxcy_window.exe dwmapi.lib user32.lib gdi32.lib uxtheme.lib comctl32.lib /link /SUBSYSTEM:WINDOWS
 * 
 * To run:
 *   .\bloxcy_window.exe
 * ============================================================================
 */

#pragma once

#include "../accounts/account_store.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

namespace Bloxcy {

class BrowserHost;

/**
 * Custom window caption control buttons (Minimize, Maximize/Restore, Close).
 */
enum class CaptionButton {
    None = 0,
    Minimize,
    Maximize,
    Close
};

/**
 * Navigation toolbar button types.
 */
enum class NavButton {
    None = 0,
    Back,
    Forward,
    Reload,
    Omnibox,
    TrustSignals,
    ForceDark,
    Settings
};

enum class SidebarButton {
    None,
    Home,
    OpenFile,
    Search,
    Controls,
    Cleaner,
    Player,
    Shortcuts,
    Bookmarks,
    History,
    Settings,
    ToggleSize
};

enum class Theme {
    Obsidian,
    Light,
    Ocean,
    Forest
};

struct ThemePalette {
    COLORREF tabStrip;
    COLORREF navSurface;
    COLORREF navBorder;
    COLORREF activeTab;
    COLORREF inactiveHover;
    COLORREF primaryText;
    COLORREF secondaryText;
    COLORREF mutedText;
    COLORREF accent;
    COLORREF content;
    COLORREF card;
    COLORREF cardHover;
    COLORREF cardBorder;
    COLORREF searchSurface;
    COLORREF searchBorder;
    COLORREF controlHover;
    COLORREF controlPressed;
};

/**
 * Structure representing an individual browser tab.
 */
struct BrowserTab {
    std::wstring title;
    std::wstring url;
    COLORREF badgeColor;
    bool isHome;
    std::wstring searchQuery;
    int groupId = 0;
    bool isSettings = false;
    bool isHibernated = false;
    size_t memoryEstimateBytes = 0;
};

struct TabGroup {
    int id;
    std::wstring name;
    COLORREF color;
};

/**
 * Structure representing a homepage shortcut tile.
 */
struct ShortcutTile {
    std::wstring name;
    std::wstring url;
    COLORREF iconBg;
    RECT rect;
};

/**
 * WindowManager handles the creation and lifecycle of a modern,
 * frameless desktop window with a true Google Chrome-style integrated top:
 * - Tabs sit directly at the very top edge of the window frame (no separate titlebar).
 * - Connected navigation bar (Back, Forward, Reload, Omnibox/URL bar, Menu).
 * - Tab management: switch, drag to reorder, color-group, close, and restore tabs.
 * - Draggable tab strip space for window movement (HTCAPTION), double-click maximize, and Aero snap.
 * - Native edge/corner resizing, DWM drop shadows, and Windows 11 rounded corners.
 * - Interactive homepage content matching the Bloxcy REVO design.
 */
class WindowManager {
public:
    WindowManager();
    virtual ~WindowManager();

    WindowManager(const WindowManager&) = delete;
    WindowManager& operator=(const WindowManager&) = delete;

    /**
     * Initializes window class, styles, and creates the HWND.
     */
    bool Initialize(
        HINSTANCE hInstance = nullptr,
        const std::wstring& title = L"Bloxcy REVO",
        int width = 1200,
        int height = 800
    );

    /**
     * Shows and activates the window.
     */
    void Show(int nCmdShow = SW_SHOWNORMAL);

    /**
     * Runs the Win32 message loop until WM_QUIT.
     */
    int RunMessageLoop();

    /**
     * Closes the window.
     */
    void Close();
    void SetAccountSession(AccountSession&& session);

    /**
     * Retrieves the native HWND handle.
     */
    HWND GetHWND() const { return m_hWnd; }

    // Tab Management
    void AddTab(const std::wstring& title, const std::wstring& url, COLORREF color = RGB(78, 228, 255), bool isHome = false);
    void CloseTab(size_t index);
    void ReopenClosedTab();
    void SelectTab(size_t index);
    size_t GetTabCount() const { return m_tabs.size(); }
    size_t GetActiveTabIndex() const { return m_activeTabIndex; }

protected:
    static LRESULT CALLBACK StaticWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK AddressEditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                            UINT_PTR subclassId, DWORD_PTR refData);
    virtual LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // Frame and Hit-Test handlers
    LRESULT HandleNcCalcSize(WPARAM wParam, LPARAM lParam);
    LRESULT HandleNcHitTest(WPARAM wParam, LPARAM lParam);

    // Painting handlers
    void HandlePaint(HDC hdc);
    void DrawTabs(HDC hdc, int width);
    void DrawHorizontalTabs(HDC hdc, int width);
    void DrawNavBar(HDC hdc, int width);
    void DrawSidebar(HDC hdc, int height);
    void DrawContent(HDC hdc, int width, int height);

    // Mouse handlers
    void HandleMouseMove(int x, int y);
    void HandleLButtonDown(int x, int y);
    void HandleLButtonUp(int x, int y);
    void HandleRButtonUp(int x, int y);
    void HandleMouseWheel(short delta, int x, int y);
    void HandleMouseLeave();
    void ReorderTab(size_t fromIndex, size_t toIndex);
    void ShowTabContextMenu(size_t index, int x, int y);
    void OpenSettingsTab();
    void HibernateTab(size_t index);

    // Layout calculations (DPI-aware)
    int GetTabStripHeight() const;
    int GetNavBarHeight() const;
    int GetTotalTopHeight() const;
    int GetVerticalTabsWidth() const;
    int GetSidebarWidth() const;
    int GetVisibleTabCount(int totalWidth) const;
    RECT GetTabRect(size_t index, int totalWidth) const;
    RECT GetTabCloseRect(const RECT& tabRect) const;
    RECT GetNewTabButtonRect(int totalWidth) const;
    RECT GetCaptionButtonRect(CaptionButton btn) const;
    RECT GetNavButtonRect(NavButton btn, int totalWidth) const;
    RECT GetOmniboxRect(int totalWidth) const;

    // Hit-testing helpers
    int HitTestTabs(int x, int y, int totalWidth) const;
    int HitTestTabClose(int x, int y, int totalWidth) const;
    bool HitTestNewTabButton(int x, int y, int totalWidth) const;
    CaptionButton HitTestCaptionButtons(int x, int y) const;
    NavButton HitTestNavButtons(int x, int y, int totalWidth) const;
    SidebarButton HitTestSidebarButton(int x, int y) const;
    RECT GetSidebarButtonRect(SidebarButton button) const;
    int HitTestShortcutTiles(int x, int y) const;

    // Window style configuration
    void EnableImmersiveDarkMode();
    void ExtendFrameIntoClientArea();
    ThemePalette GetThemePalette() const;
    void ShowSettingsMenu();
    void SetTheme(Theme theme);
    void ResizeBrowserView();
    void BeginAddressEdit();
    void NavigateFromAddressEdit();
    void UpdateActiveTabFromBrowser();
    void HandleHotkey(int hotkeyId);
    void SetCustomPalette(COLORREF background, COLORREF accent);
    void OpenFile();
    void SaveAppearanceSetting(const wchar_t* key, bool enabled);
    void ReleasePaintBuffer();
    bool RefreshSidebarData();
    void SaveCurrentBookmark();
    void HandleSidebarPanelClick(int x, int y);
    void DrawSidebarPanel(HDC hdc, const RECT& panel, const ThemePalette& palette);
    void StartCleaner();
    void ApplyResourceMode(int mode);
    void ClearPrivacyCookiesForTabChange();

private:
    HWND m_hWnd;
    HINSTANCE m_hInstance;
    HWND m_addressEdit;
    HFONT m_addressFont;
    HDC m_paintBufferDC;
    HBITMAP m_paintBufferBitmap;
    HBITMAP m_paintBufferPreviousBitmap;
    int m_paintBufferWidth;
    int m_paintBufferHeight;
    std::shared_ptr<BrowserHost> m_browser;
    AccountStore m_accountStore;
    AccountSession m_accountSession;
    std::wstring m_appDirectory;
    std::wstring m_title;
    std::wstring m_className;

    // Dimensions (base values at 96 DPI)
    int m_baseTabStripHeight;
    int m_baseNavBarHeight;
    int m_baseCaptionBtnWidth;
    int m_baseVerticalTabsWidth;
    UINT m_dpi;
    Theme m_theme;

    // Tabs
    std::vector<BrowserTab> m_tabs;
    std::vector<BrowserTab> m_closedTabs;
    std::vector<TabGroup> m_tabGroups;
    size_t m_activeTabIndex;
    int m_hoveredTabIndex;
    int m_hoveredTabCloseIndex;
    int m_draggedTabIndex;
    int m_dragStartX;
    int m_dragStartY;
    bool m_isDraggingTab;
    int m_nextTabGroupId;
    int m_tabScrollOffset;
    bool m_hoveredNewTabBtn;
    bool m_usesCustomPalette;
    bool m_sidebarVisible;
    bool m_sidebarExpanded;
    int m_browserMode;
    SidebarButton m_activeSidebarPanel;
    bool m_cleanerSelections[6];
    int m_resourceMode;
    int m_sidebarScrollOffset;
    std::vector<SavedBookmark> m_bookmarks;
    std::vector<std::wstring> m_searchHistory;
    bool m_compactTabs;
    COLORREF m_customPaletteBackground;
    COLORREF m_customPaletteAccent;
    std::unordered_map<std::wstring, HBITMAP> m_favicons;

    // Window Controls
    CaptionButton m_hoveredCaptionBtn;
    CaptionButton m_pressedCaptionBtn;

    // Nav Toolbar
    NavButton m_hoveredNavBtn;
    NavButton m_pressedNavBtn;
    SidebarButton m_hoveredSidebarButton;

    // Shortcuts on homepage
    std::vector<ShortcutTile> m_shortcuts;
    int m_hoveredShortcutIndex;

    bool m_isTrackingMouse;
};

} // namespace Bloxcy
