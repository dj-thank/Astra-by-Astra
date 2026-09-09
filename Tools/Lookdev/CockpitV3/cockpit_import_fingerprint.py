"""Pure fingerprint of the exact cockpit import recipe and referenced textures."""
import hashlib
import json
from pathlib import Path


def import_fingerprint(source: Path, manifest_path: Path, importer: Path) -> str:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    names = sorted({spec[key] for spec in manifest["materials"].values()
                    for key in ("base_color_texture", "roughness_texture", "normal_texture") if key in spec})
    digest = hashlib.sha256(b"STAR cockpit recipe 1\0")
    files = [("manifest", manifest_path), ("importer", importer), ("fingerprint", Path(__file__))]
    if (source / "ue_import_expectations_v04.json").exists():
        files.append(("ue topology", source / "ue_import_expectations_v04.json"))
    files += [(name, source / name) for name in names]
    for label, path in files:
        digest.update(label.encode("utf-8") + b"\0" + hashlib.sha256(path.read_bytes()).digest())
    return digest.hexdigest()
