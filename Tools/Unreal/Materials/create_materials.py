"""Deterministic STAR material authoring for Unreal Editor Python.

Called by the content bootstrap after texture import. Shader source is embedded in the
Custom expressions, so no runtime shader path mapping is required. This module is
also importable without Unreal for source/standalone-HLSL checks.
"""
from __future__ import annotations

import hashlib
import json
from copy import deepcopy
from pathlib import Path

try:
    import unreal
except ModuleNotFoundError:
    unreal = None

ROOT = Path(__file__).resolve().parents[3]
SHADER_DIR = ROOT / "Shaders" / "Star"
DESTINATION = "/Game/Star/Materials"
GENERATOR = "star-planet-material-library-v1"
OWNER_TAG = "StarMaterialGenerator"
HASH_TAG = "StarMaterialSourceHash"

ALIASES = {
    "earth_day": "earth_day_8k",
    "earth_night": "earth_night_8k",
    "moon_albedo": "moon_albedo_8k",
    "saturn_body": "saturn_body_reference",
    "saturn_rings": "saturn_rings_rgba",
    "stars": "stars_icrf_j2000_8k",
}

# Optional v0.2 photographic inputs.  The old bootstrap may still call this
# builder before the photo pass has imported them, so these are deliberately
# separate from the six v0.1 NASA inputs above.  ``normalize_asset_paths``
# accepts either the stable semantic key or the checked-in source stem.
PHOTO_ALIASES = {
    "earth_optical_lut": ("T_EarthOpticalDepth",),
    "cloud_density_atlas": ("T_CloudDensityAtlas",),
    "earth_pacific": ("T_Pacific_Aqua_20250906_8K",),
    "lunar_far_photo": ("apollo17_far_hills_photo_rgba_16k",),
    "lunar_orbital": ("apollo17_orbital_residual",),
    "bright_stars": ("hiptyc_2020_16k",),
    "lunar_mountain": ("apollo17_full_region_photo_rgba",),
    "earth_detail": ("earth_detail_v3",),
    "lunar_meso": ("apollo17_meso_linear",),
    "lunar_meso_normal": ("apollo17_meso_normal_dx",),
    "photo_sky": ("stars_photo_8k",),
    "apollo17_photo_regional": ("apollo17_photo_regional_4096",),
    "apollo_soil_detail": ("apollo_soil_detail_1024",),
    "apollo_soil_normal": ("apollo_soil_normal_1024_dx",),
    "plume_photo": ("T_VacuumPlumePhoto", "T_PlumePhoto"),
    "plume_axial": ("T_VacuumPlumeAxial", "T_PlumeAxial"),
}

PHOTO_MOON_DETAIL_SCALE = 26.191119735637812
PHOTO_MOON_REGIONAL_STRENGTH = 0.65
PHOTO_PLUME_MAIN_RADIANCE = 40.0
PHOTO_PLUME_RCS_RADIANCE = 8.0
PHOTO_PLUME_LAYER_ENERGY = 0.166667
PHOTO_AXIAL_LAYER_ENERGY = 0.125
PHOTO_PLUME_DEFINE = "STAR_PHOTO_PLUME=1"
AXIAL_SHADER_PATH = Path(__file__).resolve().parents[3] / "Tools" / "Lookdev" / "EngineFXV3" / "AxialDiskV3.custom.hlsl"
PHOTO_SHADER_INPUTS = {
    "M_Surface": {"textures": ["RegionalTex", "DetailTex", "DetailNormalTex"],
                   "scalars": ["RegionalStrength", "DetailScale", "DetailStrength", "NormalStrength"]},
    "M_Thruster": {"textures": ["PlumePhoto"],
                   "scalars": ["Thrust", "ThrusterRadiance", "PulsePhase", "LayerEnergy"],
                   "defines": [PHOTO_PLUME_DEFINE]},
    "M_ThrusterAxial": {"textures": ["PlumeAxial"],
                         "scalars": ["Thrust", "ThrusterRadiance", "LayerEnergy"]},
}

# Plain data is intentionally independent of the Unreal Python module.
COMMON_SCALARS = {
    "SunRadiance": 40500.0,
    "RadiusMeters": 6371000.0,
    "OccluderRadius": 0.0,
    "SunAngularRadius": 0.00465,
}
COMMON_VECTORS = {
    "SunDirectionLocal": (1.0, 0.0, 0.0),
    "CameraLocal": (3.0, 0.0, 0.0),
    "OccluderLocal": (0.0, 0.0, 0.0),
    # Body-local semi-axes in units of the physical mean radius.  Earth/Moon
    # remain spherical; the runtime supplies Saturn's oblate ratios.
    "BodyAxes": (1.0, 1.0, 1.0),
}
RING_SCALARS = {"RingInnerRadius": 1.11, "RingOuterRadius": 2.32, "RingOpacityScale": 1.0}

