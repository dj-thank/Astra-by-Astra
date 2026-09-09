"""Offline, byte-backed STAR import plan. No Unreal import or asset writes."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import wave
from pathlib import Path

MAP_PATH = "/Game/Star/Maps/L_SolarSystem"
GAME_MODE = "/Script/Star.StarGameMode"
ART_BASE = "Content/Star/Art/ExplorerV2"
PHOTO_MOON_RECIPE = "Tools/Lookdev/PhotoMoon/material_recipe.json"
PHOTO_SATURN_RECIPE = "Tools/Lookdev/PhotoSaturn/material_recipe.json"
PHOTO_SHIP_BINDINGS = "Data/photo_ship_bindings.json"
PHOTO_PLUME_RECIPE = "Data/photo_plumes_vacuum.json"
PHOTO_PLUME_INTEGRATION = "Tools/Lookdev/PhotoPlumes/INTEGRATION.md"
PHOTO_MOON_DETAIL_SCALE = 26.191119735637812
PHOTO_MOON_REGIONAL_STRENGTH = 0.65
PHOTO_SHIP_CANDIDATE_COUNT = 14
PHOTO_SHIP_FUNCTIONAL_COUNT = 7
PHOTO_SHIP_MAP_SET_COUNT = 7
PHOTO_PLUME_TEXTURES = {
    "plume_photo": ("T_VacuumPlumePhoto.png", "/Game/Star/Art/PhotoPlumes/T_VacuumPlumePhoto"),
    "plume_axial": ("T_VacuumPlumeAxial.png", "/Game/Star/Art/PhotoPlumes/T_VacuumPlumeAxial"),
}
NASA_KEYS = {
    "earth_day": "earth_day_8k", "earth_night": "earth_night_8k",
    "moon_albedo": "moon_albedo_8k", "saturn_body": "saturn_body_reference",
    "saturn_rings": "saturn_rings_rgba", "stars": "stars_icrf_j2000_8k",
}
OPTIONAL_NASA_KEYS = {"earth_clouds": "earth_clouds_8k"}


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def source_path(base, relative):
    """Reject absolute/traversing manifest paths, including Windows paths."""
    if not isinstance(relative, str) or "\\" in relative or ":" in relative:
        raise ValueError(f"Non-portable source path: {relative!r}")
    rel = Path(relative)
    base = Path(base).resolve()
    path = (base / rel).resolve()
    if rel.is_absolute() or not path.is_relative_to(base) or not path.is_file():
        raise ValueError(f"Missing/out-of-bound source: {relative}")
    return path


def name(value):
    if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", value):
        raise ValueError(f"Invalid Unreal asset name: {value!r}")
    return value


def vector(value, dimensions=3):
    if not isinstance(value, list) or len(value) != dimensions or any(
            isinstance(x, bool) or not isinstance(x, (int, float)) or not math.isfinite(x) for x in value):
        raise ValueError(f"Expected finite {dimensions}-vector: {value!r}")
    return [float(x) for x in value]


def object_path(package):
    if not package.startswith("/Game/") or "." in package:
        raise ValueError(f"Invalid package path: {package}")
    return package + "." + package.rsplit("/", 1)[1]


def runtime_assets(art):
    """Ship-local UE axes already match; only meters -> centimeters here."""
    parts = []
    seen = set()
    for part in art["model_parts"]:
        key = name(part["name"])
        if key in seen or not part.get("geometry_rotation_and_scale_baked"):
            raise ValueError(f"Duplicate or unbaked part: {key}")
        seen.add(key)
        role = "gear" if key in art["gear"]["pivots_m"] else (
            "glass" if any(art["materials"][m].get("transmission", 0) > 0 for m in part["materials"]) else "hull")
        parts.append({"name": key, "asset": object_path(f"/Game/Star/Art/ExplorerV2/Parts/{key}"),
                      "role": role, "locationCm": [x * 100 for x in vector(part["location_m"])]})
    if not parts or len(parts) != art["mesh_count"]:
        raise ValueError("Art manifest mesh_count does not match unique parts")
    sockets = art["sockets"]
    displays = []
    # Center screen is Flight (runtime gives the first display this mode).
    display_keys = sorted(k for k in sockets if k.startswith("SOCKET_Display"))
    display_keys.sort(key=lambda k: abs(sockets[k]["location_m"][1]))
    for key in display_keys:
        spec = sockets[key]
        width, height = vector(spec["size_m"], 2)
        if width <= 0 or height <= 0:
            raise ValueError(f"Invalid instrument size: {key}")
        displays.append({"name": key.removeprefix("SOCKET_"),
                         "locationCm": [x * 100 for x in vector(spec["location_m"])],
                         "widthM": width, "heightM": height})
    return {"schemaVersion": 1, "shipParts": parts,
            "sockets": {"cockpitCameraCm": [x * 100 for x in vector(sockets["SOCKET_CockpitCamera"]["location_m"])],
                        "chaseCameraCm": [x * 100 for x in vector(sockets["SOCKET_ChaseCamera"]["location_m"])]},
            "instrumentDisplays": displays}


def inspect_wav(path, cue):
    with wave.open(str(path), "rb") as audio:
        actual = {"channels": audio.getnchannels(), "sample_rate": audio.getframerate(),
                  "bit_depth": audio.getsampwidth() * 8, "frames": audio.getnframes(),
                  "compression": audio.getcomptype()}
    if actual["compression"] != "NONE" or actual["bit_depth"] != 16 or actual["frames"] <= 0:
        raise ValueError(f"WAV must be nonempty PCM16: {path}")
    for key in ("channels", "sample_rate", "bit_depth"):
        if actual[key] != cue[key]:
            raise ValueError(f"WAV {key} mismatch for {cue['id']}: {actual[key]} != {cue[key]}")
    if actual["channels"] not in (1, 2) or type(cue["loop"]) is not bool:
        raise ValueError(f"Unsupported channels/loop for {cue['id']}")
    actual["duration_seconds"] = actual["frames"] / actual["sample_rate"]
    return actual


def _finite(value, label):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"Expected finite number for {label}: {value!r}")
    return float(value)


def _photo_task(root, source_relative, package, settings, provenance):
    """Create one byte-backed photo import task with portable provenance."""
    path = source_path(root, source_relative)
    name(package.rsplit("/", 1)[1])
    return {"kind": "photo_texture", "source": str(path), "sourceRelative": source_relative,
            "package": package, "object": object_path(package), "sha256": sha256(path),
            "settings": settings, "provenance": provenance}


def _photo_texture_settings(spec, kind):
    settings = {
        "srgb": bool(spec.get("srgb", False)),
        "compression": spec.get("compression", "TC_MASKS"),
        "normal": kind in ("normal", "normal_dx"),
        "flip_green_channel": bool(spec.get("flip_green", False)),
        "address_x": spec.get("address_x", "TA_WRAP"),
        "address_y": spec.get("address_y", "TA_WRAP"),
        "mips": bool(spec.get("mips", True)),
    }
    if settings["normal"] and settings["flip_green_channel"]:
        raise ValueError("PhotoShip DX normals must not flip green during import")
    return settings


def _audio_manifest_entries(root, relative):
    """Read L1's optional existing-audio manifest without importing anything."""
    path = source_path(root, relative)
    value = read_json(path)
    if isinstance(value, dict):
        entries = value.get("cues", value.get("entries"))
        if entries is None and isinstance(value.get("audioOverrides"), dict):
            # L1's canonical manifest is keyed by the legacy cue id.  Keep
            # that id for the existing SW_<cue_id> asset and retain loop/file
            # metadata for root's later readback; no WAV is imported here.
            entries = [{"cue_id": cue_id, **spec} for cue_id, spec in value["audioOverrides"].items()]
        if entries is None:
            entries = value.get("assets", [])
    else:
        entries = value
    if not isinstance(entries, list):
        raise ValueError("Audio import manifest must contain a cues/assets/entries list")
    result = []
    seen = set()
    for entry in entries:
        if not isinstance(entry, dict):
            raise ValueError("Audio import manifest entry must be an object")
        cue_id = entry.get("cue_id", entry.get("cueId", entry.get("id")))
        if not isinstance(cue_id, str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", cue_id):
            raise ValueError(f"Invalid audio cue id: {cue_id!r}")
        if cue_id in seen:
            raise ValueError(f"Duplicate audio cue id: {cue_id}")
        seen.add(cue_id)
        asset = entry.get("asset", entry.get("asset_path", entry.get("package")))
        expected = f"/Game/Star/Audio/SW_{cue_id}"
        if asset is not None and asset != expected and asset != object_path(expected):
            raise ValueError(f"Audio manifest asset mismatch for {cue_id}: {asset!r}")
        result.append({"cueId": cue_id, "object": object_path(expected),
                       "package": expected, "loop": entry.get("loop", entry.get("looping")),
                       "source": entry.get("source", entry.get("wav_file", entry.get("file")))})
    return {"manifest": relative, "manifestSha256": sha256(path), "existingOnly": True,
            "cues": result}


def build_photo_plan(root, audio_manifest=None):
    """Build the v0.2 photo/material plan without Unreal or asset writes.

    The returned tasks are complete enough for ``apply_photo_pass.py`` to
    import existing source bytes.  It intentionally never schedules the old
    91-cue audio pack; an L1 manifest, when present, is read as existing-only
    SoundWave readback metadata.
    """
    root = Path(root).resolve()
    art_path = source_path(root, "Art/Explorer/V2/art_manifest.json")
    art = read_json(art_path)
    if art.get("version") != 2 or art.get("source_units") != "meters" or art.get("source_frame") != {
            "forward": "+X", "right": "+Y", "up": "+Z"}:
        raise ValueError("PhotoShip requires ASTER-24 V2 meter/+X,+Y,+Z art manifest")
    recipes = {
        "moon": source_path(root, PHOTO_MOON_RECIPE),
        "saturn": source_path(root, PHOTO_SATURN_RECIPE),
        "ship": source_path(root, PHOTO_SHIP_BINDINGS),
        "plumes": source_path(root, PHOTO_PLUME_RECIPE),
        "plumeIntegration": source_path(root, PHOTO_PLUME_INTEGRATION),
    }
    moon = read_json(recipes["moon"])
    if moon.get("material") != "M_Surface" or set(moon.get("textures", {})) != {
            "RegionalTex", "DetailTex", "DetailNormalTex"}:
        raise ValueError("PhotoMoon recipe must expose RegionalTex, DetailTex and DetailNormalTex")
    moon_scalars = moon.get("scalars", {})
    if abs(_finite(moon_scalars.get("RegionalStrength"), "Moon RegionalStrength") - PHOTO_MOON_REGIONAL_STRENGTH) > 1e-9:
        raise ValueError("PhotoMoon RegionalStrength drifted from 0.65")
    if abs(_finite(moon_scalars.get("DetailScale"), "Moon DetailScale") - PHOTO_MOON_DETAIL_SCALE) > 1e-12:
        raise ValueError("PhotoMoon DetailScale drifted from 26.191119735637812")
    tasks = []
    moon_assets = {}
    for parameter, spec in moon["textures"].items():
        if not isinstance(spec, dict) or not isinstance(spec.get("file"), str) or not isinstance(spec.get("key"), str):
            raise ValueError(f"Invalid PhotoMoon texture spec: {parameter}")
        expected = {
            "RegionalTex": {"srgb": False, "compression": "TC_MASKS", "address_x": "TA_CLAMP", "address_y": "TA_CLAMP"},
            "DetailTex": {"srgb": False, "compression": "TC_MASKS", "address_x": "TA_WRAP", "address_y": "TA_WRAP"},
            "DetailNormalTex": {"srgb": False, "compression": "TC_NORMALMAP", "address_x": "TA_WRAP", "address_y": "TA_WRAP",
                                 "flip_green": False},
        }[parameter]
        if any(spec.get(key) != value for key, value in expected.items()):
            raise ValueError(f"PhotoMoon import settings drifted: {parameter}")
        package = f"/Game/Star/Art/PhotoMoon/{Path(spec['file']).stem}"
        channel = "normal_dx" if parameter == "DetailNormalTex" else parameter.lower().replace("tex", "")
        task = _photo_task(root, spec["file"], package,
                           _photo_texture_settings(spec, channel),
                           {"recipe": PHOTO_MOON_RECIPE, "parameter": parameter, "spec": spec})
        tasks.append(task)
        moon_assets[spec["key"]] = task["object"]

    saturn = read_json(recipes["saturn"])
    saturn_overrides = saturn.get("asset_map_overrides")
    if not isinstance(saturn_overrides, dict) or set(saturn_overrides) != {"saturn_body", "saturn_rings"}:
        raise ValueError("PhotoSaturn asset-map overrides must keep saturn_body/saturn_rings stable")
    expected_saturn = saturn_overrides
    if any(not re.fullmatch(r"/Game/Star/Art/PhotoSaturn/[A-Za-z0-9_]+", value) for value in expected_saturn.values()):
        raise ValueError("PhotoSaturn destinations must remain in the owned photo folder")
    saturn_assets = {}
    for spec in saturn.get("imports", []):
        if spec.get("runtime_connection") == "audit_only":
            continue
        if spec.get("asset") not in expected_saturn.values():
            raise ValueError(f"Unexpected PhotoSaturn destination: {spec.get('asset')}")
        package = spec["asset"]
        task = _photo_task(root, spec["source"], package,
                           {"srgb": bool(spec.get("srgb", True)), "compression": "TC_DEFAULT",
                            "normal": False, "flip_green_channel": False,
                            "address_x": "TA_WRAP" if spec.get("address_x") == "Wrap" else "TA_CLAMP",
                            "address_y": "TA_WRAP" if spec.get("address_y") == "Wrap" else "TA_CLAMP",
                            "mips": True},
                           {"recipe": PHOTO_SATURN_RECIPE, "spec": spec})
        tasks.append(task)
        key = next(key for key, destination in expected_saturn.items() if destination == package)
        saturn_assets[key] = task["object"]
    if set(saturn_assets) != set(expected_saturn):
        raise ValueError("PhotoSaturn recipe must import body and rings")

    bindings = read_json(recipes["ship"])
    if bindings.get("version") != 2 or not isinstance(bindings.get("material_bindings"), list):
        raise ValueError("PhotoShip bindings must be version 2")
    candidates = [item for item in bindings["material_bindings"] if item.get("status") == "PHOTO_MAP_CANDIDATE"]
    functional = [item for item in bindings["material_bindings"] if item.get("status") == "PRESERVE_FUNCTIONAL"]
    if len(candidates) != PHOTO_SHIP_CANDIDATE_COUNT:
        raise ValueError(f"Expected {PHOTO_SHIP_CANDIDATE_COUNT} assigned PhotoShip candidates, got {len(candidates)}")
    if len(functional) != PHOTO_SHIP_FUNCTIONAL_COUNT:
        raise ValueError(f"Expected {PHOTO_SHIP_FUNCTIONAL_COUNT} functional PhotoShip materials, got {len(functional)}")
    map_sets = {item.get("id"): item for item in bindings.get("map_sets", [])}
    if len(map_sets) != PHOTO_SHIP_MAP_SET_COUNT or None in map_sets:
        raise ValueError(f"Expected {PHOTO_SHIP_MAP_SET_COUNT} PhotoShip map sets, got {len(map_sets)}")
    art_materials = art.get("materials", {})
    art_parts = {item.get("name"): item for item in art.get("model_parts", [])}
    ship_textures = {}
    for mapset_id, mapset in map_sets.items():
        if set(mapset.get("maps", {})) != {"modulation", "roughness", "normal_dx"}:
            raise ValueError(f"PhotoShip map set must contain modulation/roughness/normal_dx: {mapset_id}")
        repeat = mapset.get("repeat_m", mapset.get("repeat_meters"))
        if not isinstance(repeat, list) or len(repeat) != 2 or any(_finite(x, f"{mapset_id} repeat_m") <= 0 for x in repeat):
            raise ValueError(f"PhotoShip map set requires positive U/V repeat_m: {mapset_id}")
        for relative in mapset["maps"].values():
            source_path(root, relative)
    for item in candidates:
        material = item.get("material")
        if material not in art_materials or not isinstance(item.get("parts"), list) or not item["parts"]:
            raise ValueError(f"PhotoShip candidate has no valid material/parts: {material}")
        if item.get("map_set") not in map_sets:
            raise ValueError(f"Unknown PhotoShip map set: {item.get('map_set')}")
        if len(vector(item.get("base_color_linear"), 3)) != 3:
            raise ValueError(f"Invalid PhotoShip base color: {material}")
        source_color = art_materials[material].get("base_color_linear")
        if not isinstance(source_color, list) or any(abs(float(a) - float(b)) > 1e-9
                                                     for a, b in zip(item["base_color_linear"], source_color)):
            raise ValueError(f"PhotoShip binding base color drifted from art manifest: {material}")
        for part in item["parts"]:
            if part not in art_parts or material not in art_parts[part].get("materials", []):
                raise ValueError(f"PhotoShip part/material mismatch: {part}/{material}")
        overrides = item.get("part_overrides", {})
        if not isinstance(overrides, dict):
            raise ValueError(f"PhotoShip part_overrides must be an object: {material}")
        for part, override in overrides.items():
            if part not in item["parts"] or not isinstance(override, dict):
                raise ValueError(f"PhotoShip part override targets an unassigned/invalid part: {material}/{part}")
            if "roughness_offset" in override and not math.isfinite(float(override["roughness_offset"])):
                raise ValueError(f"PhotoShip roughness offset is not finite: {material}/{part}")
        for kind, relative in map_sets[item["map_set"]].get("maps", {}).items():
            if kind not in ("modulation", "roughness", "normal_dx"):
                raise ValueError(f"Unexpected PhotoShip map channel: {kind}")
            map_key = Path(relative).stem
            settings = {"srgb": False, "compression": "TC_NORMALMAP" if kind == "normal_dx" else "TC_MASKS",
                        "normal": kind == "normal_dx", "flip_green_channel": False,
                        "address_x": "TA_WRAP", "address_y": "TA_WRAP", "mips": True}
            if map_key not in ship_textures:
                task = _photo_task(root, relative, f"/Game/Star/Art/PhotoShip/Textures/{map_key}", settings,
                                   {"recipe": PHOTO_SHIP_BINDINGS, "mapSet": item["map_set"], "channel": kind,
                                    "repeat_m": map_sets[item["map_set"]].get("repeat_m", map_sets[item["map_set"]].get("repeat_meters"))})
                tasks.append(task)
                ship_textures[map_key] = task["object"]
            elif tasks[next(i for i, t in enumerate(tasks) if t["object"] == ship_textures[map_key])]["settings"] != settings:
                raise ValueError(f"PhotoShip map channel used with incompatible settings: {relative}")
    unassigned = [item for item in bindings["material_bindings"] if item.get("status") == "PHOTO_MAP_CANDIDATE_UNASSIGNED"]
    # Keep every checked-in map set in the source plan, including the
    # currently unassigned FoilResponse candidate.  It is imported for
    # reproducible review, while no mesh slot is assigned to it below.
    for mapset_id, mapset in map_sets.items():
        for kind, relative in mapset.get("maps", {}).items():
            if kind not in ("modulation", "roughness", "normal_dx"):
                raise ValueError(f"Unexpected PhotoShip map channel: {kind}")
            map_key = Path(relative).stem
            if map_key in ship_textures:
                continue
            settings = {"srgb": False, "compression": "TC_NORMALMAP" if kind == "normal_dx" else "TC_MASKS",
                        "normal": kind == "normal_dx", "flip_green_channel": False,
                        "address_x": "TA_WRAP", "address_y": "TA_WRAP", "mips": True}
            task = _photo_task(root, relative, f"/Game/Star/Art/PhotoShip/Textures/{map_key}", settings,
                               {"recipe": PHOTO_SHIP_BINDINGS, "mapSet": mapset_id, "channel": kind,
                                "repeat_m": mapset.get("repeat_m", mapset.get("repeat_meters"))})
            tasks.append(task)
            ship_textures[map_key] = task["object"]

    plumes = read_json(recipes["plumes"])
    if plumes.get("schema") != 1 or plumes.get("preferred_runtime_candidate") != "vacuum chemical":
        raise ValueError("Vacuum plume recipe is not the preferred schema-1 candidate")
    plume_tasks = {}
    files = plumes.get("files", {})
    for key, (filename, package) in PHOTO_PLUME_TEXTURES.items():
        relative = f"Content/Star/Art/PhotoPlumes/{filename}"
        expected = files.get(filename, {})
        task = _photo_task(root, relative, package,
                           {"srgb": False, "compression": "TC_DEFAULT", "normal": False,
                            "flip_green_channel": False, "address_x": "TA_CLAMP", "address_y": "TA_CLAMP", "mips": True},
                           {"recipe": PHOTO_PLUME_RECIPE, "runtimeKey": key})
        if expected.get("sha256") and expected["sha256"] != task["sha256"]:
            raise ValueError(f"Vacuum plume digest changed: {filename}")
        tasks.append(task)
        plume_tasks[key] = task["object"]
    audio_candidates = ([audio_manifest] if audio_manifest else [
        "Content/Star/AudioEngineV2/audio_import_manifest.json",
        "Content/Star/AudioEngineV2/import_manifest.json",
    ])
    audio = None
    for audio_relative in audio_candidates:
        if (root / audio_relative).is_file():
            audio = _audio_manifest_entries(root, audio_relative)
            break
    photo_materials = []
    for item in candidates:
        map_set = map_sets[item["map_set"]]
        photo_materials.append({
            "material": item["material"], "parts": item["parts"], "mapSet": item["map_set"],
            "mapSetSpec": map_set, "baseColorLinear": item["base_color_linear"],
            "metallic": item.get("metallic"), "baseColorEquation": item.get("base_color_equation"),
            "partOverrides": item.get("part_overrides", {}),
            "sourceMaterial": f"/Game/Star/Art/ExplorerV2/Materials/{item['material']}",
            "photoMaterial": f"/Game/Star/Art/PhotoShip/Materials/{item['material']}_Photo",
        })
    material_tasks = [
        {"kind": "material_graph", "package": item["photoMaterial"], "object": object_path(item["photoMaterial"]),
         "material": item["material"], "mapSet": item["mapSet"], "source": item["sourceMaterial"],
         "settings": {"baseColor": "original base paint * (1 + (Modulation.R - 0.5) * 0.3)",
                      "roughness": "photo Roughness.R + part override", "normal": "object-local signed triplanar; UE normal unpack once"},
         "provenance": {"recipe": PHOTO_SHIP_BINDINGS}} for item in photo_materials
    ]
    material_tasks.extend([
        {"kind": "material_graph", "package": "/Game/Star/Materials/M_Surface", "object": object_path("/Game/Star/Materials/M_Surface"),
         "settings": {"RegionalTex": "apollo17_photo_regional", "RegionalStrength": PHOTO_MOON_REGIONAL_STRENGTH,
                      "DetailScale": PHOTO_MOON_DETAIL_SCALE, "DetailNormalTex": "apollo_soil_normal"},
         "provenance": {"recipe": PHOTO_MOON_RECIPE}},
        {"kind": "material_graph", "package": "/Game/Star/Materials/M_Thruster", "object": object_path("/Game/Star/Materials/M_Thruster"),
         "settings": {"defines": ["STAR_PHOTO_PLUME=1"], "inputs": ["PlumePhoto", "LayerEnergy", "Thrust", "ThrusterRadiance", "PulsePhase", "ThrusterColor"]},
         "provenance": {"recipe": PHOTO_PLUME_RECIPE}},
        {"kind": "material_graph", "package": "/Game/Star/Materials/M_ThrusterAxial", "object": object_path("/Game/Star/Materials/M_ThrusterAxial"),
         "settings": {"inputs": ["PlumeAxial", "UV", "Thrust", "ThrusterRadiance", "LayerEnergy"]},
         "provenance": {"recipe": PHOTO_PLUME_RECIPE}},
    ])
    photographic_world = {}
    for key, recipe_relative, parameter, package in (
        ("photo_sky", "Content/Star/Art/PhotoSky/material_recipe.json", "PhotoSkyTex", "/Game/Star/Art/PhotoSky/T_PhotoSky"),
        ("earth_day", "Content/Star/Art/PhotoEarth/material_recipe.json", None, "/Game/Star/Art/PhotoEarth/T_EarthDay16K"),
    ):
        recipe = read_json(source_path(root, recipe_relative))
        spec = recipe["textures"][parameter] if parameter else recipe["texture"]
        task = _photo_task(root, spec["file"], package,
            {"srgb": spec["srgb"], "compression": spec["compression"], "normal": False,
             "flip_green_channel": False, "address_x": spec["address_x"], "address_y": spec["address_y"],
             "mips": True, "max_texture_size": spec.get("max_texture_size", 0), "never_stream": spec.get("never_stream", False)},
            {"recipe": recipe_relative, "recipeSha256": sha256(root / recipe_relative)})
        tasks.append(task)
        photographic_world[key] = task["object"]
    return {
        "schemaVersion": 2, "status": "PHOTO_SOURCE_PLAN_ONLY", "project": str(root),
        "photographicWorld": photographic_world,
        "artManifestSha256": sha256(art_path),
        "recipeSha256": {key: sha256(path) for key, path in recipes.items() if path.suffix.lower() == ".json"},
        "tasks": tasks + material_tasks,
        "photoMoon": {"recipe": PHOTO_MOON_RECIPE, "assets": moon_assets,
                       "RegionalStrength": PHOTO_MOON_REGIONAL_STRENGTH, "DetailScale": PHOTO_MOON_DETAIL_SCALE},
        "photoSaturn": {"recipe": PHOTO_SATURN_RECIPE, "assets": saturn_assets, "assetMapOverrides": expected_saturn},
        "photoShip": {"recipe": PHOTO_SHIP_BINDINGS, "version": 2, "candidates": photo_materials,
                      "functional": functional,
                      "candidateCount": len(candidates), "functionalCount": len(functional),
                      "mapSetCount": len(map_sets), "unassignedCandidates": unassigned,
                      "textures": ship_textures},
        "photoPlumes": {"recipe": PHOTO_PLUME_RECIPE, "integration": PHOTO_PLUME_INTEGRATION,
                        "textures": plume_tasks, "mainRadiance": 40.0, "rcsRadiance": 8.0,
                        "layerEnergy": 0.166667, "axialLayerEnergy": 0.025},
        "audio": audio or {"status": "OPTIONAL_L1_MANIFEST_MISSING", "existingOnly": True, "cues": []},
        "acceptance": "LOCAL_SOURCE_PLAN_ONLY; UE import/readback and packaged rendering remain root gates",
    }


def build_plan(root, audio_pack):
    root, audio_pack = Path(root).resolve(), Path(audio_pack).resolve()
    art_path = source_path(root, "Art/Explorer/V2/art_manifest.json")
    art = read_json(art_path)
    if art["version"] != 2 or art["source_units"] != "meters" or art["source_frame"] != {
            "forward": "+X", "right": "+Y", "up": "+Z"} or art["part_file_base"] != ART_BASE:
        raise ValueError("Unsupported ship version/units/axes/source base")
    runtime = runtime_assets(art)
    tasks = []

    def add(kind, path, package, settings, provenance):
        name(package.rsplit("/", 1)[1])
        task = {"kind": kind, "source": str(path), "package": package,
                "object": object_path(package), "sha256": sha256(path),
                "settings": settings, "provenance": provenance}
        tasks.append(task)
        return task

    manifest = read_json(source_path(root, "Data/manifest.json"))
    observed = {}
    for entry in manifest["assets"]:
        if not entry["path"].startswith("Content/Star/Textures/"):
            continue
        path = source_path(root, entry["path"])
        stem = name(path.stem)
        coverage = stem == "earth_clouds_8k"
        settings = {"srgb": path.suffix.lower() != ".exr" and not coverage, "normal": False,
                    "compression": "TC_HDR" if path.suffix.lower() == ".exr" else "TC_BC7" if (coverage or stem=="earth_night_8k") else "TC_DEFAULT"}
        if coverage or stem=="earth_night_8k":
            settings.update(address_x="TA_WRAP", address_y="TA_CLAMP", never_stream=True, max_texture_size=8192)
        task = add("texture", path, f"/Game/Star/Textures/{stem}", settings, entry)
        if task["sha256"] != entry["sha256"]:
            raise ValueError(f"NASA source digest changed: {entry['path']}")
        observed[stem] = task["object"]
    missing = set(NASA_KEYS.values()) - observed.keys()
    if missing:
        raise ValueError(f"Missing NASA material inputs: {sorted(missing)}")
    actual_textures = {p.name for p in (root / "Content/Star/Textures").iterdir()
                       if p.suffix.lower() in (".png", ".jpg", ".jpeg", ".exr", ".tif", ".tiff")}
    if actual_textures != {Path(t["source"]).name for t in tasks}:
        raise ValueError("Texture files and NASA provenance manifest differ")
    lunar_textures = {}
    lunar_recipe = read_json(source_path(root, "Tools/Lookdev/Lunar/material_recipe.json"))
    for spec in lunar_recipe["textures"].values():
        image = source_path(root, spec["file"])
        settings = {"srgb": spec["srgb"], "normal": spec["compression"] == "TC_NORMALMAP",
                    "compression": spec["compression"], "flip_green_channel": bool(spec.get("flip_green", False)),
                    "address_x": spec["address_x"], "address_y": spec["address_y"]}
        task = add("texture", image, f"/Game/Star/Art/Lookdev/Lunar/{name(image.stem)}", settings,
                   {"source": "Content/Star/Art/Lookdev/Lunar/provenance.json", "scientific_observation": False})
        lunar_textures[spec["key"]] = task["object"]
    art_textures = {}
    for material, spec in art["materials"].items():
        name(material)
        for field in ("base_color_texture", "normal_texture", "roughness_texture", "metallic_texture"):
            relative = spec.get(field)
            if not relative:
                continue
            settings = {"srgb": field == "base_color_texture", "normal": field == "normal_texture",
                        "compression": "TC_NORMALMAP" if field == "normal_texture" else (
                            "TC_DEFAULT" if field == "base_color_texture" else "TC_MASKS")}
            if relative in art_textures:
                if art_textures[relative]["settings"] != settings:
                    raise ValueError(f"Incompatible texture channel use: {relative}")
                continue
            path = source_path(root / ART_BASE, relative)
            art_textures[relative] = add("texture", path, f"/Game/Star/Art/ExplorerV2/Textures/{name(path.stem)}",
                                         settings, {"source": "Art/Explorer/V2/art_manifest.json", "scientific_observation": False})
    for part in art["model_parts"]:
        if any(x <= 0 for x in vector(part["dimensions_m"])):
            raise ValueError(f"Invalid mesh dimensions: {part['name']}")
        add("static_mesh", source_path(root / ART_BASE, part["fbx"]),
            f"/Game/Star/Art/ExplorerV2/Parts/{part['name']}",
            {"nanite": all(art["materials"][m].get("transmission", 0) == 0 for m in part["materials"]),
             "part": part}, {"source": "Art/Explorer/V2/art_manifest.json", "scientific_observation": False})
    cues_path = source_path(audio_pack, "audio_cues.json")
    cues = read_json(cues_path)["cues"]
    if len(cues) != 91 or len({cue["id"] for cue in cues}) != 91:
        raise ValueError("Expected 91 unique audio cue IDs for STAR Audio Pack v1")
    for cue in cues:
        cue_id = name(cue["id"])
        if cue["asset_name"] != "SW_" + cue_id:
            raise ValueError(f"Audio name mismatch: {cue_id}")
        # wav_file already selects the pack's preferred loop candidate when available.
        path = source_path(audio_pack, cue["wav_file"])
        actual = inspect_wav(path, cue)
        add("audio", path, f"/Game/Star/Audio/SW_{cue_id}",
            {"cue": cue, "actual_wav": actual}, cue)
    font_plan = read_json(source_path(root, "Tools/UI/font_import_plan.json"))
    font_path = source_path(root, font_plan["font_source"])
    font_task = add("font_face", font_path, font_plan["unreal_import"]["font_face_destination"],
                    {"loading_policy": "INLINE"}, font_plan)
    if font_task["sha256"] != font_plan["source_sha256"]:
        raise ValueError("Japanese font digest differs from verified font plan")
    for field in ("license", "credits"):
        source_path(root, font_plan[field])
    packages = [task["package"].lower() for task in tasks]
    if len(packages) != len(set(packages)):
        raise ValueError("Duplicate Unreal destination paths")
    return {"schemaVersion": 1, "status": "OFFLINE_SOURCE_PLAN_ONLY", "project": str(root),
            "artManifestSha256": sha256(art_path), "audioManifestSha256": sha256(cues_path),
            "map": MAP_PATH, "gameMode": GAME_MODE, "tasks": tasks,
            "planetTextures": {**{key: observed[stem] for key, stem in {**NASA_KEYS, **OPTIONAL_NASA_KEYS}.items() if stem in observed}, **lunar_textures},
            "artTextures": {key: value["object"] for key, value in art_textures.items()},
            "fontPlan": font_plan, "art": art, "runtimeAssets": runtime}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--audio-pack", type=Path)
    parser.add_argument("--audio-manifest", type=str)
    parser.add_argument("--photo-only", action="store_true",
                        help="Validate v0.2 photo/material inputs without scheduling the legacy audio pack")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.photo_only:
        plan = build_photo_plan(args.project, args.audio_manifest)
    else:
        if args.audio_pack is None:
            parser.error("--audio-pack is required unless --photo-only is selected")
        plan = build_plan(args.project, args.audio_pack)
    if args.output:
        output = args.output.resolve()
        if not output.is_relative_to((args.project / "work").resolve()):
            raise ValueError("Offline report must be inside project/work")
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(plan, ensure_ascii=False, indent=2), encoding="utf-8")
    counts = {kind: sum(t["kind"] == kind for t in plan["tasks"])
              for kind in ("texture", "photo_texture", "material_graph", "static_mesh", "audio", "font_face")}
    summary = {"status": plan["status"], "counts": counts}
    if "runtimeAssets" in plan:
        summary.update({"shipParts": len(plan["runtimeAssets"]["shipParts"]), "map": plan["map"]})
    else:
        summary.update({"photoShipCandidates": plan["photoShip"]["candidateCount"],
                        "photoShipMapSets": plan["photoShip"]["mapSetCount"],
                        "audioExistingOnly": plan["audio"]["existingOnly"]})
    print(json.dumps(summary, ensure_ascii=False))


if __name__ == "__main__":
    main()
