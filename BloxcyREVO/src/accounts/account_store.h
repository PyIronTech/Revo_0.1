#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <array>
#include <string>
#include <vector>

namespace Bloxcy {

struct AccountSession {
    std::wstring id;
    std::wstring username;
    std::wstring profileDirectory;
    std::array<BYTE, 32> vaultKey{};

    AccountSession() = default;
    ~AccountSession();
    AccountSession(const AccountSession&) = delete;
    AccountSession& operator=(const AccountSession&) = delete;
    AccountSession(AccountSession&& other) noexcept;
    AccountSession& operator=(AccountSession&& other) noexcept;
};

struct SavedCredential {
    std::wstring username;
    std::wstring password;
};

struct SavedBookmark {
    std::wstring title;
    std::wstring url;
};

class AccountStore {
public:
    bool Authenticate(HWND owner, AccountSession& session);
    bool GetDefaultSession(AccountSession& session, std::wstring& error);
    bool CreateAccount(const std::wstring& username, const std::wstring& password,
                       AccountSession& session, std::wstring& error);
    bool SignIn(const std::wstring& username, const std::wstring& password,
                AccountSession& session, std::wstring& error);
    bool RecordSearch(const AccountSession& session, const std::wstring& query,
                      std::wstring& error) const;
    bool LoadSearchHistory(const AccountSession& session, std::vector<std::wstring>& queries,
                           std::wstring& error) const;
    bool ClearSearchHistory(const AccountSession& session, std::wstring& error) const;
    bool SaveBookmark(const AccountSession& session, const std::wstring& title,
                      const std::wstring& url, std::wstring& error) const;
    bool LoadBookmarks(const AccountSession& session, std::vector<SavedBookmark>& bookmarks,
                       std::wstring& error) const;
    bool DeleteBookmark(const AccountSession& session, const std::wstring& url,
                        std::wstring& error) const;
    bool SavePassword(const AccountSession& session, const std::wstring& origin,
                      const std::wstring& username, const std::wstring& password,
                      std::wstring& error) const;
    bool LoadPasswords(const AccountSession& session, const std::wstring& origin,
                       std::vector<SavedCredential>& credentials,
                       std::wstring& error) const;
};

}
