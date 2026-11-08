"""Search YouTube metadata and rank it using available transcript text."""

from __future__ import annotations

import difflib
import math
import re
from typing import Any
from urllib.error import URLError

_WORD_PATTERN = re.compile(r"[a-z0-9]+", re.IGNORECASE)
_MAX_QUERY_LENGTH = 256


class VideoSearchError(Exception):
    """Raised when YouTube search cannot produce usable video results."""


def _tokens(text: str) -> set[str]:
    return {token.lower() for token in _WORD_PATTERN.findall(text)}


def _similarity(left: str, right: str) -> float:
    if left == right:
        return 1.0
    return difflib.SequenceMatcher(None, left, right).ratio()


def _relevance(query_tokens: set[str], title: str, transcript: str) -> int:
    title_tokens = _tokens(title)
    transcript_tokens = _tokens(transcript)
    title_score = sum(
        3 if token in title_tokens else
        1 if any(_similarity(token, candidate) >= 0.82 for candidate in title_tokens) else 0
        for token in query_tokens
    )
    transcript_score = sum(
        2 if token in transcript_tokens else
        1 if any(_similarity(token, candidate) >= 0.82 for candidate in transcript_tokens) else 0
        for token in query_tokens
    )
    return title_score + transcript_score


def _transcript_excerpt(text: str, query_tokens: set[str], maximum: int = 360) -> str:
    normalized = " ".join(text.split())
    if len(normalized) <= maximum:
        return normalized
    lower_text = normalized.lower()
    positions = [lower_text.find(token) for token in query_tokens]
    positions = [position for position in positions if position >= 0]
    start = max(0, min(positions) - maximum // 4) if positions else 0
    excerpt = normalized[start : start + maximum]
    if start:
        excerpt = "..." + excerpt
    if start + maximum < len(normalized):
        excerpt += "..."
    return excerpt


def search_videos(
    query: str, *, limit: int = 8, timeout: float = 15.0
) -> list[dict[str, Any]]:
    """Return relevant video metadata and transcript excerpts without downloading media."""
    if not isinstance(query, str) or not query.strip() or len(query) > _MAX_QUERY_LENGTH:
        raise ValueError("Enter a search query of 1 to 256 characters.")
    if not isinstance(limit, int) or isinstance(limit, bool) or not 1 <= limit <= 20:
        raise ValueError("limit must be between 1 and 20.")
    if not isinstance(timeout, (int, float)) or isinstance(timeout, bool) or not 0 < timeout <= 45:
        raise ValueError("timeout must be greater than 0 and at most 45 seconds.")

    try:
        import requests
        from yt_dlp import YoutubeDL
        from yt_dlp.utils import DownloadError
        from youtube_transcript_api import YouTubeTranscriptApi
        from youtube_transcript_api._errors import (
            YouTubeTranscriptApiException,
        )
    except ImportError as error:
        raise VideoSearchError(
            "YouTube search requires the yt-dlp and youtube-transcript-api packages."
        ) from error

    normalized_query = " ".join(query.split())
    search_options = {
        "quiet": True,
        "no_warnings": True,
        "skip_download": True,
        "extract_flat": True,
        "ignoreerrors": True,
        "socket_timeout": timeout,
        "playlistend": min(20, max(limit * 2, 8)),
    }
    try:
        with YoutubeDL(search_options) as downloader:
            search_data = downloader.extract_info(
                f"ytsearch{search_options['playlistend']}:{normalized_query}",
                download=False,
            )
    except (DownloadError, URLError, TimeoutError, OSError) as error:
        raise VideoSearchError("Could not retrieve YouTube search results.") from error

    entries = search_data.get("entries", []) if isinstance(search_data, dict) else []
    if not isinstance(entries, list):
        raise VideoSearchError("YouTube returned an unexpected search response.")

    query_tokens = _tokens(normalized_query)
    class TimeoutSession(requests.Session):
        def request(self, method: str, url: str, **kwargs: Any) -> Any:
            kwargs.setdefault("timeout", min(timeout, 5.0))
            return super().request(method, url, **kwargs)

    transcript_api = YouTubeTranscriptApi(http_client=TimeoutSession())
    ranked: list[tuple[int, dict[str, Any]]] = []
    transcript_limit = min(len(entries), 5)
    for entry_index, entry in enumerate(entries):
        if not isinstance(entry, dict):
            continue
        video_id = entry.get("id")
        if not isinstance(video_id, str) or not re.fullmatch(r"[A-Za-z0-9_-]{6,20}", video_id):
            continue
        title = entry.get("title") if isinstance(entry.get("title"), str) else "YouTube video"
        transcript_text = ""
        if entry_index < transcript_limit:
            try:
                transcript = transcript_api.fetch(video_id, languages=["en"])
                transcript_text = " ".join(
                    snippet.text for snippet in transcript if isinstance(snippet.text, str)
                )[:24_000]
            except (YouTubeTranscriptApiException, requests.exceptions.RequestException):
                transcript_text = ""

        thumbnail = entry.get("thumbnail")
        if not isinstance(thumbnail, str) or not thumbnail.startswith("https://"):
            thumbnail = f"https://i.ytimg.com/vi/{video_id}/hqdefault.jpg"
        webpage_url = f"https://www.youtube.com/watch?v={video_id}"
        result = {
            "id": video_id,
            "title": title[:300],
            "url": webpage_url,
            "thumbnail": thumbnail[:4096],
            "channel": (
                entry.get("channel")
                if isinstance(entry.get("channel"), str)
                else entry.get("uploader")
                if isinstance(entry.get("uploader"), str)
                else ""
            )[:200],
            "duration": entry.get("duration")
            if isinstance(entry.get("duration"), (int, float))
            and not isinstance(entry["duration"], bool)
            and math.isfinite(entry["duration"])
            else None,
            "transcript": _transcript_excerpt(transcript_text, query_tokens),
            "has_transcript": bool(transcript_text),
        }
        ranked.append(
            (_relevance(query_tokens, result["title"], transcript_text), result)
        )

    ranked.sort(key=lambda item: item[0], reverse=True)
    return [result for _, result in ranked[:limit]]