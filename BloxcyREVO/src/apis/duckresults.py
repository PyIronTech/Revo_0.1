"""Image search using Wikimedia Commons and Openverse."""

from __future__ import annotations

import json
import re
from concurrent.futures import ThreadPoolExecutor
from html import unescape
from urllib.error import HTTPError, URLError
from urllib.parse import quote, urlencode, urlsplit, urlunsplit
from urllib.request import Request, urlopen

_MAX_RESPONSE_BYTES = 1_500_000
_RESULTS_PER_PROVIDER = 5


class ImageSearchError(RuntimeError):
    """Raised when both image providers fail or return unusable data."""


def _request_json(url: str) -> dict[str, object]:
    request = Request(
        url,
        headers={
            "Accept": "application/json",
            "User-Agent": "BloxcyREVO/1.0 (image search)",
        },
    )
    try:
        with urlopen(request, timeout=12) as response:
            body = response.read(_MAX_RESPONSE_BYTES + 1)
        if len(body) > _MAX_RESPONSE_BYTES:
            raise ImageSearchError("Image provider response exceeded the size limit.")
        payload = json.loads(body.decode("utf-8"))
    except HTTPError as error:
        raise ImageSearchError(f"Image provider returned HTTP {error.code}.") from error
    except (URLError, TimeoutError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ImageSearchError("Could not read a valid response from an image provider.") from error
    if not isinstance(payload, dict):
        raise ImageSearchError("Image provider returned an unexpected response.")
    return payload


def _clean_text(value: object) -> str:
    return unescape(re.sub(r"<[^>]*>", "", value)).strip() if isinstance(value, str) else ""


def _http_url(value: object) -> str:
    if not isinstance(value, str):
        return ""
    if value.startswith("//"):
        value = f"https:{value}"
    return value if re.match(r"^https?://", value, re.IGNORECASE) else ""


def _extract_author(source: object) -> str:
    if not isinstance(source, str):
        return ""
    match = re.search(
        r"(?ims)^[ \t]*\|[ \t]*(?:author|artist)[ \t]*=[ \t]*(.*?)(?=^[ \t]*\|[a-z0-9_-]+[ \t]*=|^[ \t]*\}\})",
        source,
    )
    if not match:
        return ""
    author = match.group(1)
    author = re.sub(r"\{\{[^{}]*\}\}", "", author)
    author = re.sub(r"\[\[([^|\]]+)\|([^\]]+)\]\]", r"\2", author)
    author = re.sub(r"\[\[([^\]]+)\]\]", r"\1", author)
    author = re.sub(r"\[https?://\S+\s+([^\]]+)\]", r"\1", author)
    author = re.sub(r"'{2,}", "", author)
    return _clean_text(" ".join(author.split()))[:200]


def _search_wikimedia(query: str, page: int) -> list[dict[str, object]]:
    if page > 10:
        return []
    parameters = urlencode({"q": query, "limit": 50})
    payload = _request_json(
        f"https://api.wikimedia.org/core/v1/commons/search/page?{parameters}"
    )
    pages = payload.get("pages")
    if not isinstance(pages, list):
        return []

    candidates = [
        item for item in pages
        if isinstance(item, dict)
        and isinstance(item.get("key"), str)
        and item["key"].startswith("File:")
        and isinstance(item.get("thumbnail"), dict)
    ]
    start = (page - 1) * _RESULTS_PER_PROVIDER
    selected = candidates[start:start + _RESULTS_PER_PROVIDER]

    def load_metadata(item: dict[str, object]) -> dict[str, object]:
        key = item["key"]
        details = _request_json(
            "https://api.wikimedia.org/core/v1/commons/page/" + quote(key, safe="")
        )
        thumbnail_data = item.get("thumbnail")
        thumbnail = _http_url(
            thumbnail_data.get("url") if isinstance(thumbnail_data, dict) else None
        )
        if not thumbnail:
            raise ImageSearchError("Wikimedia Commons returned an invalid image thumbnail.")
        parsed_thumbnail = urlsplit(thumbnail)
        filename = parsed_thumbnail.path.rsplit("/", 1)[-1]
        if filename.startswith("60px-"):
            filename = "640px-" + filename[len("60px-"):]
            thumbnail = urlunsplit(parsed_thumbnail._replace(
                path=parsed_thumbnail.path.rsplit("/", 1)[0] + "/" + filename
            ))

        latest = details.get("latest")
        uploader = latest.get("user") if isinstance(latest, dict) else None
        license_data = details.get("license")
        title = _clean_text(item.get("title"))
        if title.lower().startswith("file:"):
            title = title[5:].strip()
        return {
            "provider": "Wikimedia Commons",
            "title": title or "Wikimedia Commons image",
            "image": thumbnail,
            "thumbnail": thumbnail,
            "source": "https://commons.wikimedia.org/wiki/" +
                quote(key.replace(" ", "_"), safe=":"),
            "creator": _extract_author(details.get("source")),
            "uploader": _clean_text(uploader.get("name") if isinstance(uploader, dict) else None),
            "license": _clean_text(license_data.get("title") if isinstance(license_data, dict) else None),
            "licenseUrl": _http_url(license_data.get("url") if isinstance(license_data, dict) else None),
        }

    with ThreadPoolExecutor(max_workers=_RESULTS_PER_PROVIDER) as executor:
        return list(executor.map(load_metadata, selected))


def _search_openverse(query: str, page: int) -> list[dict[str, object]]:
    parameters = urlencode({"q": query, "page": page, "page_size": _RESULTS_PER_PROVIDER})
    payload = _request_json(f"https://api.openverse.org/v1/images/?{parameters}")
    entries = payload.get("results")
    if not isinstance(entries, list):
        raise ImageSearchError("Openverse returned an unexpected response.")

    results: list[dict[str, object]] = []
    for entry in entries:
        if not isinstance(entry, dict):
            continue
        image = _http_url(entry.get("url"))
        thumbnail = _http_url(entry.get("thumbnail")) or _http_url(entry.get("thumbnail_url"))
        source = _http_url(entry.get("foreign_landing_url")) or image
        if not image or not thumbnail or not source:
            continue
        results.append(
            {
                "provider": "Openverse",
                "title": _clean_text(entry.get("title")) or "Openverse image",
                "image": image,
                "thumbnail": thumbnail,
                "source": source,
                "creator": _clean_text(entry.get("creator")),
                "license": str(entry.get("license") or "").upper(),
                "licenseUrl": _http_url(entry.get("license_url")),
            }
        )
    return results[:_RESULTS_PER_PROVIDER]


def search_images(query: str, *, page: int = 1) -> dict[str, object]:
    """Return up to five results per provider, retaining duplicates."""
    normalized_query = " ".join(query.split())
    if not normalized_query:
        raise ValueError("Enter an image search query.")
    if len(normalized_query) > 256:
        raise ValueError("Image search queries must be 256 characters or fewer.")
    if not isinstance(page, int) or isinstance(page, bool) or not 1 <= page <= 100:
        raise ValueError("Image search page must be between 1 and 100.")

    results: list[dict[str, object]] = []
    errors: dict[str, str] = {}
    for provider, search in (
        ("Wikimedia Commons", _search_wikimedia),
        ("Openverse", _search_openverse),
    ):
        try:
            results.extend(search(normalized_query, page))
        except ImageSearchError as error:
            errors[provider] = str(error)

    if not results:
        detail = "; ".join(f"{provider}: {error}" for provider, error in errors.items())
        raise ImageSearchError(detail or "No image results were found.")
    return {
        "results": results,
        "errors": errors,
        "nextPage": any(
            sum(result["provider"] == provider for result in results) == _RESULTS_PER_PROVIDER
            for provider in ("Wikimedia Commons", "Openverse")
        ),
    }
