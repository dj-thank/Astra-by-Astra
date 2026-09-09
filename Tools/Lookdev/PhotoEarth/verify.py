"""Offline verification for the BMNG photographic Earth-day candidate."""
from __future__ import annotations

import json
from pathlib import Path

from PIL import Image

from build import (EXPECTED_OLD_8K_SHA256, EXPECTED_RAW_SHA256, OLD_8K,
                   PROVENANCE, RAW, RUNTIME_SIZE, SOURCE_SIZE, sha256)

ROOT = Path(__file__).resolve().parents[3]
RUNTIME = ROOT / "Content" / "Star" / "Art" / "PhotoEarth" / "earth_day_16k.jpg"


def main() -> None:
    if sha256(RAW) != EXPECTED_RAW_SHA256:
        raise SystemExit("FAIL source SHA256")
    Image.MAX_IMAGE_PIXELS = None
    source = Image.open(RAW)
    if source.size != SOURCE_SIZE or source.mode != "RGB":
        raise SystemExit(f"FAIL source image: {source.mode} {source.size}")
    runtime = Image.open(RUNTIME)
    if runtime.size != RUNTIME_SIZE or runtime.mode != "RGB":
        raise SystemExit(f"FAIL runtime image: {runtime.mode} {runtime.size}")
    old_hash = sha256(OLD_8K)
    if old_hash != EXPECTED_OLD_8K_SHA256:
        raise SystemExit("FAIL existing 8K Earth map changed")
    record = json.loads(PROVENANCE.read_text(encoding="utf-8"))
    if record["source"]["sha256"] != EXPECTED_RAW_SHA256:
        raise SystemExit("FAIL provenance source hash")
    if record["derived"]["sha256"] != sha256(RUNTIME):
        raise SystemExit("FAIL provenance runtime hash")
    if record["preservedExisting"]["sha256"] != old_hash:
        raise SystemExit("FAIL provenance old 8K hash")
    print(json.dumps({
        "status": "PASS",
        "source": {"sha256": sha256(RAW), "pixels": list(source.size)},
        "runtime": {"sha256": sha256(RUNTIME), "pixels": list(runtime.size)},
        "old8k": {"sha256": old_hash, "unchanged": True},
        "ue": "NOT_RUN_ROOT_OWNS_UE",
        "gpu": "NOT_RUN_ROOT_OWNS_GPU",
    }, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
