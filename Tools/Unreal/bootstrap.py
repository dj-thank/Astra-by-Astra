"""Idempotent STAR content bootstrap; run in a dedicated Unreal Editor process.

See IMPORT.md. Asset readback is not shader/cook/gameplay acceptance.
"""
from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import traceback

sys.path.insert(0, str(Path(__file__).resolve().parent))
from import_checks import ART_BASE, GAME_MODE, MAP_PATH, build_plan, object_path, read_json, source_path

try:
    import unreal
except ModuleNotFoundError:
    unreal = None

GENERATOR = "star-editor-bootstrap-v1"
OWNER_TAG = "StarBootstrapGenerator"
HASH_TAG = "StarBootstrapHash"
SCRIPT_HASH = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
# Source/settings imports have their own recipe version: fixing a material pin
# must not reimport 8K source textures or all WAV bytes. Known v1 metadata can be
# migrated only after the imported asset passes the unchanged readback contract.
IMPORT_RECIPE_VERSION = 1
LEGACY_IMPORT_SCRIPT_HASHES = ("d7d634d8442000ae1c77ec1d5c8fbe9e7ce227181cffb9bd15897712138c72f9",)
FBX_TRANSFORM_SETTINGS = {
    "convert_scene": True, "force_front_x_axis": False, "convert_scene_unit": True,
    "import_uniform_scale": 1.0, "transform_vertex_to_absolute": True,
    "bake_pivot_in_vertex": False,
}


def digest(value):
    return hashlib.sha256((SCRIPT_HASH + json.dumps(value, sort_keys=True)).encode("utf-8")).hexdigest()


def import_digest(task):
    value = {"recipeVersion": IMPORT_RECIPE_VERSION, **task}
    if task["kind"] == "static_mesh":
        value["fbxRecipeVersion"] = 3
        value["fbxTransformSettings"] = FBX_TRANSFORM_SETTINGS
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode("utf-8")).hexdigest()


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")
    temporary.replace(path)


def owned_asset(path, cls):
    asset = unreal.load_asset(path)
    if asset is not None and (not isinstance(asset, cls) or
            unreal.EditorAssetLibrary.get_metadata_tag(asset, OWNER_TAG) != GENERATOR):
        raise RuntimeError(f"Destination belongs to another author or class: {path}")
    return asset


def stamp(asset, fingerprint, provenance=None):
    unreal.EditorAssetLibrary.set_metadata_tag(asset, OWNER_TAG, GENERATOR)
    unreal.EditorAssetLibrary.set_metadata_tag(asset, HASH_TAG, fingerprint)
    if provenance is not None:
        unreal.EditorAssetLibrary.set_metadata_tag(asset, "StarSourceProvenance",
                                                  json.dumps(provenance, ensure_ascii=False, sort_keys=True))


def save(asset):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Save failed: {asset.get_path_name()}")


def connect(source, target, pin, output=""):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(source, output, target, pin):
        raise RuntimeError(f"Material connection failed: {pin}")


def property_input(source, prop, output=""):
    if not unreal.MaterialEditingLibrary.connect_material_property(source, output, getattr(unreal.MaterialProperty, prop)):
        raise RuntimeError(f"Material output connection failed: {prop}")


def node(material, cls, **properties):
    result = unreal.MaterialEditingLibrary.create_material_expression(material, cls, -400, 0)
    if result is None:
        raise RuntimeError(f"Cannot create expression {cls}")
    for key, value in properties.items():
        result.set_editor_property(key, value)
    return result


def scalar(material, value):
    return node(material, unreal.MaterialExpressionConstant, r=float(value))


def color(material, value):
    return node(material, unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(*value, 1.0))


def multiply(material, left, right, output=""):
    result = node(material, unreal.MaterialExpressionMultiply)
    connect(left, result, "A", output)
    connect(right, result, "B")
    return result


