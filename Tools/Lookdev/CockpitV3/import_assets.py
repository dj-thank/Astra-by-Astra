"""ROOT ONLY: import candidate assets/materials. Does not replace actor components.
Run from root's real UE editor after integration. Never run in worker workspace.
"""
from pathlib import Path
import json,hashlib,sys,unreal
ROOT=Path(__file__).resolve().parents[3]
assert Path(unreal.Paths.project_dir()).resolve()==ROOT
DEST='/Game/Star/Art/CockpitV3';SOURCE=ROOT/'Content/Star/Art/CockpitV3'
manifest_path=SOURCE/'manifest_v04.json'
if not manifest_path.exists():manifest_path=ROOT/'Data/cockpit_v3_manifest.json'
manifest=json.loads(manifest_path.read_text(encoding='utf-8'))
if manifest.get('version',3)>=4:DEST='/Game/Star/Art/CockpitV4'
sys.path.insert(0,str(Path(__file__).resolve().parent))
from cockpit_import_fingerprint import import_fingerprint
recipe_hash=import_fingerprint(SOURCE,manifest_path,Path(__file__))
ue_expected=json.loads((SOURCE/'ue_import_expectations_v04.json').read_text(encoding='utf-8'))['parts'] if manifest.get('version',3)>=4 else {}
AT=unreal.AssetToolsHelpers.get_asset_tools();EA=unreal.EditorAssetLibrary;ME=unreal.MaterialEditingLibrary
OWNER='STAR_CockpitV4' if manifest.get('version',3)>=4 else 'STAR_CockpitV3'
def owned(path):
 a=unreal.load_asset(path)
 if a and EA.get_metadata_tag(a,'STAROwner')!=OWNER:raise RuntimeError('Unowned existing asset '+path)
 return a
def task(source,folder,name):
 t=unreal.AssetImportTask()
 for k,v in {'filename':str(source),'destination_path':folder,'destination_name':name,'automated':True,'replace_existing':owned(folder+'/'+name) is not None,'replace_existing_settings':True,'save':False}.items():t.set_editor_property(k,v)
 return t
textures={}
for filename in sorted({s[k] for s in manifest['materials'].values() for k in ('base_color_texture','roughness_texture','normal_texture') if k in s}):
 name=Path(filename).stem;t=task(SOURCE/filename,DEST+'/Textures',name)
 AT.import_asset_tasks([t]);a=unreal.load_asset(DEST+'/Textures/'+name);assert isinstance(a,unreal.Texture2D)
 normal='Normal' in name;color='BaseColor' in name
 for k,v in {'srgb':color,'compression_settings':unreal.TextureCompressionSettings.TC_NORMALMAP if normal else unreal.TextureCompressionSettings.TC_DEFAULT if color else unreal.TextureCompressionSettings.TC_MASKS,'flip_green_channel':normal,'virtual_texture_streaming':False,'lod_bias':0}.items():a.set_editor_property(k,v)
 EA.set_metadata_tag(a,'STAROwner',OWNER);EA.save_loaded_asset(a,False);textures[filename]=a
def node(m,cls,**kwargs):
 n=ME.create_material_expression(m,cls)
 for k,v in kwargs.items():n.set_editor_property(k,v)
 return n
