"""Offline verification for the ESO photographic sky candidate."""
from __future__ import annotations

import json
from pathlib import Path

from PIL import Image

from build import (ANCHORS, EQUATORIAL_TO_GALACTIC, EXPECTED_RAW_SHA256,
                   PROVENANCE, RAW, RUNTIME_SIZE, SOURCE_SIZE,
                   galactic_from_radec, photo_uv_from_galactic, sha256,
                   validate_rotation_and_anchors)

ROOT = Path(__file__).resolve().parents[3]
RUNTIME = ROOT / "Content" / "Star" / "Art" / "PhotoSky" / "stars_photo_8k.png"


def main() -> None:
    if sha256(RAW) != EXPECTED_RAW_SHA256:
        raise SystemExit("FAIL source SHA256")
    if Image.open(RAW).size != SOURCE_SIZE:
        raise SystemExit("FAIL source dimensions")
    image = Image.open(RUNTIME)
    if image.size != RUNTIME_SIZE or image.mode != "RGB":
        raise SystemExit(f"FAIL runtime image: {image.mode} {image.size}")
    checks = validate_rotation_and_anchors()
    if set(checks["anchors"]) != set(ANCHORS):
        raise SystemExit("FAIL anchor set")
    record = json.loads(PROVENANCE.read_text(encoding="utf-8"))
    if record["source"]["sha256"] != EXPECTED_RAW_SHA256:
        raise SystemExit("FAIL provenance source hash")
    if record["derived"]["sha256"] != sha256(RUNTIME):
        raise SystemExit("FAIL provenance runtime hash")
    if record["runtime"]["duplicateCatalogStars"] is not False:
        raise SystemExit("FAIL duplicate-star contract")
    print(json.dumps({
        "status": "PASS",
        "source": {"sha256": sha256(RAW), "pixels": SOURCE_SIZE},
        "runtime": {"sha256": sha256(RUNTIME), "pixels": list(image.size)},
        "rotation": "PASS",
        "anchors": checks["anchors"],
        "ue": "NOT_RUN_ROOT_OWNS_UE",
        "gpu": "NOT_RUN_ROOT_OWNS_GPU",
    }, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
