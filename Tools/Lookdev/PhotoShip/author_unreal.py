"""Author the STAR v0.2 PhotoShip material pass in a dedicated UE editor.

The script is intentionally usable as a module without Unreal so that source
plans and shader contracts can be checked on a normal Python installation.
When run inside Unreal it imports only the seven checked-in map sets, clones
the fourteen assigned opaque ship materials, and changes only matching slots.
The original ExplorerV2 materials, functional display materials, glass, mesh
geometry, sockets and UVs remain intact.

This is an authoring/readback step.  It requests a material compile but does
not claim UE shader, cook, packaged-game, GPU or visual acceptance.
"""
from __future__ import annotations

import hashlib
import json
import math
import re
from pathlib import Path

try:
    import unreal
except ModuleNotFoundError:  # pragma: no cover - exercised by offline checks
    unreal = None


ROOT = Path(__file__).resolve().parents[3]
TOOLS_UNREAL = ROOT / "Tools" / "Unreal"
DEST = "/Game/Star/Art/PhotoShip"
TEXTURE_DEST = DEST + "/Textures"
MATERIAL_DEST = DEST + "/Materials"
BASE_DEST = "/Game/Star/Art/ExplorerV2/Materials"
PART_DEST = "/Game/Star/Art/ExplorerV2/Parts"
OWNER_TAG = "STAR_PhotoShipOwner"
OWNER = "photo-ship-object-triplanar-v2"
SOURCE_HASH_TAG = "STAR_PhotoShipSourceSHA256"
GRAPH_HASH_TAG = "STAR_PhotoShipGraphHash"
CANOPY_FUNCTIONAL_CONTRACT = {
    "baseColor": [0.0, 0.0, 0.0], "transmittance": [0.995, 0.995, 0.995],
    "opacity": 0.0, "twoSided": False,
}


