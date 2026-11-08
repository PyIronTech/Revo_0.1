import json
import http.client
import threading
import unittest
from types import SimpleNamespace
from unittest.mock import MagicMock, patch
from urllib.parse import parse_qs, urlsplit

from src.apis.duckresults import ImageSearchError, search_images
from src.apis import search_server
from src.apis.youtuberesults import (
    _relevance,
    _transcript_excerpt,
    search_videos,
)


class CombinedImageSearchTests(unittest.TestCase):
    @staticmethod
    def provider_payloads():
        wikimedia = {
            "pages": [
                    {
                        "key": f"File:Cat-{index}.jpg",
                        "title": f"Cat {index}",
                        "thumbnail": {
                            "url": f"https://thumb.wikimedia.org/cat/60px-Cat-{index}.jpg"
                        },
                    }
                    for index in range(10)
            ]
        }
        openverse = {
            "results": [
                {
                    "title": f"A cat {index}",
                    "url": f"https://images.example/cat-{index}.jpg",
                    "thumbnail": f"https://images.example/cat-{index}-small.jpg",
                    "foreign_landing_url": f"https://openverse.org/image/cat-{index}",
                    "creator": "Openverse artist",
                    "license": "by",
                    "license_url": "https://creativecommons.org/licenses/by/4.0/",
                }
                for index in range(5)
            ]
        }
        return wikimedia, openverse

    def test_combines_both_sources_and_advances_each_page(self):
        wikimedia, openverse = self.provider_payloads()

        def fake_request(url):
            if "/search/page?" in url:
                return wikimedia
            if "/page/" in url:
                return {
                    "source": "|Author=[[User:Alvesgaspar|Alvesgaspar]]\n|Date=2020",
                    "latest": {"user": {"name": "Uploader"}},
                    "license": {
                        "title": "CC BY-SA 4.0",
                        "url": "https://creativecommons.org/licenses/by-sa/4.0/",
                    },
                }
            if "api.openverse.org" in url:
                return openverse
            raise AssertionError(f"Unexpected provider URL: {url}")

        requested_urls = []
        request_lock = threading.Lock()

        def record_request(url):
            with request_lock:
                requested_urls.append(url)
            return fake_request(url)

        with patch(
            "src.apis.duckresults._request_json", side_effect=record_request
        ):
            payload = search_images("  cat   photo ", page=2)

        self.assertEqual(len(payload["results"]), 10)
        self.assertEqual(
            [item["provider"] for item in payload["results"]],
            ["Wikimedia Commons"] * 5 + ["Openverse"] * 5,
        )
        self.assertEqual(payload["results"][0]["title"], "Cat 5")
        self.assertEqual(payload["results"][0]["creator"], "Alvesgaspar")
        self.assertEqual(payload["results"][0]["license"], "CC BY-SA 4.0")
        self.assertEqual(payload["results"][5]["creator"], "Openverse artist")
        self.assertTrue(payload["nextPage"])
        search_url = next(url for url in requested_urls if "/search/page?" in url)
        openverse_url = next(url for url in requested_urls if "api.openverse.org" in url)
        wikimedia_query = parse_qs(urlsplit(search_url).query)
        openverse_query = parse_qs(urlsplit(openverse_url).query)
        self.assertEqual(wikimedia_query["limit"], ["50"])
        self.assertEqual(len([url for url in requested_urls if "/page/" in url]), 5)
        self.assertEqual(openverse_query["page"], ["2"])
        self.assertEqual(openverse_query["page_size"], ["5"])
        self.assertEqual(wikimedia_query["q"], ["cat photo"])

    def test_keeps_partial_results_and_reports_failed_provider(self):
        _, openverse = self.provider_payloads()

        def provider_request(url):
            if "api.openverse.org" in url:
                return openverse
            raise ImageSearchError("Commons is unavailable.")

        with patch(
            "src.apis.duckresults._request_json",
            side_effect=provider_request,
        ):
            payload = search_images("cat")
        self.assertEqual(len(payload["results"]), 5)
        self.assertIn("Wikimedia Commons", payload["errors"])

    def test_both_provider_failures_are_reported(self):
        with patch(
            "src.apis.duckresults._request_json",
            side_effect=ImageSearchError("Provider failed."),
        ):
            with self.assertRaisesRegex(ImageSearchError, "Wikimedia Commons"):
                search_images("cat")

    def test_invalid_query_or_page_is_rejected_before_network(self):
        with patch("src.apis.duckresults._request_json") as request:
            with self.assertRaises(ValueError):
                search_images(" ")
            with self.assertRaises(ValueError):
                search_images("cat", page=0)
        request.assert_not_called()


