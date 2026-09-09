"""Register a real, geographically aligned Cairo night photograph above VIIRS."""
from pathlib import Path
import hashlib,json,sys,unreal
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tools/Unreal'))
import bootstrap
from Materials import create_materials as lib
assert Path(unreal.Paths.project_dir()).resolve()==ROOT
if unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages():raise RuntimeError('Fresh dedicated commandlet required')
meta=json.loads((ROOT/'Data/earth_piloted_night_photo.json').read_text(encoding='utf-8'))
source=ROOT/'Content/Star/Art/EarthRealism/T_Cairo_ISS2023_Geographic_4K.png'
assert hashlib.sha256(source.read_bytes()).hexdigest()==meta['sha256']
asset='/Game/Star/Art/EarthRealism/'+source.stem
photo=unreal.load_asset(asset)
if photo is None or unreal.EditorAssetLibrary.get_metadata_tag(photo,'STARTextureSHA256')!=meta['sha256']:
 task=unreal.AssetImportTask()
 for k,v in {'filename':str(source),'destination_path':'/Game/Star/Art/EarthRealism','destination_name':source.stem,'automated':True,'replace_existing':True}.items():task.set_editor_property(k,v)
 unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task]);photo=unreal.load_asset(asset)
assert isinstance(photo,unreal.Texture2D)
for k,v in {'never_stream':True,'srgb':True,'lod_bias':0,'max_texture_size':4096,'compression_settings':unreal.TextureCompressionSettings.TC_BC7,'address_x':unreal.TextureAddress.TA_CLAMP,'address_y':unreal.TextureAddress.TA_CLAMP}.items():photo.set_editor_property(k,v)
unreal.EditorAssetLibrary.set_metadata_tag(photo,'STARSourceSHA256',meta['sourceSHA256'])
unreal.EditorAssetLibrary.set_metadata_tag(photo,'STARTextureSHA256',meta['sha256']);bootstrap.save(photo)
back_meta=meta['backgroundFiltering'];back_source=ROOT/back_meta['file'];assert hashlib.sha256(back_source.read_bytes()).hexdigest()==back_meta['sha256']
back_asset='/Game/Star/Art/EarthRealism/'+back_source.stem;back=unreal.load_asset(back_asset)
if back is None or unreal.EditorAssetLibrary.get_metadata_tag(back,'STARTextureSHA256')!=back_meta['sha256']:
 task=unreal.AssetImportTask()
 for k,v in {'filename':str(back_source),'destination_path':'/Game/Star/Art/EarthRealism','destination_name':back_source.stem,'automated':True,'replace_existing':True}.items():task.set_editor_property(k,v)
 unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task]);back=unreal.load_asset(back_asset)
assert isinstance(back,unreal.Texture2D)
for k,v in {'never_stream':True,'srgb':True,'max_texture_size':2048,'compression_settings':unreal.TextureCompressionSettings.TC_BC7,'filter':unreal.TextureFilter.TF_TRILINEAR}.items():back.set_editor_property(k,v)
unreal.EditorAssetLibrary.set_metadata_tag(back,'STARTextureSHA256',back_meta['sha256']);bootstrap.save(back)
material=unreal.load_asset('/Game/Star/Materials/M_Planet');assert material
customs=[n for n in unreal.MaterialEditingLibrary.get_material_expressions(material) if isinstance(n,unreal.MaterialExpressionCustom) and str(n.get_editor_property('description'))=='STAR Planet'];assert len(customs)==1
custom=customs[0];names={str(x.get_editor_property('input_name')) for x in custom.get_editor_property('inputs')}
nodes={}
for key,value in {'NightPhotoEnabled':0.0,'NightPhotoWest':30.7,'NightPhotoEast':31.7,'NightPhotoSouth':29.7,'NightPhotoNorth':30.4,'NightBackdropGain':1.0}.items():
 if key not in names:nodes[key]=bootstrap.node(material,unreal.MaterialExpressionScalarParameter,parameter_name=key,default_value=value)
if 'NightPhotoTex' not in names:nodes['NightPhotoTex']=bootstrap.node(material,unreal.MaterialExpressionTextureObjectParameter,parameter_name='NightPhotoTex',texture=photo)
for key,node in nodes.items():
 pin=unreal.CustomInput();pin.set_editor_property('input_name',key)
 custom.set_editor_property('inputs',list(custom.get_editor_property('inputs'))+[pin]);bootstrap.connect(node,custom,key)
custom.set_editor_property('code',lib.shader_source('Planet.ush'));unreal.MaterialEditingLibrary.recompile_material(material);bootstrap.save(material)
earth=unreal.load_asset('/Game/Star/Materials/MI_Earth');assert earth
unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(earth,'NightPhotoTex',photo)
unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(earth,'NightDetailTex',back)
w,s,e,n=meta['boundsWsen'];values=dict(NightPhotoEnabled=1.0,NightPhotoWest=w,NightPhotoSouth=s,NightPhotoEast=e,NightPhotoNorth=n,NightMicroStrength=0.0,NightBackdropGain=meta['backgroundDisplayMatch']['gain'])
for key,value in values.items():unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(earth,key,value)
bootstrap.save(earth)
bound=unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(earth,'NightPhotoTex');assert bound and bound.get_path_name()==photo.get_path_name()
result={'asset':bound.get_path_name(),'values':{key:unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(earth,key) for key in values},'textureSHA256':meta['sha256'],'sourceSHA256':meta['sourceSHA256'],'syntheticDetail':False,'scope':'Photographic color and street layout; approximate NASA geographic registration, not current lighting or survey accuracy'}
(ROOT/'work/scenic-flight').mkdir(parents=True,exist_ok=True)
(ROOT/'work/scenic-flight/night-photo-author.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
unreal.log('STAR registered Cairo photo ready')
