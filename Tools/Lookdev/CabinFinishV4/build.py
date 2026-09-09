"""Minimal physical support addition. Existing meshes/textures/runtime remain read-only."""
from pathlib import Path
import bpy,math,json,hashlib,os
from mathutils import Vector,Matrix
ROOT=Path(__file__).resolve().parents[3];OUT=ROOT/'Content/Star/Art/CabinFinishV4';WORK=ROOT/'work/cabin-finish-v4';CONTEXT=Path(os.environ.get('STAR_CONTEXT_ROOT',str(ROOT)))
OUT.mkdir(parents=True,exist_ok=True);WORK.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
scene=bpy.context.scene;scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=1
scene.render.threads_mode='FIXED';scene.render.threads=2
objects=[];parts=[]
def material(name,color,metal,rough):
 m=bpy.data.materials.new(name);m.use_nodes=True;m.diffuse_color=(*color,1);bs=m.node_tree.nodes['Principled BSDF'];bs.inputs['Base Color'].default_value=(*color,1);bs.inputs['Metallic'].default_value=metal;bs.inputs['Roughness'].default_value=rough;return m
alloy=material('M_CV3_BeadBlastedAlloy',(.38,.42,.44),.94,.29)
dark=material('M_CV3_AnodizedGraphite',(.052,.064,.071),.72,.36)
rubber=material('M_CV3_Elastomer',(.009,.013,.016),0,.78)
def reg(o,name,mat,bevel=.001):
 o.name=name;o.data.materials.append(mat)
 for f in o.data.polygons:f.use_smooth=True
 if bevel:
  b=o.modifiers.new('Manufactured edge','BEVEL');b.width=bevel;b.segments=2
  w=o.modifiers.new('Weighted face normals','WEIGHTED_NORMAL');w.keep_sharp=True
 objects.append(o);return o
def box(name,p,size,mat=dark,bevel=.001):
 bpy.ops.mesh.primitive_cube_add(size=1,location=p);o=bpy.context.object;o.scale=size;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);return reg(o,name,mat,bevel)
def rod(name,a,b,r,mat=alloy):
 a,b=Vector(a),Vector(b);d=b-a;bpy.ops.mesh.primitive_cylinder_add(vertices=20,radius=r,depth=d.length,location=(a+b)/2);o=bpy.context.object;o.rotation_euler=d.to_track_quat('Z','Y').to_euler();return reg(o,name,mat,.0005)
# Adhesive bedding stays entirely between the old floor top .155 and pad bottom .159.
# Only the 25 forward footwell pads are included; pads around/inside seat pedestals are excluded.
for x in (5.94,6.41,6.88,7.35,7.82):
 for y in (-1.12,-.55,0,.55,1.12):
  box('Four mm footwell pad bedding',(x,y,.157),(.378,.408,.004),rubber,.0004)
  parts.append({'role':'pad bedding','center_m':[x,y,.157],'extent_m':[.378,.408,.004],'floor_contact_z_m':.155,'pad_contact_z_m':.159,'visible_surface_not_duplicated':True})
