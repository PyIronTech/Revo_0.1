import unittest
from unittest.mock import MagicMock, patch
from urllib.error import HTTPError

from src.apis.dictionary import (
    DictionaryError,
    WordNotFoundError,
    lookup_word,
)


class DictionaryLookupTests(unittest.TestCase):
    def test_lookup_returns_entries_and_encodes_word(self):
        response = MagicMock()
        response.__enter__.return_value.read.return_value = (
            b'[{"word":"mother-in-law","meanings":[]}]'
        )
        with patch("src.apis.dictionary.urlopen", return_value=response) as urlopen:
            result = lookup_word(" mother-in-law ")

        self.assertEqual(result[0]["word"], "mother-in-law")
        self.assertIn("/mother-in-law", urlopen.call_args.args[0].full_url)
        self.assertEqual(urlopen.call_args.kwargs["timeout"], 8.0)

    def test_invalid_word_is_rejected_before_network(self):
        with patch("src.apis.dictionary.urlopen") as urlopen:
            with self.assertRaises(ValueError):
                lookup_word("two words")
        urlopen.assert_not_called()

    def test_not_found_has_a_specific_error(self):
        error = HTTPError("https://example.test", 404, "not found", {}, None)
        with patch("src.apis.dictionary.urlopen", side_effect=error):
            with self.assertRaises(WordNotFoundError):
                lookup_word("unlikelyword")

    def test_unexpected_json_shape_is_reported(self):
        response = MagicMock()
        response.__enter__.return_value.read.return_value = b'{"error":"bad response"}'
        with patch("src.apis.dictionary.urlopen", return_value=response):
            with self.assertRaises(DictionaryError):
                lookup_word("example")


if __name__ == "__main__":
    unittest.main()
