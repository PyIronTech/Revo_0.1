# Rendering engine

This folder contains Bloxcy REVO's WebView2 page renderer. `BrowserHost` owns the
WebView2 controller and page lifecycle used by the native window.

WebView2 embeds Microsoft Edge (Chromium), which provides the HTML/CSS renderer
(Blink) and JavaScript engine (V8). V8's optimizing compiler/JIT is already part
of the installed WebView2 Runtime and is managed by Chromium; this project does
not compile or link a second V8 runtime.

The browser UI hosts one WebView2 controller at a time, so navigation, the local
search results document, and page-level password-manager hooks are coordinated
by `BrowserHost`. Keep renderer-specific WebView2 code here; native window chrome
and tab-strip painting remain in `src/window`.

The default tab strip is horizontal. It displays the WebView2 page favicon when
the installed runtime exposes that API, with a site initial as a fallback.
WebView2's Chromium process sandbox remains enabled; native host objects are
disabled, website permission requests are denied, unsupported top-level schemes
are blocked, and pop-up windows are routed to validated HTTP(S) tabs. A native
sidebar provides Home, Search, Settings, and an Open file picker. Local files
can also be entered by absolute path or `file:///` URL; only the selected file
is permitted for top-level file navigation, and the address bar marks it with
`(!FILE)`. The settings menu persists sidebar visibility/width and compact-tab
preferences in the app's local settings.

Bloxcy search results load the bundled `src/search_engine/results.html` page;
the native SQLite search sends its result data to that page over the WebView2
message bridge after the results page registers its listener and signals ready.
The native toolbar moon button
toggles best-effort forced-dark styling for the active website and reapplies it
after page navigation.

The new-tab page stores shortcuts, layout, scratchpad notes, theme preferences,
and an optimized wallpaper in the account's WebView2 profile. Optional
background matching samples the image's most common colors for the page and
native tab palette. The default Bloxcy search uses the local Tranco top-1m
database, offers typo-tolerant domain matching, and can display dictionary
definitions for word and definition-style queries. The homepage search-engine
selector also supports Google, Bing, DuckDuckGo, Yahoo, Brave, Ecosia,
Startpage, Qwant, Mojeek, Kagi, Swisscows, YouTube, and Wikipedia.

The local Images and Videos result tabs use the Python providers in `src/apis`.
Create a project environment with `python -m venv .venv` and install the
requirements with `.venv\Scripts\python.exe -m pip install -r requirements.txt`.
The app checks that local environment and Python on `PATH`, or accepts
`BLOXCY_PYTHON` set to the Python executable.
The app starts the provider only when Bloxcy results are requested. It binds to
127.0.0.1 on an ephemeral port, requires a per-process token for result requests,
and stops the helper when the app exits. Image search combines five results
from Wikimedia Commons with five from Openverse per page, preserves duplicates,
shows creator/license details when supplied, and reports a provider failure
without silently substituting data. YouTube metadata uses `yt-dlp` without
downloading video files; available English transcripts are used to rank results.
The results page requests data only after it has registered its WebView2 message
listener. Provider errors are shown in the results page. The Python providers
and dictionary helper have focused tests.

On first launch of a local browser profile, a setup page asks which experience
the user expects: Wellness, Gaming, Normal, or Privacy. The selected mode is
stored in that profile's `preferences.ini` and can be changed in Settings.
Profiles that completed the former profile/goals setup but do not yet have a
saved browser mode are prompted once to choose a mode.

Wellness uses `assets/calm.jpg`, a lighter homepage palette, a locally generated
ambient sound, guided mindful exercises, and a 15-minute eye-break reminder. It
filters requests to a built-in list of common advertising and tracking domains;
this is not a comprehensive content blocker. Gaming uses `assets/normal.png`.
Normal uses the same wallpaper with a more minimal homepage. Privacy uses a dark
homepage and starts clearing cookies when switching tabs. Cookie deletion is
asynchronous and applies to the shared WebView2 profile, not separate per-tab
cookie stores; it does not provide anonymity or remove other browsing data.
Website notifications remain blocked in every mode.

The local search results page uses a compact responsive layout for web, image,
video, and dictionary results. Its local web results come from the Tranco
database; image and video providers are loaded on demand, and provider errors
are shown in the page.

The address bar's Trust signals badge summarizes HTTPS, local Tranco popularity,
known tracker domains observed in page resources, and simple accessibility/
layout hints. Click the score for its measured inputs. It is a transparent
heuristic only; it is not a security verdict or a guarantee that a site is safe.