MATERIAL_SPECS = {
    "M_Planet": {
        "shader": "Planet.ush", "output": 4, "blend": "BLEND_MASKED",
        "scalars": {**COMMON_SCALARS, **RING_SCALARS, "BodyType": 0.0,
                    "AlbedoScale": 1.0, "NightIntensity": 3.0,
                    "CloudCoverage": 0.55, "CloudOpacity": 0.88,
                    "CloudHeightMeters": 5000.0, "CloudPhase": 0.0,
                    "UseCloudTexture": 0.0, "UseWaterMask": 0.0, "CloudLayerEnabled": 0.0,
                    "WaterRoughness": 0.075, "TerrainHoleCos": 1.0,
                    "TerrainHoleEnabled": 0.0,"EarthSurfaceEnabled":0.0,"TerrainWaterOnly":0.0},
        "vectors": {**COMMON_VECTORS, "TerrainHoleDirection": (1.0, 0.0, 0.0),"EarthSurfaceBounds":(0.0,0.0,3.0),"TerrainPatchBounds":(0.0,0.0,1.0),"PatchAxisXUE":(1.0,0.0,0.0),"PatchAxisYUE":(0.0,-1.0,0.0),"PatchAxisZUE":(0.0,0.0,1.0)},
        "textures": {"ObservedColor":"earth_day","TerrainWaterMask":"earth_day","EarthSurfaceMask":"earth_day","DayTex": "earth_day", "NightTex": "earth_night",
                     "CloudTex": "earth_clouds", "WaterMaskTex": "earth_water_mask",
                     "RingTex": "saturn_rings"},
    },
    "M_Clouds": {
        "shader": "Clouds.ush", "output": 4, "blend": "BLEND_TRANSLUCENT",
        "two_sided": True, "face_sign": True,
        "scalars": {**COMMON_SCALARS, "CloudHeightMeters": 5000.0,
                    "CloudPhase": 0.0, "CloudOpacity": 0.98, "CloudReliefMeters": 2200.0},
        "vectors": COMMON_VECTORS, "textures": {"CloudTex": "earth_clouds"},
    },
    "M_Atmosphere": {
        "shader": "Atmosphere.ush", "output": 4, "blend": "BLEND_TRANSLUCENT",
        "two_sided": True, "face_sign": True, "scene_depth": True,
        "scalars": {**COMMON_SCALARS, "AtmosphereHeightMeters": 100000.0,
                    "RenderRadiusCm": 637100840.0,
                    "RayleighScaleHeightMeters": 8000.0, "MieScaleHeightMeters": 1200.0,
                    "MieAnisotropy": 0.76, "DensityScale": 1.0,
                    "ViewSteps": 16.0, "SunSteps": 4.0},
        "vectors": {**COMMON_VECTORS, "RayleighBeta": (5.802e-6, 13.558e-6, 33.1e-6),
                    "MieBeta": (3.996e-6, 3.996e-6, 3.996e-6)},
    },
    "M_Rings": {
        "shader": "Rings.ush", "output": 4, "blend": "BLEND_TRANSLUCENT",
        "two_sided": True,
        "scalars": {**COMMON_SCALARS, **RING_SCALARS, "RingAlbedo": 0.72,
                    "RingBackscatter": 0.3},
        "vectors": COMMON_VECTORS, "textures": {"RingTex": "saturn_rings"},
    },
    "M_Stars": {
        "shader": "Stars.ush", "output": 3, "blend": "BLEND_ADDITIVE",
        "two_sided": True, "eye_exposure": True,
        "scalars": {"StarIntensity": 0.02, "EnhancedStars": 1.0, "PhotoSkyIntensity": 0.08},
        "textures": {"StarTex": "stars", "PhotoSkyTex": "photo_sky"},
    },
    "M_Surface": {
        "shader": "Surface.ush", "output": 4, "blend": "BLEND_OPAQUE", "lit": True,
        "detail_uv": True, "clast_uv": True, "eye_exposure": True,
        "scalars": {"AlbedoScale": 1.0, "RegionalStrength": PHOTO_MOON_REGIONAL_STRENGTH,
                    "DetailScale": PHOTO_MOON_DETAIL_SCALE, "DetailStrength": 0.3,
                    "SurfaceRoughness": 0.94, "NormalStrength": 0.2, "ClastStyleStrength": 1.0},
        # The photo branch is selected when the optional Moon inputs are
        # present.  build_materials supplies the old Lunar textures as an
        # explicit zero-strength fallback for the v0.1 bootstrap.
        "textures": {"DayTex": "moon_albedo", "RegionalTex": "apollo17_photo_regional",
                     "DetailTex": "apollo_soil_detail"},
    },
    "M_Thruster": {
        "shader": "Thruster.ush", "output": 4, "blend": "BLEND_ADDITIVE",
        "two_sided": True,
        "scalars": {"Thrust": 0.0, "ThrusterRadiance": PHOTO_PLUME_MAIN_RADIANCE, "PulsePhase": 0.0,
                    "LayerEnergy": PHOTO_PLUME_LAYER_ENERGY},
        "vectors": {"ThrusterColor": (0.25, 0.65, 1.0)},
        "textures": {"PlumePhoto": "plume_photo"},
        "defines": (PHOTO_PLUME_DEFINE,),
    },
    "M_ThrusterAxial": {
        "shader": "AxialDisk.custom.hlsl", "output": 4, "blend": "BLEND_ADDITIVE",
        "two_sided": True,
        "scalars": {"Thrust": 0.0, "ThrusterRadiance": PHOTO_PLUME_MAIN_RADIANCE,
                    "LayerEnergy": PHOTO_AXIAL_LAYER_ENERGY},
        "textures": {"PlumeAxial": "plume_axial"},
    },
}


PACIFIC_SCALARS={"PacificEnabled":0.0,"PacificWest":-152.0,"PacificSouth":0.0,
    "PacificEast":-144.0,"PacificNorth":16.0,"PacificFeatherDegrees":1.5}
