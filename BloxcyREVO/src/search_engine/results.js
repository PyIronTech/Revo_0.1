"use strict";

(() => {
    const externalEngines = [
        ["Google", "https://www.google.com/search?q="],
        ["Bing", "https://www.bing.com/search?q="],
        ["DuckDuckGo", "https://duckduckgo.com/?q="],
        ["Brave", "https://search.brave.com/search?q="],
        ["Ecosia", "https://www.ecosia.org/search?q="],
        ["Startpage", "https://www.startpage.com/sp/search?query="],
        ["Kagi", "https://kagi.com/search?q="],
        ["YouTube", "https://www.youtube.com/results?search_query="]
    ];
    let searchData = null;
    const loadedProviders = new Set();
    let imagePage = 1;
    let imageHasMore = false;
    const webview = window.chrome && window.chrome.webview;

    function safeHttpUrl(value) {
        try {
            const url = new URL(value);
            return url.protocol === "https:" || url.protocol === "http:" ? url.href : "";
        } catch {
            return "";
        }
    }

    function activateTab(name) {
        for (const tab of document.querySelectorAll(".tab")) {
            const active = tab.id === `tab-${name}`;
            tab.setAttribute("aria-selected", String(active));
            document.querySelector(`#panel-${tab.id.slice(4)}`).hidden = !active;
        }
        if (name === "images" || name === "videos") loadProvider(name);
    }

    function renderSearch(data) {
        searchData = data;
        loadedProviders.clear();
        imagePage = 1;
        imageHasMore = false;
        document.querySelector("#images-results").replaceChildren();
        document.querySelector("#images-errors").hidden = true;
        document.querySelector("#load-more-images").hidden = true;
        document.querySelector("#query-label").textContent = `Results for "${data.query}"`;
        document.documentElement.dataset.theme = data.theme || "obsidian";
        document.documentElement.dataset.browserMode = data.browserMode || "normal";
        document.querySelector("#results-query").value = data.query;

        const siteResults = document.querySelector("#site-results");
        siteResults.replaceChildren();
        const validResults = Array.isArray(data.results) ? data.results.filter((item) =>
            item && typeof item.domain === "string" &&
            /^[a-z0-9.-]+$/i.test(item.domain)) : [];
        document.querySelector("#result-count").textContent = String(validResults.length);
        if (validResults.length === 0) {
            const empty = document.createElement("p");
            empty.className = "status";
            empty.textContent = "No close matches in the local top-sites database. Try a spelling variation or another search engine.";
            siteResults.append(empty);
        } else {
            validResults.forEach((item, index) => {
                const link = document.createElement("a");
                link.className = "result";
                link.rel = "noreferrer";
                link.href = `https://${item.domain}`;
                const resultIndex = document.createElement("span");
                resultIndex.className = "result-index";
                resultIndex.textContent = String(index + 1).padStart(2, "0");
                const copy = document.createElement("span");
                copy.className = "result-copy";
                const domain = document.createElement("span");
                domain.className = "domain";
                domain.textContent = item.domain;
                const rank = document.createElement("span");
                rank.className = "rank";
                rank.textContent = `Tranco top-site match · rank #${Number(item.rank) || "—"}`;
                copy.append(domain, rank);
                link.append(resultIndex, copy);
                siteResults.append(link);
            });
        }

        const links = document.querySelector("#engine-links");
        links.replaceChildren();
        for (const [name, base] of externalEngines) {
            const link = document.createElement("a");
            link.className = "side-card";
            link.rel = "noreferrer";
            link.href = `${base}${encodeURIComponent(data.query)}`;
            const title = document.createElement("strong");
            title.textContent = name;
            const caption = document.createElement("span");
            caption.textContent = `Search for "${data.query}"`;
            link.append(title, caption);
            links.append(link);
        }

        document.querySelector("#open-images").href =
            `https://commons.wikimedia.org/w/index.php?search=${encodeURIComponent(data.query)}&title=Special:MediaSearch&type=image`;
        document.querySelector("#open-openverse").href =
            `https://openverse.org/search/image?q=${encodeURIComponent(data.query)}`;
        document.querySelector("#open-videos").href =
            `https://www.youtube.com/results?search_query=${encodeURIComponent(data.query)}`;
        renderDictionary(data.dictionaryWord);
        const selectedTab = document.querySelector('.tab[aria-selected="true"]');
        if (selectedTab && ["images", "videos"].includes(selectedTab.id.slice(4))) {
            loadProvider(selectedTab.id.slice(4));
        }
    }

    async function renderDictionary(word) {
        const card = document.querySelector("#dictionary-card");
        if (typeof word !== "string" || !word) {
            card.hidden = true;
            return;
        }
        card.hidden = false;
        const status = document.querySelector("#dictionary-status");
        const output = document.querySelector("#dictionary-results");
        status.textContent = `Looking up "${word}"…`;
        output.replaceChildren();
        try {
            const response = await fetch(
                `https://api.dictionaryapi.dev/api/v2/entries/en/${encodeURIComponent(word)}`,
                { headers: { Accept: "application/json" }, signal: AbortSignal.timeout(8000) }
            );
            if (response.status === 404) throw new Error("No definition found.");
            if (!response.ok) throw new Error(`Dictionary service returned HTTP ${response.status}.`);
            const entries = await response.json();
            if (!Array.isArray(entries)) throw new Error("Dictionary returned an unexpected response.");
            let count = 0;
            for (const entry of entries) {
                for (const meaning of Array.isArray(entry.meanings) ? entry.meanings : []) {
                    for (const definition of Array.isArray(meaning.definitions) ? meaning.definitions : []) {
                        if (count >= 4) break;
                        if (typeof definition.definition !== "string") continue;
                        const paragraph = document.createElement("p");
                        paragraph.className = "definition";
                        paragraph.textContent = `${meaning.partOfSpeech || "Definition"}: ${definition.definition}`;
                        output.append(paragraph);
                        count += 1;
                    }
                    if (count >= 4) break;
                }
                if (count >= 4) break;
            }
            status.textContent = count ? `Definition for "${word}"` : "No definition found.";
        } catch (error) {
            status.textContent = error.message || "Could not contact the dictionary service.";
        }
    }

    function makeImage(item) {
        const href = safeHttpUrl(item.source) || safeHttpUrl(item.image);
        const image = safeHttpUrl(item.thumbnail);
        if (!href || !image) return null;
        const card = document.createElement("a");
        card.className = "image-card";
        card.href = href;
        card.rel = "noreferrer";
        const img = document.createElement("img");
        img.src = image;
        img.alt = typeof item.title === "string" ? item.title : "";
        img.loading = "lazy";
        img.referrerPolicy = "no-referrer";
        const title = document.createElement("span");
        title.textContent = item.title || "Open image";
        card.append(img, title);
        const attributionParts = [
            item.provider,
            item.creator || (item.uploader ? `Uploaded by ${item.uploader}` : ""),
            item.license
        ].filter(Boolean);
        if (attributionParts.length) {
            const attribution = document.createElement("span");
            attribution.className = "image-attribution";
            attribution.textContent = attributionParts.join(" · ");
            card.append(attribution);
        }
        return card;
    }

    function makeVideo(item) {
        const href = safeHttpUrl(item.url);
        const image = safeHttpUrl(item.thumbnail);
        if (!href || !image) return null;
        const card = document.createElement("a");
        card.className = "video-card";
        card.href = href;
        card.rel = "noreferrer";
        const img = document.createElement("img");
        img.src = image;
        img.alt = "";
        img.loading = "lazy";
        img.referrerPolicy = "no-referrer";
        const details = document.createElement("div");
        const title = document.createElement("span");
        title.textContent = item.title || "YouTube video";
        const channel = document.createElement("p");
        channel.textContent = item.channel || "";
        details.append(title, channel);
        const transcript = document.createElement("p");
        transcript.textContent = item.transcript || "No transcript available; ranked by title.";
        details.append(transcript);
        card.append(img, details);
        return card;
    }

    async function loadProvider(kind, loadMore = false) {
        if (loadedProviders.has(kind) && !(kind === "images" && loadMore)) return;
        if (kind === "images" && loadMore && !imageHasMore) return;
        loadedProviders.add(kind);
        const status = document.querySelector(`#${kind}-status`);
        const output = document.querySelector(`#${kind}-results`);
        const loadMoreButton = kind === "images" ? document.querySelector("#load-more-images") : null;
        const errors = kind === "images" ? document.querySelector("#images-errors") : null;
        if (loadMoreButton) loadMoreButton.disabled = true;
        if (!searchData || !searchData.providersAvailable) {
            status.textContent = searchData?.providerError || "Search providers are unavailable.";
            if (loadMoreButton) loadMoreButton.disabled = false;
            if (!searchData) loadedProviders.delete(kind);
            return;
        }
        const requestedPage = kind === "images" ? imagePage : 1;
        status.textContent = loadMore ? "Loading more images…" : "Searching…";
        try {
            const response = await fetch(
                `${searchData.apiBase}/${kind}?q=${encodeURIComponent(searchData.query)}${kind === "images" ? `&page=${requestedPage}` : ""}`,
                {
                    headers: { Authorization: `Bearer ${searchData.token}` },
                    signal: AbortSignal.timeout(45000)
                }
            );
            const payload = await response.json();
            if (!response.ok) throw new Error(payload.error || `Provider returned HTTP ${response.status}.`);
            if (!Array.isArray(payload.results)) throw new Error("Provider returned an invalid response.");
            if (!loadMore) output.replaceChildren();
            let count = 0;
            for (const item of payload.results) {
                const card = kind === "images" ? makeImage(item) : makeVideo(item);
                if (card) {
                    output.append(card);
                    count += 1;
                }
            }
            if (kind === "images") {
                const providerErrors = payload.errors && typeof payload.errors === "object"
                    ? Object.entries(payload.errors).map(([provider, message]) => `${provider}: ${message}`)
                    : [];
                errors.textContent = providerErrors.join(" ");
                errors.hidden = providerErrors.length === 0;
                imageHasMore = Boolean(payload.nextPage);
                imagePage = requestedPage + 1;
                loadMoreButton.hidden = !imageHasMore;
                status.textContent = count
                    ? `${count} images${providerErrors.length ? " (partial results)" : ""}`
                    : "No images found.";
            } else {
                status.textContent = count ? `${count} results` : "No results found. Try another query.";
            }
        } catch (error) {
            status.textContent = error.message || "Search failed. Please try again.";
            loadedProviders.delete(kind);
            if (loadMoreButton) loadMoreButton.hidden = !imageHasMore;
        } finally {
            if (loadMoreButton) loadMoreButton.disabled = false;
        }
    }

    for (const tab of document.querySelectorAll(".tab")) {
        tab.addEventListener("click", () => activateTab(tab.id.slice(4)));
        tab.addEventListener("keydown", (event) => {
            if (event.key !== "ArrowLeft" && event.key !== "ArrowRight") return;
            event.preventDefault();
            const tabs = [...document.querySelectorAll(".tab")];
            const index = tabs.indexOf(tab);
            tabs[(index + (event.key === "ArrowRight" ? 1 : tabs.length - 1)) % tabs.length].focus();
        });
    }
    document.querySelector("#load-more-images").addEventListener(
        "click", () => loadProvider("images", true)
    );
    document.querySelector("#results-search").addEventListener("submit", (event) => {
        event.preventDefault();
        const input = document.querySelector("#results-query");
        const query = input.value.trim();
        if (!query) {
            input.focus();
            return;
        }
        if (!webview) {
            document.querySelector("#query-label").textContent =
                "Search again inside the Bloxcy browser to query the local database.";
            return;
        }
        webview.postMessage(`results:search:${query}`);
    });

    if (webview) {
        webview.addEventListener("message", (event) => {
            if (typeof event.data !== "string") return;
            try {
                renderSearch(JSON.parse(event.data));
            } catch {
                document.querySelector("#query-label").textContent = "Could not read the search results.";
            }
        });
        webview.postMessage("results-ready");
    } else {
        document.querySelector("#query-label").textContent =
            "Results are available inside the Bloxcy browser.";
    }
})();