def read_json(path: Path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def canonical_hash(value) -> str:
    return hashlib.sha256(json.dumps(value, ensure_ascii=False, sort_keys=True,
                                     separators=(",", ":")).encode("utf-8")).hexdigest()


def _source_path(root: Path, relative: str) -> Path:
    if not isinstance(relative, str) or "\\" in relative or ":" in relative:
        raise ValueError(f"Non-portable photo source path: {relative!r}")
    base = root.resolve()
    path = (base / relative).resolve()
    if Path(relative).is_absolute() or not path.is_relative_to(base) or not path.is_file():
        raise ValueError(f"Missing/out-of-bound photo source: {relative}")
    return path


def _valid_name(value: str) -> str:
    if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", value):
        raise ValueError(f"Invalid Unreal name: {value!r}")
    return value


def validate_bindings(root: Path = ROOT, bindings=None, art=None) -> dict:
    """Validate the PhotoShip v2 contract without touching Unreal."""
    root = Path(root).resolve()
    bindings = read_json(root / "Data/photo_ship_bindings.json") if bindings is None else bindings
    art = read_json(root / "Art/Explorer/V2/art_manifest.json") if art is None else art
    if bindings.get("version") != 2:
        raise ValueError("PhotoShip bindings must be version 2")
    entries = bindings.get("material_bindings")
    if not isinstance(entries, list):
        raise ValueError("PhotoShip material_bindings must be a list")
    candidates = [x for x in entries if x.get("status") == "PHOTO_MAP_CANDIDATE"]
    functional = [x for x in entries if x.get("status") == "PRESERVE_FUNCTIONAL"]
    if len(candidates) != 14:
        raise ValueError(f"Expected 14 assigned opaque PhotoShip materials, got {len(candidates)}")
    if len(functional) != 7:
        raise ValueError(f"Expected 7 functional PhotoShip materials, got {len(functional)}")
    map_sets = {x.get("id"): x for x in bindings.get("map_sets", [])}
    if len(map_sets) != 7 or None in map_sets:
        raise ValueError("Expected seven uniquely named PhotoShip map sets")
    materials = art.get("materials", {})
    parts = {x.get("name"): x for x in art.get("model_parts", [])}
    seen_candidates = set()
    for item in candidates:
        material = _valid_name(item.get("material"))
        if material in seen_candidates or material not in materials:
            raise ValueError(f"Invalid or duplicate PhotoShip material: {material}")
        seen_candidates.add(material)
        map_set = item.get("map_set")
        if map_set not in map_sets:
            raise ValueError(f"Unknown PhotoShip map set for {material}: {map_set}")
        color = item.get("base_color_linear")
        if not isinstance(color, list) or len(color) != 3 or any(
                isinstance(x, bool) or not isinstance(x, (int, float)) for x in color):
            raise ValueError(f"Invalid original linear base color for {material}")
        item_parts = item.get("parts")
        if not isinstance(item_parts, list) or not item_parts:
            raise ValueError(f"PhotoShip material has no assigned parts: {material}")
        for part in item_parts:
            if part not in parts or material not in parts[part].get("materials", []):
                raise ValueError(f"PhotoShip part/material mismatch: {part}/{material}")
        overrides = item.get("part_overrides", {})
        if not isinstance(overrides, dict):
            raise ValueError(f"PhotoShip part_overrides must be an object: {material}")
        for part, override in overrides.items():
            if part not in item_parts or not isinstance(override, dict):
                raise ValueError(f"PhotoShip part override targets an unassigned/invalid part: {material}/{part}")
            if "roughness_offset" in override and (not isinstance(override["roughness_offset"], (int, float)) or
                    not math.isfinite(float(override["roughness_offset"]))):
                raise ValueError(f"PhotoShip roughness offset must be numeric: {material}/{part}")
        map_spec = map_sets[map_set]
        if set(map_spec.get("maps", {})) != {"modulation", "roughness", "normal_dx"}:
            raise ValueError(f"PhotoShip map set must contain modulation/roughness/normal_dx: {map_set}")
        repeat = map_spec.get("repeat_m", map_spec.get("repeat_meters"))
        if not isinstance(repeat, list) or len(repeat) != 2 or any(float(x) <= 0 for x in repeat):
            raise ValueError(f"PhotoShip map set requires positive U/V repeat_m: {map_set}")
        for relative in map_spec["maps"].values():
            _source_path(root, relative)
    return {"bindings": bindings, "art": art, "candidates": candidates,
            "functional": functional, "mapSets": map_sets}


def _node(material, cls, **properties):
    result = unreal.MaterialEditingLibrary.create_material_expression(material, cls, -600, 0)
    if result is None:
        raise RuntimeError(f"Cannot create material expression {cls}")
    for key, value in properties.items():
        result.set_editor_property(key, value)
    return result


def _wire(source, target, pin: str = "", output: str = ""):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(source, output, target, pin):
        raise RuntimeError(f"Material connection failed: {pin}")


def _property(source, name: str):
    prop = getattr(unreal.MaterialProperty, name)
    if not unreal.MaterialEditingLibrary.connect_material_property(source, "", prop):
        raise RuntimeError(f"Material output connection failed: {name}")


def _scalar(material, value: float):
    return _node(material, unreal.MaterialExpressionConstant, r=float(value))


def _vector(material, values):
    return _node(material, unreal.MaterialExpressionConstant3Vector,
                 constant=unreal.LinearColor(float(values[0]), float(values[1]), float(values[2]), 1.0))


def _vector2(material, values):
    return _node(material, unreal.MaterialExpressionConstant2Vector,
                 r=float(values[0]), g=float(values[1]))


def _custom(material, code: str, inputs: dict, output_components: int = 3, description: str = ""):
    node = _node(material, unreal.MaterialExpressionCustom,
                 code=code, output_type=getattr(unreal.CustomMaterialOutputType,
                                                 f"CMOT_FLOAT{output_components}"))
    if description:
        node.set_editor_property("description", description)
    pins = []
    for key in inputs:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", key)
        pins.append(pin)
    node.set_editor_property("inputs", pins)
    for key, value in inputs.items():
        _wire(value, node, key)
    return node


def _transform(material, source, source_type: str, destination_type: str):
    node = _node(material, unreal.MaterialExpressionTransform,
                 transform_source_type=getattr(unreal.MaterialVectorCoordTransformSource,
                                                "TRANSFORMSOURCE_" + source_type),
                 transform_type=getattr(unreal.MaterialVectorCoordTransform,
                                        "TRANSFORM_" + destination_type))
    _wire(source, node)
    return node


def _coordinates(material):
    # LWC world-to-local is performed before the float custom node.  The local
    # centimeter coordinate is converted to meters before any photo repeat is
    # applied, so astronomical coordinates never enter this graph.
    world = _node(material, unreal.MaterialExpressionWorldPosition,
                  world_position_shader_offset=unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    local = _node(material, unreal.MaterialExpressionTransformPosition,
                  transform_source_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD,
                  transform_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    _wire(world, local)
    meters = _custom(material, "return P * 0.01;", {"P": local}, 3,
                     "STAR PhotoShip local centimeters to meters")
    vertex_normal = _node(material, unreal.MaterialExpressionVertexNormalWS)
    local_normal = _transform(material, vertex_normal, "WORLD", "LOCAL")
    interpolated = _node(material, unreal.MaterialExpressionVertexInterpolator)
    _wire(local_normal, interpolated, "VS")
    normal = _custom(material, "return normalize(N);", {"N": interpolated}, 3,
                     "STAR PhotoShip geometric local normal")
    weights = _custom(material,
                      "float3 w=pow(abs(N),4.0); return w/max(dot(w,1.0),1e-6);",
                      {"N": normal}, 3, "STAR PhotoShip signed triplanar weights")
    return meters, normal, weights


def _sampler_type(texture, normal=False):
    if normal:
        return unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
    compression = texture.get_editor_property("compression_settings")
    if compression == unreal.TextureCompressionSettings.TC_MASKS:
        return unreal.MaterialSamplerType.SAMPLERTYPE_MASKS
    return (unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if texture.get_editor_property("srgb")
            else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)


def _photo_samples(material, texture, sampler, position, normal, repeat, parameter_prefix):
    """Sample a linear photo map in the old Cockpit signed object-local basis."""
    repeat_node = _vector2(material, repeat)
    samples = []
    for axis, code in enumerate((
            "float2(P.y*(N.x<0?-1:1),P.z)/Repeat",
            "float2(P.z*(N.y<0?-1:1),P.x)/Repeat",
            "float2(P.x*(N.z<0?-1:1),P.y)/Repeat")):
        uv = _custom(material, f"return {code};",
                      {"P": position, "N": normal, "Repeat": repeat_node}, 2,
                      f"STAR PhotoShip {parameter_prefix} signed projection {axis}")
        sample = _node(material, unreal.MaterialExpressionTextureSampleParameter2D,
                       parameter_name=f"{parameter_prefix}_{axis}", texture=texture,
                       sampler_type=sampler)
        _wire(uv, sample, "UVs")
        samples.append(sample)
    return samples


def _blend_rgb(material, values, weights, description):
    return _custom(material, "return X*W.x+Y*W.y+Z*W.z;",
                   {"X": values[0], "Y": values[1], "Z": values[2], "W": weights}, 3, description)


def _blend_r(material, values, weights, description):
    return _custom(material, "return X.r*W.x+Y.r*W.y+Z.r*W.z;",
                   {"X": values[0], "Y": values[1], "Z": values[2], "W": weights}, 1, description)


def _photo_normal(material, samples, normal, weights):
    # SAMPLERTYPE_NORMAL already returns signed tangent XYZ (including BC5 Z).
    # The local tangent-plane slope is accumulated once and then transformed to
    # world space.  There is no second ``*2-1`` or green-channel flip.
    return _custom(material, """
float3 sx=X.xyz/max(X.z,0.2), sy=Y.xyz/max(Y.z,0.2), sz=Z.xyz/max(Z.z,0.2);
float3 d = W.x*float3(0,sx.x*(N.x<0?-1:1),sx.y)
         + W.y*float3(sy.y,0,sy.x*(N.y<0?-1:1))
         + W.z*float3(sz.x*(N.z<0?-1:1),sz.y,0);
d -= N*dot(N,d);
return normalize(N+d);
""", {"X": samples[0], "Y": samples[1], "Z": samples[2], "N": normal, "W": weights}, 3,
                    "STAR PhotoShip decoded DX normal local tangent basis")


def _clear_graph(material):
    # UE 5.8's DeleteAllMaterialExpressions mutates the collection it is
    # iterating.  A snapshot is required or stale custom output nodes survive
    # regeneration and silently double the photo pass.
    expressions = list(unreal.MaterialEditingLibrary.get_material_expressions(material))
    for expression in expressions:
        unreal.MaterialEditingLibrary.delete_material_expression(material, expression)
    remaining = list(unreal.MaterialEditingLibrary.get_material_expressions(material))
    if remaining:
        raise RuntimeError(f"Photo material graph clear left {len(remaining)} expressions: {material.get_path_name()}")


def _configure_texture(asset, settings):
    values = {
        "srgb": bool(settings["srgb"]),
        "compression_settings": getattr(unreal.TextureCompressionSettings, settings["compression"]),
        "virtual_texture_streaming": False,
        "flip_green_channel": bool(settings.get("flip_green_channel", False)),
        "address_x": getattr(unreal.TextureAddress, settings.get("address_x", "TA_WRAP")),
        "address_y": getattr(unreal.TextureAddress, settings.get("address_y", "TA_WRAP")),
        "lod_bias": 0,
        # Preserve original photographic bytes and physical repeat size, but
        # resample the full crop for BC compression and a usable mip chain.
        "power_of_two_mode": unreal.TexturePowerOfTwoSetting.STRETCH_TO_POWER_OF_TWO,
        "mip_gen_settings": unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE,
    }
    for key, value in values.items():
        asset.set_editor_property(key, value)
        if asset.get_editor_property(key) != value:
            raise RuntimeError(f"Photo texture setting readback failed: {key}")


def _owned_asset(path, cls):
    asset = unreal.load_asset(path)
    if asset is not None and (not isinstance(asset, cls) or
            unreal.EditorAssetLibrary.get_metadata_tag(asset, OWNER_TAG) != OWNER):
        raise RuntimeError(f"Unowned or incompatible PhotoShip destination: {path}")
    return asset


def import_photo_texture(task):
    """Import/reuse one source-backed photo map and return readback metadata."""
    path = task["package"]
    source = Path(task["source"])
    expected_hash = task["sha256"]
    if sha256(source) != expected_hash:
        raise ValueError(f"Photo source changed after plan creation: {task['sourceRelative']}")
    asset = _owned_asset(path, unreal.Texture2D)
    action = "reused"
    if asset is None or unreal.EditorAssetLibrary.get_metadata_tag(asset, SOURCE_HASH_TAG) != expected_hash:
        if asset is not None:
            action = "reimported"
        else:
            action = "imported"
        job = unreal.AssetImportTask()
        folder, name = path.rsplit("/", 1)
        values = {"filename": str(source), "destination_path": folder, "destination_name": name,
                  "automated": True, "replace_existing": asset is not None,
                  "replace_existing_settings": True, "save": False, "async_": False}
        for key, value in values.items():
            job.set_editor_property(key, value)
        factory = unreal.TextureFactory()
        factory.set_editor_property("create_material", False)
        job.set_editor_property("factory", factory)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([job])
        asset = unreal.load_asset(task["object"])
        if not isinstance(asset, unreal.Texture2D):
            raise RuntimeError(f"Photo texture import failed: {task['object']}")
    _configure_texture(asset, task["settings"])
    unreal.EditorAssetLibrary.set_metadata_tag(asset, OWNER_TAG, OWNER)
    unreal.EditorAssetLibrary.set_metadata_tag(asset, SOURCE_HASH_TAG, expected_hash)
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Photo texture save failed: {task['object']}")
    return {"asset": asset.get_path_name(), "source": task["sourceRelative"],
            "sourceSha256": expected_hash, "settings": task["settings"], "action": action}


def _base_sample(material, source_path, spec):
    if spec.get("base_color_texture"):
        texture = unreal.load_asset(source_path)
        if not isinstance(texture, unreal.Texture2D):
            raise RuntimeError(f"Missing original base-color texture: {source_path}")
        repeat = spec.get("paint_uv_repeat", spec.get("uv_repeat", 1.0))
        uv = _node(material, unreal.MaterialExpressionTextureCoordinate, coordinate_index=0,
                   u_tiling=float(repeat), v_tiling=float(repeat))
        sample = _node(material, unreal.MaterialExpressionTextureSampleParameter2D,
                       parameter_name="OriginalBasePaint", texture=texture,
                       sampler_type=_sampler_type(texture))
        _wire(uv, sample, "UVs")
        # Base_color_texture_tint_baked is the manifest's explicit guard
        # against multiplying a paint tint or decal twice.
        if spec.get("base_color_texture_tint_baked", False):
            return sample
        return _custom(material, "return Color*Tint;",
                       {"Color": sample, "Tint": _vector(material, spec["base_color_linear"])}, 3,
                       "STAR original paint tint")
    return _vector(material, spec["base_color_linear"])


def _original_texture_path(root: Path, relative: str) -> str:
    path = _source_path(root / "Content/Star/Art/ExplorerV2", relative)
    return f"/Game/Star/Art/ExplorerV2/Textures/{path.stem}"


def _build_clone(material, source_spec, photo_spec, textures, root: Path):
    _clear_graph(material)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    material.set_editor_property("two_sided", False)
    # The triplanar custom node returns a world-space normal after LOCAL->WORLD
    # transform, therefore this must be false.
    material.set_editor_property("tangent_space_normal", False)
    unreal.MaterialEditingLibrary.set_base_material_usage(material, unreal.MaterialUsage.MATUSAGE_NANITE, True)

    position, geometric_normal, weights = _coordinates(material)
    base = _base_sample(material, _original_texture_path(root, source_spec["base_color_texture"])
                        if source_spec.get("base_color_texture") else "", source_spec)
    map_set = photo_spec["mapSetSpec"]
    repeat = map_set.get("repeat_m", map_set.get("repeat_meters"))
    modulation_tex = textures["modulation"]
    roughness_tex = textures["roughness"]
    normal_tex = textures["normal_dx"]
    modulation = _blend_rgb(material,
                            _photo_samples(material, modulation_tex, _sampler_type(modulation_tex), position,
                                           geometric_normal, repeat, "PhotoModulation"),
                            weights, "STAR PhotoShip modulation triplanar")
    # Keep the original paint/decal signal and apply only the bounded linear
    # photographic residual.  Modulation is imported with sRGB disabled.
    photo_base = _custom(material,
                         "return saturate(Base*(1.0+(Modulation.r-0.5)*0.3));",
                         {"Base": base, "Modulation": modulation}, 3,
                         "STAR PhotoShip original paint plus linear modulation")
    photo_roughness = _blend_r(material,
                               _photo_samples(material, roughness_tex, _sampler_type(roughness_tex), position,
                                              geometric_normal, repeat, "PhotoRoughness"),
                               weights, "STAR PhotoShip roughness triplanar")
    # A variant clone supplies a documented part-specific offset (currently
    # the dark radiator faces).  Keeping the offset out of the base clone
    # prevents the radiator adjustment from changing every HeatShield slot.
    roughness_offset = float(photo_spec.get("roughnessOffset", 0.0))
    roughness = _custom(material, "return saturate(R+Offset);",
                        {"R": photo_roughness, "Offset": _scalar(material, roughness_offset)}, 1,
                        "STAR PhotoShip photo roughness with documented offset")
    local_photo_normal = _photo_normal(
        material,
        _photo_samples(material, normal_tex, _sampler_type(normal_tex, True), position,
                       geometric_normal, repeat, "PhotoNormalDX"),
        geometric_normal, weights)
    world_normal = _transform(material, local_photo_normal, "LOCAL", "WORLD")
    _property(photo_base, "MP_BASE_COLOR")
    _property(roughness, "MP_ROUGHNESS")
    _property(_scalar(material, float(photo_spec["metallic"])), "MP_METALLIC")
    _property(_scalar(material, float(source_spec.get("specular_ior_level", 0.5))), "MP_SPECULAR")
    _property(world_normal, "MP_NORMAL")
    unreal.MaterialEditingLibrary.layout_material_expressions(material)
    unreal.MaterialEditingLibrary.recompile_material(material)


def _slot_name(slot):
    for key in ("imported_material_slot_name", "material_slot_name"):
        value = str(slot.get_editor_property(key))
        if value and value != "None":
            return value
    interface = slot.get_editor_property("material_interface")
    return interface.get_name() if interface else ""


def _slot_paths(mesh):
    return [slot.get_editor_property("material_interface").get_path_name()
            if slot.get_editor_property("material_interface") else None
            for slot in mesh.get_editor_property("static_materials")]


def _duplicate_clone(base_material, package):
    clone = _owned_asset(package, unreal.Material)
    if clone is not None:
        return clone, False
    unreal.EditorAssetLibrary.make_directory(MATERIAL_DEST)
    clone = unreal.EditorAssetLibrary.duplicate_asset(base_material.get_path_name(), package)
    if not isinstance(clone, unreal.Material):
        raise RuntimeError(f"Photo material clone failed: {package}")
    unreal.EditorAssetLibrary.set_metadata_tag(clone, OWNER_TAG, OWNER)
    unreal.EditorAssetLibrary.set_metadata_tag(clone, GRAPH_HASH_TAG, "in-progress")
    return clone, True


def _offset_variants(item):
    """Group identical per-part roughness offsets into deterministic clones."""
    groups = {}
    for part, override in (item.get("partOverrides") or {}).items():
        if not isinstance(override, dict) or "roughness_offset" not in override:
            continue
        offset = float(override["roughness_offset"])
        groups.setdefault(offset, []).append(part)
    variants = []
    for offset, parts in sorted(groups.items(), key=lambda value: value[0]):
        sign = "Plus" if offset >= 0 else "Minus"
        magnitude = f"{abs(offset):.6f}".replace(".", "")
        variants.append({"suffix": f"RoughnessOffset{sign}{magnitude}",
                         "offset": offset, "parts": sorted(parts)})
    return variants


def _readback_clone(clone):
    expressions = list(unreal.MaterialEditingLibrary.get_material_expressions(clone))
    if clone.get_editor_property("blend_mode") != unreal.BlendMode.BLEND_OPAQUE:
        raise RuntimeError(f"Photo clone blend mode mismatch: {clone.get_path_name()}")
    if clone.get_editor_property("shading_model") != unreal.MaterialShadingModel.MSM_DEFAULT_LIT:
        raise RuntimeError(f"Photo clone shading model mismatch: {clone.get_path_name()}")
    if clone.get_editor_property("two_sided") or clone.get_editor_property("tangent_space_normal"):
        raise RuntimeError(f"Photo clone normal/two-sided contract mismatch: {clone.get_path_name()}")
    if not expressions:
        raise RuntimeError(f"Photo clone has no expressions: {clone.get_path_name()}")
    return {"asset": clone.get_path_name(), "expressionCount": len(expressions),
            "blendMode": "BLEND_OPAQUE", "shadingModel": "MSM_DEFAULT_LIT",
            "twoSided": False, "tangentSpaceNormal": False, "action": "readback"}


def _preflight(root: Path, plan: dict):
    """Resolve all source/base/mesh destinations before first UE mutation."""
    candidates = plan["photoShip"]["candidates"]
    art = read_json(root / "Art/Explorer/V2/art_manifest.json")
    meshes = {}
    for part in art["model_parts"]:
        path = f"{PART_DEST}/{part['name']}"
        mesh = unreal.load_asset(path)
        if not isinstance(mesh, unreal.StaticMesh):
            raise RuntimeError(f"Missing PhotoShip mesh dependency: {path}")
        meshes[part["name"]] = mesh
        for slot in mesh.get_editor_property("static_materials"):
            slot_name = _slot_name(slot)
            if slot_name not in part.get("materials", []):
                raise RuntimeError(f"Unknown PhotoShip material slot {slot_name!r} on {part['name']}")
    bases = {}
    clones = {}
    for item in candidates:
        source_name = item["material"]
        base_path = f"{BASE_DEST}/{source_name}"
        base = unreal.load_asset(base_path)
        if not isinstance(base, unreal.Material):
            raise RuntimeError(f"Missing original PhotoShip material dependency: {base_path}")
        source_spec = art["materials"][source_name]
        if source_spec.get("base_color_texture"):
            base_texture_path = _original_texture_path(root, source_spec["base_color_texture"])
            if not isinstance(unreal.load_asset(base_texture_path), unreal.Texture2D):
                raise RuntimeError(f"Missing original PhotoShip base paint texture dependency: {base_texture_path}")
        bases[source_name] = base
        package = item["photoMaterial"]
        existing = unreal.load_asset(package)
        if existing is not None and (not isinstance(existing, unreal.Material) or
                unreal.EditorAssetLibrary.get_metadata_tag(existing, OWNER_TAG) != OWNER):
            raise RuntimeError(f"Unowned or incompatible PhotoShip clone: {package}")
        clones[source_name] = existing
        for variant in _offset_variants(item):
            variant_package = f"{package}_{variant['suffix']}"
            existing_variant = unreal.load_asset(variant_package)
            if existing_variant is not None and (not isinstance(existing_variant, unreal.Material) or
                    unreal.EditorAssetLibrary.get_metadata_tag(existing_variant, OWNER_TAG) != OWNER):
                raise RuntimeError(f"Unowned or incompatible PhotoShip variant clone: {variant_package}")
    for item in plan["photoShip"]["functional"]:
        name = item.get("material")
        path = f"{BASE_DEST}/{name}"
        if not isinstance(unreal.load_asset(path), unreal.Material):
            raise RuntimeError(f"Missing functional PhotoShip material dependency: {path}")
    return art, meshes, bases, clones


def author_photo_ship(root: Path, plan: dict, imported: dict) -> dict:
    """Build owned clones and assign every matching candidate slot."""
    if unreal is None:
        raise RuntimeError("author_photo_ship requires Unreal Editor Python")
    root = Path(root).resolve()
    art, meshes, bases, clones = _preflight(root, plan)
    # Snapshot all mesh slots before any assignment.  This lets one failed
    # readback restore every changed slot, including across multiple parts.
    before = {part: _slot_paths(mesh) for part, mesh in meshes.items()}
    base_expression_counts = {name: len(list(unreal.MaterialEditingLibrary.get_material_expressions(material)))
                              for name, material in bases.items()}
    photo_materials = {}
    photo_part_materials = {}
    clone_readbacks = []
    map_textures = {}
    for task in plan["tasks"]:
        if task.get("kind") != "photo_texture" or "/PhotoShip/Textures/" not in task["package"]:
            continue
        map_textures[Path(task["sourceRelative"]).stem] = unreal.load_asset(task["object"])
    try:
        for item in plan["photoShip"]["candidates"]:
            name = item["material"]
            source_spec = art["materials"][name]
            map_set = item["mapSetSpec"]["maps"]
            textures = {channel: map_textures[Path(relative).stem]
                        for channel, relative in map_set.items()}
            if any(not isinstance(texture, unreal.Texture2D) for texture in textures.values()):
                raise RuntimeError(f"PhotoShip map texture missing for {name}")
            def build_one(photo_spec, package, existing_clone, variant_name="base"):
                fingerprint = canonical_hash({"owner": OWNER, "sourceMaterial": name,
                                               "sourceSpec": source_spec, "photoSpec": photo_spec,
                                               "mapTextures": {k: v.get_path_name() for k, v in textures.items()},
                                               "authorScript": sha256(Path(__file__))})
                clone = existing_clone
                changed = clone is None or unreal.EditorAssetLibrary.get_metadata_tag(clone, GRAPH_HASH_TAG) != fingerprint
                if clone is None:
                    clone, _ = _duplicate_clone(bases[name], package)
                if changed:
                    _build_clone(clone, source_spec, photo_spec, textures, root)
                    unreal.EditorAssetLibrary.set_metadata_tag(clone, OWNER_TAG, OWNER)
                    unreal.EditorAssetLibrary.set_metadata_tag(clone, GRAPH_HASH_TAG, fingerprint)
                    if not unreal.EditorAssetLibrary.save_loaded_asset(clone, only_if_is_dirty=False):
                        raise RuntimeError(f"PhotoShip clone save failed: {package}")
                clone_readbacks.append({**_readback_clone(clone), "material": name,
                                        "variant": variant_name, "graphHash": fingerprint,
                                        "action": "rebuilt" if changed else "reused"})
                return clone

            package = item["photoMaterial"]
            clone = build_one(item, package, clones[name])
            photo_materials[name] = clone
            photo_part_materials[name] = {part: clone for part in item["parts"]}
            for variant in _offset_variants(item):
                variant_package = f"{package}_{variant['suffix']}"
                existing_variant = unreal.load_asset(variant_package)
                variant_spec = {**item, "roughnessOffset": variant["offset"]}
                variant_clone = build_one(variant_spec, variant_package, existing_variant, variant["suffix"])
                for part in variant["parts"]:
                    if part not in item["parts"]:
                        raise RuntimeError(f"PhotoShip roughness override targets unassigned part: {name}/{part}")
                    photo_part_materials[name][part] = variant_clone

        assignments = []
        for item in plan["photoShip"]["candidates"]:
            source_name = item["material"]
            allowed_parts = set(item["parts"])
            for part_name in allowed_parts:
                mesh = meshes[part_name]
                clone = photo_part_materials[source_name][part_name]
                slots = mesh.get_editor_property("static_materials")
                for index, slot in enumerate(slots):
                    if _slot_name(slot) != source_name:
                        continue
                    previous = before[part_name][index]
                    mesh.set_material(index, clone)
                    after = _slot_paths(mesh)[index]
                    if after != clone.get_path_name():
                        raise RuntimeError(f"PhotoShip slot readback mismatch: {part_name}[{index}]")
                    assignments.append({"part": part_name, "slot": index, "material": source_name,
                                        "before": previous, "after": after})
        if not assignments:
            raise RuntimeError("PhotoShip bindings matched no existing mesh slots")
        # Every listed candidate must have appeared at least once.  This catches
        # a stale manifest or a mesh slot rename before saving any mesh.
        matched = {entry["material"] for entry in assignments}
        expected = {entry["material"] for entry in plan["photoShip"]["candidates"]}
        if matched != expected:
            raise RuntimeError(f"PhotoShip candidate slot coverage mismatch: missing={sorted(expected-matched)}")
        for part_name, mesh in meshes.items():
            after = _slot_paths(mesh)
            if len(after) != len(before[part_name]):
                raise RuntimeError(f"PhotoShip slot count changed: {part_name}")
            allowed = {(entry["slot"], entry["material"]) for entry in assignments if entry["part"] == part_name}
            for index, value in enumerate(after):
                if any(slot == index for slot, _ in allowed):
                    continue
                if value != before[part_name][index]:
                    raise RuntimeError(f"Non-target PhotoShip slot changed: {part_name}[{index}]")
            if any(entry["part"] == part_name for entry in assignments) and not unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False):
                raise RuntimeError(f"PhotoShip mesh save failed: {part_name}")
        for name, base in bases.items():
            current = len(list(unreal.MaterialEditingLibrary.get_material_expressions(base)))
            if current != base_expression_counts[name]:
                raise RuntimeError(f"Original PhotoShip material mutated: {name}")
    except Exception:
        # Recover only the exact mesh slots captured above.  Clone graphs remain
        # owned and source-hashed so a subsequent invocation can regenerate
        # them; base assets and functional materials are untouched.
        for part_name, mesh in meshes.items():
            current = _slot_paths(mesh)
            if current != before[part_name]:
                for index, path in enumerate(before[part_name]):
                    mesh.set_material(index, unreal.load_asset(path) if path else None)
                unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
        raise
    functional_names = {item["material"] for item in plan["photoShip"]["functional"]}
    functional_after = []
    for name in sorted(functional_names):
        path = f"{BASE_DEST}/{name}"
        asset = unreal.load_asset(path)
        if not isinstance(asset, unreal.Material):
            raise RuntimeError(f"Functional material disappeared: {path}")
        functional_after.append({"material": name, "asset": path,
                                 "preserved": True,
                                 "transmission": float(art["materials"][name].get("transmission", 0.0))})
    return {"status": "PHOTO_SHIP_AUTHORING_READBACK_ONLY", "owner": OWNER,
            "candidateCount": len(plan["photoShip"]["candidates"]), "mapSetCount": plan["photoShip"]["mapSetCount"],
            "functionalCount": len(functional_after), "materials": clone_readbacks,
            "assignments": assignments, "functionalPreserved": functional_after,
            "normal": {"sampler": "SAMPLERTYPE_NORMAL", "unpack": "once", "space": "local_triplanar_to_world",
                       "tangentSpaceNormal": False},
            "baseColor": "original base paint/decal * (1 + (Modulation_linear.R - 0.5) * 0.3)",
            "roughness": "photo Roughness_linear.R plus documented part offset",
            "canopyFunctionalContract": CANOPY_FUNCTIONAL_CONTRACT,
            "shaderCompilation": "REQUESTED_NOT_VERIFIED", "packagedComparison": "NOT_RUN"}


def offline_summary(root: Path = ROOT) -> dict:
    checked = validate_bindings(root)
    return {"status": "PHOTO_SHIP_SOURCE_VALIDATED", "candidateCount": len(checked["candidates"]),
            "functionalCount": len(checked["functional"]), "mapSetCount": len(checked["mapSets"]),
            "ue": "NOT_RUN"}


if __name__ == "__main__":
    print(json.dumps(offline_summary(), ensure_ascii=False))
