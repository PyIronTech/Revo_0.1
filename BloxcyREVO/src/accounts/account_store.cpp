#include "account_store.h"

#include <bcrypt.h>
#include <sqlite3.h>
#include <algorithm>
#include <cwchar>
#include <memory>
#include <utility>

namespace Bloxcy {
namespace {

constexpr ULONGLONG kPasswordHashIterations = 600000;
constexpr ULONG kSaltSize = 16;
constexpr ULONG kDerivedKeySize = 64;
constexpr ULONG kGcmNonceSize = 12;
constexpr ULONG kGcmTagSize = 16;

enum AuthControlId {
    kUsernameInput = 1,
    kPasswordInput,
    kConfirmPasswordInput,
    kAuthStatus,
    kAuthSubmit,
    kAuthToggle
};

struct DialogState {
    AccountStore* store;
    AccountSession* session;
    HWND owner;
    HWND usernameInput;
    HWND passwordInput;
    HWND confirmPasswordInput;
    HWND confirmPasswordLabel;
    HWND status;
    HWND submit;
    HWND toggle;
    bool signUp;
    bool done;
    bool authenticated;
};

struct SqliteCloser {
    void operator()(sqlite3* database) const {
        if (database) sqlite3_close(database);
    }
};

using SqliteDatabase = std::unique_ptr<sqlite3, SqliteCloser>;

void SecureClear(std::vector<BYTE>& bytes) {
    if (!bytes.empty()) SecureZeroMemory(bytes.data(), bytes.size());
    bytes.clear();
}

void SecureClear(std::array<BYTE, kDerivedKeySize>& bytes) {
    SecureZeroMemory(bytes.data(), bytes.size());
}

void SecureClearCredentials(std::vector<SavedCredential>& credentials) {
    for (auto& credential : credentials) {
        if (!credential.password.empty()) {
            SecureZeroMemory(credential.password.data(),
                             credential.password.size() * sizeof(wchar_t));
        }
    }
    credentials.clear();
}

std::wstring GetDataRoot() {
    DWORD length = GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
    if (length == 0) return {};
    std::wstring path(length, L'\0');
    const DWORD written = GetEnvironmentVariableW(L"APPDATA", path.data(), length);
    if (written == 0 || written >= length) return {};
    path.resize(written);
    path += L"\\BloxcyREVO";
    if (!CreateDirectoryW(path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return {};
    return path;
}

bool EnsureDirectory(const std::wstring& path) {
    return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

std::wstring ToWide(const char* text) {
    if (!text) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    if (length <= 0) return L"";
    std::wstring result(static_cast<size_t>(length), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, result.data(), length)) return L"";
    result.resize(static_cast<size_t>(length - 1));
    return result;
}

std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                           static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<size_t>(length), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                             static_cast<int>(text.size()), result.data(), length, nullptr, nullptr)) {
        return {};
    }
    return result;
}

bool OpenDatabase(const std::wstring& path, SqliteDatabase& database, std::wstring& error) {
    sqlite3* raw = nullptr;
    const int result = sqlite3_open16(path.c_str(), &raw);
    database.reset(raw);
    if (result != SQLITE_OK) {
        error = raw ? ToWide(sqlite3_errmsg(raw)) : L"Could not open the local account database.";
        return false;
    }
    sqlite3_busy_timeout(raw, 5000);
    return true;
}

bool ExecuteSql(sqlite3* database, const char* sql, std::wstring& error) {
    char* message = nullptr;
    const int result = sqlite3_exec(database, sql, nullptr, nullptr, &message);
    if (result == SQLITE_OK) return true;
    error = ToWide(message ? message : sqlite3_errmsg(database));
    sqlite3_free(message);
    return false;
}

bool OpenAccountsDatabase(SqliteDatabase& database, std::wstring& error) {
    const std::wstring root = GetDataRoot();
    if (root.empty()) {
        error = L"Could not access your AppData folder.";
        return false;
    }
    if (!EnsureDirectory(root + L"\\profiles")) {
        error = L"Could not create the account profile folder.";
        return false;
    }
    if (!OpenDatabase(root + L"\\accounts.sqlite3", database, error)) return false;
    return ExecuteSql(database.get(),
        "CREATE TABLE IF NOT EXISTS accounts ("
        "id TEXT PRIMARY KEY, username TEXT NOT NULL COLLATE NOCASE UNIQUE, "
        "salt BLOB NOT NULL, password_hash BLOB NOT NULL, "
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);",
        error);
}

bool InitializeProfile(const std::wstring& profileDirectory, std::wstring& error) {
    if (!EnsureDirectory(profileDirectory)) {
        error = L"Could not create the account's private profile folder.";
        return false;
    }

    const std::wstring searchPath = profileDirectory + L"\\search_history.sqlite3";
    SqliteDatabase searchDatabase;
    if (!OpenDatabase(searchPath, searchDatabase, error) ||
        !ExecuteSql(searchDatabase.get(),
            "CREATE TABLE IF NOT EXISTS search_history ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, query TEXT NOT NULL, "
            "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);"
            "CREATE TABLE IF NOT EXISTS bookmarks ("
            "url TEXT PRIMARY KEY, title TEXT NOT NULL, "
            "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);",
            error)) {
        return false;
    }
    searchDatabase.reset();

    const std::wstring passwordsPath = profileDirectory + L"\\passwords.sqlite3";
    SqliteDatabase passwordDatabase;
    if (!OpenDatabase(passwordsPath, passwordDatabase, error)) return false;
    return ExecuteSql(passwordDatabase.get(),
        "PRAGMA secure_delete=ON;"
        "CREATE TABLE IF NOT EXISTS passwords ("
        "origin TEXT NOT NULL, username TEXT NOT NULL, nonce BLOB NOT NULL, "
        "ciphertext BLOB NOT NULL, tag BLOB NOT NULL, "
        "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, "
        "PRIMARY KEY(origin, username));",
        error);
}

