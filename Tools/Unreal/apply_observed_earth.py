"""Author the actual-raster terrain material and global coverage hole in a fresh editor."""
from pathlib import Path
import sys,json,unreal,os,hashlib,datetime
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tools/Unreal'))
import bootstrap
from Materials import create_materials as lib
assert Path(unreal.Paths.project_dir()).resolve()==ROOT
if unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages():raise RuntimeError('Fresh commandlet required')
earth=unreal.load_asset('/Game/Star/Textures/earth_day_8k');assert earth
planet=lib._preflight_existing('/Game/Star/Materials/M_Planet',unreal.Material);assert planet
custom=[n for n in unreal.MaterialEditingLibrary.get_material_expressions(planet) if isinstance(n,unreal.MaterialExpressionCustom) and str(n.get_editor_property('description'))=='STAR Planet']
assert len(custom)==1
custom=custom[0];names={str(p.get_editor_property('input_name')) for p in custom.get_editor_property('inputs')}
for name in ('EarthSurfaceEnabled','EarthSurfaceBounds','EarthSurfaceMask','TerrainWaterOnly','TerrainWaterMask','PatchUV','TerrainPatchBounds','ObservedColor','SurfaceNormalWS','PatchAxisXUE','PatchAxisYUE','PatchAxisZUE','PatchNormal'):
 if name in names:continue
 if name in ('EarthSurfaceEnabled','TerrainWaterOnly'):n=bootstrap.node(planet,unreal.MaterialExpressionScalarParameter,parameter_name=name,default_value=0.0)
 elif name=='PatchUV':n=bootstrap.node(planet,unreal.MaterialExpressionTextureCoordinate,coordinate_index=1)
 elif name=='SurfaceNormalWS':n=bootstrap.node(planet,unreal.MaterialExpressionVertexNormalWS)
 elif name=='PatchNormal':n=bootstrap.node(planet,unreal.MaterialExpressionVertexColor)
 elif name.startswith('PatchAxis'):
  axis={'PatchAxisXUE':(1,0,0),'PatchAxisYUE':(0,-1,0),'PatchAxisZUE':(0,0,1)}[name]
  n=bootstrap.node(planet,unreal.MaterialExpressionVectorParameter,parameter_name=name,default_value=unreal.LinearColor(*axis,1))
 elif name in ('EarthSurfaceBounds','TerrainPatchBounds'):n=bootstrap.node(planet,unreal.MaterialExpressionVectorParameter,parameter_name=name,default_value=unreal.LinearColor(0,0,3 if name=='EarthSurfaceBounds' else 1,1))
 else:n=bootstrap.node(planet,unreal.MaterialExpressionTextureObjectParameter,parameter_name=name,texture=earth,sampler_type=lib._sampler_type(earth))
 pin=unreal.CustomInput();pin.set_editor_property('input_name',name)
 custom.set_editor_property('inputs',list(custom.get_editor_property('inputs'))+[pin]);bootstrap.connect(n,custom,name)
before_mask=unreal.MaterialEditingLibrary.get_material_property_input_node(planet,unreal.MaterialProperty.MP_OPACITY_MASK)
before_rgb=unreal.MaterialEditingLibrary.get_material_property_input_node(planet,unreal.MaterialProperty.MP_EMISSIVE_COLOR)
before={'blend':str(planet.get_editor_property('blend_mode')),'customOutput':str(custom.get_editor_property('output_type')),'opacityInput':before_mask.get_path_name() if before_mask else None,'emissiveInput':before_rgb.get_path_name() if before_rgb else None,'emissiveChannels':{c:bool(before_rgb.get_editor_property(c)) for c in 'rgba'} if isinstance(before_rgb,unreal.MaterialExpressionComponentMask) else None,'emissiveDependencies':[n.get_path_name() if n else None for n in unreal.MaterialEditingLibrary.get_inputs_for_material_expression(planet,before_rgb)] if before_rgb else []}
(ROOT/'work/earth-time/planet-output-before.json').write_text(json.dumps(before,indent=2),encoding='utf-8')
planet.set_editor_property('blend_mode',unreal.BlendMode.BLEND_MASKED)
planet.set_editor_property('opacity_mask_clip_value',0.5)
custom.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT4)
mask=before_mask if isinstance(before_mask,unreal.MaterialExpressionComponentMask) else bootstrap.node(planet,unreal.MaterialExpressionComponentMask)
for name,value in {'r':False,'g':False,'b':False,'a':True}.items():mask.set_editor_property(name,value)
bootstrap.connect(custom,mask,'')
unreal.MaterialEditingLibrary.connect_material_property(mask,'',unreal.MaterialProperty.MP_OPACITY_MASK)
rgb=before_rgb if isinstance(before_rgb,unreal.MaterialExpressionComponentMask) and before_rgb!=mask else bootstrap.node(planet,unreal.MaterialExpressionComponentMask)
for name,value in {'r':True,'g':True,'b':True,'a':False}.items():rgb.set_editor_property(name,value)
bootstrap.connect(custom,rgb,'')
unreal.MaterialEditingLibrary.connect_material_property(rgb,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
custom.set_editor_property('code',lib.shader_source('Planet.ush'))
unreal.MaterialEditingLibrary.recompile_material(planet);bootstrap.save(planet)
path='/Game/Star/Materials/M_ObservedEarth'
material=unreal.load_asset(path)
if material:
 assert unreal.EditorAssetLibrary.get_metadata_tag(material,'StarObservedEarth')=='v1'
 for n in list(unreal.MaterialEditingLibrary.get_material_expressions(material)):unreal.MaterialEditingLibrary.delete_material_expression(material,n)
else:material=unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_ObservedEarth','/Game/Star/Materials',unreal.Material,unreal.MaterialFactoryNew())
material.set_editor_property('blend_mode',unreal.BlendMode.BLEND_MASKED)
material.set_editor_property('opacity_mask_clip_value',0.5)
material.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
material.set_editor_property('two_sided',False)
sample=bootstrap.node(material,unreal.MaterialExpressionTextureSampleParameter2D,parameter_name='ObservedColor',texture=earth,const_coordinate=0)
unreal.MaterialEditingLibrary.connect_material_property(sample,'RGB',unreal.MaterialProperty.MP_BASE_COLOR)
unreal.MaterialEditingLibrary.connect_material_property(sample,'A',unreal.MaterialProperty.MP_OPACITY_MASK)
roughness=bootstrap.node(material,unreal.MaterialExpressionConstant,r=0.85)
unreal.MaterialEditingLibrary.connect_material_property(roughness,'',unreal.MaterialProperty.MP_ROUGHNESS)
unreal.MaterialEditingLibrary.recompile_material(material)
unreal.EditorAssetLibrary.set_metadata_tag(material,'StarObservedEarth','v1');bootstrap.save(material)
result={'schemaVersion':1,'material':path,'globe':planet.get_path_name(),'beforeMaskRepair':before,'surface':'Default Lit; measured RGB reflectance scaled by documented 0.0001; no invented texture detail','height':'Observed DSM; curved 3-D mesh','mask':'Only committed terrain with observed pixels may replace globe','weather':'clear sky; no live meteorological data yet','nightDetail':'prior feathered/display-normalized patches disabled'}
assert custom.get_editor_property('code')==lib.shader_source('Planet.ush')
result.update(runId=os.environ.get('STAR_AUTHOR_RUN_ID'),authoredUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),shaderSha256=hashlib.sha256(lib.shader_source('Planet.ush').encode()).hexdigest())
(ROOT/'work/earth-time/material-author.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.log('STAR observed terrain material saved')