for _name in ("M_Planet","M_Clouds"):
    MATERIAL_SPECS[_name]["scalars"].update(PACIFIC_SCALARS)
    MATERIAL_SPECS[_name]["textures"]["PacificTex"]="earth_pacific"
for _name in ("M_Clouds","M_Atmosphere"):
    MATERIAL_SPECS[_name]["scalars"].update(DreamStrength=0.0,DreamSunset=0.0)
    MATERIAL_SPECS[_name]["camera_vector"]=True
    MATERIAL_SPECS[_name]["vectors"]={**MATERIAL_SPECS[_name]["vectors"],
        "AxisXUE":(1.0,0.0,0.0),"AxisYUE":(0.0,-1.0,0.0),"AxisZUE":(0.0,0.0,1.0)}
    MATERIAL_SPECS[_name]["scalars"]["EarthLUTEnabled"]=0.0
    MATERIAL_SPECS[_name].setdefault("textures",{})["EarthOpticalLUT"]="earth_optical_lut"
MATERIAL_SPECS["M_Clouds"]["scalars"].update(CloudVolumeEnabled=1.0,CloudHeightMeters=8000.0)
MATERIAL_SPECS["M_Clouds"]["textures"]["CloudDensityAtlas"]="cloud_density_atlas"

MATERIAL_SPECS["M_Planet"]["textures"]["EarthDetailTex"] = "earth_detail"
MATERIAL_SPECS["M_Planet"]["textures"]["NightDetailTex"] = "earth_night_detail"
MATERIAL_SPECS["M_Planet"]["scalars"].update(NightDetailEnabled=0.0,NightDetailWest=29.0,
    NightDetailEast=34.0,NightDetailSouth=28.0,NightDetailNorth=33.0,NightDetailIsLuminance=0.0,NightMicroStrength=0.0)
MATERIAL_SPECS["M_Planet"]["scalars"].update(EarthDetailEnabled=0.0, EarthDetailWest=88.0, EarthDetailEast=104.0,
    EarthDetailSouth=3.0, EarthDetailNorth=19.0, EarthDetailFeatherDegrees=1.0, EarthDetailGain=1.0,EarthDetailLandOnly=0.0)
MATERIAL_SPECS["M_Clouds"]["textures"]["EarthDetailTex"] = "earth_detail"
MATERIAL_SPECS["M_Clouds"]["scalars"].update({k:v for k,v in MATERIAL_SPECS["M_Planet"]["scalars"].items() if k.startswith("EarthDetail")})
MATERIAL_SPECS["M_CloudsSurface"]=deepcopy(MATERIAL_SPECS["M_Clouds"])
MATERIAL_SPECS["M_CloudsSurface"]["shader"]="CloudsSurface.ush"
MATERIAL_SPECS["M_CloudsSurface"]["scalars"].update(CloudVolumeEnabled=0.0,CloudHeightMeters=5000.0)
MATERIAL_SPECS["M_Surface"]["textures"]["MesoTex"] = "lunar_meso"
MATERIAL_SPECS["M_Surface"]["textures"]["FarPhotoTex"] = "lunar_far_photo"
MATERIAL_SPECS["M_Surface"]["scalars"].update(FarPhotoStrength=0.0,FarPhotoGain=1.0)
MATERIAL_SPECS["M_Surface"]["textures"]["MountainTex"] = "lunar_mountain"
MATERIAL_SPECS["M_Surface"]["textures"]["OrbitalTex"] = "lunar_orbital"
MATERIAL_SPECS["M_Surface"]["scalars"]["OrbitalStrength"] = 1.0
MATERIAL_SPECS["M_Surface"]["scalars"].update(MountainStrength=1.0, MountainGain=1.0)
MATERIAL_SPECS["M_Surface"]["scalars"].update(MesoScale=0.5, MesoStrength=0.85, MesoBlendStrength=0.0)
MATERIAL_SPECS["M_Planet"]["textures"]["NightPhotoTex"] = "earth_night"
MATERIAL_SPECS["M_Planet"]["scalars"].update(NightPhotoEnabled=0.0,NightPhotoWest=30.7,NightPhotoEast=31.7,NightPhotoSouth=29.7,NightPhotoNorth=30.4,NightBackdropGain=1.0)
MATERIAL_SPECS["M_Stars"]["textures"]["BrightStarTex"] = "bright_stars"
MATERIAL_SPECS["M_Stars"]["scalars"].update(ResolvedStarsEnabled=1.0, ResolvedStarIntensity=0.6)
for _fx in ("M_Thruster", "M_ThrusterAxial"):
    MATERIAL_SPECS[_fx]["eye_exposure"] = True
    MATERIAL_SPECS[_fx]["scalars"].update(DisplayRadiance=6.0, FictionalTint=0.75, ViewAngleWeight=1.0, PulsePhase=0.0)
MATERIAL_SPECS["M_Thruster"]["defines"] = (PHOTO_PLUME_DEFINE, "STAR_ENGINE_FX_V3=1")


