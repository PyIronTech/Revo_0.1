"""Small client for the public dictionaryapi.dev English dictionary endpoint."""

from __future__ import annotations

import json
import math
import re
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.parse import quote
from urllib.request import Request, urlopen

_WORD_PATTERN = re.compile(r"[A-Za-z][A-Za-z'-]{0,79}\Z")
_API_ROOT = "https://api.dictionaryapi.dev/api/v2/entries/en/"


class DictionaryError(Exception):
    """Base error for dictionary lookup failures."""


class WordNotFoundError(DictionaryError):
    """Raised when the API has no entry for a valid word."""


def lookup_word(word: str, timeout: float = 8.0) -> list[dict[str, Any]]:
    """Return dictionary entries for one English word.

    Raises ``ValueError`` for invalid input, ``WordNotFoundError`` for a 404,
    and ``DictionaryError`` for network, HTTP, or response-format failures.
    """
    if not isinstance(word, str):
        raise ValueError("word must be a string")
    normalized = word.strip()
    if not _WORD_PATTERN.fullmatch(normalized):
        raise ValueError("Enter one English word using letters, apostrophes, or hyphens.")
    if not isinstance(timeout, (int, float)) or not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("timeout must be positive")

    request = Request(
        _API_ROOT + quote(normalized, safe=""),
        headers={"Accept": "application/json", "User-Agent": "BloxcyREVO/1.0"},
    )
    try:
        with urlopen(request, timeout=timeout) as response:
            body = response.read(1_000_001)
            if len(body) > 1_000_000:
                raise DictionaryError("Dictionary API response is too large.")
            payload = json.loads(body.decode("utf-8"))
    except HTTPError as error:
        if error.code == 404:
            raise WordNotFoundError(f"No definition found for {normalized!r}.") from error
        raise DictionaryError(f"Dictionary API returned HTTP {error.code}.") from error
    except (URLError, TimeoutError, OSError) as error:
        raise DictionaryError("Could not reach the dictionary API.") from error
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise DictionaryError("Dictionary API returned invalid JSON.") from error

    if not isinstance(payload, list) or not payload or not all(
        isinstance(entry, dict) for entry in payload
    ):
        raise DictionaryError("Dictionary API returned an unexpected response.")
    return payload