std::wstring MakeAccountId() {
    std::array<BYTE, 16> randomBytes{};
    if (BCryptGenRandom(nullptr, randomBytes.data(), static_cast<ULONG>(randomBytes.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        SecureZeroMemory(randomBytes.data(), randomBytes.size());
        return {};
    }
    static constexpr wchar_t hex[] = L"0123456789abcdef";
    std::wstring id;
    id.reserve(randomBytes.size() * 2);
    for (BYTE value : randomBytes) {
        id += hex[value >> 4];
        id += hex[value & 0x0f];
    }
    SecureZeroMemory(randomBytes.data(), randomBytes.size());
    return id;
}

bool IsValidUsername(const std::wstring& username) {
    if (username.size() < 3 || username.size() > 32) return false;
    return std::all_of(username.begin(), username.end(), [](wchar_t character) {
        return (character >= L'a' && character <= L'z') ||
               (character >= L'A' && character <= L'Z') ||
               (character >= L'0' && character <= L'9') ||
               character == L'.' || character == L'_' || character == L'-';
    });
}

bool DeriveKeys(const std::wstring& password, const BYTE* salt,
                std::array<BYTE, kDerivedKeySize>& keys, std::wstring& error) {
    std::string passwordBytes = ToUtf8(password);
    if (passwordBytes.empty()) {
        error = L"The password contains invalid text.";
        return false;
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                                   nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (status >= 0) {
        status = BCryptDeriveKeyPBKDF2(
            algorithm,
            reinterpret_cast<PUCHAR>(passwordBytes.data()),
            static_cast<ULONG>(passwordBytes.size()),
            const_cast<PUCHAR>(salt),
            kSaltSize,
            kPasswordHashIterations,
            keys.data(),
            static_cast<ULONG>(keys.size()),
            0);
    }
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    SecureZeroMemory(passwordBytes.data(), passwordBytes.size());
    if (status < 0) {
        SecureClear(keys);
        error = L"Windows could not derive the account encryption keys.";
        return false;
    }
    return true;
}

bool ConstantTimeEquals(const BYTE* left, const BYTE* right, size_t length) {
    BYTE difference = 0;
    for (size_t index = 0; index < length; ++index) difference |= left[index] ^ right[index];
    return difference == 0;
}

void RemoveEmptyProfile(const std::wstring& directory) {
    DeleteFileW((directory + L"\\search_history.sqlite3").c_str());
    DeleteFileW((directory + L"\\passwords.sqlite3").c_str());
    RemoveDirectoryW(directory.c_str());
}

bool Utf8Bytes(const std::wstring& value, std::vector<BYTE>& bytes) {
    const std::string encoded = ToUtf8(value);
    if (encoded.empty()) return value.empty();
    bytes.assign(encoded.begin(), encoded.end());
    return true;
}

bool SetAesKey(BCRYPT_ALG_HANDLE& algorithm, BCRYPT_KEY_HANDLE& key,
               std::vector<BYTE>& keyObject, const std::array<BYTE, 32>& rawKey,
               std::wstring& error) {
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (status < 0) {
        error = L"Windows could not initialize password encryption.";
        return false;
    }
    status = BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
        static_cast<ULONG>(sizeof(BCRYPT_CHAIN_MODE_GCM)), 0);
    ULONG objectSize = 0;
    ULONG resultSize = 0;
    if (status >= 0) {
        status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                                   reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize),
                                   &resultSize, 0);
    }
    if (status >= 0) {
        keyObject.resize(objectSize);
        status = BCryptGenerateSymmetricKey(algorithm, &key, keyObject.data(), objectSize,
                                            const_cast<PUCHAR>(rawKey.data()),
                                            static_cast<ULONG>(rawKey.size()), 0);
    }
    if (status < 0) {
        if (key) BCryptDestroyKey(key);
        key = nullptr;
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        algorithm = nullptr;
        SecureClear(keyObject);
        error = L"Windows could not initialize password encryption.";
        return false;
    }
    return true;
}

std::vector<BYTE> MakeAssociatedData(const std::wstring& origin, const std::wstring& username) {
    std::vector<BYTE> result;
    if (!Utf8Bytes(origin, result)) return {};
    result.push_back(0);
    std::vector<BYTE> usernameBytes;
    if (!Utf8Bytes(username, usernameBytes)) return {};
    result.insert(result.end(), usernameBytes.begin(), usernameBytes.end());
    SecureClear(usernameBytes);
    return result;
}

