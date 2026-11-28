"""Shared safe download, path validation, and atomic file helpers for tools/fetch_*.py."""

import hashlib
import json
import os
import pathlib
import re
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

ASSET_NAME_RE = re.compile(r"^[A-Za-z0-9_-][A-Za-z0-9_.-]*$")
COMPLETE_MARKER = ".complete"
CHUNK_BYTES = 64 * 1024


def validate_asset_name(asset):
    """Validate that asset is a safe single path component (no slashes or '..')."""
    if not isinstance(asset, str) or ".." in asset or not ASSET_NAME_RE.match(asset):
        raise ValueError(f"unsafe asset name: {asset!r}")
    return asset


def safe_target_path(folder, relative):
    """Resolve relative inside folder, rejecting absolute paths and '..' traversal."""
    if not isinstance(relative, str) or not relative:
        raise ValueError("empty relative path")
    if relative.startswith(("/", "\\")) or ":" in relative:
        raise ValueError(f"unsafe non-relative path: {relative!r}")
    parts = [p for p in relative.replace("\\", "/").split("/") if p not in ("", ".")]
    if not parts or any(p == ".." for p in parts):
        raise ValueError(f"unsafe traversal path: {relative!r}")
    root = pathlib.Path(folder).resolve()
    target = root.joinpath(*parts).resolve()
    if not target.is_relative_to(root) or target == root:
        raise ValueError(f"path escapes destination folder: {relative!r}")
    return target


def _require_https(url):
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme != "https" or not parsed.netloc:
        raise ValueError(f"only https:// URLs are allowed, got: {url!r}")


def _is_retryable_http(err):
    return err.code == 429 or 500 <= err.code < 600


def _verify_md5(actual_hex, expected_md5, url):
    if expected_md5 and actual_hex.lower() != expected_md5.lower():
        raise ValueError(f"MD5 mismatch for {url}: expected {expected_md5}, got {actual_hex}")


def download_bytes(url, user_agent, timeout=120, retries=3, backoff=1.0, expected_md5=None):
    """Download an https:// URL into memory with retries and optional MD5 verification."""
    _require_https(url)
    attempts = max(1, retries + 1)
    last_error = None
    for attempt in range(attempts):
        try:
            request = urllib.request.Request(url, headers={"User-Agent": user_agent})
            with urllib.request.urlopen(request, timeout=timeout) as response:
                data = response.read()
            if expected_md5:
                _verify_md5(hashlib.md5(data).hexdigest(), expected_md5, url)
            return data
        except urllib.error.HTTPError as err:
            last_error = err
            if not _is_retryable_http(err) or attempt + 1 >= attempts:
                raise
        except (urllib.error.URLError, TimeoutError, OSError, ValueError) as err:
            last_error = err
            if attempt + 1 >= attempts:
                raise
        if backoff > 0:
            time.sleep(backoff * (2 ** attempt))
    raise last_error


def download_to_spooled_file(url, user_agent, max_memory=16 * 1024 * 1024, timeout=120, retries=3, backoff=1.0, expected_md5=None):
    """Stream an https:// URL into a SpooledTemporaryFile with retries and optional MD5 check."""
    _require_https(url)
    attempts = max(1, retries + 1)
    last_error = None
    for attempt in range(attempts):
        spool = tempfile.SpooledTemporaryFile(max_size=max_memory)
        try:
            hasher = hashlib.md5() if expected_md5 else None
            request = urllib.request.Request(url, headers={"User-Agent": user_agent})
            with urllib.request.urlopen(request, timeout=timeout) as response:
                while True:
                    chunk = response.read(CHUNK_BYTES)
                    if not chunk:
                        break
                    spool.write(chunk)
                    if hasher is not None:
                        hasher.update(chunk)
            if hasher is not None:
                _verify_md5(hasher.hexdigest(), expected_md5, url)
            spool.seek(0)
            return spool
        except urllib.error.HTTPError as err:
            spool.close()
            last_error = err
            if not _is_retryable_http(err) or attempt + 1 >= attempts:
                raise
        except (urllib.error.URLError, TimeoutError, OSError, ValueError) as err:
            spool.close()
            last_error = err
            if attempt + 1 >= attempts:
                raise
        if backoff > 0:
            time.sleep(backoff * (2 ** attempt))
    raise last_error


def write_atomic(target, data):
    """Write data to a temporary sibling file and atomically replace target."""
    target = pathlib.Path(target)
    target.parent.mkdir(parents=True, exist_ok=True)
    part = target.with_name(f".{target.name}.{os.getpid()}.part")
    try:
        with open(part, "wb") as fh:
            fh.write(data)
        os.replace(part, target)
    except Exception:
        part.unlink(missing_ok=True)
        raise


def is_complete(folder, required_file):
    """Return True if the .complete marker and main required file both exist."""
    folder = pathlib.Path(folder)
    return (folder / COMPLETE_MARKER).is_file() and (folder / required_file).is_file()


def clear_complete(folder):
    """Remove any existing .complete marker before re-downloading."""
    (pathlib.Path(folder) / COMPLETE_MARKER).unlink(missing_ok=True)


def mark_complete(folder, resolution):
    """Write the .complete marker once every file for an asset has been saved."""
    payload = json.dumps({"resolution": resolution}).encode("utf-8")
    write_atomic(pathlib.Path(folder) / COMPLETE_MARKER, payload)