def shader_source(filename: str, defines=()) -> str:
    """A function body, with helper struct methods explicitly parameterized."""
    valid = {v["shader"] for v in MATERIAL_SPECS.values()} | {"SurfaceNormal.ush", "SurfacePhoto.ush", "Sun.ush"}
    if filename not in valid:
        raise ValueError(f"Unknown owned shader: {filename}")
    source_path = AXIAL_SHADER_PATH if filename == "AxialDisk.custom.hlsl" else SHADER_DIR / filename
    if not source_path.is_file():
        raise FileNotFoundError(f"Missing STAR shader source: {source_path}")
    # MaterialExpressionCustom's additional-defines convention commonly uses
    # ``NAME=VALUE`` while HLSL's preprocessor requires a whitespace between
    # the macro name and replacement token.
    prefix = "".join(f"#define {item.replace('=', ' ', 1)}\n" for item in defines)
    common = "" if filename == "AxialDisk.custom.hlsl" else (SHADER_DIR / "Common.ush").read_text(encoding="utf-8") + "\n"
    body=source_path.read_text(encoding="utf-8")
    if filename in ("Clouds.ush","CloudsSurface.ush"):
        body=body.replace('// STAR_CLOUD_SURFACE_FALLBACK',(SHADER_DIR/'CloudsSurface.ush').read_text(encoding='utf-8'))
        body=body.replace('// STAR_CLOUD_PHOTO_VISIBILITY',(SHADER_DIR/'CloudPhotoVisibility.ush').read_text(encoding='utf-8'))
    return prefix + common + body


def normalize_asset_paths(asset_paths: dict) -> dict:
    if not isinstance(asset_paths, dict):
        raise TypeError("asset_paths must map texture keys to imported Unreal paths")
    result = {}
    for key, alias in ALIASES.items():
        value = asset_paths.get(key) or asset_paths.get(alias)
        if not isinstance(value, str) or not value.startswith("/Game/"):
            raise ValueError(f"Missing imported texture asset path: {key} (alias {alias})")
        result[key] = value
    for key in ("earth_clouds", "earth_water_mask", "lunar_detail", "lunar_detail_normal"):
        value = asset_paths.get(key)
        if value:
            if not isinstance(value, str) or not value.startswith("/Game/"):
                raise ValueError(f"Invalid imported texture path: {key}")
            result[key] = value
    for key, aliases in PHOTO_ALIASES.items():
        value = asset_paths.get(key)
        if value is None:
            for alias in aliases:
                value = asset_paths.get(alias)
                if value is not None:
                    break
        if value is not None:
            if not isinstance(value, str) or not value.startswith("/Game/"):
                raise ValueError(f"Invalid imported photographic texture path: {key}")
            result[key] = value
    return result


def _connect(source, target, pin: str, output: str = ""):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(source, output, target, pin):
        raise RuntimeError(f"Cannot connect expression to {pin}")


def _property(source, property_name: str):
    prop = getattr(unreal.MaterialProperty, property_name)
    if not unreal.MaterialEditingLibrary.connect_material_property(source, "", prop):
        raise RuntimeError(f"Cannot connect material property {property_name}")


def _node(material, cls):
    node = unreal.MaterialEditingLibrary.create_material_expression(material, cls, -600, 0)
    if node is None:
        raise RuntimeError(f"Could not create material expression {cls}")
    return node


def _mask(material, source, channels: str):
    mask = _node(material, unreal.MaterialExpressionComponentMask)
    for channel in "rgba":
        mask.set_editor_property(channel, channel in channels)
    _connect(source, mask, "")
    return mask


def _custom(material, filename, inputs, output_components, defines=()):
    expression = _node(material, unreal.MaterialExpressionCustom)
    expression.set_editor_property("description", "STAR " + filename.removesuffix(".ush"))
    expression.set_editor_property("code", shader_source(filename, defines))
    expression.set_editor_property("output_type", getattr(
        unreal.CustomMaterialOutputType, f"CMOT_FLOAT{output_components}"))
    custom_inputs = []
    for name in inputs:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", name)
        custom_inputs.append(item)
    expression.set_editor_property("inputs", custom_inputs)
    for name, node in inputs.items():
        _connect(node, expression, name)
    return expression