bool EncryptPassword(const std::array<BYTE, 32>& rawKey, const std::wstring& origin,
                     const std::wstring& username, const std::wstring& password,
                     std::vector<BYTE>& nonce, std::vector<BYTE>& ciphertext,
                     std::vector<BYTE>& tag, std::wstring& error) {
    std::vector<BYTE> plaintext;
    if (!Utf8Bytes(password, plaintext) || plaintext.empty()) {
        error = L"The website password is empty or contains invalid text.";
        return false;
    }
    nonce.resize(kGcmNonceSize);
    tag.resize(kGcmTagSize);
    if (BCryptGenRandom(nullptr, nonce.data(), kGcmNonceSize, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        SecureClear(plaintext);
        error = L"Windows could not generate a secure password-encryption nonce.";
        return false;
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    std::vector<BYTE> keyObject;
    bool success = SetAesKey(algorithm, key, keyObject, rawKey, error);
    std::vector<BYTE> associatedData = MakeAssociatedData(origin, username);
    if (success && associatedData.empty()) {
        error = L"Could not encode the password encryption metadata.";
        success = false;
    }
    if (success) {
        ciphertext.resize(plaintext.size());
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
        BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
        authInfo.pbNonce = nonce.data();
        authInfo.cbNonce = static_cast<ULONG>(nonce.size());
        authInfo.pbAuthData = associatedData.data();
        authInfo.cbAuthData = static_cast<ULONG>(associatedData.size());
        authInfo.pbTag = tag.data();
        authInfo.cbTag = static_cast<ULONG>(tag.size());
        ULONG written = 0;
        const NTSTATUS status = BCryptEncrypt(key, plaintext.data(),
            static_cast<ULONG>(plaintext.size()), &authInfo, nullptr, 0,
            ciphertext.data(), static_cast<ULONG>(ciphertext.size()), &written, 0);
        if (status < 0 || written != ciphertext.size()) {
            success = false;
            error = L"Windows could not encrypt the saved website password.";
        }
    }
    if (key) BCryptDestroyKey(key);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    SecureClear(keyObject);
    SecureClear(plaintext);
    SecureClear(associatedData);
    if (!success) SecureClear(ciphertext);
    return success;
}

bool DecryptPassword(const std::array<BYTE, 32>& rawKey, const std::wstring& origin,
                     const std::wstring& username, const BYTE* nonce, ULONG nonceSize,
                     const BYTE* ciphertext, ULONG ciphertextSize, const BYTE* tag,
                     ULONG tagSize, std::wstring& password, std::wstring& error) {
    if (nonceSize != kGcmNonceSize || tagSize != kGcmTagSize || !ciphertext || ciphertextSize == 0) {
        error = L"A saved password entry has invalid encrypted data.";
        return false;
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    std::vector<BYTE> keyObject;
    bool success = SetAesKey(algorithm, key, keyObject, rawKey, error);
    std::vector<BYTE> associatedData = MakeAssociatedData(origin, username);
    std::vector<BYTE> plaintext(ciphertextSize);
    if (success && associatedData.empty()) {
        error = L"Could not encode the password encryption metadata.";
        success = false;
    }
    if (success) {
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
        BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
        authInfo.pbNonce = const_cast<PUCHAR>(nonce);
        authInfo.cbNonce = nonceSize;
        authInfo.pbAuthData = associatedData.data();
        authInfo.cbAuthData = static_cast<ULONG>(associatedData.size());
        authInfo.pbTag = const_cast<PUCHAR>(tag);
        authInfo.cbTag = tagSize;
        ULONG written = 0;
        const NTSTATUS status = BCryptDecrypt(key, const_cast<PUCHAR>(ciphertext), ciphertextSize,
            &authInfo, nullptr, 0, plaintext.data(), static_cast<ULONG>(plaintext.size()),
            &written, 0);
        if (status < 0 || written != plaintext.size()) {
            success = false;
            error = L"Could not decrypt a saved website password. The data may have been modified.";
        } else {
            const int wideLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                reinterpret_cast<const char*>(plaintext.data()), static_cast<int>(plaintext.size()),
                nullptr, 0);
            if (wideLength <= 0) {
                success = false;
                error = L"A saved website password contains invalid text.";
            } else {
                password.resize(static_cast<size_t>(wideLength));
                if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                    reinterpret_cast<const char*>(plaintext.data()), static_cast<int>(plaintext.size()),
                    password.data(), wideLength)) {
                    success = false;
                    password.clear();
                    error = L"A saved website password contains invalid text.";
                }
            }
        }
    }
    if (key) BCryptDestroyKey(key);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    SecureClear(keyObject);
    SecureClear(associatedData);
    SecureClear(plaintext);
    return success;
}

void UpdateDialogMode(DialogState* state) {
    SetWindowTextW(GetParent(state->submit), state->signUp ?
        L"Bloxcy REVO - Create account" : L"Bloxcy REVO - Sign in");
    SetWindowTextW(state->submit, state->signUp ? L"Create account" : L"Sign in");
    SetWindowTextW(state->toggle, state->signUp ? L"Already have an account? Sign in" :
                                                   L"New here? Create an account");
    ShowWindow(state->confirmPasswordInput, state->signUp ? SW_SHOW : SW_HIDE);
    ShowWindow(state->confirmPasswordLabel, state->signUp ? SW_SHOW : SW_HIDE);
    SetWindowTextW(state->status, state->signUp ?
        L"Use 3-32 letters, numbers, dots, underscores or hyphens for your username." : L"");
    SetWindowTextW(state->passwordInput, L"");
    SetWindowTextW(state->confirmPasswordInput, L"");
}

