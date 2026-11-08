document.addEventListener("DOMContentLoaded", () => {
    const form = document.querySelector("#search-form");
    const input = document.querySelector("#search-input");
    const engineSelect = document.querySelector("#search-engine");
    const badge = document.querySelector(".search-badge");
    const shortcutNav = document.querySelector(".shortcuts");
    const dialog = document.querySelector("#customize-dialog");
    const scratchpad = document.querySelector("#scratchpad");
    const scratchpadStatus = document.querySelector("#scratchpad-status");
    const dictionaryResult = document.querySelector("#dictionary-result");
    const wellnessTools = document.querySelector("#wellness-tools");
    const soundToggle = document.querySelector("#sound-toggle");
    const exerciseDialog = document.querySelector("#exercise-dialog");
    const webview = window.chrome && window.chrome.webview;
    let soundContext = null;
    let soundSource = null;
    const keys = {
        shortcuts: "bloxcy.shortcuts.v1",
        scratchpad: "bloxcy.scratchpad.v1",
        theme: "bloxcy.theme.v1",
        layout: "bloxcy.layout.v1",
        visibility: "bloxcy.visibility.v1",
        background: "bloxcy.background.v1",
        backgroundColors: "bloxcy.background-colors.v1",
        matchBackground: "bloxcy.match-background.v1",
        searchEngine: "bloxcy.search-engine.v1",
        browserMode: "bloxcy.browser-mode.v1"
    };
    const defaultShortcuts = [
        { name: "Wikipedia", url: "https://www.wikipedia.org" },
        { name: "YouTube", url: "https://www.youtube.com" },
        { name: "GitHub", url: "https://github.com" },
        { name: "Discord", url: "https://discord.com" },
        { name: "Figma", url: "https://www.figma.com" },
        { name: "Notion", url: "https://www.notion.so" }
    ];

    const readStored = (key, fallback) => {
        try {
            const value = localStorage.getItem(key);
            return value === null ? fallback : JSON.parse(value);
        } catch {
            return fallback;
        }
    };
    const store = (key, value) => {
        try {
            localStorage.setItem(key, JSON.stringify(value));
            return true;
        } catch {
            return false;
        }
    };
    const isHttpUrl = (value) => {
        try {
            const parsed = new URL(value);
            return (parsed.protocol === "https:" || parsed.protocol === "http:") &&
                parsed.hostname.length > 0;
        } catch {
            return false;
        }
    };
    const engineSearchUrl = (engine, query) => {
        const encoded = encodeURIComponent(query);
        const engines = {
            google: `https://www.google.com/search?q=${encoded}`,
            bing: `https://www.bing.com/search?q=${encoded}`,
            duckduckgo: `https://duckduckgo.com/?q=${encoded}`,
            yahoo: `https://search.yahoo.com/search?p=${encoded}`,
            brave: `https://search.brave.com/search?q=${encoded}`,
            ecosia: `https://www.ecosia.org/search?q=${encoded}`,
            startpage: `https://www.startpage.com/sp/search?query=${encoded}`,
            qwant: `https://www.qwant.com/?q=${encoded}`,
            mojeek: `https://www.mojeek.com/search?q=${encoded}`,
            kagi: `https://kagi.com/search?q=${encoded}`,
            swisscows: `https://swisscows.com/en/web?query=${encoded}`,
            youtube: `https://www.youtube.com/results?search_query=${encoded}`,
            wikipedia: `https://en.wikipedia.org/w/index.php?search=${encoded}`
        };
        return engines[engine] || `https://www.google.com/search?q=${encoded}`;
    };

    const setTheme = (theme, notifyNative = false) => {
        const valid = ["obsidian", "light", "ocean", "forest"];
        if (!valid.includes(theme)) theme = "obsidian";
        document.documentElement.dataset.theme = theme;
        document.querySelector("#theme-choice").value = theme;
        store(keys.theme, theme);
        if (notifyNative) {
            localStorage.setItem(keys.matchBackground, "false");
            document.querySelector("#match-background").checked = false;
            document.documentElement.style.removeProperty("--custom-bg");
            document.documentElement.style.removeProperty("--custom-accent");
            document.documentElement.style.removeProperty("--neon-cyan");
            document.documentElement.style.removeProperty("--border-focus");
            if (webview) {
                webview.postMessage("homepage:reset-colors");
                webview.postMessage(`homepage:theme:${theme}`);
            }
        }
    };

    const setBrowserMode = (mode) => {
        const validModes = ["wellness", "gaming", "normal", "privacy"];
        if (!validModes.includes(mode)) mode = "normal";
        document.documentElement.dataset.browserMode = mode;
        document.body.classList.toggle("mode-wellness", mode === "wellness");
        document.body.classList.toggle("mode-normal", mode === "normal");
        document.body.classList.toggle("has-mode-background",
            ["wellness", "gaming", "normal"].includes(mode));
        wellnessTools.hidden = mode !== "wellness";
        store(keys.browserMode, mode);
        if (mode !== "wellness" && soundSource) stopAmbientSound();
    };

    const stopAmbientSound = () => {
        if (soundSource) {
            soundSource.stop();
            soundSource.disconnect();
            soundSource = null;
        }
        if (soundContext) {
            soundContext.close();
            soundContext = null;
        }
        soundToggle.setAttribute("aria-pressed", "false");
        soundToggle.textContent = "Play calming sound";
    };

    const startAmbientSound = async () => {
        const AudioContextClass = window.AudioContext || window.webkitAudioContext;
        if (!AudioContextClass) throw new Error("Ambient sound is not supported in this runtime.");
        soundContext = new AudioContextClass();
        await soundContext.resume();
        const frameCount = soundContext.sampleRate * 4;
        const buffer = soundContext.createBuffer(1, frameCount, soundContext.sampleRate);
        const samples = buffer.getChannelData(0);
        let filtered = 0;
        for (let index = 0; index < frameCount; index += 1) {
            const white = Math.random() * 2 - 1;
            filtered = (filtered + 0.025 * white) / 1.025;
            samples[index] = filtered * 3.2;
        }
        soundSource = soundContext.createBufferSource();
        soundSource.buffer = buffer;
        soundSource.loop = true;
        const lowPass = soundContext.createBiquadFilter();
        lowPass.type = "lowpass";
        lowPass.frequency.value = 420;
        const gain = soundContext.createGain();
        gain.gain.value = 0.045;
        soundSource.connect(lowPass).connect(gain).connect(soundContext.destination);
        soundSource.start();
        soundToggle.setAttribute("aria-pressed", "true");
        soundToggle.textContent = "Stop calming sound";
    };

    soundToggle.addEventListener("click", async () => {
        if (soundSource) {
            stopAmbientSound();
            return;
        }
        soundToggle.disabled = true;
        try {
            await startAmbientSound();
        } catch (error) {
            stopAmbientSound();
            scratchpadStatus.textContent = error.message || "Could not start ambient sound.";
        } finally {
            soundToggle.disabled = false;
        }
    });

    document.querySelector("#open-exercises").addEventListener("click", () => {
        exerciseDialog.showModal();
    });
    const exerciseDetails = document.querySelector("#exercise-details");
    const exerciseGuides = {
        breathing: ["Slow breathing", "Breathe in gently for 4 seconds and out for 6. Repeat for one minute without forcing your breath."],
        box: ["Box breathing", "Breathe in for 4 seconds, hold comfortably for 4, breathe out for 4, then pause for 4. Repeat only while comfortable."],
        neck: ["Gentle neck reset", "Sit tall, relax your shoulders, and slowly turn your head left and right within a comfortable range. Do not pull or force the stretch."],
        eyes: ["20-20-20 eye break", "Look at something about 20 feet away for 20 seconds. Blink slowly and relax your focus."]
    };
    for (const button of exerciseDialog.querySelectorAll("[data-exercise]")) {
        button.addEventListener("click", () => {
            const guide = exerciseGuides[button.dataset.exercise];
            if (!guide) return;
            const title = document.createElement("strong");
            title.textContent = guide[0];
            const instruction = document.createElement("p");
            instruction.textContent = guide[1];
            exerciseDetails.replaceChildren(title, instruction);
        });
    }

    const applyMatchedColors = () => {
        const matched = localStorage.getItem(keys.matchBackground) === "true";
        const colors = readStored(keys.backgroundColors, null);
        const root = document.documentElement;
        if (!matched || !colors || !/^#[0-9a-f]{6}$/i.test(colors.background) ||
            !/^#[0-9a-f]{6}$/i.test(colors.accent)) {
            root.style.removeProperty("--custom-bg");
            root.style.removeProperty("--custom-accent");
            if (webview) webview.postMessage("homepage:reset-colors");
            return;
        }
        root.style.setProperty("--custom-bg", colors.background);
        root.style.setProperty("--custom-accent", colors.accent);
        root.style.setProperty("--neon-cyan", colors.accent);
        root.style.setProperty("--border-focus", `${colors.accent}66`);
        if (webview) webview.postMessage(`homepage:colors:${colors.background},${colors.accent}`);
    };

    const applyBackground = () => {
        const background = localStorage.getItem(keys.background);
        document.body.classList.toggle("has-background", Boolean(background));
        if (background && background.startsWith("data:image/")) {
            document.body.style.setProperty("--wallpaper", `url("${background}")`);
        } else {
            document.body.style.removeProperty("--wallpaper");
        }
        applyMatchedColors();
    };

    const renderShortcuts = () => {
        const stored = readStored(keys.shortcuts, defaultShortcuts);
        const shortcuts = Array.isArray(stored) ? stored.filter((item) =>
            item && typeof item.name === "string" && typeof item.url === "string" &&
            isHttpUrl(item.url)).slice(0, 12) : defaultShortcuts;
        shortcutNav.replaceChildren();
        for (const item of shortcuts) {
            const wrapper = document.createElement("div");
            wrapper.className = "shortcut-entry";
            const link = document.createElement("a");
            link.className = "shortcut-item";
            link.href = item.url;
            link.setAttribute("aria-label", `Open ${item.name}`);
            link.title = item.url;

            const tile = document.createElement("span");
            tile.className = "shortcut-tile";
            const icon = document.createElement("img");
            icon.className = "shortcut-icon";
            icon.alt = "";
            icon.loading = "lazy";
            icon.referrerPolicy = "no-referrer";
            icon.src = new URL("/favicon.ico", item.url).href;
            icon.addEventListener("error", () => {
                icon.remove();
                const fallback = document.createElement("span");
                fallback.className = "shortcut-fallback";
                fallback.textContent = item.name.trim().slice(0, 1).toUpperCase();
                tile.append(fallback);
            }, { once: true });
            tile.append(icon);
            const label = document.createElement("span");
            label.className = "shortcut-label";
            label.textContent = item.name;
            link.append(tile, label);
            link.addEventListener("click", (event) => {
                if (!webview) return;
                event.preventDefault();
                webview.postMessage(`open-url:${item.url}`);
            });
            const remove = document.createElement("button");
            remove.className = "remove-shortcut";
            remove.type = "button";
            remove.textContent = "\u00d7";
            remove.setAttribute("aria-label", `Remove ${item.name} shortcut`);
            remove.addEventListener("click", () => {
                const next = shortcuts.filter((shortcut) => shortcut !== item);
                if (!store(keys.shortcuts, next)) {
                    scratchpadStatus.textContent = "Could not save your shortcut changes.";
                    return;
                }
                renderShortcuts();
            });
            wrapper.append(link, remove);
            shortcutNav.append(wrapper);
        }
    };

    const speak = (text) => {
        if (!("speechSynthesis" in window)) {
            scratchpadStatus.textContent = "Text-to-speech is not available in this runtime.";
            return;
        }
        const content = text.trim().slice(0, 12000);
        if (!content) {
            scratchpadStatus.textContent = "There is no text to read yet.";
            return;
        }
        window.speechSynthesis.cancel();
        window.speechSynthesis.speak(new SpeechSynthesisUtterance(content));
    };

    const addShortcut = () => {
        const name = window.prompt("Shortcut name (up to 24 characters):");
        if (name === null) return;
        const url = window.prompt("Website address (http or https):");
        if (url === null) return;
        const normalizedUrl = /^https?:\/\//i.test(url.trim()) ? url.trim() : `https://${url.trim()}`;
        if (!name.trim() || name.trim().length > 24 || normalizedUrl.length > 2048 ||
            !isHttpUrl(normalizedUrl)) {
            scratchpadStatus.textContent = "Enter a short name and a valid HTTP or HTTPS address.";
            return;
        }
        const current = readStored(keys.shortcuts, defaultShortcuts);
        const shortcuts = Array.isArray(current) ? current.slice(0, 11) : defaultShortcuts.slice(0, 11);
        shortcuts.push({ name: name.trim(), url: normalizedUrl });
        if (!store(keys.shortcuts, shortcuts)) {
            scratchpadStatus.textContent = "Could not save this shortcut in the browser profile.";
            return;
        }
        renderShortcuts();
    };

    const updateVisibility = () => {
        const visibility = readStored(keys.visibility, {
            shortcuts: true, scratchpad: true, dictionary: true
        });
        const nodes = {
            shortcuts: shortcutNav,
            scratchpad: document.querySelector("#scratchpad-card"),
            dictionary: document.querySelector("#dictionary-card")
        };
        for (const [name, node] of Object.entries(nodes)) {
            const show = visibility[name] !== false;
            node.hidden = !show;
            const checkbox = document.querySelector(`[data-visibility="${name}"]`);
            checkbox.checked = show;
        }
    };

    const extractColors = (sourceCanvas) => {
        const canvas = document.createElement("canvas");
        canvas.width = 48;
        canvas.height = 48;
        const context = canvas.getContext("2d", { willReadFrequently: true });
        if (!context) throw new Error("Image color matching is unavailable.");
        context.drawImage(sourceCanvas, 0, 0, canvas.width, canvas.height);
        const pixels = context.getImageData(0, 0, canvas.width, canvas.height).data;
        const counts = new Map();
        for (let index = 0; index < pixels.length; index += 4) {
            if (pixels[index + 3] < 128) continue;
            const channels = [pixels[index], pixels[index + 1], pixels[index + 2]]
                .map((channel) => Math.round(channel / 17) * 17);
            const hex = `#${channels.map((channel) => channel.toString(16).padStart(2, "0")).join("")}`;
            counts.set(hex, (counts.get(hex) || 0) + 1);
        }
        return [...counts.entries()].sort((first, second) => second[1] - first[1])
            .slice(0, 2).map(([color]) => color);
    };

    document.querySelector("#search-form").addEventListener("submit", (event) => {
        event.preventDefault();
        const query = input.value.trim();
        if (!query) {
            input.focus();
            return;
        }
        if (webview) {
            webview.postMessage(`search:${engineSelect.value}:${query}`);
            return;
        }
        if (isHttpUrl(query)) {
            window.location.href = query;
        } else {
            window.location.href = engineSearchUrl(
                engineSelect.value === "bloxcy" ? "google" : engineSelect.value,
                query
            );
        }
    });
    engineSelect.addEventListener("change", () => {
        store(keys.searchEngine, engineSelect.value);
    });

    document.querySelector("#dictionary-form").addEventListener("submit", async (event) => {
        event.preventDefault();
        const word = document.querySelector("#dictionary-word").value.trim();
        if (!/^[A-Za-z][A-Za-z'-]{0,79}$/.test(word)) {
            dictionaryResult.textContent = "Enter one word using letters, apostrophes, or hyphens.";
            return;
        }
        dictionaryResult.textContent = "Looking up the definition…";
        const controller = new AbortController();
        const timeout = window.setTimeout(() => controller.abort(), 8000);
        try {
            const response = await fetch(
                `https://api.dictionaryapi.dev/api/v2/entries/en/${encodeURIComponent(word)}`,
                { headers: { Accept: "application/json" }, signal: controller.signal }
            );
            if (response.status === 404) {
                dictionaryResult.textContent = `No definition found for “${word}”.`;
                return;
            }
            if (!response.ok) throw new Error(`Dictionary service returned HTTP ${response.status}.`);
            const entries = await response.json();
            if (!Array.isArray(entries)) throw new Error("The dictionary returned an unexpected response.");
            dictionaryResult.replaceChildren();
            let shown = 0;
            for (const entry of entries) {
                const heading = document.createElement("h3");
                heading.textContent = typeof entry.word === "string" ? entry.word : word;
                dictionaryResult.append(heading);
                for (const meaning of Array.isArray(entry.meanings) ? entry.meanings : []) {
                    for (const definition of Array.isArray(meaning.definitions) ? meaning.definitions : []) {
                        if (shown >= 5 || typeof definition.definition !== "string") break;
                        const item = document.createElement("p");
                        item.className = "definition";
                        item.textContent = `${meaning.partOfSpeech || "Definition"}: ${definition.definition}`;
                        dictionaryResult.append(item);
                        shown += 1;
                    }
                    if (shown >= 5) break;
                }
                if (shown >= 5) break;
            }
            if (!shown) dictionaryResult.textContent = `No definition found for “${word}”.`;
        } catch (error) {
            dictionaryResult.textContent = error.name === "AbortError"
                ? "The dictionary lookup timed out. Please try again."
                : error.message || "Could not contact the dictionary service.";
        } finally {
            window.clearTimeout(timeout);
        }
    });

    document.querySelector("#add-shortcut").addEventListener("click", addShortcut);
    document.querySelector("#customize-open").addEventListener("click", () => dialog.showModal());
    document.querySelector("#read-notes").addEventListener("click", () => speak(scratchpad.value));
    document.querySelector("#read-page").addEventListener("click", () => {
        speak(`${document.querySelector(".logo").textContent}. ${shortcutNav.innerText}. ${scratchpad.value}`);
    });
    document.querySelector("#match-background").addEventListener("change", (event) => {
        localStorage.setItem(keys.matchBackground, String(event.target.checked));
        applyMatchedColors();
    });
    document.querySelector("#background-file").addEventListener("change", async (event) => {
        const file = event.target.files && event.target.files[0];
        if (!file) return;
        if (!["image/png", "image/jpeg", "image/webp"].includes(file.type) || file.size > 12 * 1024 * 1024) {
            scratchpadStatus.textContent = "Choose a PNG, JPEG, or WebP image smaller than 12 MB.";
            event.target.value = "";
            return;
        }
        try {
            const image = await createImageBitmap(file);
            const scale = Math.min(1, 1920 / image.width, 1200 / image.height);
            const canvas = document.createElement("canvas");
            canvas.width = Math.max(1, Math.round(image.width * scale));
            canvas.height = Math.max(1, Math.round(image.height * scale));
            const context = canvas.getContext("2d");
            if (!context) throw new Error("Could not process this image.");
            context.drawImage(image, 0, 0, canvas.width, canvas.height);
            const data = canvas.toDataURL("image/jpeg", 0.78);
            if (data.length > 4_000_000) throw new Error("This image is too large to save in the browser profile.");
            localStorage.setItem(keys.background, data);
            const colors = extractColors(canvas);
            if (colors.length) store(keys.backgroundColors, {
                background: colors[0], accent: colors[1] || colors[0]
            });
            image.close();
            applyBackground();
            scratchpadStatus.textContent = "Background saved in this browser profile.";
        } catch (error) {
            scratchpadStatus.textContent = error.message || "Could not save this background image.";
        } finally {
            event.target.value = "";
        }
    });
    document.querySelector("#clear-background").addEventListener("click", () => {
        localStorage.removeItem(keys.background);
        localStorage.removeItem(keys.backgroundColors);
        document.body.classList.remove("has-background");
        document.body.style.removeProperty("--wallpaper");
        applyMatchedColors();
        scratchpadStatus.textContent = "Background image removed.";
    });
    document.querySelector("#theme-choice").addEventListener("change", (event) => {
        setTheme(event.target.value, true);
    });
    document.querySelector("#layout-choice").addEventListener("change", (event) => {
        const layout = ["centered", "compact", "wide"].includes(event.target.value)
            ? event.target.value : "centered";
        document.documentElement.dataset.layout = layout;
        store(keys.layout, layout);
    });
    for (const checkbox of document.querySelectorAll("[data-visibility]")) {
        checkbox.addEventListener("change", () => {
            const visibility = readStored(keys.visibility, {
                shortcuts: true, scratchpad: true, dictionary: true
            });
            visibility[checkbox.dataset.visibility] = checkbox.checked;
            store(keys.visibility, visibility);
            updateVisibility();
        });
    }

    document.addEventListener("keydown", (event) => {
        const isInputFocused = document.activeElement === input;
        if (event.key === "/" && !isInputFocused && !event.target.matches("textarea,input,select")) {
            event.preventDefault();
            input.focus();
            input.select();
        } else if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "k") {
            event.preventDefault();
            input.focus();
            input.select();
        } else if (event.key === "Escape" && isInputFocused) {
            input.blur();
        }
    });
    if (badge) badge.addEventListener("click", () => {
        input.focus();
        input.select();
    });

    scratchpad.value = localStorage.getItem(keys.scratchpad) || "";
    let saveTimer;
    scratchpad.addEventListener("input", () => {
        scratchpadStatus.textContent = "Saving…";
        window.clearTimeout(saveTimer);
        saveTimer = window.setTimeout(() => {
            try {
                localStorage.setItem(keys.scratchpad, scratchpad.value);
                scratchpadStatus.textContent = "Saved on this device";
            } catch {
                scratchpadStatus.textContent = "Could not save notes in the browser profile.";
            }
        }, 250);
    });
    setTheme(localStorage.getItem(keys.theme) || "obsidian");
    setBrowserMode(localStorage.getItem(keys.browserMode) || "normal");
    const layout = localStorage.getItem(keys.layout) || "centered";
    document.documentElement.dataset.layout = ["centered", "compact", "wide"].includes(layout)
        ? layout : "centered";
    document.querySelector("#layout-choice").value = document.documentElement.dataset.layout;
    document.querySelector("#match-background").checked =
        localStorage.getItem(keys.matchBackground) === "true";
    const searchEngine = readStored(keys.searchEngine, "bloxcy");
    engineSelect.value = [...engineSelect.options].some((option) => option.value === searchEngine)
        ? searchEngine : "bloxcy";
    renderShortcuts();
    updateVisibility();
    applyBackground();

    if (webview) {
        webview.addEventListener("message", (event) => {
            if (typeof event.data === "string" && event.data.startsWith("set-theme:")) {
                setTheme(event.data.slice("set-theme:".length));
            } else if (typeof event.data === "string" && event.data.startsWith("native-theme:")) {
                localStorage.setItem(keys.matchBackground, "false");
                document.querySelector("#match-background").checked = false;
                document.documentElement.style.removeProperty("--custom-bg");
                document.documentElement.style.removeProperty("--custom-accent");
                document.documentElement.style.removeProperty("--neon-cyan");
                document.documentElement.style.removeProperty("--border-focus");
                setTheme(event.data.slice("native-theme:".length));
            } else if (typeof event.data === "string" &&
                event.data.startsWith("browser-mode:")) {
                setBrowserMode(event.data.slice("browser-mode:".length));
            }
        });
    }
});