R=Matrix.Rotation(-.4,3,'Y');supports=[]
for y in (-.28,.28):
 pivot=Vector((6.53,y,.34))+R@Vector((-.065,0,-.030))
 # U section adjustment rail occupies the existing 130 mm gap between rubber pad columns.
 box('Pedal floor rail web',(6.51,y,.158),(.48,.054,.006),alloy,.0006)
 for dy in (-.025,.025):box('Pedal rail folded return',(6.51,y+dy,.169),(.48,.004,.022),dark,.0005)
 # A short crossmember carries the two clevis plates and is supported by both rail returns.
 box('Pedal clevis crossmember',(pivot.x,y,.185),(.068,.256,.020),dark,.0012)
 for dy in (-.118,.118):
  z0=.192;z1=pivot.z+.021
  box('Pedal pivot clevis',(pivot.x,y+dy,(z0+z1)/2),(.054,.006,z1-z0),alloy,.002)
 rod('Pedal pivot spindle',(pivot.x,y-.134,pivot.z),(pivot.x,y+.134,pivot.z),.012,alloy)
 for side in (-1,1):
  yy=y+side*.132
  rod('Pivot retaining washer',(pivot.x,yy-side*.002,pivot.z),(pivot.x,yy+side*.002,pivot.z),.015,dark)
  rod('Pivot retaining head',(pivot.x,yy,pivot.z),(pivot.x,yy+side*.005,pivot.z),.008,alloy)
 for x in (6.30,6.72):
  rod('Floor rail captive anchor',(x,y,.154),(x,y,.164),.005,alloy)
  rod('Floor anchor washer',(x,y,.161),(x,y,.163),.008,dark)
 supports.append({'existing_pedal_center_m':[6.53,y,.34],'existing_pedal_size_m':[.21,.20,.055],'existing_rotation_y_rad':-.4,'spindle_center_m':list(pivot),'spindle_radius_m':.012,'spindle_contact_to_pedal_underside_m':.0095,'rail_floor_contact_z_m':.155,'rail_y_extent_m':[y-.027,y+.027],'existing_pad_column_gap_y_m':[-.34,-.21] if y<0 else [.21,.34]})
# Bake one additional mesh at ship origin, retaining metric UVs and weighted normals.
bpy.ops.object.select_all(action='DESELECT')
for o in objects:o.select_set(True)
bpy.context.view_layer.objects.active=objects[0];bpy.ops.object.convert(target='MESH');bpy.ops.object.join();o=bpy.context.object;o.name='SM_CabinFinishV4Support'
scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR');bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
uv=o.data.uv_layers.active or o.data.uv_layers.new(name='UVMap')
for f in o.data.polygons:
 axis=max(range(3),key=lambda i:abs(f.normal[i]));ij=[i for i in range(3) if i!=axis]
 for li in f.loop_indices:
  v=o.data.vertices[o.data.loops[li].vertex_index].co;uv.data[li].uv=(v[ij[0]],v[ij[1]])
tri=o.modifiers.new('Export triangles','TRIANGULATE')
if hasattr(tri,'keep_custom_normals'):tri.keep_custom_normals=True
bpy.ops.object.modifier_apply(modifier=tri.name)
o.data.calc_loop_triangles();tri_count=len(o.data.loop_triangles)
fbx=OUT/'SM_CabinFinishV4Support.fbx';bpy.ops.export_scene.fbx(filepath=str(fbx),use_selection=True,axis_forward='X',axis_up='Z',apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',object_types={'MESH'},bake_anim=False,use_tspace=True,path_mode='RELATIVE')
# Actual export readback, plus finite/UV/normal/triangle checks. No renders or engine process.
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False);bpy.ops.import_scene.fbx(filepath=str(fbx),use_custom_normals=True)
meshes=[x for x in bpy.context.scene.objects if x.type=='MESH'];verts=[x.matrix_world@v.co for x in meshes for v in x.data.vertices]
for x in meshes:x.data.calc_loop_triangles()
read_tris=sum(len(x.data.loop_triangles) for x in meshes)
bounds={'min':[min(v[i] for v in verts) for i in range(3)],'max':[max(v[i] for v in verts) for i in range(3)]}
checks={'finite_vertices':all(math.isfinite(c) for v in verts for c in v),'triangles_match':read_tris==tri_count,'under_10000_triangles':read_tris<10000,'one_mesh':len(meshes)==1,'uv_present':all(bool(x.data.uv_layers) for x in meshes),'bedding_in_4mm_existing_gap':all(abs(p['center_m'][2]-p['extent_m'][2]/2-.155)<1e-8 and abs(p['center_m'][2]+p['extent_m'][2]/2-.159)<1e-8 for p in parts),'rails_in_existing_pad_gaps':all(s['rail_y_extent_m'][0]>s['existing_pad_column_gap_y_m'][0] and s['rail_y_extent_m'][1]<s['existing_pad_column_gap_y_m'][1] for s in supports),'pedal_spindle_contacts_existing_underside':all(s['spindle_radius_m']-(.030-.0275)>.009 for s in supports)}
# Confirm .155 m physical floor and .159 m pad underside against current canonical FBX fixtures.
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
fixtures=[CONTEXT/'Content/Star/Art/ExplorerV2/Parts/SM_CockpitShell.fbx',CONTEXT/'Content/Star/Art/CockpitV3/Parts/SM_CockpitV3Interior.fbx']
fixture_objects=[]
for path in fixtures:
 before=set(bpy.data.objects);bpy.ops.import_scene.fbx(filepath=str(path),use_custom_normals=True);fixture_objects.extend(x for x in bpy.data.objects if x not in before and x.type=='MESH')
