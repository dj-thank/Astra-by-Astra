"""Bind archived clear coastal imagery consistently to Earth and clouds."""
from pathlib import Path
import json,unreal
ROOT=Path(__file__).resolve().parents[2]
assert Path(unreal.Paths.project_dir()).resolve()==ROOT
if unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages():raise RuntimeError('Fresh commandlet required')
name='T_Andaman_Sentinel_2016_4K'
package='/Game/Star/Art/EarthFlight/'+name
task=unreal.AssetImportTask()
for key,value in {'filename':str(ROOT/'Content/Star/Art/EarthFlight'/(name+'.png')),'destination_path':'/Game/Star/Art/EarthFlight','destination_name':name,'automated':True,'replace_existing':True}.items():task.set_editor_property(key,value)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
texture=unreal.load_asset(package)
assert isinstance(texture,unreal.Texture2D)
for key,value in {'never_stream':True,'max_texture_size':4096,'lod_bias':0,'srgb':True,'compression_settings':unreal.TextureCompressionSettings.TC_BC7,'address_x':unreal.TextureAddress.TA_CLAMP,'address_y':unreal.TextureAddress.TA_CLAMP}.items():texture.set_editor_property(key,value)
assert unreal.EditorAssetLibrary.save_loaded_asset(texture,only_if_is_dirty=False)
bounds=dict(EarthDetailWest=91.5,EarthDetailSouth=10.5,EarthDetailEast=94.5,EarthDetailNorth=13.5,EarthDetailFeatherDegrees=0.15)
for name in ('M_Planet','M_Clouds','M_CloudsSurface'):
 material=unreal.load_asset('/Game/Star/Materials/'+name)
 assert material
 for node in unreal.MaterialEditingLibrary.get_material_expressions(material):
  if isinstance(node,unreal.MaterialExpressionTextureObjectParameter) and str(node.get_editor_property('parameter_name'))=='EarthDetailTex':node.set_editor_property('texture',texture)
  if isinstance(node,unreal.MaterialExpressionScalarParameter):
   key=str(node.get_editor_property('parameter_name'))
   if key in bounds:node.set_editor_property('default_value',bounds[key])
 unreal.MaterialEditingLibrary.recompile_material(material)
 assert unreal.EditorAssetLibrary.save_loaded_asset(material,only_if_is_dirty=False)
earth=unreal.load_asset('/Game/Star/Materials/MI_Earth')
unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(earth,'EarthDetailTex',texture)
for key,value in dict(bounds,EarthDetailEnabled=1.0).items():unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(earth,key,value)
assert unreal.EditorAssetLibrary.save_loaded_asset(earth,only_if_is_dirty=False)
(ROOT/'work/earth-flight/texture-readback.json').write_text(json.dumps({'asset':texture.get_path_name(),'neverStream':bool(texture.get_editor_property('never_stream')),'maxTextureSize':int(texture.get_editor_property('max_texture_size')),'sourceRGBChanged':False,'bounds':bounds}),encoding='utf-8')
unreal.log('STAR Earth clear coast saved')
# Detailed archived night lights, registered separately from daytime imagery.
import sys
sys.path.insert(0,str(ROOT/'Tools/Unreal'))
import bootstrap
from Materials import create_materials as lib
name='T_Cairo_VIIRS_2012'
task=unreal.AssetImportTask()
for key,value in {'filename':str(ROOT/'Content/Star/Art/EarthFlight'/(name+'.png')),'destination_path':'/Game/Star/Art/EarthFlight','destination_name':name,'automated':True,'replace_existing':True}.items():task.set_editor_property(key,value)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
night=unreal.load_asset('/Game/Star/Art/EarthFlight/'+name)
assert isinstance(night,unreal.Texture2D)
for key,value in {'never_stream':True,'srgb':True,'lod_bias':0,'max_texture_size':1024,'compression_settings':unreal.TextureCompressionSettings.TC_BC7,'address_x':unreal.TextureAddress.TA_CLAMP,'address_y':unreal.TextureAddress.TA_CLAMP}.items():night.set_editor_property(key,value)
bootstrap.save(night)
material=unreal.load_asset('/Game/Star/Materials/M_Planet')
customs=[n for n in unreal.MaterialEditingLibrary.get_material_expressions(material) if isinstance(n,unreal.MaterialExpressionCustom) and str(n.get_editor_property('description'))=='STAR Planet']
assert len(customs)==1
custom=customs[0]
existing={str(pin.get_editor_property('input_name')) for pin in custom.get_editor_property('inputs')}
values={k:v for k,v in lib.MATERIAL_SPECS['M_Planet']['scalars'].items() if k.startswith('NightDetail') or k=='EarthDetailLandOnly'}
for name,value in dict(values,NightDetailTex=night).items():
 if name in existing:continue
 node=bootstrap.node(material,unreal.MaterialExpressionTextureObjectParameter,parameter_name=name,texture=night,sampler_type=lib._sampler_type(night)) if name=='NightDetailTex' else bootstrap.node(material,unreal.MaterialExpressionScalarParameter,parameter_name=name,default_value=float(value))
 pin=unreal.CustomInput();pin.set_editor_property('input_name',name)
 custom.set_editor_property('inputs',list(custom.get_editor_property('inputs'))+[pin]);bootstrap.connect(node,custom,name)
custom.set_editor_property('code',lib.shader_source('Planet.ush'))
unreal.MaterialEditingLibrary.recompile_material(material);bootstrap.save(material)
unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(earth,'NightDetailTex',night)
unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(earth,'NightDetailEnabled',1.0)
unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(earth,'EarthDetailLandOnly',1.0)
unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(earth,'EarthDetailGain',2.5)
bootstrap.save(earth)
unreal.log('STAR detailed archived night lights saved')
# Preserve the current depth-aware/light-only registration after legacy authors.
if (ROOT/'Data/earth_realism_night.json').is_file():
 import runpy
 runpy.run_path(str(ROOT/'Tools/Unreal/apply_earth_realism.py'))