void SetControlFont(HWND control) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
}

LRESULT CALLBACK AuthWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<DialogState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) return DefWindowProcW(window, message, wParam, lParam);

    switch (message) {
        case WM_CREATE: {
            state->usernameInput = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                24, 76, 350, 28, window, reinterpret_cast<HMENU>(kUsernameInput),
                nullptr, nullptr);
            state->passwordInput = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL,
                24, 130, 350, 28, window, reinterpret_cast<HMENU>(kPasswordInput),
                nullptr, nullptr);
            state->confirmPasswordInput = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL,
                24, 184, 350, 28, window, reinterpret_cast<HMENU>(kConfirmPasswordInput),
                nullptr, nullptr);
            state->status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                24, 220, 350, 44, window, reinterpret_cast<HMENU>(kAuthStatus), nullptr, nullptr);
            state->submit = CreateWindowExW(0, L"BUTTON", L"Sign in",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                24, 270, 350, 34, window, reinterpret_cast<HMENU>(kAuthSubmit), nullptr, nullptr);
            state->toggle = CreateWindowExW(0, L"BUTTON", L"New here? Create an account",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_FLAT,
                24, 312, 350, 28, window, reinterpret_cast<HMENU>(kAuthToggle), nullptr, nullptr);
            HWND usernameLabel = CreateWindowExW(0, L"STATIC", L"Username",
                WS_CHILD | WS_VISIBLE, 24, 54, 350, 20, window, nullptr, nullptr, nullptr);
            HWND passwordLabel = CreateWindowExW(0, L"STATIC", L"Password",
                WS_CHILD | WS_VISIBLE, 24, 108, 350, 20, window, nullptr, nullptr, nullptr);
            state->confirmPasswordLabel = CreateWindowExW(0, L"STATIC", L"Confirm password",
                WS_CHILD | WS_VISIBLE, 24, 162, 350, 20, window, nullptr, nullptr, nullptr);
            HWND heading = CreateWindowExW(0, L"STATIC", L"Your private browser profile",
                WS_CHILD | WS_VISIBLE, 24, 20, 350, 26, window, nullptr, nullptr, nullptr);
            const HWND controls[] = { state->usernameInput, state->passwordInput,
                state->confirmPasswordInput, state->status, state->submit, state->toggle,
                usernameLabel, passwordLabel, state->confirmPasswordLabel, heading };
            for (HWND control : controls) SetControlFont(control);
            SendMessageW(state->usernameInput, EM_SETLIMITTEXT, 32, 0);
            SendMessageW(state->passwordInput, EM_SETLIMITTEXT, 128, 0);
            SendMessageW(state->confirmPasswordInput, EM_SETLIMITTEXT, 128, 0);
            UpdateDialogMode(state);
            SetFocus(state->usernameInput);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == kAuthToggle) {
                state->signUp = !state->signUp;
                UpdateDialogMode(state);
                return 0;
            }
            if (LOWORD(wParam) == kAuthSubmit) {
                const int usernameLength = GetWindowTextLengthW(state->usernameInput);
                const int passwordLength = GetWindowTextLengthW(state->passwordInput);
                std::wstring username(static_cast<size_t>(usernameLength) + 1, L'\0');
                std::wstring password(static_cast<size_t>(passwordLength) + 1, L'\0');
                GetWindowTextW(state->usernameInput, username.data(), usernameLength + 1);
                GetWindowTextW(state->passwordInput, password.data(), passwordLength + 1);
                username.resize(static_cast<size_t>(usernameLength));
                password.resize(static_cast<size_t>(passwordLength));

                std::wstring error;
                bool success = false;
                if (state->signUp) {
                    const int confirmLength = GetWindowTextLengthW(state->confirmPasswordInput);
                    std::wstring confirmation(static_cast<size_t>(confirmLength) + 1, L'\0');
                    GetWindowTextW(state->confirmPasswordInput, confirmation.data(), confirmLength + 1);
                    confirmation.resize(static_cast<size_t>(confirmLength));
                    if (password != confirmation) {
                        error = L"Those passwords do not match.";
                    } else if (password.size() < 12 || password.size() > 128) {
                        error = L"Choose a password between 12 and 128 characters.";
                    } else {
                        success = state->store->CreateAccount(username, password, *state->session, error);
                    }
                    if (!confirmation.empty()) SecureZeroMemory(confirmation.data(),
                        confirmation.size() * sizeof(wchar_t));
                } else {
                    success = state->store->SignIn(username, password, *state->session, error);
                }
                if (!password.empty()) SecureZeroMemory(password.data(), password.size() * sizeof(wchar_t));

                if (success) {
                    state->authenticated = true;
                    state->done = true;
                    SetWindowTextW(state->passwordInput, L"");
                    SetWindowTextW(state->confirmPasswordInput, L"");
                    DestroyWindow(window);
                } else {
                    SetWindowTextW(state->status, error.c_str());
                    SetWindowTextW(state->passwordInput, L"");
                    SetWindowTextW(state->confirmPasswordInput, L"");
                    SetFocus(state->passwordInput);
                }
                return 0;
            }
            break;
        case WM_CLOSE:
            state->done = true;
            SetWindowTextW(state->passwordInput, L"");
            SetWindowTextW(state->confirmPasswordInput, L"");
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            SetWindowTextW(state->passwordInput, L"");
            SetWindowTextW(state->confirmPasswordInput, L"");
            state->done = true;
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool RegisterAuthWindowClass(HINSTANCE instance) {
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = AuthWindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = L"BloxcyREVOAccountDialog";
    if (RegisterClassExW(&windowClass)) return true;
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

} // namespace

