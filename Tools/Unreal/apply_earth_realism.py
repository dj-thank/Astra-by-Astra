"""Depth-aware atmosphere and registered light-only night imagery."""
from pathlib import Path
import hashlib,json,sys,unreal
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tools/Unreal'))
import bootstrap
from Materials import create_materials as lib
assert Path(unreal.Paths.project_dir()).resolve()==ROOT
if unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages():raise RuntimeError('Fresh dedicated commandlet required')
meta=json.loads((ROOT/'Data/earth_realism_night.json').read_text(encoding='utf-8'))
source=ROOT/'Content/Star/Art/EarthRealism/T_Nile_NightLights2016_2K.png'
assert hashlib.sha256(source.read_bytes()).hexdigest()==meta['sha256']
package='/Game/Star/Art/EarthRealism/'+source.stem
night=unreal.load_asset(package)
if night is None:
 task=unreal.AssetImportTask()
 for key,value in {'filename':str(source),'destination_path':'/Game/Star/Art/EarthRealism','destination_name':source.stem,'automated':True,'replace_existing':False}.items():task.set_editor_property(key,value)
 unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task]);night=unreal.load_asset(package)
assert isinstance(night,unreal.Texture2D)
for key,value in {'never_stream':True,'srgb':True,'lod_bias':0,'max_texture_size':2048,'compression_settings':unreal.TextureCompressionSettings.TC_BC7,'address_x':unreal.TextureAddress.TA_CLAMP,'address_y':unreal.TextureAddress.TA_CLAMP}.items():night.set_editor_property(key,value)
unreal.EditorAssetLibrary.set_metadata_tag(night,'StarEarthRealismSourceSHA',meta['sha256']);bootstrap.save(night)
readback={}
for name in ('M_Planet','M_Atmosphere'):
 material=unreal.load_asset('/Game/Star/Materials/'+name);assert material
 customs=[n for n in unreal.MaterialEditingLibrary.get_material_expressions(material) if isinstance(n,unreal.MaterialExpressionCustom) and str(n.get_editor_property('description'))=='STAR '+name[2:]]
 assert len(customs)==1,name
 custom=customs[0];names={str(pin.get_editor_property('input_name')) for pin in custom.get_editor_property('inputs')}
 def connect_missing(key,node):
  if key in names:return
  pin=unreal.CustomInput();pin.set_editor_property('input_name',key)
  custom.set_editor_property('inputs',list(custom.get_editor_property('inputs'))+[pin]);bootstrap.connect(node,custom,key);names.add(key)
 wanted={'NightDetailIsLuminance':0.0,'NightMicroStrength':0.0,'NightPhotoEnabled':0.0,'NightPhotoWest':30.7,'NightPhotoEast':31.7,'NightPhotoSouth':29.7,'NightPhotoNorth':30.4,'NightBackdropGain':1.0} if name=='M_Planet' else {'RenderRadiusCm':637100840.0}
 for key,value in wanted.items():
  if key not in names:connect_missing(key,bootstrap.node(material,unreal.MaterialExpressionScalarParameter,parameter_name=key,default_value=value))
 if name=='M_Atmosphere':
  if not {'SceneDepthCm','PixelDepthCm','PixelDistanceCm'}.issubset(names):
   for key,node in lib.scene_depth_nodes(material).items():connect_missing(key,node)
  material.set_editor_property('disable_depth_test',True)
 else:
  if 'NightPhotoTex' not in names:connect_missing('NightPhotoTex',bootstrap.node(material,unreal.MaterialExpressionTextureObjectParameter,parameter_name='NightPhotoTex',texture=night))
  for node in unreal.MaterialEditingLibrary.get_material_expressions(material):
   if isinstance(node,unreal.MaterialExpressionTextureObjectParameter) and str(node.get_editor_property('parameter_name'))=='NightDetailTex':node.set_editor_property('texture',night)
 custom.set_editor_property('code',lib.shader_source(lib.MATERIAL_SPECS[name]['shader']))
 unreal.MaterialEditingLibrary.recompile_material(material);bootstrap.save(material)
 readback[name]={'disableDepthTest':bool(material.get_editor_property('disable_depth_test')),'customInputs':sorted(names),'shaderSHA256':hashlib.sha256(lib.shader_source(lib.MATERIAL_SPECS[name]['shader']).encode('utf-8')).hexdigest()}
earth=unreal.load_asset('/Game/Star/Materials/MI_Earth');assert earth
unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(earth,'NightDetailTex',night)
w,s,e,n=meta['boundsWsen']
values=dict(NightDetailEnabled=1.0,NightDetailWest=w,NightDetailSouth=s,NightDetailEast=e,NightDetailNorth=n,NightDetailIsLuminance=1.0,NightMicroStrength=0.0)
for key,value in values.items():unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(earth,key,value)
bootstrap.save(earth)
readback['night']={'asset':night.get_path_name(),'values':values,'sourceSHA':meta['sha256'],'type':'opaque display grayscale, not calibrated radiance','addedSyntheticDetail':False,'displayTint':'Uniform warm display tint; not observed light colors'}
(ROOT/'work/earth-realism').mkdir(parents=True,exist_ok=True)
(ROOT/'work/earth-realism/author-result.json').write_text(json.dumps(readback,indent=2),encoding='utf-8')
unreal.log('STAR depth-aware atmosphere and light-only night product saved')
