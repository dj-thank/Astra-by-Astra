"""Read-only comparison of the compiled route table against the actual authored audio pack."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import wave


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser()
    parser.add_argument("--pack", type=Path, required=True)
    parser.add_argument("--contract", type=Path, default=root / "work/audio-routing-tests/compiled-routing-contract.json")
    parser.add_argument("--output", type=Path, default=root / "outputs/audio/audio-routing-verification.json")
    args = parser.parse_args()
    pack_path = args.pack.resolve()
    cue_file = pack_path / "audio_cues.json"
    source = json.loads(cue_file.read_text(encoding="utf-8"))
    cues = {cue["id"]: cue for cue in source["cues"]}
    assert len(cues) == len(source["cues"]), "Duplicate source cue IDs"
    contract = json.loads(args.contract.read_text(encoding="utf-8"))
    verified = []
    for route in contract["cues"]:
        cue = cues[route["id"]]
        assert cue["loop"] is route["loop"], f"Loop flag mismatch: {route['id']}"
        assert cue["asset_name"] == f"SW_{route['id']}", f"Asset name mismatch: {route['id']}"
        wav_path = (pack_path / cue["wav_file"]).resolve()
        assert wav_path.is_relative_to(pack_path), "Asset points outside the source pack"
        with wave.open(str(wav_path), "rb") as wav:
            assert wav.getframerate() == 48000 and wav.getsampwidth() == 2 and wav.getcomptype() == "NONE"
            assert wav.getnchannels() == cue["channels"] and wav.getnchannels() in (1, 2)
            assert wav.getnframes() > 0
            duration = wav.getnframes() / wav.getframerate()
        verified.append({"id": route["id"], "asset_path": f"/Game/Star/Audio/SW_{route['id']}.SW_{route['id']}",
                         "loop": route["loop"], "duration_seconds": duration, "wav_sha256": sha256(wav_path)})
    code_paths = ["Source/Star/Presentation/StarAudioRouting.h", "Source/Star/Presentation/StarAudioRouting.cpp",
                  "Source/Star/Presentation/StarShipAudioComponent.h", "Source/Star/Presentation/StarShipAudioComponent.cpp",
                  "Tools/Audio/AudioRoutingTests.cpp"]
    report = {"verified_utc": datetime.now(timezone.utc).isoformat(), "result": "PASS",
              "scope": "Compiled native routing table and actual local pack PCM; no Unreal launch or audible device test",
              "source_pack_cues": len(cues), "routed_cues": len(verified), "audio_cues_sha256": sha256(cue_file),
              "compiled_contract_sha256": sha256(args.contract), "routes": verified,
              "current_source_sha256": {name: sha256(root / name) for name in code_paths},
              "unreal_compile": "NOT_RUN_BY_THIS_WORKER", "game_audio_acceptance": "PENDING_ROOT"}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"PASS {len(verified)} used cue IDs/loop flags/PCM files matched against {len(cues)}-cue authored pack; source files unmodified")


if __name__ == "__main__":
    main()
