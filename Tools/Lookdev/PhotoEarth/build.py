"""Build the photographic Earth-day candidate from the pinned BMNG source.

The source is the actual NASA Blue Marble Next Generation September 2004
21600x10800 JPEG.  The runtime candidate is a deterministic 16384x8192 sRGB
Lanczos resample.  Existing Content/Star/Textures/earth_day_8k.jpg is checked
for its pinned digest and is never overwritten by this script.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[3]
# Worktree-local outputs live under ROOT, while the already pinned data worker
# source is shared from the workspace's work/ directory beside all trees.
WORKSPACE = ROOT.parents[2] if ROOT.parent.name == "trees" and ROOT.parent.parent.name == "work" else ROOT
RAW = WORKSPACE / "work" / "trees" / "data" / "work" / "raw" / "world.200409.3x21600x10800.jpg"
RAW_RECEIPT = RAW.with_name(RAW.name + ".receipt.json")
OLD_8K = ROOT / "Content" / "Star" / "Textures" / "earth_day_8k.jpg"
OUT = ROOT / "Content" / "Star" / "Art" / "PhotoEarth"
PROVENANCE = ROOT / "Data" / "photo_earth_provenance.json"
EXPECTED_RAW_SHA256 = "7cf788e13a3a7b4a926b524f8f71d635c56a18c48ba0bfe35695b05a305db3f3"
EXPECTED_OLD_8K_SHA256 = "986af5a1d974d30da6e75d5d00ac583bdc1e262a81c66bdc7f84d205e982ec22"
SOURCE_SIZE = (21600, 10800)
RUNTIME_SIZE = (16384, 8192)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def make_comparison(source: Image.Image, runtime: Image.Image, path: Path) -> None:
    board = Image.new("RGB", (1600, 520), (18, 24, 32))
    draw = ImageDraw.Draw(board)
    panels = [(source, "NASA BMNG September 2004 / source 21600x10800"),
              (runtime, "PhotoEarth runtime candidate / 16384x8192 sRGB")]
    for index, (image, title) in enumerate(panels):
        x = index * 800
        preview = image.convert("RGB")
        preview.thumbnail((770, 450), Image.Resampling.LANCZOS)
        board.paste(preview, (x + (800 - preview.width) // 2, 42))
        draw.text((x + 16, 15), title, fill=(240, 240, 240))
    path.parent.mkdir(parents=True, exist_ok=True)
    board.save(path, format="PNG", compress_level=6)


def srgb_to_linear(value: np.ndarray) -> np.ndarray:
    return np.where(value <= 0.04045, value / 12.92,
                    ((value + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(value: np.ndarray) -> np.ndarray:
    value = np.clip(value, 0.0, 1.0)
    return np.where(value <= 0.0031308, value * 12.92,
                    1.055 * value ** (1 / 2.4) - 0.055)


def resize_srgb(source_path: Path, size: tuple[int, int]) -> Image.Image:
    """Resize one sRGB channel at a time in linear light.

    A channel-at-a-time pass keeps peak allocations bounded for the 233M-pixel
    BMNG input while matching the existing STAR Earth 8K processing contract.
    """
    image = Image.open(source_path).convert("RGB")
    result = np.empty((size[1], size[0], 3), dtype=np.uint8)
    for channel in range(3):
        encoded = np.asarray(image.getchannel(channel), dtype=np.float32) / 255.0
        linear = srgb_to_linear(encoded)
        resized = np.asarray(Image.fromarray(linear).resize(
            size, Image.Resampling.LANCZOS), dtype=np.float32)
        result[:, :, channel] = np.rint(linear_to_srgb(resized) * 255.0).astype(np.uint8)
    return Image.fromarray(result)


def build() -> dict:
    if not RAW.is_file():
        raise FileNotFoundError(f"Pinned BMNG source is missing: {RAW}")
    raw_hash = sha256(RAW)
    if raw_hash != EXPECTED_RAW_SHA256:
        raise ValueError("Pinned BMNG source SHA256 mismatch; refusing to process")
    if RAW_RECEIPT.is_file():
        raw_receipt = json.loads(RAW_RECEIPT.read_text(encoding="utf-8"))
    else:
        raw_receipt = {}
    old_hash_before = sha256(OLD_8K) if OLD_8K.is_file() else None
    if old_hash_before != EXPECTED_OLD_8K_SHA256:
        raise ValueError("Existing earth_day_8k.jpg digest changed or is missing; refusing to overwrite/derive")

    Image.MAX_IMAGE_PIXELS = None
    source = Image.open(RAW).convert("RGB")
    if source.size != SOURCE_SIZE:
        raise ValueError(f"Expected source pixels {SOURCE_SIZE}, got {source.size}")
    # Resize display RGB in linear light and encode back to sRGB. This matches
    # the existing Earth 8K processing contract while remaining a visual
    # resample, not a radiance or albedo reconstruction.
    runtime = resize_srgb(RAW, RUNTIME_SIZE)
    OUT.mkdir(parents=True, exist_ok=True)
    runtime_path = OUT / "earth_day_16k.jpg"
    runtime.save(runtime_path, format="JPEG", quality=96, subsampling=0,
                 optimize=False, progressive=False)
    old_hash_after = sha256(OLD_8K)
    if old_hash_after != old_hash_before:
        raise RuntimeError("Existing earth_day_8k.jpg changed during build")
    comparison_path = OUT / "source_comparison.png"
    make_comparison(source, runtime, comparison_path)

    provenance = {
        "schemaVersion": 1,
        "status": "LOCAL_ASSET_CANDIDATE_NOT_RUNTIME_VALIDATED",
        "source": {
            "id": "earth_day_bmng_september_2004",
            "url": raw_receipt.get("sourceUrl", "https://assets.science.nasa.gov/content/dam/science/esd/eo/images/bmng/bmng-base/september/world.200409.3x21600x10800.jpg"),
            "resolvedUrl": raw_receipt.get("resolvedUrl", "https://assets.science.nasa.gov/content/dam/science/esd/eo/images/bmng/bmng-base/september/world.200409.3x21600x10800.jpg"),
            "rawPath": "work/trees/data/work/raw/world.200409.3x21600x10800.jpg",
            "receiptPath": "work/trees/data/work/raw/world.200409.3x21600x10800.jpg.receipt.json",
            "receiptSha256": sha256(RAW_RECEIPT) if RAW_RECEIPT.is_file() else None,
            "sha256": raw_hash,
            "bytes": RAW.stat().st_size,
            "pixels": list(SOURCE_SIZE),
            "retrievedAt": raw_receipt.get("retrievedAt", "2026-09-06T03:07:30.203227+00:00"),
            "lastModified": raw_receipt.get("lastModified", "Tue, 16 Dec 2025 13:13:17 GMT"),
            "observationDate": "September 2004",
            "units": "display RGB, not radiance",
            "datum": "Earth geographic plate carree centered on 0 longitude; north at top",
            "colorSpace": "sRGB display RGB",
            "credit": "NASA Earth Observatory; Reto Stockli; MODIS science team",
        },
        "derived": {
            "path": "Content/Star/Art/PhotoEarth/earth_day_16k.jpg",
            "sha256": sha256(runtime_path),
            "bytes": runtime_path.stat().st_size,
            "pixels": list(RUNTIME_SIZE),
            "format": "JPEG",
            "jpegQuality": 96,
            "chromaSubsampling": "4:4:4",
            "colorSpace": "sRGB encoded display RGB; linear-light resize then sRGB encode; import UE sRGB=true",
            "process": "sRGB decode, per-channel linear-light Lanczos resize from 21600x10800 to 16384x8192, sRGB encode, JPEG quality 96 4:4:4; no tone mapping, cloud synthesis, sharpening, georegistration, or invented resolution.",
            "resamplingLimit": "POT/mip compatibility and improved source retention only; upsampling adds no observed detail.",
        },
        "preservedExisting": {
            "path": "Content/Star/Textures/earth_day_8k.jpg",
            "sha256": old_hash_after,
            "expectedSha256": EXPECTED_OLD_8K_SHA256,
            "unchanged": old_hash_after == EXPECTED_OLD_8K_SHA256,
        },
        "runtime": {
            "textureKey": "earth_day",
            "photoOverride": "Optional higher-resolution base-color candidate; root decides material instance binding after import readback.",
            "lighting": "Keep existing Sun lighting, exposure, atmosphere, cloud/night layers, fixed epoch and physical Earth radius.",
            "scientificLimit": "Historical cloud-free visual composite; source RGB is not calibrated reflectance or current weather.",
        },
        "artifacts": [
            {
                "path": "Content/Star/Art/PhotoEarth/earth_day_16k.jpg",
                "sha256": sha256(runtime_path),
                "pixels": list(RUNTIME_SIZE),
                "role": "runtime candidate",
            },
            {
                "path": "Content/Star/Art/PhotoEarth/source_comparison.png",
                "sha256": sha256(comparison_path),
                "pixels": list(Image.open(comparison_path).size),
                "role": "source/derived visual audit; not a game capture",
            },
        ],
        "validation": {
            "sourceHash": "PASS",
            "sourceDimensions": "PASS",
            "old8kUnchanged": "PASS",
            "ueImportAndShaderCompile": "NOT_RUN_ROOT_OWNS_UE",
            "gpuRender": "NOT_RUN_ROOT_OWNS_GPU",
            "packagedGame": "NOT_RUN_ROOT_OWNS_PACKAGE",
        },
    }
    write_json(PROVENANCE, provenance)
    return provenance


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.parse_args()
    print(json.dumps(build(), ensure_ascii=False, indent=2))
