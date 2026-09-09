"""Dedicated-editor import of V3 measured/photo data and cockpit candidates."""
from pathlib import Path
import importlib.util
import hashlib
import json
import sys
import unreal

ROOT=Path(__file__).resolve().parents[2]
OUTPUT=ROOT/"work"/("v04" if (ROOT/"Content/Star/Art/CockpitV3/manifest_v04.json").exists() else "v03")
sys.path.insert(0,str(Path(__file__).resolve().parent))
import bootstrap
import import_checks
import apply_photo_pass

bootstrap.require_dedicated_editor(ROOT)
plan=import_checks.build_photo_plan(ROOT)
paths=apply_photo_pass._planet_asset_paths(plan)
specs=[
    ("lunar_far_photo","Content/Star/Art/LunarPhotoV4/apollo17_far_hills_photo_rgba_16k.png",
     "/Game/Star/Art/LunarPhotoV4/T_FarHills",True,"TC_BC7",16384),
    ("lunar_orbital","Content/Star/Art/LunarPanorama/OrbitalResidual/apollo17_orbital_residual_confidence_8192_linear.png",
     "/Game/Star/Art/LunarPanorama/OrbitalResidual/T_OrbitalResidual",False,"TC_BC7",8192),
    ("bright_stars","Content/Star/Art/PhotoSkyV3/hiptyc_2020_16k.exr",
     "/Game/Star/Art/PhotoSkyV3/T_BrightStars16K",False,"TC_HDR_COMPRESSED",16384),
    ("photo_sky","Content/Star/Art/PhotoSkyV3/eso_diffuse_8k.png",
     "/Game/Star/Art/PhotoSkyV3/T_Diffuse8K",True,"TC_BC7",8192),
    ("lunar_mountain","Content/Star/Art/LunarPanorama/FullRegion/apollo17_full_region_photo_rgba.png",
     "/Game/Star/Art/LunarPanorama/FullRegion/T_Mountains",True,"TC_BC7",8192),
    ("earth_detail","Content/Star/Art/EarthDetailV3/T_EarthDetail_Andaman_Aqua_20250906_8K.png",
     "/Game/Star/Art/EarthDetailV3/T_EarthDetail_Andaman_Aqua_20250906_8K",True,"TC_BC7",8192),
    ("lunar_meso","Content/Star/Art/LunarGroundV3/apollo17_meso_linear.png",
     "/Game/Star/Art/LunarGroundV3/T_Meso",False,"TC_MASKS",0),
    ("lunar_meso_normal","Content/Star/Art/LunarGroundV3/apollo17_meso_normal_dx.png",
     "/Game/Star/Art/LunarGroundV3/T_MesoNormal",False,"TC_NORMALMAP",0),
]
readbacks=[]
for key,source,destination,srgb,compression,size in specs:
    settings=dict(srgb=srgb,compression=compression,normal=compression=="TC_NORMALMAP",flip_green_channel=False,
                  address_x="TA_CLAMP" if key in ("earth_detail","lunar_mountain","lunar_orbital") else "TA_WRAP",
                  address_y="TA_CLAMP" if key in ("earth_detail","lunar_mountain","lunar_orbital","lunar_far_photo","photo_sky","bright_stars") else "TA_WRAP",mips=True,max_texture_size=size)
    task=import_checks._photo_task(ROOT,source,destination,settings,{"version":3,"source":source})
    readbacks.append(apply_photo_pass._import_generic_texture(task));paths[key]=task["object"]
sky=unreal.load_asset(paths["photo_sky"])
sky.set_editor_property("compression_settings",unreal.TextureCompressionSettings.TC_BC7)
bootstrap.save(sky)
builder=apply_photo_pass._load_planet_builder()
result=builder.build_materials(paths)
# The import module owns new cockpit assets only; route the two existing
# runtime part names to the new meshes after their actual import succeeds.
file=ROOT/"Tools/Lookdev/CockpitV3/import_assets.py"
sys.path.insert(0,str(file.parent))
from cockpit_import_fingerprint import import_fingerprint
cockpit_manifest_path=ROOT/"Content/Star/Art/CockpitV3/manifest_v04.json"
if not cockpit_manifest_path.exists():cockpit_manifest_path=ROOT/"Data/cockpit_v3_manifest.json"
cockpit_recipe_hash=import_fingerprint(ROOT/"Content/Star/Art/CockpitV3",cockpit_manifest_path,file)
ue_topology_path=ROOT/"Content/Star/Art/CockpitV3/ue_import_expectations_v04.json"
ue_topology=json.loads(ue_topology_path.read_text(encoding="utf-8"))["parts"] if ue_topology_path.exists() and cockpit_manifest_path.name=="manifest_v04.json" else {}
cockpit_validation_path=ROOT/"Content/Star/Art/CockpitV3/validation_v04.json"
if not cockpit_validation_path.exists():cockpit_validation_path=ROOT/"Art/Explorer/CockpitV3/validation.json"
cockpit_validation=json.loads(cockpit_validation_path.read_text(encoding="utf-8"))
cockpit_package_root="/Game/Star/Art/CockpitV4" if cockpit_validation_path.name=="validation_v04.json" else "/Game/Star/Art/CockpitV3"
cockpit_owner="STAR_CockpitV4" if cockpit_package_root.endswith("V4") else "STAR_CockpitV3"
for part in cockpit_validation["fbx_readback"]:
    source=ROOT/"Content/Star/Art/CockpitV3/Parts"/(part["part"]+".fbx")
    if hashlib.sha256(source.read_bytes()).hexdigest()!=part["sha256"]:
        raise RuntimeError("Cockpit source changed since geometry validation")