def texture_sample(material, path, repeat):
    texture = unreal.load_asset(path)
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError(f"Missing material texture: {path}")
    compression = texture.get_editor_property("compression_settings")
    sampler = (unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if compression == unreal.TextureCompressionSettings.TC_NORMALMAP
               else unreal.MaterialSamplerType.SAMPLERTYPE_MASKS if compression == unreal.TextureCompressionSettings.TC_MASKS
               else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if texture.get_editor_property("srgb")
               else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    sample = node(material, unreal.MaterialExpressionTextureSample, texture=texture, sampler_type=sampler)
    uv = node(material, unreal.MaterialExpressionTextureCoordinate, coordinate_index=0,
              u_tiling=float(repeat), v_tiling=float(repeat))
    # MaterialEditingLibrary matches MaterialGraphNode's shortened pin name.
    connect(uv, sample, "UVs")
    return sample


def material_expressions(material):
    return list(unreal.MaterialEditingLibrary.get_material_expressions(material))


def clear_material_graph(material):
    # UE 5.8 DeleteAllMaterialExpressions iterates Material->GetExpressions()
    # while DeleteMaterialExpression removes from the same collection. Iterate
    # a snapshot to avoid skipped ordinary/custom-output nodes on repeated builds.
    previous = material_expressions(material)
    for expression in previous:
        unreal.MaterialEditingLibrary.delete_material_expression(material, expression)
    remaining = material_expressions(material)
    if remaining:
        raise RuntimeError(f"Graph clear left {len(remaining)} expressions in {material.get_path_name()}")


def create_owned_material(package, fingerprint, force=False):
    material = owned_asset(package, unreal.Material)
    if material is not None and not force and unreal.EditorAssetLibrary.get_metadata_tag(material, HASH_TAG) == fingerprint:
        return material, False
    if material is None:
        folder, name = package.rsplit("/", 1)
        unreal.EditorAssetLibrary.make_directory(folder)
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
        if material is None:
            raise RuntimeError(f"Material creation failed: {package}")
        stamp(material, "in-progress")
    # Defer enabling Thin Translucent until its complete graph is connected.
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    clear_material_graph(material)
    return material, True


def readback_ship_material(material, spec):
    glass = spec.get("transmission", 0) > 0
    expressions = material_expressions(material)
    thin_count = sum(isinstance(expression, unreal.MaterialExpressionThinTranslucentMaterialOutput)
                     for expression in expressions)
    if thin_count != int(glass):
        raise RuntimeError(f"Expected {int(glass)} Thin Translucent outputs, got {thin_count}: {material.get_path_name()}")
    expected_blend = unreal.BlendMode.BLEND_TRANSLUCENT if glass else unreal.BlendMode.BLEND_OPAQUE
    expected_shading = unreal.MaterialShadingModel.MSM_THIN_TRANSLUCENT if glass else unreal.MaterialShadingModel.MSM_DEFAULT_LIT
    for key, value in {"blend_mode": expected_blend, "shading_model": expected_shading,
                       "two_sided": False, "used_with_nanite": not glass}.items():
        if material.get_editor_property(key) != value:
            raise RuntimeError(f"Ship material {key} mismatch: {material.get_path_name()}")
    usage = bool(unreal.MaterialEditingLibrary.has_material_usage(material, unreal.MaterialUsage.MATUSAGE_NANITE))
    if usage != (not glass):
        raise RuntimeError(f"Nanite usage getter mismatch: {material.get_path_name()}")
    if glass and material.get_editor_property("translucency_lighting_mode") != unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING:
        raise RuntimeError("Canopy must use Surface Forward Shading")
    return {"asset": material.get_path_name(), "expressionCount": len(expressions), "thinOutputCount": thin_count,
            "usedWithNanite": bool(material.get_editor_property("used_with_nanite")), "naniteUsageGetter": usage,
            "blendMode": str(expected_blend), "shadingModel": str(expected_shading)}


def create_ship_materials(plan, names=None, force=False, readbacks=None):
    output = {}
    for name, spec in plan["art"]["materials"].items():
        if names is not None and name not in names:
            continue
        package = f"/Game/Star/Art/ExplorerV2/Materials/{name}"
        recipe = {"spec": spec, "textures": plan["artTextures"]}
        if spec.get("transmission", 0) > 0: recipe["thinGlassRecipe"] = 2
        fingerprint = digest(recipe)
        material, changed = create_owned_material(package, fingerprint, force=force)
        output[name] = package
        if not changed:
            result = readback_ship_material(material, spec)
            if readbacks is not None:
                readbacks.append({**result, "action": "reused"})
            continue
        glass = spec.get("transmission", 0) > 0
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT if glass else unreal.BlendMode.BLEND_OPAQUE)
        # The canopy is a closed 1cm plate. The first front-facing interface
        # alone uses Thin Translucent's built-in two-interface Fresnel model.
        material.set_editor_property("two_sided", False)
        material.set_editor_property("tangent_space_normal", True)
        unreal.MaterialEditingLibrary.set_base_material_usage(material, unreal.MaterialUsage.MATUSAGE_NANITE, not glass)
        base = color(material, [0, 0, 0] if glass else spec["base_color_linear"])
        if spec.get("base_color_texture"):
            base = texture_sample(material, plan["artTextures"][spec["base_color_texture"]], spec.get("paint_uv_repeat", spec.get("uv_repeat", 1)))
            if not spec.get("base_color_texture_tint_baked", False):
                base = multiply(material, base, color(material, spec["base_color_linear"]), "RGB")
        property_input(base, "MP_BASE_COLOR")
        for field, prop in (("metallic", "MP_METALLIC"), ("roughness", "MP_ROUGHNESS")):
            if spec.get(field + "_texture"):
                repeat = spec.get("paint_uv_repeat", spec.get("uv_repeat", 1))
                value = texture_sample(material, plan["artTextures"][spec[field + "_texture"]], repeat)
                value = multiply(material, value, scalar(material, spec.get(field + "_texture_multiplier", 1)), "R")
            else:
                value = scalar(material, spec[field])
            property_input(value, prop)
        if spec.get("normal_texture"):
            normal = texture_sample(material, plan["artTextures"][spec["normal_texture"]],
                                    spec.get("normal_uv_repeat", spec.get("uv_repeat", 1)))
            # Blender NormalMap strength .22 is in the exporter for CeramicMicro;
            # SeatWeave explicitly carries .5 in the manifest. Renormalize XY gain.
            strength = spec.get("normal_strength", .22 if "CeramicMicro" in spec["normal_texture"] else 1.0)
            gain = color(material, [strength, strength, 1.0])
            normal = multiply(material, normal, gain, "RGB")
            normalized = node(material, unreal.MaterialExpressionNormalize)
            connect(normal, normalized, "VectorInput")
            property_input(normalized, "MP_NORMAL")
        if spec.get("emission_strength", 0) > 0:
            property_input(multiply(material, base, scalar(material, spec["emission_strength"])), "MP_EMISSIVE_COLOR")
        if glass:
            material.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
            material.set_editor_property("use_translucency_vertex_fog", False)
            # Thin translucent provides tinted transmission and white specular in
            # one pass. No thick-volume refraction on a single cockpit pane.
            thin = node(material, unreal.MaterialExpressionThinTranslucentMaterialOutput)
            # UE applies Beer-Lambert T^(1/NoV). Blender's colored glass base
            # must not be reused as transmission, which blue-tinted the Moon.
            connect(color(material, [0.995, 0.995, 0.995]), thin, "TransmittanceColor")
            # Opacity in this model is diffuse coating coverage, not clarity.
            property_input(scalar(material, 0.0), "MP_OPACITY")
            property_input(scalar(material, spec.get("specular_ior_level", .5)), "MP_SPECULAR")
            material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_THIN_TRANSLUCENT)
        unreal.MaterialEditingLibrary.layout_material_expressions(material)
        unreal.MaterialEditingLibrary.recompile_material(material)
        stamp(material, fingerprint, {"manifest": "Art/Explorer/V2/art_manifest.json", "material": spec,
                                     "glassOpacityArtistic": 0.0 if glass else None,
                                     "glassTransmittanceLinear": [0.995,0.995,0.995] if glass else None})
        save(material)
        result = readback_ship_material(unreal.load_asset(package), spec)
        if readbacks is not None:
            readbacks.append({**result, "action": "rebuilt"})
    return output


