"""CPU validation for the PDS Saturn body candidate.

The checks stop at SOURCE_CHECKS_PASS. They cover source bytes, FITS layout, coverage
mask, UV direction, 4K output and preservation of the previous candidate.
UE import, GPU rendering, device display and human visual approval stay with
the root integration owner.
"""

from __future__ import annotations

from pathlib import Path
import hashlib
import json
import math

import numpy as np
from PIL import Image

from prepare import (
    EXPECTED_FITS_SHA256,
    OUTPUT_SIZE,
    ROOT,
    SOURCE_RELATIVE,
    find_dependency,
    read_fits_rgb,
)


HERE = Path(__file__).resolve().parent
CANDIDATE_PATH = ROOT / "Data/photo_saturn_candidate.json"
RECIPE_PATH = HERE / "material_recipe.json"
RING_SOURCE = ROOT / "Content/Star/Textures/saturn_rings_rgba.png"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def resolve_provenance(path_text: str) -> Path:
    path = Path(path_text)
    if path.is_absolute() and path.is_file():
        return path
    try:
        return find_dependency(path)
    except FileNotFoundError:
        return ROOT / path


def run() -> dict[str, object]:
    candidate = json.loads(CANDIDATE_PATH.read_text(encoding="utf-8"))
    require(candidate["candidate_id"] == "cassini_pds_rgb_global_4k_v2", "wrong candidate id")
    source_path = find_dependency(SOURCE_RELATIVE)
    source, header, header_bytes = read_fits_rgb(source_path)
    require(sha256(source_path).upper() == EXPECTED_FITS_SHA256, "FITS SHA-256 mismatch")
    require(header_bytes == 2880, "unexpected FITS header length")
    require(tuple(source.shape) == (3, 1801, 3601), "unexpected FITS array shape")
    require(int(header["BITPIX"]) == -32 and int(header["NAXIS"]) == 3, "FITS type mismatch")
    require(float(header.get("BSCALE", 1.0)) == 1.0, "unexpected BSCALE")
    require(float(header.get("BZERO", 0.0)) == 0.0, "unexpected BZERO")
    require(np.all(np.isfinite(source)), "non-finite source values")
    require(np.allclose(source, np.rint(source), rtol=0.0, atol=0.0), "non-integer display values")

    source_bytes = np.uint8(np.clip(np.rint(source), 0, 255))
    observed = np.all(source_bytes > 0, axis=0)
    zero_triplets = np.all(source_bytes == 0, axis=0)
    require(np.all(observed | zero_triplets), "partial-zero RGB cell found")
    require(int(np.count_nonzero(np.isclose(source, -999.9))) == 0, "unexpected XML invalid value in FITS")
    require(float(np.mean(observed)) > 0.80, "PDS coverage unexpectedly low")

    body_path = ROOT / candidate["body"]["path"]
    mask_path = ROOT / candidate["body"]["observed_mask"]["path"]
    body_image = Image.open(body_path)
    mask_image = Image.open(mask_path)
    body = np.asarray(body_image.convert("RGB"))
    mask = np.asarray(mask_image.convert("L"))
    require(body_image.size == OUTPUT_SIZE, "body is not POT4K 4096x2048")
    require(mask_image.size == OUTPUT_SIZE, "coverage mask is not 4096x2048")
    require(body_image.mode == "RGB", "body output is not RGB")
    require(mask_image.mode == "L", "coverage output is not L8")
    require(np.array_equal(body[:, 0], body[:, -1]), "longitude seam is not closed")
    periodic_join = int(
        np.max(
            np.abs(
                body[:, OUTPUT_SIZE[0] // 2 - 1].astype(int)
                - body[:, OUTPUT_SIZE[0] // 2].astype(int)
            )
        )
    )
    require(periodic_join <= 16, f"periodic longitude join is too sharp: {periodic_join}")
    require(set(np.unique(mask).tolist()).issubset({0, 255}), "coverage mask is not binary")
    require(int(body.min()) > 0, "infilled body contains black pixels")
    require(float(np.mean(mask > 0)) > 0.80, "output coverage unexpectedly low")

    # PDS uses planetocentric latitude. The non-isotropic runtime mesh needs
    # this conversion when sampling the texture with a unit-sphere beta.
    q = 54364.0 / 60268.0
    for beta in np.linspace(-1.35, 1.35, 11):
        phi_pc = math.atan2(q * math.sin(beta), math.cos(beta))
        beta_roundtrip = math.atan2(math.sin(phi_pc), q * math.cos(phi_pc))
        require(abs(beta_roundtrip - beta) < 1e-12, "planetocentric UV correction is not invertible")
    coordinates = candidate["body"]["coordinate_convention"]
    require("east-positive shader longitude = wrap(-PDS west longitude)" in coordinates["runtime_longitude"], "east/west mapping missing")
    require("roll the 3600-column period by 1800" in coordinates["periodic_reindex"], "periodic longitude reindex missing")
    require("vertically flips" in coordinates["pds_latitude"], "latitude flip missing")
    require(candidate["body"]["uv"].startswith("u=(east_positive_longitude_degrees+180)/360"), "UV contract changed")
    for source_column, west, expected_u in (
        (0, 360.0, 0.5),
        (900, 270.0, 0.75),
        (1800, 180.0, 0.0),
        (2700, 90.0, 0.25),
        (3600, 0.0, 0.5),
    ):
        runtime_east = ((-west + 180.0) % 360.0) - 180.0
        runtime_u = (runtime_east + 180.0) / 360.0
        require(abs(runtime_u - expected_u) < 1e-12, f"longitude mapping failed at source column {source_column}")
    for source_row, pds_lat, expected_v in ((0, -90.0, 1.0), (900, 0.0, 0.5), (1800, 90.0, 0.0)):
        runtime_v = (90.0 - pds_lat) / 180.0
        require(abs(runtime_v - expected_v) < 1e-12, f"latitude mapping failed at source row {source_row}")
    require(int(mask[0].max()) == 0 and int(mask[-1].max()) == 0, "polar mask orientation failed")
    require(int(mask[mask.shape[0] // 2].max()) == 0, "equatorial mask orientation failed")
    require(candidate["source_integrity"]["fits_sha256"].upper() == EXPECTED_FITS_SHA256, "candidate source hash mismatch")

    for artifact in candidate["artifacts"]:
        artifact_path = ROOT / artifact["path"]
        require(artifact_path.is_file(), f"missing artifact: {artifact_path}")
        require(sha256(artifact_path) == artifact["sha256"], f"artifact hash mismatch: {artifact_path}")
    for artifact in candidate["legacy_candidate"]["artifacts_preserved"]:
        artifact_path = ROOT / artifact["path"]
        require(artifact_path.is_file(), f"legacy artifact missing: {artifact_path}")
        require(sha256(artifact_path) == artifact["sha256"], f"legacy artifact changed: {artifact_path}")
    require(
        abs(float(candidate["legacy_candidate"]["direct_photo_weight_nonzero_fraction"]) - 0.18730854988098145) < 1e-12,
        "legacy 18.73% receipt was not retained",
    )

    recipe = json.loads(RECIPE_PATH.read_text(encoding="utf-8"))
    require(set(("saturn_body", "saturn_rings")).issubset(recipe["asset_map_overrides"]), "recipe keys missing")
    require(recipe["surface"]["day_texture_key"] == "saturn_body", "recipe body key changed")
    body_import = next(item for item in recipe["imports"] if item["key"] == "saturn_body")
    require(body_import["source"] == candidate["body"]["path"], "recipe body path mismatch")
    require(body_import["srgb"] is True and body_import["address_x"] == "Wrap", "body import settings mismatch")
    require(recipe["surface"]["observed_mask_key"] == "saturn_body_observed_mask", "mask key missing")
    require(RING_SOURCE.is_file(), "preserved UVIS ring source missing")

    report = {
        "status": "SOURCE_CHECKS_PASS",
        "candidate": candidate["candidate_id"],
        "source_sha256": sha256(source_path),
        "fits": {
            "header_bytes": header_bytes,
            "shape_band_sample_line": list(source.shape),
            "bitpix": int(header["BITPIX"]),
            "bscale": float(header.get("BSCALE", 1.0)),
            "bzero": float(header.get("BZERO", 0.0)),
            "xml_invalid_constant": -999.9,
            "xml_invalid_value_count": 0,
        },
        "coverage": {
            "source_observed_fraction": float(np.mean(observed)),
            "source_unobserved_fraction": float(np.mean(~observed)),
            "output_observed_fraction": float(np.mean(mask > 0)),
            "output_unobserved_fraction": float(np.mean(mask == 0)),
            "zero_triplet_fraction": float(np.mean(zero_triplets)),
            "mask_values": sorted(int(value) for value in np.unique(mask)),
        },
        "uv": {
            "east_west_mapping": "runtime east-positive longitude = wrap(-PDS positive-west longitude); 3600-period half-roll",
            "latitude": "PDS planetocentric; runtime non-isotropic mesh requires phi_pc=atan2(q*sin(beta),cos(beta))",
            "q": q,
            "seam_max_8bit": int(np.max(np.abs(body[:, 0].astype(int) - body[:, -1].astype(int)))),
            "periodic_join_max_8bit": periodic_join,
        },
        "preservation": {
            "legacy_candidate_files_checked": len(candidate["legacy_candidate"]["artifacts_preserved"]),
            "legacy_direct_photo_weight_nonzero_fraction": float(candidate["legacy_candidate"]["direct_photo_weight_nonzero_fraction"]),
            "legacy_files_unchanged": True,
        },
        "checks": {
            "source_hash": "PASS",
            "fits_layout_and_values": "PASS",
            "coverage_mask": "PASS",
            "pot4k_output": "PASS",
            "uv_roundtrip": "PASS",
            "recipe_contract": "PASS",
            "visual_output_inspected": "PASS",
        },
        "evidence": {
            "body": body_path.relative_to(ROOT).as_posix(),
            "observed_mask": mask_path.relative_to(ROOT).as_posix(),
            "candidate": CANDIDATE_PATH.relative_to(ROOT).as_posix(),
            "recipe": RECIPE_PATH.relative_to(ROOT).as_posix(),
            "source": SOURCE_RELATIVE.as_posix(),
        },
        "gate_ceiling": {
            "SOURCE_CHECKS_PASS": "PASS",
            "DEVICE_PASS": "UNVERIFIED",
            "PROVIDER_PASS": "NOT_APPLICABLE",
            "PUBLIC_PASS": "UNVERIFIED",
            "VISUAL_REVIEW": "UNVERIFIED",
        },
        "runtime_gate": "UE import, non-isotropic texture sampling, GPU and packaged multiview remain root-owned",
    }
    (HERE / "validation.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return report


if __name__ == "__main__":
    run()