def prop(m,n,p):assert ME.connect_material_property(n,'',getattr(unreal.MaterialProperty,p))
materials={}
for name,s in manifest['materials'].items():
 path=DEST+'/Materials/'+name;m=owned(path)
 if not m:m=AT.create_asset(name,DEST+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
 for n in list(ME.get_material_expressions(m)):ME.delete_material_expression(m,n)
 m.set_editor_property('two_sided',False);m.set_editor_property('blend_mode',unreal.BlendMode.BLEND_OPAQUE)
 m.set_editor_property('tangent_space_normal',True)
 base=node(m,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(*s['base_color_linear'],1))
 if 'base_color_texture' in s:base=node(m,unreal.MaterialExpressionTextureSample,texture=textures[s['base_color_texture']],sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
 prop(m,base,'MP_BASE_COLOR')
 prop(m,node(m,unreal.MaterialExpressionConstant,r=s['metallic']),'MP_METALLIC')
 rough=node(m,unreal.MaterialExpressionConstant,r=s['roughness'])
 if 'roughness_texture' in s:
  rough=node(m,unreal.MaterialExpressionTextureSample,texture=textures[s['roughness_texture']],sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
  uv=node(m,unreal.MaterialExpressionTextureCoordinate,u_tiling=s.get('roughness_uv_repeat',1),v_tiling=s.get('roughness_uv_repeat',1))
  assert ME.connect_material_expressions(uv,'',rough,'UVs')
 prop(m,rough,'MP_ROUGHNESS')
 if 'normal_texture' in s:
  n=node(m,unreal.MaterialExpressionTextureSample,texture=textures[s['normal_texture']],sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
  uv=node(m,unreal.MaterialExpressionTextureCoordinate,u_tiling=s.get('normal_uv_repeat',1),v_tiling=s.get('normal_uv_repeat',1));assert ME.connect_material_expressions(uv,'',n,'UVs')
  # Normal sample is unpacked [-1,1]; lerp with +Z calibrates amplitude.
  flat=node(m,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(0,0,1,1))
  lerp=node(m,unreal.MaterialExpressionLinearInterpolate,const_alpha=s.get('normal_strength',1))
  assert ME.connect_material_expressions(flat,'',lerp,'A');assert ME.connect_material_expressions(n,'',lerp,'B');prop(m,lerp,'MP_NORMAL')
 ME.recompile_material(m);EA.set_metadata_tag(m,'STAROwner',OWNER);EA.save_loaded_asset(m,False);materials[name]=m
readback=[]
editor_world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(editor_world,'Interchange.FeatureFlags.Import.FBX 0')
for p in manifest['model_parts']:
 name=p['name'];t=task(SOURCE/p['fbx'],DEST+'/Parts',name)
 opt=unreal.FbxImportUI()
 for k,v in {'automated_import_should_detect_type':False,'import_mesh':True,'import_as_skeletal':False,'import_materials':False,'import_textures':False,'import_animations':False,'mesh_type_to_import':unreal.FBXImportType.FBXIT_STATIC_MESH}.items():opt.set_editor_property(k,v)
 d=opt.get_editor_property('static_mesh_import_data')
 settings={'convert_scene':True,'force_front_x_axis':False,'convert_scene_unit':True,'import_uniform_scale':1.,'transform_vertex_to_absolute':True,'bake_pivot_in_vertex':False,'import_translation':unreal.Vector(0,0,0),'import_rotation':unreal.Rotator(0,0,0),'combine_meshes':True,'build_nanite':False,'auto_generate_collision':False,'generate_lightmap_u_vs':False,'normal_import_method':unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS,'normal_generation_method':unreal.FBXNormalGenerationMethod.MIKK_T_SPACE,'remove_degenerates':True}
 for k,v in settings.items():d.set_editor_property(k,v)
 existing=owned(DEST+'/Parts/'+name)
 if existing:
  stored=existing.get_editor_property('asset_import_data')
  if not isinstance(stored,unreal.FbxStaticMeshImportData):
   stored=unreal.FbxStaticMeshImportData(outer=existing)
   existing.set_editor_property('asset_import_data',stored)
  stored.scripted_add_filename(str(SOURCE/p['fbx']),0,'')
  for k,v in settings.items():stored.set_editor_property(k,v)
 t.set_editor_property('factory',unreal.FbxFactory())
 t.set_editor_property('options',opt);AT.import_asset_tasks([t]);a=unreal.load_asset(DEST+'/Parts/'+name);assert isinstance(a,unreal.StaticMesh)
 assert a.get_path_name() in list(t.get_editor_property('imported_object_paths')),('Mesh import returned no matching result',name)
 expected=ue_expected.get(name)
 if expected:
  assert expected['sourceSha256']==hashlib.sha256((SOURCE/p['fbx']).read_bytes()).hexdigest()
  assert a.get_num_triangles(0)==expected['expectedRenderTriangles'],('Imported geometry differs from UE topology prediction',name,a.get_num_triangles(0),expected)
 else:assert abs(a.get_num_triangles(0)-p['triangles'])<=max(1000,p['triangles']*.01)
 assert Path(a.get_editor_property('asset_import_data').get_first_filename()).resolve()==(SOURCE/p['fbx']).resolve(),('Actual import source filename mismatch',name)
 for i,slot in enumerate(a.get_editor_property('static_materials')):
  key=str(slot.get_editor_property('imported_material_slot_name'))
  if key not in materials:key=str(slot.get_editor_property('material_slot_name'))
  assert key in materials,(name,key);a.set_material(i,materials[key])
 EA.set_metadata_tag(a,'STAROwner',OWNER)
 EA.set_metadata_tag(a,'STARSourceSHA256',hashlib.sha256((SOURCE/p['fbx']).read_bytes()).hexdigest())
 EA.set_metadata_tag(a,'STARImportRecipeSHA256',recipe_hash)
 EA.save_loaded_asset(a,False)
 build=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).get_lod_build_settings(a,0)
 readback.append({'asset':a.get_path_name(),'bounds_origin_cm':str(a.get_bounds().origin),'bounds_extent_cm':str(a.get_bounds().box_extent),'material_slots':len(a.get_editor_property('static_materials')),'normal_import_method':str(a.get_editor_property('asset_import_data').get_editor_property('normal_import_method')),'normal_generation_method':str(a.get_editor_property('asset_import_data').get_editor_property('normal_generation_method')),'remove_degenerates':a.get_editor_property('asset_import_data').get_editor_property('remove_degenerates'),'render_triangles':a.get_num_triangles(0),'render_vertices':a.get_num_vertices(0),'render_uv_channels':a.get_num_tex_coords(0),'source_filename':a.get_editor_property('asset_import_data').get_first_filename(),'source_sha256':EA.get_metadata_tag(a,'STARSourceSHA256'),'distance_field_resolution_scale':build.get_editor_property('distance_field_resolution_scale'),'max_lumen_mesh_cards':build.get_editor_property('max_lumen_mesh_cards')})
(ROOT/'Art/Explorer/CockpitV3/ue_import_readback.json').write_text(json.dumps(readback,indent=2),encoding='utf-8')
unreal.log('CockpitV3 candidate assets imported. Actor replacement and actual game review remain root work.')
