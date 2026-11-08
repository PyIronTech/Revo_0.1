@echo off
setlocal

echo ===================================================
echo   Building Bloxcy REVO Browser
echo ===================================================

if not exist "dependencies\WebView2\include\WebView2.h" (
    echo [ERROR] WebView2 SDK headers are missing.
    exit /b 1
)

if not exist "C:\msys64\ucrt64\bin\g++.exe" (
    echo [ERROR] MSYS2 UCRT64 GCC is required at C:\msys64\ucrt64.
    exit /b 1
)

if not exist "C:\msys64\ucrt64\include\sqlite3.h" (
    echo [ERROR] SQLite development headers are missing from MSYS2 UCRT64.
    exit /b 1
)

if not exist "ui\settings\index.html" (
    echo [ERROR] The dedicated settings page is missing.
    exit /b 1
)
if not exist "ui\onboarding\index.html" (
    echo [ERROR] The first-run preferences page is missing.
    exit /b 1
)
if not exist "ui\preferences\preferences.js" (
    echo [ERROR] The shared browser preferences UI is missing.
    exit /b 1
)

set "PATH=C:\msys64\ucrt64\bin;%PATH%"
g++.exe -std=c++17 -Wall -Wextra -O2 -static-libgcc -static-libstdc++ ^
    -Idependencies\WebView2\include -IC:\msys64\ucrt64\include ^
    src\accounts\account_store.cpp src\window\window_manager.cpp "rendering engine\browser_host.cpp" ^
    -o bloxcy_window.exe -mwindows ^
    -LC:\msys64\ucrt64\lib -Wl,-Bstatic -lsqlite3 -Wl,-Bdynamic ^
    -ldwmapi -lgdi32 -luser32 -luxtheme -lcomctl32 -lcomdlg32 -lshlwapi -lole32 -luuid -lbcrypt -lwindowscodecs -lmsimg32
if errorlevel 1 (
    echo [ERROR] Compilation failed.
    exit /b 1
)

copy /Y "dependencies\WebView2\x64\WebView2Loader.dll" "WebView2Loader.dll" >nul
if errorlevel 1 (
    echo [ERROR] Could not copy WebView2Loader.dll beside the app.
    exit /b 1
)

for %%D in (libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll) do (
    if exist "C:\msys64\ucrt64\bin\%%D" copy /Y "C:\msys64\ucrt64\bin\%%D" "%%D" >nul
)

if not exist "src\search_engine" (
    echo [ERROR] The search results page files are missing.
    exit /b 1
)
if not exist "src\apis\search_server.py" (
    echo [ERROR] The search provider backend files are missing.
    exit /b 1
)

echo [SUCCESS] Build completed: bloxcy_window.exe
start "" "bloxcy_window.exe"
exit /b 0
