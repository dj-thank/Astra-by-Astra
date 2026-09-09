"""Scoped Earth authoring in a fresh Unreal Python commandlet."""
from pathlib import Path
import hashlib,json,sys
import unreal
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tools/Unreal'))
import bootstrap
from Materials import create_materials as lib
assert Path(unreal.Paths.project_dir()).resolve()==ROOT
if unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages():
    raise RuntimeError('Fresh commandlet required')

# Keep the original entry point usable after the V2 material contract expands.
if all((ROOT/'Content/Star/Art/EarthV2'/name).is_file() for name in (
    'T_EarthOpticalDepth.exr','T_CloudDensityAtlas.png','T_Pacific_Aqua_20250906_8K.png')):
    import runpy
    runpy.run_path(str(ROOT/'Tools/Unreal/apply_earth_v2.py'),run_name='__main__')
else:
    textures={
        'earth_day':unreal.load_asset('/Game/Star/Art/PhotoEarth/T_EarthDay16K'),
        'earth_night':unreal.load_asset('/Game/Star/Textures/earth_night_8k'),
        'earth_clouds':unreal.load_asset('/Game/Star/Textures/earth_clouds_8k'),
        'earth_detail':unreal.load_asset('/Game/Star/Art/EarthDetailV3/T_EarthDetail_Andaman_Aqua_20250906_8K'),
    }
    for key,texture in textures.items():
        assert isinstance(texture,unreal.Texture2D),key
    readback=[]
    for key,size in [('earth_day',16384),('earth_clouds',8192),('earth_night',8192)]:
        texture=textures[key]
        texture.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_BC7)
        texture.set_editor_property('srgb',key!='earth_clouds')
        texture.set_editor_property('never_stream',True)
        texture.set_editor_property('lod_bias',0)
        texture.set_editor_property('max_texture_size',size)
        bootstrap.save(texture)
        assert texture.get_editor_property('never_stream')
        readback.append({'asset':texture.get_path_name(),'neverStream':True,'maximum':size,'compression':'BC7','sRGB':key!='earth_clouds'})

    planet=lib._preflight_existing('/Game/Star/Materials/M_Planet',unreal.Material)
    assert planet is not None
    expressions=list(unreal.MaterialEditingLibrary.get_material_expressions(planet))
    customs=[n for n in expressions if isinstance(n,unreal.MaterialExpressionCustom) and n.get_editor_property('description')=='STAR Planet']
    assert len(customs)==1
    custom=customs[0]
    custom.set_editor_property('code',lib.shader_source('Planet.ush'))
    if not any(str(n.get_editor_property('input_name'))=='CloudLayerEnabled' for n in custom.get_editor_property('inputs')):
        pin=unreal.CustomInput();pin.set_editor_property('input_name','CloudLayerEnabled')
        custom.set_editor_property('inputs',list(custom.get_editor_property('inputs'))+[pin])
        parameter=bootstrap.node(planet,unreal.MaterialExpressionScalarParameter,parameter_name='CloudLayerEnabled',default_value=0.0)
        bootstrap.connect(parameter,custom,'CloudLayerEnabled')
    for n in expressions:
        if isinstance(n,unreal.MaterialExpressionTextureObjectParameter) and str(n.get_editor_property('parameter_name'))=='CloudTex':
            n.set_editor_property('sampler_type',lib._sampler_type(textures['earth_clouds']))
    unreal.MaterialEditingLibrary.recompile_material(planet)
    bootstrap.save(planet)

    cloud=lib._preflight_existing('/Game/Star/Materials/M_Clouds',unreal.Material)
    if cloud is None:
        cloud=unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_Clouds','/Game/Star/Materials',unreal.Material,unreal.MaterialFactoryNew())
        unreal.EditorAssetLibrary.set_metadata_tag(cloud,lib.OWNER_TAG,lib.GENERATOR)
    for n in list(unreal.MaterialEditingLibrary.get_material_expressions(cloud)):
        unreal.MaterialEditingLibrary.delete_material_expression(cloud,n)
    cloud.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT)
    cloud.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    cloud.set_editor_property('two_sided',True)
    cloud.set_editor_property('use_translucency_vertex_fog',False)
    spec=lib.MATERIAL_SPECS['M_Clouds']
    samplers={k:lib._sampler_type(v) for k,v in textures.items()}
    nodes=lib._parameter_nodes(cloud,spec,textures,samplers,textures)
    expression=lib._custom(cloud,'Clouds.ush',nodes,4)
    lib._property(lib._mask(cloud,expression,'rgb'),'MP_EMISSIVE_COLOR')
    lib._property(lib._mask(cloud,expression,'a'),'MP_OPACITY')
    unreal.MaterialEditingLibrary.recompile_material(cloud)
    lib._stamp_save(cloud,hashlib.sha256(lib.shader_source('Clouds.ush').encode('utf-8')).hexdigest())
    bootstrap.write_json(ROOT/'work/earth-polish/author-result.json',{'status':'SAVED','textures':readback,'cloudMaterial':cloud.get_path_name(),'gpu':'pending'})
    unreal.log('STAR Earth high-resolution textures and cloud shell saved')