def create_sun():
    from Materials import create_materials as library
    source = library.shader_source("Sun.ush")
    fingerprint = digest({"name": "M_Sun", "source": source, "version": 2})
    material, changed = create_owned_material("/Game/Star/Materials/M_Sun", fingerprint)
    if changed:
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
        material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        inputs = {
            "CameraVector": node(material, unreal.MaterialExpressionCameraVectorWS),
            "SurfaceNormal": node(material, unreal.MaterialExpressionPixelNormalWS),
        }
        for name, value in {"SunRadiance": 18000000.0, "EarthRadiusMeters": 0.0}.items():
            inputs[name] = node(material, unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=value)
        for name, value in {"EarthCameraLocal": (0,0,3), "EarthAxisX": (1,0,0),
                            "EarthAxisY": (0,-1,0), "EarthAxisZ": (0,0,1)}.items():
            inputs[name] = node(material, unreal.MaterialExpressionVectorParameter,
                parameter_name=name, default_value=unreal.LinearColor(*value,1.0))
        expression = library._custom(material,"Sun.ush",inputs,3)
        property_input(expression,"MP_EMISSIVE_COLOR")
        unreal.MaterialEditingLibrary.recompile_material(material)
        stamp(material, fingerprint, {"type": "analytic limb darkening and Earth RGB extinction; approximate atmosphere"})
        save(material)
    return material.get_path_name()


