"""Build the optional photographic sky from the supplied ESO panorama.

The source is a 6000x3000 ground-based optical panorama.  The runtime PNG is
an sRGB Lanczos resample to 8192x4096 for texture/mip compatibility; it does
not add catalogue stars, inpaint pixels, or claim astrometric precision.

This script is intentionally offline and fail-closed.  It reads the pinned
source and receipt under ``work/photo-pass`` and never downloads or starts UE.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[3]
# Worktree-local outputs live under ROOT, while the already pinned source
# receipt is shared from the workspace's work/ directory beside all trees.
WORKSPACE = ROOT.parents[2] if ROOT.parent.name == "trees" and ROOT.parent.parent.name == "work" else ROOT
RAW = WORKSPACE / "work" / "photo-pass" / "raw" / "eso0932a.tif"
SOURCE_RECEIPT = WORKSPACE / "work" / "photo-pass" / "eso-source.json"
OUT = ROOT / "Content" / "Star" / "Art" / "PhotoSky"
PROVENANCE = ROOT / "Data" / "photo_sky_provenance.json"
RECIPE = OUT / "material_recipe.json"
EXPECTED_RAW_SHA256 = "10f209ab83e1fd89e7fa1ed70277ffc6ed19c43549f04ec637c6806d98aff035"
SOURCE_SIZE = (6000, 3000)
RUNTIME_SIZE = (8192, 4096)

# J2000 equatorial -> Galactic rotation from
# SPICE pxform('J2000','GALACTIC',0), CSPICE_N0067.  It is constant for the
# epoch range used here.  This is a registration aid, not catalogue astrometry.
EQUATORIAL_TO_GALACTIC = (
    (-0.0548755393957425, -0.873437104727596, -0.483834991770025),
    (0.494109453627744, -0.444829594297575, 0.746982248699892),
    (-0.867666135683374, -0.198076389613020, 0.455983794521420),
)
OBLIQUITY_DEGREES = 23.439291111

# Independent bright-neighbour anchors.  The coordinates are only used to
# check the rotation and pixel-centre formula; they do not label individual
# stars in the photographic source.
ANCHORS = {
    "LMC": {
        "raDeg": 80.8939,
        "decDeg": -69.7561,
        "expectedGalacticLDeg": 280.465,
        "expectedGalacticBDeg": -32.888,
    },
    "SMC": {
        "raDeg": 13.1867,
        "decDeg": -72.8286,
        "expectedGalacticLDeg": 302.797,
        "expectedGalacticBDeg": -44.299,
    },
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def vector_length(value: tuple[float, float, float]) -> float:
    return math.sqrt(sum(component * component for component in value))


def matrix_vector(matrix, value):
    return tuple(sum(matrix[row][column] * value[column] for column in range(3)) for row in range(3))


def galactic_from_radec(ra_deg: float, dec_deg: float) -> tuple[float, float]:
    ra = math.radians(ra_deg)
    dec = math.radians(dec_deg)
    equatorial = (
        math.cos(dec) * math.cos(ra),
        math.cos(dec) * math.sin(ra),
        math.sin(dec),
    )
    galactic = matrix_vector(EQUATORIAL_TO_GALACTIC, equatorial)
    longitude = math.degrees(math.atan2(galactic[1], galactic[0])) % 360.0
    latitude = math.degrees(math.asin(max(-1.0, min(1.0, galactic[2]))))
    return longitude, latitude


def photo_uv_from_galactic(longitude_deg: float, latitude_deg: float) -> tuple[float, float]:
    # The source contract is stated in source pixel-centre coordinates:
    # l=(3050-(x+0.5))*360/6000 and b=(1475-(y+0.5))*180/3000.
    u = (3050.0 / SOURCE_SIZE[0] - longitude_deg / 360.0) % 1.0
    v = 1475.0 / SOURCE_SIZE[1] - latitude_deg / 180.0
    return u, max(0.0, min(1.0, v))


def validate_rotation_and_anchors() -> dict:
    rows = [tuple(row) for row in EQUATORIAL_TO_GALACTIC]
    row_lengths = [vector_length(row) for row in rows]
    row_dots = [sum(rows[a][i] * rows[b][i] for i in range(3))
                for a, b in ((0, 1), (0, 2), (1, 2))]
    if max(abs(length - 1.0) for length in row_lengths) > 1e-9:
        raise ValueError("J2000->Galactic matrix row is not unit length")
    if max(abs(dot) for dot in row_dots) > 1e-9:
        raise ValueError("J2000->Galactic matrix is not orthogonal")

    checks = {}
    for name, anchor in ANCHORS.items():
        longitude, latitude = galactic_from_radec(anchor["raDeg"], anchor["decDeg"])
        if abs(((longitude - anchor["expectedGalacticLDeg"] + 180.0) % 360.0) - 180.0) > 0.02:
            raise ValueError(f"{name} Galactic longitude anchor mismatch")
        if abs(latitude - anchor["expectedGalacticBDeg"]) > 0.02:
            raise ValueError(f"{name} Galactic latitude anchor mismatch")
        u, v = photo_uv_from_galactic(longitude, latitude)
        checks[name] = {
            "inputRaDeg": anchor["raDeg"],
            "inputDecDeg": anchor["decDeg"],
            "computedGalacticLDeg": round(longitude, 9),
            "computedGalacticBDeg": round(latitude, 9),
            "photoUv": [round(u, 9), round(v, 9)],
            "toleranceDeg": 0.02,
        }
    return {
        "matrix": [list(row) for row in rows],
        "rowLengths": row_lengths,
        "pairwiseRowDots": row_dots,
        "anchors": checks,
    }


def make_comparison(source: Image.Image, runtime: Image.Image, path: Path) -> None:
    board = Image.new("RGB", (1600, 520), (20, 23, 29))
    draw = ImageDraw.Draw(board)
    panels = [(source, "ESO observed panorama 6000x3000"),
              (runtime, "Runtime resample 8192x4096 / sRGB")]
    for index, (image, title) in enumerate(panels):
        x = index * 800
        preview = image.convert("RGB")
        preview.thumbnail((770, 450), Image.Resampling.LANCZOS)
        board.paste(preview, (x + (800 - preview.width) // 2, 42))
        draw.text((x + 16, 15), title, fill=(240, 240, 240))
    path.parent.mkdir(parents=True, exist_ok=True)
    board.save(path, format="PNG", compress_level=6)


def build() -> dict:
    if not RAW.is_file():
        raise FileNotFoundError(f"Pinned ESO source is missing: {RAW}")
    if sha256(RAW) != EXPECTED_RAW_SHA256:
        raise ValueError("Pinned ESO source SHA256 mismatch; refusing to process")
    if not SOURCE_RECEIPT.is_file():
        raise FileNotFoundError(f"ESO source receipt is missing: {SOURCE_RECEIPT}")
    source_receipt = json.loads(SOURCE_RECEIPT.read_text(encoding="utf-8"))
    source = Image.open(RAW)
    if source.size != SOURCE_SIZE or source.mode not in {"RGB", "RGBA"}:
        raise ValueError(f"Expected RGB {SOURCE_SIZE}, got {source.mode} {source.size}")
    source_rgb = source.convert("RGB")
    runtime = source_rgb.resize(RUNTIME_SIZE, Image.Resampling.LANCZOS)

    OUT.mkdir(parents=True, exist_ok=True)
    runtime_path = OUT / "stars_photo_8k.png"
    save_kwargs = {"format": "PNG", "compress_level": 6}
    if source.info.get("icc_profile"):
        save_kwargs["icc_profile"] = source.info["icc_profile"]
    runtime.save(runtime_path, **save_kwargs)
    comparison_path = OUT / "source_comparison.png"
    make_comparison(source_rgb, runtime, comparison_path)

    rotation = validate_rotation_and_anchors()
    provenance = {
        "schemaVersion": 1,
        "status": "LOCAL_ASSET_CANDIDATE_NOT_RUNTIME_VALIDATED",
        "source": {
            "id": "eso0932a",
            "url": source_receipt.get("url", "https://cdn.eso.org/images/original/eso0932a.tif"),
            "page": source_receipt.get("page", "https://www.eso.org/public/images/eso0932a/"),
            "rawPath": "work/photo-pass/raw/eso0932a.tif",
            "receiptPath": "work/photo-pass/eso-source.json",
            "receiptSha256": sha256(SOURCE_RECEIPT),
            "sha256": sha256(RAW),
            "pixels": list(SOURCE_SIZE),
            "retrievedAt": source_receipt.get("retrieved", "2026-09-06T12:39:55.510331+00:00"),
            "credit": source_receipt.get("credit", "ESO/S. Brunier"),
            "license": source_receipt.get("license", "CC BY 4.0"),
            "licensePage": source_receipt.get("licensePage", "https://www.eso.org/public/outreach/copyright/"),
            "observed": source_receipt.get("observed", "Ground-based optical photographic all-sky mosaic; not a current sky snapshot"),
        },
        "derived": {
            "path": "Content/Star/Art/PhotoSky/stars_photo_8k.png",
            "sha256": sha256(runtime_path),
            "bytes": runtime_path.stat().st_size,
            "pixels": list(RUNTIME_SIZE),
            "format": "PNG",
            "colorSpace": "sRGB encoded display RGB",
            "process": "Source RGB resized with Lanczos from 6000x3000 to 8192x4096; no tone mapping, white-balance change, inpainting, sharpening, or catalogue-star synthesis.",
            "resamplingLimit": "POT/mip compatibility only; upsampling adds no observed detail.",
        },
        "registration": {
            "projection": "Galactic CAR/equirectangular approximation",
            "sourcePixelCenterFormula": {
                "longitudeDeg": "(3050-(x+0.5))*360/6000",
                "latitudeDeg": "(1475-(y+0.5))*180/3000",
            },
            "uvFormula": "u=fract(3050/6000-l/360), v=1475/3000-b/180",
            "approximateDistortion": "about 1 degree; source panorama is visually registered and is not star astrometry",
            "rotationSource": "SPICE pxform('J2000','GALACTIC',0) using CSPICE_N0067; fixed validated matrix",
            "equatorialToGalacticRowMajor": rotation["matrix"],
            "obliquityDegrees": OBLIQUITY_DEGREES,
            "anchorChecks": rotation["anchors"],
        },
        "runtime": {
            "material": "M_Stars",
            "textureKey": "photo_sky",
            "shaderInputs": ["StarTex", "StarIntensity", "PhotoSkyTex", "EnhancedStars", "PhotoSkyIntensity", "EyeExposure"],
            "enhancedStars": "Cosmetic photo replacement branch; disabled mode samples only the original NASA ICRF/J2000 catalogue.",
            "duplicateCatalogStars": False,
            "defaultEnhancedStars": True,
            "photoSkyIntensity": 0.08,
            "eyeExposure": "Positive MaterialExpressionEyeAdaptation exposure multiplier; shader uses reciprocal clamped to 0.001..1000000.0 for artistic brightness compensation only.",
            "catalogueAssetPreserved": "Content/Star/Textures/stars_icrf_j2000_8k.exr",
        },
        "artifacts": [
            {
                "path": "Content/Star/Art/PhotoSky/stars_photo_8k.png",
                "sha256": sha256(runtime_path),
                "pixels": list(RUNTIME_SIZE),
                "role": "runtime candidate",
            },
            {
                "path": "Content/Star/Art/PhotoSky/source_comparison.png",
                "sha256": sha256(comparison_path),
                "pixels": list(Image.open(comparison_path).size),
                "role": "source/derived visual audit; not a game capture",
            },
        ],
        "validation": {
            "sourceHash": "PASS",
            "sourceDimensions": "PASS",
            "rotationAndAnchors": "PASS",
            "ueShaderCompile": "NOT_RUN_ROOT_OWNS_UE",
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