AccountSession::~AccountSession() {
    SecureZeroMemory(vaultKey.data(), vaultKey.size());
}

AccountSession::AccountSession(AccountSession&& other) noexcept
    : id(std::move(other.id))
    , username(std::move(other.username))
    , profileDirectory(std::move(other.profileDirectory))
    , vaultKey(other.vaultKey) {
    SecureZeroMemory(other.vaultKey.data(), other.vaultKey.size());
}

AccountSession& AccountSession::operator=(AccountSession&& other) noexcept {
    if (this != &other) {
        SecureZeroMemory(vaultKey.data(), vaultKey.size());
        id = std::move(other.id);
        username = std::move(other.username);
        profileDirectory = std::move(other.profileDirectory);
        vaultKey = other.vaultKey;
        SecureZeroMemory(other.vaultKey.data(), other.vaultKey.size());
    }
    return *this;
}

bool AccountStore::GetDefaultSession(AccountSession& session, std::wstring& error) {
    const std::wstring root = GetDataRoot();
    if (root.empty()) {
        error = L"Could not access your AppData folder.";
        return false;
    }
    const std::wstring profileDirectory = root + L"\\Aprofiles\\default";
    if (!InitializeProfile(profileDirectory, error)) {
        return false;
    }
    AccountSession defaultSession;
    defaultSession.id = L"default";
    defaultSession.username = L"Local User";
    defaultSession.profileDirectory = profileDirectory;
    session = std::move(defaultSession);
    return true;
}

bool AccountStore::Authenticate(HWND owner, AccountSession& session) {
    SqliteDatabase database;
    std::wstring error;
    if (!OpenAccountsDatabase(database, error)) {
        MessageBoxW(owner, error.c_str(), L"Bloxcy REVO Accounts", MB_OK | MB_ICONERROR);
        return false;
    }
    database.reset();

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!RegisterAuthWindowClass(instance)) {
        MessageBoxW(owner, L"Could not register the account sign-in dialog.",
                    L"Bloxcy REVO Accounts", MB_OK | MB_ICONERROR);
        return false;
    }

    DialogState state = {};
    state.store = this;
    state.session = &session;
    state.owner = owner;
    state.signUp = false;
    state.done = false;
    state.authenticated = false;
    if (owner) EnableWindow(owner, FALSE);
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, L"BloxcyREVOAccountDialog",
        L"Bloxcy REVO - Sign in",
        WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 414, 390, owner, nullptr, instance, &state);
    if (!window) {
        if (owner) EnableWindow(owner, TRUE);
        MessageBoxW(owner, L"Could not open the account sign-in dialog.",
                    L"Bloxcy REVO Accounts", MB_OK | MB_ICONERROR);
        return false;
    }

    RECT dialogRect = {};
    GetWindowRect(window, &dialogRect);
    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(window, nullptr, (screenWidth - (dialogRect.right - dialogRect.left)) / 2,
                 (screenHeight - (dialogRect.bottom - dialogRect.top)) / 2,
                 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    MSG message = {};
    while (!state.done) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            state.done = true;
            break;
        }
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (owner) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    return state.authenticated;
}