def import_asset(task):
    cls = {"texture": unreal.Texture2D, "static_mesh": unreal.StaticMesh, "audio": unreal.SoundWave,
           "font_face": unreal.FontFace}[task["kind"]]
    existing = owned_asset(task["package"], cls)
    fingerprint = import_digest(task)
    if existing is not None:
        previous = unreal.EditorAssetLibrary.get_metadata_tag(existing, HASH_TAG)
        if previous == fingerprint:
            return existing, "reused"
        known_legacy = {hashlib.sha256((script_hash + json.dumps(task, sort_keys=True)).encode("utf-8")).hexdigest()
                        for script_hash in LEGACY_IMPORT_SCRIPT_HASHES}
        if previous in known_legacy:
            return existing, "reused_legacy_hash"
    job = unreal.AssetImportTask()
    folder, name = task["package"].rsplit("/", 1)
    for key, value in {"filename": task["source"], "destination_path": folder, "destination_name": name,
                       "automated": True, "replace_existing": existing is not None,
                       "replace_existing_settings": True, "save": False, "async_": False}.items():
        job.set_editor_property(key, value)
    if task["kind"] == "static_mesh":
        options = unreal.FbxImportUI()
        for key, value in {"automated_import_should_detect_type": False, "import_mesh": True,
                           "import_as_skeletal": False, "import_materials": False, "import_textures": False,
                           "import_animations": False, "mesh_type_to_import": unreal.FBXImportType.FBXIT_STATIC_MESH}.items():
            options.set_editor_property(key, value)
        data = options.get_editor_property("static_mesh_import_data")
        # Confirmed by a new-name HullStructure probe on UE 5.8.2: absolute
        # transform applies the FBX 100x unit conversion; front-X forcing rotates
        # this Blender export by 90 degrees, so it must remain disabled.
        properties = {**FBX_TRANSFORM_SETTINGS,
                      "import_translation": unreal.Vector(0, 0, 0), "import_rotation": unreal.Rotator(0, 0, 0),
                      "combine_meshes": True, "build_nanite": False,
                      "auto_generate_collision": False, "generate_lightmap_u_vs": False,
                      "normal_import_method": unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS}
        for key, value in properties.items():
            data.set_editor_property(key, value)
        if existing is not None:
            # UE's unattended UReimportFbxStaticMeshFactory replaces task options
            # with Mesh.asset_import_data even with replace_existing_settings.
            # Update only this ownership-checked mesh's actual stored recipe.
            stored_data = existing.get_editor_property("asset_import_data")
            if not isinstance(stored_data, unreal.FbxStaticMeshImportData):
                raise RuntimeError(f"Owned mesh has unexpected import data: {task['object']}")
            for key, value in properties.items():
                stored_data.set_editor_property(key, value)
        job.set_editor_property("factory", unreal.FbxFactory())
        job.set_editor_property("options", options)
    elif task["kind"] == "audio":
        factory = unreal.SoundFactory()
        factory.set_editor_property("auto_create_cue", False)
        job.set_editor_property("factory", factory)
    elif task["kind"] == "font_face":
        factory = unreal.FontFileImportFactory()
        factory.set_editor_property("batch_create_font_asset", unreal.BatchCreateFontAsset.NO)
        job.set_editor_property("factory", factory)
    else:
        factory = unreal.TextureFactory()
        factory.set_editor_property("create_material", False)
        job.set_editor_property("factory", factory)
    unreal.EditorAssetLibrary.make_directory(folder)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([job])
    objects = list(job.get_objects())
    asset = unreal.load_asset(task["object"])
    if not isinstance(asset, cls) or not any(o.get_path_name() == task["object"] for o in objects):
        raise RuntimeError(f"Import did not return expected {cls}: {task['object']}; returned {[o.get_path_name() for o in objects]}")
    unexpected = [o.get_path_name() for o in objects if o.get_path_name() != task["object"]]
    if unexpected:
        raise RuntimeError(f"Import produced unexpected extra assets: {unexpected}")
    stamp(asset, fingerprint, task["provenance"])
    return asset, "imported"


