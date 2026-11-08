"use strict";

(() => {
    const modes = [
        {
            value: "wellness",
            label: "Wellness",
            description: "A calmer, lighter start page with mindful exercises and eye-break reminders."
        },
        {
            value: "gaming",
            label: "Gaming",
            description: "A vivid, full-featured browser with a bold background and quick access."
        },
        {
            value: "normal",
            label: "Normal",
            description: "A balanced, cleaner layout with everyday browser tools."
        },
        {
            value: "privacy",
            label: "Privacy",
            description: "A dark interface that clears cookies when you leave a tab."
        }
    ];

    function populateModes(container, selectedMode) {
        container.replaceChildren();
        const validMode = modes.some(({ value }) => value === selectedMode)
            ? selectedMode : "normal";
        for (const mode of modes) {
            const label = document.createElement("label");
            label.className = "mode-option";
            const input = document.createElement("input");
            input.type = "radio";
            input.name = "browser-mode";
            input.value = mode.value;
            input.checked = mode.value === validMode;
            const content = document.createElement("span");
            content.className = "mode-copy";
            const title = document.createElement("strong");
            title.textContent = mode.label;
            const description = document.createElement("span");
            description.textContent = mode.description;
            content.append(title, description);
            label.append(input, content);
            container.append(label);
        }
    }

    function selectedMode(container) {
        const selected = container.querySelector('input[name="browser-mode"]:checked');
        return selected ? selected.value : "";
    }

    window.BloxcyPreferences = Object.freeze({ populateModes, selectedMode });
})();
