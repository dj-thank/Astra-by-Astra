"""Replace only the 13 owned legacy cue assets with the verified Engine V2 WAVs."""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sys
import wave

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import bootstrap


def build_plan():
    manifest = ROOT / "Content/Star/AudioEngineV2/audio_import_manifest.json"
    source = json.loads(manifest.read_text(encoding="utf-8"))
    tasks = []
    if len(source["audioOverrides"]) != 13:
        raise ValueError("Expected precisely 13 existing cue overrides")
    for cue_id, item in source["audioOverrides"].items():
        path = (ROOT / "Content/Star" / item["file"]).resolve()
        if not path.is_relative_to(ROOT / "Content/Star/AudioEngineV2/wav"):
            raise ValueError("Only primary Engine V2 files may be imported")
        sha = hashlib.sha256(path.read_bytes()).hexdigest()
        if sha != item["wavSHA256"]:
            raise ValueError(f"Audio SHA mismatch: {cue_id}")
        with wave.open(str(path), "rb") as wav:
            if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) != (1, 2, 48000):
                raise ValueError(f"Audio format mismatch: {cue_id}")
            actual = dict(sample_rate=48000, duration_seconds=wav.getnframes() / 48000)
        package = f"/Game/Star/Audio/SW_{cue_id}"
        tasks.append(dict(kind="audio", source=str(path), sha256=sha, package=package,
                          object=package + "." + package.rsplit("/", 1)[1],
                          settings=dict(cue=dict(id=cue_id, loop=item["loop"], channels=1,
                                                 sample_rate=48000, bit_depth=16), actual_wav=actual),
                          provenance=dict(manifest=str(manifest.relative_to(ROOT)), asset_id=item["assetId"],
                                          original_mp3_sha256=item["sourceSHA256"], origin=item["origin"])))
    return tasks


def run():
    import unreal
    bootstrap.require_dedicated_editor(ROOT)
    report = dict(startedAt=datetime.now(timezone.utc).isoformat(), status="FAILED", assets=[])
    try:
        tasks = build_plan()
        # Inspect every destination before the first reversible owned-asset write.
        for task in tasks:
            if bootstrap.owned_asset(task["package"], unreal.SoundWave) is None:
                raise RuntimeError(f"Legacy cue does not exist: {task['package']}")
        for task in tasks:
            asset, action = bootstrap.import_asset(task)
            bootstrap.configure_asset(task, asset, {})
            bootstrap.save(asset)
            checked = bootstrap.readback(task, unreal.load_asset(task["object"]))
            checked["action"] = action
            report["assets"].append(checked)
        report["status"] = "AUDIO_V2_IMPORTED_SAVED_READBACK"
        return report
    finally:
        report["finishedAt"] = datetime.now(timezone.utc).isoformat()
        bootstrap.write_json(ROOT / "work/photo-pass/audio-import-result.json", report)


if __name__ == "__main__":
    if bootstrap.unreal is None:
        print(json.dumps(dict(status="SOURCE_PLAN_ONLY", count=len(build_plan()))))
    else:
        print(json.dumps(run()))
