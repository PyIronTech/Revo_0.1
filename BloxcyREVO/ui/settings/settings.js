document.addEventListener("DOMContentLoaded", () => {
    const status = document.querySelector("#settings-status");
    const memoryValue = document.querySelector("#memory-value");
    const profileUsername = document.querySelector("#profile-username");
    const profilePath = document.querySelector("#profile-path");
    const browserModeOptions = document.querySelector("#browser-mode-options");
    const choices = document.querySelectorAll("[data-theme-choice]");

    const setTheme = (theme) => {
        if (!["obsidian", "light", "ocean", "forest"].includes(theme)) return;
        document.documentElement.dataset.theme = theme;
        for (const button of choices) {
            button.setAttribute("aria-pressed", String(button.dataset.themeChoice === theme));
        }
    };

    const setBrowserMode = (mode) => {
        const valid = ["wellness", "gaming", "normal", "privacy"].includes(mode)
            ? mode : "normal";
        document.documentElement.dataset.browserMode = valid;
        window.BloxcyPreferences.populateModes(browserModeOptions, valid);
    };

    for (const button of choices) {
        button.addEventListener("click", () => {
            const theme = button.dataset.themeChoice;
            setTheme(theme);
            if (window.chrome?.webview) {
                window.chrome.webview.postMessage(`settings:theme:${theme}`);
                status.textContent = `${button.textContent.trim()} theme selected.`;
            }
        });
    }

    document.querySelector("#refresh-memory").addEventListener("click", () => {
        if (window.chrome?.webview) {
            window.chrome.webview.postMessage("settings:refresh-memory");
            memoryValue.textContent = "Refreshing…";
        }
    });

    document.querySelector("#save-browser-mode").addEventListener("click", () => {
        if (!window.chrome?.webview) {
            status.textContent = "Browser modes can be saved inside the desktop app.";
            return;
        }
        const mode = window.BloxcyPreferences.selectedMode(browserModeOptions);
        if (!mode) {
            status.textContent = "Choose a browser mode first.";
            return;
        }
        window.chrome.webview.postMessage(`settings:mode:${mode}`);
        status.textContent = "Applying browser mode…";
    });

    if (window.chrome?.webview) {
        window.chrome.webview.addEventListener("message", (event) => {
            const data = event.data;
            if (data === "settings:mode-saved") {
                status.textContent = "Browser mode saved to this profile.";
                return;
            }
            if (typeof data === "string" &&
                (data.startsWith("set-theme:") || data.startsWith("native-theme:"))) {
                const prefix = data.startsWith("native-theme:") ? "native-theme:" : "set-theme:";
                setTheme(data.slice(prefix.length));
                return;
            }
            if (typeof data === "string" && data.startsWith("browser-mode:")) {
                setBrowserMode(data.slice("browser-mode:".length));
                return;
            }
            if (!data || typeof data !== "object") return;
            if (data.type === "settings-data") {
                setTheme(data.theme);
                profileUsername.textContent = data.username;
                profilePath.textContent = data.profile;
                profilePath.title = data.profile;
                setBrowserMode(data.browserMode);
            }
            if (data.type === "memory-estimate") {
                memoryValue.textContent = Number.isFinite(data.bytes) && data.bytes > 0
                    ? `${(data.bytes / (1024 * 1024)).toFixed(1)} MB JS heap`
                    : "Unavailable for this page";
            }
        });
    } else {
        status.textContent = "Preview mode — browser settings are available in the desktop app.";
    }
});
