"""Loopback-only, authenticated bridge from the native browser to search providers."""

from __future__ import annotations

import hmac
import json
import logging
import secrets
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

if __package__:
    from .duckresults import ImageSearchError, search_images
    from .youtuberesults import VideoSearchError, search_videos
else:
    from duckresults import ImageSearchError, search_images
    from youtuberesults import VideoSearchError, search_videos

_MAX_QUERY_LENGTH = 256
_MAX_RESPONSE_BYTES = 2_500_000


class SearchHandler(BaseHTTPRequestHandler):
    server_version = "BloxcySearch/1"
    sys_version = ""

    def log_message(self, _format: str, *_args: object) -> None:
        return

    def do_OPTIONS(self) -> None:
        if not self._authorized(require_token=False):
            self._send_json(403, {"error": "Search provider request was not authorized."})
            return
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "null")
        self.send_header("Access-Control-Allow-Headers", "Authorization, Content-Type")
        self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
        if self.headers.get("Access-Control-Request-Private-Network") == "true":
            self.send_header("Access-Control-Allow-Private-Network", "true")
        self.send_header("Vary", "Origin")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_GET(self) -> None:
        if not self._authorized():
            self._send_json(403, {"error": "Search provider request was not authorized."})
            return
        if len(self.path) > 2048:
            self._send_json(414, {"error": "Search request is too long."})
            return

        try:
            request = urlsplit(self.path)
            parameters = parse_qs(request.query, keep_blank_values=True, max_num_fields=5)
        except ValueError:
            self._send_json(400, {"error": "Search request contains too many parameters."})
            return
        query_values = parameters.get("q", [])
        if len(query_values) != 1 or not query_values[0].strip() or len(query_values[0]) > _MAX_QUERY_LENGTH:
            self._send_json(400, {"error": "Enter a search query of 1 to 256 characters."})
            return
        query = query_values[0]

        try:
            if request.path == "/images":
                page_values = parameters.get("page", ["1"])
                if len(page_values) != 1 or not page_values[0].isdigit():
                    self._send_json(400, {"error": "Image page must be a positive integer."})
                    return
                page = int(page_values[0])
                if not 1 <= page <= 100:
                    self._send_json(400, {"error": "Image page must be between 1 and 100."})
                    return
                image_payload = search_images(query, page=page)
                self._send_json(200, {"query": query, "page": page, **image_payload})
                return
            elif request.path == "/videos":
                results = search_videos(query, limit=8)
            else:
                self._send_json(404, {"error": "Unknown search provider endpoint."})
                return
        except (ValueError, ImageSearchError, VideoSearchError) as error:
            self._send_json(502, {"error": str(error)})
            return
        except Exception:
            logging.exception("Unexpected Bloxcy search-provider failure")
            self._send_json(500, {"error": "The search provider failed unexpectedly."})
            return

        self._send_json(200, {"query": query, "results": results})

    def _authorized(self, *, require_token: bool = True) -> bool:
        expected_host = f"127.0.0.1:{self.server.server_port}"
        origin = self.headers.get("Origin")
        authorization = self.headers.get("Authorization", "")
        return (
            self.client_address[0] == "127.0.0.1"
            and self.headers.get("Host") == expected_host
            and origin in (None, "null")
            and (
                not require_token
                or hmac.compare_digest(authorization, f"Bearer {self.server.auth_token}")
            )
        )

    def _send_json(self, status: int, payload: dict[str, object]) -> None:
        body = json.dumps(payload, ensure_ascii=True, separators=(",", ":")).encode("utf-8")
        if len(body) > _MAX_RESPONSE_BYTES:
            status = 502
            body = b'{"error":"Search provider response exceeded the size limit."}'
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "null")
        self.send_header("Vary", "Origin")
        self.end_headers()
        self.wfile.write(body)


class SearchServer(ThreadingHTTPServer):
    auth_token: str
    daemon_threads = True
    allow_reuse_address = False


def main() -> None:
    server = SearchServer(("127.0.0.1", 0), SearchHandler)
    server.auth_token = secrets.token_urlsafe(32)
    print(f"READY\t{server.server_port}\t{server.auth_token}", flush=True)
    try:
        server.serve_forever(poll_interval=0.25)
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
