"""Export the authored nozzle subset into the packaged runtime data directory."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
source = ROOT / "Art/Explorer/V2/art_manifest.json"
data = json.loads(source.read_text(encoding="utf-8"))
result = dict(schema_version=1, source="Art/Explorer/V2/art_manifest.json",
              source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
              source_units="meters", frame=data["source_frame"],
              model_parts=[dict(name=x["name"], location_m=x["location_m"])
                           for x in data["model_parts"]
                           if x["name"] in ("SM_EnginePort", "SM_EngineStarboard")],
              rcs=data["rcs"])
assert len(result["model_parts"]) == 2 and len(result["rcs"]) == 16
destination = ROOT / "Content/Star/Data/ship_nozzles.json"
destination.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(destination)
