"""Import source-bound V3 EVA waves into the production cue paths."""
from pathlib import Path
import hashlib
import json
import sys
import wave
import unreal

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import bootstrap

bootstrap.require_dedicated_editor(ROOT)
manifest = json.loads((ROOT / "Content/Star/AudioExpansionV3/audio_import_manifest.json").read_text(encoding="utf-8"))
tasks = []
for cue_id, entry in manifest["audioOverrides"].items():
    source = (ROOT / "Content/Star" / entry["file"]).resolve()
    if not source.is_relative_to(ROOT / "Content/Star/AudioExpansionV3"):
        raise ValueError("Audio source outside V3 pack")
    sha = hashlib.sha256(source.read_bytes()).hexdigest()
    if sha != entry["wavSHA256"]:
        raise ValueError(f"Source hash mismatch: {cue_id}")
    with wave.open(str(source), "rb") as wav:
        if (wav.getnchannels(), wav.getframerate(), wav.getsampwidth()) != (1, 48000, 2):
            raise ValueError(f"Unexpected PCM format: {cue_id}")
        actual = dict(sample_rate=48000, duration_seconds=wav.getnframes()/48000)
    package = entry["assetPath"]
    if package != f"/Game/Star/Audio/SW_{cue_id}":
        raise ValueError("Unexpected cue path")
    bootstrap.owned_asset(package, unreal.SoundWave)
    tasks.append(dict(kind="audio", package=package, object=package+"."+package.rsplit("/",1)[1],
        source=str(source), sha256=sha, settings=dict(cue=dict(loop=entry["loop"],channels=1,sample_rate=48000),actual_wav=actual),
        provenance=dict(origin=entry["origin"],sourceSHA256=entry["sourceSHA256"],wavSHA256=sha)))
results = []
for task in tasks:
    asset, action = bootstrap.import_asset(task)
    bootstrap.configure_asset(task, asset, {})
    bootstrap.save(asset)
    results.append(dict(action=action, **bootstrap.readback(task, unreal.load_asset(task["object"]))))
bootstrap.write_json(ROOT / "work/v03/audio-import.json", dict(status="IMPORTED_READBACK", count=len(results), assets=results, runtime_mix="NOT_VERIFIED"))