def configure_asset(task, asset, ship_materials):
    settings = task["settings"]
    if task["kind"] == "texture":
        for key, value in {"srgb": settings["srgb"], "compression_settings": getattr(unreal.TextureCompressionSettings, settings["compression"]),
                           "virtual_texture_streaming": False, "flip_green_channel": settings.get("flip_green_channel", settings["normal"]),
                           "lod_bias": 0, "max_texture_size": settings.get("max_texture_size",0),
                           "never_stream": settings.get("never_stream",False)}.items():
            asset.set_editor_property(key, value)
        for address in ("address_x", "address_y"):
            if address in settings:
                asset.set_editor_property(address, getattr(unreal.TextureAddress, settings[address]))
    elif task["kind"] == "audio":
        cue = settings["cue"]
        for key, value in {"looping": cue["loop"], "volume": cue.get("volume_multiplier", 1.0),
                           "compression_quality": 90,
                           "loading_behavior": unreal.SoundWaveLoadingBehavior.LOAD_ON_DEMAND}.items():
            asset.set_editor_property(key, value)
    elif task["kind"] == "font_face":
        asset.set_editor_property("loading_policy", unreal.FontLoadingPolicy.INLINE)
    else:
        # Match actual imported FBX slots by name; do not assume manifest order
        # survived FBX slot compaction/reordering.
        allowed = set(settings["part"]["materials"])
        for index, slot in enumerate(asset.get_editor_property("static_materials")):
            slot_name = str(slot.get_editor_property("imported_material_slot_name"))
            if slot_name not in allowed:
                slot_name = str(slot.get_editor_property("material_slot_name"))
            if slot_name not in allowed:
                raise RuntimeError(f"Unknown imported material slot for {task['package']}: {slot_name}")
            asset.set_material(index, unreal.load_asset(ship_materials[slot_name]))
        subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        nanite = subsystem.get_nanite_settings(asset)
        nanite.set_editor_property("enabled", settings["nanite"])
        subsystem.set_nanite_settings(asset, nanite, True)


def readback(task, asset):
    if asset.get_path_name() != task["object"]:
        raise RuntimeError(f"Asset path mismatch: {task['object']}")
    result = {"asset": asset.get_path_name(), "kind": task["kind"], "sourceSha256": task["sha256"]}
    settings = task["settings"]
    if task["kind"] == "texture":
        for key, expected in {"srgb": settings["srgb"], "virtual_texture_streaming": False,
                              "flip_green_channel": settings.get("flip_green_channel", settings["normal"])}.items():
            if asset.get_editor_property(key) != expected:
                raise RuntimeError(f"Texture {key} mismatch: {task['object']}")
        if asset.get_editor_property("compression_settings") != getattr(unreal.TextureCompressionSettings, settings["compression"]):
            raise RuntimeError(f"Texture compression mismatch: {task['object']}")
        result["settings"] = settings
    elif task["kind"] == "audio":
        cue, actual = settings["cue"], settings["actual_wav"]
        for key, expected in {"num_channels": cue["channels"], "imported_sample_rate": cue["sample_rate"], "looping": cue["loop"]}.items():
            if asset.get_editor_property(key) != expected:
                raise RuntimeError(f"SoundWave {key} mismatch: {task['object']}")
        duration = float(asset.get_editor_property("duration"))
        if abs(duration - actual["duration_seconds"]) > max(.01, 2 / actual["sample_rate"]):
            raise RuntimeError(f"SoundWave duration mismatch: {task['object']}")
        result.update({"looping": cue["loop"], "channels": cue["channels"], "durationSeconds": duration})
        if asset.get_editor_property("loading_behavior") != unreal.SoundWaveLoadingBehavior.LOAD_ON_DEMAND:
            raise RuntimeError(f"SoundWave must stream on demand: {task['object']}")
    elif task["kind"] == "font_face":
        if asset.get_editor_property("loading_policy") != unreal.FontLoadingPolicy.INLINE:
            raise RuntimeError("Japanese font face is not embedded Inline")
        result["loadingPolicy"] = "INLINE"
    else:
        data = asset.get_editor_property("asset_import_data")
        for key, expected_value in FBX_TRANSFORM_SETTINGS.items():
            if data.get_editor_property(key) != expected_value:
                raise RuntimeError(f"Saved FBX {key} setting mismatch: {task['object']}")
        bounds = asset.get_bounding_box()
        measured = [float(getattr(bounds.max, axis) - getattr(bounds.min, axis)) for axis in ("x", "y", "z")]
        expected = [x * 100 for x in settings["part"]["dimensions_m"]]
        if any(abs(got - want) > max(1.0, want * .02) for got, want in zip(measured, expected)):
            raise RuntimeError(f"FBX axes/units mismatch for {task['object']}: {measured} cm; expected {expected}")
        nanite = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).get_nanite_settings(asset)
        if bool(nanite.get_editor_property("enabled")) != settings["nanite"]:
            raise RuntimeError(f"Nanite setting mismatch: {task['object']}")
        slots = list(asset.get_editor_property("static_materials"))
        if not slots or any(slot.get_editor_property("material_interface") is None for slot in slots):
            raise RuntimeError(f"Unbound ship material: {task['object']}")
        result.update({"dimensionsCm": measured, "nanite": settings["nanite"], "materialSlots": len(slots),
                       "fbxTransformSettings": FBX_TRANSFORM_SETTINGS})
    return result


