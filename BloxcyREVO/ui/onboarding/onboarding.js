"use strict";

document.addEventListener("DOMContentLoaded", () => {
    const modes = document.querySelector("#mode-options");
    const status = document.querySelector("#setup-status");
    const finish = document.querySelector("#finish-setup");
    const preferences = window.BloxcyPreferences;
    preferences.populateModes(modes, "normal");

    modes.addEventListener("change", () => {
        const selected = modes.querySelector('input[name="browser-mode"]:checked');
        finish.textContent = `Continue with ${selected?.value || "Normal"}`;
        if (selected) {
            finish.textContent = `Continue with ${
                selected.value[0].toUpperCase() + selected.value.slice(1)
            }`;
        }
    });

    finish.addEventListener("click", () => {
        if (!window.chrome?.webview) {
            status.textContent = "Open this page in Bloxcy REVO to save your mode.";
            return;
        }
        const mode = preferences.selectedMode(modes);
        if (!mode) {
            status.textContent = "Choose a browser mode to continue.";
            return;
        }
        finish.disabled = true;
        status.textContent = "Saving your browser mode…";
        window.chrome.webview.postMessage(`onboarding:complete|${mode}`);
    });
});