bool AccountStore::CreateAccount(const std::wstring& username, const std::wstring& password,
                                 AccountSession& session, std::wstring& error) {
    if (!IsValidUsername(username)) {
        error = L"Username must be 3-32 characters: letters, numbers, dot, underscore or hyphen.";
        return false;
    }
    if (password.size() < 12 || password.size() > 128) {
        error = L"Choose a password between 12 and 128 characters.";
        return false;
    }

    SqliteDatabase database;
    if (!OpenAccountsDatabase(database, error)) return false;

    std::array<BYTE, kSaltSize> salt{};
    if (BCryptGenRandom(nullptr, salt.data(), static_cast<ULONG>(salt.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        SecureZeroMemory(salt.data(), salt.size());
        error = L"Windows could not generate a secure account salt.";
        return false;
    }
    std::array<BYTE, kDerivedKeySize> keys{};
    if (!DeriveKeys(password, salt.data(), keys, error)) {
        SecureZeroMemory(salt.data(), salt.size());
        return false;
    }

    const std::wstring id = MakeAccountId();
    const std::wstring root = GetDataRoot();
    if (id.empty() || root.empty()) {
        SecureClear(keys);
        SecureZeroMemory(salt.data(), salt.size());
        error = L"Could not create a secure account profile.";
        return false;
    }
    const std::wstring profileDirectory = root + L"\\profiles\\" + id;
    if (!InitializeProfile(profileDirectory, error)) {
        RemoveEmptyProfile(profileDirectory);
        SecureClear(keys);
        SecureZeroMemory(salt.data(), salt.size());
        return false;
    }

    sqlite3_stmt* statement = nullptr;
    int result = sqlite3_prepare_v2(database.get(),
        "INSERT INTO accounts(id, username, salt, password_hash) VALUES(?1, ?2, ?3, ?4);",
        -1, &statement, nullptr);
    if (result == SQLITE_OK) {
        const std::string usernameUtf8 = ToUtf8(username);
        sqlite3_bind_text16(statement, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 2, usernameUtf8.c_str(),
                          static_cast<int>(usernameUtf8.size()), SQLITE_TRANSIENT);
        sqlite3_bind_blob(statement, 3, salt.data(), static_cast<int>(salt.size()), SQLITE_TRANSIENT);
        sqlite3_bind_blob(statement, 4, keys.data(), 32, SQLITE_TRANSIENT);
        result = sqlite3_step(statement);
    }
    if (statement) sqlite3_finalize(statement);
    SecureZeroMemory(salt.data(), salt.size());
    if (result != SQLITE_DONE) {
        RemoveEmptyProfile(profileDirectory);
        SecureClear(keys);
        error = result == SQLITE_CONSTRAINT ?
            L"That username is already registered." : ToWide(sqlite3_errmsg(database.get()));
        return false;
    }

    AccountSession authenticated;
    authenticated.id = id;
    authenticated.username = username;
    authenticated.profileDirectory = profileDirectory;
    std::copy(keys.begin() + 32, keys.end(), authenticated.vaultKey.begin());
    SecureClear(keys);
    session = std::move(authenticated);
    return true;
}

bool AccountStore::SignIn(const std::wstring& username, const std::wstring& password,
                          AccountSession& session, std::wstring& error) {
    SqliteDatabase database;
    if (!OpenAccountsDatabase(database, error)) return false;

    sqlite3_stmt* statement = nullptr;
    int result = sqlite3_prepare_v2(database.get(),
        "SELECT id, salt, password_hash FROM accounts WHERE username = ?1 COLLATE NOCASE;",
        -1, &statement, nullptr);
    if (result != SQLITE_OK) {
        error = ToWide(sqlite3_errmsg(database.get()));
        return false;
    }
    const std::string usernameUtf8 = ToUtf8(username);
    sqlite3_bind_text(statement, 1, usernameUtf8.c_str(),
                      static_cast<int>(usernameUtf8.size()), SQLITE_TRANSIENT);
    result = sqlite3_step(statement);
    if (result != SQLITE_ROW) {
        sqlite3_finalize(statement);
        error = L"Username or password is incorrect.";
        return false;
    }
    const auto* idText = static_cast<const wchar_t*>(sqlite3_column_text16(statement, 0));
    const auto* salt = static_cast<const BYTE*>(sqlite3_column_blob(statement, 1));
    const int saltSize = sqlite3_column_bytes(statement, 1);
    const auto* storedHash = static_cast<const BYTE*>(sqlite3_column_blob(statement, 2));
    const int hashSize = sqlite3_column_bytes(statement, 2);
    const std::wstring id = idText ? idText : L"";
    std::array<BYTE, kSaltSize> saltCopy{};
    std::array<BYTE, 32> hashCopy{};
    if (salt && saltSize == static_cast<int>(saltCopy.size())) {
        std::copy(salt, salt + saltSize, saltCopy.begin());
    }
    if (storedHash && hashSize == static_cast<int>(hashCopy.size())) {
        std::copy(storedHash, storedHash + hashSize, hashCopy.begin());
    }
    sqlite3_finalize(statement);
    if (id.empty() || saltSize != static_cast<int>(saltCopy.size()) ||
        hashSize != static_cast<int>(hashCopy.size())) {
        SecureZeroMemory(saltCopy.data(), saltCopy.size());
        SecureZeroMemory(hashCopy.data(), hashCopy.size());
        error = L"The account record is invalid. Contact support before continuing.";
        return false;
    }

    std::array<BYTE, kDerivedKeySize> keys{};
    if (!DeriveKeys(password, saltCopy.data(), keys, error)) {
        SecureZeroMemory(saltCopy.data(), saltCopy.size());
        SecureZeroMemory(hashCopy.data(), hashCopy.size());
        return false;
    }
    const bool validPassword = ConstantTimeEquals(keys.data(), hashCopy.data(), hashCopy.size());
    SecureZeroMemory(saltCopy.data(), saltCopy.size());
    SecureZeroMemory(hashCopy.data(), hashCopy.size());
    if (!validPassword) {
        SecureClear(keys);
        error = L"Username or password is incorrect.";
        return false;
    }

    const std::wstring root = GetDataRoot();
    const std::wstring profileDirectory = root + L"\\profiles\\" + id;
    const DWORD attributes = GetFileAttributesW(profileDirectory.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        SecureClear(keys);
        error = L"The account profile folder is missing.";
        return false;
    }

    AccountSession authenticated;
    authenticated.id = id;
    authenticated.username = username;
    authenticated.profileDirectory = profileDirectory;
    std::copy(keys.begin() + 32, keys.end(), authenticated.vaultKey.begin());
    SecureClear(keys);
    session = std::move(authenticated);
    return true;
}

bool AccountStore::RecordSearch(const AccountSession& session, const std::wstring& query,
                                std::wstring& error) const {
    SqliteDatabase database;
    if (!OpenDatabase(session.profileDirectory + L"\\search_history.sqlite3", database, error)) return false;
    sqlite3_stmt* statement = nullptr;
    const int result = sqlite3_prepare_v2(database.get(),
        "INSERT INTO search_history(query) VALUES(?1);", -1, &statement, nullptr);
    if (result != SQLITE_OK) {
        error = ToWide(sqlite3_errmsg(database.get()));
        return false;
    }
    sqlite3_bind_text16(statement, 1, query.c_str(), -1, SQLITE_TRANSIENT);
    const int stepResult = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (stepResult != SQLITE_DONE) {
        error = ToWide(sqlite3_errmsg(database.get()));
        return false;
    }
    return true;
}

bool AccountStore::LoadSearchHistory(const AccountSession& session,
                                     std::vector<std::wstring>& queries,
                                     std::wstring& error) const {
    queries.clear();
    SqliteDatabase database;
    if (!OpenDatabase(session.profileDirectory + L"\\search_history.sqlite3", database, error)) {
        return false;
    }
    if (!ExecuteSql(database.get(),
        "CREATE TABLE IF NOT EXISTS bookmarks ("
        "url TEXT PRIMARY KEY, title TEXT NOT NULL, "
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);", error)) {
        return false;
    }
    sqlite3_stmt* statement = nullptr;
    const int result = sqlite3_prepare_v2(database.get(),
        "SELECT query FROM search_history ORDER BY id DESC LIMIT 20;", -1, &statement, nullptr);
    if (result != SQLITE_OK) {
        error = ToWide(sqlite3_errmsg(database.get()));
        return false;
    }
    int stepResult = SQLITE_ROW;
    while ((stepResult = sqlite3_step(statement)) == SQLITE_ROW) {
        const auto* query = static_cast<const wchar_t*>(sqlite3_column_text16(statement, 0));
        if (query) queries.emplace_back(query);
    }
    sqlite3_finalize(statement);
    if (stepResult != SQLITE_DONE) {
        error = ToWide(sqlite3_errmsg(database.get()));
        return false;
    }
    return true;
}

bool AccountStore::ClearSearchHistory(const AccountSession& session, std::wstring& error) const {
    SqliteDatabase database;
    if (!OpenDatabase(session.profileDirectory + L"\\search_history.sqlite3", database, error)) {
        return false;
    }
    return ExecuteSql(database.get(),
        "PRAGMA secure_delete=ON; DELETE FROM search_history;", error);
}

bool AccountStore::SaveBookmark(const AccountSession& session, const std::wstring& title,
                                const std::wstring& url, std::wstring& error) const {
    if (title.empty() || url.empty() || url.size() > 4096 ||
        (url.compare(0, 7, L"http://") != 0 && url.compare(0, 8, L"https://") != 0 &&
         url.compare(0, 8, L"file:///") != 0)) {
        error = L"Only HTTP, HTTPS, and local-file pages can be bookmarked.";
        return false;
    }
    SqliteDatabase database;
    if (!OpenDatabase(session.profileDirectory + L"\\search_history.sqlite3", database, error)) {
        return false;
    }
    if (!ExecuteSql(database.get(),
        "CREATE TABLE IF NOT EXISTS bookmarks ("
        "url TEXT PRIMARY KEY, title TEXT NOT NULL, "
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);", error)) {
        return false;
    }
    sqlite3_stmt* statement = nullptr;
    const int result = sqlite3_prepare_v2(database.get(),
        "INSERT INTO bookmarks(title, url) VALUES(?1, ?2) "
        "ON CONFLICT(url) DO UPDATE SET title=excluded.title;",
        -1, &statement, nullptr);
    if (result != SQLITE_OK) {
        error = ToWide(sqlite3_errmsg(database.get()));
        return false;
    }
    sqlite3_bind_text16(statement, 1, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(statement, 2, url.c_str(), -1, SQLITE_TRANSIENT);
    const int stepResult = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (stepResult != SQLITE_DONE) {
        error = ToWide(sqlite3_errmsg(database.get()));
        return false;
    }
    return true;
}

bool AccountStore::LoadBookmarks(const AccountSession& session,
                                 std::vector<SavedBookmark>& bookmarks,
                                 std::wstring& error) const {
    bookmarks.clear();
    SqliteDatabase database;
    if (!OpenDatabase(session.profileDirectory + L"\\search_history.sqlite3", database, error)) {
        return false;
    }
    if (!ExecuteSql(database.get(),
        "CREATE TABLE IF NOT EXISTS bookmarks ("
        "url TEXT PRIMARY KEY, title TEXT NOT NULL, "
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);", error)) {
        return false;
    }
    sqlite3_stmt* statement = nullptr;
    const int result = sqlite3_prepare_v2(database.get(),
        "SELECT title, url FROM bookmarks ORDER BY created_at DESC LIMIT 100;",
        -1, &statement, nullptr);
    if (result != SQLITE_OK) {
        error = ToWide(sqlite3_errmsg(database.get()));
        return false;
    }
    int stepResult = SQLITE_ROW;
    while ((stepResult = sqlite3_step(statement)) == SQLITE_ROW) {
        const auto* title = static_cast<const wchar_t*>(sqlite3_column_text16(statement, 0));
        const auto* url = static_cast<const wchar_t*>(sqlite3_column_text16(statement, 1));
        if (title && url) bookmarks.push_back({ title, url });
    }
    sqlite3_finalize(statement);
    if (stepResult != SQLITE_DONE) {
        error = ToWide(sqlite3_errmsg(database.get()));
        return false;
    }
    return true;
}

bool AccountStore::DeleteBookmark(const AccountSession& session, const std::wstring& url,
                                  std::wstring& error) const {
    SqliteDatabase database;
    if (!OpenDatabase(session.profileDirectory + L"\\search_history.sqlite3", database, error)) {
        return false;
    }
    if (!ExecuteSql(database.get(),
        "CREATE TABLE IF NOT EXISTS bookmarks ("
        "url TEXT PRIMARY KEY, title TEXT NOT NULL, "
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);", error)) {
        return false;
    }
    sqlite3_stmt* statement = nullptr;
    const int result = sqlite3_prepare_v2(database.get(),
        "DELETE FROM bookmarks WHERE url=?1;", -1, &statement, nullptr);
    if (result != SQLITE_OK) {
        error = ToWide(sqlite3_errmsg(database.get()));
        return false;
    }
    sqlite3_bind_text16(statement, 1, url.c_str(), -1, SQLITE_TRANSIENT);
    const int stepResult = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (stepResult != SQLITE_DONE) {
        error = ToWide(sqlite3_errmsg(database.get()));
        return false;
    }
    return true;
}

bool AccountStore::SavePassword(const AccountSession& session, const std::wstring& origin,
                                const std::wstring& username, const std::wstring& password,
                                std::wstring& error) const {
    if (origin.compare(0, 8, L"https://") != 0 || username.empty() || password.empty()) {
        error = L"Only non-empty credentials from secure HTTPS pages can be saved.";
        return false;
    }
    std::vector<BYTE> nonce, ciphertext, tag;
    if (!EncryptPassword(session.vaultKey, origin, username, password,
                         nonce, ciphertext, tag, error)) return false;

    SqliteDatabase database;
    if (!OpenDatabase(session.profileDirectory + L"\\passwords.sqlite3", database, error)) {
        SecureClear(ciphertext);
        return false;
    }
    sqlite3_stmt* statement = nullptr;
    const int result = sqlite3_prepare_v2(database.get(),
        "INSERT INTO passwords(origin, username, nonce, ciphertext, tag) "
        "VALUES(?1, ?2, ?3, ?4, ?5) "
        "ON CONFLICT(origin, username) DO UPDATE SET nonce=excluded.nonce, "
        "ciphertext=excluded.ciphertext, tag=excluded.tag, "
        "updated_at=CURRENT_TIMESTAMP;",
        -1, &statement, nullptr);
    if (result != SQLITE_OK) {
        error = ToWide(sqlite3_errmsg(database.get()));
        SecureClear(ciphertext);
        return false;
    }
    sqlite3_bind_text16(statement, 1, origin.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(statement, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(statement, 3, nonce.data(), static_cast<int>(nonce.size()), SQLITE_TRANSIENT);
    sqlite3_bind_blob(statement, 4, ciphertext.data(), static_cast<int>(ciphertext.size()), SQLITE_TRANSIENT);
    sqlite3_bind_blob(statement, 5, tag.data(), static_cast<int>(tag.size()), SQLITE_TRANSIENT);
    const int stepResult = sqlite3_step(statement);
    sqlite3_finalize(statement);
    SecureClear(ciphertext);
    if (stepResult != SQLITE_DONE) {
        error = ToWide(sqlite3_errmsg(database.get()));
        return false;
    }
    return true;
}

bool AccountStore::LoadPasswords(const AccountSession& session, const std::wstring& origin,
                                 std::vector<SavedCredential>& credentials,
                                 std::wstring& error) const {
    SecureClearCredentials(credentials);
    SqliteDatabase database;
    if (!OpenDatabase(session.profileDirectory + L"\\passwords.sqlite3", database, error)) return false;
    sqlite3_stmt* statement = nullptr;
    const int result = sqlite3_prepare_v2(database.get(),
        "SELECT username, nonce, ciphertext, tag FROM passwords WHERE origin=?1 "
        "ORDER BY updated_at DESC;", -1, &statement, nullptr);
    if (result != SQLITE_OK) {
        error = ToWide(sqlite3_errmsg(database.get()));
        return false;
    }
    sqlite3_bind_text16(statement, 1, origin.c_str(), -1, SQLITE_TRANSIENT);
    int stepResult = SQLITE_ROW;
    while ((stepResult = sqlite3_step(statement)) == SQLITE_ROW) {
        const auto* username = static_cast<const wchar_t*>(sqlite3_column_text16(statement, 0));
        const auto* nonce = static_cast<const BYTE*>(sqlite3_column_blob(statement, 1));
        const auto* ciphertext = static_cast<const BYTE*>(sqlite3_column_blob(statement, 2));
        const auto* tag = static_cast<const BYTE*>(sqlite3_column_blob(statement, 3));
        std::wstring password;
        if (!DecryptPassword(session.vaultKey, origin, username ? username : L"",
                nonce, static_cast<ULONG>(sqlite3_column_bytes(statement, 1)),
                ciphertext, static_cast<ULONG>(sqlite3_column_bytes(statement, 2)),
                tag, static_cast<ULONG>(sqlite3_column_bytes(statement, 3)),
                password, error)) {
            sqlite3_finalize(statement);
            SecureClearCredentials(credentials);
            return false;
        }
        credentials.push_back({ username ? username : L"", std::move(password) });
    }
    sqlite3_finalize(statement);
    if (stepResult != SQLITE_DONE) {
        error = ToWide(sqlite3_errmsg(database.get()));
        SecureClearCredentials(credentials);
        return false;
    }
    return true;
}

} // namespace Bloxcy