def create_composite_font(plan):
    spec = plan["fontPlan"]["unreal_import"]
    missing = [name for name in ("FontData", "TypefaceEntry", "Typeface", "CompositeFont")
               if not hasattr(unreal, name)]
    if missing:
        # UE 5.8.2 does not expose the nested composite structs. The runtime has
        # an exact file-backed Japanese fallback; do not create an empty UFont
        # or claim composite readiness. FontFace was already saved/read back.
        face = unreal.load_asset(spec["font_face_destination"])
        return {"status": "FONT_FACE_INLINE_WITH_OTF_FALLBACK", "face": face.get_path_name(),
                "fallbackFile": plan["fontPlan"]["font_source"],
                "requiredComposite": spec["required_composite_font"], "compositeUsable": False,
                "compositeStatus": "SKIPPED_PYTHON_STRUCTS_UNAVAILABLE", "missingPythonTypes": missing,
                "verification": "Packaged Japanese glyph rendering remains pending; runtime must reject an empty composite."}
    package = spec["required_composite_font"].split(".")[0]
    font = owned_asset(package, unreal.Font)
    fingerprint = digest(plan["fontPlan"])
    if font is None:
        folder, name = package.rsplit("/", 1)
        font = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Font, unreal.FontFactory())
        if font is None:
            raise RuntimeError("Japanese composite font creation failed")
        stamp(font, "in-progress")
    if unreal.EditorAssetLibrary.get_metadata_tag(font, HASH_TAG) != fingerprint:
        font.set_editor_property("font_cache_type", unreal.FontCacheType.RUNTIME)
        face = unreal.load_asset(spec["font_face_destination"])
        data = unreal.FontData()
        data.set_editor_property("font_face_asset", face)
        entry = unreal.TypefaceEntry()
        entry.set_editor_property("name", spec["default_typeface_name"])
        entry.set_editor_property("font", data)
        typeface = unreal.Typeface()
        typeface.set_editor_property("fonts", [entry])
        composite = unreal.CompositeFont()
        composite.set_editor_property("default_typeface", typeface)
        font.set_editor_property("composite_font", composite)
        font.set_editor_property("legacy_font_name", spec["default_typeface_name"])
        stamp(font, fingerprint, plan["fontPlan"])
        save(font)
    entries = font.get_editor_property("composite_font").get_editor_property("default_typeface").get_editor_property("fonts")
    if len(entries) != 1 or str(entries[0].get_editor_property("name")) != spec["default_typeface_name"]:
        raise RuntimeError("Composite font Regular typeface missing")
    actual = entries[0].get_editor_property("font").get_editor_property("font_face_asset")
    if actual is None or actual.get_path_name() != object_path(spec["font_face_destination"]):
        raise RuntimeError("Composite font face reference mismatch")
    return {"asset": font.get_path_name(), "face": actual.get_path_name(), "typeface": spec["default_typeface_name"]}


def create_level():
    game_mode = unreal.load_class(None, GAME_MODE)
    if game_mode is None:
        raise RuntimeError(f"Compiled game mode unavailable: {GAME_MODE}")
    existing = owned_asset(MAP_PATH, unreal.World)
    subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ok = subsystem.load_level(MAP_PATH) if existing is not None else subsystem.new_level(MAP_PATH, False)
    if not ok:
        raise RuntimeError(f"Could not load/create persistent level: {MAP_PATH}")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", game_mode)
    stamp(world, digest({"map": MAP_PATH, "gameMode": GAME_MODE}))
    if not subsystem.save_current_level() or not subsystem.load_level(MAP_PATH):
        raise RuntimeError("Persistent level save/reload failed")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    actual = world.get_world_settings().get_editor_property("default_game_mode")
    if actual is None or actual.get_path_name() != GAME_MODE:
        raise RuntimeError("Persisted level game mode override mismatch")
    return {"asset": world.get_path_name(), "gameMode": actual.get_path_name(), "reloaded": True}


