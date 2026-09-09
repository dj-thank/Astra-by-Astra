"""Import Earth V2 numeric/photo assets and update only Earth-related graphs."""
from pathlib import Path
import hashlib,json,sys
import unreal
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tools/Unreal'))
import bootstrap
from Materials import create_materials as lib
assert Path(unreal.Paths.project_dir()).resolve()==ROOT
if unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages():raise RuntimeError('Fresh commandlet required')
OWNER='StarEarthV2Author'
textures={
 'earth_clouds':unreal.load_asset('/Game/Star/Textures/earth_clouds_8k'),
 'earth_detail':unreal.load_asset('/Game/Star/Art/EarthDetailV3/T_EarthDetail_Andaman_Aqua_20250906_8K'),
 'earth_night_detail':unreal.load_asset('/Game/Star/Art/EarthRealism/T_Nile_NightLights2016_2K') or unreal.load_asset('/Game/Star/Art/EarthFlight/T_Cairo_VIIRS_2012') or unreal.load_asset('/Game/Star/Textures/earth_night_8k'),
}
textures['earth_detail'].set_editor_property('never_stream',True)
bootstrap.save(textures['earth_detail'])
readbacks=[]
for key,filename,compression,srgb,mips in [
 ('earth_optical_lut','T_EarthOpticalDepth.exr','TC_HDR',False,False),
 ('cloud_density_atlas','T_CloudDensityAtlas.png','TC_GRAYSCALE',False,False),
 ('earth_pacific','T_Pacific_Aqua_20250906_8K.png','TC_BC7',True,True)]:
 path=ROOT/'Content/Star/Art/EarthV2'/filename
 package='/Game/Star/Art/EarthV2/'+path.stem
 sha=hashlib.sha256(path.read_bytes()).hexdigest()
 texture=unreal.load_asset(package)
 if texture and unreal.EditorAssetLibrary.get_metadata_tag(texture,OWNER)!='v2':raise RuntimeError('Unowned texture '+package)
 if texture is None or unreal.EditorAssetLibrary.get_metadata_tag(texture,'StarEarthV2SourceSHA')!=sha:
  task=unreal.AssetImportTask();task.set_editor_property('filename',str(path))
  task.set_editor_property('destination_path','/Game/Star/Art/EarthV2')
  task.set_editor_property('destination_name',path.stem)
  task.set_editor_property('automated',True);task.set_editor_property('replace_existing',texture is not None)
  unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
  texture=unreal.load_asset(package)
 if not isinstance(texture,unreal.Texture2D):raise RuntimeError('Texture import failed '+package)
 for prop,value in {'compression_settings':getattr(unreal.TextureCompressionSettings,compression),'srgb':srgb,
  'never_stream':True,'virtual_texture_streaming':False,'lod_bias':0,'max_texture_size':0,
  'address_x':unreal.TextureAddress.TA_CLAMP,'address_y':unreal.TextureAddress.TA_CLAMP}.items():texture.set_editor_property(prop,value)
 if not mips:
  texture.set_editor_property('mip_gen_settings',unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
  texture.set_editor_property('filter',unreal.TextureFilter.TF_BILINEAR)
 unreal.EditorAssetLibrary.set_metadata_tag(texture,OWNER,'v2')
 unreal.EditorAssetLibrary.set_metadata_tag(texture,'StarEarthV2SourceSHA',sha)
 bootstrap.save(texture);textures[key]=texture
 readbacks.append({'asset':texture.get_path_name(),'sourceSHA':sha,'neverStream':bool(texture.get_editor_property('never_stream')),'sRGB':bool(texture.get_editor_property('srgb')),'compression':str(texture.get_editor_property('compression_settings'))})

def add_missing_inputs(material,custom,spec):
 names={str(pin.get_editor_property('input_name')) for pin in custom.get_editor_property('inputs')}
 if spec.get('scene_depth') and not {'SceneDepthCm','PixelDepthCm','PixelDistanceCm'}.issubset(names):
  for key,node in lib.scene_depth_nodes(material).items():
   if key in names:continue
   pin=unreal.CustomInput();pin.set_editor_property('input_name',key)
   custom.set_editor_property('inputs',list(custom.get_editor_property('inputs'))+[pin])
   bootstrap.connect(node,custom,key)
 if spec.get('camera_vector') and 'ViewToCamera' not in names:
  node=bootstrap.node(material,unreal.MaterialExpressionCameraVectorWS)
  pin=unreal.CustomInput();pin.set_editor_property('input_name','ViewToCamera')
  custom.set_editor_property('inputs',list(custom.get_editor_property('inputs'))+[pin])
  bootstrap.connect(node,custom,'ViewToCamera')

 for kind in ('scalars','vectors','textures'):
  for name,value in spec.get(kind,{}).items():
   if name in names:continue
   if kind=='scalars':node=bootstrap.node(material,unreal.MaterialExpressionScalarParameter,parameter_name=name,default_value=float(value))
   elif kind=='vectors':node=bootstrap.node(material,unreal.MaterialExpressionVectorParameter,parameter_name=name,default_value=unreal.LinearColor(*value,1.0))
   else:node=bootstrap.node(material,unreal.MaterialExpressionTextureObjectParameter,parameter_name=name,texture=textures[value],sampler_type=lib._sampler_type(textures[value]))
   pin=unreal.CustomInput();pin.set_editor_property('input_name',name)
   custom.set_editor_property('inputs',list(custom.get_editor_property('inputs'))+[pin])
   bootstrap.connect(node,custom,name)

for name in ('M_Planet','M_Atmosphere'):
 material=lib._preflight_existing('/Game/Star/Materials/'+name,unreal.Material)
 assert material is not None
 nodes=[n for n in unreal.MaterialEditingLibrary.get_material_expressions(material) if isinstance(n,unreal.MaterialExpressionCustom) and str(n.get_editor_property('description'))=='STAR '+name[2:]]
 assert len(nodes)==1,name
 spec=lib.MATERIAL_SPECS[name]
 for parameter in unreal.MaterialEditingLibrary.get_material_expressions(material):
  if isinstance(parameter,unreal.MaterialExpressionScalarParameter):
   key=str(parameter.get_editor_property('parameter_name'))
   if key.startswith('Pacific') and key in spec['scalars']:
    parameter.set_editor_property('default_value',float(spec['scalars'][key]))
 add_missing_inputs(material,nodes[0],spec)
 material.set_editor_property('disable_depth_test',bool(spec.get('scene_depth')))
 nodes[0].set_editor_property('code',lib.shader_source(spec['shader']))
 unreal.MaterialEditingLibrary.recompile_material(material);bootstrap.save(material)

samplers={k:lib._sampler_type(v) for k,v in textures.items()}
for name in ('M_Clouds','M_CloudsSurface'):
 material=lib._preflight_existing('/Game/Star/Materials/'+name,unreal.Material)
 if material is None:
  material=unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,'/Game/Star/Materials',unreal.Material,unreal.MaterialFactoryNew())
  unreal.EditorAssetLibrary.set_metadata_tag(material,lib.OWNER_TAG,lib.GENERATOR)
 for node in list(unreal.MaterialEditingLibrary.get_material_expressions(material)):unreal.MaterialEditingLibrary.delete_material_expression(material,node)
 material.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT)
 material.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
 material.set_editor_property('two_sided',True);material.set_editor_property('use_translucency_vertex_fog',False)
 spec=lib.MATERIAL_SPECS[name]
 nodes=lib._parameter_nodes(material,spec,textures,samplers,textures)
 custom=lib._custom(material,spec['shader'],nodes,4)
 lib._property(lib._mask(material,custom,'rgb'),'MP_EMISSIVE_COLOR')
 lib._property(lib._mask(material,custom,'a'),'MP_OPACITY')
 unreal.MaterialEditingLibrary.recompile_material(material)
 lib._stamp_save(material,hashlib.sha256(lib.shader_source(spec['shader']).encode('utf-8')).hexdigest())
bootstrap.write_json(ROOT/'work/earth-v2/author-result.json',{'status':'SAVED_NOT_GPU_VALIDATED','textures':readbacks})
unreal.log('STAR Earth V2 graphs and textures saved')
# Preserve the current depth-aware/light-only registration after legacy authors.
if (ROOT/'Data/earth_realism_night.json').is_file():
 import runpy
 runpy.run_path(str(ROOT/'Tools/Unreal/apply_earth_realism.py'))
