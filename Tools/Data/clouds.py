"""Build one pinned NASA cloud visualization as an 8K linear scalar texture.

Run ``python Tools/Data/clouds.py --download`` to acquire missing source bytes,
then ``python Tools/Data/clouds.py --verify`` to check the committed deliverable.
Without --download, conversion is offline and requires work/clouds source TIFF.
This script deliberately does not change the shared source catalog or manifest.
Requires Pillow and NumPy. All output paths are within this checkout.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
import struct
import urllib.parse
import urllib.request

import numpy as np
from PIL import Image, PngImagePlugin

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / "work/clouds"
RAW = WORK / "cloud_combined_8192.tif"
OUTPUT = ROOT / "Content/Star/Textures/earth_clouds_8k.png"
RECORD = ROOT / "Data/earth_clouds_source.json"
URL = "https://eoimages.gsfc.nasa.gov/images/imagerecords/57000/57747/cloud_combined_8192.tif"
RAW_SHA256 = "d137775d8966ab8d443fd5126dc6e7ad72072bc1ed50555c5818d221735daf0f"
RAW_BYTES = 35_870_468
DOWNLOAD_LIMIT = 200_000_000
SIZE = (8192, 4096)
SEAM_WIDTH = 16


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def save_json(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2, allow_nan=False)
                    + "\n", encoding="utf-8", newline="\n")


def source(download: bool) -> np.ndarray:
    if not RAW.exists():
        if not download:
            raise RuntimeError("Source missing; run with --download to fetch the pinned NASA TIFF")
        WORK.mkdir(parents=True, exist_ok=True)
        partial = RAW.with_suffix(".tif.partial")
        if partial.exists():
            raise RuntimeError(f"Existing partial retained for inspection: {partial}")
        with urllib.request.urlopen(URL, timeout=90) as response:
            host = urllib.parse.urlparse(response.url).hostname or ""
            if not host.endswith(".nasa.gov"):
                raise RuntimeError("Unexpected non-NASA redirect; no source bytes accepted")
            size = int(response.headers.get("Content-Length", "0"))
            if size > DOWNLOAD_LIMIT:
                raise RuntimeError("Source exceeds the 200 MB download limit")
            count = 0
            with partial.open("xb") as stream:
                while chunk := response.read(1024 * 1024):
                    count += len(chunk)
                    if count > DOWNLOAD_LIMIT:
                        raise RuntimeError("Download exceeds the 200 MB limit; partial retained")
                    stream.write(chunk)
        if count != RAW_BYTES or digest(partial) != RAW_SHA256:
            raise RuntimeError("Source differs from pinned NASA bytes; partial retained")
        partial.rename(RAW)
    if RAW.stat().st_size != RAW_BYTES or digest(RAW) != RAW_SHA256:
        raise RuntimeError("Existing source differs from pinned NASA bytes; left unchanged")
    with Image.open(RAW) as image:
        if image.size != SIZE or image.mode != "RGB":
            raise RuntimeError("Unexpected NASA TIFF dimensions or channels")
        rgb = np.asarray(image)
        if not (np.array_equal(rgb[:, :, 0], rgb[:, :, 1])
                and np.array_equal(rgb[:, :, 0], rgb[:, :, 2])):
            raise RuntimeError("Source is no longer neutral grayscale")
        return rgb[:, :, 0].copy()


def coverage(raw: np.ndarray) -> np.ndarray:
    """Identity intensity-to-coverage with a narrow, explicit artistic seam repair."""
    result = raw.copy()
    first = raw[:, :SEAM_WIDTH].astype(np.float32)
    last = raw[:, -SEAM_WIDTH:].astype(np.float32)
    target = (first[:, 0] + last[:, -1]) * 0.5
    t = np.linspace(0.0, 1.0, SEAM_WIDTH, dtype=np.float32)
    weight = 1.0 - t * t * (3.0 - 2.0 * t)
    first += (target - first[:, 0])[:, None] * weight
    last += (target - last[:, -1])[:, None] * weight[::-1]
    result[:, :SEAM_WIDTH] = np.rint(np.clip(first, 0, 255)).astype(np.uint8)
    result[:, -SEAM_WIDTH:] = np.rint(np.clip(last, 0, 255)).astype(np.uint8)
    return result


def statistics(values: np.ndarray) -> dict:
    seam = np.abs(values[:, 0].astype(np.int16) - values[:, -1].astype(np.int16))
    weights = np.cos(np.pi / 2 - (np.arange(SIZE[1]) + 0.5) / SIZE[1] * np.pi)
    return {
        "minMaxByte": [int(values.min()), int(values.max())],
        "meanByte": float(values.mean()),
        "sphereAreaWeightedMeanCoverage": float(np.average(values.mean(axis=1), weights=weights) / 255),
        "fractionAboveHalfCoverage": float((values >= 128).mean()),
        "seamAbsoluteDifferenceByte": {"mean": float(seam.mean()), "p99": float(np.percentile(seam, 99)), "max": int(seam.max())},
    }


def verify() -> None:
    record = json.loads(RECORD.read_text(encoding="utf-8"))
    assert OUTPUT.stat().st_size == record["bytes"], "PNG size differs from provenance"
    assert digest(OUTPUT) == record["sha256"], "PNG hash differs from provenance"
    with Image.open(OUTPUT) as image:
        assert image.mode == "L" and image.size == SIZE, "Expected 8K uint8 grayscale"
        assert image.info.get("gamma") == 1.0, "PNG must declare linear scalar encoding"
        assert "srgb" not in image.info, "Scalar texture must not declare sRGB"
        values = np.asarray(image)
        assert np.array_equal(values[:, 0], values[:, -1]), "Longitude seam does not close"
        assert values.min() == 0 and values.max() == 255, "Coverage dynamic range was lost"
    if RAW.exists():
        raw = source(False)
        assert np.array_equal(values, coverage(raw)), "PNG does not match deterministic conversion"
        assert np.array_equal(values[:, SEAM_WIDTH:-SEAM_WIDTH], raw[:, SEAM_WIDTH:-SEAM_WIDTH]), "Interior source pixels changed"
    print(json.dumps({"verified": True, "sha256": digest(OUTPUT), "bytes": OUTPUT.stat().st_size,
                      "dimensions": list(SIZE), "statistics": statistics(values)}, indent=2))


def build(download: bool) -> None:
    raw = source(download)
    values = coverage(raw)
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    info = PngImagePlugin.PngInfo()
    info.add(b"gAMA", struct.pack(">I", 100_000))
    info.add_text("Description", "NASA Blue Marble clouds; artistic scalar coverage, not optical depth or current weather")
    Image.fromarray(values).save(OUTPUT, pnginfo=info, compress_level=9)
    record = {
        "schemaVersion": 1,
        "id": "earth_clouds",
        "path": OUTPUT.relative_to(ROOT).as_posix(),
        "sha256": digest(OUTPUT),
        "bytes": OUTPUT.stat().st_size,
        "dimensions": list(SIZE),
        "format": "PNG grayscale uint8, one linear scalar channel, gAMA=1.0",
        "sources": [{
            "id": "nasa_blue_marble_clouds_2002",
            "title": "Blue Marble: Clouds",
            "sourceUrl": URL,
            "sourcePage": "https://visibleearth.nasa.gov/images/57747/blue-marble-clouds",
            "sourcePageStatus": "Old Visible Earth page redirects to NASA Science Earth Observatory landing page as checked 2026-09-06; indexed original caption identifies this file family.",
            "currentBackgroundPage": "https://svs.gsfc.nasa.gov/30614/",
            "datasetEpochReferencePage": "https://svs.gsfc.nasa.gov/2709/",
            "sha256": RAW_SHA256,
            "bytes": RAW_BYTES,
            "dimensions": list(SIZE),
            "rawCachePath": RAW.relative_to(ROOT).as_posix(),
            "retrievedAt": dt.datetime.fromtimestamp(RAW.stat().st_mtime, dt.timezone.utc).isoformat(),
            "sourceColorSpace": "Neutral RGB uint8 visual composite; R=G=B verified, no embedded ICC profile or declared transfer function found.",
            "publishedDate": "2002-02-11",
        }],
        "observationDate": "Historical Blue Marble 2002 cloud composite; exact per-pixel observation dates are not present in the TIFF.",
        "observationEpoch": {
            "exactPerPixelDatesKnown": False,
            "composite": "Visible Earth caption describes two days of visible imagery plus a third thermal-infrared day over the poles; NASA SVS 30614 confirms two-day MODIS cloud imagery.",
            "relatedDatasetDates": ["2002-01", "2001-07-29"],
            "relatedDatasetDatesLimit": "NASA SVS 2709 lists these dates for Blue Marble Cloud Cover (dataset 494); assignment to individual pixels of this TIFF is not supplied. Publication date is not an observation timestamp.",
        },
        "units": "dimensionless artistic coverage in [0,1]; value=storedByte/255",
        "datum": "Earth body-fixed, east-positive longitude; visual equirectangular map, no geodetic tags or measured cloud heights in source TIFF",
        "colorSpace": "linear scalar coverage; UE sRGB=false; sample red/grayscale channel",
        "mapping": {
            "projection": "equirectangular",
            "boundsDegrees": {"west": -180, "east": 180, "north": 90, "south": -90},
            "uv": "u=(longitudeRadians+pi)/(2*pi); v=(pi/2-latitudeRadians)/pi",
            "pixelCenter": "lon=-180+(column+0.5)*360/8192; lat=90-(row+0.5)*180/4096",
            "rowOrder": "north-to-south", "columnOrder": "west-to-east",
            "orientationProcessing": "Original global Blue Marble layout retained; no mirror, flip, longitude shift or resampling. Intended to align with NASA BMNG day map and STAR sphere UVs.",
            "runtimeAddressing": "U wrap; V clamp",
        },
        "processing": {
            "script": "Tools/Data/clouds.py",
            "inputChannel": "R (identical to G and B)",
            "contrast": "Identity: no threshold, gain, normalization, sharpening or microdetail generation.",
            "gamma": "Coverage=source grayscale byte/255; gamma exponent=1.0. This is an artistic reinterpretation of visual brightness, not a radiometric conversion.",
            "seam": "First/last 16 columns taper toward their per-row endpoint average using 1-smoothstep(0,1,t); endpoints match exactly. All other pixels remain byte-identical.",
            "maxSeamBandDegreesEachSide": SEAM_WIDTH * 360 / SIZE[0],
            "changedPixelFraction": float((raw != values).mean()),
            "quantization": "round-to-nearest uint8 after seam adjustment; PNG gAMA=1.0; no sRGB chunk",
        },
        "credit": "NASA Goddard Space Flight Center; Reto Stockli (Blue Marble land surface, shallow water and clouds); MODIS Science Team. Blue Marble compositing enhancements by Robert Simmon.",
        "scientificLimit": "Visualization-derived static coverage, not calibrated cloud optical depth, measured cloud fraction, forecast or current weather. Source can contain baked brightness/shading/shadow, compositing and polar visible/infrared differences. Coverage cannot separate these effects or recover 3D cloud thickness. Engine lighting/shadows remain an artistic approximation; raw observation-level masks are unavailable.",
        "knownLimitations": [
            "No new source detail was invented; close range quality is limited to the 8192x4096 source (about 4.9 km per equatorial texel).",
            "Polar distortion and any source compositing artifacts are retained; no claimed exact geodetic registration or pole reconstruction.",
            "Narrow longitude seam repair changes observation-derived pixels and is explicitly artistic.",
            "Use smooth/translucent coverage when fine cloud edges matter; a hard alpha-test threshold discards wispy detail.",
            "Texture inspection does not establish an Unreal import, packaged-game appearance, performance or rendered shadow pass.",
        ],
        "verification": {"raw": statistics(raw), "output": statistics(values)},
    }
    save_json(RECORD, record)
    WORK.mkdir(parents=True, exist_ok=True)
    preview = Image.fromarray(values)
    preview.thumbnail((2048, 1024))
    preview.save(WORK / "coverage-preview.png")
    # Put the antimeridian at the center for visual inspection of wrap continuity.
    band = np.concatenate([values[:, -256:], values[:, :256]], axis=1)
    Image.fromarray(band).resize((512, 1024), Image.Resampling.LANCZOS).save(WORK / "seam-preview.png")
    verify()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--download", action="store_true", help="Acquire the pinned official NASA source if absent")
    parser.add_argument("--verify", action="store_true", help="Verify output bytes and coverage without modifying files")
    arguments = parser.parse_args()
    if arguments.verify:
        verify()
    else:
        build(arguments.download)