class YouTubeResultRankingTests(unittest.TestCase):
    def test_transcript_relevance_outranks_unrelated_title(self):
        query_tokens = {"python", "tutorial"}
        relevant = _relevance(query_tokens, "A coding lesson", "Python tutorial for beginners")
        unrelated = _relevance(query_tokens, "Travel vlog", "A day at the beach")
        self.assertGreater(relevant, unrelated)

    def test_long_transcript_excerpt_contains_matching_text(self):
        excerpt = _transcript_excerpt("A" * 400 + " keyword " + "B" * 400, {"keyword"})
        self.assertLessEqual(len(excerpt), 366)
        self.assertIn("keyword", excerpt)

    def test_invalid_query_is_rejected_before_import_or_network(self):
        with patch.dict("sys.modules", {"yt_dlp": None}):
            with self.assertRaises(ValueError):
                search_videos(" ")

    def test_video_search_uses_transcript_to_rank_without_download(self):
        from yt_dlp import YoutubeDL
        from youtube_transcript_api import YouTubeTranscriptApi

        downloader = MagicMock()
        downloader.__enter__.return_value = downloader
        downloader.extract_info.return_value = {
            "entries": [
                {"id": "abcdefghijk", "title": "General video"},
                {"id": "lmnopqrstuv", "title": "Python tutorial"},
            ]
        }
        transcript_api = MagicMock()
        transcript_api.fetch.side_effect = [
            [SimpleNamespace(text="A short unrelated description")],
            [SimpleNamespace(text="Learn Python tutorial basics")],
        ]
        with patch("yt_dlp.YoutubeDL", return_value=downloader) as ydl, patch(
            "youtube_transcript_api.YouTubeTranscriptApi", return_value=transcript_api
        ):
            results = search_videos("python tutorial", limit=2)

        self.assertEqual(results[0]["id"], "lmnopqrstuv")
        self.assertTrue(results[0]["has_transcript"])
        self.assertTrue(ydl.call_args.args[0]["skip_download"])


class SearchServerTests(unittest.TestCase):
    def setUp(self):
        self.server = search_server.SearchServer(("127.0.0.1", 0), search_server.SearchHandler)
        self.server.auth_token = "test-token"
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.thread.join(timeout=2)
        self.server.server_close()

    def request(self, method, path, headers=None):
        headers = dict(headers or {})
        if headers.get("Authorization"):
            headers["Authorization"] = f"Bearer {self.server.auth_token}"
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port)
        connection.request(method, path, headers=headers)
        response = connection.getresponse()
        body = response.read()
        status = response.status
        connection.close()
        return status, body

    def test_result_endpoint_requires_token(self):
        status, _ = self.request("GET", "/images?q=cat", {"Origin": "null"})
        self.assertEqual(status, 403)

    def test_result_endpoint_returns_provider_json(self):
        with patch(
            "src.apis.search_server.search_images",
            return_value={
                "results": [{"title": "cat", "image": "https://example.test/cat.jpg"}],
                "errors": {},
                "nextPage": False,
            },
        ):
            status, body = self.request(
                "GET",
                "/images?q=cat",
                {"Origin": "null", "Authorization": "Bearer test-token"},
            )
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(body)["results"][0]["title"], "cat")

    def test_image_page_is_validated(self):
        status, _ = self.request(
            "GET",
            "/images?q=cat&page=101",
            {"Origin": "null", "Authorization": "******"},
        )
        self.assertEqual(status, 400)

    def test_browser_preflight_does_not_need_token(self):
        status, _ = self.request(
            "OPTIONS",
            "/images",
            {"Origin": "null", "Access-Control-Request-Headers": "authorization"},
        )
        self.assertEqual(status, 204)


if __name__ == "__main__":
    unittest.main()