def _sampler_type(texture):
    """Match imported texture settings; never silently mutate scientific inputs."""
    compression = texture.get_editor_property("compression_settings")
    srgb = bool(texture.get_editor_property("srgb"))
    if texture.get_editor_property("virtual_texture_streaming"):
        raise ValueError(f"STAR Custom nodes require non-virtual textures: {texture.get_path_name()}")
    if compression == unreal.TextureCompressionSettings.TC_MASKS:
        return unreal.MaterialSamplerType.SAMPLERTYPE_MASKS
    if compression == unreal.TextureCompressionSettings.TC_NORMALMAP:
        return unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
    if compression == unreal.TextureCompressionSettings.TC_GRAYSCALE:
        return (unreal.MaterialSamplerType.SAMPLERTYPE_GRAYSCALE if srgb
                else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    return (unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if srgb
            else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)


def scene_depth_nodes(material):
    """UE linear view depths plus a radial distance; all rendered units are cm."""
    distance=_node(material,unreal.MaterialExpressionDistance)
    _connect(_node(material,unreal.MaterialExpressionWorldPosition),distance,"A")
    _connect(_node(material,unreal.MaterialExpressionCameraPositionWS),distance,"B")
    return {"SceneDepthCm":_node(material,unreal.MaterialExpressionSceneDepth),
            "PixelDepthCm":_node(material,unreal.MaterialExpressionPixelDepth),
            "PixelDistanceCm":distance}


def _parameter_nodes(material, spec, textures, samplers, supplied):
    nodes = {}
    uv = _node(material, unreal.MaterialExpressionTextureCoordinate)
    uv.set_editor_property("coordinate_index", 0)
    nodes["UV"] = uv
    if spec["shader"]=="Planet.ush":
        nodes["PatchUV"]=_node(material,unreal.MaterialExpressionTextureCoordinate)
        nodes["PatchUV"].set_editor_property("coordinate_index",1)
        nodes["SurfaceNormalWS"]=_node(material,unreal.MaterialExpressionVertexNormalWS)
        nodes["PatchNormal"]=_node(material,unreal.MaterialExpressionVertexColor)
    if spec.get("detail_uv"):
        uv1 = _node(material, unreal.MaterialExpressionTextureCoordinate)
        uv1.set_editor_property("coordinate_index", 1)
        nodes["DetailUV"] = uv1
    if spec.get("clast_uv"):
        for name, index in (("ClastUV", 2), ("ClastVariation", 3)):
            coordinate = _node(material, unreal.MaterialExpressionTextureCoordinate)
            coordinate.set_editor_property("coordinate_index", index)
            nodes[name] = coordinate
    if spec.get("camera_vector"):
        nodes["ViewToCamera"] = _node(material, unreal.MaterialExpressionCameraVectorWS)
    if spec.get("scene_depth"):
        nodes.update(scene_depth_nodes(material))
    if spec.get("face_sign"):
        nodes["FaceSign"] = _node(material, unreal.MaterialExpressionTwoSidedSign)
    if spec.get("eye_exposure"):
        nodes["EyeExposure"] = _node(material, unreal.MaterialExpressionEyeAdaptation)
    for name, value in spec.get("scalars", {}).items():
        node = _node(material, unreal.MaterialExpressionScalarParameter)
        node.set_editor_property("parameter_name", name)
        if name == "UseCloudTexture":
            value = float("earth_clouds" in supplied)
        elif name == "UseWaterMask":
            value = float("earth_water_mask" in supplied)
        node.set_editor_property("default_value", float(value))
        node.set_editor_property("group", "STAR")
        nodes[name] = node
    for name, value in spec.get("vectors", {}).items():
        node = _node(material, unreal.MaterialExpressionVectorParameter)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", unreal.LinearColor(*value, 1.0))
        node.set_editor_property("group", "STAR")
        # Vector parameter default output is RGB; alpha is deliberately not connected.
        nodes[name] = node
    for name, key in spec.get("textures", {}).items():
        node = _node(material, unreal.MaterialExpressionTextureObjectParameter)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("texture", textures[key])
        node.set_editor_property("sampler_type", samplers[key])
        node.set_editor_property("group", "STAR textures")
        nodes[name] = node
    return nodes


def _active_specs(supplied):
    """Return graph specs with explicit v0.1 fallbacks where photo assets are absent.

    The public ``MATERIAL_SPECS`` remains the v0.2 contract so offline callers
    can inspect the expected pins.  The old full bootstrap may run before the
    photo pass; in that case only the optional branch is disabled and the
    existing Lunar/legacy thruster inputs remain usable.
    """
    active = {key: deepcopy(value) for key, value in MATERIAL_SPECS.items()}
    if "earth_pacific" not in supplied:
        for name in ("M_Planet","M_Clouds","M_CloudsSurface"):
            active[name]["textures"]["PacificTex"]="earth_day"
    if "earth_optical_lut" not in supplied:
        for name in ("M_Atmosphere","M_Clouds","M_CloudsSurface"):
            active[name]["textures"]["EarthOpticalLUT"]="stars"
    if "cloud_density_atlas" not in supplied:
        active["M_Clouds"]["textures"]["CloudDensityAtlas"]="stars"
        active["M_CloudsSurface"]["textures"]["CloudDensityAtlas"]="stars"
        active["M_Clouds"]["scalars"]["CloudVolumeEnabled"]=0.0
    if "lunar_far_photo" not in supplied:
        active["M_Surface"]["textures"]["FarPhotoTex"] = "moon_albedo"
    if "lunar_orbital" not in supplied:
        active["M_Surface"]["textures"]["OrbitalTex"] = "moon_albedo"
        active["M_Surface"]["scalars"]["OrbitalStrength"] = 0.0
    if "bright_stars" not in supplied:
        active["M_Stars"]["textures"]["BrightStarTex"] = "stars"
        active["M_Stars"]["scalars"]["ResolvedStarsEnabled"] = 0.0
    if "lunar_mountain" not in supplied:
        active["M_Surface"]["textures"]["MountainTex"] = "moon_albedo"
        active["M_Surface"]["scalars"]["MountainStrength"] = 0.0
    if "earth_detail" not in supplied:
        active["M_Planet"]["textures"]["EarthDetailTex"] = "earth_day"
        active["M_Clouds"]["textures"]["EarthDetailTex"] = "earth_day"
        active["M_CloudsSurface"]["textures"]["EarthDetailTex"] = "earth_day"
    if "lunar_meso" not in supplied:
        active["M_Surface"]["textures"]["MesoTex"] = "lunar_detail"
        active["M_Surface"]["scalars"]["MesoStrength"] = 0.0
    if "photo_sky" not in supplied:
        active["M_Stars"]["scalars"]["EnhancedStars"] = 0.0
        active["M_Stars"]["textures"]["PhotoSkyTex"] = "stars"
    has_moon_photo = any(key in supplied for key in (
        "apollo17_photo_regional", "apollo_soil_detail", "apollo_soil_normal"))
    if has_moon_photo and not all(key in supplied for key in (
            "apollo17_photo_regional", "apollo_soil_detail", "apollo_soil_normal")):
        raise ValueError("Moon photo pass requires RegionalTex, DetailTex and DetailNormalTex together")
    if not has_moon_photo:
        # Surface.ush still receives a RegionalTex input, but RegionalStrength
        # is zero and the image itself is the real imported lunar albedo rather
        # than a generated placeholder.
        active["M_Surface"]["scalars"]["RegionalStrength"] = 0.0
        active["M_Surface"]["textures"]["RegionalTex"] = "moon_albedo"
        active["M_Surface"]["textures"]["DetailTex"] = "lunar_detail"
    has_plume_photo = any(key in supplied for key in ("plume_photo", "plume_axial"))
    if has_plume_photo and not all(key in supplied for key in ("plume_photo", "plume_axial")):
        raise ValueError("Vacuum plume pass requires PlumePhoto and PlumeAxial together")
    if not has_plume_photo:
        # Preserve the old Thruster.ush branch until both vacuum textures have
        # been imported.  Do not create the axial material without its source.
        active["M_Thruster"]["textures"] = {}
        active["M_Thruster"]["scalars"].pop("LayerEnergy", None)
        active["M_Thruster"].pop("defines", None)
        active.pop("M_ThrusterAxial", None)
    return active


def _preflight_existing(path, expected_class):
    asset = unreal.load_asset(path)
    if asset is not None:
        owner = unreal.EditorAssetLibrary.get_metadata_tag(asset, OWNER_TAG)
        if not isinstance(asset, expected_class) or owner != GENERATOR:
            raise RuntimeError(f"Refusing to replace unrelated/non-generated asset at {path}")
    return asset


def _stamp_save(asset, digest):
    unreal.EditorAssetLibrary.set_metadata_tag(asset, OWNER_TAG, GENERATOR)
    unreal.EditorAssetLibrary.set_metadata_tag(asset, HASH_TAG, digest)
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {asset.get_path_name()}")


def build_materials(asset_paths: dict) -> dict:
    """Create/rebuild only STAR-owned graphs; does not claim shader compile success.

    Requires EditorScriptingUtilities and PythonScriptPlugin. Texture imports and
    project/Substrate configuration belong to the root bootstrap. All supplied and
    existing destination assets are preflighted before the first graph mutation.
    """
    if unreal is None:
        raise RuntimeError("build_materials must run inside Unreal Editor Python")
    supplied = normalize_asset_paths(asset_paths)
    night_detail='/Game/Star/Art/EarthRealism/T_Nile_NightLights2016_2K'
    if not isinstance(unreal.load_asset(night_detail),unreal.Texture2D):
        night_detail='/Game/Star/Art/EarthFlight/T_Cairo_VIIRS_2012'
    if isinstance(unreal.load_asset(night_detail),unreal.Texture2D):
        supplied['earth_night_detail']=night_detail
    # Preserve already-authored V2 resources when an older bootstrap caller
    # supplies only its original texture dictionary.
    for key,stem in {"earth_optical_lut":"T_EarthOpticalDepth",
        "cloud_density_atlas":"T_CloudDensityAtlas","earth_pacific":"T_Pacific_Aqua_20250906_8K"}.items():
        path="/Game/Star/Art/EarthV2/"+stem
        if key not in supplied and isinstance(unreal.load_asset(path),unreal.Texture2D):
            supplied[key]=path

    if not all(key in supplied for key in ("lunar_detail", "lunar_detail_normal")):
        raise ValueError("Import the lunar lookdev textures before rebuilding M_Surface")
    active_specs = _active_specs(supplied)
    textures = {}
    warnings = []
    for key, path in supplied.items():
        asset = unreal.load_asset(path)
        if not isinstance(asset, unreal.Texture2D):
            raise ValueError(f"Missing or non-Texture2D asset for {key}: {path}")
        textures[key] = asset
    # Real, already-required linear HDR resource fills inactive texture pins. The
    if 'earth_night_detail' not in textures:
        textures['earth_night_detail']=textures['earth_night']
    # shader branches do not use it as a cloud/water observation. This avoids extra
    # generated placeholder assets and preserves linear sampler compatibility.
    for key in ("earth_clouds", "earth_water_mask"):
        if key not in textures:
            textures[key] = textures["stars"]
            warnings.append(f"{key} absent: using documented synthetic/heuristic model")
    if textures["stars"].get_editor_property("srgb"):
        raise ValueError("HDR stars must be imported with sRGB disabled")
    for key in ("earth_clouds", "earth_water_mask"):
        if key in supplied and textures[key].get_editor_property("srgb"):
            raise ValueError(f"Coverage/mask must be imported linear: {key}")
    # The v0.1 path has no photo soil inputs.  Reuse already imported Lunar
    # assets for inactive Custom pins and set RegionalStrength=0 above; this
    # keeps the graph type-stable without inventing a texture asset.
    if "apollo17_photo_regional" not in supplied:
        textures["apollo17_photo_regional"] = textures["moon_albedo"]
    if "apollo_soil_detail" not in supplied:
        textures["apollo_soil_detail"] = textures["lunar_detail"]
    if "plume_photo" not in supplied:
        # This value is only used by the optional photo spec, which is removed
        # by _active_specs.  Keeping a complete lookup makes the helper safe
        # for callers that inspect all specs before selecting active graphs.
        textures["plume_photo"] = textures["stars"]
    if "plume_axial" not in supplied:
        textures["plume_axial"] = textures["stars"]
    samplers = {key: _sampler_type(texture) for key, texture in textures.items()}
    existing = {name: _preflight_existing(f"{DESTINATION}/{name}", unreal.Material)
                for name in active_specs}
    instance_specs = {
        "MI_Earth": ("earth_day", {"BodyType": 0.0, "RadiusMeters": 6371000.0, "EarthDetailEnabled": float("earth_detail" in supplied), "NightDetailEnabled": float("earth_night_detail" in supplied)}),
        "MI_Moon": ("moon_albedo", {"BodyType": 1.0, "RadiusMeters": 1737400.0}),
        "MI_Saturn": ("saturn_body", {"BodyType": 2.0, "RadiusMeters": 58232000.0,
                       "SunRadiance": 40500.0/(9.58*9.58), "SunAngularRadius": 0.00465/9.58}),
    }
    if supplied.get('earth_night_detail','').endswith('T_Nile_NightLights2016_2K'):
        instance_specs['MI_Earth'][1].update(NightDetailIsLuminance=1.0,NightMicroStrength=0.0,NightDetailWest=27.0,
            NightDetailSouth=25.466666666666665,NightDetailEast=35.53333333333333,NightDetailNorth=34.0)
    existing_instances = {name: _preflight_existing(f"{DESTINATION}/{name}", unreal.MaterialInstanceConstant)
                          for name in instance_specs}
    source_paths = list(SHADER_DIR.glob("*.ush"))
    if "M_ThrusterAxial" in active_specs:
        source_paths.append(AXIAL_SHADER_PATH)
    source_bytes = b"".join(path.read_bytes() for path in sorted(source_paths))
    digest = hashlib.sha256(source_bytes + Path(__file__).read_bytes() + json.dumps(
        {"paths": supplied, "samplers": {k: str(v) for k, v in samplers.items()},
         "active_specs": active_specs},
        sort_keys=True).encode("utf-8")).hexdigest()
    unreal.EditorAssetLibrary.make_directory(DESTINATION)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    materials = {}
    rebuilt = []
    for name, spec in active_specs.items():
        material = existing[name]
        if material is not None and unreal.EditorAssetLibrary.get_metadata_tag(material, HASH_TAG) == digest:
            materials[name] = material
            continue
        if material is None:
            material = asset_tools.create_asset(name, DESTINATION, unreal.Material, unreal.MaterialFactoryNew())
            if material is None:
                raise RuntimeError(f"Could not create {name}")
            # Tag immediately so an interrupted owned rebuild can be resumed.
            unreal.EditorAssetLibrary.set_metadata_tag(material, OWNER_TAG, GENERATOR)
        # UE 5.8.2's bulk helper iterates the collection while removing from it.
        # Iterate a snapshot instead so repeated builds leave no stale outputs.
        for expression in list(unreal.MaterialEditingLibrary.get_material_expressions(material)):
            unreal.MaterialEditingLibrary.delete_material_expression(material, expression)
        if unreal.MaterialEditingLibrary.get_num_material_expressions(material) != 0:
            raise RuntimeError(f"Material graph did not clear completely: {name}")
        material.set_editor_property("blend_mode", getattr(unreal.BlendMode, spec["blend"]))
        material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT
                                     if spec.get("lit") else unreal.MaterialShadingModel.MSM_UNLIT)
        material.set_editor_property("two_sided", spec.get("two_sided", False))
        material.set_editor_property("disable_depth_test",bool(spec.get("scene_depth")))
        material.set_editor_property("tangent_space_normal", True)
        # Do not mix the physical body atmosphere with generic world-space fog.
        if spec["blend"] in ("BLEND_TRANSLUCENT", "BLEND_ADDITIVE"):
            material.set_editor_property("use_translucency_vertex_fog", False)
        if spec["blend"] == "BLEND_MASKED":
            material.set_editor_property("opacity_mask_clip_value", 0.5)
        nodes = _parameter_nodes(material, spec, textures, samplers, supplied)
        expression = _custom(material, spec["shader"], nodes, spec["output"], spec.get("defines", ()))
        rgb = _mask(material, expression, "rgb") if spec["output"] == 4 else expression
        if name == "M_Surface":
            photo_inputs={k:nodes[k] for k in ("ClastUV","ClastVariation","FarPhotoTex","FarPhotoStrength","FarPhotoGain","EyeExposure")}
            photo=_custom(material,"SurfacePhoto.ush",photo_inputs,4)
            inverse=_node(material,unreal.MaterialExpressionOneMinus)
            _connect(_mask(material,photo,"a"),inverse,"")
            attenuated=_node(material,unreal.MaterialExpressionMultiply)
            _connect(rgb,attenuated,"A");_connect(inverse,attenuated,"B")
            rgb=attenuated
            _property(_mask(material,photo,"rgb"),"MP_EMISSIVE_COLOR")
        _property(rgb, "MP_BASE_COLOR" if spec.get("lit") else "MP_EMISSIVE_COLOR")
        if spec.get("lit"):
            _property(_mask(material, expression, "a"), "MP_ROUGHNESS")
            normal_inputs = {k: nodes[k] for k in ("DetailUV", "DetailScale", "NormalStrength", "ClastUV", "ClastStyleStrength")}
            normal_sample = _node(material, unreal.MaterialExpressionTextureSampleParameter2D)
            normal_sample.set_editor_property("parameter_name", "DetailNormalTex")
            normal_key = "apollo_soil_normal" if "apollo_soil_normal" in supplied else "lunar_detail_normal"
            normal_sample.set_editor_property("texture", textures[normal_key])
            normal_sample.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
            normal_uv = _node(material, unreal.MaterialExpressionMultiply)
            _connect(nodes["DetailUV"], normal_uv, "A")
            _connect(nodes["DetailScale"], normal_uv, "B")
            _connect(normal_uv, normal_sample, "UVs")
            normal_inputs["DetailNormal"] = normal_sample
            meso_normal = _node(material, unreal.MaterialExpressionTextureSampleParameter2D)
            meso_normal.set_editor_property("parameter_name", "MesoNormalTex")
            meso_normal.set_editor_property("texture", textures.get("lunar_meso_normal", textures[normal_key]))
            meso_normal.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
            meso_uv = _node(material, unreal.MaterialExpressionMultiply)
            _connect(nodes["DetailUV"], meso_uv, "A")
            _connect(nodes["MesoScale"], meso_uv, "B")
            _connect(meso_uv, meso_normal, "UVs")
            normal_inputs.update(MesoNormal=meso_normal, MesoScale=nodes["MesoScale"], MesoStrength=nodes["MesoStrength"])
            # A second rotated/noncommensurate sample breaks the repeated blank
            # patches of the analogous photograph without inventing measured relief.
            neg_y=_node(material, unreal.MaterialExpressionMultiply)
            neg_y.set_editor_property("const_b",-1.0)
            _connect(_mask(material,meso_uv,"g"),neg_y,"A")
            rotated=_node(material,unreal.MaterialExpressionAppendVector)
            _connect(neg_y,rotated,"A");_connect(_mask(material,meso_uv,"r"),rotated,"B")
            scaled=_node(material,unreal.MaterialExpressionMultiply)
            scaled.set_editor_property("const_b",1.137);_connect(rotated,scaled,"A")
            offset=_node(material,unreal.MaterialExpressionConstant2Vector)
            offset.set_editor_property("r",0.371);offset.set_editor_property("g",0.618)
            alt_uv=_node(material,unreal.MaterialExpressionAdd)
            _connect(scaled,alt_uv,"A");_connect(offset,alt_uv,"B")
            alternate=_node(material,unreal.MaterialExpressionTextureSampleParameter2D)
            alternate.set_editor_property("parameter_name","MesoNormalTex")
            alternate.set_editor_property("texture",textures.get("lunar_meso_normal",textures[normal_key]))
            alternate.set_editor_property("sampler_type",unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
            _connect(alt_uv,alternate,"UVs")
            normal_inputs.update(MesoNormalAlternate=alternate,MesoBlendStrength=nodes["MesoBlendStrength"])
            _property(_custom(material, "SurfaceNormal.ush", normal_inputs, 3), "MP_NORMAL")
            specular = _node(material, unreal.MaterialExpressionConstant)
            specular.set_editor_property("r", 0.25)
            if name == "M_Surface":
                covered_specular=_node(material,unreal.MaterialExpressionMultiply)
                _connect(specular,covered_specular,"A");_connect(inverse,covered_specular,"B")
                specular=covered_specular
            _property(specular, "MP_SPECULAR")
        elif spec["output"] == 4:
            _property(_mask(material, expression, "a"),
                      "MP_OPACITY_MASK" if spec["blend"] == "BLEND_MASKED" else "MP_OPACITY")
        unreal.MaterialEditingLibrary.layout_material_expressions(material)
        unreal.MaterialEditingLibrary.recompile_material(material)
        _stamp_save(material, digest)
        rebuilt.append(name)
        materials[name] = material
    instances = {}
    for name, (texture_key, scalars) in instance_specs.items():
        instance = existing_instances[name]
        if instance is None:
            instance = asset_tools.create_asset(name, DESTINATION, unreal.MaterialInstanceConstant,
                                                unreal.MaterialInstanceConstantFactoryNew())
            if instance is None:
                raise RuntimeError(f"Could not create {name}")
            unreal.EditorAssetLibrary.set_metadata_tag(instance, OWNER_TAG, GENERATOR)
        if unreal.EditorAssetLibrary.get_metadata_tag(instance, HASH_TAG) != digest:
            unreal.MaterialEditingLibrary.set_material_instance_parent(instance, materials["M_Planet"])
            # UE 5.8.2 setters apply the value but currently return false even
            # on success. Verify the resulting parameter through its getter.
            unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
                instance, "DayTex", textures[texture_key])
            actual_texture = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(instance, "DayTex")
            if actual_texture is None or actual_texture.get_path_name() != textures[texture_key].get_path_name():
                raise RuntimeError(f"DayTex readback mismatch for {name}")
            for parameter, value in scalars.items():
                unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(instance, parameter, value)
                actual = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(instance, parameter)
                if abs(actual - value) > max(1e-7, abs(value) * 1e-6):
                    raise RuntimeError(f"{parameter} readback mismatch for {name}: {actual} != {value}")
            _stamp_save(instance, digest)
        instances[name] = f"{DESTINATION}/{name}"
    return {"materials": {name: f"{DESTINATION}/{name}" for name in materials},
            "instances": instances, "source_hash": digest,
            "status": "BUILT_COMPILE_REQUESTED" if rebuilt else "REUSED_SOURCE_HASH_MATCH",
            "rebuilt": rebuilt, "warnings": warnings,
            "photo_inputs": PHOTO_SHADER_INPUTS if all(key in supplied for key in (
                "apollo17_photo_regional", "apollo_soil_detail", "apollo_soil_normal",
                "plume_photo", "plume_axial")) else {},
            "verification": "Inspect editor shader compiler errors, then render/cook on target UE 5.8 DX12."}
