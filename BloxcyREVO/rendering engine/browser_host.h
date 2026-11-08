#pragma once

#include "../src/accounts/account_store.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <memory>
#include <string>

struct ICoreWebView2;
struct ICoreWebView2Controller;
struct ICoreWebView2Environment;
struct sqlite3;

namespace Bloxcy {

constexpr UINT kBrowserStateChangedMessage = WM_APP + 20;
constexpr UINT kFocusAddressMessage = WM_APP + 21;
constexpr UINT kNewTabMessage = WM_APP + 22;
constexpr UINT kNavigateAddressMessage = WM_APP + 23;
constexpr UINT kCancelAddressMessage = WM_APP + 24;
constexpr UINT kSetThemeMessage = WM_APP + 25;
constexpr UINT kBrowserMemoryEstimateMessage = WM_APP + 26;
constexpr UINT kRequestMemoryEstimateMessage = WM_APP + 27;
constexpr UINT kSetCustomPaletteMessage = WM_APP + 28;
constexpr UINT kOpenUrlMessage = WM_APP + 29;
constexpr UINT kBrowserFaviconMessage = WM_APP + 30;
constexpr UINT kBrowserDataClearedMessage = WM_APP + 31;
constexpr UINT kSetBrowserModeMessage = WM_APP + 32;

constexpr int kBrowserModeWellness = 0;
constexpr int kBrowserModeGaming = 1;
constexpr int kBrowserModeNormal = 2;
constexpr int kBrowserModePrivacy = 3;

struct BrowserFaviconUpdate {
    std::wstring url;
    HBITMAP bitmap;
};

class BrowserHost : public std::enable_shared_from_this<BrowserHost> {
public:
    BrowserHost();
    ~BrowserHost();

    BrowserHost(const BrowserHost&) = delete;
    BrowserHost& operator=(const BrowserHost&) = delete;

    bool Initialize(HWND parent, const std::wstring& appDirectory, const AccountSession& session);
    void Shutdown();
    void SetBounds(const RECT& bounds);
    void Navigate(const std::wstring& url);
    void NavigateHome();
    void NavigateSettings();
    void Hibernate();
    void NavigateOrSearch(const std::wstring& input);
    void Search(const std::wstring& query, const std::wstring& engine = L"bloxcy");
    void SetForceDark(bool enabled);
    bool IsForceDarkEnabled() const { return m_forceDark; }
    void Back();
    void Forward();
    void Reload();
    void OpenDevTools();
    void SetTheme(const std::wstring& themeName);
    void ResetZoom();
    void ChangeZoom(double delta);
    void ShowKeyboardShortcuts() const;
    void SpeakPage();
    void RequestMemoryEstimate();
    void RequestFavicon();
    bool ClearBrowsingData(unsigned int dataKinds);
    void ClearCookiesForPrivacyMode();

    std::wstring GetCurrentUrl() const;
    std::wstring GetDocumentTitle() const;
    bool IsHomePage() const { return m_isHomePage; }
    bool IsSearchPage() const { return m_isSearchPage; }
    bool IsSettingsPage() const { return m_isSettingsPage; }
    const std::wstring& GetLastSearchQuery() const { return m_lastSearchQuery; }
    int GetTrustScore() const { return m_trustScore; }
    const std::wstring& GetTrustDetails() const { return m_trustDetails; }
    bool IsPrivacyMode() const { return m_browserMode == L"privacy"; }
    const std::wstring& GetBrowserMode() const { return m_browserMode; }

private:
    bool InitializeSearchDatabase(const std::wstring& appDirectory);
    void CreateController(ICoreWebView2Environment* environment);
    void OnWebMessage(const std::wstring& message, const std::wstring& source);
    void HandlePasswordMessage(const std::wstring& message, const std::wstring& source);
    void NavigateOnboardingPage();
    bool SaveBrowserMode(const std::wstring& message, bool completeOnboarding);
    void NotifyBrowserModeChanged(bool applyNativePreset = false);
    bool ClearBrowsingDataInternal(unsigned int dataKinds, bool notify);
    void EvaluateTrustSignals();
    int LookupTrancoRank(const std::wstring& host, std::wstring& error) const;
    void FillSavedPasswords(const std::wstring& origin);
    void HandleEnvironmentError(HRESULT error);
    void HandleControllerError(HRESULT error);
    void ShowError(const wchar_t* action, HRESULT error) const;
    void NavigateHomePage();
    bool EnsureSearchBackend(std::wstring& error);

    HWND m_parent;
    HMODULE m_loaderModule;
    ICoreWebView2Environment* m_environment;
    ICoreWebView2Controller* m_controller;
    ICoreWebView2* m_webView;
    sqlite3* m_database;
    HANDLE m_searchBackendProcess;
    unsigned short m_searchBackendPort;
    std::wstring m_searchBackendToken;
    AccountStore m_accountStore;
    AccountSession m_account;
    std::wstring m_homeUri;
    std::wstring m_settingsUri;
    std::wstring m_resultsUri;
    std::wstring m_onboardingUri;
    std::wstring m_preferencesPath;
    std::wstring m_pendingResultsJson;
    std::wstring m_pendingUrl;
    std::wstring m_allowedFilePath;
    std::wstring m_lastSearchQuery;
    std::wstring m_themeName;
    std::wstring m_preferenceGoals;
    std::wstring m_browserMode;
    std::wstring m_trustDetails;
    int m_trustScore;
    bool m_isHomePage;
    bool m_isSearchPage;
    bool m_isSettingsPage;
    bool m_isOnboardingPage;
    bool m_isHibernating;
    bool m_pendingNavigate;
    bool m_pendingHome;
    bool m_pendingSearch;
    bool m_forceDark;
    bool m_onboardingCompleted;
    bool m_comInitialized;
    bool m_shutdown;
};

} // namespace Bloxcy
