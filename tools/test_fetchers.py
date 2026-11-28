"""Unit tests for tools/fetch_common.py, tools/fetch_models.py, and tools/fetch_textures.py."""

import hashlib
import io
import json
import pathlib
import sys
import tempfile
import unittest
import urllib.error
from unittest import mock
import zipfile

TOOLS_DIR = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

import fetch_common  # noqa: E402
import fetch_models  # noqa: E402
import fetch_textures  # noqa: E402


class _FakeResponse:
    def __init__(self, data, chunk_size=7):
        self._data = data
        self._pos = 0
        self._chunk = chunk_size

    def read(self, n=-1):
        if n is None or n < 0:
            rem = self._data[self._pos:]
            self._pos = len(self._data)
            return rem
        end = min(len(self._data), self._pos + min(n, self._chunk))
        out = self._data[self._pos:end]
        self._pos = end
        return out

    def __enter__(self):
        return self

    def __exit__(self, *args):
        return False


class FetchSecurityTests(unittest.TestCase):
    def test_validate_asset_name(self):
        self.assertEqual(fetch_common.validate_asset_name("Grass004"), "Grass004")
        self.assertEqual(fetch_common.validate_asset_name("dead_tree_01"), "dead_tree_01")
        for bad in ["", ".", "..", "../evil", "a/b", r"a\b", "foo..bar", "bad name", "C:drive"]:
            with self.subTest(bad=bad):
                with self.assertRaises(ValueError):
                    fetch_common.validate_asset_name(bad)

    def test_safe_target_path(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            ok = fetch_common.safe_target_path(root, "textures/leaf_diff.jpg")
            self.assertEqual(ok, (root / "textures" / "leaf_diff.jpg").resolve())

            for bad in ["", ".", "/", "\\", "../escape.txt", "textures/../../out.txt", "/etc/passwd", "C:/Windows/win.ini"]:
                with self.subTest(bad=bad):
                    with self.assertRaises(ValueError):
                        fetch_common.safe_target_path(root, bad)

    def test_rejects_non_https_urls(self):
        for url in ["file:///etc/passwd", "http://example.com/a.jpg", "ftp://example.com/a.zip"]:
            with self.subTest(url=url):
                with self.assertRaises(ValueError):
                    fetch_common.download_bytes(url, "ua", backoff=0)
                with self.assertRaises(ValueError):
                    fetch_common.download_to_spooled_file(url, "ua", backoff=0)

    def test_write_atomic_replaces_and_cleans_part_on_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = pathlib.Path(tmp) / "sub" / "data.bin"
            fetch_common.write_atomic(target, b"hello")
            self.assertEqual(target.read_bytes(), b"hello")
            self.assertEqual(list(target.parent.glob("*.part")), [])

            with mock.patch("os.replace", side_effect=OSError("disk full")):
                with self.assertRaises(OSError):
                    fetch_common.write_atomic(target, b"world")
            self.assertEqual(target.read_bytes(), b"hello")
            self.assertEqual(list(target.parent.glob("*.part")), [])

    def test_md5_verification_and_retry_behavior(self):
        good_data = b"payload-123"
        good_md5 = hashlib.md5(good_data).hexdigest()

        calls = {"count": 0}

        def flaky_urlopen(req, timeout=0):
            calls["count"] += 1
            if calls["count"] == 1:
                raise urllib.error.HTTPError(req.full_url, 503, "busy", None, None)
            return _FakeResponse(good_data)

        with mock.patch("urllib.request.urlopen", side_effect=flaky_urlopen):
            got = fetch_common.download_bytes("https://example.com/file.bin", "ua", retries=2, backoff=0, expected_md5=good_md5)
        self.assertEqual(got, good_data)
        self.assertEqual(calls["count"], 2)

        with mock.patch("urllib.request.urlopen", return_value=_FakeResponse(good_data)):
            with self.assertRaises(ValueError):
                fetch_common.download_bytes("https://example.com/file.bin", "ua", retries=1, backoff=0, expected_md5="0" * 32)

        not_found_calls = {"count": 0}

        def not_found(req, timeout=0):
            not_found_calls["count"] += 1
            raise urllib.error.HTTPError(req.full_url, 404, "not found", None, None)

        with mock.patch("urllib.request.urlopen", side_effect=not_found):
            with self.assertRaises(urllib.error.HTTPError):
                fetch_common.download_bytes("https://example.com/missing.bin", "ua", retries=3, backoff=0)
        self.assertEqual(not_found_calls["count"], 1)

    def test_fetch_model_rejects_traversal_and_writes_complete(self):
        bin_data = b"BIN"
        tex_data = b"TEX"
        alpha_data = b"ALP"
        gltf_data = b"{}"
        api_payload = {
            "gltf": {
                "1k": {
                    "gltf": {
                        "url": "https://cdn.polyhaven.com/models/tree/tree.gltf",
                        "md5": hashlib.md5(gltf_data).hexdigest(),
                        "include": {
                            "tree.bin": {
                                "url": "https://cdn.polyhaven.com/models/tree/tree.bin",
                                "md5": hashlib.md5(bin_data).hexdigest(),
                            },
                            "textures/leaf_diff_1k.jpg": {
                                "url": "https://cdn.polyhaven.com/models/tree/leaf_diff_1k.jpg",
                                "md5": hashlib.md5(tex_data).hexdigest(),
                            },
                        },
                    }
                }
            },
            "leaves_alpha": {
                "1k": {
                    "jpg": {
                        "url": "https://cdn.polyhaven.com/models/tree/leaf_alpha_1k.jpg",
                        "md5": hashlib.md5(alpha_data).hexdigest(),
                    }
                }
            },
        }
        url_map = {
            "https://api.polyhaven.com/files/oak": json.dumps(api_payload).encode("utf-8"),
            "https://cdn.polyhaven.com/models/tree/tree.bin": bin_data,
            "https://cdn.polyhaven.com/models/tree/leaf_diff_1k.jpg": tex_data,
            "https://cdn.polyhaven.com/models/tree/leaf_alpha_1k.jpg": alpha_data,
            "https://cdn.polyhaven.com/models/tree/tree.gltf": gltf_data,
        }

        with tempfile.TemporaryDirectory() as tmp:
            folder = pathlib.Path(tmp) / "oak"
            with mock.patch("urllib.request.urlopen", side_effect=lambda req, timeout=0: _FakeResponse(url_map[req.full_url])):
                size = fetch_models.fetch_model("oak", "1k", folder)
            self.assertEqual(size, len(bin_data) + len(tex_data) + len(alpha_data) + len(gltf_data))
            self.assertTrue(fetch_common.is_complete(folder, "oak.gltf"))
            self.assertEqual((folder / "textures" / "leaf_alpha_1k.jpg").read_bytes(), alpha_data)

            # Reject path traversal in glTF include map
            bad_payload = json.loads(json.dumps(api_payload))
            bad_payload["gltf"]["1k"]["gltf"]["include"] = {
                "../../evil.txt": {"url": "https://cdn.polyhaven.com/models/tree/tree.bin", "md5": hashlib.md5(bin_data).hexdigest()}
            }
            url_map_bad = dict(url_map)
            url_map_bad["https://api.polyhaven.com/files/oak"] = json.dumps(bad_payload).encode("utf-8")
            folder_bad = pathlib.Path(tmp) / "oak_bad"
            with mock.patch("urllib.request.urlopen", side_effect=lambda req, timeout=0: _FakeResponse(url_map_bad[req.full_url])):
                with self.assertRaises(ValueError):
                    fetch_models.fetch_model("oak", "1k", folder_bad)
            self.assertFalse(fetch_common.is_complete(folder_bad, "oak.gltf"))

    def test_fetch_textures_polyhaven_and_ambientcg(self):
        albedo = b"JPG-ALBEDO"
        normal = b"JPG-NORMAL"
        poly_api = {
            "Diffuse": {"2k": {"jpg": {"url": "https://cdn.polyhaven.com/tex/alb.jpg", "md5": hashlib.md5(albedo).hexdigest()}}},
            "nor_gl": {"2k": {"jpg": {"url": "https://cdn.polyhaven.com/tex/nor.jpg", "md5": hashlib.md5(normal).hexdigest()}}},
        }
        poly_map = {
            "https://api.polyhaven.com/files/grass": json.dumps(poly_api).encode("utf-8"),
            "https://cdn.polyhaven.com/tex/alb.jpg": albedo,
            "https://cdn.polyhaven.com/tex/nor.jpg": normal,
        }

        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w") as zf:
            zf.writestr("Grass004_2K-JPG_Color.jpg", albedo)
            zf.writestr("Grass004_2K-JPG_NormalGL.jpg", normal)
        zip_bytes = buf.getvalue()

        with tempfile.TemporaryDirectory() as tmp:
            poly_dir = pathlib.Path(tmp) / "grass"
            with mock.patch("urllib.request.urlopen", side_effect=lambda req, timeout=0: _FakeResponse(poly_map[req.full_url])):
                fetch_textures.fetch_polyhaven("grass", "2k", poly_dir)
            self.assertTrue(fetch_common.is_complete(poly_dir, "albedo.jpg"))
            self.assertEqual((poly_dir / "albedo.jpg").read_bytes(), albedo)
            self.assertEqual((poly_dir / "normal.jpg").read_bytes(), normal)

            amb_dir = pathlib.Path(tmp) / "Grass004"
            with mock.patch("urllib.request.urlopen", side_effect=lambda req, timeout=0: _FakeResponse(zip_bytes)):
                fetch_textures.fetch_ambientcg("Grass004", "2k", amb_dir)
            self.assertTrue(fetch_common.is_complete(amb_dir, "albedo.jpg"))
            self.assertEqual((amb_dir / "albedo.jpg").read_bytes(), albedo)
            self.assertEqual((amb_dir / "normal.jpg").read_bytes(), normal)


if __name__ == "__main__":
    unittest.main()