cockpit_existing=[unreal.load_asset(cockpit_package_root+"/Parts/"+p["part"]) for p in cockpit_validation["fbx_readback"]]
if any(cockpit_existing) and not all(cockpit_existing):
    raise RuntimeError("Partial cockpit import; inspect before replacement")
needs_cockpit_import=not any(cockpit_existing) or any(
    unreal.EditorAssetLibrary.get_metadata_tag(asset,"STARSourceSHA256")!=part["sha256"] or
    unreal.EditorAssetLibrary.get_metadata_tag(asset,"STARImportRecipeSHA256")!=cockpit_recipe_hash
    for asset,part in zip(cockpit_existing,cockpit_validation["fbx_readback"]))
if needs_cockpit_import:
    module_spec=importlib.util.spec_from_file_location("star_cockpit_v3_import",file)
    module=importlib.util.module_from_spec(module_spec);module_spec.loader.exec_module(module)
for part in cockpit_validation["fbx_readback"]:
    asset=unreal.load_asset(cockpit_package_root+"/Parts/"+part["part"])
    source=ROOT/"Content/Star/Art/CockpitV3/Parts"/(part["part"]+".fbx")
    if hashlib.sha256(source.read_bytes()).hexdigest()!=part["sha256"]:
        raise RuntimeError("Cockpit source changed since validated import")
    if not isinstance(asset,unreal.StaticMesh) or unreal.EditorAssetLibrary.get_metadata_tag(asset,"STAROwner")!=cockpit_owner:
        raise RuntimeError("Unexpected cockpit ownership/class")
    if unreal.EditorAssetLibrary.get_metadata_tag(asset,"STARSourceSHA256")!=part["sha256"]:
        raise RuntimeError("Actual cockpit import source tag differs from validated source")
    if unreal.EditorAssetLibrary.get_metadata_tag(asset,"STARImportRecipeSHA256")!=cockpit_recipe_hash:
        raise RuntimeError("Cockpit texture/material import recipe differs from current source")
    expected_triangles=ue_topology.get(part["part"],{}).get("expectedRenderTriangles",part["triangles"])
    triangle_tolerance=0 if part["part"] in ue_topology else max(1000,part["triangles"]*0.01)
    if abs(asset.get_num_triangles(0)-expected_triangles)>triangle_tolerance:
        raise RuntimeError("Imported cockpit triangle count differs from validated geometry")
    bounds=asset.get_bounds()
    for axis in range(3):
        component=("x","y","z")[axis]
        center=getattr(bounds.origin,component);extent=getattr(bounds.box_extent,component)
        if abs((center-extent)-part["bounds_m"]["min"][axis]*100)>0.1 or abs((center+extent)-part["bounds_m"]["max"][axis]*100)>0.1:
            raise RuntimeError("Cockpit bounds do not match validated FBX")
runtime_path=ROOT/"Content/Star/Data/runtime_assets.json"
runtime=json.loads(runtime_path.read_text(encoding="utf-8"))
replacements={"SM_CockpitInterior":"SM_CockpitV3Interior","SM_InstrumentPanel":"SM_CockpitV3InstrumentPanel"}
for part in runtime["shipParts"]:
    if part["name"] in replacements:
        name=replacements[part["name"]]
        package=f"{cockpit_package_root}/Parts/{name}"
        asset=unreal.load_asset(package)
        if not isinstance(asset,unreal.StaticMesh):raise RuntimeError(f"Cockpit asset missing: {package}")
        part["asset"]=asset.get_path_name();part["locationCm"]=[0,0,0]
bootstrap.write_json(runtime_path,runtime)
import import_v03_side
side_result=import_v03_side.run(OUTPUT,cockpit_package_root+"/Materials")
bootstrap.write_json(OUTPUT/"graphics-import.json",dict(status="IMPORTED_AND_SAVED",textures=readbacks,materials=result,
    runtime_replacements=replacements,side_details=side_result,shader_and_visual_quality="REQUIRE_REAL_GAME_VERIFICATION"))