def require_dedicated_editor(root):
    if unreal is None:
        raise RuntimeError("bootstrap.py requires Unreal Editor Python; use import_checks.py offline")
    if Path(unreal.Paths.project_dir()).resolve() != root:
        raise RuntimeError("Script/project mismatch; execute the script from the opened Star project")
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if level_subsystem.is_in_play_in_editor():
        raise RuntimeError("Stop PIE before bootstrap")
    # Dedicated process only: loading/new_level closes the existing map unsaved.
    if unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages() or unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages():
        raise RuntimeError("Unsaved editor content exists; use a fresh dedicated editor process")


def repair_ship_materials(root, report):
    """Rebuild only existing owned ship materials; no source asset imports/map writes."""
    require_dedicated_editor(root)
    art = read_json(source_path(root, "Art/Explorer/V2/art_manifest.json"))
    if art.get("version") != 2:
        raise ValueError("Material repair requires ASTER-24 V2 manifest")
    textures = {}
    for spec in art["materials"].values():
        for key in ("base_color_texture", "normal_texture", "roughness_texture", "metallic_texture"):
            if spec.get(key):
                path = source_path(root / ART_BASE, spec[key])
                package = f"/Game/Star/Art/ExplorerV2/Textures/{path.stem}"
                asset = owned_asset(package, unreal.Texture2D)
                if asset is None:
                    raise RuntimeError(f"Material repair requires existing texture: {package}")
                textures[spec[key]] = asset.get_path_name()
    before = []
    for name in art["materials"]:
        package = f"/Game/Star/Art/ExplorerV2/Materials/{name}"
        material = owned_asset(package, unreal.Material)
        if material is None:
            raise RuntimeError(f"Material repair requires existing owned material: {package}")
        expressions = material_expressions(material)
        before.append({"asset": material.get_path_name(), "expressionCount": len(expressions),
                       "thinOutputCount": sum(isinstance(e, unreal.MaterialExpressionThinTranslucentMaterialOutput) for e in expressions),
                       "usedWithNanite": bool(material.get_editor_property("used_with_nanite"))})
    report.update({"stage": "rebuild_ship_materials", "before": before, "materials": []})
    plan = {"art": art, "artTextures": textures}
    create_ship_materials(plan, force=True, readbacks=report["materials"])
    # Exercise the exact failed boundary again on glass only. A hash-cache hit
    # would never prove that the second clear/rebuild leaves just one output.
    report["stage"] = "repeat_glass_rebuild"
    repeated = []
    glass_names = [name for name, spec in art["materials"].items() if spec.get("transmission", 0) > 0]
    create_ship_materials(plan, names=glass_names, force=True, readbacks=repeated)
    first_counts = {item["asset"]: item["expressionCount"] for item in report["materials"]}
    if any(item["expressionCount"] != first_counts[item["asset"]] for item in repeated):
        raise RuntimeError("Repeated glass rebuild changed expression count")
    report.update({"status": "SHIP_MATERIALS_SAVED_READBACK_VERIFIED", "stage": "complete",
                   "repeatedGlassReadbacks": repeated, "sourceAssetsImported": 0,
                   "runtimeAssetsWritten": False, "mapWritten": False,
                   "shaderCompilation": "REQUESTED_NOT_VERIFIED", "gameplay": "NOT_RUN"})


