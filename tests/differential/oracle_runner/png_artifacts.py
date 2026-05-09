"""PNG artifact helpers for differential runners."""

from __future__ import annotations

import hashlib
from pathlib import Path
from typing import Any


def _require_pillow():
    try:
        from PIL import Image  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "Pillow is required for decoded RGBA PNG hashing; install "
            "`pip install -r tests/differential/python/requirements-wasm.txt` "
            "or `pip install -r "
            "tests/differential/oracle_runner/requirements-oracle.txt`"
        ) from exc
    return Image


def png_rgba_info(path: Path) -> tuple[int, int, str]:
    """Return width, height, and SHA-256 of decoded RGBA pixels."""
    Image = _require_pillow()
    try:
        with Image.open(path) as image:
            width, height = image.size
            rgba = image.convert("RGBA")
            return (
                int(width),
                int(height),
                hashlib.sha256(rgba.tobytes()).hexdigest(),
            )
    except Exception as exc:
        raise RuntimeError(f"failed to decode PNG as RGBA: {path}") from exc


def rgba_sha256_file(path: Path) -> str:
    return png_rgba_info(path)[2]


def bgra_rgba_sha256_bytes(data: bytes) -> str:
    """Return SHA-256 for BGRA32 bytes interpreted as decoded RGBA pixels."""
    if len(data) % 4 != 0:
        raise RuntimeError(
            f"raw BGRA size is not pixel-aligned: {len(data)} bytes")
    rgba = bytearray(len(data))
    rgba[0::4] = data[2::4]
    rgba[1::4] = data[1::4]
    rgba[2::4] = data[0::4]
    rgba[3::4] = data[3::4]
    return hashlib.sha256(rgba).hexdigest()


def bgra_rgba_sha256_file(path: Path) -> str:
    return bgra_rgba_sha256_bytes(path.read_bytes())


def write_bgra_png(
    *,
    raw_path: Path,
    path: Path,
    width: int,
    height: int,
) -> None:
    """Write a tightly packed BGRA32 raw buffer as a PNG."""
    Image = _require_pillow()
    data = raw_path.read_bytes()
    expected = width * height * 4
    if len(data) != expected:
        raise RuntimeError(
            f"raw BGRA size mismatch for {raw_path}: "
            f"got {len(data)}, expected {expected}"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    image = Image.frombytes("RGBA", (width, height), data, "raw", "BGRA")
    image.save(path, "PNG")


def write_rgba_png(
    *,
    raw_path: Path,
    path: Path,
    width: int,
    height: int,
    bottom_left_origin: bool = False,
) -> None:
    """Write a tightly packed RGBA32 raw buffer as a PNG."""
    Image = _require_pillow()
    data = raw_path.read_bytes()
    expected = width * height * 4
    if len(data) != expected:
        raise RuntimeError(
            f"raw RGBA size mismatch for {raw_path}: "
            f"got {len(data)}, expected {expected}"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    image = Image.frombytes("RGBA", (width, height), data, "raw", "RGBA")
    if bottom_left_origin:
        transpose = getattr(Image, "Transpose", None)
        flip_top_bottom = (
            transpose.FLIP_TOP_BOTTOM if transpose is not None
            else Image.FLIP_TOP_BOTTOM
        )
        image = image.transpose(flip_top_bottom)
    image.save(path, "PNG")


def png_manifest_entry(
    *,
    frame: int,
    path: Path,
    rel: Path,
    phase: str | None = None,
) -> dict[str, Any]:
    width, height, rgba_sha256 = png_rgba_info(path)
    entry: dict[str, Any] = {
        "frame": frame,
        "path": rel.as_posix(),
        "width": width,
        "height": height,
        "bytes": path.stat().st_size,
        "rgbaSha256": rgba_sha256,
    }
    if phase is not None:
        entry["phase"] = phase
    return entry


def image_pixel_hash(image: dict[str, Any]) -> str | None:
    value = image.get("rgbaSha256")
    if isinstance(value, str) and value:
        return value
    return None
