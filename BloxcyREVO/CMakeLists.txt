# ============================================================================
# Project: Bloxcy REVO Browser
# File: CMakeLists.txt
# Description: CMake configuration for Bloxcy REVO Topless Window
# ============================================================================
# 
# PREREQUISITES & DEPENDENCIES:
# 1. Operating System: Windows 10 (Version 1809+) or Windows 11 (64-bit)
# 2. C++ Compiler: MSYS2 UCRT64 MinGW-w64 GCC
# 3. CMake 3.16+, SQLite development files, and the Microsoft Edge WebView2 Runtime
# 4. The WebView2 SDK headers and loader are in dependencies\WebView2
# 5. Windows system libraries: user32, gdi32, dwmapi, uxtheme, comctl32, bcrypt
# 6. Account passwords use PBKDF2-HMAC-SHA-256; saved website passwords are AES-GCM encrypted.
# 
# COMMAND LINE TO DOWNLOAD & INSTALL ALL DEPENDENCIES:
# ----------------------------------------------------------------------------
# Option 1 (Using Windows Package Manager - Recommended):
#   winget install MSYS2.MSYS2
#   pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-sqlite3
# 
# Option 2 (Visual Studio 2022 C++ Build Tools via winget):
#   winget install Microsoft.VisualStudio.2022.BuildTools --force --passive --includeRecommended
# 
# ----------------------------------------------------------------------------
# COMMAND LINE TO CONFIGURE & BUILD:
# ----------------------------------------------------------------------------
#   cmake -B build -S .
#   cmake --build build --config Release
#   .\build\Release\bloxcy_window.exe
# ============================================================================

cmake_minimum_required(VERSION 3.16)
project(BloxcyREVO VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(bloxcy_window WIN32
    src/accounts/account_store.h
    src/accounts/account_store.cpp
    "rendering engine/browser_host.h"
    "rendering engine/browser_host.cpp"
    src/window/window_manager.h
    src/window/window_manager.cpp
)

set(WEBVIEW2_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/WebView2")
if(NOT EXISTS "${WEBVIEW2_SDK_DIR}/include/WebView2.h")
    message(FATAL_ERROR "WebView2 SDK headers are missing. Run the dependency setup for this project.")
endif()

find_path(SQLITE3_INCLUDE_DIR sqlite3.h
    PATHS "C:/msys64/ucrt64/include"
)
find_file(SQLITE3_STATIC_LIBRARY NAMES libsqlite3.a
    PATHS "C:/msys64/ucrt64/lib"
)
if(NOT SQLITE3_STATIC_LIBRARY)
    find_library(SQLITE3_STATIC_LIBRARY NAMES sqlite3
        PATHS "C:/msys64/ucrt64/lib"
    )
endif()
if(NOT SQLITE3_INCLUDE_DIR OR NOT SQLITE3_STATIC_LIBRARY)
    message(FATAL_ERROR "SQLite development files are required to build the search database.")
endif()

target_include_directories(bloxcy_window PRIVATE
    "${WEBVIEW2_SDK_DIR}/include"
    "${SQLITE3_INCLUDE_DIR}"
)

target_link_libraries(bloxcy_window PRIVATE
    dwmapi
    gdi32
    user32
    uxtheme
    comctl32
    comdlg32
    shlwapi
    ole32
    uuid
    bcrypt
    windowscodecs
    msimg32
    "${SQLITE3_STATIC_LIBRARY}"
)

if(MSVC)
    target_compile_options(bloxcy_window PRIVATE /W4 /permissive-)
else()
    target_compile_options(bloxcy_window PRIVATE -Wall -Wextra -O2)
endif()

add_custom_command(TARGET bloxcy_window POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${WEBVIEW2_SDK_DIR}/x64/WebView2Loader.dll"
        "$<TARGET_FILE_DIR:bloxcy_window>/WebView2Loader.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/homepage"
        "$<TARGET_FILE_DIR:bloxcy_window>/ui/homepage"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/settings"
        "$<TARGET_FILE_DIR:bloxcy_window>/ui/settings"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/preferences"
        "$<TARGET_FILE_DIR:bloxcy_window>/ui/preferences"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/onboarding"
        "$<TARGET_FILE_DIR:bloxcy_window>/ui/onboarding"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/databases"
        "$<TARGET_FILE_DIR:bloxcy_window>/databases"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:bloxcy_window>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/src/apis"
        "$<TARGET_FILE_DIR:bloxcy_window>/src/apis"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/src/search_engine"
        "$<TARGET_FILE_DIR:bloxcy_window>/src/search_engine"
)

if(MINGW)
    target_link_options(bloxcy_window PRIVATE -static-libgcc -static-libstdc++)
    get_filename_component(MSYS2_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(RUNTIME_DLL libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
        if(EXISTS "${MSYS2_BIN_DIR}/${RUNTIME_DLL}")
            add_custom_command(TARGET bloxcy_window POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MSYS2_BIN_DIR}/${RUNTIME_DLL}"
                    "$<TARGET_FILE_DIR:bloxcy_window>/${RUNTIME_DLL}"
            )
        endif()
    endforeach()
    if(EXISTS "C:/msys64/ucrt64/bin/libsqlite3-0.dll")
        add_custom_command(TARGET bloxcy_window POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "C:/msys64/ucrt64/bin/libsqlite3-0.dll"
                "$<TARGET_FILE_DIR:bloxcy_window>/libsqlite3-0.dll"
        )
    endif()
endif()