def fixture_ray(origin,direction,max_distance):
 hits=[]
 for obj in fixture_objects:
  inv=obj.matrix_world.inverted();hit,pos,normal,face=obj.ray_cast(inv@Vector(origin),inv.to_3x3()@Vector(direction),distance=max_distance)
  if hit:hits.append(obj.matrix_world@pos)
 return min(hits,key=lambda p:(p-Vector(origin)).length) if hits else None
contact_samples=[]
for x,y in ((6.30,-.28),(6.30,.28),(6.41,0),(5.94,-1.12),(7.35,.55)):
 floor=fixture_ray((x,y,.157),(0,0,-1),.01);pad=fixture_ray((x,y,.157),(0,0,1),.01) if abs(y) not in (.28,) else None
 contact_samples.append({'xy_m':[x,y],'floor_hit_z_m':floor.z if floor else None,'pad_hit_z_m':pad.z if pad else None})
checks['actual_floor_contacts']=all(p['floor_hit_z_m'] is not None and abs(p['floor_hit_z_m']-.155)<1e-5 for p in contact_samples)
checks['actual_pad_undersides']=all(p['pad_hit_z_m'] is not None and abs(p['pad_hit_z_m']-.159)<1e-5 for p in contact_samples[2:])
manifest={'status':'LOCAL_GEOMETRY_PASS' if all(checks.values()) else 'FAIL','mesh_name':'SM_CabinFinishV4Support','fbx':fbx.name,'sha256':hashlib.sha256(fbx.read_bytes()).hexdigest(),'triangles':read_tris,'bounds_m':bounds,'source_units':'meters','source_frame':{'forward':'+X','right':'+Y','up':'+Z'},'attach_location_ue_cm':[0,0,0],'attach_rotation_ue_deg':[0,0,0],'attach_scale':[1,1,1],'materials_reuse':{m.name:'/Game/Star/Art/CockpitV3/Materials/'+m.name for m in (alloy,dark,rubber)},'import':{'convert_scene':True,'convert_scene_unit':True,'force_front_x_axis':False,'import_uniform_scale':1,'transform_vertex_to_absolute':True,'import_normals':True,'recompute_tangents':'MikkTSpace','remove_degenerates':True,'import_materials':False,'import_textures':False},'pedal_supports':supports,'pad_bedding':parts,'fixture_hashes':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in fixtures},'contact_rays':contact_samples,'checks':checks,'source_use':'Original ASTER geometry inference informed by NASA mockup support/rail construction. No claim that these pedals are copied/measured from Orion. No new photo textures.','not_verified':['UE import','packaged game appearance','actual joystick','seat material quality'],'unchanged':['all existing surfaces','all source photos and textures','existing seats/harnesses','runtime code']}
(OUT/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps({'status':manifest['status'],'triangles':read_tris,'bounds_m':bounds,'checks':checks},indent=2),flush=True)
if not all(checks.values()):raise RuntimeError('Cabin support export validation failed')
