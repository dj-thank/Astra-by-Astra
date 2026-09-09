"""Apply STAR v0.2 photographic materials in a dedicated UE 5.8 editor.

Offline invocation builds the byte-backed source plan and performs no Unreal
work.  When Unreal is available, the script imports only photo textures,
rebuilds owned planet/plume graphs, creates PhotoShip material clones, and
assigns the fourteen candidate materials to matching slots across all V2
parts.  Existing audio overrides are read back by legacy cue id and are never
reimported by this pass.

Root owns editor launch, engine/build/cook, and final visual/runtime gates.
"""
from __future__ import annotations

import hashlib
import importlib.util
import json
import sys
import traceback
from datetime import datetime, timezone
from pathlib import Path

try:
    import unreal
except ModuleNotFoundError:  # pragma: no cover - normal offline path
    unreal = None


ROOT = Path(__file__).resolve().parents[2]
TOOLS_UNREAL = ROOT / "Tools" / "Unreal"
sys.path.insert(0, str(TOOLS_UNREAL))
import import_checks


def _load_photo_ship_author():
    module_path = ROOT / "Tools/Lookdev/PhotoShip/author_unreal.py"
    spec = importlib.util.spec_from_file_location("star_photo_ship_author", module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load PhotoShip author: {module_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _load_planet_builder():
    module_path = ROOT / "Tools/Unreal/Materials/create_materials.py"
    spec = importlib.util.spec_from_file_location("star_photo_planet_materials", module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load planet material builder: {module_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


PHOTO_PASS_OWNER_TAG = "STAR_PhotoPassOwner"
PHOTO_PASS_OWNER = "photo-pass-texture-import-v2"
SOURCE_HASH_TAG = "STAR_PhotoPassSourceSHA256"


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _write_json(path: Path, value):
    path = Path(path).resolve()
    if not path.is_relative_to((ROOT / "work").resolve()):
        raise ValueError(f"Evidence must stay under project/work: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def _configure_texture(asset, settings):
    values = {
        "srgb": bool(settings["srgb"]),
        "compression_settings": getattr(unreal.TextureCompressionSettings, settings["compression"]),
        "virtual_texture_streaming": False,
        "flip_green_channel": bool(settings.get("flip_green_channel", False)),
        "address_x": getattr(unreal.TextureAddress, settings.get("address_x", "TA_WRAP")),
        "address_y": getattr(unreal.TextureAddress, settings.get("address_y", "TA_WRAP")),
        "lod_bias": 0,
        "max_texture_size": int(settings.get("max_texture_size", 0)),
        "never_stream": bool(settings.get("never_stream", False)),
    }
    for key, value in values.items():
        asset.set_editor_property(key, value)
        if asset.get_editor_property(key) != value:
            raise RuntimeError(f"Photo texture {key} readback mismatch: {asset.get_path_name()}")


def _preflight_import_destinations(plan, photo_author):
    """Reject unowned destinations before the first import or graph mutation."""
    for task in plan["tasks"]:
        if task.get("kind") != "photo_texture":
            continue
        asset = unreal.load_asset(task["object"])
        if asset is None:
            continue
        if not isinstance(asset, unreal.Texture2D):
            raise RuntimeError(f"Photo destination is not Texture2D: {task['object']}")
        if "/PhotoShip/Textures/" in task["package"]:
            owner = unreal.EditorAssetLibrary.get_metadata_tag(asset, photo_author.OWNER_TAG)
        else:
            owner = unreal.EditorAssetLibrary.get_metadata_tag(asset, PHOTO_PASS_OWNER_TAG)
        allowed = (photo_author.OWNER,) if "/PhotoShip/Textures/" in task["package"] else (PHOTO_PASS_OWNER,)
        if owner not in allowed:
            raise RuntimeError(f"Photo destination is unowned: {task['object']}")


def _import_generic_texture(task):
    source = Path(task["source"])
    if _sha256(source) != task["sha256"]:
        raise ValueError(f"Photo source changed after plan: {task['sourceRelative']}")
    asset = unreal.load_asset(task["object"])
    if asset is not None and not isinstance(asset, unreal.Texture2D):
        raise RuntimeError(f"Photo destination is not Texture2D: {task['object']}")
    action = "reused"
    if asset is None or unreal.EditorAssetLibrary.get_metadata_tag(asset, SOURCE_HASH_TAG) != task["sha256"]:
        action = "reimported" if asset is not None else "imported"
        job = unreal.AssetImportTask()
        folder, name = task["package"].rsplit("/", 1)
        for key, value in {"filename": str(source), "destination_path": folder, "destination_name": name,
                           "automated": True, "replace_existing": asset is not None,
                           "replace_existing_settings": True, "save": False, "async_": False}.items():
            job.set_editor_property(key, value)
        factory = unreal.TextureFactory()
        factory.set_editor_property("create_material", False)
        job.set_editor_property("factory", factory)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([job])
        asset = unreal.load_asset(task["object"])
        if not isinstance(asset, unreal.Texture2D):
            raise RuntimeError(f"Photo texture import failed: {task['object']}")
    _configure_texture(asset, task["settings"])
    unreal.EditorAssetLibrary.set_metadata_tag(asset, PHOTO_PASS_OWNER_TAG, PHOTO_PASS_OWNER)
    unreal.EditorAssetLibrary.set_metadata_tag(asset, SOURCE_HASH_TAG, task["sha256"])
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Photo texture save failed: {task['object']}")
    return {"asset": asset.get_path_name(), "source": task["sourceRelative"],
            "sourceSha256": task["sha256"], "settings": task["settings"], "action": action}


def _load_asset_path(key, candidates):
    for path in candidates:
        asset = unreal.load_asset(path)
        if isinstance(asset, unreal.Texture2D):
            return path
    raise RuntimeError(f"Missing imported material texture dependency {key}: {candidates}")


def _planet_asset_paths(plan):
    """Resolve existing NASA/Lunar assets plus the freshly imported photo maps."""
    moon_assets = plan["photoMoon"]["assets"]
    plume_assets = plan["photoPlumes"]["textures"]
    values = {
        "earth_day": plan["photographicWorld"]["earth_day"],
        "photo_sky": plan["photographicWorld"]["photo_sky"],
        "earth_clouds": "/Game/Star/Textures/earth_clouds_8k",
        "earth_night": "/Game/Star/Textures/earth_night_8k",
        "moon_albedo": "/Game/Star/Textures/moon_albedo_8k",
        "saturn_body": plan["photoSaturn"]["assets"]["saturn_body"],
        "saturn_rings": plan["photoSaturn"]["assets"]["saturn_rings"],
        "stars": "/Game/Star/Textures/stars_icrf_j2000_8k",
        "lunar_detail": "/Game/Star/Art/Lookdev/Lunar/lunar_detail_linear",
        "lunar_detail_normal": "/Game/Star/Art/Lookdev/Lunar/lunar_detail_normal_dx",
        "apollo17_photo_regional": moon_assets["apollo17_photo_regional"],
        "apollo_soil_detail": moon_assets["apollo_soil_detail"],
        "apollo_soil_normal": moon_assets["apollo_soil_normal"],
        "plume_photo": plume_assets["plume_photo"],
        "plume_axial": plume_assets["plume_axial"],
    }
    for key, path in list(values.items()):
        if not isinstance(unreal.load_asset(path), unreal.Texture2D):
            # NASA aliases are stable but a previous root import may use the
            # semantic key.  Probe only those exact alternate paths.
            aliases = {
                "earth_day": ["/Game/Star/Textures/earth_day"],
                "earth_night": ["/Game/Star/Textures/earth_night"],
                "moon_albedo": ["/Game/Star/Textures/moon_albedo"],
                "stars": ["/Game/Star/Textures/stars"],
                "lunar_detail": ["/Game/Star/Art/Lookdev/Lunar/lunar_detail"],
                "lunar_detail_normal": ["/Game/Star/Art/Lookdev/Lunar/lunar_detail_normal"],
            }.get(key, [])
            values[key] = _load_asset_path(key, [path, *aliases])
    return values


def _verify_audio_overrides(plan):
    audio = plan.get("audio", {})
    if not audio.get("cues"):
        return {"status": audio.get("status", "OPTIONAL_L1_MANIFEST_MISSING"),
                "existingOnly": True, "checked": 0, "reimported": 0}
    checked = []
    for cue in audio["cues"]:
        asset = unreal.load_asset(cue["object"])
        if not isinstance(asset, unreal.SoundWave):
            raise RuntimeError(f"Existing audio override missing: {cue['object']}")
        expected_loop = cue.get("loop")
        if expected_loop is not None and bool(asset.get_editor_property("looping")) != bool(expected_loop):
            raise RuntimeError(f"Existing audio loop flag mismatch: {cue['cueId']}")
        checked.append({"cueId": cue["cueId"], "asset": asset.get_path_name(),
                        "loop": bool(asset.get_editor_property("looping")), "action": "existing_only"})
    return {"status": "AUDIO_OVERRIDES_EXISTING_ONLY_READBACK", "existingOnly": True,
            "checked": len(checked), "reimported": 0, "cues": checked}


def run(root: Path = ROOT, output: Path | None = None) -> dict:
    if unreal is None:
        raise RuntimeError("run requires Unreal Editor Python; use offline_plan() outside UE")
    root = Path(root).resolve()
    if Path(unreal.Paths.project_dir()).resolve() != root:
        raise RuntimeError("Photo pass project mismatch; run from the STAR project")
    try:
        level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        if level_subsystem.is_in_play_in_editor():
            raise RuntimeError("Stop PIE before Photo pass")
    except AttributeError:
        # If a minimal editor build does not expose the subsystem, root still
        # owns the dedicated-process check; do not make an unsupported API a
        # fake success condition.
        pass
    report = {"schemaVersion": 2, "generator": "star-photo-pass-v2",
              "scriptSha256": _sha256(Path(__file__)), "startedAt": datetime.now(timezone.utc).isoformat(),
              "project": str(root), "status": "FAILED", "stage": "source_preflight",
              "runtimeAssetsWritten": False, "cook": "NOT_RUN", "gameplay": "NOT_RUN"}
    try:
        # Build and preflight the complete byte-backed plan before any import,
        # graph clear, clone, or mesh-slot mutation.  Failures here still get
        # a typed receipt from the finally block below.
        plan = import_checks.build_photo_plan(root)
        photo_author = _load_photo_ship_author()
        _preflight_import_destinations(plan, photo_author)
        # Resolve all existing ExplorerV2 meshes, base materials, source paint
        # textures and clone destinations before importing the first photo map.
        photo_author._preflight(root, plan)
        # L1's manifest is existing-only for this script.  Check those
        # SoundWave destinations before any photo import so a missing audio
        # integration cannot leave a half-applied material pass.
        audio_readback = _verify_audio_overrides(plan)
        report["sourcePlan"] = {"candidateCount": plan["photoShip"]["candidateCount"],
                                "functionalCount": plan["photoShip"]["functionalCount"],
                                "mapSetCount": plan["photoShip"]["mapSetCount"],
                                "moonDetailScale": plan["photoMoon"]["DetailScale"],
                                "moonRegionalStrength": plan["photoMoon"]["RegionalStrength"],
                                "saturnAssetMapOverrides": plan["photoSaturn"]["assetMapOverrides"]}
        report["stage"] = "import_photo_textures"
        imports = []
        for task in plan["tasks"]:
            if task.get("kind") != "photo_texture":
                continue
            if "/PhotoShip/Textures/" in task["package"]:
                imports.append(photo_author.import_photo_texture(task))
            else:
                imports.append(_import_generic_texture(task))
        report["imports"] = imports
        report["stage"] = "author_photoship"
        # author_photo_ship has its own all-mesh/all-clone preflight and exact
        # slot rollback.  It consumes the just-imported source-hashed maps.
        report["photoShip"] = photo_author.author_photo_ship(root, plan, imports)
        report["stage"] = "author_planet_and_plumes"
        builder = _load_planet_builder()
        planet_assets = _planet_asset_paths(plan)
        report["planetMaterials"] = builder.build_materials(planet_assets)
        report["audio"] = audio_readback
        report.update({"status": "PHOTO_PASS_AUTHORING_READBACK_ONLY", "stage": "complete",
                       "shaderCompilation": "REQUESTED_NOT_VERIFIED", "ueShader": "ROOT_REQUIRED",
                       "packagedComparison": "NOT_RUN", "gpu": "NOT_RUN", "device": "NOT_RUN"})
        return report
    except Exception as error:
        report["error"] = str(error)
        report["traceback"] = traceback.format_exc()
        raise
    finally:
        report["finishedAt"] = datetime.now(timezone.utc).isoformat()
        if output is None:
            output = root / "work/photo-pass/materials-result.json"
        _write_json(output, report)
        if unreal is not None:
            unreal.log(f"STAR photo pass receipt: {output}; status={report['status']}")


def offline_plan(root: Path = ROOT, output: Path | None = None) -> dict:
    plan = import_checks.build_photo_plan(Path(root).resolve())
    if output is not None:
        _write_json(output, plan)
    return plan


def main():
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, default=ROOT)
    parser.add_argument("--output", type=Path,
                        help="Offline plan or UE receipt under project/work")
    parser.add_argument("--offline", action="store_true",
                        help="Build source plan only even when run beside Unreal")
    args = parser.parse_args()
    if args.offline or unreal is None:
        plan = offline_plan(args.project, args.output)
        print(json.dumps({"status": plan["status"], "photo_texture_tasks": sum(
            task["kind"] == "photo_texture" for task in plan["tasks"]),
            "material_graph_tasks": sum(task["kind"] == "material_graph" for task in plan["tasks"]),
            "photoShipCandidates": plan["photoShip"]["candidateCount"],
            "photoShipMapSets": plan["photoShip"]["mapSetCount"],
            "audioExistingOnly": plan["audio"]["existingOnly"]}, ensure_ascii=False))
        return 0
    report = run(args.project, args.output)
    print(json.dumps({"status": report["status"], "stage": report["stage"],
                      "photoShipAssignments": len(report["photoShip"]["assignments"])}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
