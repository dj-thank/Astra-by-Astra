"""Read back the actual FBX files and verify geometry plus live-screen clearance."""
from pathlib import Path
import bpy,json,math,hashlib
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[3];ART=ROOT/'Art/Explorer/CockpitV3';OUT=ROOT/'Content/Star/Art/CockpitV3'
manifest=json.loads((ART/'manifest.json').read_text(encoding='utf-8'))
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
checks=[]
for p in manifest['model_parts']:
 path=OUT/p['fbx'];before=set(bpy.data.objects)
 bpy.ops.import_scene.fbx(filepath=str(path),use_custom_normals=True)
 objs=[o for o in bpy.data.objects if o not in before and o.type=='MESH']
 verts=[o.matrix_world@v.co for o in objs for v in o.data.vertices]
 finite=all(math.isfinite(c) for v in verts for c in v)
 bounds={'min':[min(v[i] for v in verts) for i in range(3)],'max':[max(v[i] for v in verts) for i in range(3)]}
 tris=0
 for o in objs:o.data.calc_loop_triangles();tris+=len(o.data.loop_triangles)
 checks.append({'part':p['name'],'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'finite':finite,'triangles':tris,'authored_triangles':p['triangles'],'triangle_count_match':tris==p['triangles'],'bounds_m':bounds,'uv_present':all(bool(o.data.uv_layers) for o in objs),'expected_meter_scale':4<bounds['min'][0]<7 and bounds['max'][0]<8.5})
deps=bpy.context.evaluated_depsgraph_get();camera=Vector((6,0,1.3));rays=[]
for idx,cy in enumerate((-.88,0,.88)):
 blocked=[]
 for u in (-.95,-.5,0,.5,.95):
  for v in (-.95,-.5,0,.5,.95):
   target=Vector((6.838,cy+u*.619/2,.747+v*.312/2));d=target-camera
   hit,loc,n,face,obj,mat=bpy.context.scene.ray_cast(deps,camera,d.normalized(),distance=d.length-.0005)
   if hit:blocked.append({'u':u,'v':v,'object':obj.name,'hit_m':list(loc)})
 rays.append({'screen':idx,'sample_count':25,'blocked':blocked})
report={'status':'LOCAL_GEOMETRY_PASS' if all(c['finite'] and c['triangle_count_match'] and c['uv_present'] and c['expected_meter_scale'] for c in checks) and all(not r['blocked'] for r in rays) else 'FAIL','fbx_readback':checks,'display_clearance':rays,'not_verified':['UE import/material recreation','packaged game images','physical input','photoreal target acceptance'],'camera_m':list(camera)}
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
glb=OUT/'STAR_CockpitV3.glb';bpy.ops.import_scene.gltf(filepath=str(glb))
objects=[o for o in bpy.context.scene.objects if o.type=='MESH'];triangles=0
for o in objects:o.data.calc_loop_triangles();triangles+=len(o.data.loop_triangles)
used_materials={m for o in objects for m in o.data.materials if m}
report['glb_readback']={'sha256':hashlib.sha256(glb.read_bytes()).hexdigest(),'mesh_objects':len(objects),'triangles':triangles,'triangle_count_match':triangles==sum(c['triangles'] for c in checks),'uv_present':all(bool(o.data.uv_layers) for o in objects),'textured_material_count':sum(any(n.type=='TEX_IMAGE' and n.image for n in m.node_tree.nodes) for m in used_materials if m.use_nodes)}
if not report['glb_readback']['triangle_count_match'] or not report['glb_readback']['uv_present']:report['status']='FAIL'
(ART/'validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report,indent=2),flush=True)