def run(root, audio_pack, report):
    require_dedicated_editor(root)
    plan = build_plan(root, audio_pack)
    write_json(root / "work/import/source-plan.json", plan)
    report.update({"stage": "source_preflight_complete", "artManifestSha256": plan["artManifestSha256"],
                   "audioManifestSha256": plan["audioManifestSha256"], "assets": []})
    # Prevent UE5's Interchange name/pipeline overrides for this explicit legacy
    # FBX/TextureFactory/SoundFactory import contract. This CVar is process-local.
    editor_world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    unreal.SystemLibrary.execute_console_command(editor_world, "Interchange.FeatureFlags.Import.FBX 0")
    classes = {"texture": unreal.Texture2D, "static_mesh": unreal.StaticMesh, "audio": unreal.SoundWave, "font_face": unreal.FontFace}
    for task in plan["tasks"]:
        owned_asset(task["package"], classes[task["kind"]])
    owned_asset(MAP_PATH, unreal.World)
    owned_asset("/Game/Star/Materials/M_Sun", unreal.Material)
    owned_asset(plan["fontPlan"]["unreal_import"]["required_composite_font"], unreal.Font)
    for name in plan["art"]["materials"]:
        owned_asset(f"/Game/Star/Art/ExplorerV2/Materials/{name}", unreal.Material)
    ship_materials = {}

    def import_group(kind):
        report["stage"] = "import_" + kind
        for task in (t for t in plan["tasks"] if t["kind"] == kind):
            unreal.log(f"STAR bootstrap {kind}: {task['object']}")
            asset, action = import_asset(task)
            if action == "imported":
                configure_asset(task, asset, ship_materials)
                save(asset)
            result = readback(task, asset)
            if action == "reused_legacy_hash":
                stamp(asset, import_digest(task), task["provenance"])
                save(asset)
            result["action"] = action
            suffix = ".umap" if kind == "world" else ".uasset"
            disk = root / "Content" / (task["package"].removeprefix("/Game/") + suffix)
            if not disk.is_file():
                raise RuntimeError(f"Saved asset file missing: {disk}")
            report["assets"].append(result)

    import_group("texture")
    ship_materials.update(create_ship_materials(plan))
    module_spec = importlib.util.spec_from_file_location("star_planet_materials", root / "Tools/Unreal/Materials/create_materials.py")
    builder = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(builder)
    report["planetMaterials"] = builder.build_materials(plan["planetTextures"])
    report["sunMaterial"] = create_sun()
    for path in [*ship_materials.values(), *report["planetMaterials"]["materials"].values(),
                 *report["planetMaterials"]["instances"].values(), report["sunMaterial"]]:
        if not isinstance(unreal.load_asset(path), unreal.MaterialInterface):
            raise RuntimeError(f"Material readback failed: {path}")
    import_group("static_mesh")
    import_group("audio")
    import_group("font_face")
    report["font"] = create_composite_font(plan)
    report["stage"] = "persistent_level"
    report["level"] = create_level()
    # Publication is last: all requested source assets exist, are saved, and have
    # passed their typed settings/geometry/audio readback. Preserve old JSON on error.
    runtime = plan["runtimeAssets"]
    runtime["sourceArtManifestSha256"] = plan["artManifestSha256"]
    write_json(root / "Content/Star/Data/runtime_assets.json", runtime)
    report.update({"status": "ASSETS_SAVED_READBACK_VERIFIED", "stage": "complete",
                   "runtimeAssetsWritten": True, "shipMaterials": ship_materials,
                   "shaderCompilation": "REQUESTED_NOT_VERIFIED", "cook": "NOT_RUN",
                   "gameplay": "NOT_RUN", "physicalJoystick": "NOT_RUN"})


def main():
    root = Path(__file__).resolve().parents[2]
    audio_pack = Path(os.environ.get("STAR_AUDIO_PACK", str(root / "outputs/audio/STAR-Audio-Pack-v1"))).resolve()
    mode = os.environ.get("STAR_BOOTSTRAP_MODE", "full")
    if mode not in ("full", "ship-materials"):
        raise ValueError(f"Unknown STAR_BOOTSTRAP_MODE: {mode}")
    receipt = root / "work/import" / ("ship-material-repair-result.json" if mode == "ship-materials" else "bootstrap-result.json")
    report = {"schemaVersion": 1, "generator": GENERATOR, "scriptSha256": SCRIPT_HASH,
              "startedAt": datetime.now(timezone.utc).isoformat(), "project": str(root),
              "mode": mode, "status": "FAILED", "stage": "initial", "runtimeAssetsWritten": False}
    try:
        if mode == "ship-materials":
            repair_ship_materials(root, report)
        else:
            run(root, audio_pack, report)
    except Exception as error:
        report["error"] = str(error)
        report["traceback"] = traceback.format_exc()
        raise
    finally:
        report["finishedAt"] = datetime.now(timezone.utc).isoformat()
        write_json(receipt, report)
        if unreal is not None:
            unreal.log(f"STAR bootstrap receipt: {receipt}; status={report['status']}")


if __name__ == "__main__":
    main()
